#include "g1/M3GScene.hpp"

#include <algorithm>
#include <cmath>
#include <cstring>
#include <functional>
#include <limits>
#include <set>
#include <sstream>
#include <variant>

namespace g1 {
namespace {
constexpr M3GMatrix identity{{1,0,0,0, 0,1,0,0, 0,0,1,0, 0,0,0,1}};
constexpr std::size_t max_vertices=1'000'000;

void require(bool condition,const char* detail) {
    if (!condition) throw FormatError(detail);
}

class Reader {
public:
    Reader(const Bytes& bytes,std::size_t begin,std::size_t size)
        : bytes_(bytes),cursor_(begin),end_(begin+size) {
        require(begin<=bytes.size() && size<=bytes.size()-begin,"M3G body range exceeds section");
    }
    std::size_t remaining() const noexcept { return end_-cursor_; }
    std::uint8_t byte() { require(remaining()!=0,"truncated M3G body"); return bytes_[cursor_++]; }
    bool boolean() { const auto b=byte(); require(b<=1,"invalid M3G boolean"); return b!=0; }
    std::uint16_t u16() {
        const auto low=byte(); const auto high=byte();
        return std::uint16_t(std::uint16_t(low)|(std::uint16_t(high)<<8U));
    }
    std::uint32_t u32() {
        const auto low=u16(); const auto high=u16();
        return std::uint32_t(low)|(std::uint32_t(high)<<16U);
    }
    float scalar() {
        const auto bits=u32();
        const auto exponent=bits&0x7f800000U;
        require(exponent!=0x7f800000U && bits!=0x80000000U &&
                (exponent!=0U || (bits&0x007fffffU)==0U),"invalid serialized M3G Float32");
        float result; std::memcpy(&result,&bits,4); return result;
    }
    template<std::size_t N> std::array<float,N> floats() {
        std::array<float,N> result{}; for (auto& value:result) value=scalar(); return result;
    }
    template<std::size_t N> std::array<float,N> color() {
        std::array<float,N> result{}; for (auto& value:result) value=float(byte())/255.0F; return result;
    }
    std::size_t count(std::size_t element_size) {
        const auto n=u32();
        require(element_size!=0 && n<=remaining()/element_size,"unbounded M3G counted array");
        return n;
    }
    std::vector<std::uint32_t> references() {
        const auto n=count(4); std::vector<std::uint32_t> result; result.reserve(n);
        for (std::size_t i=0;i<n;++i) result.push_back(u32());
        return result;
    }
    void skip(std::size_t n) { require(n<=remaining(),"truncated M3G body data"); cursor_+=n; }
    Bytes bytes(std::size_t n) {
        require(n<=remaining(),"truncated M3G byte array");
        Bytes result(bytes_.begin()+std::ptrdiff_t(cursor_),bytes_.begin()+std::ptrdiff_t(cursor_+n));
        cursor_+=n; return result;
    }
    void finish() const { require(remaining()==0,"M3G object body has trailing data"); }
private:
    const Bytes& bytes_; std::size_t cursor_,end_;
};

struct VertexArray { std::uint8_t width{},components{}; std::uint16_t count{}; std::vector<std::int16_t> values; };
struct Coordinates { std::uint32_t array{}; std::array<float,3> bias{}; float scale{}; };
struct VertexBuffer {
    std::array<float,4> color; Coordinates positions;
    std::uint32_t normals{},colors{}; std::vector<Coordinates> texcoords;
};
struct Indices { std::vector<std::uint32_t> values,strip_lengths; };
struct Image { std::uint8_t format{}; bool mutable_image{}; std::uint32_t width{},height{}; Bytes palette,pixels; };
struct Appearance { std::int8_t layer{}; std::uint32_t compositing{},fog{},polygon{},material{}; std::vector<std::uint32_t> textures; };
struct Texture { std::uint32_t image{}; M3GTextureState state; };
struct Mesh { std::uint32_t vertices{}; std::vector<std::array<std::uint32_t,2>> submeshes; };
struct Track { std::uint32_t sequence{},controller{},property{}; };
struct World { std::uint32_t camera{},background{}; };
struct Background { std::uint32_t image{}; };
struct Sprite { std::uint32_t image{},appearance{}; bool scaled{}; std::array<std::int32_t,4> crop{}; };
struct Morph { Mesh mesh; std::vector<std::uint32_t> targets; };
struct Skin { Mesh mesh; std::uint32_t skeleton{}; std::vector<std::uint32_t> bones; };
using Body=std::variant<std::monostate,VertexArray,VertexBuffer,Indices,Image,Appearance,Texture,
                        Mesh,Track,World,Background,Sprite,Morph,Skin,M3GMaterial,M3GAppearanceState>;
struct Object { M3GObjectInfo info; Body body; };

M3GTransform read_transform(Reader& reader) {
    M3GTransform result;
    if (reader.boolean()) {
        result.translation=reader.floats<3>(); result.scale=reader.floats<3>();
        result.angle_degrees=reader.scalar(); result.axis=reader.floats<3>();
    }
    if (reader.boolean()) {
        for (std::size_t row=0;row<4;++row)
            for (std::size_t col=0;col<4;++col) result.general[col*4+row]=reader.scalar();
    }
    return result;
}

bool node_type(std::uint8_t type) noexcept {
    return type==5 || type==9 || type==12 || type==14 || type==15 || type==16 || type==18 || type==22;
}

Mesh read_mesh(Reader& reader) {
    Mesh mesh; mesh.vertices=reader.u32(); const auto n=reader.count(8); mesh.submeshes.reserve(n);
    for (std::size_t i=0;i<n;++i) mesh.submeshes.push_back({reader.u32(),reader.u32()});
    return mesh;
}

Object read_object(const M3GFile& file,const M3GObject& serialized) {
    Object object; auto& info=object.info;
    info.index=serialized.index; info.type=serialized.type;
    if (info.type==0) return object;
    require(info.type!=255,"external M3G references are not supported by the native rigid loader");
    Reader reader(file.sections().at(serialized.section),serialized.payload_offset,serialized.payload_size);
    info.user_id=reader.u32(); info.animation_tracks=reader.references();
    const auto parameters=reader.count(8); std::set<std::uint32_t> parameter_ids;
    for (std::size_t i=0;i<parameters;++i) {
        require(parameter_ids.insert(reader.u32()).second,"duplicate M3G user parameter");
        const auto n=reader.u32(); reader.skip(n);
    }
    info.is_node=node_type(info.type);
    if (info.is_node || info.type==17) info.transform=read_transform(reader);
    if (info.is_node) {
        require(info.transform.general[3]==0 && info.transform.general[7]==0 &&
                info.transform.general[11]==0 && info.transform.general[15]==1,
                "M3G node general transform is not affine");
        info.rendering_enabled=reader.boolean(); (void)reader.boolean();
        info.alpha_factor=reader.byte(); info.scope=reader.u32(); info.has_alignment=reader.boolean();
        if (info.has_alignment) {
            // Alignment constraints are only applied by a runtime align call;
            // the static authored transform remains the value loaded above.
            const auto z=reader.byte(),y=reader.byte();
            require(z>=144 && z<=148 && y>=144 && y<=148,"invalid M3G alignment target");
            require(reader.u32()<=info.index && reader.u32()<=info.index,"forward M3G alignment reference");
        }
    }
    switch (info.type) {
    case 1: // Controller values are validated but not applied to the static pose.
        (void)reader.scalar(); (void)reader.scalar(); (void)reader.u32(); (void)reader.u32();
        (void)reader.scalar(); (void)reader.u32(); break;
    case 2: object.body=Track{reader.u32(),reader.u32(),reader.u32()}; break;
    case 3: {
        Appearance appearance;
        const int layer=reader.byte(); appearance.layer=static_cast<std::int8_t>(layer<128 ? layer : layer-256);
        require(appearance.layer>=-63 && appearance.layer<=63,"invalid M3G appearance layer");
        appearance.compositing=reader.u32(); appearance.fog=reader.u32(); appearance.polygon=reader.u32();
        appearance.material=reader.u32(); appearance.textures=reader.references(); object.body=std::move(appearance); break;
    }
    case 4: {
        (void)reader.color<4>(); Background background{reader.u32()};
        const auto x=reader.byte(),y=reader.byte();
        require((x==32 || x==33) && (y==32 || y==33),"invalid M3G background mode");
        for (int i=0;i<4;++i) (void)reader.u32();
        (void)reader.boolean(); (void)reader.boolean(); object.body=background; break;
    }
    case 5: {
        const auto projection=reader.byte();
        require(projection>=48 && projection<=50,"invalid M3G camera projection");
        for (int i=0;i<(projection==48 ? 16 : 4);++i) (void)reader.scalar();
        break;
    }
    case 6: {
        M3GAppearanceState state;
        state.depth_test=reader.boolean(); state.depth_write=reader.boolean();
        state.color_write=reader.boolean(); state.alpha_write=reader.boolean(); state.blending=reader.byte();
        require(state.blending>=64 && state.blending<=68,"invalid M3G compositing blend function");
        state.alpha_threshold=float(reader.byte())/255.0F;
        state.depth_offset_factor=reader.scalar(); state.depth_offset_units=reader.scalar(); object.body=state; break;
    }
    case 7: {
        (void)reader.color<3>(); const auto mode=reader.byte();
        require(mode==80 || mode==81,"invalid M3G fog mode");
        (void)reader.scalar(); if (mode==81) (void)reader.scalar(); break;
    }
    case 8: {
        M3GAppearanceState state;
        state.culling=reader.byte(); state.shading=reader.byte(); state.winding=reader.byte();
        require(state.culling>=160 && state.culling<=162 && state.shading>=164 && state.shading<=165 &&
                state.winding>=168 && state.winding<=169,"invalid M3G polygon mode");
        state.two_sided_lighting=reader.boolean(); state.local_camera_lighting=reader.boolean();
        state.perspective_correction=reader.boolean(); object.body=state; break;
    }
    case 9: info.children=reader.references(); break;
    case 10: {
        Image image;
        image.format=reader.byte(); image.mutable_image=reader.boolean(); image.width=reader.u32(); image.height=reader.u32();
        require(image.format>=96 && image.format<=100 && image.width!=0 && image.height!=0 &&
                image.width<=8192 && image.height<=8192,"invalid M3G image format or dimensions");
        if (!image.mutable_image) {
            auto n=reader.u32(); image.palette=reader.bytes(n); n=reader.u32(); image.pixels=reader.bytes(n);
        }
        object.body=std::move(image); break;
    }
    case 11: {
        Indices indices; const auto encoding=reader.byte(); const auto width=encoding&127U;
        require((encoding<=2 || (encoding>=128 && encoding<=130)),"invalid M3G strip encoding");
        const auto read_index=[&]() -> std::uint32_t {
            return width==0 ? reader.u32() : width==1 ? reader.byte() : reader.u16();
        };
        const bool explicit_indices=(encoding&128U)!=0;
        std::uint32_t first=0;
        if (explicit_indices) {
            const auto n=reader.count(width==0 ? 4U : width==1 ? 1U : 2U); indices.values.reserve(n);
            for (std::size_t i=0;i<n;++i) indices.values.push_back(read_index());
        } else first=read_index();
        indices.strip_lengths=reader.references(); std::uint64_t total=0;
        require(!indices.strip_lengths.empty(),"empty M3G strip-length array");
        for (const auto n:indices.strip_lengths) {
            require(n>=3,"M3G triangle strip has fewer than three vertices"); total+=n;
        }
        require(total<=max_vertices,"M3G strip index bound exceeded");
        if (explicit_indices) require(total<=indices.values.size(),"M3G strip lengths exceed explicit indices");
        else {
            require(std::uint64_t(first)+total<=65536U,"implicit M3G indices exceed vertex-array range");
            indices.values.reserve(static_cast<std::size_t>(total));
            for (std::uint32_t i=0;i<total;++i) indices.values.push_back(first+i);
        }
        object.body=std::move(indices); break;
    }
    case 12: {
        (void)reader.floats<3>(); (void)reader.color<3>(); const auto mode=reader.byte();
        require(mode>=128 && mode<=131,"invalid M3G light mode"); (void)reader.floats<3>(); break;
    }
    case 13: {
        M3GMaterial material;
        material.ambient=reader.color<3>(); material.diffuse=reader.color<4>();
        material.emissive=reader.color<3>(); material.specular=reader.color<3>();
        material.shininess=reader.scalar(); material.vertex_color_tracking=reader.boolean();
        require(material.shininess>=0 && material.shininess<=128,"invalid M3G material shininess");
        object.body=material; break;
    }
    case 14: object.body=read_mesh(reader); break;
    case 15: {
        Morph morph; morph.mesh=read_mesh(reader); const auto n=reader.count(8);
        for (std::size_t i=0;i<n;++i) { morph.targets.push_back(reader.u32()); (void)reader.scalar(); }
        object.body=std::move(morph); break;
    }
    case 16: {
        Skin skin; skin.mesh=read_mesh(reader); skin.skeleton=reader.u32(); const auto n=reader.count(16);
        for (std::size_t i=0;i<n;++i) {
            skin.bones.push_back(reader.u32()); (void)reader.u32(); (void)reader.u32(); (void)reader.u32();
        }
        object.body=std::move(skin); break;
    }
    case 17: {
        Texture texture; texture.image=reader.u32(); auto& state=texture.state;
        state.blend_color=reader.color<3>(); state.function=reader.byte();
        state.wrap_s=reader.byte(); state.wrap_t=reader.byte();
        state.level_filter=reader.byte(); state.image_filter=reader.byte();
        require(state.function>=224 && state.function<=228 && state.wrap_s>=240 && state.wrap_s<=241 &&
                state.wrap_t>=240 && state.wrap_t<=241 && state.level_filter>=208 && state.level_filter<=210 &&
                state.image_filter>=209 && state.image_filter<=210,"invalid M3G texture state");
        object.body=texture; break;
    }
    case 18: {
        Sprite sprite;sprite.image=reader.u32();sprite.appearance=reader.u32();sprite.scaled=reader.boolean();
        for(auto& coordinate:sprite.crop) {
            const auto raw=reader.u32();std::memcpy(&coordinate,&raw,4);
        }
        object.body=sprite; break;
    }
    case 19: {
        const auto interpolation=reader.byte(),repeat=reader.byte(),encoding=reader.byte();
        require(interpolation>=176 && interpolation<=180 && repeat>=192 && repeat<=193 && encoding<=2,
                "invalid M3G keyframe encoding");
        (void)reader.u32(); const auto first=reader.u32(),last=reader.u32(),components=reader.u32(),count=reader.u32();
        require(components!=0 && components<=256 && count!=0 && first<count && last<count,
                "invalid M3G keyframe range");
        if (encoding!=0) for (std::uint32_t i=0;i<components*2U;++i) (void)reader.scalar();
        const std::size_t width=encoding==0 ? 4U : encoding==1 ? 1U : 2U;
        require(count<=reader.remaining()/(4U+std::size_t(components)*width),"unbounded M3G keyframe count");
        for (std::uint32_t frame=0;frame<count;++frame) {
            (void)reader.u32();
            if (encoding==0) for (std::uint32_t i=0;i<components;++i) (void)reader.scalar();
            else reader.skip(std::size_t(components)*width);
        }
        break;
    }
    case 20: {
        VertexArray array; array.width=reader.byte(); array.components=reader.byte(); const auto encoding=reader.byte();
        array.count=reader.u16();
        require(array.width>=1 && array.width<=2 && array.components>=2 && array.components<=4 &&
                encoding<=1 && array.count!=0,"invalid M3G vertex-array format");
        const auto n=std::size_t(array.count)*array.components;
        require(n<=reader.remaining()/array.width,"truncated M3G vertex-array values");
        array.values.reserve(n); std::array<std::uint32_t,4> accum{};
        const std::uint32_t modulus=array.width==1 ? 256U : 65536U;
        for (std::size_t i=0;i<n;++i) {
            const std::uint32_t raw=array.width==1 ? reader.byte() : reader.u16();
            auto& value=accum[i%array.components]; value=((encoding ? value : 0U)+raw)&(modulus-1U);
            array.values.push_back(static_cast<std::int16_t>(value<modulus/2 ? std::int32_t(value) :
                                                            std::int32_t(value)-std::int32_t(modulus)));
        }
        object.body=std::move(array); break;
    }
    case 21: {
        VertexBuffer buffer; buffer.color=reader.color<4>(); buffer.positions.array=reader.u32();
        buffer.positions.bias=reader.floats<3>(); buffer.positions.scale=reader.scalar();
        buffer.normals=reader.u32(); buffer.colors=reader.u32(); const auto n=reader.count(20);
        for (std::size_t i=0;i<n;++i) {
            Coordinates coordinates; coordinates.array=reader.u32(); coordinates.bias=reader.floats<3>();
            coordinates.scale=reader.scalar(); buffer.texcoords.push_back(coordinates);
        }
        object.body=std::move(buffer); break;
    }
    case 22: info.children=reader.references(); object.body=World{reader.u32(),reader.u32()}; break;
    default: throw FormatError("unhandled serialized M3G object type");
    }
    reader.finish(); return object;
}

std::array<float,4> transform_vector(const M3GMatrix& matrix,const std::array<float,4>& input) noexcept {
    std::array<float,4> result{};
    for (std::size_t row=0;row<4;++row)
        for (std::size_t col=0;col<4;++col) result[row]+=matrix[col*4+row]*input[col];
    return result;
}

RgbaImage decode_image(const Image& image) {
    require(!image.mutable_image,"mutable M3G image has no serialized static pixels");
    const std::size_t components=image.format==96 || image.format==97 ? 1U : image.format==98 ? 2U : image.format==99 ? 3U : 4U;
    const auto count=std::size_t(image.width)*image.height;
    require(count<=Archive::max_resource_size/4U,"M3G RGBA image expansion bound exceeded");
    const bool indexed=!image.palette.empty();
    require(!indexed || (image.palette.size()%components==0 && image.palette.size()/components<=256),
            "invalid M3G image palette");
    require(image.pixels.size()==count*(indexed ? 1U : components),"M3G image pixel count differs from dimensions");
    RgbaImage result{image.width,image.height,Bytes(count*4U)};
    for (std::size_t i=0;i<count;++i) {
        const auto offset=indexed ? std::size_t(image.pixels[i])*components : i*components;
        const auto& input=indexed ? image.palette : image.pixels;
        require(offset<=input.size() && components<=input.size()-offset,"M3G image palette index is outside palette");
        auto* pixel=result.pixels.data()+i*4U;
        switch (image.format) {
        case 96: pixel[0]=255; pixel[1]=255; pixel[2]=255; pixel[3]=input[offset]; break;
        case 97: pixel[0]=input[offset]; pixel[1]=pixel[0]; pixel[2]=pixel[0]; pixel[3]=255; break;
        case 98: pixel[0]=input[offset]; pixel[1]=pixel[0]; pixel[2]=pixel[0]; pixel[3]=input[offset+1]; break;
        case 99: pixel[0]=input[offset]; pixel[1]=input[offset+1]; pixel[2]=input[offset+2]; pixel[3]=255; break;
        case 100: std::copy_n(input.data()+offset,4,pixel); break;
        default: throw FormatError("invalid M3G image format");
        }
    }
    return result;
}
} // namespace

M3GMatrix m3g_multiply(const M3GMatrix& left,const M3GMatrix& right) noexcept {
    M3GMatrix result{};
    for (std::size_t col=0;col<4;++col)
        for (std::size_t row=0;row<4;++row)
            for (std::size_t k=0;k<4;++k) result[col*4+row]+=left[k*4+row]*right[col*4+k];
    return result;
}

M3GMatrix m3g_transform_matrix(const M3GTransform& value) {
    M3GMatrix rotation=identity;
    if (value.angle_degrees!=0) {
        const auto& a=value.axis;
        const float magnitude=std::sqrt((a[0]*a[0]+a[1]*a[1])+a[2]*a[2]);
        require(magnitude>0 && std::isfinite(magnitude),"invalid M3G orientation axis");
        // Same float degrees coefficient used by the original CssTransform
        // wrapper. Host sinf/cosf remain a documented libm boundary.
        const float half=(value.angle_degrees*0x1.1df46ap-6F)*0.5F;
        const float sine=std::sin(half),w=std::cos(half);
        const float x=(a[0]/magnitude)*sine,y=(a[1]/magnitude)*sine,z=(a[2]/magnitude)*sine;
        rotation={{1-2*(y*y+z*z),2*(x*y+z*w),2*(x*z-y*w),0,
                   2*(x*y-z*w),1-2*(x*x+z*z),2*(y*z+x*w),0,
                   2*(x*z+y*w),2*(y*z-x*w),1-2*(x*x+y*y),0, 0,0,0,1}};
    }
    for (std::size_t col=0;col<3;++col)
        for (std::size_t row=0;row<3;++row) rotation[col*4+row]*=value.scale[col];
    for (std::size_t row=0;row<3;++row) rotation[12+row]=value.translation[row];
    const auto result=m3g_multiply(rotation,value.general);
    for (const auto f:result) require(std::isfinite(f),"M3G composite transform is nonfinite");
    return result;
}

struct M3GScene::Impl {
    std::vector<Object> objects;
    std::vector<M3GObjectInfo> infos;

