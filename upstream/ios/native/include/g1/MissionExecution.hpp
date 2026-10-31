#pragma once

#include "g1/Text.hpp"
#include "g1/World.hpp"
#include "g1/MissionProgression.hpp"
#include "g1/MissionObjectives.hpp"

namespace g1 {

// Values supplied by the text renderer, not guessed from the character count.
// Original font virtual methods +8 and +12 supply metric_a and metric_b.
struct DialogueLayout {
    std::int32_t line_count{};
    std::int32_t line_height{};
    std::int32_t metric_a{};
    std::int32_t metric_b{};
    std::int32_t box_height{};
    bool visible_text_fully_revealed{}; // Original draw result map+0x1fc.
};

struct DialogueProgress {
    std::int32_t first_line{};
    std::int32_t reveal_offset{-65536000}; // Original map+0x26c.
    bool ready_for_next_command{};
    bool skip_following_dialogue{};
};

struct DialogueUpdate {
    DialogueProgress progress;
    std::uint32_t remaining_input_edges{};
    bool more_pages{};
    bool skip_audio_event{}; // Original 0x2ad12(app, 5, 18) request.
};

// Direct translation of 0x4e3bc, including 0x2b9c2's consume-on-first-match
// input behavior. Masks: 1 back, 16/64 advance, 128 skip. A match clears the
// whole edge word, so simultaneous bits are not independent button presses.
// No input gesture, layout, font, text drawing or audio is synthesized here.
DialogueUpdate advance_dialogue(const DialogueProgress&, const DialogueLayout&,
                                std::uint32_t input_edges, std::uint32_t delta_ms);

// Original draw-side character budget at 0x5b9aa..0x5b9ae. The renderer still
// determines page clipping and whether all visible text has been drawn.
std::int32_t dialogue_character_budget(const DialogueProgress&) noexcept;

struct MissionDialogue {
    std::int16_t speaker_type{}; // -1 is the original tutorial-heading branch.
    std::int16_t text_index{};
    std::uint16_t text_id{};
    std::uint16_t heading_text_id{};
    DialogueProgress progress;
};

enum class MissionGate {
    dialogue,
    camera,
    delay,
    transition,
    captured_site_item,
    objective,
    world_command,
    world_frame,
    completion_frame,
    advance_pending,
    stopped,
    end_of_stream,
};

enum class MissionStop {
    none,
    unsupported_command,
    unsupported_objective,
    unsupported_player_loss_branch,
    completion_presentation_required,
    campaign_handoff_required,
};

enum class MissionEffect {
    close_dialogue,
    skip_dialogue_audio_event, // 0x2ad12(app, 5, 18).
    enter_gameplay_mode,      // Original 0x553ee; broader gameplay setup external.
    reset_current_player_actions, // 0x55840(false) -> 0x430fc per current-player unit.
    return_to_script_mode,
};

struct MissionExecutionState {
    std::size_t command_index{};
    std::uint16_t opcode{};
    std::int32_t original_phase{}; // 99 is pending advance while in script mode.
    std::uint8_t interaction_mode{6}; // 6 script, 0 gameplay; goal gate suspends script ticks.
    MissionGate gate{MissionGate::stopped};
    MissionStop stop{MissionStop::none};
    std::string stop_detail;
    bool phase_flag_225{};
    bool skip_following_dialogue{};
    bool alternate_dialogue_variant{}; // Original map+0x231.
    std::int32_t transition_value{};
    std::int32_t remaining_delay_ms{};
    // Only the active gate should be presented. As in the original direct
    // end-of-stream -> gameplay path, backing dialogue data may outlive it.
    std::optional<MissionDialogue> dialogue;
    std::optional<WorldCameraRequest> camera;
    std::optional<CapturedSiteObjective> objective;
    std::optional<MissionObjective> mission_objective; // All four original kinds.
    std::optional<std::uint16_t> pending_text_id;
};

struct MissionTickInput {
    std::uint32_t input_edges{};
    std::optional<DialogueLayout> dialogue_layout;
};

struct MissionTickResult {
    std::uint32_t remaining_input_edges{};
    std::vector<MissionEffect> effects;
};

// Native saves are taken only at a stationary opcode12 gameplay boundary.
// The objective is re-decoded from the matching archive, never supplied by
// the save. These backing controller values survive while that gate is open.
struct MissionGameplayCheckpoint {
    std::size_t command_index{};
    std::int32_t original_phase{};
    bool phase_flag_225{};
    bool alternate_dialogue_variant{};
    std::int32_t transition_value{};
    std::int32_t remaining_delay_ms{};
};

// Native control-flow continuation from a decoded campaign execution boundary.
// It owns no renderer, audio, combat, save system or original machine code.
// The one-argument constructor retains the cold first-campaign default; other
// campaign tables require explicit matching context. It returns typed requests.
// Clients must implement the returned effects; receiving an effect is not
// evidence that its gameplay/UI work has been performed.
//
// tick() performs one original phase update. Phase 99 advances the cursor on
// the following tick, without carrying unused delay time into another command.
// Camera callbacks are explicit because 0x557f4 delegates completion to the
// renderer. Goal observations are separate from clock ticks, as in the game.
// Opcode 8 stops until begin_completion receives actual campaign/world state.
// Opcode 10 stops for actual profile persistence and results/handoff handling
// through MissionProgression. No API labels this partial flow as a playthrough.
class MissionExecution {
public:
    explicit MissionExecution(const WorldBootstrap&);
    MissionExecution(const WorldBootstrap&, const CampaignContext&);
    const MissionExecutionState& state() const noexcept { return state_; }
    MissionGameplayCheckpoint gameplay_checkpoint() const;
    // Requires an already reconstructed matching objective; does not advance
    // any command, satisfy a goal, reset units or acknowledge presentation.
    void restore_gameplay_checkpoint(const MissionGameplayCheckpoint&);
    MissionTickResult tick(std::uint32_t delta_ms, const MissionTickInput& = {});

