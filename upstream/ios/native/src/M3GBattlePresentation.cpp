#include "g1/M3GBattlePresentation.hpp"

#include "g1/M3GBattleCamera.hpp"
#include "g1/M3GBattleController.hpp"
#include "g1/M3GMaterialOverrides.hpp"

#include <algorithm>
#include <cmath>
#include <cstring>
#include <limits>
#include <set>

namespace g1 {
namespace {
using Node=M3GBattleCameraNode;
constexpr M3GMatrix identity{{1,0,0,0,0,1,0,0,0,0,1,0,0,0,0,1}};
constexpr std::uint32_t resource_key=0xf4e02223U;
constexpr std::array<std::uint32_t,24> model_resources{{408,699,444,492,1300,561,369,565,
    634,395,475,867,1039,631,795,967,564,1299,829,1201,1070,489,490,500}};
constexpr std::array<std::uint32_t,3> scene_resources{{486,534,1212}};
constexpr std::array<std::uint32_t,4> effect_resources{{1050,511,382,969}};
void require(bool condition,const char* message) { if(!condition)throw FormatError(message); }
float bits(std::uint32_t value) noexcept { float out;std::memcpy(&out,&value,4);return out; }
float addf(float a,float b) noexcept { volatile float value=a+b;return value; }
float mulf(float a,float b) noexcept { volatile float value=a*b;return value; }
Bytes resource(const Archive& archive,std::uint32_t id) {
    const auto found=archive.find(id,resource_key);
    require(found.size()==1,"required original battle resource is missing or ambiguous");
    return archive.read(found.front());
}
std::uint32_t u32(const Bytes& bytes,std::size_t at) {
    require(at<=bytes.size() && bytes.size()-at>=4,"truncated battle metadata");
    return std::uint32_t(bytes[at])|(std::uint32_t(bytes[at+1])<<8U)|
        (std::uint32_t(bytes[at+2])<<16U)|(std::uint32_t(bytes[at+3])<<24U);
}
struct WorldInfo {
    std::size_t world{},camera{},background{};
    std::array<float,4> clear{};
    bool clear_color{true},clear_depth{true};
};
WorldInfo world_info(const Bytes& bytes,const M3GScene& scene) {
    WorldInfo result;
    for(const auto& object:scene.objects())if(object.type==22) {
        require(!result.world,"battle resource has multiple serialized Worlds");result.world=object.index;
    }
    require(result.world!=0,"battle resource has no serialized World");
    const M3GFile file(bytes);
    for(const auto& object:file.objects())if(object.index==result.world) {
        require(object.payload_size>=8,"invalid validated World body");
        const auto& section=file.sections().at(object.section);
        result.camera=u32(section,object.payload_offset+object.payload_size-8);
        result.background=u32(section,object.payload_offset+object.payload_size-4);
    }
    if(!result.background)return result;
    const auto found=std::find_if(file.objects().begin(),file.objects().end(),[&](const auto& o) {
        return o.index==result.background;
    });
    require(found!=file.objects().end() && found->type==4,"invalid battle World background");
    const auto& data=file.sections().at(found->section);std::size_t at=found->payload_offset;
    const auto end=at+found->payload_size;
    const auto read=[&]() { require(at<=end && end-at>=4,"truncated background metadata");const auto v=u32(data,at);at+=4;return v; };
    (void)read();const auto tracks=read();require(tracks<=(end-at)/4,"unbounded background bindings");at+=std::size_t(tracks)*4;
    const auto parameters=read();require(parameters<=(end-at)/8,"unbounded background parameters");
    for(std::uint32_t n=0;n<parameters;++n) { (void)read();const auto size=read();require(size<=end-at,"background parameter bounds");at+=size; }
    require(end-at==28,"unexpected validated background layout");
    std::array<std::uint8_t,4> rgba{};for(auto& c:rgba)c=data.at(at++);
    require(read()==0,"battle background images are outside the proven solid-clear path");
    at+=2+16;result.clear_color=data.at(at++)!=0;result.clear_depth=data.at(at++)!=0;
    require(at==end,"trailing background bytes");
    if(result.clear_color)for(std::size_t i=0;i<4;++i) {
        auto value=mulf(static_cast<float>(rgba[i]),bits(0x3b808081U));
        value=addf(mulf(value,65536.0F),0.5F);
        result.clear[i]=static_cast<float>(static_cast<std::int32_t>(value))/65536.0F;
    }
    return result;
}
std::size_t node_index(const M3GScene& scene,std::size_t index) {
    const auto& objects=scene.objects();
    const auto found=std::find_if(objects.begin(),objects.end(),[&](const auto& o) { return o.index==index; });
    require(found!=objects.end() && found->is_node,"battle binding is not a Node");
    return static_cast<std::size_t>(found-objects.begin());
}
const M3GObjectInfo& node_info(const M3GScene& scene,std::size_t index) {
    return scene.objects().at(node_index(scene,index));
}

struct Instance {
    Bytes bytes;
    M3GScene scene;
    M3GAnimations animation;
    M3GScenePose pose;
    M3GNodeBindings bindings;
    WorldInfo source;
    std::size_t root{};
    bool wrapper{};
    M3GTransform wrapper_transform;
    std::map<std::size_t,std::int32_t> appearance_keys;
    std::map<std::size_t,std::int32_t> sample_times;
    std::set<std::size_t> explicit_translations;
    explicit Instance(Bytes input):bytes(std::move(input)),scene(bytes),animation(bytes),
        pose(scene,decode_m3g_cameras(bytes)),bindings(scene),source(world_info(bytes,scene)),root(source.world),
        appearance_keys(m3g_mesh_appearance_order_keys(scene)) {}
};
struct Snapshot {
    M3GScenePose pose;
    std::map<std::size_t,std::int32_t> sample_times;
};
} // namespace

struct M3GBattlePresentation::Impl {
    const Archive& archive;
    M3GBattleRequest request;
    std::array<CombatUnitState,2> units;
    std::vector<M3GBattleAsset> assets;
    std::vector<std::unique_ptr<Instance>> instances;
    M3GBattleControllerConfig config;
    M3GBattleControllerState state;
    std::vector<MissionAudioRequest> audio;
    std::optional<Node> active_camera;
    M3GBattleCameraState camera_state;
    std::optional<std::int32_t> damage_text;
    std::uint64_t revision{1};
    std::vector<Snapshot> previous;
    bool cut{true};

