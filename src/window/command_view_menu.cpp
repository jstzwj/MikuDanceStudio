// ===========================================================================
// CommandDispatch family: View/Edit option-menu cases 251..302 (0x47E8A0)
// ===========================================================================
// Cases 251..302 (0xFB..0x12E) of the 368-case switch in CommandDispatch
// (0x47E8A0): display/physics option toggles (byte-flag flips), menu check
// sync (CheckMenuItem) and enable state (EnableMenuItem), refresh tails
// (0x42C810 / 0x40CAC0 / 0x414610 PanelPaint), modal editor dialogs
// (DialogBoxParamA / CreateDialogParamA), the bone/camera rotation dialogs
// (case 300) and the render-to-picture / save-enhanced-model / open-oni
// file dialogs (cases 276/263/296).  Case 278 reloads the toon texture set
// (0x424DC0, InitToonTextures).
//
// Calling convention of the original (sub_47E8A0): thiscall, this = app
// (ebp = ecx at 0x47E8F4); arg_0 = sending control HWND (only used by the
// default handler's notify==1 compare chain, which can never match a menu
// id); arg_4 = wParam (id = LOWORD).  All `hwnd` uses in this family read
// app+0xA06B8 (the main window), i.e. the `hwnd` parameter of the port.
//
// Case map (id -> behaviour -> original VA).  Index byte_48F650[id-0xC8]
// selects the jump-table entry jpt_47E903 (0x48F30C); targets below are the
// `jumptable 0047E903 case N` labels confirmed by IDA:
//
//   id  hex  VA            behaviour
//   --- ---  ------------  ---------------------------------------------------
//   251 0FB  0x0048A1CC    gate app+0x2F8: return if set; 0xBC=1; modal
//                          "frame control" dialog (0x294 EN / 0x27C JP,
//                          sub_44D3F0); return when it closes with 2,
//                          else default (no-op)
//   252 0FC  0x0048A219    same gate/flag; dialog 0x295 EN / 0x283 JP
//                          (sub_44D510); same close handling
//   253 0FD  0x00489FC8    if app+0xA0B44 (dialog) or app+0x2F8 set:
//                          default (no-op); else CreateDialogParamA
//                          (0x296 EN / 0x285 JP, sub_44C7F0), store
//                          app+0xA0B44, ShowWindow(5) + UpdateWindow
//   254 0FE  0x0048A26A    toggle physics-display byte app+0x9ED9A with
//                          CheckMenuItem(GetMenu(hwnd), 0xFE, 0/8) -
//                          same flag as menu 0xD8
//   255 0FF  0x0048A883    0x54=1, sub_439E40(app), dirty 0xA0B0D=1
//   256 100  0x0048A89B    0x5C=1, sub_43A650(app), dirty 0xA0B0D=1
//   257 101  0x0048A8B3    0x64=1, sub_43B720(app), dirty 0xA0B0D=1
//   258 102  0x0048A8CB    0x68=1, sub_43BB30(app), dirty 0xA0B0D=1
//   259 103  0x0048A8E3    if app+0xA0B50 or app+0x2F8 set: default no-op;
//                          else CreateDialogParamA (0x29A EN / 0x299 JP,
//                          sub_464BD0) stored at app+0xA0B50, show+update
//   260 104  0x0048A862    toggle English UI byte 0xA0B4C (setz), then
//                          LocalizeUI (0x441AD0) + sub_40B5A0 (0x40B5A0)
//   261 105  0x0048A958    gate app+0x2F8; 0xBC=1; modal "enhance model"
//                          dialog (0x2AA EN / 0x2A9 JP, sub_43C9A0);
//                          if closed with 2 return; MessageBoxA
//                          "Please preserve the edit result as a new model
//                          by 'save enhanced model'." (JP 0x52E724), caption
//                          "enhance model" (JP 0x52E764), MB_TOPMOST;
//                          0xA0B64=1
//   262 106  0x0048A9EC    if app+0xA0B74 or app+0x2F8 set: default no-op /
//                          return; 0x48=1, 0xBC=1; CreateDialogParamA
//                          (0x2AC EN / 0x2AB JP, sub_465020) at app+0xA0B74
//   263 107  0x0048AA6F    "save enhanced model": SetCurrentDirectoryW(exe),
//                          swprintf_s(kEmptyPathFormat), OPENFILENAMEW (lStruct
//                          0x4C, filter "Polygon Model files(*.pmd)" /
//                          "*.pmd" / "All Files(*.*)", nFilterIndex 1,
//                          Flags 6, defext "pmd", title "save enhanced
//                          model" / JP 0x52F650, initial dir = DirModel
//                          when menu 0x12D checked else "UserFile\Model");
//                          dir-copy gate like 0x12D -> ExtractDirFromPath
//                          (app+0x9F338) -> CopyDirPathW(DirModel);
//                          sub_41EC10(app, path) (save model file)
//   264 108  0x0048ABB0    0x40=1; toggle byte app+0xA0CC8 with
//                          CheckMenuItem 0x108
//   265 109  0x0048ACA7    app+0xA0CC4 = 1; check 0x109, uncheck
//                          0x10D/0x10E/0x110; ModelKinematicSync(0x4B2210)
//                          over the 100 model slots; 0x9EDB5=1
//   266 10A  0x0048AEE1    if app+0xA0CCC set: return; 0x4C=1, 0xBC=1;
//                          CreateDialogParamA (0x323 EN / 0x322 JP,
//                          sub_479E90) at app+0xA0CCC
//   267 10B  0x0048AF57    0x44=1; MessageBoxA "Bullet Physics Library
//                          Ver.2.75\n\nhttp://www.bulletphysics.com/",
//                          caption "about physical engine" (JP 0x52F5E0),
//                          MB_TOPMOST
//   268 10C  0x0048AECE    0x54=1, 0x9EDB5=1
//   269 10D  0x0048AC0E    app+0xA0CC4 = 2; check 0x10D, uncheck
//                          0x109/0x10E/0x110; kinematic sync loop;
//                          0x9EDB5=1
//   270 10E  0x0048AD38    app+0xA0CC4 = 3; check 0x10E, uncheck
//                          0x10D/0x109/0x110; sync loop; 0x9EDB5=1
//   271 10F  0x0048AE60    0x60=1; menu 0x10F (IK display) toggled from its
//                          GetMenuState bit 8; byte app+0xA066D mirrors
//   272 110  0x0048ADCC    app+0xA0CC4 = 0; check 0x110, uncheck
//                          0x10D/0x109/0x10E; sync loop (no 0x9EDB5)
//   273 111  0x0048DBFF    gate app+0x2F8; 0x48=1; per-bone selection:
//                          sel[i] = (frames[i*0x25C+0x1EC] != 0),
//                          curBone = i when visible; RebuildTimelineTrackPanel +
//                          InvalidateSelectionReadouts
//   274 112  0x0048DC96    gate app+0x2F8; 0x30=1; clear frame-selection
//                          flags of morph (+0x10/0x14), other (+0x14/0x1C)
//                          and bone (+0x38/0x3C) tables, then mark the
//                          bone frames of every visible bone and its
//                          parent chain (+0x38 when +0x39==0); PanelPaint
//   275 113  0x0048DE30    0x38=1, 0xBC=1; modal dialog 0x32D EN / 0x32C JP
//                          (sub_461CE0) - no close handling (always return)
//   276 114  0x0048AFA4    "render to picture file": SetCurrentDirectoryW,
//                          swprintf_s(kEmptyPathFormat), OPENFILENAMEW ("All
//                          picture format" / JP 0x52F218, defext "bmp",
//                          title "render to picture file" / JP 0x52F1D0,
//                          initial dir DirUser / "UserFile" on menu 0x12D);
//                          dir-copy gate, wcscpy_s(app+0x9F134, path);
//                          grow locale render size (0x1D4E4/0x1D4E8 ->
//                          0x1D4FC/0x1D500) + PostDeviceReset when render
//                          W/H grew; CreateWindowExA "RecWindow" class
//                          (title 0x52F1C0 "描画中(0 frame)", style
//                          0x80C80000, X = sidebar+0x32, Y = 0x64,
//                          AdjustWindowRect 0x80C00000) stored at
//                          app+0xA0D24; CreateWindow-failed MessageBox
//                          (EN/JP 0x52DFF8); scene vtable+0x94 call when
//                          locale+0x1D4F8 == 0; AviBgOverlayRefresh when (0x9EB84==3
//                          || 0x91C==1) && 0x9E400!=0; RefreshMainWindowViewport;
//                          InvalidateRect(&app+0xA0D40); 0xA03B7=1
//   277 115  0x0048B2F7    0x58=1; menu 0x115 (shadow) toggled from
//                          GetMenuState bit 8; scene vtable+0xE4(scene,
//                          0xA1, on/off)
//   278 116  0x0048B401    InitToonTextures (0x424DC0), sub_4076E0(locale
//                          0xA06C4) - toon texture reload
//   279 117  0x0048B393    0x60=1; menu 0x117 (self shadow) toggle;
//                          byte app+0xA0188 mirrors
//   280 118  0x0048B416    app+0xA0D38 ? SaveFlagSubsystem (0x461FA0)
//                          : InitFlagSubsystem (0x461E00)
//   281 119  0x0048B433    0x6C=1; menu 0x119 (transparent window) toggle;
//                          SetWindowPos(app+0xA0D38, HWND_TOPMOST /
//                          HWND_NOTOPMOST, 0,0,0,0, 0x43)
//   282 11A  0x0048B624    0x40=1; toggle byte app+0xA0194 with
//                          CheckMenuItem 0x11A
//   283 11B  0x0048B682    0x44=1; toggle byte app+0xA0195 with
//                          CheckMenuItem 0x11B
//   284 11C  0x0048B6E0    0x6C=1; toggle byte app+0xA0196 with
//                          CheckMenuItem 0x11C
//   285 11D  0x0048B73E    0x6C=1; toggle byte app+0xA0197 with
//                          CheckMenuItem 0x11D; sub048+0x44 -> ptr,
//                          ptr+0xD4 = 1 / -1
//   286 11E  0x0048B7B8    0x48=1, 0xBC=1; ChooseColorA (lStruct 0x24,
//                          rgbResult from app+0xA0198/9C/A0, Flags 3,
//                          cust colors app+0xA01A4); write R/G/B back,
//                          SetModelColor(model, r, g, b) over the 100 slots
//   287 11F  0x0048B8CF    toggle byte app+0xA01E4 with CheckMenuItem 0x11F
//   288 120  0x0048A08F    0xBC=1; modal dialog 0x325 EN / 0x324 JP
//                          (sub_4641F0); return when closed
//   289 121  0x0048A0DF    0x38=1, 0xBC=1; modal dialog 0x32B EN / 0x32A JP
//                          (sub_42E370); return when closed
//   290 122  0x0048B926    app+0xA0274=1; sub_4629D0 (0x4629D0),
//                          PostDeviceReset (0x440DB0)
//   291 123  0x0048B93E    0x5C=1; app+0xA03B8 ? DisableKinect (0x42A020)
//                          : OpenNiInit (0x429CB0) with 0
//   292 124  0x0048B964    0x38=1; auto-record toggle (byte app+0xA0D68):
//                          off->on: CheckMenuItem 0x124, SetTimer(hwnd,
//                          0x65, 0x5DC), 0x9EDB5=1, model+0x38FD =
//                          app+0xA03EA per slot; on->off: KillTimer(0x65),
//                          uncheck, 0xA0B0D=1, 0x9EDB5=1
//   293 125  0x0048BA1E    toggle byte app+0xA03DC with CheckMenuItem 0x125
//   294 126  0x0048BA75    0x74=1; toggle byte app+0xA03DD with
//                          CheckMenuItem 0x126
//   295 127  0x0048BAD3    0x40=1; toggle byte app+0xA03DE with
//                          CheckMenuItem 0x127
//   296 128  0x0048BB31    "open oni data": app+0x2F8 set -> MessageBox
//                          "Please select model!" (JP 0x52E2F4), caption
//                          "open oni data" / "oni"; else GetOpenFileNameW
//                          ("oni files(*.oni)", defext "oni", Flags
//                          0x1000, initial dir "UserFile", owner
//                          app+0xA0D38 ?: hwnd); DisableKinect when
//                          0xA03B8; WideToSjisPath (0x407910) then
//                          OpenNiInit (0x429CB0)
//   297 129  0x0048BC9B    0x40=1; toggle byte app+0xA03E9 with
//                          CheckMenuItem 0x129
//   298 12A  0x0048BD18    menu 0x12A (2D/3D render) toggle; locale
//                          byte +0x1D570 = on/off; InitRenderStates
//                          (0x406E90) on the locale subobject
//   299 12B  0x0048BDA1    menu 0x12B toggle; BM_SETCHECK(0xF1, on/off)
//                          into GetDlgItem(app+0xA0D38 ?: hwnd, 0x228);
//                          byte app+0xA4420 mirrors
//   300 12C  0x0048A2C1    "rotation" dialog: 0xBC=1.  app+0x2F8 set:
//                          camera path - posx/y/z + cam2/3/4 + camangle
//                          through degree/radian conversion (dbl_52B768
//                          pi, dbl_52B760 180.0, dbl_52E678 (float)pi),
//                          dialog 0x298 EN / 0x289 JP (sub_40FBC0),
//                          writeback, dirty, PostViewRefresh.  else bone
//                          path: cur bone frames pos/quat snapshot,
//                          D3DXMatrixRotationQuaternion + asin/atan2
//                          extraction (0x40A690/0x40A6B0) + quadrant
//                          fixes (+-pi on m01/m13 when fabs(cos(t3))
//                          < flt_52B740), clamp, dialog 0x297 EN /
//                          0x288 JP (sub_40F860), PushBoneEditUndo, pos/angle
//                          writeback, RotationZ*X*Y matrix, quaternion
//                          writeback, frameFlag, dirty, PostViewRefresh
//   301 12D  0x0048D700    menu 0x12D (enhanced mode / accessory column)
//                          toggle only
//   302 12E  0x0048E0A6    "reset rotation": 0x34=1.  app+0x2F8 set:
//                          cam2/3/4 = 0, dirty, PostViewRefresh.  else
//                          cur bone (2D90 != -1): PushBoneEditUndo, identity
//                          matrix, D3DXQuaternionRotationMatrix into the
//                          bone quat, frameFlag[cur]=1, dirty,
//                          PostViewRefresh (tail shared with case 300)
//
// Fidelity notes:
//   * All flags/menu calls follow the original byte-for-byte (CheckMenuItem
//     flags 0/8, SetWindowPos flags 0x43, BM_SETCHECK 0/1, MB_TOPMOST
//     0x40000).  The `def_47E903` default handler only acts when
//     HIWORD(wParam) == BN_CLICKED and the control equals
//     GetDlgItem(hwnd, 0x1B4) - a no-op for every menu id in this family,
//     so `break` == the original's `jmp def_47E903`.
//   * `jmp loc_48F2E0` (function epilogue) is `return`.
//   * Float math follows the original x87 shape (double intermediates,
//     float stores).  Constants are bit-exact: dbl_52B768 = 0x400921FB
//     00000000 (pi truncated to 0x921FB mantissa), dbl_52B760 = 180.0,
//     dbl_52E678 = 0x400921FC80000000 (= (double)(float)pi),
//     flt_52B740 = 0x358637BD, flt_52B73C = 0xC0490FD8 (-pi float),
//     flt_52B738 = 0x40490FD8 (+pi float).
//   * Model-order and gravity dialogs live in dedicated translation units;
//     this file opens those dialogs and keeps the remaining menu handlers.
//
// Reference: ../translated/MikuMikuDance/fcn_0047e8a0.cpp
//   (the translated file covers only cases 200..0xDC; this port follows the
//   IDA decompilation of MikuMikuDance.exe 0x47E8A0 directly)
// =========================================================================//
#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include "mikudancestudio/dialog_resources.h"
#include "mikudancestudio/menu_commands.h"
#include <Windows.h>
#include <commctrl.h>
#include <commdlg.h>

