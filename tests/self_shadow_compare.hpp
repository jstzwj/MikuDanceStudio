#pragma once
#include "mikudancestudio/rhi_render_pass.hpp"
#include <algorithm>
#include <cmath>
#include <cstring>
#include <stdexcept>

namespace shadow_acceptance {
// Numerical portability policy for the new built-in self-shadow gate only.
// Exact comparison remains available; neither image nor reference is modified.
inline constexpr unsigned RgbTolerance = 1;
inline constexpr double DepthTolerance = 1e-6;
struct Difference {
    std::size_t elements = 0, outsideTolerance = 0, coverageDifferences = 0;
    double maximum = 0;
    bool Exact() const { return elements == 0; }
    bool Accepted() const { return outsideTolerance == 0 && coverageDifferences == 0; }
};
inline Difference Compare(const mikudancestudio::rhi::AttachmentReadback& a,
                          const mikudancestudio::rhi::AttachmentReadback& b) {
    using mikudancestudio::rhi::TextureFormat;
    const auto& desc = a.description;
    if (desc.format != b.description.format || desc.width != b.description.width ||
        desc.height != b.description.height || !desc.width || !desc.height ||
        a.bytes.size() != std::size_t(desc.width) * desc.height * 4 ||
        a.bytes.size() != b.bytes.size() ||
        (desc.format != TextureFormat::Rgba8 && desc.format != TextureFormat::R32Float))
        throw std::runtime_error("self-shadow readback shape/format mismatch");
    Difference d;
    for (std::size_t i = 0; i < a.bytes.size(); i += 4) {
        if (desc.format == TextureFormat::R32Float) {
            float x, y;
            std::memcpy(&x, a.bytes.data() + i, 4);
            std::memcpy(&y, b.bytes.data() + i, 4);
            if (!std::isfinite(x) || !std::isfinite(y))
                throw std::runtime_error("nonfinite shadow output");
            const double error = std::fabs(double(x) - double(y));
            d.elements += std::memcmp(&x, &y, 4) != 0;
            d.maximum = std::max(d.maximum, error);
            d.outsideTolerance += error > DepthTolerance;
            // A cleared texel remains distinguishable from a written texel.
            d.coverageDifferences += (x < 1) != (y < 1);
        } else {
            bool xCovered = false, yCovered = false;
            for (unsigned c = 0; c < 4; ++c) {
                const auto x = a.bytes[i + c], y = b.bytes[i + c];
                const auto error = unsigned(std::abs(int(x) - int(y)));
                d.elements += error != 0;
                d.maximum = std::max(d.maximum, double(error));
                // Alpha controls blending and retains its exact byte contract.
                d.outsideTolerance += error > (c == 3 ? 0 : RgbTolerance);
                if (c < 3) {
                    xCovered |= x != 255;
                    yCovered |= y != 255;
                }
            }
            d.coverageDifferences += xCovered != yCovered;
        }
    }
    return d;
}
}
