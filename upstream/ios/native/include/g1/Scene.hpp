#pragma once

#include "g1/Image.hpp"
#include "g1/SpriteGeometry.hpp"
#include "g1/World.hpp"

#include <map>

namespace g1 {

// Explicit 32-byte GPU layout. A Metal float3 has different alignment from
// SpriteVertex's packed 20-byte C++ layout, so the renderer converts here.
struct alignas(16) SceneVertex {
    std::array<float, 4> position;
    std::array<float, 2> uv;
    std::array<float, 2> padding{};
};
static_assert(sizeof(SceneVertex) == 32, "Metal vertex stride must be 32 bytes");
static_assert(offsetof(SceneVertex, uv) == 16, "Metal texture coordinate offset changed");

struct SceneSprite {
    std::size_t unit_index;
    SpriteResource resource;
    AtlasDescriptor descriptor;
};

// Resources and native geometry for an already decoded/applied world. The
// current unique-type roster may include inactive entries and any original
// gameplay type. Camera, unit playback state and command acknowledgement are
// separate host responsibilities. Unsupported terrain paths remain errors.
class MissionScene {
public:
    MissionScene(const Archive& archive, const Bytes& terrain_tables, WorldBootstrap world);
    WorldBootstrap world;
    TerrainMesh terrain;
    std::map<std::uint32_t, RgbaImage> textures;
    std::vector<SceneSprite> sprites;

    bool matches_map(const WorldBootstrap&) const noexcept;
    bool matches_roster(const std::vector<WorldUnit>&) const noexcept;
};

// Kept for the existing first-campaign tools/reference. This constructor does
// not execute campaign continuation or imply that every map style is complete.
class FirstMissionScene : public MissionScene {
public:
    FirstMissionScene(const Archive& archive, const Bytes& terrain_tables);
};

std::vector<SceneVertex> terrain_vertices(const TerrainMesh&);
std::vector<SceneVertex> terrain_vertices(const std::vector<TerrainQuad>&, float position_scale);
std::vector<SceneVertex> sprite_vertices(const AtlasDescriptor&, std::size_t frame);

} // namespace g1
