#define NOMINMAX
#include <Windows.h>
#include <d3dx9.h>
#include <d3dcompiler.h>
#include <wrl/client.h>
#include "mikudancestudio/runtime_cpu_scene_producer.hpp"
#include "mikudancestudio/mme_portable.hpp"
#include "mikudancestudio/rhi_backend.hpp"
#include "mikudancestudio/rhi_scene_renderer.hpp"
#include "mikudancestudio/rhi_shared_targets.hpp"
#include "mikudancestudio/rhi_post_process.hpp"
#include "mikudancestudio/rhi_float.hpp"
#include "mikudancestudio/rhi_material_sampling.hpp"
#include "mikudancestudio/rhi_d3d12.hpp"
#include "mikudancestudio/rhi_vulkan.hpp"
#include "mme_legacy_api.h"
#include "mme_portable_api.h"
#include "effect_engine.h"
#include "mme_context.h"
#include "mme_globals.h"
#include "sas_exec.h"
#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <cwchar>
#include <map>
#include <fstream>
#include <stdexcept>
using namespace mikudancestudio;
using namespace mikudancestudio::rhi;
using Microsoft::WRL::ComPtr;
#define CHECK(x)                                                                                   \
    do {                                                                                           \
        if (!(x))                                                                                  \
            throw std::runtime_error(std::to_string(__LINE__) + ": " #x);                          \
    } while (false)
namespace {
unsigned blockedDialogs = 0;
LRESULT CALLBACK BlockDialog(int code, WPARAM window, LPARAM data) {
    wchar_t name[32]{};
    if (code == HCBT_CREATEWND && GetClassNameW(reinterpret_cast<HWND>(window), name, 32) &&
        std::wcscmp(name, L"#32770") == 0) {
        ++blockedDialogs;
        std::fputs("Unexpected native dialog blocked before creation\n", stderr);
        return 1;
    }
    return CallNextHookEx(nullptr, code, window, data);
}
struct DialogGuard {
    HHOOK hook = SetWindowsHookExW(WH_CBT, BlockDialog, nullptr, GetCurrentThreadId());
    ~DialogGuard() {
        if (hook)
            UnhookWindowsHookEx(hook);
    }
};
void CheckHr(HRESULT value, unsigned line) {
    if (FAILED(value)) {
        char text[96];
        std::snprintf(text, sizeof(text), "line %u HRESULT=0x%08lX", line,
                      static_cast<unsigned long>(value));
        throw std::runtime_error(text);
    }
}
#define Hr(value) CheckHr(value, __LINE__)
struct Window {
    HWND handle = CreateWindowExW(WS_EX_NOACTIVATE, L"STATIC", L"Hidden material script oracle",
                                  WS_OVERLAPPED, 0, 0, 128, 128, nullptr, nullptr,
                                  GetModuleHandleW(nullptr), nullptr);
    ~Window() {
        if (handle)
            DestroyWindow(handle);
    }
};
struct Runtime {
    Runtime(IDirect3DDevice9* device) {
        mme::g_context = new mme::MmeContext(device);
        mme::MmeEngineInit(device, true);
    }
    ~Runtime() {
        mme::SasSetHostCallbacks({});
        mme::MmeEngineTerm();
        if (mme::g_effectPool) {
            mme::g_effectPool->Release();
            mme::g_effectPool = nullptr;
        }
        if (mme::g_offscreenSurface) {
            mme::g_offscreenSurface->Release();
            mme::g_offscreenSurface = nullptr;
        }
        delete mme::g_context;
        mme::g_context = nullptr;
    }
};
struct NativeResource {
    ComPtr<IDirect3DVertexBuffer9> vertices;
    ComPtr<IDirect3DIndexBuffer9> indices;
    ComPtr<IDirect3DTexture9> texture;
};
std::map<ReplayResourceKey, NativeResource> Upload(IDirect3DDevice9* device,
                                                   const RenderFrame& frame) {
    std::map<ReplayResourceKey, NativeResource> resources;
    for (const auto& source : frame.resources) {
        NativeResource r;
        void* mapped = nullptr;
        if (source.kind == ReplayResourceKind::Texture) {
            const auto& levels = source.texture.levels;
            Hr(device->CreateTexture(levels[0].width, levels[0].height, UINT(levels.size()), 0,
                                     D3DFMT_A8R8G8B8, D3DPOOL_MANAGED, &r.texture, nullptr));
            for (unsigned n = 0; n < levels.size(); ++n) {
                D3DLOCKED_RECT lock{};
                Hr(r.texture->LockRect(n, &lock, nullptr, 0));
                const auto& image = levels[n];
                for (unsigned y = 0; y < image.height; ++y)
                    for (unsigned x = 0; x < image.width; ++x) {
                        auto* p = static_cast<unsigned char*>(lock.pBits) + y * lock.Pitch + x * 4;
                        const auto* q = image.pixels.data() + (y * image.width + x) * 4;
                        p[0] = q[2];
                        p[1] = q[1];
                        p[2] = q[0];
                        p[3] = q[3];
                    }
                Hr(r.texture->UnlockRect(n));
            }
        } else if (source.kind == ReplayResourceKind::Vertices) {
            Hr(device->CreateVertexBuffer(UINT(source.bytes.size()), 0, 0, D3DPOOL_MANAGED,
                                          &r.vertices, nullptr));
            Hr(r.vertices->Lock(0, 0, &mapped, 0));
            std::memcpy(mapped, source.bytes.data(), source.bytes.size());
            Hr(r.vertices->Unlock());
        } else {
            Hr(device->CreateIndexBuffer(UINT(source.bytes.size()), 0,
                                         source.indexFormat == IndexFormat::UInt16 ? D3DFMT_INDEX16
                                                                                   : D3DFMT_INDEX32,
                                         D3DPOOL_MANAGED, &r.indices, nullptr));
            Hr(r.indices->Lock(0, 0, &mapped, 0));
            std::memcpy(mapped, source.bytes.data(), source.bytes.size());
            Hr(r.indices->Unlock());
        }
        resources.emplace(source.key, std::move(r));
    }
    return resources;
}
void NativeValues(ID3DXEffect* fx, const ModelReplayDraw& draw,
                  const std::map<ReplayResourceKey, NativeResource>& resources) {
    const auto& s = draw.pipeline.shader;
    D3DXMATRIX identity, view, wvp, world, projection;
    D3DXMatrixIdentity(&identity);
    std::memcpy(&world, s.world.data(), 64);
    std::memcpy(&view, s.view.data(), 64);
    std::memcpy(&projection, s.projection.data(), 64);
    D3DXMatrixMultiply(&view, &world, &view);
    D3DXMatrixMultiply(&wvp, &view, &projection);
    for (auto item : {std::pair<const char*, const D3DXMATRIX*>{"matWorld", &identity},
                      {"matView", &view},
                      {"matWorldView", &view},
                      {"matWorldViewProject", &wvp},
                      {"wvp", &wvp}})
        if (auto h = fx->GetParameterByName(nullptr, item.first))
            Hr(fx->SetMatrix(h, item.second));
    for (auto item :
         {std::pair<const char*, const float*>{"MaterialDiffuse", s.nativeDiffuse.data()},
          {"MaterialAmbient", s.emissive.data()},
          {"MaterialSpecular", s.materialSpecular.data()}})
        if (auto h = fx->GetParameterByName(nullptr, item.first))
            Hr(fx->SetFloatArray(h, item.second, 4));
    if (auto h = fx->GetParameterByName(nullptr, "MaterialPower"))
        Hr(fx->SetFloat(h, s.shininess));
    if (auto h = fx->GetParameterByName(nullptr, "time"))
        Hr(fx->SetFloat(h, 0));
    for (const auto* name : {"use_texture", "use_subtexture", "use_spheremap", "use_toon"})
        if (auto h = fx->GetParameterByName(nullptr, name))
            Hr(fx->SetBool(h, std::string(name) == "use_texture"));
    for (unsigned stage = 0; stage < 3; ++stage)
        if (s.layout.stages[stage].role == MaterialTextureRole::Base)
            Hr(fx->SetTexture("DiffuseMap", resources.at(draw.textures[stage]).texture.Get()));
}
MmePortableEffect Describe(mme::LoadedEffect& loaded, mme::SasEffect& publisher,
                           unsigned technique) {
    MmePortableEffect effect;
    effect.source = loaded.originalSource;
    effect.legacySource = loaded.legacySource;
    effect.clockReady = true;
    effect.profile = "legacy-material-v1";
    auto program = std::make_shared<MmeLegacyProgram>();
    std::string reason;
    CHECK(effect.legacySource && MmeLegacyImport(*effect.legacySource, *program, reason));
    effect.legacy = program;
    for (const auto& v : program->variables) {
        MmePortableParameter p;
        p.name = v.name;
        p.semantic = v.semantic;
        p.rows = v.rows;
        p.columns = v.columns;
        p.elements = v.elements;
        const auto count = p.rows * p.columns * p.elements;
        p.value.resize(count * 4);
        if (v.type == "bool") {
            p.type = MmePortableParameter::Type::Bool;
            std::vector<BOOL> values(count);
            Hr(loaded.effect->GetBoolArray(v.name.c_str(), values.data(), count));
            std::memcpy(p.value.data(), values.data(), p.value.size());
        } else {
            std::vector<float> values(count);
            Hr(loaded.effect->GetFloatArray(v.name.c_str(), values.data(), count));
            std::memcpy(p.value.data(), values.data(), p.value.size());
        }
        effect.parameters.push_back(std::move(p));
    }
    for (const auto& d : program->textures)
        if (d.shared) {
            const auto r = std::find_if(publisher.resources.begin(), publisher.resources.end(),
                                        [&](const auto& r) { return r.name == d.name; });
            CHECK(r != publisher.resources.end());
            MmePortableTarget target{d.name,
                                     r->format == D3DFMT_A16B16G16R16F
                                         ? MmePortableFormat::Rgba16Float
                                         : MmePortableFormat::Rgba8,
                                     false, r->portableIdentity};
            target.relative = r->hasViewportRatio;
            target.viewportRatio = {r->viewportRatio[0], r->viewportRatio[1]};
            effect.targets.push_back(target);
        }
    for (const auto& p : program->passes)
        if (p.technique == loaded.sas->techniques.at(technique).name) {
            MmePortablePass pass{p.technique, p.name, p.vertex, p.pixel};
            for (auto slot : MmeLegacySamplers(*program, p)) {
                pass.sampleSlots.push_back(slot);
                pass.sampled.push_back(program->samplers[slot].texture);
            }
            effect.passes.push_back(std::move(pass));
        }
    CHECK(MmePortableExportMaterial(*loaded.sas, technique, effect, reason));
    return effect;
}
std::vector<std::uint8_t> Read(IDirect3DDevice9* device, IDirect3DSurface9* surface) {
    D3DSURFACE_DESC d{};
    Hr(surface->GetDesc(&d));
    const unsigned stride = d.Format == D3DFMT_A16B16G16R16F ? 8 : 4;
    ComPtr<IDirect3DSurface9> cpu;
    Hr(device->CreateOffscreenPlainSurface(d.Width, d.Height, d.Format, D3DPOOL_SYSTEMMEM, &cpu,
                                           nullptr));
    Hr(device->GetRenderTargetData(surface, cpu.Get()));
    D3DLOCKED_RECT lock{};
    Hr(cpu->LockRect(&lock, nullptr, D3DLOCK_READONLY));
    std::vector<std::uint8_t> bytes(d.Width * d.Height * stride);
    for (unsigned y = 0; y < d.Height; ++y)
        for (unsigned x = 0; x < d.Width; ++x) {
            auto* q = bytes.data() + (y * d.Width + x) * stride;
            const auto* p =
                static_cast<const std::uint8_t*>(lock.pBits) + y * lock.Pitch + x * stride;
            if (stride == 8)
                std::memcpy(q, p, 8);
            else {
                q[0] = p[2];
                q[1] = p[1];
                q[2] = p[0];
                q[3] = p[3];
            }
        }
    Hr(cpu->UnlockRect());
    return bytes;
}
void Compare(const AttachmentReadback& image, const std::vector<std::uint8_t>& reference,
             const char* label) {
    CHECK(image.bytes.size() == reference.size());
    float maximum = 0;
    if (image.description.format == TextureFormat::Rgba16Float) {
        for (std::size_t i = 0; i < reference.size(); i += 2) {
            std::uint16_t a, b;
            std::memcpy(&a, image.bytes.data() + i, 2);
            std::memcpy(&b, reference.data() + i, 2);
            const float va = HalfToFloat(a), vb = HalfToFloat(b);
            CHECK(std::isfinite(va) && std::isfinite(vb));
            const float ulp = std::ldexp(
                1.f, std::max(-24, std::ilogb(std::max(std::abs(vb), std::ldexp(1.f, -24))) - 10));
            maximum = std::max(maximum, std::abs(va - vb) / ulp);
        }
        std::printf("%s half ULP=%g limit=1\n", label, maximum);
        CHECK(maximum <= 1);
    } else {
        for (std::size_t i = 0; i < reference.size(); ++i)
            maximum = std::max(maximum, float(std::abs(int(image.bytes[i]) - int(reference[i]))));
        std::printf("%s RGBA delta=%g limit=2\n", label, maximum);
        if (maximum > 2) {
            unsigned shown = 0;
            for (std::size_t i = 0; i < reference.size() && shown < 8; i += 4) {
                unsigned pixelDelta = 0;
                for (unsigned c = 0; c < 4; ++c)
                    pixelDelta = std::max(pixelDelta, unsigned(std::abs(int(image.bytes[i + c]) -
                                                                        int(reference[i + c]))));
                if (pixelDelta > 2) {
                    std::printf("pixel %zu,%zu RHI=%u,%u,%u,%u reference=%u,%u,%u,%u\n",
                                (i / 4) % image.description.width,
                                (i / 4) / image.description.width, image.bytes[i],
                                image.bytes[i + 1], image.bytes[i + 2], image.bytes[i + 3],
                                reference[i], reference[i + 1], reference[i + 2], reference[i + 3]);
                    ++shown;
                }
            }
        }
        CHECK(maximum <= 2);
    }
}
void PublicMaterialCoverage(const AttachmentReadback& primary,
                            const AttachmentReadback& secondary) {
    CHECK(primary.description.format == TextureFormat::Rgba8 &&
          secondary.description.format == TextureFormat::Rgba8 &&
          primary.description.width == secondary.description.width &&
          primary.description.height == secondary.description.height &&
          primary.bytes.size() == secondary.bytes.size());
    unsigned left = 0, right = 0;
    for (std::size_t n = 3; n < primary.bytes.size(); n += 4) {
        const bool a = primary.bytes[n] > 127, b = secondary.bytes[n] > 127;
        CHECK(!(a && b));
        left += a;
        right += b;
    }
    CHECK(left > 20 && right > 20);
    std::printf("public clip coverage: primary=%u secondary=%u overlap=0\n", left, right);
}
void Diagnostics(Device& device) {
#ifdef MIKUDANCESTUDIO_RHI_D3D12
    if (device.BackendType() == Backend::D3D12)
        CHECK(QueryD3D12Stats(device).debugErrors == 0);
#endif
#ifdef MIKUDANCESTUDIO_RHI_VULKAN
    if (device.BackendType() == Backend::Vulkan)
        CHECK(QueryVulkanStats(device).validationErrors == 0);
#endif
}
MmePortableEffect Publisher(const MmePortableEffect& material) {
    const auto path = std::filesystem::path(MDS_TEST_SOURCE_DIR) /
                      L"assets/effects/portable/LegacyMaterialMapScene.fx";
    std::ifstream file(path);
    auto source = std::make_shared<const std::string>(std::istreambuf_iterator<char>(file),
                                                      std::istreambuf_iterator<char>());
    auto imported = std::make_shared<MmeLegacyProgram>();
    std::string reason;
    CHECK(MmeLegacyImportScene(*source, *imported, reason));
    MmePortableEffect scene;
    scene.source = source;
    scene.legacySource = source;
    scene.legacy = imported;
    scene.post = true;
    scene.profile = "legacy-scene-v1";
    scene.targets = material.targets;
    for (const auto* name : {"Capture", "Depth"}) {
        MmePortableTarget target{name, MmePortableFormat::Rgba8, std::string(name) == "Depth",
                                 std::string(name) == "Depth" ? 201u : 200u};
        target.relative = true;
        target.viewportRatio = {1, 1};
        scene.targets.push_back(target);
    }
    for (const auto& v : imported->variables) {
        MmePortableParameter p;
        p.name = v.name;
        p.rows = v.rows;
        p.columns = v.columns;
        p.elements = v.elements;
        p.value.resize(v.rows * v.columns * v.elements * 4);
        if (v.name == "One") {
            const float one = 1;
            std::memcpy(p.value.data(), &one, 4);
        }
        scene.parameters.push_back(std::move(p));
    }
    for (const auto& p : imported->passes) {
        MmePortablePass pass{p.technique, p.name, p.vertex, p.pixel};
        for (auto slot : MmeLegacySamplers(*imported, p)) {
            pass.sampleSlots.push_back(slot);
            pass.sampled.push_back(imported->samplers[slot].texture);
        }
        scene.passes.push_back(std::move(pass));
    }
    using K = MmePortableCommand::Kind;
    // Independent CPU description of the public native script. Hidden editor
    // and DX9 oracle gates use the production SAS exporter instead.
    scene.script = {{K::ClearColorValue, "Black"},
                    {K::ClearDepthValue, "One"},
                    {K::ColorTarget, "Gbuffer2RT", 0},
                    {K::ColorTarget, "Gbuffer3RT", 1},
                    {K::ColorTarget, "Gbuffer4RT", 2},
                    {K::Clear, "", 1},
                    {K::ColorTarget, "Gbuffer5RT", 0},
                    {K::ColorTarget, "Gbuffer6RT", 1},
                    {K::ColorTarget, "Gbuffer7RT", 2},
                    {K::ColorTarget, "Gbuffer8RT", 3},
                    {K::Clear, "", 1},
                    {K::ColorTarget, "", 3},
                    {K::ColorTarget, "", 2},
                    {K::ColorTarget, "", 1},
                    {K::ColorTarget, "Capture", 0},
                    {K::DepthTarget, "Depth"},
                    {K::Clear, "", 1},
                    {K::Clear, "", 6},
                    {K::Capture},
                    {K::ColorTarget, "", 0},
                    {K::DepthTarget, ""},
                    {K::Clear, "", 1},
                    {K::Pass, "", 0}};
    if (!MmePortableSceneLayout(scene, reason))
        throw std::runtime_error(reason);
    return scene;
}
// Independent of DX9 availability: exercise the real CPU producer, source
// importer, material lowering and Null dependency/binding validator.
int Contract(const std::filesystem::path& model, const std::filesystem::path& material,
             const std::string& api, unsigned width = 128, float opacity = 1) {
    const auto root = std::filesystem::path(MDS_TEST_SOURCE_DIR);
    std::ifstream file(material, std::ios::binary);
    const std::string source{std::istreambuf_iterator<char>(file), {}};
    CHECK(!source.empty());
    const D3D_SHADER_MACRO macros[] = {
        {"_INDEX", "PSIZE15"}, {"MME_MIPMAP", ""}, {nullptr, nullptr}};
    ComPtr<ID3DBlob> expanded, errors;
    Hr(D3DPreprocess(source.data(), source.size(), material.string().c_str(), macros,
                     D3D_COMPILE_STANDARD_FILE_INCLUDE, &expanded, &errors));
    auto text = std::make_shared<const std::string>(
        static_cast<const char*>(expanded->GetBufferPointer()), expanded->GetBufferSize());
    auto program = std::make_shared<MmeLegacyProgram>();
    std::string reason;
    CHECK(MmeLegacyImport(*text, *program, reason));
    MmePortableEffect effect;
    effect.source = text;
    effect.legacySource = text;
    effect.legacy = program;
    effect.profile = "legacy-material-v1";
    effect.clockReady = true;
    for (const auto& v : program->variables) {
        MmePortableParameter p;
        p.name = v.name;
        p.semantic = v.semantic;
        p.rows = v.rows;
        p.columns = v.columns;
        p.elements = v.elements;
        if (v.type == "bool")
            p.type = MmePortableParameter::Type::Bool;
        p.value.resize(p.rows * p.columns * p.elements * 4);
        effect.parameters.push_back(std::move(p));
    }
    unsigned identity = 100;
    for (const auto& t : program->textures)
        if (t.shared) {
            const bool half = t.name == "Gbuffer4RT" || t.name == "Gbuffer8RT";
            MmePortableTarget target{
                t.name, half ? MmePortableFormat::Rgba16Float : MmePortableFormat::Rgba8, false,
                identity++};
            target.relative = true;
            target.viewportRatio = {1, 1};
            effect.targets.push_back(target);
        }
    const auto technique = program->passes.front().technique;
    for (const auto& p : program->passes)
        if (p.technique == technique) {
            MmePortablePass pass{p.technique, p.name, p.vertex, p.pixel};
            for (auto slot : MmeLegacySamplers(*program, p)) {
                pass.sampleSlots.push_back(slot);
                pass.sampled.push_back(program->samplers[slot].texture);
            }
            effect.passes.push_back(std::move(pass));
        }
    // The native-SAS exporter is covered by the oracle path; this branch
    // independently supplies the two MRT groups expected from that script.
    using K = MmePortableCommand::Kind;
    effect.script = {{K::ColorTarget, "", 0},
                     {K::ColorTarget, "Gbuffer2RT", 1},
                     {K::ColorTarget, "Gbuffer3RT", 2},
                     {K::ColorTarget, "Gbuffer4RT", 3},
                     {K::Pass, "", 0},
                     {K::ColorTarget, "Gbuffer5RT", 0},
                     {K::ColorTarget, "Gbuffer6RT", 1},
                     {K::ColorTarget, "Gbuffer7RT", 2},
                     {K::ColorTarget, "Gbuffer8RT", 3},
                     {K::Pass, "", 1}};
    CHECK(MmePortableMaterialLayout(effect, reason));
    FileAssetSource assets(root / L"res/assets");
    ToonTextureResolver textures(model.parent_path(), assets);
    RuntimeCpuScene scene;
    CHECK(scene.LoadModel(model, reason));
    for (auto slot : scene.Order())
        if (!scene.Model(slot)->PrepareMaterials(textures, reason))
            throw std::runtime_error("CPU material preparation: " + reason);
    format::SceneResourceResolver resolver(model);
    RuntimeCpuSceneProducer producer;
    CHECK(producer.Load(scene, resolver, false, assets, reason));
    render::ModelFrameInputs inputs;
    inputs.view.viewport = {0, 0, width, 96};
    inputs.view.aspect = float(width) / 96;
    HistoryDesc extent;
    extent.width = width;
    extent.height = 96;
    RenderFrame base;
    CHECK(producer.BuildFrame(scene, inputs, extent, {}, base, reason));
    auto draw = base.passes.at(0).draws.at(0);
    // Isolated material host: the native oracle also explicitly disables
    // stencil, which the generic material pipeline does not implement.
    draw.pipeline.stencil = false;
    draw.pipeline.shader.nativeDiffuse[3] = opacity;
    {
        auto single = effect;
        single.steps.clear();
        single.passes.resize(1);
        auto incompatible = draw;
        incompatible.pipeline.colorWriteMask = 1;
        CHECK(!ApplyPortableMmeMaterial(incompatible, single, reason) &&
              reason == "MME_legacy_inherited_material_state");
        if (!program->passes.front().states.count("STENCILENABLE")) {
            incompatible = draw;
            incompatible.pipeline.stencil = true;
            CHECK(!ApplyPortableMmeMaterial(incompatible, single, reason) &&
                  reason == "MME_legacy_inherited_material_state");
        }
    }
    auto host = base.passes[0];
    host.draws.clear();
    base.passes.clear();
    CHECK(RegisterPortableMmeTargets(base, {effect}, reason));
    const auto assemble = [&](bool initialize) {
        auto frame = base;
        auto pending = host;
        if (initialize)
            for (const auto& target : effect.targets) {
                FramePass clear;
                clear.color = FrameAttachment{SharedFrameTargetId(target.identity)};
                // Shared attachments use the publisher's transparent black clear.
                clear.color->clear = {0, 0, 0, 0};
                frame.passes.push_back(clear);
            }
        if (!AppendPortableMmeMaterial(frame, pending, draw, effect, reason))
            throw std::runtime_error(reason);
        frame.passes.push_back(pending);
        return frame;
    };
    auto frame = assemble(true);
    auto valid = ValidateRenderFrame(frame);
    if (!valid)
        throw std::runtime_error(valid.reason);
    CHECK(!ValidateRenderFrame(assemble(false)));
    auto bad = frame;
    bad.passes[effect.targets.size() + 1].sampled.push_back(1);
    CHECK(!ValidateRenderFrame(bad));
    bad = base;
    bad.targets.back().description.width--;
    auto pending = host;
    const auto oldCount = bad.passes.size();
    CHECK(!AppendPortableMmeMaterial(bad, pending, draw, effect, reason) &&
          bad.passes.size() == oldCount && pending.draws.empty());
    // The material policy preserves explicit point filters and preserves the
    // entire native request when Native is selected.
    auto& samplers = frame.passes[effect.targets.size() + 1].draws[0].shader->samplers;
    CHECK(!samplers.empty() && samplers[0].sampler.min == SamplerFilter::Anisotropic &&
          samplers[0].sampler.maxAnisotropy == 16);
    CHECK(ApplyMaterialSampling(frame, MaterialSamplingPolicy::Native) &&
          samplers[0].sampler.maxAnisotropy == 16);
    CHECK(ApplyMaterialSampling(frame, MaterialSamplingPolicy::PortableTrilinear) &&
          samplers[0].sampler.min == SamplerFilter::Linear &&
          samplers[0].sampler.maxAnisotropy == 1);
    samplers[0].sampler.min = SamplerFilter::Point;
    CHECK(ApplyMaterialSampling(frame, MaterialSamplingPolicy::PortableTrilinear) &&
          samplers[0].sampler.min == SamplerFilter::Point);
    // The point-preservation assertion above must not alter the oracle profile.
    samplers[0].sampler.min = SamplerFilter::Linear;
    // Wrap the actual ordered MRT object passes inside a single-color scene
    // capture; only the original primary attachment may be remapped.
    auto captured = assemble(false);
    if (!AppendPortableMmePost(captured, Publisher(effect), reason))
        throw std::runtime_error(reason);
    CHECK(ApplyMaterialSampling(captured, MaterialSamplingPolicy::PortableTrilinear));
    CHECK(ValidateRenderFrame(captured));
    frame = std::move(captured);
    if (api != "null") {
        BackendOptions options;
        options.backend = api.find("dx11") == 0   ? Backend::D3D11
                          : api.find("dx12") == 0 ? Backend::D3D12
                                                  : Backend::Vulkan;
        options.debugLayer = api.find("debug") != std::string::npos;
        auto backend = CreateOffscreenBackendDevice(width, 96, options);
        if (!backend && options.debugLayer &&
            (backend.reason == "validation_layer_unavailable" ||
             backend.reason == "validation_extensions_unavailable"))
            return 77;
        CHECK(backend);
        if (!backend.device->ShaderFeatures().hlsl)
            return 77;
        for (const auto& pass : frame.passes)
            for (const auto& d : pass.draws)
                if (d.shader) {
                    const auto result = backend.device->PrepareShaderPipeline(d.shader->pipeline);
                    if (!result)
                        throw std::runtime_error(backend.device->ShaderDiagnostic());
                }
        const auto finalInput = frame.finalColor;
        constexpr std::uint64_t copyOutput = 4096;
        CHECK(AppendPostProcess(frame, finalInput, copyOutput, width, 96));
        const auto render = [&](Device& device) {
            SceneRenderer renderer(device);
            PreparedModelFrame prepared;
            CompletionToken completion;
            const auto ready = renderer.Prepare(1, frame, prepared);
            if (!ready)
                throw std::runtime_error(ready.reason + ": " + device.ShaderDiagnostic());
            CHECK(renderer.Submit(prepared, completion));
            std::vector<AttachmentReadback> output;
            for (const auto id : [&] {
                     std::vector<std::uint64_t> ids{SharedFrameTargetId(200)};
                     for (const auto& t : effect.targets)
                         ids.push_back(SharedFrameTargetId(t.identity));
                     ids.push_back(finalInput);
                     return ids;
                 }()) {
                AttachmentReadback image;
                CHECK(renderer.ReadTarget(1, id, image));
                output.push_back(std::move(image));
            }
            const auto coverage = opacity == 1 ? 0u : 4u;
            CHECK(std::count_if(output[coverage].bytes.begin(), output[coverage].bytes.end(),
                                [](auto b) { return b != 0; }) > 20);
            if (material.filename() == L"LegacyMaterialMap.fx")
                PublicMaterialCoverage(output[0], output[4]);
            AttachmentReadback final;
            CHECK(renderer.ReadTarget(1, copyOutput, final) && final.bytes == output.back().bytes);
            for (unsigned n = 0; n < 5; ++n)
                CHECK(renderer.Submit(prepared, completion));
            CHECK(renderer.ReadTarget(1, copyOutput, final) && final.bytes == output.back().bytes);
            CHECK(renderer.Detach());
            Diagnostics(device);
            return output;
        };
        const auto images = render(*backend.device);
        BackendOptions referenceOptions;
        referenceOptions.backend = Backend::D3D11;
        auto reference = CreateOffscreenBackendDevice(width, 96, referenceOptions);
        CHECK(reference);
        const auto expected = render(*reference.device);
        CHECK(images.size() == expected.size());
        for (unsigned n = 0; n < images.size(); ++n)
            Compare(images[n], expected[n].bytes,
                    n == 0                   ? "MaterialMap"
                    : n + 1 == images.size() ? "FinalColor"
                                             : effect.targets[n - 1].name.c_str());
        auto replacement = CreateOffscreenBackendDevice(width, 96, options);
        CHECK(replacement);
        const auto restored = render(*replacement.device);
        for (unsigned n = 0; n < images.size(); ++n)
            CHECK(images[n].bytes == restored[n].bytes);
        std::printf(
            "PASS %s %ux96 opacity=%g: eight targets vs DX11, strict final copy, repeat and replacement; no DX9\n",
            api.c_str(), width, opacity);
        return 0;
    }
    auto device = CreateNullDevice();
    std::map<std::uint64_t, Handle> targets;
    for (const auto& t : frame.targets) {
        const auto h = device->CreateRenderTexture(t.description);
        CHECK(h);
        targets[t.id] = h;
    }
    const auto texture = device->CreateTexture({1, 1});
    CHECK(texture);
    const unsigned white = 0xFFFFFFFF;
    CHECK(device->UploadTexture(texture, {&white, 4, 4}));
    const auto attachment = [&](const FrameAttachment& a) {
        return PassAttachment{targets.at(a.target), a.load,        a.store, a.clear,
                              a.clearDepth,         a.clearStencil};
    };
    CHECK(device->BeginFrame({}));
    RenderPassDesc stale;
    for (const auto& sourcePass : frame.passes) {
        RenderPassDesc pass;
        for (const auto& a : ColorAttachments(sourcePass))
            pass.colors.push_back(attachment(a));
        if (sourcePass.depthStencil)
            pass.depthStencil = attachment(*sourcePass.depthStencil);
        for (auto id : sourcePass.sampled)
            pass.sampled.push_back(targets.at(id));
        if (stale.colors.empty()) {
            stale = pass;
            auto undefined = pass;
            undefined.colors[0].load = AttachmentLoad::Load;
            CHECK(!device->BeginRenderPass(undefined));
            auto feedback = pass;
            feedback.sampled.push_back(pass.colors[0].texture);
            CHECK(!device->BeginRenderPass(feedback));
        }
        CHECK(device->BeginRenderPass(pass));
        for (const auto& d : sourcePass.draws) {
            CHECK(d.shader && device->SetShaderPipeline(d.shader->pipeline));
            ShaderResourceBindings bindings;
            bindings.constants = d.shader->constants;
            bindings.samplers = d.shader->samplers;
            for (const auto& t : d.shader->textures)
                bindings.textures.push_back(
                    {t.slot, t.target.target ? targets.at(t.target.target) : texture});
            CHECK(device->BindShaderResources(bindings));
        }
        CHECK(device->EndRenderPass());
    }
    CHECK(device->EndFrame() && device->Present());
    CHECK(device->Destroy(stale.colors[0].texture));
    CHECK(device->BeginFrame({}) && !device->BeginRenderPass(stale));
    CHECK(device->EndFrame() && device->Present());
    std::puts(
        "PASS material MRT: Null bindings, initialization, feedback, stale handles, extent and atomic failure");
    return 0;
}
}
int main(int argc, char** argv) try {
    SetErrorMode(SEM_FAILCRITICALERRORS | SEM_NOGPFAULTERRORBOX);
    std::setvbuf(stdout, nullptr, _IONBF, 0);
    CHECK(argc == 3 || argc == 4);
    const std::string api = argv[1];
    const auto model = std::filesystem::absolute(std::filesystem::u8path(argv[2]));
    const auto root = std::filesystem::path(MDS_TEST_SOURCE_DIR);
    const auto material = argc == 4 ? std::filesystem::absolute(std::filesystem::u8path(argv[3]))
                                    : root / L"assets/effects/portable/LegacyMaterialMap.fx";
    if (api == "null" || api.find("compile") != std::string::npos) {
        for (unsigned width : {128u, 163u, 128u}) {
            for (float opacity : {1.f, .4f}) {
                const auto result = Contract(model, material, api, width, opacity);
                if (result)
                    return result;
            }
        }
        return 0;
    }
    BackendOptions options;
    options.backend = api.find("dx11") == 0   ? Backend::D3D11
                      : api.find("dx12") == 0 ? Backend::D3D12
                                              : Backend::Vulkan;
    options.debugLayer = api.find("debug") != std::string::npos;
    auto backend = CreateOffscreenBackendDevice(128, 96, options);
    if (!backend && options.debugLayer &&
        (backend.reason == "validation_layer_unavailable" ||
         backend.reason == "validation_extensions_unavailable"))
        return 77;
    CHECK(backend);
    if (!backend.device->ShaderFeatures().hlsl) {
        std::puts("SKIP runtime HLSL unavailable");
        return 77;
    }
    DialogGuard dialogGuard;
    CHECK(dialogGuard.hook);
    Window window;
    CHECK(window.handle && !IsWindowVisible(window.handle));
    ComPtr<IDirect3D9> d3d;
    d3d.Attach(Direct3DCreate9(D3D_SDK_VERSION));
    CHECK(d3d);
    D3DPRESENT_PARAMETERS pp{};
    pp.Windowed = TRUE;
    pp.SwapEffect = D3DSWAPEFFECT_DISCARD;
    pp.BackBufferWidth = pp.BackBufferHeight = 128;
    pp.BackBufferCount = 1;
    pp.BackBufferFormat = D3DFMT_X8R8G8B8;
    pp.PresentationInterval = D3DPRESENT_INTERVAL_IMMEDIATE;
    pp.hDeviceWindow = window.handle;
    ComPtr<IDirect3DDevice9> device;
    Hr(d3d->CreateDevice(0, D3DDEVTYPE_HAL, window.handle,
                         D3DCREATE_HARDWARE_VERTEXPROCESSING | D3DCREATE_FPU_PRESERVE, &pp,
                         &device));
    Runtime runtime(device.Get());
    FileAssetSource assets(root / L"res/assets");
    ToonTextureResolver textures(model.parent_path(), assets);
    RuntimeCpuScene scene;
    std::string reason;
    CHECK(scene.LoadModel(model, reason));
    for (auto slot : scene.Order())
        CHECK(scene.Model(slot)->PrepareMaterials(textures, reason));
    format::SceneResourceResolver resolver(model);
    RuntimeCpuSceneProducer producer;
    CHECK(producer.Load(scene, resolver, false, assets, reason));
    SceneRenderer renderer(*backend.device);
    auto publisher = mme::MmeEngineLoadEffectFile(
        device.Get(), (root / L"assets/effects/portable/LegacyMaterialMapScene.fx").string());
    auto child = mme::MmeEngineLoadEffectFile(device.Get(), material.string());
    CHECK(publisher && publisher->sas && child && child->sas && !mme::SasHadErrors(child->sas));
    for (unsigned width : {128u, 163u, 128u}) {
        ComPtr<IDirect3DSurface9> color, depth;
        Hr(device->SetDepthStencilSurface(nullptr));
        for (unsigned slot = 1; slot < 4; ++slot)
            Hr(device->SetRenderTarget(slot, nullptr));
        Hr(device->CreateRenderTarget(width, 96, D3DFMT_A8R8G8B8, D3DMULTISAMPLE_NONE, 0, FALSE,
                                      &color, nullptr));
        Hr(device->CreateDepthStencilSurface(width, 96, D3DFMT_D24S8, D3DMULTISAMPLE_NONE, 0, FALSE,
                                             &depth, nullptr));
        Hr(device->SetRenderTarget(0, color.Get()));
        Hr(device->SetDepthStencilSurface(depth.Get()));
        D3DVIEWPORT9 viewport{0, 0, width, 96, 0, 1};
        Hr(device->SetViewport(&viewport));
        mme::SasRefreshViewport(publisher->sas, device.Get());
        mme::SasRefreshViewport(child->sas, device.Get());
        render::ModelFrameInputs inputs;
        inputs.view.viewport = {0, 0, width, 96};
        inputs.view.aspect = float(width) / 96;
        HistoryDesc extent;
        extent.width = width;
        extent.height = 96;
        RenderFrame base;
        CHECK(producer.BuildFrame(scene, inputs, extent, {}, base, reason));
        CHECK(base.passes.size() == 1 && !base.passes[0].draws.empty());
        const auto uploads = Upload(device.Get(), base);
        for (float opacity : {1.f, .4f}) {
            auto frame = base;
            auto host = base.passes[0];
            host.draws.clear();
            host.color->clear = {0, 0, 0, 0};
            frame.passes.clear();
            auto effect = Describe(*child, *publisher->sas, 0);
            CHECK(RegisterPortableMmeTargets(frame, {effect}, reason));
            for (const auto& target : effect.targets) {
                FramePass clear;
                clear.color = FrameAttachment{SharedFrameTargetId(target.identity)};
                clear.color->clear = {0, 0, 0, 0};
                frame.passes.push_back(clear);
                ComPtr<IDirect3DBaseTexture9> texture;
                ComPtr<IDirect3DTexture9> image;
                ComPtr<IDirect3DSurface9> surface;
                Hr(child->effect->GetTexture(target.name.c_str(), &texture));
                Hr(texture.As(&image));
                Hr(image->GetSurfaceLevel(0, &surface));
                Hr(device->SetDepthStencilSurface(nullptr));
                Hr(device->SetRenderTarget(0, surface.Get()));
                Hr(device->Clear(0, nullptr, D3DCLEAR_TARGET, 0, 1, 0));
            }
            Hr(device->SetRenderTarget(0, color.Get()));
            Hr(device->SetDepthStencilSurface(depth.Get()));
            Hr(device->SetViewport(&viewport));
            Hr(device->Clear(0, nullptr, D3DCLEAR_TARGET | D3DCLEAR_ZBUFFER, 0, 1, 0));
            Hr(device->SetRenderState(D3DRS_STENCILENABLE, FALSE));
            Hr(device->SetRenderState(D3DRS_SRGBWRITEENABLE, FALSE));
            Hr(device->SetRenderState(D3DRS_SEPARATEALPHABLENDENABLE, FALSE));
            for (auto state : {D3DRS_COLORWRITEENABLE, D3DRS_COLORWRITEENABLE1,
                               D3DRS_COLORWRITEENABLE2, D3DRS_COLORWRITEENABLE3})
                Hr(device->SetRenderState(state, 15));
            Hr(device->BeginScene());
            for (auto draw : base.passes[0].draws) {
                draw.pipeline.shader.nativeDiffuse[3] = opacity;
                draw.pipeline.raster.cull = BatchCull::None;
                draw.pipeline.raster.alphaBlend = false;
                draw.pipeline.stencil = false;
                NativeValues(child->effect, draw, uploads);
                effect = Describe(*child, *publisher->sas, 0);
                if (!AppendPortableMmeMaterial(frame, host, draw, effect, reason))
                    throw std::runtime_error(reason);
                Hr(device->SetFVF(D3DFVF_XYZ | D3DFVF_NORMAL | D3DFVF_TEX1));
                Hr(device->SetStreamSource(0, uploads.at(draw.vertices).vertices.Get(), 0, 32));
                Hr(device->SetIndices(uploads.at(draw.indices).indices.Get()));
                Hr(device->SetRenderState(D3DRS_CULLMODE, D3DCULL_NONE));
                Hr(device->SetRenderState(D3DRS_ZENABLE, TRUE));
                Hr(device->SetRenderState(D3DRS_ZWRITEENABLE, TRUE));
                Hr(device->SetRenderState(D3DRS_ZFUNC, D3DCMP_LESSEQUAL));
                struct Callback {
                    IDirect3DDevice9* device;
                    const ModelReplayDraw* draw;
                    unsigned calls = 0;
                } callback{device.Get(), &draw};
                mme::SasHostCallbacks callbacks{};
                callbacks.ctx = &callback;
                callbacks.RunPass = [](void* ctx, mme::SasEffect* sas, int kind, int pass) -> long {
                    auto& c = *static_cast<Callback*>(ctx);
                    CHECK(kind == 0);
                    ++c.calls;
                    UINT passes = 0;
                    Hr(sas->effect->Begin(&passes, 0));
                    Hr(sas->effect->BeginPass(pass));
                    // Match the selected portable trilinear material profile.
                    for (unsigned slot = 0; slot < 2; ++slot) {
                        Hr(c.device->SetSamplerState(slot, D3DSAMP_MINFILTER, D3DTEXF_LINEAR));
                        Hr(c.device->SetSamplerState(slot, D3DSAMP_MAGFILTER, D3DTEXF_LINEAR));
                        Hr(c.device->SetSamplerState(slot, D3DSAMP_MIPFILTER, D3DTEXF_LINEAR));
                        Hr(c.device->SetSamplerState(slot, D3DSAMP_MAXANISOTROPY, 1));
                    }
                    Hr(c.device->DrawIndexedPrimitive(D3DPT_TRIANGLELIST, 0, 0, c.draw->vertexCount,
                                                      c.draw->firstIndex, c.draw->indexCount / 3));
                    Hr(sas->effect->EndPass());
                    Hr(sas->effect->End());
                    return S_OK;
                };
                mme::SasSetHostCallbacks(callbacks);
                mme::SasExecuteTechnique(child->sas, device.Get(), 0, 0);
                mme::SasSetHostCallbacks({});
                CHECK(!child->sas->runFailed && callback.calls == 2);
            }
            Hr(device->EndScene());
            frame.passes.push_back(host);
            CHECK(ApplyMaterialSampling(frame, MaterialSamplingPolicy::PortableTrilinear));
            CHECK(AppendPostProcess(frame, 1, 16, width, 96));
            auto valid = ValidateRenderFrame(frame);
            if (!valid)
                throw std::runtime_error(valid.reason);
            PreparedModelFrame prepared;
            CompletionToken completion;
            auto ready = renderer.Prepare(1, frame, prepared);
            if (!ready)
                throw std::runtime_error(ready.reason + ": " + backend.device->ShaderDiagnostic());
            CHECK(renderer.Submit(prepared, completion));
            std::vector<std::pair<std::string, std::uint64_t>> targets{{"MaterialMap", 1}};
            std::array<AttachmentReadback, 2> publicGroups;
            for (const auto& target : effect.targets)
                targets.emplace_back(target.name, SharedFrameTargetId(target.identity));
            for (const auto& target : targets) {
                ComPtr<IDirect3DSurface9> surface = color;
                if (target.second != 1) {
                    ComPtr<IDirect3DBaseTexture9> texture;
                    ComPtr<IDirect3DTexture9> image;
                    surface.Reset();
                    Hr(child->effect->GetTexture(target.first.c_str(), &texture));
                    Hr(texture.As(&image));
                    Hr(image->GetSurfaceLevel(0, &surface));
                }
                AttachmentReadback image;
                CHECK(renderer.ReadTarget(1, target.second, image));
                Compare(image, Read(device.Get(), surface.Get()), target.first.c_str());
                if (material.filename() == L"LegacyMaterialMap.fx") {
                    if (target.first == "MaterialMap")
                        publicGroups[0] = image;
                    else if (target.first == "Gbuffer5RT")
                        publicGroups[1] = image;
                }
                if (target.first == (opacity == 1.f ? "MaterialMap" : "Gbuffer5RT"))
                    CHECK(std::count_if(image.bytes.begin(), image.bytes.end(),
                                        [](auto b) { return b != 0; }) > 20);
            }
            if (material.filename() == L"LegacyMaterialMap.fx")
                PublicMaterialCoverage(publicGroups[0], publicGroups[1]);
            AttachmentReadback before, after;
            CHECK(renderer.ReadTarget(1, 1, before) && renderer.ReadTarget(1, 16, after) &&
                  before.bytes == after.bytes);
            for (unsigned n = 0; n < 5; ++n)
                CHECK(renderer.Submit(prepared, completion));
            CHECK(renderer.ReadTarget(1, 16, after) && before.bytes == after.bytes);
            if (width == 163) {
                Diagnostics(*backend.device);
                auto replacement = CreateOffscreenBackendDevice(width, 96, options);
                CHECK(replacement && renderer.Rebind(*replacement.device));
                backend = std::move(replacement);
                CHECK(renderer.Prepare(1, frame, prepared) &&
                      renderer.Submit(prepared, completion));
                CHECK(renderer.ReadTarget(1, 16, after) && before.bytes == after.bytes);
            }
            std::printf(
                "PASS %s %s %ux96 opacity=%g: eight native targets, strict copy, repeat, rebind\n",
                api.c_str(), material.filename().string().c_str(), width, opacity);
        }
    }
    CHECK(renderer.Detach());
    Diagnostics(*backend.device);
    CHECK(!IsWindowVisible(window.handle) && blockedDialogs == 0);
} catch (const std::exception& e) {
    std::fprintf(stderr, "%s\n", e.what());
    return 1;
}
