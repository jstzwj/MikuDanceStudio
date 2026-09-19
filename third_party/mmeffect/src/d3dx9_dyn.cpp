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
    HRESULT (WINAPI* createCubeTexture)(IDirect3DDevice9*, unsigned int,
                                        unsigned int, unsigned long, D3DFORMAT,
                                        D3DPOOL, IDirect3DCubeTexture9**) = nullptr;
    HRESULT (WINAPI* createTextureFromFileExW)(
        IDirect3DDevice9*, const wchar_t*, unsigned int, unsigned int,
        unsigned int, unsigned long, D3DFORMAT, D3DPOOL, unsigned long,
        unsigned long, unsigned long, void*, void*,
        IDirect3DTexture9**) = nullptr;
    HRESULT (WINAPI* createTextureFromFileExA)(
        IDirect3DDevice9*, const char*, unsigned int, unsigned int,
        unsigned int, unsigned long, D3DFORMAT, D3DPOOL, unsigned long,
        unsigned long, unsigned long, void*, void*,
        IDirect3DTexture9**) = nullptr;
    HRESULT (WINAPI* createCubeTextureFromFileExA)(
        IDirect3DDevice9*, const char*, unsigned int, unsigned int,
        unsigned long, D3DFORMAT, D3DPOOL, unsigned long, unsigned long,
        unsigned long, void*, void*, IDirect3DCubeTexture9**) = nullptr;
    HRESULT (WINAPI* createVolumeTextureFromFileExA)(
        IDirect3DDevice9*, const char*, unsigned int, unsigned int,
        unsigned int, unsigned int, unsigned long, D3DFORMAT, D3DPOOL,
        unsigned long, unsigned long, unsigned long, void*, void*,
        IDirect3DVolumeTexture9**) = nullptr;
    HRESULT (WINAPI* createVolumeTexture)(IDirect3DDevice9*, unsigned int,
                                          unsigned int, unsigned int,
                                          unsigned int, unsigned long, D3DFORMAT,
                                          D3DPOOL, IDirect3DVolumeTexture9**) = nullptr;
    HRESULT (WINAPI* compileShaderFromFileA)(
        const char*, const void*, const void*, const char*, const char*,
        unsigned long, void**, void**, void**) = nullptr;
    HRESULT (WINAPI* createTextureShader)(const unsigned long*,
                                          void**) = nullptr;
    HRESULT (WINAPI* fillTextureTX)(IDirect3DTexture9*, void*) = nullptr;
    HRESULT (WINAPI* fillCubeTextureTX)(IDirect3DCubeTexture9*, void*) = nullptr;
    HRESULT (WINAPI* fillVolumeTextureTX)(IDirect3DVolumeTexture9*,
                                          void*) = nullptr;
    HRESULT (WINAPI* saveSurfaceToFileA)(const char*, unsigned long,
                                         IDirect3DSurface9*, const void*,
                                         const void*) = nullptr;
    HRESULT (WINAPI* createEffectPool)(ID3DXEffectPool**) = nullptr;
    HRESULT (WINAPI* createEffectFromFileW)(
        IDirect3DDevice9*, const wchar_t*, const void*, const void*, unsigned long,
        void*, void**, void**) = nullptr;
    void* (WINAPI* matrixMultiply)(void*, const void*, const void*) = nullptr;
    void* (WINAPI* matrixInverse)(void*, float*, const void*) = nullptr;
    void* (WINAPI* matrixTranspose)(void*, const void*) = nullptr;
    void* (WINAPI* matrixLookAtLH)(void*, const void*, const void*, const void*) = nullptr;
    void* (WINAPI* matrixScaling)(void*, float, float, float) = nullptr;
    void* (WINAPI* vec3TransformNormal)(void*, const void*, const void*) = nullptr;
    void* (WINAPI* vec3Normalize)(void*, const void*) = nullptr;
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
            api.createCubeTexture =
                reinterpret_cast<decltype(api.createCubeTexture)>(
                    GetProcAddress(module, "D3DXCreateCubeTexture"));
            api.createTextureFromFileExW =
                reinterpret_cast<decltype(api.createTextureFromFileExW)>(
                    GetProcAddress(module, "D3DXCreateTextureFromFileExW"));
            api.createTextureFromFileExA =
                reinterpret_cast<decltype(api.createTextureFromFileExA)>(
                    GetProcAddress(module, "D3DXCreateTextureFromFileExA"));
            api.createCubeTextureFromFileExA =
                reinterpret_cast<decltype(api.createCubeTextureFromFileExA)>(
                    GetProcAddress(module, "D3DXCreateCubeTextureFromFileExA"));
            api.createVolumeTextureFromFileExA =
                reinterpret_cast<decltype(api.createVolumeTextureFromFileExA)>(
                    GetProcAddress(module, "D3DXCreateVolumeTextureFromFileExA"));
            api.createVolumeTexture =
                reinterpret_cast<decltype(api.createVolumeTexture)>(
                    GetProcAddress(module, "D3DXCreateVolumeTexture"));
            api.compileShaderFromFileA =
                reinterpret_cast<decltype(api.compileShaderFromFileA)>(
                    GetProcAddress(module, "D3DXCompileShaderFromFileA"));
            api.createTextureShader =
                reinterpret_cast<decltype(api.createTextureShader)>(
                    GetProcAddress(module, "D3DXCreateTextureShader"));
            api.fillTextureTX = reinterpret_cast<decltype(api.fillTextureTX)>(
                GetProcAddress(module, "D3DXFillTextureTX"));
            api.fillCubeTextureTX =
                reinterpret_cast<decltype(api.fillCubeTextureTX)>(
                    GetProcAddress(module, "D3DXFillCubeTextureTX"));
            api.fillVolumeTextureTX =
                reinterpret_cast<decltype(api.fillVolumeTextureTX)>(
                    GetProcAddress(module, "D3DXFillVolumeTextureTX"));
            api.createEffectPool = reinterpret_cast<decltype(api.createEffectPool)>(
                GetProcAddress(module, "D3DXCreateEffectPool"));
            api.saveSurfaceToFileA =
                reinterpret_cast<decltype(api.saveSurfaceToFileA)>(
                    GetProcAddress(module, "D3DXSaveSurfaceToFileA"));
            api.createEffectFromFileW =
                reinterpret_cast<decltype(api.createEffectFromFileW)>(
                    GetProcAddress(module, "D3DXCreateEffectFromFileW"));
            api.matrixMultiply = reinterpret_cast<decltype(api.matrixMultiply)>(
                GetProcAddress(module, "D3DXMatrixMultiply"));
            api.matrixInverse = reinterpret_cast<decltype(api.matrixInverse)>(
                GetProcAddress(module, "D3DXMatrixInverse"));
            // 原版 MMEffect.dll 锁定 d3dx9_43.dll（静态导入）；这里沿用本层
            // 既有策略，复用宿主已加载的 d3dx9_XX.dll 按名解析，不加版本强锁。
            api.matrixTranspose = reinterpret_cast<decltype(api.matrixTranspose)>(
                GetProcAddress(module, "D3DXMatrixTranspose"));
            api.matrixLookAtLH = reinterpret_cast<decltype(api.matrixLookAtLH)>(
                GetProcAddress(module, "D3DXMatrixLookAtLH"));
            api.matrixScaling = reinterpret_cast<decltype(api.matrixScaling)>(
                GetProcAddress(module, "D3DXMatrixScaling"));
            // D3DXMatrixIdentity 不做 GetProcAddress：d3dx9_XX.dll 导出表中
            // 没有该入口（SDK 头文件 D3DXINLINE 实现，原版 MMEffect.dll 也
            // 未导入它）。按名解析恒为 null，且一旦计入下面的 loaded 总闸，
            // 整个转发层会因这一个永不存在的导出而全体 E_NOTIMPL。
            api.vec3TransformNormal =
                reinterpret_cast<decltype(api.vec3TransformNormal)>(
                    GetProcAddress(module, "D3DXVec3TransformNormal"));
            api.vec3Normalize = reinterpret_cast<decltype(api.vec3Normalize)>(
                GetProcAddress(module, "D3DXVec3Normalize"));
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
            api.loaded = api.createTexture && api.createCubeTexture &&
                         api.createTextureFromFileExW &&
                         api.createTextureFromFileExA &&
                         api.createCubeTextureFromFileExA &&
                         api.createVolumeTextureFromFileExA &&
                         api.createVolumeTexture &&
                         api.compileShaderFromFileA &&
                         api.createTextureShader && api.fillTextureTX &&
                         api.fillCubeTextureTX && api.fillVolumeTextureTX &&
                         api.createEffectPool &&
                         api.createEffectFromFileW && api.matrixMultiply &&
                         api.matrixInverse && api.matrixLookAtLH &&
                         api.matrixTranspose &&
                         api.matrixScaling &&
                         api.vec3TransformNormal && api.vec3Normalize &&
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

