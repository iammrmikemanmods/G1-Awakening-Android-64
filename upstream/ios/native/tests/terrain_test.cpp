#include "g1/Terrain.hpp"

#include <cmath>
#include <fstream>
#include <iostream>
#include <limits>
#include <stdexcept>
#include <string>

namespace {

std::size_t contracts = 0;

void require(bool value, const char* message) {
    if (!value) throw std::runtime_error(message);
    ++contracts;
}

template<class Function> void rejects(Function&& function) {
    bool rejected = false;
    try { function(); } catch (const g1::FormatError&) { rejected = true; }
    require(rejected, "invalid terrain input was accepted");
}

int number(std::istream& input) {
    int value = 0;
    if (!(input >> value)) throw std::runtime_error("truncated terrain oracle fixture");
    return value;
}

void equal(int value, int expected, const std::string& name) {
    if (value != expected)
        throw std::runtime_error(name + ": native " + std::to_string(value) +
                                 ", original " + std::to_string(expected));
}

} // namespace

int main(int argc, char** argv) {
    try {
        if (argc != 2) throw std::runtime_error("pass terrain_cases.txt as the only argument");
        std::ifstream input(argv[1]);
        std::string magic;
        input >> magic;
        if (magic != "G1_TERRAIN_ORACLE_V1") throw std::runtime_error("not a terrain oracle fixture");
        const int cases = number(input);
        if (cases != 128) throw std::runtime_error("terrain original-execution fixture is incomplete");
        std::size_t comparisons = 0, quads = 0;
        for (int n = 0; n < cases; ++n) {
            std::string name;
            if (!(input >> name)) throw std::runtime_error("missing terrain oracle case name");
            g1::TerrainCellShape shape;
            shape.origin_x = number(input);
            shape.origin_z = number(input);
            shape.top = number(input);
            shape.bottom = number(input);
            shape.face_mask = static_cast<std::uint8_t>(number(input));
            shape.previous_mask = static_cast<std::uint8_t>(number(input));
            shape.bevel = number(input) != 0;
            g1::TerrainVisualRecord record{};
            for (auto& cell : record.atlas_cells) cell = static_cast<std::uint8_t>(number(input));
            const auto result = g1::terrain_cell_quads(shape, record);
            equal(static_cast<int>(result.size()), number(input), name + "/quad_count");
            ++comparisons;
            quads += result.size();
            for (const auto& quad : result) {
                equal(static_cast<int>(quad.face), number(input), name + "/face");
                equal(quad.atlas_cell, number(input), name + "/atlas_cell");
                comparisons += 2;
                for (const auto& vertex : quad.positions)
                    for (const auto value : vertex) { equal(value, number(input), name + "/position"); ++comparisons; }
                for (const auto& uv : quad.quantized_uv)
                    for (const auto value : uv) { equal(value, number(input), name + "/quantized_uv"); ++comparisons; }
                for (const auto& uv : quad.normalized_uv)
                    for (const auto value : uv)
                        if (!std::isfinite(value) || value < 0.0F || value > 1.0F)
                            throw std::runtime_error("normalized terrain UV is outside the texture");
            }
        }
        std::string extra;
        if (input >> extra) throw std::runtime_error("trailing oracle fixture data");

        const auto grid = g1::decode_terrain_grid({2, 2, 0, 1, 31, 255});
        require(grid.width == 2 && grid.height == 2 && grid.tiles.size() == 4, "grid framing");
        require(grid.tile_at(0, 1) == 31 && grid.tile_at(1, 1) == 255, "row-major order");
        rejects([&] { grid.tile_at(2, 0); });
        rejects([&] { grid.tile_at(0, 2); });
        const g1::Bytes encoded{2, 2, 0, 1, 2, 3};
        for (std::size_t i = 0; i < encoded.size(); ++i)
            rejects([&] { g1::decode_terrain_grid(g1::Bytes(encoded.begin(), encoded.begin() + i)); });
        rejects([] { g1::decode_terrain_grid({0, 1}); });
        rejects([] { g1::decode_terrain_grid({1, 0}); });
        rejects([] { g1::decode_terrain_grid({1, 1, 32}); });
        rejects([] { g1::decode_terrain_grid({1, 1, 254}); });
        rejects([] { g1::decode_terrain_grid({1, 1, 0, 0}); });
        g1::Bytes lookup(33, 0);
        lookup[0] = 1;
        lookup[32] = 5;
        require(g1::decode_terrain_lookup(lookup)[31] == 5, "lookup framing");
        lookup[0] = 0;
        rejects([&] { g1::decode_terrain_lookup(lookup); });
        lookup[0] = 1;
        lookup.pop_back();
        rejects([&] { g1::decode_terrain_lookup(lookup); });
        lookup.resize(34);
        rejects([&] { g1::decode_terrain_lookup(lookup); });
        const auto rules = g1::decode_terrain_rules({2, 3, 7, 8, 9, 3, 10, 11, 12});
        require(rules.size() == 2 && rules[0].fields[2] == 9 && rules[1].fields[0] == 10,
                "nested rule framing");
        rejects([] { g1::decode_terrain_rules({}); });
        rejects([] { g1::decode_terrain_rules({0}); });
        rejects([] { g1::decode_terrain_rules({1, 2, 7, 8, 9}); });
        rejects([] { g1::decode_terrain_rules({1, 3, 7, 8}); });
        rejects([] { g1::decode_terrain_rules({1, 3, 7, 8, 9, 0}); });
        rejects([] { g1::decode_terrain_rules({255, 3, 7, 8, 9}); });
        g1::Bytes raw(480, 0);
        raw[4] = 255;
        raw[164] = 128;
        const auto tables = g1::decode_terrain_tables(raw);
        require(tables[0][0].height_steps == -1 && tables[1][0].height_steps == -128,
                "signed terrain height");
        require(g1::terrain_atlas_resource(0) == 693 && g1::terrain_atlas_resource(1) == 777 &&
                g1::terrain_atlas_resource(2) == 761, "original atlas resource mapping");
        rejects([] { g1::terrain_atlas_resource(3); });
        raw.pop_back();
        rejects([&] { g1::decode_terrain_tables(raw); });
        raw.resize(481);
        rejects([&] { g1::decode_terrain_tables(raw); });
        raw.resize(480);
        raw[0] = 32;
        rejects([&] { g1::decode_terrain_tables(raw); });
        g1::TerrainCellShape shape;
        g1::TerrainVisualRecord record{};
        shape.face_mask = 4;
        rejects([&] { g1::terrain_cell_quads(shape, record); });
        shape.face_mask = 16;
        shape.previous_mask = 4;
        rejects([&] { g1::terrain_cell_quads(shape, record); });
        shape.previous_mask = 16;
        shape.top = 32768;
        rejects([&] { g1::terrain_cell_quads(shape, record); });
        shape.top = 0;
        shape.origin_x = std::numeric_limits<std::int32_t>::max();
        rejects([&] { g1::terrain_cell_quads(shape, record); });
        shape.origin_x = 0;
        record.atlas_cells[3] = 255;
        rejects([&] { g1::terrain_cell_quads(shape, record); });
        rejects([&] { g1::build_terrain_mesh(grid, tables, 0); });
        const auto plain = g1::decode_terrain_grid({1, 1, 1});
        rejects([&] { g1::build_terrain_mesh(plain, tables, 1); });
        rejects([&] { g1::build_terrain_mesh(plain, tables, 3); });
        const auto mesh = g1::build_terrain_mesh(plain, tables, 2);
        require(mesh.quads.size() == 1 && mesh.atlas_resource_id == 761 &&
                mesh.quads[0].face == g1::TerrainFace::top, "plain native grid mesh");
        std::cout << "Passed " << cases << " original-execution cases, " << quads << " quads, "
                  << comparisons << " scalar comparisons and " << contracts << " format/API contracts.\n";
        return 0;
    } catch (const std::exception& error) {
        std::cerr << error.what() << '\n';
        return 1;
    }
}
