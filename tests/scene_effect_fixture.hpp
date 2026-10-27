#pragma once

#include "scene_render_fixture.hpp"
#include "../src/render/fx_slots.hpp"
#include "d3d9_device_fixture.hpp"
#include "render_numeric_trace.hpp"
#include "mikudancestudio/mme_bridge.hpp"
#include "mikudancestudio/menu_commands.h"
#include "mme_host_api.h"
#include "emm_manager.h"
#include "mme_context.h"
#include "mme_globals.h"
#include "material_bind.h"
#include "sas_exec.h"

#include <set>
#include <functional>

namespace {
struct StandardEffect {
    D3DRenderer& renderer;
    Com<ID3DXEffect> effect;
    Com<IDirect3DTexture9> white;
    StandardEffect(D3DRenderer& value, IDirect3DDevice9* device, int shaderModel)
        : renderer(value) {
        const auto path = std::filesystem::path(MDS_TEST_SOURCE_DIR) / "res" / "assets" /
                          (shaderModel == 3 ? "skin_effect_sm3.fx" : "skin_effect_sm2.fx");
        Com<ID3DXBuffer> errors;
        const HRESULT result = D3DXCreateEffectFromFileW(device, path.c_str(), nullptr, nullptr, 0,
                                                         nullptr, effect.Out(), errors.Out());
        if (FAILED(result) && errors.p)
            std::fprintf(stderr, "%.*s\n", int(errors->GetBufferSize()),
                         static_cast<const char*>(errors->GetBufferPointer()));
        Hr(result, "compile standard effect");
        renderer.effect = effect.p;
        renderer.postProcessEnabled = 1;
        renderer.shaderModel3 = 1;
        renderer.renderTargetWidth = renderer.renderTargetHeight = kSize;
        Hr(device->CreateTexture(1, 1, 1, 0, D3DFMT_A8R8G8B8, D3DPOOL_MANAGED, white.Out(),
                                 nullptr),
           "unshadowed texture");
        D3DLOCKED_RECT lock{};
        Hr(white->LockRect(0, &lock, nullptr, 0), "white lock");
        *static_cast<DWORD*>(lock.pBits) = 0xFFFFFFFF;
        Hr(white->UnlockRect(0), "white unlock");
        renderer.spriteTexture = white.p;
        if (shaderModel == 2)
            Hr(effect->SetInt("SKII1", 600), "SM2 R32F constant");
    }
    void ReleaseTargets() {
        for (auto** slot : {&renderer.shadowSurface, &renderer.shadowDepthSurface}) {
            if (*slot) {
                (*slot)->Release();
                *slot = nullptr;
            }
        }
        if (renderer.hdrTexture) {
            renderer.hdrTexture->Release();
            renderer.hdrTexture = nullptr;
        }
        renderer.backbufferSurface = renderer.depthStencilSurface = nullptr;
    }
    ~StandardEffect() {
        ReleaseTargets();
        renderer.effect = nullptr;
        renderer.spriteTexture = nullptr;
    }
};
// The oracle executes the original application ABI. Only this fixture writes
// its retired render workspaces; production uses AppRuntimeState accessors.
class OriginalRenderApp {
    std::unique_ptr<MMDAppState> state = std::make_unique<MMDAppState>();
    template <class T> void Put(std::size_t offset, const T& value) {
        Check(offset + sizeof value <= sizeof(*state), "original render field bounds");
        std::memcpy(reinterpret_cast<unsigned char*>(state.get()) + offset, &value, sizeof value);
    }

public:
    explicit OriginalRenderApp(MMDApp& source, OriginalModel& model) {
        *state = source.state;
        for (auto& slot : state->modelSlots)
            slot = nullptr;
        state->modelSlots[0] = source.ModelSlot(0) ? model.model.get() : nullptr;
        state->renderer = model.renderer.get();
        static_assert(offsetof(MMDAppState, reservedLightDirection) ==
                          (sizeof(void*) == 8 ? 0x9F040 : 0x9E174),
                      "original PMM light direction");
        std::memcpy(state->reservedLightDirection, source.LightDirection(), 12);
        // The raw PMM direction ends at 9F04C; D3DLIGHT9::Direction is +64.
        static_assert(offsetof(D3DLIGHT9, Direction) == 64, "D3D9 light ABI");
#if defined(_M_X64)
        Put(0x9F04C, source.SceneLight());
        Put(0xA1158, source.LightViewProjection());
        Put(0xA1198, source.WorldViewProjection());
        Put(0xA1680, source.ViewRotationTransform());
        Put(0xA11D8, DWORD(0));
        Put(0xA11DC, 0);
        Put(0xA11E0, 1);
#else
        Put(0x9E180, source.SceneLight());
        Put(0xA01E8, source.LightViewProjection());
        Put(0xA0228, source.WorldViewProjection());
        Put(0xA0674, source.ViewRotationTransform());
#endif
    }
    MMDAppState* State() { return state.get(); }
    void Shadow(OriginalModel& model, const D3DXMATRIX& view, const D3DXMATRIX& world) {
#if defined(_M_X64)
        using Call = void (*)(MMDAppState*, const D3DXMATRIX*, const D3DXMATRIX*);
        model.module.Function<Call>(0xA1030)(state.get(), &view, &world);
#else
        using Call = void(__thiscall*)(MMDAppState*, D3DMATRIX, D3DMATRIX);
        model.module.Function<Call>(0x26CD0)(state.get(), view, world);
#endif
    }
    void Frame(OriginalModel& model, const D3DXMATRIX& world) {
#if defined(_M_X64)
        model.module.Function<void (*)(MMDAppState*, const D3DXMATRIX*)>(0xA1E60)(state.get(),
                                                                                  &world);
#else
        model.module.Function<void(__thiscall*)(MMDAppState*, D3DMATRIX)>(0x277E0)(state.get(),
                                                                                   world);
#endif
    }
    D3DMATRIX LightMatrix() const {
        D3DMATRIX matrix;
        std::memcpy(&matrix,
                    reinterpret_cast<const unsigned char*>(state.get()) +
                        (sizeof(void*) == 8 ? 0xA1158 : 0xA01E8),
                    sizeof matrix);
        return matrix;
    }
};
std::vector<DWORD> ShadowPixels(IDirect3DDevice9* device, IDirect3DSurface9* surface,
                                const std::filesystem::path& output) {
    Check(surface != nullptr, "shadow surface created");
    Com<IDirect3DSurface9> copy;
    Hr(device->CreateOffscreenPlainSurface(kSize, kSize, D3DFMT_R32F, D3DPOOL_SYSTEMMEM, copy.Out(),
                                           nullptr),
       "shadow readback surface");
    Hr(device->GetRenderTargetData(surface, copy.p), "read shadow map");
    if (!output.empty())
        Hr(D3DXSaveSurfaceToFileW(output.c_str(), kPngFormat, copy.p, nullptr, nullptr),
           "save shadow map");
    D3DLOCKED_RECT lock{};
    Hr(copy->LockRect(&lock, nullptr, D3DLOCK_READONLY), "lock shadow map");
    std::vector<DWORD> result(kSize * kSize);
    for (UINT y = 0; y < kSize; ++y)
        std::memcpy(result.data() + kSize * y,
                    static_cast<const char*>(lock.pBits) + lock.Pitch * y, kSize * 4);
    Hr(copy->UnlockRect(), "unlock shadow map");
    return result;
}
struct Image {
    std::vector<DWORD> color, shadow;
    D3DMATRIX light{};
    std::vector<std::string> constants;
    std::vector<std::array<DWORD, 65>> states;
};
struct ShadowDifference {
    unsigned pixels = 0;
    DWORD maximumUlp = 0;
    ShadowDifference(const std::vector<DWORD>& a, const std::vector<DWORD>& b) {
        Check(a.size() == b.size(), "shadow texel count");
        const auto ordered = [](DWORD bits) {
            return bits & 0x80000000 ? ~bits : bits ^ 0x80000000;
        };
        for (std::size_t i = 0; i < a.size(); ++i) {
            pixels += a[i] != b[i];
            const auto x = ordered(a[i]), y = ordered(b[i]);
            maximumUlp = std::max(maximumUlp, x > y ? x - y : y - x);
        }
    }
};
void SameImage(const Image& a, const Image& b, const char* message) {
    Check(a.color == b.color && a.shadow == b.shadow &&
              std::memcmp(&a.light, &b.light, sizeof a.light) == 0,
          message);
}
Image Draw(Fixture& f, StandardEffect& effect, const D3DXMATRIX& camera,
           const D3DXMATRIX& projection, const D3DXMATRIX& world,
           const std::filesystem::path& output, const std::string& name,
           OriginalModel* original = nullptr, UINT dimension = kSize,
           RenderNumericTrace* trace = nullptr,
           const std::function<void(MMDAppState*)>& bind = {}) {
    if (trace)
        trace->Begin();
    auto& app = *f.app;
    auto& r = effect.renderer;
    RenderTarget target(f.device, dimension);
    r.backbufferSurface = target.color.p;
    r.depthStencilSurface = target.depth.p;
    // Prepare the frame counter before copying/binding the original host.
    // In x86 it lives in the copied state; setting it afterwards skips Frame.
    app.state.renderPassCount = 1;
    std::unique_ptr<OriginalRenderApp> originalState;
    if (original && bind) {
        originalState = std::make_unique<OriginalRenderApp>(app, *original);
        bind(originalState->State());
    }
    struct RestoreBinding {
        const std::function<void(MMDAppState*)>& bind;
        ~RestoreBinding() {
            if (bind)
                bind(nullptr);
        }
    } restoreBinding{bind};
    InitRenderStates(&app);
    Hr(f.device->SetVertexShader(nullptr), "clear vertex shader");
    Hr(f.device->SetPixelShader(nullptr), "clear pixel shader");
    Hr(f.device->SetTransform(D3DTS_WORLD, &world), "world");
    Hr(f.device->SetTransform(D3DTS_VIEW, &camera), "camera");
    Hr(f.device->SetTransform(D3DTS_PROJECTION, &projection), "projection");
    Hr(f.device->SetLight(0, &app.SceneLight()), "scene light");
    Hr(f.device->SetRenderState(D3DRS_CULLMODE, D3DCULL_CCW), "cull");
    Hr(f.device->Clear(0, nullptr, D3DCLEAR_TARGET | D3DCLEAR_ZBUFFER, kBackground, 1, 0),
       "clear frame");
    Hr(mikudancestudio::mme::BeginScene(&app, f.device), "effect begin scene");
    // Each sample owns a fresh render target. Register its clear after MME has
    // identified it at BeginScene, rather than inheriting a previous target.
    Hr(mikudancestudio::mme::ClearScene(&app, f.device, D3DCLEAR_TARGET | D3DCLEAR_ZBUFFER,
                                        kBackground, 1, 0),
       "tracked frame clear");
    Image image;
    if (original) {
        if (!originalState)
            originalState = std::make_unique<OriginalRenderApp>(app, *original);
        originalState->Shadow(*original, camera, world);
        image.light = originalState->LightMatrix();
        // The native entry draws one turn; the original frame driver owns
        // the repeat loop and decrements the host counter before each call.
        auto* repeat =
            reinterpret_cast<int*>(reinterpret_cast<unsigned char*>(originalState->State()) +
                                   (sizeof(void*) == 8 ? 0xA11E0 : 0xA0270));
        unsigned turns = 0;
        while (*repeat > 0) {
            Check(++turns < 32, "native MME render pass convergence");
            --*repeat;
            originalState->Frame(*original, world);
        }
        Check(turns > 0, "native frame driver must execute at least one turn");
    } else {
        RenderShadowMap(&app, &world._11);
        image.light = app.LightViewProjection();
        unsigned passes = 0;
        while (app.state.renderPassCount > 0) {
            Check(++passes < 32, "MME render pass convergence");
            --app.state.renderPassCount;
            RenderModelsEffect(&app, &world._11);
        }
    }
    Hr(mikudancestudio::mme::EndScene(&app, f.device), "effect end scene");
    image.color =
        target.Pixels(output.empty() ? std::filesystem::path() : output / (name + ".png"));
    image.shadow =
        ShadowPixels(f.device, r.shadowSurface,
                     output.empty() ? std::filesystem::path() : output / (name + "_shadow.png"));
    for (DWORD bits : image.shadow) {
        float depth;
        std::memcpy(&depth, &bits, sizeof depth);
        Check(std::isfinite(depth), "non-finite shadow depth");
    }
    if (!output.empty()) {
        SaveRenderBytes(output, name + ".light", &image.light, sizeof image.light);
        std::ofstream raw(output / (name + "_shadow.r32f"), std::ios::binary);
        raw.write(reinterpret_cast<const char*>(image.shadow.data()),
                  image.shadow.size() * sizeof(DWORD));
        Check(raw.good(), "write raw shadow map");
    }
    float place[4]{};
    Hr(effect.effect->GetFloatArray("Place", place, 4), "read camera reference parameter");
    Check(place[3] == 1.0f, "camera reference must retain exact homogeneous W");
    if (trace) {
        image.constants = trace->constants;
        image.states = trace->states;
        trace->Save(output, name);
    }
    const auto drawn = std::count_if(image.color.begin(), image.color.end(),
                                     [](DWORD p) { return p != kBackground; });
    Check(drawn > 100, "effect frame must contain model pixels");
    std::printf("%s: color=%016llX shadow=%016llX drawn=%zu\n", name.c_str(), Digest(image.color),
                Digest(image.shadow), std::size_t(drawn));
    r.backbufferSurface = r.depthStencilSurface = nullptr;
    std::fflush(stdout);
    return image;
}

// Only the driver's loss notifications are injected. Reset, resources, effects,
// all draws and readback still execute on the real HAL device.
struct RecoveryDevice : SnapshotDeviceFixture {
    MMDApp& app;
    unsigned presents = 0, polls = 0, resets = 0;
    bool injectLoss = false;
    HRESULT resetResult = E_PENDING;
    RecoveryDevice(Fixture& f) : SnapshotDeviceFixture(f.device), app(*f.app) {}
    HRESULT STDMETHODCALLTYPE Present(const RECT* source, const RECT* destination, HWND window,
                                      const RGNDATA* dirty) override {
        ++presents;
        return injectLoss ? D3DERR_DEVICELOST : real->Present(source, destination, window, dirty);
    }
    HRESULT STDMETHODCALLTYPE TestCooperativeLevel() override {
        ++polls;
        return polls < 3 ? D3DERR_DEVICELOST : D3DERR_DEVICENOTRESET;
    }
    HRESULT STDMETHODCALLTYPE Reset(D3DPRESENT_PARAMETERS* parameters) override {
        ++resets;
        auto* r = app.Renderer();
        Check(!app.CaptureTexture() && !app.AviBackgroundTexture() && !app.AviBackgroundSurface() &&
                  !app.CaptureRenderTarget() && !app.CaptureSystemSurface() && !r->captureSurface &&
                  !r->backbufferSurface && !r->depthStencilSurface && !r->shadowSurface &&
                  !r->hdrTexture && !r->shadowDepthSurface,
              "resources released before Reset");
        resetResult = real->Reset(parameters);
        return resetResult;
    }
};

struct CompositeScene {
    MMDApp& app;
    explicit CompositeScene(Fixture& f) : app(*f.app) {
        Check(MakeLineGeometry(&app), "ground geometry");
        const auto path = f.directory / "render.x";
        // .x FLOAT tokens require decimal points; integer tokens are raw DWORDs.
        std::ofstream(path) << "xof 0303txt 0032\nMesh {3;0.0;0.0;0.0;,1.0;0.0;0.0;,0.0;1.0;0.0;;"
                               "2;3;0,2,1;,3;0,1,2;;"
                               "MeshNormals {1;0.0;0.0;-1.0;;2;3;0,0,0;,3;0,0,0;;}"
                               "MeshTextureCoords {3;0.0;0.0;,1.0;0.0;,0.0;1.0;;}"
                               "MeshMaterialList {1;2;0,0;; Material {1.0;0.3;0.1;1.0;;"
                               " 4.0; 0.0;0.0;0.0;; 0.1;0.1;0.1;;}}}\n";
        for (int i = 0; i < 2; ++i) {
            const auto objectPath = f.directory / (i ? "second.x" : "first.x");
            std::filesystem::copy_file(path, objectPath);
            auto* object =
                static_cast<mdl::AccessoryRecord*>(::operator new(sizeof(mdl::AccessoryRecord)));
            std::memset(object, 0, sizeof(*object));
            InitAccessoryRecord(object);
            app.AccessorySlot(i) = object;
            const auto previousDirectory = std::filesystem::current_path();
            const bool loaded = LoadAccessoryObject(&app, object, objectPath.c_str());
            std::filesystem::current_path(previousDirectory);
            Check(loaded, "render accessory");
            Check(object->materialCount == 1, "accessory draw material");
            Check(static_cast<D3DMATERIAL9*>(object->materials)->Diffuse.a == 1.0f,
                  "accessory material parsed as floats");
            object->order = static_cast<std::uint8_t>(i);
            object->position[0] = i ? .75f : -.25f;
            object->position[1] = .25f;
            object->position[2] = -1;
            object->scale = .125f;
            object->opacity = i ? .4f : 1;
            object->visible = object->shadowEnabled = 1;
        }
        app.AccessoryRenderSplitOrder() = 1;
        app.GroundGridEnabled() = app.GroundShadowEnabled() = 1;
    }
    ~CompositeScene() {
        for (auto** slot : {&app.GroundGridVertices(), &app.GroundPlaneVertices()}) {
            if (*slot)
                (*slot)->Release();
            *slot = nullptr;
        }
        if (app.GroundGridIndices())
            app.GroundGridIndices()->Release();
        app.GroundGridIndices() = nullptr;
    }
};

void SeedResetResources(Fixture& f) {
    auto& app = *f.app;
    auto* r = f.renderer.get();
    for (auto** slot : {&app.CaptureTexture(), &app.AviBackgroundTexture()})
        Hr(f.device->CreateTexture(64, 64, 1, D3DUSAGE_RENDERTARGET, D3DFMT_A8R8G8B8,
                                   D3DPOOL_DEFAULT, slot, nullptr),
           "reset texture");
    Hr(app.AviBackgroundTexture()->GetSurfaceLevel(0, &app.AviBackgroundSurface()),
       "background level");
    for (auto** slot : {&app.CaptureRenderTarget(), &r->captureSurface})
        Hr(f.device->CreateRenderTarget(64, 64, D3DFMT_A8R8G8B8, D3DMULTISAMPLE_NONE, 0, FALSE,
                                        slot, nullptr),
           "reset surface");
    Hr(f.device->CreateOffscreenPlainSurface(64, 64, D3DFMT_A8R8G8B8, D3DPOOL_SYSTEMMEM,
                                             &app.CaptureSystemSurface(), nullptr),
       "reset readback");
    Hr(f.device->GetRenderTarget(0, &r->backbufferSurface), "reset backbuffer reference");
    Hr(f.device->GetDepthStencilSurface(&r->depthStencilSurface), "reset depth reference");
    for (int i = 0; i < 3; ++i)
        strcpy_s(app.RecentFile(i), 256, "reset-marker");
}

std::filesystem::path WriteOffscreenEffect(Fixture& f, bool relative = false) {
    const auto path = f.directory / (relative ? "relative.fx" : "offscreen.fx");
    std::ofstream file(path);
    file << R"FX(
float4x4 WVP : WORLDVIEWPROJECTION;
texture SceneColor : OFFSCREENRENDERTARGET <
)FX";
    file << (relative ? "float2 ViewportRatio={0.5,0.5};" : "int Width=64; int Height=64;");
    file << R"FX(
 string Format="A8R8G8B8";
 float4 ClearColor={0.1,0.2,0.3,1}; float ClearDepth=1;
 string DefaultEffect="*=offscreen_child.fx;";
>;
float4 VS(float4 position : POSITION) : POSITION { return mul(position,WVP); }
float4 PS() : COLOR { return float4(1,0.25,0.125,1); }
technique Main < string MMDPass="object"; > {
 pass P { CullMode=None; VertexShader=compile vs_2_0 VS(); PixelShader=compile ps_2_0 PS(); }
}
technique Shadowed < string MMDPass="object_ss"; > {
 pass P { CullMode=None; VertexShader=compile vs_2_0 VS(); PixelShader=compile ps_2_0 PS(); }
}
)FX";
    file.close();
    Check(file.good(), "write offscreen fixture");
    std::ofstream child(f.directory / "offscreen_child.fx");
    child << R"FX(
