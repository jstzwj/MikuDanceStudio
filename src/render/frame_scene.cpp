#include "mikudancestudio/application_swapchain.hpp"
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
#include <cstdlib>
#include <cstring>
#include <algorithm>
#include <array>
#include <string>
#include <vector>

#include "mikudancestudio/screen_vertex.hpp"
#include "mikudancestudio/screen_capture.hpp"
#include "mikudancestudio/mme_bridge.hpp"
#include "mikudancestudio/mmd_app.hpp"
#include "mikudancestudio/model_cpu_preview.hpp"
#include "mikudancestudio/ported_funcs.hpp"
#include "mikudancestudio/rhi_d3d9.hpp"
#include "mikudancestudio/rhi_assets.hpp"
#include "mikudancestudio/model_batch_bridge.hpp"
#include "mikudancestudio/rhi_scene_history.hpp"
#include "mikudancestudio/rhi_history_replay.hpp"
#include "mikudancestudio/model.hpp"

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

constexpr DWORD kTexturedScreenVertexFvf = D3DFVF_XYZRHW | D3DFVF_DIFFUSE | D3DFVF_TEX1;
constexpr DWORD kColoredScreenVertexFvf = D3DFVF_XYZRHW | D3DFVF_DIFFUSE;

bool SelectionAdapterRequested() {
    char enabled[2]{};
    return GetEnvironmentVariableA("MDS_RHI_SELECTION_ADAPTER", enabled, sizeof(enabled)) == 1 &&
           enabled[0] == '1';
}
bool SpriteAdapterRequested() {
    char enabled[2]{};
    return GetEnvironmentVariableA("MDS_RHI_SPRITE_ADAPTER", enabled, sizeof(enabled)) == 1 &&
           enabled[0] == '1';
}
bool TextAdapterRequested() {
    char enabled[2]{};
    return GetEnvironmentVariableA("MDS_RHI_TEXT_ADAPTER", enabled, sizeof(enabled)) == 1 &&
           enabled[0] == '1';
}
bool CaptureHudSampler(IDirect3DDevice9* device, rhi::SamplerDesc& sampler) {
    DWORD minFilter = 0, magFilter = 0, mipFilter = 0;
    DWORD addressU = 0, addressV = 0, maxAnisotropy = 0;
    if (FAILED(device->GetSamplerState(0, D3DSAMP_MINFILTER, &minFilter)) ||
        FAILED(device->GetSamplerState(0, D3DSAMP_MAGFILTER, &magFilter)) ||
        FAILED(device->GetSamplerState(0, D3DSAMP_MIPFILTER, &mipFilter)) ||
        FAILED(device->GetSamplerState(0, D3DSAMP_ADDRESSU, &addressU)) ||
        FAILED(device->GetSamplerState(0, D3DSAMP_ADDRESSV, &addressV)) ||
        FAILED(device->GetSamplerState(0, D3DSAMP_MAXANISOTROPY, &maxAnisotropy)) ||
        minFilter != D3DTEXF_ANISOTROPIC || magFilter != D3DTEXF_ANISOTROPIC ||
        mipFilter != D3DTEXF_ANISOTROPIC || addressU != D3DTADDRESS_WRAP ||
        addressV != D3DTADDRESS_WRAP || maxAnisotropy == 0)
        return false;
    sampler.maxAnisotropy = maxAnisotropy;
    return true;
}
bool CaptureHudTexture(IDirect3DTexture9* source, rhi::TextureDesc& description,
                       std::vector<std::uint8_t>& rgba) {
    if (!source || source->GetLevelCount() != 1)
        return false;
    D3DSURFACE_DESC native{};
    if (FAILED(source->GetLevelDesc(0, &native)) || native.Format != D3DFMT_A8R8G8B8 ||
        !native.Width || !native.Height || native.Width > 8192 || native.Height > 8192)
        return false;
    D3DLOCKED_RECT locked{};
    if (FAILED(source->LockRect(0, &locked, nullptr, D3DLOCK_READONLY)))
        return false;
    description = {native.Width, native.Height, rhi::TextureFormat::Rgba8};
    const std::size_t pitch = static_cast<std::size_t>(native.Width) * 4u;
    if (locked.Pitch < 0 || static_cast<std::size_t>(locked.Pitch) < pitch) {
        source->UnlockRect(0);
        return false;
    }
    rgba.resize(pitch * native.Height);
    for (UINT y = 0; y < native.Height; ++y) {
        const auto* from = static_cast<const std::uint8_t*>(locked.pBits) + y * locked.Pitch;
        auto* to = rgba.data() + y * pitch;
        for (std::size_t x = 0; x < pitch; x += 4) {
            to[x] = from[x + 2];
            to[x + 1] = from[x + 1];
            to[x + 2] = from[x];
            to[x + 3] = from[x + 3];
        }
    }
    return SUCCEEDED(source->UnlockRect(0));
}

const rhi::AtlasAsset* PortableFontAtlas() {
    char enabled[2]{};
    if (GetEnvironmentVariableA("MDS_RHI_PORTABLE_FONT", enabled, sizeof(enabled)) != 1 ||
        enabled[0] != '1')
        return nullptr;
    wchar_t directory[MAX_PATH]{};
    const DWORD length = GetEnvironmentVariableW(L"MDS_RHI_PORTABLE_FONT_DIR", directory, MAX_PATH);
    if (!length || length >= MAX_PATH)
        return nullptr;
    static std::wstring lastDirectory;
    static rhi::AtlasAsset atlas;
    if (lastDirectory != directory) {
        lastDirectory = directory;
        rhi::LoadAtlas(rhi::FileAssetSource(directory), rhi::AssetId::SceneFont, atlas);
    }
    return atlas.revision && atlas.image.width == 512 && atlas.image.height == 512 &&
                   atlas.image.pixels.size() == 512u * 512u * 4u
               ? &atlas
               : nullptr;
}

#ifdef MIKUDANCESTUDIO_DIAG
void ReportPortableFontDifference(IDirect3DTexture9* legacy, const rhi::AtlasAsset& candidate) {
    static bool reported = false;
    if (reported)
        return;
    char directory[MAX_PATH]{};
    const DWORD length = GetEnvironmentVariableA("MDS_RHI_LIVE_COMPARE_DIR", directory, MAX_PATH);
    if (!length || length >= MAX_PATH)
        return;
    reported = true;
    rhi::TextureDesc description{};
    std::vector<std::uint8_t> oldPixels;
    const bool captured = CaptureHudTexture(legacy, description, oldPixels);
    const auto& pixels = candidate.image.pixels;
    std::size_t differences = 0;
    if (captured && description.width == candidate.image.width &&
        description.height == candidate.image.height && oldPixels.size() == pixels.size()) {
        for (std::size_t index = 0; index < pixels.size(); ++index)
            differences += oldPixels[index] != pixels[index];
    }
    char path[MAX_PATH]{};
    const int pathLength =
        std::snprintf(path, sizeof(path), "%s\\text_overlay.portable_source", directory);
    if (pathLength <= 0 || pathLength >= static_cast<int>(sizeof(path)))
        return;
    FILE* file = nullptr;
    if (fopen_s(&file, path, "wb") == 0 && file) {
        std::fprintf(file, "status=%s differentChannels=%zu bytes=%zu\n",
                     captured && description.width == candidate.image.width &&
                             description.height == candidate.image.height &&
                             oldPixels.size() == pixels.size()
                         ? "COMPARED"
                         : "FAILED",
                     differences, pixels.size());
        std::fclose(file);
    }
}

void ReportOverlayAtlasParity(IDirect3DTexture9* legacy, rhi::OverlayAtlasSlot slot,
                              const std::uint8_t* pixels, std::size_t bytes, std::uint32_t width,
                              std::uint32_t height) {
    static bool checkedSprite = false, checkedText = false;
    bool& checked = slot == rhi::OverlayAtlasSlot::Text ? checkedText : checkedSprite;
    if (checked || !pixels)
        return;
    char directory[MAX_PATH]{};
    const DWORD length = GetEnvironmentVariableA("MDS_RHI_LIVE_COMPARE_DIR", directory, MAX_PATH);
    if (!length || length >= MAX_PATH)
        return;
    checked = true;
    rhi::TextureDesc legacyDescription{};
    std::vector<std::uint8_t> oldPixels;
    const bool captured = CaptureHudTexture(legacy, legacyDescription, oldPixels);
    std::size_t differences = 0;
    if (captured && legacyDescription.width == width && legacyDescription.height == height &&
        oldPixels.size() == bytes) {
        for (std::size_t index = 0; index < bytes; ++index)
            differences += oldPixels[index] != pixels[index];
    }
    char path[MAX_PATH]{};
    const int pathLength = std::snprintf(
        path, sizeof(path), "%s\\%s", directory,
        slot == rhi::OverlayAtlasSlot::Text ? "text_overlay.source" : "sprite_overlay.source");
    if (pathLength <= 0 || pathLength >= static_cast<int>(sizeof(path)))
        return;
    FILE* file = nullptr;
    if (fopen_s(&file, path, "wb") == 0 && file) {
        std::fprintf(file, "status=%s differences=%zu bytes=%zu\n",
                     captured && legacyDescription.width == width &&
                             legacyDescription.height == height && oldPixels.size() == bytes &&
                             differences == 0
                         ? "PASS"
                         : "FAIL",
                     differences, bytes);
        std::fclose(file);
    }
}

void ReportOverlayCacheReuse(IDirect3DDevice9* device, rhi::OverlayAtlasSlot slot) {
    const auto stats = rhi::ApplicationOverlayCacheStats(device, slot);
    if (stats.uploads != 1 || stats.hits != 1)
        return;
    char directory[MAX_PATH]{};
    const DWORD length = GetEnvironmentVariableA("MDS_RHI_LIVE_COMPARE_DIR", directory, MAX_PATH);
    if (!length || length >= MAX_PATH)
        return;
    char path[MAX_PATH]{};
    const int pathLength = std::snprintf(
        path, sizeof(path), "%s\\%s", directory,
        slot == rhi::OverlayAtlasSlot::Text ? "text_overlay.cache" : "sprite_overlay.cache");
    if (pathLength <= 0 || pathLength >= static_cast<int>(sizeof(path)))
        return;
    FILE* file = nullptr;
    if (fopen_s(&file, path, "wb") == 0 && file) {
        std::fprintf(file, "uploads=%llu hits=%llu\n",
                     static_cast<unsigned long long>(stats.uploads),
                     static_cast<unsigned long long>(stats.hits));
        std::fclose(file);
    }
}

