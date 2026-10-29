#define NOMINMAX
#include <Windows.h>
#include <d3dx9.h>
#include <wrl/client.h>
#include "mikudancestudio/mme_portable.hpp"
#include "mikudancestudio/rhi_backend.hpp"
#include "mikudancestudio/rhi_float.hpp"
#include "mikudancestudio/rhi_post_process.hpp"
#include "mikudancestudio/rhi_scene_renderer.hpp"
#include "mme_portable_api.h"
#include <filesystem>
#include <fstream>
#include <iterator>
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
constexpr unsigned Width = 48, Height = 32;
std::vector<std::uint8_t> Read(IDirect3DDevice9* d, IDirect3DSurface9* surface, bool half) {
    ComPtr<IDirect3DSurface9> cpu;
    CHECK(SUCCEEDED(d->CreateOffscreenPlainSurface(Width, Height,
                                                   half ? D3DFMT_A16B16G16R16F : D3DFMT_A8R8G8B8,
                                                   D3DPOOL_SYSTEMMEM, &cpu, nullptr)));
    CHECK(SUCCEEDED(d->GetRenderTargetData(surface, cpu.Get())));
    D3DLOCKED_RECT lock{};
    CHECK(SUCCEEDED(cpu->LockRect(&lock, nullptr, D3DLOCK_READONLY)));
    const unsigned stride = half ? 8 : 4;
    std::vector<std::uint8_t> out(Width * Height * stride);
    for (unsigned y = 0; y < Height; ++y)
        for (unsigned x = 0; x < Width; ++x) {
            const auto* p =
                static_cast<const std::uint8_t*>(lock.pBits) + y * lock.Pitch + x * stride;
            auto* q = out.data() + (y * Width + x) * stride;
            if (half)
                std::memcpy(q, p, 8);
            else {
                q[0] = p[2];
                q[1] = p[1];
                q[2] = p[0];
                q[3] = p[3];
            }
        }
    cpu->UnlockRect();
    return out;
}
void CompareHalf(const std::vector<std::uint8_t>& actual, const std::vector<std::uint8_t>& expected,
                 const char* label) {
    CHECK(actual.size() == expected.size());
    float maxUlps = 0;
    for (std::size_t n = 0; n < actual.size(); n += 2) {
        std::uint16_t a, b;
        std::memcpy(&a, actual.data() + n, 2);
        std::memcpy(&b, expected.data() + n, 2);
        const float va = HalfToFloat(a), vb = HalfToFloat(b);
        CHECK(std::isfinite(va) && std::isfinite(vb));
        const float ulp = std::ldexp(
            1.f, std::max(-24, std::ilogb(std::max(std::abs(vb), std::ldexp(1.f, -24))) - 10));
        maxUlps = std::max(maxUlps, std::abs(va - vb) / ulp);
    }
    std::printf("%s DX9 max half ULP=%.3f (limit=1)\n", label, maxUlps);
    CHECK(maxUlps <= 1);
}
}
int main(int argc, char** argv) try {
    SetErrorMode(SEM_FAILCRITICALERRORS | SEM_NOGPFAULTERRORBOX);
    const std::string api = argc > 1 ? argv[1] : "dx11";
    BackendOptions options;
    options.backend = api == "dx11"   ? Backend::D3D11
                      : api == "dx12" ? Backend::D3D12
                                      : Backend::Vulkan;
    auto native = CreateOffscreenBackendDevice(Width, Height, options);
    CHECK(native);
    if (!native.device->ShaderFeatures().hlsl)
        return 77;
    struct Window {
        HWND h =
            CreateWindowExW(WS_EX_NOACTIVATE, L"STATIC", L"Hidden MRT FX oracle", WS_OVERLAPPED, 0,
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
    pp.BackBufferWidth = Width;
    pp.BackBufferHeight = Height;
    pp.BackBufferCount = 1;
    pp.BackBufferFormat = D3DFMT_X8R8G8B8;
    pp.SwapEffect = D3DSWAPEFFECT_DISCARD;
    ComPtr<IDirect3DDevice9> dx9;
    CHECK(SUCCEEDED(d3d->CreateDevice(0, D3DDEVTYPE_HAL, window.h,
                                      D3DCREATE_HARDWARE_VERTEXPROCESSING, &pp, &dx9)));
    const auto path =
        std::filesystem::path(MDS_TEST_SOURCE_DIR) / L"assets/effects/portable/MrtHdr.fx";
    std::ifstream file(path, std::ios::binary);
    MmePortableEffect effect;
    effect.post = true;
    effect.profile = "mrt-hdr-v1";
    effect.source = std::make_shared<const std::string>(std::istreambuf_iterator<char>(file),
                                                        std::istreambuf_iterator<char>());
    effect.passes = {{"Post", "Split", "fullscreen_vs", "split_ps"},
                     {"Post", "Combine", "fullscreen_vs", "combine_ps"},
                     {"Post", "ToneMap", "fullscreen_vs", "tone_ps"},
                     {"Post", "Finish", "fullscreen_vs", "finish_ps"}};
    effect.multiply = {.5f, .75f, 1, .5f};
    effect.add = {.125f, .0625f, 0, .125f};
    std::string reason;
    CHECK(MmePortablePostLayout(effect, reason));
    ComPtr<ID3DXEffect> fx;
    ComPtr<ID3DXBuffer> errors;
    const auto hr = D3DXCreateEffectFromFileW(dx9.Get(), path.c_str(), nullptr, nullptr, 0, nullptr,
                                              &fx, &errors);
    if (FAILED(hr))
        throw std::runtime_error(errors ? static_cast<const char*>(errors->GetBufferPointer())
                                        : "FX compile");
    std::array<ComPtr<IDirect3DTexture9>, 6> images;
    std::array<ComPtr<IDirect3DSurface9>, 6> surfaces;
    const char* names[] = {"SceneColor", "LayerA", "LayerB", "Hdr", "Tone", "FinalColor"};
    for (unsigned n = 0; n < images.size(); ++n) {
        CHECK(
            SUCCEEDED(dx9->CreateTexture(Width, Height, 1, D3DUSAGE_RENDERTARGET,
                                         n >= 1 && n <= 3 ? D3DFMT_A16B16G16R16F : D3DFMT_A8R8G8B8,
                                         D3DPOOL_DEFAULT, &images[n], nullptr)));
        CHECK(SUCCEEDED(images[n]->GetSurfaceLevel(0, &surfaces[n])));
        if (n < 5)
            CHECK(SUCCEEDED(fx->SetTexture(names[n], images[n].Get())));
    }
    CHECK(SUCCEEDED(dx9->SetDepthStencilSurface(nullptr)));
    CHECK(SUCCEEDED(dx9->SetRenderTarget(0, surfaces[0].Get())));
    const D3DCOLOR colors[] = {D3DCOLOR_ARGB(64, 32, 128, 192), D3DCOLOR_ARGB(128, 192, 32, 64),
                               D3DCOLOR_ARGB(192, 128, 192, 32), D3DCOLOR_ARGB(224, 64, 64, 128)};
    for (unsigned n = 0; n < 4; ++n) {
        D3DRECT rect{LONG(n % 2 * Width / 2), LONG(n / 2 * Height / 2),
                     LONG((n % 2 + 1) * Width / 2), LONG((n / 2 + 1) * Height / 2)};
        CHECK(SUCCEEDED(dx9->Clear(1, &rect, D3DCLEAR_TARGET, colors[n], 1, 0)));
    }
    const auto input = Read(dx9.Get(), surfaces[0].Get(), false);
    const float size[] = {float(Width), float(Height)};
    fx->SetFloatArray("viewportSize", size, 2);
    fx->SetTechnique("Post");
    const float left = -1 - 2.f / Width, top = 1 + 2.f / Height, u = -1.f / Width,
                v = -1.f / Height;
    const float quad[][8] = {{1, -1, 0, 0, 0, 0, 1, 1},
                             {left, -1, 0, 0, 0, 0, u, 1},
                             {left, top, 0, 0, 0, 0, u, v},
                             {1, top, 0, 0, 0, 0, 1, v}};
    CHECK(SUCCEEDED(dx9->SetFVF(D3DFVF_XYZ | D3DFVF_NORMAL | D3DFVF_TEX1)));
    CHECK(SUCCEEDED(dx9->BeginScene()));
    UINT count = 0;
    CHECK(SUCCEEDED(fx->Begin(&count, 0)));
    CHECK(count == 4);
    for (unsigned p = 0; p < 4; ++p) {
        for (unsigned slot = 0; slot < 16; ++slot)
            dx9->SetTexture(slot, nullptr);
        CHECK(SUCCEEDED(dx9->SetRenderTarget(1, nullptr)));
        CHECK(SUCCEEDED(dx9->SetRenderTarget(0, surfaces[p == 0 ? 1 : p + 2].Get())));
        if (p == 0)
            CHECK(SUCCEEDED(dx9->SetRenderTarget(1, surfaces[2].Get())));
        CHECK(SUCCEEDED(fx->BeginPass(p)));
        CHECK(SUCCEEDED(dx9->DrawPrimitiveUP(D3DPT_TRIANGLEFAN, 2, quad, 32)));
        CHECK(SUCCEEDED(fx->EndPass()));
    }
    fx->End();
    CHECK(SUCCEEDED(dx9->EndScene()));
    std::array<std::vector<std::uint8_t>, 5> reference;
    for (unsigned n = 1; n < 6; ++n)
        reference[n - 1] = Read(dx9.Get(), surfaces[n].Get(), n <= 3);
    CHECK(reference[3] == reference[4]);
    RenderFrame frame;
    const auto access =
        TextureAccess::ColorAttachment | TextureAccess::Sampled | TextureAccess::CopySource;
    frame.targets = {{1, {Width, Height, TextureFormat::Rgba8, access}}};
    frame.finalColor = 1;
    ModelReplayResource upload;
    upload.kind = ReplayResourceKind::Texture;
    upload.key = {1, 1};
    upload.texture.levels.push_back({Width, Height, input});
    frame.resources.push_back(upload);
    ModelReplayDraw draw;
    CHECK(AttachFullscreenTriangle(frame, draw));
    draw.shader = ReplayShader{};
    auto& shader = *draw.shader;
    shader.pipeline.program = PortableMmeProgram(effect, effect.passes.back(), reason);
    CHECK(shader.pipeline.program);
    shader.pipeline.viewport = ModelViewport{0, 0, Width, Height, 0, 1};
    shader.pipeline.raster.cull = BatchCull::None;
    shader.pipeline.raster.depthTest = shader.pipeline.raster.depthWrite =
        shader.pipeline.raster.alphaBlend = false;
    SnapshotBytes constants;
    constants.resize(32);
    constants.Seal();
    shader.constants.push_back({0, constants});
    shader.textures.push_back({0, upload.key, {}});
    shader.samplers.push_back({0,
                               {SamplerFilter::Point, SamplerFilter::Point, SamplerFilter::Point,
                                SamplerAddress::Clamp, SamplerAddress::Clamp, 1}});
    FramePass capture;
    capture.color = FrameAttachment{1};
    capture.draws = {draw};
    frame.passes = {capture};
    const auto base = frame;
    CHECK(AppendPortableMmePost(frame, effect, reason));
    CHECK(frame.passes.size() == 5);
    CHECK(frame.passes[1].colors.size() == 2 && frame.passes[2].sampled.size() == 2);
    auto feedback = effect;
    feedback.passes[0].sampled = {"LayerA"};
    auto unchanged = base;
    CHECK(!AppendPortableMmePost(unchanged, feedback, reason));
    CHECK(unchanged.targets.size() == 1 && unchanged.passes.size() == 1);
    SceneRenderer renderer(*native.device);
    PreparedModelFrame prepared;
    CompletionToken token;
    const auto ready = renderer.Prepare(1, frame, prepared);
    if (!ready)
        throw std::runtime_error(ready.reason + ": " + native.device->ShaderDiagnostic());
    CHECK(renderer.Submit(prepared, token));
    for (unsigned n = 0; n < 5; ++n) {
        AttachmentReadback actual;
        CHECK(renderer.ReadTarget(1, 16 + n, actual));
        if (n < 3)
            CompareHalf(actual.bytes, reference[n], names[n + 1]);
        else {
            CHECK(actual.bytes.size() == reference[n].size());
            unsigned maximum = 0;
            for (std::size_t i = 0; i < actual.bytes.size(); ++i)
                maximum = std::max(maximum,
                                   unsigned(std::abs(int(actual.bytes[i]) - int(reference[n][i]))));
            std::printf("%s DX9 max UNORM8 delta=%u (limit=2)\n", names[n + 1], maximum);
            CHECK(maximum <= 2);
        }
    }
    AttachmentReadback final, tone;
    CHECK(renderer.ReadTarget(1, 20, final) && renderer.ReadTarget(1, 19, tone));
    CHECK(final.bytes == tone.bytes);
    for (unsigned n = 0; n < 8; ++n)
        CHECK(renderer.Submit(prepared, token));
    CHECK(renderer.ReadTarget(1, 20, tone) && final.bytes == tone.bytes);
    CHECK(renderer.Detach());
    CHECK(!IsWindowVisible(window.h));
    std::printf("PASS %s public MRT/HDR FX and DX9 raw-float oracle\n", api.c_str());
} catch (const std::exception& e) {
    std::fprintf(stderr, "%s\n", e.what());
    return 1;
}
