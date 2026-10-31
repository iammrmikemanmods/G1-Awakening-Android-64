#include "g1/Scene.hpp"
#include "g1/TileUpscale.hpp"
#include <iostream>
#include <fstream>
#include <filesystem>
int main(int argc,char**argv){try{
 if(argc!=4)throw std::runtime_error("usage: g1_visual_asset_probe iphone.big terrain-tables.bin output_directory");
 const g1::Archive archive(g1::read_file(argv[1]));const auto tablebytes=g1::read_file(argv[2],480);
 const auto tables=g1::decode_terrain_tables(tablebytes);const g1::FirstMissionScene scene(archive,tablebytes);
 std::filesystem::path dir(argv[3]);std::filesystem::create_directories(dir);
 for(const auto& pair:scene.textures){const auto& original=pair.second;
    const auto expanded=original.width==512&&original.height==256?g1::scale4x_tiles(original,64,64):g1::scale4x_rgba(original);
    const auto bytes=g1::encode_png(expanded);std::ofstream f(dir/(std::to_string(pair.first)+"_scale4x.png"),std::ios::binary);f.write(reinterpret_cast<const char*>(bytes.data()),bytes.size());}
 std::ofstream json(dir/"first_mission_visual_manifest.json");
 json<<"{\"grid_resource\":"<<scene.world.grid_resource_id<<",\"width\":"<<scene.world.grid.width<<",\"height\":"<<scene.world.grid.height<<",\"style\":"<<int(scene.world.style)<<",\"atlas_resource\":"<<scene.terrain.atlas_resource_id<<",\"tiles\":[";
 bool comma=false;for(auto tile:scene.world.grid.tiles){if(comma)json<<',';comma=true;json<<int(tile);}json<<"],\"visual_records\":[";comma=false;
 for(auto rec:tables.at(scene.world.style)){if(comma)json<<',';comma=true;json<<"{\"top\":"<<int(rec.atlas_cells[0])<<",\"height\":"<<int(rec.height_steps)<<'}';}
 json<<"],\"render_only\":true,\"gameplay_mutated\":false}\n";
 std::cout<<"Original-data derived pixels and mission visual manifest exported; no game saves written.\n";return 0;
}catch(const std::exception&e){std::cerr<<e.what()<<'\n';return 1;}}
