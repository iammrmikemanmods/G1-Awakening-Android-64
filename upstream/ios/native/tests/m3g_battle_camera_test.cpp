#include "g1/M3GBattleCamera.hpp"
#include "m3g_battle_camera_fixture.hpp"

#include <cstring>
#include <functional>
#include <iostream>
#include <limits>

namespace {
std::size_t checks=0,original_fields=0;
void check(bool condition,const char* message) {
    ++checks;if(!condition)throw std::runtime_error(message);
}
void original(bool condition,const char* message) {
    ++original_fields;check(condition,message);
}
void rejects(const std::function<void()>& action,const char* message) {
    bool rejected=false;try { action(); }catch(const g1::FormatError&) { rejected=true; }
    check(rejected,message);
}
std::uint32_t bits(float value) { std::uint32_t out;std::memcpy(&out,&value,sizeof(out));return out; }
void effects(const std::vector<g1::M3GBattleCameraEffect>& actual,
             const std::vector<g1::M3GBattleCameraEffect>& expected) {
    original(actual.size()==expected.size(),"original camera effect count differs");
    for(std::size_t i=0;i<actual.size();++i) {
        original(actual[i].kind==expected[i].kind,"original camera effect ordering/kind differs");
        original(actual[i].node==expected[i].node,"original camera effect target differs");
        original(actual[i].time==expected[i].time,"original camera effect time differs");
        for(std::size_t k=0;k<3;++k)
            original(bits(actual[i].translation[k])==bits(expected[i].translation[k]),"original camera translation bits differ");
    }
}
} // namespace

