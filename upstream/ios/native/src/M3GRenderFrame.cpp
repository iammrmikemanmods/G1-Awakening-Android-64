#include "g1/M3GRenderFrame.hpp"

#include <cmath>
#include <limits>
#include <set>

namespace g1 {
namespace {
constexpr M3GMatrix identity{{1,0,0,0, 0,1,0,0, 0,0,1,0, 0,0,0,1}};
void require(bool condition,const char* message) { if(!condition)throw FormatError(message); }
void alpha(float value) {
    require(std::isfinite(value) && value>=0 && value<=1,"invalid M3G render alpha");
}
void finite(const M3GMatrix& matrix) {
    for(const auto value:matrix)require(std::isfinite(value),"nonfinite M3G frame matrix");
}
void affine(const M3GMatrix& matrix) {
    finite(matrix);
    require(matrix[3]==0 && matrix[7]==0 && matrix[11]==0 && matrix[15]==1,
            "M3G frame transform is not affine");
}
float multiply(float a,float b) noexcept { volatile float value=a*b;return value; }
float add(float a,float b) noexcept { volatile float value=a+b;return value; }
std::array<float,3> origin(const M3GMatrix& matrix) {
    return {{matrix[12],matrix[13],matrix[14]}};
}
std::int32_t grouping(const std::map<std::size_t,std::int32_t>* values,std::size_t appearance) {
    require(values!=nullptr,"opaque M3G draw requires explicit original Appearance grouping metadata");
    const auto found=values->find(appearance);
    require(found!=values->end(),"opaque M3G Appearance grouping metadata is absent");
    return found->second;
}
void sprite_vertices(M3GPreparedDraw& draw) {
    const auto& projection=draw.sprite_projection;
    if(!projection.drawn())return;
    constexpr std::array<std::size_t,6> strip_to_triangles{{0,1,2,2,1,3}};
    for(std::size_t i=0;i<strip_to_triangles.size();++i) {
        const auto index=strip_to_triangles[i];auto& vertex=draw.sprite_vertices[i];
        vertex.position={{add(projection.center_ndc_gl[0],multiply(static_cast<float>(projection.offsets_fixed16[index*2]),1.0F/65536.0F)),
                          add(projection.center_ndc_gl[1],multiply(static_cast<float>(projection.offsets_fixed16[index*2+1]),1.0F/65536.0F)),
                          multiply(add(projection.center_ndc_gl[2],1.0F),0.5F),1.0F}};
        vertex.normal={{0,0,0,0}};vertex.color={{1,1,1,1}};
        vertex.texcoord={{multiply(static_cast<float>(projection.uv_fixed16[index*2]),1.0F/65536.0F),
                         multiply(static_cast<float>(projection.uv_fixed16[index*2+1]),1.0F/65536.0F),0,1}};
        for(const auto value:vertex.position)require(std::isfinite(value),"nonfinite projected Sprite3D vertex");
    }
}
} // namespace

bool m3g_mesh_queue_transparent(const M3GMeshQueueInput& input) {
    require(input.blending>=64 && input.blending<=68,"invalid M3G queue blend mode");
    require(std::isfinite(input.effective_alpha),"nonfinite M3G queue effective alpha");
    alpha(input.default_color_alpha);
    if(input.material_diffuse_alpha)alpha(*input.material_diffuse_alpha);
    require(input.vertex_color_components==0 || input.vertex_color_components==3 || input.vertex_color_components==4,
            "invalid M3G queue vertex-color components");
    for(const auto format:input.texture_image_formats)if(format)
        require(*format>=96 && *format<=100,"invalid M3G queue image format");
    if(input.blending==68)return false;
    if(input.blending!=64)return true;
    if(input.effective_alpha<1)return true;
    if(input.material_diffuse_alpha && *input.material_diffuse_alpha!=1)return true;
    if(input.vertex_color_components==4 ||
       (!input.vertex_color_components && input.default_color_alpha!=1))return true;
    for(const auto format:input.texture_image_formats)
        if(format && (*format==96 || *format==98 || *format==100))return true;
    return false;
}

std::map<std::size_t,std::int32_t> m3g_mesh_appearance_order_keys(const M3GScene& scene) {
    std::map<std::size_t,std::int32_t> result;
    for(const auto& object:scene.objects())if(object.type==3) {
        require(result.size()<static_cast<std::size_t>(std::numeric_limits<std::int32_t>::max()),
                "M3G Appearance rank exceeds the recovered signed ordering domain");
        result.emplace(object.index,static_cast<std::int32_t>(result.size()));
    }
    return result;
}

M3GPreparedFrame prepare_m3g_frame(const std::vector<M3GRenderInstance>& instances,
                                 const M3GRenderView& view) {
    for(const auto dimension:view.viewport_pixels)
        require(dimension>0 && dimension<=16777216U,"invalid M3G frame viewport dimensions");
    affine(view.camera_to_world);
    M3GPreparedFrame frame;
    frame.camera_view=m3g_camera_view_matrix(view.camera_to_world);
    frame.projection_gl=m3g_camera_projection_matrix(view.projection);
    frame.metal_view_projection=m3g_camera_metal_view_projection(view.projection,view.camera_to_world);
    std::set<std::uint32_t> identities;
    std::vector<M3GDrawQueueEntry> queue;
    const auto camera_origin=origin(view.camera_to_world);
    for(std::size_t instance_index=0;instance_index<instances.size();++instance_index) {
        const auto& instance=instances[instance_index];
        require(instance.asset && instance.nodes,"M3G frame instance lacks decoded geometry or pose");
        frame.assets.push_back(instance.asset);
        require(identities.insert(instance.identity).second,"duplicate M3G frame instance namespace");
        affine(instance.parent_transform);alpha(instance.parent_alpha);
        const auto& asset=*instance.asset;
        for(const auto& reference:asset.traversal) {
            require(reference.kind==M3GDrawableKind::mesh || reference.kind==M3GDrawableKind::sprite,
                    "unknown M3G frame drawable kind");
            const bool mesh=reference.kind==M3GDrawableKind::mesh;
            require(mesh ? reference.index<asset.geometry.surfaces.size() : reference.index<asset.sprites.size(),
                    "M3G frame drawable reference is out of bounds");
            const auto node=mesh ? asset.geometry.surfaces[reference.index].mesh_object : asset.sprites[reference.index].node_object;
            require(node<=std::numeric_limits<std::uint32_t>::max(),"M3G node exceeds frame identity domain");
            const auto state=instance.nodes->find(node);
            require(state!=instance.nodes->end(),"M3G frame pose omits a drawable node");
            affine(state->second.model);alpha(state->second.effective_alpha);
            if(!instance.parent_enabled || !state->second.rendering_enabled || !(state->second.scope&view.scope))continue;
            M3GPreparedDraw draw;draw.instance=instance_index;draw.geometry_index=reference.index;
            draw.node_object=node;draw.kind=reference.kind;
            draw.model_to_world=m3g_multiply(instance.parent_transform,state->second.model);affine(draw.model_to_world);
            draw.effective_alpha=multiply(instance.parent_alpha,state->second.effective_alpha);
            std::int32_t layer;
            if(mesh) {
                const auto& surface=asset.geometry.surfaces[reference.index];
                M3GMeshQueueInput input;input.blending=surface.appearance.blending;
                input.effective_alpha=draw.effective_alpha;input.vertex_color_components=surface.vertex_color_components;
                input.default_color_alpha=surface.default_color_alpha;
                if(surface.appearance.material)input.material_diffuse_alpha=surface.appearance.material->diffuse[3];
                if(surface.appearance.texture)input.texture_image_formats[0]=surface.appearance.texture->image_format;
                draw.transparent=m3g_mesh_queue_transparent(input);layer=surface.appearance.layer;
                if(!draw.transparent)draw.grouping_key=grouping(instance.mesh_appearance_serials,surface.appearance_object);
                draw.metal_matrix=m3g_multiply(frame.metal_view_projection,draw.model_to_world);finite(draw.metal_matrix);
            } else {
                const auto& sprite=asset.sprites[reference.index];
                const auto image=asset.geometry.images.find(sprite.image_object);
                require(image!=asset.geometry.images.end(),"M3G Sprite3D frame image is absent");
                draw.transparent=sprite.appearance.blending!=68;layer=sprite.appearance.layer;
                if(!draw.transparent)draw.grouping_key=grouping(instance.sprite_appearance_identities,sprite.appearance_object);
                M3GSprite3DInput input;input.model_to_world=draw.model_to_world;
                input.camera_view=frame.camera_view;input.projection_gl=frame.projection_gl;input.projection_type=view.projection.type;
                input.viewport_pixels=view.viewport_pixels;input.image_pixels={{image->second.width,image->second.height}};
                for(std::size_t i=0;i<4;++i)input.crop_pixels[i]=static_cast<float>(sprite.crop_pixels[i]);
                input.scaled=sprite.scaled;input.appearance_present=true;
                draw.sprite_projection=project_m3g_sprite3d(input);draw.metal_matrix=identity;
                sprite_vertices(draw);
            }
            draw.queue_key=m3g_draw_queue_key(layer,draw.transparent,origin(draw.model_to_world),camera_origin);
            queue.push_back({(std::uint64_t(instance.identity)<<32U)|node,layer,draw.transparent,
                             origin(draw.model_to_world),draw.grouping_key});
            frame.draws.push_back(std::move(draw));
        }
    }
    const auto order=m3g_draw_queue_order(queue,camera_origin);
    auto unsorted=std::move(frame.draws);frame.draws.clear();frame.draws.reserve(order.size());
    for(const auto index:order)frame.draws.push_back(std::move(unsorted.at(index)));
    return frame;
}

} // namespace g1
