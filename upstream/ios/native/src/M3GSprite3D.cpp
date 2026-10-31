#include "g1/M3GSprite3D.hpp"

#include <cmath>
#include <cstring>
#include <limits>

namespace g1 {
namespace {
static_assert(sizeof(float)==4 && std::numeric_limits<float>::is_iec559,
              "G1 Sprite3D requires IEEE754 binary32");
void require(bool condition,const char* message) { if(!condition)throw FormatError(message); }

// The original ARMv6 helpers round after each operation. Volatile storage
// prevents a native compiler from combining a multiply and add into an FMA.
float add(float a,float b) noexcept { volatile float v=a+b;return v; }
float sub(float a,float b) noexcept { volatile float v=a-b;return v; }
float mul(float a,float b) noexcept { volatile float v=a*b;return v; }
float div(float a,float b) noexcept { volatile float v=a/b;return v; }
float from_bits(std::uint32_t v) noexcept { float f;std::memcpy(&f,&v,4);return f; }
std::uint32_t float_bits(float v) noexcept { std::uint32_t u;std::memcpy(&u,&v,4);return u; }
std::int32_t signed_bits(std::uint32_t v) noexcept { std::int32_t s;std::memcpy(&s,&v,4);return s; }

template<std::size_t N> void finite(const std::array<float,N>& values) {
    for(const auto v:values)require(std::isfinite(v),"nonfinite M3G Sprite3D input or arithmetic");
}
void affine(const M3GMatrix& m) {
    finite(m);
    require(m[3]==0 && m[7]==0 && m[11]==0 && m[15]==1,
            "M3G Sprite3D currently requires affine model and view matrices");
}
using Vec3=std::array<float,3>;

M3GMatrix multiply_affine(const M3GMatrix& a,const M3GMatrix& b) {
    M3GMatrix out{};out[15]=1;
    for(std::size_t column=0;column<4;++column) {
        for(std::size_t row=0;row<3;++row) {
            float v=add(mul(b[column*4],a[row]),mul(b[column*4+1],a[4+row]));
            v=add(v,mul(b[column*4+2],a[8+row]));
            if(column==3)v=add(v,a[12+row]);
            out[column*4+row]=v;
        }
    }
    finite(out);return out;
}

Vec3 difference(const Vec3& a,const Vec3& b) {
    return {{sub(a[0],b[0]),sub(a[1],b[1]),sub(a[2],b[2])}};
}
float magnitude_squared(const Vec3& v) {
    return add(add(mul(v[0],v[0]),mul(v[1],v[1])),mul(v[2],v[2]));
}
float magnitude(const Vec3& v) {
    const float sum=magnitude_squared(v);
    require(std::isfinite(sum),"M3G Sprite3D vector magnitude overflow");
    return std::sqrt(sum);
}
Vec3 divide_homogeneous(Vec3 v,float w) {
    require(std::isfinite(w),"M3G Sprite3D homogeneous projection overflow");
    const float epsilon=from_bits(0x377ba882U);
    // Original CssVector3D(x,y,z,w) avoids division near zero and near one.
    if(std::fabs(sub(w,1))>epsilon && std::fabs(w)>epsilon) {
        const float reciprocal=div(1,w);
        for(auto& component:v)component=mul(component,reciprocal);
    }
    finite(v);return v;
}
Vec3 project(const Vec3& point,const M3GMatrix& p,std::uint8_t type) {
    if(type!=48) {
        Vec3 value{{mul(point[0],p[0]),mul(point[1],p[5]),add(mul(point[2],p[10]),p[14])}};
        const float w=type==49?p[15]:mul(point[2],p[11]);
        return divide_homogeneous(value,w);
    }
    std::array<float,4> value{};
    for(std::size_t row=0;row<4;++row) {
        float v=add(mul(p[row],point[0]),mul(p[4+row],point[1]));
        v=add(v,mul(p[8+row],point[2]));
        value[row]=add(v,p[12+row]);
    }
    finite(value);
    return divide_homogeneous({{value[0],value[1],value[2]}},value[3]);
}

std::int32_t checked_i32(std::int64_t v) {
    require(v>=std::numeric_limits<std::int32_t>::min() && v<=std::numeric_limits<std::int32_t>::max(),
            "M3G Sprite3D fixed-point range exceeded");
    return static_cast<std::int32_t>(v);
}
std::int32_t truncate_i32(float v) {
    require(std::isfinite(v) && double(v)>=-2147483648.0 && double(v)<2147483648.0,
            "M3G Sprite3D float-to-integer range exceeded");
    return static_cast<std::int32_t>(v);
}
std::int32_t fixed16(float value) { return truncate_i32(add(mul(value,65536),0.5F)); }
std::int64_t ratio_fixed16(std::int64_t numerator,std::int64_t denominator) {
    require(denominator>0 && numerator>=0 && numerator<=134217728,
            "unsupported M3G Sprite3D clipping ratio");
    return numerator*65536/denominator;
}
std::int64_t multiply_fixed16(std::int64_t value,std::int64_t factor) {
    require(factor>=0,"negative M3G Sprite3D clipping factor");
    if(factor==0)return 0;
    require(value>=std::numeric_limits<std::int64_t>::min()/factor &&
            value<=std::numeric_limits<std::int64_t>::max()/factor,"M3G Sprite3D fixed-point product overflow");
    const auto product=value*factor;
    // Original 64-bit arithmetic shift rounds negative products downward.
    const auto truncated=product/65536;
    return product<0 && product%65536!=0?truncated-1:truncated;
}

void clip_one_axis(std::int32_t start,std::int32_t end,std::int32_t crop_start,
                   std::int32_t crop_end,std::uint32_t image_size,bool mirror,
                   std::int32_t& clipped_start,std::int32_t& clipped_end,
                   std::int32_t& uv_start,std::int32_t& uv_end) {
    const std::int64_t maximum=std::int64_t(image_size)*65536;
    const std::int64_t extent=std::int64_t(crop_end)-crop_start;
    require(extent>0,"M3G Sprite3D crop collapsed in fixed-point conversion");
    const std::int64_t destination=std::int64_t(end)-start;
    std::int64_t first=start,last=end,u=0,v=65536;
    if(crop_start<0) {
        first+=multiply_fixed16(destination,ratio_fixed16(-std::int64_t(crop_start),extent));
    } else {
        u=std::int64_t(crop_start)*65536/maximum;
    }
    if(crop_end>=maximum) {
        last-=multiply_fixed16(destination,ratio_fixed16(std::int64_t(crop_end)-maximum,extent));
    } else {
        v=std::int64_t(crop_end)*65536/maximum;
    }
    if(mirror) {
        const auto shift=std::int64_t(end)-last;
        first+=shift;last+=shift;
        const auto temporary=u;u=v;v=temporary;
    }
    clipped_start=checked_i32(first);clipped_end=checked_i32(last);
    uv_start=checked_i32(u);uv_end=checked_i32(v);
}

void validate(const M3GSprite3DInput& in) {
    affine(in.model_to_world);affine(in.camera_view);finite(in.projection_gl);finite(in.crop_pixels);
    require(in.projection_type>=48 && in.projection_type<=50,"unsupported M3G Sprite3D projection type");
    for(const auto size:in.viewport_pixels)
        require(size>0 && size<=16777216U,"M3G Sprite3D viewport size is invalid or inexact in binary32");
    for(const auto size:in.image_pixels)
        require(size>0 && size<=1024U && (size&(size-1U))==0,
                "M3G Sprite3D requires a power-of-two image block of 1..1024 pixels per axis");
    require(std::fabs(in.crop_pixels[2])<=1024 && std::fabs(in.crop_pixels[3])<=1024,
            "M3G Sprite3D crop exceeds original 1024-pixel dimension limit");
    if(in.projection_type!=48) {
        const auto& p=in.projection_gl;
        for(const auto index:{1U,2U,3U,4U,6U,7U,8U,9U,12U,13U})
            require(p[index]==0,"typed M3G Sprite3D projection has unsupported off-diagonal terms");
        require(in.projection_type==49?(p[11]==0):(p[15]==0),"typed M3G Sprite3D projection layout differs");
    }
}
} // namespace

M3GSprite3DProjection project_m3g_sprite3d(const M3GSprite3DInput& in) {
    validate(in);
    M3GSprite3DProjection out;
    if(!in.appearance_present) { out.result=M3GSprite3DResult::no_appearance;return out; }
    const auto& crop=in.crop_pixels;
    const float width=std::fabs(crop[2]),height=std::fabs(crop[3]);
    if(width==0 || height==0) { out.result=M3GSprite3DResult::zero_crop;return out; }
    const std::int64_t x=truncate_i32(crop[0]),y=truncate_i32(crop[1]);
    const std::int64_t integer_width=truncate_i32(width),integer_height=truncate_i32(height);
    const auto right=x+integer_width-1,bottom=y+integer_height-1;
    require(right>=std::numeric_limits<std::int32_t>::min() && right<=std::numeric_limits<std::int32_t>::max() &&
            bottom>=std::numeric_limits<std::int32_t>::min() && bottom<=std::numeric_limits<std::int32_t>::max(),
            "M3G Sprite3D integer crop intersection overflow");
    if(x>std::int64_t(in.image_pixels[0])-1 || y>std::int64_t(in.image_pixels[1])-1 || right<0 || bottom<0) {
        out.result=M3GSprite3DResult::outside_image;return out;
    }
    const auto model_view=multiply_affine(in.camera_view,in.model_to_world);
    const Vec3 origin{{model_view[12],model_view[13],model_view[14]}};
    if(in.projection_type!=48) {
        const float w=in.projection_type==49?in.projection_gl[15]:mul(origin[2],in.projection_gl[11]);
        if(std::fabs(w)<=from_bits(0x377ba882U)) { out.result=M3GSprite3DResult::projection_at_infinity;return out; }
    }
    out.center_ndc_gl=project(origin,in.projection_gl,in.projection_type);
    if(out.center_ndc_gl[2]<-1 || out.center_ndc_gl[2]>1) {
        out.result=M3GSprite3DResult::outside_depth;return out;
    }
    float half_width,half_height;
    if(in.scaled) {
        Vec3 x_end{},y_end{};
        for(std::size_t n=0;n<3;++n) {
            x_end[n]=add(model_view[n],origin[n]);
            y_end[n]=add(model_view[4+n],origin[n]);
        }
        const float x_length=magnitude(difference(x_end,origin));
        const float y_length=magnitude(difference(y_end,origin));
        x_end=origin;y_end=origin;
        x_end[0]=add(x_length,origin[0]);y_end[1]=add(y_length,origin[1]);
        const auto projected_x=project(x_end,in.projection_gl,in.projection_type);
        const auto projected_y=project(y_end,in.projection_gl,in.projection_type);
        half_width=mul(magnitude(difference(projected_x,out.center_ndc_gl)),0.5F);
        half_height=mul(magnitude(difference(projected_y,out.center_ndc_gl)),0.5F);
    } else {
        half_width=mul(width,div(1,static_cast<float>(in.viewport_pixels[0])));
        half_height=mul(height,div(1,static_cast<float>(in.viewport_pixels[1])));
    }
    if(add(half_width,out.center_ndc_gl[0])<-1 || sub(out.center_ndc_gl[0],half_width)>1 ||
       add(half_height,out.center_ndc_gl[1])<-1 || sub(out.center_ndc_gl[1],half_height)>1) {
        out.result=M3GSprite3DResult::outside_viewport;return out;
    }
    out.rectangle_fixed16={{fixed16(-half_width),fixed16(half_height),fixed16(half_width),fixed16(-half_height)}};
    out.crop_bounds_fixed16={{fixed16(crop[0]),fixed16(crop[1]),fixed16(add(width,crop[0])),fixed16(add(height,crop[1]))}};
    out.mirror_x=crop[2]<0;out.mirror_y=crop[3]<0;
    std::int32_t left,right_position,top,bottom_position,u0,u1,v0,v1;
    clip_one_axis(out.rectangle_fixed16[0],out.rectangle_fixed16[2],out.crop_bounds_fixed16[0],
                  out.crop_bounds_fixed16[2],in.image_pixels[0],out.mirror_x,left,right_position,u0,u1);
    clip_one_axis(out.rectangle_fixed16[1],out.rectangle_fixed16[3],out.crop_bounds_fixed16[1],
                  out.crop_bounds_fixed16[3],in.image_pixels[1],out.mirror_y,top,bottom_position,v0,v1);
    out.offsets_fixed16={{left,top,left,bottom_position,right_position,top,right_position,bottom_position}};
    out.uv_fixed16={{u0,v0,u0,v1,u1,v0,u1,v1}};
    out.result=M3GSprite3DResult::drawn;return out;
}

std::int32_t m3g_draw_queue_key(std::int32_t layer,bool transparent,
                               const std::array<float,3>& origin,const std::array<float,3>& camera) {
    require(layer>=-63 && layer<=63,"M3G draw layer exceeds original -63..63 range");
    finite(origin);finite(camera);
    std::uint32_t distance_key=0;
    if(transparent) {
        const float squared=magnitude_squared(difference(origin,camera));
        require(std::isfinite(squared) && squared>=0,"M3G draw distance overflow");
        const auto quantized=float_bits(squared)>>8U;
        distance_key=0x00ffffffU-(quantized>0x00ffffffU?0x00ffffffU:quantized);
    }
    const auto key=(std::uint32_t(layer)<<25U)|(std::uint32_t(transparent)<<24U)|distance_key;
    return signed_bits(key);
}

std::vector<std::size_t> m3g_draw_queue_order(const std::vector<M3GDrawQueueEntry>& input,
                                            const std::array<float,3>& camera) {
    require(input.size()<std::numeric_limits<std::size_t>::max(),"M3G draw queue size overflow");
    struct Record { std::int32_t key{};std::size_t next{}; };
    std::vector<Record> records(input.size()+1);
    std::size_t last=0,anchor=0;
    for(std::size_t i=1;i<records.size();++i) {
        const auto& entry=input[i-1];
        require(entry.node_identity!=0,"M3G draw queue node identity is zero");
        const auto key=m3g_draw_queue_key(entry.layer,entry.transparent,entry.world_origin,camera);
        records[i].key=key;
        std::size_t after=0;
        if(last && input[last-1].node_identity==entry.node_identity && records[last].key==key) {
            if(input[last-1].grouping_key==entry.grouping_key)after=last;
            else {
                after=anchor;
                for(auto n=records[after].next;n!=0;n=records[after].next) {
                    if(input[n-1].node_identity!=entry.node_identity || records[n].key!=key ||
                       entry.grouping_key<input[n-1].grouping_key)break;
                    after=n;
                }
            }
        } else {
            anchor=i;
            if(key==records[last].key)after=last;
            else {
                after=key<records[last].key?0:last;
                while(records[after].next && key>=records[records[after].next].key)after=records[after].next;
            }
        }
        records[i].next=records[after].next;records[after].next=i;last=i;
    }
    std::vector<std::size_t> order;order.reserve(input.size());
    for(auto n=records[0].next;n;n=records[n].next) {
        require(order.size()<input.size(),"M3G draw queue cycle");order.push_back(n-1);
    }
    require(order.size()==input.size(),"M3G draw queue omitted entries");
    return order;
}

} // namespace g1
