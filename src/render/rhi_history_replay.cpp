#include "mikudancestudio/rhi_history_replay.hpp"
#ifdef MIKUDANCESTUDIO_DIAG
#define NOMINMAX
#include <Windows.h>
#include <d3d9.h>
#include "mme/mesh_subset.hpp"
#include "mikudancestudio/rhi_d3d9_replay_conversion.hpp"
#include "mikudancestudio/rhi_d3d9_mip_capture.hpp"
#include "mikudancestudio/rhi_d3d11.hpp"
#include "mikudancestudio/render_scene_passes.hpp"
#include "mikudancestudio/model_cpu_preview.hpp"
#include <wrl/client.h>
#include <array>
#include <vector>
#include <string>
#include <fstream>
#include <cstring>
#include <memory>
#include <cstdlib>
using Microsoft::WRL::ComPtr;
namespace mikudancestudio {
namespace {
using StateValue = rhi::D3D9ReplayState;
// Only documented identifiers: some drivers return S_OK plus poison values
// for holes in the numeric render-state enum.
const D3DRENDERSTATETYPE checkedStates[] = {D3DRS_ZENABLE,
                                            D3DRS_FILLMODE,
                                            D3DRS_SHADEMODE,
                                            D3DRS_ZWRITEENABLE,
                                            D3DRS_ALPHATESTENABLE,
                                            D3DRS_LASTPIXEL,
                                            D3DRS_SRCBLEND,
                                            D3DRS_DESTBLEND,
                                            D3DRS_CULLMODE,
                                            D3DRS_ZFUNC,
                                            D3DRS_ALPHAREF,
                                            D3DRS_ALPHAFUNC,
                                            D3DRS_DITHERENABLE,
                                            D3DRS_ALPHABLENDENABLE,
                                            D3DRS_FOGENABLE,
                                            D3DRS_SPECULARENABLE,
                                            D3DRS_STENCILENABLE,
                                            D3DRS_STENCILFAIL,
                                            D3DRS_STENCILZFAIL,
                                            D3DRS_STENCILPASS,
                                            D3DRS_STENCILFUNC,
                                            D3DRS_STENCILREF,
                                            D3DRS_STENCILMASK,
                                            D3DRS_STENCILWRITEMASK,
                                            D3DRS_TEXTUREFACTOR,
                                            D3DRS_LIGHTING,
                                            D3DRS_AMBIENT,
                                            D3DRS_COLORVERTEX,
                                            D3DRS_LOCALVIEWER,
                                            D3DRS_NORMALIZENORMALS,
                                            D3DRS_DIFFUSEMATERIALSOURCE,
                                            D3DRS_SPECULARMATERIALSOURCE,
                                            D3DRS_AMBIENTMATERIALSOURCE,
                                            D3DRS_EMISSIVEMATERIALSOURCE,
                                            D3DRS_VERTEXBLEND,
                                            D3DRS_CLIPPLANEENABLE,
                                            D3DRS_MULTISAMPLEANTIALIAS,
                                            D3DRS_MULTISAMPLEMASK,
                                            D3DRS_COLORWRITEENABLE,
                                            D3DRS_BLENDOP,
                                            D3DRS_SCISSORTESTENABLE,
                                            D3DRS_SLOPESCALEDEPTHBIAS,
                                            D3DRS_DEPTHBIAS,
                                            D3DRS_SRGBWRITEENABLE,
                                            D3DRS_TWOSIDEDSTENCILMODE,
                                            D3DRS_CCW_STENCILFAIL,
                                            D3DRS_CCW_STENCILZFAIL,
                                            D3DRS_CCW_STENCILPASS,
                                            D3DRS_CCW_STENCILFUNC,
                                            D3DRS_SEPARATEALPHABLENDENABLE,
                                            D3DRS_SRCBLENDALPHA,
                                            D3DRS_DESTBLENDALPHA,
                                            D3DRS_BLENDOPALPHA};
const D3DTEXTURESTAGESTATETYPE checkedStages[] = {
    D3DTSS_COLOROP,   D3DTSS_COLORARG1, D3DTSS_COLORARG2,     D3DTSS_ALPHAOP,
    D3DTSS_ALPHAARG1, D3DTSS_ALPHAARG2, D3DTSS_TEXCOORDINDEX, D3DTSS_TEXTURETRANSFORMFLAGS,
    D3DTSS_COLORARG0, D3DTSS_ALPHAARG0, D3DTSS_RESULTARG,     D3DTSS_CONSTANT};
struct Draw : rhi::D3D9ReplaySnapshot {
    ComPtr<IDirect3DStateBlock9> state;
    ComPtr<IDirect3DVertexBuffer9> vb;
    ComPtr<IDirect3DIndexBuffer9> ib;
    ComPtr<IDirect3DVertexBuffer9> sourceVb;
    ComPtr<IDirect3DIndexBuffer9> sourceIb;
    std::array<ComPtr<IDirect3DTexture9>, 8> textures;
    std::vector<unsigned char> nativeBefore;
    std::string pass;
};
struct Replay {
    std::uint64_t capture = 0;
    bool recording = false, done = false, pending = false, nativeExact = false;
    std::string directory, blocked;
    ComPtr<IDirect3DSurface9> source, depth, target, replayDepth;
    D3DSURFACE_DESC desc{}, ds{};
    D3DVIEWPORT9 viewport{};
    unsigned flags = 0, color = 0, stencil = 0, material = 0;
    float z = 1;
    std::vector<std::unique_ptr<Draw>> draws;
    std::vector<unsigned char> before, after;
    std::ofstream log;
    rhi::ModelReplayStream portable;
    std::string portableBlocked;
    std::string pass = "unspecified";
    bool portableEnabled = false;
    std::uint64_t portableFailureSequence = 0;
    unsigned portableFailureDraw = 0;
    std::optional<render::SceneDrawReplacement> cpuPending;
    std::vector<render::SceneDrawReplacement> cpuDraws;
} g;
std::uint64_t nextLiveCapture = 0;
bool Requested(const char* name) {
    char b[2]{};
    return GetEnvironmentVariableA(name, b, 2) == 1 && b[0] == '1';
}
unsigned long long Hash(const std::vector<unsigned char>& bytes) {
    unsigned long long h = 14695981039346656037ull;
    for (auto b : bytes) {
        h ^= b;
        h *= 1099511628211ull;
    }
    return h;
}
bool Read(IDirect3DDevice9*, IDirect3DSurface9*, std::vector<unsigned char>&);
bool Snapshot(IDirect3DDevice9* device, Draw& c) {
    ComPtr<IDirect3DSurface9> rt, ds, extra;
    if (FAILED(device->GetRenderTarget(0, &rt)) || FAILED(device->GetDepthStencilSurface(&ds)) ||
        rt.Get() != g.source.Get() || ds.Get() != g.depth.Get())
        return false;
    if (SUCCEEDED(device->GetRenderTarget(1, &extra)) && extra)
        return false;
    ComPtr<IDirect3DVertexShader9> vs;
    ComPtr<IDirect3DPixelShader9> ps;
    if (FAILED(device->GetVertexShader(&vs)) || FAILED(device->GetPixelShader(&ps)) || vs || ps)
        return false;
    if (FAILED(device->GetFVF(&c.fvf)) || !c.fvf || FAILED(device->GetViewport(&c.viewport)) ||
        FAILED(device->CreateStateBlock(D3DSBT_ALL, &c.state)))
        return false;
    if (FAILED(device->GetTransform(D3DTS_WORLD, &c.world)) ||
        FAILED(device->GetTransform(D3DTS_VIEW, &c.view)) ||
        FAILED(device->GetTransform(D3DTS_PROJECTION, &c.projection)) ||
        FAILED(device->GetMaterial(&c.material)))
        return false;
    D3DCAPS9 caps{};
    if (FAILED(device->GetDeviceCaps(&caps)))
        return false;
    for (DWORD i = 0; i < caps.MaxActiveLights; ++i) {
        D3DLIGHT9 light{};
        BOOL enabled = FALSE;
        if (SUCCEEDED(device->GetLight(i, &light))) {
            if (FAILED(device->GetLightEnable(i, &enabled)))
                return false;
            c.lights.push_back({i, light, enabled});
        }
    }
    for (auto key : checkedStates) {
        DWORD value = 0;
        if (FAILED(device->GetRenderState(key, &value)))
            return false;
        c.renderStates.push_back({0, static_cast<unsigned>(key), value});
    }
    if (FAILED(device->GetScissorRect(&c.scissor)))
        return false;
    for (unsigned stage = 0; stage < 8; ++stage) {
        if (FAILED(device->GetTransform(static_cast<D3DTRANSFORMSTATETYPE>(D3DTS_TEXTURE0 + stage),
                                        &c.textureMatrices[stage])))
            return false;
        for (auto key : checkedStages) {
            DWORD value = 0;
            if (FAILED(device->GetTextureStageState(stage, key, &value)))
                return false;
            c.stageStates.push_back({stage, static_cast<unsigned>(key), value});
        }
        for (unsigned key = 1; key <= 13; ++key) {
            DWORD value = 0;
            if (FAILED(
                    device->GetSamplerState(stage, static_cast<D3DSAMPLERSTATETYPE>(key), &value)))
                return false;
            c.samplerStates.push_back({stage, key, value});
        }
    }
    ComPtr<IDirect3DVertexBuffer9> vb;
    ComPtr<IDirect3DIndexBuffer9> ib;
    D3DVERTEXBUFFER_DESC vd{};
    D3DINDEXBUFFER_DESC id{};
    void* bytes = nullptr;
    if (FAILED(device->GetStreamSource(0, &vb, &c.offset, &c.stride)) || !vb ||
        FAILED(vb->GetDesc(&vd)))
        return false;
    for (unsigned stream = 1; stream < 16; ++stream) {
        ComPtr<IDirect3DVertexBuffer9> other;
        UINT o = 0, s = 0;
        if (FAILED(device->GetStreamSource(stream, &other, &o, &s)) || other)
            return false;
    }
    UINT frequency = 0;
    if (FAILED(device->GetStreamSourceFreq(0, &frequency)) || frequency != 1)
        return false;
    if (FAILED(vb->Lock(0, 0, &bytes, 0)))
        return false;
    c.vertexBytes.assign(static_cast<unsigned char*>(bytes),
                         static_cast<unsigned char*>(bytes) + vd.Size);
    c.sourceVb = vb;
    g.log << "BUFFER sequence=" << g.draws.size() + 1 << " kind=vertex usage=" << vd.Usage
          << " pool=" << vd.Pool << " fvf=" << vd.FVF << " size=" << vd.Size << "\n";
    if (FAILED(vb->Unlock()) ||
        FAILED(device->CreateVertexBuffer(vd.Size, vd.Usage, vd.FVF, vd.Pool, &c.vb, nullptr)) ||
        FAILED(c.vb->Lock(0, 0, &bytes, 0)))
        return false;
    std::memcpy(bytes, c.vertexBytes.data(), vd.Size);
    if (FAILED(c.vb->Unlock()))
        return false;
    if (c.indexed) {
        if (FAILED(device->GetIndices(&ib)) || !ib || FAILED(ib->GetDesc(&id)) ||
            FAILED(ib->Lock(0, 0, &bytes, 0)))
            return false;
        c.indexFormat = id.Format;
        c.indexBytes.assign(static_cast<unsigned char*>(bytes),
                            static_cast<unsigned char*>(bytes) + id.Size);
        c.sourceIb = ib;
        g.log << "BUFFER sequence=" << g.draws.size() + 1 << " kind=index usage=" << id.Usage
              << " pool=" << id.Pool << " format=" << id.Format << " size=" << id.Size << "\n";
        if (FAILED(ib->Unlock()) ||
            FAILED(
                device->CreateIndexBuffer(id.Size, id.Usage, id.Format, id.Pool, &c.ib, nullptr)) ||
            FAILED(c.ib->Lock(0, 0, &bytes, 0)))
            return false;
        std::memcpy(bytes, c.indexBytes.data(), id.Size);
        if (FAILED(c.ib->Unlock()))
            return false;
    }
    for (unsigned stage = 0; stage < 8; ++stage) {
        ComPtr<IDirect3DBaseTexture9> base;
        ComPtr<IDirect3DTexture9> source;
        if (FAILED(device->GetTexture(stage, &base)))
            return false;
        if (!base)
            continue;
        if (base->GetType() != D3DRTYPE_TEXTURE || FAILED(base.As(&source)))
            return false;
        D3DSURFACE_DESC d{};
        if (FAILED(source->GetLevelDesc(0, &d)) || (d.Usage & D3DUSAGE_RENDERTARGET))
            return false;
        unsigned bpp = 0, block = 0;
        switch (d.Format) {
        case D3DFMT_A8R8G8B8:
        case D3DFMT_X8R8G8B8:
            bpp = 4;
            break;
        case D3DFMT_R5G6B5:
        case D3DFMT_A1R5G5B5:
        case D3DFMT_A4R4G4B4:
            bpp = 2;
            break;
        case D3DFMT_A8:
        case D3DFMT_L8:
            bpp = 1;
            break;
        case D3DFMT_DXT1:
            block = 8;
            break;
        case D3DFMT_DXT3:
        case D3DFMT_DXT5:
            block = 16;
            break;
        default:
            return false;
        }
        const bool autoMips = (d.Usage & D3DUSAGE_AUTOGENMIPMAP) != 0;
        c.textureInfo[stage] = {true,
                                d.Format,
                                d.Width,
                                d.Height,
                                source->GetLevelCount(),
                                source->GetLOD(),
                                autoMips,
                                static_cast<DWORD>(source->GetAutoGenFilterType())};
        g.log << "TEXTURE sequence=" << g.draws.size() + 1 << " stage=" << stage
              << " width=" << d.Width << " height=" << d.Height << " format=" << d.Format
              << " levels=" << source->GetLevelCount() << " autoMips=" << autoMips
              << " mipFilter=" << source->GetAutoGenFilterType() << " lod=" << source->GetLOD()
              << "\n";
        if (FAILED(device->CreateTexture(d.Width, d.Height, autoMips ? 0 : source->GetLevelCount(),
                                         autoMips ? D3DUSAGE_AUTOGENMIPMAP : 0, d.Format,
                                         D3DPOOL_MANAGED, &c.textures[stage], nullptr)))
            return false;
        c.textures[stage]->SetLOD(source->GetLOD());
        if (autoMips &&
            FAILED(c.textures[stage]->SetAutoGenFilterType(source->GetAutoGenFilterType())))
            return false;
        for (unsigned level = 0; level < source->GetLevelCount(); ++level) {
            D3DLOCKED_RECT a{}, b{};
            if (FAILED(source->GetLevelDesc(level, &d)) ||
                FAILED(source->LockRect(level, &a, nullptr, D3DLOCK_READONLY)))
                return false;
            if (FAILED(c.textures[stage]->LockRect(level, &b, nullptr, 0))) {
                source->UnlockRect(level);
                return false;
            }
            const unsigned rows = block ? (d.Height + 3) / 4 : d.Height,
                           rowBytes = block ? ((d.Width + 3) / 4) * block : d.Width * bpp;
            for (unsigned y = 0; y < rows; ++y) {
                const auto* p = static_cast<const unsigned char*>(a.pBits) + y * a.Pitch;
                std::memcpy(static_cast<unsigned char*>(b.pBits) + y * b.Pitch, p, rowBytes);
                c.textureBytes[stage].insert(c.textureBytes[stage].end(), p, p + rowBytes);
            }
            const HRESULT x = source->UnlockRect(level), y = c.textures[stage]->UnlockRect(level);
            if (FAILED(x) || FAILED(y))
                return false;
        }
        if (autoMips)
            c.textures[stage]->GenerateMipSubLevels();
        if (autoMips && g.portableEnabled && Requested("MDS_RHI_PORTABLE_MIP_CAPTURE")) {
            const auto status =
                rhi::CaptureD3D9GeneratedMips(device, source.Get(), true, c.preparedMips[stage]);
            g.log << "MIP_CAPTURE draw=" << g.draws.size() + 1 << " stage=" << stage
                  << " status=" << (SUCCEEDED(status) ? "CAPTURED" : "BLOCKED")
                  << " hresult=" << static_cast<unsigned long>(status)
                  << " levels=" << c.preparedMips[stage].levels.size() << "\n";
            for (unsigned level = 0; level < c.preparedMips[stage].levels.size(); ++level) {
                const auto& mip = c.preparedMips[stage].levels[level];
                g.log << "MIP_LEVEL draw=" << g.draws.size() + 1 << " stage=" << stage
                      << " level=" << level << " width=" << mip.width << " height=" << mip.height
                      << " hash=" << Hash(mip.pixels) << "\n";
            }
            // A failed preparation never relaxes portable admission. The
            // original native clone/replay path remains separately checkable.
        }
    }
    return true;
}
bool OriginalGeometryUnchanged(const Draw& c) {
    void* p = nullptr;
    if (FAILED(c.sourceVb->Lock(0, static_cast<UINT>(c.vertexBytes.size()), &p, 0)))
        return false;
    bool same = std::memcmp(p, c.vertexBytes.data(), c.vertexBytes.size()) == 0;
    same = SUCCEEDED(c.sourceVb->Unlock()) && same;
    if (c.sourceIb) {
        if (FAILED(c.sourceIb->Lock(0, static_cast<UINT>(c.indexBytes.size()), &p, 0)))
            return false;
        same = std::memcmp(p, c.indexBytes.data(), c.indexBytes.size()) == 0 && same;
        same = SUCCEEDED(c.sourceIb->Unlock()) && same;
    }
    return same;
}
bool Execute(IDirect3DDevice9* d, const Draw& c, bool copyGeometry = true, bool copyTextures = true,
             unsigned disable = 0) {
    if (FAILED(c.state->Apply()) || FAILED(d->SetRenderTarget(0, g.target.Get())) ||
        FAILED(d->SetDepthStencilSurface(g.replayDepth.Get())) ||
        FAILED(d->SetViewport(&c.viewport)))
        return false;
    D3DMATRIX w{}, v{}, p{};
    D3DMATERIAL9 material{};
    if (FAILED(d->GetTransform(D3DTS_WORLD, &w)) || FAILED(d->GetTransform(D3DTS_VIEW, &v)) ||
        FAILED(d->GetTransform(D3DTS_PROJECTION, &p)) || FAILED(d->GetMaterial(&material)))
        return false;
    if (std::memcmp(&w, &c.world, sizeof(w)) || std::memcmp(&v, &c.view, sizeof(v)) ||
        std::memcmp(&p, &c.projection, sizeof(p)) ||
        std::memcmp(&material, &c.material, sizeof(material))) {
        g.log << "STATE_RESTORE status=FAIL kind=matrix_or_material\n";
        return false;
    }
    for (const auto& entry : c.lights) {
        D3DLIGHT9 light{};
        BOOL enabled = FALSE;
        if (FAILED(d->GetLight(entry.index, &light)) ||
            FAILED(d->GetLightEnable(entry.index, &enabled)) ||
            std::memcmp(&light, &entry.value, sizeof(light)) || enabled != entry.enabled) {
            g.log << "STATE_RESTORE status=FAIL kind=light index=" << entry.index << "\n";
            return false;
        }
    }
    for (const auto& entry : c.renderStates) {
        DWORD value = 0;
        if (FAILED(d->GetRenderState(static_cast<D3DRENDERSTATETYPE>(entry.key), &value)) ||
            value != entry.value) {
            g.log << "STATE_RESTORE status=FAIL kind=render key=" << entry.key
                  << " expected=" << entry.value << " actual=" << value << "\n";
            return false;
        }
    }
    for (const auto& entry : c.stageStates) {
        DWORD value = 0;
        if (FAILED(d->GetTextureStageState(
                entry.stage, static_cast<D3DTEXTURESTAGESTATETYPE>(entry.key), &value)) ||
            value != entry.value) {
            g.log << "STATE_RESTORE status=FAIL kind=texture_stage stage=" << entry.stage
                  << " key=" << entry.key << "\n";
            return false;
        }
    }
    for (const auto& entry : c.samplerStates) {
        DWORD value = 0;
        if (FAILED(d->GetSamplerState(entry.stage, static_cast<D3DSAMPLERSTATETYPE>(entry.key),
                                      &value)) ||
            value != entry.value) {
            g.log << "STATE_RESTORE status=FAIL kind=sampler stage=" << entry.stage
                  << " key=" << entry.key << "\n";
            return false;
        }
    }
    RECT scissor{};
    if (FAILED(d->GetScissorRect(&scissor)) || std::memcmp(&scissor, &c.scissor, sizeof(scissor))) {
        g.log << "STATE_RESTORE status=FAIL kind=scissor\n";
        return false;
    }
    for (unsigned stage = 0; stage < 8; ++stage) {
        D3DMATRIX matrix{};
        if (FAILED(d->GetTransform(static_cast<D3DTRANSFORMSTATETYPE>(D3DTS_TEXTURE0 + stage),
                                   &matrix)) ||
            std::memcmp(&matrix, &c.textureMatrices[stage], sizeof(matrix))) {
            g.log << "STATE_RESTORE status=FAIL kind=texture_matrix stage=" << stage << "\n";
            return false;
        }
    }
    if (copyGeometry && (FAILED(d->SetStreamSource(0, c.vb.Get(), c.offset, c.stride)) ||
                         FAILED(d->SetIndices(c.ib.Get()))))
        return false;
    if (copyTextures)
        for (unsigned i = 0; i < 8; ++i)
            if (FAILED(d->SetTexture(i, c.textures[i].Get())))
                return false;
    if (disable == 1 && FAILED(d->SetRenderState(D3DRS_LIGHTING, FALSE)))
        return false;
    if (disable == 2 && FAILED(d->SetRenderState(D3DRS_ZENABLE, FALSE)))
        return false;
    if (disable == 3 && FAILED(d->SetRenderState(D3DRS_STENCILENABLE, FALSE)))
        return false;
    if (disable == 5 && FAILED(d->SetRenderState(D3DRS_NORMALIZENORMALS, FALSE)))
        return false;
    if (disable == 6 && FAILED(d->SetRenderState(D3DRS_SPECULARENABLE, FALSE)))
        return false;
    if (disable == 4 && FAILED(d->SetRenderState(D3DRS_ALPHABLENDENABLE, FALSE)))
        return false;
    return SUCCEEDED(
        c.indexed ? d->DrawIndexedPrimitive(static_cast<D3DPRIMITIVETYPE>(c.type), c.base,
                                            c.minimum, c.vertices, c.start, c.count)
                  : d->DrawPrimitive(static_cast<D3DPRIMITIVETYPE>(c.type), c.start, c.count));
}
bool Read(IDirect3DDevice9* d, IDirect3DSurface9* surface, std::vector<unsigned char>& result) {
    // D3D9 forbids StretchRect inside BeginScene/EndScene. Even drivers that
    // return S_OK need an explicit submission boundary before MSAA resolves.
    if (FAILED(d->EndScene()))
        return false;
    struct Resume {
        IDirect3DDevice9* d;
        bool done = false;
        bool Apply() {
            done = true;
            return SUCCEEDED(d->BeginScene());
        }
        ~Resume() {
            if (!done)
                d->BeginScene();
        }
    } resume{d};
    ComPtr<IDirect3DSurface9> resolved, system;
    if (FAILED(d->CreateRenderTarget(g.desc.Width, g.desc.Height, g.desc.Format,
                                     D3DMULTISAMPLE_NONE, 0, FALSE, &resolved, nullptr)) ||
        FAILED(d->StretchRect(surface, nullptr, resolved.Get(), nullptr, D3DTEXF_NONE)) ||
        FAILED(d->CreateOffscreenPlainSurface(g.desc.Width, g.desc.Height, g.desc.Format,
                                              D3DPOOL_SYSTEMMEM, &system, nullptr)) ||
        FAILED(d->GetRenderTargetData(resolved.Get(), system.Get())))
        return false;
    D3DLOCKED_RECT lock{};
    if (FAILED(system->LockRect(&lock, nullptr, D3DLOCK_READONLY)))
        return false;
    result.resize(static_cast<size_t>(g.viewport.Width) * g.viewport.Height * 4);
    for (unsigned y = 0; y < g.viewport.Height; ++y)
        std::memcpy(result.data() + static_cast<size_t>(y) * g.viewport.Width * 4,
                    static_cast<const unsigned char*>(lock.pBits) +
                        (y + g.viewport.Y) * lock.Pitch + g.viewport.X * 4,
                    g.viewport.Width * 4);
    const bool unlocked = SUCCEEDED(system->UnlockRect());
    return resume.Apply() && unlocked;
}
size_t Difference(const std::vector<unsigned char>& a, const std::vector<unsigned char>& b) {
    if (a.size() != b.size())
        return static_cast<size_t>(-1);
    size_t n = 0;
    for (size_t i = 0; i < a.size(); i += 4)
        if (std::memcmp(a.data() + i, b.data() + i, 4))
            ++n;
    return n;
}
struct Restore {
    IDirect3DDevice9* device;
    ComPtr<IDirect3DStateBlock9> state;
    D3DVIEWPORT9 viewport{};
    bool valid = false, restored = false;
    explicit Restore(IDirect3DDevice9* d) : device(d) {
        valid = SUCCEEDED(d->GetViewport(&viewport)) &&
                SUCCEEDED(d->CreateStateBlock(D3DSBT_ALL, &state));
    }
    bool Apply() {
        bool ok = SUCCEEDED(device->SetRenderTarget(0, g.source.Get()));
        ok = SUCCEEDED(device->SetDepthStencilSurface(g.depth.Get())) && ok;
        ok = SUCCEEDED(state->Apply()) && ok;
        ok = SUCCEEDED(device->SetViewport(&viewport)) && ok;
        restored = true;
        return ok;
    }
    ~Restore() {
        if (valid && !restored)
            Apply();
    }
};
void Save(const char* name, const std::vector<unsigned char>& bytes) {
    std::ofstream file(g.directory + "/" + name, std::ios::binary);
    file.write(reinterpret_cast<const char*>(bytes.data()), bytes.size());
}
void ExportReductionBatch(unsigned index, const std::string& stem = "history_replay.reduction");
// Diagnostic only: native rendering stays on DX9. Admission and image parity
// have separate gates; a valid stream never implies equal scene images.
void ComparePortableScene(bool nativeExact) {
    if (!Requested("MDS_RHI_PORTABLE_DX11_COMPARE"))
        return;
    auto blocked = [](const char* reason) {
        g.log << "DX11_GATE status=BLOCKED reason=" << reason << "\n";
        g.log.flush();
    };
    if (!nativeExact) {
        blocked("native_prefix_not_exact");
        return;
    }
    if (!g.portableEnabled || !g.portableBlocked.empty() || !rhi::ValidateModelReplay(g.portable)) {
        blocked("portable_stream_not_valid");
        return;
    }
    if (!Requested("MDS_RHI_HISTORY_TRACE")) {
        blocked("native_draw_boundaries_not_captured");
        return;
    }
#ifdef MIKUDANCESTUDIO_RHI_D3D11
    struct Window {
        HWND handle;
        ~Window() {
            if (handle)
                DestroyWindow(handle);
        }
    } window{CreateWindowExW(0, L"STATIC", L"Portable scene image diagnostic", WS_POPUP, 0, 0, 64,
                             64, nullptr, nullptr, GetModuleHandleW(nullptr), nullptr)};
    if (!window.handle) {
        blocked("diagnostic_window_failed");
        return;
    }
    rhi::SwapchainDesc desc;
    desc.window = {rhi::WindowSystem::Win32, reinterpret_cast<std::uintptr_t>(window.handle)};
    desc.width = desc.height = 64;
    const auto driver = Requested("MDS_RHI_PORTABLE_DX11_WARP") ? rhi::D3D11Driver::Warp
                                                                : rhi::D3D11Driver::Hardware;
    auto bundle = rhi::CreateD3D11Device(desc, driver);
    if (!bundle) {
        g.log << "DX11_CREATION hresult=" << bundle.creationError << "\n";
        blocked("device_creation_failed");
        return;
    }
    g.log << "DX11_DEVICE driver=" << (driver == rhi::D3D11Driver::Warp ? "warp" : "hardware")
          << " adapter=" << bundle.adapterName << "\n";
    std::size_t differentBoundaries = 0, firstDraw = 0, comparisons = 0;
    bool exact = true;
    for (std::size_t boundary = 0; boundary <= g.draws.size(); ++boundary) {
        const auto& actual =
            boundary == g.draws.size() ? g.before : g.draws[boundary]->nativeBefore;
        if (actual.size() != static_cast<std::size_t>(g.viewport.Width) * g.viewport.Height * 4) {
            blocked("missing_native_image");
            return;
        }
        rhi::RgbaImage image;
        const auto replay =
            rhi::ReplayD3D11ModelPrefix(*bundle.device, g.portable, boundary, image);
        if (!replay) {
            g.log << "DX11_REPLAY afterDraw=" << boundary << " sequence=" << replay.sequence
                  << " reason=" << replay.reason << "\n";
            blocked("replay_failed");
            return;
        }
        if (image.width != g.desc.Width || image.height != g.desc.Height) {
            blocked("readback_size_mismatch");
            return;
        }
        std::vector<unsigned char> cropped(actual.size());
        std::size_t differences = 0, firstPixel = 0;
        unsigned maxChannel = 0;
        for (unsigned y = 0; y < g.viewport.Height; ++y)
            for (unsigned x = 0; x < g.viewport.Width; ++x) {
                const auto pixel = static_cast<std::size_t>(y) * g.viewport.Width + x;
                const auto* rgba =
                    image.pixels.data() +
                    (static_cast<std::size_t>(y + g.viewport.Y) * image.width + x + g.viewport.X) *
                        4;
                auto* bgra = cropped.data() + pixel * 4;
                bgra[0] = rgba[2];
                bgra[1] = rgba[1];
                bgra[2] = rgba[0];
                bgra[3] = rgba[3];
                bool changed = false;
                for (unsigned channel = 0; channel < 4; ++channel) {
                    const unsigned delta = static_cast<unsigned>(
                        std::abs(static_cast<int>(bgra[channel]) - actual[pixel * 4 + channel]));
                    if (delta > maxChannel)
                        maxChannel = delta;
                    changed = changed || delta != 0;
                }
                if (changed) {
                    if (!differences)
                        firstPixel = pixel;
                    ++differences;
                }
            }
        g.log << "DX11_TRACE afterDraw=" << boundary
              << " pass=" << (boundary ? g.draws[boundary - 1]->pass : "clear")
              << " differentPixels=" << differences << " maxChannelDelta=" << maxChannel;
        if (differences) {
            g.log << " firstX=" << g.viewport.X + firstPixel % g.viewport.Width
                  << " firstY=" << g.viewport.Y + firstPixel / g.viewport.Width;
            const auto at = firstPixel * 4;
            g.log << " nativeBGRA=";
            for (unsigned channel = 0; channel < 4; ++channel)
                g.log << (channel ? "," : "") << unsigned(actual[at + channel]);
            g.log << " replayBGRA=";
            for (unsigned channel = 0; channel < 4; ++channel)
                g.log << (channel ? "," : "") << unsigned(cropped[at + channel]);
            if (!differentBoundaries) {
                firstDraw = boundary;
                Save("history_replay.dx11_first_actual.bgra", actual);
                Save("history_replay.dx11_first_rebuilt.bgra", cropped);
                // Freeze every input through the first divergent draw, with
                // the actual submission-boundary images. Never substitute a
                // reconstructed clear/history for this native evidence.
                for (unsigned draw = 0; draw < boundary; ++draw) {
                    const auto stem = "history_replay.dx11_prefix.draw" + std::to_string(draw + 1);
                    ExportReductionBatch(draw, stem);
                    Save((stem + ".before.bgra").c_str(), g.draws[draw]->nativeBefore);
                }
            }
            ++differentBoundaries;
            exact = false;
        }
        g.log << "\n";
        g.log.flush();
        ++comparisons;
    }
    const bool retired = bundle.swapchain->Retire();
    g.log << "DX11_GATE status=" << (exact && retired ? "PASS" : "FAIL")
          << " comparisons=" << comparisons << " draws=" << g.draws.size()
          << " differentBoundaries=" << differentBoundaries << " firstAfterDraw=" << firstDraw
          << " region=" << g.viewport.X << "," << g.viewport.Y << "," << g.viewport.Width << ","
          << g.viewport.Height << " outside=UNSPECIFIED\n";
    g.log.flush();
#else
    blocked("dx11_backend_not_built");
#endif
}
}
bool CopyPortableHistoryReplay(rhi::ModelReplayStream& output) {
    output = {};
    if (!g.nativeExact || !g.portableEnabled || !g.done || !g.portableBlocked.empty() ||
        !g.blocked.empty() || !rhi::ValidateModelReplay(g.portable))
        return false;
    output = g.portable;
    return true;
}
bool CpuSceneFamilyRequested(render::SceneDrawFamily family) {
    if (CpuScenePreviewRequested())
        return true;
    char value[16]{};
    if (!GetEnvironmentVariableA("MDS_RHI_CPU_PREFIX", value, 16))
        return false;
    const auto mask = std::strtoul(value, nullptr, 10);
    const unsigned bit = family == render::SceneDrawFamily::GroundLines ||
                                 family == render::SceneDrawFamily::GroundPlane
                             ? 1
                         : family == render::SceneDrawFamily::ProjectedModelShadow ? 2
                         : family == render::SceneDrawFamily::ModelMaterial        ? 4
                         : family == render::SceneDrawFamily::AccessoryMaterial    ? 8
                         : family == render::SceneDrawFamily::ModelOutline         ? 16
                                                                                   : 0;
    return (mask & bit) != 0;
}
bool CpuSceneHistoryRecording() {
    return CpuSceneFrameRecording() || (g.recording && !g.done && !g.pending && g.blocked.empty());
}
void StageCpuSceneHistoryDraw(render::SceneDrawReplacement draw) {
    if (CpuSceneFrameRecording())
        AppendCpuSceneFrame(draw);
    if (!g.recording || g.done || g.pending || !g.blocked.empty())
        return;
    if (g.cpuPending) {
        BlockHistoryReplay("unconsumed_cpu_scene_draw");
        return;
    }
    draw.boundary = g.draws.size() + 1;
    g.cpuPending = std::move(draw);
}
bool CopyCpuSceneHistoryReplay(rhi::ModelReplayStream& output) {
    rhi::ModelReplayStream baseline;
    if (!CopyPortableHistoryReplay(baseline))
        return false;
    if (g.cpuDraws.empty()) {
        g.log << "CPU_PREFIX status=BLOCKED reason=no_cpu_draws\n";
        return false;
    }
    for (std::size_t n = 0; n < g.draws.size(); ++n) {
        const auto& native = *g.draws[n];
        const bool expected =
            (native.pass == "ground" &&
             CpuSceneFamilyRequested(render::SceneDrawFamily::GroundLines)) ||
            (native.pass == "projected_shadow" && native.fvf == 66 &&
             CpuSceneFamilyRequested(render::SceneDrawFamily::ProjectedModelShadow)) ||
            (native.pass == "model_material" &&
             CpuSceneFamilyRequested(render::SceneDrawFamily::ModelMaterial)) ||
            ((native.pass == "accessory" || native.pass == "accessory_tail") &&
             CpuSceneFamilyRequested(render::SceneDrawFamily::AccessoryMaterial)) ||
            (native.pass == "model_outline" &&
             CpuSceneFamilyRequested(render::SceneDrawFamily::ModelOutline));
        if (expected && std::none_of(g.cpuDraws.begin(), g.cpuDraws.end(),
                                     [&](const auto& cpu) { return cpu.boundary == n + 1; })) {
            g.log << "CPU_PREFIX status=BLOCKED reason=missing_family_draw boundary=" << n + 1
                  << " pass=" << native.pass << "\n";
            return false;
        }
    }
    baseline.source.frame = g.cpuDraws.front().cpu.source.frame;
    rhi::ModelReplayStream audited;
    auto result = render::ReplaceSceneDraws(baseline, g.cpuDraws, audited);
    // A native baseline proves parity but never supplies the rendered stream.
    // Partial-prefix diagnostics retain their previous explicit composition.
    if (Requested("MDS_RHI_CPU_FULL_SCENE")) {
        if (result && (!CopyAssembledCpuSceneFrame(output) ||
                       rhi::ValidateModelReplay(output).draws != result.draws))
            result = {false, 0, 0, "independent_cpu_frame_unavailable"};
    } else if (result)
        output = std::move(audited);
    const auto name =
        g.directory + "/cpu_prefix." + std::to_string(GetCurrentProcessId()) +
        (Requested("MDS_RHI_CPU_LIVE_HISTORY") ? "." + std::to_string(g.capture) : "") + ".csv";
    std::ofstream report(name);
    report << "frame,boundary,family,model,material,status,reason,geometrySource,stateSource\n";
    for (const auto& draw : g.cpuDraws)
        report << draw.cpu.source.frame << "," << draw.boundary << ","
               << render::SceneDrawFamilyName(draw.family) << "," << draw.model << ","
               << draw.material << "," << (result ? "PASS" : "BLOCKED") << ","
               << (result ? "exact_fields_and_bytes" : result.reason) << ",cpu,cpu\n";
    g.log << "CPU_PREFIX status=" << (result ? "PASS" : "BLOCKED")
          << " replacements=" << g.cpuDraws.size() << " boundary=" << result.sequence
          << " reason=" << result.reason << "\n";
    g.log.flush();
    return bool(result) && bool(report);
}
static bool HistoryRgba(const std::vector<unsigned char>& bytes, rhi::RgbaImage& image) {
    image = {};
    if (!g.nativeExact || bytes.size() != std::uint64_t(g.viewport.Width) * g.viewport.Height * 4)
        return false;
    image = {g.viewport.Width, g.viewport.Height, bytes};
    for (std::size_t i = 0; i < image.pixels.size(); i += 4)
        std::swap(image.pixels[i], image.pixels[i + 2]);
    return true;
}
bool CopyVerifiedHistoryImage(rhi::RgbaImage& image) {
    return HistoryRgba(g.before, image);
}
bool CopyVerifiedHistoryBoundaries(std::vector<rhi::RgbaImage>& images) {
    images.clear();
    if (!g.nativeExact)
        return false;
    for (const auto& command : g.draws) {
        rhi::RgbaImage image;
        if (!HistoryRgba(command->nativeBefore, image)) {
            images.clear();
            return false;
        }
        images.push_back(std::move(image));
    }
    rhi::RgbaImage image;
    if (!HistoryRgba(g.before, image)) {
        images.clear();
        return false;
    }
    images.push_back(std::move(image));
    return true;
}
bool ReadVerifiedHistoryImage(IDirect3DDevice9* device, rhi::RgbaImage& image) {
    image = {};
    if (!g.nativeExact)
        return false;
    ComPtr<IDirect3DSurface9> rt, ds;
    D3DVIEWPORT9 v{};
    if (FAILED(device->GetRenderTarget(0, &rt)) || FAILED(device->GetDepthStencilSurface(&ds)) ||
        rt.Get() != g.source.Get() || ds.Get() != g.depth.Get() ||
        FAILED(device->GetViewport(&v)) || v.X != g.viewport.X || v.Y != g.viewport.Y ||
        v.Width != g.viewport.Width || v.Height != g.viewport.Height || v.MinZ != g.viewport.MinZ ||
        v.MaxZ != g.viewport.MaxZ)
        return false;
    std::vector<unsigned char> bytes;
    return Read(device, g.source.Get(), bytes) && HistoryRgba(bytes, image);
}
void ResetHistoryReplay() {
    const bool done = g.done;
    g = Replay{};
    g.done = done;
}
void RestartHistoryReplay() {
    g = Replay{};
}
void SuspendHistoryReplay() {
    g.recording = false;
    g.done = true;
    g.pending = false;
}
void SetHistoryReplayPass(const char* name) {
    g.pass = name ? name : "unspecified";
}
void BlockHistoryReplay(const char* reason) {
    BlockCpuSceneFrame(reason);
    if (g.recording && g.blocked.empty()) {
        g.blocked = reason;
        g.log << "BLOCK reason=" << reason << " boundary=" << g.draws.size() + 1 << "\n";
        g.log.flush();
        if (g.portableEnabled && g.portableBlocked.empty()) {
            g.portableBlocked = reason;
            g.portableFailureDraw = static_cast<unsigned>(g.draws.size() + 1);
            rhi::ModelReplayEvent event;
            event.sequence = g.portable.events.size() + 1;
            event.op = rhi::ModelReplayOp::Unsupported;
            event.label = reason;
            g.portableFailureSequence = event.sequence;
            g.portable.events.push_back(std::move(event));
        }
    }
}
void BeginHistoryReplay(IDirect3DDevice9* d, unsigned flags, unsigned color, float z,
                        unsigned stencil) {
    if (g.done || !Requested("MDS_RHI_HISTORY_REPLAY"))
        return;
    g = Replay{};
    g.capture = ++nextLiveCapture;
    char directory[MAX_PATH]{};
    const auto n =
        GetEnvironmentVariableA(Requested("MDS_RHI_CPU_SCENE_HISTORY") ? "MDS_RHI_CPU_GEOMETRY_DIR"
                                                                       : "MDS_RHI_LIVE_COMPARE_DIR",
                                directory, MAX_PATH);
    if (!n || n >= MAX_PATH)
        return;
    g.directory = directory;
    const auto logName =
        Requested("MDS_RHI_CPU_SCENE_HISTORY")
            ? "history_replay." + std::to_string(GetCurrentProcessId()) +
                  (Requested("MDS_RHI_CPU_LIVE_HISTORY") ? "." + std::to_string(g.capture) : "") +
                  ".dx9"
            : "history_replay.dx9";
    g.log.open(g.directory + "/" + logName, std::ios::trunc);
    g.flags = flags;
    g.color = color;
    g.z = z;
    g.stencil = stencil;
    g.recording = true;
    if (!Requested("MDS_RHI_MODEL_NO_MME")) {
        BlockHistoryReplay("MME_not_disabled");
        return;
    }
    if (FAILED(d->GetRenderTarget(0, &g.source)) || FAILED(d->GetDepthStencilSurface(&g.depth)) ||
        FAILED(g.source->GetDesc(&g.desc)) || FAILED(g.depth->GetDesc(&g.ds)) ||
        FAILED(d->GetViewport(&g.viewport))) {
        BlockHistoryReplay("boundary_capture");
        return;
    }
    if ((g.desc.MultiSampleType != D3DMULTISAMPLE_8_SAMPLES &&
         !(Requested("MDS_RHI_REPLAY_SINGLE_SAMPLE_CONTROL") &&
           g.desc.MultiSampleType == D3DMULTISAMPLE_NONE)) ||
        (g.desc.Format != D3DFMT_A8R8G8B8 && g.desc.Format != D3DFMT_X8R8G8B8) ||
        g.ds.MultiSampleType != g.desc.MultiSampleType ||
        g.ds.MultiSampleQuality != g.desc.MultiSampleQuality ||
        (flags & (D3DCLEAR_TARGET | D3DCLEAR_ZBUFFER | D3DCLEAR_STENCIL)) !=
            (D3DCLEAR_TARGET | D3DCLEAR_ZBUFFER | D3DCLEAR_STENCIL))
        BlockHistoryReplay("incomplete_clear_or_unsupported_8x_target");
    g.portableEnabled = Requested("MDS_RHI_PORTABLE_REPLAY");
    if (g.portableEnabled) {
        auto& stream = g.portable;
        stream.target.width = g.desc.Width;
        stream.target.height = g.desc.Height;
        stream.clear = {((color >> 16) & 255) / 255.f, ((color >> 8) & 255) / 255.f,
                        (color & 255) / 255.f, ((color >> 24) & 255) / 255.f};
        stream.events.push_back({1, rhi::ModelReplayOp::Clear});
        if (g.desc.MultiSampleType != D3DMULTISAMPLE_NONE || g.desc.Format != D3DFMT_A8R8G8B8 ||
            g.ds.Format != D3DFMT_D24S8 || z != 1 || stencil != 0)
            g.portableBlocked = "unsupported_clear_or_target";
        const rhi::ModelViewport viewport{g.viewport.X,      g.viewport.Y,    g.viewport.Width,
                                          g.viewport.Height, g.viewport.MinZ, g.viewport.MaxZ};
        if (!rhi::ValidModelViewport(viewport, g.desc.Width, g.desc.Height))
            g.portableBlocked = "invalid_initial_viewport";
        else if (g.viewport.X || g.viewport.Y || g.viewport.Width != g.desc.Width ||
                 g.viewport.Height != g.desc.Height) {
            stream.initializedRegion = viewport;
            g.log << "PORTABLE_REGION x=" << viewport.x << " y=" << viewport.y
                  << " width=" << viewport.width << " height=" << viewport.height
                  << " outside=UNSPECIFIED\n";
        }
        if (!g.portableBlocked.empty())
            g.log << "PORTABLE_BLOCK draw=0 sequence=0 reason=" << g.portableBlocked
                  << " target=" << g.desc.Width << "," << g.desc.Height
                  << " colorFormat=" << g.desc.Format << " depthFormat=" << g.ds.Format
                  << " viewport=" << g.viewport.X << "," << g.viewport.Y << "," << g.viewport.Width
                  << "," << g.viewport.Height << "\n";
    }
    g.log << "BEGIN samples=" << g.desc.MultiSampleType << " quality=" << g.desc.MultiSampleQuality
          << " clearFlags=" << flags << " viewport=" << g.viewport.X << "," << g.viewport.Y << ","
          << g.viewport.Width << "," << g.viewport.Height << "\n";
}
static unsigned selectedType = 0, selectedMinimum = 0, selectedVertices = 0, selectedStart = 0,
                selectedCount = 0;
static int selectedBase = 0;
void RecordHistoryDraw(IDirect3DDevice9* d, unsigned type, bool indexed, int base, unsigned minimum,
                       unsigned vertices, unsigned start, unsigned count) {
    if (g.pending) {
        selectedType = type;
        selectedMinimum = minimum;
        selectedVertices = vertices;
        selectedStart = start;
        selectedCount = count;
        selectedBase = base;
        return;
    }
    ObserveCpuSceneDraw(g.pass.c_str());
    if (!g.recording || !g.blocked.empty())
        return;
    if (g.cpuPending) {
        g.cpuDraws.push_back(std::move(*g.cpuPending));
        g.cpuPending.reset();
    }
    auto c = std::make_unique<Draw>();
    c->accessoryMaterial = g.pass == "accessory" || g.pass == "accessory_tail";
    c->pass = g.pass;
    c->type = type;
    c->indexed = indexed;
    c->base = base;
    c->minimum = minimum;
    c->vertices = vertices;
    c->start = start;
    c->count = count;
    if (!Snapshot(d, *c)) {
        BlockHistoryReplay("unsupported_target_shader_stream_or_resource");
        return;
    }
    if (Requested("MDS_RHI_HISTORY_TRACE") && !Read(d, g.source.Get(), c->nativeBefore)) {
        BlockHistoryReplay("trace_readback_failed");
        return;
    }
    g.log << "DRAW sequence=" << g.draws.size() + 1 << " pass=" << g.pass << " fvf=" << c->fvf
          << " type=" << type << " indexed=" << indexed << " base=" << base
          << " minimum=" << minimum << " vertices=" << vertices << " start=" << start
          << " count=" << count << " vbVersion=" << Hash(c->vertexBytes)
          << " ibVersion=" << Hash(c->indexBytes);
    for (unsigned i = 0; i < 8; ++i)
        if (c->textures[i])
            g.log << " texture" << i << "Version=" << Hash(c->textureBytes[i]);
    g.log << "\n";
    // Numeric DX9 state identifiers are diagnostic transport, not portable
    // RHI enums. The native state block supplies the full replay state.
    g.log << "STATE sequence=" << g.draws.size() + 1
          << " softwareVP=" << d->GetSoftwareVertexProcessing();
    for (const auto& entry : c->renderStates)
        g.log << " rs." << entry.key << "=" << entry.value;
    for (const auto& entry : c->stageStates)
        g.log << " tss." << entry.stage << "." << entry.key << "=" << entry.value;
    for (const auto& entry : c->samplerStates)
        g.log << " sampler." << entry.stage << "." << entry.key << "=" << entry.value;
    g.log << "\n";
    for (auto transform : {D3DTS_WORLD, D3DTS_VIEW, D3DTS_PROJECTION, D3DTS_TEXTURE0,
                           D3DTS_TEXTURE1, D3DTS_TEXTURE2}) {
        D3DMATRIX matrix{};
        if (FAILED(d->GetTransform(transform, &matrix))) {
            BlockHistoryReplay("transform_read_failed");
            return;
        }
        g.log << "MATRIX sequence=" << g.draws.size() + 1 << " transform=" << transform;
        const auto* values = reinterpret_cast<const float*>(&matrix);
        g.log.precision(9);
        for (unsigned i = 0; i < 16; ++i)
            g.log << " m" << i << "=" << values[i];
        g.log << "\n";
    }
    if (g.portableEnabled && g.portableBlocked.empty()) {
        const auto converted = rhi::AppendD3D9ReplaySnapshot(
            g.portable, *c, Requested("MDS_RHI_PORTABLE_SHADOW_CAPTURE"));
        if (!converted) {
            g.portableBlocked = converted.reason;
            g.portableFailureDraw = static_cast<unsigned>(g.draws.size() + 1);
            rhi::ModelReplayEvent event;
            event.sequence = g.portable.events.size() + 1;
            event.op = rhi::ModelReplayOp::Unsupported;
            event.label = converted.reason;
            g.portableFailureSequence = event.sequence;
            g.portable.events.push_back(std::move(event));
        }
        g.log << "PORTABLE draw=" << g.draws.size() + 1 << " pass=" << g.pass
              << " sequence=" << converted.sequence
              << " status=" << (converted ? "CONVERTED" : "BLOCKED")
              << " reason=" << converted.reason << "\n";
    } else if (g.portableEnabled) {
        // Audit later draw types without admitting them to a broken prefix.
        rhi::ModelReplayStream isolated;
        isolated.target = g.portable.target;
        isolated.clear = g.portable.clear;
        isolated.initializedRegion = g.portable.initializedRegion;
        isolated.events.push_back({1, rhi::ModelReplayOp::Clear});
        const auto audit = rhi::AppendD3D9ReplaySnapshot(
            isolated, *c, Requested("MDS_RHI_PORTABLE_SHADOW_CAPTURE"));
        g.log << "PORTABLE_AUDIT draw=" << g.draws.size() + 1 << " pass=" << g.pass
              << " isolated=1 status=" << (audit ? "CONVERTIBLE" : "BLOCKED")
              << " reason=" << audit.reason << "\n";
    }
    g.draws.push_back(std::move(c));
}
void RecordHistoryMesh(IDirect3DDevice9* d, ID3DXMesh* mesh, unsigned attribute) {
    const bool emptyCpuSubset = ConsumeCpuSceneEmptyMesh();
    if (!g.recording || !g.blocked.empty()) {
        if (!emptyCpuSubset)
            ObserveCpuSceneDraw(g.pass.c_str());
        return;
    }
    mme::MeshSubsetPlan plan;
    ComPtr<IDirect3DVertexBuffer9> vb;
    ComPtr<IDirect3DIndexBuffer9> ib;
    ComPtr<IDirect3DStateBlock9> state;
    bool ok = SUCCEEDED(mme::PlanMeshSubset(*mesh, attribute, plan)) &&
              SUCCEEDED(mesh->GetVertexBuffer(&vb)) && SUCCEEDED(mesh->GetIndexBuffer(&ib)) &&
              SUCCEEDED(d->CreateStateBlock(D3DSBT_ALL, &state));
    if (ok && (mesh->GetOptions() & 1)) {
        D3DCAPS9 caps{};
        D3DVERTEXBUFFER_DESC vd{};
        D3DINDEXBUFFER_DESC id{};
        ok = SUCCEEDED(d->GetDeviceCaps(&caps)) && SUCCEEDED(vb->GetDesc(&vd)) &&
             SUCCEEDED(ib->GetDesc(&id)) && mesh->GetNumBytesPerVertex();
        if (ok)
            ok = caps.MaxVertexIndex > 0xffffu &&
                 caps.MaxVertexIndex >= vd.Size / mesh->GetNumBytesPerVertex() &&
                 caps.MaxPrimitiveCount >= id.Size / (3 * sizeof(DWORD));
    }
    ok = ok && SUCCEEDED(d->SetFVF(mesh->GetFVF())) &&
         SUCCEEDED(d->SetStreamSource(0, vb.Get(), 0, mesh->GetNumBytesPerVertex())) &&
         SUCCEEDED(d->SetIndices(ib.Get()));
    if (ok)
        for (const auto& range : plan.ranges)
            RecordHistoryDraw(d, D3DPT_TRIANGLELIST, true, 0, range.VertexStart, range.VertexCount,
                              range.FaceStart * 3, range.FaceCount);
    if (state)
        ok = SUCCEEDED(state->Apply()) && ok;
    if (!ok)
        BlockHistoryReplay("unsupported_mesh_subset_or_UP_fallback");
}
namespace {
void ExportReductionBatch(unsigned index, const std::string& stem) {
    const auto& c = *g.draws[index];
    Save((stem + ".vertices").c_str(), c.vertexBytes);
    Save((stem + ".indices").c_str(), c.indexBytes);
    std::ofstream file(g.directory + "/" + stem + ".state", std::ios::trunc);
    file << "DRAW sequence=" << index + 1 << " type=" << c.type << " fvf=" << c.fvf
         << " indexed=" << c.indexed << " base=" << c.base << " minimum=" << c.minimum
         << " vertices=" << c.vertices << " start=" << c.start << " count=" << c.count
         << " offset=" << c.offset << " stride=" << c.stride << "\n";
    file << "VIEWPORT x=" << c.viewport.X << " y=" << c.viewport.Y << " width=" << c.viewport.Width
         << " height=" << c.viewport.Height << " minZ=" << c.viewport.MinZ
         << " maxZ=" << c.viewport.MaxZ << "\n";
    file << "TARGET width=" << g.desc.Width << " height=" << g.desc.Height
         << " colorFormat=" << g.desc.Format << " depthFormat=" << g.ds.Format
         << " samples=" << g.desc.MultiSampleType << " quality=" << g.desc.MultiSampleQuality
         << "\n";
    file << "CLEAR flags=" << g.flags << " color=" << g.color << " depth=" << g.z
         << " stencil=" << g.stencil << "\n";
    file << "SCISSOR left=" << c.scissor.left << " top=" << c.scissor.top
         << " right=" << c.scissor.right << " bottom=" << c.scissor.bottom << "\n";
    file.precision(9);
    const auto floats = [&](const char* name, const void* bytes, unsigned count) {
        file << name;
        const auto* values = static_cast<const float*>(bytes);
        for (unsigned i = 0; i < count; ++i)
            file << " f" << i << "=" << values[i];
        file << "\n";
    };
    floats("WORLD", &c.world, 16);
    floats("VIEW", &c.view, 16);
    floats("PROJECTION", &c.projection, 16);
    floats("MATERIAL", &c.material, 17);
    for (const auto& light : c.lights) {
        file << "LIGHT index=" << light.index << " enabled=" << light.enabled
             << " type=" << light.value.Type << "\n";
        floats("LIGHT_VALUES", &light.value.Diffuse, 25);
    }
    for (const auto& value : c.renderStates)
        file << "RS key=" << value.key << " value=" << value.value << "\n";
    for (const auto& value : c.stageStates)
        file << "TSS stage=" << value.stage << " key=" << value.key << " value=" << value.value
             << "\n";
    for (const auto& value : c.samplerStates)
        file << "SAMPLER stage=" << value.stage << " key=" << value.key << " value=" << value.value
             << "\n";
    for (unsigned stage = 0; stage < 8; ++stage) {
        const auto& t = c.textureInfo[stage];
        file << "TEXTURE stage=" << stage << " present=" << t.present << " format=" << t.format
             << " width=" << t.width << " height=" << t.height << " levels=" << t.levels
             << " lod=" << t.lod << " autogen=" << t.autogen << " filter=" << t.autogenFilter
             << "\n";
        floats("TEXTURE_MATRIX", &c.textureMatrices[stage], 16);
        if (c.textures[stage]) {
            const auto name = stem + ".texture" + std::to_string(stage);
            Save(name.c_str(), c.textureBytes[stage]);
        }
        for (unsigned level = 0; level < c.preparedMips[stage].levels.size(); ++level) {
            const auto& mip = c.preparedMips[stage].levels[level];
            const auto suffix =
                ".prepared" + std::to_string(stage) + "." + std::to_string(level) + ".rgba";
            file << "PREPARED_MIP stage=" << stage << " level=" << level << " width=" << mip.width
                 << " height=" << mip.height << " hash=" << Hash(mip.pixels) << "\n";
            Save((stem + suffix).c_str(), mip.pixels);
        }
    }
    // Native diagnostic values plus original bytes, not a portable RHI stream.
}
void ReduceHistory(IDirect3DDevice9* d, unsigned suspect) {
    ExportReductionBatch(suspect);
    for (unsigned i = 0; i <= suspect; ++i)
        ExportReductionBatch(i, "history_replay.reduction.draw" + std::to_string(i + 1));
    std::ofstream report(g.directory + "/history_replay.reduction", std::ios::trunc);
    report << "BEGIN suspectSequence=" << suspect + 1 << " samples=" << g.desc.MultiSampleType
           << " repeats=10 actualPrefixAcceptance=0\n";
    const auto last = suspect;
    for (unsigned start = 0; start <= last; ++start)
        for (unsigned disable = 0; disable <= 7; ++disable) {
            if (disable && start != last)
                continue;
            ComPtr<IDirect3DVertexBuffer9> originalVertices;
            if (disable == 7) {
                auto& batch = *g.draws[last];
                auto bytes = batch.vertexBytes;
                unsigned repaired = 0;
                if (batch.fvf != (D3DFVF_XYZ | D3DFVF_NORMAL | D3DFVF_TEX1) || batch.stride != 32) {
                    report << "UNSUPPORTED zero_normal_control\n";
                    continue;
                }
                for (size_t at = 0; at + 32 <= bytes.size(); at += 32) {
                    float n[3];
                    std::memcpy(n, bytes.data() + at + 12, 12);
                    if (n[0] == 0 && n[1] == 0 && n[2] == 0) {
                        n[1] = 1;
                        std::memcpy(bytes.data() + at + 12, n, 12);
                        ++repaired;
                    }
                }
                D3DVERTEXBUFFER_DESC desc{};
                ComPtr<IDirect3DVertexBuffer9> replacement;
                void* bits = nullptr;
                if (FAILED(batch.vb->GetDesc(&desc)) ||
                    FAILED(d->CreateVertexBuffer(desc.Size, desc.Usage, desc.FVF, desc.Pool,
                                                 &replacement, nullptr)) ||
                    FAILED(replacement->Lock(0, 0, &bits, 0))) {
                    report << "FAIL zero_normal_control_setup\n";
                    continue;
                }
                std::memcpy(bits, bytes.data(), bytes.size());
                if (FAILED(replacement->Unlock()))
                    continue;
                originalVertices = batch.vb;
                batch.vb = replacement;
                report << "CONTROL zeroNormalsReplaced=" << repaired << " acceptanceEligible=0\n";
            }
            std::vector<unsigned char> baseline;
            bool ok = true;
            size_t maximum = 0;
            for (unsigned repeat = 0; repeat < 10; ++repeat) {
                Restore restore(d);
                ok = restore.valid && SUCCEEDED(d->SetRenderTarget(0, g.target.Get())) &&
                     SUCCEEDED(d->SetDepthStencilSurface(g.replayDepth.Get())) &&
                     SUCCEEDED(d->SetViewport(&g.viewport)) &&
                     SUCCEEDED(d->Clear(0, nullptr, g.flags, g.color, g.z, g.stencil));
                for (unsigned i = start; i <= last && ok; ++i) {
                    if (Requested("MDS_RHI_HISTORY_TRACE")) {
                        std::vector<unsigned char> boundary;
                        ok = Read(d, g.target.Get(), boundary);
                    }
                    if (ok)
                        ok = Execute(d, *g.draws[i], true, true, disable);
                }
                std::vector<unsigned char> pixels;
                ok = ok && Read(d, g.target.Get(), pixels);
                if (restore.valid)
                    ok = restore.Apply() && ok;
                if (!repeat)
                    baseline = pixels;
                const auto diff = ok ? Difference(baseline, pixels) : static_cast<size_t>(-1);
                if (diff > maximum)
                    maximum = diff;
                report << "REDUCE startSequence=" << start + 1 << " endSequence=" << last + 1
                       << " disable=" << disable << " repeat=" << repeat
                       << " differentPixels=" << diff << " operationSuccess=" << ok << "\n";
                if (start == last && repeat == 0) {
                    const auto name =
                        "history_replay.reduction.single" + std::to_string(disable) + ".bgra";
                    Save(name.c_str(), pixels);
                }
                if (!ok)
                    break;
            }
            if (originalVertices)
                g.draws[last]->vb = originalVertices;
            report << "RESULT startSequence=" << start + 1 << " endSequence=" << last + 1
                   << " disable=" << disable
                   << " status=" << (ok && maximum == 0 ? "STABLE" : "DIFFERENT")
                   << " maximumDifferentPixels=" << maximum << "\n";
        }
    report << "END sceneGate=NOT_PASSED\n";
}
}
void CompareHistoryReplay(IDirect3DDevice9* d, unsigned slot, unsigned material, unsigned indices) {
    if (!g.recording)
        return;
    g.recording = false;
    g.done = true;
    g.material = material;
    if (Requested("MDS_RHI_SHADOW_BATCH_EXPORT"))
        for (unsigned draw = 0; draw < g.draws.size(); ++draw)
            if (g.draws[draw]->fvf == (D3DFVF_XYZ | D3DFVF_DIFFUSE)) {
                ExportReductionBatch(draw, "history_replay.shadow_batch");
                g.log << "SHADOW_EXPORT draw=" << draw + 1
                      << " path=history_replay.shadow_batch.state\n";
                break;
            }
    if (g.portableEnabled) {
        g.portable.complete = g.blocked.empty() && g.portableBlocked.empty();
        auto validation = rhi::ValidateModelReplay(g.portable);
        g.log << "PORTABLE_GATE status=" << (validation ? "VALIDATED" : "BLOCKED")
              << " draws=" << validation.draws << " blockedDraw=" << g.portableFailureDraw
              << " sequence=" << (validation ? validation.sequence : g.portableFailureSequence)
              << " reason="
              << (!g.portableBlocked.empty() ? g.portableBlocked
                  : !g.blocked.empty()       ? g.blocked
                                             : validation.reason)
              << "\n";
        g.log.flush();
    }
    if (!g.blocked.empty()) {
        g.log << "GATE status=BLOCKED reason=" << g.blocked << "\n";
        ComparePortableScene(false);
        g.log.flush();
        return;
    }
    Restore restore(d);
    bool ok = restore.valid &&
              SUCCEEDED(d->CreateRenderTarget(g.desc.Width, g.desc.Height, g.desc.Format,
                                              g.desc.MultiSampleType, g.desc.MultiSampleQuality,
                                              FALSE, &g.target, nullptr)) &&
              SUCCEEDED(d->CreateDepthStencilSurface(g.ds.Width, g.ds.Height, g.ds.Format,
                                                     g.ds.MultiSampleType, g.ds.MultiSampleQuality,
                                                     FALSE, &g.replayDepth, nullptr));
    ok = ok && Read(d, g.source.Get(), g.before);
    ok = ok && SUCCEEDED(d->SetRenderTarget(0, g.target.Get())) &&
         SUCCEEDED(d->SetDepthStencilSurface(g.replayDepth.Get())) &&
         SUCCEEDED(d->SetViewport(&g.viewport)) &&
         SUCCEEDED(d->Clear(0, nullptr, g.flags, g.color, g.z, g.stencil));
    unsigned sequence = 0, firstDifference = 0;
    for (const auto& c : g.draws) {
        ++sequence;
        if (ok && !c->nativeBefore.empty()) {
            std::vector<unsigned char> image;
            ok = Read(d, g.target.Get(), image);
            const auto pixels = ok ? Difference(c->nativeBefore, image) : static_cast<size_t>(-1);
            g.log << "TRACE beforeSequence=" << sequence << " differentPixels=" << pixels << "\n";
            if (ok && pixels && !firstDifference) {
                firstDifference = sequence;
                Save("history_replay.first_actual.bgra", c->nativeBefore);
                Save("history_replay.first_rebuilt.bgra", image);
                g.log << "FIRST_DIFFERENCE beforeSequence=" << sequence
                      << " precedingDraw=" << sequence - 1 << " differentPixels=" << pixels << "\n";
            }
        }
        if (ok)
            ok = Execute(d, *c);
    }
    std::vector<unsigned char> replay;
    ok = ok && Read(d, g.target.Get(), replay);
    if (ok && Requested("MDS_RHI_HISTORY_ISOLATE")) {
        std::vector<unsigned char> second;
        ok = Read(d, g.target.Get(), second);
        g.log << "READBACK_REPEAT differentPixels="
              << (ok ? Difference(replay, second) : static_cast<size_t>(-1)) << "\n";
    }
    if (restore.valid)
        ok = restore.Apply() && ok;
    const auto diff = ok ? Difference(g.before, replay) : static_cast<size_t>(-1);
    g.nativeExact = ok && diff == 0 && firstDifference == 0 && Requested("MDS_RHI_HISTORY_TRACE");
    for (const auto& c : g.draws)
        g.nativeExact = g.nativeExact && c->nativeBefore.size() == g.before.size();
    if (ok && diff && !firstDifference && Requested("MDS_RHI_HISTORY_TRACE")) {
        Save("history_replay.first_actual.bgra", g.before);
        Save("history_replay.first_rebuilt.bgra", replay);
        g.log << "FIRST_DIFFERENCE beforeSequence=" << g.draws.size() + 1
              << " precedingDraw=" << g.draws.size() << " differentPixels=" << diff << "\n";
    }
    g.log << "PREFIX status=" << (ok && diff == 0 ? "PASS" : "FAIL") << " slot=" << slot
          << " material=" << material << " indices=" << indices << " draws=" << g.draws.size()
          << " beforeDifferentPixels=" << diff
          << " depthStencilVerification=selected_draw_pending\n";
    Save("history_replay.before.bgra", g.before);
    Save("history_replay.rebuilt.bgra", replay);
    g.log.flush();
    if (Requested("MDS_RHI_PORTABLE_PREFIX_EXPORT")) {
        // CPU history captures both initial load and Save As reopen. Keep
        // archives disjoint, including the index-format recorder expected by
        // standalone reduction readers. Never overwrite one process's inputs.
        const auto parent = g.directory;
        if (Requested("MDS_RHI_CPU_SCENE_HISTORY")) {
            const auto captureSuffix =
                Requested("MDS_RHI_CPU_LIVE_HISTORY") ? "." + std::to_string(g.capture) : "";
            g.directory =
                parent + "/prefix." + std::to_string(GetCurrentProcessId()) + captureSuffix;
            CreateDirectoryA(g.directory.c_str(), nullptr);
            g.log.flush();
            const auto source = parent + "/history_replay." +
                                std::to_string(GetCurrentProcessId()) + captureSuffix + ".dx9";
            CopyFileA(source.c_str(), (g.directory + "/history_replay.dx9").c_str(), FALSE);
        }
        const bool trace = Requested("MDS_RHI_HISTORY_TRACE");
        const auto validation = rhi::ValidateModelReplay(g.portable);
        bool exported = trace && ok && diff == 0 && firstDifference == 0;
        for (unsigned draw = 0; draw < g.draws.size(); ++draw) {
            const auto stem = "history_replay.scene_prefix.draw" + std::to_string(draw + 1);
            ExportReductionBatch(draw, stem);
            Save((stem + ".before.bgra").c_str(), g.draws[draw]->nativeBefore);
            exported = exported && g.draws[draw]->nativeBefore.size() == g.before.size();
        }
        Save("history_replay.scene_prefix.final.bgra", g.before);
        std::ofstream manifest(g.directory + "/history_replay.scene_prefix.manifest",
                               std::ios::trunc);
        manifest << "SCENE_PREFIX schema=1 status="
                 << (exported && validation ? "READY" : "BLOCKED") << " nativeExact=" << exported
                 << " draws=" << g.draws.size() << " events=" << g.portable.events.size()
                 << " blockedDraw=" << g.portableFailureDraw
                 << " blockedSequence=" << g.portableFailureSequence << " reason="
                 << (!validation
                         ? (g.portableBlocked.empty() ? validation.reason : g.portableBlocked)
                     : !exported ? "native_boundary_not_exact"
                                 : "none")
                 << "\n";
        for (unsigned draw = 0; draw < g.draws.size(); ++draw)
            manifest << "PASS draw=" << draw + 1 << " name=" << g.draws[draw]->pass << "\n";
        manifest.flush();
        g.log << "SCENE_PREFIX_EXPORT status="
              << (exported && validation && manifest ? "READY" : "BLOCKED")
              << " draws=" << g.draws.size() << " nativeExact=" << exported << "\n";
        g.log.flush();
        g.directory = parent;
    }
    // Live CPU diagnostics compare both measured backends in the application
    // after CPU replacement. The older default-math portable control remains
    // available for one-shot captures, without duplicating every live prefix.
    if (!Requested("MDS_RHI_CPU_LIVE_HISTORY"))
        ComparePortableScene(ok && diff == 0 && firstDifference == 0);
    if (ok && Requested("MDS_RHI_HISTORY_REDUCE")) {
        // beforeSequence N attributes its first divergence to command N-1.
        // No divergence chooses the last prefix command as a control only.
        const unsigned suspect =
            firstDifference >= 2 ? firstDifference - 2 : static_cast<unsigned>(g.draws.size() - 1);
        ReduceHistory(d, suspect);
        g.pending = false;
        g.log.flush();
        return;
    }
    if (ok && Requested("MDS_RHI_HISTORY_ISOLATE")) {
        // These borrowed-resource controls diagnose copies; they cannot open
        // the frozen-resource gate and never replace the baseline images.
        unsigned changedGeometry = 0;
        for (const auto& command : g.draws)
            if (!OriginalGeometryUnchanged(*command))
                ++changedGeometry;
        g.log << "CONTROL_VERSIONS changedGeometryDraws=" << changedGeometry
              << " textureVersionsVerified=0\n";
        std::vector<unsigned char> previousFrozen;
        bool repeatStable = true;
        unsigned repeatCount = 0;
        for (unsigned mode = 0; mode < 6; ++mode) {
            Restore controlRestore(d);
            bool good = controlRestore.valid && SUCCEEDED(d->SetRenderTarget(0, g.target.Get())) &&
                        SUCCEEDED(d->SetDepthStencilSurface(g.replayDepth.Get())) &&
                        SUCCEEDED(d->SetViewport(&g.viewport)) &&
                        SUCCEEDED(d->Clear(0, nullptr, g.flags, g.color, g.z, g.stencil));
            unsigned controlSequence = 0, controlFirst = 0;
            for (const auto& command : g.draws) {
                ++controlSequence;
                if (good && !command->nativeBefore.empty()) {
                    std::vector<unsigned char> boundary;
                    good = Read(d, g.target.Get(), boundary);
                    if (good && !controlFirst && Difference(command->nativeBefore, boundary))
                        controlFirst = controlSequence;
                }
                if (good)
                    good = Execute(d, *command, mode == 0 || mode >= 3, mode == 1 || mode >= 3);
            }
            g.log << "CONTROL_TRACE mode=" << mode << " firstBeforeSequence=" << controlFirst
                  << "\n";
            std::vector<unsigned char> image;
            good = good && Read(d, g.target.Get(), image);
            if (mode >= 3) {
                const auto baseline = good ? Difference(replay, image) : static_cast<size_t>(-1);
                const auto previous =
                    good && !previousFrozen.empty() ? Difference(previousFrozen, image) : baseline;
                g.log << "REPEAT mode=" << mode << " baselineDifferentPixels=" << baseline
                      << " previousDifferentPixels=" << previous << " originalDifferentPixels="
                      << (good ? Difference(g.before, image) : static_cast<size_t>(-1))
                      << " matchingReadbackBoundaries=1 stateRestoreVerified=" << good << "\n";
                repeatStable = repeatStable && good && baseline == 0 && previous == 0;
                ++repeatCount;
                previousFrozen = image;
            }
            if (controlRestore.valid)
                good = controlRestore.Apply() && good;
            g.log << "CONTROL geometry=" << (mode == 0 || mode >= 3 ? "frozen" : "borrowed")
                  << " textures=" << (mode == 1 || mode >= 3 ? "frozen" : "borrowed")
                  << " differentPixels="
                  << (good ? Difference(g.before, image) : static_cast<size_t>(-1))
                  << " acceptanceEligible=0\n";
        }
        g.log << "STABILITY status=" << (repeatStable && repeatCount == 3 ? "PASS" : "FAIL")
              << " frozenRepeats=" << repeatCount
              << " matchingReadbackBoundaries=1 acceptanceEligible=0\n";
        g.log.flush();
        // Controls leave different history in the replay target. Never issue
        // the selected draw using their result, even if baseline passed.
        g.pending = false;
    } else {
        g.pending = ok && diff == 0 && !Requested("MDS_RHI_REPLAY_SINGLE_SAMPLE_CONTROL");
    }
}
void CompleteHistoryReplay(IDirect3DDevice9* d, unsigned material) {
    if (!g.pending || material != g.material)
        return;
    g.pending = false;
    // The selected actual draw has completed. Replay it from its unchanged
    // effective native state and immutable geometry, using the rebuilt DS.
    Restore restore(d);
    Draw c;
    c.type = D3DPT_TRIANGLELIST;
    c.indexed = true;
    // Filled by the selected bridge draw below (recording is off).
    c.type = selectedType;
    c.minimum = selectedMinimum;
    c.vertices = selectedVertices;
    c.start = selectedStart;
    c.count = selectedCount;
    c.base = selectedBase;
    bool ok = restore.valid && selectedCount && Snapshot(d, c) &&
              Read(d, g.source.Get(), g.after) && Execute(d, c);
    std::vector<unsigned char> replay;
    ok = ok && Read(d, g.target.Get(), replay);
    if (restore.valid)
        ok = restore.Apply() && ok;
    const auto diff = ok ? Difference(g.after, replay) : static_cast<size_t>(-1);
    size_t changed = ok ? Difference(g.before, g.after) : 0;
    g.log << "GATE status=" << (ok && diff == 0 && changed ? "PASS" : "FAIL")
          << " afterDifferentPixels=" << diff << " changedPixels=" << changed
          << " depthStencilVerification=selected_draw_functional perSampleReadback=0\n";
    g.log.flush();
    Save("history_replay.actual_after.bgra", g.after);
    Save("history_replay.replayed_after.bgra", replay);
    g.draws.clear();
    g.source.Reset();
    g.depth.Reset();
    g.target.Reset();
    g.replayDepth.Reset();
}
}
#endif
