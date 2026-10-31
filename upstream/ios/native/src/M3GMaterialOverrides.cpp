#include "g1/M3GMaterialOverrides.hpp"

#include "g1/M3GScene.hpp"

#include <algorithm>
#include <limits>
#include <set>

namespace g1 {
namespace {
constexpr std::size_t max_objects=1'000'000;
constexpr std::size_t max_references=4'000'000;
constexpr std::uint32_t material_key=500033589U,material2_key=253263228U;
constexpr std::uint32_t image_key=0xf4e02223U;
constexpr std::array<std::array<std::int32_t,2>,24> image_pairs{{
    {{-1,-1}},{{-1,-1}},{{-1,-1}},{{686,651}},{{-1,-1}},{{-1,-1}},
    {{1216,-1}},{{1255,-1}},{{-1,-1}},{{-1,-1}},{{-1,-1}},{{-1,-1}},
    {{-1,-1}},{{572,704}},{{595,790}},{{1270,1244}},{{-1,-1}},{{-1,-1}},
    {{-1,-1}},{{-1,-1}},{{796,-1}},{{-1,-1}},{{-1,-1}},{{-1,-1}}
}};
struct ImageDefinition { std::uint32_t id,width,height;std::uint8_t color_type; };
constexpr std::array<ImageDefinition,11> image_definitions{{
    {686,256,128,3},{651,256,256,3},{1216,256,256,3},{1255,256,256,3},
    {572,256,128,3},{704,256,256,3},{595,256,128,3},{790,256,256,2},
    {1270,256,128,2},{1244,512,256,2},{796,256,256,3}
}};
void require(bool condition,const char* message) {
    if (!condition) throw FormatError(message);
}
bool group(std::uint8_t type) noexcept { return type==9 || type==22; }
bool node(std::uint8_t type) noexcept {
    return type==5 || type==9 || type==12 || type==14 || type==18 || type==22;
}
const ImageDefinition& image_definition(std::uint32_t id) {
    const auto it=std::find_if(image_definitions.begin(),image_definitions.end(),
                            [id](const auto& image){return image.id==id;});
    require(it!=image_definitions.end(),"unknown original M3G material image resource");
    return *it;
}

// The complete native scene decoder validates body lengths and field semantics
// first. This bounded second pass retains reference identity which the renderer
// previously expanded into per-surface values; it does not decode pixels twice.
class Reader {
public:
    Reader(const Bytes& bytes,std::size_t offset,std::size_t size):bytes_(bytes),at_(offset) {
        require(offset<=bytes.size() && size<=bytes.size()-offset,"M3G material body range overflow");
        end_=offset+size;
    }
    std::uint8_t byte() { require(at_<end_,"truncated M3G material identity");return bytes_[at_++]; }
    std::uint32_t word() {
        std::uint32_t value=0;
        for(unsigned i=0;i<4;++i)value|=std::uint32_t(byte())<<(i*8U);
        return value;
    }
    void skip(std::size_t n) { require(n<=end_-at_,"truncated M3G material identity");at_+=n; }
    std::size_t count(std::size_t width) {
        const auto value=word();require(width!=0 && value<=(end_-at_)/width,"unbounded M3G identity array");return value;
    }
    std::vector<std::size_t> refs() {
        const auto n=count(4);std::vector<std::size_t> result;result.reserve(n);
        for(std::size_t i=0;i<n;++i)result.push_back(word());
        return result;
    }
private:
    const Bytes& bytes_;std::size_t at_,end_{};
};
void append(std::vector<std::size_t>& into,const std::vector<std::size_t>& from) {
    into.insert(into.end(),from.begin(),from.end());
}
std::vector<M3GMaterialBindingObject> read_identities(const Bytes& bytes) {
    const M3GScene checked(bytes);
    const M3GFile file(bytes);
    require(file.objects().size()<=max_objects,"M3G material object limit exceeded");
    std::vector<M3GMaterialBindingObject> result;result.reserve(file.objects().size());
    for(const auto& serialized:file.objects()) {
        M3GMaterialBindingObject row;row.index=serialized.index;row.type=serialized.type;
        if(row.type==0) { result.push_back(std::move(row));continue; }
        require(row.type!=15 && row.type!=16,"nonrigid M3G material reference ordering is not supported");
        Reader r(file.sections().at(serialized.section),serialized.payload_offset,serialized.payload_size);
        row.user_id=r.word();row.references=r.refs();
        const auto parameters=r.count(8);
        for(std::size_t i=0;i<parameters;++i) { (void)r.word();const auto n=r.word();r.skip(n); }
        if(node(row.type) || row.type==17) {
            if(r.byte()!=0)r.skip(40);
            if(r.byte()!=0)r.skip(64);
        }
        if(node(row.type)) {
            r.skip(7);
            if(r.byte()!=0) {
                r.skip(2);const auto z=r.word(),y=r.word();
                // Required authored model graphs have no nonnull alignment
                // targets. Do not treat serialized alignment references as
                // ordinary Object3D.find references without separate proof.
                require(z==0 && y==0,"nonnull M3G alignment-reference lookup is not recovered");
            }
        }
        const auto add=[&](){row.references.push_back(r.word());};
        switch(row.type) {
        case 2:add();add();break;
        case 3:
            r.skip(1);for(unsigned i=0;i<4;++i)add();
            row.texture_units=r.refs();
            require(row.texture_units.size()<=2,"original material lookup supports at most two texture units");
            append(row.references,row.texture_units);break;
        case 4:r.skip(4);add();break;
        case 9:row.children=r.refs();append(row.references,row.children);break;
        case 14: {
            add();const auto n=r.count(8);row.submesh_appearances.reserve(n);
            for(std::size_t i=0;i<n;++i) { add();row.submesh_appearances.push_back(r.word()); }
            append(row.references,row.submesh_appearances);break;
        }
        case 17:row.texture_image=r.word();row.references.push_back(row.texture_image);break;
        case 18:add();add();break;
        case 21: {
            r.skip(4);add();r.skip(16);add();add();const auto n=r.count(20);
            require(n<=2,"original material lookup supports at most two texture-coordinate references");
            for(std::size_t i=0;i<n;++i) { add();r.skip(16); }break;
        }
        case 22:row.children=r.refs();append(row.references,row.children);add();add();break;
        default:break; // Remaining supported bodies only reference animation tracks.
        }
        result.push_back(std::move(row));
    }
    // Keep the independently validated metadata alive through this pass.
    require(checked.objects().size()==result.size(),"M3G identity object count differs from scene decoder");
    return result;
}
std::uint32_t big_word(const Bytes& bytes,std::size_t at) {
    require(at<=bytes.size() && bytes.size()-at>=4,"truncated material PNG word");
    return (std::uint32_t(bytes[at])<<24U)|(std::uint32_t(bytes[at+1])<<16U)|
           (std::uint32_t(bytes[at+2])<<8U)|bytes[at+3];
}
} // namespace

M3GMaterialBindings::M3GMaterialBindings(const Bytes& bytes):objects_(read_identities(bytes)) { validate(); }
M3GMaterialBindings::M3GMaterialBindings(std::vector<M3GMaterialBindingObject> objects)
    :objects_(std::move(objects)) { validate(); }

void M3GMaterialBindings::validate() {
    require(!objects_.empty() && objects_.size()<=max_objects,"invalid M3G material graph size");
    std::size_t total=0;
    for(std::size_t i=0;i<objects_.size();++i) {
        const auto& row=objects_[i];
        require(row.index!=0 && row.index<=std::numeric_limits<std::uint32_t>::max(),"invalid M3G material object identity");
        require(row.type<=22 && row.type!=15 && row.type!=16,"unsupported M3G material graph object type");
        require(positions_.emplace(row.index,i).second,"duplicate M3G material object identity");
        for(const auto size:{row.references.size(),row.children.size(),row.texture_units.size(),row.submesh_appearances.size()}) {
            require(size<=max_references-total,"M3G material graph reference limit exceeded");total+=size;
        }
        require(group(row.type) || row.children.empty(),"M3G material children belong to a non-Group");
        require(row.type==3 || row.texture_units.empty(),"texture units belong to a non-Appearance");
        require(row.type==14 || row.submesh_appearances.empty(),"submeshes belong to a non-Mesh");
        require(row.type==17 || row.texture_image==0,"image binding belongs to a non-Texture2D");
        require(row.texture_units.size()<=2,"original material lookup supports at most two texture units");
        require(row.type!=0 || (row.user_id==0 && row.references.empty()),"header is not an Object3D material binding");
    }
    std::set<std::size_t> parented;
    for(const auto& row:objects_) {
        std::set<std::size_t> edges(row.references.begin(),row.references.end());
        for(const auto id:row.references) {
            if(id==0)continue;
            const auto found=positions_.find(id);
            require(found!=positions_.end() && objects_[found->second].type!=0,"dangling M3G material reference");
        }
        const auto typed=[&](std::size_t id,std::uint8_t type,bool nullable) {
            if(id==0) { require(nullable,"null M3G material child");return; }
            const auto at=positions_.find(id);
            require(at!=positions_.end() && edges.count(id)!=0,"M3G material typed edge missing from references");
            require(objects_[at->second].type==type,"wrong M3G material reference type");
        };
        for(const auto id:row.children) {
            const auto at=positions_.find(id);
            require(id!=0 && at!=positions_.end() && node(objects_[at->second].type) && edges.count(id)!=0,
                    "invalid M3G material Node child");
            require(parented.insert(id).second,"M3G material Node has multiple parents");
        }
        for(const auto id:row.texture_units)typed(id,17,true);
        for(const auto id:row.submesh_appearances)typed(id,3,true);
        if(row.type==17)typed(row.texture_image,10,false);
    }
    // A reference DAG may share Appearance, Texture2D, Image and animation
    // objects. Reject cycles with an explicit stack instead of recursing.
    std::vector<std::uint8_t> state(objects_.size());
    for(std::size_t root=0;root<objects_.size();++root) {
        if(state[root])continue;
        std::vector<std::pair<std::size_t,std::size_t>> stack{{root,0}};state[root]=1;
        while(!stack.empty()) {
            auto& frame=stack.back();const auto& refs=objects_[frame.first].references;
            if(frame.second==refs.size()) { state[frame.first]=2;stack.pop_back();continue; }
            const auto id=refs[frame.second++];if(id==0)continue;
            const auto at=positions_.at(id);
            require(state[at]!=1,"cyclic M3G material references");
            if(state[at]==0) { state[at]=1;stack.emplace_back(at,0); }
        }
    }
}

std::array<std::optional<std::uint32_t>,2> m3g_material_image_resources(std::uint32_t type) {
    require(type<image_pairs.size(),"effective model type is outside original material table");
    std::array<std::optional<std::uint32_t>,2> result{};
    for(std::size_t i=0;i<2;++i)if(image_pairs[type][i]>=0)result[i]=std::uint32_t(image_pairs[type][i]);
    return result;
}

std::vector<M3GTextureImageOverride> M3GMaterialBindings::plan(std::size_t root,std::uint32_t type) const {
    const auto resources=m3g_material_image_resources(type);
    const auto root_at=positions_.find(root);
    require(root_at!=positions_.end() && group(objects_[root_at->second].type),"material override root is not a Group");
    std::vector<M3GTextureImageOverride> result;
    if(!resources[0] && !resources[1])return result;
    const auto& root_row=objects_[root_at->second];
    std::vector<std::size_t> reachable,stack{root_at->second};std::set<std::size_t> visited;
    while(!stack.empty()) {
        const auto at=stack.back();stack.pop_back();if(!visited.insert(at).second)continue;
        reachable.push_back(at);
        for(const auto id:objects_[at].references)if(id!=0)stack.push_back(positions_.at(id));
    }
    const auto texture0=[&](const M3GMaterialBindingObject& appearance) {
        return appearance.texture_units.empty() ? std::size_t(0) : appearance.texture_units[0];
    };
    for(std::size_t slot=0;slot<2;++slot) {
        if(!resources[slot])continue;
        M3GTextureImageOverride step;step.slot=slot==0 ? M3GMaterialSlot::material : M3GMaterialSlot::material2;
        step.resource_id=*resources[slot];const auto key=slot==0 ? material_key : material2_key;
        for(const auto at:reachable)if(objects_[at].user_id==key) {
            require(!step.named_object,"ambiguous material Object3D find ordering is not recovered");
            step.named_object=objects_[at].index;
        }
        const M3GMaterialBindingObject* appearance=nullptr;
        if(step.named_object) {
            const auto& named=objects_[positions_.at(*step.named_object)];
            if(named.type==3 && texture0(named)!=0)appearance=&named;
        }
        if(!appearance) {
            step.used_mesh_fallback=true;const M3GMaterialBindingObject* mesh=nullptr;
            for(const auto child:root_row.children) {
                const auto* current=&objects_[positions_.at(child)];
                if(!group(current->type) || current->children.empty())continue;
                for(;;) {
                    // Original follows child0 repeatedly after the first
                    // nonempty Group. A nested empty Group is a bad structure,
                    // not permission to try a later sibling instead.
                    require(!current->children.empty(),"empty Group in original material fallback chain");
                    const auto* first=&objects_[positions_.at(current->children[0])];
                    if(group(first->type)) { current=first;continue; }
                    if(first->type==14)mesh=first;
                    break;
                }
                if(mesh)break;
            }
            require(mesh!=nullptr,"original material fallback found no Mesh");
            require(!mesh->submesh_appearances.empty() && mesh->submesh_appearances[0]!=0,
                    "original material fallback requires appearance0");
            appearance=&objects_[positions_.at(mesh->submesh_appearances[0])];
        }
        step.appearance_object=appearance->index;
        const auto texture=texture0(*appearance);
        if(texture!=0) {
            step.texture_object=texture;
            step.authored_image_object=objects_[positions_.at(texture)].texture_image;
        }
        result.push_back(std::move(step));
    }
    return result;
}

M3GMaterialImage decode_m3g_material_image(std::uint32_t resource_id,const Bytes& png) {
    const auto& definition=image_definition(resource_id);
    auto image=decode_png(png); // Bounds, PNG framing/CRC and actual pixel decode.
    require(image.width==definition.width && image.height==definition.height,
            "material replacement PNG dimensions differ from original");
    require(png.size()>=33 && big_word(png,8)==13 && png[12]=='I' && png[13]=='H' && png[14]=='D' && png[15]=='R',
            "material replacement PNG has no canonical IHDR");
    require(png[24]==8 && png[25]==definition.color_type && png[28]==0,
            "material replacement PNG encoding differs from original RGB input");
    std::size_t at=8;
    while(at<png.size()) {
        require(png.size()-at>=12,"truncated material PNG chunk");const auto n=big_word(png,at);
        require(n<=png.size()-at-12,"material PNG chunk size overflows input");
        require(!(png[at+4]=='t' && png[at+5]=='R' && png[at+6]=='N' && png[at+7]=='S'),
                "material replacement transparency differs from original RGB input");
        at+=std::size_t(n)+12U;
    }
    for(std::size_t i=3;i<image.pixels.size();i+=4)
        require(image.pixels[i]==255,"original RGB material replacement acquired alpha");
    return {resource_id,99,std::move(image)};
}

M3GMaterialImage load_m3g_material_image(const Archive& archive,std::uint32_t resource_id) {
    (void)image_definition(resource_id);
    const auto candidates=archive.find(resource_id,image_key);
    require(candidates.size()==1,"material image archive identity is missing or ambiguous");
    return decode_m3g_material_image(resource_id,archive.read(candidates[0]));
}
} // namespace g1
