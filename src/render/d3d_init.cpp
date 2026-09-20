// ===========================================================================
// VA 0x00408020 - InitD3D  (original: sub_408020, 0x845 bytes)
// ===========================================================================
// Direct3D9 subsystem bootstrap, called at the top of UI creation
// (0x00466D20 @ 0x466D5B, i.e. during WM_CREATE).  The render object is
// the D3DRenderer restored in include/mikudancestudio/d3d_wrapper.hpp (original
// field offsets pinned there by static_assert on both architectures).
//
// Flow: monitor enum -> Direct3DCreate9 -> format probing (CheckDeviceType +
// CheckDepthStencilMatch 75/77/80 + multisample ladder 8/4/2/1) ->
// CreateDevice 7-attempt ladder (HAL/64 fmt22,21 -> SW/64 -> soft flags ->
// SW/64,32 -> REF/64,32) -> caps gate (VS/PS major >= 2) -> HDR RT +
// sprite + effect (res 118 SM3 / 117 SM2; technique desc 'r' check ->
// SetFloat("SKII1", 600/500)) -> aspect + PerspectiveFovLH(PI/4, aspect,
// 1.0f, 10000.0f) -> SetTransform(PROJECTION) -> render states (0x406E90)
// -> backbuffer/depth handles -> lockable capture render target.
// =========================================================================//
#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <Windows.h>
#include <d3d9.h>

#include <cstdio>
#include <cstring>
#include <cstdint>

#include "mikudancestudio/d3d_wrapper.hpp"
#include "mikudancestudio/d3dx_dyn.hpp"
#include "mikudancestudio/mme_bridge.hpp"
#include "mikudancestudio/mmd_app.hpp"
#include "mikudancestudio/ported_funcs.hpp"

