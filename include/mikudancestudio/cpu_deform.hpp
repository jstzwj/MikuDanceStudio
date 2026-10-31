#pragma once

// Extracted from model_skinning.cpp's measured PMX workers. Keep expression
// order and float stores: BDEF2 accumulates bone 1 first; BDEF4's position and
// normal accumulate in different orders. x86 legacy BDEF2 retains its x87
// adapter in model_skinning.cpp. No D3D, native model records or allocation.
namespace mikudancestudio::cpu_deform {
inline void TransformPosition(float out[3], const float in[3], const float* m) {
    out[0] = in[0] * m[0] + in[1] * m[4] + in[2] * m[8] + m[12];
    out[1] = in[0] * m[1] + in[1] * m[5] + in[2] * m[9] + m[13];
    out[2] = in[0] * m[2] + in[1] * m[6] + in[2] * m[10] + m[14];
}
inline void TransformNormal(float out[3], const float in[3], const float* m) {
    out[0] = in[0] * m[0] + in[1] * m[4] + in[2] * m[8];
    out[1] = in[0] * m[1] + in[1] * m[5] + in[2] * m[9];
    out[2] = in[0] * m[2] + in[1] * m[6] + in[2] * m[10];
}
inline float BlendTransformComponent(const float* in, const float* a, const float* b,
                                     int c, float weight, bool position) {
    const float other = 1.0f - weight;
    const float x = in[0] * a[c] + in[1] * a[c + 4] + in[2] * a[c + 8] +
                    (position ? a[c + 12] : 0.0f);
    const float y = in[0] * b[c] + in[1] * b[c + 4] + in[2] * b[c + 8] +
                    (position ? b[c + 12] : 0.0f);
    return y * other + x * weight;
}
inline float TransformComponent(const float* in, const float* m, int c, bool position) {
    float value = in[0] * m[c];
    value += in[1] * m[c + 4];
    value += in[2] * m[c + 8];
    if (position) value += m[c + 12];
    return value;
}
inline void Bdef4(float position[3], float normal[3], const float input[3],
                  const float direction[3], const float* const m[4], const float weights[4]) {
    for (int c = 0; c < 3; ++c) {
        float p = TransformComponent(input, m[1], c, true) * weights[1];
        p += TransformComponent(input, m[0], c, true) * weights[0];
        p += TransformComponent(input, m[2], c, true) * weights[2];
        p += TransformComponent(input, m[3], c, true) * weights[3];
        position[c] = p;
        float n = TransformComponent(direction, m[0], c, false) * weights[0];
        n += TransformComponent(direction, m[1], c, false) * weights[1];
        n += TransformComponent(direction, m[2], c, false) * weights[2];
        n += TransformComponent(direction, m[3], c, false) * weights[3];
        normal[c] = n;
    }
}
inline void AccumulateVertexMorph(float position[3], const float offset[3], float weight,
                                   float referenceWeight = 1.0f) {
    position[0] += offset[0] * weight * referenceWeight;
    position[1] += offset[1] * weight * referenceWeight;
    position[2] += offset[2] * weight * referenceWeight;
}
}
