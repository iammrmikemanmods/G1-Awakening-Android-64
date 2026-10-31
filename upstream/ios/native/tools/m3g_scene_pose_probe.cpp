#include "g1/M3GScenePose.hpp"
#include "g1/Archive.hpp"

#include <algorithm>
#include <cmath>
#include <cstring>
#include <iomanip>
#include <iostream>

namespace {
std::uint32_t bits(float value) { std::uint32_t result;std::memcpy(&result,&value,4);return result; }
}
int main(int argc,char** argv) {
    try {
        if(argc!=2)throw std::runtime_error("usage: m3g_scene_pose_probe scene.m3g");
        const auto bytes=g1::read_file(argv[1]);
        const g1::M3GScene scene(bytes);const g1::M3GAnimations animations(bytes);
        g1::M3GScenePose pose(scene,g1::decode_m3g_cameras(bytes));
        std::cout<<"{\"bytes\":"<<bytes.size()<<",\"nodes\":"<<pose.local_nodes().size()
                 <<",\"cameras\":"<<pose.cameras().size()<<",\"poses\":[";
        bool first=true;
        for(const auto time:{0,666,1666,3333,6666,11666}) {
            const auto samples=animations.sample(time);pose.apply(samples);
            const auto nodes=pose.evaluated_nodes();
            std::size_t enabled=0;float minimum=1,maximum=0;
            std::map<std::uint32_t,std::size_t> properties;
            for(const auto& sample:samples)++properties[sample.property];
            for(const auto& entry:nodes) {
                enabled+=entry.second.rendering_enabled ? 1U : 0U;
                minimum=std::min(minimum,entry.second.effective_alpha);
                maximum=std::max(maximum,entry.second.effective_alpha);
            }
            if(!first)std::cout<<',';
            first=false;
            std::cout<<"{\"time_ms\":"<<time<<",\"enabled\":"<<enabled
                     <<",\"minimum_alpha_bits\":"<<bits(minimum)<<",\"maximum_alpha_bits\":"<<bits(maximum)
                     <<",\"property_targets\":{";
            bool first_property=true;
            for(const auto& property:properties) {
                if(!first_property)std::cout<<',';
                first_property=false;
                std::cout<<'"'<<property.first<<"\":"<<property.second;
            }
            std::cout<<"},\"camera_fields\":[";bool first_camera=true;
            for(const auto& entry:pose.cameras()) {
                const auto& camera=entry.second;
                const auto matrix=g1::m3g_camera_metal_view_projection(camera,nodes.at(entry.first).model);
                for(const auto value:matrix)if(!std::isfinite(value))throw std::runtime_error("nonfinite evaluated camera");
                if(!first_camera)std::cout<<',';
                first_camera=false;
                std::cout<<"{\"object\":"<<entry.first<<",\"type\":"<<unsigned(camera.type)
                         <<",\"field_bits\":"<<bits(camera.parameters[0])<<'}';
            }
            std::cout<<"]}";
        }
        std::cout<<"]}\n";return 0;
    }catch(const std::exception& error) { std::cerr<<error.what()<<'\n';return 1; }
}
