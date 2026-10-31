#pragma once

#include "g1/GameSession.hpp"
#include "g1/SaveStore.hpp"
#include "g1/WorldCommands.hpp"

namespace g1 {

enum class CampaignControllerGate {
    idle,
    save_failed,
    results,
    next_mission,
    campaign_menu,
    campaign_ending,
    handed_off,
};

struct CampaignResultsPresentation {
    CampaignContext campaign;
    MissionRecord current_record;
    std::optional<MissionRecord> previous_record;
    MissionRecord best_record;
    bool finished_campaign{};
    bool drawn{};
    std::int32_t background_image_height{}; // Actual rendered image/canvas extent.
    std::int32_t background_offset_q16{};
};

struct CampaignControllerState {
    CampaignControllerGate gate{CampaignControllerGate::idle};
    std::uint64_t token{}; // Process-unique, including across controller instances.
    std::size_t command_index{};
    std::uint32_t save_attempts{};
    bool profile_persisted{}; // True only after the checked real filesystem write.
    std::string save_error;
    std::optional<CampaignResultsPresentation> results;
    std::optional<CampaignHandoff> handoff;
};

struct PreparedCampaign {
    CampaignContext context;
    CampaignBootstrap bootstrap;
};

// One opcode10 save/results/handoff cycle. Construct a new controller for a
// later mission. It loads the actual native profile; a missing file starts a
// fresh profile, while an unreadable/corrupt existing file rejects. The host
// creates/selects its app-private directory. No legacy save format is inferred.
class CampaignController {
public:
    explicit CampaignController(std::string profile_destination);
    // A pending token has exactly one transaction owner. Copying or moving the
    // owner could leave another usable object accepting the same UI response.
    CampaignController(const CampaignController&) = delete;
    CampaignController& operator=(const CampaignController&) = delete;
    CampaignController(CampaignController&&) = delete;
    CampaignController& operator=(CampaignController&&) = delete;
    const CampaignControllerState& state() const noexcept { return state_; }
    // Last confirmed persisted profile (or the fresh default before first save).
    const CampaignProfile& profile() const noexcept { return profile_; }

    // Native durability policy for the recovered one-time support message.
    // May run only before a campaign handoff begins. Writes only this profile
    // flag, without changing results, unlocks, session state, RNG or tokens.
    // Failure keeps the last confirmed profile intact and permits retry.
    SaveWriteResult consume_support_tutorial();

    // Requires the real stopped opcode10 gate, reads its signed next-index word,
    // snapshots actual counts/metrics/RNG, and attempts a real atomic save.
    // Failure retains the exact prepared bytes for explicit retry. It neither
    // unlocks results nor acknowledges the mission's gameplay objectives.
    SaveWriteResult begin(const GameSession&);
    SaveWriteResult retry_save(std::uint64_t token);

    // Called after the host has actually drawn this results request. Height
    // comes from that real background/canvas. No input can hand off before it.
    void report_results_presented(std::uint64_t token,std::int32_t actual_height);
    // Exact original result input masks16/64/0x8000; all matching edges are
    // consumed together. A request is not accepted from elapsed time alone.
    ResultsUpdate update_results(std::uint64_t token,std::uint32_t input_edges,
                                 std::uint32_t delta_ms);

    // Only available after successful persistence and the required real
    // results input (group2 has the original immediate-handoff exception).
    // Preserves the captured shared generator without reseeding. A failed
    // archive/bootstrap load leaves the request retryable and unconsumed.
    // The first successful preparation is cached; later calls return a copy.
    PreparedCampaign prepare_next_campaign(std::uint64_t token,const Archive&,
                                           bool config_damage_immunity=false);
    // Call only after actual host installation. Checks the new session's
    // campaign/resource/cursor, roster, counts and exact RNG state against the
    // prepared result. Its camera/presentation gates remain independent.
    void report_next_session_installed(std::uint64_t token,const GameSession&);

    // These acknowledge only that the requested host destination was shown.
    // They do not select another campaign or invent the ending's closing flow.
    void report_menu_presented(std::uint64_t token);
    void report_ending_presented(std::uint64_t token);

private:
    std::string destination_;
    Bytes legacy_profile_bytes_;
    CampaignProfile profile_;
    CampaignControllerState state_;
    std::optional<CampaignProfileUpdate> prepared_update_;
    Bytes prepared_bytes_;
    CampaignContext source_campaign_;
    MissionRecord source_record_;
    std::int16_t next_index_{};
    std::optional<GameRandom> source_random_;
    std::optional<TerrainTables> source_terrain_tables_;
    std::optional<PreparedCampaign> prepared_next_;
    bool prepared_config_immunity_{};

    void require_gate(std::uint64_t,CampaignControllerGate) const;
    SaveWriteResult attempt_save();
    SaveWriteResult preserve_legacy_profile();
    void request_handoff();
};

} // namespace g1
