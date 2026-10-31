#include "g1/TerrainSpriteDecorations.hpp"
#include "g1/Combat.hpp"

#include <algorithm>
#include <limits>
#include <utility>

namespace g1 {
namespace {

constexpr std::size_t maximum_sprites = 2048;

void check_grid(const TerrainGrid& grid, std::uint8_t style) {
    if ((style != 0 && style != 2) || !grid.width || !grid.height ||
        grid.width > 127 || grid.height > 127 ||
        grid.tiles.size() != std::size_t(grid.width) * grid.height)
        throw FormatError("terrain sprite layout requires style0/2 and a bounded signed-byte grid");
    if (std::any_of(grid.tiles.begin(), grid.tiles.end(), [](auto tile) { return tile >= 32; }))
        throw FormatError("terrain sprite grid contains an unsupported tile or empty sentinel");
    // Conservative face-allocation upper bound keeps the previously recovered
    // row-major candidate registration inside one original constructor batch.
    if (std::size_t(grid.width) * (grid.height + 3U) * 5U > 2000U)
        throw FormatError("terrain sprite layout exceeds the supported single-batch profile");
}

std::uint32_t fixed_capacity(const TerrainGrid& grid, const TerrainLookup& lookup,
                             std::uint8_t style) {
    std::uint32_t result = style == 0 ? 4U * grid.width : 0U;
    for (const auto tile : grid.tiles) {
        if (style == 0 && lookup[tile] == 1) result += 2;
        if (style == 2 && (tile == 10 || tile == 30 || tile == 31)) ++result;
    }
    return result;
}

std::vector<std::uint8_t> selectors(std::uint8_t style, std::uint32_t slots,
                                   std::uint32_t fixed, std::uint16_t height) {
    const auto count = 7U + slots + fixed + (style == 2 ? 3U * (height + 1U) : 0U);
    if (count > maximum_sprites)
        throw FormatError("terrain sprite count exceeds its explicit native bound");
    std::vector<std::uint8_t> result;
    result.reserve(count - 4U);
    for (std::uint32_t index = 4; index < count; ++index)
        result.push_back(index < 7 ? 73 : index < 7U + slots ? (style == 0 ? 75 : 78) :
                         index < 7U + slots + fixed ? (style == 0 ? 74 : 76) : 77);
    return result;
}

std::uint32_t validate_layout(const TerrainSpriteDecorationLayout& layout) {
    check_grid(layout.grid, layout.style);
    if (layout.initial_candidate_records.size() % 3 ||
        layout.initial_candidate_records.size() > 3U * 889U)
        throw FormatError("terrain sprite candidate allocation is invalid");
    const auto capacity = layout.initial_candidate_records.size() / 3;
    const auto slots = static_cast<std::uint32_t>(capacity / 7U);
    if (slots >= 127 || layout.registered_candidates > capacity ||
        layout.fixed_sprite_capacity != fixed_capacity(layout.grid, layout.lookup, layout.style) ||
        layout.selectors != selectors(layout.style, slots, layout.fixed_sprite_capacity, layout.grid.height))
        throw FormatError("terrain sprite layout counts/selectors disagree");
    for (std::size_t i = 0; i < capacity; ++i) {
        const auto at = i * 3;
        if (i < layout.registered_candidates) {
            if (layout.initial_candidate_records[at] != 255 ||
                layout.initial_candidate_records[at + 1] >= layout.grid.width ||
                layout.initial_candidate_records[at + 2] >= layout.grid.height)
                throw FormatError("terrain sprite candidate coordinates are invalid");
        } else if (layout.initial_candidate_records[at] != 127 ||
                   layout.initial_candidate_records[at + 1] != 127 ||
                   layout.initial_candidate_records[at + 2] != 127) {
            throw FormatError("terrain sprite unused capacity must preserve original127 bytes");
        }
    }
    return slots;
}

void validate_bank(const TerrainSpriteAnimationBank& bank) {
    for (const auto& animations : bank) {
        if (animations.empty() || animations.size() > 255)
            throw FormatError("terrain sprite descriptor requires1..255 animations");
        for (const auto& steps : animations) (void)SpriteAnimation(steps);
    }
}

const std::vector<std::vector<AtlasStep>>& animations_for(
    const TerrainSpriteDecorationLayout& layout, const TerrainSpriteAnimationBank& bank,
    std::size_t instance) {
    return bank.at(layout.selectors.at(instance) - 73U);
}

TerrainSpriteDecoration& sprite_at(TerrainSpriteDecorationState& state, std::int64_t index) {
    if (index < 4 || std::uint64_t(index - 4) >= state.sprites.size())
        throw FormatError("original terrain decoration index exceeds its allocated sprite array");
    return state.sprites[static_cast<std::size_t>(index - 4)];
}

std::int64_t magnitude_word(GameRandom& random) {
    const auto word = random.next_word();
    const auto value = word <= 0x7fffffffU ? std::int64_t(word) : std::int64_t(word) - 0x100000000LL;
    // Preserve original signed32 abs(INT_MIN) wrap. Bit masks remain valid;
    // negative remainder accesses are rejected by the specific consumer.
    return value == std::numeric_limits<std::int32_t>::min() ? value : (value < 0 ? -value : value);
}

std::uint32_t random_modulo(GameRandom& random, std::size_t count) {
    if (count == 0 || count > std::size_t(std::numeric_limits<std::int32_t>::max()))
        throw FormatError("original terrain random remainder divisor is invalid");
    const auto result = magnitude_word(random) % static_cast<std::int64_t>(count);
    if (result < 0)
        throw FormatError("original terrain random selection produces an unsupported negative index");
    return static_cast<std::uint32_t>(result);
}

std::int32_t reserve_candidate(TerrainSpriteDecorationState& state,
                               std::uint32_t slot, GameRandom& random) {
    const auto count = state.candidate_records.size() / 3U;
    auto at = random_modulo(random, count) * 3U;
    std::size_t traversed = 0;
    for (;;) {
        if (state.candidate_records.at(at) >= 128) {
            if (at % 3 || at + 2 >= state.candidate_records.size())
                throw FormatError("original terrain selection reached a non-record byte offset");
            state.candidate_records[at] = static_cast<std::uint8_t>(slot);
            return static_cast<std::int32_t>(at);
        }
        if (traversed >= count) return -1;
        // Original64e96 uses candidate COUNT, not the triplet byte count.
        at = static_cast<std::uint32_t>((at + 3U) % count);
        traversed += 3;
    }
}

void start(TerrainSpriteDecorationState& state, const TerrainSpriteDecorationLayout& layout,
           const TerrainSpriteAnimationBank& bank, std::int64_t index,
           std::uint32_t animation, std::int32_t repeats) {
    auto& sprite = sprite_at(state, index);
    const auto& animations = animations_for(layout, bank, static_cast<std::size_t>(index - 4));
    if (animation >= animations.size())
        throw FormatError("original terrain selects an animation outside its descriptor");
    sprite.animation_index = static_cast<std::uint8_t>(animation);
    sprite.playback = SpriteAnimation(animations[animation], repeats).state();
}

void advance_sprite(TerrainSpriteDecoration& sprite, const std::vector<std::vector<AtlasStep>>& animations,
                    std::uint32_t delta_ms) {
    auto playback = SpriteAnimation::restore(animations.at(sprite.animation_index), sprite.playback);
    playback.advance_ms(delta_ms);
    sprite.playback = playback.state();
}

void position(TerrainSpriteDecorationState& state, std::int64_t index,
              std::int32_t x, std::int32_t y, std::int32_t z) {
    auto& sprite = sprite_at(state, index);
    sprite.visible = true;
    sprite.original_position = {{x, y, z}};
}

bool relocate_slot(const TerrainSpriteDecorationLayout& layout,
                   TerrainSpriteDecorationState& state, std::uint32_t slot, GameRandom& random) {
    bool relocated = false;
    for (std::size_t at = 0; at < state.candidate_records.size(); at += 3) {
        if (state.candidate_records[at] != slot) continue;
        const auto x = state.candidate_records[at + 1], y = state.candidate_records[at + 2];
        const auto tile = layout.grid.tile_at(x, y);
        state.candidate_records[at] = 255;
        position(state, 7U + slot, x * 32, std::int32_t(layout.visuals[tile].height_steps) * 32, y * 32);
        relocated = true;
        break;
    }
    (void)reserve_candidate(state, slot, random);
    return relocated;
}

void stage2_placements(const TerrainSpriteDecorationLayout& layout,
                       const TerrainSpriteAnimationBank& bank,
                       TerrainSpriteDecorationState& state, GameRandom& random) {
    const auto width = layout.grid.width, height = layout.grid.height;
    const auto style = layout.style;
    const auto slots = static_cast<std::uint32_t>(state.slot_phases.size());
    const auto fixed = layout.fixed_sprite_capacity;
    const auto base = 7U + slots;
    std::uint32_t fixed_cursor = 0, front_cursor = 0, attachment_order = 0;
    const auto attach = [&](std::int64_t index, TerrainSpriteParent parent) {
        auto& sprite = sprite_at(state, index);
        sprite.parent = parent;
        sprite.attachment_order = ++attachment_order;
    };
    const auto tile_at = [&](std::uint16_t x, std::uint16_t y) { return layout.grid.tile_at(x, y); };
    const auto choose = [&](std::int64_t index, std::size_t count) {
        start(state, layout, bank, index, random_modulo(random, count), 0);
    };
    for (std::uint16_t x = 0; x < width; ++x) {
        const auto first = tile_at(x, 0);
        if (front_cursor < 3 && ((style == 0 && (first == 8 || first == 10)) ||
                                (style == 2 && (first == 2 || first == 3)))) {
            const auto index = 4U + front_cursor++;
            position(state, index, x * 32, 0, 0);
            attach(index, TerrainSpriteParent::front_group);
        }
        if (style == 0 && (x <= 4 || std::int32_t(x) > std::int32_t(width) - 6)) {
            const auto index = std::int64_t(base) + fixed - width + x;
            position(state, index, x * 32 + (x <= 4 ? -160 : 160), 10, 19);
            attach(index, TerrainSpriteParent::decoration_group);
            start(state, layout, bank, index, 0, 0);
        }
        for (std::uint16_t y = 0; y < height; ++y) {
            if (x <= 2) {
                // x2 reads the RIGHT raw edge tile before randomly choosing
                // which side supplies the terrain class. Preserve that order.
                const auto edge_tile = tile_at(x == 0 ? 0 : width - 1U, y);
                auto edge_x = x == 0 ? 0U : std::uint32_t(width - 1U);
                std::int32_t column = x == 0 ? 0 : width + (style == 2 ? 1 : 0);
                if (x == 2) {
                    if ((static_cast<std::uint64_t>(magnitude_word(random)) & 1U) != 0U) {
                        edge_x = 0;
                        column = -2;
                    } else {
                        column = width + (style == 2 ? 3 : 2);
                    }
                }
                const auto fraction = float(static_cast<std::uint64_t>(magnitude_word(random)) & 31U) * 0.03125F;
                const auto offset = style == 0 && x == 2 ? fraction - 0.5F :
                                    fraction * 0.5F + 0x1.ccccccp-2F;
                if (layout.lookup[tile_at(static_cast<std::uint16_t>(edge_x), y)] != 3 && edge_tile != 14) {
                    const bool special = (style == 2 && edge_tile >= 2 && edge_tile <= 4) ||
                                         (style == 0 && edge_tile >= 8 && edge_tile <= 11);
                    if (special && x == 2) continue; // Also skips this interior cell.
                    const auto index = style == 2 ? std::int64_t(base) + fixed + x * (height + 1U) + y :
                        std::int64_t(base) + fixed - 4U * width + x * height + y;
                    const auto px = ((float(column) - 0.5F) + (column > 0 ? 1.0F : -1.0F) * offset) * 32.0F;
                    const auto pz = (float(y) + 0x1.ccccccp-1F) * 32.0F + 10.0F;
                    position(state, index, static_cast<std::int32_t>(px), 10, static_cast<std::int32_t>(pz));
                    attach(index, TerrainSpriteParent::decoration_group);
                    const auto count = animations_for(layout, bank, static_cast<std::size_t>(index - 4)).size();
                    if (style == 2)
                        start(state, layout, bank, index, special ? 7U : 4U + random_modulo(random, 3), 0);
                    else if (x == 2) choose(index, count);
                    else start(state, layout, bank, index, 0, 0);
                }
            }
            const auto tile = tile_at(x, y);
            if (fixed_cursor < fixed && style == 0 && layout.lookup[tile] == 1) {
                const auto index = std::int64_t(base) + fixed_cursor;
                const auto z0 = ((float(y) + 0x1.99999ap-3F) * 32.0F + 10.0F) + 2.0F;
                const auto z1 = ((float(y) + 0x1.99999ap-1F) * 32.0F + 10.0F) + 2.0F;
                position(state, index, x * 32, 4, static_cast<std::int32_t>(z0));
                const auto count = animations_for(layout, bank, static_cast<std::size_t>(index - 4)).size();
                choose(index, count);
                position(state, index + 1, x * 32, 4, static_cast<std::int32_t>(z1));
                choose(index + 1, count); // Original uses the first sprite's count for both.
                // Original6a070..6a088 adds only the first mesh to this group.
                attach(index, TerrainSpriteParent::decoration_group);
                fixed_cursor += 2;
            }
            if (fixed_cursor < fixed && style == 2 && (tile == 10 || tile == 30 || tile == 31)) {
                const auto index = std::int64_t(base) + fixed_cursor;
                const auto pz = ((float(y) + 0x1.99999ap-1F) * 32.0F + 10.0F) + 2.0F;
                position(state, index, x * 32, 4, static_cast<std::int32_t>(pz));
                choose(index, animations_for(layout, bank, static_cast<std::size_t>(index - 4)).size());
                attach(index, TerrainSpriteParent::decoration_group);
                ++fixed_cursor;
            }
        }
    }
    for (std::uint32_t slot = 0; slot < slots; ++slot)
        attach(7U + slot, TerrainSpriteParent::terrain_group);
}

} // namespace

TerrainSpriteAnimationBank load_terrain_sprite_animations(const Archive& archive) {
    TerrainSpriteAnimationBank bank;
    for (std::uint8_t selector = 73; selector <= 78; ++selector) {
        const auto resource = sprite_resource_for_type(selector);
        if (!resource) throw FormatError("terrain sprite selector has no known resource binding");
        const auto matches = archive.find(resource->descriptor_id, resource->resource_key);
        if (matches.size() != 1) throw FormatError("terrain sprite descriptor is missing or ambiguous");
        bank[selector - 73U] = AtlasDescriptor(archive.read(matches.front())).animations();
    }
    validate_bank(bank);
    return bank;
}

TerrainSpriteDecorationLayout build_terrain_sprite_decoration_layout(
    const TerrainGrid& grid, const TerrainTables& tables, const TerrainAnimationConfig& config,
    const TerrainLookup& lookup, std::uint8_t style) {
    check_grid(grid, style);
    if (config.tables[0].size() != 3 || config.tables[0][style].size() > 255)
        throw FormatError("terrain sprite config requires the original three style rows");
    const auto& row = config.tables[0][style];
    const auto candidate = [&](std::uint8_t tile) {
        // Original45a5e returns13 for every style0 row index. An empty row
        // still produces no candidates because474a0 supplies its loop count.
        return style == 0 ? !row.empty() && tile == 13 :
               std::find(row.begin(), row.end(), tile) != row.end();
    };
    std::uint32_t capacity = 0;
    for (std::uint16_t y = 0; y < grid.height + 3U; ++y)
        for (std::uint16_t x = 0; x < grid.width; ++x)
            if (candidate(grid.tile_at(x, std::min<std::uint16_t>(y, grid.height - 1U)))) ++capacity;
    TerrainSpriteDecorationLayout out{grid, tables[style], lookup, style,
        Bytes(capacity * 3U, 127), 0, fixed_capacity(grid, lookup, style), {}};
    for (std::uint16_t y = 0; y < grid.height; ++y)
        for (std::uint16_t x = 0; x < grid.width; ++x) {
            if (!candidate(grid.tile_at(x, y))) continue;
            const auto at = static_cast<std::size_t>(out.registered_candidates++) * 3;
            out.initial_candidate_records.at(at) = 255;
            out.initial_candidate_records.at(at + 1) = static_cast<std::uint8_t>(x);
            out.initial_candidate_records.at(at + 2) = static_cast<std::uint8_t>(y);
        }
    out.selectors = selectors(style, capacity / 7U, out.fixed_sprite_capacity, grid.height);
    (void)validate_layout(out);
    return out;
}

void validate_terrain_sprite_decorations(const TerrainSpriteDecorationLayout& layout,
    const TerrainSpriteAnimationBank& bank, const TerrainSpriteDecorationState& state) {
    const auto slots = validate_layout(layout);
    validate_bank(bank);
    if (state.accumulated_ms > std::uint32_t(std::numeric_limits<std::int32_t>::max()) ||
        state.phase >= 8 || state.slot_phases.size() != slots ||
        state.candidate_records.size() != layout.initial_candidate_records.size() ||
        state.sprites.size() != layout.selectors.size())
        throw FormatError("terrain sprite checkpoint count/clock is invalid");
    std::vector<unsigned> marks(slots, 0);
    for (std::size_t at = 0; at < state.candidate_records.size(); ++at) {
        const auto initial = layout.initial_candidate_records[at], value = state.candidate_records[at];
        if (at % 3 || at / 3 >= layout.registered_candidates) {
            if (value != initial) throw FormatError("terrain sprite immutable candidate bytes changed");
        } else if (value != 255) {
            if (value >= slots || ++marks[value] > 1)
                throw FormatError("terrain sprite candidate mark is invalid or duplicated");
        }
    }
    if (std::any_of(state.slot_phases.begin(), state.slot_phases.end(), [](auto phase) { return phase >= 8; }))
        throw FormatError("terrain sprite slot phase exceeds original8-phase period");
    std::vector<bool> attachment_orders(maximum_sprites * 4U + 1U, false);
    for (std::size_t i = 0; i < state.sprites.size(); ++i) {
        const auto& sprite = state.sprites[i];
        const auto& animations = animations_for(layout, bank, i);
        if (sprite.animation_index >= animations.size() ||
            static_cast<unsigned>(sprite.parent) > 3 || (sprite.visible && !sprite.original_position))
            throw FormatError("terrain sprite saved animation, parent or position is invalid");
        if (i >= 3 && i < 3U + slots && sprite.parent != TerrainSpriteParent::terrain_group)
            throw FormatError("terrain transient sprite lost its original parent group");
        if ((sprite.parent == TerrainSpriteParent::unattached) != (sprite.attachment_order == 0) ||
            sprite.attachment_order >= attachment_orders.size() ||
            (sprite.attachment_order != 0 && attachment_orders[sprite.attachment_order]))
            throw FormatError("terrain sprite saved attachment order is invalid or duplicated");
        attachment_orders[sprite.attachment_order] = true;
        (void)SpriteAnimation::restore(animations[sprite.animation_index], sprite.playback);
        if (sprite.original_position)
            for (const auto value : *sprite.original_position)
                if (value < -32768 || value > 32767)
                    throw FormatError("terrain sprite saved coordinate exceeds bounded profile");
    }
}

TerrainSpriteDecorationState create_terrain_sprite_decorations(
    const TerrainSpriteDecorationLayout& layout, const TerrainSpriteAnimationBank& bank, GameRandom& random) {
    const auto slots = validate_layout(layout);
    validate_bank(bank);
    auto next_random = random;
    TerrainSpriteDecorationState state;
    state.candidate_records = layout.initial_candidate_records;
    state.slot_phases.resize(slots);
    // Stage1 has no existing sprites or old marks to release.
    for (std::uint32_t slot = 0; slot < slots; ++slot) {
        state.slot_phases[slot] = static_cast<std::uint8_t>(slot % 8U);
        (void)reserve_candidate(state, slot, next_random);
    }
    state.sprites.resize(layout.selectors.size());
    for (std::size_t i = 0; i < state.sprites.size(); ++i) {
        auto playback = SpriteAnimation(animations_for(layout, bank, i).front());
        if (layout.selectors[i] == 75) playback.advance_ms(static_cast<std::uint32_t>(i - 3U) * 250U);
        state.sprites[i].playback = playback.state();
    }
    stage2_placements(layout, bank, state, next_random);
    validate_terrain_sprite_decorations(layout, bank, state);
    random = next_random;
    return state;
}

TerrainSpriteDecorationStep advance_terrain_sprite_decorations(
    const TerrainSpriteDecorationLayout& layout, const TerrainSpriteAnimationBank& bank,
    TerrainSpriteDecorationState& state, std::uint32_t delta_ms, GameRandom& random) {
    validate_terrain_sprite_decorations(layout, bank, state);
    if (delta_ms > std::uint32_t(std::numeric_limits<std::int32_t>::max()) ||
        std::uint64_t(state.accumulated_ms) + delta_ms > std::uint32_t(std::numeric_limits<std::int32_t>::max()))
        throw FormatError("terrain sprite elapsed-time addition exceeds original signed32 range");
    auto next = state;
    auto next_random = random;
    TerrainSpriteDecorationStep step;
    for (std::size_t slot = 0; slot < next.slot_phases.size(); ++slot) {
        const auto instance = slot + 3U;
        advance_sprite(next.sprites[instance], animations_for(layout, bank, instance), delta_ms);
    }
    next.accumulated_ms += delta_ms;
    if (next.accumulated_ms >= 250U) {
        next.accumulated_ms -= 250U;
        step.phase_advanced = true;
        for (std::uint32_t slot = 0; slot < next.slot_phases.size(); ++slot) {
            if (++next.slot_phases[slot] != 8) continue;
            next.slot_phases[slot] = 0;
            if (relocate_slot(layout, next, slot, next_random)) step.relocated_slots.push_back(slot);
        }
        next.phase = static_cast<std::uint8_t>((next.phase + 1U) % 8U);
    }
    for (std::size_t i = 0; i < 3; ++i)
        if (next.sprites[i].visible)
            advance_sprite(next.sprites[i], animations_for(layout, bank, i), delta_ms);
    step.random_words = static_cast<std::uint32_t>(next_random.state().draws - random.state().draws);
    validate_terrain_sprite_decorations(layout, bank, next);
    state = std::move(next);
    random = next_random;
    return step;
}

std::array<float, 16> terrain_sprite_decoration_model(const std::array<std::int32_t, 3>& position) {
    for (const auto value : position)
        if (value < -32768 || value > 32767)
            throw FormatError("terrain sprite model coordinate exceeds bounded profile");
    constexpr float cosine = 0x1.6a09e4p-1F, sine = 0x1.6a09e8p-1F;
    return {{1, 0, 0, 0, 0, cosine, -sine, 0, 0, sine, cosine, 0,
             float(position[0]) / 100.0F, float(position[1]) / 100.0F,
             float(position[2]) / 100.0F, 1}};
}

std::vector<TerrainSpriteDecorationDraw> terrain_sprite_decoration_draws(
    const TerrainSpriteDecorationLayout& layout, const TerrainSpriteAnimationBank& bank,
    const TerrainSpriteDecorationState& state) {
    validate_terrain_sprite_decorations(layout, bank, state);
    std::vector<TerrainSpriteDecorationDraw> draws;
    for (std::size_t i = 0; i < state.sprites.size(); ++i) {
        const auto& sprite = state.sprites[i];
        if (!sprite.visible || sprite.parent == TerrainSpriteParent::unattached) continue;
        const auto resource = sprite_resource_for_type(layout.selectors[i]);
        if (!resource) throw FormatError("terrain sprite has no recovered descriptor binding");
        const auto frame = animations_for(layout, bank, i)[sprite.animation_index][sprite.playback.step_index].frame_index;
        draws.push_back({static_cast<std::uint32_t>(i + 4), *resource, frame, sprite.parent, sprite.attachment_order,
                         terrain_sprite_decoration_model(*sprite.original_position)});
    }
    std::sort(draws.begin(), draws.end(), [](const auto& a, const auto& b) {
        return a.attachment_order < b.attachment_order;
    });
    return draws;
}

} // namespace g1
