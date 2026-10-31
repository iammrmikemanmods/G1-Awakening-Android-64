#include "g1/CaptureAction.hpp"

#include <stdexcept>

namespace g1 {
namespace {

void check_count(std::size_t count) {
    if (count > 255) throw std::invalid_argument("Capture count exceeds original byte loop");
}
void check_owner(std::int8_t owner) {
    if (owner < -1 || owner > 1) throw std::invalid_argument("Unsupported capture owner");
}
void check_actor_owner(std::uint8_t owner) {
    if (owner > 1) throw std::invalid_argument("Unsupported actor owner");
}
void validate(const CaptureNetwork& network) {
    check_count(network.sites.size());
    if (network.item_links.size() != network.sites.size())
        throw std::invalid_argument("Capture link/site shape mismatch");
    for (std::size_t s = 0; s < network.sites.size(); ++s) {
        const auto& site = network.sites[s];
        check_owner(site.state);
        check_count(site.item_states.size());
        if (network.item_links[s].size() != site.item_states.size())
            throw std::invalid_argument("Capture link/group shape mismatch");
        for (std::size_t g = 0; g < site.item_states.size(); ++g) {
            check_count(site.item_states[g].size());
            if (network.item_links[s][g].size() != site.item_states[g].size())
                throw std::invalid_argument("Capture link/item shape mismatch");
            for (auto owner : site.item_states[g]) check_owner(owner);
        }
    }
}
void validate_coordinates(const std::vector<WorldSite>& coordinates) {
    check_count(coordinates.size());
    for (const auto& site : coordinates) {
        check_count(site.coordinate_groups.size());
        for (const auto& group : site.coordinate_groups) check_count(group.size());
    }
}
void validate(const std::vector<WorldSite>& coordinates, const CaptureNetwork& network) {
    validate(network);
    validate_coordinates(coordinates);
    if (coordinates.size() != network.sites.size())
        throw std::invalid_argument("Capture coordinate/site shape mismatch");
    for (std::size_t s = 0; s < coordinates.size(); ++s) {
        if (coordinates[s].coordinate_groups.size() != network.sites[s].item_states.size())
            throw std::invalid_argument("Capture coordinate/group shape mismatch");
        for (std::size_t g = 0; g < coordinates[s].coordinate_groups.size(); ++g)
            if (coordinates[s].coordinate_groups[g].size() != network.sites[s].item_states[g].size())
                throw std::invalid_argument("Capture coordinate/item shape mismatch");
    }
}
bool matches(WorldCoordinate coordinate, WorldCoordinate actor) {
    // Coordinate vectors are loaded with LDRSB; unit coordinates use LDRB.
    // Values >=128 therefore never match an original nonnegative unit cell.
    return coordinate.x < 128 && coordinate.y < 128 &&
           coordinate.x == actor.x && coordinate.y == actor.y;
}
std::int8_t faction_of(std::int8_t owner,
                       const std::array<std::int8_t, 2>& factions) {
    return owner == -1 ? -1 : factions[static_cast<std::size_t>(owner)];
}

} // namespace

CaptureNetwork initial_capture_network(const std::vector<WorldSite>& coordinates) {
    validate_coordinates(coordinates);
    CaptureNetwork result;
    for (const auto& coordinate : coordinates) {
        check_owner(coordinate.state);
        MissionSiteState state;
        state.state = coordinate.state;
        std::vector<std::vector<std::uint8_t>> links;
        for (const auto& group : coordinate.coordinate_groups) {
            state.item_states.emplace_back(group.size(), -1);
            links.emplace_back(group.size(), 0);
        }
        result.sites.push_back(std::move(state));
        result.item_links.push_back(std::move(links));
    }
    return result;
}

void refresh_capture_network(CaptureNetwork& network,
                             std::array<std::int8_t, 2> factions,
                             bool counters, bool links) {
    validate(network);
    if (counters) network.counters_68 = {{1, 1}};
    for (std::size_t s = 0; s < network.sites.size(); ++s) {
        const auto& site = network.sites[s];
        const auto site_faction = faction_of(site.state, factions);
        if (counters && site_faction != -1)
            network.counters_68[static_cast<std::size_t>(site.state)] += 2;
        for (std::size_t g = 0; g < site.item_states.size(); ++g) {
            auto reference_faction = site_faction;
            auto reference_owner = site.state;
            bool seen_owner = site_faction != -1;
            bool conflicting_faction = false;
            for (std::size_t i = 0; i < site.item_states[g].size(); ++i) {
                const auto owner = site.item_states[g][i];
                const auto faction = faction_of(owner, factions);
                if (links) network.item_links[s][g][i] = 0;
                if (reference_faction == -1) {
                    if (faction == -1) continue;
                    reference_faction = faction;
                    reference_owner = owner;
                } else if (faction != reference_faction) {
                    if (faction == -1) continue;
                    conflicting_faction = true;
                }
                if (faction == -1) continue;
                if (reference_faction != -1 && (!seen_owner || !conflicting_faction)) {
                    if (counters)
                        network.counters_68[static_cast<std::size_t>(reference_owner)] += 2;
                    if (links) network.item_links[s][g][i] = 1;
                } else if (counters) {
                    ++network.counters_68[static_cast<std::size_t>(owner)];
                }
                seen_owner = true;
            }
        }
    }
}

CaptureAvailability capture_availability(const std::vector<WorldSite>& coordinates,
                                         const CaptureNetwork& network,
                                         WorldCoordinate position, std::uint8_t owner) {
    check_actor_owner(owner);
    validate(coordinates, network);
    CaptureAvailability result;
    for (std::size_t s = 0; s < coordinates.size(); ++s) {
        if (matches(coordinates[s].position, position) && network.sites[s].state != owner)
            result.site = true;
        for (std::size_t g = 0; g < coordinates[s].coordinate_groups.size(); ++g)
            for (std::size_t i = 0; i < coordinates[s].coordinate_groups[g].size(); ++i)
                if (matches(coordinates[s].coordinate_groups[g][i], position) &&
                    network.sites[s].item_states[g][i] != owner) result.item = true;
    }
    return result;
}

std::optional<CaptureTarget> choose_capture_target(const std::vector<WorldSite>& coordinates,
                                                  WorldCoordinate position) {
    validate_coordinates(coordinates);
    std::optional<CaptureTarget> site_match;
    for (std::size_t s = 0; s < coordinates.size(); ++s) {
        if (matches(coordinates[s].position, position))
            site_match = CaptureTarget{CaptureTargetKind::site, s, 0, 0};
        for (std::size_t g = 0; g < coordinates[s].coordinate_groups.size(); ++g)
            for (std::size_t i = 0; i < coordinates[s].coordinate_groups[g].size(); ++i)
                if (matches(coordinates[s].coordinate_groups[g][i], position))
                    return CaptureTarget{CaptureTargetKind::item, s, g, i};
    }
    return site_match;
}

CaptureResult apply_capture_target(const std::vector<WorldSite>& coordinates,CaptureNetwork& network,
    const CaptureTarget& target,std::uint8_t owner,std::array<std::int8_t,2> factions,
    std::array<std::int8_t,2> controllers) {
    check_actor_owner(owner);
    validate(coordinates, network);
    if (target.site>=network.sites.size() ||
        (target.kind!=CaptureTargetKind::site && target.kind!=CaptureTargetKind::item) ||
        (target.kind==CaptureTargetKind::item &&
         (target.group>=network.sites[target.site].item_states.size() ||
          target.item>=network.sites[target.site].item_states[target.group].size())))
        throw std::invalid_argument("Capture target index is outside ownership state");
    CaptureResult result;
    if (target.kind==CaptureTargetKind::item) {
        network.sites[target.site].item_states[target.group][target.item]=static_cast<std::int8_t>(owner);
        result.clear_selection_flags=true;
    } else {
        network.sites[target.site].state=static_cast<std::int8_t>(owner);
        result.site_visual_faction=factions[owner];
    }
    refresh_capture_network(network,factions,false,true);
    result.status=CaptureStatus::captured;
    result.target=target;
    result.close_command_menu=true;
    result.audio_5_12=controllers[owner]==0;
    return result;
}

CaptureResult perform_capture(const std::vector<WorldSite>& coordinates, CaptureNetwork& network,
                              CaptureActor& actor, std::array<std::int8_t, 2> factions,
                              std::array<std::int8_t, 2> controllers) {
    check_actor_owner(actor.owner);
    validate(coordinates, network);
    CaptureResult result;
    if (!actor.active) { result.status = CaptureStatus::inactive; return result; }
    if (actor.flag_2c != 0) { result.status = CaptureStatus::action_used; return result; }
    const auto available = capture_availability(coordinates, network, actor.position, actor.owner);
    if (!available.item && !available.site) return result;
    const auto target = choose_capture_target(coordinates, actor.position);
    if (!target) throw std::logic_error("Capture availability without matching coordinate");
    if (actor.form_field10 != 1) {
        result.status = CaptureStatus::form_cannot_capture;
        result.message_id = 1145;
        return result;
    }
    result=apply_capture_target(coordinates,network,*target,actor.owner,factions,controllers);
    actor.flag_2c = 1;
    return result;
}

} // namespace g1