float4x4 WVP : WORLDVIEWPROJECTION;
float4 VS(float4 position : POSITION) : POSITION { return mul(position,WVP); }
float4 PS() : COLOR { return float4(0.125,1,0.25,1); }
technique Main < string MMDPass="object"; > {
 pass P { CullMode=None; VertexShader=compile vs_2_0 VS(); PixelShader=compile ps_2_0 PS(); }
}
technique Shadowed < string MMDPass="object_ss"; > {
 pass P { CullMode=None; VertexShader=compile vs_2_0 VS(); PixelShader=compile ps_2_0 PS(); }
}
)FX";
    child.close();
    Check(child.good(), "write child effect");
    return path;
}
void AssignOffscreenEffect(Fixture& f) {
    const auto path = WriteOffscreenEffect(f);
    const auto id = reinterpret_cast<std::uintptr_t>(f.app->ModelSlot(0));
    Check(::mme::MmeAssignEffect(id, -1, path.string()), "assign offscreen effect through EMM");
}

std::vector<DWORD> CheckOffscreen(Fixture& f) {
    auto* model =
        ::mme::MmeFindOrCreateModelEntry(reinterpret_cast<std::uintptr_t>(f.app->ModelSlot(0)));
    auto* binding = ::mme::MmeActiveModelBinding(model);
    Check(binding && binding->sas && !::mme::SasHadErrors(binding->sas), "offscreen binding");
    unsigned targets = 0;
    std::vector<DWORD> pixels;
    for (auto& resource : binding->sas->resources) {
        if (resource.name != "SceneColor")
            continue;
        ++targets;
        Check(resource.surface != nullptr, "offscreen target exists");
        Com<IDirect3DSurface9> copy;
        Hr(f.device->CreateOffscreenPlainSurface(64, 64, D3DFMT_A8R8G8B8, D3DPOOL_SYSTEMMEM,
                                                 copy.Out(), nullptr),
           "offscreen readback");
        Hr(f.device->GetRenderTargetData(resource.surface, copy.p), "offscreen copy");
        D3DLOCKED_RECT lock{};
        Hr(copy->LockRect(&lock, nullptr, D3DLOCK_READONLY), "offscreen lock");
        std::set<DWORD> colors;
        pixels.resize(64 * 64);
        for (int y = 0; y < 64; ++y)
            for (int x = 0; x < 64; ++x)
                colors.insert(pixels[y * 64 + x] = reinterpret_cast<const DWORD*>(
                                  static_cast<const char*>(lock.pBits) + y * lock.Pitch)[x]);
        Hr(copy->UnlockRect(), "offscreen unlock");
        Check(colors.size() > 1, "offscreen must contain rendered scene geometry");
    }
    Check(targets == 1, "one declared offscreen target");
    return pixels;
}

