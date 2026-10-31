#include "g1/M3GScene.hpp"

#include <cmath>
#include <cstring>
#include <functional>
#include <iostream>
#include <zlib.h>

namespace {
using g1::Bytes;
int checks=0;
void check(bool condition,const char* message) {
    ++checks; if (!condition) throw std::runtime_error(message);
}
void rejects(const std::function<void()>& action,const char* message) {
    bool rejected=false; try { action(); } catch (const g1::FormatError&) { rejected=true; }
    check(rejected,message);
}
void byte(Bytes& out,std::uint8_t value) { out.push_back(value); }
void u16(Bytes& out,std::uint16_t value) { byte(out,std::uint8_t(value)); byte(out,std::uint8_t(value>>8U)); }
void u32(Bytes& out,std::uint32_t value) { u16(out,std::uint16_t(value)); u16(out,std::uint16_t(value>>16U)); }
void scalar(Bytes& out,float value) { std::uint32_t bits; std::memcpy(&bits,&value,4); u32(out,bits); }
void append(Bytes& out,const Bytes& bytes) { out.insert(out.end(),bytes.begin(),bytes.end()); }
Bytes object3d(std::uint32_t id=0) { Bytes out; u32(out,id); u32(out,0); u32(out,0); return out; }
Bytes node(bool enabled=true,std::uint8_t alpha=255,std::array<float,3> translation={},bool general=false,std::uint32_t scope=0xffffffffU) {
    auto out=object3d(); byte(out,1);
    for (const auto v:translation) scalar(out,v);
    for (int i=0;i<3;++i) scalar(out,1);
    for (int i=0;i<4;++i) scalar(out,0);
    byte(out,general ? 1 : 0);
    if (general) {
        // Serialized row-major translation, not already column-major data.
        const std::array<float,16> matrix{{1,0,0,0.25F, 0,1,0,0.5F, 0,0,1,0.75F, 0,0,0,1}};
        for (const auto v:matrix) scalar(out,v);
    }
    byte(out,enabled ? 1 : 0); byte(out,1); byte(out,alpha); u32(out,scope); byte(out,0);
    return out;
}
Bytes vertex_array(const std::vector<std::int16_t>& values,std::uint8_t width,std::uint8_t components,bool delta) {
    auto out=object3d(); byte(out,width); byte(out,components); byte(out,delta ? 1 : 0);
    u16(out,static_cast<std::uint16_t>(values.size()/components));
    std::array<std::uint32_t,4> prior{}; const std::uint32_t mask=width==1 ? 255U : 65535U;
    for (std::size_t i=0;i<values.size();++i) {
        const auto value=std::uint32_t(std::int32_t(values[i]))&mask;
        const auto encoded=(value-(delta ? prior[i%components] : 0U))&mask; prior[i%components]=value;
        if (width==1) byte(out,static_cast<std::uint8_t>(encoded)); else u16(out,static_cast<std::uint16_t>(encoded));
    }
    return out;
}
Bytes chunk(std::uint8_t type,const Bytes& payload) { Bytes out; byte(out,type); u32(out,static_cast<std::uint32_t>(payload.size())); append(out,payload); return out; }
Bytes section(const Bytes& raw) {
    Bytes out; byte(out,0); u32(out,static_cast<std::uint32_t>(raw.size()+13)); u32(out,static_cast<std::uint32_t>(raw.size())); append(out,raw);
    const auto sum=adler32(1,out.data(),static_cast<uInt>(out.size())); u32(out,static_cast<std::uint32_t>(sum)); return out;
}
Bytes file(const std::vector<std::pair<std::uint8_t,Bytes>>& objects) {
    Bytes raw; for (const auto& obj:objects) append(raw,chunk(obj.first,obj.second));
    const auto scene=section(raw); const auto length=static_cast<std::uint32_t>(12U+30U+scene.size());
    Bytes header{1,0,0}; u32(header,length); u32(header,length); byte(header,0);
    Bytes out{0xab,'J','S','R','1','8','4',0xbb,13,10,26,10}; append(out,section(chunk(0,header))); append(out,scene); return out;
}
struct Options {
    bool delta{},short_positions{},colors{true},normals{},texture{},palette{true},rendering{true},general{};
    bool missing_positions{},null_appearance{},duplicate_child{},bad_index{},bad_reference{},mutable_image{},bad_palette{};
    bool negative_zero{};
    std::uint8_t image_format{99},position_components{3};
    std::uint32_t scope{0xffffffffU};
    std::vector<std::int16_t> positions{0,0,0, 2,0,0, 0,2,0, 2,2,0};
};
Bytes fixture(const Options& o={}) {
    std::vector<std::pair<std::uint8_t,Bytes>> objects;
    const auto add=[&](std::uint8_t type,Bytes data) {
        objects.emplace_back(type,std::move(data)); return static_cast<std::uint32_t>(objects.size()+1);
    };
    auto position_values=o.positions;
    if (o.position_components==4) {
        position_values.clear();
        for (std::size_t i=0;i<4;++i) { for (std::size_t c=0;c<3;++c) position_values.push_back(o.positions[i*3+c]); position_values.push_back(0); }
    }
    const auto positions=add(20,vertex_array(position_values,o.short_positions ? 2 : 1,o.position_components,o.delta));
    const auto colors=o.colors ? add(20,vertex_array({-1,-128,0, -1,0,0, 0,-1,0, 0,0,-1},1,3,false)) : 0U;
    const auto normals=o.normals ? add(20,vertex_array({127,-128,0, 127,-128,0, 127,-128,0, 127,-128,0},1,3,false)) : 0U;
    const auto uv=o.texture ? add(20,vertex_array({0,0, 1,0, 0,1, 1,1},1,2,false)) : 0U;
    auto buffer=object3d(); append(buffer,{32,64,128,128}); u32(buffer,o.bad_reference ? 999U : o.missing_positions ? 0U : positions);
    scalar(buffer,o.negative_zero ? -0.0F : 1.0F); scalar(buffer,2); scalar(buffer,3); scalar(buffer,0.5F);
    u32(buffer,normals); u32(buffer,colors); u32(buffer,o.texture ? 1U : 0U);
    if (o.texture) { u32(buffer,uv); for (int i=0;i<3;++i) scalar(buffer,0); scalar(buffer,1); }
    const auto vertices=add(21,buffer);
    auto indices=object3d(); byte(indices,129); u32(indices,4); append(indices,{0,1,2,static_cast<std::uint8_t>(o.bad_index ? 7 : 3)}); u32(indices,1); u32(indices,4);
    const auto strip=add(11,indices);
    std::uint32_t texture=0;
    if (o.texture) {
        auto image=object3d(); byte(image,o.image_format); byte(image,o.mutable_image ? 1 : 0); u32(image,2); u32(image,1);
        const std::map<std::uint8_t,Bytes> pixels{{96,{64,128}},{97,{16,240}},{98,{16,64,240,128}},
                                               {99,{255,0,0,0,255,0}},{100,{255,0,0,64,0,255,0,128}}};
        const auto& data=pixels.at(o.image_format);
        if (!o.mutable_image) {
            u32(image,o.palette ? static_cast<std::uint32_t>(data.size()) : 0U); if (o.palette) append(image,data);
            u32(image,o.palette ? 2U : static_cast<std::uint32_t>(data.size()));
            if (o.palette) append(image,{0,static_cast<std::uint8_t>(o.bad_palette ? 255 : 1)}); else append(image,data);
        }
        const auto image_id=add(10,image);
        auto tex=object3d(); byte(tex,0); byte(tex,0); u32(tex,image_id); append(tex,{0,0,0,228,241,241,208,210}); texture=add(17,tex);
    }
    auto appearance=object3d(); byte(appearance,0); for (int i=0;i<4;++i) u32(appearance,0); u32(appearance,o.texture ? 1U : 0U); if (o.texture) u32(appearance,texture);
    const auto style=add(3,appearance);
    auto mesh=node(o.rendering,128,{},o.general,o.scope); u32(mesh,vertices); u32(mesh,1); u32(mesh,strip); u32(mesh,o.null_appearance ? 0U : style);
    const auto mesh_id=add(14,mesh);
    auto group=node(true,128,{{2,3,4}}); u32(group,o.duplicate_child ? 2U : 1U); u32(group,mesh_id); if (o.duplicate_child) u32(group,mesh_id);
    const auto group_id=add(9,group);
    auto world=node(); u32(world,1); u32(world,group_id); u32(world,0); u32(world,0); add(22,world);
    return file(objects);
}
} // namespace

