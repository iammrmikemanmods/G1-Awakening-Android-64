#pragma once

#include "g1/MissionExecution.hpp"
#include "g1/MovementAction.hpp"
#include "g1/MovementArrival.hpp"
#include "g1/CaptureAction.hpp"
#include "g1/Combat.hpp"
#include "g1/TransformAction.hpp"
#include "g1/TurnProgression.hpp"
#include "g1/AIExecution.hpp"
#include "g1/HealAction.hpp"
#include "g1/UpgradeAction.hpp"
#include "g1/UnitSpawn.hpp"
#include "g1/WorldCommands.hpp"
#include "g1/UnitInteractions.hpp"
#include "g1/UndoAction.hpp"

#include <memory>

namespace g1 {

struct SessionCheckpoint;

enum class MoveRequestResult {
    started, script_controls_input, computer_controls_input, movement_in_progress, no_selected_unit,
    inactive_unit, other_player, action_used, already_moved,
    outside_map, occupied_destination, no_route,
};

struct SessionTick {
    MissionTickResult mission;
    std::optional<CompletionPresentation> completion;
    bool movement_finished{};
    bool world_changed{};
    bool terrain_changed{}; // Visual simulation; not a tactical-command revision.
    std::optional<TurnTransition> turn_transition;
    std::vector<MissionAudioRequest> audio;
    std::vector<std::uint32_t> message_ids;
};

struct SessionBattle {
    std::uint64_t token{};
    std::size_t attacker{},defender{};
    CombatUnitState attacker_before,defender_before;
    // Read-only tactical presentation before resolved damage removes units.
    // Pending battles cannot be checkpointed; this is not serialized state.
    std::vector<MovementUnitState> units_before;
    CombatExchange exchange;
};

enum class SessionFocusReason {
    turn_start, ai_move_destination, ai_attack_target, ai_capture_target,
    ai_heal_target, ai_production,
};
struct SessionFocusRequest {
    std::uint64_t token{};
    WorldCoordinate tile;
    std::optional<std::size_t> unit_index;
    SessionFocusReason reason{SessionFocusReason::turn_start};
    bool immediate{};
};

struct SessionAIAction {
    std::size_t actor{};
    std::optional<AIPlan> plan;
    std::optional<std::uint8_t> production_type;
    AICommandPhase phase{AICommandPhase::idle};
    std::int32_t elapsed_ms{};
    std::optional<std::uint64_t> focus_token;
    bool focus_presented{};
    bool battle_pending{};
};

struct SessionWorldPresentation {
    std::uint64_t token{};
    // A cold bootstrap has already consumed its setup commands; later world
    // operations retain the exact MissionExecution command until presentation.
    std::optional<std::size_t> command_index;
    std::vector<WorldCommandPlan> operations;
};

// Native integration of recovered mission, movement, capture, transformation
// and combat state. Host presentation acknowledgements never replace actual
// gameplay state or establish a completed campaign.
class GameSession {
public:
    explicit GameSession(WorldBootstrap, std::uint32_t random_seed, CampaignContext = {});
    // Archive must outlive this session. Bootstrap state and RNG are committed
    // together; its applied operations are presented without replaying writes.
    GameSession(const Archive&,CampaignBootstrap,CampaignContext = {});
    const WorldBootstrap& world() const noexcept { return world_; }
    const MissionExecutionState& mission() const noexcept { return mission_.state(); }
    const std::vector<MovementUnitState>& units() const noexcept { return units_; }
    const std::vector<MissionSiteState>& sites() const noexcept { return captures_.sites; }
    const CaptureNetwork& captures() const noexcept { return captures_; }
    const std::vector<CombatUnitState>& combat_units() const noexcept { return combat_; }
    const std::optional<std::size_t>& selected() const noexcept { return selected_; }
    std::uint8_t current_player() const noexcept { return turn_.current_player; }
    const TurnState& turn() const noexcept { return turn_; }
    CampaignContext campaign_context() const;
    MissionRecord mission_record() const noexcept;
    const std::optional<SessionBattle>& battle() const noexcept { return battle_; }
    const std::optional<SessionFocusRequest>& focus_request() const noexcept { return focus_; }
    const std::optional<SessionAIAction>& ai_action() const noexcept { return ai_; }
    const std::array<std::int32_t,2>& upgrade_slots() const noexcept { return upgrade_slots_; }
    const GameRandom& random() const noexcept { return random_; }
    const std::optional<SessionWorldPresentation>& world_presentation() const noexcept { return world_presentation_; }
    void report_world_presented(std::uint64_t token);
    std::uint64_t world_generation() const noexcept { return world_generation_; }
    const TerrainTables* terrain_tables() const noexcept { return terrain_ ? &terrain_->tables : nullptr; }
    const TerrainDecorationLayout* terrain_layout() const noexcept {
        return terrain_ && terrain_->layout ? &*terrain_->layout : nullptr;
    }
    const TerrainDecorationState* terrain_state() const noexcept {
        return terrain_ && terrain_->state ? &*terrain_->state : nullptr;
    }
    const TerrainSpriteDecorationLayout* terrain_sprite_layout() const noexcept {
        return terrain_ && terrain_->sprite_layout ? &*terrain_->sprite_layout : nullptr;
    }
    const TerrainSpriteAnimationBank* terrain_sprite_animations() const noexcept {
        return terrain_ && terrain_->sprite_animations ? &*terrain_->sprite_animations : nullptr;
    }
    const TerrainSpriteDecorationState* terrain_sprite_state() const noexcept {
        return terrain_ && terrain_->sprite_state ? &*terrain_->sprite_state : nullptr;
    }
    std::uint64_t terrain_generation() const noexcept { return terrain_generation_; }
    // Native presentation policy: acknowledge only a matching frame that was
    // actually presented. This does not stand in for an original logic gate.
    void report_focus_presented(std::uint64_t token);
    bool moving() const noexcept { return moving_.has_value(); }
    bool checkpoint_available() const noexcept;
    SessionCheckpoint make_checkpoint() const;
    static std::unique_ptr<GameSession> restore_checkpoint(const Archive&,const SessionCheckpoint&);

