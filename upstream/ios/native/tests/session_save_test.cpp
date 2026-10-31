#include "g1/SessionSave.hpp"

#include <filesystem>
#include <functional>
#include <iostream>
#include <map>
#include <unistd.h>

namespace {
std::size_t checks{};
void check(bool value,const char* message) { ++checks;if(!value)throw std::runtime_error(message); }
void rejects(const std::function<void()>& fn,const char* message) {
    try{fn();}catch(const g1::FormatError&){++checks;return;}throw std::runtime_error(message);
}
void put32(g1::Bytes& bytes,std::size_t at,std::size_t value) {
    for(std::size_t i=0;i<4;++i)bytes.at(at+i)=static_cast<std::uint8_t>(value>>(8U*i));
}
g1::Bytes script(std::initializer_list<std::vector<std::uint16_t>> commands) {
    g1::Bytes bytes{1,static_cast<std::uint8_t>(commands.size())};
    for(const auto& command:commands)for(auto word:command){bytes.push_back(static_cast<std::uint8_t>(word>>8U));bytes.push_back(static_cast<std::uint8_t>(word));}
    return bytes;
}
g1::Archive fixture_archive(bool different=false,bool animated=false,int sprite_style=-1) {
    // Original-format synthetic inputs; no original game payload is embedded.
    std::map<std::uint32_t,g1::Bytes> resources;
    for(std::uint8_t type=0;type<23;++type) {
        const auto cost=static_cast<std::uint8_t>(type==19?99:0);
        g1::Bytes data{0,0,0,0,0,static_cast<std::uint8_t>(type==19?1:0),cost,1,0,0,0,2};
        for(int form=0;form<2;++form) {
            data.push_back(14);const auto at=data.size();data.resize(at+14,0);
            data[at]=4;data[at+1]=1;data[at+2]=1;data[at+3]=type==19?50:25;
            data[at+4]=type==19?60:35;data[at+5]=type==19?50:30;
            data[at+10]=type==19?0:1;
        }
        resources[g1::unit_definition_resource(type)]=std::move(data);
    }
    resources[306]={5,3,1,0,0,3,3,0,0,3,1,0,0,3,99,0,0,3,1,0,0};
    resources[268]=g1::Bytes(66,0);resources[268][0]=8;resources[268][1]=8;
    if(different)resources[268][65]=1;
    resources[282]=g1::Bytes(33,0);resources[282][0]=1;
    resources[280]=resources[282];resources[307]={3,0,1,0,0,0,0,0};
    if(sprite_style>=0) {
        // Two distinct frames and eight sequences cover the style2 selector7
        // branch as well as front/transient clocks; no original artwork.
        g1::Bytes descriptor{2,0,0,0,0,0,0,0,0,0,0,2,0,0,0,0,0,8};
        for(int i=0;i<8;++i)descriptor.insert(descriptor.end(),{2,0,0,37,1,0,53});
        for(const auto id:{638U,825U,581U,1095U,1132U,982U})resources[id]=descriptor;
        resources[281]=resources[282];resources[307]={3,1,13,1,0,1,13,0,0,0};
        for(std::size_t x=0;x<8;++x)resources[268][2+7*8+x]=13;
        resources[268][2]=static_cast<std::uint8_t>(sprite_style==0?8:2);
        resources[268][2+4*8+4]=static_cast<std::uint8_t>(sprite_style==0?1:10);
    }
    const std::vector<std::uint16_t> presentation=(animated || sprite_style>=0) ?
        std::vector<std::uint16_t>{2} : std::vector<std::uint16_t>{4,1,1,1};
    const auto style=static_cast<std::uint16_t>(sprite_style>=0?sprite_style:animated?1:0);
    resources[315]=script({{0},{16,208},{9,0,style},
        {6,11,1,1,0,0},{6,19,3,1,0,0},{6,0,5,5,0,0},
        {7,1,2,1,1,1,1,1},presentation,{12,3,245,11,7,7},{1},{8,202},{10,1}});
    const auto count=resources.size(),members=32U+count*8U,body=members+(count+1U)*8U;
    g1::Bytes bytes(body,0);put32(bytes,0,0x42494746U);put32(bytes,4,0x00800001U);
    put32(bytes,8,32);put32(bytes,12,count);put32(bytes,16,members);put32(bytes,20,count);put32(bytes,24,body);
    std::size_t index{};
    for(const auto& item:resources) {
        const auto at=32U+index*8U;put32(bytes,at,item.first);bytes[at+4]=1;
        bytes[at+6]=static_cast<std::uint8_t>(index);bytes[at+7]=static_cast<std::uint8_t>(index>>8U);
        put32(bytes,members+index*8U,0xf4e02223U);put32(bytes,members+index*8U+4U,bytes.size());
        const auto begin=bytes.size();bytes.resize(begin+4);put32(bytes,begin,4);
        bytes.insert(bytes.end(),item.second.begin(),item.second.end());++index;
    }
    put32(bytes,members+count*8U+4U,bytes.size());put32(bytes,28,bytes.size()-body);return g1::Archive(std::move(bytes));
}
void acknowledge_headless(g1::GameSession& session) {
    // Deliberately synthetic presentation fixture, never evidence of a real
    // drawable, sound, UI or iPhone camera acknowledgement.
    if(session.world_presentation())session.report_world_presented(session.world_presentation()->token);
    if(session.focus_request())session.report_focus_presented(session.focus_request()->token);
    for(std::size_t i=0;i<session.units().size();++i)if(session.unit_transform_waiting(i)) {
        session.clear_transform_animation_request(i);session.report_transform_finished(i,session.unit_transform_generation(i));
    }
    if(session.mission().gate==g1::MissionGate::camera)session.report_camera(session.mission().command_index,true);
}
void reach_goal(g1::GameSession& session) {
    for(int i=0;i<50;++i) {
        acknowledge_headless(session);
        if(session.mission().gate==g1::MissionGate::objective)return;
        (void)session.tick(30);
    }
    throw std::runtime_error("synthetic startup did not reach the actual goal");
}
std::vector<std::int32_t> enemy_turn(g1::GameSession& session) {
    acknowledge_headless(session);check(session.end_turn().has_value(),"saved human state could not end its turn");
    std::vector<std::int32_t> damages;
    for(int i=0;i<2000;++i) {
        acknowledge_headless(session);(void)session.tick(30);
        if(session.battle()) {
            damages.push_back(session.battle()->exchange.attack.rolled_damage);
            if(session.battle()->exchange.counterattack)damages.push_back(session.battle()->exchange.counterattack->rolled_damage);
            session.report_battle_finished(session.battle()->token);
        }
        if(session.current_player()==0 && !session.ai_action()) {
            acknowledge_headless(session);return damages;
        }
    }
    throw std::runtime_error("enemy turn did not return after native actions");
}
void checksum(g1::Bytes& bytes) {
    put32(bytes,bytes.size()-4,g1::adler32_bytes(g1::Bytes(bytes.begin(),bytes.end()-4)));
}
}

