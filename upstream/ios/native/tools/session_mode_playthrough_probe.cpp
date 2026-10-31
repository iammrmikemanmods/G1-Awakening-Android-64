#include "g1/CampaignController.hpp"
#include "g1/SessionSave.hpp"
#include "SupportPilotValidation.hpp"

#include <algorithm>
#include <chrono>
#include <cmath>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <limits>
#include <memory>
#include <queue>
#include <set>
#include <sstream>
#include <string>
#include <unordered_set>

// Development-only headless input pilot. It never writes native gameplay
// state, unit fields, objectives, controller flags or command cursors. All
// actions use public GameSession commands; the real native AI owns its turns.
// Renderer/dialogue/battle/results feedback is explicitly SYNTHETIC. A native
// campaign handoff here is not evidence of UIKit/Metal/audio/device gameplay.
namespace {
using Clock=std::chrono::steady_clock;
constexpr std::int32_t infinity=100000000;

std::string quote(const std::string& value) {
    std::ostringstream out;out<<'"';
    for (unsigned char c:value) {
        if(c=='"' || c=='\\')out<<'\\'<<c;
        else if(c=='\n')out<<"\\n";
        else if(c=='\r')out<<"\\r";
        else if(c=='\t')out<<"\\t";
        else if(c<32)out<<'?';
        else out<<c;
    }
    out<<'"';return out.str();
}

int signed_byte(std::uint8_t value) {return value<128 ? int(value) : int(value)-256;}
int distance(g1::WorldCoordinate a,g1::WorldCoordinate b) {
    return std::abs(int(a.x)-int(b.x))+std::abs(int(a.y)-int(b.y));
}

struct Options {
    std::string archive,tables,output;
    std::string resume_checkpoint,resume_profile,initial_profile;
    std::uint8_t arena_faction{};
    std::int8_t arena_opponent{1};
    std::int32_t capture_mission{-1},capture_turn{1};
    std::string followup_policy{"baseline"};
    std::string strategy{"direct"};
    std::uint8_t group{};
    std::int32_t start{};
    std::uint32_t seed{0x4741U};
    std::size_t max_missions{17},max_ticks{200000},max_human_turns{500};
    double max_seconds{90};
    bool verify_support{};
};

Options parse(int argc,char** argv) {
    Options result;
    for(int i=1;i<argc;++i) {
        const std::string key=argv[i];
        if(i+1>=argc)throw std::runtime_error("Every probe option needs a value");
        const std::string value=argv[++i];
        if(key=="--archive")result.archive=value;
        else if(key=="--terrain-tables")result.tables=value;
        else if(key=="--output")result.output=value;
        else if(key=="--strategy")result.strategy=value;
        else if(key=="--verify-support") {
            if(value!="0" && value!="1")throw std::runtime_error("--verify-support must be 0 or 1");
            result.verify_support=value=="1";
        }
        else if(key=="--resume-checkpoint")result.resume_checkpoint=value;
        else if(key=="--resume-profile")result.resume_profile=value;
        else if(key=="--initial-profile")result.initial_profile=value;
        else if(key=="--arena-faction") {
            const auto number=std::stoul(value);if(number>1)throw std::runtime_error("Invalid Arena faction");
            result.arena_faction=static_cast<std::uint8_t>(number);
        } else if(key=="--arena-opponent") {
            const auto number=std::stoul(value);if(number>1)throw std::runtime_error("Invalid Arena opponent");
            result.arena_opponent=static_cast<std::int8_t>(number);
        }
        else if(key=="--capture-mission")result.capture_mission=std::stoi(value);
        else if(key=="--capture-turn")result.capture_turn=std::stoi(value);
        else if(key=="--followup-policy")result.followup_policy=value;
        else if(key=="--group") {
            const auto number=std::stoul(value);if(number>2)throw std::runtime_error("Invalid group");
            result.group=static_cast<std::uint8_t>(number);
        } else if(key=="--start-mission")result.start=std::stoi(value);
        else if(key=="--seed") {
            const auto number=std::stoull(value,nullptr,0);
            if(number>std::numeric_limits<std::uint32_t>::max())throw std::runtime_error("Seed exceeds32bits");
            result.seed=static_cast<std::uint32_t>(number);
        } else if(key=="--max-missions")result.max_missions=std::stoull(value);
        else if(key=="--max-ticks")result.max_ticks=std::stoull(value);
        else if(key=="--max-human-turns")result.max_human_turns=std::stoull(value);
        else if(key=="--max-seconds")result.max_seconds=std::stod(value);
        else throw std::runtime_error("Unknown probe option: "+key);
    }
    if(result.archive.empty() || result.tables.empty() || result.output.empty() ||
       result.max_missions==0 || result.max_ticks==0 || result.max_human_turns==0 ||
       !std::isfinite(result.max_seconds) || result.max_seconds<=0)
        throw std::runtime_error("Usage: session_playthrough_probe --archive FILE --terrain-tables FILE "
            "--output NEW_DIRECTORY [--strategy direct --group 0 --start-mission 0 --seed 0x4741 "
            "--max-missions 17 --max-ticks 200000 --max-human-turns 500 --max-seconds 90 --verify-support 0]");
    (void)g1::mission_resource_for_campaign(result.group,result.start);
    if(!result.initial_profile.empty() && !result.resume_checkpoint.empty())
        throw std::runtime_error("A profile-menu start cannot also restore a session");
    if(result.group<2 && (result.arena_faction!=0 || result.arena_opponent!=1))
        throw std::runtime_error("Only Arena has faction/opponent choices");
    if(result.resume_checkpoint.empty()!=result.resume_profile.empty())
        throw std::runtime_error("A resume requires both the actual native checkpoint and profile");
    if(result.followup_policy!="baseline" && result.followup_policy!="cohesive_right" &&
       result.followup_policy!="cohesive_left" && result.followup_policy!="combat" &&
       result.followup_policy!="capture_right" && result.followup_policy!="ranged" &&
       result.followup_policy!="aggressive" && result.followup_policy!="defensive" &&
       result.followup_policy!="ranged_economy")throw std::runtime_error("Unknown follow-up input policy");
    if(result.strategy!="direct" && result.strategy!="economy" &&
       result.strategy!="coordinated" && result.strategy!="coordinated-economy" &&
       result.strategy!="native-planner" && result.strategy!="native-planner-search" &&
       result.strategy!="native-planner-all" && result.strategy!="tactical-search" &&
       result.strategy!="native-planner-tactical" && result.strategy!="mode-hybrid")
        throw std::runtime_error("Unknown input strategy");
    return result;
}

struct Counters {
    std::size_t ticks{},commands{},selections{},moves{},attacks{},captures{},transforms{},
        heals{},upgrades{},productions{},human_end_turns{},ai_turn_entries{},
        synthetic_world{},synthetic_camera{},synthetic_dialogue{},synthetic_focus{},
        synthetic_transform{},synthetic_battle{},synthetic_results{},profile_writes{},
        next_mission_installs{},checkpoints{},search_commands{},search_accepted{},search_settle_bounds{},
        search_solutions{},replay_checkpoint_matches{};
};

struct Goal {
    g1::WorldCoordinate tile;
    bool capture{};
    std::optional<std::uint8_t> required_type;
    std::optional<std::size_t> enemy;
};

std::vector<Goal> goals_for(const g1::GameSession& session) {
    const auto& world=session.world();
    std::vector<Goal> goals;
    const auto owner=session.current_player();
    const auto objective=session.mission().mission_objective;
    if(objective) {
        const auto& args=objective->parameters;
        if(objective->kind==g1::MissionObjectiveKind::site_owner && args[1]==owner) {
            for(std::size_t s=0;s<world.sites.size();++s)
                if((args[0]<0 || static_cast<std::size_t>(args[0])==s) && session.sites()[s].state!=owner)
                    goals.push_back({world.sites[s].position,true,{},{}});
        } else if(objective->kind==g1::MissionObjectiveKind::site_item_owner && args[3]==owner) {
            const bool all=args[0]==-1 || args[1]==-1 || args[2]==-1;
            for(std::size_t s=0;s<world.sites.size();++s) {
                if(!all && static_cast<std::size_t>(args[0])!=s)continue;
                if(all && session.sites()[s].state!=owner)
                    goals.push_back({world.sites[s].position,true,{},{}});
                for(std::size_t g=0;g<world.sites[s].coordinate_groups.size();++g) {
                    if(!all && static_cast<std::size_t>(args[1])!=g)continue;
                    for(std::size_t item=0;item<world.sites[s].coordinate_groups[g].size();++item)
                        if((all || static_cast<std::size_t>(args[2])==item) &&
                           session.sites()[s].item_states[g][item]!=owner)
                            goals.push_back({world.sites[s].coordinate_groups[g][item],true,{},{}});
                }
            }
        } else if(objective->kind==g1::MissionObjectiveKind::unit_at_position) {
            if(args[1]>=0 && args[2]>=0 && args[1]<world.grid.width && args[2]<world.grid.height)
                goals.push_back({{static_cast<std::uint8_t>(args[1]),static_cast<std::uint8_t>(args[2])},
                    false,args[0]>=0 ? std::optional<std::uint8_t>(static_cast<std::uint8_t>(args[0])) : std::nullopt,{}});
        } else if(objective->kind==g1::MissionObjectiveKind::inactive_unit && args[0]>=0) {
            for(std::size_t i=0;i<session.combat_units().size();++i) {
                const auto& unit=session.combat_units()[i];
                if(unit.active && unit.owner!=owner && unit.type==args[0])
                    goals.push_back({{unit.x,unit.y},false,{},i});
            }
        }
    }
    if(!goals.empty())return goals;
    for(std::size_t i=0;i<session.combat_units().size();++i) {
        const auto& unit=session.combat_units()[i];
        if(unit.active && unit.owner!=owner)goals.push_back({{unit.x,unit.y},false,{},i});
    }
    return goals;
}

std::vector<Goal> economy_goals(const g1::GameSession& session) {
    std::vector<Goal> result;
    const auto owner=session.current_player();
    for(std::size_t s=0;s<session.world().sites.size();++s) {
        const auto& site=session.world().sites[s];const auto& state=session.sites()[s];
        if(state.state!=owner)result.push_back({site.position,true,{},{}});
        for(std::size_t g=0;g<site.coordinate_groups.size();++g)
            for(std::size_t i=0;i<site.coordinate_groups[g].size();++i)
                if(state.item_states[g][i]!=owner)
                    result.push_back({site.coordinate_groups[g][i],true,{},{}});
    }
    return result;
}

// Test-input navigation only. These distances never replace native movement
// legality: selected_movement_range and move_selected_to authorize every move.
std::vector<std::int32_t> distances_to(const g1::MovementBoard& board,
    const g1::MovementProfile& profile,const std::vector<g1::WorldCoordinate>& destinations) {
    std::vector<std::int32_t> result(board.cells.size(),infinity);
    using Item=std::pair<std::int32_t,std::size_t>;
    std::priority_queue<Item,std::vector<Item>,std::greater<Item>> pending;
    for(const auto destination:destinations) {
        const auto cell=std::size_t(destination.y)*board.width+destination.x;
        if(cell<result.size()){result[cell]=0;pending.push({0,cell});}
    }
    constexpr int dx[]{0,0,-1,1},dy[]{-1,1,0,0};
    while(!pending.empty()) {
        const auto [cost,cell]=pending.top();pending.pop();
        if(result[cell]!=cost)continue;
        const auto x=static_cast<std::int32_t>(cell%board.width);
        const auto y=static_cast<std::int32_t>(cell/board.width);
        const auto entry=g1::movement_entry_cost(board,profile,x,y);
        if(entry==99 || entry>=g1::movement_blocked ||
           (profile.traversal_field6<=0 && board.cells[cell].terrain_cost==99))continue;
        for(std::size_t direction=0;direction<4;++direction) {
            const auto px=x+dx[direction],py=y+dy[direction];
            if(px<0 || py<0 || px>=board.width || py>=board.height)continue;
            const auto previous=std::size_t(py)*board.width+static_cast<std::size_t>(px);
            const auto next=cost+std::max(0,entry);
            if(next<result[previous]){result[previous]=next;pending.push({next,previous});}
        }
    }
    return result;
}

struct MoveChoice {
    std::optional<g1::WorldCoordinate> destination;
    std::int32_t distance{infinity};
    std::int32_t after_distance{infinity};
    int estimate{infinity};
};

MoveChoice plan_move(const g1::GameSession& session,std::size_t actor,std::uint8_t form,
                     const std::vector<Goal>& goals) {
    const auto& unit=session.units()[actor];const auto& identity=session.world().units[actor];
    const auto& definition=session.world().unit_definitions[identity.type];
    const auto& stats=definition.forms.at(form);
    const auto board=session.world().movement_board();
    auto profile=session.world().movement_profile(actor);
    profile.base_budget=stats[0];profile.traversal_field6=static_cast<std::int8_t>(signed_byte(stats[6]));
    std::vector<g1::WorldCoordinate> destinations;
    bool needs_capable_form=false;
    for(const auto& goal:goals) {
        if(goal.required_type && *goal.required_type!=identity.type)continue;
        if(goal.enemy) {
            const auto& attack_form=signed_byte(stats[3])>0 ? stats : definition.forms[0];
            if(signed_byte(attack_form[3])<=0)continue;
            const int minimum=signed_byte(attack_form[1]),maximum=signed_byte(attack_form[2]);
            for(std::uint16_t y=0;y<board.height;++y)for(std::uint16_t x=0;x<board.width;++x) {
                const g1::WorldCoordinate p{static_cast<std::uint8_t>(x),static_cast<std::uint8_t>(y)};
                const auto d=distance(p,goal.tile);
                if(d>=minimum && d<=maximum &&
                   (board.cells[std::size_t(y)*board.width+x].active_owner<0 ||
                    (x==unit.tile_x && y==unit.tile_y)))destinations.push_back(p);
            }
            needs_capable_form=needs_capable_form || signed_byte(stats[3])<=0;
        } else {
            destinations.push_back(goal.tile);
            needs_capable_form=needs_capable_form || (goal.capture && stats[10]!=1);
        }
    }
    MoveChoice choice;if(destinations.empty())return choice;
    const auto origin=std::size_t(unit.tile_y)*board.width+unit.tile_x;
    auto distances=distances_to(board,profile,destinations);
    if(distances.at(origin)>=infinity) {
        // Advice about a FUTURE path must not treat every moving opponent as
        // a permanent wall. This temporary planning copy ignores occupancy
        // only for its distance estimate. The real range below still uses
        // the untouched live board, and public move_selected_to authorizes
        // every submitted destination. No session ownership/state is changed.
        auto geometry_only=board;
        for(auto& cell:geometry_only.cells)cell.active_owner=-1;
        distances=distances_to(geometry_only,profile,destinations);
    }
    choice.distance=distances.at(origin);
    if(choice.distance>=infinity)return choice;
    const int budget=std::max(1,signed_byte(stats[0]));
    choice.estimate=(choice.distance+budget-1)/budget+(needs_capable_form ? 1 : 0);
    if(unit.spent_budget!=0 || unit.flag_2c!=0)return choice;
    const auto field=g1::compute_movement_range(board,profile,unit.tile_x,unit.tile_y);
    if(!field.has_unoccupied_destination)return choice;
    auto geometric=[&destinations](g1::WorldCoordinate p) {
        int best=infinity;for(const auto target:destinations)best=std::min(best,distance(p,target));return best;
    };
    auto best_score=std::int64_t(choice.distance)*1024+geometric({unit.tile_x,unit.tile_y});
    for(std::uint16_t y=0;y<board.height;++y)for(std::uint16_t x=0;x<board.width;++x) {
        const auto cell=std::size_t(y)*board.width+x;
        if((x==unit.tile_x && y==unit.tile_y) || board.cells[cell].active_owner>=0 ||
           field.remaining[cell]<0 || field.remaining[cell]==127 || distances[cell]>=infinity)continue;
        const g1::WorldCoordinate p{static_cast<std::uint8_t>(x),static_cast<std::uint8_t>(y)};
        const auto score=std::int64_t(distances[cell])*1024+geometric(p);
        if(score<best_score) {
            choice.destination=p;choice.after_distance=distances[cell];best_score=score;
        }
    }
    return choice;
}

struct MissionResult {
    std::uint8_t group{};
    std::int32_t index{};
    std::uint32_t resource{};
    std::string handoff;
    std::size_t first_tick{},last_tick{};
    g1::CampaignContext context;
    g1::MissionRecord record;
    bool profile_written{};
};

class Pilot {
public:
    Pilot(Options options):options_(std::move(options)),
        archive_(g1::read_file(options_.archive)),
        tables_(g1::decode_terrain_tables(g1::read_file(options_.tables,480))) {
        output_=options_.output;
        if(std::filesystem::exists(output_))throw std::runtime_error("Output directory must be new");
        std::filesystem::create_directories(output_);
        events_.open(output_/"events.jsonl");
        if(!events_)throw std::runtime_error("Cannot create event log");
        if(options_.verify_support)
            support_validation_=std::make_unique<g1::tools::SupportPilotValidation>(archive_);
        if(!options_.initial_profile.empty()) {
            const auto imported=g1::read_file(options_.initial_profile);
            (void)g1::decode_campaign_profile(imported);
            const auto copied=g1::write_native_save_atomic((output_/"campaign.g1native").string(),imported);
            if(!copied.succeeded)throw g1::FormatError("Cannot copy actual completed profile: "+copied.error);
            if(g1::read_file((output_/"campaign.g1native").string())!=imported)
                throw g1::FormatError("Imported profile bytes changed");
        }
        if(!options_.resume_checkpoint.empty()) {
            const auto profile=g1::read_file(options_.resume_profile);
            (void)g1::decode_campaign_profile(profile);
            const auto wrote=g1::write_native_save_atomic((output_/"campaign.g1native").string(),profile);
            if(!wrote.succeeded)throw g1::FormatError("Cannot copy checked native profile: "+wrote.error);
            const auto saved=g1::read_file(options_.resume_checkpoint);
            const auto decoded=g1::decode_session_checkpoint(archive_,saved,&tables_);
            session_=g1::GameSession::restore_checkpoint(archive_,decoded);
            options_.group=decoded.campaign.group;options_.start=decoded.campaign.mission_index;
            model_feedback(*session_);
            if(!session_->checkpoint_available() || g1::encode_session_checkpoint(session_->make_checkpoint())!=saved)
                throw g1::FormatError("Native saved-objective restore changed its serialized gameplay state");
            event("restored_live_checkpoint","\"input\":"+quote(options_.resume_checkpoint)+
                ",\"encoded_bytes_equal\":true,\"counterfactual_state_installed\":false");
        } else {
            const g1::CampaignController menu((output_/"campaign.g1native").string());
            const auto& profile=menu.profile();
            // Identical admission conditions to the native app's profile menu.
            if((options_.group<2 && options_.start>profile.highest_unlocked[options_.group]) ||
               (options_.group==1 && !profile.completed[0]) ||
               (options_.group==2 && !profile.completed[1]))
                throw g1::FormatError("Campaign mission is not unlocked in the actual saved profile");
            g1::CampaignContext context;context.group=options_.group;context.mission_index=options_.start;
            context.player_factions={{options_.arena_faction,static_cast<std::uint8_t>(1-options_.arena_faction)}};
            context.player_controllers={{0,options_.arena_opponent}};
            const g1::GameRandom random(options_.seed);
            session_=std::make_unique<g1::GameSession>(archive_,
                g1::bootstrap_campaign(archive_,context,random,false,&tables_),context);
        }
        controller_=std::make_unique<g1::CampaignController>((output_/"campaign.g1native").string());
        last_player_=session_->current_player();started_=Clock::now();
        if(!options_.initial_profile.empty())event("profile_menu_start_admitted",
            "\"input_profile\":"+quote(options_.initial_profile)+",\"profile_bytes_unchanged\":true,\"unlock_flags_forced\":false");
        event("started","\"presentation_feedback\":\"synthetic\",\"seed\":"+std::to_string(options_.seed));
    }

