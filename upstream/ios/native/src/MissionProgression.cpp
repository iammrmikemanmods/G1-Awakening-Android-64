#include "g1/MissionProgression.hpp"

#include <algorithm>

namespace g1 {
namespace {

constexpr std::array<std::uint32_t, 17> campaign_0{{
    315U, 326U, 333U, 334U, 335U, 336U, 337U, 338U, 339U,
    316U, 317U, 318U, 319U, 320U, 321U, 322U, 323U}};
constexpr std::array<std::uint32_t, 8> campaign_1{{
    324U, 325U, 327U, 328U, 329U, 330U, 331U, 332U}};
constexpr std::array<std::uint32_t, 5> campaign_2{{310U, 311U, 312U, 313U, 314U}};

void validate_context(const CampaignContext& context) {
    if (context.mission_index < 0 ||
        static_cast<std::size_t>(context.mission_index) >= campaign_mission_count(context.group))
        throw FormatError("campaign mission index is outside the original table");
}

bool special_player_loss(const CampaignContext& context) noexcept {
    return context.group == 0U && context.mission_index == 9 &&
           context.active_unit_counts[0] == 1U && context.type_12_active;
}

bool failed_campaign_attempt(const CampaignContext& context) noexcept {
    return (context.group != 2U && context.active_unit_counts[0] == 0U) ||
           special_player_loss(context);
}

std::int32_t signed32(std::uint32_t value) noexcept {
    return value <= 0x7fffffffU ? static_cast<std::int32_t>(value) :
        static_cast<std::int32_t>(static_cast<std::int64_t>(value) - 0x100000000LL);
}

std::int32_t wrap32(std::int64_t value) noexcept {
    return signed32(static_cast<std::uint32_t>(value));
}

std::int64_t floor_div_65536(std::int64_t value) noexcept {
    return value >= 0 ? value / 65536 : -((-value + 65535) / 65536);
}

} // namespace

std::size_t campaign_mission_count(std::uint8_t group) {
    switch (group) {
    case 0: return campaign_0.size();
    case 1: return campaign_1.size();
    case 2: return campaign_2.size();
    default: throw FormatError("unsupported campaign group");
    }
}

std::uint32_t mission_resource_for_campaign(std::uint8_t group, std::int32_t index) {
    if (index < 0 || static_cast<std::size_t>(index) >= campaign_mission_count(group))
        throw FormatError("campaign mission index is outside the original table");
    const auto offset = static_cast<std::size_t>(index);
    if (group == 0U) return campaign_0[offset];
    if (group == 1U) return campaign_1[offset];
    return campaign_2[offset];
}

CompletionPresentation mission_completion_presentation(const CampaignContext& context,
                                                      std::int16_t text_index) {
    validate_context(context);
    CompletionPresentation result;
    const auto mapped = 0x21ff0000U | script_text_id(text_index);
    std::uint32_t text_value = static_cast<std::uint32_t>(static_cast<std::int32_t>(text_index));
    if (mapped == 0x21ff052bU) {
        const std::uint8_t owner = context.active_unit_counts[0] == 0U ||
                                  special_player_loss(context) ? 1U : 0U;
        result.selected_owner = owner;
        const auto faction = context.player_factions[owner];
        if (context.group == 2U) text_value = faction != 0U ? 0x21ff0437U : 0x21ff029aU;
        else if (faction == 1U) text_value = 0x21ff0332U;
        result.audio_requests.push_back({1, 0});
        result.audio_requests.push_back({5, context.player_controllers[owner] == 0 ? 21 : 22});
    }
    // The mapper returns 0xffffffff for tagged inputs. The original calls it
    // again only to suppress the ordinary MISSION COMPLETE popup; 0x301aa
    // then maps values <=392 and otherwise passes its input through directly.
    const bool suppress_text = text_value <= 485U &&
        (0x21ff0000U | script_text_id(static_cast<std::int32_t>(text_value))) == 0x21ff052bU;
    if (!suppress_text) {
        result.text_resource_id = text_value <= 392U ?
            0x21ff0000U | script_text_id(static_cast<std::int32_t>(text_value)) : text_value;
    }
    result.set_config_flag_44 = text_value == 0x21ff0332U ||
                               text_value == 0x21ff029aU || text_value == 0x21ff0437U;
    return result;
}

MissionRecord update_mission_record(const MissionRecord& previous,
                                   const MissionRecord& current) noexcept {
    MissionRecord result = previous;
    if (result.value_274 == -1 || result.value_274 > current.value_274)
        result.value_274 = current.value_274;
    if (result.value_278 == -1 || result.value_278 < current.value_278)
        result.value_278 = current.value_278;
    if (result.value_27c == -1 || result.value_27c > current.value_27c)
        result.value_27c = current.value_27c;
    return result;
}

CampaignProfileUpdate prepare_campaign_profile_update(const CampaignProfile& previous,
    const CampaignContext& context, std::int16_t next_index, const MissionRecord& current) {
    validate_context(context);
    if (previous.records[0].size() != campaign_0.size() ||
        previous.records[1].size() != campaign_1.size())
        throw FormatError("campaign profile record tables have an invalid shape");
    if (next_index >= 0)
        static_cast<void>(mission_resource_for_campaign(context.group, next_index));
    CampaignProfileUpdate result;
    result.profile = previous;
    if (context.group != 2U) {
        const auto index = static_cast<std::size_t>(context.mission_index);
        auto& record = result.profile.records[context.group][index];
        result.previous_record = record;
        record = update_mission_record(record, current);
    }
    if (failed_campaign_attempt(context)) return result;

    const auto unlock_group = context.group == 0U ? 0U : 1U;
    auto& highest = result.profile.highest_unlocked[unlock_group];
    highest = std::max(highest, static_cast<std::int32_t>(next_index));
    if (next_index < 0 && context.group < 2U) {
        result.profile.completed[context.group] = true;
        highest = static_cast<std::int32_t>(campaign_mission_count(context.group));
        result.finished_campaign = true;
    }
    return result;
}

CampaignSaveDisposition campaign_save_disposition(std::uint8_t group,
                                                  bool persistence_succeeded) {
    static_cast<void>(campaign_mission_count(group));
    if (!persistence_succeeded) return CampaignSaveDisposition::persistence_failed;
    return group == 2U ? CampaignSaveDisposition::immediate_handoff :
                         CampaignSaveDisposition::present_results;
}

ResultsUpdate advance_mission_results(std::int32_t background_offset_q16,
    std::int32_t background_image_height, std::uint32_t edges, std::uint32_t delta_ms) {
    if (background_image_height <= 0 || background_image_height > 32767)
        throw FormatError("results image height cannot form a positive Q16.16 period");
    ResultsUpdate result{background_offset_q16, edges, false};
    if ((edges & (16U | 64U | 0x8000U)) != 0U) {
        result.remaining_input_edges = 0U;
        result.request_handoff = true;
        return result;
    }
    const auto delta = signed32(delta_ms << 16U);
    const auto increment = floor_div_65536(static_cast<std::int64_t>(delta) * 1024);
    auto offset = wrap32(static_cast<std::int64_t>(background_offset_q16) + increment);
    const auto period = background_image_height * 65536;
    if (offset >= period) offset %= period;
    result.background_offset_q16 = offset;
    return result;
}

CampaignHandoff campaign_handoff(const CampaignContext& context, std::int16_t next_index,
                                bool finished_campaign) {
    validate_context(context);
    CampaignHandoff result;
    if (failed_campaign_attempt(context)) return result;
    if (next_index < 0) {
        if (context.group < 2U && finished_campaign) {
            result.kind = CampaignHandoffKind::campaign_ending;
            result.ending_text_resource = context.group == 0U ? 0x21ff0390U : 0x21ff018dU;
        }
        return result;
    }
    result.kind = CampaignHandoffKind::next_mission;
    result.next_index = next_index;
    result.next_mission_resource = mission_resource_for_campaign(context.group, next_index);
    result.reset_world = true;
    result.reset_loading_stage = true;
    result.snapshot_mission_index = static_cast<std::uint8_t>(next_index);
    return result;
}

} // namespace g1
