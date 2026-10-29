#include "../include/mme_standard_api.h"
#include <algorithm>
#include <cctype>
#include <cmath>
#include <cstring>

namespace {
std::string Upper(std::string s) {
    for (auto& c : s)
        c = char(std::toupper(static_cast<unsigned char>(c)));
    return s;
}
}
bool MmeStandardClassify(const MmeLegacyVariable& v, MmeStandardBinding& out, std::string& reason) {
    MmeStandardBinding b;
    using K = MmeStandardBinding::Kind;
    auto semantic = Upper(v.semantic), object = Upper(v.object);
    const auto name = Upper(v.name);
    const auto fail = [&](const char* why) {
        reason = why;
        return false;
    };
    b.light = object == "LIGHT" || object == "LIGHT0";
    // Semantic claims take precedence over the native case-insensitive name table.
    if (semantic.empty()) {
        if (name == "MATWORLD")
            semantic = "WORLD";
        else if (name == "MATWORLDVIEWPROJ")
            semantic = "WORLDVIEWPROJECTION";
        else if (name == "LIGHTDIR")
            b.kind = K::LightDirection;
        else if (name == "USE_TEXTURE") {
            b.kind = K::BoolFlag;
            b.index = 0;
        } else if (name == "USE_SPHEREMAP") {
            b.kind = K::BoolFlag;
            b.index = 1;
        } else if (name == "USE_SUBTEXTURE") {
            b.kind = K::BoolFlag;
            b.index = 2;
        } else if (name == "USE_TOON") {
            b.kind = K::BoolFlag;
            b.index = 3;
        } else if (name == "TRANSP") {
            b.kind = K::BoolFlag;
            b.index = 4;
        } else if (name == "VERTEXCOUNT")
            b.kind = K::Count;
        else if (name == "SUBSETCOUNT") {
            b.kind = K::Count;
            b.index = 1;
        } else if (name == "MATLIGHTVIEWPROJ" || name == "MATROTATE" || name == "EGCOLOR" ||
                   name == "TOONCOLOR" || name == "SPCCOLOR" || name == "DIFCOLOR" ||
                   name == "PARTHF" || name == "SPADD" || name == "OPADD" || name == "TEXCADD" ||
                   name == "TEXCMUL" || name == "SPHCADD" || name == "SPHCMUL")
            return fail("MME_legacy_named_draw_parameter");
        // Place has no native setter. Preserve its instance value.
    }
    if (!semantic.empty() && semantic != "CONTROLOBJECT") {
        auto base = semantic;
        for (const auto& suffix :
             {std::string("INVERSETRANSPOSE"), std::string("TRANSPOSE"), std::string("INVERSE")})
            if (base.size() > suffix.size() &&
                base.compare(base.size() - suffix.size(), suffix.size(), suffix) == 0) {
                base.resize(base.size() - suffix.size());
                b.inverse = suffix != "TRANSPOSE";
                b.transpose = suffix != "INVERSE";
                break;
            }
        static const std::array<const char*, 6> matrices{
            "WORLD", "VIEW", "PROJECTION", "WORLDVIEW", "VIEWPROJECTION", "WORLDVIEWPROJECTION"};
        const auto matrix = std::find(matrices.begin(), matrices.end(), base);
        if (matrix != matrices.end()) {
            if (b.light || (!object.empty() && object != "CAMERA" && object != "GEOMETRY"))
                return fail("MME_legacy_light_matrix_not_supported");
            b.kind = K::Matrix;
            b.index = unsigned(matrix - matrices.begin());
        } else if (b.inverse || b.transpose)
            return fail("MME_legacy_parameter_semantic");
        else if (semantic == "POSITION" || semantic == "DIRECTION") {
            if (object != "CAMERA" && !b.light)
                return fail("MME_legacy_parameter_object");
            b.kind = semantic == "POSITION" ? K::Position : K::Direction;
        } else if (semantic == "DIFFUSE" || semantic == "AMBIENT" || semantic == "SPECULAR" ||
                   semantic == "EMISSIVE") {
            if (object != "GEOMETRY" && !b.light)
                return fail("MME_legacy_parameter_object");
            if (b.light && semantic == "EMISSIVE")
                return fail("MME_legacy_parameter_semantic");
            b.kind = K::Color;
            b.index = semantic == "DIFFUSE"    ? 0
                      : semantic == "AMBIENT"  ? 1
                      : semantic == "SPECULAR" ? 2
                                               : 3;
        } else if (semantic == "SPECULARPOWER" && object == "GEOMETRY")
            b.kind = K::Power;
        else if (semantic == "VIEWPORTPIXELSIZE" && object.empty())
            b.kind = K::Viewport;
        else if (semantic == "TIME" || semantic == "ELAPSEDTIME" || semantic == "TIME2" ||
                 semantic == "ELAPSEDTIME2") {
            if (!object.empty())
                return fail("MME_legacy_parameter_object");
            b.kind = K::Clock;
            b.index = semantic == "TIME"          ? 0
                      : semantic == "ELAPSEDTIME" ? 1
                      : semantic == "TIME2"       ? 2
                                                  : 3;
        } else
            return fail("MME_legacy_parameter_semantic");
    }
    if (b.kind != K::Value) {
        const auto type = b.kind == K::Matrix     ? "float4x4"
                          : b.kind == K::BoolFlag ? "bool"
                          : b.kind == K::Count    ? "int"
                          : b.kind == K::Position || b.kind == K::Direction || b.kind == K::Color ||
                                  b.kind == K::LightDirection
                              ? "float4"
                          : b.kind == K::Viewport ? "float2"
                                                  : "float";
        if (v.type != type || v.elements != 1)
            return fail("MME_legacy_semantic_shape");
    }
    out = b;
    reason.clear();
    return true;
}
void MmeAdvanceFrameClock(MmeFrameClock& s, std::uint32_t now, float frame, bool edit) {
    if (!s.initialized) {
        s.initialized = true;
        s.base = now;
        s.time = edit ? 0.f : frame;
        s.rawDelta = 0;
        s.delta = .033333335f;
    } else {
        if (!edit) {
            s.delta = s.edit ? float(now - s.tick) / 1000.f : frame - s.time;
            s.time = frame;
        } else {
            if (!s.edit)
                s.base = now;
            s.delta = float(now - s.tick) / 1000.f;
            s.time = float(now - s.base) / 1000.f;
        }
        s.rawDelta = frame - s.raw;
    }
    s.raw = frame;
    s.edit = edit;
    s.tick = now;
}
bool MmeStandardEvaluate(const MmeStandardBinding& b, const MmeStandardInputs& s,
                         const MmeStandardMath& math, std::vector<std::uint8_t>& bytes,
                         std::string& reason) {
    using K = MmeStandardBinding::Kind;
    const auto fail = [&](const char* why) {
        reason = why;
        return false;
    };
    const auto write = [&](const auto* data, unsigned count) {
        bytes.resize(count * 4);
        std::memcpy(bytes.data(), data, bytes.size());
        reason.clear();
        return true;
    };
    if (b.kind == K::Matrix) {
        if (!math.multiply || !math.inverse || b.index > 5)
            return fail("MME_standard_math_missing");
        auto m = b.index == 0   ? s.world
                 : b.index == 1 ? s.view
                 : b.index == 2 ? s.projection
                 : b.index == 3 ? math.multiply(s.world, s.view)
                 : b.index == 4 ? math.multiply(s.view, s.projection)
                                : math.multiply(s.world, math.multiply(s.view, s.projection));
        if (b.inverse) {
            MmeStandardInputs::Matrix inverse;
            if (!math.inverse(m, inverse))
                return fail("MME_standard_singular_matrix");
            m = inverse;
        }
        if (b.transpose)
            for (unsigned i = 0; i < 4; ++i)
                for (unsigned j = i + 1; j < 4; ++j)
                    std::swap(m[i * 4 + j], m[j * 4 + i]);
        for (float v : m)
            if (!std::isfinite(v))
                return fail("MME_standard_nonfinite");
        return write(m.data(), 16);
    }
    if (b.kind == K::Position || b.kind == K::Direction) {
        if (!math.inverse || !math.normalize)
            return fail("MME_standard_math_missing");
        MmeStandardInputs::Matrix inverse;
        if (!math.inverse(b.light ? s.beginWorld : s.view, inverse))
            return fail("MME_standard_singular_matrix");
        std::array<float, 3> v;
        if (b.light) {
            for (unsigned c = 0; c < 3; ++c)
                v[c] = s.lightDirection[0] * inverse[c] + s.lightDirection[1] * inverse[4 + c] +
                       s.lightDirection[2] * inverse[8 + c];
            if (b.kind == K::Position)
                for (auto& f : v)
                    f = -f;
        } else
            for (unsigned c = 0; c < 3; ++c)
                v[c] = inverse[(b.kind == K::Position ? 12 : 8) + c];
        if (b.kind == K::Direction)
            math.normalize(v);
        const std::array<float, 4> value{v[0], v[1], v[2],
                                         !b.light && b.kind == K::Position ? 1.f : 0.f};
        for (float f : value)
            if (!std::isfinite(f))
                return fail("MME_standard_nonfinite");
        return write(value.data(), 4);
    }
    if (b.kind == K::Color) {
        if (b.index >= (b.light ? 3u : 4u))
            return fail("MME_standard_binding");
        auto color = b.light ? s.lightColor[b.index] : s.material[b.index];
        if (b.light)
            color[3] = 1;
        return write(color.data(), 4);
    }
    if (b.kind == K::LightDirection) {
        const std::array<float, 4> v{s.lightDirection[0], s.lightDirection[1], s.lightDirection[2],
                                     0};
        return write(v.data(), 4);
    }
    if (b.kind == K::Power) {
        const float power = s.power == 0.f ? .1f : s.power;
        return write(&power, 1);
    }
    if (b.kind == K::Viewport)
        return write(s.viewport.data(), 2);
    if (b.kind == K::Clock) {
        if (!s.clockReady || b.index > 3)
            return fail("MME_standard_clock_unavailable");
        return write(&s.clock[b.index], 1);
    }
    if (b.kind == K::BoolFlag) {
        if (b.index >= s.flags.size())
            return fail("MME_standard_binding");
        const int flag = s.flags[b.index] ? 1 : 0;
        return write(&flag, 1);
    }
    if (b.kind == K::Count) {
        if (b.index > 1 || s.counts[b.index] < 0)
            return fail("MME_standard_count_unavailable");
        return write(&s.counts[b.index], 1);
    }
    return fail("MME_standard_binding");
}
