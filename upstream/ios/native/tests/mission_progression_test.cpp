#include "g1/MissionProgression.hpp"

#include <fstream>
#include <iostream>
#include <sstream>

namespace {

using Number = std::int64_t;
std::size_t checks{};

void require(bool value, const char* detail) {
    ++checks;
    if (!value) throw std::runtime_error(detail);
}

struct Row {
    std::vector<Number> values;
    std::size_t position{};
    Number take() {
        if (position >= values.size()) throw std::runtime_error("short original reference row");
        return values[position++];
    }
    void compare(const std::vector<Number>& actual) {
        require(position + actual.size() == values.size(), "reference output shape");
        for (const auto value : actual) {
            const auto expected = take();
            if (value != expected) {
                std::cerr << "column " << position - 1U << ": native=" << value
                          << " original=" << expected << '\n';
                throw std::runtime_error("native/original mission progression mismatch");
            }
            ++checks;
        }
    }
};

g1::CampaignContext context(Row& row) {
    g1::CampaignContext result;
    result.group = static_cast<std::uint8_t>(row.take());
    result.mission_index = static_cast<std::int32_t>(row.take());
    for (auto& count : result.active_unit_counts) count = static_cast<std::uint8_t>(row.take());
    result.type_12_active = row.take() != 0;
    for (auto& faction : result.player_factions) faction = static_cast<std::uint8_t>(row.take());
    for (auto& controller : result.player_controllers) controller = static_cast<std::int8_t>(row.take());
    return result;
}

g1::MissionRecord record(Row& row) {
    g1::MissionRecord result;
    result.value_274 = static_cast<std::int32_t>(row.take());
    result.value_278 = static_cast<std::int32_t>(row.take());
    result.value_27c = static_cast<std::int32_t>(row.take());
    return result;
}

void compare_record(const g1::MissionRecord& a, const g1::MissionRecord& b) {
    require(a.value_274 == b.value_274 && a.value_278 == b.value_278 &&
            a.value_27c == b.value_27c, "untouched profile record changed");
}

template<class Function> void rejects(Function function) {
    bool rejected = false;
    try { function(); } catch (const g1::FormatError&) { rejected = true; }
    require(rejected, "invalid progression input was accepted");
}

} // namespace

