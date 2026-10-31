#include "g1/BitmapFont.hpp"
#include "../research/font_recovery/original_vectors.hpp"

#include <array>
#include <cstdlib>
#include <iostream>
#include <stdexcept>

namespace {
std::size_t checks{};

void require(bool condition, const char* message) {
    ++checks;
    if (!condition) throw std::runtime_error(message);
}

template<typename Function>
void rejects(Function function) {
    bool rejected{};
    try { function(); } catch (const g1::FormatError&) { rejected = true; }
    require(rejected, "malformed or unsupported font input was accepted");
}

void put16(g1::Bytes& bytes,std::size_t at,std::uint16_t value) {
    bytes.at(at)=std::uint8_t(value);bytes.at(at+1)=std::uint8_t(value>>8U);
}

void put32(g1::Bytes& bytes,std::size_t at,std::uint32_t value) {
    for(std::size_t i=0;i<4;++i)bytes.at(at+i)=std::uint8_t(value>>(i*8U));
}

g1::Bytes bitmap(unsigned depth,bool top_down,bool trailer) {
    const unsigned stride=64U*depth/8U;
    g1::Bytes bytes(70U+stride*32U+(trailer?2U:0U));
    bytes[0]='B';bytes[1]='M';put32(bytes,2,static_cast<std::uint32_t>(bytes.size()));
    put32(bytes,10,70);put32(bytes,14,40);put32(bytes,18,64);
    put32(bytes,22,top_down ? 0xffffffe0U : 32U);
    put16(bytes,26,1);put16(bytes,28,static_cast<std::uint16_t>(depth));
    put32(bytes,34,stride*32U+(trailer?2U:0U));put32(bytes,46,4);
    put32(bytes,54,0x00ff00ffU);put32(bytes,58,0x00ffffffU);
    put32(bytes,62,0x00112233U);put32(bytes,66,0x00998877U);
    for(unsigned y=0;y<32;++y)for(unsigned x=0;x<64;++x) {
        const auto index=std::uint8_t((x+y)%4U);
        if(depth==8)bytes[70U+y*stride+x]=index;
        else bytes[70U+y*stride+x/2U] |= std::uint8_t(index<<((x%2U)==0?4U:0U));
    }
    return bytes;
}

g1::Archive synthetic_archive(const g1::Bytes& image,g1::Bytes properties={}) {
    if(properties.empty()) {
        properties.resize(24);
        put32(properties,0,6);put32(properties,4,0xffffffffU);
        put32(properties,8,9);put32(properties,12,0x00ff00ffU);
        put32(properties,16,14);put32(properties,20,0x020001e3U);
    }
    const std::array<g1::Bytes,3> payloads{{image,properties,g1_font_fixture::descriptors.front()}};
    const std::array<std::uint32_t,3> ids{{483,608,1134}},keys{{0xb717945fU,0xe179f4beU,0xf4e02223U}};
    const std::uint32_t members=56,body=88;
    g1::Bytes bytes(body);
    put32(bytes,0,0x42494746U);put32(bytes,4,0x00800001U);put32(bytes,8,32);put32(bytes,12,3);
    put32(bytes,16,members);put32(bytes,20,3);put32(bytes,24,body);
    for(std::size_t i=0;i<3;++i) {
        put32(bytes,32U+i*8U,ids[i]);put16(bytes,36U+i*8U,1);put16(bytes,38U+i*8U,static_cast<std::uint16_t>(i));
        put32(bytes,members+i*8U,keys[i]);put32(bytes,members+i*8U+4U,static_cast<std::uint32_t>(bytes.size()));
        bytes.insert(bytes.end(),{4,0,0,0});bytes.insert(bytes.end(),payloads[i].begin(),payloads[i].end());
    }
    put32(bytes,members+3U*8U,0);put32(bytes,members+3U*8U+4U,static_cast<std::uint32_t>(bytes.size()));
    put32(bytes,28,static_cast<std::uint32_t>(bytes.size())-body);
    return g1::Archive(std::move(bytes));
}

void bitmap_contract() {
    const std::array<std::array<std::uint8_t,4>,4> palette{{
        {{255,0,255,0}},{{255,255,255,255}},{{17,34,51,255}},{{153,136,119,255}}
    }};
    for(const auto depth:{4U,8U})for(const auto top_down:{false,true})for(const auto trailer:{false,true}) {
        const auto assets=g1::load_bitmap_font_assets(synthetic_archive(bitmap(depth,top_down,trailer)),0);
        require(assets.image.width==64&&assets.image.height==32,"synthetic font BMP dimensions mismatch");
        for(unsigned y=0;y<32;++y)for(unsigned x=0;x<64;++x) {
            const auto& color=palette[(x+(top_down?y:31U-y))%4U];
            for(std::size_t c=0;c<4;++c)
                require(assets.image.pixels[(y*64U+x)*4U+c]==color[c],"font BMP row/palette/key mismatch");
        }
    }
    auto bytes=bitmap(8,false,true);
    for(std::size_t length=0;length<bytes.size();++length) {
        const g1::Bytes truncated(bytes.begin(),bytes.begin()+static_cast<std::ptrdiff_t>(length));
        rejects([&]{g1::load_bitmap_font_assets(synthetic_archive(truncated),0);});
    }
    for(const auto& field:std::array<std::pair<std::size_t,std::uint32_t>,10>{{
        {6,1},{10,54},{14,108},{18,8193},{22,0},{30,1},{34,1},{46,257},{50,5},{54,0x12345678U}
    }}) {
        auto changed=bytes;put32(changed,field.first,field.second);
        if(field.first==54) {
            // A different RGB key is opaque; a reserved high byte does not
            // change palette RGB or turn it into source alpha.
            const auto decoded=g1::load_bitmap_font_assets(synthetic_archive(changed),0);
            require(decoded.image.pixels[(31U*64U)*4U+3U]==255,"font palette reserved byte became alpha");
        } else rejects([&]{g1::load_bitmap_font_assets(synthetic_archive(changed),0);});
    }
    auto changed=bytes;changed.back()=1;
    rejects([&]{g1::load_bitmap_font_assets(synthetic_archive(changed),0);});
    changed=bytes;put16(changed,28,24);
    rejects([&]{g1::load_bitmap_font_assets(synthetic_archive(changed),0);});
    changed=bytes;changed[70]=5;
    rejects([&]{g1::load_bitmap_font_assets(synthetic_archive(changed),0);});
    changed=bytes;put32(changed,54,0x12ff00ffU);
    const auto high_key=g1::load_bitmap_font_assets(synthetic_archive(changed),0);
    require(high_key.image.pixels[(31U*64U)*4U+3U]==0,"font key comparison used high palette byte");
}

void source_assets(const char* path) {
    const g1::Archive archive(g1::read_file(path));
    const std::array<unsigned,12> counts{{85,90,87,10,136,138,142,142,141,13,13,20}};
    const std::array<unsigned,12> widths{{182,232,161,203,244,245,227,227,182,94,94,168}};
    const std::array<unsigned,12> heights{{51,26,20,90,45,45,84,84,51,14,14,44}};
    const std::array<std::uint32_t,12> checksums{{0xf1a95d47U,0x5a007921U,0xb42c4194U,0xa8ad46e9U,
        0,0,0,0,0,0,0,0x2874ad3aU}};
    std::size_t total_glyphs{};
    for (std::size_t slot=0; slot<counts.size(); ++slot) {
        const auto binding=g1::bitmap_font_bindings()[slot];
        const auto records=archive.find(binding.descriptor_resource_id,0xf4e02223U);
        require(records.size()==1,"original font descriptor identity mismatch");
        const g1::BitmapFontDescriptor font(archive.read(records.front()),widths[slot],heights[slot]);
        require(font.glyphs().size()==counts[slot],"original glyph inventory mismatch");
        require(font.blanks().size()==2,"original blank inventory mismatch");
        total_glyphs+=font.glyphs().size();
        if(checksums[slot]) {
            const auto assets=g1::load_bitmap_font_assets(archive,slot);
            require(assets.image.width==widths[slot]&&assets.image.height==heights[slot],"original font image dimensions mismatch");
            require(g1::adler32_bytes(assets.image.pixels)==checksums[slot],"independent keyed original font pixels mismatch");
            for (const auto& glyph:assets.descriptor.glyphs())
                require(assets.descriptor.contains(glyph.character),"original font glyph lookup failed");
        } else rejects([&]{g1::load_bitmap_font_assets(archive,slot);});
    }
    const auto support=g1::load_bitmap_font_assets(archive,2);
    for(const auto c:g1::bitmap_font_utf8("OPTIMUS PRIME +25%"))
        require(support.descriptor.contains(c),"support banner coverage mismatch");
    rejects([&]{g1::load_bitmap_font_assets(archive,12);});
    std::cout<<"original font assets: "<<counts.size()<<" descriptors, "<<total_glyphs
             <<" glyph records, 5 keyed BMP sheets\n";
}

} // namespace

