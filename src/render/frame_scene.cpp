// ===========================================================================
// VA 0x0046DC00..0x0046E787 - FrameDriver D3D scene envelope
// ===========================================================================
// This is the non-model shell of sub_46B090's render stage: exact clear
// selection, scene lifetime, and the three dynamic overlay batches.  The
// model/shadow/background helpers remain separate nodes in the recovered
// call graph and are intentionally not approximated here.
// ===========================================================================
#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <Windows.h>
#include <d3d9.h>

#include <cstdio>
#include <cstdint>
#include <cstring>

#include "mikudancestudio/mme_bridge.hpp"
#include "mikudancestudio/mmd_app.hpp"
#include "mikudancestudio/ported_funcs.hpp"

namespace mikudancestudio {
namespace {

struct TexturedScreenVertex {
    float x;
    float y;
    float z;
    float reciprocalW;
    D3DCOLOR color;
    float u;
    float v;
};

struct ColoredScreenVertex {
    float x;
    float y;
    float z;
    float reciprocalW;
    D3DCOLOR color;
};

constexpr DWORD kTexturedScreenVertexFvf =
    D3DFVF_XYZRHW | D3DFVF_DIFFUSE | D3DFVF_TEX1;
constexpr DWORD kColoredScreenVertexFvf = D3DFVF_XYZRHW | D3DFVF_DIFFUSE;

static_assert(sizeof(TexturedScreenVertex) == 28);
static_assert(sizeof(ColoredScreenVertex) == 20);

void DrawTextOverlay(MMDApp* app, IDirect3DDevice9* device) {
    const UINT count = app->TextOverlayPrimitiveCount();
    auto* vb = app->OverlayVertices();
    if (count == 0 || vb == nullptr)
        return;

    device->SetTexture(0, app->TextOverlayTexture());
    device->SetStreamSource(0, vb, 0, sizeof(TexturedScreenVertex));
    device->SetFVF(kTexturedScreenVertexFvf);
    mme::DrawPrimitive(device, D3DPT_TRIANGLELIST, 0, count);
}

void DrawLineOverlay(MMDApp* app, D3DRenderer* sub,
                     IDirect3DDevice9* device) {
    const UINT count = app->LineOverlayPrimitiveCount();
    // The renderer owns the selection and guide-line vertex buffer.
    auto* vb = sub->lineVertexBuffer;
    if (count == 0 || vb == nullptr)
        return;

    // 原版线批前显式解绑 stage 0（x64 0x7FF7CB44AF44 的 SetTexture(0,NULL)，
    // 位于文本批之后、线批之前），否则线批会继承文本批的字体图集纹理。
    device->SetTexture(0, nullptr);
    device->SetStreamSource(0, vb, 0, sizeof(ColoredScreenVertex));
    device->SetFVF(kColoredScreenVertexFvf);
    // MME: 原版线框批次（x64 @0x44AFB4）与文本/精灵 overlay 一样经设备虚表
    // DrawPrimitive 槽发出（被 MMHack 包装可见），故同样过桥。
    mme::DrawPrimitive(device, D3DPT_LINELIST, 0, count);
}

void DrawSpriteOverlay(MMDApp* app, IDirect3DDevice9* device) {
    const UINT count = app->SpriteOverlayPrimitiveCount();
    auto* vb = app->SpriteOverlayVertices();
    if (count == 0 || vb == nullptr)
        return;

    device->SetTexture(0, app->OverlayTexture());
    device->SetStreamSource(0, vb, 0, sizeof(TexturedScreenVertex));
    device->SetFVF(kTexturedScreenVertexFvf);
    mme::DrawPrimitive(device, D3DPT_TRIANGLELIST, 0, count);
}

void DrawFullscreenQuad(IDirect3DDevice9* device,
                        IDirect3DBaseTexture9* texture,
                        IDirect3DVertexBuffer9* vertices) {
    device->SetTexture(0, texture);
    device->SetStreamSource(0, vertices, 0, 28);
    device->SetFVF(0x144);
    mme::DrawPrimitive(device, D3DPT_TRIANGLELIST, 0, 2);
}

// Porting-era screen-texture readback under
// MIKUDANCESTUDIO_SCREEN_TEXTURE_DUMP_DIR (CMake option MIKUDANCESTUDIO_DIAG,
// default OFF); the OFF stub below keeps the call sites valid and inlines
// away to nothing.
#ifdef MIKUDANCESTUDIO_DIAG
#pragma pack(push, 1)
struct DumpBitmapFileHeader {
    std::uint16_t type;
    std::uint32_t size;
    std::uint16_t reserved1;
    std::uint16_t reserved2;
    std::uint32_t bitsOffset;
};
#pragma pack(pop)

// Test-only readback of the texture exposed to screen.bmp.  GetRenderTargetData
// is deliberately issued after EndScene; doing it inside 0x46E025 changes the
// D3D command boundary and can fail on real D3D9 drivers.
void DumpAccessoryScreenTexture(MMDApp* app, IDirect3DDevice9* device) {
    char directory[MAX_PATH]{};
    const DWORD length = GetEnvironmentVariableA(
        "MIKUDANCESTUDIO_SCREEN_TEXTURE_DUMP_DIR", directory, MAX_PATH);
    if (length == 0 || length >= MAX_PATH)
        return;

    char stableCapture[2]{};
    if (GetEnvironmentVariableA("MIKUDANCESTUDIO_AB_STABLE_CAPTURE", stableCapture,
                                sizeof(stableCapture)) == 1 &&
        stableCapture[0] == '1') {
        char gatePath[MAX_PATH]{};
        std::snprintf(gatePath, sizeof(gatePath), "%s\\vb.capture.ready",
                      directory);
        const DWORD attributes = GetFileAttributesA(gatePath);
        if (attributes == INVALID_FILE_ATTRIBUTES ||
            (attributes & FILE_ATTRIBUTE_DIRECTORY) != 0)
            return;
    }

    static LONG dumped = 0;
    if (InterlockedCompareExchange(&dumped, 1, 0) != 0)
        return;

    IDirect3DTexture9* texture = app->CaptureTexture();
    if (texture == nullptr)
        return;

    D3DSURFACE_DESC description{};
    IDirect3DSurface9* source = nullptr;
    IDirect3DSurface9* system = nullptr;
    D3DLOCKED_RECT locked{};
    bool isLocked = false;
    if (FAILED(texture->GetLevelDesc(0, &description)) ||
        FAILED(texture->GetSurfaceLevel(0, &source)) || source == nullptr ||
        FAILED(device->CreateOffscreenPlainSurface(
            description.Width, description.Height, description.Format,
            D3DPOOL_SYSTEMMEM, &system, nullptr)) || system == nullptr ||
        FAILED(device->GetRenderTargetData(source, system)) ||
        FAILED(system->LockRect(&locked, nullptr, D3DLOCK_READONLY))) {
        if (system != nullptr)
            system->Release();
        if (source != nullptr)
            source->Release();
        return;
    }
    isLocked = true;

    CreateDirectoryA(directory, nullptr);
    char rawPath[MAX_PATH]{};
    char bitmapPath[MAX_PATH]{};
    char metaPath[MAX_PATH]{};
    std::snprintf(rawPath, sizeof(rawPath), "%s\\screen_texture.bgra",
                  directory);
    std::snprintf(bitmapPath, sizeof(bitmapPath), "%s\\screen_texture.bmp",
                  directory);
    std::snprintf(metaPath, sizeof(metaPath), "%s\\screen_texture.meta.json",
                  directory);

    const DWORD rowBytes = description.Width * 4;
    FILE* stream = nullptr;
    if (fopen_s(&stream, rawPath, "wb") == 0 && stream != nullptr) {
        for (UINT y = 0; y < description.Height; ++y) {
            const auto* row = static_cast<const std::uint8_t*>(locked.pBits) +
                              y * locked.Pitch;
            std::fwrite(row, 1, rowBytes, stream);
        }
        std::fclose(stream);
    }

    if (fopen_s(&stream, bitmapPath, "wb") == 0 && stream != nullptr) {
        const DWORD pixelBytes = rowBytes * description.Height;
        DumpBitmapFileHeader fileHeader{
            0x4D42, static_cast<DWORD>(sizeof(DumpBitmapFileHeader) +
                                      sizeof(BITMAPINFOHEADER) + pixelBytes),
            0, 0, static_cast<DWORD>(sizeof(DumpBitmapFileHeader) +
                                     sizeof(BITMAPINFOHEADER))};
        BITMAPINFOHEADER info{};
        info.biSize = sizeof(info);
        info.biWidth = static_cast<LONG>(description.Width);
        info.biHeight = static_cast<LONG>(description.Height);
        info.biPlanes = 1;
        info.biBitCount = 32;
        info.biCompression = BI_RGB;
        info.biSizeImage = pixelBytes;
        std::fwrite(&fileHeader, 1, sizeof(fileHeader), stream);
        std::fwrite(&info, 1, sizeof(info), stream);
        for (UINT y = description.Height; y-- > 0;) {
            const auto* row = static_cast<const std::uint8_t*>(locked.pBits) +
                              y * locked.Pitch;
            std::fwrite(row, 1, rowBytes, stream);
        }
        std::fclose(stream);
    }

    if (fopen_s(&stream, metaPath, "wb") == 0 && stream != nullptr) {
        const int mode = static_cast<int>(app->CaptureMode());
        RECT capture = app->RecordingWindow() != nullptr
            ? RECT{0, 0, app->RenderWidth(), app->RenderHeight()}
            : app->ViewportRect();
        if (mode == 2) {
            const double width =
                static_cast<double>(capture.right - capture.left);
            const double height =
                static_cast<double>(capture.bottom - capture.top);
            const double targetWidth = height * 4.0 / 3.0;
            if (targetWidth < width) {
                const double center =
                    static_cast<double>(capture.left + capture.right) * 0.5;
                capture.left = static_cast<LONG>(center - targetWidth * 0.5);
                capture.right = static_cast<LONG>(center + targetWidth * 0.5);
            } else {
                const double targetHeight = width * 3.0 / 4.0;
                const double center =
                    static_cast<double>(capture.top + capture.bottom) * 0.5;
                capture.top =
                    static_cast<LONG>(center - targetHeight * 0.5);
                capture.bottom =
                    static_cast<LONG>(center + targetHeight * 0.5);
            }
        }
        std::fprintf(stream,
            "{\"schema\":2,\"width\":%u,\"height\":%u,"
            "\"format\":%u,\"pitch\":%d,\"mode\":%d,"
            "\"source_rect\":[%ld,%ld,%ld,%ld]}\n",
            description.Width, description.Height,
            static_cast<unsigned>(description.Format), locked.Pitch,
            mode, capture.left, capture.top, capture.right, capture.bottom);
        std::fclose(stream);
    }

    if (isLocked)
        system->UnlockRect();
    system->Release();
    source->Release();
}
#else
inline void DumpAccessoryScreenTexture(MMDApp*, IDirect3DDevice9*) {}
#endif

void ComposeSelfShadow(MMDApp* app, D3DRenderer* sub,
                       IDirect3DDevice9* device) { // 0x46DE61
    if (app->SelfShadowCompositionEnabled() == 0 ||
        app->SelfShadowMode() <= 0)
        return;
    device->SetRenderState(D3DRS_ALPHABLENDENABLE, FALSE);
    DrawFullscreenQuad(device,
        sub->hdrTexture,
        app->LeftViewportVertices());
    device->SetRenderState(D3DRS_ALPHABLENDENABLE, TRUE);
}

void ComposeCallbackTexture(MMDApp* app,
                            IDirect3DDevice9* device) { // 0x46DF2A
    if (app->DepthTextureCompositionEnabled() == 0 ||
        app->DepthDeviceEnabled() == 0 ||
        app->FrameStepPlayback() != 0)
        return;
    DepthTextureProvider callback = app->DepthTextureCallback();
    if (callback == nullptr)
        return;
    IDirect3DBaseTexture9* texture = nullptr;
    callback(&texture);
    DrawFullscreenQuad(device, texture,
        app->RightViewportVertices());
}

void CaptureAccessoryScreenTexture(MMDApp* app, D3DRenderer* sub,
                                   IDirect3DDevice9* device) { // 0x46E025
    const int mode = static_cast<int>(app->CaptureMode());
    if (mode != 1 && mode != 2)
        return;
    auto*& texture = app->CaptureTexture();
    if (texture == nullptr) {
        HRESULT result = device->CreateTexture(1024, 1024, 1,
            D3DUSAGE_RENDERTARGET, D3DFMT_X8R8G8B8, D3DPOOL_DEFAULT,
            &texture, nullptr);
        if (result == D3DERR_OUTOFVIDEOMEMORY || result == E_OUTOFMEMORY)
            device->CreateTexture(512, 512, 1, D3DUSAGE_RENDERTARGET,
                D3DFMT_X8R8G8B8, D3DPOOL_DEFAULT, &texture, nullptr);
        if (texture == nullptr)
            return;
    }

    IDirect3DSurface9* textureSurface = nullptr;
    texture->GetSurfaceLevel(0, &textureSurface);
    auto** backBuffer = &sub->backbufferSurface;
    if (*backBuffer == nullptr)
        device->GetBackBuffer(0, 0, D3DBACKBUFFER_TYPE_MONO, backBuffer);
    if (mode == 1 && textureSurface != nullptr && *backBuffer != nullptr) {
        const RECT* sourceRect;
        RECT captureRect{};
        if (app->RecordingWindow() != nullptr) {
            captureRect.right = app->RenderWidth();
            captureRect.bottom = app->RenderHeight();
            sourceRect = &captureRect;
        } else {
            sourceRect = &app->ViewportRect();
        }
        if (FAILED(device->StretchRect(*backBuffer, sourceRect,
                                       textureSurface, nullptr,
                                       D3DTEXF_LINEAR)))
            device->StretchRect(*backBuffer, sourceRect, textureSurface,
                                nullptr, D3DTEXF_NONE);
    } else if (mode == 2 && textureSurface != nullptr &&
               *backBuffer != nullptr) {
        RECT source = app->RecordingWindow() != nullptr
            ? RECT{0, 0, app->RenderWidth(), app->RenderHeight()}
            : app->ViewportRect();
        const double width = static_cast<double>(source.right - source.left);
        const double height = static_cast<double>(source.bottom - source.top);
        const double targetWidth = height * 4.0 / 3.0;
        if (targetWidth < width) {
            const double center =
                static_cast<double>(source.left + source.right) * 0.5;
            source.left = static_cast<LONG>(center - targetWidth * 0.5);
            source.right = static_cast<LONG>(center + targetWidth * 0.5);
        } else {
            const double targetHeight = width * 3.0 / 4.0;
            const double center =
                static_cast<double>(source.top + source.bottom) * 0.5;
            source.top = static_cast<LONG>(center - targetHeight * 0.5);
            source.bottom = static_cast<LONG>(center + targetHeight * 0.5);
        }
        if (FAILED(device->StretchRect(*backBuffer, &source,
                                       textureSurface, nullptr,
                                       D3DTEXF_LINEAR)))
            device->StretchRect(*backBuffer, &source, textureSurface,
                                nullptr, D3DTEXF_NONE);
    }
    if (textureSurface != nullptr)
        textureSurface->Release();
}

}  // namespace

void RenderFrameScene(MMDApp* app) {
    D3DRenderer* sub = app->Renderer();
    if (sub == nullptr)
        return;

    IDirect3DDevice9* device = sub->device;
    if (device == nullptr)
        return;

    SetupFrameWorldTransform(app);
    UpdateFrameStereo(app);
    PrepareFrameSpriteOverlay(app); // 0x46BD79, after transform setup
    D3DMATRIX frameWorld{};
    device->GetTransform(D3DTS_WORLD, &frameWorld);

    // 0x46DB41: the whole block - per-model VB update (inner gate
    // app+0xA0665 == 0) AND the Clear - runs only when app+0xA0D6C
    // (messageSeen) != 0; frames without a message leave both untouched.
    if (app->state.messageSeen == 0)
        return;
    if (app->state.accessoryEditDialogOpen == 0) {
        for (int slot = 0; slot < kModelSlotCount; ++slot) {
            auto* model = app->ModelSlot(slot);
            if (model != nullptr)
                UpdateModelVertexBuffers(
                    app, model, reinterpret_cast<const float*>(&frameWorld));
        }
    }

    DWORD clearFlags = D3DCLEAR_TARGET | D3DCLEAR_ZBUFFER;
    if (sub->d3dInitialized != 0)
        clearFlags |= D3DCLEAR_STENCIL;
    const D3DCOLOR clearColor =
        app->state.blackBackgroundEnabled != 0
            ? D3DCOLOR_XRGB(0, 0, 0)
            : D3DCOLOR_XRGB(255, 255, 255);
    mme::ClearScene(app, device, clearFlags, clearColor, 1.0f, 0);

    app->state.renderPassCount = 1;
    if (FAILED(mme::BeginScene(app, device)))
        return;

    const bool effectRenderer = UseEffectModelRenderer(app);
    if (effectRenderer)
        RenderShadowMap(app, reinterpret_cast<const float*>(&frameWorld));

    // 0x46DDF6..0x46DE5F is deliberately a loop: render callbacks may add
    // another pass by incrementing A0270 while a pass is in progress.
    while (app->state.renderPassCount > 0) {
        --app->state.renderPassCount;
        if (effectRenderer)
            RenderModelsEffect(app,
                reinterpret_cast<const float*>(&frameWorld));
        else
            RenderModelsFixed(app);
    }

    ComposeSelfShadow(app, sub, device);
    ComposeCallbackTexture(app, device);

    // MME: 主渲染目标即将被回读（捕获/AVI 采样）——若本帧后处理链尚未
    // 运行则先触发（对应原版 MMHack 对 UpdateSurface/GetRenderTargetData/
    // StretchRect 槽 30/32/34 的拦截）。
    mme::PreRenderTargetCopy(app, device);

    // 0x46DFDE: mirror the full-size capture RT into the secondary-window
    // back buffer.  The original reacquires the back buffer after a reset.
    if (sub->multisampleAvailable == 0 &&
        app->RecordingWindow() != nullptr) {
        auto** backBufferSlot = &sub->backbufferSurface;
        if (*backBufferSlot == nullptr)
            device->GetBackBuffer(0, 0, D3DBACKBUFFER_TYPE_MONO,
                                  backBufferSlot);
        DownsampleCaptureSurface(app);
    }

    CaptureAccessoryScreenTexture(app, sub, device);

    // 0x46E208: the recording/sub-window path ends the scene here; it never
    // draws debug geometry or the dynamic overlay batches.
    if (app->RecordingWindow() != nullptr) {
        mme::EndScene(app, device);
        DumpAccessoryScreenTexture(app, device);
        return;
    }

    // 0x46E214..0x46E554: accessory-dialog or Bullet collision debug pass.
    if (app->state.rigidBodyDisplayEnabled == 1 ||
        app->state.frameCopyDialog != 0) {
        device->SetRenderState(D3DRS_LIGHTING, FALSE);
        device->SetTexture(0, nullptr);
        device->SetRenderState(D3DRS_ZENABLE, FALSE);
        if (app->state.frameCopyDialog != 0)
            DrawAccessoryDebug(app);
        else
            DrawPhysicsCollisionDebug(app->Physics());
        device->SetRenderState(D3DRS_ZENABLE, TRUE);
        device->SetRenderState(D3DRS_LIGHTING, TRUE);
    }

    // 0x46E556..0x46E592: selected-bone local/global operation axis.
    if (app->state.optflag[0] == 0 &&
        app->PlaybackActive() == 0 &&
        app->state.frameCopyDialog == 0) {
        DrawBoneOperationAxis(app,
            reinterpret_cast<const float*>(&frameWorld));
    }

    device->SetRenderState(D3DRS_ALPHABLENDENABLE, TRUE);
    device->SetRenderState(D3DRS_SRCBLEND, D3DBLEND_SRCALPHA);
    device->SetRenderState(D3DRS_DESTBLEND, D3DBLEND_INVSRCALPHA);

    DrawTextOverlay(app, device);
    DrawLineOverlay(app, sub, device);
    DrawSpriteOverlay(app, device);
    mme::EndScene(app, device);
    DumpAccessoryScreenTexture(app, device);
}

}  // namespace mikudancestudio
