#include "g1/M3GScenePose.hpp"
#include "m3g_property_fixture.hpp"
#include "m3g_animation_fixture.hpp"

#include <cmath>
#include <cstring>
#include <functional>
#include <iostream>
#include <limits>

namespace {
using g1::Bytes;
int checks=0,parity=0,accepted_direct=0,accepted_dispatch=0,rejection_vectors=0;
void check(bool condition,const char* message) {
    ++checks;if(!condition)throw std::runtime_error(message);
}
void equal(std::uint32_t value,std::uint32_t expected,const char* message) {
    ++parity;check(value==expected,message);
}
void rejects(const std::function<void()>& action,const char* message) {
    bool rejected=false;try { action(); }catch(const g1::FormatError&) { rejected=true; }
    check(rejected,message);
}
float from_bits(std::uint32_t bits) { float value;std::memcpy(&value,&bits,4);return value; }
std::uint32_t bits(float value) { std::uint32_t result;std::memcpy(&result,&value,4);return result; }
struct Fixture { Bytes bytes;std::size_t target; };
Fixture fixture(std::uint8_t mode,bool visible,
                const std::vector<g1_property_test::Track>& tracks={}) {
    using namespace g1_test;
    Bytes raw;std::vector<std::uint32_t> bound;std::uint32_t next=2;
    for(const auto& input:tracks) {
        auto sequence=base();append(sequence,{180,192,0});
        u32(sequence,100);u32(sequence,0);u32(sequence,0);u32(sequence,1);u32(sequence,1);
        u32(sequence,0);u32(sequence,input.value);
        const auto sequence_index=next++;append(raw,chunk(19,sequence));
        auto controller=base();scalar(controller,1);u32(controller,input.weight);
        u32(controller,static_cast<std::uint32_t>(input.start));u32(controller,static_cast<std::uint32_t>(input.end));
        scalar(controller,0);u32(controller,0);
        const auto controller_index=next++;append(raw,chunk(1,controller));
        auto track=base();u32(track,sequence_index);u32(track,input.null_controller ? 0 : controller_index);
        u32(track,input.property);bound.push_back(next++);append(raw,chunk(2,track));
    }
    auto body=base(bound);append(body,{0,0,static_cast<std::uint8_t>(visible),1,255});
    u32(body,0xffffffffU);body.push_back(0);
    if(!mode)u32(body,0);
    else {
        body.push_back(mode);
        if(mode==48)for(int row=0;row<4;++row)for(int column=0;column<4;++column)
            scalar(body,row==column ? 1.0F : 0.0F);
        else {
            scalar(body,mode==49 ? 2.0F : 45.0F);scalar(body,1.5F);scalar(body,1.0F);scalar(body,100.0F);
        }
    }
    const auto target=next++;append(raw,chunk(mode ? 5 : 9,body));
    auto world=node();u32(world,1);u32(world,target);u32(world,mode ? target : 0);u32(world,0);
    append(raw,chunk(22,world));return {file(raw),target};
}
g1::M3GScenePose initial(const Fixture& data,std::uint8_t mode) {
    g1::M3GScenePose pose(data.bytes);
    pose.apply({{data.target,256,{0.75F}}});
    if(mode==48)pose.apply({{data.target,264,{45.0F}}});
    return pose;
}
void compare(const g1::M3GScenePose& pose,std::size_t target,std::uint8_t mode,
             std::uint32_t alpha,bool enabled,std::uint32_t field) {
    const auto& node=pose.local_nodes().at(target);
    equal(bits(node.alpha),alpha,"original alpha assignment mismatch");
    equal(node.rendering_enabled ? 1U : 0U,enabled ? 1U : 0U,"original visibility assignment mismatch");
    if(mode) {
        const auto& camera=pose.cameras().at(target);
        equal(camera.type,mode,"original camera type changed");
        equal(bits(camera.parameters[0]),field,"original camera field assignment mismatch");
        if(mode!=48) {
            equal(bits(camera.parameters[1]),0x3fc00000U,"camera aspect changed");
            equal(bits(camera.parameters[2]),0x3f800000U,"camera near changed");
            equal(bits(camera.parameters[3]),0x42c80000U,"camera far changed");
        } else {
            const auto matrix=g1::m3g_camera_projection_matrix(camera);
            for(std::size_t i=0;i<16;++i)
                check(matrix[i]==(i%5==0 ? 1.0F : 0.0F),"generic camera matrix changed by field assignment");
        }
    }
}
} // namespace

