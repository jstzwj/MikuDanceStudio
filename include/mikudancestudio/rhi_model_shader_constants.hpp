#pragma once
#include "rhi_material_textures.hpp"
namespace mikudancestudio::rhi {
// Constant-buffer transport for the optional shader probe, matching HLSL b0.
// The semantic scene contract remains MaterialShaderInputs.
struct ModelVertexConstants {
    std::array<float, 16> w, v, p, n;
    std::array<std::array<float, 16>, 3> transforms;
    std::array<float, 4> diffuse, ambient, specular, emissive, globalAmbient;
    std::array<std::array<float, 4>, 8> direction, lightDiffuse, lightAmbient, lightSpecular;
    // coordinates.w: four 2-bit material-source selections, bit 8 colorVertex.
    // Only the PositionColor VS consumes it; the constant-buffer size is stable.
    std::array<unsigned, 4> flags, coordinates;
    std::array<float, 4> parameters;
};
static_assert(sizeof(ModelVertexConstants) == 1088, "HLSL constant buffer layout");
// Backend compatibility transport (b1), separate from semantic scene inputs.
struct ModelCompatibilityConstants {
    std::array<float, 16> combined{}, worldView{};
};
static_assert(sizeof(ModelCompatibilityConstants) == 128, "HLSL compatibility buffer layout");
struct BuiltinShadowConstants {
    std::array<float, 16> light, camera, rotation;
    std::array<float, 4> color, toon, specular, direction, eye;
    std::array<unsigned, 4> options;
    std::array<float, 4> dimensions;
};
static_assert(sizeof(BuiltinShadowConstants) == 304, "HLSL shadow buffer layout");
}
