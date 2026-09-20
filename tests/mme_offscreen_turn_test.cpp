#include "effect_engine.h"
#include "mme_context.h"
#include "mme_globals.h"
#include "pass_planner.h"
#include "sas_exec.h"
#include "material_bind.h"
#include "model_data.h"

#include <cstdio>
#include <cstring>
#include <set>

namespace {
template<class T> struct Com {
    T* p = nullptr;
    ~Com() { if (p) p->Release(); }
    T* operator->() const { return p; }
};
struct Window {
    HWND handle = CreateWindowW(L"STATIC", L"MME Ray script smoke test", WS_OVERLAPPED,
        0, 0, 128, 128, nullptr, nullptr, GetModuleHandleW(nullptr), nullptr);
    ~Window() { if (handle) DestroyWindow(handle); }
};
struct Runtime {
    explicit Runtime(IDirect3DDevice9* device) {
        mme::g_context = new mme::MmeContext(device);
        mme::MmeEngineInit(device, true);
    }
    ~Runtime() {
        mme::SasSetHostCallbacks({});
        delete mme::g_ownerManager;
        mme::g_ownerManager = nullptr;
        mme::MmeEngineTerm();
        if (mme::g_effectPool) { mme::g_effectPool->Release(); mme::g_effectPool = nullptr; }
        if (mme::g_offscreenSurface) {
            mme::g_offscreenSurface->Release(); mme::g_offscreenSurface = nullptr;
        }
        delete mme::g_context;
        mme::g_context = nullptr;
    }
};
}

