#include "mikudancestudio/scene_runtime.hpp"
#include "mikudancestudio/scene_resources.hpp"
#include "cpu_scene_session_fixture.hpp"
#include <fstream>
#include <iostream>
#include <sstream>
#include <cmath>
#include <limits>
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
int Read(void* p, void* data, std::size_t n) {
    auto& file = *static_cast<std::istream*>(p);
    file.read(static_cast<char*>(data), n);
    return int(file.gcount());
}
bool Write(void* p, const void* data, std::size_t n) {
    auto& file = *static_cast<std::ostream*>(p);
    file.write(static_cast<const char*>(data), n);
    return bool(file);
}
int Run(int argc, char** argv) {
    CHECK(argc == 2);
    const auto root = std::filesystem::absolute(argv[1]);
    unsigned checkpoints = 0;
    for (const char* name :
         {"v1.pmm", "v2.pmm", "pmx.pmm", "ik-v1.pmm", "physics-v2.pmm", "sdef-physics-v2.pmm",
          "bound-v1.pmm", "bound-v2.pmm", "passes-v2.pmm"}) {
        std::cout << "scene=" << name << std::endl;
        const auto path = root / name;
        SceneResourceResolver resolver(path);
        SceneSession session;
        std::ifstream input(path, std::ios::binary);
        CHECK(SceneSession::Load(Read, &input, SceneResourceResolver::Resolve, &resolver, session));
        std::map<std::uint8_t, ModelDocument> models;
        for (std::size_t n = 0; n < session.Models().size(); ++n) {
            CHECK(session.Models()[n]);
            models.emplace(session.Document().models[n].slot, *session.Models()[n]);
        }
        runtime::SceneRuntime scene;
        std::cout << "load " << std::flush;
        CHECK(scene.Load(session, models, reason));
        if (std::string(name) == "pmx.pmm") {
            CHECK(cpu_scene_fixture::RoundTrip(models.begin()->second,
                                               *scene.Resource(models.begin()->first), 0, reason));
        }
        std::cout << "loaded\n" << std::flush;
        for (const auto& accessory : session.Document().accessories) {
            std::ifstream source(root / "bound.x", std::ios::binary);
            std::string data{std::istreambuf_iterator<char>(source), {}};
            std::shared_ptr<const runtime::AccessoryResource> resource;
            CHECK(runtime::AccessoryResource::Parse(data.data(), data.size(), resource, reason));
            const auto saved = resource;
            CHECK(scene.AttachAccessory(accessory.slot, resource, reason));
            resource.reset();
            CHECK(scene.Accessory(accessory.slot) == saved);
            cpu_math::Matrix world;
            CHECK(scene.AccessoryPlacement(accessory.slot, world, reason));
            if (accessory.state.parentModel >= 0)
                CHECK(!scene.UnloadModel(std::uint8_t(accessory.state.parentModel), reason) &&
                      reason == "model-still-referenced");
            CHECK(scene.UnloadAccessory(accessory.slot, reason) &&
                  !scene.Accessory(accessory.slot));
            CHECK(scene.AttachAccessory(accessory.slot, saved, reason));
        }
        for (unsigned frame : {0u, 3u, 10u, 15u, 0u}) {
            std::cout << "seek=" << frame << std::endl;
            const auto count = scene.Counters().frames;
            CHECK(scene.Seek(frame, reason));
            CHECK(scene.Counters().frames == count + 1);
            for (const auto& pair : models) {
                const auto* pose = scene.Snapshot(pair.first);
                CHECK(pose && pose->frame == frame);
                for (const auto& vertex : pose->vertices)
                    for (float f : vertex.position)
                        CHECK(std::isfinite(f));
                for (std::size_t n = 0; n < pose->bones.size(); ++n) {
                    cpu_math::Matrix matrix;
                    CHECK(scene.BoneAttachment(pair.first, int(n), matrix, reason));
                }
            }
            for (const auto& accessory : session.Document().accessories) {
                cpu_math::Matrix matrix;
                CHECK(scene.AccessoryPlacement(accessory.slot, matrix, reason));
            }
            ++checkpoints;
        }
        std::cout << "start\n" << std::flush;
        CHECK(scene.StartPlayback(reason));
        float previous = 0;
        for (float target : {.03f, .09f, .21f, .33333334f, .4f, .5f}) {
            CHECK(scene.Advance(target, target - previous, reason));
            previous = target;
            ++checkpoints;
        }
        std::cout << "pause\n" << std::flush;
        scene.Pause();
        CHECK(scene.Tick(.02f, reason));
        CHECK(!scene.Advance(.6f, .1f, reason));
        const auto before = scene.Counters().frames;
        CHECK(!scene.Load(session, {}, reason));
        CHECK(scene.Counters().frames == before);
        CHECK(!scene.Seek(0xffffffff, reason));
        CHECK(scene.Reset(reason));
        std::ostringstream bytes(std::ios::binary);
        CHECK(scene.Session()->Save(Write, &bytes));
        std::istringstream saved(bytes.str(), std::ios::binary);
        SceneSession reloaded;
        CHECK(
            SceneSession::Load(Read, &saved, SceneResourceResolver::Resolve, &resolver, reloaded));
        runtime::SceneRuntime second;
        CHECK(second.Load(reloaded, models, reason));
        CHECK(second.Seek(3, reason));
        CHECK(scene.Seek(3, reason));
        for (const auto& pair : models) {
            const auto* a = scene.Snapshot(pair.first);
            const auto* b = second.Snapshot(pair.first);
            CHECK(a->vertices.size() == b->vertices.size());
            for (std::size_t n = 0; n < a->vertices.size(); ++n)
                for (int c = 0; c < 3; ++c)
                    CHECK(std::abs(a->vertices[n].position[c] - b->vertices[n].position[c]) <
                          1e-4f);
        }
    }
    // Physics trajectories must repeat exactly within one implementation.
    {
        const auto path = root / "sdef-physics-v2.pmm";
        SceneResourceResolver resolver(path);
        SceneSession session;
        std::ifstream file(path, std::ios::binary);
        CHECK(SceneSession::Load(Read, &file, SceneResourceResolver::Resolve, &resolver, session));
        std::map<std::uint8_t, ModelDocument> models;
        for (std::size_t n = 0; n < session.Models().size(); ++n)
            models.emplace(session.Document().models[n].slot, *session.Models()[n]);
        runtime::SceneRuntime a, b;
        CHECK(a.Load(session, models, reason) && b.Load(session, models, reason));
        CHECK(a.StartPlayback(reason) && b.StartPlayback(reason));
        for (unsigned n = 1; n <= 600; ++n) {
            const float t = float(n) / 30;
            CHECK(a.Advance(t, 1.f / 30, reason) && b.Advance(t, 1.f / 30, reason));
            CHECK(a.Session()->Pose().frame == n);
            for (const auto& pair : models) {
                CHECK(a.Snapshot(pair.first)->frame == n);
                CHECK(a.Bodies(pair.first) == b.Bodies(pair.first));
                const auto& av = a.Snapshot(pair.first)->vertices;
                const auto& bv = b.Snapshot(pair.first)->vertices;
                CHECK(av.size() == bv.size());
                for (std::size_t i = 0; i < av.size(); ++i) {
                    CHECK(av[i].position == bv[i].position);
                    for (float f : av[i].position)
                        CHECK(std::isfinite(f));
                }
            }
        }
        const auto count = a.Counters().frames;
        CHECK(!a.Advance(100000.f, .01f, reason) && a.Counters().frames == count);
    }
    // Global VMD imports reuse the PMM seek/playback evaluators and preserve
    // the existing PMM serialization guard when key topology changes.
    {
        const auto path = root / "v2.pmm";
        SceneResourceResolver resolver(path);
        SceneSession session;
        std::ifstream file(path, std::ios::binary);
        CHECK(SceneSession::Load(Read, &file, SceneResourceResolver::Resolve, &resolver, session));
        std::map<std::uint8_t, ModelDocument> models;
        for (std::size_t n = 0; n < session.Models().size(); ++n)
            models.emplace(session.Document().models[n].slot, *session.Models()[n]);
        runtime::SceneRuntime scene;
        CHECK(scene.Load(session, models, reason));
        VmdDocument motion;
        VmdCameraKey camera;
        camera.frame = 10;
        camera.distance = -40;
        camera.eye[1] = 12;
        camera.target[0] = .2f;
        camera.fov = 37;
        for (auto& curve : camera.interpolation) {
            curve[0] = curve[2] = 20;
            curve[1] = curve[3] = 107;
        }
        motion.cameras.push_back(camera);
        VmdLightKey light;
        light.frame = 10;
        light.color[0] = .4f;
        light.color[1] = .6f;
        light.color[2] = .8f;
        light.direction[1] = -1;
        motion.lights.push_back(light);
        motion.shadows.push_back({10, 2, .005f});
        CHECK(scene.BindGlobalMotion(motion, reason) && scene.Seek(10, reason));
        const auto& pose = scene.Session()->Pose();
        CHECK(pose.camera.distance == -40 && pose.camera.fov == 37 &&
              pose.camera.position[1] == 12);
        CHECK(pose.lightState[0] == .4f && pose.selfShadowMode == 2 &&
              pose.selfShadowDistance == .005f);
        const auto count = scene.Counters().frames;
        motion.cameras[0].distance = std::numeric_limits<float>::infinity();
        CHECK(!scene.BindGlobalMotion(motion, reason) && scene.Counters().frames == count &&
              scene.Session()->Pose().camera.distance == -40);
        std::ostringstream bytes;
        CHECK(!scene.Session()->Save(Write, &bytes));
        CHECK(scene.StartPlayback(reason) && scene.Advance(.5f, 1.f / 6, reason));
    }
    runtime::SceneRuntime single;
    ModelDocument boneOnly;
    std::ifstream file(root / "bone-only.pmd", std::ios::binary);
    CHECK(ReadModelDocument(Read, &file, boneOnly) && single.LoadModel(boneOnly, reason));
    CHECK(single.Snapshot(0)->bones.size() == 1);
    VmdDocument motion;
    VmdBoneKey key;
    key.name[0] = 'B';
    key.frame = 10;
    key.rotation[3] = 1;
    key.position[0] = 4;
    motion.bones.push_back(key);
    std::shared_ptr<const runtime::MotionClip> clip;
    CHECK(runtime::MotionClip::Create(single.Resource(0), motion, clip, reason));
    CHECK(single.BindMotion(0, clip, reason) && single.Seek(10, reason));
    CHECK(single.Snapshot(0)->bones[0].translation[0] == 4);
    CHECK(single.SetPhysicsMode(0, reason) && !single.SetPhysicsMode(4, reason));
    const auto retained = single.Resource(0);
    CHECK(single.UnloadModel(0, reason));
    CHECK(!single.Snapshot(0));
    CHECK(single.Tick(.02f, reason));
    std::cout << "portable scene checkpoints=" << checkpoints
              << "; playback, seek, pause, reset, PMM roundtrip, unload passed\n";
    return 0;
}

int main(int argc, char** argv) {
    try {
        return Run(argc, argv);
    } catch (const std::exception& e) {
        std::cerr << "exception: " << e.what() << std::endl;
        return 2;
    }
}