namespace mikudancestudio {
namespace {

// VA 0x00406E50 - fnEnum: track the largest monitor dimensions.
BOOL CALLBACK EnumMonitorsMax(HMONITOR, HDC, LPRECT rect, LPARAM lParam) {
    auto* renderer = reinterpret_cast<D3DRenderer*>(lParam);
    const std::int32_t w = rect->right - rect->left;
    const std::int32_t h = rect->bottom - rect->top;
    if (renderer->screenWidth < w)  renderer->screenWidth = w;
    if (renderer->screenHeight < h) renderer->screenHeight = h;
    return TRUE;
}

void ReleaseCom(void* object) {
    if (object == nullptr)
        return;
    reinterpret_cast<IUnknown*>(object)->Release();  // vtable slot 2
}

// Porting-era error/state dumps under MIKUDANCESTUDIO_STATE_DUMP_DIR /
// MIKUDANCESTUDIO_VB_DUMP_DIR (CMake option MIKUDANCESTUDIO_DIAG, default
// OFF); the OFF stubs below keep the call sites valid and inline away to
// nothing.
#ifdef MIKUDANCESTUDIO_DIAG
void DumpCreateDeviceFailure(const D3DPRESENT_PARAMETERS& pp,
                             const HRESULT* attempts, int attemptCount,
                             D3DFORMAT depthFormat) {
    char directory[MAX_PATH]{};
    const DWORD length = GetEnvironmentVariableA(
        "MIKUDANCESTUDIO_VB_DUMP_DIR", directory, MAX_PATH);
    if (length == 0 || length >= MAX_PATH)
        return;
    CreateDirectoryA(directory, nullptr);
    char path[MAX_PATH]{};
    std::snprintf(path, sizeof(path), "%s\\d3d_create.error.txt", directory);
    FILE* stream = nullptr;
    if (fopen_s(&stream, path, "wb") != 0 || stream == nullptr)
        return;
    std::fprintf(stream,
        "BackBuffer=%ux%u format=%u depth=%u swap=%u msaa=%u quality=%u "
        "flags=%u interval=%u\r\n",
        pp.BackBufferWidth, pp.BackBufferHeight,
        static_cast<unsigned>(pp.BackBufferFormat),
        static_cast<unsigned>(depthFormat),
        static_cast<unsigned>(pp.SwapEffect),
        static_cast<unsigned>(pp.MultiSampleType), pp.MultiSampleQuality,
        pp.Flags, pp.PresentationInterval);
    for (int index = 0; index < attemptCount; ++index)
        std::fprintf(stream, "attempt[%d]=0x%08X\r\n", index,
                     static_cast<unsigned>(attempts[index]));
    std::fclose(stream);
}

void DumpEffectFailure(HRESULT result, int resourceId, void* errors) {
    char directory[MAX_PATH]{};
    DWORD length = GetEnvironmentVariableA(
        "MIKUDANCESTUDIO_STATE_DUMP_DIR", directory, MAX_PATH);
    if (length == 0 || length >= MAX_PATH) {
        length = GetEnvironmentVariableA(
            "MIKUDANCESTUDIO_VB_DUMP_DIR", directory, MAX_PATH);
    }
    if (length == 0 || length >= MAX_PATH)
        return;
    CreateDirectoryA(directory, nullptr);
    char path[MAX_PATH]{};
    std::snprintf(path, sizeof(path), "%s\\effect_create.error.txt",
                  directory);
    FILE* stream = nullptr;
    if (fopen_s(&stream, path, "wb") != 0 || stream == nullptr)
        return;
    std::fprintf(stream, "resource=%d HRESULT=0x%08X\r\n", resourceId,
                 static_cast<unsigned>(result));
    if (errors != nullptr) {
        using GetPointer = void*(__stdcall*)(void*);
        using GetSize = DWORD(__stdcall*)(void*);
        void** vtable = *reinterpret_cast<void***>(errors);
        const void* bytes = reinterpret_cast<GetPointer>(vtable[3])(errors);
        const DWORD size = reinterpret_cast<GetSize>(vtable[4])(errors);
        if (bytes != nullptr && size != 0)
            std::fwrite(bytes, 1, size, stream);
    }
    std::fclose(stream);
}

void DumpEffectStatus(bool haveD3dx, int vsMajor, int psMajor,
                      int maxTexW, int maxTexH, D3DRenderer* renderer) {
    char directory[MAX_PATH]{};
    const DWORD length = GetEnvironmentVariableA(
        "MIKUDANCESTUDIO_STATE_DUMP_DIR", directory, MAX_PATH);
    if (length == 0 || length >= MAX_PATH)
        return;
    CreateDirectoryA(directory, nullptr);
    char path[MAX_PATH]{};
    std::snprintf(path, sizeof(path), "%s\\effect_create.status.txt",
                  directory);
    FILE* stream = nullptr;
    if (fopen_s(&stream, path, "wb") != 0 || stream == nullptr)
        return;
    std::fprintf(stream,
        "d3dx=%d vs=%d ps=%d max=%dx%d capable=%u rt=%p surface=%p "
        "depth=%p effect=%p\r\n",
        haveD3dx ? 1 : 0, vsMajor, psMajor, maxTexW, maxTexH,
        renderer->postProcessEnabled,
        renderer->hdrTexture, renderer->shadowSurface,
        renderer->shadowDepthSurface, renderer->effect);
    std::fclose(stream);
}
#else
inline void DumpCreateDeviceFailure(const D3DPRESENT_PARAMETERS&,
                                    const HRESULT*, int, D3DFORMAT) {}
inline void DumpEffectFailure(HRESULT, int, void*) {}
inline void DumpEffectStatus(bool, int, int, int, int, D3DRenderer*) {}
#endif

}  // namespace

bool InitD3D(MMDApp* app, HWND hwnd, bool english, HMODULE hModule) {
    D3DRenderer* r = app->Renderer();                               // 657092
    if (r == nullptr)
        return false;
    // The required D3DX import is resolved by Windows before WinMain.
    auto* d3dx = &d3dx::Get();
    constexpr bool haveD3dx = true;

    r->hwnd = hwnd;
    EnumDisplayMonitors(nullptr, nullptr, EnumMonitorsMax,
                        reinterpret_cast<LPARAM>(r));

    IDirect3D9* d3d = Direct3DCreate9(D3D_SDK_VERSION /*0x20*/);
    r->d3d9 = d3d;
    if (d3d == nullptr) {
        MessageBoxA(hwnd,
            english ? "DirectX9 is not installed in your PC!!\nPlease install it if you want."
                    : "DirectX\x82\xCC\x83\x6F\x81\x5B\x83\x57\x83\x87"
                      "\x83\x93\x39\x82\xAA\x83\x43\x83\x93\x83\x58"
                      "\x83\x67\x81\x5B\x83\x8B\x82\xB3\x82\xEA\x82"
                      "\xC4\x82\xA2\x82\xDC\x82\xB9\x82\xF1\x0A\x8D"
                      "\xC5\x90\x56\x82\xCC\x44\x69\x72\x65\x63\x74"
                      "\x58\x82\xF0\x83\x70\x83\x73\x83\x52\x83\x93"
                      "\x82\xC9\x83\x43\x83\x93\x83\x58\x83\x67\x81"
                      "\x5B\x83\x8B\x82\xB5\x82\xC4\x89\xBA\x82\xB3"
                      "\x82\xA2",  // x64 0x7FF7CB54A338

            "Direct3D::Init", MB_OK);
        return false;
    }

    // back-buffer format preference: A8R8G8B8(21) unless unsupported -> X8R8G8B8(22)
    r->backbufferFormat = static_cast<D3DFORMAT>(21);
    if (FAILED(d3d->CheckDeviceType(0, D3DDEVTYPE_HAL,
                                    D3DFMT_X8R8G8B8 /*22*/,
                                    D3DFMT_A8R8G8B8 /*21*/, TRUE)))
        r->backbufferFormat = static_cast<D3DFORMAT>(22);

    // depth format ladder: D24S8(75) -> D24X8(77) -> D16(80)
    const D3DFORMAT bbFmt = r->backbufferFormat;
    r->presentParameters.AutoDepthStencilFormat =
        static_cast<D3DFORMAT>(75);
    if (FAILED(d3d->CheckDepthStencilMatch(
            0, D3DDEVTYPE_HAL, D3DFMT_X8R8G8B8,
            bbFmt, D3DFMT_D24S8))) {
        r->d3dInitialized = 0;
        r->presentParameters.AutoDepthStencilFormat =
            static_cast<D3DFORMAT>(77);
        if (FAILED(d3d->CheckDepthStencilMatch(
                0, D3DDEVTYPE_HAL, D3DFMT_X8R8G8B8,
                bbFmt, D3DFMT_D24X8)))
            r->presentParameters.AutoDepthStencilFormat =
                static_cast<D3DFORMAT>(80);
    }

    // presentation parameters (fields land on the object layout exactly)
    D3DPRESENT_PARAMETERS pp;
    std::memset(&pp, 0, sizeof(pp));
    pp.BackBufferWidth = r->screenWidth;
    pp.BackBufferHeight = r->screenHeight;
    pp.BackBufferFormat = bbFmt;
    pp.BackBufferCount = 1;
    pp.SwapEffect = D3DSWAPEFFECT_DISCARD /*1*/;
    pp.hDeviceWindow = hwnd;
    pp.Windowed = TRUE;
    pp.EnableAutoDepthStencil = TRUE;
    pp.AutoDepthStencilFormat = r->presentParameters.AutoDepthStencilFormat;
    pp.PresentationInterval = 0x80000000;   // D3DPRESENT_INTERVAL_IMMEDIATE

    // multisample ladder 8 -> 4 -> 2 -> 1 (both depth-format candidates)
    DWORD quality = 0;
    auto msProbe = [&](D3DFORMAT fmt, DWORD type) {
        DWORD q = 0;
        return SUCCEEDED(d3d->CheckDeviceMultiSampleType(
                   0, D3DDEVTYPE_HAL, fmt, TRUE,
                   static_cast<D3DMULTISAMPLE_TYPE>(type), &q))
                   ? (q, true)
                   : false;
    };
    const D3DFORMAT fmt128 = r->backbufferFormat;
    const D3DFORMAT fmt100 = r->presentParameters.AutoDepthStencilFormat;
    if (msProbe(fmt128, 8) && SUCCEEDED(d3d->CheckDeviceMultiSampleType(
            0, D3DDEVTYPE_HAL, fmt100, TRUE, D3DMULTISAMPLE_8_SAMPLES, &quality))) {
        pp.MultiSampleType = D3DMULTISAMPLE_8_SAMPLES;
    } else if (msProbe(fmt128, 4) &&
               SUCCEEDED(d3d->CheckDeviceMultiSampleType(
                   0, D3DDEVTYPE_HAL, fmt100, TRUE,
                   D3DMULTISAMPLE_4_SAMPLES, &quality))) {
        pp.MultiSampleType = D3DMULTISAMPLE_4_SAMPLES;
    } else if (msProbe(fmt128, 2) &&
               SUCCEEDED(d3d->CheckDeviceMultiSampleType(
                   0, D3DDEVTYPE_HAL, fmt100, TRUE,
                   D3DMULTISAMPLE_2_SAMPLES, &quality))) {
        pp.MultiSampleType = D3DMULTISAMPLE_2_SAMPLES;
    } else if (msProbe(fmt128, 1) &&
               SUCCEEDED(d3d->CheckDeviceMultiSampleType(
                   0, D3DDEVTYPE_HAL, fmt100, TRUE,
                   D3DMULTISAMPLE_NONMASKABLE, &quality))) {
        pp.MultiSampleType = D3DMULTISAMPLE_NONMASKABLE;
    } else {
        pp.SwapEffect = static_cast<D3DSWAPEFFECT>(3);
        pp.MultiSampleType = D3DMULTISAMPLE_NONE;
        pp.MultiSampleQuality = 0;
        r->multisampleAvailable = 0;
    }
    if (pp.MultiSampleType != D3DMULTISAMPLE_NONE) {
        pp.MultiSampleQuality = quality ? quality - 1 : 0;
        r->multisampleAvailable = 1;
    }
    // mirror into the object (later code reads the embedded copy)
    r->presentParameters = pp;

    // CreateDevice ladder: HAL/64 fmt22, HAL/64 fmt21, (soft flags) HAL/64,
    // HAL/32, REF/64, REF/32 - exactly the original retry sequence.
    IDirect3DDevice9** deviceSlot = &r->device;
    HRESULT attempts[7]{};
    int attemptCount = 0;
    const auto createDevice = [&](D3DDEVTYPE type, DWORD behavior) {
        const HRESULT result = d3d->CreateDevice(
            0, type, hwnd, behavior, &pp, deviceSlot);
        attempts[attemptCount++] = result;
        return result;
    };
    HRESULT hr = createDevice(D3DDEVTYPE_HAL, 0x40 /*HARDWARE VP*/);
    if (FAILED(hr)) {
        r->backbufferFormat = static_cast<D3DFORMAT>(22);
        r->presentParameters.BackBufferFormat = D3DFMT_X8R8G8B8;
        pp.BackBufferFormat = D3DFMT_X8R8G8B8;
        hr = createDevice(D3DDEVTYPE_HAL, 0x40);
        if (FAILED(hr)) {
            r->backbufferFormat = static_cast<D3DFORMAT>(21);
            r->presentParameters.BackBufferFormat = D3DFMT_A8R8G8B8;
            pp.BackBufferFormat = D3DFMT_A8R8G8B8;
            pp.SwapEffect = static_cast<D3DSWAPEFFECT>(3);
            pp.MultiSampleType = D3DMULTISAMPLE_NONE;
            pp.MultiSampleQuality = 0;
            pp.Flags = 1;                        // lockable back buffer
            r->multisampleAvailable = 0;
            hr = createDevice(D3DDEVTYPE_HAL, 0x40);
            if (FAILED(hr)) {
                r->backbufferFormat = static_cast<D3DFORMAT>(22);
                r->presentParameters.BackBufferFormat = D3DFMT_X8R8G8B8;
                pp.BackBufferFormat = D3DFMT_X8R8G8B8;
                hr = createDevice(D3DDEVTYPE_HAL, 0x40);
                if (FAILED(hr)) {
                    hr = createDevice(D3DDEVTYPE_HAL,
                                      0x20 /*SOFTWARE VP*/);
                    if (FAILED(hr)) {
                        hr = createDevice(D3DDEVTYPE_REF, 0x40);
                        if (FAILED(hr)) {
                            hr = createDevice(D3DDEVTYPE_REF, 0x20);
                            if (FAILED(hr)) {
                                DumpCreateDeviceFailure(
                                    pp, attempts, attemptCount,
                                    r->presentParameters
                                        .AutoDepthStencilFormat);
                                MessageBoxA(hwnd, "CreateDevice Failed!",
                                            "Direct3D::Init", MB_OK);
                                d3d->Release();
                                return false;
                            }
                        }
                    }
                }
            }
        }
    }
    IDirect3DDevice9* device = r->device;

    // caps: raw dword reads at the original offsets (Ghidra split locals)
    D3DCAPS9 caps;
    if (FAILED(device->GetDeviceCaps(&caps)))
        return true;                       // original proceeds regardless
    // x64 0x7FF7CB4280E1: renderer+240048 mirrors caps+0x6C, which is
    // D3DCAPS9::MaxAnisotropy (MaxTextureWidth/Height live at +0x58/+0x5C
    // in this struct, pushing MaxAnisotropy to +0x6C; MaxStreams is the
    // far later +0xBC).  render_states feeds this field to
    // D3DSAMP_MAXANISOTROPY.  The old +0x70 read picked MaxVertexW (float
    // bits) and delivered a garbage anisotropy cap.
    r->shaderModelCaps =
        static_cast<std::int32_t>(caps.MaxAnisotropy);
    // The original stack-local labels are displaced by one dword around
    // GetDeviceCaps.  The actual fields consumed at 0x4084DA..0x408533 are
    // MaxTextureWidth/Height and the VS/PS version dwords.  Reading the
    // apparent Hex-Rays offsets (+5C/+60, +C8/+D0) instead selects
    // MaxTextureHeight/MaxVolumeExtent and, critically,
    // MaxVertexShaderConst/PixelShader1xMaxValue.  On modern hardware that
    // produces the observed bogus gate values VS=1, PS=224 and disables the
    // complete effect/reflection renderer.
    const std::int32_t maxTexW = static_cast<std::int32_t>(
        caps.MaxTextureWidth);
    const std::int32_t maxTexH = static_cast<std::int32_t>(
        caps.MaxTextureHeight);
    r->maxTextureWidth = maxTexW;
    r->maxTextureHeight = maxTexH;
    // BYTE1 of caps dwords +0xC8/+0xD0 = VS/PS major version gates
    const int vsMajor = (caps.VertexShaderVersion >> 8) & 0xFF;
    const int psMajor = (caps.PixelShaderVersion >> 8) & 0xFF;

    if (haveD3dx && vsMajor >= 2 && psMajor >= 2) {
        r->postProcessEnabled = 1;
        std::int32_t rtW = maxTexW, rtH = maxTexH;
        if (rtW > 2048) rtW = 2048;
        if (rtH > 2048) rtH = 2048;
        r->renderTargetWidth = rtW;
        r->renderTargetHeight = rtH;

        if (SUCCEEDED(d3dx->createTexture(device, rtW, rtH, 1,
                                          D3DUSAGE_RENDERTARGET /*1*/,
                                          static_cast<D3DFORMAT>(114) /*A16B16G16R16F*/,
                                          D3DPOOL_DEFAULT /*0*/, &r->hdrTexture))) {
            // sprite texture from the embedded PNG (res 0x67)
            HRSRC res = FindResourceA(hModule, MAKEINTRESOURCEA(0x67), "PNG");
            DWORD size = SizeofResource(nullptr, res);
            HGLOBAL glob = LoadResource(hModule, res);
            void* data = LockResource(glob);
            d3dx->fromMemEx(device, data, size, UINT(-1), UINT(-1), 1, 0,
                            D3DFMT_UNKNOWN, D3DPOOL_MANAGED, DWORD(-1),
                            DWORD(-1), 0, nullptr, nullptr,
                            &r->spriteTexture);
            IDirect3DSurface9** spriteSurf = &r->shadowSurface;
            r->hdrTexture->GetSurfaceLevel(0, spriteSurf);

            IDirect3DSurface9** shadowDepth = &r->shadowDepthSurface;
            if (FAILED(device->CreateDepthStencilSurface(
                    rtW, rtH, static_cast<D3DFORMAT>(77) /*D24X8*/,
                    D3DMULTISAMPLE_NONE, 0, FALSE, shadowDepth, nullptr))) {
                if (*spriteSurf) { (*spriteSurf)->Release(); *spriteSurf = nullptr; }
                if (r->hdrTexture) { r->hdrTexture->Release(); r->hdrTexture = nullptr; }
            } else {
                void** effect = reinterpret_cast<void**>(&r->effect);
                void* effectErrors = nullptr;
                HRESULT effectResult = E_FAIL;
                int effectResource = 0;
                if (vsMajor >= 3 && psMajor >= 3) {
                    effectResource = 118;
                    effectResult = d3dx->createEffectFromResA(
                        device, nullptr, MAKEINTRESOURCEA(118), nullptr,
                        nullptr, 0, nullptr, effect, &effectErrors);
                    r->shaderModel3 = 1;                            // SM3
                } else {
                    effectResource = 117;
                    effectResult = d3dx->createEffectFromResA(
                        device, nullptr, MAKEINTRESOURCEA(117), nullptr,
                        nullptr, 0, nullptr, effect, &effectErrors);
                    // Level-0 format of the HDR texture decides the integer
                    // the effect receives under the name "SKII1": 114 ==
                    // D3DFMT_R32F ('r') -> 600 plus the shader-model-3 flag,
                    // anything else -> 500.  (In the original this is
                    // IDirect3DTexture9::GetLevelDesc followed by
                    // ID3DXBaseEffect::SetInt - x86 vtable bytes 0x44 on the
                    // texture and 0x68 on the effect.)
                    if (*effect != nullptr) {
                        D3DSURFACE_DESC levelDesc{};
                        r->hdrTexture->GetLevelDesc(0, &levelDesc);
                        const bool r32fTarget =
                            levelDesc.Format == D3DFMT_R32F;
                        static_cast<d3dx::Effect*>(*effect)->SetInt(
                            "SKII1", r32fTarget ? 600 : 500);
                        r->shaderModel3 =
                            static_cast<std::uint8_t>(r32fTarget);
                    }
                }
                if (FAILED(effectResult) || *effect == nullptr)
                    DumpEffectFailure(effectResult, effectResource,
                                      effectErrors);
                ReleaseCom(effectErrors);
                if (*effect == nullptr) {
                    if (*spriteSurf) { (*spriteSurf)->Release(); *spriteSurf = nullptr; }
                    if (r->hdrTexture) { r->hdrTexture->Release(); r->hdrTexture = nullptr; }
                    if (*shadowDepth) {
                        (*shadowDepth)->Release();
                        *shadowDepth = nullptr;
                    }
                }
            }
        }
        if (r->effect == nullptr)
            r->postProcessEnabled = 0;
    }
    DumpEffectStatus(haveD3dx, vsMajor, psMajor, maxTexW, maxTexH, r);

    // aspect + projection: PerspectiveFovLH(PI/4, aspect, 1.0f, 10000.0f)
    const float aspect = static_cast<float>(
        static_cast<double>(r->screenWidth) /
        static_cast<double>(r->screenHeight));
    r->aspectRatio = aspect;
    if (haveD3dx) {
        d3dx::D3DXMATRIXF proj;
        d3dx->perspectiveFovLH(&proj, 0.7853981852531433f /*PI/4, 0x529698*/,
                               aspect, 1.0f, 10000.0f /*0x529618*/);
        device->SetTransform(D3DTS_PROJECTION /*3*/,
                             reinterpret_cast<const D3DMATRIX*>(&proj));
    }

    InitRenderStates(app);                                         // 0x406E90

    r->viewScale = 1.0f;
    device->GetBackBuffer(0, 0, D3DBACKBUFFER_TYPE_MONO,
                          &r->backbufferSurface);
    device->GetDepthStencilSurface(&r->depthStencilSurface);

    if (r->multisampleAvailable == 0 &&
        device->CreateRenderTarget(
            r->screenWidth, r->screenHeight,
            r->backbufferFormat,
            D3DMULTISAMPLE_NONE, 0, TRUE /*lockable*/,
            &r->captureSurface, nullptr) != D3D_OK)
        r->multisampleAvailable = 1;

    if (r->stereoEnabled != 0 &&
        ProbeStereo3D(device, reinterpret_cast<unsigned char*>(
                                  &r->stereoHandle)))
        r->stereoEnabled = 0;

    // 内置 MMEffect：设备与内置标准效果均已就绪，注册宿主窗口/标准效果。
    // 实际 Initialize 保持原版的惰性时机（首个 BeginScene）。InitD3D 运行
    // 于 WM_CREATE 期间，app->Hwnd() 尚未赋值——传参数句柄。
    mme::OnDeviceCreated(app, hwnd);
    return true;
}

}  // namespace mikudancestudio