int main(int argc,char** argv) {
    try {
        std::vector<g1::BitmapFontDescriptor> fonts;
        for(const auto& bytes:g1_font_fixture::descriptors)fonts.emplace_back(bytes,64,32);
        const std::array<std::int32_t,4> line_height{{11,28,26,16}};
        const std::array<std::int32_t,4> leading{{1,1,-5,1}};
        for(std::size_t i=0;i<fonts.size();++i) {
            require(fonts[i].metrics().line_height==line_height[i],"original metric +8 mismatch");
            require(fonts[i].metrics().leading==leading[i],"original metric +12 mismatch");
            require(fonts[i].advance(U'!')==2,"original blank lookup must precede glyph lookup");
            require(fonts[i].advance(U'B')==7,"original duplicate midpoint selection changed");
            require(fonts[i].advance(U'a')==0,"missing glyph acquired invented advance");
        }
        std::size_t original_draws{};
        for(const auto& expected:g1_font_fixture::cases) {
            const auto& font=fonts.at(expected.profile);
            const std::u32string text(expected.characters.begin(),expected.characters.end());
            const auto measured=font.measure(text,expected.max_width);
            require(measured.width==expected.width,"original string width mismatch");
            require(measured.characters==expected.accepted,"original width-fit character count mismatch");
            const auto draws=font.layout(text,expected.x,expected.y,expected.max_width,expected.max_height);
            require(draws.size()==expected.draws.size(),"original glyph draw count mismatch");
            original_draws+=draws.size();
            for(std::size_t i=0;i<draws.size();++i) {
                const auto& got=draws[i];const auto& want=expected.draws[i];
                require(got.source_x==want.sx&&got.source_y==want.sy,"original glyph crop origin mismatch");
                require(got.width==want.width&&got.height==want.height,"original limited crop dimensions mismatch");
                require(got.x==want.x&&got.y==want.y,"original glyph draw offset mismatch");
            }
        }
        const auto original=g1_font_fixture::descriptors.front();
        for(std::size_t size=0;size<original.size();++size) {
            const g1::Bytes truncated(original.begin(),original.begin()+static_cast<std::ptrdiff_t>(size));
            rejects([&]{g1::BitmapFontDescriptor font(truncated,64,32);});
        }
        auto invalid=original;invalid[0]=1;
        rejects([&]{g1::BitmapFontDescriptor font(invalid,64,32);});
        invalid=original;invalid.push_back(0);
        rejects([&]{g1::BitmapFontDescriptor font(invalid,64,32);});
        rejects([&]{g1::BitmapFontDescriptor font(original,1,1);});
        invalid=original;invalid[8]=0xff;invalid[9]=0x7f;
        rejects([&]{g1::BitmapFontDescriptor font(invalid,64,32);});
        invalid=original;invalid[12]=0xff;invalid[13]=0xff;
        rejects([&]{g1::BitmapFontDescriptor font(invalid,64,32);});
        const auto utf8=g1::bitmap_font_utf8("A\xc3\x89\xf0\x9f\x98\x80");
        require(utf8==U"AÉ😀","strict UTF-8 scalar decoding mismatch");
        for(const auto& bytes:{std::string("\0",1),std::string("\xc0\xaf",2),std::string("\xed\xa0\x80",3),
                              std::string("\xf4\x90\x80\x80",4),std::string("\xe2\x82",2)})
            rejects([&]{g1::bitmap_font_utf8(bytes);});
        rejects([&]{fonts.front().measure(std::u32string(1,0xd800));});
        bitmap_contract();
        if(argc>1)source_assets(argv[1]);
        std::cout<<"bitmap font contract: "<<checks<<" checks, "<<g1_font_fixture::cases.size()
                 <<" original-function cases, "<<original_draws<<" original draws passed\n";
        return EXIT_SUCCESS;
    } catch(const std::exception& error) {
        std::cerr<<"bitmap font failure: "<<error.what()<<'\n';return EXIT_FAILURE;
    }
}