// [MMEffect.dll IAT 0x1800A4720] RENDERCOLORTARGET 的 cube 创建转发。
HRESULT WINAPI D3DXCreateCubeTexture(IDirect3DDevice9* device,
                                     unsigned int size, unsigned int mipLevels,
                                     unsigned long usage, D3DFORMAT format,
                                     D3DPOOL pool,
                                     IDirect3DCubeTexture9** cubeTexture)
{
    const MmeD3dxApi& api = MmeD3dx();
    if (!api.loaded || api.createCubeTexture == nullptr)
        return E_NOTIMPL;
    return api.createCubeTexture(device, size, mipLevels, usage, format, pool,
                                 cubeTexture);
}

HRESULT WINAPI D3DXSaveSurfaceToFileA(const char* destFile, unsigned long format,
                                      IDirect3DSurface9* srcSurface,
                                      const void* srcPalette, const void* srcRect)
{
    const MmeD3dxApi& api = MmeD3dx();
    if (!api.loaded || api.saveSurfaceToFileA == nullptr)
        return E_NOTIMPL;
    return api.saveSurfaceToFileA(destFile, format, srcSurface, srcPalette,
                                  srcRect);
}

HRESULT WINAPI D3DXCreateEffectPool(ID3DXEffectPool** pool)
{
    const MmeD3dxApi& api = MmeD3dx();
    if (!api.loaded || api.createEffectPool == nullptr)
        return E_NOTIMPL;
    return api.createEffectPool(pool);
}

