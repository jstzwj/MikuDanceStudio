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
#include "mme_log.h"
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
    , cachedDepthStencil(nullptr)         // ctx+0x40 [ctor L16] = 0
    , hasRenderTargetsFlag(0)             // ctx+0x188 [ctor] = 0
    , postClearEnabled(0)                 // ctx+0x438
    , allObjectsSpecialFlag(0)            // ctx+0x439
    , postEffectRanFlag(0)                // ctx+0x189
    , lastPostEffectHr(0)                 // reconstruction addition
    , postClearColor(0)                   // ctx+0x3c staged clear color
    , currentBindingObject(nullptr)
    , animatedTextures(nullptr)
    , thirdDeviceRef(nullptr)
    , ownerManager(nullptr)
    , lastPlanViewportValid(false)        // ctx+0x18a [ctor L78 word = 0x100 marker; 0 = unset]
{
    // [ctor L10-14] first device reference (AddRef).
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
        cachedRenderTargets[i] = nullptr;      // ctx+0x20..0x38 [ctor L14-15/L23-36]
    }
    memset(&lastPlanViewport, 0, sizeof(lastPlanViewport));
    // [ctor L65] ctx+0x240 animated-texture collection (FUN_180001000) -
    // Phase 4: MmeAnimeCreate builds the GDI+ backed registry.
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
    passTrie.clear();

    // [L3376-3392] pass-list teardown releases per-entry COM objects: the
    // cached render-target/depth surfaces and the binding-context pool
    // (FUN_180055780 releases every pooled IDirect3DStateBlock9).
    ReleaseBindingContextPool();
    for (int i = 0; i < 4; ++i) {
        if (cachedRenderTargets[i] != nullptr) {
            cachedRenderTargets[i]->Release();
            cachedRenderTargets[i] = nullptr;
        }
    }
    if (cachedDepthStencil != nullptr) {
        cachedDepthStencil->Release();
        cachedDepthStencil = nullptr;
    }

    // [L3393-3405] release the tail COM pointers (Phase 2 binding objects)
    // and the ctx+0x440 device reference.
    if (thirdDeviceRef != nullptr) {
        thirdDeviceRef->Release();
        thirdDeviceRef = nullptr;
    }
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

void MmeContext::ReleaseBindingContextPool()
{
    // [0x180055780] FUN_180055780: release every pooled state block and reset
    // the round-robin index.
    for (size_t i = 0; i < bindingContextPool.size(); ++i) {
        if (bindingContextPool[i] != nullptr) {
            bindingContextPool[i]->Release();
            bindingContextPool[i] = nullptr;
        }
    }
    bindingContextPool.clear();
    bindingContextIndex = 0;
    postBindingContext = nullptr;
    // The FUN_18005d5c0 background-quad fixup cache holds device-dependent
    // vertex buffers; the original releases the ctx+0x190 map entries in the
    // device-lost walk (FUN_18005e640) and the context dtor.
    for (std::map<IDirect3DVertexBuffer9*, IDirect3DVertexBuffer9*>::iterator it =
             backgroundFixedVbs.begin();
         it != backgroundFixedVbs.end(); ++it) {
        if (it->second != nullptr) {
            it->second->Release();
        }
    }
    backgroundFixedVbs.clear();
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

    // [L21] erase from the manager list.
    if (g_ownerManager != nullptr) {
        g_ownerManager->UnregisterObject(model);
    }

    // [L22] erase the hash entry.
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

} // namespace mme
