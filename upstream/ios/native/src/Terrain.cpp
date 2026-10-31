#include "g1/Terrain.hpp"

#include <algorithm>
#include <limits>
#include <utility>

namespace g1 {
namespace {

void validate_grid(const TerrainGrid& grid) {
    if (grid.width == 0 || grid.width > 255 || grid.height == 0 || grid.height > 255)
        throw FormatError("terrain grid dimensions must be 1..255");
    if (grid.tiles.size() != static_cast<std::size_t>(grid.width) * grid.height)
        throw FormatError("terrain grid payload does not match dimensions");
    for (const auto tile : grid.tiles)
        if (tile > 31 && tile != 255)
            throw FormatError("terrain tile is outside the observed table or empty sentinel");
}

std::int16_t narrow_position(std::int64_t value) {
    if (value < std::numeric_limits<std::int16_t>::min() ||
        value > std::numeric_limits<std::int16_t>::max())
        throw FormatError("terrain coordinate exceeds the supported signed16 range");
    return static_cast<std::int16_t>(value);
}

std::array<std::int16_t, 3> position(std::int64_t x, std::int64_t y, std::int64_t z) {
    return {narrow_position(x), narrow_position(y), narrow_position(z)};
}

std::array<std::array<std::int16_t, 2>, 4> cell_uv(std::uint8_t cell, bool animated = false) {
    if (cell >= (animated ? 8 : 32)) throw FormatError("terrain atlas cell exceeds its atlas");
    // Original 0x714c0 uses the WIDTH-derived inset on both texture axes.
    const int u = (cell % 8) * 8192 - 32768;
    const int stride_v = animated ? 65536 : 16384;
    const int v = (cell / 8) * stride_v - 32768;
    const int inset = animated ? 256 : 128;
    return {{{static_cast<std::int16_t>(u + inset), static_cast<std::int16_t>(v + inset)},
             {static_cast<std::int16_t>(u + inset), static_cast<std::int16_t>(v + stride_v - inset - 1)},
             {static_cast<std::int16_t>(u + 8192 - inset - 1), static_cast<std::int16_t>(v + inset)},
             {static_cast<std::int16_t>(u + 8192 - inset - 1),
              static_cast<std::int16_t>(v + stride_v - inset - 1)}}};
}

TerrainQuad make_quad(TerrainFace face, std::uint8_t cell,
                       const std::array<std::array<std::int16_t, 3>, 4>& positions,
                       bool animated = false) {
    TerrainQuad result{};
    result.face = face;
    result.atlas_cell = cell;
    result.positions = positions;
    result.quantized_uv = cell_uv(cell, animated);
    // 0x71884 mode1, with the single-cell subdivision ratio exactly1.
    constexpr float bias = 0.50000762939453125F; // Original bits 0x3f000080.
    constexpr float scale = 1.0F / 65535.0F;
    for (std::size_t i = 0; i < 4; ++i)
        for (std::size_t j = 0; j < 2; ++j)
            result.normalized_uv[i][j] = static_cast<float>(result.quantized_uv[i][j]) * scale + bias;
    return result;
}

void validate_animation_config(const TerrainAnimationConfig& config) {
    if (config.tables[0].size() != 3)
        throw FormatError("terrain animation config requires three style rows");
    for (const auto& table : config.tables) {
        if (table.size() > 255) throw FormatError("terrain animation row count exceeds original byte");
        for (const auto& row : table)
            if (row.size() > 255) throw FormatError("terrain animation row width exceeds original byte");
    }
}

} // namespace

std::uint8_t TerrainGrid::tile_at(std::uint16_t x, std::uint16_t y) const {
    if (width == 0 || height == 0 || width > 255 || height > 255 ||
        tiles.size() != static_cast<std::size_t>(width) * height ||
        x >= width || y >= height)
        throw FormatError("terrain grid coordinate or shape is invalid");
    return tiles[static_cast<std::size_t>(y) * width + x];
}

TerrainGrid decode_terrain_grid(const Bytes& bytes) {
    if (bytes.size() < 2) throw FormatError("terrain grid header is truncated");
    if (bytes.size() != 2 + static_cast<std::size_t>(bytes[0]) * bytes[1])
        throw FormatError("terrain grid payload does not match dimensions");
    TerrainGrid result{bytes[0], bytes[1], Bytes(bytes.begin() + 2, bytes.end())};
    validate_grid(result);
    return result;
}

TerrainLookup decode_terrain_lookup(const Bytes& bytes) {
    if (bytes.size() != 33 || bytes[0] != 1)
        throw FormatError("terrain lookup requires observed marker 1 and 32 entries");
    TerrainLookup result{};
    std::copy(bytes.begin() + 1, bytes.end(), result.begin());
    return result;
}

TerrainRules decode_terrain_rules(const Bytes& bytes) {
    if (bytes.empty() || bytes[0] == 0) throw FormatError("terrain rules require counted rows");
    const auto count = static_cast<std::size_t>(bytes[0]);
    if (bytes.size() != 1 + count * 4)
        throw FormatError("terrain rule count does not match three-byte rows");
    TerrainRules result;
    result.reserve(count);
    for (std::size_t i = 0; i < count; ++i) {
        const auto offset = 1 + i * 4;
        if (bytes[offset] != 3) throw FormatError("terrain rule row width is unsupported");
        result.push_back({{bytes[offset + 1], bytes[offset + 2], bytes[offset + 3]}});
    }
    return result;
}

TerrainTables decode_terrain_tables(const Bytes& bytes) {
    if (bytes.size() != 480) throw FormatError("terrain visual tables require exactly 480 data bytes");
    TerrainTables result{};
    std::size_t offset = 0;
    for (auto& style : result) {
        for (auto& record : style) {
            for (auto& cell : record.atlas_cells) {
                cell = bytes[offset++];
                if (cell >= 32) throw FormatError("terrain table has an unsupported atlas selector");
            }
            const auto raw = bytes[offset++];
            record.height_steps = static_cast<std::int8_t>(raw < 128 ? raw : static_cast<int>(raw) - 256);
        }
    }
    return result;
}

std::uint32_t terrain_atlas_resource(std::uint8_t style) {
    constexpr std::array<std::uint32_t, 3> ids{693, 777, 761};
    if (style >= ids.size()) throw FormatError("terrain style is outside the recovered table");
    return ids[style];
}

TerrainAnimationConfig decode_terrain_animation_config(const Bytes& bytes) {
    TerrainAnimationConfig result;
    std::size_t cursor = 0;
    const auto byte = [&]() {
        if (cursor >= bytes.size()) throw FormatError("terrain animation config is truncated");
        return bytes[cursor++];
    };
    for (auto& table : result.tables) {
        const auto count = byte();
        table.reserve(count);
        for (std::size_t i = 0; i < count; ++i) {
            const auto length = byte();
            if (length > bytes.size() - cursor)
                throw FormatError("terrain animation row is truncated");
            table.emplace_back(bytes.begin() + static_cast<std::ptrdiff_t>(cursor),
                               bytes.begin() + static_cast<std::ptrdiff_t>(cursor + length));
            cursor += length;
        }
    }
    if (cursor != bytes.size()) throw FormatError("terrain animation config has trailing data");
    validate_animation_config(result);
    return result;
}

std::vector<TerrainQuad> terrain_cell_quads(const TerrainCellShape& shape,
                                           const TerrainVisualRecord& record) {
    constexpr std::uint8_t valid_mask = 0x1b;
    if ((shape.face_mask & ~valid_mask) != 0 || (shape.previous_mask & ~valid_mask) != 0)
        throw FormatError("terrain face mask contains an unsupported bit");
    for (const auto cell : record.atlas_cells)
        if (cell >= 32) throw FormatError("terrain cell contains an unsupported atlas selector");
    const std::int64_t x0 = static_cast<std::int64_t>(shape.origin_x) +
        ((shape.bevel && (shape.previous_mask & 2)) ? 16 : 0);
    const std::int64_t x1 = static_cast<std::int64_t>(shape.origin_x) + 32;
    const std::int64_t z0 = shape.origin_z, z1 = z0 + 32;
    const auto low_left = x0 - ((shape.bevel && (shape.face_mask & 8)) ? 16 : 0);
    const auto low_right = x1 + ((shape.bevel && (shape.face_mask & 2)) ? 16 : 0);
    const auto top = shape.top, bottom = shape.bottom;
    std::vector<TerrainQuad> result;
    result.reserve(4);
    // Preserve original append order, which differs from record field order.
    if (shape.face_mask & 1)
        result.push_back(make_quad(TerrainFace::front, record.atlas_cells[1],
            {position(x0, top, z1), position(low_left, bottom, z1),
             position(x1, top, z1), position(low_right, bottom, z1)}));
    if (shape.face_mask & 2)
        result.push_back(make_quad(TerrainFace::right, record.atlas_cells[3],
            {position(x1, top, z1), position(low_right, bottom, z1),
             position(x1, top, z0), position(low_right, bottom, z0)}));
    if (shape.face_mask & 8)
        result.push_back(make_quad(TerrainFace::left, record.atlas_cells[2],
            {position(x0, top, z0), position(low_left, bottom, z0),
             position(x0, top, z1), position(low_left, bottom, z1)}));
    if (shape.face_mask & 16)
        result.push_back(make_quad(TerrainFace::top, record.atlas_cells[0],
            {position(x0, top, z0), position(x0, top, z1),
             position(x1, top, z0), position(x1, top, z1)}));
    return result;
}

TerrainCellSurfaces terrain_style1_cell_quads(const TerrainCellShape& shape,
    const TerrainVisualRecord& record, bool face14_enabled, std::uint32_t decoration_slots) {
    if (shape.bevel || (shape.face_mask & 16) == 0)
        throw FormatError("style1 terrain requires its top face and no style0 bevel");
    TerrainCellSurfaces result{terrain_cell_quads(shape, record), {}};
    if (!face14_enabled) return result;
    const auto x0 = static_cast<std::int64_t>(shape.origin_x), x1 = x0 + 32;
    const auto z0 = static_cast<std::int64_t>(shape.origin_z), z1 = z0 + 32;
    const auto top = static_cast<std::int64_t>(shape.top), bottom = top - 64;
    const std::uint8_t cell = decoration_slots == 0 ? 0 : 4;
    // Original routing iterates record order, independently of the base face
    // append order. A routed top marks the whole original cell255.
    if (record.atlas_cells[0] == 14) {
        result.animated.push_back(make_quad(TerrainFace::top, cell,
            {position(x0, top, z0), position(x0, top, z1),
             position(x1, top, z0), position(x1, top, z1)}, true));
        result.base.clear();
    } else {
        result.base.erase(std::remove_if(result.base.begin(), result.base.end(),
            [](const auto& quad) { return quad.atlas_cell == 14; }), result.base.end());
    }
    if (record.atlas_cells[1] == 14 && (shape.face_mask & 1))
        result.animated.push_back(make_quad(TerrainFace::front, cell,
            {position(x0, top, z1), position(x0, bottom, z1),
             position(x1, top, z1), position(x1, bottom, z1)}, true));
    if (record.atlas_cells[2] == 14 && (shape.face_mask & 8))
        result.animated.push_back(make_quad(TerrainFace::left, cell,
            {position(x0, top, z0), position(x0, bottom, z0),
             position(x0, top, z1), position(x0, bottom, z1)}, true));
    if (record.atlas_cells[3] == 14 && (shape.face_mask & 2))
        result.animated.push_back(make_quad(TerrainFace::right, cell,
            {position(x1, top, z1), position(x1, bottom, z1),
             position(x1, top, z0), position(x1, bottom, z0)}, true));
    return result;
}

bool advance_terrain_animation(TerrainAnimationState& state, std::uint32_t delta_ms) {
    constexpr auto maximum = static_cast<std::uint32_t>(std::numeric_limits<std::int32_t>::max());
    if (state.phase >= 4 || state.accumulated_ms > maximum || delta_ms > maximum - state.accumulated_ms)
        throw FormatError("terrain animation phase or signed32 time range is invalid");
    const auto sum = state.accumulated_ms + delta_ms;
    const bool advanced = sum >= 250;
    state.accumulated_ms = advanced ? sum - 250 : sum;
    if (advanced) state.phase = static_cast<std::uint8_t>((state.phase + 1U) % 4U);
    return advanced;
}

float terrain_animation_u_offset(const TerrainAnimationState& state) {
    if (state.phase >= 4) throw FormatError("terrain animation phase is outside0..3");
    return static_cast<float>(state.phase) * 0.125F;
}

namespace {
TerrainMesh build_mesh(const TerrainGrid& grid, const TerrainTables& tables,
                       std::uint8_t style, const TerrainAnimationConfig* config) {
    validate_grid(grid);
    const auto atlas = terrain_atlas_resource(style);
    if (style == 1 && config == nullptr)
        throw FormatError("style1 terrain requires decoded resource307 animation config");
    if (std::find(grid.tiles.begin(), grid.tiles.end(), 255) != grid.tiles.end())
        throw FormatError("empty-sentinel terrain mesh routing is outside the observed grid corpus");
    const auto& visuals = tables[style];
    TerrainMesh result;
    result.style = style;
    result.atlas_resource_id = atlas;
    bool animate_faces = false;
    if (style == 1) {
        validate_animation_config(*config);
        const auto& ids = config->tables[0][1];
        std::uint32_t candidates = 0;
        // 0x6a434..0x6a766 scans actual columns and three extra bottom rows,
        // clamping those rows to the final source row. Each cell counts once.
        const auto counts = [&](std::uint8_t tile) {
            return std::find(ids.begin(), ids.end(), tile) != ids.end();
        };
        for (const auto tile : grid.tiles) if (counts(tile)) ++candidates;
        for (std::uint16_t x = 0; x < grid.width; ++x)
            if (counts(grid.tile_at(x, grid.height - 1))) candidates += 3;
        result.animated_surface.emplace();
        result.animated_surface->decoration_slots = candidates / 7;
        animate_faces = !config->tables[2].empty();
    }
    result.quads.reserve(grid.tiles.size() * 2);
    for (std::uint16_t y = 0; y < grid.height; ++y) {
        std::uint8_t previous = 0x10;
        for (std::uint16_t x = 0; x < grid.width; ++x) {
            const auto tile = grid.tile_at(x, y);
            const auto& record = visuals[tile];
            const auto h = static_cast<std::int32_t>(record.height_steps);
            std::uint8_t mask = 0x10;
            std::int32_t low = 0;
            const auto lower_neighbor = [&](std::uint8_t neighbor, std::uint8_t bit) {
                if (neighbor != 255 && h > visuals[neighbor].height_steps) {
                    mask = static_cast<std::uint8_t>(mask | bit);
                    low = std::min(low, static_cast<std::int32_t>(visuals[neighbor].height_steps));
                }
            };
            if (x > 0) lower_neighbor(grid.tile_at(x - 1, y), 8);
            if (y + 1 < grid.height) lower_neighbor(grid.tile_at(x, y + 1), 1);
            else if (style == 0) lower_neighbor(13, 1);
            if (x + 1 < grid.width) lower_neighbor(grid.tile_at(x + 1, y), 2);
            const TerrainCellShape shape{static_cast<std::int32_t>(x) * 32,
                static_cast<std::int32_t>(y) * 32, h * 32, low * 32, mask, previous, style == 0};
            std::vector<TerrainQuad> quads;
            if (style == 1) {
                auto cell = terrain_style1_cell_quads(shape, record, animate_faces,
                                                      result.animated_surface->decoration_slots);
                for (auto& quad : cell.animated) { quad.tile_x = x; quad.tile_y = y; }
                auto& animated = result.animated_surface->quads;
                animated.insert(animated.end(), cell.animated.begin(), cell.animated.end());
                quads = std::move(cell.base);
            } else {
                quads = terrain_cell_quads(shape, record);
            }
            for (auto& quad : quads) { quad.tile_x = x; quad.tile_y = y; }
            result.quads.insert(result.quads.end(), quads.begin(), quads.end());
            previous = mask;
        }
    }
    if (result.animated_surface && result.animated_surface->quads.empty() &&
        result.animated_surface->decoration_slots == 0)
        result.animated_surface.reset();
    return result;
}
} // namespace

TerrainMesh build_terrain_mesh(const TerrainGrid& grid, const TerrainTables& tables,
                               std::uint8_t style) {
    return build_mesh(grid, tables, style, nullptr);
}

TerrainMesh build_terrain_mesh(const TerrainGrid& grid, const TerrainTables& tables,
                               std::uint8_t style, const TerrainAnimationConfig& config) {
    return build_mesh(grid, tables, style, &config);
}

} // namespace g1