void DumpSelectionDrawState(MMDApp* app, IDirect3DDevice9* device) {
    if (app->LineOverlayPrimitiveCount() != 4 ||
        std::abs(app->BoneBoxStartX() - app->MouseX()) < 20 ||
        std::abs(app->BoneBoxStartY() - app->MouseY()) < 20)
        return;
    static LONG captured = 0;
    char directory[MAX_PATH]{};
    const DWORD length =
        GetEnvironmentVariableA("MIKUDANCESTUDIO_VB_DUMP_DIR", directory, MAX_PATH);
    if (!length || length >= MAX_PATH || InterlockedCompareExchange(&captured, 1, 0) != 0)
        return;
    char path[MAX_PATH]{};
    const int pathLength = std::snprintf(path, sizeof(path), "%s\\line_selection.state", directory);
    if (pathLength < 0 || pathLength >= static_cast<int>(sizeof(path)))
        return;
    D3DVIEWPORT9 viewport{};
    DWORD fvf = 0;
    DWORD lighting = 0, cull = 0, depth = 0, depthWrite = 0, depthFunc = 0;
    DWORD blend = 0, source = 0, destination = 0, colorOp = 0, colorArg = 0;
    DWORD colorArg2 = 0, alphaOp = 0, alphaArg = 0, alphaArg2 = 0;
    DWORD scissor = 0, colorWrite = 0;
    DWORD stencil = 0, stencilFunc = 0, stencilRef = 0, stencilMask = 0;
    DWORD stencilFail = 0, stencilZFail = 0, stencilPass = 0, stencilWriteMask = 0;
    DWORD alphaTest = 0, alphaFunc = 0, alphaRef = 0;
    DWORD depthBias = 0, slopeDepthBias = 0;
    DWORD stage1ColorOp = 0, stage1AlphaOp = 0;
    IDirect3DSurface9* target = nullptr;
    IDirect3DSurface9* depthSurface = nullptr;
    IDirect3DVertexBuffer9* stream = nullptr;
    IDirect3DBaseTexture9* texture = nullptr;
    IDirect3DVertexShader9* vertexShader = nullptr;
    IDirect3DPixelShader9* pixelShader = nullptr;
    UINT offset = 0, stride = 0;
    const bool valid =
        SUCCEEDED(device->GetViewport(&viewport)) && SUCCEEDED(device->GetFVF(&fvf)) &&
        SUCCEEDED(device->GetRenderState(D3DRS_LIGHTING, &lighting)) &&
        SUCCEEDED(device->GetRenderState(D3DRS_CULLMODE, &cull)) &&
        SUCCEEDED(device->GetRenderState(D3DRS_ZENABLE, &depth)) &&
        SUCCEEDED(device->GetRenderState(D3DRS_ZWRITEENABLE, &depthWrite)) &&
        SUCCEEDED(device->GetRenderState(D3DRS_ZFUNC, &depthFunc)) &&
        SUCCEEDED(device->GetRenderState(D3DRS_ALPHABLENDENABLE, &blend)) &&
        SUCCEEDED(device->GetRenderState(D3DRS_SRCBLEND, &source)) &&
        SUCCEEDED(device->GetRenderState(D3DRS_DESTBLEND, &destination)) &&
        SUCCEEDED(device->GetTextureStageState(0, D3DTSS_COLOROP, &colorOp)) &&
        SUCCEEDED(device->GetTextureStageState(0, D3DTSS_COLORARG1, &colorArg)) &&
        SUCCEEDED(device->GetTextureStageState(0, D3DTSS_COLORARG2, &colorArg2)) &&
        SUCCEEDED(device->GetTextureStageState(0, D3DTSS_ALPHAOP, &alphaOp)) &&
        SUCCEEDED(device->GetTextureStageState(0, D3DTSS_ALPHAARG1, &alphaArg)) &&
        SUCCEEDED(device->GetTextureStageState(0, D3DTSS_ALPHAARG2, &alphaArg2)) &&
        SUCCEEDED(device->GetTextureStageState(1, D3DTSS_COLOROP, &stage1ColorOp)) &&
        SUCCEEDED(device->GetTextureStageState(1, D3DTSS_ALPHAOP, &stage1AlphaOp)) &&
        SUCCEEDED(device->GetRenderState(D3DRS_SCISSORTESTENABLE, &scissor)) &&
        SUCCEEDED(device->GetRenderState(D3DRS_COLORWRITEENABLE, &colorWrite)) &&
        SUCCEEDED(device->GetRenderState(D3DRS_STENCILENABLE, &stencil)) &&
        SUCCEEDED(device->GetRenderState(D3DRS_STENCILFUNC, &stencilFunc)) &&
        SUCCEEDED(device->GetRenderState(D3DRS_STENCILREF, &stencilRef)) &&
        SUCCEEDED(device->GetRenderState(D3DRS_STENCILMASK, &stencilMask)) &&
        SUCCEEDED(device->GetRenderState(D3DRS_STENCILFAIL, &stencilFail)) &&
        SUCCEEDED(device->GetRenderState(D3DRS_STENCILZFAIL, &stencilZFail)) &&
        SUCCEEDED(device->GetRenderState(D3DRS_STENCILPASS, &stencilPass)) &&
        SUCCEEDED(device->GetRenderState(D3DRS_STENCILWRITEMASK, &stencilWriteMask)) &&
        SUCCEEDED(device->GetRenderState(D3DRS_ALPHATESTENABLE, &alphaTest)) &&
        SUCCEEDED(device->GetRenderState(D3DRS_ALPHAFUNC, &alphaFunc)) &&
        SUCCEEDED(device->GetRenderState(D3DRS_ALPHAREF, &alphaRef)) &&
        SUCCEEDED(device->GetRenderState(D3DRS_DEPTHBIAS, &depthBias)) &&
        SUCCEEDED(device->GetRenderState(D3DRS_SLOPESCALEDEPTHBIAS, &slopeDepthBias)) &&
        SUCCEEDED(device->GetRenderTarget(0, &target)) &&
        SUCCEEDED(device->GetDepthStencilSurface(&depthSurface)) &&
        SUCCEEDED(device->GetStreamSource(0, &stream, &offset, &stride)) &&
        SUCCEEDED(device->GetTexture(0, &texture)) &&
        SUCCEEDED(device->GetVertexShader(&vertexShader)) &&
        SUCCEEDED(device->GetPixelShader(&pixelShader));
    D3DSURFACE_DESC description{};
    D3DSURFACE_DESC depthDescription{};
    const bool ready = valid && target && depthSurface && stream &&
                       SUCCEEDED(target->GetDesc(&description)) &&
                       SUCCEEDED(depthSurface->GetDesc(&depthDescription));
    if (ready) {
        char data[1024]{};
        const int size = std::snprintf(
            data, sizeof(data),
            "target=%ux%u format=%u viewport=%u,%u,%u,%u minZ=%g maxZ=%g "
            "fvf=%lu offset=%u stride=%u texture=%u "
            "lighting=%lu cull=%lu depth=%lu depthWrite=%lu depthFunc=%lu "
            "blend=%lu source=%lu destination=%lu colorOp=%lu colorArg=%lu "
            "colorArg2=%lu alphaOp=%lu alphaArg=%lu alphaArg2=%lu "
            "vertexShader=%u pixelShader=%u scissor=%lu colorWrite=%lu "
            "stencil=%lu stencilFunc=%lu stencilRef=%lu stencilMask=%lu "
            "stencilFail=%lu stencilZFail=%lu stencilPass=%lu stencilWriteMask=%lu "
            "alphaTest=%lu alphaFunc=%lu alphaRef=%lu "
            "depthBias=%lu slopeDepthBias=%lu "
            "stage1ColorOp=%lu stage1AlphaOp=%lu "
            "samples=%u sampleQuality=%u depthFormat=%u depthSamples=%u depthQuality=%u\n",
            description.Width, description.Height, description.Format, viewport.X, viewport.Y,
            viewport.Width, viewport.Height, viewport.MinZ, viewport.MaxZ, fvf, offset, stride,
            texture != nullptr, lighting, cull, depth, depthWrite, depthFunc, blend, source,
            destination, colorOp, colorArg, colorArg2, alphaOp, alphaArg, alphaArg2,
            vertexShader != nullptr, pixelShader != nullptr, scissor, colorWrite, stencil,
            stencilFunc, stencilRef, stencilMask, stencilFail, stencilZFail, stencilPass,
            stencilWriteMask, alphaTest, alphaFunc, alphaRef, depthBias, slopeDepthBias,
            stage1ColorOp, stage1AlphaOp, description.MultiSampleType,
            description.MultiSampleQuality, depthDescription.Format,
            depthDescription.MultiSampleType, depthDescription.MultiSampleQuality);
        HANDLE file = CreateFileA(path, GENERIC_WRITE, FILE_SHARE_READ, nullptr, CREATE_ALWAYS,
                                  FILE_ATTRIBUTE_NORMAL, nullptr);
        if (file != INVALID_HANDLE_VALUE) {
            DWORD written = 0;
            if (size > 0 && size < static_cast<int>(sizeof(data)))
                WriteFile(file, data, static_cast<DWORD>(size), &written, nullptr);
            CloseHandle(file);
        }
    }
    if (texture)
        texture->Release();
    if (vertexShader)
        vertexShader->Release();
    if (pixelShader)
        pixelShader->Release();
    if (stream)
        stream->Release();
    if (depthSurface)
        depthSurface->Release();
    if (target)
        target->Release();
}

struct SelectionLiveCapture {
    IDirect3DQuery9* visibilityQuery = nullptr;
    IDirect3DQuery9* visibilityModes[10]{};
    IDirect3DStateBlock9* drawState = nullptr;
    IDirect3DVertexBuffer9* drawStream = nullptr;
    UINT drawOffset = 0;
    UINT drawStride = 0;
    D3DVERTEXBUFFER_DESC streamDescription{};
    HRESULT streamDescriptionStatus = E_FAIL;
    HRESULT streamReadStatus = E_FAIL;
    HRESULT streamUnlockStatus = E_FAIL;
    std::array<ColoredScreenVertex, 8> streamVertices{};
    bool streamBytesReady = false;
    bool streamVerticesMatch = false;
    LONG skippedStaleFrames = 0;
    bool streamSameAfter = false;
    IDirect3DSurface9* target = nullptr;
    IDirect3DSurface9* before = nullptr;
    IDirect3DSurface9* immediate = nullptr;
    IDirect3DSurface9* after = nullptr;
    IDirect3DSurface9* sameFrameClone = nullptr;
    IDirect3DSurface9* sameFrameAdapterClone = nullptr;
    IDirect3DSurface9* sameFrameLegacyResult = nullptr;
    IDirect3DSurface9* sameFrameAdapterResult = nullptr;
    HRESULT sameFrameCloneStatus = E_FAIL;
    HRESULT sameFrameProbeStatus = E_FAIL;
    HRESULT sameFrameLegacyDrawStatus = E_FAIL;
    HRESULT sameFrameAdapterDrawStatus = E_FAIL;
    bool sameFrameAdapterApplied = false;
    D3DSURFACE_DESC description{};
    mme::LineDrawBridgeProbe bridgeBefore{};
    mme::LineDrawBridgeProbe bridgeAfter{};
    mme::DrawSnapshotProbe hostBefore{};
    mme::DrawSnapshotProbe hostAfter{};
    HRESULT beforeStatus = E_FAIL;
    HRESULT immediateStatus = E_FAIL;
    HRESULT afterStatus = E_FAIL;
    HRESULT drawStatus = E_FAIL;
    int left = 0, top = 0, right = 0, bottom = 0;
    char directory[MAX_PATH]{};
    bool active = false;
    bool effectsActive = false;
    bool adapter = false;
    bool adapterApplied = false;
};

SelectionLiveCapture g_selectionLiveCapture;
LONG g_selectionAbStage = 0;
struct SelectionAbReference {
    std::vector<DWORD> before;
    std::vector<DWORD> immediate;
    int left = 0, top = 0, right = 0, bottom = 0;
    UINT width = 0, height = 0;
    bool valid = false;
};
SelectionAbReference g_selectionAbReference;

bool SelectionAbEnabled() {
    char value[2]{};
    return GetEnvironmentVariableA("MDS_RHI_LIVE_AB", value, sizeof(value)) == 1 && value[0] == '1';
}

void ProbeSelectionVisibility(IDirect3DDevice9* device) {
    auto& capture = g_selectionLiveCapture;
    if (!capture.active)
        return;
    IDirect3DStateBlock9* saved = nullptr;
    if (FAILED(device->CreateStateBlock(D3DSBT_ALL, &saved)) || !saved)
        return;
    const bool ready = SUCCEEDED(saved->Capture()) && capture.drawState &&
                       SUCCEEDED(capture.drawState->Apply()) &&
                       SUCCEEDED(device->SetRenderState(D3DRS_COLORWRITEENABLE, 0)) &&
                       SUCCEEDED(device->SetRenderState(D3DRS_ZWRITEENABLE, FALSE)) &&
                       SUCCEEDED(device->SetRenderState(D3DRS_STENCILFAIL, D3DSTENCILOP_KEEP)) &&
                       SUCCEEDED(device->SetRenderState(D3DRS_STENCILZFAIL, D3DSTENCILOP_KEEP)) &&
                       SUCCEEDED(device->SetRenderState(D3DRS_STENCILPASS, D3DSTENCILOP_KEEP));
    if (ready) {
        for (unsigned mode = 0; mode < 10; ++mode) {
            if (FAILED(capture.drawState->Apply()) ||
                FAILED(device->SetRenderState(D3DRS_COLORWRITEENABLE, 0)) ||
                FAILED(device->SetRenderState(D3DRS_ZWRITEENABLE, FALSE)) ||
                FAILED(device->SetRenderState(D3DRS_STENCILFAIL, D3DSTENCILOP_KEEP)) ||
                FAILED(device->SetRenderState(D3DRS_STENCILZFAIL, D3DSTENCILOP_KEEP)) ||
                FAILED(device->SetRenderState(D3DRS_STENCILPASS, D3DSTENCILOP_KEEP)) ||
                !capture.drawStream ||
                FAILED(device->SetStreamSource(0, capture.drawStream, capture.drawOffset,
                                               capture.drawStride)) ||
                FAILED(
                    device->SetRenderState(D3DRS_ZENABLE, mode == 0 || mode == 2 || mode >= 8)) ||
                FAILED(device->SetRenderState(D3DRS_STENCILENABLE,
                                              mode == 0 || mode == 1 || mode >= 8)) ||
                FAILED(device->SetRenderState(D3DRS_ALPHATESTENABLE, mode < 4 || mode >= 8)) ||
                FAILED(
                    device->CreateQuery(D3DQUERYTYPE_OCCLUSION, &capture.visibilityModes[mode])) ||
                !capture.visibilityModes[mode])
                break;
            if (mode >= 5 && mode < 8 &&
                (FAILED(device->SetTextureStageState(0, D3DTSS_COLOROP, D3DTOP_SELECTARG1)) ||
                 FAILED(device->SetTextureStageState(0, D3DTSS_COLORARG1, D3DTA_DIFFUSE)) ||
                 FAILED(device->SetTextureStageState(0, D3DTSS_ALPHAOP, D3DTOP_SELECTARG1)) ||
                 FAILED(device->SetTextureStageState(0, D3DTSS_ALPHAARG1, D3DTA_DIFFUSE)) ||
                 FAILED(device->SetTextureStageState(1, D3DTSS_COLOROP, D3DTOP_DISABLE))))
                break;
            auto* query = capture.visibilityModes[mode];
            const ColoredScreenVertex controlLine[2] = {
                {static_cast<float>(capture.left), static_cast<float>(capture.top), 0.0f, 1.0f,
                 0xffffffffu},
                {static_cast<float>(capture.right), static_cast<float>(capture.bottom), 0.0f, 1.0f,
                 0xffffffffu},
            };
            const ColoredScreenVertex boxLines[8] = {
                {static_cast<float>(capture.left), static_cast<float>(capture.top), 0.0f, 1.0f,
                 0xffffffffu},
                {static_cast<float>(capture.left), static_cast<float>(capture.bottom), 0.0f, 1.0f,
                 0xffffffffu},
                {static_cast<float>(capture.left), static_cast<float>(capture.bottom), 0.0f, 1.0f,
                 0xffffffffu},
                {static_cast<float>(capture.right), static_cast<float>(capture.bottom), 0.0f, 1.0f,
                 0xffffffffu},
                {static_cast<float>(capture.right), static_cast<float>(capture.bottom), 0.0f, 1.0f,
                 0xffffffffu},
                {static_cast<float>(capture.right), static_cast<float>(capture.top), 0.0f, 1.0f,
                 0xffffffffu},
                {static_cast<float>(capture.right), static_cast<float>(capture.top), 0.0f, 1.0f,
                 0xffffffffu},
                {static_cast<float>(capture.left), static_cast<float>(capture.top), 0.0f, 1.0f,
                 0xffffffffu},
            };
            if (FAILED(query->Issue(D3DISSUE_BEGIN)) ||
                FAILED(
                    mode == 9 && capture.streamBytesReady
                        ? device->DrawPrimitiveUP(D3DPT_LINELIST, 4, capture.streamVertices.data(),
                                                  sizeof(ColoredScreenVertex))
                    : mode == 6 ? device->DrawPrimitiveUP(D3DPT_LINELIST, 1, controlLine,
                                                          sizeof(ColoredScreenVertex))
                    : mode == 7 || mode == 8 ? device->DrawPrimitiveUP(D3DPT_LINELIST, 4, boxLines,
                                                                       sizeof(ColoredScreenVertex))
                                             : device->DrawPrimitive(D3DPT_LINELIST, 0, 4)) ||
                FAILED(query->Issue(D3DISSUE_END)))
                break;
        }
    }
    saved->Apply();
    saved->Release();
}

