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
// imports the matching runtime through the shared D3DX interface.
// Device: *(this+657092)+120032 (guard: null until the D3D init is ported).
// =========================================================================//
#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <Windows.h>
#include <d3d9.h>

#include <cstdio>
#include <cstring>

#include "mikudancestudio/mmd_app.hpp"
#include "mikudancestudio/render_self_shadow.hpp"
#include "mikudancestudio/d3dx_dyn.hpp"
#include "mikudancestudio/mme_bridge.hpp"
#include "mikudancestudio/ported_funcs.hpp"

namespace mikudancestudio {
namespace {

auto& g_d3dx = d3dx::Get();

IDirect3DDevice9* DeviceOf(MMDApp* app) {
    D3DRenderer* sub = app->Renderer(); // 657092
    if (sub == nullptr)
        return nullptr;
    return sub->device; // +0x1D4E0
}

} // namespace

bool InitToonTextures(MMDApp* app) {
    auto& s = *app;
    IDirect3DDevice9* device = DeviceOf(app);
    if (device == nullptr)
        return false; // original would fail on the resource-path call chain

    SetCurrentDirectoryW(app->ExeDir()); // 657102

    // release the 11 toon texture slots (vtable+8 = Release)
    for (int i = 0; i < 11; ++i) {
        IDirect3DTexture9* tex = s.ToonTexture(i); // 650720..
        if (tex != nullptr) {
            static_cast<IUnknown*>(tex)->Release();
            s.ToonTexture(i) = nullptr;
        }
    }

    // slot 0: embedded PNG (resource 0x67, type "PNG")
    HRSRC res = FindResourceA(static_cast<HMODULE>(s.HInstance()), MAKEINTRESOURCEA(0x67), "PNG");
    DWORD size = SizeofResource(nullptr, res);
    HGLOBAL glob = LoadResource(static_cast<HMODULE>(s.HInstance()), res);
    void* data = LockResource(glob);
    IDirect3DTexture9* tex0 = nullptr;
    if (FAILED(g_d3dx.fromMemEx(device, data, size, static_cast<UINT>(-1), static_cast<UINT>(-1), 1,
                                0, D3DFMT_UNKNOWN, D3DPOOL_MANAGED, static_cast<DWORD>(-1),
                                static_cast<DWORD>(-1), 0, nullptr, nullptr, &tex0)))
        return false;
    s.ToonTexture(0) = tex0;

    // edge-colour table defaults (this+655632, exact original values)
    std::memcpy(s.state.toonEdgeTable,render::DefaultToonShadowColors.data(),
                sizeof(render::DefaultToonShadowColors));

    // slots 1..10: toonNN.bmp with embedded PNG fallback, colour from pixel
    for (int i = 1; i < 11; ++i) {
        char path[256];
        sprintf_s(path, 0x100, "data\\toon%02d.bmp", i);
        wchar_t widePath[256] = {};
        MultiByteToWideChar(CP_ACP, 0, path, -1, widePath, 256);
        IDirect3DTexture9** slot = &s.ToonTexture(i);
        D3DXIMAGE_INFO info{};
        if (FAILED(g_d3dx.fromFileExA(device, path, static_cast<UINT>(-1), static_cast<UINT>(-1), 1,
                                      0, D3DFMT_A8R8G8B8 /*21*/, D3DPOOL_MANAGED /*1*/,
                                      static_cast<DWORD>(-1), static_cast<DWORD>(-1), 0, &info,
                                      nullptr, slot))) {
            HRSRC r = FindResourceA(static_cast<HMODULE>(s.HInstance()), MAKEINTRESOURCEA(i + 103),
                                    "PNG");
            DWORD sz = SizeofResource(nullptr, r);
            HGLOBAL g = LoadResource(static_cast<HMODULE>(s.HInstance()), r);
            void* d = LockResource(g);
            g_d3dx.fromMemEx(device, d, sz, static_cast<UINT>(-1), static_cast<UINT>(-1), 1, 0,
                             D3DFMT_UNKNOWN, D3DPOOL_MANAGED, static_cast<DWORD>(-1),
                             static_cast<DWORD>(-1), 0, nullptr, nullptr, slot);
        } else {
            // x64 sub_7FF7CB4BA130+0x3F2..0x437 (0x7FF7CB4BA522..0x7FF7CB4BA567):
            // the bottom-left pixel of toon%02d.bmp (A8R8G8B8 lock order
            // B,G,R,A) fills table entry 3*(N-1)..3*(N-1)+2 as R,G,B
            // (*entry = row[2]/256, entry[1] = row[1]/256, entry[2] =
            // row[0]/256); the entry pointer then advances by three floats
            // per texture (add r12,0Ch @0x7FF7CB4BA563).
            D3DLOCKED_RECT rect;
            if (SUCCEEDED((*slot)->LockRect(0, &rect, nullptr, 0))) {
                auto* row =
                    static_cast<unsigned char*>(rect.pBits) + rect.Pitch * (info.Height - 1);
                float* out = &s.state.toonEdgeTable[3 * (i - 1)];
                out[0] = row[2] * 0.00390625f; // R
                out[1] = row[1] * 0.00390625f; // G
                out[2] = row[0] * 0.00390625f; // B
                (*slot)->UnlockRect(0);
            }
        }
        if (*slot != nullptr) {
            // Embedded MMEffect: register embedded toon textures (bare-name GetToonTexture queries for
            // "toonNN.bmp" resolve through MME aliases).
            mme::RecordTexture(widePath, *slot);
        }
    }
    return true;
}

} // namespace mikudancestudio
