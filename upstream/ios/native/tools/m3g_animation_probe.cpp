#include "g1/M3GAnimation.hpp"
#include "../tests/m3g_animation_fixture.hpp"

#include <iomanip>
#include <iostream>
#include <set>

namespace {
template<class T> void array(const T& values) {
    std::cout<<'[';bool first=true;
    for(const auto value:values) { if(!first)std::cout<<',';first=false;std::cout<<value; }
    std::cout<<']';
}
}
int main(int argc,char** argv) {
    try {
        if(argc!=3)throw std::runtime_error("usage: m3g_animation_probe MODEL.m3g ROOT_USER_ID");
        const auto bytes=g1::read_file(argv[1]);const g1::M3GScene scene(bytes);const g1::M3GAnimations animations(bytes);
        const auto root=scene.find_user_id(static_cast<std::uint32_t>(std::stoul(argv[2])));
        if(root.size()!=1)throw std::runtime_error("requested rigid root is absent or ambiguous");
        std::cout<<std::setprecision(9)<<"{\"sequences\":"<<animations.sequences().size()<<",\"tracks\":"<<animations.tracks().size()
                 <<",\"controllers\":"<<animations.controllers().size()<<",\"records\":[";
        bool first=true;
        for(const auto& entry:animations.sequences()) {
            const auto& sequence=entry.second;
            std::set<std::int32_t> times{-1,0,1,sequence.duration-1,sequence.duration,sequence.duration+1};
            for(std::size_t i=0;i<sequence.keyframes.size();++i) {
                times.insert(sequence.keyframes[i].time);
                if(i)times.insert((sequence.keyframes[i-1].time+sequence.keyframes[i].time)/2);
            }
            for(const auto time:times) {
                if(!first)std::cout<<',';
                first=false;
                std::cout<<"{\"sequence\":"<<entry.first<<",\"world_time\":"<<time<<",\"value\":";
                array(g1::m3g_sample_sequence(sequence,static_cast<float>(time)));std::cout<<'}';
            }
        }
        std::cout<<"],\"poses\":[";first=true;
        g1::M3GRigidPose pose(scene);auto model=scene.static_model(root.front());
        for(const auto time:{0,1,166,500,1000,11666}) {
            const auto samples=animations.sample(time);pose.apply(samples);pose.update_model(model);
            if(!first)std::cout<<',';
            first=false;
            std::cout<<"{\"world_time\":"<<time<<",\"properties\":"<<samples.size()<<",\"minimum\":";array(model.minimum);
            std::cout<<",\"maximum\":";array(model.maximum);std::cout<<",\"matrices\":[";
            bool first_matrix=true;
            for(const auto& entry:pose.world_matrices()) {
                if(!first_matrix)std::cout<<',';
                first_matrix=false;
                std::cout<<"{\"object\":"<<entry.first<<",\"matrix\":";array(entry.second);std::cout<<'}';
            }
            std::cout<<"]}";
        }
        std::cout<<"],\"controller_cases\":[";first=true;
        const std::vector<std::array<double,5>> controller_cases{{{0,1,0,0,0}},{{1,1,0,0,0}},{{100,0,12,20,0}},{{24,-.5,12,20,0}},
            {{100000001,1,0,0,100}},{{100000001,2,3,0,100}},{{100000001,.25,-2,19,100}},
            {{100000001,-.5,0,23,100}},{{-100000001,1,5,0,100}},{{-100000001,-2.5,100,-999,100}},
            {{1234,1.25,4.5,200,0}},{{0,1,0,100000001,100}}};
        for(const auto& test:controller_cases) {
            g1::M3GController controller;controller.speed=static_cast<float>(test[1]);
            controller.reference_sequence_time=static_cast<float>(test[2]);controller.reference_world_time=static_cast<std::int32_t>(test[3]);
            if(!first)std::cout<<',';
            first=false;
            std::cout<<"{\"world\":"<<test[0]<<",\"speed\":"<<test[1]<<",\"reference_sequence\":"<<test[2]
                     <<",\"reference_world\":"<<test[3]<<",\"loop_duration\":"<<test[4]<<",\"position\":"
                     <<g1::m3g_controller_position(controller,static_cast<std::int32_t>(test[0]),static_cast<std::int32_t>(test[4]))<<'}';
        }
        std::cout<<"],\"weighted_cases\":[";first=true;
        for(const std::uint32_t property:{275U,268U})for(const auto& weights:std::vector<std::array<float,2>>{{{.25F,.75F}},{{.2F,.6F}},{{2,.25F}}}) {
            const g1::M3GAnimations blend(g1_test::weighted_animation(property,weights[0],weights[1]));
            for(const auto time:{0,25,50,99}) {
                if(!first)std::cout<<',';
                first=false;
                std::cout<<"{\"property\":"<<property<<",\"world_time\":"<<time<<",\"weights\":";array(weights);
                std::cout<<",\"value\":";array(blend.sample(time).at(0).value);std::cout<<'}';
            }
        }
        std::cout<<"],\"gpu_execution\":false,\"battle_clip_selection\":false}\n";return 0;
    } catch(const std::exception& error) { std::cerr<<"ERROR: "<<error.what()<<'\n';return 1; }
}
