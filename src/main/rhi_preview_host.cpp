#include "mikudancestudio/rhi_preview_host.hpp"
#ifdef MIKUDANCESTUDIO_CPU_RENDERING
#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <Windows.h>
#include "mikudancestudio/runtime_cpu_scene.hpp"
#include "mikudancestudio/runtime_cpu_scene_producer.hpp"
#include "mikudancestudio/scene_resources.hpp"
#include "mikudancestudio/render_scene_frame.hpp"
#include "mikudancestudio/rhi_scene_preview.hpp"
#include "mikudancestudio/d3dx_dyn.hpp"
#include <algorithm>
#include <chrono>
#include <fstream>
#include <iostream>
#include <limits>
#include <map>
#include <cstring>
#include <stdexcept>
#include <sstream>

namespace mikudancestudio {
namespace {
using namespace format;
using namespace render;
using namespace rhi;
int Read(void* p, void* out, std::size_t size) {
    auto& in = *static_cast<std::istream*>(p);
    in.read(static_cast<char*>(out), size);
    return int(in.gcount());
}
bool Write(void* p, const void* data, std::size_t size) {
    auto& out = *static_cast<std::ostream*>(p);
    out.write(static_cast<const char*>(data), size);
    return bool(out);
}
bool SaveNew(const std::filesystem::path& path, const std::string& bytes) {
    // CREATE_NEW refuses an existing destination, including a race after a
    // file-picker check. Publish only after in-memory write/read validation.
    const HANDLE file = CreateFileW(path.c_str(), GENERIC_WRITE, 0, nullptr, CREATE_NEW,
                                    FILE_ATTRIBUTE_NORMAL, nullptr);
    if (file == INVALID_HANDLE_VALUE)
        return false;
    std::size_t offset = 0;
    bool okay = true;
    while (okay && offset < bytes.size()) {
        DWORD written = 0;
        const auto size = DWORD(std::min<std::size_t>(bytes.size() - offset, MAXDWORD));
        okay = WriteFile(file, bytes.data() + offset, size, &written, nullptr) != 0 && written != 0;
        offset += written;
    }
    okay = okay && FlushFileBuffers(file) != 0;
    okay = CloseHandle(file) != 0 && okay;
    if (!okay)
        DeleteFileW(path.c_str());
    return okay;
}
ModelMatrix Identity() {
    return {1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1};
}
std::uint64_t CpuStateFingerprint(RuntimeCpuScene& scene) {
    std::uint64_t hash = 14695981039346656037ull;
    const auto add = [&](const void* source, std::size_t size) {
        const auto* bytes = static_cast<const unsigned char*>(source);
        for (std::size_t n = 0; n < size; ++n) {
            hash ^= bytes[n];
            hash *= 1099511628211ull;
        }
    };
    const auto& clock = scene.Simulation();
    add(&clock.cursorSeconds, 4);
    add(&clock.remainingSeconds, 4);
    add(&clock.gravityNoiseTimer, 4);
    for (auto slot : scene.Order()) {
        CpuSolvedModel model;
        if (!scene.Snapshot(slot, model))
            throw std::runtime_error("cpu_state_snapshot_failed");
        add(&model.slot, 1);
        add(&model.visible, 1);
        add(&model.displayCursor, 4);
        add(&model.displayActive, 1);
        for (const auto& bone : model.bones) {
            for (const auto* matrix : {&bone.skin, &bone.local, &bone.world, &bone.extra})
                add(matrix->data(), 64);
            for (const auto* pose : {&bone.pose, &bone.backup, &bone.working})
                add(pose->data(), 28);
            add(&bone.cursor, 4);
            add(&bone.armedFrame, 4);
            add(&bone.active, 1);
            add(&bone.physicsDisabled, 1);
        }
        add(model.morphs.data(), model.morphs.size() * 4);
        add(model.ikEnabled.data(), model.ikEnabled.size());
        for (const auto& body : model.bodies)
            add(body.data(), body.size() * 4);
    }
    return hash;
}
struct HostWindow {
    HWND handle = nullptr;
    bool closed = false, seekLeft = false, seekRight = false, toggle = false, replace = false;
    static LRESULT CALLBACK Procedure(HWND h, UINT m, WPARAM w, LPARAM l) {
        auto* self = reinterpret_cast<HostWindow*>(GetWindowLongPtrW(h, GWLP_USERDATA));
        if (m == WM_NCCREATE) {
            self = static_cast<HostWindow*>(reinterpret_cast<CREATESTRUCTW*>(l)->lpCreateParams);
            SetWindowLongPtrW(h, GWLP_USERDATA, reinterpret_cast<LONG_PTR>(self));
        }
        if (self) {
            if (m == WM_CLOSE) {
                self->closed = true;
                return 0;
            }
            if (m == WM_KEYDOWN) {
                self->seekLeft = w == VK_LEFT;
                self->seekRight = w == VK_RIGHT;
                self->toggle = w == VK_SPACE;
                self->replace = w == VK_F5;
            }
        }
        return DefWindowProcW(h, m, w, l);
    }
    bool Open(const wchar_t* title, bool hidden) {
        WNDCLASSW c{};
        c.lpfnWndProc = Procedure;
        c.hInstance = GetModuleHandleW(nullptr);
        c.lpszClassName = L"MDS_INDEPENDENT_RHI";
        c.hCursor = LoadCursorW(nullptr, MAKEINTRESOURCEW(32512));
        if (!RegisterClassW(&c) && GetLastError() != ERROR_CLASS_ALREADY_EXISTS)
            return false;
        handle = CreateWindowExW(hidden ? WS_EX_NOACTIVATE | WS_EX_TOOLWINDOW : 0, c.lpszClassName,
                                 title, WS_OVERLAPPEDWINDOW, CW_USEDEFAULT, CW_USEDEFAULT, 800, 600,
                                 nullptr, nullptr, c.hInstance, this);
        return handle != nullptr;
    }
    ~HostWindow() {
        if (handle)
            DestroyWindow(handle);
    }
};

}
int RunIndependentRhiPreview(int argc, wchar_t** argv) {
    if (argc < 3) {
        std::cerr
            << "usage: mds_rhi_preview dx11|dx12|vulkan scene.pmm|model.pmd|model.pmx [--sampling portable|native] [--self-shadow 1|2|track] [--warp] [--frames N] [--root directory] [--reselect slot model] [--model-slot slot] [--model-materials-only|--scene-passes] [--reselect-accessory slot file.x] [--lifecycle] [--save-as output.pmm]\n";
        return 2;
    }
    try {
        BackendOptions options;
        const std::wstring api = argv[1];
        if (api != L"dx11" && api != L"dx12" && api != L"vulkan")
            return 2;
        options.backend = api == L"dx11"   ? Backend::D3D11
                          : api == L"dx12" ? Backend::D3D12
                                           : Backend::Vulkan;
        const auto input = std::filesystem::absolute(argv[2]);
        SceneResourceResolver resolver(input);
        resolver.SetPathDecoder([](const std::string& raw) -> std::optional<std::filesystem::path> {
            const int n = MultiByteToWideChar(932, MB_ERR_INVALID_CHARS, raw.data(),
                                              int(raw.size()), nullptr, 0);
            if (n <= 0)
                return {};
            std::wstring decoded(n, L'\0');
            MultiByteToWideChar(932, MB_ERR_INVALID_CHARS, raw.data(), int(raw.size()),
                                decoded.data(), n);
            return decoded;
        });
        unsigned frames = 0;
        int selected = -1;
        bool lifecycle = false, materialsOnly = false, scenePasses = false;
        bool selfShadow = false;
        SelfShadowSettings shadowSettings;
        std::filesystem::path saveAs;
        const auto number = [](const wchar_t* text) {
            std::size_t n = 0;
            const std::wstring s = text;
            if (s.empty() || s[0] == L'-')
                throw std::runtime_error("invalid_unsigned_argument");
            auto v = std::stoul(s, &n);
            if (n != s.size() || v > 100000)
                throw std::runtime_error("invalid_unsigned_argument");
            return unsigned(v);
        };
        for (int i = 3; i < argc; ++i) {
            const std::wstring arg = argv[i];
            if (arg == L"--warp")
                options.driver = BackendDriver::Software;
            else if (arg == L"--lifecycle")
                lifecycle = true;
            else if (arg == L"--model-materials-only")
                materialsOnly = true;
            else if (arg == L"--scene-passes")
                scenePasses = true;
            else {
                if (i + 1 >= argc)
                    return 2;
                const wchar_t* value = argv[++i];
                if (arg == L"--sampling") {
                    const std::wstring sampling = value;
                    if (sampling != L"native" && sampling != L"portable")
                        return 2;
                    options.materialSampling = sampling == L"native"
                                                   ? MaterialSamplingPolicy::Native
                                                   : MaterialSamplingPolicy::PortableTrilinear;
                } else if (arg == L"--self-shadow") {
                    selfShadow = true;
                    if (std::wstring(value) != L"track") {
                        shadowSettings.mode = int(number(value));
                        if (shadowSettings.mode != 1 && shadowSettings.mode != 2)
                            return 2;
                    }
                } else if (arg == L"--frames") {
                    frames = number(value);
                    if (!frames)
                        return 2;
                } else if (arg == L"--model-slot") {
                    selected = int(number(value));
                    if (selected > 254)
                        return 2;
                } else if (arg == L"--root")
                    resolver.AddSearchRoot(value);
                else if (arg == L"--save-as")
                    saveAs = value;
                else if (arg == L"--reselect") {
                    const auto slot = number(value);
                    if (slot > 254 || i + 1 >= argc)
                        return 2;
                    resolver.ReselectModel(std::uint8_t(slot), argv[++i]);
                } else if (arg == L"--reselect-accessory") {
                    const auto slot = number(value);
                    if (slot > 254 || i + 1 >= argc)
                        return 2;
                    resolver.ReselectAccessory(std::uint8_t(slot), argv[++i]);
                } else
                    return 2;
            }
        }
        SceneSession loaded;
        RuntimeCpuScene cpuScene;
        std::string reason;
        if (scenePasses && (materialsOnly || selected >= 0))
            throw std::runtime_error("scene_passes_requires_all_models_and_accessories");
        if (selfShadow && lifecycle)
            throw std::runtime_error("--lifecycle is unavailable with --self-shadow");
        const bool pmm = input.extension() == L".pmm" || input.extension() == L".PMM";
        if (pmm) {
            std::ifstream file(input, std::ios::binary);
            if (!file ||
                !SceneSession::Load(Read, &file, SceneResourceResolver::Resolve, &resolver, loaded))
                throw std::runtime_error("independent_scene_load_failed");
            // A model subset is explicit; this host does not claim accessory,
            // shadow or post-effect presentation until those passes migrate.
            if (selected < 0 && !materialsOnly && !scenePasses &&
                !loaded.Document().accessories.empty())
                throw std::runtime_error(
                    "independent_host_accessories_require_scene_passes_or_explicit_model_materials_scope");
            if (!cpuScene.Load(std::move(loaded), resolver.Resolved(), reason, selected))
                throw std::runtime_error(reason);
        } else if (!cpuScene.LoadModel(input, reason))
            throw std::runtime_error(reason);
        const auto& session = pmm ? *cpuScene.Session() : loaded;
        const auto sourceWidth = pmm ? session.Document().envelope.settings.renderWidth : 640;
        const auto sourceHeight = pmm ? session.Document().envelope.settings.renderHeight : 480;
        if (sourceWidth <= 0 || sourceHeight <= 0 || sourceWidth > 16384 || sourceHeight > 16384)
            throw std::runtime_error("invalid_cpu_scene_extent");
        CallbackAssetSource builtins([](AssetId id, std::vector<std::uint8_t>& bytes) {
            const auto module = GetModuleHandleW(nullptr);
            const auto resource = FindResourceW(module, MAKEINTRESOURCEW(unsigned(id)), L"PNG");
            if (!resource)
                return false;
            const auto length = SizeofResource(module, resource);
            const auto* p =
                static_cast<const std::uint8_t*>(LockResource(LoadResource(module, resource)));
            if (!p || !length)
                return false;
            bytes.assign(p, p + length);
            return true;
        });
        ToonTextureResolver textures(input.parent_path() / L"Toon", builtins);
        for (auto slot : cpuScene.Order())
            if (!cpuScene.Model(slot)->PrepareMaterials(textures, reason))
                throw std::runtime_error(reason);
        RuntimeCpuSceneProducer producer;
        if (!producer.Load(cpuScene, resolver, scenePasses, builtins, reason))
            throw std::runtime_error(reason);
        HostWindow window;
        if (!window.Open(scenePasses ? L"MikuDanceStudio independent RHI - scene passes"
                         : selected >= 0
                             ? L"MikuDanceStudio independent RHI - selected model materials"
                             : L"MikuDanceStudio independent RHI - model materials",
                         frames != 0))
            return 3;
        SwapchainDesc desc;
        desc.window = {WindowSystem::Win32, reinterpret_cast<std::uintptr_t>(window.handle)};
        RECT initialClient{};
        if (!GetClientRect(window.handle, &initialClient) || initialClient.right <= 0 ||
            initialClient.bottom <= 0)
            throw std::runtime_error("invalid_surface_extent");
        desc.width = unsigned(initialClient.right);
        desc.height = unsigned(initialClient.bottom);
        desc.present = PresentMode::Immediate;
        ScenePreviewController preview;
        if (!preview.Open(desc, options))
            throw std::runtime_error(preview.Reason());
        // Import completed before device creation; CPU owners retain no handles.
        std::cout << "cpu_import=PASS native_model_buffers=0 legacy_startup=bypassed models="
                  << cpuScene.Order().size() << " scope="
                  << (scenePasses     ? "fixed_scene_passes"
                      : selected >= 0 ? "selected_model_materials"
                                      : "model_materials")
                  << " adapter=" << preview.Adapter() << " sampling="
                  << (options.materialSampling == MaterialSamplingPolicy::Native ? "native"
                                                                                 : "portable")
                  << " self_shadow=" << selfShadow << "\n";
        bool playing = false;
        float cursorSeconds = cpuScene.Simulation().cursorSeconds;
        unsigned submitted = 0;
        std::size_t covered = 0;
        std::uint64_t staticTextures = 0, geometryUpdates = 0;
        bool expectStableTextures = false;
        auto previous = std::chrono::steady_clock::now();
        while (!window.closed && (!frames || submitted < frames)) {
            if (frames && !lifecycle && IsWindowVisible(window.handle))
                throw std::runtime_error("window_visible_in_bounded_preview");
            MSG message;
            while (PeekMessageW(&message, nullptr, 0, 0, PM_REMOVE)) {
                TranslateMessage(&message);
                DispatchMessageW(&message);
            }
            if (window.closed)
                break;
            if (window.replace) {
                window.replace = false;
                if (!preview.ReplaceDevice())
                    throw std::runtime_error(preview.Reason());
            }
            const auto now = std::chrono::steady_clock::now();
            const float delta =
                frames ? 1.f / 30 : std::chrono::duration<float>(now - previous).count();
            previous = now;
            std::map<std::uint8_t, std::uint64_t> versions;
            for (auto slot : cpuScene.Order()) {
                const auto* geometry = cpuScene.Model(slot)->Geometry();
                versions[slot] = geometry ? geometry->main.key.version : 0;
            }
            bool evaluated = false;
            if (pmm) {
                if (window.toggle) {
                    playing = !playing;
                    window.toggle = false;
                    if (playing) {
                        if (!cpuScene.StartPlayback(reason))
                            throw std::runtime_error(reason);
                    } else
                        cpuScene.Pause();
                }
                if (window.seekLeft || window.seekRight) {
                    const auto frame = session.Pose().frame;
                    playing = false;
                    if (!cpuScene.Seek(window.seekLeft && frame ? frame - 1
                                       : window.seekRight       ? frame + 1
                                                                : 0,
                                       reason))
                        throw std::runtime_error(reason);
                    cursorSeconds = cpuScene.Simulation().cursorSeconds;
                    window.seekLeft = window.seekRight = false;
                    evaluated = true;
                }
                if (frames) {
                    if (submitted == 0 || submitted == 2) {
                        if (!cpuScene.Seek(submitted, reason))
                            throw std::runtime_error(reason);
                        cursorSeconds = cpuScene.Simulation().cursorSeconds;
                    } else {
                        if (!cpuScene.StartPlayback(reason))
                            throw std::runtime_error(reason);
                        cursorSeconds += delta;
                        if (!cpuScene.Advance(cursorSeconds, delta, reason))
                            throw std::runtime_error(reason);
                    }
                    if (submitted == 3) {
                        if (!cpuScene.Edit(
                                [](PmmSceneDocument& scene) {
                                    scene.envelope.settings.editPanelWidth += 1;
                                    for (auto& model : scene.models) {
                                        if (!model.boneKeys.sparse.empty())
                                            model.boneKeys.sparse[0].value.position[0] += .125f;
                                        if (!model.displayKeys.dense[0].ikEnabled.empty())
                                            model.displayKeys.dense[0].ikEnabled[0] = 0;
                                    }
                                },
                                reason))
                            throw std::runtime_error(reason);
                        cursorSeconds = cpuScene.Simulation().cursorSeconds;
                    }
                    evaluated = true;
                } else if (playing) {
                    cursorSeconds += delta;
                    if (!cpuScene.Advance(cursorSeconds, delta, reason))
                        throw std::runtime_error(reason);
                    evaluated = true;
                }
            }
            if (!evaluated && !cpuScene.Tick(delta, reason))
                throw std::runtime_error(reason);
            ModelFrameInputs frame;
            frame.world = frame.lightRotation = Identity();
            frame.view.aspect = float(sourceWidth) / sourceHeight;
            frame.view.viewport = {0, 0, unsigned(sourceWidth), unsigned(sourceHeight), 0, 1};
            for (auto slot : cpuScene.Order()) {
                auto* model = cpuScene.Model(slot);
                geometryUpdates +=
                    model->Geometry() && model->Geometry()->main.key.version != versions[slot];
            }
            HistoryDesc target;
            target.width = sourceWidth;
            target.height = sourceHeight;
            ModelReplayStream stream;
            RECT client{};
            GetClientRect(window.handle, &client);
            RenderFrame multipass;
            if (selfShadow && !IsIconic(window.handle) && client.right > 0 && client.bottom > 0) {
                target.width = unsigned(client.right);
                target.height = unsigned(client.bottom);
                frame.view.aspect = float(target.width) / target.height;
                frame.view.viewport = {0, 0, target.width, target.height, 0, 1};
                if (!producer.BuildFrame(cpuScene, frame, target, shadowSettings, multipass,
                                         reason))
                    throw std::runtime_error(reason);
            } else if (!selfShadow && !producer.Build(cpuScene, frame, target, stream, reason))
                throw std::runtime_error(reason);
            if (!preview.Resize(IsIconic(window.handle) ? 0 : unsigned(client.right),
                                IsIconic(window.handle) ? 0 : unsigned(client.bottom)))
                throw std::runtime_error(preview.Reason());
            RgbaImage image;
            if (!(selfShadow ? preview.Draw(multipass, frames ? &image : nullptr)
                             : preview.Draw(stream, frames ? &image : nullptr)))
                throw std::runtime_error(preview.Reason());
            if (frames) {
                for (std::size_t p = 0; p < image.pixels.size(); p += 4)
                    covered += image.pixels[p] != 255 || image.pixels[p + 1] != 255 ||
                               image.pixels[p + 2] != 255;
            }
            const auto stats = preview.GetRenderer()->Stats();
            if (expectStableTextures && stats.textureUploads != staticTextures)
                throw std::runtime_error("unchanged_texture_reuploaded");
            staticTextures = stats.textureUploads;
            expectStableTextures = true;
            ++submitted;
            if (lifecycle && submitted == 2) {
                const auto cpuBefore = CpuStateFingerprint(cpuScene);
                if (!preview.Resize(704, 512) || !preview.Draw(stream))
                    throw std::runtime_error(preview.Reason());
                ShowWindow(window.handle, SW_MINIMIZE);
                if (!IsIconic(window.handle) || !preview.Resize(0, 0) || !preview.Draw(stream) ||
                    preview.State() != PreviewState::Suspended)
                    throw std::runtime_error("minimize_not_suspended");
                ShowWindow(window.handle, SW_RESTORE);
                ShowWindow(window.handle, SW_HIDE);
                RgbaImage before, after;
                if (!preview.Resize(640, 480) || !preview.Draw(stream, &before) ||
                    !preview.ReplaceDevice() || !preview.Draw(stream, &after))
                    throw std::runtime_error(preview.Reason());
                if (before.width != after.width || before.height != after.height ||
                    before.pixels != after.pixels)
                    throw std::runtime_error("device_replacement_image_mismatch");
                if (CpuStateFingerprint(cpuScene) != cpuBefore)
                    throw std::runtime_error("device_replacement_changed_cpu_world");
                staticTextures = preview.GetRenderer()->Stats().textureUploads;
            }
            if (!frames) {
                ShowWindow(window.handle, SW_SHOWNOACTIVATE);
                MsgWaitForMultipleObjects(0, nullptr, FALSE, 16, QS_ALLINPUT);
            }
        }
        const float finalCursor = cursorSeconds;
        if (!saveAs.empty()) {
            if (!pmm || std::filesystem::exists(saveAs))
                throw std::runtime_error("save_as_requires_new_pmm_path");
            if (!cpuScene.Edit(
                    [](PmmSceneDocument& scene) { scene.envelope.settings.editPanelWidth += 1; },
                    reason))
                throw std::runtime_error(reason);
            std::ostringstream out(std::ios::binary);
            if (!session.Save(Write, &out))
                return 4;
            std::istringstream in(out.str(), std::ios::binary);
            SceneSession reread;
            if (!SceneSession::Load(Read, &in, SceneResourceResolver::Resolve, &resolver, reread) ||
                reread.Document().envelope.settings.editPanelWidth !=
                    session.Document().envelope.settings.editPanelWidth ||
                !reread.Seek(session.Pose().frame))
                throw std::runtime_error("save_readback_mismatch");
            if (!SaveNew(saveAs, out.str()))
                throw std::runtime_error("save_new_file_failed");
        }
        if (frames && !covered)
            throw std::runtime_error("bounded_run_no_visible_pixels");
        if (!preview.Unload() || preview.GetRenderer()->Stats().residentResources ||
            !preview.Close())
            throw std::runtime_error("independent_shutdown_failed");
        std::cout << "PASS independent_rhi frames=" << submitted << " covered=" << covered
                  << " native_model_buffers=0 lifecycle=" << lifecycle
                  << " restored_image_exact=" << lifecycle
                  << " resident_after_unload=0 geometry_updates=" << geometryUpdates
                  << " shared_cpu_world=1 bodies=" << cpuScene.BodyCount()
                  << " physics_mode=" << cpuScene.Simulation().physicsMode
                  << " playback_cursor_seconds=" << finalCursor << "\n";
        return 0;
    } catch (const std::exception& e) {
        std::cerr << "independent_rhi: " << e.what() << "\n";
        return 1;
    }
}
}
#endif
