#include "g1/M3GAnimation.hpp"

#include <algorithm>
#include <cmath>
#include <cstring>
#include <functional>
#include <limits>
#include <set>

namespace g1 {
namespace {
void require(bool value,const char* message) { if(!value)throw FormatError(message); }
std::int32_t signed32(std::uint32_t value) noexcept {
    std::int32_t result;std::memcpy(&result,&value,4);return result;
}
std::int32_t wrap(std::int64_t value) noexcept { return signed32(static_cast<std::uint32_t>(value)); }
std::int32_t shift_right(std::int32_t value,unsigned bits) noexcept {
    if(bits>=32)return value<0 ? -1 : 0;
    if(value>=0)return static_cast<std::int32_t>(std::uint32_t(value)>>bits);
    return signed32(~(std::uint32_t(~value)>>bits));
}
// Each original arithmetic helper returns binary32 before the next operation.
// Explicit round points also prevent a native FMA from changing that order.
float add(float a,float b) noexcept { volatile float v=a+b;return v; }
float sub(float a,float b) noexcept { volatile float v=a-b;return v; }
float mul(float a,float b) noexcept { volatile float v=a*b;return v; }
float div(float a,float b) noexcept { volatile float v=a/b;return v; }
std::int32_t integer(float value) {
    require(std::isfinite(value) && double(value)>=-2147483648.0 && double(value)<2147483648.0,
            "M3G animation arithmetic exceeds original signed integer domain");
    return static_cast<std::int32_t>(value);
}
float scalar_bits(std::uint32_t bits) noexcept { float result;std::memcpy(&result,&bits,4);return result; }

float inverse_sqrt(float value) {
    std::uint32_t bits;std::memcpy(&bits,&value,4);
    float estimate=scalar_bits(0x5f374800U-(bits>>1U));
    for(int i=0;i<2;++i)estimate=mul(mul(estimate,0.5F),sub(3.0F,mul(estimate,mul(value,estimate))));
    return estimate;
}
float norm_squared(const std::vector<float>& q) {
    require(q.size()==4,"M3G orientation requires four components");
    float n=mul(q[3],q[3]);for(std::size_t i=0;i<3;++i)n=add(n,mul(q[i],q[i]));return n;
}
void normalize(std::vector<float>& q) {
    const float n=norm_squared(q);
    require(std::isfinite(n),"M3G quaternion norm is nonfinite");
    if(n!=1 && n>std::numeric_limits<float>::min()) {
        const float scale=inverse_sqrt(n);for(auto& value:q)value=mul(scale,value);
    }
}

// Original fixed-point atan support (0x8da18,0x8ddf8,0x8dfcc). The reciprocal
// table at 0xcfbd4 is exactly floor(2^30/i), i1..512; generate its values.
std::int32_t reciprocal(std::int32_t input) {
    const bool negative=input<0;
    std::int32_t value=negative ? wrap(-std::int64_t(input)) : input;
    require(value>=0,"M3G reciprocal input is outside the recovered domain");
    int precision=32;
    const auto table=[](std::int32_t i) { return i==0 ? std::numeric_limits<std::int32_t>::max() : (1<<30)/i; };
    std::int32_t result;
    if(value<=512)result=table(value);
    else {
        auto fraction=signed32(std::uint32_t(value)<<8U);
        while(value>4095) { precision-=4;value=shift_right(value,4);fraction=shift_right(fraction,4); }
        while(value>511) { --precision;value=shift_right(value,1);fraction=shift_right(fraction,1); }
        const auto low=table(value);
        result=wrap(std::int64_t(low)+shift_right(wrap(std::int64_t(table(value+1)-low)*(std::uint32_t(fraction)&255U)),8));
    }
    const int shift=precision-30;
    result=shift<0 ? shift_right(result,static_cast<unsigned>(-shift)) :
        signed32(std::uint32_t(result)<<static_cast<unsigned>(shift));
    return negative ? wrap(-std::int64_t(result)) : result;
}
std::int32_t fixed_mul(std::int32_t a,std::int32_t b) noexcept {
    const std::int64_t value=std::int64_t(a)*b;
    // Arithmetic shift by16, expressed without implementation-defined shift.
    const auto scaled=value>=0 ? value/65536 : -((-value+65535)/65536);
    return wrap(scaled);
}
std::int32_t fixed_div(std::int32_t a,std::int32_t b) noexcept {
    return b==0 ? -1 : wrap(std::int64_t(a)*65536/b);
}
std::int32_t fixed_atan(std::int32_t input) {
    const bool negative=input<0;auto x=negative ? wrap(-std::int64_t(input)) : input;
    require(x>=0,"M3G atan input is outside the recovered domain");
    unsigned region=0;
    if(x>65536) { x=reciprocal(x);region=2; }
    if(x>17560) {
        ++region;
        x=fixed_div(wrap(std::int64_t(x)+fixed_mul(x,47976)-65536),wrap(std::int64_t(x)+113512));
    }
    const auto z=fixed_mul(x,x);
    auto numerator=wrap(std::int64_t(fixed_mul(z,-54891))-556703);
    numerator=wrap(std::int64_t(fixed_mul(z,numerator))-1343871);
    numerator=wrap(std::int64_t(fixed_mul(z,numerator))-897106);
    numerator=fixed_mul(x,fixed_mul(z,numerator));
    auto denominator=wrap(std::int64_t(z)+984613);
    denominator=wrap(std::int64_t(fixed_mul(z,denominator))+3904533);
    denominator=wrap(std::int64_t(fixed_mul(z,denominator))+5646408);
    denominator=wrap(std::int64_t(fixed_mul(z,denominator))+2691322);
    auto result=wrap(std::int64_t(x)+fixed_div(numerator,denominator));
    if(region>1)result=wrap(-std::int64_t(result));
    constexpr std::array<std::int32_t,4> offsets{{0,34315,102944,68629}};
    result=wrap(std::int64_t(result)+offsets.at(region));
    return negative ? wrap(-std::int64_t(result)) : result;
}
std::int32_t fixed_atan2(std::int32_t y,std::int32_t x) {
    if(x==0 && y==0)return 0;
    const auto ax=std::abs(std::int64_t(x)),ay=std::abs(std::int64_t(y));
    if(ay>ax*32768)return y<0 ? -102943 : 102943;
    auto result=ax>ay*32768 ? 0 : fixed_atan(fixed_div(y,x));
    if(x<=0)result=wrap(std::int64_t(result)+(y<0 ? -205887 : 205887));
    return result;
}

std::vector<float> slerp(const std::vector<float>& a,const std::vector<float>& b,float fraction) {
    if(fraction==0)return a;
    if(fraction==1)return b;
    float dot=mul(a[3],b[3]);for(std::size_t i=0;i<3;++i)dot=add(dot,mul(a[i],b[i]));
    float left=sub(1,fraction),right=fraction;
    if(dot<scalar_bits(0x3f7ff972U)) {
        const float sine=std::sqrt(sub(1,mul(dot,dot)));
        if(std::abs(sine)>scalar_bits(0x37fba882U)) {
            const auto y=integer(add(mul(sine,65536),0.5F));
            const auto x=integer(add(mul(dot,65536),0.5F));
            const float angle=mul(static_cast<float>(fixed_atan2(y,x)),1.0F/65536.0F);
            left=div(std::sin(mul(angle,left)),sine);
            right=div(std::sin(mul(fraction,angle)),sine);
        }
    }
    std::vector<float> result(4);
    for(std::size_t i=0;i<4;++i)result[i]=add(mul(left,a[i]),mul(right,b[i]));
    const float correction=mul(mul(sub(3,norm_squared(result)),536870912.0F),scalar_bits(0x30800000U));
    for(auto& value:result)value=mul(value,correction);
    return result;
}

float sign_flip(float value) noexcept {
    std::uint32_t bits;std::memcpy(&bits,&value,4);return scalar_bits(bits^0x80000000U);
}
std::vector<float> quaternion_product(const std::vector<float>& a,const std::vector<float>& b) {
    require(a.size()==4 && b.size()==4,"M3G quaternion product needs four components");
    return {
        add(add(add(mul(a[3],b[0]),mul(a[0],b[3])),mul(a[1],b[2])),mul(sign_flip(a[2]),b[1])),
        add(add(add(mul(a[3],b[1]),mul(a[1],b[3])),mul(a[2],b[0])),mul(sign_flip(a[0]),b[2])),
        add(add(add(mul(a[3],b[2]),mul(a[2],b[3])),mul(a[0],b[1])),mul(sign_flip(a[1]),b[0])),
        add(add(add(mul(a[3],b[3]),mul(sign_flip(a[0]),b[0])),mul(sign_flip(a[1]),b[1])),mul(sign_flip(a[2]),b[2]))
    };
}
float quaternion_vector_length(const std::vector<float>& q) {
    return std::sqrt(add(add(mul(q[0],q[0]),mul(q[1],q[1])),mul(q[2],q[2])));
}
std::vector<float> relative_quaternion_log(const std::vector<float>& a,const std::vector<float>& b) {
    auto conjugate=a;for(std::size_t i=0;i<3;++i)conjugate[i]=sign_flip(conjugate[i]);
    auto result=quaternion_product(conjugate,b);normalize(result);
    const auto length=quaternion_vector_length(result);
    if(length>scalar_bits(0x377ba882U)) {
        const auto y=integer(add(mul(length,65536.0F),0.5F));
        const auto x=integer(add(mul(result[3],65536.0F),0.5F));
        const auto angle=mul(static_cast<float>(fixed_atan2(y,x)),1.0F/65536.0F);
        const auto factor=div(angle,length);
        for(std::size_t i=0;i<3;++i)result[i]=mul(factor,result[i]);
    }
    result[3]=0.0F;return result;
}
std::vector<float> quaternion_exp(std::vector<float> value) {
    const auto length=quaternion_vector_length(value);
    if(length>scalar_bits(0x377ba882U)) {
        const auto factor=div(std::sin(length),length);
        value[0]=mul(value[0],factor);value[1]=mul(factor,value[1]);value[2]=mul(factor,value[2]);
    }
    value[3]=std::cos(length);return value;
}

class Reader {
public:
    Reader(const Bytes& bytes,std::size_t begin,std::size_t size):bytes_(bytes),cursor_(begin),end_(begin+size) {
        require(begin<=bytes.size() && size<=bytes.size()-begin,"M3G animation body bounds");
    }
    std::size_t remaining() const noexcept { return end_-cursor_; }
    std::uint8_t byte() { require(remaining()!=0,"truncated M3G animation body");return bytes_[cursor_++]; }
    std::uint16_t u16() { const auto a=byte(),b=byte();return std::uint16_t(std::uint16_t(a)|(std::uint16_t(b)<<8U)); }
    std::uint32_t u32() { const auto a=u16(),b=u16();return std::uint32_t(a)|(std::uint32_t(b)<<16U); }
    std::int32_t i32() { return signed32(u32()); }
    float scalar() {
        const auto bits=u32(),exponent=bits&0x7f800000U;
        require(exponent!=0x7f800000U && bits!=0x80000000U && (exponent || !(bits&0x007fffffU)),
                "invalid serialized M3G animation Float32");return scalar_bits(bits);
    }
    void skip(std::size_t n) { require(n<=remaining(),"truncated M3G user parameters");cursor_+=n; }
    void finish() const { require(remaining()==0,"trailing M3G animation data"); }
private:
    const Bytes& bytes_;std::size_t cursor_,end_;
};

std::vector<std::size_t> sequence_order(const M3GSequence& sequence) {
    const auto count=sequence.keyframes.size();
    require(count && sequence.first<count && sequence.last<count && sequence.components && sequence.components<=256,
            "invalid M3G animation range or component count");
    require(sequence.duration>0,"M3G animation duration is unset");
    require(sequence.repeat_mode==192 || sequence.repeat_mode==193,"invalid M3G animation repeat mode");
    std::vector<std::size_t> order;
    std::int32_t prior=-1;
    for(auto index=sequence.first;;index=(index+1)%count) {
        const auto& key=sequence.keyframes[index];
        require(key.time>=0 && key.time>=prior && key.time<=sequence.duration && key.value.size()==sequence.components,
                "invalid M3G valid keyframe timing or values");
        for(const auto value:key.value)require(std::isfinite(value),"nonfinite M3G keyframe");
        prior=key.time;order.push_back(index);if(index==sequence.last)break;
    }
    return order;
}
void controller_valid(const M3GController& c) {
    require(std::isfinite(c.speed) && std::isfinite(c.weight) && c.weight>=0 &&
            std::isfinite(c.reference_sequence_time) && c.active_end>=c.active_start,
            "invalid M3G animation controller");
}
constexpr M3GMatrix identity{{1,0,0,0,0,1,0,0,0,0,1,0,0,0,0,1}};
M3GMatrix quaternion_matrix(const std::array<float,4>& q) {
    const auto x=q[0],y=q[1],z=q[2],w=q[3];
    return {{1-2*(y*y+z*z),2*(x*y+z*w),2*(x*z-y*w),0,
             2*(x*y-z*w),1-2*(x*x+z*z),2*(y*z+x*w),0,
             2*(x*z+y*w),2*(y*z-x*w),1-2*(x*x+y*y),0,0,0,0,1}};
}
} // namespace

bool m3g_controller_active(const M3GController& c,std::int32_t time) noexcept {
    return c.active_start==c.active_end || (time>=c.active_start && time<c.active_end);
}

float m3g_controller_position(const M3GController& c,std::int32_t time,std::int32_t duration) {
    controller_valid(c);require(duration>=0,"negative M3G loop duration");
    auto elapsed=wrap(std::int64_t(time)-c.reference_world_time);
    float position;
    if(c.speed==1) {
        if(duration && (elapsed>10000000 || elapsed< -10000000)) {
            const auto multiple=wrap(std::int64_t(duration)*(elapsed/duration));
            elapsed=wrap(std::int64_t(elapsed)-integer(static_cast<float>(multiple)));
        }
        position=static_cast<float>(elapsed);
    } else {
        position=mul(static_cast<float>(elapsed),c.speed);
        if(duration && c.speed!=0 && (position>10000000 || position< -10000000)) {
            const auto cycles=integer(div(position,static_cast<float>(duration)));
            const auto correction=c.speed>1 ? wrap(std::int64_t(duration)*integer(div(static_cast<float>(cycles),c.speed))) :
                integer(div(static_cast<float>(wrap(std::int64_t(duration)*cycles)),c.speed));
            position=mul(static_cast<float>(wrap(std::int64_t(elapsed)-correction)),c.speed);
        }
    }
    position=add(position,c.reference_sequence_time);
    require(std::isfinite(position),"M3G controller position is nonfinite");return position;
}

M3GSequenceSampler::M3GSequenceSampler(const M3GSequence& sequence)
    :interpolation_(sequence.interpolation),repeat_mode_(sequence.repeat_mode),
     duration_(sequence.duration),components_(sequence.components),order_(sequence_order(sequence)) {
    require(interpolation_>=176 && interpolation_<=180,"invalid M3G interpolation mode");
    require((interpolation_!=177 && interpolation_!=179) || components_==4,
            "M3G SLERP/SQUAD needs four components");
    keyframes_.reserve(sequence.keyframes.size());
    for(const auto& key:sequence.keyframes)keyframes_.push_back({key.time,key.value,{},{}});
    if(interpolation_==177 || interpolation_==179)
        for(const auto index:order_)normalize(keyframes_[index].value);
    if(interpolation_!=178 && interpolation_!=179)return;
    const auto duration=integer(add(0.5F,static_cast<float>(duration_)));
    for(std::size_t position=0;position<order_.size();++position) {
        const auto index=order_[position];auto& key=keyframes_[index];
        const auto previous=order_[(position+order_.size()-1)%order_.size()];
        const auto next=order_[(position+1)%order_.size()];
        key.outgoing.resize(components_);key.incoming.resize(components_);
        const bool endpoint=position==0 || position+1==order_.size();
        if(repeat_mode_==192 && endpoint && interpolation_==178)continue;
        float before=0.0F,after=0.0F;
        if(repeat_mode_!=192 || !endpoint) {
            auto prior_time=keyframes_[previous].time,next_time=keyframes_[next].time;
            if(position==0)prior_time=wrap(std::int64_t(prior_time)-duration);
            else if(position+1==order_.size())next_time=wrap(std::int64_t(next_time)+duration);
            const auto denominator=static_cast<float>(wrap(std::int64_t(next_time)-prior_time));
            require(denominator!=0.0F,"degenerate M3G cubic auxiliary interval");
            before=div(static_cast<float>(wrap(std::int64_t(key.time)-prior_time)),denominator);
            after=div(static_cast<float>(wrap(std::int64_t(next_time)-key.time)),denominator);
        }
        if(interpolation_==178) {
            for(std::size_t i=0;i<components_;++i) {
                const auto difference=sub(keyframes_[next].value[i],keyframes_[previous].value[i]);
                key.outgoing[i]=mul(after,difference);key.incoming[i]=mul(before,difference);
            }
        } else {
            const auto left=relative_quaternion_log(keyframes_[previous].value,key.value);
            const auto right=relative_quaternion_log(key.value,keyframes_[next].value);
            std::vector<float> outgoing(4),incoming(4);
            for(std::size_t i=0;i<4;++i) {
                const auto sum=add(left[i],right[i]);
                outgoing[i]=mul(mul(sub(mul(after,sum),right[i]),536870912.0F),scalar_bits(0x30800000U));
                incoming[i]=mul(mul(sub(left[i],mul(before,sum)),536870912.0F),scalar_bits(0x30800000U));
            }
            key.outgoing=quaternion_product(key.value,quaternion_exp(std::move(outgoing)));
            key.incoming=quaternion_product(key.value,quaternion_exp(std::move(incoming)));
        }
        for(const auto value:key.outgoing)require(std::isfinite(value),"nonfinite M3G cubic outgoing auxiliary");
        for(const auto value:key.incoming)require(std::isfinite(value),"nonfinite M3G cubic incoming auxiliary");
    }
}

std::vector<float> M3GSequenceSampler::sample(float time) const {
    require(std::isfinite(time),"nonfinite M3G sequence time");
    const auto value=[&](std::size_t index) -> const std::vector<float>& { return keyframes_[index].value; };
    const float duration=static_cast<float>(duration_);
    const auto first=order_.front(),last=order_.back();
    const float first_time=static_cast<float>(keyframes_[first].time),last_time=static_cast<float>(keyframes_[last].time);
    if(repeat_mode_==193) {
        const auto cycles=integer(div(time,duration));time=sub(time,mul(duration,static_cast<float>(cycles)));
        if(time<0)time=add(time,duration);
    } else {
        if(time<first_time)return value(first);
        // The original 0x9346c clamps a CONSTANT sequence at duration-1,
        // before checking its final keyframe; retain that observed policy.
        if(time>=duration)time=static_cast<float>(duration_-1);
        if(time>last_time)return value(last);
    }
    if(order_.size()==1)return value(first);
    std::size_t before=last,after=first;
    float factor=0;
    if(time>=first_time && time<last_time) {
        auto position=std::upper_bound(order_.begin(),order_.end(),time,[&](float t,std::size_t key) {
            return t<static_cast<float>(keyframes_[key].time);
        });
        require(position!=order_.begin() && position!=order_.end(),"M3G keyframe segment selection");
        after=*position;before=*(position-1);
        const float begin=static_cast<float>(keyframes_[before].time),end=static_cast<float>(keyframes_[after].time);
        if(time==begin)return value(before);
        factor=div(sub(time,begin),sub(end,begin));
    } else {
        if(repeat_mode_!=193 || interpolation_==180 || time==last_time)return value(last);
        const float gap=sub(duration,last_time),length=add(first_time,gap);
        require(length>0,"invalid M3G wrapped interpolation interval");
        factor=time<first_time ? div(add(time,gap),length) : div(sub(time,last_time),length);
    }
    const auto& a=keyframes_[before].value;const auto& b=keyframes_[after].value;
    if(interpolation_==180)return a;
    if(interpolation_==177)return slerp(a,b,factor);
    if(interpolation_==179) {
        const auto keys=slerp(a,b,factor);
        const auto controls=slerp(keyframes_[before].outgoing,keyframes_[after].incoming,factor);
        auto blend=mul(factor,sub(1.0F,factor));blend=add(blend,blend);
        return slerp(keys,controls,blend);
    }
    if(interpolation_==178) {
        const auto u2=mul(factor,factor),u3=mul(factor,u2);
        const auto twice=add(u3,u3),three=mul(u2,3.0F);
        const auto h00=add(twice,sub(1.0F,three)),h01=add(sign_flip(twice),three);
        const auto h10=add(u3,add(factor,mul(u2,-2.0F))),h11=sub(u3,u2);
        std::vector<float> result(components_);
        for(std::size_t i=0;i<result.size();++i)
            result[i]=add(add(add(mul(h00,a[i]),mul(h01,b[i])),
                              mul(h10,keyframes_[before].outgoing[i])),
                              mul(h11,keyframes_[after].incoming[i]));
        return result;
    }
    std::vector<float> result(components_);
    for(std::size_t i=0;i<result.size();++i)result[i]=add(a[i],mul(factor,sub(b[i],a[i])));
    return result;
}

std::vector<float> m3g_sample_sequence(const M3GSequence& sequence,float time) {
    return M3GSequenceSampler(sequence).sample(time);
}

M3GAnimations::M3GAnimations(const Bytes& bytes) {
    const M3GFile file(bytes);
    require(!file.has_external_references(),"external M3G animation objects need a resolver");
    for(const auto& object:file.objects()) {
        if(object.type==0)continue;
        require(object.type!=255,"external M3G animation reference");
        Reader r(file.sections().at(object.section),object.payload_offset,object.payload_size);
        (void)r.u32();const auto tracks=r.u32();require(tracks<=r.remaining()/4,"unbounded M3G animation bindings");
        auto& bound=bindings_[object.index];for(std::uint32_t i=0;i<tracks;++i)bound.push_back(r.u32());
        const auto parameters=r.u32();require(parameters<=r.remaining()/8,"unbounded M3G animation parameters");
        std::set<std::uint32_t> ids;
        for(std::uint32_t i=0;i<parameters;++i) { require(ids.insert(r.u32()).second,"duplicate M3G user parameter");const auto size=r.u32();r.skip(size); }
        if(object.type==1) {
            M3GController c;c.object=object.index;c.speed=r.scalar();c.weight=r.scalar();
            c.active_start=r.i32();c.active_end=r.i32();c.reference_sequence_time=r.scalar();c.reference_world_time=r.i32();
            controller_valid(c);controllers_.emplace(object.index,c);r.finish();
        } else if(object.type==2) {
            M3GTrack t;t.object=object.index;t.sequence=r.u32();t.controller=r.u32();t.property=r.u32();
            require(t.property>=256 && t.property<=276,"invalid M3G animation target property");
            tracks_.emplace(object.index,t);r.finish();
        } else if(object.type==19) {
            M3GSequence s;s.object=object.index;s.interpolation=r.byte();s.repeat_mode=r.byte();const auto encoding=r.byte();
            require(s.interpolation>=176 && s.interpolation<=180 && encoding<=2,"invalid M3G sequence encoding");
            s.duration=r.i32();s.first=r.u32();s.last=r.u32();s.components=r.u32();const auto count=r.u32();
            require(s.components && s.components<=256 && count && count<=1000000,"invalid M3G keyframe dimensions");
            std::vector<float> bias(s.components),scale(s.components);
            if(encoding) { for(auto& x:bias)x=r.scalar();for(auto& x:scale)x=r.scalar(); }
            const std::size_t width=encoding==0 ? 4 : encoding==1 ? 1 : 2;
            require(count<=r.remaining()/(4+s.components*width),"truncated M3G keyframes");
            s.keyframes.reserve(count);
            for(std::uint32_t i=0;i<count;++i) {
                M3GKeyframe k;k.time=r.i32();require(k.time>=0,"negative serialized M3G key time");k.value.resize(s.components);
                for(std::size_t c=0;c<s.components;++c) {
                    if(!encoding)k.value[c]=r.scalar();
                    else { const float raw=encoding==1 ? r.byte() : r.u16();
                        k.value[c]=add(bias[c],mul(scale[c],div(raw,encoding==1 ? 255.0F : 65535.0F))); }
                }
                s.keyframes.push_back(std::move(k));
            }
            r.finish();samplers_.emplace(object.index,M3GSequenceSampler(s));sequences_.emplace(object.index,std::move(s));
        }
    }
    for(const auto& entry:bindings_)for(const auto track:entry.second)
        require(track<entry.first && tracks_.count(track),"invalid M3G animation track binding");
    for(const auto& entry:tracks_) {
        const auto& t=entry.second;
        require(t.sequence<t.object && sequences_.count(t.sequence),"invalid M3G sequence reference");
        require(!t.controller || (t.controller<t.object && controllers_.count(t.controller)),"invalid M3G controller reference");
        const auto count=sequences_.at(t.sequence).components;
        require((t.property!=268 || count==4) && (t.property!=275 || count==3) &&
                (t.property!=270 || count==1 || count==3) &&
                ((t.property!=256 && t.property!=264 && t.property!=276) || count==1),
                "incompatible M3G animation track components");
    }
}

std::vector<M3GPropertySample> M3GAnimations::sample(std::int32_t time,const std::map<std::size_t,M3GController>& overrides) const {
    return sample_selected(time,nullptr,overrides);
}
std::vector<M3GPropertySample> M3GAnimations::sample_targets(std::int32_t time,
    const std::set<std::size_t>& targets,const std::map<std::size_t,M3GController>& overrides) const {
    for(const auto target:targets)require(bindings_.count(target),"unknown M3G animation target selection");
    return sample_selected(time,&targets,overrides);
}
std::vector<M3GPropertySample> M3GAnimations::sample_selected(std::int32_t time,
    const std::set<std::size_t>* selected,const std::map<std::size_t,M3GController>& overrides) const {
    for(const auto& entry:overrides) { require(controllers_.count(entry.first),"override references an unknown M3G controller");controller_valid(entry.second); }
    std::map<std::pair<std::size_t,std::uint32_t>,std::vector<float>> accumulated;
    for(const auto& target:bindings_) {
      if(selected && !selected->count(target.first))continue;
      for(const auto track:target.second) {
        const auto& t=tracks_.at(track);if(!t.controller)continue;
        const auto found=overrides.find(t.controller);
        const auto& c=found==overrides.end() ? controllers_.at(t.controller) : found->second;
        if(c.weight==0 || !m3g_controller_active(c,time))continue;
        const auto& sequence=sequences_.at(t.sequence);
        auto value=samplers_.at(t.sequence).sample(m3g_controller_position(c,time,sequence.repeat_mode==193 ? sequence.duration : 0));
        if(t.property==270 && value.size()==1)value.resize(3,value[0]);
        require((t.property==268 && value.size()==4) || ((t.property==270 || t.property==275) && value.size()==3) ||
                ((t.property==256 || t.property==264 || t.property==276) && value.size()==1),
                "active M3G property is outside supported node/camera animation");
        auto& sum=accumulated[{target.first,t.property}];if(sum.empty())sum.resize(value.size());
        require(sum.size()==value.size(),"incompatible M3G track component counts");
        for(std::size_t i=0;i<sum.size();++i)sum[i]=add(sum[i],mul(c.weight,value[i]));
      }
    }
    std::vector<M3GPropertySample> result;
    for(auto& entry:accumulated) {
        if(entry.first.second==268)normalize(entry.second);
        result.push_back({entry.first.first,entry.first.second,std::move(entry.second)});
    }
    return result;
}

M3GRigidPose::M3GRigidPose(const M3GScene& scene) {
    for(const auto& info:scene.objects())if(info.is_node)states_.emplace(info.index,State{info,std::nullopt});
}
void M3GRigidPose::apply(const std::vector<M3GPropertySample>& samples) {
    // Validate the entire update before changing any target.
    for(const auto& s:samples) {
        require(states_.count(s.target),"M3G rigid pose cannot animate a non-node target");
        require((s.property==268 && s.value.size()==4) || ((s.property==270 || s.property==275) && s.value.size()==3),
                "unsupported rigid M3G pose property");
        for(const auto x:s.value)require(std::isfinite(x),"nonfinite M3G pose assignment");
    }
    for(const auto& s:samples) {
        auto& state=states_.at(s.target);
        if(s.property==268)state.orientation=std::array<float,4>{{s.value[0],s.value[1],s.value[2],s.value[3]}};
        else {
            auto& field=s.property==275 ? state.info.transform.translation : state.info.transform.scale;
            std::copy(s.value.begin(),s.value.end(),field.begin());
        }
    }
}
void M3GRigidPose::set_general_matrix(std::size_t target,const M3GMatrix& matrix) {
    const auto found=states_.find(target);require(found!=states_.end(),"unknown M3G general-transform target");
    for(const auto value:matrix)require(std::isfinite(value),"nonfinite M3G general transform");
    found->second.info.transform.general=matrix;
}
namespace {
std::array<float,3> public_vector(std::array<float,3> values) {
    for(auto& value:values) {
        require(std::isfinite(value),"nonfinite M3G public vector");
        // Public conversion0x8dc88 flushes subnormal inputs to positive zero.
        if(std::fpclassify(value)==FP_SUBNORMAL)value=0.0F;
    }
    return values;
}
}
void M3GRigidPose::set_translation(std::size_t target,const std::array<float,3>& value) {
    const auto found=states_.find(target);require(found!=states_.end(),"unknown M3G translation target");
    const auto converted=public_vector(value);
    found->second.info.transform.translation=converted;
}
void M3GRigidPose::translate(std::size_t target,const std::array<float,3>& value) {
    const auto found=states_.find(target);require(found!=states_.end(),"unknown M3G Translate target");
    const auto converted=public_vector(value);
    const auto epsilon=scalar_bits(0x377ba882U);
    if(std::abs(converted[0])<=epsilon && std::abs(converted[1])<=epsilon &&
       std::abs(converted[2])<=epsilon)return;
    auto next=found->second.info.transform.translation;
    for(std::size_t i=0;i<3;++i) {
        next[i]=add(next[i],converted[i]);
        require(std::isfinite(next[i]),"overflow in M3G local Translate");
    }
    found->second.info.transform.translation=next;
}
std::array<float,3> M3GRigidPose::translation(std::size_t target) const {
    const auto found=states_.find(target);require(found!=states_.end(),"unknown M3G translation target");
    return found->second.info.transform.translation;
}
void M3GRigidPose::detach(std::size_t target) {
    const auto found=states_.find(target);require(found!=states_.end(),"unknown M3G detach target");
    found->second.info.parent.reset();
}
std::map<std::size_t,M3GMatrix> M3GRigidPose::world_matrices() const {
    std::map<std::size_t,M3GMatrix> result;
    const auto local=[](const State& value) {
        const auto& t=value.info.transform;
        auto matrix=m3g_transform_matrix(t);
        if(value.orientation) {
            auto translation=identity,scale=identity;
            for(std::size_t i=0;i<3;++i) { translation[12+i]=t.translation[i];scale[i*5]=t.scale[i]; }
            matrix=m3g_multiply(m3g_multiply(m3g_multiply(translation,quaternion_matrix(*value.orientation)),scale),t.general);
        }
        return matrix;
    };
    // Input byte bounds do not imply a safe C++ recursion depth. Walk each
    // uncached ancestor chain iteratively, then evaluate from its root down.
    for(const auto& entry:states_) {
        if(result.count(entry.first))continue;
        std::vector<std::size_t> chain;std::set<std::size_t> visiting;
        auto index=entry.first;
        while(!result.count(index)) {
            require(visiting.insert(index).second,"cyclic M3G pose hierarchy");
            const auto state=states_.find(index);require(state!=states_.end(),"missing M3G pose ancestor");
            chain.push_back(index);
            if(!state->second.info.parent)break;
            index=*state->second.info.parent;
        }
        for(auto i=chain.rbegin();i!=chain.rend();++i) {
            const auto& state=states_.at(*i);auto matrix=local(state);
            if(state.info.parent)matrix=m3g_multiply(result.at(*state.info.parent),matrix);
            result.emplace(*i,matrix);
        }
    }
    return result;
}
void M3GRigidPose::update_model(M3GStaticModel& model) const {
    const auto matrices=world_matrices();model.has_geometry=false;
    for(auto& surface:model.surfaces) {
        const auto found=matrices.find(surface.mesh_object);require(found!=matrices.end(),"M3G surface has no pose");surface.model=found->second;
        for(const auto& vertex:surface.vertices) {
            std::array<float,4> p{};
            for(std::size_t r=0;r<4;++r)for(std::size_t c=0;c<4;++c)p[r]+=surface.model[c*4+r]*vertex.position[c];
            require(std::isfinite(p[3]) && p[3]!=0,"invalid M3G posed homogeneous position");
            for(std::size_t i=0;i<3;++i) {
                const float v=p[i]/p[3];require(std::isfinite(v),"nonfinite M3G posed vertex");
                if(!model.has_geometry)model.minimum[i]=model.maximum[i]=v;
                else { model.minimum[i]=std::min(model.minimum[i],v);model.maximum[i]=std::max(model.maximum[i],v); }
            }
            model.has_geometry=true;
        }
    }
}

} // namespace g1
