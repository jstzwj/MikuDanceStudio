#include "mikudancestudio/scene_runtime.hpp"
#include "mikudancestudio/scene_resources.hpp"
#include "runtime_cli_output.hpp"
#include <charconv>
#include <iostream>
#include <limits>

namespace {
using namespace mikudancestudio;
using namespace mikudancestudio::cli;
int Run(const std::vector<std::string>& args) {
    std::string input, motionPath, output, save, reason;
    unsigned mode = 2, playFrames = 0;
    std::vector<unsigned> frames;
    bool explicitMode = false;
    const auto fail = [&](const std::string& message) {
        std::cerr << "{\"status\":\"failed\",\"reason\":" << Quote(message) << "}\n";
        return 1;
    };
    for (std::size_t n = 1; n < args.size(); ++n) {
        const auto& arg = args[n];
        if (n + 1 == args.size())
            return fail("missing-option-value:" + arg);
        const auto& value = args[++n];
        if (arg == "--input")
            input = value;
        else if (arg == "--motion")
            motionPath = value;
        else if (arg == "--output")
            output = value;
        else if (arg == "--save")
            save = value;
        else if (arg == "--frame" || arg == "--physics-mode" || arg == "--play-frames") {
            unsigned number;
            const auto parsed = std::from_chars(value.data(), value.data() + value.size(), number);
            if (parsed.ec != std::errc{} || parsed.ptr != value.data() + value.size())
                return fail("invalid-number:" + arg);
            if (arg == "--frame")
                frames.push_back(number);
            else if (arg == "--play-frames")
                playFrames = number;
            else {
                mode = number;
                explicitMode = true;
            }
        } else
            return fail("unknown-option:" + arg);
    }
    if (input.empty() || playFrames > 100000 || mode > 3)
        return fail(
            "usage:mds_cpu_scene_cli --input scene.pmm|model.pmd|model.pmx [--motion path.vmd] [--frame N] [--play-frames N] [--physics-mode 0..3] [--save path.pmm] [--output path.json]");
    const auto path = std::filesystem::absolute(std::filesystem::u8path(input));
    runtime::SceneRuntime scene;
    std::vector<std::uint8_t> slots;
    if (path.extension() == ".pmm" || path.extension() == ".PMM") {
        format::SceneResourceResolver resolver(path);
        format::SceneSession session;
        std::ifstream file(path, std::ios::binary);
        if (!file || !format::SceneSession::Load(
                         Read, &file, format::SceneResourceResolver::Resolve, &resolver, session))
            return fail("scene-read-or-resolve-failed");
        std::map<std::uint8_t, format::ModelDocument> models;
        for (std::size_t n = 0; n < session.Models().size(); ++n) {
            if (!session.Models()[n])
                return fail("scene-model-not-resolved");
            const auto slot = session.Document().models[n].slot;
            slots.push_back(slot);
            models.emplace(slot, *session.Models()[n]);
        }
        if (explicitMode && !session.Edit([&](auto& document) {
                document.physics.playbackMode = std::uint8_t(mode);
            }))
            return fail("physics-mode-edit-failed");
        if (!scene.Load(session, models, reason))
            return fail(reason);
        for (const auto& item : session.Document().accessories) {
            std::filesystem::path accessory;
            if (!resolver.ResolveAccessory(item.slot, item.path, accessory))
                return fail("scene-accessory-not-resolved");
            std::ifstream source(accessory, std::ios::binary);
            std::string bytes{std::istreambuf_iterator<char>(source), {}};
            std::shared_ptr<const runtime::AccessoryResource> resource;
            if (!source ||
                !runtime::AccessoryResource::Parse(bytes.data(), bytes.size(), resource, reason) ||
                !scene.AttachAccessory(item.slot, resource, reason))
                return fail(reason);
        }
    } else {
        std::ifstream file(path, std::ios::binary);
        format::ModelDocument model;
        if (!file || !format::ReadModelDocument(Read, &file, model))
            return fail("model-read-failed");
        if (!scene.LoadModel(model, reason))
            return fail(reason);
        slots.push_back(0);
    }
    if (explicitMode && !scene.Session() && !scene.SetPhysicsMode(mode, reason))
        return fail(reason);
    if (!motionPath.empty()) {
        format::VmdDocument motion;
        if (!ReadMotion(std::filesystem::u8path(motionPath), motion))
            return fail("motion-read-failed");
        if (scene.Session()) {
            if (!scene.BindGlobalMotion(motion, reason))
                return fail(reason);
        } else {
            std::shared_ptr<const runtime::MotionClip> clip;
            if (!runtime::MotionClip::Create(scene.Resource(0), motion, clip, reason) ||
                !scene.BindMotion(0, clip, reason))
                return fail(reason);
        }
    }
    std::ostringstream result;
    result.imbue(std::locale::classic());
    result << std::setprecision(std::numeric_limits<float>::max_digits10);
    result << "{\"schema\":1,\"stage\":\"P2\",\"physics_mode\":" << scene.Simulation().physicsMode
           << ",\"snapshots\":[";
    bool first = true;
    const auto snapshot = [&](const char* operation) {
        if (!first)
            result << ',';
        first = false;
        result << "{\"operation\":" << Quote(operation) << ",\"models\":[";
        bool firstModel = true;
        for (auto slot : slots) {
            if (!firstModel)
                result << ',';
            firstModel = false;
            result << "{\"slot\":" << unsigned(slot) << ",\"pose\":";
            WriteSnapshot(result, *scene.Snapshot(slot), *scene.Resource(slot));
            result << ",\"bodies\":[";
            bool firstBody = true;
            for (const auto& body : scene.Bodies(slot)) {
                if (!firstBody)
                    result << ',';
                firstBody = false;
                Numbers(result, body);
            }
            result << "]}";
        }
        result << "],\"accessories\":[";
        bool firstAccessory = true;
        if (scene.Session())
            for (const auto& accessory : scene.Session()->Pose().accessories) {
                cpu_math::Matrix matrix;
                if (!scene.AccessoryPlacement(accessory.slot, matrix, reason))
                    return false;
                if (!firstAccessory)
                    result << ',';
                firstAccessory = false;
                result << "{\"slot\":" << unsigned(accessory.slot) << ",\"world\":";
                Numbers(result, matrix);
                result << '}';
            }
        result << "]}";
        return true;
    };
    if (frames.empty())
        frames.push_back(0);
    for (auto frame : frames)
        if (!scene.Seek(frame, reason) || !snapshot("seek"))
            return fail(reason);
    if (playFrames) {
        const auto base = scene.Simulation().cursorSeconds;
        if (!scene.StartPlayback(reason))
            return fail(reason);
        for (unsigned n = 1; n <= playFrames; ++n)
            if (!scene.Advance(base + float(n) / 30, 1.f / 30, reason))
                return fail(reason);
        scene.Pause();
        if (!snapshot("play"))
            return fail(reason);
    }
    if (!save.empty()) {
        if (!scene.Session())
            return fail("save-requires-pmm");
        const auto destination = std::filesystem::absolute(std::filesystem::u8path(save));
        if (destination.lexically_normal() == path.lexically_normal())
            return fail("save-must-not-overwrite-input");
        if (destination.parent_path().lexically_normal() != path.parent_path().lexically_normal())
            return fail("save-directory-must-match-source");
        std::ofstream file(std::filesystem::u8path(save), std::ios::binary);
        if (!file || !scene.Session()->Save(
                         [](void* p, const void* bytes, std::size_t count) {
                             auto& stream = *static_cast<std::ostream*>(p);
                             stream.write(static_cast<const char*>(bytes), count);
                             return bool(stream);
                         },
                         &file))
            return fail("scene-save-failed");
    }
    const auto& counters = scene.Counters();
    result << "],\"counters\":{\"frames\":" << counters.frames
           << ",\"prepared_models\":" << counters.preparedModels
           << ",\"published_models\":" << counters.publishedModels << "},\"status\":\"passed\"}\n";
    if (output.empty())
        std::cout << result.str();
    else {
        std::ofstream file(std::filesystem::u8path(output), std::ios::binary);
        file << result.str();
        if (!file)
            return fail("snapshot-write-failed");
    }
    return 0;
}
}
#ifdef _WIN32
int wmain(int argc, wchar_t** argv) {
    std::vector<std::string> args;
    for (int n = 0; n < argc; ++n)
        args.push_back(std::filesystem::path(argv[n]).u8string());
    try {
        return Run(args);
    } catch (const std::exception& e) {
        std::cerr << "runtime-error:" << e.what() << '\n';
        return 1;
    }
}
#else
int main(int argc, char** argv) {
    try {
        return Run({argv, argv + argc});
    } catch (const std::exception& e) {
        std::cerr << "runtime-error:" << e.what() << '\n';
        return 1;
    }
}
#endif
