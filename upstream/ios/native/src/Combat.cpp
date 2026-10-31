#include "g1/Combat.hpp"

#include <algorithm>
#include <limits>
#include <stdexcept>

namespace g1 {
namespace {

std::int32_t signed32(std::uint32_t value) noexcept {
    if (value <= 0x7fffffffU) return static_cast<std::int32_t>(value);
    return -1 - static_cast<std::int32_t>(0xffffffffU - value);
}

std::int32_t signed8(std::uint8_t value) noexcept {
    return value < 128 ? value : static_cast<std::int32_t>(value) - 256;
}

std::int16_t signed16(std::uint32_t value) noexcept {
    value &= 0xffffU;
    return static_cast<std::int16_t>(value < 0x8000U
        ? static_cast<std::int32_t>(value)
        : static_cast<std::int32_t>(value) - 65536);
}

std::int32_t add32(std::int32_t a, std::int32_t b) noexcept {
    return signed32(static_cast<std::uint32_t>(a) + static_cast<std::uint32_t>(b));
}

std::int32_t sub32(std::int32_t a, std::int32_t b) noexcept {
    return signed32(static_cast<std::uint32_t>(a) - static_cast<std::uint32_t>(b));
}

std::int32_t abs32(std::int32_t value) noexcept {
    return value < 0 ? signed32(0U - static_cast<std::uint32_t>(value)) : value;
}

std::int32_t asr16(std::int32_t value) noexcept {
    return value >= 0 ? value / 65536 :
        -1 - static_cast<std::int32_t>((-1 - static_cast<std::int64_t>(value)) / 65536);
}

std::int32_t draw_span(const CombatUnitState& unit, GameRandom& random) noexcept {
    const std::int32_t span = static_cast<std::int32_t>(unit.stats.attack_high) -
                              unit.stats.attack_low;
    if (span == 0) return 0;
    // Original ___modsi3 consumes a SIGNED generator word. int64_t avoids
    // C++'s undefined INT_MIN % -1 while preserving the original zero result.
    return static_cast<std::int32_t>(
        static_cast<std::int64_t>(signed32(random.next_word())) % span);
}

const Bytes& form_data(const UnitDefinition& definition, std::uint8_t form) {
    if (form >= definition.forms.size() || definition.forms[form].size() != 14)
        throw std::invalid_argument("combat requires a decoded 14-byte unit form");
    return definition.forms[form];
}

const UnitDefinition& definition(const CombatRulesView& rules,
                                 const CombatUnitState& unit) {
    return rules.definitions[unit.type];
}

const Bytes& form_data(const CombatRulesView& rules, const CombatUnitState& unit) {
    return rules.definitions[unit.type].forms[unit.form];
}

int distance(const CombatUnitState& a, const CombatUnitState& b) noexcept {
    return std::abs(static_cast<int>(a.x) - b.x) +
           std::abs(static_cast<int>(a.y) - b.y);
}

void validate(const CombatRulesView& rules, const std::vector<CombatUnitState>& units,
              std::size_t attacker, std::size_t defender) {
    if (!rules.grid.width || !rules.grid.height || rules.grid.width > 255 ||
        rules.grid.height > 255 || rules.grid.tiles.size() !=
            static_cast<std::size_t>(rules.grid.width) * rules.grid.height)
        throw std::invalid_argument("combat grid dimensions/span are invalid");
    if (rules.definitions.size() != 23 || units.size() > 23 ||
        attacker >= units.size() || defender >= units.size() || attacker == defender)
        throw std::invalid_argument("combat requires distinct bounded unit indices and 23 definitions");
    for (std::size_t i = 0; i != rules.definitions.size(); ++i) {
        const auto& d = rules.definitions[i];
        if (d.type != i || d.forms.size() != 2)
            throw std::invalid_argument("combat definition order/form count differs from source variant");
        form_data(d, 0);
        form_data(d, 1);
    }
    for (const auto category : rules.lookup)
        if (category >= rules.terrain_rules.size())
            throw std::invalid_argument("combat terrain category is outside decoded rules");
    for (const auto tile : rules.grid.tiles)
        if (tile >= rules.lookup.size())
            throw std::invalid_argument("combat terrain cell has no recovered category");
    std::array<bool, 23> seen{};
    for (const auto& unit : units) {
        if (unit.type >= seen.size() || seen[unit.type] || unit.owner > 1 ||
            unit.form > 1 || (unit.active &&
                (unit.x >= rules.grid.width || unit.y >= rules.grid.height)))
            throw std::invalid_argument("combat unit type/owner/form/active coordinates are invalid");
        seen[unit.type] = true;
    }
}

bool aura(const CombatRulesView& rules, const std::vector<CombatUnitState>& units,
          std::size_t subject) {
    const auto& unit = units[subject];
    for (std::size_t i = 0; i != units.size(); ++i) {
        const auto& other = units[i];
        if (i == subject || !other.active || other.owner != unit.owner ||
            form_data(rules, other)[11] == 0) continue;
        if (std::abs(static_cast<int>(other.x) - unit.x) <= 1 &&
            std::abs(static_cast<int>(other.y) - unit.y) <= 1) return true;
    }
    return false;
}

CombatBonuses bonuses(const CombatRulesView& rules,
                      const std::vector<CombatUnitState>& units,
                      std::size_t attacker, std::size_t defender) {
    const auto& a = units[attacker];
    const auto& d = units[defender];
    const auto modifier_field = form_data(rules, d)[6] == 1 ? 4U : 3U;
    const auto tile = rules.grid.tiles[static_cast<std::size_t>(d.y) * rules.grid.width + d.x];
    const auto category = rules.lookup[tile];
    return {signed8(definition(rules, a).scalar_fields[modifier_field]) +
                (aura(rules, units, attacker) ? 15 : 0),
            rules.terrain_rules[category].fields[1] +
                (aura(rules, units, defender) ? 15 : 0)};
}

bool can_target(const CombatRulesView& rules,
                const std::vector<CombatUnitState>& units,
                std::size_t attacker, std::size_t defender) {
    const auto& a = units[attacker];
    const auto& d = units[defender];
    const auto& form = form_data(rules, a);
    if (a.flag_38 != 0 || signed8(form[3]) == 0 || a.stats.attack_low == 0 ||
        (a.faction != 1 && rules.phase_flag_227) || !d.active || a.owner == d.owner)
        return false;
    const int range = distance(a, d);
    return range >= form[1] && range <= form[2];
}

bool counter_allowed(const CombatRulesView& rules,
                     const std::vector<CombatUnitState>& units,
                     std::size_t attacker, std::size_t defender) {
    if (signed8(definition(rules, units[attacker]).forms[0][2]) > 1 &&
        distance(units[attacker], units[defender]) > 1) return false;
    return can_target(rules, units, defender, attacker);
}

std::vector<std::size_t> helpers(const CombatRulesView& rules,
                                  const std::vector<CombatUnitState>& units,
                                  std::size_t attacker, std::size_t defender) {
    std::vector<std::size_t> result;
    if (distance(units[attacker], units[defender]) != 1) return result;
    for (std::size_t i = 0; i != units.size(); ++i) {
        const auto& other = units[i];
        if (i == attacker || !other.active || other.owner == units[defender].owner ||
            distance(other, units[defender]) != 1) continue;
        const auto& form = form_data(rules, other);
        if (signed8(form[3]) > 0 && signed8(form[1]) == 1)
            result.push_back(i);
    }
    std::sort(result.begin(),result.end(),[&](std::size_t a,std::size_t b) {
        return units[a].type<units[b].type;
    });
    return result;
}

void prepare_support(CombatUnitState& attacker, std::int32_t attack_bonus,
                     std::size_t count, GameRandom& random) noexcept {
    const auto rolled = draw_span(attacker, random); // Draw even if count==0.
    attacker.support_bonus = 0;
    if (count == 0) return;
    const auto base = add32(add32(attacker.stats.attack_low, attack_bonus), abs32(rolled));
    const auto fixed = signed32(static_cast<std::uint32_t>(base) << 16U);
    const auto fraction = static_cast<std::int32_t>(count * 16384U);
    // Original 0x2b644 signed 64-bit multiplication, low32(product>>16), then
    // caller arithmetic shift16. Keep wrapping behavior without signed UB.
    const auto product = static_cast<std::uint64_t>(static_cast<std::int64_t>(fixed) * fraction);
    attacker.support_bonus = asr16(signed32(static_cast<std::uint32_t>(product >> 16U)));
}

} // namespace

GameRandom::GameRandom(std::uint32_t seed) noexcept {
    state_.words[0] = seed;
    for (std::uint32_t i = 1; i != state_.words.size(); ++i)
        state_.words[i] = 1812433253U *
            (state_.words[i - 1] ^ (state_.words[i - 1] >> 30U)) + i;
}

std::uint32_t GameRandom::next_word() noexcept {
    auto& words = state_.words;
    if (state_.index == words.size()) {
        for (std::size_t i = 0; i < 227; ++i) {
            const auto y = (words[i] & 0x80000000U) | (words[i + 1] & 0x7fffffffU);
            words[i] = words[i + 397] ^ (y >> 1U) ^ ((y & 1U) ? 0x9908b0dfU : 0U);
        }
        for (std::size_t i = 227; i < 623; ++i) {
            const auto y = (words[i] & 0x80000000U) | (words[i + 1] & 0x7fffffffU);
            words[i] = words[i - 227] ^ (y >> 1U) ^ ((y & 1U) ? 0x9908b0dfU : 0U);
        }
        const auto y = (words[623] & 0x80000000U) | (words[0] & 0x7fffffffU);
        words[623] = words[396] ^ (y >> 1U) ^ ((y & 1U) ? 0x9908b0dfU : 0U);
        state_.index = 0;
    }
    auto y = words[state_.index++];
    y ^= y >> 11U;
    y ^= (y << 7U) & 0x9d2c5680U;
    y ^= (y << 15U) & 0xefc60000U;
    y ^= y >> 18U;
    ++state_.draws;
    return y;
}

void GameRandom::restore(const State& state) {
    if (state.index > state.words.size())
        throw std::invalid_argument("game random index is outside 624-word state");
    state_ = state;
}

CombatStats combat_stats(const UnitDefinition& definition, std::uint8_t form,
                         std::uint8_t level) {
    const auto& data = form_data(definition, form);
    const auto scale = [level](std::uint8_t value) {
        const auto base = signed8(value);
        return signed16(static_cast<std::uint32_t>(base + level * (base / 5)));
    };
    return {scale(data[3]), scale(data[4]), scale(data[5])};
}

CombatUnitState make_combat_unit(const WorldUnit& unit, const UnitDefinition& d) {
    if (unit.type != d.type) throw std::invalid_argument("combat unit definition type mismatch");
    CombatUnitState result;
    result.type = unit.type;
    result.owner = unit.owner;
    result.faction = unit.faction;
    result.form = unit.form;
    result.x = unit.x;
    result.y = unit.y;
    result.active = unit.active;
    result.stats = combat_stats(d, unit.form, 0);
    return result;
}

void set_combat_health(CombatUnitState& unit, std::int32_t value) noexcept {
    unit.health = value < 0 ? 0 : static_cast<std::uint8_t>(value);
    const auto hp = unit.health;
    unit.preset_34_ms = hp <= 9 ? 750 : hp <= 24 ? 1500 : hp <= 49 ? 2500 :
                        hp <= 74 ? 5000 : -1;
    unit.timer_30_ms = unit.preset_34_ms;
}

void apply_combat_damage(CombatUnitState& unit, std::int32_t damage) noexcept {
    if (!unit.damage_immune) set_combat_health(unit, sub32(unit.health, damage));
}

CombatBonuses combat_bonuses(const CombatRulesView& rules,
                            const std::vector<CombatUnitState>& units,
                            std::size_t attacker, std::size_t defender) {
    validate(rules, units, attacker, defender);
    if (!units[attacker].active || !units[defender].active)
        throw std::invalid_argument("combat bonuses require active combatants");
    return bonuses(rules, units, attacker, defender);
}

bool combat_can_target(const CombatRulesView& rules,
                       const std::vector<CombatUnitState>& units,
                       std::size_t attacker, std::size_t defender) {
    validate(rules, units, attacker, defender);
    return can_target(rules, units, attacker, defender);
}

CombatStrike resolve_combat_strike(const CombatUnitState& attacker,
                                   CombatUnitState& defender,
                                   CombatBonuses modifiers, GameRandom& random) {
    const auto draws_before = random.state().draws;
    CombatStrike result;
    result.health_before = defender.health;
    result.immune = defender.damage_immune;
    const auto variation = abs32(draw_span(attacker, random));
    auto raw = add32(attacker.stats.attack_low, attacker.support_bonus);
    raw = sub32(raw, defender.stats.defense);
    raw = add32(raw, modifiers.attack);
    raw = add32(raw, variation);
    raw = sub32(raw, modifiers.defense);
    const auto weighted = signed32(static_cast<std::uint32_t>(raw) * attacker.health);
    auto damage = std::max<std::int32_t>(5, weighted / 100);
    if (defender.type == 10) {
        const auto evade = static_cast<std::int64_t>(abs32(signed32(random.next_word()))) % 100;
        if (evade > 49) {
            damage = 0;
            result.evaded = true;
        }
    }
    result.rolled_damage = damage;
    apply_combat_damage(defender, damage);
    result.health_after = defender.health;
    result.random_words = static_cast<std::uint32_t>(random.state().draws - draws_before);
    return result;
}

CombatExchange resolve_combat_exchange(const CombatRulesView& rules,
                                       std::vector<CombatUnitState>& units,
                                       std::size_t attacker, std::size_t defender,
                                       GameRandom& random) {
    validate(rules, units, attacker, defender);
    if (!units[attacker].active || !units[defender].active ||
        units[attacker].owner == units[defender].owner)
        throw std::invalid_argument("combat exchange requires active enemy units");
    const bool can_counter = counter_allowed(rules, units, attacker, defender);
    auto attack_helpers = helpers(rules, units, attacker, defender);
    auto counter_helpers = can_counter ? helpers(rules, units, defender, attacker) :
                                         std::vector<std::size_t>{};
    if (attack_helpers.size() + counter_helpers.size() > 6)
        throw std::invalid_argument("combat support exceeds the original six-slot storage");
    CombatExchange result;
    result.counter_eligible_before_attack = can_counter;
    result.support_units=attack_helpers;
    result.support_units.insert(result.support_units.end(),counter_helpers.begin(),counter_helpers.end());
    result.support_types.reserve(result.support_units.size());
    for(const auto index:result.support_units)result.support_types.push_back(units[index].type);
    auto updated = units;
    auto next_random = random;
    const auto draw_begin = next_random.state().draws;
    prepare_support(updated[attacker], bonuses(rules, updated, attacker, defender).attack,
                    attack_helpers.size(), next_random);
    if (can_counter)
        prepare_support(updated[defender], bonuses(rules, updated, defender, attacker).attack,
                        counter_helpers.size(), next_random);
    result.attack = resolve_combat_strike(updated[attacker], updated[defender],
        bonuses(rules, updated, attacker, defender), next_random);
    const auto record_death = [&](std::size_t index) {
        auto& unit = updated[index];
        unit.active = false;
        result.destroyed.push_back({unit.type, unit.owner, unit.x, unit.y});
    };
    if (updated[defender].health == 0) {
        record_death(defender);
    } else if (can_counter) {
        result.counterattack = resolve_combat_strike(updated[defender], updated[attacker],
            bonuses(rules, updated, defender, attacker), next_random);
        if (updated[attacker].health == 0) record_death(attacker);
    }
    result.random_words = static_cast<std::uint32_t>(next_random.state().draws - draw_begin);
    units = std::move(updated);
    random = next_random;
    return result;
}

} // namespace g1
