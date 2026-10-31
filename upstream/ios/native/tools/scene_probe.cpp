#include "g1/Scene.hpp"
#include "g1/InitialPresentation.hpp"

#include <iostream>

int main(int argc, char** argv) {
    try {
        if (argc != 3) throw std::runtime_error("usage: scene_probe iphone.big terrain-tables.bin");
        const g1::Archive archive(g1::read_file(argv[1]));
        const g1::FirstMissionScene scene(archive, g1::read_file(argv[2], 480));
        const auto vertices = g1::terrain_vertices(scene.terrain);
        const auto initial = g1::initial_presentations(archive,scene);
        std::cout << "{\n  \"mission_resource\": " << scene.world.mission_resource_id
                  << ", \"map_resource\": " << scene.world.grid_resource_id
                  << ", \"width\": " << scene.world.grid.width
                  << ", \"height\": " << scene.world.grid.height
                  << ",\n  \"stopped_before_command\": " << scene.world.next_command_index
                  << ", \"terrain_quads\": " << scene.terrain.quads.size()
                  << ", \"terrain_vertices\": " << vertices.size() << ",\n  \"textures\": [";
        bool first = true;
        for (const auto& entry : scene.textures) {
            if (!first) std::cout << ',';
            first = false;
            std::cout << "{\"id\":" << entry.first << ",\"width\":" << entry.second.width
                      << ",\"height\":" << entry.second.height << '}';
        }
        std::cout << "],\n  \"sprites\": [";
        first = true;
        for (const auto& sprite : scene.sprites) {
            if (!first) std::cout << ',';
            first = false;
            std::size_t all_vertices = 0;
            for (std::size_t frame = 0; frame < sprite.descriptor.frames().size(); ++frame)
                all_vertices += g1::sprite_vertices(sprite.descriptor, frame).size();
            const auto& unit = scene.world.units[sprite.unit_index];
            const auto range = g1::compute_movement_range(scene.world.movement_board(),
                scene.world.movement_profile(sprite.unit_index), unit.x, unit.y);
            std::size_t reachable = 0;
            for (const auto value : range.remaining) if (value >= 0) ++reachable;
            const auto& presentation = initial.at(sprite.unit_index);
            std::cout << "{\"type\":" << unsigned(unit.type) << ",\"descriptor\":"
                      << sprite.resource.descriptor_id << ",\"frames\":"
                      << sprite.descriptor.frames().size() << ",\"all_frame_vertices\":"
                      << all_vertices << ",\"reachable_cells\":" << reachable
                      << ",\"initial_animation\":" << presentation.animation
                      << ",\"model_translation\":[" << presentation.model[12] << ','
                      << presentation.model[13] << ',' << presentation.model[14] << "]}";
        }
        std::cout << "],\n  \"gameplay_executor\": false, \"gpu_execution\": false\n}\n";
        return 0;
    } catch (const std::exception& error) {
        std::cerr << "ERROR: " << error.what() << '\n';
        return 1;
    }
}
