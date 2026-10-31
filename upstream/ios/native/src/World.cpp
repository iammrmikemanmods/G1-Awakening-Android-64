#include "g1/World.hpp"

#include <algorithm>
#include <limits>
#include <utility>

namespace g1 {
namespace {

// Original read-only tables at 0xcecc0, 0xcec18 and 0xcec60. Values are the
// low 24 bits of tagged resource IDs. The tag is not an archive member key.
constexpr std::array<std::uint32_t, 23> unit_resources{{
    292, 294, 290, 302, 291, 283, 285, 295, 305, 301, 288, 284,
    287, 299, 304, 300, 293, 289, 303, 286, 296, 297, 298}};
constexpr std::array<std::uint32_t, 17> campaign_resources{{
    315, 326, 333, 334, 335, 336, 337, 338, 339,
    316, 317, 318, 319, 320, 321, 322, 323}};
constexpr std::array<std::uint32_t, 22> grid_resources{{
    268, 274, 277, 272, 276, 263, 265, 266, 278, 270, 271,
    273, 275, 267, 279, 269, 264, 258, 259, 260, 261, 262}};
constexpr std::uint32_t data_key = 0xf4e02223U;

void require(bool condition, const char* message) {
    if (!condition) throw FormatError(message);
}

Bytes resource(const Archive& archive, std::uint32_t id) {
    const auto found = archive.find(id, data_key);
    require(found.size() == 1U, "world resource ID/key is missing or ambiguous");
    return archive.read(found.front());
}

class ByteReader {
public:
    explicit ByteReader(const Bytes& data) : data_(data) {}
    std::size_t remaining() const noexcept { return data_.size() - offset_; }
    std::uint8_t byte() {
        require(remaining() != 0U, "truncated unit definition");
        return data_[offset_++];
    }
    std::uint16_t word() {
        const auto high = byte();
        const auto low = byte();
        return static_cast<std::uint16_t>((std::uint16_t(high) << 8U) | low);
    }
    Bytes counted_bytes() {
        const auto count = byte();
        require(count <= remaining(), "unit byte-vector count exceeds resource");
        Bytes result;
        result.reserve(count);
        for (std::size_t i = 0; i < count; ++i) result.push_back(byte());
        return result;
    }
private:
    const Bytes& data_;
    std::size_t offset_{};
};

std::int16_t signed_word(std::uint16_t value) noexcept {
    const auto widened = static_cast<std::int32_t>(value);
    return static_cast<std::int16_t>(value < 0x8000U ? widened : widened - 0x10000);
}

std::int8_t signed_byte(std::uint8_t value) noexcept {
    return static_cast<std::int8_t>(value < 0x80U ? static_cast<int>(value)
                                               : static_cast<int>(value) - 0x100);
}

void argument_count(const MissionCommand& command, std::size_t count) {
    require(command.argument_words_u16.size() == count,
            "bootstrap command has an inconsistent argument count");
}

void require_position(const TerrainGrid& grid, std::uint16_t x, std::uint16_t y) {
    require(x < grid.width && y < grid.height,
            "bootstrap coordinate is outside the loaded map");
}

std::vector<WorldSite> decode_sites(const MissionCommand& command,
                                  const TerrainGrid& grid) {
    const auto& words = command.argument_words_u16;
    std::size_t cursor = 0;
    const auto word = [&]() -> std::uint16_t {
        require(cursor < words.size(), "truncated bootstrap site list");
        return words[cursor++];
    };
    const auto count = word();
    require(count <= (words.size() - cursor) / 3U, "bootstrap site count exceeds arguments");
    std::vector<WorldSite> sites;
    for (std::size_t i = 0; i < count; ++i) {
        const auto x = word();
        const auto y = word();
        require_position(grid, x, y);
        WorldSite site{{static_cast<std::uint8_t>(x), static_cast<std::uint8_t>(y)}, -1, {}};
        const auto groups = word();
        require(groups <= words.size() - cursor, "bootstrap site group count exceeds arguments");
        for (std::size_t group = 0; group < groups; ++group) {
            const auto pairs = word();
            require(pairs <= (words.size() - cursor) / 2U,
                    "bootstrap site coordinate count exceeds arguments");
            std::vector<WorldCoordinate> coordinates;
            for (std::size_t pair = 0; pair < pairs; ++pair) {
                const auto px = word();
                const auto py = word();
                require_position(grid, px, py);
                coordinates.push_back({static_cast<std::uint8_t>(px), static_cast<std::uint8_t>(py)});
            }
            site.coordinate_groups.push_back(std::move(coordinates));
        }
        sites.push_back(std::move(site));
    }
    require(cursor == words.size(), "bootstrap site list has trailing arguments");
    return sites;
}

} // namespace

UnitDefinition decode_unit_definition(const Bytes& bytes, std::uint8_t type,
                                      std::uint32_t resource_id) {
    require(type < unit_resources.size(), "unsupported unit definition type");
    require(!bytes.empty() && bytes.size() <= Archive::max_resource_size,
            "invalid unit definition size");
    ByteReader reader(bytes);
    UnitDefinition definition;
    definition.type = type;
    definition.resource_id = resource_id;
    for (auto& index : definition.raw_text_indices) index = reader.word();
    const auto text_count = reader.byte();
    require(text_count <= reader.remaining() / 2U, "unit text-reference count exceeds resource");
    for (std::size_t i = 0; i < text_count; ++i)
        definition.raw_detail_text_indices.push_back(reader.word());
    for (auto& field : definition.scalar_fields) field = reader.byte();
    definition.auxiliary_fields = reader.counted_bytes();
    const auto form_count = reader.byte();
    require(form_count != 0U && form_count <= reader.remaining(),
            "unit form count is empty or exceeds resource");
    for (std::size_t i = 0; i < form_count; ++i) {
        auto form = reader.counted_bytes();
        // This is the recovered G1 form-record variant, not a generic Swerve
        // schema. Reject a different layout rather than reinterpret its fields.
        require(form.size() == 14U, "unsupported unit form record width");
        definition.forms.push_back(std::move(form));
    }
    require(reader.remaining() == 0U, "unit definition has trailing bytes");
    return definition;
}

std::uint32_t unit_definition_resource(std::uint8_t type) {
    require(type < unit_resources.size(), "unit type is outside the original resource table");
    return unit_resources[type];
}

std::int8_t unit_spawn_cost(const UnitDefinition& definition) {
    require(definition.type < unit_resources.size(), "spawn cost has an invalid unit type");
    return definition.type == 21U ? std::int8_t(40) : signed_byte(definition.scalar_fields[1]);
}

std::int32_t apply_spawn_funds_debit(std::int32_t funds, std::int8_t cost) noexcept {
    const auto bits = static_cast<std::uint32_t>(funds) - static_cast<std::uint32_t>(cost);
    const auto signed_result = bits <= 0x7fffffffU ? static_cast<std::int64_t>(bits) :
        static_cast<std::int64_t>(bits) - 0x100000000LL;
    return signed_result < 0 ? 0 : static_cast<std::int32_t>(signed_result);
}

std::uint32_t campaign_mission_resource(std::size_t index) {
    require(index < campaign_resources.size(), "campaign index is outside the original resource table");
    return campaign_resources[index];
}

std::uint32_t mission_grid_resource(std::size_t index) {
    require(index < grid_resources.size(), "map index is outside the original resource table");
    return grid_resources[index];
}

std::uint32_t mission_lookup_resource(std::uint8_t style) {
    // 0x56cd4..0x56cea, 0x57898..0x578cc. This order differs from the
    // atlas/style table and must not be inferred from resource numbering.
    constexpr std::array<std::uint32_t, 3> resources{{282, 280, 281}};
    require(style < resources.size(), "unsupported mission terrain style");
    return resources[style];
}

TerrainLookup apply_mission_style(const TerrainLookup& raw, std::uint8_t style) {
    (void)mission_lookup_resource(style); // Reject unsupported styles.
    auto effective = raw;
    if (style == 0U) {
        // Original 0x56d3c..0x56de0 discards the raw lookup for this style.
        effective.fill(0);
        for (const auto tile : {1U, 2U, 3U, 4U, 5U, 26U, 27U}) effective[tile] = 1;
        for (const auto tile : {6U, 12U, 13U}) effective[tile] = 3;
    } else if (style == 2U) {
        // Original 0x56d08..0x56d3a adjusts four decoded entries.
        effective[8] = 1;
        effective[19] = 0;
        effective[30] = 1;
        effective[31] = 1;
    }
    return effective;
}

MovementBoard WorldBootstrap::movement_board() const {
    require(grid.width != 0U && grid.height != 0U &&
                grid.tiles.size() == std::size_t(grid.width) * grid.height,
            "bootstrap has no complete grid");
    MovementBoard board;
    board.width = grid.width;
    board.height = grid.height;
    board.cells.reserve(grid.tiles.size());
    for (const auto tile : grid.tiles) {
        require(tile < effective_lookup.size(), "empty or unsupported tile in movement board");
        const auto terrain = effective_lookup[tile];
        require(terrain < terrain_rules.size(), "tile maps outside terrain-rule table");
        const auto& fields = terrain_rules[terrain].fields;
        board.cells.push_back({fields[0], fields[2], -1});
    }
    for (const auto& unit : units) {
        if (!unit.active) continue;
        require_position(grid, unit.x, unit.y);
        require(unit.owner < player_factions.size(), "active unit has no supported owner");
        auto& cell = board.cells[std::size_t(unit.y) * grid.width + unit.x];
        require(cell.active_owner == -1, "overlapping active placements are not supported by bootstrap");
        cell.active_owner = unit.owner;
    }
    // Opcode-7 sites at+0x3f0 are distinct from opcode17 points at+0x420.
    // The original parses opcode21 but does not install cost99 rectangles.
    for (const auto& point:site_points) board.points.push_back(point.point);
    board.cost99_regions=cost99_regions;
    return board;
}

MovementProfile WorldBootstrap::movement_profile(std::size_t index) const {
    require(index < units.size(), "active unit index is outside bootstrap roster");
    const auto& unit = units[index];
    require(unit.type < unit_definitions.size(), "unit has no decoded definition");
    const auto& definition = unit_definitions[unit.type];
    require(definition.type == unit.type && unit.form < definition.forms.size(),
            "unit definition order or selected form is invalid");
    const auto& fields = definition.forms[unit.form];
    require(fields.size() == 14U, "selected unit form has an unsupported layout");
    return {unit.type, unit.owner, fields[0], unit.spent_movement, signed_byte(fields[6])};
}

WorldBootstrap bootstrap_first_campaign(const Archive& archive) {
    WorldBootstrap world;
    world.mission_resource_id = campaign_mission_resource(0);
    world.terrain_rules = decode_terrain_rules(resource(archive, 306));
    for (std::size_t type = 0; type < unit_resources.size(); ++type) {
        const auto id = unit_resources[type];
        world.unit_definitions.push_back(decode_unit_definition(
            resource(archive, id), static_cast<std::uint8_t>(type), id));
    }
    world.commands = MissionStream(resource(archive, world.mission_resource_id)).commands();
    bool map_loaded = false;
    for (; world.next_command_index < world.commands.size(); ++world.next_command_index) {
        const auto& command = world.commands[world.next_command_index];
        const auto& args = command.argument_words_u16;
        switch (command.opcode) {
        case 0:
            argument_count(command, 0);
            world.flag_225 = true;
            world.last_phase_opcode = 0;
            break;
        case 16:
            argument_count(command, 1);
            world.title_text_index = args[0];
            world.last_phase_opcode = 16;
            break;
        case 9: {
            argument_count(command, 2);
            // Replacing a live world also resets route/unit/resource objects.
            // Only the proven first, empty-world load is supported here.
            require(!map_loaded && world.units.empty(), "bootstrap cannot replace an active map");
            require(args[1] <= 2U, "unsupported mission terrain style");
            world.style = static_cast<std::uint8_t>(args[1]);
            world.grid_resource_id = mission_grid_resource(args[0]);
            world.lookup_resource_id = mission_lookup_resource(world.style);
            world.grid = decode_terrain_grid(resource(archive, world.grid_resource_id));
            world.effective_lookup = apply_mission_style(
                decode_terrain_lookup(resource(archive, world.lookup_resource_id)), world.style);
            // 0x56e38 ->0x4e80c. The original map loader has cleared sites at
            // this point; recording the snapshot preserves that ordering.
            world.initialization_effects.push_back({
                WorldInitializationEffectKind::begin_current_player_turn,
                command.instruction_index, 0, 0, world.sites});
            map_loaded = true;
            break;
        }
        case 6: {
            argument_count(command, 5);
            require(map_loaded, "unit placement precedes map load");
            // Negative type sentinels select randomized inactive units. The
            // fourth word may flip faction and the fifth alters unit flags.
            // Those branches are outside this recovered cold-start slice.
            if (args[0] >= unit_resources.size() || args[3] != 0U || args[4] != 0U) {
                world.stop = BootstrapStop::unsupported_command;
                world.stop_detail = "opcode 6 sentinel/faction/special-placement branch is not implemented";
                return world;
            }
            require_position(world.grid, args[1], args[2]);
            const auto type = static_cast<std::uint8_t>(args[0]);
            require(std::none_of(world.units.begin(), world.units.end(), [type](const auto& unit) {
                return unit.type == type;
            }), "bootstrap cannot place an already active unit type");
            const auto faction = world.unit_definitions[type].faction();
            const auto owner = std::find(world.player_factions.begin(), world.player_factions.end(), faction);
            require(owner != world.player_factions.end(), "unit faction has no configured player");
            world.units.push_back({type, faction,
                static_cast<std::uint8_t>(owner - world.player_factions.begin()),
                0, static_cast<std::uint8_t>(args[1]), static_cast<std::uint8_t>(args[2]),
                0, true, command.instruction_index, {}});
            world.initialization_effects.push_back({
                WorldInitializationEffectKind::debit_spawn_funds,
                command.instruction_index, world.units.back().owner,
                unit_spawn_cost(world.unit_definitions[type]), {}, 1});
            break;
        }
        case 7:
            require(map_loaded, "site placement precedes map load");
            world.sites = decode_sites(command, world.grid);
            break;
        case 15:
            require(!args.empty() && std::size_t(args.front()) == args.size() - 1U,
                    "available-unit list has inconsistent length");
            world.available_types.fill(false);
            for (std::size_t i = 1; i < args.size(); ++i) {
                require(args[i] < world.available_types.size(), "available-unit type is out of range");
                world.available_types[args[i]] = true;
            }
            break;
        case 4:
            argument_count(command, 3);
            world.camera = WorldCameraRequest{signed_word(args[0]), signed_word(args[1]), args[2] != 0U};
            world.last_phase_opcode = 4;
            break;
        case 2:
            argument_count(command, 0);
            world.flag_225 = false;
            world.last_phase_opcode = 2;
            break;
        case 13:
        case 14:
            argument_count(command, 0);
            world.flag_227 = command.opcode == 13U;
            world.last_phase_opcode = command.opcode;
            break;
        case 3:
            world.stop = BootstrapStop::presentation_required;
            world.stop_detail = "opcode 3 presentation handler is pending; command has not executed";
            return world;
        default:
            world.stop = BootstrapStop::unsupported_command;
            world.stop_detail = "mission opcode " + std::to_string(command.opcode) + " is not implemented by bootstrap";
            return world;
        }
    }
    world.stop = BootstrapStop::end_of_stream;
    world.stop_detail = "decoded stream ended; this does not establish mission completion";
    return world;
}

} // namespace g1