    const Object& get(std::uint32_t index) const {
        require(index!=0 && index<objects.size(),"M3G reference is outside its file"); return objects[index];
    }
    void reference(const Object& owner,std::uint32_t target,std::initializer_list<int> types,bool nullable=true) const {
        if (target==0) { require(nullable,"required M3G reference is null"); return; }
        require(target<=owner.info.index,"forward M3G reference");
        const auto& value=get(target);
        require(std::find(types.begin(),types.end(),value.info.type)!=types.end(),"M3G reference has the wrong object type");
    }
    template<class T> const T& body(std::uint32_t index) const {
        const auto* result=std::get_if<T>(&get(index).body);
        require(result!=nullptr,"M3G object body type mismatch"); return *result;
    }
    M3GAppearanceState appearance(std::uint32_t id) const {
        const auto& source=body<Appearance>(id); M3GAppearanceState result;
        if (source.compositing) result=body<M3GAppearanceState>(source.compositing);
        result.layer=source.layer;
        if (source.polygon) {
            const auto& p=body<M3GAppearanceState>(source.polygon);
            result.culling=p.culling; result.shading=p.shading; result.winding=p.winding;
            result.two_sided_lighting=p.two_sided_lighting; result.local_camera_lighting=p.local_camera_lighting;
            result.perspective_correction=p.perspective_correction;
        }
        require(source.fog==0,"M3G static renderer fog evaluation is not implemented");
        if (source.material) result.material=body<M3GMaterial>(source.material);
        require(source.textures.size()<=1,"M3G static renderer supports one texture unit");
        if (!source.textures.empty()) {
            const auto texture_id=source.textures.front(); const auto& texture=body<Texture>(texture_id);
            result.texture=texture.state; result.texture->texture_object=texture_id;
            result.texture->image_object=texture.image;
            result.texture->image_format=body<Image>(texture.image).format;
            result.texture->transform=m3g_transform_matrix(get(texture_id).info.transform);
        }
        return result;
    }
};

M3GScene::M3GScene(const Bytes& bytes) {
    const M3GFile file(bytes);
    require(!file.has_external_references(),"external M3G resources require a separate resolver");
    auto impl=std::make_shared<Impl>(); impl->objects.emplace_back();
    for (const auto& serialized:file.objects()) impl->objects.push_back(read_object(file,serialized));
    for (auto& object:impl->objects) {
        if (object.info.index==0) continue;
        for (const auto id:object.info.animation_tracks) impl->reference(object,id,{2},false);
        for (const auto id:object.info.children) {
            impl->reference(object,id,{5,9,12,14,15,16,18,22},false);
            require(id!=object.info.index && !impl->objects[id].info.parent,"cyclic or multiply parented M3G node");
            impl->objects[id].info.parent=object.info.index;
        }
        const auto check_mesh=[&](const Mesh& mesh) {
            impl->reference(object,mesh.vertices,{21},false);
            for (const auto& sub:mesh.submeshes) {
                impl->reference(object,sub[0],{11},false); impl->reference(object,sub[1],{3});
            }
        };
        if (const auto* p=std::get_if<VertexBuffer>(&object.body)) {
            impl->reference(object,p->positions.array,{20}); impl->reference(object,p->normals,{20}); impl->reference(object,p->colors,{20});
            for (const auto& c:p->texcoords) impl->reference(object,c.array,{20},false);
        } else if (const auto* p=std::get_if<Appearance>(&object.body)) {
            impl->reference(object,p->compositing,{6}); impl->reference(object,p->fog,{7});
            impl->reference(object,p->polygon,{8}); impl->reference(object,p->material,{13});
            for (const auto id:p->textures) impl->reference(object,id,{17},false);
        } else if (const auto* p=std::get_if<Texture>(&object.body)) impl->reference(object,p->image,{10},false);
        else if (const auto* p=std::get_if<Mesh>(&object.body)) check_mesh(*p);
        else if (const auto* p=std::get_if<Morph>(&object.body)) {
            check_mesh(p->mesh); for (const auto id:p->targets) impl->reference(object,id,{21},false);
        } else if (const auto* p=std::get_if<Skin>(&object.body)) {
            check_mesh(p->mesh); impl->reference(object,p->skeleton,{9},false);
            for (const auto id:p->bones) impl->reference(object,id,{5,9,12,14,15,16,18,22},false);
        } else if (const auto* p=std::get_if<Track>(&object.body)) {
            impl->reference(object,p->sequence,{19},false); impl->reference(object,p->controller,{1});
        } else if (const auto* p=std::get_if<World>(&object.body)) {
            impl->reference(object,p->camera,{5}); impl->reference(object,p->background,{4});
        } else if (const auto* p=std::get_if<Background>(&object.body)) impl->reference(object,p->image,{10});
        else if (const auto* p=std::get_if<Sprite>(&object.body)) {
            impl->reference(object,p->image,{10},false); impl->reference(object,p->appearance,{3});
        }
    }
    const auto& author=file.authoring_field();
    const std::string start="<SwerveAuthor2NameTable>",end="</SwerveAuthor2NameTable>";
    const auto begin=author.find(start);
    std::map<std::uint32_t,std::string> names;
    if (begin!=std::string::npos) {
        const auto last=author.find(end,begin+start.size()); require(last!=std::string::npos,"unterminated M3G authoring name table");
        std::istringstream lines(author.substr(begin+start.size(),last-begin-start.size())); std::string line;
        while (std::getline(lines,line)) {
            std::istringstream row(line); std::uint64_t id; std::string name;
            if (!(row>>id)) continue;
            require(id<=std::numeric_limits<std::uint32_t>::max(),"M3G authoring ID exceeds uint32");
            std::getline(row>>std::ws,name); if (!name.empty() && name.back()=='\r') name.pop_back();
            require(!name.empty() && names.emplace(static_cast<std::uint32_t>(id),name).second,"invalid M3G authoring name entry");
        }
    }
    for (std::size_t i=1;i<impl->objects.size();++i) {
        auto& info=impl->objects[i].info; const auto found=names.find(info.user_id);
        if (found!=names.end()) info.name=found->second;
        impl->infos.push_back(info);
    }
    impl_=std::move(impl);
}

const std::vector<M3GObjectInfo>& M3GScene::objects() const noexcept { return impl_->infos; }
std::vector<std::size_t> M3GScene::find_user_id(std::uint32_t id) const {
    std::vector<std::size_t> result;
    for (const auto& info:impl_->infos) if (info.user_id==id) result.push_back(info.index);
    return result;
}

M3GStaticModel M3GScene::static_model(std::size_t root) const {
    return build_asset(root,false).geometry;
}
M3GRenderAsset M3GScene::render_asset(std::size_t root) const {
    return build_asset(root,true);
}
M3GRenderAsset M3GScene::build_asset(std::size_t root,bool stateful) const {
    if (root==0) {
        for (const auto& object:impl_->infos) if (object.type==22) {
            require(root==0,"M3G static model requires an explicit root for multiple worlds"); root=object.index;
        }
    }
    require(root!=0 && root<impl_->objects.size() && impl_->objects[root].info.is_node,"invalid M3G static subtree root");
    M3GRenderAsset asset;auto& result=asset.geometry;result.root_object=root;
    std::vector<M3GMatrix> matrices(impl_->objects.size(),identity);
    std::vector<bool> visible(impl_->objects.size(),true),computed(impl_->objects.size(),false);
    std::vector<float> alpha(impl_->objects.size(),1.0F);
    const auto evaluate=[&](std::size_t index) {
        std::vector<std::size_t> chain;
        while(!computed[index]) {
            chain.push_back(index);
            const auto parent=impl_->objects[index].info.parent;
            if(!parent)break;
            index=*parent;
            require(chain.size()<=impl_->objects.size(),"cyclic M3G render hierarchy");
        }
        for(auto i=chain.rbegin();i!=chain.rend();++i) {
            const auto& info=impl_->objects[*i].info;
            matrices[*i]=m3g_transform_matrix(info.transform);
            visible[*i]=info.rendering_enabled;alpha[*i]=float(info.alpha_factor)/255.0F;
            if(info.parent) {
                matrices[*i]=m3g_multiply(matrices[*info.parent],matrices[*i]);
                visible[*i]=visible[*i] && visible[*info.parent];alpha[*i]*=alpha[*info.parent];
            }
            computed[*i]=true;
        }
    };
    std::vector<std::size_t> pending{root};
    while(!pending.empty()) {
        const auto index=pending.back();pending.pop_back();
        const auto& object=impl_->objects[index]; const auto& info=object.info;
        evaluate(index);result.animation_track_bindings+=info.animation_tracks.size();
        if(!stateful && !visible[index])continue;
        require(!stateful || !info.has_alignment,"runtime M3G alignment is outside the render-asset path");
        require(info.type!=15 && info.type!=16,"nonrigid M3G deformation is not implemented by the static mesh path");
        require(stateful || info.type!=18,"M3G Sprite3D is not implemented by the rigid mesh path");
        if(const auto* sprite=std::get_if<Sprite>(&object.body);sprite && sprite->appearance) {
            const auto& source=impl_->body<Appearance>(sprite->appearance);
            require(source.fog==0 && source.material==0 && source.polygon==0 && source.textures.empty(),
                    "Sprite3D render asset requires the recovered unlit appearance path");
            const auto& image=impl_->body<Image>(sprite->image);
            require(image.width && image.height && image.width<=1024 && image.height<=1024 &&
                    !(image.width&(image.width-1)) && !(image.height&(image.height-1)),
                    "Sprite3D render asset requires a power-of-two single image block");
            M3GSpriteAsset out;out.node_object=index;out.image_object=sprite->image;
            out.appearance_object=sprite->appearance;out.model=matrices[index];out.effective_alpha=alpha[index];
            out.scope=info.scope;out.scaled=sprite->scaled;out.crop_pixels=sprite->crop;out.image_format=image.format;
            out.appearance=impl_->appearance(sprite->appearance);out.appearance.culling=162;
            if(!result.images.count(sprite->image))result.images.emplace(sprite->image,decode_image(image));
            asset.traversal.push_back({M3GDrawableKind::sprite,asset.sprites.size()});
            asset.sprites.push_back(std::move(out));
        }
        if (const auto* mesh=std::get_if<Mesh>(&object.body); mesh && (stateful || info.scope!=0) &&
            std::any_of(mesh->submeshes.begin(),mesh->submeshes.end(),[](const auto& part) { return part[1]!=0; })) {
            const auto& buffer=impl_->body<VertexBuffer>(mesh->vertices);
            const auto& positions=impl_->body<VertexArray>(buffer.positions.array);
            require(positions.components==3,"M3G position array must have three components");
            const VertexArray* normals=buffer.normals ? &impl_->body<VertexArray>(buffer.normals) : nullptr;
            const VertexArray* colors=buffer.colors ? &impl_->body<VertexArray>(buffer.colors) : nullptr;
            require(!normals || (normals->components==3 && normals->count==positions.count),"M3G normals differ from positions");
            require(!colors || (colors->width==1 && colors->components>=3 && colors->components<=4 && colors->count==positions.count),
                    "M3G colors differ from positions");
            for (std::size_t part=0;part<mesh->submeshes.size();++part) {
                const auto& sub=mesh->submeshes[part]; if (sub[1]==0) continue;
                M3GSurface surface; surface.mesh_object=index; surface.submesh_index=part;
                surface.vertex_buffer_object=mesh->vertices; surface.index_buffer_object=sub[0]; surface.appearance_object=sub[1];
                surface.model=matrices[index]; surface.effective_alpha=alpha[index];surface.scope=info.scope;
                surface.vertex_color_components=colors ? colors->components : 0;
                surface.default_color_alpha=buffer.color[3];
                surface.appearance=impl_->appearance(sub[1]);
                const VertexArray* texcoords=nullptr;
                if (surface.appearance.texture) {
                    require(!buffer.texcoords.empty(),"textured M3G mesh has no texture coordinates");
                    texcoords=&impl_->body<VertexArray>(buffer.texcoords.front().array);
                    require((texcoords->components==2 || texcoords->components==3) && texcoords->count==positions.count,
                            "M3G texture coordinates differ from positions");
                    require(texcoords->components!=2 || buffer.texcoords.front().bias[2]==0,"2D M3G texture coordinate biasZ must be zero");
                    const auto image_id=surface.appearance.texture->image_object;
                    if (!result.images.count(image_id)) result.images.emplace(image_id,decode_image(impl_->body<Image>(static_cast<std::uint32_t>(image_id))));
                }
                const auto vertex=[&](std::uint32_t id) {
                    require(id<positions.count,"M3G triangle index exceeds vertex count");
                    M3GVertex value{{0,0,0,1},{0,0,0,0},buffer.color,{0,0,0,1}};
                    for (std::size_t component=0;component<3;++component) {
                        value.position[component]=float(positions.values[id*3U+component])*buffer.positions.scale+buffer.positions.bias[component];
                        if (normals) {
                            const auto raw=normals->values[id*3U+component];
                            value.normal[component]=(2.0F*float(raw)+1.0F)/(normals->width==1 ? 255.0F : 65535.0F);
                        }
                    }
                    if (colors) {
                        value.color[3]=1.0F;
                        for (std::size_t component=0;component<colors->components;++component)
                            value.color[component]=float(static_cast<std::uint8_t>(colors->values[id*colors->components+component]))/255.0F;
                    }
                    if (texcoords) {
                        const auto& coordinates=buffer.texcoords.front();
                        for (std::size_t component=0;component<texcoords->components;++component)
                            value.texcoord[component]=float(texcoords->values[id*texcoords->components+component])*coordinates.scale+coordinates.bias[component];
                        value.texcoord=transform_vector(surface.appearance.texture->transform,value.texcoord);
                    }
                    for (const auto& tuple:{value.position,value.normal,value.color,value.texcoord})
                        for (const auto f:tuple) require(std::isfinite(f),"decoded M3G vertex is nonfinite");
                    const auto world=transform_vector(surface.model,value.position);
                    require(world[3]!=0 && std::isfinite(world[3]),"M3G world vertex has invalid homogeneous coordinate");
                    for (std::size_t component=0;component<3;++component) {
                        const float coordinate=world[component]/world[3];
                        require(std::isfinite(coordinate),"M3G world vertex is nonfinite");
                        if (!result.has_geometry) result.minimum[component]=result.maximum[component]=coordinate;
                        else { result.minimum[component]=std::min(result.minimum[component],coordinate); result.maximum[component]=std::max(result.maximum[component],coordinate); }
                    }
                    result.has_geometry=true; return value;
                };
                const auto& indices=impl_->body<Indices>(sub[0]); std::size_t begin=0;
                for (const auto length:indices.strip_lengths) {
                    require(std::uint64_t(length-2U)*3U<=max_vertices-surface.vertices.size(),"M3G expanded surface vertex bound exceeded");
                    for (std::size_t triangle=0;triangle<length-2U;++triangle) {
                        const auto a=indices.values[begin+triangle+(triangle&1U)];
                        const auto b=indices.values[begin+triangle+1U-(triangle&1U)];
                        const auto c=indices.values[begin+triangle+2U];
                        surface.vertices.push_back(vertex(a)); surface.vertices.push_back(vertex(b)); surface.vertices.push_back(vertex(c));
                    }
                    begin+=length;
                }
                asset.traversal.push_back({M3GDrawableKind::mesh,result.surfaces.size()});
                result.surfaces.push_back(std::move(surface));
            }
        }
        for(auto child=info.children.rbegin();child!=info.children.rend();++child)pending.push_back(*child);
    }
    return asset;
}

} // namespace g1
