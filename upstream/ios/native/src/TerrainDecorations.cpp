#include "g1/TerrainDecorations.hpp"
#include "g1/Combat.hpp"

#include <algorithm>
#include <limits>
#include <utility>

namespace g1 {
namespace {

std::uint32_t slot_count(const TerrainDecorationLayout& layout) {
    if (!layout.width || !layout.height || layout.width > 127 || layout.height > 127 ||
        layout.initial_candidate_records.size() % 3 != 0 ||
        layout.initial_candidate_records.size() > 3 * 889U)
        throw FormatError("terrain decoration layout exceeds recovered signed-byte storage");
    const auto capacity = layout.initial_candidate_records.size() / 3;
    const auto slots = static_cast<std::uint32_t>(capacity / 7);
    if (slots >= 127 || layout.registered_candidates > capacity ||
        layout.mesh_quad_capacity < slots || layout.mesh_quad_capacity > 65535)
        throw FormatError("terrain decoration layout count is invalid");
    for (std::size_t i = 0; i < capacity; ++i) {
        const auto at = i * 3;
        if (i < layout.registered_candidates) {
            if (layout.initial_candidate_records[at] != 255 ||
                layout.initial_candidate_records[at + 1] >= layout.width ||
                layout.initial_candidate_records[at + 2] >= layout.height)
                throw FormatError("terrain decoration candidate coordinates are invalid");
        } else if (layout.initial_candidate_records[at] != 127 ||
                   layout.initial_candidate_records[at + 1] != 127 ||
                   layout.initial_candidate_records[at + 2] != 127) {
            throw FormatError("terrain decoration unused capacity must retain original127 bytes");
        }
    }
    return slots;
}

std::int32_t select_candidate(TerrainDecorationState& state, GameRandom& random) {
    const auto count = static_cast<std::uint32_t>(state.candidate_records.size() / 3);
    if (count == 0) throw FormatError("original decoration selection requires a nonzero divisor");
    const auto word = random.next_word();
    const auto value = word <= 0x7fffffffU ? static_cast<std::int64_t>(word) :
        static_cast<std::int64_t>(word) - 0x100000000LL;
    // Original two's-complement abs(INT_MIN) stays negative. Its remainder can
    // be zero for some counts; reject only an actual before-allocation offset.
    const auto magnitude = value == std::numeric_limits<std::int32_t>::min() ? value :
        (value < 0 ? -value : value);
    const auto initial = (magnitude % count) * 3;
    if (initial < 0)
        throw FormatError("original decoration draw produces an unsupported negative candidate offset");
    auto offset = static_cast<std::uint32_t>(initial);
    std::uint32_t traversed = 0;
    for (;;) {
        if (state.candidate_records[offset] >= 128) return static_cast<std::int32_t>(offset);
        if (traversed >= count) return -1;
        // Preserve0x64e96 exactly: retry modulus is candidate COUNT, not3*count.
        // It can probe x/y bytes. Do not replace this with a conventional scan.
        offset = (offset + 3U) % count;
        traversed += 3;
    }
}

bool place_slot(TerrainDecorationState& state, std::uint32_t slot, GameRandom& random) {
    const auto offset = select_candidate(state, random);
    if (offset < 0) return false; // Original leaves both prior geometry and marks intact.
    const auto at = static_cast<std::size_t>(offset);
    if (at % 3 != 0 || at + 2 >= state.candidate_records.size())
        throw FormatError("original decoration selection reached an unsupported non-record offset");
    state.candidate_records[at] = static_cast<std::uint8_t>(slot);
    state.slots[slot].candidate_byte_offset = offset;
    // Style1 intentionally skips the old-candidate release branch of64ebc.
    return true;
}

TerrainQuad quad(std::int32_t x0, std::int32_t x1, std::int32_t y,
                 std::int32_t z0, std::int32_t z1, std::uint8_t cell) {
    if (cell >= 4) throw FormatError("decoration cell is outside the first four420 frames");
    const auto narrow = [](std::int32_t v) {
        if (v < -32768 || v > 32767) throw FormatError("decoration position exceeds signed16 storage");
        return static_cast<std::int16_t>(v);
    };
    TerrainQuad out{};
    out.face = TerrainFace::top;
    out.atlas_cell = cell;
    out.positions = {{{narrow(x0), narrow(y), narrow(z0)}, {narrow(x0), narrow(y), narrow(z1)},
                      {narrow(x1), narrow(y), narrow(z0)}, {narrow(x1), narrow(y), narrow(z1)}}};
    const auto u0 = static_cast<std::int16_t>(-32768 + cell * 8192 + 256);
    const auto u1 = static_cast<std::int16_t>(-32768 + (cell + 1) * 8192 - 257);
    out.quantized_uv = {{{u0, -32512}, {u0, 32511}, {u1, -32512}, {u1, 32511}}};
    constexpr float scale = 1.0F / 65535.0F, bias = 0.50000762939453125F;
    for (std::size_t i = 0; i < 4; ++i)
        for (std::size_t j = 0; j < 2; ++j)
            out.normalized_uv[i][j] = static_cast<float>(out.quantized_uv[i][j]) * scale + bias;
    return out;
}

} // namespace

TerrainDecorationLayout build_terrain_decoration_layout(const TerrainGrid& grid,
    const TerrainTables& tables, const TerrainAnimationConfig& config) {
    // Reuse strict grid/table/config validation and recovered fixed-face gate.
    const auto mesh = build_terrain_mesh(grid, tables, 1, config);
    if (grid.width > 127 || grid.height > 127)
        throw FormatError("decoration coordinates require nonnegative original signed bytes");
    const auto& visuals = tables[1];
    const auto& eligible = config.tables[0][1];
    const auto counts = [&](std::uint8_t tile) {
        return std::find(eligible.begin(), eligible.end(), tile) != eligible.end();
    };
    const auto gate_count = static_cast<std::uint32_t>(config.tables[2].size());
    std::uint32_t candidates = 0, potential_faces = 0, base_allocation = 0;
    for (std::uint16_t y = 0; y < grid.height + 3; ++y) {
        const auto source_y = std::min<std::uint16_t>(y, static_cast<std::uint16_t>(grid.height - 1));
        for (std::uint16_t x = 0; x < grid.width; ++x) {
            const auto tile = grid.tile_at(x, source_y);
            const auto& record = visuals[tile];
            if (counts(tile)) ++candidates;
            if (gate_count) {
                potential_faces += record.atlas_cells[0] == 14 ? 1U : 0U;
                for (std::size_t face = 1; face < 4; ++face)
                    if (record.atlas_cells[face] == 14) potential_faces += gate_count;
            }
            ++base_allocation;
            const auto height = record.height_steps;
            if (y == 0 && height != 0) ++base_allocation;
            if (x > 0 && height > visuals[grid.tile_at(x - 1, source_y)].height_steps) ++base_allocation;
            if (x + 1 < grid.width && height > visuals[grid.tile_at(x + 1, source_y)].height_steps) ++base_allocation;
            if (y + 1 < grid.height && height > visuals[grid.tile_at(x, y + 1)].height_steps) ++base_allocation;
        }
    }
    if (base_allocation > 2000)
        throw FormatError("decoration candidate ordering requires unsupported multi-batch original construction");
    TerrainDecorationLayout out{grid.width, grid.height, Bytes(candidates * 3U, 127), 0,
                                candidates / 7 + potential_faces};
    for (std::uint16_t y = 0; y < grid.height; ++y)
        for (std::uint16_t x = 0; x < grid.width; ++x) {
            const auto tile = grid.tile_at(x, y);
            if (!counts(tile) || (gate_count && visuals[tile].atlas_cells[0] == 14)) continue;
            const auto at = static_cast<std::size_t>(out.registered_candidates++) * 3;
            out.initial_candidate_records.at(at) = 255;
            out.initial_candidate_records.at(at + 1) = static_cast<std::uint8_t>(x);
            out.initial_candidate_records.at(at + 2) = static_cast<std::uint8_t>(y);
        }
    const auto slots = slot_count(out);
    if (mesh.animated_surface && mesh.animated_surface->decoration_slots != slots)
        throw FormatError("terrain surfaces and decoration pool disagree about original capacity");
    return out;
}

void validate_terrain_decorations(const TerrainDecorationLayout& layout, const TerrainDecorationState& state) {
    const auto count = slot_count(layout);
    if (state.slots.size() != count || state.candidate_records.size() != layout.initial_candidate_records.size() ||
        state.animation.phase >= 4 || state.animation.accumulated_ms > 0x7fffffffU)
        throw FormatError("terrain decoration state shape or clock is invalid");
    for (std::size_t i = 0; i < state.candidate_records.size(); ++i) {
        const auto initial = layout.initial_candidate_records[i], value = state.candidate_records[i];
        if (i % 3 || initial == 127) {
            if (value != initial) throw FormatError("terrain decoration fixed candidate bytes changed");
        } else if (value != 255 && value >= count) {
            throw FormatError("terrain decoration candidate has an invalid slot mark");
        }
    }
    for (std::size_t i = 0; i < state.slots.size(); ++i) {
        const auto& slot = state.slots[i];
        if (slot.phase >= 4 || slot.candidate_byte_offset < -1)
            throw FormatError("terrain decoration slot phase or candidate is invalid");
        if (slot.candidate_byte_offset >= 0) {
            const auto at = static_cast<std::size_t>(slot.candidate_byte_offset);
            if (at % 3 || at / 3 >= layout.registered_candidates || state.candidate_records[at] != i)
                throw FormatError("terrain decoration selected candidate does not match its slot");
        }
    }
}

TerrainDecorationState create_terrain_decorations(const TerrainDecorationLayout& layout, GameRandom& random) {
    const auto count = slot_count(layout);
    TerrainDecorationState state{{}, layout.initial_candidate_records,
                                  std::vector<TerrainDecorationSlot>(count)};
    auto next_random = random;
    for (std::uint32_t i = 0; i < count; ++i) {
        state.slots[i].phase = static_cast<std::uint8_t>(i % 4);
        place_slot(state, i, next_random);
    }
    validate_terrain_decorations(layout, state);
    random = next_random;
    return state;
}

TerrainDecorationStep advance_terrain_decorations(const TerrainDecorationLayout& layout,
    TerrainDecorationState& state, std::uint32_t delta_ms, GameRandom& random) {
    validate_terrain_decorations(layout, state);
    auto next = state;
    auto next_random = random;
    TerrainDecorationStep result;
    result.phase_advanced = advance_terrain_animation(next.animation, delta_ms);
    if (result.phase_advanced) {
        for (std::uint32_t i = 0; i < next.slots.size(); ++i) {
            auto& slot = next.slots[i];
            ++slot.phase;
            if (slot.phase == 4) {
                slot.phase = 0;
                ++result.random_words;
                if (place_slot(next, i, next_random)) result.placed_slots.push_back(i);
            }
        }
    }
    state = std::move(next);
    random = next_random;
    return result;
}

std::vector<TerrainQuad> terrain_decoration_quads(const TerrainDecorationLayout& layout,
    const TerrainDecorationState& state, const TerrainGrid& grid, const TerrainTables& tables) {
    validate_terrain_decorations(layout, state);
    if (grid.width != layout.width || grid.height != layout.height ||
        grid.tiles.size() != static_cast<std::size_t>(grid.width) * grid.height)
        throw FormatError("terrain decoration world differs from its immutable layout");
    std::vector<TerrainQuad> result;
    result.reserve(state.slots.size());
    for (std::size_t i = 0; i < state.slots.size(); ++i) {
        const auto offset = state.slots[i].candidate_byte_offset;
        if (offset < 0) {
            if (!layout.mesh_quad_capacity) throw FormatError("decoration strip has a zero subdivision divisor");
            const auto span = static_cast<std::uint32_t>(grid.width) * 32U;
            const auto x0 = static_cast<std::int32_t>(span * i / layout.mesh_quad_capacity);
            const auto x1 = static_cast<std::int32_t>(span * (i + 1) / layout.mesh_quad_capacity);
            result.push_back(quad(x0, x1, 0, 0, grid.height * 32, static_cast<std::uint8_t>(i % 4)));
        } else {
            const auto at = static_cast<std::size_t>(offset);
            const auto x = state.candidate_records[at + 1], y = state.candidate_records[at + 2];
            const auto tile = grid.tile_at(x, y);
            if (tile >= 32) throw FormatError("decoration selected an unsupported terrain sentinel");
            auto out = quad(x * 32, (x + 1) * 32, tables[1][tile].height_steps * 32,
                            y * 32, (y + 1) * 32, 0);
            out.tile_x = x;
            out.tile_y = y;
            result.push_back(out);
        }
    }
    return result;
}

} // namespace g1
