// Independent Windows CPU reference. No HWND or D3D device is created.
#include "mikudancestudio/cpu_math.hpp"
#include "mikudancestudio/d3dx_dyn.hpp"
#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstring>
using namespace mikudancestudio;
using namespace cpu_math;
#define CHECK(x) do { if (!(x)) { std::fprintf(stderr,"line %d: %s\n",__LINE__,#x); return 1; } } while (0)
bool Equal(const float* a,const float* b,unsigned count,float tolerance=4e-6f) {
    for(unsigned n=0;n<count;++n)
        if(!std::isfinite(a[n]) || !std::isfinite(b[n]) ||
           std::abs(a[n]-b[n])>tolerance*std::max(1.f,std::abs(b[n]))) return false;
    return true;
}
int main() {
    for (int sample=0;sample<64;++sample) {
        Quaternion q; CHECK(Normalize(Quaternion{.01f*sample,.2f,-.3f,1},q));
        auto rotation=RotationQuaternion(q);
        d3dx::D3DXMATRIXF nativeRotation, nativeA, nativeB, nativeProduct;
        d3dx::Get().matrixRotationQuaternion(&nativeRotation,q.data());
        CHECK(Equal(rotation.data(),&nativeRotation.m[0][0],16));
        auto a=Multiply(rotation,Translation({.25f*sample,-2,3}));
        auto b=Multiply(Translation({1,3,-2}),RotationQuaternion({.5f,0,0,.8660254f}));
        std::memcpy(&nativeA,a.data(),64); std::memcpy(&nativeB,b.data(),64);
        d3dx::Get().multiply(&nativeProduct,&nativeA,&nativeB);
        CHECK(Equal(Multiply(a,b).data(),&nativeProduct.m[0][0],16));
        Matrix inverse; CHECK(Inverse(a,inverse));
        D3DXMATRIX referenceInverse;
        CHECK(D3DXMatrixInverse(&referenceInverse,nullptr,reinterpret_cast<D3DXMATRIX*>(&nativeA)));
        CHECK(Equal(inverse.data(),&referenceInverse.m[0][0],16,2e-5f));
        Quaternion other; CHECK(Normalize(Quaternion{.4f,.1f,.02f*sample,1},other));
        Quaternion referenceProduct;
        d3dx::Get().quatMultiply(referenceProduct.data(),other.data(),q.data());
        CHECK(Equal(HamiltonProduct(q,other).data(),referenceProduct.data(),4));
    }
    std::puts("64 portable matrix/quaternion cases agree with independent D3DX CPU imports");
    return 0;
}
