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

#include "model_name_registry.h"

#include <list>
#include <map>
#include <memory>
#include <set>
#include <string>
#include <tuple>
#include <unordered_map>
#include <utility>
#include <vector>

#include <d3d9.h>

#include "render_snapshot.h"

namespace mme {

class ModelData;
struct MaterialBinding;
struct SasEffect;
struct SasResource;

// [FUN_180067500 / FUN_1800675E0 payload] a saved device target set: the
// original saves the current render targets into this shape (mask bit 0 =
// depth stencil, bits 1..4 = color targets 0..3 - the same bit layout as
// SasRunState::redirectedMask) and rebinds it on the snapshot error paths.
// Instances live on the stack (FUN_18005da50 / FUN_18005cac0 locals) and
// permanently inside the ctx+0x240 manager (mgr+0x50 mask / mgr+0x58 targets
// / mgr+0x78 depth / mgr+0x80 viewport).
struct MmeTargetSet {
    unsigned int       mask = 0;
    IDirect3DSurface9* targets[4] = {nullptr, nullptr, nullptr, nullptr};
    IDirect3DSurface9* depth = nullptr;
    D3DVIEWPORT9       viewport = {};
};

// Each slot has two owning caches. The active set only records identities;
// it does not extend a surface's COM lifetime (original manager +0x1D8).
struct MmeSnapshotCache {
    struct Key {
        unsigned int width, height;
        D3DFORMAT format;
        bool operator<(const Key& other) const {
            return std::tie(width, height, format) <
                   std::tie(other.width, other.height, other.format);
        }
    };
    struct ReleaseSurface {
        void operator()(IDirect3DSurface9* surface) const {
            if (surface != nullptr) surface->Release();
        }
    };
    using Surface = std::unique_ptr<IDirect3DSurface9, ReleaseSurface>;
    struct Slot {
        std::map<Key, Surface> exactSize;
        std::map<D3DFORMAT, Surface> atLeastSize;

        IDirect3DSurface9* Find(const Key& key, bool special);
        void Store(const Key& key, bool special, Surface surface);
        void Prune(const std::set<IDirect3DSurface9*>& active, bool special);
    };
    Slot colors[4];
    Slot depth;
    std::set<IDirect3DSurface9*> active;

