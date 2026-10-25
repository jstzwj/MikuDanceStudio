#define NOMINMAX
#include "scene_fixture.hpp"
#include "original_physics_module.hpp"
#include "../src/render/model_materials.hpp"

#include <algorithm>
#include <array>
#include <cstdint>

namespace {
template <class T> struct Com {
    T* p = nullptr;
    Com() = default;
    Com(const Com&) = delete;
    Com& operator=(const Com&) = delete;
    ~Com() {
        if (p)
            p->Release();
    }
    T** Out() {
        Check(p == nullptr, "COM output already occupied");
        return &p;
    }
    T* operator->() const { return p; }
};
void Hr(HRESULT result, const char* operation) {
    if (FAILED(result)) {
        std::fprintf(stderr, "%s: %08lX\n", operation, static_cast<unsigned long>(result));
        throw std::runtime_error(operation);
    }
}
constexpr UINT kSize = 512;
constexpr DWORD kBackground = 0xFFB0B8C0;
constexpr UINT kD3dxDefault = static_cast<UINT>(-1);
constexpr int kPngFormat = 3; // D3DXIMAGE_FILEFORMAT::D3DXIFF_PNG in d3dx9_32.
struct RenderTarget {
    IDirect3DDevice9* device;
    Com<IDirect3DSurface9> previous, previousDepth, color, depth, readback;
    explicit RenderTarget(IDirect3DDevice9* value) : device(value) {
        Hr(device->GetRenderTarget(0, previous.Out()), "previous target");
        device->GetDepthStencilSurface(previousDepth.Out());
        Hr(device->CreateRenderTarget(kSize, kSize, D3DFMT_A8R8G8B8, D3DMULTISAMPLE_NONE, 0, FALSE,
                                      color.Out(), nullptr),
           "image target");
        Hr(device->CreateDepthStencilSurface(kSize, kSize, D3DFMT_D24S8, D3DMULTISAMPLE_NONE, 0,
                                             TRUE, depth.Out(), nullptr),
           "image depth");
        Hr(device->CreateOffscreenPlainSurface(kSize, kSize, D3DFMT_A8R8G8B8, D3DPOOL_SYSTEMMEM,
                                               readback.Out(), nullptr),
           "image readback");
        Hr(device->SetRenderTarget(0, color.p), "bind image target");
        Hr(device->SetDepthStencilSurface(depth.p), "bind image depth");
        const D3DVIEWPORT9 viewport{0, 0, kSize, kSize, 0, 1};
        Hr(device->SetViewport(&viewport), "image viewport");
    }
    ~RenderTarget() {
        device->SetRenderTarget(0, previous.p);
        device->SetDepthStencilSurface(previousDepth.p);
    }
    std::vector<DWORD> Pixels(const std::filesystem::path& path = {}) {
        Hr(device->GetRenderTargetData(color.p, readback.p), "read GPU pixels");
        if (!path.empty())
            Hr(D3DXSaveSurfaceToFileW(path.c_str(), kPngFormat, readback.p, nullptr, nullptr),
               "save GPU image");
        D3DLOCKED_RECT lock{};
        Hr(readback->LockRect(&lock, nullptr, D3DLOCK_READONLY), "lock pixels");
        std::vector<DWORD> result(kSize * kSize);
        for (UINT y = 0; y < kSize; ++y)
            std::memcpy(result.data() + y * kSize,
                        static_cast<const char*>(lock.pBits) + y * lock.Pitch, kSize * 4);
        Hr(readback->UnlockRect(), "unlock pixels");
        return result;
    }
};
#if defined(_M_X64)
struct OriginalModel {
    OriginalPhysicsModule& module;
    std::unique_ptr<D3DRenderer> renderer = std::make_unique<D3DRenderer>();
    std::unique_ptr<mdl::ModelRecord> model = std::make_unique<mdl::ModelRecord>();
    PhysicsScene scene{};
    OriginalModel(OriginalPhysicsModule& binary, Fixture& port, const std::filesystem::path& path)
        : module(binary) {
        renderer->device = port.device;
        const auto locale = reinterpret_cast<_locale_t (*)(int, const char*)>(
            GetProcAddress(GetModuleHandleW(L"msvcr90.dll"), "_create_locale"));
        for (auto& value : renderer->localeTable)
            value = locale(LC_ALL, "Japanese_Japan.932");
        Check(
            module.Function<bool (*)(PhysicsScene*, D3DRenderer*)>(0x27B0)(&scene, renderer.get()),
            "original scene");
        module.Function<void (*)(mdl::ModelRecord*)>(0xA9610)(model.get());
        std::array<wchar_t, 32768> paths{};
        using Load = bool (*)(mdl::ModelRecord*, HWND, const wchar_t*, D3DRenderer*, unsigned char,
                              unsigned char, PhysicsScene*, void*);
        const auto cwd = std::filesystem::current_path();
        const bool loaded = module.Function<Load>(0xB2670)(
            model.get(), port.window, path.c_str(), renderer.get(), 0, 1, &scene, paths.data());
        std::filesystem::current_path(cwd);
        Check(loaded, "original model load");
    }
    ~OriginalModel() {
        module.Function<void (*)(mdl::ModelRecord*)>(0xA8D50)(model.get());
        module.Function<void (*)(PhysicsScene*)>(0x2580)(&scene);
        const auto freeLocale = reinterpret_cast<void (*)(_locale_t)>(
            GetProcAddress(GetModuleHandleW(L"msvcr90.dll"), "_free_locale"));
        for (auto value : renderer->localeTable)
            if (value)
                freeLocale(value);
    }
    void Pose() {
        module.Function<void (*)(mdl::ModelRecord*, std::uint32_t, int)>(0xCBD90)(model.get(), 0,
                                                                                  0);
        module.Function<void (*)(mdl::ModelRecord*)>(0xBD780)(model.get());
        mdl::ModelRecord* models[kModelSlotCount]{};
        models[0] = model.get();
        using PoseCall = void (*)(mdl::ModelRecord*, int, mdl::ModelRecord**, int);
        module.Function<PoseCall>(0xB9F70)(model.get(), 0, models, 0);
        module.Function<PoseCall>(0xB9F70)(model.get(), 1, models, 0);
    }
    void Skin(MMDApp& app, const D3DXMATRIX& world) {
        // IDA RVA C13C0 and caller 2A1D0: WORLD, camera XYZ, FOV, shadow byte.
        using SkinCall =
            void (*)(mdl::ModelRecord*, const float*, const float*, float, unsigned char);
        module.Function<SkinCall>(0xC13C0)(model.get(), &world._11, app.CameraPosition(),
                                           app.CameraFov(), app.state.selfShadowEnabled);
    }
    void Materials(MMDApp& app) {
        std::array<IDirect3DTexture9*, 11> toons{};
        for (int i = 0; i < 11; ++i)
            toons[i] = app.ToonTexture(i);
        using Draw =
            void (*)(mdl::ModelRecord*, D3DRenderer*, const D3DVECTOR*, IDirect3DTexture9**);
        module.Function<Draw>(0xB6D70)(model.get(), renderer.get(), &app.SceneLight().Direction,
                                       toons.data());
    }
    void Edges(bool projected) {
        module.Function<void (*)(mdl::ModelRecord*, D3DRenderer*, unsigned char)>(0xB82A0)(
            model.get(), renderer.get(), projected ? 1 : 0);
    }
};
#endif
std::uint64_t Digest(const std::vector<DWORD>& pixels) {
    std::uint64_t value = 14695981039346656037ull;
    for (DWORD pixel : pixels)
        for (int shift = 0; shift < 32; shift += 8) {
            value ^= (pixel >> shift) & 255;
            value *= 1099511628211ull;
        }
    return value;
}
struct Difference {
    unsigned pixels = 0, maximum = 0;
    Difference(const std::vector<DWORD>& a, const std::vector<DWORD>& b) {
        Check(a.size() == b.size(), "pixel count");
        for (std::size_t i = 0; i < a.size(); ++i) {
            if (a[i] != b[i])
                ++pixels;
            for (int shift = 0; shift < 32; shift += 8)
                maximum =
                    std::max(maximum, static_cast<unsigned>(std::abs(int((a[i] >> shift) & 255) -
                                                                     int((b[i] >> shift) & 255))));
        }
    }
};
std::vector<unsigned char> Buffer(IDirect3DVertexBuffer9* buffer) {
    D3DVERTEXBUFFER_DESC desc{};
    Hr(buffer->GetDesc(&desc), "vertex description");
    void* bits = nullptr;
    Hr(buffer->Lock(0, 0, &bits, D3DLOCK_READONLY), "vertex read");
    const auto* bytes = static_cast<const unsigned char*>(bits);
    std::vector<unsigned char> result(bytes, bytes + desc.Size);
    Hr(buffer->Unlock(), "vertex unlock");
    return result;
}

int Run(const std::filesystem::path& modelPath, const std::filesystem::path& original,
        const std::filesystem::path& output) {
    Fixture f;
    if (!f.ready)
        return 77;
    D3DCAPS9 caps{};
    Hr(f.device->GetDeviceCaps(&caps), "real device caps");
    if (caps.DeviceType == D3DDEVTYPE_NULLREF)
        return 77;
    D3DADAPTER_IDENTIFIER9 adapter{};
    Hr(f.d3d->GetAdapterIdentifier(0, 0, &adapter), "adapter identity");
    std::printf("GPU: %s; device type %u\n", adapter.Description, unsigned(caps.DeviceType));
    auto path = modelPath;
    if (path.empty()) {
        path = f.directory / L"render.pmd";
        WriteModel(path);
    }
    auto& model = f.LoadModelFile(path, 0, 0);
    std::printf("model: %u vertices, %u materials, %u bones, UV=%u\n", model.vertexCount,
                model.materialCount, model.boneCount, unsigned(model.pmxAdditionalUvCount));
    auto& app = *f.app;
    app.ResetSceneLight();
    app.CameraFov() = 30;
    app.state.selfShadowEnabled = 0;
    f.renderer->postProcessEnabled = 0; // Exercise the fixed-function material/edge path.
    f.renderer->shaderModelCaps = caps.MaxAnisotropy;
    app.SetSelectedModelSlot(0);
    SeekModelFrame(model, 0, 0);
    ModelApplyMorphs(&model);
    SetPhysicsMode(&model, 0, app.ModelSlots(), 0);
    SetPhysicsMode(&model, 1, app.ModelSlots(), 0);
    std::array<Com<IDirect3DTexture9>, 11> toons;
    for (int i = 0; i < 11; ++i) {
        char name[32]{};
        sprintf_s(name, "toon%02d.png", i);
        const auto toon = std::filesystem::path(MDS_TEST_SOURCE_DIR) / "res" / "assets" / name;
        Hr(D3DXCreateTextureFromFileExW(f.device, toon.c_str(), kD3dxDefault, kD3dxDefault, 1, 0,
                                        D3DFMT_UNKNOWN, D3DPOOL_MANAGED, kD3dxDefault, kD3dxDefault,
                                        0, nullptr, nullptr, toons[i].Out()),
           "load shared Toon texture");
        app.ToonTexture(i) = toons[i].p;
    }
#if defined(_M_X64)
    std::unique_ptr<OriginalPhysicsModule> binary;
    std::unique_ptr<OriginalModel> reference;
    if (!original.empty()) {
        binary = std::make_unique<OriginalPhysicsModule>(original);
        reference = std::make_unique<OriginalModel>(*binary, f, path);
        reference->Pose();
    }
#else
    Check(original.empty(), "x64 original requires x64 test");
#endif
    if (!output.empty())
        std::filesystem::create_directories(output);
    std::ofstream report;
    if (!output.empty()) {
        report.open(output / "report.json");
        Check(report.is_open(), "open image report");
        report << "{\"width\":512,\"height\":512,\"architecture_bits\":" << sizeof(void*) * 8
               << ",\"pipeline\":\"fixed_function\",\"frame\":0,\"physics\":false,\"original\":"
               << (original.empty() ? "false" : "true") << ",\"vertices\":" << model.vertexCount
               << ",\"materials\":" << model.materialCount << ",\"cases\":[\n";
    }
    unsigned failures = 0, sequence = 0;
    const bool pmx = model.physicsMode == 2;
    const D3DXVECTOR3 center = pmx ? D3DXVECTOR3(0, 10, 0) : D3DXVECTOR3(1, .5f, 0);
    const float distance = pmx ? 45.0f : 5.0f;
    D3DXMATRIX world;
    D3DXMatrixIdentity(&world);
    for (int view : {0, 1}) {
        const D3DXVECTOR3 eye(center.x + view * distance * .35f, center.y + view * distance * .1f,
                              -distance);
        const D3DXVECTOR3 up(0, 1, 0);
        D3DXMATRIX camera, projection;
        D3DXMatrixLookAtLH(&camera, &eye, &center, &up);
        D3DXMatrixPerspectiveFovLH(&projection, 3.14159265358979323846f / 6, 1, .1f, 1000);
        std::memcpy(app.CameraPosition(), &eye, sizeof eye);
        for (float scale : {0.0f, 1.0f, 2.0f}) {
            model.edgeScale = scale;
            UpdateModelVertexBuffers(&app, &model, &world._11);
#if defined(_M_X64)
            if (reference) {
                reference->model->edgeScale = scale;
                reference->Skin(app, world);
                const bool mainEqual =
                    Buffer(mdl::ResourceAs<IDirect3DVertexBuffer9>(model.vertexBuffer)) ==
                    Buffer(mdl::ResourceAs<IDirect3DVertexBuffer9>(reference->model->vertexBuffer));
                const bool edgeEqual =
                    Buffer(mdl::ResourceAs<IDirect3DVertexBuffer9>(model.vertexBuffer2)) ==
                    Buffer(
                        mdl::ResourceAs<IDirect3DVertexBuffer9>(reference->model->vertexBuffer2));
                std::printf("vertices view=%d edge=%.0f: main=%d edge=%d\n", view, scale, mainEqual,
                            edgeEqual);
                Check(mainEqual && edgeEqual, "original/port vertex buffers differ");
            }
#endif
            std::vector<DWORD> baseline;
            // Recreate default-pool surfaces across Reset; managed model/Toon buffers stay live.
            for (int pass = 0; pass < 3; ++pass) {
#if defined(_M_X64)
                const bool oracle = pass == 1 && reference != nullptr;
#else
                const bool oracle = false;
#endif
                if (pass == 1 && !oracle)
                    continue;
                if (pass == 2) {
                    D3DPRESENT_PARAMETERS pp{};
                    pp.Windowed = TRUE;
                    pp.SwapEffect = D3DSWAPEFFECT_DISCARD;
                    pp.hDeviceWindow = f.window;
                    pp.BackBufferWidth = pp.BackBufferHeight = 64;
                    pp.BackBufferFormat = D3DFMT_A8R8G8B8;
                    f.device->SetStreamSource(0, nullptr, 0, 0);
                    f.device->SetIndices(nullptr);
                    for (DWORD stage = 0; stage < 3; ++stage)
                        f.device->SetTexture(stage, nullptr);
                    Hr(f.device->Reset(&pp), "Reset with live model and textures");
                }
                RenderTarget target(f.device);
                InitRenderStates(&app);
                Hr(f.device->SetTransform(D3DTS_WORLD, &world), "world");
                Hr(f.device->SetTransform(D3DTS_VIEW, &camera), "view");
                Hr(f.device->SetTransform(D3DTS_PROJECTION, &projection), "projection");
                Hr(f.device->SetLight(0, &app.SceneLight()), "light");
                Hr(f.device->SetRenderState(D3DRS_CULLMODE, D3DCULL_CCW), "cull");
                Hr(f.device->SetRenderState(D3DRS_ALPHATESTENABLE, TRUE), "alpha test");
                Hr(f.device->SetRenderState(D3DRS_ALPHAFUNC, D3DCMP_GREATER), "alpha function");
                Hr(f.device->Clear(0, nullptr, D3DCLEAR_TARGET | D3DCLEAR_ZBUFFER, kBackground, 1,
                                   0),
                   "clear");
                Hr(f.device->BeginScene(), "begin image");
#if defined(_M_X64)
                if (oracle)
                    reference->Materials(app);
                else
#endif
                    DrawModelMaterials(&app, &model, false, false);
                if (scale > 0) {
                    Hr(f.device->SetRenderState(D3DRS_LIGHTING, FALSE), "edge lighting");
                    Hr(f.device->SetRenderState(D3DRS_CULLMODE, D3DCULL_CW), "edge cull");
                    Hr(f.device->SetTexture(0, nullptr), "edge texture");
#if defined(_M_X64)
                    if (oracle)
                        reference->Edges(false);
                    else
#endif
                        DrawModelEdgeGeometry(&app, &model, false);
                }
                Hr(f.device->EndScene(), "end image");
                const auto name = "view" + std::to_string(view) + "_edge" +
                                  std::to_string(int(scale)) +
                                  (oracle      ? "_original"
                                   : pass == 2 ? "_reset"
                                               : "_port");
                const auto pixels = target.Pixels(output.empty() ? std::filesystem::path()
                                                                 : output / (name + ".png"));
                const auto drawn = std::count_if(pixels.begin(), pixels.end(),
                                                 [](DWORD value) { return value != kBackground; });
                Check(drawn > 100, "rendered image must contain model pixels");
                if (pass == 0)
                    baseline = pixels;
                const Difference difference(baseline, pixels);
                failures += difference.pixels != 0;
                std::printf("%s: colored=%zu hash=%016llX different=%u max=%u\n", name.c_str(),
                            std::size_t(drawn), Digest(pixels), difference.pixels,
                            difference.maximum);
                if (report) {
                    report << (sequence++ ? ",\n" : "") << "{\"case\":\"" << name
                           << "\",\"drawn_pixels\":" << drawn
                           << ",\"different_pixels\":" << difference.pixels
                           << ",\"max_channel_error\":" << difference.maximum << "}";
                }
                std::fflush(stdout);
            }
        }
    }
    if (report)
        report << "\n],\"failures\":" << failures << "}\n";
    for (int i = 0; i < 11; ++i)
        app.ToonTexture(i) = nullptr;
    Check(dialogs == 0, "unexpected model/render dialog");
    return failures ? 1 : 0;
}
}
int wmain(int argc, wchar_t** argv) {
    SetEnvironmentVariableA("OMP_NUM_THREADS", "1");
    const HHOOK hook =
        SetWindowsHookExW(WH_CBT, CloseUnexpectedDialog, nullptr, GetCurrentThreadId());
    int result = 1;
    try {
        std::filesystem::path model, original, output;
        Check(argc % 2 == 1, "expected --model/--original/--output path pairs");
        for (int i = 1; i + 1 < argc; i += 2) {
            const std::wstring option(argv[i]);
            if (option == L"--model")
                model = std::filesystem::absolute(argv[i + 1]);
            else if (option == L"--original")
                original = std::filesystem::absolute(argv[i + 1]);
            else if (option == L"--output")
                output = std::filesystem::absolute(argv[i + 1]);
            else
                throw std::runtime_error("unknown render test option");
        }
        result = Run(model, original, output);
    } catch (const std::exception& error) {
        std::fprintf(stderr, "scene render: %s\n", error.what());
    }
    UnhookWindowsHookEx(hook);
    return result;
}
