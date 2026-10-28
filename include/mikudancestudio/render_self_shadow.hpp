#pragma once
#include "render_model_inputs.hpp"
namespace mikudancestudio::render {
inline constexpr std::array<float, 30> DefaultToonShadowColors{
    .80078101f, .80078101f, .80078101f, .95703101f, .87890601f, .87890601f, .60156298f, .60156298f,
    .60156298f, .96875f,    .93359399f, .91796899f, 1,          .90234399f, .86328101f, .76171899f,
    .671875f,   .011719f,   1,          1,          1,          1,          1,          1,
    1,          1,          1,          1,          1,          1};
struct SelfShadowSettings {
    // -1 uses the evaluated PMM shadow track, or disables it for a bare model.
    int mode = -1;
    // Nonpositive uses evaluated PMM distance, or the native .01125 default.
    float distance = 0;
    unsigned mapSize = 1024;
};
// Shared with the native DX9 renderer. Windows supplies measured D3DX CPU math;
// no device, native state reads or GPU resources participate in this operation.
bool BuildSelfShadowTransform(const ModelMatrix& world, const std::array<float, 3>& target,
                              const std::array<float, 3>& light, float distance, int mode,
                              ModelMatrix& output, const ModelInputMath&);
}