    int run() {
        std::string outcome="tick_bound",error;
        try {
            for(;counts_.ticks<options_.max_ticks;++counts_.ticks) {
                if(elapsed()>=options_.max_seconds){outcome="wall_time_bound";break;}
                if(session_->turn().human_turn_counter_274>static_cast<std::int32_t>(options_.max_human_turns)) {
                    outcome="human_turn_bound";break;
                }
                log_transition();
                synthetic_feedback();
                if(session_->campaign_context().group!=0 && session_->mission().gate==g1::MissionGate::objective &&
                   session_->current_player()==0 && session_->checkpoint_available()) {
                    const auto context=session_->campaign_context();
                    const auto key=std::make_pair(int(context.group),context.mission_index);
                    if(saved_entries_.insert(key).second) {
                        const auto stem="entry-"+std::to_string(context.group)+"-"+std::to_string(context.mission_index);
                        const auto bytes=g1::encode_session_checkpoint(session_->make_checkpoint());
                        const auto wrote=g1::write_native_save_atomic((output_/(stem+".g1native")).string(),bytes);
                        if(!wrote.succeeded || g1::read_file((output_/(stem+".g1native")).string())!=bytes)
                            throw g1::FormatError("Live mode-entry checkpoint write failed");
                        const auto restored=g1::GameSession::restore_checkpoint(archive_,g1::decode_session_checkpoint(archive_,bytes,&tables_));
                        model_feedback(*restored);
                        if(g1::encode_session_checkpoint(restored->make_checkpoint())!=bytes)
                            throw g1::FormatError("Live mode-entry checkpoint changed after reconstruction");
                        const auto profile=g1::read_file((output_/"campaign.g1native").string());
                        if(!g1::write_native_save_atomic((output_/(stem+".profile")).string(),profile).succeeded)
                            throw g1::FormatError("Live mode-entry profile copy failed");
                        ++counts_.checkpoints;
                        event("actual_mode_entry_checkpoint","\"bytes\":"+std::to_string(bytes.size())+
                              ",\"reconstructed_bytes_equal\":true,\"live_state_replaced\":false");
                    }
                }
                if(session_->campaign_context().mission_index==options_.capture_mission &&
                   session_->turn().human_turn_counter_274>=options_.capture_turn &&
                   session_->mission().gate==g1::MissionGate::objective &&
                   session_->current_player()==0 && session_->checkpoint_available()) {
                    const auto saved=g1::encode_session_checkpoint(session_->make_checkpoint());
                    const auto write=g1::write_native_save_atomic((output_/"captured-objective.g1native").string(),saved);
                    if(!write.succeeded)throw g1::FormatError("Actual objective checkpoint write failed: "+write.error);
                    const auto profile=g1::read_file((output_/"campaign.g1native").string());
                    const auto profile_write=g1::write_native_save_atomic((output_/"captured-profile.g1native").string(),profile);
                    if(!profile_write.succeeded)throw g1::FormatError("Actual objective profile write failed: "+profile_write.error);
                    event("real_objective_checkpoint_captured","\"bytes\":"+std::to_string(saved.size())+
                          ",\"counterfactual_state_installed\":false");
                    ++counts_.checkpoints;outcome="live_checkpoint_captured";break;
                }
                if(session_->mission().stop==g1::MissionStop::campaign_handoff_required) {
                    const auto result=handoff();
                    if(!result.empty()){outcome=result;break;}
                    continue;
                }
                if(session_->mission().stop!=g1::MissionStop::none &&
                   session_->mission().stop!=g1::MissionStop::completion_presentation_required)
                    throw g1::FormatError("Native mission stopped: "+session_->mission().stop_detail);
                if(session_->mission().interaction_mode==0 && !session_->moving() &&
                   !session_->battle() && !session_->focus_request() && !session_->world_presentation() &&
                   session_->turn().controllers[session_->current_player()]!=1)
                    human_input();
                g1::MissionTickInput input;
                if(session_->mission().gate==g1::MissionGate::dialogue) {
                    input={16,g1::DialogueLayout{1,16,14,3,40,true}};
                    ++counts_.synthetic_dialogue;
                }
                const auto tick=session_->tick(30,input);
                if(tick.completion)event("native_completion_request",
                    "\"text_resource\":"+(tick.completion->text_resource_id ?
                        std::to_string(*tick.completion->text_resource_id) : "null"));
                if(tick.movement_finished)event("movement_finished");
                if(tick.turn_transition)event("native_turn_transition");
            }
        } catch(const std::exception& e) {outcome="native_exception";error=e.what();event("exception","\"error\":"+quote(error));}
        checkpoint();
        write_result(outcome,error);
        std::cout<<"Outcome "<<outcome<<"; native handoffs "<<history_.size()
                 <<"; ticks "<<counts_.ticks<<"; human moves "<<counts_.moves
                 <<"; attacks "<<counts_.attacks<<"; captures "<<counts_.captures<<"\n";
        if(!error.empty())std::cerr<<error<<'\n';
        return error.empty() ? 0 : 1;
    }

private:
    Options options_;
    g1::Archive archive_;
    g1::TerrainTables tables_;
    std::filesystem::path output_;
    std::ofstream events_;
    std::unique_ptr<g1::GameSession> session_;
    std::unique_ptr<g1::CampaignController> controller_;
    std::unique_ptr<g1::tools::SupportPilotValidation> support_validation_;
    Counters counts_;
    Clock::time_point started_;
    std::size_t actor_cursor_{},mission_first_tick_{};
    std::uint8_t last_player_{};
    std::string last_state_;
    std::vector<MissionResult> history_;
    std::size_t last_command_{std::numeric_limits<std::size_t>::max()};
    std::set<std::pair<std::size_t,std::uint8_t>> tried_forms_;
    std::vector<std::size_t> actor_order_;
    unsigned human_pass_{};
    struct HumanPlan {std::size_t actor;g1::AIPlan plan;};
    std::optional<HumanPlan> human_plan_;
    enum class SearchCommandKind { transform,move,end_turn,attack,capture,heal,upgrade,produce };
    struct SearchCommand {
        SearchCommandKind kind{};
        std::size_t actor{};
        g1::WorldCoordinate tile{};
        g1::Bytes expected_ready_checkpoint;
        std::size_t target{};
        g1::ProductionMode production_mode{g1::ProductionMode::ordinary};
    };
    struct SearchNode {
        g1::SessionCheckpoint checkpoint;
        std::vector<SearchCommand> commands;
        double score{};
    };
    std::vector<SearchCommand> escape_commands_;
    std::size_t escape_cursor_{};
    std::optional<std::size_t> searched_command_;
    g1::Bytes expected_escape_checkpoint_;
    std::set<std::pair<int,std::int32_t>> saved_entries_;
    std::vector<SearchCommand> tactical_commands_;
    std::size_t tactical_cursor_{};
    g1::Bytes expected_tactical_checkpoint_;

