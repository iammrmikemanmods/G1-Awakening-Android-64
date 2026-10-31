#include "g1/Terrain.hpp"

#include <cstring>
#include <fstream>
#include <iostream>
#include <limits>
#include <stdexcept>
#include <string>

namespace {
std::size_t checks{};
void require(bool value, const char* detail) {
    ++checks;
    if (!value) throw std::runtime_error(detail);
}
template<class F> void rejects(F&& function) {
    bool rejected = false;
    try { function(); } catch (const g1::FormatError&) { rejected = true; }
    require(rejected, "malformed animated terrain state was accepted");
}
int number(std::istream& input) {
    int value{};
    if (!(input >> value)) throw std::runtime_error("truncated animated terrain fixture");
    return value;
}
std::uint32_t bits(float value) {
    std::uint32_t result{};
    static_assert(sizeof(value) == sizeof(result), "32-bit float required");
    std::memcpy(&result, &value, sizeof(result));
    return result;
}
void compare(const std::vector<g1::TerrainQuad>& quads, std::istream& input) {
    require(quads.size() == static_cast<std::size_t>(number(input)), "original quad count differs");
    for (const auto& quad : quads) {
        require(static_cast<int>(quad.face) == number(input), "original face order differs");
        require(quad.atlas_cell == number(input), "original animated atlas selector differs");
        for (const auto& vertex : quad.positions)
            for (const auto v : vertex) require(v == number(input), "original position differs");
        for (const auto& uv : quad.quantized_uv)
            for (const auto v : uv) require(v == number(input), "original quantized UV differs");
    }
}
} // namespace

int main(int argc, char** argv) {
    try {
        if (argc != 2) throw std::runtime_error("pass terrain_animated_cases.txt");
        std::ifstream input(argv[1]);
        std::string tag;
        input >> tag;
        require(tag == "G1_ANIMATED_TERRAIN_ORACLE_V1", "wrong animated terrain fixture");
        const auto cases = number(input);
        require(cases == 128, "animated terrain fixture is incomplete");
        for (int i = 0; i < cases; ++i) {
            input >> tag;
            g1::TerrainCellShape shape;
            shape.origin_x=number(input); shape.origin_z=number(input);
            shape.top=number(input); shape.bottom=number(input);
            shape.face_mask=static_cast<std::uint8_t>(number(input));
            shape.previous_mask=static_cast<std::uint8_t>(number(input));
            const auto enabled=number(input)!=0;
            const auto slots=static_cast<std::uint32_t>(number(input));
            g1::TerrainVisualRecord record{};
            for (auto& value : record.atlas_cells) value=static_cast<std::uint8_t>(number(input));
            const auto result=g1::terrain_style1_cell_quads(shape,record,enabled,slots);
            compare(result.base,input);
            compare(result.animated,input);
        }
        input >> tag;
        require(tag == "CLOCKS", "missing original clock cases");
        const auto clocks=number(input);
        require(clocks == 64, "clock fixture is incomplete");
        for (int i=0;i<clocks;++i) {
            g1::TerrainAnimationState state;
            state.accumulated_ms=static_cast<std::uint32_t>(number(input));
            state.phase=static_cast<std::uint8_t>(number(input));
            const auto delta=static_cast<std::uint32_t>(number(input));
            g1::advance_terrain_animation(state,delta);
            require(state.accumulated_ms == static_cast<std::uint32_t>(number(input)),
                    "original animation accumulator differs");
            require(state.phase == number(input), "original phase differs");
            require(bits(g1::terrain_animation_u_offset(state)) ==
                        static_cast<std::uint32_t>(number(input)), "original U translation differs");
            require(number(input)==0 && number(input)==0, "original V/Z texture translation changed");
        }
        require(!(input>>tag), "trailing animated terrain fixture data");
        const g1::Bytes config_bytes{3,0,1,7,2,8,9, 0, 1,0, 0};
        const auto config=g1::decode_terrain_animation_config(config_bytes);
        require(config.tables[0].size()==3 && config.tables[0][0].empty() &&
                config.tables[0][1]==g1::Bytes{7} && config.tables[0][2]==g1::Bytes({8,9}) &&
                config.tables[1].empty() && config.tables[2].size()==1 &&
                config.tables[2][0].empty() && config.tables[3].empty(), "nested animation framing differs");
        for (std::size_t n=0;n<config_bytes.size();++n)
            rejects([&]{g1::decode_terrain_animation_config(g1::Bytes(config_bytes.begin(),
                config_bytes.begin()+static_cast<std::ptrdiff_t>(n)));});
        auto malformed=config_bytes; malformed.push_back(0);
        rejects([&]{g1::decode_terrain_animation_config(malformed);});
        rejects([]{g1::decode_terrain_animation_config({2,0,0,0,0,0});});
        auto invalid=g1::TerrainAnimationState{0,4};
        rejects([&]{g1::advance_terrain_animation(invalid,0);});
        require(invalid.phase==4 && invalid.accumulated_ms==0, "invalid phase mutated on rejection");
        rejects([&]{g1::terrain_animation_u_offset(invalid);});
        invalid={static_cast<std::uint32_t>(std::numeric_limits<std::int32_t>::max()),0};
        rejects([&]{g1::advance_terrain_animation(invalid,1);});
        require(invalid.accumulated_ms==2147483647U && invalid.phase==0,
                "time overflow mutated state on rejection");
        g1::TerrainCellShape shape;
        g1::TerrainVisualRecord record{};
        shape.bevel=true;
        rejects([&]{g1::terrain_style1_cell_quads(shape,record,true,0);});
        shape.bevel=false; shape.face_mask=1;
        rejects([&]{g1::terrain_style1_cell_quads(shape,record,true,0);});
        shape.face_mask=16; shape.top=-32768; record.atlas_cells[1]=14; shape.face_mask|=1;
        rejects([&]{g1::terrain_style1_cell_quads(shape,record,true,0);});
        g1::TerrainTables tables{};
        const auto grid=g1::decode_terrain_grid({1,1,0});
        auto bad_config=config; bad_config.tables[0].clear();
        rejects([&]{g1::build_terrain_mesh(grid,tables,1,bad_config);});
        bad_config=config; bad_config.tables[2].resize(256);
        rejects([&]{g1::build_terrain_mesh(grid,tables,1,bad_config);});
        rejects([&]{g1::build_terrain_mesh(grid,tables,1);});
        const auto ordinary=g1::build_terrain_mesh(grid,tables,1,config);
        require(ordinary.quads.size()==1 && !ordinary.animated_surface,
                "ordinary style1 cell incorrectly creates an animated surface");
        std::cout << "animated terrain: " << cases << " geometry and " << clocks
                  << " clock cases, " << checks << " comparisons/contracts passed\n";
        return 0;
    } catch (const std::exception& error) {
        std::cerr << error.what() << '\n';
        return 1;
    }
}
