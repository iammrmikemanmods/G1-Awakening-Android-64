#include "g1/TacticalModel.hpp"
#include "g1/UIBitmap.hpp"
#include "g1/SpriteGeometry.hpp"
#include <fstream>
#include <filesystem>
#include <iostream>
namespace fs=std::filesystem;
int main(int argc,char**argv){try{
 if(argc!=3)throw std::runtime_error("usage: clean_texture_probe iphone.big private_output");
 g1::Archive archive(g1::read_file(argv[1]));fs::path out(argv[2]);fs::create_directories(out);
 std::ofstream manifest(out/"inputs.tsv");manifest<<"name\trole\twidth\theight\n";
 const auto emit=[&](const std::string& name,const std::string& role,const g1::RgbaImage& image){
    auto bytes=g1::encode_png(image);std::ofstream f(out/(name+".png"),std::ios::binary);
    if(!f.write(reinterpret_cast<char*>(bytes.data()),bytes.size()))throw std::runtime_error("Export failed");
    manifest<<name<<'\t'<<role<<'\t'<<image.width<<'\t'<<image.height<<'\n';
 };
 for(auto id:{1002,1224,417,657,514,874,799,709,1034,1038,721,1118,919,633,904,433,660,850,669,726,454,442,548,793,1076,591,1191,905,719}){
    const auto ids=archive.find(id,0xb7178678U);if(ids.size()!=1)throw std::runtime_error("Missing UI PNG");
    emit("ui_"+std::to_string(id),"ui",g1::decode_png(archive.read(ids[0])));
 }
 for(auto id:{646,731,396,878,670,1295,1156}){
    const auto ids=archive.find(id,0xb717945fU);if(ids.size()!=1)throw std::runtime_error("Missing UI BMP");
    emit("ui_"+std::to_string(id),"ui",g1::decode_ui_bmp(archive.read(ids[0]),0x00ff00ffU));
 }
 for(int type=0;type<23;++type){g1::TacticalModel model(archive,type);
    for(const auto& [id,image]:model.asset().geometry.images)emit("character_"+std::to_string(type)+"_"+std::to_string(id),"character",image);}
 for(auto resource:{486,534,1212}){
    const auto ids=archive.find(resource,0xf4e02223U);if(ids.size()!=1)throw std::runtime_error("Missing battle environment");
    g1::M3GScene scene(archive.read(ids[0]));auto asset=scene.render_asset();
    for(const auto& [id,image]:asset.geometry.images)emit("battle_"+std::to_string(resource)+"_"+std::to_string(id),"environment",image);
 }
 std::map<std::uint32_t,std::set<std::array<int,4>>> scenery_rects;
 std::map<std::uint32_t,g1::RgbaImage> scenery_images;
 for(std::uint8_t type=73;type<=78;++type) {
    const auto resource=g1::sprite_resource_for_type(type);if(!resource)throw std::runtime_error("Missing scenery sprite binding");
    const auto ids=archive.find(resource->image_id,resource->resource_key);
    const auto desc=archive.find(resource->descriptor_id,resource->resource_key);
    if(ids.size()!=1||desc.size()!=1)throw std::runtime_error("Ambiguous scenery sprite data");
    auto image=g1::decode_png(archive.read(ids[0]));g1::AtlasDescriptor atlas(archive.read(desc[0]));
    for(const auto& r:atlas.rectangles()){if(r.width<=0||r.height<=0)continue;
        if(r.x<0||r.y<0||r.x+r.width>int(image.width)||r.y+r.height>int(image.height))throw std::runtime_error("Scenery rectangle out of bounds");
        scenery_rects[resource->image_id].insert({r.x,r.y,r.width,r.height});}
    scenery_images[resource->image_id]=std::move(image);
 }
 for(const auto& [id,image]:scenery_images) {
    const auto name="scenery_"+std::to_string(id);emit(name,"sprite",image);
    std::ofstream rects(out/(name+".rects.json"));rects<<'[';bool first=true;
    for(const auto& r:scenery_rects.at(id)){
        if(!first)rects<<',';first=false;rects<<'['<<r[0]<<','<<r[1]<<','<<r[2]<<','<<r[3]<<']';}
    rects<<']';
 }
 for(const auto& e:archive.resources()) {
    if(e.resource_id!=693 && e.resource_id!=761 && e.resource_id!=777)continue;
    auto bytes=archive.read(e.index);if(std::string(g1::resource_kind(bytes))!="png")continue;
    auto image=g1::decode_png(bytes);if(image.width==512&&image.height==256)
        emit("terrain_"+std::to_string(e.resource_id),"terrain",image);
 }
 std::cout<<"Native source pixel inputs exported for private reconstruction; no fonts included.\n";
}catch(std::exception&e){std::cerr<<e.what()<<'\n';return 1;}}