void BeginSelectionLiveCapture(MMDApp* app, IDirect3DDevice9* device) {
    if (app->LineOverlayPrimitiveCount() != 4 ||
        std::abs(app->BoneBoxStartX() - app->MouseX()) < 20 ||
        std::abs(app->BoneBoxStartY() - app->MouseY()) < 20)
        return;
    auto& capture = g_selectionLiveCapture;
    if (capture.active)
        return;
    static LONG started = 0;
    static LONG skippedStaleFrames = 0;
    char directory[MAX_PATH]{};
    const DWORD length = GetEnvironmentVariableA("MDS_RHI_LIVE_COMPARE_DIR", directory, MAX_PATH);
    if (!length || length >= MAX_PATH || g_selectionAbStage >= (SelectionAbEnabled() ? 2 : 1) ||
        InterlockedCompareExchange(&started, 0, 0) != g_selectionAbStage)
        return;
    // The line producer runs after rendering. On the first mouse-move frame
    // its VB still holds the collapsed rectangle from the previous frame.
    // Wait for the exact bytes that the draw will consume before latching a
    // one-shot capture; otherwise this diagnostic compares different frames.
    IDirect3DVertexBuffer9* candidate = nullptr;
    UINT candidateOffset = 0, candidateStride = 0;
    bool currentBoxReady = false;
    if (SUCCEEDED(device->GetStreamSource(0, &candidate, &candidateOffset, &candidateStride)) &&
        candidate && candidateStride == sizeof(ColoredScreenVertex)) {
        void* bytes = nullptr;
        if (SUCCEEDED(
                candidate->Lock(candidateOffset, sizeof(ColoredScreenVertex) * 8, &bytes, 0)) &&
            bytes) {
            const float left = static_cast<float>((std::min)(app->BoneBoxStartX(), app->MouseX()));
            const float right = static_cast<float>((std::max)(app->BoneBoxStartX(), app->MouseX()));
            const float top = static_cast<float>((std::min)(app->BoneBoxStartY(), app->MouseY()));
            const float bottom =
                static_cast<float>((std::max)(app->BoneBoxStartY(), app->MouseY()));
            const ColoredScreenVertex expected[8] = {
                {left, top, 0.0f, 1.0f, 0xffffffffu},     {left, bottom, 0.0f, 1.0f, 0xffffffffu},
                {left, bottom, 0.0f, 1.0f, 0xffffffffu},  {right, bottom, 0.0f, 1.0f, 0xffffffffu},
                {right, bottom, 0.0f, 1.0f, 0xffffffffu}, {right, top, 0.0f, 1.0f, 0xffffffffu},
                {right, top, 0.0f, 1.0f, 0xffffffffu},    {left, top, 0.0f, 1.0f, 0xffffffffu},
            };
            currentBoxReady = std::memcmp(bytes, expected, sizeof(expected)) == 0;
            candidate->Unlock();
        }
    }
    if (candidate)
        candidate->Release();
    if (!currentBoxReady) {
        InterlockedIncrement(&skippedStaleFrames);
        return;
    }
    if (InterlockedCompareExchange(&started, g_selectionAbStage + 1, g_selectionAbStage) !=
        g_selectionAbStage)
        return;
    std::memcpy(capture.directory, directory, length + 1);
    capture.active = true;
    capture.adapter = g_selectionAbStage == 1;
    capture.skippedStaleFrames = skippedStaleFrames;
    capture.effectsActive = mme::EffectsActive();
    capture.left = (std::min)(app->BoneBoxStartX(), app->MouseX());
    capture.right = (std::max)(app->BoneBoxStartX(), app->MouseX());
    capture.top = (std::min)(app->BoneBoxStartY(), app->MouseY());
    capture.bottom = (std::max)(app->BoneBoxStartY(), app->MouseY());
    capture.bridgeBefore = mme::ProbeLineDrawBridge();
    capture.hostBefore = mme::ProbeDrawSnapshot();
    device->GetStreamSource(0, &capture.drawStream, &capture.drawOffset, &capture.drawStride);
    if (capture.drawStream) {
        capture.streamDescriptionStatus = capture.drawStream->GetDesc(&capture.streamDescription);
        void* bytes = nullptr;
        capture.streamReadStatus =
            capture.drawStream->Lock(capture.drawOffset, sizeof(capture.streamVertices), &bytes, 0);
        if (SUCCEEDED(capture.streamReadStatus) && bytes) {
            std::memcpy(capture.streamVertices.data(), bytes, sizeof(capture.streamVertices));
            capture.streamBytesReady = true;
            const float left = static_cast<float>(capture.left);
            const float right = static_cast<float>(capture.right);
            const float top = static_cast<float>(capture.top);
            const float bottom = static_cast<float>(capture.bottom);
            const ColoredScreenVertex expected[8] = {
                {left, top, 0.0f, 1.0f, 0xffffffffu},     {left, bottom, 0.0f, 1.0f, 0xffffffffu},
                {left, bottom, 0.0f, 1.0f, 0xffffffffu},  {right, bottom, 0.0f, 1.0f, 0xffffffffu},
                {right, bottom, 0.0f, 1.0f, 0xffffffffu}, {right, top, 0.0f, 1.0f, 0xffffffffu},
                {right, top, 0.0f, 1.0f, 0xffffffffu},    {left, top, 0.0f, 1.0f, 0xffffffffu},
            };
            capture.streamVerticesMatch =
                std::memcmp(expected, capture.streamVertices.data(), sizeof(expected)) == 0;
            capture.streamUnlockStatus = capture.drawStream->Unlock();
        }
    }
    if (SUCCEEDED(device->CreateStateBlock(D3DSBT_ALL, &capture.drawState)) && capture.drawState)
        capture.drawState->Capture();
    device->CreateQuery(D3DQUERYTYPE_OCCLUSION, &capture.visibilityQuery);
    if (SUCCEEDED(device->GetRenderTarget(0, &capture.target)) && capture.target &&
        SUCCEEDED(capture.target->GetDesc(&capture.description))) {
        const auto& desc = capture.description;
        capture.beforeStatus =
            device->CreateRenderTarget(desc.Width, desc.Height, desc.Format, D3DMULTISAMPLE_NONE, 0,
                                       FALSE, &capture.before, nullptr);
        if (SUCCEEDED(capture.beforeStatus))
            capture.beforeStatus = device->CreateRenderTarget(desc.Width, desc.Height, desc.Format,
                                                              D3DMULTISAMPLE_NONE, 0, FALSE,
                                                              &capture.immediate, nullptr);
        if (SUCCEEDED(capture.beforeStatus))
            capture.beforeStatus =
                device->CreateRenderTarget(desc.Width, desc.Height, desc.Format,
                                           D3DMULTISAMPLE_NONE, 0, FALSE, &capture.after, nullptr);
        if (SUCCEEDED(capture.beforeStatus))
            capture.beforeStatus =
                device->StretchRect(capture.target, nullptr, capture.before, nullptr, D3DTEXF_NONE);
        if (SelectionAbEnabled() && !capture.adapter) {
            capture.sameFrameCloneStatus = device->CreateRenderTarget(
                desc.Width, desc.Height, desc.Format, desc.MultiSampleType, desc.MultiSampleQuality,
                FALSE, &capture.sameFrameClone, nullptr);
            if (SUCCEEDED(capture.sameFrameCloneStatus))
                capture.sameFrameCloneStatus = device->CreateRenderTarget(
                    desc.Width, desc.Height, desc.Format, desc.MultiSampleType,
                    desc.MultiSampleQuality, FALSE, &capture.sameFrameAdapterClone, nullptr);
            if (SUCCEEDED(capture.sameFrameCloneStatus))
                capture.sameFrameCloneStatus = device->CreateRenderTarget(
                    desc.Width, desc.Height, desc.Format, D3DMULTISAMPLE_NONE, 0, FALSE,
                    &capture.sameFrameLegacyResult, nullptr);
            if (SUCCEEDED(capture.sameFrameCloneStatus))
                capture.sameFrameCloneStatus = device->CreateRenderTarget(
                    desc.Width, desc.Height, desc.Format, D3DMULTISAMPLE_NONE, 0, FALSE,
                    &capture.sameFrameAdapterResult, nullptr);
            if (SUCCEEDED(capture.sameFrameCloneStatus))
                capture.sameFrameCloneStatus = device->StretchRect(
                    capture.target, nullptr, capture.sameFrameClone, nullptr, D3DTEXF_NONE);
            if (SUCCEEDED(capture.sameFrameCloneStatus))
                capture.sameFrameCloneStatus = device->StretchRect(
                    capture.target, nullptr, capture.sameFrameAdapterClone, nullptr, D3DTEXF_NONE);
        }
    }
}

