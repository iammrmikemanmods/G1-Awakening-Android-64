#include "g1/TerrainDecorations.hpp"
#include "g1/Combat.hpp"

#include <fstream>
#include <iostream>
#include <stdexcept>
#include <string>

namespace {
std::size_t checks{};
void require(bool value,const char* message){++checks;if(!value)throw std::runtime_error(message);}
void tag(std::istream& input,const char* expected){
    std::string value;input>>value;
    require(value==expected,"decoration fixture tag differs");
}
std::int64_t number(std::istream& input){
    std::int64_t value{};if(!(input>>value))throw std::runtime_error("short decoration fixture");return value;
}
g1::Bytes bytes(std::istream& input){
    const auto n=number(input);
    if(n<0||n>65535)throw std::runtime_error("fixture byte count out of range");
    g1::Bytes out;out.reserve(static_cast<std::size_t>(n));
    for(std::int64_t i=0;i<n;++i){
        const auto value=number(input);
        if(value<0||value>255)throw std::runtime_error("fixture byte out of range");
        out.push_back(static_cast<std::uint8_t>(value));
    }
    return out;
}
bool same(const g1::GameRandom::State& a,const g1::GameRandom::State& b){
    return a.index==b.index&&a.draws==b.draws&&a.words==b.words;
}
bool same(const g1::TerrainDecorationState& a,const g1::TerrainDecorationState& b){
    if(a.animation.accumulated_ms!=b.animation.accumulated_ms||a.animation.phase!=b.animation.phase||
       a.candidate_records!=b.candidate_records||a.slots.size()!=b.slots.size())return false;
    for(std::size_t i=0;i<a.slots.size();++i)
        if(a.slots[i].phase!=b.slots[i].phase||
           a.slots[i].candidate_byte_offset!=b.slots[i].candidate_byte_offset)return false;
    return true;
}
template<class F>void rejects(F&& function){
    bool rejected=false;try{function();}catch(const g1::FormatError&){rejected=true;}
    require(rejected,"invalid decoration state accepted");
}
void compare(std::istream& input,const g1::TerrainDecorationLayout& layout,
    const g1::TerrainDecorationState& state,const g1::TerrainGrid& grid,
    const g1::TerrainTables& tables,const g1::GameRandom& random){
    require(state.animation.accumulated_ms==number(input),"original clock accumulator differs");
    require(state.animation.phase==number(input),"original clock phase differs");
    require(random.state().index==number(input),"original random index differs");
    require(random.state().draws==static_cast<std::uint64_t>(number(input)),"original random draw count differs");
    require(state.candidate_records==bytes(input),"original candidate bytes differ");
    require(state.slots.size()==static_cast<std::size_t>(number(input)),"original slot count differs");
    const auto quads=g1::terrain_decoration_quads(layout,state,grid,tables);
    for(std::size_t i=0;i<state.slots.size();++i){
        require(state.slots[i].phase==number(input),"original slot phase differs");
        require(state.slots[i].candidate_byte_offset==number(input),"original selection offset differs");
        for(const auto& v:quads[i].positions)
            for(const auto value:v)require(value==number(input),"original decoration position differs");
        for(const auto& uv:quads[i].quantized_uv)
            for(const auto value:uv)require(value==number(input),"original decoration UV differs");
    }
}
}

