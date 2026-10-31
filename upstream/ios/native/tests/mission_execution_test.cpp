#include "g1/MissionExecution.hpp"

#include <fstream>
#include <functional>
#include <iostream>
#include <sstream>

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

std::vector<std::vector<std::int64_t>> rows(const char* path) {
    std::ifstream file(path);
    if (!file) throw std::runtime_error("cannot open original-function fixture");
    std::vector<std::vector<std::int64_t>> result;
    std::string line;
    while (std::getline(file, line)) {
        if (line.empty() || line[0] == '#') continue;
        std::istringstream input(line);
        std::int64_t value{};
        std::vector<std::int64_t> row;
        while (input >> value) row.push_back(value);
        if (!input.eof()) throw std::runtime_error("bad integer in original fixture");
        result.push_back(std::move(row));
    }
    return result;
}

g1::WorldBootstrap world(std::initializer_list<std::vector<std::uint16_t>> script) {
    g1::WorldBootstrap result;
    result.mission_resource_id = 315U;
    result.stop = g1::BootstrapStop::presentation_required;
    for (std::uint8_t type = 0; type < 23U; ++type) {
        g1::UnitDefinition definition;
        definition.type = type;
        definition.raw_text_indices[0] = type == 11U ? 124U : 122U;
        result.unit_definitions.push_back(definition);
    }
    std::size_t index{}, offset{2U};
    for (const auto& words : script) {
        const auto byte_size = words.size() * 2U;
        result.commands.push_back({index++, offset, byte_size, words.at(0),
            std::vector<std::uint16_t>(words.begin() + 1, words.end())});
        offset += byte_size;
    }
    return result;
}

g1::MissionTickInput advance() {
    return {16U, g1::DialogueLayout{1, 16, 14, 3, 40, true}};
}
} // namespace

