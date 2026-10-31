#include "g1/AudioEvents.hpp"

#include <fstream>
#include <iostream>
#include <sstream>

namespace {
std::vector<std::int64_t> run(const std::vector<std::int64_t>& v) {
    if (v.size()!=10) throw std::runtime_error("audio fixture input shape");
    g1::AudioControlState state;
    for (std::size_t i=0;i<24;++i) state.loaded[i]=(v[2]&(std::int64_t(1)<<i))!=0;
    state.last_music_slot=static_cast<std::int32_t>(v[3]);
    state.last_played_slot=static_cast<std::int32_t>(v[4]);
    const auto result=g1::dispatch_audio_request(state,{v[5]!=0,v[6]!=0,v[7]!=0},
        {v[8]!=0,v[9]!=0},{static_cast<std::int32_t>(v[0]),static_cast<std::int32_t>(v[1])});
    std::int64_t mask=0;
    for (std::size_t i=0;i<24;++i) if (state.loaded[i]) mask|=std::int64_t(1)<<i;
    std::vector<std::int64_t> out{mask,state.last_music_slot,state.last_played_slot,
        result.return_value,static_cast<std::int64_t>(result.actions.size())};
    for (auto action:result.actions) {
        out.push_back(static_cast<int>(action.kind));out.push_back(action.slot);
    }
    return out;
}
std::vector<std::int64_t> numbers(const std::string& line) {
    std::istringstream in(line);std::vector<std::int64_t> out;std::int64_t value;
    while (in>>value) out.push_back(value);
    return out;
}
}

int main(int argc,char** argv) {
    try {
        std::ifstream file;
        std::istream* input=&std::cin;
        if (argc==2) {file.open(argv[1]);if(!file)throw std::runtime_error("audio fixture missing");input=&file;}
        else if(argc!=1)throw std::runtime_error("usage: audio_events_test [fixture]");
        std::string line;std::size_t cases=0,checks=0;
        while(std::getline(*input,line)) {
            if(line.empty() || line[0]=='#')continue;
            const auto bar=line.find('|');
            const auto actual=run(numbers(line.substr(0,bar)));
            ++cases;
            if(bar!=std::string::npos) {
                const auto expected=numbers(line.substr(bar+1));
                if(actual!=expected)throw std::runtime_error("audio original mismatch case"+std::to_string(cases));
                checks+=actual.size();
            } else {
                for(std::size_t i=0;i<actual.size();++i)std::cout<<(i?" ":"")<<actual[i];
                std::cout<<'\n';
            }
        }
        if(argc==2) {
            if(cases==0)throw std::runtime_error("empty audio fixture");
            std::cout<<cases<<" original audio dispatch cases / "<<checks<<" scalar comparisons passed\n";
        }
    } catch(const std::exception& error) {std::cerr<<error.what()<<'\n';return 1;}
}
