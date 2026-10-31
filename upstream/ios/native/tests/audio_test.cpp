#include "g1/Audio.hpp"
#include <iostream>

int main() {
    unsigned rejected = 0;
    for (const auto& data : {g1::Bytes{},g1::Bytes{'O','g','g','S'},g1::Bytes(64,0)}) {
        try { (void)g1::decode_vorbis(data); }
        catch (const g1::FormatError&) { ++rejected; }
    }
    if (rejected != 3) { std::cerr << "Invalid Vorbis headers accepted\n"; return 1; }
    std::cout << "Three invalid Vorbis header contracts passed. Original audio corpus is tested separately.\n";
    return 0;
}
