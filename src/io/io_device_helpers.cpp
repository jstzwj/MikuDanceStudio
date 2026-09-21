// ===========================================================================
// io_device_helpers.cpp - assorted gap functions from the 0x403030..0x409700 window
// ===========================================================================
// Historical x86 address map for the IO helpers implemented here and nearby:
//   ported here:  0x00407660  texture-name table find (texture slot)
//                 0x00407830  texture-name table find (average color)
//                 0x00407470  NVAPI stereo activation toggle (0x4CC0F0 call)
//                 0x00408EF0  DirectShowInit (CoInitialize wrapper)
//   already present elsewhere (see the banners below for pointers):
//                 0x00406950  DrawPhysicsCollisionDebug
//                 0x00403030  Bullet btRigidBodyConstructionInfo ctor
//   implemented in src/app/dshow_record_graph.cpp (FindPinDShow,
//   BindSelectedCompressor, FindInputPin and FindOutputPin):
//                 0x004095D0, 0x00409170, 0x004096D0, 0x00409700
// ===========================================================================
#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <Windows.h>
#include <objbase.h>    // CoInitialize (0x408EF0 DirectShowInit)

#include <cstdint>
#include <cwchar>

#include "mikudancestudio/d3d_wrapper.hpp"

namespace mikudancestudio {

// ---------------------------------------------------------------------------
// Shared layout note for 0x407660 / 0x407830 / 0x407470 (also 0x4076E0 and
// 0x406BE0): `this` (ecx) is the 0x1D574 render/locale wrapper object kept
// at app+0xA06C4, restored as mikudancestudio::D3DRenderer.
// Its first 120000 bytes are the 10000-entry resourcePool with a 12-byte
// stride (entry i @ wrapper + 4 + 12*i):
//     .heapBuffer  wchar_t* name (null slot terminates the table)
//     .comObject   IDirect3DTexture9* texture (written by 0x4076E0)
//     .tag         spill BGR of entry i itself - the loader 0x4076E0
//                  stores the bottom-left pixel of the loaded texture at
//                  texture-slot bytes +4/+5/+6, i.e. 12*i+0x0C..0x0E,
//                  which physically overlaps entry i+1's first dword
// Slots past the table: wrapper+120004..+120016 locale table (30001..30004,
// see ConvertAnsiToWide) = localeTable[0..3], wrapper+120020 NVAPI stereo
// handle (30005) = stereoHandle, wrapper+120032 D3D device (30008) = device.
// ---------------------------------------------------------------------------

// VA 0x00407660 - texture-name table find: texture slot.
// thiscall(this = 0x1D574 wrapper, const wchar_t* name).
// Null/empty name or an unmatched name returns nullptr; a match returns the
// entry's texture pointer (.comObject, null there when the load in 0x4076E0
// failed).  Callers: 0x4912F0, 0x492AC0, 0x4C4A10, 0x4C55C0 (the model and
// accessory renderers, which immediately hand the result to SetTexture).
void* TextureTableFind(D3DRenderer* wrapper, const wchar_t* name) {
    if (name[0] == L'\0')                                       // 0x407665
        return nullptr;
    for (int index = 0;;) {
        const wchar_t* entryName = static_cast<const wchar_t*>(
            wrapper->resourcePool[index].heapBuffer);
        if (entryName == nullptr)                               // 0x407684
            return nullptr;
        if (wcscmp(entryName, name) == 0)                       // 0x40768E
            return wrapper->resourcePool[index].comObject;
        if (++index >= 10000)                                   // 0x4076BD
            return nullptr;
    }
}

// VA 0x00407830 - texture-name table find: spill color.
// thiscall(this = 0x1D574 wrapper, float outRgb[3], const wchar_t* name)
// -> returns outRgb.  outRgb is seeded with (1,1,1); an empty/unmatched
// name leaves that white.  A match converts the entry's spill BGR bytes
// (the texture's bottom-left pixel, stored by 0x4076E0 at +0x0C..0x0E, now
// the .tag dword) to three floats.  The original fild's each byte and
// multiplies by the double 0.00390625 (dbl_529680 = 0x3F70000000000000)
// before the fstp dword truncation, so the products are computed in double
// precision.
float* TextureTableLookupColor(D3DRenderer* wrapper, float* outRgb,
                               const wchar_t* name) {
    outRgb[2] = 1.0f;                                           // 0x407838
    outRgb[1] = 1.0f;                                           // 0x40783D
    outRgb[0] = 1.0f;                                           // 0x407844
    if (name[0] == L'\0')                                       // 0x407846
        return outRgb;
    for (int index = 0;;) {
        const wchar_t* entryName = static_cast<const wchar_t*>(
            wrapper->resourcePool[index].heapBuffer);
        if (entryName == nullptr)                               // 0x407863
            return outRgb;
        if (wcscmp(entryName, name) == 0) {                     // 0x40789D
            const unsigned char* spill =                        // 0x4078BC
                reinterpret_cast<const unsigned char*>(
                    &wrapper->resourcePool[index].tag);
            outRgb[0] = static_cast<float>(                    // 0x4078E1
                static_cast<double>(spill[0]) * 0.00390625);
            outRgb[1] = static_cast<float>(                    // 0x4078ED
                static_cast<double>(spill[1]) * 0.00390625);
            outRgb[2] = static_cast<float>(                    // 0x4078F6
                static_cast<double>(spill[2]) * 0.00390625);
            return outRgb;
        }
        if (++index >= 10000)                                   // 0x4078A5
            return outRgb;
    }
}

namespace {

// NVAPI interface id resolved through nvapi_QueryInterface (opaque
// selector, kept in hex).
constexpr std::uint32_t kNvapiStereoReverseBlitControlId = 0x3CD58F89u;

// VA 0x004CC0F0 - NVAPI stereo call, interface id 0x3CD58F89.
// __cdecl(void* stereoHandle, unsigned enable) -> NvAPI status.
// The original consults the init-time nvapi_QueryInterface pointer
// (@0x542660, set by the 0x4C6940 init chain), caches the resolved
// function pointer in dword_542668 behind the one-shot flag byte_54266C,
// and returns -3 when the interface cannot be resolved.  The port resolves
// lazily through the same arch-split LoadLibrary path used by
// src/render/stereo_nvapi.cpp and src/app/shutdown_cleanup.cpp (x64
// original: sub_7FF7CB4FEA80 loads "nvapi64.dll"); the two
// conditional trace hooks (dword_545948/dword_54594C) are always-null
// instrumentation in the original and are omitted.
int NvapiStereo3CD58F89(void* stereoHandle, unsigned enable) {
    using QueryInterface = void*(__cdecl*)(std::uint32_t);
    using StereoCall = int(__cdecl*)(void*, unsigned);
    static StereoCall call = nullptr;    // mirrors dword_542668 cache
    static bool resolved = false;        // mirrors byte_54266C one-shot
    if (!resolved) {
        resolved = true;
        HMODULE module = LoadLibraryA(sizeof(void*) == 8 ? "nvapi64.dll"
                                                         : "nvapi.dll");
        if (module != nullptr) {
            auto query = reinterpret_cast<QueryInterface>(
                GetProcAddress(module, "nvapi_QueryInterface"));
            if (query != nullptr)
                call = reinterpret_cast<StereoCall>(
                    query(kNvapiStereoReverseBlitControlId));
        }
    }
    if (call == nullptr)
        return -3;                                               // 0x4CC136
    return call(stereoHandle, enable);                           // 0x4CC17D
}

}  // namespace

// VA 0x00407470 - stereo activation toggle on the 0x1D574 wrapper.
// thiscall(this = 0x1D574 wrapper, char enable) -> NvAPI status.
// Forwards the NVAPI stereo handle at wrapper+120020 (slot 30005) to the
// 0x4CC0F0 bridge.  Called from the frame driver (0x46B090):
//   0x46EA51 push 1 - re-enable stereo after the device was reset
//            (TestCooperativeLevel 0x88760869 path, right after
//            PostDeviceReset 0x440DB0);
//   0x46EDE4 push 0 - disable stereo while the app+0xA0D61 record flag
//            is set.
// The unsigned char* parameter is kept because record_readback.cpp (outside
// this conversion wave) passes the raw wrapper pointer.
int StereoActivationToggle(unsigned char* wrapper, std::uint8_t enable) {
    void* stereoHandle =
        reinterpret_cast<D3DRenderer*>(wrapper)->stereoHandle;  // 0x407474
    return NvapiStereo3CD58F89(stereoHandle, enable);           // 0x407484
}

// VA 0x00408EF0 - DirectShowInit.
// __stdcall(HWND hWnd) -> success byte.  CoInitialize(nullptr) >= 0
// returns 1; failure shows "Failed CoInitialize!" (0x529858) under the
// caption "DirectShowInit" (0x529870) and returns 0.  The single original
// caller 0x466D20 (CreateUIControls) at 0x467320 already carries this
// body inline in src/window/ui_init.cpp; this standalone definition makes
// the VA available to the DirectShow cluster without changing that port.
bool DirectShowInit(HWND hWnd) {
    if (CoInitialize(nullptr) >= 0)                              // 0x408EFA
        return true;                                             // 0x408F18
    MessageBoxA(hWnd, "Failed CoInitialize!", "DirectShowInit", 0);
    return false;                                                // 0x408F15
}

// ---------------------------------------------------------------------------
// NOT ported in this file (tracked for completeness):
//
// VA 0x00406950 - physics collision debug draw.  Already ported as
//   DrawPhysicsCollisionDebug(void*) in src/render/debug_geometry.cpp
//   (declared in include/mikudancestudio/ported_funcs.hpp); red pass for bodies
//   with a non-zero group mask, green pass otherwise.
//
// VA 0x00403030 - rigid-body construction-info constructor.  This is the
//   stock Bullet 2.75 btRigidBodyConstructionInfo ctor compiled into the
//   original (damping 0/0, friction 0.5, restitution 0, sleeping
//   0.8/1.0, additional damping off with 0.005/0.01 factors, identity
//   start transform), not application code; the ported callers already
//   construct the real library object (src/physics/scene_create.cpp:160
//   at 0x405F6D and src/physics/physics_create.cpp:456 at 0x4067EF).
//
// VA 0x004095D0 / 0x00409170 / 0x004096D0 / 0x00409700 - DirectShow AVI
//   recording graph cluster (pin finder, compressor moniker bind, ...).
//   Implemented in src/app/dshow_record_graph.cpp; see its named helpers
//   and the custom COM interface contract documented there.
// ---------------------------------------------------------------------------

}  // namespace mikudancestudio
