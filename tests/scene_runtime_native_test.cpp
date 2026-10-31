#include "mikudancestudio/scene_runtime.hpp"
#include "mikudancestudio/runtime_cpu_scene.hpp"
#include "mikudancestudio/scene_resources.hpp"
#include "mikudancestudio/runtime_cpu_accessory.hpp"
#include <algorithm>
#include <fstream>
#include <iostream>
#include <cmath>
#include <cstring>
using namespace mikudancestudio;
using namespace format;
std::string reason;
#define CHECK(x)                                                                                   \
    do {                                                                                           \
        if (!(x)) {                                                                                \
            std::cerr << "line " << __LINE__ << ": " << #x << " reason=" << reason << '\n';        \
            return 1;                                                                              \
        }                                                                                          \
    } while (0)
int Read(void* p, void* out, std::size_t n) {
    auto& file = *static_cast<std::istream*>(p);
    file.read(static_cast<char*>(out), n);
    return int(file.gcount());
}
bool Equal(const float* a, const float* b, std::size_t count, const char* field) {
    for (std::size_t n = 0; n < count; ++n)
        if (!std::isfinite(a[n]) || !std::isfinite(b[n]) ||
            std::abs(a[n] - b[n]) > 2e-5f * std::max(1.f, std::abs(b[n]))) {
            std::cerr << field << " component=" << n << " portable=" << a[n] << " native=" << b[n]
                      << '\n';
            return false;
        }
    return true;
}
bool Compare(const runtime::SceneRuntime& portable, const RuntimeCpuScene& native) {
    for (auto slot : native.Order()) {
        CpuSolvedModel expected;
        if (!native.Snapshot(slot, expected))
            return false;
        const auto* actual = portable.Snapshot(slot);
        if (!actual || actual->bones.size() != expected.bones.size())
            return false;
        for (std::size_t n = 0; n < actual->bones.size(); ++n) {
            if (!Equal(actual->bones[n].skin.data(), expected.bones[n].skin.data(), 16, "skin") ||
                !Equal(actual->bones[n].local.data(), expected.bones[n].local.data(), 16,
                       "local")) {
                std::cerr << "slot=" << unsigned(slot) << " bone=" << n
                          << " native disabled=" << int(expected.bones[n].physicsDisabled)
                          << " native pos=" << expected.bones[n].pose[0] << ","
                          << expected.bones[n].pose[1] << " backup=" << expected.bones[n].backup[1]
                          << " working=" << expected.bones[n].working[1]
                          << " actual pose=" << actual->bones[n].translation[0] << ","
                          << actual->bones[n].translation[1] << '\n';
                return false;
            }
        }
        const auto* geometry = const_cast<RuntimeCpuScene&>(native).Model(slot)->Geometry();
        if (geometry) {
            if (actual->vertices.size() != geometry->vertexCount)
                return false;
            const auto stride = 32 + 16 * geometry->additionalUvCount;
            for (std::size_t n = 0; n < actual->vertices.size(); ++n) {
                render::ModelVertex vertex;
                std::memcpy(&vertex, geometry->main.bytes.data() + n * stride, sizeof vertex);
                if (!Equal(actual->vertices[n].position.data(), vertex.position, 3,
                           "vertex-position") ||
                    !Equal(actual->vertices[n].normal.data(), vertex.normal, 3, "vertex-normal") ||
                    !Equal(actual->vertices[n].uv.data(), vertex.uv, 2, "vertex-uv"))
                    return false;
            }
        }
        if (portable.Simulation().physicsMode == 0)
            continue;
        const auto bodies = portable.Bodies(slot);
        if (bodies.size() != expected.bodies.size())
            return false;
        for (std::size_t n = 0; n < bodies.size(); ++n)
            if (!Equal(bodies[n].data(), expected.bodies[n].data(), 38, "body")) {
                std::cerr << "slot=" << unsigned(slot) << " body=" << n << '\n';
                return false;
            }
    }
    if (portable.Session())
        for (const auto& accessory : portable.Session()->Pose().accessories) {
            cpu_math::Matrix actual;
            render::ModelMatrix expected;
            std::string why;
            if (!portable.AccessoryPlacement(accessory.slot, actual, why) ||
                !native.AccessoryPlacement(accessory.slot, expected, why) ||
                !Equal(actual.data(), expected.data(), 16, "accessory-placement"))
                return false;
        }
    return true;
}
bool MeasureDynamic(runtime::SceneRuntime& portable, RuntimeCpuScene& native, float& position,
                    float& normal) {
    const auto* a = portable.Snapshot(0);
    const auto* g = native.Model(0)->Geometry();
    if (!a || !g || a->vertices.size() != g->vertexCount)
        return false;
    for (std::size_t n = 0; n < a->vertices.size(); ++n) {
        render::ModelVertex v;
        std::memcpy(&v, g->main.bytes.data() + n * (32 + 16 * g->additionalUvCount), sizeof v);
        for (int c = 0; c < 3; ++c) {
            if (!std::isfinite(a->vertices[n].position[c]) ||
                !std::isfinite(a->vertices[n].normal[c]))
                return false;
            position = std::max(position, std::abs(a->vertices[n].position[c] - v.position[c]));
            normal = std::max(normal, std::abs(a->vertices[n].normal[c] - v.normal[c]));
        }
    }
    return true;
}
int main(int argc, char** argv) {
    if (argc == 3 && std::string(argv[1]) == "--model") {
        const auto path = std::filesystem::absolute(argv[2]);
        std::ifstream file(path, std::ios::binary);
        ModelDocument document;
        CHECK(ReadModelDocument(Read, &file, document));
        runtime::SceneRuntime portable;
        RuntimeCpuScene native;
        CHECK(portable.LoadModel(document, reason));
        CHECK(native.LoadModel(path, reason));
        CHECK(portable.SetPhysicsMode(0, reason) && native.SetPhysicsMode(0, reason));
        CHECK(Compare(portable, native));
        for (unsigned frame : {0u, 1u, 10u, 0u}) {
            CHECK(portable.Seek(frame, reason) && native.Seek(frame, reason));
            CHECK(Compare(portable, native));
        }
        CHECK(portable.StartPlayback(reason) && native.StartPlayback(reason));
        for (unsigned n = 1; n <= 30; ++n) {
            const float t = float(n) / 30;
            CHECK(portable.Advance(t, 1.f / 30, reason) && native.Advance(t, 1.f / 30, reason));
            CHECK(Compare(portable, native));
        }
        float dynamicPosition = 0, dynamicNormal = 0;
        CHECK(portable.SetPhysicsMode(2, reason) && native.SetPhysicsMode(2, reason));
        CHECK(portable.Reset(reason) && native.Seek(0, reason));
        CHECK(MeasureDynamic(portable, native, dynamicPosition, dynamicNormal));
        CHECK(portable.StartPlayback(reason) && native.StartPlayback(reason));
        for (unsigned n = 1; n <= 30; ++n) {
            const float t = float(n) / 30;
            CHECK(portable.Advance(t, 1.f / 30, reason) && native.Advance(t, 1.f / 30, reason));
            CHECK(MeasureDynamic(portable, native, dynamicPosition, dynamicNormal));
        }
        std::cout << "dynamic max vertex position=" << dynamicPosition
                  << " normal=" << dynamicNormal << '\n';
        std::cout
            << "real model: strict kinematic comparison passed; dynamic comparison is measurement only\n";
        return 0;
    }
    CHECK(argc == 2);
    const auto root = std::filesystem::absolute(argv[1]);
    unsigned checkpoints = 0;
    for (const char* name :
         {"bound.x", "bound-varied.x", "bound-tzip.x", "bound-tzip-multi.x", "triangle-bzip.x"}) {
        std::ifstream file(root / name, std::ios::binary);
        std::string bytes{std::istreambuf_iterator<char>(file), {}};
        std::shared_ptr<const runtime::AccessoryResource> resource;
        RuntimeCpuAccessory native;
        CHECK(runtime::AccessoryResource::Parse(bytes.data(), bytes.size(), resource, reason));
        CHECK(native.Load(root / name, 1, reason));
        const auto* geometry = native.Geometry();
        CHECK(geometry && geometry->vertexCount == resource->Vertices().size());
        for (std::size_t n = 0; n < resource->Vertices().size(); ++n) {
            render::ModelVertex expected;
            std::memcpy(&expected, geometry->main.bytes.data() + n * sizeof(expected),
                        sizeof expected);
            CHECK(Equal(resource->Vertices()[n].position.data(), expected.position, 3,
                        "accessory-position"));
            CHECK(Equal(resource->Vertices()[n].normal.data(), expected.normal, 3,
                        "accessory-normal"));
            CHECK(Equal(resource->Vertices()[n].uv.data(), expected.uv, 2, "accessory-uv"));
        }
    }
    for (const char* name :
         {"v1.pmm", "v2.pmm", "pmx.pmm", "ik-v1.pmm", "physics-v2.pmm", "sdef-physics-v2.pmm",
          "bound-v1.pmm", "bound-v2.pmm", "passes-v2.pmm"}) {
        std::cout << name << std::endl;
        const auto path = root / name;
        SceneResourceResolver resolver(path);
        SceneSession session;
        std::ifstream file(path, std::ios::binary);
        CHECK(SceneSession::Load(Read, &file, SceneResourceResolver::Resolve, &resolver, session));
        std::map<std::uint8_t, ModelDocument> documents;
        for (std::size_t n = 0; n < session.Models().size(); ++n)
            documents.emplace(session.Document().models[n].slot, *session.Models()[n]);
        for (unsigned mode = 0;
             mode < (session.Document().envelope.header.version == PmmVersion::V1 ? 1u : 4u);
             ++mode) {
            auto source = session;
            if (source.Document().envelope.header.version == PmmVersion::V2)
                CHECK(
                    source.Edit([&](auto& doc) { doc.physics.playbackMode = std::uint8_t(mode); }));
            runtime::SceneRuntime portable;
            RuntimeCpuScene native;
            CHECK(portable.Load(source, documents, reason));
            CHECK(native.Load(source, resolver.Resolved(), reason));
            CHECK(Compare(portable, native));
            ++checkpoints;
            for (unsigned frame : {3u, 10u, 15u, 0u}) {
                std::cout << " mode=" << mode << " seek=" << frame << std::endl;
                CHECK(portable.Seek(frame, reason) && native.Seek(frame, reason));
                CHECK(Compare(portable, native));
                ++checkpoints;
            }
            CHECK(portable.StartPlayback(reason) && native.StartPlayback(reason));
            float previous = 0;
            for (float target : {.03f, .09f, .21f, .33333334f, .4f, .5f}) {
                std::cout << " target=" << target << std::endl;
                CHECK(portable.Advance(target, target - previous, reason) &&
                      native.Advance(target, target - previous, reason));
                CHECK(Compare(portable, native));
                previous = target;
                ++checkpoints;
            }
            portable.Pause();
            native.Pause();
            CHECK(portable.Tick(.02f, reason) && native.Tick(.02f, reason));
            CHECK(Compare(portable, native));
            ++checkpoints;
        }
    }
    std::cout << "portable/native complete CPU scene checkpoints=" << checkpoints
              << " tolerance=2e-5 relative\n";
}