#include <cmath>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <cwchar>

#include "mikudancestudio/charset_conv.hpp"
#include "mikudancestudio/d3dx_dyn.hpp"
#include "mikudancestudio/accessory_layout.hpp"
#include "mikudancestudio/global_key_layout.hpp"
#include "mikudancestudio/mmd_app.hpp"
#include "mikudancestudio/ported_funcs.hpp"
#include "mikudancestudio/model.hpp"
#include "mikudancestudio/panel_controls.hpp"

#include "dialog_scaffold.hpp"
#include "enhanced_model_messages.hpp"

namespace mikudancestudio {

// ---- dialog helpers moved to dedicated TUs (original VAs kept) ----------
// physics-model editor (menu 262, dialog 0x2AC): physics_model_dialog.cpp
void AddRigidBody(HWND hDlg);                                         // VA 0x0045F480
LRESULT CALLBACK PhysicsEditSubclassProc(HWND, UINT, WPARAM, LPARAM); // VA 0x0041EC50
void InitPhysicsModelDialog(HWND hDlg);                               // VA 0x0045F670
void CollectBodyEdits(MMDApp* app);                                   // VA 0x0041FF30
void CollectJointEdits(MMDApp* app);                                  // VA 0x004204F0
void AddJoint(HWND hDlg);                                             // VA 0x0043CA50
void ApplyBodyRecordToEdits(HWND hDlg, int idx);                      // VA 0x0043CCD0
void ApplyJointRecordToEdits(HWND hDlg, int idx);                     // VA 0x004214A0
void FlipPhysicsDialogPage(HWND hDlg, int flag);                      // VA 0x00421C20
void UpdateShapeControls(HWND hDlg, int mode, int idx);               // VA 0x00421CE0
void CommitPhysicsEdits(HWND hDlg);                                   // VA 0x004220F0
void PickPivotBone(HWND hDlg);                                        // VA 0x00420DC0
INT_PTR CALLBACK PhysicsModelDlgProc(HWND, UINT, WPARAM, LPARAM);     // VA 0x00465020
// enhance-model IO (toon collect / save model): enhance_model_io.cpp
int CollectToonFileNames(HWND hDlg); // VA 0x0041EA20
// (SaveEnhancedModel / SetModelColor declared in ported_funcs.hpp; RefreshMenuLanguage below)
void RefreshMenuLanguage(MMDApp* app); // VA 0x0040B5A0
// misc dialog bodies: misc_dialogs.cpp
void ApplyCameraFrameScaleAdd(MMDApp* app, HWND hDlg); // VA 0x0043DAD0
void ApplyBoneFrameScaleAdd(MMDApp* app, HWND hDlg);   // VA 0x0043E000
void ApplyMorphScaleAdd(MMDApp* app, HWND hDlg);       // VA 0x0043E680
void ApplyPhysicsOnOff(int on);                        // VA 0x004403C0

// Model-frame seek helper shared by the numeric-input dialogs.
void SeekSelectedModelToCurrentFrame(MMDApp* app); // VA 0x004220C0 (model_frame_seek.cpp)

namespace {

// Bit-exact original constants (see fidelity notes in the header).
// C++17 has no std::bit_cast; MSVC's __builtin_bit_cast is accepted in
// constant expressions, so the static_asserts pin each literal below to
// the original .rdata bit pattern.
constexpr double kPiTruncatedDouble = static_cast<double>(3.141592f); // pi (truncated)
static_assert(__builtin_bit_cast(std::uint64_t, kPiTruncatedDouble) == 0x400921FB00000000ull,
              "dbl_52B768 bit-exact");
constexpr double kDegreesScale = 180.0; // 180.0
static_assert(__builtin_bit_cast(std::uint64_t, kDegreesScale) == 0x4066800000000000ull,
              "dbl_52B760 bit-exact");
// 3.141594886779785 is the shortest round-trip decimal for dbl_52E678.
constexpr double kPiWidenedFloat = 3.141594886779785; // (double)(float)pi
static_assert(__builtin_bit_cast(std::uint64_t, kPiWidenedFloat) == 0x400921FC80000000ull,
              "dbl_52E678 bit-exact");
constexpr float kAngleEpsilon = 1e-6f; // ~1e-6 threshold
static_assert(__builtin_bit_cast(std::uint32_t, kAngleEpsilon) == 0x358637BDu,
              "flt_52B740 bit-exact");
constexpr float kNegativePiFloat = -3.141592f; // -pi float
static_assert(__builtin_bit_cast(std::uint32_t, kNegativePiFloat) == 0xC0490FD8u,
              "flt_52B73C bit-exact");
constexpr float kPiFloat = 3.141592f; // +pi float
static_assert(__builtin_bit_cast(std::uint32_t, kPiFloat) == 0x40490FD8u, "flt_52B738 bit-exact");

// 0x529688: swprintf_s format L"\0\0%s%s" - the leading NULs make the
// original call (which passes NO varargs) write an empty string; the %s
// slots are never consumed.  Kept byte-identical (same idiom as
// command_panel_toggles.cpp case 472).

// Active model = slot array at app+0x780 indexed by byte app+0x910.
static unsigned char* ActiveModel(MMDApp* app) {
    return app->SelectedModel();
}

// 0x40A680 / 0x40A690 / 0x40A6B0 / 0x40A6D0 - the D3DX angle helpers of the
// bone-rotation dialog (case 300).  sub_40A6B0 dispatches to the CRT
// atan2 (cintrin table entry "atan2" at 0x544C40): arg0 = x, arg1 = y;
// every result is rounded to float before it is stored.
double AngleFabs(float x) {
    return static_cast<double>(std::fabs(x));
}
double AngleAsin(float x) {
    return static_cast<double>(std::asin(x));
}
double AngleCos(float x) {
    return static_cast<double>(std::cos(x));
}
double AngleAtan2(float y, float x) {
    return static_cast<double>(static_cast<float>(std::atan2(y, x)));
}

// Capture uses the renderer-owned surface when multisampling is unavailable.
void BindCaptureRenderTarget(MMDApp* app) {
    D3DRenderer* locale = app->Renderer();
    locale->device->SetRenderTarget(0, locale->captureSurface); // 0x1D534
}

void SetMultisampleAntialias(MMDApp* app, int on) {
    D3DRenderer* locale = app->Renderer();
    locale->device->SetRenderState(D3DRS_MULTISAMPLEANTIALIAS, static_cast<DWORD>(on));
}

// Shared tail of cases 300/302 (0x48A805): D3DXQuaternionRotationMatrix of
// the current bone's quaternion from `m`, frameFlag[curBone] = 1,
// dirty = 1, PostViewRefresh.
void BoneRotationWriteback(MMDApp* app, const d3dx::D3DXMATRIXF& m) {
    unsigned char* model = ActiveModel(app);
    const std::int32_t cur = mdl::Mdl(model)->selectedBone;
    mdl::BoneRecord* const bones = mdl::Bones(model);
    float quat[4];
    d3dx::Get().quatFromMatrix(quat, &m);
    std::memcpy(bones[cur].rotQuat, quat, sizeof(quat));
    mdl::Mdl(model)->bonePhysicsState[cur] = 1;
    app->SceneModified() = 1;
    PostViewRefresh(app);
}

// ---------------------------------------------------------------------------
// Dialog procedures of this family, ported from the MikuMikuDance.exe
// decompilation (each carries its original VA).  They reach the app state via
// the global g_Block and share the rotation temp floats app+0xA0B28..0xA0B40.
// ---------------------------------------------------------------------------

// ---- JP strings, byte-exact Shift-JIS as in the binary ------------------
// 0x52F5E0: "物理エンジンについて"
static const char kCaptionPhysEngineJp[] =
    "\x95\xA8\x97\x9D\x83\x47\x83\x93\x83\x57\x83\x93\x82\xC9\x82\xC2"
    "\x82\xA2\x82\xC4";
// 0x52DFF8: "描画ウィンドウ作成"
static const char kCaptionCreateWndJp[] =
    "\x98\x5E\x89\xE6\x83\x45\x83\x42\x83\x93\x83\x68\x83\x45\x8D\xEC"
    "\x90\xAC";
// 0x52F1C0: window title "描画中(0 frame)" (ANSI)
static const char kRecWndTitle[] = "\x98\x5E\x89\xE6\x92\x86(0 frame)";
// 0x52E2F4: "モデルを選択していないとoniデータは読めません"
static const char kMsgOpenOniJp[] =
    "\x83\x4A\x83\x81\x83\x89\x81\x45\x8F\xC6\x96\xBE\x83\x82\x81\x5B"
    "\x83\x68\x82\xC5\x82\xCD"
    "oni"
    "\x83\x66\x81\x5B\x83\x5E\x82\xCD"
    "\x93\xC7\x82\xDF\x82\xDC\x82\xB9\x82\xF1";

// 0x52F650: JP title "拡張モデル保存"
static const wchar_t kTitleSaveModelJp[] = L"\x62E1\x5F35\x30E2\x30C7\x30EB\x4FDD\x5B58";
// 0x52F218: JP picture filter label "書きの画像フォーマット" (label is
// followed by a double NUL in the binary - the pattern list is empty).
static const wchar_t kFilterPicJp[] =
    L"\x5168\x3066\x306E\x5BFE\x5FDC\x30D5\x30A9\x30FC\x30DE\x30C3\x30C8"
    L"\x00\x00"
    L"*.bmp;*.jpg;*.png;*.dds;*.dib;*.pfm;*.hdr\x00\x00"
    L"Bmp files(*.bmp)\x00\x00*.bmp\x00\x00";
// 0x52F1D0: JP title "画像ファイル出力"
static const wchar_t kTitleRenderJp[] = L"\x753B\x50CF\x30D5\x30A1\x30A4\x30EB\x51FA\x529B";
// 0x52DC84: JP title "ファイルを開く"
static const wchar_t kTitleOpenJp[] = L"\x30D5\x30A1\x30A4\x30EB\x3092\x958B\x304F";

// ---- dialog helpers retained in this translation unit ----------------------

// VA 0x0041E9C0 - enhance-model dialog init: mirrors the ten 100-byte SJIS
// toon texture file-name slots of the active model (+0x33D8) into edits 709..718.
void FillToonTextureEdits(MMDApp* app, HWND hDlg) { // VA 0x0041E9C0
    unsigned char* model = app->SelectedModel();
    for (int i = 0; i < 10; ++i)
        SendMessageA(GetDlgItem(hDlg, panel::kToon01Edit + i), EM_REPLACESEL, 0,
                     reinterpret_cast<LPARAM>(mdl::Mdl(model)->pmdToonFileNames[i]));
}

// ---- JP strings of the dialogs, byte-exact -------------------------------
// 0x52E628: "ON (X印)" (SJIS 0x88F3 = 印); 0x52E624: "OFF".
static const char kOnXMarkJp[] = "ON (X\x88\xF3)";
static const char kOff[] = "OFF";
// "fine shadow mode" notice (x64 0x7FF7CB54D6A0 / 0x7FF7CB54D6B0): JP
// caption = the Fshadow button's two-kanji label + "モード"; JP text =
// "この命令は画面右上の「<Fshadow label>」ボタンに取って変わりました"
// (SJIS, byte-exact).
static const char kCaptionFineShadowJp[] = "\x94\xFC\x89\x65\x83\x82\x81\x5B\x83\x68";
static const char kMsgFineShadowJp[] =
    "\x82\xB1\x82\xCC\x89\x42\x82\xB5\x83\x52\x83\x7D\x83\x93\x83\x68\x82\xCD"
    "\x89\xE6\x96\xCA\x89\x45\x8F\xE3\x82\xCC\x81\x75\x94\xFC\x81\x40\x89\x65"
    "\x81\x76\x83\x7B\x83\x5E\x83\x93\x82\xC9\x8E\xE6\x82\xC1\x82\xC4\x95\xCF"
    "\x82\xED\x82\xE8\x82\xDC\x82\xB5\x82\xBD";

// ===========================================================================
// 0x44D3F0 - case 251 bone-frame multiply dialog (template caption
// "multiply of bone position-angle" / ﾎﾞｰﾝﾌﾚｰﾑ位置角度補正; 0x294 EN /
// 0x27C JP).  WM_INITDIALOG: top-most the dialog when the accessory column
// exists (app+0xA0D38), prefill the 12 edit boxes 686..697 (even columns
// "1.0", odd columns "0.0" via EM_REPLACESEL), focus 686 and select all.
// WM_COMMAND: id 1 -> sub_43E000 apply + EndDialog(1); id 2 -> EndDialog(2).
// Reference: MikuMikuDance.exe sub_44D3F0.
// ===========================================================================
INT_PTR CALLBACK BoneFrameMultiplyDlgProc(HWND hDlg, UINT msg, WPARAM wParam,
                                          LPARAM lParam) { // VA 0x0044D3F0
    (void)lParam;
    if (msg == WM_INITDIALOG) {
        MMDApp* app = g_Block;
        MakeDialogTopmostIfRequested(app, hDlg);
        for (int i = 0; i < 12; i += 2) {
            PrefillEdit(hDlg, panel::kBoneMulPosXScaleEdit + i, "1.0");
        }
        for (int j = 0; j < 12; j += 2) {
            PrefillEdit(hDlg, panel::kBoneMulPosXOffsetEdit + j, "0.0");
        }
        SelectAllEdit(hDlg, panel::kBoneMulPosXScaleEdit);
        return 0;
    }
    if (msg == WM_COMMAND) {
        const WORD id = LOWORD(wParam);
        if (id == IDOK) {
            ApplyBoneFrameScaleAdd(g_Block, hDlg); // 0x43E000
            EndDialog(hDlg, IDOK);
            return 0;
        }
        if (id == IDCANCEL) {
            EndDialog(hDlg, IDCANCEL);
            return 0;
        }
    }
    return 0;
}

// ===========================================================================
// 0x44D510 - case 252 facial-multiply dialog (template caption "multiply of
// facial expression" / 表情大きさ補正; 0x295 EN / 0x283 JP).
// WM_INITDIALOG: same top-most gate, but only edits 686 ("1.0") and 687
// ("0.0") are prefilled; focus 686 + select all.
// WM_COMMAND: id 1 -> sub_43E680 apply + EndDialog(1); id 2 -> EndDialog(2).
// Reference: MikuMikuDance.exe sub_44D510.
// ===========================================================================
INT_PTR CALLBACK FacialMultiplyDlgProc(HWND hDlg, UINT msg, WPARAM wParam,
                                       LPARAM lParam) { // VA 0x0044D510
    (void)lParam;
    if (msg == WM_INITDIALOG) {
        MMDApp* app = g_Block;
        MakeDialogTopmostIfRequested(app, hDlg);
        PrefillEdit(hDlg, panel::kMorphMulScaleEdit, "1.0");
        PrefillEdit(hDlg, panel::kMorphMulOffsetEdit, "0.0");
        SelectAllEdit(hDlg, panel::kMorphMulScaleEdit);
        return 0;
    }
    if (msg == WM_COMMAND) {
        const WORD id = LOWORD(wParam);
        if (id == IDOK) {
            ApplyMorphScaleAdd(g_Block, hDlg); // 0x43E680
            EndDialog(hDlg, IDOK);
            return 0;
        }
        if (id == IDCANCEL) {
            EndDialog(hDlg, IDCANCEL);
            return 0;
        }
    }
    return 0;
}

// ===========================================================================
// 0x43C9A0 - case 261 "enhance model" dialog (template caption "toon
// texture" / トゥーンテクスチャ; 0x2AA EN / 0x2A9 JP).
// WM_INITDIALOG: top-most gate + sub_41E9C0 init.
// WM_COMMAND: id 1 -> EndDialog(1) when sub_41EA20 succeeds else EndDialog(2);
// id 2 -> EndDialog(2).
// Reference: MikuMikuDance.exe sub_43C9A0.
// ===========================================================================
INT_PTR CALLBACK EnhanceModelDlgProc(HWND hDlg, UINT msg, WPARAM wParam,
                                     LPARAM lParam) { // VA 0x0043C9A0
    (void)lParam;
    if (msg == WM_INITDIALOG) {
        MMDApp* app = g_Block;
        MakeDialogTopmostIfRequested(app, hDlg);
        FillToonTextureEdits(app, hDlg); // 0x41E9C0
        return 0;
    }
    if (msg != WM_COMMAND) {
        return 0;
    }
    const WORD id = LOWORD(wParam);
    if (id == IDOK) {
        if (CollectToonFileNames(hDlg) != 0) { // 0x41EA20
            EndDialog(hDlg, IDOK);
        } else {
            EndDialog(hDlg, IDCANCEL);
        }
        return 0;
    }
    if (id == IDCANCEL) {
        EndDialog(hDlg, IDCANCEL);
        return 0;
    }
    return 0;
}

// ===========================================================================
// 0x461CE0 - case 275 physics ON/OFF frame dialog (template caption "change
// physics ON/OFF frame" / 物理ON/OFFフレーム変換; 0x32D EN / 0x32C JP).
// WM_INITDIALOG: CB_RESETCONTENT(669), CB_ADDSTRING "ON (X mark)" / JP
// "ON (X印)" then "OFF", CB_SETCURSEL(0).  WM_COMMAND: id 1 -> sub_4403C0
// (CB_GETCURSEL != 0) then EndDialog(1); id 2 -> EndDialog(1) as well (the
// caller of case 275 never inspects the result).
// Reference: MikuMikuDance.exe sub_461CE0.
// ===========================================================================
INT_PTR CALLBACK PhysicsOnOffFrameDlgProc(HWND hDlg, UINT msg, WPARAM wParam,
                                          LPARAM lParam) { // VA 0x00461CE0
    (void)lParam;
    if (msg == WM_INITDIALOG) {
        SendMessageA(GetDlgItem(hDlg, panel::kBoneNameCombo), CB_RESETCONTENT, 0, 0);
        const char* first = g_Block->state.englishUI != 0 ? "ON (X mark)" : kOnXMarkJp;
        SendMessageA(GetDlgItem(hDlg, panel::kBoneNameCombo), CB_ADDSTRING, 0, (LPARAM)first);
        SendMessageA(GetDlgItem(hDlg, panel::kBoneNameCombo), CB_ADDSTRING, 0, (LPARAM)kOff);
        SendMessageA(GetDlgItem(hDlg, panel::kBoneNameCombo), CB_SETCURSEL, 0, 0);
        return 0;
    }
    if (msg == WM_COMMAND) {
        const WORD id = LOWORD(wParam);
        if (id == IDOK) {
            if (SendMessageA(GetDlgItem(hDlg, panel::kBoneNameCombo), CB_GETCURSEL, 0, 0) != 0) {
                ApplyPhysicsOnOff(1); // 0x4403C0
            } else {
                ApplyPhysicsOnOff(0); // 0x4403C0
            }
            EndDialog(hDlg, IDOK);
            return 0;
        }
        if (id == IDCANCEL) {
            EndDialog(hDlg, IDOK);
            return 0;
        }
    }
    return 0;
}

// ===========================================================================
// 0x40FBC0 - case 300 camera numeric-input dialog (template caption
// "numeric input(camera)" / 数値入力(カメラ); 0x298 EN / 0x289 JP).
// WM_INITDIALOG: top-most gate; the seven temp floats app+0xA0B28..0xA0B40
// (set by the case-300 camera path: posx/y/z, -cam2.., cam3.., cam4..,
// -camangle) are written as "%7.3f" into edits 637..642 + 644 via
// EM_REPLACESEL; focus 637 + select all.
// WM_COMMAND: id 1 -> read all seven edits back into the temps (atof),
// EndDialog(1); id 2 -> EndDialog(2).  The caller converts back to radians.
// Reference: MikuMikuDance.exe sub_40FBC0.
// ===========================================================================
INT_PTR CALLBACK CameraNumericInputDlgProc(HWND hDlg, UINT msg, WPARAM wParam,
                                           LPARAM lParam) { // VA 0x0040FBC0
    (void)lParam;
    MMDApp* app = g_Block;
    if (msg == WM_INITDIALOG) {
        MakeDialogTopmostIfRequested(app, hDlg);
        static const std::size_t kEditIds[7] = {637, 638, 639, 640, 641, 642, 644};
        char buf[20];
        for (int i = 0; i < 7; ++i) {
            sprintf_s(buf, 0x14, "%7.3f", static_cast<double>(app->RotationDialogTemp(i)));
            PrefillEdit(hDlg, static_cast<int>(kEditIds[i]), buf);
        }
        SelectAllEdit(hDlg, panel::kNumInputPosXEdit);
        return 0;
    }
    if (msg == WM_COMMAND) {
        const WORD id = LOWORD(wParam);
        if (id == IDOK) {
            static const std::size_t kEditIds[7] = {637, 638, 639, 640, 641, 642, 644};
            for (int i = 0; i < 7; ++i) {
                app->RotationDialogTemp(i) = ReadFloatFromEdit(hDlg, static_cast<int>(kEditIds[i]));
            }
            EndDialog(hDlg, IDOK);
        } else if (id == IDCANCEL) {
            EndDialog(hDlg, IDCANCEL);
        }
    }
    return 0;
}

// ===========================================================================
// 0x40F860 - case 300 bone numeric-input dialog (template caption "numeric
// input(bone)" / 数値入力(ボーン); 0x297 EN / 0x288 JP).
// WM_INITDIALOG: top-most gate; the six temp floats app+0xA0B28..0xA0B3C
// (pos x/y/z + the three euler angles in degrees, set by the case-300 bone
// path) are written as "%7.3f" into edits 637..642; focus 637 + select all.
// WM_COMMAND: id 1 -> read the six edits back (atof), EndDialog(1);
// id 2 -> EndDialog(2).  The caller converts back to radians.
// Reference: MikuMikuDance.exe sub_40F860.
// ===========================================================================
INT_PTR CALLBACK BoneNumericInputDlgProc(HWND hDlg, UINT msg, WPARAM wParam,
                                         LPARAM lParam) { // VA 0x0040F860
    (void)lParam;
    MMDApp* app = g_Block;
    if (msg == WM_INITDIALOG) {
        MakeDialogTopmostIfRequested(app, hDlg);
        static const std::size_t kEditIds[6] = {637, 638, 639, 640, 641, 642};
        char buf[20];
        for (int i = 0; i < 6; ++i) {
            sprintf_s(buf, 0x14, "%7.3f", static_cast<double>(app->RotationDialogTemp(i)));
            PrefillEdit(hDlg, static_cast<int>(kEditIds[i]), buf);
        }
        SelectAllEdit(hDlg, panel::kNumInputPosXEdit);
        return 0;
    }
    if (msg == WM_COMMAND) {
        const WORD id = LOWORD(wParam);
        if (id == IDOK) {
            static const std::size_t kEditIds[6] = {637, 638, 639, 640, 641, 642};
            for (int i = 0; i < 6; ++i) {
                app->RotationDialogTemp(i) = ReadFloatFromEdit(hDlg, static_cast<int>(kEditIds[i]));
            }
            EndDialog(hDlg, IDOK);
        } else if (id == IDCANCEL) {
            EndDialog(hDlg, IDCANCEL);
        }
    }
    return 0;
}

} // namespace

// ===========================================================================
// "fine shadow mode" notice - the Ctrl+S branch of the 'S' letter hotkey
// (x64 main pump sub_7FF7CB4474F0 @ 0x7FF7CB44F625).
// ===========================================================================
// The branch is NOT a menu command case: it lives in the main pump's letter
// hotkey ladder (x86 0x4726B2, right after the 0x46FF02 key-poll call;
// ported in src/app/key_ladder.cpp, which calls this from its 'G' block).
// Original block:
//   gate: (GetFocus() == main window || separate-window byte 0x9FCDD) &&
//         letterKeyState[7] == 1 ('G' pressed this frame - the cell the
//         PollKey letter table assigns to 'g'/'G') &&
//         GetFocus() != the frame-number edit 0x22A
//   - Shift held (shiftModifierState == 3): toggle the byte at
//     x64 app+0xA1DC8 (shadow render-mode toggle; x86 twin VA unrecovered)
//   - Ctrl held (ctrlModifierState == 3, the "fine" modifier - NOT the
//     selection mode): THIS notice
//   - plain 'S': frame-number edit 0x22A read-back, seek and echo into
//     0x1A1 (same body as command case 553)
// EN: caption "fine shadow mode" (0x7FF7CB54D658), text "This command is
// changed for \"Fshadow\" button." (0x7FF7CB54D670); JP: byte_7FF7CB54D6A0 /
// byte_7FF7CB54D6B0 (constants above), gated by the English-UI byte.
// =========================================================================//
void FineShadowModeNotice(MMDApp* app) {
    const bool english = app->state.englishUI != 0;
    MessageBoxA(app->state.hwnd,
                english ? "This command is changed for \"Fshadow\" button." : kMsgFineShadowJp,
                english ? "fine shadow mode" : kCaptionFineShadowJp, 0);
}

void CmdViewMenu(MMDApp* app, HWND hwnd, std::uint16_t id, std::uint16_t notify) {
    (void)notify; // see header: the default handler's notify gate can never
                  // match any id of this family
    HINSTANCE hInst = static_cast<HINSTANCE>(app->HInstance());
    const bool english = app->state.englishUI != 0;

    switch (id) {
    // ------------------------------------------------------------------
    // 251 (0x0048A1CC): bone-frame multiply dialog (0x294 EN / 0x27C JP).
    // Gate: app+0x2F8 must be clear; closes with 2 -> return, else the
    // default handler (no-op for menu ids).
    // ------------------------------------------------------------------
    case MDS_CMD_MULTIPLY_BONE_FRAMES: {
        if (app->CameraMode() != 0) {
            return;
        }
        app->state.enterKeyState = 1;
        if (DialogBoxParamA(hInst,
                            MAKEINTRESOURCEA(english ? MDS_DLG_BONE_FRAME_MULTIPLY_EN
                                                     : MDS_DLG_BONE_FRAME_MULTIPLY_JP),
                            hwnd, BoneFrameMultiplyDlgProc, 0) == IDCANCEL) {
            return;
        }
        break;
    }

    // ------------------------------------------------------------------
    // 252 (0x0048A219): facial-multiply dialog 0x295 EN / 0x283 JP
    //                    (sub_44D510).
    // ------------------------------------------------------------------
    case MDS_CMD_MULTIPLY_MORPH_FRAMES: {
        if (app->CameraMode() != 0) {
            return;
        }
        app->state.enterKeyState = 1;
        if (DialogBoxParamA(
                hInst,
                MAKEINTRESOURCEA(english ? MDS_DLG_MORPH_MULTIPLY_EN : MDS_DLG_MORPH_MULTIPLY_JP),
                hwnd, FacialMultiplyDlgProc, 0) == IDCANCEL) {
            return;
        }
        break;
    }

    // ------------------------------------------------------------------
    // 253 (0x00489FC8): edge-thickness dialog (0x296 EN / 0x285 JP,
    // sub_44C7F0) as a modeless window stored at app+0xA0B44.
    // ------------------------------------------------------------------
    case MDS_CMD_EDGE_THICKNESS: {
        if (app->EdgeThicknessDialog() != nullptr) {
            break; // jnz def_47E903 (no-op)
        }
        if (app->CameraMode() != 0) {
            break;
        }
        HWND dlg = CreateDialogParamA(
            hInst,
            MAKEINTRESOURCEA(english ? MDS_DLG_EDGE_THICKNESS_EN : MDS_DLG_EDGE_THICKNESS_JP), hwnd,
            EdgeThicknessDlgProc, 0);
        app->EdgeThicknessDialog() = dlg;
        ShowWindow(dlg, SW_SHOW);
        UpdateWindow(dlg);
        break;
    }

    // ------------------------------------------------------------------
    // 254 (0x0048A26A): toggle physics-display flag app+0x9ED9A and menu
    // item 0xFE (same flag as menu 0xD8 / CmdTogglePhysicsDisplay).
    // ------------------------------------------------------------------
    case MDS_CMD_TOGGLE_GROUND_SHADOW_TRANSPARENCY: {
        if (app->state.projectedShadowBlendEnabled != 0) {
            app->state.projectedShadowBlendEnabled = 0;
            CheckMenuItem(GetMenu(hwnd), MDS_CMD_TOGGLE_GROUND_SHADOW_TRANSPARENCY, MF_UNCHECKED);
        } else {
            app->state.projectedShadowBlendEnabled = 1;
            CheckMenuItem(GetMenu(hwnd), MDS_CMD_TOGGLE_GROUND_SHADOW_TRANSPARENCY, MF_CHECKED);
        }
        break;
    }

    // ------------------------------------------------------------------
    // 255..258 (0x0048A883/0x48A89B/0x48A8B3/0x48A8CB): open the four
    // frame-edit dialogs: flag 0x54/0x5C/0x64/0x68 = 1, helper call,
    // dirty 0xA0B0D = 1.  Table-driven: the four bodies are identical
    // except for the flag index and the frame-line helper called.
    // ------------------------------------------------------------------
    case MDS_CMD_INSERT_BONE_CAMERA_FRAME_LINE:
    case MDS_CMD_DELETE_BONE_CAMERA_FRAME_LINE:
    case MDS_CMD_INSERT_MORPH_LIGHT_FRAME_LINE:
    case MDS_CMD_DELETE_MORPH_LIGHT_FRAME_LINE: {
        static const struct {
            int id;
            LetterKey letterKey;
            void (*frameLineOp)(MMDApp*);
        } kFrameEditDialogs[] = {
            {MDS_CMD_INSERT_BONE_CAMERA_FRAME_LINE, LetterKey::I,
             InsertBoneCameraFrameLine}, // 0x439E40
            {MDS_CMD_DELETE_BONE_CAMERA_FRAME_LINE, LetterKey::K,
             DeleteBoneCameraFrameLine}, // 0x43A650
            {MDS_CMD_INSERT_MORPH_LIGHT_FRAME_LINE, LetterKey::U,
             InsertFacialLightFrameLine}, // 0x43B720
            {MDS_CMD_DELETE_MORPH_LIGHT_FRAME_LINE, LetterKey::J,
             DeleteFacialLightFrameLine}, // 0x43BB30
        };
        for (const auto& entry : kFrameEditDialogs) {
            if (entry.id == id) {
                app->LetterKeyState(entry.letterKey) = 1;
                entry.frameLineOp(app);
                app->SceneModified() = 1;
                return;
            }
        }
        break;
    }

    // ------------------------------------------------------------------
    // 259 (0x0048A8E3): English-name dialog (0x29A EN / 0x299 JP,
    // sub_464BD0) at app+0xA0B50.
    // ------------------------------------------------------------------
    case MDS_CMD_EDIT_ENGLISH_NAMES: {
        if (app->EnglishNameDialog() != nullptr) {
            break;
        }
        if (app->CameraMode() != 0) {
            break;
        }
        HWND dlg = CreateDialogParamA(
            hInst, MAKEINTRESOURCEA(english ? MDS_DLG_ENGLISH_NAMES_EN : MDS_DLG_ENGLISH_NAMES_JP),
            hwnd, EnglishNameDlgProc, 0);
        app->EnglishNameDialog() = dlg;
        ShowWindow(dlg, SW_SHOW);
        UpdateWindow(dlg);
        return;
    }

    // ------------------------------------------------------------------
    // 260 (0x0048A862): toggle English UI (byte 0xA0B4C), then the
    // language sweep (0x441AD0 LocalizeUI) and UI refresh (0x40B5A0).
    // ------------------------------------------------------------------
    case MDS_CMD_TOGGLE_UI_LANGUAGE: {
        const std::uint8_t cur = app->state.englishUI;
        app->state.englishUI = (cur == 0) ? 1 : 0;
        LocalizeUI(app);          // 0x441AD0
        RefreshMenuLanguage(app); // 0x40B5A0
        return;
    }

    // ------------------------------------------------------------------
    // 261 (0x0048A958): enhance-model dialog (0x2AA EN / 0x2A9 JP,
    // sub_43C9A0); on close (result != 2) the save reminder box,
    // then 0xA0B64 = 1.
    // ------------------------------------------------------------------
    case MDS_CMD_TOON_TEXTURES: {
        if (app->CameraMode() != 0) {
            return;
        }
        app->state.enterKeyState = 1;
        const INT_PTR r = DialogBoxParamA(
            hInst, MAKEINTRESOURCEA(english ? MDS_DLG_TOON_TEXTURE_EN : MDS_DLG_TOON_TEXTURE_JP),
            hwnd, EnhanceModelDlgProc, 0);
        if (r == IDCANCEL) {
            return;
        }
        if (english) {
            MessageBoxA(hwnd,
                        "Please preserve the edit result as a new model by "
                        "'save enhanced model'.",
                        "enhance model", MB_TOPMOST);
        } else {
            MessageBoxA(hwnd, kMsgEnhanceModelJp, kCaptionEnhanceModelJp, MB_TOPMOST);
        }
        app->EnhancedModelDirty() = 1;
        return;
    }

    // ------------------------------------------------------------------
    // 262 (0x0048A9EC): frame-edit dialog (0x2AC EN / 0x2AB JP,
    // sub_465020) at app+0xA0B74.
    // ------------------------------------------------------------------
    case MDS_CMD_PHYSICS_MODEL: {
        if (app->FrameCopyOrPhysicsDialog() != nullptr) {
            break;
        }
        if (app->CameraMode() != 0) {
            return;
        }
        app->LetterKeyState(LetterKey::B) = 1;
        app->state.enterKeyState = 1;
        HWND dlg = CreateDialogParamA(
            hInst, MAKEINTRESOURCEA(english ? MDS_DLG_PHYSICS_MODEL_EN : MDS_DLG_PHYSICS_MODEL_JP),
            hwnd, PhysicsModelDlgProc, 0);
        app->FrameCopyOrPhysicsDialog() = dlg;
        ShowWindow(dlg, SW_SHOW);
        UpdateWindow(dlg);
        return;
    }

    // ------------------------------------------------------------------
    // 263 (0x0048AA6F): "save enhanced model" file dialog.  Filter
    // "Polygon Model files(*.pmd)\0*.pmd\0All Files(*.*)\0\0" (0x52F688),
    // title EN "save enhanced model" / JP 0x52F650, defext "pmd",
    // Flags 6, nFilterIndex 1; initial dir = DirModel when menu 0x12D is
    // checked else "UserFile\Model" (0x52DCBC); on success the menu gate
    // copies the directory into DirModel, then sub_41EC10 saves the file.
    // ------------------------------------------------------------------
    case MDS_CMD_SAVE_ENHANCED_MODEL: {
        app->LetterKeyState(LetterKey::P) = 1;
        app->state.enterKeyState = 1;
        SetCurrentDirectoryW(app->ExeDir());
        wchar_t fileBuf[0x100];
        fileBuf[0] = L'\0';
        OPENFILENAMEW ofn;
        std::memset(&ofn, 0, sizeof(ofn));
        ofn.lStructSize = sizeof(ofn);
        ofn.hwndOwner = hwnd;
        ofn.nFilterIndex = 1;
        ofn.lpstrFilter = L"Polygon Model files(*.pmd)\x00\x00*.pmd\x00\x00"
                          L"All Files(*.*)\x00\x00";
        ofn.lpstrFile = fileBuf;
        ofn.nMaxFile = 0x100;
        ofn.Flags = (OFN_OVERWRITEPROMPT | OFN_HIDEREADONLY);
        if ((GetMenuState(GetMenu(hwnd), MDS_CMD_TOGGLE_PREVIOUS_FOLDER, 0) & 8) != 0) {
            ofn.lpstrInitialDir = app->DirModel();
        } else {
            ofn.lpstrInitialDir = L"UserFile\\Model";
        }
        ofn.lpstrDefExt = L"pmd";
        wchar_t fileTitle[0x100];
        ofn.nMaxFileTitle = 0x100;
        ofn.lpstrFileTitle = fileTitle;
        ofn.lpstrTitle = english ? L"save enhanced model" : kTitleSaveModelJp;
        if (!GetSaveFileNameW(&ofn)) {
            return;
        }
        if ((GetMenuState(GetMenu(hwnd), MDS_CMD_TOGGLE_PREVIOUS_FOLDER, 0) & 8) != 0) {
            wchar_t* dir = ExtractDirFromPath(app->PathWorkspace().projectDirectory, fileBuf);
            CopyDirPathW(app->DirModel(), dir);
        }
        SaveEnhancedModel(app, fileBuf); // 0x41EC10
        return;
    }

    // ------------------------------------------------------------------
    // 264 (0x0048ABB0): toggle byte app+0xA0CC8 with CheckMenuItem 0x108.
    // ------------------------------------------------------------------
    case MDS_CMD_TOGGLE_PHYSICS_BODIES: {
        app->LetterKeyState(LetterKey::D) = 1;
        if (app->state.rigidBodyDisplayEnabled != 0) {
            app->state.rigidBodyDisplayEnabled = 0;
            CheckMenuItem(GetMenu(hwnd), MDS_CMD_TOGGLE_PHYSICS_BODIES, MF_UNCHECKED);
        } else {
            app->state.rigidBodyDisplayEnabled = 1;
            CheckMenuItem(GetMenu(hwnd), MDS_CMD_TOGGLE_PHYSICS_BODIES, MF_CHECKED);
        }
        return;
    }

    // ------------------------------------------------------------------
    // 265/269/270/272 (0x0048ACA7/0x48AC0E/0x48AD38/0x48ADCC): view-mode
    // radios - app+0xA0CC4 = 1/2/3/0, the matching menu item
    // (0x109/0x10D/0x10E/0x110) is checked, the other three unchecked,
    // then ModelKinematicSync (0x4B2210, thiscall(model, app+0xA0CC4))
    // over the 100 model slots.  Cases 265/269/270 set 0x9EDB5 = 1.
    // ------------------------------------------------------------------
    case MDS_CMD_PHYSICS_ALWAYS: {
        app->PlaybackPhysicsMode() = 1;
        CheckMenuItem(GetMenu(hwnd), MDS_CMD_PHYSICS_ON_OFF, MF_UNCHECKED);
        CheckMenuItem(GetMenu(hwnd), MDS_CMD_PHYSICS_ALWAYS, MF_CHECKED);
        CheckMenuItem(GetMenu(hwnd), MDS_CMD_PHYSICS_TRACE, MF_UNCHECKED);
        CheckMenuItem(GetMenu(hwnd), MDS_CMD_PHYSICS_DISABLED, MF_UNCHECKED);
        // x64 dispatcher sub_7FF7CB45F550 case 265 twin walks all 255 slots
        // (mov r13d, 0FFh ... dec r13 at 0x7FF7CB46F309..0x7FF7CB46F32D).
        for (int i = 0; i < kModelSlotCount; ++i) {
            unsigned char* model = app->ModelSlot(i);
            if (model != nullptr) {
                ModelKinematicSync(model); // orig: thiscall(model, 0xA0CC4)
            }
        }
        app->PhysicsResetPending() = 1;
        return;
    }

    case MDS_CMD_PHYSICS_ON_OFF: {
        app->PlaybackPhysicsMode() = 2;
        CheckMenuItem(GetMenu(hwnd), MDS_CMD_PHYSICS_ON_OFF, MF_CHECKED);
        CheckMenuItem(GetMenu(hwnd), MDS_CMD_PHYSICS_ALWAYS, MF_UNCHECKED);
        CheckMenuItem(GetMenu(hwnd), MDS_CMD_PHYSICS_TRACE, MF_UNCHECKED);
        CheckMenuItem(GetMenu(hwnd), MDS_CMD_PHYSICS_DISABLED, MF_UNCHECKED);
        for (int i = 0; i < kModelSlotCount; ++i) {
            unsigned char* model = app->ModelSlot(i);
            if (model != nullptr) {
                ModelKinematicSync(model);
            }
        }
        app->PhysicsResetPending() = 1;
        return;
    }

    case MDS_CMD_PHYSICS_TRACE: {
        app->PlaybackPhysicsMode() = 3;
        CheckMenuItem(GetMenu(hwnd), MDS_CMD_PHYSICS_ON_OFF, MF_UNCHECKED);
        CheckMenuItem(GetMenu(hwnd), MDS_CMD_PHYSICS_ALWAYS, MF_UNCHECKED);
        CheckMenuItem(GetMenu(hwnd), MDS_CMD_PHYSICS_TRACE, MF_CHECKED);
        CheckMenuItem(GetMenu(hwnd), MDS_CMD_PHYSICS_DISABLED, MF_UNCHECKED);
        for (int i = 0; i < kModelSlotCount; ++i) {
            unsigned char* model = app->ModelSlot(i);
            if (model != nullptr) {
                ModelKinematicSync(model);
            }
        }
        app->PhysicsResetPending() = 1;
        return;
    }

    case MDS_CMD_PHYSICS_DISABLED: {
        app->PlaybackPhysicsMode() = 0;
        CheckMenuItem(GetMenu(hwnd), MDS_CMD_PHYSICS_ON_OFF, MF_UNCHECKED);
        CheckMenuItem(GetMenu(hwnd), MDS_CMD_PHYSICS_ALWAYS, MF_UNCHECKED);
        CheckMenuItem(GetMenu(hwnd), MDS_CMD_PHYSICS_TRACE, MF_UNCHECKED);
        CheckMenuItem(GetMenu(hwnd), MDS_CMD_PHYSICS_DISABLED, MF_CHECKED);
        for (int i = 0; i < kModelSlotCount; ++i) {
            unsigned char* model = app->ModelSlot(i);
            if (model != nullptr) {
                ModelKinematicSync(model);
            }
        }
        return;
    }

    // ------------------------------------------------------------------
    // 266 (0x0048AEE1): gravity-setting dialog (0x323 EN / 0x322 JP,
    // sub_479E90) at app+0xA0CCC.
    // ------------------------------------------------------------------
    case MDS_CMD_GRAVITY_SETTINGS: {
        if (app->GravitySettingDialog() != 0) {
            return;
        }
        app->LetterKeyState(LetterKey::G) = 1;
        app->state.enterKeyState = 1;
        HWND dlg = CreateDialogParamA(
            hInst, MAKEINTRESOURCEA(english ? MDS_DLG_GRAVITY_EN : MDS_DLG_GRAVITY_JP), hwnd,
            GravitySettingDlgProc, 0);
        app->GravitySettingDialog() = dlg;
        ShowWindow(dlg, SW_SHOW);
        UpdateWindow(dlg);
        return;
    }

    // ------------------------------------------------------------------
    // 267 (0x0048AF57): physical-engine about box.
    // ------------------------------------------------------------------
    case MDS_CMD_ABOUT_PHYSICS: {
        app->LetterKeyState(LetterKey::A) = 1;
        if (english) {
            MessageBoxA(hwnd,
                        "Bullet Physics Library Ver.2.75\n\n"
                        "http://www.bulletphysics.com/",
                        "about physical engine", MB_TOPMOST);
        } else {
            MessageBoxA(hwnd,
                        "Bullet Physics Library Ver.2.75\n\n"
                        "http://www.bulletphysics.com/",
                        kCaptionPhysEngineJp, MB_TOPMOST);
        }
        return;
    }

    // ------------------------------------------------------------------
    // 268 (0x0048AECE): 0x54 = 1, undo-available byte 0x9EDB5 = 1.
    // ------------------------------------------------------------------
    case MDS_CMD_RESET_PHYSICS_BODIES:
        app->LetterKeyState(LetterKey::I) = 1;
        app->PhysicsResetPending() = 1;
        return;

    // ------------------------------------------------------------------
    // 271 (0x0048AE60): toggle menu 0x10F (IK display) from its own
    // GetMenuState bit 8; byte app+0xA066D mirrors the checked state.
    // ------------------------------------------------------------------
    case MDS_CMD_TOGGLE_PLAYBACK_PHYSICS_ON_OFF: {
        app->LetterKeyState(LetterKey::P) = 1;
        if ((GetMenuState(GetMenu(hwnd), MDS_CMD_TOGGLE_PLAYBACK_PHYSICS_ON_OFF, 0) & 8) == 0) {
            CheckMenuItem(GetMenu(hwnd), MDS_CMD_TOGGLE_PLAYBACK_PHYSICS_ON_OFF, MF_CHECKED);
            app->state.playbackAlwaysOnOffMode = 1;
        } else {
            CheckMenuItem(GetMenu(hwnd), MDS_CMD_TOGGLE_PLAYBACK_PHYSICS_ON_OFF, MF_UNCHECKED);
            app->state.playbackAlwaysOnOffMode = 0;
        }
        return;
    }

    // ------------------------------------------------------------------
    // 273 (0x0048DBFF): bone visibility -> selection: sel[i] = 1 and
    // curBone = i for every bone whose frame visibility byte +0x1EC is
    // set, then the language sweeps (0x42F1E0 / 0x40D070).
    // ------------------------------------------------------------------
    case MDS_CMD_SELECT_PHYSICS_BONES: {
        if (app->CameraMode() != 0) {
            return;
        }
        app->LetterKeyState(LetterKey::B) = 1;
        unsigned char* model = ActiveModel(app);
        const std::int32_t boneCount = static_cast<std::int32_t>(mdl::Mdl(model)->boneCount);
        if (boneCount > 0) {
            mdl::BoneRecord* const bones = mdl::Bones(model);
            unsigned char* sel = mdl::Mdl(model)->boneSelection;
            for (std::int32_t i = 0; i < boneCount; ++i) {
                if (bones[i].hasRigidBody != 0) {
                    sel[i] = 1;
                    mdl::Mdl(model)->selectedBone = i;
                } else {
                    sel[i] = 0;
                }
            }
        }
        RebuildTimelineTrackPanel(app);   // 0x42F1E0
        InvalidateSelectionReadouts(app); // 0x40D070
        return;
    }

    // ------------------------------------------------------------------
    // Select physics-enabled keys of bones that have rigid bodies.
    // The model helper owns the three key-pool sweeps and linked-key walk.
    // ------------------------------------------------------------------
    case MDS_CMD_SELECT_PHYSICS_ON_FRAMES: {
        if (app->CameraMode() != 0) {
            return;
        }
        app->LetterKeyState(LetterKey::X) = 1;
        mdl::SelectPhysicsOnBoneKeys(*mdl::Mdl(ActiveModel(app)));
        PanelPaint(app); // 0x414610
        return;
    }

    // ------------------------------------------------------------------
    // 275 (0x0048DE30): modal dialog 0x32D EN / 0x32C JP (sub_461CE0);
    // no close handling in the original (always returns).
    // ------------------------------------------------------------------
    case MDS_CMD_TOGGLE_FRAME_PHYSICS:
        app->LetterKeyState(LetterKey::C) = 1;
        app->state.enterKeyState = 1;
        DialogBoxParamA(hInst,
                        MAKEINTRESOURCEA(english ? MDS_DLG_PHYSICS_FRAME_CONVERSION_EN
                                                 : MDS_DLG_PHYSICS_FRAME_CONVERSION_JP),
                        hwnd, PhysicsOnOffFrameDlgProc, 0);
        return;

    // ------------------------------------------------------------------
    // 276 (0x0048AFA4): "render to picture file".  Save dialog with the
    // picture filter (EN 0x52F3F8 / JP 0x52F218, label followed by an
    // empty pattern list as in the binary), defext "bmp", title "render
    // to picture file" / JP 0x52F1D0, initial dir DirUser / "UserFile";
    // dir gate 0x12D like 263; the chosen path goes to app+0x9F134 and
    // the render target grows (locale screenWidth/screenHeight, 0x1D4E4/
    // 0x1D4E8 -> presentParameters.BackBufferWidth/Height, 0x1D4FC/
    // 0x1D500 + PostDeviceReset) when wider/taller; then the recording
    // window ("RecWindow" class, title 0x52F1C0) is created at
    // app+0xA0D24 and the refresh tail runs.
    // ------------------------------------------------------------------
    case MDS_CMD_RENDER_IMAGE: {
        app->LetterKeyState(LetterKey::B) = 1;
        app->state.enterKeyState = 1;
        SetCurrentDirectoryW(app->ExeDir());
        wchar_t fileBuf[0x100];
        fileBuf[0] = L'\0';
        OPENFILENAMEW ofn;
        std::memset(&ofn, 0, sizeof(ofn));
        ofn.lStructSize = sizeof(ofn);
        ofn.hwndOwner = hwnd;
        ofn.lpstrFilter = english ? L"All picture format\x00\x00"
                                    L"*.bmp;*.jpg;*.png;*.dds;*.dib;*.pfm;*.hdr\x00\x00"
                                    L"Bmp files(*.bmp)\x00\x00*.bmp\x00\x00"
                                  : kFilterPicJp;
        ofn.lpstrFile = fileBuf;
        ofn.nFilterIndex = 1;
        ofn.nMaxFile = 0x100;
        ofn.Flags = (OFN_OVERWRITEPROMPT | OFN_HIDEREADONLY);
        if ((GetMenuState(GetMenu(hwnd), MDS_CMD_TOGGLE_PREVIOUS_FOLDER, 0) & 8) != 0) {
            ofn.lpstrInitialDir = app->DirUser();
        } else {
            ofn.lpstrInitialDir = L"UserFile";
        }
        ofn.lpstrDefExt = L"bmp";
        wchar_t fileTitle[0x100];
        ofn.nMaxFileTitle = 0x100;
        ofn.lpstrFileTitle = fileTitle;
        ofn.lpstrTitle = english ? L"render to picture file" : kTitleRenderJp;
        if (!GetSaveFileNameW(&ofn)) {
            return;
        }
        if ((GetMenuState(GetMenu(hwnd), MDS_CMD_TOGGLE_PREVIOUS_FOLDER, 0) & 8) != 0) {
            wchar_t* dir = ExtractDirFromPath(app->PathWorkspace().projectDirectory, fileBuf);
            CopyDirPathW(app->DirUser(), dir);
        }
        wcscpy_s(app->CaptureSavePath(), 0x100, fileBuf);
        D3DRenderer* locale = app->Renderer();
        const std::int32_t oldW = locale->screenWidth;  // 0x1D4E4
        const std::int32_t oldH = locale->screenHeight; // 0x1D4E8
        const std::int32_t renderW = app->RenderWidth();
        const std::int32_t renderH = app->RenderHeight();
        if (renderW > oldW || renderH > oldH) {
            locale->presentParameters.BackBufferWidth = renderW;  // 0x1D4FC
            locale->presentParameters.BackBufferHeight = renderH; // 0x1D500
            locale->screenWidth = renderW;                        // 0x1D4E4
            locale->screenHeight = renderH;                       // 0x1D4E8
            PostDeviceReset(app);                                 // 0x440DB0
        }
        RECT rc;
        rc.left = 0;
        rc.top = 0;
        rc.right = renderW;
        rc.bottom = renderH;
        AdjustWindowRect(&rc, WS_POPUP | WS_CAPTION, FALSE);
        HWND recWnd = CreateWindowExA(0, "RecWindow", kRecWndTitle, 0x80C80000,
                                      app->SidebarWidth() + 0x32, 0x64, rc.right - rc.left,
                                      rc.bottom - rc.top, hwnd, nullptr, hInst, nullptr);
        app->RecordingWindow() = recWnd; // 0xA0D24
        if (recWnd == nullptr) {
            MessageBoxA(hwnd, "CreateWindow failed",
                        english ? "create main window" : kCaptionCreateWndJp, MB_TOPMOST);
            return;
        }
        ShowWindow(recWnd, SW_SHOW);
        UpdateWindow(recWnd);
        if (locale->multisampleAvailable == 0) { // 0x1D4F8
            BindCaptureRenderTarget(app);
        }
        const bool needRefresh = app->CaptureMode() == ScreenCaptureMode::BackgroundRefresh ||
                                 app->state.aviBackgroundEnabled == 1;
        if (needRefresh && app->state.aviStream != 0) {
            AviBgOverlayRefresh(app); // 0x4168D0
        }
        RefreshMainWindowViewport(app); // 0x42C810
        InvalidateRect(hwnd, &app->ViewportRect(), FALSE);
        app->state.timelineAdvanceRequested = 1;
        return;
    }

    // ------------------------------------------------------------------
    // 277 (0x0048B2F7): shadow toggle - menu 0x115 from GetMenuState bit
    // 8, SetRenderState(0xA1, on/off) (x64 sub_7FF7CB45F550 0x7FF7CB46FAB4:
    // vtable+0x1C8 = 槽 57; x86 +0xE4/4 = 槽 57).
    // ------------------------------------------------------------------
    case MDS_CMD_TOGGLE_ANTIALIASING: {
        app->LetterKeyState(LetterKey::H) = 1;
        if ((GetMenuState(GetMenu(hwnd), MDS_CMD_TOGGLE_ANTIALIASING, 0) & 8) == 0) {
            CheckMenuItem(GetMenu(hwnd), MDS_CMD_TOGGLE_ANTIALIASING, MF_CHECKED);
            SetMultisampleAntialias(app, 1);
        } else {
            CheckMenuItem(GetMenu(hwnd), MDS_CMD_TOGGLE_ANTIALIASING, MF_UNCHECKED);
            SetMultisampleAntialias(app, 0);
        }
        return;
    }

    // ------------------------------------------------------------------
    // 278 (0x0048B401): reload the toon texture set (0x424DC0
    // InitToonTextures), then the locale refresh sub_4076E0.
    // ------------------------------------------------------------------
    case MDS_CMD_RESTORE_TEXTURES:
        InitToonTextures(app);               // 0x424DC0
        ReloadTextureCache(app->Renderer()); // 0x4076E0
        return;

    // ------------------------------------------------------------------
    // 279 (0x0048B393): self-shadow toggle - menu 0x117, byte
    // app+0xA0188 mirrors.
    // ------------------------------------------------------------------
    case MDS_CMD_TOGGLE_SELF_SHADOW: {
        app->LetterKeyState(LetterKey::P) = 1;
        if ((GetMenuState(GetMenu(hwnd), MDS_CMD_TOGGLE_SELF_SHADOW, 0) & 8) == 0) {
            CheckMenuItem(GetMenu(hwnd), MDS_CMD_TOGGLE_SELF_SHADOW, MF_CHECKED);
            app->state.selfShadowEnabled = 1;
        } else {
            CheckMenuItem(GetMenu(hwnd), MDS_CMD_TOGGLE_SELF_SHADOW, MF_UNCHECKED);
            app->state.selfShadowEnabled = 0;
        }
        return;
    }

    // ------------------------------------------------------------------
    // 280 (0x0048B416): flag-subsystem save/init keyed on app+0xA0D38.
    // ------------------------------------------------------------------
    case MDS_CMD_TOGGLE_SEPARATE_WINDOW:
        if (app->state.floatingWindow != 0) {
            SaveFlagSubsystem(app); // 0x461FA0
        } else {
            InitFlagSubsystem(app); // 0x461E00
        }
        return;

    // ------------------------------------------------------------------
    // 281 (0x0048B433): transparent-window toggle - menu 0x119 plus
    // SetWindowPos of the accessory column (app+0xA0D38) to
    // HWND_TOPMOST / HWND_NOTOPMOST with flags 0x43.
    // ------------------------------------------------------------------
    case MDS_CMD_TOGGLE_ALWAYS_ON_TOP: {
        app->LetterKeyState(LetterKey::F) = 1;
        if ((GetMenuState(GetMenu(hwnd), MDS_CMD_TOGGLE_ALWAYS_ON_TOP, 0) & 8) == 0) {
            CheckMenuItem(GetMenu(hwnd), MDS_CMD_TOGGLE_ALWAYS_ON_TOP, MF_CHECKED);
            if (app->state.floatingWindow != 0) {
                SetWindowPos(app->state.floatingWindow, HWND_TOPMOST, 0, 0, 0, 0, 0x43);
            }
        } else {
            CheckMenuItem(GetMenu(hwnd), MDS_CMD_TOGGLE_ALWAYS_ON_TOP, MF_UNCHECKED);
            if (app->state.floatingWindow != 0) {
                SetWindowPos(app->state.floatingWindow, HWND_NOTOPMOST, 0, 0, 0, 0, 0x43);
            }
        }
        return;
    }

    // ------------------------------------------------------------------
    // 282..285 (0x0048B624/0x48B682/0x48B6E0/0x48B73E): option toggles -
    // bytes app+0xA0194..0xA0197 mirrored by menu items 0x11A..0x11D.
    // Case 285 additionally stores 1 / -1 into scene->groundBody -> +0xD4.
    // ------------------------------------------------------------------
    case MDS_CMD_TOGGLE_BLACK_BACKGROUND: {
        app->LetterKeyState(LetterKey::D) = 1;
        if (app->state.blackBackgroundEnabled != 0) {
            app->state.blackBackgroundEnabled = 0;
            CheckMenuItem(GetMenu(hwnd), MDS_CMD_TOGGLE_BLACK_BACKGROUND, MF_UNCHECKED);
        } else {
            app->state.blackBackgroundEnabled = 1;
            CheckMenuItem(GetMenu(hwnd), MDS_CMD_TOGGLE_BLACK_BACKGROUND, MF_CHECKED);
        }
        return;
    }

    case MDS_CMD_TOGGLE_MODEL_NON_DISPLAY: {
        app->LetterKeyState(LetterKey::A) = 1;
        if (app->state.modelNonDisplayMode != 0) {
            app->state.modelNonDisplayMode = 0;
            CheckMenuItem(GetMenu(hwnd), MDS_CMD_TOGGLE_MODEL_NON_DISPLAY, MF_UNCHECKED);
        } else {
            app->state.modelNonDisplayMode = 1;
            CheckMenuItem(GetMenu(hwnd), MDS_CMD_TOGGLE_MODEL_NON_DISPLAY, MF_CHECKED);
        }
        return;
    }

    case MDS_CMD_TOGGLE_WAVE_ON_FRAME: {
        app->LetterKeyState(LetterKey::F) = 1;
        if (app->state.wavPlaysOnFrameMove != 0) {
            app->state.wavPlaysOnFrameMove = 0;
            CheckMenuItem(GetMenu(hwnd), MDS_CMD_TOGGLE_WAVE_ON_FRAME, MF_UNCHECKED);
        } else {
            app->state.wavPlaysOnFrameMove = 1;
            CheckMenuItem(GetMenu(hwnd), MDS_CMD_TOGGLE_WAVE_ON_FRAME, MF_CHECKED);
        }
        return;
    }

    case MDS_CMD_TOGGLE_PHYSICS_FLOOR: {
        app->LetterKeyState(LetterKey::F) = 1;
        unsigned char* target =
            reinterpret_cast<unsigned char*>(app->Physics()->groundBody); // scene slot 0x44
        // x64 0x7FF7CB4700BC / 0x7FF7CB4700FB: mov dword ptr [rcx+0E0h], 1 /
        // r14d(-1) on the btRigidBody* at scene+0x88.  +0xE0 is the x64
        // twin of the x86 +0xD4 store (three pointer members earlier in
        // btCollisionObject widen by 4 each); the raw x86 literal would
        // land 12 bytes short in the x64 Bullet layout.
        if (app->state.floorVisible != 0) {
            app->state.floorVisible = 0;
            CheckMenuItem(GetMenu(hwnd), MDS_CMD_TOGGLE_PHYSICS_FLOOR, MF_UNCHECKED);
            *reinterpret_cast<std::int32_t*>(target + 0xE0) = -1;
        } else {
            app->state.floorVisible = 1;
            CheckMenuItem(GetMenu(hwnd), MDS_CMD_TOGGLE_PHYSICS_FLOOR, MF_CHECKED);
            *reinterpret_cast<std::int32_t*>(target + 0xE0) = 1;
        }
        return;
    }

    // ------------------------------------------------------------------
    // 286 (0x0048B7B8): background-color picker - ChooseColorA with the
    // current R/G/B from app+0xA0198/0xA019C/0xA01A0, custom colors at
    // app+0xA01A4, flags 3; the new color is written back and applied
    // via SetModelColor(model, r, g, b) over the 100 model slots.
    // ------------------------------------------------------------------
    case MDS_CMD_EDGE_COLOR: {
        app->LetterKeyState(LetterKey::B) = 1;
        app->state.enterKeyState = 1;
        CHOOSECOLORA cc;
        std::memset(&cc, 0, sizeof(cc));
        cc.lStructSize = sizeof(cc);
        cc.hwndOwner = hwnd;
        const std::uint8_t r = app->state.modelOutlineColorRed;
        const std::uint16_t g = app->state.modelOutlineColorGreen;
        const std::uint8_t b = app->state.modelOutlineColorBlue;
        cc.rgbResult = (static_cast<COLORREF>(b) << 16) | (static_cast<COLORREF>(g) << 8) | r;
        cc.lpCustColors = reinterpret_cast<COLORREF*>(app->state.customColorTable);
        cc.Flags = 3; // CC_RGBINIT | CC_FULLOPEN
        if (!ChooseColorA(&cc)) {
            return;
        }
        const std::uint8_t nr = cc.rgbResult & 0xFF;
        const std::uint8_t ng = (cc.rgbResult >> 8) & 0xFF;
        const std::uint8_t nb = (cc.rgbResult >> 16) & 0xFF;
        app->state.modelOutlineColorRed = nr;
        app->state.modelOutlineColorGreen = ng;
        app->state.modelOutlineColorBlue = nb;
        // x64 dispatcher twin (0x7FF7CB4701EF..0x7FF7CB470228) walks all
        // 255 slots before each SetModelColor (sub_7FF7CB4F2240) call.
        for (int i = 0; i < kModelSlotCount; ++i) {
            unsigned char* model = app->ModelSlot(i);
            if (model != nullptr) {
                SetModelColor(reinterpret_cast<MMDApp*>(model), nr, ng, nb);
            }
        }
        return;
    }

    // ------------------------------------------------------------------
    // 287 (0x0048B8CF): toggle byte app+0xA01E4 with CheckMenuItem 0x11F.
    // ------------------------------------------------------------------
    case MDS_CMD_TOGGLE_WIREFRAME: {
        if (app->WireframeRenderingEnabled() != 0) {
            CheckMenuItem(GetMenu(hwnd), MDS_CMD_TOGGLE_WIREFRAME, MF_UNCHECKED);
            app->WireframeRenderingEnabled() = 0;
        } else {
            CheckMenuItem(GetMenu(hwnd), MDS_CMD_TOGGLE_WIREFRAME, MF_CHECKED);
            app->WireframeRenderingEnabled() = 1;
        }
        return;
    }

    // ------------------------------------------------------------------
    // 288 (0x0048A08F): modal dialog 0x325 EN / 0x324 JP (sub_4641F0);
    // return when closed.
    // ------------------------------------------------------------------
    case MDS_CMD_MODEL_DRAW_ORDER:
        app->state.enterKeyState = 1;
        DialogBoxParamA(
            hInst,
            MAKEINTRESOURCEA(english ? MDS_DLG_MODEL_DRAW_ORDER_EN : MDS_DLG_MODEL_DRAW_ORDER_JP),
            hwnd, ModelDisplayOrderDlgProc, 0);
        return;

    // ------------------------------------------------------------------
    // 289 (0x0048A0DF): modal dialog 0x32B EN / 0x32A JP (sub_42E370);
    // return when closed.
    // ------------------------------------------------------------------
    case MDS_CMD_MODEL_CALCULATION_ORDER:
        app->LetterKeyState(LetterKey::C) = 1;
        app->state.enterKeyState = 1;
        DialogBoxParamA(hInst,
                        MAKEINTRESOURCEA(english ? MDS_DLG_MODEL_CALCULATE_ORDER_EN
                                                 : MDS_DLG_MODEL_CALCULATE_ORDER_JP),
                        hwnd, ModelCalculateOrderDlgProc, 0);
        return;

    // ------------------------------------------------------------------
    // 290 (0x0048B926): full scene reload - 0xA0274 = 1,
    // sub_4629D0 (0x4629D0), PostDeviceReset (0x440DB0).
    // ------------------------------------------------------------------
    case MDS_CMD_TOGGLE_FULL_SCREEN:
        app->FullscreenMode() = 1;
        ApplyFullscreenWindowState(app); // 0x4629D0
        PostDeviceReset(app);            // 0x440DB0
        return;

    // ------------------------------------------------------------------
    // 291 (0x0048B93E): open-ONI gate - app+0xA03B8 selects between the
    // plugin loader (0x42A020) and the OpenNi init (0x429CB0, arg 0).
    // ------------------------------------------------------------------
    case MDS_CMD_TOGGLE_KINECT:
        app->LetterKeyState(LetterKey::K) = 1;
        if (app->state.depthDeviceEnabled != 0) {
            DisableKinect(app); // 0x42A020
        } else {
            OpenNiInit(app, nullptr); // 0x429CB0
        }
        return;

    // ------------------------------------------------------------------
    // 292 (0x0048B964): auto-frame-record toggle (byte app+0xA0D68):
    // on - CheckMenuItem 0x124, SetTimer(hwnd, 0x65, 0x5DC, 0), per-slot
    // model+0x38FD = app+0xA03EA, 0x9EDB5 = 1; off - KillTimer(0x65),
    // uncheck, dirty + 0x9EDB5 = 1.
    // ------------------------------------------------------------------
    case MDS_CMD_TOGGLE_MOTION_CAPTURE: {
        app->LetterKeyState(LetterKey::C) = 1;
        if (app->state.autoRepeat == 0) {
            CheckMenuItem(GetMenu(hwnd), MDS_CMD_TOGGLE_MOTION_CAPTURE, MF_CHECKED);
            app->state.autoRepeat = 1;
            SetTimer(hwnd, 0x65, 0x5DC, nullptr);
            app->PhysicsResetPending() = 1;
            // x64 dispatcher twin (0x7FF7CB470326..0x7FF7CB47034F) walks all
            // 255 slots (movzx eax,[rbx+0A137Eh]; mov [rcx+3CA5h],al).
            for (int i = 0; i < kModelSlotCount; ++i) {
                unsigned char* model = app->ModelSlot(i);
                if (model != nullptr) {
                    mdl::Mdl(model)->openniVersion = app->state.openniVersion;
                }
            }
        } else {
            KillTimer(hwnd, 0x65);
            CheckMenuItem(GetMenu(hwnd), MDS_CMD_TOGGLE_MOTION_CAPTURE, MF_UNCHECKED);
            app->state.autoRepeat = 0;
            app->SceneModified() = 1;
            app->PhysicsResetPending() = 1;
        }
        return;
    }

    // ------------------------------------------------------------------
    // 293/294/295 (0x0048BA1E/0x48BA75/0x48BAD3): option toggles - bytes
    // app+0xA03DC/0xA03DD/0xA03DE mirrored by menu items 0x125..0x127.
    // ------------------------------------------------------------------
    case MDS_CMD_TOGGLE_CAPTURE_MIRROR: {
        if (app->state.kinectMirrorEnabled != 0) {
            CheckMenuItem(GetMenu(hwnd), MDS_CMD_TOGGLE_CAPTURE_MIRROR, MF_UNCHECKED);
            app->state.kinectMirrorEnabled = 0;
        } else {
            CheckMenuItem(GetMenu(hwnd), MDS_CMD_TOGGLE_CAPTURE_MIRROR, MF_CHECKED);
            app->state.kinectMirrorEnabled = 1;
        }
        return;
    }

    case MDS_CMD_TOGGLE_LOST_BONE_RESET: {
        app->LetterKeyState(LetterKey::L) = 1;
        if (app->state.kinectInitLostBone != 0) {
            CheckMenuItem(GetMenu(hwnd), MDS_CMD_TOGGLE_LOST_BONE_RESET, MF_UNCHECKED);
            app->state.kinectInitLostBone = 0;
        } else {
            CheckMenuItem(GetMenu(hwnd), MDS_CMD_TOGGLE_LOST_BONE_RESET, MF_CHECKED);
            app->state.kinectInitLostBone = 1;
        }
        return;
    }

    case MDS_CMD_TOGGLE_CAPTURE_SKELETON: {
        app->LetterKeyState(LetterKey::D) = 1;
        if (app->state.depthTextureCompositionEnabled != 0) {
            CheckMenuItem(GetMenu(hwnd), MDS_CMD_TOGGLE_CAPTURE_SKELETON, MF_UNCHECKED);
            app->state.depthTextureCompositionEnabled = 0;
        } else {
            CheckMenuItem(GetMenu(hwnd), MDS_CMD_TOGGLE_CAPTURE_SKELETON, MF_CHECKED);
            app->state.depthTextureCompositionEnabled = 1;
        }
        return;
    }

    // ------------------------------------------------------------------
    // 296 (0x0048BB31): "open oni data" - model-edit gate
    // (app+0x2F8): message box "Please select model!" / JP 0x52E2F4,
    // caption "open oni data" / "oni"; else the oni file dialog
    // (filter 0x52F168, defext "oni", Flags 0x1000, initial dir
    // "UserFile", owner app+0xA0D38 ?: hwnd), DisableKinect when
    // app+0xA03B8, wide->SJIS conversion (0x407910) and OpenNiInit
    // (0x429CB0).
    // ------------------------------------------------------------------
    case MDS_CMD_LOAD_ONI: {
        if (app->CameraMode() != 0) {
            if (english) {
                MessageBoxA(hwnd, "Please select model!", "open oni data", MB_TOPMOST);
            } else {
                MessageBoxA(hwnd, kMsgOpenOniJp, "oni", MB_TOPMOST);
            }
            return;
        }
        SetCurrentDirectoryW(app->ExeDir());
        wchar_t fileBuf[0x100];
        fileBuf[0] = L'\0';
        OPENFILENAMEW ofn;
        std::memset(&ofn, 0, sizeof(ofn));
        ofn.lStructSize = sizeof(ofn);
        ofn.hwndOwner = app->state.floatingWindow != 0 ? app->state.floatingWindow : hwnd;
        ofn.lpstrFilter = L"oni files(*.oni)\x00\x00*.oni\x00\x00All Files(*.*)\x00\x00";
        ofn.lpstrFile = fileBuf;
        ofn.nMaxFile = 0x100;
        ofn.Flags = OFN_FILEMUSTEXIST;
        ofn.lpstrInitialDir = L"UserFile";
        ofn.lpstrDefExt = L"oni";
        wchar_t fileTitle[0x100];
        ofn.nMaxFileTitle = 0x100;
        ofn.lpstrFileTitle = fileTitle;
        ofn.lpstrTitle = english ? L"open file data" : kTitleOpenJp;
        if (!GetOpenFileNameW(&ofn)) {
            return;
        }
        if (app->state.depthDeviceEnabled != 0) {
            DisableKinect(app); // 0x42A020
        }
        char sjisPath[0x100];
        WideToSjis(app->Renderer(), sjisPath, fileBuf, 0x100); // 0x407910
        OpenNiInit(app, sjisPath);                             // 0x429CB0
        return;
    }

    // ------------------------------------------------------------------
    // 297 (0x0048BC9B): toggle byte app+0xA03E9 with CheckMenuItem 0x129.
    // ------------------------------------------------------------------
    case MDS_CMD_TOGGLE_WAVE_DISABLED: {
        app->LetterKeyState(LetterKey::D) = 1;
        if (app->state.automaticFrameAdvanceEnabled != 0) {
            app->state.automaticFrameAdvanceEnabled = 0;
            CheckMenuItem(GetMenu(hwnd), MDS_CMD_TOGGLE_WAVE_DISABLED, MF_UNCHECKED);
        } else {
            app->state.automaticFrameAdvanceEnabled = 1;
            CheckMenuItem(GetMenu(hwnd), MDS_CMD_TOGGLE_WAVE_DISABLED, MF_CHECKED);
        }
        return;
    }

    // ------------------------------------------------------------------
    // 298 (0x0048BD18): render-mode toggle - menu 0x12A, locale byte
    // runtimeToggle (+0x1D570) mirrors, InitRenderStates (0x406E90) on the
    // locale subobject (the ported helper takes the app).
    // ------------------------------------------------------------------
    case MDS_CMD_TOGGLE_MIPMAP: {
        D3DRenderer* locale = app->Renderer();
        if ((GetMenuState(GetMenu(hwnd), MDS_CMD_TOGGLE_MIPMAP, 0) & 8) == 0) {
            CheckMenuItem(GetMenu(hwnd), MDS_CMD_TOGGLE_MIPMAP, MF_CHECKED);
            locale->runtimeToggle = 1; // 0x1D570
            InitRenderStates(app);     // 0x406E90 (orig: thiscall(locale))
        } else {
            CheckMenuItem(GetMenu(hwnd), MDS_CMD_TOGGLE_MIPMAP, MF_UNCHECKED);
            locale->runtimeToggle = 0; // 0x1D570
            InitRenderStates(app);
        }
        return;
    }

    // ------------------------------------------------------------------
    // 299 (0x0048BDA1): accessory-column toggle - menu 0x12B, checkbox
    // 0x228 of the accessory window (app+0xA0D38 ?: main) via
    // BM_SETCHECK, byte app+0xA4420 mirrors.
    // ------------------------------------------------------------------
    case MDS_CMD_TOGGLE_LOW_POWER: {
        HWND owner = app->state.floatingWindow != 0 ? app->state.floatingWindow : hwnd;
        if ((GetMenuState(GetMenu(hwnd), MDS_CMD_TOGGLE_LOW_POWER, 0) & 8) == 0) {
            CheckMenuItem(GetMenu(hwnd), MDS_CMD_TOGGLE_LOW_POWER, MF_CHECKED);
            SendMessageA(GetDlgItem(owner, panel::kFrameVolumeCheckbox), BM_SETCHECK, 1, 0);
            app->FrameVolumeControlEnabled() = 1;
        } else {
            CheckMenuItem(GetMenu(hwnd), MDS_CMD_TOGGLE_LOW_POWER, MF_UNCHECKED);
            SendMessageA(GetDlgItem(owner, panel::kFrameVolumeCheckbox), BM_SETCHECK, 0, 0);
            app->FrameVolumeControlEnabled() = 0;
        }
        return;
    }

    // ------------------------------------------------------------------
    // 300 (0x0048A2C1): rotation dialog.  Camera path when app+0x2F8 is
    // set (posx/y/z + cam2/3/4 + camangle through the pi/180 conversion,
    // dialog 0x298 EN / 0x289 JP, writeback, dirty, PostViewRefresh);
    // bone path otherwise (quaternion extraction via D3DXMatrixRotation-
    // Quaternion + asin/atan2 (0x40A690/0x40A6B0), quadrant fixes,
    // clamp, dialog 0x297 EN / 0x288 JP, PushBoneEditUndo, pos/angle writeback,
    // RotationZ*X*Y matrix, quaternion writeback, frameFlag, dirty,
    // PostViewRefresh).  The 16-float temp block app+0xA0B28..0xA0B40 is
    // shared with the dialog procs in the original.
    // ------------------------------------------------------------------
    case MDS_CMD_NUMERIC_INPUT: {
        const double kD1 = kPiTruncatedDouble; // pi (truncated)
        const double kD2 = kDegreesScale;      // 180.0
        const double kD3 = kPiWidenedFloat;    // (double)(float)pi
        const float kPi = kPiFloat;            // +pi float
        const float kNegPi = kNegativePiFloat;
        const float kThr = kAngleEpsilon;
        app->state.enterKeyState = 1;
        if (app->CameraMode() != 0) {
            // ---- camera path (0x48A2D9..0x48A407) -----------------------
            const float t0 = app->CameraPositionX();
            const float t1 = app->CameraPositionY();
            const float t2 = app->CameraPositionZ();
            const float t3 =
                static_cast<float>(-static_cast<double>(app->CameraPitch()) * kD2 / kD1);
            const float t4 = static_cast<float>(static_cast<double>(app->CameraYaw()) * kD2 / kD1);
            const float t5 = static_cast<float>(static_cast<double>(app->CameraRoll()) * kD2 / kD1);
            const float t6 = static_cast<float>(-static_cast<double>(app->CameraDistance()));
            if (DialogBoxParamA(hInst,
                                MAKEINTRESOURCEA(english ? MDS_DLG_CAMERA_NUMERIC_INPUT_EN
                                                         : MDS_DLG_CAMERA_NUMERIC_INPUT_JP),
                                hwnd, CameraNumericInputDlgProc, 0) == IDCANCEL) {
                return;
            }
            app->CameraPositionX() = t0;
            app->CameraPositionY() = t1;
            app->CameraPositionZ() = t2;
            app->CameraPitch() = static_cast<float>(-static_cast<double>(t3) * kD3 / kD2);
            app->CameraYaw() = static_cast<float>(static_cast<double>(t4) * kD3 / kD2);
            app->CameraRoll() = static_cast<float>(kD2 / (kD3 * static_cast<double>(t5)));
            app->CameraDistance() = static_cast<float>(-static_cast<double>(t6));
            app->SceneModified() = 1;
            PostViewRefresh(app); // 0x40D130
            return;
        }
        // ---- bone path (0x48A40C..0x48A85D) -----------------------------
        unsigned char* model = ActiveModel(app);
        const std::int32_t curBone = mdl::Mdl(model)->selectedBone;
        if (curBone == -1) {
            return;
        }
        mikudancestudio::mdl::BoneRecord* bone = mdl::Bones(model) + curBone;
        const float t0 = bone->trans[0];
        const float t1 = bone->trans[1];
        const float t2 = bone->trans[2];
        // rotation matrix from the bone's stored quaternion (0x14C)
        d3dx::D3DXMATRIXF m{};
        d3dx::Get().matrixRotationQuaternion(&m, reinterpret_cast<const float*>(bone->rotQuat));
        // euler extraction: t5 = atan2(m10, m01), t3 = asin(-m20),
        // t4 = atan2(m21, m13) - helper results are float-rounded
        float t5 = static_cast<float>(AngleAtan2(m.m[1][0], m.m[0][1]));
        float t3 = static_cast<float>(AngleAsin(-m.m[2][0]));
        float t4 = static_cast<float>(AngleAtan2(m.m[2][1], m.m[1][3]));
        // quadrant fixes (0x48A531..0x48A5BB): when
        // fabs(cos(t3)) < flt_52B740, add -pi/+pi to t5 and t4 depending
        // on the sign of m01 / m13 (m13 == 0 -> +pi, as in the original's
        // 0.0-vs-m13 compare)
        const double r1 = AngleFabs(static_cast<float>(AngleCos(t3)));
        if (r1 < static_cast<double>(kThr)) {
            t5 = static_cast<float>(static_cast<double>(t5) + (!(m.m[0][1] > 0.0f) ? kNegPi : kPi));
            t4 = static_cast<float>(static_cast<double>(t4) + (!(m.m[1][3] > 0.0f) ? kNegPi : kPi));
        }
        // clamp to zero (0x48A5BF..0x48A632)
        if (static_cast<float>(AngleFabs(t3)) < kThr) {
            t3 = 0.0f;
        }
        if (static_cast<float>(AngleFabs(t4)) < kThr) {
            t4 = 0.0f;
        }
        if (static_cast<float>(AngleFabs(t5)) < kThr) {
            t5 = 0.0f;
        }
        // degrees for the dialog (0x48A634..0x48A677)
        t3 = static_cast<float>(static_cast<double>(t3) * kD2 / kD1);
        t4 = static_cast<float>(-static_cast<double>(t4) * kD2 / kD1);
        t5 = static_cast<float>(-kD1 * kD2 / static_cast<double>(t5));
        if (DialogBoxParamA(hInst,
                            MAKEINTRESOURCEA(english ? MDS_DLG_BONE_NUMERIC_INPUT_EN
                                                     : MDS_DLG_BONE_NUMERIC_INPUT_JP),
                            hwnd, BoneNumericInputDlgProc, 0) == IDCANCEL) {
            return;
        }
        PushBoneEditUndo(app); // 0x42D6E0 (bone-edit keyframe register)
        // write back position and angles
        model = ActiveModel(app);
        bone = mdl::Bones(model) + curBone;
        bone->trans[0] = t0;
        bone->trans[1] = t1;
        bone->trans[2] = t2;
        t3 = static_cast<float>(static_cast<double>(t3) * kD1 / kD2);
        t4 = static_cast<float>(-static_cast<double>(t4) * kD1 / kD2);
        t5 = static_cast<float>(-kD2 / (static_cast<double>(t5) * kD1));
        // combined rotation matrix: Rz(t5) * Rx(t3) * Ry(t4) (0x48A746..)
        d3dx::D3DXMATRIXF tmp{};
        d3dx::D3DXMATRIXF rot{};
        d3dx::Get().rotZ(&rot, t5);
        d3dx::Get().rotX(&tmp, t3);
        d3dx::Get().multiply(&rot, &rot, &tmp);
        d3dx::Get().rotY(&tmp, t4);
        d3dx::Get().multiply(&rot, &rot, &tmp);
        // shared tail (0x48A805): quaternion writeback + frameFlag +
        // dirty + PostViewRefresh
        BoneRotationWriteback(app, rot);
        return;
    }

    // ------------------------------------------------------------------
    // 301 (0x0048D700): menu 0x12D (enhanced-mode) toggle only.
    // ------------------------------------------------------------------
    case MDS_CMD_TOGGLE_PREVIOUS_FOLDER: {
        if ((GetMenuState(GetMenu(hwnd), MDS_CMD_TOGGLE_PREVIOUS_FOLDER, 0) & 8) == 0) {
            CheckMenuItem(GetMenu(hwnd), MDS_CMD_TOGGLE_PREVIOUS_FOLDER, MF_CHECKED);
        } else {
            CheckMenuItem(GetMenu(hwnd), MDS_CMD_TOGGLE_PREVIOUS_FOLDER, MF_UNCHECKED);
        }
        return;
    }

    // ------------------------------------------------------------------
    // 302 (0x0048E0A6): reset rotation.  app+0x2F8 set: cam2/3/4 = 0,
    // dirty, PostViewRefresh.  else: current bone (2D90 != -1),
    // PushBoneEditUndo, identity matrix, quaternion writeback via the shared
    // tail (0x48A805), frameFlag[cur] = 1, dirty, PostViewRefresh.
    // ------------------------------------------------------------------
    case MDS_CMD_RESET_BONE_CAMERA_ROTATION: {
        app->LetterKeyState(LetterKey::Z) = 1;
        if (app->CameraMode() != 0) {
            app->CameraPitch() = 0.0f;
            app->CameraYaw() = 0.0f;
            app->CameraRoll() = 0.0f;
            app->SceneModified() = 1;
            PostViewRefresh(app);
            return;
        }
        unsigned char* model = ActiveModel(app);
        if (mdl::Mdl(model)->selectedBone == -1) {
            return;
        }
        PushBoneEditUndo(app); // 0x42D6E0
        d3dx::D3DXMATRIXF ident{};
        for (int i = 0; i < 4; ++i) {
            ident.m[i][i] = 1.0f;
        }
        BoneRotationWriteback(app, ident);
        return;
    }

    default:
        // def_47E903 (0x482897): only reachable actions are gated on
        // notify == BN_CLICKED and the sending control being
        // GetDlgItem(hwnd, 0x1B4) - a no-op for every menu id 251..302.
        break;
    }
}

} // namespace mikudancestudio
