// Real D3D surfaces through the production snapshot and lifetime entry points.
// The forwarding device injects only creation results; the host edit-mode
// query is controlled so the real OnEndScene callback can cover both modes.
#include <windows.h>
#include <cstdio>
#include <stdexcept>
#include "d3d9_device_fixture.hpp"
static BOOL fixtureEditMode = TRUE;
extern "C" BOOL __cdecl SnapshotFixtureIsEditMode() { return fixtureEditMode; }
#define IsEditMode SnapshotFixtureIsEditMode
#include "../third_party/mmeffect/src/mmeffect/callbacks.cpp"
#undef IsEditMode

namespace {
using Surface = mme::MmeSnapshotCache::Surface;
void Check(bool ok, const char* message) {
    if (!ok) throw std::runtime_error(message);
}
void Hr(HRESULT hr, const char* message) { Check(SUCCEEDED(hr), message); }
struct Window {
    HWND handle = CreateWindowW(L"STATIC", L"Snapshot cache fixture", WS_OVERLAPPED,
        0, 0, 128, 128, nullptr, nullptr, GetModuleHandleW(nullptr), nullptr);
    ~Window() { if (handle) DestroyWindow(handle); }
};
template<class T> struct Com {
    T* p = nullptr;
    ~Com() { if (p) p->Release(); }
    T* operator->() const { return p; }
};
Surface Color(IDirect3DDevice9* device, D3DFORMAT format) {
    IDirect3DSurface9* surface = nullptr;
    Hr(device->CreateRenderTarget(128, 128, format, D3DMULTISAMPLE_NONE, 0,
        FALSE, &surface, nullptr), "create fixture color");
    return Surface(surface);
}
Surface Depth(IDirect3DDevice9* device, D3DFORMAT format) {
    IDirect3DSurface9* surface = nullptr;
    Hr(device->CreateDepthStencilSurface(128, 128, format, D3DMULTISAMPLE_NONE,
        0, FALSE, &surface, nullptr), "create fixture depth");
    return Surface(surface);
}
struct Output { Surface color0, color1, depth; };
Output Bound(IDirect3DDevice9* device) {
    IDirect3DSurface9 *c0 = nullptr, *c1 = nullptr, *d = nullptr;
    Hr(device->GetRenderTarget(0, &c0), "read output color0");
    Hr(device->GetRenderTarget(1, &c1), "read output color1");
    Hr(device->GetDepthStencilSurface(&d), "read output depth");
    return {Surface(c0), Surface(c1), Surface(d)};
}
bool Empty(const mme::MmeSnapshotCache& cache) {
    for (const auto& slot : cache.colors)
        if (!slot.exactSize.empty() || !slot.atLeastSize.empty()) return false;
    return cache.depth.exactSize.empty() && cache.depth.atLeastSize.empty();
}
void Run(IDirect3DDevice9* real, D3DPRESENT_PARAMETERS& pp) {
    SnapshotDeviceFixture device(real);
    mme::MmeContext ctx(&device);
    struct ContextScope {
        explicit ContextScope(mme::MmeContext* ctx) { mme::g_context = ctx; }
        ~ContextScope() { mme::g_context = nullptr; }
    } scope(&ctx);
    ctx.effectEnabled = 0; // EndScene tests maintenance, not unrelated effects.
    auto color0 = Color(real, D3DFMT_A8R8G8B8);
    auto color1 = Color(real, D3DFMT_A8R8G8B8);
    auto other0 = Color(real, D3DFMT_X8R8G8B8);
    auto other1 = Color(real, D3DFMT_X8R8G8B8);
    auto depth = Depth(real, D3DFMT_D24S8);
    auto otherDepth = Depth(real, D3DFMT_D16);
    auto bind = [&](UINT size, bool other = false) {
        Hr(real->SetDepthStencilSurface(nullptr), "unbind depth");
        Hr(real->SetRenderTarget(1, nullptr), "unbind MRT");
        Hr(real->SetRenderTarget(0, other ? other0.get() : color0.get()), "bind color0");
        Hr(real->SetRenderTarget(1, other ? other1.get() : color1.get()), "bind color1");
        Hr(real->SetDepthStencilSurface(other ? otherDepth.get() : depth.get()), "bind depth");
        D3DVIEWPORT9 vp = {0, 0, size, size, 0.0f, 1.0f};
        Hr(real->SetViewport(&vp), "set viewport");
        mme::MmeSaveTargetSet(ctx.persistentTargetSet, &device, 31);
        ctx.persistentLayerEligible = 0;
    };
    auto snapshot = [&](UINT size, bool special, bool other = false) {
        bind(size, other);
        ctx.allObjectsSpecialFlag = special;
        Check(mme::MmeSnapshotMainTargets(&ctx, true, true, 0xff123456, 1.0f) == S_OK,
              "production snapshot failed");
        return Bound(real);
    };
    auto a = snapshot(32, false);
    auto b = snapshot(64, false);
    auto again = snapshot(32, false);
    Check(a.color0 == again.color0 && a.color1 == again.color1 && a.depth == again.depth,
          "family A must retain and reuse each slot's A/B/A identity");
    Check(a.color0 != b.color0 && a.color1 != b.color1 && a.depth != b.depth,
          "family A exact-size keys must be distinct");
    Check(device.colorCreates == 4 && device.depthCreates == 2, "family A creation count");
    Check(ctx.snapshotCache.active.size() == 6, "all acquired identities become active");

    // A non-boundary repeat must not clear active; ==N must clear it.
    ctx.renderPassList.resize(2);
    ctx.lastRepeatCount = 1;
    mme::MmeUpdatePassBookkeeping(&ctx);
    Check(ctx.snapshotCache.active.size() == 6, "non-boundary repeat keeps active");
    ctx.lastRepeatCount = 2;
    mme::MmeUpdatePassBookkeeping(&ctx);
    Check(ctx.snapshotCache.active.empty(), "repeat==N clears active");
    again = snapshot(32, false);
    OnEndScene(&device);
    Check(ctx.mainTargetSet.mask == 0 && ctx.persistentTargetSet.mask == 0,
          "EndScene releases both saved sets");
    Check(ctx.snapshotCache.colors[0].exactSize.size() == 1 &&
          ctx.snapshotCache.colors[1].exactSize.size() == 1 &&
          ctx.snapshotCache.depth.exactSize.size() == 1, "edit mode prunes inactive B");
    const auto colorCreates = device.colorCreates, depthCreates = device.depthCreates;
    again = snapshot(32, false);
    Check(device.colorCreates == colorCreates && device.depthCreates == depthCreates,
          "active A survives EndScene");
    b = snapshot(64, false);
    ctx.lastRepeatCount = 2;
    mme::MmeUpdatePassBookkeeping(&ctx);
    fixtureEditMode = FALSE;
    OnEndScene(&device);
    Check(ctx.snapshotCache.colors[0].exactSize.size() == 2 &&
          ctx.snapshotCache.depth.exactSize.size() == 2, "playback retains inactive entries");
    fixtureEditMode = TRUE;
    OnEndScene(&device);
    Check(Empty(ctx.snapshotCache), "edit mode with empty active set evicts all entries");

    a = snapshot(32, true);
    b = snapshot(32, true, true);
    again = snapshot(16, true);
    Check(a.color0 == again.color0 && a.color1 == again.color1 && a.depth == again.depth,
          "family B retains formats and reuses larger surfaces");
    Check(ctx.snapshotCache.colors[0].atLeastSize.size() == 2 &&
          ctx.snapshotCache.depth.atLeastSize.size() == 2, "family B format keys coexist");
    auto larger = snapshot(64, true);
    Check(larger.color0 != a.color0 && larger.depth != a.depth,
          "family B replaces undersized format only");
    again = snapshot(32, true, true);
    Check(again.color0 == b.color0 && again.depth == b.depth, "other format survives growth");
    again = snapshot(32, false);
    Check(ctx.snapshotCache.colors[0].atLeastSize.empty() &&
          ctx.snapshotCache.depth.atLeastSize.empty(), "switch to A drops B");
    again = snapshot(32, true);
    Check(ctx.snapshotCache.colors[0].exactSize.empty() &&
          ctx.snapshotCache.depth.exactSize.empty(), "switch to B drops A");

    // Too-small family-B nodes become empty before persistent borrowing.
    bind(64);
    ctx.persistentLayerEligible = 1;
    ctx.persistentClearUsed = 0;
    const size_t activeBeforeBorrow = ctx.snapshotCache.active.size();
    Check(mme::MmeSnapshotMainTargets(&ctx, true, true, 0, 1) == S_OK, "persistent snapshot");
    auto borrowed = Bound(real);
    Check(borrowed.color0 == color0 && borrowed.color1 == color1 && borrowed.depth == depth,
          "persistent surfaces returned directly");
    Check(ctx.persistentClearUsed == 1 && ctx.snapshotCache.active.size() == activeBeforeBorrow,
          "persistent hit does not mark active");
    Check(ctx.snapshotCache.colors[0].atLeastSize.size() == 1 &&
          !ctx.snapshotCache.colors[0].atLeastSize.begin()->second &&
          !ctx.snapshotCache.depth.atLeastSize.begin()->second,
          "persistent hit retains existing empty format nodes");

    // Inject two distinct failures into real device creation boundaries.
    bind(48);
    ctx.allObjectsSpecialFlag = 0;
    device.colorResults = {E_OUTOFMEMORY, D3DERR_INVALIDCALL};
    device.depthResults = {D3DERR_NOTAVAILABLE};
    Check(mme::MmeSnapshotMainTargets(&ctx, true, true, 0, 1) == E_OUTOFMEMORY,
          "snapshot returns first creation failure");
    Check(ctx.snapshotError == static_cast<unsigned long>(D3DERR_NOTAVAILABLE),
          "manager retains latest creation failure");
    Check(Empty(ctx.snapshotCache) && ctx.mainTargetSet.mask == 0 &&
          ctx.persistentTargetSet.mask == 0, "failure clears both families and saved sets");
    again = Bound(real);
    Check(again.color0 == color0 && again.color1 == color1 && again.depth == depth,
          "failure restores the input bindings");

    // A successful earlier slot must survive working-set cleanup after a
    // later failure; also exercise the original hr!=0 test with S_FALSE.
    bind(48);
    device.colorResults = {S_OK, S_FALSE};
    Check(mme::MmeSnapshotMainTargets(&ctx, true, true, 0, 1) == S_FALSE,
          "positive nonzero creation result is a snapshot failure");
    Check(Empty(ctx.snapshotCache), "partial success is released on failure");
    again = snapshot(32, false);
    const size_t activeBeforeReset = ctx.snapshotCache.active.size();
    OnLostDevice(&device);
    std::printf("after device loss: cache empty=%d, main mask=%u, persistent mask=%u\n",
        Empty(ctx.snapshotCache), ctx.mainTargetSet.mask, ctx.persistentTargetSet.mask);
    Check(Empty(ctx.snapshotCache) && ctx.mainTargetSet.mask == 0 &&
          ctx.persistentTargetSet.mask == 0, "device-loss entry clears caches and saved sets");
    Check(ctx.snapshotCache.active.size() == activeBeforeReset,
          "reset cleanup preserves non-owning active identities until repeat boundary");

    // Remove every external/default-pool reference and actually reset D3D.
    a = {}; b = {}; again = {}; larger = {}; borrowed = {};
    Hr(real->SetRenderTarget(1, nullptr), "unbind MRT before reset");
    Hr(real->SetDepthStencilSurface(nullptr), "unbind depth before reset");
    IDirect3DSurface9* backBuffer = nullptr;
    Hr(real->GetBackBuffer(0, 0, D3DBACKBUFFER_TYPE_MONO, &backBuffer), "get backbuffer");
    Hr(real->SetRenderTarget(0, backBuffer), "restore backbuffer");
    backBuffer->Release();
    color0.reset(); color1.reset(); other0.reset(); other1.reset(); depth.reset(); otherDepth.reset();
    Hr(real->Reset(&pp), "real Reset after cache release");
    std::puts("snapshot cache: A/B/A, MRT/depth, family switch/growth, repeat boundary, edit/playback, persistent borrow, failures and real Reset passed");
}
}

