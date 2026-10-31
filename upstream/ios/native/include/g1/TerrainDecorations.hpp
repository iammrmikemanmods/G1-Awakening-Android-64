#pragma once

#include "g1/Terrain.hpp"

namespace g1 {

class GameRandom;

// Immutable data rebuilt from the same world resources without random draws.
// This profile follows the existing interior mesh's zero outside-padding,
// single-column-batch construction. Larger original batches are rejected.
struct TerrainDecorationLayout {
    std::uint16_t width{}, height{};
    Bytes initial_candidate_records; // Original status,x,y triplets; unused bytes127.
    std::uint32_t registered_candidates{};
    std::uint32_t mesh_quad_capacity{}; // Slots plus original overallocated fixed-face count.
};

struct TerrainDecorationSlot {
    std::uint8_t phase{};
    // Original byte offset, not a candidate index. -1 retains the original
    // initial strip geometry until a selection succeeds.
    std::int32_t candidate_byte_offset{-1};
};

// Complete mutable pool state. Persist these integer/byte fields together with
// the session's ONE shared GameRandom state. Restoring/validating/deriving quads
// consumes no draws; never call create again during checkpoint restoration.
struct TerrainDecorationState {
    TerrainAnimationState animation;
    Bytes candidate_records;
    std::vector<TerrainDecorationSlot> slots;
};

struct TerrainDecorationStep {
    bool phase_advanced{};
    std::uint32_t random_words{};
    std::vector<std::uint32_t> placed_slots;
};

TerrainDecorationLayout build_terrain_decoration_layout(const TerrainGrid&,
    const TerrainTables&, const TerrainAnimationConfig&);

// Original0x6b606 initialization: per-slot phase=i%4, then one actual shared
// random word per slot. The caller owns when a new world is constructed.
TerrainDecorationState create_terrain_decorations(const TerrainDecorationLayout&, GameRandom&);

void validate_terrain_decorations(const TerrainDecorationLayout&, const TerrainDecorationState&);

// Call once per simulation update in the recovered original update order,
// never from display rendering. Shares the same clock as fixed animated faces.
// All validation/unsupported-original-access failures leave state/RNG unchanged.
TerrainDecorationStep advance_terrain_decorations(const TerrainDecorationLayout&,
    TerrainDecorationState&, std::uint32_t delta_ms, GameRandom&);

// Read-only material420 quads. Assigned slots use original64ebc coordinates and
// atlas cell0; unassigned slots retain original71248 initial subdivision strips.
// Apply terrain_animation_u_offset(state.animation) to this material as well.
std::vector<TerrainQuad> terrain_decoration_quads(const TerrainDecorationLayout&,
    const TerrainDecorationState&, const TerrainGrid&, const TerrainTables&);

} // namespace g1
