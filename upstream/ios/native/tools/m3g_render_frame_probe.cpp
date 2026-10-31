#include "g1/M3GRenderFrame.hpp"
#include "g1/Archive.hpp"

#include <iostream>

int main(int argc,char** argv) {
    try {
        if(argc!=2)throw std::runtime_error("usage: m3g_render_frame_probe scene.m3g");
        const auto bytes=g1::read_file(argv[1]);const g1::M3GScene scene(bytes);
        const g1::M3GAnimations animation(bytes);const auto asset=scene.render_asset();
        const auto order_keys=g1::m3g_mesh_appearance_order_keys(scene);
        g1::M3GScenePose pose(scene,g1::decode_m3g_cameras(bytes));
        if(pose.cameras().empty())throw std::runtime_error("probe requires explicit serialized camera seeds");
        const std::array<std::size_t,2> cameras{{pose.cameras().begin()->first,pose.cameras().rbegin()->first}};
        std::cout<<"{\"appearance_order_keys\":[";bool first=true;
        for(const auto& entry:order_keys) {
            if(!first)std::cout<<',';
            first=false;
            std::cout<<'['<<entry.first<<','<<entry.second<<']';
        }
        std::cout<<"],\"frames\":[";first=true;
        for(const auto time:{0,666,1666,3333,6666,11666}) {
            pose.apply(animation.sample(time));const auto nodes=pose.evaluated_nodes();
            for(const auto camera:cameras) {
                g1::M3GRenderInstance instance;instance.asset=&asset;instance.nodes=&nodes;instance.identity=1;
                instance.mesh_appearance_serials=&order_keys;
                g1::M3GRenderView view;view.projection=pose.cameras().at(camera);
                view.camera_to_world=nodes.at(camera).model;view.scope=nodes.at(camera).scope;
                view.viewport_pixels={{480,320}};
                const auto frame=g1::prepare_m3g_frame({instance},view);
                std::size_t mesh_draws=0,sprite_draws=0,culled_sprites=0;
                for(const auto& draw:frame.draws) {
                    if(draw.kind==g1::M3GDrawableKind::mesh)++mesh_draws;
                    else if(draw.draws_geometry())++sprite_draws;
                    else ++culled_sprites;
                }
                if(!first)std::cout<<',';
            first=false;
                std::cout<<"{\"time_ms\":"<<time<<",\"camera_object\":"<<camera<<",\"mesh_draws\":"<<mesh_draws
                         <<",\"sprite_draws\":"<<sprite_draws<<",\"culled_sprites\":"<<culled_sprites
                         <<",\"queue\":[";
                bool first_draw=true;
                for(const auto& draw:frame.draws) {
                    if(!first_draw)std::cout<<',';
                    first_draw=false;
                    std::cout<<"[\""<<(draw.kind==g1::M3GDrawableKind::mesh?"mesh":"sprite")<<"\","<<draw.node_object
                             <<','<<draw.geometry_index<<','<<draw.queue_key<<','<<draw.grouping_key<<']';
                }
                std::cout<<"]}";
            }
        }
        std::cout<<"]}\n";return 0;
    }catch(const std::exception& error) { std::cerr<<error.what()<<'\n';return 1; }
}
