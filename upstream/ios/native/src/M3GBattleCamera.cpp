#include "g1/M3GBattleCamera.hpp"

#include <set>

namespace g1 {
namespace {
void require(bool condition,const char* message) {
    if(!condition)throw FormatError(message);
}
void validate(const M3GBattleCameraAncestry& ancestry) {
    for(const auto& node:{ancestry.camera,ancestry.parent,ancestry.grandparent})
        require(!node || node->object!=0,"M3G battle camera has a null native object handle");
    require(!ancestry.parent || bool(ancestry.camera),"M3G camera parent lacks a Camera");
    require(!ancestry.grandparent || bool(ancestry.parent),"M3G camera grandparent lacks a parent");
    require(!ancestry.parent || ancestry.parent!=ancestry.camera,"M3G camera is its own parent");
    require(!ancestry.grandparent || (ancestry.grandparent!=ancestry.camera && ancestry.grandparent!=ancestry.parent),
            "M3G camera ancestry contains a cycle");
}
void validate_joint(const M3GBattleCameraAncestry& current,
                    const M3GBattleCameraAncestry& selected) {
    using Key=std::pair<std::uint32_t,std::size_t>;
    const auto key=[](const M3GBattleCameraNode& n) { return Key{n.instance,n.object}; };
    std::map<Key,std::optional<M3GBattleCameraNode>> edges;
    const auto edge=[&](const std::optional<M3GBattleCameraNode>& node,
                        const std::optional<M3GBattleCameraNode>& parent) {
        if(!node)return;
        const auto inserted=edges.emplace(key(*node),parent);
        require(inserted.second || inserted.first->second==parent,
                "M3G camera ancestry inputs declare conflicting parents");
    };
    for(const auto* ancestry:{&current,&selected}) {
        edge(ancestry->camera,ancestry->parent);edge(ancestry->parent,ancestry->grandparent);
    }
    for(const auto& entry:edges) {
        std::set<Key> visited;
        auto node=entry.first;
        for(;;) {
            require(visited.insert(node).second,"M3G combined camera ancestry is cyclic");
            const auto found=edges.find(node);
            if(found==edges.end() || !found->second)break;
            node=key(*found->second);
        }
    }
}
std::int32_t clock(M3GBattleCameraState state,const std::array<std::int32_t,2>& clocks) noexcept {
    return state.has_model_camera ? clocks[state.side_a ? 0U : 1U] : 0;
}
void propagate(std::vector<M3GBattleCameraEffect>& effects,
    const M3GBattleCameraAncestry& ancestry,std::int32_t time,
    std::uint8_t adjustment_flag,bool has_model_camera) {
    if(!ancestry.camera || !ancestry.parent)return;
    const auto target=ancestry.grandparent ? *ancestry.grandparent : *ancestry.parent;
    effects.push_back({M3GBattleCameraEffectKind::animate,target,time,{}});
    if(adjustment_flag!=0 && !has_model_camera)
        effects.push_back({M3GBattleCameraEffectKind::translate,*ancestry.parent,0,{{0,0,2}}});
}
} // namespace

M3GBattleCameraRoute m3g_battle_camera_route(const M3GBattleCameraRequest& request) {
    require(request.camera_id<=12,"unsupported M3G battle camera ID");
    M3GBattleCameraRoute route;
    const auto id=request.camera_id;
    route.side_a=id<=2 || id==6 || id==7 || id==9 || id==11;
    const auto side=route.side_a ? 0U : 1U;
    if(id!=8)
        route.preferred_model_camera=(id==6 || id==7 || request.forms[side]==0)
            ? M3GModelCameraKind::robot : M3GModelCameraKind::vehicle;
    switch(id) {
    case 0:route.scene_camera_name=request.forms[0]==0 ? "cam_auto_takehit" : "cam_auto_car";break;
    case 1:route.scene_camera_name="cam_auto_shoot";break;
    case 2:route.scene_camera_name=request.forms[0]==0 ? "cam_auto_death" : "cam_auto_car";break;
    case 3:route.scene_camera_name="cam_decpt_takehit";break;
    case 4:route.scene_camera_name="cam_decpt_shoot";break;
    case 5:route.scene_camera_name="cam_decpt_death";break;
    case 6:route.scene_camera_name="Stat_Camera";break;
    case 7:route.scene_camera_name="d_Stat_Camera";break;
    case 8:route.scene_camera_name="cam_rot";break;
    case 9:route.scene_camera_name="cam_auto_setup";break;
    case 10:route.scene_camera_name="cam_decpt_setup";break;
    case 11:route.scene_camera_name=request.forms[0]==0 ? "cam_auto_zoom" : "cam_auto_car";break;
    case 12:route.scene_camera_name="cam_decpt_zoom";break;
    default:throw FormatError("unsupported M3G battle camera ID");
    }
    if(id<=5 || id>=9)route.adjustment_flag=request.adjustment_flags[side];
    route.scene_camera_user_id=m3g_name_hash(route.scene_camera_name);
    return route;
}

std::vector<M3GBattleCameraEffect> m3g_battle_camera_propagation(
    const M3GBattleCameraAncestry& ancestry,std::int32_t time,
    std::uint8_t adjustment_flag,bool has_model_camera) {
    validate(ancestry);
    std::vector<M3GBattleCameraEffect> result;
    propagate(result,ancestry,time,adjustment_flag,has_model_camera);
    return result;
}

M3GBattleCameraSwitch m3g_battle_camera_switch(
    const M3GBattleCameraRequest& request,M3GBattleCameraState previous,
    bool model_camera_found,const std::array<std::int32_t,2>& model_clocks,
    const M3GBattleCameraAncestry& current,const M3GBattleCameraAncestry& selected) {
    M3GBattleCameraSwitch result;result.route=m3g_battle_camera_route(request);
    validate(current);validate(selected);validate_joint(current,selected);
    require(bool(selected.camera),"M3G battle selection has no actual Camera");
    require(!model_camera_found || bool(result.route.preferred_model_camera),
            "M3G battle camera ID bypasses model lookup");
    result.state={model_camera_found,result.route.side_a};
    result.before_time=clock(previous,model_clocks);
    result.after_time=clock(result.state,model_clocks);
    propagate(result.effects,current,result.before_time,result.route.adjustment_flag,previous.has_model_camera);
    result.effects.push_back({M3GBattleCameraEffectKind::set_active_camera,*selected.camera,0,{}});
    propagate(result.effects,selected,result.after_time,result.route.adjustment_flag,model_camera_found);
    return result;
}

} // namespace g1
