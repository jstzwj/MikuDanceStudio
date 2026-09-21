// ===========================================================================
// VA 0x00401000 - QuaternionToMatrix3x4  (original: fcn.00401000)
// ===========================================================================
// __thiscall(out: float* this, in: float* quat).  Quaternion layout is
// DirectX order (x, y, z, w); output is a row-major 3x4 matrix (float[12])
// with the 4th column zeroed (pure rotation).
//
// Each architecture retains its original floating-point evaluation order.
// ===========================================================================
#include "mikudancestudio/globals.hpp"

namespace mikudancestudio {

void QuaternionToMatrix3x4(float* outMatrix, const float* quaternion) {
    const float qx = quaternion[0];
    const float qy = quaternion[1];
    const float qz = quaternion[2];
    const float qw = quaternion[3];

#if defined(_WIN64)
    // Preserve the SSE evaluation order of the x64 Bullet helper.
    const float normSquared = ((qx * qx + qy * qy) + qz * qz) + qw * qw;
    const float scale = g_QuatScaleFactor / normSquared;
    const float sx = qx * scale, sy = qy * scale, sz = qz * scale;
    const float wx = qw * sx, xx = qx * sx;
    const float wy = qw * sy, yy = qy * sy, xy = qx * sy;
    const float zz = qz * sz, wz = qw * sz, xz = qx * sz, yz = qy * sz;
    outMatrix[0] = 1.0f - (zz + yy);
    outMatrix[1] = xy - wz;
    outMatrix[2] = xz + wy;
    outMatrix[3] = 0.0f;
    outMatrix[4] = xy + wz;
    outMatrix[5] = 1.0f - (zz + xx);
    outMatrix[6] = yz - wx;
    outMatrix[7] = 0.0f;
    outMatrix[8] = xz - wy;
    outMatrix[9] = yz + wx;
    outMatrix[10] = 1.0f - (yy + xx);
    outMatrix[11] = 0.0f;
#else
    // original: fVar2 = *(0x5294C8) / (qw^2 + qz^2 + qx^2 + qy^2)
    const float normSquared = qw * qw + qz * qz + qx * qx + qy * qy;
    const float invNormScaled = g_QuatScaleFactor / normSquared;

    const float scaledQxQw = invNormScaled * qx * qw;  // fVar13
    const float scaledQy   = invNormScaled * qy;       // fVar3
    const float scaledQz   = invNormScaled * qz;       // fVar5

    // Row 0
    outMatrix[0]  = 1.0f - (scaledQy * qy + scaledQz * qz);   // M00
    outMatrix[1]  = qx * scaledQy - scaledQz * qw;            // M01
    outMatrix[2]  = qx * scaledQz + scaledQy * qw;            // M02
    outMatrix[3]  = 0.0f;                                     // pad

    // Row 1 (fVar1 reuse: qx -> invNormScaled * qx^2)
    outMatrix[4]  = qx * scaledQy + scaledQz * qw;            // M10
    const float scaledQxSq = qx * invNormScaled * qx;
    outMatrix[5]  = 1.0f - (scaledQxSq + scaledQz * qz);      // M11
    outMatrix[6]  = scaledQz * qy - scaledQxQw;               // M12
    outMatrix[7]  = 0.0f;                                     // pad

    // Row 2
    outMatrix[8]  = qx * scaledQz - scaledQy * qw;            // M20
    outMatrix[9]  = scaledQxQw + scaledQz * qy;               // M21
    outMatrix[10] = 1.0f - (scaledQy * qy + scaledQxSq);      // M22
    outMatrix[11] = 0.0f;                                     // pad
#endif
}

}  // namespace mikudancestudio
