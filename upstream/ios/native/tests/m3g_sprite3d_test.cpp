#include "m3g_sprite3d_fixture.hpp"

#include <fstream>
#include <iostream>
#include <limits>

namespace {
std::size_t original_checks=0,contract_checks=0;
template<class T> void equal(const T& actual,const T& expected,const std::string& context) {
    ++original_checks;
    if(actual!=expected)throw std::runtime_error("Original Sprite3D comparison failed: "+context);
}
template<class T,std::size_t N> void equal_array(const std::array<T,N>& actual,const std::array<T,N>& expected,
                                                const std::string& context) {
    for(std::size_t n=0;n<N;++n)equal(actual[n],expected[n],context+"["+std::to_string(n)+"]");
}
template<class Fn> void rejects(Fn fn) {
    bool rejected=false;
    try { fn(); } catch(const g1::FormatError&) { rejected=true; }
    ++contract_checks;
    if(!rejected)throw std::runtime_error("Malformed Sprite3D input was accepted");
}
void contract(bool condition,const char* message) {
    ++contract_checks;if(!condition)throw std::runtime_error(message);
}
}

int main(int argc,char** argv) {
    try {
        if(argc!=2)throw std::runtime_error("Usage: g1_m3g_sprite3d_tests original_fixture.txt");
        std::ifstream input(argv[1]);if(!input)throw std::runtime_error("Cannot open Sprite3D original fixture");
        const auto fixture=sprite3d_fixture::read(input);
        if(fixture.sprites.size()!=142 || fixture.queues.size()!=4)throw std::runtime_error("Original Sprite3D corpus counts changed");
        std::size_t draws=0,queue_entries=0;
        for(const auto& entry:fixture.sprites) {
            const auto result=g1::project_m3g_sprite3d(entry.input);
            equal(result.drawn(),entry.draws,entry.name+" draw/reject");
            if(!entry.draws)continue;
            ++draws;
            for(std::size_t n=0;n<3;++n)
                equal(sprite3d_fixture::bits(result.center_ndc_gl[n]),sprite3d_fixture::bits(entry.expected.center_ndc_gl[n]),entry.name+" origin bits");
            equal_array(result.rectangle_fixed16,entry.expected.rectangle_fixed16,entry.name+" preclip rectangle");
            equal_array(result.crop_bounds_fixed16,entry.expected.crop_bounds_fixed16,entry.name+" source crop");
            equal(result.mirror_x,entry.expected.mirror_x,entry.name+" mirror X");
            equal(result.mirror_y,entry.expected.mirror_y,entry.name+" mirror Y");
            equal_array(result.offsets_fixed16,entry.expected.offsets_fixed16,entry.name+" final position");
            equal_array(result.uv_fixed16,entry.expected.uv_fixed16,entry.name+" final UV");
        }
        for(const auto& queue:fixture.queues) {
            for(std::size_t n=0;n<queue.entries.size();++n) {
                const auto& entry=queue.entries[n];++queue_entries;
                equal(g1::m3g_draw_queue_key(entry.layer,entry.transparent,entry.world_origin,queue.camera),queue.keys[n],queue.name+" key");
            }
            const auto order=g1::m3g_draw_queue_order(queue.entries,queue.camera);
            if(order.size()!=queue.order.size())throw std::runtime_error("Original Sprite3D queue size differs");
            for(std::size_t n=0;n<order.size();++n)equal(order[n],queue.order[n],queue.name+" order");
        }
        if(draws!=47 || queue_entries!=26 || original_checks!=1557)throw std::runtime_error("Original Sprite3D comparison coverage changed");
        const auto baseline=fixture.sprites.front().input;
        const float nan=std::numeric_limits<float>::quiet_NaN(),inf=std::numeric_limits<float>::infinity();
        auto changed=baseline;changed.model_to_world[0]=nan;
        rejects([&]{g1::project_m3g_sprite3d(changed);});
        changed=baseline;changed.camera_view[12]=inf;
        rejects([&]{g1::project_m3g_sprite3d(changed);});
        changed=baseline;changed.projection_gl[0]=inf;
        rejects([&]{g1::project_m3g_sprite3d(changed);});
        changed=baseline;changed.model_to_world[3]=1;
        rejects([&]{g1::project_m3g_sprite3d(changed);});
        changed=baseline;changed.camera_view[15]=0;
        rejects([&]{g1::project_m3g_sprite3d(changed);});
        changed=baseline;changed.projection_type=47;
        rejects([&]{g1::project_m3g_sprite3d(changed);});
        changed=baseline;changed.projection_gl[4]=1;
        rejects([&]{g1::project_m3g_sprite3d(changed);});
        changed=baseline;changed.viewport_pixels[0]=0;
        rejects([&]{g1::project_m3g_sprite3d(changed);});
        changed=baseline;changed.viewport_pixels[1]=16777217U;
        rejects([&]{g1::project_m3g_sprite3d(changed);});
        changed=baseline;changed.image_pixels[0]=0;
        rejects([&]{g1::project_m3g_sprite3d(changed);});
        changed=baseline;changed.image_pixels[1]=1025;
        rejects([&]{g1::project_m3g_sprite3d(changed);});
        changed=baseline;changed.image_pixels[0]=63;
        rejects([&]{g1::project_m3g_sprite3d(changed);});
        changed=baseline;changed.crop_pixels[2]=-1025;
        rejects([&]{g1::project_m3g_sprite3d(changed);});
        changed=baseline;changed.crop_pixels[0]=nan;
        rejects([&]{g1::project_m3g_sprite3d(changed);});
        changed=baseline;changed.crop_pixels[0]=2147483648.0F;
        rejects([&]{g1::project_m3g_sprite3d(changed);});
        changed=baseline;changed.model_to_world[0]=std::numeric_limits<float>::max();
        rejects([&]{g1::project_m3g_sprite3d(changed);});
        changed=baseline;changed.model_to_world[0]=10000000;
        rejects([&]{g1::project_m3g_sprite3d(changed);});
        changed=baseline;changed.model_to_world[14]=std::numeric_limits<float>::max();
        changed.projection_gl[11]=2;changed.projection_gl[10]=0;changed.projection_gl[14]=0;
        rejects([&]{g1::project_m3g_sprite3d(changed);});
        rejects([&]{g1::m3g_draw_queue_key(-64,false,{{0,0,0}},{{0,0,0}});});
        rejects([&]{g1::m3g_draw_queue_key(64,false,{{0,0,0}},{{0,0,0}});});
        rejects([&]{g1::m3g_draw_queue_key(0,true,{{nan,0,0}},{{0,0,0}});});
        rejects([&]{g1::m3g_draw_queue_key(0,true,{{std::numeric_limits<float>::max(),0,0}},{{0,0,0}});});
        rejects([&]{g1::m3g_draw_queue_order({g1::M3GDrawQueueEntry{}},{{0,0,0}});});
        changed=baseline;changed.appearance_present=false;
        contract(g1::project_m3g_sprite3d(changed).result==g1::M3GSprite3DResult::no_appearance,"Null appearance rejection reason differs");
        changed=baseline;changed.crop_pixels[2]=0;
        contract(g1::project_m3g_sprite3d(changed).result==g1::M3GSprite3DResult::zero_crop,"Zero crop rejection reason differs");
        changed=baseline;changed.crop_pixels[0]=1000;
        contract(g1::project_m3g_sprite3d(changed).result==g1::M3GSprite3DResult::outside_image,"Crop intersection rejection reason differs");
        changed=baseline;changed.model_to_world[14]=0;
        contract(g1::project_m3g_sprite3d(changed).result==g1::M3GSprite3DResult::projection_at_infinity,"Near-zero homogeneous projection was not rejected");
        changed=baseline;changed.model_to_world[0]=0;
        const auto zero=g1::project_m3g_sprite3d(changed);
        contract(zero.drawn() && zero.rectangle_fixed16[0]==0 && zero.rectangle_fixed16[2]==0,"Finite zero-axis scale received replacement geometry");
        contract(g1::m3g_draw_queue_order({},{{0,0,0}}).empty(),"Empty draw queue is not empty");
        const std::vector<g1::M3GDrawQueueEntry> stable{{1,0,true,{{0,0,0}},0},{2,0,true,{{0,0,0}},0}};
        contract(g1::m3g_draw_queue_order(stable,{{0,0,0}})==std::vector<std::size_t>({0,1}),"Transparent equal-distance object order changed");
        std::cout<<"{\"renderer_cases\":"<<fixture.sprites.size()<<",\"draws\":"<<draws
                 <<",\"queue_sequences\":"<<fixture.queues.size()<<",\"queue_entries\":"<<queue_entries
                 <<",\"original_value_checks\":"<<original_checks<<",\"contract_checks\":"<<contract_checks<<",\"passed\":true}\n";
    } catch(const std::exception& error) { std::cerr<<error.what()<<'\n';return 1; }
}
