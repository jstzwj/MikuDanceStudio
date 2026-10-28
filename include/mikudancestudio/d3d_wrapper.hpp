// ===========================================================================
// MikuDanceStudio - the render/locale subsystem object ("0x1D574 object")
// ===========================================================================
// Original form (recovered from the x86 AND x64 binaries):
//   InitMainWindowAndD3D (x86 0x47A5B0 / x64 sub_14000CB50) performs
//       obj = operator new(SIZE);      // x86 0x1D574 / x64 0x3AA88
//       memset(obj, 0, SIZE);
//       ctor (x86 0x406D40 / x64 0x1400078C0);
//   and stores the pointer at app+657092 (x86) / app+661216 (x64).
//
// This header restores the object as a source-level struct.  The layout
// below reproduces BOTH original binaries through natural compiler
// alignment alone - no packing pragmas, no hand padding.  Every field's
// offset is pinned by static_assert against instruction-level evidence
// collected from the two IDBs (build-x64/ab/offtab/wrapper_fields.md):
//   * x86 offsets from `fld/fst/fild/byte ptr` hits in the 81 wrapper-
//     touching functions; x64 offsets from the corresponding 82.
//   * A field that is a dword on x86 but a qword on x64 is a pointer;
//     equal widths on both are scalars.  That is how every member type
//     below was chosen.
// ===========================================================================
#pragma once

#include <cstddef>
#include <cstdint>

#include <Windows.h>
#include <locale.h>

#include <d3d9.h>

struct ID3DXEffect; // d3dx9.h - forward decl, pointer-only use here

namespace mikudancestudio {

// One slot of the 10000-entry resource pool that dominates the object.
// ctor (x86 0x406D40) zeroes every slot; dtor (x86 0x406BE0) walks them
// doing free(heapBuffer) + comObject->Release() (vtbl+8).
struct ResourcePoolEntry {
    void* heapBuffer;    // freed with free() in the dtor
    IUnknown* comObject; // released via vtbl+8 (IUnknown::Release)
    std::int32_t tag;    // untouched by ctor/dtor; the texture loaders
                         // stash BGR overflow bytes here
};

class D3DRenderer {
public:
    // -- window / locale --------------------------------------------------
    HWND hwnd; // +0        main window handle

    ResourcePoolEntry resourcePool[10000]; // x86 +4 / x64 +8

    _locale_t localeTable[4]; // x86 +120004 / x64 +240008 (_locale_t is
                              // itself a pointer typedef)
                              // [0]=_create_locale(0,"Locale")
                              // [1]=".OCP" [2]=".ACP" [3]="JPN"
    void* stereoHandle;       // 120020 / 240040  NVAPI stereo-3D probe
                              // object (released by x86 0x4CB4F0)

    // -- device / mode ----------------------------------------------------
    std::int32_t shaderModelCaps;             // 120024 / 240048  ctor=1; caps-derived
                                              // dword (x86 reads D3DCAPS9+0x70)
    IDirect3D9* d3d9;                         // 120028 / 240056
    IDirect3DDevice9* device;                 // 120032 / 240064  ~500+ read sites
    std::int32_t screenWidth;                 // 120036 / 240072  monitor max (fnEnum)
    std::int32_t screenHeight;                // 120040 / 240076
    float aspectRatio;                        // 120044 / 240080  = W/H
    float viewScale;                          // 120048 / 240084  seeded 1.0f by InitD3D;
                                              // read as the viewport/UI scale by the
                                              // cursor, media and overlay paths
    IDirect3DVertexBuffer9* lineVertexBuffer; // 120052 / 240088 selection/
                                              // wireframe line VB (CreateVertexBuffer
                                              // out-param at 0x467509; dtor-released)
    unsigned char multisampleAvailable;       // 120056 / 240096

    D3DPRESENT_PARAMETERS presentParameters; // 120060 / 240104
                                             // x86 56B / x64 64B (HWND widens+aligns)