    explicit Impl(const Archive& source,M3GBattleRequest input):archive(source),request(std::move(input)) {
        require(request.token && request.terrain_style<scene_resources.size(),"invalid native battle request identity or style");
        require(request.attacker_before.type<23 && request.defender_before.type<23 &&
                request.attacker_before.type!=request.defender_before.type,"battle roster is not the original distinct-type pair");
        require(request.attacker_before.form<=1 && request.defender_before.form<=1,"unsupported battle unit form");
        require(request.exchange.attack.health_before==request.defender_before.health,
                "battle outcome does not match the defender snapshot");
        if(request.exchange.counterattack)
            require(request.exchange.counterattack->health_before==request.attacker_before.health,
                    "battle counterattack does not match attacker snapshot");
        config.a_first=request.attacker_before.faction==0;
        units[config.a_first ? 0 : 1]=request.attacker_before;
        units[config.a_first ? 1 : 0]=request.defender_before;
        config.counter=request.exchange.counterattack.has_value();
        config.final_health[config.a_first ? 1 : 0]=request.exchange.attack.health_after;
        config.damage[config.a_first ? 1 : 0]=request.exchange.attack.rolled_damage;
        config.final_health[config.a_first ? 0 : 1]=request.exchange.counterattack ?
            request.exchange.counterattack->health_after : request.attacker_before.health;
        config.damage[config.a_first ? 0 : 1]=request.exchange.counterattack ?
            request.exchange.counterattack->rolled_damage : 0;
        for(std::size_t i=0;i<2;++i) {
            config.forms[i]=units[i].form;config.factions[i]=units[i].faction;config.immune[i]=units[i].damage_immune;
            state.display_health[i]=units[i].health;
            config.dead[i]=std::any_of(request.exchange.destroyed.begin(),request.exchange.destroyed.end(),
                [&](const auto& d) { return d.type==units[i].type; });
        }
        config.intro=units[0].health==100 && units[1].health==100 && units[1].form==0;
        state.slide_fixed16=-(config.logical_width*131072);
        add_scene();
        for(std::size_t i=0;i<2;++i)add_model(i);
        for(std::uint8_t id=0;id<13;++id) {
            const auto route=m3g_battle_camera_route({id,config.forms,config.adjustments});
            if(route.preferred_model_camera)
                config.embedded_camera[id]=instances.at(route.side_a ? 1 : 2)->bindings.model_camera(
                    instances.at(route.side_a ? 1 : 2)->root,*route.preferred_model_camera).has_value();
        }
        std::uint8_t initial;
        if(config.forms[0]==1 || (!config.a_first && !config.counter))initial=10;
        else if(config.a_first && !config.counter)initial=9;
        else if(config.intro)initial=8;
        else initial=config.a_first ? 1 : 4;
        select_camera(initial);
        for(std::size_t i=0;i<4;++i)add_effect(i);
        add_death();
        if(units[0].health==100 && units[1].health==100)audio.push_back({5,23});
        state.camera_side_a=camera_state.side_a;state.embedded_current=camera_state.has_model_camera;
        remember();cut=true;
    }

