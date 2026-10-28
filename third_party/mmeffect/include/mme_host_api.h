// ===========================================================================
// mme_host_api.h - host integration API for embedded MMEffect.dll
// ===========================================================================
// Original MME intercepts MMD device calls through a d3d9.dll proxy + IAT/vtable hooks; this project
// has its own complete rendering pipeline, and the host (MikuDanceStudio.exe) directly
// calls this API at the corresponding stages. Each semantic matches an original MMHack interception point:
//
//   Host call site                    Original counterpart (MMHack interception)
//   ------------------------------    -------------------------------------
//   MmeHostSetMainWindow              Main-window discovery along CreateDevice's owner chain
//   MmeHostSetStandardEffect          Hooked_D3DXCreateEffectFromResourceA
//   MmeHostSetDrawnWindow             Presentation window recorded by Present
//   MmeHostClear                      Device Clear slot 43
//   MmeHostBeginScene                 Device BeginScene slot 41 (including lazy Initialize,
//                                     object addition/removal differences, OnBeginScene)
//   MmeHostDrawPrimitive              Device DrawPrimitive slot 81
//   MmeHostDrawIndexedPrimitive       Device DrawIndexedPrimitive slot 82
//   MmeHostPreRenderTargetCopy        UpdateSurface/GetRenderTargetData/
//                                     Early triggering at StretchRect slots 30/32/34
//   MmeHostEndScene                   Device EndScene slot 42 (including OnEndScene)
//   MmeHostOnLost/OnResetDevice       Before/after device Reset slot 16
//   MmeHostDestroyDevice              Device Release-to-zero destruction path
//   MmeHostNotifyPmmLoaded/Saved      Hooked_CreateFileW .pmm tracking
//   MmeHostRecordTextureFile          Hooked_D3DXCreateTextureFromFileEx* records
//
// Drawing contract matches the original: after handing Draw* to this API, the host must not issue it again;
// the effect engine either reissues it through the assigned technique or forwards it unchanged.
// =========================================================================//
#ifndef MIKUDANCESTUDIO_MME_HOST_API_H_
#define MIKUDANCESTUDIO_MME_HOST_API_H_

#include <windows.h>
#include <d3d9.h>

