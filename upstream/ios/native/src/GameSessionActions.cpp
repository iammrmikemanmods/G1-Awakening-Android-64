#include "g1/GameSession.hpp"

#include <algorithm>

namespace g1 {
namespace {
std::array<std::int8_t,2> factions(const WorldBootstrap& world) noexcept {
    return {{static_cast<std::int8_t>(world.player_factions[0]),
             static_cast<std::int8_t>(world.player_factions[1])}};
}
}

bool GameSession::selected_human_command_available() const {
    return selected_ && !moving_ && !battle_ && !world_presentation_ && !focus_ &&
        !original_transform_gate_ && mission_.state().interaction_mode==0 &&
        mission_.state().stop==MissionStop::none && turn_.controllers[turn_.current_player]!=1 &&
        units_[*selected_].active && world_.units[*selected_].owner==turn_.current_player;
}

bool GameSession::selected_can_undo() const {
    return selected_human_command_available() &&
        undo_available(units_[*selected_],world_.unit_definitions[world_.units[*selected_].type]);
}

std::optional<UndoResult> GameSession::undo_selected() {
    if(!selected_can_undo())return {};
    const auto actor=*selected_;
    auto result=undo_unit_action(units_[actor],combat_[actor],
        world_.unit_definitions[world_.units[actor].type],animation_pending_[actor],false);
    units_[actor]=result.movement;combat_[actor]=result.combat;
    animation_pending_[actor]=result.animation_pending_2e;
    if(result.reversed_transform) {
        ++transform_generation_[actor];
        transform_waiting_[actor]=animation_pending_[actor]!=0;
    }
    synchronize_unit(actor);
    if(!result.retain_selection)clear_selection();
    return result;
}

ProductionEligibility GameSession::selected_production_eligibility(ProductionMode mode) const {
    if(!selected_human_command_available())return ProductionEligibility::inactive;
    const auto slots=ai_owner_slots(world_.unit_definitions,combat_,factions(world_));
    return production_eligibility({world_.unit_definitions,combat_,units_,slots},
        world_.movement_board(),world_.available_types,*selected_,mode,turn_.funds[turn_.current_player]);
}

std::vector<std::uint8_t> GameSession::selected_production_candidates(ProductionMode mode) const {
    if(selected_production_eligibility(mode)!=ProductionEligibility::allowed)return {};
    const auto slots=ai_owner_slots(world_.unit_definitions,combat_,factions(world_));
    return production_candidates({world_.unit_definitions,combat_,units_,slots},
        world_.available_types,turn_.current_player,mode);
}

std::optional<ProductionPlacement> GameSession::selected_production_placement(ProductionMode mode) const {
    if(selected_production_eligibility(mode)!=ProductionEligibility::allowed)return {};
    const auto& unit=world_.units[*selected_];
    const auto value=world_.unit_definitions[unit.type].forms[unit.form][6];
    const auto field6=static_cast<std::int8_t>(value<128U ? int(value) : int(value)-256);
    return production_placement(world_.movement_board(),{unit.x,unit.y},field6);
}

std::optional<ProductionResult> GameSession::produce_selected(ProductionMode mode,std::uint8_t type,
    WorldCoordinate destination,ProductionConfirmation confirmation) {
    if(selected_production_eligibility(mode)!=ProductionEligibility::allowed)return {};
    const auto actor=*selected_;
    const auto slots=ai_owner_slots(world_.unit_definitions,combat_,factions(world_));
    const auto board=world_.movement_board();
    const UnitInteractionView view{world_.unit_definitions,combat_,units_,slots};
    const auto candidates=production_candidates(view,world_.available_types,turn_.current_player,mode);
    if(std::find(candidates.begin(),candidates.end(),type)==candidates.end())return {};
    if(confirmation==ProductionConfirmation::funds_checked_button &&
       !production_affordable(world_.unit_definitions[type],turn_.funds[turn_.current_player]))return {};
    const auto placement=selected_production_placement(mode);
    const auto& producer=world_.units[actor];
    if(destination.x>=board.width || destination.y>=board.height ||
       (destination.x==producer.x && destination.y==producer.y) ||
       placement->range.at(destination.x,destination.y)<0 ||
       board.cells[std::size_t(destination.y)*board.width+destination.x].active_owner!=-1)return {};
    // Compute the complete original human action before committing any state.
    // commit_activation performs the single debit and active-count increment.
    auto result=produce_unit(view,board,world_.available_types,actor,mode,type,destination,
        turn_.funds[turn_.current_player],confirmation);
    const auto index=find_or_create_unit(type,result.activation.world.owner);
    commit_activation(index,result.activation);
    units_[actor]=result.producer_movement;
    synchronize_unit(actor);
    clear_selection();
    return result;
}

void GameSession::finish_transport_arrival(std::size_t actor) {
    const auto owner=combat_.at(actor).owner;
    const auto slots=ai_owner_slots(world_.unit_definitions,combat_,factions(world_));
    const auto result=apply_transport_arrival({world_.unit_definitions,combat_,units_,slots},
        actor,active_counts_[owner],upgrade_slots_[owner]);
    if(result.matching_carrier_types.empty())return;
    combat_[actor]=result.combat;units_[actor]=result.movement;
    active_counts_[owner]=result.active_count_after;upgrade_slots_[owner]=result.upgrade_slot_after;
    synchronize_unit(actor);
    if(selected_ && *selected_==actor)selected_.reset();
}

} // namespace g1