int main() {
    try {
        for(const auto& reference:g1_property_test::direct) {
            const auto data=fixture(reference.mode,reference.visible);
            auto pose=initial(data,reference.mode);
            if(!reference.finite) {
                rejects([&] { pose.apply({{data.target,reference.property,{from_bits(reference.value)}}}); },
                        "nonfinite original diagnostic accepted by native pose");
                ++rejection_vectors;continue;
            }
            pose.apply({{data.target,reference.property,{from_bits(reference.value)}}});
            compare(pose,data.target,reference.mode,reference.alpha,reference.enabled,reference.field);
            ++accepted_direct;
        }
        for(const auto& reference:g1_property_test::dispatch) {
            const auto data=fixture(reference.mode,reference.visible,reference.tracks);
            if(!reference.accepted) {
                rejects([&] { (void)g1::M3GAnimations(data.bytes); },
                        "invalid original controller diagnostic accepted by native decoder");
                ++rejection_vectors;continue;
            }
            auto pose=initial(data,reference.mode);
            const auto samples=g1::M3GAnimations(data.bytes).sample(reference.time);
            equal(static_cast<std::uint32_t>(samples.size()),
                  static_cast<std::uint32_t>(reference.samples.size()),"original contributing property count mismatch");
            for(std::size_t i=0;i<samples.size();++i) {
                equal(static_cast<std::uint32_t>(samples[i].target),static_cast<std::uint32_t>(data.target),
                      "original scalar dispatch target mismatch");
                equal(samples[i].property,reference.samples[i].property,"original dispatch property order mismatch");
                equal(static_cast<std::uint32_t>(samples[i].value.size()),1,"scalar dispatch width mismatch");
                equal(bits(samples[i].value[0]),reference.samples[i].value,"original weighted accumulation mismatch");
            }
            pose.apply(samples);
            compare(pose,data.target,reference.mode,reference.alpha,reference.enabled,reference.field);
            ++accepted_dispatch;
        }
        const auto data=fixture(50,true);auto pose=initial(data,50);
        const auto before=pose.evaluated_nodes().at(data.target);
        rejects([&] { pose.apply({{data.target,275,{1,2,3}},{data.target,256,{0}},
                                 {999,276,{0}}}); },"unknown scalar target accepted");
        check(pose.evaluated_nodes().at(data.target).model==before.model &&
              pose.local_nodes().at(data.target).alpha==0.75F,"invalid update partly changed pose");
        rejects([&] { pose.apply({{data.target,256,{0}},
                                 {data.target,264,{std::numeric_limits<float>::infinity()}}}); },
                "mixed nonfinite update accepted");
        check(pose.local_nodes().at(data.target).alpha==0.75F,"nonfinite update partly changed alpha");
        rejects([&] { pose.apply({{data.target,276,{0,1}}}); },"scalar component mismatch accepted");
        rejects([&] { pose.apply({{data.target,257,{1}}}); },"unsupported material property accepted");
        const auto group=fixture(0,true);auto group_pose=initial(group,0);
        rejects([&] { group_pose.apply({{group.target,264,{40}}}); },"non-camera field-of-view accepted");
        rejects([&] { (void)g1::M3GScenePose(g1::M3GScene(data.bytes),{}); },"missing decoded camera accepted");
        auto camera_map=g1::decode_m3g_cameras(data.bytes);camera_map.at(data.target).object=999;
        rejects([&] { (void)g1::M3GScenePose(g1::M3GScene(data.bytes),camera_map); },"different camera object accepted");

        const g1::M3GScene chain(g1_test::deep_hierarchy(20000));
        g1::M3GScenePose inherited(chain,{});
        inherited.apply({{20002,256,{0.5F}},{20001,256,{0.25F}},{2,256,{0.5F}}});
        auto evaluated=inherited.evaluated_nodes();
        check(evaluated.size()==20001 && evaluated.at(2).effective_alpha==0.0625F,
              "deep iterative alpha inheritance failed");
        inherited.apply({{20001,276,{0}}});evaluated=inherited.evaluated_nodes();
        check(!evaluated.at(2).rendering_enabled && inherited.local_nodes().at(2).rendering_enabled,
              "parent visibility did not hide descendant locally enabled node");
        inherited.apply({{20001,276,{1}}});
        check(inherited.evaluated_nodes().at(2).rendering_enabled,"restoring parent enable did not reveal child");
        Bytes scoped_raw;
        auto scoped_node=[](std::uint32_t scope) {
            auto out=g1_test::base();g1_test::append(out,{0,0,1,1,255});
            g1_test::u32(out,scope);out.push_back(0);return out;
        };
        auto child=scoped_node(2);g1_test::u32(child,0);
        g1_test::append(scoped_raw,g1_test::chunk(9,child));
        auto parent=scoped_node(0);g1_test::u32(parent,1);g1_test::u32(parent,2);
        g1_test::u32(parent,0);g1_test::u32(parent,0);
        g1_test::append(scoped_raw,g1_test::chunk(22,parent));
        const g1::M3GScenePose scoped(g1_test::file(scoped_raw));
        check(scoped.evaluated_nodes().at(2).scope==2,
              "ancestor scope was incorrectly intersected with drawable scope");
        std::cout<<"{\"checks\":"<<checks<<",\"original_field_comparisons\":"<<parity
                 <<",\"accepted_direct\":"<<accepted_direct<<",\"accepted_dispatch\":"<<accepted_dispatch
                 <<",\"native_rejections_of_original_diagnostics\":"<<rejection_vectors<<"}\n";
        return 0;
    }catch(const std::exception& error) { std::cerr<<"FAIL: "<<error.what()<<'\n';return 1; }
}
