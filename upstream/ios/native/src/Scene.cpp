#include "g1/Scene.hpp"

#include <utility>

namespace g1 {
namespace {
Bytes read_resource(const Archive& archive, std::uint32_t id, std::uint32_t key) {
    const auto found = archive.find(id, key);
    if (found.size() != 1) throw FormatError("scene resource is missing or ambiguous");
    return archive.read(found.front());
}

void texture(const Archive& archive, std::map<std::uint32_t, RgbaImage>& textures,
             std::uint32_t id, std::uint32_t key, std::uint32_t width, std::uint32_t height) {
    if (textures.count(id) != 0) return;
    auto image = decode_png(read_resource(archive, id, key));
    if (image.width != width || image.height != height)
        throw FormatError("scene texture dimensions differ from the recovered atlas");
    textures.emplace(id, std::move(image));
}

TerrainMesh scene_terrain(const Archive& archive,const Bytes& tables,const WorldBootstrap& world) {
    const auto decoded=decode_terrain_tables(tables);
    if (world.style==1)
        return build_terrain_mesh(world.grid,decoded,world.style,
            decode_terrain_animation_config(read_resource(archive,307,0xf4e02223U)));
    return build_terrain_mesh(world.grid,decoded,world.style);
}
} // namespace

MissionScene::MissionScene(const Archive& archive, const Bytes& tables, WorldBootstrap current_world)
    : world(std::move(current_world)),
      terrain(scene_terrain(archive,tables,world)) {
    constexpr std::uint32_t key = 0xf4e02223U;
    if (world.units.size() > 23) throw FormatError("scene roster exceeds original type count");
    std::array<bool,23> seen{};
    texture(archive, textures, terrain.atlas_resource_id, key, 512, 256);
    if (terrain.animated_surface)
        texture(archive,textures,terrain.animated_surface->atlas_resource_id,key,256,32);
    for (std::size_t i = 0; i < world.units.size(); ++i) {
        const auto& unit = world.units[i];
        if (unit.type >= seen.size() || seen[unit.type])
            throw FormatError("scene roster has an invalid or duplicate gameplay type");
        seen[unit.type] = true;
        if (unit.active && (unit.x >= world.grid.width || unit.y >= world.grid.height))
            throw FormatError("active scene unit is outside the current map");
        const auto mapping = sprite_resource_for_type(unit.type);
        if (!mapping) throw FormatError("unit has no recovered sprite mapping");
        texture(archive, textures, mapping->image_id, mapping->resource_key, 512, 512);
        sprites.push_back({i, *mapping, AtlasDescriptor(
            read_resource(archive, mapping->descriptor_id, mapping->resource_key))});
    }
}

bool MissionScene::matches_map(const WorldBootstrap& other) const noexcept {
    return world.grid_resource_id == other.grid_resource_id && world.style == other.style &&
        world.grid.width == other.grid.width && world.grid.height == other.grid.height &&
        world.grid.tiles == other.grid.tiles;
}

bool MissionScene::matches_roster(const std::vector<WorldUnit>& other) const noexcept {
    if (world.units.size() != other.size()) return false;
    for (std::size_t i=0;i<other.size();++i) if (world.units[i].type != other[i].type) return false;
    return true;
}

FirstMissionScene::FirstMissionScene(const Archive& archive, const Bytes& tables)
    : MissionScene(archive,tables,bootstrap_first_campaign(archive)) {}

std::vector<SceneVertex> terrain_vertices(const TerrainMesh& mesh) {
    return terrain_vertices(mesh.quads,mesh.position_scale);
}

std::vector<SceneVertex> terrain_vertices(const std::vector<TerrainQuad>& quads,float position_scale) {
    std::vector<SceneVertex> result;
    result.reserve(quads.size() * 6);
    for (const auto& quad : quads) {
        for (const auto index : sprite_triangle_indices) {
            const auto& p = quad.positions[index];
            result.push_back({{p[0] * position_scale, p[1] * position_scale,
                               p[2] * position_scale, 1.0F}, quad.normalized_uv[index], {}});
        }
    }
    return result;
}

std::vector<SceneVertex> sprite_vertices(const AtlasDescriptor& descriptor, std::size_t frame) {
    const auto quads = make_sprite_geometry(descriptor, frame);
    std::vector<SceneVertex> result;
    result.reserve(quads.size() * 6);
    for (const auto& quad : quads) {
        for (const auto index : sprite_triangle_indices) {
            const auto& v = quad.vertices[index];
            result.push_back({{v.position[0], v.position[1], v.position[2], 1.0F}, v.uv, {}});
        }
    }
    return result;
}
} // namespace g1
