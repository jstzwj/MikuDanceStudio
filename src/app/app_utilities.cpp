// ===========================================================================
// MikuDanceStudio - app/render gap-fill bodies (11 original functions, one file)
// ===========================================================================
// Contents (original VAs):
//   0x0040A680  AngleFabsF          float fabs wrapper (CRT fabs)
//   0x0040A690  AngleAsinF          float asin wrapper (__CIasin)
//   0x0040A6B0  AngleAtan2F         float atan2 wrapper (cintrin "atan2")
//   0x0040A6D0  AngleCosF           float cos wrapper (__CIcos)
//   0x0040AF40  MakeLineGeometry    axes/grid VB+IB + ground quad VB
//   0x0040E3D0  UpdateKeyEdgeState  GetKeyState edge-detect state machine
//   0x0040E440  ShowWin32ErrorMessage  FormatMessageA error box
//   0x0040E4E0  DrawGridLines       grid LINELIST draw (VB@768 / IB@772)
//   0x0040E5A0  DrawGroundPolygon   ground quad draw (VB@651560)
//   0x00441070  JumpNextKeyframe    timeline "next registration" jump
//   0x004414C0  JumpPrevKeyframe    timeline "previous registration" jump
//
// Naming notes:
//   * Angle*F are exact float-in/float-out twins of the double-returning
//     AngleFabs/AngleAsin/AngleCos/AngleAtan2 helpers already defined in
//     src/window/command_view_menu.cpp (external linkage there - these
//     bodies must use distinct symbols).
//   * MakeLineGeometry is the bit-exact body of 0x40AF40.  The earlier
//     port src/render/grid_geometry.cpp (InitGridGeometry) covers only the
//     line VB/IB, drops the ground-quad VB at app+651560 and the Japanese
//     message variant - keep both until callers are rewired.
//   * JumpNextKeyframe / JumpPrevKeyframe are the REAL bodies behind the
//     no-op stubs 0x441070 / 0x4414C0 in src/app/late_ports.cpp; the
//     stub symbols still exist, so these live under new names.
// =========================================================================//
#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <Windows.h>
#include <d3d9.h>

#include <cmath>
#include <cstdint>
#include <cstdio>

#include "mikudancestudio/mmd_app.hpp"
#include "mikudancestudio/ported_funcs.hpp"
#include "mikudancestudio/model.hpp"
#include "mikudancestudio/panel_controls.hpp"

