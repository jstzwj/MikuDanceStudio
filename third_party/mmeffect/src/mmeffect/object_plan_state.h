#pragma once

#include <d3d9.h>

namespace mme {

// Original ModelData +ec..+137: drawing order, host index, visibility and
// an ordinary world matrix. The four 1.0 stores are its diagonal, not colors.
struct ObjectPlanState {
    int renderOrder = 0;
    int passKey = -1;
    unsigned char flag = 0;
    D3DMATRIX world = Identity();

    static D3DMATRIX Identity() {
        D3DMATRIX value = {};
        value.m[0][0] = value.m[1][1] = value.m[2][2] = value.m[3][3] = 1.0f;
        return value;
    }
};

} // namespace mme
