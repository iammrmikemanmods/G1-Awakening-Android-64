#include "g1/M3GBindings.hpp"
#include "g1/Archive.hpp"

#include <iostream>
#include <set>

namespace {
void optional(std::optional<std::size_t> value) {
    if(value)std::cout<<*value;else std::cout<<"null";
}
void camera(const std::optional<g1::M3GModelCameraBinding>& value) {
    if(!value) { std::cout<<"null";return; }
    std::cout<<"{\"transform\":"<<value->transform<<",\"inner_group\":"<<value->inner_group
             <<",\"camera\":"<<value->camera<<'}';
}
} // namespace

int main(int argc,char** argv) {
    try {
        if(argc!=2)throw std::runtime_error("usage: m3g_bindings_probe scene.m3g");
        const g1::M3GScene scene(g1::read_file(argv[1]));const g1::M3GNodeBindings bindings(scene);
        const g1::M3GObjectInfo* world=nullptr;std::set<std::uint32_t> queries;
        for(const auto& object:scene.objects()) {
            if(object.type==22) {
                if(world)throw g1::FormatError("binding audit requires one serialized World");
                world=&object;
            }
            if(object.is_node)queries.insert(object.user_id);
        }
        if(!world)throw g1::FormatError("binding audit has no serialized World");
        const std::vector<std::size_t> ordered(world->children.begin(),world->children.end());
        const auto selected=bindings.select_loaded_container_group(ordered);
        const auto forms=bindings.form_nodes(selected);
        std::cout<<"{\"world\":"<<world->index<<",\"ordered_world_children\":[";
        for(std::size_t i=0;i<ordered.size();++i) { if(i)std::cout<<',';std::cout<<ordered[i]; }
        std::cout<<"],\"selected_group\":"<<selected<<",\"forms\":{\"robot\":";optional(forms.robot);
        std::cout<<",\"car\":";optional(forms.car);std::cout<<"},\"model_cameras\":{\"robot\":";
        camera(bindings.model_camera(selected,g1::M3GModelCameraKind::robot));std::cout<<",\"vehicle\":";
        camera(bindings.model_camera(selected,g1::M3GModelCameraKind::vehicle));
        std::cout<<"},\"transform_keys\":{\"robot\":"<<g1::m3g_transform_name_hash("robot")
                 <<",\"car\":"<<g1::m3g_transform_name_hash("car")
                 <<",\"roboCAM\":"<<g1::m3g_transform_name_hash("roboCAM")
                 <<",\"vehiCAM\":"<<g1::m3g_transform_name_hash("vehiCAM")<<"},\"node_queries\":[";
        queries.insert(0xffffffffU);bool first=true;
        for(const auto key:queries) {
            if(!first)std::cout<<',';
            first=false;
            std::cout<<'['<<key<<',';optional(bindings.find_node(selected,key));std::cout<<']';
        }
        std::cout<<"]}\n";return 0;
    }catch(const std::exception& error) { std::cerr<<"ERROR: "<<error.what()<<'\n';return 1; }
}
