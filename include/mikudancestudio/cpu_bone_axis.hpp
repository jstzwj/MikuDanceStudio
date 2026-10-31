#pragma once
#include <cmath>
namespace mikudancestudio::cpu_bone {
template <class Ops>
bool ConstrainRotationAxis(float* quaternion, const float (&boneAxis)[3], Ops& api) {
    float sourceAxis[3], angle;
    api.quatToAxisAngle(quaternion, sourceAxis, &angle);
    if (!std::isfinite(quaternion[3]))
        quaternion[3] = 1.0f;
    const float squaredW = quaternion[3] * quaternion[3];
    float sinHalf = 0.0f;
    if (squaredW <= 1.0f) {
        const float residual = 1.0f - squaredW;
#if defined(_M_IX86)
        // The x87 sqrt input and output each pass through a float slot.
        sinHalf = static_cast<float>(std::sqrt(static_cast<double>(residual)));
#else
        sinHalf = std::sqrt(residual);
#endif
    } else {
        quaternion[3] = 1.0f;
    }
#if defined(_M_IX86)
    // Original x87 sums stay wide until the explicit float stores. The dot
    // has a double store; the two squared lengths and their roots have float
    // stores, followed by a wide multiply/divide and a final float store.
    const double dot = static_cast<double>(boneAxis[2]) * sourceAxis[2] +
                       static_cast<double>(boneAxis[0]) * sourceAxis[0] +
                       static_cast<double>(boneAxis[1]) * sourceAxis[1];
    const float boneSquared = static_cast<float>(static_cast<double>(boneAxis[2]) * boneAxis[2] +
                                                 static_cast<double>(boneAxis[1]) * boneAxis[1] +
                                                 static_cast<double>(boneAxis[0]) * boneAxis[0]);
    const float sourceSquared =
        static_cast<float>(static_cast<double>(sourceAxis[0]) * sourceAxis[0] +
                           static_cast<double>(sourceAxis[1]) * sourceAxis[1] +
                           static_cast<double>(sourceAxis[2]) * sourceAxis[2]);
    const float boneLength = static_cast<float>(std::sqrt(static_cast<double>(boneSquared)));
    const float sourceLength = static_cast<float>(std::sqrt(static_cast<double>(sourceSquared)));
    const float cosine = static_cast<float>(dot / (static_cast<double>(sourceLength) * boneLength));
#else
    // Preserve the SSE sum order and binary32 intermediates.
    const float dot =
        sourceAxis[1] * boneAxis[1] + sourceAxis[0] * boneAxis[0] + sourceAxis[2] * boneAxis[2];
    const float sourceLength =
        std::sqrt(sourceAxis[1] * sourceAxis[1] + sourceAxis[0] * sourceAxis[0] +
                  sourceAxis[2] * sourceAxis[2]);
    const float boneLength = std::sqrt(boneAxis[1] * boneAxis[1] + boneAxis[0] * boneAxis[0] +
                                       boneAxis[2] * boneAxis[2]);
    const float cosine = dot / (sourceLength * boneLength);
#endif
    if (cosine < 0.0f)
        sinHalf = -sinHalf;
    for (int i = 0; i < 3; ++i)
        quaternion[i] = sinHalf * boneAxis[i];
    api.quatNormalize(quaternion, quaternion);
    return true;
}
}
