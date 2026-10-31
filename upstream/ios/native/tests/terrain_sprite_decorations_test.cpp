#include "g1/TerrainSpriteDecorations.hpp"
#include "g1/Combat.hpp"

#include <algorithm>
#include <cstring>
#include <fstream>
#include <iostream>
#include <limits>
#include <stdexcept>
#include <string>

namespace {
std::size_t checks{};
void require(bool value, const char* message) {
    ++checks;
    if (!value) throw std::runtime_error(message);
}
void tag(std::istream& input, const char* expected) {
    std::string value;
    input >> value;
    require(value == expected, "terrain sprite fixture tag differs");
}
std::int64_t number(std::istream& input) {
    std::int64_t value{};
    if (!(input >> value)) throw std::runtime_error("short terrain sprite fixture");
    return value;
}
std::size_t count(std::istream& input, std::size_t maximum) {
    const auto value = number(input);
    if (value < 0 || std::uint64_t(value) > maximum)
        throw std::runtime_error("terrain sprite fixture count is out of range");
    return static_cast<std::size_t>(value);
}
g1::Bytes bytes(std::istream& input) {
    const auto n = count(input, 65535);
    g1::Bytes result;
    result.reserve(n);
    for (std::size_t i = 0; i < n; ++i)
        result.push_back(static_cast<std::uint8_t>(count(input, 255)));
    return result;
}
g1::TerrainSpriteAnimationBank bank(std::istream& input) {
    g1::TerrainSpriteAnimationBank result;
    for (auto& animations : result) {
        const auto n = count(input, 255);
        for (std::size_t i = 0; i < n; ++i) {
            const auto m = count(input, 255);
            std::vector<g1::AtlasStep> steps;
            for (std::size_t j = 0; j < m; ++j) {
                const auto frame = count(input, 255), duration = count(input, 65535);
                steps.push_back({static_cast<std::uint8_t>(frame), static_cast<std::uint16_t>(duration)});
            }
            animations.push_back(std::move(steps));
        }
    }
    return result;
}
bool same(const g1::GameRandom::State& a, const g1::GameRandom::State& b) {
    return a.index == b.index && a.draws == b.draws && a.words == b.words;
}
std::vector<std::int64_t> snapshot(const g1::TerrainSpriteDecorationLayout& layout,
    const g1::TerrainSpriteAnimationBank& animations, const g1::TerrainSpriteDecorationState& state,
    const g1::GameRandom& random) {
    std::vector<std::int64_t> result{state.accumulated_ms, state.phase, random.state().index,
        static_cast<std::int64_t>(random.state().draws), static_cast<std::int64_t>(state.candidate_records.size())};
    result.insert(result.end(), state.candidate_records.begin(), state.candidate_records.end());
    result.push_back(static_cast<std::int64_t>(state.slot_phases.size()));
    result.insert(result.end(), state.slot_phases.begin(), state.slot_phases.end());
    result.push_back(static_cast<std::int64_t>(state.sprites.size()));
    for (std::size_t i = 0; i < state.sprites.size(); ++i) {
        const auto& sprite = state.sprites[i];
        const auto& playback = sprite.playback;
        const auto frame = animations[layout.selectors[i] - 73U][sprite.animation_index][playback.step_index].frame_index;
        result.insert(result.end(), {static_cast<std::int64_t>(i + 4), layout.selectors[i],
            sprite.animation_index, sprite.visible, sprite.visible, static_cast<std::int64_t>(sprite.parent),
            sprite.attachment_order, sprite.original_position.has_value()});
        if (sprite.original_position)
            result.insert(result.end(), sprite.original_position->begin(), sprite.original_position->end());
        result.insert(result.end(), {static_cast<std::int64_t>(playback.step_index), frame, playback.elapsed_ms,
                                     playback.repeat_count, playback.reverse, playback.completed});
    }
    return result;
}
template<class F> void rejects(F operation) {
    bool rejected = false;
    try { operation(); }
    catch (const g1::FormatError&) { rejected = true; }
    catch (const std::invalid_argument&) { rejected = true; }
    catch (const std::out_of_range&) { rejected = true; }
    require(rejected, "invalid terrain sprite state was accepted");
}

void contracts() {
    g1::TerrainSpriteAnimationBank animations;
    for (auto& entries : animations) entries = {{{0, 100}, {1, 130}}};
    animations[4].resize(8, {{0, 100}});
    g1::TerrainTables tables{};
    g1::TerrainAnimationConfig config;
    config.tables[0].resize(3);
    config.tables[0][0] = {20}; // Original style0 returns13, not this raw row byte.
    config.tables[0][2] = {18, 23};
    g1::TerrainLookup lookup{};
    const g1::TerrainGrid grid{4, 3, g1::Bytes(12, 13)};
    const auto layout = g1::build_terrain_sprite_decoration_layout(grid, tables, config, lookup, 0);
    g1::GameRandom random(1);
    const auto initial = g1::create_terrain_sprite_decorations(layout, animations, random);
    auto state = initial;
    const auto stable_random = random.state();
    auto invalid = state;
    invalid.phase = 8;
    rejects([&] { g1::validate_terrain_sprite_decorations(layout, animations, invalid); });
    invalid = state;
    invalid.slot_phases[0] = 8;
    rejects([&] { g1::advance_terrain_sprite_decorations(layout, animations, invalid, 250, random); });
    invalid = state;
    invalid.candidate_records[1] = 127;
    rejects([&] { g1::validate_terrain_sprite_decorations(layout, animations, invalid); });
    invalid = state;
    invalid.candidate_records[0] = 0;
    invalid.candidate_records[3] = 0;
    rejects([&] { g1::validate_terrain_sprite_decorations(layout, animations, invalid); });
    invalid = state;
    invalid.candidate_records.back() = 0;
    rejects([&] { g1::validate_terrain_sprite_decorations(layout, animations, invalid); });
    invalid = state;
    invalid.sprites[3].parent = g1::TerrainSpriteParent::unattached;
    rejects([&] { g1::validate_terrain_sprite_decorations(layout, animations, invalid); });
    invalid = state;
    invalid.sprites[3].visible = true;
    invalid.sprites[3].original_position.reset();
    rejects([&] { g1::validate_terrain_sprite_decorations(layout, animations, invalid); });
    invalid = state;
    invalid.sprites[3].playback.step_index = 255;
    rejects([&] { g1::validate_terrain_sprite_decorations(layout, animations, invalid); });
    invalid = state;
    invalid.sprites[3].playback.elapsed_ms = -1;
    rejects([&] { g1::validate_terrain_sprite_decorations(layout, animations, invalid); });
    invalid = state;
    invalid.sprites[3].attachment_order = 0;
    rejects([&] { g1::validate_terrain_sprite_decorations(layout, animations, invalid); });
    auto invalid_layout = layout;
    invalid_layout.selectors[0] = 72;
    rejects([&] { g1::create_terrain_sprite_decorations(invalid_layout, animations, random); });
    invalid_layout = layout;
    ++invalid_layout.fixed_sprite_capacity;
    rejects([&] { g1::create_terrain_sprite_decorations(invalid_layout, animations, random); });
    auto invalid_bank = animations;
    invalid_bank[2][0][0].duration_units = 0;
    rejects([&] { g1::create_terrain_sprite_decorations(layout, invalid_bank, random); });
    require(same(random.state(), stable_random), "malformed input consumed shared random words");
    rejects([&] { g1::terrain_sprite_decoration_model({0, 32768, 0}); });
    rejects([&] { g1::build_terrain_sprite_decoration_layout(grid, tables, config, lookup, 1); });
    auto oversized = grid;
    oversized.width = 128;
    oversized.tiles.resize(128 * grid.height, 13);
    rejects([&] { g1::build_terrain_sprite_decoration_layout(oversized, tables, config, lookup, 0); });

    invalid = state;
    invalid.accumulated_ms = 0x7fffffffU;
    auto before = snapshot(layout, animations, invalid, random);
    rejects([&] { g1::advance_terrain_sprite_decorations(layout, animations, invalid, 1, random); });
    require(snapshot(layout, animations, invalid, random) == before && same(random.state(), stable_random),
            "clock overflow changed state or random stream");
    invalid = state;
    invalid.sprites[3].playback.elapsed_ms = std::numeric_limits<std::int32_t>::max();
    before = snapshot(layout, animations, invalid, random);
    rejects([&] { g1::advance_terrain_sprite_decorations(layout, animations, invalid, 1, random); });
    require(snapshot(layout, animations, invalid, random) == before && same(random.state(), stable_random),
            "sprite playback overflow changed state or random stream");

    auto generator_state = random.state();
    generator_state.index = 0;
    generator_state.words[0] = 0x80102204U; // Actual generator tempers this toINT_MIN.
    random.restore(generator_state);
    auto check_word = random;
    require(check_word.next_word() == 0x80000000U, "boundary generator setup differs");
    rejects([&] { g1::create_terrain_sprite_decorations(layout, animations, random); });
    require(same(random.state(), generator_state), "rejected initial negative remainder consumed a word");
    state.slot_phases[0] = 7;
    before = snapshot(layout, animations, state, random);
    rejects([&] { g1::advance_terrain_sprite_decorations(layout, animations, state, 250, random); });
    require(snapshot(layout, animations, state, random) == before && same(random.state(), generator_state),
            "failed reselect committed released reservation, geometry, playback or RNG");
    const g1::TerrainGrid zero_remainder_grid{4, 1, g1::Bytes(4, 13)};
    const auto zero_remainder = g1::build_terrain_sprite_decoration_layout(zero_remainder_grid, tables, config, lookup, 0);
    const auto accepted = g1::create_terrain_sprite_decorations(zero_remainder, animations, random);
    require(accepted.candidate_records[0] == 0 && random.state().draws > generator_state.draws,
            "valid INT_MIN zero-remainder reservation was rejected or altered");
}
}

