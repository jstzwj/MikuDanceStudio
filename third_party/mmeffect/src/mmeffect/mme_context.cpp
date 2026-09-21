// mme_context.cpp - see mme_context.h
#include "mme_context.h"

#include <algorithm>
#include <cstring>

#include "MMDExport.h"   // ExpGetRenderRepeatCount / ExpSetRenderRepeatCount
#include "mmhack_api.h"

#include "material_bind.h"
#include "emm_manager.h"
#include "model_data.h"
#include "mme_globals.h"
#include "pass_planner.h"   // MmeReleaseTargetSet (ReleaseMainSnapshotCache)
#include "anime_texture.h"   // [Phase 4] MmeAnimeCreate/MmeAnimeDestroy

namespace mme {

EffectOwnerManager::EffectOwnerManager()
    : planDirty(false)
{
    // [0x18002a220] the original ctor builds the +0xa0 std::map and object
    // list; the std containers initialize themselves.
}

EffectOwnerManager::~EffectOwnerManager()
{
    // [0x180058740] the original does not own the ModelData objects
    // (the context destructor frees them); it releases the binding objects.
    std::map<BindingKey, MaterialBinding*>::iterator it = bindings.begin();
    while (it != bindings.end()) {
        delete it->second;
        ++it;
    }
    bindings.clear();
}

void EffectOwnerManager::RegisterObject(ModelData* model)
{
    // [0x18005b7a0 L56-60] list insert + dirty flag.
    registeredObjects.push_back(model);
    planDirty = true;   // manager+0x90 = 1
}

void EffectOwnerManager::UnregisterObject(ModelData* model)
{
    // [0x18005b910 L21] FUN_18002a430 removes the matching list entry.
    registeredObjects.remove(model);
}

MmeContext::MmeContext(IDirect3DDevice9* device)
    : device(nullptr)
    , lastDrawnModel(nullptr)
    , bindingReadyFlag(0)
    , errorReportedFlag(0)
    , backgroundDrawnFlag(0)
    , effectEnabled(1)                    // [ctor L77] ctx+0x13 = 1
    , lastRepeatCount(0)                  // [ctor L33] ctx+0x14 = 0
    , bindingContextDevice(nullptr)
    , bindingContextIndex(0)              // ctx+0x70 (FUN_180055690 round-robin)
    , postBindingContext(nullptr)         // ctx+0x60 [ctor L16] = 0
                                      // cachedTargetSet (ctx+0x18) default-
                                      // constructs with mask 0 / null slots
    , snapshotSuccessFlag(0)              // ctx+0x188 (the ctor leaves the
                                          // byte unwritten; da50's branch
                                          // head clear precedes every read)
    , postEffectRanFlag(0)                // ctx+0x189 [ctor WORD 0x100 -> 0]
    , persistentClearUsed(0)              // ctx+0x438 = mgr+0x1F8
    , persistentLayerEligible(0)          // ctx+0x439 = mgr+0x1F9
    , allObjectsSpecialFlag(0)            // ctx+0x43A = mgr+0x1FA
    , lastPostEffectHr(0)                 // reconstruction addition
    , postClearColor(0)                   // ctx+0x3c staged clear color
    , currentBindingObject(nullptr)
    , currentBindingOffscreen(nullptr)
    , sceneWalkCarrier(nullptr)          // [R3/R8] no active scene walk
    , animatedTextures(nullptr)
    , thirdDeviceRef(nullptr)
    , ownerManager(nullptr)
    , offscreenDefaultEffect(nullptr)    // offscreen DefaultEffect staging
    , backgroundQuadViewportDirty(1)          // ctx+0x18A [ctor WORD 0x100 at ctx+0x189 -> 1]
    , snapshotDepth(nullptr)              // ctx+0x240 mgr cache (Phase 3)
    , snapshotFamilySpecial(false)        // mgr+0x1FA mirror
    , snapshotError(0)                    // mgr+0x1FC mirror
    // persistentTargetSet (mgr+0x08..0x38) default-constructs empty (mask 0,
    // null slots) like the original's manager ctor; it is filled by
    // MmeUpdatePassBookkeeping's ==N branch and dropped at the repeat-0
    // boundary / device loss / dtor.
{    // [ctor L10-14] first device reference (AddRef).
    this->device = device;
    if (device != nullptr) {
        device->AddRef();
    }
    // [ctor L17-22] second device reference at ctx+0x68 (AddRef).
    bindingContextDevice = device;
    if (device != nullptr) {
        device->AddRef();
    }
    // [ctor L66-69] third device reference at ctx+0x440 (AddRef).
    thirdDeviceRef = device;
    if (device != nullptr) {
        device->AddRef();
    }
    for (int i = 0; i < 4; ++i) {
        snapshotSurfaces[i] = nullptr;         // ctx+0x240 mgr cache [ctor]
    }
    memset(&lastPlanViewport, 0, sizeof(lastPlanViewport));
    // The original's ctx+0x240 is the turn-boundary snapshot manager (its
    // ctor FUN_180001000 builds the two saved target sets and the snapshot
    // cache families - modeled by mainTargetSet/snapshotSurfaces above).
    // The animated-texture registry is a PORT ADDITION (Phase 4,
    // MmeAnimeCreate; see the header note).
    animatedTextures = MmeAnimeCreate();
    // [ctor L46] ctx+0x190 / [ctor L70] ctx+0x448 containers - Phase 2 seams.
}

MmeContext::~MmeContext()
{
    // [0x18005b2f0] FUN_18005b2f0: destroy every remaining ModelData
    // (MME_ModelData_Destructor + free) and tear down the containers.
    for (size_t i = 0; i < models.size(); ++i) {
        delete models[i];
    }
    models.clear();
    modelRegistry.clear();          // [L3375 FUN_180060ec0]
    pendingSnapshots.clear();
    passPlanA.clear();              // [L3370-3374 vector clears]
    passPlanB.clear();
    renderPassList.clear();
    // Offscreen DefaultEffect staging: the borrowed row vector dies with the
    // engine cache; the transient bindings are owned here.
    offscreenDefaultEffect = nullptr;


    // [L3376-3392] pass-list teardown releases per-entry COM objects: the
    // cached render-target/depth surfaces and the binding-context pool
    // (FUN_180055780 releases every pooled IDirect3DStateBlock9).
    ReleaseBindingContextPool();
    // The ctx+0x18 cached target set's borrowed references.
    for (int i = 0; i < 4; ++i) {
        if (cachedTargetSet.targets[i] != nullptr) {
            cachedTargetSet.targets[i]->Release();
            cachedTargetSet.targets[i] = nullptr;
        }
    }
    if (cachedTargetSet.depth != nullptr) {
        cachedTargetSet.depth->Release();
        cachedTargetSet.depth = nullptr;
    }
    cachedTargetSet.mask = 0;

    // [L3393-3405] release the tail COM pointers (Phase 2 binding objects)
    // and the ctx+0x440 device reference.
    if (thirdDeviceRef != nullptr) {
        thirdDeviceRef->Release();
        thirdDeviceRef = nullptr;
    }
    // (The ctx+0x240 manager's snapshot cache - persistent set, surfaces and
    // the saved main target set - was already released inside
    // ReleaseBindingContextPool above, whose internal
    // ReleaseMainSnapshotCache also serves the device-reset path.)
    // [L3406] FUN_1800011d0(ctx+0x240): animated-texture teardown
    // (Phase 4: MmeAnimeDestroy releases the GDI+ images and D3D textures).
    if (animatedTextures != nullptr) {
        MmeAnimeDestroy(static_cast<MmeAnimatedTextureSet*>(animatedTextures));
        animatedTextures = nullptr;
    }

    // The ctx+0x190 background-fixup cache (device-dependent vertex buffers).
    for (std::map<IDirect3DVertexBuffer9*, IDirect3DVertexBuffer9*>::iterator it =
             backgroundFixedVbs.begin();
         it != backgroundFixedVbs.end(); ++it) {
        if (it->second != nullptr) {
            it->second->Release();
        }
    }
    backgroundFixedVbs.clear();

    // The remaining destructor body releases the ctx+0x68 and ctx+0x00
    // device references.
    if (bindingContextDevice != nullptr) {
        bindingContextDevice->Release();
        bindingContextDevice = nullptr;
    }
    if (device != nullptr) {
        device->Release();
        device = nullptr;
    }
}

void MmeContext::ReleaseStateBlockPool()
{
    // [0x180055780] FUN_180055780: release every pooled state block and reset
    // the round-robin index (a1+8 = the ctx+0x70 index, a1+0x10..0x18 =
    // the ctx+0x78 pool vector; the postBindingContext clear is the port's
    // borrowed-reference guard).
    for (size_t i = 0; i < bindingContextPool.size(); ++i) {
        if (bindingContextPool[i] != nullptr) {
            bindingContextPool[i]->Release();
            bindingContextPool[i] = nullptr;
        }
    }
    bindingContextPool.clear();
    bindingContextIndex = 0;
    postBindingContext = nullptr;
}

void MmeContext::ReleaseBackgroundFixedVbs()
{
    // The FUN_18005d5c0 background-quad fixup cache holds device-dependent
    // vertex buffers. (The original releases the ctx+0x190 map entries only
    // in the context dtor and the viewport-change branch - the device-lost
    // walk never touches it; see the header note.)
    for (std::map<IDirect3DVertexBuffer9*, IDirect3DVertexBuffer9*>::iterator it =
             backgroundFixedVbs.begin();
         it != backgroundFixedVbs.end(); ++it) {
        if (it->second != nullptr) {
            it->second->Release();
        }
    }
    backgroundFixedVbs.clear();
}

void MmeContext::ReleaseBindingContextPool()
{
    // [0x180055780] FUN_180055780: release every pooled state block and reset
    // the round-robin index.
    ReleaseStateBlockPool();
    // The ctx+0x240 manager's snapshot cache holds POOL_DEFAULT surfaces -
    // they must be released before the reset just like the state blocks.
    ReleaseMainSnapshotCache();
    // The FUN_18005d5c0 background-quad fixup cache holds device-dependent
    // vertex buffers (port addition to the original's release set - see
    // ReleaseBackgroundFixedVbs).
    ReleaseBackgroundFixedVbs();
}

void MmeContext::ReleaseSnapshotSurfaceCache()
{
    // [sub_180001880's prologue, sub_180002C60/sub_180003170] the inactive
    // family's cached surfaces only - neither the mgr+0x08 persistent set
    // nor the mgr+0x50 saved main set is touched here (the original's
    // prologue never drops them; the saved set is refetched at step 2
    // anyway, so the historical mainTargetSet release in this path was a
    // harmless idempotent extra and is now omitted for 1:1).
    for (int i = 0; i < 4; ++i) {
        if (snapshotSurfaces[i] != nullptr) {
            snapshotSurfaces[i]->Release();
            snapshotSurfaces[i] = nullptr;
        }
    }
    if (snapshotDepth != nullptr) {
        snapshotDepth->Release();
        snapshotDepth = nullptr;
    }
}

void MmeContext::ReleaseMainSnapshotCache()
{
    // [sub_180001660] the persistent target set (mgr+0x08 -
    // sub_180001660's leading sub_180067680(a1+1)), the snapshot surfaces
    // (mgr+0x98 family, sub_180002100/sub_180002440 cache owners) and the
    // saved main target set's (mgr+0x50) borrowed references.
    MmeReleaseTargetSet(persistentTargetSet);
    ReleaseSnapshotSurfaceCache();
    for (int i = 0; i < 4; ++i) {
        if (mainTargetSet.targets[i] != nullptr) {
            mainTargetSet.targets[i]->Release();
            mainTargetSet.targets[i] = nullptr;
        }
    }
    if (mainTargetSet.depth != nullptr) {
        mainTargetSet.depth->Release();
        mainTargetSet.depth = nullptr;
    }
    mainTargetSet.mask = 0;
}

MmeContext* MmeGetContext()
{
    return g_context;
}

ModelData* MmeFindOrCreateModelEntry(unsigned long long id)
{
    // [0x180060ca0] hash lookup; a miss inserts a node whose value is null.
    // The original throws std::bad_alloc on allocation failure - the MSVC
    // unordered_map operator[] does the same via operator new.
    MmeContext* ctx = g_context;
    if (ctx == nullptr) {
        return nullptr;
    }
    return ctx->modelRegistry[id];
}

void MmeRegisterModelData(unsigned long long objectId, const char* filename, int kind,
                          unsigned int materialCount, IUnknown* reservedObject)
{
    // [0x18005b7a0] MME_RegisterModelData.
    MmeContext* ctx = g_context;
    if (ctx == nullptr) {
        return; // divergence guard: the original would deref null
    }

    // [L26-33] operator new(0x370) + MME_ModelData_Constructor
    // (device arg = *ctx = ctx+0x00 device).
    ModelData* model = new ModelData(ctx->device, objectId, filename, kind,
                                     materialCount, reservedObject);

    // [L34-53] push_back into the ctx+0xf8..0x108 vector.
    ctx->models.push_back(model);

    // [L54-55] MME_FindOrCreateModelEntry + node value = model.
    ctx->modelRegistry[objectId] = model;

    // [L56-60] manager list insert + dirty flag.
    if (g_ownerManager != nullptr) {
        g_ownerManager->RegisterObject(model);
    }

    // Phase 2: auto-assign the effect file (same-name .fx beside the model,
    // the "[<fx>]" embedded-name convention, then the EMM default row;
    // MMEffect.txt lines 20-66). The discovery is silent - the "AutoLoading: "
    // log belongs to the EMM autoload (FUN_1800570D0, pass planner), so the
    // Phase 1 "AutoLoading: " log here was a divergence and is removed. The
    // assignment loads the effect through the engine ("Loading effect file: ").
    std::string effectFile = MmeFindEffectFileForModel(model);
    if (!effectFile.empty()) {
        MmeAssignEffect(objectId, -1, effectFile);
    }
}

void MmeUnregisterModelData(unsigned long long objectId)
{
    // [0x18005b910] MME_UnregisterModelData.
    MmeContext* ctx = g_context;
    if (ctx == nullptr) {
        return;
    }

    // [L19-20] FindOrCreateModelEntry + node value.
    std::unordered_map<unsigned long long, ModelData*>::iterator it =
        ctx->modelRegistry.find(objectId);
    if (it == ctx->modelRegistry.end()) {
        // The original would create an empty entry and erase it again -
        // observably a no-op.
        return;
    }
    ModelData* model = it->second;

    // Object IDs may be reused after unload; discard target-local overrides.
    if (g_ownerManager) {
        for (const auto& entry : g_ownerManager->bindings) {
            MaterialBinding* binding = entry.second;
            if (!binding || !binding->sas) continue;
            for (auto& resource : binding->sas->resources) {
                for (auto row = resource.effectOverrides.begin(); row != resource.effectOverrides.end();)
                    if (row->first.first == objectId) row = resource.effectOverrides.erase(row);
                    else ++row;
                for (auto row = resource.shownOverrides.begin(); row != resource.shownOverrides.end();)
                    if (row->first.first == objectId) row = resource.shownOverrides.erase(row);
                    else ++row;
            }
        }
    }

    // Scene loads delete accessories in batches between render callbacks. A
    // plan built at BeginScene may therefore still contain this pointer until
    // the next frame. Remove every transient reference before destroying the
    // ModelData, otherwise OnEndScene/post processing dereferences freed
    // memory (the observed ntdll access violations while replacing a scene).
    if (model != nullptr && model->runState() != nullptr) {
        SasRestoreRunState(ctx->device, model->runState());
    }
    ctx->passPlanA.erase(std::remove(ctx->passPlanA.begin(), ctx->passPlanA.end(), model),
                         ctx->passPlanA.end());
    ctx->passPlanB.erase(std::remove(ctx->passPlanB.begin(), ctx->passPlanB.end(), model),
                         ctx->passPlanB.end());
    ctx->renderPassList.erase(
        std::remove_if(ctx->renderPassList.begin(), ctx->renderPassList.end(),
                       [model](const MmeRenderPassItem& item) {
                           return item.carrier == model;
                       }),
        ctx->renderPassList.end());
    if (ctx->currentBindingObject == model) {
        ctx->currentBindingObject = nullptr;
        ctx->currentBindingOffscreen = nullptr;
    }
    if (ctx->lastDrawnModel == model) {
        ctx->lastDrawnModel = nullptr;
    }

    // [L21] erase from the manager list.
    if (g_ownerManager != nullptr) {
        g_ownerManager->UnregisterObject(model);
        g_ownerManager->orderedPlan.erase(
            std::remove_if(g_ownerManager->orderedPlan.begin(),
                           g_ownerManager->orderedPlan.end(),
                           [model](const MmeRenderPassItem& item) {
                               return item.carrier == model;
                           }),
            g_ownerManager->orderedPlan.end());
        g_ownerManager->planDirty = true;
    }

    // [L22] erase the hash entry.
    ctx->modelsByName.Remove(model);
    ctx->modelRegistry.erase(it);

    // [L23-36] remove the pointer from the ctx+0xf8 vector.
    std::vector<ModelData*>::iterator vit =
        std::find(ctx->models.begin(), ctx->models.end(), model);
    if (vit != ctx->models.end()) {
        ctx->models.erase(vit);
    }

    // [L37-40] MME_ModelData_Destructor + free; the effect-owner bindings of
    // the model go with it (FUN_180058740 walks the manager map on cleanup;
    // per-model erase keeps the map consistent between deletes).
    if (model != nullptr) {
        MmeDropMaterialBindings(model);
        delete model;
    }
}

// MmeRebuildRenderPassPlan [0x18005b9e0] is implemented in pass_planner.cpp.

// ---------------------------------------------------------------------------
// [0x18005E4F0 / 0x18005E570 / 0x18005E5D0] the staged-clear map writers
// (moved here from pass_planner.cpp so the SAS Clear command's runtime
// registration - MmeStageSceneClearRuntime below - and the repeat-boundary
// record apply share the exact original implementations).
// ---------------------------------------------------------------------------

// [sub_18005E4F0] stage `color` into ctx+0x1E0 (clearColorMap) keyed by each
// of the four CURRENT render targets: GetRenderTarget(i) (device vtable slot
// 38, +0x130), immediate Release of the probe reference, then the hash-map
// operator[] insert-or-overwrite (sub_180061560) with the D3DCOLOR at node+8.
void MmeStageClearColor(MmeContext* ctx, D3DCOLOR color)
{
    IDirect3DDevice9* device = (ctx != nullptr) ? ctx->device : nullptr;
    if (device == nullptr) {
        return;
    }
    for (UINT i = 0; i < 4; ++i) {
        IDirect3DSurface9* target = nullptr;
        if (SUCCEEDED(device->GetRenderTarget(i, &target)) && target != nullptr) {
            ctx->clearColorMap[target] = color;
            target->Release();
        }
    }
}

// [sub_18005E570] stage `clearDepth` into ctx+0x210 (clearDepthMap) keyed by
// the CURRENT depth stencil surface (GetDepthStencilSurface, slot +0x140,
// Release, operator[] with the float at node+8).
void MmeStageClearDepth(MmeContext* ctx, float clearDepth)
{
    IDirect3DDevice9* device = (ctx != nullptr) ? ctx->device : nullptr;
    if (device == nullptr) {
        return;
    }
    IDirect3DSurface9* depth = nullptr;
    if (SUCCEEDED(device->GetDepthStencilSurface(&depth)) && depth != nullptr) {
        ctx->clearDepthMap[depth] = clearDepth;
        depth->Release();
    }
}

// [sub_18005E5D0] ERASE the four current render targets from the COLOR map
// only (sub_180061310 = the custom hash-map erase): the original's buffer/
// geometry pass prologue for scene-class carriers (0x18005a781 /
// 0x18005aa2f). The depth map is never erased here - once the effect's own
// pass has drawn onto the targets the staged color "needs clear" decision is
// invalid, but the staged depth survives for the snapshot validation.
void MmeEraseStagedClearColor(MmeContext* ctx)
{
    IDirect3DDevice9* device = (ctx != nullptr) ? ctx->device : nullptr;
    if (device == nullptr) {
        return;
    }
    for (UINT i = 0; i < 4; ++i) {
        IDirect3DSurface9* target = nullptr;
        if (SUCCEEDED(device->GetRenderTarget(i, &target)) && target != nullptr) {
            ctx->clearColorMap.erase(target);
            target->Release();
        }
    }
}

// [sub_18005AEE0 0x18005af06-0x18005af2b] the SAS Clear command's host-side
// runtime registration. The original's clear dispatcher receives the walking
// carrier's ModelData+0x08 self as a1 and gates BOTH registrations on
// `*(a1 + 860) == 2` (= ModelData+0x364, the port's renderClass() == 2: the
// scene/postprocess class): flags & 1 -> sub_18005E4F0(host, color),
// flags & 2 -> sub_18005E570(host, depth), BEFORE the device->Clear
// (vtable+344). Preprocess (class 1) and object (class 0) carriers stage
// nothing here - their only map writer stays the repeat-boundary annotation
// apply (FUN_18005c970 / MmeApplyPassRecord).
void MmeStageSceneClearRuntime(unsigned long flags, unsigned long color,
                               float depth)
{
    MmeContext* ctx = g_context;
    ModelData* carrier = (ctx != nullptr) ? ctx->sceneWalkCarrier : nullptr;
    if (carrier == nullptr || carrier->renderClass() != 2) {
        return;
    }
    if ((flags & D3DCLEAR_TARGET) != 0) {
        MmeStageClearColor(ctx, static_cast<D3DCOLOR>(color));
    }
    if ((flags & D3DCLEAR_ZBUFFER) != 0) {
        MmeStageClearDepth(ctx, depth);
    }
}

} // namespace mme
