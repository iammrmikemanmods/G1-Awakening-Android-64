#include "g1/MissionExecution.hpp"

#include <iostream>

namespace {
void hex_bytes(const std::string& bytes) {
    constexpr char digits[] = "0123456789abcdef";
    std::cout << '"';
    for (const auto character : bytes) {
        const auto value = static_cast<unsigned char>(character);
        std::cout << digits[value >> 4U] << digits[value & 15U];
    }
    std::cout << '"';
}

const char* gate_name(g1::MissionGate gate) {
    switch (gate) {
    case g1::MissionGate::dialogue: return "dialogue";
    case g1::MissionGate::camera: return "camera";
    case g1::MissionGate::delay: return "delay";
    case g1::MissionGate::transition: return "transition";
    case g1::MissionGate::captured_site_item: return "captured_site_item";
    case g1::MissionGate::objective: return "objective";
    case g1::MissionGate::completion_frame: return "completion_frame";
    case g1::MissionGate::world_command: return "world_command";
    case g1::MissionGate::world_frame: return "world_frame";
    case g1::MissionGate::advance_pending: return "advance_pending";
    case g1::MissionGate::stopped: return "stopped";
    case g1::MissionGate::end_of_stream: return "end_of_stream";
    }
    throw g1::FormatError("invalid mission gate");
}
} // namespace

int main(int argc, char** argv) {
    if (argc != 2) {
        std::cerr << "usage: mission_execution_probe /local/path/iphone.big\n";
        return 2;
    }
    try {
        const g1::Archive archive(g1::read_file(argv[1]));
        const auto texts = g1::load_text_table(archive);
        const auto world = g1::bootstrap_first_campaign(archive);
        g1::MissionExecution execution(world);
        auto sites = g1::initial_mission_site_states(world);
        std::array<std::uint8_t, 2> active_counts{};
        for (const auto& unit : world.units) {
            if (unit.owner >= active_counts.size()) throw g1::FormatError("probe unit owner outside two-player context");
            if (unit.active) ++active_counts[unit.owner];
        }
        std::cout << "{\"control_flow_reference_only\":true,\"not_a_playthrough\":true,"
                     "\"renderer_and_capture_observations_are_synthetic\":true,\"effects_applied\":false,"
                     "\"text_entries\":[";
        for (std::size_t n = 0; n < texts.entries().size(); ++n) {
            if (n) std::cout << ',';
            const auto& entry = texts.entries()[n];
            std::cout << "{\"id\":" << entry.id << ",\"offset\":" << entry.source_offset << ",\"utf8_hex\":";
            hex_bytes(entry.utf8);
            std::cout << '}';
        }
        std::cout << "],\"mission_resource_id\":" << world.mission_resource_id
                  << ",\"mission_command_count\":" << world.commands.size()
                  << ",\"command_entries\":[";
        std::size_t previous = world.commands.size();
        bool comma{}, observed_goal{};
        for (std::size_t iteration = 0; iteration < 200U; ++iteration) {
            const auto& state = execution.state();
            if (state.command_index != previous) {
                if (comma) std::cout << ',';
                comma = true;
                previous = state.command_index;
                std::cout << "{\"index\":" << state.command_index << ",\"opcode\":" << state.opcode
                          << ",\"phase\":" << state.original_phase << ",\"mode\":" << unsigned(state.interaction_mode)
                          << ",\"gate\":\"" << gate_name(state.gate) << '"';
                if (state.dialogue) {
                    const auto& dialogue = *state.dialogue;
                    (void)texts.at(dialogue.text_id);
                    (void)texts.at(dialogue.heading_text_id);
                    std::cout << ",\"speaker_type\":" << dialogue.speaker_type
                              << ",\"heading_text_id\":" << dialogue.heading_text_id
                              << ",\"text_index\":" << dialogue.text_index
                              << ",\"text_id\":" << dialogue.text_id;
                }
                if (state.camera)
                    std::cout << ",\"camera\":[" << state.camera->x << ',' << state.camera->y << ','
                              << (state.camera->immediate ? 1 : 0) << ']';
                if (state.objective) {
                    const auto& objective = *state.objective;
                    std::cout << ",\"objective_indices\":[" << objective.site << ',' << objective.group << ','
                              << objective.item << ',' << objective.desired_state << ']'
                              << ",\"text_id\":" << objective.text_id;
                }
                if (state.pending_text_id) std::cout << ",\"pending_text_id\":" << *state.pending_text_id;
                std::cout << '}';
            }
            if (state.gate == g1::MissionGate::stopped) break;
            if (state.gate == g1::MissionGate::dialogue) {
                // This is a control-flow fixture, not a font/layout or UI test.
                (void)execution.tick(16U, {16U, g1::DialogueLayout{1, 16, 14, 3, 40, true}});
            } else if (state.gate == g1::MissionGate::camera) {
                execution.report_camera(state.command_index, true);
            } else if (state.gate == g1::MissionGate::captured_site_item) {
                if (observed_goal || !state.objective) throw g1::FormatError("unexpected additional goal in first mission");
                if (g1::captured_site_objective_satisfied(*state.objective, sites))
                    throw g1::FormatError("initial first-mission capture state unexpectedly satisfies goal");
                const auto objective = *state.objective;
                if (objective.site != 0 || objective.group != 0 || objective.item != 0 || objective.desired_state != 0)
                    throw g1::FormatError("first-mission objective differs from reference boundary");
                const auto point = world.sites.at(0).coordinate_groups.at(0).at(0);
                if (point.x != 5U || point.y != 7U) throw g1::FormatError("first capture target coordinate changed");
                // A deliberately supplied DEVELOPMENT REFERENCE observation.
                // No capture action has been executed, and no effect is applied.
                sites.at(0).item_states.at(0).at(0) = 0;
                (void)execution.observe_objective(sites, active_counts);
                observed_goal = true;
            } else {
                (void)execution.tick(1000U);
            }
        }
        if (execution.state().stop != g1::MissionStop::completion_presentation_required ||
            execution.state().command_index != 26U || !observed_goal)
            throw g1::FormatError("first-script control-flow fixture did not reach its explicit boundary");
        std::cout << "],\"capture_target\":[5,7],\"initial_capture_satisfied\":false,"
                     "\"synthetic_capture_observation_used\":true,\"stop_index\":26,"
                     "\"stop\":\"completion_presentation_required\",\"opcode_8_consumed\":false}\n";
        return 0;
    } catch (const std::exception& error) {
        std::cerr << "mission_execution_probe: " << error.what() << '\n';
        return 1;
    }
}
