#include "../third_party/mmeffect/include/mme_standard_api.h"
#include <cmath>
#include <cstdio>
#include <cstring>
#include <stdexcept>
#define CHECK(x)                                                                                   \
    do {                                                                                           \
        if (!(x))                                                                                  \
            throw std::runtime_error(std::to_string(__LINE__) + ": " #x);                          \
    } while (false)
int main() try {
    MmeFrameClock clock;
    MmeAdvanceFrameClock(clock, 1000, 12, true);
    CHECK(clock.time == 0 && clock.raw == 12 && clock.delta == .033333335f && clock.rawDelta == 0);
    MmeAdvanceFrameClock(clock, 1250, 12, true);
    CHECK(clock.time == .25f && clock.delta == .25f && clock.rawDelta == 0);
    MmeAdvanceFrameClock(clock, 1500, 2, false);
    CHECK(clock.time == 2 && clock.delta == .25f && clock.rawDelta == -10);
    MmeAdvanceFrameClock(clock, 1750, 2.5f, false);
    CHECK(clock.time == 2.5f && clock.delta == .5f);
    MmeAdvanceFrameClock(clock, 2000, 2.5f, false); // paused playback
    CHECK(clock.delta == 0 && clock.rawDelta == 0);
    MmeAdvanceFrameClock(clock, 2250, 1, false); // backwards seek
    CHECK(clock.delta == -1.5f && clock.rawDelta == -1.5f);
    MmeAdvanceFrameClock(clock, 2500, 1, true); // entering edit restarts edit epoch
    CHECK(clock.time == 0 && clock.delta == .25f);
    clock.tick = clock.base = 0xfffffff0u;
    MmeAdvanceFrameClock(clock, 0x10u, 1, true);
    CHECK(clock.delta == .032f && clock.time == .032f);
    std::string reason;
    MmeStandardBinding b;
    const auto classify = [&](const char* name, const char* type, const char* semantic,
                              const char* object = "") {
        MmeLegacyVariable v;
        v.name = name;
        v.type = type;
        v.semantic = semantic;
        v.object = object;
        return MmeStandardClassify(v, b, reason);
    };
    CHECK(classify("Anything", "float4x4", "worldviewprojectioninversetranspose"));
    CHECK(b.kind == MmeStandardBinding::Kind::Matrix && b.index == 5 && b.inverse && b.transpose);
    const char* families[] = {"WORLD",     "VIEW",           "PROJECTION",
                              "WORLDVIEW", "VIEWPROJECTION", "WORLDVIEWPROJECTION"};
    const char* suffixes[] = {"", "INVERSE", "TRANSPOSE", "INVERSETRANSPOSE"};
    for (unsigned family = 0; family < 6; ++family)
        for (unsigned variant = 0; variant < 4; ++variant) {
            const auto semantic = std::string(families[family]) + suffixes[variant];
            CHECK(classify("matrix", "float4x4", semantic.c_str()));
            CHECK(b.index == family && b.inverse == bool(variant & 1) &&
                  b.transpose == bool(variant & 2));
        }
    CHECK(classify("MaTwOrLd", "float4x4", ""));
    CHECK(b.kind == MmeStandardBinding::Kind::Matrix && b.index == 0);
    CHECK(classify("matWorld", "float4", "POSITION", "Camera")); // semantic takes precedence
    CHECK(b.kind == MmeStandardBinding::Kind::Position);
    CHECK(!classify("x", "float4x4", "VIEW", "Light"));
    CHECK(!classify("x", "int", "TIME"));
    CHECK(!classify("DifColor", "float4", ""));
    CHECK(classify("x", "float4", "DIFFUSE", "Light0"));
    MmeStandardInputs values;
    values.lightColor[0] = {.25f, .5f, .75f, 0};
    MmeStandardMath math;
    std::vector<std::uint8_t> bytes;
    CHECK(MmeStandardEvaluate(b, values, math, bytes, reason));
    float rgba[4];
    std::memcpy(rgba, bytes.data(), 16);
    CHECK(rgba[0] == .25f && rgba[3] == 1);
    CHECK(classify("x", "float", "TIME2"));
    CHECK(!MmeStandardEvaluate(b, values, math, bytes, reason));
    CHECK(reason == "MME_standard_clock_unavailable");
    values.clockReady = true;
    values.clock[2] = 3.25f;
    CHECK(MmeStandardEvaluate(b, values, math, bytes, reason));
    float time;
    std::memcpy(&time, bytes.data(), 4);
    CHECK(time == 3.25f);
    const auto previous = bytes;
    CHECK(classify("matrix", "float4x4", "VIEWINVERSE"));
    CHECK(!MmeStandardEvaluate(b, values, math, bytes, reason));
    CHECK(reason == "MME_standard_math_missing" && bytes == previous);
    math.multiply = [](const auto& a, const auto&) { return a; };
    math.inverse = [](const auto&, auto&) { return false; };
    CHECK(!MmeStandardEvaluate(b, values, math, bytes, reason));
    CHECK(reason == "MME_standard_singular_matrix" && bytes == previous);
    CHECK(classify("x", "float4", "POSITION", "Camera"));
    math.normalize = [](auto&) {};
    CHECK(!MmeStandardEvaluate(b, values, math, bytes, reason));
    CHECK(reason == "MME_standard_singular_matrix" && bytes == previous);
    CHECK(classify("VertexCount", "int", ""));
    values.counts[0] = -1;
    CHECK(!MmeStandardEvaluate(b, values, math, bytes, reason));
    CHECK(reason == "MME_standard_count_unavailable" && bytes == previous);
    CHECK(classify("use_subtexture", "bool", ""));
    values.flags[2] = true;
    CHECK(MmeStandardEvaluate(b, values, math, bytes, reason));
    int flag = 0;
    std::memcpy(&flag, bytes.data(), 4);
    CHECK(flag == 1);
    std::puts(
        "PASS standard bindings, edit/play/pause/seek clock, rollover and unavailable providers");
} catch (const std::exception& e) {
    std::fprintf(stderr, "%s\n", e.what());
    return 1;
}
