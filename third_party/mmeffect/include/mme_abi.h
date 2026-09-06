// mme_abi.h — the 11 MMEffect.dll callbacks (reconstruction header).
//
// Signatures follow MMD_9.31_MME_X_Development_Kit/analysis/sdk/mmd_931_mme.hpp,
// whose argument lists were recovered from MMHack.dll call sites (see
// analysis/reports/PHASE3_MME_ABI.md in the kit).
#pragma once

#include <windows.h>
#include <d3d9.h>

#ifdef __cplusplus
extern "C" {
#endif

// Model kind passed to OnCreateModel: 0 = accessory (*.x), 1 = PMD/PMX model.
typedef enum MME_MODEL_KIND {
    MME_MODEL_KIND_ACCESSORY = 0,
    MME_MODEL_KIND_PMD_PMX   = 1,
} MME_MODEL_KIND;

typedef int      (__cdecl *MME_InitializeFn)(IDirect3DDevice9* device);
typedef void     (__cdecl *MME_CleanupFn)(IDirect3DDevice9* device);
typedef void     (__cdecl *MME_OnCreateModelFn)(IDirect3DDevice9* device, unsigned long long object_id,
                                                const char* filename, int kind,
                                                unsigned long material_count,
                                                void* reserved0, IUnknown* reserved1);
typedef void     (__cdecl *MME_OnDeleteModelFn)(IDirect3DDevice9* device, unsigned long long object_id);
typedef void     (__cdecl *MME_OnBeginSceneFn)(IDirect3DDevice9* device);
typedef void     (__cdecl *MME_OnEndSceneFn)(IDirect3DDevice9* device);
typedef HRESULT  (__cdecl *MME_OnDrawPrimitiveFn)(IDirect3DDevice9* device,
                                                  D3DPRIMITIVETYPE primitive_type,
                                                  unsigned int start_vertex,
                                                  unsigned int primitive_count);
typedef HRESULT  (__cdecl *MME_OnDrawIndexedPrimitiveFn)(IDirect3DDevice9* device,
                                                         D3DPRIMITIVETYPE primitive_type,
                                                         int base_vertex_index,
                                                         unsigned int min_vertex_index,
                                                         unsigned int vertex_count,
                                                         unsigned int start_index,
                                                         unsigned int primitive_count);
typedef void     (__cdecl *MME_OnLostDeviceFn)(IDirect3DDevice9* device);
typedef void     (__cdecl *MME_OnResetDeviceFn)(IDirect3DDevice9* device);
typedef HRESULT  (__cdecl *MME_OnClearFn)(IDirect3DDevice9* device,
                                          unsigned long rect_count,
                                          const D3DRECT* rects,
                                          unsigned long flags,
                                          D3DCOLOR color,
                                          float z,
                                          unsigned long stencil,
                                          int is_main_target);

// The exports provided by MMEffect.dll (alphabetical, ordinals 1..11).
extern int      __cdecl Initialize(IDirect3DDevice9* device);
extern void     __cdecl Cleanup(IDirect3DDevice9* device);
extern void     __cdecl OnBeginScene(IDirect3DDevice9* device);
extern HRESULT  __cdecl OnClear(IDirect3DDevice9* device, unsigned long rect_count, const D3DRECT* rects,
                                unsigned long flags, D3DCOLOR color, float z, unsigned long stencil,
                                int is_main_target);
extern void     __cdecl OnCreateModel(IDirect3DDevice9* device, unsigned long long object_id,
                                      const char* filename, int kind, unsigned long material_count,
                                      void* reserved0, IUnknown* reserved1);
extern void     __cdecl OnDeleteModel(IDirect3DDevice9* device, unsigned long long object_id);
extern HRESULT  __cdecl OnDrawIndexedPrimitive(IDirect3DDevice9* device, D3DPRIMITIVETYPE primitive_type,
                                               int base_vertex_index, unsigned int min_vertex_index,
                                               unsigned int vertex_count, unsigned int start_index,
                                               unsigned int primitive_count);
extern HRESULT  __cdecl OnDrawPrimitive(IDirect3DDevice9* device, D3DPRIMITIVETYPE primitive_type,
                                        unsigned int start_vertex, unsigned int primitive_count);
extern void     __cdecl OnEndScene(IDirect3DDevice9* device);
extern void     __cdecl OnLostDevice(IDirect3DDevice9* device);
extern void     __cdecl OnResetDevice(IDirect3DDevice9* device);

#ifdef __cplusplus
}
#endif
