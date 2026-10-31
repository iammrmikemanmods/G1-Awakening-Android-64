#include "g1/M3GMaterialOverrides.hpp"

#include <iostream>
#include <limits>
#include <string>

namespace {
std::uint32_t number(const char* text) {
    const std::string input(text);std::size_t consumed=0;
    if(input.empty() || input[0]=='-')throw std::runtime_error("invalid unsigned argument");
    const auto result=std::stoull(input,&consumed,0);
    if(consumed!=input.size() || result>std::numeric_limits<std::uint32_t>::max())
        throw std::runtime_error("unsigned argument exceeds original domain");
    return std::uint32_t(result);
}
void optional(std::optional<std::size_t> value) {
    if(value)std::cout<<*value;else std::cout<<"null";
}
void array(const std::vector<std::size_t>& values) {
    std::cout<<'[';
    for(std::size_t i=0;i<values.size();++i) { if(i)std::cout<<',';std::cout<<values[i]; }
    std::cout<<']';
}
}

int main(int argc,char** argv) {
    try {
        if(argc!=4)throw std::runtime_error("usage: m3g_material_probe model.m3g root_id effective_type | --image resource_id image.png | --archive-image resource_id iphone.big");
        if(std::string(argv[1])=="--image" || std::string(argv[1])=="--archive-image") {
            const auto image=std::string(argv[1])=="--image"
                ? g1::decode_m3g_material_image(number(argv[2]),g1::read_file(argv[3]))
                : g1::load_m3g_material_image(g1::Archive(g1::read_file(argv[3])),number(argv[2]));
            std::cout<<"{\"resource_id\":"<<image.resource_id<<",\"format\":"<<unsigned(image.image_format)
                     <<",\"width\":"<<image.image.width<<",\"height\":"<<image.image.height
                     <<",\"rgba_bytes\":"<<image.image.pixels.size()<<"}\n";
            std::cout.write(reinterpret_cast<const char*>(image.image.pixels.data()),std::streamsize(image.image.pixels.size()));
            if(!std::cout)throw std::runtime_error("material image output failed");
            return 0;
        }
        const g1::M3GMaterialBindings bindings(g1::read_file(argv[1]));
        const auto steps=bindings.plan(number(argv[2]),number(argv[3]));
        std::cout<<"{\"objects\":[";bool first=true;
        for(const auto& object:bindings.objects()) {
            if(!first)std::cout<<',';
            first=false;
            std::cout<<"{\"index\":"<<object.index<<",\"type\":"<<unsigned(object.type)
                     <<",\"user_id\":"<<object.user_id<<",\"references\":";array(object.references);
            std::cout<<",\"children\":";array(object.children);
            std::cout<<",\"texture_units\":";array(object.texture_units);
            std::cout<<",\"submesh_appearances\":";array(object.submesh_appearances);
            std::cout<<",\"texture_image\":"<<object.texture_image<<'}';
        }
        std::cout<<"],\"steps\":[";first=true;
        for(const auto& step:steps) {
            if(!first)std::cout<<',';
            first=false;
            std::cout<<"{\"slot\":"<<unsigned(step.slot)<<",\"resource_id\":"<<step.resource_id<<",\"named_object\":";
            optional(step.named_object);
            std::cout<<",\"appearance_object\":"<<step.appearance_object<<",\"texture_object\":";optional(step.texture_object);
            std::cout<<",\"authored_image_object\":";optional(step.authored_image_object);
            std::cout<<",\"used_mesh_fallback\":"<<(step.used_mesh_fallback?"true":"false")<<'}';
        }
        std::cout<<"]}\n";return 0;
    }catch(const std::exception& error) { std::cerr<<"ERROR: "<<error.what()<<'\n';return 1; }
}
