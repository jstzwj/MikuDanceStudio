// ===========================================================================
// Runtime-resolved D3DX9 entry points (shared)
// ===========================================================================
// The reference imports d3dx9_32.dll on x86 and d3dx9_43.dll on x64.
// Resolve that exact runtime by architecture, avoiding a build-time SDK
// dependency. InitD3D requires it; a missing runtime cannot run the renderer.
// =========================================================================//
#pragma once

#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <Windows.h>
#include <d3d9.h>
#include <d3dx9.h>

#include <cstdint>

namespace mikudancestudio::d3dx {

struct D3DXMATRIXF { float m[4][4]; };

// One shared, typed COM declaration for host and built-in effect engine.
using Effect = ID3DXEffect;

using FnCreateTexInMemEx = HRESULT(WINAPI*)(
    IDirect3DDevice9*, LPCVOID, UINT, UINT, UINT, UINT, DWORD, D3DFORMAT,
    D3DPOOL, DWORD, DWORD, D3DCOLOR, void*, void*, IDirect3DTexture9**);
using FnCreateTexFromFileExA = HRESULT(WINAPI*)(
    IDirect3DDevice9*, LPCSTR, UINT, UINT, UINT, DWORD, D3DFORMAT, D3DPOOL,
    DWORD, DWORD, D3DCOLOR, void*, void*, IDirect3DTexture9**);
using FnCreateTexFromFileExW = HRESULT(WINAPI*)(
    IDirect3DDevice9*, LPCWSTR, UINT, UINT, UINT, DWORD, D3DFORMAT, D3DPOOL,
    DWORD, DWORD, D3DCOLOR, void*, void*, IDirect3DTexture9**);
using FnCreateTexture = HRESULT(WINAPI*)(
    IDirect3DDevice9*, UINT, UINT, UINT, DWORD, D3DFORMAT, D3DPOOL,
    IDirect3DTexture9**);
using FnCreateEffectFromResA = HRESULT(WINAPI*)(
    IDirect3DDevice9*, HMODULE, LPCSTR, const void*, const void*, DWORD,
    void*, void**, void*);
using FnMatrixPerspectiveFovLH = D3DXMATRIXF*(WINAPI*)(
    D3DXMATRIXF*, float, float, float, float);
using FnMatrixOp1 = D3DXMATRIXF*(WINAPI*)(D3DXMATRIXF*, float);
using FnMatrixOp2 = D3DXMATRIXF*(WINAPI*)(
    D3DXMATRIXF*, const D3DXMATRIXF*, const D3DXMATRIXF*);
using FnMatrixTranslation = D3DXMATRIXF*(WINAPI*)(
    D3DXMATRIXF*, float, float, float);
using FnMatrixLookAtLH = D3DXMATRIXF*(WINAPI*)(
    D3DXMATRIXF*, const float*, const float*, const float*);
using FnMatrixInverse = D3DXMATRIXF*(WINAPI*)(
    D3DXMATRIXF*, float*, const D3DXMATRIXF*);
using FnVec3Transform = float*(WINAPI*)(
    float out[4], const float src[3], const D3DXMATRIXF*);
using FnLoadMeshFromXInMemory = HRESULT(WINAPI*)(
    LPCVOID, DWORD, DWORD, IDirect3DDevice9*, void**, void**, void**, DWORD*,
    void**);
using FnLoadMeshFromXW = HRESULT(WINAPI*)(
    LPCWSTR, DWORD, IDirect3DDevice9*, void**, void**, void**, DWORD*, void**);
using FnComputeNormals = HRESULT(WINAPI*)(void*, const DWORD*);
using FnVecQuat = float*(WINAPI*)(float out[4], const D3DXMATRIXF* m);
using FnVec3Normalize = float*(WINAPI*)(float out[3], const float* src);
using FnQuatMultiply = float*(WINAPI*)(float out[4], const float* q1,
                                       const float* q2);
using FnMatrixQuat = D3DXMATRIXF*(WINAPI*)(D3DXMATRIXF* out,
                                           const float q[4]);
using FnQuatToAxisAngle = float*(WINAPI*)(const float q[4],
                                             float axis[3], float* angle);
using FnQuatNormalize = float*(WINAPI*)(float out[4], const float q[4]);
using FnSaveSurfaceToFileW = HRESULT(WINAPI*)(
    const wchar_t*, int /*D3DXIMAGE_FILEFORMAT*/, IDirect3DSurface9*,
    const void* /*PALETTEENTRY*/, const RECT*);

struct Api {
    HMODULE module = nullptr;
    bool available = false;
    FnCreateTexInMemEx fromMemEx = nullptr;
    FnCreateTexFromFileExA fromFileExA = nullptr;
    FnCreateTexFromFileExW fromFileExW = nullptr;
    FnCreateTexture createTexture = nullptr;
    FnCreateEffectFromResA createEffectFromResA = nullptr;
    FnMatrixPerspectiveFovLH perspectiveFovLH = nullptr;
    FnMatrixOp1 rotX = nullptr;
    FnMatrixOp1 rotY = nullptr;
    FnMatrixOp1 rotZ = nullptr;
    FnMatrixOp2 multiply = nullptr;
    FnMatrixTranslation translation = nullptr;
    FnMatrixTranslation scaling = nullptr;
    FnMatrixLookAtLH lookAtLH = nullptr;
    FnMatrixInverse inverse = nullptr;
    FnVec3Transform vec3Transform = nullptr;
    FnLoadMeshFromXInMemory loadMeshFromXInMemory = nullptr;
    FnLoadMeshFromXW loadMeshFromXW = nullptr;
    FnComputeNormals computeNormals = nullptr;
    FnVecQuat quatFromMatrix = nullptr;
    FnVec3Normalize vec3Normalize = nullptr;
    FnQuatMultiply quatMultiply = nullptr;
    FnMatrixQuat matrixRotationQuaternion = nullptr;
    FnQuatToAxisAngle quatToAxisAngle = nullptr;
    FnQuatNormalize quatNormalize = nullptr;
    FnSaveSurfaceToFileW saveSurfaceToFileW = nullptr;

