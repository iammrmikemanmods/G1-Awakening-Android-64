#include "g1/MissionExecution.hpp"
#include "g1/WorldCommands.hpp"

#include <algorithm>
#include <limits>

namespace g1 {
namespace {

std::int32_t signed32(std::uint32_t value) noexcept {
    return value <= 0x7fffffffU ? static_cast<std::int32_t>(value) :
        static_cast<std::int32_t>(static_cast<std::int64_t>(value) - 0x100000000LL);
}

std::int32_t wrap32(std::int64_t value) noexcept {
    return signed32(static_cast<std::uint32_t>(value));
}

std::int16_t signed16(std::uint16_t value) noexcept {
    const auto wide = value <= 0x7fffU ? static_cast<std::int32_t>(value) :
        static_cast<std::int32_t>(value) - 0x10000;
    return static_cast<std::int16_t>(wide);
}

std::int64_t floor_div_65536(std::int64_t value) noexcept {
    return value >= 0 ? value / 65536 : -((-value + 65535) / 65536);
}

// Original 0x2b644 takes a signed Q16.16 delta and multiplier. The preceding
// LSL #16 discards high bits; the low 32-bit result is preserved explicitly.
std::int32_t scaled_delta(std::uint32_t delta, std::int32_t multiplier) noexcept {
    const auto shifted = signed32(delta << 16U);
    return wrap32(floor_div_65536(static_cast<std::int64_t>(shifted) * multiplier));
}

bool consume_edge(std::uint32_t& edges, std::uint32_t mask) noexcept {
    if ((edges & mask) == 0U) return false;
    edges = 0U;
    return true;
}

void require_words(const MissionCommand& command, std::size_t count) {
    if (command.argument_words_u16.size() != count)
        throw FormatError("mission execution received a malformed decoded command");
}

void mark_ready(MissionExecutionState& state) noexcept {
    state.original_phase = 99;
    state.gate = MissionGate::advance_pending;
}

} // namespace

DialogueUpdate advance_dialogue(const DialogueProgress& input,
                                const DialogueLayout& layout,
                                std::uint32_t edges, std::uint32_t delta_ms) {
    if (layout.line_count < 0 || layout.line_height <= 0 || layout.box_height < 0 ||
        input.first_line < 0 || input.first_line > layout.line_count)
        throw FormatError("invalid dialogue layout or line cursor");
    if (input.ready_for_next_command)
        throw FormatError("cannot tick a completed dialogue gate");

    DialogueUpdate result;
    result.progress = input;
    auto& progress = result.progress;
    if (progress.reveal_offset < 0) {
        progress.reveal_offset = wrap32(static_cast<std::int64_t>(progress.reveal_offset) +
                                      scaled_delta(delta_ms, 2048));
        if (progress.reveal_offset > 0) progress.reveal_offset = 0;
    }
    const auto remaining = layout.line_count - progress.first_line;
    const auto height = wrap32(static_cast<std::int64_t>(remaining - 1) *
                              layout.line_height +
                              wrap32(static_cast<std::int64_t>(layout.metric_a) - layout.metric_b));
    result.more_pages = remaining > 0 && height >= layout.box_height;

    const bool back = consume_edge(edges, 1U);
    const bool advance = consume_edge(edges, 16U) || consume_edge(edges, 64U);
    const bool skip = consume_edge(edges, 128U);
    if (back || advance) {
        if (progress.first_line > 0 && back) {
            --progress.first_line;
        } else if (result.more_pages && advance) {
            const auto numerator = wrap32(static_cast<std::int64_t>(layout.box_height) + layout.metric_b);
            const auto lines = std::min(numerator / layout.line_height, remaining);
            if (lines < 0) throw FormatError("dialogue metrics produce a negative page step");
            progress.first_line += lines;
        } else if (!layout.visible_text_fully_revealed) {
            progress.reveal_offset = 0;
        } else if (!result.more_pages && advance) {
            progress.ready_for_next_command = true;
        }
    } else if (skip) {
        progress.ready_for_next_command = true;
        progress.skip_following_dialogue = true;
        result.skip_audio_event = true;
    }
    result.remaining_input_edges = edges;
    return result;
}

std::int32_t dialogue_character_budget(const DialogueProgress& progress) noexcept {
    return static_cast<std::int32_t>(1000 + floor_div_65536(progress.reveal_offset));
}

MissionExecution::MissionExecution(const WorldBootstrap& world)
    : MissionExecution(world, CampaignContext{}) {}

MissionExecution::MissionExecution(const WorldBootstrap& world, const CampaignContext& campaign)
    : commands_(world.commands), campaign_group_(campaign.group),
      campaign_index_(campaign.mission_index) {
    if (world.mission_resource_id != mission_resource_for_campaign(campaign.group, campaign.mission_index) ||
        world.next_command_index >= commands_.size() ||
        (world.stop != BootstrapStop::execution_required &&
         (world.stop != BootstrapStop::presentation_required ||
          commands_[world.next_command_index].opcode != 3U)))
        throw FormatError("mission continuation requires a matching campaign execution boundary");
    raw_unit_name_indices_.reserve(world.unit_definitions.size());
    for (std::size_t type = 0; type < world.unit_definitions.size(); ++type) {
        const auto& definition = world.unit_definitions[type];
        if (definition.type != type)
            throw FormatError("mission speaker definitions are not indexed by unit type");
        raw_unit_name_indices_.push_back(definition.raw_text_indices[0]);
    }
    state_.command_index = world.next_command_index;
    state_.phase_flag_225 = world.flag_225;
    state_.original_phase = 99;
    enter_current(initial_effects_);
}

std::vector<MissionEffect> MissionExecution::take_initial_effects() {
    std::vector<MissionEffect> result;
    result.swap(initial_effects_);
    return result;
}

MissionGameplayCheckpoint MissionExecution::gameplay_checkpoint() const {
    if ((state_.gate!=MissionGate::objective && state_.gate!=MissionGate::captured_site_item) ||
        state_.stop!=MissionStop::none || state_.interaction_mode!=0 || state_.opcode!=12 ||
        !state_.mission_objective || state_.skip_following_dialogue)
        throw FormatError("mission is not at a checkpointable gameplay objective");
    return {state_.command_index,state_.original_phase,state_.phase_flag_225,
            state_.alternate_dialogue_variant,state_.transition_value,state_.remaining_delay_ms};
}

void MissionExecution::restore_gameplay_checkpoint(const MissionGameplayCheckpoint& saved) {
    const auto current=gameplay_checkpoint();
    if (saved.command_index!=current.command_index || saved.original_phase<0 || saved.original_phase>99)
        throw FormatError("saved mission checkpoint does not match its objective boundary");
    state_.original_phase=saved.original_phase;
    state_.phase_flag_225=saved.phase_flag_225;
    state_.alternate_dialogue_variant=saved.alternate_dialogue_variant;
    state_.transition_value=saved.transition_value;
    state_.remaining_delay_ms=saved.remaining_delay_ms;
}

void MissionExecution::stop(MissionStop reason, const std::string& detail) {
    state_.gate = MissionGate::stopped;
    state_.stop = reason;
    state_.stop_detail = detail;
}

void MissionExecution::enter_current(std::vector<MissionEffect>& effects) {
    if (state_.command_index >= commands_.size()) {
        state_.gate = MissionGate::end_of_stream;
        state_.interaction_mode = 0U;
        effects.push_back(MissionEffect::enter_gameplay_mode);
        return;
    }
    const auto& command = commands_[state_.command_index];
    if (state_.command_index > 0U && commands_[state_.command_index - 1U].opcode == 3U &&
        command.opcode != 3U)
        effects.push_back(MissionEffect::close_dialogue);
    state_.opcode = command.opcode;
    state_.dialogue.reset();
    state_.camera.reset();
    state_.objective.reset();
    state_.mission_objective.reset();
    state_.pending_text_id.reset();
    state_.stop=MissionStop::none;
    state_.stop_detail.clear();
    const auto& args = command.argument_words_u16;
    if (is_world_command(command.opcode)) {
        state_.gate=MissionGate::world_command;
        return;
    }
    switch (command.opcode) {
    case 3: {
        require_words(command, 2U);
        if (state_.skip_following_dialogue) { mark_ready(state_); return; }
        MissionDialogue dialogue;
        dialogue.speaker_type = signed16(args[0]);
        dialogue.text_index = signed16(args[1]);
        dialogue.text_id = script_text_id(dialogue.text_index, state_.alternate_dialogue_variant);
        if (dialogue.speaker_type == -1) dialogue.heading_text_id = 0x019dU;
        else {
            if (dialogue.speaker_type < 0 ||
                static_cast<std::size_t>(dialogue.speaker_type) >= raw_unit_name_indices_.size())
                throw FormatError("dialogue speaker type is outside the recovered definitions");
            dialogue.heading_text_id = script_text_id(unit_text_index(
                raw_unit_name_indices_[static_cast<std::size_t>(dialogue.speaker_type)]));
        }
        state_.phase_flag_225 = false;
        state_.original_phase = 3;
        state_.gate = MissionGate::dialogue;
        state_.dialogue = dialogue;
        return;
    }
    case 4:
        require_words(command, 3U);
        state_.original_phase = 4;
        state_.camera = WorldCameraRequest{signed16(args[0]), signed16(args[1]), args[2] != 0U};
        state_.gate = MissionGate::camera;
        return;
    case 5:
        require_words(command, 1U);
        state_.original_phase = 5;
        state_.remaining_delay_ms = signed16(args[0]);
        state_.gate = MissionGate::delay;
        return;
    case 1:
    case 2:
        require_words(command, 0U);
        state_.phase_flag_225 = false;
        state_.transition_value = 0;
        state_.original_phase = command.opcode;
        state_.gate = MissionGate::transition;
        return;
    case 12: {
        if (args.empty() || args[0] > 3U) {
            stop(MissionStop::unsupported_objective, "unknown original objective kind");
            return;
        }
        state_.mission_objective = decode_mission_objective(command);
        if (state_.mission_objective->kind == MissionObjectiveKind::site_item_owner) {
            const auto& objective = *state_.mission_objective;
            const auto& parameters = objective.parameters;
            if (parameters[0] < -1 || parameters[1] < -1 || parameters[2] < -1)
                throw FormatError("unsupported negative objective index");
            state_.objective = CapturedSiteObjective{parameters[0], parameters[1], parameters[2],
                parameters[3], objective.text_index, objective.text_id};
            state_.gate = MissionGate::captured_site_item;
        } else {
            state_.gate = MissionGate::objective;
        }
        state_.interaction_mode = 0U;
        state_.skip_following_dialogue = false; // Original cold path 0x5744c.
        // 0x56f5e does not write map+0x1e0. Script ticks are suspended by mode 0.
        effects.push_back(MissionEffect::enter_gameplay_mode);
        return;
    }
    case 8:
        require_words(command, 1U);
        state_.pending_text_id = script_text_id(signed16(args[0]));
        stop(MissionStop::completion_presentation_required,
             "opcode 8 requires its player-dependent completion presentation and audio handlers");
        return;
    case 10:
        require_words(command, 1U);
        stop(MissionStop::campaign_handoff_required,
             "opcode 10 requires actual profile persistence and results/campaign handoff");
        return;
    default:
        stop(MissionStop::unsupported_command, "mission command has no recovered continuation handler");
        return;
    }
}

MissionTickResult MissionExecution::tick(std::uint32_t delta_ms, const MissionTickInput& input) {
    MissionTickResult result{input.input_edges, {}};
    switch (state_.gate) {
    case MissionGate::advance_pending:
        ++state_.command_index;
        enter_current(result.effects);
        break;
    case MissionGate::dialogue: {
        if (!input.dialogue_layout)
            throw FormatError("dialogue tick requires actual renderer layout/reveal feedback");
        const auto update = advance_dialogue(state_.dialogue->progress,
            *input.dialogue_layout, input.input_edges, delta_ms);
        state_.dialogue->progress = update.progress;
        result.remaining_input_edges = update.remaining_input_edges;
        if (update.skip_audio_event) result.effects.push_back(MissionEffect::skip_dialogue_audio_event);
        if (update.progress.skip_following_dialogue) state_.skip_following_dialogue = true;
        if (update.progress.ready_for_next_command) mark_ready(state_);
        break;
    }
    case MissionGate::delay:
        state_.remaining_delay_ms = signed32(static_cast<std::uint32_t>(state_.remaining_delay_ms) - delta_ms);
        if (state_.remaining_delay_ms <= 0) mark_ready(state_);
        break;
    case MissionGate::transition: {
        const auto change = scaled_delta(delta_ms, 20000);
        if (state_.original_phase == 1) {
            state_.transition_value = wrap32(static_cast<std::int64_t>(state_.transition_value) + change);
            if (state_.transition_value >= 0) {
                state_.transition_value = 0;
                state_.phase_flag_225 = true;
                mark_ready(state_);
            }
        } else {
            state_.transition_value = wrap32(static_cast<std::int64_t>(state_.transition_value) - change);
            if (state_.transition_value < 0) {
                state_.transition_value = 0;
                mark_ready(state_);
            }
        }
        break;
    }
    case MissionGate::completion_frame:
        mark_ready(state_); // Original 0x57914 phase 8 has no acknowledgement test.
        break;
    case MissionGate::world_frame:
        mark_ready(state_); // Original 0x57930..0x57952:0/16/13/14/18/19/20.
        break;
    case MissionGate::world_command:
    case MissionGate::camera:
    case MissionGate::captured_site_item:
    case MissionGate::objective:
    case MissionGate::stopped:
    case MissionGate::end_of_stream:
        break;
    }
    return result;
}

const MissionCommand& MissionExecution::pending_world_command() const {
    if (state_.gate!=MissionGate::world_command || state_.command_index>=commands_.size() ||
        !is_world_command(state_.opcode))
        throw FormatError("no pending world command at this mission gate");
    return commands_[state_.command_index];
}

void MissionExecution::report_world_command_applied(std::size_t command_index) {
    const auto& command=pending_world_command();
    if (command_index!=state_.command_index)
        throw FormatError("world response does not match the pending mission command");
    switch (command.opcode) {
    case 0:
        state_.phase_flag_225=true;
        [[fallthrough]];
    case 13:case 14:case 16:case 18:case 19:case 20:
        state_.original_phase=command.opcode;
        state_.gate=MissionGate::world_frame;
        break;
    case 6:case 7:
        mark_ready(state_);
        break;
    case 9:case 15:case 17:case 21:
        // These entries do not write the phase. Normal dispatch arrives in99;
        // an incompatible custom state is not silently repaired here.
        if (state_.original_phase!=99)
            throw FormatError("phase-preserving world command did not enter from phase99");
        state_.gate=MissionGate::advance_pending;
        break;
    default:
        throw FormatError("world acknowledgement has no recovered phase transition");
    }
}

void MissionExecution::report_camera(std::size_t command_index, bool reached) {
    if (state_.gate != MissionGate::camera || command_index != state_.command_index)
        throw FormatError("camera response does not match the pending mission command");
    if (reached) mark_ready(state_);
}

MissionTickResult MissionExecution::observe_objective(const std::vector<MissionSiteState>& sites,
                                                     std::array<std::uint8_t, 2> active_counts) {
    if (state_.gate != MissionGate::captured_site_item || !state_.objective)
        throw FormatError("objective observation without an active capture goal");
    if (campaign_group_ != 0U || campaign_index_ != 0)
        throw FormatError("legacy capture observation is limited to the first campaign mission");
    MissionTickResult result;
    if (active_counts[0] == 0U || active_counts[1] == 0U) {
        stop(MissionStop::unsupported_player_loss_branch,
             "player-loss and alternate mission jumps require separate recovered behavior");
        return result;
    }
    if (captured_site_objective_satisfied(*state_.objective, sites)) {
        state_.interaction_mode = 6U;
        mark_ready(state_);
        result.effects.push_back(MissionEffect::reset_current_player_actions);
        result.effects.push_back(MissionEffect::return_to_script_mode);
    }
    return result;
}

MissionTickResult MissionExecution::observe_objective(const std::vector<MissionSiteState>& sites,
    const std::vector<MissionObjectiveUnit>& units, const CampaignContext& campaign,
    std::uint8_t current_owner) {
    if ((state_.gate != MissionGate::captured_site_item && state_.gate != MissionGate::objective) ||
        !state_.mission_objective)
        throw FormatError("objective observation without an active mission goal");
    if (campaign.group != campaign_group_ || campaign.mission_index != campaign_index_)
        throw FormatError("objective observation belongs to a different campaign mission");
    const auto decision = evaluate_mission_objective(*state_.mission_objective,
        {campaign, units, sites, commands_, state_.command_index, current_owner});
    MissionTickResult result;
    if (!decision.resume_script) return result;
    if (decision.command_cursor < 0)
        throw FormatError("objective transition search produced a negative controller cursor");
    state_.command_index = static_cast<std::size_t>(decision.command_cursor);
    state_.interaction_mode = 6U;
    if (decision.set_alternate_dialogue_variant) state_.alternate_dialogue_variant = true;
    mark_ready(state_);
    if (decision.reset_current_player_actions)
        result.effects.push_back(MissionEffect::reset_current_player_actions);
    result.effects.push_back(MissionEffect::return_to_script_mode);
    return result;
}

CompletionPresentation MissionExecution::begin_completion(std::size_t command_index,
                                                          const CampaignContext& context) {
    if (state_.gate != MissionGate::stopped ||
        state_.stop != MissionStop::completion_presentation_required ||
        command_index != state_.command_index || state_.opcode != 8U)
        throw FormatError("completion entry does not match the pending mission command");
    if (context.group != campaign_group_ || context.mission_index != campaign_index_)
        throw FormatError("mission controller received a different campaign context");
    const auto& command = commands_.at(command_index);
    require_words(command, 1U);
    auto result = mission_completion_presentation(context, signed16(command.argument_words_u16[0]));
    state_.original_phase = 8;
    state_.gate = MissionGate::completion_frame;
    state_.stop = MissionStop::none;
    state_.stop_detail.clear();
    state_.pending_text_id.reset();
    return result;
}

} // namespace g1
