// ===========================================================================
// VA 0x00424DC0 - InitToonTextures  (original: sub_424DC0, 0x2FA bytes)
// ===========================================================================
// Toon-shading gradient loader (NOT the font system - reclassified; the
// translated file's "font init" comment was wrong):
//   1. release the 11 texture slots at this+650720..+650760
//   2. texture slot 0: embedded PNG resource 0x67 via
//      D3DXCreateTextureFromFileInMemoryEx (A8R8G8B8, MANAGED pool)
//   3. edge-colour table at this+655632 (30 floats, exact values below;
//      x64 0x7FF7CB4BA24C..0x7FF7CB4BA388 writes all 30 dwords, literals
//      below reproduce the original bit patterns, not n/256 values)
//   4. slots 1..10: data\toon%02d.bmp via D3DXCreateTextureFromFileExA
//      (fmt 21 = A8R8G8B8, pool 1 = MANAGED); on failure fall back to the
//      embedded PNG resource (id+103); on success read the bottom-left
//      pixel via LockRect and store R/G/B * (1/256) into entries
//      [3*(N-1) .. 3*(N-1)+2] of the same table (stride three floats
//      per toon texture; x64 0x7FF7CB4BA522..0x7FF7CB4BA567).
//
// D3DX fidelity: the original imports d3dx9_32.dll specifically; the port
// LoadLibrary's the very same DLL and resolves the two entry points, so no
// build-time D3DX dependency is introduced.
// Device: *(this+657092)+120032 (guard: null until the D3D init is ported).
// =========================================================================//
#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <Windows.h>
#include <d3d9.h>

#include <cstdio>
#include <cstring>

#include "mikudancestudio/mmd_app.hpp"
#include "mikudancestudio/mme_bridge.hpp"
#include "mikudancestudio/ported_funcs.hpp"

namespace mikudancestudio {
namespace {

// runtime-resolved d3dx9_32.dll entry points (import table of the original)
using FnCreateTexInMemEx = HRESULT(WINAPI*)(
    IDirect3DDevice9*, LPCVOID, UINT, UINT, UINT, UINT, DWORD, D3DFORMAT,
    D3DPOOL, DWORD, DWORD, D3DCOLOR, void*, void*, IDirect3DTexture9**);
using FnCreateTexFromFileExA = HRESULT(WINAPI*)(
    IDirect3DDevice9*, LPCSTR, UINT, UINT, UINT, DWORD, D3DFORMAT, D3DPOOL,
    DWORD, DWORD, D3DCOLOR, void*, void*, IDirect3DTexture9**);

// Binary-compatible D3DXIMAGE_INFO layout.  The project resolves D3DX at
// runtime, so it cannot include the legacy D3DX SDK header directly.
struct D3dxImageInfo {
    UINT Width;
    UINT Height;
    UINT Depth;
    UINT MipLevels;
    D3DFORMAT Format;
    D3DRESOURCETYPE ResourceType;
    UINT ImageFileFormat;
};

struct D3dxApi {
    HMODULE module = nullptr;
    FnCreateTexInMemEx fromMemEx = nullptr;
    FnCreateTexFromFileExA fromFileExA = nullptr;

