#pragma once

#include "g1/UnitSpawn.hpp"

#include <array>
#include <optional>

namespace g1 {

// One native roster entry per original type, in stable session order. The full
// packed owner slots also include types not yet materialized in that roster.
struct UnitInteractionView {
    const std::vector<UnitDefinition>& definitions;
    const std::vector<CombatUnitState>& units;
    const std::vector<MovementUnitState>& movement_units;
    const std::array<std::vector<std::uint8_t>, 2>& owner_slot_types;
};

struct UnitStaticLinks {
    std::optional<std::uint8_t> field_10_type;
    std::optional<std::uint8_t> field_14_type;
};

// Original0x5417a..0x54238, after object creation/owner assignment. These are
// type links, not a mutable cargo inventory. Later arrival does not write them.
// The two fields deliberately retain their offsets: field10 means different
// things on the carrier and the special unit, and synthetic dual-role types
// can overwrite it in the original loop's order.
std::array<UnitStaticLinks, 23> derive_unit_static_links(
    const std::vector<UnitDefinition>&,
    const std::array<std::vector<std::uint8_t>, 2>& owner_slot_types);

enum class ProductionMode { ordinary, deploy };

// The original has two distinct confirmation paths. The button/input-mask
// path555b0 checks funds. The actual type-list callback35cfe ->461d2 instead
// resolves a type and enters placement through59e46 without that comparison.
// Both later commit through55f8c and debit/clamp funds through54016.
enum class ProductionConfirmation { funds_checked_button, direct_type_list };

// Original0x50116. Availability, inactive state and EITHER-form byte8==1
// classify the list. Funds do not filter it. Ascending original owner-slot
// order is retained, including inactive types absent from the native roster.
std::vector<std::uint8_t> production_candidates(const UnitInteractionView&,
    const std::array<bool,23>& available_types, std::uint8_t owner, ProductionMode);

struct ProductionPlacement {
    // Original range from0x4a678: budget1, cost mode0. Points/regions and actual
    // terrain costs other than99 are ignored. Occupancy affects vacancy only.
    MovementRange range;
    // Original human direction order: north, south, west, east (0x499c8).
    std::array<bool,4> reachable{};
    std::array<bool,4> vacant{};
    // The original initial cursor uses reachability, even on an occupied tile.
    std::optional<WorldCoordinate> initial_cursor;
};

ProductionPlacement production_placement(const MovementBoard&,
    WorldCoordinate origin, std::int8_t selected_form_field6);

enum class ProductionEligibility {
    allowed, inactive, action_used, wrong_form, no_candidates,
    no_vacant_destination, upgrade_has_priority,
};

// Original command-mask slice0x5300e ->0x50236/0x501f2. Includes the common
// active/acted guard and ordinary-production's earlier upgrade priority.
// Current-player, mission, moving/battle and UI authorization remain callers'.
ProductionEligibility production_eligibility(const UnitInteractionView&,
    const MovementBoard&, const std::array<bool,23>& available_types,
    std::size_t producer, ProductionMode, std::int32_t owner_funds);

// Original human menu0x555b0..0x555d4: signed funds >= signed map+14c[type].
// Type21's actual loader override is included by unit_spawn_cost.
bool production_affordable(const UnitDefinition&, std::int32_t owner_funds);

struct ProductionResult {
    UnitActivation activation;
    MovementUnitState producer_movement;
    std::int32_t funds_after{};
    WorldCoordinate return_cursor;
    // Full0x55f8c marks BOTH new unit and producer acted. The AI wrapper marks
    // only the new unit and must not use this human action as its wrapper.
    std::int32_t pending_type_after{-1};
    std::int32_t selection_mode_after{-1};
    std::array<std::uint8_t,4> command_flags_after{}; // map+c4..c7 cleared.
    std::int32_t audio_operation{5};
    std::int32_t audio_argument{16};
};

// Rechecks the live candidate and vacant adjacent destination, then
// composes original0x54016 with0x55f8c's human effects. Input state is immutable.
// The explicitly selected confirmation path determines the funds check above;
// the API has no default that could silently merge these different paths.
// Existing inactive state is preserved where the original activation leaves
// it untouched; an absent type uses original constructor defaults. Deploy
// follows the same reset/debit path; it does not restore a saved cargo object.
ProductionResult produce_unit(const UnitInteractionView&, const MovementBoard&,
    const std::array<bool,23>& available_types, std::size_t producer,
    ProductionMode, std::uint8_t new_type, WorldCoordinate destination,
    std::int32_t owner_funds, ProductionConfirmation);

enum class FriendlyDestinationAdmission {
    move, select_actor, already_moving, enemy_occupied,
    occupant_cannot_carry, actor_cannot_board, missing_route,
};

// Exact decision slice0x54ebe. The original looks up only active CENTERED
// occupants, owner0 then owner1 and ascending slot. The current form fields8/9
// here test !=0, unlike candidate/deploy classification's ==1. route_available
// means original first route word !=-1; it does not authorize a made-up route.
// The caller has already admitted an active actor/current-player move command.
FriendlyDestinationAdmission friendly_destination_admission(
    const UnitInteractionView&, std::size_t actor, WorldCoordinate destination,
    bool actor_moving, bool route_available);

struct TransportArrivalResult {
    CombatUnitState combat;
    MovementUnitState movement;
    std::uint8_t active_count_after{};
    std::int32_t upgrade_slot_after{-1};
    // One entry per matching active same-owner carrier, in owner-slot order.
    // The original loop has no break and repeats count/cache notifications.
    std::vector<std::uint8_t> matching_carrier_types;
    bool clear_status_cache{}; // map+1a4[type]=0 and its status string cleared.
    bool deactivate_renderer{};
};

// Original0x59884..0x5994a, after separately recovered arrival facing.
// Matching ignores carrier offsets, unlike destination lookup. Only the
// actor's active flag is changed; HP/form/actions/spent/route fields survive.
// Counts wrap as an original byte. Static links and carriers are not mutated.
// Caller separately performs later arrival phase/UI reset and notifications.
TransportArrivalResult apply_transport_arrival(const UnitInteractionView&,
    std::size_t actor, std::uint8_t owner_active_count, std::int32_t upgrade_slot);

} // namespace g1
