#include "g1/M3GAnimation.hpp"
#include "m3g_animation_fixture.hpp"

#include <cmath>
#include <cstring>
#include <functional>
#include <iostream>
#include <zlib.h>

namespace {
using g1::Bytes;
int checks=0;
void check(bool value,const char* text) { ++checks;if(!value)throw std::runtime_error(text); }
void near(float a,float b,float tolerance,const char* text) { check(std::abs(a-b)<=tolerance,text); }
void rejects(const std::function<void()>& action,const char* text) {
    bool failed=false;try { action(); }catch(const g1::FormatError&) { failed=true; }check(failed,text);
}
void u32(Bytes& out,std::uint32_t value) { for(unsigned i=0;i<4;++i)out.push_back(static_cast<std::uint8_t>(value>>(i*8U))); }
void scalar(Bytes& out,float value) { std::uint32_t bits;std::memcpy(&bits,&value,4);u32(out,bits); }
void append(Bytes& out,const Bytes& bytes) { out.insert(out.end(),bytes.begin(),bytes.end()); }
Bytes base(std::vector<std::uint32_t> tracks={}) {
    Bytes b;u32(b,0);u32(b,static_cast<std::uint32_t>(tracks.size()));for(const auto t:tracks)u32(b,t);u32(b,0);return b;
}
Bytes node(std::vector<std::uint32_t> tracks={}) {
    auto b=base(std::move(tracks));append(b,{0,0,1,1,255});u32(b,0xffffffffU);b.push_back(0);return b;
}
Bytes chunk(std::uint8_t type,const Bytes& body) {
    Bytes b{type};u32(b,static_cast<std::uint32_t>(body.size()));append(b,body);return b;
}
Bytes section(const Bytes& raw) {
    Bytes b{0};u32(b,static_cast<std::uint32_t>(raw.size()+13));u32(b,static_cast<std::uint32_t>(raw.size()));append(b,raw);
    u32(b,static_cast<std::uint32_t>(adler32(1,b.data(),static_cast<uInt>(b.size()))));return b;
}
Bytes fixture(std::uint8_t encoding=0,std::uint32_t property=275,float weight=1) {
    auto seq=base();append(seq,{176,192,encoding});u32(seq,100);u32(seq,0);u32(seq,1);u32(seq,3);u32(seq,2);
    if(encoding) { for(int i=1;i<=3;++i)scalar(seq,static_cast<float>(i));for(int i=1;i<=3;++i)scalar(seq,static_cast<float>(i*2)); }
    for(unsigned n=0;n<2;++n) {
        u32(seq,n*100);
        for(unsigned c=1;c<=3;++c) {
            if(!encoding)scalar(seq,static_cast<float>(n*c*10));
            else if(encoding==1)seq.push_back(n ? 255 : 0);
            else append(seq,n ? Bytes{255,255} : Bytes{0,0});
        }
    }
    auto controller=base();scalar(controller,1);scalar(controller,weight);u32(controller,0);u32(controller,0);scalar(controller,0);u32(controller,0);
    auto track=base();u32(track,2);u32(track,3);u32(track,property);
    auto group=node({4});u32(group,0);
    auto world=node();u32(world,1);u32(world,5);u32(world,0);u32(world,0);
    Bytes raw;for(const auto& entry:std::vector<std::pair<std::uint8_t,Bytes>>{{19,seq},{1,controller},{2,track},{9,group},{22,world}})
        append(raw,chunk(entry.first,entry.second));
    auto body=section(raw);const auto length=static_cast<std::uint32_t>(12+30+body.size());
    Bytes header{1,0,0};u32(header,length);u32(header,length);header.push_back(0);
    Bytes file{0xab,'J','S','R','1','8','4',0xbb,13,10,26,10};append(file,section(chunk(0,header)));append(file,body);return file;
}
g1::M3GSequence linear() {
    g1::M3GSequence s;s.interpolation=176;s.repeat_mode=192;s.duration=100;s.first=0;s.last=1;s.components=1;
    s.keyframes={g1::M3GKeyframe{0,{0}},g1::M3GKeyframe{100,{100}}};return s;
}
}
int main() {
    try {
        auto s=linear();near(g1::m3g_sample_sequence(s,25)[0],25,0,"linear interior");
        near(g1::m3g_sample_sequence(s,-1)[0],0,0,"constant before first");
        near(g1::m3g_sample_sequence(s,100)[0],99,0,"original duration-minus-one boundary");
        near(g1::m3g_sample_sequence(s,200)[0],99,0,"constant after duration");
        s.repeat_mode=193;near(g1::m3g_sample_sequence(s,100)[0],0,0,"loop at duration");
        near(g1::m3g_sample_sequence(s,-1)[0],99,0,"negative loop position");
        s.keyframes={{10,{0}},{90,{80}}};
        near(g1::m3g_sample_sequence(s,0)[0],40,0,"loop before first interpolates prior cycle");
        near(g1::m3g_sample_sequence(s,95)[0],60,0,"loop after last interpolates next cycle");
        s.interpolation=180;near(g1::m3g_sample_sequence(s,0)[0],80,0,"STEP wrap holds final key");
        s=linear();s.last=2;s.keyframes={{0,{0}},{10,{1}},{10,{2}}};
        near(g1::m3g_sample_sequence(s,10)[0],2,0,"coincident final chooses highest index");
        s.last=3;s.keyframes.push_back({20,{4}});
        near(g1::m3g_sample_sequence(s,10)[0],2,0,"coincident starting segment uses highest index");
        near(g1::m3g_sample_sequence(s,5)[0],0.5F,0,"ending segment uses first coincident key");
        s.first=2;s.last=1;s.keyframes={{50,{5}},{75,{7.5F}},{0,{0}},{25,{2.5F}}};
        near(g1::m3g_sample_sequence(s,37.5F)[0],3.75F,0,"wrapped valid index range");
        auto broken=s;broken.duration=0;rejects([&]{(void)g1::m3g_sample_sequence(broken,0);},"unset duration accepted");
        broken=linear();broken.keyframes.front().time=-1;rejects([&]{(void)g1::m3g_sample_sequence(broken,0);},"negative first public key time accepted");
        broken=s;broken.keyframes[3].time=-1;rejects([&]{(void)g1::m3g_sample_sequence(broken,0);},"decreasing key time accepted");
        broken=s;broken.interpolation=181;rejects([&]{(void)g1::m3g_sample_sequence(broken,0);},"invalid interpolation silently sampled");
        broken=s;broken.keyframes[2].value.clear();rejects([&]{(void)g1::m3g_sample_sequence(broken,0);},"wrong key width accepted");
        s=linear();s.interpolation=177;s.components=4;s.repeat_mode=193;
        s.keyframes={{0,{0,0,0,1}},{100,{0,0,0.70710677F,-0.70710677F}}};
        const auto rotation=g1::m3g_sample_sequence(s,50);
        near(rotation[2],0.92388F,0.0001F,"SLERP must not flip negative-dot quaternion");
        near(rotation[3],0.382683F,0.0001F,"SLERP preserves authored rotation direction");
        g1::M3GController c;
        check(g1::m3g_controller_active(c,-5),"equal controller endpoints should be always active");
        c.active_start=10;c.active_end=20;
        check(!g1::m3g_controller_active(c,9) && g1::m3g_controller_active(c,10) && !g1::m3g_controller_active(c,20),"active interval boundary");
        c.speed=-0.5F;c.reference_world_time=20;c.reference_sequence_time=12;
        near(g1::m3g_controller_position(c,24),10,0,"reverse controller mapping");
        c.speed=0;near(g1::m3g_controller_position(c,100000000,100),12,0,"paused controller does not drift");
        c.speed=1;c.reference_sequence_time=0;c.reference_world_time=0;
        near(g1::m3g_controller_position(c,100000001,100),1,0,"long-running original loop precision reduction");
        const auto bytes=fixture();const g1::M3GScene scene(bytes);const g1::M3GAnimations animation(bytes);
        check(animation.sequences().size()==1 && animation.controllers().size()==1 && animation.tracks().size()==1,"serialized animation references");
        const auto samples=animation.sample(50);check(samples.size()==1 && samples[0].target==5 && samples[0].property==275,"serialized target association");
        check(samples[0].value==std::vector<float>({5,10,15}),"decoded translation sampling");
        g1::M3GRigidPose pose(scene);pose.apply(samples);
        auto matrices=pose.world_matrices();near(matrices.at(5)[12],5,0,"posed column-major translation");
        auto override=animation.controllers().at(3);override.weight=0;
        const auto inactive=animation.sample(75,{{3,override}});check(inactive.empty(),"zero weight changed target");
        pose.apply(inactive);check(pose.world_matrices()==matrices,"inactive pose reset authored state");
        rejects([&]{pose.apply({{5,275,{1,2,3}},{999,275,{1,2,3}}});},"unknown pose target accepted");
        check(pose.world_matrices()==matrices,"rejected pose update partly mutated state");
        override.active_start=100;override.active_end=110;override.weight=1;
        check(animation.sample(50,{{3,override}}).empty(),"inactive interval produced assignment");
        for(const auto encoding:{1,2}) {
            const g1::M3GAnimations encoded(fixture(static_cast<std::uint8_t>(encoding)));
            check(encoded.sample(0)[0].value==std::vector<float>({1,2,3}),"quantized vector bias");
            check(encoded.sample(50)[0].value==std::vector<float>({2,4,6}),"quantized vector scale");
        }
        const g1::M3GAnimations unsupported(fixture(0,257));
        rejects([&]{(void)unsupported.sample(50);},"unsupported active material property silently skipped");
        const g1::M3GAnimations inactive_unsupported(fixture(0,257,0));
        check(inactive_unsupported.sample(50).empty(),"inactive unsupported property should remain untouched");
        const g1::M3GAnimations blend(g1_test::weighted_animation(275,0.25F,0.75F));
        check(blend.sample(50)[0].value==std::vector<float>({11,3.25F,0.75F}),"two independent tracks did not accumulate by weight");
        const g1::M3GAnimations nonunit(g1_test::weighted_animation(275,2,0.25F));
        check(nonunit.sample(50)[0].value==std::vector<float>({13.25F,20.25F,29}),"track weights were normalized or blended with an unstated default");
        const g1::M3GScene deep(g1_test::deep_hierarchy(20000));
        const auto deep_matrices=g1::M3GRigidPose(deep).world_matrices();
        check(deep_matrices.size()==20001 && deep_matrices.at(2)[15]==1,"deep acyclic hierarchy failed iterative evaluation");
        std::cout<<checks<<" native M3G animation contracts passed\n";return 0;
    } catch(const std::exception& error) { std::cerr<<"FAIL: "<<error.what()<<'\n';return 1; }
}
