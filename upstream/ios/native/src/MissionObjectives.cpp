#include "g1/MissionObjectives.hpp"

#include <limits>

namespace g1 {
namespace {

std::int16_t signed16(std::uint16_t value) noexcept {
    return static_cast<std::int16_t>(value <= 0x7fffU ? static_cast<std::int32_t>(value) :
                                    static_cast<std::int32_t>(value) - 0x10000);
}

std::uint8_t expected_parameters(MissionObjectiveKind kind) {
    switch (kind) {
    case MissionObjectiveKind::site_owner: return 2U;
    case MissionObjectiveKind::site_item_owner: return 4U;
    case MissionObjectiveKind::inactive_unit: return 1U;
    case MissionObjectiveKind::unit_at_position: return 3U;
    }
    throw FormatError("unknown original mission objective kind");
}

const MissionObjectiveUnit& unit(const MissionObjectiveView& view, std::int16_t type) {
    if (type < 0 || static_cast<std::size_t>(type) >= view.units_by_type.size())
        throw FormatError("mission objective unit type is outside native state");
    return view.units_by_type[static_cast<std::size_t>(type)];
}

bool active(const MissionObjectiveView& view, std::int16_t type) {
    const auto& value = unit(view, type);
    return value.present && value.active;
}

std::uint8_t owner_for_faction(const MissionObjectiveView& view, std::uint8_t faction) {
    for (std::uint8_t owner = 0; owner < 2U; ++owner)
        if (view.campaign.player_factions[owner] == faction) return owner;
    throw FormatError("mission objective faction is absent from the two owner slots");
}

bool centered_at(const MissionObjectiveUnit& value, std::int16_t x, std::int16_t y) noexcept {
    return value.x == x && value.y == y && value.offset_x == 0 && value.offset_y == 0;
}

} // namespace

std::vector<MissionSiteState> initial_mission_site_states(const WorldBootstrap& world) {
    std::vector<MissionSiteState> result;
    result.reserve(world.sites.size());
    for (const auto& site : world.sites) {
        MissionSiteState state;
        state.state = site.state;
        for (const auto& group : site.coordinate_groups)
            state.item_states.emplace_back(group.size(), static_cast<std::int8_t>(-1));
        result.push_back(std::move(state));
    }
    return result;
}

bool captured_site_objective_satisfied(const CapturedSiteObjective& objective,
                                      const std::vector<MissionSiteState>& sites) {
    if (objective.site < -1 || objective.group < -1 || objective.item < -1)
        throw FormatError("unsupported negative capture-objective index");
    if (objective.site == -1 || objective.group == -1 || objective.item == -1) {
        for (const auto& site : sites) {
            if (site.state != objective.desired_state) return false;
            for (const auto& group : site.item_states)
                for (const auto item : group)
                    if (item != objective.desired_state) return false;
        }
        return true;
    }
    const auto site_index = static_cast<std::size_t>(objective.site);
    const auto group_index = static_cast<std::size_t>(objective.group);
    const auto item_index = static_cast<std::size_t>(objective.item);
    if (site_index >= sites.size() || group_index >= sites[site_index].item_states.size() ||
        item_index >= sites[site_index].item_states[group_index].size())
        throw FormatError("capture-objective index exceeds native site state");
    return sites[site_index].item_states[group_index][item_index] == objective.desired_state;
}

MissionObjective decode_mission_objective(const MissionCommand& command) {
    const auto& words = command.argument_words_u16;
    if (command.opcode != 12U || words.size() < 2U || words[0] > 3U)
        throw FormatError("not a supported original mission objective command");
    MissionObjective result;
    result.kind = static_cast<MissionObjectiveKind>(words[0]);
    result.parameter_count = expected_parameters(result.kind);
    if (words.size() != static_cast<std::size_t>(result.parameter_count) + 2U)
        throw FormatError("mission objective argument count mismatch");
    result.text_index = signed16(words[1]);
    result.text_id = script_text_id(result.text_index);
    for (std::size_t index = 0; index < result.parameter_count; ++index)
        result.parameters[index] = signed16(words[index + 2U]);
    return result;
}

MissionObjectiveDecision evaluate_mission_objective(const MissionObjective& objective,
                                                   const MissionObjectiveView& view) {
    if (objective.parameter_count != expected_parameters(objective.kind))
        throw FormatError("malformed native mission objective parameter count");
    static_cast<void>(mission_resource_for_campaign(view.campaign.group, view.campaign.mission_index));
    if (view.command_index >= view.commands.size() ||
        view.commands.size() > static_cast<std::size_t>(std::numeric_limits<std::int32_t>::max()) ||
        view.current_owner >= 2U || view.units_by_type.size() <= 12U)
        throw FormatError("invalid objective cursor/current owner/unit state");
    for (const auto& value : view.units_by_type)
        if (value.present && value.owner >= 2U)
            throw FormatError("objective unit has an invalid owner slot");
    const auto& context = view.campaign;
    const bool type12_active = active(view, 12);
    const bool special_loss = context.group == 0U && context.mission_index == 9 &&
        context.active_unit_counts[0] == 1U && type12_active;
    bool satisfied = context.active_unit_counts[0] == 0U || special_loss ||
        (context.active_unit_counts[1] == 0U &&
         !(context.group == 0U && context.mission_index == 0));
    if (!satisfied) {
        const auto& args = objective.parameters;
        switch (objective.kind) {
        case MissionObjectiveKind::site_owner:
            if (args[0] == -1) {
                satisfied = true;
                for (const auto& site : view.sites)
                    if (site.state != args[1]) { satisfied = false; break; }
            } else {
                if (args[0] < 0 || static_cast<std::size_t>(args[0]) >= view.sites.size())
                    throw FormatError("mission objective site index is out of bounds");
                satisfied = view.sites[static_cast<std::size_t>(args[0])].state == args[1];
            }
            break;
        case MissionObjectiveKind::site_item_owner:
            satisfied = captured_site_objective_satisfied({args[0], args[1], args[2], args[3],
                objective.text_index, objective.text_id}, view.sites);
            break;
        case MissionObjectiveKind::inactive_unit:
            if (args[0] == -1 || args[0] == -2) {
                const auto faction = static_cast<std::uint8_t>(args[0] == -1 ? 0U : 1U);
                const auto owner = owner_for_faction(view, faction);
                satisfied = true;
                for (const auto& value : view.units_by_type)
                    if (value.present && value.owner == owner && value.active) {
                        satisfied = false; break;
                    }
            } else {
                satisfied = !active(view, args[0]);
            }
            break;
        case MissionObjectiveKind::unit_at_position:
            if (args[0] == -3) {
                // This is the original comparison: faction byte versus the
                // current OWNER byte. Do not replace it with current_owner.
                const auto owner = owner_for_faction(view, view.current_owner);
                if (context.player_controllers[owner] != 1) {
                    for (const auto& value : view.units_by_type)
                        if (value.present && value.owner == owner && value.active &&
                            centered_at(value, args[1], args[2])) { satisfied = true; break; }
                }
            } else {
                const auto& value = unit(view, args[0]);
                if (!value.present)
                    throw FormatError("named position objective refers to an absent unit object");
                satisfied = centered_at(value, args[1], args[2]);
            }
            break;
        }
    }
    MissionObjectiveDecision result;
    result.command_cursor = static_cast<std::int32_t>(view.command_index);
    if (!satisfied) return result;
    if (objective.kind == MissionObjectiveKind::inactive_unit &&
        objective.parameters[0] == 0 && active(view, 0) && !type12_active) {
        result.set_alternate_dialogue_variant = true;
        const auto jumped = static_cast<std::int64_t>(result.command_cursor) + 10;
        if (jumped >= static_cast<std::int64_t>(view.commands.size()))
            throw FormatError("original objective branch exceeds the native script");
        result.command_cursor = static_cast<std::int32_t>(jumped);
    }
    if ((context.group == 0U && context.active_unit_counts[0] == 0U &&
         context.mission_index != 16) || special_loss) {
        auto cursor = static_cast<std::int32_t>(view.commands.size()) - 1;
        while (cursor > result.command_cursor) {
            if (view.commands[static_cast<std::size_t>(cursor)].opcode == 1U) break;
            --cursor;
        }
        result.command_cursor = cursor - 1;
    }
    result.resume_script = true;
    result.reset_current_player_actions = true;
    return result;
}

} // namespace g1
