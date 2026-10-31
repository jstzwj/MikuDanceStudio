#include "mikudancestudio/scene_runtime.hpp"
#include <filesystem>
#include <fstream>
#include <iostream>
#include <cmath>
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
    CHECK(argc == 2);
    std::ifstream stream(std::filesystem::u8path(argv[1]), std::ios::binary);
    format::ModelDocument document;
    CHECK(stream && format::ReadModelDocument(Read, &stream, document));
    runtime::SceneRuntime a, b;
    CHECK(a.LoadModel(document, reason) && b.LoadModel(document, reason));
    CHECK(Equal(a, b));
    for (unsigned frame : {3u, 30u, 10u, 0u}) {
        CHECK(a.Seek(frame, reason) && b.Seek(frame, reason));
        CHECK(Equal(a, b));
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
        std::cout << "mode=" << mode << " 600-frame finite/exact-repeat trajectory passed\n"
                  << std::flush;
    }
    const auto retained = a.Resource(0);
    CHECK(a.UnloadModel(0, reason) && b.UnloadModel(0, reason));
    CHECK(!a.Snapshot(0) && !b.Snapshot(0) && retained);
    CHECK(a.Tick(.1f, reason) && b.Tick(.1f, reason));
    CHECK(a.LoadModel(document, reason) && b.LoadModel(document, reason) && Equal(a, b));
    std::cout
        << "real model: load, seeks, four physics modes, pause, reset, unload/reload and retained immutable resource passed\n";
}
