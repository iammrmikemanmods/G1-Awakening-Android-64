#pragma once
#include "g1/TechSpecsModel.hpp"
#include "g1/M3GBattleTimeline.hpp"
#include "g1/TacticalMotion.hpp"
#include <limits>

namespace g1 {
// Presentation only. Original ready poses/vertex data remain immutable.
// Optional authored rigid-subtree gait and assembly transitions are evaluated
// into disposable node copies. They are not original recovered animations.
// Original Z-up asset space is converted to tactical Y-up once.
struct TacticalBounds {std::array<float,3> minimum{},maximum{};};
inline M3GMatrix tactical_placement(const TacticalBounds& b, std::uint8_t facing,
                                   float x,float y,float z) {
    if(facing>3 || !std::isfinite(x)||!std::isfinite(y)||!std::isfinite(z))
        throw FormatError("Invalid tactical model placement");
    for(int i=0;i<3;++i)if(!std::isfinite(b.minimum[i])||!std::isfinite(b.maximum[i])||b.maximum[i]<b.minimum[i])
        throw FormatError("Invalid tactical model bounds");
    const float height=b.maximum[2]-b.minimum[2];
    const float width=std::max(b.maximum[0]-b.minimum[0],b.maximum[1]-b.minimum[1]);
    if(height<=0.001F || width<=0.001F || height>1000 || width>1000)
        throw FormatError("Degenerate tactical model extent");
    // Same positive scale for every axis; retain source relative sizes until
    // the footprint limit is reached. Never widen a face or shorten limbs.
    const float s=std::min(0.055F,0.42F/width);
    // Native facing: up=0, down=1, left=2, right=3. Source faces +Y (verified from original textured meshes).
    constexpr float pi=3.14159265358979323846F;
    constexpr float angle[4]={0,pi,pi/2,-pi/2};
    const float c=std::cos(angle[facing]),q=std::sin(angle[facing]);
    const M3GMatrix yaw{{c,0,-q,0, 0,1,0,0, q,0,c,0, x,y,z,1}};
    const M3GMatrix axis{{s,0,0,0, 0,0,-s,0, 0,s,0,0, 0,0,0,1}};
    const M3GMatrix foot{{1,0,0,0, 0,1,0,0, 0,0,1,0,
        -(b.minimum[0]+b.maximum[0])*0.5F,
        -(b.minimum[1]+b.maximum[1])*0.5F,-b.minimum[2],1}};
    return m3g_multiply(m3g_multiply(yaw,axis),foot);
}
inline bool tactical_model_eligible(bool modern,bool active,bool moving,bool transforming,
                                    bool special_presentation) noexcept {
    // Walking/transform clips have not been recovered for the tactical 3D
    // path. Keep the proven original sprite animation rather than gliding a
    // frozen robot or inventing a mesh morph. This gate is intentionally safe.
    return modern && active && !moving && !transforming && !special_presentation;
}
class TacticalModel {
public:
    explicit TacticalModel(const Archive& archive,std::uint8_t type):type_(type) {
        const auto ids=archive.find(tech_specs_model_resource(type),0xf4e02223U);
        if(ids.size()!=1)throw FormatError("Tactical original model missing or ambiguous");
        bytes_=archive.read(ids.front());scene_=std::make_unique<M3GScene>(bytes_);
        M3GNodeBindings bindings(*scene_);
        const auto& objects=scene_->objects();
        const auto world=std::find_if(objects.begin(),objects.end(),[](const auto& o){return o.type==22;});
        if(world==objects.end())throw FormatError("Tactical M3G World missing");
        const auto root=bindings.select_loaded_container_group({world->children.begin(),world->children.end()});
        forms_=bindings.form_nodes(root);asset_=scene_->render_asset(root);
        if(!asset_.sprites.empty())throw FormatError("Tactical mesh has unsupported attached Sprite3D");
        appearance_=m3g_mesh_appearance_order_keys(*scene_);
        for(const auto& item:M3GMaterialBindings(bytes_).plan(root,type)) {
            auto replacement=load_m3g_material_image(archive,item.resource_id);
            if(!item.texture_object)continue;
            const auto key=std::max(scene_->objects().size()+1,
                asset_.geometry.images.empty()?std::size_t(1):asset_.geometry.images.rbegin()->first+1);
            asset_.geometry.images.emplace(key,std::move(replacement.image));
            for(auto& s:asset_.geometry.surfaces)if(s.appearance.texture && s.appearance.texture->texture_object==*item.texture_object) {
                s.appearance.texture->image_object=key;s.appearance.texture->image_format=replacement.image_format;
            }
        }
        std::set<std::size_t> targets;std::vector<std::size_t> todo{root};
        while(!todo.empty()) {
            const auto id=todo.back();todo.pop_back();
            if(!targets.insert(id).second)throw FormatError("Cyclic tactical model subtree");
            const auto found=std::find_if(objects.begin(),objects.end(),[&](const auto& o){return o.index==id;});
            if(found==objects.end())throw FormatError("Missing tactical model descendant");
            todo.insert(todo.end(),found->children.begin(),found->children.end());
        }
        M3GScenePose pose(*scene_,decode_m3g_cameras(bytes_));pose.detach(root);
        pose.apply(M3GAnimations(bytes_).sample_targets(start_m3g_battle_clip(1).animation_time_ms,targets));
        pose.set_rendering_enabled(root,true);
        for(std::uint8_t f=0;f<2;++f) {
            if((f==0 && !forms_.robot)||(f==1 && !forms_.car))continue;
            auto selected=pose;
            if(forms_.robot)selected.set_rendering_enabled(*forms_.robot,f==0);
            if(forms_.car)selected.set_rendering_enabled(*forms_.car,f==1);
            nodes_[f]=selected.evaluated_nodes();
            auto& b=bounds_[f];b.minimum.fill(std::numeric_limits<float>::max());
            b.maximum.fill(std::numeric_limits<float>::lowest());
            std::size_t count=0;
            for(const auto& surface:asset_.geometry.surfaces) {
                const auto& n=nodes_[f].at(surface.mesh_object);
                if(!n.rendering_enabled || n.effective_alpha<=0)continue;
                for(const auto& vertex:surface.vertices)for(int i=0;i<3;++i) {
                    float value=0;for(int j=0;j<4;++j)value+=n.model[j*4+i]*vertex.position[j];
                    if(!std::isfinite(value))throw FormatError("Nonfinite tactical pose");
                    b.minimum[i]=std::min(b.minimum[i],value);b.maximum[i]=std::max(b.maximum[i],value);
                    ++count;
                }
            }
            if(!count)continue;
            (void)tactical_placement(b,1,0,0,0);supported_[f]=true;
        }
        if(!supported_[0])throw FormatError("No valid original robot pose");
        // Find an authored sibling pair of long lower-limb groups. Names
        // differ between exporters (Bumblebee/Frenzy have no named pelvis).
        // Require two floor-reaching, laterally separated branches from ONE
        // parent. Ambiguous/quadruped hierarchies retain the original sprite.
        const auto collect=[&](std::size_t child) {
            std::set<std::size_t> found;std::vector<std::size_t> pending{child};
            while(!pending.empty()) {
                const auto n=pending.back();pending.pop_back();
                if(!found.insert(n).second)throw FormatError("Cyclic motion rig");
                const auto& info=objects.at(n-1);
                if(info.index!=n)throw FormatError("Unexpected motion object index");
                pending.insert(pending.end(),info.children.begin(),info.children.end());
            }
            return found;
        };
        const auto robotTree=collect(*forms_.robot);
        const auto& rb=bounds_[0];const float height=rb.maximum[2]-rb.minimum[2];
        const float center=(rb.minimum[0]+rb.maximum[0])*0.5F;
        std::vector<std::vector<Hip>> candidates;
        for(const auto& parent:objects)if(robotTree.count(parent.index)&&parent.type==9) {
            std::vector<Hip> pair;
            for(auto child:parent.children) {
                const auto& info=objects.at(child-1);if(info.index!=child||info.type!=9)continue;
                auto descendants=collect(child);
                float low=std::numeric_limits<float>::max(),high=std::numeric_limits<float>::lowest();
                for(const auto& surface:asset_.geometry.surfaces)if(descendants.count(surface.mesh_object)) {
                    const auto& n=nodes_[0].at(surface.mesh_object);if(!n.rendering_enabled)continue;
                    for(const auto& v:surface.vertices){float z=0;for(int j=0;j<4;++j)z+=n.model[j*4+2]*v.position[j];
                        low=std::min(low,z);high=std::max(high,z);}
                }
                const auto& m=nodes_[0].at(child).model;
                if(high-low>height*0.20F && high<rb.minimum[2]+height*0.65F &&
                   low<rb.minimum[2]+height*0.12F && m[14]>rb.minimum[2]+height*0.22F &&
                   m[14]<rb.minimum[2]+height*0.66F)
                    pair.push_back({{{m[12],m[13],m[14]}},std::move(descendants)});
            }
            if(pair.size()!=2)continue;
            std::sort(pair.begin(),pair.end(),[](const auto&a,const auto&b){return a.pivot[0]<b.pivot[0];});
            if(std::abs((pair[0].pivot[0]+pair[1].pivot[0])*0.5F-center)<height*0.18F &&
               pair[1].pivot[0]-pair[0].pivot[0]>height*0.08F &&
               std::abs(pair[1].pivot[2]-pair[0].pivot[2])<height*0.12F)
                candidates.push_back(std::move(pair));
        }
        if(candidates.size()==1)hips_=std::move(candidates.front());
        // Optional upper-body sibling pair. Head/face branches remain fixed.
        // A pair must be mirrored about the torso, not merely two objects.
        std::vector<std::vector<Hip>> shoulders;
        for(const auto& parent:objects)if(robotTree.count(parent.index)&&parent.type==9) {
            std::vector<Hip> pair;
            for(auto child:parent.children) {
                const auto& info=objects.at(child-1);if(info.type!=9)continue;
                const auto& m=nodes_[0].at(child).model;
                if(m[14]<rb.minimum[2]+height*.60F || m[14]>rb.minimum[2]+height*.90F)continue;
                auto descendants=collect(child);float low=1e20F,high=-1e20F;
                for(const auto& surface:asset_.geometry.surfaces)if(descendants.count(surface.mesh_object)) {
                    const auto& n=nodes_[0].at(surface.mesh_object);if(!n.rendering_enabled)continue;
                    for(const auto& v:surface.vertices){float z=0;for(int j=0;j<4;++j)z+=n.model[j*4+2]*v.position[j];
                        low=std::min(low,z);high=std::max(high,z);}
                }
                if(high-low>height*.15F && low>rb.minimum[2]+height*.20F && high>rb.minimum[2]+height*.65F)
                    pair.push_back({{{m[12],m[13],m[14]}},std::move(descendants)});
            }
            if(pair.size()!=2)continue;
            std::sort(pair.begin(),pair.end(),[](const auto&a,const auto&b){return a.pivot[0]<b.pivot[0];});
            if(pair[1].pivot[0]-pair[0].pivot[0]>height*.22F &&
               std::abs(pair[1].pivot[2]-pair[0].pivot[2])<height*.10F)shoulders.push_back(std::move(pair));
        }
        if(shoulders.size()==1)shoulders_=std::move(shoulders.front());

    }
    const M3GRenderAsset& asset() const noexcept{return asset_;}
    bool supports(std::uint8_t form) const noexcept{return form<2 && supported_[form];}
    const TacticalBounds& bounds(std::uint8_t form) const {
        if(!supports(form))throw FormatError("Original tactical form not available");return bounds_[form];
    }
    bool walking_supported(std::uint8_t form) const noexcept {return supports(form)&&(form==1||hips_.size()==2);}
    std::size_t hip_count() const noexcept{return hips_.size();}
    M3GPreparedFrame prepare(const M3GRenderAsset& uploaded,std::uint8_t form,std::uint8_t facing,
                            std::array<float,3> foot,const M3GMatrix& camera,
                            std::array<std::uint32_t,2> pixels,TacticalMotion motion={}) const {
        if(!supports(form))throw FormatError("Original tactical form not available");
        if(motion.transforming&&(!supports(0)||!supports(1)))throw FormatError("Transformation endpoint unavailable");
        if(motion.walking&&!walking_supported(form))throw FormatError("Unverified walking rig");
        if(!std::isfinite(motion.stride_phase))throw FormatError("Nonfinite gait phase");
        if(uploaded.geometry.surfaces.size()!=asset_.geometry.surfaces.size())
            throw FormatError("Tactical uploaded asset mismatch");
        auto nodes=nodes_[form];auto parent=tactical_placement(bounds_[form],facing,foot[0],foot[1],foot[2]);
        MechanicalPhase phase{};
        if(motion.transforming) {
            phase=mechanical_phase(motion.vehicle_fraction);
            nodes=nodes_[0];
            // Both authored endpoint trees are present throughout the exchange.
            for(const auto& [id,n]:nodes_[1])if(n.rendering_enabled)nodes[id]=n;
            for(auto& [id,n]:nodes)if(n.rendering_enabled) {
                const bool robot=nodes_[0].at(id).rendering_enabled;
                auto placement=tactical_placement(bounds_[robot?0:1],facing,foot[0],foot[1],foot[2]);
                if(robot) {
                    for(std::size_t k=0;k<hips_.size();++k)if(hips_[k].descendants.count(id))
                        n.model=m3g_multiply(rigid_hinge(hips_[k].pivot,-0.95F*phase.robot_fold),n.model);
                    for(std::size_t k=0;k<shoulders_.size();++k)if(shoulders_[k].descendants.count(id))
                        n.model=m3g_multiply(rigid_hinge(shoulders_[k].pivot,(k?1:-1)*.50F*phase.robot_fold,1),n.model);
                    const auto& b=bounds_[0];const float h=b.maximum[2]-b.minimum[2];
                    const std::array<float,3> pivot{{(b.minimum[0]+b.maximum[0])/2,
                        (b.minimum[1]+b.maximum[1])/2,b.minimum[2]+h*0.42F}};
                    n.model=m3g_multiply(rigid_hinge(pivot,-0.65F*phase.robot_fold),n.model);

                } else placement[13]+=0.025F*phase.vehicle_release;
                n.model=m3g_multiply(placement,n.model);
            }
            // Place each articulated endpoint on the same tactical ground.
            // No squashing or hidden lower limbs beneath the tile surface.
            std::array<float,2> floors{{std::numeric_limits<float>::max(),std::numeric_limits<float>::max()}};
            for(const auto& surface:asset_.geometry.surfaces) {
                const auto& n=nodes.at(surface.mesh_object);if(!n.rendering_enabled)continue;
                const int f=nodes_[0].at(surface.mesh_object).rendering_enabled?0:1;
                for(const auto& vertex:surface.vertices){float y=0;for(int j=0;j<4;++j)y+=n.model[j*4+1]*vertex.position[j];
                    floors[f]=std::min(floors[f],y);}
            }
            for(auto& [id,n]:nodes)if(n.rendering_enabled) {
                const int f=nodes_[0].at(id).rendering_enabled?0:1;
                if(std::isfinite(floors[f]))n.model[13]+=foot[1]-floors[f]+(f?0.025F*phase.vehicle_release:0);
            }
            parent=motion_identity();
        } else if(motion.walking && form==0) {
            const float wave=std::sin(motion.stride_phase*6.28318530718F);
            for(auto& [id,n]:nodes)for(std::size_t k=0;k<hips_.size();++k)
                if(n.rendering_enabled&&hips_[k].descendants.count(id))
                    n.model=m3g_multiply(rigid_hinge(hips_[k].pivot,(k?1:-1)*0.24F*wave),n.model);
            for(auto& [id,n]:nodes)for(std::size_t k=0;k<shoulders_.size();++k)
                if(n.rendering_enabled&&shoulders_[k].descendants.count(id))
                    n.model=m3g_multiply(rigid_hinge(shoulders_[k].pivot,(k?-1:1)*.07F*wave),n.model);
            // Contact correction uses the posed vertices, not nonuniform scaling.
            float floor=std::numeric_limits<float>::max();
            for(const auto& s:asset_.geometry.surfaces){const auto& n=nodes.at(s.mesh_object);if(!n.rendering_enabled)continue;
                for(const auto& v:s.vertices){float z=0;for(int j=0;j<4;++j)z+=n.model[j*4+2]*v.position[j];floor=std::min(floor,z);}}
            const float shift=bounds_[0].minimum[2]-floor;
            for(auto& [id,n]:nodes)n.model[14]+=shift;
        }
        M3GRenderInstance instance;instance.asset=&uploaded;instance.nodes=&nodes;instance.identity=1;
        instance.mesh_appearance_serials=&appearance_;instance.parent_transform=parent;
        M3GRenderView view{};view.viewport_pixels=pixels;view.projection.type=48;view.projection.generic=camera;
        for(int column=0;column<4;++column)view.projection.generic[column*4+2]=2*camera[column*4+2]-camera[column*4+3];
        auto frame=prepare_m3g_frame({instance},view);
        if(motion.transforming)for(auto& d:frame.draws) {
            d.assembly_role=nodes_[0].at(d.node_object).rendering_enabled?1:2;
            d.assembly_mix=phase.exchange;d.assembly_highlight=phase.highlight;
        }
        return frame;
    }
private:
    struct Hip {std::array<float,3> pivot;std::set<std::size_t> descendants;};
    std::vector<Hip> hips_,shoulders_;
    std::uint8_t type_{};Bytes bytes_;std::unique_ptr<M3GScene> scene_;
    M3GRenderAsset asset_;M3GFormNodeBindings forms_;
    std::map<std::size_t,std::int32_t> appearance_;
    std::array<std::map<std::size_t,M3GEvaluatedNode>,2> nodes_;
    std::array<TacticalBounds,2> bounds_{};std::array<bool,2> supported_{};
};
} // namespace g1
