#include "g1/M3GCamera.hpp"
#include "m3g_camera_fixture.hpp"
#include "m3g_animation_fixture.hpp"

#include <cmath>
#include <cstring>
#include <functional>
#include <iomanip>
#include <iostream>
#include <limits>
#include <sstream>

namespace {
std::size_t comparisons=0,contracts=0;
void check(bool v,const char* text) { ++contracts;if(!v)throw std::runtime_error(text); }
void rejects(const std::function<void()>& action,const char* text) {
    bool failed=false;try { action(); }catch(const g1::FormatError&) { failed=true; }check(failed,text);
}
std::uint32_t bits(float value) { std::uint32_t result;std::memcpy(&result,&value,4);return result; }
float from_bits(std::uint32_t raw) { float result;std::memcpy(&result,&raw,4);return result; }
std::string hex(std::uint32_t raw) {
    std::ostringstream text;text<<"0x"<<std::hex<<std::setfill('0')<<std::setw(8)<<raw;return text.str();
}
float platform_tangent(float value) {
    // Query the target libm, not the compiler's constant-folded host answer.
    float (*volatile call)(float)=static_cast<float(*)(float)>(std::tan);
    return call(value);
}
const g1_camera_fixture::TangentBoundary* tangent_boundary(std::size_t case_index) {
    for(const auto& boundary:g1_camera_fixture::tangent_boundaries)
        if(boundary.case_index==case_index)return &boundary;
    return nullptr;
}
g1::Bytes camera_file(std::uint8_t type,const std::vector<float>& fields) {
    auto camera=g1_test::node();camera.push_back(type);for(auto v:fields)g1_test::scalar(camera,v);
    auto raw=g1_test::chunk(5,camera);auto world=g1_test::node();
    g1_test::u32(world,1);g1_test::u32(world,2);g1_test::u32(world,2);g1_test::u32(world,0);
    g1_test::append(raw,g1_test::chunk(22,world));return g1_test::file(raw);
}
}

