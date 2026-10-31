#pragma once

#include "g1/Archive.hpp"

#include <array>
#include <optional>

namespace g1 {

// Native data decoding only. No original executable or guest CPU is used.
// The original grid reader at 0x50522 reads width, height and width*height
// row-major bytes. The renderer additionally recognizes 255 as an empty cell.
struct TerrainGrid {
    std::uint16_t width{};
    std::uint16_t height{};
    Bytes tiles;

    std::uint8_t tile_at(std::uint16_t x, std::uint16_t y) const;
};

TerrainGrid decode_terrain_grid(const Bytes& bytes);

using TerrainLookup = std::array<std::uint8_t, 32>;

// Observed resource variant: leading marker 1 followed by 32 lookup bytes.
// Mission style selection and the original opcode-9 overrides are applied by
// World bootstrap, not this raw resource decoder.
TerrainLookup decode_terrain_lookup(const Bytes& bytes);

struct TerrainRule {
    // Original counted row fields. The native movement implementation consumes
    // field 0 as terrain cost and field 2 as its special-terrain flag. The
    // meaning of field 1 is intentionally not inferred by this decoder.
    std::array<std::uint8_t, 3> fields;
};

using TerrainRules = std::vector<TerrainRule>;

// Counted rows at 0x2a2ba, reached by resource306 through 0x2a714.
// The observed row width is exactly three bytes; all bytes must be consumed.
TerrainRules decode_terrain_rules(const Bytes& bytes);

struct TerrainVisualRecord {
    // Original record order: top, front (+Z), left (-X), right (+X).
    std::array<std::uint8_t, 4> atlas_cells;
    std::int8_t height_steps;
};

using TerrainStyleTable = std::array<TerrainVisualRecord, 32>;
using TerrainTables = std::array<TerrainStyleTable, 3>;

// Exactly 480 build-time-extracted data bytes: styles 0,1,2, then 32 five-byte
// records per style. Use tools/extract_terrain_tables.py with the original IPA.
// This is a port-owned data envelope, not an original on-disk file format.
TerrainTables decode_terrain_tables(const Bytes& bytes);
std::uint32_t terrain_atlas_resource(std::uint8_t style);

// Resource307 contains four consecutive counted tables of counted byte rows.
// Original loader0x2a85a writes these at shared config+28/+30/+38/+40.
// Table0 supplies per-style decoration tile IDs; table2's row count gates
// the style1 face14 route. Other row meanings remain raw rather than guessed.
struct TerrainAnimationConfig {
    std::array<std::vector<Bytes>, 4> tables;
};

TerrainAnimationConfig decode_terrain_animation_config(const Bytes& bytes);

enum class TerrainFace : std::uint8_t { top = 0, front = 1, left = 2, right = 3 };

struct TerrainCellShape {
    std::int32_t origin_x{}, origin_z{}; // Original integer world units.
    std::int32_t top{}, bottom{};
    std::uint8_t face_mask{0x10}; // front=1, right=2, left=8, top=16.
    std::uint8_t previous_mask{0x10}; // Previous cell in the same row.
    bool bevel{}; // Original style0 rule.
};

struct TerrainQuad {
    std::uint16_t tile_x{}, tile_y{};
    TerrainFace face{};
    std::uint8_t atlas_cell{};
    // Original four-vertex triangle-strip order. A triangle-list consumer uses
    // {0,1,2, 2,1,3}; no winding or axis inversion is applied here.
    std::array<std::array<std::int16_t, 3>, 4> positions;
    std::array<std::array<std::int16_t, 2>, 4> quantized_uv;
    // Original terrain mode1 transform. Base atlas cells are64x64 in512x256;
    // the separate animated surface uses32x32 cells in256x32.
    std::array<std::array<float, 2>, 4> normalized_uv;
};

// The observed single-atlas-cell path through 0x65310/0x71248/0x70f70.
// No camera, padding, animated-terrain routing or scene transform is included.
std::vector<TerrainQuad> terrain_cell_quads(const TerrainCellShape& shape,
                                           const TerrainVisualRecord& record);

struct TerrainCellSurfaces {
    std::vector<TerrainQuad> base;
    std::vector<TerrainQuad> animated;
};

// Isolated original style1 route0x6af02..0x6b1e8. Requires the ordinary top
// mask and no style0 bevel. A face14 top suppresses the entire base cell;
// eligible face14 sides use top-64, not the ordinary neighbor bottom.
TerrainCellSurfaces terrain_style1_cell_quads(const TerrainCellShape&,
    const TerrainVisualRecord&, bool face14_enabled, std::uint32_t decoration_slots);

struct TerrainAnimatedSurface {
    std::uint32_t atlas_resource_id{420}; // PNG256x32, key0xf4e02223.
    std::vector<TerrainQuad> quads;
    // Original transient-decoration pool capacity. Its random placement is
    // separate work; this is not a claim that its quads have been generated.
    std::uint32_t decoration_slots{};
};

struct TerrainAnimationState {
    std::uint32_t accumulated_ms{};
    std::uint8_t phase{}; // Original style1 phase0..3, initial0.
};

// One original0x6d624 update: add elapsed time, subtract250 and advance once
// if accumulated>=250. A large delta does NOT consume several phases at once.
// Invalid phase or signed32 accumulator overflow rejects without mutation.
bool advance_terrain_animation(TerrainAnimationState&, std::uint32_t delta_ms);
float terrain_animation_u_offset(const TerrainAnimationState&);

struct TerrainMesh {
    std::uint8_t style{};
    std::uint32_t atlas_resource_id{};
    // M3G positionScale at the original terrain finalizer (r1=1.0).
    float position_scale{0.01F};
    std::vector<TerrainQuad> quads;
    std::optional<TerrainAnimatedSurface> animated_surface;
};

// Generates only cells inside the decoded grid, with recovered neighbor-height
// masks and style0 bevels. Original camera-dependent outside padding is absent.
// Style1 requires the overload with decoded resource307 config so routing and
// the separate420 material are explicit. That surface's U translation is
// terrain_animation_u_offset(state); base terrain receives no translation.
// Empty-sentinel cells also require a later mesh-routing implementation; none
// occurs in the 22 original decoded grids, and this builder rejects them.
// Original execution oracles validate the lower face builder independently;
// scene placement, presentation and complete campaign rendering remain separate.
TerrainMesh build_terrain_mesh(const TerrainGrid& grid, const TerrainTables& tables,
                               std::uint8_t style);
TerrainMesh build_terrain_mesh(const TerrainGrid& grid, const TerrainTables& tables,
                               std::uint8_t style, const TerrainAnimationConfig& config);

} // namespace g1
