#include <algorithm>
#include <array>
#include <charconv>
#include <chrono>
#include <condition_variable>
#include <cstdint>
#include <deque>
#include <exception>
#include <iostream>
#include <limits>
#include <mutex>
#include <optional>
#include <random>
#include <semaphore>
#include <sstream>
#include <stdexcept>
#include <string>
#include <string_view>
#include <thread>
#include <utility>
#include <vector>

struct Config {
    int instances, tanks, healers, dps, minimum, maximum;
};

// Read complete tokens so values such as "2x" cannot be partly accepted.
Config read_config() {
    std::array<int, 6> values{};
    std::size_t count = 0;
    std::string line;
    while (count < values.size()) {
        if (!std::getline(std::cin, line)) {
            throw std::runtime_error("Expected six integers; input ended early or could not be read.");
        }
        std::istringstream tokens(line);
        std::string token;
        while (tokens >> token) {
            if (count == values.size()) {
                throw std::runtime_error("Expected exactly six integers.");
            }
            std::string_view number(token);
            if (!number.empty() && number.front() == '+') {
                number.remove_prefix(1);
                if (!number.empty() && number.front() == '-') {
                    throw std::runtime_error("Invalid integer: " + token);
                }
            }
            int value = 0;
            const auto [end, error] = std::from_chars(number.data(), number.data() + number.size(), value);
            if (number.empty() || error != std::errc{} || end != number.data() + number.size()) {
                throw std::runtime_error("Invalid or out-of-range integer: " + token);
            }
            values[count++] = value;
        }
    }
    Config config{values[0], values[1], values[2], values[3], values[4], values[5]};
    if (config.instances <= 0) {
        throw std::runtime_error("n must be positive.");
    }
    if (config.instances > std::counting_semaphore<>::max()) {
        throw std::runtime_error("n exceeds this implementation's semaphore capacity.");
    }
    if (config.tanks < 0 || config.healers < 0 || config.dps < 0) {
        throw std::runtime_error("Player counts must be nonnegative.");
    }
    if (config.minimum < 0 || config.minimum > config.maximum || config.maximum > 15) {
        throw std::runtime_error("Durations must satisfy 0 <= t1 <= t2 <= 15.");
    }
    return config;
}

// Move-only ownership: each successful acquire has exactly one release, even
// when job construction, admission, or execution exits through an exception.
class Permit {
public:
    explicit Permit(std::counting_semaphore<>& semaphore) : semaphore_(&semaphore) {
        semaphore_->acquire();
    }
    Permit(Permit&& other) noexcept : semaphore_(std::exchange(other.semaphore_, nullptr)) {}
    Permit(const Permit&) = delete;
    Permit& operator=(const Permit&) = delete;
    Permit& operator=(Permit&&) = delete;
    ~Permit() {
        if (semaphore_) {
            semaphore_->release();
        }
    }

private:
    std::counting_semaphore<>* semaphore_;
};

struct Party {
    int id;
    int duration;
    // Role queues are implicit ranges: T1..Tt, H1..Hh, D1..Dd.
    int tank() const { return id; }
    int healer() const { return id; }
    int first_dps() const { return 3 * (id - 1) + 1; }
};

struct Instance {
    std::optional<Party> party;
    std::uint64_t served = 0;
    std::uint64_t seconds = 0;
};

struct Job {
    std::size_t instance;
    Party party;
    Permit permit;
};

class Simulator {
public:
    explicit Simulator(Config config)
        : config_(config), party_count_(std::min({config.tanks, config.healers, config.dps / 3})),
          permits_(config.instances), instances_(static_cast<std::size_t>(config.instances)) {}