int main() {
    Window window;
    Com<IDirect3D9> d3d;
    d3d.p = Direct3DCreate9(D3D_SDK_VERSION);
    if (!window.handle || !d3d.p) {
        std::puts("SKIP: window creation or Direct3DCreate9 unavailable"); return 77;
    }
    D3DPRESENT_PARAMETERS pp = {};
    pp.Windowed = TRUE; pp.SwapEffect = D3DSWAPEFFECT_DISCARD;
    pp.hDeviceWindow = window.handle; pp.BackBufferWidth = pp.BackBufferHeight = 128;
    pp.BackBufferFormat = D3DFMT_A8R8G8B8;
    pp.EnableAutoDepthStencil = TRUE; pp.AutoDepthStencilFormat = D3DFMT_D24S8;
    Com<IDirect3DDevice9> device;
    const HRESULT created = d3d->CreateDevice(0, D3DDEVTYPE_HAL, window.handle,
        D3DCREATE_SOFTWARE_VERTEXPROCESSING, &pp, &device.p);
    if (FAILED(created)) {
        std::printf("SKIP: HAL CreateDevice failed: %08lx\n", created); return 77;
    }
    D3DCAPS9 caps = {};
    device->GetDeviceCaps(&caps);
    if (caps.NumSimultaneousRTs < 2) {
        std::puts("SKIP: device supports fewer than two simultaneous render targets"); return 77;
    }
    try { Run(device.p, pp); }
    catch (const std::exception& error) { std::fprintf(stderr, "FAIL: %s\n", error.what()); return 1; }
    return 0;
}
