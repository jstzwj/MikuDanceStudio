// mmhack_state.h - Built-in MMEffect render-state tracking block (the original MMHack.dll .data section).
//
// Each field comment preserves its original .data slot address as evidence. The 22 state-query functions read this
// block. In the original, writers are the MyDirect3D9/MyDirect3DDevice9/MyD3DXMesh wrappers and
// PMM file-monitoring hooks; here, they are the MmeHost* integration layer in src/mme_host.cpp (called
// directly at the corresponding points in the host rendering pipeline).
#pragma once

#include <windows.h>
#include <d3d9.h>
#include <d3dx9.h>
#include <string>
#include <vector>
#include <map>
#include <unordered_map>
#include <list>

// --- 12 MME technique descriptors, original .rdata static table [0x18005bdf0] -------------------
// Flag bytes: [0]=base texture used, [1]=sphere(Sphia), [2]=cd2(subtexture), [3]=Diffuse.
// Registered in the technique cache by MmeHostSetStandardEffect (the registration path of the original Hooked_D3DXCreateEffect-
// FromResourceA [0x180004440]).
struct MmhTecDesc { const char* name; DWORD flags; };
extern const MmhTecDesc g_tecTable[12];
const int MMH_TEC_COUNT = 12;

// Per-object data record (original: std::map<unsigned long long, ObjData>, keyed by host object
// id, DAT_18006e958; accessor FUN_18000f2b0).
// MmhMaterialEntry is the original 0x78-byte entry, populated by MmhMaterialTableLoadFromFile
// [FUN_18000e020] by parsing model files directly:
//   +0x00 texName - toon texture reference (directory-qualified path, except built-in toon01..toon10.bmp)
//   +0x28 name0   - kind 0 name (PMX Japanese name / line i of PMD "<model>.txt")
//   +0x50 name1   - kind 1 name (PMX English name / repeated PMD line)
struct MmhMaterialEntry {
    std::wstring texName;    // entry +0x00: toon texture path
    std::wstring name0;      // entry +0x28: kind 0 name (Japanese)
    std::wstring name1;      // entry +0x50: kind 1 name (English)
};
struct MmhObjData {
    unsigned long long id = 0;
    bool isPmd = false;
    std::wstring modelName;              // FUN_18000f340: value+0x30 wstring
    std::vector<MmhMaterialEntry> mats;  // Original 0x78-byte entry
    // Attachment fields (original ObjData +0x240/+0x244). The original library has no writers at all;
    // the real data source for GetAcsAttachedPmd [0x1800012d0] is sub_18000F2B0's fallback return on a miss
    // at [0x18000f331]: treat the id as a host accessory pointer and read its current
    // +0x240 attachPmdIndex (-1 = unattached)/+0x244 attachBoneIndex. This project
    // reads the same offsets directly from the host's AccessoryRecord in mmhack_getters.cpp (on x64,
    // 576/580 match the original). These two members remain solely as layout anchors and have no readers.
    int attachPmdIndex = -1;             // +0x240 (-1 = unattached)
    int attachBoneIndex = 0;             // +0x244
};

// BeginScene cached object (original: 0x50-byte node table at wrapper +0x58, id map +0x30,
// order map +0x60; built by [0x180002de0]).
struct MmhCachedObject {
    unsigned long long id = 0;   // node[2]
    int isPmd = 0;               // node[3]: 1 = PMD model, 0 = accessory
    std::string fileName;        // node[1]: ANSI filename (ExpGetPmdFilename)
    void* texRef = nullptr;      // node[4]: AddRef'd object captured at creation
    MmhObjData* data = nullptr;  // node[5]: MmhGetObjectData(id) result
};

// Host exports (a subset of the 37 Exp* functions in the original MikuMikuDance.exe / this project's MikuMikuDanceE.exe).
// See MMDExport.h for declarations.
#include "MMDExport.h"

// --- Global state (single instance) ------------------------------------------------------
struct MmhState {
    // --- Per-frame/per-draw state (written by BeginScene + draw forwarding) -----------------------
    unsigned long long currentModelID = 0;   // DAT_18006e868 (GetCurrentModelID)
    int    currentDrawType = 0;              // DAT_18006e798 (GetCurrentDrawType = ExpGetCurrentTechnic)
    int    currentObjectKind = 0;            // DAT_18006e870 (1 = PMD model, 0 = accessory)
    int    currentObjectIndex = 0;           // Wrapper +0x28 (current object)
    int    currentSubsetIndex = -1;          // DAT_18006ccc0 (GetCurrentSubsetIndex, -1 = none)
    int    blendMode = 0;                    // DAT_18006e7b8 (GetBlendMode; D3DRS_DESTBLEND == D3DBLEND_ONE)
    bool   zbufferUsed = false;              // DAT_18006e7b8 twin (GetRenderState(D3DRS_DESTBLEND)==2 -> 1)
    void*  currentEffect = nullptr;          // DAT_18006e7a0 (GetCurrentEffect, ID3DXEffect*)
    bool   effectFileUsed = false;           // DAT_18006e795 (IsEffectFileUsed; shader path active)
    bool   toonAvailable = false;            // DAT_18006e795 companion (fixed-function path)

