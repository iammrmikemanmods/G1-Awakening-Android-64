#include "g1/MissionObjectives.hpp"

#include <fstream>
#include <iostream>
#include <sstream>

namespace {
std::size_t checks{};
void require(bool value, const char* message) {
    ++checks;
    if (!value) throw std::runtime_error(message);
}
template<class Function> void rejects(Function function) {
    bool rejected{};
    try { function(); } catch (const g1::FormatError&) { rejected = true; }
    require(rejected, "invalid objective input was accepted");
}
struct Row {
    std::vector<std::int64_t> values;
    std::size_t position{};
    std::int64_t take() {
        if (position >= values.size()) throw std::runtime_error("short objective reference row");
        return values[position++];
    }
};
std::vector<g1::MissionCommand> commands(unsigned shape) {
    std::vector<g1::MissionCommand> result;
    for (std::size_t i = 0; i < 40U; ++i) result.push_back({i, 2U + 2U * i, 2U, 5U, {}});
    result[10].opcode = 12U;
    if (shape == 0U) result[30].opcode = result[36].opcode = 1U;
    if (shape == 1U) result[20].opcode = 1U;
    return result;
}
} // namespace

int main(int argc, char** argv) try {
    if (argc != 2) throw std::runtime_error("expected original objective fixture path");
    std::ifstream file(argv[1]);
    if (!file) throw std::runtime_error("cannot open original objective fixture");
    std::string line;
    std::size_t cases{};
    std::array<std::size_t, 4> kinds{};
    while (std::getline(file, line)) {
        if (line.empty() || line.front() == '#') continue;
        Row row;
        std::istringstream input(line);
        std::int64_t value{};
        while (input >> value) row.values.push_back(value);
        g1::CampaignContext context;
        context.group = static_cast<std::uint8_t>(row.take());
        context.mission_index = static_cast<std::int32_t>(row.take());
        for (auto& count : context.active_unit_counts) count = static_cast<std::uint8_t>(row.take());
        context.type_12_active = row.take() != 0;
        for (auto& faction : context.player_factions) faction = static_cast<std::uint8_t>(row.take());
        for (auto& controller : context.player_controllers) controller = static_cast<std::int8_t>(row.take());
        const auto owner = static_cast<std::uint8_t>(row.take());
        const auto script = commands(static_cast<unsigned>(row.take()));
        g1::MissionObjective objective;
        objective.kind = static_cast<g1::MissionObjectiveKind>(row.take());
        objective.parameter_count = static_cast<std::uint8_t>(row.take());
        for (std::size_t p = 0; p < objective.parameter_count; ++p)
            objective.parameters.at(p) = static_cast<std::int16_t>(row.take());
        std::vector<g1::MissionObjectiveUnit> units(static_cast<std::size_t>(row.take()));
        for (auto& unit : units) {
            unit.present = row.take() != 0;
            unit.active = row.take() != 0;
            unit.owner = static_cast<std::uint8_t>(row.take());
            unit.x = static_cast<std::uint8_t>(row.take());
            unit.y = static_cast<std::uint8_t>(row.take());
            unit.offset_x = static_cast<std::int32_t>(row.take());
            unit.offset_y = static_cast<std::int32_t>(row.take());
        }
        std::vector<g1::MissionSiteState> sites(static_cast<std::size_t>(row.take()));
        for (auto& site : sites) {
            site.state = static_cast<std::int8_t>(row.take());
            site.item_states.resize(static_cast<std::size_t>(row.take()));
            for (auto& group : site.item_states) {
                group.resize(static_cast<std::size_t>(row.take()));
                for (auto& item : group) item = static_cast<std::int8_t>(row.take());
            }
        }
        const auto result = g1::evaluate_mission_objective(objective,
            {context, units, sites, script, 10U, owner});
        std::size_t reset_count{};
        if (result.reset_current_player_actions)
            for (const auto& unit : units) if (unit.present && unit.owner == owner) ++reset_count;
        const std::array<std::int64_t, 7> actual{{result.resume_script ? 1 : 0,
            result.command_cursor, result.set_alternate_dialogue_variant ? 1 : 0,
            result.resume_script ? 99 : 777, result.resume_script ? 6 : 0,
            result.resume_script ? 0 : 1, static_cast<std::int64_t>(reset_count)}};
        require(row.position + actual.size() == row.values.size(), "objective reference shape");
        for (const auto item : actual) {
            const auto expected = row.take();
            if (item != expected) {
                std::cerr << "case " << cases << " kind " << static_cast<unsigned>(objective.kind)
                          << " column " << row.position - 1U << " native=" << item
                          << " original=" << expected << '\n';
                throw std::runtime_error("native objective differs from original");
            }
            ++checks;
        }
        ++cases;
        ++kinds.at(static_cast<std::size_t>(objective.kind));
    }
    require(cases == 400U && kinds == std::array<std::size_t, 4>{{80U, 80U, 160U, 80U}},
            "incomplete original objective corpus");
    for (std::uint16_t kind = 0U; kind < 4U; ++kind) {
        const std::array<std::size_t, 4> counts{{2U, 4U, 1U, 3U}};
        g1::MissionCommand command{0U, 2U, 0U, 12U, {kind, 245U}};
        command.argument_words_u16.insert(command.argument_words_u16.end(), counts[kind], 0xffffU);
        const auto decoded = g1::decode_mission_objective(command);
        require(decoded.kind == static_cast<g1::MissionObjectiveKind>(kind) &&
                decoded.text_id == 0x026bU && decoded.parameter_count == counts[kind] &&
                decoded.parameters[0] == -1, "objective argument decode/sign extension");
        command.argument_words_u16.pop_back();
        rejects([&] { static_cast<void>(g1::decode_mission_objective(command)); });
    }
    rejects([] { static_cast<void>(g1::decode_mission_objective({0U, 2U, 0U, 12U, {4U, 245U}})); });
    g1::CampaignContext context;
    context.active_unit_counts = {{2U, 1U}};
    auto script = commands(0U);
    std::vector<g1::MissionObjectiveUnit> units(23U);
    std::vector<g1::MissionSiteState> sites;
    g1::MissionObjective objective{g1::MissionObjectiveKind::unit_at_position, 245, 0x026bU,
                                   {{11, 5, 7, 0}}, 3U};
    units[11].x = 5U; units[11].y = 7U;
    require(g1::evaluate_mission_objective(objective, {context, units, sites, script, 10U, 0U}).resume_script,
            "original named-position branch incorrectly added active check");
    objective.parameters[0] = -3;
    require(!g1::evaluate_mission_objective(objective, {context, units, sites, script, 10U, 0U}).resume_script,
            "wildcard-position branch omitted active check");
    rejects([&] { auto malformed = objective; malformed.parameters[0] = -2;
        static_cast<void>(g1::evaluate_mission_objective(malformed, {context, units, sites, script, 10U, 0U})); });
    rejects([&] { auto malformed = objective; malformed.parameters[0] = 99;
        static_cast<void>(g1::evaluate_mission_objective(malformed, {context, units, sites, script, 10U, 0U})); });
    rejects([&] { auto malformed = objective; malformed.parameter_count = 2U;
        static_cast<void>(g1::evaluate_mission_objective(malformed, {context, units, sites, script, 10U, 0U})); });
    rejects([&] { static_cast<void>(g1::evaluate_mission_objective(objective,
        {context, units, sites, script, 40U, 0U})); });
    rejects([&] { static_cast<void>(g1::evaluate_mission_objective(objective,
        {context, units, sites, script, 10U, 2U})); });
    std::cout << "{\"all_passed\":true,\"original_cases\":" << cases
              << ",\"native_checks\":" << checks << "}\n";
} catch (const std::exception& error) {
    std::cerr << error.what() << '\n';
    return 1;
}
