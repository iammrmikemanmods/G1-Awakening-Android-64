#pragma once

#include "g1/Combat.hpp"
#include "g1/MovementAction.hpp"

namespace g1 {

struct UpgradeWorldView {
    const std::vector<UnitDefinition>& definitions;
    const std::vector<CombatUnitState>& units;
    const std::vector<MovementUnitState>& movement_units;
    // Full packed original owner slots, including inactive/unspawned types.
    const std::array<std::vector<std::uint8_t>, 2>& owner_slot_types;
};

enum class UpgradeEligibility {
    allowed, inactive, action_used, producer_type, level_limit, insufficient_funds,
};

// Original 0x4ec0e. Funds belong to the actor's owner. Current-player command
// authorization remains the caller's responsibility; map+0x34 is an AI planner
// selection and is not an extra human upgrade eligibility condition.
UpgradeEligibility upgrade_eligibility(const UpgradeWorldView&, std::size_t actor,
                                       std::int32_t owner_funds);

struct UpgradeResult {
    CombatUnitState combat;
    MovementUnitState movement;
    std::int32_t funds_after{};
    std::int32_t signed_cost{};
};

// Authorized ordinary upgrade: 0x43f2c plus the fund writes in0x556a8.
// Rejects an ineligible action before changing any caller state. Renderer,
// strings and current-player slot advancement remain explicit caller work.
UpgradeResult apply_upgrade(const UpgradeWorldView&, std::size_t actor,
                            std::int32_t owner_funds);

// Opcode6's post-spawn scan0x567f6: first active, form0 field12!=1 slot.
// If no candidate exists the original leaves the stored slot unchanged.
// A freshly loaded map initializes the supplied stored value to-1.
std::int32_t first_upgrade_slot(const UpgradeWorldView&, std::uint8_t owner,
                                std::int32_t stored_slot);

// Original0x556a8 advances CURRENT player's slot after upgrade. Eligibility for
// slot selection checks only active and form0 field12!=1, not level/funds/acted.
// An arrangement with no candidate is rejected instead of looping forever.
// Stored slot22 is unsupported here: the original initially reads slot23 before
// testing its wrap. Native code does not reproduce that out-of-bounds read.
std::int32_t advance_upgrade_slot(const UpgradeWorldView&, std::uint8_t owner,
                                  std::int32_t stored_slot);

// Original0x43ac8 cycles only if the removed unit held the stored slot. It treats
// that unit as inactive; when a full cycle finds no replacement it retains the
// removed slot. This does not perform other death/count/renderer bookkeeping.
std::int32_t upgrade_slot_after_removal(const UpgradeWorldView&, std::uint8_t owner,
                                       std::uint8_t removed_type,
                                       std::int32_t stored_slot);

} // namespace g1
