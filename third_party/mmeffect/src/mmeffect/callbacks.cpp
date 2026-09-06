// callbacks.cpp - the 11 MMEffect.dll exports (ABI: include/mme_abi.h,
// export table: src/MMEffect/MMEffect.def, ordinals 1..11 alphabetical).
//
// Ground truth: kit decompiled 1800564a0_Initialize.c, 180058530_Cleanup.c,
// 180057430_OnCreateModel.c, 1800574a0_OnDeleteModel.c, 1800574e0_OnBeginScene.c,
// 180058400_OnEndScene.c, 180058440_OnClear.c, 180058490_OnDrawPrimitive.c,
// 1800584b0_OnDrawIndexedPrimitive.c, 180058970_OnLostDevice.c,
// 180058a20_OnResetDevice.c, plus 18005d340_MME_HandleDrawIndexedPrimitive.c
// and 18005d250 (big-C 74756) for the draw handlers.
//
// Documented divergence: the original talks to a device whose vtable is
// shifted by +0x20 relative to the standard IDirect3DDevice9 (the original
// MMHack wrapper adds 4 leading slots) except one call at GetMaterial with a
// +0x10 shift (see PHASE1_IMPLEMENTATION_NOTES.md). The rebuilt suite has no
// such wrapper yet, so every device interaction uses the standard interface
// with identical arguments; MMHack must hand MMEffect a standard-shaped device.
#include "mme_abi.h"     // fixed ABI (include/)

#include <d3dx9.h>
#include <io.h>
#include <cstdio>
#include <cstring>

#include "MMDExport.h"   // ExpGetRenderRepeatCount / ExpSetRenderRepeatCount
#include "mmhack_api.h"  // the 22 MMHack state queries

#include "effect_engine.h"
#include "emm_manager.h"
#include "material_bind.h"
#include "ini_file.h"
#include "mme_context.h"
#include "mme_globals.h"
#include "mme_log.h"
#include "mme_ui.h"     // MmeUiInstallOffscreenSubclass (the 0x180055b10 proc)
#include "mme_util.h"
#include "model_data.h"
#include "pass_planner.h"
#include "render_snapshot.h"

