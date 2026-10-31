#include "g1/M3GBattlePresentation.hpp"

#include <iostream>
#include <set>

namespace {
g1::M3GBattleRequest request(std::uint8_t first,std::uint8_t second,std::uint8_t style,
    std::uint8_t first_form=0,std::uint8_t second_form=0,std::uint8_t scenario=0) {
    if(scenario>7)throw std::runtime_error("scenario must be0..7");
    const bool death=scenario==1 || scenario==5;
    g1::M3GBattleRequest result;result.token=1;result.terrain_style=style;
    result.attacker_before.type=first;result.attacker_before.form=first_form;
    result.attacker_before.faction=0;result.attacker_before.owner=0;
    result.defender_before.type=second;result.defender_before.form=second_form;
    result.defender_before.faction=1;result.defender_before.owner=1;
    result.exchange.attack.rolled_damage=death ? 100 : 20;
    result.exchange.attack.health_before=100;result.exchange.attack.health_after=death ? 0 : 80;
    if(death)result.exchange.destroyed.push_back({second,1,0,0});
    else if(scenario!=3) {
        result.exchange.counterattack=g1::CombatStrike{};
        result.exchange.counterattack->rolled_damage=15;result.exchange.counterattack->health_before=100;
        result.exchange.counterattack->health_after=85;
    }
    if(scenario==2) {
        result.exchange.counterattack->rolled_damage=100;
        result.exchange.counterattack->health_after=0;
        result.exchange.destroyed.push_back({first,0,0,0});
    }
    if(scenario==4 || scenario==5) {
        result.attacker_before.faction=1; result.defender_before.faction=0;
    }
    if(scenario==6) {
        result.attacker_before.health=75; result.defender_before.health=60;
        result.exchange.attack.health_before=60; result.exchange.attack.health_after=40;
        result.exchange.counterattack->health_before=75; result.exchange.counterattack->health_after=60;
    }
    if(scenario==7) {
        result.defender_before.damage_immune=true;
        result.exchange.attack.immune=true; result.exchange.attack.health_after=100;
    }
    return result;
}
}
int main(int argc,char** argv) {
    try {
        if(argc<2 || argc>8)throw std::runtime_error("archive [typeA typeB style formA formB scenario0..7]");
        const g1::Archive archive(g1::read_file(argv[1]));
        const auto number=[&](int argument,int fallback) {
            const int n=argc>argument ? std::stoi(argv[argument]) : fallback;
            if(n<0 || n>255)throw std::runtime_error("numeric argument outside byte range");
            return static_cast<std::uint8_t>(n);
        };
        const auto input=request(number(2,11),number(3,19),number(4,0),number(5,0),number(6,0),number(7,0));
        g1::M3GBattlePresentation battle(archive,input);
        std::vector<const g1::M3GRenderAsset*> uploaded;std::size_t surfaces=0,sprites=0;
        for(const auto& asset:battle.assets()) {
            uploaded.push_back(&asset.render);surfaces+=asset.render.geometry.surfaces.size();sprites+=asset.render.sprites.size();
        }
        std::set<std::uint8_t> phases;std::size_t updates=0,frames=0,draws=0,visible=0,audio=0;
        auto last=battle.status().phase;
        const auto frame=[&]() {
            const auto output=battle.prepare_frame({1792,828},uploaded,0.5F,1792.0F/828.0F);
            if(output.submissions.size()!=8 || output.token!=input.token)
                throw std::runtime_error("battle output omitted a required original submission");
            for(const auto& pass:output.submissions) {
                if(pass.frame.assets.size()!=1 || pass.frame.assets[0]!=uploaded.at(pass.asset_index))
                    throw std::runtime_error("battle frame changed asset identity");
                draws+=pass.frame.draws.size();
                for(const auto& draw:pass.frame.draws)visible+=draw.draws_geometry();
            }
            ++frames;
        };
        frame();phases.insert(last);audio+=battle.take_audio_requests().size();
        while(!battle.status().complete && updates<2000) {
            battle.advance(30);++updates;const auto status=battle.status();
            audio+=battle.take_audio_requests().size();phases.insert(status.phase);
            if(status.phase!=last || updates%30==0 || status.complete)frame();
            last=status.phase;
        }
        if(!battle.status().complete)throw std::runtime_error("ordinary battle did not reach its real completion state");
        std::cout<<"{\"source\":\"synthetic resolved exchange with original assets; no GPU\",\"assets\":"<<battle.assets().size()
            <<",\"mesh_surfaces\":"<<surfaces<<",\"sprites\":"<<sprites<<",\"updates\":"<<updates
            <<",\"prepared_frames\":"<<frames<<",\"queued_draws\":"<<draws<<",\"nonculled_draws\":"<<visible
            <<",\"audio_requests\":"<<audio<<",\"complete\":true,\"phases\":[";
        bool first=true;for(const auto p:phases){if(!first)std::cout<<',';first=false;std::cout<<unsigned(p);}
        std::cout<<"]}\n";return 0;
    }catch(const std::exception& error) { std::cerr<<"FAIL: "<<error.what()<<'\n';return 1; }
}
