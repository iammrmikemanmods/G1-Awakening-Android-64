#include "g1/Terrain.hpp"

#include <charconv>
#include <iomanip>
#include <iostream>
#include <string>

namespace {

std::uint32_t number(const std::string& text) {
    const bool hex = text.size() > 2 && text[0] == '0' && (text[1] == 'x' || text[1] == 'X');
    std::uint32_t value = 0;
    const auto* begin = text.data() + (hex ? 2 : 0);
    const auto result = std::from_chars(begin, text.data() + text.size(), value, hex ? 16 : 10);
    if (result.ec != std::errc{} || result.ptr != text.data() + text.size())
        throw g1::FormatError("invalid unsigned integer");
    return value;
}

template<class Range, class Function> void list(const Range& range, Function emit) {
    std::cout << '[';
    bool first = true;
    for (const auto& value : range) {
        if (!first) std::cout << ',';
        first = false;
        emit(value);
    }
    std::cout << ']';
}

template<class Range> void numbers(const Range& range) {
    list(range, [](auto value) { std::cout << +value; });
}

void print_grid(const g1::TerrainGrid& grid) {
    std::cout << "{\"width\":" << grid.width << ",\"height\":" << grid.height << ",\"tiles\":";
    numbers(grid.tiles);
    std::cout << '}';
}

void print_quads(const std::vector<g1::TerrainQuad>& quads) {
    list(quads, [](const auto& quad) {
        std::cout << "{\"tile_x\":" << quad.tile_x << ",\"tile_y\":" << quad.tile_y
                  << ",\"face\":" << static_cast<unsigned>(quad.face)
                  << ",\"atlas_cell\":" << +quad.atlas_cell << ",\"positions\":";
        list(quad.positions, [](const auto& vertex) { numbers(vertex); });
        std::cout << ",\"quantized_uv\":";
        list(quad.quantized_uv, [](const auto& uv) { numbers(uv); });
        std::cout << ",\"normalized_uv\":";
        list(quad.normalized_uv, [](const auto& uv) { numbers(uv); });
        std::cout << '}';
    });
}

void print_mesh(const g1::TerrainMesh& mesh) {
    std::cout << "{\"style\":" << +mesh.style << ",\"atlas_resource_id\":" << mesh.atlas_resource_id
              << ",\"position_scale\":" << mesh.position_scale << ",\"quads\":";
    print_quads(mesh.quads);
    if (mesh.animated_surface) {
        std::cout << ",\"animated_surface\":{\"atlas_resource_id\":"
                  << mesh.animated_surface->atlas_resource_id
                  << ",\"decoration_slots\":" << mesh.animated_surface->decoration_slots
                  << ",\"quads\":";
        print_quads(mesh.animated_surface->quads);
        std::cout << '}';
    }
    std::cout << '}';
}

void decode(const std::string& kind, const g1::Bytes& bytes) {
    if (kind == "grid") print_grid(g1::decode_terrain_grid(bytes));
    else if (kind == "lookup") numbers(g1::decode_terrain_lookup(bytes));
    else if (kind == "rules") list(g1::decode_terrain_rules(bytes), [](const auto& row) { numbers(row.fields); });
    else if (kind == "animation_config") {
        list(g1::decode_terrain_animation_config(bytes).tables, [](const auto& table) {
            list(table, [](const auto& row) { numbers(row); });
        });
    }
    else if (kind == "tables") {
        list(g1::decode_terrain_tables(bytes), [](const auto& style) {
            list(style, [](const auto& record) {
                std::cout << "{\"atlas_cells\":";
                numbers(record.atlas_cells);
                std::cout << ",\"height_steps\":" << +record.height_steps << '}';
            });
        });
    } else throw g1::FormatError("unknown terrain resource kind");
}

} // namespace

int main(int argc, char** argv) {
    try {
        std::cout << std::setprecision(9);
        if (argc == 3) {
            decode(argv[1], g1::read_file(argv[2], 65527));
        } else if (argc == 6 && std::string(argv[2]) == "--archive") {
            const g1::Archive archive(g1::read_file(argv[3]));
            const auto matches = archive.find(number(argv[4]), number(argv[5]));
            if (matches.size() != 1) throw g1::FormatError("terrain ID/key must identify one resource");
            decode(argv[1], archive.read(matches.front()));
        } else if ((argc == 5 || argc == 6) && std::string(argv[1]) == "mesh") {
            const auto style = number(argv[4]);
            if (style > 2) throw g1::FormatError("terrain style is outside recovered range");
            const auto grid = g1::decode_terrain_grid(g1::read_file(argv[2], 65527));
            const auto tables = g1::decode_terrain_tables(g1::read_file(argv[3], 480));
            if (argc == 6) {
                const auto config = g1::decode_terrain_animation_config(g1::read_file(argv[5], 261124));
                print_mesh(g1::build_terrain_mesh(grid, tables, static_cast<std::uint8_t>(style), config));
            } else {
                print_mesh(g1::build_terrain_mesh(grid, tables, static_cast<std::uint8_t>(style)));
            }
        } else {
            std::cerr << "Usage: terrain_probe grid|lookup|rules|tables|animation_config FILE\n"
                         "   or: terrain_probe grid|lookup|rules|animation_config --archive iphone.big ID KEY\n"
                         "   or: terrain_probe mesh GRID_FILE TABLES_FILE STYLE [ANIMATION_CONFIG_FILE]\n";
            return 2;
        }
        std::cout << '\n';
        return 0;
    } catch (const std::exception& error) {
        std::cerr << "ERROR: " << error.what() << '\n';
        return 1;
    }
}