    Node root(std::size_t instance) const {
        return {static_cast<std::uint32_t>(instance),instances.at(instance)->wrapper ? 0 : instances.at(instance)->root};
    }
    Node world() const { return {0,instances.front()->source.world}; }
    std::optional<Node> parent(Node node) const {
        const auto& instance=*instances.at(node.instance);
        if(node.instance!=0 && node.object==0) { require(instance.wrapper,"unexpected synthetic battle node");return world(); }
        if(node.instance!=0 && node.object==instance.root)return instance.wrapper ? Node{node.instance,0} : world();
        const auto& info=node_info(instance.scene,node.object);
        return info.parent ? std::optional<Node>{{node.instance,*info.parent}} : std::nullopt;
    }
    std::vector<Node> children(Node node) const {
        const auto& instance=*instances.at(node.instance);std::vector<Node> result;
        if(node.object==0) { require(instance.wrapper,"unexpected synthetic battle Group");return {{node.instance,instance.root}}; }
        for(const auto child:node_info(instance.scene,node.object).children) {
            const Node h{node.instance,child};if(parent(h)==std::optional<Node>{node})result.push_back(h);
        }
        if(node==world())for(std::size_t i=1;i<instances.size();++i)result.push_back(root(i));
        return result;
    }
    M3GBattleCameraAncestry ancestry(std::optional<Node> camera) const {
        M3GBattleCameraAncestry result;result.camera=camera;
        if(camera) { result.parent=parent(*camera);if(result.parent)result.grandparent=parent(*result.parent); }
        return result;
    }
    std::vector<std::map<std::size_t,M3GEvaluatedNode>> evaluated(const std::vector<M3GScenePose>& poses) const {
        std::vector<std::map<std::size_t,M3GEvaluatedNode>> result;result.reserve(poses.size());
        for(const auto& pose:poses)result.push_back(pose.evaluated_nodes());
        return result;
    }
    M3GMatrix matrix(Node node,const std::vector<std::map<std::size_t,M3GEvaluatedNode>>& values) const {
        const auto& instance=*instances.at(node.instance);
        if(node.instance==0)return values[0].at(node.object).model;
        const auto& scene_world=values[0].at(world().object).model;
        const auto parent_matrix=instance.wrapper ? m3g_multiply(scene_world,m3g_transform_matrix(instance.wrapper_transform)) : scene_world;
        return node.object==0 ? parent_matrix : m3g_multiply(parent_matrix,values[node.instance].at(node.object).model);
    }
    std::vector<M3GScenePose> poses() const {
        std::vector<M3GScenePose> result;result.reserve(instances.size());
        for(const auto& instance:instances)result.push_back(instance->pose);
        return result;
    }
    M3GMatrix relative_to_world(Node node) const {
        const auto values=evaluated(poses());const auto target=matrix(world(),values);
        return m3g_multiply(m3g_camera_view_matrix(target),matrix(node,values));
    }
    void animate(Node node,std::int32_t time) {
        std::vector<std::set<std::size_t>> targets(instances.size());std::vector<Node> pending{node};
        std::set<std::pair<std::uint32_t,std::size_t>> seen;
        while(!pending.empty()) {
            const auto current=pending.back();pending.pop_back();
            require(seen.emplace(current.instance,current.object).second,"cyclic native battle attachment graph");
            if(current.object)targets.at(current.instance).insert(current.object);
            const auto child_list=children(current);pending.insert(pending.end(),child_list.begin(),child_list.end());
        }
        for(std::size_t i=0;i<targets.size();++i)if(!targets[i].empty()) {
            auto& instance=*instances[i];const auto samples=instance.animation.sample_targets(time,targets[i]);
            instance.pose.apply(samples);
            for(const auto& sample:samples)instance.sample_times[sample.target]=time;
        }
    }
    void apply_camera(const std::vector<M3GBattleCameraEffect>& effects) {
        for(const auto& effect:effects)switch(effect.kind) {
        case M3GBattleCameraEffectKind::animate:animate(effect.node,effect.time);break;
        case M3GBattleCameraEffectKind::set_active_camera:active_camera=effect.node;break;
        case M3GBattleCameraEffectKind::translate: {
            require(effect.node.object!=0,"camera cannot translate an effect wrapper in this graph");
            auto& instance=*instances.at(effect.node.instance);instance.pose.translate(effect.node.object,effect.translation);
            instance.explicit_translations.insert(effect.node.object);break;
        }
        }
    }
    void select_camera(std::uint8_t id) {
        const M3GBattleCameraRequest asked{id,config.forms,config.adjustments};
        const auto route=m3g_battle_camera_route(asked);
        std::optional<Node> selected;bool embedded=false;
        if(route.preferred_model_camera) {
            const auto index=route.side_a ? 1U : 2U;const auto& instance=*instances.at(index);
            if(const auto found=instance.bindings.model_camera(instance.root,*route.preferred_model_camera)) {
                selected=Node{index,found->camera};embedded=true;
            }
        }
        if(!selected) {
            const auto& instance=*instances.front();
            const auto found=instance.bindings.find_node(instance.root,route.scene_camera_user_id);
            require(found && node_info(instance.scene,*found).type==5,"required named battle scene camera is absent or malformed");
            selected=Node{0,*found};
        }
        const auto change=m3g_battle_camera_switch(asked,camera_state,embedded,state.clocks,
            ancestry(active_camera),ancestry(selected));
        apply_camera(change.effects);camera_state=change.state;cut=true;
    }
    void propagate(std::int32_t time,std::uint8_t adjustment) {
        apply_camera(m3g_battle_camera_propagation(ancestry(active_camera),time,adjustment,camera_state.has_model_camera));
    }
    void remember() {
        previous.clear();previous.reserve(instances.size());
        for(const auto& instance:instances)previous.push_back({instance->pose,instance->sample_times});
        for(auto& instance:instances)instance->explicit_translations.clear();
    }

