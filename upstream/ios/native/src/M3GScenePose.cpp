#include "g1/M3GScenePose.hpp"

#include <cmath>
#include <cstring>
#include <set>

namespace g1 {
namespace {
void require(bool condition,const char* message) {
    if(!condition)throw FormatError(message);
}
float from_bits(std::uint32_t bits) noexcept {
    float value;std::memcpy(&value,&bits,4);return value;
}
float multiply(float a,float b) noexcept { volatile float value=a*b;return value; }
bool transform_property(std::uint32_t property) noexcept {
    return property==268 || property==270 || property==275;
}
float assigned_alpha(float value) noexcept {
    // Ordered finite comparisons preserve a supplied negative zero.
    if(value<0)value=0;
    if(value>1)value=1;
    return value;
}
float assigned_camera_field(float value,std::uint8_t projection_type) noexcept {
    // The original lower condition is <=0, not max(value, epsilon).
    if(value<=0)value=from_bits(0x377ba882U);
    if(projection_type!=49 && value>=180)value=from_bits(0x4333ffffU);
    return value;
}
} // namespace

M3GScenePose::M3GScenePose(const M3GScene& scene,
    const std::map<std::size_t,M3GCameraProjection>& cameras)
    :transforms_(scene),cameras_(cameras) {
    std::set<std::size_t> camera_objects;
    for(const auto& object:scene.objects())if(object.is_node) {
        require(!object.has_alignment,"runtime M3G alignment is outside the evaluated scene path");
        local_.emplace(object.index,M3GNodeProperties{
            static_cast<float>(object.alpha_factor)/255.0F,
            object.rendering_enabled,object.scope});
        parents_.emplace(object.index,object.parent);
        if(object.type==5)camera_objects.insert(object.index);
    }
    require(camera_objects.size()==cameras_.size(),"M3G pose camera set does not match the scene");
    for(const auto& entry:cameras_) {
        require(camera_objects.count(entry.first) && entry.first==entry.second.object,
                "M3G pose camera references a different scene object");
        for(const auto value:entry.second.parameters)
            require(std::isfinite(value),"nonfinite M3G pose camera parameter");
        (void)m3g_camera_projection_matrix(entry.second);
    }
}

M3GScenePose::M3GScenePose(const Bytes& bytes)
    :M3GScenePose(M3GScene(bytes),decode_m3g_cameras(bytes)) {}

void M3GScenePose::apply(const std::vector<M3GPropertySample>& samples) {
    // Build and validate the complete transformation update before invoking
    // its setter. Subsequent scalar assignments do not allocate or fail.
    std::vector<M3GPropertySample> transforms;
    for(const auto& sample:samples) {
        require(local_.count(sample.target),"M3G scene pose target is not a node");
        const auto property=sample.property;
        require((property==268 && sample.value.size()==4) ||
                ((property==270 || property==275) && sample.value.size()==3) ||
                ((property==256 || property==264 || property==276) && sample.value.size()==1),
                "unsupported M3G scene pose property or component count");
        require(property!=264 || cameras_.count(sample.target),
                "M3G field-of-view target is not a camera");
        for(const auto value:sample.value)
            require(std::isfinite(value),"nonfinite M3G scene pose assignment");
        if(transform_property(property))transforms.push_back(sample);
    }
    transforms_.apply(transforms);
    for(const auto& sample:samples) {
        if(sample.property==256)local_.at(sample.target).alpha=assigned_alpha(sample.value[0]);
        else if(sample.property==276)local_.at(sample.target).rendering_enabled=sample.value[0]>=0.5F;
        else if(sample.property==264) {
            auto& camera=cameras_.at(sample.target);
            // GENERIC keeps its matrix and projection type. parameters[0]
            // retains its updated stored FOV even though GENERIC does not use
            // that field to construct its projection matrix.
            camera.parameters[0]=assigned_camera_field(sample.value[0],camera.type);
        }
    }
}

void M3GScenePose::set_general_matrix(std::size_t target,const M3GMatrix& matrix) {
    transforms_.set_general_matrix(target,matrix);
}
void M3GScenePose::set_translation(std::size_t target,const std::array<float,3>& value) {
    transforms_.set_translation(target,value);
}
void M3GScenePose::translate(std::size_t target,const std::array<float,3>& value) {
    transforms_.translate(target,value);
}
std::array<float,3> M3GScenePose::translation(std::size_t target) const {
    return transforms_.translation(target);
}
void M3GScenePose::set_rendering_enabled(std::size_t target,bool enabled) {
    const auto found=local_.find(target);require(found!=local_.end(),"unknown M3G rendering-enable target");
    found->second.rendering_enabled=enabled;
}
void M3GScenePose::detach(std::size_t target) {
    const auto found=parents_.find(target);require(found!=parents_.end(),"unknown M3G detach target");
    transforms_.detach(target);found->second.reset();
}

std::map<std::size_t,M3GEvaluatedNode> M3GScenePose::evaluated_nodes() const {
    const auto matrices=transforms_.world_matrices();
    std::map<std::size_t,M3GEvaluatedNode> result;
    for(const auto& entry:local_) {
        if(result.count(entry.first))continue;
        std::vector<std::size_t> chain;std::set<std::size_t> visiting;
        auto index=entry.first;
        while(!result.count(index)) {
            require(visiting.insert(index).second,"cyclic M3G evaluated hierarchy");
            const auto found=parents_.find(index);
            require(found!=parents_.end(),"missing M3G evaluated ancestor");
            chain.push_back(index);
            if(!found->second)break;
            index=*found->second;
        }
        for(auto i=chain.rbegin();i!=chain.rend();++i) {
            const auto& state=local_.at(*i);
            M3GEvaluatedNode value{matrices.at(*i),state.alpha,state.rendering_enabled,state.scope};
            for(const auto component:value.model)
                require(std::isfinite(component),"nonfinite M3G evaluated matrix");
            if(const auto parent=parents_.at(*i)) {
                const auto& inherited=result.at(*parent);
                value.effective_alpha=multiply(inherited.effective_alpha,state.alpha);
                value.rendering_enabled=inherited.rendering_enabled && state.rendering_enabled;
            }
            result.emplace(*i,value);
        }
    }
    return result;
}

} // namespace g1
