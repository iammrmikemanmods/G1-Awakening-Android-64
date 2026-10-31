#include "g1/TurnProgression.hpp"

#include <algorithm>

namespace g1 {
namespace {
std::int32_t add32(std::int32_t a, std::int32_t b) noexcept {
    const auto word = static_cast<std::uint32_t>(a) + static_cast<std::uint32_t>(b);
    return word <= 0x7fffffffU ? static_cast<std::int32_t>(word) :
        static_cast<std::int32_t>(static_cast<std::int64_t>(word) - 0x100000000LL);
}
} // namespace

void begin_current_player_turn(CaptureNetwork& network, std::array<std::int8_t,2> factions,
                               TurnState& state) {
    if (state.current_player>1) throw FormatError("invalid native turn current player");
    auto updated_network=network;
    auto updated_state=state;
    const auto owner=state.current_player;
    refresh_capture_network(updated_network,factions,true,true);
    updated_state.funds[owner]=add32(updated_state.funds[owner],updated_network.counters_68[owner]);
    updated_state.income_notice_pending=true;
    if (updated_state.controllers[owner]==1) updated_state.ai_unit_cursor=0;
    else updated_state.human_turn_counter_274=add32(updated_state.human_turn_counter_274,1);
    network=std::move(updated_network);
    state=updated_state;
}

TurnTransition advance_player_turn(const std::vector<WorldUnit>& roster,
    std::vector<MovementUnitState>& units, CaptureNetwork& network,
    std::array<std::int8_t,2> factions, TurnState& state) {
    if (state.current_player > 1 || roster.size() != units.size() || roster.size() > 23)
        throw FormatError("invalid native turn roster/current player");
    std::array<bool,23> seen{};
    std::optional<std::size_t> first;
    const auto next = static_cast<std::uint8_t>((state.current_player + 1U) & 1U);
    for (std::size_t i=0;i<roster.size();++i) {
        const auto& unit=roster[i];
        if (unit.owner > 1 || unit.type >= seen.size() || seen[unit.type])
            throw FormatError("turn roster has invalid/duplicate original owner slots");
        seen[unit.type]=true;
        if (unit.owner == next && units[i].active &&
            (!first || unit.type < roster[*first].type)) first=i;
    }
    auto updated_units=units;
    auto updated_network=network;
    auto updated_state=state;
    for (std::size_t i=0;i<roster.size();++i)
        if (roster[i].owner == state.current_player) reset_movement_turn(updated_units[i]);
    TurnTransition result;
    if (first) {
        result.player_changed=true;
        const auto& unit=updated_units[*first];
        result.camera_focus=WorldCoordinate{unit.tile_x,unit.tile_y};
        updated_state.current_player=next;
        begin_current_player_turn(updated_network,factions,updated_state);
    }
    result.snapshot_requested=updated_state.controllers[updated_state.current_player] != 1;
    units=std::move(updated_units);
    network=std::move(updated_network);
    state=updated_state;
    return result;
}
} // namespace g1