    // -- surfaces / effects -----------------------------------------------
    IDirect3DSurface9* captureSurface;      // 120116 / 240168 lockable RT
    IDirect3DSurface9* backbufferSurface;   // 120120 / 240176
    IDirect3DSurface9* depthStencilSurface; // 120124 / 240184
    D3DFORMAT backbufferFormat;             // 120128 / 240192 21/22 cache
    unsigned char postProcessEnabled;       // 120132 / 240196 HDR switch
    IDirect3DTexture9* hdrTexture;          // 120136 / 240200 fmt 114
    IDirect3DTexture9* spriteTexture;       // 120140 / 240208 PNG res 0x67
    IDirect3DSurface9* shadowSurface;       // 120144 / 240216 shadow RT
    IDirect3DSurface9* shadowDepthSurface;  // 120148 / 240224 D24X8=77
    std::int32_t renderTargetWidth;         // 120152 / 240232 <=2048
    std::int32_t renderTargetHeight;        // 120156 / 240236
    ID3DXEffect* effect;                    // 120160 / 240240
    unsigned char d3dInitialized;           // 120164 / 240248 ctor=1
    unsigned char shaderModel3;             // 120165 / 240249 SM3 flag
    unsigned char stereoEnabled;            // 120166 / 240250
    std::int32_t maxTextureWidth;           // 120168 / 240252 caps
    std::int32_t maxTextureHeight;          // 120172 / 240256 caps
    unsigned char runtimeToggle;            // 120176 / 240260 ctor=1,
                                            // flipped from config - meaning TBD
};

// -- layout pinned to the two original binaries --------------------------
static_assert(sizeof(D3DRenderer) == 0x1D574 || sizeof(D3DRenderer) == 0x3AA88,
              "size must be the x86 or x64 original allocation size");

#define MIKUDANCESTUDIO_WRAPPER_OFF(f, x86off, x64off)                                             \
    static_assert(offsetof(D3DRenderer, f) == (sizeof(void*) == 8 ? (x64off) : (x86off)),          \
                  #f " offset must match the original binary")

MIKUDANCESTUDIO_WRAPPER_OFF(hwnd, 0, 0);
MIKUDANCESTUDIO_WRAPPER_OFF(resourcePool, 4, 8);
MIKUDANCESTUDIO_WRAPPER_OFF(localeTable, 120004, 240008);
MIKUDANCESTUDIO_WRAPPER_OFF(stereoHandle, 120020, 240040);
MIKUDANCESTUDIO_WRAPPER_OFF(shaderModelCaps, 120024, 240048);
MIKUDANCESTUDIO_WRAPPER_OFF(d3d9, 120028, 240056);
MIKUDANCESTUDIO_WRAPPER_OFF(device, 120032, 240064);
MIKUDANCESTUDIO_WRAPPER_OFF(screenWidth, 120036, 240072);
MIKUDANCESTUDIO_WRAPPER_OFF(screenHeight, 120040, 240076);
MIKUDANCESTUDIO_WRAPPER_OFF(aspectRatio, 120044, 240080);
MIKUDANCESTUDIO_WRAPPER_OFF(viewScale, 120048, 240084);
MIKUDANCESTUDIO_WRAPPER_OFF(lineVertexBuffer, 120052, 240088);
MIKUDANCESTUDIO_WRAPPER_OFF(multisampleAvailable, 120056, 240096);
MIKUDANCESTUDIO_WRAPPER_OFF(presentParameters, 120060, 240104);
MIKUDANCESTUDIO_WRAPPER_OFF(captureSurface, 120116, 240168);
MIKUDANCESTUDIO_WRAPPER_OFF(backbufferSurface, 120120, 240176);
MIKUDANCESTUDIO_WRAPPER_OFF(depthStencilSurface, 120124, 240184);
MIKUDANCESTUDIO_WRAPPER_OFF(backbufferFormat, 120128, 240192);
MIKUDANCESTUDIO_WRAPPER_OFF(postProcessEnabled, 120132, 240196);
MIKUDANCESTUDIO_WRAPPER_OFF(hdrTexture, 120136, 240200);
MIKUDANCESTUDIO_WRAPPER_OFF(spriteTexture, 120140, 240208);
MIKUDANCESTUDIO_WRAPPER_OFF(shadowSurface, 120144, 240216);
MIKUDANCESTUDIO_WRAPPER_OFF(shadowDepthSurface, 120148, 240224);
MIKUDANCESTUDIO_WRAPPER_OFF(renderTargetWidth, 120152, 240232);
MIKUDANCESTUDIO_WRAPPER_OFF(renderTargetHeight, 120156, 240236);
MIKUDANCESTUDIO_WRAPPER_OFF(effect, 120160, 240240);
MIKUDANCESTUDIO_WRAPPER_OFF(d3dInitialized, 120164, 240248);
MIKUDANCESTUDIO_WRAPPER_OFF(shaderModel3, 120165, 240249);
MIKUDANCESTUDIO_WRAPPER_OFF(stereoEnabled, 120166, 240250);
MIKUDANCESTUDIO_WRAPPER_OFF(maxTextureWidth, 120168, 240252);
MIKUDANCESTUDIO_WRAPPER_OFF(maxTextureHeight, 120172, 240256);
MIKUDANCESTUDIO_WRAPPER_OFF(runtimeToggle, 120176, 240260);

#undef MIKUDANCESTUDIO_WRAPPER_OFF

} // namespace mikudancestudio
