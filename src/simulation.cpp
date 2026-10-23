#include "simulation.hpp"
#include "counting_semaphore.hpp"

#include <algorithm>
#include <atomic>
#include <charconv>
#include <exception>
#include <istream>
#include <mutex>
#include <ostream>
#include <queue>
#include <random>
#include <stdexcept>
#include <string>
#include <system_error>
#include <thread>

namespace dungeon {
namespace {

int read_integer(std::istream& input, const char* name) {
    std::string token;
    if (!(input >> token)) {
        throw std::invalid_argument(std::string("Missing input: ") + name);
    }
    const char* first = token.data();
    const char* last = first + token.size();
    if (first != last && *first == '+') {
        ++first;
        // Do not accidentally accept a sign sequence such as '+-1'.
        if (first == last || *first < '0' || *first > '9') {
            throw std::invalid_argument(std::string("Invalid integer for ") + name);
        }
    }
    int value = 0;
    const auto parsed = std::from_chars(first, last, value);
    if (parsed.ec == std::errc::result_out_of_range) {
        throw std::invalid_argument(std::string("Integer out of range for ") + name);
    }
    if (parsed.ec != std::errc{} || parsed.ptr != last) {
        throw std::invalid_argument(std::string("Invalid integer for ") + name);
    }
    return value;
}

} // namespace

Config read_config(std::istream& input) {
    Config config{};
    config.instance_count = read_integer(input, "n");
    config.tank_count = read_integer(input, "t");
    config.healer_count = read_integer(input, "h");
    config.dps_count = read_integer(input, "d");
    config.min_seconds = read_integer(input, "t1");
    config.max_seconds = read_integer(input, "t2");
    validate_config(config);
    return config;
}

void validate_config(const Config& config) {
    if (config.instance_count < 1) {
        throw std::invalid_argument("n must be at least 1");
    }
    if (config.tank_count < 0 || config.healer_count < 0 || config.dps_count < 0) {
        throw std::invalid_argument("t, h, and d must be nonnegative");
    }
    if (config.min_seconds < 0 || config.min_seconds > config.max_seconds ||
        config.max_seconds > 15) {
        throw std::invalid_argument("Duration bounds must satisfy 0 <= t1 <= t2 <= 15");
    }
}

Summary simulate(const Config& config, const Options& options) {
    validate_config(config);
    const int party_count = std::min({config.tank_count, config.healer_count,
                                      config.dps_count / 3});
    Summary summary;
    summary.unassigned_tanks = config.tank_count - party_count;
    summary.unassigned_healers = config.healer_count - party_count;
    summary.unassigned_dps = config.dps_count - 3 * party_count;
    summary.instances.reserve(static_cast<std::size_t>(config.instance_count));
    for (int index = 0; index < config.instance_count; ++index) {
        summary.instances.push_back({index + 1, std::nullopt, 0, 0});
    }

    std::mt19937 random(options.seed ? *options.seed : std::random_device{}());
    std::uniform_int_distribution<int> duration(config.min_seconds, config.max_seconds);
    std::queue<std::optional<Party>> jobs;
    // All players are present at startup. Sequential role IDs are implicit FIFO
    // queues: consume their heads without allocating unused leftover players.
    for (int index = 0; index < party_count; ++index) {
        jobs.push(Party{index + 1, index + 1, index + 1,
                        {3 * index + 1, 3 * index + 2, 3 * index + 3}, duration(random)});
    }
    for (int index = 0; index < config.instance_count; ++index) {
        jobs.push(std::nullopt); // Shutdown markers follow every real party.
    }

    CountingSemaphore available_jobs;
    available_jobs.release(jobs.size()); // Exactly one permit per queued item.
    std::mutex queue_mutex;
    std::mutex status_mutex;
    std::atomic<bool> cancelled{false};
    std::exception_ptr worker_error;
    const auto sleep = options.sleep ? options.sleep :
        std::function<void(std::chrono::seconds)>([](std::chrono::seconds seconds) {
            std::this_thread::sleep_for(seconds);
        });

    // Initial/final calls run outside the worker lifetime; start/finish calls
    // hold status_mutex so every snapshot and its output are coherent.
    const auto observe = [&](Event event, int instance_id, std::optional<Party> party) {
        if (options.observe) {
            options.observe(Snapshot{event, instance_id, party, summary.instances});
        }
    };
    observe(Event::initial, 0, std::nullopt);

    const auto worker = [&](int index) {
        try {
            for (;;) {
                available_jobs.acquire(); // No queue/status mutex held here.
                if (cancelled.load()) {
                    return;
                }
                std::optional<Party> party;
                {
                    std::lock_guard<std::mutex> lock(queue_mutex);
                    party = jobs.front();
                    jobs.pop();
                }
                if (!party) {
                    return;
                }
                {
                    std::lock_guard<std::mutex> lock(status_mutex);
                    summary.instances[static_cast<std::size_t>(index)].active_party = party;
                    observe(Event::started, index + 1, party);
                }
                sleep(std::chrono::seconds(party->duration_seconds));
                {
                    std::lock_guard<std::mutex> lock(status_mutex);
                    auto& instance = summary.instances[static_cast<std::size_t>(index)];
                    instance.active_party.reset();
                    ++instance.parties_served;
                    instance.total_seconds += static_cast<std::uint64_t>(party->duration_seconds);
                    ++summary.completed_parties;
                    observe(Event::finished, index + 1, party);
                }
            }
        } catch (...) {
            // Preserve errors from callbacks, join all threads, then report them
            // on the caller's thread instead of invoking std::terminate.
            cancelled.store(true);
            std::lock_guard<std::mutex> lock(status_mutex);
            if (!worker_error) {
                worker_error = std::current_exception();
            }
        }
    };

    std::vector<std::thread> workers;
    workers.reserve(static_cast<std::size_t>(config.instance_count));
    try {
        for (int index = 0; index < config.instance_count; ++index) {
            workers.emplace_back(worker, index);
        }
    } catch (...) {
        // Even partial thread creation must not destroy joinable threads.
        cancelled.store(true);
        for (auto& thread : workers) {
            thread.join();
        }
        throw;
    }
    for (auto& thread : workers) {
        thread.join();
    }
    if (worker_error) {
        std::rethrow_exception(worker_error);
    }
    observe(Event::final, 0, std::nullopt);
    return summary;
}

void print_snapshot(std::ostream& output, const Snapshot& snapshot) {
    switch (snapshot.event) {
    case Event::initial:
        output << "\nInitial status\n";
        break;
    case Event::started:
        output << "\nParty " << snapshot.party->id << " started in instance "
               << snapshot.instance_id << '\n';
        break;
    case Event::finished:
        output << "\nParty " << snapshot.party->id << " finished in instance "
               << snapshot.instance_id << '\n';
        break;
    case Event::final:
        output << "\nFinal status\n";
        break;
    }
    for (const auto& instance : snapshot.instances) {
        output << "Instance " << instance.id << ": ";
        if (instance.active_party) {
            const auto& party = *instance.active_party;
            output << "active | Party " << party.id << " | Tank T" << party.tank_id
                   << " | Healer H" << party.healer_id << " | DPS D" << party.dps_ids[0]
                   << ", D" << party.dps_ids[1] << ", D" << party.dps_ids[2]
                   << " | Duration " << party.duration_seconds << " s\n";
        } else {
            output << "empty\n";
        }
    }
    output.flush();
}

void print_summary(std::ostream& output, const Summary& summary) {
    output << "\nSummary\n";
    for (const auto& instance : summary.instances) {
        output << "Instance " << instance.id << ": parties served = " << instance.parties_served
               << ", total time served = " << instance.total_seconds << " s\n";
    }
    output << "Total parties completed: " << summary.completed_parties << '\n'
           << "Unassigned players: tanks = " << summary.unassigned_tanks
           << ", healers = " << summary.unassigned_healers
           << ", DPS = " << summary.unassigned_dps << '\n';
}

} // namespace dungeon