int main(int argc,char** argv){
    try{
        if(argc!=2)throw std::runtime_error("pass terrain_decorations_cases.txt");
        std::ifstream input(argv[1]);
        tag(input,"G1_TERRAIN_DECORATIONS_ORACLE_V1");
        const auto cases=number(input);
        require(cases==24,"decoration fixture is incomplete");
        std::size_t snapshots=0;
        for(std::int64_t c=0;c<cases;++c){
            tag(input,"CASE");std::string name;input>>name;
            g1::GameRandom random(static_cast<std::uint32_t>(number(input)));
            tag(input,"DATA");const auto grid=g1::decode_terrain_grid(bytes(input));
            tag(input,"DATA");const auto tables=g1::decode_terrain_tables(bytes(input));
            tag(input,"DATA");const auto config=g1::decode_terrain_animation_config(bytes(input));
            const auto layout=g1::build_terrain_decoration_layout(grid,tables,config);
            tag(input,"LAYOUT");
            require(layout.initial_candidate_records.size()/3==static_cast<std::size_t>(number(input)),"original candidate capacity differs");
            require(layout.registered_candidates==number(input),"original registered candidate count differs");
            require(layout.mesh_quad_capacity==number(input),"original initial strip capacity differs");
            const auto slots=number(input);
            require(layout.initial_candidate_records==bytes(input),"original initial registration bytes differ");
            auto state=g1::create_terrain_decorations(layout,random);
            require(state.slots.size()==static_cast<std::size_t>(slots),"original initialized slots differ");
            tag(input,"STATES");const auto count=number(input);
            require(count==17,"original snapshot fixture incomplete");
            g1::TerrainDecorationState restored;
            g1::GameRandom restored_random(0xdeadbeefU);
            for(std::int64_t i=0;i<count;++i){
                tag(input,"STATE");const auto delta=static_cast<std::uint32_t>(number(input));
                if(i){
                    g1::advance_terrain_decorations(layout,state,delta,random);
                    if(i>count/2)g1::advance_terrain_decorations(layout,restored,delta,restored_random);
                }
                compare(input,layout,state,grid,tables,random);
                ++snapshots;
                if(i==count/2){
                    restored=state;
                    restored_random.restore(random.state());
                    g1::validate_terrain_decorations(layout,restored);
                }else if(i>count/2){
                    require(same(state,restored),"restored decoration continuation differs");
                    require(same(random.state(),restored_random.state()),"restored shared random continuation differs");
                }
            }
            tag(input,"RNG");
            for(const auto word:random.state().words)
                require(word==static_cast<std::uint32_t>(number(input)),"original624-word random state differs");
            tag(input,"END");
        }
        std::string extra;require(!(input>>extra),"trailing decoration fixture data");

        g1::TerrainTables tables{};
        g1::TerrainAnimationConfig config;config.tables[0].resize(3);config.tables[0][1]={1};
        const auto grid=g1::decode_terrain_grid({1,4,1,1,1,1});
        const auto layout=g1::build_terrain_decoration_layout(grid,tables,config);
        g1::GameRandom random(1);
        auto state=g1::create_terrain_decorations(layout,random);
        auto invalid=state;invalid.candidate_records[1]=127;
        rejects([&]{g1::validate_terrain_decorations(layout,invalid);});
        invalid=state;invalid.slots[0].candidate_byte_offset=-2;
        rejects([&]{g1::validate_terrain_decorations(layout,invalid);});
        invalid=state;invalid.slots[0].phase=4;
        rejects([&]{g1::advance_terrain_decorations(layout,invalid,250,random);});
        invalid=state;invalid.animation.accumulated_ms=0x7fffffffU;
        const auto prior=invalid;const auto rng_before=random.state();
        rejects([&]{g1::advance_terrain_decorations(layout,invalid,1,random);});
        require(same(invalid,prior)&&same(random.state(),rng_before),"time rejection changed state or shared random stream");
        auto invalid_layout=layout;invalid_layout.width=128;
        rejects([&]{g1::create_terrain_decorations(invalid_layout,random);});
        invalid_layout=layout;invalid_layout.initial_candidate_records.pop_back();
        rejects([&]{g1::create_terrain_decorations(invalid_layout,random);});
        auto bad_world=grid;bad_world.width=2;
        rejects([&]{g1::terrain_decoration_quads(layout,state,bad_world,tables);});

        // Valid explicit generator state that tempers its next word toINT_MIN.
        // With seven candidates, original abs/remainder addresses before storage.
        auto raw=random.state();raw.index=0;raw.words[0]=0x80102204U;
        random.restore(raw);const auto before=random.state();
        auto check_word=random;require(check_word.next_word()==0x80000000U,"boundary generator input is wrong");
        rejects([&]{g1::create_terrain_decorations(layout,random);});
        require(same(random.state(),before),"unsupported original access consumed the shared draw");
        // INT_MIN has zero remainder for eight candidates; that path is valid.
        const auto eight_grid=g1::decode_terrain_grid({1,5,1,1,1,1,1});
        const auto eight=g1::build_terrain_decoration_layout(eight_grid,tables,config);
        const auto accepted=g1::create_terrain_decorations(eight,random);
        require(accepted.slots[0].candidate_byte_offset==0&&random.state().draws==before.draws+1,
                "valid zero-remainder boundary selection differs");
        std::cout<<"terrain decorations: "<<cases<<" original cases, "<<snapshots
                 <<" snapshots, "<<checks<<" comparisons/contracts passed\n";
    }catch(const std::exception& e){std::cerr<<e.what()<<'\n';return 1;}
}