void VerifyReset(Fixture& f, StandardEffect& effect, const D3DXMATRIX& view,
                 const D3DXMATRIX& projection, const D3DXMATRIX& world,
                 const std::filesystem::path& output, const std::string& prefix = "reset",
                 bool requireIdentical = true) {
    auto& app = *f.app;
    auto& r = *f.renderer;
    const HMENU previousMenu = GetMenu(f.window);
    const HMENU menu = previousMenu ? previousMenu : CreateMenu();
    Check(menu &&
              AppendMenuW(menu, MF_STRING | MF_CHECKED, MDS_CMD_TOGGLE_ANTIALIASING,
                          L"Anti-aliasing") &&
              SetMenu(f.window, menu),
          "reset menu fixture");
    struct MenuLifetime {
        HWND window;
        HMENU menu;
        bool owned;
        ~MenuLifetime() {
            DeleteMenu(menu, MDS_CMD_TOGGLE_ANTIALIASING, MF_BYCOMMAND);
            if (owned) {
                SetMenu(window, nullptr);
                DestroyMenu(menu);
            }
        }
    } menuLifetime{f.window, menu, previousMenu == nullptr};
    RecoveryDevice device(f);
    r.device = &device;
    struct RestoreDevice {
        Fixture& f;
        ~RestoreDevice() { f.renderer->device = f.device; }
    } restore{f};
    // A window-sized backbuffer is required by the production viewport refresh.
    auto& pp = r.presentParameters;
    pp = {};
    pp.Windowed = TRUE;
    pp.SwapEffect = D3DSWAPEFFECT_DISCARD;
    pp.hDeviceWindow = f.window;
    pp.BackBufferWidth = pp.BackBufferHeight = 900;
    pp.BackBufferFormat = D3DFMT_A8R8G8B8;
    pp.EnableAutoDepthStencil = TRUE;
    pp.AutoDepthStencilFormat = D3DFMT_D24S8;
    PostDeviceReset(&app);
    Hr(device.resetResult, "initial production Reset");
    DWORD antialias = 0;
    Hr(f.device->GetRenderState(D3DRS_MULTISAMPLEANTIALIAS, &antialias), "antialias state");
    Check(antialias == TRUE, "Reset restores checked antialiasing menu state");
    const auto before = Draw(f, effect, view, projection, world, output, prefix + "_before");
    SeedResetResources(f);
    CheckMenuItem(menu, MDS_CMD_TOGGLE_ANTIALIASING, MF_BYCOMMAND | MF_UNCHECKED);
    PostDeviceReset(&app);
    Hr(device.resetResult, "production Reset with live resources");
    Hr(f.device->GetRenderState(D3DRS_MULTISAMPLEANTIALIAS, &antialias), "antialias disabled");
    Check(antialias == FALSE, "Reset restores unchecked antialiasing menu state");
    for (int i = 0; i < 3; ++i)
        Check(app.RecentFile(i)[0] == 0, "reset clears three recent paths");
    Check(r.shadowSurface == nullptr && r.hdrTexture == nullptr, "shadow targets recreated lazily");
    const auto after = Draw(f, effect, view, projection, world, output, prefix + "_after");
    if (requireIdentical)
        SameImage(before, after, "production Reset changed the effect frame");
    // Enter the real frame driver's Present -> cooperative-level wait -> Reset path.
    // Keep simulation stopped so this checks recovery independently of elapsed time.
    app.PlaybackActive() = 0;
    app.CameraMode() = 1;
    app.state.accessoryEditDialogOpen = 1;
    app.state.frameVolumeControlEnabled = 0;
    app.state.messageSeen = 1;
    device.injectLoss = true;
    const auto resets = device.resets;
    FrameDriver(&app);
    Check(device.presents == 1 && device.polls == 3 && device.resets == resets + 1,
          "frame driver recovery sequence");
    Hr(device.resetResult, "frame driver real Reset");
    app.PlaybackActive() = 1;
    const auto recovered = Draw(f, effect, view, projection, world, output, prefix + "_recovered");
    if (requireIdentical)
        SameImage(before, recovered, "frame driver recovery changed the effect frame");
    std::puts(requireIdentical
                  ? "production Reset and injected Present loss recovered identical GPU pixels"
                  : "captured external effect Reset and Present loss for native comparison");
}

} // namespace