    void run() {
        started_ = std::chrono::steady_clock::now();
        std::cout << "Possible parties: " << party_count_
                  << "; unmatched players: tanks=" << config_.tanks - party_count_
                  << " healers=" << config_.healers - party_count_
                  << " DPS=" << config_.dps - 3 * party_count_ << '\n';
        std::cout << "[0 ms] Initial instance status\n";
        print_status();

        try {
            const int worker_count = std::min(config_.instances, party_count_);
            workers_.reserve(static_cast<std::size_t>(worker_count));
            for (int i = 0; i < worker_count; ++i) {
                workers_.emplace_back(&Simulator::worker, this);
            }

            std::mt19937 random(std::random_device{}());
            std::uniform_int_distribution<int> duration(config_.minimum, config_.maximum);
            // Only this dispatcher admits parties. Semaphore wake-up order never
            // determines admission order or the choice of players.
            for (int index = 0; index < party_count_; ++index) {
                Permit permit(permits_); // Never wait for a permit holding mutex_.
                {
                    std::lock_guard lock(mutex_);
                    if (worker_error_) {
                        std::rethrow_exception(worker_error_);
                    }
                    const auto empty = std::find_if(instances_.begin(), instances_.end(),
                        [](const Instance& instance) { return !instance.party; });
                    if (empty == instances_.end()) {
                        throw std::logic_error("Permit acquired without an empty instance.");
                    }
                    const auto instance = static_cast<std::size_t>(empty - instances_.begin());
                    Party party{index + 1, duration(random)};
                    jobs_.push_back(Job{instance, party, std::move(permit)});
                    empty->party = party;
                    print_event("START", instance, party);
                    print_status();
                }
                work_ready_.notify_one();
            }
            {
                std::unique_lock lock(mutex_);
                finished_.wait(lock, [this] { return completed_ == party_count_ || worker_error_; });
                if (worker_error_) {
                    std::rethrow_exception(worker_error_);
                }
            }
        } catch (...) {
            stop_and_join();
            throw;
        }
        stop_and_join();
        print_summary();
    }

private:
    void worker() {
        for (;;) {
            std::optional<Job> job;
            {
                std::unique_lock lock(mutex_);
                work_ready_.wait(lock, [this] { return stopping_ || !jobs_.empty(); });
                if (jobs_.empty()) {
                    return;
                }
                job.emplace(std::move(jobs_.front()));
                jobs_.pop_front();
            }
            try {
                std::this_thread::sleep_for(std::chrono::seconds(job->party.duration));
                std::lock_guard lock(mutex_);
                auto& instance = instances_[job->instance];
                instance.party.reset();
                ++instance.served;
                instance.seconds += static_cast<std::uint64_t>(job->party.duration);
                ++completed_;
                print_event("FINISH", job->instance, job->party);
                print_status();
            } catch (...) {
                // Preserve clean shutdown even if a worker encounters an error.
                std::lock_guard lock(mutex_);
                instances_[job->instance].party.reset();
                if (!worker_error_) {
                    worker_error_ = std::current_exception();
                }
            }
            // Mark the instance empty before returning its permit. The job owns
            // the permit both while queued and while running.
            job.reset();
            finished_.notify_one();
        }
    }

    void stop_and_join() {
        {
            std::lock_guard lock(mutex_);
            stopping_ = true;
        }
        work_ready_.notify_all();
        for (auto& thread : workers_) {
            if (thread.joinable()) {
                thread.join();
            }
        }
    }

    void print_party(const Party& party) const {
        std::cout << "party=" << party.id << " tank=T" << party.tank()
                  << " healer=H" << party.healer() << " DPS=D" << party.first_dps()
                  << ",D" << party.first_dps() + 1 << ",D" << party.first_dps() + 2
                  << " duration=" << party.duration << "s";
    }

    void print_event(const char* event, std::size_t instance, const Party& party) const {
        const auto elapsed = std::chrono::duration_cast<std::chrono::milliseconds>(
            std::chrono::steady_clock::now() - started_).count();
        std::cout << '[' << elapsed << " ms] " << event << " instance=" << instance + 1 << ' ';
        print_party(party);
        std::cout << '\n';
    }

    // Called under mutex_, or before threads start / after they have all joined.
    void print_status() const {
        for (std::size_t i = 0; i < instances_.size(); ++i) {
            std::cout << "  Instance " << i + 1 << ": ";
            if (instances_[i].party) {
                std::cout << "active ";
                print_party(*instances_[i].party);
            } else {
                std::cout << "empty";
            }
            std::cout << '\n';
        }
        std::cout << std::flush;
    }

    void print_summary() const {
        std::cout << "Final summary\n";
        for (std::size_t i = 0; i < instances_.size(); ++i) {
            std::cout << "Instance " << i + 1 << ": parties served=" << instances_[i].served
                      << ", total time served=" << instances_[i].seconds << "s\n";
        }
        std::cout << "Total completed parties: " << completed_
                  << "\nUnmatched tanks: " << config_.tanks - party_count_
                  << "\nUnmatched healers: " << config_.healers - party_count_
                  << "\nUnmatched DPS: " << config_.dps - 3 * party_count_ << '\n';
    }

    Config config_;
    int party_count_;
    std::counting_semaphore<> permits_;
    std::vector<Instance> instances_;
    std::deque<Job> jobs_;
    std::vector<std::thread> workers_;
    std::mutex mutex_;
    std::condition_variable work_ready_;
    std::condition_variable finished_;
    bool stopping_ = false;
    int completed_ = 0;
    std::exception_ptr worker_error_;
    std::chrono::steady_clock::time_point started_;
};

int main() {
    std::cout << "Enter n t h d t1 t2 (six integers):\n" << std::flush;
    try {
        Simulator simulator(read_config());
        simulator.run();
        return 0;
    } catch (const std::exception& error) {
        std::cerr << "Error: " << error.what() << '\n';
        return 1;
    }
}