    void DiscardInactiveFamily(bool special);
    void ReleaseSurfaces();
    void Prune();
};

// A render turn belongs to an assignment's OFFSCREENRENDERTARGET resource.
// Its stable ID is scoped by (parent turn, object, subset, resource name).
// Techniques are selected from the binding in the active turn at draw time.
struct MmeRenderPassItem {
    ModelData*   carrier;
    unsigned int turnId = 0;
    MaterialBinding* assignment = nullptr;
    SasResource* offscreen;   // the 0x2E resource this turn renders into
                              // (wrapper+0; null never occurs - a queue item
                              // exists only because a 0x2E resource did)
    MmeRenderPassItem() : carrier(nullptr), offscreen(nullptr) {}
    MmeRenderPassItem(ModelData* c, SasResource* o, unsigned int id = 0)
        : carrier(c), turnId(id), offscreen(o) {}
};

// Effect-owner manager (original DAT_1800d9a40, 400 bytes, FUN_18002a220 ctor
// / FUN_180058740 dtor). Carries the registered-object list (+0x70), the plan
// dirty flag (+0x90), the binding std::map (+0xa0; key (turnId,
// ModelData*, subset) per MME_SelectMaterialEffectBinding / FUN_18002d910)
// and the sorted extra-pass plan (+0x158, filled by the FUN_18002baa0 sort).
class EffectOwnerManager {
public:
    // The +0xa0 map key (u32 turnId, ModelData*, int subset), ordered
    // exactly like the original's tuple comparison (count, model, subset).
    struct BindingKey {
        unsigned int turnId;
        ModelData*   model;
        int          subset;
        BindingKey(unsigned int turn, ModelData* m, int s)
            : turnId(turn), model(m), subset(s) {}
        bool operator<(const BindingKey& other) const
        {
            if (turnId != other.turnId) {
                return turnId < other.turnId;
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

    // manager+0xa0: (turnId, model, subset) -> binding object.
    std::map<BindingKey, MaterialBinding*> bindings;
    unsigned int nextTurnId = 0;
    std::map<unsigned int, ModelData*> turnOwners;

    // manager+0x158: the sorted extra-pass plan (FUN_18002baa0 tail) - ONE
    // entry per queued OFFSCREENRENDERTARGET resource of each carrier (the
    // materialized 0x48 wrappers), copied entry-for-entry into ctx+0x148 by
    // FUN_18005fd30.
    std::vector<MmeRenderPassItem> orderedPlan;
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

    // ctx+0x18..0x60: the inline cached target set (verified against the
    // binary; FUN_18005c510's ==N branch sets the mask dword to 31, refetches
    // GetRenderTarget(0..3) into ctx+0x20..0x38 and GetDepthStencilSurface
    // into ctx+0x40, GetViewport into ctx+0x48; fcn_18005b9e0 zeroes the mask
    // and releases the refs at the frame head; FUN_18005d130 applies the set
    // (sub_1800675E0) when lastRepeatCount == 0 and FUN_18005e210's postClear
    // branch applies it around the Clear(0,0,7)). The former separate
    // cachedRenderTargets[4]/cachedDepthStencil fields were this set's
    // targets/depth slots.
    MmeTargetSet cachedTargetSet;

    // ctx+0x188 (byte 392): the turn's main-RT snapshot success flag. Only
    // FUN_18005da50 writes it - cleared at the null-binding branch head
    // (0x18005db8d, before the adaptive gate) and set when sub_180001880
    // returns S_OK (0x18005dd16); read by da50's host-Clear re-issue gate
    // (0x18005e11e) and by FUN_18005e210's empty-binding branch gate
    // (0x18005e297). FUN_18005c510 never touches this byte.
    unsigned char snapshotSuccessFlag;
    // ctx+0x189 (byte 393): the da50 post-effect-error latch (the ctor's
    // WORD write at ctx+0x189 inits it 0 - the write's high byte is
    // backgroundQuadViewportDirty below; da50 sets the latch once the failure
    // box has been shown, after which the snapshot branch is skipped for
    // good).
    unsigned char postEffectRanFlag;
    // ctx+0x438 (byte 1080, mgr+0x1F8): set ONLY by the persistent-surface
    // fast paths of sub_180002100/sub_180002440 (family-B + the mgr+0x1F9
    // gate + the sub_180002780 desc check, 0x180002237/0x180002590) and
    // cleared by FUN_18005c510's WORD write at ctx+0x438; gates FUN_18005e210's
    // Clear(0,0,7) (0x18005e2bb).
    unsigned char persistentClearUsed;
    // ctx+0x439 (byte 1081, mgr+0x1F9): the persistent-layer gate - set by
    // FUN_18005c510's WORD write (high half of the 0x100 store), cleared by
    // FUN_18005da50 when the cached main RT0/depth carry no staged clear
    // (0x18005dce9); read by the sub_180002100/sub_180002440 fast paths.
    unsigned char persistentLayerEligible;
    // ctx+0x43A (byte 1082, mgr+0x1FA): the plan-family flag - set 1 and
    // cleared when the plan-B walk finds a flag360!=1 && drawsGeometry object
    // (FUN_18005c510); selects the second snapshot cache family and the
    // rect-bounded copies (sub_180001880/sub_180001e70).
    unsigned char allObjectsSpecialFlag;

    // Reconstruction addition (not in the original layout): the last HRESULT
    // produced by the post-effect snapshot machinery. The original carries it
    // in the ctx+0x240 manager (mgr+0x1fc, written by sub_180002100 /
    // sub_180002440 on CreateRenderTarget / CreateDepthStencilSurface
    // failure) plus function locals; the port mirrors it here, written at
    // the two verified sub_180001880 call sites (FUN_18005da50's
    // null-binding branch and FUN_18005cac0) so the error report and the
    // dedup latch observe real failures instead of a constant S_OK.
    unsigned long lastPostEffectHr;

    // Turn-boundary snapshot manager (original ctx+0x240):
    // persistentTargetSet is captured at repeat==plan-size; mainTargetSet
    // is refetched by each snapshot and restored by copyback. Both saved
    // sets release their COM references at EndScene and on reset/failure.
    // snapshotCache owns per-slot exact-size (family A) and format-keyed,
    // at-least-size (family B) surfaces. allObjectsSpecialFlag selects B.
    // snapshotError retains the latest creator HRESULT.
    MmeTargetSet       persistentTargetSet;
    MmeTargetSet       mainTargetSet;
    MmeSnapshotCache   snapshotCache;
    unsigned long      snapshotError;

    // ctx+0x3c / ctx+0x42: the per-target clear-color/depth maps (the SAS
    // render-target tables; keyed by surface). Phase 2 stages the host clear
    // color here; the SAS target machinery (Phase 3) consumes them.
    D3DCOLOR postClearColor;
    std::map<IDirect3DSurface9*, D3DCOLOR> clearColorMap;
    std::map<IDirect3DSurface9*, float> clearDepthMap;

    // ctx+0x98 vector (FUN_180060140 push target): background/draw-type-0
    // snapshot records consumed by the post-effect runner (Phase 2 sas_exec).
    std::vector<RenderSnapshot> pendingSnapshots;

    // [offscreen DefaultEffect consumption, sub_180011960 record +0x78] the
    // parsed rows of the offscreen render target a suspended scene technique
    // currently renders into. The pass planner stages the pointer at the
    // scene step / repeat-boundary record apply (the corridors that bind the
    // offscreen surface at RT0) and clears it at the resume; while staged,
    // the binding resolution draws models WITHOUT an assigned effect through
    // the row-mapped effect (none/hide/main_default/<absolute path>). Null
    // while the main targets are current. The vector lives in the engine
    // assignment's SasEffect, so the borrowed pointer stays valid for the pass.
    const std::vector<std::pair<std::string, std::string>>* offscreenDefaultEffect;
    // [FUN_18005c970 / FUN_18005cac0 `*a1` - the renderPassList wrapper's
    // inner record] the 0x98 OFFSCREEN record of the OFFSCREENRENDERTARGET
    // parameter the CURRENT turn's wrapper names. The original holds the
    // pointer directly at wrapper+0 from queue-build time on (sub_18002CA80
    // 0x18002d415, refreshed only when the effect (re)loaded - the `if (v97)
    // { if (!v135) skip }` dirty gate at 0x18002d33e) and it is sticky
    // across the run-state lifecycle BY CONSTRUCTION: FUN_18005d130 calls
    // FUN_18005c510 (whose per-model FUN_1800599a0 reset destroys every
    // ModelData's +0x358 run state) BEFORE FUN_18005c970 reads wrapper+0,
    // so the association never comes from a live staged state at the apply
    // point. The port carries the association in the queue item itself
    // (MmeRenderPassItem::offscreen, set at plan build) and mirrors the
    // CURRENT turn's item here for the drivers that read it through the
    // context (FUN_18005e210's pass-record branch, FUN_18005da50's record
    // executor). Null at the base turn (the original's null ctx+0x168).
    ModelData*   currentBindingObject;
    SasResource* currentBindingOffscreen;
    unsigned int currentBindingTurnId = 0;

    // ctx+0x190 map (FUN_1800601e0): the FUN_18005d5c0 background-quad fixup
    // cache - source stream vertex buffer -> rescaled copy. Owning references;
    // released with the context (device loss releases them first via
    // ReleaseBackgroundFixedVbs from OnLostDevice). FUN_18005b9e0's
    // viewport-change branch destroys the whole map (the ctx+0x198 sentinel /
    // ctx+0x1A0 size live inside this map object).
    std::map<IDirect3DVertexBuffer9*, IDirect3DVertexBuffer9*> backgroundFixedVbs;

    // ctx+0xf8..0x108 vector of all registered ModelData pointers
    // (MME_RegisterModelData push_back / MME_UnregisterModelData erase).
    std::vector<ModelData*> models;
    ModelNameRegistry<ModelData> modelsByName; // rebuilt before effect updates

    // ctx+0x118 id -> ModelData* hash map (MME_FindOrCreateModelEntry
    // [0x180060ca0]; Phase 1 uses std::unordered_map).
    std::unordered_map<unsigned long long, ModelData*> modelRegistry;

    // [R3/R8 2026-09; the walk host-self plumbing of sub_18005A410 /
    // sub_18005A5C0 / sub_18005A2C0] the scene carrier whose technique walk is
    // CURRENTLY executing. The original threads the carrier's ModelData self
    // through the walk's host slots - FUN_18005a740 / 0x18005A9C0 take the
    // carrier as a1 (self = ModelData+0x08) and hand a1-8 (the ModelData) to
    // sub_18001B5B0 as the parameter-bind host, and the SAS Clear dispatcher
    // sub_18005AEE0 gates its staged registration on the same carrier's
    // ModelData+0x364 (the port's renderClass) == 2. The port publishes the
    // carrier here instead, set around SasExecuteTechniqueStep /
    // SasResumeTechnique by the three scene drivers (MmeStepSceneRecord /
    // MmeFinishSceneRecord / MmeFullRunSceneRecord) and null again right after
    // the walk returns; the host slots (SasHostRunPass / the quad pass) and
    // MmeStageSceneClearRuntime read it. Null outside those walks: object
    // -class effects keep their per-draw binding path (MmeApplyModelRenderSnapshot)
    // untouched, exactly the original's split.
    ModelData* sceneWalkCarrier;

    // ctx+0x1b8/+0x1e0/+0x218 pass-plan containers (Phase 2 pass_planner);
    // ctx+0x148/0x150 pass list read by HandleDrawIndexedPrimitive/
    // FUN_18005d130 for the repeat count.
    std::vector<ModelData*> passPlanA;        // ctx+0xb8..0xc8 (renderClass 1)
    std::vector<ModelData*> passPlanB;        // ctx+0xd8..0xe8 (renderClass 2)
    std::vector<MmeRenderPassItem> renderPassList;   // ctx+0x148..0x150 - one
                                                     // item per queued 0x2E
                                                     // resource (the
                                                     // materialized wrappers,
                                                     // FUN_18005fd30 copy of
                                                     // mgr+0x158)

    // ctx+0x170..0x188: the last viewport seen by the plan rebuild
    // (FUN_18005b9e0 L449-484 compares GetViewport against this). The ctor
    // zeroes it like the original's, so the first rebuild always takes the
    // viewport-changed branch.
    D3DVIEWPORT9 lastPlanViewport;

    // ctx+0x18A (byte 394): the background-quad fixup dirty flag. The ctor's
    // WORD write at ctx+0x189 inits it 1; FUN_18005b9e0 sets it on a
    // viewport change; FUN_18005d5c0 rebuilds (instead of binding) a cached
    // fixup vertex buffer while it is set and clears it after the first
    // fixup attempt (0x18005d762 / 0x18005d9a7).
    unsigned char backgroundQuadViewportDirty;

    // Animated-texture registry; independent of the original ctx+0x240
    // snapshot manager. The registry is ticked from OnEndScene.
    void* animatedTextures;

    // ctx+0x440 third device reference (AddRef'd [ctor L66-69]).
    IDirect3DDevice9* thirdDeviceRef;

    // Effect-owner manager pointer. In the original this is a separate global
    // (DAT_1800d9a40); the Phase 1 plan surfaces it here as well.
    EffectOwnerManager* ownerManager;

    // [0x180055780] FUN_180055780: release every pooled binding-context state
    // block and reset the round-robin index (device loss / context dtor).
    // OnLostDevice calls this pool-only step directly (FUN_18005e640 walk
    // step 3, after the ctx+0x18 cached target set and the ctx+0x240 snapshot
    // cache, before the run-state walk).
    void ReleaseStateBlockPool();

    // Release the background-quad fixup cache (ctx+0x190 map): every rescaled
    // vertex-buffer copy. The original frees this map in the context dtor
    // (FUN_18005b2f0, 0x18005b388-0x18005b3ed) and on FUN_18005b9e0's
    // viewport-change branch - the device-lost walk (FUN_18005e640) never
    // touches it; the port drops it on device loss as well so a Reset cannot
    // leave dangling POOL_DEFAULT buffer pointers in the cache.
    void ReleaseBackgroundFixedVbs();

    // [0x180055780] FUN_180055780: release every pooled binding-context state
    // block and reset the round-robin index (device loss / context dtor).
    void ReleaseBindingContextPool();

    // EndScene snapshot maintenance: release both saved sets, then in edit
    // mode prune cache entries absent from the active surface identity set.
    void FinishSnapshotFrame(bool editMode);

    // Release the main-snapshot cache (the ctx+0x240 manager's surfaces are
    // D3DPOOL_DEFAULT render targets: they must go before a device reset) and
    // the saved main target set's references. Equivalent to sub_180001660:
    // the persistent set (mgr+0x08) + the saved main set (mgr+0x50) + both
    // cache families. Called from the context dtor, the device-loss path
    // (ReleaseBindingContextPool callers) and the 180001880 failure path.
    void ReleaseMainSnapshotCache();
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

// --- [0x18005E4F0 / 0x18005E570 / 0x18005E5D0] the staged-clear map writers ---
// The two register helpers key the CURRENT device targets (render targets
// 0..3 / the depth stencil surface, GetRenderTarget/GetDepthStencilSurface
// then immediate Release of the probe ref, exactly the original) into
// ctx->clearColorMap / ctx->clearDepthMap (operator[] semantics: an existing
// entry is OVERWRITTEN). The erase helper removes the four current render
// targets from the COLOR map only - the original's sub_18005E5D0 never touches
// the depth map.
void MmeStageClearColor(MmeContext* ctx, D3DCOLOR color);
void MmeStageClearDepth(MmeContext* ctx, float clearDepth);
void MmeEraseStagedClearColor(MmeContext* ctx);

// [sub_18005AEE0 0x18005af06-0x18005af2b] the SAS Clear command's host
// dispatcher: when the CURRENTLY-WALKING carrier (ctx->sceneWalkCarrier) is
// scene class (ModelData+0x364 == 2, the port's renderClass() == 2), register
// the RUNTIME clear values into the per-target maps BEFORE the device Clear -
// flags & D3DCLEAR_TARGET -> the staged color, flags & D3DCLEAR_ZBUFFER -> the
// staged depth (the values the caller selected per flag, i.e. the run state's
// ClearSetColor/ClearSetDepth overrides or their creation defaults). A
// non-scene carrier (class 0/1) or no active walk stages nothing; the
// annotation-value staging of the repeat-boundary record apply
// (MmeApplyPassRecord, FUN_18005c970) is a SEPARATE writer and stays as is.
void MmeStageSceneClearRuntime(unsigned long flags, unsigned long color,
                               float depth);

} // namespace mme
