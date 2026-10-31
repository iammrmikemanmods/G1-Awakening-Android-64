#include "g1/AI.hpp"
#include "g1/AIIncome.hpp"

#include <algorithm>
#include <numeric>
#include <stdexcept>

namespace g1 {
namespace {
std::int8_t signed_byte(std::uint8_t value) noexcept {
    return static_cast<std::int8_t>(value<128U ? int(value) : int(value)-256);
}

void validate_site_view(const AIWorldView& world,std::size_t actor) {
    if (actor>=world.units.size() || world.units.size()>23 ||
        world.movement_units.size()!=world.units.size() || world.current_player>1 ||
        world.combat.definitions.size()!=23 || world.movement.width==0 ||
        world.movement.height==0 || world.movement.width>255 || world.movement.height>255 ||
        world.movement.cells.size()!=std::size_t(world.movement.width)*world.movement.height ||
        world.combat.grid.width!=world.movement.width || world.combat.grid.height!=world.movement.height)
        throw std::invalid_argument("AI capture input shape is invalid");
    std::array<bool,23> seen{};
    for (std::size_t i=0;i<world.units.size();++i) {
        const auto& unit=world.units[i];
        const auto& motion=world.movement_units[i];
        if (unit.type>=23 || seen[unit.type] || unit.owner>1 || unit.form>1 ||
            (unit.active && (unit.x>=world.movement.width || unit.y>=world.movement.height)) ||
            motion.active!=unit.active || motion.form!=unit.form ||
            motion.tile_x!=unit.x || motion.tile_y!=unit.y)
            throw std::invalid_argument("AI capture unit state is invalid or unsynchronized");
        const auto& definition=world.combat.definitions[unit.type];
        if (definition.type!=unit.type || definition.forms.size()!=2 ||
            definition.forms[0].size()!=14 || definition.forms[1].size()!=14)
            throw std::invalid_argument("AI capture definition is malformed");
        seen[unit.type]=true;
    }
    if (world.units[actor].x>=world.movement.width || world.units[actor].y>=world.movement.height)
        throw std::invalid_argument("AI capture actor is outside map");
    (void)capture_availability(world.sites,world.captures,{},world.units[actor].owner);
    // The original scratch candidate count is one byte. Actual mission data
    // fits this bound; reject a larger synthetic shape rather than inventing
    // behavior for its wrapping storage cursor.
    std::size_t items=0;
    for (const auto& site:world.sites)
        for (const auto& group:site.coordinate_groups) items+=group.size();
    if (world.sites.size()>255 || items>255)
        throw std::invalid_argument("AI capture candidates exceed original storage");
}
} // namespace

std::optional<AIPlan> choose_ai_capture_plan(const AIWorldView& world,std::size_t actor) {
    validate_site_view(world,actor);
    const auto& unit=world.units[actor];
    const auto& definition=world.combat.definitions[unit.type];
    auto capture_form=unit.form;
    // Original 0x508a2 tests nonzero, whereas the eventual capture command
    // specifically requires 1. Preserve those two different predicates.
    if (definition.forms[capture_form][10]==0) {
        capture_form=static_cast<std::uint8_t>(1-capture_form);
        if (definition.forms[capture_form][10]==0) return {};
    }
    const auto& form=definition.forms[unit.form];
    const MovementProfile profile{unit.type,unit.owner,form[0],
        world.movement_units[actor].spent_budget,signed_byte(form[6])};
    const auto range=compute_movement_range(world.movement,profile,unit.x,unit.y);
    std::vector<std::size_t> order(world.units.size());
    std::iota(order.begin(),order.end(),0);
    std::sort(order.begin(),order.end(),[&](std::size_t a,std::size_t b) {
        return world.units[a].type<world.units[b].type;
    });
    const auto eligible=[&](WorldCoordinate p) {
        // Site coordinate vectors are read with signed byte loads.
        if (p.x>=128 || p.y>=128 || p.x>=range.width || p.y>=range.height) return false;
        const auto value=range.at(p.x,p.y);
        if (value<0 || value==movement_reserved_cell) return false;
        for (std::uint8_t owner=0;owner<2;++owner)
            for (auto i:order) {
                const auto& other=world.units[i];
                const auto& motion=world.movement_units[i];
                if (other.active && other.owner==owner && other.x==p.x && other.y==p.y &&
                    motion.offset_x==0 && motion.offset_y==0) return i==actor;
            }
        return true;
    };
    const auto own=world.captures.counters_68[unit.owner];
    const auto other=world.captures.counters_68[unit.owner^1U];
    const auto make_plan=[&](CaptureTarget target,WorldCoordinate position) {
        AIPlan result;
        // The original helper receives map.current_player, but these two
        // explicit baseline counters were read using the actor's owner.
        result.score=ai_capture_income_potential(world.captures,world.player_factions,
            world.current_player,target,own,other);
        result.action=2;
        result.target_type=-1;
        result.destination=position;
        result.site=static_cast<std::int32_t>(target.site);
        result.group=target.kind==CaptureTargetKind::site ? -1 : static_cast<std::int32_t>(target.group);
        result.item=target.kind==CaptureTargetKind::site ? -1 : static_cast<std::int32_t>(target.item);
        result.movement_form=unit.form;
        result.transform_on_arrival=unit.form!=capture_form;
        return result;
    };
    std::optional<AIPlan> best_site,best_item;
    for (std::size_t s=0;s<world.sites.size();++s) {
        const auto& site=world.sites[s];
        if (world.captures.sites[s].state!=unit.owner && eligible(site.position)) {
            const auto plan=make_plan({CaptureTargetKind::site,s,0,0},site.position);
            if (!best_site || plan.score>best_site->score) best_site=plan;
        }
        for (std::size_t g=0;g<site.coordinate_groups.size();++g)
            for (std::size_t i=0;i<site.coordinate_groups[g].size();++i) {
                const auto position=site.coordinate_groups[g][i];
                if (world.captures.sites[s].item_states[g][i]==unit.owner || !eligible(position)) continue;
                const auto plan=make_plan({CaptureTargetKind::item,s,g,i},position);
                if (!best_item || plan.score>best_item->score) best_item=plan;
            }
    }
    // Stored order wins ties inside each class; a site wins an item tie.
    if (best_site && (!best_item || best_site->score>=best_item->score)) return best_site;
    return best_item;
}
} // namespace g1
