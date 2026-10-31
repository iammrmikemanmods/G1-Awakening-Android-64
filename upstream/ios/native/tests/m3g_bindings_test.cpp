#include "g1/M3GBindings.hpp"
#include "m3g_bindings_fixture.hpp"

#include <functional>
#include <iostream>

namespace {
int checks=0,original_checks=0;
void check(bool condition,const char* message) {
    ++checks;if(!condition)throw std::runtime_error(message);
}
void rejects(const std::function<void()>& action,const char* message) {
    bool failed=false;try { action(); }catch(const g1::FormatError&) { failed=true; }
    check(failed,message);
}
std::string unhex(std::string_view hex) {
    const auto nibble=[](char c)->unsigned {
        if(c>='0' && c<='9')return unsigned(c-'0');
        if(c>='a' && c<='f')return unsigned(c-'a'+10);
        throw std::runtime_error("invalid fixture hex");
    };
    if(hex.size()%2)throw std::runtime_error("odd fixture hex");
    std::string out;
    for(std::size_t i=0;i<hex.size();i+=2)
        out.push_back(static_cast<char>(nibble(hex[i])*16+nibble(hex[i+1])));
    return out;
}
g1::M3GObjectInfo object(std::size_t index,std::uint8_t type,std::uint32_t user_id=0,
                       std::vector<std::uint32_t> children={}) {
    g1::M3GObjectInfo out;out.index=index;out.type=type;out.user_id=user_id;out.children=std::move(children);
    out.is_node=type==5 || type==9 || type==12 || type==14 || type==15 || type==16 || type==18 || type==22;
    return out;
}
} // namespace

