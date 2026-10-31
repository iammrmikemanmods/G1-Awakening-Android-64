#include "g1/Archive.hpp"
#include "g1/M3G.hpp"
#include <iostream>

int main(int argc, char** argv) {
    try {
        if (argc != 2) {
            std::cerr << "Usage: m3g_probe file.m3g\n";
            return 2;
        }
        const g1::M3GFile model(g1::read_file(argv[1], g1::Archive::max_resource_size));
        std::cout << "objects=" << model.objects().size() << " sections=" << model.sections().size() << '\n';
        return 0;
    } catch (const std::exception& error) {
        std::cerr << "ERROR: " << error.what() << '\n';
        return 1;
    }
}
