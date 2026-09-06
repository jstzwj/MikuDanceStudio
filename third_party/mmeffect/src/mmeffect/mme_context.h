// mme_context.h - the MME context singleton and the effect-owner manager.
//
// Evidence (globals_structures.md sections 3-4):
//   - MME context = DAT_1800d9bb8, a 0x480-byte object allocated in Initialize
//     (operator new(0x480) + FUN_18005b030 ctor) and freed in Cleanup after
//     FUN_18005b2f0. 52 references across the render path.
//   - Effect-owner manager = DAT_1800d9a40, a 400-byte object constructed by
//     FUN_18002a220 in Initialize, freed via FUN_180058740 + free in Cleanup.
//
// PHASE 1 DIVERGENCE (documented): the std containers below replace the
// original's MSVC-era raw layouts (hash map at ctx+0x118, vector at
// ctx+0xf8..0x108, containers at ctx+0x1b8/+0x1e0/+0x218, manager list at
// +0x70) - behavioral equivalence, not byte equivalence.
#pragma once

#include <list>
#include <map>
#include <unordered_map>
#include <vector>

#include <d3d9.h>

#include "render_snapshot.h"

namespace mme {

class ModelData;
struct MaterialBinding;

// Effect-owner manager (original DAT_1800d9a40, 400 bytes, FUN_18002a220 ctor
// / FUN_180058740 dtor). Carries the registered-object list (+0x70), the plan
// dirty flag (+0x90), the binding std::map (+0xa0; key (materialCount,
// ModelData*, subset) per MME_SelectMaterialEffectBinding / FUN_18002d910)
// and the sorted extra-pass plan (+0x158, filled by the FUN_18002baa0 sort).
class EffectOwnerManager {
public:
    // The +0xa0 map key (u32 materialCount, ModelData*, int subset), ordered
    // exactly like the original's tuple comparison (count, model, subset).
    struct BindingKey {
        unsigned int materialCount;
        ModelData*   model;
        int          subset;
        BindingKey(unsigned int count, ModelData* m, int s)
            : materialCount(count), model(m), subset(s) {}
        bool operator<(const BindingKey& other) const
        {
            if (materialCount != other.materialCount) {
                return materialCount < other.materialCount;
            }
            if (model != other.model) {
                return model < other.model;
            }
            return subset < other.subset;
        }
    };

    EffectOwnerManager();
    ~EffectOwnerManager();

    // [0x18005b7a0 L56-60] registration inserts the model into the manager
    // list (FUN_1800044a0/FUN_180039d10) and sets the dirty flag at +0x90.
    void RegisterObject(ModelData* model);

    // [0x18005b910 L21] FUN_18002a430 erase.
    void UnregisterObject(ModelData* model);

    std::list<ModelData*> registeredObjects;  // manager+0x70 (exact container type UNCERTAIN)
    bool planDirty;                           // manager+0x90 ("plan dirty", set 1 on registration)

    // manager+0xa0: (materialCount, model, subset) -> binding object.
    std::map<BindingKey, MaterialBinding*> bindings;

    // manager+0x158: the sorted extra-pass plan (FUN_18002baa0 tail), copied
    // into ctx+0x148 by FUN_18005fd30.
    std::vector<ModelData*> orderedPlan;
};

// MME context singleton (original DAT_1800d9bb8, 0x480 bytes, ctor
// FUN_18005b030 / dtor FUN_18005b2f0).
class MmeContext {
public:
    explicit MmeContext(IDirect3DDevice9* device);
    ~MmeContext();

    MmeContext(const MmeContext&) = delete;
    MmeContext& operator=(const MmeContext&) = delete;

    // --- fields (original offsets in comments; ctor FUN_18005b030) ---
    IDirect3DDevice9* device;                 // ctx+0x00 (AddRef'd [ctor L10-14])
    ModelData*        lastDrawnModel;         // ctx+0x08 (pass-change detector [18005d340 L88-98])
    unsigned char     bindingReadyFlag;       // ctx+0x10 [18005d340 L54,99-105]
    unsigned char     errorReportedFlag;      // ctx+0x11 [18005d340 L107]
    unsigned char     backgroundDrawnFlag;    // ctx+0x12 [18005d340 L55; set by the post-effect runner]
    unsigned char     effectEnabled;          // ctx+0x13 ("effect rendering enabled", ctor sets 1
                                              // [ctor L77]; gates OnEndScene/OnClear and the draw path)
    int               lastRepeatCount;        // ctx+0x14 (last ExpGetRenderRepeatCount [18005d340 L37-41])

    IDirect3DDevice9* bindingContextDevice;   // ctx+0x68 (AddRef'd [ctor L17-22]; source of the
                                              // binding contexts via FUN_180055690)

    // ctx+0x68..0x88 binding-context pool state (FUN_180055690): the round
    // -robin index (+0x70) and the pooled IDirect3DStateBlock9 vector
    // (+0x78..0x88). FUN_180055780 releases the pool (device loss / dtor).
    unsigned long long bindingContextIndex;
    std::vector<IDirect3DStateBlock9*> bindingContextPool;

    // ctx+0x60: the post-effect runner's borrowed binding context
    // (FUN_18005e210 acquires, Captures at slot 0x20, Applies at slot 0x28).
    IDirect3DStateBlock9* postBindingContext;

    // ctx+0x20..0x40: the cached render-target/depth surfaces of the state
    // re-init block (FUN_18005c510: release + GetRenderTarget(0..3) /
    // GetDepthStencilSurface).
    IDirect3DSurface9* cachedRenderTargets[4];
    IDirect3DSurface9* cachedDepthStencil;