    void push(std::unique_ptr<Instance> instance,std::uint32_t id) {
        M3GBattleAsset asset{id,instance->root,instance->scene.render_asset(instance->root)};
        assets.push_back(std::move(asset));instances.push_back(std::move(instance));
    }
    void add_scene() {
        const auto id=scene_resources.at(request.terrain_style);
        auto instance=std::make_unique<Instance>(resource(archive,id));
        require(instance->source.camera && node_info(instance->scene,instance->source.camera).type==5,
                "battle World has no valid authored active camera");
        active_camera=Node{0,instance->source.camera};push(std::move(instance),id);
    }
    void add_model(std::size_t side) {
        const auto requested=units[side].type;
        const auto effective=requested==17 && side==1 ? 23U : unsigned(requested);
        const auto id=model_resources.at(effective);
        auto instance=std::make_unique<Instance>(resource(archive,id));
        std::vector<std::size_t> child_ids;
        for(const auto child:node_info(instance->scene,instance->source.world).children)child_ids.push_back(child);
        instance->root=instance->bindings.select_loaded_container_group(child_ids);
        instance->pose.detach(instance->root);
        const auto position=instances.size();push(std::move(instance),id);
        auto& attached=*instances[position];
        const auto plan=M3GMaterialBindings(attached.bytes).plan(attached.root,effective);
        for(const auto& replacement:plan) {
            auto image=load_m3g_material_image(archive,replacement.resource_id);
            if(!replacement.texture_object)continue; // Proven image-load-only branch, not a fabricated texture.
            auto& asset=assets[position].render;
            std::size_t image_key=attached.scene.objects().size()+1;
            if(!asset.geometry.images.empty())image_key=std::max(image_key,asset.geometry.images.rbegin()->first+1);
            asset.geometry.images.emplace(image_key,std::move(image.image));
            for(auto& surface:asset.geometry.surfaces)
                if(surface.appearance.texture && surface.appearance.texture->texture_object==*replacement.texture_object) {
                    surface.appearance.texture->image_object=image_key;
                    surface.appearance.texture->image_format=image.image_format;
                }
        }
        const auto clock=effective==units[0].type ? 0U : 1U;
        const auto start=start_m3g_battle_clip(1);state.clocks[clock]=start.stored_clock_ms;
        animate(root(position),start.animation_time_ms);
        attached.pose.set_rendering_enabled(attached.root,true);
        const auto forms=attached.bindings.form_nodes(attached.root);
        if(forms.robot)attached.pose.set_rendering_enabled(*forms.robot,units[side].form==0);
        if(forms.car)attached.pose.set_rendering_enabled(*forms.car,units[side].form!=0);
        const auto anchor=instances[0]->bindings.find_node(world().object,
            m3g_transform_name_hash(side ? "decepticon" : "autobot"));
        require(anchor.has_value(),"required authored battle placement anchor is absent");
        attached.pose.set_general_matrix(attached.root,relative_to_world({0,*anchor}));
        if(const auto head=attached.bindings.find_node(attached.root,m3g_transform_name_hash("Head"))) {
            const auto transform=relative_to_world({static_cast<std::uint32_t>(position),*head});
            config.adjustments[side]=addf(transform[14],bits(0x3ed682b4U))>2.0F ? 1 : 0;
        }
    }
    void add_effect(std::size_t slot) {
        const auto id=effect_resources.at(slot);auto instance=std::make_unique<Instance>(resource(archive,id));
        const auto found=instance->bindings.find_node(instance->source.world,m3g_transform_name_hash("shot_pos"));
        require(found && (node_info(instance->scene,*found).type==9 || node_info(instance->scene,*found).type==22),
                "original shot effect has no named Group attachment");
        instance->root=*found;instance->pose.detach(instance->root);instance->wrapper=true;
        push(std::move(instance),id);
    }
    void add_death() {
        auto instance=std::make_unique<Instance>(resource(archive,920));std::vector<std::size_t> child_ids;
        for(const auto child:node_info(instance->scene,instance->source.world).children)child_ids.push_back(child);
        instance->root=instance->bindings.select_loaded_container_group(child_ids);instance->pose.detach(instance->root);
        instance->pose.set_rendering_enabled(instance->root,false);push(std::move(instance),920);
        const auto side=config.dead[0] ? 0U : 1U;const auto& actor=*instances.at(side+1);
        const auto attachment=actor.bindings.find_node(actor.root,m3g_transform_name_hash(
            config.forms[side] ? "smoke_pos_car" : "smoke_pos_robot"));
        const auto placement=attachment ? relative_to_world({side+1,*attachment}) : identity;
        instances.back()->pose.set_general_matrix(instances.back()->root,placement);
    }
    void place_effect(bool side_a,bool hit) {
        const auto side=side_a ? 0U : 1U;const auto& actor=*instances.at(side+1);
        const auto name=hit ? (config.forms[side] ? "hit_pos_car" : "hit_pos_robot") : "shot_pos";
        const auto attachment=actor.bindings.find_node(actor.root,m3g_transform_name_hash(name));
        if(!attachment)return; // Original missing-name branch preserves the existing generic transform.
        const auto slot=hit ? (side_a ? 3U : 1U) : (side_a ? 0U : 2U);
        instances.at(slot+3)->wrapper_transform.general=relative_to_world({side+1,*attachment});cut=true;
    }
    void execute(const M3GBattleEvent& event) {
        switch(event.kind) {
        case M3GBattleEventKind::animate:animate(root(std::size_t(event.target)+1),event.time);break;
        case M3GBattleEventKind::start_clip:cut=true;break; // Numeric clock/start work already performed by controller.
        case M3GBattleEventKind::tick_clip:break; // Its following animate event is the pose operation.
        case M3GBattleEventKind::select_camera:select_camera(static_cast<std::uint8_t>(event.id));break;
        case M3GBattleEventKind::propagate_camera:propagate(event.time,static_cast<std::uint8_t>(event.adjust));break;
        case M3GBattleEventKind::place_effect:place_effect(event.side_a,event.hit);break;
        case M3GBattleEventKind::clear_text:if(event.field==0x10)damage_text.reset();break;
        case M3GBattleEventKind::set_integer_text:if(event.field==0x10)damage_text=event.value;break;
        case M3GBattleEventKind::audio:audio.push_back({event.operation,event.argument});break;
        }
    }
    void advance(std::int32_t delta) {
        if(state.complete)return;
        remember();cut=false;
        const auto step=advance_m3g_battle_controller(config,state,delta);
        // Camera switch uses clocks at its actual event position. Replay the
        // numeric clip stores in event order as well as the visible effects.
        for(const auto& event:step.events) {
            if(event.kind==M3GBattleEventKind::start_clip)
                state.clocks.at(static_cast<std::size_t>(event.clock))=start_m3g_battle_clip(static_cast<std::uint8_t>(event.clip)).stored_clock_ms;
            else if(event.kind==M3GBattleEventKind::tick_clip) {
                auto& clock=state.clocks.at(static_cast<std::size_t>(event.clock));
                clock=advance_m3g_battle_clip(static_cast<std::uint8_t>(event.clip),clock,event.delta,event.restart).stored_clock_ms;
            }
            execute(event);
        }
        state=step.state;
        require(state.embedded_current==camera_state.has_model_camera,"native camera/controller binding disagreement");
        camera_state.side_a=state.camera_side_a;
        ++revision;
    }

