#include "g1/M3GRenderFrame.hpp"
#include "m3g_mesh_queue_fixture.hpp"
#include "m3g_animation_fixture.hpp"

#include <cmath>
#include <cstring>
#include <functional>
#include <iostream>

namespace {
int checks=0,original=0;
void check(bool condition,const char* message) { ++checks;if(!condition)throw std::runtime_error(message); }
void equal(std::uint32_t value,std::uint32_t expected,const char* message) { ++original;check(value==expected,message); }
void rejects(const std::function<void()>& action,const char* message) {
    bool failed=false;try { action(); }catch(const g1::FormatError&) { failed=true; }check(failed,message);
}
float value(std::uint32_t bits) { float out;std::memcpy(&out,&bits,4);return out; }
std::uint32_t bits(float v) { std::uint32_t out;std::memcpy(&out,&v,4);return out; }
g1::M3GMatrix matrix(float z) {
    return {{1,0,0,0, 0,1,0,0, 0,0,1,0, 0,0,z,1}};
}
} // namespace
int main() {
    try {
        for(const auto& reference:g1_queue_test::cases) {
            g1::M3GMeshQueueInput input;input.blending=reference.blend;
            volatile float effective=value(reference.parent)*value(reference.node);
            input.effective_alpha=effective;
            equal(bits(input.effective_alpha),reference.effective,"original inherited classification alpha mismatch");
            if(reference.material>=0)input.material_diffuse_alpha=static_cast<float>(reference.material)/255.0F;
            input.vertex_color_components=reference.colors;
            input.default_color_alpha=static_cast<float>(reference.default_alpha)/255.0F;
            if(reference.texture0>=0)input.texture_image_formats[0]=static_cast<std::uint8_t>(reference.texture0);
            if(reference.texture1>=0)input.texture_image_formats[1]=static_cast<std::uint8_t>(reference.texture1);
            const bool transparent=g1::m3g_mesh_queue_transparent(input);
            equal(transparent ? 1U : 0U,reference.transparent ? 1U : 0U,"original mesh queue classification mismatch");
            equal(transparent ? 0U : static_cast<std::uint32_t>(reference.serial),
                  static_cast<std::uint32_t>(reference.grouping),"original opaque grouping selection mismatch");
        }
        g1::Bytes rank_raw;
        auto appearance=g1_test::base();appearance.push_back(0);
        for(int i=0;i<5;++i)g1_test::u32(appearance,0);
        g1_test::append(rank_raw,g1_test::chunk(3,appearance)); // object2
        auto group=g1_test::node();g1_test::u32(group,0);
        g1_test::append(rank_raw,g1_test::chunk(9,group)); // object3
        g1_test::append(rank_raw,g1_test::chunk(3,appearance)); // distinct identical-body object4
        auto world=g1_test::node();g1_test::u32(world,1);g1_test::u32(world,3);
        g1_test::u32(world,0);g1_test::u32(world,0);
        g1_test::append(rank_raw,g1_test::chunk(22,world));
        const auto rank_keys=g1::m3g_mesh_appearance_order_keys(g1::M3GScene(g1_test::file(rank_raw)));
        check(rank_keys==std::map<std::size_t,std::int32_t>{{2,0},{4,1}},
              "Appearance order keys used raw IDs or merged distinct identical records");
        g1::M3GRenderAsset asset;
        g1::M3GSurface opaque;opaque.mesh_object=10;opaque.appearance_object=1;
        opaque.appearance.blending=68;opaque.default_color_alpha=0.5F;
        g1::M3GSurface transparent;transparent.mesh_object=11;transparent.appearance_object=2;
        transparent.appearance.blending=64;transparent.appearance.texture=g1::M3GTextureState{};
        transparent.appearance.texture->image_format=100;
        asset.geometry.surfaces={opaque,transparent};
        g1::M3GSpriteAsset sprite;sprite.node_object=20;sprite.appearance_object=3;sprite.image_object=4;
        sprite.image_format=100;sprite.scaled=true;sprite.crop_pixels={{0,0,64,128}};sprite.appearance.blending=64;
        asset.sprites={sprite};
        g1::RgbaImage image;image.width=64;image.height=128;asset.geometry.images.emplace(4,image);
        asset.traversal={{g1::M3GDrawableKind::mesh,0},{g1::M3GDrawableKind::sprite,0},{g1::M3GDrawableKind::mesh,1}};
        std::map<std::size_t,g1::M3GEvaluatedNode> nodes{{10,{matrix(-5),1,true,1}},
                                                      {11,{matrix(-20),1,true,1}},
                                                      {20,{matrix(-10),1,true,1}}};
        const std::map<std::size_t,std::int32_t> serials{{1,37}};
        g1::M3GRenderInstance instance;instance.asset=&asset;instance.nodes=&nodes;
        instance.mesh_appearance_serials=&serials;
        g1::M3GRenderView view;view.viewport_pixels={{640,480}};view.scope=1;
        view.projection.type=50;view.projection.parameters={{60,1.5F,1,100}};
        const auto frame=g1::prepare_m3g_frame({instance},view);
        check(frame.draws.size()==3,"mixed frame draw count changed");
        check(frame.draws[0].node_object==10 && frame.draws[1].node_object==11 && frame.draws[2].node_object==20,
              "mesh and sprite did not share the original opaque/distance queue");
        const auto& projected=frame.draws[2];
        check(projected.sprite_projection.drawn() && projected.draws_geometry(),"actual sprite geometry was not prepared");
        check(projected.sprite_projection.rectangle_fixed16==std::array<std::int32_t,4>{{-3783,5676,3784,-5675}},
              "original perspective rectangle changed in native frame integration");
        check(projected.sprite_vertices[0].position[0]==-3783.0F/65536.0F &&
              projected.sprite_vertices[0].position[1]==5676.0F/65536.0F &&
              projected.sprite_vertices[0].position[2]==(projected.sprite_projection.center_ndc_gl[2]+1)*0.5F &&
              projected.sprite_vertices[0].position[3]==1,"Sprite3D clip/depth conversion changed");
        check(projected.sprite_vertices[0].texcoord==std::array<float,4>{{0,0,0,1}} &&
              projected.sprite_vertices[5].texcoord==std::array<float,4>{{1,1,0,1}},"Sprite3D strip conversion UV order changed");
        check(projected.sprite_vertices[3].position==projected.sprite_vertices[2].position &&
              projected.sprite_vertices[4].position==projected.sprite_vertices[1].position,
              "Sprite3D expanded triangle winding changed");
        auto missing=instance;missing.mesh_appearance_serials=nullptr;
        rejects([&] { (void)g1::prepare_m3g_frame({missing},view); },"opaque object index silently replaced missing serial");
        rejects([&] { (void)g1::prepare_m3g_frame({instance,instance},view); },"duplicate instance namespace accepted");
        nodes.at(10).scope=2;
        check(g1::prepare_m3g_frame({instance},view).draws.size()==2,"drawable scope mismatch was not omitted");
        nodes.at(11).rendering_enabled=false;
        check(g1::prepare_m3g_frame({instance},view).draws.size()==1,"hidden evaluated mesh remained in queue");
        nodes.at(20).model=matrix(10);
        const auto behind=g1::prepare_m3g_frame({instance},view);
        check(behind.draws.size()==1 && !behind.draws[0].draws_geometry(),"culled sprite was replaced by geometry or lost its queue record");
        nodes.erase(20);
        rejects([&] { (void)g1::prepare_m3g_frame({instance},view); },"missing Sprite3D pose silently skipped");
        std::cout<<"{\"checks\":"<<checks<<",\"classification_cases\":"<<g1_queue_test::cases.size()
                 <<",\"original_classification_fields\":"<<original<<"}\n";return 0;
    }catch(const std::exception& error) { std::cerr<<"FAIL: "<<error.what()<<'\n';return 1; }
}