int main(int argc, char** argv) try {
    if (argc != 2) throw std::runtime_error("usage: mission_progression_test ORIGINAL_VECTORS");
    std::ifstream fixture(argv[1]);
    if (!fixture) throw std::runtime_error("cannot open original mission progression fixture");
    std::string line;
    std::array<std::size_t, 6> counts{};
    std::size_t line_number{};
    while (std::getline(fixture, line)) {
        ++line_number;
        if (line.empty() || line.front() == '#') continue;
        Row row;
        std::istringstream input(line);
        Number value{};
        while (input >> value) row.values.push_back(value);
        const auto kind = static_cast<std::size_t>(row.take());
        if (kind >= counts.size()) throw std::runtime_error("unknown original reference row");
        ++counts[kind];
        try {
            if (kind == 0U) {
                const auto c = context(row);
                const auto text = static_cast<std::int16_t>(row.take());
                const auto result = g1::mission_completion_presentation(c, text);
                std::vector<Number> actual{result.original_phase,
                    result.selected_owner ? static_cast<Number>(*result.selected_owner) : -1,
                    static_cast<Number>(result.audio_requests.size())};
                if (result.audio_requests.empty()) actual.insert(actual.end(), 4U, -1);
                else for (const auto& request : result.audio_requests) {
                    actual.push_back(request.operation); actual.push_back(request.argument);
                }
                actual.push_back(result.text_resource_id ? static_cast<Number>(*result.text_resource_id) : -1);
                actual.push_back(result.set_config_flag_44 ? 1 : 0);
                row.compare(actual);
            } else if (kind == 1U) {
                const auto c = context(row);
                const auto next = static_cast<std::int16_t>(row.take());
                g1::CampaignProfile profile;
                for (auto& highest : profile.highest_unlocked) highest = static_cast<std::int32_t>(row.take());
                for (auto& flag : profile.completed) flag = row.take() != 0;
                const auto old = record(row);
                const auto current = record(row);
                const auto persisted = row.take();
                for (auto& group : profile.records)
                    for (auto& item : group) item = {-77, -77, -77};
                if (c.group < 2U) profile.records[c.group][static_cast<std::size_t>(c.mission_index)] = old;
                const auto result = g1::prepare_campaign_profile_update(profile, c, next, current);
                const auto updated = c.group < 2U ?
                    result.profile.records[c.group][static_cast<std::size_t>(c.mission_index)] :
                    g1::MissionRecord{-77, -77, -77};
                const auto previous = result.previous_record.value_or(g1::MissionRecord{7777, 7778, 7779});
                row.compare({result.profile.highest_unlocked[0], result.profile.highest_unlocked[1],
                    result.profile.completed[0] ? 1 : 0, result.profile.completed[1] ? 1 : 0,
                    result.finished_campaign ? 1 : 0, updated.value_274, updated.value_278, updated.value_27c,
                    previous.value_274, previous.value_278, previous.value_27c, persisted});
                require(result.previous_record.has_value() == (c.group < 2U), "previous-record presence");
                for (std::size_t group = 0; group < 2U; ++group)
                    for (std::size_t i = 0; i < profile.records[group].size(); ++i)
                        if (group != c.group || i != static_cast<std::size_t>(c.mission_index))
                            compare_record(result.profile.records[group][i], profile.records[group][i]);
            } else if (kind == 2U) {
                const auto c = context(row);
                const auto next = static_cast<std::int16_t>(row.take());
                const bool finished = row.take() != 0;
                const auto result = g1::campaign_handoff(c, next, finished);
                const bool load = result.kind == g1::CampaignHandoffKind::next_mission;
                const bool ending = result.kind == g1::CampaignHandoffKind::campaign_ending;
                row.compare({ending ? 8 : 6, result.next_index.value_or(c.mission_index),
                    result.snapshot_mission_index.value_or(250U), result.reset_loading_stage ? 0 : 456,
                    load ? -2 : result.ending_text_resource ? static_cast<Number>(*result.ending_text_resource) : -1,
                    load ? 1 : 0, load ? c.group : -1, load ? next : -1});
                require(result.reset_world == load, "world reset only precedes a real next script");
                require(result.next_mission_resource.has_value() == load, "script resource presence");
                if (load) require(*result.next_mission_resource ==
                    g1::mission_resource_for_campaign(c.group, next), "handoff script table");
            } else if (kind == 3U) {
                const auto group = static_cast<std::uint8_t>(row.take());
                const bool persisted = row.take() != 0;
                const auto disposition = g1::campaign_save_disposition(group, persisted);
                const bool failed = disposition == g1::CampaignSaveDisposition::persistence_failed;
                const bool results = disposition == g1::CampaignSaveDisposition::present_results;
                const bool handoff = disposition == g1::CampaignSaveDisposition::immediate_handoff;
                row.compare({results ? 7 : 6, failed ? 1 : 0, failed ? 0 : 123,
                             results ? 0 : 456, handoff ? 1 : 0, failed ? 1 : 0, results ? 2 : 0});
            } else if (kind == 4U) {
                const auto offset = static_cast<std::int32_t>(row.take());
                const auto height = static_cast<std::int32_t>(row.take());
                const auto edges = static_cast<std::uint32_t>(row.take());
                const auto delta = static_cast<std::uint32_t>(row.take());
                const auto result = g1::advance_mission_results(offset, height, edges, delta);
                row.compare({result.background_offset_q16, result.remaining_input_edges,
                             result.request_handoff ? 1 : 0});
            } else {
                const auto group = static_cast<std::uint8_t>(row.take());
                const auto index = static_cast<std::int32_t>(row.take());
                row.compare({g1::mission_resource_for_campaign(group, index)});
            }
        } catch (...) {
            std::cerr << "reference line " << line_number << ": " << line << '\n';
            throw;
        }
    }
    require(counts == std::array<std::size_t, 6>{{144U, 144U, 288U, 6U, 126U, 30U}},
            "incomplete original reference corpus");
    g1::CampaignContext valid;
    valid.active_unit_counts = {{1U, 1U}};
    rejects([] { static_cast<void>(g1::campaign_mission_count(3U)); });
    rejects([] { static_cast<void>(g1::mission_resource_for_campaign(0U, -1)); });
    rejects([] { static_cast<void>(g1::mission_resource_for_campaign(0U, 17)); });
    rejects([&] { static_cast<void>(g1::mission_completion_presentation(valid, -1)); });
    rejects([&] { static_cast<void>(g1::mission_completion_presentation(valid, 486)); });
    rejects([&] { auto c = valid; c.mission_index = 17;
        static_cast<void>(g1::campaign_handoff(c, 1, false)); });
    rejects([&] { static_cast<void>(g1::campaign_handoff(valid, 17, false)); });
    rejects([&] { g1::CampaignProfile p; p.records[1].clear();
        static_cast<void>(g1::prepare_campaign_profile_update(p, valid, 1, {})); });
    rejects([&] { static_cast<void>(g1::prepare_campaign_profile_update({}, valid, 17, {})); });
    rejects([] { static_cast<void>(g1::campaign_save_disposition(3U, true)); });
    rejects([] { static_cast<void>(g1::advance_mission_results(0, 0, 0, 30)); });
    rejects([] { static_cast<void>(g1::advance_mission_results(0, 32768, 0, 30)); });
    std::cout << "{\"all_passed\":true,\"original_reference_rows\":738,\"native_checks\":"
              << checks << "}\n";
} catch (const std::exception& error) {
    std::cerr << error.what() << '\n';
    return 1;
}
