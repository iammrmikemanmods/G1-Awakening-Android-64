#include "g1/GameSession.hpp"

#include <algorithm>

namespace g1 {
namespace {
std::int8_t signed_byte(std::uint8_t value) noexcept {
    return static_cast<std::int8_t>(value<128U ? int(value) : int(value)-256);
}
std::array<std::int8_t,2> factions(const WorldBootstrap& world) noexcept {
    return {{signed_byte(world.player_factions[0]),signed_byte(world.player_factions[1])}};
}
void append_transform_audio(SessionTick& tick,const TransformResult& result) {
    if(result.audio_event_in_group5)tick.audio.push_back({5,*result.audio_event_in_group5});
}
}

AIWorldView GameSession::ai_world(const MovementBoard& board) const {
    return {combat_rules(),board,combat_,units_,world_.sites,captures_,factions(world_),
            turn_.controllers,turn_.current_player,turn_.funds,upgrade_slots_,
            ai_owner_slots(world_.unit_definitions,combat_,factions(world_)),world_.available_types};
}

void GameSession::initialize_upgrade_slots() {
    const auto slots=ai_owner_slots(world_.unit_definitions,combat_,factions(world_));
    const UpgradeWorldView view{world_.unit_definitions,combat_,units_,slots};
    for(std::uint8_t owner=0;owner<2;++owner)
        upgrade_slots_[owner]=first_upgrade_slot(view,owner,upgrade_slots_[owner]);
}

UpgradeEligibility GameSession::selected_upgrade_eligibility() const {
    if(!selected_ || moving_ || battle_ || world_presentation_ || focus_ || original_transform_gate_ || mission_.state().interaction_mode!=0 ||
       mission_.state().stop!=MissionStop::none ||
       turn_.controllers[turn_.current_player]==1 ||
       world_.units[*selected_].owner!=turn_.current_player) return UpgradeEligibility::inactive;
    const auto slots=ai_owner_slots(world_.unit_definitions,combat_,factions(world_));
    return upgrade_eligibility({world_.unit_definitions,combat_,units_,slots},*selected_,
                                turn_.funds[world_.units[*selected_].owner]);
}

UpgradeResult GameSession::upgrade_actor(std::size_t actor) {
    const auto slots=ai_owner_slots(world_.unit_definitions,combat_,factions(world_));
    const UpgradeWorldView view{world_.unit_definitions,combat_,units_,slots};
    auto result=apply_upgrade(view,actor,turn_.funds[combat_.at(actor).owner]);
    // The original slot scan uses CURRENT player; the funds use actor owner.
    const auto next=advance_upgrade_slot(view,turn_.current_player,
                                         upgrade_slots_[turn_.current_player]);
    combat_[actor]=result.combat;
    units_[actor]=result.movement;
    turn_.funds[result.combat.owner]=result.funds_after;
    upgrade_slots_[turn_.current_player]=next;
    synchronize_unit(actor);
    return result;
}

std::optional<UpgradeResult> GameSession::upgrade_selected() {
    if(selected_upgrade_eligibility()!=UpgradeEligibility::allowed)return std::nullopt;
    const auto result=upgrade_actor(*selected_);
    clear_selection();
    // Human menu branch59edc requests audio(5,13); host owns that UI request.
    // The AI's556a8 call has no equivalent sound.
    return result;
}

std::vector<std::size_t> GameSession::selected_heal_targets() const {
    if(!selected_ || moving_ || battle_ || world_presentation_ || focus_ || original_transform_gate_ || mission_.state().interaction_mode!=0 ||
       mission_.state().stop!=MissionStop::none || turn_.controllers[turn_.current_player]==1 ||
       world_.units[*selected_].owner!=turn_.current_player)return {};
    const auto actor=*selected_;
    if(heal_eligibility(combat_[actor],units_[actor],world_.unit_definitions[combat_[actor].type])
       !=HealEligibility::allowed)return {};
    return healing_targets(combat_,actor);
}

std::optional<HealResult> GameSession::heal_selected(std::size_t target) {
    const auto targets=selected_heal_targets();
    if(std::find(targets.begin(),targets.end(),target)==targets.end())return std::nullopt;
    const auto actor=*selected_;
    auto result=apply_heal(combat_[actor],units_[actor],world_.unit_definitions[combat_[actor].type],
                           combat_[target],turn_.funds[turn_.current_player]);
    combat_[target]=result.target;
    units_[target].timer_ms=result.target.timer_30_ms;
    units_[actor]=result.actor_movement;
    if(actor==target)units_[actor].timer_ms=result.target.timer_30_ms;
    turn_.funds[turn_.current_player]=result.funds_after;
    synchronize_unit(target);synchronize_unit(actor);
    if(result.outcome==HealOutcome::healed)clear_selection();
    return result;
}

std::size_t GameSession::find_or_create_unit(std::uint8_t type,std::uint8_t owner) {
    if(type>=23 || owner>1)throw FormatError("native spawn identity is invalid");
    for(std::size_t i=0;i<world_.units.size();++i)if(world_.units[i].type==type)return i;
    WorldUnit identity;
    identity.type=type;identity.owner=owner;identity.faction=world_.unit_definitions.at(type).faction();
    identity.active=false;
    auto combat=make_combat_unit(identity,world_.unit_definitions.at(type));
    MovementUnitState motion;motion.active=false;
    world_.units.push_back(identity);units_.push_back(motion);combat_.push_back(combat);
    animation_pending_.push_back(0);transform_generation_.push_back(0);
    transform_waiting_.push_back(false);spawn_generation_.push_back(0);
    return units_.size()-1;
}

void GameSession::commit_activation(std::size_t index,const UnitActivation& activation) {
    if(index>=units_.size() || world_.units[index].type!=activation.world.type)
        throw FormatError("native activation changed a stable unit identity");
    world_.units[index]=activation.world;units_[index]=activation.movement;
    combat_[index]=activation.combat;
    ++spawn_generation_[index];
    const auto owner=activation.world.owner;
    active_counts_[owner]=static_cast<std::uint8_t>(active_counts_[owner]+activation.active_count_increment);
    turn_.funds[owner]=apply_spawn_funds_debit(turn_.funds[owner],activation.signed_cost);
}

void GameSession::clear_ai_action() {
    if(ai_ && ai_->focus_token && focus_ && *ai_->focus_token==focus_->token)focus_.reset();
    ai_.reset();
    if(!moving_)selected_.reset();
}

void GameSession::set_ai_phase(AICommandPhase phase) {
    if(!ai_)throw FormatError("AI phase has no pending action");
    ai_->phase=phase;ai_->elapsed_ms=0;ai_->focus_token.reset();ai_->focus_presented=false;
    if(phase==AICommandPhase::production) {
        // The original production phase has no camera/renderer readiness gate.
        ai_->focus_presented=true;
        return;
    }
    if(!ai_->plan)throw FormatError("AI action phase has no plan");
    const auto& plan=*ai_->plan;
    auto target=plan.destination;
    auto reason=SessionFocusReason::ai_move_destination;
    std::optional<std::size_t> index=ai_->actor;
    if(phase==AICommandPhase::attack || phase==AICommandPhase::heal) {
        index.reset();
        for(std::size_t i=0;i<combat_.size();++i)
            if(combat_[i].type==plan.target_type) {index=i;break;}
        if(!index)throw FormatError("AI plan target has no original unit object");
        target={combat_[*index].x,combat_[*index].y};
        reason=phase==AICommandPhase::attack ? SessionFocusReason::ai_attack_target : SessionFocusReason::ai_heal_target;
    } else if(phase==AICommandPhase::capture_item || phase==AICommandPhase::capture_site) {
        reason=SessionFocusReason::ai_capture_target;
    }
    request_focus(target,reason,index);
    ai_->focus_token=focus_->token;
}

void GameSession::begin_ai_plan(std::size_t actor,const AIPlanningResult& planned,SessionTick& tick) {
    const bool form_changed=units_.at(actor).form!=planned.form_after_planning;
    units_[actor].form=planned.form_after_planning;
    combat_[actor].stats=planned.stats_after_planning;
    synchronize_unit(actor);
    tick.world_changed=tick.world_changed || form_changed;
    if(!planned.plan)return;
    SessionAIAction action;action.actor=actor;action.plan=planned.plan;
    ai_=std::move(action);selected_=actor;
    const auto prepared=prepare_ai_plan(*planned.plan,units_[actor],world_.sites);
    if(prepared.transform_to_movement_form)append_transform_audio(tick,apply_transform(actor,true));
    if(prepared.transform_again)
        append_transform_audio(tick,apply_transform(actor,prepared.second_transform_sets_attack_gate));
    if(prepared.upgrade_immediately) {
        (void)upgrade_actor(actor);tick.world_changed=true;clear_ai_action();return;
    }
    if(prepared.phase_requests.empty()) {clear_ai_action();return;}
    // All same-tile capture setup requests select this same actor/coordinate;
    // the final ordered request is the original active phase.
    set_ai_phase(prepared.phase_requests.back());
}

void GameSession::finish_ai_arrival(std::size_t actor,SessionTick& tick) {
    if(!ai_ || ai_->actor!=actor || !ai_->plan)throw FormatError("AI arrival lost its action plan");
    const auto plan=*ai_->plan;
    if(plan.transform_on_arrival && units_[actor].form==plan.movement_form)
        append_transform_audio(tick,apply_transform(actor,true));
    switch(plan.action) {
    case 0:clear_ai_action();break;
    case 1:set_ai_phase(AICommandPhase::attack);break;
    case 2:set_ai_phase(plan.group==-1 ? AICommandPhase::capture_site : AICommandPhase::capture_item);break;
    case 3:set_ai_phase(AICommandPhase::heal);break;
    case 4:(void)upgrade_actor(actor);tick.world_changed=true;clear_ai_action();break;
    default:throw FormatError("unknown AI arrival action");
    }
}

void GameSession::tick_ai(std::uint32_t delta_ms,SessionTick& tick) {
    if(moving_ || battle_ || turn_.controllers[turn_.current_player]!=1)return;
    const auto board=world_.movement_board();
    const auto view=ai_world(board);
    if(!ai_) {
        // Finish the explicit incoming-turn/restore camera request before a
        // new action replaces it. This is native presentation coordination.
        if(focus_)return;
        const auto admission=choose_ai_turn_admission(view,turn_.ai_unit_cursor);
        if(admission.kind==AITurnAdmissionKind::turn_finished) {
            tick.turn_transition=advance_turn();tick.world_changed=true;return;
        }
        if(!admission.actor)throw FormatError("AI admission omitted its actual actor");
        const auto actor=*admission.actor;
        if(admission.kind==AITurnAdmissionKind::production) {
            if(!admission.production_type)throw FormatError("AI production omitted its type");
            SessionAIAction action;action.actor=actor;action.production_type=admission.production_type;
            ai_=std::move(action);selected_=actor;set_ai_phase(AICommandPhase::production);return;
        }
        const auto planned=plan_ai_unit(view,actor);
        // Original58f14 commits the cursor only after58158/planning returns.
        begin_ai_plan(actor,planned,tick);
        turn_.ai_unit_cursor=admission.next_cursor;
        return;
    }
    if(ai_->battle_pending)return;
    ai_->elapsed_ms=advance_ai_delay(ai_->elapsed_ms,delta_ms);
    if(!ai_action_ready(ai_->phase,ai_->elapsed_ms,ai_->focus_presented,
                        !ai_->focus_presented,original_transform_gate_))return;
    const auto actor=ai_->actor;
    switch(ai_->phase) {
    case AICommandPhase::production: {
        const auto destination=ai_production_destination(view,actor);
        if(!destination)return; // Original phase remains waiting; no invented spawn/end-turn.
        const auto type=*ai_->production_type;
        const auto index=find_or_create_unit(type,turn_.current_player);
        auto identity=world_.units[index];identity.owner=turn_.current_player;
        const auto tile=world_.grid.tile_at(destination->x,destination->y);
        const auto& terrain=world_.terrain_rules.at(world_.effective_lookup.at(tile)).fields;
        auto activated=activate_unit(world_.unit_definitions[type],identity,*destination,terrain[0],terrain[2],
                                     units_[index],combat_[index]);
        activated.movement.flag_2c=1; // Original59264 marks the returned NEW unit.
        commit_activation(index,activated);
        tick.world_changed=true;clear_ai_action();break;
    }
    case AICommandPhase::move: {
        if(!ai_->plan)throw FormatError("AI movement has no plan");
        const auto destination=ai_->plan->destination;
        for(std::size_t i=0;i<units_.size();++i)
            if(i!=actor && units_[i].active && units_[i].tile_x==destination.x && units_[i].tile_y==destination.y)
                throw FormatError("AI movement destination is occupied; transport is not an ordinary move");
        const auto profile=world_.movement_profile(actor);
        const auto range=compute_movement_range(board,profile,units_[actor].tile_x,units_[actor].tile_y);
        auto route=choose_movement_route(range,{units_[actor].tile_x,units_[actor].tile_y},
                                         {destination.x,destination.y});
        if(route.status!=MovementRouteStatus::ready && route.status!=MovementRouteStatus::already_at_destination)
            throw FormatError("AI move cannot reconstruct its original route");
        auto action=std::make_unique<MovementAction>(units_[actor],profile,range,std::move(route),
            signed_byte(world_.unit_definitions[world_.units[actor].type].forms[0][2]));
        action_=std::move(action);moving_=actor;units_[actor]=action_->state();synchronize_unit(actor);
        tick.world_changed=true;break;
    }
    case AICommandPhase::attack: {
        const auto type=ai_->plan->target_type;
        auto target=combat_.size();
        for(std::size_t i=0;i<combat_.size();++i)if(combat_[i].type==type){target=i;break;}
        if(target==combat_.size())throw FormatError("AI combat target is absent");
        begin_battle(actor,target);ai_->battle_pending=true;tick.world_changed=true;break;
    }
    case AICommandPhase::capture_item:
    case AICommandPhase::capture_site: {
        const auto plan=*ai_->plan;
        if(plan.site<0 || (ai_->phase==AICommandPhase::capture_item && (plan.group<0 || plan.item<0)))
            throw FormatError("AI capture target indices are invalid");
        const CaptureTarget target{ai_->phase==AICommandPhase::capture_site ? CaptureTargetKind::site : CaptureTargetKind::item,
            static_cast<std::size_t>(plan.site),static_cast<std::size_t>(std::max(0,plan.group)),
            static_cast<std::size_t>(std::max(0,plan.item))};
        const auto captured=apply_capture_target(world_.sites,captures_,target,turn_.current_player,
                                                 factions(world_),turn_.controllers);
        if(captured.status!=CaptureStatus::captured)throw FormatError("AI capture did not apply its original target");
        units_[actor].flag_2c=1;synchronize_unit(actor);
        if(captured.audio_5_12)tick.audio.push_back({5,12});
        tick.world_changed=true;clear_ai_action();break;
    }
    case AICommandPhase::heal: {
        const auto type=ai_->plan->target_type;
        auto target=combat_.size();
        for(std::size_t i=0;i<combat_.size();++i)if(combat_[i].type==type){target=i;break;}
        if(target==combat_.size())throw FormatError("AI healing target is absent");
        const auto healed=apply_heal(combat_[actor],units_[actor],world_.unit_definitions[combat_[actor].type],
                                     combat_[target],turn_.funds[turn_.current_player]);
        combat_[target]=healed.target;units_[target].timer_ms=healed.target.timer_30_ms;
        units_[actor]=healed.actor_movement;
        if(actor==target)units_[actor].timer_ms=healed.target.timer_30_ms;
        turn_.funds[turn_.current_player]=healed.funds_after;
        synchronize_unit(target);synchronize_unit(actor);
        if(healed.audio_event_in_group5)tick.audio.push_back({5,*healed.audio_event_in_group5});
        if(healed.message_id)tick.message_ids.push_back(*healed.message_id);
        tick.world_changed=true;clear_ai_action();break;
    }
    case AICommandPhase::idle:throw FormatError("AI action remained in idle without an admission request");
    }
}
} // namespace g1
