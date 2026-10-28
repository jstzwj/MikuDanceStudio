#include "mikudancestudio/rhi_d3d9_mip_capture.hpp"
#include <d3dcompiler.h>
#include <wrl/client.h>
#include <array>
#include <cstring>
using Microsoft::WRL::ComPtr;
namespace mikudancestudio::rhi {
HRESULT CaptureD3D9GeneratedMips(IDirect3DDevice9* d, IDirect3DTexture9* source, bool sceneActive,
                                 ModelTextureData& out) {
    out = {};
    if (!d || !source)
        return E_INVALIDARG;
    D3DSURFACE_DESC desc{};
    D3DCAPS9 caps{};
    ComPtr<IDirect3DDevice9> owner;
    HRESULT hr = source->GetDevice(&owner);
    if (FAILED(hr))
        return hr;
    if (owner.Get() != d)
        return E_INVALIDARG;
    if (FAILED(hr = source->GetLevelDesc(0, &desc)) || FAILED(hr = d->GetDeviceCaps(&caps)))
        return hr;
    if (!(desc.Usage & D3DUSAGE_AUTOGENMIPMAP) ||
        (desc.Format != D3DFMT_A8R8G8B8 && desc.Format != D3DFMT_X8R8G8B8) ||
        source->GetLOD() != 0 || source->GetLevelCount() != 1 || !desc.Width || !desc.Height ||
        desc.Width > 4096 || desc.Height > 4096 || caps.PixelShaderVersion < D3DPS_VERSION(3, 0) ||
        (source->GetAutoGenFilterType() != D3DTEXF_POINT &&
         source->GetAutoGenFilterType() != D3DTEXF_LINEAR))
        return D3DERR_NOTAVAILABLE;
    static const char shader[] =
        "sampler2D source:register(s0); float4 level:register(c0); float4 main(float2 uv:TEXCOORD0):COLOR0 {return tex2Dlod(source,float4(uv,0,level.x));}";
    ComPtr<ID3DBlob> code, error;
    ComPtr<IDirect3DPixelShader9> ps;
    if (FAILED(hr = D3DCompile(shader, sizeof(shader) - 1, nullptr, nullptr, nullptr, "main",
                               "ps_3_0", D3DCOMPILE_IEEE_STRICTNESS, 0, &code, &error)) ||
        FAILED(hr = d->CreatePixelShader(static_cast<const DWORD*>(code->GetBufferPointer()), &ps)))
        return hr;
    struct Restore {
        IDirect3DDevice9* d;
        ComPtr<IDirect3DStateBlock9> state;
        std::array<ComPtr<IDirect3DSurface9>, 4> targets;
        ComPtr<IDirect3DSurface9> depth;
        D3DVIEWPORT9 viewport{};
        DWORD count = 0;
        bool ready = false, done = false, inside = false, resume = false;
        HRESULT Apply() {
            done = true;
            HRESULT status = S_OK;
            auto check = [&](HRESULT r) {
                if (FAILED(r) && SUCCEEDED(status))
                    status = r;
            };
            if (inside) {
                check(d->EndScene());
                inside = false;
            }
            for (DWORD i = 1; i < count; ++i)
                check(d->SetRenderTarget(i, nullptr));
            check(d->SetDepthStencilSurface(nullptr));
            check(d->SetRenderTarget(0, targets[0].Get()));
            for (DWORD i = 1; i < count; ++i)
                check(d->SetRenderTarget(i, targets[i].Get()));
            check(d->SetDepthStencilSurface(depth.Get()));
            check(state->Apply());
            check(d->SetViewport(&viewport));
            if (resume)
                check(d->BeginScene());
            return status;
        }
        ~Restore() {
            if (ready && !done)
                Apply();
        }
    } restore{d};
    restore.count = caps.NumSimultaneousRTs < 4 ? caps.NumSimultaneousRTs : 4;
    if (FAILED(hr = d->CreateStateBlock(D3DSBT_ALL, &restore.state)) ||
        FAILED(hr = d->GetViewport(&restore.viewport)))
        return hr;
    for (DWORD i = 0; i < restore.count; ++i) {
        hr = d->GetRenderTarget(i, &restore.targets[i]);
        if (FAILED(hr) && (i == 0 || hr != D3DERR_NOTFOUND))
            return hr;
    }
    hr = d->GetDepthStencilSurface(&restore.depth);
    if (FAILED(hr) && hr != D3DERR_NOTFOUND)
        return hr;
    if (sceneActive && FAILED(hr = d->EndScene()))
        return hr;
    restore.resume = sceneActive;
    restore.ready = true;
    for (DWORD i = 1; i < restore.count; ++i)
        if (FAILED(hr = d->SetRenderTarget(i, nullptr)))
            return hr;
    if (FAILED(hr = d->SetDepthStencilSurface(nullptr)) ||
        FAILED(hr = d->SetVertexShader(nullptr)) || FAILED(hr = d->SetPixelShader(ps.Get())) ||
        FAILED(hr = d->SetFVF(D3DFVF_XYZRHW | D3DFVF_TEX1)) ||
        FAILED(hr = d->SetTexture(0, source)) ||
        FAILED(hr = d->SetTextureStageState(0, D3DTSS_TEXCOORDINDEX, 0)) ||
        FAILED(hr = d->SetTextureStageState(0, D3DTSS_TEXTURETRANSFORMFLAGS, D3DTTFF_DISABLE)))
        return hr;
    const std::pair<D3DRENDERSTATETYPE, DWORD> states[] = {{D3DRS_ZENABLE, FALSE},
                                                           {D3DRS_ZWRITEENABLE, FALSE},
                                                           {D3DRS_ALPHABLENDENABLE, FALSE},
                                                           {D3DRS_ALPHATESTENABLE, FALSE},
                                                           {D3DRS_STENCILENABLE, FALSE},
                                                           {D3DRS_SCISSORTESTENABLE, FALSE},
                                                           {D3DRS_FOGENABLE, FALSE},
                                                           {D3DRS_DITHERENABLE, FALSE},
                                                           {D3DRS_SRGBWRITEENABLE, FALSE},
                                                           {D3DRS_CULLMODE, D3DCULL_NONE},
                                                           {D3DRS_FILLMODE, D3DFILL_SOLID},
                                                           {D3DRS_LIGHTING, FALSE},
                                                           {D3DRS_SPECULARENABLE, FALSE},
                                                           {D3DRS_SHADEMODE, D3DSHADE_GOURAUD},
                                                           {D3DRS_VERTEXBLEND, D3DVBF_DISABLE},
                                                           {D3DRS_INDEXEDVERTEXBLENDENABLE, FALSE},
                                                           {D3DRS_COLORWRITEENABLE, 15},
                                                           {D3DRS_CLIPPLANEENABLE, 0},
                                                           {D3DRS_MULTISAMPLEMASK, 0xffffffffu}};
    for (auto s : states)
        if (FAILED(hr = d->SetRenderState(s.first, s.second)))
            return hr;
    const std::pair<D3DSAMPLERSTATETYPE, DWORD> samplers[] = {{D3DSAMP_MINFILTER, D3DTEXF_POINT},
                                                              {D3DSAMP_MAGFILTER, D3DTEXF_POINT},
                                                              {D3DSAMP_MIPFILTER, D3DTEXF_POINT},
                                                              {D3DSAMP_ADDRESSU, D3DTADDRESS_CLAMP},
                                                              {D3DSAMP_ADDRESSV, D3DTADDRESS_CLAMP},
                                                              {D3DSAMP_SRGBTEXTURE, 0},
                                                              {D3DSAMP_MIPMAPLODBIAS, 0},
                                                              {D3DSAMP_MAXMIPLEVEL, 0}};
    for (auto s : samplers)
        if (FAILED(hr = d->SetSamplerState(0, s.first, s.second)))
            return hr;
    ModelTextureData captured;
    unsigned w = desc.Width, h = desc.Height;
    // Do not regenerate or otherwise mutate the source's mip contents.
    for (unsigned level = 0;; ++level) {
        ComPtr<IDirect3DSurface9> target, cpu;
        if (FAILED(hr = d->CreateRenderTarget(w, h, D3DFMT_A8R8G8B8, D3DMULTISAMPLE_NONE, 0, FALSE,
                                              &target, nullptr)) ||
            FAILED(hr = d->CreateOffscreenPlainSurface(w, h, D3DFMT_A8R8G8B8, D3DPOOL_SYSTEMMEM,
                                                       &cpu, nullptr)) ||
            FAILED(hr = d->SetRenderTarget(0, target.Get())))
            return hr;
        const D3DVIEWPORT9 viewport{0, 0, w, h, 0, 1};
        const float lod[4] = {float(level), 0, 0, 0};
        if (FAILED(hr = d->SetViewport(&viewport)) ||
            FAILED(hr = d->SetPixelShaderConstantF(0, lod, 1)) || FAILED(hr = d->BeginScene()))
            return hr;
        restore.inside = true;
        struct Vertex {
            float x, y, z, rhw, u, v;
        };
        const Vertex quad[] = {{-.5f, -.5f, 0, 1, 0, 0},
                               {float(w) - .5f, -.5f, 0, 1, 1, 0},
                               {-.5f, float(h) - .5f, 0, 1, 0, 1},
                               {float(w) - .5f, float(h) - .5f, 0, 1, 1, 1}};
        if (FAILED(hr = d->DrawPrimitiveUP(D3DPT_TRIANGLESTRIP, 2, quad, sizeof(Vertex))) ||
            FAILED(hr = d->EndScene()))
            return hr;
        restore.inside = false;
        if (FAILED(hr = d->GetRenderTargetData(target.Get(), cpu.Get())))
            return hr;
        D3DLOCKED_RECT lock{};
        if (FAILED(hr = cpu->LockRect(&lock, nullptr, D3DLOCK_READONLY)))
            return hr;
        RgbaImage image{w, h, std::vector<std::uint8_t>(std::size_t(w) * h * 4)};
        for (unsigned y = 0; y < h; ++y)
            for (unsigned x = 0; x < w; ++x) {
                auto* src = static_cast<const unsigned char*>(lock.pBits) + y * lock.Pitch + x * 4;
                auto* dst = image.pixels.data() + (std::size_t(y) * w + x) * 4;
                dst[0] = src[2];
                dst[1] = src[1];
                dst[2] = src[0];
                dst[3] = src[3];
            }
        if (FAILED(hr = cpu->UnlockRect()))
            return hr;
        captured.levels.push_back(std::move(image));
        if (w == 1 && h == 1)
            break;
        w = w > 1 ? w / 2 : 1;
        h = h > 1 ? h / 2 : 1;
    }
    if (FAILED(hr = restore.Apply()))
        return hr;
    out = std::move(captured);
    return S_OK;
}
}