HRESULT WINAPI D3DXCreateTextureFromFileExW(
    IDirect3DDevice9* device, const wchar_t* srcFile, unsigned int width,
    unsigned int height, unsigned int mipLevels, unsigned long usage,
    D3DFORMAT format, D3DPOOL pool, unsigned long filter, unsigned long mipFilter,
    unsigned long colorKey, void* srcInfo, void* palette,
    IDirect3DTexture9** texture)
{
    const MmeD3dxApi& api = MmeD3dx();
    if (!api.loaded || api.createTextureFromFileExW == nullptr)
        return E_NOTIMPL;
    return api.createTextureFromFileExW(device, srcFile, width, height, mipLevels,
                                        usage, format, pool, filter, mipFilter,
                                        colorKey, srcInfo, palette, texture);
}

// SAS "ResourceName" 文件纹理的 ANSI 入口：原版 MMEffect.dll 经静态导入
// （IAT 0x1800A4708，调用点 sub_1800143D0+0x9C5=0x180015195）直接调用
// D3DXCreateTextureFromFileExA，路径按 ANSI 代码页解释。尺寸/MipLevels
// 传注解存档（无注解时为 D3DX_DEFAULT=0xFFFFFFFF），与调用点一致。
HRESULT WINAPI D3DXCreateTextureFromFileExA(
    IDirect3DDevice9* device, const char* srcFile, unsigned int width,
    unsigned int height, unsigned int mipLevels, unsigned long usage,
    D3DFORMAT format, D3DPOOL pool, unsigned long filter, unsigned long mipFilter,
    unsigned long colorKey, void* srcInfo, void* palette,
    IDirect3DTexture9** texture)
{
    const MmeD3dxApi& api = MmeD3dx();
    if (!api.loaded || api.createTextureFromFileExA == nullptr)
        return E_NOTIMPL;
    return api.createTextureFromFileExA(device, srcFile, width, height, mipLevels,
                                        usage, format, pool, filter, mipFilter,
                                        colorKey, srcInfo, palette, texture);
}