    // --- Texture capture (borrowed pointers, refreshed per draw) ------------------------------------
    IDirect3DBaseTexture9* baseTexture = nullptr;    // DAT_18006e7a8 (GetTexture)
    IDirect3DBaseTexture9* sphereTexture = nullptr;  // DAT_18006e7b0 (GetSphereMapTexture)
    unsigned char sphereUsed = 0;            // byte @0x18006e789 (flag byte 1)
    unsigned char subTexUsed = 0;            // byte @0x18006e78a (flag byte 2 = cd2)
    unsigned char diffuseFlag = 0;           // byte @0x18006e78b (flag byte 3)
    unsigned char texUsed = 0;               // byte @0x18006e788 (flag byte 0)
    unsigned char sphereAdd = 0;             // DAT_18006e797 ("spadd" / additive sphere map)
    bool   toonUsed = false;                 // Derived: IsToonUsed

    // --- Windows --------------------------------------------------------------
    HWND mainWindow = nullptr;               // DAT_18006e7e0 (GetMMDMainWindow; root owner)
    HWND deviceWindow = nullptr;             // DAT_18006e7c8 (hDeviceWindow tracking)
    HWND presentWindow = nullptr;            // DAT_18006e7c0 (GetDrawnWindow)

    // --- Miscellaneous ---------------------------------------------------------------
    float frameTime = 0.0f;                  // DAT_18006e79c (GetCurrentFrameTime = ExpGetFrameTime)
    int   notEditMode = 0;                   // DAT_18006e796 (IsEditMode returns !this)
    int   modalFlag = 0;                     // DAT_18006e7bd (sticky modal dialog flag)
    bool  debugMode = false;                 // DAT_18006e7bc (IsDebugMode; MMEffect.debug exists)
    bool  effectsDisabled = false;           // DAT_18006e794 (MME initialization failed/disabled)
    D3DCOLOR clearColor = 0;                 // DAT_18006ccc4 (GetClearColor; captured during Clear)

    // --- PMM file paths (LoadedPMMFile / SavedPMMFile) ------------------------
    std::wstring loadedPMMFile;              // DAT_18006cd28 (+ flag DAT_18006cd38)
    bool loadedPmmFlag = false;
    std::list<std::wstring> savedPmmList;    // DAT_18006e848/850 (SavedPMMFile pops the queue front)
    std::wstring currentOpenFile;            // DAT_18006cda0 (CreateFileW GENERIC_READ context)

    // --- Global object data map (DAT_18006e958, accessor FUN_18000f2b0) ---------------
    std::map<unsigned long long, MmhObjData> objData;
    MmhObjData objDataDefault;               // Header-node value (empty entry)

    // --- Texture cache: filename -> entry (hash-map family DAT_18006ccd0) ----------------
    struct TexCacheEntry {
        IDirect3DBaseTexture9* tex = nullptr;     // First texture for this filename
        IDirect3DBaseTexture9* toonTex = nullptr; // Lazy = tex (node +0x50)
    };
    std::unordered_map<std::wstring, TexCacheEntry> texCache;

    // --- Technique cache: D3DXHANDLE -> table index (FUN_1800061a0 map) ------------------
    std::map<const char*, int> tecCache;     // Original DAT_18006ccd0 map

    // --- Light matrices (GetLightViewProjMatrix recomputes on every call) ---------------------
    D3DMATRIX worldMatrix;                   // DAT_18006e8b0 (GetTransform(D3DTS_WORLDMATRIX(0)))
    D3DMATRIX worldInverse;                  // DAT_18006e8f0 (D3DXMatrixInverse of world)
};
extern MmhState g_mmh;

// --- Implemented in mmhack_registry.cpp ---------------------------------------------
MmhObjData* MmhGetObjectData(unsigned long long id);          // [FUN_18000f2b0]
bool  MmhIsKnownObjectId(unsigned long long id);              // [FUN_18000fb60 approximation]
IDirect3DBaseTexture9* MmhGetCurrentToonTexture(unsigned long long modelDataId); // [FUN_18000f810]
void  MmhRecordTextureFile(const std::wstring& name, IDirect3DBaseTexture9* tex); // [FUN_18000f3d0]
void  MmhRecordTecHandle(D3DXHANDLE h, int index);            // Technique cache insertion
int   MmhTecCacheIndex(const char* tecName);                  // [FUN_1800061a0 approximation]

// --- Implemented in mmhack_material.cpp ---------------------------------------------
const wchar_t* MmhGetMaterialName(unsigned long long object_id,
                                  unsigned long material_index, int name_kind);

// --- Implemented in mmhack_material_table.cpp ---------------------------------------
// Parse model files (PMD/PMX) directly to fill per-material entries: +0x00 toon texture, +0x28 Japanese name,
// +0x50 English name; missing files leave the table unchanged, corrupt data may fill it partially.
void MmhMaterialTableLoadFromFile(const wchar_t* modelPath,
                                  std::vector<MmhMaterialEntry>* out);

// --- Implemented in mmhack_lightmatrix.cpp ------------------------------------------
void MmhComputeLightViewProjMatrix(D3DMATRIX* outView, D3DMATRIX* outProj,
                                   D3DMATRIX* outViewProj);
