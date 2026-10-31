#include "g1/AI.hpp"
#include "g1/AIIncome.hpp"

#include <algorithm>
#include <limits>
#include <numeric>
#include <stdexcept>

namespace g1 {
namespace {

int sbyte(std::uint8_t value) noexcept {
    return value < 128 ? value : static_cast<int>(value)-256;
}
std::int32_t signed_bits(std::uint32_t value) noexcept {
    return value <= 0x7fffffffU ? static_cast<std::int32_t>(value)
        : -1-static_cast<std::int32_t>(~value);
}
std::int32_t add(std::int32_t a, std::int32_t b) noexcept {
    return signed_bits(static_cast<std::uint32_t>(a)+static_cast<std::uint32_t>(b));
}
std::int32_t sub(std::int32_t a, std::int32_t b) noexcept {
    return signed_bits(static_cast<std::uint32_t>(a)-static_cast<std::uint32_t>(b));
}
const Bytes& form(const AIWorldView& world, std::size_t unit, std::uint8_t which) {
    return world.combat.definitions.at(world.units.at(unit).type).forms.at(which);
}

void validate_common(const AIWorldView& world) {
    if (world.units.size() > 23 ||
        world.movement_units.size() != world.units.size() || world.current_player > 1)
        throw std::invalid_argument("AI unit/player shape is invalid");
    if (world.combat.definitions.size() != 23 || world.combat.grid.width == 0 ||
        world.combat.grid.height == 0 || world.combat.grid.width > 255 ||
        world.combat.grid.height > 255 ||
        world.combat.grid.tiles.size() != std::size_t(world.combat.grid.width)*world.combat.grid.height ||
        world.movement.width != world.combat.grid.width ||
        world.movement.height != world.combat.grid.height ||
        world.movement.cells.size() != world.combat.grid.tiles.size())
        throw std::invalid_argument("AI map/definition shape is invalid");
    for (std::size_t t=0; t<23; ++t) {
        const auto& def=world.combat.definitions[t];
        if (def.type != t || def.forms.size() != 2 ||
            def.forms[0].size() != 14 || def.forms[1].size() != 14)
            throw std::invalid_argument("AI unit definition is not the recovered two-form variant");
    }
    std::array<bool,23> seen{};
    for (std::size_t i=0; i<world.units.size(); ++i) {
        const auto& u=world.units[i];
        const auto& m=world.movement_units[i];
        if (u.type >= 23 || seen[u.type] || u.owner > 1 || u.form > 1 ||
            (u.active && (u.x >= world.movement.width || u.y >= world.movement.height)) ||
            m.active != u.active || m.form != u.form || m.tile_x != u.x || m.tile_y != u.y)
            throw std::invalid_argument("AI unit state is invalid or unsynchronized");
        seen[u.type]=true;
    }
    for (std::size_t i=0; i<world.combat.grid.tiles.size(); ++i) {
        const auto tile=world.combat.grid.tiles[i];
        if (tile >= world.combat.lookup.size() || world.combat.lookup[tile] >= world.combat.terrain_rules.size())
            throw std::invalid_argument("AI terrain lookup exceeds decoded rules");
        const auto& rule=world.combat.terrain_rules[world.combat.lookup[tile]].fields;
        if (world.movement.cells[i].terrain_cost != rule[0] ||
            world.movement.cells[i].terrain_field2 != rule[2])
            throw std::invalid_argument("AI movement and terrain rules differ");
    }
    // Public recovered helper checks the complete site/ownership/link shape.
    (void)capture_availability(world.sites,world.captures,{},0);
}
void validate(const AIWorldView& world, std::size_t actor) {
    validate_common(world);
    if (actor>=world.units.size() || world.units[actor].x>=world.movement.width ||
        world.units[actor].y>=world.movement.height)
        throw std::invalid_argument("AI actor is absent or outside map");
}

std::vector<std::size_t> original_order(const AIWorldView& world) {
    std::vector<std::size_t> order(world.units.size());
    std::iota(order.begin(),order.end(),0);
    std::sort(order.begin(),order.end(),[&](std::size_t a,std::size_t b) {
        return world.units[a].type < world.units[b].type;
    });
    return order;
}

bool matches(WorldCoordinate a, WorldCoordinate b) noexcept {
    // Original coordinate vectors use signed byte loads; unit tiles do not.
    return a.x < 128 && a.y < 128 && a.x == b.x && a.y == b.y;
}
bool is_site_or_item(const AIWorldView& world,WorldCoordinate position) {
    for (const auto& site:world.sites) {
        if (matches(site.position,position)) return true;
        for (const auto& group:site.coordinate_groups)
            for (const auto& item:group)
                if (matches(item,position)) return true;
    }
    return false;
}

bool any_target(const AIWorldView& world,const std::vector<CombatUnitState>& units,
                std::size_t actor) {
    const auto& a=units[actor];
    if (a.stats.attack_low == 0) return false;
    const auto& stats=world.combat.definitions[a.type].forms[a.form];
    // 0x43954's target/form arguments are overwritten by 0x4392a. It tests
    // ANY opposing active unit against the actor's current form and position.
    for (std::size_t i=0; i<units.size(); ++i) {
        const auto& u=units[i];
        if (i == actor || !u.active || u.owner == a.owner) continue;
        const int distance=std::abs(int(a.x)-u.x)+std::abs(int(a.y)-u.y);
        if (distance >= stats[1] && distance <= stats[2]) return true;
    }
    return false;
}

std::optional<std::size_t> occupant(const AIWorldView& world,
                                  const std::vector<std::size_t>& order,int x,int y) {
    for (std::uint8_t owner=0; owner<2; ++owner)
        for (auto i:order) {
            const auto& u=world.units[i];
            const auto& m=world.movement_units[i];
            if (u.active && u.owner == owner && u.x == x && u.y == y &&
                m.offset_x == 0 && m.offset_y == 0) return i;
        }
    return {};
}

std::optional<WorldCoordinate> production_destination(
    const AIWorldView& world,const std::vector<std::size_t>& order,std::size_t actor) {
    const auto& a=world.units[actor];
    const auto& f=form(world,actor,a.form);
    const auto origin=std::size_t(a.y)*world.movement.width+a.x;
    if (a.x>=128 || a.y>=128 ||
        (world.movement.cells[origin].terrain_cost==99 && sbyte(f[6])<=0)) return {};
    // 0x4a678 uses cost mode0, budget1: raw non99 terrain costs1. Occupancy
    // is checked after filling; points and regions do not participate.
    constexpr int dx[]={0,-1,1,0},dy[]={-1,0,0,1};
    for (int d=0;d<4;++d) {
        const int x=int(a.x)+dx[d],y=int(a.y)+dy[d];
        if (x<0 || y<0 || x>=world.movement.width || y>=world.movement.height) continue;
        if (world.movement.cells[std::size_t(y)*world.movement.width+std::size_t(x)].terrain_cost!=99 &&
            !occupant(world,order,x,y))
            return WorldCoordinate{static_cast<std::uint8_t>(x),static_cast<std::uint8_t>(y)};
    }
    return {};
}

std::int32_t position_score(const AIWorldView& world,std::size_t actor,
                           WorldCoordinate position,std::optional<std::size_t> excluded,
                           bool alternate) {
    auto units=world.units;
    auto& a=units[actor];
    a.x=position.x;
    a.y=position.y;
    std::int32_t score=0;
    const auto order=original_order(world);
    for (std::uint8_t owner=0; owner<2; ++owner) {
        if (owner == a.owner) continue;
        for (auto enemy:order) {
            const auto& e=units[enemy];
            if (!e.active || e.owner != owner || (excluded && *excluded == enemy)) continue;
            auto enemy_form=e.form;
            if (alternate) {
                if (sbyte(form(world,enemy,0)[3]) > 0) enemy_form=0;
                else if (sbyte(form(world,enemy,1)[3]) > 0) enemy_form=1;
            }
            if (any_target(world,units,enemy) && form(world,enemy,enemy_form)[3] != 0 &&
                e.flag_38 == 0 && (e.faction == 1 || !world.combat.phase_flag_227)) {
                const auto bonuses=combat_bonuses(world.combat,units,enemy,actor);
                score=add(score,sub(sub(a.stats.defense,e.stats.attack_high),bonuses.attack));
                score=add(score,bonuses.defense);
            }
            if (!any_target(world,units,actor)) continue;
            const WorldCoordinate enemy_position{e.x,e.y};
            const auto own=world.captures.counters_68[a.owner];
            const auto other=world.captures.counters_68[1-a.owner];
            for (std::size_t s=0; s<world.sites.size(); ++s) {
                const auto& site=world.sites[s];
                if (world.captures.sites[s].state != a.owner && matches(site.position,enemy_position))
                    score=add(score,ai_capture_income_potential(world.captures,world.player_factions,
                        a.owner,{CaptureTargetKind::site,s,0,0},own,other));
                for (std::size_t g=0; g<site.coordinate_groups.size(); ++g)
                    for (std::size_t i=0; i<site.coordinate_groups[g].size(); ++i)
                        if (world.captures.sites[s].item_states[g][i] != a.owner &&
                            matches(site.coordinate_groups[g][i],enemy_position))
                            score=add(score,ai_capture_income_potential(world.captures,world.player_factions,
                                a.owner,{CaptureTargetKind::item,s,g,i},own,other));
            }
        }
    }
    return score;
}

std::vector<MovementTile> ai_gradient_route(const MovementRange& field,
                                           MovementTile origin,MovementTile destination) {
    // 0x49e90 has no destination-budget admission test. Its AI caller may ask
    // for a route to an enemy cell whose own field value is negative.
    std::vector<MovementTile> backwards;
    std::vector<bool> visited(field.remaining.size());
    auto current=destination;
    const auto capacity=2U*(std::size_t(field.width)+field.height);
    while (current.x != origin.x || current.y != origin.y) {
        const auto index=std::size_t(current.y)*field.width+current.x;
        if (visited.at(index))
            throw std::invalid_argument("Original AI gradient route cycles; no substitute route");
        visited[index]=true;
        int best=-1;
        MovementTile next{};
        constexpr int dx[]={0,0,-1,1},dy[]={-1,1,0,0};
        for (int direction=0;direction<4;++direction) {
            const int x=int(current.x)+dx[direction],y=int(current.y)+dy[direction];
            if (x<0 || y<0 || x>=field.width || y>=field.height) continue;
            const auto value=field.at(x,y);
            if (value == movement_reserved_cell || value <= best) continue;
            best=value;next={static_cast<std::uint8_t>(x),static_cast<std::uint8_t>(y)};
        }
        if (best<0) {
            if (backwards.empty()) return {};
            throw std::invalid_argument("Original AI gradient route is disconnected");
        }
        backwards.push_back(current);
        if (backwards.size()>=capacity || backwards.size()>256U)
            throw std::invalid_argument("Original AI route storage would be exceeded");
        current=next;
    }
    return {backwards.rbegin(),backwards.rend()};
}

bool fallback_coordinate(const MovementRange& field,
                         WorldCoordinate point,WorldCoordinate origin,int limit) {
    if (point.x>=128 || point.y>=128 || point.x>=field.width || point.y>=field.height)
        return false;
    const auto value=field.at(point.x,point.y);
    if (limit!=-1 && sub(field.at(origin.x,origin.y),value)>limit) return false;
    return value>=0 && value!=movement_reserved_cell;
}

bool can_upgrade(const AIWorldView& world,std::size_t actor) {
    const auto& a=world.units[actor];
    const auto slot=world.upgrade_slots_34[world.current_player];
    if (slot==-1) return false;
    if (slot<0 || slot>=23) throw std::invalid_argument("AI upgrade slot is outside original storage");
    std::optional<std::size_t> original_slot;
    for (const auto& slots:world.owner_slot_types) {
        if (slots.size()>23) throw std::invalid_argument("AI owner slots exceed original storage");
        for (std::size_t i=0;i<slots.size();++i) {
            if (slots[i]>=23 || (i && slots[i]<=slots[i-1]))
                throw std::invalid_argument("AI owner slots are not ascending unique types");
            if (!original_slot && slots[i]==a.type) original_slot=i;
        }
    }
    if (!original_slot) throw std::invalid_argument("AI upgrade selection requires complete owner slots");
    if (std::size_t(slot)!=*original_slot) return false;
    return a.active && world.movement_units[actor].flag_2c==0 &&
        sbyte(form(world,actor,0)[12])!=1 && a.level<=24 &&
        world.funds[a.owner]>=unit_spawn_cost(world.combat.definitions[a.type]);
}

} // namespace

std::int32_t ai_position_score(const AIWorldView& world,std::size_t actor,
                              WorldCoordinate position,std::optional<std::size_t> excluded,
                              bool alternate) {
    validate(world,actor);
    if (position.x >= world.movement.width || position.y >= world.movement.height ||
        (excluded && *excluded >= world.units.size()))
        throw std::invalid_argument("AI candidate/excluded unit is outside input");
    return position_score(world,actor,position,excluded,alternate);
}

std::optional<AIPlan> choose_ai_attack(const AIWorldView& world,std::size_t actor) {
    validate(world,actor);
    const auto& a=world.units[actor];
    auto attack_form=a.form;
    if (form(world,actor,attack_form)[3] == 0) {
        attack_form=static_cast<std::uint8_t>(1-attack_form);
        if (form(world,actor,attack_form)[3] == 0) return {};
    }
    const auto& moving_stats=form(world,actor,a.form);
    const auto& attack_stats=form(world,actor,attack_form);
    int budget=sbyte(moving_stats[0]);
    if (sbyte(form(world,actor,0)[2]) > 1 && world.controllers[world.current_player] == 1)
        budget=0;
    const MovementProfile profile{a.type,a.owner,moving_stats[0],0,
                                  static_cast<std::int8_t>(sbyte(moving_stats[6]))};
    const auto field=compute_movement_range_with_budget(world.movement,profile,a.x,a.y,budget);
    // 0x49d58 only marks previously negative cells 127. 0x50ed6 excludes
    // both negative and127, so that overlay cannot change eligible positions.
    const int minimum=sbyte(attack_stats[1]),maximum=sbyte(attack_stats[2]);
    const auto order=original_order(world);
    std::optional<AIPlan> result;
    for (auto enemy:order) {
        const auto& e=world.units[enemy];
        if (!e.active || e.owner == a.owner) continue;
        bool found=false;
        std::int32_t best_score=-1;
        WorldCoordinate best{};
        for (int y=std::max(0,int(e.y)-maximum);
             y<=std::min(int(world.movement.height)-1,int(e.y)+maximum); ++y) {
            for (int x=std::max(0,int(e.x)-maximum);
                 x<=std::min(int(world.movement.width)-1,int(e.x)+maximum); ++x) {
                if (x == e.x && y == e.y) continue;
                const auto occupied=occupant(world,order,x,y);
                if (occupied && world.units[*occupied].type != a.type) continue;
                const auto cell=std::size_t(y)*world.movement.width+std::size_t(x);
                const auto& rule=world.combat.terrain_rules[
                    world.combat.lookup[world.combat.grid.tiles[cell]]].fields;
                if (rule[0] == 99 && rule[2] == 0 &&
                    form(world,actor,1)[6] != 0 && attack_form == 0) continue;
                if (field.remaining[cell] < 0 || field.remaining[cell] == movement_reserved_cell) continue;
                const auto distance=std::abs(x-int(e.x))+std::abs(y-int(e.y));
                if (distance < minimum || distance > maximum) continue;
                const WorldCoordinate candidate{static_cast<std::uint8_t>(x),static_cast<std::uint8_t>(y)};
                const auto score=position_score(world,actor,candidate,{},false);
                bool replace=!found;
                if (found && score > best_score) replace=!is_site_or_item(world,best);
                else if (found && x == a.x && y == a.y)
                    replace=is_site_or_item(world,candidate) || !is_site_or_item(world,best);
                if (replace) { found=true; best_score=score; best=candidate; }
            }
        }
        // -1 is an original sentinel even when a candidate genuinely scores -1.
        if (best_score == -1 || (result && best_score <= result->score)) continue;
        AIPlan plan;
        plan.score=best_score;
        plan.action=1;
        plan.target_type=e.type;
        plan.destination=best;
        plan.movement_form=a.form;
        plan.transform_on_arrival=a.form != attack_form;
        result=plan;
    }
    return result;
}

AIPlanningResult choose_ai_fallback_plan(const AIWorldView& world,std::size_t actor) {
    validate(world,actor);
    if (world.combat.terrain_rules.size()<=4)
        throw std::invalid_argument("AI fallback needs original terrain rule4");
    const auto& a=world.units[actor];
    const auto& def=world.combat.definitions[a.type];
    const auto original_form=a.form;
    auto current_form=original_form;
    auto stats=a.stats;
    const auto toggle=[&] {
        current_form=static_cast<std::uint8_t>(1-current_form);
        stats=combat_stats(def,current_form,a.level);
    };
    const int base0=sbyte(def.forms[0][0]),base1=sbyte(def.forms[1][0]);
    std::uint8_t movement_form=0;
    int movement_budget=base0;
    if (sbyte(def.scalar_fields[2])!=0 && base1>base0) {
        movement_form=1;movement_budget=base1;
        if (original_form!=1) toggle();
    }
    const auto& moving_stats=def.forms[current_form];
    const MovementProfile search_profile{a.type,a.owner,moving_stats[0],0,
        static_cast<std::int8_t>(sbyte(moving_stats[6]))};
    const int search_budget=world.combat.terrain_rules[4].fields[0]*
        (int(world.movement.width)+world.movement.height);
    const auto field=compute_movement_range_with_budget(world.movement,search_profile,a.x,a.y,search_budget);
    const auto order=original_order(world);
    const WorldCoordinate origin{a.x,a.y};
    const auto neutral=[&](std::int8_t owner) {
        return owner==-1 || world.player_factions[static_cast<std::size_t>(owner)]==-1;
    };
    const auto own=world.captures.counters_68[a.owner];
    const auto other=world.captures.counters_68[1-a.owner];
    std::optional<AIPlan> result;
    for (std::size_t s=0;s<world.sites.size();++s) {
        const auto position=world.sites[s].position;
        const auto owner=world.captures.sites[s].state;
        if (owner==1-a.owner || !neutral(owner) ||
            !fallback_coordinate(field,position,origin,movement_budget) ||
            occupant(world,order,position.x,position.y)) continue;
        AIPlan plan;
        plan.destination=position;plan.movement_form=movement_form;
        plan.score=ai_capture_income_potential(world.captures,world.player_factions,a.owner,
            {CaptureTargetKind::site,s,0,0},own,other);
        // Original site fallback overwrites unconditionally: last viable site.
        result=plan;
    }
    if (!result) {
        for (std::size_t s=0;s<world.sites.size();++s)
            for (std::size_t g=0;g<world.sites[s].coordinate_groups.size();++g)
                for (std::size_t i=0;i<world.sites[s].coordinate_groups[g].size();++i) {
                    const auto position=world.sites[s].coordinate_groups[g][i];
                    const auto owner=world.captures.sites[s].item_states[g][i];
                    if (owner==1-a.owner || !neutral(owner) ||
                        !fallback_coordinate(field,position,origin,movement_budget) ||
                        occupant(world,order,position.x,position.y)) continue;
                    // Original 0x4efb0 calls SITE potential 0x4b56c even here.
                    const auto score=ai_capture_income_potential(world.captures,world.player_factions,a.owner,
                        {CaptureTargetKind::site,s,0,0},own,other);
                    if (result && result->score>=score) continue;
                    AIPlan plan;
                    plan.score=score;plan.destination=position;plan.movement_form=movement_form;
                    result=plan;
                }
    }
    if (!result) {
        std::vector<MovementTile> nearest;
        for (auto enemy:order) {
            const auto& e=world.units[enemy];
            if (!e.active || e.owner==a.owner) continue;
            auto route=ai_gradient_route(field,{a.x,a.y},{e.x,e.y});
            if (!route.empty() && (nearest.empty() || route.size()<nearest.size())) nearest=std::move(route);
        }
        if (!nearest.empty()) {
            const auto& desired_stats=def.forms[movement_form];
            const MovementProfile cost_profile{a.type,a.owner,desired_stats[0],0,
                static_cast<std::int8_t>(sbyte(desired_stats[6]))};
            std::int32_t total=0;
            std::size_t i=0;
            for (;i<nearest.size();++i) {
                total=add(total,movement_entry_cost(world.movement,cost_profile,nearest[i].x,nearest[i].y));
                if (i>0 && total>movement_budget) break;
            }
            while (i>0) {
                const auto candidate=nearest[i-1];
                if (!occupant(world,order,candidate.x,candidate.y)) {
                    AIPlan plan;
                    plan.destination={candidate.x,candidate.y};plan.movement_form=movement_form;
                    result=plan;
                    // Exact extra toggle in successful enemy-approach tail.
                    if (movement_form!=original_form) toggle();
                    break;
                }
                --i;
            }
        }
    }
    if (result && can_upgrade(world,actor)) result->action=4;
    if (movement_form!=original_form) toggle();
    return {result,current_form,stats};
}

AIPlanningResult plan_ai_unit(const AIWorldView& world,std::size_t actor) {
    const auto attack=choose_ai_attack(world,actor);
    const auto capture=choose_ai_capture_plan(world,actor);
    if (attack && (!capture || attack->score>capture->score))
        return {attack,world.units[actor].form,world.units[actor].stats};
    if (capture) return {capture,world.units[actor].form,world.units[actor].stats};
    return choose_ai_fallback_plan(world,actor);
}

std::array<std::vector<std::uint8_t>,2> ai_owner_slots(
    const std::vector<UnitDefinition>& definitions,const std::vector<CombatUnitState>& units,
    std::array<std::int8_t,2> factions) {
    if (definitions.size()!=23) throw std::invalid_argument("AI owner slots require all23definitions");
    std::array<int,23> live_faction{};
    live_faction.fill(-1);
    for (const auto& u:units) {
        if (u.type>=23 || live_faction[u.type]!=-1)
            throw std::invalid_argument("AI owner-slot roster has duplicate/invalidtype");
        live_faction[u.type]=u.faction;
    }
    std::array<std::vector<std::uint8_t>,2> result;
    for (std::size_t owner=0;owner<2;++owner)
        for (std::size_t t=0;t<23;++t) {
            if (definitions[t].type!=t) throw std::invalid_argument("AI definitions are not type-indexed");
            const auto faction=live_faction[t]==-1 ? definitions[t].faction() : live_faction[t];
            if (faction==factions[owner]) result[owner].push_back(static_cast<std::uint8_t>(t));
        }
    return result;
}

AITurnAdmission choose_ai_turn_admission(const AIWorldView& world,std::int32_t cursor) {
    validate_common(world);
    if (cursor<0 || cursor>23 || world.controllers[world.current_player]!=1)
        throw std::invalid_argument("AI turn admission requires an AI player and cursor0..23");
    std::array<std::optional<std::size_t>,23> by_type{};
    for (std::size_t i=0;i<world.units.size();++i) by_type[world.units[i].type]=i;
    for (const auto& slots:world.owner_slot_types) {
        if (slots.size()>23) throw std::invalid_argument("AI owner slots exceed original capacity");
        for (std::size_t i=0;i<slots.size();++i)
            if (slots[i]>=23 || (i && slots[i]<=slots[i-1]))
                throw std::invalid_argument("AI owner slots must contain ascending unique types");
    }
    const auto& slots=world.owner_slot_types[world.current_player];
    for (const auto& u:world.units)
        if (u.active && u.owner==world.current_player &&
            std::find(slots.begin(),slots.end(),u.type)==slots.end())
            throw std::invalid_argument("AI owner slots omit an active current-playerunit");
    const auto order=original_order(world);
    const auto adjacent_space=[&](std::size_t actor) {
        const auto& a=world.units[actor];
        const auto& f=form(world,actor,a.form);
        const auto origin=std::size_t(a.y)*world.movement.width+a.x;
        if (world.movement.cells[origin].terrain_cost==99 && sbyte(f[6])<=0) return false;
        // 0x4a678 uses cost mode0, budget1: raw non99 terrain costs1. Its
        // occupancy test follows filling, so points/regions do not participate.
        constexpr int dx[]={0,0,-1,1},dy[]={-1,1,0,0};
        for (int d=0;d<4;++d) {
            const int x=int(a.x)+dx[d],y=int(a.y)+dy[d];
            if (x<0 || y<0 || x>=world.movement.width || y>=world.movement.height) continue;
            if (world.movement.cells[std::size_t(y)*world.movement.width+std::size_t(x)].terrain_cost!=99 &&
                !occupant(world,order,x,y)) return true;
        }
        return false;
    };
    for (int priority=0;priority<2;++priority) {
        const auto field=priority==0 ? std::size_t(12) : std::size_t(9);
        std::optional<std::size_t> selected_producer;
        std::optional<std::uint8_t> selected_type;
        for (auto type:slots) {
            const auto producer=by_type[type];
            if (!producer || !world.units[*producer].active ||
                sbyte(form(world,*producer,0)[field])!=1 ||
                sbyte(form(world,*producer,world.units[*producer].form)[field])!=1 ||
                !adjacent_space(*producer)) continue;
            const auto owner=world.units[*producer].owner;
            std::optional<std::uint8_t> candidate;
            int highest_cost=0;
            for (auto target:world.owner_slot_types[owner]) {
                if (!world.available_types[target] || (by_type[target] && world.units[*by_type[target]].active)) continue;
                const auto& def=world.combat.definitions[target];
                const bool field8=sbyte(def.forms[0][8])==1 || sbyte(def.forms[1][8])==1;
                if (field8!=(priority==1)) continue;
                const auto cost=unit_spawn_cost(def);
                if (cost>highest_cost && cost<=world.funds[world.current_player]) {
                    candidate=target;highest_cost=cost;
                }
            }
            if (candidate) { selected_producer=producer;selected_type=candidate; }
        }
        if (selected_producer)
            return {AITurnAdmissionKind::production,selected_producer,selected_type,cursor};
    }
    for (std::size_t slot=static_cast<std::size_t>(cursor);slot<slots.size();++slot) {
        const auto actor=by_type[slots[slot]];
        if (actor && world.units[*actor].active && world.movement_units[*actor].flag_2c==0)
            return {AITurnAdmissionKind::unit,actor,{},static_cast<std::int32_t>(slot+1)};
    }
    return {AITurnAdmissionKind::turn_finished,{},{},23};
}

std::optional<WorldCoordinate> ai_production_destination(const AIWorldView& world,
                                                       std::size_t producer) {
    validate(world,producer);
    return production_destination(world,original_order(world),producer);
}

} // namespace g1
