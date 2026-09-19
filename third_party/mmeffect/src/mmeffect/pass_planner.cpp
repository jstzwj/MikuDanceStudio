// pass_planner.cpp - see pass_planner.h
#include "pass_planner.h"

#include <algorithm>
#include <cstdint>
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
#include "sas_interpreter.h"   // [PHASE3 wiring] host callbacks
#include "sas_exec.h"          // [PHASE3 wiring] SasEffect full definition

namespace mme {

namespace {

// [R3 forward declaration] the host slots (SasDefaultDrawBufferPass /
// SasHostRunPass, above the definition) bind the scene walk carrier's
// whole-object record through the same (0, model, -1) resolver the scene
// drivers use - the definition sits with the other record helpers below.
static MaterialBinding* MmeSceneRecordBinding(ModelData* record);

// [PHASE3 wiring] the fullscreen-quad pass draw for post-effect techniques
// (RunPass kind 1), a 1:1 port of MMEffect.dll v0.37 x64 [0x18005A9C0]
// (verified by full decompile + disasm, IDA session 2109c52a, 2026-09-15).
// The original draws a screen-covering TRIANGLEFAN of four XYZ|NORMAL|TEX1
// vertices (SetFVF(274) = 0x112, stride 32, DrawPrimitiveUP) in NDC with a
// one-pixel outward expansion on the left and top edges, wrapped in the
// effect sandwich Begin / param-bind / SetFVF / BeginPass / draw / EndPass /
// End, between a save-clear-restore of four render states and the geometry
// bindings (vertex declaration, stream-0 source, indices). NOT XYZRHW and
// NOT pixel coordinates: ray.fx's ScreenSpaceQuadVS is `return Position;`
// and reconstructs the view ray from Position, so it consumes exactly these
// NDC inputs.
struct SasQuadVertex {
    float x, y, z;
    float nx, ny, nz;
    float u, v;
};

HRESULT SasDefaultDrawBufferPass(ID3DXEffect* effect, IDirect3DDevice9* device, int passIndex)
{
    if (device == nullptr) {
        return E_POINTER;
    }
    // [0x18005aa0e] GetViewport FIRST: the expansion terms below use the
    // CURRENT viewport W/H - during a redirected technique walk this is the
    // offscreen RT viewport, exactly what the original computes against.
    D3DVIEWPORT9 vp;
    memset(&vp, 0, sizeof(vp));
    device->GetViewport(&vp);
    if (effect == nullptr) {
        // [0x18005aa14-0x18005aa22] a null effect is a silent S_OK no-op in
        // the original (the result is preset to 0), not a latching failure.
        return S_OK;
    }
    // [0x18005aa2f-0x18005aa31] the staged per-target clear ERASE: the
    // original checks the walk carrier's class here (`*(ModelData+0x364) ==
    // 2`, the scene/postprocess class) and, when set, calls sub_18005E5D0 -
    // the four CURRENT render targets leave the ctx+0x1E0 color map (the
    // depth map is untouched). Once this pass draws onto the targets, their
    // staged "needs clear" decision must not survive into the next snapshot
    // validation. The port resolves the carrier from ctx->sceneWalkCarrier
    // (set by the scene walk drivers); object-class walks carry no carrier
    // and keep their existing behavior.
    MmeContext* walkCtx = g_context;
    ModelData* walkCarrier =
        (walkCtx != nullptr) ? walkCtx->sceneWalkCarrier : nullptr;
    if (walkCarrier != nullptr && walkCarrier->renderClass() == 2) {
        MmeEraseStagedClearColor(walkCtx);              // [sub_18005E5D0]
    }
    // [0x18005aa36-0x18005aab8] save the geometry bindings: the vertex
    // declaration (GetVertexDeclaration, device vtable slot 88), the
    // stream-0 source (GetStreamSource -> VB + offset + stride, slot 101)
    // and the index buffer (GetIndices, slot 105).
    IDirect3DVertexDeclaration9* savedDecl = nullptr;
    IDirect3DVertexBuffer9* savedStreamVb = nullptr;
    UINT savedStreamOffset = 0;
    UINT savedStreamStride = 0;
    IDirect3DIndexBuffer9* savedIndices = nullptr;
    device->GetVertexDeclaration(&savedDecl);
    device->GetStreamSource(0, &savedStreamVb, &savedStreamOffset,
                            &savedStreamStride);
    device->GetIndices(&savedIndices);
    // [0x18005aacf-0x18005ab14] save FILLMODE(7) / LIGHTING(137) /
    // STENCILENABLE(52) / SCISSORTESTENABLE(161), then [0x18005ab28-
    // 0x18005ab66] zero all four - FILLMODE is literally set to 0.
    DWORD rsFillMode = 0;
    DWORD rsLighting = 0;
    DWORD rsStencil = 0;
    DWORD rsScissor = 0;
    device->GetRenderState(D3DRS_FILLMODE, &rsFillMode);
    device->GetRenderState(D3DRS_LIGHTING, &rsLighting);
    device->GetRenderState(D3DRS_STENCILENABLE, &rsStencil);
    device->GetRenderState(D3DRS_SCISSORTESTENABLE, &rsScissor);
    device->SetRenderState(D3DRS_FILLMODE, 0);
    device->SetRenderState(D3DRS_LIGHTING, 0);
    device->SetRenderState(D3DRS_STENCILENABLE, 0);
    device->SetRenderState(D3DRS_SCISSORTESTENABLE, 0);
    // Every failure path below releases the saved refs (indices, stream VB,
    // declaration - the original's release order) WITHOUT restoring the
    // device [0x18005ab87-0x18005aba9 and the parallel branches]: the
    // propagated failure latches the sas+0x39 run-failed flag in the walk,
    // this path never draws again, and the original deliberately leaves the
    // cleared state behind.
    auto releaseSavedRefs = [&]() {
        if (savedIndices != nullptr) {
            savedIndices->Release();
        }
        if (savedStreamVb != nullptr) {
            savedStreamVb->Release();
        }
        if (savedDecl != nullptr) {
            savedDecl->Release();
        }
    };
    UINT passes = 0;
    HRESULT hr = effect->Begin(&passes, 0);
    if (FAILED(hr)) {
        static int logged = 0;
        if (logged++ < 3) {
            char line[0x220];
            sprintf_s(line, sizeof(line), "QuadPass: Begin FAILED hr=0x%08X\n",
                      static_cast<unsigned int>(hr));
            MmeLogWrite(line, 0);
        }
        releaseSavedRefs();
        return hr;
    }
    // [0x18005abb5-0x18005abc3] the pass parameter bind, BETWEEN Begin and
    // BeginPass/SetFVF: the original stores the current-subset context
    // (`ModelData+0xE0 = &ModelData+0x138` - the carrier's cached snapshot
    // block) and calls sub_18001B5B0(binding, ModelData) - the material-color
    // family (ids 24-30/0x4E via host vt+0x10), the material textures
    // (0x29-0x2B via vt+0x48), the name-table colors (0x39-0x49 via vt+0x58),
    // the scalar bools (0x3D-0x42 via vt+0x60) and the ANIMATEDTEXTURE seek
    // (case 45). The port's unified binder reads the same cached snapshot via
    // MmeBindStandardParameters/MmeBindSemanticParameters. Only scene walks
    // publish a carrier (ctx->sceneWalkCarrier); object-class effects already
    // bind through their per-draw snapshot apply, unchanged.
    if (walkCarrier != nullptr) {
        MaterialBinding* passBinding = MmeSceneRecordBinding(walkCarrier);
        if (passBinding != nullptr) {
            MmeBindStandardParameters(walkCarrier, passBinding,
                                      walkCarrier->snapshot());
        }
    }
    // [0x18005abf5-0x18005acbc] the quad, from the entry viewport: one full
    // pixel of NDC expansion on the left/top (2/W, 2/H) and one full texel
    // of under-scan on the matching u/v edges (-1/W, -1/H). V0 is the
    // constant pair 0x1800B3A90={1,-1,0,0} / 0x1800B3AA0={0,0,1,1} (so
    // uv=(1,1)); V1..V3 carry the computed edges. Every normal is (0,0,0)
    // and every z is 0.
    const float w = static_cast<float>(vp.Width);
    const float h = static_cast<float>(vp.Height);
    const float xLeft = -1.0f - (1.0f / w) * 2.0f;    // [0x18005ac29]
    const float yTop = (1.0f / h) * 2.0f + 1.0f;      // [0x18005ac6f]
    const float uLeft = -(1.0f / w);                  // [0x18005ac51]
    const float vTop = -(1.0f / h);                   // [0x18005ac94]
    SasQuadVertex quad[4] = {
        { 1.0f, -1.0f, 0.0f, 0.0f, 0.0f, 0.0f, 1.0f, 1.0f },
        { xLeft, -1.0f, 0.0f, 0.0f, 0.0f, 0.0f, uLeft, 1.0f },
        { xLeft, yTop, 0.0f, 0.0f, 0.0f, 0.0f, uLeft, vTop },
        { 1.0f, yTop, 0.0f, 0.0f, 0.0f, 0.0f, 1.0f, vTop },
    };
    // [0x18005accd] SetFVF(274) = D3DFVF_XYZ | D3DFVF_NORMAL | D3DFVF_TEX1
    // (32 bytes/vertex), set BEFORE BeginPass - not an XYZRHW/POSITIONT quad.
    device->SetFVF(D3DFVF_XYZ | D3DFVF_NORMAL | D3DFVF_TEX1);
    hr = effect->BeginPass(static_cast<UINT>(passIndex));
    if (FAILED(hr)) {
        static int logged = 0;
        if (logged++ < 3) {
            char line[0x220];
            sprintf_s(line, sizeof(line), "QuadPass: BeginPass(%d) FAILED hr=0x%08X\n",
                      passIndex, static_cast<unsigned int>(hr));
            MmeLogWrite(line, 0);
        }
        releaseSavedRefs();
        return hr;   // no End after a failed BeginPass [0x18005ace9]
    }
    // [0x18005ad2c] DrawPrimitiveUP(TRIANGLEFAN = 6, 2 primitives, stride 32).
    hr = device->DrawPrimitiveUP(D3DPT_TRIANGLEFAN, 2, quad,
                                 sizeof(SasQuadVertex));
    if (FAILED(hr)) {
        static int logged = 0;
        if (logged++ < 3) {
            char line[0x220];
            sprintf_s(line, sizeof(line),
                      "QuadPass: DrawPrimitiveUP FAILED hr=0x%08X (vp=%ux%u)\n",
                      static_cast<unsigned int>(hr), vp.Width, vp.Height);
            MmeLogWrite(line, 0);
        }
        releaseSavedRefs();
        return hr;   // no EndPass/End after a failed draw [0x18005ad38]
    }
    hr = effect->EndPass();                          // [0x18005ad65]
    if (FAILED(hr)) {
        releaseSavedRefs();
        return hr;
    }
    hr = effect->End();                              // [0x18005ad9e]
    if (FAILED(hr)) {
        releaseSavedRefs();
        return hr;
    }
    // [0x18005ade2-0x18005ae6d] the success-path restore: the four render
    // states first, then SetVertexDeclaration, SetStreamSource(0),
    // SetIndices (the original ignores every restore call's HRESULT).
    device->SetRenderState(D3DRS_FILLMODE, rsFillMode);
    device->SetRenderState(D3DRS_LIGHTING, rsLighting);
    device->SetRenderState(D3DRS_STENCILENABLE, rsStencil);
    device->SetRenderState(D3DRS_SCISSORTESTENABLE, rsScissor);
    device->SetVertexDeclaration(savedDecl);
    device->SetStreamSource(0, savedStreamVb, savedStreamOffset,
                            savedStreamStride);
    device->SetIndices(savedIndices);
    releaseSavedRefs();   // [0x18005ae7c-0x18005aea3] indices, VB, decl
    return hr;
}

long SasHostRunPass(void* /*ctx*/, SasEffect* sas, int kind, int passIndex)
{
    // The device comes from the live context (registration is one-time, the
    // device can be recreated across resets).
    MmeContext* ctx = g_context;
    IDirect3DDevice9* device = (ctx != nullptr) ? ctx->device : nullptr;
    if (device == nullptr) {
        return E_POINTER;
    }
    if (kind == 1) {
        ID3DXEffect* effect = (sas != nullptr) ? sas->effect : nullptr;
        HRESULT hr = SasDefaultDrawBufferPass(effect, device, passIndex);
        return SUCCEEDED(hr) ? 0 : hr;
    }
    // kind 0 (standard-geometry draw) = the original's host slot 0
    // [FUN_18005a740, the ModelData+0x08 embedded callback]. The walk reached
    // here after SetTechnique (SasRunPassCommands / SasApplyCommandImpl), so
    // the remaining sandwich is Begin / BeginPass(passIndex) / the RECORDED
    // DrawIndexedPrimitive / EndPass / End.
    // [fcn_18005a740 instruction-level, 2026-09-18 审计修正] the original
    // resolves the draw through the slot-0 SELF's record - the WALKED
    // CARRIER's persistent ModelData+0x138 snapshot for scene walks, the
    // staged per-draw record for object walks. The carrier snapshot is live:
    // fcn_180059ba0 updates it on every owning host draw BEFORE the
    // render-class 1/2 "not drawn as an object" early-return (fcn_18005d340
    // L94 precedes L118). The previous "scene carriers never record a draw"
    // note was a misread - without this fallback every Draw=Geometry (and
    // script-less Pass=) inside a scene/sceneorobject technique silently
    // dropped the carrier's geometry layer (ray 系灯/雾伴生子 fx 丢层).
    // The "+0x160 < 0" invalid-DrawState guard is the snapshot's unset
    // subset marker (subset_index < 0).
    SasHostDrawRecord stagedRec;
    SasHostDrawRecord* rec = SasHostDrawRecordCurrent();
    ModelData* walkCarrier =
        (ctx != nullptr) ? ctx->sceneWalkCarrier : nullptr;
    if ((rec == nullptr || rec->device == nullptr) && walkCarrier != nullptr &&
        ctx != nullptr && ctx->device != nullptr) {
        const RenderSnapshot& cs = walkCarrier->snapshot();
        if (cs.subset_index >= 0 && cs.enabled != 0 && cs.primitive_count > 0) {
            stagedRec.device = ctx->device;
            stagedRec.primitiveType = cs.primitive_type;
            stagedRec.baseVertexIndex = cs.base_vertex_index;
            stagedRec.minVertexIndex = cs.min_vertex_index;
            stagedRec.vertexCount = cs.vertex_count;
            stagedRec.startIndex = cs.start_index;
            stagedRec.primitiveCount = cs.primitive_count;
            rec = &stagedRec;
        }
    }
    if (rec == nullptr || rec->device == nullptr) {
        return 0;
    }
    // [FUN_18005a740 0x18005a77f-0x18005a781] right after the pass-index
    // guard, the original checks the walk carrier's class
    // (`*(ModelData+0x364) == 2`, scene/postprocess) and erases the staged
    // per-target clear colors (sub_18005E5D0) - BEFORE the effect resolution
    // and Begin. Object-class walks (ctx->sceneWalkCarrier null) skip it.
    if (walkCarrier != nullptr && walkCarrier->renderClass() == 2) {
        MmeEraseStagedClearColor(ctx);                  // [sub_18005E5D0]
    }
    ID3DXEffect* effect = (sas != nullptr) ? sas->effect : nullptr;
    if (effect == nullptr) {
        // [FUN_18005a740 a2 == null branch] the bare recorded draw, no effect
        // sandwich (the FUN_18001b940 fallback `(**a7)(a7, 0, 0)` lands here;
        // its result is discarded by that caller).
        return rec->device->DrawIndexedPrimitive(
            static_cast<D3DPRIMITIVETYPE>(rec->primitiveType),
            rec->baseVertexIndex, rec->minVertexIndex, rec->vertexCount,
            rec->startIndex, rec->primitiveCount);
    }
    UINT passes = 0;
    HRESULT hr = effect->Begin(&passes, 0);
    if (FAILED(hr)) {
        return hr;
    }
    // [FUN_18005a740 0x18005a839-0x18005a840] the current-subset context
    // store (`ModelData+0xE0 = &ModelData+0x138`) + sub_18001B5B0(binding,
    // ModelData) BETWEEN Begin and BeginPass - the same pass parameter bind
    // the quad slot does (see SasDefaultDrawBufferPass). Only scene walks
    // carry a binding context here; object-class effects keep their per-draw
    // binding (MmeApplyModelRenderSnapshot), unchanged.
    if (walkCarrier != nullptr) {
        MaterialBinding* passBinding = MmeSceneRecordBinding(walkCarrier);
        if (passBinding != nullptr) {
            MmeBindStandardParameters(walkCarrier, passBinding,
                                      walkCarrier->snapshot());
        }
    }
    hr = effect->BeginPass(static_cast<UINT>(passIndex));
    if (FAILED(hr)) {
        // [FUN_18005a740 failure chain] the original skips EndPass/End after
        // a failed BeginPass/Begin - the propagated failure latches the
        // sas+0x39 run-failed flag in the walk, which stops every further
        // effect draw for this effect (the bare host draw takes over).
        return hr;
    }
    hr = rec->device->DrawIndexedPrimitive(
        static_cast<D3DPRIMITIVETYPE>(rec->primitiveType),
        rec->baseVertexIndex, rec->minVertexIndex, rec->vertexCount,
        rec->startIndex, rec->primitiveCount);
    if (FAILED(hr)) {
        return hr;   // no EndPass/End after a failed draw, like the original
    }
    hr = effect->EndPass();
    if (FAILED(hr)) {
        return hr;
    }
    return effect->End();
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
    callbacks.GetClearColor = &SasHostGetClearColor;
    callbacks.GetClearDepth = &SasHostGetClearDepth;
    SasSetHostCallbacks(callbacks);
    installed = true;
}

int MmeAbsInt(int value)
{
    // [big-C 72287] (uVar9 ^ (int)uVar9 >> 0x1f) - ((int)uVar9 >> 0x1f).
    return (value ^ (value >> 31)) - (value >> 31);
}

// ---------------------------------------------------------------------------
// FUN_18005a410 / FUN_18005a5c0 - the per-record scene-effect drivers
// ---------------------------------------------------------------------------

// The whole-object binding of a pass record (the (0, model, -1) entry the
// lazy resolver created).
static MaterialBinding* MmeSceneRecordBinding(ModelData* record)
{
    if (record == nullptr) {
        return nullptr;
    }
    MaterialBinding* binding = MmeActiveModelBinding(record);
    if (binding == nullptr) {
        binding = MmeResolveModelEffectBinding(record);
    }
    return binding;
}

// [FUN_18005c970 `v4 = *a1`] the wrapper's inner OFFSCREEN record is carried
// by the queue item itself since the 2026-09 re-audit (sub_18002CA80 stores
// the 0x98 record pointer at wrapper+0 at QUEUE-BUILD time, 0x18002d415 -
// the runtime staged-RT0 identity matching this port used before was a
// workaround for the missing association, not the original's mechanism).
// The staged-RT0 match below survives for ONE consumer: the offscreen
// DefaultEffect staging after a scene step - the rows belong to the 0x2E
// resource whose surface the technique's OWN script commands actually
// redirected RT0 into (a RenderColorTarget=X script binds X's surface,
// which is not necessarily the queue item's resource), so the host must
// identify the rows' owner by the staged surface, exactly like the
// original's per-parameter records do.
static SasResource* MmeMatchOffscreenForState(SasRunState* state,
                                              MaterialBinding* binding)
{
    if (state == nullptr || state->activeColor[0] == nullptr ||
        binding == nullptr || binding->sas == nullptr) {
        return nullptr;
    }
    for (size_t i = 0; i < binding->sas->resources.size(); ++i) {
        SasResource& res = binding->sas->resources[i];
        if (res.semanticId == 0x2E && res.surface == state->activeColor[0]) {
            return &res;
        }
    }
    return nullptr;
}

static SasResource* MmeMatchStagedOffscreen(ModelData* record,
                                            MaterialBinding* binding)
{
    if (record == nullptr) {
        return nullptr;
    }
    return MmeMatchOffscreenForState(record->runState(), binding);
}

// [offscreen DefaultEffect staging] publish the parsed rows of the offscreen
// target a suspended scene technique renders into, for the binding
// resolution's unassigned-model fallback (ctx->offscreenDefaultEffect). The
// staged RT0 (the surface the step's RenderColorTarget0 script bound /
// FUN_18005c970 re-bound) identifies the 0x2E resource; a null match or an
// empty row vector clears the staging. The resume walk drops it again
// (MmeClearOffscreenDefaultBindings in MmeFinishSceneRecord).
static void MmeStageOffscreenDefaultEffect(MmeContext* ctx, ModelData* record)
{
    if (ctx == nullptr) {
        return;
    }
    ctx->offscreenDefaultEffect = nullptr;
    if (record == nullptr) {
        return;
    }
    SasRunState* state = record->runState();
    if (state == nullptr || !state->suspended) {
        return;
    }
    SasResource* offscreen =
        MmeMatchStagedOffscreen(record, MmeSceneRecordBinding(record));
    if (offscreen != nullptr && !offscreen->defaultEffectMap.empty()) {
        ctx->offscreenDefaultEffect = &offscreen->defaultEffectMap;
    }
}

// Flush the sas->log lines appended by a run into MMEffect.txt (the per
// -effect log string is only written to the file once at load; runtime
// script errors/warnings would otherwise be invisible).
static void MmeFlushSasLogDelta(SasEffect* sas, size_t logLengthBefore)
{
    if (sas != nullptr && sas->log.size() > logLengthBefore) {
        MmeLogWrite(sas->log.substr(logLengthBefore).c_str(), 0);
    }
}

// [sub_18002CA80 0x18002d1a0-0x18002d69c] the carrier's queue entries come
// from the walk over the binding record's OFFSCREENRENDERTARGET vector
// (binding+0x1B8). That vector is filled ONLY by the SAS parameter parser
// (FUN_180011960) for semanticId == 0x2E (0x180014080-0x180014093:
// sub_18001F3E0 = push_back of the 0x98 record, in parameter declaration
// order); 0x26 RENDERCOLORTARGET / 0x27 RENDERDEPTHSTENCILTARGET / plain
// textures never enter it. The port therefore enumerates the binding's SAS
// resource table filtered to semanticId == 0x2E - the exact projection the
// original's vector holds, in the same order (the resource table IS the
// parameter enumeration order). Technique validity/count plays no role: a
// technique is never the unit of a render turn.
static void MmeCollectSceneOffscreens(ModelData* record,
                                      std::vector<SasResource*>* out)
{
    out->clear();
    MaterialBinding* binding = MmeSceneRecordBinding(record);
    if (binding == nullptr || binding->sas == nullptr) {
        return;
    }
    SasEffect* sas = binding->sas;
    for (size_t i = 0; i < sas->resources.size(); ++i) {
        if (sas->resources[i].semanticId == 0x2E) {
            out->push_back(&sas->resources[i]);
        }
    }
}

// [0x18005db44 / 0x18005e170 / 0x18005e46a `mov rdx, [rsi+0x168]`] the
// WRAPPER-KEYED technique resolution every scene driver selects its technique
// through. The three walk sites (FUN_18005da50's planB step walk, its planA
// full-run walk, FUN_18005e210's planB resume walk) all pass ctx+0x168 -
// the CURRENT turn's queue wrapper - as the second argument, and
// sub_18005A410 / sub_18005A5C0 / sub_18005A2C0 look the binding record up
// in the mgr+0xA0 map keyed (wrapper+56 id, carrier, -1) [verified at the
// binary level, 0x18005a449 `mov edx,[rdx+38h]` + the (id, carrier, subset)
// tree walk at 0x18005a464-0x18005a4bb]:
//   - wrapper NULL (the base turn / a null ctx+0x168): key id 0 = the ROOT
//     binding record, whose techniques[0] is the carrier's FIRST pass
//     technique - every scene carrier steps/resumes its first technique;
//   - wrapper = a queue item: the child-round drain created one (id,
//     carrier, -1) record per carrier for the turn's resource NAME, so
//     EVERY carrier whose effect declares a 0x2E parameter of that name
//     walks its (single, script-first) technique in that turn; carriers
//     without the name hold a null record and the walk no-ops.
// The walk technique is ALWAYS the binding's own first technique (the
// original's (id, carrier) record stores no per-turn technique choice) -
// never the queue item's.
static int MmeSceneWalkTechIndex(MmeContext* ctx, ModelData* record)
{
    if (ctx == nullptr || record == nullptr) {
        return -1;
    }
    MaterialBinding* binding = MmeSceneRecordBinding(record);
    int first = (binding != nullptr) ? binding->sceneTechIndex : -1;
    if (first < 0) {
        return -1;
    }
    if (ctx->currentBindingObject == nullptr) {
        return first;   // base turn: the root record, every carrier walks
    }
    // [sub_18005A410's (id, carrier, -1) lookup] the turn's id came from the
    // resource NAME: a carrier owns the turn iff its effect declares a 0x2E
    // parameter with that name.
    SasResource* turn = ctx->currentBindingOffscreen;
    SasEffect* sas = (binding != nullptr) ? binding->sas : nullptr;
    if (turn == nullptr || sas == nullptr) {
        return -1;
    }
    for (size_t i = 0; i < sas->resources.size(); ++i) {
        if (sas->resources[i].semanticId == 0x2E &&
            sas->resources[i].name == turn->name) {
            return first;
        }
    }
    return -1;
}

// [FUN_18005a410 / FUN_18005a5c0 / FUN_18005a2c0 shared prologue,
// 0x18005a4eb-0x18005a51f / 0x18005a63b-0x18005a65f / 0x18005a313-0x18005a337]
// `if (*(record+0x158)) { GetViewport; Apply; SetViewport; }` - re-apply the
// carrier's CACHED binding-context state block (ModelData+0x158 = the pooled
// block MME_UpdateModelRenderSnapshot stored and Captured on the record's
// whole-object host draws, sub_180059ba0 L36-41; render-class 1/2 carriers
// acquire one), preserving the CURRENT viewport over the applied state (the
// GetViewport right before, the SetViewport right after - device vtable
// slots 0x180/0x178, state-block Apply slot 0x28).
static void MmeApplyRecordStateBlock(ModelData* record,
                                     IDirect3DDevice9* device)
{
    if (record == nullptr || device == nullptr) {
        return;
    }
    IDirect3DStateBlock9* block =
        static_cast<IDirect3DStateBlock9*>(record->snapshot().binding_context);
    if (block == nullptr) {
        return;
    }
    D3DVIEWPORT9 viewport;
    memset(&viewport, 0, sizeof(viewport));
    device->GetViewport(&viewport);                       // slot 0x180
    block->Apply();                                       // slot 0x28
    device->SetViewport(&viewport);                       // slot 0x178
}

// [FUN_18005a410] step the record's scene technique to the ScriptExternal
// suspension. The technique comes from the CALLER's wrapper key (the
// (turn id, carrier) binding record's techniques[0]; the D130 site
// 0x18005d17c dispatches through the queue item's own wrapper+0 chain, the
// DA50 planB walk passes ctx+0x168) - the binding's FIRST technique, the
// same one every turn of the carrier walks (a turn is one of the carrier's
// 0x2E resources, never one of its techniques). Verified against the
// binary: the original has NO suspended-state reuse - every call
// unconditionally new(0x88)s a fresh run state, constructs it via
// sub_18001b7b0 (capturing the CURRENT color targets 0..3, the depth
// stencil and the viewport as the saved set; command index 0), then
// OVERWRITES +0x358, orphaning (leaking) the old state, and re-walks the
// technique from index 0 in step mode (sub_18001bbc0, a4 = 1): the
// pre-scene commands re-bind + re-clear the G-buffers and the walk parks at
// the suspension point. The port destroys the old state instead of leaking
// it; the orphaned original state never restores either, so the observable
// device sequence is identical.
//
// The rebuild (not reuse) is load-bearing at the second step site of a
// turn boundary. Sequence per the binary: the turn-tail step of
// renderPassList[repeat] (FUN_18005d130 -> vtable+0x80, 0x18005d18d), then
// FUN_18005c510 resets the flags, then FUN_18005c970 (0x18005d218)
// rebinds the bound record's STAGED targets + Clear, then the first-draw
// DA50 walk (sub_18005a410 per planB record, 0x18005db4e, re-entered at
// 0x18005d579) steps the same records again. The rebuilt state therefore
// captures the staged (redirected) targets as its saved set and its
// pre-scene commands re-clear the G-buffers for the upcoming turn - the
// original's intent, not an accident: the following resume's restore
// epilogue returns to that staged set, not to a stale back-buffer capture.
// (The original prologue 0x18005a4eb-0x18005a51f re-Applies the record's
// +0x158 state block, viewport-preserved [see MmeApplyRecordStateBlock
// below].)
static void MmeStepSceneRecord(ModelData* record, int techIndex)
{
    IDirect3DDevice9* device = nullptr;
    MmeContext* ctx = g_context;
    if (ctx != nullptr) {
        device = ctx->device;
    }
    if (record == nullptr || device == nullptr) {
        return;
    }
    MaterialBinding* binding = MmeSceneRecordBinding(record);
    if (binding == nullptr || binding->sas == nullptr ||
        techIndex < 0 ||
        techIndex >=
            static_cast<int>(binding->sas->techniques.size())) {
        return;
    }
    // [0x18005a4eb-0x18005a51f, inside the binding/sas-valid branch] the
    // +0x158 state-block prelude: MME_UpdateModelRenderSnapshot
    // [FUN_180059ba0 L36-41] stores a POOLED binding-context state block at
    // ModelData+0x158 (Captured) on the first whole-object host draw of
    // every render-class 1/2 carrier - plan-B records are render-class 2, so
    // the slot is LIVE for them, not vacuous. GetViewport -> Apply ->
    // SetViewport: the block's device state is restored while the CURRENT
    // viewport is preserved over it.
    MmeApplyRecordStateBlock(record, device);
    // [0x18005a410] no suspended check and no completed-state exception: a
    // step is the ownership transfer point for +0x358. Whatever state a
    // PREVIOUS step left there (suspended at ScriptExternal, or completed
    // with the index past the last command) is discarded now - the original
    // overwrites the slot and leaks the old 0x88 object; the port releases
    // it (no device restore is issued in either: the orphaned original
    // state's epilogue never runs). The state created below is the one the
    // P0-4 lifecycle protects: it stays at +0x358 until the post-walk
    // resume (MmeFinishSceneRecord), the planner's viewport-change reset or
    // the teardown destroys it.
    if (record->runState() != nullptr) {
        SasDestroyRunState(record->runState());
        record->setRunState(nullptr);
    }
    SasRunState* state = SasCreateRunState(device);
    record->setRunState(state);
    // [0x18005a50f] `*(dword*)(record+0xE8) = *(dword*)(record+0x168)` -
    // re-sync the draw-type index (+0xE8) to the CACHED snapshot's
    // draw_type_index (ModelData+0x168 = snapshot+0x30, the normalized type
    // of the record the draw path last cached at +0x138). This makes the
    // FUN_18005a1e0 change detector (drawTypeIndex vs the incoming record's
    // +0x30) re-run the binding refresh on the record's next host draw after
    // a different pass drew last.
    record->setDrawTypeIndex(record->snapshot().draw_type_index);
    // [0x18005a513] sub_18005A020(record, a2, record+0x138): model+0xE0 =
    // &model+0x138 (the snapshot-pointer store; the +0xE0 slot is implicit
    // in the port) + the whole-object binding's sub_18001B340 parameter
    // walk - the matrix family (ids 0-23/52-54 via host vt+0/+8, the
    // CONTROLOBJECT arrays 24-27 via vt+0x18), VIEWPORTPIXELSIZE (33 via
    // vt+0x30), the step-time bools (60/62/69 via vt+0x60) and the ints
    // (67/68 via vt+0x68) - reading the carrier's cached snapshot block.
    // The port's unified binder (MmeBindStandardParameters ->
    // MmeBindSemanticParameters) covers exactly those families through the
    // same snapshot; this is the ONLY step-time bind and its values persist
    // across the ScriptExternal suspension (the resume never re-runs A020).
    // The per-subset A020 loop (sub_18002A210 walk, gated on
    // !model+0x364 && materialCount > 0) never runs for scene carriers -
    // the port's MmeSelectMaterialEffectBinding keeps it for renderClass 0.
    MmeBindStandardParameters(record, binding, record->snapshot());
    SasTechnique& tech = binding->sas->techniques[
        static_cast<size_t>(techIndex)];
    size_t logBefore = binding->sas->log.size();
    // The walk parks at ScriptExternal (suspended) or runs to completion -
    // either way the original (FUN_18005a410) keeps the 0x88 state at
    // +0x358; it never frees it here. A completed walk leaves the index
    // past the last command, so the post-walk resume (FUN_18005a5c0) runs
    // no commands and only re-applies the restore epilogue
    // (FUN_18001bbc0 LABEL_12 -> LABEL_13) before destroying the state.
    // Freeing the completed state would null +0x358 at the resume and make
    // the post walk re-run the whole technique a second time.
    // [FUN_18005a410 0x18005a586 `cmp byte ptr [rdi+39h], 0` -> the
    // `!(effect && tech && !sas+0x39)` walk gate] the step-mode walk is
    // additionally skipped while the script run-failed latch is set - the
    // state-block prelude, the fresh run state, the +0xE8 re-sync and the
    // sub_18005A020 bind above have already run by then; ONLY the walk
    // is gated.
    if (!binding->sas->runFailed) {
        // The walk's host slots (FUN_18005a740 / 0x18005A9C0 / the Clear
        // dispatcher sub_18005AEE0) receive the carrier's ModelData self;
        // the port publishes it for the walk's duration (R3/R8 seam).
        ctx->sceneWalkCarrier = record;
        SasExecuteTechniqueStep(binding->sas, &tech, device, state);
        ctx->sceneWalkCarrier = nullptr;
        MmeFlushSasLogDelta(binding->sas, logBefore);
    }
    // Offscreen DefaultEffect staging: the walk just bound the pre-scene
    // targets; if it parked at ScriptExternal on an offscreen target with
    // DefaultEffect rows, publish them for the scene draws that follow. The
    // rows' host is identified by the STAGED RT0 (the technique's own script
    // commands may have redirected into any 0x2E parameter - see
    // MmeMatchOffscreenForState), not by the queue item.
    MmeStageOffscreenDefaultEffect(ctx, record);
}

// [FUN_18005a5c0] resume the record's stepped technique to completion: the
// post-scene commands run (the lighting/composite passes), the redirected
// targets and the viewport restore, the run state is destroyed. The
// technique comes from the caller's wrapper key (a2+56 -> the (id, carrier)
// binding record's techniques[0]; FUN_18005e210's planB walk passes
// ctx+0x168 at 0x18005e46a) - the SAME technique the step parked. A state
// whose walk already completed (no ScriptExternal) resumes as a harmless
// empty walk - only the restore epilogue re-applies - and is destroyed
// here as well.
static void MmeFinishSceneRecord(ModelData* record, int techIndex)
{
    IDirect3DDevice9* device = nullptr;
    MmeContext* ctx = g_context;
    if (ctx != nullptr) {
        device = ctx->device;
    }
    // The offscreen DefaultEffect pass is over: the resume restores the main
    // targets, so the staged rows and the transient bindings must not leak
    // into the draws that follow (all paths - the early returns below fire
    // for records that never staged anything, and the clear is idempotent).
    MmeClearOffscreenDefaultBindings();
    if (record == nullptr || device == nullptr) {
        return;
    }
    MaterialBinding* binding = MmeSceneRecordBinding(record);
    if (binding == nullptr || binding->sas == nullptr ||
        techIndex < 0 ||
        techIndex >=
            static_cast<int>(binding->sas->techniques.size())) {
        return;
    }
    SasTechnique& tech = binding->sas->techniques[
        static_cast<size_t>(techIndex)];
    if (record->runState() == nullptr) {
        // The original (FUN_18005a5c0) passes +0x358 straight into
        // FUN_18001bbc0 with no null check - a null state dereferences at
        // `v17 = *a3` the moment the command list is non-empty. The
        // structural invariant is that every resumed record was stepped
        // first: FUN_18005a410 always leaves a state at +0x358 (suspended
        // at ScriptExternal, or completed with the index past the last
        // command). The port cannot reproduce the crash; a violation of
        // that invariant is a no-op.
        return;
    }
    // [0x18005a63b-0x18005a65f, inside the binding/sas-valid branch] the
    // same +0x158 state-block prelude as the step (GetViewport -> Apply ->
    // SetViewport; see MmeApplyRecordStateBlock). NOTE the resume has NO
    // +0xE8 re-sync and NO sub_18005A020/B340 refresh - the step-time
    // semantic values persist in the effect across the suspension. The ONLY
    // context store is [0x18005a6c9] `*(record+0xE0) = record+0x138` (the
    // current-subset pointer onto the cached snapshot) so the walk's per
    // -pass B5B0 slot binds (0x18005a840 / 0x18005abc3) read THIS record -
    // the port's equivalent is publishing ctx->sceneWalkCarrier around the
    // walk below.
    MmeApplyRecordStateBlock(record, device);
    size_t logBefore = binding->sas->log.size();
    // [FUN_18005a5c0 0x18005a5f2 `cmp byte ptr [rax+39h], 0` -> the
    // `!(effect && tech && !sas+0x39)` walk gate] the post-scene walk is
    // skipped while the run-failed latch is set, but the run state at +0x358
    // is destroyed UNCONDITIONALLY right after (the original passes the
    // destroy straight through the flagged branch).
    if (!binding->sas->runFailed) {
        ctx->sceneWalkCarrier = record;   // [0x18005a6c9] the walk host-self
        SasResumeTechnique(binding->sas, &tech, device, record->runState());
        ctx->sceneWalkCarrier = nullptr;
        MmeFlushSasLogDelta(binding->sas, logBefore);
    }
    SasDestroyRunState(record->runState());
    record->setRunState(nullptr);
}

// [FUN_18005a2c0] run the record's whole scene technique in one go:
// FUN_18001bab0 stack-constructs a fresh 0x88 run state, walks the
// technique via FUN_18001bbc0 in full mode (a4 = 0 - ScriptExternal is a
// no-op, nothing suspends) and destroys the state. Unlike the step/resume
// pair this never touches +0x358: preprocess-class records are not stepped.
static void MmeFullRunSceneRecord(ModelData* record)
{
    IDirect3DDevice9* device = nullptr;
    MmeContext* ctx = g_context;
    if (ctx != nullptr) {
        device = ctx->device;
    }
    if (record == nullptr || device == nullptr) {
        return;
    }
    MaterialBinding* binding = MmeSceneRecordBinding(record);
    if (binding == nullptr || binding->sas == nullptr ||
        binding->sceneTechIndex < 0 ||
        binding->sceneTechIndex >=
            static_cast<int>(binding->sas->techniques.size())) {
        return;
    }
    // [0x18005a313-0x18005a337, inside the binding/sas-valid branch] the
    // same +0x158 state-block prelude as the step/resume pair (GetViewport
    // -> Apply -> SetViewport; see MmeApplyRecordStateBlock), then the SAME
    // +0xE8 re-sync and sub_18005A020 binding refresh the step does
    // (`*(dword*)(record+0xE8) = *(dword*)(record+0x168)`; the A020 refresh
    // = the entry resolve above + the Phase 3 per-subset seam).
    MmeApplyRecordStateBlock(record, device);
    record->setDrawTypeIndex(record->snapshot().draw_type_index);
    // [0x18005a3d4] sub_18005A020(record, a2, record+0x138) - the SAME step
    // -time semantic bind as MmeStepSceneRecord (the whole-object binding's
    // sub_18001B340 walk over the carrier's cached snapshot), running BEFORE
    // FUN_18001BAB0 - whose run-failed head gate below therefore fires only
    // after the bind, exactly the original's ordering.
    MmeBindStandardParameters(record, binding, record->snapshot());
    // [FUN_18001BAB0 0x18001bb01 `!effect || !tech || sas+0x39 -> return 0`]
    // the full-run entry checks the run-failed latch at its HEAD, before the
    // run state is constructed: a latched effect draws NOTHING here (no
    // walk, no restore - 05a2c0's prelude/re-sync/bind above have already
    // run, mirroring the original calling BAB0 after them).
    if (binding->sas->runFailed) {
        return;
    }
    // The original resolves the full-run technique through the same wrapper
    // key as the step/resume pair (sub_18005A2C0's a2 = ctx+0x168, passed by
    // FUN_18005da50's planA walk at 0x18005e170 - the (turn id, carrier)
    // binding record's techniques[0]; the record stores no per-turn
    // technique choice, so the walk is ALWAYS the binding's first
    // technique). The caller's MmeSceneWalkTechIndex gate already enforced
    // the (id, carrier) hit: a carrier not owning the turn's resource name
    // never reaches this body.
    const int techIndex = binding->sceneTechIndex;
    SasTechnique& tech = binding->sas->techniques[
        static_cast<size_t>(techIndex)];
    SasRunState* state = SasCreateRunState(device);
    size_t logBefore = binding->sas->log.size();
    // The walk host-self publication (same as the step/resume pair): the
    // full run's Draw=Buffer/Geometry slots and Clear commands resolve THIS
    // carrier (R3/R8 seam).
    ctx->sceneWalkCarrier = record;
    SasResumeTechnique(binding->sas, &tech, device, state);
    ctx->sceneWalkCarrier = nullptr;
    MmeFlushSasLogDelta(binding->sas, logBefore);
    SasDestroyRunState(state);
}

} // namespace

// Forward declarations (mme scope, matching the definitions further below):
// the sub_180067500-family target-set helpers [0x180067500 / 0x1800675E0 /
// 0x180067680], used earlier in this file by the FUN_18005b9e0 frame head /
// the FUN_18005c510 bookkeeping (the ctx+0x18 cached set).
void MmeReleaseTargetSet(MmeTargetSet& set);
void MmeSaveTargetSet(MmeTargetSet& set, IDirect3DDevice9* device,
                      unsigned int mask);
void MmeRestoreTargetSet(const MmeTargetSet& set, IDirect3DDevice9* device);

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
    // the Phase 2 manager bindings are owned by the manager map.  The
    // original sub_1800599A0 tail (0x180059a05..0x180059a16):
    //   sub_180059A30(*(this+856)); *(this+856) = 0;
    // i.e. destroy the run state parked at the slot and null it.  No device
    // restore is issued - the orphaned state's epilogue never ran in the
    // original either (a state destroyed here is one the post-walk resume
    // never reached; step re-creates fresh, so the next walk re-captures).
    // This is also the ownership anchor the MmeStepSceneRecord comment
    // documents ("the planner's viewport-change reset ... destroys it").
    if (model->runState() != nullptr) {
        SasDestroyRunState(model->runState());
        model->setRunState(nullptr);
    }
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
        scratch.renderOrder = MmeAbsInt(ExpGetPmdOrder(hostIndex));   // +0xec [L72286-72287]
        // [L72288-72297] +0xf8/+0x10c/+0x120/+0x134 = 1.0f, rest 0 = identity.
        memset(&model->planMatrix(), 0, sizeof(D3DMATRIX));
        model->planMatrix().m[0][0] = 1.0f;
        model->planMatrix().m[1][1] = 1.0f;
        model->planMatrix().m[2][2] = 1.0f;
        model->planMatrix().m[3][3] = 1.0f;
        scratch.flag = ExpGetPmdDisp(hostIndex) ? 1 : 0;         // +0xf4 [L72298]
    } else {
        // [L72300-72335] accessory: |ExpGetAcsOrder| + ExpGetAcsWorldMat.
        scratch.renderOrder = MmeAbsInt(ExpGetAcsOrder(hostIndex));
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

void MmeSortPlanObjects(MmeContext* ctx,
                        const std::map<int, ModelData*>& orderMap)
{
    // [0x18002baa0 + 0x18005fd30, re-verified 2026-09-15] TWO different
    // orderings feed the three lists:
    //
    // ctx+0xb8 / ctx+0xd8 (passPlanA/B) come from fcn_18005b9e0's own
    // UNIQUE-key |order| map (the insert at 0x18005bd34 is std::map::
    // operator[] = insert-or-OVERWRITE, so a duplicate |order| keeps only
    // the LAST host index - the accessory scan overwrites same-order PMD
    // entries) - exactly the |orderMap| walk below.
    //
    // manager+0x158 (orderedPlan, copied into ctx+0x148 by FUN_18005fd30's
    // straight vector assign) is built from DIFFERENT data:
    // sub_18002BAA0 first rebuilds the mgr+0xD8 object vector from a
    // std::set keyed (ModelData+0xEC = |order| scratch, ModelData*) - BOTH
    // equal-|order| objects survive, pointer ascending; its pending-record
    // drain (sub_18002C910 -> sub_18002CA80) walks that vector's
    // whole-object assignments and pushes ONE mgr+0x118 queue id PER
    // OFFSCREENRENDERTARGET entry of the binding record's +0x1B8 vector
    // (only semanticId 0x2E enters that vector - the parser sub_180011960
    // filters; see MmeCollectSceneOffscreens), gated v142 = a2 && *(object
    // +0xF4 disp); the tail loop materializes mgr+0x158 = the queue order
    // through the mgr+0xB8 id map (node+32 = the 0x48 wrapper: +0 the
    // entry's own 0x98 offscreen record, +48 the carrier, +56 the id) with
    // no sort. The port therefore queues ONE ITEM PER 0x2E RESOURCE
    // (aligned 2026-09-15; the earlier per-technique list queued one turn
    // for ray.fx's single technique and starved the other eight targets,
    // and the one-item-per-carrier list before that starved every technique
    // after the first); subset-effect carriers hold no whole-object 0x2E
    // entries and stay unqueued.
    EffectOwnerManager* manager = g_ownerManager;
    ctx->passPlanA.clear();
    ctx->passPlanB.clear();
    if (manager != nullptr) {
        manager->orderedPlan.clear();
    }

    for (std::map<int, ModelData*>::const_iterator it = orderMap.begin();
         it != orderMap.end(); ++it) {
        ModelData* model = it->second;
        if (model->renderClass() == 1) {
            ctx->passPlanA.push_back(model);            // [kit L303-361]
        } else if (model->renderClass() == 2) {
            ctx->passPlanB.push_back(model);            // [kit L363-422]
        }
    }

    // [sub_18002BAA0 phase 1] the (|order|, ModelData*) set contents, disp
    // gated by the same ExpGetPmdDisp/ExpGetAcsDisp scan that filled
    // orderMap (+0xf4 is set by MmeRefreshObjectPlan before this call).
    std::vector<ModelData*> carriers;
    for (size_t i = 0; i < ctx->models.size(); ++i) {
        ModelData* model = ctx->models[i];
        if (model == nullptr || model->passPlanScratch().flag == 0) {
            continue;                                   // +0xf4 disp gate
        }
        if (model->renderClass() == 0) {
            continue;   // object-class: no whole-object 0x2E resources to
                        // queue (the original's per-subset drain reaches
                        // these carriers too, but a material-only effect
                        // never declares OFFSCREENRENDERTARGET in practice)
        }
        carriers.push_back(model);
    }
    std::sort(carriers.begin(), carriers.end(),
              [](const ModelData* a, const ModelData* b) {
                  const int oa = a->passPlanScratch().renderOrder;   // +0xec
                  const int ob = b->passPlanScratch().renderOrder;
                  if (oa != ob) {
                      return oa < ob;
                  }
                  return reinterpret_cast<uintptr_t>(a) <
                         reinterpret_cast<uintptr_t>(b);
              });
    if (manager != nullptr) {
        // [sub_18002CA80's binding+0x1B8 walk, 0x18002d1a0-0x18002d69c] one
        // queue item per OFFSCREENRENDERTARGET resource of each carrier, in
        // the multi-set order, each carrier's resources in parameter
        // declaration order. The turn id comes from the manager-wide
        // name->id map (sub_180032FB0(mgr+0x30)+56, ids from sub_18002D820):
        // a resource NAME seen at an earlier carrier resolves to the SAME
        // id, so cross-effect/cross-carrier same-name targets share ONE
        // render turn (the RayMMD shared-target dependency). The port keys
        // the plan by the name itself; the first carrier declaring the name
        // supplies the queue item's resource pointer (the original's
        // wrapper+0 holds the first creator's 0x98 record for a shared id).
        // Render class plays no filtering role in the original (preprocess
        // and scene carriers queue identically); an effect with no 0x2E
        // resource queues NOTHING - its repeat stays at the base turn, the
        // post-effect whole running inside the base turn's step/resume.
        std::map<std::string, bool> queuedNames;   // name -> already queued
        std::vector<SasResource*> scratch;
        manager->orderedPlan.reserve(carriers.size());
        for (size_t i = 0; i < carriers.size(); ++i) {
            ModelData* model = carriers[i];
            MmeCollectSceneOffscreens(model, &scratch);
            for (size_t r = 0; r < scratch.size(); ++r) {
                SasResource* res = scratch[r];
                if (queuedNames.find(res->name) != queuedNames.end()) {
                    continue;   // same name: the earlier carrier's turn
                }
                queuedNames[res->name] = true;
                manager->orderedPlan.push_back(MmeRenderPassItem(model, res));
            }
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
    ctx->modelsByName.Clear();

    // [L123-138, 0x18005bb7d-0x18005bbbd] drop the ctx+0x18 cached target
    // set: the mask dword goes 0 first, then the 4 color refs (ctx+0x20..
    // +0x38) and the depth ref (ctx+0x40) are released, and ctx+0x168 (the
    // current binding object) is nulled. The cached set stays empty until
    // FUN_18005c510's repeat == N branch re-captures it.
    MmeReleaseTargetSet(ctx->cachedTargetSet);
    ctx->currentBindingObject = nullptr;        // puVar5[0x2d] = 0 [L138]
    ctx->currentBindingOffscreen = nullptr;     // the wrapper's whole identity
                                               // dies with the plan rebuild
                                               // (the fresh list below
                                               // re-publishes both)

    // [L153-205] the PMD scan.
    int pmdNum = ExpGetPmdNum();                // [L153]
    int acsNum = ExpGetAcsNum();                // [L154]
    // [fcn_18005b9e0] the scratch |order| map is a UNIQUE-key tree: the
    // original's insert is a lower_bound walk (0x18005bce0) + insert only
    // when the key is absent (0x18005bcff: "jnb loc_18005BD34" skips the
    // insert on an equal key) + an UNCONDITIONAL value store (0x18005bd34:
    // mov [node+0x20], model) = std::map::operator[]. A duplicate |order|
    // therefore OVERWRITES the earlier entry: within a scan the later host
    // index wins, and the accessory scan overwrites same-|order| PMD entries.
    // (std::map::insert would keep the first; std::multimap would keep both
    // - neither matches the binary.)
    std::map<int, ModelData*> orderMap;        // [L140-152] scratch order map
    for (int i = 0; i < pmdNum; ++i) {
        unsigned long long id =
            reinterpret_cast<unsigned long long>(ExpGetPmdID(i));      // [L160]
        if (ctx->modelRegistry.find(id) != ctx->modelRegistry.end()) { // [L162 FUN_180061470]
            ModelData* model = MmeFindOrCreateModelEntry(id);          // [L164]
            if (model != nullptr) {
                MmeRefreshObjectPlan(model, i);                        // [L166 FUN_180059aa0]
                // Name registry includes hidden objects; duplicate order keeps first.
                ctx->modelsByName.Add(model->name(), model->passPlanScratch().renderOrder, model);
                if (ExpGetPmdDisp(i)) {                                // [L172]
                    int order = MmeAbsInt(ExpGetPmdOrder(i));          // [L174-175]
                    orderMap[order] = model;  // insert-or-OVERWRITE [L190-197, 0x18005bd34]
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
                ctx->modelsByName.Add(model->name(), model->passPlanScratch().renderOrder, model);
                if (ExpGetAcsDisp(i)) {                                // [L222]
                    int order = MmeAbsInt(ExpGetAcsOrder(i));          // [L224-225]
                    orderMap[order] = model;  // insert-or-OVERWRITE [L240-247, 0x18005be6e]
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
    MmeSortPlanObjects(ctx, orderMap);

    // [L449-484; 0x18005c38a] the viewport-change detection: GetViewport
    // (vtable+0x180) vs the cached ctx+0x170 viewport (the ctor zeroes it, so
    // the first rebuild always takes the changed branch); on change: destroy
    // the ctx+0x190 background-fixup map (per-node subtree destroy
    // sub_180003310 + COM Release + node free, then the ctx+0x198 sentinel /
    // ctx+0x1A0 size reset), store the viewport and set ctx+0x18A = 1. The
    // cached effects' SAS offscreen resources keep their parse-time size -
    // the original refreshes them only on a device reset (FUN_18002de40).
    IDirect3DDevice9* device = ctx->device;
    if (device != nullptr) {
        D3DVIEWPORT9 viewport;
        memset(&viewport, 0, sizeof(viewport));
        device->GetViewport(&viewport);                                // slot 0x180 [L449]
        if (memcmp(&ctx->lastPlanViewport, &viewport, sizeof(viewport)) != 0) {
            // Every cached RHW quad was scaled against the old viewport -
            // release the complete map (one entry per source stream vertex
            // buffer).
            for (std::map<IDirect3DVertexBuffer9*, IDirect3DVertexBuffer9*>::iterator it =
                     ctx->backgroundFixedVbs.begin();
                 it != ctx->backgroundFixedVbs.end(); ++it) {
                if (it->second != nullptr) {
                    it->second->Release();
                }
            }
            ctx->backgroundFixedVbs.clear();
            ctx->lastPlanViewport = viewport;                          // [L478-483]
            ctx->backgroundQuadViewportDirty = 1;                      // [L484] ctx+0x18A = 1
        }
    }

    // [L486-492] the repeat tail [fcn_18005b9e0 L628-636]: empty pass list
    // (or effects disabled) -> ctx+0x14 = 1, no host call; otherwise
    // ctx+0x14 = N+1 and ExpSetRenderRepeatCount(N+1) where N =
    // |renderPassList| - ONE entry per queued 0x2E RESOURCE, names deduped
    // across carriers (the materialized wrappers; x86 sub_10053A20
    // 0x100541c6-0x100541d5: (end-begin)/4 + 1 over the id vector). The
    // porter's earlier "+postEffectCount" extra was a guess - the original
    // counts the ordered plan only.
    if (ctx->renderPassList.empty() || ctx->effectEnabled == 0) {
        ctx->lastRepeatCount = 1;
    } else {
        ctx->lastRepeatCount = static_cast<int>(ctx->renderPassList.size()) + 1;
        ExpSetRenderRepeatCount(ctx->lastRepeatCount);                 // [L491]
    }
    // (No plan-composition log here: FUN_18005b9e0 writes none - the binary
    // carries no "Plan:"/"passPlan" strings at all.)

    // [PHASE3 wiring] per-frame CONTROLOBJECT resolution (the original's
    // EffectFrameParamSetter walk): every loaded effect's control parameters
    // read the live object/bone/morph/panel values. Runs after the object
    // scan so the name tables and host indexes are current.
    SasEnsureHostCallbacksInstalled();
    MmeUpdateControlObjects();

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

    // [L74184-74189, 0x18005c58d-0x18005c5ba] the container/flag resets: the
    // clear-color/depth maps (ctx+0x1e0 / ctx+0x210) are CLEARED here every
    // call (sub_180060EC0 x2 - the hash-map node free); the repeat == 0 tail
    // below restages the host color/z, and the FUN_18005c970 apply's
    // sub_18005e4f0/sub_18005e570 re-stage the record's values. Without this
    // clear, a previous record's staged values would leak into the next
    // turn's snapshot validation. Then ctx+0x10 = 0, ctx+0x08 = 0,
    // ctx+0x60 = 0, ctx+0x11/0x12 = 0.
    ctx->clearColorMap.clear();
    ctx->clearDepthMap.clear();
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
        // [0x18005c5fb-0x18005c6aa] the ctx+0x18 cached target set: release
        // the old slots, mask = 31, refetch GetRenderTarget(0..3) /
        // GetDepthStencilSurface and GetViewport into the set (the set the
        // FUN_18005d130 repeat-0 boundary and FUN_18005e210's postClear
        // branch re-apply).
        if (device != nullptr) {
            MmeSaveTargetSet(ctx->cachedTargetSet, device, 31);
        } else {
            MmeReleaseTargetSet(ctx->cachedTargetSet);
        }
        // [0x18005c98e-0x18005ca33, the v18/v19 block] the mgr's PERSISTENT
        // target set (mgr+0x08..0x38): release the old slots, mask = 31, and
        // refetch the CURRENT device targets (GetRenderTarget(0..3) into
        // mgr+0x10..0x2F, GetDepthStencilSurface into mgr+0x30, GetViewport
        // into mgr+0x38). The ==N boundary is the frame's LAST turn
        // boundary, so this captures the snapshot surfaces the previous turn
        // rendered onto - the pool the sub_180002100/sub_180002440
        // persistent fast paths borrow from until the set is dropped.
        if (device != nullptr) {
            MmeSaveTargetSet(ctx->persistentTargetSet, device, 31);
        } else {
            MmeReleaseTargetSet(ctx->persistentTargetSet);
        }
        // [0x18005c78e-0x18005c7c9] the mgr's saved main set (mgr+0x50, the
        // set the sub_180001880 snapshot refetches and sub_180001e70
        // consumes) is dropped here: mask = 0 + release every ref.
        MmeReleaseTargetSet(ctx->mainTargetSet);
        // [L74286, 0x18005c7d8] the WORD write at ctx+0x438 (0x438 <- 0,
        // 0x439 <- 1): the persistent-snapshot marker that gates e210's
        // Clear(0,0,7) goes down, and the persistent-layer gate goes up for
        // the sub_180002100/sub_180002440 fast paths of the next turn.
        // ctx+0x188 (the snapshot success flag) is NOT touched here.
        ctx->persistentClearUsed = 0;
        ctx->persistentLayerEligible = 1;
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
        // [FUN_180001320 head, 0x180001335-0x180001346] the per-OnEndScene
        // cleanup: sub_180067680 x2 unconditionally drops the mgr's persistent
        // set (mgr+0x08) and saved main set (mgr+0x50). The original runs it
        // at the frame tail (OnEndScene -> e210 -> 18000132); the port runs
        // it here at the repeat-0 boundary of the NEXT frame - the last
        // persistent consumer of the previous frame is the OnEndScene e210's
        // sub_18005cac0 snapshot, and every sub_180002100/sub_180002440
        // query of this frame happens after this point, so the fast path
        // observes an empty persistent pool exactly like the original. The
        // mgr+0x50 drop is the defensive tail (e70 and the 180001880 failure
        // path already release it).
        MmeReleaseTargetSet(ctx->persistentTargetSet);
        MmeReleaseTargetSet(ctx->mainTargetSet);
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
// [0x180067500 / 0x1800675E0] FUN_180067500 / FUN_1800675E0: save / rebind a
// device target set ({mask, targets[4], depth, viewport}; mask bit 0 =
// depth, bits 1..4 = color targets 0..3 - the redirectedMask layout).
// ---------------------------------------------------------------------------

void MmeReleaseTargetSet(MmeTargetSet& set)
{
    for (int i = 0; i < 4; ++i) {
        if (set.targets[i] != nullptr) {
            set.targets[i]->Release();
            set.targets[i] = nullptr;
        }
    }
    if (set.depth != nullptr) {
        set.depth->Release();
        set.depth = nullptr;
    }
    set.mask = 0;
}

// [0x180067500] release the previous contents, store the mask,
// GetRenderTarget(i) for bits 1..4, GetDepthStencilSurface for bit 0,
// GetViewport unconditionally.
void MmeSaveTargetSet(MmeTargetSet& set, IDirect3DDevice9* device,
                      unsigned int mask)
{
    MmeReleaseTargetSet(set);
    set.mask = mask;
    if (mask != 0) {
        for (int i = 0; i < 4; ++i) {
            if ((mask & (2u << i)) != 0) {
                device->GetRenderTarget(static_cast<UINT>(i), &set.targets[i]);
            }
        }
        if ((mask & 1u) != 0) {
            device->GetDepthStencilSurface(&set.depth);
        }
    }
    device->GetViewport(&set.viewport);
}

// [0x1800675E0] SetRenderTarget(i) for mask bits 1..4,
// SetDepthStencilSurface for bit 0, SetViewport always.
void MmeRestoreTargetSet(const MmeTargetSet& set,
                         IDirect3DDevice9* device)
{
    for (int i = 0; i < 4; ++i) {
        if ((set.mask & (2u << i)) != 0) {
            device->SetRenderTarget(static_cast<UINT>(i), set.targets[i]);
        }
    }
    if ((set.mask & 1u) != 0) {
        device->SetDepthStencilSurface(set.depth);
    }
    device->SetViewport(&set.viewport);
}

// ---------------------------------------------------------------------------
// [FUN_18005da50 L75190-75229 == FUN_18005cac0 staged validation] every
// present color target of the just-saved set must carry one staged clear
// color in ctx+0x1E0 (clearColorMap; RT0 must be present) and the depth
// target a staged z in ctx+0x210 (clearDepthMap). The color starts as the
// host GetClearColor() and is overridden by RT0's staged value.
// ---------------------------------------------------------------------------

static void MmeValidateStagedClear(MmeContext* ctx,
                                   IDirect3DSurface9* const* targets,
                                   IDirect3DSurface9* depth, bool* colorsValid,
                                   D3DCOLOR* color, bool* depthStaged,
                                   float* clearDepth)
{
    *colorsValid = true;
    *color = GetClearColor();                                   // [L75192]
    *depthStaged = true;
    *clearDepth = 1.0f;                                         // [L75193]
    for (int i = 0; i < 4; ++i) {
        IDirect3DSurface9* target = targets[i];
        if (target == nullptr) {
            if (i == 0) {
                *colorsValid = false;   // RT0 missing -> no staged clear
            }
            continue;                   // absent slots 1..3 are skipped
        }
        std::map<IDirect3DSurface9*, D3DCOLOR>::const_iterator it =
            ctx->clearColorMap.find(target);                    // [L75198 find]
        if (it == ctx->clearColorMap.end()) {
            *colorsValid = false;
            break;
        }
        if (i == 0) {
            *color = it->second;                                // [L75209]
        } else if (*color != it->second) {
            *colorsValid = false;                               // [L75213]
            break;
        }
    }
    if (depth != nullptr) {
        std::map<IDirect3DSurface9*, float>::const_iterator it =
            ctx->clearDepthMap.find(depth);                     // [L75224 find]
        if (it != ctx->clearDepthMap.end()) {
            *clearDepth = it->second;
        } else {
            *depthStaged = false;
        }
    } else {
        *depthStaged = false;                                   // [L75226]
    }
}

// ---------------------------------------------------------------------------
// [0x18005e4f0 / 0x18005e570 / 0x18005e5d0] MmeStageClearColor /
// MmeStageClearDepth / MmeEraseStagedClearColor now live in mme_context.cpp
// (declared in mme_context.h): the SAS Clear command's runtime registration
// (MmeStageSceneClearRuntime, sas_exec.cpp) and the pass-slot erase below
// share the exact original implementations with this file's record apply.
// FUN_18005c970's tail calls the two register helpers after the record
// rebind so the FUN_18005da50 / FUN_18005cac0 validation finds the record's
// staged annotation values at the next first draw.
// ---------------------------------------------------------------------------

// ---------------------------------------------------------------------------
// [FUN_18005da50 L75237-75346 == FUN_18005cac0 failure block] the shared
// "DirectX Error: <desc> [%08X]\n" log line, the localized
// "Failed to process post effect:\n" prefix and the MessageBoxA(hWndParent,
// ..., "MikuMikuEffect", MB_ICONERROR). (The shown-message dedup walk over
// the byte_1800D99D8 list is a Phase 3 seam.)
// ---------------------------------------------------------------------------

static void MmeReportPostEffectFailure(unsigned long hr)
{
    const char* desc = MmeDxErrDescription(hr);
    char hex[16];
    sprintf_s(hex, sizeof(hex), "%08X", static_cast<unsigned int>(hr));
    std::string errorLine = "DirectX Error: ";
    errorLine += (desc != nullptr ? desc : "");
    errorLine += " [";
    errorLine += hex;
    errorLine += "]\n";
    MmeLogWrite(errorLine.c_str(), 0);                          // [L75294]
    std::string message;
    if (MmeIsEnglishUiMode()) {
        message = "Failed to process post effect:\n";           // 0x1800b5988
    } else {
        // 0x1800b5958: GBK bytes of the localized release.
        message += "\xBA\xF3\xC6\xDA\xCC\xD8\xD0\xA7\xB4\xA6\xC0\xED"
                   "\xD6\xD0\xB7\xA2\xC9\xFA\xB4\xED\xCE\xF3\x3A";
    }
    message += errorLine;
    MessageBoxA(g_mainWindow, message.c_str(), "MikuMikuEffect",
                MB_ICONERROR);                                  // [L75346]
}

// ---------------------------------------------------------------------------
// [0x180002100 / 0x180002440] sub_180002100 / sub_180002440: the cached
// per-slot offscreen snapshot surfaces. Created with CreateRenderTarget /
// CreateDepthStencilSurface (device slots 0x1c0 / 0x1c8 - NOT offscreen
// plain surfaces, they are re-bound as targets by the snapshot below;
// multisample sampled via GetDesc from the PERSISTENT set's surfaces
// [0x18000230c GetDesc(mgr+0x10) / 0x18000265c GetDesc(mgr+0x30)], not the
// saved mains - the persistent pool holds the surfaces the frame tail
// captured, so a freshly created snapshot inherits the main chain's
// multisample even while an offscreen target is bound). Requested size = the saved main-set viewport W/H at mgr+0x80+8/+0xC (the
// "mgr+0x88/0x8C" pair sub_180001880 refetches at its step 1 - written only
// by that GetViewport, no separate writer); format from each saved target's
// GetDesc. Cache key, selected by mgr+0x1FA (== ctx+0x43A): the A family
// (mgr+0x138 per-slot map, flag 0) is keyed by the EXACT (w, h, fmt) triple
// - a resized request misses and creates a fresh node; the B family
// (mgr+0x98 per-slot map, flag 1) is keyed by fmt and reuses the cached
// surface while GetDesc says it is big enough, releasing it when too small.
// The port keeps one surface per slot: on an A-family key mismatch the
// displaced surface is released instead of staying in the map (documented
// divergence from the multi-node A-family map). Failure stores the HRESULT
// at mgr+0x1FC (ctx->snapshotError) and returns null.
//
// [0x180002237-0x180002253 / 0x180002590-0x1800025a8, B-branch only] the
// PERSISTENT fast path: after the family-B cache misses, the mgr+0x1F9 gate
// (persistentLayerEligible - set by FUN_18005c510's ==N WORD write, cleared
// by FUN_18005da50's staged-miss check) opens the manager's persistent
// target set (mgr+0x08..0x38, filled by the same c510 ==N branch with the
// CURRENT device targets of the frame's last turn boundary). sub_180002780
// validates the slot's persistent surface (mgr+0x10+8*slot / mgr+0x30)
// against the request: w <= desc.Width && h <= desc.Height &&
// (!fmt || fmt == desc.Format). On a hit the persistent surface is returned
// as the snapshot (it IS the current device target - the snapshot main
// renders directly on it, no StretchRect copy happens) and mgr+0x1F8 goes
// up (persistentClearUsed), arming FUN_18005e210's Clear(0,0,7). The
// family-A branch (flag 0) has no persistent lookup; the persistent hit is
// NOT entered into the mgr+0x1D8 active set (the cache reuse paths are).
// ---------------------------------------------------------------------------

static bool MmePersistentSurfaceValid(IDirect3DSurface9* surface,
                                      unsigned int width, unsigned int height,
                                      D3DFORMAT format)
{
    // [0x180002780] sub_180002780: null -> false; GetDesc then
    // w <= desc.Width && h <= desc.Height && (!fmt || fmt == desc.Format).
    if (surface == nullptr) {
        return false;
    }
    D3DSURFACE_DESC desc;
    memset(&desc, 0, sizeof(desc));
    if (FAILED(surface->GetDesc(&desc))) {
        return false;    // the original checks the desc fields of an
                         // unzeroed stack on GetDesc failure; the port
                         // treats a failed probe as a miss (observably
                         // equivalent: garbage sizes rarely pass)
    }
    return width <= desc.Width && height <= desc.Height &&
           (format == 0 || format == desc.Format);
}

static IDirect3DSurface9* MmeGetSnapshotSurface(MmeContext* ctx,
                                                IDirect3DDevice9* device,
                                                int slot, D3DFORMAT format,
                                                bool familySpecial)
{
    const D3DVIEWPORT9& vp = ctx->mainTargetSet.viewport;
    IDirect3DSurface9* cached = ctx->snapshotSurfaces[slot];
    if (cached != nullptr) {
        D3DSURFACE_DESC desc;
        memset(&desc, 0, sizeof(desc));
        bool reusable = SUCCEEDED(cached->GetDesc(&desc)) && desc.Format == format &&
                        (familySpecial
                             ? (desc.Width >= vp.Width && desc.Height >= vp.Height)
                             : (desc.Width == vp.Width && desc.Height == vp.Height));
        if (reusable) {
            return cached;
        }
        cached->Release();
        ctx->snapshotSurfaces[slot] = nullptr;
    }
    // [0x180002237] family-B only: the persistent fast path.
    if (familySpecial && ctx->persistentLayerEligible != 0) {
        IDirect3DSurface9* persistent = ctx->persistentTargetSet.targets[slot];
        if (MmePersistentSurfaceValid(persistent, vp.Width, vp.Height, format)) {
            ctx->persistentClearUsed = 1;   // mgr+0x1F8
            return persistent;              // mgr+0x10+8*slot
        }
    }
    // [0x18000230c] the multisample pair: GetDesc on the PERSISTENT set's
    // slot-0 surface (mgr+0x10). The persistent set can be empty at this
    // point in the port (the repeat-0 bookkeeping drops it); the original
    // would dereference null there, the port falls back to the saved main
    // RT0 (documented divergence).
    D3DSURFACE_DESC mainDesc;
    memset(&mainDesc, 0, sizeof(mainDesc));
    IDirect3DSurface9* multisampleSource = ctx->persistentTargetSet.targets[0];
    if (multisampleSource == nullptr) {
        multisampleSource = ctx->mainTargetSet.targets[0];
    }
    if (multisampleSource != nullptr) {
        multisampleSource->GetDesc(&mainDesc);
    }
    IDirect3DSurface9* surface = nullptr;
    HRESULT hr = device->CreateRenderTarget(
        vp.Width != 0 ? vp.Width : 1, vp.Height != 0 ? vp.Height : 1, format,
        mainDesc.MultiSampleType, mainDesc.MultiSampleQuality, FALSE,
        &surface, nullptr);
    if (FAILED(hr) || surface == nullptr) {
        ctx->snapshotError = static_cast<unsigned long>(hr);   // mgr+0x1FC
        if (surface != nullptr) {
            surface->Release();
        }
        return nullptr;
    }
    ctx->snapshotSurfaces[slot] = surface;   // the cache owns the reference
    return surface;
}

static IDirect3DSurface9* MmeGetSnapshotDepth(MmeContext* ctx,
                                              IDirect3DDevice9* device,
                                              D3DFORMAT format, bool familySpecial)
{
    const D3DVIEWPORT9& vp = ctx->mainTargetSet.viewport;
    IDirect3DSurface9* cached = ctx->snapshotDepth;
    if (cached != nullptr) {
        D3DSURFACE_DESC desc;
        memset(&desc, 0, sizeof(desc));
        bool reusable = SUCCEEDED(cached->GetDesc(&desc)) && desc.Format == format &&
                        (familySpecial
                             ? (desc.Width >= vp.Width && desc.Height >= vp.Height)
                             : (desc.Width == vp.Width && desc.Height == vp.Height));
        if (reusable) {
            return cached;
        }
        cached->Release();
        ctx->snapshotDepth = nullptr;
    }
    // [0x180002590] family-B only: the persistent fast path (mgr+0x30).
    if (familySpecial && ctx->persistentLayerEligible != 0) {
        IDirect3DSurface9* persistent = ctx->persistentTargetSet.depth;
        if (MmePersistentSurfaceValid(persistent, vp.Width, vp.Height, format)) {
            ctx->persistentClearUsed = 1;   // mgr+0x1F8
            return persistent;              // mgr+0x30
        }
    }
    // [0x18000265c] the multisample pair: GetDesc on the PERSISTENT set's
    // depth surface (mgr+0x30) - see MmeGetSnapshotSurface; fall back to the
    // saved main RT0 when the persistent set is empty.
    D3DSURFACE_DESC mainDesc;
    memset(&mainDesc, 0, sizeof(mainDesc));
    IDirect3DSurface9* multisampleSource = ctx->persistentTargetSet.depth;
    if (multisampleSource == nullptr) {
        multisampleSource = ctx->mainTargetSet.targets[0];
    }
    if (multisampleSource != nullptr) {
        multisampleSource->GetDesc(&mainDesc);
    }
    IDirect3DSurface9* surface = nullptr;
    HRESULT hr = device->CreateDepthStencilSurface(
        vp.Width != 0 ? vp.Width : 1, vp.Height != 0 ? vp.Height : 1, format,
        mainDesc.MultiSampleType, mainDesc.MultiSampleQuality, FALSE,
        &surface, nullptr);
    if (FAILED(hr) || surface == nullptr) {
        ctx->snapshotError = static_cast<unsigned long>(hr);   // mgr+0x1FC
        if (surface != nullptr) {
            surface->Release();
        }
        return nullptr;
    }
    ctx->snapshotDepth = surface;           // the cache owns the reference
    return surface;
}

// ---------------------------------------------------------------------------
// [0x180001880] sub_180001880(mgr = ctx+0x240, clearColorsValid,
// depthStaged, clearColor, clearDepth) -> HRESULT: the turn-boundary
// main-RT snapshot. Choreography (verified against the binary):
//   1. drop the INACTIVE per-slot snapshot cache family (mgr+0x1FA ==
//      ctx+0x43A, c510's all-planB-special flag, selects the family);
//   2. refetch the manager's saved set (release + GetRenderTarget(0..3) +
//      GetDepthStencilSurface + GetViewport, mask 31) into mgr+0x50;
//   3. fetch a working set (v49/v50/v51, mask 31) and replace every slot
//      tracked by the manager with an offscreen snapshot surface
//      (sub_180002100 / sub_180002440 caches - which may answer from the
//      PERSISTENT set instead: a hit hands back the current device target
//      itself, and the `workTargets[i] != snapshot` guard below leaves the
//      working set untouched, so no copy or rebind happens for that slot);
//   4. on any create failure: rebind the saved set (sub_1800675E0), drop
//      the manager refs + both cache families (sub_180001660), return the
//      stored HRESULT;
//   5. color-clear decision: staged-valid -> clear; otherwise PRESERVE the
//      content by StretchRect(main RT -> snapshot, whole surface,
//      D3DTEXF_NONE) per slot, and only the FIRST FAILED copy turns the
//      color Clear on (0x180001cd4 probe loop - the copy IS the snapshot);
//   6. depth-clear decision: staged -> clear; mgr+0x1FA -> clear without a
//      probe; otherwise probe StretchRect(mainDepth -> snapshotDepth)
//      (D3D9 cannot stretch depth-stencil, so the probe fails - and the
//      depth Clear runs - exactly like the original);
//   7. rebind the snapshots as the device targets (SetRenderTarget per
//      mask bit, SetDepthStencilSurface, SetViewport) - the new turn
//      renders onto the snapshot copies while the saved mains keep the
//      previous content;
//   8. Clear(0, 0, (color?1:0)|(depth?6:0), staged color/z, 0) lands on
//      the just-bound snapshots; return S_OK.
// ---------------------------------------------------------------------------

HRESULT MmeSnapshotMainTargets(MmeContext* ctx, bool clearColorsValid,
                               bool depthStaged, D3DCOLOR clearColor,
                               float clearDepth)
{
    IDirect3DDevice9* device = (ctx != nullptr) ? ctx->device : nullptr;
    if (device == nullptr) {
        return E_POINTER;
    }
    MmeTargetSet& mgr = ctx->mainTargetSet;

    // [prologue, sub_180002C60/sub_180003170 x4+1] clear the inactive cache
    // family. The port keeps one family; it is dropped when the family flag
    // changed since the cache was built. The mgr+0x08 persistent set and the
    // mgr+0x50 saved set are NOT dropped here (the original's prologue never
    // touches them).
    const bool familySpecial = ctx->allObjectsSpecialFlag != 0;
    if (ctx->snapshotFamilySpecial != familySpecial) {
        ctx->ReleaseSnapshotSurfaceCache();
        ctx->snapshotFamilySpecial = familySpecial;
    }

    // [step 1] the manager's saved set, mask 31.
    MmeSaveTargetSet(mgr, device, 31);

    // [step 2] the working set (v49/v50/v51). workOwnsTarget[i] tracks the
    // GetRenderTarget references the snapshot replaces.
    IDirect3DSurface9* workTargets[4] = {nullptr, nullptr, nullptr, nullptr};
    IDirect3DSurface9* workDepth = nullptr;
    bool workOwnsTarget[4] = {false, false, false, false};
    bool workOwnsDepth = false;
    D3DVIEWPORT9 workViewport;
    memset(&workViewport, 0, sizeof(workViewport));
    const unsigned int workMask = 31;
    for (int i = 0; i < 4; ++i) {
        device->GetRenderTarget(static_cast<UINT>(i), &workTargets[i]);
        workOwnsTarget[i] = workTargets[i] != nullptr;
    }
    device->GetDepthStencilSurface(&workDepth);
    workOwnsDepth = workDepth != nullptr;
    device->GetViewport(&workViewport);

    // [step 3] per-slot snapshots; the tracked slots replace the fetched
    // current targets in the working set.
    bool haveError = false;
    for (int i = 0; i < 4; ++i) {
        IDirect3DSurface9* savedTarget = mgr.targets[i];
        if (savedTarget == nullptr) {
            continue;
        }
        D3DSURFACE_DESC desc;
        memset(&desc, 0, sizeof(desc));
        savedTarget->GetDesc(&desc);                            // vtable+0x60
        IDirect3DSurface9* snapshot =
            MmeGetSnapshotSurface(ctx, device, i, desc.Format, familySpecial);
        if (snapshot == nullptr) {
            if (!haveError) {
                haveError = true;   // hr = mgr+0x1FC (stored by the creator)
            }
            continue;
        }
        if (workTargets[i] != snapshot) {
            if (workOwnsTarget[i] && workTargets[i] != nullptr) {
                workTargets[i]->Release();
            }
            workTargets[i] = snapshot;      // borrowed from the cache
            workOwnsTarget[i] = false;
        }
    }
    if (mgr.depth != nullptr) {
        D3DSURFACE_DESC desc;
        memset(&desc, 0, sizeof(desc));
        mgr.depth->GetDesc(&desc);
        IDirect3DSurface9* snapshot =
            MmeGetSnapshotDepth(ctx, device, desc.Format, familySpecial);
        if (snapshot == nullptr) {
            if (!haveError) {
                haveError = true;
            }
        } else if (workDepth != snapshot) {
            if (workOwnsDepth && workDepth != nullptr) {
                workDepth->Release();
            }
            workDepth = snapshot;          // borrowed from the cache
            workOwnsDepth = false;
        }
    }

    if (haveError) {
        // [LABEL_59] restore the pre-snapshot bindings (sub_1800675E0 on the
        // mgr+0x50 set), drop the manager refs and the caches
        // (sub_180001660), return the stored HRESULT.
        if (mgr.mask != 0) {
            MmeRestoreTargetSet(mgr, device);
        }
        mgr.mask = 0;
        for (int i = 0; i < 4; ++i) {
            if (mgr.targets[i] != nullptr) {
                mgr.targets[i]->Release();
                mgr.targets[i] = nullptr;
            }
        }
        if (mgr.depth != nullptr) {
            mgr.depth->Release();
            mgr.depth = nullptr;
        }
        ctx->ReleaseMainSnapshotCache();
        for (int i = 0; i < 4; ++i) {
            if (workOwnsTarget[i] && workTargets[i] != nullptr) {
                workTargets[i]->Release();
            }
        }
        if (workOwnsDepth && workDepth != nullptr) {
            workDepth->Release();
        }
        return static_cast<HRESULT>(ctx->snapshotError);
    }

    // [step 5, 0x180001cc6-d11] the color-Clear decision. The loop IS the
    // snapshot: each main RT's content is StretchRect-copied into its
    // offscreen copy; the first FAILED copy (content not preservable) turns
    // the Clear on. [0x180001c9c-0x180001cc2] the copy's DEST rect is
    // {0, 0, W, H} (the saved-set viewport W/H at mgr+0x88/+0x8C) when the
    // mgr+0x1FA family flag is set - the big-enough family-B snapshot may be
    // larger than the viewport, so the copy must land on the sub-rect - and
    // whole-surface (null) otherwise (exact-triple family A is viewport
    // sized). Source rect null, filter D3DTEXF_NONE.
    RECT copyRect;
    memset(&copyRect, 0, sizeof(copyRect));
    copyRect.right = static_cast<LONG>(mgr.viewport.Width);
    copyRect.bottom = static_cast<LONG>(mgr.viewport.Height);
    const RECT* copyDstRect = familySpecial ? &copyRect : nullptr;
    bool clearColorNow = clearColorsValid;                      // a2
    if (!clearColorNow) {
        for (int i = 0; i < 4; ++i) {
            if (workTargets[i] == nullptr) {
                continue;
            }
            if (FAILED(device->StretchRect(mgr.targets[i], nullptr,
                                           workTargets[i], copyDstRect,
                                           D3DTEXF_NONE))) {
                clearColorNow = true;
                break;
            }
        }
    }

    // [step 6, 0x180001d17-d64] the depth-Clear decision.
    bool clearDepthNow = false;
    if (workDepth != nullptr) {
        clearDepthNow = depthStaged;                            // a3
        if (!clearDepthNow) {
            if (familySpecial) {
                clearDepthNow = true;                           // [0x180001d31]
            } else if (mgr.depth != nullptr &&
                       FAILED(device->StretchRect(mgr.depth, nullptr,
                                                  workDepth, nullptr,
                                                  D3DTEXF_NONE))) {
                clearDepthNow = true;
            }
        }
    }

    // [step 7, 0x180001d64-dc6] rebind the snapshots as the device targets.
    if (workMask != 0) {
        for (int i = 0; i < 4; ++i) {
            if ((workMask & (2u << i)) != 0) {
                device->SetRenderTarget(static_cast<UINT>(i), workTargets[i]);
            }
        }
        if ((workMask & 1u) != 0) {
            device->SetDepthStencilSurface(workDepth);
        }
        device->SetViewport(&workViewport);
    }

    // [step 8, 0x180001dcc-e0e] the staged Clear lands on the snapshots.
    DWORD clearFlags = (clearColorNow ? D3DCLEAR_TARGET : 0u) |
                       (clearDepthNow ? (D3DCLEAR_ZBUFFER | D3DCLEAR_STENCIL)
                                      : 0u);
    if (clearFlags != 0) {
        device->Clear(0, nullptr, clearFlags, clearColor, clearDepth, 0);
    }

    for (int i = 0; i < 4; ++i) {
        if (workOwnsTarget[i] && workTargets[i] != nullptr) {
            workTargets[i]->Release();
        }
    }
    if (workOwnsDepth && workDepth != nullptr) {
        workDepth->Release();
    }
    return S_OK;
}

// ---------------------------------------------------------------------------
// [0x18005cac0] sub_18005cac0(record, device): the bound record's
// turn-boundary post-effect executor (FUN_18005da50's record branch, i.e.
// the first draw of a repeat whose currentBindingObject is set - turn > 0).
// Gate `!*record || !record+0x3c` (the c970-computed snapshot gate);
// EndScene -> save the current target set (mask 31) -> staged validation
// (clearColorMap / clearDepthMap) -> sub_180001880 snapshot -> on failure
// record+0x3c WORD = 0x100 (gate 0 + failure latch) + the error box ->
// BeginScene.
// ---------------------------------------------------------------------------

void MmeRunPostEffectRecord(MmeContext* ctx, ModelData* record,
                            int /*techIndex*/, IDirect3DDevice9* device)
{
    // The wrapper's inner record comes from the CURRENT turn's queue item
    // (ctx->currentBindingOffscreen); the legacy techIndex parameter stays
    // in the declaration (pass_planner.h is outside this change's file set)
    // and carries nothing.
    SasResource* offscreen =
        (ctx != nullptr) ? ctx->currentBindingOffscreen : nullptr;
    if (record == nullptr || device == nullptr) {
        return;                       // `if (!*a1) return`
    }
    // [gate, 0x18005cad1] `if (!*a1 || !record+0x3c) return;` - the wrapper's
    // inner offscreen record (the queue item's 0x2E resource - carried since
    // the 2026-09 re-audit, wrapper+0) AND the gate byte that apply
    // recomputes (wrapper+0x3C, kept on the resource). No run-state
    // condition: the original never consults +0x358 here (inside the DA50
    // walk the record's state was just re-created by the planB step walk
    // anyway, but a completed-not-suspended walk still runs this executor
    // while its gate holds).
    if (offscreen == nullptr || offscreen->passSnapshotGate == 0) {
        return;
    }
    device->EndScene();                                        // slot 0x150
    MmeTargetSet current;
    MmeSaveTargetSet(current, device, 31);                     // [FUN_180067500]
    bool colorsValid = false;
    bool depthStaged = false;
    D3DCOLOR color = 0;
    float clearDepth = 1.0f;
    MmeValidateStagedClear(ctx, current.targets, current.depth, &colorsValid,
                           &color, &depthStaged, &clearDepth);
    HRESULT hr = MmeSnapshotMainTargets(ctx, colorsValid, depthStaged, color,
                                        clearDepth);
    ctx->lastPostEffectHr = static_cast<unsigned long>(hr);    // the HR flow
    if (FAILED(hr)) {
        // [gate kill] record+0x3c WORD = 0x100: gate byte 0 (disable),
        // failure byte 1 (latched for the wrapper's lifetime - the port
        // keeps it on the 0x2E resource, so a sibling target's gate of the
        // same carrier survives).
        offscreen->passSnapshotGate = 0;
        offscreen->passSnapshotFailed = 1;
        MmeReportPostEffectFailure(hr);
    }
    device->BeginScene();                                      // slot 0x148
    MmeReleaseTargetSet(current);
}

// ---------------------------------------------------------------------------
// [0x18005c970] FUN_18005c970(record, device, adaptive): the repeat-boundary
// record apply (called by FUN_18005d130 at 0x18005d218 after the bookkeeping).
// Verified against the binary (instruction level, incl. the 2026-09 re-audit):
//   - a1 = the renderPassList wrapper (ctx+0x168 = renderPassList[repeat-1],
//     ONE PER QUEUED 0x2E RESOURCE); v4 = *a1 = the wrapper's inner record =
//     the 0x98 OFFSCREEN record of THAT resource (carried by the port's
//     queue item as SasResource* - sub_18002CA80 stored it at wrapper+0 at
//     queue-build time, 0x18002d415; inner+0 = the 0xC0 resource object
//     whose +8 surface feeds SetRenderTarget; inner+8 = the PAIRED depth
//     stencil; inner+0x18/+0x10 = the ClearColor annotation flag / packed
//     ARGB; inner+0x19/+0x14 = ClearDepth flag / float; inner+0x1A = the
//     "AntiAlias" annotation flag).
//   - the ONLY branch is `if (*a1)`: without an inner offscreen record the
//     WHOLE apply is a no-op - no gate write, no rebind, no viewport, no
//     Clear, no staging (the wrapper+0x3c gate byte keeps its previous
//     value, which nothing else ever sets - it stays 0 for such a binding).
//     There is NO suspended/staged precondition and NO "no-redirect early
//     exit"; FUN_18005d130 even runs FUN_18005c510 (whose per-model reset
//     destroys every +0x358 run states) BETWEEN the step and this apply, so
//     the original can never consult a live staged state here - wrapper+0
//     is sticky for the binding's lifetime.
//   - with an inner record the body ALWAYS runs: the gate
//     wrapper+0x3c = adaptive && inner+0x1A (AntiAlias) && !wrapper+0x3d,
//     SetRenderTarget(0, *(inner->resource+8)) (slot 0 ONLY),
//     UNCONDITIONAL SetDepthStencilSurface(inner+8), the viewport
//     {X=0, Y=0, W=wrapper+0x40, H=wrapper+0x44} on top of the CURRENT
//     GetViewport values (MinZ/MaxZ keep the live values), the
//     ANNOTATION-driven Clear (D3DCLEAR_ZBUFFER always; D3DCLEAR_TARGET when
//     the ClearColor annotation is present, with the +0x10 packed ARGB;
//     D3DCLEAR_STENCIL when the ClearDepth annotation is present, with the
//     +0x14 float) and the annotation color/z staging into the per-surface
//     maps (sub_18005e4f0/sub_18005e570), gated on the same flags. The gate
//     and its failure latch live on the wrapper (one per turn id); the port
//     keeps both on the queue item's SasResource.
// ---------------------------------------------------------------------------
static void MmeApplyPassRecord(MmeContext* ctx, ModelData* record,
                               SasResource* offscreen,
                               IDirect3DDevice9* device,
                               int adaptiveTessSet)
{
    if (record == nullptr || device == nullptr || offscreen == nullptr) {
        return;   // FUN_18005c970: `if (!*a1) return;`
    }
    // [FUN_18005c970 head, 0x18005c9b0] `v4 = *a1` - the wrapper's inner
    // offscreen record, carried by the queue item (a queue item exists only
    // because a 0x2E resource did, so the null case can only be a defensive
    // one): the whole apply is a no-op and the gate byte is NOT written (it
    // keeps its previous value, exactly like a wrapper whose +0 never held
    // an offscreen record).
    // [0x18005c9b6] the gate: wrapper+0x3c = adaptive (a3, the
    // GetRenderState(0xa1) probe) && inner+0x1A (the offscreen's "AntiAlias"
    // annotation flag) && !wrapper+0x3d (the failure latch). No staged or
    // suspended precondition.
    offscreen->passSnapshotGate =
        (adaptiveTessSet != 0 && offscreen->antiAlias &&
         offscreen->passSnapshotFailed == 0) ? 1 : 0;
    // [0x18005c9c9] SetRenderTarget(0, *(inner->resource + 8)) - the
    // offscreen's level-0 surface on render target slot 0 ONLY. The original
    // never rebinds slots 1..3 here: an MRT record's other slots keep
    // whatever the step's script commands left bound (the step re-walks the
    // technique from index 0 right before this apply, so the pre-scene
    // SetRenderTarget commands refreshed them).
    device->SetRenderTarget(0, offscreen->surface);
    // Offscreen DefaultEffect staging: the apply just re-bound the offscreen
    // RT0 for the upcoming turn's scene draws - publish (or clear) the
    // offscreen's rows for the binding resolution's unassigned-model
    // fallback.
    ctx->offscreenDefaultEffect =
        !offscreen->defaultEffectMap.empty()
            ? &offscreen->defaultEffectMap
            : nullptr;
    // [0x18005c9dc] SetDepthStencilSurface(inner + 8) - the offscreen
    // record's PAIRED depth stencil (offscreen+8, created unconditionally by
    // sub_1800143D0 case 46), UNCONDITIONAL here: it overrides whatever
    // RenderDepthStencilTarget the script staged for the suspension turn
    // (a null surface legitimately unbinds to the device default depth).
    device->SetDepthStencilSurface(offscreen->offscreenDepth);
    // [0x18005c9ee-0x18005ca17] GetViewport into a stack copy, overwrite
    // Width/Height with the wrapper's staged size (wrapper+0x40/+0x44) and
    // zero X/Y (one qword store); MinZ/MaxZ keep the CURRENT values the
    // GetViewport just fetched. [wrapper+0x40/+0x44 pinned 2026-09-15] the
    // two dwords are the offscreen resource surface's width/height, queried
    // ONCE at queue-build time (0x18002d426-0x18002d443: the 0x98 record's
    // 0xC0 resource object -> +8 surface -> vtable+0x60 descriptor query ->
    // the two dwords at desc+0x18/+0x1C stored as wrapper+0x40/+0x44). The
    // port re-derives them from the same surface's GetDesc at apply time -
    // the same numbers through the same source, and they track a device-
    // reset surface swap without a queue rebuild.
    D3DVIEWPORT9 viewport;
    memset(&viewport, 0, sizeof(viewport));
    device->GetViewport(&viewport);
    viewport.X = 0;
    viewport.Y = 0;
    if (offscreen->surface != nullptr) {
        D3DSURFACE_DESC desc = {};
        if (SUCCEEDED(offscreen->surface->GetDesc(&desc))) {
            viewport.Width = desc.Width;
            viewport.Height = desc.Height;
        }
    }
    device->SetViewport(&viewport);
    // [0x18005ca20-0x18005ca62] the ANNOTATION-driven frame-head clear:
    // flags = D3DCLEAR_ZBUFFER (always) | D3DCLEAR_TARGET when the
    // ClearColor annotation is present | D3DCLEAR_STENCIL when the
    // ClearDepth annotation is present, with the record's packed ARGB
    // (offscreen+0x10) and ClearDepth float (offscreen+0x14). Note the
    // Z clear runs even without a ClearDepth annotation - the original
    // reads the then-uninitialized +0x14 (a fresh MSVC small-heap page
    // reads 0.0f, the port's pinned default).
    DWORD clearFlags = D3DCLEAR_ZBUFFER;
    if (offscreen->hasClearColor) {
        clearFlags |= D3DCLEAR_TARGET;
    }
    if (offscreen->hasClearDepth) {
        clearFlags |= D3DCLEAR_STENCIL;
    }
    device->Clear(0, nullptr, clearFlags, offscreen->clearColorArgb,
                  offscreen->clearDepth, 0);
    // [0x18005ca6c-0x18005ca98] stage the OFFSCREEN's annotation color/z
    // into the per-surface maps for the CURRENT (just-rebound) targets
    // (sub_18005e4f0 / sub_18005e570) - the values the DA50
    // record-branch snapshot validates at the next first draw. Gated on
    // the same annotation flags.
    if (offscreen->hasClearColor) {
        MmeStageClearColor(ctx, offscreen->clearColorArgb);
    }
    if (offscreen->hasClearDepth) {
        MmeStageClearDepth(ctx, offscreen->clearDepth);
    }
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
    if (it != ctx->backgroundFixedVbs.end() && it->second != nullptr) {
        fixed = it->second;
        fixed->AddRef();                     // [0x18005d753] the bind borrow; the
                                             // map keeps the owning reference
    }

    // [0x18005d762] ctx+0x18A gates the cached copy: a cached buffer is
    // rebuilt instead of bound while the planner's viewport-change flag is
    // set, and the flag drops after the first fixup attempt (0x18005d9a7),
    // whether the rebuild succeeded or not.
    if (fixed == nullptr || ctx->backgroundQuadViewportDirty != 0) {
        ctx->backgroundQuadViewportDirty = 0;
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
                    // The displaced entry: one Release drops the lookup
                    // borrow above, the other the map's owning reference.
                    fixed->Release();
                    fixed->Release();
                    ctx->backgroundFixedVbs.erase(it);
                }
                ctx->backgroundFixedVbs[stream] = rebuilt;    // owning reference
                fixed = rebuilt;
                fixed->AddRef();                              // borrow for the bind below
            } else if (fixed != nullptr) {
                // [0x18005d7aa-0x18005d7b9] the create path drops the cached
                // borrow before creating; when the create fails there is no
                // bind (the record draws with the source stream) and the map
                // keeps the old entry.
                fixed->Release();
                fixed = nullptr;
            }
            stream->Unlock();
        }
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
        // [L74726-74733] renderPassList[lastRepeatCount] record step: the
        // original calls the queue item's wrapper chain (0x18005d17c:
        // wrapper+0 -> offscreen record -> resource -> OBJ -> vtable slot
        // 0x80, 0x18005d18d) right after the resume, so the UPCOMING turn
        // opens with a freshly bound + cleared G-buffer. The stepped
        // technique is the ITEM OWNER binding's own first technique (the
        // (turn id, carrier) record stores no per-turn technique choice);
        // the port resolves it from the item's carrier binding.
        const MmeRenderPassItem& nextItem = ctx->renderPassList[
            static_cast<size_t>(ctx->lastRepeatCount)];
        if (nextItem.carrier != nullptr) {
            MaterialBinding* nextBinding =
                MmeSceneRecordBinding(nextItem.carrier);
            MmeStepSceneRecord(nextItem.carrier,
                (nextBinding != nullptr) ? nextBinding->sceneTechIndex : -1);
        }
        // [L74731-74733, 0x18005d199-0x18005d1a6] when lastRepeatCount == 0
        // (the base-scene turn of a new frame) and the ctx+0x18 cached
        // target set is present (its mask was set by FUN_18005c510's
        // repeat == N capture at the end of the previous frame), APPLY it
        // (sub_1800675E0: SetRenderTarget per mask bit + depth +
        // SetViewport) so the main targets captured at the frame tail are
        // rebound before the base scene's draws.
        if (ctx->lastRepeatCount == 0 && ctx->cachedTargetSet.mask != 0 &&
            device != nullptr) {
            MmeRestoreTargetSet(ctx->cachedTargetSet, device);
        }
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
            ctx->currentBindingOffscreen = nullptr;
        } else {
            // [L74743-74745] ctx+0x168 = renderPassList[lastRepeatCount - 1]
            // - the CURRENT turn's queue item (the original stores the 0x48
            // wrapper; its resource identity is what the port's
            // currentBindingOffscreen carries for the step/resume/apply
            // drivers).
            int index = ctx->lastRepeatCount - 1;
            if (index >= 0 &&
                index < static_cast<int>(ctx->renderPassList.size())) {
                const MmeRenderPassItem& item = ctx->renderPassList[
                    static_cast<size_t>(index)];
                ctx->currentBindingObject = item.carrier;
                ctx->currentBindingOffscreen = item.offscreen;
            } else {
                ctx->currentBindingObject = nullptr;
                ctx->currentBindingOffscreen = nullptr;
            }
            // [L74746] GetRenderState(0xa1) feeds the FUN_18005c970 record
            // apply (the record's SetRenderTarget/SetDepthStencilSurface/
            // viewport/Clear choreography + the snapshot-gate recompute).
            DWORD adaptive = 0;
            if (device != nullptr) {
                device->GetRenderState(static_cast<D3DRENDERSTATETYPE>(0xa1), &adaptive);   // 161 = D3DRS_ADAPTIVE_TESS_X (raw constant; not in the DXSDK headers)
            }
            MmeApplyPassRecord(ctx, ctx->currentBindingObject,
                               ctx->currentBindingOffscreen, device,
                               adaptive != 0 ? 1 : 0);
        }
        if (device != nullptr) {
            device->BeginScene();                                  // [L74749 slot 0x148]
        }
    }
}

// ---------------------------------------------------------------------------
// [0x180001e70] sub_180001e70(mgr = ctx+0x240): the live-screen -> saved-slot
// refresh, called by BOTH FUN_18005e210 branches between their EndScene and
// BeginScene. Verified against the binary:
//   1. gate: the mgr's saved main set (mgr+0x50, mask dword) must be present;
//      nothing to refresh otherwise (the set is produced by the sub_180001880
//      snapshot's refetch and consumed exactly here);
//   2. save the CURRENT device target set on the stack (sub_180067500,
//      mask 31 - targets 0..3 + depth + viewport);
//   3. per slot: StretchRect(LIVE targets[i] -> saved targets[i]) - the
//      composite on the working targets lands on the saved mains. The source
//      rect is null, or {0, 0, savedViewport.W, savedViewport.H} when the
//      mgr+0x1FA family flag (== ctx+0x43A, c510's all-planB-special flag)
//      is set; filter D3DTEXF_NONE, dest rect null;
//   4. when the family flag is CLEAR and the live depth exists:
//      StretchRect(live depth -> saved depth) - D3D9 cannot stretch
//      depth-stencil, so this fails harmlessly exactly like the original;
//   5. rebind the saved set (sub_1800675E0: SetRenderTarget per mask bit +
//      SetDepthStencilSurface + SetViewport) - the mains become the device
//      targets again;
//   6. drop the saved set (mask = 0, release every ref) and release the
//      stack set.
// ---------------------------------------------------------------------------

static void MmeRefreshMainSaveFromLive(MmeContext* ctx)
{
    IDirect3DDevice9* device = (ctx != nullptr) ? ctx->device : nullptr;
    if (device == nullptr) {
        return;
    }
    MmeTargetSet& mgr = ctx->mainTargetSet;
    if (mgr.mask == 0) {
        return;                                   // [0x180001ead] gate
    }
    MmeTargetSet live;
    MmeSaveTargetSet(live, device, 31);           // [0x180001ed7-0x180001fa4]
    RECT srcRect;
    memset(&srcRect, 0, sizeof(srcRect));
    srcRect.right = static_cast<LONG>(mgr.viewport.Width);
    srcRect.bottom = static_cast<LONG>(mgr.viewport.Height);
    const bool familySpecial = ctx->allObjectsSpecialFlag != 0;  // mgr+0x1FA
    const RECT* sourceRect = familySpecial ? &srcRect : nullptr;
    for (int i = 0; i < 4; ++i) {
        if (live.targets[i] == nullptr || mgr.targets[i] == nullptr) {
            continue;
        }
        device->StretchRect(live.targets[i], sourceRect, mgr.targets[i],
                            nullptr, D3DTEXF_NONE);   // [0x180002011]
    }
    if (!familySpecial && live.depth != nullptr && mgr.depth != nullptr) {
        device->StretchRect(live.depth, nullptr, mgr.depth, nullptr,
                            D3DTEXF_NONE);            // [0x18000204b]
    }
    MmeRestoreTargetSet(mgr, device);             // [0x18000205f]
    MmeReleaseTargetSet(mgr);                     // [0x180002064-0x18000209a]
    MmeReleaseTargetSet(live);
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
        // [0x18005e297] the gate reads ctx+0x188 (byte 392) - da50's
        // snapshot success flag for THIS turn, not any c510-staged marker.
        if (ctx->snapshotSuccessFlag != 0) {
            // [L75477-75503] the frame's scene is closed (slot 0x150 =
            // EndScene on the original's wrapper vtable), the live-screen
            // -> saved-slot refresh runs (sub_180001e70(ctx+0x240) - the
            // working targets' composite is StretchRect'd onto the saved
            // mains and the mains are rebound), the optional post Clear is
            // issued, and a fresh scene is opened (slot 0x148 = BeginScene)
            // so the draws that follow land on top of the post chain's
            // output.
            if (device != nullptr) {
                device->EndScene();                                    // [L75477]
            }
            MmeRefreshMainSaveFromLive(ctx);                           // [L75478]
            // [0x18005e2bb] the Clear gate reads ctx+0x438 (byte 1080),
            // set only by the persistent-surface fast paths of
            // sub_180002100/sub_180002440 (a snapshot whose surface IS the
            // current device target) and cleared by c510's WORD write at
            // every ==N boundary. With the persistent layer live the gate
            // fires the turn after the persistent snapshot was taken: the
            // restore re-binds the ctx+0x18 cached set (= the persistent
            // surfaces captured at the ==N boundary) and the Clear(0,0,7)
            // wipes them for the next persistent reuse.
            if (ctx->persistentClearUsed != 0) {
                // [L75479-75501, 0x18005e2bb-0x18005e3ff] the optional post
                // Clear. READ-ONLY staged lookup: color = clearColorMap[
                // cachedRT0] when present else GetClearColor(); z =
                // clearDepthMap[cachedDepth] when present else 1.0f. The
                // original NEVER writes the maps here - the staged values
                // survive for the next turn's snapshot validation. The
                // Clear itself is bracketed by a stack target-set
                // save/apply/restore (sub_180067500 mask 31 -> apply the
                // ctx+0x18 cached set when its mask is set -> Clear(0,0,7)
                // -> re-apply the stack set), so the clear lands on the
                // cached MAIN targets while the current (possibly
                // redirected) bindings are preserved.
                D3DCOLOR clearColor = GetClearColor();                 // [L75479]
                float clearDepth = 1.0f;
                ctx->postClearColor = clearColor;
                IDirect3DSurface9* cachedTarget0 = ctx->cachedTargetSet.targets[0];
                std::map<IDirect3DSurface9*, D3DCOLOR>::const_iterator colorIt =
                    ctx->clearColorMap.find(cachedTarget0);            // [sub_180061470]
                if (colorIt != ctx->clearColorMap.end()) {
                    clearColor = colorIt->second;                      // [0x18005e30e]
                }
                IDirect3DSurface9* cachedDepth = ctx->cachedTargetSet.depth;
                std::map<IDirect3DSurface9*, float>::const_iterator depthIt =
                    ctx->clearDepthMap.find(cachedDepth);              // [sub_180061470]
                if (depthIt != ctx->clearDepthMap.end()) {
                    clearDepth = depthIt->second;                      // [0x18005e347]
                }
                if (device != nullptr) {
                    MmeTargetSet current;
                    MmeSaveTargetSet(current, device, 31);             // [0x18005e389]
                    if (ctx->cachedTargetSet.mask != 0) {
                        MmeRestoreTargetSet(ctx->cachedTargetSet, device);  // [0x18005e39a]
                    }
                    device->Clear(0, nullptr, 7, clearColor, clearDepth, 0); // [0x18005e3c3]
                    if (current.mask != 0) {
                        MmeRestoreTargetSet(current, device);          // [0x18005e3d8]
                    }
                    MmeReleaseTargetSet(current);
                }
            }
            if (device != nullptr) {
                device->BeginScene();                                  // [slot 0x148]
            }
        }
    } else {
        // [L75513-75519] the pass-record branch: gated by `*record &&
        // record+0x3c` (0x18005e41b) - the CURRENT queue item's 0x2E
        // resource (wrapper+0, carried by the item) AND its gate byte, NOT
        // the run state (a preprocess-class carrier never holds a +0x358
        // state yet runs this branch when its offscreen turn and gate are
        // set). The scene is closed around sub_180001e70(ctx+0x240) - the
        // live-screen -> saved-slot refresh (StretchRect the working
        // targets into the mgr's saved mains and rebind them) - and
        // reopened.
        SasResource* turnOffscreen = ctx->currentBindingOffscreen;
        if (ctx->currentBindingObject != nullptr &&
            turnOffscreen != nullptr && turnOffscreen->passSnapshotGate != 0) {
            if (device != nullptr) {
                device->EndScene();                                // slot 0x150
            }
            MmeRefreshMainSaveFromLive(ctx);                       // [0x18005e43b]
            if (device != nullptr) {
                device->BeginScene();                              // slot 0x148
            }
        }
    }

    // [L75521-75527] the passPlanB walk: FUN_18005a5c0 per record - resume
    // the stepped techniques to completion (the lighting/composite passes
    // onto the restored targets; a walk that already completed only
    // re-applies the restore epilogue) and drop the run states. The resume
    // resolves each carrier's technique through the SAME wrapper key as the
    // step (0x18005e46a passes ctx+0x168): at the base turn (a null
    // currentBindingObject) every scene carrier resumes its FIRST
    // technique; at an item turn every carrier whose effect declares the
    // turn's resource NAME resumes its (first) technique - every other
    // carrier's (id, carrier) record is null and sub_18005A5C0's walk
    // no-ops (the port's -1 gate below).
    for (size_t i = 0; i < ctx->passPlanB.size(); ++i) {
        ModelData* record = ctx->passPlanB[i];
        int techIndex = MmeSceneWalkTechIndex(ctx, record);
        if (techIndex < 0) {
            continue;
        }
        MmeFinishSceneRecord(record, techIndex);
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
// passPlanB backwards step walk, the null-binding branch (EndScene ->
// staged validation over the mask-saved target set -> the FUN_180001880
// main-RT snapshot + staged Clear -> BeginScene, gated on adaptive-tess +
// the redirected-mask OR, deduped by ctx+0x189), the bound-record executor
// (FUN_18005cac0), the passPlanA walk, the queued background-record replay
// (FUN_18005d5c0) and the viewport/state restore.
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

    // [L75169-...] the passPlanB backwards walk: FUN_18005a410 per record -
    // the ScriptExternal step that binds + clears the G-buffers and parks
    // the technique at the suspension point for the scene turn. The step
    // resolves each carrier's technique through the WRAPPER key
    // (0x18005db44 `mov rdx, [rsi+0x168]`; the (turn id, carrier, -1) map
    // lookup is verified at 0x18005a449-0x18005a4bb): at the base turn (a
    // null currentBindingObject) every scene carrier steps its FIRST
    // technique; at an item turn every carrier whose effect declares the
    // turn's resource NAME steps its (first) technique - every other
    // carrier's (wrapper id, carrier) record is null (created so by
    // sub_18002CA80's child-round drain) and sub_18005A410's map miss skips
    // the whole body. A skipped carrier
    // holds no run state here (FUN_18005c510 destroyed them all at the
    // boundary), so it contributes no redirectedMask bit either.
    unsigned int bindingErrorFlags = 0;
    for (size_t i = ctx->passPlanB.size(); i > 0; --i) {
        ModelData* record = ctx->passPlanB[i - 1];
        int techIndex = MmeSceneWalkTechIndex(ctx, record);
        if (techIndex < 0) {
            continue;
        }
        MmeStepSceneRecord(record, techIndex);
        if (record->runState() != nullptr) {
            bindingErrorFlags |= record->runState()->redirectedMask;
        }
    }

    if (ctx->currentBindingObject == nullptr) {                        // [L75177]
        ctx->snapshotSuccessFlag = 0;                                  // [0x18005db8d] ctx+0x188 = 0
        if (adaptive != 0 && bindingErrorFlags != 0) {                 // [L75179 gate]
            if (ctx->postEffectRanFlag != 0) {
                // [L75180-75182] ctx+0x189 set: the failure message was
                // already shown this session - skip the whole snapshot block
                // (ctx+0x188 stays 0, the LABEL_93 host clear runs).
            } else if (device != nullptr) {
                // [L75184] the scene is closed around the snapshot chain
                // (slot 0x150 = EndScene; slot 0x148 = BeginScene at
                // LABEL_89 below).
                device->EndScene();
                // [L75186-75189] FUN_180067500: save the current targets
                // masked by the planB redirectedMask OR into a local set.
                MmeTargetSet current;
                MmeSaveTargetSet(current, device, bindingErrorFlags);
                // [L75190-75229] the staged validation over the saved set:
                // RT0 present, all present color targets share one staged
                // color (ctx+0x1e0 map), the depth target staged (ctx+0x210
                // map, else the fallback is the host clear color / 1.0f).
                bool colorsValid = false;
                bool depthStaged = false;
                D3DCOLOR stagedColor = 0;
                float stagedDepth = 1.0f;
                MmeValidateStagedClear(ctx, current.targets, current.depth,
                                       &colorsValid, &stagedColor,
                                       &depthStaged, &stagedDepth);
                // [0x18005dce9] the ctx+0x439 byte clear: when the cached
                // main RT0/depth carry no staged clear, the persistent-layer
                // gate goes down (read by the sub_180002100/sub_180002440
                // fast paths; NOT the family flag at ctx+0x43A).
                if (ctx->clearColorMap.find(ctx->cachedTargetSet.targets[0]) ==
                        ctx->clearColorMap.end() ||
                    ctx->clearDepthMap.find(ctx->cachedTargetSet.depth) ==
                        ctx->clearDepthMap.end()) {
                    ctx->persistentLayerEligible = 0;
                }
                // [L75230] sub_180001880(ctx+0x240, ...) - the turn-boundary
                // main-RT snapshot + staged Clear (the post-effect HRESULT;
                // mirrored into ctx->lastPostEffectHr, the mgr+0x1fc
                // equivalent).
                HRESULT hr = MmeSnapshotMainTargets(ctx, colorsValid,
                                                    depthStaged, stagedColor,
                                                    stagedDepth);
                ctx->lastPostEffectHr = static_cast<unsigned long>(hr);
                if (SUCCEEDED(hr)) {
                    ctx->snapshotSuccessFlag = 1;                     // [0x18005dd16] ctx+0x188 = 1
                } else {
                    ctx->postEffectRanFlag = 1;                        // [0x18005dd22] ctx+0x189 = 1
                    // [L75237-75346] "DirectX Error: <desc> [%08X]\n" + the
                    // localized "Failed to process post effect:" message box.
                    MmeReportPostEffectFailure(static_cast<unsigned long>(hr));
                }
                device->BeginScene();                                  // [LABEL_89 slot 0x148]
                MmeReleaseTargetSet(current);                          // [LABEL_89 release]
            }
            // [L75368-75405] the shown-message dedup walk (DAT_1800d99d8
            // list) - Phase 3 seam, not reproduced.
        }
    } else {
        // [L75410] FUN_18005cac0(record, device): the bound record's
        // post-effect EXECUTOR - EndScene, staged validation and the
        // sub_180001880 snapshot + Clear (which resets the bound snapshot
        // targets for the new turn), BeginScene; gated by the wrapper+0x3c
        // byte the FUN_18005c970 apply recomputes at the repeat boundary
        // (kept on the turn's 0x2E resource).
        // (The c970 rebind semantics live in MmeApplyPassRecord, called by
        // FUN_18005d130 - the ORIGINAL call site of FUN_18005c970.)
        MmeRunPostEffectRecord(ctx, ctx->currentBindingObject, 0, device);
    }

    // [L75413-75421, 0x18005e11e] with no record bound and no snapshot
    // success this turn, re-issue the host clear the suppressed OnClear was
    // holding back. This runs once per repeat iteration, after the queued
    // background records have been collected and before they are replayed
    // below.
    if (ctx->currentBindingObject == nullptr && ctx->snapshotSuccessFlag == 0) {
        D3DCOLOR clearColor = GetClearColor();
        if (device != nullptr) {
            device->Clear(0, nullptr, 7, clearColor, 1.0f, 0);         // slot 0x158
        }
    }

    // [L75423] the passPlanA walk (FUN_18005a2c0): preprocess-class scene
    // effects run their whole technique here (they precede the objects; no
    // suspension - the original routes them through FUN_18001bab0's full
    // run on a fresh state that never touches +0x358). The walk site passes
    // ctx+0x168 (0x18005e170 `mov rdx,[rsi+0x168]`), so at an item turn
    // only the carriers whose effect declares the turn's resource NAME run
    // (the (turn id, carrier, -1) map miss no-ops the others) - the base
    // turn's null wrapper resolves the root record and every carrier runs.
    for (size_t i = 0; i < ctx->passPlanA.size(); ++i) {
        if (MmeSceneWalkTechIndex(ctx, ctx->passPlanA[i]) < 0) {
            continue;
        }
        MmeFullRunSceneRecord(ctx->passPlanA[i]);
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

} // namespace mme