int main() {
    try {
        const std::array<std::int32_t,2> clocks{{1777,3888}};
        for(const auto& row:g1_camera_test::rows) {
            const auto plan=g1::m3g_battle_camera_switch(row.request,row.previous,row.model_found,clocks,row.current,row.selected);
            original(plan.route.side_a==row.side_a,"original camera side differs");
            original(plan.route.scene_camera_name==row.scene_name,"original fallback camera name differs");
            original(plan.route.scene_camera_user_id==row.scene_key,"original raw camera hash differs");
            const auto key=plan.route.preferred_model_camera ?
                g1::m3g_transform_name_hash(*plan.route.preferred_model_camera==g1::M3GModelCameraKind::robot ? "roboCAM" : "vehiCAM") : 0U;
            original(key==row.model_key,"original embedded camera hash or bypass differs");
            original(plan.route.adjustment_flag==row.adjustment,"original adjustment byte differs");
            original(plan.before_time==row.before && plan.after_time==row.after,"original before/after clock differs");
            original(plan.state.side_a==row.side_a && plan.state.has_model_camera==row.model_found,"original stored camera state differs");
            effects(plan.effects,row.effects);
            const auto before=g1::m3g_battle_camera_propagation(row.current,row.before,row.adjustment,row.previous.has_model_camera);
            const auto after=g1::m3g_battle_camera_propagation(row.selected,row.after,row.adjustment,row.model_found);
            std::vector<g1::M3GBattleCameraEffect> joined=before;
            joined.push_back({g1::M3GBattleCameraEffectKind::set_active_camera,*row.selected.camera,0,{}});
            joined.insert(joined.end(),after.begin(),after.end());
            effects(joined,row.effects);
        }
        const auto before_contracts=checks;
        const g1::M3GBattleCameraNode camera{4,11},parent{4,10},grandparent{3,5};
        const g1::M3GBattleCameraAncestry full{camera,parent,grandparent};
        const auto propagation=[&](g1::M3GBattleCameraAncestry ancestry) {
            return g1::m3g_battle_camera_propagation(ancestry,123,1,false);
        };
        check(propagation({}).empty(),"null active camera emitted effects");
        check(propagation({camera,{},{}}).empty(),"parentless camera emitted effects");
        const auto shifted=propagation(full);
        check(shifted.size()==2 && shifted[0].node==grandparent && shifted[1].node==parent,
              "cross-instance actual ancestry was not preserved");
        check(shifted[1].kind==g1::M3GBattleCameraEffectKind::translate && shifted[1].translation==std::array<float,3>{{0,0,2}},
              "fallback adjustment was not additive positive Z2");
        check(g1::m3g_battle_camera_propagation(full,123,255,true).size()==1,"cached model camera received fallback translation");
        rejects([&] { (void)propagation({{},parent,{}}); },"parent without camera accepted");
        rejects([&] { (void)propagation({camera,{},grandparent}); },"grandparent without parent accepted");
        rejects([&] { (void)propagation({camera,camera,{}}); },"self parent accepted");
        rejects([&] { (void)propagation({camera,parent,camera}); },"camera ancestry cycle accepted");
        rejects([&] { (void)propagation({camera,parent,parent}); },"parent ancestry cycle accepted");
        rejects([&] { (void)propagation({g1::M3GBattleCameraNode{0,0},{},{}}); },"null object handle accepted");
        for(const auto id:{13U,255U})rejects([&] {
            (void)g1::m3g_battle_camera_route({static_cast<std::uint8_t>(id),{}, {}});
        },"unknown camera ID accepted");
        rejects([&] { (void)g1::m3g_battle_camera_switch({8,{},{}},{},true,clocks,{},full); },"ID8 fabricated a model camera");
        rejects([&] { (void)g1::m3g_battle_camera_switch({0,{},{}},{},false,clocks,{},{}); },"missing selected Camera accepted");
        const auto switch_to=[&](g1::M3GBattleCameraAncestry next) {
            return g1::m3g_battle_camera_switch({0,{},{}},{},false,clocks,full,next);
        };
        rejects([&] { (void)switch_to({camera,g1::M3GBattleCameraNode{4,12},{}}); },"same Camera with different parents accepted");
        rejects([&] { (void)switch_to({g1::M3GBattleCameraNode{4,12},parent,g1::M3GBattleCameraNode{4,13}}); },
                "same parent with different grandparents accepted");
        rejects([&] { (void)switch_to({camera,{},{}}); },"parent and explicit no-parent both accepted");
        rejects([&] { (void)switch_to({grandparent,camera,parent}); },"cycle across individually valid ancestry inputs accepted");
        const auto nonzero=g1::m3g_battle_camera_route({0,{{255,0}},{{255,0}}});
        check(nonzero.preferred_model_camera==g1::M3GModelCameraKind::vehicle && nonzero.scene_camera_name=="cam_auto_car" && nonzero.adjustment_flag==255,
              "nonzero form/adjustment bytes were reinterpreted as strict booleans");
        const auto seven=g1::m3g_battle_camera_route({7,{{1,1}},{{1,1}}});
        check(seven.side_a && seven.preferred_model_camera==g1::M3GModelCameraKind::robot && seven.adjustment_flag==0,
              "camera7 was normalized to sideB or its vehicle form");
        const auto negative=g1::m3g_battle_camera_switch({0,{},{}},{true,true},true,
            {{std::numeric_limits<std::int32_t>::min(),-1}},full,full);
        check(negative.before_time==std::numeric_limits<std::int32_t>::min() && negative.after_time==negative.before_time,
              "signed camera clock was clamped or arithmetically changed");
        const auto repeated=g1::m3g_battle_camera_switch({0,{},{{1,0}}},{false,true},false,clocks,full,full);
        check(repeated.effects.size()==5 && repeated.effects[1].kind==g1::M3GBattleCameraEffectKind::translate &&
              repeated.effects[3].kind==g1::M3GBattleCameraEffectKind::animate &&
              repeated.effects[4].kind==g1::M3GBattleCameraEffectKind::translate &&
              repeated.effects[1].node==parent && repeated.effects[4].node==parent,
              "ordered before/after additive operations on the same parent were deduplicated");
        std::cout<<"{\"checks\":"<<checks<<",\"original_cases\":"<<g1_camera_test::rows.size()
                 <<",\"original_field_comparisons\":"<<original_fields<<",\"native_contracts\":"<<checks-before_contracts<<"}\n";
        return 0;
    }catch(const std::exception& error) { std::cerr<<"FAIL: "<<error.what()<<'\n';return 1; }
}