int main() {
    try {
        const auto basic=g1::M3GScene(fixture()).static_model();
        check(basic.surfaces.size()==1 && basic.surfaces[0].vertices.size()==6,"strip expansion count");
        const auto& surface=basic.surfaces[0];
        check(surface.vertices[0].position==std::array<float,4>{{1,2,3,1}},"position scale/bias");
        check(surface.vertices[3].position==std::array<float,4>{{1,3,3,1}} &&
              surface.vertices[4].position==std::array<float,4>{{2,2,3,1}},"odd strip winding");
        check(surface.vertices[0].color[0]==1 && surface.vertices[0].color[1]==128.0F/255.0F &&
              surface.vertices[0].color[3]==1,"unsigned RGB colors ignore default alpha");
        check(std::abs(surface.effective_alpha-(128.0F/255.0F)*(128.0F/255.0F))<1e-7F,"hierarchical alpha");
        check(basic.minimum==std::array<float,3>{{3,5,7}} && basic.maximum==std::array<float,3>{{4,6,7}},"parent matrix and bounds");
        Options options; options.delta=true; options.positions={127,0,0,-128,0,0,126,0,0,0,0,0};
        const auto delta=g1::M3GScene(fixture(options)).static_model();
        check(delta.surfaces[0].vertices[0].position[0]==64.5F && delta.surfaces[0].vertices[1].position[0]==-63,"8-bit delta overflow");
        options.short_positions=true; options.positions={32767,0,0,-32768,0,0,32766,0,0,0,0,0};
        const auto short_delta=g1::M3GScene(fixture(options)).static_model();
        check(short_delta.surfaces[0].vertices[1].position[0]==-16383,"16-bit signed delta overflow");
        options={}; options.normals=true;
        const auto normals=g1::M3GScene(fixture(options)).static_model().surfaces[0].vertices[0].normal;
        check(normals[0]==1 && normals[1]==-1 && normals[2]==1.0F/255.0F,"normal signed-range mapping");
        options={}; options.colors=false;
        check(g1::M3GScene(fixture(options)).static_model().surfaces[0].vertices[0].color[3]==128.0F/255.0F,"default RGBA alpha");
        options={}; options.general=true;
        const auto translated=g1::M3GScene(fixture(options)).static_model();
        check(translated.minimum==std::array<float,3>{{3.25F,5.5F,7.75F}},"serialized row-major matrix conversion");
        options={}; options.rendering=false;
        check(g1::M3GScene(fixture(options)).render_asset().geometry.surfaces.size()==1,
              "stateful asset discarded hidden valid mesh geometry");
        options={}; options.scope=0;
        check(g1::M3GScene(fixture(options)).static_model().surfaces.empty(),"static zero-scope mesh was not skipped");
        check(g1::M3GScene(fixture(options)).render_asset().geometry.surfaces.size()==1,
              "stateful asset discarded zero-scope mesh metadata");
        options={}; options.rendering=false; options.missing_positions=true;
        check(!g1::M3GScene(fixture(options)).static_model().has_geometry,"disabled node skips unavailable geometry");
        options={}; options.null_appearance=true; options.missing_positions=true;
        check(!g1::M3GScene(fixture(options)).static_model().has_geometry,"null appearance disables submesh");
        for (std::uint8_t format=96;format<=100;++format) for (const bool palette:{false,true}) {
            options={}; options.texture=true; options.image_format=format; options.palette=palette;
            const auto model=g1::M3GScene(fixture(options)).static_model();
            check(model.images.size()==1 && model.images.begin()->second.pixels.size()==8,"M3G image expansion");
            const auto& pixels=model.images.begin()->second.pixels;
            if (format==96) check(pixels[0]==255 && pixels[3]==64 && pixels[7]==128,"ALPHA image channel mapping");
            if (format==99) check(pixels==Bytes({255,0,0,255,0,255,0,255}),"RGB palette/direct bytes");
            check(model.surfaces[0].appearance.texture->function==228 &&
                  model.surfaces[0].appearance.texture->image_filter==210,"original texture state");
            const auto& identity=*model.surfaces[0].appearance.texture;
            check(identity.texture_object==identity.image_object+1 && identity.texture_object!=0,
                  "serialized Texture2D identity was discarded");
        }
        options={}; options.bad_reference=true; rejects([&]{g1::M3GScene scene(fixture(options));},"forward reference accepted");
        options={}; options.duplicate_child=true; rejects([&]{g1::M3GScene scene(fixture(options));},"duplicate parent accepted");
        options={}; options.bad_index=true; rejects([&]{g1::M3GScene(fixture(options)).static_model();},"out-of-range index accepted");
        options={}; options.position_components=4; rejects([&]{g1::M3GScene(fixture(options)).static_model();},"four-component positions accepted");
        options={}; options.negative_zero=true; rejects([&]{g1::M3GScene scene(fixture(options));},"invalid serialized Float32 accepted");
        options={}; options.texture=true; options.mutable_image=true;
        rejects([&]{g1::M3GScene(fixture(options)).static_model();},"mutable image invented pixels");
        options={}; options.texture=true; options.bad_palette=true;
        rejects([&]{g1::M3GScene(fixture(options)).static_model();},"out-of-range palette index accepted");
        auto truncated=fixture(); truncated.pop_back(); rejects([&]{g1::M3GScene scene(truncated);},"truncated section accepted");
        g1::M3GTransform transform; transform.translation={{1,2,3}}; transform.scale={{2,3,4}};
        transform.angle_degrees=90; transform.axis={{0,0,2}}; transform.general[12]=1;
        const auto matrix=g1::m3g_transform_matrix(transform);
        check(std::abs(matrix[12]-1)<1e-5F && std::abs(matrix[13]-4)<1e-5F && matrix[14]==3,"TRSM component order");
        transform.axis={}; rejects([&]{g1::m3g_transform_matrix(transform);},"nonzero rotation with zero axis accepted");
        std::cout << checks << " native M3G scene contracts passed\n"; return 0;
    } catch (const std::exception& error) {
        std::cerr << "FAIL: " << error.what() << '\n'; return 1;
    }
}
