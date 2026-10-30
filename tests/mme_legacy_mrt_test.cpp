#define NOMINMAX
#include <Windows.h>
#include <d3dx9.h>
#include <wrl/client.h>
#include "mikudancestudio/mme_portable.hpp"
#include "mikudancestudio/rhi_backend.hpp"
#include "mikudancestudio/rhi_d3d12.hpp"
#include "mikudancestudio/rhi_vulkan.hpp"
#include "mikudancestudio/rhi_float.hpp"
#include "mikudancestudio/rhi_scene_renderer.hpp"
#include "mikudancestudio/rhi_shared_targets.hpp"
#include "mme_portable_api.h"
#include "mme_legacy_api.h"
#include "sas_exec.h"
#include "sas_interpreter.h"
#include "pass_planner.h"
#include "mme_requirements_report.hpp"
#include <filesystem>
#include <fstream>
#include <iterator>
#include <map>
#include <cmath>
#include <cstdio>
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
void CheckDiagnostics(Device& device) {
#ifdef MIKUDANCESTUDIO_RHI_D3D12
    if (device.BackendType() == Backend::D3D12)
        CHECK(QueryD3D12Stats(device).debugErrors == 0);
#endif
#ifdef MIKUDANCESTUDIO_RHI_VULKAN
    if (device.BackendType() == Backend::Vulkan)
        CHECK(QueryVulkanStats(device).validationErrors == 0);
#endif
}
void CheckNull(const RenderFrame& frame) {
    auto device = CreateNullDevice();
    std::map<std::uint64_t, Handle> targets;
    for (const auto& target : frame.targets) {
        const auto handle = device->CreateRenderTexture(target.description);
        CHECK(handle);
        targets.emplace(target.id, handle);
    }
    const auto attachment = [&](const FrameAttachment& a) {
        return PassAttachment{targets.at(a.target), a.load,        a.store, a.clear,
                              a.clearDepth,         a.clearStencil};
    };
    CHECK(device->BeginFrame({}));
    RenderPassDesc first;
    for (const auto& source : frame.passes) {
        RenderPassDesc pass;
        for (const auto& a : ColorAttachments(source))
            pass.colors.push_back(attachment(a));
        if (source.depthStencil)
            pass.depthStencil = attachment(*source.depthStencil);
        for (auto id : source.sampled)
            pass.sampled.push_back(targets.at(id));
        if (first.colors.empty()) {
            first = pass;
            auto feedback = pass;
            feedback.sampled.push_back(pass.colors.front().texture);
            CHECK(!device->BeginRenderPass(feedback));
        }
        CHECK(device->BeginRenderPass(pass));
        for (const auto& draw : source.draws) {
            CHECK(draw.shader && device->SetShaderPipeline(draw.shader->pipeline));
            ShaderResourceBindings bindings;
            bindings.constants = draw.shader->constants;
            bindings.samplers = draw.shader->samplers;
            for (const auto& t : draw.shader->textures)
                bindings.textures.push_back({t.slot, targets.at(t.target.target)});
            CHECK(device->BindShaderResources(bindings));
        }
        CHECK(device->EndRenderPass());
    }
    CHECK(device->EndFrame() && device->Present());
    CHECK(device->Destroy(first.colors.front().texture));
    CHECK(device->BeginFrame({}) && !device->BeginRenderPass(first));
    CHECK(device->EndFrame() && device->Present());
}
struct Fixture {
    ComPtr<ID3DXEffect> fx;
    std::unique_ptr<mme::SasEffect, decltype(&mme::SasUnload)> sas{nullptr, mme::SasUnload};
    MmePortableEffect portable;
    Fixture(IDirect3DDevice9* device, const wchar_t* name, bool scene) {
        const auto path =
            std::filesystem::path(MDS_TEST_SOURCE_DIR) / L"assets/effects/portable" / name;
        std::ifstream file(path, std::ios::binary);
        portable.source = std::make_shared<const std::string>(std::istreambuf_iterator<char>(file),
                                                              std::istreambuf_iterator<char>());
        auto imported = std::make_shared<MmeLegacyProgram>();
        std::string reason;
        CHECK(scene ? MmeLegacyImportScene(*portable.source, *imported, reason)
                    : MmeLegacyImport(*portable.source, *imported, reason));
        portable.legacy = imported;
        portable.post = scene;
        portable.profile = scene ? "legacy-scene-v1" : "legacy-material-v1";
        ComPtr<ID3DXBuffer> errors;
        if (FAILED(D3DXCreateEffectFromFileW(device, path.c_str(), nullptr, nullptr, 0, nullptr,
                                             &fx, &errors)))
            throw std::runtime_error(errors ? static_cast<const char*>(errors->GetBufferPointer())
                                            : "FX compile");
        if (scene) {
            sas.reset(mme::SasParse(fx.Get(), path.string(), device));
            CHECK(sas && !mme::SasHadErrors(sas.get()));
            std::uint64_t identity = 100;
            for (const auto& r : sas->resources) {
                MmePortableTarget target{r.name};
                target.depth = r.semanticId == 0x27;
                target.format = r.format == D3DFMT_A16B16G16R16F ? MmePortableFormat::Rgba16Float
                                                                 : MmePortableFormat::Rgba8;
                target.identity = identity++;
                target.relative = r.hasViewportRatio;
                target.viewportRatio = {r.viewportRatio[0], r.viewportRatio[1]};
                portable.targets.push_back(target);
            }
        }
        for (const auto& p : imported->passes) {
            MmePortablePass pass{p.technique, p.name, p.vertex, p.pixel};
            for (auto slot : MmeLegacySamplers(*imported, p)) {
                pass.sampleSlots.push_back(slot);
                pass.sampled.push_back(imported->samplers[slot].texture);
            }
            portable.passes.push_back(std::move(pass));
        }
        for (const auto& v : imported->variables) {
            MmePortableParameter p;
            p.name = v.name;
            p.semantic = v.semantic;
            p.rows = v.rows;
            p.columns = v.columns;
            p.elements = v.elements;
            std::vector<float> values(v.rows * v.columns * v.elements);
            CHECK(SUCCEEDED(fx->GetFloatArray(v.name.c_str(), values.data(), UINT(values.size()))));
            p.value.resize(values.size() * 4);
            std::memcpy(p.value.data(), values.data(), p.value.size());
            portable.parameters.push_back(std::move(p));
        }
        if (scene && !MmePortableExportScene(*sas, portable, reason))
            throw std::runtime_error(reason);
    }
    std::uint64_t Id(const char* name) const {
        for (const auto& t : portable.targets)
            if (t.name == name)
                return SharedFrameTargetId(t.identity);
        throw std::runtime_error("missing target");
    }
};
std::vector<std::uint8_t> Read(IDirect3DDevice9* device, IDirect3DSurface9* surface) {
    D3DSURFACE_DESC desc{};
    CHECK(SUCCEEDED(surface->GetDesc(&desc)));
    const bool half = desc.Format == D3DFMT_A16B16G16R16F;
    const unsigned stride = half ? 8 : 4;
    ComPtr<IDirect3DSurface9> cpu;
    CHECK(SUCCEEDED(device->CreateOffscreenPlainSurface(desc.Width, desc.Height, desc.Format,
                                                        D3DPOOL_SYSTEMMEM, &cpu, nullptr)));
    CHECK(SUCCEEDED(device->GetRenderTargetData(surface, cpu.Get())));
    D3DLOCKED_RECT lock{};
    CHECK(SUCCEEDED(cpu->LockRect(&lock, nullptr, D3DLOCK_READONLY)));
    std::vector<std::uint8_t> bytes(desc.Width * desc.Height * stride);
    for (unsigned y = 0; y < desc.Height; ++y)
        for (unsigned x = 0; x < desc.Width; ++x) {
            const auto* p =
                static_cast<const std::uint8_t*>(lock.pBits) + y * lock.Pitch + x * stride;
            auto* q = bytes.data() + (y * desc.Width + x) * stride;
            if (half)
                std::memcpy(q, p, 8);
            else {
                q[0] = p[2];
                q[1] = p[1];
                q[2] = p[0];
                q[3] = p[3];
            }
        }
    CHECK(SUCCEEDED(cpu->UnlockRect()));
    return bytes;
}
void Compare(const AttachmentReadback& actual, const std::vector<std::uint8_t>& expected,
             const char* label) {
    CHECK(actual.bytes.size() == expected.size());
    float maximum = 0;
    if (actual.description.format == TextureFormat::Rgba16Float) {
        for (std::size_t n = 0; n < expected.size(); n += 2) {
            std::uint16_t a, b;
            std::memcpy(&a, actual.bytes.data() + n, 2);
            std::memcpy(&b, expected.data() + n, 2);
            const float va = HalfToFloat(a), vb = HalfToFloat(b);
            CHECK(std::isfinite(va) && std::isfinite(vb));
            const float ulp = std::ldexp(
                1.f, std::max(-24, std::ilogb(std::max(std::abs(vb), std::ldexp(1.f, -24))) - 10));
            maximum = std::max(maximum, std::abs(va - vb) / ulp);
        }
        std::printf("%s DX9 half ULP=%.3f (limit=1)\n", label, maximum);
        CHECK(maximum <= 1);
    } else {
        for (std::size_t n = 0; n < expected.size(); ++n)
            maximum = std::max(maximum, float(std::abs(int(actual.bytes[n]) - int(expected[n]))));
        std::printf("%s DX9 UNORM8 delta=%.0f (limit=2)\n", label, maximum);
        CHECK(maximum <= 2);
    }
}
}
int main(int argc, char** argv) try {
    SetErrorMode(SEM_FAILCRITICALERRORS | SEM_NOGPFAULTERRORBOX);
    std::setvbuf(stdout, nullptr, _IONBF, 0);
    const std::string api = argc > 1 ? argv[1] : "dx11";
    BackendOptions options;
    options.backend = api.find("dx11") == 0   ? Backend::D3D11
                      : api.find("dx12") == 0 ? Backend::D3D12
                                              : Backend::Vulkan;
    options.debugLayer = api.find("debug") != std::string::npos;
    auto native = CreateOffscreenBackendDevice(48, 32, options);
    if (!native && options.debugLayer &&
        (native.reason == "validation_layer_unavailable" ||
         native.reason == "validation_extensions_unavailable"))
        return 77;
    CHECK(native);
    if (!native.device->ShaderFeatures().hlsl)
        return 77;
    struct Window {
        HWND h =
            CreateWindowExW(WS_EX_NOACTIVATE, L"STATIC", L"Hidden FX9 MRT oracle", WS_OVERLAPPED, 0,
                            0, 64, 64, nullptr, nullptr, GetModuleHandleW(nullptr), nullptr);
        ~Window() { DestroyWindow(h); }
    } window;
    CHECK(window.h && !IsWindowVisible(window.h));
    ComPtr<IDirect3D9> d3d;
    d3d.Attach(Direct3DCreate9(D3D_SDK_VERSION));
    CHECK(d3d);
    D3DPRESENT_PARAMETERS pp{};
    pp.Windowed = TRUE;
    pp.hDeviceWindow = window.h;
    pp.BackBufferWidth = 64;
    pp.BackBufferHeight = 64;
    pp.BackBufferCount = 1;
    pp.BackBufferFormat = D3DFMT_X8R8G8B8;
    pp.SwapEffect = D3DSWAPEFFECT_DISCARD;
    ComPtr<IDirect3DDevice9> dx9;
    CHECK(SUCCEEDED(d3d->CreateDevice(0, D3DDEVTYPE_HAL, window.h,
                                      D3DCREATE_HARDWARE_VERTEXPROCESSING, &pp, &dx9)));
    SceneRenderer renderer(*native.device);
    const auto identity = std::array<float, 16>{1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1};
    const float vertices[][8] = {
        {-1, -1, 0, 0, 0, 1, 0, 1}, {-1, 3, 0, 0, 0, 1, 0, -1}, {3, -1, 0, 0, 0, 1, 2, 1}};
    const char* names[] = {"CaptureA", "CaptureB", "HalfA", "HalfB", "QuarterHdr", "Tone"};
    for (const auto extent : {std::pair<unsigned, unsigned>{48, 32}, {55, 37}, {48, 32}}) {
        const auto [width, height] = extent;
        ComPtr<IDirect3DSurface9> final, depth;
        CHECK(SUCCEEDED(dx9->SetRenderTarget(1, nullptr)));
        CHECK(SUCCEEDED(dx9->SetDepthStencilSurface(nullptr)));
        CHECK(SUCCEEDED(dx9->CreateRenderTarget(width, height, D3DFMT_A8R8G8B8, D3DMULTISAMPLE_NONE,
                                                0, FALSE, &final, nullptr)));
        CHECK(SUCCEEDED(dx9->CreateDepthStencilSurface(
            width, height, D3DFMT_D24S8, D3DMULTISAMPLE_NONE, 0, FALSE, &depth, nullptr)));
        CHECK(SUCCEEDED(dx9->SetRenderTarget(0, final.Get())));
        CHECK(SUCCEEDED(dx9->SetDepthStencilSurface(depth.Get())));
        const D3DVIEWPORT9 viewport{0, 0, width, height, 0, 1};
        CHECK(SUCCEEDED(dx9->SetViewport(&viewport)));
        Fixture material(dx9.Get(), L"LegacyMrtMaterial.fx", false),
            scene(dx9.Get(), L"LegacyMrtScene.fx", true);
        MmeRequirementsReport requirements;
        requirements.Portable(scene.portable.source, scene.sas.get(), "public-MRT");
        CHECK(requirements.Rows().find("public-MRT:Scene/Split outputs=2 vs=ok ps=ok") !=
              std::string::npos);
        CHECK(SUCCEEDED(material.fx->SetValue("worldViewProjection", identity.data(), 64)));
        const float size[] = {float(width), float(height)};
        CHECK(SUCCEEDED(scene.fx->SetFloatArray("viewportSize", size, 2)));
        material.fx->SetTechnique("Main");
        scene.fx->SetTechnique("Scene");
        mme::SasHostCallbacks callbacks{};
        callbacks.ctx = dx9.Get();
        callbacks.RunPass = [](void* ctx, mme::SasEffect* sas, int kind, int pass) -> long {
            CHECK(kind == 1);
            return mme::SasDefaultDrawBufferPass(sas->effect, static_cast<IDirect3DDevice9*>(ctx),
                                                 pass);
        };
        mme::SasSetHostCallbacks(callbacks);
        auto* state = mme::SasCreateRunState(dx9.Get());
        CHECK(state);
        CHECK(mme::SasExecuteTechniqueStep(scene.sas.get(), &scene.sas->techniques[0], dx9.Get(),
                                           state));
        CHECK(SUCCEEDED(dx9->BeginScene()));
        CHECK(SUCCEEDED(dx9->SetFVF(D3DFVF_XYZ | D3DFVF_NORMAL | D3DFVF_TEX1)));
        UINT passes = 0;
        CHECK(SUCCEEDED(material.fx->Begin(&passes, 0)) && passes == 1);
        CHECK(SUCCEEDED(material.fx->BeginPass(0)));
        CHECK(SUCCEEDED(dx9->DrawPrimitiveUP(D3DPT_TRIANGLELIST, 1, vertices, 32)));
        CHECK(SUCCEEDED(material.fx->EndPass()));
        CHECK(SUCCEEDED(material.fx->End()));
        CHECK(SUCCEEDED(
            mme::SasResumeTechnique(scene.sas.get(), &scene.sas->techniques[0], dx9.Get(), state)));
        CHECK(SUCCEEDED(dx9->EndScene()));
        mme::SasRestoreRunState(dx9.Get(), state);
        mme::SasDestroyRunState(state);
        mme::SasSetHostCallbacks({});
        const auto reference = Read(dx9.Get(), final.Get());
        RenderFrame frame;
        const auto access =
            TextureAccess::ColorAttachment | TextureAccess::Sampled | TextureAccess::CopySource;
        frame.targets = {{1, {width, height, TextureFormat::Rgba8, access}},
                         {2,
                          {width, height, TextureFormat::Depth24Stencil8,
                           unsigned(TextureAccess::DepthStencilAttachment)}}};
        frame.finalColor = 1;
        ModelReplayResource geometry;
        geometry.key = {1, 1};
        geometry.bytes.resize(sizeof(vertices));
        std::memcpy(geometry.bytes.data(), vertices, sizeof(vertices));
        frame.resources = {geometry};
        ModelReplayDraw draw;
        draw.indexed = false;
        draw.vertices = geometry.key;
        draw.drawVertexCount = 3;
        draw.pipeline.viewport = ModelViewport{0, 0, width, height};
        draw.pipeline.shader.world = draw.pipeline.shader.view = draw.pipeline.shader.projection =
            identity;
        std::string reason;
        CHECK(ApplyPortableMmeMaterial(draw, material.portable, reason));
        FramePass capture;
        capture.color = FrameAttachment{1};
        capture.depthStencil = FrameAttachment{2};
        capture.draws = {draw};
        frame.passes = {capture};
        const auto base = frame;
        if (!AppendPortableMmePost(frame, scene.portable, reason))
            throw std::runtime_error(reason);
        {
            // Relative allocations keep the discovery viewport even inside a
            // smaller child turn; only VIEWPORTPIXELSIZE changes to that turn.
            auto nested = scene.portable;
            for (auto& target : nested.targets) {
                target.relative = true;
                target.viewportRatio[0] *= .5f;
                target.viewportRatio[1] *= .5f;
            }
            MmePortableTurn turn;
            turn.id = 1;
            turn.target = {"Child", MmePortableFormat::Rgba16Float, false, 500};
            turn.target.relative = true;
            turn.target.viewportRatio = {.5f, .5f};
            turn.effects = {nested};
            auto childFrame = base;
            childFrame.passes[0].draws.clear();
            std::vector<MmePortableEffect> completed;
            CHECK(PrependPortableMmeTurns(childFrame, {turn}, {}, completed, reason));
            const auto half =
                std::find_if(childFrame.targets.begin(), childFrame.targets.end(),
                             [&](const auto& t) { return t.id == scene.Id("HalfA"); });
            CHECK(half != childFrame.targets.end() &&
                  half->description.width == unsigned(width * .25f) &&
                  half->description.height == unsigned(height * .25f));
            CheckNull(childFrame);
        }
        PreparedModelFrame prepared;
        CompletionToken token;
        auto ready = renderer.Prepare(1, frame, prepared);
        if (!ready)
            throw std::runtime_error(ready.reason + ": " + native.device->ShaderDiagnostic());
        CHECK(renderer.Submit(prepared, token));
        for (const auto* name : names) {
            ComPtr<IDirect3DBaseTexture9> texture;
            ComPtr<IDirect3DTexture9> image;
            ComPtr<IDirect3DSurface9> surface;
            CHECK(SUCCEEDED(scene.fx->GetTexture(name, &texture)) &&
                  SUCCEEDED(texture.As(&image)) && SUCCEEDED(image->GetSurfaceLevel(0, &surface)));
            AttachmentReadback actual;
            CHECK(renderer.ReadTarget(1, scene.Id(name), actual));
            D3DSURFACE_DESC desc{};
            surface->GetDesc(&desc);
            CHECK(actual.description.width == desc.Width &&
                  actual.description.height == desc.Height);
            Compare(actual, Read(dx9.Get(), surface.Get()), name);
            if (actual.description.format == TextureFormat::Rgba16Float) {
                const auto value = [&](unsigned x, unsigned y, unsigned channel) {
                    std::uint16_t half;
                    std::memcpy(&half, actual.bytes.data() + (y * desc.Width + x) * 8 + channel * 2,
                                2);
                    return HalfToFloat(half);
                };
                if (std::string(name) == "CaptureA") {
                    CHECK(value(0, 0, 0) > 1 && value(0, 0, 1) == -.5f && value(0, 0, 3) == .5f);
                    CHECK(value(0, 0, 0) < value(desc.Width - 1, 0, 0));
                } else if (std::string(name) == "CaptureB") {
                    CHECK(value(0, 0, 0) == -1 && value(0, 0, 1) > 1);
                    CHECK(value(0, 0, 1) < value(0, desc.Height - 1, 1));
                } else if (std::string(name) == "QuarterHdr")
                    CHECK(value(0, 0, 1) < 0 && value(0, 0, 3) == .3125f);
            }
        }
        AttachmentReadback actual, tone;
        CHECK(renderer.ReadTarget(1, frame.finalColor, actual));
        Compare(actual, reference, "FinalColor");
        CHECK(renderer.ReadTarget(1, scene.Id("Tone"), tone) && actual.bytes == tone.bytes);
        for (unsigned n = 0; n < 8; ++n)
            CHECK(renderer.Submit(prepared, token));
        CHECK(renderer.ReadTarget(1, frame.finalColor, tone) && actual.bytes == tone.bytes);
        if (width == 55) {
            CheckDiagnostics(*native.device);
            auto replacement = CreateOffscreenBackendDevice(width, height, options);
            CHECK(replacement);
            CHECK(renderer.Rebind(*replacement.device));
            native = std::move(replacement);
            CHECK(renderer.Prepare(1, frame, prepared) && renderer.Submit(prepared, token));
            CHECK(renderer.ReadTarget(1, frame.finalColor, tone) && actual.bytes == tone.bytes);
        }
        // Reject an attachment extent conflict atomically, before any uploads.
        auto bad = scene.portable;
        for (auto& target : bad.targets)
            if (target.name == "HalfB")
                target.viewportRatio = {.25f, .25f};
        auto unchanged = base;
        CHECK(!AppendPortableMmePost(unchanged, bad, reason));
        CHECK(unchanged.targets.size() == base.targets.size() &&
              unchanged.passes.size() == base.passes.size());
        bad = scene.portable;
        for (auto& target : bad.targets)
            if (target.name == "CaptureA") {
                target.identity = 0;
                target.relative = true;
                target.viewportRatio = {.5f, .5f};
            }
        unchanged = base;
        CHECK(!AppendPortableMmePost(unchanged, bad, reason));
        CHECK(reason == "MME_scene_capture_extent_not_supported" &&
              unchanged.targets.size() == base.targets.size());
        std::printf("%s %ux%u resize/repeat/rebind/extent rejection passed\n", api.c_str(), width,
                    height);
    }
    CHECK(renderer.Detach());
    CheckDiagnostics(*native.device);
    CHECK(!IsWindowVisible(window.h));
    std::printf("PASS %s ordinary FX9 MRT/HDR and relative targets\n", api.c_str());
} catch (const std::exception& error) {
    std::fprintf(stderr, "%s\n", error.what());
    return 1;
}
