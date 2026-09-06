// mmhack_api.h — the 22 state-query exports of MMHack.dll (reconstruction header).
//
// Signatures follow MMD_9.31_MME_X_Development_Kit/analysis/sdk/mmd_931_mmhack.hpp
// and analysis/reports/architecture/mmhack_render_exports.csv. All pointers
// returned by GetTexture/GetToonTexture/GetSphereMapTexture/GetCurrentEffect/
// GetMaterialName/LoadedPMMFile/SavedPMMFile are borrowed references.
#pragma once

#include <windows.h>
#include <d3d9.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef enum MMH_SPHERE_MAP_MODE {
    MMH_SPHERE_NONE      = 0,
    MMH_SPHERE_MULTIPLY  = 1,
    MMH_SPHERE_ADD       = 2,
    MMH_SPHERE_SUBTEXTURE= 3,
} MMH_SPHERE_MAP_MODE;

// Render target kinds tracked by MMHack (draw-type values seen by
// GetCurrentDrawType). Recovered from MMHack draw dispatch; see
// MMEffect.dll.c snapshot +0x2C/+0x30 semantics.
typedef enum MMH_DRAW_TYPE {
    MMH_DRAW_NONE            = 0,  // not inside a tracked object draw
    MMH_DRAW_NORMAL_NOSELFSHADOW = 1,  // normal object pass, self-shadow off
    MMH_DRAW_NORMAL_SELFSHADOW   = 2,  // normal object pass, self-shadow on
    MMH_DRAW_SHADOW          = 3,  // ground/projected shadow pass
    MMH_DRAW_EDGE            = 4,  // edge (outline) pass
    MMH_DRAW_ZPLOT           = 5,  // self-shadow Z-buffer plot pass
} MMH_DRAW_TYPE;

// --- 22 exports (alphabetical order = ordinal order 1..22) ---
extern BOOL     __cdecl GetAcsAttachedPmd(unsigned long long accessory_id,
                                          unsigned long long* model_id, int* bone_index);
extern int      __cdecl GetBlendMode(void);
extern D3DCOLOR __cdecl GetClearColor(void);
extern int      __cdecl GetCurrentDrawType(void);
extern void*    __cdecl GetCurrentEffect(void);
extern float    __cdecl GetCurrentFrameTime(void);
extern unsigned long long __cdecl GetCurrentModelID(void);
extern int      __cdecl GetCurrentSubsetIndex(void);
extern HWND     __cdecl GetDrawnWindow(void);
extern void     __cdecl GetLightViewProjMatrix(D3DMATRIX* light_view,
                                               D3DMATRIX* light_projection,
                                               D3DMATRIX* light_view_projection);
extern HWND     __cdecl GetMMDMainWindow(void);
extern const wchar_t* __cdecl GetMaterialName(unsigned long long object_id,
                                              unsigned long material_index, int name_kind);
extern unsigned char __cdecl GetSphereMapMode(void);
extern IDirect3DBaseTexture9* __cdecl GetSphereMapTexture(void);
extern IDirect3DBaseTexture9* __cdecl GetTexture(void);
extern IDirect3DBaseTexture9* __cdecl GetToonTexture(void);
extern BOOL     __cdecl IsDebugMode(void);
extern BOOL     __cdecl IsEditMode(void);
extern BOOL     __cdecl IsEffectFileUsed(void);
extern BOOL     __cdecl IsToonUsed(void);
extern const wchar_t* __cdecl LoadedPMMFile(void);
extern const wchar_t* __cdecl SavedPMMFile(void);

#ifdef __cplusplus
}
#endif
