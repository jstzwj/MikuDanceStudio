#include <cstdio>
#include <cstring>

#include "mikudancestudio/d3dx_dyn.hpp"

int main(int argc, char** argv) {
    // The loader-failure test verifies that this marker is never reached.
    if (argc == 2) {
        FILE* marker = nullptr;
        if (fopen_s(&marker, argv[1], "wb") != 0) return 2;
        std::fputs("entered main", marker);
        std::fclose(marker);
    }
    mikudancestudio::d3dx::D3DXMATRIXF a{}, b{}, product{};
    auto& api = mikudancestudio::d3dx::Get();
    api.translation(&a, 1, 2, 3);
    api.scaling(&b, 2, 3, 4);
    api.multiply(&product, &a, &b);
    if (product.m[3][0] != 2 || product.m[3][1] != 6 || product.m[3][2] != 12) return 3;
    const float quaternion[4] = {0, 0, 0, 1};
    float axis[3]{}, angle = 0;
    api.quatToAxisAngle(quaternion, axis, &angle);
    if (angle != 0) return 4;
    D3DXMATRIX identity;
    D3DXMatrixIdentity(&identity);
    D3DXMATRIX transpose;
    D3DXMatrixTranspose(&transpose, &identity);
    if (std::memcmp(&transpose, &identity, sizeof(identity)) != 0) return 5;
    std::puts("D3DX direct imports passed");
}
