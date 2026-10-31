#pragma once

#include "g1/MissionProgression.hpp"
#include "g1/World.hpp"

namespace g1 {

struct MissionSiteState {
    std::int8_t state{-1};
    std::vector<std::vector<std::int8_t>> item_states;
};

// Separate from site coordinates: map+0x3e8 is site state, map+0x3f8 is nested
// item state, and opcode7 initializes both to-1. A real capture action must
// update them; merely moving to a coordinate does not satisfy a capture goal.
std::vector<MissionSiteState> initial_mission_site_states(const WorldBootstrap&);

struct CapturedSiteObjective {
    std::int16_t site{}, group{}, item{}, desired_state{};
    std::int16_t text_index{};
    std::uint16_t text_id{};
};

// Original0x55a90..0x55b6c. ANY -1 index selects ALL sites and ALL items;
// it does not retain the other two index filters. Invalid storage fails.
bool captured_site_objective_satisfied(const CapturedSiteObjective&,
                                      const std::vector<MissionSiteState>&);

enum class MissionObjectiveKind : std::uint8_t {
    site_owner = 0,
    site_item_owner = 1,
    inactive_unit = 2,
    unit_at_position = 3,
};

struct MissionObjective {
    MissionObjectiveKind kind{};
    std::int16_t text_index{};
    std::uint16_t text_id{};
    std::array<std::int16_t, 4> parameters{};
    std::uint8_t parameter_count{};
};

// Actual opcode-12 argument layouts at0x57040..0x571a6. All four kinds found
// in the original30 streams are decoded; unknown kinds fail explicitly.
MissionObjective decode_mission_objective(const MissionCommand&);

// Indexed by original unit type, including inactive objects. The named-unit
// position predicate reads coordinates/offsets even if active is false; the
// wildcard predicate additionally requires active, exactly as in0x559b4.
struct MissionObjectiveUnit {
    bool present{true};
    bool active{};
    std::uint8_t owner{};
    std::uint8_t x{}, y{};
    std::int32_t offset_x{}, offset_y{};
};

struct MissionObjectiveView {
    const CampaignContext& campaign;
    const std::vector<MissionObjectiveUnit>& units_by_type;
    const std::vector<MissionSiteState>& sites;
    const std::vector<MissionCommand>& commands;
    std::size_t command_index{};
    std::uint8_t current_owner{};
};

struct MissionObjectiveDecision {
    bool resume_script{};
    // Cursor BEFORE the next phase-99 tick. It may be -1 when the original
    // reverse transition search returns to the first command.
    std::int32_t command_cursor{};
    bool set_alternate_dialogue_variant{}; // Original map+0x231 = true.
    bool reset_current_player_actions{}; // Actual0x55840(false), never an AI turn.
};

// Original0x559b4 plus success control flow0x558e4. Checks real active counts,
// type activity, owner lists, site states and centered coordinates. It does
// not move/capture/kill units, change turns, or accept a UI acknowledgement as
// an objective result. A successful decision requires the caller to perform
// action resets, clear objective-active, enter mode6, and set phase99.
MissionObjectiveDecision evaluate_mission_objective(const MissionObjective&,
                                                   const MissionObjectiveView&);

} // namespace g1