void ProbeSelectionSameFrame(IDirect3DDevice9* device) {
    auto& capture = g_selectionLiveCapture;
    if (!capture.active || capture.adapter || FAILED(capture.sameFrameCloneStatus) ||
        !capture.drawState || !capture.drawStream || !capture.streamBytesReady)
        return;
    IDirect3DStateBlock9* previous = nullptr;
    if (FAILED(device->CreateStateBlock(D3DSBT_ALL, &previous)) || !previous)
        return;
    HRESULT status = previous->Capture();
    if (SUCCEEDED(status))
        status = capture.drawState->Apply();
    if (SUCCEEDED(status))
        status = device->SetRenderTarget(0, capture.sameFrameClone);
    if (SUCCEEDED(status))
        status = device->SetRenderState(D3DRS_ZWRITEENABLE, FALSE);
    if (SUCCEEDED(status))
        status = device->SetRenderState(D3DRS_STENCILFAIL, D3DSTENCILOP_KEEP);
    if (SUCCEEDED(status))
        status = device->SetRenderState(D3DRS_STENCILZFAIL, D3DSTENCILOP_KEEP);
    if (SUCCEEDED(status))
        status = device->SetRenderState(D3DRS_STENCILPASS, D3DSTENCILOP_KEEP);
    if (SUCCEEDED(status))
        status =
            device->SetStreamSource(0, capture.drawStream, capture.drawOffset, capture.drawStride);
    if (SUCCEEDED(status)) {
        capture.sameFrameLegacyDrawStatus = device->DrawPrimitive(D3DPT_LINELIST, 0, 4);
        status = capture.sameFrameLegacyDrawStatus;
    }
    if (SUCCEEDED(status))
        status = capture.drawState->Apply();
    if (SUCCEEDED(status))
        status = device->SetRenderTarget(0, capture.sameFrameAdapterClone);
    if (SUCCEEDED(status)) {
        long result = E_FAIL;
        capture.sameFrameAdapterApplied = rhi::DrawSelectionBoxInScene(
            device, capture.streamVertices.data(), sizeof(capture.streamVertices),
            [](IDirect3DDevice9* current, unsigned first, unsigned primitives) {
                return current->DrawPrimitive(D3DPT_LINELIST, first, primitives);
            },
            &result, true);
        capture.sameFrameAdapterDrawStatus = static_cast<HRESULT>(result);
        if (!capture.sameFrameAdapterApplied)
            status = FAILED(result) ? static_cast<HRESULT>(result) : E_FAIL;
    }
    // D3D9 state blocks do not own the render target. Restore it explicitly.
    const HRESULT targetRestore = device->SetRenderTarget(0, capture.target);
    const HRESULT stateRestore = previous->Apply();
    previous->Release();
    if (SUCCEEDED(status) && FAILED(targetRestore))
        status = targetRestore;
    if (SUCCEEDED(status) && FAILED(stateRestore))
        status = stateRestore;
    if (SUCCEEDED(status))
        status = device->StretchRect(capture.sameFrameClone, nullptr, capture.sameFrameLegacyResult,
                                     nullptr, D3DTEXF_NONE);
    if (SUCCEEDED(status))
        status = device->StretchRect(capture.sameFrameAdapterClone, nullptr,
                                     capture.sameFrameAdapterResult, nullptr, D3DTEXF_NONE);
    capture.sameFrameProbeStatus = status;
}

void EndSelectionLiveCapture(IDirect3DDevice9* device, HRESULT drawStatus) {
    auto& capture = g_selectionLiveCapture;
    if (!capture.active)
        return;
    capture.drawStatus = drawStatus;
    if (capture.visibilityQuery)
        capture.visibilityQuery->Issue(D3DISSUE_END);
    ProbeSelectionVisibility(device);
    capture.bridgeAfter = mme::ProbeLineDrawBridge();
    capture.hostAfter = mme::ProbeDrawSnapshot();
    IDirect3DVertexBuffer9* currentStream = nullptr;
    UINT currentOffset = 0, currentStride = 0;
    if (SUCCEEDED(device->GetStreamSource(0, &currentStream, &currentOffset, &currentStride)))
        capture.streamSameAfter = currentStream == capture.drawStream &&
                                  currentOffset == capture.drawOffset &&
                                  currentStride == capture.drawStride;
    if (currentStream)
        currentStream->Release();
    if (SUCCEEDED(capture.beforeStatus))
        capture.immediateStatus =
            device->StretchRect(capture.target, nullptr, capture.immediate, nullptr, D3DTEXF_NONE);
}

bool ReadSelectionLivePixels(IDirect3DDevice9* device, IDirect3DSurface9* source,
                             IDirect3DSurface9* system, UINT width, UINT height,
                             std::vector<DWORD>& pixels) {
    if (!source || FAILED(device->GetRenderTargetData(source, system)))
        return false;
    D3DLOCKED_RECT locked{};
    if (FAILED(system->LockRect(&locked, nullptr, D3DLOCK_READONLY)))
        return false;
    pixels.resize(static_cast<std::size_t>(width) * height);
    for (UINT row = 0; row < height; ++row)
        std::memcpy(pixels.data() + static_cast<std::size_t>(row) * width,
                    static_cast<const std::uint8_t*>(locked.pBits) + row * locked.Pitch,
                    width * sizeof(DWORD));
    return SUCCEEDED(system->UnlockRect());
}

struct SpriteSameFrameCapture {
    IDirect3DSurface9* original = nullptr;
    IDirect3DSurface9* baseline = nullptr;
    IDirect3DSurface9* legacy = nullptr;
    IDirect3DSurface9* adapter = nullptr;
    IDirect3DSurface9* legacyResolved = nullptr;
    IDirect3DSurface9* adapterResolved = nullptr;
    IDirect3DSurface9* actualResolved = nullptr;
    D3DSURFACE_DESC description{};
    HRESULT status = E_FAIL;
    HRESULT legacyDraw = E_FAIL;
    HRESULT adapterDraw = E_FAIL;
    HRESULT actualStatus = E_FAIL;
    bool adapterApplied = false;
    bool actualAdapterApplied = false;
    UINT primitives = 0;
    bool text = false;
    bool active = false;
    char directory[MAX_PATH]{};
    mme::LineDrawBridgeProbe bridgeBefore{};
    mme::LineDrawBridgeProbe bridgeAfter{};
};
SpriteSameFrameCapture g_spriteSameFrameCapture;

void ProbeSpriteSameFrame(MMDApp* app, IDirect3DDevice9* device, IDirect3DVertexBuffer9* vb,
                          IDirect3DTexture9* sourceTexture, UINT count, bool text) {
    auto& capture = g_spriteSameFrameCapture;
    if (capture.active || count == 0 || count > 333 || !app->PortableSceneSession() ||
        !app->SelectedModel())
        return;
    static LONG spriteStarted = 0;
    static LONG textStarted = 0;
    LONG& started = text ? textStarted : spriteStarted;
    char directory[MAX_PATH]{};
    if (GetEnvironmentVariableA(text ? "MDS_RHI_TEXT_AB" : "MDS_RHI_SPRITE_AB", directory,
                                sizeof(directory)) != 1 ||
        directory[0] != '1' || InterlockedCompareExchange(&started, 1, 0) != 0)
        return;
    const DWORD pathLength =
        GetEnvironmentVariableA("MDS_RHI_LIVE_COMPARE_DIR", capture.directory, MAX_PATH);
    if (!pathLength || pathLength >= MAX_PATH)
        return;
    capture.active = true;
    capture.text = text;
    capture.primitives = count;
    capture.bridgeBefore = mme::ProbeTriangleDrawBridge();
    capture.status = device->GetRenderTarget(0, &capture.original);
    if (SUCCEEDED(capture.status))
        capture.status = capture.original->GetDesc(&capture.description);
    const auto& desc = capture.description;
    const auto createMsaa = [&](IDirect3DSurface9** out) {
        return device->CreateRenderTarget(desc.Width, desc.Height, desc.Format,
                                          desc.MultiSampleType, desc.MultiSampleQuality, FALSE, out,
                                          nullptr);
    };
    const auto createResolved = [&](IDirect3DSurface9** out) {
        return device->CreateRenderTarget(desc.Width, desc.Height, desc.Format, D3DMULTISAMPLE_NONE,
                                          0, FALSE, out, nullptr);
    };
    if (SUCCEEDED(capture.status))
        capture.status = createMsaa(&capture.legacy);
    if (SUCCEEDED(capture.status))
        capture.status = createMsaa(&capture.adapter);
    if (SUCCEEDED(capture.status))
        capture.status = createResolved(&capture.baseline);
    if (SUCCEEDED(capture.status))
        capture.status = createResolved(&capture.legacyResolved);
    if (SUCCEEDED(capture.status))
        capture.status = createResolved(&capture.adapterResolved);
    if (SUCCEEDED(capture.status))
        capture.status = createResolved(&capture.actualResolved);
    if (SUCCEEDED(capture.status))
        capture.status =
            device->StretchRect(capture.original, nullptr, capture.legacy, nullptr, D3DTEXF_NONE);
    if (SUCCEEDED(capture.status))
        capture.status =
            device->StretchRect(capture.original, nullptr, capture.adapter, nullptr, D3DTEXF_NONE);
    if (SUCCEEDED(capture.status))
        capture.status =
            device->StretchRect(capture.original, nullptr, capture.baseline, nullptr, D3DTEXF_NONE);
    std::vector<TexturedScreenVertex> vertices(count * 3u);
    void* bytes = nullptr;
    if (SUCCEEDED(capture.status)) {
        capture.status = vb->Lock(
            0, static_cast<UINT>(vertices.size() * sizeof(TexturedScreenVertex)), &bytes, 0);
        if (SUCCEEDED(capture.status) && bytes) {
            std::memcpy(vertices.data(), bytes, vertices.size() * sizeof(TexturedScreenVertex));
            capture.status = vb->Unlock();
        } else if (SUCCEEDED(capture.status)) {
            capture.status = E_FAIL;
        }
    }
    DWORD minFilter = 0, magFilter = 0, mipFilter = 0;
    DWORD addressU = 0, addressV = 0, maxAnisotropy = 0;
    if (SUCCEEDED(capture.status)) {
        capture.status = device->GetSamplerState(0, D3DSAMP_MINFILTER, &minFilter);
        if (SUCCEEDED(capture.status))
            capture.status = device->GetSamplerState(0, D3DSAMP_MAGFILTER, &magFilter);
        if (SUCCEEDED(capture.status))
            capture.status = device->GetSamplerState(0, D3DSAMP_MIPFILTER, &mipFilter);
        if (SUCCEEDED(capture.status))
            capture.status = device->GetSamplerState(0, D3DSAMP_ADDRESSU, &addressU);
        if (SUCCEEDED(capture.status))
            capture.status = device->GetSamplerState(0, D3DSAMP_ADDRESSV, &addressV);
        if (SUCCEEDED(capture.status))
            capture.status = device->GetSamplerState(0, D3DSAMP_MAXANISOTROPY, &maxAnisotropy);
        if (SUCCEEDED(capture.status) &&
            (minFilter != D3DTEXF_ANISOTROPIC || magFilter != D3DTEXF_ANISOTROPIC ||
             mipFilter != D3DTEXF_ANISOTROPIC || addressU != D3DTADDRESS_WRAP ||
             addressV != D3DTADDRESS_WRAP || maxAnisotropy == 0))
            capture.status = E_FAIL;
    }
    IDirect3DStateBlock9* previous = nullptr;
    IDirect3DBaseTexture9* texture = nullptr;
    if (SUCCEEDED(capture.status))
        capture.status = device->GetTexture(0, &texture);
    if (SUCCEEDED(capture.status) && !texture)
        capture.status = E_FAIL;
    if (SUCCEEDED(capture.status))
        capture.status = device->CreateStateBlock(D3DSBT_ALL, &previous);
    if (SUCCEEDED(capture.status) && previous)
        capture.status = previous->Capture();
    if (SUCCEEDED(capture.status))
        capture.status = device->SetRenderTarget(0, capture.legacy);
    if (SUCCEEDED(capture.status))
        capture.status = device->SetRenderState(D3DRS_ZWRITEENABLE, FALSE);
    if (SUCCEEDED(capture.status))
        capture.status = device->SetRenderState(D3DRS_STENCILPASS, D3DSTENCILOP_KEEP);
    if (SUCCEEDED(capture.status))
        capture.legacyDraw = device->DrawPrimitive(D3DPT_TRIANGLELIST, 0, count);
    if (SUCCEEDED(capture.status) && SUCCEEDED(capture.legacyDraw))
        capture.status = device->SetRenderTarget(0, capture.adapter);
    if (SUCCEEDED(capture.status)) {
        rhi::SamplerDesc sampler{};
        sampler.maxAnisotropy = maxAnisotropy;
        rhi::TextureDesc textureDescription{};
        std::vector<std::uint8_t> pixels;
        long result = E_FAIL;
        capture.adapterApplied =
            CaptureHudTexture(sourceTexture, textureDescription, pixels) &&
            rhi::DrawHudSpriteInScene(
                device, textureDescription,
                {pixels.data(), static_cast<std::size_t>(textureDescription.width) * 4u,
                 pixels.size()},
                vertices.data(), vertices.size() * sizeof(TexturedScreenVertex), sampler,
                [](IDirect3DDevice9* current, unsigned first, unsigned primitives) {
                    return current->DrawPrimitive(D3DPT_TRIANGLELIST, first, primitives);
                },
                &result, true);
        capture.adapterDraw = static_cast<HRESULT>(result);
        if (!capture.adapterApplied)
            capture.status = E_FAIL;
    }
    const HRESULT targetRestore =
        capture.original ? device->SetRenderTarget(0, capture.original) : E_FAIL;
    const HRESULT stateRestore = previous ? previous->Apply() : E_FAIL;
    if (previous)
        previous->Release();
    if (texture)
        texture->Release();
    if (SUCCEEDED(capture.status) && FAILED(targetRestore))
        capture.status = targetRestore;
    if (SUCCEEDED(capture.status) && FAILED(stateRestore))
        capture.status = stateRestore;
    if (SUCCEEDED(capture.status))
        capture.status = device->StretchRect(capture.legacy, nullptr, capture.legacyResolved,
                                             nullptr, D3DTEXF_NONE);
    if (SUCCEEDED(capture.status))
        capture.status = device->StretchRect(capture.adapter, nullptr, capture.adapterResolved,
                                             nullptr, D3DTEXF_NONE);
}

