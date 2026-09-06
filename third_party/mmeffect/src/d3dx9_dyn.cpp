// d3dx9_dyn.cpp - 内置 MMEffect 模块的 D3DX 自由函数动态转发层。
//
// include/d3dx9.h 声明的自由函数在这里解析：复用宿主的
// mikudancestudio::d3dx::Api 模块句柄（LoadLibrary 引用计数保证同一
// d3dx9_XX.dll 实例），GetProcAddress 按名解析后缓存。任何一个入口缺失
// 时相关函数返回失败值——模块初始化时宿主已确认 d3dx 可用，这里仅作
// 防御。不引入任何构建期 d3dx9.lib 依赖。
#include "d3dx9.h"

#include "mikudancestudio/d3dx_dyn.hpp"

#include <cstdlib>

namespace {

struct MmeD3dxApi {
    bool attempted = false;
    bool loaded = false;
    HRESULT (WINAPI* createTexture)(IDirect3DDevice9*, unsigned int, unsigned int,
                                    unsigned int, unsigned long, D3DFORMAT, D3DPOOL,
                                    IDirect3DTexture9**) = nullptr;
    HRESULT (WINAPI* createEffectPool)(ID3DXEffectPool**) = nullptr;
    HRESULT (WINAPI* createEffectFromFileW)(
        IDirect3DDevice9*, const wchar_t*, const void*, const void*, unsigned long,
        void*, void**, void**) = nullptr;
    void* (WINAPI* matrixMultiply)(void*, const void*, const void*) = nullptr;
    void* (WINAPI* matrixInverse)(void*, float*, const void*) = nullptr;
    void* (WINAPI* matrixLookAtLH)(void*, const void*, const void*, const void*) = nullptr;
    const char* (WINAPI* getVertexShaderProfile)(IDirect3DDevice9*) = nullptr;
    const char* (WINAPI* getPixelShaderProfile)(IDirect3DDevice9*) = nullptr;
    unsigned int (WINAPI* getShaderVersion)(const unsigned long*) = nullptr;
    HRESULT (WINAPI* getShaderInputSemantics)(const unsigned long*, void*,
                                              unsigned int*) = nullptr;
    HRESULT (WINAPI* disassembleEffect)(void*, BOOL, void**) = nullptr;
};

MmeD3dxApi& MmeD3dx()
{
    static MmeD3dxApi api;
    if (!api.attempted) {
        api.attempted = true;
        if (mikudancestudio::d3dx::Get().Load()) {
            HMODULE module = mikudancestudio::d3dx::Get().module;
            api.createTexture = reinterpret_cast<decltype(api.createTexture)>(
                GetProcAddress(module, "D3DXCreateTexture"));
            api.createEffectPool = reinterpret_cast<decltype(api.createEffectPool)>(
                GetProcAddress(module, "D3DXCreateEffectPool"));
            api.createEffectFromFileW =
                reinterpret_cast<decltype(api.createEffectFromFileW)>(
                    GetProcAddress(module, "D3DXCreateEffectFromFileW"));
            api.matrixMultiply = reinterpret_cast<decltype(api.matrixMultiply)>(
                GetProcAddress(module, "D3DXMatrixMultiply"));
            api.matrixInverse = reinterpret_cast<decltype(api.matrixInverse)>(
                GetProcAddress(module, "D3DXMatrixInverse"));
            api.matrixLookAtLH = reinterpret_cast<decltype(api.matrixLookAtLH)>(
                GetProcAddress(module, "D3DXMatrixLookAtLH"));
            api.getVertexShaderProfile =
                reinterpret_cast<decltype(api.getVertexShaderProfile)>(
                    GetProcAddress(module, "D3DXGetVertexShaderProfile"));
            api.getPixelShaderProfile =
                reinterpret_cast<decltype(api.getPixelShaderProfile)>(
                    GetProcAddress(module, "D3DXGetPixelShaderProfile"));
            api.getShaderVersion = reinterpret_cast<decltype(api.getShaderVersion)>(
                GetProcAddress(module, "D3DXGetShaderVersion"));
            api.getShaderInputSemantics =
                reinterpret_cast<decltype(api.getShaderInputSemantics)>(
                    GetProcAddress(module, "D3DXGetShaderInputSemantics"));
            api.disassembleEffect = reinterpret_cast<decltype(api.disassembleEffect)>(
                GetProcAddress(module, "D3DXDisassembleEffect"));
            api.loaded = api.createTexture && api.createEffectPool &&
                         api.createEffectFromFileW && api.matrixMultiply &&
                         api.matrixInverse && api.matrixLookAtLH &&
                         api.getVertexShaderProfile && api.getPixelShaderProfile &&
                         api.getShaderVersion && api.getShaderInputSemantics &&
                         api.disassembleEffect;
        }
    }
    return api;
}

}  // namespace