int main() {
    std::filesystem::path directory;
    try {
        const auto archive=fixture_archive();g1::GameRandom random(0x4741U);g1::CampaignContext campaign;
        auto source=std::make_unique<g1::GameSession>(archive,g1::bootstrap_campaign(archive,campaign,random),campaign);
        check(!source->checkpoint_available(),"unpresented startup was checkpointable");
        rejects([&]{(void)source->make_checkpoint();},"unpresented startup exported a checkpoint");
        reach_goal(*source);check(source->checkpoint_available(),"stable actual goal did not allow a checkpoint");
        check(source->select(0) && source->capture_selected().status==g1::CaptureStatus::captured,
              "fixture could not perform a real ownership-changing action");
        check(source->select(2) && source->upgrade_selected().has_value(),"fixture could not perform a real upgrade");
        (void)source->tick(30);
        const auto exported=source->make_checkpoint();const auto bytes=g1::encode_session_checkpoint(exported);
        check(exported.captures.sites[0].item_states[0][0]==0 && exported.combat[2].level==1 &&
              exported.movement[0].flag_2c==1 && exported.movement[2].flag_2c==1,"checkpoint omitted committed player actions");
        check(exported.random.draws==source->random().state().draws && exported.mission.command_index==8,
              "checkpoint altered RNG or actual script cursor");
        const auto decoded=g1::decode_session_checkpoint(archive,bytes);
        check(g1::encode_session_checkpoint(decoded)==bytes,"checkpoint codec lost native state");
        auto pattern=(std::filesystem::current_path()/"g1-session-save-XXXXXX").string();
        std::vector<char> name(pattern.begin(),pattern.end());name.push_back(0);const auto made=mkdtemp(name.data());
        if(!made)throw std::runtime_error("isolated checkpoint directory could not be created");
        directory=made;
        const auto path=(directory/"session.g1native").string();
        check(g1::write_native_save_atomic(path,bytes).succeeded,"actual checkpoint write failed");
        check(g1::read_file(path,g1::max_session_save_size)==bytes,"closed checkpoint file differs from committed bytes");
        check(!g1::write_native_save_atomic((directory/"missing"/"session.g1native").string(),bytes).succeeded &&
              g1::read_file(path,g1::max_session_save_size)==bytes,"failed replacement damaged the prior checkpoint");
        const auto expected_damages=enemy_turn(*source);
        check(!expected_damages.empty(),"continuation fixture did not exercise a stochastic battle");
        const auto expected=g1::encode_session_checkpoint(source->make_checkpoint());
        source.reset(); // The continuing game below cannot use old live objects.
        const auto disk=g1::read_file(path,g1::max_session_save_size);
        auto restored=g1::GameSession::restore_checkpoint(archive,g1::decode_session_checkpoint(archive,disk));
        check(restored->world_presentation().has_value() && restored->focus_request().has_value() && !restored->checkpoint_available(),
              "restoration skipped real scene/focus presentation requirements");
        check(restored->sites()[0].item_states[0][0]==0 && restored->combat_units()[2].level==1 &&
              restored->units()[0].flag_2c==1 && restored->turn().funds==exported.turn.funds,
              "cold restore lost ownership, stats, action or funds");
        check(restored->random().state().words==exported.random.words && restored->random().state().draws==exported.random.draws,
              "cold restore drew/reseeded the gameplay random generator");
        check(enemy_turn(*restored)==expected_damages,"restored game changed the subsequent actual battle outcomes");
        check(g1::encode_session_checkpoint(restored->make_checkpoint())==expected,
              "restored continuation differs from uninterrupted gameplay");
        auto timer_fixture=restored->make_checkpoint();
        timer_fixture.movement[0].timer_ms=timer_fixture.combat[0].timer_30_ms=90;
        timer_fixture.movement[2].timer_ms=timer_fixture.combat[2].timer_30_ms=90;
        auto timed=g1::GameSession::restore_checkpoint(archive,timer_fixture);acknowledge_headless(*timed);
        check(timed->select(2) && timed->move_selected_to({5,4})==g1::MoveRequestResult::started,
              "timer fixture could not enter actual movement");
        (void)timed->tick(30);
        check(timed->units()[0].timer_ms==60 && timed->units()[2].timer_ms==60 &&
              timed->combat_units()[0].timer_30_ms==60 && timed->combat_units()[2].timer_30_ms==60,
              "active stationary or moving countdown was omitted or double-ticked");
        for(int i=0;i<3;++i)(void)timed->tick(30);
        check(timed->units()[0].timer_ms==0 && timed->units()[2].timer_ms==0,
              "native countdown did not clamp at zero");
        check(restored->select(2) && restored->move_selected_to({5,4})==g1::MoveRequestResult::started,
              "restored game could not begin a real human move");
        check(!restored->checkpoint_available(),"partly completed movement allowed a checkpoint");
        rejects([&]{(void)restored->make_checkpoint();},"moving checkpoint fabricated arrival");
        check(g1::read_file(path,g1::max_session_save_size)==bytes,"busy action replaced the previous durable checkpoint");
        for(int i=0;i<80 && restored->moving();++i)(void)restored->tick(30);
        check(!restored->moving() && restored->checkpoint_available(),"completed movement did not become saveable");
        check(restored->world().units[2].y==4 && restored->units()[2].spent_budget>0,"restored move did not apply position and cost");
        const auto after_move=g1::encode_session_checkpoint(restored->make_checkpoint());
        check(after_move!=bytes && g1::write_native_save_atomic(path,after_move).succeeded,"completed action could not replace checkpoint");
        rejects([&]{(void)g1::decode_session_checkpoint(fixture_archive(true),bytes);},"different original game data accepted checkpoint");
        for(const auto length:{std::size_t(0),std::size_t(31),bytes.size()-1}) {
            const g1::Bytes truncated(bytes.begin(),bytes.begin()+static_cast<std::ptrdiff_t>(length));
            rejects([&]{(void)g1::decode_session_checkpoint(archive,truncated);},"truncated checkpoint accepted");
        }
        auto corrupt=bytes;corrupt[50]^=1;
        rejects([&]{(void)g1::decode_session_checkpoint(archive,corrupt);},"payload corruption accepted");
        corrupt=bytes;put32(corrupt,8,99);checksum(corrupt);
        rejects([&]{(void)g1::decode_session_checkpoint(archive,corrupt);},"unsupported checkpoint version accepted");
        auto invalid=decoded;invalid.combat[0].x=7;
        rejects([&]{(void)g1::encode_session_checkpoint(invalid);},"inconsistent unit mirrors accepted");
        invalid=decoded;invalid.mission.command_index=7;
        rejects([&]{(void)g1::GameSession::restore_checkpoint(archive,invalid);},"saved camera index bypassed gameplay boundary");
        invalid=decoded;invalid.random.index=625;
        rejects([&]{(void)g1::encode_session_checkpoint(invalid);},"invalid RNG cursor accepted");
        invalid=decoded;invalid.captures.item_links.clear();
        rejects([&]{(void)g1::GameSession::restore_checkpoint(archive,invalid);},"corrupt capture-network shape accepted");

        // A real native session consumes terrain's original random stream as
        // well as combat draws. Restore must retain both, without recreation.
        const auto animated_archive=fixture_archive(false,true);g1::TerrainTables tables{};
        auto animated_boot=g1::bootstrap_campaign(animated_archive,campaign,random,false,&tables);
        check(animated_boot.terrain && animated_boot.terrain->state &&
              !animated_boot.terrain->state->slots.empty() &&
              animated_boot.random_after.draws==animated_boot.terrain->state->slots.size(),
              "style1 bootstrap did not commit the initial pool draws with its world");
        auto animated=std::make_unique<g1::GameSession>(animated_archive,std::move(animated_boot),campaign);
        reach_goal(*animated);
        const auto initial_draws=animated->random().state().draws;
        for(int i=0;i<20;++i)(void)animated->tick(30);
        check(animated->terrain_generation()>1 && animated->random().state().draws>initial_draws,
              "session simulation did not advance the shared terrain clock/random stream");
        const auto terrain_saved=animated->make_checkpoint();
        const auto terrain_bytes=g1::encode_session_checkpoint(terrain_saved);
        rejects([&]{(void)g1::decode_session_checkpoint(animated_archive,terrain_bytes);},
                "terrain checkpoint accepted missing immutable table data");
        auto wrong_tables=tables;wrong_tables[1][0].height_steps=1;
        rejects([&]{(void)g1::decode_session_checkpoint(animated_archive,terrain_bytes,&wrong_tables);},
                "terrain checkpoint accepted different immutable tables");
        const auto terrain_decoded=g1::decode_session_checkpoint(animated_archive,terrain_bytes,&tables);
        check(g1::encode_session_checkpoint(terrain_decoded)==terrain_bytes,
              "terrain checkpoint codec lost the exact shared state");
        check(g1::write_native_save_atomic(path,terrain_bytes).succeeded,"animated checkpoint actual write failed");
        const auto animated_damages=enemy_turn(*animated);
        const auto animated_expected=g1::encode_session_checkpoint(animated->make_checkpoint());
        animated.reset();
        auto animated_restore=g1::GameSession::restore_checkpoint(animated_archive,
            g1::decode_session_checkpoint(animated_archive,g1::read_file(path,g1::max_session_save_size),&tables));
        check(animated_restore->random().state().draws==terrain_saved.random.draws &&
              animated_restore->terrain_state()->candidate_records==terrain_saved.terrain->state->candidate_records,
              "animated checkpoint restore recreated or reseeded its pool");
        check(enemy_turn(*animated_restore)==animated_damages &&
              g1::encode_session_checkpoint(animated_restore->make_checkpoint())==animated_expected,
              "terrain plus battle continuation changed after a cold actual file reload");
        invalid=terrain_decoded;invalid.terrain->state->slots[0].phase=4;
        rejects([&]{(void)g1::GameSession::restore_checkpoint(animated_archive,invalid);},
                "invalid saved terrain phase was installed");
        for(const int style:{0,2}) {
            const auto sprite_archive=fixture_archive(false,false,style);
            auto boot=g1::bootstrap_campaign(sprite_archive,campaign,random,false,&tables);
            check(boot.terrain && !boot.terrain->state && boot.terrain->sprite_state &&
                  !boot.terrain->sprite_state->slot_phases.empty() &&
                  boot.random_after.draws>boot.terrain->sprite_state->slot_phases.size(),
                  "style0/2 bootstrap omitted the original two terrain initialization stages");
            auto sprites=std::make_unique<g1::GameSession>(sprite_archive,std::move(boot),campaign);
            reach_goal(*sprites);
            const auto draws_before=sprites->random().state().draws;
            for(int i=0;i<71;++i)(void)sprites->tick(30);
            check(sprites->terrain_sprite_layout() && sprites->terrain_sprite_animations() &&
                  sprites->terrain_generation()>1 && sprites->random().state().draws>draws_before,
                  "style0/2 terrain did not use the live session clock and shared RNG");
            const auto saved=sprites->make_checkpoint();const auto encoded=g1::encode_session_checkpoint(saved);
            const auto decoded_sprites=g1::decode_session_checkpoint(sprite_archive,encoded,&tables);
            check(g1::encode_session_checkpoint(decoded_sprites)==encoded,
                  "sprite checkpoint lost playback, attachment, position or shared RNG bytes");
            check(g1::write_native_save_atomic(path,encoded).succeeded,"sprite checkpoint actual write failed");
            const auto damages=enemy_turn(*sprites);
            check(!damages.empty(),"sprite checkpoint continuation did not exercise an actual battle");
            const auto continuation=g1::encode_session_checkpoint(sprites->make_checkpoint());
            sprites.reset();
            auto cold=g1::GameSession::restore_checkpoint(sprite_archive,g1::decode_session_checkpoint(
                sprite_archive,g1::read_file(path,g1::max_session_save_size),&tables));
            check(cold->random().state().words==saved.random.words && cold->random().state().draws==saved.random.draws,
                  "sprite cold restore recreated its pool or consumed random words");
            check(enemy_turn(*cold)==damages && g1::encode_session_checkpoint(cold->make_checkpoint())==continuation,
                  "style0/2 terrain and battle continuation changed after a cold actual file reload");
            auto bad=decoded_sprites;bad.terrain->sprite_state->sprites[0].playback.step_index=99;
            rejects([&]{(void)g1::GameSession::restore_checkpoint(sprite_archive,bad);},
                    "sprite restore accepted an out-of-sequence saved frame");
            bad=decoded_sprites;bad.terrain->sprite_state->sprites[0].parent=static_cast<g1::TerrainSpriteParent>(4);
            rejects([&]{(void)g1::encode_session_checkpoint(bad);},"sprite save accepted a nonexistent parent group");
            bad=decoded_sprites;bad.terrain->sprite_state.reset();
            rejects([&]{(void)g1::GameSession::restore_checkpoint(sprite_archive,bad);},
                    "sprite restore silently rebuilt a missing mutable pool");
        }
        std::filesystem::remove_all(directory);
        std::cout<<checks<<" native session checkpoint, actual file reload and deterministic continuation checks passed; synthetic host, not a playthrough\n";
    } catch(const std::exception& error) {
        if(!directory.empty())std::filesystem::remove_all(directory);
        std::cerr<<error.what()<<'\n';return 1;
    }
}
