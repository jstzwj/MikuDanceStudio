#include "mikudancestudio/cpu_math.hpp"
#include "mikudancestudio/cpu_deform.hpp"
#include "mikudancestudio/vmd_bone_interpolation.hpp"
#include <cmath>
#include <cstdio>
#include <limits>
using namespace mikudancestudio;
using namespace cpu_math;
#define CHECK(x) do { if (!(x)) { std::fprintf(stderr,"line %d: %s\n",__LINE__,#x); return 1; } } while (0)
bool Near(float a,float b) { return std::abs(a-b)<=3e-6f; }
int main() {
    CHECK(sizeof(Matrix)==64 && sizeof(Vec3)==12 && sizeof(Quaternion)==16);
    const auto identity=Identity(), translation=Translation({3,-2,4});
    CHECK((TransformPoint({1,2,3},translation)==Vec3{4,0,7}));
    CHECK((TransformDirection({1,2,3},translation)==Vec3{1,2,3}));
    CHECK(Multiply(identity,translation)==translation && Transpose(Transpose(translation))==translation);
    const Quaternion q{0,0,std::sqrt(.5f),std::sqrt(.5f)};
    const auto rotation=RotationQuaternion(q);
    const auto point=TransformPoint({1,0,0},Multiply(rotation,translation));
    CHECK(Near(point[0],3) && Near(point[1],-1) && Near(point[2],4));
    const auto other=TransformPoint({1,0,0},Multiply(translation,rotation));
    CHECK(!Near(other[0],point[0])); // order cannot commute
    const auto local=BoneLocal({2,0,0},{0,3,0},q);
    const auto pivot=TransformPoint({2,0,0},local);
    CHECK(Near(pivot[0],2) && Near(pivot[1],3));
    Matrix inverse=Multiply(rotation,translation);
    CHECK(Inverse(inverse,inverse)); // alias-safe output
    const auto roundtrip=Multiply(Multiply(rotation,translation),inverse);
    for (unsigned n=0;n<16;++n) CHECK(Near(roundtrip[n],identity[n]));
    auto retained=translation;
    CHECK(!Inverse(Matrix{},retained) && retained==translation);
    auto invalid=identity; invalid[0]=std::numeric_limits<float>::infinity();
    CHECK(!Inverse(invalid,retained) && retained==translation);
    Vec3 normalized{3,0,4}; CHECK(Normalize(normalized,normalized));
    CHECK(Near(normalized[0],.6f) && Near(normalized[2],.8f));
    const auto saved=normalized;
    CHECK(!Normalize(Vec3{},normalized) && normalized==saved);
    Quaternion unit; CHECK(Normalize(Quaternion{0,0,0,2},unit));
    CHECK((unit==Quaternion{0,0,0,1}));
    const auto a=HamiltonProduct({1,0,0,0},{0,1,0,0});
    const auto b=HamiltonProduct({0,1,0,0},{1,0,0,0});
    CHECK((a==Quaternion{0,0,1,0} && b==Quaternion{0,0,-1,0}));
    const float v[3]{1,2,3};
    CHECK(cpu_deform::BlendTransformComponent(v,identity.data(),translation.data(),0,.25f,true)==3.25f);
    CHECK(cpu_deform::BlendTransformComponent(v,identity.data(),translation.data(),1,.25f,false)==2);
    Matrix m[4]{identity,identity,identity,identity};
    m[0][12]=-1e20f; m[1][12]=1e20f; m[2][12]=3; m[3][12]=4;
    const float* matrices[]{m[0].data(),m[1].data(),m[2].data(),m[3].data()};
    const float weights[]{.25f,.25f,.25f,.25f}, zero[3]{};
    float p[3],n[3]; cpu_deform::Bdef4(p,n,zero,v,matrices,weights);
    CHECK(p[0]==1.75f && n[0]==1 && n[1]==2 && n[2]==3);
    float morphed[3]{1,2,3}; const float offset[3]{2,-4,8};
    cpu_deform::AccumulateVertexMorph(morphed,offset,.5f,.25f);
    CHECK(morphed[0]==1.25f && morphed[1]==1.5f && morphed[2]==4);
    std::array<std::uint8_t,64> bytes{};
    for (unsigned i=0;i<64;++i) bytes[i]=static_cast<std::uint8_t>(i);
    const auto curve=format::DecodeVmdBoneInterpolation(bytes);
    CHECK(curve[0]==0 && curve[1]==16 && curve[4]==4 && curve[15]==60);
    std::puts("CPU math conventions, checked inverse/normalize, deform order and VMD layout passed");
    return 0;
}