    // ctx+0x188 word (0x100 = set): "offscreen render targets present" marker
    // (FUN_18005c510 sets 0x100; FUN_18005e210 gates the Clear on it).
    unsigned int hasRenderTargetsFlag;
    // ctx+0x438: the post-effect Clear gate; ctx+0x439: the all-special flag
    // (FUN_18005c510's renderClass walk); ctx+0x189: 0x100 word alias - the
    // port keeps the single hasRenderTargetsFlag.
    unsigned char postClearEnabled;
    unsigned char allObjectsSpecialFlag;
    // ctx+0x189 byte: the DA50 post-effect-error latch (the original sets
    // it once the failure message has been shown for this session).
    unsigned char postEffectRanFlag;

    // Reconstruction addition (not in the original layout): the last HRESULT
    // produced by the post-effect/animated-texture machinery. The original
    // re-derives it inside FUN_18005da50 by re-running FUN_180001880; Phase 3
    // fills it from the script runner, Phase 2 leaves S_OK.
    unsigned long lastPostEffectHr;

    // ctx+0x3c / ctx+0x42: the per-target clear-color/depth maps (the SAS
    // render-target tables; keyed by surface). Phase 2 stages the host clear
    // color here; the SAS target machinery (Phase 3) consumes them.
    D3DCOLOR postClearColor;
    std::map<IDirect3DSurface9*, D3DCOLOR> clearColorMap;
    std::map<IDirect3DSurface9*, float> clearDepthMap;

    // ctx+0x98 vector (FUN_180060140 push target): background/draw-type-0
    // snapshot records consumed by the post-effect runner (Phase 2 sas_exec).
    std::vector<RenderSnapshot> pendingSnapshots;

    // ctx+0x190 map (FUN_1800601e0): the FUN_18005d5c0 background-quad fixup
    // cache - source stream vertex buffer -> rescaled copy. Owning references;
    // released with the context (device loss releases them first via
    // OnLostDevice's binding-pool walk).
    std::map<IDirect3DVertexBuffer9*, IDirect3DVertexBuffer9*> backgroundFixedVbs;

    // ctx+0xf8..0x108 vector of all registered ModelData pointers
    // (MME_RegisterModelData push_back / MME_UnregisterModelData erase).
    std::vector<ModelData*> models;

    // ctx+0x118 id -> ModelData* hash map (MME_FindOrCreateModelEntry
    // [0x180060ca0]; Phase 1 uses std::unordered_map).
    std::unordered_map<unsigned long long, ModelData*> modelRegistry;

    // ctx+0x168 current effect-binding object (engine-internal; Phase 2).
    void* currentBindingObject;

    // ctx+0x1b8/+0x1e0/+0x218 pass-plan containers (Phase 2 pass_planner);
    // ctx+0x148/0x150 pass list read by HandleDrawIndexedPrimitive/
    // FUN_18005d130 for the repeat count.
    std::vector<ModelData*> passPlanA;        // ctx+0xb8..0xc8 (renderClass 1)
    std::vector<ModelData*> passPlanB;        // ctx+0xd8..0xe8 (renderClass 2)
    std::vector<ModelData*> renderPassList;   // ctx+0x148..0x150 (FUN_18005fd30 copy)
    std::vector<void*> passTrie;              // ctx+0x1b0 container (Phase 2 seam)

    // ctx+0x170..0x188: the last viewport seen by the plan rebuild
    // (FUN_18005b9e0 L449-484 compares GetViewport against this).
    D3DVIEWPORT9 lastPlanViewport;
    bool lastPlanViewportValid;               // ctx+0x18a (set 1 on change)

    // ctx+0x240 animated-texture collection (ctor FUN_180001000, ticked per
    // frame by FUN_180001320 from OnEndScene). Phase 2 seam (anime_texture).
    void* animatedTextures;

    // ctx+0x440 third device reference (AddRef'd [ctor L66-69]).
    IDirect3DDevice9* thirdDeviceRef;

    // Effect-owner manager pointer. In the original this is a separate global
    // (DAT_1800d9a40); the Phase 1 plan surfaces it here as well.
    EffectOwnerManager* ownerManager;

    // [0x180055780] FUN_180055780: release every pooled binding-context state
    // block and reset the round-robin index (device loss / context dtor).
    void ReleaseBindingContextPool();
};

// Global singleton accessors (DAT_1800d9bb8 / DAT_1800d9a40 live in mme_globals).
MmeContext* MmeGetContext();

// [0x180060ca0] MME_FindOrCreateModelEntry: returns the ModelData* registered
// under `id`, inserting a null entry when absent (the original allocates a
// 0x20-byte hash node and stores a null value; OOM throws std::bad_alloc).
ModelData* MmeFindOrCreateModelEntry(unsigned long long id);

// [0x18005b7a0] MME_RegisterModelData: operator new(0x370) + ModelData ctor,
// push into the ctx+0xf8 vector, insert into the ctx+0x118 hash map, register
// with the manager list and set the manager dirty flag.
void MmeRegisterModelData(unsigned long long objectId, const char* filename, int kind,
                          unsigned int materialCount, IUnknown* reservedObject);

// [0x18005b910] MME_UnregisterModelData: erase from the manager list, erase the
// hash entry, remove from the ctx+0xf8 vector, run the ModelData destructor and
// free the object.
void MmeUnregisterModelData(unsigned long long objectId);

// [0x18005b9e0] MME_RebuildRenderPassPlan. Implemented in pass_planner.cpp:
// the per-model scratch reset, the host object scan (ExpGetPmd*/ExpGetAcs*),
// the LoadedPMMFile EMM autoload, the pass bookkeeping and the repeat-count
// tail (N extra passes -> ExpSetRenderRepeatCount(N+1)).
void MmeRebuildRenderPassPlan();

} // namespace mme
