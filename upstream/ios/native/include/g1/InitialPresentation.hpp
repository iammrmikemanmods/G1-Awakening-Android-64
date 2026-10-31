#pragma once

#include "g1/Scene.hpp"

namespace g1 {
using UnitAnimationTable = std::array<std::array<std::uint8_t,9>,23>;
UnitAnimationTable decode_unit_animation_table(const Bytes&);

struct InitialPresentation {
    std::array<float,16> model;
    std::size_t animation;
    std::int32_t repeats{-1};
    bool reverse{};
};

// The bootstrap's original spawn sets form0, facing down1, zero position offsets.
// Resource309 maps [type*9 + form*4 + facing] to descriptor animation identity.
// This handles the proven initial two units, before movement/actions/dialogue.
// Initial site-dependent poses outside that prefix fail explicitly.
std::vector<InitialPresentation> initial_presentations(const Archive&, const FirstMissionScene&);
} // namespace g1
