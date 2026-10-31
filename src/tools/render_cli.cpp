#include "mikudancestudio/scene_frame_producer.hpp"
#include "mikudancestudio/scene_resources.hpp"
#include "mikudancestudio/rhi_backend.hpp"
#include "mikudancestudio/rhi_scene_renderer.hpp"
#include "mikudancestudio/rhi_vulkan.hpp"
#include "runtime_cli_output.hpp"
#include <charconv>
#include <algorithm>
#include <cctype>
#include <chrono>
#include <iostream>
namespace {
using namespace mikudancestudio;
using namespace mikudancestudio::cli;
using namespace mikudancestudio::rhi;
using Clock = std::chrono::steady_clock;
double Ms(Clock::time_point start) {
    return std::chrono::duration<double, std::milli>(Clock::now() - start).count();
}
void Require(bool success, const std::string& reason) {
    if (!success)
        throw std::runtime_error(reason);
}
int Run(const std::vector<std::string>& args) {
    std::filesystem::path input, motion, assets, output;
    BackendOptions options;
    options.backend = Backend::Vulkan;
    render::SceneRenderSettings settings;
    settings.width = 127;
    settings.height = 95;
    settings.shadow.mode = 1;
    settings.shadow.mapSize = 128;
    unsigned frames = 1, seek = 0, replace = 0, resize = 0;
    for (std::size_t n = 1; n < args.size(); ++n) {
        const auto& arg = args[n];
        Require(n + 1 < args.size(), "missing_option_value:" + arg);
        const auto& value = args[++n];
        if (arg == "--input")
            input = std::filesystem::u8path(value);
        else if (arg == "--motion")
            motion = std::filesystem::u8path(value);
        else if (arg == "--assets")
            assets = std::filesystem::u8path(value);
        else if (arg == "--output")
            output = std::filesystem::u8path(value);
        else if (arg == "--vulkan-library")
            options.vulkanLibrary = value;
        else if (arg == "--driver") {
            Require(value == "software" || value == "hardware", "invalid_driver");
            options.driver =
                value == "software" ? BackendDriver::Software : BackendDriver::Hardware;
        } else if (arg == "--backend") {
            Require(value == "vulkan" || value == "dx11" || value == "dx12", "invalid_backend");
            options.backend = value == "dx11"   ? Backend::D3D11
                              : value == "dx12" ? Backend::D3D12
                                                : Backend::Vulkan;
        } else {
            unsigned number = 0;
            const auto parsed = std::from_chars(value.data(), value.data() + value.size(), number);
            Require(parsed.ec == std::errc{} && parsed.ptr == value.data() + value.size(),
                    "invalid_number:" + arg);
            if (arg == "--width")
                settings.width = number;
            else if (arg == "--height")
                settings.height = number;
            else if (arg == "--frames")
                frames = number;
            else if (arg == "--frame")
                seek = number;
            else if (arg == "--replace-at")
                replace = number;
            else if (arg == "--resize")
                resize = number;
            else if (arg == "--shadow")
                settings.shadow.mode = int(number);
            else if (arg == "--debug")
                options.debugLayer = number != 0;
            else
                throw std::runtime_error("unknown_option:" + arg);
        }
    }
    Require(
        !input.empty() && !assets.empty() && !output.empty() && frames > 0 && frames <= 10000 &&
            resize <= 1,
        "usage:mds_render_cli --input scene.pmm|model.pmd|model.pmx --assets directory --output prefix [--backend vulkan|dx11|dx12] [--driver hardware|software] [--motion vmd] [--frame N] [--frames N] [--resize 1] [--replace-at N] [--shadow 0..2]");
    input = std::filesystem::absolute(input);
    runtime::SceneRuntime scene;
    render::SceneRenderSources sources;
    std::string reason;
    auto extension = input.extension().u8string();
    std::transform(extension.begin(), extension.end(), extension.begin(),
                   [](unsigned char c) { return char(std::tolower(c)); });
    if (extension == ".pmm") {
        format::SceneResourceResolver resolver(input);
        format::SceneSession session;
        std::ifstream file(input, std::ios::binary);
        Require(file &&
                    format::SceneSession::Load(Read, &file, format::SceneResourceResolver::Resolve,
                                               &resolver, session),
                "scene_read_or_resolve");
        std::map<std::uint8_t, format::ModelDocument> models;
        for (std::size_t n = 0; n < session.Models().size(); ++n) {
            Require(bool(session.Models()[n]), "scene_model_missing");
            models.emplace(session.Document().models[n].slot, *session.Models()[n]);
        }
        Require(scene.Load(session, models, reason), reason);
        sources.models = resolver.Resolved();
        for (const auto& item : session.Document().accessories) {
            std::filesystem::path path;
            Require(resolver.ResolveAccessory(item.slot, item.path, path),
                    "scene_accessory_missing");
            sources.accessories.emplace(item.slot, path);
            std::ifstream source(path, std::ios::binary);
            std::string bytes{std::istreambuf_iterator<char>(source), {}};
            std::shared_ptr<const runtime::AccessoryResource> resource;
            Require(source &&
                        runtime::AccessoryResource::Parse(bytes.data(), bytes.size(), resource,
                                                          reason) &&
                        scene.AttachAccessory(item.slot, resource, reason),
                    reason);
        }
    } else {
        std::ifstream file(input, std::ios::binary);
        format::ModelDocument model;
        Require(file && format::ReadModelDocument(Read, &file, model), "model_read");
        Require(scene.LoadModel(model, reason), reason);
        sources.models.emplace(0, input);
    }
    if (!motion.empty()) {
        format::VmdDocument document;
        Require(ReadMotion(motion, document), "motion_read");
        if (scene.Session())
            Require(scene.BindGlobalMotion(document, reason), reason);
        else {
            std::shared_ptr<const runtime::MotionClip> clip;
            Require(runtime::MotionClip::Create(scene.Resource(0), document, clip, reason) &&
                        scene.BindMotion(0, clip, reason),
                    reason);
        }
    }
    Require(scene.SetPhysicsMode(0, reason) && scene.Seek(seek, reason), reason);
    FileAssetSource builtins(assets);
    render::SceneFrameProducer producer;
    Require(producer.Load(scene, sources, builtins, reason), reason);
    auto native = CreateOffscreenBackendDevice(settings.width, settings.height, options);
    Require(bool(native), native.reason);
    std::string adapter = native.adapter;
    SceneRenderer renderer(*native.device);
    PreparedModelFrame prepared;
    CompletionToken token;
    const auto initial = scene.Counters();
    if (frames > 1)
        Require(scene.StartPlayback(reason), reason);
    double updateMs = 0, produceMs = 0, prepareMs = 0, submitMs = 0, readMs = 0;
    unsigned replacements = 0;
    AttachmentReadback actual, main, shadow;
    const auto originalWidth = settings.width, originalHeight = settings.height;
    for (unsigned i = 0; i < frames; ++i) {
        auto start = Clock::now();
        if (i)
            Require(scene.Tick(1.f / 30, reason), reason);
        updateMs += Ms(start);
        settings.width = originalWidth + (resize && i % 2 ? 4 : 0);
        settings.height = originalHeight + (resize && i % 2 ? 2 : 0);
        const auto before = scene.Counters();
        RenderFrame frame;
        start = Clock::now();
        Require(producer.Build(scene, settings, frame, reason), reason);
        produceMs += Ms(start);
        Require(scene.Counters().frames == before.frames &&
                    scene.Counters().publishedModels == before.publishedModels,
                "render_producer_updated_cpu_scene");
        if (replace && i == replace) {
            const auto old = native.device->Status().identity;
            Require(renderer.Detach(), "detach_before_replace");
            native.device.reset();
            native = CreateOffscreenBackendDevice(settings.width, settings.height, options);
            Require(bool(native), native.reason);
            Require(native.device->Status().identity != old && renderer.Rebind(*native.device),
                    "replace_rebind");
            Require(!renderer.Submit(prepared, token), "accepted_old_prepared_frame");
            ++replacements;
        }
        start = Clock::now();
        auto result = renderer.Prepare(1, frame, prepared);
        Require(bool(result), result.reason);
        prepareMs += Ms(start);
        start = Clock::now();
        result = renderer.Submit(prepared, token);
        Require(bool(result), result.reason);
        Require(renderer.Collect(), "collect");
        submitMs += Ms(start);
        if (i + 1 == frames) {
            start = Clock::now();
            Require(bool(renderer.ReadTarget(1, frame.finalColor, actual)), "final_color_readback");
            Require(bool(renderer.ReadTarget(1, 1, main)), "scene_color_readback");
            Require(actual.bytes == main.bytes, "post_copy_not_exact");
            if (settings.shadow.mode)
                Require(bool(renderer.ReadTarget(1, 3, shadow)), "shadow_readback");
            readMs += Ms(start);
        }
    }
    Require(scene.Counters().frames - initial.frames == frames - 1, "cpu_update_count");
    if (!output.parent_path().empty())
        std::filesystem::create_directories(output.parent_path());
    const auto write = [&](const char* suffix, const std::vector<std::uint8_t>& bytes) {
        auto path = output;
        path += suffix;
        std::ofstream file(path, std::ios::binary);
        file.write(reinterpret_cast<const char*>(bytes.data()), bytes.size());
        Require(bool(file), "render_output_write");
    };
    write(".rgba", actual.bytes);
    if (!shadow.bytes.empty())
        write(".shadow.f32", shadow.bytes);
    const auto stats = renderer.Stats();
    VulkanStats vk;
#ifdef MIKUDANCESTUDIO_RHI_VULKAN
    if (options.backend == Backend::Vulkan) {
        vk = QueryVulkanStats(*native.device);
        Require(vk.validationErrors == 0, "vulkan_validation_errors");
    }
#endif
    Require(renderer.Unload(1) && renderer.Detach(), "render_unload");
    auto reportPath = output;
    reportPath += ".json";
    std::ofstream report(reportPath, std::ios::binary);
    report.imbue(std::locale::classic());
    report << "{\"schema\":1,\"stage\":\"P3\",\"status\":\"passed\",\"adapter\":" << Quote(adapter)
           << ",\"backend\":"
           << Quote(options.backend == Backend::Vulkan  ? "vulkan"
                    : options.backend == Backend::D3D11 ? "dx11"
                                                        : "dx12")
           << ",\"width\":" << actual.description.width
           << ",\"height\":" << actual.description.height << ",\"frames\":" << frames
           << ",\"cpu_updates\":" << scene.Counters().frames - initial.frames
           << ",\"device_replacements\":" << replacements << ",\"uploads\":" << stats.uploads
           << ",\"vulkan_readbacks\":" << vk.readbacks
           << ",\"validation_errors\":" << vk.validationErrors
           << ",\"milliseconds\":{\"cpu_update\":" << updateMs
           << ",\"frame_production\":" << produceMs << ",\"prepare\":" << prepareMs
           << ",\"submit_collect\":" << submitMs << ",\"explicit_readback\":" << readMs << "}}\n";
    Require(bool(report), "report_write");
    std::cout << "P3 scene rendered: " << adapter << ", frames=" << frames << '\n';
    return 0;
}
}
#ifdef _WIN32
int wmain(int argc, wchar_t** argv) {
    std::vector<std::string> args;
    for (int i = 0; i < argc; ++i)
        args.push_back(std::filesystem::path(argv[i]).u8string());
#else
int main(int argc, char** argv) {
    std::vector<std::string> args{argv, argv + argc};
#endif
    try {
        return Run(args);
    } catch (const std::exception& e) {
        std::cerr << "render_error:" << e.what() << '\n';
        return 1;
    }
}