    bool Load() {
        if (module != nullptr)
            return fromMemEx != nullptr;
        // original import: x86 links d3dx9_32, the x64 rebuild d3dx9_43
        module = LoadLibraryA(sizeof(void*) == 8 ? "d3dx9_43.dll"
                                                 : "d3dx9_32.dll");
        if (module == nullptr)
            return false;
        fromMemEx = reinterpret_cast<FnCreateTexInMemEx>(
            GetProcAddress(module, "D3DXCreateTextureFromFileInMemoryEx"));
        fromFileExA = reinterpret_cast<FnCreateTexFromFileExA>(
            GetProcAddress(module, "D3DXCreateTextureFromFileExA"));
        return fromMemEx != nullptr && fromFileExA != nullptr;
    }
};

D3dxApi g_d3dx;

IDirect3DDevice9* DeviceOf(MMDApp* app) {
    D3DRenderer* sub = app->Renderer();                           // 657092
    if (sub == nullptr)
        return nullptr;
    return sub->device;                                           // +0x1D4E0
}

}  // namespace

bool InitToonTextures(MMDApp* app) {
    auto& s = *app;
    IDirect3DDevice9* device = DeviceOf(app);
    if (device == nullptr || !g_d3dx.Load())
        return false;   // original would fail on the resource-path call chain

    SetCurrentDirectoryW(app->ExeDir());                          // 657102

    // release the 11 toon texture slots (vtable+8 = Release)
    for (int i = 0; i < 11; ++i) {
        IDirect3DTexture9* tex = s.ToonTexture(i);                // 650720..
        if (tex != nullptr) {
            static_cast<IUnknown*>(tex)->Release();
            s.ToonTexture(i) = nullptr;
        }
    }

    // slot 0: embedded PNG (resource 0x67, type "PNG")
    HRSRC res = FindResourceA(static_cast<HMODULE>(s.HInstance()),
                              MAKEINTRESOURCEA(0x67), "PNG");
    DWORD size = SizeofResource(nullptr, res);
    HGLOBAL glob = LoadResource(static_cast<HMODULE>(s.HInstance()), res);
    void* data = LockResource(glob);
    IDirect3DTexture9* tex0 = nullptr;
    if (FAILED(g_d3dx.fromMemEx(device, data, size,
                                static_cast<UINT>(-1), static_cast<UINT>(-1),
                                1, 0, D3DFMT_UNKNOWN, D3DPOOL_MANAGED,
                                static_cast<DWORD>(-1), static_cast<DWORD>(-1),
                                0, nullptr, nullptr, &tex0)))
        return false;
    s.ToonTexture(0) = tex0;

    // edge-colour table defaults (this+655632, exact original values)
    const float defaults[14] = {
        0.80078101f, 0.80078101f, 0.80078101f, 0.95703101f, 0.87890601f,
        0.87890601f, 0.60156298f, 0.60156298f, 0.60156298f, 0.96875f,
        0.93359399f, 0.91796899f, 1.0f, 0.90234399f,
    };
    for (int i = 0; i < 14; ++i)
        s.state.toonEdgeTable[i] = defaults[i];
    // Trailing table (dwords 0x28052..0x28061): [14..17] pull the four
    // rdata floats 0x52c0c0/c0bc/c0b8/c0b4 (0.863281, 0.761719, 0.671875,
    // 0.011719); [18..29] are all 1.0.
    const float tail[4] = {0.86328101f, 0.76171899f, 0.671875f,
                           0.011719f};
    for (int i = 0; i < 4; ++i)
        s.state.toonEdgeTable[14 + i] = tail[i];
    for (int i = 18; i < 30; ++i)
        s.state.toonEdgeTable[i] = 1.0f;

    // slots 1..10: toonNN.bmp with embedded PNG fallback, colour from pixel
    for (int i = 1; i < 11; ++i) {
        char path[256];
        sprintf_s(path, 0x100, "data\\toon%02d.bmp", i);
        wchar_t widePath[256] = {};
        MultiByteToWideChar(CP_ACP, 0, path, -1, widePath, 256);
        IDirect3DTexture9** slot = &s.ToonTexture(i);
        D3dxImageInfo info{};
        if (FAILED(g_d3dx.fromFileExA(device, path,
                                      static_cast<UINT>(-1),
                                      static_cast<UINT>(-1), 1, 0,
                                      D3DFMT_A8R8G8B8 /*21*/,
                                      D3DPOOL_MANAGED /*1*/,
                                      static_cast<DWORD>(-1),
                                      static_cast<DWORD>(-1), 0,
                                      &info, nullptr, slot))) {
            HRSRC r = FindResourceA(static_cast<HMODULE>(s.HInstance()),
                                    MAKEINTRESOURCEA(i + 103), "PNG");
            DWORD sz = SizeofResource(nullptr, r);
            HGLOBAL g = LoadResource(static_cast<HMODULE>(s.HInstance()), r);
            void* d = LockResource(g);
            g_d3dx.fromMemEx(device, d, sz, static_cast<UINT>(-1),
                             static_cast<UINT>(-1), 1, 0, D3DFMT_UNKNOWN,
                             D3DPOOL_MANAGED, static_cast<DWORD>(-1),
                             static_cast<DWORD>(-1), 0, nullptr, nullptr,
                             slot);
        } else {
            // x64 sub_7FF7CB4BA130+0x3F2..0x437 (0x7FF7CB4BA522..0x7FF7CB4BA567):
            // the bottom-left pixel of toon%02d.bmp (A8R8G8B8 lock order
            // B,G,R,A) fills table entry 3*(N-1)..3*(N-1)+2 as R,G,B
            // (*entry = row[2]/256, entry[1] = row[1]/256, entry[2] =
            // row[0]/256); the entry pointer then advances by three floats
            // per texture (add r12,0Ch @0x7FF7CB4BA563).
            D3DLOCKED_RECT rect;
            if (SUCCEEDED((*slot)->LockRect(0, &rect, nullptr, 0))) {
                auto* row = static_cast<unsigned char*>(rect.pBits) +
                            rect.Pitch * (info.Height - 1);
                float* out = &s.state.toonEdgeTable[3 * (i - 1)];
                out[0] = row[2] * 0.00390625f;     // R
                out[1] = row[1] * 0.00390625f;     // G
                out[2] = row[0] * 0.00390625f;     // B
                (*slot)->UnlockRect(0);
            }
        }
        if (*slot != nullptr) {
            // 内置 MMEffect：登记内置 toon 纹理（GetToonTexture 的
            // "toonNN.bmp" 裸名查询经 MME 侧别名机制解析）。
            mme::RecordTexture(widePath, *slot);
        }
    }
    return true;
}

}  // namespace mikudancestudio
