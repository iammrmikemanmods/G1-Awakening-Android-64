#include "g1/M3GCamera.hpp"

#include <cmath>
#include <cstring>
#include <limits>

namespace g1 {
namespace {
static_assert(sizeof(float)==4 && std::numeric_limits<float>::is_iec559,
              "G1 camera projection requires IEEE754 binary32");
void require(bool condition,const char* message) { if(!condition)throw FormatError(message); }
float from_bits(std::uint32_t bits) noexcept { float value;std::memcpy(&value,&bits,4);return value; }

void finite(const M3GMatrix& matrix) {
    for(const auto value:matrix)require(std::isfinite(value),"nonfinite M3G camera matrix");
}

class Reader {
public:
    Reader(const Bytes& bytes,std::size_t begin,std::size_t size):bytes_(bytes),at_(begin),end_(begin+size) {
        require(begin<=bytes.size() && size<=bytes.size()-begin,"camera body exceeds M3G section");
    }
    std::uint8_t byte() { require(at_<end_,"truncated M3G camera body");return bytes_[at_++]; }
    bool boolean() { const auto v=byte();require(v<=1,"invalid camera base boolean");return v!=0; }
    std::uint32_t u32() {
        std::uint32_t result=0;for(unsigned n=0;n<4;++n)result|=std::uint32_t(byte())<<(n*8U);return result;
    }
    void skip(std::size_t count) { require(count<=end_-at_,"truncated M3G camera base");at_+=count; }
    void skip_words(std::uint32_t count) { require(count<=(end_-at_)/4,"unbounded camera base words");skip(std::size_t(count)*4); }
    float scalar() { const auto result=from_bits(u32());require(std::isfinite(result),"nonfinite camera projection parameter");return result; }
    void finish() const { require(at_==end_,"trailing M3G camera body"); }
private:
    const Bytes& bytes_;std::size_t at_,end_;
};

void validate(const M3GCameraProjection& camera) {
    require(camera.type>=48 && camera.type<=50,"unsupported M3G camera projection type");
    if(camera.type==48) { finite(camera.generic);return; }
    const auto& p=camera.parameters;
    for(const auto v:p)require(std::isfinite(v),"nonfinite camera projection parameter");
    require(p[0]>0 && p[1]>0,"camera height/field of view and aspect must be positive");
    const float difference=p[3]-p[2];
    require(std::isfinite(difference) && std::fabs(difference)>from_bits(0x377ba882U),
            "camera clipping-plane separation is below the original loader bound");
    if(camera.type==50)
        require(p[0]<180 && p[2]>0 && p[3]>0,"invalid original perspective camera parameters");
}
} // namespace

std::map<std::size_t,M3GCameraProjection> decode_m3g_cameras(const Bytes& bytes) {
    // The existing decoder checks complete object framing, serialized Float32
    // restrictions, graph/reference validity and all shared Node fields.
    (void)M3GScene(bytes);
    const M3GFile file(bytes);
    std::map<std::size_t,M3GCameraProjection> result;
    for(const auto& object:file.objects()) {
        if(object.type!=5)continue;
        Reader reader(file.sections().at(object.section),object.payload_offset,object.payload_size);
        (void)reader.u32();reader.skip_words(reader.u32());
        const auto parameters=reader.u32();
        for(std::uint32_t i=0;i<parameters;++i) { (void)reader.u32();reader.skip(reader.u32()); }
        if(reader.boolean())reader.skip(40); // Translation, scale, angle, axis.
        if(reader.boolean())reader.skip(64); // Shared general matrix.
        (void)reader.boolean();(void)reader.boolean();(void)reader.byte();(void)reader.u32();
        if(reader.boolean())reader.skip(10); // Node alignment metadata only.
        M3GCameraProjection camera;camera.object=object.index;camera.type=reader.byte();
        if(camera.type==48) {
            for(std::size_t row=0;row<4;++row)
                for(std::size_t col=0;col<4;++col)camera.generic[col*4+row]=reader.scalar();
        } else {
            for(auto& v:camera.parameters)v=reader.scalar();
        }
        reader.finish();validate(camera);result.emplace(object.index,camera);
    }
    return result;
}

M3GMatrix m3g_camera_projection_matrix(const M3GCameraProjection& camera) {
    validate(camera);
    if(camera.type==48)return camera.generic;
    const auto& p=camera.parameters;
    const float depth=p[3]-p[2];
    float y;
    if(camera.type==49) {
        const float reciprocal=1.0F/p[0];y=reciprocal+reciprocal;
    } else {
        // Preserve all original binary32 operations before its platform tanf.
        float radians=p[0]*from_bits(0x3c8efa35U);
        radians=radians*from_bits(0x4e000000U);
        radians=radians*from_bits(0x30800000U);
        y=1.0F/std::tan(radians);
    }
    M3GMatrix matrix{};matrix[0]=y/p[1];matrix[5]=y;
    if(camera.type==49) {
        matrix[10]=-2.0F/depth;matrix[14]=-(p[3]+p[2])/depth;matrix[15]=1;
    } else {
        const float product=p[2]*p[3];
        matrix[10]=-(p[2]+p[3])/depth;matrix[11]=-1;
        matrix[14]=(product*-2.0F)/depth;
    }
    finite(matrix);return matrix;
}

M3GMatrix m3g_camera_view_matrix(const M3GMatrix& matrix) {
    finite(matrix);
    require(matrix[3]==0 && matrix[7]==0 && matrix[11]==0 && matrix[15]==1,
            "camera-to-world transform is not affine");
    const double a=matrix[0],b=matrix[4],c=matrix[8],d=matrix[1],e=matrix[5],f=matrix[9],
                 g=matrix[2],h=matrix[6],i=matrix[10];
    const double determinant=a*(e*i-f*h)-b*(d*i-f*g)+c*(d*h-e*g);
    double magnitude=0;
    for(std::size_t column=0;column<3;++column)
        for(std::size_t row=0;row<3;++row)
            if(std::fabs(double(matrix[column*4+row]))>magnitude)magnitude=std::fabs(double(matrix[column*4+row]));
    // An exactly singular binary32 basis can leave a nonzero residual in a
    // double cofactor sum. Bound that cancellation error relative to the
    // coefficient scale before attempting an inverse.
    const double floor=64.0*std::numeric_limits<double>::epsilon()*magnitude*magnitude*magnitude;
    require(std::isfinite(determinant) && std::fabs(determinant)>floor,
            "singular or numerically ill-conditioned camera-to-world transform");
    std::array<double,16> inverse{{
        (e*i-f*h)/determinant,(f*g-d*i)/determinant,(d*h-e*g)/determinant,0,
        (c*h-b*i)/determinant,(a*i-c*g)/determinant,(b*g-a*h)/determinant,0,
        (b*f-c*e)/determinant,(c*d-a*f)/determinant,(a*e-b*d)/determinant,0,
        0,0,0,1}};
    for(std::size_t row=0;row<3;++row)
        inverse[12+row]=-(inverse[row]*matrix[12]+inverse[4+row]*matrix[13]+inverse[8+row]*matrix[14]);
    M3GMatrix result{};
    for(std::size_t n=0;n<16;++n) {
        require(std::isfinite(inverse[n]) && std::fabs(inverse[n])<=std::numeric_limits<float>::max(),
                "camera inverse exceeds binary32 range");
        result[n]=static_cast<float>(inverse[n]);
    }
    return result;
}

M3GMatrix m3g_camera_metal_view_projection(const M3GCameraProjection& camera,const M3GMatrix& camera_to_world) {
    auto result=m3g_multiply(m3g_camera_projection_matrix(camera),m3g_camera_view_matrix(camera_to_world));
    finite(result);
    for(std::size_t column=0;column<4;++column)
        result[column*4+2]=result[column*4+2]*0.5F+result[column*4+3]*0.5F;
    finite(result);return result;
}

} // namespace g1