    // A general bootstrap may begin directly at an objective. Its real entry
    // effects are retained here rather than discarded by the constructor.
    // Drain once during session initialization; subsequent calls are empty.
    std::vector<MissionEffect> take_initial_effects();

    // WorldCommands prepares this exact decoded command. The native session
    // must apply its gameplay changes and required external work before this
    // acknowledgement. UI/time alone cannot advance a pending world command.
    const MissionCommand& pending_world_command() const;
    void report_world_command_applied(std::size_t command_index);

    // The command token prevents a stale renderer response from completing a
    // later camera request. false retains the current camera gate.
    void report_camera(std::size_t command_index, bool reached);

    // Legacy first-campaign capture-only observation. Loss/alternate branches
    // stop explicitly. Use the full-state overload below for recovered general
    // behavior. Counts and item states must come from actual gameplay.
    MissionTickResult observe_objective(const std::vector<MissionSiteState>&,
                                       std::array<std::uint8_t, 2> active_unit_counts);

    // Complete original objective/control-flow decision for all four kinds.
    // Uses current native state, including inactive unit objects indexed by
    // original type. The campaign group/index must match this script.
    MissionTickResult observe_objective(const std::vector<MissionSiteState>&,
        const std::vector<MissionObjectiveUnit>& units_by_type,
        const CampaignContext&, std::uint8_t current_owner);

    // Consumes only the currently stopped opcode-8 entry. Returns the original
    // state-dependent audio/text requests, which the caller must implement.
    // It is not a user acknowledgement or a way to satisfy a gameplay goal.
    // Phase 8 becomes 99 on the next tick; the following tick dispatches the
    // next command. The token rejects stale or repeated presentation entries.
    CompletionPresentation begin_completion(std::size_t command_index,
                                            const CampaignContext&);

private:
    std::vector<MissionCommand> commands_;
    std::vector<std::uint16_t> raw_unit_name_indices_;
    std::uint8_t campaign_group_{};
    std::int32_t campaign_index_{};
    MissionExecutionState state_;
    std::vector<MissionEffect> initial_effects_;
    void enter_current(std::vector<MissionEffect>&);
    void stop(MissionStop, const std::string&);
};

} // namespace g1
