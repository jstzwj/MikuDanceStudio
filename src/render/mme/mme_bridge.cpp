#include "mikudancestudio/rhi_scene_history.hpp"
#include "mikudancestudio/rhi_history_replay.hpp"
#include "mikudancestudio/model_cpu_preview.hpp"
// mme_bridge.cpp - 内置 MMEffect 宿主桥实现。
//
// See include/mikudancestudio/mme_bridge.hpp. The effect engine is statically embedded in the host;
// all calls go directly to MmeHost*, without proxy DLLs or device hooks.
//
// [Verification conclusion, 2026-09] Original MMHack's user32!GetKeyState IAT hook in the host EXE
// ([0x180004910], installed unconditionally by DllMain through sub_18000ada0, regardless of MME initialization
// state) returns 0 when the queried key is 'E'(69) and GetKeyState('E'/VK_CONTROL/VK_SHIFT) all
// have their high bits set. However, this hook has no observable effect on MMD v932 (x64 and the matching x86 0x42D3A0 key table):
// the EXE's only GetKeyState call site is the per-frame keyboard polling dispatcher
// (x64 sub_7FF7CB446AD0 -> sub_7FF7CB446F50), whose fixed key table (arrows/Shift/
// Ctrl/Alt/Space/Delete/Esc/Tab/Enter/mouse/numpad/letters XZCV DABS GHKPUJFRL/
// VK 221,226) omits 'E'; there is no accelerator table (no LoadAccelerators/Translate-
// Accelerator imports) or GetAsyncKeyState/GetKeyboardState/RegisterHotKey.
// Loading MMHack therefore leaves MMD behavior unchanged; the hook is behaviorally dead code
// (MME's own hotkeys use WH_KEYBOARD + GetAsyncKeyState; see MmeKeyboardHookProc in mme_ui.cpp,
// which is unaffected by this IAT hook). The embedded version consequently needs no
// Ctrl+Shift+E suppression and should not add any. This project's polling table matches the original (frame_modes.
// cpp / model_query_helpers.cpp) and likewise never queries 'E'.
#include "mikudancestudio/panel_controls.hpp"
#include "mikudancestudio/mme_bridge.hpp"

#include <Windows.h>
#include <string.h>

#include "mikudancestudio/accessory_layout.hpp"
#include "mikudancestudio/d3d_wrapper.hpp"
#include "mikudancestudio/mmd_app.hpp"
#include "mme_host_api.h"
#include "mesh_subset.hpp"

// Internal direct access from src/exports/effect_api.cpp (extern "C", not exported): original model/accessory
// wide-character paths. Injected into MMEffect.dll through MmeHostSetPathProviders (equivalent to
// original MMHook's D3DXLoadMeshFromXW wide-path registration, without an SJIS round trip).
extern "C" const wchar_t* MmdModelPathW(int index);
extern "C" const wchar_t* MmdAcsPathW(int index);

