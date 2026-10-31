#include "g1/UpgradeAction.hpp"

#include <algorithm>
#include <stdexcept>

namespace g1 {
namespace {

int signed_byte(std::uint8_t value) noexcept {
    return value<128 ? value : int(value)-256;
}
std::int32_t signed_word(std::uint32_t value) noexcept {
    return value<=0x7fffffffU ? static_cast<std::int32_t>(value)
                            : -1-static_cast<std::int32_t>(~value);
}

std::array<std::optional<std::size_t>,23> validate(const UpgradeWorldView& world) {
    if (world.definitions.size()!=23 || world.units.size()>23 ||
        world.movement_units.size()!=world.units.size())
        throw std::invalid_argument("Upgrade roster shape is invalid");
    for (std::size_t t=0;t<23;++t) {
        const auto& def=world.definitions[t];
        if (def.type!=t || def.forms.size()!=2 || def.forms[0].size()!=14 || def.forms[1].size()!=14)
            throw std::invalid_argument("Upgrade definitions are not the recovered two-form variant");
    }
    std::array<std::optional<std::size_t>,23> by_type{};
    for (std::size_t i=0;i<world.units.size();++i) {
        const auto& u=world.units[i];const auto& m=world.movement_units[i];
        if (u.type>=23 || by_type[u.type] || u.owner>1 || u.form>1 ||
            m.active!=u.active || m.form!=u.form || m.tile_x!=u.x || m.tile_y!=u.y)
            throw std::invalid_argument("Upgrade unit state is invalid or unsynchronized");
        by_type[u.type]=i;
    }
    for (std::size_t owner=0;owner<2;++owner) {
        const auto& slots=world.owner_slot_types[owner];
        if (slots.size()>23) throw std::invalid_argument("Upgrade owner slots exceed original capacity");
        for (std::size_t i=0;i<slots.size();++i)
            if (slots[i]>=23 || (i && slots[i]<=slots[i-1]))
                throw std::invalid_argument("Upgrade owner slots must be ascending unique types");
        for (const auto& unit:world.units)
            if (unit.active && unit.owner==owner &&
                std::find(slots.begin(),slots.end(),unit.type)==slots.end())
                throw std::invalid_argument("Upgrade owner slots omit an active unit");
    }
    return by_type;
}

void selection(const UpgradeWorldView& world,std::uint8_t owner,std::int32_t stored) {
    if (owner>1 || stored < -1 || stored>=23 ||
        (stored>=0 && static_cast<std::size_t>(stored)>=world.owner_slot_types[owner].size()))
        throw std::invalid_argument("Upgrade owner or stored slot is invalid");
}

bool candidate(const UpgradeWorldView& world,
               const std::array<std::optional<std::size_t>,23>& by_type,
               std::uint8_t owner,std::int32_t slot,
               std::optional<std::uint8_t> removed={}) {
    const auto& slots=world.owner_slot_types[owner];
    if (slot<0 || static_cast<std::size_t>(slot)>=slots.size()) return false;
    const auto type=slots[static_cast<std::size_t>(slot)];
    if (removed && type==*removed) return false;
    return by_type[type] && world.units[*by_type[type]].active &&
           signed_byte(world.definitions[type].forms[0][12])!=1;
}

UpgradeEligibility eligibility(const UpgradeWorldView& world,std::size_t actor,
                               std::int32_t funds) {
    if (actor>=world.units.size()) throw std::invalid_argument("Upgrade actor is absent");
    const auto& unit=world.units[actor];
    const auto& definition=world.definitions[unit.type];
    if (!unit.active) return UpgradeEligibility::inactive;
    if (world.movement_units[actor].flag_2c!=0) return UpgradeEligibility::action_used;
    if (signed_byte(definition.forms[0][12])==1) return UpgradeEligibility::producer_type;
    if (unit.level>24) return UpgradeEligibility::level_limit;
    if (funds<unit_spawn_cost(definition)) return UpgradeEligibility::insufficient_funds;
    return UpgradeEligibility::allowed;
}

} // namespace

UpgradeEligibility upgrade_eligibility(const UpgradeWorldView& world,std::size_t actor,
                                       std::int32_t funds) {
    (void)validate(world);
    return eligibility(world,actor,funds);
}

UpgradeResult apply_upgrade(const UpgradeWorldView& world,std::size_t actor,
                            std::int32_t funds) {
    (void)validate(world);
    if (eligibility(world,actor,funds)!=UpgradeEligibility::allowed)
        throw std::invalid_argument("Upgrade action is not eligible");
    UpgradeResult result{world.units[actor],world.movement_units[actor],funds,0};
    const auto& definition=world.definitions[result.combat.type];
    ++result.combat.level;
    result.combat.stats=combat_stats(definition,result.combat.form,result.combat.level);
    result.movement.flag_2c=1;
    result.signed_cost=unit_spawn_cost(definition);
    result.funds_after=signed_word(static_cast<std::uint32_t>(funds)-
                                  static_cast<std::uint32_t>(result.signed_cost));
    if (result.funds_after<0) result.funds_after=0;
    return result;
}

std::int32_t first_upgrade_slot(const UpgradeWorldView& world,std::uint8_t owner,
                                std::int32_t stored) {
    const auto by_type=validate(world);selection(world,owner,stored);
    for (std::int32_t slot=0;slot<23;++slot)
        if (candidate(world,by_type,owner,slot)) return slot;
    return stored;
}

std::int32_t advance_upgrade_slot(const UpgradeWorldView& world,std::uint8_t owner,
                                  std::int32_t stored) {
    const auto by_type=validate(world);selection(world,owner,stored);
    if (stored==22)
        throw std::invalid_argument("Original upgrade first increments slot22 past its23-slot allocation");
    auto slot=stored;
    for (int count=0;count<23;++count) {
        slot=(slot+1)%23;
        if (candidate(world,by_type,owner,slot)) return slot;
    }
    throw std::invalid_argument("Original upgrade slot cycle has no eligible active unit");
}

std::int32_t upgrade_slot_after_removal(const UpgradeWorldView& world,std::uint8_t owner,
                                       std::uint8_t removed,std::int32_t stored) {
    const auto by_type=validate(world);selection(world,owner,stored);
    const auto& slots=world.owner_slot_types[owner];
    const auto found=std::find(slots.begin(),slots.end(),removed);
    if (removed>=23 || found==slots.end())
        throw std::invalid_argument("Removed unit has no original owner slot");
    if (stored==-1 || stored!=found-slots.begin()) return stored;
    auto slot=stored;
    for (int count=0;count<23;++count) {
        if (candidate(world,by_type,owner,slot,removed)) return slot;
        slot=(slot+1)%23;
        if (slot==stored) break;
    }
    return stored;
}

} // namespace g1
