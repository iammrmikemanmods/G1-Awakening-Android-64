#include "g1/M3GMaterialOverrides.hpp"

#include <algorithm>
#include <fstream>
#include <functional>
#include <iostream>
#include <sstream>
#include <string>
#include <zlib.h>

namespace {
std::size_t checks=0;
void check(bool value,const char* detail) {
    ++checks;if(!value)throw std::runtime_error(detail);
}
template<class F> void rejects(F&& operation,const char* detail) {
    bool rejected=false;
    try { operation(); }catch(const g1::FormatError&) { rejected=true; }
    check(rejected,detail);
}
using Object=g1::M3GMaterialBindingObject;
Object object(std::size_t id,std::uint8_t type,std::uint32_t key=0,
              std::vector<std::size_t> references={},std::vector<std::size_t> children={},
              std::vector<std::size_t> textures={},std::vector<std::size_t> appearances={},std::size_t image=0) {
    return {id,type,key,std::move(references),std::move(children),std::move(textures),std::move(appearances),image};
}
std::vector<Object> graph() {
    return {object(1,9,0,{2},{2}),object(2,9,0,{3},{3}),object(3,14,0,{4,7},{},{},{4,7}),
            object(4,3,500033589U,{5},{},{5}),object(5,17,0,{6},{},{},{},6),object(6,10),
            object(7,3,253263228U,{8},{},{8}),object(8,17,0,{9},{},{},{},9),object(9,10)};
}
void tag(std::istream& input,const char* expected) {
    std::string text;if(!(input>>text) || text!=expected)throw std::runtime_error("invalid material fixture tag");
}
std::vector<std::size_t> values(std::istream& input) {
    std::size_t size;if(!(input>>size) || size>1000000)throw std::runtime_error("invalid material fixture array");
    std::vector<std::size_t> result(size);
    for(auto& value:result)if(!(input>>value))throw std::runtime_error("truncated material fixture array");
    return result;
}
std::optional<std::size_t> optional(std::size_t value) {
    return value ? std::optional<std::size_t>(value) : std::nullopt;
}
void original_cases(const char* path) {
    std::ifstream input(path);if(!input)throw std::runtime_error("cannot open material fixture");
    tag(input,"G1_M3G_MATERIAL_V1");std::size_t cases;
    if(!(input>>cases) || cases!=40)throw std::runtime_error("incomplete material fixture");
    for(std::size_t i=0;i<cases;++i) {
        tag(input,"CASE");std::string name;std::uint32_t type;std::size_t root,count,reject,steps;
        if(!(input>>name>>type>>root>>count>>reject>>steps) || count>1000000 || reject>1 || steps>2)
            throw std::runtime_error("invalid material fixture case");
        std::vector<Object> objects;objects.reserve(count);
        for(std::size_t j=0;j<count;++j) {
            tag(input,"O");Object row;unsigned kind;
            if(!(input>>row.index>>kind>>row.user_id>>row.texture_image) || kind>255)
                throw std::runtime_error("invalid material fixture object");
            row.type=std::uint8_t(kind);row.references=values(input);row.children=values(input);
            row.texture_units=values(input);row.submesh_appearances=values(input);objects.push_back(std::move(row));
        }
        std::vector<g1::M3GTextureImageOverride> expected;
        for(std::size_t j=0;j<steps;++j) {
            tag(input,"S");g1::M3GTextureImageOverride row;unsigned slot,fallback;
            std::size_t named,texture,image;
            if(!(input>>slot>>row.resource_id>>named>>row.appearance_object>>texture>>image>>fallback) || slot>1 || fallback>1)
                throw std::runtime_error("invalid material fixture step");
            row.slot=g1::M3GMaterialSlot(slot);row.named_object=optional(named);row.texture_object=optional(texture);
            row.authored_image_object=optional(image);row.used_mesh_fallback=fallback!=0;expected.push_back(row);
        }
        tag(input,"END");
        const g1::M3GMaterialBindings bindings(std::move(objects));
        if(reject) { rejects([&](){(void)bindings.plan(root,type);},"original rejected material structure was accepted");continue; }
        const auto actual=bindings.plan(root,type);check(actual.size()==expected.size(),"original material step count differs");
        for(std::size_t j=0;j<expected.size();++j) {
            const auto& a=actual[j];const auto& e=expected[j];
            check(a.slot==e.slot,"original material slot differs");
            check(a.resource_id==e.resource_id,"original material resource differs");
            check(a.named_object==e.named_object,"original named object differs");
            check(a.appearance_object==e.appearance_object,"original fallback Appearance differs");
            check(a.texture_object==e.texture_object,"original Texture2D identity differs");
            check(a.authored_image_object==e.authored_image_object,"authored image identity differs");
            check(a.used_mesh_fallback==e.used_mesh_fallback,"original fallback choice differs");
        }
    }
    std::string trailing;check(!(input>>trailing),"material fixture has trailing cases");
}

void big_word(g1::Bytes& out,std::uint32_t value) {
    for(int shift=24;shift>=0;shift-=8)out.push_back(std::uint8_t(value>>unsigned(shift)));
}
void chunk(g1::Bytes& out,const char* name,const g1::Bytes& data) {
    big_word(out,std::uint32_t(data.size()));const auto begin=out.size();
    out.insert(out.end(),name,name+4);out.insert(out.end(),data.begin(),data.end());
    big_word(out,std::uint32_t(crc32(0,out.data()+begin,uInt(data.size()+4))));
}
g1::Bytes png(unsigned width,unsigned height,std::uint8_t color,bool transparent=false) {
    g1::Bytes out{137,80,78,71,13,10,26,10},header,raw;
    big_word(header,width);big_word(header,height);header.insert(header.end(),{8,color,0,0,0});chunk(out,"IHDR",header);
    if(color==3)chunk(out,"PLTE",{1,2,3,9,8,7});
    if(transparent)chunk(out,"tRNS",color==3 ? g1::Bytes{128,255} : g1::Bytes{0,1,0,2,0,3});
    for(unsigned y=0;y<height;++y) {
        raw.push_back(0);
        for(unsigned x=0;x<width;++x) {
            const auto alternate=(x+y)%2;
            if(color==3)raw.push_back(std::uint8_t(alternate));
            else {
                if(alternate)raw.insert(raw.end(),{9,8,7});else raw.insert(raw.end(),{1,2,3});
                if(color==6)raw.push_back(255);
            }
        }
    }
    uLongf size=compressBound(uLong(raw.size()));g1::Bytes compressed(size);
    if(compress2(compressed.data(),&size,raw.data(),uLong(raw.size()),Z_BEST_SPEED)!=Z_OK)
        throw std::runtime_error("synthetic PNG compression failed");
    compressed.resize(size);chunk(out,"IDAT",compressed);chunk(out,"IEND",{});return out;
}

void contracts() {
    rejects([](){g1::M3GMaterialBindings b(std::vector<Object>{});},"empty graph accepted");
    rejects([](){auto g=graph();g.push_back(g[0]);g1::M3GMaterialBindings b(g);},"duplicate identity accepted");
    rejects([](){auto g=graph();g[0].index=0;g1::M3GMaterialBindings b(g);},"null identity accepted");
    rejects([](){auto g=graph();g[0].type=15;g1::M3GMaterialBindings b(g);},"unrecovered skin graph accepted");
    rejects([](){auto g=graph();g[0].references.push_back(99);g1::M3GMaterialBindings b(g);},"dangling reference accepted");
    rejects([](){auto g=graph();g[3].references.clear();g1::M3GMaterialBindings b(g);},"missing typed edge accepted");
    rejects([](){auto g=graph();g[3].texture_units={6};g[3].references={6};g1::M3GMaterialBindings b(g);},"nontexture unit accepted");
    rejects([](){auto g=graph();g[4].texture_image=0;g[4].references={0};g1::M3GMaterialBindings b(g);},"null texture image accepted");
    rejects([](){auto g=graph();g[4].texture_image=3;g[4].references={3};g1::M3GMaterialBindings b(g);},"nonimage texture binding accepted");
    rejects([](){auto g=graph();g[0].children.push_back(3);g[0].references.push_back(3);g1::M3GMaterialBindings b(g);},"multiple Node parents accepted");
    rejects([](){auto g=graph();g[0].children.push_back(2);g1::M3GMaterialBindings b(g);},"duplicate child accepted");
    rejects([](){auto g=graph();g[0].children={4};g[0].references={4};g1::M3GMaterialBindings b(g);},"nonNode child accepted");
    rejects([](){auto g=graph();g[5].references={4};g1::M3GMaterialBindings b(g);},"Object3D reference cycle accepted");
    rejects([](){auto g=graph();g[0].children={1};g[0].references={1};g1::M3GMaterialBindings b(g);},"Node self cycle accepted");
    rejects([](){auto g=graph();g[3].texture_units={5,5,5};g1::M3GMaterialBindings b(g);},"unsupported third texture unit accepted");
    rejects([](){auto g=graph();g[6].user_id=500033589U;const g1::M3GMaterialBindings b(g);(void)b.plan(1,3);},"ambiguous name traversal guessed");
    const g1::M3GMaterialBindings bindings(graph());
    rejects([&](){(void)bindings.plan(99,3);},"missing root accepted");
    rejects([&](){(void)bindings.plan(3,3);},"nonGroup root accepted");
    rejects([&](){(void)bindings.plan(1,24);},"out-of-range effective type accepted");
    rejects([](){(void)g1::m3g_material_image_resources(0xffffffffU);},"negative effective type bits accepted");
    auto reverse=graph();std::reverse(reverse.begin(),reverse.end());
    check(g1::M3GMaterialBindings(reverse).plan(1,3)[0].texture_object==5,"file storage order changed explicit graph result");

    std::vector<Object> deep;
    constexpr std::size_t depth=20000;
    deep.reserve(depth+4);
    for(std::size_t i=1;i<=depth;++i)deep.push_back(object(i,9,0,{i+1},{i+1}));
    deep.push_back(object(depth+1,14,0,{depth+2},{},{},{depth+2}));
    deep.push_back(object(depth+2,3,0,{depth+3},{},{depth+3}));
    deep.push_back(object(depth+3,17,0,{depth+4},{},{},{},depth+4));deep.push_back(object(depth+4,10));
    const auto deep_plan=g1::M3GMaterialBindings(std::move(deep)).plan(1,6);
    check(deep_plan.size()==1 && deep_plan[0].used_mesh_fallback && deep_plan[0].texture_object==depth+3,
          "deep fallback chain failed iterative traversal");

    const auto palette=png(256,128,3);const auto rgb=png(256,128,2);
    const auto first=g1::decode_m3g_material_image(686,palette),second=g1::decode_m3g_material_image(1270,rgb);
    check(first.resource_id==686 && first.image_format==99,"palette image format differs from original RGB");
    check(first.image.width==256 && first.image.height==128,"palette image dimensions differ");
    check(first.image.pixels.size()==256*128*4,"palette image decoded length differs");
    check(first.image.pixels==second.image.pixels,"palette and RGB sample expansion differ");
    check(first.image.pixels[0]==1 && first.image.pixels[3]==255 && first.image.pixels[256*4]==9,
          "image row order or alpha differs");
    rejects([&](){(void)g1::decode_m3g_material_image(999,palette);},"unknown replacement resource accepted");
    rejects([](){(void)g1::decode_m3g_material_image(686,{});},"empty replacement PNG accepted");
    rejects([&](){(void)g1::decode_m3g_material_image(686,rgb);},"changed authored PNG encoding accepted");
    rejects([](){(void)g1::decode_m3g_material_image(686,png(32,128,3));},"changed replacement dimensions accepted");
    rejects([](){(void)g1::decode_m3g_material_image(686,png(256,128,3,true));},"palette transparency silently made RGB");
    rejects([](){(void)g1::decode_m3g_material_image(1270,png(256,128,2,true));},"RGB transparent key silently discarded");
    rejects([](){(void)g1::decode_m3g_material_image(1270,png(256,128,6));},"RGBA encoding silently labelled RGB");
    rejects([&](){auto bad=palette;bad.back()^=1;(void)g1::decode_m3g_material_image(686,bad);},"PNG checksum corruption accepted");
    rejects([&](){auto bad=palette;bad.resize(bad.size()-7);(void)g1::decode_m3g_material_image(686,bad);},"truncated PNG accepted");
    rejects([&](){auto bad=palette;bad.push_back(0);(void)g1::decode_m3g_material_image(686,bad);},"trailing partial PNG chunk accepted");
}
}

int main(int argc,char** argv) {
    try {
        if(argc!=2)throw std::runtime_error("usage: g1_m3g_material_overrides_tests fixture.txt");
        original_cases(argv[1]);const auto original_checks=checks;contracts();
        std::cout<<"{\"original_cases\":40,\"original_checks\":"<<original_checks
                 <<",\"contract_checks\":"<<checks-original_checks<<",\"checks\":"<<checks<<",\"all_passed\":true}\n";
        return 0;
    }catch(const std::exception& error) { std::cerr<<"ERROR: "<<error.what()<<'\n';return 1; }
}