    bool coordinated() const {return options_.strategy.find("coordinated")==0;}
    bool economy() const {return options_.strategy.find("economy")!=std::string::npos;}
    void reset_input_turn() {actor_cursor_=0;human_pass_=0;actor_order_.clear();tried_forms_.clear();human_plan_.reset();}

    double elapsed() const {return std::chrono::duration<double>(Clock::now()-started_).count();}

    void event(const std::string& name,const std::string& fields={}) {
        events_<<"{\"tick\":"<<counts_.ticks<<",\"event\":"<<quote(name)
            <<",\"group\":"<<unsigned(session_->campaign_context().group)
            <<",\"mission\":"<<session_->campaign_context().mission_index
            <<",\"command\":"<<session_->mission().command_index;
        if(!fields.empty())events_<<','<<fields;
        events_<<"}\n";events_.flush();
    }

    void log_transition() {
        const auto& mission=session_->mission();
        std::ostringstream state;state<<session_->world().mission_resource_id<<':'<<mission.command_index<<':'
            <<int(mission.gate)<<':'<<int(mission.stop)<<':'<<int(session_->current_player())<<':'
            <<session_->turn().human_turn_counter_274;
        if(state.str()!=last_state_) {
            event("state","\"gate\":"+std::to_string(int(mission.gate))+
                ",\"phase\":"+std::to_string(mission.original_phase)+",\"owner\":"+
                std::to_string(session_->current_player())+",\"human_turn\":"+
                std::to_string(session_->turn().human_turn_counter_274));
            last_state_=state.str();
            if(mission.gate==g1::MissionGate::objective && session_->current_player()==0) {
                std::ostringstream details;
                details<<"\"funds\":["<<session_->turn().funds[0]<<','<<session_->turn().funds[1]<<"],\"units\":[";
                bool first=true;
                for(const auto& unit:session_->combat_units()) {
                    if(!first)details<<',';
                    first=false;
                    details<<"{\"type\":"<<unsigned(unit.type)<<",\"owner\":"<<unsigned(unit.owner)
                        <<",\"active\":"<<(unit.active ? "true" : "false")<<",\"x\":"<<unsigned(unit.x)
                        <<",\"y\":"<<unsigned(unit.y)<<",\"form\":"<<unsigned(unit.form)
                        <<",\"health\":"<<unsigned(unit.health)<<",\"level\":"<<unsigned(unit.level)<<'}';
                }
                details<<"]";
                event("human_turn_snapshot",details.str());
            }
        }
        if(last_player_!=session_->current_player()) {
            last_player_=session_->current_player();reset_input_turn();
            if(session_->turn().controllers[last_player_]==1)++counts_.ai_turn_entries;
        }
        if(last_command_!=mission.command_index) {
            last_command_=mission.command_index;reset_input_turn();
            escape_commands_.clear();escape_cursor_=0;searched_command_.reset();expected_escape_checkpoint_.clear();
            tactical_commands_.clear();tactical_cursor_=0;expected_tactical_checkpoint_.clear();
        }
    }

    void synthetic_feedback() {
        if(session_->world_presentation()) {
            event("synthetic_world_presented","\"token\":"+std::to_string(session_->world_presentation()->token));
            session_->report_world_presented(session_->world_presentation()->token);++counts_.synthetic_world;
        }
        if(session_->focus_request()) {
            event("synthetic_focus_presented","\"token\":"+std::to_string(session_->focus_request()->token));
            session_->report_focus_presented(session_->focus_request()->token);++counts_.synthetic_focus;
        }
        if(session_->mission().gate==g1::MissionGate::camera) {
            session_->report_camera(session_->mission().command_index,true);++counts_.synthetic_camera;
        }
        for(std::size_t i=0;i<session_->units().size();++i) {
            if(session_->transform_animation_pending(i))session_->clear_transform_animation_request(i);
            if(session_->unit_transform_waiting(i)) {
                session_->report_transform_finished(i,session_->unit_transform_generation(i));
                ++counts_.synthetic_transform;
            }
        }
        if(session_->battle()) {
            const auto battle=*session_->battle();
            event("synthetic_battle_finished","\"attacker_type\":"+
                std::to_string(battle.attacker_before.type)+",\"defender_type\":"+
                std::to_string(battle.defender_before.type)+",\"defender_health_after\":"+
                std::to_string(session_->combat_units()[battle.defender].health)+
                ",\"attacker_health_after\":"+
                std::to_string(session_->combat_units()[battle.attacker].health));
            if(support_validation_)support_validation_->verify(*session_);
            session_->report_battle_finished(battle.token);++counts_.synthetic_battle;
        }
    }

    bool capture_here(std::size_t actor) {
        if(!session_->selected_can_capture())return false;
        const auto& identity=session_->world().units[actor];
        const auto& form=session_->world().unit_definitions[identity.type].forms[session_->units()[actor].form];
        if(form[10]!=1)return false;
        const auto result=session_->capture_selected();
        if(result.status!=g1::CaptureStatus::captured)return false;
        ++counts_.captures;++counts_.commands;
        event("human_capture","\"type\":"+std::to_string(identity.type));return true;
    }

    bool attack_here(std::size_t actor) {
        const auto targets=session_->selected_attack_targets();if(targets.empty())return false;
        auto chosen=targets.front();
        for(auto target:targets)
            if(session_->combat_units()[target].health<session_->combat_units()[chosen].health)chosen=target;
        if(economy() && !coordinated()) {
            const auto& world=session_->world();const auto& units=session_->combat_units();
            const g1::CombatRulesView rules{world.grid,world.effective_lookup,world.terrain_rules,
                                           world.unit_definitions,world.flag_227};
            const auto& a=units[actor];const auto& d=units[chosen];
            const auto attack=g1::combat_bonuses(rules,units,actor,chosen);
            const auto defense=g1::combat_bonuses(rules,units,chosen,actor);
            const int expected_attack=std::max(5,((int(a.stats.attack_low)+int(a.stats.attack_high))/2+
                attack.attack-int(d.stats.defense)-attack.defense)*int(a.health)/100);
            const bool counter=g1::combat_can_target(rules,units,chosen,actor) &&
                !(world.unit_definitions[a.type].forms[0][2]>1 && distance({a.x,a.y},{d.x,d.y})>1);
            const int expected_counter=counter ? std::max(5,((int(d.stats.attack_low)+int(d.stats.attack_high))/2+
                defense.attack-int(a.stats.defense)-defense.defense)*std::max(0,int(d.health)-expected_attack)/100) : 0;
            // A test-input heuristic, never an alternative battle resolver.
            // Wait for an upgrade/support opportunity instead of knowingly
            // choosing a poor exchange against a surviving stronger target.
            if(expected_attack<int(d.health) && expected_attack*2<expected_counter)return false;
        }
        const auto type=session_->world().units[actor].type;
        const auto target_type=session_->world().units[chosen].type;
        if(!session_->attack_selected(chosen))return false;
        ++counts_.attacks;++counts_.commands;
        event("human_attack","\"type\":"+std::to_string(type)+",\"target_type\":"+std::to_string(target_type));
        return true;
    }

    bool produce_here(std::size_t actor,const std::vector<Goal>& goals) {
        for(auto mode:{g1::ProductionMode::deploy,g1::ProductionMode::ordinary}) {
            const auto candidates=session_->selected_production_candidates(mode);
            const auto placement=session_->selected_production_placement(mode);
            if(candidates.empty() || !placement)continue;
            std::optional<std::uint8_t> type;int best=-infinity;
            for(auto candidate:candidates) {
                const auto& def=session_->world().unit_definitions[candidate];
                if(!g1::production_affordable(def,session_->turn().funds[session_->current_player()]))continue;
                const int value=signed_byte(def.forms[0][3])+signed_byte(def.forms[0][4])+
                    5*signed_byte(def.forms[0][0]);
                if(value>best){best=value;type=candidate;}
            }
            if(!type)continue;
            const auto board=session_->world().movement_board();
            std::optional<g1::WorldCoordinate> tile;int closest=infinity;
            for(std::uint16_t y=0;y<board.height;++y)for(std::uint16_t x=0;x<board.width;++x) {
                const auto cell=std::size_t(y)*board.width+x;
                if(placement->range.remaining[cell]<0 || board.cells[cell].active_owner>=0)continue;
                const g1::WorldCoordinate position{static_cast<std::uint8_t>(x),static_cast<std::uint8_t>(y)};
                int score=0;if(!goals.empty()){score=infinity;for(const auto& goal:goals)score=std::min(score,distance(position,goal.tile));}
                if(!tile || score<closest){tile=position;closest=score;}
            }
            if(!tile)continue;
            const auto producer=session_->world().units[actor].type;
            if(session_->produce_selected(mode,*type,*tile,g1::ProductionConfirmation::direct_type_list)) {
                ++counts_.productions;++counts_.commands;
                event("human_production","\"producer\":"+std::to_string(producer)+",\"type\":"+
                    std::to_string(*type)+",\"confirmation\":\"direct_type_list\",\"strategy_checked_funds\":true");
                return true;
            }
        }
        return false;
    }