#ifdef __cplusplus
extern "C" {
#endif

// Process lifetime, separate from per-device Initialize/Cleanup.
void __cdecl MmeHostInitializeRuntime(HINSTANCE instance);
void __cdecl MmeHostShutdownRuntime();

// Record MMD's main window (target for MME menu injection and subclassing). Call after device creation, before the first frame.
void __cdecl MmeHostSetMainWindow(HWND window);

// Register direct accessors for original host model/accessory wide paths (internal MmdModelPathW/MmdAcsPathW in
// src/exports/effect_api.cpp, not exported). Equivalent to original MMHack's
// D3DXLoadMeshFromXW hook [0x1800043b0] registering the unchanged wide path in ObjData+0x30
// (wide-character material/toon parsing throughout, without an SJIS round trip). MMEffect.dll cannot resolve non-exported
// symbols from the EXE import library, so inject function pointers here. Call after device creation, before the first frame.
typedef const wchar_t* (__cdecl* MmeHostPathProviderFn)(int index);
void __cdecl MmeHostSetPathProviders(MmeHostPathProviderFn model_path,
                                     MmeHostPathProviderFn acs_path);

// Record the host's embedded standard effect (ID3DXEffect* compiled from resource 117/118, passed as an opaque
// pointer) and register 12 MME standard technique handles. Equivalent to original interception of
// the successful D3DXCreateEffectFromResourceA path.
void __cdecl MmeHostSetStandardEffect(void* effect);

// Record the current presentation window (source for GetDrawnWindow; main window during normal rendering, recording window
// during AVI output; MME subclasses it to obtain mouse coordinates).
void __cdecl MmeHostSetDrawnWindow(HWND window);

// Frame clear. Compare with the tracked render target to identify the main target; main-target clears record the clear color and pass through
// OnClear (when effects are enabled, suppress the clear and let post-processing Clear). Returns the device Clear result.
HRESULT __cdecl MmeHostClear(IDirect3DDevice9* device, unsigned long rect_count,
                               const D3DRECT* rects, unsigned long flags,
                               D3DCOLOR color, float z, unsigned long stencil);

// Frame BeginScene state machine: sample edit mode, lazy Initialize, object addition/removal differences
// (OnCreateModel/OnDeleteModel), cache the world matrix, track render targets, OnBeginScene
// (render-pass planning occurs here). notEditMode != 0 means non-edit mode (AVI output/modal).
// Initialization failures report an error each time, retain pending initialization and return S_OK without
// calling real BeginScene, as in the original; after successful initialization, call real BeginScene and return its result.
HRESULT __cdecl MmeHostBeginScene(IDirect3DDevice9* device, int not_edit_mode);

// Draw forwarding: the effect engine takes over (reissues through assigned effects) or forwards unchanged. Returns draw HRESULT.
HRESULT __cdecl MmeHostDrawPrimitive(IDirect3DDevice9* device,
                                       D3DPRIMITIVETYPE type,
                                       unsigned int start_vertex,
                                       unsigned int primitive_count);
#ifdef MIKUDANCESTUDIO_DIAG
struct MmeHostDrawSnapshotProbe {
    unsigned int pending = 0;
    int effectEnabled = 0;
    int bindingReady = 0;
    int bindingActive = 0;
    int endSceneFired = 0;
    int effectsDisabled = 0;
    int lineDisposition = 0;
};
MmeHostDrawSnapshotProbe __cdecl MmeHostProbeDrawSnapshot();
#endif
HRESULT __cdecl MmeHostDrawIndexedPrimitive(IDirect3DDevice9* device,
                                              D3DPRIMITIVETYPE type,
                                              int base_vertex_index,
                                              unsigned int min_vertex_index,
                                              unsigned int vertex_count,
                                              unsigned int start_index,
                                              unsigned int primitive_count);

// Call before main render-target readback ahead of EndScene (back-buffer capture/AVI sampling): if this frame's
// OnEndScene has not fired, trigger it first to ensure readback sees the post-processed result.
void __cdecl MmeHostPreRenderTargetCopy(IDirect3DDevice9* device,
                                        IDirect3DSurface9* source);

// Frame EndScene: trigger OnEndScene once (runs the post-processing chain), then call real EndScene.
HRESULT __cdecl MmeHostEndScene(IDirect3DDevice9* device);

// Device lost/reset pair (once before and after host Reset, matching original Reset slot 16 interception).
void __cdecl MmeHostOnLostDevice(IDirect3DDevice9* device);
void __cdecl MmeHostOnResetDevice(IDirect3DDevice9* device);

// Device destruction: Cleanup + release all tracked state (original device Release-to-zero path).
void __cdecl MmeHostDestroyDevice(IDirect3DDevice9* device);

// .pmm project load/save notifications (original Hooked_CreateFileW tracking). loaded is one-shot;
// saved notifications are queued and consumed in order.
void __cdecl MmeHostNotifyPmmLoaded(const wchar_t* path);
void __cdecl MmeHostNotifyPmmSaved(const wchar_t* path);

// Texture creation records (original Hooked_D3DXCreateTextureFromFileEx* tracking; bare toon01..10
// aliases are registered automatically).
void __cdecl MmeHostRecordTextureFile(const wchar_t* path,
                                        IDirect3DBaseTexture9* texture);

// True when the effect engine is initialized and enabled (used by the host for accessory manual DrawSubset and other
// decisions to bypass ID3DXMesh's internal drawing). Can be 1 only after registering the standard effect.
int __cdecl MmeHostEffectsActive(void);
// CPU policy query: bridge initialization and the user's effect toggle are
// different states. No device state or resource is read.
int __cdecl MmeHostEffectsEnabled(void);

#ifdef __cplusplus
}
#endif

#endif  // MIKUDANCESTUDIO_MME_HOST_API_H_
