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

struct Api {
    static HRESULT WINAPI fromMemEx(IDirect3DDevice9* device, LPCVOID source, UINT sourceBytes,
                                    UINT width, UINT height, UINT mipLevels, DWORD usage,
                                    D3DFORMAT format, D3DPOOL pool, DWORD filter, DWORD mipFilter,
                                    D3DCOLOR colorKey, D3DXIMAGE_INFO* sourceInfo,
                                    PALETTEENTRY* palette, IDirect3DTexture9** texture) {
        return ::D3DXCreateTextureFromFileInMemoryEx(
            device, source, sourceBytes, width, height, mipLevels, usage, format, pool, filter,
            mipFilter, colorKey, sourceInfo, palette, texture);
    }
    static HRESULT WINAPI fromFileExA(IDirect3DDevice9* device, LPCSTR path, UINT width,
                                      UINT height, UINT mipLevels, DWORD usage, D3DFORMAT format,
                                      D3DPOOL pool, DWORD filter, DWORD mipFilter,
                                      D3DCOLOR colorKey, D3DXIMAGE_INFO* sourceInfo,
                                      PALETTEENTRY* palette, IDirect3DTexture9** texture) {
        return ::D3DXCreateTextureFromFileExA(device, path, width, height, mipLevels, usage, format,
                                              pool, filter, mipFilter, colorKey, sourceInfo,
                                              palette, texture);
    }
    static HRESULT WINAPI fromFileExW(IDirect3DDevice9* device, LPCWSTR path, UINT width,
                                      UINT height, UINT mipLevels, DWORD usage, D3DFORMAT format,
                                      D3DPOOL pool, DWORD filter, DWORD mipFilter,
                                      D3DCOLOR colorKey, D3DXIMAGE_INFO* sourceInfo,
                                      PALETTEENTRY* palette, IDirect3DTexture9** texture) {
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
                                               LPCSTR name, const D3DXMACRO* defines,
                                               ID3DXInclude* include, DWORD flags,
                                               ID3DXEffectPool* pool, ID3DXEffect** effect,
                                               ID3DXBuffer** errors) {
        return ::D3DXCreateEffectFromResourceA(device, module, name, defines, include, flags, pool,
                                               effect, errors);
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
                                                IDirect3DDevice9* device, ID3DXBuffer** adjacency,
                                                ID3DXBuffer** materials, ID3DXBuffer** effects,
                                                DWORD* count, ID3DXMesh** mesh) {
        return ::D3DXLoadMeshFromXInMemory(source, bytes, flags, device, adjacency, materials,
                                           effects, count, mesh);
    }
    static HRESULT WINAPI loadMeshFromXW(LPCWSTR source, DWORD flags, IDirect3DDevice9* device,
                                         ID3DXBuffer** adjacency, ID3DXBuffer** materials,
                                         ID3DXBuffer** effects, DWORD* count, ID3DXMesh** mesh) {
        return ::D3DXLoadMeshFromXW(source, flags, device, adjacency, materials, effects, count,
                                    mesh);
    }
    static HRESULT WINAPI computeNormals(ID3DXBaseMesh* mesh, const DWORD* adjacency) {
        return ::D3DXComputeNormals(mesh, adjacency);
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
    static HRESULT WINAPI saveSurfaceToFileW(LPCWSTR path, D3DXIMAGE_FILEFORMAT format,
                                             IDirect3DSurface9* surface,
                                             const PALETTEENTRY* palette, const RECT* rectangle) {
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
    // Runtime records retain the original float-array layout. Adapt only at the D3DX boundary.
    static_assert(sizeof(D3DXMATRIXF) == sizeof(D3DXMATRIX));
    static_assert(alignof(D3DXMATRIXF) == alignof(D3DXMATRIX));
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
} // namespace mikudancestudio::d3dx