// Production repeat-boundary and first-draw regression for a real Ray MaterialMap turn.
int main(int argc, char** argv) {
    if (argc != 2) {
        std::fprintf(stderr, "Usage: mme_offscreen_turn_test.exe <absolute ray.fx path>\n");
        return 2;
    }
    char path[32768] = {};
    const DWORD pathLength = GetFullPathNameA(argv[1], sizeof(path), path, nullptr);
    if (!pathLength || pathLength >= sizeof(path) ||
        GetFileAttributesA(path) == INVALID_FILE_ATTRIBUTES) {
        std::fprintf(stderr, "Effect path cannot be resolved\n");
        return 2;
    }
    Window window;
    Com<IDirect3D9> d3d;
    d3d.p = Direct3DCreate9(D3D_SDK_VERSION);
    if (!window.handle || !d3d.p) return 77;
    D3DPRESENT_PARAMETERS pp = {};
    pp.BackBufferWidth = pp.BackBufferHeight = 128;
    pp.BackBufferFormat = D3DFMT_A8R8G8B8;
    pp.BackBufferCount = 1;
    pp.SwapEffect = D3DSWAPEFFECT_DISCARD;
    pp.hDeviceWindow = window.handle;
    pp.Windowed = TRUE;
    pp.EnableAutoDepthStencil = TRUE;
    pp.AutoDepthStencilFormat = D3DFMT_D24S8;
    pp.PresentationInterval = D3DPRESENT_INTERVAL_IMMEDIATE;
    Com<IDirect3DDevice9> device;
    HRESULT hr = d3d->CreateDevice(D3DADAPTER_DEFAULT, D3DDEVTYPE_HAL, window.handle,
        D3DCREATE_HARDWARE_VERTEXPROCESSING, &pp, &device.p);
    if (FAILED(hr)) {
        std::fprintf(stderr, "HAL device unavailable: 0x%08lX\n", static_cast<unsigned long>(hr));
        return 77;
    }
#define REQUIRE_HR(call) do { const HRESULT result = (call); if (FAILED(result)) { \
    std::fprintf(stderr, "%s failed: 0x%08lX\n", #call, static_cast<unsigned long>(result)); return 1; } } while (0)
    Runtime runtime(device.p);
    std::printf("Compiling and parsing: %s\n", path);
    auto loaded = mme::MmeEngineLoadEffectFile(device.p, path);
    if (!loaded || !loaded->effect || !loaded->sas) {
        std::fprintf(stderr, "Ray load/parse failed:\n%s\n", loaded ? loaded->errorText.c_str() : "null entry");
        return 1;
    }
    auto* sas = loaded->sas;
    std::printf("Resources: %zu; selected techniques: %zu\n", sas->resources.size(), sas->techniqueOrder.size());
    std::fputs(mme::SasGetLog(sas), stdout);
    if (mme::SasHadErrors(sas) || sas->techniqueOrder.empty()) return 1;

    mme::g_ownerManager = new mme::EffectOwnerManager();
    auto* ctx = mme::g_context;
    auto* carrier = new mme::ModelData(device.p, 1, "ray.x", 0, 0, nullptr);
    carrier->setEffectFile(path);
    ctx->models.push_back(carrier);
    auto* binding = mme::MmeResolveModelEffectBinding(carrier);
    if (!binding || binding->sceneTechIndex < 0) {
        std::fprintf(stderr, "Ray carrier binding missing\n");
        return 1;
    }
    carrier->setRenderClass(2);
    mme::SasResource* material = nullptr;
    mme::SasResource* scene = nullptr;
    for (auto& resource : sas->resources) {
        if (resource.name == "MaterialMap") material = &resource;
        if (resource.name == "ScnMap") scene = &resource;
    }
    if (!material || !scene || material->defaultEffectMap.empty()) return 1;
    ctx->passPlanB.push_back(carrier);
    ctx->renderPassList.emplace_back(carrier, material);
    ctx->lastRepeatCount = 1;
    REQUIRE_HR(device->SetRenderState(D3DRS_MULTISAMPLEANTIALIAS, FALSE));
    REQUIRE_HR(device->BeginScene());
    // Exercise the actual repeat boundary; its private ApplyPassRecord stages
    // MaterialMap, depth and its DefaultEffect rows. With one selected turn,
    // lastRepeatCount == size avoids unrelated prior-turn resume work.
    mme::MmePassBookkeeping(ctx);
    Com<IDirect3DSurface9> before;
    REQUIRE_HR(device->GetRenderTarget(0, &before.p));
    const bool rowsBefore = ctx->offscreenDefaultEffect == &material->defaultEffectMap;
    std::printf("After repeat boundary: MaterialMap RT=%d, MaterialMap rows=%d\n",
        before.p == material->surface, rowsBefore);
    if (before.p != material->surface || !rowsBefore) return 1;
    // A declared resource must not bypass its explicit self=hide row.
    if (!mme::MmeOffscreenDefaultEffectHides(carrier) ||
        mme::MmeFindMaterialBinding(0, carrier, -1, true) != nullptr) {
        std::fprintf(stderr, "Carrier incorrectly inherited root binding despite self=hide\n");
        return 1;
    }
    auto* sky = new mme::ModelData(device.p, 2, "Time of day fast.pmx", 0, 0, nullptr);
    ctx->models.push_back(sky);
    auto* skyBinding = mme::MmeFindMaterialBinding(0, sky, -1, true);
    if (!skyBinding || skyBinding->effectPath.find("material_2.0.fx") == std::string::npos) {
        std::fprintf(stderr, "Sky filename failed to resolve MaterialMap *.pmx child effect\n");
        return 1;
    }
    auto* namedSkybox = new mme::ModelData(device.p, 3, "Sky with box.pmx", 0, 0, nullptr);
    ctx->models.push_back(namedSkybox);
    auto* namedSkyboxBinding = mme::MmeFindMaterialBinding(0, namedSkybox, -1, true);
    if (!namedSkyboxBinding || namedSkyboxBinding->effectPath.find("material_skybox.fx") == std::string::npos) {
        std::fprintf(stderr, "Named skybox failed to resolve its MaterialMap child effect\n");
        return 1;
    }
    std::printf("Sky child effects loaded: %s; %s\n",
        skyBinding->effectPath.c_str(), namedSkyboxBinding->effectPath.c_str());
    // main_default means this object's scene-0 assignment, including a
    // material override, rather than the global EMM default effect.
    const auto savedRows = material->defaultEffectMap;
    material->defaultEffectMap = {{"self", "main_default"}};
    if (mme::MmeFindMaterialBinding(0, carrier, -1, true) != binding) return 1;
    auto* subsetBinding = mme::MmeEnsureMaterialBinding(0, carrier, 3,
        loaded->effect, path, loaded);
    if (mme::MmeFindMaterialBinding(0, carrier, 3, true) != subsetBinding) return 1;
    material->defaultEffectMap = {{"self", "none"}};
    if (!mme::MmeHasOffscreenDefaultEffectRow(carrier) ||
        mme::MmeOffscreenDefaultEffectHides(carrier) ||
        mme::MmeFindMaterialBinding(0, carrier, -1, true) != nullptr) return 1;
    material->defaultEffectMap.clear();
    if (mme::MmeHasOffscreenDefaultEffectRow(carrier)) return 1;
    material->defaultEffectMap = savedRows;
    mme::MmeReportDrawError(ctx);
    Com<IDirect3DSurface9> after;
    REQUIRE_HR(device->GetRenderTarget(0, &after.p));
    const bool rowsAfter = ctx->offscreenDefaultEffect == &material->defaultEffectMap;
    std::printf("After first-draw choreography: MaterialMap RT=%d, ScnMap RT=%d, MaterialMap rows=%d, runFailed=%d\n",
        after.p == material->surface, after.p == scene->surface, rowsAfter, sas->runFailed);
    std::fputs(mme::SasGetLog(sas), stdout);
    REQUIRE_HR(device->EndScene());
    if (sas->runFailed) return 1;
    // This is the intended invariant, so the current bug produces a failing
    // regression rather than a test that blesses the broken implementation.
    if (after.p != material->surface || !rowsAfter) {
        std::fprintf(stderr, "Offscreen turn lost its render target or DefaultEffect rows before object drawing\n");
        return 1;
    }

    // The next repeat boundary finishes the preceding offscreen texture with
    // GenerateMipSubLevels. It must not execute ray's scene preamble again.
    // A sentinel in ScnMap makes that erroneous Clear=Color observable even
    // when ApplyPassRecord subsequently rebinds MaterialMap and hides it.
    Com<IDirect3DSurface9> sentinelReadback;
    D3DSURFACE_DESC sceneDesc = {};
    REQUIRE_HR(scene->surface->GetDesc(&sceneDesc));
    REQUIRE_HR(device->CreateOffscreenPlainSurface(sceneDesc.Width, sceneDesc.Height,
        sceneDesc.Format, D3DPOOL_SYSTEMMEM, &sentinelReadback.p, nullptr));
    REQUIRE_HR(device->SetRenderTarget(0, scene->surface));
    REQUIRE_HR(device->Clear(0, nullptr, D3DCLEAR_TARGET, 0xffff00ff, 1.0f, 0));
    REQUIRE_HR(device->GetRenderTargetData(scene->surface, sentinelReadback.p));
    unsigned char sentinel[8] = {};
    D3DLOCKED_RECT locked = {};
    REQUIRE_HR(sentinelReadback->LockRect(&locked, nullptr, D3DLOCK_READONLY));
    std::memcpy(sentinel, locked.pBits, sizeof(sentinel));
    REQUIRE_HR(sentinelReadback->UnlockRect());
    REQUIRE_HR(device->SetRenderTarget(0, material->surface));
    ctx->renderPassList.emplace_back(carrier, material);
    ctx->lastRepeatCount = 1;
    ctx->backgroundDrawnFlag = 1; // Previous turn's post-effects are complete.
    REQUIRE_HR(device->BeginScene());
    mme::MmePassBookkeeping(ctx);
    REQUIRE_HR(device->EndScene());
    REQUIRE_HR(device->GetRenderTargetData(scene->surface, sentinelReadback.p));
    REQUIRE_HR(sentinelReadback->LockRect(&locked, nullptr, D3DLOCK_READONLY));
    const bool sceneUntouched = std::memcmp(sentinel, locked.pBits, sizeof(sentinel)) == 0;
    REQUIRE_HR(sentinelReadback->UnlockRect());
    std::printf("Repeat-boundary texture finalization kept ScnMap unchanged: %d\n", sceneUntouched);
    if (!sceneUntouched) {
        std::fprintf(stderr, "Repeat boundary wrongly executed ray's scene Clear instead of texture mip generation\n");
        return 1;
    }
    std::puts("MME offscreen turn target/DefaultEffect regression passed");
    return 0;
}
