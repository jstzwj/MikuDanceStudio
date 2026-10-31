#pragma once
#include <cmath>

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
}inline void BlendSdefQuaternions(float out[4], const float from[4], const float to[4], float t) {
    const float dot = ((to[0] * from[0] + to[3] * from[3]) + to[1] * from[1]) + to[2] * from[2];
    float delta[4];
    if (dot >= 0.0f) {
        delta[0] = to[0] - from[0];
        delta[1] = to[1] - from[1];
        delta[2] = to[2] - from[2];
        delta[3] = to[3] - from[3];
    } else {
        t = -t;
        delta[0] = to[0] + from[0];
        delta[1] = to[1] + from[1];
        delta[2] = to[2] + from[2];
        delta[3] = to[3] + from[3];
    }
    out[0] = delta[0] * t + from[0];
    out[1] = delta[1] * t + from[1];
    out[2] = delta[2] * t + from[2];
    out[3] = delta[3] * t + from[3];
    const float length =
        std::sqrt(((out[0] * out[0] + out[1] * out[1]) + out[2] * out[2]) + out[3] * out[3]);
    if (length <= 0.0f) {
        out[0] = 0.0f;
        out[1] = 0.0f;
        out[2] = 0.0f;
        out[3] = 1.0f;
        return;
    }
    const float inverse = 1.0f / length;
    out[0] *= inverse;
    out[1] *= inverse;
    out[2] *= inverse;
    out[3] *= inverse;
}


template<class Ops>
void Sdef(float position[3],float normal[3],const float input[3],const float direction[3],
          const float center[3],const float r0[3],const float r1[3],const float* matrix0,
          const float* matrix1,float weight0,Ops& ops) {
    const float weight1=1.f-weight0;
    float pivot[3];
    for(int c=0;c<3;++c) {
        const float blendedCenter=TransformComponent(center,matrix0,c,true)*weight0+
                                  TransformComponent(center,matrix1,c,true)*weight1;
        const float blendedArms=TransformComponent(r0,matrix0,c,false)*weight0+
                                TransformComponent(r1,matrix1,c,false)*weight1;
        pivot[c]=((blendedArms+blendedCenter)+blendedCenter)*.5f;
    }
    float q0[4],q1[4],q[4],rotation[16];
    ops.QuatFromMatrix(q0,matrix0);ops.QuatFromMatrix(q1,matrix1);
    BlendSdefQuaternions(q,q0,q1,weight1);ops.RotationMatrix(rotation,q);
    const float delta[]{input[0]-center[0],input[1]-center[1],input[2]-center[2]};
    for(int c=0;c<3;++c) {
        position[c]=TransformComponent(delta,rotation,c,false)+pivot[c];
        normal[c]=TransformComponent(direction,rotation,c,false);
    }
}

}
