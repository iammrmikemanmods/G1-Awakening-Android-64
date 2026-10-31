#pragma once

#include "g1/TerrainRuntime.hpp"

#include "g1/MissionProgression.hpp"
#include "g1/UnitSpawn.hpp"

#include <variant>

namespace g1 {

struct WorldPhaseCommand {
    std::optional<bool> phase_flag_225;
    std::optional<std::uint16_t> title_text_index;
    std::optional<std::uint16_t> title_text_id;
    std::uint8_t minimum_loading_stage{}; // Original app+0x78: at least1/2.
};

struct WorldMapInstall {
    std::uint32_t grid_resource_id{}, lookup_resource_id{};
    std::uint8_t style{};
    TerrainGrid grid;
    TerrainLookup effective_lookup{};
    bool style_changed{};
    std::optional<MissionAudioRequest> music_request;
    bool stop_music_if_config_a4_disabled{}; // Conditional original audio(3,0).
    // Applying opcode9 deactivates existing units, clears sites,
    // points, cost regions and both markers; preserves type/faction objects
    // and funds. Then current owner becomes0 and its real turn initializer
    // runs against the empty site network. Owner slots/counts reset; the map
    // cursor becomes(0,0). Rendering must finish before acknowledgement.
};

struct ScriptRandomChoice {
    std::uint8_t type{};
    GameRandom::State after;
    std::uint32_t words_consumed{};
};

// Exact4e53a + caller566d0..566f6 rejection sequence. Draws are signed words;
// candidate=abs(signed_word % definition_count). Faction uses definition
// scalar0, not available_types. Active candidates are retried. The input
// generator is unchanged; commit 'after' only when applying the placement.
// Empty candidates or a one-million-draw resource bound fail explicitly.
ScriptRandomChoice choose_random_script_unit(const std::vector<UnitDefinition>&,
    const std::vector<WorldUnit>& current_units, std::uint8_t faction, const GameRandom&);

struct WorldSpawnCommand {
    WorldUnit identity; // Resolved owner/faction; activation still required.
    WorldCoordinate destination;
    std::uint8_t terrain0{}, terrain2{};
    UnitSpawnModifiers modifiers;
    bool rebuild_owner_slots{}; // Explicit-type faction inversion changed byte0xb.
    std::optional<ScriptRandomChoice> random_choice;
    // Every application increments the original active-count byte, including
    // a repeated activation of an already-active type (actual mission323).
    std::uint8_t active_count_increment{1};
    // After activation, original opcode6 selects the first active owner slot
    // whose FORM0 byte12 !=1. That cursor update is separate from UI selection.
};

struct WorldSitesCommand { std::vector<WorldSite> sites; };
struct WorldAvailabilityCommand { std::array<bool,23> available{}; };
struct WorldCombatFlagCommand { bool value{}; };

struct WorldPointsCommand { std::vector<WorldSitePoint> points; };

enum class WorldMarkerSlot { field_254, field_25c };
struct WorldMarkerCommand {
    WorldMarkerSlot slot{};
    std::int16_t x{}, y{};
    std::uint8_t renderer_mode{}; // Original5c9ec mode2 or3.
    bool rebuild_shared_geometry{}; // Opcode19 also calls6549a(1,1).
};
struct WorldImmunityCommand { std::uint8_t type{}; bool value{}; };
struct WorldOriginalNoEffect {
    // The original parses opcode21's count+4N data but falls through dispatcher
    // 0x576e2..0x578e4. This is a verified original no-op, not an unknown-opcode
    // fallback. It must not be reinterpreted as movement-blocking rectangles.
};

using WorldCommandData = std::variant<WorldPhaseCommand,WorldMapInstall,WorldSpawnCommand,WorldSitesCommand,
    WorldAvailabilityCommand,WorldCombatFlagCommand,WorldPointsCommand,
    WorldMarkerCommand,WorldImmunityCommand,WorldOriginalNoEffect>;

struct WorldCommandPlan {
    std::size_t command_index{};
    std::uint16_t opcode{};
    // A value is the phase written by the original. nullopt preserves the
    // entering phase (normally99). Phase13/14/18/19/20 needs its own next tick.
    std::optional<std::int32_t> phase_after;
    WorldCommandData data;
};

bool is_world_command(std::uint16_t opcode) noexcept;

// Decodes/prepares only proven world-command branches. No game state, RNG,
// cursor, rendering or persistence is mutated. The caller must apply the
// typed operation to the actual session before acknowledging its command.
// Unknown commands, invalid indices and unsupported inconsistent state throw.
// Native saved sessions restore their own state; the original binary-profile
// filtering branch is not substituted with invented replay behavior.
WorldCommandPlan prepare_world_command(const Archive&, const WorldBootstrap&,
    const MissionCommand&, const CampaignContext&, const GameRandom* random = nullptr,
    bool config_damage_immunity = false);

struct CampaignBootstrap {
    WorldBootstrap world;
    // Final activation states, aligned exactly with world.units. They retain
    // special-placement HP/timers and reset semantics across repeated types.
    std::vector<UnitActivation> unit_activations;
    // Gameplay metadata has been applied in order to this result. Real audio,
    // title/marker presentation and renderer loading remain caller work; do
    // not reapply these plans as additional placements or funds debits.
    std::vector<WorldCommandPlan> applied_commands;
    GameRandom::State random_after;
    std::optional<TerrainRuntime> terrain;
};

// Cold script setup for any original campaign-table entry. Stops BEFORE the
// first camera/dialogue/delay/transition/objective/completion/handoff request,
// leaving it for MissionExecution. A camera can precede initial spawns, so an
// empty initial roster is valid. Only a cold initial map load is performed;
// later map replacement belongs to the live session command application.
// The input RNG is unchanged even on failure. Commit random_after together
// with the returned world, so randomized group2 spawns share the real stream.
CampaignBootstrap bootstrap_campaign(const Archive&, const CampaignContext&,
    const GameRandom&, bool config_damage_immunity = false,
    const TerrainTables* terrain_tables = nullptr);

} // namespace g1
