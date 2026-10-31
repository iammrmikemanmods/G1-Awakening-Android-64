#include "g1/World.hpp"

#include <algorithm>
#include <functional>
#include <fstream>
#include <iostream>
#include <map>
#include <stdexcept>

namespace {
std::size_t checks{};

void check(bool value, const char* message) {
    ++checks;
    if (!value) throw std::runtime_error(message);
}

void rejects(const std::function<void()>& action, const char* message) {
    try { action(); }
    catch (const g1::FormatError&) { ++checks; return; }
    throw std::runtime_error(message);
}

void append16(g1::Bytes& bytes, std::uint16_t value) {
    bytes.push_back(static_cast<std::uint8_t>(value >> 8U));
    bytes.push_back(static_cast<std::uint8_t>(value & 255U));
}

void put32(g1::Bytes& bytes, std::size_t at, std::size_t value) {
    for (std::size_t n = 0; n < 4U; ++n)
        bytes.at(at + n) = static_cast<std::uint8_t>((value >> (8U * n)) & 255U);
}

g1::Bytes mission(std::initializer_list<std::vector<std::uint16_t>> commands) {
    g1::Bytes bytes{1, static_cast<std::uint8_t>(commands.size())};
    for (const auto& command : commands)
        for (const auto word : command) append16(bytes, word);
    return bytes;
}

g1::Bytes definition(std::uint8_t faction, std::uint8_t budget = 4U) {
    // Deliberately constructed format input; it is not copied game data.
    g1::Bytes bytes{0x12, 0x34, 0xff, 0xfe, 2, 0x80, 0, 0, 9,
                    faction, 7, 1, 2, 3, 3, 6, 7, 8, 2, 14};
    bytes.insert(bytes.end(), {budget, 2, 3, 4, 5, 6, 0xff, 8, 9, 10, 11, 12, 13, 14});
    bytes.push_back(14);
    bytes.insert(bytes.end(), {9, 8, 7, 6, 5, 4, 1, 2, 1, 0, 1, 2, 3, 4});
    return bytes;
}

using Payloads = std::map<std::uint32_t, g1::Bytes>;

Payloads resources(const g1::Bytes& script) {
    Payloads result;
    for (std::uint8_t type = 0; type < 23U; ++type)
        result[g1::unit_definition_resource(type)] = definition(type == 19U ? 1U : 0U);
    result[306] = {4, 3, 1, 9, 0, 3, 3, 8, 0, 3, 2, 7, 1, 3, 99, 6, 0};
    result[268] = {4, 4, 0, 1, 6, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0};
    result[282] = g1::Bytes(33, 2); result[282][0] = 1;
    result[280] = result[282]; result[281] = result[282];
    result[315] = script;
    return result;
}

g1::Archive archive(const Payloads& payloads, std::uint32_t key = 0xf4e02223U) {
    const auto count = payloads.size();
    const auto members = 32U + count * 8U;
    const auto body = members + (count + 1U) * 8U;
    g1::Bytes bytes(body, 0);
    put32(bytes, 0, 0x42494746U); put32(bytes, 4, 0x00800001U);
    put32(bytes, 8, 32U); put32(bytes, 12, count);
    put32(bytes, 16, members); put32(bytes, 20, count); put32(bytes, 24, body);
    std::size_t ordinal{};
    for (const auto& item : payloads) {
        const auto group = 32U + ordinal * 8U;
        put32(bytes, group, item.first);
        bytes[group + 4U] = 1;
        bytes[group + 6U] = static_cast<std::uint8_t>(ordinal & 255U);
        bytes[group + 7U] = static_cast<std::uint8_t>(ordinal >> 8U);
        put32(bytes, members + ordinal * 8U, key);
        put32(bytes, members + ordinal * 8U + 4U, bytes.size());
        bytes.insert(bytes.end(), {4, 0, 0, 0});
        bytes.insert(bytes.end(), item.second.begin(), item.second.end());
        ++ordinal;
    }
    put32(bytes, members + count * 8U + 4U, bytes.size());
    put32(bytes, 28, bytes.size() - body);
    return g1::Archive(std::move(bytes));
}

g1::Bytes prefix() {
    return mission({{0}, {16, 1234}, {9, 0, 0}, {6, 11, 0, 0, 0, 0},
                    {6, 19, 3, 3, 0, 0},
                    {7, 1, 2, 2, 3, 0, 1, 0, 1, 2, 1, 1, 2, 1},
                    {15, 2, 11, 19}, {4, 0xfffe, 3, 1}, {2}, {3, 4, 99}, {1}});
}
} // namespace