namespace {

using namespace mme;

// [0x18005d340] MME_HandleDrawIndexedPrimitive port.
void MmeHandleDrawIndexedPrimitive(IDirect3DDevice9* device,
                                   unsigned int type, int baseVertexIndex,
                                   unsigned int minVertexIndex, unsigned int vertexCount,
                                   unsigned int startIndex, unsigned int primitiveCount)
{
    MmeContext* ctx = g_context;
    if (ctx == nullptr) {
        return; // divergence guard: the original derefs the context unconditionally
    }

    // [L37-41] pass-change detection on the host repeat count.
    if (ctx->lastRepeatCount != 0) {
        int repeat = ExpGetRenderRepeatCount();
        if (repeat != ctx->lastRepeatCount) {
            ctx->lastRepeatCount = repeat;
            MmePassBookkeeping(ctx);               // FUN_18005d130
        }
    }

    // [L43-51] effects disabled: forward the draw unless the engine is inside
    // its own binding pass (ctx+0x168 != 0).
    if (ctx->effectEnabled == 0) {
        if (ctx->currentBindingObject != nullptr) {
            return;
        }
        if (device != nullptr) {
            // device slot 0x290 = DrawIndexedPrimitive pass-through [L49].
            device->DrawIndexedPrimitive(static_cast<D3DPRIMITIVETYPE>(type),
                                         baseVertexIndex, minVertexIndex,
                                         vertexCount, startIndex, primitiveCount);
        }
        return;
    }

    int drawType = GetCurrentDrawType();           // [L52]
    if (drawType == 0) {
        if (ctx->bindingReadyFlag != 0) {          // ctx+0x10 [L54]
            if (ctx->backgroundDrawnFlag == 0) {   // ctx+0x12 [L55]
                MmeRunPostEffect(ctx);             // FUN_18005e210
            }
            if (ctx->currentBindingObject != nullptr) {   // [L58]
                return;
            }
            if (device != nullptr) {
                device->DrawIndexedPrimitive(static_cast<D3DPRIMITIVETYPE>(type),
                                             baseVertexIndex, minVertexIndex,
                                             vertexCount, startIndex, primitiveCount);
            }
            return;
        }
        // ctx+0x10 clear: fall through to the snapshot path [L63-66 /
        // big-C LAB_18005d444].
        if (ctx->currentBindingObject != nullptr) {
            return;
        }
    } else if (drawType == 5) {
        // [L67-70] z-plot path: swallow while the engine holds a binding pass.
        if (ctx->currentBindingObject != nullptr) {
            return;
        }
    }

    // [L71-84] build the 0x220 record on the stack. The original leaves the
    // reserved fields uninitialized; we zero the record first (defensive,
    // documented).
    RenderSnapshot snap;
    memset(&snap, 0, sizeof(snap));
    snap.enabled = 1;                              // [L72]
    snap.primitive_type = type;                    // [L76]
    snap.base_vertex_index = baseVertexIndex;      // [L77]
    snap.min_vertex_index = minVertexIndex;        // [L78]
    snap.vertex_count = vertexCount;               // [L79]
    snap.start_index = startIndex;                 // [L73]
    snap.primitive_count = primitiveCount;         // [L74]
    snap.binding_context = nullptr;                // [L75]
    snap.draw_type = drawType;                     // [L80 local_22c]

    if (drawType == 0) {
        // [L81-85] background/gizmo: acquire the binding context and queue the
        // record for the post-effect runner (FUN_180060140 push, ctx+0x98).
        void* bindingContext = MmeAcquireBindingContext(ctx);
        snap.binding_context = bindingContext;
        // [L83] slot 0x20 on the context = IDirect3DStateBlock9::Capture -
        // the queued record replays through Apply(), so the device state at
        // queue time must be captured into the pooled block now.
        if (bindingContext != nullptr) {
            static_cast<IDirect3DStateBlock9*>(bindingContext)->Capture();
        }
        ctx->pendingSnapshots.push_back(snap);
    } else {
        // [L86-113] resolve the model for the current draw.
        unsigned long long id = GetCurrentModelID();               // [L87]
        ModelData* model = ctx->lastDrawnModel;
        if (model == nullptr || model->objectId() != id) {          // [L88-92]
            model = MmeFindOrCreateModelEntry(id);                  // [L90]
        }
        if (model != nullptr) {
            // The assignment-dialog hide gate: an object hidden through the
            // mapping (the row checkbox / Hide-Show) is not rendered while
            // MME's effect path drives the frame; disabling the effects
            // forwards every draw again (the host pipeline takes over).
            if (!model->shown()) {
                return;
            }
            // [L94] snapshot update runs whenever the model resolved.
            MmeUpdateModelRenderSnapshot(model, &snap);
            // [L93-97] apply gate: draw types 1/2 always; others only when
            // the model is a normal object (unknownFlag360 != 1 && class 0).
            if (drawType == 1 || drawType == 2 ||
                (model->unknownFlag360() != 1 && model->renderClass() == 0)) {
                if (ctx->lastDrawnModel == nullptr || ctx->lastDrawnModel != model ||
                    (model->unknownFlag360() != 1 && model->renderClass() == 0)) {
                    ctx->lastDrawnModel = model;                    // [L98]
                    if (ctx->bindingReadyFlag == 0) {               // [L99-105]
                        ctx->bindingReadyFlag = (drawType != 5) ? 1 : 0;
                    }
                    if (model->renderClass() != 1 && model->renderClass() != 2) {  // [L106]
                        if (ctx->bindingReadyFlag != 0 && ctx->errorReportedFlag == 0) {  // [L107]
                            MmeReportDrawError(ctx);                // FUN_18005da50
                        }
                        // [L110] MME_ApplyModelRenderSnapshot (Phase 2 seam).
                        MmeApplyModelRenderSnapshot(model, ctx->currentBindingObject, snap);
                    }
                }
            }
            // The technique-pass draw wrapper [the original FUN_18005a9c0
            // draw op]: render the draw through the assigned effect's
            // selected technique - Begin(&passes, 0) / FUN_18001b5b0 parameter
            // walk (the standard apply above) / BeginPass / the recorded draw /
            // EndPass / End, exactly like the original op-0 executor.
            bool drewThroughEffect = false;
            if (drawType == 1 || drawType == 2) {
                MaterialBinding* binding = MmeActiveModelBinding(model);
                {
                    static unsigned long long loggedId = 0;
                    static int loggedType = -1;
                    if (loggedId != id || loggedType != drawType) {
                        loggedId = id;
                        loggedType = drawType;
                        char line[0x220];
                        sprintf_s(line, sizeof(line),
                                  "DrawWrap: id=%I64u type=%d binding=%p path='%s'\n",
                                  id, drawType, (void*)binding,
                                  (binding != nullptr ? binding->effectPath.c_str() : ""));
                        MmeLogWrite(line, 0);
                    }
                }
                if (binding != nullptr && binding->effect != nullptr &&
                    binding->technique != nullptr && binding->passCount > 0) {
                    UINT passes = 0;
                    HRESULT beginHr = binding->effect->SetTechnique(binding->technique);
                    if (SUCCEEDED(beginHr)) {
                        beginHr = binding->effect->Begin(&passes, 0);
                    }
                    if (SUCCEEDED(beginHr)) {
                        static int loggedBegin = 0;
                        if (loggedBegin == 0) {
                            loggedBegin = 1;
                            char line[0x220];
                            sprintf_s(line, sizeof(line),
                                      "DrawWrap Begin ok: passes=%u\n", passes);
                            MmeLogWrite(line, 0);
                        }
                    } else {
                        static int loggedFail = 0;
                        if (loggedFail == 0) {
                            loggedFail = 1;
                            char line[0x220];
                            sprintf_s(line, sizeof(line),
                                      "DrawWrap Begin FAILED hr=0x%08X\n",
                                      (unsigned int)beginHr);
                            MmeLogWrite(line, 0);
                        }
                    }
                    if (SUCCEEDED(beginHr)) {
                        for (UINT passIndex = 0; passIndex < passes; ++passIndex) {
                            if (SUCCEEDED(binding->effect->BeginPass(passIndex))) {
                                if (device != nullptr) {
                                    device->DrawIndexedPrimitive(
                                        static_cast<D3DPRIMITIVETYPE>(type),
                                        baseVertexIndex, minVertexIndex, vertexCount,
                                        startIndex, primitiveCount);
                                }
                                binding->effect->EndPass();
                            }
                        }
                        binding->effect->End();
                        drewThroughEffect = true;
                    }
                }
            }
            if (!drewThroughEffect) {
                // [FUN_18001b940 fallback] the original's op walk falls back
                // to the raw recorded draw whenever the binding has no effect
                // or no usable technique op list (the binding's error latch
                // takes the same path after a Begin failure). Never swallow
                // the model: an object without an effect (or with an effect
                // that cannot begin) renders through the host pipeline, which
                // is what the original does and what keeps the viewport from
                // going blank while effects are enabled.
                if (device != nullptr) {
                    device->DrawIndexedPrimitive(
                        static_cast<D3DPRIMITIVETYPE>(type),
                        baseVertexIndex, minVertexIndex, vertexCount,
                        startIndex, primitiveCount);
                }
            }
        }
    }
}

// [0x18005d250] FUN_18005d250 - OnDrawPrimitive handler.
void MmeHandleDrawPrimitive(IDirect3DDevice9* device,
                            unsigned int type, unsigned int startVertex,
                            unsigned int primitiveCount)
{
    MmeContext* ctx = g_context;
    if (ctx == nullptr) {
        return;
    }

    // [L74773-74778] repeat-count change detection.
    if (ctx->lastRepeatCount != 0) {
        int repeat = ExpGetRenderRepeatCount();
        if (repeat != ctx->lastRepeatCount) {
            ctx->lastRepeatCount = repeat;
            MmePassBookkeeping(ctx);               // FUN_18005d130
        }
    }

    if (ctx->effectEnabled != 0) {
        if (ctx->bindingReadyFlag == 0) {          // [L74781]
            // [L74782-74791] queue a background record (enabled = 0) and
            // return WITHOUT forwarding the draw.
            RenderSnapshot snap;
            memset(&snap, 0, sizeof(snap));
            snap.enabled = 0;                      // [L74783]
            snap.primitive_type = type;            // [L74784]
            snap.start_index = startVertex;        // [L74785]
            snap.primitive_count = primitiveCount; // [L74786]
            void* bindingContext = MmeAcquireBindingContext(ctx);   // [L74787]
            snap.binding_context = bindingContext;
            // [L74788] slot 0x20 on the context = IDirect3DStateBlock9::
            // Capture (the replay path Applies this block per record).
            if (bindingContext != nullptr) {
                static_cast<IDirect3DStateBlock9*>(bindingContext)->Capture();
            }
            ctx->pendingSnapshots.push_back(snap);                  // [L74790]
            return;
        }
        if (ctx->backgroundDrawnFlag == 0) {       // [L74793]
            MmeRunPostEffect(ctx);                 // FUN_18005e210
        }
    }

    // [L74797-74798] forward unless the engine holds a binding pass
    // (device slot 0x288 = DrawPrimitive).
    if (ctx->currentBindingObject == nullptr && device != nullptr) {
        device->DrawPrimitive(static_cast<D3DPRIMITIVETYPE>(type),
                              startVertex, primitiveCount);
    }
}

// [0x180056220] FUN_180056220 - device/shader info string for the banner.
std::string MmeBuildDeviceInfoString(IDirect3DDevice9* device)
{
    // "Device: " + type + (HAL: ": " + description + driver version)
    // + "\n" + "ShaderVersion: <vs>, <ps>" + "\n\n"
    // (strings byte-verified: 0x1800b5758 "Device: ", 0x1800b5764 "REF",
    //  0x1800b5768 "HAL", 0x1800b5770 ": ", 0x1800b576c "SW",
    //  0x1800b5778 " (driver: %d.%d.%d.%d)", 0x1800b26fc "\n",
    //  0x1800b5790 "ShaderVersion: ", 0x1800b57a0 "%s, %s", 0x1800b3ac0 "\n\n").
    std::string out = "Device: ";
    if (device != nullptr) {
        D3DCAPS9 caps;
        memset(&caps, 0, sizeof(caps));
        device->GetDeviceCaps(&caps);              // [L69834 slot 0x38]

        if (caps.DeviceType == D3DDEVTYPE_REF) {   // [L69840]
            out += "REF";
        } else if (caps.DeviceType == D3DDEVTYPE_HAL) {  // [L69845]
            out += "HAL";
            out += ": ";
            IDirect3D9* d3d = nullptr;
            if (SUCCEEDED(device->GetDirect3D(&d3d)) && d3d != nullptr) {  // [L69849 slot 0x30]
                D3DADAPTER_IDENTIFIER9 ident;
                memset(&ident, 0, sizeof(ident));
                if (SUCCEEDED(d3d->GetAdapterIdentifier(caps.AdapterOrdinal, 0, &ident))) {  // [L69851 slot 0x28]
                    out += ident.Description;
                    // [L69863] " (driver: %d.%d.%d.%d)" from the packed
                    // DriverVersion; word order UNCERTAIN (high part first,
                    // matching the visible decompile args).
                    out += MmeFormat(" (driver: %d.%d.%d.%d)",
                                     HIWORD(static_cast<unsigned int>(
                                         ident.DriverVersion.HighPart)),
                                     LOWORD(static_cast<unsigned int>(
                                         ident.DriverVersion.HighPart)),
                                     HIWORD(static_cast<unsigned int>(
                                         ident.DriverVersion.LowPart)),
                                     LOWORD(static_cast<unsigned int>(
                                         ident.DriverVersion.LowPart)));
                }
                d3d->Release();                    // [L69874-69876]
            }
        } else if (caps.DeviceType == D3DDEVTYPE_SW) {   // [L69879]
            out += "SW";
        }
    }

    out += "\n";                                   // 0x1800b26fc
    out += "ShaderVersion: ";                      // 0x1800b5790
    const char* vsProfile = D3DXGetVertexShaderProfile(device);  // [L69888]
    const char* psProfile = D3DXGetPixelShaderProfile(device);   // [L69887]
    out += vsProfile != nullptr ? vsProfile : "";
    out += ", ";
    out += psProfile != nullptr ? psProfile : "";
    out += "\n\n";                                 // 0x1800b3ac0
    return out;
}

// [0x180058530 L78-81] the 96-dash separator line logged by Cleanup.
std::string MmeBuildSeparatorLine()
{
    return std::string(96, '-') + "\n";
}

} // namespace

