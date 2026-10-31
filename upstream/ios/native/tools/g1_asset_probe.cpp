#include "g1/Archive.hpp"
#include "g1/M3G.hpp"

#include <filesystem>
#include <fstream>
#include <iomanip>
#include <iostream>
#include <sstream>

int main(int argc, char** argv) {
    try {
        if (argc != 2 && argc != 3) {
            std::cerr << "Usage: g1_asset_probe iphone.big [NEW_extraction_directory]\n";
            return 2;
        }
        const g1::Archive archive(g1::read_file(argv[1]));
        std::filesystem::path out;
        if (argc == 3) {
            out = argv[2];
            if (!std::filesystem::create_directory(out))
                throw g1::FormatError("extraction directory already exists or cannot be created");
        }
        std::cout << "index\tgroup_id\tresource_id\tkey\tsize\tadler32\tkind\tm3g_objects\n";
        for (const auto& entry : archive.resources()) {
            const auto data = archive.read(entry.index);
            const std::string kind = g1::resource_kind(data);
            std::size_t objects = 0;
            if (kind == "m3g") objects = g1::M3GFile(data).objects().size();
            std::cout << entry.index << '\t' << entry.group_id << '\t' << entry.resource_id << '\t' << entry.key << '\t'
                      << data.size() << '\t' << g1::adler32_bytes(data) << '\t'
                      << kind << '\t' << objects << '\n';
            if (!out.empty()) {
                std::ostringstream name;
                name << std::setfill('0') << std::setw(4) << entry.index << ".bin";
                std::ofstream file(out/name.str(), std::ios::binary);
                file.write(reinterpret_cast<const char*>(data.data()), static_cast<std::streamsize>(data.size()));
                if (!file) throw g1::FormatError("resource output write failed");
            }
        }
        return 0;
    } catch (const std::exception& error) {
        std::cerr << "ERROR: " << error.what() << '\n';
        return 1;
    }
}
