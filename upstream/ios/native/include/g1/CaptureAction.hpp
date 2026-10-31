#pragma once

#include "g1/MissionExecution.hpp"

namespace g1 {

// Native ownership data for original map+0x3e8/+0x3f8, with the separate
// derived link bytes at +0x408 and numbered counters at +0x68. Counter refresh
// is not an Energon payment; the wider economy tick is outside this module.
struct CaptureNetwork {
    std::vector<MissionSiteState> sites;
    std::vector<std::vector<std::vector<std::uint8_t>>> item_links;
    // Helper refresh baseline, not original cold state. Session startup at
    // 0x57b80 initializes this pair to zero before the first turn refresh.
    std::array<std::int32_t, 2> counters_68{{1, 1}};
};

CaptureNetwork initial_capture_network(const std::vector<WorldSite>&);

// Exact 0x4a852. Each flag controls its own writes. A normal capture passes
// false,true: it changes link bytes and preserves the current counters.
// State owners must be -1,0,1; counts must fit the original eight-bit loops.
void refresh_capture_network(CaptureNetwork&,
                             std::array<std::int8_t, 2> player_factions,
                             bool refresh_counters, bool refresh_links);

struct CaptureAvailability { bool item{}, site{}; };

// Coordinate/ownership predicates 0x4b050 and 0x4b394. These do not check
// form capability: the original command shows its explanatory message later.
CaptureAvailability capture_availability(const std::vector<WorldSite>&,
                                         const CaptureNetwork&,
                                         WorldCoordinate, std::uint8_t owner);

enum class CaptureTargetKind { item, site };
struct CaptureTarget {
    CaptureTargetKind kind{CaptureTargetKind::item};
    std::size_t site{}, group{}, item{};
};

// Original human-command scan 0x59fc4..0x5a14c: first matching item in stored
// order wins; if none exists, the last matching site wins. Selection itself
// does not filter ownership. The preceding availability check is separate.
std::optional<CaptureTarget> choose_capture_target(const std::vector<WorldSite>&,
                                                  WorldCoordinate);

struct CaptureActor {
    std::uint8_t owner{};
    WorldCoordinate position;
    bool active{true};
    std::uint8_t flag_2c{};
    std::int8_t form_field10{};
};

enum class CaptureStatus { captured, inactive, action_used, unavailable, form_cannot_capture };
struct CaptureResult {
    CaptureStatus status{CaptureStatus::unavailable};
    std::optional<CaptureTarget> target;
    bool close_command_menu{};
    bool audio_5_12{};
    bool clear_selection_flags{}; // Item branch calls original 0x4e38c.
    std::optional<std::int8_t> site_visual_faction;
    // form_cannot_capture requests original localized message id1145.
    std::optional<std::uint16_t> message_id;
};

// Caller owns current-player selection, stationary input phase and presentation
// of the returned requests. Success changes only authentic capture state,
// derived links and actor flag2c. Moving onto a target alone never calls this.
// Full shape/index validation happens before writes; rejected actions preserve
// both actor and network. No mission cursor is advanced here.
CaptureResult perform_capture(const std::vector<WorldSite>&, CaptureNetwork&,
                              CaptureActor&,
                              std::array<std::int8_t, 2> player_factions,
                              std::array<std::int8_t, 2> controller_flags);

// Original5488c/548e2 target-specific ownership/link mutation. The AI passes
// its stored site/group/item indices directly; it does not run the human
// coordinate selector or human form-capability message branch again.
// Caller authorizes arrival/action and marks the acting unit's flag2c.
CaptureResult apply_capture_target(const std::vector<WorldSite>&,CaptureNetwork&,
    const CaptureTarget&,std::uint8_t owner,std::array<std::int8_t,2> player_factions,
    std::array<std::int8_t,2> controller_flags);

} // namespace g1
