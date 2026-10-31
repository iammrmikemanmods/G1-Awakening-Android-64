#include "g1/M3GScenePose.hpp"
#include "m3g_animation_fixture.hpp"

#include <cmath>
#include <functional>
#include <iostream>
#include <limits>

namespace {
int checks=0;
void check(bool value,const char* message) {
    ++checks;if(!value)throw std::runtime_error(message);
}
void rejects(const std::function<void()>& action,const char* message) {
    bool rejected=false;try { action(); }catch(const g1::FormatError&) { rejected=true; }
    check(rejected,message);
}
}
int main() {
    try {
        const auto bytes=g1_test::weighted_animation(275,0.25F,0.75F);
        const g1::M3GAnimations animation(bytes);
        const auto all=animation.sample(50),selected=animation.sample_targets(50,{8});
        check(selected.size()==1 && all.size()==1 && selected[0].target==all[0].target &&
              selected[0].property==all[0].property && selected[0].value==all[0].value,
              "target restriction changed selected arithmetic");
        check(animation.sample_targets(50,{}).empty(),"empty target set sampled unrelated nodes");
        check(animation.sample_targets(50,{9}).empty(),"untracked parent animated a child implicitly");
        rejects([&] { (void)animation.sample_targets(50,{999}); },"unknown selected target accepted");
        const g1::M3GAnimations unrelated(g1_test::weighted_animation(257,1,1));
        check(unrelated.sample_targets(50,{9}).empty(),"excluded unsupported property was evaluated");
        rejects([&] { (void)unrelated.sample_targets(50,{8}); },"selected unsupported property was ignored");

        g1::M3GScenePose pose(bytes);
        pose.apply({{8,275,{1,2,3}},{8,270,{2,3,4}},{9,275,{100,200,300}}});
        g1::M3GMatrix general{{1,0,0,0,0,1,0,0,0,0,1,0,10,20,30,1}};
        pose.set_general_matrix(8,general);
        auto evaluated=pose.evaluated_nodes();
        check(evaluated.at(8).model[12]==121 && evaluated.at(8).model[13]==262 &&
              evaluated.at(8).model[14]==423,"general assignment replaced TRS or ancestor");
        pose.translate(8,{{0,0,2}});pose.translate(8,{{0,0,2}});
        check(pose.translation(8)==std::array<float,3>{{1,2,7}},"Translate effects were replaced or deduplicated");
        check(pose.evaluated_nodes().at(8).model[14]==427,"Translate changed a world/general component");
        pose.detach(8);evaluated=pose.evaluated_nodes();
        check(evaluated.at(8).model[12]==21 && evaluated.at(8).model[13]==62 &&
              evaluated.at(8).model[14]==127,"detach altered local components or retained parent");
        pose.set_rendering_enabled(9,false);
        check(pose.evaluated_nodes().at(8).rendering_enabled,"detached node retained former parent's visibility");
        pose.set_rendering_enabled(8,false);
        check(!pose.evaluated_nodes().at(8).rendering_enabled,"runtime rendering disable ignored");
        pose.set_rendering_enabled(8,true);
        check(pose.evaluated_nodes().at(8).rendering_enabled,"runtime rendering enable ignored");

        pose.set_translation(8,{{0,0,0}});
        pose.translate(8,{{1.0e-6F,-1.0e-6F,0}});
        check(pose.translation(8)==std::array<float,3>{{0,0,0}},"all-small original Translate should be a no-op");
        pose.translate(8,{{1.0e-6F,0,2}});
        check(pose.translation(8)==std::array<float,3>{{1.0e-6F,0,2}},"large-vector Translate incorrectly discarded small component");
        const auto denormal=std::numeric_limits<float>::denorm_min();
        pose.set_translation(8,{{-denormal,denormal,1.0e-6F}});
        check(pose.translation(8)==std::array<float,3>{{0,0,1.0e-6F}} &&
              !std::signbit(pose.translation(8)[0]),"public subnormal conversion or small absolute setter changed");
        auto before=pose.evaluated_nodes().at(8).model;
        auto invalid=general;invalid[7]=std::numeric_limits<float>::quiet_NaN();
        rejects([&] { pose.set_general_matrix(8,invalid); },"nonfinite generic matrix accepted");
        check(pose.evaluated_nodes().at(8).model==before,"failed generic assignment changed pose");
        const auto prior_translation=pose.translation(8);
        rejects([&] { pose.translate(8,{{1,2,std::numeric_limits<float>::infinity()}}); },"nonfinite Translate accepted");
        check(pose.translation(8)==prior_translation,"failed Translate partly changed components");
        pose.set_translation(8,{{1,2,std::numeric_limits<float>::max()}});
        const auto huge=pose.translation(8);
        rejects([&] { pose.translate(8,{{1,2,std::numeric_limits<float>::max()}}); },"overflowing Translate accepted");
        check(pose.translation(8)==huge,"overflowing Translate partially mutated components");
        rejects([&] { pose.detach(999); },"unknown detach target accepted");
        rejects([&] { pose.set_rendering_enabled(999,true); },"unknown enable target accepted");
        rejects([&] { pose.set_translation(999,{{1,2,3}}); },"unknown absolute translation target accepted");
        rejects([&] { pose.set_general_matrix(999,general); },"unknown general target accepted");
        std::cout<<checks<<" native M3G runtime-state contracts passed\n";return 0;
    }catch(const std::exception& error) { std::cerr<<"FAIL: "<<error.what()<<'\n';return 1; }
}
