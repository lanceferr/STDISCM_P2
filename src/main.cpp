#include "simulation.hpp"

#include <exception>
#include <iostream>

int main() {
    try {
        std::cout << "Enter six integers: n t h d t1 t2\n"
                     "(instances, tanks, healers, DPS, minimum seconds, maximum seconds)\n> "
                  << std::flush;
        const auto config = dungeon::read_config(std::cin);
        dungeon::Options options;
        options.observe = [](const dungeon::Snapshot& snapshot) {
            dungeon::print_snapshot(std::cout, snapshot);
        };
        const auto summary = dungeon::simulate(config, options);
        dungeon::print_summary(std::cout, summary);
        return 0;
    } catch (const std::exception& error) {
        std::cerr << "Error: " << error.what() << '\n';
        return 1;
    }
}