extern "C" {

HRESULT WINAPI D3DXCreateTexture(IDirect3DDevice9* device, unsigned int width,
                                 unsigned int height, unsigned int mipLevels,
                                 unsigned long usage, D3DFORMAT format, D3DPOOL pool,
                                 IDirect3DTexture9** texture)
{
    const MmeD3dxApi& api = MmeD3dx();
    if (!api.loaded || api.createTexture == nullptr)
        return E_NOTIMPL;
    return api.createTexture(device, width, height, mipLevels, usage, format, pool,
                             texture);
}

HRESULT WINAPI D3DXCreateEffectPool(ID3DXEffectPool** pool)
{
    const MmeD3dxApi& api = MmeD3dx();
    if (!api.loaded || api.createEffectPool == nullptr)
        return E_NOTIMPL;
    return api.createEffectPool(pool);
}

HRESULT WINAPI D3DXCreateEffectFromFileW(IDirect3DDevice9* device,
                                         const wchar_t* srcFile,
                                         const D3DXMACRO* defines,
                                         ID3DXInclude* include,
                                         unsigned long flags, ID3DXEffectPool* pool,
                                         ID3DXEffect** effect,
                                         ID3DXBuffer** compilationErrors)
{
    const MmeD3dxApi& api = MmeD3dx();
    if (!api.loaded || api.createEffectFromFileW == nullptr)
        return E_NOTIMPL;
    return api.createEffectFromFileW(device, srcFile, defines, include, flags, pool,
                                     reinterpret_cast<void**>(effect),
                                     reinterpret_cast<void**>(compilationErrors));
}

D3DXMATRIX* WINAPI D3DXMatrixMultiply(D3DXMATRIX* out, const D3DXMATRIX* a,
                                      const D3DXMATRIX* b)
{
    const MmeD3dxApi& api = MmeD3dx();
    if (!api.loaded || api.matrixMultiply == nullptr)
        return nullptr;
    return static_cast<D3DXMATRIX*>(
        api.matrixMultiply(out, a, b));
}

D3DXMATRIX* WINAPI D3DXMatrixInverse(D3DXMATRIX* out, float* determinant,
                                     const D3DXMATRIX* matrix)
{
    const MmeD3dxApi& api = MmeD3dx();
    if (!api.loaded || api.matrixInverse == nullptr)
        return nullptr;
    return static_cast<D3DXMATRIX*>(api.matrixInverse(out, determinant, matrix));
}

D3DXMATRIX* WINAPI D3DXMatrixLookAtLH(D3DXMATRIX* out, const D3DXVECTOR3* eye,
                                      const D3DXVECTOR3* at, const D3DXVECTOR3* up)
{
    const MmeD3dxApi& api = MmeD3dx();
    if (!api.loaded || api.matrixLookAtLH == nullptr)
        return nullptr;
    return static_cast<D3DXMATRIX*>(
        api.matrixLookAtLH(out, eye, at, up));
}

const char* WINAPI D3DXGetVertexShaderProfile(IDirect3DDevice9* device)
{
    const MmeD3dxApi& api = MmeD3dx();
    if (!api.loaded || api.getVertexShaderProfile == nullptr)
        return nullptr;
    return api.getVertexShaderProfile(device);
}

const char* WINAPI D3DXGetPixelShaderProfile(IDirect3DDevice9* device)
{
    const MmeD3dxApi& api = MmeD3dx();
    if (!api.loaded || api.getPixelShaderProfile == nullptr)
        return nullptr;
    return api.getPixelShaderProfile(device);
}

unsigned int WINAPI D3DXGetShaderVersion(const unsigned long* function)
{
    const MmeD3dxApi& api = MmeD3dx();
    if (!api.loaded || api.getShaderVersion == nullptr)
        return 0;
    return api.getShaderVersion(function);
}

HRESULT WINAPI D3DXGetShaderInputSemantics(const unsigned long* function,
                                           D3DXSEMANTIC* semantics,
                                           unsigned int* count)
{
    const MmeD3dxApi& api = MmeD3dx();
    if (!api.loaded || api.getShaderInputSemantics == nullptr)
        return E_NOTIMPL;
    return api.getShaderInputSemantics(function, semantics, count);
}

HRESULT WINAPI D3DXDisassembleEffect(ID3DXEffect* effect, BOOL enableColorCode,
                                     ID3DXBuffer** disassembly)
{
    const MmeD3dxApi& api = MmeD3dx();
    if (!api.loaded || api.disassembleEffect == nullptr)
        return E_NOTIMPL;
    return api.disassembleEffect(effect, enableColorCode,
                                 reinterpret_cast<void**>(disassembly));
}

}  // extern "C"
