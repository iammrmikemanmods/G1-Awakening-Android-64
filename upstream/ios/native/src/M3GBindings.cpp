#include "g1/M3GBindings.hpp"

#include <limits>
#include <utility>

namespace g1 {
namespace {
constexpr std::size_t maximum_objects=1000000;
void require(bool condition,const char* message) {
    if(!condition)throw FormatError(message);
}
bool node_type(std::uint8_t type) noexcept {
    switch(type) {
    case 5:case 9:case 12:case 14:case 15:case 16:case 18:case 22:return true;
    default:return false;
    }
}
bool group_type(std::uint8_t type) noexcept { return type==9 || type==22; }
void name_bytes(std::string_view name) {
    require(name.find('\0')==std::string_view::npos,"M3G name contains an embedded NUL");
}
std::uint32_t hash_reverse(std::uint32_t state,std::string_view name) noexcept {
    for(auto it=name.rbegin();it!=name.rend();++it) {
        const auto byte=static_cast<unsigned char>(*it);
        const std::int32_t signed_byte=byte<128 ? std::int32_t(byte) : std::int32_t(byte)-256;
        state=state*0x0019660dU+0x3c6ef35fU+static_cast<std::uint32_t>(signed_byte);
    }
    return state;
}
std::uint32_t hash_result(std::uint32_t state) noexcept {
    if(state&0x80000000U)state=0U-state;
    return state&0x3fffffffU;
}
} // namespace

std::uint32_t m3g_name_hash(std::string_view name) {
    name_bytes(name);return hash_result(hash_reverse(0,name));
}
std::uint32_t m3g_transform_name_hash(std::string_view name) {
    name_bytes(name);return hash_result(hash_reverse(hash_reverse(0,"Transform"),name));
}

M3GNodeBindings::M3GNodeBindings(const std::vector<M3GObjectInfo>& objects) {
    require(!objects.empty() && objects.size()<=maximum_objects,"M3G binding object inventory is empty or exceeds bound");
    std::size_t nodes=0;
    for(const auto& info:objects) {
        require(info.index!=0 && info.index<=std::numeric_limits<std::uint32_t>::max(),
                "M3G binding object ID is outside the supported domain");
        require(info.type<=22 && info.is_node==node_type(info.type),"M3G binding object type/Node metadata differs");
        require(info.children.empty() || group_type(info.type),
                "M3G binding children are present on a non-Group node");
        Object value;value.type=info.type;value.user_id=info.user_id;
        value.children.assign(info.children.begin(),info.children.end());
        require(objects_.emplace(info.index,std::move(value)).second,"duplicate M3G binding object ID");
        nodes+=info.is_node ? 1U : 0U;
    }
    std::map<std::size_t,std::size_t> parents;
    for(const auto& entry:objects_)for(const auto child:entry.second.children) {
        (void)node(child);
        require(parents.emplace(child,entry.first).second,"M3G binding node has duplicate or multiple parents");
    }
    // Kahn traversal of a single-parent forest validates every Node without
    // recursive C++ calls, including cycles outside a later selected subtree.
    std::vector<std::size_t> pending;
    for(const auto& entry:objects_)
        if(node_type(entry.second.type) && !parents.count(entry.first))pending.push_back(entry.first);
    std::size_t visited=0;
    while(!pending.empty()) {
        const auto index=pending.back();pending.pop_back();++visited;
        const auto& children=objects_.at(index).children;
        pending.insert(pending.end(),children.begin(),children.end());
    }
    require(visited==nodes,"cyclic M3G binding Node hierarchy");
}

const M3GNodeBindings::Object& M3GNodeBindings::object(std::size_t index) const {
    const auto found=objects_.find(index);
    require(found!=objects_.end(),"M3G binding object ID is absent");return found->second;
}
const M3GNodeBindings::Object& M3GNodeBindings::node(std::size_t index) const {
    const auto& value=object(index);require(node_type(value.type),"M3G binding requires a Node");return value;
}
bool M3GNodeBindings::group_candidate(std::size_t index) const {
    return group_type(object(index).type);
}

std::optional<std::size_t> M3GNodeBindings::find_node(std::size_t root,std::uint32_t user_id) const {
    (void)node(root);
    std::vector<std::size_t> pending{root};
    while(!pending.empty()) {
        const auto index=pending.back();pending.pop_back();
        const auto& value=objects_.at(index);
        if(value.user_id==user_id)return index;
        for(auto child=value.children.rbegin();child!=value.children.rend();++child)pending.push_back(*child);
    }
    return std::nullopt;
}

std::size_t M3GNodeBindings::select_loaded_container_group(const std::vector<std::size_t>& children) const {
    require(!children.empty(),"loaded M3G container has no child0");
    if(group_candidate(children[0]))return children[0];
    require(children.size()>1,"loaded M3G container fallback child1 is absent");
    require(group_candidate(children[1]),"loaded M3G container fallback child1 lacks the Group interface");
    return children[1];
}

M3GFormNodeBindings M3GNodeBindings::form_nodes(std::size_t root) const {
    return {find_node(root,m3g_transform_name_hash("robot")),
            find_node(root,m3g_transform_name_hash("car"))};
}

std::optional<M3GModelCameraBinding> M3GNodeBindings::model_camera(
    std::size_t root,M3GModelCameraKind kind) const {
    std::string_view name;
    switch(kind) {
    case M3GModelCameraKind::robot:name="roboCAM";break;
    case M3GModelCameraKind::vehicle:name="vehiCAM";break;
    default:throw FormatError("unknown M3G model camera kind");
    }
    const auto transform=find_node(root,m3g_transform_name_hash(name));
    if(!transform)return std::nullopt;
    const auto& outer=node(*transform);
    require(group_type(outer.type) && !outer.children.empty(),"M3G model camera transform lacks Group interface or child0");
    const auto inner_id=outer.children[0];const auto& inner=node(inner_id);
    require(group_type(inner.type) && !inner.children.empty(),"M3G model camera child0 lacks Group interface or child0");
    const auto camera_id=inner.children[0];
    require(node(camera_id).type==5,"M3G model camera second child0 is not Camera5");
    return M3GModelCameraBinding{*transform,inner_id,camera_id};
}

} // namespace g1
