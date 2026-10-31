#include "g1/SupportTutorial.hpp"

#include <fstream>
#include <iostream>
#include <sstream>
#include <stdexcept>
#include <string>
#include <vector>

namespace {
std::vector<std::int64_t> numbers(const std::string& text) {
    std::istringstream input(text);
    std::vector<std::int64_t> result;
    std::int64_t value;
    while (input >> value) result.push_back(value);
    if (!input.eof()) throw std::runtime_error("invalid support tutorial fixture number");
    return result;
}

std::vector<std::int64_t> run(const std::vector<std::int64_t>& values) {
    if (values.size() != 9 || values[0] < 0 || values[0] > 255 ||
        values[1] < 0 || values[1] > 255 || values[2] < -128 || values[2] > 127)
        throw std::runtime_error("support tutorial fixture input shape");
    g1::SupportTutorialSlots helpers{};
    for (std::size_t i = 0; i < helpers.size(); ++i) {
        const auto owner = values[3 + i];
        if (owner < -1 || owner > 255)
            throw std::runtime_error("support tutorial helper owner outside fixture");
        if (owner != -1) helpers[i] = static_cast<std::uint8_t>(owner);
    }
    const auto before = helpers;
    const auto decision = g1::support_tutorial_entry(
        static_cast<std::uint8_t>(values[0]), static_cast<std::uint8_t>(values[1]),
        static_cast<std::int8_t>(values[2]), helpers);
    if (helpers != before) throw std::runtime_error("support tutorial mutated helper identity");
    if (decision.request_message) {
        if (decision.pending_flag != 0)
            throw std::runtime_error("support tutorial did not consume pending flag before message");
        const auto again = g1::support_tutorial_entry(decision.pending_flag,
            static_cast<std::uint8_t>(values[1]), static_cast<std::int8_t>(values[2]), helpers);
        if (again.request_message || again.pending_flag != 0)
            throw std::runtime_error("consumed support tutorial requested a duplicate message");
    }
    return {decision.pending_flag, decision.request_message ? 1 : 0,
            decision.request_message ? g1::support_tutorial_text_resource : 0};
}
} // namespace

int main(int argc, char** argv) {
    try {
        if (argc != 2) throw std::runtime_error("usage: support_tutorial_test fixture");
        std::ifstream input(argv[1]);
        if (!input) throw std::runtime_error("support tutorial fixture missing");
        std::size_t cases = 0, checks = 0;
        std::string line;
        while (std::getline(input, line)) {
            if (line.empty() || line[0] == '#') continue;
            const auto separator = line.find('|');
            if (separator == std::string::npos)
                throw std::runtime_error("support tutorial fixture lacks original output");
            const auto actual = run(numbers(line.substr(0, separator)));
            const auto expected = numbers(line.substr(separator + 1));
            ++cases;
            if (actual != expected)
                throw std::runtime_error("support tutorial original mismatch case " + std::to_string(cases));
            checks += actual.size();
        }
        if (!cases) throw std::runtime_error("empty support tutorial fixture");
        if (g1::support_tutorial_default_pending != 1)
            throw std::runtime_error("support tutorial default differs from original reset prefix");
        std::cout << cases << " original support tutorial cases / " << checks
                  << " scalar comparisons passed; helper identity and one-shot checks passed\n";
    } catch (const std::exception& error) {
        std::cerr << error.what() << '\n';
        return 1;
    }
}
