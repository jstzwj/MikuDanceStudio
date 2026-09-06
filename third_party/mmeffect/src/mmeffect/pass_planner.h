// pass_planner.h - the render pass planner and the draw-path bookkeeping of
// MMEffect.dll.
//
// Evidence:
//   - MME_RebuildRenderPassPlan [0x18005b9e0, kit decompile 18KB]: per-model
//     scratch reset (FUN_1800599a0 + the inline +0xec..+0x137 reset), host
//     object scan (ExpGetPmdNum/ID/Order/Disp, ExpGetAcsNum/ID/Order/Disp),
//     per-object plan refresh (FUN_180059aa0), the LoadedPMMFile EMM autoload
//     (FUN_1800570D0), the |order|-sorted plan build (FUN_18002baa0 +
//     FUN_18005fd30 into ctx+0x148), the viewport-change detection
//     (GetViewport vs ctx+0x170..0x188) and the repeat tail
//     (empty/effect-disabled -> ctx+0x14 = 1 with no host call; otherwise
//     ctx+0x14 = N+1 and ExpSetRenderRepeatCount(N+1); FUN_18005c510 last).
//   - FUN_180059aa0 [0x180059aa0]: per-object plan refresh - host index to
//     ModelData+0xf0, |order| to +0xec, visibility to +0xf4, and the
//     accessory world matrix (ExpGetAcsWorldMat) into +0xf8..+0x137 with the
//     +0x190 copy (models keep the identity).
//   - FUN_1800599a0 [0x1800599a0]: per-object reset - draw-type index -1
//     (+0xe8), cached-snapshot head reset (+0x158..+0x18e), release of the
//     Phase 3 binding object at +0x358.
//   - FUN_18005d130 [0x18005d130]: pass bookkeeping on the draw paths - with
//     passes pending and repeat index < size: run the post effect
//     (FUN_18005e210) and step the pass record; then FUN_18005c510; then
//     BeginStateBlock (slot 0x150) / EndStateBlock (slot 0x148) around the
//     ctx+0x168 update: repeat < 1 -> null; else renderPassList[repeat-1]
//     plus GetRenderState(0xa1) + the FUN_18005c970 record apply (Phase 3).
//   - FUN_18005c510 [0x18005c510]: final bookkeeping - per-model resets, the
//     pending-snapshot clear, the ctx flag resets, the state re-init block
//     (gated on lastRepeatCount == pass size) and the repeat==0 clear-color
//     staging (GetClearColor into the per-target map, depth = 1.0f).
//   - FUN_18005e210 [0x18005e210]: post-effect runner - backgroundDrawnFlag
//     gate, binding-context acquire + Capture, the error-report gate, the
//     GetClearColor + Clear(0,0,7) block (state-block wrapped), the per-model
//     pass apply (Phase 3) and the Capture/Apply viewport restore.
//   - FUN_18005da50 [0x18005da50]: the DirectX Error reporter
//     ("Failed to process post effect:\n" / "DirectX Error: " + DXErr9
//     description + " [%08X]\n" + MessageBoxA, English/localized selector).
//   - FUN_180055690 [0x180055690]: the binding-context pool on ctx+0x68
//     (CreateStateBlock(D3DSBT_ALL) via device slot 0x1d8, round-robin reuse).
//   - FUN_180055780 [0x180055780]: pool release (device loss / context dtor).
#pragma once

namespace mme {

class MmeContext;
class ModelData;

// [0x1800599a0] FUN_1800599a0: per-model pass-state reset (start of every
// plan rebuild; also the FUN_18005c510 head).
void MmeResetObjectPlanState(ModelData* model);

// [0x180059aa0] FUN_180059aa0: refresh the per-object plan scratch from the
// host (host index, |order|, visibility, accessory world matrix).
void MmeRefreshObjectPlan(ModelData* model, int hostIndex);

// [0x18002baa0 + 0x18005fd30] FUN_18002baa0: sort the scanned objects by
// |order|, split renderClass 1/2 into ctx+0xb8/ctx+0xd8, publish the extra
// -pass plan (manager+0x158) into ctx+0x148 (FUN_18005fd30).
void MmeSortPlanObjects(MmeContext* ctx);

// [0x18005c510] FUN_18005c510: final pass bookkeeping (end of the plan
// rebuild and of every FUN_18005d130).
void MmeUpdatePassBookkeeping(MmeContext* ctx);

// [0x18005d130] FUN_18005d130: pass bookkeeping on repeat-count change
// (draw paths).
void MmePassBookkeeping(MmeContext* ctx);

// [0x18005e210] FUN_18005e210: post-effect pass runner. Ported observable
// part: the ctx+0x12 flag write, the binding-context acquire/Capture/Apply
// pair and the GetClearColor + Clear(0, 0, 7) block; the SAS script passes
// are Phase 3.
void MmeRunPostEffect(MmeContext* ctx);

// [0x18005da50] FUN_18005da50: the DirectX Error reporter ("DirectX Error: "
// + DXErr9 description; compact HRESULT subset, documented divergence).
void MmeReportDrawError(MmeContext* ctx);

// [0x180055690] FUN_180055690: binding-context pool acquire (the pool holds
// IDirect3DStateBlock9 objects created with D3DSBT_ALL; borrowed reference,
// the callers Capture() them at slot 0x20).
void* MmeAcquireBindingContext(MmeContext* ctx);

// [0x180055780] FUN_180055780: release the binding-context pool (device loss).
void MmeReleaseBindingContextPool(MmeContext* ctx);

} // namespace mme
