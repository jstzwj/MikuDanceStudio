#include "mikudancestudio/rhi_material_textures.hpp"
#include <cmath>
#include <algorithm>
namespace mikudancestudio::rhi {
bool EvaluateMaterialTextureCascade(const MaterialTextureLayout& layout,
                                    const std::array<float, 4>& diffuse,
                                    const std::array<std::array<float, 4>, 3>& samples,
                                    std::array<float, 4>& output) {
    output = {};
    auto current = diffuse;
    for (auto v : diffuse)
        if (!std::isfinite(v) || v < 0 || v > 1)
            return false;
    for (unsigned stage = 0; stage < 3; ++stage) {
        const auto& s = layout.stages[stage];
        if (s.role == MaterialTextureRole::None)
            continue;
        if (static_cast<unsigned>(s.role) > 3 ||
            (s.color != MaterialCombine::Modulate && s.color != MaterialCombine::Add) ||
            s.colorArg1 != MaterialColorSource::Texture ||
            s.colorArg2 !=
                (stage == 0 ? MaterialColorSource::LitDiffuse : MaterialColorSource::Previous) ||
            s.alpha != MaterialCombine::Modulate || s.alphaArg1 != MaterialColorSource::Texture ||
            s.alphaArg2 != MaterialColorSource::Previous)
            return false;
        const auto& sample = samples[stage];
        for (auto v : sample)
            if (!std::isfinite(v) || v < 0 || v > 1)
                return false;
        for (unsigned i = 0; i < 3; ++i)
            current[i] = std::clamp(s.color == MaterialCombine::Add ? current[i] + sample[i]
                                                                    : current[i] * sample[i],
                                    0.f, 1.f);
        current[3] *= sample[3];
    }
    output = current;
    return true;
}
bool MaterialAlphaTest(float alpha, bool enabled, DepthComparison compare, std::uint8_t reference) {
    if (!std::isfinite(alpha) || alpha < 0 || alpha > 1 || static_cast<unsigned>(compare) > 7)
        return false;
    if (!enabled)
        return true;
    alpha = std::floor(alpha * 255.0f + 0.5f);
    const float value = reference;
    switch (compare) {
    case DepthComparison::Never:
        return false;
    case DepthComparison::Less:
        return alpha < value;
    case DepthComparison::Equal:
        return alpha == value;
    case DepthComparison::LessEqual:
        return alpha <= value;
    case DepthComparison::Greater:
        return alpha > value;
    case DepthComparison::NotEqual:
        return alpha != value;
    case DepthComparison::GreaterEqual:
        return alpha >= value;
    case DepthComparison::Always:
        return true;
    }
    return false;
}
bool BuildShaderNormalMatrix(const std::array<float, 16>& world, const std::array<float, 16>& view,
                             std::array<float, 16>& normal) {
    normal = {};
    double m[3][3]{};
    for (unsigned i = 0; i < 3; ++i)
        for (unsigned j = 0; j < 3; ++j)
            for (unsigned k = 0; k < 3; ++k)
                m[i][j] += static_cast<double>(world[i * 4 + k]) * view[k * 4 + j];
    const double determinant = m[0][0] * (m[1][1] * m[2][2] - m[1][2] * m[2][1]) -
                               m[0][1] * (m[1][0] * m[2][2] - m[1][2] * m[2][0]) +
                               m[0][2] * (m[1][0] * m[2][1] - m[1][1] * m[2][0]);
    if (!std::isfinite(determinant) || determinant == 0)
        return false;
    for (unsigned i = 0; i < 3; ++i)
        for (unsigned j = 0; j < 3; ++j) {
            const auto a = (i + 1) % 3, b = (i + 2) % 3, c = (j + 1) % 3, d = (j + 2) % 3;
            normal[i * 4 + j] =
                static_cast<float>((m[a][c] * m[b][d] - m[a][d] * m[b][c]) / determinant);
            if (!std::isfinite(normal[i * 4 + j])) {
                normal = {};
                return false;
            }
        }
    normal[15] = 1;
    return true;
}
std::array<float, 4> TransformShaderVector(const std::array<float, 4>& v,
                                           const std::array<float, 16>& m) {
    std::array<float, 4> out{};
    for (unsigned j = 0; j < 4; ++j)
        for (unsigned i = 0; i < 4; ++i)
            out[j] += v[i] * m[i * 4 + j];
    return out;
}
bool ValidateMaterialShaderInputs(const MaterialShaderInputs& s) {
    const auto finite = [](const auto& values) {
        for (auto v : values)
            if (!std::isfinite(v))
                return false;
        return true;
    };
    if (!finite(s.world) || !finite(s.view) || !finite(s.projection) || !finite(s.normalView) ||
        !finite(s.nativeDiffuse) || !finite(s.diffuse) || !finite(s.materialAmbient) ||
        !finite(s.materialSpecular) || !finite(s.emissive) || !finite(s.globalAmbient) ||
        !std::isfinite(s.shininess) || s.shininess < 0)
        return false;
    // Affine world/view; perspective projection must retain its fourth column.
    for (const auto* m : {&s.world, &s.view})
        if ((*m)[3] != 0 || (*m)[7] != 0 || (*m)[11] != 0 || (*m)[15] != 1)
            return false;
    if (s.normalView[3] != 0 || s.normalView[7] != 0 || s.normalView[11] != 0 ||
        s.normalView[12] != 0 || s.normalView[13] != 0 || s.normalView[14] != 0 ||
        s.normalView[15] != 1)
        return false;
    for (auto source : s.materialSources)
        if (source > 2)
            return false;
    for (const auto& l : s.lights)
        if (l.enabled && (!finite(l.direction) || !finite(l.diffuse) || !finite(l.ambient) ||
                          !finite(l.specular) ||
                          l.direction[0] * l.direction[0] + l.direction[1] * l.direction[1] +
                                  l.direction[2] * l.direction[2] ==
                              0))
            return false;
    if (static_cast<unsigned>(s.alphaCompare) > 7 || static_cast<unsigned>(s.layout.sphere) > 3)
        return false;
    for (unsigned i = 0; i < 3; ++i) {
        const auto& stage = s.layout.stages[i];
        const auto& sampler = s.samplers[i];
        if (static_cast<unsigned>(stage.role) > 3 || static_cast<unsigned>(stage.coordinates) > 2 ||
            static_cast<unsigned>(stage.color) > 3 || static_cast<unsigned>(stage.alpha) > 3 ||
            static_cast<unsigned>(stage.colorArg1) > 2 ||
            static_cast<unsigned>(stage.colorArg2) > 2 ||
            static_cast<unsigned>(stage.alphaArg1) > 2 ||
            static_cast<unsigned>(stage.alphaArg2) > 2)
            return false;
        if (stage.role != MaterialTextureRole::None &&
            (!finite(s.textureTransforms[i]) || sampler.maxAnisotropy == 0 ||
             static_cast<unsigned>(sampler.min) > 2 || static_cast<unsigned>(sampler.mag) > 2 ||
             static_cast<unsigned>(sampler.mip) > 2 || static_cast<unsigned>(sampler.u) > 1 ||
             static_cast<unsigned>(sampler.v) > 1))
            return false;
    }
    return true;
}
}
