#include "g1/World.hpp"

#include <iostream>

namespace {
template<class Container>
void integers(const Container& values) {
    std::cout << '[';
    bool comma = false;
    for (const auto value : values) {
        if (comma) std::cout << ',';
        std::cout << static_cast<std::int64_t>(value);
        comma = true;
    }
    std::cout << ']';
}

const char* stop_name(g1::BootstrapStop stop) {
    switch (stop) {
    case g1::BootstrapStop::presentation_required: return "presentation_required";
    case g1::BootstrapStop::execution_required: return "execution_required";
    case g1::BootstrapStop::unsupported_command: return "unsupported_command";
    case g1::BootstrapStop::end_of_stream: return "end_of_stream";
    }
    throw g1::FormatError("invalid bootstrap stop enum");
}
} // namespace

int main(int argc, char** argv) {
    if (argc != 2) {
        std::cerr << "usage: world_probe /path/to/iphone.big\n";
        return 2;
    }
    try {
        const g1::Archive archive(g1::read_file(argv[1]));
        const auto world = g1::bootstrap_first_campaign(archive);
        const auto board = world.movement_board();
        std::cout << "{\"campaign_resources\":[";
        for (std::size_t i = 0; i < 17U; ++i) {
            if (i) std::cout << ',';
            std::cout << g1::campaign_mission_resource(i);
        }
        std::cout << "],\"grid_resources\":[";
        for (std::size_t i = 0; i < 22U; ++i) {
            if (i) std::cout << ',';
            std::cout << g1::mission_grid_resource(i);
        }
        std::cout << "],\"style_lookups\":[";
        for (std::uint8_t style = 0; style < 3U; ++style) {
            if (style) std::cout << ',';
            const auto id = g1::mission_lookup_resource(style);
            const auto found = archive.find(id, 0xf4e02223U);
            if (found.size() != 1U) throw g1::FormatError("probe style lookup missing or ambiguous");
            const auto raw = g1::decode_terrain_lookup(archive.read(found.front()));
            std::cout << "{\"style\":" << unsigned(style) << ",\"resource_id\":" << id
                      << ",\"raw\":";
            integers(raw);
            std::cout << ",\"effective\":"; integers(g1::apply_mission_style(raw, style));
            std::cout << '}';
        }
        std::cout << "],\"mission_resource_id\":" << world.mission_resource_id
                  << ",\"grid_resource_id\":" << world.grid_resource_id
                  << ",\"lookup_resource_id\":" << world.lookup_resource_id
                  << ",\"style\":" << unsigned(world.style)
                  << ",\"width\":" << world.grid.width << ",\"height\":" << world.grid.height
                  << ",\"next_command_index\":" << world.next_command_index
                  << ",\"command_count\":" << world.commands.size()
                  << ",\"stop\":\"" << stop_name(world.stop) << "\",\"next_opcode\":";
        if (world.next_command_index < world.commands.size())
            std::cout << world.commands[world.next_command_index].opcode;
        else std::cout << "null";
        std::cout << ",\"title_text_index\":";
        if (world.title_text_index) std::cout << *world.title_text_index;
        else std::cout << "null";
        std::cout << ",\"flag_225\":" << (world.flag_225 ? "true" : "false")
                  << ",\"last_phase_opcode\":" << world.last_phase_opcode
                  << ",\"camera\":";
        if (world.camera) std::cout << "{\"x\":" << world.camera->x << ",\"y\":" << world.camera->y
                                   << ",\"immediate\":" << (world.camera->immediate ? "true" : "false") << '}';
        else std::cout << "null";
        std::cout << ",\"player_factions\":"; integers(world.player_factions);
        std::cout << ",\"available_types\":[";
        bool comma = false;
        for (std::size_t type = 0; type < world.available_types.size(); ++type) {
            if (!world.available_types[type]) continue;
            if (comma) std::cout << ',';
            std::cout << type; comma = true;
        }
        std::cout << "],\"tiles\":"; integers(world.grid.tiles);
        std::cout << ",\"effective_lookup\":"; integers(world.effective_lookup);
        std::cout << ",\"terrain_rules\":[";
        for (std::size_t n = 0; n < world.terrain_rules.size(); ++n) {
            if (n) std::cout << ',';
            integers(world.terrain_rules[n].fields);
        }
        std::cout << "],\"definitions\":[";
        for (std::size_t n = 0; n < world.unit_definitions.size(); ++n) {
            if (n) std::cout << ',';
            const auto& definition = world.unit_definitions[n];
            std::cout << "{\"type\":" << unsigned(definition.type)
                      << ",\"resource_id\":" << definition.resource_id << ",\"raw_text_indices\":";
            integers(definition.raw_text_indices);
            std::cout << ",\"raw_detail_text_indices\":"; integers(definition.raw_detail_text_indices);
            std::cout << ",\"scalar_fields\":"; integers(definition.scalar_fields);
            std::cout << ",\"auxiliary_fields\":"; integers(definition.auxiliary_fields);
            std::cout << ",\"forms\":[";
            for (std::size_t form = 0; form < definition.forms.size(); ++form) {
                if (form) std::cout << ',';
                integers(definition.forms[form]);
            }
            std::cout << "]}";
        }
        std::cout << "],\"sites\":[";
        for (std::size_t n = 0; n < world.sites.size(); ++n) {
            if (n) std::cout << ',';
            const auto& site = world.sites[n];
            std::cout << "{\"x\":" << unsigned(site.position.x) << ",\"y\":" << unsigned(site.position.y)
                      << ",\"state\":" << int(site.state) << ",\"coordinate_groups\":[";
            for (std::size_t group = 0; group < site.coordinate_groups.size(); ++group) {
                if (group) std::cout << ',';
                std::cout << '[';
                for (std::size_t pair = 0; pair < site.coordinate_groups[group].size(); ++pair) {
                    if (pair) std::cout << ',';
                    const auto point = site.coordinate_groups[group][pair];
                    std::cout << '[' << unsigned(point.x) << ',' << unsigned(point.y) << ']';
                }
                std::cout << ']';
            }
            std::cout << "]}";
        }
        std::cout << "],\"units\":[";
        for (std::size_t n = 0; n < world.units.size(); ++n) {
            if (n) std::cout << ',';
            const auto& unit = world.units[n];
            const auto profile = world.movement_profile(n);
            const auto range = g1::compute_movement_range(board, profile, unit.x, unit.y);
            std::cout << "{\"type\":" << unsigned(unit.type) << ",\"faction\":" << unsigned(unit.faction)
                      << ",\"owner\":" << unsigned(unit.owner) << ",\"form\":" << unsigned(unit.form)
                      << ",\"x\":" << unsigned(unit.x) << ",\"y\":" << unsigned(unit.y)
                      << ",\"spent_budget\":" << unsigned(unit.spent_movement)
                      << ",\"base_budget\":" << unsigned(profile.base_budget)
                      << ",\"field6\":" << int(profile.traversal_field6)
                      << ",\"active\":" << (unit.active ? "true" : "false")
                      << ",\"placement_command_index\":" << unit.placement_command_index
                      << ",\"remaining_budget\":" << unsigned(g1::remaining_movement_budget(profile))
                      << ",\"has_unoccupied_destination\":" << (range.has_unoccupied_destination ? "true" : "false")
                      << ",\"remaining\":";
            integers(range.remaining);
            std::cout << ",\"entry_costs\":[";
            for (std::size_t index = 0; index < board.cells.size(); ++index) {
                if (index) std::cout << ',';
                std::cout << g1::movement_entry_cost(board, profile,
                    static_cast<std::int32_t>(index % board.width),
                    static_cast<std::int32_t>(index / board.width));
            }
            std::cout << "]}";
        }
        std::cout << "]}\n";
        return 0;
    } catch (const std::exception& error) {
        std::cerr << "world_probe: " << error.what() << '\n';
        return 1;
    }
}