    std::vector<M3GScenePose> render_poses(float blend) const {
        require(std::isfinite(blend) && blend>=0 && blend<=1,"invalid battle presentation interpolation");
        if(cut || state.complete || previous.size()!=instances.size() || blend==1)return poses();
        std::vector<M3GScenePose> output;output.reserve(instances.size());
        for(std::size_t i=0;i<instances.size();++i) {
            const auto& current=*instances[i];const auto& prior=previous[i];output.push_back(prior.pose);
            for(const auto& sampled:current.sample_times) {
                const auto before=prior.sample_times.find(sampled.first);
                if(before==prior.sample_times.end()) {
                    output.back().apply(current.animation.sample_targets(sampled.second,{sampled.first}));continue;
                }
                // Integer-millisecond resampling is a read-only native display
                // policy. No controller transition, audio or random word runs.
                const double t=double(before->second)+(double(sampled.second)-before->second)*blend;
                require(t>=std::numeric_limits<std::int32_t>::min() && t<=std::numeric_limits<std::int32_t>::max(),
                        "interpolated battle sample time exceeds signed32");
                output.back().apply(current.animation.sample_targets(static_cast<std::int32_t>(t),{sampled.first}));
            }
            for(const auto node:current.explicit_translations) {
                const auto a=prior.pose.translation(node),b=current.pose.translation(node);std::array<float,3> v{};
                for(std::size_t n=0;n<3;++n)v[n]=a[n]+(b[n]-a[n])*blend;
                output.back().set_translation(node,v);
            }
        }
        return output;
    }
};

M3GBattlePresentation::M3GBattlePresentation(const Archive& archive,M3GBattleRequest request)
    :impl_(std::make_unique<Impl>(archive,std::move(request))) {}
M3GBattlePresentation::~M3GBattlePresentation()=default;
const std::vector<M3GBattleAsset>& M3GBattlePresentation::assets() const noexcept { return impl_->assets; }
M3GBattleStatus M3GBattlePresentation::status() const noexcept {
    const auto& p=*impl_;M3GBattleStatus out;out.token=p.request.token;out.revision=p.revision;
    out.phase=p.state.phase;out.phase_time_ms=p.state.elapsed_ms;out.slide_fixed16=p.state.slide_fixed16;
    out.complete=p.state.complete;out.health_visible=true;out.display_health=p.state.display_health;
    out.damage_text=(p.state.phase==2 || p.state.phase==5) ? p.damage_text : std::nullopt;
    out.damage_on_left=p.config.factions[p.state.phase==2 ? 0 : 1]!=0;
    for(std::size_t i=0;i<2;++i) {
        out.types[i]=p.units[i].type;out.forms[i]=p.units[i].form;out.health_before[i]=p.units[i].health;
        out.health_after[i]=static_cast<std::uint8_t>(p.config.final_health[i]);
    }
    return out;
}
void M3GBattlePresentation::advance(std::int32_t delta) { impl_->advance(delta); }
std::vector<MissionAudioRequest> M3GBattlePresentation::take_audio_requests() {
    std::vector<MissionAudioRequest> result;result.swap(impl_->audio);return result;
}
M3GBattleFrame M3GBattlePresentation::prepare_frame(std::array<float,2> viewport,
    const std::vector<const M3GRenderAsset*>& uploaded,float blend,std::optional<float> aspect_override) const {
    const auto& p=*impl_;require(uploaded.size()==p.instances.size(),"battle GPU asset set is incomplete");
    for(std::size_t i=0;i<uploaded.size();++i)
        require(uploaded[i] && uploaded[i]->geometry.root_object==p.assets[i].render.geometry.root_object &&
                uploaded[i]->geometry.surfaces.size()==p.assets[i].render.geometry.surfaces.size() &&
                uploaded[i]->sprites.size()==p.assets[i].render.sprites.size(),"battle GPU asset shape differs from its prepared source");
    std::array<std::uint32_t,2> pixels{};
    for(std::size_t i=0;i<2;++i) {
        require(std::isfinite(viewport[i]) && viewport[i]>=1 && viewport[i]<=16384 && std::floor(viewport[i])==viewport[i],
                "invalid battle drawable dimensions");pixels[i]=static_cast<std::uint32_t>(viewport[i]);
    }
    require(p.active_camera.has_value(),"battle has no selected actual camera");
    auto poses=p.render_poses(blend);const auto plan=m3g_battle_draw_plan(p.config,p.state,true);
    for(std::size_t i=1;i<poses.size();++i)if(!p.instances[i]->wrapper)
        poses[i].set_rendering_enabled(p.instances[i]->root,plan.root_enabled[i-1]);
    const auto values=p.evaluated(poses);const auto camera=*p.active_camera;
    auto projection=poses.at(camera.instance).cameras().at(camera.object);
    if(aspect_override) {
        require(projection.type==49 || projection.type==50,"native aspect adaptation needs parallel/perspective camera");
        require(std::isfinite(*aspect_override) && *aspect_override>0,"invalid native battle aspect override");
        projection.parameters[1]=*aspect_override;
    }
    const M3GRenderView view{projection,p.matrix(camera,values),pixels,values.at(camera.instance).at(camera.object).scope};
    M3GBattleFrame output;output.token=p.request.token;output.revision=p.revision;output.complete=p.state.complete;
    output.clear_color=p.instances[0]->source.clear_color;output.clear_depth=p.instances[0]->source.clear_depth;
    output.background_color=p.instances[0]->source.clear;
    for(std::size_t i=0;i<p.instances.size();++i) {
        const auto& source=*p.instances[i];M3GRenderInstance instance;
        instance.asset=uploaded[i];instance.nodes=&values[i];instance.identity=static_cast<std::uint32_t>(i+1);
        if(i) {
            instance.parent_transform=values[0].at(p.world().object).model;
            if(source.wrapper) {
                instance.parent_transform=m3g_multiply(instance.parent_transform,m3g_transform_matrix(source.wrapper_transform));
                instance.parent_enabled=plan.root_enabled[i-1];
            }
        }
        instance.mesh_appearance_serials=&source.appearance_keys;
        // Opaque Sprite3D has one entry per node: only distinct identity/equality
        // matters at its same-node insertion boundary. No pointer rank is inferred.
        instance.sprite_appearance_identities=&source.appearance_keys;
        output.submissions.push_back({i,prepare_m3g_frame({instance},view)});
    }
    return output;
}

} // namespace g1
