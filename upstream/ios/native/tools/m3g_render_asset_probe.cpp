#include "g1/M3GScene.hpp"
#include "g1/Archive.hpp"

#include <cstring>
#include <iostream>

namespace {
std::uint32_t bits(float value) { std::uint32_t out;std::memcpy(&out,&value,4);return out; }
template<class T,std::size_t N> void array(const std::array<T,N>& values) {
    std::cout<<'[';for(std::size_t i=0;i<N;++i) { if(i)std::cout<<',';std::cout<<values[i]; }std::cout<<']';
}
} // namespace
int main(int argc,char** argv) {
    try {
        if(argc!=2)throw std::runtime_error("usage: m3g_render_asset_probe scene.m3g");
        const g1::M3GScene scene(g1::read_file(argv[1]));const auto asset=scene.render_asset();
        std::cout<<"{\"root\":"<<asset.geometry.root_object<<",\"surfaces\":[";
        for(std::size_t i=0;i<asset.geometry.surfaces.size();++i) {
            if(i)std::cout<<',';
            const auto& surface=asset.geometry.surfaces[i];
            std::cout<<"{\"object\":"<<surface.mesh_object<<",\"part\":"<<surface.submesh_index
                     <<",\"appearance\":"<<surface.appearance_object<<",\"vertices\":"<<surface.vertices.size()
                     <<",\"vertex_color_components\":"<<unsigned(surface.vertex_color_components)
                     <<",\"default_alpha_bits\":"<<bits(surface.default_color_alpha)<<'}';
        }
        std::cout<<"],\"sprites\":[";
        for(std::size_t i=0;i<asset.sprites.size();++i) {
            if(i)std::cout<<',';
            const auto& sprite=asset.sprites[i];const auto& image=asset.geometry.images.at(sprite.image_object);
            std::cout<<"{\"object\":"<<sprite.node_object<<",\"image\":"<<sprite.image_object
                     <<",\"appearance\":"<<sprite.appearance_object<<",\"scaled\":"<<(sprite.scaled ? "true":"false")
                     <<",\"crop\":";array(sprite.crop_pixels);
            std::cout<<",\"image_size\":["<<image.width<<','<<image.height<<"],\"image_format\":"<<unsigned(sprite.image_format)
                     <<",\"alpha_bits\":"<<bits(sprite.effective_alpha)<<",\"scope\":"<<sprite.scope
                     <<",\"layer\":"<<int(sprite.appearance.layer)<<",\"blend\":"<<unsigned(sprite.appearance.blending)
                     <<",\"alpha_threshold_bits\":"<<bits(sprite.appearance.alpha_threshold)
                     <<",\"depth_test\":"<<(sprite.appearance.depth_test?"true":"false")
                     <<",\"depth_write\":"<<(sprite.appearance.depth_write?"true":"false")
                     <<",\"color_write\":"<<(sprite.appearance.color_write?"true":"false")
                     <<",\"alpha_write\":"<<(sprite.appearance.alpha_write?"true":"false")
                     <<",\"culling\":"<<unsigned(sprite.appearance.culling)<<'}';
        }
        std::cout<<"],\"traversal\":[";
        for(std::size_t i=0;i<asset.traversal.size();++i) {
            if(i)std::cout<<',';
            const auto& draw=asset.traversal[i];
            std::cout<<"[\""<<(draw.kind==g1::M3GDrawableKind::mesh?"mesh":"sprite")<<"\","<<draw.index<<']';
        }
        std::cout<<"],\"images\":"<<asset.geometry.images.size()<<"}\n";return 0;
    }catch(const std::exception& error) { std::cerr<<error.what()<<'\n';return 1; }
}
