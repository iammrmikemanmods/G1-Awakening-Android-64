#include "g1/Scene.hpp"
#include "g1/InitialPresentation.hpp"

#include <cmath>
#include <iostream>
#include <map>
#include <sstream>

namespace {
g1::Bytes resource(const g1::Archive& archive, std::uint32_t id) {
    const auto matches = archive.find(id,0xf4e02223U);
    if (matches.size() != 1) throw g1::FormatError("catalog resource is missing or ambiguous");
    return archive.read(matches.front());
}

std::string quoted(const std::string& value) {
    std::ostringstream out;
    out << '"';
    for (const unsigned char c : value) {
        if (c == '"' || c == '\\') out << '\\' << c;
        else if (c == '\n') out << "\\n";
        else if (c < 32) throw std::runtime_error("unexpected control byte in diagnostic");
        else out << c;
    }
    out << '"';
    return out.str();
}

void require(bool value, const char* detail) {
    if (!value) throw std::runtime_error(detail);
}
} // namespace

// A private-data probe, not a campaign executor or device-render test. It
// decodes actual opcode9 map/style references without advancing those scripts.
int main(int argc, char** argv) {
    try {
        if (argc != 3) throw std::runtime_error("usage: scene_catalog_probe iphone.big terrain-tables.bin");
        const g1::Archive archive(g1::read_file(argv[1]));
        const auto tables = g1::read_file(argv[2],480);
        auto world = g1::bootstrap_first_campaign(archive);
        world.units.clear();
        const g1::MissionScene empty(archive,tables,world);
        require(empty.sprites.empty() && empty.textures.size() == 1,"empty roster scene failed");
        for (std::uint8_t type=0;type<23;++type) {
            g1::WorldUnit unit;
            unit.type=type;
            // Inactive records can retain coordinates from a prior map.
            unit.x=255; unit.y=255; unit.active=false;
            world.units.push_back(unit);
        }
        const g1::MissionScene roster(archive,tables,world);
        require(roster.sprites.size() == 23 && roster.textures.size() == 2,"full roster resources failed");
        require(roster.matches_map(world) && roster.matches_roster(world.units),"matching scene identity failed");
        auto changed = world;
        changed.units[0].active=true; changed.units[0].form=1;
        require(roster.matches_roster(changed.units),"activity/form must not change asset identity");
        std::swap(changed.units[0],changed.units[1]);
        require(!roster.matches_roster(changed.units),"roster reordering was not detected");
        changed=world; changed.units.pop_back();
        require(!roster.matches_roster(changed.units),"roster removal was not detected");
        changed=world; changed.grid.tiles[0]^=1;
        require(!roster.matches_map(changed),"map content change was not detected");
        changed=world; changed.style=1;
        require(!roster.matches_map(changed),"map style change was not detected");
        bool rejectedDuplicate=false,rejectedOutside=false;
        changed=world; changed.units[1].type=changed.units[0].type;
        try { const g1::MissionScene invalid(archive,tables,changed); }
        catch (const g1::FormatError&) { rejectedDuplicate=true; }
        changed=world; changed.units[0].active=true;
        try { const g1::MissionScene invalid(archive,tables,changed); }
        catch (const g1::FormatError&) { rejectedOutside=true; }
        require(rejectedDuplicate && rejectedOutside,"invalid live roster was accepted");

        const auto animations=g1::decode_unit_animation_table(resource(archive,309));
        std::cout << "{\n  \"scope\":\"Native data/geometry load; no gameplay execution or GPU draw\",\n"
                  << "  \"roster_contract_checks_passed\":true,\n  \"empty_roster_supported\":true,\n  \"types\":[";
        std::size_t totalVertices=0;
        for (std::size_t i=0;i<roster.sprites.size();++i) {
            const auto& sprite=roster.sprites[i];
            std::size_t vertices=0;
            for (std::size_t frame=0;frame<sprite.descriptor.frames().size();++frame) {
                const auto geometry=g1::sprite_vertices(sprite.descriptor,frame);
                for (const auto& vertex:geometry) {
                    for (const auto value:vertex.position) require(std::isfinite(value),"nonfinite sprite position");
                    for (const auto value:vertex.uv) require(std::isfinite(value),"nonfinite sprite UV");
                }
                vertices+=geometry.size();
            }
            for (const auto raw:animations[i]) {
                const int id=raw<128 ? int(raw) : int(raw)-256;
                require(id==-1 || (id>=0 && std::size_t(id)<sprite.descriptor.animations().size()),
                        "animation lookup exceeds actual descriptor");
            }
            totalVertices+=vertices;
            if (i) std::cout << ',';
            std::cout << "{\"type\":" << i << ",\"descriptor\":" << sprite.resource.descriptor_id
                      << ",\"image\":" << sprite.resource.image_id << ",\"frames\":"
                      << sprite.descriptor.frames().size() << ",\"animations\":"
                      << sprite.descriptor.animations().size() << ",\"vertices\":" << vertices << '}';
        }
        std::map<std::pair<std::uint16_t,std::uint16_t>,std::vector<std::pair<std::uint32_t,std::size_t>>> maps;
        for (std::uint32_t id=310;id<=339;++id) {
            const g1::MissionStream stream(resource(archive,id));
            for (const auto& command:stream.commands()) if (command.opcode==9) {
                const auto& words=command.argument_words_u16;
                require(words.size()==2 && words[1]<=2,"invalid original map command");
                maps[{words[0],words[1]}].push_back({id,command.instruction_index});
            }
        }
        std::cout << "],\n  \"all_frame_vertices\":" << totalVertices << ",\n  \"map_style_pairs\":[";
        std::size_t supported=0,unsupported=0;
        bool first=true;
        for (const auto& entry:maps) {
            auto mapWorld=world;
            mapWorld.units.clear(); mapWorld.sites.clear(); mapWorld.camera.reset();
            mapWorld.grid_resource_id=g1::mission_grid_resource(entry.first.first);
            mapWorld.style=static_cast<std::uint8_t>(entry.first.second);
            mapWorld.grid=g1::decode_terrain_grid(resource(archive,mapWorld.grid_resource_id));
            if (!first) std::cout << ',';
            first=false;
            std::cout << "{\"map_index\":" << entry.first.first << ",\"resource\":"
                      << mapWorld.grid_resource_id << ",\"style\":" << unsigned(mapWorld.style)
                      << ",\"width\":" << mapWorld.grid.width << ",\"height\":" << mapWorld.grid.height;
            try {
                const g1::MissionScene scene(archive,tables,mapWorld);
                const auto geometry=g1::terrain_vertices(scene.terrain);
                for (const auto& vertex:geometry) {
                    for (const auto value:vertex.position) require(std::isfinite(value),"nonfinite terrain position");
                    for (const auto value:vertex.uv) require(std::isfinite(value),"nonfinite terrain UV");
                }
                if(scene.terrain.animated_surface) {
                    const auto& animated=*scene.terrain.animated_surface;
                    const auto extra=g1::terrain_vertices(animated.quads,scene.terrain.position_scale);
                    for(const auto& vertex:extra) {
                        for(const auto value:vertex.position)require(std::isfinite(value),"nonfinite animated terrain position");
                        for(const auto value:vertex.uv)require(std::isfinite(value),"nonfinite animated terrain UV");
                    }
                    require(scene.textures.count(animated.atlas_resource_id)==1,"animated terrain texture is absent");
                }
                ++supported;
                std::cout << ",\"loaded\":true,\"atlas\":" << scene.terrain.atlas_resource_id
                          << ",\"quads\":" << scene.terrain.quads.size()
                          << ",\"animated_quads\":" << (scene.terrain.animated_surface ? scene.terrain.animated_surface->quads.size() : 0)
                          << ",\"decoration_slots\":" << (scene.terrain.animated_surface ? scene.terrain.animated_surface->decoration_slots : 0);
            } catch (const g1::FormatError& error) {
                // Preserve explicit unsupported rendering boundaries. A data
                // corruption or unrelated decoder failure still fails this probe.
                const std::string detail=error.what();
                if (detail.find("animated")==std::string::npos) throw;
                ++unsupported;
                std::cout << ",\"loaded\":false,\"error\":" << quoted(detail);
            }
            std::cout << ",\"references\":[";
            for (std::size_t i=0;i<entry.second.size();++i) {
                if (i) std::cout << ',';
                std::cout << '[' << entry.second[i].first << ',' << entry.second[i].second << ']';
            }
            std::cout << "]}";
        }
        std::cout << "],\n  \"supported_map_style_pairs\":" << supported
                  << ",\"unsupported_map_style_pairs\":" << unsupported
                  << ",\n  \"gpu_execution\":false,\"campaign_completion\":false\n}\n";
        return 0;
    } catch (const std::exception& error) {
        std::cerr << "ERROR: " << error.what() << '\n';
        return 1;
    }
}
