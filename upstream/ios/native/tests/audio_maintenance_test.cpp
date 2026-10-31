#include "g1/AudioMaintenance.hpp"

#include <fstream>
#include <iostream>
#include <sstream>
#include <stdexcept>
#include <vector>

namespace {
std::vector<std::int32_t> numbers(const std::string& text) {
    std::istringstream input(text);
    std::vector<std::int32_t> result;
    std::int32_t value;
    while (input >> value) result.push_back(value);
    return result;
}

std::vector<std::int32_t> run(const std::vector<std::int32_t>& values) {
    if (values.size() != 6) throw std::runtime_error("maintenance input shape");
    const auto site = values[0] == 0 ? g1::MusicMaintenanceSite::map_update :
                                     g1::MusicMaintenanceSite::ordinary_battle;
    const auto plan = g1::plan_music_maintenance(
        {site, values[1] != 0, values[4], values[5]}, values[3]);
    std::vector<std::int32_t> output;
    if (plan.query_playing) {
        output.push_back(6); output.push_back(0);
        if (values[2] == 0 && plan.when_idle) {
            output.push_back(plan.when_idle->operation);
            output.push_back(plan.when_idle->argument);
        }
    }
    output.insert(output.begin(), static_cast<std::int32_t>(output.size() / 2));
    return output;
}
} // namespace

int main(int argc, char** argv) {
    try {
        std::ifstream file;
        std::istream* input = &std::cin;
        if (argc == 2) {
            file.open(argv[1]);
            if (!file) throw std::runtime_error("maintenance fixture missing");
            input = &file;
        } else if (argc != 1) throw std::runtime_error("usage: audio_maintenance_test [fixture]");
        std::size_t cases = 0, checks = 0;
        std::string line;
        while (std::getline(*input, line)) {
            if (line.empty() || line[0] == '#') continue;
            const auto bar = line.find('|');
            const auto actual = run(numbers(line.substr(0, bar)));
            ++cases;
            if (bar != std::string::npos) {
                if (actual != numbers(line.substr(bar + 1)))
                    throw std::runtime_error("maintenance original mismatch case " + std::to_string(cases));
                checks += actual.size();
            } else {
                for (std::size_t i = 0; i < actual.size(); ++i)
                    std::cout << (i ? " " : "") << actual[i];
                std::cout << '\n';
            }
        }
        if (argc == 2) {
            if (cases == 0) throw std::runtime_error("empty maintenance fixture");
            std::cout << cases << " original music-maintenance cases / " << checks
                      << " scalar comparisons passed\n";
        }
    } catch (const std::exception& error) { std::cerr << error.what() << '\n'; return 1; }
}
