#include "g1/MenuMusic.hpp"

#include <array>
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
    if (values.size() != 16) throw std::runtime_error("menu music input shape");
    std::array<g1::MenuMusicNode, 8> nodes{};
    const auto node = [&](std::int32_t index) -> const g1::MenuMusicNode* {
        if (index < 0 || index > 8) throw std::runtime_error("menu node outside fixture");
        return index == 0 ? nullptr : &nodes[static_cast<std::size_t>(index - 1)];
    };
    for (std::size_t i = 0; i < nodes.size(); ++i) nodes[i].parent = node(values[8 + i]);
    // Reject a malformed synthetic fixture before calling the original-domain
    // helper. The native UI uses a fixed acyclic menu graph.
    for (std::size_t n = 0; n < nodes.size(); ++n) {
        const auto* cursor = &nodes[n];
        std::size_t depth = 0;
        while (cursor) {
            if (++depth > nodes.size()) throw std::runtime_error("cyclic menu fixture");
            cursor = cursor->parent;
        }
    }
    const g1::MenuMusicContext context{values[0] != 0, values[2], node(values[3]),
                                      node(values[4]), node(values[5]),
                                      node(values[6]), node(values[7])};
    std::vector<std::int32_t> output;
    if (g1::menu_music_queries_playing(context)) {
        output.push_back(6); output.push_back(0);
        if (values[1] == 0) if (const auto request = g1::idle_menu_music(context)) {
            output.push_back(request->operation); output.push_back(request->argument);
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
            if (!file) throw std::runtime_error("menu music fixture missing");
            input = &file;
        } else if (argc != 1) throw std::runtime_error("usage: menu_music_test [fixture]");
        std::size_t cases = 0, checks = 0;
        std::string line;
        while (std::getline(*input, line)) {
            if (line.empty() || line[0] == '#') continue;
            const auto bar = line.find('|');
            const auto actual = run(numbers(line.substr(0, bar)));
            ++cases;
            if (bar != std::string::npos) {
                if (actual != numbers(line.substr(bar + 1)))
                    throw std::runtime_error("menu music original mismatch case " + std::to_string(cases));
                checks += actual.size();
            } else {
                for (std::size_t i = 0; i < actual.size(); ++i)
                    std::cout << (i ? " " : "") << actual[i];
                std::cout << '\n';
            }
        }
        if (argc == 2) {
            if (cases == 0) throw std::runtime_error("empty menu music fixture");
            std::cout << cases << " original menu music cases / " << checks
                      << " scalar comparisons passed\n";
        }
    } catch (const std::exception& error) { std::cerr << error.what() << '\n'; return 1; }
}