void ReportSpriteSameFrame(IDirect3DDevice9* device) {
    auto& capture = g_spriteSameFrameCapture;
    if (!capture.active)
        return;
    IDirect3DSurface9* system = nullptr;
    const auto& desc = capture.description;
    std::vector<DWORD> baseline, legacy, adapter, actual;
    const bool ready =
        SUCCEEDED(capture.status) && desc.Format == D3DFMT_A8R8G8B8 &&
        SUCCEEDED(device->CreateOffscreenPlainSurface(desc.Width, desc.Height, desc.Format,
                                                      D3DPOOL_SYSTEMMEM, &system, nullptr)) &&
        system &&
        ReadSelectionLivePixels(device, capture.baseline, system, desc.Width, desc.Height,
                                baseline) &&
        ReadSelectionLivePixels(device, capture.legacyResolved, system, desc.Width, desc.Height,
                                legacy) &&
        ReadSelectionLivePixels(device, capture.adapterResolved, system, desc.Width, desc.Height,
                                adapter) &&
        SUCCEEDED(capture.actualStatus) &&
        ReadSelectionLivePixels(device, capture.actualResolved, system, desc.Width, desc.Height,
                                actual);
    if (system)
        system->Release();
    std::size_t differences = 0, changed = 0, actualDifferences = 0;
    if (ready)
        for (std::size_t i = 0; i < legacy.size(); ++i) {
            differences += legacy[i] != adapter[i];
            changed += legacy[i] != baseline[i];
            actualDifferences += legacy[i] != actual[i];
        }
    const auto bridgeCalls = capture.bridgeAfter.calls - capture.bridgeBefore.calls;
    const auto hostCalls = capture.bridgeAfter.hostCalls - capture.bridgeBefore.hostCalls;
    char path[MAX_PATH]{};
    const int length =
        std::snprintf(path, sizeof(path), "%s\\%s", capture.directory,
                      capture.text ? "text_overlay.same_frame" : "sprite_overlay.same_frame");
    if (length > 0 && length < static_cast<int>(sizeof(path))) {
        FILE* file = nullptr;
        if (fopen_s(&file, path, "wb") == 0 && file) {
            std::fprintf(
                file,
                "status=%s pixels=%zu differences=%zu changed=%zu "
                "actualDifferences=%zu actualAdapterApplied=%u "
                "primitives=%u bridgeCalls=%lu hostCalls=%lu "
                "probeHr=%08lx legacyHr=%08lx adapterHr=%08lx "
                "adapterApplied=%u readback=%u\n",
                ready && differences == 0 &&
                        (!(capture.text ? TextAdapterRequested() : SpriteAdapterRequested()) ||
                         capture.actualAdapterApplied) &&
                        changed > 0 && capture.adapterApplied && bridgeCalls == 1 && hostCalls == 1
                    ? "PASS"
                    : "FAIL",
                legacy.size(), differences, changed, actualDifferences,
                capture.actualAdapterApplied, capture.primitives, bridgeCalls, hostCalls,
                static_cast<unsigned long>(capture.status),
                static_cast<unsigned long>(capture.legacyDraw),
                static_cast<unsigned long>(capture.adapterDraw), capture.adapterApplied, ready);
            std::fclose(file);
        }
    }
    if (capture.original)
        capture.original->Release();
    if (capture.baseline)
        capture.baseline->Release();
    if (capture.legacy)
        capture.legacy->Release();
    if (capture.adapter)
        capture.adapter->Release();
    if (capture.legacyResolved)
        capture.legacyResolved->Release();
    if (capture.adapterResolved)
        capture.adapterResolved->Release();
    if (capture.actualResolved)
        capture.actualResolved->Release();
    capture = {};
}

