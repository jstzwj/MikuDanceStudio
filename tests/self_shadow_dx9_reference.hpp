#pragma once
// Independent oracle: the shipped SM3 effect, compiled for a native DX9 device.
// No editor, visible window, desktop capture, input, or presentation is used.
#include "mikudancestudio/rhi_frame_passes.hpp"
#include <d3dx9.h>
#include <wrl/client.h>
#include <cstring>
#include <map>
#include <stdexcept>
namespace shadow_reference {
using namespace mikudancestudio::rhi;
using Microsoft::WRL::ComPtr;
inline void Check(HRESULT hr) {
    if (FAILED(hr))
        throw std::runtime_error("DX9 HRESULT=" + std::to_string(unsigned(hr)));
}
class Reference {
    struct Target {
        RenderTextureDesc description;
        ComPtr<IDirect3DTexture9> texture;
        ComPtr<IDirect3DSurface9> surface;
    };
    struct Resource {
        ComPtr<IDirect3DVertexBuffer9> vertices;
        ComPtr<IDirect3DIndexBuffer9> indices;
        ComPtr<IDirect3DTexture9> texture;
    };
    // HWND_MESSAGE creates no top-level window and cannot become foreground.
    struct MessageWindow {
        HWND handle = CreateWindowExW(0, L"STATIC", L"MDS shadow oracle", 0, 0, 0, 1, 1,
                                      HWND_MESSAGE, nullptr, GetModuleHandleW(nullptr), nullptr);
        ~MessageWindow() {
            if (handle)
                DestroyWindow(handle);
        }
    } window;
    ComPtr<IDirect3D9> api;
    ComPtr<IDirect3DDevice9> device;
    ComPtr<ID3DXEffect> effect;
    ComPtr<IDirect3DTexture9> white;
    std::map<std::uint64_t, Target> targets;
    std::map<ReplayResourceKey, Resource> resources;
    void Texture(const ModelTextureData& image, ComPtr<IDirect3DTexture9>& result) {
        const auto& top = image.levels.front();
        Check(device->CreateTexture(top.width, top.height, UINT(image.levels.size()), 0,
                                    D3DFMT_A8R8G8B8, D3DPOOL_MANAGED, &result, nullptr));
        for (unsigned mip = 0; mip < image.levels.size(); ++mip) {
            const auto& level = image.levels[mip];
            D3DLOCKED_RECT lock{};
            Check(result->LockRect(mip, &lock, nullptr, 0));
            for (unsigned y = 0; y < level.height; ++y)
                for (unsigned x = 0; x < level.width; ++x) {
                    const auto* source = level.pixels.data() + (y * level.width + x) * 4;
                    auto* dest = static_cast<unsigned char*>(lock.pBits) + y * lock.Pitch + x * 4;
                    dest[0] = source[2];
                    dest[1] = source[1];
                    dest[2] = source[0];
                    dest[3] = source[3];
                }
            Check(result->UnlockRect(mip));
        }
    }
    void State(const ModelPipeline& p) {
        const auto set = [&](D3DRENDERSTATETYPE key, DWORD value) {
            Check(device->SetRenderState(key, value));
        };
        static const DWORD blends[] = {
            D3DBLEND_ZERO,      D3DBLEND_ONE,          D3DBLEND_SRCCOLOR,   D3DBLEND_INVSRCCOLOR,
            D3DBLEND_SRCALPHA,  D3DBLEND_INVSRCALPHA,  D3DBLEND_DESTALPHA,  D3DBLEND_INVDESTALPHA,
            D3DBLEND_DESTCOLOR, D3DBLEND_INVDESTCOLOR, D3DBLEND_SRCALPHASAT};
        set(D3DRS_ZENABLE, p.raster.depthTest);
        set(D3DRS_ZWRITEENABLE, p.raster.depthWrite);
        set(D3DRS_ZFUNC, unsigned(p.raster.depthCompare) + 1);
        set(D3DRS_CULLMODE, unsigned(p.raster.cull) + 1);
        set(D3DRS_ALPHABLENDENABLE, p.raster.alphaBlend);
        set(D3DRS_SRCBLEND, blends[unsigned(p.raster.sourceBlend)]);
        set(D3DRS_DESTBLEND, blends[unsigned(p.raster.destinationBlend)]);
        set(D3DRS_BLENDOP, unsigned(p.colorOperation));
        set(D3DRS_SEPARATEALPHABLENDENABLE, p.separateAlpha);
        set(D3DRS_SRCBLENDALPHA, blends[unsigned(p.sourceAlpha)]);
        set(D3DRS_DESTBLENDALPHA, blends[unsigned(p.destinationAlpha)]);
        set(D3DRS_BLENDOPALPHA, unsigned(p.alphaOperation));
        set(D3DRS_ALPHATESTENABLE, p.shader.alphaTest);
        set(D3DRS_ALPHAFUNC, unsigned(p.shader.alphaCompare) + 1);
        set(D3DRS_ALPHAREF, p.shader.alphaReference);
        set(D3DRS_COLORWRITEENABLE, p.colorWriteMask);
        set(D3DRS_STENCILENABLE, p.stencil);
        set(D3DRS_STENCILREF, p.stencilReference);
        set(D3DRS_STENCILFUNC, unsigned(p.stencilCompare) + 1);
        set(D3DRS_STENCILMASK, p.stencilReadMask);
        set(D3DRS_STENCILWRITEMASK, p.stencilWriteMask);
        set(D3DRS_STENCILFAIL, unsigned(p.stencilFail));
        set(D3DRS_STENCILZFAIL, unsigned(p.stencilDepthFail));
        set(D3DRS_STENCILPASS, unsigned(p.stencilPass));
        set(D3DRS_SRGBWRITEENABLE, FALSE);
        set(D3DRS_LIGHTING, FALSE);
    }
    void Draw(const ModelReplayDraw& draw) {
        const auto& p = draw.pipeline;
        const auto& s = p.shadow;
        const auto& v = resources.at(draw.vertices);
        Check(device->SetStreamSource(0, v.vertices.Get(), 0, ModelVertexStride(p.vertexLayout)));
        Check(device->SetFVF(p.vertexLayout == VertexLayout::PositionColor
                                 ? D3DFVF_XYZ | D3DFVF_DIFFUSE
                                 : D3DFVF_XYZ | D3DFVF_NORMAL | D3DFVF_TEX1));
        if (draw.indexed)
            Check(device->SetIndices(resources.at(draw.indices).indices.Get()));
        State(p);
        if (p.viewport) {
            const auto& v = *p.viewport;
            D3DVIEWPORT9 viewport{v.x, v.y, v.width, v.height, v.minDepth, v.maxDepth};
            Check(device->SetViewport(&viewport));
        }
        const auto matrix = [&](const char* key, const auto& value) {
            Check(effect->SetMatrix(key, reinterpret_cast<const D3DXMATRIX*>(value.data())));
        };
        const auto color = [&](const char* key, const auto& value) {
            Check(effect->SetFloatArray(key, value.data(), 4));
        };
        matrix("matLightViewProj", s.lightViewProjection);
        matrix("matWorldViewProj", s.worldViewProjection);
        matrix("matRotate", s.rotation);
        color("EgColor", s.color);
        color("ToonColor", s.toonColor);
        color("SpcColor", s.specular);
        color("LightDir", s.lightDirection);
        color("Place", s.eye);
        const float one[] = {1, 1, 1, 1}, zero[] = {0, 0, 0, 0};
        Check(effect->SetFloatArray("TexCMul", one, 4));
        Check(effect->SetFloatArray("SphCMul", one, 4));
        Check(effect->SetFloatArray("TexCAdd", zero, 4));
        Check(effect->SetFloatArray("SphCAdd", zero, 4));
        Check(effect->SetBool("parthf", s.perspective));
        Check(effect->SetBool("transp", s.transparent));
        IDirect3DTexture9 *base = nullptr, *sphere = nullptr;
        bool additive = false;
        for (unsigned stage = 0; stage < 3; ++stage) {
            const auto& t = p.shader.layout.stages[stage];
            if (t.role == MaterialTextureRole::Base)
                base = resources.at(draw.textures[stage]).texture.Get();
            if (t.role == MaterialTextureRole::Sphere) {
                sphere = resources.at(draw.textures[stage]).texture.Get();
                additive = t.color == MaterialCombine::Add;
            }
        }
        const char* technique = s.mode == BuiltinShadowMode::Depth     ? "ZValuePlotTec"
                                : s.mode == BuiltinShadowMode::Outline ? "ColorRenderTec"
                                : base && sphere                       ? "BShadowSphiaTextureTec"
                                : sphere                               ? "BShadowSphiaTec"
                                : base                                 ? "BShadowTextureTec"
                                                                       : "BufferShadowTec";
        if (s.mode == BuiltinShadowMode::None) {
            // The bare-model full-frame fixture has only fixed black PMD edges
            // outside the built-in effect; preserve its native transform path.
            Check(device->SetVertexShader(nullptr));
            Check(device->SetPixelShader(nullptr));
            Check(device->SetTransform(D3DTS_WORLD,
                                       reinterpret_cast<const D3DMATRIX*>(p.shader.world.data())));
            Check(device->SetTransform(D3DTS_VIEW,
                                       reinterpret_cast<const D3DMATRIX*>(p.shader.view.data())));
            Check(device->SetTransform(
                D3DTS_PROJECTION, reinterpret_cast<const D3DMATRIX*>(p.shader.projection.data())));
            Check(device->SetTexture(0, nullptr));
            Check(device->SetTextureStageState(0, D3DTSS_COLOROP, D3DTOP_SELECTARG1));
            Check(device->SetTextureStageState(0, D3DTSS_COLORARG1, D3DTA_DIFFUSE));
            Check(device->SetTextureStageState(0, D3DTSS_ALPHAOP, D3DTOP_SELECTARG1));
            Check(device->SetTextureStageState(0, D3DTSS_ALPHAARG1, D3DTA_DIFFUSE));
            Check(device->SetTextureStageState(1, D3DTSS_COLOROP, D3DTOP_DISABLE));
        } else {
            Check(effect->SetTechnique(technique));
            Check(effect->SetBool("spadd", additive));
            Check(device->SetTexture(
                0, s.mode == BuiltinShadowMode::Material
                       ? (s.receive ? targets.at(draw.shadowTarget).texture.Get() : white.Get())
                       : nullptr));
            Check(device->SetTexture(1, base ? base : sphere));
            Check(device->SetTexture(2, base ? sphere : nullptr));
            for (unsigned stage = 1; stage <= 2; ++stage) {
                // These are the native model material sampler states.
                Check(device->SetSamplerState(stage, D3DSAMP_MINFILTER, D3DTEXF_ANISOTROPIC));
                Check(device->SetSamplerState(stage, D3DSAMP_MAGFILTER, D3DTEXF_ANISOTROPIC));
                Check(device->SetSamplerState(stage, D3DSAMP_MIPFILTER, D3DTEXF_ANISOTROPIC));
                Check(device->SetSamplerState(stage, D3DSAMP_MAXANISOTROPY, 16));
                Check(device->SetSamplerState(stage, D3DSAMP_ADDRESSU, D3DTADDRESS_WRAP));
                Check(device->SetSamplerState(stage, D3DSAMP_ADDRESSV, D3DTADDRESS_WRAP));
            }
            UINT passes = 0;
            Check(effect->Begin(&passes, 0));
            Check(effect->BeginPass(0));
        }
        const auto arity = ModelPrimitiveArity(p.topology);
        const auto topology =
            p.topology == PrimitiveTopology::TriangleList ? D3DPT_TRIANGLELIST : D3DPT_LINELIST;
        if (draw.indexed)
            Check(device->DrawIndexedPrimitive(topology, 0, 0, draw.vertexCount, draw.firstIndex,
                                               draw.indexCount / arity));
        else
            Check(device->DrawPrimitive(topology, draw.firstVertex, draw.drawVertexCount / arity));
        if (s.mode != BuiltinShadowMode::None) {
            Check(effect->EndPass());
            Check(effect->End());
        }
    }

public:
    explicit Reference(const std::filesystem::path& effectPath) {
        api.Attach(Direct3DCreate9(D3D_SDK_VERSION));
        if (!api || !window.handle)
            throw std::runtime_error("DX9 message-window device unavailable");
        D3DPRESENT_PARAMETERS p{};
        p.Windowed = TRUE;
        p.SwapEffect = D3DSWAPEFFECT_DISCARD;
        p.BackBufferWidth = p.BackBufferHeight = 1;
        p.BackBufferFormat = D3DFMT_A8R8G8B8;
        p.hDeviceWindow = window.handle;
        Check(api->CreateDevice(0, D3DDEVTYPE_HAL, window.handle,
                                D3DCREATE_HARDWARE_VERTEXPROCESSING | D3DCREATE_FPU_PRESERVE, &p,
                                &device));
        ComPtr<ID3DXBuffer> errors;
        const auto hr = D3DXCreateEffectFromFileW(device.Get(), effectPath.c_str(), nullptr,
                                                  nullptr, 0, nullptr, &effect, &errors);
        if (FAILED(hr) && errors)
            throw std::runtime_error(static_cast<const char*>(errors->GetBufferPointer()));
        Check(hr);
        ModelTextureData image;
        image.levels = {{1, 1, {255, 255, 255, 255}}};
        Texture(image, white);
    }
    void Render(const RenderFrame& frame) {
        Check(device->SetDepthStencilSurface(nullptr));
        targets.clear();
        resources.clear();
        for (const auto& source : frame.targets) {
            Target t;
            t.description = source.description;
            const auto& d = t.description;
            if (d.format == TextureFormat::Depth24Stencil8)
                Check(device->CreateDepthStencilSurface(d.width, d.height, D3DFMT_D24S8,
                                                        D3DMULTISAMPLE_NONE, 0, FALSE, &t.surface,
                                                        nullptr));
            else {
                Check(device->CreateTexture(d.width, d.height, 1, D3DUSAGE_RENDERTARGET,
                                            d.format == TextureFormat::R32Float ? D3DFMT_R32F
                                                                                : D3DFMT_A8R8G8B8,
                                            D3DPOOL_DEFAULT, &t.texture, nullptr));
                Check(t.texture->GetSurfaceLevel(0, &t.surface));
            }
            targets.emplace(source.id, std::move(t));
        }
        for (const auto& source : frame.resources) {
            Resource r;
            void* bytes = nullptr;
            if (source.kind == ReplayResourceKind::Texture)
                Texture(source.texture, r.texture);
            else if (source.kind == ReplayResourceKind::Vertices) {
                Check(device->CreateVertexBuffer(UINT(source.bytes.size()), 0, 0, D3DPOOL_MANAGED,
                                                 &r.vertices, nullptr));
                Check(r.vertices->Lock(0, 0, &bytes, 0));
                std::memcpy(bytes, source.bytes.data(), source.bytes.size());
                Check(r.vertices->Unlock());
            } else {
                Check(device->CreateIndexBuffer(
                    UINT(source.bytes.size()), 0,
                    source.indexFormat == IndexFormat::UInt16 ? D3DFMT_INDEX16 : D3DFMT_INDEX32,
                    D3DPOOL_MANAGED, &r.indices, nullptr));
                Check(r.indices->Lock(0, 0, &bytes, 0));
                std::memcpy(bytes, source.bytes.data(), source.bytes.size());
                Check(r.indices->Unlock());
            }
            resources.emplace(source.key, std::move(r));
        }
        for (const auto& pass : frame.passes) {
            for (unsigned s = 0; s < 3; ++s)
                Check(device->SetTexture(s, nullptr));
            Check(device->SetDepthStencilSurface(nullptr));
            Check(device->SetRenderTarget(0, targets.at(pass.color->target).surface.Get()));
            if (pass.depthStencil)
                Check(device->SetDepthStencilSurface(
                    targets.at(pass.depthStencil->target).surface.Get()));
            const auto c = pass.color->clear;
            Check(device->Clear(0, nullptr,
                                D3DCLEAR_TARGET |
                                    (pass.depthStencil ? D3DCLEAR_ZBUFFER | D3DCLEAR_STENCIL : 0),
                                D3DCOLOR_COLORVALUE(c.red, c.green, c.blue, c.alpha), 1, 0));
            Check(device->BeginScene());
            for (const auto& draw : pass.draws)
                Draw(draw);
            Check(device->EndScene());
        }
    }
    AttachmentReadback Read(std::uint64_t id) {
        const auto& t = targets.at(id);
        D3DSURFACE_DESC desc{};
        Check(t.surface->GetDesc(&desc));
        ComPtr<IDirect3DSurface9> staging;
        Check(device->CreateOffscreenPlainSurface(desc.Width, desc.Height, desc.Format,
                                                  D3DPOOL_SYSTEMMEM, &staging, nullptr));
        Check(device->GetRenderTargetData(t.surface.Get(), staging.Get()));
        D3DLOCKED_RECT lock{};
        Check(staging->LockRect(&lock, nullptr, D3DLOCK_READONLY));
        AttachmentReadback out;
        out.description = t.description;
        out.bytes.resize(std::size_t(desc.Width) * desc.Height * 4);
        for (unsigned y = 0; y < desc.Height; ++y) {
            auto* dest = out.bytes.data() + std::size_t(y) * desc.Width * 4;
            std::memcpy(dest, static_cast<const char*>(lock.pBits) + y * lock.Pitch,
                        desc.Width * 4);
            if (desc.Format == D3DFMT_A8R8G8B8)
                for (unsigned x = 0; x < desc.Width; ++x)
                    std::swap(dest[x * 4], dest[x * 4 + 2]);
        }
        Check(staging->UnlockRect());
        return out;
    }
};
}
