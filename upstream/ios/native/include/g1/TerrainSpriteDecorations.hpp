#pragma once

#include "g1/SpriteAnimation.hpp"
#include "g1/SpriteGeometry.hpp"
#include "g1/Terrain.hpp"

namespace g1 {

class GameRandom;

// Original selectors73..78 map to descriptors638,825,581,1095,1132,982
// through the validated sprite table. These are decoded immutable sequences;
// original descriptor/image bytes are supplied locally through Archive.
using TerrainSpriteAnimationBank = std::array<std::vector<std::vector<AtlasStep>>, 6>;
TerrainSpriteAnimationBank load_terrain_sprite_animations(const Archive&);

// Same bounded interior/zero-camera-padding profile as Terrain. This extends
// it with the original sprite constructor's front and side decoration rows.
// Immutable data is rebuilt without consuming random words during restore.
struct TerrainSpriteDecorationLayout {
    TerrainGrid grid;
    TerrainStyleTable visuals;
    TerrainLookup lookup;
    std::uint8_t style{}; // Only0 and2; style1 uses TerrainDecorations.
    Bytes initial_candidate_records;
    std::uint32_t registered_candidates{};
    std::uint32_t fixed_sprite_capacity{};
    // Entry i corresponds to original d8e90 index i+4. Original0..3 belong to
    // other renderer features and are not fabricated by this terrain module.
    std::vector<std::uint8_t> selectors;
};

enum class TerrainSpriteParent : std::uint8_t {
    unattached = 0,
    front_group = 1,      // Original d8e98[0].
    decoration_group = 2,// Original d8e98[9].
    terrain_group = 3    // Original d95ec, transient slots.
};

struct TerrainSpriteDecoration {
    std::uint8_t animation_index{};
    SpriteAnimationState playback;
    bool visible{}; // Original byte74 and the matching mesh enable call.
    TerrainSpriteParent parent{TerrainSpriteParent::unattached};
    // Original addChild call order. Zero means never attached; positive values
    // preserve sibling order without inventing complete parent-group traversal.
    std::uint32_t attachment_order{};
    // No initial pose is invented for an unplaced sprite. Visible but
    // unattached original nodes remain unattached, even when they have a pose.
    std::optional<std::array<std::int32_t, 3>> original_position;
};

// Complete native checkpoint state, persisted alongside the session's ONE
// GameRandom. Validation and drawing never restart sequences or consume RNG.
struct TerrainSpriteDecorationState {
    std::uint32_t accumulated_ms{};
    std::uint8_t phase{};
    Bytes candidate_records;
    std::vector<std::uint8_t> slot_phases;
    std::vector<TerrainSpriteDecoration> sprites;
};

struct TerrainSpriteDecorationStep {
    bool phase_advanced{};
    std::uint32_t random_words{};
    std::vector<std::uint32_t> relocated_slots;
};

TerrainSpriteDecorationLayout build_terrain_sprite_decoration_layout(
    const TerrainGrid&, const TerrainTables&, const TerrainAnimationConfig&,
    const TerrainLookup&, std::uint8_t style);

// Executes recovered stage1 reservation draws, then stage2 sprite placement
// and animation-selection draws. The original opcode9 completes these stages
// BEFORE following opcode6 spawns. Do not append these draws after bootstrap.
TerrainSpriteDecorationState create_terrain_sprite_decorations(
    const TerrainSpriteDecorationLayout&, const TerrainSpriteAnimationBank&, GameRandom&);

void validate_terrain_sprite_decorations(const TerrainSpriteDecorationLayout&,
    const TerrainSpriteAnimationBank&, const TerrainSpriteDecorationState&);

// Simulation-only: advance every transient sprite, then the single-subtraction
// 250ms/8-phase reservation cycle, then visible front sprites. Later static
// decorations retain their selected initial frame. Unsupported original
// accesses or malformed state leave both supplied state and RNG unchanged.
TerrainSpriteDecorationStep advance_terrain_sprite_decorations(
    const TerrainSpriteDecorationLayout&, const TerrainSpriteAnimationBank&,
    TerrainSpriteDecorationState&, std::uint32_t delta_ms, GameRandom&);

struct TerrainSpriteDecorationDraw {
    std::uint32_t original_index{};
    SpriteResource resource;
    std::uint8_t frame_index{};
    TerrainSpriteParent parent;
    std::uint32_t attachment_order{};
    std::array<float, 16> model; // Column-major T(raw/100)*Rx(-45), scale1.
};

// Original727a4 scale1 transform, with its recovered quaternion basis. The
// matrix-core reference uses host sinf/cosf at the external libm boundary;
// historical iOS libm pixel identity and native GPU fidelity are not claimed.
std::array<float, 16> terrain_sprite_decoration_model(
    const std::array<std::int32_t, 3>& original_position);

// Returns original attachment-call order. This preserves sibling ordering;
// ordering these parent groups against the rest of the scene is the consumer's
// scene-graph responsibility, not a claimed reconstruction of every render pass.
std::vector<TerrainSpriteDecorationDraw> terrain_sprite_decoration_draws(
    const TerrainSpriteDecorationLayout&, const TerrainSpriteAnimationBank&,
    const TerrainSpriteDecorationState&);

} // namespace g1
