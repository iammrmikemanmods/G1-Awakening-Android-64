#pragma once

#include "g1/M3GSprite3D.hpp"

#include <cstring>
#include <istream>
#include <stdexcept>

namespace sprite3d_fixture {
inline float scalar(std::uint32_t bits) { float value;std::memcpy(&value,&bits,4);return value; }
inline std::uint32_t bits(float value) { std::uint32_t result;std::memcpy(&result,&value,4);return result; }
inline void tag(std::istream& in,const char* expected) {
    std::string actual;if(!(in>>actual) || actual!=expected)throw std::runtime_error(std::string("Expected Sprite3D fixture tag ")+expected);
}
template<class T> inline T value(std::istream& in) {
    T result{};if(!(in>>result))throw std::runtime_error("Truncated Sprite3D fixture value");return result;
}
template<class T,std::size_t N> inline void values(std::istream& in,std::array<T,N>& target) {
    for(auto& item:target)item=value<T>(in);
}
template<std::size_t N> inline void scalars(std::istream& in,std::array<float,N>& target) {
    for(auto& item:target)item=scalar(value<std::uint32_t>(in));
}
struct SpriteCase {
    std::string name;g1::M3GSprite3DInput input;bool draws{};g1::M3GSprite3DProjection expected;
};
struct QueueCase {
    std::string name;std::array<float,3> camera{};std::vector<g1::M3GDrawQueueEntry> entries;
    std::vector<std::int32_t> keys;std::vector<std::size_t> order;
};
struct Fixture { std::vector<SpriteCase> sprites;std::vector<QueueCase> queues; };
inline Fixture read(std::istream& in) {
    Fixture fixture;tag(in,"G1SPRITE3D1");
    const auto sprite_count=value<std::size_t>(in),queue_count=value<std::size_t>(in);
    if(sprite_count>10000 || queue_count>10000)throw std::runtime_error("Sprite3D fixture count exceeds bound");
    for(std::size_t i=0;i<sprite_count;++i) {
        SpriteCase entry;tag(in,"CASE");entry.name=value<std::string>(in);tag(in,"I");
        const auto type=value<unsigned>(in),scaled=value<unsigned>(in),appearance=value<unsigned>(in);
        if(type<48 || type>50 || scaled>1 || appearance>1)throw std::runtime_error("Invalid Sprite3D fixture enum");
        entry.input.projection_type=static_cast<std::uint8_t>(type);entry.input.scaled=scaled!=0;entry.input.appearance_present=appearance!=0;
        values(in,entry.input.viewport_pixels);values(in,entry.input.image_pixels);
        tag(in,"C");scalars(in,entry.input.crop_pixels);
        tag(in,"M");scalars(in,entry.input.model_to_world);
        tag(in,"V");scalars(in,entry.input.camera_view);
        tag(in,"P");scalars(in,entry.input.projection_gl);
        tag(in,"E");const auto drawn=value<unsigned>(in);
        if(drawn>1)throw std::runtime_error("Invalid Sprite3D fixture draw flag");
        entry.draws=drawn!=0;
        if(entry.draws) {
            tag(in,"O");scalars(in,entry.expected.center_ndc_gl);
            tag(in,"R");values(in,entry.expected.rectangle_fixed16);
            tag(in,"B");values(in,entry.expected.crop_bounds_fixed16);
            tag(in,"F");const auto x=value<unsigned>(in),y=value<unsigned>(in);
            if(x>1 || y>1)throw std::runtime_error("Invalid Sprite3D fixture mirror flag");
            entry.expected.mirror_x=x!=0;entry.expected.mirror_y=y!=0;
            tag(in,"XY");values(in,entry.expected.offsets_fixed16);
            tag(in,"UV");values(in,entry.expected.uv_fixed16);
        }
        fixture.sprites.push_back(std::move(entry));
    }
    for(std::size_t q=0;q<queue_count;++q) {
        QueueCase entry;tag(in,"QUEUE");entry.name=value<std::string>(in);
        const auto count=value<std::size_t>(in);
        if(count>10000)throw std::runtime_error("Sprite3D queue fixture exceeds bound");
        tag(in,"CAM");scalars(in,entry.camera);
        for(std::size_t n=0;n<count;++n) {
            g1::M3GDrawQueueEntry request;tag(in,"D");
            request.node_identity=value<std::uint64_t>(in);request.layer=value<std::int32_t>(in);
            const auto transparent=value<unsigned>(in);
            if(transparent>1)throw std::runtime_error("Invalid Sprite3D fixture transparency");
            request.transparent=transparent!=0;request.grouping_key=value<std::int32_t>(in);scalars(in,request.world_origin);
            tag(in,"K");entry.keys.push_back(value<std::int32_t>(in));entry.entries.push_back(request);
        }
        tag(in,"ORDER");for(std::size_t n=0;n<count;++n)entry.order.push_back(value<std::size_t>(in));
        fixture.queues.push_back(std::move(entry));
    }
    tag(in,"END");std::string trailing;if(in>>trailing)throw std::runtime_error("Trailing Sprite3D fixture content");
    return fixture;
}
} // namespace sprite3d_fixture
