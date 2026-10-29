#define NOMINMAX
#include <Windows.h>
#include <d3dx9.h>
#include <wrl/client.h>
#include "mikudancestudio/rhi_backend.hpp"
#include "mikudancestudio/rhi_scene_renderer.hpp"
#include "mikudancestudio/mme_portable.hpp"
#include "mme_portable_api.h"
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
    if (post) {
        out.passes = {{"Post", "Grade", "fullscreen_vs", "grade_ps"},
                      {"Post", "Finish", "fullscreen_vs", "finish_ps"}};
        out.multiply = {.5f, .75f, 1, .5f};
        out.add = {.125f, .0625f, 0, .125f};
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
    CHECK(maximum <= limit);
}
}
int main(int argc, char** argv) try {
    SetErrorMode(SEM_FAILCRITICALERRORS | SEM_NOGPFAULTERRORBOX);
    const std::string api = argc > 1 ? argv[1] : "dx11";
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
    struct Window {
        HWND h = CreateWindowExW(WS_EX_NOACTIVATE, L"STATIC", L"Hidden portable FX oracle",
                                 WS_OVERLAPPED, 0, 0, 64, 64, nullptr, nullptr,
                                 GetModuleHandleW(nullptr), nullptr);
        ~Window() { DestroyWindow(h); }
    } window;
    CHECK(window.h && !IsWindowVisible(window.h));
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
    CHECK(SUCCEEDED(d3d->CreateDevice(0, D3DDEVTYPE_HAL, window.h,
                                      D3DCREATE_HARDWARE_VERTEXPROCESSING, &pp, &dx9)));
    const auto material = Effect(L"MaterialTint.fx", false), post = Effect(L"TwoStage.fx", true);
    auto compile = [&](const MmePortableEffect& source) {
        ComPtr<ID3DXEffect> fx;
        ComPtr<ID3DXBuffer> errors;
        const auto path = std::filesystem::path(MDS_TEST_SOURCE_DIR) / L"assets/effects/portable" /
                          (source.post ? L"TwoStage.fx" : L"MaterialTint.fx");
        const auto result = D3DXCreateEffectFromFileW(dx9.Get(), path.c_str(), nullptr, nullptr, 0,
                                                      nullptr, &fx, &errors);
        if (FAILED(result))
            throw std::runtime_error(errors ? static_cast<const char*>(errors->GetBufferPointer())
                                            : "DX9 FX compile failed");
        return fx;
    };
    auto materialFx = compile(material), postFx = compile(post);
    std::string reason;
    CHECK(PortableMmeProgram(material, material.passes[0], reason));
    for (const auto& p : post.passes)
        CHECK(PortableMmeProgram(post, p, reason));
    // Opt-in metadata cannot hide an unsupported legacy pass state.
    auto bad = material;
    auto text = *bad.source;
    const auto offset = text.find("ZEnable=true");
    text.replace(offset, 12, "ZEnable=false");
    bad.source = std::make_shared<const std::string>(text);
    CHECK(!PortableMmeProgram(bad, bad.passes[0], reason));
    ComPtr<IDirect3DTexture9> base, scene, ping;
    CHECK(SUCCEEDED(
        dx9->CreateTexture(2, 2, 1, 0, D3DFMT_A8R8G8B8, D3DPOOL_MANAGED, &base, nullptr)));
    const std::uint8_t pixels[] = {255, 0, 0,   64,  0,   255, 0,   128,
                                   0,   0, 255, 192, 255, 255, 255, 254};
    D3DLOCKED_RECT lock{};
    CHECK(SUCCEEDED(base->LockRect(0, &lock, nullptr, 0)));
    for (unsigned y = 0; y < 2; ++y)
        for (unsigned x = 0; x < 2; ++x) {
            const auto* p = pixels + (y * 2 + x) * 4;
            auto* b = static_cast<std::uint8_t*>(lock.pBits) + y * lock.Pitch + x * 4;
            b[0] = p[2];
            b[1] = p[1];
            b[2] = p[0];
            b[3] = p[3];
        }
    CHECK(SUCCEEDED(base->UnlockRect(0)));
    CHECK(SUCCEEDED(dx9->CreateTexture(Width, Height, 1, D3DUSAGE_RENDERTARGET, D3DFMT_A8R8G8B8,
                                       D3DPOOL_DEFAULT, &scene, nullptr)));
    CHECK(SUCCEEDED(dx9->CreateTexture(Width, Height, 1, D3DUSAGE_RENDERTARGET, D3DFMT_A8R8G8B8,
                                       D3DPOOL_DEFAULT, &ping, nullptr)));
    ComPtr<IDirect3DSurface9> sceneSurface, pingSurface, finalSurface;
    scene->GetSurfaceLevel(0, &sceneSurface);
    ping->GetSurfaceLevel(0, &pingSurface);
    CHECK(SUCCEEDED(dx9->CreateRenderTarget(Width, Height, D3DFMT_A8R8G8B8, D3DMULTISAMPLE_NONE, 0,
                                            FALSE, &finalSurface, nullptr)));
    const float vertices[][8] = {
        {-1, 1, .25f, 0, 0, 0, 0, 0}, {3, 1, .25f, 0, 0, 0, 2, 0}, {-1, -3, .25f, 0, 0, 0, 0, 2}};
    const std::array<float, 16> identity{1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1};
    const float size[] = {float(Width), float(Height)}, diffuse[] = {1, 1, 1, 1};
    CHECK(SUCCEEDED(materialFx->SetValue("worldViewProjection", identity.data(), 64)));
    materialFx->SetFloatArray("viewportSize", size, 2);
    materialFx->SetFloatArray("materialDiffuse", diffuse, 4);
    materialFx->SetTexture("baseTexture", base.Get());
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
    ModelReplayDraw draw;
    draw.indexed = false;
    draw.vertices = geometry.key;
    draw.drawVertexCount = 3;
    draw.textures[0] = texture.key;
    draw.pipeline.shader.layout.stages[0].role = MaterialTextureRole::Base;
    draw.pipeline.shader.world = draw.pipeline.shader.view = draw.pipeline.shader.projection =
        identity;
    draw.pipeline.shader.nativeDiffuse = {1, 1, 1, 1};
    CHECK(ApplyPortableMmeMaterial(draw, material, reason));
    FramePass pass;
    pass.color = FrameAttachment{1};
    pass.color->clear = {0, 0, 0, 0};
    pass.depthStencil = FrameAttachment{2};
    pass.draws = {draw};
    frame.passes = {pass};
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
    const auto legacyFinal = Read(dx9.Get(), finalSurface.Get());
    CHECK(legacyFinal == Read(dx9.Get(), pingSurface.Get()));
    RenderFrame missingTarget;
    missingTarget.finalColor = 123;
    CHECK(!AppendPortableMmePost(missingTarget, post, reason));
    CHECK(reason == "MME_post_missing_scene_color" && missingTarget.targets.empty());
    CHECK(AppendPortableMmePost(frame, post, reason));
    ready = renderer.Prepare(1, frame, prepared);
    if (!ready)
        throw std::runtime_error(ready.reason);
    CHECK(renderer.Submit(prepared, token));
    CHECK(renderer.ReadTarget(1, frame.finalColor, actual));
    Compare(actual.bytes, legacyFinal, 2, "two-stage post");
    AttachmentReadback intermediate;
    CHECK(renderer.ReadTarget(1, 16, intermediate));
    CHECK(actual.bytes == intermediate.bytes);
    for (unsigned n = 0; n < 12; ++n)
        CHECK(renderer.Submit(prepared, token));
    CHECK(renderer.ReadTarget(1, frame.finalColor, intermediate));
    CHECK(actual.bytes == intermediate.bytes);
    CHECK(renderer.Detach());
    CHECK(!IsWindowVisible(window.h));
    std::printf("%s public FX: DX9 comparison, strict final copy and repeat passed\n", api.c_str());
    return 0;
} catch (const std::exception& e) {
    std::fprintf(stderr, "%s\n", e.what());
    return 1;
}
