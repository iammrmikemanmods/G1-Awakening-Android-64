#include "g1/InitialPresentation.hpp"
#include "g1/SpriteAnimation.hpp"

#include <algorithm>
#include <charconv>
#include <filesystem>
#include <fstream>
#include <iomanip>
#include <iostream>
#include <limits>

// Development-only export of new native C++ output. No old executable or
// guest instruction execution is used. Outputs contain original artwork and
// must stay outside the source repository.
namespace {
using Matrix = std::array<float, 16>;

Matrix multiply(const Matrix& a, const Matrix& b) {
    Matrix result{};
    for (std::size_t column = 0; column < 4; ++column)
        for (std::size_t row = 0; row < 4; ++row)
            for (std::size_t k = 0; k < 4; ++k)
                result[column * 4 + row] += a[k * 4 + row] * b[column * 4 + k];
    return result;
}

std::array<float, 4> transform(const Matrix& matrix, const std::array<float, 4>& point) {
    std::array<float, 4> result{};
    for (std::size_t row = 0; row < 4; ++row)
        for (std::size_t column = 0; column < 4; ++column)
            result[row] += matrix[column * 4 + row] * point[column];
    return result;
}

// Exact float expressions transcribed from InspectionCamera::fit/matrix in
// platform/apple/NativeApp.mm. This is not the original game's camera.
Matrix inspection_camera(const g1::FirstMissionScene& scene, unsigned width, unsigned height) {
    constexpr float up_y = 0.7071067811865476F;
    constexpr float up_z = -0.7071067811865476F;
    const auto aspect = static_cast<float>(static_cast<double>(width) / height);
    const float center_x = scene.world.grid.width * 0.16F;
    const float center_z = scene.world.grid.height * 0.16F;
    const float span = 1.15F * std::max(scene.world.grid.height * 0.32F * -up_z + 0.6F,
                                      (scene.world.grid.width * 0.32F + 0.8F) / aspect);
    Matrix result{};
    result[0] = 2 / (span * aspect);
    result[5] = 2 * up_y / span;
    result[9] = 2 * up_z / span;
    result[6] = up_z / 100;
    result[10] = -up_y / 100;
    result[12] = -center_x * result[0];
    result[13] = -center_z * result[9];
    result[14] = 0.5F - center_z * result[10];
    result[15] = 1;
    return result;
}

unsigned dimension(const char* text) {
    unsigned result{};
    const std::string value(text);
    const auto converted = std::from_chars(value.data(), value.data() + value.size(), result);
    if (converted.ec != std::errc{} || converted.ptr != value.data() + value.size() ||
        result == 0 || result > 4096)
        throw g1::FormatError("image dimensions must be integers in 1..4096");
    return result;
}

template<class Range, class Function> void list(std::ostream& out, const Range& range, Function emit) {
    out << '[';
    bool first = true;
    for (const auto& value : range) {
        if (!first) out << ',';
        first = false;
        emit(value);
    }
    out << ']';
}

template<class Range> void numbers(std::ostream& out, const Range& range) {
    list(out, range, [&](auto value) { out << +value; });
}

void mesh(std::ostream& out, const std::vector<g1::SceneVertex>& vertices, const Matrix& matrix) {
    out << ",\"matrix\":";
    numbers(out, matrix);
    out << ",\"vertices_position_xyzw_uv\":";
    list(out, vertices, [&](const auto& vertex) {
        const std::array<float, 6> data{{vertex.position[0], vertex.position[1], vertex.position[2],
                                       vertex.position[3], vertex.uv[0], vertex.uv[1]}};
        numbers(out, data);
    });
    out << ",\"clip_positions_xyzw\":";
    list(out, vertices, [&](const auto& vertex) { numbers(out, transform(matrix, vertex.position)); });
}
} // namespace

