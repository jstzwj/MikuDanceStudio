// pass_planner.cpp - see pass_planner.h
#include "pass_planner.h"

#include <cstdio>
#include <cstring>
#include <map>

#include "MMDExport.h"   // ExpGetPmd*/ExpGetAcs*/ExpGet/SetRenderRepeatCount
#include "mmhack_api.h"  // GetClearColor / LoadedPMMFile

#include "effect_engine.h"   // MmeDxErrDescription + MmeEngineForEachSas
#include "emm_manager.h"
#include "material_bind.h"
#include "mme_context.h"
#include "mme_globals.h"
#include "mme_log.h"
#include "mme_util.h"
#include "model_data.h"
#include "sas_interpreter.h"   // [PHASE3 wiring] SasExecutePostEffect + host callbacks
#include "sas_exec.h"          // [PHASE3 wiring] SasEffect full definition

namespace mme {

namespace {

// [PHASE3 wiring] the fullscreen-quad pass draw for post-effect techniques
// (RunPass kind 1). The original draws a screen-covering quad with the
// effect's current pass (BeginPass/draw/EndPass); the vertex layout is
// XYZRHW + TEX1, the standard MME full.fx post-effect convention.
// UNCERTAIN(0x18001bf80 slot1): the exact original vertex data is not in the
// decompile; XYZRHW+UV is the convention every published MME post fx relies on.
struct SasQuadVertex {
    float x, y, z, rhw;
    float u, v;
};

HRESULT SasDefaultDrawBufferPass(ID3DXEffect* effect, IDirect3DDevice9* device, int passIndex)
{
    if (device == nullptr || effect == nullptr) {
        return E_POINTER;
    }
    D3DVIEWPORT9 vp;
    memset(&vp, 0, sizeof(vp));
    device->GetViewport(&vp);
    const float w = static_cast<float>(vp.Width);
    const float h = static_cast<float>(vp.Height);
    const float halfTexelX = 0.0f, halfTexelY = 0.0f;
    SasQuadVertex quad[4] = {
        { -0.5f + halfTexelX, -0.5f + halfTexelY, 0.0f, 1.0f, 0.0f, 0.0f },
        { -0.5f + halfTexelX, h - 0.5f + halfTexelY, 0.0f, 1.0f, 0.0f, 1.0f },
        { w - 0.5f + halfTexelX, -0.5f + halfTexelY, 0.0f, 1.0f, 1.0f, 0.0f },
        { w - 0.5f + halfTexelX, h - 0.5f + halfTexelY, 0.0f, 1.0f, 1.0f, 1.0f },
    };
    // [FUN_18001bf80 slot 1] BeginPass/draw/EndPass on the effect object
    // (SetTechnique was already issued by the script runner).
    HRESULT hr = effect->BeginPass(static_cast<UINT>(passIndex));
    if (FAILED(hr)) {
        return hr;
    }
    hr = device->DrawPrimitiveUP(D3DPT_TRIANGLESTRIP, 2, quad, sizeof(SasQuadVertex));
    effect->EndPass();
    return hr;
}

long SasHostRunPass(void* ctx, SasEffect* sas, int kind, int passIndex)
{
    IDirect3DDevice9* device = static_cast<IDirect3DDevice9*>(ctx);
    if (device == nullptr) {
        return E_POINTER;
    }
    if (kind == 1) {
        ID3DXEffect* effect = (sas != nullptr) ? sas->effect : nullptr;
        HRESULT hr = SasDefaultDrawBufferPass(effect, device, passIndex);
        return SUCCEEDED(hr) ? 0 : hr;
    }
    // kind 0 (standard-shader object draw) is driven by the host repeat
    // mechanism (ExpSetRenderRepeatCount); inside a post-effect script it is
    // a no-op, matching MME's screen-space post chain.
    return 0;
}

long SasHostScriptExternalColor(void* /*ctx*/, SasEffect* /*sas*/)
{
    // [Tips (2) ScriptExternal=Color] the scene re-render into the bound
    // target is owned by the host repeat mechanism; per repeat iteration the
    // planner re-runs the object passes before this chain executes.
    return 0;
}

unsigned long SasHostGetClearColor(void* /*ctx*/)
{
    return static_cast<unsigned long>(GetClearColor());
}

float SasHostGetClearDepth(void* /*ctx*/)
{
    return 1.0f;
}

// One-time host-callback registration (thread-safe enough for MMD's single
// render thread; guarded by a simple flag).
void SasEnsureHostCallbacksInstalled()
{
    static bool installed = false;
    if (installed) {
        return;
    }
    SasHostCallbacks callbacks;
    callbacks.ctx = nullptr;
    callbacks.RunPass = &SasHostRunPass;
    callbacks.ScriptExternalColor = &SasHostScriptExternalColor;
    callbacks.GetClearColor = &SasHostGetClearColor;
    callbacks.GetClearDepth = &SasHostGetClearDepth;
    SasSetHostCallbacks(callbacks);
    installed = true;
}

struct SasPostEffectVisitorState {
    IDirect3DDevice9* device;
    int executed;
};

bool SasPostEffectVisitor(void* user, SasEffect* sas)
{
    SasPostEffectVisitorState* state = static_cast<SasPostEffectVisitorState*>(user);
    if (sas == nullptr || state == nullptr || state->device == nullptr) {
        return true;
    }
    if (!SasHasPostEffect(sas)) {
        return true;
    }
    SasExecutePostEffect(sas, state->device, -1);
    ++state->executed;
    return true;
}

int MmeAbsInt(int value)
{
    // [big-C 72287] (uVar9 ^ (int)uVar9 >> 0x1f) - ((int)uVar9 >> 0x1f).
    return (value ^ (value >> 31)) - (value >> 31);
}

} // namespace

// ---------------------------------------------------------------------------
// FUN_1800599a0 / FUN_180059aa0
// ---------------------------------------------------------------------------

void MmeResetObjectPlanState(ModelData* model)
{
    // [0x1800599a0] FUN_1800599a0.
    if (model == nullptr) {
        return;
    }
    model->setDrawTypeIndex(-1);                        // +0xe8 [L72221]
    RenderSnapshot& snap = model->snapshot();
    snap.subset_index = -1;                             // +0x160 [L72222]
    snap.draw_type = 1;                                 // +0x164 qword = 1 [L72223]
    snap.draw_type_index = 0;
    // +0xe0 (the original's snapshot-pointer slot) is implicit in the port.
    snap.binding_context = nullptr;                     // +0x158 [L72225]
    snap.base_texture = nullptr;                        // +0x170..0x180 [L72226-72228]
    snap.toon_texture = nullptr;                        // (borrowed references; no
    snap.sphere_texture = nullptr;                      //  releases, like the original)
    snap.sphere_mode = 0;                               // +0x188 dword [L72229]
    snap.toon_used = 0;                                 // +0x18c word [L72230]
    snap.effect_file_used = 0;
    snap.base_texture_present = 0;                      // +0x18e byte [L72231]
    // [L72232-72235] release the Phase 3 SAS binding object at ModelData+0x358;
    // the Phase 2 manager bindings are owned by the manager map.
}

void MmeRefreshObjectPlan(ModelData* model, int hostIndex)
{
    // [0x180059aa0] FUN_180059aa0.
    if (model == nullptr) {
        return;
    }
    model->passPlanScratch().passKey = hostIndex;       // +0xf0 [L72283]
    if (hostIndex < 0) {
        return;                                         // [L72284]
    }
    ModelData::PassPlanScratch& scratch = model->passPlanScratch();
    if (model->kind() == 1) {
        // [L72285-72299] PMD/PMX model: |ExpGetPmdOrder| + identity scratch.
        scratch.state0 = MmeAbsInt(ExpGetPmdOrder(hostIndex));   // +0xec [L72286-72287]
        // [L72288-72297] +0xf8/+0x10c/+0x120/+0x134 = 1.0f, rest 0 = identity.
        memset(&model->planMatrix(), 0, sizeof(D3DMATRIX));
        model->planMatrix().m[0][0] = 1.0f;
        model->planMatrix().m[1][1] = 1.0f;
        model->planMatrix().m[2][2] = 1.0f;
        model->planMatrix().m[3][3] = 1.0f;
        scratch.flag = ExpGetPmdDisp(hostIndex) ? 1 : 0;         // +0xf4 [L72298]
    } else {
        // [L72300-72335] accessory: |ExpGetAcsOrder| + ExpGetAcsWorldMat.
        scratch.state0 = MmeAbsInt(ExpGetAcsOrder(hostIndex));
        D3DMATRIX world = ExpGetAcsWorldMat(hostIndex);          // [L72303]
        memcpy(&model->planMatrix(), &world, sizeof(D3DMATRIX)); // +0xf8..+0x134
        scratch.flag = ExpGetAcsDisp(hostIndex) ? 1 : 0;         // [L72335]
    }
    // [L72338] memcpy(+0x190, +0xf8, 0x40).
    model->setPlanMatrixCopy(model->planMatrix());
}

// ---------------------------------------------------------------------------
// FUN_18002baa0 + FUN_18005fd30
// ---------------------------------------------------------------------------

void MmeSortPlanObjects(MmeContext* ctx)
{
    // [0x18002baa0] FUN_18002baa0: |order|-sorted pass over the registered
    // objects; renderClass 1 -> ctx+0xb8, renderClass 2 -> ctx+0xd8 (kit
    // L301-447), and the merged extra-pass plan into manager+0x158, copied to
    // ctx+0x148 by FUN_18005fd30.
    EffectOwnerManager* manager = g_ownerManager;
    ctx->passPlanA.clear();
    ctx->passPlanB.clear();
    if (manager != nullptr) {
        manager->orderedPlan.clear();
    }

    std::map<int, ModelData*> sorted;   // |order| -> model (first insert wins)
    for (size_t i = 0; i < ctx->models.size(); ++i) {
        ModelData* model = ctx->models[i];
        if (model == nullptr) {
            continue;
        }
        // Objects skipped by the host scan keep their reset order 0 (+0xec).
        sorted.insert(std::make_pair(model->passPlanScratch().state0, model));
    }

    for (std::map<int, ModelData*>::const_iterator it = sorted.begin();
         it != sorted.end(); ++it) {
        ModelData* model = it->second;
        if (model->renderClass() == 1) {
            ctx->passPlanA.push_back(model);            // [kit L303-361]
        } else if (model->renderClass() == 2) {
            ctx->passPlanB.push_back(model);            // [kit L363-422]
        }
        if (model->renderClass() != 0 && manager != nullptr) {
            manager->orderedPlan.push_back(model);      // manager+0x158
        }
    }

    // [0x18005fd30] FUN_18005fd30(dst = ctx+0x148, src = manager+0x158).
    if (manager != nullptr) {
        ctx->renderPassList.assign(manager->orderedPlan.begin(),
                                   manager->orderedPlan.end());
    } else {
        ctx->renderPassList.clear();
    }
}

// ---------------------------------------------------------------------------
// MME_RebuildRenderPassPlan [0x18005b9e0]
// ---------------------------------------------------------------------------

void MmeRebuildRenderPassPlan()
{
    MmeContext* ctx = g_context;
    if (ctx == nullptr) {
        return;
    }

    // [L82-98] per-model scratch reset loop (FUN_1800599a0 + the inline
    // +0xec..+0x137 reset).
    for (size_t i = 0; i < ctx->models.size(); ++i) {
        MmeResetObjectPlanState(ctx->models[i]);
        ctx->models[i]->ResetPassPlanScratch();
    }

    // [L99-111] clear the pending plan containers; reset the binding-context
    // round-robin (ctx[0xe] = ctx+0x70 = 0).
    ctx->passPlanA.clear();
    ctx->passPlanB.clear();
    ctx->bindingContextIndex = 0;

    // [L123-137] release the cached render-target/depth slots
    // (ctx+0x20..0x38 / ctx+0x40) and [L138] the current binding object.
    for (int i = 0; i < 4; ++i) {
        if (ctx->cachedRenderTargets[i] != nullptr) {
            ctx->cachedRenderTargets[i]->Release();
            ctx->cachedRenderTargets[i] = nullptr;
        }
    }
    if (ctx->cachedDepthStencil != nullptr) {
        ctx->cachedDepthStencil->Release();
        ctx->cachedDepthStencil = nullptr;
    }
    ctx->currentBindingObject = nullptr;        // puVar5[0x2d] = 0 [L138]

    // [L153-205] the PMD scan.
    int pmdNum = ExpGetPmdNum();                // [L153]
    int acsNum = ExpGetAcsNum();                // [L154]
    std::map<int, ModelData*> orderMap;         // [L140-152] the scratch order map
    for (int i = 0; i < pmdNum; ++i) {
        unsigned long long id =
            reinterpret_cast<unsigned long long>(ExpGetPmdID(i));      // [L160]
        if (ctx->modelRegistry.find(id) != ctx->modelRegistry.end()) { // [L162 FUN_180061470]
            ModelData* model = MmeFindOrCreateModelEntry(id);          // [L164]
            if (model != nullptr) {
                MmeRefreshObjectPlan(model, i);                        // [L166 FUN_180059aa0]
                // [L169-171] the manager per-model subset map (Phase 3).
                if (ExpGetPmdDisp(i)) {                                // [L172]
                    int order = MmeAbsInt(ExpGetPmdOrder(i));          // [L174-175]
                    orderMap.insert(std::make_pair(order, model));     // [L190-197]
                }
            }
        }
    }

    // [L206-253] the accessory scan (+ the Phase 2 CONTROLOBJECT staging and
    // the GetAcsAttachedPmd attach resolution).
    for (int i = 0; i < acsNum; ++i) {
        unsigned long long id =
            reinterpret_cast<unsigned long long>(ExpGetAcsID(i));      // [L210]
        if (ctx->modelRegistry.find(id) != ctx->modelRegistry.end()) { // [L212]
            ModelData* model = MmeFindOrCreateModelEntry(id);          // [L214]
            if (model != nullptr) {
                MmeRefreshObjectPlan(model, i);                        // [L216]
                MmeStageControlObjectValues(model, i);   // Phase 3 seam (host values)
                MmeResolveAccessoryAttach(model, id);    // GetAcsAttachedPmd
                // [L219-221] the manager per-model subset map (Phase 3).
                if (ExpGetAcsDisp(i)) {                                // [L222]
                    int order = MmeAbsInt(ExpGetAcsOrder(i));          // [L224-225]
                    orderMap.insert(std::make_pair(order, model));     // [L240-247]
                }
            }
        }
    }

    // [L254-291] LoadedPMMFile -> EMM autoload (FUN_1800570D0 when the path
    // is not the "(invalid)" marker).
    const wchar_t* pmm = LoadedPMMFile();                              // [L254]
    if (pmm != nullptr) {
        std::string pmmAnsi = MmeWideToAnsi(pmm);                      // [L256 FUN_180063340]
        if (pmmAnsi != "(invalid)") {                                  // [L258-275 memcmp]
            MmeAutoLoadEmmForPmm(pmmAnsi.c_str());                     // [L281 FUN_1800570D0]
        }
    }

    // [L292-297] clear ctx+0x148 (the pass list).
    ctx->renderPassList.clear();

    // [L298-300] FUN_18002baa0 + FUN_18005fd30: build the sorted plan.
    MmeSortPlanObjects(ctx);

    // [L449-484] the viewport-change detection: GetViewport vs ctx+0x170;
    // on change: reset the ctx+0x198 offscreen-target list (Phase 3), store
    // the viewport and set ctx+0x18a = 1.
    IDirect3DDevice9* device = ctx->device;
    if (device != nullptr) {
        D3DVIEWPORT9 viewport;
        memset(&viewport, 0, sizeof(viewport));
        device->GetViewport(&viewport);                                // slot 0x180 [L449]
        bool changed = !ctx->lastPlanViewportValid ||
                       memcmp(&ctx->lastPlanViewport, &viewport, sizeof(viewport)) != 0;
        if (changed) {
            ctx->passTrie.clear();          // ctx+0x198 offscreen-target list (Phase 3)
            ctx->lastPlanViewport = viewport;                          // [L478-483]
            ctx->lastPlanViewportValid = true;                         // [L484] ctx+0x18a = 1
        }
    }

    // [L486-492] the repeat tail:
    //   empty pass list (or effects disabled) -> ctx+0x14 = 1, no host call;
    //   otherwise ctx+0x14 = N+1 and ExpSetRenderRepeatCount(N+1)
    //   (PHASE21B: N extra passes -> repeat count N+1).
    if (ctx->renderPassList.empty() || ctx->effectEnabled == 0) {
        ctx->lastRepeatCount = 1;
    } else {
        // [PHASE3 wiring] a loaded post-effect (ScriptClass scene/sceneorobject
        // + ScriptOrder postprocess) needs one extra object-render iteration
        // per frame (its ScriptExternal=Color re-renders the scene), so it
        // contributes +1 to N like a pass-plan entry does.
        int postEffectCount = 0;
        MmeEngineForEachSas([](void* user, SasEffect* sas) {
            if (sas != nullptr && SasHasPostEffect(sas)) {
                ++*static_cast<int*>(user);
            }
            return true;
        }, &postEffectCount);
        ctx->lastRepeatCount = static_cast<int>(ctx->renderPassList.size()) +
                               (postEffectCount > 0 ? 1 : 0) + 1;
        ExpSetRenderRepeatCount(ctx->lastRepeatCount);                 // [L491]
    }

    // [L493] FUN_18005c510 - final pass bookkeeping.
    MmeUpdatePassBookkeeping(ctx);

    // [L494-507] the scratch order map is destroyed with the scope.
}

// ---------------------------------------------------------------------------
// FUN_18005c510
// ---------------------------------------------------------------------------

void MmeUpdatePassBookkeeping(MmeContext* ctx)
{
    // [0x18005c510] FUN_18005c510.
    if (ctx == nullptr) {
        return;
    }

    // [L74177-74180] per-model FUN_1800599a0 resets.
    for (size_t i = 0; i < ctx->models.size(); ++i) {
        MmeResetObjectPlanState(ctx->models[i]);
    }

    // [L74181-74183] the pending-snapshot vector clear (begin -> end copy).
    if (!ctx->pendingSnapshots.empty()) {
        ctx->pendingSnapshots.clear();
    }

    // [L74184-74189] the container/flag resets: the clear-color/depth maps
    // (ctx+0x3c / ctx+0x42, Phase 3 SAS target maps - the port keeps them in
    // sync below), ctx+0x10 = 0, ctx+0x08 = 0, ctx+0x60 = 0, ctx+0x11/0x12 = 0.
    ctx->bindingReadyFlag = 0;
    ctx->lastDrawnModel = nullptr;
    ctx->postBindingContext = nullptr;
    ctx->errorReportedFlag = 0;
    ctx->backgroundDrawnFlag = 0;

    if (ctx->lastRepeatCount == static_cast<int>(ctx->renderPassList.size())) {
        // [L74191-74315] the state re-init block (runs when the draw-time
        // repeat index reaches the pass size): re-read the render targets,
        // reset the viewport bookkeeping, the animated-texture manager and
        // the flags.
        IDirect3DDevice9* device = ctx->device;
        for (int i = 0; i < 4; ++i) {
            if (ctx->cachedRenderTargets[i] != nullptr) {
                ctx->cachedRenderTargets[i]->Release();               // [L74218-74221]
                ctx->cachedRenderTargets[i] = nullptr;
            }
            if (device != nullptr) {
                device->GetRenderTarget(i, &ctx->cachedRenderTargets[i]);   // slot 0x130
            }
        }
        if (ctx->cachedDepthStencil != nullptr) {
            ctx->cachedDepthStencil->Release();                       // [L74227-74229]
            ctx->cachedDepthStencil = nullptr;
        }
        if (device != nullptr) {
            device->GetDepthStencilSurface(&ctx->cachedDepthStencil); // slot 0x140
        }
        // [L74286] ctx+0x438 word = 0x100 (the post-effect Clear gate marker).
        ctx->hasRenderTargetsFlag = 0x100;
        ctx->postClearEnabled = 1;
        // [L74287] ctx+0x43a = 1, then [L74303-74313] cleared when any plan-B
        // model is a "normal" object (unknownFlag360 != 1 && flag368 != 0).
        ctx->allObjectsSpecialFlag = 1;
        for (size_t i = 0; i < ctx->passPlanB.size(); ++i) {
            ModelData* model = ctx->passPlanB[i];
            if (model != nullptr && model->unknownFlag360() != 1 &&
                model->flag368() != 0) {
                ctx->allObjectsSpecialFlag = 0;
                break;
            }
        }
    }

    // [L74317-74336] repeat == 0: stage the clear colors (GetClearColor into
    // the per-target map; the depth map gets 1.0f). The per-target map is the
    // Phase 3 SAS target table; Phase 2 latches the host clear color.
    if (ctx->lastRepeatCount == 0) {
        D3DCOLOR clearColor = GetClearColor();                        // [L74318]
        ctx->postClearColor = clearColor;
        IDirect3DDevice9* device = ctx->device;
        for (int i = 0; i < 4; ++i) {
            IDirect3DSurface9* target = nullptr;
            if (device != nullptr && SUCCEEDED(device->GetRenderTarget(i, &target)) &&
                target != nullptr) {
                ctx->clearColorMap[target] = clearColor;              // [L74321-74325]
                target->Release();
            }
        }
        IDirect3DSurface9* depth = nullptr;
        if (device != nullptr && SUCCEEDED(device->GetDepthStencilSurface(&depth)) &&
            depth != nullptr) {
            ctx->clearDepthMap[depth] = 1.0f;                         // [L74330-74334]
            depth->Release();
        }
    }
}

// ---------------------------------------------------------------------------
// FUN_18005c970 (record apply) - Phase 3 seam. The original reads the record
// object at model+0x00 (RT surface at +8, depth at model+8, viewport W/H at
// model+0x40/0x44, clear flags/color) and issues SetRenderTarget(0) /
// SetDepthStencilSurface / SetViewport / Clear. It no-ops when the record
// pointer is null. The port's ModelData carries no per-object record yet
// (Phase 3 sas binding objects), so every call takes the null-record path.
// ---------------------------------------------------------------------------
static void MmeApplyPassRecord(void* /*record*/, IDirect3DDevice9* /*device*/,
                               int /*adaptiveTessSet*/)
{
    // FUN_18005c970: `if (rec == 0) return;` - the port's records are always
    // null today, which is exactly the original's null-record early-out.
}

// ---------------------------------------------------------------------------
// FUN_18005cac0 / FUN_18005a410 / FUN_18005a2c0 / FUN_18005a5c0 - the per
// -model record walkers (manager+0xa0 subset-map entries; EndScene/BeginScene
// -wrapped when a binding object exists). The port's manager map is empty
// (Phase 3 seam), so the walks iterate nothing - the same observable no-op
// the original produces with null binding objects. As the Phase 3 stand-in
// for FUN_18005a5c0 (the class-2/post-chain walker), the loaded effects'
// post-effect chains run here: render-target switches, clears, script
// external draws and the loop-unrolled fullscreen passes. The scene
// re-render remains owned by the host repeat mechanism.
// ---------------------------------------------------------------------------
static void MmeWalkObjectRecords(ModelData* model, void* bindingObject)
{
    (void)model;
    (void)bindingObject;
    IDirect3DDevice9* device = nullptr;
    MmeContext* ctx = MmeGetContext();
    if (ctx != nullptr) {
        device = ctx->device;
    }
    if (device == nullptr) {
        return;
    }
    SasEnsureHostCallbacksInstalled();
    SasPostEffectVisitorState state;
    state.device = device;
    state.executed = 0;
    MmeEngineForEachSas(&SasPostEffectVisitor, &state);
}

// ---------------------------------------------------------------------------
// FUN_18005d5c0 - the queued background-record replay. Port of the decompiled
// body: for every 0x220 record, Apply() its captured state block, re-assert
// the current viewport, and re-issue the recorded draw. Records with
// enabled == 0 replay DrawPrimitive; the rest DrawIndexedPrimitive. A
// fullscreen XYZRHW triangle-pair record (primitive_count == 2, type ==
// TRIANGLELIST) additionally goes through the background-quad fixup when the
// viewport changed since the record was queued: the source vertex buffer is
// re-scaled into a cached copy (map keyed by the stream vertex buffer, the
// original ctx+0x190 map) and bound via SetStreamSource before the draw.
// ---------------------------------------------------------------------------

// The FUN_18005d5c0 background-quad fixup (the GetFVF(RHW) -> GetStreamSource
// -> copy/rescale -> SetStreamSource block, big-C 74955-75040).
static void MmeFixupBackgroundQuad(MmeContext* ctx, IDirect3DDevice9* device,
                                   const RenderSnapshot& rec, const D3DVIEWPORT9& vp)
{
    DWORD fvf = 0;
    if (FAILED(device->GetFVF(&fvf)) || (fvf & 0x4) == 0) {   // D3DFVF_XYZRHW only
        return;
    }
    IDirect3DVertexBuffer9* stream = nullptr;
    UINT offset = 0;
    UINT stride = 0;
    if (FAILED(device->GetStreamSource(0, &stream, &offset, &stride))) {
        return;
    }
    if (stream == nullptr) {
        return;
    }
    D3DVERTEXBUFFER_DESC desc;
    memset(&desc, 0, sizeof(desc));
    if (FAILED(stream->GetDesc(&desc)) || desc.Size == 0) {
        stream->Release();                                    // borrowed
        return;
    }

    IDirect3DVertexBuffer9* fixed = nullptr;
    std::map<IDirect3DVertexBuffer9*, IDirect3DVertexBuffer9*>::iterator it =
        ctx->backgroundFixedVbs.find(stream);
    if (it != ctx->backgroundFixedVbs.end()) {
        fixed = it->second;
    }

    // Rebuild when there is no cached copy or the plan viewport changed
    // (ctx+0x18a, the same two conditions as the original).
    if (fixed == nullptr || ctx->lastPlanViewportValid) {
        void* src = nullptr;
        if (SUCCEEDED(stream->Lock(0, 0, &src, D3DLOCK_READONLY)) && src != nullptr) {
            IDirect3DVertexBuffer9* rebuilt = nullptr;
            if (SUCCEEDED(device->CreateVertexBuffer(desc.Size, 0, desc.FVF,
                                                     D3DPOOL_DEFAULT, &rebuilt,
                                                     nullptr)) &&
                rebuilt != nullptr) {
                void* dst = nullptr;
                if (SUCCEEDED(rebuilt->Lock(0, 0, &dst, 0)) && dst != nullptr) {
                    const float cachedX = static_cast<float>(ctx->lastPlanViewport.X);
                    const float cachedY = static_cast<float>(ctx->lastPlanViewport.Y);
                    const float cachedW = static_cast<float>(ctx->lastPlanViewport.Width);
                    const float cachedH = static_cast<float>(ctx->lastPlanViewport.Height);
                    const unsigned int vertexCount = rec.primitive_count * 3;
                    const unsigned int vertexBytes = 16;    // x, y, z, rhw
                    const unsigned int copyBytes = vertexCount * vertexBytes;
                    if (copyBytes <= desc.Size) {
                        memcpy(dst, src, copyBytes);
                        unsigned char* d = static_cast<unsigned char*>(dst);
                        for (unsigned int v = 0; v < vertexCount; ++v) {
                            float* xy = reinterpret_cast<float*>(d + v * stride);
                            float x = xy[0] - cachedX;
                            float y = xy[1] - cachedY;
                            if (x >= 0.0f) {
                                x = (static_cast<float>(vp.Width) * x) / cachedW;
                            }
                            if (y >= 0.0f) {
                                y = (static_cast<float>(vp.Height) * y) / cachedH;
                            }
                            xy[0] = x;
                            xy[1] = y;
                        }
                    }
                    rebuilt->Unlock();
                }
                if (fixed != nullptr) {
                    fixed->Release();                         // drop the replaced cache entry
                    ctx->backgroundFixedVbs.erase(it);
                }
                ctx->backgroundFixedVbs[stream] = rebuilt;    // owning reference
                fixed = rebuilt;
                fixed->AddRef();                              // borrow for the bind below
            }
            stream->Unlock();
        }
        ctx->lastPlanViewportValid = false;
    }

    if (fixed != nullptr) {
        device->SetStreamSource(0, fixed, offset, stride);
        fixed->Release();                                     // the borrowed reference
    }
    stream->Release();                                        // the GetStreamSource reference
}

static void MmeReplayBackgroundRecords(MmeContext* ctx)
{
    if (ctx == nullptr || ctx->device == nullptr) {
        return;
    }
    IDirect3DDevice9* device = ctx->device;
    D3DVIEWPORT9 vp;
    memset(&vp, 0, sizeof(vp));
    device->GetViewport(&vp);

    for (size_t i = 0; i < ctx->pendingSnapshots.size(); ++i) {
        const RenderSnapshot& rec = ctx->pendingSnapshots[i];
        IDirect3DStateBlock9* block =
            static_cast<IDirect3DStateBlock9*>(rec.binding_context);
        if (block != nullptr) {
            block->Apply();                                   // slot 0x28
        }
        device->SetViewport(&vp);                             // slot 0x178
        if (rec.enabled == 0) {
            if (rec.draw_type == 2 && rec.primitive_type == 4 &&
                vp.X == 0 && vp.Y == 0 &&
                (ctx->lastPlanViewport.X != 0 || ctx->lastPlanViewport.Y != 0 ||
                 vp.Width != ctx->lastPlanViewport.Width ||
                 vp.Height != ctx->lastPlanViewport.Height)) {
                MmeFixupBackgroundQuad(ctx, device, rec, vp);
            }
            device->DrawPrimitive(static_cast<D3DPRIMITIVETYPE>(rec.primitive_type),
                                  rec.start_index, rec.primitive_count);
        } else {
            device->DrawIndexedPrimitive(
                static_cast<D3DPRIMITIVETYPE>(rec.primitive_type),
                rec.base_vertex_index, rec.min_vertex_index, rec.vertex_count,
                rec.start_index, rec.primitive_count);
        }
    }
}

// ---------------------------------------------------------------------------
// FUN_18005d130
// ---------------------------------------------------------------------------

void MmePassBookkeeping(MmeContext* ctx)
{
    // [0x18005d130] FUN_18005d130.
    if (ctx == nullptr) {
        return;
    }
    IDirect3DDevice9* device = ctx->device;
    bool hasPasses = !ctx->renderPassList.empty() && ctx->effectEnabled != 0;
    if (hasPasses && ctx->lastRepeatCount < static_cast<int>(ctx->renderPassList.size())) {
        // [L74723-74725] the post effect runs when the repeat index is still
        // inside the pass list.
        MmeRunPostEffect(ctx);
        // [L74726-74733] renderPassList[lastRepeatCount] record step (the
        // binding object's vtable slot 0x80 call on the record). The port's
        // records carry no binding object (Phase 3), so the step is the
        // null-record no-op, and the ctx+0x18 captured-state restore has no
        // token to apply.
    }
    MmeUpdatePassBookkeeping(ctx);                                 // [L74736]
    if (hasPasses) {
        // [L74738-74749] the scene is closed while the new repeat's record is
        // applied and reopened for the draws that follow. Offsets 0x150/0x148
        // on the original's wrapper vtable are EndScene/BeginScene (the same
        // slots MMHack hooks for its own BeginScene/EndScene).
        if (device != nullptr) {
            device->EndScene();                                    // [L74738 slot 0x150]
        }
        if (ctx->lastRepeatCount < 1) {
            ctx->currentBindingObject = nullptr;                   // [L74740]
        } else {
            // [L74743-74745] ctx+0x168 = renderPassList[lastRepeatCount - 1].
            int index = ctx->lastRepeatCount - 1;
            if (index >= 0 &&
                index < static_cast<int>(ctx->renderPassList.size())) {
                ctx->currentBindingObject = ctx->renderPassList[index];
            } else {
                ctx->currentBindingObject = nullptr;
            }
            // [L74746] GetRenderState(0xa1) feeds the FUN_18005c970 record
            // apply (the record's SetRenderTarget/SetDepthStencilSurface/
            // viewport/Clear choreography).
            DWORD adaptive = 0;
            if (device != nullptr) {
                device->GetRenderState(static_cast<D3DRENDERSTATETYPE>(0xa1), &adaptive);   // 161 = D3DRS_ADAPTIVE_TESS_X (raw constant; not in the DXSDK headers)
            }
            MmeApplyPassRecord(ctx->currentBindingObject, device, adaptive != 0 ? 1 : 0);
        }
        if (device != nullptr) {
            device->BeginScene();                                  // [L74749 slot 0x148]
        }
    }
}

// ---------------------------------------------------------------------------
// FUN_18005e210
// ---------------------------------------------------------------------------

void MmeRunPostEffect(MmeContext* ctx)
{
    // [0x18005e210] FUN_18005e210.
    if (ctx == nullptr || ctx->backgroundDrawnFlag != 0) {
        return;                                                        // [L75462]
    }
    IDirect3DDevice9* device = ctx->device;
    bool hadBinding = ctx->postBindingContext != nullptr;
    if (!hadBinding) {
        // [L75464-75468] acquire + Capture (slot 0x20 on the standard
        // IDirect3DStateBlock9 = Capture).
        ctx->postBindingContext =
            static_cast<IDirect3DStateBlock9*>(MmeAcquireBindingContext(ctx));
        if (ctx->postBindingContext != nullptr) {
            ctx->postBindingContext->Capture();
        }
    }
    if (ctx->errorReportedFlag == 0) {
        MmeReportDrawError(ctx);                                       // [L75469-75471]
    }
    ctx->backgroundDrawnFlag = 1;                                      // [L75472]

    if (ctx->currentBindingObject == nullptr) {
        if (ctx->hasRenderTargetsFlag != 0) {                          // [L75475]
            // [L75477-75503] the frame's scene is closed (slot 0x150 =
            // EndScene on the original's wrapper vtable), the animated
            // -texture pre-tick runs outside the scene, the optional post
            // Clear is issued, and a fresh scene is opened (slot 0x148 =
            // BeginScene) so the draws that follow land on top of the post
            // chain's output.
            if (device != nullptr) {
                device->EndScene();                                    // [L75477]
            }
            // [FUN_180001e70(ctx+0x240)] the animated-texture pre tick
            // (IsEditMode checked inside).
            MmeTickAnimatedTextures(ctx);
            if (ctx->postClearEnabled != 0) {                          // [L75478]
                // [L75479-75501] GetClearColor + the per-target map updates
                // (Phase 3) + Clear(0, 0, 7) with the host clear color, with
                // the device state captured around the Clear.
                D3DCOLOR clearColor = GetClearColor();                 // [L75479]
                ctx->postClearColor = clearColor;
                for (int i = 0; i < 4; ++i) {
                    if (ctx->cachedRenderTargets[i] != nullptr) {
                        ctx->clearColorMap[ctx->cachedRenderTargets[i]] = clearColor;
                    }
                }
                if (ctx->cachedDepthStencil != nullptr) {
                    ctx->clearDepthMap[ctx->cachedDepthStencil] = 1.0f;
                }
                if (device != nullptr) {
                    device->BeginStateBlock();                         // FUN_180067500
                }
                if (device != nullptr) {
                    device->Clear(0, nullptr, 7, clearColor, 1.0f, 0); // slot 0x158 [L75501]
                }
                if (device != nullptr) {
                    IDirect3DStateBlock9* captured = nullptr;
                    if (SUCCEEDED(device->EndStateBlock(&captured)) && captured != nullptr) {
                        captured->Apply();                             // FUN_1800675e0
                        captured->Release();
                    }
                }
            }
            if (device != nullptr) {
                device->BeginScene();                                  // [slot 0x148]
            }
        }
    } else {
        // [L75513-75519] the pass-record branch (Phase 3): the record's gate
        // byte at +0x3c selects EndScene / animated-texture tick / BeginScene.
    }

    // [L75521-75527] the passPlanB walk (FUN_18005a5c0 per model): the SAS
    // record apply for every class-2 object. The port's manager subset map is
    // empty (Phase 3), so each walk is the original's null-binding no-op.
    // Phase 3 stand-in: the loaded effects' post-effect chains execute here
    // once per runner pass (the port keeps the previous wiring, which the
    // decompile attributes to this walk once class-2 records exist).
    MmeWalkObjectRecords(nullptr, ctx->currentBindingObject);
    for (size_t i = 0; i < ctx->passPlanB.size(); ++i) {
        MmeWalkObjectRecords(ctx->passPlanB[i], ctx->currentBindingObject);
    }

    if (!hadBinding) {
        // [L75528-75533] restore: GetViewport / Apply (slot 0x28) /
        // SetViewport, then drop the borrowed context.
        D3DVIEWPORT9 viewport;
        memset(&viewport, 0, sizeof(viewport));
        if (device != nullptr) {
            device->GetViewport(&viewport);                            // slot 0x180
        }
        if (ctx->postBindingContext != nullptr) {
            ctx->postBindingContext->Apply();                          // slot 0x28 [L75530]
        }
        if (device != nullptr) {
            device->SetViewport(&viewport);                            // slot 0x178
        }
        ctx->postBindingContext = nullptr;
    }
}

// ---------------------------------------------------------------------------
// FUN_18005da50 - the first-draw runner. Despite the historical name in the
// port, the decompiled body is the per-repeat choreography entry, not just an
// error reporter: state-block capture, the GetRenderState(0xa1) probe, the
// passPlanB backwards error-flag walk, the null-binding branch (post-effect
// execution gated on adaptive-tess + binding flags, then the host clear),
// the bound-record apply (FUN_18005cac0), the passPlanA walk, the queued
// background-record replay (FUN_18005d5c0) and the viewport/state restore.
// ---------------------------------------------------------------------------
void MmeReportDrawError(MmeContext* ctx)
{
    if (ctx == nullptr || ctx->errorReportedFlag != 0) {
        return;                                                        // [L75142]
    }
    ctx->errorReportedFlag = 1;                                        // [L75143]
    IDirect3DDevice9* device = ctx->device;
    bool acquiredHere = ctx->postBindingContext == nullptr;
    if (acquiredHere) {
        ctx->postBindingContext =
            static_cast<IDirect3DStateBlock9*>(MmeAcquireBindingContext(ctx));
        if (ctx->postBindingContext != nullptr) {
            ctx->postBindingContext->Capture();                        // slot 0x20
        }
    }

    // [L75150-75176] GetRenderState(0xa1); when set, probe the RT0 surface
    // description (the original keeps it for the validation block below).
    DWORD adaptive = 0;
    if (device != nullptr) {
        device->GetRenderState(static_cast<D3DRENDERSTATETYPE>(0xa1), &adaptive);   // slot 0x1d0; 161 = D3DRS_ADAPTIVE_TESS_X (raw constant)
    }
    if (adaptive != 0 && device != nullptr) {
        IDirect3DSurface9* target = nullptr;
        if (SUCCEEDED(device->GetRenderTarget(0, &target)) && target != nullptr) {
            D3DSURFACE_DESC desc;
            memset(&desc, 0, sizeof(desc));
            target->GetDesc(&desc);
            (void)desc;
            target->Release();
        }
    }

    // [L75169-...] the passPlanB backwards walk (FUN_18005a410) OR-ing each
    // model's binding-error flag (ModelData+0x358+4; Phase 3 -> 0).
    unsigned int bindingErrorFlags = 0;
    for (size_t i = ctx->passPlanB.size(); i > 0; --i) {
        MmeWalkObjectRecords(ctx->passPlanB[i - 1], ctx->currentBindingObject);
    }

    if (ctx->currentBindingObject == nullptr) {                        // [L75177]
        ctx->hasRenderTargetsFlag = 0;                                 // [L75178] ctx+0x188 = 0
        if (adaptive != 0 && bindingErrorFlags != 0) {                 // [L75179 gate]
            if (ctx->postEffectRanFlag == 0) {                         // +0x189 == 0
                // [L75184] the scene is closed around the post-effect chain
                // (slot 0x150 = EndScene, slot 0x148 = BeginScene).
                if (device != nullptr) {
                    device->EndScene();
                }
                // [L75190-75229] the four color targets must share one staged
                // clear color and the depth target must be staged; the Phase 3
                // target tables are empty in the port, so the validation
                // passes trivially (clearColorsValid = depthStaged = 1).
                // [L75230] uVar12 = FUN_180001880(ctx+0x240, ...) - the
                // post-effect HRESULT carried by ctx->lastPostEffectHr.
                unsigned long hr = ctx->lastPostEffectHr;
                if (hr == 0) {
                    ctx->hasRenderTargetsFlag = 0x100;                 // [L75233]
                } else {
                    ctx->postEffectRanFlag = 1;                        // [L75237 area] +0x189 = 1
                    // [L75237-75346] "DirectX Error: <desc> [%08X]\n" + the
                    // localized "Failed to process post effect:" message box.
                    const char* desc = MmeDxErrDescription(hr);
                    char hex[16];
                    sprintf_s(hex, sizeof(hex), "%08X", static_cast<unsigned int>(hr));
                    std::string errorLine = "DirectX Error: ";
                    errorLine += (desc != nullptr ? desc : "");
                    errorLine += " [";
                    errorLine += hex;
                    errorLine += "]\n";
                    MmeLogWrite(errorLine.c_str(), 0);                 // [L75294]
                    std::string message;
                    if (MmeIsEnglishUiMode()) {
                        message = "Failed to process post effect:\n";  // 0x1800b5988
                    } else {
                        // 0x1800b5958: GBK bytes of the localized release.
                        message += "\xBA\xF3\xC6\xDA\xCC\xD8\xD0\xA7\xB4\xA6\xC0\xED"
                                   "\xD6\xD0\xB7\xA2\xC9\xFA\xB4\xED\xCE\xF3\x3A";
                    }
                    message += errorLine;
                    MessageBoxA(g_mainWindow, message.c_str(), "MikuMikuEffect",
                                MB_ICONERROR);
                }
            }
            // [L75368-75405] the shown-message dedup walk (DAT_1800d99d8 list).
        }
    } else {
        // [L75410] FUN_18005cac0: the bound record apply. Phase 3 null-record
        // no-op (the original early-outs when the record object is null).
        MmeApplyPassRecord(ctx->currentBindingObject, device, 0);
    }

    // [L75413-75421] with no record bound and no post target set, re-issue
    // the host clear the suppressed OnClear was holding back. This runs once
    // per repeat iteration, after the queued background records have been
    // collected and before they are replayed below.
    if (ctx->currentBindingObject == nullptr && ctx->hasRenderTargetsFlag == 0) {
        D3DCOLOR clearColor = GetClearColor();
        if (device != nullptr) {
            device->Clear(0, nullptr, 7, clearColor, 1.0f, 0);         // slot 0x158
        }
    }

    // [L75423] the passPlanA walk (FUN_18005a2c0; null-binding no-op).
    for (size_t i = 0; i < ctx->passPlanA.size(); ++i) {
        MmeWalkObjectRecords(ctx->passPlanA[i], ctx->currentBindingObject);
    }

    // [L75426] FUN_18005d5c0: replay every queued background record so the
    // grid / axis / tool-icon / background-image draws suppressed during the
    // record collection land on the fresh clear.
    if (ctx->currentBindingObject == nullptr) {
        MmeReplayBackgroundRecords(ctx);
    }

    if (acquiredHere) {
        // [L75429-75437] restore: GetViewport / Apply (slot 0x28) /
        // SetViewport, then drop the borrowed context.
        D3DVIEWPORT9 viewport;
        memset(&viewport, 0, sizeof(viewport));
        if (device != nullptr) {
            device->GetViewport(&viewport);                            // slot 0x180
        }
        if (ctx->postBindingContext != nullptr) {
            ctx->postBindingContext->Apply();                          // slot 0x28
        }
        if (device != nullptr) {
            device->SetViewport(&viewport);                            // slot 0x178
        }
        ctx->postBindingContext = nullptr;
    }
}

// ---------------------------------------------------------------------------
// FUN_180055690 / FUN_180055780
// ---------------------------------------------------------------------------

void* MmeAcquireBindingContext(MmeContext* ctx)
{
    // [0x180055690] FUN_180055690: round-robin over the pooled state blocks;
    // past the end, CreateStateBlock(D3DSBT_ALL) via device slot 0x1d8
    // [L69596] and push_back into the ctx+0x78 vector.
    if (ctx == nullptr || ctx->bindingContextDevice == nullptr) {
        return nullptr;
    }
    size_t index = static_cast<size_t>(ctx->bindingContextIndex);
    if (index >= ctx->bindingContextPool.size()) {
        IDirect3DStateBlock9* block = nullptr;
        // D3DSBT_ALL = 1 [L69596 CreateStateBlock(device, 1, &out)].
        if (FAILED(ctx->bindingContextDevice->CreateStateBlock(D3DSBT_ALL, &block)) ||
            block == nullptr) {
            return nullptr;
        }
        ctx->bindingContextPool.push_back(block);
    }
    void* result = ctx->bindingContextPool[index];
    ctx->bindingContextIndex += 1;                                     // [L69619]
    return result;
}

void MmeReleaseBindingContextPool(MmeContext* ctx)
{
    // [0x180055780] FUN_180055780.
    if (ctx != nullptr) {
        ctx->ReleaseBindingContextPool();
    }
}

} // namespace mme
