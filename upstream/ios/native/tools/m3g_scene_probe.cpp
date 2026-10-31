#include "g1/M3GScene.hpp"

#include <filesystem>
#include <fstream>
#include <iomanip>
#include <iostream>
#include <sstream>

namespace {
std::string quoted(const std::string& value) {
    std::ostringstream result; result << '"';
    for (const char c:value) {
        if (c=='"' || c=='\\') result << '\\';
        if (static_cast<unsigned char>(c)<32) throw std::runtime_error("control character in model name");
        result << c;
    }
    result << '"'; return result.str();
}
template<class Array> void array(std::ostream& out,const Array& values) {
    out << '['; bool first=true;
    for (const auto value:values) { if (!first) out << ','; first=false; out << value; }
    out << ']';
}
void write(const std::filesystem::path& path,const void* data,std::size_t bytes) {
    std::ofstream stream(path,std::ios::binary);
    if (!stream || !stream.write(static_cast<const char*>(data),static_cast<std::streamsize>(bytes)))
        throw std::runtime_error("cannot write private model export");
}
} // namespace

int main(int argc,char** argv) {
    try {
        if (argc<2) throw std::runtime_error("usage: m3g_scene_probe MODEL.m3g [--user-id ID] [--export DIRECTORY]");
        std::uint32_t user=0; std::filesystem::path output;
        for (int i=2;i<argc;i+=2) {
            if (i+1>=argc) throw std::runtime_error("missing model probe argument");
            const std::string option=argv[i];
            if (option=="--user-id") {
                const auto value=std::stoull(argv[i+1]);
                if (value>0xffffffffU) throw std::runtime_error("user ID is outside uint32");
                user=static_cast<std::uint32_t>(value);
            } else if (option=="--export") output=argv[i+1];
            else throw std::runtime_error("unknown model probe option");
        }
        const g1::M3GScene scene(g1::read_file(argv[1]));
        std::size_t root=0;
        if (user) {
            const auto matches=scene.find_user_id(user);
            if (matches.size()!=1) throw std::runtime_error("requested model user ID is missing or ambiguous");
            root=matches.front();
        }
        const auto model=scene.static_model(root);
        if (!output.empty()) std::filesystem::create_directories(output);
        std::ostringstream out; out << std::setprecision(9);
        out << "{\"objects\":" << scene.objects().size() << ",\"root\":" << model.root_object
            << ",\"static_serialized_pose\":true,\"animation_evaluated\":false,\"gpu_execution\":false"
            << ",\"animation_track_bindings\":" << model.animation_track_bindings << ",\"minimum\":";
        array(out,model.minimum); out << ",\"maximum\":"; array(out,model.maximum);
        out << ",\"nodes\":["; bool first=true;
        for (const auto& info:scene.objects()) if (info.is_node || info.type==17) {
            if (!first) out << ',';
            first=false;
            out << "{\"index\":" << info.index << ",\"type\":" << unsigned(info.type)
                << ",\"user_id\":" << info.user_id << ",\"name\":" << quoted(info.name) << ",\"parent\":";
            if (info.parent) out << *info.parent; else out << "null";
            out << ",\"children\":"; array(out,info.children);
            out << ",\"render_enabled\":" << (info.rendering_enabled ? "true" : "false")
                << ",\"alpha\":" << unsigned(info.alpha_factor) << ",\"local_matrix\":";
            array(out,g1::m3g_transform_matrix(info.transform)); out << '}';
        }
        out << "],\"surfaces\":[";
        for (std::size_t i=0;i<model.surfaces.size();++i) {
            const auto& surface=model.surfaces[i]; const auto& appearance=surface.appearance;
            if (i) out << ',';
            out << "{\"mesh\":" << surface.mesh_object << ",\"submesh\":" << surface.submesh_index
                << ",\"vertex_buffer\":" << surface.vertex_buffer_object << ",\"index_buffer\":" << surface.index_buffer_object
                << ",\"appearance\":" << surface.appearance_object << ",\"vertices\":" << surface.vertices.size()
                << ",\"layer\":" << int(appearance.layer) << ",\"model\":"; array(out,surface.model);
            out << ",\"material\":" << (appearance.material ? "true" : "false") << ",\"texture\":";
            if (appearance.texture) {
                const auto& t=*appearance.texture;
                out << "{\"image\":" << t.image_object << ",\"format\":" << unsigned(t.image_format)
                    << ",\"function\":" << unsigned(t.function) << ",\"wrap_s\":" << unsigned(t.wrap_s)
                    << ",\"wrap_t\":" << unsigned(t.wrap_t) << ",\"level_filter\":" << unsigned(t.level_filter)
                    << ",\"image_filter\":" << unsigned(t.image_filter) << '}';
            } else out << "null";
            out << '}';
            if (!output.empty()) write(output/("surface_"+std::to_string(i)+".f32"),surface.vertices.data(),surface.vertices.size()*sizeof(g1::M3GVertex));
        }
        out << "],\"images\":["; first=true;
        for (const auto& entry:model.images) {
            if (!first) out << ',';
            first=false;
            out << "{\"object\":" << entry.first << ",\"width\":" << entry.second.width << ",\"height\":" << entry.second.height << '}';
            if (!output.empty()) write(output/("image_"+std::to_string(entry.first)+".rgba"),entry.second.pixels.data(),entry.second.pixels.size());
        }
        out << "]}\n";
        const auto text=out.str();
        if (!output.empty()) write(output/"model.json",text.data(),text.size());
        std::cout << text;
        return 0;
    } catch (const std::exception& error) {
        std::cerr << "ERROR: " << error.what() << '\n'; return 1;
    }
}
