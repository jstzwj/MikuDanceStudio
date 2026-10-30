#define NOMINMAX
#include <Windows.h>
#include <d3dx9.h>
#include <wrl/client.h>
#include "mikudancestudio/rhi_backend.hpp"
#include "mikudancestudio/mmd_app.hpp"
#include "mikudancestudio/rhi_scene_renderer.hpp"
#include "mikudancestudio/mme_portable.hpp"
#include "mme_portable_api.h"
#include "mikudancestudio/rhi_shared_targets.hpp"
#include "mikudancestudio/rhi_d3d12.hpp"
#include "mikudancestudio/rhi_vulkan.hpp"
#include "mme_legacy_api.h"
#include "mme_standard_api.h"
#include "model_data.h"
#include "material_bind.h"
#include "effect_engine.h"
#include "mme_globals.h"
#include <d3dcompiler.h>
#include "sas_exec.h"
#include "pass_planner.h"
#include <filesystem>
#include <fstream>
#include <iterator>
#include <cstdio>
#include <cstring>
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
constexpr unsigned Width = 48, Height = 32;
MmePortableEffect Effect(const wchar_t* name, bool post) {
    const auto path =
        std::filesystem::path(MDS_TEST_SOURCE_DIR) / L"assets/effects/portable" / name;
    std::ifstream file(path, std::ios::binary);
    auto source = std::make_shared<const std::string>(std::istreambuf_iterator<char>(file),
                                                      std::istreambuf_iterator<char>());
    MmePortableEffect out;
    out.source = source;
    out.post = post;
    out.profile = post ? "post-v1" : "material-v1";
    if (post) {
        out.passes = {{"Post", "Grade", "fullscreen_vs", "grade_ps"},
                      {"Post", "Finish", "fullscreen_vs", "finish_ps"}};
        out.multiply = {.5f, .75f, 1, .5f};
        out.add = {.125f, .0625f, 0, .125f};
        std::string reason;
        CHECK(MmePortablePostLayout(out, reason));
    } else {
        out.passes = {{"Main", "Tint", "material_vs", "material_ps"}};
        out.multiply = {.75f, 1, .5f, 1};
    }
    return out;
}
std::vector<std::uint8_t> Read(IDirect3DDevice9* d, IDirect3DSurface9* surface) {
    ComPtr<IDirect3DSurface9> cpu;
    CHECK(SUCCEEDED(d->CreateOffscreenPlainSurface(Width, Height, D3DFMT_A8R8G8B8,
                                                   D3DPOOL_SYSTEMMEM, &cpu, nullptr)));
    CHECK(SUCCEEDED(d->GetRenderTargetData(surface, cpu.Get())));
    D3DLOCKED_RECT lock{};
    CHECK(SUCCEEDED(cpu->LockRect(&lock, nullptr, D3DLOCK_READONLY)));
    std::vector<std::uint8_t> rgba(Width * Height * 4);
    for (unsigned y = 0; y < Height; ++y)
        for (unsigned x = 0; x < Width; ++x) {
            const auto* bgra =
                static_cast<const std::uint8_t*>(lock.pBits) + y * lock.Pitch + x * 4;
            auto* p = rgba.data() + (y * Width + x) * 4;
            p[0] = bgra[2];
            p[1] = bgra[1];
            p[2] = bgra[0];
            p[3] = bgra[3];
        }
    cpu->UnlockRect();
    return rgba;
}
void Compare(const std::vector<std::uint8_t>& a, const std::vector<std::uint8_t>& b, unsigned limit,
             const char* label) {
    CHECK(a.size() == b.size());
    unsigned maximum = 0;
    for (std::size_t n = 0; n < a.size(); ++n)
        maximum = std::max(maximum, unsigned(std::abs(int(a[n]) - int(b[n]))));
    std::printf("%s DX9 max channel delta=%u (limit=%u)\n", label, maximum, limit);
    if (maximum > limit)
        std::printf("first rgba: native=%u,%u,%u,%u DX9=%u,%u,%u,%u\n", a[0], a[1], a[2], a[3],
                    b[0], b[1], b[2], b[3]);
    CHECK(maximum <= limit);
}
}
int main(int argc, char** argv) try {
    SetErrorMode(SEM_FAILCRITICALERRORS | SEM_NOGPFAULTERRORBOX);
    const std::string api = argc > 1 ? argv[1] : "dx11";
    const bool legacyScene = argc > 2 && std::string(argv[2]) == "legacy-scene";
    const bool sceneScript = legacyScene || (argc > 2 && std::string(argv[2]) == "scene");
    const std::string signature = argc > 2 ? argv[2] : "";
    const bool standard = signature.find("standard") == 0;
    const bool legacyMulti = argc > 2 && std::string(argv[2]) == "legacy-multi";
    const bool legacy =
        legacyScene || standard || legacyMulti || (argc > 2 && std::string(argv[2]) == "legacy");
    const bool controls =
        (sceneScript && !legacyScene) || (argc > 2 && std::string(argv[2]) == "controls");
    BackendOptions options;
    options.backend = api.find("dx11") == 0   ? Backend::D3D11
                      : api.find("dx12") == 0 ? Backend::D3D12
                                              : Backend::Vulkan;
    options.debugLayer = api.find("debug") != std::string::npos;
    auto native = CreateOffscreenBackendDevice(Width, Height, options);
    if (!native && options.debugLayer &&
        (native.reason == "validation_layer_unavailable" ||
         native.reason == "validation_extensions_unavailable"))
        return 77;
    CHECK(native);
    if (!native.device->ShaderFeatures().hlsl)
        return 77;
    auto material = Effect(legacyScene ? L"LegacySharedMaterial.fx"
                           : standard  ? L"LegacyStandard.fx"
                           : legacy    ? L"LegacyMaterial.fx"
                           : controls  ? L"ControlMaterial.fx"
                                       : L"MaterialTint.fx",
                           false),
         post = Effect(legacyScene   ? L"LegacyScene.fx"
                       : sceneScript ? L"SceneScript.fx"
                       : controls    ? L"ControlPost.fx"
                                     : L"TwoStage.fx",
                       true);
    std::string vertexEntry = "VertexMain", pixelEntry = "PixelMain";
    if (standard && signature != "standard") {
        auto source = *material.source;
        const bool inout = signature == "standard-inout";
        const std::string wrapper = inout ? R"(
void WrappedVertex(in float4 p:POSITION, in float3 n:NORMAL,
                   inout float2 uv:TEXCOORD0, out float4 clip:POSITION,
                   out float3 normal:TEXCOORD1) {
    clip=VertexMain(p,n,uv,uv,normal);
}
struct PixelOutput { float4 color:COLOR0; };
void WrappedPixel(in float2 uv:TEXCOORD0,in float3 n:TEXCOORD1,out PixelOutput result) {
    PixelMain(uv,n,result.color);
}
)"
                                          : R"(
struct VertexInput { float4 p:POSITION0; float3 n:NORMAL0; float2 uv:TEXCOORD0; };
struct VertexOutput { float4 p:POSITION; float2 uv:TEXCOORD0; float3 n:TEXCOORD1; };
void WrappedVertex(in VertexInput input, out VertexOutput result) {
    result.p=VertexMain(input.p,input.n,input.uv,result.uv,result.n);
}
struct PixelInput { float2 uv:TEXCOORD0; float3 n:TEXCOORD1; };
struct PixelOutput { float4 color:COLOR0; };
PixelOutput WrappedPixel(in PixelInput input) {
    PixelOutput result; PixelMain(input.uv,input.n,result.color); return result;
}
)";
        source.insert(source.find("technique Main"), wrapper);
        for (const auto& entry : {std::string("VertexMain"), std::string("PixelMain")}) {
            const std::string from = entry + "();",
                              to = (entry == "VertexMain" ? "WrappedVertex" : "WrappedPixel") +
                                   std::string("();");
            for (auto at = source.find(from); at != std::string::npos;
                 at = source.find(from, at + to.size()))
                source.replace(at, from.size(), to);
        }
        vertexEntry = "WrappedVertex";
        pixelEntry = "WrappedPixel";
        material.source = std::make_shared<const std::string>(std::move(source));
    }
    if (legacyMulti) {
        auto source = *material.source;
        source.insert(source.find("sampler2D ColorSampler"),
                      "texture Lookup : MATERIALTOONTEXTURE;\n"
                      "sampler2D LookupSampler = sampler_state { Texture=<Lookup>;"
                      " MinFilter=POINT; MagFilter=POINT; MipFilter=POINT;"
                      " AddressU=CLAMP; AddressV=CLAMP; };\n");
        const std::string original = "tex2D(ColorSampler,f.uv)";
        source.replace(source.find(original), original.size(),
                       "(.75*tex2D(ColorSampler,f.uv*2-.5)+"
                       ".25*tex2D(LookupSampler,f.uv*2-.5))");
        material.source = std::make_shared<const std::string>(std::move(source));
    }
    if (argc > 3 && std::string(argv[3]) == "--compile-only") {
        CHECK(standard);
        material.profile = "legacy-material-v1";
        material.passes = {{"Main", "Tint", vertexEntry, pixelEntry}};
        auto imported = std::make_shared<MmeLegacyProgram>();
        std::string reason;
        CHECK(MmeLegacyImport(*material.source, *imported, reason));
        material.legacy = imported;
        for (const auto& v : imported->variables) {
            MmePortableParameter p;
            p.name = v.name;
            p.semantic = v.semantic;
            p.rows = v.rows;
            p.columns = v.columns;
            p.elements = v.elements;
            p.type = v.type == "bool"  ? MmePortableParameter::Type::Bool
                     : v.type == "int" ? MmePortableParameter::Type::Int
                                       : MmePortableParameter::Type::Float;
            p.value.resize(v.rows * v.columns * v.elements * 4);
            material.parameters.push_back(std::move(p));
        }
        const auto program = PortableMmeProgram(material, material.passes[0], reason);
        if (!program)
            throw std::runtime_error(reason);
        ShaderPipeline pipeline;
        pipeline.program = program;
        const auto result = native.device->PrepareShaderPipeline(pipeline);
        if (!result)
            throw std::runtime_error(native.device->ShaderDiagnostic());
        std::printf(
            "PASS %s %s: actual native compilation/reflection of FX9 stage wrappers; no DX9 device\n",
            api.c_str(), signature.c_str());
        return 0;
    }
    struct Window {
        HWND h = CreateWindowExW(WS_EX_NOACTIVATE, L"STATIC", L"Hidden portable FX oracle",
                                 WS_OVERLAPPED, 0, 0, 64, 64, nullptr, nullptr,
                                 GetModuleHandleW(nullptr), nullptr);
        ~Window() { DestroyWindow(h); }
    } window;
    CHECK(window.h && !IsWindowVisible(window.h));
    struct FxRuntime {
#ifdef _WIN64
        HMODULE handle = LoadLibraryW(L"d3dx9_43.dll");
#else
        HMODULE handle = LoadLibraryW(L"d3dx9_32.dll");
#endif
        ~FxRuntime() {
            if (handle)
                FreeLibrary(handle);
        }
    } runtime;
    CHECK(runtime.handle);
    using CreateEffect =
        HRESULT(WINAPI*)(IDirect3DDevice9*, LPCVOID, UINT, const D3DXMACRO*, ID3DXInclude*, DWORD,
                         ID3DXEffectPool*, ID3DXEffect**, ID3DXBuffer**);
    const auto createEffect =
        reinterpret_cast<CreateEffect>(GetProcAddress(runtime.handle, "D3DXCreateEffect"));
    CHECK(createEffect);
    ComPtr<IDirect3D9> d3d;
    d3d.Attach(Direct3DCreate9(D3D_SDK_VERSION));
    CHECK(d3d);
    D3DPRESENT_PARAMETERS pp{};
    pp.Windowed = TRUE;
    pp.hDeviceWindow = window.h;
    pp.BackBufferWidth = Width;
    pp.BackBufferHeight = Height;
    pp.BackBufferFormat = D3DFMT_X8R8G8B8;
    pp.BackBufferCount = 1;
    pp.SwapEffect = D3DSWAPEFFECT_DISCARD;
    pp.EnableAutoDepthStencil = TRUE;
    pp.AutoDepthStencilFormat = D3DFMT_D24S8;
    ComPtr<IDirect3DDevice9> dx9;
    const auto deviceResult = d3d->CreateDevice(0, D3DDEVTYPE_HAL, window.h,
                                                D3DCREATE_HARDWARE_VERTEXPROCESSING, &pp, &dx9);
    if (FAILED(deviceResult)) {
        char diagnostic[80];
        std::snprintf(diagnostic, sizeof(diagnostic), "DX9 CreateDevice failed: 0x%08lX",
                      static_cast<unsigned long>(deviceResult));
        throw std::runtime_error(diagnostic);
    }
    if (controls)
        for (auto* effect : {&material, &post}) {
            const auto value = [&](const char* name, unsigned rows, unsigned columns,
                                   const std::vector<float>& values) {
                MmePortableParameter p;
                p.name = name;
                p.rows = rows;
                p.columns = columns;
                p.elements = unsigned(values.size()) / (rows * columns);
                p.value.resize(values.size() * 4);
                std::memcpy(p.value.data(), values.data(), p.value.size());
                effect->parameters.push_back(p);
            };
            value("controlPosition", 1, 4, {.25f, .5f, 0, 1});
            value("controlWorld", 4, 4, {1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1, 0, .25f, .5f, 0, 1});
            value("sharedExposure", 1, 1, {1});
            effect->parameters.back().shared = true;
            value("weights", 1, 2, {.5f, .25f, .5f, .75f});
            value("enabled", 1, 1, {0});
            effect->parameters.back().type = MmePortableParameter::Type::Bool;
            const std::uint32_t enabled = 1;
            std::memcpy(effect->parameters.back().value.data(), &enabled, 4);
        }
    if (sceneScript && !legacyScene) {
        post.profile = "scene-v2";
        post.passes = {{"Post", "Grade", "fullscreen_vs", "grade_ps", {"SceneColor"}},
                       {"Post", "Lift", "fullscreen_vs", "lift_ps", {"Ping"}},
                       {"Post", "Finish", "fullscreen_vs", "finish_ps", {"Pong"}}};
        post.targets = {
            {"SceneColor"}, {"Ping"}, {"Pong"}, {"Depth", MmePortableFormat::Rgba8, true}};
        for (const auto& [name, number] : {std::pair{"iterations", 2}, {"iteration", 0}}) {
            MmePortableParameter p;
            p.name = name;
            p.type = MmePortableParameter::Type::Int;
            p.value.resize(4);
            std::memcpy(p.value.data(), &number, 4);
            post.parameters.push_back(p);
        }
        MmePortableParameter clear;
        clear.name = "clearColor";
        clear.columns = 4;
        const float color[] = {1, 1, 1, 0};
        clear.value.resize(sizeof(color));
        std::memcpy(clear.value.data(), color, sizeof(color));
        post.parameters.push_back(clear);
        using K = MmePortableCommand::Kind;
        post.script = {{K::ColorTarget, "SceneColor", 0},
                       {K::DepthTarget, "Depth"},
                       {K::ClearColorValue, "clearColor"},
                       {K::Clear, "", D3DCLEAR_TARGET},
                       {K::Clear, "", D3DCLEAR_ZBUFFER | D3DCLEAR_STENCIL},
                       {K::Capture},
                       {K::ColorTarget, "Ping", 0},
                       {K::DepthTarget},
                       {K::Clear, "", D3DCLEAR_TARGET},
                       {K::Pass, "", 0},
                       {K::ColorTarget, "Pong", 0},
                       {K::Clear, "", D3DCLEAR_TARGET},
                       {K::Loop, "iterations"},
                       {K::LoopIndex, "iteration"},
                       {K::Pass, "", 1},
                       {K::LoopEnd},
                       {K::ColorTarget},
                       {K::Clear, "", D3DCLEAR_TARGET},
                       {K::Pass, "", 2}};
        std::string reason;
        CHECK(MmePortableSceneLayout(post, reason));
    }
    ComPtr<ID3DXEffectPool> sharedPool;
    if (legacyScene)
        CHECK(SUCCEEDED(D3DXCreateEffectPool(&sharedPool)));
    auto compile = [&](const MmePortableEffect& source) {
        ComPtr<ID3DXEffect> fx;
        ComPtr<ID3DXBuffer> errors;
        const auto path = std::filesystem::path(MDS_TEST_SOURCE_DIR) / L"assets/effects/portable" /
                          (source.post ? (legacyScene   ? L"LegacyScene.fx"
                                          : sceneScript ? L"SceneScript.fx"
                                          : controls    ? L"ControlPost.fx"
                                                        : L"TwoStage.fx")
                                       : (legacy     ? L"LegacyMaterial.fx"
                                          : controls ? L"ControlMaterial.fx"
                                                     : L"MaterialTint.fx"));
        const auto result =
            legacy && !source.post
                ? createEffect(dx9.Get(), source.source->data(), UINT(source.source->size()),
                               nullptr, nullptr, 0, sharedPool.Get(), &fx, &errors)
                : D3DXCreateEffectFromFileW(dx9.Get(), path.c_str(), nullptr, nullptr, 0,
                                            sharedPool.Get(), &fx, &errors);
        if (FAILED(result))
            throw std::runtime_error(errors ? static_cast<const char*>(errors->GetBufferPointer())
                                            : "DX9 FX compile failed");
        if (controls)
            for (const auto& p : source.parameters) {
                if (p.type == MmePortableParameter::Type::Bool)
                    CHECK(SUCCEEDED(fx->SetBool(p.name.c_str(), TRUE)));
                else if (p.type == MmePortableParameter::Type::Int) {
                    int value = 0;
                    std::memcpy(&value, p.value.data(), 4);
                    CHECK(SUCCEEDED(fx->SetInt(p.name.c_str(), value)));
                } else {
                    std::vector<float> values(p.value.size() / 4);
                    std::memcpy(values.data(), p.value.data(), p.value.size());
                    CHECK(SUCCEEDED(
                        fx->SetFloatArray(p.name.c_str(), values.data(), unsigned(values.size()))));
                }
            }
        return fx;
    };
    auto materialFx = compile(material), postFx = compile(post);
    std::string reason;
    std::unique_ptr<mme::SasEffect, decltype(&mme::SasUnload)> importedScene(nullptr,
                                                                             mme::SasUnload);
    if (legacyScene) {
        post.profile = "legacy-scene-v1";
        auto imported = std::make_shared<MmeLegacyProgram>();
        CHECK(MmeLegacyImportScene(*post.source, *imported, reason));
        post.legacy = imported;
        post.passes.clear();
        post.targets.clear();
        post.parameters.clear();
        std::uint64_t identity = 100;
        for (const auto& t : imported->textures)
            post.targets.push_back({t.name, MmePortableFormat::Rgba8,
                                    t.semantic == "RENDERDEPTHSTENCILTARGET", identity++});
        for (const auto& pass : imported->passes) {
            MmePortablePass value{pass.technique, pass.name, pass.vertex, pass.pixel};
            for (auto slot : MmeLegacySamplers(*imported, pass)) {
                value.sampleSlots.push_back(slot);
                value.sampled.push_back(imported->samplers[slot].texture);
            }
            post.passes.push_back(std::move(value));
        }
        for (const auto& v : imported->variables) {
            MmePortableParameter parameter;
            parameter.name = v.name;
            parameter.semantic = v.semantic;
            parameter.rows = v.rows;
            parameter.columns = v.columns;
            parameter.elements = v.elements;
            parameter.value.resize(v.rows * v.columns * v.elements * 4);
            std::vector<float> values(parameter.value.size() / 4);
            CHECK(SUCCEEDED(
                postFx->GetFloatArray(v.name.c_str(), values.data(), UINT(values.size()))));
            std::memcpy(parameter.value.data(), values.data(), parameter.value.size());
            post.parameters.push_back(std::move(parameter));
        }
        importedScene.reset(mme::SasParse(postFx.Get(), "LegacyScene.fx", dx9.Get()));
        CHECK(importedScene && !mme::SasHadErrors(importedScene.get()));
        CHECK(MmePortableExportScene(*importedScene, post, reason));
        material.targets = {{"SharedMask", MmePortableFormat::Rgba8, false, 100}};
        ComPtr<IDirect3DBaseTexture9> sharedBase;
        ComPtr<IDirect3DTexture9> sharedTexture;
        ComPtr<IDirect3DSurface9> sharedSurface;
        CHECK(SUCCEEDED(materialFx->GetTexture("SharedMask", &sharedBase)) && sharedBase);
        CHECK(SUCCEEDED(sharedBase.As(&sharedTexture)));
        CHECK(SUCCEEDED(sharedTexture->GetSurfaceLevel(0, &sharedSurface)));
        CHECK(SUCCEEDED(dx9->SetRenderTarget(0, sharedSurface.Get())));
        CHECK(SUCCEEDED(
            dx9->Clear(0, nullptr, D3DCLEAR_TARGET, D3DCOLOR_ARGB(255, 127, 191, 255), 1, 0)));
    }
    if (legacy) {
        material.profile = "legacy-material-v1";
        auto imported = std::make_shared<MmeLegacyProgram>();
        CHECK(MmeLegacyImport(*material.source, *imported, reason));
        material.legacy = imported;
        material.passes = {{"Main", "Tint", vertexEntry, pixelEntry}};
        for (const auto& v : imported->variables) {
            MmePortableParameter p;
            p.name = v.name;
            p.semantic = v.semantic;
            p.rows = v.rows;
            p.columns = v.columns;
            p.elements = v.elements;
            p.value.resize(v.rows * v.columns * v.elements * 4);
            if (v.type == "bool") {
                p.type = MmePortableParameter::Type::Bool;
                BOOL value = FALSE;
                CHECK(SUCCEEDED(materialFx->GetBool(v.name.c_str(), &value)));
                std::memcpy(p.value.data(), &value, 4);
            } else if (v.type == "int") {
                p.type = MmePortableParameter::Type::Int;
                int value = 0;
                CHECK(SUCCEEDED(materialFx->GetInt(v.name.c_str(), &value)));
                std::memcpy(p.value.data(), &value, 4);
            } else {
                std::vector<float> values(p.value.size() / 4);
                CHECK(SUCCEEDED(materialFx->GetFloatArray(v.name.c_str(), values.data(),
                                                          unsigned(values.size()))));
                std::memcpy(p.value.data(), values.data(), p.value.size());
            }
            material.parameters.push_back(std::move(p));
        }
    }
    CHECK(PortableMmeProgram(material, material.passes[0], reason));
    if (legacy) {
        auto frozen = material;
        frozen.source = std::make_shared<const std::string>(
            "#ifdef MDS_RHI\n#error No modern variant\n#endif\n#include \"Frozen.fxsub\"\n");
        frozen.legacySource = material.source;
        frozen.modernSource.reset();
        CHECK(PortableMmeProgram(frozen, frozen.passes[0], reason));
    }
    for (const auto& p : post.passes)
        CHECK(PortableMmeProgram(post, p, reason));
    // Opt-in metadata cannot hide an unsupported legacy pass state.
    auto bad = material;
    auto text = *bad.source;
    const auto offset = text.find("ZEnable=true");
    text.replace(offset, 12, legacy ? "ZEnable=42" : "ZEnable=false");
    bad.source = std::make_shared<const std::string>(text);
    if (legacy) {
        auto imported = std::make_shared<MmeLegacyProgram>();
        CHECK(MmeLegacyImport(text, *imported, reason));
        bad.legacy = imported;
    }
    CHECK(!PortableMmeProgram(bad, bad.passes[0], reason));
    ComPtr<IDirect3DTexture9> base, lookup, scene, ping;
    const std::uint8_t pixels[] = {255, 0, 0,   64,  0,   255, 0,   128,
                                   0,   0, 255, 192, 255, 255, 255, 254};
    const std::uint8_t lookupPixels[] = {32,  64, 96, 255, 128, 160, 192, 255,
                                         192, 64, 32, 255, 96,  32,  160, 255};
    const auto upload = [&](const std::uint8_t* rgba, ComPtr<IDirect3DTexture9>& texture) {
        CHECK(SUCCEEDED(
            dx9->CreateTexture(2, 2, 1, 0, D3DFMT_A8R8G8B8, D3DPOOL_MANAGED, &texture, nullptr)));
        D3DLOCKED_RECT lock{};
        CHECK(SUCCEEDED(texture->LockRect(0, &lock, nullptr, 0)));
        for (unsigned y = 0; y < 2; ++y)
            for (unsigned x = 0; x < 2; ++x) {
                const auto* p = rgba + (y * 2 + x) * 4;
                auto* b = static_cast<std::uint8_t*>(lock.pBits) + y * lock.Pitch + x * 4;
                b[0] = p[2];
                b[1] = p[1];
                b[2] = p[0];
                b[3] = p[3];
            }
        CHECK(SUCCEEDED(texture->UnlockRect(0)));
    };
    upload(pixels, base);
    if (legacyMulti)
        upload(lookupPixels, lookup);
    CHECK(SUCCEEDED(dx9->CreateTexture(Width, Height, 1, D3DUSAGE_RENDERTARGET, D3DFMT_A8R8G8B8,
                                       D3DPOOL_DEFAULT, &scene, nullptr)));
    CHECK(SUCCEEDED(dx9->CreateTexture(Width, Height, 1, D3DUSAGE_RENDERTARGET, D3DFMT_A8R8G8B8,
                                       D3DPOOL_DEFAULT, &ping, nullptr)));
    ComPtr<IDirect3DSurface9> sceneSurface, pingSurface, finalSurface;
    scene->GetSurfaceLevel(0, &sceneSurface);
    ping->GetSurfaceLevel(0, &pingSurface);
    CHECK(SUCCEEDED(dx9->CreateRenderTarget(Width, Height, D3DFMT_A8R8G8B8, D3DMULTISAMPLE_NONE, 0,
                                            FALSE, &finalSurface, nullptr)));
    const float vertices[][8] = {{-1, 1, .25f, .25f, .5f, 1, 0, 0},
                                 {3, 1, .25f, .25f, .5f, 1, 2, 0},
                                 {-1, -3, .25f, .25f, .5f, 1, 0, 2}};
    const std::array<float, 16> identity{1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1};
    auto legacyWorld = identity, legacyView = identity, legacyProjection = identity;
    D3DXMATRIX legacyWvp{};
    if (legacy) {
        legacyWorld[0] = .75f;
        legacyWorld[5] = .75f;
        legacyWorld[12] = .125f;
        legacyWorld[13] = -.125f;
        legacyView[4] = .125f;
        legacyView[12] = -.0625f;
        legacyProjection[0] = .875f;
        legacyProjection[5] = .9375f;
        legacyProjection[11] = .125f;
        D3DXMATRIX worldView{};
        D3DXMatrixMultiply(&worldView, reinterpret_cast<const D3DXMATRIX*>(legacyWorld.data()),
                           reinterpret_cast<const D3DXMATRIX*>(legacyView.data()));
        D3DXMatrixMultiply(&legacyWvp, &worldView,
                           reinterpret_cast<const D3DXMATRIX*>(legacyProjection.data()));
        D3DXMATRIX cameraView{};
        D3DXMatrixMultiply(&cameraView, reinterpret_cast<const D3DXMATRIX*>(legacyWorld.data()),
                           reinterpret_cast<const D3DXMATRIX*>(legacyView.data()));
        CHECK(SUCCEEDED(materialFx->SetValue("ObjectWorld", identity.data(), 64)));
        CHECK(SUCCEEDED(materialFx->SetValue("CameraView", &cameraView, 64)));
        CHECK(SUCCEEDED(materialFx->SetValue("CameraProjection", legacyProjection.data(), 64)));
    }
    const float size[] = {float(Width), float(Height)}, diffuse[] = {1, 1, 1, 1};
    CHECK(SUCCEEDED(
        materialFx->SetValue(legacy ? "Transform" : "worldViewProjection",
                             legacy ? static_cast<const void*>(&legacyWvp) : identity.data(), 64)));
    materialFx->SetFloatArray(legacy ? "SurfaceSize" : "viewportSize", size, 2);
    materialFx->SetFloatArray(legacy ? "GeometryColor" : "materialDiffuse", diffuse, 4);
    materialFx->SetTexture(legacy ? "Albedo" : "baseTexture", base.Get());
    if (legacyMulti)
        CHECK(SUCCEEDED(materialFx->SetTexture("Lookup", lookup.Get())));
    const std::array<float, 3> sunDirection{.25f, -.5f, .75f};
    if (standard) {
        auto app = std::make_unique<MMDApp>();
        std::memset(&app->state, 0, sizeof(app->state));
        struct HostScope {
            MMDApp* previous;
            ~HostScope() { g_Block = previous; }
        } hostScope{g_Block};
        g_Block = app.get();
        mme::ModelData model(dx9.Get(), 1, "standard.pmx", 1, 2, nullptr);
        model.setCachedPerVertexValue(3);
        auto& snapshot = model.snapshot();
        std::memset(&snapshot, 0, sizeof(snapshot));
        snapshot.draw_type = 1;
        snapshot.base_texture_present = 1;
        snapshot.base_texture = base.Get();
        std::memcpy(&snapshot.model_world, identity.data(), 64);
        std::memcpy(&snapshot.effect_world, identity.data(), 64);
        std::memcpy(&snapshot.world_view_projection, &legacyWvp, 64);
        D3DXMATRIX cameraView{};
        D3DXMatrixMultiply(&cameraView, reinterpret_cast<const D3DXMATRIX*>(legacyWorld.data()),
                           reinterpret_cast<const D3DXMATRIX*>(legacyView.data()));
        std::memcpy(&mme::g_worldViewMatrix, &cameraView, 64);
        std::memcpy(&mme::g_projMatrix, legacyProjection.data(), 64);
        D3DXMATRIX inverse{};
        CHECK(D3DXMatrixInverse(&inverse, nullptr,
                                reinterpret_cast<const D3DXMATRIX*>(legacyWorld.data())));
        std::memcpy(&mme::g_invWorldAtBegin, &inverse, 64);
        mme::g_cachedLight.Type = D3DLIGHT_DIRECTIONAL;
        mme::g_cachedLight.Direction = {sunDirection[0], sunDirection[1], sunDirection[2]};
        mme::g_beginViewport.Width = Width;
        mme::g_beginViewport.Height = Height;
        mme::g_cachedLight.Diffuse = {.25f, .5f, .75f, 0};
        mme::g_cachedLight.Ambient = {.125f, .25f, .5f, 0};
        mme::g_cachedLight.Specular = {.5f, .25f, .125f, 0};
        CHECK(SUCCEEDED(dx9->SetLight(0, &mme::g_cachedLight)));
        D3DMATERIAL9 nativeMaterial{};
        nativeMaterial.Diffuse = {1, 1, 1, 1};
        CHECK(SUCCEEDED(dx9->SetMaterial(&nativeMaterial)));
        material.clock = {1.5f, .125f, 2.25f, -.25f};
        material.clockReady = true;
        material.counts = {3, 2};
        mme::g_frameTimeBase = material.clock[0];
        mme::g_deltaSeconds = material.clock[1];
        mme::g_lastFrameTime = material.clock[2];
        mme::g_frameDelta = material.clock[3];
        mme::MaterialBinding binding{};
        binding.effect = materialFx.Get();
        mme::MmeBindStandardParameters(&model, &binding, snapshot);
    }
    CHECK(SUCCEEDED(dx9->SetRenderTarget(0, sceneSurface.Get())));
    CHECK(SUCCEEDED(
        dx9->Clear(0, nullptr, D3DCLEAR_TARGET | D3DCLEAR_ZBUFFER | D3DCLEAR_STENCIL, 0, 1, 0)));
    CHECK(SUCCEEDED(dx9->BeginScene()));
    CHECK(SUCCEEDED(dx9->SetFVF(D3DFVF_XYZ | D3DFVF_NORMAL | D3DFVF_TEX1)));
    UINT passes = 0;
    materialFx->SetTechnique("Main");
    CHECK(SUCCEEDED(materialFx->Begin(&passes, 0)));
    CHECK(SUCCEEDED(materialFx->BeginPass(0)));
    CHECK(SUCCEEDED(dx9->DrawPrimitiveUP(D3DPT_TRIANGLELIST, 1, vertices, 32)));
    materialFx->EndPass();
    materialFx->End();
    CHECK(SUCCEEDED(dx9->EndScene()));
    const auto legacyMaterial = Read(dx9.Get(), sceneSurface.Get());
    RenderFrame frame;
    const auto access =
        TextureAccess::ColorAttachment | TextureAccess::Sampled | TextureAccess::CopySource;
    frame.targets = {{1, {Width, Height, TextureFormat::Rgba8, access}},
                     {2,
                      {Width, Height, TextureFormat::Depth24Stencil8,
                       unsigned(TextureAccess::DepthStencilAttachment)}}};
    frame.finalColor = 1;
    ModelReplayResource geometry;
    geometry.key = {1, 1};
    geometry.bytes.resize(sizeof(vertices));
    std::memcpy(geometry.bytes.data(), vertices, sizeof(vertices));
    ModelReplayResource texture;
    texture.key = {2, 1};
    texture.kind = ReplayResourceKind::Texture;
    texture.texture.levels.push_back({2, 2, {pixels, pixels + sizeof(pixels)}});
    frame.resources = {geometry, texture};
    if (legacyMulti) {
        auto resource = texture;
        resource.key = {3, 1};
        resource.texture.levels[0].pixels =
            SnapshotBytes(lookupPixels, lookupPixels + sizeof(lookupPixels));
        frame.resources.push_back(std::move(resource));
    }
    ModelReplayDraw draw;
    draw.indexed = false;
    draw.vertices = geometry.key;
    draw.drawVertexCount = 3;
    draw.textures[0] = texture.key;
    draw.pipeline.shader.layout.stages[0].role = MaterialTextureRole::Base;
    if (legacyMulti) {
        draw.textures[1] = frame.resources.back().key;
        draw.pipeline.shader.layout.stages[1].role = MaterialTextureRole::Toon;
    }
    draw.pipeline.shader.world = draw.pipeline.shader.view = draw.pipeline.shader.projection =
        identity;
    if (legacy) {
        draw.pipeline.shader.world = legacyWorld;
        draw.pipeline.shader.view = legacyView;
        draw.pipeline.shader.projection = legacyProjection;
    }
    draw.pipeline.shader.nativeDiffuse = {1, 1, 1, 1};
    if (standard) {
        draw.pipeline.shader.lights[0].direction = sunDirection;
        draw.pipeline.shader.lights[0].diffuse = {.25f, .5f, .75f, 0};
        draw.pipeline.shader.lights[0].ambient = {.125f, .25f, .5f, 0};
        draw.pipeline.shader.lights[0].specular = {.5f, .25f, .125f, 0};
    }
    draw.pipeline.viewport = ModelViewport{0, 0, Width, Height};
    if (legacy) {
        auto invalid = draw;
        invalid.pipeline.viewport.reset();
        CHECK(!ApplyPortableMmeMaterial(invalid, material, reason));
        CHECK(reason == "MME_legacy_viewport_missing" && !invalid.shader);
        invalid = draw;
        invalid.pipeline.shader.layout = {};
        CHECK(!ApplyPortableMmeMaterial(invalid, material, reason));
        CHECK(reason == "MME_legacy_material_texture_unavailable" && !invalid.shader);
        auto missingPass = material;
        auto metadata = std::make_shared<MmeLegacyProgram>(*material.legacy);
        metadata->passes.clear();
        missingPass.legacy = metadata;
        invalid = draw;
        CHECK(!ApplyPortableMmeMaterial(invalid, missingPass, reason));
        CHECK(reason == "MME_legacy_pass_source" && !invalid.shader);
    }
    if (controls) {
        const auto reject = [&](MmePortableEffect bad, const char* expected) {
            auto invalid = draw;
            CHECK(!ApplyPortableMmeMaterial(invalid, bad, reason));
            CHECK(reason == expected);
            CHECK(!invalid.shader); // Failure must leave the draw unchanged.
        };
        auto bad = material;
        bad.parameters.pop_back();
        reject(bad, "MME_parameter_missing_value");
        bad = material;
        bad.parameters[2].value.resize(1);
        reject(bad, "MME_parameter_snapshot_layout");
        bad = material;
        bad.parameters[2].type = MmePortableParameter::Type::Int;
        reject(bad, "MME_parameter_snapshot_type");
    }
    draw.pipeline.shadow.mode = BuiltinShadowMode::Material;
    CHECK(ApplyPortableMmeMaterial(draw, material, reason));
    CHECK(draw.pipeline.shadow.mode == BuiltinShadowMode::None && !draw.shadowTarget);
    if (standard) {
        auto singular = draw;
        singular.pipeline.shader.view = {};
        CHECK(!ApplyPortableMmeMaterial(singular, material, reason));
        CHECK(reason == "MME_standard_singular_matrix");
        // Compare generated constants against the original MME binder, not a
        // second implementation of the expected formulas in the test.
        const auto& description = draw.shader->pipeline.program->description;
        for (const auto& layout : description.bindings)
            if (layout.kind == ShaderBindingKind::Constants && layout.slot == 1) {
                const auto value =
                    std::find_if(draw.shader->constants.begin(), draw.shader->constants.end(),
                                 [](const auto& c) { return c.slot == 1; });
                CHECK(value != draw.shader->constants.end());
                for (const auto& member : layout.members) {
                    auto parameter =
                        std::find_if(material.parameters.begin(), material.parameters.end(),
                                     [&](const auto& p) { return p.name == member.name; });
                    CHECK(parameter != material.parameters.end());
                    if (parameter->type == MmePortableParameter::Type::Float) {
                        std::vector<float> native(parameter->rows * parameter->columns);
                        CHECK(SUCCEEDED(materialFx->GetFloatArray(
                            member.name.c_str(), native.data(), UINT(native.size()))));
                        for (unsigned n = 0; n < native.size(); ++n) {
                            float actual;
                            std::memcpy(&actual, value->bytes.data() + member.offset + n * 4, 4);
                            if (std::abs(actual - native[n]) > 1.e-5f)
                                std::fprintf(stderr, "%s[%u] cpu=%g DX9=%g\n", member.name.c_str(),
                                             n, actual, native[n]);
                            CHECK(std::abs(actual - native[n]) <= 1.e-5f);
                        }
                    } else {
                        int native = 0;
                        CHECK(SUCCEEDED(parameter->type == MmePortableParameter::Type::Int
                                            ? materialFx->GetInt(member.name.c_str(), &native)
                                            : materialFx->GetBool(member.name.c_str(), &native)));
                        int actual;
                        std::memcpy(&actual, value->bytes.data() + member.offset, 4);
                        CHECK(actual == native);
                    }
                }
            }
    }
    FramePass pass;
    pass.color = FrameAttachment{1};
    pass.color->clear = {0, 0, 0, 0};
    pass.depthStencil = FrameAttachment{2};
    pass.draws = {draw};
    if (legacyScene)
        pass.sampled = {SharedFrameTargetId(100)};
    frame.passes = {pass};
    if (legacyScene) {
        CHECK(RegisterPortableMmeTargets(frame, {material, post}, reason));
        FramePass initialize;
        initialize.color = FrameAttachment{SharedFrameTargetId(100)};
        initialize.color->clear = {127.f / 255, 191.f / 255, 1, 1};
        frame.passes.insert(frame.passes.begin(), initialize);
    }
    SceneRenderer renderer(*native.device);
    PreparedModelFrame prepared;
    CompletionToken token;
    auto ready = renderer.Prepare(1, frame, prepared);
    if (!ready)
        throw std::runtime_error(ready.reason);
    CHECK(renderer.Submit(prepared, token));
    AttachmentReadback actual;
    CHECK(renderer.ReadTarget(1, 1, actual));
    Compare(actual.bytes, legacyMaterial, 1, "material");
    // Execute the same two FX passes on DX9, using the original MME quad mapping.
    postFx->SetFloatArray("viewportSize", size, 2);
    if (sceneScript) {
        struct SasOracle {
            mme::SasEffect* sas = nullptr;
            mme::SasRunState* state = nullptr;
            IDirect3DDevice9* device;
            ~SasOracle() {
                if (state) {
                    mme::SasRestoreRunState(device, state);
                    mme::SasDestroyRunState(state);
                }
                if (sas)
                    mme::SasUnload(sas);
                mme::SasSetHostCallbacks({});
            }
        } oracle{nullptr, nullptr, dx9.Get()};
        CHECK(SUCCEEDED(dx9->SetRenderTarget(0, finalSurface.Get())));
        oracle.sas = legacyScene ? importedScene.release()
                                 : mme::SasParse(postFx.Get(), "SceneScript.fx", dx9.Get());
        CHECK(oracle.sas && !mme::SasHadErrors(oracle.sas));
        mme::SasHostCallbacks callbacks{};
        callbacks.ctx = dx9.Get();
        callbacks.RunPass = [](void* context, mme::SasEffect* sas, int kind, int index) -> long {
            CHECK(kind == 1);
            return mme::SasDefaultDrawBufferPass(sas->effect,
                                                 static_cast<IDirect3DDevice9*>(context), index);
        };
        mme::SasSetHostCallbacks(callbacks);
        oracle.state = mme::SasCreateRunState(dx9.Get());
        CHECK(oracle.state && oracle.sas->techniques.size() == 1);
        CHECK(mme::SasExecuteTechniqueStep(oracle.sas, &oracle.sas->techniques[0], dx9.Get(),
                                           oracle.state));
        CHECK(SUCCEEDED(dx9->BeginScene()));
        // ScriptExternal resumes the actual scene draw against the script's
        // clear color. Copying an earlier composited frame would hide Alpha /
        // capture-timing errors in both the implementation and the oracle.
        CHECK(SUCCEEDED(dx9->SetFVF(D3DFVF_XYZ | D3DFVF_NORMAL | D3DFVF_TEX1)));
        CHECK(SUCCEEDED(materialFx->Begin(&passes, 0)));
        CHECK(SUCCEEDED(materialFx->BeginPass(0)));
        CHECK(SUCCEEDED(dx9->DrawPrimitiveUP(D3DPT_TRIANGLELIST, 1, vertices, 32)));
        CHECK(SUCCEEDED(materialFx->EndPass()));
        CHECK(SUCCEEDED(materialFx->End()));
        CHECK(SUCCEEDED(mme::SasResumeTechnique(oracle.sas, &oracle.sas->techniques[0], dx9.Get(),
                                                oracle.state)));
        CHECK(SUCCEEDED(dx9->EndScene()));
        ComPtr<IDirect3DBaseTexture9> pongBase;
        ComPtr<IDirect3DTexture9> pong;
        CHECK(SUCCEEDED(postFx->GetTexture(legacyScene ? "Graded" : "Pong", &pongBase)));
        CHECK(SUCCEEDED(pongBase.As(&pong)));
        pingSurface.Reset();
        CHECK(SUCCEEDED(pong->GetSurfaceLevel(0, &pingSurface)));
    } else {
        postFx->SetTexture("SceneColor", scene.Get());
        postFx->SetTexture("Ping", ping.Get());
        postFx->SetTechnique("Post");
        CHECK(SUCCEEDED(dx9->BeginScene()));
        CHECK(SUCCEEDED(postFx->Begin(&passes, 0)));
        CHECK(passes == 2);
        const float left = -1 - 2.f / Width, top = 1 + 2.f / Height, u = -1.f / Width,
                    v = -1.f / Height;
        const float quad[][8] = {{1, -1, 0, 0, 0, 0, 1, 1},
                                 {left, -1, 0, 0, 0, 0, u, 1},
                                 {left, top, 0, 0, 0, 0, u, v},
                                 {1, top, 0, 0, 0, 0, 1, v}};
        for (unsigned p = 0; p < 2; ++p) {
            CHECK(SUCCEEDED(dx9->SetRenderTarget(0, p ? finalSurface.Get() : pingSurface.Get())));
            CHECK(SUCCEEDED(postFx->BeginPass(p)));
            CHECK(SUCCEEDED(dx9->DrawPrimitiveUP(D3DPT_TRIANGLEFAN, 2, quad, 32)));
            CHECK(SUCCEEDED(postFx->EndPass()));
        }
        postFx->End();
        CHECK(SUCCEEDED(dx9->EndScene()));
    }
    const auto legacyFinal = Read(dx9.Get(), finalSurface.Get());
    CHECK(legacyFinal == Read(dx9.Get(), pingSurface.Get()));
    RenderFrame missingTarget;
    missingTarget.finalColor = 123;
    CHECK(!AppendPortableMmePost(missingTarget, post, reason));
    CHECK(reason == "MME_post_missing_scene_color" && missingTarget.targets.empty());
    CHECK(AppendPortableMmePost(frame, post, reason));
    if (legacyScene)
        for (const auto& target : frame.targets)
            CHECK(target.id < (std::uint64_t(1) << 62) ||
                  std::any_of(
                      post.targets.begin(), post.targets.end(),
                      [&](const auto& t) {
                          return t.identity && SharedFrameTargetId(t.identity) == target.id;
                      }));
    ready = renderer.Prepare(1, frame, prepared);
    if (!ready)
        throw std::runtime_error(ready.reason);
    CHECK(renderer.Submit(prepared, token));
    CHECK(renderer.ReadTarget(1, frame.finalColor, actual));
    Compare(actual.bytes, legacyFinal, 2, "two-stage post");
    AttachmentReadback intermediate;
    CHECK(renderer.ReadTarget(1,
                              legacyScene   ? SharedFrameTargetId(102)
                              : sceneScript ? 17
                                            : 16,
                              intermediate));
    CHECK(actual.bytes == intermediate.bytes);
    for (unsigned n = 0; n < 12; ++n)
        CHECK(renderer.Submit(prepared, token));
    CHECK(renderer.ReadTarget(1, frame.finalColor, intermediate));
    CHECK(actual.bytes == intermediate.bytes);
    CHECK(renderer.Detach());
#ifdef MIKUDANCESTUDIO_RHI_D3D12
    if (options.backend == Backend::D3D12)
        CHECK(QueryD3D12Stats(*native.device).debugErrors == 0);
#endif
#ifdef MIKUDANCESTUDIO_RHI_VULKAN
    if (options.backend == Backend::Vulkan)
        CHECK(QueryVulkanStats(*native.device).validationErrors == 0);
#endif
    CHECK(!IsWindowVisible(window.h));
    std::printf("%s public FX: DX9 comparison, strict final copy and repeat passed\n", api.c_str());
    return 0;
} catch (const std::exception& e) {
    std::fprintf(stderr, "%s\n", e.what());
    return 1;
}
