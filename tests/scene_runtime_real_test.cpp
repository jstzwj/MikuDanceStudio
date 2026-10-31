#include "mikudancestudio/scene_runtime.hpp"
#include "cpu_scene_session_fixture.hpp"
#include <filesystem>
#include <fstream>
#include <iostream>
#include <cmath>
#include <algorithm>
using namespace mikudancestudio;
std::string reason;
#define CHECK(x)                                                                                   \
    do {                                                                                           \
        if (!(x)) {                                                                                \
            std::cerr << "line " << __LINE__ << ": " << #x << " reason=" << reason << '\n';        \
            return 1;                                                                              \
        }                                                                                          \
    } while (0)
int Read(void* context, void* output, std::size_t size) {
    auto& stream = *static_cast<std::istream*>(context);
    stream.read(static_cast<char*>(output), size);
    return int(stream.gcount());
}
bool Equal(const runtime::SceneRuntime& a, const runtime::SceneRuntime& b) {
    const auto* first = a.Snapshot(0);
    const auto* second = b.Snapshot(0);
    if (!first || !second || first->vertices.size() != second->vertices.size() ||
        a.Bodies(0) != b.Bodies(0))
        return false;
    for (std::size_t n = 0; n < first->vertices.size(); ++n) {
        const auto& x = first->vertices[n];
        const auto& y = second->vertices[n];
        if (x.position != y.position || x.normal != y.normal || x.uv != y.uv)
            return false;
        for (float f : x.position)
            if (!std::isfinite(f))
                return false;
        for (float f : x.normal)
            if (!std::isfinite(f))
                return false;
    }
    return true;
}
int main(int argc, char** argv) {
    CHECK(argc == 2 || (argc == 3 && std::string(argv[2]) == "--session-only"));
    std::ifstream stream(std::filesystem::u8path(argv[1]), std::ios::binary);
    format::ModelDocument document;
    CHECK(stream && format::ReadModelDocument(Read, &stream, document));
    runtime::SceneRuntime a, b;
    CHECK(a.LoadModel(document, reason) && b.LoadModel(document, reason));
    CHECK(Equal(a, b));
    const auto& names = a.Resource(0)->BoneNames();
    auto target = std::find(names.begin(), names.end(), u8"\u30bb\u30f3\u30bf\u30fc");
    std::string encoded = "\x83\x5a\x83\x93\x83\x5e\x81\x5b";
    if (target == names.end()) {
        target = std::find_if(names.begin(), names.end(), [](const auto& name) {
            return name.size() <= 15 && std::all_of(name.begin(), name.end(), [](unsigned char c) {
                       return c > 0 && c < 128;
                   });
        });
        CHECK(target != names.end());
        encoded = *target;
    }
    const auto bone = std::size_t(target - names.begin());
    if (argc == 3) {
        CHECK(cpu_scene_fixture::RoundTrip(document, *a.Resource(0), bone, reason));
        std::cout
            << "real model: generated PMM animation, seek, playback, save/reload and reset passed\n";
        return 0;
    }
    format::VmdDocument motion;
    for (unsigned frame : {0u, 30u, 60u}) {
        format::VmdBoneKey key;
        std::copy(encoded.begin(), encoded.end(), key.name.begin());
        key.frame = frame;
        key.rotation[3] = 1;
        key.position[1] = frame == 30 ? 2.f : 0.f;
        motion.bones.push_back(key);
    }
    std::shared_ptr<const runtime::MotionClip> firstMotion, secondMotion;
    CHECK(runtime::MotionClip::Create(a.Resource(0), motion, firstMotion, reason));
    CHECK(runtime::MotionClip::Create(b.Resource(0), motion, secondMotion, reason));
    CHECK(firstMotion->Warnings().empty() && secondMotion->Warnings().empty());
    CHECK(a.BindMotion(0, firstMotion, reason) && b.BindMotion(0, secondMotion, reason));
    for (unsigned frame : {3u, 30u, 10u, 0u}) {
        CHECK(a.Seek(frame, reason) && b.Seek(frame, reason));
        CHECK(Equal(a, b));
        if (frame == 30)
            CHECK(a.Snapshot(0)->bones[bone].translation[1] == 2.f);
    }
    for (unsigned mode : {0u, 1u, 2u, 3u}) {
        CHECK(a.SetPhysicsMode(mode, reason) && b.SetPhysicsMode(mode, reason));
        CHECK(a.Reset(reason) && b.Reset(reason) && Equal(a, b));
        CHECK(a.StartPlayback(reason) && b.StartPlayback(reason));
        for (unsigned n = 1; n <= 600; ++n) {
            CHECK(a.Advance(float(n) / 30, 1.f / 30, reason) &&
                  b.Advance(float(n) / 30, 1.f / 30, reason));
            CHECK(Equal(a, b));
        }
        a.Pause();
        b.Pause();
        CHECK(a.Tick(1.f / 60, reason) && b.Tick(1.f / 60, reason) && Equal(a, b));
        CHECK(a.Reset(reason) && b.Reset(reason) && Equal(a, b));
        std::cout << "mode=" << mode
                  << " 600-frame animated finite/exact-repeat trajectory passed\n"
                  << std::flush;
    }
    const auto retained = a.Resource(0);
    CHECK(a.UnloadModel(0, reason) && b.UnloadModel(0, reason));
    CHECK(!a.Snapshot(0) && !b.Snapshot(0) && retained);
    CHECK(a.Tick(.1f, reason) && b.Tick(.1f, reason));
    CHECK(a.LoadModel(document, reason) && b.LoadModel(document, reason) && Equal(a, b));
    CHECK(cpu_scene_fixture::RoundTrip(document, *retained, bone, reason));
    std::cout
        << "real model: generated PMM animation, seek, playback, save/reload and reset passed\n";
    std::cout
        << "real model: load, seeks, four physics modes, pause, reset, unload/reload and retained immutable resource passed\n";
}
