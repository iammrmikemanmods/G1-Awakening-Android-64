#include "g1/AIIncome.hpp"

namespace g1 {
std::int32_t ai_capture_income_potential(const CaptureNetwork& network,
    std::array<std::int8_t,2> factions, std::uint8_t owner,
    const CaptureTarget& target, std::int32_t own_before, std::int32_t other_before) {
    if (owner>1 || target.site>=network.sites.size())
        throw FormatError("AI income potential has an invalid owner/site");
    auto hypothetical=network;
    auto& site=hypothetical.sites[target.site];
    if (target.kind==CaptureTargetKind::site) site.state=static_cast<std::int8_t>(owner);
    else if (target.kind==CaptureTargetKind::item) {
        if (target.group>=site.item_states.size() || target.item>=site.item_states[target.group].size())
            throw FormatError("AI income potential has an invalid item index");
        site.item_states[target.group][target.item]=static_cast<std::int8_t>(owner);
    } else throw FormatError("AI income potential has an invalid target kind");
    refresh_capture_network(hypothetical,factions,true,false);
    const auto own=static_cast<std::uint32_t>(hypothetical.counters_68[owner])-
        static_cast<std::uint32_t>(own_before);
    const auto other=static_cast<std::uint32_t>(hypothetical.counters_68[owner^1U])-
        static_cast<std::uint32_t>(other_before);
    const auto value=own-other;
    return value<=0x7fffffffU ? static_cast<std::int32_t>(value) :
        static_cast<std::int32_t>(static_cast<std::int64_t>(value)-0x100000000LL);
}
} // namespace g1