    void native_planned_input() {
        const auto& world=session_->world();
        if(actor_order_.empty()) {
            for(std::size_t i=0;i<session_->units().size();++i)actor_order_.push_back(i);
            std::stable_sort(actor_order_.begin(),actor_order_.end(),[&world](auto a,auto b) {
                return world.units[a].type<world.units[b].type;
            });
        }
        while(actor_order_.size()<session_->units().size())actor_order_.push_back(actor_order_.size());
        while(actor_cursor_<actor_order_.size()) {
            const auto actor=actor_order_[actor_cursor_];
            const auto& motion=session_->units()[actor];
            if(!motion.active || world.units[actor].owner!=session_->current_player() || motion.flag_2c) {
                ++actor_cursor_;human_plan_.reset();continue;
            }
            if(!session_->selected() || *session_->selected()!=actor) {
                if(!session_->select(actor))return;
                ++counts_.selections;
            }
            if(!human_plan_) {
                if(produce_here(actor,goals_for(*session_)))return;
                const auto board=world.movement_board();
                const std::array<std::int8_t,2> factions{{
                    static_cast<std::int8_t>(world.player_factions[0]),
                    static_cast<std::int8_t>(world.player_factions[1])}};
                const g1::AIWorldView view{{world.grid,world.effective_lookup,world.terrain_rules,
                    world.unit_definitions,world.flag_227},board,session_->combat_units(),session_->units(),
                    world.sites,session_->captures(),factions,session_->turn().controllers,
                    session_->current_player(),session_->turn().funds,session_->upgrade_slots(),
                    g1::ai_owner_slots(world.unit_definitions,session_->combat_units(),factions),world.available_types};
                // Reuse a read-only native planning function as test-input
                // advice. Its returned temporary form/stat side effects are
                // NOT applied. Every desired form/move/action must go through
                // the real human command and its stricter admission rules.
                const auto planned=g1::plan_ai_unit(view,actor);
                if(!planned.plan) {++actor_cursor_;continue;}
                human_plan_=HumanPlan{actor,*planned.plan};
                event("human_plan_advice","\"type\":"+std::to_string(world.units[actor].type)+
                    ",\"action\":"+std::to_string(planned.plan->action)+
                    ",\"x\":"+std::to_string(planned.plan->destination.x)+
                    ",\"y\":"+std::to_string(planned.plan->destination.y));
            }
            const auto plan=human_plan_->plan;
            const bool at_destination=motion.tile_x==plan.destination.x && motion.tile_y==plan.destination.y;
            const auto desired_form=static_cast<std::uint8_t>(at_destination && plan.transform_on_arrival
                ? 1-plan.movement_form : plan.movement_form);
            if(motion.form!=desired_form) {
                if(session_->transform_selected()) {
                    ++counts_.transforms;++counts_.commands;
                    event("human_transform","\"type\":"+std::to_string(world.units[actor].type)+
                        ",\"form\":"+std::to_string(desired_form));return;
                }
                event("human_plan_rejected","\"reason\":\"human_transform_admission\"");
                human_plan_.reset();++actor_cursor_;continue;
            }
            if(!at_destination) {
                const auto request=session_->move_selected_to(plan.destination);
                if(request==g1::MoveRequestResult::started) {
                    ++counts_.moves;++counts_.commands;
                    event("human_move","\"type\":"+std::to_string(world.units[actor].type)+
                        ",\"x\":"+std::to_string(plan.destination.x)+",\"y\":"+std::to_string(plan.destination.y));return;
                }
                event("human_plan_rejected","\"reason\":\"human_move_admission\",\"code\":"+std::to_string(int(request)));
                human_plan_.reset();++actor_cursor_;continue;
            }
            std::optional<std::size_t> target;
            for(std::size_t i=0;i<world.units.size();++i)if(world.units[i].type==plan.target_type)target=i;
            bool accepted=false;
            if(plan.action==1 && target && session_->attack_selected(*target)) {
                ++counts_.attacks;++counts_.commands;accepted=true;
                event("human_attack","\"type\":"+std::to_string(world.units[actor].type)+
                    ",\"target_type\":"+std::to_string(plan.target_type));
            } else if(plan.action==2)accepted=capture_here(actor);
            else if(plan.action==3 && target) {
                const auto hp=session_->combat_units()[*target].health;
                const auto healed=session_->heal_selected(*target);
                if(healed && healed->outcome==g1::HealOutcome::healed) {
                    ++counts_.heals;++counts_.commands;accepted=true;
                    event("human_heal","\"type\":"+std::to_string(world.units[actor].type)+
                        ",\"health_before\":"+std::to_string(hp)+",\"health_after\":"+
                        std::to_string(healed->target.health)+",\"funds_spent\":"+std::to_string(healed->spent));
                }
            } else if(plan.action==4 && session_->upgrade_selected()) {
                ++counts_.upgrades;++counts_.commands;accepted=true;
                event("human_upgrade","\"type\":"+std::to_string(world.units[actor].type));
            }
            if(plan.action!=0 && !accepted)event("human_plan_rejected","\"reason\":\"human_action_admission\"");
            human_plan_.reset();++actor_cursor_;
            if(accepted)return;
        }
        if(session_->end_turn_available() && session_->end_turn()) {
            ++counts_.human_end_turns;++counts_.commands;reset_input_turn();event("human_end_turn");
        }
    }

    static void model_feedback(g1::GameSession& model) {
        if(model.world_presentation())model.report_world_presented(model.world_presentation()->token);
        if(model.focus_request())model.report_focus_presented(model.focus_request()->token);
        for(std::size_t i=0;i<model.units().size();++i) {
            if(model.transform_animation_pending(i))model.clear_transform_animation_request(i);
            if(model.unit_transform_waiting(i))model.report_transform_finished(i,model.unit_transform_generation(i));
        }
        if(model.battle())model.report_battle_finished(model.battle()->token);
    }

    static bool model_ready(g1::GameSession& model,std::size_t command) {
        for(unsigned ticks=0;ticks<3000;++ticks) {
            (void)model.tick(30,{});model_feedback(model);
            if(model.mission().command_index!=command || model.mission().gate!=g1::MissionGate::objective)return true;
            if(model.turn().controllers[model.current_player()]!=1 && model.checkpoint_available())return true;
        }
        return false;
    }

    static bool escaped(const g1::GameSession& model,g1::WorldCoordinate goal) {
        if(model.mission().gate==g1::MissionGate::objective)return false;
        for(std::size_t i=0;i<model.units().size();++i) {
            const auto& u=model.units()[i];
            if(u.active && model.world().units[i].owner==0 && u.tile_x==goal.x && u.tile_y==goal.y &&
               u.offset_x==0 && u.offset_y==0)return true;
        }
        return false;
    }

    double escape_score(const g1::GameSession& model,g1::WorldCoordinate goal) const {
        auto geometry=model.world().movement_board();
        for(auto& cell:geometry.cells)cell.active_owner=-1;
        std::int32_t nearest=infinity;int health{},alive{},freedom{};
        for(std::size_t i=0;i<model.units().size();++i) {
            const auto& u=model.units()[i];if(!u.active || model.world().units[i].owner!=0)continue;
            ++alive;health+=model.combat_units()[i].health;
            const auto profile=model.world().movement_profile(i);
            const auto distances=distances_to(geometry,profile,{goal});
            nearest=std::min(nearest,distances[std::size_t(u.tile_y)*geometry.width+u.tile_x]);
            const auto range=g1::compute_movement_range(model.world().movement_board(),profile,u.tile_x,u.tile_y);
            for(auto remaining:range.remaining)if(remaining>=0 && remaining!=127)++freedom;
        }
        return -100.0*nearest+health*0.6+alive*80.0+freedom*0.8;
    }

    std::vector<g1::WorldCoordinate> escape_destinations(g1::GameSession& model,std::size_t actor,
                                                       g1::WorldCoordinate goal) const {
        if(!model.selected() || *model.selected()!=actor)if(!model.select(actor))return {};
        const auto range=model.selected_movement_range();if(!range)return {};
        const auto board=model.world().movement_board();auto geometry=board;
        for(auto& cell:geometry.cells)cell.active_owner=-1;
        const auto distances=distances_to(geometry,model.world().movement_profile(actor),{goal});
        struct Candidate {g1::WorldCoordinate tile;double score;};
        std::vector<Candidate> choices;
        for(std::uint16_t y=0;y<board.height;++y)for(std::uint16_t x=0;x<board.width;++x) {
            const auto cell=std::size_t(y)*board.width+x;
            if(range->remaining[cell]<0 || range->remaining[cell]==127 || board.cells[cell].active_owner>=0)continue;
            const g1::WorldCoordinate tile{static_cast<std::uint8_t>(x),static_cast<std::uint8_t>(y)};
            int separation=20;
            for(const auto& enemy:model.combat_units())if(enemy.active && enemy.owner!=0)
                separation=std::min(separation,distance(tile,{enemy.x,enemy.y}));
            choices.push_back({tile,-double(distances[cell])*20.0+std::min(separation,5)*4.0});
        }
        std::stable_sort(choices.begin(),choices.end(),[](const auto& a,const auto& b){return a.score>b.score;});
        std::vector<g1::WorldCoordinate> result;
        for(const auto& candidate:choices) {result.push_back(candidate.tile);if(result.size()==10)break;}
        // Retain safe retreats and lateral alternatives, rather than pruning
        // every route which must first draw a guard out of a narrow corridor.
        for(unsigned side=0;side<4 && !choices.empty();++side) {
            const auto best=std::min_element(choices.begin(),choices.end(),[side](const auto& a,const auto& b) {
                const int av=side<2 ? a.tile.x : a.tile.y,bv=side<2 ? b.tile.x : b.tile.y;
                return side%2 ? av>bv : av<bv;
            })->tile;
            if(std::none_of(result.begin(),result.end(),[best](auto p){return p.x==best.x && p.y==best.y;}))result.push_back(best);
        }
        return result;
    }

    bool search_escape() {
        const auto& objective=*session_->mission().mission_objective;
        const g1::WorldCoordinate goal{static_cast<std::uint8_t>(objective.parameters[1]),
                                      static_cast<std::uint8_t>(objective.parameters[2])};
        const auto initial_command=session_->mission().command_index;
        searched_command_=initial_command;
        std::vector<std::size_t> humans;
        for(std::size_t i=0;i<session_->units().size();++i)
            if(session_->units()[i].active && session_->world().units[i].owner==0)humans.push_back(i);
        if(humans.empty() || humans.size()>2 || !session_->checkpoint_available())return false;
        const auto search_started=Clock::now();std::size_t evaluated{},accepted_evaluations{},settle_bounds{};bool budget=false;
        const auto available=[&]() {
            return evaluated<30000 && std::chrono::duration<double>(Clock::now()-search_started).count()<10.0;
        };
        const auto clone=[this](const g1::SessionCheckpoint& checkpoint) {
            auto model=g1::GameSession::restore_checkpoint(archive_,checkpoint);model_feedback(*model);return model;
        };
        std::vector<SearchNode> frontier{{session_->make_checkpoint(),{},0}};
        std::vector<SearchCommand> found;
        const auto advance=[&](const SearchNode& parent,SearchCommand command)->std::optional<SearchNode> {
            if(!available()){budget=true;return {};}
            ++evaluated;auto model=clone(parent.checkpoint);
            bool accepted=false;
            if(command.kind==SearchCommandKind::end_turn)accepted=bool(model->end_turn());
            else if(model->select(command.actor)) {
                if(command.kind==SearchCommandKind::transform)accepted=bool(model->transform_selected());
                else accepted=model->move_selected_to(command.tile)==g1::MoveRequestResult::started;
            }
            if(!accepted)return {};
            ++accepted_evaluations;
            if(!model_ready(*model,initial_command)){++settle_bounds;return {};}
            auto path=parent.commands;
            if(escaped(*model,goal)) {path.push_back(std::move(command));found=std::move(path);return {};}
            if(!model->checkpoint_available() || model->current_player()!=0 ||
               model->mission().command_index!=initial_command)return {};
            auto checkpoint=model->make_checkpoint();command.expected_ready_checkpoint=g1::encode_session_checkpoint(checkpoint);
            path.push_back(std::move(command));
            return SearchNode{std::move(checkpoint),std::move(path),escape_score(*model,goal)};
        };
        const auto prune=[](std::vector<SearchNode> nodes,std::size_t limit) {
            std::stable_sort(nodes.begin(),nodes.end(),[](const auto& a,const auto& b){return a.score>b.score;});
            std::unordered_set<std::string> seen;std::vector<SearchNode> result;
            for(auto& node:nodes) {
                std::ostringstream key;
                for(std::size_t i=0;i<node.checkpoint.combat.size();++i) {
                    const auto& u=node.checkpoint.combat[i];
                    key<<int(u.type)<<','<<u.active<<','<<int(u.x)<<','<<int(u.y)<<','<<int(u.form)<<','<<int(u.health)<<';';
                }
                if(seen.insert(key.str()).second){result.push_back(std::move(node));if(result.size()==limit)break;}
            }
            return result;
        };
        for(unsigned turn=0;turn<10 && found.empty() && available() && !frontier.empty();++turn) {
            std::vector<SearchNode> next;
            for(const auto& start:frontier) {
                for(unsigned order=0;order<(humans.size()==2 ? 2U : 1U) && found.empty() && available();++order) {
                    auto actors=humans;if(order)std::reverse(actors.begin(),actors.end());
                    std::vector<SearchNode> partial{start};
                    for(const auto actor:actors) {
                        std::vector<SearchNode> expanded;
                        for(auto& node:partial) {
                            auto model=clone(node.checkpoint);
                            if(actor>=model->units().size() || !model->units()[actor].active) {expanded.push_back(std::move(node));continue;}
                            const auto& definition=model->world().unit_definitions[model->world().units[actor].type];
                            const auto form=model->units()[actor].form;
                            if(definition.forms[1-form][0]>definition.forms[form][0]) {
                                auto transformed=advance(node,{SearchCommandKind::transform,actor,{},{}});
                                if(transformed){node=std::move(*transformed);model=clone(node.checkpoint);}
                            }
                            const auto destinations=escape_destinations(*model,actor,goal);
                            expanded.push_back(node); // Choosing no command for this unit is legal.
                            for(auto tile:destinations) {
                                auto moved=advance(node,{SearchCommandKind::move,actor,tile,{}});
                                if(moved)expanded.push_back(std::move(*moved));
                                if(!found.empty() || !available())break;
                            }
                            if(!found.empty() || !available())break;
                        }
                        partial=prune(std::move(expanded),8);
                        if(!found.empty() || !available())break;
                    }
                    if(!found.empty() || !available())break;
                    for(const auto& node:partial) {
                        auto ended=advance(node,{SearchCommandKind::end_turn,0,{},{}});
                        if(ended)next.push_back(std::move(*ended));
                        if(!found.empty() || !available())break;
                    }
                }
                if(!found.empty() || !available())break;
            }
            frontier=prune(std::move(next),12);
            event("escape_search_depth","\"turns\":"+std::to_string(turn+1)+
                ",\"branch_command_evaluations\":"+std::to_string(evaluated)+
                ",\"surviving_frontier\":"+std::to_string(frontier.size())+
                ",\"best_score\":"+(frontier.empty() ? "null" : std::to_string(frontier.front().score)));
        }
        event("escape_search","\"branch_command_evaluations\":"+std::to_string(evaluated)+
            ",\"accepted_commands\":"+std::to_string(accepted_evaluations)+",\"settle_bounds\":"+std::to_string(settle_bounds)+
            ",\"found\":"+(found.empty() ? "false" : "true")+",\"budget_reached\":"+
            ((!available() || budget) ? "true" : "false")+",\"state_transplanted\":false");
        counts_.search_commands+=evaluated;
        counts_.search_accepted+=accepted_evaluations;counts_.search_settle_bounds+=settle_bounds;
        if(found.empty())return false;
        ++counts_.search_solutions;
        escape_commands_=std::move(found);escape_cursor_=0;return true;
    }

