#pragma once
#include "mme_legacy_api.h"
#include <array>
#include <cstdint>

// CPU-only standard-parameter providers. No host, native object or GPU handle.
struct MmeStandardBinding {
    enum class Kind {
        Value,
        Matrix,
        Position,
        Direction,
        Color,
        Power,
        Viewport,
        Clock,
        LightDirection,
        BoolFlag,
        Count
    } kind = Kind::Value;
    unsigned index = 0;
    bool inverse = false, transpose = false, light = false;
};
bool MmeStandardClassify(const MmeLegacyVariable&, MmeStandardBinding&, std::string& reason);
struct MmeFrameClock {
    bool initialized = false, edit = false;
    std::uint32_t tick = 0, base = 0;
    float time = 0, delta = 0, raw = 0, rawDelta = 0;
};
void MmeAdvanceFrameClock(MmeFrameClock&, std::uint32_t tick, float rawFrame, bool edit);
struct MmeStandardInputs {
    using Matrix = std::array<float, 16>;
    Matrix world{}, view{}, projection{}, beginWorld{};
    std::array<std::array<float, 4>, 4> material{};   // diffuse, ambient, specular, emissive
    std::array<std::array<float, 4>, 3> lightColor{}; // diffuse, ambient, specular
    std::array<float, 3> lightDirection{};
    std::array<float, 2> viewport{};
    std::array<float, 4> clock{}; // time, elapsed, time2, elapsed2
    bool clockReady = false;
    float power = 0;
    std::array<bool, 5> flags{}; // base, sphere, subtexture, toon, transparent
    std::array<int, 2> counts{-1, -1}; // vertices, subsets; negative means unavailable
};
struct MmeStandardMath {
    using Matrix = MmeStandardInputs::Matrix;
    Matrix (*multiply)(const Matrix&, const Matrix&) = nullptr;
    bool (*inverse)(const Matrix&, Matrix&) = nullptr;
    void (*normalize)(std::array<float, 3>&) = nullptr;
};
// Logical 32-bit components; numeric type/shape comes from classification.
bool MmeStandardEvaluate(const MmeStandardBinding&, const MmeStandardInputs&,
                         const MmeStandardMath&, std::vector<std::uint8_t>&, std::string& reason);
