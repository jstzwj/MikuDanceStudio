#pragma once
#include "mikudancestudio/rhi_backend.hpp"
#include "mikudancestudio/rhi_scene_renderer.hpp"
#include "mikudancestudio/rhi_screen_texture.hpp"
#include "mikudancestudio/rhi_d3d12.hpp"
#include "mikudancestudio/rhi_vulkan.hpp"
#include "material_sampling_fixture.hpp"
#include "screen_capture_dx9_reference.hpp"
#include <iostream>
#include <fstream>
#include <filesystem>
#define SCREEN_CHECK(x)                                                                            \
    do {                                                                                           \
        if (!(x))                                                                                  \
            throw std::runtime_error(std::string("line ") + std::to_string(__LINE__) + ": " + #x); \
    } while (0)
namespace screen_acceptance {
using namespace mikudancestudio;
using namespace mikudancestudio::rhi;
using namespace mikudancestudio::render;
void Require(ModelReplayResult result) {
    if (!result)
        throw std::runtime_error(result.reason);
}
RgbaImage Image(const AttachmentReadback& r) {
    return {r.description.width, r.description.height, r.bytes};
}
void CompareCapture(const AttachmentReadback& actual, const RgbaImage& expected) {
    SCREEN_CHECK(actual.bytes.size() == expected.pixels.size());
    unsigned maximum = 0;
    std::uint64_t total = 0;
    for (std::size_t n = 0; n < actual.bytes.size(); ++n) {
        const auto delta = unsigned(std::abs(int(actual.bytes[n]) - int(expected.pixels[n])));
        if (delta > maximum) {
            maximum = delta;
        }
        total += delta;
    }
    std::cout << "native StretchRect maximum=" << maximum << "\n";
    if (maximum > 2) {
        std::filesystem::create_directories("out");
        std::ofstream("out/capture-actual.rgba", std::ios::binary)
            .write(reinterpret_cast<const char*>(actual.bytes.data()), actual.bytes.size());
        std::ofstream("out/capture-native.rgba", std::ios::binary)
            .write(reinterpret_cast<const char*>(expected.pixels.data()), expected.pixels.size());
    }
    SCREEN_CHECK(maximum <= 2 && double(total) / actual.bytes.size() <= .25);
    // Explicit 2D filtered-resize rounding; identity copies stay byte-exact.
}
void Generated(OffscreenBackendDevice& native, const BackendOptions& options) {
    SceneRenderer renderer(*native.device);
    ScreenTextureHistory history;
    screen_reference::Capture dx9;
    AttachmentReadback previous;
    PreparedModelFrame prepared;
    CompletionToken token;
    bool captured = false;
    for (auto mode :
         {ScreenCaptureKind::Disabled, ScreenCaptureKind::FullFrame, ScreenCaptureKind::FullFrame,
          ScreenCaptureKind::Disabled, ScreenCaptureKind::CropFourByThree,
          ScreenCaptureKind::BackgroundRefresh, ScreenCaptureKind::FullFrame}) {
        auto frame = sampling_fixture::Fixture(0, SamplerFilter::Linear, 1, SamplerAddress::Wrap, 1,
                                               false, false);
        frame.resources.pop_back();
        auto& consumer = frame.passes[1].draws[0];
        consumer.textures = {};
        consumer.pipeline.shader.layout = {};
        consumer.pipeline.shader.lighting = false;
        frame.passes[1].color->clear = {.2f, .4f, .8f, .3f};
        const auto access =
            TextureAccess::ColorAttachment | TextureAccess::Sampled | TextureAccess::CopySource;
        frame.targets.push_back({10, {23, 17, TextureFormat::Rgba8, access}});
        FramePass background;
        background.color = FrameAttachment{10};
        background.color->clear = {1, .25f, .6f, 1};
        frame.passes.insert(frame.passes.begin(), background);
        SCREEN_CHECK(AppendPostProcess(frame, 1, 5, 96, 64));
        const auto read = history.ReadTarget();
        SCREEN_CHECK(history.Apply(frame, {{2, 0}}, mode, 5, {10, 23, 17}));
        const auto binding = frame.passes[2].draws[0].targetTextures[0].target;
        SCREEN_CHECK(binding == (mode == ScreenCaptureKind::BackgroundRefresh ? 10
                                 : captured                                   ? read
                                                                              : 0));
        SCREEN_CHECK(ApplyMaterialSampling(frame, MaterialSamplingPolicy::PortableTrilinear));
        Require(renderer.Prepare(1, frame, prepared));
        Require(renderer.Submit(prepared, token));
        AttachmentReadback main, final, old;
        SCREEN_CHECK(renderer.ReadTarget(1, 1, main) && renderer.ReadTarget(1, 5, final));
        SCREEN_CHECK(main.bytes == final.bytes);
        if (mode == ScreenCaptureKind::BackgroundRefresh) {
            std::size_t backgroundPixels = 0;
            for (std::size_t n = 0; n < final.bytes.size(); n += 4)
                backgroundPixels +=
                    final.bytes[n] == 255 && final.bytes[n + 1] == 64 && final.bytes[n + 2] == 153;
            SCREEN_CHECK(backgroundPixels > 100);
        }
        SCREEN_CHECK(renderer.ReadTarget(1, read, old));
        if (captured)
            SCREEN_CHECK(old.bytes == previous.bytes);
        else
            for (std::size_t n = 0; n < old.bytes.size(); n += 4)
                SCREEN_CHECK(old.bytes[n] == 0 && old.bytes[n + 1] == 0 && old.bytes[n + 2] == 0 &&
                             old.bytes[n + 3] == 255);
        // The same prepared frame keeps reading the same prior capture.
        for (unsigned n = 0; n < 4; ++n)
            Require(renderer.Submit(prepared, token));
        AttachmentReadback repeated;
        SCREEN_CHECK(renderer.ReadTarget(1, 5, repeated) && repeated.bytes == final.bytes);
        history.Commit();
        if (mode == ScreenCaptureKind::FullFrame || mode == ScreenCaptureKind::CropFourByThree) {
            captured = true;
            SCREEN_CHECK(history.ReadTarget() != read);
            SCREEN_CHECK(renderer.ReadTarget(1, history.ReadTarget(), previous));
            CompareCapture(previous, dx9.Copy(Image(final), mode));
        } else
            SCREEN_CHECK(history.ReadTarget() == read);
    }
    SCREEN_CHECK(renderer.Unload(1));
    // Reuse after unload initializes explicit history rather than stale device contents.
    auto reset = sampling_fixture::Fixture(0, SamplerFilter::Point, 1, SamplerAddress::Clamp, 1,
                                           false, false);
    reset.resources.pop_back();
    reset.passes[1].draws[0].textures = {};
    reset.passes[1].draws[0].pipeline.shader.layout = {};
    SCREEN_CHECK(AppendPostProcess(reset, 1, 5, 96, 64));
    SCREEN_CHECK(history.Apply(reset, {{1, 0}}, ScreenCaptureKind::Disabled, 5));
    Require(renderer.Prepare(1, reset, prepared));
    Require(renderer.Submit(prepared, token));
    SCREEN_CHECK(renderer.ReadTarget(1, history.ReadTarget(), previous));
    for (std::size_t n = 0; n < previous.bytes.size(); n += 4)
        SCREEN_CHECK(previous.bytes[n] == 0 && previous.bytes[n + 1] == 0 &&
                     previous.bytes[n + 2] == 0 && previous.bytes[n + 3] == 255);
    SCREEN_CHECK(renderer.Detach());
    auto replacement = CreateOffscreenBackendDevice(96, 64, options);
    SCREEN_CHECK(replacement && renderer.Rebind(*replacement.device));
    Require(renderer.Prepare(1, reset, prepared));
    Require(renderer.Submit(prepared, token));
    AttachmentReadback oldAfterReplacement_;
    SCREEN_CHECK(renderer.ReadTarget(1, history.ReadTarget(), oldAfterReplacement_));
    SCREEN_CHECK(oldAfterReplacement_.bytes == previous.bytes);
    SCREEN_CHECK(renderer.Detach());
}
void CaptureExtents(Device& device) {
    SceneRenderer renderer(device);
    screen_reference::Capture dx9;
    for (const auto size : {std::pair<unsigned, unsigned>{137, 79}, {79, 137}, {1280, 720}})
        for (auto mode : {ScreenCaptureKind::FullFrame, ScreenCaptureKind::CropFourByThree})
            for (unsigned captureSize : {512u, 1024u}) {
                auto frame = sampling_fixture::Fixture(2, SamplerFilter::Linear, 1,
                                                       SamplerAddress::Wrap, 7.37f, false, false);
                for (auto& t : frame.targets)
                    if (t.id == 1 || t.id == 2) {
                        t.description.width = size.first;
                        t.description.height = size.second;
                    }
                SCREEN_CHECK(AppendPostProcess(frame, 1, 5, size.first, size.second));
                ScreenTextureHistory history;
                SCREEN_CHECK(history.Apply(frame, {}, mode, 5, {}, captureSize));
                PreparedModelFrame prepared;
                CompletionToken token;
                Require(renderer.Prepare(2, frame, prepared));
                Require(renderer.Submit(prepared, token));
                AttachmentReadback final, capture;
                SCREEN_CHECK(renderer.ReadTarget(2, 5, final));
                history.Commit();
                SCREEN_CHECK(renderer.ReadTarget(2, history.ReadTarget(), capture));
                std::cout << "capture " << size.first << "x" << size.second << " -> " << captureSize
                          << " mode=" << unsigned(mode) << "\n";
                try {
                    CompareCapture(capture, dx9.Copy(Image(final), mode, captureSize));
                } catch (...) {
                    std::ofstream("out/capture-source.rgba", std::ios::binary)
                        .write(reinterpret_cast<const char*>(final.bytes.data()),
                               final.bytes.size());
                    throw;
                }
                SCREEN_CHECK(renderer.Unload(2));
            }
    SCREEN_CHECK(renderer.Detach());
}
}