    bool replay_escape_command() {
        if(!expected_escape_checkpoint_.empty()) {
            if(!session_->checkpoint_available() ||
               g1::encode_session_checkpoint(session_->make_checkpoint())!=expected_escape_checkpoint_)
                throw g1::FormatError("Escape test-input replay diverged from its public-command simulation");
            ++counts_.replay_checkpoint_matches;
            expected_escape_checkpoint_.clear();
        }
        if(escape_cursor_>=escape_commands_.size())return false;
        const auto& command=escape_commands_[escape_cursor_++];bool accepted=false;
        if(command.kind==SearchCommandKind::end_turn) {
            accepted=bool(session_->end_turn());if(accepted){++counts_.human_end_turns;event("human_end_turn");}
        } else if(session_->select(command.actor)) {
            ++counts_.selections;
            if(command.kind==SearchCommandKind::transform) {
                accepted=bool(session_->transform_selected());
                if(accepted){++counts_.transforms;event("human_transform","\"type\":"+std::to_string(session_->world().units[command.actor].type)+
                    ",\"form\":"+std::to_string(session_->units()[command.actor].form));}
            } else {
                accepted=session_->move_selected_to(command.tile)==g1::MoveRequestResult::started;
                if(accepted){++counts_.moves;event("human_move","\"type\":"+std::to_string(session_->world().units[command.actor].type)+
                    ",\"x\":"+std::to_string(command.tile.x)+",\"y\":"+std::to_string(command.tile.y));}
            }
        }
        if(!accepted)throw g1::FormatError("Escape test-input replay command was rejected");
        ++counts_.commands;expected_escape_checkpoint_=command.expected_ready_checkpoint;return true;
    }

    // Development-only counterfactual input search. Checkpoints are restored
    // without edits, and every candidate uses the same public commands as a
    // human. Only the accepted command sequence is replayed into session_.
    static bool apply_tactical_command(g1::GameSession& model,const SearchCommand& command) {
        if(command.kind==SearchCommandKind::end_turn)return bool(model.end_turn());
        if(!model.select(command.actor))return false;
        switch(command.kind) {
        case SearchCommandKind::transform:return bool(model.transform_selected());
        case SearchCommandKind::move:return model.move_selected_to(command.tile)==g1::MoveRequestResult::started;
        case SearchCommandKind::attack:return model.attack_selected(command.target);
        case SearchCommandKind::capture:return model.capture_selected().status==g1::CaptureStatus::captured;
        case SearchCommandKind::heal: {
            const auto result=model.heal_selected(command.target);
            return result && result->outcome==g1::HealOutcome::healed;
        }
        case SearchCommandKind::upgrade:return bool(model.upgrade_selected());
        case SearchCommandKind::produce: {
            if(command.target>=model.world().unit_definitions.size() ||
               !g1::production_affordable(model.world().unit_definitions[command.target],
                                           model.turn().funds[model.current_player()]))return false;
            return bool(model.produce_selected(command.production_mode,
                static_cast<std::uint8_t>(command.target),command.tile,
                g1::ProductionConfirmation::direct_type_list));
        }
        case SearchCommandKind::end_turn:break;
        }
        return false;
    }

    std::string effective_policy(const g1::GameSession& model) const {
        if(options_.strategy!="mode-hybrid")return options_.followup_policy;
        const auto context=model.campaign_context();
        if(context.group==1) {
            if(context.mission_index<4 || context.mission_index==7)return "baseline";
            if(context.mission_index==4)return "combat";
            if(context.mission_index==5 && model.turn().human_turn_counter_274<30)return "baseline";
        }
        return "aggressive";
    }

    bool followup_active(const g1::GameSession& model) const {
        const auto context=model.campaign_context();
        return effective_policy(model)!="baseline" &&
            ((context.group==0 && context.mission_index>=14) ||
             (context.group==1 && context.mission_index>=4) || context.group==2);
    }

    std::vector<Goal> tactical_goals(const g1::GameSession& model) const {
        auto goals=goals_for(model);
        if(!followup_active(model) || goals.empty())return goals;
        if(effective_policy(model)=="ranged_economy") {
            const auto captures=economy_goals(model);goals.insert(goals.end(),captures.begin(),captures.end());
            return goals;
        }
        const bool right=effective_policy(model)!="cohesive_left";
        const bool capture=goals.front().capture;
        // A test-input objective priority, never a change to the actual goal.
        // The whole squad approaches one defended station/target together.
        std::stable_sort(goals.begin(),goals.end(),[&](const auto& a,const auto& b){
            if(capture && a.tile.x!=b.tile.x)return right ? a.tile.x>b.tile.x : a.tile.x<b.tile.x;
            int da{},db{};
            for(const auto& unit:model.combat_units())if(unit.active && unit.owner==0) {
                da+=distance({unit.x,unit.y},a.tile);db+=distance({unit.x,unit.y},b.tile);
            }
            return da<db;
        });
        goals.resize(1);return goals;
    }

    double tactical_score(const g1::GameSession& model,const std::vector<Goal>& goals) const {
        double score{};int own_count{},enemy_count{};
        const bool followup=followup_active(model);
        for(const auto& unit:model.combat_units()) {
            if(!unit.active)continue;
            const auto& definition=model.world().unit_definitions[unit.type];
            const auto robot=g1::combat_stats(definition,0,unit.level);
            const auto vehicle=g1::combat_stats(definition,1,unit.level);
            const double power=180.0+std::max(robot.attack_high,vehicle.attack_high)+
                0.6*std::max(robot.defense,vehicle.defense)+
                8.0*std::max(signed_byte(definition.forms[0][0]),signed_byte(definition.forms[1][0]));
            const auto value=power*(followup && effective_policy(model)=="ranged_economy" ?
                0.05+0.95*std::pow(unit.health/100.0,1.4) : 0.25+0.75*unit.health/100.0);
            const bool late=followup && (model.campaign_context().group!=0 || model.campaign_context().mission_index>=15);
            const double own_weight=late && effective_policy(model)=="defensive" ? 6.0 :
                late && effective_policy(model)=="ranged_economy" ? 4.0 : followup ? 3.0 : 1.0;
            const double enemy_weight=late && effective_policy(model)=="aggressive" ? 4.0 :
                late && effective_policy(model)=="ranged_economy" ? 3.0 : followup ? 2.0 : 1.15;
            if(unit.owner==0){++own_count;score+=value*own_weight;}
            else {++enemy_count;score-=value*enemy_weight;}
        }
        if(own_count==0)return -1.0e9;
        if(enemy_count==0)return 1.0e8+score;
        for(const auto& site:model.sites()) {
            const double owner_value=followup && effective_policy(model)=="ranged_economy" ? 1500.0 :
                !followup || effective_policy(model)=="capture_right" ? 1800.0 :
                (effective_policy(model)=="combat" || effective_policy(model)=="ranged" ||
                 effective_policy(model)=="aggressive" || effective_policy(model)=="defensive") ? 30.0 : 600.0;
            score+=site.state==0 ? owner_value : (site.state==1 ? -owner_value/2.0 : 0.0);
            for(const auto& group:site.item_states)for(auto owner:group)
                score+=owner==0 ? 300.0 : (owner==1 ? -150.0 : 0.0);
        }
        // Money is useful only through legal future actions. Avoid valuing a
        // cash reserve more than the active army it can buy or improve.
        score+=0.75*model.turn().funds[0]-0.4*model.turn().funds[1];
        for(const auto& goal:goals) {
            double nearest=100.0;
            for(const auto& unit:model.combat_units()) {
                if(!unit.active || unit.owner!=0 || (goal.required_type && unit.type!=*goal.required_type))continue;
                const auto& definition=model.world().unit_definitions[unit.type];
                if(goal.capture && definition.forms[0][10]!=1 && definition.forms[1][10]!=1)continue;
                const int speed=std::max(signed_byte(definition.forms[0][0]),signed_byte(definition.forms[1][0]));
                if(speed<=0)continue;
                nearest=std::min(nearest,double(distance({unit.x,unit.y},goal.tile)));
            }
            score-=22.0*nearest;
        }
        if(followup && !goals.empty()) {
            for(std::size_t i=0;i<model.combat_units().size();++i) {
                const auto& unit=model.combat_units()[i];if(!unit.active || unit.owner!=0)continue;
                const auto& def=model.world().unit_definitions[unit.type];
                if(signed_byte(def.forms[0][0])>0 || signed_byte(def.forms[1][0])>0)
                    score-=9.0*distance({unit.x,unit.y},goals.front().tile);
                for(std::size_t j=i+1;j<model.combat_units().size();++j) {
                    const auto& other=model.combat_units()[j];if(!other.active || other.owner!=0)continue;
                    score-=5.0*std::max(0,distance({unit.x,unit.y},{other.x,other.y})-3);
                }
            }
        }
        if(followup && (model.campaign_context().group!=0 || model.campaign_context().mission_index>=15) && effective_policy(model)!="combat" &&
           effective_policy(model)!="cohesive_left" && effective_policy(model)!="cohesive_right") {
            for(const auto& unit:model.combat_units()) {
                if(!unit.active || unit.owner!=0)continue;
                const auto& def=model.world().unit_definitions[unit.type];
                const auto& robot=def.forms[0];
                int closest=100;
                for(const auto& enemy:model.combat_units())if(enemy.active && enemy.owner!=0)
                    closest=std::min(closest,distance({unit.x,unit.y},{enemy.x,enemy.y}));
                const int minimum=signed_byte(robot[1]),maximum=signed_byte(robot[2]);
                if(signed_byte(robot[3])>0) {
                    if(closest<minimum)score-=250.0*(minimum-closest);
                    else if(closest>maximum)score-=12.0*(closest-maximum);
                }
                if(unit.form==1 && signed_byte(def.forms[1][3])==0)score-=4.0;
            }
        }
        return score;
    }