    // Selection is a native UI operation. Command admission still enforces
    // original active/current-player/action/spent rules before a move starts.
    bool select(std::size_t unit_index);
    void clear_selection() noexcept;
    std::optional<MovementRange> selected_movement_range() const;
    MoveRequestResult move_selected_to(WorldCoordinate);
    bool selected_can_capture() const;
    CaptureResult capture_selected();
    TransformEligibility selected_transform_eligibility() const;
    std::optional<TransformResult> transform_selected();
    bool selected_can_undo() const;
    std::optional<UndoResult> undo_selected();
    UpgradeEligibility selected_upgrade_eligibility() const;
    std::optional<UpgradeResult> upgrade_selected();
    std::vector<std::size_t> selected_heal_targets() const;
    std::optional<HealResult> heal_selected(std::size_t target);
    ProductionEligibility selected_production_eligibility(ProductionMode) const;
    std::vector<std::uint8_t> selected_production_candidates(ProductionMode) const;
    std::optional<ProductionPlacement> selected_production_placement(ProductionMode) const;
    // The actual touch type-list path and the original funds-checked button
    // path differ. The host must select its confirmation path explicitly.
    std::optional<ProductionResult> produce_selected(ProductionMode,std::uint8_t type,
        WorldCoordinate,ProductionConfirmation);
    std::vector<std::size_t> selected_attack_targets() const;
    bool attack_selected(std::size_t defender);
    // Called only after the host actually completes this battle presentation.
    // A stale token cannot unlock input for a later exchange.
    void report_battle_finished(std::uint64_t token);
    std::uint8_t transform_animation_pending(std::size_t index) const { return animation_pending_.at(index); }
    void clear_transform_animation_request(std::size_t index) { animation_pending_.at(index)=0; }
    std::uint64_t unit_transform_generation(std::size_t index) const { return transform_generation_.at(index); }
    bool unit_transform_waiting(std::size_t index) const { return transform_waiting_.at(index); }
    void report_transform_finished(std::size_t index,std::uint64_t generation);
    std::uint64_t unit_spawn_generation(std::size_t index) const { return spawn_generation_.at(index); }
    bool end_turn_available() const;
    std::optional<TurnTransition> end_turn();