namespace mikudancestudio {

// Already-ported callees (real bodies; PostViewRefresh is declared in
// ported_funcs.hpp, RefreshAfterFrameApply lives in
// src/window/ui_frame_step.cpp).
void RefreshAfterFrameApply(MMDApp* app);   // VA 0x00432FA0
                                            // frame-apply refresh chain

namespace {

// D3D device of the 0x1D574 render wrapper: *(this+657092)->device
// (wrapper+120032 / 0x1D4E0), same access idiom as
// src/render/toon_textures.cpp.
IDirect3DDevice9* RenderDeviceOf(MMDApp* app) {
    D3DRenderer* r = app->Renderer();
    if (r == nullptr)
        return nullptr;
    return r->device;  // +120032 (0x1D4E0)
}

// Line/ground vertex: D3DFVF_XYZ|D3DFVF_DIFFUSE (0x42), 16 bytes.
struct LineVertex {
    float x;
    float y;
    float z;
    std::uint32_t color;
};
static_assert(sizeof(LineVertex) == 16);

// Original Japanese message @0x52A294/0x52A234/0x52A1F8 (Shift-JIS):
// "グラフィックカードの性能が足りません(MakeLine[23])"
const char kJpMakeLine[] =
    "\203O\203\211\203t\203B\203b\203N\203J\201[\203h"
    "\202\314\220\253\224\\\202\252\221\253\202\350\202\334\202\271\202\361";

// --- 0x40AF40 message helper: EN/JP selection via app+658252 -----------
void MakeLineFailBox(MMDApp* app, const char* jpSuffix, const char* title) {
    char text[256];
    if (app->state.englishUI != 0)
        sprintf_s(text, 0x100, "%s",
                  "The performance of the graphics card doesn't suffice.");
    else
        sprintf_s(text, 0x100, "%s%s", kJpMakeLine, jpSuffix);
    MessageBoxA(static_cast<HWND>(app->Hwnd()), text, title, 0);
}

}  // namespace

// ===========================================================================
// VA 0x0040A680 - AngleFabsF  (original: sub_40A680)
// ===========================================================================
// fld st,arg0 / fabs / fstp dword: float in, float out.  Only caller in the
// binary: sub_47E8A0 (bone-rotation dialog math, case 300).
// ===========================================================================
float AngleFabsF(float v) {  // 0x40A680
    return std::fabs(v);
}

// ===========================================================================
// VA 0x0040A690 - AngleAsinF  (original: sub_40A690)
// ===========================================================================
// fld arg0 / call __CIasin / fstp dword: the x87 intrinsic computes at
// double precision, the wrapper truncates the result to float.
// ===========================================================================
float AngleAsinF(float v) {  // 0x40A690
    return static_cast<float>(std::asin(static_cast<double>(v)));
}

// ===========================================================================
// VA 0x0040A6B0 - AngleAtan2F  (original: sub_40A6B0)
// ===========================================================================
// fld arg0 / fld arg1 / call sub_50791A.  0x50791A jumps through the CRT
// __cintrindisp2 table whose entry at 0x544C40 is tagged "atan2", so the
// helper computes atan(st1/st0) = atan2(arg0, arg1) at double precision
// and rounds to float before returning.
// ===========================================================================
float AngleAtan2F(float a, float b) {  // 0x40A6B0
    return static_cast<float>(std::atan2(static_cast<double>(a),
                                         static_cast<double>(b)));
}

// ===========================================================================
// VA 0x0040A6D0 - AngleCosF  (original: sub_40A6D0)
// ===========================================================================
// fld arg0 / call __CIcos / fstp dword - double-precision cos, float out.
// ===========================================================================
float AngleCosF(float v) {  // 0x40A6D0
    return static_cast<float>(std::cos(static_cast<double>(v)));
}

// ===========================================================================
// VA 0x0040AF40 - MakeLineGeometry  (original: sub_40AF40, __usercall ecx=app)
// ===========================================================================
// Builds the three fixed debug-geometry buffers on the D3D device
// (*(this+657092)+120032):
//   1. line VB @app+768 (kDword300): 90 XYZ|DIFFUSE verts (1440 bytes,
//      D3DUSAGE_WRITEONLY=8, FVF 0x42, POOL MANAGED).  10 axis verts
//      (blue 0xFF000001 Z-, red X+, green Y+, gray +50/-50 stubs) then 10
//      iterations x 8 grid verts (x = i*5 .. i*-5, plane y=0.1, +/-50,
//      gray 0xFFB4B4B4).  A previous VB at +768 is released first.
//   2. line IB @app+772 (kLineIndexBufferSlot): 90 sequential WORD indices (180
//      bytes, D3DFMT_INDEX16=101, MANAGED).
//   3. ground VB @app+651560 (kGroundVertexBufferSlot): 6 verts (96 bytes) forming the
//      100x100 white quad at y=0 - creation/lock results unchecked, just
//      like the original.
// Every checked failure raises MessageBoxA(app+657080, EN-or-JP text,
// "MakeLine"/"MakeLine2"/"MakeLine3", 0) and returns false.
// Note: src/render/grid_geometry.cpp (InitGridGeometry) is an earlier,
// incomplete take on the same VA - this is the full body.
// ===========================================================================
bool MakeLineGeometry(MMDApp* app) {  // 0x40AF40
    auto& s = *app;
    IDirect3DDevice9* device = RenderDeviceOf(app);

    // 1. line vertex buffer (release stale one first) ---------------------
    IDirect3DVertexBuffer9*& lineVb = s.GroundGridVertices();       // 768
    if (lineVb != nullptr) {
        lineVb->Release();
        lineVb = nullptr;
    }
    if (FAILED(device->CreateVertexBuffer(1440, 8 /*D3DUSAGE_WRITEONLY*/,
                                          0x42 /*XYZ|DIFFUSE*/,
                                          D3DPOOL_MANAGED /*1*/,
                                          &lineVb, nullptr))) {
        MakeLineFailBox(&s, "(MakeLine)", "MakeLine");              // 0x40afca
        return false;
    }

    LineVertex* verts = nullptr;
    lineVb->Lock(0, 1440, reinterpret_cast<void**>(&verts), 0);     // 0x40b00b
    // x64 0x7FF7CB42F4C8 / 0x7FF7CB42F4F4: 0xFF0000FF, not 0xFF000001.
    constexpr std::uint32_t kAxisB = 0xFF0000FFu;   // -16776961 (blue)
    constexpr std::uint32_t kAxisR = 0xFFFF0000u;   // -65536    (red)
    constexpr std::uint32_t kAxisG = 0xFF00FF00u;   // -16711936 (green)
    constexpr std::uint32_t kGrid  = 0xFFB4B4B4u;   // -4934476  (gray)

    verts[0]  = {0.0f, 0.1f, 0.0f, kAxisB};    // 0x40b013..
    verts[1]  = {0.0f, 0.1f, -65.0f, kAxisB};
    verts[2]  = {0.0f, 0.1f, 0.0f, kAxisR};
    verts[3]  = {65.0f, 0.1f, 0.0f, kAxisR};
    verts[4]  = {0.0f, 0.1f, 0.0f, kAxisG};
    verts[5]  = {0.0f, 65.0f, 0.0f, kAxisG};    // x64 0x7FF7CB42F584: Y=65
    verts[6]  = {0.0f, 0.1f, 0.0f, kGrid};
    verts[7]  = {0.0f, 0.1f, 50.0f, kGrid};
    verts[8]  = {0.0f, 0.1f, 0.0f, kGrid};
    verts[9]  = {-50.0f, 0.1f, 0.0f, kGrid};

    for (int t = 1; t <= 10; ++t) {             // 0x40b1c2 loop, v39 = 1..10
        const float p = static_cast<float>(t) * 5.0f;
        const float n = static_cast<float>(t) * -5.0f;
        LineVertex* w = verts + 10 + 8 * (t - 1);
        w[0] = {p, 0.1f, -50.0f, kGrid};        // 0x40b1d6..
        w[1] = {p, 0.1f, 50.0f, kGrid};
        w[2] = {n, 0.1f, -50.0f, kGrid};
        w[3] = {n, 0.1f, 50.0f, kGrid};
        w[4] = {-50.0f, 0.1f, p, kGrid};
        w[5] = {50.0f, 0.1f, p, kGrid};
        w[6] = {-50.0f, 0.1f, n, kGrid};
        w[7] = {50.0f, 0.1f, n, kGrid};
    }
    lineVb->Unlock();                            // 0x40b38d

    // 2. line index buffer -----------------------------------------------
    IDirect3DIndexBuffer9*& lineIb = s.GroundGridIndices();         // 772
    if (FAILED(device->CreateIndexBuffer(180, 0,
                                         D3DFMT_INDEX16 /*101*/,
                                         D3DPOOL_MANAGED /*1*/,
                                         &lineIb, nullptr))) {      // 0x40b3b5
        MakeLineFailBox(&s, "(MakeLine2)", "MakeLine2");
        return false;
    }
    void* idxRaw = nullptr;
    if (FAILED(lineIb->Lock(0, 0, &idxRaw, 0))) {                   // 0x40b402
        MakeLineFailBox(&s, "(MakeLine3)", "MakeLine3");
        return false;
    }
    auto* indices = static_cast<std::uint16_t*>(idxRaw);
    for (int i = 0; i < 90; ++i)                                   // 0x40b447
        indices[i] = static_cast<std::uint16_t>(i);
    lineIb->Unlock();                                              // 0x40b468

    // 3. ground quad VB @app+651560 (unchecked, like the original) --------
    IDirect3DVertexBuffer9*& groundVb = s.GroundPlaneVertices();    // 651560
    device->CreateVertexBuffer(96, 8 /*WRITEONLY*/, 0x42,
                               D3DPOOL_MANAGED, &groundVb, nullptr);
    LineVertex* ground = nullptr;
    groundVb->Lock(0, 96, reinterpret_cast<void**>(&ground), 0);    // 0x40b4a2
    constexpr std::uint32_t kWhite = 0x00FFFFFFu;
    ground[0] = {-50.0f, 0.0f, -50.0f, kWhite};   // 0x40b4ae..
    ground[1] = {-50.0f, 0.0f, 50.0f, kWhite};
    ground[2] = {50.0f, 0.0f, -50.0f, kWhite};
    ground[3] = {50.0f, 0.0f, -50.0f, kWhite};
    ground[4] = {-50.0f, 0.0f, 50.0f, kWhite};
    ground[5] = {50.0f, 0.0f, 50.0f, kWhite};
    groundVb->Unlock();                                            // 0x40b578
    return true;
}

// ===========================================================================
// VA 0x0040E3D0 - UpdateKeyEdgeState  (original: sub_40E3D0, thiscall)
// ===========================================================================
// Per-key debounce state machine driven from the frame loop
// (0x46FF02 -> sub_42D3A0 -> here, 63 call sites).  State values:
//   0 idle, 1 pressed, 2 released, 3 held.
// Down test is (GetKeyState(vk) & 0x80) == 0x80 (low-byte bit 7, exactly
// as assembled: `and al, 80h / cmp al, 80h`).  Every 0->1 and 1/3->2
// transition additionally arms the generic input flag at +658796
// (kGenericInputFlagSlot).
// ===========================================================================
void UpdateKeyEdgeState(MMDApp* app, int nVirtKey,
                        std::uint32_t* state) {  // 0x40E3D0
    if ((GetKeyState(nVirtKey) & 0x80) == 0x80) {                  // 0x40e3de
        if (*state != 0) {
            *state = 3;                                            // 0x40e401
        } else {
            *state = 1;                                            // 0x40e3ed
            app->state.messageSeen = 1;      // 658796
        }
    } else if (*state == 1 || *state == 3) {
        *state = 2;                                                // 0x40e425
        app->state.messageSeen = 1;
    } else {
        *state = 0;                                                // 0x40e41b
    }
}

// ===========================================================================
// VA 0x0040E440 - ShowWin32ErrorMessage  (original: sub_40E440, thiscall)
// ===========================================================================
// GetLastError() reporter of the Kinect/DxOpenNI loader (sub_429CB0):
// FormatMessageA(ALLOCATE|FROM_SYSTEM|IGNORE_INSERTS, lang 0x400) into a
// LocalAlloc buffer, then MessageBoxA(main HWND @+657080, "<ctx>: <id>:
// <msg>\n", "error", 0) and LocalFree.
// ===========================================================================
void ShowWin32ErrorMessage(MMDApp* app, const char* context,
                           DWORD messageId) {  // 0x40E440
    char* message = nullptr;
    char text[256];
    FormatMessageA(0x1300, nullptr, messageId, 0x400,
                   reinterpret_cast<LPSTR>(&message), 0, nullptr);  // 0x40e47d
    sprintf_s(text, 0x100, "%s: %d:%s\n", context, messageId,
              message);                                            // 0x40e49a
    MessageBoxA(static_cast<HWND>(app->Hwnd()), text, "error", 0);  // 0x40e4b4
    LocalFree(message);                                            // 0x40e4c5
}

// ===========================================================================
// VA 0x0040E4E0 - DrawGridLines  (original: sub_40E4E0, thiscall)
// ===========================================================================
// Issues the grid/axes LINELIST built by 0x40AF40:
//   SetRenderState(D3DRS_LIGHTING=137, 0), SetTexture(0, null),
//   SetFVF(0x42), SetStreamSource(0, VB@768, 0, 16),
//   SetIndices(IB@772),
//   return DrawIndexedPrimitive(LINELIST, 0, 0, 90, 0, 45).
// Callers: RenderModelsFixed (0x425D20) / RenderModelsEffect (0x4277E0).
// ===========================================================================
HRESULT DrawGridLines(MMDApp* app) {  // 0x40E4E0
    IDirect3DDevice9* device = RenderDeviceOf(app);
    device->SetRenderState(D3DRS_LIGHTING /*137*/, 0);              // 0x40e4ff
    device->SetTexture(0, nullptr);                                 // 0x40e51a
    device->SetFVF(0x42 /*D3DFVF_XYZ|D3DFVF_DIFFUSE*/);             // 0x40e533
    device->SetStreamSource(
        0, app->GroundGridVertices() /*768*/,
        0, 16);                                                     // 0x40e557
    device->SetIndices(app->GroundGridIndices() /*772*/);           // 0x40e575
    return device->DrawIndexedPrimitive(D3DPT_LINELIST /*2*/, 0, 0, 90,
                                        0, 45);                     // 0x40e59a
}

// ===========================================================================
// VA 0x0040E5A0 - DrawGroundPolygon  (original: sub_40E5A0, thiscall)
// ===========================================================================
// Draws the white ground quad (VB@651560, 2 TRIANGLELIST prims) with
// stencil gating read from the render wrapper at *(this+657092):
//   if (byte@sub+120164) SetRenderState(STENCILENABLE=52, 0);
//   SetStreamSource(0, VB@651560, 0, 16); SetFVF(0x42);
//   DrawPrimitive(TRIANGLELIST, 0, 2);
//   SetRenderState(ALPHABLENDENABLE=27, 0);
//   if (byte@sub+120164) return SetRenderState(STENCILENABLE, 1);
//   return the sub pointer value (original leaves it in eax).
// Callers: RenderModelsFixed (0x425D20) / RenderModelsEffect (0x4277E0).
// ===========================================================================
std::uintptr_t DrawGroundPolygon(MMDApp* app) {  // 0x40E5A0
    D3DRenderer* r = app->Renderer();
    IDirect3DDevice9* device = r->device;  // +120032 (0x1D4E0)
    if (r->d3dInitialized != 0)   // wrapper+120164 (0x1D564)          0x40e5a9
        device->SetRenderState(D3DRS_STENCILENABLE /*52*/, 0);
    device->SetStreamSource(
        0, app->GroundPlaneVertices() /*651560*/,
        0, 16);                                                     // 0x40e5eb
    device->SetFVF(0x42);                                           // 0x40e604
    device->DrawPrimitive(D3DPT_TRIANGLELIST /*4*/, 0, 2);          // 0x40e621
    device->SetRenderState(D3DRS_ALPHABLENDENABLE /*27*/, 0);       // 0x40e63c
    if (r->d3dInitialized != 0)   // wrapper+120164 (0x1D564)        // 0x40e644
        return static_cast<std::uintptr_t>(
            device->SetRenderState(D3DRS_STENCILENABLE /*52*/, 1));
    return reinterpret_cast<std::uintptr_t>(r);
}

// ===========================================================================
// VA 0x00441070 - JumpNextKeyframe  (original: sub_441070, thiscall)
// ===========================================================================
// Timeline "next registration" jump (button 0x214 -> 0x48BC87).  Scans the
// keyframe trees for the smallest frame > app+2432 (kDword980, current
// frame) and, when found (< 0xFFFFFFFA), stores it, mirrors it into the
// frame edit (dialog item 417) and runs the frame-apply chain
// sub_432FA0 + PostViewRefresh (0x40D130).
//
// Branch on app+760 (kByteOptflag0):
//  true  - global tracks: camera tree @+884 (gate +656356, node 21 dwords),
//          light @+888 (gate +656357, 10 dwords), self-shadow @+892 (gate
//          +656356+2, 6 dwords), gravity/misc @+896 (gate +656359, 9
//          dwords), then 255 accessory slots (tree ptr @+900+4i, object
//          ptr @646512+4i, gate object byte +1196, node 15 dwords).
//          Tree nodes: [0]=frame, [1]/[2]=child indices; the walk follows
//          [2] while frame <= current.
//  false - selected model (slot = byte @+2320 into ptr array @+1920):
//          bone tree @model+9960 (gate byte +14588, node 7 dwords),
//          morph tracks @model+9956 (gate count byte +11692, 23-byte
//          records @*(model+9948)+42: [u16 node][?][flag], node 20 bytes),
//          morph/tree @model+9952 (count dword +11652, flag array
//          *(model+11668), node 60 bytes).
// ===========================================================================
void JumpNextKeyframe(MMDApp* app) {  // 0x441070
    auto& s = *app;
    const std::uint32_t cur = s.state.currentFrame;

    if (s.state.optflag[0] != 0) {        // 0x441089
        std::uint32_t best = 0xFFFFFFFAu;

        // camera tree (+884, node stride 21 dwords)
        if (s.GlobalTrackSelected(GlobalTimelineTrack::Camera) != 0) {
            std::uint32_t* n = reinterpret_cast<std::uint32_t*>(s.CameraKeys());
            std::uint32_t last = 0;
            if (n[0] <= cur) {                                      // 0x4410b9
                std::uint32_t idx = 0;
                for (;;) {                                          // 0x4410d4
                    const std::uint32_t next = n[idx + 2];
                    if (next == 0)
                        break;
                    last = next;
                    idx = 21 * next;
                    if (n[idx] > cur)
                        break;
                }
            }
            const std::uint32_t f = n[21 * last];                   // 0x4410d9
            if (f > cur && f < 0xFFFFFFFAu)                         // 0x4410e2
                best = f;
        }
        // light tree (+888, node stride 10 dwords)
        if (s.GlobalTrackSelected(GlobalTimelineTrack::Light) != 0) {
            std::uint32_t* n = reinterpret_cast<std::uint32_t*>(s.LightKeys());
            std::uint32_t last = 0;
            if (n[0] <= cur) {
                std::uint32_t idx = 0;
                for (;;) {                                          // 0x441121
                    const std::uint32_t next = n[idx + 2];
                    if (next == 0)
                        break;
                    last = next;
                    idx = 10 * next;
                    if (n[idx] > cur)
                        break;
                }
            }
            const std::uint32_t f = n[10 * last];                   // 0x441126
            if (f > cur && f < best)
                best = f;
        }
        // self-shadow tree (+892, node stride 6 dwords)
        if (s.GlobalTrackSelected(GlobalTimelineTrack::SelfShadow) != 0) {
            std::uint32_t* n = reinterpret_cast<std::uint32_t*>(s.ShadowKeys());
            std::uint32_t last = 0;
            if (n[0] <= cur) {
                std::uint32_t idx = 0;
                for (;;) {                                          // 0x44116e
                    const std::uint32_t next = n[idx + 2];
                    if (next == 0)
                        break;
                    last = next;
                    idx = 6 * next;
                    if (n[idx] > cur)
                        break;
                }
            }
            const std::uint32_t f = n[6 * last];                    // 0x441173
            if (f > cur && f < best)
                best = f;
        }
        // gravity/misc tree (+896, node stride 9 dwords)
        if (s.GlobalTrackSelected(GlobalTimelineTrack::Gravity) != 0) {
            std::uint32_t* n = reinterpret_cast<std::uint32_t*>(s.GravityKeys());
            std::uint32_t last = 0;
            if (n[0] <= cur) {
                std::uint32_t idx = 0;
                for (;;) {                                          // 0x4411b9
                    const std::uint32_t next = n[idx + 2];
                    if (next == 0)
                        break;
                    last = next;
                    idx = 9 * next;
                    if (n[idx] > cur)
                        break;
                }
            }
            const std::uint32_t f = n[9 * last];                    // 0x4411be
            if (f > cur && f < best)
                best = f;
        }
        // 255 accessory slots: tree ptr @app+900+4i, object @app+646512+4i
        for (int i = 0; i < 255; ++i) {                             // 0x4411d5
            std::uint32_t* n =
                reinterpret_cast<std::uint32_t*>(s.AccessoryKeys(i));
            unsigned char* acc =
                static_cast<unsigned char*>(s.ObjectSlot(i));
            if (acc != nullptr && acc[1196 /*0x4AC*/] != 0) {       // 0x4411ea
                std::uint32_t last = 0;
                if (n[0] <= cur) {                                  // 0x4411ff
                    const std::uint32_t* p = n;
                    for (;;) {                                      // 0x44121d
                        const std::uint32_t next = p[2];
                        if (next == 0)
                            break;
                        last = next;
                        p = n + 15 * next;
                        if (p[0] > cur)
                            break;
                    }
                }
                const std::uint32_t f = n[15 * last];               // 0x441226
                if (f > cur && f < best)                            // 0x44122f
                    best = f;
            }
        }

        if (best < 0xFFFFFFFAu) {                                   // 0x441244
            s.state.currentFrame = best;        // 0x44125a
            char buf[256];
            sprintf_s(buf, 0x100, "%d", best);                      // 0x441260
            SetWindowTextA(GetDlgItem(static_cast<HWND>(s.Hwnd()), panel::kCurrentFrameEdit),
                           buf);                                    // 0x441279/483
            RefreshAfterFrameApply(app);                            // 0x44148c
            PostViewRefresh(app);                                   // 0x441493
        }
        return;
    }

    // ---- model branch (byte @+760 == 0) --------------------------------
    unsigned char* model = s.SelectedModel();                       // 0x441285
    std::uint32_t best = 0xFFFFFFFAu;

    // Display/IK key track (the named gate keeps its x64 layout correct).
    if (mdl::Mdl(model)->displayKeyframesPresent != 0) {             // 0x44128c
        mdl::DisplayKey* n = mdl::DisplayKeys(model);
        std::uint32_t last = 0;
        if (n[0].frame <= cur) {
            std::uint32_t idx = 0;
            for (;;) {                                              // 0x4412e5
                const std::uint32_t next = n[last].next;
                if (next == 0)
                    break;
                last = next;
                idx = next;
                if (n[idx].frame > cur)
                    break;
            }
        }
        const std::uint32_t f = n[last].frame;
        if (f > cur && f < 0xFFFFFFFAu)
            best = f;
    }
    // morph tracks @model+9956 (gate count byte @model+11692; 23-byte
    // records @*(model+9948)+42 with [u16 node][?][flag], node 20 bytes)
    if (mikudancestudio::mdl::Mdl(model)->facialFrameCount != 0) {                                    // 0x441300
        int count = mikudancestudio::mdl::Mdl(model)->facialFrameCount;
        mdl::MorphKey* base = mdl::MorphKeys(model);
        const unsigned char* rec =
            *reinterpret_cast<unsigned char**>(model + 9948) + 42;
        do {                                                        // 0x441396
            if (rec[2] != 0) {                                      // 0x441333
                std::uint32_t node =
                    *reinterpret_cast<const std::uint16_t*>(rec);
                if (base[node].frame <= cur) {
                    for (;;) {                                      // 0x441374
                        const std::uint32_t next = base[node].next;
                        if (next == 0)
                            break;
                        node = next;
                        if (base[node].frame > cur)
                            break;
                    }
                }
                const std::uint32_t f = base[node].frame;
                if (f > cur && f < best)
                    best = f;
            }
            rec += 23;
        } while (--count != 0);
    }
    // key tree @model+9952 (count dword @model+11652, flag array
    // *(model+11668), node 60 bytes)
    {
        const int count =
            static_cast<int>(mikudancestudio::mdl::Mdl(model)->boneCount);   // 0x4413a0
        if (count > 0) {
            mdl::BoneKey* base = mdl::BoneKeys(model);
            const unsigned char* flags =
                mikudancestudio::mdl::Mdl(model)->boneSelection;
            for (int k = 0; k < count; ++k) {
                if (flags[k] != 0) {                                // 0x4413dc
                    std::uint32_t node = k;
                    if (base[node].frame <= cur) {
                        for (;;) {                                  // 0x441417
                            const std::uint32_t next = base[node].next;
                            if (next == 0)
                                break;
                            node = next;
                            if (base[node].frame > cur)
                                break;
                        }
                    }
                    const std::uint32_t f = base[node].frame;
                    if (f > cur && f < best)
                        best = f;
                }
            }
        }
    }

    if (best < 0xFFFFFFFAu) {                                       // 0x44144c
        s.state.currentFrame = best;            // 0x44145e
        char buf[256];
        sprintf_s(buf, 0x100, "%d", best);                          // 0x441464
        SetWindowTextA(GetDlgItem(static_cast<HWND>(s.Hwnd()), panel::kCurrentFrameEdit),
                       buf);                                        // 0x44147d/483
        RefreshAfterFrameApply(app);                                // 0x44148c
        PostViewRefresh(app);                                       // 0x441493
    }
}

// ===========================================================================
// VA 0x004414C0 - JumpPrevKeyframe  (original: sub_4414C0, thiscall)
// ===========================================================================
// Timeline "previous registration" jump (button 0x215 -> 0x48BC91) -
// mirror image of 0x441070: largest frame < current.  The tree walk runs
// while frame < current, and when the walked node's frame is >= current
// the [1] child is probed as the previous candidate.  The tail (store +
// item 417 echo + sub_432FA0 + PostViewRefresh) runs unconditionally.
// ===========================================================================
void JumpPrevKeyframe(MMDApp* app) {  // 0x4414C0
    auto& s = *app;
    const std::uint32_t cur = s.state.currentFrame;

    if (s.state.optflag[0] != 0) {        // 0x4414d9
        std::uint32_t best = 0;

        // camera tree (+884, node stride 21 dwords)
        if (s.GlobalTrackSelected(GlobalTimelineTrack::Camera) != 0) {
            std::uint32_t* n = reinterpret_cast<std::uint32_t*>(s.CameraKeys());
            std::uint32_t last = 0;
            if (n[0] < cur) {                                       // 0x441506
                std::uint32_t idx = 0;
                for (;;) {                                          // 0x441524
                    const std::uint32_t next = n[idx + 2];
                    if (next == 0)
                        break;
                    last = next;
                    idx = 21 * next;
                    if (n[idx] >= cur)
                        break;
                }
            }
            const std::uint32_t f = n[21 * last];                   // 0x441529
            if (f >= cur) {                                         // 0x441530
                const std::uint32_t f2 = n[21 * n[21 * last + 1]];  // 0x441544
                if (f2 < cur && f2 != 0)
                    best = f2;
            } else if (f != 0) {                                    // 0x441534
                best = f;
            }
        }
        // light tree (+888, node stride 10 dwords)
        if (s.GlobalTrackSelected(GlobalTimelineTrack::Light) != 0) {
            std::uint32_t* n = reinterpret_cast<std::uint32_t*>(s.LightKeys());
            std::uint32_t last = 0;
            if (n[0] < cur) {
                std::uint32_t idx = 0;
                for (;;) {                                          // 0x44158c
                    const std::uint32_t next = n[idx + 2];
                    if (next == 0)
                        break;
                    last = next;
                    idx = 10 * next;
                    if (n[idx] >= cur)
                        break;
                }
            }
            const std::uint32_t f = n[10 * last];                   // 0x441591
            if (f < cur) {                                          // 0x441598
                if (f > best)
                    best = f;
            } else {
                const std::uint32_t f2 = n[10 * n[10 * last + 1]];  // 0x4415a8
                if (f2 < cur && f2 > best)
                    best = f2;
            }
        }
        // self-shadow tree (+892, node stride 6 dwords)
        if (s.GlobalTrackSelected(GlobalTimelineTrack::SelfShadow) != 0) {
            std::uint32_t* n = reinterpret_cast<std::uint32_t*>(s.ShadowKeys());
            std::uint32_t last = 0;
            if (n[0] < cur) {
                std::uint32_t idx = 0;
                for (;;) {                                          // 0x4415f0
                    const std::uint32_t next = n[idx + 2];
                    if (next == 0)
                        break;
                    last = next;
                    idx = 6 * next;
                    if (n[idx] >= cur)
                        break;
                }
            }
            const std::uint32_t f = n[6 * last];                    // 0x4415f5
            if (f >= cur) {                                         // 0x4415fc
                const std::uint32_t f2 = n[6 * n[6 * last + 1]];    // 0x44160c
                if (f2 < cur && f2 > best)
                    best = f2;
            } else if (f > best) {                                  // 0x441600
                best = f;
            }
        }
        // gravity/misc tree (+896, node stride 9 dwords)
        if (s.GlobalTrackSelected(GlobalTimelineTrack::Gravity) != 0) {
            std::uint32_t* n = reinterpret_cast<std::uint32_t*>(s.GravityKeys());
            std::uint32_t last = 0;
            if (n[0] < cur) {
                std::uint32_t idx = 0;
                for (;;) {                                          // 0x441658
                    const std::uint32_t next = n[idx + 2];
                    if (next == 0)
                        break;
                    last = next;
                    idx = 9 * next;
                    if (n[idx] >= cur)
                        break;
                }
            }
            const std::uint32_t f = n[9 * last];                    // 0x44165d
            if (f >= cur) {                                         // 0x441664
                const std::uint32_t f2 = n[9 * n[9 * last + 1]];    // 0x441674
                if (f2 < cur && f2 > best)
                    best = f2;
            } else if (f > best) {                                  // 0x441668
                best = f;
            }
        }
        // 255 accessory slots: tree ptr @app+900+4i, object @app+646512+4i
        for (int i = 0; i < 255; ++i) {                             // 0x441685
            std::uint32_t* n =
                reinterpret_cast<std::uint32_t*>(s.AccessoryKeys(i));
            unsigned char* acc =
                static_cast<unsigned char*>(s.ObjectSlot(i));
            if (acc == nullptr || acc[1196 /*0x4AC*/] == 0)         // 0x4416a0
                continue;
            std::uint32_t last = 0;
            if (n[0] < cur) {                                       // 0x4416c3
                const std::uint32_t* p = n;
                for (;;) {                                          // 0x4416e1
                    const std::uint32_t next = p[2];
                    if (next == 0)
                        break;
                    last = next;
                    p = n + 15 * next;
                    if (p[0] >= cur)
                        break;
                }
            }
            const std::uint32_t f = n[15 * last];                   // 0x4416ee
            if (f >= cur) {                                         // 0x4416f3
                const std::uint32_t f2 = n[15 * n[15 * last + 1]];  // 0x441721
                if (f2 < cur && f2 > best)
                    best = f2;
            } else if (f > best) {                                  // 0x4416f7
                best = f;
            }
        }

        s.state.currentFrame = best;            // 0x441753
        char buf[256];
        sprintf_s(buf, 0x100, "%d", best);                          // 0x441759
        SetWindowTextA(GetDlgItem(static_cast<HWND>(s.Hwnd()), panel::kCurrentFrameEdit),
                       buf);                                        // 0x441772/a9e
        RefreshAfterFrameApply(app);                                // 0x441aa7
        PostViewRefresh(app);                                       // 0x441ab3
        return;
    }

    // ---- model branch (byte @+760 == 0) --------------------------------
    unsigned char* model = s.SelectedModel();                       // 0x44177e
    std::uint32_t best = 0;                                         // 0x441787

    // Display/IK key track.
    if (mdl::Mdl(model)->displayKeyframesPresent != 0) {             // 0x441789
        mdl::DisplayKey* n = mdl::DisplayKeys(model);
        std::uint32_t last = 0;
        if (n[0].frame < cur) {
            std::uint32_t idx = 0;
            for (;;) {                                              // 0x4417e3
                const std::uint32_t next = n[last].next;
                if (next == 0)
                    break;
                last = next;
                idx = next;
                if (n[idx].frame >= cur)
                    break;
            }
        }
        const std::uint32_t f = n[last].frame;
        if (f >= cur) {                                             // 0x4417f7
            const std::uint32_t f2 =
                n[n[last].previous].frame;
            if (f2 < cur && f2 != 0)
                best = f2;
        } else if (f != 0) {                                        // 0x4417fb
            best = f;
        }
    }
    // morph tracks @model+9956 (gate count byte @model+11692)
    if (mikudancestudio::mdl::Mdl(model)->facialFrameCount != 0) {                                    // 0x441858
        int count = mikudancestudio::mdl::Mdl(model)->facialFrameCount;
        mdl::MorphKey* base = mdl::MorphKeys(model);
        const unsigned char* rec =
            *reinterpret_cast<unsigned char**>(model + 9948) + 42;
        do {                                                        // 0x441946
            if (rec[2] != 0) {                                      // 0x441890
                std::uint32_t node =
                    *reinterpret_cast<const std::uint16_t*>(rec);
                if (base[node].frame < cur) {
                    for (;;) {                                      // 0x4418d5
                        const std::uint32_t next = base[node].next;
                        if (next == 0)
                            break;
                        node = next;
                        if (base[node].frame >= cur)
                            break;
                    }
                }
                const std::uint32_t f = base[node].frame;
                if (f >= cur) {                                     // 0x4418e3
                    const std::uint32_t f2 =
                        base[base[node].previous].frame;
                    if (f2 < cur && f2 > best)
                        best = f2;
                } else if (f > best) {                              // 0x4418e9
                    best = f;
                }
            }
            rec += 23;
        } while (--count != 0);
    }
    // key tree @model+9952 (count dword @model+11652, node 60 bytes)
    {
        const int count =
            static_cast<int>(mikudancestudio::mdl::Mdl(model)->boneCount);   // 0x441954
        if (count > 0) {
            mdl::BoneKey* base = mdl::BoneKeys(model);
            const unsigned char* flags =
                mikudancestudio::mdl::Mdl(model)->boneSelection;
            for (int k = 0; k < count; ++k) {
                if (flags[k] != 0) {                                // 0x441994
                    std::uint32_t node = k;
                    if (base[node].frame < cur) {
                        for (;;) {                                  // 0x4419d7
                            const std::uint32_t next = base[node].next;
                            if (next == 0)
                                break;
                            node = next;
                            if (base[node].frame >= cur)
                                break;
                        }
                    }
                    const std::uint32_t f = base[node].frame;
                    if (f >= cur) {                                 // 0x4419e9
                        const std::uint32_t f2 =
                            base[base[node].previous].frame;
                        if (f2 < cur && f2 > best)
                            best = f2;
                    } else if (f > best) {                          // 0x4419ef
                        best = f;
                    }
                }
            }
        }
    }

    s.state.currentFrame = best;                // 0x441a79
    char buf[256];
    sprintf_s(buf, 0x100, "%d", best);                              // 0x441a7f
    SetWindowTextA(GetDlgItem(static_cast<HWND>(s.Hwnd()), panel::kCurrentFrameEdit),
                   buf);                                            // 0x441a98/a9e
    RefreshAfterFrameApply(app);                                    // 0x441aa7
    PostViewRefresh(app);                                           // 0x441ab3
}

}  // namespace mikudancestudio
