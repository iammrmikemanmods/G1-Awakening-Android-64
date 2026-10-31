#pragma once

#include "g1/TechSpecs.hpp"
#include "g1/M3GBindings.hpp"
#include "g1/M3GMaterialOverrides.hpp"
#include "g1/M3GRenderFrame.hpp"
#include <algorithm>
#include <cmath>
#include <memory>

namespace g1 {

// Read-only presenter of an actual original M3G character model. Rendering
// never creates a synthetic GameSession battle, damage or action animation.
// Camera fitting is a new native viewer policy, NOT recovered ARM32 tech-spec
// camera behaviour. The authored geometry, materials and form bindings are
// retained and drawn by the same Metal adapter as original battle models.
class TechSpecsModel {
public:
    explicit TechSpecsModel(const Archive& archive,std::uint8_t type):type_(type) {
        const auto id=tech_specs_model_resource(type);
        const auto found=archive.find(id,0xf4e02223U);
        if(found.size()!=1)throw FormatError("Tech Specs original M3G model missing or ambiguous");
        bytes_=archive.read(found.front());
        scene_=std::make_unique<M3GScene>(bytes_);
        bindings_=std::make_unique<M3GNodeBindings>(*scene_);
        pose_=std::make_unique<M3GScenePose>(*scene_,decode_m3g_cameras(bytes_));
        const auto& objects=scene_->objects();
        const auto world=std::find_if(objects.begin(),objects.end(),[](const auto& o){return o.type==22;});
        if(world==objects.end())throw FormatError("Tech Specs original M3G World is missing");
        root_=bindings_->select_loaded_container_group({world->children.begin(),world->children.end()});
        forms_=bindings_->form_nodes(root_);
        asset_=scene_->render_asset(root_);
        for(const auto& o:M3GMaterialBindings(bytes_).plan(root_,type_)) {
            auto replacement=load_m3g_material_image(archive,o.resource_id);
            if(!o.texture_object)continue;
            std::size_t key=scene_->objects().size()+1;
            if(!asset_.geometry.images.empty())key=std::max(key,asset_.geometry.images.rbegin()->first+1);
            asset_.geometry.images.emplace(key,std::move(replacement.image));
            for(auto& surface:asset_.geometry.surfaces)if(surface.appearance.texture &&
                surface.appearance.texture->texture_object==*o.texture_object) {
                surface.appearance.texture->image_object=key;
                surface.appearance.texture->image_format=replacement.image_format;
            }
        }
        appearance_serials_=m3g_mesh_appearance_order_keys(*scene_);
        if(!asset_.geometry.has_geometry || asset_.geometry.surfaces.empty())
            throw FormatError("Original Tech Specs model has no supported renderable geometry");
        for(int axis=0;axis<3;++axis) {
            midpoint_[axis]=(asset_.geometry.minimum[axis]+asset_.geometry.maximum[axis])/2.0F;
            diameter_=std::max(diameter_,asset_.geometry.maximum[axis]-asset_.geometry.minimum[axis]);
        }
        if(!std::isfinite(diameter_) || diameter_<=0 || diameter_>1000)
            throw FormatError("Original Tech Specs M3G geometry bounds are invalid");
        set_form(0);
    }
    const M3GRenderAsset& asset() const noexcept {return asset_;}
    std::uint8_t type() const noexcept {return type_;}
    void set_form(std::uint8_t form) {
        if(form>1)throw FormatError("Original Tech Specs form is outside two-form roster");
        // Some tape and special units have only the robot visibility node.
        // Preserve that authored node when there is no car binding.
        if(forms_.robot)pose_->set_rendering_enabled(*forms_.robot,form==0 || !forms_.car);
        if(forms_.car)pose_->set_rendering_enabled(*forms_.car,form!=0);
        form_=form;
    }
    M3GPreparedFrame prepare(std::uint32_t width,std::uint32_t height,float angle=0) const {
        if(!width || !height || width>8192 || height>8192 || !std::isfinite(angle))
            throw FormatError("Invalid Tech Specs model drawable viewport or orbit");
        const auto nodes=pose_->evaluated_nodes();
        M3GRenderInstance instance;
        instance.asset=&asset_;instance.nodes=&nodes;instance.identity=1;
        instance.mesh_appearance_serials=&appearance_serials_;
        // Rotate about the actual original bounding-box centre, not an
        // arbitrary screen-space pivot. No GameSession state is changed.
        const float s=std::sin(angle),c=std::cos(angle);
        const M3GMatrix to{{1,0,0,0,0,1,0,0,0,0,1,0,
                            midpoint_[0],midpoint_[1],midpoint_[2],1}};
        const M3GMatrix from{{1,0,0,0,0,1,0,0,0,0,1,0,
                              -midpoint_[0],-midpoint_[1],-midpoint_[2],1}};
        const M3GMatrix orbit{{c,0,-s,0,0,1,0,0,s,0,c,0,0,0,0,1}};
        instance.parent_transform=m3g_multiply(m3g_multiply(to,orbit),from);
        M3GCameraProjection camera{};
        camera.type=50;camera.parameters={35.0F,float(width)/float(height),0.1F,1000.0F};
        M3GMatrix location{{1,0,0,0,0,1,0,0,0,0,1,0,
            midpoint_[0],midpoint_[1],midpoint_[2]+diameter_*2.6F,1}};
        return prepare_m3g_frame({instance},{camera,location,{width,height},0xffffffffU});
    }
private:
    std::uint8_t type_{},form_{};
    Bytes bytes_;
    std::unique_ptr<M3GScene> scene_;
    std::unique_ptr<M3GNodeBindings> bindings_;
    std::unique_ptr<M3GScenePose> pose_;
    M3GFormNodeBindings forms_;
    std::size_t root_{};
    M3GRenderAsset asset_;
    std::map<std::size_t,std::int32_t> appearance_serials_;
    std::array<float,3> midpoint_{};
    float diameter_{};
};

} // namespace g1
