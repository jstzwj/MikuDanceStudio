// D3DX compatibility facade for the host's existing float-array call sites.
// All functions below use normal PE imports: Windows resolves the required
// architecture-specific runtime before any application code executes.
#pragma once
#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <Windows.h>
#include <d3dx9.h>
namespace mikudancestudio::d3dx {
struct D3DXMATRIXF {
    float m[4][4];
};
using Effect = ID3DXEffect;
using FnCreateTexInMemEx = HRESULT(WINAPI*)(IDirect3DDevice9*, LPCVOID, UINT, UINT, UINT, UINT,
                                            DWORD, D3DFORMAT, D3DPOOL, DWORD, DWORD, D3DCOLOR,
                                            void*, void*, IDirect3DTexture9**);
using FnCreateTexFromFileExA = HRESULT(WINAPI*)(IDirect3DDevice9*, LPCSTR, UINT, UINT, UINT, DWORD,
                                                D3DFORMAT, D3DPOOL, DWORD, DWORD, D3DCOLOR, void*,
                                                void*, IDirect3DTexture9**);
using FnCreateTexFromFileExW = HRESULT(WINAPI*)(IDirect3DDevice9*, LPCWSTR, UINT, UINT, UINT, DWORD,
                                                D3DFORMAT, D3DPOOL, DWORD, DWORD, D3DCOLOR, void*,
                                                void*, IDirect3DTexture9**);
using FnCreateTexture = HRESULT(WINAPI*)(IDirect3DDevice9*, UINT, UINT, UINT, DWORD, D3DFORMAT,
                                         D3DPOOL, IDirect3DTexture9**);
using FnCreateEffectFromResA = HRESULT(WINAPI*)(IDirect3DDevice9*, HMODULE, LPCSTR, const void*,
                                                const void*, DWORD, void*, void**, void*);
using FnMatrixPerspectiveFovLH = D3DXMATRIXF*(WINAPI*)(D3DXMATRIXF*, float, float, float, float);
using FnMatrixOp1 = D3DXMATRIXF*(WINAPI*)(D3DXMATRIXF*, float);
using FnMatrixOp2 = D3DXMATRIXF*(WINAPI*)(D3DXMATRIXF*, const D3DXMATRIXF*, const D3DXMATRIXF*);
using FnMatrixTranslation = D3DXMATRIXF*(WINAPI*)(D3DXMATRIXF*, float, float, float);
using FnMatrixLookAtLH = D3DXMATRIXF*(WINAPI*)(D3DXMATRIXF*, const float*, const float*,
                                               const float*);
using FnMatrixInverse = D3DXMATRIXF*(WINAPI*)(D3DXMATRIXF*, float*, const D3DXMATRIXF*);
using FnVec3Transform = float*(WINAPI*)(float out[4], const float src[3], const D3DXMATRIXF*);
using FnLoadMeshFromXInMemory = HRESULT(WINAPI*)(LPCVOID, DWORD, DWORD, IDirect3DDevice9*, void**,
                                                 void**, void**, DWORD*, void**);
using FnLoadMeshFromXW = HRESULT(WINAPI*)(LPCWSTR, DWORD, IDirect3DDevice9*, void**, void**, void**,
                                          DWORD*, void**);
using FnComputeNormals = HRESULT(WINAPI*)(void*, const DWORD*);
using FnVecQuat = float*(WINAPI*)(float out[4], const D3DXMATRIXF* m);
using FnVec3Normalize = float*(WINAPI*)(float out[3], const float* src);
using FnQuatMultiply = float*(WINAPI*)(float out[4], const float* q1, const float* q2);
using FnMatrixQuat = D3DXMATRIXF*(WINAPI*)(D3DXMATRIXF* out, const float q[4]);
using FnQuatToAxisAngle = void(WINAPI*)(const float q[4], float axis[3], float* angle);
using FnQuatNormalize = float*(WINAPI*)(float out[4], const float q[4]);
using FnSaveSurfaceToFileW = HRESULT(WINAPI*)(const wchar_t*, int /*D3DXIMAGE_FILEFORMAT*/,
                                              IDirect3DSurface9*, const void* /*PALETTEENTRY*/,
                                              const RECT*);

struct Api {
    // Kept for source compatibility with existing callers. This is no longer
    // a loader and cannot fail after the Windows loader admitted the process.
    static constexpr bool Load() noexcept { return true; }
    static HRESULT WINAPI fromMemEx(IDirect3DDevice9* device, LPCVOID source, UINT sourceBytes,
                                    UINT width, UINT height, UINT mipLevels, DWORD usage,
                                    D3DFORMAT format, D3DPOOL pool, DWORD filter, DWORD mipFilter,
                                    D3DCOLOR colorKey, void* sourceInfo, void* palette,
                                    IDirect3DTexture9** texture) {
        return ::D3DXCreateTextureFromFileInMemoryEx(
            device, source, sourceBytes, width, height, mipLevels, usage, format, pool, filter,
            mipFilter, colorKey, sourceInfo, palette, texture);
    }
    static HRESULT WINAPI fromFileExA(IDirect3DDevice9* device, LPCSTR path, UINT width,
                                      UINT height, UINT mipLevels, DWORD usage, D3DFORMAT format,
                                      D3DPOOL pool, DWORD filter, DWORD mipFilter,
                                      D3DCOLOR colorKey, void* sourceInfo, void* palette,
                                      IDirect3DTexture9** texture) {
        return ::D3DXCreateTextureFromFileExA(device, path, width, height, mipLevels, usage, format,
                                              pool, filter, mipFilter, colorKey, sourceInfo,
                                              palette, texture);
    }
    static HRESULT WINAPI fromFileExW(IDirect3DDevice9* device, LPCWSTR path, UINT width,
                                      UINT height, UINT mipLevels, DWORD usage, D3DFORMAT format,
                                      D3DPOOL pool, DWORD filter, DWORD mipFilter,
                                      D3DCOLOR colorKey, void* sourceInfo, void* palette,
                                      IDirect3DTexture9** texture) {
        return ::D3DXCreateTextureFromFileExW(device, path, width, height, mipLevels, usage, format,
                                              pool, filter, mipFilter, colorKey, sourceInfo,
                                              palette, texture);
    }
    static HRESULT WINAPI createTexture(IDirect3DDevice9* device, UINT width, UINT height,
                                        UINT mipLevels, DWORD usage, D3DFORMAT format, D3DPOOL pool,
                                        IDirect3DTexture9** texture) {
        return ::D3DXCreateTexture(device, width, height, mipLevels, usage, format, pool, texture);
    }
    static HRESULT WINAPI createEffectFromResA(IDirect3DDevice9* device, HMODULE module,
                                               LPCSTR name, const void* defines,
                                               const void* include, DWORD flags, void* pool,
                                               void** effect, void* errors) {
        return ::D3DXCreateEffectFromResourceA(
            device, module, name, static_cast<const D3DXMACRO*>(defines),
            static_cast<ID3DXInclude*>(const_cast<void*>(include)), flags,
            static_cast<ID3DXEffectPool*>(pool), reinterpret_cast<ID3DXEffect**>(effect),
            static_cast<ID3DXBuffer**>(errors));
    }
    static D3DXMATRIXF* WINAPI perspectiveFovLH(D3DXMATRIXF* out, float fov, float aspect,
                                                float nearZ, float farZ) {
        return reinterpret_cast<D3DXMATRIXF*>(
            ::D3DXMatrixPerspectiveFovLH(Matrix(out), fov, aspect, nearZ, farZ));
    }
    static D3DXMATRIXF* WINAPI multiply(D3DXMATRIXF* out, const D3DXMATRIXF* a,
                                        const D3DXMATRIXF* b) {
        return reinterpret_cast<D3DXMATRIXF*>(
            ::D3DXMatrixMultiply(Matrix(out), Matrix(a), Matrix(b)));
    }
    static D3DXMATRIXF* WINAPI lookAtLH(D3DXMATRIXF* out, const float* eye, const float* at,
                                        const float* up) {
        return reinterpret_cast<D3DXMATRIXF*>(
            ::D3DXMatrixLookAtLH(Matrix(out), Vector(eye), Vector(at), Vector(up)));
    }
    static D3DXMATRIXF* WINAPI inverse(D3DXMATRIXF* out, float* determinant,
                                       const D3DXMATRIXF* matrix) {
        return reinterpret_cast<D3DXMATRIXF*>(
            ::D3DXMatrixInverse(Matrix(out), determinant, Matrix(matrix)));
    }
    static float* WINAPI vec3Transform(float* out, const float* vector, const D3DXMATRIXF* matrix) {
        return reinterpret_cast<float*>(::D3DXVec3Transform(reinterpret_cast<D3DXVECTOR4*>(out),
                                                            Vector(vector), Matrix(matrix)));
    }
    static HRESULT WINAPI loadMeshFromXInMemory(LPCVOID source, DWORD bytes, DWORD flags,
                                                IDirect3DDevice9* device, void** adjacency,
                                                void** materials, void** effects, DWORD* count,
                                                void** mesh) {
        return ::D3DXLoadMeshFromXInMemory(
            source, bytes, flags, device, reinterpret_cast<ID3DXBuffer**>(adjacency),
            reinterpret_cast<ID3DXBuffer**>(materials), reinterpret_cast<ID3DXBuffer**>(effects),
            count, reinterpret_cast<ID3DXMesh**>(mesh));
    }
    static HRESULT WINAPI loadMeshFromXW(LPCWSTR source, DWORD flags, IDirect3DDevice9* device,
                                         void** adjacency, void** materials, void** effects,
                                         DWORD* count, void** mesh) {
        return ::D3DXLoadMeshFromXW(
            source, flags, device, reinterpret_cast<ID3DXBuffer**>(adjacency),
            reinterpret_cast<ID3DXBuffer**>(materials), reinterpret_cast<ID3DXBuffer**>(effects),
            count, reinterpret_cast<ID3DXMesh**>(mesh));
    }
    static HRESULT WINAPI computeNormals(void* mesh, const DWORD* adjacency) {
        return ::D3DXComputeNormals(static_cast<ID3DXMesh*>(mesh), adjacency);
    }
    static float* WINAPI quatFromMatrix(float* out, const D3DXMATRIXF* matrix) {
        return reinterpret_cast<float*>(
            ::D3DXQuaternionRotationMatrix(Quaternion(out), Matrix(matrix)));
    }
    static float* WINAPI vec3Normalize(float* out, const float* vector) {
        return reinterpret_cast<float*>(::D3DXVec3Normalize(Vector(out), Vector(vector)));
    }
    static float* WINAPI quatMultiply(float* out, const float* a, const float* b) {
        return reinterpret_cast<float*>(
            ::D3DXQuaternionMultiply(Quaternion(out), Quaternion(a), Quaternion(b)));
    }
    static D3DXMATRIXF* WINAPI matrixRotationQuaternion(D3DXMATRIXF* out, const float* quaternion) {
        return reinterpret_cast<D3DXMATRIXF*>(
            ::D3DXMatrixRotationQuaternion(Matrix(out), Quaternion(quaternion)));
    }
    static void WINAPI quatToAxisAngle(const float* quaternion, float* axis, float* angle) {
        ::D3DXQuaternionToAxisAngle(Quaternion(quaternion), Vector(axis), angle);
    }
    static float* WINAPI quatNormalize(float* out, const float* quaternion) {
        return reinterpret_cast<float*>(
            ::D3DXQuaternionNormalize(Quaternion(out), Quaternion(quaternion)));
    }
    static float* WINAPI quatInverse(float* out, const float* quaternion) {
        return reinterpret_cast<float*>(
            ::D3DXQuaternionInverse(Quaternion(out), Quaternion(quaternion)));
    }
    static float* WINAPI quatRotationAxis(float* out, const float* axis, float angle) {
        return reinterpret_cast<float*>(
            ::D3DXQuaternionRotationAxis(Quaternion(out), Vector(axis), angle));
    }
    static HRESULT WINAPI saveSurfaceToFileW(LPCWSTR path, int format, IDirect3DSurface9* surface,
                                             const void* palette, const RECT* rectangle) {
        return ::D3DXSaveSurfaceToFileW(path, format, surface, palette, rectangle);
    }
    static D3DXMATRIXF* WINAPI rotX(D3DXMATRIXF* out, float angle) {
        return reinterpret_cast<D3DXMATRIXF*>(::D3DXMatrixRotationX(Matrix(out), angle));
    }
    static D3DXMATRIXF* WINAPI rotY(D3DXMATRIXF* out, float angle) {
        return reinterpret_cast<D3DXMATRIXF*>(::D3DXMatrixRotationY(Matrix(out), angle));
    }
    static D3DXMATRIXF* WINAPI rotZ(D3DXMATRIXF* out, float angle) {
        return reinterpret_cast<D3DXMATRIXF*>(::D3DXMatrixRotationZ(Matrix(out), angle));
    }
    static D3DXMATRIXF* WINAPI translation(D3DXMATRIXF* out, float x, float y, float z) {
        return reinterpret_cast<D3DXMATRIXF*>(::D3DXMatrixTranslation(Matrix(out), x, y, z));
    }
    static D3DXMATRIXF* WINAPI scaling(D3DXMATRIXF* out, float x, float y, float z) {
        return reinterpret_cast<D3DXMATRIXF*>(::D3DXMatrixScaling(Matrix(out), x, y, z));
    }

   private:
    static D3DXMATRIX* Matrix(D3DXMATRIXF* value) { return reinterpret_cast<D3DXMATRIX*>(value); }
    static const D3DXMATRIX* Matrix(const D3DXMATRIXF* value) {
        return reinterpret_cast<const D3DXMATRIX*>(value);
    }
    static D3DXQUATERNION* Quaternion(float* value) {
        return reinterpret_cast<D3DXQUATERNION*>(value);
    }
    static const D3DXQUATERNION* Quaternion(const float* value) {
        return reinterpret_cast<const D3DXQUATERNION*>(value);
    }
    static D3DXVECTOR3* Vector(float* value) { return reinterpret_cast<D3DXVECTOR3*>(value); }
    static const D3DXVECTOR3* Vector(const float* value) {
        return reinterpret_cast<const D3DXVECTOR3*>(value);
    }
};
inline Api& Get() {
    static Api api;
    return api;
}
}  // namespace mikudancestudio::d3dx
