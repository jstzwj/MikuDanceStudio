#include "mikudancestudio/scene_resources.hpp"
#include "mikudancestudio/render_snapshot.hpp"
#include <algorithm>
#include <cstdlib>
#include <cmath>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <limits>
#include <string>
#include <vector>

namespace fs = std::filesystem;
using namespace mikudancestudio::format;
namespace {
int Read(void* opaque, void* output, std::size_t size) {
    auto& stream = *static_cast<std::istream*>(opaque);
    stream.read(static_cast<char*>(output), static_cast<std::streamsize>(size));
    return static_cast<int>(stream.gcount());
}
bool Write(void* opaque, const void* input, std::size_t size) {
    auto& stream = *static_cast<std::ostream*>(opaque);
    stream.write(static_cast<const char*>(input), static_cast<std::streamsize>(size));
    return stream.good();
}
bool Unsigned(const char* text, std::uint32_t& result) {
    try {
        const std::string value(text);
        if (value.empty() || value[0] == '-')
            return false;
        std::size_t end = 0;
        const auto parsed = std::stoull(value, &end);
        if (end != value.size() ||
            parsed > static_cast<unsigned long long>(std::numeric_limits<std::int32_t>::max()))
            return false;
        result = static_cast<std::uint32_t>(parsed);
        return true;
    } catch (...) {
        return false;
    }
}
}
int main(int argc, char** argv) {
    if (argc < 3) {
        std::cerr << "usage: mds_scene_cli input.pmm output.pmm "
                     "[--root directory] [--reselect slot model] "
                     "[--frame index] [--seconds elapsed]\n";
        return 2;
    }
    const fs::path input = fs::u8path(argv[1]);
    const fs::path output = fs::u8path(argv[2]);
    if (fs::absolute(input).lexically_normal() == fs::absolute(output).lexically_normal()) {
        std::cerr << "input and output must differ\n";
        return 2;
    }
    SceneResourceResolver resources(input);
    std::uint32_t frame = 0;
    float seconds = 5.0f / 30.0f;
    for (int index = 3; index < argc;) {
        const std::string option(argv[index++]);
        if (index >= argc)
            return 2;
        const char* value = argv[index++];
        if (option == "--root")
            resources.AddSearchRoot(fs::u8path(value));
        else if (option == "--frame") {
            if (!Unsigned(value, frame))
                return 2;
        } else if (option == "--reselect") {
            if (index >= argc)
                return 2;
            std::uint32_t slot = 0;
            if (!Unsigned(value, slot) || slot > 255)
                return 2;
            resources.ReselectModel(static_cast<std::uint8_t>(slot), fs::u8path(argv[index++]));
        } else if (option == "--seconds") {
            try {
                std::size_t end = 0;
                seconds = std::stof(value, &end);
                if (end != std::string(value).size() || !std::isfinite(seconds) || seconds < 0)
                    return 2;
            } catch (...) {
                return 2;
            }
        } else
            return 2;
    }
    std::ifstream source(input, std::ios::binary);
    SceneSession session;
    if (!source ||
        !SceneSession::Load(Read, &source, SceneResourceResolver::Resolve, &resources, session)) {
        for (const auto& missing : resources.Missing()) {
            const auto& raw = missing.reference.rawPath;
            std::cerr << "missing model slot=" << static_cast<unsigned>(missing.reference.slot)
                      << " path="
                      << std::string(raw.begin(), std::find(raw.begin(), raw.end(), '\0')) << '\n';
        }
        std::cerr << "scene load failed\n";
        return 3;
    }
    if (!session.Seek(frame))
        return 4;
    const auto previousWidth = session.Document().envelope.settings.renderWidth;
    const auto previousFov = session.Document().envelope.settings.cameraFov;
    if (previousWidth == std::numeric_limits<std::int32_t>::max() ||
        (!session.Document().cameraKeys.dense.empty() &&
         session.Document().cameraKeys.dense[0].fov == std::numeric_limits<std::int32_t>::max()) ||
        !session.Edit([](PmmSceneDocument& scene) {
            ++scene.envelope.settings.renderWidth;
            scene.envelope.settings.cameraFov += 1.0f;
            if (!scene.cameraKeys.dense.empty())
                ++scene.cameraKeys.dense[0].fov;
        }) ||
        !session.Seek(frame) || !session.StartPlayback() || !session.Advance(seconds))
        return 4;
    const auto evaluatedFrame = session.Pose().frame;
    mikudancestudio::render::SceneSnapshot rendered;
    if (!mikudancestudio::render::CaptureSceneSnapshot(session, rendered))
        return 4;
    std::error_code error;
    if (fs::exists(output, error) || error) {
        std::cerr << "output already exists or cannot be inspected\n";
        return 5;
    }
    const auto temporary = fs::path(output.string() + ".tmp");
    if (fs::exists(temporary, error) || error)
        return 5;
    {
        std::ofstream destination(temporary, std::ios::binary);
        if (!destination || !session.Save(Write, &destination) || !destination.flush()) {
            destination.close();
            fs::remove(temporary, error);
            return 5;
        }
    }
    std::ifstream saved(temporary, std::ios::binary);
    SceneSession reloaded;
    SceneResourceResolver rereadResources(temporary);
    for (const auto& pair : resources.Resolved())
        rereadResources.ReselectModel(pair.first, pair.second);
    const bool reread = saved && SceneSession::Load(Read, &saved, SceneResourceResolver::Resolve,
                                                    &rereadResources, reloaded);
    mikudancestudio::render::SceneSnapshot readback;
    const bool snapshotReadback = reread && reloaded.Seek(evaluatedFrame) &&
                                  mikudancestudio::render::CaptureSceneSnapshot(reloaded, readback);
    bool renderStateMatches = snapshotReadback && rendered.frame == readback.frame &&
                              rendered.models.size() == readback.models.size() &&
                              rendered.accessories.size() == readback.accessories.size();
    if (renderStateMatches) {
        for (std::size_t index = 0; index < rendered.models.size(); ++index) {
            const auto& before = rendered.models[index];
            const auto& after = readback.models[index];
            if (before.slot != after.slot || before.visible != after.visible ||
                before.vertexCount != after.vertexCount || before.indexCount != after.indexCount ||
                before.batches.size() != after.batches.size() ||
                before.bones.size() != after.bones.size()) {
                renderStateMatches = false;
                break;
            }
        }
    }
    const bool valid = reread && renderStateMatches &&
                       reloaded.Document().envelope.settings.renderWidth == previousWidth + 1 &&
                       reloaded.Document().envelope.settings.cameraFov == previousFov + 1 &&
                       reloaded.Pose().frame == evaluatedFrame &&
                       reloaded.Document().models.size() == session.Document().models.size();
    saved.close();
    if (!valid) {
        fs::remove(temporary, error);
        std::cerr << "saved scene failed validation read=" << reread
                  << " width=" << (reread ? reloaded.Document().envelope.settings.renderWidth : 0)
                  << " expectedWidth=" << previousWidth + 1
                  << " fov=" << (reread ? reloaded.Document().envelope.settings.cameraFov : 0)
                  << " expectedFov=" << previousFov + 1
                  << " frame=" << (reread ? reloaded.Pose().frame : 0)
                  << " expectedFrame=" << evaluatedFrame << " render=" << renderStateMatches
                  << " snapshotFrames=" << rendered.frame << ',' << readback.frame
                  << " snapshotModels=" << rendered.models.size() << ',' << readback.models.size()
                  << " accessories=" << rendered.accessories.size() << ','
                  << readback.accessories.size();
        if (snapshotReadback && !rendered.models.empty() && !readback.models.empty())
            std::cerr << " model=" << unsigned(rendered.models[0].slot) << ','
                      << unsigned(readback.models[0].slot)
                      << " visible=" << rendered.models[0].visible << ','
                      << readback.models[0].visible
                      << " vertices=" << rendered.models[0].vertexCount << ','
                      << readback.models[0].vertexCount
                      << " indices=" << rendered.models[0].indexCount << ','
                      << readback.models[0].indexCount
                      << " batches=" << rendered.models[0].batches.size() << ','
                      << readback.models[0].batches.size()
                      << " bones=" << rendered.models[0].bones.size() << ','
                      << readback.models[0].bones.size();
        std::cerr << '\n';
        return 6;
    }
    fs::rename(temporary, output, error);
    if (error) {
        fs::remove(temporary, error);
        return 6;
    }
    std::cout << "{\"version\":" << static_cast<int>(session.Document().envelope.header.version)
              << ",\"models\":" << reloaded.Document().models.size()
              << ",\"resolved\":" << rereadResources.Resolved().size()
              << ",\"frame\":" << reloaded.Pose().frame
              << ",\"width\":" << reloaded.Document().envelope.settings.renderWidth
              << ",\"renderModels\":" << readback.models.size()
              << ",\"renderFrame\":" << readback.frame << ",\"readback\":true}\n";
    return 0;
}
