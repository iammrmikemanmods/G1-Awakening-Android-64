#include "g1/MovementArrival.hpp"

#include <algorithm>
#include <array>
#include <cstdlib>
#include <stdexcept>

namespace g1 {

MovementArrivalFacing choose_movement_arrival_facing(
    const ArrivalUnit& actor, const std::vector<ArrivalUnit>& world_units,
    std::uint8_t minimum_distance, std::uint8_t maximum_distance,
    std::int8_t selected_form_field13) {
    if (actor.type >= 23U || actor.owner > 1U || actor.facing > 3U)
        throw std::invalid_argument("arrival actor is outside the recovered unit variant");
    MovementArrivalFacing result;
    result.facing = actor.facing;
    if (selected_form_field13 == 1) return result;

    std::array<const ArrivalUnit*, 23> by_type{};
    for (const auto& unit : world_units) {
        if (unit.type >= by_type.size() || unit.owner > 1U || by_type[unit.type])
            throw std::invalid_argument("arrival requires unique types0..22 and owners0..1");
        by_type[unit.type] = &unit;
    }
    result.writes_facing = true;
    result.facing = 1;
    result.target_types.emplace();
    const ArrivalUnit* nearest{};
    int best_distance{}, best_vertical_distance{};
    for (const auto* unit : by_type) {
        if (!unit || !unit->active || unit->type == actor.type || unit->owner == actor.owner)
            continue;
        const auto dx = std::abs(int(unit->x) - int(actor.x));
        const auto dy = std::abs(int(unit->y) - int(actor.y));
        const auto distance = dx + dy;
        if (distance < minimum_distance || distance > maximum_distance) continue;
        result.target_types->push_back(unit->type);
        if (!nearest || distance < best_distance ||
            (distance == best_distance && dy < best_vertical_distance)) {
            nearest = unit;
            best_distance = distance;
            best_vertical_distance = dy;
        }
    }
    if (nearest) {
        result.nearest_type = nearest->type;
        if (actor.x > nearest->x) result.facing = 2;
        else if (actor.x < nearest->x) result.facing = 3;
    }
    return result;
}

} // namespace g1
