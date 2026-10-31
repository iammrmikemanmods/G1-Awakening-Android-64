#include "g1/InitialPresentation.hpp"
#include <iostream>

int main() {
    try {
        g1::Bytes bytes(208,0);
        bytes[0] = 207;
        for (std::size_t i = 1; i < bytes.size(); ++i)
            bytes[i] = static_cast<std::uint8_t>(i - 1);
        const auto table = g1::decode_unit_animation_table(bytes);
        if (table[0][0] != 0 || table[11][1] != 100 || table[19][1] != 172 || table[22][8] != 206)
            throw std::runtime_error("animation vector row/field addressing changed");
        unsigned rejected = 0;
        for (const auto count : {0,23,206,208}) {
            auto invalid = bytes;
            invalid[0] = static_cast<std::uint8_t>(count);
            try { (void)g1::decode_unit_animation_table(invalid); }
            catch (const g1::FormatError&) { ++rejected; }
        }
        bytes.pop_back();
        try { (void)g1::decode_unit_animation_table(bytes); }
        catch (const g1::FormatError&) { ++rejected; }
        if (rejected != 5) throw std::runtime_error("invalid counted byte-vector accepted");
        std::cout << "Animation table addressing and five malformed cases passed.\n";
        return 0;
    } catch (const std::exception& error) {
        std::cerr << "FAIL: " << error.what() << '\n'; return 1;
    }
}
