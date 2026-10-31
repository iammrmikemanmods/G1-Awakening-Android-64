#pragma once

#include "g1/TerrainDecorations.hpp"
#include "g1/TerrainSpriteDecorations.hpp"
#include "g1/World.hpp"

namespace g1 {

// Session-owned visual simulation. The renderer only reads it. The immutable
// tables are packaged data extracted from the user's original game, not code.
struct TerrainRuntime {
    TerrainTables tables;
    // Style1 uses animated terrain quads; styles0/2 use original sprite pools.
    // Exactly one pair of layout/state is populated for the installed style.
    std::optional<TerrainDecorationLayout> layout;
    std::optional<TerrainDecorationState> state;
    std::optional<TerrainSpriteDecorationLayout> sprite_layout;
    std::optional<TerrainSpriteAnimationBank> sprite_animations;
    std::optional<TerrainSpriteDecorationState> sprite_state;
};

std::uint32_t terrain_tables_checksum(const TerrainTables&);
void validate_terrain_runtime(const TerrainRuntime&,std::uint8_t style);
// Construct at opcode9, before later random unit-placement commands. The
// shared generator is committed only after the whole terrain state validates.
TerrainRuntime create_terrain_runtime(const Archive&,const WorldBootstrap&,
    const TerrainTables&,GameRandom&);
// Rebuild immutable layout and validate saved bytes without consuming RNG.
TerrainRuntime restore_terrain_runtime(const Archive&,const WorldBootstrap&,
    const TerrainTables&,const std::optional<TerrainDecorationState>&,
    const std::optional<TerrainSpriteDecorationState>& = std::nullopt);

} // namespace g1
