#include "../tests/m3g_sprite3d_fixture.hpp"

#include <fstream>
#include <iostream>

namespace {
template<class T,std::size_t N> void array(const std::array<T,N>& values) {
    std::cout<<'[';for(std::size_t n=0;n<N;++n) { if(n)std::cout<<',';std::cout<<values[n]; }std::cout<<']';
}
}
int main(int argc,char** argv) {
    try {
        if(argc!=2)throw std::runtime_error("Usage: m3g_sprite3d_probe original_fixture.txt");
        std::ifstream input(argv[1]);if(!input)throw std::runtime_error("Cannot open Sprite3D fixture");
        const auto fixture=sprite3d_fixture::read(input);
        std::cout<<"{\"cases\":[";
        bool first=true;
        for(const auto& entry:fixture.sprites) {
            const auto result=g1::project_m3g_sprite3d(entry.input);
            if(!first)std::cout<<',';
            first=false;
            // Names in this fixed local fixture contain only alphanumerics/_ .
            for(const auto c:entry.name)
                if(!((c>='a'&&c<='z')||(c>='A'&&c<='Z')||(c>='0'&&c<='9')||c=='_'||c=='.'))
                    throw std::runtime_error("Unsafe Sprite3D fixture name");
            std::cout<<"{\"name\":\""<<entry.name<<"\",\"result\":"<<unsigned(result.result)
                     <<",\"drawn\":"<<(result.drawn()?"true":"false");
            if(result.drawn()) {
                std::array<std::uint32_t,3> origin{};
                for(std::size_t n=0;n<3;++n)origin[n]=sprite3d_fixture::bits(result.center_ndc_gl[n]);
                std::cout<<",\"center_bits\":";array(origin);
                std::cout<<",\"rectangle_fixed16\":";array(result.rectangle_fixed16);
                std::cout<<",\"crop_bounds_fixed16\":";array(result.crop_bounds_fixed16);
                std::cout<<",\"mirror_x\":"<<(result.mirror_x?"true":"false")
                         <<",\"mirror_y\":"<<(result.mirror_y?"true":"false");
                std::cout<<",\"positions_fixed16\":";array(result.offsets_fixed16);
                std::cout<<",\"uv_fixed16\":";array(result.uv_fixed16);
            }
            std::cout<<'}';
        }
        std::cout<<"],\"queues\":[";first=true;
        for(const auto& entry:fixture.queues) {
            if(!first)std::cout<<',';
            first=false;
            std::cout<<"{\"name\":\""<<entry.name<<"\",\"keys\":[";
            for(std::size_t n=0;n<entry.entries.size();++n) {
                if(n)std::cout<<',';
                const auto& request=entry.entries[n];
                std::cout<<g1::m3g_draw_queue_key(request.layer,request.transparent,request.world_origin,entry.camera);
            }
            const auto order=g1::m3g_draw_queue_order(entry.entries,entry.camera);
            std::cout<<"],\"order\":[";for(std::size_t n=0;n<order.size();++n) { if(n)std::cout<<',';std::cout<<order[n]; }
            std::cout<<"]}";
        }
        std::cout<<"]}\n";
    } catch(const std::exception& error) { std::cerr<<error.what()<<'\n';return 1; }
}
