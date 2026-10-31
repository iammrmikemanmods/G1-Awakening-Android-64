#include "g1/M3GScene.hpp"
#include "m3g_animation_fixture.hpp"

#include <functional>
#include <iostream>

namespace {
int checks=0;
void check(bool condition,const char* message) { ++checks;if(!condition)throw std::runtime_error(message); }
void rejects(const std::function<void()>& action,const char* message) {
    bool failed=false;try { action(); }catch(const g1::FormatError&) { failed=true; }check(failed,message);
}
g1::Bytes fixture(bool hidden=false,bool null_appearance=false,std::uint32_t width=2) {
    using namespace g1_test;
    g1::Bytes raw;
    auto image=base();append(image,{100,0});u32(image,width);u32(image,2);u32(image,0);
    u32(image,width*2*4);for(std::uint32_t i=0;i<width*2;++i)append(image,{255,0,0,128});
    append(raw,chunk(10,image)); //2
    auto composite=base();append(composite,{1,1,1,1,64,25});scalar(composite,0);scalar(composite,0);
    append(raw,chunk(6,composite)); //3
    auto appearance=base();append(appearance,{0});u32(appearance,3);
    u32(appearance,0);u32(appearance,0);u32(appearance,0);u32(appearance,0);
    append(raw,chunk(3,appearance)); //4
    auto sprite=base();append(sprite,{0,0,static_cast<std::uint8_t>(!hidden),1,128});u32(sprite,2);sprite.push_back(0);
    u32(sprite,2);u32(sprite,null_appearance ? 0 : 4);sprite.push_back(1);
    u32(sprite,1);u32(sprite,0);u32(sprite,static_cast<std::uint32_t>(-2));u32(sprite,2);
    append(raw,chunk(18,sprite)); //5
    auto world=node();u32(world,1);u32(world,5);u32(world,0);u32(world,0);
    append(raw,chunk(22,world));
    return file(raw);
}
} // namespace
int main() {
    try {
        const g1::M3GScene scene(fixture());
        rejects([&] { (void)scene.static_model(); },"rigid-only API silently accepted required Sprite3D");
        const auto asset=scene.render_asset();
        check(asset.geometry.surfaces.empty() && !asset.geometry.has_geometry && asset.sprites.size()==1,
              "sprite-only asset fabricated rigid geometry");
        check(asset.geometry.images.size()==1 && asset.geometry.images.at(2).pixels.size()==16,
              "Sprite3D image was not decoded");
        const auto& sprite=asset.sprites.front();
        check(sprite.node_object==5 && sprite.image_object==2 && sprite.appearance_object==4,
              "Sprite3D references changed");
        check(sprite.crop_pixels==std::array<std::int32_t,4>{{1,0,-2,2}} && sprite.scaled,
              "signed Sprite3D crop/scaled flag changed");
        check(sprite.scope==2 && sprite.image_format==100 && sprite.effective_alpha==128.0F/255.0F,
              "Sprite3D inherited state or format changed");
        check(sprite.appearance.blending==64 && sprite.appearance.culling==162 &&
              sprite.appearance.alpha_threshold==25.0F/255.0F && !sprite.appearance.texture,
              "Sprite3D image/compositing/cull state changed");
        check(asset.traversal.size()==1 && asset.traversal[0].kind==g1::M3GDrawableKind::sprite &&
              asset.traversal[0].index==0,"Sprite3D traversal reference missing");
        const g1::M3GScene hidden(fixture(true));
        check(hidden.static_model().surfaces.empty(),"hidden static Sprite3D was not skipped");
        check(hidden.render_asset().sprites.size()==1,"stateful asset discarded a hidden animatable Sprite3D");
        check(g1::M3GScene(fixture(false,true)).render_asset().sprites.empty(),
              "null Sprite3D appearance generated a drawable");
        rejects([&] { (void)g1::M3GScene(fixture(false,false,3)).render_asset(); },
                "unrecovered non-power-of-two Sprite3D image accepted");
        const g1::M3GScene deep(g1_test::deep_hierarchy(20000));
        check(deep.render_asset().traversal.empty(),"deep iterative render traversal fabricated draws");
        check(!deep.static_model().has_geometry,"deep static hierarchy failed iterative evaluation");
        std::cout<<checks<<" native M3G render-asset contracts passed\n";return 0;
    }catch(const std::exception& error) { std::cerr<<"FAIL: "<<error.what()<<'\n';return 1; }
}