// [MMEffect.dll IAT 0x1800A4710 / 0x1800A4718] case 44 文件纹理的 cube /
// volume 变体转发，参数面与 ExA 同构。
HRESULT WINAPI D3DXCreateCubeTextureFromFileExA(
    IDirect3DDevice9* device, const char* srcFile, unsigned int size,
    unsigned int mipLevels, unsigned long usage, D3DFORMAT format, D3DPOOL pool,
    unsigned long filter, unsigned long mipFilter, unsigned long colorKey,
    void* srcInfo, void* palette, IDirect3DCubeTexture9** cubeTexture)
{
    const MmeD3dxApi& api = MmeD3dx();
    if (!api.loaded || api.createCubeTextureFromFileExA == nullptr)
        return E_NOTIMPL;
    return api.createCubeTextureFromFileExA(device, srcFile, size, mipLevels,
                                            usage, format, pool, filter,
                                            mipFilter, colorKey, srcInfo,
                                            palette, cubeTexture);
}

HRESULT WINAPI D3DXCreateVolumeTextureFromFileExA(
    IDirect3DDevice9* device, const char* srcFile, unsigned int width,
    unsigned int height, unsigned int depth, unsigned int mipLevels,
    unsigned long usage, D3DFORMAT format, D3DPOOL pool, unsigned long filter,
    unsigned long mipFilter, unsigned long colorKey, void* srcInfo,
    void* palette, IDirect3DVolumeTexture9** volumeTexture)
{
    const MmeD3dxApi& api = MmeD3dx();
    if (!api.loaded || api.createVolumeTextureFromFileExA == nullptr)
        return E_NOTIMPL;
    return api.createVolumeTextureFromFileExA(device, srcFile, width, height,
                                              depth, mipLevels, usage, format,
                                              pool, filter, mipFilter, colorKey,
                                              srcInfo, palette, volumeTexture);
}

// [MMEffect.dll IAT 0x1800A46F0] 0x2C Function 生成分支的 volume 创建转发
// （usage=0、pool=MANAGED 由调用方传入）。
HRESULT WINAPI D3DXCreateVolumeTexture(IDirect3DDevice9* device,
                                       unsigned int width, unsigned int height,
                                       unsigned int depth, unsigned int mipLevels,
                                       unsigned long usage, D3DFORMAT format,
                                       D3DPOOL pool,
                                       IDirect3DVolumeTexture9** volumeTexture)
{
    const MmeD3dxApi& api = MmeD3dx();
    if (!api.loaded || api.createVolumeTexture == nullptr)
        return E_NOTIMPL;
    return api.createVolumeTexture(device, width, height, depth, mipLevels,
                                   usage, format, pool, volumeTexture);
}

// [MMEffect.dll IAT 0x1800A4700，调用点 sub_1800143D0+0xED6=0x1800152A6]
// 0x2C Function 生成分支的着色器编译转发：pSrcFile 为效果自身 .fx 全路径，
// 入口点/profile 来自 "Function"/"Target" 注解，defines/include/flags 空，
// ppConstantTable 恒 NULL（栈上 [rsp+0x40]=0）。
HRESULT WINAPI D3DXCompileShaderFromFileA(
    const char* srcFile, const D3DXMACRO* defines, ID3DXInclude* include,
    const char* functionName, const char* profile, unsigned long flags,
    ID3DXBuffer** shader, ID3DXBuffer** errorMsgs, void** constantTable)
{
    const MmeD3dxApi& api = MmeD3dx();
    if (!api.loaded || api.compileShaderFromFileA == nullptr)
        return E_NOTIMPL;
    return api.compileShaderFromFileA(
        srcFile, defines, include, functionName, profile, flags,
        reinterpret_cast<void**>(shader), reinterpret_cast<void**>(errorMsgs),
        constantTable);
}

// [MMEffect.dll IAT 0x1800A46F8，调用点 0x180015688] 编译产物字节码包成
// ID3DXTextureShader。
HRESULT WINAPI D3DXCreateTextureShader(const unsigned long* function,
                                       ID3DXTextureShader** textureShader)
{
    const MmeD3dxApi& api = MmeD3dx();
    if (!api.loaded || api.createTextureShader == nullptr)
        return E_NOTIMPL;
    return api.createTextureShader(
        function, reinterpret_cast<void**>(textureShader));
}

// [MMEffect.dll IAT 0x1800A46E0/0x1800A46E8/0x1800A4740] 纹理着色器填充
// 的三个变体转发（2D/cube/volume 按 textureType 分派）。
HRESULT WINAPI D3DXFillTextureTX(IDirect3DTexture9* texture,
                                 ID3DXTextureShader* textureShader)
{
    const MmeD3dxApi& api = MmeD3dx();
    if (!api.loaded || api.fillTextureTX == nullptr)
        return E_NOTIMPL;
    return api.fillTextureTX(texture, textureShader);
}

