#include "mikudancestudio/model_cpu_preview.hpp"
#ifdef MIKUDANCESTUDIO_DIAG
#define NOMINMAX
#include <Windows.h>
#include "mikudancestudio/mmd_app.hpp"
#include "mikudancestudio/rhi_d3d11.hpp"
#include "mikudancestudio/rhi_d3d12.hpp"
#include "mikudancestudio/rhi_scene_renderer.hpp"
#include <cstdio>
#include <cstring>
#include <map>
#include <algorithm>
#include <cstdlib>
#include <fstream>
#include <iomanip>
#include "mikudancestudio/render_model_inputs.hpp"
#include "mikudancestudio/rhi_history_replay.hpp"

namespace mikudancestudio {
namespace {
bool Option(const char* name) {
    char flag[2]{};
    return GetEnvironmentVariableA(name, flag, 2) == 1 && flag[0] == '1';
}
const rhi::ModelOptions& DiagnosticModelOptions() {
    // Frozen for this process, shared by subset/full-scene diagnostic devices
    // and every replacement. Changing the environment cannot alter mid-frame math.
    static const rhi::ModelOptions options{
        rhi::ModelCompatibility::Dx9Measured, Option("MDS_RHI_DIAGNOSTIC_ALPHA12"),
        Option("MDS_RHI_DIAGNOSTIC_TEXTURE_FMA"), Option("MDS_RHI_DIAGNOSTIC_SPECULAR_FMA")};
    return options;
}
struct Preview {
    const rhi::ModelOptions options = DiagnosticModelOptions();
    HWND window11 = nullptr, window12 = nullptr;
    rhi::D3D11Bundle eleven;
    rhi::D3D12Bundle twelve;
    std::unique_ptr<rhi::SceneRenderer> renderer11, renderer12;
    render::ModelSubmissionBuilder builder;
    mdl::ModelRecord* collecting = nullptr;
    bool blocked = false;
    std::vector<render::ModelMaterialPacket> packets;
    std::vector<rhi::ModelPipeline> references;
    bool historyAttempted = false, historyActive = false;
    rhi::ModelReplayStream history;
    rhi::RgbaImage historyBefore;
    std::vector<rhi::RgbaImage> historyAfter;
    std::vector<rhi::RgbaImage> historyBoundaries;
    rhi::HistoryDesc target;
    bool attempted = false, creationFailed = false, failed = false;
    std::uint64_t sequence = 0;
    ~Preview() {
        renderer11.reset();
        renderer12.reset();
        eleven.swapchain.reset();
        twelve.swapchain.reset();
        eleven.device.reset();
        twelve.device.reset();
        if (window11)
            DestroyWindow(window11);
        if (window12)
            DestroyWindow(window12);
    }
    bool Initialize() {
        if (attempted)
            return !creationFailed;
        attempted = true;
#if defined(MIKUDANCESTUDIO_RHI_D3D11) && defined(MIKUDANCESTUDIO_RHI_D3D12)
        const auto window = [](const wchar_t* title) {
            return CreateWindowExW(0, L"STATIC", title, WS_POPUP, 0, 0, 64, 64, nullptr, nullptr,
                                   GetModuleHandleW(nullptr), nullptr);
        };
        window11 = window(L"CPU batch DX11");
        window12 = window(L"CPU batch DX12");
        rhi::SwapchainDesc desc;
        desc.window = {rhi::WindowSystem::Win32, reinterpret_cast<std::uintptr_t>(window11)};
        desc.width = desc.height = 64;
        desc.present = rhi::PresentMode::Immediate;
        eleven = rhi::CreateD3D11Device(desc, rhi::D3D11Driver::Hardware, options);
        desc.window.handle = reinterpret_cast<std::uintptr_t>(window12);
        twelve = rhi::CreateD3D12Device(desc, rhi::D3D12Driver::Hardware, {options, false});
        char directory[MAX_PATH]{};
        if (GetEnvironmentVariableA("MDS_RHI_CPU_GEOMETRY_DIR", directory, MAX_PATH) > 0) {
            char path[MAX_PATH]{};
            std::snprintf(path, sizeof(path), "%s\\cpu_preview_devices.csv", directory);
            FILE* file = nullptr;
            const bool fresh = GetFileAttributesA(path) == INVALID_FILE_ATTRIBUTES;
            if (!fopen_s(&file, path, "a") && file) {
                if (fresh)
                    std::fputs(
                        "backend,success,stage,hresult,reason,adapter,diagnosticAlpha12,diagnosticTextureFma,diagnosticSpecularFma,process\n",
                        file);
                std::fprintf(
                    file,
                    "dx11,%d,%u,%llx,%s,%s,%u,%u,%u,%lu\ndx12,%d,%u,%llx,%s,%s,%u,%u,%u,%lu\n",
                    bool(eleven), unsigned(eleven.stage), eleven.creationError,
                    eleven.creationReason.c_str(), eleven.adapterName.c_str(),
                    unsigned(options.diagnosticAlpha12), unsigned(options.diagnosticTextureFma),
                    unsigned(options.diagnosticSpecularFma), GetCurrentProcessId(), bool(twelve),
                    unsigned(twelve.stage), twelve.creationError, twelve.creationReason.c_str(),
                    twelve.adapterName.c_str(), unsigned(options.diagnosticAlpha12),
                    unsigned(options.diagnosticTextureFma), unsigned(options.diagnosticSpecularFma),
                    GetCurrentProcessId());
                std::fclose(file);
            }
        }
        creationFailed = !eleven || !twelve ||
                         eleven.modelSupport.diagnosticAlpha12 != options.diagnosticAlpha12 ||
                         twelve.modelSupport.diagnosticAlpha12 != options.diagnosticAlpha12;
        creationFailed = creationFailed ||
                         eleven.modelSupport.diagnosticTextureFma != options.diagnosticTextureFma ||
                         twelve.modelSupport.diagnosticTextureFma != options.diagnosticTextureFma;
        creationFailed =
            creationFailed ||
            eleven.modelSupport.diagnosticSpecularFma != options.diagnosticSpecularFma ||
            twelve.modelSupport.diagnosticSpecularFma != options.diagnosticSpecularFma;
        if (!creationFailed) {
            if (!renderer11)
                renderer11 = std::make_unique<rhi::SceneRenderer>(*eleven.device);
            else
                creationFailed = !renderer11->Rebind(*eleven.device);
            if (!renderer12)
                renderer12 = std::make_unique<rhi::SceneRenderer>(*twelve.device);
            else
                creationFailed = creationFailed || !renderer12->Rebind(*twelve.device);
        }
#else
        creationFailed = true;
#endif
        return !creationFailed;
    }
};
std::map<mdl::ModelRecord*, std::unique_ptr<Preview>> previews;
bool FullSceneRequested() {
    char flag[2]{};
    return GetEnvironmentVariableA("MDS_RHI_CPU_FULL_SCENE", flag, 2) == 1 && flag[0] == '1';
}
bool HistoryRequested() {
    char flag[2]{};
    return GetEnvironmentVariableA("MDS_RHI_CPU_SCENE_HISTORY", flag, 2) == 1 && flag[0] == '1';
}
struct FullSceneCheckpoint {
    bool attempted = false, pending = false, playing = false;
    std::uint64_t accepted = 0, id = 0;
    std::string phase;
};
FullSceneCheckpoint fullCheckpoint;
std::string DiagnosticDirectory() {
    char directory[MAX_PATH]{};
    const auto n = GetEnvironmentVariableA("MDS_RHI_CPU_GEOMETRY_DIR", directory, MAX_PATH);
    return n && n < MAX_PATH ? directory : "";
}
std::vector<unsigned> Selection() {
    char text[256]{};
    const auto length = GetEnvironmentVariableA("MDS_RHI_CPU_MATERIALS", text, sizeof(text));
    if (!length)
        GetEnvironmentVariableA("MDS_RHI_MODEL_MATERIAL", text, sizeof(text));
    std::vector<unsigned> result;
    const char* cursor = text;
    while (*cursor) {
        char* end = nullptr;
        const auto n = std::strtoul(cursor, &end, 10);
        if (end == cursor || n > UINT32_MAX || (!result.empty() && n <= result.back()))
            return {};
        result.push_back(unsigned(n));
        if (!*end)
            break;
        if (*end != ',')
            return {};
        cursor = end + 1;
        if (!*cursor)
            return {};
    }
    return result;
}
void LifecycleReport(const char* operation, const char* status, const rhi::DeviceStatus& before11,
                     const rhi::DeviceStatus& before12, const Preview& p,
                     const rhi::SceneRendererStats& old11, const rhi::SceneRendererStats& old12) {
    char directory[MAX_PATH]{};
    if (!GetEnvironmentVariableA("MDS_RHI_CPU_GEOMETRY_DIR", directory, MAX_PATH))
        return;
    char path[MAX_PATH]{};
    std::snprintf(path, sizeof(path), "%s\\cpu_preview_lifecycle.csv", directory);
    const bool fresh = GetFileAttributesA(path) == INVALID_FILE_ATTRIBUTES;
    FILE* file = nullptr;
    if (fopen_s(&file, path, "a") || !file)
        return;
    if (fresh)
        std::fputs(
            "operation,status,backend,oldDevice,newDevice,oldEpoch,newEpoch,uploadsBefore,uploadsAfter,texturesBefore,texturesAfter,diagnosticAlpha12,diagnosticTextureFma,diagnosticSpecularFma,process\n",
            file);
    const auto row = [&](const char* backend, const rhi::DeviceStatus& old,
                         const rhi::Device& device, const rhi::SceneRendererStats& before,
                         const rhi::SceneRendererStats& after) {
        const auto now = device.Status();
        std::fprintf(file, "%s,%s,%s,%llu,%llu,%llu,%llu,%llu,%llu,%llu,%llu,%u,%u,%u,%lu\n",
                     operation, status, backend, old.identity, now.identity, old.epoch, now.epoch,
                     before.uploads, after.uploads, before.textureUploads, after.textureUploads,
                     unsigned(p.options.diagnosticAlpha12),
                     unsigned(p.options.diagnosticTextureFma),
                     unsigned(p.options.diagnosticSpecularFma), GetCurrentProcessId());
    };
    if (p.eleven.device)
        row("dx11", before11, *p.eleven.device, old11, p.renderer11->Stats());
    if (p.twelve.device)
        row("dx12", before12, *p.twelve.device, old12, p.renderer12->Stats());
    std::fclose(file);
}
bool VerifyLifecycle(Preview& p, const rhi::ModelReplayStream& stream,
                     rhi::PreparedModelFrame& old11, rhi::PreparedModelFrame& old12,
                     const rhi::RgbaImage& image11, const rhi::RgbaImage& image12) {
    char enabled[2]{};
    if (GetEnvironmentVariableA("MDS_RHI_CPU_LIFECYCLE", enabled, 2) != 1 || enabled[0] != '1' ||
        (p.sequence != 8 && p.sequence != 16))
        return true;
    const bool replace = p.sequence == 16;
    const auto before11 = p.eleven.device->Status(), before12 = p.twelve.device->Status();
    const auto stats11 = p.renderer11->Stats(), stats12 = p.renderer12->Stats();
    bool okay = true;
    rhi::PreparedModelFrame rebuilt11, rebuilt12;
    rhi::CompletionToken token;
    if (!replace) {
        auto enlarged = stream;
        if (enlarged.target.width > UINT32_MAX - 8)
            return false;
        enlarged.target.width += 8;
        okay = bool(p.renderer11->Prepare(1, enlarged, rebuilt11)) &&
               bool(p.renderer12->Prepare(1, enlarged, rebuilt12));
    } else {
        okay = p.renderer11->Detach() && p.renderer12->Detach();
        if (okay) {
            p.eleven.swapchain.reset();
            p.twelve.swapchain.reset();
            p.eleven.device.reset();
            p.twelve.device.reset();
            if (p.window11)
                DestroyWindow(p.window11);
            if (p.window12)
                DestroyWindow(p.window12);
            p.window11 = p.window12 = nullptr;
            p.attempted = false;
            okay = p.Initialize();
        }
    }
    if (okay)
        okay = !p.renderer11->Submit(old11, token) && !p.renderer12->Submit(old12, token);
    if (okay)
        okay = bool(p.renderer11->Prepare(1, stream, rebuilt11)) &&
               bool(p.renderer11->Submit(rebuilt11, token)) &&
               bool(p.renderer12->Prepare(1, stream, rebuilt12)) &&
               bool(p.renderer12->Submit(rebuilt12, token));
    rhi::RgbaImage restored11, restored12;
    if (okay)
        okay = p.eleven.device->ReadModelTarget(restored11) &&
               p.twelve.device->ReadModelTarget(restored12) &&
               restored11.pixels == image11.pixels && restored12.pixels == image12.pixels;
    if (okay) {
        const auto after11 = p.renderer11->Stats(), after12 = p.renderer12->Stats();
        if (replace)
            okay = p.eleven.device->Status().identity != before11.identity &&
                   p.twelve.device->Status().identity != before12.identity &&
                   after11.uploads == stats11.uploads + stats11.residentResources &&
                   after12.uploads == stats12.uploads + stats12.residentResources &&
                   after11.textureUploads > stats11.textureUploads &&
                   after12.textureUploads > stats12.textureUploads;
        else
            okay = p.eleven.device->Status().identity == before11.identity &&
                   p.twelve.device->Status().identity == before12.identity &&
                   p.eleven.device->Status().epoch > before11.epoch &&
                   p.twelve.device->Status().epoch > before12.epoch &&
                   after11.uploads == stats11.uploads && after12.uploads == stats12.uploads;
    }
    LifecycleReport(replace ? "replace" : "resize", okay ? "PASS" : "FAIL", before11, before12, p,
                    stats11, stats12);
    return okay;
}
void Report(MMDApp* app, unsigned material, std::uint64_t version, const char* status,
            const char* reason, std::size_t difference, const rhi::SceneRendererStats& a = {},
            const rhi::SceneRendererStats& b = {}, std::size_t covered = 0) {
    char directory[MAX_PATH]{};
    const auto length = GetEnvironmentVariableA("MDS_RHI_CPU_GEOMETRY_DIR", directory, MAX_PATH);
    if (!length || length >= MAX_PATH)
        return;
    char path[MAX_PATH]{};
    if (std::snprintf(path, sizeof(path), "%s\\cpu_preview.%lu.csv", directory,
                      GetCurrentProcessId()) >= MAX_PATH)
        return;
    const bool fresh = GetFileAttributesA(path) == INVALID_FILE_ATTRIBUTES;
    FILE* file = nullptr;
    if (fopen_s(&file, path, "a") || !file)
        return;
    if (fresh)
        std::fputs(
            "frame,material,version,status,reason,differentPixels,dx11Uploads,dx12Uploads,dx11TextureUploads,dx12TextureUploads,coveredPixels,process\n",
            file);
    const auto* session = app->PortableSceneSession();
    const int evaluatedFrame = session && session->Loaded()
                                   ? static_cast<int>(session->Pose().frame)
                                   : app->CurrentFrame();
    std::fprintf(file, "%d,%u,%llu,%s,%s,%zu,%llu,%llu,%llu,%llu,%zu,%lu\n", evaluatedFrame,
                 material, version, status, reason, difference, a.uploads, b.uploads,
                 a.textureUploads, b.textureUploads, covered, GetCurrentProcessId());
    std::fclose(file);
}
}
namespace {
void HistoryReport(MMDApp* app, unsigned boundary, unsigned material, const char* backend,
                   const char* status, const char* reason, std::size_t different, std::size_t draws,
                   const char* phase = "material") {
    char directory[MAX_PATH]{};
    if (!GetEnvironmentVariableA("MDS_RHI_CPU_GEOMETRY_DIR", directory, MAX_PATH))
        return;
    char path[MAX_PATH]{};
    std::snprintf(path, sizeof(path), "%s\\cpu_scene_history.%lu.csv", directory,
                  GetCurrentProcessId());
    const bool fresh = GetFileAttributesA(path) == INVALID_FILE_ATTRIBUTES;
    FILE* file = nullptr;
    if (fopen_s(&file, path, "a") || !file)
        return;
    if (fresh)
        std::fputs(
            "frame,boundary,material,backend,status,reason,differentPixels,prefixDraws,process,phase\n",
            file);
    std::fprintf(file, "%u,%u,%u,%s,%s,%s,%zu,%zu,%lu,%s\n", app->CpuRenderFrame().scene.frame,
                 boundary, material, backend, status, reason, different, draws,
                 GetCurrentProcessId(), phase);
    std::fclose(file);
}
std::size_t RegionDifference(const rhi::RgbaImage& full, const rhi::RgbaImage& native,
                             const rhi::ModelViewport& region) {
    if (!rhi::ValidModelViewport(region, full.width, full.height) || native.width != region.width ||
        native.height != region.height ||
        full.pixels.size() != std::uint64_t(full.width) * full.height * 4 ||
        native.pixels.size() != std::uint64_t(native.width) * native.height * 4)
        return SIZE_MAX;
    std::size_t different = 0;
    for (unsigned y = 0; y < region.height; ++y)
        for (unsigned x = 0; x < region.width; ++x)
            different +=
                std::memcmp(full.pixels.data() +
                                (std::size_t(y + region.y) * full.width + x + region.x) * 4,
                            native.pixels.data() + (std::size_t(y) * native.width + x) * 4, 4) != 0;
    return different;
}
void HistoryFailureImages(const rhi::RgbaImage& full, const rhi::RgbaImage& native,
                          const rhi::ModelViewport& region, unsigned boundary, const char* backend,
                          const char* phase) {
    char directory[MAX_PATH]{};
    if (!GetEnvironmentVariableA("MDS_RHI_CPU_GEOMETRY_DIR", directory, MAX_PATH))
        return;
    if (!rhi::ValidModelViewport(region, full.width, full.height) || native.width != region.width ||
        native.height != region.height)
        return;
    auto cropped = native;
    for (unsigned y = 0; y < region.height; ++y)
        std::memcpy(cropped.pixels.data() + std::size_t(y) * region.width * 4,
                    full.pixels.data() + (std::size_t(y + region.y) * full.width + region.x) * 4,
                    region.width * 4);
    const auto stem = std::string(directory) + "/cpu_history_failure." +
                      std::to_string(GetCurrentProcessId()) + "." + phase + "." +
                      std::to_string(boundary) + "." + backend;
    const auto save = [](const std::string& path, const rhi::RgbaImage& image) {
        std::ofstream file(path, std::ios::binary);
        file.write(reinterpret_cast<const char*>(image.pixels.data()), image.pixels.size());
    };
    save(stem + ".native.rgba", native);
    save(stem + ".candidate.rgba", cropped);
    std::ofstream differences(stem + ".pixels.csv");
    differences
        << "x,y,nativeR,nativeG,nativeB,nativeA,candidateR,candidateG,candidateB,candidateA\n";
    for (unsigned y = 0; y < region.height; ++y)
        for (unsigned x = 0; x < region.width; ++x) {
            const auto offset = (std::size_t(y) * region.width + x) * 4;
            const auto* a = native.pixels.data() + offset;
            const auto* b = cropped.pixels.data() + offset;
            if (std::memcmp(a, b, 4))
                differences << x + region.x << "," << y + region.y << "," << unsigned(a[0]) << ","
                            << unsigned(a[1]) << "," << unsigned(a[2]) << "," << unsigned(a[3])
                            << "," << unsigned(b[0]) << "," << unsigned(b[1]) << ","
                            << unsigned(b[2]) << "," << unsigned(b[3]) << "\n";
        }
}
void ExecuteSceneHistory(MMDApp* app, Preview& p, const rhi::ModelReplayStream& suffix) {
    rhi::ModelReplayStream joined;
    auto result = rhi::ComposeModelReplay(p.history, suffix, joined);
    if (!result || p.historyAfter.size() != p.packets.size() ||
        p.historyBoundaries.size() != rhi::ValidateModelReplay(p.history).draws + 1) {
        HistoryReport(app, 0, p.packets.front().material, "both", "BLOCKED",
                      !result ? result.reason.c_str() : "missing_native_after_image", 0, 0);
        return;
    }
    // Separate diagnostic resource owners: prefix IDs must not collide with
    // resident subset resources. Both use the shared renderer implementation.
    rhi::SceneRenderer eleven(*p.eleven.device), twelve(*p.twelve.device);
    const auto region = joined.initializedRegion.value_or(
        rhi::ModelViewport{0, 0, joined.target.width, joined.target.height, 0, 1});
    const auto prefixDraws = rhi::ValidateModelReplay(p.history).draws;
    std::vector<std::size_t> boundaries{1};
    for (std::size_t n = 0; n < p.history.events.size(); ++n)
        if (p.history.events[n].op == rhi::ModelReplayOp::Draw)
            boundaries.push_back(n + 1);
    for (std::size_t n = p.history.events.size(); n < joined.events.size(); ++n)
        if (joined.events[n].op == rhi::ModelReplayOp::Draw)
            boundaries.push_back(n + 1);
    for (std::size_t n = 0; n < boundaries.size(); ++n) {
        auto stream = joined;
        stream.events.resize(boundaries[n]);
        const bool preceding = n <= prefixDraws;
        const auto selected = preceding ? 0 : n - prefixDraws;
        const auto& native = preceding ? p.historyBoundaries[n] : p.historyAfter[selected - 1];
        const auto material =
            preceding ? p.packets.front().material : p.packets[selected - 1].material;
        const auto phase = preceding ? "prefix" : "material";
        const auto boundary = preceding ? unsigned(n) : unsigned(selected);
        rhi::PreparedModelFrame frame;
        rhi::CompletionToken token;
        const auto run = [&](const char* name, rhi::Device& device, rhi::SceneRenderer& renderer) {
            rhi::ModelReplayResult rendered;
            if (n == 0) {
                // Clear-only is a native frame operation, not a model replay
                // (whose validation deliberately requires at least one draw).
                const bool cleared = device.EnsureModelTarget(stream.target) &&
                                     device.BeginFrame(stream.clear) && device.EndFrame();
                token = cleared ? device.SignalCompletion() : rhi::CompletionToken{};
                rendered = {cleared && token && device.WaitCompletion(token, 5000), 0, 0,
                            "initial_clear_failed"};
            } else {
                rendered = renderer.Prepare(1, stream, frame);
                if (rendered)
                    rendered = renderer.Submit(frame, token);
            }
            rhi::RgbaImage image;
            const bool read = rendered && device.ReadModelTarget(image);
            const auto different = read ? RegionDifference(image, native, region) : SIZE_MAX;
            HistoryReport(app, boundary, material, name, read && !different ? "PASS" : "FAIL",
                          !rendered   ? rendered.reason.c_str()
                          : !read     ? "readback_failed"
                          : different ? "native_image_difference"
                                      : "matched",
                          different, prefixDraws, phase);
            if (read && different && different != SIZE_MAX)
                HistoryFailureImages(image, native, region, boundary, name, phase);
            renderer.Collect();
            return read && !different;
        };
        const bool a = run("dx11", *p.eleven.device, eleven),
                   b = run("dx12", *p.twelve.device, twelve);
        if (!a || !b)
            break; // stop this history; independent subset stays usable
    }
}
void ExecutePreview(MMDApp* app, Preview& p, unsigned material, std::uint64_t version,
                    const rhi::ModelReplayStream& stream,
                    const rhi::ModelReplayStream* baseline = nullptr) {
    auto result = rhi::ValidateModelReplay(stream);
    if (!p.Initialize()) {
        Report(app, material, version, "UNAVAILABLE", "native_backend_creation", 0);
        return;
    }
    if (p.failed) {
        Report(app, material, version, "BLOCKED", "previous_draw_failed", 0);
        return;
    }
    rhi::PreparedModelFrame a, b;
    rhi::CompletionToken completion;
    rhi::RgbaImage native11, native12;
    if (baseline) {
        result = p.renderer11->Prepare(1, *baseline, a);
        if (result)
            result = p.renderer11->Submit(a, completion);
        if (result)
            result = p.renderer12->Prepare(1, *baseline, b);
        if (result)
            result = p.renderer12->Submit(b, completion);
        if (result && (!p.eleven.device->ReadModelTarget(native11) ||
                       !p.twelve.device->ReadModelTarget(native12)))
            result = {false, 0, 0, "reference_readback_failed"};
    }
    if (result)
        result = p.renderer11->Prepare(1, stream, a);
    if (result)
        result = p.renderer11->Submit(a, completion);
    if (result)
        result = p.renderer12->Prepare(1, stream, b);
    if (result)
        result = p.renderer12->Submit(b, completion);
    rhi::RgbaImage image11, image12;
    const bool read = result && p.eleven.device->ReadModelTarget(image11) &&
                      p.twelve.device->ReadModelTarget(image12);
    std::size_t different = 0;
    if (read && image11.pixels.size() == image12.pixels.size())
        for (std::size_t i = 0; i < image11.pixels.size(); i += 4)
            different += std::memcmp(image11.pixels.data() + i, image12.pixels.data() + i, 4) != 0;
    else if (read)
        different = 1;
    if (read && baseline) {
        if (native11.pixels.size() != image11.pixels.size() ||
            native12.pixels.size() != image12.pixels.size())
            ++different;
        else
            for (std::size_t i = 0; i < image11.pixels.size(); i += 4)
                different +=
                    std::memcmp(native11.pixels.data() + i, image11.pixels.data() + i, 4) != 0 ||
                    std::memcmp(native12.pixels.data() + i, image12.pixels.data() + i, 4) != 0;
    }
    std::size_t covered = 0;
    const unsigned char background = app->state.blackBackgroundEnabled ? 0 : 255;
    if (read)
        for (std::size_t i = 0; i < image11.pixels.size(); i += 4)
            covered += image11.pixels[i] != background || image11.pixels[i + 1] != background ||
                       image11.pixels[i + 2] != background;
    // A tiny moving batch can legitimately cover no samples in a frame. The
    // test must prove nonempty coverage over the sequence, not in every frame.
    const bool lifecycle = read && !different && VerifyLifecycle(p, stream, a, b, image11, image12);
    const bool passed = read && !different && lifecycle;
    Report(app, material, version, passed ? "PASS" : "FAIL",
           !result      ? result.reason.c_str()
           : !read      ? "readback_failed"
           : different  ? "backend_image_difference"
           : !lifecycle ? "lifecycle_verification_failed"
           : !covered   ? "matched_empty"
                        : "matched",
           different, p.renderer11->Stats(), p.renderer12->Stats(), covered);
    p.failed = !passed;
    p.renderer11->Collect();
    p.renderer12->Collect();
    ++p.sequence;
}
void DrawOrderReport(MMDApp* app, Preview& p, const rhi::ModelReplayStream& stream) {
    char directory[MAX_PATH]{};
    if (!GetEnvironmentVariableA("MDS_RHI_CPU_GEOMETRY_DIR", directory, MAX_PATH))
        return;
    char path[MAX_PATH]{};
    std::snprintf(path, sizeof(path), "%s\\cpu_material_order.%lu.csv", directory,
                  GetCurrentProcessId());
    const bool fresh = GetFileAttributesA(path) == INVALID_FILE_ATTRIBUTES;
    FILE* file = nullptr;
    if (fopen_s(&file, path, "a") || !file)
        return;
    if (fresh)
        std::fputs(
            "submission,frame,model,order,material,firstIndex,indexCount,toonId,toonVersion,baseId,baseVersion,sphereId,sphereVersion,cull,blend,depthWrite,status,process\n",
            file);
    // The CSV survives scene unload, while Preview::sequence is local to a
    // model's renderer. Keep log identities unique across same-process reloads.
    static std::uint64_t nextSubmission = 0;
    const auto submission = nextSubmission++;
    int slot = -1;
    for (unsigned i = 0; i < 255; ++i)
        if (app->ModelSlot(i) == p.collecting)
            slot = i;
    unsigned order = 0;
    for (const auto& e : stream.events)
        if (e.op == rhi::ModelReplayOp::Draw) {
            const auto& d = e.draw;
            std::fprintf(
                file, "%llu,%u,%d,%u,%u,%u,%u,%llu,%llu,%llu,%llu,%llu,%llu,%u,%u,%u,PASS,%lu\n",
                submission, app->CpuRenderFrame().scene.frame, slot, order,
                p.packets[order].material, d.firstIndex, d.indexCount, d.textures[0].id,
                d.textures[0].version, d.textures[1].id, d.textures[1].version, d.textures[2].id,
                d.textures[2].version, unsigned(d.pipeline.raster.cull),
                d.pipeline.raster.alphaBlend, d.pipeline.raster.depthWrite, GetCurrentProcessId());
            ++order;
        }
    std::fclose(file);
}
}
bool CpuMaterialSelected(unsigned material) {
    const auto selected = Selection();
    return std::find(selected.begin(), selected.end(), material) != selected.end();
}
void BeginCpuModelMaterials(MMDApp* app, mdl::ModelRecord* model) {
    if (CpuScenePreviewRequested() && !CpuSceneNativeCheckpointPending())
        return;
    char enabled[2]{};
    if (GetEnvironmentVariableA("MDS_RHI_CPU_MATERIALS", enabled, 2) == 0)
        return;
    const auto found = app->CpuModelFrames().find(model);
    if (found == app->CpuModelFrames().end() || !found->second->verified || found->second->failed)
        return;
    auto& owner = previews[model];
    if (!owner)
        owner = std::make_unique<Preview>();
    auto& p = *owner;
    p.collecting = model;
    p.blocked = false;
    p.packets.clear();
    p.references.clear();
    p.historyActive = false;
    p.historyAfter.clear();
}
void EndCpuModelMaterials(MMDApp* app, mdl::ModelRecord* model) {
    const auto owner = previews.find(model);
    if (owner == previews.end() || owner->second->collecting != model)
        return;
    auto& p = *owner->second;
    const auto selected = Selection();
    bool complete = !p.blocked && p.packets.size() == selected.size() && selected.size() > 1 &&
                    (p.references.empty() || p.references.size() == p.packets.size());
    for (std::size_t i = 0; complete && i < selected.size(); ++i)
        complete = p.packets[i].material == selected[i];
    if (!complete) {
        Report(app, selected.empty() ? 0 : selected.front(), 0, "BLOCKED",
               "incomplete_material_submission", 0);
        p.collecting = nullptr;
        return;
    }
    const auto& geometry = app->CpuModelFrames().at(model)->geometry.Snapshot();
    rhi::ModelReplayStream stream;
    const auto clear = app->state.blackBackgroundEnabled ? rhi::ClearColor{0, 0, 0, 0}
                                                         : rhi::ClearColor{1, 1, 1, 0};
    const auto result = p.builder.Build(geometry, p.historyActive ? p.history.target : p.target,
                                        clear, p.packets, stream);
    if (!result) {
        Report(app, selected.front(), geometry.main.key.version, "BLOCKED", result.reason.c_str(),
               0);
        p.collecting = nullptr;
        return;
    }
    if (p.historyActive) {
        stream.clear = p.history.clear;
        stream.source = p.history.source;
    }
    auto baseline = stream;
    std::size_t index = 0;
    if (!p.references.empty())
        for (auto& e : baseline.events)
            if (e.op == rhi::ModelReplayOp::Draw)
                e.draw.pipeline = p.references.at(index++);
    DrawOrderReport(app, p, stream);
    // Replay every prefix from its clear; no application history is imported.
    // Keep the full immutable resource set in each prefix so future texture
    // versions are not retired by the renderer between diagnostic boundaries.
    for (std::size_t boundary = 0; boundary < stream.events.size(); ++boundary)
        if (stream.events[boundary].op == rhi::ModelReplayOp::Draw) {
            auto prefix = stream;
            prefix.events.resize(boundary + 1);
            auto reference = baseline;
            reference.events.resize(boundary + 1);
            ExecutePreview(
                app, p, std::strtoul(stream.events[boundary].label.c_str() + 9, nullptr, 10),
                geometry.main.key.version, prefix, p.references.empty() ? nullptr : &reference);
            if (p.failed)
                break;
        }
    if (p.historyActive && !p.failed)
        ExecuteSceneHistory(app, p, stream);
    p.collecting = nullptr;
}
void SubmitCpuModelPreview(MMDApp* app, mdl::ModelRecord* model, unsigned material, unsigned first,
                           unsigned count, const rhi::HistoryDesc& target,
                           const rhi::ModelPipeline& pipeline,
                           const std::array<rhi::MaterialTextureAsset, 3>& assets,
                           const rhi::ModelPipeline* reference) {
    char enabled[2]{};
    if (GetEnvironmentVariableA("MDS_RHI_CPU_PREVIEW", enabled, 2) != 1 || enabled[0] != '1')
        return;
    const auto found = app->CpuModelFrames().find(model);
    if (found == app->CpuModelFrames().end() || !found->second->verified || found->second->failed)
        return;
    if (!found->second->ready) {
        BlockCpuModelPreview(app, model, material, "unverified_cpu_frame");
        return;
    }
    auto& owner = previews[model];
    if (!owner)
        owner = std::make_unique<Preview>();
    auto& p = *owner;
    render::ModelMaterialPacket packet;
    packet.material = material;
    packet.firstIndex = first;
    packet.indexCount = count;
    packet.pipeline = pipeline;
    packet.textures = assets;
    if (p.collecting == model) {
        if (p.packets.empty() && HistoryRequested() && !FullSceneRequested() &&
            !p.historyAttempted) {
            p.historyAttempted = true;
            int slot = -1;
            for (unsigned i = 0; i < 255; ++i)
                if (app->ModelSlot(i) == model)
                    slot = i;
            CompareHistoryReplay(app->Renderer()->device, unsigned(slot), material, count);
            p.historyActive = CopyPortableHistoryReplay(p.history) &&
                              CopyVerifiedHistoryImage(p.historyBefore) &&
                              CopyVerifiedHistoryBoundaries(p.historyBoundaries);
            char cpuPrefix[16]{};
            if (p.historyActive && GetEnvironmentVariableA("MDS_RHI_CPU_PREFIX", cpuPrefix, 16))
                p.historyActive = CopyCpuSceneHistoryReplay(p.history);
            if (p.historyActive) {
                p.history.source.scene =
                    reinterpret_cast<std::uintptr_t>(app->PortableSceneSession());
                p.history.source.frame = app->CpuRenderFrame().scene.frame;
            } else
                HistoryReport(app, 0, material, "both", "BLOCKED", "native_history_not_verified", 0,
                              0);
        }
        p.target = target;
        p.packets.push_back(std::move(packet));
        if (reference)
            p.references.push_back(*reference);
        return;
    }
    const auto& geometry = found->second->geometry.Snapshot();
    rhi::ModelReplayStream stream;
    const auto clear = app->state.blackBackgroundEnabled ? rhi::ClearColor{0, 0, 0, 0}
                                                         : rhi::ClearColor{1, 1, 1, 0};
    auto result = p.builder.Build(geometry, target, clear, {packet}, stream);
    if (!result) {
        BlockCpuModelPreview(app, model, material, result.reason.c_str());
        return;
    }
    auto baseline = stream;
    if (reference)
        baseline.events.back().draw.pipeline = *reference;
    ExecutePreview(app, p, material, geometry.main.key.version, stream,
                   reference ? &baseline : nullptr);
}

void CaptureCpuMaterialHistory(MMDApp* app, mdl::ModelRecord* model, unsigned material) {
    if (!HistoryRequested() || !CpuMaterialSelected(material))
        return;
    const auto owner = previews.find(model);
    if (owner == previews.end())
        return;
    auto& p = *owner->second;
    if (!p.historyActive || p.collecting != model || p.blocked)
        return;
    if (p.history.source.frame != app->CpuRenderFrame().scene.frame ||
        p.historyAfter.size() >= p.packets.size() ||
        p.packets[p.historyAfter.size()].material != material) {
        HistoryReport(app, unsigned(p.historyAfter.size() + 1), material, "both", "BLOCKED",
                      "history_boundary_order", 0, 0);
        p.historyActive = false;
        return;
    }
    rhi::RgbaImage image;
    if (!ReadVerifiedHistoryImage(app->Renderer()->device, image)) {
        HistoryReport(app, unsigned(p.historyAfter.size() + 1), material, "both", "BLOCKED",
                      "native_after_readback_failed", 0, 0);
        p.historyActive = false;
        return;
    }
    p.historyAfter.push_back(std::move(image));
}

bool CpuSceneNativeCheckpointPending() {
    return fullCheckpoint.pending;
}
void BeginCpuFullSceneValidation(MMDApp* app) {
    if (!FullSceneRequested() || !Option("MDS_RHI_CPU_LIVE_HISTORY") || fullCheckpoint.pending)
        return;
    // Between explicit checkpoints do not start the older one-shot recorder:
    // otherwise an unfinished capture spills into overlays and the next clear.
    SuspendHistoryReplay();
    if (!app->CpuRenderFrameReady())
        return;
    const auto directory = DiagnosticDirectory();
    if (directory.empty())
        return;
    std::ifstream request(directory + "/cpu_live.request." + std::to_string(GetCurrentProcessId()) +
                          ".txt");
    std::uint64_t id = 0;
    std::string phase;
    if (!(request >> id >> phase) || !id || id <= fullCheckpoint.accepted || id > 16 ||
        phase.size() > 63 ||
        phase.find_first_not_of("abcdefghijklmnopqrstuvwxyz0123456789_") != std::string::npos)
        return;
    fullCheckpoint.id = id;
    fullCheckpoint.accepted = id;
    fullCheckpoint.phase = phase;
    fullCheckpoint.pending = true;
    InvalidateCpuScenePreviewGate();
    fullCheckpoint.playing = app->PlaybackActive() != 0;
    RestartHistoryReplay();
}
void HistoryFailureContract(const rhi::ModelReplayStream& stream,
                            const rhi::ModelReplayStream* native, unsigned boundary,
                            const char* backend, const char* phase) {
    char directory[MAX_PATH]{};
    if (!GetEnvironmentVariableA("MDS_RHI_CPU_GEOMETRY_DIR", directory, MAX_PATH))
        return;
    const rhi::ModelReplayEvent* selected = nullptr;
    unsigned draw = 0;
    for (const auto& event : stream.events)
        if (event.op == rhi::ModelReplayOp::Draw) {
            ++draw;
            if (draw == boundary) {
                selected = &event;
                break;
            }
        }
    if (!selected)
        return;
    const auto stem = std::string(directory) + "/cpu_history_failure." +
                      std::to_string(GetCurrentProcessId()) + "." + phase + "." +
                      std::to_string(boundary) + "." + backend;
    std::ofstream out(stem + ".contract.csv");
    out << "field,component,value\n";
    const auto matrix = [&](const char* name, const auto& values) {
        for (unsigned n = 0; n < values.size(); ++n)
            out << name << "," << n << "," << std::setprecision(17) << values[n] << "\n";
    };
    matrix("world", selected->draw.pipeline.shader.world);
    matrix("view", selected->draw.pipeline.shader.view);
    matrix("projection", selected->draw.pipeline.shader.projection);
    out << "draw,sequence," << selected->sequence << "\ndraw,firstIndex,"
        << selected->draw.firstIndex << "\ndraw,indexCount," << selected->draw.indexCount << "\n";
    out << "vertex,id," << selected->draw.vertices.id << "\nvertex,version,"
        << selected->draw.vertices.version << "\nindex,id," << selected->draw.indices.id
        << "\nindex,version," << selected->draw.indices.version << "\n";
    for (unsigned n = 0; n < 3; ++n)
        out << "texture" << n << ",id," << selected->draw.textures[n].id << "\ntexture" << n
            << ",version," << selected->draw.textures[n].version << "\n";
    bool fieldsExact = false;
    if (native) {
        const rhi::ModelReplayEvent* reference = nullptr;
        unsigned referenceDraw = 0;
        for (const auto& event : native->events)
            if (event.op == rhi::ModelReplayOp::Draw && ++referenceDraw == boundary) {
                reference = &event;
                break;
            }
        if (reference) {
            const auto differences =
                render::CompareModelInputs(selected->draw.pipeline, reference->draw.pipeline);
            fieldsExact = std::all_of(differences.begin(), differences.end(), [](const auto& d) {
                // These three arrays are convenience summaries retained by the
                // CPU producer; Active() deliberately removes them before the
                // real replacement contract is checked.
                return d.field == "diffuse" || d.field == "ambient" || d.field == "specular";
            });
            if (!differences.empty()) {
                const auto& d = differences.front();
                out << "field_difference,0," << d.field << "[" << d.component
                    << "] generated=" << std::setprecision(17) << d.generated
                    << " reference=" << d.reference << "\n";
            }
        }
    }
    out << "classification,0,"
        << (fieldsExact ? "portable_contract_exact_backend_precision"
                        : "portable_contract_field_difference")
        << "\n";
}
void FinishCpuFullSceneValidation(MMDApp* app) {
    const bool live = Option("MDS_RHI_CPU_LIVE_HISTORY");
    if (!FullSceneRequested() || (live ? !fullCheckpoint.pending : fullCheckpoint.attempted) ||
        !app->CpuRenderFrameReady())
        return;
    fullCheckpoint.attempted = true;
    fullCheckpoint.pending = false;
    const auto directory = DiagnosticDirectory();
    if (directory.empty())
        return;
    struct Acknowledge {
        std::string directory;
        std::uint64_t checkpoint;
        bool enabled, passed = false;
        ~Acknowledge() {
            if (enabled) {
                SuspendHistoryReplay();
                std::ofstream ack(directory + "/cpu_live.ack." +
                                  std::to_string(GetCurrentProcessId()) + ".txt");
                ack << checkpoint << " " << (passed ? "PASS" : "BLOCKED") << "\n";
            }
        }
    } ack{directory, fullCheckpoint.id, live};
    const auto filename =
        directory + "/cpu_full_scene." + std::to_string(GetCurrentProcessId()) + ".csv";
    const bool fresh = GetFileAttributesA(filename.c_str()) == INVALID_FILE_ATTRIBUTES;
    std::ofstream report(filename, live ? std::ios::app : std::ios::trunc);
    if (fresh || !live)
        report
            << "frame,boundary,family,backend,status,reason,differentPixels,draws,process,checkpoint,phase,playing,pmmVersion,diagnosticAlpha12,diagnosticTextureFma,diagnosticSpecularFma\n";
    rhi::ModelReplayStream stream, nativePortable;
    std::vector<rhi::RgbaImage> native;
    const auto row = [&](unsigned boundary, const std::string& family, const char* backend,
                         const char* status, const char* reason, std::size_t difference,
                         std::size_t draws) {
        const auto* session = app->PortableSceneSession();
        report << app->CpuRenderFrame().scene.frame << "," << boundary << "," << family << ","
               << backend << "," << status << "," << reason << "," << difference << "," << draws
               << "," << GetCurrentProcessId() << "," << fullCheckpoint.id << ","
               << (live ? fullCheckpoint.phase : "oneshot") << ","
               << unsigned(live ? fullCheckpoint.playing : app->PlaybackActive() != 0) << ","
               << (session ? (session->Document().envelope.header.version == format::PmmVersion::V1
                                  ? "v1"
                                  : "v2")
                           : "none")
               << "," << unsigned(DiagnosticModelOptions().diagnosticAlpha12) << ","
               << unsigned(DiagnosticModelOptions().diagnosticTextureFma) << ","
               << unsigned(DiagnosticModelOptions().diagnosticSpecularFma) << "\n";
        report.flush();
    };
    if (live && (fullCheckpoint.phase == "playing" || fullCheckpoint.phase == "v2_playing") !=
                    fullCheckpoint.playing) {
        row(0, "clear", "both", "BLOCKED", "checkpoint_playback_state_mismatch", 0, 0);
        return;
    }
    CompareHistoryReplay(app->Renderer()->device, 255, 65535, 0);
    const bool nativePortableReady = CopyPortableHistoryReplay(nativePortable);
    if (!CopyCpuSceneHistoryReplay(stream) || !nativePortableReady ||
        !CopyVerifiedHistoryBoundaries(native)) {
        row(0, "clear", "both", "BLOCKED", "native_or_cpu_full_scene_unverified", 0, 0);
        return;
    }
    const auto draws = rhi::ValidateModelReplay(stream).draws;
    if (native.size() != draws + 1) {
        row(0, "clear", "both", "BLOCKED", "missing_native_boundary", 0, draws);
        return;
    }
    if (live) {
        std::ofstream resources(directory + "/cpu_full_resources." +
                                std::to_string(GetCurrentProcessId()) + "." +
                                std::to_string(fullCheckpoint.id) + ".csv");
        resources << "id,version,kind,bytes,levels,hash\n";
        for (const auto& r : stream.resources) {
            std::uint64_t hash = 14695981039346656037ull, size = r.bytes.size();
            const auto digest = [&](const auto& bytes) {
                for (auto b : bytes) {
                    hash ^= b;
                    hash *= 1099511628211ull;
                }
            };
            digest(r.bytes);
            for (const auto& mip : r.texture.levels) {
                digest(mip.pixels);
                size += mip.pixels.size();
            }
            resources << r.key.id << "," << r.key.version << "," << unsigned(r.kind) << "," << size
                      << "," << r.texture.levels.size() << "," << hash << "\n";
        }
        if (!resources) {
            row(0, "clear", "both", "BLOCKED", "resource_evidence_failed", 0, draws);
            return;
        }
    }
    Preview p;
    if (!p.Initialize()) {
        row(0, "clear", "both", "BLOCKED", "native_backend_creation", 0, draws);
        return;
    }
    const auto region = stream.initializedRegion.value_or(
        rhi::ModelViewport{0, 0, stream.target.width, stream.target.height, 0, 1});
    std::vector<std::size_t> boundaries{1};
    std::vector<std::string> families{"clear"};
    for (std::size_t n = 0; n < stream.events.size(); ++n)
        if (stream.events[n].op == rhi::ModelReplayOp::Draw) {
            boundaries.push_back(n + 1);
            families.push_back(stream.events[n].label);
        }
    rhi::PreparedModelFrame final11, final12;
    rhi::RgbaImage last11, last12;
    bool exact = true;
    for (std::size_t n = 0; n < boundaries.size(); ++n) {
        auto prefix = stream;
        prefix.events.resize(boundaries[n]);
        const auto run = [&](const char* name, rhi::Device& device, rhi::SceneRenderer& renderer) {
            rhi::ModelReplayResult result;
            rhi::CompletionToken token;
            rhi::PreparedModelFrame prepared;
            if (n == 0) {
                const bool okay = device.EnsureModelTarget(prefix.target) &&
                                  device.BeginFrame(prefix.clear) && device.EndFrame();
                token = okay ? device.SignalCompletion() : rhi::CompletionToken{};
                result = {okay && token && device.WaitCompletion(token, 5000), 0, 0,
                          "initial_clear_failed"};
            } else {
                result = renderer.Prepare(1, prefix, prepared);
                if (result)
                    result = renderer.Submit(prepared, token);
            }
            rhi::RgbaImage image;
            const bool read = result && device.ReadModelTarget(image);
            const auto difference = read ? RegionDifference(image, native[n], region) : SIZE_MAX;
            row(unsigned(n), families[n], name, read && !difference ? "PASS" : "FAIL",
                !result      ? result.reason.c_str()
                : !read      ? "readback_failed"
                : difference ? "native_image_difference"
                             : "matched",
                difference, draws);
            if (read && difference && difference != SIZE_MAX) {
                HistoryFailureImages(image, native[n], region, unsigned(n), name, "full_scene");
                HistoryFailureContract(prefix, &nativePortable, unsigned(n), name, "full_scene");
            }
            if (n + 1 == boundaries.size() && read && !difference) {
                if (name[3] == '1') {
                    last11 = image;
                    final11 = std::move(prepared);
                } else {
                    last12 = image;
                    final12 = std::move(prepared);
                }
            }
            renderer.Collect();
            return read && !difference;
        };
        const bool a = run("dx11", *p.eleven.device, *p.renderer11),
                   b = run("dx12", *p.twelve.device, *p.renderer12);
        if (!a || !b) {
            exact = false;
            break;
        }
    }
    if (exact && live) {
        // Re-submit the final prepared CPU frame without new uploads. Target
        // resize and device replacement must reject the old frame and preserve
        // the frozen Alpha policy, CPU texture versions and exact output.
        const auto before11 = p.renderer11->Stats(), before12 = p.renderer12->Stats();
        rhi::CompletionToken token;
        for (unsigned repeat = 0; repeat < 3 && exact; ++repeat) {
            rhi::RgbaImage a, b;
            exact = p.renderer11->Submit(final11, token) && p.renderer12->Submit(final12, token) &&
                    p.eleven.device->ReadModelTarget(a) && p.twelve.device->ReadModelTarget(b) &&
                    !RegionDifference(a, native.back(), region) &&
                    !RegionDifference(b, native.back(), region) &&
                    p.renderer11->Stats().uploads == before11.uploads &&
                    p.renderer12->Stats().uploads == before12.uploads;
        }
        p.sequence = 8;
        if (exact)
            exact = VerifyLifecycle(p, stream, final11, final12, last11, last12);
        // Refresh prepared snapshots after resize before testing replacement.
        if (exact)
            exact = p.renderer11->Prepare(1, stream, final11) &&
                    p.renderer12->Prepare(1, stream, final12);
        p.sequence = 16;
        if (exact)
            exact = VerifyLifecycle(p, stream, final11, final12, last11, last12);
        row(unsigned(draws), "lifecycle", "both", exact ? "PASS" : "FAIL",
            exact ? "repeat_resize_replace_exact" : "repeat_or_lifecycle_failure",
            exact ? 0 : SIZE_MAX, draws);
    }
    if (exact && CpuScenePreviewRequested())
        exact = ValidateResidentCpuSceneFrame(app, native.back());
    ack.passed = exact && bool(report);
}

const rhi::ModelOptions& CpuDiagnosticModelOptions() {
    return DiagnosticModelOptions();
}
void ReleaseCpuModelPreview() {
    ReleaseCpuSceneFrame();
    previews.clear();
    const auto accepted = fullCheckpoint.accepted;
    fullCheckpoint = {};
    fullCheckpoint.accepted = accepted;
}
void ReleaseCpuModelPreview(mdl::ModelRecord* model) {
    previews.erase(model);
}
void BlockCpuModelPreview(MMDApp* app, mdl::ModelRecord* model, unsigned material,
                          const char* reason) {
    const auto p = previews.find(model);
    if (p != previews.end() && p->second->collecting == model)
        p->second->blocked = true;
    const auto found = app->CpuModelFrames().find(model);
    if (found != app->CpuModelFrames().end() && found->second->verified && !found->second->failed)
        Report(app, material, found->second->geometry.Snapshot().main.key.version, "BLOCKED",
               reason, 0);
}
}
#endif