    static bool tactical_goal_reached(const g1::GameSession& model,const g1::MissionObjective& objective) {
        unsigned own{},enemy{};
        for(const auto& unit:model.combat_units())if(unit.active) {
            if(unit.owner==0)++own;else ++enemy;
        }
        if(own==0)return false;
        if(enemy==0)return true;
        const auto& args=objective.parameters;
        if(objective.kind==g1::MissionObjectiveKind::site_owner) {
            if(args[0]>=0)return model.sites().at(static_cast<std::size_t>(args[0])).state==args[1];
            return std::all_of(model.sites().begin(),model.sites().end(),[&](const auto& site){return site.state==args[1];});
        }
        if(objective.kind==g1::MissionObjectiveKind::site_item_owner)
            return g1::captured_site_objective_satisfied({args[0],args[1],args[2],args[3],
                objective.text_index,objective.text_id},model.sites());
        if(objective.kind==g1::MissionObjectiveKind::inactive_unit) {
            if(args[0]>=0) {
                for(const auto& unit:model.combat_units())if(unit.type==args[0])return !unit.active;
                return true;
            }
            const auto faction=args[0]==-1 ? 0 : 1;
            for(const auto& unit:model.combat_units())if(unit.active && unit.faction==faction)return false;
            return true;
        }
        return false;
    }

    static std::vector<SearchNode> tactical_prune(std::vector<SearchNode> nodes,std::size_t limit) {
        std::stable_sort(nodes.begin(),nodes.end(),[](const auto& a,const auto& b){return a.score>b.score;});
        std::unordered_set<std::string> seen;std::vector<SearchNode> result;
        for(auto& node:nodes) {
            std::ostringstream key;
            for(std::size_t i=0;i<node.checkpoint.combat.size();++i) {
                const auto& u=node.checkpoint.combat[i];const auto& m=node.checkpoint.movement[i];
                key<<int(u.type)<<','<<u.active<<','<<int(u.x)<<','<<int(u.y)<<','<<int(u.form)<<','
                   <<int(u.health)<<','<<int(u.level)<<','<<int(m.spent_budget)<<','<<int(m.flag_2c)<<';';
            }
            key<<node.checkpoint.turn.funds[0]<<','<<node.checkpoint.turn.funds[1]<<';';
            for(const auto& site:node.checkpoint.captures.sites) {
                key<<int(site.state)<<';';
                for(const auto& group:site.item_states)for(auto owner:group)key<<int(owner)<<',';
            }
            if(seen.insert(key.str()).second) {result.push_back(std::move(node));if(result.size()==limit)break;}
        }
        return result;
    }

    std::vector<g1::WorldCoordinate> tactical_destinations(g1::GameSession& model,std::size_t actor,
                                                         const std::vector<Goal>& goals) const {
        if(!model.select(actor))return {};
        const auto range=model.selected_movement_range();if(!range)return {};
        const auto board=model.world().movement_board();const auto& unit=model.combat_units()[actor];
        const auto& form=model.world().unit_definitions[unit.type].forms[unit.form];
        struct Candidate {g1::WorldCoordinate tile;double score;};std::vector<Candidate> choices;
        for(std::uint16_t y=0;y<board.height;++y)for(std::uint16_t x=0;x<board.width;++x) {
            const auto cell=std::size_t(y)*board.width+x;
            if(range->remaining[cell]<0 || range->remaining[cell]==127 || board.cells[cell].active_owner>=0)continue;
            const g1::WorldCoordinate tile{static_cast<std::uint8_t>(x),static_cast<std::uint8_t>(y)};
            int nearest=100;for(const auto& goal:goals)nearest=std::min(nearest,distance(tile,goal.tile));
            double value=-5.0*nearest;
            const bool ranged_followup=followup_active(model) && (model.campaign_context().group!=0 || model.campaign_context().mission_index>=15) &&
                (effective_policy(model)=="ranged" || effective_policy(model)=="aggressive" || effective_policy(model)=="defensive" ||
                 effective_policy(model)=="ranged_economy");
            int closest_enemy=100;
            for(const auto& enemy:model.combat_units())if(enemy.active && enemy.owner!=unit.owner) {
                const int d=distance(tile,{enemy.x,enemy.y});closest_enemy=std::min(closest_enemy,d);
                if(signed_byte(form[3])>0 && d>=signed_byte(form[1]) && d<=signed_byte(form[2]))
                    value+=25.0+(100-enemy.health)*0.1;
                const auto& opposing=model.world().unit_definitions[enemy.type].forms[enemy.form];
                if(signed_byte(opposing[3])>0 && d<=signed_byte(opposing[2])+signed_byte(opposing[0]))value-=8.0;
            }
            if(ranged_followup) {
                const auto& robot=model.world().unit_definitions[unit.type].forms[0];
                const int minimum=signed_byte(robot[1]),maximum=signed_byte(robot[2]);
                if(closest_enemy<minimum)value-=100.0*(minimum-closest_enemy);
                else if(closest_enemy<=maximum)value+=35.0;
            }
            for(const auto& goal:goals)if(goal.capture && distance(tile,goal.tile)==0)value+=100.0;
            choices.push_back({tile,value});
        }
        std::stable_sort(choices.begin(),choices.end(),[](const auto& a,const auto& b){return a.score>b.score;});
        std::vector<g1::WorldCoordinate> result;
        const std::size_t destination_limit=followup_active(model) && (model.campaign_context().group!=0 || model.campaign_context().mission_index>=15) ? 16U : 7U;
        for(const auto& choice:choices){result.push_back(choice.tile);if(result.size()==destination_limit)break;}
        for(unsigned side=0;side<4 && !choices.empty();++side) {
            const auto best=std::min_element(choices.begin(),choices.end(),[side](const auto& a,const auto& b){
                const int av=side<2 ? a.tile.x : a.tile.y,bv=side<2 ? b.tile.x : b.tile.y;
                return side%2 ? av>bv : av<bv;
            })->tile;
            if(std::none_of(result.begin(),result.end(),[best](auto p){return p.x==best.x && p.y==best.y;}))result.push_back(best);
        }
        return result;
    }

    bool search_tactical_turn() {
        if(!session_->checkpoint_available() || !session_->mission().mission_objective)return false;
        const auto initial_command=session_->mission().command_index;
        const auto objective=*session_->mission().mission_objective;
        const auto goals=tactical_goals(*session_);const auto began=Clock::now();
        std::size_t evaluated{},accepted{},settle_bounds{};
        std::array<std::size_t,8> accepted_kinds{};
        const auto available=[&](){return evaluated<8000 && elapsed()<options_.max_seconds &&
            std::chrono::duration<double>(Clock::now()-began).count()<6.0;};
        const auto clone=[&](const g1::SessionCheckpoint& checkpoint){
            auto model=g1::GameSession::restore_checkpoint(archive_,checkpoint);model_feedback(*model);return model;
        };
        std::vector<SearchCommand> found;
        const auto advance=[&](const SearchNode& parent,SearchCommand command)->std::optional<SearchNode>{
            if(!available())return {};
            ++evaluated;auto model=clone(parent.checkpoint);
            if(!apply_tactical_command(*model,command))return {};
            ++accepted;++accepted_kinds[static_cast<std::size_t>(command.kind)];
            if(!model_ready(*model,initial_command)){++settle_bounds;return {};}
            auto path=parent.commands;
            // A public attack/capture can finish the objective before the next
            // interpreter tick. Such a state deliberately rejects end_turn().
            // Preserve its input path now; never replace the live state.
            if(tactical_goal_reached(*model,objective)) {
                path.push_back(std::move(command));found=std::move(path);return {};
            }
            if(model->mission().command_index!=initial_command || model->mission().gate!=g1::MissionGate::objective)
                return {};
            if(!model->checkpoint_available() || model->current_player()!=0)return {};
            auto checkpoint=model->make_checkpoint();command.expected_ready_checkpoint=g1::encode_session_checkpoint(checkpoint);
            path.push_back(std::move(command));
            return SearchNode{std::move(checkpoint),std::move(path),tactical_score(*model,goals)};
        };
        const SearchNode start{session_->make_checkpoint(),{},tactical_score(*session_,goals)};
        if(session_->turn().human_turn_counter_274==1 || session_->turn().human_turn_counter_274==20) {
            auto diagnostic=clone(start.checkpoint);
            for(std::size_t i=0;i<diagnostic->units().size();++i) {
                if(!diagnostic->units()[i].active || diagnostic->world().units[i].owner!=0 || !diagnostic->select(i))continue;
                const auto type=diagnostic->world().units[i].type;
                const auto& definition=diagnostic->world().unit_definitions[type];
                std::ostringstream fields;fields<<"\"type\":"<<unsigned(type)<<",\"forms\":[";
                for(std::size_t f=0;f<2;++f) {if(f)fields<<',';fields<<'[';
                    for(std::size_t n=0;n<definition.forms[f].size();++n){if(n)fields<<',';fields<<signed_byte(definition.forms[f][n]);}
                    fields<<']';}
                fields<<"],\"upgrade_eligibility\":"<<int(diagnostic->selected_upgrade_eligibility())
                    <<",\"production_candidates\":"<<diagnostic->selected_production_candidates(g1::ProductionMode::ordinary).size()
                    <<",\"deploy_candidates\":"<<diagnostic->selected_production_candidates(g1::ProductionMode::deploy).size()
                    <<",\"transform_eligibility\":"<<int(diagnostic->selected_transform_eligibility());
                event("tactical_actor_diagnostic",fields.str());
            }
        }
        std::optional<SearchNode> best=advance(start,{SearchCommandKind::end_turn,0,{},{}});
        std::vector<std::size_t> actors;
        for(std::size_t i=0;i<session_->units().size();++i)
            if(session_->units()[i].active && session_->world().units[i].owner==0)actors.push_back(i);
        // Higher potential attackers first; the original type still breaks ties.
        std::stable_sort(actors.begin(),actors.end(),[&](auto a,auto b){
            const auto& left=session_->combat_units()[a];const auto& right=session_->combat_units()[b];
            const auto lp=g1::combat_stats(session_->world().unit_definitions[left.type],0,left.level).attack_high;
            const auto rp=g1::combat_stats(session_->world().unit_definitions[right.type],0,right.level).attack_high;
            return lp!=rp ? lp>rp : left.type<right.type;
        });
        std::vector<SearchNode> partial{start};
        for(auto actor:actors) {
            std::vector<SearchNode> expanded;
            for(const auto& node:partial) {
                if(!available() || !found.empty())break;
                auto model=clone(node.checkpoint);
                if(!model->units()[actor].active || model->units()[actor].flag_2c || !model->select(actor)) {
                    expanded.push_back(node);continue;
                }
                std::vector<SearchNode> forms{node};
                if(model->selected_transform_eligibility()==g1::TransformEligibility::allowed) {
                    auto transformed=advance(node,{SearchCommandKind::transform,actor,{},{}});
                    if(transformed)forms.push_back(std::move(*transformed));
                }
                std::vector<SearchNode> positions;
                for(const auto& form:forms) {
                    positions.push_back(form);auto posed=clone(form.checkpoint);
                    for(auto tile:tactical_destinations(*posed,actor,goals)) {
                        auto moved=advance(form,{SearchCommandKind::move,actor,tile,{}});
                        if(moved)positions.push_back(std::move(*moved));
                        if(!available() || !found.empty())break;
                    }
                }
                // A vehicle may use its movement, then legally transform at
                // the destination and act. This is a public command sequence,
                // not the AI planner's temporary form/state substitution.
                const auto moved_positions=positions;
                for(const auto& position:moved_positions) {
                    if(!available() || !found.empty())break;
                    auto posed=clone(position.checkpoint);
                    if(posed->select(actor) && posed->selected_transform_eligibility()==g1::TransformEligibility::allowed) {
                        auto transformed=advance(position,{SearchCommandKind::transform,actor,{},{}});
                        if(transformed)positions.push_back(std::move(*transformed));
                    }
                }
                for(const auto& position:positions) {
                    if(!available() || !found.empty())break;
                    expanded.push_back(position);auto posed=clone(position.checkpoint);
                    if(!posed->select(actor))continue;
                    std::vector<SearchCommand> actions;
                    if(posed->selected_can_capture())actions.push_back({SearchCommandKind::capture,actor,{},{}});
                    for(auto target:posed->selected_attack_targets())actions.push_back({SearchCommandKind::attack,actor,{},{},target});
                    if(posed->selected_upgrade_eligibility()==g1::UpgradeEligibility::allowed)
                        actions.push_back({SearchCommandKind::upgrade,actor,{},{}});
                    for(auto target:posed->selected_heal_targets())if(posed->combat_units()[target].health<90)
                        actions.push_back({SearchCommandKind::heal,actor,{},{},target});
                    for(auto mode:{g1::ProductionMode::deploy,g1::ProductionMode::ordinary}) {
                        auto candidates=posed->selected_production_candidates(mode);
                        const auto placement=posed->selected_production_placement(mode);if(!placement)continue;
                        candidates.erase(std::remove_if(candidates.begin(),candidates.end(),[&](auto type){
                            return !g1::production_affordable(posed->world().unit_definitions[type],posed->turn().funds[0]);
                        }),candidates.end());
                        std::stable_sort(candidates.begin(),candidates.end(),[&](auto a,auto b){
                            const auto& defs=posed->world().unit_definitions;
                            return signed_byte(defs[a].forms[0][4])>signed_byte(defs[b].forms[0][4]);
                        });
                        if(candidates.size()>2)candidates.resize(2);
                        const auto board=posed->world().movement_board();
                        for(auto type:candidates)for(std::uint16_t y=0;y<board.height;++y)for(std::uint16_t x=0;x<board.width;++x) {
                            const auto cell=std::size_t(y)*board.width+x;
                            if(placement->range.remaining[cell]<0 || placement->range.remaining[cell]==127 || board.cells[cell].active_owner>=0)continue;
                            actions.push_back({SearchCommandKind::produce,actor,{static_cast<std::uint8_t>(x),static_cast<std::uint8_t>(y)},{},type,mode});
                        }
                    }
                    for(auto action:actions) {
                        auto acted=advance(position,std::move(action));if(acted)expanded.push_back(std::move(*acted));
                        if(!available() || !found.empty())break;
                    }
                }
            }
            if(!found.empty())break;
            if(expanded.empty())break;
            // Preserve choices which survive the real opponent response,
            // rather than retaining only attractive but exposed positions.
            // The preview is discarded; node.checkpoint is never edited.
            auto candidates=tactical_prune(std::move(expanded),32);
            for(auto& candidate:candidates) {
                auto preview=advance(candidate,{SearchCommandKind::end_turn,0,{},{}});
                if(preview) {
                    candidate.score=preview->score;
                    if(!best || preview->score>best->score)best=std::move(preview);
                } else candidate.score=-1.0e10;
                if(!available() || !found.empty())break;
            }
            partial=tactical_prune(std::move(candidates),8);
            if(!available())break;
        }
        if(found.empty())for(const auto& node:partial) {
            if(!available())break;
            auto ended=advance(node,{SearchCommandKind::end_turn,0,{},{}});
            if(ended && (!best || ended->score>best->score))best=std::move(ended);
            if(!found.empty())break;
        }
        std::ostringstream kind_counts;kind_counts<<'[';
        for(std::size_t k=0;k<accepted_kinds.size();++k){if(k)kind_counts<<',';kind_counts<<accepted_kinds[k];}
        kind_counts<<']';
        event("tactical_turn_search","\"branch_command_evaluations\":"+std::to_string(evaluated)+
            ",\"accepted_commands\":"+std::to_string(accepted)+",\"settle_bounds\":"+std::to_string(settle_bounds)+
            ",\"objective_reached\":"+(found.empty() ? "false" : "true")+
            ",\"budget_reached\":"+(available() ? "false" : "true")+
            ",\"before_score\":"+std::to_string(start.score)+",\"chosen_score\":"+
            (best ? std::to_string(best->score) : "null")+",\"accepted_by_kind\":"+kind_counts.str()+",\"state_transplanted\":false");
        counts_.search_commands+=evaluated;counts_.search_accepted+=accepted;counts_.search_settle_bounds+=settle_bounds;
        if(!found.empty())tactical_commands_=std::move(found);
        else if(best)tactical_commands_=std::move(best->commands);
        else return false;
        tactical_cursor_=0;++counts_.search_solutions;return true;
    }

