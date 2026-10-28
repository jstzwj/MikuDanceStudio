#include "mikudancestudio/model_batch_bridge.hpp"
#include "mikudancestudio/model_cpu_preview.hpp"
#include "mikudancestudio/render_model_inputs.hpp"
#ifdef MIKUDANCESTUDIO_DIAG
#define NOMINMAX
#include <Windows.h>
#include <d3d9.h>
#include "mikudancestudio/mmd_app.hpp"
#include "mikudancestudio/model.hpp"
#include "mikudancestudio/mme_bridge.hpp"
#include "mikudancestudio/render_snapshot.hpp"
#include "mikudancestudio/rhi_d3d9.hpp"
#include "mikudancestudio/rhi_real_batch_capture.hpp"
#include "mikudancestudio/rhi_scene_history.hpp"
#include "mikudancestudio/rhi_history_replay.hpp"
#include <fstream>
#include <algorithm>
#include <cstdio>
#include <cstring>
#include <cstdlib>
#include <string>
#include <vector>

namespace mikudancestudio {
namespace {
bool Requested(const char* name) {
    char flag[2]{};
    return GetEnvironmentVariableA(name, flag, 2) == 1 && flag[0] == '1';
}
std::string Directory() {
    char path[MAX_PATH]{};
    const auto length = GetEnvironmentVariableA("MDS_RHI_LIVE_COMPARE_DIR", path, MAX_PATH);
    return length && length < MAX_PATH ? path : "";
}
render::SceneSnapshot g_snapshot;
FILE* g_report = nullptr;
unsigned g_sequence = 0, g_mismatches = 0, g_eligible = 0;
bool g_snapshotSaved = false, g_snapshotPassed = false;
bool g_frameReady = false;
const format::SceneSession* g_session = nullptr;
unsigned g_expected = 0;
std::vector<std::pair<unsigned, unsigned>> g_seen;
bool g_gatePassed = false;
int g_gateSlot = -1;
unsigned g_gateMaterial = 0;
IDirect3DDevice9* g_gateDevice = nullptr;
rhi::MaterialTextureAsset g_toon;
std::unique_ptr<rhi::BorrowedModelTextureCache> g_toonCache;
IDirect3DDevice9* g_toonDevice = nullptr;
int g_toonSlot = -1;
unsigned g_toonMaterial = 0;
std::array<rhi::MaterialTextureAsset, 3> g_materialAssets;
std::array<bool, 3> g_materialOwned{};
bool g_materialReady = false;
rhi::MaterialShaderInputs g_shaderInputs;
bool g_shaderReady = false;
unsigned g_sourceChecks = 0, g_sourceFailures = 0;
unsigned g_sourceBlocked = 0;
FILE* g_sources = nullptr;

template <class T> void Release(T*& object) {
    if (object)
        object->Release();
    object = nullptr;
}
struct Comparison {
    IDirect3DSurface9 *original = nullptr, *baseline = nullptr;
    IDirect3DSurface9 *legacy = nullptr, *adapter = nullptr;
    IDirect3DSurface9 *legacyResolved = nullptr, *adapterResolved = nullptr;
    IDirect3DSurface9* actualResolved = nullptr;
    D3DSURFACE_DESC desc{};
    D3DVIEWPORT9 viewport{};
    bool active = false, applied = false, actualApplied = false, actualAttempted = false;
    HRESULT hr = E_FAIL, oldHr = E_FAIL, newHr = E_FAIL, actualHr = E_FAIL;
    unsigned slot = 0, material = 0, frame = 0, first = 0, count = 0;
    unsigned long bridgeCalls = 0, hostCalls = 0, directCalls = 0;
    std::string directory;
    void Clear() {
        Release(original);
        Release(baseline);
        Release(legacy);
        Release(adapter);
        Release(legacyResolved);
        Release(adapterResolved);
        Release(actualResolved);
        active = false;
    }
    ~Comparison() { Clear(); }
} g_comparison;

DWORD State(IDirect3DDevice9* device, D3DRENDERSTATETYPE key) {
    DWORD value = 0;
    device->GetRenderState(key, &value);
    return value;
}
bool CaptureRaster(IDirect3DDevice9* device, rhi::BatchRasterState& state) {
    constexpr D3DRENDERSTATETYPE keys[] = {
        D3DRS_ZENABLE,  D3DRS_ZWRITEENABLE, D3DRS_ZFUNC,   D3DRS_ALPHABLENDENABLE,
        D3DRS_SRCBLEND, D3DRS_DESTBLEND,    D3DRS_CULLMODE};
    DWORD values[7]{};
    for (unsigned i = 0; i < 7; ++i)
        if (FAILED(device->GetRenderState(keys[i], &values[i])))
            return false;
    if (values[0] > 1 || values[2] < 1 || values[2] > 8 || values[4] < 1 || values[4] > 11 ||
        values[5] < 1 || values[5] > 11 || values[6] < 1 || values[6] > 3)
        return false;
    state.depthTest = values[0] != 0;
    state.depthWrite = values[1] != 0;
    state.depthCompare = static_cast<rhi::DepthComparison>(values[2] - 1);
    state.alphaBlend = values[3] != 0;
    state.sourceBlend = static_cast<rhi::BlendFactor>(values[4] - 1);
    state.destinationBlend = static_cast<rhi::BlendFactor>(values[5] - 1);
    state.cull = static_cast<rhi::BatchCull>(values[6] - 1);
    return true;
}
// The live diagnostic uses the full portable model raster contract, including
// separate alpha and stencil. Unsupported native states remain explicit.
const char* CaptureModelRaster(IDirect3DDevice9* device, rhi::ModelPipeline& pipeline) {
    for (const auto item :
         {std::pair{D3DRS_FOGENABLE, "fog"}, std::pair{D3DRS_CLIPPLANEENABLE, "user_clip_planes"},
          std::pair{D3DRS_DEPTHBIAS, "depth_bias"},
          std::pair{D3DRS_SLOPESCALEDEPTHBIAS, "slope_depth_bias"},
          std::pair{D3DRS_SRGBWRITEENABLE, "srgb_write"},
          std::pair{D3DRS_TWOSIDEDSTENCILMODE, "two_sided_stencil"},
          std::pair{D3DRS_SCISSORTESTENABLE, "scissor"}, std::pair{D3DRS_DITHERENABLE, "dither"},
          std::pair{D3DRS_VERTEXBLEND, "vertex_blend"}}) {
        DWORD value = 0;
        if (FAILED(device->GetRenderState(item.first, &value)))
            return "render_state_read_failed";
        if (value)
            return item.second;
    }
    if (State(device, D3DRS_FILLMODE) != D3DFILL_SOLID)
        return "fill_mode";
    if (!CaptureRaster(device, pipeline.raster))
        return "raster_capture_failed";
    constexpr D3DRENDERSTATETYPE keys[] = {D3DRS_BLENDOP,          D3DRS_SEPARATEALPHABLENDENABLE,
                                           D3DRS_SRCBLENDALPHA,    D3DRS_DESTBLENDALPHA,
                                           D3DRS_BLENDOPALPHA,     D3DRS_COLORWRITEENABLE,
                                           D3DRS_STENCILENABLE,    D3DRS_STENCILFUNC,
                                           D3DRS_STENCILREF,       D3DRS_STENCILMASK,
                                           D3DRS_STENCILWRITEMASK, D3DRS_STENCILFAIL,
                                           D3DRS_STENCILZFAIL,     D3DRS_STENCILPASS};
    DWORD v[14]{};
    for (unsigned n = 0; n < 14; ++n)
        if (FAILED(device->GetRenderState(keys[n], v + n)))
            return "render_state_read_failed";
    if (v[0] < 1 || v[0] > 5 || v[2] < 1 || v[2] > 11 || v[3] < 1 || v[3] > 11 || v[4] < 1 ||
        v[4] > 5 || v[5] > 15 || v[7] < 1 || v[7] > 8 || v[11] < 1 || v[11] > 8 || v[12] < 1 ||
        v[12] > 8 || v[13] < 1 || v[13] > 8)
        return "unsupported_raster_enum";
    pipeline.colorOperation = static_cast<rhi::ModelBlendOp>(v[0]);
    pipeline.separateAlpha = v[1] != 0;
    pipeline.sourceAlpha = static_cast<rhi::BlendFactor>(v[2] - 1);
    pipeline.destinationAlpha = static_cast<rhi::BlendFactor>(v[3] - 1);
    pipeline.alphaOperation = static_cast<rhi::ModelBlendOp>(v[4]);
    pipeline.colorWriteMask = static_cast<std::uint8_t>(v[5]);
    pipeline.stencil = v[6] != 0;
    pipeline.stencilCompare = static_cast<rhi::DepthComparison>(v[7] - 1);
    pipeline.stencilReference = v[8];
    pipeline.stencilReadMask = static_cast<std::uint8_t>(v[9]);
    pipeline.stencilWriteMask = static_cast<std::uint8_t>(v[10]);
    pipeline.stencilFail = static_cast<rhi::ModelStencilOp>(v[11]);
    pipeline.stencilDepthFail = static_cast<rhi::ModelStencilOp>(v[12]);
    pipeline.stencilPass = static_cast<rhi::ModelStencilOp>(v[13]);
    return nullptr;
}
std::wstring Decode(const render::TextureReference& ref) {
    if (ref.path.empty())
        return {};
    std::wstring text;
    if (ref.encoding == render::TextureEncoding::Utf16LE) {
        if (ref.path.size() % 2)
            return {};
        for (std::size_t i = 0; i < ref.path.size(); i += 2)
            text.push_back(static_cast<wchar_t>(ref.path[i] | ref.path[i + 1] << 8));
    } else {
        const auto page = ref.encoding == render::TextureEncoding::Utf8 ? CP_UTF8 : 932;
        const auto size =
            MultiByteToWideChar(page, 0, reinterpret_cast<const char*>(ref.path.data()),
                                static_cast<int>(ref.path.size()), nullptr, 0);
        if (!size)
            return {};
        text.resize(size);
        MultiByteToWideChar(page, 0, reinterpret_cast<const char*>(ref.path.data()),
                            static_cast<int>(ref.path.size()), text.data(), size);
    }
    std::replace(text.begin(), text.end(), L'/', L'\\');
    return text;
}
bool CompareTexture(IDirect3DDevice9* device, unsigned stage, rhi::MaterialTextureAsset& asset,
                    D3DSURFACE_DESC& desc, std::size_t& differences) {
    IDirect3DBaseTexture9* base = nullptr;
    bool readable =
        SUCCEEDED(device->GetTexture(stage, &base)) && base && base->GetType() == D3DRTYPE_TEXTURE;
    auto* texture = readable ? static_cast<IDirect3DTexture9*>(base) : nullptr;
    readable = readable && SUCCEEDED(texture->GetLevelDesc(0, &desc)) &&
               texture->GetLevelCount() == 1 &&
               (desc.Format == D3DFMT_A8R8G8B8 || desc.Format == D3DFMT_X8R8G8B8);
    const rhi::RgbaImage* expectedImage = &asset.content.image;
    if (readable && (desc.Width != expectedImage->width || desc.Height != expectedImage->height) &&
        (Requested("MDS_RHI_TEXTURE_PREP_CANDIDATE") || Requested("MDS_RHI_TEXTURE_PREP_D3DX"))) {
        D3DCAPS9 caps{};
        std::uint32_t width = 0, height = 0;
        if (SUCCEEDED(device->GetDeviceCaps(&caps)) &&
            rhi::NextPowerOfTwoTextureSize(expectedImage->width,
                                           std::min<DWORD>(caps.MaxTextureWidth, 4096), width) &&
            rhi::NextPowerOfTwoTextureSize(expectedImage->height,
                                           std::min<DWORD>(caps.MaxTextureHeight, 4096), height) &&
            width == desc.Width && height == desc.Height) {
            bool ready = false;
            if (Requested("MDS_RHI_TEXTURE_PREP_D3DX")) {
                auto preparer = rhi::CreateD3D9TexturePreparer(device);
                ready = preparer &&
                        preparer->Prepare(*expectedImage, width, height, asset.prepared.image);
            } else
                ready = rhi::PrepareTriangleDitherWrap(*expectedImage, width, height,
                                                       asset.prepared.image);
            if (ready) {
                asset.prepared.revision = rhi::ImageRevision(asset.prepared.image);
                expectedImage = &asset.prepared.image;
            }
        }
    }
    readable =
        readable && desc.Width == expectedImage->width && desc.Height == expectedImage->height;
    if (readable && (desc.Usage & D3DUSAGE_AUTOGENMIPMAP)) {
        readable = texture->GetAutoGenFilterType() == D3DTEXF_LINEAR;
        if (readable)
            asset.mips = rhi::TextureDesc::Mips::GenerateLinear;
    }
    D3DLOCKED_RECT lock{};
    readable = readable && SUCCEEDED(texture->LockRect(0, &lock, nullptr, D3DLOCK_READONLY));
    differences = 0;
    if (readable) {
        for (unsigned y = 0; y < desc.Height; ++y)
            for (unsigned x = 0; x < desc.Width; ++x) {
                const auto* actual =
                    static_cast<const std::uint8_t*>(lock.pBits) + y * lock.Pitch + x * 4;
                const auto* expected = expectedImage->pixels.data() +
                                       (static_cast<std::size_t>(y) * desc.Width + x) * 4;
                differences += actual[2] != expected[0];
                differences += actual[1] != expected[1];
                differences += actual[0] != expected[2];
                differences += (desc.Format == D3DFMT_X8R8G8B8 ? 255 : actual[3]) != expected[3];
            }
        readable = SUCCEEDED(texture->UnlockRect(0));
    }
    Release(base);
    const bool pass = readable && !differences;
    asset.usePrepared = pass && expectedImage == &asset.prepared.image;
    return pass;
}
bool CaptureShaderInputs(IDirect3DDevice9* device, const render::MaterialBatch& batch, bool pmd,
                         rhi::MaterialShaderInputs& inputs) {
    inputs = {};
    if (!rhi::BuildMaterialTextureLayout(pmd, Decode(batch.texture), Decode(batch.sphere),
                                         batch.sphereMode, inputs.layout))
        return false;
    inputs.diffuse = batch.diffuse;
    inputs.lighting = State(device, D3DRS_LIGHTING) != 0;
    D3DMATERIAL9 material{};
    if (FAILED(device->GetMaterial(&material)))
        return false;
    inputs.ambient = {material.Ambient.r, material.Ambient.g, material.Ambient.b};
    inputs.specular = {material.Specular.r, material.Specular.g, material.Specular.b};
    inputs.shininess = material.Power;
    const auto color = [](const D3DCOLORVALUE& v) {
        return std::array<float, 4>{v.r, v.g, v.b, v.a};
    };
    inputs.nativeDiffuse = color(material.Diffuse);
    inputs.materialAmbient = color(material.Ambient);
    inputs.materialSpecular = color(material.Specular);
    inputs.emissive = color(material.Emissive);
    for (auto pair : {std::pair{D3DTS_WORLD, &inputs.world}, std::pair{D3DTS_VIEW, &inputs.view},
                      std::pair{D3DTS_PROJECTION, &inputs.projection}})
        if (FAILED(device->GetTransform(pair.first,
                                        reinterpret_cast<D3DMATRIX*>(pair.second->data()))))
            return false;
    const DWORD ambient = State(device, D3DRS_AMBIENT);
    if (!rhi::BuildShaderNormalMatrix(inputs.world, inputs.view, inputs.normalView))
        return false;
    inputs.globalAmbient = {((ambient >> 16) & 255) / 255.f, ((ambient >> 8) & 255) / 255.f,
                            (ambient & 255) / 255.f, ((ambient >> 24) & 255) / 255.f};
    inputs.normalizeNormals = State(device, D3DRS_NORMALIZENORMALS) != 0;
    inputs.specularEnabled = State(device, D3DRS_SPECULARENABLE) != 0;
    inputs.localViewer = State(device, D3DRS_LOCALVIEWER) != 0;
    inputs.colorVertex = State(device, D3DRS_COLORVERTEX) != 0;
    unsigned sourceIndex = 0;
    for (auto key : {D3DRS_DIFFUSEMATERIALSOURCE, D3DRS_AMBIENTMATERIALSOURCE,
                     D3DRS_SPECULARMATERIALSOURCE, D3DRS_EMISSIVEMATERIALSOURCE}) {
        const auto v = State(device, key);
        if (v > 2)
            return false;
        inputs.materialSources[sourceIndex++] = static_cast<std::uint8_t>(v);
    }
    D3DCAPS9 caps{};
    if (FAILED(device->GetDeviceCaps(&caps)))
        return false;
    for (unsigned i = 0; i < std::min<DWORD>(caps.MaxActiveLights, 8); ++i) {
        BOOL enabled = FALSE;
        if (FAILED(device->GetLightEnable(i, &enabled)))
            continue;
        inputs.lights[i].enabled = enabled != FALSE;
        if (!enabled)
            continue;
        D3DLIGHT9 light{};
        if (FAILED(device->GetLight(i, &light)) || light.Type != D3DLIGHT_DIRECTIONAL)
            return false;
        inputs.lights[i].direction = {light.Direction.x, light.Direction.y, light.Direction.z};
        inputs.lights[i].diffuse = color(light.Diffuse);
        inputs.lights[i].ambient = color(light.Ambient);
        inputs.lights[i].specular = color(light.Specular);
    }
    inputs.alphaTest = State(device, D3DRS_ALPHATESTENABLE) != 0;
    const auto alpha = State(device, D3DRS_ALPHAFUNC);
    if (alpha < 1 || alpha > 8)
        return false;
    inputs.alphaCompare = static_cast<rhi::DepthComparison>(alpha - 1);
    inputs.alphaReference = static_cast<std::uint8_t>(State(device, D3DRS_ALPHAREF));
    for (unsigned stage = 0; stage < 3; ++stage) {
        auto& input = inputs.layout.stages[stage];
        if (input.role == rhi::MaterialTextureRole::None)
            continue;
        DWORD color = 0, coordinate = 0, transform = 0;
        if (FAILED(device->GetTextureStageState(stage, D3DTSS_COLOROP, &color)) ||
            FAILED(device->GetTextureStageState(stage, D3DTSS_TEXCOORDINDEX, &coordinate)) ||
            FAILED(device->GetTextureStageState(stage, D3DTSS_TEXTURETRANSFORMFLAGS, &transform)))
            return false;
        const DWORD expectedColor =
            input.color == rhi::MaterialCombine::Add ? D3DTOP_ADD : D3DTOP_MODULATE;
        const DWORD expectedCoordinate = input.coordinates == rhi::MaterialCoordinates::CameraNormal
                                             ? D3DTSS_TCI_CAMERASPACENORMAL
                                         : input.coordinates == rhi::MaterialCoordinates::MeshUv1
                                             ? 1
                                             : 0;
        if (color != expectedColor || coordinate != expectedCoordinate ||
            transform != (input.coordinates == rhi::MaterialCoordinates::MeshUv0 ? 0u : 2u)) {
            if (g_report)
                std::fprintf(
                    g_report,
                    "SHADER_DIFFERENCE frame=%u stage=%u fields=colorOp,coordinates,transformFlags expected=%u,%u,%u actual=%u,%u,%u\n",
                    g_snapshot.frame, stage, expectedColor, expectedCoordinate,
                    input.coordinates == rhi::MaterialCoordinates::MeshUv0 ? 0u : 2u, color,
                    coordinate, transform);
            return false;
        }
        const auto argument = [](rhi::MaterialColorSource source) -> DWORD {
            return source == rhi::MaterialColorSource::Texture      ? D3DTA_TEXTURE
                   : source == rhi::MaterialColorSource::LitDiffuse ? D3DTA_DIFFUSE
                                                                    : D3DTA_CURRENT;
        };
        for (auto pair : {std::pair<D3DTEXTURESTAGESTATETYPE, DWORD>{D3DTSS_COLORARG1,
                                                                     argument(input.colorArg1)},
                          {D3DTSS_COLORARG2, argument(input.colorArg2)},
                          {D3DTSS_ALPHAOP, D3DTOP_MODULATE},
                          {D3DTSS_ALPHAARG1, argument(input.alphaArg1)},
                          {D3DTSS_ALPHAARG2, argument(input.alphaArg2)}}) {
            DWORD value = 0;
            if (FAILED(device->GetTextureStageState(stage, pair.first, &value)) ||
                value != pair.second) {
                if (g_report)
                    std::fprintf(
                        g_report,
                        "SHADER_DIFFERENCE frame=%u stage=%u field=nativeTextureStage%u expected=%u actual=%u\n",
                        g_snapshot.frame, stage, static_cast<unsigned>(pair.first), pair.second,
                        value);
                return false;
            }
        }
        if (FAILED(device->GetTransform(
                static_cast<D3DTRANSFORMSTATETYPE>(D3DTS_TEXTURE0 + stage),
                reinterpret_cast<D3DMATRIX*>(inputs.textureTransforms[stage].data()))))
            return false;
        DWORD min = 0, mag = 0, mip = 0, u = 0, v = 0, aniso = 0, srgb = 0;
        if (FAILED(device->GetSamplerState(stage, D3DSAMP_MINFILTER, &min)) ||
            FAILED(device->GetSamplerState(stage, D3DSAMP_MAGFILTER, &mag)) ||
            FAILED(device->GetSamplerState(stage, D3DSAMP_MIPFILTER, &mip)) ||
            FAILED(device->GetSamplerState(stage, D3DSAMP_ADDRESSU, &u)) ||
            FAILED(device->GetSamplerState(stage, D3DSAMP_ADDRESSV, &v)) ||
            FAILED(device->GetSamplerState(stage, D3DSAMP_MAXANISOTROPY, &aniso)) ||
            FAILED(device->GetSamplerState(stage, D3DSAMP_SRGBTEXTURE, &srgb)))
            return false;
        if (min < 1 || min > 3 || mag < 1 || mag > 3 || mip < 1 || mip > 3 || (u != 1 && u != 3) ||
            (v != 1 && v != 3))
            return false;
        inputs.samplers[stage] = {static_cast<rhi::SamplerFilter>(min - 1),
                                  static_cast<rhi::SamplerFilter>(mag - 1),
                                  static_cast<rhi::SamplerFilter>(mip - 1),
                                  u == 1 ? rhi::SamplerAddress::Wrap : rhi::SamplerAddress::Clamp,
                                  v == 1 ? rhi::SamplerAddress::Wrap : rhi::SamplerAddress::Clamp,
                                  aniso};
        inputs.srgbSample[stage] = srgb != 0;
    }
    return rhi::ValidateMaterialShaderInputs(inputs);
}
bool LoadMaterialInputs(MMDApp* app, mdl::ModelRecord* model, unsigned slot, unsigned material,
                        const render::MaterialBatch& batch, bool pmd, bool auditOnly) {
    if (!auditOnly && g_materialReady)
        return true;
    rhi::MaterialShaderInputs inputs;
    bool pass = CaptureShaderInputs(app->Renderer()->device, batch, pmd, inputs);
    std::array<rhi::MaterialTextureAsset, 3> assets;
    std::array<bool, 3> owned{};
    bool blockedInput = false;
    bool preparedInput = false;
    const bool contractPass = pass;
    for (unsigned stage = 1; stage < 3; ++stage) {
        const auto role = inputs.layout.stages[stage].role;
        if (role == rhi::MaterialTextureRole::None)
            continue;
        const auto& ref = stage == 1 ? batch.texture : batch.sphere;
        // PMX promotes its explicit sphere reference to stage 1 when base is absent.
        const auto& effective =
            !pmd && stage == 1 && role == rhi::MaterialTextureRole::Sphere ? batch.sphere : ref;
        const auto status =
            rhi::LoadMaterialTexture(model->modelDirectory, Decode(effective), assets[stage]);
        D3DSURFACE_DESC desc{};
        std::size_t differences = 0;
        const bool match =
            status == rhi::MaterialResourceStatus::Ready &&
            CompareTexture(app->Renderer()->device, stage, assets[stage], desc, differences);
        const bool preparationAttempted = assets[stage].prepared.revision != 0;
        preparedInput = preparedInput || preparationAttempted;
        const bool blocked =
            !match && status == rhi::MaterialResourceStatus::Ready && desc.Width && desc.Height &&
            (preparationAttempted || desc.Width != assets[stage].content.image.width ||
             desc.Height != assets[stage].content.image.height);
        blockedInput = blockedInput || blocked;
        owned[stage] = match;
        pass = pass && match;
        if (auditOnly && g_sources) {
            ++g_sourceChecks;
            g_sourceFailures += !match && !blocked;
            g_sourceBlocked += blocked;
            std::fprintf(
                g_sources,
                "TEXTURE status=%s slot=%u material=%u stage=%u role=%u "
                "differences=%zu size=%ux%u decodedSize=%ux%u format=%u usage=%u mips=%u preparedSize=%ux%u preparedAccepted=%u revision=%llu identity=%s\n",
                match     ? "PASS"
                : blocked ? (preparationAttempted ? "BLOCKED_PREPARATION" : "BLOCKED_DIMENSIONS")
                          : "FAIL",
                slot, material, stage, static_cast<unsigned>(role), differences, desc.Width,
                desc.Height, assets[stage].content.image.width, assets[stage].content.image.height,
                static_cast<unsigned>(desc.Format), desc.Usage,
                static_cast<unsigned>(assets[stage].mips), assets[stage].prepared.image.width,
                assets[stage].prepared.image.height, assets[stage].usePrepared,
                static_cast<unsigned long long>(assets[stage].content.revision),
                assets[stage].identity.c_str());
        }
    }
    if (auditOnly && g_sources) {
        if (!contractPass)
            ++g_sourceFailures;
        std::fprintf(
            g_sources,
            "INPUTS status=%s slot=%u material=%u sphere=%u roles=%u,%u,%u alphaTest=%u alphaCompare=%u alphaRef=%u\n",
            pass ? "PASS"
            : blockedInput && contractPass
                ? (preparedInput ? "BLOCKED_PREPARATION" : "BLOCKED_DIMENSIONS")
                : "FAIL",
            slot, material, static_cast<unsigned>(inputs.layout.sphere),
            static_cast<unsigned>(inputs.layout.stages[0].role),
            static_cast<unsigned>(inputs.layout.stages[1].role),
            static_cast<unsigned>(inputs.layout.stages[2].role), inputs.alphaTest,
            static_cast<unsigned>(inputs.alphaCompare), inputs.alphaReference);
    }
    if (!auditOnly && pass) {
        g_shaderInputs = inputs;
        g_shaderReady = true;
        g_materialAssets = std::move(assets);
        g_materialOwned = owned;
        g_materialReady = true;
    }
    return pass;
}
bool LoadToon(MMDApp* app, mdl::ModelRecord* model, unsigned slot, unsigned material,
              const render::TextureReference& ref, bool pmd) {
    auto* device = app->Renderer()->device;
    if (g_toonDevice == device && g_toonSlot == static_cast<int>(slot) &&
        g_toonMaterial == material && g_toonCache)
        return true;
    g_toonCache.reset();
    g_toonDevice = nullptr;
    g_materialAssets = {};
    g_materialOwned = {};
    g_materialReady = false;
    g_shaderReady = false;
    rhi::ToonRequest request;
    request.modelDirectory = model->modelDirectory;
    request.missing = pmd ? rhi::MissingToonPolicy::Unbound : rhi::MissingToonPolicy::Default;
    if (!ref.defaultFallback && ref.index >= 0) {
        request.kind = rhi::ToonKind::ModelFile;
        request.path = Decode(ref);
        char expected[16]{};
        std::snprintf(expected, sizeof(expected), "toon%02u.bmp", ref.index + 1);
        const std::string name(ref.path.begin(), ref.path.end());
        if (ref.encoding == render::TextureEncoding::SharedToon ||
            (pmd && ref.index < 10 && name == expected)) {
            request.kind = rhi::ToonKind::Shared;
            request.sharedSlot = ref.index + 1;
        }
    }
    const rhi::CallbackAssetSource builtin([&](rhi::AssetId id, std::vector<std::uint8_t>& bytes) {
        const auto instance = static_cast<HMODULE>(app->HInstance());
        const auto resource =
            FindResourceA(instance, MAKEINTRESOURCEA(static_cast<unsigned>(id)), "PNG");
        if (!resource)
            return false;
        const auto size = SizeofResource(instance, resource);
        const auto loaded = LoadResource(instance, resource);
        const auto* data =
            static_cast<const std::uint8_t*>(loaded ? LockResource(loaded) : nullptr);
        if (!data || !size)
            return false;
        bytes.assign(data, data + size);
        return true;
    });
    const rhi::ToonTextureResolver resolver(std::filesystem::path(app->ExeDir()) / L"data",
                                            builtin);
    rhi::MaterialTextureAsset asset;
    const bool loaded = resolver.Load(request, asset);
    IDirect3DBaseTexture9* base = nullptr;
    D3DSURFACE_DESC desc{};
    bool readable =
        SUCCEEDED(device->GetTexture(0, &base)) && base && base->GetType() == D3DRTYPE_TEXTURE;
    auto* texture = readable ? static_cast<IDirect3DTexture9*>(base) : nullptr;
    readable = readable && loaded && SUCCEEDED(texture->GetLevelDesc(0, &desc)) &&
               (desc.Format == D3DFMT_A8R8G8B8 || desc.Format == D3DFMT_X8R8G8B8) &&
               desc.Width == asset.content.image.width && desc.Height == asset.content.image.height;
    std::size_t differences = 0;
    D3DLOCKED_RECT lock{};
    readable = readable && SUCCEEDED(texture->LockRect(0, &lock, nullptr, D3DLOCK_READONLY));
    if (readable) {
        for (unsigned y = 0; y < desc.Height; ++y)
            for (unsigned x = 0; x < desc.Width; ++x) {
                const auto* actual =
                    static_cast<const std::uint8_t*>(lock.pBits) + y * lock.Pitch + x * 4;
                const auto* expected = asset.content.image.pixels.data() +
                                       (static_cast<std::size_t>(y) * desc.Width + x) * 4;
                differences += actual[2] != expected[0];
                differences += actual[1] != expected[1];
                differences += actual[0] != expected[2];
                differences += (desc.Format == D3DFMT_X8R8G8B8 ? 255 : actual[3]) != expected[3];
            }
        readable = SUCCEEDED(texture->UnlockRect(0));
    }
    Release(base);
    const bool pass = readable && !differences;
    if (auto* file = std::fopen((Directory() + "\\model_toon.source").c_str(), "wb")) {
        std::fprintf(file,
                     "status=%s differences=%zu loaded=%u readable=%u slot=%u material=%u "
                     "size=%ux%u nativeFormat=%u origin=%u fallback=%u identity=%s revision=%llu\n",
                     pass ? "PASS" : "FAIL", differences, loaded, readable, slot, material,
                     desc.Width, desc.Height, static_cast<unsigned>(desc.Format),
                     static_cast<unsigned>(asset.origin), asset.fallback, asset.identity.c_str(),
                     static_cast<unsigned long long>(asset.content.revision));
        std::fclose(file);
    }
    if (!pass)
        return false;
    g_toonCache = rhi::CreateBorrowedModelTextureCache(device);
    if (!g_toonCache)
        return false;
    g_toon = std::move(asset);
    g_toonDevice = device;
    g_toonSlot = slot;
    g_toonMaterial = material;
    return true;
}
bool AdapterDraw(IDirect3DDevice9* device, const std::vector<std::uint8_t>& vertices,
                 const std::vector<std::uint8_t>& indices, unsigned first, unsigned count,
                 rhi::BatchRasterState raster, rhi::InSceneIndexedDraw draw, long* result,
                 bool readOnly, bool* issued = nullptr) {
    if (Requested("MDS_RHI_MODEL_TOON")) {
        if (Requested("MDS_RHI_SHADER_INPUTS")) {
            const auto model = std::find_if(g_snapshot.models.begin(), g_snapshot.models.end(),
                                            [](const auto& m) { return m.slot == g_toonSlot; });
            // Never retain previous-frame camera/light values on a cache hit.
            g_shaderReady =
                model != g_snapshot.models.end() && g_toonMaterial < model->batches.size() &&
                CaptureShaderInputs(device, model->batches[g_toonMaterial],
                                    model->kind == format::ModelKind::Pmd, g_shaderInputs);
            if (!g_shaderReady) {
                if (issued)
                    *issued = false;
                if (result)
                    *result = E_FAIL;
                return false;
            }
        }
        if (!g_toonCache || g_toonDevice != device) {
            if (issued)
                *issued = false;
            if (result)
                *result = E_FAIL;
            return false;
        }
        std::array<const rhi::MaterialTextureAsset*, 3> assets{&g_toon, nullptr, nullptr};
        if (Requested("MDS_RHI_SHADER_INPUTS")) {
            const auto submit = std::move(draw);
            draw = [submit](IDirect3DDevice9* native, unsigned vertices, unsigned start,
                            unsigned primitives) {
                const auto model = std::find_if(g_snapshot.models.begin(), g_snapshot.models.end(),
                                                [](const auto& m) { return m.slot == g_toonSlot; });
                rhi::MaterialShaderInputs actual;
                if (model == g_snapshot.models.end() ||
                    !CaptureShaderInputs(native, model->batches[g_toonMaterial],
                                         model->kind == format::ModelKind::Pmd, actual))
                    return static_cast<long>(E_FAIL);
                const auto directory = Directory();
                FILE* report = nullptr;
                if (!directory.empty())
                    fopen_s(&report, (directory + "/shader_inputs.transport").c_str(), "a");
                unsigned differences = 0;
                const auto compare = [&](const char* name, const auto& expected,
                                         const auto& observed) {
                    for (std::size_t i = 0; i < expected.size(); ++i)
                        if (expected[i] != observed[i]) {
                            ++differences;
                            if (report)
                                std::fprintf(
                                    report,
                                    "DIFFERENCE frame=%u slot=%d material=%u field=%s component=%zu expected=%.9g actual=%.9g\n",
                                    g_snapshot.frame, g_toonSlot, g_toonMaterial, name, i,
                                    static_cast<double>(expected[i]),
                                    static_cast<double>(observed[i]));
                        }
                };
                const auto& s = g_shaderInputs;
                compare("world", s.world, actual.world);
                compare("view", s.view, actual.view);
                compare("projection", s.projection, actual.projection);
                compare("normalView", s.normalView, actual.normalView);
                compare("diffuse", s.nativeDiffuse, actual.nativeDiffuse);
                compare("ambient", s.materialAmbient, actual.materialAmbient);
                compare("specular", s.materialSpecular, actual.materialSpecular);
                compare("emissive", s.emissive, actual.emissive);
                compare("globalAmbient", s.globalAmbient, actual.globalAmbient);
                compare("materialSources", s.materialSources, actual.materialSources);
                compare("shininess", std::array<float, 1>{s.shininess},
                        std::array<float, 1>{actual.shininess});
                compare("lightingFlags",
                        std::array<unsigned, 5>{s.lighting, s.normalizeNormals, s.specularEnabled,
                                                s.localViewer, s.colorVertex},
                        std::array<unsigned, 5>{actual.lighting, actual.normalizeNormals,
                                                actual.specularEnabled, actual.localViewer,
                                                actual.colorVertex});
                compare("alphaTest",
                        std::array<unsigned, 3>{s.alphaTest, static_cast<unsigned>(s.alphaCompare),
                                                s.alphaReference},
                        std::array<unsigned, 3>{actual.alphaTest,
                                                static_cast<unsigned>(actual.alphaCompare),
                                                actual.alphaReference});
                for (unsigned i = 0; i < 8; ++i) {
                    differences += s.lights[i].enabled != actual.lights[i].enabled;
                    if (s.lights[i].enabled) {
                        compare("lightDirection", s.lights[i].direction,
                                actual.lights[i].direction);
                        compare("lightDiffuse", s.lights[i].diffuse, actual.lights[i].diffuse);
                        compare("lightAmbient", s.lights[i].ambient, actual.lights[i].ambient);
                        compare("lightSpecular", s.lights[i].specular, actual.lights[i].specular);
                    }
                }
                for (unsigned i = 0; i < 3; ++i)
                    if (s.layout.stages[i].role != rhi::MaterialTextureRole::None) {
                        compare("textureTransform", s.textureTransforms[i],
                                actual.textureTransforms[i]);
                        const auto sampler = [](const rhi::SamplerDesc& v) {
                            return std::array<unsigned, 6>{
                                static_cast<unsigned>(v.min), static_cast<unsigned>(v.mag),
                                static_cast<unsigned>(v.mip), static_cast<unsigned>(v.u),
                                static_cast<unsigned>(v.v),   v.maxAnisotropy};
                        };
                        compare("sampler", sampler(s.samplers[i]), sampler(actual.samplers[i]));
                        compare("srgbSample", std::array<unsigned, 1>{s.srgbSample[i]},
                                std::array<unsigned, 1>{actual.srgbSample[i]});
                    }
                if (report) {
                    std::fprintf(
                        report, "TRANSPORT status=%s frame=%u slot=%d material=%u differences=%u\n",
                        differences ? "FAIL" : "PASS", g_snapshot.frame, g_toonSlot, g_toonMaterial,
                        differences);
                    std::fclose(report);
                }
                return differences ? static_cast<long>(E_FAIL)
                                   : submit(native, vertices, start, primitives);
            };
        }
        if (Requested("MDS_RHI_MODEL_TEXTURES")) {
            if (!g_materialReady)
                return false;
            for (unsigned stage = 1; stage < 3; ++stage)
                if (g_materialOwned[stage])
                    assets[stage] = &g_materialAssets[stage];
        }
        return g_toonCache->DrawMaterial(
            assets, vertices.data(), vertices.size(), indices.data(), indices.size(), first, count,
            raster, std::move(draw), result, readOnly, issued,
            Requested("MDS_RHI_SHADER_INPUTS") && g_shaderReady ? &g_shaderInputs : nullptr);
    }
    return rhi::DrawModelBatchInScene(device, vertices.data(), vertices.size(), indices.data(),
                                      indices.size(), first, count, raster, std::move(draw), result,
                                      readOnly, issued);
}
bool SamePath(const render::TextureReference& reference, const wchar_t* path) {
    auto expected = Decode(reference);
    if (!reference.path.empty() && expected.empty())
        return false;
    std::wstring actual(path);
    std::replace(actual.begin(), actual.end(), L'/', L'\\');
    if (expected.empty())
        return actual.empty();
    if (actual.size() < expected.size())
        return false;
    const auto at = actual.size() - expected.size();
    return _wcsicmp(actual.c_str() + at, expected.c_str()) == 0 &&
           (at == 0 || actual[at - 1] == L'\\');
}
void Path(FILE* file, const char* label, const render::TextureReference& ref) {
    std::fprintf(file, " %s={index:%d,encoding:%u,default:%u,bytes:", label, ref.index,
                 static_cast<unsigned>(ref.encoding), ref.defaultFallback);
    for (auto byte : ref.path)
        std::fprintf(file, "%02x", byte);
    std::fputc('}', file);
}
const render::ModelSnapshot* FindModel(mdl::ModelRecord* model, MMDApp* app) {
    for (const auto& snapshot : g_snapshot.models)
        if (app->ModelSlot(snapshot.slot) == model)
            return &snapshot;
    return nullptr;
}
bool MeshBytes(IDirect3DVertexBuffer9* vb, IDirect3DIndexBuffer9* ib, unsigned vertices,
               unsigned indices, std::vector<std::uint8_t>& vertexBytes,
               std::vector<std::uint8_t>& indexBytes) {
    D3DVERTEXBUFFER_DESC vd{};
    D3DINDEXBUFFER_DESC id{};
    if (FAILED(vb->GetDesc(&vd)) || FAILED(ib->GetDesc(&id)) || id.Format != D3DFMT_INDEX16 ||
        vertices > vd.Size / 32 || indices > id.Size / 2 || !vertices || !indices)
        return false;
    void* data = nullptr;
    if (FAILED(vb->Lock(0, vertices * 32, &data, 0)))
        return false;
    vertexBytes.assign(static_cast<std::uint8_t*>(data),
                       static_cast<std::uint8_t*>(data) + vertices * 32);
    if (FAILED(vb->Unlock()) || FAILED(ib->Lock(0, indices * 2, &data, 0)))
        return false;
    indexBytes.assign(static_cast<std::uint8_t*>(data),
                      static_cast<std::uint8_t*>(data) + indices * 2);
    return SUCCEEDED(ib->Unlock());
}
bool ExportRealBatch(IDirect3DDevice9* device, unsigned slot, unsigned material, unsigned first,
                     unsigned count, const std::vector<std::uint8_t>& vertices,
                     const std::vector<std::uint8_t>& indices) {
    if (!Requested("MDS_RHI_REAL_BATCH_CAPTURE"))
        return true;
    EndDiagnosticHistoryAudit(device, slot, material, count);
    CompareHistoryReplay(device, slot, material, count);
    DWORD fvf = 0;
    if (FAILED(device->GetFVF(&fvf)) || fvf != (D3DFVF_XYZ | D3DFVF_NORMAL | D3DFVF_TEX1))
        return false;
    const auto model = std::find_if(g_snapshot.models.begin(), g_snapshot.models.end(),
                                    [&](const auto& m) { return m.slot == slot; });
    rhi::MaterialShaderInputs inputs;
    if (model == g_snapshot.models.end() || material >= model->batches.size() ||
        !CaptureShaderInputs(device, model->batches[material],
                             model->kind == format::ModelKind::Pmd, inputs))
        return false;
    rhi::RealBatchCapture header;
    D3DVIEWPORT9 viewport{};
    if (FAILED(device->GetViewport(&viewport)))
        return false;
    header.width = viewport.Width;
    header.height = viewport.Height;
    header.frame = g_snapshot.frame;
    header.slot = slot;
    header.material = material;
    header.first = first;
    header.count = count;
    header.vertexBytes = static_cast<unsigned>(vertices.size());
    header.indexBytes = static_cast<unsigned>(indices.size());
    IDirect3DSurface9* target = nullptr;
    D3DSURFACE_DESC targetDesc{};
    if (FAILED(device->GetRenderTarget(0, &target)))
        return false;
    const auto hr = target->GetDesc(&targetDesc);
    Release(target);
    if (FAILED(hr))
        return false;
    header.multisample = targetDesc.MultiSampleType;
    header.colorFormat = targetDesc.Format;
    header.vertexProcessing = device->GetSoftwareVertexProcessing() ? 1u : 0u;
    header.multisampleQuality = targetDesc.MultiSampleQuality;
    for (unsigned i = 0; i < header.render.size(); ++i) {
        DWORD v = 0xffffffff;
        device->GetRenderState(static_cast<D3DRENDERSTATETYPE>(i), &v);
        header.render[i] = v;
    }
    for (unsigned s = 0; s < 3; ++s) {
        for (unsigned i = 0; i < 33; ++i) {
            DWORD v = 0xffffffff;
            device->GetTextureStageState(s, static_cast<D3DTEXTURESTAGESTATETYPE>(i), &v);
            header.stages[s][i] = v;
        }
        for (unsigned i = 0; i < 14; ++i) {
            DWORD v = 0xffffffff;
            device->GetSamplerState(s, static_cast<D3DSAMPLERSTATETYPE>(i), &v);
            header.samplers[s][i] = v;
        }
    }
    std::vector<std::uint8_t> historyColor;
    std::vector<std::uint32_t> historyDepth;
    if (Requested("MDS_RHI_SCENE_HISTORY")) {
        rhi::HistoryData history;
        if (CaptureDiagnosticHistory(device, history) != rhi::HistoryResult::Success)
            return false;
        historyColor = std::move(history.rgba);
        historyDepth = std::move(history.depthStencil);
        header.history = 1;
    }
    std::ofstream out(Directory() + "/real_batch.capture", std::ios::binary | std::ios::trunc);
    const auto write = [&](const auto& value) {
        out.write(reinterpret_cast<const char*>(&value), sizeof(value));
    };
    write(header);
    write(inputs);
    out.write(reinterpret_cast<const char*>(vertices.data()), vertices.size());
    out.write(reinterpret_cast<const char*>(indices.data()), indices.size());
    for (unsigned s = 0; s < 3; ++s) {
        IDirect3DBaseTexture9* base = nullptr;
        rhi::RealBatchTexture textureHeader;
        if (FAILED(device->GetTexture(s, &base)))
            return false;
        if (!base) {
            write(textureHeader);
            continue;
        }
        if (base->GetType() != D3DRTYPE_TEXTURE) {
            Release(base);
            return false;
        }
        auto* texture = static_cast<IDirect3DTexture9*>(base);
        D3DSURFACE_DESC desc{};
        bool ok = SUCCEEDED(texture->GetLevelDesc(0, &desc)) &&
                  (desc.Format == D3DFMT_A8R8G8B8 || desc.Format == D3DFMT_X8R8G8B8);
        textureHeader = {desc.Width, desc.Height, texture->GetLevelCount(),
                         (desc.Usage & D3DUSAGE_AUTOGENMIPMAP) ? 1u : 0u};
        write(textureHeader);
        for (unsigned level = 0; ok && level < textureHeader.levels; ++level) {
            D3DLOCKED_RECT lock{};
            ok = SUCCEEDED(texture->GetLevelDesc(level, &desc)) &&
                 SUCCEEDED(texture->LockRect(level, &lock, nullptr, D3DLOCK_READONLY));
            if (!ok)
                break;
            std::vector<std::uint8_t> rgba(desc.Width * desc.Height * 4);
            for (unsigned y = 0; y < desc.Height; ++y)
                for (unsigned x = 0; x < desc.Width; ++x) {
                    const auto* p =
                        static_cast<const std::uint8_t*>(lock.pBits) + y * lock.Pitch + x * 4;
                    auto* q = rgba.data() + (y * desc.Width + x) * 4;
                    q[0] = p[2];
                    q[1] = p[1];
                    q[2] = p[0];
                    q[3] = desc.Format == D3DFMT_X8R8G8B8 ? 255 : p[3];
                }
            out.write(reinterpret_cast<const char*>(rgba.data()), rgba.size());
            ok = SUCCEEDED(texture->UnlockRect(level));
        }
        Release(base);
        if (!ok)
            return false;
    }
    if (header.history) {
        out.write(reinterpret_cast<const char*>(historyColor.data()), historyColor.size());
        out.write(reinterpret_cast<const char*>(historyDepth.data()), historyDepth.size() * 4);
    }
    return !!out;
}
void Probe(IDirect3DDevice9* device, unsigned slot, unsigned material, unsigned first,
           unsigned count, unsigned vertices, const std::vector<std::uint8_t>& vertexBytes,
           const std::vector<std::uint8_t>& indexBytes, rhi::BatchRasterState raster) {
    auto& c = g_comparison;
    if (c.active)
        return;
    if (!ExportRealBatch(device, slot, material, first, count, vertexBytes, indexBytes)) {
        if (g_report)
            std::fprintf(g_report, "REAL_BATCH_CAPTURE status=FAIL slot=%u material=%u\n", slot,
                         material);
    }
    c.active = true;
    c.applied = c.actualApplied = c.actualAttempted = false;
    c.bridgeCalls = c.hostCalls = c.directCalls = 0;
    c.slot = slot;
    c.material = material;
    c.frame = g_snapshot.frame;
    c.first = first;
    c.count = count;
    c.directory = Directory();
    c.hr = device->GetRenderTarget(0, &c.original);
    if (SUCCEEDED(c.hr))
        c.hr = device->GetViewport(&c.viewport);
    if (SUCCEEDED(c.hr))
        c.hr = c.original->GetDesc(&c.desc);
    const auto create = [&](IDirect3DSurface9** out, bool multisample) {
        return device->CreateRenderTarget(
            c.desc.Width, c.desc.Height, c.desc.Format,
            multisample ? c.desc.MultiSampleType : D3DMULTISAMPLE_NONE,
            multisample ? c.desc.MultiSampleQuality : 0, FALSE, out, nullptr);
    };
    for (auto out : {&c.legacy, &c.adapter})
        if (SUCCEEDED(c.hr))
            c.hr = create(out, true);
    for (auto out : {&c.baseline, &c.legacyResolved, &c.adapterResolved, &c.actualResolved})
        if (SUCCEEDED(c.hr))
            c.hr = create(out, false);
    for (auto out : {c.legacy, c.adapter, c.baseline})
        if (SUCCEEDED(c.hr))
            c.hr = device->StretchRect(c.original, nullptr, out, nullptr, D3DTEXF_NONE);
    IDirect3DStateBlock9* previous = nullptr;
    if (SUCCEEDED(c.hr))
        c.hr = device->CreateStateBlock(D3DSBT_ALL, &previous);
    if (SUCCEEDED(c.hr))
        c.hr = previous->Capture();
    if (SUCCEEDED(c.hr))
        c.hr = device->SetRenderTarget(0, c.legacy);
    // SetRenderTarget resets the viewport to the entire surface. The model
    // viewport is the canvas inside the editor, so restore it for both draws.
    if (SUCCEEDED(c.hr))
        c.hr = device->SetViewport(&c.viewport);
    // Both probes share the scene's exact depth/stencil tests. All writes
    // are suppressed, so probing cannot change the following real draw.
    if (SUCCEEDED(c.hr))
        c.hr = device->SetRenderState(D3DRS_ZWRITEENABLE, FALSE);
    for (auto key : {D3DRS_STENCILFAIL, D3DRS_STENCILZFAIL, D3DRS_STENCILPASS})
        if (SUCCEEDED(c.hr))
            c.hr = device->SetRenderState(key, D3DSTENCILOP_KEEP);
    if (SUCCEEDED(c.hr))
        c.oldHr =
            device->DrawIndexedPrimitive(D3DPT_TRIANGLELIST, 0, 0, vertices, first, count / 3);
    if (SUCCEEDED(c.hr) && FAILED(c.oldHr))
        c.hr = c.oldHr;
    if (SUCCEEDED(c.hr))
        c.hr = device->SetRenderTarget(0, c.adapter);
    if (SUCCEEDED(c.hr))
        c.hr = device->SetViewport(&c.viewport);
    if (SUCCEEDED(c.hr)) {
        long result = E_FAIL;
        c.applied = AdapterDraw(
            device, vertexBytes, indexBytes, first, count, raster,
            [](IDirect3DDevice9* d, unsigned v, unsigned f, unsigned p) {
                return d->DrawIndexedPrimitive(D3DPT_TRIANGLELIST, 0, 0, v, f, p);
            },
            &result, true);
        c.newHr = result;
        if (!c.applied)
            c.hr = E_FAIL;
    }
    const auto targetRestored = c.original ? device->SetRenderTarget(0, c.original) : E_FAIL;
    const auto stateRestored = previous ? previous->Apply() : E_FAIL;
    Release(previous);
    if (SUCCEEDED(c.hr) && FAILED(targetRestored))
        c.hr = targetRestored;
    if (SUCCEEDED(c.hr) && FAILED(stateRestored))
        c.hr = stateRestored;
    if (SUCCEEDED(c.hr))
        c.hr = device->StretchRect(c.legacy, nullptr, c.legacyResolved, nullptr, D3DTEXF_NONE);
    if (SUCCEEDED(c.hr))
        c.hr = device->StretchRect(c.adapter, nullptr, c.adapterResolved, nullptr, D3DTEXF_NONE);
}
bool Pixels(IDirect3DDevice9* device, IDirect3DSurface9* source, IDirect3DSurface9* system,
            const D3DSURFACE_DESC& desc, std::vector<DWORD>& pixels) {
    if (FAILED(device->GetRenderTargetData(source, system)))
        return false;
    D3DLOCKED_RECT locked{};
    if (FAILED(system->LockRect(&locked, nullptr, D3DLOCK_READONLY)))
        return false;
    pixels.resize(static_cast<std::size_t>(desc.Width) * desc.Height);
    for (unsigned y = 0; y < desc.Height; ++y)
        std::memcpy(pixels.data() + static_cast<std::size_t>(y) * desc.Width,
                    static_cast<const std::uint8_t*>(locked.pBits) + y * locked.Pitch,
                    desc.Width * 4);
    return SUCCEEDED(system->UnlockRect());
}
}

void BeginModelBatchCapture(MMDApp* app, IDirect3DDevice9* device) {
    if (Requested("MDS_RHI_CPU_INPUTS")) {
        g_frameReady = app->PortableSceneSession() &&
                       render::CaptureSceneSnapshot(*app->PortableSceneSession(), g_snapshot);
        return;
    }
    g_frameReady = false;
    if (app->PortableSceneSession() != g_session) {
        InvalidateModelBatchComparison();
        g_session = app->PortableSceneSession();
    }
    if (!Requested("MDS_RHI_MODEL_AB") || !app->PortableSceneSession() || Directory().empty() ||
        !render::CaptureSceneSnapshot(*app->PortableSceneSession(), g_snapshot))
        return;
    g_frameReady = true;
    if (!g_snapshotSaved) {
        g_report = std::fopen((Directory() + "\\model_material.snapshot").c_str(), "wb");
        g_sequence = g_mismatches = g_eligible = 0;
        g_expected = 0;
        g_seen.clear();
        if (Requested("MDS_RHI_MODEL_TEXTURES")) {
            g_sourceChecks = g_sourceFailures = g_sourceBlocked = 0;
            g_sources = std::fopen((Directory() + "\\model_textures.source").c_str(), "wb");
        }
        for (const auto& model : g_snapshot.models) {
            const auto* runtime = app->ModelSlot(model.slot);
            const bool resourceSame = runtime && runtime->loadComplete &&
                                      runtime->vertexCount == model.vertexCount &&
                                      runtime->indexCount == model.indexCount &&
                                      runtime->materialCount == model.batches.size();
            g_mismatches += !resourceSame;
            for (const auto& batch : model.batches)
                if (batch.indexCount && model.visible)
                    ++g_expected;
        }
        if (g_report)
            std::fprintf(g_report, "schema=1 frame=%u models=%zu backend=DX9\n", g_snapshot.frame,
                         g_snapshot.models.size());
    }
    if (g_gateDevice && g_gateDevice != device)
        g_gatePassed = false;
}
void EndModelBatchCapture() {
    if (!g_report)
        return;
    g_mismatches += g_seen.size() != g_expected;
    std::fprintf(g_report,
                 "SUMMARY status=%s draws=%u mismatches=%u eligible=%u unique=%zu expected=%u\n",
                 g_sequence && !g_mismatches ? "PASS" : "FAIL", g_sequence, g_mismatches,
                 g_eligible, g_seen.size(), g_expected);
    std::fclose(g_report);
    g_report = nullptr;
    // Empty frames (loading or camera tool mode) are not acceptance evidence.
    g_snapshotSaved = g_sequence != 0;
    g_snapshotPassed = g_snapshotSaved && !g_mismatches;
    if (g_sources) {
        std::fprintf(g_sources, "SUMMARY status=%s checks=%u failures=%u blocked=%u\n",
                     g_sourceChecks && !g_sourceFailures ? (g_sourceBlocked ? "PARTIAL" : "PASS")
                                                         : "FAIL",
                     g_sourceChecks, g_sourceFailures, g_sourceBlocked);
        std::fclose(g_sources);
        g_sources = nullptr;
    }
}

bool AuditLiveCpuModelInputs(MMDApp* app, mdl::ModelRecord* raw, unsigned material, unsigned first,
                             unsigned count, const rhi::ModelPipeline& generated,
                             const std::array<rhi::MaterialTextureAsset, 3>& assets,
                             rhi::ModelPipeline& reference) {
    const auto* model = FindModel(raw, app);
    if (!g_frameReady || !model || material >= model->batches.size())
        return false;
    const auto& batch = model->batches[material];
    auto* device = app->Renderer()->device;
    const char* unsupported = CaptureModelRaster(device, reference);
    D3DVIEWPORT9 viewport{};
    bool captured =
        !unsupported && SUCCEEDED(device->GetViewport(&viewport)) &&
        CaptureShaderInputs(device, batch, model->kind == format::ModelKind::Pmd, reference.shader);
    reference.viewport = rhi::ModelViewport{viewport.X,      viewport.Y,    viewport.Width,
                                            viewport.Height, viewport.MinZ, viewport.MaxZ};
    IDirect3DVertexShader9* vs = nullptr;
    IDirect3DPixelShader9* ps = nullptr;
    captured = captured && SUCCEEDED(device->GetVertexShader(&vs)) &&
               SUCCEEDED(device->GetPixelShader(&ps)) && !vs && !ps;
    Release(vs);
    Release(ps);
    const auto differences = render::CompareModelInputs(generated, reference);
    std::size_t textureDifferences = 0;
    bool texturesSame = true;
    for (unsigned stage = 0; stage < 3; ++stage)
        if (generated.shader.layout.stages[stage].role != rhi::MaterialTextureRole::None) {
            // Audit a copy. Backend preparation must never silently mutate CPU data.
            auto asset = assets[stage];
            D3DSURFACE_DESC desc{};
            std::size_t different = 0;
            const bool same =
                CompareTexture(device, stage, asset, desc, different) && !asset.usePrepared;
            textureDifferences += different;
            texturesSame = texturesSame && same;
        }
    const bool rangeSame = first == batch.firstIndex && count == batch.indexCount;
    const bool pass = captured && differences.empty() && texturesSame && rangeSame;
    char directory[MAX_PATH]{};
    if (GetEnvironmentVariableA("MDS_RHI_CPU_GEOMETRY_DIR", directory, MAX_PATH)) {
        char path[MAX_PATH]{};
        std::snprintf(path, sizeof(path), "%s\\cpu_inputs.%lu.csv", directory,
                      GetCurrentProcessId());
        const bool fresh = GetFileAttributesA(path) == INVALID_FILE_ATTRIBUTES;
        FILE* file = nullptr;
        if (!fopen_s(&file, path, "a") && file) {
            if (fresh)
                std::fputs(
                    "frame,model,material,playing,cameraSource,status,field,component,generated,reference,delta,differentFields,textureDifferentBytes\n",
                    file);
            const auto& frame = app->CpuRenderFrame();
            const auto row = [&](const char* status, const char* field, unsigned component,
                                 double cpu, double native) {
                std::fprintf(file, "%u,%u,%u,%u,%s,%s,%s,%u,%.17g,%.17g,%.17g,%zu,%zu\n",
                             frame.scene.frame, model->slot, material,
                             app->PortableSceneSession()->Playing(),
                             frame.view.editorNavigation ? "editor_navigation" : "scene_session",
                             status, field, component, cpu, native, cpu - native,
                             differences.size(), textureDifferences);
            };
            row(pass ? "PASS" : "FAIL",
                unsupported     ? unsupported
                : !captured     ? "native_reference_unavailable"
                : !rangeSame    ? "mesh_range"
                : !texturesSame ? "texture"
                                : "summary",
                0, 0, 0);
            for (const auto& difference : differences)
                row("DIFFERENCE", difference.field.c_str(), difference.component,
                    difference.generated, difference.reference);
            std::fclose(file);
        }
    }
    return pass;
}

bool TryModelBatchAdapter(MMDApp* app, mdl::ModelRecord* model, unsigned materialIndex,
                          unsigned first, unsigned count, unsigned stride) {
    if (Requested("MDS_RHI_CPU_INPUTS"))
        return false;
    if (!Requested("MDS_RHI_MODEL_AB") || !g_frameReady)
        return false;
    auto* device = app->Renderer()->device;
    const auto* snapshot = FindModel(model, app);
    auto& state = *model;
    if (!snapshot || materialIndex >= snapshot->batches.size())
        return false;
    const auto& batch = snapshot->batches[materialIndex];
    const auto& material = (model)->materials[materialIndex];
    rhi::BatchRasterState raster;
    const bool rasterValid = CaptureRaster(device, raster);
    const bool rangeSame = first == batch.firstIndex && count == batch.indexCount &&
                           state.vertexCount == snapshot->vertexCount &&
                           state.indexCount == snapshot->indexCount;
    const bool orderSame = state.comboSelIndex == snapshot->drawOrder;
    const bool colorSame = std::memcmp(batch.diffuse.data(), material.diffuse, 16) == 0;
    const bool textureSame = SamePath(batch.texture, material.texturePath);
    const bool sphereSame = SamePath(batch.sphere, material.spherePath);
    const auto& session = *app->PortableSceneSession();
    const auto found =
        std::find_if(session.Document().models.begin(), session.Document().models.end(),
                     [&](const format::PmmSceneModel& m) { return m.slot == snapshot->slot; });
    if (found == session.Document().models.end())
        return false;
    const auto resourceIndex = static_cast<std::size_t>(found - session.Document().models.begin());
    const auto& resource = *session.Models()[resourceIndex];
    const auto& sourceMaterial = resource.kind == format::ModelKind::Pmd
                                     ? resource.pmd.materials[materialIndex]
                                     : resource.pmx.materials[materialIndex].fields;
    const auto expectedToon = batch.toon.defaultFallback ? 0xff : sourceMaterial.toonReference;
    bool toonSame =
        expectedToon == material.toonReference &&
        (batch.toon.encoding == render::TextureEncoding::SharedToon ||
         resource.kind == format::ModelKind::Pmd || SamePath(batch.toon, material.toonPath));
    if (resource.kind == format::ModelKind::Pmd && sourceMaterial.toonReference < 10) {
        const auto* name = (model)->pmdToonFileNames[sourceMaterial.toonReference];
        const std::vector<std::uint8_t> bytes(name, std::find(name, name + 100, '\0'));
        toonSame = toonSame && bytes == batch.toon.path;
    }
    const bool flagsSame =
        resource.kind == format::ModelKind::Pmd
            ? material.doubleSided == (batch.flags ? 1 : 0)
            : (batch.flags == material.flags && batch.sphereMode == material.sphereMode);
    const bool same = rangeSame && orderSame && colorSame && textureSame && sphereSame &&
                      toonSame && flagsSame && rasterValid;
    const auto host = mme::ProbeDrawSnapshot();
    const bool noEffects = !mme::EffectsActive() || host.effectsDisabled || host.endSceneFired;
    IDirect3DVertexShader9* vs = nullptr;
    IDirect3DPixelShader9* ps = nullptr;
    const bool shadersRead =
        SUCCEEDED(device->GetVertexShader(&vs)) && SUCCEEDED(device->GetPixelShader(&ps));
    const bool fixed = shadersRead && !vs && !ps;
    Release(vs);
    Release(ps);
    const bool eligible = batch.dependency == render::BatchDependency::Independent && noEffects &&
                          fixed && stride == 32 && same && state.displayState == 0 &&
                          state.postLoadFlag2 == 0;
    if (g_report) {
        ++g_sequence;
        g_mismatches += !same;
        g_eligible += eligible;
        const auto identity = std::make_pair(static_cast<unsigned>(snapshot->slot), materialIndex);
        if (std::find(g_seen.begin(), g_seen.end(), identity) == g_seen.end())
            g_seen.push_back(identity);
        render::MaterialDraw draw;
        draw.modelSlot = snapshot->slot;
        draw.modelOrder = snapshot->drawOrder;
        draw.sequence = g_sequence - 1;
        draw.pass = static_cast<unsigned>(
            std::count_if(g_snapshot.materialDraws.begin(), g_snapshot.materialDraws.end(),
                          [&](const render::MaterialDraw& prior) {
                              return prior.modelSlot == draw.modelSlot &&
                                     prior.batch.materialIndex == materialIndex;
                          }));
        draw.batch = batch;
        draw.raster = raster;
        {
            rhi::MaterialShaderInputs inputs;
            if (CaptureShaderInputs(device, batch, resource.kind == format::ModelKind::Pmd, inputs))
                draw.shaderInputs = inputs;
            if (Requested("MDS_RHI_MODEL_TEXTURES"))
                LoadMaterialInputs(app, model, snapshot->slot, materialIndex, batch,
                                   resource.kind == format::ModelKind::Pmd, true);
        }
        g_snapshot.materialDraws.push_back(draw);
        std::fprintf(
            g_report,
            "DRAW sequence=%u pass=%u slot=%u order=%u material=%u first=%u count=%u vertices=%u "
            "rangeSame=%u orderSame=%u colorSame=%u textureSame=%u sphereSame=%u toonSame=%u flagsSame=%u "
            "depthTest=%u depthWrite=%u depthCompare=%u alphaBlend=%u srcBlend=%u dstBlend=%u "
            "cull=%u dependency=%u noEffects=%u eligible=%u",
            g_sequence - 1, draw.pass, snapshot->slot, state.comboSelIndex, materialIndex, first,
            count, state.vertexCount, rangeSame, orderSame, colorSame, textureSame, sphereSame,
            toonSame, flagsSame, raster.depthTest, raster.depthWrite,
            static_cast<unsigned>(raster.depthCompare), raster.alphaBlend,
            static_cast<unsigned>(raster.sourceBlend),
            static_cast<unsigned>(raster.destinationBlend), static_cast<unsigned>(raster.cull),
            static_cast<unsigned>(batch.dependency), noEffects, eligible);
        Path(g_report, "texture", batch.texture);
        Path(g_report, "sphere", batch.sphere);
        Path(g_report, "toon", batch.toon);
        std::fprintf(g_report, " nativeAlphaTest=%u nativeLighting=%u\n",
                     State(device, D3DRS_ALPHATESTENABLE), State(device, D3DRS_LIGHTING));
        std::fprintf(g_report, "SHADER sequence=%u status=%s\n", draw.sequence,
                     draw.shaderInputs ? "CAPTURED" : "UNSUPPORTED");
        if (draw.shaderInputs) {
            const auto& s = *draw.shaderInputs;
            const auto values = [&](const char* name, const auto& a) {
                std::fprintf(g_report, "INPUT sequence=%u field=%s", draw.sequence, name);
                for (auto v : a)
                    std::fprintf(g_report, " %.9g", static_cast<double>(v));
                std::fputc('\n', g_report);
            };
            values("world", s.world);
            values("view", s.view);
            values("projection", s.projection);
            values("normalView", s.normalView);
            values("nativeDiffuse", s.nativeDiffuse);
            values("ambient", s.materialAmbient);
            values("specular", s.materialSpecular);
            values("emissive", s.emissive);
            values("globalAmbient", s.globalAmbient);
            std::fprintf(
                g_report,
                "PARAMETERS sequence=%u shininess=%.9g lighting=%u normalizeNormals=%u specular=%u localViewer=%u colorVertex=%u alphaTest=%u alphaCompare=%u alphaReference=%u\n",
                draw.sequence, s.shininess, s.lighting, s.normalizeNormals, s.specularEnabled,
                s.localViewer, s.colorVertex, s.alphaTest, static_cast<unsigned>(s.alphaCompare),
                s.alphaReference);
            values("materialSources", s.materialSources);
            for (unsigned i = 0; i < 3; ++i)
                if (s.layout.stages[i].role != rhi::MaterialTextureRole::None) {
                    const auto& stage = s.layout.stages[i];
                    std::fprintf(
                        g_report,
                        "TEXTURE_INPUT sequence=%u stage=%u role=%u coordinates=%u color=%u colorArgs=%u,%u alpha=%u alphaArgs=%u,%u srgb=%u\n",
                        draw.sequence, i, static_cast<unsigned>(stage.role),
                        static_cast<unsigned>(stage.coordinates),
                        static_cast<unsigned>(stage.color), static_cast<unsigned>(stage.colorArg1),
                        static_cast<unsigned>(stage.colorArg2), static_cast<unsigned>(stage.alpha),
                        static_cast<unsigned>(stage.alphaArg1),
                        static_cast<unsigned>(stage.alphaArg2), s.srgbSample[i]);
                    values("textureTransform", s.textureTransforms[i]);
                }
            for (unsigned i = 0; i < 8; ++i)
                if (s.lights[i].enabled) {
                    std::fprintf(g_report, "LIGHT sequence=%u index=%u enabled=1\n", draw.sequence,
                                 i);
                    values("lightDirection", s.lights[i].direction);
                    values("lightDiffuse", s.lights[i].diffuse);
                    values("lightAmbient", s.lights[i].ambient);
                    values("lightSpecular", s.lights[i].specular);
                }
        }
        std::fprintf(g_report, "NATIVE sequence=%u toon=%u expectedToon=%u", draw.sequence,
                     material.toonReference, expectedToon);
        for (auto key :
             {D3DRS_BLENDOP, D3DRS_SEPARATEALPHABLENDENABLE, D3DRS_SRCBLENDALPHA,
              D3DRS_DESTBLENDALPHA, D3DRS_BLENDOPALPHA, D3DRS_ALPHAFUNC, D3DRS_ALPHAREF,
              D3DRS_STENCILENABLE, D3DRS_STENCILFUNC, D3DRS_STENCILREF, D3DRS_STENCILMASK,
              D3DRS_STENCILWRITEMASK, D3DRS_STENCILPASS, D3DRS_STENCILFAIL, D3DRS_STENCILZFAIL,
              D3DRS_COLORWRITEENABLE, D3DRS_SRGBWRITEENABLE})
            std::fprintf(g_report, " rs%u=%u", static_cast<unsigned>(key), State(device, key));
        std::fputc('\n', g_report);
        // These compatibility tokens describe the borrowed DX9 cascade;
        // future shader backends translate them rather than passing them on.
        for (unsigned stage = 0; stage < 3; ++stage) {
            std::fprintf(g_report, "STAGE sequence=%u stage=%u", draw.sequence, stage);
            for (auto key : {D3DTSS_COLOROP, D3DTSS_COLORARG1, D3DTSS_COLORARG2, D3DTSS_ALPHAOP,
                             D3DTSS_ALPHAARG1, D3DTSS_ALPHAARG2, D3DTSS_TEXCOORDINDEX,
                             D3DTSS_TEXTURETRANSFORMFLAGS}) {
                DWORD value = 0;
                device->GetTextureStageState(stage, key, &value);
                std::fprintf(g_report, " tss%u=%u", static_cast<unsigned>(key), value);
            }
            IDirect3DBaseTexture9* texture = nullptr;
            const bool bound = SUCCEEDED(device->GetTexture(stage, &texture)) && texture;
            Release(texture);
            for (auto key :
                 {D3DSAMP_ADDRESSU, D3DSAMP_ADDRESSV, D3DSAMP_MINFILTER, D3DSAMP_MAGFILTER,
                  D3DSAMP_MIPFILTER, D3DSAMP_MAXANISOTROPY, D3DSAMP_SRGBTEXTURE}) {
                DWORD value = 0;
                device->GetSamplerState(stage, key, &value);
                std::fprintf(g_report, " sampler%u=%u", static_cast<unsigned>(key), value);
            }
            std::fprintf(g_report, " bound=%u\n", bound);
        }
    }
    char selected[16]{};
    const auto selectedLength = GetEnvironmentVariableA("MDS_RHI_MODEL_MATERIAL", selected, 16);
    if (selectedLength >= 16)
        return false;
    if (selectedLength && selectedLength < 16) {
        char* end = nullptr;
        const auto number = std::strtoul(selected, &end, 10);
        if (!end || *end || number != materialIndex)
            return false;
    }
    if (!eligible || (!g_report && !g_snapshotPassed)) {
        if (Requested("MDS_RHI_CPU_PREVIEW"))
            BlockCpuModelPreview(app, model, materialIndex,
                                 batch.dependency != render::BatchDependency::Independent
                                     ? "dependent_model_batch"
                                 : !noEffects               ? "effects_active"
                                 : !fixed                   ? "custom_shader_active"
                                 : stride != 32             ? "unsupported_vertex_stride"
                                 : !same                    ? "model_snapshot_mismatch"
                                 : state.displayState != 0  ? "model_display_override"
                                 : state.postLoadFlag2 != 0 ? "postload_override"
                                                            : "snapshot_not_verified");
        return false;
    }
    if (Requested("MDS_RHI_MODEL_TOON") &&
        !LoadToon(app, model, snapshot->slot, materialIndex, batch.toon,
                  resource.kind == format::ModelKind::Pmd)) {
        if (Requested("MDS_RHI_CPU_PREVIEW"))
            BlockCpuModelPreview(app, model, materialIndex, "toon_resource_unavailable");
        g_gatePassed = false;
        return false;
    }
    if (Requested("MDS_RHI_MODEL_TEXTURES") &&
        !LoadMaterialInputs(app, model, snapshot->slot, materialIndex, batch,
                            resource.kind == format::ModelKind::Pmd, false)) {
        if (Requested("MDS_RHI_CPU_PREVIEW"))
            BlockCpuModelPreview(app, model, materialIndex, "material_resource_unavailable");
        g_gatePassed = false;
        return false;
    }
    if (Requested("MDS_RHI_CPU_PREVIEW")) {
        // The live geometry route precedes MeshBytes: it never obtains its
        // vertex/index data by reading a native DX9 buffer.
        rhi::ModelPipeline pipeline;
        pipeline.raster = raster;
        D3DVIEWPORT9 viewport{};
        const char* unsupported = CaptureModelRaster(device, pipeline);
        const bool captured =
            !unsupported && SUCCEEDED(device->GetViewport(&viewport)) &&
            CaptureShaderInputs(device, batch, resource.kind == format::ModelKind::Pmd,
                                pipeline.shader);
        if (captured) {
            auto assets = g_materialAssets;
            assets[0] = g_toon;
            rhi::HistoryDesc target;
            target.width = viewport.X + viewport.Width;
            target.height = viewport.Y + viewport.Height;
            pipeline.viewport = rhi::ModelViewport{viewport.X,      viewport.Y,    viewport.Width,
                                                   viewport.Height, viewport.MinZ, viewport.MaxZ};
            SubmitCpuModelPreview(app, model, materialIndex, first, count, target, pipeline,
                                  assets);
        } else
            BlockCpuModelPreview(app, model, materialIndex,
                                 unsupported ? unsupported : "shader_input_capture_failed");
    }
    auto* vb = mdl::ResourceAs<IDirect3DVertexBuffer9>(state.vertexBuffer);
    auto* ib = mdl::ResourceAs<IDirect3DIndexBuffer9>(state.indexBuffer);
    std::vector<std::uint8_t> vertices, indices;
    if (!MeshBytes(vb, ib, state.vertexCount, state.indexCount, vertices, indices))
        return false;
    if (!g_gatePassed && !g_comparison.active)
        Probe(device, snapshot->slot, materialIndex, first, count, state.vertexCount, vertices,
              indices, raster);
    const bool use = g_gatePassed && g_snapshotPassed && Requested("MDS_RHI_MODEL_ADAPTER") &&
                     device == g_gateDevice && snapshot->slot == g_gateSlot &&
                     materialIndex == g_gateMaterial;
    if (!use)
        return false;
    if (!g_comparison.active)
        Probe(device, snapshot->slot, materialIndex, first, count, state.vertexCount, vertices,
              indices, raster);
    long result = E_FAIL;
    const auto beforeBridge = mme::ProbeIndexedDrawBridge();
    bool issued = false;
    const bool applied = AdapterDraw(
        device, vertices, indices, first, count, raster,
        [](IDirect3DDevice9* d, unsigned v, unsigned f, unsigned p) {
            return mme::DrawIndexedPrimitive(d, D3DPT_TRIANGLELIST, 0, 0, v, f, p);
        },
        &result, false, &issued);
    if (g_comparison.active && g_comparison.slot == snapshot->slot &&
        g_comparison.material == materialIndex) {
        g_comparison.actualApplied = applied;
        g_comparison.actualAttempted = true;
        g_comparison.actualHr = result;
        const auto afterBridge = mme::ProbeIndexedDrawBridge();
        g_comparison.bridgeCalls = afterBridge.calls - beforeBridge.calls;
        g_comparison.hostCalls = afterBridge.hostCalls - beforeBridge.hostCalls;
        g_comparison.directCalls = afterBridge.directCalls - beforeBridge.directCalls;
        if (applied)
            g_comparison.actualHr = device->StretchRect(
                g_comparison.original, nullptr, g_comparison.actualResolved, nullptr, D3DTEXF_NONE);
    }
    if (!applied)
        g_gatePassed = false;
    // Once the callback ran, never submit the legacy draw a second time,
    // even if restoring device state failed. Revoke the gate instead.
    return issued;
}

void CaptureModelHistoryResult(MMDApp* app, mdl::ModelRecord* model, unsigned material) {
    CompleteHistoryReplay(app->Renderer()->device, material);
    const auto& c = g_comparison;
    if (!Requested("MDS_RHI_SCENE_HISTORY") || !c.active || c.material != material ||
        app->ModelSlot(c.slot) != model || c.actualAttempted)
        return;
    auto* device = app->Renderer()->device;
    IDirect3DSurface9* system = nullptr;
    std::vector<DWORD> pixels;
    const bool ready =
        SUCCEEDED(device->CreateOffscreenPlainSurface(c.desc.Width, c.desc.Height, c.desc.Format,
                                                      D3DPOOL_SYSTEMMEM, &system, nullptr)) &&
        Pixels(device, c.original, system, c.desc, pixels);
    Release(system);
    if (!ready)
        return;
    std::ofstream reference(c.directory + "/history_live.rgba", std::ios::binary | std::ios::trunc);
    for (unsigned y = 0; y < c.viewport.Height; ++y)
        for (unsigned x = 0; x < c.viewport.Width; ++x) {
            const DWORD pixel = pixels[(y + c.viewport.Y) * c.desc.Width + x + c.viewport.X];
            const unsigned char rgba[] = {
                static_cast<unsigned char>(pixel >> 16), static_cast<unsigned char>(pixel >> 8),
                static_cast<unsigned char>(pixel),
                c.desc.Format == D3DFMT_X8R8G8B8 ? static_cast<unsigned char>(255)
                                                 : static_cast<unsigned char>(pixel >> 24)};
            reference.write(reinterpret_cast<const char*>(rgba), 4);
        }
}
void ReportModelBatchComparison(IDirect3DDevice9* device) {
    auto& c = g_comparison;
    if (!c.active)
        return;
    IDirect3DSurface9* system = nullptr;
    std::vector<DWORD> baseline, legacy, adapter, actual;
    const bool ready =
        SUCCEEDED(c.hr) && (c.desc.Format == D3DFMT_A8R8G8B8 || c.desc.Format == D3DFMT_X8R8G8B8) &&
        SUCCEEDED(device->CreateOffscreenPlainSurface(c.desc.Width, c.desc.Height, c.desc.Format,
                                                      D3DPOOL_SYSTEMMEM, &system, nullptr)) &&
        Pixels(device, c.baseline, system, c.desc, baseline) &&
        Pixels(device, c.legacyResolved, system, c.desc, legacy) &&
        Pixels(device, c.adapterResolved, system, c.desc, adapter);
    const bool actualReady = ready && c.actualApplied && SUCCEEDED(c.actualHr) &&
                             Pixels(device, c.actualResolved, system, c.desc, actual);
    Release(system);
    std::size_t differences = 0, changed = 0, actualDifferences = 0;
    if (ready)
        for (std::size_t i = 0; i < legacy.size(); ++i) {
            differences += legacy[i] != adapter[i];
            changed += legacy[i] != baseline[i];
            if (actualReady)
                actualDifferences += actual[i] != legacy[i];
        }
    const bool pass = ready && c.applied && !differences && changed && g_snapshotPassed &&
                      (!c.actualAttempted ||
                       c.actualApplied && actualReady && !actualDifferences && c.bridgeCalls == 1);
    if (ready && Requested("MDS_RHI_SCENE_HISTORY")) {
        std::ofstream reference(c.directory + "/history_probe.rgba",
                                std::ios::binary | std::ios::trunc);
        for (unsigned y = 0; y < c.viewport.Height; ++y)
            for (unsigned x = 0; x < c.viewport.Width; ++x) {
                const DWORD pixel = legacy[(y + c.viewport.Y) * c.desc.Width + x + c.viewport.X];
                const unsigned char rgba[] = {
                    static_cast<unsigned char>(pixel >> 16), static_cast<unsigned char>(pixel >> 8),
                    static_cast<unsigned char>(pixel),
                    c.desc.Format == D3DFMT_X8R8G8B8 ? static_cast<unsigned char>(255)
                                                     : static_cast<unsigned char>(pixel >> 24)};
                reference.write(reinterpret_cast<const char*>(rgba), 4);
            }
    }
    g_gatePassed = pass;
    if (pass) {
        g_gateSlot = c.slot;
        g_gateMaterial = c.material;
        g_gateDevice = device;
    }
    const bool optin = Requested("MDS_RHI_MODEL_ADAPTER");
    // Opt-in is reported only after the previously validated batch actually
    // traverses the adapter in a following frame.
    if (!optin || actualReady || !pass) {
        FILE* file = std::fopen((c.directory + "\\model_batch.same_frame").c_str(), "wb");
        if (file) {
            std::fprintf(
                file,
                "status=%s slot=%u material=%u frame=%u first=%u count=%u pixels=%zu "
                "differences=%zu changed=%zu actualAdapterApplied=%u actualDifferences=%zu "
                "probeHr=%08lx multisample=%u viewport=%u,%u,%u,%u bridgeCalls=%lu hostCalls=%lu directCalls=%lu "
                "readOnlyDepthStencil=1 ownedToon=%u ownedBaseSphere=%u,%u\n",
                pass && (!optin || actualReady && !actualDifferences) ? "PASS" : "FAIL", c.slot,
                c.material, c.frame, c.first, c.count, legacy.size(), differences, changed,
                c.actualApplied, actualDifferences, static_cast<unsigned long>(c.hr),
                static_cast<unsigned>(c.desc.MultiSampleType), c.viewport.X, c.viewport.Y,
                c.viewport.Width, c.viewport.Height, c.bridgeCalls, c.hostCalls, c.directCalls,
                Requested("MDS_RHI_MODEL_TOON"), g_materialOwned[1], g_materialOwned[2]);
            std::fclose(file);
        }
    }
    if (g_toonCache) {
        const auto stats = g_toonCache->Stats();
        if (auto* file = std::fopen((c.directory + "\\model_toon.cache").c_str(), "wb")) {
            std::fprintf(file, "uploads=%llu hits=%llu\n",
                         static_cast<unsigned long long>(stats.uploads),
                         static_cast<unsigned long long>(stats.hits));
            std::fclose(file);
        }
    }
    c.Clear();
}
void InvalidateModelBatchComparison() {
    // This invalidates DX9 resources only. Independent DX11/DX12 devices and
    // CPU-owned scene resources survive a DX9 Reset.
    g_comparison.Clear();
    if (g_report) {
        std::fclose(g_report);
        g_report = nullptr;
    }
    g_gatePassed = g_snapshotSaved = g_snapshotPassed = g_frameReady = false;
    g_gateDevice = nullptr;
    g_gateSlot = -1;
    g_snapshot = {};
    g_toonCache.reset();
    g_toon = {};
    g_toonDevice = nullptr;
    g_toonSlot = -1;
    if (g_sources) {
        std::fclose(g_sources);
        g_sources = nullptr;
    }
    g_materialAssets = {};
    g_materialOwned = {};
    g_materialReady = false;
}
}
#endif
