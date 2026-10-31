#include "g1/M3GAnimation.hpp"
#include <cmath>
#include <iostream>
#include <limits>
#include <map>
#include <set>

int main(int argc,char** argv) {
    try {
        if(argc!=2)throw std::runtime_error("usage: m3g_curve_probe MODEL.m3g");
        const auto bytes=g1::read_file(argv[1]);const g1::M3GAnimations animations(bytes);
        std::size_t samples=0,scalars=0;std::map<unsigned,std::size_t> modes;
        for(const auto& entry:animations.sequences()) {
            const auto& sequence=entry.second;const g1::M3GSequenceSampler sampler(sequence);
            ++modes[sequence.interpolation];
            std::set<std::int32_t> times{-1,0,1,sequence.duration-1,sequence.duration};
            if(sequence.duration<std::numeric_limits<std::int32_t>::max())times.insert(sequence.duration+1);
            for(std::size_t i=0;i<sequence.keyframes.size();++i) {
                times.insert(sequence.keyframes[i].time);
                if(i)times.insert(static_cast<std::int32_t>((std::int64_t(sequence.keyframes[i-1].time)+sequence.keyframes[i].time)/2));
            }
            for(const auto time:times) {
                const auto value=sampler.sample(static_cast<float>(time));
                if(value.size()!=sequence.components)throw std::runtime_error("sample component count differs");
                for(const auto scalar:value)if(!std::isfinite(scalar))throw std::runtime_error("nonfinite sampled component");
                ++samples;scalars+=value.size();
            }
        }
        std::cout<<"{\"sequences\":"<<animations.sequences().size()<<",\"samples\":"<<samples<<",\"scalars\":"<<scalars<<",\"modes\":{";
        bool first=true;for(const auto& entry:modes) { if(!first)std::cout<<',';first=false;std::cout<<'"'<<entry.first<<"\":"<<entry.second; }
        std::cout<<"},\"passed\":true}\n";return 0;
    }catch(const std::exception& error) { std::cerr<<"ERROR: "<<error.what()<<'\n';return 1; }
}
