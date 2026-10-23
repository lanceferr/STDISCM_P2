#pragma once

#include <array>
#include <chrono>
#include <cstdint>
#include <functional>
#include <iosfwd>
#include <optional>
#include <vector>

namespace dungeon {

struct Config {
    int instance_count;
    int tank_count;
    int healer_count;
    int dps_count;
    int min_seconds;
    int max_seconds;
};

struct Party {
    int id;
    int tank_id;
    int healer_id;
    std::array<int, 3> dps_ids;
    int duration_seconds;
};

struct InstanceStatus {
    int id;
    std::optional<Party> active_party;
    int parties_served = 0;
    std::uint64_t total_seconds = 0;
};

enum class Event { initial, started, finished, final };

struct Snapshot {
    Event event;
    int instance_id; // Zero for initial/final snapshots.
    std::optional<Party> party; // The party involved in a start/finish event.
    std::vector<InstanceStatus> instances;
};

struct Summary {
    std::vector<InstanceStatus> instances;
    int completed_parties = 0;
    int unassigned_tanks = 0;
    int unassigned_healers = 0;
    int unassigned_dps = 0;
};

struct Options {
    std::optional<std::uint32_t> seed;
    // Called concurrently by workers; a custom function must be thread-safe.
    // If absent, the simulation uses std::this_thread::sleep_for.
    std::function<void(std::chrono::seconds)> sleep;
    // Called serially under the status mutex. Keep observers short and do not
    // wait for other workers here. Copy the snapshot if retaining it.
    std::function<void(const Snapshot&)> observe;
};

Config read_config(std::istream& input);
void validate_config(const Config& config);
Summary simulate(const Config& config, const Options& options = {});
void print_snapshot(std::ostream& output, const Snapshot& snapshot);
void print_summary(std::ostream& output, const Summary& summary);

} // namespace dungeon