namespace mikudancestudio {
namespace mme {
namespace {

// Device is registered with the embedded effect engine. Lazy Initialize checks standard-effect availability.
bool g_deviceRegistered = false;
#ifdef MIKUDANCESTUDIO_DIAG
LONG g_lineDrawCalls = 0;
LONG g_lineHostCalls = 0;
LONG g_lineDirectCalls = 0;
LONG g_triangleDrawCalls = 0;
LONG g_triangleHostCalls = 0;
LONG g_triangleDirectCalls = 0;
LONG g_indexedDrawCalls = 0;
LONG g_indexedHostCalls = 0;
LONG g_indexedDirectCalls = 0;
#endif
// 已注册给 MME 的主窗口（帧级刷新用；初始化期间 WM_CREATE 的句柄可能
// 尚未写回 app->Hwnd()）。
HWND g_registeredWindow = nullptr;

// ---- Equivalent state for sub_18000EFF0 (MMHack.dll, called by BeginScene each frame) --------
// 0x18006E840 (hWnd): cached target edit control. Original probing order: GetDlgItem(main window,
// 317) with class name "EDIT" (stricmp), otherwise fall back to GetDlgItem(main window, 417) (also
// requiring EDIT). If neither is EDIT, remain null and retry each frame. 317 is the frame edit ID in older
// MMD versions (cross-version fallback); MMD v932x64 has no control 317 (no immediate references to
// 317 in the EXE), and the port's main-window creation table ui_controls.inc covers only 400..567, so
// it always falls back to 417 (panel::kCurrentFrameEdit, class "EDIT").
HWND g_editModeControl = nullptr;
// 0x18006E7BD (byte_18006E7BD): sticky RecWindow (AVI recording window) flag.
bool g_recWindowSticky = false;

// 0x18000EF60 (function, EnumThreadWindows callback): a candidate top-level window whose class name strcmp
// exactly matches "RecWindow" sets found and stops enumeration. Only the class name matters; neither #32770
// modal dialogs nor IsWindowEnabled/parent-child/owner relationships are checked.
BOOL CALLBACK FindRecWindowProc(HWND candidate, LPARAM found) {
    char className[32];
    if (GetClassNameA(candidate, className, 32) != 0 && strcmp(className, "RecWindow") == 0) {
        *reinterpret_cast<bool*>(found) = true;
        return FALSE;
    }
    return TRUE;
}

// VA sub_18000EFF0: returns notEditMode (caller stores 0x18006E796; the IsEditMode
// export returns its inverse; mme_host.cpp writes frame time 0x18006E79C immediately afterward).
// Meaning: the target edit control is disabled (MMD disables 415..468, including 417, when playback starts;
// playback_state.cpp), or RecWindow exists and has latched. Timing: the frame in which RecWindow
// appears is still edit mode and sets the latch; non-edit mode starts next frame. Disappearance clears the latch that frame,
// and the return value then depends only on the control's enabled state. IsWindowEnabled(NULL)==FALSE: if no target EDIT
// control exists in the main window, the mode is always non-edit (as in the original).
int ProbeNotEditMode(HWND mainWindow) {
    if (g_editModeControl == nullptr) {
        char className[32];
        HWND probe = GetDlgItem(mainWindow, 317);
        if (probe == nullptr || GetClassNameA(probe, className, 32) == 0 ||
            _stricmp(className, "EDIT") != 0)
            probe = nullptr;
        if (probe == nullptr) {
            // The original also writes dword_18006CCB8 = 14208 here (dead store with no consumers,
            // not ported).
            probe = GetDlgItem(mainWindow, panel::kCurrentFrameEdit);
            if (probe != nullptr &&
                (GetClassNameA(probe, className, 32) == 0 || _stricmp(className, "EDIT") != 0))
                probe = nullptr;
        }
        g_editModeControl = probe;
    }
    bool recPresent = false;
    EnumThreadWindows(GetCurrentThreadId(), FindRecWindowProc,
                      reinterpret_cast<LPARAM>(&recPresent));
    if (!recPresent)
        g_recWindowSticky = false;
    if (IsWindowEnabled(g_editModeControl) == FALSE)
        return 1;
    if (recPresent) {
        if (g_recWindowSticky)
            return 1;
        g_recWindowSticky = true;
    }
    return 0;
}

} // namespace

// ---------------------------------------------------------------------------
// Device lifecycle
// ---------------------------------------------------------------------------
// [Final verdict, 2026-09-14 recheck; retracting the first audit's misread slots] Key slots in original
// MMHack.dll MyDirect3DDevice9 wrapper vtable at 0x18005c058
// (confirmed both by reading the vtable and disassembling forwarding targets):
//   slot 15(0x78) 0x180003ed0 = GetNumberOfSwapChains, pure forwarding jmp [real+0x78]
//                (misidentified as "Reset passthrough" in the early audit);
//   slot 16(0x80) 0x180003d30 = Reset interception: MME OnLostDevice(0x180058970,
//                prints "Resetting MME...") -> real Reset call [real+0x80] ->
//                MME OnResetDevice(0x180058A20) → qword_18006E7C8 =
//                d3dpp.hDeviceWindow(+0x20), or if null, hFocusWindow(+0x08) from GetCreationParameters
//                [real+0x48];
//   slot 17(0x88) 0x180003cf0 = Present interception: only records qword_18006E7C0 =
//                hDestWindowOverride non-null ? override : qword_18006E7C8
//                (source documented inside BeginScene), then calls [real+0x88],
//                without any MME notification;
//   slot 18(0x90) 0x180001ef0 = GetBackBuffer passthrough call [real+0x90].
// MMD side (x64): main loop sub_7FF7CB4474F0 calls [dev+0x88] Present every frame;
// only when Present returns D3DERR_DEVICELOST(0x88760868) and TestCooperativeLevel
// ([dev+0x18]) polling reaches D3DERR_DEVICENOTRESET(0x88760869) does it call
// sub_7FF7CB4C6160 -> [dev+0x80] Reset (other Reset entry points also use slot 16).
// Thus MME OnLostDevice/OnResetDevice are triggered only by actual device Reset;
// the old claim "slot 16 = Present, every Present triggers Resetting MME" (confusing adjacent slots
// 16/17) is false. OnLost/OnReset require both export pointers to be non-null
// (0x18006e828/0x18006e830). Equivalent bridge behavior: no Present notifications (see
// frame_driver.cpp section 9), one notification before and after Reset (device_reset.cpp,
// OnResetDevice immediately after Reset, before InitRenderStates).
void OnDeviceCreated(MMDApp* app, HWND hwnd) {
    g_deviceRegistered = false;
    g_registeredWindow = nullptr;
#ifdef MIKUDANCESTUDIO_DIAG
    // Isolate a fixed-function material batch from MME for the model A/B
    // fixture. Normal application startup never sets this diagnostic flag.
    char noMme[2]{};
    if (GetEnvironmentVariableA("MDS_RHI_MODEL_NO_MME", noMme, 2) == 1 && noMme[0] == '1')
        return;
#endif
    D3DRenderer* renderer = app->Renderer();
    if (renderer == nullptr || renderer->device == nullptr)
        return;
    // Even with no standard effect, register the device and enter the same
    // lazy Initialize state machine as every other initialization failure.
    // MMHack 0x180002E35..0x180002E8F retries on each BeginScene, reports the
    // error, leaves initialization pending, and returns S_OK without starting
    // the real scene. There is deliberately no second failure latch here.
    // InitD3D runs during WM_CREATE, while app->Hwnd() is still null; use the argument handle
    // (falling back to app->Hwnd() is only defensive).
    HWND window = hwnd;
    if (window == nullptr)
        window = static_cast<HWND>(app->Hwnd());
    MmeHostSetPathProviders(&MmdModelPathW, &MmdAcsPathW);
    MmeHostSetMainWindow(window);
    MmeHostSetDrawnWindow(window);
    MmeHostSetStandardEffect(renderer->effect);
    g_registeredWindow = window;
    g_deviceRegistered = true;
}

void OnDeviceDestroyed(MMDApp* app) {
    if (!g_deviceRegistered)
        return;
    D3DRenderer* renderer = app->Renderer();
    MmeHostDestroyDevice(renderer != nullptr ? renderer->device : nullptr);
    g_deviceRegistered = false;
}

void OnLostDevice(MMDApp* app) {
    if (!g_deviceRegistered)
        return;
    D3DRenderer* renderer = app->Renderer();
    if (renderer != nullptr)
        MmeHostOnLostDevice(renderer->device);
}

void OnResetDevice(MMDApp* app) {
    if (!g_deviceRegistered)
        return;
    D3DRenderer* renderer = app->Renderer();
    if (renderer != nullptr)
        MmeHostOnResetDevice(renderer->device);
}

// ---------------------------------------------------------------------------
// Per frame
// ---------------------------------------------------------------------------
HRESULT ClearScene(MMDApp* app, IDirect3DDevice9* device, unsigned long flags, D3DCOLOR color,
                   float z, unsigned long stencil) {
#ifdef MIKUDANCESTUDIO_DIAG
    BlockHistoryReplay("additional_scene_clear");
#endif
    if (!g_deviceRegistered)
        return device->Clear(0, nullptr, flags, color, z, stencil);
    return MmeHostClear(device, 0, nullptr, flags, color, z, stencil);
}

HRESULT BeginScene(MMDApp* app, IDirect3DDevice9* device) {
    if (!g_deviceRegistered)
        return device->BeginScene();
    // Refresh the main-window handle (initialization fallback: update a registered handle that differs from the current one;
    // this must take effect before MME lazy Initialize).
    HWND window = static_cast<HWND>(app->Hwnd());
    if (window != nullptr && window != g_registeredWindow) {
        MmeHostSetMainWindow(window);
        g_registeredWindow = window;
    }
    // Original semantics (sub_18000EFF0 / MmhProbeEditMode): a disabled frame edit control (317 falling back to
    // 417), or a RecWindow-class window (windowed AVI recording; fullscreen 3D Vision
    // recording uses RecordingWindow==main window, whose class is not RecWindow and does not qualify) present
    // for a full frame and latched -> non-edit mode. Playback disables 415..468, so during playback/pause
    // notEditMode=1; modal dialogs do not qualify (the callback matches only class RecWindow).
    const int notEditMode = ProbeNotEditMode(g_registeredWindow);
    return MmeHostBeginScene(device, notEditMode);
}

HRESULT EndScene(MMDApp* app, IDirect3DDevice9* device) {
    if (!g_deviceRegistered)
        return device->EndScene();
    return MmeHostEndScene(device);
}

HRESULT Present(MMDApp* app, IDirect3DDevice9* device, const RECT* source, const RECT* destination,
                HWND destinationWindow, const RGNDATA* dirtyRegion) {
    if (g_deviceRegistered) {
        MmeHostSetDrawnWindow(destinationWindow != nullptr ? destinationWindow
                                                           : static_cast<HWND>(app->Hwnd()));
    }
    return device->Present(source, destination, destinationWindow, dirtyRegion);
}

void PreRenderTargetCopy(MMDApp* app, IDirect3DDevice9* device, IDirect3DSurface9* source) {
    if (!g_deviceRegistered)
        return;
    MmeHostPreRenderTargetCopy(device, source);
}

// ---------------------------------------------------------------------------
// Draw forwarding
// ---------------------------------------------------------------------------
HRESULT DrawPrimitive(IDirect3DDevice9* device, D3DPRIMITIVETYPE type, unsigned int start_vertex,
                      unsigned int primitive_count) {
#ifdef MIKUDANCESTUDIO_CPU_RENDERING
    RecordHistoryDraw(device, type, false, 0, 0, 0, start_vertex, primitive_count);
#endif
#ifdef MIKUDANCESTUDIO_DIAG
    AuditDiagnosticHistoryDraw(device, type, primitive_count, false);
    if (type == D3DPT_LINELIST) {
        InterlockedIncrement(&g_lineDrawCalls);
        InterlockedIncrement(g_deviceRegistered ? &g_lineHostCalls : &g_lineDirectCalls);
    }
    if (type == D3DPT_TRIANGLELIST) {
        InterlockedIncrement(&g_triangleDrawCalls);
        InterlockedIncrement(g_deviceRegistered ? &g_triangleHostCalls : &g_triangleDirectCalls);
    }
#endif
    const HRESULT result = !g_deviceRegistered
                               ? device->DrawPrimitive(type, start_vertex, primitive_count)
                               : MmeHostDrawPrimitive(device, type, start_vertex, primitive_count);
#ifdef MIKUDANCESTUDIO_CPU_RENDERING
    if (FAILED(result))
        BlockHistoryReplay("primitive_submission_failed");
#endif
    return result;
}

#ifdef MIKUDANCESTUDIO_DIAG
LineDrawBridgeProbe ProbeLineDrawBridge() {
    return {static_cast<unsigned long>(g_lineDrawCalls),
            static_cast<unsigned long>(g_lineHostCalls),
            static_cast<unsigned long>(g_lineDirectCalls)};
}
LineDrawBridgeProbe ProbeTriangleDrawBridge() {
    return {static_cast<unsigned long>(g_triangleDrawCalls),
            static_cast<unsigned long>(g_triangleHostCalls),
            static_cast<unsigned long>(g_triangleDirectCalls)};
}

DrawSnapshotProbe ProbeDrawSnapshot() {
    const auto state = MmeHostProbeDrawSnapshot();
    return {state.pending,       state.effectEnabled,   state.bindingReady,   state.bindingActive,
            state.endSceneFired, state.effectsDisabled, state.lineDisposition};
}
#endif

HRESULT DrawIndexedPrimitive(IDirect3DDevice9* device, D3DPRIMITIVETYPE type, int base_vertex_index,
                             unsigned int min_vertex_index, unsigned int vertex_count,
                             unsigned int start_index, unsigned int primitive_count) {
#ifdef MIKUDANCESTUDIO_CPU_RENDERING
    RecordHistoryDraw(device, type, true, base_vertex_index, min_vertex_index, vertex_count,
                      start_index, primitive_count);
#endif
#ifdef MIKUDANCESTUDIO_DIAG
    AuditDiagnosticHistoryDraw(device, type, primitive_count, true);
    InterlockedIncrement(&g_indexedDrawCalls);
    InterlockedIncrement(g_deviceRegistered ? &g_indexedHostCalls : &g_indexedDirectCalls);
#endif
    const HRESULT result =
        !g_deviceRegistered
            ? device->DrawIndexedPrimitive(type, base_vertex_index, min_vertex_index, vertex_count,
                                           start_index, primitive_count)
            : MmeHostDrawIndexedPrimitive(device, type, base_vertex_index, min_vertex_index,
                                          vertex_count, start_index, primitive_count);
#ifdef MIKUDANCESTUDIO_CPU_RENDERING
    if (FAILED(result))
        BlockHistoryReplay("indexed_submission_failed");
#endif
    return result;
}

bool EffectsActive() {
    return g_deviceRegistered && MmeHostEffectsActive() != 0;
}
bool EffectsEnabled() {
    return g_deviceRegistered && MmeHostEffectsEnabled() != 0;
}
#ifdef MIKUDANCESTUDIO_DIAG
LineDrawBridgeProbe ProbeIndexedDrawBridge() {
    return {static_cast<unsigned long>(g_indexedDrawCalls),
            static_cast<unsigned long>(g_indexedHostCalls),
            static_cast<unsigned long>(g_indexedDirectCalls)};
}
#endif

// ---------------------------------------------------------------------------
// Accessory mesh drawing (observable equivalent of ID3DXMesh::DrawSubset)
// ---------------------------------------------------------------------------
HRESULT DrawAccessorySubset(MMDApp* app, void* accessory, unsigned long material) {
    IDirect3DDevice9* device = app->Renderer()->device;
    auto* mesh = static_cast<ID3DXMesh*>(mdl::Accessory(accessory)->mesh);
    if (mesh == nullptr)
        return E_FAIL;
    if (!EffectsActive()) {
#ifdef MIKUDANCESTUDIO_CPU_RENDERING
        RecordHistoryMesh(device, mesh, material);
#endif
        const HRESULT result = mesh->DrawSubset(material);
#ifdef MIKUDANCESTUDIO_CPU_RENDERING
        if (FAILED(result))
            BlockHistoryReplay("mesh_submission_failed");
#endif
        return result;
    }
#ifdef MIKUDANCESTUDIO_CPU_RENDERING
    // An empty CPU subset produces no primitive callback on the initialized
    // bridge route. Consume its explicit no-op before entering that route.
    if (CpuSceneFrameRecording())
        ConsumeCpuSceneEmptyMesh();
#endif

    IDirect3DVertexBuffer9* vb = nullptr;
    IDirect3DIndexBuffer9* ib = nullptr;
    HRESULT hr = mesh->GetVertexBuffer(&vb);
    if (FAILED(hr) || vb == nullptr)
        return FAILED(hr) ? hr : E_FAIL;
    hr = mesh->GetIndexBuffer(&ib);
    if (FAILED(hr) || ib == nullptr) {
        vb->Release();
        return FAILED(hr) ? hr : E_FAIL;
    }

    hr = DrawMeshSubsetWithFallback(*mesh, *device, *vb, *ib, material, [&] {
        MeshSubsetPlan plan;
        HRESULT planned = PlanMeshSubset(*mesh, material, plan);
        if (FAILED(planned))
            return planned;
        // Accessory loading clones to the host FVF (XYZ/NORMAL/TEX1). Setting
        // that FVF selects the same declaration as the mesh's DrawSubset.
        device->SetFVF(mesh->GetFVF());
        device->SetStreamSource(0, vb, 0, mesh->GetNumBytesPerVertex());
        device->SetIndices(ib);
        return DrawMeshSubsetPlan(plan, mesh->GetNumFaces(), (mesh->GetOptions() & 1) != 0,
                                  [device](const D3DXATTRIBUTERANGE& range) {
                                      return DrawIndexedPrimitive(
                                          device, D3DPT_TRIANGLELIST, 0, range.VertexStart,
                                          range.VertexCount, range.FaceStart * 3, range.FaceCount);
                                  });
    });
    vb->Release();
    ib->Release();
    return hr;
}

// ---------------------------------------------------------------------------
// File notifications
// ---------------------------------------------------------------------------
void NotifyPmmLoaded(MMDApp* app) {
    if (!g_deviceRegistered)
        return;
    MmeHostNotifyPmmLoaded(app->EnvFileName());
}

void NotifyPmmSaved(MMDApp* app) {
    if (!g_deviceRegistered)
        return;
    MmeHostNotifyPmmSaved(app->EnvFileName());
}

void RecordTexture(const wchar_t* path, IDirect3DBaseTexture9* texture) {
    if (!g_deviceRegistered || path == nullptr || texture == nullptr)
        return;
    MmeHostRecordTextureFile(path, texture);
}

} // namespace mme
} // namespace mikudancestudio