    bool replay_tactical_command() {
        if(!expected_tactical_checkpoint_.empty()) {
            if(!session_->checkpoint_available() ||
               g1::encode_session_checkpoint(session_->make_checkpoint())!=expected_tactical_checkpoint_)
                throw g1::FormatError("Tactical test-input replay diverged from its public-command simulation");
            ++counts_.replay_checkpoint_matches;expected_tactical_checkpoint_.clear();
        }
        if(tactical_cursor_>=tactical_commands_.size())return false;
        const auto& command=tactical_commands_[tactical_cursor_++];
        if(!apply_tactical_command(*session_,command))throw g1::FormatError("Tactical test-input replay command rejected");
        ++counts_.commands;if(command.kind!=SearchCommandKind::end_turn)++counts_.selections;
        const auto actor=command.kind==SearchCommandKind::end_turn ? 255U : session_->world().units[command.actor].type;
        switch(command.kind) {
        case SearchCommandKind::move:++counts_.moves;break;
        case SearchCommandKind::transform:++counts_.transforms;break;
        case SearchCommandKind::attack:++counts_.attacks;break;
        case SearchCommandKind::capture:++counts_.captures;break;
        case SearchCommandKind::heal:++counts_.heals;break;
        case SearchCommandKind::upgrade:++counts_.upgrades;break;
        case SearchCommandKind::produce:++counts_.productions;break;
        case SearchCommandKind::end_turn:++counts_.human_end_turns;reset_input_turn();break;
        }
        event("human_search_command","\"kind\":"+std::to_string(int(command.kind))+
            ",\"actor_type\":"+std::to_string(actor)+",\"target\":"+std::to_string(command.target)+
            ",\"x\":"+std::to_string(command.tile.x)+",\"y\":"+std::to_string(command.tile.y));
        expected_tactical_checkpoint_=command.expected_ready_checkpoint;return true;
    }

    void human_input() {
        const auto context=session_->campaign_context();
        if(options_.strategy=="mode-hybrid" && context.group==1 &&
           (context.mission_index==6 || (context.mission_index==5 &&
            session_->turn().human_turn_counter_274>=130))) {
            // Complete/check an existing public-command replay before changing
            // only the input adviser at this ordinary turn boundary.
            if(replay_tactical_command())return;
            native_planned_input();return;
        }
        if((options_.strategy=="mode-hybrid" || options_.strategy=="tactical-search" || (options_.strategy=="native-planner-tactical" &&
             session_->campaign_context().mission_index>=7)) && session_->mission().mission_objective &&
           session_->mission().mission_objective->kind!=g1::MissionObjectiveKind::unit_at_position) {
            if(replay_tactical_command())return;
            if(search_tactical_turn() && replay_tactical_command())return;
        }
        if((options_.strategy=="native-planner-search" || options_.strategy=="native-planner-all" ||
            options_.strategy=="native-planner-tactical") && session_->mission().mission_objective &&
           session_->world().flag_227 &&
           session_->mission().mission_objective->kind==g1::MissionObjectiveKind::unit_at_position &&
           session_->mission().mission_objective->parameters[0]==-3) {
            if(!searched_command_)search_escape();
            if(replay_escape_command())return;
        }
        if(options_.strategy.find("native-planner")==0 && session_->mission().mission_objective &&
           (session_->mission().mission_objective->kind==g1::MissionObjectiveKind::inactive_unit ||
            (options_.strategy=="native-planner-all" &&
             session_->mission().mission_objective->kind!=g1::MissionObjectiveKind::unit_at_position))) {
            native_planned_input();return;
        }
        auto goals=goals_for(*session_);
        if(economy() && session_->mission().mission_objective &&
           session_->mission().mission_objective->kind==g1::MissionObjectiveKind::inactive_unit) {
            auto captures=economy_goals(*session_);
            if(!captures.empty())goals=std::move(captures);
        }
        while(actor_order_.size()<session_->units().size())actor_order_.push_back(actor_order_.size());
        while(actor_cursor_<actor_order_.size()) {
            const auto actor=actor_order_[actor_cursor_];
            const auto& before=session_->units()[actor];
            const auto identity=session_->world().units[actor];
            if(!before.active || identity.owner!=session_->current_player() || before.flag_2c) {++actor_cursor_;continue;}
            if(!session_->selected() || *session_->selected()!=actor) {
                if(!session_->select(actor))return;
                ++counts_.selections;
            }
            if(capture_here(actor) || ((!coordinated() || human_pass_!=0) && attack_here(actor)))return;
            const auto heals=session_->selected_heal_targets();
            if(!heals.empty()) {
                auto target=heals.front();
                for(auto other:heals)if(session_->combat_units()[other].health<session_->combat_units()[target].health)target=other;
                if(session_->combat_units()[target].health<80 && session_->turn().funds[identity.owner]>0) {
                    const auto health_before=session_->combat_units()[target].health;
                    const auto healed=session_->heal_selected(target);
                    if(healed && healed->outcome==g1::HealOutcome::healed) {
                        ++counts_.heals;++counts_.commands;
                        event("human_heal","\"type\":"+std::to_string(identity.type)+
                            ",\"health_before\":"+std::to_string(health_before)+
                            ",\"health_after\":"+std::to_string(healed->target.health)+
                            ",\"funds_spent\":"+std::to_string(healed->spent));return;
                    }
                }
            }
            if(produce_here(actor,goals))return;
            const auto& state=session_->units()[actor];
            const auto current=plan_move(*session_,actor,state.form,goals);
            if(session_->selected_transform_eligibility()==g1::TransformEligibility::allowed) {
                const auto alternative=static_cast<std::uint8_t>(state.form==0 ? 1 : 0);
                const auto other=plan_move(*session_,actor,alternative,goals);
                bool enables_here=false;
                const auto& alternate_stats=session_->world().unit_definitions[identity.type].forms[alternative];
                const auto& current_stats=session_->world().unit_definitions[identity.type].forms[state.form];
                if(session_->selected_can_capture() && current_stats[10]!=1 && alternate_stats[10]==1)enables_here=true;
                if((!session_->world().flag_227 || identity.faction==1) &&
                   signed_byte(current_stats[3])<=0 && signed_byte(alternate_stats[3])>0)
                    for(const auto& enemy:session_->combat_units())if(enemy.active && enemy.owner!=identity.owner) {
                        const auto separation=distance({state.tile_x,state.tile_y},{enemy.x,enemy.y});
                        if(separation>=signed_byte(alternate_stats[1]) && separation<=signed_byte(alternate_stats[2]))enables_here=true;
                    }
                const bool faster=(!coordinated() || human_pass_==0) && state.spent_budget==0 &&
                    other.destination && other.estimate<current.estimate;
                if((enables_here || faster) && !tried_forms_.count({actor,alternative})) {
                    if(session_->transform_selected()) {
                        tried_forms_.insert({actor,alternative});++counts_.transforms;++counts_.commands;
                        event("human_transform","\"type\":"+std::to_string(identity.type)+",\"form\":"+std::to_string(alternative));return;
                    }
                }
            }
            if((!coordinated() || human_pass_==0) && current.destination && state.spent_budget==0) {
                const auto result=session_->move_selected_to(*current.destination);
                if(result==g1::MoveRequestResult::started) {
                    ++counts_.moves;++counts_.commands;
                    event("human_move","\"type\":"+std::to_string(identity.type)+",\"x\":"+
                        std::to_string(current.destination->x)+",\"y\":"+std::to_string(current.destination->y));return;
                }
                event("move_rejected","\"type\":"+std::to_string(identity.type)+",\"reason\":"+std::to_string(int(result)));
            }
            // A legitimate player may upgrade after movement when no immediate
            // attack/capture was available. It does not create an AI command.
            if(session_->selected_upgrade_eligibility()==g1::UpgradeEligibility::allowed &&
               (!coordinated() || human_pass_!=0) &&
               session_->combat_units()[actor].level<(economy() ? 12 : 3) &&
               (economy() || current.distance>0) &&
               session_->turn().funds[identity.owner]>=(economy() ? 1 : 2)*
                    std::max(1,int(g1::unit_spawn_cost(session_->world().unit_definitions[identity.type])))) {
                if(session_->upgrade_selected()) {
                    ++counts_.upgrades;++counts_.commands;event("human_upgrade","\"type\":"+std::to_string(identity.type));return;
                }
            }
            ++actor_cursor_;
        }
        if(coordinated() && human_pass_==0) {
            // Legal input ordering: move the formation before issuing attacks,
            // then let its strongest healthy unit fire first. No unit state or
            // support bonus is written by this test strategy.
            human_pass_=1;actor_cursor_=0;
            std::stable_sort(actor_order_.begin(),actor_order_.end(),[this](auto a,auto b) {
                const auto& ua=session_->combat_units()[a];const auto& ub=session_->combat_units()[b];
                return int(ua.stats.attack_high)*ua.health>int(ub.stats.attack_high)*ub.health;
            });
            return;
        }
        if(session_->end_turn_available()) {
            const auto result=session_->end_turn();
            if(result) {
                ++counts_.human_end_turns;++counts_.commands;reset_input_turn();
                event("human_end_turn");
            }
        }
    }