int main(int argc, char** argv) {
    try {
        if (argc != 2) throw std::runtime_error("pass terrain_sprite_decorations_cases.txt");
        std::ifstream input(argv[1]);
        tag(input, "G1_TERRAIN_SPRITE_ORACLE_V1");
        const auto cases = count(input, 256);
        require(cases == 12, "terrain sprite fixture is incomplete");
        std::size_t snapshots = 0;
        for (std::size_t c = 0; c < cases; ++c) {
            tag(input, "CASE");
            std::string name;
            input >> name;
            tag(input, "INPUT");
            const auto seed = static_cast<std::uint32_t>(count(input, 0xffffffffU));
            const auto style = static_cast<std::uint8_t>(count(input, 2));
            const auto grid = g1::decode_terrain_grid(bytes(input));
            const auto tables = g1::decode_terrain_tables(bytes(input));
            const auto config = g1::decode_terrain_animation_config(bytes(input));
            const auto raw_lookup = bytes(input);
            require(raw_lookup.size() == 32, "fixture lookup shape differs");
            g1::TerrainLookup lookup{};
            std::copy(raw_lookup.begin(), raw_lookup.end(), lookup.begin());
            const auto animations = bank(input);
            std::vector<std::uint32_t> deltas(count(input, 4096));
            for (auto& delta : deltas) delta = static_cast<std::uint32_t>(count(input, 0x7fffffffU));
            const auto layout = g1::build_terrain_sprite_decoration_layout(grid, tables, config, lookup, style);
            g1::GameRandom random(seed), restored_random(0xdeadbeefU);
            auto state = g1::create_terrain_sprite_decorations(layout, animations, random);
            g1::TerrainSpriteDecorationState restored;
            tag(input, "LAYOUT");
            require(layout.initial_candidate_records.size() / 3 == count(input, 889), "original candidate capacity differs");
            require(layout.registered_candidates == count(input, 889), "original registered count differs");
            require(state.slot_phases.size() == count(input, 126), "original slot count differs");
            require(layout.fixed_sprite_capacity == count(input, 2048), "original fixed capacity differs");
            require(layout.selectors.size() + 4 == count(input, 2048), "original sprite allocation differs");
            tag(input, "STATES");
            const auto n = count(input, 4097);
            require(n == deltas.size() + 1, "original snapshot count differs");
            for (std::size_t i = 0; i < n; ++i) {
                if (i) {
                    g1::advance_terrain_sprite_decorations(layout, animations, state, deltas[i - 1], random);
                    if (i > n / 2)
                        g1::advance_terrain_sprite_decorations(layout, animations, restored, deltas[i - 1], restored_random);
                }
                tag(input, "STATE");
                const auto actual = snapshot(layout, animations, state, random);
                require(actual.size() == count(input, 100000), "original serialized state extent differs");
                for (const auto value : actual) require(value == number(input), "original sprite/candidate/playback state differs");
                ++snapshots;
                if (i == n / 2) {
                    restored = state;
                    restored_random.restore(random.state());
                    g1::validate_terrain_sprite_decorations(layout, animations, restored);
                } else if (i > n / 2) {
                    require(snapshot(layout, animations, restored, restored_random) == actual &&
                            same(random.state(), restored_random.state()), "restored decoration/RNG continuation differs");
                }
            }
            tag(input, "RNG");
            for (const auto value : random.state().words)
                require(value == static_cast<std::uint32_t>(count(input, 0xffffffffU)), "original624-word generator state differs");
            const auto before = snapshot(layout, animations, state, random);
            const auto draws = g1::terrain_sprite_decoration_draws(layout, animations, state);
            require(before == snapshot(layout, animations, state, random), "draw generation mutated simulation state");
            tag(input, "DRAWS");
            require(draws.size() == count(input, 2048), "original attached draw count differs");
            for (const auto& draw : draws) {
                tag(input, "DRAW");
                for (const auto value : {draw.original_index, draw.resource.descriptor_id, draw.resource.image_id,
                                        draw.resource.resource_key, std::uint32_t(draw.frame_index),
                                        std::uint32_t(draw.parent), draw.attachment_order})
                    require(value == static_cast<std::uint32_t>(count(input, 0xffffffffU)), "original draw binding or parent order differs");
                std::array<std::uint32_t, 16> bits{};
                std::memcpy(bits.data(), draw.model.data(), sizeof(bits));
                for (const auto value : bits)
                    require(value == static_cast<std::uint32_t>(count(input, 0xffffffffU)), "original terrain model matrix differs");
            }
            tag(input, "END");
        }
        std::string extra;
        require(!(input >> extra), "trailing terrain sprite fixture data");
        contracts();
        std::cout << "terrain sprite decorations: " << cases << " original cases, " << snapshots
                  << " snapshots, " << checks << " comparisons/contracts passed\n";
    } catch (const std::exception& error) {
        std::cerr << error.what() << '\n';
        return 1;
    }
    return 0;
}
