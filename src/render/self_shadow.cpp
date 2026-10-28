#include "mikudancestudio/render_self_shadow.hpp"
#include <cmath>
namespace mikudancestudio::render {
bool BuildSelfShadowTransform(const ModelMatrix& world, const std::array<float, 3>& target,
                              const std::array<float, 3>& light, float base, int mode,
                              ModelMatrix& output, const ModelInputMath& math) {
    if (!math.multiply || !math.normalize || !math.lookAtTarget || !std::isfinite(base) ||
        (mode != 1 && mode != 2))
        return false;
    for (float v : world)
        if (!std::isfinite(v))
            return false;
    for (const auto& a : {target, light})
        for (float v : a)
            if (!std::isfinite(v))
                return false;
    const std::array<float, 3> eye{target[0] - light[0] * 50, target[1] - light[1] * 50,
                                   target[2] - light[2] * 50};
    ModelMatrix view;
    math.lookAtTarget(view, eye, target, {0, 0, 1});
    auto direction = light;
    math.normalize(direction);
    const float slope = -direction[2], edge = 1 - std::fabs(slope);
    const bool one = mode == 1;
    float scale;
    if (one) {
        if (slope < -.9f || slope > .9f)
            scale = base;
        else if (slope < -.8f)
            scale = ((slope - .1f) * 10 + 10) * base + base;
        else if (slope > .8f)
            scale = (10 - (slope + .1f) * 10) * base + base;
        else
            scale = base + base;
    } else {
        if (slope < -.9f || slope > .9f)
            scale = base;
        else if (slope < -.8f)
            scale = (slope * 20 + 19) * base;
        else if (slope > .8f)
            scale = (19 - slope * 20) * base;
        else
            scale = base * 3;
    }
    const ModelMatrix identity{1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1};
    ModelMatrix projection{};
    projection[0] = projection[5] = scale;
    projection[10] = base * .1500000059604645f;
    projection[15] = 1;
    ModelMatrix temporary, result;
    math.multiply(temporary, world, view);
    math.multiply(result, temporary, projection);
    auto transform = identity;
    transform[10] = 2;
    math.multiply(result, result, transform);
    transform = identity;
    transform[14] = -1;
    math.multiply(result, result, transform);
    float positive, negative;
    if (slope < -.9f || slope > .9f) {
        positive = edge;
        negative = -edge;
    } else if (slope < -.8f) {
        positive = one ? 4 * slope + 3.7f : 9 * slope + 8.2f;
        negative = -9 * slope - 8.2f;
    } else if (slope > .8f) {
        positive = one ? 3.7f - 4 * slope : 8.2f - 9 * slope;
        negative = 9 * slope - 8.2f;
    } else {
        positive = one ? .5f : 1.f;
        negative = -1;
    }
    transform = identity;
    transform[7] = positive;
    transform[13] = negative;
    math.multiply(result, result, transform);
    transform = identity;
    transform[14] = 1;
    math.multiply(result, result, transform);
    transform = identity;
    transform[10] = .5f;
    math.multiply(result, result, transform);
    for (float v : result)
        if (!std::isfinite(v))
            return false;
    output = result;
    return true;
}
}
