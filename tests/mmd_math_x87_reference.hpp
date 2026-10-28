// Frozen pre-refactor x86 oracle; see mmd_math_x87_reference.cpp.
#pragma once
namespace mmd_math_x87_reference {
float sinTimes(float angle, float factor);
float cosine(float angle);
void integrateRotation(const float* w, float dt, float* axis, float* pdw);
// Frozen outline sequences from model_skinning.cpp before the C++ migration.
float pmxEdgeComponent(float position, float normal, float frameScale, float vertexScale,
                       float materialSize);
float edgeDistanceScale(float distance, float fov, float modelScale);
}