int main(int argc, char** argv) {
    try {
        check(g1::campaign_mission_resource(0) == 315U &&
              g1::campaign_mission_resource(16) == 323U, "campaign table boundary mismatch");
        check(g1::mission_grid_resource(0) == 268U &&
              g1::mission_grid_resource(21) == 262U, "map table boundary mismatch");
        check(g1::unit_definition_resource(11) == 284U &&
              g1::unit_definition_resource(19) == 286U, "unit type/resource mismatch");
        check(g1::mission_lookup_resource(0) == 282U &&
              g1::mission_lookup_resource(1) == 280U &&
              g1::mission_lookup_resource(2) == 281U, "style/lookup order changed");
        rejects([] { (void)g1::campaign_mission_resource(17); }, "campaign overflow accepted");
        rejects([] { (void)g1::mission_grid_resource(22); }, "map overflow accepted");
        rejects([] { (void)g1::unit_definition_resource(23); }, "unit overflow accepted");
        rejects([] { (void)g1::mission_lookup_resource(3); }, "style overflow accepted");

        const auto bytes = definition(1);
        const auto decoded = g1::decode_unit_definition(bytes, 19, 286);
        check(decoded.faction() == 1U && decoded.forms.size() == 2U &&
              decoded.forms[0][0] == 4U && decoded.forms[1][0] == 9U,
              "definition fields/forms changed");
        check(decoded.raw_text_indices == std::array<std::uint16_t, 2>{{0x1234, 0xfffe}} &&
              decoded.raw_detail_text_indices == std::vector<std::uint16_t>({0x8000, 9}) &&
              decoded.auxiliary_fields == g1::Bytes({6, 7, 8}),
              "raw unit fields were reinterpreted");
        for (std::size_t size = 0; size < bytes.size(); ++size) {
            const g1::Bytes truncated(bytes.begin(), bytes.begin() + static_cast<std::ptrdiff_t>(size));
            rejects([&] { (void)g1::decode_unit_definition(truncated, 19, 286); },
                    "unit definition truncation accepted");
        }
        auto extra = bytes; extra.push_back(0);
        rejects([&] { (void)g1::decode_unit_definition(extra, 19, 286); }, "unit trailing byte accepted");
        auto wrong_width = bytes; wrong_width[19] = 13;
        rejects([&] { (void)g1::decode_unit_definition(wrong_width, 19, 286); }, "unknown form width accepted");
        auto no_form = bytes; no_form[18] = 0;
        rejects([&] { (void)g1::decode_unit_definition(no_form, 19, 286); }, "empty form list accepted");

        g1::TerrainLookup raw{}; raw.fill(2);
        const auto style0 = g1::apply_mission_style(raw, 0);
        const auto style1 = g1::apply_mission_style(raw, 1);
        const auto style2 = g1::apply_mission_style(raw, 2);
        check(style0[0] == 0U && style0[1] == 1U && style0[5] == 1U &&
              style0[6] == 3U && style0[12] == 3U && style0[13] == 3U &&
              style0[26] == 1U && style0[27] == 1U && style0[31] == 0U,
              "style zero overwrite mismatch");
        check(style1 == raw && style2[8] == 1U && style2[19] == 0U &&
              style2[30] == 1U && style2[31] == 1U && style2[7] == 2U,
              "style one/two override mismatch");

        const auto world = g1::bootstrap_first_campaign(archive(resources(prefix())));
        check(world.mission_resource_id == 315U && world.grid_resource_id == 268U &&
              world.lookup_resource_id == 282U && world.style == 0U,
              "bootstrap resource association mismatch");
        check(world.stop == g1::BootstrapStop::presentation_required &&
              world.next_command_index == 9U && world.commands[9].opcode == 3U &&
              world.commands.size() == 11U, "presentation command was consumed or later commands ran");
        check(world.title_text_index == 1234U && !world.flag_225 && world.last_phase_opcode == 2U,
              "phase/title state mismatch");
        check(!world.flag_227 && world.initialization_effects.size() == 3U,
              "bootstrap omitted initial turn/debit metadata");
        const auto& turn = world.initialization_effects[0];
        check(turn.kind == g1::WorldInitializationEffectKind::begin_current_player_turn &&
              turn.command_index == 2U && turn.owner == 0U && turn.sites_at_turn_start.empty(),
              "initial turn observed later site placement");
        for (std::size_t i = 1; i < world.initialization_effects.size(); ++i) {
            const auto& debit = world.initialization_effects[i];
            check(debit.kind == g1::WorldInitializationEffectKind::debit_spawn_funds &&
                  debit.command_index == i + 2U && debit.owner == i - 1U && debit.signed_cost == 7 &&
                  debit.active_count_increment == 1U,
                  "script placement debit order/cost changed");
        }
        check(world.units[0].spawn.health == 100U && !world.units[0].spawn.flag_18 &&
              !world.units[0].spawn.damage_immune, "ordinary activation properties changed");
        auto cost_definition = decoded;
        cost_definition.scalar_fields[1] = 255;
        check(g1::unit_spawn_cost(cost_definition) == -1, "spawn cost lost signed-byte semantics");
        cost_definition.type = 21;
        check(g1::unit_spawn_cost(cost_definition) == 40, "type21 loader cost override omitted");
        check(g1::apply_spawn_funds_debit(1,17) == 0 &&
              g1::apply_spawn_funds_debit(50,17) == 33 &&
              g1::apply_spawn_funds_debit(0,-1) == 1,
              "spawn debit clamp/sign mismatch");
        const auto flag_on = g1::bootstrap_first_campaign(archive(resources(mission({{13}}))));
        const auto flag_off = g1::bootstrap_first_campaign(archive(resources(mission({{13},{14}}))));
        check(flag_on.flag_227 && !flag_off.flag_227, "opcode13/14 combat flag mismatch");
        check(world.units.size() == 2U && world.units[0].type == 11U &&
              world.units[0].owner == 0U && world.units[1].type == 19U &&
              world.units[1].owner == 1U && world.units[0].form == 0U &&
              world.units[1].form == 0U && world.units[0].spent_movement == 0U,
              "default faction ownership or spawn form mismatch");
        check(world.units[0].active && world.units[1].active &&
              world.units[0].placement_command_index == 3U &&
              world.units[1].placement_command_index == 4U,
              "placement identity/active state mismatch");
        check(world.camera && world.camera->x == -2 && world.camera->y == 3 &&
              world.camera->immediate, "signed camera words were not preserved");
        check(std::count(world.available_types.begin(), world.available_types.end(), true) == 2 &&
              world.available_types[11] && world.available_types[19], "available roster mismatch");
        check(world.sites.size() == 1U && world.sites[0].state == -1 &&
              world.sites[0].position.x == 2U && world.sites[0].position.y == 2U &&
              world.sites[0].coordinate_groups.size() == 3U &&
              world.sites[0].coordinate_groups[0].empty() &&
              world.sites[0].coordinate_groups[1].size() == 1U &&
              world.sites[0].coordinate_groups[2].size() == 2U,
              "site/group initialization mismatch");
        const auto board = world.movement_board();
        const auto profile = world.movement_profile(0);
        check(board.cells[0].active_owner == 0 && board.cells[15].active_owner == 1 &&
              board.cells[10].active_owner == -1 && board.points.empty() &&
              board.cost99_regions.empty(), "occupancy or site/movement-point distinction changed");
        check(board.cells[0].terrain_cost == 1U && board.cells[1].terrain_cost == 3U &&
              board.cells[2].terrain_cost == 99U, "terrain rule input mismatch");
        check(profile.base_budget == 4U && profile.spent_budget == 0U &&
              profile.traversal_field6 == -1, "selected form movement profile mismatch");
        const auto range = g1::compute_movement_range(board, profile, 0, 0);
        check(range.at(0, 0) == 4 && range.at(0, 1) == 3 && range.at(1, 0) == 3 &&
              range.at(2, 0) == -1 && range.at(3, 3) == -1 && range.has_unoccupied_destination,
              "bootstrap did not feed recovered movement rules correctly");
        rejects([&] { (void)world.movement_profile(2); }, "missing unit profile accepted");

        const auto unsupported = g1::bootstrap_first_campaign(archive(resources(mission({{9, 0, 0}, {1}}))));
        check(unsupported.stop == g1::BootstrapStop::unsupported_command &&
              unsupported.next_command_index == 1U, "unsupported command silently consumed");
        for (const auto& command : {std::vector<std::uint16_t>{6, 0xffff, 0, 0, 0, 0},
                                   std::vector<std::uint16_t>{6, 11, 0, 0, 1, 0},
                                   std::vector<std::uint16_t>{6, 11, 0, 0, 0, 1}}) {
            const auto stopped = g1::bootstrap_first_campaign(archive(resources(mission({{9, 0, 0}, command}))));
            check(stopped.stop == g1::BootstrapStop::unsupported_command && stopped.units.empty() &&
                  stopped.next_command_index == 1U, "unsupported spawn branch mutated roster");
        }
        const auto ended = g1::bootstrap_first_campaign(archive(resources(mission({{9, 0, 0}}))));
        check(ended.stop == g1::BootstrapStop::end_of_stream && ended.next_command_index == 1U,
              "end-of-stream cursor mismatch");
        rejects([&] { (void)g1::bootstrap_first_campaign(archive(resources(mission({{6, 11, 0, 0, 0, 0}})))); },
                "placement before map accepted");
        rejects([&] { (void)g1::bootstrap_first_campaign(archive(resources(mission({{9, 0, 0}, {6, 11, 4, 0, 0, 0}})))); },
                "out-of-map placement accepted");
        rejects([&] { (void)g1::bootstrap_first_campaign(archive(resources(mission({{9, 0, 0}, {6, 11, 0, 0, 0, 0}, {6, 11, 1, 0, 0, 0}})))); },
                "duplicate active type accepted");
        auto missing = resources(prefix()); missing.erase(284);
        rejects([&] { (void)g1::bootstrap_first_campaign(archive(missing)); }, "missing definition accepted");
        rejects([&] { (void)g1::bootstrap_first_campaign(archive(resources(prefix()), 0U)); }, "wrong member key accepted");
        auto wrong_faction = resources(prefix()); wrong_faction[284] = definition(2);
        rejects([&] { (void)g1::bootstrap_first_campaign(archive(wrong_faction)); }, "unowned faction accepted");
        auto invalid_board = world; invalid_board.grid.tiles[0] = 255;
        rejects([&] { (void)invalid_board.movement_board(); }, "empty tile movement rule invented");
        invalid_board = world; invalid_board.units[1].x = 0; invalid_board.units[1].y = 0;
        rejects([&] { (void)invalid_board.movement_board(); }, "overlapping active occupancy accepted");
        if (argc > 2) throw std::runtime_error("usage: world_test [original_funds_fixture]");
        if (argc == 2) {
            std::ifstream input(argv[1]);
            if (!input) throw std::runtime_error("cannot open original world funds fixture");
            int type{}, raw{}, expected_cost{};
            std::int32_t funds{}, expected_funds{};
            std::size_t rows{};
            while (input >> type >> raw >> funds >> expected_cost >> expected_funds) {
                if (type < 0 || type >= 23 || raw < 0 || raw > 255)
                    throw std::runtime_error("invalid original funds fixture row");
                g1::UnitDefinition value;
                value.type = static_cast<std::uint8_t>(type);
                value.scalar_fields[1] = static_cast<std::uint8_t>(raw);
                const auto cost = g1::unit_spawn_cost(value);
                check(cost == expected_cost, "original loader cost differs");
                check(g1::apply_spawn_funds_debit(funds,cost) == expected_funds,
                      "original spawn funds differ");
                ++rows;
            }
            if (!input.eof() || rows == 0) throw std::runtime_error("malformed/empty funds fixture");
            std::cout << "Compared " << rows << " original loader/debit vectors.\n";
        }
        std::cout << "Passed " << checks << " world bootstrap contract checks; synthetic data only.\n";
        return 0;
    } catch (const std::exception& error) {
        std::cerr << "FAIL: " << error.what() << '\n';
        return 1;
    }
}
