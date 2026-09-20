#include "effect_engine.h"
#include "mme_context.h"
#include "mme_globals.h"
#include "pass_planner.h"
#include "sas_exec.h"

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
        mme::MmeEngineTerm();
        if (mme::g_effectPool) { mme::g_effectPool->Release(); mme::g_effectPool = nullptr; }
        if (mme::g_offscreenSurface) {
            mme::g_offscreenSurface->Release(); mme::g_offscreenSurface = nullptr;
        }
        delete mme::g_context;
        mme::g_context = nullptr;
    }
};
struct DrawStats {
    IDirect3DDevice9* device;
    int draws = 0;
    HRESULT firstFailure = S_OK;
    std::set<int> passes;
};
long DrawPass(void* opaque, mme::SasEffect* sas, int kind, int index) {
    auto& stats = *static_cast<DrawStats*>(opaque);
    if (kind != 1) {
        std::fprintf(stderr, "Unexpected Draw=Geometry in Ray screen script\n");
        stats.firstFailure = E_NOTIMPL;
        return E_NOTIMPL;
    }
    ++stats.draws;
    stats.passes.insert(index);
    const HRESULT hr = mme::SasDefaultDrawBufferPass(sas->effect, stats.device, index);
    if (FAILED(hr) && SUCCEEDED(stats.firstFailure)) stats.firstFailure = hr;
    std::printf("Buffer pass %d: 0x%08lX\n", index, static_cast<unsigned long>(hr));
    return hr;
}
}

// Runs the actual loader, SAS resource creation, step/resume interpreter and
// production fullscreen draw on a HAL device. It deliberately has no model,
// controller binding or DefaultEffect child scene; this is not an image A/B test.
int main(int argc, char** argv) {
    if (argc != 2) {
        std::fprintf(stderr, "Usage: mme_ray_smoke_test.exe <absolute ray.fx path>\n");
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
    DrawStats stats{device.p};
    mme::SasHostCallbacks callbacks = {};
    callbacks.ctx = &stats;
    callbacks.RunPass = DrawPass;
    mme::SasSetHostCallbacks(callbacks);
    REQUIRE_HR(device->SetRenderState(D3DRS_CULLMODE, D3DCULL_NONE));
    REQUIRE_HR(device->SetRenderState(D3DRS_FILLMODE, D3DFILL_SOLID));
    REQUIRE_HR(device->Clear(0, nullptr, D3DCLEAR_TARGET | D3DCLEAR_ZBUFFER | D3DCLEAR_STENCIL,
                             0xff000000, 1.0f, 0));
    Com<IDirect3DSurface9> originalTarget, originalDepth;
    REQUIRE_HR(device->GetRenderTarget(0, &originalTarget.p));
    REQUIRE_HR(device->GetDepthStencilSurface(&originalDepth.p));
    D3DVIEWPORT9 originalViewport = {};
    REQUIRE_HR(device->GetViewport(&originalViewport));
    for (const auto handle : sas->techniqueOrder) {
        const int index = mme::SasFindTechniqueIndex(sas, handle);
        if (index < 0) return 1;
        auto& technique = sas->techniques[static_cast<size_t>(index)];
        stats.passes.clear();
        const size_t logBefore = sas->log.size();
        REQUIRE_HR(device->BeginScene());
        auto* state = mme::SasCreateRunState(device.p);
        const bool suspended = mme::SasExecuteTechniqueStep(sas, &technique, device.p, state);
        hr = sas->runFailed ? E_FAIL : mme::SasResumeTechnique(sas, &technique, device.p, state);
        mme::SasDestroyRunState(state);
        REQUIRE_HR(device->EndScene());
        std::printf("Technique %s: suspended=%d, Buffer passes=%zu, hr=0x%08lX, runFailed=%d\n",
            technique.name.c_str(), suspended, stats.passes.size(), static_cast<unsigned long>(hr), sas->runFailed);
        std::fputs(sas->log.c_str() + logBefore, stdout);
        if (FAILED(hr) || sas->runFailed || FAILED(stats.firstFailure)) return 1;
        Com<IDirect3DSurface9> target, depth;
        REQUIRE_HR(device->GetRenderTarget(0, &target.p));
        REQUIRE_HR(device->GetDepthStencilSurface(&depth.p));
        D3DVIEWPORT9 viewport = {};
        REQUIRE_HR(device->GetViewport(&viewport));
        if (target.p != originalTarget.p || depth.p != originalDepth.p ||
            std::memcmp(&viewport, &originalViewport, sizeof(viewport))) {
            std::fprintf(stderr, "Script failed to restore main target/depth/viewport\n");
            return 1;
        }
    }
    if (!stats.draws) {
        std::fprintf(stderr, "No Buffer passes executed\n");
        return 1;
    }
    std::printf("Ray empty-scene HAL script smoke passed (%d Buffer draws); model/control/offscreen child rendering untested\n", stats.draws);
    return 0;
}