    // One simulation update. The host supplies elapsed time independently of
    // its display refresh. MovementAction remains fixed-distance per update.
    SessionTick tick(std::uint32_t delta_ms, const MissionTickInput& = {});
    void report_camera(std::size_t command_index, bool reached);

private:
    GameSession(WorldBootstrap,GameRandom,CampaignContext,std::vector<UnitActivation>);
    WorldBootstrap world_;
    const Archive* archive_{};
    CampaignContext campaign_;
    MissionExecution mission_;
    std::vector<MovementUnitState> units_;
    CaptureNetwork captures_;
    std::vector<CombatUnitState> combat_;
    std::vector<std::uint8_t> animation_pending_;
    std::vector<std::uint64_t> transform_generation_,spawn_generation_;
    std::vector<bool> transform_waiting_;
    bool original_transform_gate_{};
    GameRandom random_;
    std::optional<std::size_t> selected_, moving_;
    std::unique_ptr<MovementAction> action_;
    TurnState turn_;
    std::optional<SessionBattle> battle_;
    std::uint64_t next_battle_token_{1};
    std::optional<SessionFocusRequest> focus_;
    std::uint64_t next_focus_token_{1};
    std::optional<SessionAIAction> ai_;
    std::array<std::int32_t,2> upgrade_slots_{{-1,-1}};
    std::optional<SessionWorldPresentation> world_presentation_;
    std::uint64_t next_world_token_{1},world_generation_{1};
    bool config_damage_immunity_{};
    std::int32_t death_counter_278_{},death_counter_27c_{};
    // Original map+0x7c bytes count activations, including repeated placement
    // of an already-active type. They cannot be derived from unique units.
    std::array<std::uint8_t,2> active_counts_{};
    std::vector<std::array<std::int32_t,4>> death_log_;
    std::optional<TerrainRuntime> terrain_;
    std::uint64_t terrain_generation_{1};

    void synchronize_unit(std::size_t);
    void apply_mission_effects(const MissionTickResult&);
    void finish_ordinary_arrival(std::size_t,SessionTick&);
    std::vector<MissionObjectiveUnit> objective_units() const;
    CombatRulesView combat_rules() const;
    void begin_battle(std::size_t attacker,std::size_t defender);
    TransformResult apply_transform(std::size_t,bool set_attack_gate=true);
    void request_focus(WorldCoordinate,SessionFocusReason,std::optional<std::size_t> = {});
    AIWorldView ai_world(const MovementBoard&) const;
    void initialize_upgrade_slots();
    UpgradeResult upgrade_actor(std::size_t);
    std::size_t find_or_create_unit(std::uint8_t type,std::uint8_t owner);
    void commit_activation(std::size_t,const UnitActivation&);
    void tick_ai(std::uint32_t,SessionTick&);
    void begin_ai_plan(std::size_t,const AIPlanningResult&,SessionTick&);
    void set_ai_phase(AICommandPhase);
    void finish_ai_arrival(std::size_t,SessionTick&);
    void clear_ai_action();
    TurnTransition advance_turn();
    void apply_world_command(const WorldCommandPlan&);
    void process_world_command(SessionTick&);
    bool selected_human_command_available() const;
    void finish_transport_arrival(std::size_t);
    void tick_terrain(std::uint32_t,SessionTick&);
};

// Native scheduling policy based on the observed original 30ms timer request.
// It makes the simulation independent of 60Hz drawing. The original's one-shot
// callback overhead is not inferred as an exact wall-clock 33.333Hz guarantee.
// Reset on background/resume; do not run catch-up turns while the app is away.
class SimulationClock {
public:
    static constexpr std::uint32_t step_ms = 30;
    std::uint32_t add_elapsed(std::uint32_t elapsed_ms) noexcept;
    float interpolation() const noexcept {
        return static_cast<float>(remainder_ms_) / static_cast<float>(step_ms);
    }
    void reset() noexcept { remainder_ms_ = 0; }
private:
    std::uint32_t remainder_ms_{};
};

} // namespace g1
