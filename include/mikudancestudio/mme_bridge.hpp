// ===========================================================================
// mme_bridge.hpp - host bridge for statically embedded MMEffect
// ===========================================================================
// Bridge responsibilities: call MmeHost* directly at the corresponding rendering stages (see
// third_party/mmeffect/include/mme_host_api.h). Register any valid device; a missing standard effect
// still enters MME lazy initialization, preserving the original per-frame failure diagnostics and retries.
//
// Draw contract: once Draw* is passed to the bridge, the host must not issue the same draw again. The engine
// either reissues it through the assigned effect technique or forwards it unchanged (as original MMHack does).
// =========================================================================//
#pragma once

#include <d3d9.h>

namespace mikudancestudio {

class MMDApp;

namespace mme {

// Called after device creation (end of InitD3D): register the main window and embedded standard effect. If the
// standard effect is null, MME lazy initialization reports failure and retries on the next BeginScene. During WM_CREATE,
// app->Hwnd() is not assigned yet; pass the window handle received by InitD3D.
void OnDeviceCreated(MMDApp* app, HWND hwnd);

// Called before device destruction (application shutdown/device release paths).
void OnDeviceDestroyed(MMDApp* app);

// Before/after device Reset (inside PostDeviceReset). OnResetDevice must immediately follow
// device->Reset and precede InitRenderStates, matching the original MMHack vtable slot 16
// Reset interceptor's [0x180003d30] OnLost->Reset->OnReset sequence (see the
// device lifecycle section header in mme_bridge.cpp).
void OnLostDevice(MMDApp* app);
void OnResetDevice(MMDApp* app);

// Per-frame hooks: Clear / BeginScene / EndScene / before main render-target readback.
HRESULT ClearScene(MMDApp* app, IDirect3DDevice9* device, unsigned long flags, D3DCOLOR color,
                   float z, unsigned long stencil);
HRESULT BeginScene(MMDApp* app, IDirect3DDevice9* device);
HRESULT EndScene(MMDApp* app, IDirect3DDevice9* device);
HRESULT Present(MMDApp* app, IDirect3DDevice9* device, const RECT* source, const RECT* destination,
                HWND destinationWindow, const RGNDATA* dirtyRegion);
void PreRenderTargetCopy(MMDApp* app, IDirect3DDevice9* device, IDirect3DSurface9* source);

// Draw forwarding (current object/material/technique state comes from Exp* queries).
HRESULT DrawPrimitive(IDirect3DDevice9* device, D3DPRIMITIVETYPE type, unsigned int start_vertex,
                      unsigned int primitive_count);
#ifdef MIKUDANCESTUDIO_DIAG
struct LineDrawBridgeProbe {
    unsigned long calls = 0;
    unsigned long hostCalls = 0;
    unsigned long directCalls = 0;
};
LineDrawBridgeProbe ProbeLineDrawBridge();
LineDrawBridgeProbe ProbeTriangleDrawBridge();
LineDrawBridgeProbe ProbeIndexedDrawBridge();
struct DrawSnapshotProbe {
    unsigned int pending = 0;
    int effectEnabled = 0;
    int bindingReady = 0;
    int bindingActive = 0;
    int endSceneFired = 0;
    int effectsDisabled = 0;
    int lineDisposition = 0;
};
DrawSnapshotProbe ProbeDrawSnapshot();
#endif
HRESULT DrawIndexedPrimitive(IDirect3DDevice9* device, D3DPRIMITIVETYPE type, int base_vertex_index,
                             unsigned int min_vertex_index, unsigned int vertex_count,
                             unsigned int start_index, unsigned int primitive_count);

// True when MME is initialized and the effect engine is enabled. The host uses this to select paths
// that bypass ID3DXMesh's internal drawing (which holds the real device and bypasses draw forwarding).
bool EffectsActive();
// User-visible effect policy, independent of bridge/resource initialization.
bool EffectsEnabled();
bool PrepareEffectAssignments(MMDApp* app);
void BeginPortableFrameClock();
void EndPortableFrameClock();

// Accessory mesh DrawSubset equivalent: when MME is active, set streams/indices/FVF manually and forward
// the draw (replicating ID3DXMesh::DrawSubset's hardware path); otherwise call the original
// DrawSubset directly. Returns the draw HRESULT.
HRESULT DrawAccessorySubset(MMDApp* app, void* accessory, unsigned long material);

// Notify successful .pmm project load/save points (EMM automatic loading/saving).
void NotifyPmmLoaded(MMDApp* app);
void NotifyPmmSaved(MMDApp* app);

// Register textures by path after successful model/accessory/toon creation (for GetToonTexture queries).
void RecordTexture(const wchar_t* path, IDirect3DBaseTexture9* texture);

} // namespace mme
} // namespace mikudancestudio
