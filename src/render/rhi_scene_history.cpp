#include "mikudancestudio/rhi_scene_history.hpp"
#ifdef MIKUDANCESTUDIO_DIAG
#define NOMINMAX
#include <Windows.h>
#include <d3d9.h>
#include <d3dcompiler.h>
#include <wrl/client.h>
#include <cmath>
#include <algorithm>
#include <cstring>
#include <fstream>
#include <string>
using Microsoft::WRL::ComPtr;
namespace mikudancestudio {
namespace {
bool Enabled() {
    char flag[2]{};
    return GetEnvironmentVariableA("MDS_RHI_SCENE_HISTORY", flag, 2) == 1 && flag[0] == '1';
}
struct Vertex {
    float x, y, z, w, u, v;
};
std::ofstream audit;
unsigned auditDraws = 0, auditModels = 0, auditOther = 0;
bool auditSaved = false;
}
void BeginDiagnosticHistoryAudit(IDirect3DDevice9* device) {
    if (auditSaved)
        return;
    char flag[2]{};
    if (GetEnvironmentVariableA("MDS_RHI_MSAA_REPLAY_AUDIT", flag, 2) != 1 || flag[0] != '1')
        return;
    char directory[MAX_PATH]{};
    const auto length = GetEnvironmentVariableA("MDS_RHI_LIVE_COMPARE_DIR", directory, MAX_PATH);
    if (!length || length >= MAX_PATH)
        return;
    if (audit.is_open())
        audit.close();
    audit.clear();
    audit.open(std::string(directory) + "/msaa_replay.prefix", std::ios::trunc);
    auditDraws = auditModels = auditOther = 0;
    ComPtr<IDirect3DSurface9> target;
    D3DSURFACE_DESC desc{};
    if (FAILED(device->GetRenderTarget(0, &target)) || FAILED(target->GetDesc(&desc))) {
        audit.close();
        return;
    }
    audit << "BEGIN boundary=afterSceneClear samples=" << desc.MultiSampleType
          << " quality=" << desc.MultiSampleQuality << "\n";
}
void AuditDiagnosticHistoryDraw(IDirect3DDevice9* device, unsigned type, unsigned count,
                                bool indexed) {
    if (!audit.is_open())
        return;
    DWORD fvf = 0, lighting = 0, blend = 0, depth = 0;
    device->GetFVF(&fvf);
    device->GetRenderState(D3DRS_LIGHTING, &lighting);
    device->GetRenderState(D3DRS_ALPHABLENDENABLE, &blend);
    device->GetRenderState(D3DRS_ZENABLE, &depth);
    ++auditDraws;
    if (fvf == (D3DFVF_XYZ | D3DFVF_NORMAL | D3DFVF_TEX1))
        ++auditModels;
    else
        ++auditOther;
    audit << "DRAW sequence=" << auditDraws << " primitive=" << type << " count=" << count
          << " indexed=" << indexed << " fvf=" << fvf << " lighting=" << lighting
          << " blend=" << blend << " depth=" << depth << "\n";
}
void EndDiagnosticHistoryAudit(IDirect3DDevice9*, unsigned slot, unsigned material,
                               unsigned count) {
    if (!audit.is_open())
        return;
    audit
        << "GATE status=BLOCKED slot=" << slot << " material=" << material << " indices=" << count
        << " observedDraws=" << auditDraws << " modelDraws=" << auditModels
        << " otherDraws=" << auditOther
        << " completeDrawReplay=0 allEventsRecorded=0 allResourcesVersioned=0 reason=drawAuditIsNotReplayStream\n";
    audit.close();
    auditSaved = true;
}
bool PrepareDiagnosticSceneDepth(IDirect3DDevice9* device) {
    if (!Enabled())
        return true;
    ComPtr<IDirect3DSurface9> target, old;
    D3DSURFACE_DESC desc{}, ds{};
    if (FAILED(device->GetRenderTarget(0, &target)) || FAILED(target->GetDesc(&desc)) ||
        desc.MultiSampleType != D3DMULTISAMPLE_NONE ||
        FAILED(device->GetDepthStencilSurface(&old)) || FAILED(old->GetDesc(&ds)))
        return false;
    const auto intz = static_cast<D3DFORMAT>(MAKEFOURCC('I', 'N', 'T', 'Z'));
    if (ds.Format == intz)
        return true;
    ComPtr<IDirect3DTexture9> texture;
    ComPtr<IDirect3DSurface9> surface;
    if (FAILED(device->CreateTexture(desc.Width, desc.Height, 1, D3DUSAGE_DEPTHSTENCIL, intz,
                                     D3DPOOL_DEFAULT, &texture, nullptr)) ||
        FAILED(texture->GetSurfaceLevel(0, &surface)) ||
        FAILED(device->SetDepthStencilSurface(surface.Get())))
        return false;
    // The bound surface owns its container; no static DEFAULT-pool reference
    // survives reset. This mode uses the driver INTZ extension explicitly.
    return true;
}
rhi::HistoryCapabilities DiagnosticHistoryFeatures(IDirect3DDevice9* device) {
    rhi::HistoryCapabilities capabilities;
    if (!Enabled())
        return capabilities;
    ComPtr<IDirect3DSurface9> depth;
    D3DSURFACE_DESC desc{};
    if (FAILED(device->GetDepthStencilSurface(&depth)) || FAILED(depth->GetDesc(&desc)) ||
        desc.MultiSampleType != D3DMULTISAMPLE_NONE ||
        desc.Format != static_cast<D3DFORMAT>(MAKEFOURCC('I', 'N', 'T', 'Z')))
        return capabilities;
    capabilities.colorCapture = rhi::HistoryAccess::Native;
    capabilities.depthCapture = capabilities.stencilCapture = rhi::HistoryAccess::DriverExtension;
    // This capture service does not implement restore. The standalone replay
    // reader reconstructs targets independently; do not advertise it here.
    return capabilities;
}
rhi::HistoryResult CaptureDiagnosticHistory(IDirect3DDevice9* device, rhi::HistoryData& output) {
    output = {};
    D3DVIEWPORT9 viewport{};
    if (FAILED(device->GetViewport(&viewport)))
        return rhi::HistoryResult::DeviceLost;
    rhi::HistoryDesc desc;
    desc.width = viewport.Width;
    desc.height = viewport.Height;
    if (!rhi::CanCaptureHistory(DiagnosticHistoryFeatures(device), desc))
        return rhi::HistoryResult::Unsupported;
    if (!ReadDiagnosticSceneHistory(device, output.rgba, output.depthStencil)) {
        output = {};
        return rhi::HistoryResult::Invalid;
    }
    output.description = desc;
    if (!rhi::ValidHistoryData(output)) {
        output = {};
        return rhi::HistoryResult::Invalid;
    }
    return rhi::HistoryResult::Success;
}
bool ReadDiagnosticSceneHistory(IDirect3DDevice9* device, std::vector<std::uint8_t>& color,
                                std::vector<std::uint32_t>& depthStencil) {
    if (!Enabled())
        return false;
    ComPtr<IDirect3DSurface9> target, depth;
    ComPtr<IDirect3DTexture9> depthTexture;
    ComPtr<IDirect3DStateBlock9> state;
    D3DVIEWPORT9 viewport{};
    D3DSURFACE_DESC desc{}, ds{};
    if (FAILED(device->GetRenderTarget(0, &target)) ||
        FAILED(device->GetDepthStencilSurface(&depth)) || FAILED(target->GetDesc(&desc)) ||
        FAILED(depth->GetDesc(&ds)) || desc.MultiSampleType != D3DMULTISAMPLE_NONE ||
        (desc.Format != D3DFMT_A8R8G8B8 && desc.Format != D3DFMT_X8R8G8B8) ||
        ds.Format != static_cast<D3DFORMAT>(MAKEFOURCC('I', 'N', 'T', 'Z')) ||
        FAILED(depth->GetContainer(__uuidof(IDirect3DTexture9),
                                   reinterpret_cast<void**>(depthTexture.GetAddressOf()))) ||
        FAILED(device->GetViewport(&viewport)) ||
        FAILED(device->CreateStateBlock(D3DSBT_ALL, &state)) || FAILED(state->Capture()))
        return false;
    struct Restore {
        IDirect3DDevice9* d;
        IDirect3DSurface9* rt;
        IDirect3DSurface9* ds;
        IDirect3DStateBlock9* s;
        D3DVIEWPORT9 v;
        bool done = false;
        bool Apply() {
            bool good = true;
            good = SUCCEEDED(d->SetTexture(0, nullptr)) && good;
            good = SUCCEEDED(d->SetRenderTarget(0, rt)) && good;
            good = SUCCEEDED(d->SetDepthStencilSurface(ds)) && good;
            good = SUCCEEDED(s->Apply()) && good;
            good = SUCCEEDED(d->SetViewport(&v)) && good;
            done = true;
            return good;
        }
        ~Restore() {
            if (!done)
                Apply();
        }
    } restore{device, target.Get(), depth.Get(), state.Get(), viewport};
    if (viewport.X + viewport.Width > desc.Width || viewport.Y + viewport.Height > desc.Height)
        return false;
    color.resize(viewport.Width * viewport.Height * 4);
    depthStencil.resize(viewport.Width * viewport.Height);
    ComPtr<IDirect3DSurface9> readColor, readFloat, floatTarget, stencilTarget;
    if (FAILED(device->CreateOffscreenPlainSurface(desc.Width, desc.Height, desc.Format,
                                                   D3DPOOL_SYSTEMMEM, &readColor, nullptr)) ||
        FAILED(device->GetRenderTargetData(target.Get(), readColor.Get())))
        return false;
    D3DLOCKED_RECT lock{};
    if (FAILED(readColor->LockRect(&lock, nullptr, D3DLOCK_READONLY)))
        return false;
    for (unsigned y = 0; y < viewport.Height; ++y)
        for (unsigned x = 0; x < viewport.Width; ++x) {
            const auto* p = static_cast<const unsigned char*>(lock.pBits) +
                            (y + viewport.Y) * lock.Pitch + (x + viewport.X) * 4;
            auto* q = color.data() + (y * viewport.Width + x) * 4;
            q[0] = p[2];
            q[1] = p[1];
            q[2] = p[0];
            q[3] = desc.Format == D3DFMT_X8R8G8B8 ? 255 : p[3];
        }
    if (FAILED(readColor->UnlockRect()) ||
        FAILED(device->CreateRenderTarget(desc.Width, desc.Height, D3DFMT_R32F, D3DMULTISAMPLE_NONE,
                                          0, FALSE, &floatTarget, nullptr)) ||
        FAILED(device->CreateOffscreenPlainSurface(desc.Width, desc.Height, D3DFMT_R32F,
                                                   D3DPOOL_SYSTEMMEM, &readFloat, nullptr)) ||
        FAILED(device->CreateRenderTarget(desc.Width, desc.Height, desc.Format, D3DMULTISAMPLE_NONE,
                                          0, FALSE, &stencilTarget, nullptr)))
        return false;
    const char* code =
        "sampler2D depth:register(s0);float4 main(float2 uv:TEXCOORD0):COLOR0{return float4(tex2D(depth,uv).r,0,0,1);}";
    ComPtr<ID3DBlob> compiled, errors;
    ComPtr<IDirect3DPixelShader9> shader;
    if (FAILED(D3DCompile(code, std::strlen(code), nullptr, nullptr, nullptr, "main", "ps_3_0",
                          D3DCOMPILE_IEEE_STRICTNESS, 0, &compiled, &errors)) ||
        FAILED(device->CreatePixelShader(static_cast<const DWORD*>(compiled->GetBufferPointer()),
                                         &shader)))
        return false;
    const Vertex quad[] = {{-.5f, -.5f, 0, 1, 0, 0},
                           {desc.Width - .5f, -.5f, 0, 1, 1, 0},
                           {-.5f, desc.Height - .5f, 0, 1, 0, 1},
                           {desc.Width - .5f, desc.Height - .5f, 0, 1, 1, 1}};
    const auto rs = [&](D3DRENDERSTATETYPE k, DWORD v) {
        return SUCCEEDED(device->SetRenderState(k, v));
    };
    D3DVIEWPORT9 full{0, 0, desc.Width, desc.Height, 0, 1};
    if (FAILED(device->SetRenderTarget(0, floatTarget.Get())) ||
        FAILED(device->SetDepthStencilSurface(nullptr)) || FAILED(device->SetViewport(&full)) ||
        FAILED(device->SetVertexShader(nullptr)) || FAILED(device->SetPixelShader(shader.Get())) ||
        FAILED(device->SetFVF(D3DFVF_XYZRHW | D3DFVF_TEX1)) ||
        FAILED(device->SetTexture(0, depthTexture.Get())) || !rs(D3DRS_ZENABLE, FALSE) ||
        !rs(D3DRS_ZWRITEENABLE, FALSE) || !rs(D3DRS_STENCILENABLE, FALSE) ||
        !rs(D3DRS_ALPHATESTENABLE, FALSE) || !rs(D3DRS_ALPHABLENDENABLE, FALSE) ||
        !rs(D3DRS_FOGENABLE, FALSE) || !rs(D3DRS_CULLMODE, D3DCULL_NONE) ||
        !rs(D3DRS_SCISSORTESTENABLE, FALSE) || !rs(D3DRS_COLORWRITEENABLE, 15) ||
        !rs(D3DRS_SRGBWRITEENABLE, FALSE) || !rs(D3DRS_FILLMODE, D3DFILL_SOLID) ||
        !rs(D3DRS_TWOSIDEDSTENCILMODE, FALSE))
        return false;
    for (auto k : {D3DSAMP_MINFILTER, D3DSAMP_MAGFILTER})
        if (FAILED(device->SetSamplerState(0, k, D3DTEXF_POINT)))
            return false;
    if (FAILED(device->SetSamplerState(0, D3DSAMP_MIPFILTER, D3DTEXF_NONE)) ||
        FAILED(device->SetSamplerState(0, D3DSAMP_SRGBTEXTURE, 0)) ||
        FAILED(device->DrawPrimitiveUP(D3DPT_TRIANGLESTRIP, 2, quad, sizeof(Vertex))) ||
        FAILED(device->GetRenderTargetData(floatTarget.Get(), readFloat.Get())) ||
        FAILED(readFloat->LockRect(&lock, nullptr, D3DLOCK_READONLY)))
        return false;
    bool valid = true;
    for (unsigned y = 0; y < viewport.Height; ++y)
        for (unsigned x = 0; x < viewport.Width; ++x) {
            float value;
            std::memcpy(&value,
                        static_cast<const unsigned char*>(lock.pBits) +
                            (y + viewport.Y) * lock.Pitch + (x + viewport.X) * 4,
                        4);
            if (!std::isfinite(value) || value < 0 || value > 1)
                valid = false;
            depthStencil[y * viewport.Width + x] = static_cast<std::uint32_t>(
                std::floor(std::clamp(static_cast<double>(value), 0.0, 1.0) * 16777215.0 + .5));
        }
    if (FAILED(readFloat->UnlockRect()) || !valid || FAILED(device->SetTexture(0, nullptr)) ||
        FAILED(device->SetPixelShader(nullptr)) ||
        FAILED(device->SetRenderTarget(0, stencilTarget.Get())) ||
        FAILED(device->SetDepthStencilSurface(depth.Get())) || FAILED(device->SetViewport(&full)) ||
        !rs(D3DRS_LIGHTING, FALSE) || !rs(D3DRS_STENCILENABLE, TRUE) ||
        !rs(D3DRS_STENCILFUNC, D3DCMP_EQUAL) || !rs(D3DRS_STENCILMASK, 255) ||
        !rs(D3DRS_STENCILWRITEMASK, 0) || !rs(D3DRS_STENCILFAIL, D3DSTENCILOP_KEEP) ||
        !rs(D3DRS_STENCILZFAIL, D3DSTENCILOP_KEEP) || !rs(D3DRS_STENCILPASS, D3DSTENCILOP_KEEP))
        return false;
    for (auto pair : {std::pair{D3DTSS_COLOROP, DWORD(D3DTOP_SELECTARG1)},
                      std::pair{D3DTSS_COLORARG1, DWORD(D3DTA_TFACTOR)},
                      std::pair{D3DTSS_ALPHAOP, DWORD(D3DTOP_SELECTARG1)},
                      std::pair{D3DTSS_ALPHAARG1, DWORD(D3DTA_TFACTOR)}})
        if (FAILED(device->SetTextureStageState(0, pair.first, pair.second)))
            return false;
    if (FAILED(device->SetTextureStageState(1, D3DTSS_COLOROP, D3DTOP_DISABLE)))
        return false;
    for (unsigned reference = 0; reference < 256; ++reference)
        if (!rs(D3DRS_STENCILREF, reference) ||
            !rs(D3DRS_TEXTUREFACTOR, 0xff000000 | (reference << 16)) ||
            FAILED(device->DrawPrimitiveUP(D3DPT_TRIANGLESTRIP, 2, quad, sizeof(Vertex))))
            return false;
    if (FAILED(device->GetRenderTargetData(stencilTarget.Get(), readColor.Get())) ||
        FAILED(readColor->LockRect(&lock, nullptr, D3DLOCK_READONLY)))
        return false;
    for (unsigned y = 0; y < viewport.Height; ++y)
        for (unsigned x = 0; x < viewport.Width; ++x) {
            const auto* p = static_cast<const unsigned char*>(lock.pBits) +
                            (y + viewport.Y) * lock.Pitch + (x + viewport.X) * 4;
            depthStencil[y * viewport.Width + x] |= std::uint32_t(p[2]) << 24;
        }
    const bool unlocked = SUCCEEDED(readColor->UnlockRect());
    return restore.Apply() && unlocked;
}
}
#endif
