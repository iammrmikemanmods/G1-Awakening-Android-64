#include "g1/Text.hpp"

#include <fstream>
#include <functional>
#include <iostream>
#include <sstream>

namespace {
std::size_t checks{};
void check(bool value, const char* message) {
    ++checks;
    if (!value) throw std::runtime_error(message);
}
void rejects(const std::function<void()>& action, const char* message) {
    try { action(); }
    catch (const g1::FormatError&) { ++checks; return; }
    throw std::runtime_error(message);
}
void put16(g1::Bytes& bytes, std::size_t at, std::uint16_t value) {
    bytes.at(at) = static_cast<std::uint8_t>(value & 255U);
    bytes.at(at + 1U) = static_cast<std::uint8_t>(value >> 8U);
}
void put32(g1::Bytes& bytes, std::size_t at, std::uint32_t value) {
    for (std::size_t n = 0; n < 4U; ++n)
        bytes.at(at + n) = static_cast<std::uint8_t>((value >> (n * 8U)) & 255U);
}
g1::Bytes table() {
    g1::Bytes bytes(32U, 0xaaU); // Index padding is permitted by the source format.
    put16(bytes, 0, 0x2000U); put16(bytes, 2, 2U);
    put16(bytes, 4, 1000U); put16(bytes, 6, 32U);
    put16(bytes, 8, 3U); put16(bytes, 10, 40U);
    bytes.insert(bytes.end(), {4, 0, 0, 0, 'A', 'B', 'C', 0});
    bytes.insert(bytes.end(), {4, 0, 0, 0, 0xc3, 0xa9, 0xf0, 0x9f, 0x9a, 0x80, 0});
    put32(bytes, 12, static_cast<std::uint32_t>(bytes.size()));
    return bytes;
}
} // namespace

int main(int argc, char** argv) {
    try {
        if (argc != 2) throw std::runtime_error("expected original text-index fixture path");
        const auto bytes = table();
        const g1::TextTable decoded(bytes);
        check(decoded.entries().size() == 2U && decoded.entries()[0].id == 1000U &&
              decoded.entries()[1].source_offset == 40U, "sparse ID/order/offset changed");
        check(decoded.at(1000U) == "ABC" && decoded.at(3U) == std::string("\xc3\xa9\xf0\x9f\x9a\x80"),
              "UTF-8 bytes changed");
        check(decoded.contains(3U) && !decoded.contains(99U), "localization lookup mismatch");
        rejects([&] { (void)decoded.at(99U); }, "missing ID accepted");
        for (std::size_t n = 0; n < bytes.size(); ++n) {
            const g1::Bytes short_bytes(bytes.begin(), bytes.begin() + static_cast<std::ptrdiff_t>(n));
            rejects([&] { (void)g1::TextTable(short_bytes); }, "truncated table accepted");
        }
        auto bad = bytes; put16(bad, 0, 0);
        rejects([&] { (void)g1::TextTable(bad); }, "unknown flags accepted");
        bad = bytes; put16(bad, 2, 65535U);
        rejects([&] { (void)g1::TextTable(bad); }, "oversized index accepted");
        bad = bytes; put16(bad, 8, 1000U);
        rejects([&] { (void)g1::TextTable(bad); }, "duplicate ID accepted");
        bad = bytes; put16(bad, 6, 8U);
        rejects([&] { (void)g1::TextTable(bad); }, "index-overlapping string accepted");
        bad = bytes; put16(bad, 10, 31U);
        rejects([&] { (void)g1::TextTable(bad); }, "decreasing string offsets accepted");
        bad = bytes; put16(bad, 10, 65535U);
        rejects([&] { (void)g1::TextTable(bad); }, "out-of-resource offset accepted");
        bad = bytes; put32(bad, 12, 999U);
        rejects([&] { (void)g1::TextTable(bad); }, "wrong sentinel accepted");
        bad = bytes; bad[32] = 3U;
        rejects([&] { (void)g1::TextTable(bad); }, "unknown wrapper accepted");
        bad = bytes; bad[37] = 0U;
        rejects([&] { (void)g1::TextTable(bad); }, "embedded NUL accepted");
        bad = bytes; bad.back() = 1U;
        rejects([&] { (void)g1::TextTable(bad); }, "missing terminator accepted");
        for (const auto& sequence : {g1::Bytes{0xc0, 0xaf}, g1::Bytes{0xed, 0xa0, 0x80},
                                   g1::Bytes{0xf4, 0x90, 0x80, 0x80}, g1::Bytes{0xe2, 0x82}}) {
            g1::Bytes malformed(12U, 0);
            put16(malformed, 0, 0x2000U); put16(malformed, 2, 1U);
            put16(malformed, 6, 12U);
            malformed.insert(malformed.end(), {4, 0, 0, 0});
            malformed.insert(malformed.end(), sequence.begin(), sequence.end());
            malformed.push_back(0);
            put32(malformed, 8, static_cast<std::uint32_t>(malformed.size()));
            rejects([&] { (void)g1::TextTable(malformed); }, "malformed UTF-8 accepted");
        }
        g1::Bytes empty{0, 0x20, 0, 0, 8, 0, 0, 0};
        check(g1::TextTable(empty).entries().empty(), "valid empty table rejected");
        empty.push_back(0); put32(empty, 4, 9U);
        rejects([&] { (void)g1::TextTable(empty); }, "unindexed empty-table payload accepted");

        std::ifstream fixture(argv[1]);
        if (!fixture) throw std::runtime_error("cannot open text-index fixture");
        std::string line;
        std::size_t rows{};
        while (std::getline(fixture, line)) {
            if (line.empty() || line[0] == '#') continue;
            std::istringstream input(line);
            std::int32_t index{}, variant{}, expected{};
            if (!(input >> index >> variant >> expected)) throw std::runtime_error("bad text-index fixture");
            check(g1::script_text_id(index, variant != 0) == expected, "original text-index switch mismatch");
            ++rows;
        }
        check(rows == 972U, "original text-index fixture count changed");
        rejects([] { (void)g1::script_text_id(-1); }, "negative text index accepted");
        rejects([] { (void)g1::script_text_id(486); }, "text index 486 accepted");
        check(g1::unit_text_index(124U) == 128 &&
              g1::script_text_id(g1::unit_text_index(124U)) == 0x01a0U &&
              g1::script_text_id(g1::unit_text_index(122U)) == 0x02a7U,
              "original unit-name +4 conversion changed");
        check(g1::unit_text_index(65532U) == 0 && g1::unit_text_index(32764U) == -32768,
              "unit text-index wrap/sign behavior changed");
        std::cout << "text: " << checks << " checks; " << rows << " original switch cases\n";
        return 0;
    } catch (const std::exception& error) {
        std::cerr << "text test failed: " << error.what() << '\n';
        return 1;
    }
}