void ReportSelectionLiveCapture(IDirect3DDevice9* device) {
    auto& capture = g_selectionLiveCapture;
    if (!capture.active)
        return;
    capture.active = false;
    const auto& desc = capture.description;
    IDirect3DSurface9* current = nullptr;
    const bool currentKnown = SUCCEEDED(device->GetRenderTarget(0, &current));
    const bool currentSame = currentKnown && current == capture.target;
    if (current)
        current->Release();
    if (SUCCEEDED(capture.beforeStatus))
        capture.afterStatus =
            device->StretchRect(capture.target, nullptr, capture.after, nullptr, D3DTEXF_NONE);
    std::vector<DWORD> before, immediate, after;
    IDirect3DSurface9* system = nullptr;
    bool pixelsReady =
        SUCCEEDED(capture.beforeStatus) && SUCCEEDED(capture.immediateStatus) &&
        SUCCEEDED(capture.afterStatus) && desc.Width > 0 && desc.Height > 0 &&
        desc.Format == D3DFMT_A8R8G8B8 &&
        SUCCEEDED(device->CreateOffscreenPlainSurface(desc.Width, desc.Height, desc.Format,
                                                      D3DPOOL_SYSTEMMEM, &system, nullptr)) &&
        system &&
        ReadSelectionLivePixels(device, capture.before, system, desc.Width, desc.Height, before) &&
        ReadSelectionLivePixels(device, capture.immediate, system, desc.Width, desc.Height,
                                immediate) &&
        ReadSelectionLivePixels(device, capture.after, system, desc.Width, desc.Height, after);
    if (system)
        system->Release();
    DWORD samplesPassed = 0;
    DWORD visibleModes[10]{};
    HRESULT visibleStatuses[10]{E_FAIL, E_FAIL, E_FAIL, E_FAIL, E_FAIL,
                                E_FAIL, E_FAIL, E_FAIL, E_FAIL, E_FAIL};
    HRESULT queryStatus = E_FAIL;
    if (capture.visibilityQuery) {
        for (int attempt = 0; attempt < 1000; ++attempt) {
            queryStatus = capture.visibilityQuery->GetData(&samplesPassed, sizeof(samplesPassed),
                                                           D3DGETDATA_FLUSH);
            if (queryStatus != S_FALSE)
                break;
            Sleep(1);
        }
    }
    for (unsigned mode = 0; mode < 10; ++mode) {
        auto* query = capture.visibilityModes[mode];
        if (!query)
            continue;
        for (int attempt = 0; attempt < 1000; ++attempt) {
            visibleStatuses[mode] =
                query->GetData(&visibleModes[mode], sizeof(visibleModes[mode]), D3DGETDATA_FLUSH);
            if (visibleStatuses[mode] != S_FALSE)
                break;
            Sleep(1);
        }
    }
    std::size_t changed = 0, changedInside = 0, immediateChanged = 0;
    std::size_t immediateInside = 0;
    UINT minX = desc.Width, minY = desc.Height, maxX = 0, maxY = 0;
    std::uint64_t beforeHash = 14695981039346656037ull;
    std::uint64_t afterHash = beforeHash;
    if (pixelsReady) {
        for (std::size_t i = 0; i < before.size(); ++i) {
            changed += before[i] != after[i];
            const int x = static_cast<int>(i % desc.Width);
            const int y = static_cast<int>(i / desc.Width);
            if (before[i] != after[i]) {
                minX = (std::min)(minX, static_cast<UINT>(x));
                minY = (std::min)(minY, static_cast<UINT>(y));
                maxX = (std::max)(maxX, static_cast<UINT>(x));
                maxY = (std::max)(maxY, static_cast<UINT>(y));
            }
            if (before[i] != after[i] && x >= capture.left - 2 && x <= capture.right + 2 &&
                y >= capture.top - 2 && y <= capture.bottom + 2)
                ++changedInside;
            immediateChanged += before[i] != immediate[i];
            if (before[i] != immediate[i] && x >= capture.left - 2 && x <= capture.right + 2 &&
                y >= capture.top - 2 && y <= capture.bottom + 2)
                ++immediateInside;
            beforeHash = (beforeHash ^ before[i]) * 1099511628211ull;
            afterHash = (afterHash ^ after[i]) * 1099511628211ull;
        }
    }
    const auto calls = capture.bridgeAfter.calls - capture.bridgeBefore.calls;
    const auto hostCalls = capture.bridgeAfter.hostCalls - capture.bridgeBefore.hostCalls;
    const auto directCalls = capture.bridgeAfter.directCalls - capture.bridgeBefore.directCalls;
    const bool visibilityReady =
        (std::all_of)(std::begin(visibleStatuses), std::end(visibleStatuses),
                      [](HRESULT status) { return status == S_OK; });
    const bool explainedInvisible =
        visibilityReady && visibleModes[0] == 0 && visibleModes[5] > 0 && visibleModes[6] > 0;
    const bool passed = pixelsReady && (immediateInside > 0 || explainedInvisible) &&
                        (!capture.adapter || capture.adapterApplied) &&
                        SUCCEEDED(capture.drawStatus) && calls == 1 && hostCalls + directCalls == 1;
    CreateDirectoryA(capture.directory, nullptr);
    if (SelectionAbEnabled() && !capture.adapter) {
        IDirect3DSurface9* sameFrameSystem = nullptr;
        std::vector<DWORD> legacyPixels, adapterPixels;
        const bool sameFrameReadback =
            SUCCEEDED(capture.sameFrameProbeStatus) && pixelsReady &&
            SUCCEEDED(device->CreateOffscreenPlainSurface(desc.Width, desc.Height, desc.Format,
                                                          D3DPOOL_SYSTEMMEM, &sameFrameSystem,
                                                          nullptr)) &&
            sameFrameSystem &&
            ReadSelectionLivePixels(device, capture.sameFrameLegacyResult, sameFrameSystem,
                                    desc.Width, desc.Height, legacyPixels) &&
            ReadSelectionLivePixels(device, capture.sameFrameAdapterResult, sameFrameSystem,
                                    desc.Width, desc.Height, adapterPixels);
        if (sameFrameSystem)
            sameFrameSystem->Release();
        std::size_t differences = 0, changedInsideBox = 0;
        if (sameFrameReadback) {
            for (std::size_t i = 0; i < legacyPixels.size(); ++i) {
                differences += legacyPixels[i] != adapterPixels[i];
                const int x = static_cast<int>(i % desc.Width);
                const int y = static_cast<int>(i / desc.Width);
                changedInsideBox += before[i] != legacyPixels[i] && x >= capture.left &&
                                    x <= capture.right && y >= capture.top && y <= capture.bottom;
            }
        }
        char sameFramePath[MAX_PATH]{};
        const int sameFrameLength =
            std::snprintf(sameFramePath, sizeof(sameFramePath), "%s\\line_selection.same_frame",
                          capture.directory);
        if (sameFrameLength > 0 && sameFrameLength < static_cast<int>(sizeof(sameFramePath))) {
            FILE* file = nullptr;
            if (fopen_s(&file, sameFramePath, "wb") == 0 && file) {
                std::fprintf(file,
                             "status=%s pixels=%zu differences=%zu changedInside=%zu "
                             "cloneHr=%08lx probeHr=%08lx legacyHr=%08lx "
                             "adapterHr=%08lx adapterApplied=%u "
                             "readback=%u depthStencilWrites=0\n",
                             sameFrameReadback && differences == 0 && changedInsideBox > 0 &&
                                     capture.sameFrameAdapterApplied
                                 ? "PASS"
                                 : "FAIL",
                             legacyPixels.size(), differences, changedInsideBox,
                             static_cast<unsigned long>(capture.sameFrameCloneStatus),
                             static_cast<unsigned long>(capture.sameFrameProbeStatus),
                             static_cast<unsigned long>(capture.sameFrameLegacyDrawStatus),
                             static_cast<unsigned long>(capture.sameFrameAdapterDrawStatus),
                             capture.sameFrameAdapterApplied, sameFrameReadback);
                std::fclose(file);
            }
        }
    }
    char path[MAX_PATH]{};
    const int pathLength =
        std::snprintf(path, sizeof(path), "%s\\%s", capture.directory,
                      capture.adapter ? "line_selection.adapter.live" : "line_selection.live");
    if (pathLength > 0 && pathLength < static_cast<int>(sizeof(path))) {
        FILE* file = nullptr;
        if (fopen_s(&file, path, "wb") == 0 && file) {
            std::fprintf(
                file,
                "status=%s path=%s adapterApplied=%u target=%ux%u samples=%u changed=%zu changedInside=%zu "
                "box=%d,%d,%d,%d changedBounds=%u,%u,%u,%u "
                "immediateChanged=%zu immediateInside=%zu "
                "visibility=%lu,%lu,%lu,%lu,%lu,%lu,%lu,%lu,%lu,%lu visibilityReady=%u "
                "beforeHash=%016llx afterHash=%016llx "
                "bridgeCalls=%lu hostCalls=%lu directCalls=%lu "
                "currentSame=%u effectsActive=%u "
                "streamSameAfter=%u streamOffset=%u streamStride=%u "
                "streamSize=%u streamUsage=%lu streamPool=%u "
                "streamDescHr=%08lx streamReadHr=%08lx streamUnlockHr=%08lx "
                "streamBytesReady=%u streamVerticesMatch=%u "
                "skippedStaleFrames=%ld "
                "streamFirst=%.1f,%.1f streamLast=%.1f,%.1f "
                "streamXY=%.1f,%.1f;%.1f,%.1f;%.1f,%.1f;%.1f,%.1f;"
                "%.1f,%.1f;%.1f,%.1f;%.1f,%.1f;%.1f,%.1f "
                "samplesPassed=%lu queryHr=%08lx "
                "pendingBefore=%u pendingAfter=%u effectEnabled=%d "
                "bindingReadyBefore=%d bindingReadyAfter=%d "
                "bindingActiveBefore=%d bindingActiveAfter=%d "
                "endSceneFired=%d "
                "effectsDisabled=%d lineDisposition=%d drawHr=%08lx "
                "beforeHr=%08lx immediateHr=%08lx "
                "afterHr=%08lx cloneHr=%08lx readback=%u\n",
                passed ? "PASS" : "FAIL", capture.adapter ? "adapter" : "legacy",
                capture.adapterApplied, desc.Width, desc.Height, desc.MultiSampleType, changed,
                changedInside, capture.left, capture.top, capture.right, capture.bottom, minX, minY,
                maxX, maxY, immediateChanged, immediateInside, visibleModes[0], visibleModes[1],
                visibleModes[2], visibleModes[3], visibleModes[4], visibleModes[5], visibleModes[6],
                visibleModes[7], visibleModes[8], visibleModes[9], visibilityReady, beforeHash,
                afterHash, calls, hostCalls, directCalls, currentSame, capture.effectsActive,
                capture.streamSameAfter, capture.drawOffset, capture.drawStride,
                capture.streamDescription.Size, capture.streamDescription.Usage,
                capture.streamDescription.Pool,
                static_cast<unsigned long>(capture.streamDescriptionStatus),
                static_cast<unsigned long>(capture.streamReadStatus),
                static_cast<unsigned long>(capture.streamUnlockStatus), capture.streamBytesReady,
                capture.streamVerticesMatch, capture.skippedStaleFrames,
                capture.streamVertices.front().x, capture.streamVertices.front().y,
                capture.streamVertices.back().x, capture.streamVertices.back().y,
                capture.streamVertices[0].x, capture.streamVertices[0].y,
                capture.streamVertices[1].x, capture.streamVertices[1].y,
                capture.streamVertices[2].x, capture.streamVertices[2].y,
                capture.streamVertices[3].x, capture.streamVertices[3].y,
                capture.streamVertices[4].x, capture.streamVertices[4].y,
                capture.streamVertices[5].x, capture.streamVertices[5].y,
                capture.streamVertices[6].x, capture.streamVertices[6].y,
                capture.streamVertices[7].x, capture.streamVertices[7].y, samplesPassed,
                static_cast<unsigned long>(queryStatus), capture.hostBefore.pending,
                capture.hostAfter.pending, capture.hostBefore.effectEnabled,
                capture.hostBefore.bindingReady, capture.hostAfter.bindingReady,
                capture.hostBefore.bindingActive, capture.hostAfter.bindingActive,
                capture.hostBefore.endSceneFired, capture.hostBefore.effectsDisabled,
                capture.hostAfter.lineDisposition, static_cast<unsigned long>(capture.drawStatus),
                static_cast<unsigned long>(capture.beforeStatus),
                static_cast<unsigned long>(capture.immediateStatus),
                static_cast<unsigned long>(capture.afterStatus),
                static_cast<unsigned long>(capture.sameFrameCloneStatus), pixelsReady);
            std::fclose(file);
        }
    }
    if (SelectionAbEnabled()) {
        const auto extractBox = [&](const std::vector<DWORD>& source) {
            std::vector<DWORD> box;
            if (!pixelsReady || source.empty() || capture.left < 0 || capture.top < 0 ||
                capture.right >= static_cast<int>(desc.Width) ||
                capture.bottom >= static_cast<int>(desc.Height))
                return box;
            box.reserve(static_cast<std::size_t>(capture.right - capture.left + 1) *
                        (capture.bottom - capture.top + 1));
            for (int y = capture.top; y <= capture.bottom; ++y)
                for (int x = capture.left; x <= capture.right; ++x)
                    box.push_back(source[static_cast<std::size_t>(y) * desc.Width + x]);
            return box;
        };
        const auto beforeBox = extractBox(before);
        const auto immediateBox = extractBox(immediate);
        if (!capture.adapter) {
            g_selectionAbReference.before = beforeBox;
            g_selectionAbReference.immediate = immediateBox;
            g_selectionAbReference.left = capture.left;
            g_selectionAbReference.top = capture.top;
            g_selectionAbReference.right = capture.right;
            g_selectionAbReference.bottom = capture.bottom;
            g_selectionAbReference.width = desc.Width;
            g_selectionAbReference.height = desc.Height;
            g_selectionAbReference.valid =
                passed && !beforeBox.empty() && beforeBox.size() == immediateBox.size();
        } else {
            const bool matchingGeometry =
                g_selectionAbReference.valid && g_selectionAbReference.left == capture.left &&
                g_selectionAbReference.top == capture.top &&
                g_selectionAbReference.right == capture.right &&
                g_selectionAbReference.bottom == capture.bottom &&
                g_selectionAbReference.width == desc.Width &&
                g_selectionAbReference.height == desc.Height &&
                g_selectionAbReference.before.size() == beforeBox.size() &&
                g_selectionAbReference.immediate.size() == immediateBox.size();
            std::size_t beforeDifferences = 0;
            std::size_t immediateDifferences = 0;
            std::size_t effectDifferences = 0;
            std::size_t affectedPixels = 0;
            std::size_t unstableAffectedPixels = 0;
            std::size_t stableEffectDifferences = 0;
            std::size_t lineOutputDifferences = 0;
            if (matchingGeometry) {
                for (std::size_t i = 0; i < beforeBox.size(); ++i) {
                    const bool changedBefore = g_selectionAbReference.before[i] != beforeBox[i];
                    const bool legacyAffected =
                        g_selectionAbReference.before[i] != g_selectionAbReference.immediate[i];
                    const bool adapterAffected = beforeBox[i] != immediateBox[i];
                    beforeDifferences += changedBefore;
                    immediateDifferences += g_selectionAbReference.immediate[i] != immediateBox[i];
                    affectedPixels += legacyAffected;
                    unstableAffectedPixels += changedBefore && (legacyAffected || adapterAffected);
                    const bool effectMismatch =
                        legacyAffected != adapterAffected ||
                        (legacyAffected && adapterAffected &&
                         g_selectionAbReference.immediate[i] != immediateBox[i]);
                    effectDifferences += effectMismatch;
                    stableEffectDifferences += !changedBefore && effectMismatch;
                    lineOutputDifferences += (legacyAffected || adapterAffected) &&
                                             g_selectionAbReference.immediate[i] != immediateBox[i];
                }
            }
            const bool equal = passed && matchingGeometry && affectedPixels > 0 &&
                               stableEffectDifferences == 0 && lineOutputDifferences == 0;
            char abPath[MAX_PATH]{};
            const int abLength =
                std::snprintf(abPath, sizeof(abPath), "%s\\line_selection.ab", capture.directory);
            if (abLength > 0 && abLength < static_cast<int>(sizeof(abPath))) {
                FILE* file = nullptr;
                if (fopen_s(&file, abPath, "wb") == 0 && file) {
                    std::fprintf(file,
                                 "status=%s geometry=%u pixels=%zu beforeDifferences=%zu "
                                 "immediateDifferences=%zu effectDifferences=%zu "
                                 "stableEffectDifferences=%zu lineOutputDifferences=%zu "
                                 "affectedPixels=%zu unstableAffectedPixels=%zu "
                                 "adapterDraw=%u "
                                 "bridgeCalls=%lu hostCalls=%lu\n",
                                 equal ? "PASS" : "FAIL", matchingGeometry, beforeBox.size(),
                                 beforeDifferences, immediateDifferences, effectDifferences,
                                 stableEffectDifferences, lineOutputDifferences, affectedPixels,
                                 unstableAffectedPixels, capture.adapterApplied, calls, hostCalls);
                    std::fclose(file);
                }
            }
        }
    }
    if (capture.target)
        capture.target->Release();
    if (capture.visibilityQuery)
        capture.visibilityQuery->Release();
    if (capture.drawState)
        capture.drawState->Release();
    if (capture.drawStream)
        capture.drawStream->Release();
    for (auto*& query : capture.visibilityModes) {
        if (query)
            query->Release();
        query = nullptr;
    }
    if (capture.before)
        capture.before->Release();
    if (capture.immediate)
        capture.immediate->Release();
    if (capture.after)
        capture.after->Release();
    if (capture.sameFrameClone)
        capture.sameFrameClone->Release();
    if (capture.sameFrameAdapterClone)
        capture.sameFrameAdapterClone->Release();
    if (capture.sameFrameLegacyResult)
        capture.sameFrameLegacyResult->Release();
    if (capture.sameFrameAdapterResult)
        capture.sameFrameAdapterResult->Release();
    capture.target = nullptr;
    capture.visibilityQuery = nullptr;
    capture.drawState = nullptr;
    capture.drawStream = nullptr;
    capture.before = nullptr;
    capture.immediate = nullptr;
    capture.after = nullptr;
    capture.sameFrameClone = nullptr;
    capture.sameFrameAdapterClone = nullptr;
    capture.sameFrameLegacyResult = nullptr;
    capture.sameFrameAdapterResult = nullptr;
    if (SelectionAbEnabled())
        g_selectionAbStage = capture.adapter ? 2 : g_selectionAbReference.valid ? 1 : 2;
}
#endif

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
#ifdef MIKUDANCESTUDIO_DIAG
    ProbeSpriteSameFrame(app, device, vb, app->TextOverlayTexture(), count, true);
#endif
    HRESULT result = E_FAIL;
    bool handled = false;
    if (TextAdapterRequested() && count <= 0x6d60u / 84u) {
        rhi::SamplerDesc sampler{};
        const auto atlas = GetSceneFontRgba(app);
        if (CaptureHudSampler(device, sampler) && atlas.pixels && atlas.bytes == 512u * 512u * 4u) {
#ifdef MIKUDANCESTUDIO_DIAG
            ReportOverlayAtlasParity(app->TextOverlayTexture(), rhi::OverlayAtlasSlot::Text,
                                     atlas.pixels, atlas.bytes, 512, 512);
#endif
            const auto* portable = PortableFontAtlas();
#ifdef MIKUDANCESTUDIO_DIAG
            if (portable)
                ReportPortableFontDifference(app->TextOverlayTexture(), *portable);
#endif
            const auto* pixels = portable ? portable->image.pixels.data() : atlas.pixels;
            const std::size_t bytes = portable ? portable->image.pixels.size() : atlas.bytes;
            const std::uint64_t revision = portable ? portable->revision : atlas.revision;
            std::vector<TexturedScreenVertex> vertices(count * 3u);
            void* source = nullptr;
            if (SUCCEEDED(
                    vb->Lock(0, static_cast<UINT>(vertices.size() * sizeof(TexturedScreenVertex)),
                             &source, 0)) &&
                source) {
                std::memcpy(vertices.data(), source,
                            vertices.size() * sizeof(TexturedScreenVertex));
                if (SUCCEEDED(vb->Unlock())) {
                    long borrowedResult = E_FAIL;
                    const bool applied = rhi::DrawCachedHudSpriteInScene(
                        device, rhi::OverlayAtlasSlot::Text, revision,
                        {512, 512, rhi::TextureFormat::Rgba8}, {pixels, 512u * 4u, bytes},
                        vertices.data(), vertices.size() * sizeof(TexturedScreenVertex), sampler,
                        [](IDirect3DDevice9* current, unsigned first, unsigned primitives) {
                            return mme::DrawPrimitive(current, D3DPT_TRIANGLELIST, first,
                                                      primitives);
                        },
                        &borrowedResult);
                    result = static_cast<HRESULT>(borrowedResult);
                    handled = applied || SUCCEEDED(result);
#ifdef MIKUDANCESTUDIO_DIAG
                    if (g_spriteSameFrameCapture.active && g_spriteSameFrameCapture.text)
                        g_spriteSameFrameCapture.actualAdapterApplied = applied;
                    if (applied)
                        ReportOverlayCacheReuse(device, rhi::OverlayAtlasSlot::Text);
#endif
                }
            }
        }
    }
    if (!handled)
        result = mme::DrawPrimitive(device, D3DPT_TRIANGLELIST, 0, count);
