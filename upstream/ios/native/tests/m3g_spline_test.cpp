#include "g1/M3GAnimation.hpp"
#include "m3g_spline_fixture.hpp"

#include <algorithm>
#include <cmath>
#include <cstring>
#include <functional>
#include <iostream>
#include <limits>
#include <stdexcept>

namespace {
std::size_t contracts=0,prepared_scalars=0,sampled_scalars=0,prepared_exact=0,sampled_exact=0;
float maximum_math_error=0;
std::uint32_t bits(float value) { std::uint32_t result;std::memcpy(&result,&value,4);return result; }
float add(float a,float b) { volatile float value=a+b;return value; }
float mul(float a,float b) { volatile float value=a*b;return value; }
void check(bool condition,const std::string& message) { ++contracts;if(!condition)throw std::runtime_error(message); }
std::size_t compare(const std::vector<float>& actual,const std::vector<std::uint32_t>& expected,
                    const std::string& label,bool host_math_boundary=false) {
    if(actual.size()!=expected.size())throw std::runtime_error(label+": component count differs");
    std::size_t identical=0;
    for(std::size_t i=0;i<actual.size();++i) {
        if(bits(actual[i])==expected[i]) { ++identical;continue; }
        const auto error=std::abs(actual[i]-g1_spline_test::scalar(expected[i]));
        if(!host_math_boundary || !std::isfinite(error) || error>2e-6F)
            throw std::runtime_error(label+": scalar "+std::to_string(i)+" native bits "+std::to_string(bits(actual[i]))+" != original "+std::to_string(expected[i]));
        maximum_math_error=std::max(maximum_math_error,error);
    }
    return identical;
}
void rejects(const std::function<void()>& action,const char* message) {
    bool rejected=false;try { action(); }catch(const g1::FormatError&) { rejected=true; }check(rejected,message);
}
}
int main() {
    try {
        const auto corpus=g1_spline_test::cases();std::size_t samples=0;
        for(const auto& c:corpus) {
            const g1::M3GSequenceSampler prepared(c.sequence);
            for(const auto& expected:c.prepared) {
                const auto& actual=prepared.keyframes().at(expected.index);
                const auto label=c.name+" key "+std::to_string(expected.index);
                prepared_exact+=compare(actual.value,expected.value,label+" value");
                prepared_exact+=compare(actual.outgoing,expected.outgoing,label+" outgoing",c.sequence.interpolation==179);
                prepared_exact+=compare(actual.incoming,expected.incoming,label+" incoming",c.sequence.interpolation==179);
                prepared_scalars+=expected.value.size()+expected.outgoing.size()+expected.incoming.size();
            }
            for(const auto& expected:c.samples) {
                auto actual=prepared.sample(static_cast<float>(expected.time));
                const auto weight=g1_spline_test::scalar(expected.weight);
                for(std::size_t i=0;i<actual.size();++i) {
                    if(weight!=1.0F)actual[i]=mul(actual[i],weight);
                    actual[i]=add(g1_spline_test::scalar(expected.initial.at(i)),actual[i]);
                }
                sampled_exact+=compare(actual,expected.expected,c.name+" time "+std::to_string(expected.time),c.sequence.interpolation==179);
                sampled_scalars+=actual.size();++samples;
            }
        }
        auto source=corpus.front().sequence;
        const g1::M3GSequenceSampler immutable(source);
        const auto prior=immutable.sample(47);source.keyframes.front().value[0]=99;
        check(immutable.sample(47)==prior,"prepared curve changed after caller mutation");
        auto invalid=source;invalid.interpolation=181;
        rejects([&]{const g1::M3GSequenceSampler value(invalid);},"invalid cubic interpolation accepted");
        invalid=source;invalid.interpolation=179;
        rejects([&]{const g1::M3GSequenceSampler value(invalid);},"nonquaternion SQUAD accepted");
        invalid=source;invalid.keyframes.front().time=-1;
        rejects([&]{const g1::M3GSequenceSampler value(invalid);},"negative cubic first key time accepted");
        invalid=source;invalid.keyframes.front().value[0]=std::numeric_limits<float>::infinity();
        rejects([&]{const g1::M3GSequenceSampler value(invalid);},"nonfinite cubic input accepted");
        invalid=source;invalid.keyframes={{10,{1,2,3}},{10,{4,5,6}},{10,{7,8,9}}};invalid.first=0;invalid.last=2;
        rejects([&]{const g1::M3GSequenceSampler value(invalid);},"zero auxiliary interval accepted");
        rejects([&]{(void)immutable.sample(std::numeric_limits<float>::quiet_NaN());},"nonfinite curve time accepted");
        auto partial=corpus.at(8).sequence;
        partial.keyframes.push_back({0,{0,0,0,8}});
        const g1::M3GSequenceSampler range(partial);
        check(range.keyframes().back().value[3]==8,"outside-range quaternion normalized");
        check(range.keyframes().back().outgoing.empty() && range.keyframes().back().incoming.empty(),"outside-range controls prepared");
        check(range.sample(47)==g1::m3g_sample_sequence(partial,47),"cached and one-shot sampling differ");
        std::cout<<"{\"original_sequences\":"<<corpus.size()<<",\"original_samples\":"<<samples
                 <<",\"prepared_scalar_comparisons\":"<<prepared_scalars<<",\"sample_scalar_comparisons\":"<<sampled_scalars
                 <<",\"prepared_binary32_identical\":"<<prepared_exact<<",\"sample_binary32_identical\":"<<sampled_exact
                 <<",\"maximum_host_math_error\":"<<maximum_math_error<<",\"host_math_tolerance\":0.000002"
                 <<",\"native_contracts\":"<<contracts<<",\"passed\":true}\n";
        return 0;
    } catch(const std::exception& error) { std::cerr<<"FAIL: "<<error.what()<<'\n';return 1; }
}
