// render_snapshot.h - the 0x220-byte per-draw render snapshot of MMEffect.dll.
//
// Field offsets are byte-exact per the kit evidence:
//   - kit: analysis/reports/architecture/mme_render_snapshot_layout.csv (PHASE4)
//   - decompiled 18005d340_MME_HandleDrawIndexedPrimitive.c (record construction)
//   - decompiled 180059c60_MME_CaptureCurrentRenderState.c (field capture)
//   - decompiled 180059ba0_MME_UpdateModelRenderSnapshot.c (draw-type normalize,
//     memcpy into ModelData+0x138, binding-context pointer at snapshot+0x20)
//
// Internal snapshots use typed assignment. The verified layout remains here
// as a reconstruction reference, not an external ABI requirement.
#pragma once

#include <d3d9.h>
#include <cstdint>
#include <cstring>

namespace mme {

struct RenderSnapshot {
    // ---- +0x00 ----
    uint8_t  enabled;               // +0x00 [18005d340] 1 = record built (indexed path), 0 = DrawPrimitive path
    uint8_t  pad00[3];
    // ---- +0x04 : indexed-draw arguments ----
    uint32_t primitive_type;        // +0x04 D3DPRIMITIVETYPE
    int32_t  base_vertex_index;     // +0x08
    uint32_t min_vertex_index;      // +0x0c
    uint32_t vertex_count;          // +0x10
    uint32_t start_index;           // +0x14
    uint32_t primitive_count;       // +0x18
    uint32_t reserved_01c;          // +0x1c (uninitialized in the original record)
    // ---- +0x20 ----
    void*    binding_context;       // +0x20 MME draw/binding context (FUN_180055690 result, ctx+0x68 pool);
                                    //        stored into ModelData+0x158 for cached snapshots [180059ba0]
#ifndef _WIN64
    uint32_t pad_ptr_20;            // x86: 指针 4 字节，显式补齐恢复 x64 偏移网格
#endif
    // ---- +0x28 ----
    int32_t  subset_index;          // +0x28 GetCurrentSubsetIndex() [180059c60 L53]
    int32_t  draw_type;             // +0x2c GetCurrentDrawType() raw value [18005d340 local_22c]
    int32_t  draw_type_index;       // +0x30 draw type 1..5 normalized to 0..4 [180059ba0 L17-35]
    uint32_t reserved_034;          // +0x34
    // ---- +0x38 : host textures (borrowed references) ----
    IDirect3DBaseTexture9* base_texture;    // +0x38 GetTexture() [180059c60 L41-43]
#ifndef _WIN64
    uint32_t pad_ptr_38;
#endif
    IDirect3DBaseTexture9* toon_texture;    // +0x40 GetToonTexture() [L44-45]
#ifndef _WIN64
    uint32_t pad_ptr_40;
#endif
    IDirect3DBaseTexture9* sphere_texture;  // +0x48 GetSphereMapTexture() [L46-47]
#ifndef _WIN64
    uint32_t pad_ptr_48;
#endif
    uint32_t sphere_mode;                   // +0x50 GetSphereMapMode(): 0 none / 1 mul / 2 add / 3 sub [L48-49]
    // ---- +0x54 : flags ----
    uint8_t  toon_used;             // +0x54 IsToonUsed() [L50-52]
    uint8_t  effect_file_used;      // +0x55 IsEffectFileUsed() [L40,51]
    uint8_t  base_texture_present;  // +0x56 GetTexture() != null [L42-43]
    uint8_t  reserved_057;          // +0x57
    // ---- +0x58 : matrices (D3DMATRIX = 0x40 bytes) ----
    D3DMATRIX model_world;          // +0x58 fixed-function: D3DTS_WORLD * inv(world@BeginScene) [L58-93]
                                    //        effect: ModelData+0xf8 scratch (accessory) or identity [L108-132]
    D3DMATRIX effect_world;         // +0x98 fixed-function: model_world with translation row zeroed [L59-92]
                                    //        effect: matWorld (DAT_1800d9b30) [L110] or identity
    D3DMATRIX rotation;             // +0xd8 matRotate handle DAT_1800d9b48 [L144]
    D3DMATRIX world_view_projection;// +0x118 matWorldViewProj handle DAT_1800d9b38 [L106]
    D3DMATRIX light_view_projection;// +0x158 matLightViewProj (effect) or model_world * light-view-proj [L93-103]
    // ---- +0x198 : eight D3DCOLORVALUE effect colors (0x10 each) ----
    D3DCOLORVALUE edge_color;       // +0x198 EgColor   (DAT_1800d9b50)
    D3DCOLORVALUE toon_color;       // +0x1a8 ToonColor (DAT_1800d9b58)
    D3DCOLORVALUE specular_color;   // +0x1b8 SpcColor  (DAT_1800d9b68)
    D3DCOLORVALUE diffuse_color;    // +0x1c8 DifColor  (DAT_1800d9b78)
    D3DCOLORVALUE texture_add;      // +0x1d8 TexCAdd   (DAT_1800d9b98)
    D3DCOLORVALUE texture_multiply; // +0x1e8 TexCMul   (DAT_1800d9ba0)
    D3DCOLORVALUE sphere_add;       // +0x1f8 SphCAdd   (DAT_1800d9ba8)
    D3DCOLORVALUE sphere_multiply;  // +0x208 SphCMul   (DAT_1800d9bb0)
    // ---- +0x218 ----
    int32_t  transparent;           // +0x218 transp bool (DAT_1800d9b90) [L138]
    uint32_t reserved_21c;          // +0x21c tail padding
};

static_assert(sizeof(RenderSnapshot) == 0x220, "RenderSnapshot must match the original 0x220-byte record");
static_assert(offsetof(RenderSnapshot, primitive_type)         == 0x04,  "snapshot layout drift at 0x04");
static_assert(offsetof(RenderSnapshot, binding_context)        == 0x20,  "snapshot layout drift at 0x20");
static_assert(offsetof(RenderSnapshot, subset_index)           == 0x28,  "snapshot layout drift at 0x28");
static_assert(offsetof(RenderSnapshot, draw_type)              == 0x2c,  "snapshot layout drift at 0x2c");
static_assert(offsetof(RenderSnapshot, draw_type_index)        == 0x30,  "snapshot layout drift at 0x30");
static_assert(offsetof(RenderSnapshot, base_texture)           == 0x38,  "snapshot layout drift at 0x38");
static_assert(offsetof(RenderSnapshot, toon_texture)           == 0x40,  "snapshot layout drift at 0x40");
static_assert(offsetof(RenderSnapshot, sphere_texture)         == 0x48,  "snapshot layout drift at 0x48");
static_assert(offsetof(RenderSnapshot, sphere_mode)            == 0x50,  "snapshot layout drift at 0x50");
static_assert(offsetof(RenderSnapshot, toon_used)              == 0x54,  "snapshot layout drift at 0x54");
static_assert(offsetof(RenderSnapshot, effect_file_used)       == 0x55,  "snapshot layout drift at 0x55");
static_assert(offsetof(RenderSnapshot, base_texture_present)   == 0x56,  "snapshot layout drift at 0x56");
static_assert(offsetof(RenderSnapshot, model_world)            == 0x58,  "snapshot layout drift at 0x58");
static_assert(offsetof(RenderSnapshot, effect_world)           == 0x98,  "snapshot layout drift at 0x98");
static_assert(offsetof(RenderSnapshot, rotation)               == 0xd8,  "snapshot layout drift at 0xd8");
static_assert(offsetof(RenderSnapshot, world_view_projection)  == 0x118, "snapshot layout drift at 0x118");
static_assert(offsetof(RenderSnapshot, light_view_projection)  == 0x158, "snapshot layout drift at 0x158");
static_assert(offsetof(RenderSnapshot, edge_color)             == 0x198, "snapshot layout drift at 0x198");
static_assert(offsetof(RenderSnapshot, toon_color)             == 0x1a8, "snapshot layout drift at 0x1a8");
static_assert(offsetof(RenderSnapshot, specular_color)         == 0x1b8, "snapshot layout drift at 0x1b8");
static_assert(offsetof(RenderSnapshot, diffuse_color)          == 0x1c8, "snapshot layout drift at 0x1c8");
static_assert(offsetof(RenderSnapshot, texture_add)            == 0x1d8, "snapshot layout drift at 0x1d8");
static_assert(offsetof(RenderSnapshot, texture_multiply)       == 0x1e8, "snapshot layout drift at 0x1e8");
static_assert(offsetof(RenderSnapshot, sphere_add)             == 0x1f8, "snapshot layout drift at 0x1f8");
static_assert(offsetof(RenderSnapshot, sphere_multiply)        == 0x208, "snapshot layout drift at 0x208");
static_assert(offsetof(RenderSnapshot, transparent)            == 0x218, "snapshot layout drift at 0x218");

class ModelData;

// [0x180059ba0] MME_UpdateModelRenderSnapshot: capture current state into *snap,
// normalize draw_type into draw_type_index, and for render-class 1/2 models with
// a negative cached subset index copy the record into ModelData+0x138 (0x220 memcpy)
// plus store the AddRef'd binding context at snapshot+0x20.
void MmeUpdateModelRenderSnapshot(ModelData* model, RenderSnapshot* snap);

// [0x180059c60] MME_CaptureCurrentRenderState: fill *snap from MMHack state
// queries and (when an effect file is used) the current ID3DXEffect parameters.
void MmeCaptureCurrentRenderState(ModelData* model, RenderSnapshot* snap);

// [0x18005af70] cached per-vertex value (ModelData+0x3c): queried once from
// GetStreamSource(0) stride and vertex-buffer desc.Size; returns the cached
// value or -1 when unavailable. Called at the top of UpdateModelRenderSnapshot.
int MmeCachePerVertexValue(ModelData* model);

// [0x18005a1e0] MME_ApplyModelRenderSnapshot: material/effect selection +
// parameter binding for the first use of a model in a pass.
// PHASE 2 seam (material_bind): Phase 1 provides the entry point only.
void MmeApplyModelRenderSnapshot(ModelData* model, void* bindingContext, const RenderSnapshot& snap);

} // namespace mme