int main(int argc, char** argv) {
    try {
        if (argc != 4) throw std::runtime_error("expected dialogue, capture and clock fixture paths");
        const auto dialogue_rows = rows(argv[1]);
        for (const auto& row : dialogue_rows) {
            if (row.size() != 19U) throw std::runtime_error("bad dialogue fixture width");
            const g1::DialogueProgress initial{static_cast<std::int32_t>(row[0]),
                static_cast<std::int32_t>(row[1]), false, row[2] != 0};
            const g1::DialogueLayout layout{static_cast<std::int32_t>(row[3]),
                static_cast<std::int32_t>(row[4]), static_cast<std::int32_t>(row[5]),
                static_cast<std::int32_t>(row[6]), static_cast<std::int32_t>(row[7]), row[8] != 0};
            const auto actual = g1::advance_dialogue(initial, layout,
                static_cast<std::uint32_t>(row[9]), static_cast<std::uint32_t>(row[10]));
            check(actual.progress.first_line == row[11] && actual.progress.reveal_offset == row[12] &&
                  actual.progress.ready_for_next_command == (row[13] != 0) &&
                  actual.progress.skip_following_dialogue == (row[14] != 0) &&
                  actual.remaining_input_edges == row[15] && actual.more_pages == (row[16] != 0) &&
                  actual.skip_audio_event == (row[17] != 0) &&
                  g1::dialogue_character_budget(actual.progress) == row[18],
                  "original dialogue branch/input/reveal mismatch");
        }
        check(dialogue_rows.size() == 476U, "dialogue fixture count changed");

        const auto capture_rows = rows(argv[2]);
        for (const auto& row : capture_rows) {
            if (row.size() < 6U) throw std::runtime_error("bad capture fixture width");
            g1::CapturedSiteObjective objective{static_cast<std::int16_t>(row[0]),
                static_cast<std::int16_t>(row[1]), static_cast<std::int16_t>(row[2]),
                static_cast<std::int16_t>(row[3]), 245, 619};
            std::vector<g1::MissionSiteState> sites;
            std::size_t at = 6U;
            for (std::int64_t site = 0; site < row[5]; ++site) {
                g1::MissionSiteState state;
                state.state = static_cast<std::int8_t>(row.at(at++));
                const auto groups = row.at(at++);
                for (std::int64_t group = 0; group < groups; ++group) {
                    const auto count = row.at(at++);
                    std::vector<std::int8_t> items;
                    for (std::int64_t item = 0; item < count; ++item)
                        items.push_back(static_cast<std::int8_t>(row.at(at++)));
                    state.item_states.push_back(std::move(items));
                }
                sites.push_back(std::move(state));
            }
            if (at != row.size()) throw std::runtime_error("capture fixture has trailing fields");
            check(g1::captured_site_objective_satisfied(objective, sites) == (row[4] != 0),
                  "original capture predicate mismatch");
        }
        check(capture_rows.size() == 164U, "capture fixture count changed");

        const auto clock_rows = rows(argv[3]);
        for (const auto& row : clock_rows) {
            if (row.size() != 8U) throw std::runtime_error("bad mission clock fixture width");
            const auto phase = static_cast<std::uint16_t>(row[0]);
            auto seeded = world({{3, 11, 280}, {phase}, {3, 1, 281}});
            if (phase == 5U) seeded.commands[1].argument_words_u16.push_back(static_cast<std::uint16_t>(row[1]));
            g1::MissionExecution execution(seeded);
            (void)execution.tick(0, advance());
            (void)execution.tick(0);
            (void)execution.tick(static_cast<std::uint32_t>(row[2]));
            const auto& state = execution.state();
            check(state.original_phase == row[3] && state.remaining_delay_ms == row[4] &&
                  state.transition_value == row[5] && state.phase_flag_225 == (row[6] != 0) &&
                  state.command_index == static_cast<std::size_t>(row[7]),
                  "original mission clock phase/delay mismatch");
        }
        check(clock_rows.size() == 48U, "mission clock fixture count changed");

        auto seeded = world({{3, 11, 280}, {3, 1, 281}, {4, 5, 7, 0},
            {12, 1, 245, 0, 0, 0, 0}, {5, 1000}, {3, 11, 288}, {1}, {8, 202}, {5, 1000}, {10, 1}});
        seeded.sites.push_back({{5, 10}, -1, {{{5, 7}, {10, 7}}}});
        auto sites = g1::initial_mission_site_states(seeded);
        check(sites.size() == 1U && sites[0].state == -1 && sites[0].item_states[0] ==
              std::vector<std::int8_t>({-1, -1}), "site state was confused with coordinates");
        g1::MissionExecution execution(seeded);
        check(execution.state().dialogue->speaker_type == 11 &&
              execution.state().dialogue->heading_text_id == 0x01a0U &&
              execution.state().dialogue->text_id == 0x02d5U, "initial dialogue identity mismatch");
        rejects([&] { (void)execution.tick(16); }, "missing renderer layout silently accepted");
        auto unrevealed = advance(); unrevealed.dialogue_layout->visible_text_fully_revealed = false;
        (void)execution.tick(16, unrevealed);
        check(execution.state().gate == g1::MissionGate::dialogue &&
              execution.state().dialogue->progress.reveal_offset == 0,
              "first reveal input incorrectly advanced dialogue");
        (void)execution.tick(16, advance());
        check(execution.state().command_index == 0U && execution.state().original_phase == 99,
              "dialogue advanced cursor in the same tick");
        (void)execution.tick(16);
        check(execution.state().dialogue->heading_text_id == 0x02a7U &&
              execution.state().command_index == 1U, "next dialogue/speaker mismatch");
        (void)execution.tick(16, advance());
        const auto camera_effects = execution.tick(16);
        check(camera_effects.effects == std::vector<g1::MissionEffect>{g1::MissionEffect::close_dialogue} &&
              execution.state().camera->x == 5 && execution.state().camera->y == 7 &&
              !execution.state().camera->immediate, "camera request or dialogue close mismatch");
        rejects([&] { execution.report_camera(1U, true); }, "stale camera callback accepted");
        execution.report_camera(2U, false);
        (void)execution.tick(1000);
        check(execution.state().gate == g1::MissionGate::camera, "camera completed without renderer response");
        execution.report_camera(2U, true);
        const auto start_goal = execution.tick(16);
        check(start_goal.effects == std::vector<g1::MissionEffect>{g1::MissionEffect::enter_gameplay_mode} &&
              execution.state().gate == g1::MissionGate::captured_site_item &&
              execution.state().interaction_mode == 0U && execution.state().original_phase == 99,
              "objective did not suspend script ticks in gameplay mode");
        (void)execution.tick(999999U, advance());
        (void)execution.observe_objective(sites, {{1, 1}});
        check(execution.state().command_index == 3U &&
              execution.state().gate == g1::MissionGate::captured_site_item,
              "time or dialogue input satisfied a capture objective");
        sites[0].state = 0; sites[0].item_states[0][1] = 0;
        (void)execution.observe_objective(sites, {{1, 1}});
        check(execution.state().gate == g1::MissionGate::captured_site_item,
              "different site/item capture satisfied indexed goal");
        sites[0].item_states[0][0] = 0;
        const auto complete_goal = execution.observe_objective(sites, {{1, 1}});
        check(complete_goal.effects == std::vector<g1::MissionEffect>{
                  g1::MissionEffect::reset_current_player_actions, g1::MissionEffect::return_to_script_mode} &&
              execution.state().interaction_mode == 6U && execution.state().command_index == 3U,
              "capture success effects or cursor changed");
        (void)execution.tick(9999U); // Dispatch only: elapsed time is not charged to the new wait.
        check(execution.state().remaining_delay_ms == 1000, "delta leaked into newly dispatched delay");
        (void)execution.tick(999U);
        check(execution.state().remaining_delay_ms == 1 && execution.state().original_phase == 5,
              "delay threshold changed");
        (void)execution.tick(2U);
        check(execution.state().remaining_delay_ms == -1 && execution.state().command_index == 4U,
              "delay overrun carried into later command");
        (void)execution.tick(1U);
        (void)execution.tick(16U, advance());
        (void)execution.tick(16U);
        (void)execution.tick(0U);
        (void)execution.tick(16U);
        check(execution.state().stop == g1::MissionStop::completion_presentation_required &&
              execution.state().command_index == 7U && execution.state().pending_text_id == 0x052bU,
              "completion presentation was silently consumed");
        (void)execution.tick(100000U, advance());
        check(execution.state().command_index == 7U, "unsupported completion advanced to campaign save");
        g1::CampaignContext campaign;
        campaign.active_unit_counts = {{1U, 1U}};
        rejects([&] { (void)execution.begin_completion(6U, campaign); },
                "stale completion token accepted");
        rejects([&] { auto wrong = campaign; wrong.mission_index = 1;
            (void)execution.begin_completion(7U, wrong); }, "wrong campaign context accepted");
        const auto completion = execution.begin_completion(7U, campaign);
        check(completion.original_phase == 8 && completion.selected_owner == 0U &&
              !completion.text_resource_id && !completion.set_config_flag_44 &&
              completion.audio_requests.size() == 2U &&
              completion.audio_requests[0].operation == 1 && completion.audio_requests[0].argument == 0 &&
              completion.audio_requests[1].operation == 5 && completion.audio_requests[1].argument == 21,
              "ordinary first-campaign completion request differs from original");
        check(execution.state().gate == g1::MissionGate::completion_frame &&
              execution.state().original_phase == 8 && execution.state().command_index == 7U &&
              execution.state().stop == g1::MissionStop::none && !execution.state().pending_text_id,
              "completion entry skipped its original phase");
        rejects([&] { (void)execution.begin_completion(7U, campaign); },
                "completion requests emitted twice");
        (void)execution.tick(10000U);
        check(execution.state().gate == g1::MissionGate::advance_pending &&
              execution.state().original_phase == 99 && execution.state().command_index == 7U,
              "completion phase advanced cursor or required invented acknowledgement");
        (void)execution.tick(10000U);
        check(execution.state().command_index == 8U && execution.state().remaining_delay_ms == 1000,
              "completion elapsed time leaked into subsequent wait");
        (void)execution.tick(1000U);
        (void)execution.tick(0U);
        check(execution.state().command_index == 9U &&
              execution.state().stop == g1::MissionStop::campaign_handoff_required,
              "profile save/handoff was bypassed after completion");

        g1::MissionExecution skipped(world({{3, 11, 280}, {3, 1, 281}, {12, 1, 245, 0, 0, 0, 0}}));
        auto skip_input = advance(); skip_input.input_edges = 128U;
        const auto skip = skipped.tick(0, skip_input);
        check(skip.effects == std::vector<g1::MissionEffect>{g1::MissionEffect::skip_dialogue_audio_event},
              "skip notification missing");
        (void)skipped.tick(0);
        check(skipped.state().command_index == 1U && skipped.state().original_phase == 99,
              "skip-following dialogue behavior changed");
        (void)skipped.tick(0);
        check(!skipped.state().skip_following_dialogue && skipped.state().gate == g1::MissionGate::captured_site_item,
              "cold objective entry did not clear dialogue skip");
        (void)skipped.observe_objective(sites, {{0, 1}});
        check(skipped.state().stop == g1::MissionStop::unsupported_player_loss_branch,
              "unrecovered player-loss branch was guessed");
        rejects([&] { (void)g1::captured_site_objective_satisfied({0, 0, 2, 0, 245, 619}, sites); },
                "out-of-bounds objective index accepted");
        rejects([&] { (void)g1::captured_site_objective_satisfied({-2, 0, 0, 0, 245, 619}, sites); },
                "unknown negative objective index accepted");
        rejects([] { (void)g1::advance_dialogue({}, {1, 0, 14, 3, 40, false}, 16, 16); },
                "zero line height accepted");

        auto next_world = world({{3, 11, 280}, {12, 2, 239, 65534},
                                 {3, 1, 281}, {1}, {8, 202}, {10, 2}});
        next_world.mission_resource_id = 326U;
        auto next_campaign = campaign; next_campaign.mission_index = 1;
        g1::MissionExecution next_execution(next_world, next_campaign);
        (void)next_execution.tick(0U, advance());
        (void)next_execution.tick(0U);
        check(next_execution.state().gate == g1::MissionGate::objective &&
              next_execution.state().mission_objective->kind == g1::MissionObjectiveKind::inactive_unit &&
              !next_execution.state().objective, "generic objective entry was treated as capture");
        std::vector<g1::MissionObjectiveUnit> live_units(23U);
        live_units[11].active = true;
        live_units[19].active = true; live_units[19].owner = 1U;
        (void)next_execution.tick(100000U, advance());
        const auto still_waiting = next_execution.observe_objective({}, live_units, next_campaign, 0U);
        check(still_waiting.effects.empty() && next_execution.state().command_index == 1U &&
              next_execution.state().gate == g1::MissionGate::objective,
              "generic goal accepted time/input instead of live unit state");
        rejects([&] { auto wrong = next_campaign; wrong.mission_index = 0;
            (void)next_execution.observe_objective({}, live_units, wrong, 0U); },
            "objective callback from different campaign accepted");
        live_units[19].active = false; next_campaign.active_unit_counts[1] = 0U;
        const auto defeated = next_execution.observe_objective({}, live_units, next_campaign, 0U);
        check(defeated.effects == std::vector<g1::MissionEffect>{
                  g1::MissionEffect::reset_current_player_actions, g1::MissionEffect::return_to_script_mode} &&
              next_execution.state().original_phase == 99 && next_execution.state().command_index == 1U,
              "real inactive-unit goal failed to request script resumption");
        (void)next_execution.tick(0U);
        check(next_execution.state().command_index == 2U &&
              next_execution.state().gate == g1::MissionGate::dialogue, "generic success skipped dialogue");

        next_campaign.active_unit_counts = {{1U, 1U}};
        g1::MissionExecution lost(next_world, next_campaign);
        (void)lost.tick(0U, advance()); (void)lost.tick(0U);
        live_units[11].active = false; live_units[19].active = true;
        next_campaign.active_unit_counts[0] = 0U;
        (void)lost.observe_objective({}, live_units, next_campaign, 0U);
        check(lost.state().original_phase == 99 && lost.state().command_index == 2U,
              "loss branch did not seek last later transition");
        (void)lost.tick(0U);
        check(lost.state().command_index == 3U && lost.state().original_phase == 1 &&
              lost.state().gate == g1::MissionGate::transition, "loss branch dispatch mismatch");

        auto early_camera=world({{4,3,2,1},{6,11,1,2,0,0},{2},{3,11,280}});
        early_camera.stop=g1::BootstrapStop::execution_required;
        g1::MissionExecution early(early_camera);
        check(early.state().gate==g1::MissionGate::camera && early.state().command_index==0 &&
              early.take_initial_effects().empty(),"general empty-roster camera boundary rejected");
        (void)early.tick(1000000,advance());
        check(early.state().gate==g1::MissionGate::camera,"early camera completed from time/input");
        early.report_camera(0,true);(void)early.tick(0);
        check(early.state().gate==g1::MissionGate::world_command &&
              early.pending_world_command().opcode==6,"later initial spawn was skipped");
        (void)early.tick(1000000,advance());
        check(early.state().command_index==1 && early.state().gate==g1::MissionGate::world_command,
              "unapplied world command advanced from time/input");
        rejects([&]{early.report_world_command_applied(0);},"stale world token accepted");
        early.report_world_command_applied(1);
        rejects([&]{early.report_world_command_applied(1);},"world command acknowledged twice");
        rejects([&]{(void)early.pending_world_command();},"applied world command remained pending");
        (void)early.tick(0);
        check(early.state().gate==g1::MissionGate::transition && early.state().original_phase==2,
              "world command bypassed subsequent transition");

        for (const auto opcode:{0U,6U,7U,9U,13U,14U,15U,16U,17U,18U,19U,20U,21U}) {
            auto entry=world({{static_cast<std::uint16_t>(opcode)},{3,11,280}});
            auto& args=entry.commands[0].argument_words_u16;
            if(opcode==6U)args={11,1,2,0,0};
            else if(opcode==9U || opcode==18U || opcode==19U || opcode==20U)args={0,0};
            else if(opcode==7U || opcode==15U || opcode==17U || opcode==21U)args={0};
            else if(opcode==16U)args={208};
            entry.commands[0].byte_size=2U+args.size()*2U;
            entry.stop=g1::BootstrapStop::execution_required;
            g1::MissionExecution pending(entry);
            check(pending.state().gate==g1::MissionGate::world_command,
                  "known world opcode has no application gate");
            pending.report_world_command_applied(0);
            const bool extra_frame=opcode==0U || opcode==13U || opcode==14U || opcode==16U ||
                opcode==18U || opcode==19U || opcode==20U;
            check(pending.state().gate==(extra_frame?g1::MissionGate::world_frame:g1::MissionGate::advance_pending),
                  "world command lost original phase timing");
            if(extra_frame) {
                check(pending.state().original_phase==static_cast<std::int32_t>(opcode),
                      "world command phase changed before its original next tick");
                (void)pending.tick(1000000);
                check(pending.state().original_phase==99 && pending.state().command_index==0,
                      "world phase tick advanced cursor immediately");
            }
            (void)pending.tick(0);
            check(pending.state().gate==g1::MissionGate::dialogue && pending.state().command_index==1,
                  "world gate did not resume original next-command dispatch");
        }
        auto objective_entry=world({{12,2,239,65534},{1}});
        objective_entry.stop=g1::BootstrapStop::execution_required;
        g1::MissionExecution initial_goal(objective_entry);
        check(initial_goal.state().gate==g1::MissionGate::objective &&
              initial_goal.take_initial_effects()==std::vector<g1::MissionEffect>{g1::MissionEffect::enter_gameplay_mode} &&
              initial_goal.take_initial_effects().empty(),"initial objective effects dropped or duplicated");
        auto wrong_boundary=early_camera;wrong_boundary.stop=g1::BootstrapStop::presentation_required;
        rejects([&]{g1::MissionExecution invalid(wrong_boundary);},"legacy dialogue boundary weakened");
        std::cout << "mission execution: " << checks << " checks; " << dialogue_rows.size()
                  << " original dialogue, " << capture_rows.size() << " capture, "
                  << clock_rows.size() << " clock cases\n";
        return 0;
    } catch (const std::exception& error) {
        std::cerr << "mission execution test failed: " << error.what() << '\n';
        return 1;
    }
}
