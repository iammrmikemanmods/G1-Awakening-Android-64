#include "g1/Image.hpp"

#include <iostream>
#include <stdexcept>

int main() {
    try {
        // Distinct corners and translucent straight-alpha samples expose row
        // flips, channel swaps and accidental premultiplication.
        const g1::RgbaImage source{2,2,{255,0,0,255, 0,255,0,128,
                                       0,0,255,0, 23,47,91,63}};
        const auto encoded = g1::encode_png(source);
        const auto decoded = g1::decode_png(encoded);
        if (decoded.width != 2 || decoded.height != 2 || decoded.pixels != source.pixels)
            throw std::runtime_error("RGBA orientation/alpha contract changed");
        auto truncated = encoded; truncated.pop_back();
        unsigned rejected = 0;
        for (const auto& bytes : {g1::Bytes{}, g1::Bytes{0,1,2}, truncated}) {
            try { (void)g1::decode_png(bytes); }
            catch (const g1::FormatError&) { ++rejected; }
        }
        try { (void)g1::encode_png({0,1,{}}); }
        catch (const g1::FormatError&) { ++rejected; }
        try { (void)g1::encode_png({2,2,{0}}); }
        catch (const g1::FormatError&) { ++rejected; }
        if (rejected != 5) throw std::runtime_error("malformed image accepted");
        std::cout << "RGBA row/channel/straight-alpha round trip and five malformed cases passed.\n";
        return 0;
    } catch (const std::exception& error) {
        std::cerr << "FAIL: " << error.what() << '\n'; return 1;
    }
}