extern "C" {

// [0x1800564a0] Initialize - full port of the control flow that exists in
// Phase 1. Returns 0 on success, 1 on failure.
int __cdecl Initialize(IDirect3DDevice9* device)
{
    // [L45-46] debug flag + EMMAutoSave default.
    g_debugMode = IsDebugMode() ? 1 : 0;           // DAT_1800d99de
    g_emmAutoSave = 1;                             // DAT_1800d72e0

    // [L49-73] build the exe directory and the MMEffect.ini path via
    // GetModuleFileNameA(NULL) + _splitpath_s + _makepath_s.
    char modulePath[0x104];
    modulePath[0] = '\0';
    GetModuleFileNameA(nullptr, modulePath, 0x104);
    char drive[3] = { 0 };
    char dir[0x100] = { 0 };
    if (_splitpath_s(modulePath, drive, sizeof(drive), dir, sizeof(dir),
                     nullptr, 0, nullptr, 0) == 0) {
        char exeDir[0x104];
        if (_makepath_s(exeDir, sizeof(exeDir), drive, dir, nullptr, nullptr) == 0) {
            g_exeDir = exeDir;
        }
        char iniPath[0x104];
        if (_makepath_s(iniPath, sizeof(iniPath), drive, dir, "MMEffect", ".ini") == 0) {
            g_iniPath = iniPath;
        }
    }

    // Phase 1 divergence: open/create MMEffect.txt in the exe directory
    // before any logging (see mme_log.h for the rationale).
    MmeLogInit(g_exeDir.c_str());

    // [L74-155] read MMEffect.ini when it exists.
    if (_access_s(g_iniPath.c_str(), 4) == 0) {
        IniFile ini;                               // global IniFile object [L76]
        if (ini.Load(g_iniPath.c_str())) {
            // [L80-101] [System] EMMAutoSave: default 1, cleared unless the
            // value is exactly "true" (DAT_1800b5208 compare).
            if (!MmeIniValueIsTrue(ini.GetString("System", "EMMAutoSave"))) {
                g_emmAutoSave = 0;
            }
            // [L120-154] [System] SkipValidation == "true" -> DAT_1800d99d9 = 1.
            if (MmeIniValueIsTrue(ini.GetString("System", "SkipValidation"))) {
                g_skipValidation = 1;
            }
        }
    }

    // [L156-157] ExpGetEnglishMode is resolved dynamically from the host exe.
    g_englishModeFn = reinterpret_cast<ExpGetEnglishModeFn>(
        GetProcAddress(GetModuleHandleA(nullptr), "ExpGetEnglishMode"));

    // [L158-165] host UI install (menu swap + subclass + WH_CBT hook) and the
    // timeGetTime frame base. Phase 2 seam returns true so the flow continues.
    g_mainWindow = GetMMDMainWindow();             // [L158] DAT_1800d9b00
    if (!MmeInstallUiHooks(0)) {                   // [L161-162] FUN_180055890
        return 1;
    }
    g_cbtHook = nullptr;                           // [L164] installed by mme_ui in Phase 2
    g_timeBase = timeGetTime();                    // [L165]
    g_lastTick = 0;                                // [L166]
    g_timeInitialized = 0;                         // [L167]

    // [L170-171] device sampler query + effect-pool/engine init.
    DWORD mipFilter = 0;
    if (device != nullptr) {
        // device slot 0x220 = GetSamplerState(1, 7 /*D3DSAMP_MIPFILTER*/).
        device->GetSamplerState(1, static_cast<D3DSAMPLERSTATETYPE>(7), &mipFilter);
    }
    MmeInitEffectEngine(device, mipFilter != 0);   // FUN_18000a8e0

    // [L172-177] MME context singleton (operator new(0x480) + FUN_18005b030).
    g_context = new MmeContext(device);            // DAT_1800d9bb8

    // [L178-185] effect-owner manager (operator new(400) + FUN_18002a220).
    g_ownerManager = new EffectOwnerManager();     // DAT_1800d9a40
    g_context->ownerManager = g_ownerManager;

    // [L186-190] banner.
    MmeLogWrite("MikuMikuEffect  ver.0.37\n\n", 0);

    // [L191-199] device/shader info line (info + "\n").
    std::string info = MmeBuildDeviceInfoString(device);
    info += "\n";
    MmeLogWrite(info.c_str(), 0);

    // [L203-222] resolve and cache the 17 effect parameter handles from the
    // current host effect via GetParameterByName(NULL, name) (slot 0x48).
    ID3DXEffect* effect = static_cast<ID3DXEffect*>(GetCurrentEffect());
    if (effect == nullptr) {
        return 1;                                  // [L233]
    }
    for (int i = 0; i < kParamCount; ++i) {
        g_paramHandles[i] = effect->GetParameterByName(nullptr, g_paramNames[i]);
    }
    g_currentEffect = effect;                      // [L222] DAT_1800d9918

    // [L223-229] cache D3DMATERIAL9 and force Diffuse.b / Diffuse.a >= 1.
    if (device != nullptr) {
        device->GetMaterial(&g_cachedMaterial);
    }
    if (g_cachedMaterial.Diffuse.b == 0) {
        g_cachedMaterial.Diffuse.b = 1;            // DAT_1800d9880
    }
    if (g_cachedMaterial.Diffuse.a == 0) {
        g_cachedMaterial.Diffuse.a = 1;            // DAT_1800d9884
    }

    return 0;                                      // [L230]
}

// [0x180058530] Cleanup.
void __cdecl Cleanup(IDirect3DDevice9* /*device*/)
{
    MmeLogWrite("Terminating MME...\n", 0);        // [L28-29]

    // [L33-41] free the effect-owner manager.
    if (g_ownerManager != nullptr) {
        delete g_ownerManager;
        g_ownerManager = nullptr;
    }

    // [L42-47] free the MME context (dtor runs MME_ModelData_Destructor for
    // every remaining model).
    if (g_context != nullptr) {
        delete g_context;
        g_context = nullptr;
    }

    // [L48] effect-engine teardown (FUN_18001eeb0: the effect/texture caches).
    MmeEngineTerm();

    // [L49-56] release the cached COM pointers.
    if (g_effectPool != nullptr) {
        g_effectPool->Release();
        g_effectPool = nullptr;
    }
    if (g_offscreenSurface != nullptr) {
        g_offscreenSurface->Release();
        g_offscreenSurface = nullptr;
    }

    // [L57-60] remove the CBT hook.
    if (g_cbtHook != nullptr) {
        UnhookWindowsHookEx(g_cbtHook);
        g_cbtHook = nullptr;
    }

    // [L61-74] restore the host UI. Phase 1 guards the window accesses (the
    // original dereferences DAT_1800d9b00 unconditionally; with the Phase 2
    // UI seam never having installed anything the handles are null here).
    if (g_mainWindow != nullptr) {
        HMENU menu = GetMenu(g_mainWindow);
        if (menu != nullptr) {
            DeleteMenu(menu, 0x65, 0);             // [L63]
            DeleteMenu(menu, 0x6b, 0);             // [L64]
        }
        g_menuState = 0;                           // [L65]
        DrawMenuBar(g_mainWindow);                 // [L66]
        if (g_mainOriginalWndProc != 0) {
            SetWindowLongPtrA(g_mainWindow, GWLP_WNDPROC, g_mainOriginalWndProc);  // [L67]
        }
    }
    if (g_offscreenWindow != nullptr && g_offscreenOriginalWndProc != 0) {
        SetWindowLongPtrA(g_offscreenWindow, GWLP_WNDPROC, g_offscreenOriginalWndProc);  // [L68-70]
    }
    g_offscreenWindow = nullptr;                   // [L71]
    g_offscreenOriginalWndProc = 0;                // [L72]
    g_mainOriginalWndProc = 0;                     // [L73]
    g_mainWindow = nullptr;                        // [L74]

    // [L75-84] the 96-dash separator line.
    std::string separator = MmeBuildSeparatorLine();
    MmeLogWrite(separator.c_str(), 0);
}

// [0x180057430] OnCreateModel.
void __cdecl OnCreateModel(IDirect3DDevice9* /*device*/, unsigned long long objectId,
                           const char* filename, int kind, unsigned long materialCount,
                           void* /*reserved0*/, IUnknown* reserved1)
{
    // [L14-18] SavedPMMFile loop -> EMM autosave (Phase 2 seam).
    const wchar_t* pmm = SavedPMMFile();
    while (pmm != nullptr) {
        MmeAutoSaveEmmForPmm(pmm);
        pmm = SavedPMMFile();
    }

    MmeLogFlush(1);                                // [L19] FUN_1800094e0(1)

    // [L20] MME_RegisterModelData.
    MmeRegisterModelData(objectId, filename, kind, materialCount, reserved1);
}

// [0x1800574a0] OnDeleteModel.
void __cdecl OnDeleteModel(IDirect3DDevice9* /*device*/, unsigned long long objectId)
{
    const wchar_t* pmm = SavedPMMFile();
    while (pmm != nullptr) {
        MmeAutoSaveEmmForPmm(pmm);
        pmm = SavedPMMFile();
    }

    MmeLogFlush(1);                                // [L18]

    // [L19] MME_UnregisterModelData.
    MmeUnregisterModelData(objectId);
}

// [0x1800574e0] OnBeginScene.
void __cdecl OnBeginScene(IDirect3DDevice9* device)
{
    const wchar_t* pmm = SavedPMMFile();           // [L32-36]
    while (pmm != nullptr) {
        MmeAutoSaveEmmForPmm(pmm);
        pmm = SavedPMMFile();
    }

    MmeLogFlush(1);                                // [L37]

    MmeUpdateFrameTime();                          // [L38] FUN_180056f80

    // [L39-45] cache D3DMATERIAL9, force Diffuse.b / Diffuse.a >= 1.
    if (device != nullptr) {
        device->GetMaterial(&g_cachedMaterial);
        if (g_cachedMaterial.Diffuse.b == 0) {
            g_cachedMaterial.Diffuse.b = 1;
        }
        if (g_cachedMaterial.Diffuse.a == 0) {
            g_cachedMaterial.Diffuse.a = 1;
        }
    }

    // [L46-49] cache the BeginScene viewport (vtable+0x180 GetViewport into
    // DAT_1800d9878); the FUN_180055b10 mouse tail divides by its extents,
    // so a zero extent is coerced to 1.
    if (device != nullptr) {
        device->GetViewport(&g_beginViewport);
        if (g_beginViewport.Width == 0) {
            g_beginViewport.Width = 1;
        }
        if (g_beginViewport.Height == 0) {
            g_beginViewport.Height = 1;
        }
    }

    // [L50-61] offscreen (drawn) window subclassing: the same 0x180055b10
    // proc as the main window (its mouse tail needs it); the previous proc is
    // restored first when one was installed.
    HWND drawn = GetDrawnWindow();
    if (drawn == g_mainWindow) {
        drawn = nullptr;
    }
    if (drawn != g_offscreenWindow) {
        if (g_offscreenWindow != nullptr) {
            // [L51-55] restore the previous offscreen proc.
            if (g_offscreenOriginalWndProc != 0) {
                SetWindowLongPtrA(g_offscreenWindow, GWLP_WNDPROC, g_offscreenOriginalWndProc);
            }
            g_offscreenOriginalWndProc = 0;
            g_offscreenWindow = nullptr;
        }
        if (drawn != nullptr) {
            MmeUiInstallOffscreenSubclass(drawn);
        }
    }

    if (device != nullptr) {
        // [L62-91] transform cache.
        device->GetTransform(D3DTS_WORLD, &g_worldAtBegin);                    // DAT_1800d9d30
        D3DXMatrixInverse(reinterpret_cast<D3DXMATRIX*>(&g_invWorldAtBegin), nullptr,
                          reinterpret_cast<const D3DXMATRIX*>(&g_worldAtBegin));           // DAT_1800d9d70
        device->GetTransform(D3DTS_VIEW, &g_worldViewMatrix);                  // DAT_1800d9db0 (view first)
        D3DXMatrixMultiply(reinterpret_cast<D3DXMATRIX*>(&g_worldViewMatrix),
                           reinterpret_cast<const D3DXMATRIX*>(&g_worldAtBegin),
                           reinterpret_cast<const D3DXMATRIX*>(&g_worldViewMatrix));       // = world * view
        device->GetTransform(D3DTS_PROJECTION, &g_projMatrix);                 // DAT_1800d9df0
        D3DXMatrixMultiply(reinterpret_cast<D3DXMATRIX*>(&g_viewProjMatrix),
                           reinterpret_cast<const D3DXMATRIX*>(&g_worldViewMatrix),
                           reinterpret_cast<const D3DXMATRIX*>(&g_projMatrix));            // DAT_1800d9e30

        // [L92] GetLight(0).
        device->GetLight(0, &g_cachedLight);
    }

    // [L93] MME_RebuildRenderPassPlan.
    MmeRebuildRenderPassPlan();

    // [L94] clear the inside-draw flag.
    g_insideModelDraw = 0;                         // DAT_1800d99df
}

// [0x180058400] OnEndScene.
void __cdecl OnEndScene(IDirect3DDevice9* /*device*/)
{
    MmeContext* ctx = g_context;
    // [L14-16] post-effect runner gated on ctx+0x13.
    if (ctx != nullptr && ctx->effectEnabled != 0) {
        MmeRunPostEffect(ctx);                     // FUN_18005e210
    }
    // [L17] animated-texture tick on ctx+0x240 (FUN_180001320).
    if (ctx != nullptr) {
        MmeTickAnimatedTextures(ctx);
    }
    // [L18] log flush.
    MmeLogFlush(0);
}

// [0x180058440] OnClear.
HRESULT __cdecl OnClear(IDirect3DDevice9* device, unsigned long rectCount,
                        const D3DRECT* rects, unsigned long flags, D3DCOLOR color,
                        float z, unsigned long stencil, int isMainTarget)
{
    // [L13-16] when effect rendering is enabled AND the clear targets the
    // main render target, the clear is suppressed (the post-effect runner
    // issues its own Clear(0, 0, 7) with the host clear color [18005e210
    // L75479-75501]); otherwise the call is forwarded to the device as-is.
    MmeContext* ctx = g_context;
    bool suppress = (ctx != nullptr && ctx->effectEnabled != 0 && isMainTarget != 0);
    if (!suppress && device != nullptr) {
        device->Clear(rectCount, rects, flags, color, z, stencil);
    }
    return 0;
}

// [0x180058490] OnDrawPrimitive.
HRESULT __cdecl OnDrawPrimitive(IDirect3DDevice9* device, D3DPRIMITIVETYPE primitiveType,
                                unsigned int startVertex, unsigned int primitiveCount)
{
    MmeHandleDrawPrimitive(device, static_cast<unsigned int>(primitiveType),
                           startVertex, primitiveCount);
    return 0;
}

// [0x1800584b0] OnDrawIndexedPrimitive.
HRESULT __cdecl OnDrawIndexedPrimitive(IDirect3DDevice9* device, D3DPRIMITIVETYPE primitiveType,
                                       int baseVertexIndex, unsigned int minVertexIndex,
                                       unsigned int vertexCount, unsigned int startIndex,
                                       unsigned int primitiveCount)
{
    // [L16-22] on the first tracked draw of a frame, capture the light
    // view/projection matrices once.
    if (g_insideModelDraw == 0) {
        int drawType = GetCurrentDrawType();
        if (drawType != 0) {
            g_insideModelDraw = 1;
            GetLightViewProjMatrix(&g_lightViewMatrix, &g_lightProjMatrix,
                                   &g_lightViewProjMatrix);
        }
    }

    // [L23] MME_HandleDrawIndexedPrimitive.
    MmeHandleDrawIndexedPrimitive(device, static_cast<unsigned int>(primitiveType),
                                  baseVertexIndex, minVertexIndex, vertexCount,
                                  startIndex, primitiveCount);
    return 0;
}

// [0x180058970] OnLostDevice.
void __cdecl OnLostDevice(IDirect3DDevice9* /*device*/)
{
    MmeLogWrite("Resetting MME...", 0);            // [L29-32]

    // [L40] release device-dependent resources (FUN_18005e640): the loaded
    // effects' OnLostDevice, then the binding-context state-block pool
    // (FUN_180055780 on ctx+0x68).
    MmeEngineOnLostDevice();
    MmeReleaseBindingContextPool(g_context);

    // [L41] FUN_18000a990 - clear the engine-side container pair
    // (DAT_1800d9c60/DAT_1800d9c80). PHASE 2 seam (effect_engine state).
    MmePassBookkeeping(g_context);

    // [L42-45] release the offscreen render-target surface.
    if (g_offscreenSurface != nullptr) {
        g_offscreenSurface->Release();
        g_offscreenSurface = nullptr;
    }
}

// [0x180058a20] OnResetDevice.
void __cdecl OnResetDevice(IDirect3DDevice9* device)
{
    // [L44] FUN_18002de40 - reacquire effect engine state: the loaded
    // effects' OnResetDevice (first failure wins, like the original chain).
    HRESULT reacquire = MmeEngineOnResetDevice(device);

    // [L54] re-create the 16x16 device-pool render target (slot 0xe0).
    HRESULT recreate = E_FAIL;
    if (device != nullptr) {
        if (g_offscreenSurface != nullptr) {
            g_offscreenSurface->Release();
            g_offscreenSurface = nullptr;
        }
        recreate = device->CreateRenderTarget(0x10, 0x10, static_cast<D3DFORMAT>(0x16),
                                              D3DMULTISAMPLE_NONE, 0, FALSE,
                                              &g_offscreenSurface, nullptr);
    }

    // [L49-61] the failure chain: first non-zero result wins.
    HRESULT failure = S_OK;
    if (FAILED(reacquire)) {
        failure = reacquire;
    }
    if (FAILED(recreate)) {
        failure = recreate;
    }

    if (SUCCEEDED(failure)) {
        // [L61-68] "done."
        MmeLogWrite("done.", 0);
        return;
    }

    // [L70-74] "failed."
    MmeLogWrite("failed.", 0);

    // [L78-136] localized message + dedup + MessageBoxA(main, msg,
    // "MikuMikuEffect", MB_ICONERROR). The Japanese string is byte-verified
    // SJIS at 0x1800b58d8: B3F5 CABBCBAF + ASCII "MikuMikuEffect" + CAA7B0DC.
    const char* message = MmeIsEnglishUiMode()
        ? "Failed to reset MikuMikuEffect"                 // 0x1800b5900
        : "\xB3\xF5\xCA\xBB\xCB\xAF" "MikuMikuEffect" "\xCA\xA7\xB0\xDC";  // 0x1800b58d8

    static std::string shownResetMessage;   // DAT_1800d99d8-gated shown list (approximate)
    if (shownResetMessage != message) {
        shownResetMessage = message;
        MessageBoxA(g_mainWindow, message, "MikuMikuEffect", MB_ICONERROR);
    }
}

} // extern "C"