int main() {
    try {
        std::ostringstream boundary_results;std::size_t boundary_count=0,host_boundary_differences=0;
        for(std::size_t index=0;index<g1_camera_fixture::cases.size();++index) {
            const auto& row=g1_camera_fixture::cases[index];
            g1::M3GCameraProjection camera;camera.type=row.type;camera.parameters=row.parameters;
            const auto matrix=g1::m3g_camera_projection_matrix(camera);
            const auto* expected=&row.matrix_bits;
            const auto* boundary=tangent_boundary(index);
            std::ostringstream context;context<<"case="<<index<<" type="<<unsigned(row.type)
                <<" parameters=["<<row.parameters[0]<<','<<row.parameters[1]<<','<<row.parameters[2]<<','<<row.parameters[3]<<']';
            check((row.type==50)==(boundary!=nullptr),"perspective fixture missing captured tangent boundary");
            if(boundary) {
                const auto observed=bits(platform_tangent(from_bits(boundary->input_bits)));
                context<<" tanf_input="<<hex(boundary->input_bits)<<" target_tanf="<<hex(observed)
                    <<" historical_host_tanf="<<hex(boundary->historical_output_bits)
                    <<" tangent_bounds=["<<hex(boundary->lower_bits)<<','<<hex(boundary->upper_bits)<<']';
                // Only this external libm result may choose between two exact
                // original replays. Every matrix scalar below stays bit-exact.
                if(observed==boundary->lower_bits)expected=&boundary->lower_matrix_bits;
                else if(observed==boundary->upper_bits)expected=&boundary->upper_matrix_bits;
                else throw std::runtime_error("target tanf is outside the proven adjacent-float bracket: "+context.str());
                if(boundary_count++)boundary_results<<',';
                boundary_results<<"{\"case\":"<<index<<",\"input_bits\":"<<boundary->input_bits
                    <<",\"target_output_bits\":"<<observed<<",\"historical_output_bits\":"<<boundary->historical_output_bits
                    <<",\"lower_bits\":"<<boundary->lower_bits<<",\"upper_bits\":"<<boundary->upper_bits<<'}';
                if(observed!=boundary->historical_output_bits)++host_boundary_differences;
            }
            for(std::size_t n=0;n<16;++n) {
                ++comparisons;
                if(bits(matrix[n])!=(*expected)[n])throw std::runtime_error("original camera projection bits differ: "+
                    context.str()+" element="+std::to_string(n)+" actual="+hex(bits(matrix[n]))+" expected="+hex((*expected)[n]));
            }
        }
        const auto decoded=g1::decode_m3g_cameras(camera_file(50,{30,1.5F,2,5000}));
        check(decoded.size()==1 && decoded.at(2).type==50 && decoded.at(2).parameters==std::array<float,4>{{30,1.5F,2,5000}},"serialized camera parameters");
        const auto generic=g1::decode_m3g_cameras(camera_file(48,{1,2,3,4,5,6,7,8,9,10,11,12,13,14,15,16}));
        check(generic.at(2).generic==g1::M3GMatrix{{1,5,9,13,2,6,10,14,3,7,11,15,4,8,12,16}},"generic projection transpose");
        auto camera=decoded.at(2);
        const g1::M3GMatrix identity{{1,0,0,0,0,1,0,0,0,0,1,0,0,0,0,1}};
        const auto metal=g1::m3g_camera_metal_view_projection(camera,identity);
        const auto depth=[&](float z) { return (metal[10]*z+metal[14])/(metal[11]*z+metal[15]); };
        check(std::abs(depth(-2))<0.000001F && std::abs(depth(-5000)-1)<0.000001F,"Metal depth mapping");
        const g1::M3GMatrix posed{{0,2,0,0,-3,0,0,0,0,0,4,0,5,-6,7,1}};
        const auto inverse=g1::m3g_camera_view_matrix(posed);
        const auto unit=g1::m3g_multiply(inverse,posed);
        for(std::size_t n=0;n<16;++n)check(std::abs(unit[n]-identity[n])<0.000001F,"affine camera inverse");
        auto bad=posed;bad[0]=bad[1]=bad[2]=0;
        rejects([&]{(void)g1::m3g_camera_view_matrix(bad);},"singular camera accepted");
        bad={{2895626,6180765,9076391,0,2933305,5289181,8222486,0,6895124,4568742,11463866,0,0,0,0,1}};
        rejects([&]{(void)g1::m3g_camera_view_matrix(bad);},"exactly dependent float32 rows survived cofactor cancellation");
        bad=posed;bad[3]=1;rejects([&]{(void)g1::m3g_camera_view_matrix(bad);},"nonaffine camera accepted");
        bad=posed;bad[0]=std::numeric_limits<float>::infinity();rejects([&]{(void)g1::m3g_camera_view_matrix(bad);},"nonfinite camera accepted");
        camera.parameters[3]=camera.parameters[2];rejects([&]{(void)g1::m3g_camera_projection_matrix(camera);},"zero-volume projection accepted");
        camera=decoded.at(2);camera.parameters[0]=180;rejects([&]{(void)g1::m3g_camera_projection_matrix(camera);},"invalid perspective fovy accepted");
        camera=decoded.at(2);camera.parameters[1]=0;rejects([&]{(void)g1::m3g_camera_projection_matrix(camera);},"zero aspect accepted");
        camera=decoded.at(2);camera.parameters[2]=-1;rejects([&]{(void)g1::m3g_camera_projection_matrix(camera);},"negative perspective near accepted");
        rejects([&]{(void)g1::decode_m3g_cameras(camera_file(50,{30,1.5F,2}));},"truncated camera body accepted");
        std::cout<<"{\"original_cases\":"<<g1_camera_fixture::cases.size()<<",\"projection_comparisons\":"<<comparisons
                 <<",\"native_contracts\":"<<contracts<<",\"target_tanf_cases\":"<<boundary_count
                 <<",\"historical_host_tanf_differences\":"<<host_boundary_differences
                 <<",\"tanf_boundaries\":["<<boundary_results.str()<<"],\"passed\":true}\n";
        return 0;
    } catch(const std::exception& error) { std::cerr<<"FAIL: "<<error.what()<<'\n';return 1; }
}