    std::string handoff() {
        const auto context=session_->campaign_context();
        MissionResult result{context.group,context.mission_index,session_->world().mission_resource_id,
            {},mission_first_tick_,counts_.ticks,context,session_->mission_record(),false};
        const auto write=controller_->begin(*session_);
        if(!write.succeeded)throw g1::FormatError("Real campaign profile write failed: "+write.error);
        ++counts_.profile_writes;result.profile_written=true;
        const auto token=controller_->state().token;
        if(controller_->state().gate==g1::CampaignControllerGate::results) {
            event("synthetic_results_presented","\"token\":"+std::to_string(token));
            controller_->report_results_presented(token,256);++counts_.synthetic_results;
            (void)controller_->update_results(token,16,30);
        }
        const auto gate=controller_->state().gate;
        if(gate==g1::CampaignControllerGate::next_mission) {
            result.handoff="next_mission";history_.push_back(result);
            event("native_handoff_next","\"next_index\":"+std::to_string(*controller_->state().handoff->next_index));
            if(history_.size()>=options_.max_missions)return "mission_count_bound";
            auto prepared=controller_->prepare_next_campaign(token,archive_);
            auto next=std::make_unique<g1::GameSession>(archive_,std::move(prepared.bootstrap),prepared.context);
            controller_->report_next_session_installed(token,*next);
            session_=std::move(next);
            controller_=std::make_unique<g1::CampaignController>((output_/"campaign.g1native").string());
            ++counts_.next_mission_installs;mission_first_tick_=counts_.ticks+1;
            reset_input_turn();last_state_.clear();last_command_=std::numeric_limits<std::size_t>::max();
            last_player_=session_->current_player();return {};
        }
        if(gate==g1::CampaignControllerGate::campaign_ending) {
            result.handoff="campaign_ending";history_.push_back(result);
            event("synthetic_ending_presented");controller_->report_ending_presented(token);
            return "native_campaign_ending";
        }
        if(gate==g1::CampaignControllerGate::campaign_menu) {
            result.handoff="campaign_menu";history_.push_back(result);
            event("synthetic_campaign_menu_presented");controller_->report_menu_presented(token);
            if(context.group==2)return "native_arena_finished";
            return context.active_unit_counts[0]==0 ||
                (context.group==0 && context.mission_index==9 && context.active_unit_counts[0]==1 && context.type_12_active)
                ? "native_loss_handoff" : "native_menu_handoff";
        }
        throw g1::FormatError("Campaign controller did not expose a supported real handoff");
    }

    void checkpoint() {
        if(!session_->checkpoint_available())return;
        try {
            const auto bytes=g1::encode_session_checkpoint(session_->make_checkpoint());
            const auto write=g1::write_native_save_atomic((output_/"last-objective.g1native").string(),bytes);
            if(!write.succeeded)throw g1::FormatError(write.error);
            ++counts_.checkpoints;
            event("real_checkpoint_written","\"bytes\":"+std::to_string(bytes.size()));
        } catch(const std::exception& e) {event("checkpoint_error","\"error\":"+quote(e.what()));}
    }

    void write_result(const std::string& outcome,const std::string& error) {
        std::ofstream out(output_/"result.json");
        if(!out)throw std::runtime_error("Cannot write pilot result");
        const auto& mission=session_->mission();const auto context=session_->campaign_context();
        out<<"{\n\"scope\":\"headless native gameplay command pilot\",\n"
            <<"\"presentation_feedback\":\"synthetic: world/camera/dialogue/focus/transform/battle/results/menu/ending\",\n"
            <<"\"device_or_rendered_playthrough\":false,\n\"gameplay_state_forced\":false,\n"
            <<"\"strategy\":\"public human commands chosen by the named test-input strategy; real native opponent AI\",\n"
            <<"\"strategy_variant\":"<<quote(options_.strategy)<<",\n"
            <<"\"followup_policy\":"<<quote(options_.followup_policy)<<",\n"
            <<"\"start_mode\":"<<quote(!options_.resume_checkpoint.empty() ? "actual_native_checkpoint" :
                !options_.initial_profile.empty() ? "actual_profile_menu_selection" : "fresh_campaign")<<",\n"
            <<"\"initial_profile\":"<<quote(options_.initial_profile)<<",\n"
            <<"\"arena_faction\":"<<unsigned(options_.arena_faction)<<",\n"
            <<"\"arena_opponent\":"<<int(options_.arena_opponent)<<",\n"
            <<"\"resume_input\":"<<quote(options_.resume_checkpoint)<<",\n"
            <<"\"seed\":"<<options_.seed<<",\n\"start_group\":"<<unsigned(options_.group)
            <<",\n\"start_mission\":"<<options_.start<<",\n\"outcome\":"<<quote(outcome)
            <<",\n\"error\":"<<quote(error)<<",\n\"wall_seconds\":"<<elapsed()
            <<",\n\"ticks\":"<<counts_.ticks<<",\n\"simulation_step_ms\":30,\n\"actions\":{"
            <<"\"commands\":"<<counts_.commands<<",\"selections\":"<<counts_.selections
            <<",\"moves\":"<<counts_.moves<<",\"attacks\":"<<counts_.attacks<<",\"captures\":"<<counts_.captures
            <<",\"transforms\":"<<counts_.transforms<<",\"heals\":"<<counts_.heals
            <<",\"upgrades\":"<<counts_.upgrades<<",\"productions\":"<<counts_.productions
            <<",\"human_end_turns\":"<<counts_.human_end_turns<<",\"ai_turn_entries\":"<<counts_.ai_turn_entries<<"},\n"
            <<"\"synthetic_feedback_counts\":{\"world\":"<<counts_.synthetic_world<<",\"camera\":"<<counts_.synthetic_camera
            <<",\"dialogue_ticks\":"<<counts_.synthetic_dialogue<<",\"focus\":"<<counts_.synthetic_focus
            <<",\"transform\":"<<counts_.synthetic_transform<<",\"battle\":"<<counts_.synthetic_battle
            <<",\"results\":"<<counts_.synthetic_results<<"},\n\"profile_writes\":"<<counts_.profile_writes
            <<",\n\"checkpoint_writes\":"<<counts_.checkpoints<<",\n\"next_mission_installs\":"<<counts_.next_mission_installs
            <<",\n\"escape_search\":{\"branch_command_evaluations\":"<<counts_.search_commands
            <<",\"accepted_branch_commands\":"<<counts_.search_accepted<<",\"settle_bounds\":"<<counts_.search_settle_bounds
            <<",\"solutions\":"<<counts_.search_solutions<<",\"live_replay_checkpoint_matches\":"<<counts_.replay_checkpoint_matches
            <<",\"counter_scope\":\"escape and tactical searches combined; solutions are replayable plans, not mission victories\""
            <<",\"state_transplanted\":false,\"per_search_command_bound\":30000,\"per_search_turn_bound\":10,\"per_search_seconds_bound\":10}"
            <<",\n\"tactical_search_limits\":{\"branch_commands\":8000,\"human_turn_depth\":1,\"seconds\":6,"
            <<"\"partial_beam\":8,\"enemy_response_candidates_per_actor\":32,\"state_transplanted\":false}"
            <<",\n\"native_handoffs\":[";
        bool first=true;
        for(const auto& result:history_) {
            if(!first)out<<',';
            first=false;
            out<<"{\"group\":"<<unsigned(result.group)<<",\"index\":"<<result.index
                <<",\"resource\":"<<result.resource<<",\"handoff\":"<<quote(result.handoff)
                <<",\"first_tick\":"<<result.first_tick<<",\"last_tick\":"<<result.last_tick
                <<",\"active_counts\":["<<unsigned(result.context.active_unit_counts[0])<<','
                <<unsigned(result.context.active_unit_counts[1])<<"],\"record\":["<<result.record.value_274<<','
                <<result.record.value_278<<','<<result.record.value_27c<<"],\"profile_written\":"
                <<(result.profile_written ? "true" : "false")<<'}';
        }
        out<<"],\n\"final\":{\"group\":"<<unsigned(context.group)<<",\"mission\":"<<context.mission_index
            <<",\"resource\":"<<session_->world().mission_resource_id<<",\"command\":"<<mission.command_index
            <<",\"gate\":"<<int(mission.gate)<<",\"phase\":"<<mission.original_phase<<",\"stop\":"<<int(mission.stop)
            <<",\"stop_detail\":"<<quote(mission.stop_detail)<<",\"owner\":"<<unsigned(session_->current_player())
            <<",\"human_turn\":"<<session_->turn().human_turn_counter_274<<",\"funds\":["<<session_->turn().funds[0]<<','
            <<session_->turn().funds[1]<<"],\"active_counts\":["<<unsigned(context.active_unit_counts[0])<<','
            <<unsigned(context.active_unit_counts[1])<<"],\"rng_draws\":"<<session_->random().state().draws
            <<",\"objective\":";
        if(mission.mission_objective) {
            const auto& objective=*mission.mission_objective;
            out<<"{\"kind\":"<<int(objective.kind)<<",\"parameters\":[";
            for(std::size_t i=0;i<objective.parameter_count;++i){if(i)out<<',';out<<objective.parameters[i];}
            out<<"]}";
        } else out<<"null";
        out<<",\"units\":[";first=true;
        for(std::size_t i=0;i<session_->units().size();++i) {
            if(!first)out<<',';
            first=false;
            const auto& u=session_->combat_units()[i];const auto& m=session_->units()[i];
            out<<"{\"type\":"<<unsigned(u.type)<<",\"owner\":"<<unsigned(u.owner)<<",\"active\":"<<(u.active ? "true" : "false")
                <<",\"form\":"<<unsigned(u.form)<<",\"position\":["<<unsigned(u.x)<<','<<unsigned(u.y)
                <<"],\"health\":"<<unsigned(u.health)<<",\"level\":"<<unsigned(u.level)
                <<",\"spent\":"<<unsigned(m.spent_budget)<<",\"acted\":"<<unsigned(m.flag_2c)
                <<",\"transformed\":"<<unsigned(m.flag_2d)
                <<",\"damage_immune\":"<<(u.damage_immune ? "true" : "false")
                <<",\"flag_38\":"<<unsigned(u.flag_38)
                <<",\"stats\":["<<u.stats.attack_low<<','<<u.stats.attack_high<<','<<u.stats.defense<<"]}";
        }
        out<<"],\"sites\":[";first=true;
        for(const auto& site:session_->sites()) {
            if(!first)out<<',';
            first=false;out<<"{\"owner\":"<<int(site.state)<<",\"items\":[";
            bool first_group=true;for(const auto& group:site.item_states) {
                if(!first_group)out<<',';
                first_group=false;out<<'[';
                for(std::size_t i=0;i<group.size();++i){if(i)out<<',';out<<int(group[i]);}out<<']';
            }
            out<<"]}";
        }
        out<<"]},\n\"persisted_profile\":{\"completed\":["<<(controller_->profile().completed[0] ? "true" : "false")
            <<','<<(controller_->profile().completed[1] ? "true" : "false")<<"],\"highest_unlocked\":["
            <<controller_->profile().highest_unlocked[0]<<','<<controller_->profile().highest_unlocked[1]<<"]}\n}\n";
        if(!out)throw std::runtime_error("Pilot result write failed");
        if(support_validation_)
            support_validation_->write_report((output_/"support_validation.json").string());
    }
};
}

int main(int argc,char** argv) {
    try {Pilot pilot(parse(argc,argv));return pilot.run();}
    catch(const std::exception& e){std::cerr<<"session_playthrough_probe: "<<e.what()<<'\n';return 2;}
}
