#include "g1/Movement.hpp"

#include <cstdint>
#include <fstream>
#include <iostream>
#include <limits>
#include <stdexcept>
#include <string>

namespace {
int read_int(std::istream& input) {
    int value;
    if (!(input >> value)) throw std::runtime_error("Truncated movement oracle fixture");
    return value;
}

void equal(int actual, int expected, const std::string& where) {
    if (actual != expected)
        throw std::runtime_error(where + ": actual " + std::to_string(actual) +
                                 ", original " + std::to_string(expected));
}

template<class Function> void rejects(Function&& function, const char* name) {
    bool caught = false;
    try { function(); } catch (const std::invalid_argument&) { caught = true; }
    if (!caught) throw std::runtime_error(std::string("Malformed-input check failed: ") + name);
}
}

int main(int argc, char** argv) {
    try {
        if (argc != 2) throw std::runtime_error("Pass movement_cases.txt as the only argument");
        std::ifstream input(argv[1]);
        std::string magic;
        if (!(input >> magic) || magic != "G1_MOVEMENT_ORACLE_V1")
            throw std::runtime_error("Not a movement oracle fixture");
        const int count = read_int(input);
        if (count <= 0 || count > 10000) throw std::runtime_error("Invalid fixture count");
        std::size_t comparisons = 0;
        for (int n = 0; n < count; ++n) {
            std::string name;
            if (!(input >> name)) throw std::runtime_error("Missing case name");
            g1::MovementBoard board;
            board.width = static_cast<std::uint16_t>(read_int(input));
            board.height = static_cast<std::uint16_t>(read_int(input));
            const int x = read_int(input), y = read_int(input);
            g1::MovementProfile profile;
            profile.unit_type = static_cast<std::uint8_t>(read_int(input));
            profile.owner = static_cast<std::uint8_t>(read_int(input));
            profile.base_budget = static_cast<std::uint8_t>(read_int(input));
            profile.spent_budget = static_cast<std::uint8_t>(read_int(input));
            profile.traversal_field6 = static_cast<std::int8_t>(read_int(input));
            const int original_form = read_int(input);
            (void)original_form; // The C++ profile already contains this form's decoded stats.
            const auto cells = static_cast<std::size_t>(board.width) * board.height;
            board.cells.resize(cells);
            for (auto& cell : board.cells) {
                cell.terrain_cost = static_cast<std::uint8_t>(read_int(input));
                cell.terrain_field2 = static_cast<std::uint8_t>(read_int(input));
                cell.active_owner = static_cast<std::int16_t>(read_int(input));
            }
            const int point_count = read_int(input);
            for (int i = 0; i < point_count; ++i)
                board.points.push_back({read_int(input), read_int(input), read_int(input)});
            const int region_count = read_int(input);
            for (int i = 0; i < region_count; ++i)
                board.cost99_regions.push_back({read_int(input), read_int(input),
                                                read_int(input), read_int(input)});
            equal(g1::remaining_movement_budget(profile), read_int(input), name + "/budget");
            const auto result = g1::compute_movement_range(board, profile, x, y);
            equal(result.has_unoccupied_destination, read_int(input), name + "/vacant");
            comparisons += 2;
            for (std::size_t i = 0; i < cells; ++i) {
                equal(result.remaining[i], read_int(input), name + "/range[" + std::to_string(i) + "]");
                ++comparisons;
            }
            for (std::size_t i = 0; i < cells; ++i) {
                equal(g1::movement_entry_cost(board, profile, static_cast<int>(i % board.width),
                                               static_cast<int>(i / board.width)),
                      read_int(input), name + "/cost[" + std::to_string(i) + "]");
                ++comparisons;
            }
            const int outside_x[4] = {-1, 0, board.width, 0};
            const int outside_y[4] = {0, -1, 0, board.height};
            for (int i = 0; i < 4; ++i) {
                equal(g1::movement_entry_cost(board, profile, outside_x[i], outside_y[i]),
                      read_int(input), name + "/outside[" + std::to_string(i) + "]");
                ++comparisons;
            }
        }
        std::string trailing;
        if (input >> trailing) throw std::runtime_error("Unexpected fixture trailing data");

        // API input validation is additional native behavior, not original-code evidence.
        g1::MovementProfile profile;
        g1::MovementBoard bad;
        rejects([&] { g1::compute_movement_range(bad, profile, 0, 0); }, "zero dimensions");
        bad.width = bad.height = 1;
        rejects([&] { g1::compute_movement_range(bad, profile, 0, 0); }, "missing cells");
        bad.cells.resize(1);
        rejects([&] { g1::compute_movement_range(bad, profile, -1, 0); }, "invalid origin");
        bad.cost99_regions.push_back({std::numeric_limits<std::int32_t>::max(), 0, 1, 1});
        rejects([&] { g1::compute_movement_range(bad, profile, 0, 0); }, "region overflow");
        std::cout << "Passed " << count << " original-execution cases, " << comparisons
                  << " scalar comparisons, and 4 separate malformed-input checks.\n";
        return 0;
    } catch (const std::exception& error) {
        std::cerr << error.what() << '\n';
        return 1;
    }
}