int main(int argc, char** argv) {
    try {
        if (argc != 6) {
            std::cerr << "Usage: scene_export iphone.big terrain-tables.bin NEW_OUTPUT_DIR WIDTH HEIGHT\n";
            return 2;
        }
        const auto width = dimension(argv[4]), height = dimension(argv[5]);
        const g1::Archive archive(g1::read_file(argv[1]));
        const g1::FirstMissionScene scene(archive, g1::read_file(argv[2], 480));
        const auto presentations = g1::initial_presentations(archive, scene);
        if (presentations.size() != scene.sprites.size()) throw g1::FormatError("presentation count mismatch");
        const std::filesystem::path directory(argv[3]);
        if (!std::filesystem::create_directory(directory))
            throw g1::FormatError("output directory must be new and its parent must exist");
        std::ofstream out(directory / "scene.json");
        out.exceptions(std::ios::failbit | std::ios::badbit);
        out << std::setprecision(std::numeric_limits<float>::max_digits10);
        const auto camera = inspection_camera(scene, width, height);
        out << "{\"schema\":1,\"kind\":\"native scene export for CPU reference, not Metal capture\","
               "\"camera_kind\":\"NativeApp inspection camera, not recovered original camera\","
               "\"animation_elapsed_ms\":0,\"width\":" << width << ",\"height\":" << height
            << ",\"clear_rgba\":[0.025,0.04,0.065,1],\"depth_clear\":1,"
               "\"sampler\":\"linear clamp-to-edge; no mipmaps\",\"depth_compare\":\"less-equal\","
               "\"alpha_discard_threshold\":0.0039215686,\"cull\":\"none\","
               "\"blend\":\"RGB srcAlpha/oneMinusSrcAlpha; alpha one/oneMinusSrcAlpha\","
               "\"mission_id\":" << scene.world.mission_resource_id
            << ",\"grid_id\":" << scene.world.grid_resource_id << ",\"grid_width\":" << scene.world.grid.width
            << ",\"grid_height\":" << scene.world.grid.height << ",\"style\":" << +scene.world.style
            << ",\"campaign_command_cursor\":" << scene.world.next_command_index
            << ",\"terrain_quads\":" << scene.terrain.quads.size() << ",\"inspection_matrix\":";
        numbers(out, camera);
        out << ",\"textures\":";
        list(out, scene.textures, [&](const auto& entry) {
            const auto name = "texture_" + std::to_string(entry.first) + ".rgba";
            std::ofstream pixels(directory / name, std::ios::binary);
            pixels.exceptions(std::ios::failbit | std::ios::badbit);
            pixels.write(reinterpret_cast<const char*>(entry.second.pixels.data()),
                         static_cast<std::streamsize>(entry.second.pixels.size()));
            out << "{\"resource_id\":" << entry.first << ",\"width\":" << entry.second.width
                << ",\"height\":" << entry.second.height << ",\"file\":\"" << name
                << "\",\"format\":\"top-to-bottom straight RGBA8 from native decode_png\"}";
        });
        out << ",\"draws\":[{\"kind\":\"terrain\",\"texture\":" << scene.terrain.atlas_resource_id
            << ",\"depth_write\":true,\"tint\":[1,1,1,1]";
        mesh(out, g1::terrain_vertices(scene.terrain), camera);
        out << '}';
        std::vector<std::size_t> order;
        for (std::size_t i = 0; i < scene.sprites.size(); ++i) order.push_back(i);
        std::stable_sort(order.begin(), order.end(), [&](auto a, auto b) {
            const auto pa = transform(multiply(camera, presentations[a].model), {{0,0,0,1}});
            const auto pb = transform(multiply(camera, presentations[b].model), {{0,0,0,1}});
            return pa[2] > pb[2];
        });
        for (const auto index : order) {
            const auto& sprite = scene.sprites[index];
            const auto& presentation = presentations[index];
            const auto& unit = scene.world.units.at(sprite.unit_index);
            g1::SpriteAnimation animation(sprite.descriptor.animations().at(presentation.animation),
                                          presentation.repeats, presentation.reverse);
            animation.advance_ms(0); // The first NativeApp draw has zero elapsed time.
            const auto frame = animation.current_frame();
            out << ",{\"kind\":\"unit\",\"unit_index\":" << sprite.unit_index << ",\"type\":" << +unit.type
                << ",\"tile\":[" << +unit.x << ',' << +unit.y << "],\"descriptor_id\":" << sprite.resource.descriptor_id
                << ",\"texture\":" << sprite.resource.image_id << ",\"animation\":" << presentation.animation
                << ",\"frame\":" << +frame << ",\"depth_write\":true,\"tint\":[1,1,1,1],\"model\":";
            numbers(out, presentation.model);
            mesh(out, g1::sprite_vertices(sprite.descriptor, frame), multiply(camera, presentation.model));
            out << '}';
        }
        out << "]}\n";
        out.close();
        std::cout << "Exported " << scene.terrain.quads.size() << " terrain quads and "
                  << scene.sprites.size() << " initial sprites to " << directory.string() << '\n';
        return 0;
    } catch (const std::exception& error) {
        std::cerr << "ERROR: " << error.what() << '\n';
        return 1;
    }
}
