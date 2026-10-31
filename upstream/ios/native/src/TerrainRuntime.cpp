#include "g1/TerrainRuntime.hpp"
#include "g1/Combat.hpp"

namespace g1 {
namespace {
TerrainRuntime layout_for(const Archive& archive,
    const WorldBootstrap& world,const TerrainTables& tables) {
    if(world.style>2)throw FormatError("terrain runtime style is invalid");
    const auto found=archive.find(307,0xf4e02223U);
    if(found.size()!=1)throw FormatError("terrain runtime resource307 is missing or ambiguous");
    const auto config=decode_terrain_animation_config(archive.read(found.front()));
    TerrainRuntime result;result.tables=tables;
    if(world.style==1)result.layout=build_terrain_decoration_layout(world.grid,tables,config);
    else {
        result.sprite_layout=build_terrain_sprite_decoration_layout(world.grid,tables,config,
            world.effective_lookup,world.style);
        result.sprite_animations=load_terrain_sprite_animations(archive);
    }
    return result;
}
}

std::uint32_t terrain_tables_checksum(const TerrainTables& tables) {
    Bytes bytes;bytes.reserve(480);
    for(const auto& style:tables)for(const auto& record:style) {
        bytes.insert(bytes.end(),record.atlas_cells.begin(),record.atlas_cells.end());
        bytes.push_back(static_cast<std::uint8_t>(record.height_steps));
    }
    return adler32_bytes(bytes);
}

void validate_terrain_runtime(const TerrainRuntime& runtime,std::uint8_t style) {
    if(style>2 || runtime.layout.has_value()!=(style==1) ||
       runtime.state.has_value()!=(style==1) ||
       runtime.sprite_layout.has_value()!=(style!=1) ||
       runtime.sprite_animations.has_value()!=(style!=1) ||
       runtime.sprite_state.has_value()!=(style!=1))
        throw FormatError("terrain runtime state does not match its world style");
    if(runtime.layout)validate_terrain_decorations(*runtime.layout,*runtime.state);
    else {
        if(runtime.sprite_layout->style!=style)
            throw FormatError("terrain sprite layout style does not match its world");
        // The animation player's generic API reports invalid value states as
        // logic errors. At the saved-data boundary they are format failures,
        // with no partial installation or animation restart.
        try {
            validate_terrain_sprite_decorations(*runtime.sprite_layout,*runtime.sprite_animations,*runtime.sprite_state);
        } catch(const std::logic_error& error) {
            throw FormatError(std::string("terrain sprite playback is invalid: ")+error.what());
        }
    }
}

TerrainRuntime create_terrain_runtime(const Archive& archive,const WorldBootstrap& world,
    const TerrainTables& tables,GameRandom& random) {
    auto result=layout_for(archive,world,tables);
    auto working=random;
    if(result.layout)result.state=create_terrain_decorations(*result.layout,working);
    else result.sprite_state=create_terrain_sprite_decorations(*result.sprite_layout,
        *result.sprite_animations,working);
    validate_terrain_runtime(result,world.style);
    random=std::move(working);
    return result;
}

TerrainRuntime restore_terrain_runtime(const Archive& archive,const WorldBootstrap& world,
    const TerrainTables& tables,const std::optional<TerrainDecorationState>& state,
    const std::optional<TerrainSpriteDecorationState>& sprite_state) {
    auto result=layout_for(archive,world,tables);
    result.state=state;result.sprite_state=sprite_state;
    validate_terrain_runtime(result,world.style);
    return result;
}

} // namespace g1