    bool Load() {
        if (module != nullptr)
            return available;
        // original import name: x86 links d3dx9_32, the x64 rebuild d3dx9_43
        module = LoadLibraryA(sizeof(void*) == 8 ? "d3dx9_43.dll"
                                                 : "d3dx9_32.dll");
        if (module == nullptr)
            return false;
        fromMemEx = reinterpret_cast<FnCreateTexInMemEx>(GetProcAddress(
            module, "D3DXCreateTextureFromFileInMemoryEx"));
        fromFileExA = reinterpret_cast<FnCreateTexFromFileExA>(GetProcAddress(
            module, "D3DXCreateTextureFromFileExA"));
        fromFileExW = reinterpret_cast<FnCreateTexFromFileExW>(GetProcAddress(
            module, "D3DXCreateTextureFromFileExW"));
        createTexture = reinterpret_cast<FnCreateTexture>(GetProcAddress(
            module, "D3DXCreateTexture"));
        createEffectFromResA = reinterpret_cast<FnCreateEffectFromResA>(
            GetProcAddress(module, "D3DXCreateEffectFromResourceA"));
        perspectiveFovLH = reinterpret_cast<FnMatrixPerspectiveFovLH>(
            GetProcAddress(module, "D3DXMatrixPerspectiveFovLH"));
        rotX = reinterpret_cast<FnMatrixOp1>(
            GetProcAddress(module, "D3DXMatrixRotationX"));
        rotY = reinterpret_cast<FnMatrixOp1>(
            GetProcAddress(module, "D3DXMatrixRotationY"));
        rotZ = reinterpret_cast<FnMatrixOp1>(
            GetProcAddress(module, "D3DXMatrixRotationZ"));
        multiply = reinterpret_cast<FnMatrixOp2>(
            GetProcAddress(module, "D3DXMatrixMultiply"));
        translation = reinterpret_cast<FnMatrixTranslation>(
            GetProcAddress(module, "D3DXMatrixTranslation"));
        scaling = reinterpret_cast<FnMatrixTranslation>(
            GetProcAddress(module, "D3DXMatrixScaling"));
        lookAtLH = reinterpret_cast<FnMatrixLookAtLH>(
            GetProcAddress(module, "D3DXMatrixLookAtLH"));
        inverse = reinterpret_cast<FnMatrixInverse>(
            GetProcAddress(module, "D3DXMatrixInverse"));
        vec3Transform = reinterpret_cast<FnVec3Transform>(
            GetProcAddress(module, "D3DXVec3Transform"));
        loadMeshFromXInMemory = reinterpret_cast<FnLoadMeshFromXInMemory>(
            GetProcAddress(module, "D3DXLoadMeshFromXInMemory"));
        loadMeshFromXW = reinterpret_cast<FnLoadMeshFromXW>(
            GetProcAddress(module, "D3DXLoadMeshFromXW"));
        computeNormals = reinterpret_cast<FnComputeNormals>(
            GetProcAddress(module, "D3DXComputeNormals"));
        quatFromMatrix = reinterpret_cast<FnVecQuat>(
            GetProcAddress(module, "D3DXQuaternionRotationMatrix"));
        vec3Normalize = reinterpret_cast<FnVec3Normalize>(
            GetProcAddress(module, "D3DXVec3Normalize"));
        quatMultiply = reinterpret_cast<FnQuatMultiply>(
            GetProcAddress(module, "D3DXQuaternionMultiply"));
        matrixRotationQuaternion = reinterpret_cast<FnMatrixQuat>(
            GetProcAddress(module, "D3DXMatrixRotationQuaternion"));
        quatToAxisAngle = reinterpret_cast<FnQuatToAxisAngle>(
            GetProcAddress(module, "D3DXQuaternionToAxisAngle"));
        quatNormalize = reinterpret_cast<FnQuatNormalize>(
            GetProcAddress(module, "D3DXQuaternionNormalize"));
        saveSurfaceToFileW = reinterpret_cast<FnSaveSurfaceToFileW>(
            GetProcAddress(module, "D3DXSaveSurfaceToFileW"));
        available = fromMemEx && fromFileExA && fromFileExW && createTexture &&
               createEffectFromResA && perspectiveFovLH && rotX && rotY &&
               rotZ && multiply && translation && scaling && lookAtLH &&
               inverse && vec3Transform &&
               loadMeshFromXInMemory && loadMeshFromXW && computeNormals &&
               quatFromMatrix &&
               vec3Normalize && quatMultiply && matrixRotationQuaternion &&
               quatToAxisAngle && quatNormalize;
        return available;
    }
};

inline Api& Get() {
    static Api api;
    return api;
}

}  // namespace mikudancestudio::d3dx
