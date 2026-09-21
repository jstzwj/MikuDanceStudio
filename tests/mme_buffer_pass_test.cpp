#include "pass_planner.h"
#include "material_bind.h"
#include "mme_context.h"
#include "mme_globals.h"
#include "model_data.h"
#include "sas_exec.h"
#include <d3dx9.h>
#include <cstdio>
#include <cstring>

namespace {
template<class T> struct Com {
    T* p = nullptr;
    ~Com() { if (p) p->Release(); }
    T* operator->() const { return p; }
};
struct Window {
    HWND handle = CreateWindowW(L"STATIC", L"MME buffer regression", WS_OVERLAPPED,
                                0, 0, 64, 64, nullptr, nullptr,
                                GetModuleHandleW(nullptr), nullptr);
    ~Window() { if (handle) DestroyWindow(handle); }
};
struct Module {
#ifdef _WIN64
    HMODULE handle = LoadLibraryW(L"d3dx9_43.dll");
#else
    HMODULE handle = LoadLibraryW(L"d3dx9_32.dll");
#endif
    ~Module() { if (handle) FreeLibrary(handle); }
};
}

// A real HAL test: shader compilation and successful DrawPrimitiveUP alone do
// not prove that a pixel survived depth/stencil/scissor tests. Exercise the
// production Draw=Buffer callback against a deliberately hostile host state.
int main() {
    Window window;
    Module d3dx;
    if (!d3dx.handle) {
        std::fprintf(stderr, "Required D3DX runtime is unavailable\n");
        return 1;
    }
    if (!window.handle) {
        std::puts("SKIP: test window unavailable");
        return 77;
    }
    Com<IDirect3D9> d3d;
    d3d.p = Direct3DCreate9(D3D_SDK_VERSION);
    if (!d3d.p) {
        std::puts("SKIP: Direct3D unavailable");
        return 77;
    }
    D3DPRESENT_PARAMETERS pp = {};
    pp.BackBufferWidth = pp.BackBufferHeight = 64;
    pp.BackBufferFormat = D3DFMT_A8R8G8B8;
    pp.BackBufferCount = 1;
    pp.SwapEffect = D3DSWAPEFFECT_DISCARD;
    pp.hDeviceWindow = window.handle;
    pp.Windowed = TRUE;
    pp.EnableAutoDepthStencil = TRUE;
    pp.AutoDepthStencilFormat = D3DFMT_D24S8;
    pp.PresentationInterval = D3DPRESENT_INTERVAL_IMMEDIATE;
    Com<IDirect3DDevice9> device;
#define REQUIRE_HR(call) do { const HRESULT result = (call); if (FAILED(result)) { \
    std::fprintf(stderr, "%s failed: 0x%08lX\n", #call, static_cast<unsigned long>(result)); return 1; } } while (0)
    const HRESULT deviceResult = d3d->CreateDevice(D3DADAPTER_DEFAULT, D3DDEVTYPE_HAL,
        window.handle, D3DCREATE_HARDWARE_VERTEXPROCESSING, &pp, &device.p);
    if (FAILED(deviceResult)) {
        std::printf("SKIP: required HAL device unavailable: 0x%08lX\n",
                    static_cast<unsigned long>(deviceResult));
        return 77;
    }

    using CreateEffect = HRESULT (WINAPI*)(IDirect3DDevice9*, LPCVOID, UINT,
        const D3DXMACRO*, ID3DXInclude*, DWORD, ID3DXEffectPool*, ID3DXEffect**, ID3DXBuffer**);
    const auto createEffect = reinterpret_cast<CreateEffect>(
        GetProcAddress(d3dx.handle, "D3DXCreateEffect"));
    if (!createEffect) return 1;
    const char source[] =
        "float4 VS(float4 p : POSITION) : POSITION { return p; }\n"
        "float4 PS() : COLOR0 { return float4(0,1,0,1); }\n"
        "technique T { pass P { VertexShader=compile vs_3_0 VS();"
        "PixelShader=compile ps_3_0 PS(); } }\n";
    Com<ID3DXEffect> effect;
    Com<ID3DXBuffer> errors;
    HRESULT compiled = createEffect(device.p, source, static_cast<UINT>(std::strlen(source)),
        nullptr, nullptr, 0, nullptr, &effect.p, &errors.p);
    if (FAILED(compiled)) {
        std::fprintf(stderr, "Effect compilation: %s\n",
                     errors.p ? static_cast<const char*>(errors->GetBufferPointer()) : "failed");
        return 1;
    }
    REQUIRE_HR(effect->SetTechnique(effect->GetTechnique(0)));
    Com<IDirect3DVertexDeclaration9> declaration;
    const D3DVERTEXELEMENT9 elements[] = {
        {0, 0, D3DDECLTYPE_FLOAT3, D3DDECLMETHOD_DEFAULT, D3DDECLUSAGE_POSITION, 0},
        D3DDECL_END()
    };
    REQUIRE_HR(device->CreateVertexDeclaration(elements, &declaration.p));
    Com<IDirect3DVertexBuffer9> vertices;
    Com<IDirect3DIndexBuffer9> indices;
    REQUIRE_HR(device->CreateVertexBuffer(256, 0, 0, D3DPOOL_MANAGED, &vertices.p, nullptr));
    REQUIRE_HR(device->CreateIndexBuffer(12, 0, D3DFMT_INDEX16, D3DPOOL_MANAGED, &indices.p, nullptr));
    REQUIRE_HR(device->SetVertexDeclaration(declaration.p));
    REQUIRE_HR(device->SetStreamSource(0, vertices.p, 12, 32));
    REQUIRE_HR(device->SetIndices(indices.p));
    REQUIRE_HR(device->SetRenderState(D3DRS_ZENABLE, D3DZB_TRUE));
    REQUIRE_HR(device->SetRenderState(D3DRS_ZFUNC, D3DCMP_LESS));
    REQUIRE_HR(device->SetRenderState(D3DRS_FILLMODE, D3DFILL_SOLID));
    REQUIRE_HR(device->SetRenderState(D3DRS_LIGHTING, TRUE));
    REQUIRE_HR(device->SetRenderState(D3DRS_STENCILENABLE, TRUE));
    REQUIRE_HR(device->SetRenderState(D3DRS_STENCILFUNC, D3DCMP_NEVER));
    REQUIRE_HR(device->SetRenderState(D3DRS_CULLMODE, D3DCULL_NONE));
    REQUIRE_HR(device->Clear(0, nullptr, D3DCLEAR_TARGET | D3DCLEAR_ZBUFFER | D3DCLEAR_STENCIL,
                           0xffff0000, 0.0f, 0));
    const RECT centerScissor = {16, 16, 48, 48};
    REQUIRE_HR(device->SetScissorRect(&centerScissor));
    REQUIRE_HR(device->SetRenderState(D3DRS_SCISSORTESTENABLE, TRUE));
    REQUIRE_HR(device->SetRenderState(D3DRS_MULTISAMPLEANTIALIAS, TRUE));
    REQUIRE_HR(device->BeginScene());
    HRESULT drawn = mme::SasDefaultDrawBufferPass(effect.p, device.p, 0);
    REQUIRE_HR(device->EndScene());
    REQUIRE_HR(drawn);

    int failures = 0;
    const auto check = [&](bool ok, const char* message) {
        if (!ok) { std::fprintf(stderr, "%s\n", message); ++failures; }
    };
    const D3DRENDERSTATETYPE states[] = {D3DRS_ZENABLE, D3DRS_FILLMODE, D3DRS_LIGHTING,
        D3DRS_STENCILENABLE, D3DRS_SCISSORTESTENABLE, D3DRS_MULTISAMPLEANTIALIAS};
    const DWORD expected[] = {D3DZB_TRUE, D3DFILL_SOLID, TRUE, TRUE, TRUE, TRUE};
    for (int i = 0; i < 6; ++i) {
        DWORD actual = 0;
        REQUIRE_HR(device->GetRenderState(states[i], &actual));
        if (actual != expected[i]) {
            std::fprintf(stderr, "Render state %u not restored: %lu != %lu\n",
                         static_cast<unsigned>(states[i]), actual, expected[i]);
            ++failures;
        }
    }
    Com<IDirect3DVertexDeclaration9> restoredDecl;
    Com<IDirect3DVertexBuffer9> restoredStream;
    Com<IDirect3DIndexBuffer9> restoredIndices;
    UINT offset = 0, stride = 0;
    REQUIRE_HR(device->GetVertexDeclaration(&restoredDecl.p));
    REQUIRE_HR(device->GetStreamSource(0, &restoredStream.p, &offset, &stride));
    REQUIRE_HR(device->GetIndices(&restoredIndices.p));
    check(restoredDecl.p == declaration.p, "Vertex declaration not restored");
    check(restoredStream.p == vertices.p && offset == 12 && stride == 32,
          "Stream 0 buffer/offset/stride not restored");
    check(restoredIndices.p == indices.p, "Index buffer not restored");

    Com<IDirect3DSurface9> backBuffer;
    Com<IDirect3DSurface9> readback;
    REQUIRE_HR(device->GetBackBuffer(0, 0, D3DBACKBUFFER_TYPE_MONO, &backBuffer.p));
    REQUIRE_HR(device->CreateOffscreenPlainSurface(64, 64, D3DFMT_A8R8G8B8,
                                                  D3DPOOL_SYSTEMMEM, &readback.p, nullptr));
    REQUIRE_HR(device->GetRenderTargetData(backBuffer.p, readback.p));
    D3DLOCKED_RECT locked = {};
    REQUIRE_HR(readback->LockRect(&locked, nullptr, D3DLOCK_READONLY));
    DWORD pixel = 0;
    DWORD outsidePixel = 0;
    std::memcpy(&pixel, static_cast<const char*>(locked.pBits) + 32 * locked.Pitch + 32 * 4, 4);
    std::memcpy(&outsidePixel, static_cast<const char*>(locked.pBits) + 8 * locked.Pitch + 8 * 4, 4);
    REQUIRE_HR(readback->UnlockRect());
    std::printf("Draw=Buffer center pixel: 0x%08lX (expected green 0xFF00FF00)\n", pixel);
    check((pixel & 0x00ffffff) == 0x0000ff00,
          "Draw=Buffer did not cover the center pixel with host depth testing enabled");
    check((outsidePixel & 0x00ffffff) == 0x00ff0000,
          "Draw=Buffer changed a pixel outside the host scissor rectangle");

    // Exercise the actual semantic resolver/binder, not a parallel copy of
    // its size selection. Intermediate pass viewports must not replace the
    // BeginScene size or the active offscreen turn's size.
    {
        const char viewportSource[] =
            "float2 ViewSize : VIEWPORTPIXELSIZE;\n"
            "float4 PS() : COLOR0 { return float4(ViewSize,0,1); }\n"
            "technique T { pass P { PixelShader=compile ps_3_0 PS(); } }\n";
        Com<ID3DXEffect> viewportEffect;
        REQUIRE_HR(createEffect(device.p, viewportSource,
            static_cast<UINT>(std::strlen(viewportSource)), nullptr, nullptr,
            0, nullptr, &viewportEffect.p, nullptr));
        Com<IDirect3DSurface9> largeTarget;
        Com<IDirect3DSurface9> offscreenTarget;
        REQUIRE_HR(device->CreateRenderTarget(1024, 512, D3DFMT_A8R8G8B8,
            D3DMULTISAMPLE_NONE, 0, FALSE, &largeTarget.p, nullptr));
        REQUIRE_HR(device->CreateRenderTarget(640, 360, D3DFMT_A8R8G8B8,
            D3DMULTISAMPLE_NONE, 0, FALSE, &offscreenTarget.p, nullptr));
        REQUIRE_HR(device->SetDepthStencilSurface(nullptr));
        REQUIRE_HR(device->SetRenderTarget(0, largeTarget.p));
        mme::MmeContext context(device.p);
        mme::ModelData model(device.p, 0, "viewport-test.x", 0, 0, nullptr);
        mme::MaterialBinding binding;
        binding.effect = viewportEffect.p;
        mme::RenderSnapshot snapshot{};
        snapshot.effect_file_used = 1;
        struct RestoreGlobals {
            mme::MmeContext* context = mme::g_context;
            D3DVIEWPORT9 viewport = mme::g_beginViewport;
            ~RestoreGlobals() {
                mme::g_context = context;
                mme::g_beginViewport = viewport;
            }
        } restoreGlobals;
        mme::g_context = &context;
        mme::g_beginViewport = {0, 0, 1024, 512, 0.0f, 1.0f};
        D3DVIEWPORT9 intermediate = {0, 0, 256, 128, 0.0f, 1.0f};
        REQUIRE_HR(device->SetViewport(&intermediate));
        mme::MmeBindStandardParameters(&model, &binding, snapshot);
        const D3DXHANDLE viewSize = viewportEffect->GetParameterByName(nullptr, "ViewSize");
        check(viewSize != nullptr, "VIEWPORTPIXELSIZE test parameter missing");
        float size[2] = {};
        REQUIRE_HR(viewportEffect->GetFloatArray(viewSize, size, 2));
        check(size[0] == 1024 && size[1] == 512,
              "VIEWPORTPIXELSIZE must use BeginScene dimensions, not the 256x128 viewport");
        mme::SasResource offscreen;
        offscreen.surface = offscreenTarget.p; // borrowed; Com owns the surface
        context.currentBindingOffscreen = &offscreen;
        REQUIRE_HR(device->SetRenderTarget(0, offscreenTarget.p));
        intermediate.Width = 160;
        intermediate.Height = 90;
        REQUIRE_HR(device->SetViewport(&intermediate));
        mme::MmeBindStandardParameters(&model, &binding, snapshot);
        REQUIRE_HR(viewportEffect->GetFloatArray(viewSize, size, 2));
        check(size[0] == 640 && size[1] == 360,
              "VIEWPORTPIXELSIZE must use offscreen turn dimensions, not the 160x90 viewport");
        context.currentBindingOffscreen = nullptr;
        std::printf("VIEWPORTPIXELSIZE offscreen binding: %.0fx%.0f (expected 640x360)\n",
                    size[0], size[1]);
    }
    if (!failures) std::puts("MME Draw=Buffer HAL regression passed");
    return failures ? 1 : 0;
}
