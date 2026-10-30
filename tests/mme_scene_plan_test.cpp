#include "../third_party/mmeffect/include/mme_portable_api.h"
#include <cstdio>
#include <cstring>
#include <stdexcept>
#include <limits>
#define CHECK(x)                                                                                   \
    do {                                                                                           \
        if (!(x))                                                                                  \
            throw std::runtime_error(std::to_string(__LINE__) + ": " #x);                          \
    } while (false)
using K = MmePortableCommand::Kind;
MmePortableEffect Fixture() {
    MmePortableEffect e;
    e.post = true;
    e.profile = "scene-v2";
    e.targets = {{"SceneColor"}, {"Ping"}, {"Depth", MmePortableFormat::Rgba8, true}};
    e.passes = {{"Post", "Transform", "vs", "ps", {"SceneColor"}, {}},
                {"Post", "Copy", "vs", "ps", {"Ping"}, {}}};
    for (const auto* name : {"count", "index"}) {
        MmePortableParameter p;
        p.name = name;
        p.type = MmePortableParameter::Type::Int;
        p.value.resize(4);
        int n = 2;
        std::memcpy(p.value.data(), &n, 4);
        e.parameters.push_back(std::move(p));
    }
    MmePortableParameter color;
    color.name = "clear";
    color.columns = 4;
    color.value.resize(16);
    const float rgba[] = {.5f, .25f, 2, -1};
    std::memcpy(color.value.data(), rgba, 16);
    e.parameters.push_back(color);
    e.script = {{K::ColorTarget, "SceneColor", 0},
                {K::DepthTarget, "Depth"},
                {K::ClearColorValue, "clear"},
                {K::Clear, "", 1},
                {K::Clear, "", 6},
                {K::Capture},
                {K::ColorTarget, "Ping", 0},
                {K::Clear, "", 1},
                {K::Loop, "count"},
                {K::LoopIndex, "index"},
                {K::Pass, "", 0},
                {K::LoopEnd},
                {K::ColorTarget, "", 0},
                {K::Clear, "", 1},
                {K::Pass, "", 1}};
    return e;
}
int main() try {
    auto e = Fixture();
    std::string reason;
    CHECK(MmePortableSceneLayout(e, reason));
    CHECK(e.steps.size() == 8 && e.steps[2].kind == MmePortableStep::Kind::Capture);
    CHECK(e.steps[0].color[0] == 127.f / 255 && e.steps[0].color[2] == 1 &&
          e.steps[0].color[3] == 0);
    for (unsigned n = 0; n < 2; ++n) {
        int index = -1;
        std::memcpy(&index, e.steps[4 + n].parameters[1].value.data(), 4);
        CHECK(index == int(n));
    }
    int original = 0;
    std::memcpy(&original, e.parameters[1].value.data(), 4);
    CHECK(original == 2);
    CHECK(e.writes.size() == 1 && e.writes[0].name == "index");
    std::memcpy(&original, e.writes[0].value.data(), 4);
    CHECK(original == 1);
    const auto rejects = [&](MmePortableEffect bad, const char* expected) {
        CHECK(!MmePortableSceneLayout(bad, reason));
        if (reason != expected)
            std::fprintf(stderr, "expected %s, got %s\n", expected, reason.c_str());
        CHECK(reason == expected);
        CHECK(bad.steps.empty());
    };
    auto bad = Fixture();
    bad.script.insert(bad.script.begin() + 6, {K::Capture});
    rejects(bad, "MME_scene_capture_layout");
    bad = Fixture();
    bad.script.erase(bad.script.begin() + 3);
    rejects(bad, "MME_scene_capture_requires_clear");
    bad = Fixture();
    bad.targets[0].format = MmePortableFormat::Rgba16Float;
    CHECK(MmePortableSceneLayout(bad, reason));
    bad = Fixture();
    bad.passes[0].sampled = {"Ping"};
    rejects(bad, "MME_scene_feedback");
    bad = Fixture();
    bad.passes[0].sampled = {"missing"};
    rejects(bad, "MME_scene_read_before_write");
    auto external = Fixture();
    external.targets.push_back({"External"});
    external.targets.back().identity = 42;
    external.passes[0].sampled = {"External"};
    CHECK(MmePortableSceneLayout(external, reason)); // Producer is checked in the assembled frame.
    bad = Fixture();
    bad.targets.push_back({"External"});
    bad.passes[0].sampled = {"External"};
    rejects(bad, "MME_scene_read_before_write");
    bad = Fixture();
    bad.script.erase(bad.script.begin() + 7);
    rejects(bad, "MME_scene_write_requires_clear");
    bad = Fixture();
    bad.parameters[1].shared = true;
    rejects(bad, "MME_scene_loop_index_parameter");
    bad = Fixture();
    bad.script[11].kind = K::DepthTarget;
    rejects(bad, "MME_scene_unbalanced_loop");
    bad = Fixture();
    int huge = 4097;
    std::memcpy(bad.parameters[0].value.data(), &huge, 4);
    rejects(bad, "MME_scene_loop_parameter");
    bad = Fixture();
    bad.script.insert(bad.script.begin() + 3, {K::ColorTarget, "SceneColor", 1});
    rejects(bad, "MME_scene_duplicate_attachment");
    bad = Fixture();
    bad.script.insert(bad.script.begin() + 3, {K::ColorTarget, "Ping", 2});
    rejects(bad, "MME_scene_color_layout");
    bad = Fixture();
    bad.targets.push_back({"SceneColor"});
    rejects(bad, "MME_scene_duplicate_target");
    auto empty = Fixture();
    int zero = 0;
    std::memcpy(empty.parameters[0].value.data(), &zero, 4);
    CHECK(MmePortableSceneLayout(empty, reason));
    CHECK(empty.steps.size() == 6);
    CHECK(empty.writes.empty());
    auto nested = Fixture();
    nested.script.insert(nested.script.begin() + 9, {K::Loop, "count"});
    nested.script.insert(nested.script.begin() + 12, {K::LoopEnd});
    CHECK(MmePortableSceneLayout(nested, reason));
    CHECK(nested.steps.size() == 10);
    for (unsigned n = 0; n < 4; ++n) {
        int index = -1;
        std::memcpy(&index, nested.steps[4 + n].parameters[1].value.data(), 4);
        CHECK(index == int(n % 2));
    }
    bad = Fixture();
    int many = 64;
    std::memcpy(bad.parameters[0].value.data(), &many, 4);
    rejects(bad, "MME_scene_step_budget");
    bad = Fixture();
    many = 4096;
    std::memcpy(bad.parameters[0].value.data(), &many, 4);
    bad.script.erase(bad.script.begin() + 10); // Empty draws still consume CPU commands.
    rejects(bad, "MME_scene_script_budget");
    MmePortableTarget relative{"Half"};
    relative.relative = true;
    relative.viewportRatio = {.5f, .25f};
    unsigned width, height;
    CHECK(MmePortableExtent(relative, 55, 37, width, height, reason) && width == 27 && height == 9);
    CHECK(MmePortableExtent(relative, 3, 4, width, height, reason) && width == 1 && height == 1);
    CHECK(!MmePortableExtent(relative, 1, 1, width, height, reason));
    relative.viewportRatio[0] = std::numeric_limits<float>::quiet_NaN();
    CHECK(!MmePortableExtent(relative, 55, 37, width, height, reason));
    relative.viewportRatio[0] = -1;
    CHECK(!MmePortableExtent(relative, 55, 37, width, height, reason));
    relative.viewportRatio[0] = 1;
    relative.width = 64;
    relative.height = 32;
    CHECK(!MmePortableExtent(relative, 55, 37, width, height, reason));
    std::puts(
        "PASS pure SAS scene lowering: capture/clear, loops/snapshots, dependency rejection and budgets");
} catch (const std::exception& e) {
    std::fprintf(stderr, "%s\n", e.what());
    return 1;
}
