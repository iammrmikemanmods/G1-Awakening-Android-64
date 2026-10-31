#include "g1/M3GCamera.hpp"

#include <cstring>
#include <iostream>

namespace {
std::uint32_t bits(float value) { std::uint32_t result;std::memcpy(&result,&value,4);return result; }
template<std::size_t N> void print_bits(const std::array<float,N>& values) {
    std::cout<<'[';for(std::size_t i=0;i<N;++i) { if(i)std::cout<<',';std::cout<<bits(values[i]); }std::cout<<']';
}
}

int main(int argc,char** argv) {
    try {
        if(argc!=2)throw std::runtime_error("usage: g1_m3g_camera_probe LOCAL_MODEL.m3g");
        const auto cameras=g1::decode_m3g_cameras(g1::read_file(argv[1],g1::Archive::max_resource_size));
        std::cout<<"{\"cameras\":[";bool comma=false;
        for(const auto& [index,camera]:cameras) {
            if(comma)std::cout<<',';
            comma=true;
            std::cout<<"{\"object\":"<<index<<",\"type\":"<<unsigned(camera.type)<<",\"parameter_bits\":";
            print_bits(camera.parameters);std::cout<<",\"projection_bits\":";
            print_bits(g1::m3g_camera_projection_matrix(camera));std::cout<<'}';
        }
        std::cout<<"]}\n";return 0;
    } catch(const std::exception& error) { std::cerr<<"FAIL: "<<error.what()<<'\n';return 1; }
}