#ifdef MIKUDANCESTUDIO_DIAG
    if (g_spriteSameFrameCapture.active && g_spriteSameFrameCapture.text) {
        g_spriteSameFrameCapture.actualStatus =
            SUCCEEDED(result) ? device->StretchRect(g_spriteSameFrameCapture.original, nullptr,
                                                    g_spriteSameFrameCapture.actualResolved,
                                                    nullptr, D3DTEXF_NONE)
                              : result;
        g_spriteSameFrameCapture.bridgeAfter = mme::ProbeTriangleDrawBridge();
    }
#endif
}

void DrawLineOverlay(MMDApp* app, D3DRenderer* sub, IDirect3DDevice9* device) {
    const UINT count = app->LineOverlayPrimitiveCount();
    // The renderer owns the selection and guide-line vertex buffer.
    auto* vb = sub->lineVertexBuffer;
    if (count == 0 || vb == nullptr)
        return;

    // The original explicitly unbinds stage 0 before the line batch (SetTexture(0,NULL) at x64 0x7FF7CB44AF44,
    // after the text batch and before the line batch); otherwise lines inherit the text batch's font atlas texture.
    device->SetTexture(0, nullptr);
    device->SetStreamSource(0, vb, 0, sizeof(ColoredScreenVertex));
    device->SetFVF(kColoredScreenVertexFvf);
#ifdef MIKUDANCESTUDIO_DIAG
    DumpSelectionDrawState(app, device);
    BeginSelectionLiveCapture(app, device);
    ProbeSelectionSameFrame(device);
    if (g_selectionLiveCapture.active && g_selectionLiveCapture.visibilityQuery)
        g_selectionLiveCapture.visibilityQuery->Issue(D3DISSUE_BEGIN);
#endif
    // MME: 原版线框批次（x64 @0x44AFB4）与文本/精灵 overlay 一样经设备虚表
    // DrawPrimitive 槽发出（被 MMHack 包装可见），故同样过桥。
    HRESULT result = E_FAIL;
    bool handled = false;
#ifdef MIKUDANCESTUDIO_DIAG
    if (g_selectionLiveCapture.active && g_selectionLiveCapture.adapter &&
        g_selectionLiveCapture.streamBytesReady) {
        long borrowedResult = E_FAIL;
        g_selectionLiveCapture.adapterApplied = rhi::DrawSelectionBoxInScene(
            device, g_selectionLiveCapture.streamVertices.data(),
            sizeof(g_selectionLiveCapture.streamVertices),
            [](IDirect3DDevice9* current, unsigned first, unsigned primitives) {
                return mme::DrawPrimitive(current, D3DPT_LINELIST, first, primitives);
            },
            &borrowedResult);
        result = static_cast<HRESULT>(borrowedResult);
        handled = SUCCEEDED(result);
    }
#endif
    if (!handled && count == 4 && app->BoneBoxSelectionActive() != 0 && SelectionAdapterRequested()
#ifdef MIKUDANCESTUDIO_DIAG
        && !SelectionAbEnabled()
#endif
    ) {
        std::array<ColoredScreenVertex, 8> vertices{};
        void* source = nullptr;
        if (SUCCEEDED(vb->Lock(0, sizeof(vertices), &source, 0)) && source) {
            std::memcpy(vertices.data(), source, sizeof(vertices));
            const HRESULT unlock = vb->Unlock();
            if (SUCCEEDED(unlock)) {
                long borrowedResult = E_FAIL;
                handled = rhi::DrawSelectionBoxInScene(
                    device, vertices.data(), sizeof(vertices),
                    [](IDirect3DDevice9* current, unsigned first, unsigned primitives) {
                        return mme::DrawPrimitive(current, D3DPT_LINELIST, first, primitives);
                    },
                    &borrowedResult);
                result = static_cast<HRESULT>(borrowedResult);
                // A successful host draw cannot be replayed even if state
                // restoration fails; that would blend the line twice.
                handled = handled || SUCCEEDED(result);
#ifdef MIKUDANCESTUDIO_DIAG
                if (g_selectionLiveCapture.active)
                    g_selectionLiveCapture.adapterApplied = SUCCEEDED(result);
#endif
            }
        }
    }
    if (!handled)
        result = mme::DrawPrimitive(device, D3DPT_LINELIST, 0, count);
#ifdef MIKUDANCESTUDIO_DIAG
    EndSelectionLiveCapture(device, result);
#endif
}

void DrawSpriteOverlay(MMDApp* app, IDirect3DDevice9* device) {
    const UINT count = app->SpriteOverlayPrimitiveCount();
    auto* vb = app->SpriteOverlayVertices();
    if (count == 0 || vb == nullptr)
        return;

    device->SetTexture(0, app->OverlayTexture());
    device->SetStreamSource(0, vb, 0, sizeof(TexturedScreenVertex));
    device->SetFVF(kTexturedScreenVertexFvf);
#ifdef MIKUDANCESTUDIO_DIAG
    static LONG capturedSpriteState = 0;
    char spriteCaptureDir[MAX_PATH]{};
    const DWORD spriteCaptureLength =
        GetEnvironmentVariableA("MDS_RHI_LIVE_COMPARE_DIR", spriteCaptureDir, MAX_PATH);
    if (spriteCaptureLength > 0 && spriteCaptureLength < MAX_PATH &&
        InterlockedCompareExchange(&capturedSpriteState, 1, 0) == 0) {
        const auto render = [device](D3DRENDERSTATETYPE state) {
            DWORD value = 0;
            device->GetRenderState(state, &value);
            return value;
        };
        const auto stage = [device](D3DTEXTURESTAGESTATETYPE state) {
            DWORD value = 0;
            device->GetTextureStageState(0, state, &value);
            return value;
        };
        const auto sampler = [device](D3DSAMPLERSTATETYPE state) {
            DWORD value = 0;
            device->GetSamplerState(0, state, &value);
            return value;
        };
        IDirect3DBaseTexture9* texture = nullptr;
        device->GetTexture(0, &texture);
        char spritePath[MAX_PATH]{};
        const int length = std::snprintf(spritePath, sizeof(spritePath), "%s\\sprite_overlay.state",
                                         spriteCaptureDir);
        if (length > 0 && length < static_cast<int>(sizeof(spritePath))) {
            FILE* file = nullptr;
            if (fopen_s(&file, spritePath, "wb") == 0 && file) {
                std::fprintf(
                    file,
                    "primitives=%u texture=%u lighting=%lu cull=%lu "
                    "depth=%lu depthWrite=%lu depthFunc=%lu "
                    "blend=%lu source=%lu destination=%lu "
                    "alphaTest=%lu alphaFunc=%lu alphaRef=%lu "
                    "stencil=%lu stencilFunc=%lu stencilRef=%lu "
                    "stencilPass=%lu colorWrite=%lu "
                    "colorOp=%lu colorArg1=%lu colorArg2=%lu "
                    "alphaOp=%lu alphaArg1=%lu alphaArg2=%lu "
                    "min=%lu mag=%lu mip=%lu addressU=%lu addressV=%lu "
                    "maxAnisotropy=%lu stage1Color=%lu stage1Alpha=%lu\n",
                    count, texture != nullptr, render(D3DRS_LIGHTING), render(D3DRS_CULLMODE),
                    render(D3DRS_ZENABLE), render(D3DRS_ZWRITEENABLE), render(D3DRS_ZFUNC),
                    render(D3DRS_ALPHABLENDENABLE), render(D3DRS_SRCBLEND), render(D3DRS_DESTBLEND),
                    render(D3DRS_ALPHATESTENABLE), render(D3DRS_ALPHAFUNC), render(D3DRS_ALPHAREF),
                    render(D3DRS_STENCILENABLE), render(D3DRS_STENCILFUNC),
                    render(D3DRS_STENCILREF), render(D3DRS_STENCILPASS),
                    render(D3DRS_COLORWRITEENABLE), stage(D3DTSS_COLOROP), stage(D3DTSS_COLORARG1),
                    stage(D3DTSS_COLORARG2), stage(D3DTSS_ALPHAOP), stage(D3DTSS_ALPHAARG1),
                    stage(D3DTSS_ALPHAARG2), sampler(D3DSAMP_MINFILTER), sampler(D3DSAMP_MAGFILTER),
                    sampler(D3DSAMP_MIPFILTER), sampler(D3DSAMP_ADDRESSU),
                    sampler(D3DSAMP_ADDRESSV), sampler(D3DSAMP_MAXANISOTROPY),
                    [&]() {
                        DWORD v = 0;
                        device->GetTextureStageState(1, D3DTSS_COLOROP, &v);
                        return v;
                    }(),
                    [&]() {
                        DWORD v = 0;
                        device->GetTextureStageState(1, D3DTSS_ALPHAOP, &v);
                        return v;
                    }());
                std::fclose(file);
            }
        }
        if (texture)
            texture->Release();
    }
    ProbeSpriteSameFrame(app, device, vb, app->OverlayTexture(), count, false);
#endif
    HRESULT spriteDrawResult = E_FAIL;
    bool spriteHandled = false;
    if (SpriteAdapterRequested() && count <= 0x445c0u / 84u) {
        rhi::SamplerDesc sampler{};
        const std::uint8_t* pixels = nullptr;
        std::size_t pixelBytes = 0;
        std::uint32_t width = 0, height = 0;
        std::uint64_t revision = 0;
        if (CaptureHudSampler(device, sampler) &&
            LoadHudSpriteRgba(app, pixels, pixelBytes, width, height, revision) && pixels &&
            pixelBytes == static_cast<std::size_t>(width) * height * 4u) {
#ifdef MIKUDANCESTUDIO_DIAG
            ReportOverlayAtlasParity(app->OverlayTexture(), rhi::OverlayAtlasSlot::Sprite, pixels,
                                     pixelBytes, width, height);
#endif
            std::vector<TexturedScreenVertex> vertices(count * 3u);
            void* source = nullptr;
            if (SUCCEEDED(
                    vb->Lock(0, static_cast<UINT>(vertices.size() * sizeof(TexturedScreenVertex)),
                             &source, 0)) &&
                source) {
                std::memcpy(vertices.data(), source,
                            vertices.size() * sizeof(TexturedScreenVertex));
                if (SUCCEEDED(vb->Unlock())) {
                    long borrowedResult = E_FAIL;
                    const bool applied = rhi::DrawCachedHudSpriteInScene(
                        device, rhi::OverlayAtlasSlot::Sprite, revision,
                        {width, height, rhi::TextureFormat::Rgba8},
                        {pixels, static_cast<std::size_t>(width) * 4u, pixelBytes}, vertices.data(),
                        vertices.size() * sizeof(TexturedScreenVertex), sampler,
                        [](IDirect3DDevice9* current, unsigned first, unsigned primitives) {
                            return mme::DrawPrimitive(current, D3DPT_TRIANGLELIST, first,
                                                      primitives);
                        },
                        &borrowedResult);
                    spriteDrawResult = static_cast<HRESULT>(borrowedResult);
                    spriteHandled = applied || SUCCEEDED(spriteDrawResult);
#ifdef MIKUDANCESTUDIO_DIAG
                    if (g_spriteSameFrameCapture.active)
                        g_spriteSameFrameCapture.actualAdapterApplied = applied;
                    if (applied)
                        ReportOverlayCacheReuse(device, rhi::OverlayAtlasSlot::Sprite);
#endif
                }
            }
        }
    }
    if (!spriteHandled)
        spriteDrawResult = mme::DrawPrimitive(device, D3DPT_TRIANGLELIST, 0, count);
#ifdef MIKUDANCESTUDIO_DIAG
    if (g_spriteSameFrameCapture.active && !g_spriteSameFrameCapture.text) {
        g_spriteSameFrameCapture.actualStatus =
            SUCCEEDED(spriteDrawResult)
                ? device->StretchRect(g_spriteSameFrameCapture.original, nullptr,
                                      g_spriteSameFrameCapture.actualResolved, nullptr,
                                      D3DTEXF_NONE)
                : spriteDrawResult;
        g_spriteSameFrameCapture.bridgeAfter = mme::ProbeTriangleDrawBridge();
    }
#endif
}