int main() {
    try {
        for(const auto& row:g1_binding_test::names) {
            const auto name=unhex(row.hex);
            const auto value=row.transform_suffix ? g1::m3g_transform_name_hash(name) : g1::m3g_name_hash(name);
            ++original_checks;check(value==row.expected,"original name hash differs");
        }
        for(const auto& tree:g1_binding_test::trees) {
            std::vector<g1::M3GObjectInfo> objects;
            for(const auto& row:tree.rows)objects.push_back(object(row.index,9,row.user_id,row.children));
            const g1::M3GNodeBindings bindings(objects);
            ++original_checks;check(bindings.find_node(tree.root,tree.key)==tree.expected,"original Node lookup differs");
        }
        rejects([] { (void)g1::m3g_name_hash(std::string_view("a\0b",3)); },"embedded NUL name was silently reinterpreted");
        rejects([] { (void)g1::m3g_transform_name_hash(std::string_view("a\0b",3)); },"embedded NUL suffix name was silently reinterpreted");
        check(g1::m3g_transform_name_hash("car")!=g1::m3g_name_hash("car"),"Transform suffix was omitted");
        check(g1::m3g_transform_name_hash("carTransform")!=g1::m3g_transform_name_hash("car"),"Transform suffix was deduplicated");

        const std::vector<g1::M3GObjectInfo> candidates{object(1,9),object(2,12),object(3,9),object(4,22),object(5,3)};
        const g1::M3GNodeBindings selection(candidates);
        check(selection.select_loaded_container_group({1,3})==1,"first Group was not selected");
        check(selection.select_loaded_container_group({2,3})==3,"second-child Group fallback failed");
        check(selection.select_loaded_container_group({5,3})==3,"nonnull non-Node candidate was mistaken for a Group");
        check(selection.select_loaded_container_group({1,999})==1,"unused fallback child was inspected after a first-child Group");
        rejects([&] { (void)selection.select_loaded_container_group({}); },"empty container was accepted");
        rejects([&] { (void)selection.select_loaded_container_group({2}); },"missing fallback child was accepted");
        rejects([&] { (void)selection.select_loaded_container_group({2,5,3}); },"later compatible child was substituted");
        rejects([&] { (void)selection.select_loaded_container_group({999,3}); },"absent required child was accepted");
        rejects([&] { (void)selection.select_loaded_container_group({2,999}); },"absent second child was accepted");
        check(selection.select_loaded_container_group({4,3})==4,"proven World-to-Group first-child cast failed");
        check(selection.select_loaded_container_group({2,4})==4,"proven World-to-Group fallback cast failed");

        const auto robot_key=g1::m3g_transform_name_hash("robot");
        const auto car_key=g1::m3g_transform_name_hash("car");
        const auto robot_camera_key=g1::m3g_transform_name_hash("roboCAM");
        const auto vehicle_camera_key=g1::m3g_transform_name_hash("vehiCAM");
        std::vector<g1::M3GObjectInfo> model{
            object(100,9,7,{11,20,30,40}),object(11,9,robot_key),object(20,9,car_key,{21}),
            object(21,9,car_key),object(30,9,robot_camera_key,{31}),object(31,9,0,{32}),object(32,5),
            object(40,9,vehicle_camera_key,{41}),object(41,9,0,{42}),object(42,5)};
        model[4].rendering_enabled=false; // Lookup is independent of rendering enable.
        const g1::M3GNodeBindings bound(model);
        const auto forms=bound.form_nodes(100);
        check(forms.robot==11 && forms.car==20,"suffixed form binding or outer duplicate order changed");
        const auto robot=bound.model_camera(100,g1::M3GModelCameraKind::robot);
        check(robot && robot->transform==30 && robot->inner_group==31 && robot->camera==32,
              "exact robot camera chain failed");
        const auto vehicle=bound.model_camera(100,g1::M3GModelCameraKind::vehicle);
        check(vehicle && vehicle->transform==40 && vehicle->inner_group==41 && vehicle->camera==42,
              "exact vehicle camera chain failed");
        const g1::M3GNodeBindings world_groups({object(1,9,0,{2}),object(2,22,robot_camera_key,{3}),
                                               object(3,22,0,{4}),object(4,5)});
        const auto world_camera=world_groups.model_camera(1,g1::M3GModelCameraKind::robot);
        check(world_camera && world_camera->transform==2 && world_camera->inner_group==3 && world_camera->camera==4,
              "proven World Group interfaces were rejected in the exact camera chain");
        check(!bound.form_nodes(11).car,"outside-subtree form was substituted");
        check(!bound.model_camera(11,g1::M3GModelCameraKind::robot),"missing model camera was fabricated");
        rejects([&] { (void)bound.model_camera(100,static_cast<g1::M3GModelCameraKind>(99)); },"unknown camera kind was accepted");
        rejects([&] { (void)bound.find_node(0,7); },"null search root was accepted");
        rejects([&] { (void)bound.find_node(999,7); },"missing search root was accepted");
        rejects([&] { (void)selection.find_node(5,0); },"non-Node search root was accepted");

        const auto broken_camera=[&](std::vector<g1::M3GObjectInfo> nodes) {
            rejects([&] { (void)g1::M3GNodeBindings(nodes).model_camera(1,g1::M3GModelCameraKind::robot); },
                    "malformed found camera chain was treated as missing or substituted");
        };
        broken_camera({object(1,9,0,{2}),object(2,5,robot_camera_key)});
        broken_camera({object(1,9,0,{2}),object(2,9,robot_camera_key)});
        broken_camera({object(1,9,0,{2}),object(2,9,robot_camera_key,{3}),object(3,5)});
        broken_camera({object(1,9,0,{2}),object(2,9,robot_camera_key,{3}),object(3,9)});
        broken_camera({object(1,9,0,{2}),object(2,9,robot_camera_key,{3}),object(3,9,0,{4,5}),object(4,12),object(5,5)});

        const auto invalid_graph=[&](std::vector<g1::M3GObjectInfo> nodes) {
            rejects([&] { (void)g1::M3GNodeBindings(nodes); },"malformed Node hierarchy was accepted");
        };
        invalid_graph({});
        invalid_graph({object(0,9)});
        invalid_graph({object(1,9),object(1,9)});
        invalid_graph({object(1,9,0,{2})});
        invalid_graph({object(1,9,0,{2}),object(2,3)});
        invalid_graph({object(1,9,0,{2,2}),object(2,9)});
        invalid_graph({object(1,9,0,{3}),object(2,9,0,{3}),object(3,9)});
        invalid_graph({object(1,9,0,{1})});
        invalid_graph({object(1,9),object(2,9,0,{3}),object(3,9,0,{2})});
        invalid_graph({object(1,5,0,{2}),object(2,9)});
        auto wrong_flag=object(1,9);wrong_flag.is_node=false;invalid_graph({wrong_flag});

        auto isolated_root=object(1,9,1);isolated_root.animation_tracks={2};
        const g1::M3GNodeBindings node_only({isolated_root,object(2,9,42)});
        check(!node_only.find_node(1,42),"Node lookup incorrectly followed an arbitrary non-child reference");
        std::vector<g1::M3GObjectInfo> deep;deep.reserve(20000);
        for(std::uint32_t i=1;i<=20000;++i)
            deep.push_back(object(i,9,i==20000 ? 42U : 0U,i<20000 ? std::vector<std::uint32_t>{i+1} : std::vector<std::uint32_t>{}));
        const g1::M3GNodeBindings deep_tree(deep);
        check(deep_tree.find_node(1,42)==20000,"deep iterative Node search failed");
        std::cout<<"{\"checks\":"<<checks<<",\"original_hash_cases\":"<<g1_binding_test::names.size()
                 <<",\"original_tree_cases\":"<<g1_binding_test::trees.size()
                 <<",\"original_comparisons\":"<<original_checks
                 <<",\"contract_checks\":"<<checks-original_checks<<"}\n";
        return 0;
    }catch(const std::exception& error) { std::cerr<<"FAIL: "<<error.what()<<'\n';return 1; }
}
