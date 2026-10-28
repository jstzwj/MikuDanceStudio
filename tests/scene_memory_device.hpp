#pragma once

// CPU storage for state/physics fixtures when Windows cannot create a D3D9
// device. Texture creation deliberately fails; this never validates rendering.
#include "d3d9_device_fixture.hpp"
#include <vector>

template <class Interface, class Descriptor> class SceneMemoryBuffer final : public Interface {
    std::vector<unsigned char> bytes_;
    Descriptor descriptor_{};
    ULONG references_ = 1;

public:
    explicit SceneMemoryBuffer(Descriptor descriptor)
        : bytes_(descriptor.Size), descriptor_(descriptor) {}
    HRESULT STDMETHODCALLTYPE QueryInterface(REFIID, void**) override { return E_NOINTERFACE; }
    ULONG STDMETHODCALLTYPE AddRef() override { return ++references_; }
    ULONG STDMETHODCALLTYPE Release() override {
        const ULONG result = --references_;
        if (!result)
            delete this;
        return result;
    }
    HRESULT STDMETHODCALLTYPE GetDevice(IDirect3DDevice9**) override { return E_FAIL; }
    HRESULT STDMETHODCALLTYPE SetPrivateData(REFGUID, const void*, DWORD, DWORD) override {
        return E_FAIL;
    }
    HRESULT STDMETHODCALLTYPE GetPrivateData(REFGUID, void*, DWORD*) override { return E_FAIL; }
    HRESULT STDMETHODCALLTYPE FreePrivateData(REFGUID) override { return E_FAIL; }
    DWORD STDMETHODCALLTYPE SetPriority(DWORD) override { return 0; }
    DWORD STDMETHODCALLTYPE GetPriority() override { return 0; }
    void STDMETHODCALLTYPE PreLoad() override {}
    D3DRESOURCETYPE STDMETHODCALLTYPE GetType() override { return descriptor_.Type; }
    HRESULT STDMETHODCALLTYPE Lock(UINT offset, UINT length, void** data, DWORD) override {
        if (!data || offset > bytes_.size() || length > bytes_.size() - offset)
            return D3DERR_INVALIDCALL;
        *data = bytes_.data() + offset;
        return S_OK;
    }
    HRESULT STDMETHODCALLTYPE Unlock() override { return S_OK; }
    HRESULT STDMETHODCALLTYPE GetDesc(Descriptor* descriptor) override {
        if (!descriptor)
            return D3DERR_INVALIDCALL;
        *descriptor = descriptor_;
        return S_OK;
    }
};
class SceneMemoryDevice final : public SnapshotDeviceFixture {
    ULONG references_ = 1;

public:
    SceneMemoryDevice() : SnapshotDeviceFixture(nullptr) {}
    HRESULT STDMETHODCALLTYPE QueryInterface(REFIID, void**) override { return E_NOINTERFACE; }
    ULONG STDMETHODCALLTYPE AddRef() override { return ++references_; }
    ULONG STDMETHODCALLTYPE Release() override {
        const ULONG result = --references_;
        if (!result)
            delete this;
        return result;
    }
    HRESULT STDMETHODCALLTYPE GetDisplayMode(UINT, D3DDISPLAYMODE*) override { return E_FAIL; }
    HRESULT STDMETHODCALLTYPE GetDeviceCaps(D3DCAPS9*) override { return E_FAIL; }
    HRESULT STDMETHODCALLTYPE GetDirect3D(IDirect3D9**) override { return E_FAIL; }
    HRESULT STDMETHODCALLTYPE CreateTexture(UINT, UINT, UINT, DWORD, D3DFORMAT, D3DPOOL,
                                            IDirect3DTexture9**, HANDLE*) override {
        return E_FAIL;
    }
    HRESULT STDMETHODCALLTYPE SetTransform(D3DTRANSFORMSTATETYPE, const D3DMATRIX*) override {
        return S_OK;
    }
    HRESULT STDMETHODCALLTYPE SetLight(DWORD, const D3DLIGHT9*) override { return S_OK; }
    HRESULT STDMETHODCALLTYPE SetViewport(const D3DVIEWPORT9*) override { return S_OK; }
    HRESULT STDMETHODCALLTYPE CreateVertexBuffer(UINT length, DWORD usage, DWORD fvf, D3DPOOL pool,
                                                 IDirect3DVertexBuffer9** result,
                                                 HANDLE*) override {
        D3DVERTEXBUFFER_DESC descriptor{};
        descriptor.Format = D3DFMT_VERTEXDATA;
        descriptor.Type = D3DRTYPE_VERTEXBUFFER;
        descriptor.Usage = usage;
        descriptor.Pool = pool;
        descriptor.Size = length;
        descriptor.FVF = fvf;
        *result = new SceneMemoryBuffer<IDirect3DVertexBuffer9, D3DVERTEXBUFFER_DESC>(descriptor);
        return S_OK;
    }
    HRESULT STDMETHODCALLTYPE CreateIndexBuffer(UINT length, DWORD usage, D3DFORMAT format,
                                                D3DPOOL pool, IDirect3DIndexBuffer9** result,
                                                HANDLE*) override {
        D3DINDEXBUFFER_DESC descriptor{};
        descriptor.Format = format;
        descriptor.Type = D3DRTYPE_INDEXBUFFER;
        descriptor.Usage = usage;
        descriptor.Pool = pool;
        descriptor.Size = length;
        *result = new SceneMemoryBuffer<IDirect3DIndexBuffer9, D3DINDEXBUFFER_DESC>(descriptor);
        return S_OK;
    }
};