void DrawFullscreenQuad(IDirect3DDevice9* device, IDirect3DBaseTexture9* texture,
                        IDirect3DVertexBuffer9* vertices) {
    device->SetTexture(0, texture);
    device->SetStreamSource(0, vertices, 0, sizeof(TexturedScreenVertex));
    device->SetFVF(kTexturedScreenVertexFvf);
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
    const DWORD length =
        GetEnvironmentVariableA("MIKUDANCESTUDIO_SCREEN_TEXTURE_DUMP_DIR", directory, MAX_PATH);
    if (length == 0 || length >= MAX_PATH)
        return;

    char stableCapture[2]{};
    if (GetEnvironmentVariableA("MIKUDANCESTUDIO_AB_STABLE_CAPTURE", stableCapture,
                                sizeof(stableCapture)) == 1 &&
        stableCapture[0] == '1') {
        char gatePath[MAX_PATH]{};
        std::snprintf(gatePath, sizeof(gatePath), "%s\\vb.capture.ready", directory);
        const DWORD attributes = GetFileAttributesA(gatePath);
        if (attributes == INVALID_FILE_ATTRIBUTES || (attributes & FILE_ATTRIBUTE_DIRECTORY) != 0)
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
        FAILED(device->CreateOffscreenPlainSurface(description.Width, description.Height,
                                                   description.Format, D3DPOOL_SYSTEMMEM, &system,
                                                   nullptr)) ||
        system == nullptr || FAILED(device->GetRenderTargetData(source, system)) ||
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
    std::snprintf(rawPath, sizeof(rawPath), "%s\\screen_texture.bgra", directory);
    std::snprintf(bitmapPath, sizeof(bitmapPath), "%s\\screen_texture.bmp", directory);
    std::snprintf(metaPath, sizeof(metaPath), "%s\\screen_texture.meta.json", directory);

    const DWORD rowBytes = description.Width * 4;
    FILE* stream = nullptr;
    if (fopen_s(&stream, rawPath, "wb") == 0 && stream != nullptr) {
        for (UINT y = 0; y < description.Height; ++y) {
            const auto* row = static_cast<const std::uint8_t*>(locked.pBits) + y * locked.Pitch;
            std::fwrite(row, 1, rowBytes, stream);
        }
        std::fclose(stream);
    }

    if (fopen_s(&stream, bitmapPath, "wb") == 0 && stream != nullptr) {
        const DWORD pixelBytes = rowBytes * description.Height;
        DumpBitmapFileHeader fileHeader{
            0x4D42,
            static_cast<DWORD>(sizeof(DumpBitmapFileHeader) + sizeof(BITMAPINFOHEADER) +
                               pixelBytes),
            0, 0, static_cast<DWORD>(sizeof(DumpBitmapFileHeader) + sizeof(BITMAPINFOHEADER))};
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
            const auto* row = static_cast<const std::uint8_t*>(locked.pBits) + y * locked.Pitch;
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
            const double width = static_cast<double>(capture.right - capture.left);
            const double height = static_cast<double>(capture.bottom - capture.top);
            const double targetWidth = height * 4.0 / 3.0;
            if (targetWidth < width) {
                const double center = static_cast<double>(capture.left + capture.right) * 0.5;
                capture.left = static_cast<LONG>(center - targetWidth * 0.5);
                capture.right = static_cast<LONG>(center + targetWidth * 0.5);
            } else {
                const double targetHeight = width * 3.0 / 4.0;
                const double center = static_cast<double>(capture.top + capture.bottom) * 0.5;
                capture.top = static_cast<LONG>(center - targetHeight * 0.5);
                capture.bottom = static_cast<LONG>(center + targetHeight * 0.5);
            }
        }
        std::fprintf(stream,
                     "{\"schema\":2,\"width\":%u,\"height\":%u,"
                     "\"format\":%u,\"pitch\":%d,\"mode\":%d,"
                     "\"source_rect\":[%ld,%ld,%ld,%ld]}\n",
                     description.Width, description.Height,
                     static_cast<unsigned>(description.Format), locked.Pitch, mode, capture.left,
                     capture.top, capture.right, capture.bottom);
        std::fclose(stream);
    }

    if (isLocked)
        system->UnlockRect();
    system->Release();
    source->Release();
}
#else
inline void DumpAccessoryScreenTexture(MMDApp*, IDirect3DDevice9*) {
}
#endif

void ComposeSelfShadow(MMDApp* app, D3DRenderer* sub,
                       IDirect3DDevice9* device) { // 0x46DE61
    if (app->SelfShadowCompositionEnabled() == 0 || app->SelfShadowMode() <= 0)
        return;
    device->SetRenderState(D3DRS_ALPHABLENDENABLE, FALSE);
    DrawFullscreenQuad(device, sub->hdrTexture, app->LeftViewportVertices());
    device->SetRenderState(D3DRS_ALPHABLENDENABLE, TRUE);
    app->CpuRenderFrame().groundAlphaBlend = true;
}

void ComposeCallbackTexture(MMDApp* app,
                            IDirect3DDevice9* device) { // 0x46DF2A
    if (app->DepthTextureCompositionEnabled() == 0 || app->DepthDeviceEnabled() == 0 ||
        app->FrameStepPlayback() != 0)
        return;
    DepthTextureProvider callback = app->DepthTextureCallback();
    if (callback == nullptr)
        return;
    IDirect3DTexture9* texture = nullptr;
    callback(&texture);
    DrawFullscreenQuad(device, texture, app->RightViewportVertices());
}

void CaptureAccessoryScreenTexture(MMDApp* app, D3DRenderer* sub,
                                   IDirect3DDevice9* device) { // 0x46E025
    const int mode = static_cast<int>(app->CaptureMode());
    if (mode != 1 && mode != 2)
        return;
    auto*& texture = app->CaptureTexture();
    if (texture == nullptr) {
        HRESULT result = device->CreateTexture(1024, 1024, 1, D3DUSAGE_RENDERTARGET,
                                               D3DFMT_X8R8G8B8, D3DPOOL_DEFAULT, &texture, nullptr);
        if (result == D3DERR_OUTOFVIDEOMEMORY || result == E_OUTOFMEMORY)
            device->CreateTexture(512, 512, 1, D3DUSAGE_RENDERTARGET, D3DFMT_X8R8G8B8,
                                  D3DPOOL_DEFAULT, &texture, nullptr);
        if (texture == nullptr)
            return;
    }

    app->CpuNativeCaptureTexturePresent() = true;
    IDirect3DSurface9* textureSurface = nullptr;
    texture->GetSurfaceLevel(0, &textureSurface);
    auto** backBuffer = &sub->backbufferSurface;
    if (*backBuffer == nullptr)
        device->GetBackBuffer(0, 0, D3DBACKBUFFER_TYPE_MONO, backBuffer);
    if (*backBuffer != nullptr && textureSurface != nullptr)
        mme::PreRenderTargetCopy(app, device, *backBuffer);
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
        if (FAILED(device->StretchRect(*backBuffer, sourceRect, textureSurface, nullptr,
                                       D3DTEXF_LINEAR)))
            device->StretchRect(*backBuffer, sourceRect, textureSurface, nullptr, D3DTEXF_NONE);
    } else if (mode == 2 && textureSurface != nullptr && *backBuffer != nullptr) {
        RECT source = app->RecordingWindow() != nullptr
                          ? RECT{0, 0, app->RenderWidth(), app->RenderHeight()}
                          : app->ViewportRect();
        const auto cropped = render::ScreenSourceRect(
            {source.left, source.top, source.right, source.bottom},
            render::ScreenCaptureKind::CropFourByThree);
        source = {cropped.left, cropped.top, cropped.right, cropped.bottom};
        if (FAILED(
                device->StretchRect(*backBuffer, &source, textureSurface, nullptr, D3DTEXF_LINEAR)))
            device->StretchRect(*backBuffer, &source, textureSurface, nullptr, D3DTEXF_NONE);
    }
    if (textureSurface != nullptr)
        textureSurface->Release();
}

} // namespace

#ifdef MIKUDANCESTUDIO_DIAG
void WritePmmWorldParityReport(MMDApp* app);
#endif

void RenderFrameScene(MMDApp* app) {
    if (!ApplicationSwapchainCanRender(app))
        return;
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
                UpdateModelVertexBuffers(app, model, reinterpret_cast<const float*>(&frameWorld));
        }
    }
#ifdef MIKUDANCESTUDIO_DIAG
    WritePmmWorldParityReport(app);
#endif

    DWORD clearFlags = D3DCLEAR_TARGET | D3DCLEAR_ZBUFFER;
    if (sub->d3dInitialized != 0)
        clearFlags |= D3DCLEAR_STENCIL;
    // x86 0x46DC2D/0x46DC32, x64 0x7FF7CB44A2A2/0x7FF7CB44A2B1:
    // both backgrounds retain zero alpha, including image-file capture.
    const D3DCOLOR clearColor = app->state.blackBackgroundEnabled != 0
                                    ? D3DCOLOR_ARGB(0, 0, 0, 0)
                                    : D3DCOLOR_ARGB(0, 255, 255, 255);
    if (!PrepareDiagnosticSceneDepth(device))
        return;
    const HRESULT sceneClear = mme::ClearScene(app, device, clearFlags, clearColor, 1.0f, 0);
    (void)sceneClear;
#ifdef MIKUDANCESTUDIO_DIAG
    BeginDiagnosticHistoryAudit(device);
    BeginCpuFullSceneValidation(app);
    BeginHistoryReplay(device, clearFlags, clearColor, 1.0f, 0);
    if (FAILED(sceneClear))
        BlockHistoryReplay("initial_clear_failed");
#endif

#ifdef MIKUDANCESTUDIO_CPU_RENDERING
    BeginCpuSceneFrame(app, clearFlags, clearColor);
    if (FAILED(sceneClear))
        BlockCpuSceneFrame("initial_clear_failed");
#endif
    app->state.renderPassCount = 1;
    if (FAILED(mme::BeginScene(app, device))) {
#ifdef MIKUDANCESTUDIO_CPU_RENDERING
        BlockHistoryReplay("begin_scene_failed");
        FinishCpuSceneFrame(app);
        PresentCpuSceneFrame(app);
#endif
        return;
    }

    const bool effectRenderer = UseEffectModelRenderer(app);
    if (effectRenderer)
        RenderShadowMap(app, reinterpret_cast<const float*>(&frameWorld));

    // 0x46DDF6..0x46DE5F is deliberately a loop: render callbacks may add
    // another pass by incrementing A0270 while a pass is in progress.
    while (app->state.renderPassCount > 0) {
        --app->state.renderPassCount;
        if (effectRenderer)
            RenderModelsEffect(app, reinterpret_cast<const float*>(&frameWorld));
        else
            RenderModelsFixed(app);
    }

#ifdef MIKUDANCESTUDIO_CPU_RENDERING
    FinishCpuSceneFrame(app);
#ifdef MIKUDANCESTUDIO_DIAG
    FinishCpuFullSceneValidation(app);
#endif
    PresentCpuSceneFrame(app);
#endif
    ComposeSelfShadow(app, sub, device);
    ComposeCallbackTexture(app, device);

    // 0x46DFDE: mirror the full-size capture RT into the secondary-window
    // back buffer.  The original reacquires the back buffer after a reset.
    if (sub->multisampleAvailable == 0 && app->RecordingWindow() != nullptr) {
        auto** backBufferSlot = &sub->backbufferSurface;
        if (*backBufferSlot == nullptr)
            device->GetBackBuffer(0, 0, D3DBACKBUFFER_TYPE_MONO, backBufferSlot);
        DownsampleCaptureSurface(app);
    }

    CaptureAccessoryScreenTexture(app, sub, device);

    // 0x46E208: the recording/sub-window path ends the scene here; it never
    // draws debug geometry or the dynamic overlay batches.
    if (app->RecordingWindow() != nullptr) {
        mme::EndScene(app, device);
        ReportModelBatchComparison(device);
        DumpAccessoryScreenTexture(app, device);
        return;
    }

    // 0x46E214..0x46E554: accessory-dialog or Bullet collision debug pass.
    if (app->state.rigidBodyDisplayEnabled == 1 || app->FrameCopyOrPhysicsDialog() != 0) {
        device->SetRenderState(D3DRS_LIGHTING, FALSE);
        device->SetTexture(0, nullptr);
        device->SetRenderState(D3DRS_ZENABLE, FALSE);
        if (app->FrameCopyOrPhysicsDialog() != 0)
            DrawAccessoryDebug(app);
        else
            DrawPhysicsCollisionDebug(app->Physics());
        device->SetRenderState(D3DRS_ZENABLE, TRUE);
        device->SetRenderState(D3DRS_LIGHTING, TRUE);
    }

    // 0x46E556..0x46E592: selected-bone local/global operation axis.
    if (app->CameraMode() == 0 && app->PlaybackActive() == 0 &&
        app->FrameCopyOrPhysicsDialog() == 0) {
        DrawBoneOperationAxis(app, app->SelectedModel(),
                              reinterpret_cast<const float*>(&frameWorld));
    }

    device->SetRenderState(D3DRS_ALPHABLENDENABLE, TRUE);
    device->SetRenderState(D3DRS_SRCBLEND, D3DBLEND_SRCALPHA);
    device->SetRenderState(D3DRS_DESTBLEND, D3DBLEND_INVSRCALPHA);
    app->CpuRenderFrame().groundAlphaBlend = true;

    DrawTextOverlay(app, device);
    DrawLineOverlay(app, sub, device);
    DrawSpriteOverlay(app, device);
    mme::EndScene(app, device);
    ReportModelBatchComparison(device);
#ifdef MIKUDANCESTUDIO_DIAG
    ReportSelectionLiveCapture(device);
    ReportSpriteSameFrame(device);
#endif
    DumpAccessoryScreenTexture(app, device);
}

} // namespace mikudancestudio
