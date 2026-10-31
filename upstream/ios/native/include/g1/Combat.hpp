#pragma once

#include "g1/World.hpp"

#include <array>
#include <cstdint>
#include <optional>
#include <vector>

namespace g1 {

// Original shared game generator, recovered from 0x7acc/0x7b26. The caller
// supplies the seed and retains ONE stream across actions. There is no host
// random distribution, implicit reseed, clock access or original CPU here.
// This state is a native representation, not a decoded original save format.
class GameRandom {
public:
    struct State {
        std::array<std::uint32_t, 624> words{};
        std::uint32_t index{624};
        std::uint64_t draws{};
    };

    explicit GameRandom(std::uint32_t seed) noexcept;
    std::uint32_t next_word() noexcept;
    const State& state() const noexcept { return state_; }
    void restore(const State&); // Rejects index >624 without changing state.

private:
    State state_;
};

struct CombatStats {
    std::int16_t attack_low{};
    std::int16_t attack_high{};
    std::int16_t defense{};
};

// Original 0x4328a: signed form bytes3/4/5 plus level*(base/5), with signed
// division toward zero. The level input is a native byte, matching stored
// upgrade levels; the original routine's unrestricted register input is not
// part of this interface.
CombatStats combat_stats(const UnitDefinition&, std::uint8_t form,
                         std::uint8_t level);

struct CombatUnitState {
    std::uint8_t type{}, owner{}, faction{}, form{}, x{}, y{}, level{};
    std::uint8_t health{100}; // Original unit+0x5a.
    bool active{true};
    bool damage_immune{}; // Original unit+0x19 !=0, not an inferred ability name.
    std::uint8_t flag_38{}; // Attack eligibility gate, wider meaning unresolved.
    CombatStats stats;
    std::int32_t support_bonus{}; // Original unit+0x5c, prepared before exchange.
    std::int32_t timer_30_ms{-1};
    // Original4306c zeroes +34; the outer43f42 constructor separately sets
    // timer+30 to-1. Later activations retain this preset, including zero.
    std::int32_t preset_34_ms{};
};

// Reads existing decoded definitions. New placements start with health100,
// level0 and zero support. This does not infer saved/upgraded unit state.
CombatUnitState make_combat_unit(const WorldUnit&, const UnitDefinition&);

// Original 0x43184/0x43198. A negative new health clamps to0; otherwise the
// original stores one byte before choosing its countdown threshold. Immunity
// suppresses both HP and countdown writes. These do not deactivate a unit;
// destruction occurs in the map's exchange resolver after the HP test.
void set_combat_health(CombatUnitState&, std::int32_t value) noexcept;
void apply_combat_damage(CombatUnitState&, std::int32_t damage) noexcept;

// Borrowed immutable data must remain alive for a call. Unit positions and
// health are supplied separately, so no stale bootstrap placements are read.
struct CombatRulesView {
    const TerrainGrid& grid;
    const TerrainLookup& lookup;
    const TerrainRules& terrain_rules;
    const std::vector<UnitDefinition>& definitions; // Indexed by original type.
    bool phase_flag_227{}; // Original map+0x227, not guessed from mission phase.
};

struct CombatBonuses {
    std::int32_t attack{};
    std::int32_t defense{};
};

// Original 0x438c2/0x4383a: signed scalar3/4 type modifier (selected by the
// target's form field6==1), nearby same-owner form-field11 aura (+15), and
// terrain-rule field1 at the defender tile. No line of sight is added.
CombatBonuses combat_bonuses(const CombatRulesView&,
                            const std::vector<CombatUnitState>&,
                            std::size_t attacker, std::size_t defender);

// Membership predicate recovered from 0x52fdc ->0x52f84 ->0x43976 ->0x4926a.
// Includes signed form-field3 !=0, scaled attack_low !=0, flag38, faction/227,
// enemy owner, active target, and inclusive Manhattan form-byte1/2 range.
// It does not decide UI selection, turn ownership or whether an action is spent.
bool combat_can_target(const CombatRulesView&,
                       const std::vector<CombatUnitState>&,
                       std::size_t attacker, std::size_t defender);

struct CombatStrike {
    std::int32_t rolled_damage{}; // Returned even when target is immune.
    std::uint8_t health_before{}, health_after{};
    bool evaded{}; // Original type10 extra random branch.
    bool immune{};
    std::uint32_t random_words{};
};

// Exact arithmetic slice 0x43bea; caller supplies recovered world bonuses.
// This operation alone does not authorize an attack or mark destruction.
CombatStrike resolve_combat_strike(const CombatUnitState& attacker,
                                   CombatUnitState& defender,
                                   CombatBonuses, GameRandom&);

struct CombatDestruction {
    std::uint8_t type{}, owner{}, x{}, y{};
};

struct CombatExchange {
    CombatStrike attack;
    std::optional<CombatStrike> counterattack;
    bool counter_eligible_before_attack{};
    // Original support list order: attacker's helpers, then defender's helpers;
    // ascending original type within each owner list. At most6 original slots.
    std::vector<std::uint8_t> support_types;
    // The same ordered helpers as indices into the combat/session roster.
    // Captured before damage so native presentation never guesses a helper
    // from its type or recalculates support against the post-combat state.
    std::vector<std::size_t> support_units;
    std::vector<CombatDestruction> destroyed;
    std::uint32_t random_words{};
};

// Native reconstruction of the support preparation in0x585c4 and the health
// transition prefix0x58172..0x582ec. Requires the caller to have authorized the
// action; validates distinct active enemy combatants and bounded source data.
// Does not substitute a different route/RNG when input is unsupported.
//
// Support draws precede damage draws, even when no helper contributes. A ranged
// form0 attacker farther than1 tile suppresses the counter; otherwise original
// target eligibility applies. A surviving defender attacks using reduced HP.
// Death sets active=false and emits a destruction record. Caller MUST apply
// the associated world/occupancy, turn-selection, objective, UI and statistics
// cleanup. Battle cinematics, action-spent flags and mission advancement are
// outside this core; a returned result does not mean a mission was completed.
// Validation/unsupported-storage errors leave units and random state unchanged.
CombatExchange resolve_combat_exchange(const CombatRulesView&,
                                       std::vector<CombatUnitState>&,
                                       std::size_t attacker,
                                       std::size_t defender, GameRandom&);

} // namespace g1
