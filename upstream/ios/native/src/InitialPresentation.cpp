#include "g1/InitialPresentation.hpp"
#include "g1/SpritePlacement.hpp"

#include <algorithm>

namespace g1 {
UnitAnimationTable decode_unit_animation_table(const Bytes& bytes) {
    // Original resource309 is a counted BYTE vector (207 bytes), with the
    // type/form/facing accessor establishing the nine-byte row stride.
    if (bytes.size() != 208 || bytes[0] != 207)
        throw FormatError("unit animation table requires a 207-byte counted vector");
    UnitAnimationTable table{};
    for (std::size_t type = 0; type < table.size(); ++type)
        std::copy_n(bytes.begin() + 1 + type * 9, 9, table[type].begin());
    return table;
}

std::vector<InitialPresentation> initial_presentations(const Archive& archive,
                                                       const FirstMissionScene& scene) {
    const auto found = archive.find(309,0xf4e02223U);
    if (found.size() != 1) throw FormatError("unit animation table is missing or ambiguous");
    const auto table = decode_unit_animation_table(archive.read(found.front()));
    std::vector<InitialPresentation> result;
    for (const auto& sprite : scene.sprites) {
        const auto& unit = scene.world.units.at(sprite.unit_index);
        if (!unit.active || unit.form != 0 || unit.spent_movement != 0 || unit.type >= table.size())
            throw FormatError("unit is outside the recovered initial presentation state");
        // The two first-mission units do not match any site or grouped
        // coordinate, so both original site predicates are false. This is not
        // a general implementation of site activation/capture/ownership.
        for (const auto& site : scene.world.sites) {
            const auto matches = [&](const WorldCoordinate& p) { return p.x == unit.x && p.y == unit.y; };
            if (matches(site.position))
                throw FormatError("initial site-dependent presentation is not implemented");
            for (const auto& group : site.coordinate_groups)
                if (std::any_of(group.begin(),group.end(),matches))
                    throw FormatError("initial site-group presentation is not implemented");
        }
        const auto animation = table[unit.type][1]; // form0, facing down1.
        if (animation >= sprite.descriptor.animations().size())
            throw FormatError("unit animation table points outside sprite descriptor");
        const auto placement = make_unit_sprite_placement({unit.x,unit.y,unit.form,0,0,false,false});
        result.push_back({placement.model,animation,-1,false});
    }
    return result;
}
} // namespace g1
