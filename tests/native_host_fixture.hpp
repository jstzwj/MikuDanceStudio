#pragma once

#include "scene_effect_fixture.hpp"
#include "native_mmd_queries.hpp"
#include "native_mmh_oracle.hpp"
#include "mme_native_oracle.hpp"
#include "MMDExport.h"

namespace {
ID3DXEffect* supplied = nullptr;
HRESULT WINAPI SupplyStandardEffect(IDirect3DDevice9*, HMODULE, const char*, const D3DXMACRO*,
                                    ID3DXInclude*, DWORD, ID3DXEffectPool*, ID3DXEffect** out,
                                    ID3DXBuffer**) {
    *out = supplied;
    supplied->AddRef();
    return S_OK;
}

// Synchronize the port's runtime-only fields at the native MMD ABI boundary.
// Queries, MMHack interception, callbacks and effect execution remain native.
struct NativeHostDevice : SnapshotDeviceFixture {
    NativeMmdQueries& queries;
    MMDApp& app;
    IDirect3DDevice9* wrapped;
    bool enabled = false;
    bool trackCreation = true;
    bool synchronizeQueries = true;
    NativeHostDevice(Fixture& f, NativeMmdQueries& q, IDirect3DDevice9* w)
        : SnapshotDeviceFixture(f.device), queries(q), app(*f.app), wrapped(w) {}
    void Sync() {
        if (synchronizeQueries)
            queries.Sync(app);
    }
    HRESULT Finish(HRESULT result) {
        if (synchronizeQueries)
            app.state.renderPassCount = queries.Call<int>("ExpGetRenderRepeatCount");
        return result;
    }
    HRESULT STDMETHODCALLTYPE BeginScene() override {
        if (!enabled)
            return real->BeginScene();
        Sync();
        return Finish(wrapped->BeginScene());
    }
    HRESULT STDMETHODCALLTYPE EndScene() override {
        if (!enabled)
            return real->EndScene();
        Sync();
        return Finish(wrapped->EndScene());
    }
    HRESULT STDMETHODCALLTYPE Clear(DWORD count, const D3DRECT* rects, DWORD flags, D3DCOLOR color,
                                    float z, DWORD stencil) override {
        if (!enabled)
            return real->Clear(count, rects, flags, color, z, stencil);
        Sync();
        return Finish(wrapped->Clear(count, rects, flags, color, z, stencil));
    }
    HRESULT STDMETHODCALLTYPE DrawIndexedPrimitive(D3DPRIMITIVETYPE type, INT base, UINT min,
                                                   UINT vertices, UINT start, UINT count) override {
        if (!enabled)
            return real->DrawIndexedPrimitive(type, base, min, vertices, start, count);
        Sync();
        return Finish(wrapped->DrawIndexedPrimitive(type, base, min, vertices, start, count));
    }
    HRESULT STDMETHODCALLTYPE DrawPrimitive(D3DPRIMITIVETYPE type, UINT start,
                                            UINT count) override {
        if (!enabled)
            return real->DrawPrimitive(type, start, count);
        Sync();
        return Finish(wrapped->DrawPrimitive(type, start, count));
    }
    HRESULT STDMETHODCALLTYPE CreateVertexBuffer(UINT length, DWORD usage, DWORD fvf, D3DPOOL pool,
                                                 IDirect3DVertexBuffer9** out,
                                                 HANDLE* shared) override {
        return (trackCreation ? wrapped : real)
            ->CreateVertexBuffer(length, usage, fvf, pool, out, shared);
    }
    HRESULT STDMETHODCALLTYPE CreateIndexBuffer(UINT length, DWORD usage, D3DFORMAT format,
                                                D3DPOOL pool, IDirect3DIndexBuffer9** out,
                                                HANDLE* shared) override {
        // The original records INDEX buffers here (D3D9 slot 27), which
        // BeginScene uses to recognize model objects among their first fields.
        return (trackCreation ? wrapped : real)
            ->CreateIndexBuffer(length, usage, format, pool, out, shared);
    }
    HRESULT STDMETHODCALLTYPE Reset(D3DPRESENT_PARAMETERS* parameters) override {
        if (!enabled)
            return real->Reset(parameters);
        Sync();
        return Finish(wrapped->Reset(parameters));
    }
};
void BindEngine(NativeMmhOracle& mmh, HMODULE engine, HWND window) {
    const char* symbols[] = {"Initialize",
                             "OnCreateModel",
                             "OnDeleteModel",
                             "OnBeginScene",
                             "OnEndScene",
                             "OnDrawPrimitive",
                             "OnDrawIndexedPrimitive",
                             "Cleanup",
                             "OnLostDevice",
                             "OnResetDevice",
                             "OnClear"};
    for (std::size_t i = 0; i < std::size(symbols); ++i) {
        const FARPROC address = GetProcAddress(engine, symbols[i]);
        Check(address != nullptr, symbols[i]);
        mmh.Set((sizeof(void*) == 8 ? 0x6E7E8 : 0x51510) + i * sizeof(void*), address);
    }
    mmh.Set(sizeof(void*) == 8 ? 0x6E7E0 : 0x51508, window);
    mmh.Set(sizeof(void*) == 8 ? 0x6E7C0 : 0x514FC, window);
}
void Mapping(Fixture& f, NativeMmeOracle* engine, const std::filesystem::path& effect) {
    const auto path = f.directory / "native.emm";
    std::ofstream file(path);
    file << "[Info]\nVersion=3\n[Object]\nPmd0=" << ExpGetPmdFilename(0)
         << "\n[Effect]\nDefault=none\nPmd0=" << (effect.empty() ? "none" : effect.string())
         << '\n';
    file.close();
    Check(file.good(), "native mapping file");
    if (engine)
        engine->LoadMapping(path, f.window);
    else
        Check(::mme::MmeEmmLoad(path.string().c_str()), "port mapping load");
}
} // namespace