HRESULT WINAPI D3DXFillCubeTextureTX(IDirect3DCubeTexture9* cubeTexture,
                                     ID3DXTextureShader* textureShader)
{
    const MmeD3dxApi& api = MmeD3dx();
    if (!api.loaded || api.fillCubeTextureTX == nullptr)
        return E_NOTIMPL;
    return api.fillCubeTextureTX(cubeTexture, textureShader);
}

HRESULT WINAPI D3DXFillVolumeTextureTX(IDirect3DVolumeTexture9* volumeTexture,
                                       ID3DXTextureShader* textureShader)
{
    const MmeD3dxApi& api = MmeD3dx();
    if (!api.loaded || api.fillVolumeTextureTX == nullptr)
        return E_NOTIMPL;
    return api.fillVolumeTextureTX(volumeTexture, textureShader);
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

// [MMEffect.dll IAT 0x1800A46A0] 矩阵转置转发。原版经 d3dx9_43.dll 静态
// 导入，共 3 处调用（IDA 反编译实证均为 pOut==pM 的原地转置、返回值未用）：
//   - sub_180057720+0xE5 = 0x180057805：SAS 保留光默认矩阵（switch 4/8/16
//     选常量源矩阵）后的可选 Inverse/Transpose；
//   - sub_18005EA40+0x164 = 0x18005EBA4：CONTROLOBJECT MatrixObject 参数
//     设置器（switch 12/20/52/54/0 选源 → 可选 Inverse → 可选 Transpose →
//     SetMatrix）；
//   - sub_18005EBE0+0x16E = 0x18005ED4E：同上，switch 0/4/8/12/16/20 选源。
// 模块选择沿用本层既有策略：复用宿主 d3dx9_XX.dll（原版锁 43 版，此处
// 不加版本强锁）。业务侧接线（material_bind）由后续负责，本层仅转发。
D3DXMATRIX* WINAPI D3DXMatrixTranspose(D3DXMATRIX* out, const D3DXMATRIX* matrix)
{
    const MmeD3dxApi& api = MmeD3dx();
    if (!api.loaded || api.matrixTranspose == nullptr)
        return nullptr;
    return static_cast<D3DXMATRIX*>(api.matrixTranspose(out, matrix));
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

D3DXMATRIX* WINAPI D3DXMatrixScaling(D3DXMATRIX* out, float sx, float sy,
                                     float sz)
{
    const MmeD3dxApi& api = MmeD3dx();
    if (!api.loaded || api.matrixScaling == nullptr)
        return nullptr;
    return static_cast<D3DXMATRIX*>(api.matrixScaling(out, sx, sy, sz));
}

// 见上方解析处的说明：d3dx9_XX.dll 无此导出（SDK 头内联实现，原版
// MMEffect.dll 在 0x1800581d0 同样内联生成单位阵）。这里按 SDK 的
// D3DXINLINE 语义直接填充，不经转发层。
D3DXMATRIX* WINAPI D3DXMatrixIdentity(D3DXMATRIX* out)
{
    for (int row = 0; row < 4; ++row) {
        for (int col = 0; col < 4; ++col)
            out->m[row][col] = (row == col) ? 1.0f : 0.0f;
    }
    return out;
}

D3DXVECTOR3* WINAPI D3DXVec3TransformNormal(D3DXVECTOR3* out,
                                            const D3DXVECTOR3* v,
                                            const D3DXMATRIX* matrix)
{
    const MmeD3dxApi& api = MmeD3dx();
    if (!api.loaded || api.vec3TransformNormal == nullptr)
        return nullptr;
    return static_cast<D3DXVECTOR3*>(api.vec3TransformNormal(out, v, matrix));
}

D3DXVECTOR3* WINAPI D3DXVec3Normalize(D3DXVECTOR3* out, const D3DXVECTOR3* v)
{
    const MmeD3dxApi& api = MmeD3dx();
    if (!api.loaded || api.vec3Normalize == nullptr)
        return nullptr;
    return static_cast<D3DXVECTOR3*>(api.vec3Normalize(out, v));
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
