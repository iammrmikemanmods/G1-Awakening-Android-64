#pragma once

#include "g1/Text.hpp"

#include <array>
#include <optional>

namespace g1 {

// Original configuration +0xc and +8. Groups 0/1 are the two campaigns;
// group 2 uses the five-entry table. Values come from real active world state.
struct CampaignContext {
    std::uint8_t group{};
    std::int32_t mission_index{};
    std::array<std::uint8_t, 2> active_unit_counts{};
    bool type_12_active{};
    std::array<std::uint8_t, 2> player_factions{{0U, 1U}};
    std::array<std::int8_t, 2> player_controllers{{0, 1}};
};

std::size_t campaign_mission_count(std::uint8_t group);
std::uint32_t mission_resource_for_campaign(std::uint8_t group, std::int32_t index);

struct MissionAudioRequest {
    std::int32_t operation{}; // Original 0x2ad12 operation, not a new sound ID.
    std::int32_t argument{};
};

struct CompletionPresentation {
    std::int32_t original_phase{8};
    std::optional<std::uint8_t> selected_owner;
    std::vector<MissionAudioRequest> audio_requests;
    // Exact input to the original text-resource lookup after 0x301aa's
    // index-or-tag conversion. Valid tagged localization IDs use 0x21ff....
    // No generic text request exists for an ordinary group-0 victory.
    std::optional<std::uint32_t> text_resource_id;
    bool set_config_flag_44{};
};

// Original opcode-8 branch 0x5648a..0x56600. It emits presentation requests;
// it does not establish a victory, save a profile, or advance the campaign.
// After this entry the original clock changes phase 8 to 99 on one tick.
CompletionPresentation mission_completion_presentation(const CampaignContext&,
                                                      std::int16_t text_index);

// The three values passed to 0x44058. The first is map+0x274 (the human-turn
// counter); names of the other two are intentionally not guessed here.
struct MissionRecord {
    std::int32_t value_274{-1}; // Lower is better; -1 is unset.
    std::int32_t value_278{-1}; // Higher is better; -1 is unset.
    std::int32_t value_27c{-1}; // Lower is better; -1 is unset.
};

struct CampaignProfile {
    std::array<bool, 2> completed{{false, false}};
    std::array<std::int32_t, 2> highest_unlocked{{0, 0}};
    std::array<std::vector<MissionRecord>, 2> records{{
        std::vector<MissionRecord>(17U), std::vector<MissionRecord>(8U)}};
    // Original profile config+0x0e: enabled by reset, consumed before the
    // first eligible human support explanation. Independent of mission saves.
    bool support_tutorial_pending{true};
};

// Direct signed comparisons from original 0x44058; independent of the
// victory decision. The original records best metrics even on a loss.
MissionRecord update_mission_record(const MissionRecord& previous,
                                   const MissionRecord& current) noexcept;

struct CampaignProfileUpdate {
    CampaignProfile profile;
    std::optional<MissionRecord> previous_record;
    bool finished_campaign{}; // Original map+0x3c8, cleared before this update.
};

// Original 0x4e5a6 up to its persistence call at 0x4e7a4. Mutations are
// prepared before the save attempt, just as in the original. Group 2 skips
// best-record updates, but still takes the original second unlock-field path.
// Returning this value does not mean it has been written to durable storage.
CampaignProfileUpdate prepare_campaign_profile_update(const CampaignProfile&,
    const CampaignContext&, std::int16_t next_index, const MissionRecord& current);

enum class CampaignSaveDisposition {
    persistence_failed,
    present_results,
    immediate_handoff,
};

// The bool must be an actual persistence result. Original 0x56eec..0x56f40
// opens its save-error UI on failure; no results/handoff is permitted then.
CampaignSaveDisposition campaign_save_disposition(std::uint8_t group,
                                                  bool persistence_succeeded);

struct ResultsUpdate {
    std::int32_t background_offset_q16{};
    std::uint32_t remaining_input_edges{};
    bool request_handoff{};
};

// Original results updater 0x57e54: masks 16, 64, 0x8000 consume the whole
// input edge word and request handoff. Otherwise only the background scroll
// accumulator advances. Height comes from the actual results image.
ResultsUpdate advance_mission_results(std::int32_t background_offset_q16,
    std::int32_t background_image_height, std::uint32_t input_edges,
    std::uint32_t delta_ms);

enum class CampaignHandoffKind {
    next_mission,
    campaign_menu,
    campaign_ending,
};

struct CampaignHandoff {
    CampaignHandoffKind kind{CampaignHandoffKind::campaign_menu};
    std::optional<std::int32_t> next_index;
    std::optional<std::uint32_t> next_mission_resource;
    std::optional<std::uint32_t> ending_text_resource;
    bool reset_world{}; // Original 0x52e90(map, false) before loading next script.
    bool reset_loading_stage{}; // Original app+0x78 = 0.
    std::optional<std::uint8_t> snapshot_mission_index;
};

// Decision portion of original 0x57c98. Called only after a successful profile
// save and, for groups 0/1, actual results input. The returned plan still
// requires a real world reset/next-script load or menu/ending presentation.
// This function never labels a mission as complete merely because UI was read.
CampaignHandoff campaign_handoff(const CampaignContext&, std::int16_t next_index,
                                bool finished_campaign);

} // namespace g1
