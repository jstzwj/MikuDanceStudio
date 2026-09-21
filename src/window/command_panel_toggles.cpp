// ===========================================================================
// CommandDispatch family: control notifications 450..499 (0x47E8A0)
// ===========================================================================
// Cases 450..499 (0x1C2..0x1F3) of the 368-case switch in CommandDispatch
// (0x47E8A0): editor-panel control notifications (buttons / combos / toggles
// of the accessory, bone-selection and physics panels), plus value echo and
// panel refresh chains.
//
// Case map (id -> behaviour -> original VA).  Index byte_48F650[id-0xC8]
// selects the jump-table entry jpt_47E903 (0x48F30C); targets below are the
// `jumptable 0047E903 case N` labels confirmed by IDA:
//
//   id  hex  VA            behaviour
//   --- ---  ------------  ---------------------------------------------------
//   450 1C2  def_47E903    default (no-op for this id - see default note)
//   451 1C3  0x00486431    camera reset: pos/rot floats 0, gate app+0xA0430
//                          (<0: posY 10, camangle -45; else 0), Refresh(-1),
//                          PostViewRefresh
//   452 1C4  0x0048681D    clear frame selection (4 arrays + 255 bone-frame
//                          tables), Refresh(-1), RegisterCameraState,
//                          PanelPaint, SelectionReeval
//   453 1C5  0x0047E99F    opt flag byte 0x2F9 = 0; PostModelReload2,
//                          HandleWindowSize, InvalidateRect
//   454 1C6  0x0047E9C8    opt flag byte 0x2F9 = 1; same refresh chain
//   455-466 1C7-1D2  def_47E903   default (no-op)
//   467 1D3  0x0048648F    accessory reset: pos (-0.5,-1,0.5), pose blob
//                          memset + kind 3, color 0.6, scene vtable 0xCC/0xD4,
//                          echo %3d / %+3.1f into 461-466, TBM_SETPOS
//                          455-460, Refresh(-2)
//   468 1D4  0x004868C2    clear frame selection (like 452), Refresh(-2),
//                          RegisterLightState, PanelPaint
//   469 1D5  0x0047E9F1    opt flag byte 0x2FA = 0; same refresh chain
//   470 1D6  0x0047EA06    opt flag byte 0x2FA = 1; same refresh chain
//   471 1D7  def_47E903    default (no-op)
//   472 1D8  0x00486A59    load accessory (OPENFILENAMEW, EN/JP filter,
//                          menu-0x12D gate for initial dir, LoadAccessoryFile)
//   473 1D9  0x00486BA7    delete accessory: CB_GETCURSEL(0x1D7) lookup in
//                          slot table 0x9DD70, confirm MessageBox (EN/JP),
//                          free + rebuild slot (0x927C0 blob), index fixup,
//                          combo rebuild, EnableMenuItem 0xF9, RebuildCameraModePanel,
//                          Refresh(-1), PostLanguageSweep2
//   474,475 1DA,1DB  def_47E903   default (no-op)
//   476 1DC  0x00486F91    toggle accessory display byte 0x210,
//                          BM_SETCHECK(0x1DC), Refresh(byte 0x9E170)
//   477 1DD  0x0048A199    toggle accessory flag byte 0x49E (then default,
//                          which is a no-op for this id)
//   478-485 1DE-1E5  def_47E903   default (no-op)
//   486 1E6  0x00487011    toggle accessory flag byte 0x49C,
//                          BM_SETCHECK(0x1E6), Refresh(byte 0x9E170)
//   487 1E7  0x0048696B    per-accessory checkbox sync 0x1DE..0x1E4 via
//                          CommitEditControl, clear frame selection, Refresh(byte
//                          0x9E170), RegisterAccessoryKey, PanelPaint
//   488 1E8  0x0047EA2F    opt flag byte 0x2FB = 0; PostModelReload2,
//                          HandleWindowSize, InvalidateRect
//   489 1E9  0x0047EA58    opt flag byte 0x2FB = 1; same refresh chain
//   490 1EA  0x0047ECD6    physics radio 0x1EA: uncheck 0x1EB/0x1EC/0x1ED,
//                          app+0x914 = checked(0x1EA) ? 0 : 2
//   491 1EB  0x0047ED57    physics radio 0x1EB: uncheck 0x1EA/0x1EC/0x1ED,
//                          app+0x914 = checked(0x1EB) ? 1 : 2
//   492 1EC  0x0047EDDA    physics radio 0x1EC: uncheck 0x1EB/0x1EA/0x1ED,
//                          app+0x914 = checked(0x1EC) ? 4 : 2
//   493 1ED  0x0047EE5E    physics radio 0x1ED: uncheck 0x1EB/0x1EC/0x1EA,
//                          app+0x914 = checked(0x1ED) ? 3 : 2
//   494 1EE  0x0047FB24    select all bones (flags at model+0x26BC, byte
//                          +0x1E4 type / word +0x1F4 & 0x400 gate), check
//                          radio 0x1EA, re-dispatch WM_COMMAND(0x1EA),
//                          PostLanguageSweep + PostLanguageSweep2
//   495 1EF  0x0047F3FA    insert current-frame keyframes for all selected
//                          bones (bone-0 seed, D3DXMatrixRotationQuaternion
//                          bake for parent-less bones, identity for parented,
//                          change detection, frame-flag 0x2D98 marks,
//                          PostViewRefresh)
//   496 1F0  0x00481926    bone copy: count selected bones, enable 0x1F1/
//                          0x1F2, allocate 0x30-stride record array (app+0x350)
//                          with name/pos/quat snapshots
//   497 1F1  0x00481B4B    bone paste: clear selection, paste-counter 0x31B4
//                          wrap at 0x1E, undo records at model+0x26FC
//                          (0x24-stride), name-match paste loop, SetFocus,
//                          undo-flag 0x9EDB5 = 1
//   498 1F2  0x0048203D    bone paste (delete-tag variant, "ボーン削除"
//                          name filter); head ported, tail TODO(port)
//   499 1F3  def_47E903    default (no-op)
//
// Default handler def_47E903 (0x482897): only acts when HIWORD(notify)==1
// (BN_CLICKED) and the sending control equals GetDlgItem(hwnd, 0x1B4) (then
// ApplyModelComboSelection when app+0xA0B50 == 0); otherwise the control-HWND compare chain
// (0x1B4, 0x1BB, ...) is walked and always misses for the ids in this family
// (450/452/455-466/471/474/475/478-485/499 are all > 0x1B4/0x1BB), so the
// default is a no-op here.  `notify` (= HIWORD of the original wParam) is
// therefore unused by the family; the parameter is kept for the shared
// dispatch signature.
//
// Fidelity notes:
//   * All float math follows the original x87 shape (double intermediates,
//     float stores).  Constants: flt_52964C = -0.5f, flt_5295E8 = -1.0f,
//     flt_52960C = 0.5f, flt_52C9A4 = 0.6f, flt_52A1E4 = 10.0f,
//     flt_52A1E8 = -45.0f, dbl_52B9E8 = 256.0, dbl_52B8E0 = 100.0.
//   * App-state members are named state fields; the accessory / model
//     field offsets used by this family are declared below as
//     file-local constants.
//
// Reference: ../translated/MikuMikuDance/fcn_0047e8a0.cpp
//   (the translated file covers only cases 200..0xDC; this port follows the
//   IDA decompilation of MikuMikuDance.exe 0x47E8A0 directly)
// =========================================================================//
#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <Windows.h>
#include <commctrl.h>

#include <commdlg.h>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <new>

#include "mikudancestudio/d3dx_dyn.hpp"
#include "mikudancestudio/mmd_app.hpp"
#include "mikudancestudio/ported_funcs.hpp"
#include "mikudancestudio/model.hpp"
#include "mikudancestudio/panel_controls.hpp"

namespace mikudancestudio {

struct BoneCopyRecord {
    char name[20];
    float position[3];
    float rotation[4];
};
static_assert(sizeof(BoneCopyRecord) == 0x30,
              "bone copy record ABI");

// 32-bit multiply with the original's overflow idiom (mul/seto/neg/or):
// returns 0xFFFFFFFF when the product overflows (cases 496/497/498).
static std::uint32_t MulOrMax(std::uint32_t a, std::uint32_t b) {
    const std::uint64_t r = static_cast<std::uint64_t>(a) * b;
    return r > 0xFFFFFFFFull ? 0xFFFFFFFFu : static_cast<std::uint32_t>(r);
}

// Active model = slot array at this+0x780 indexed by byte this+0x910.
// (sub_47E8A0 pattern; the original re-derives it on every use.)
static unsigned char* ActiveModel(MMDApp* app) {
    return app->SelectedModel();
}

// The four frame-selection clear loops shared by cases 452/468/487
// (0x48681D / 0x4868C2 / 0x48696B): zero the selection byte at +0x48 of the
// 0x54-stride bone frames, +0x24 of the 0x28-stride morph frames, +0x14 of
// the 0x18-stride camera/light frames, +0x21 of the 0x24-stride frames and
// +0x18 of the 0x3C-stride frames of all 255 tables at app+0x384.
void ClearFrameSelection(MMDApp* app) {
    app->SceneModified() = 1;
    for (std::size_t i = 0; i < 10000; ++i) {
        app->CameraKeys()[i].selected = 0;
        app->LightKeys()[i].selected = 0;
        app->ShadowKeys()[i].selected = 0;
        app->GravityKeys()[i].selected = 0;
    }
    for (int k = 0; k < 0xFF; ++k) {
        unsigned char* tbl = reinterpret_cast<unsigned char*>(app->AccessoryKeys(k));
        if (tbl != nullptr) {
            for (std::size_t i = 0; i < 0x927C0u; i += 0x3C) {
                *(tbl + i + 0x18) = 0;
            }
        }
    }
}

// IDirect3DDevice9 slot 0xCC is SetLight(0, D3DLIGHT9*).
void ApplySceneLight(MMDApp* app) {
    app->Renderer()->device->SetLight(0, &app->SceneLight());
}

// IDirect3DDevice9 slot 0xD4 is LightEnable(0, TRUE).
void EnableSceneLight(MMDApp* app) {
    app->Renderer()->device->LightEnable(0, TRUE);
}

// Japanese strings, byte-exact Shift-JIS as in the binary.
// 0x530648: "アクセサリ：%sを削除します\nアクセサリのフレームデータも全て削除されます\n(この操作は元に戻す事はできません)\n\n削除してもよろしいですか？"
static const char kMsgDelAccessoryJp[] =
    "\x83\x41\x83\x4E\x83\x5A\x83\x54\x83\x8A\x81\x46"
    "%s"
    "\x82\xF0\x8D\xED\x8F\x9C\x82\xB5\x82\xDC\x82\xB7\n"
    "\x83\x41\x83\x4E\x83\x5A\x83\x54\x83\x8A\x82\xCC\x83\x74\x83\x8C"
    "\x81\x5B\x83\x80\x83\x66\x81\x5B\x83\x5E\x82\xE0\x91\x53\x82\xC4"
    "\x8D\xED\x8F\x9C\x82\xB3\x82\xEA\x82\xDC\x82\xB7\n"
    "\x28\x82\xB1\x82\xCC\x91\x80\x8D\xEC\x82\xCD\x8C\xB3\x82\xC9"
    "\x96\xDF\x82\xB7\x8E\x96\x82\xCD\x82\xC5\x82\xAB\x82\xDC\x82\xB9"
    "\x82\xF1\x29\n\n"
    "\x8D\xED\x8F\x9C\x82\xB5\x82\xC4\x82\xE0\x82\xE6\x82\xEB\x82\xB5"
    "\x82\xA2\x82\xC5\x82\xB7\x82\xA9\x81\x48";
// 0x530638: "アクセサリ削除"
static const char kCaptionDelAccessoryJp[] =
    "\x83\x41\x83\x4E\x83\x5A\x83\x54\x83\x8A\x8D\xED\x8F\x9C";

// 0x529688: swprintf_s format L"\0\0%s%s" - the leading NULs make the
// original call (which passes NO varargs) write an empty string; the %s
// slots are never consumed.  Kept byte-identical; the two dummy args below
// are never read (only silence the compiler's format-string warning).
static const wchar_t kEmptyPathFormat[] = L"\x0\x0%s%s";

// 0x52D948: JP open-filter for accessories (double-NUL terminated):
// "読込可能ファイル(*.x,*.vac)"
static const wchar_t kFilterAccJp[] =
    L"\x8AAD\x8FBC\x53EF\x80FD\x30D5\x30A1\x30A4\x30EB"
    L"(*.x,*.vac)\x00*.x;*.vac\x00x files(*.x)\x00*.x\x00"
    L"vac files(*.vac)\x00";
// 0x52DC84: JP dialog title "ファイルを開く"
static const wchar_t kTitleOpenJp[] = L"\x30D5\x30A1\x30A4\x30EB\x3092\x958B\x304F";

// strstr needles of case 498, matched against bone names:
//   kBoneNameNeedleRight = Shift-JIS 右 ("right"),
//   kBoneNameNeedleModelDelete = Shift-JIS モデル削除 ("model delete").
static const char kBoneNameNeedleRight[] = "\x89\x45";
static const char kBoneNameNeedleModelDelete[] =
    "\x83\x82\x83\x66\x83\x8B\x8D\xED\x8F\x9C";

// ---------------------------------------------------------------------------
// External targets ported in other translation units (declared here with
// their original VAs; not yet registered in ported_funcs.hpp).
// ---------------------------------------------------------------------------
void RefreshRequest(int area);                     // VA 0x00440AC0
void PanelPaint(MMDApp* app);                      // VA 0x00414610
void SelectionReeval(MMDApp* app);                 // VA 0x00430510 (stubs.cpp)
void CopyDirPathW(wchar_t* dest, const wchar_t* src); // VA 0x0042AE20 path copy

// ---------------------------------------------------------------------------
// Unported dependencies - kept as file-local external stubs with the call
// sites intact (stubs.cpp must not be touched).  TODO(port): replace with
// real bodies as the corresponding functions are ported.
// ---------------------------------------------------------------------------
void PushBoneEditUndo(MMDApp* app);                       // VA 0x0042D6E0
void RebuildCameraModePanel(MMDApp* app);                       // VA 0x0044D780
void SyncAccessoryEditPanel(MMDApp* app);                       // VA 0x004134E0
void RegisterAccessoryKey(MMDApp* app, int frame, int slot);   // VA 0x00413CB0
void RegisterCameraState(MMDApp* app, int frame);  // VA 0x00410560
void RegisterLightState(MMDApp* app, int frame);   // VA 0x00411630
void CommitEditControl(MMDApp* app, HWND hwnd);            // VA 0x00463640
void DeleteAccessory(mdl::AccessoryRecord* accessory, int flag);  // VA 0x0040A6F0
void IdentityCtor(void* obj);                         // VA 0x004C46F0 (ctor)
void* ConstructArrayElements(void* block, std::uint32_t elementSize,
                             std::uint32_t count,
                             void* ctor);  // VA 0x00401150

void CmdControl450(MMDApp* app, HWND hwnd, std::uint16_t id,
                   std::uint16_t notify) {
    (void)notify;  // see header: the default handler's notify gate can never
                   // match any id of this family
    switch (id) {
    // ------------------------------------------------------------------
    // 451 (0x00486431): camera reset - position/rotation floats cleared,
    // camera angle gated on app+0xA0430 (camera-cluster flag, default -1).
    // ------------------------------------------------------------------
    case 451: {
        app->CameraPositionX() = 0.0f;
        app->CameraPositionZ() = 0.0f;
        app->CameraPitch() = 0.0f;
        app->CameraYaw() = 0.0f;
        app->CameraRoll() = 0.0f;
        if (app->CameraParentModel() < 0) {
            app->CameraPositionY() = 10.0f;
            app->CameraDistance() = -45.0f;
        } else {
            app->CameraPositionY() = 0.0f;
            app->CameraDistance() = 0.0f;
        }
        RefreshRequest(-1);
        PostViewRefresh(app);
        break;
    }

    // ------------------------------------------------------------------
    // 453 / 454 (0x0047E99F / 0x0047E9C8): UI option flag byte 0x2F9 =
    // 0 / 1, then PostModelReload2 + HandleWindowSize + InvalidateRect.
    // 469 / 470 (0x0047E9F1 / 0x0047EA06): flag byte 0x2FA, same refresh
    // chain.  488 / 489 (0x0047EA2F / 0x0047EA58): flag byte 0x2FB, same
    // refresh chain.  Table-driven: the six bodies are identical except
    // for the flag index and the value stored.
    // ------------------------------------------------------------------
    case 453:
    case 454:
    case 469:
    case 470:
    case 488:
    case 489: {
        static const struct {
            int id;
            int flagIdx;
            unsigned char value;  // optflag is unsigned char[7]
        } kOptFlagRadios[] = {
            {453, 1, 0}, {454, 1, 1},  // 0x2F9
            {469, 2, 0}, {470, 2, 1},  // 0x2FA
            {488, 3, 0}, {489, 3, 1},  // 0x2FB
        };
        for (const auto& radio : kOptFlagRadios) {
            if (radio.id == id) {
                app->state.optflag[radio.flagIdx] = radio.value;
                break;
            }
        }
        PostModelReload2(app);
        HandleWindowSize(app);
        InvalidateRect(hwnd, nullptr, FALSE);
        break;
    }

    // ------------------------------------------------------------------
    // 452 (0x0048681D): clear frame selection; RefreshRequest(-1),
    // RegisterCameraState(app, app+0x980), PanelPaint, SelectionReeval.
    // ------------------------------------------------------------------
    case 452: {
        ClearFrameSelection(app);
        RefreshRequest(-1);
        RegisterCameraState(app, app->state.currentFrame);
        PanelPaint(app);
        SelectionReeval(app);
        break;
    }

    // ------------------------------------------------------------------
    // 467 (0x0048648F): accessory reset - position (-0.5,-1.0,0.5),
    // pose blob memset + kind 3, color 0.6, scene vtable 0xCC/0xD4,
    // %3d / %+3.1f echoes into 461-466, TBM_SETPOS 455-460, Refresh(-2).
    // ------------------------------------------------------------------
    case 467: {
        float* const direction = app->LightDirection();
        direction[0] = -0.5f;
        direction[1] = -1.0f;
        direction[2] = 0.5f;
        D3DLIGHT9& light = app->SceneLight();
        light = {};
        light.Type = D3DLIGHT_DIRECTIONAL;
        light.Direction = {direction[0], direction[1], direction[2]};
        light.Diffuse = {};
        // 0.602f is bit-exact with the x64 immediate 0x3F1A1CAC
        // (0x7FF7CB46A492..0x7FF7CB46A4BC, written to all three light
        // channels and copied into Specular); same constant as the
        // light-key default in command_frame_edit.cpp ResetLightRecord.
        const float col = 0.602f;  // flt_52C9A4
        app->LightColor()[0] = col;
        app->LightColor()[1] = col;
        app->LightColor()[2] = col;
        light.Ambient = {col, col, col, 0.0f};
        light.Specular = {col, col, col, 0.0f};
        ApplySceneLight(app);
        EnableSceneLight(app);

        char buf[256];
        for (int ch = 0; ch < 3; ++ch) {
            sprintf_s(buf, 0x100, "%3d",
                      static_cast<int>(static_cast<double>(
                                           app->LightColor()[ch]) *
                                       256.0));  // dbl_52B9E8
            SetWindowTextA(GetDlgItem(hwnd, 0x1CD + ch), buf);  // 461..463
        }
        for (int ax = 0; ax < 3; ++ax) {
            sprintf_s(buf, 0x100, "%+3.1f",
                      static_cast<double>(direction[ax]));
            SetWindowTextA(GetDlgItem(hwnd, 0x1D0 + ax), buf);  // 464..466
        }
        for (int ch = 0; ch < 3; ++ch) {
            SendMessageA(GetDlgItem(hwnd, panel::kLightColorSliderR + ch), TBM_SETPOS,
                         1, static_cast<LPARAM>(static_cast<int>(
                                static_cast<double>(
                                    app->LightColor()[ch]) *
                                256.0)));  // sliders 455..457
        }
        for (int ax = 0; ax < 3; ++ax) {
            SendMessageA(GetDlgItem(hwnd, panel::kLightDirSliderX + ax), TBM_SETPOS,
                         1, static_cast<LPARAM>(static_cast<int>(
                                static_cast<double>(
                                    direction[ax]) *
                                100.0)));  // sliders 458..460, dbl_52B8E0
        }
        RefreshRequest(-2);
        break;
    }

    // ------------------------------------------------------------------
    // 468 (0x004868C2): clear frame selection (like 452); Refresh(-2),
    // RegisterLightState(app, app+0x980), PanelPaint.
    // ------------------------------------------------------------------
    case 468: {
        ClearFrameSelection(app);
        RefreshRequest(-2);
        RegisterLightState(app, app->state.currentFrame);
        PanelPaint(app);
        break;
    }

    // ------------------------------------------------------------------
    // 472 (0x00486A59): load accessory via OPENFILENAMEW; initial dir
    // gated on menu item 0x12D, EN/JP filter + title, defext "x";
    // dir-copy into app+0xA1D10 when the menu gate is set, then
    // LoadAccessoryFile(app, path).
    // ------------------------------------------------------------------
    case 472: {
        SetCurrentDirectoryW(app->ExeDir());  // 0xA06CE
        wchar_t fileBuf[0x100];
        swprintf_s(fileBuf, 0x100, kEmptyPathFormat, L"", L"");
        OPENFILENAMEW ofn;
        memset(&ofn, 0, sizeof(ofn));
        ofn.lStructSize = sizeof(ofn);
        ofn.hwndOwner = app->state.floatingWindow != 0
                            ? reinterpret_cast<HWND>(
                                  app->state.floatingWindow)
                            : hwnd;
        if (app->state.englishUI != 0) {
            ofn.lpstrFilter = L"accessory files(*.x,*.vac)\0*.x;*.vac\0";
            ofn.lpstrTitle = L"open file";
        } else {
            ofn.lpstrFilter = kFilterAccJp;
            ofn.lpstrTitle = kTitleOpenJp;
        }
        if ((GetMenuState(GetMenu(hwnd), 0x12D, 0) & 8) != 0) {
            ofn.lpstrInitialDir = app->DirAccs();  // 0xA1D10
        } else {
            ofn.lpstrInitialDir = L"UserFile\\Accessory";
        }
        ofn.lpstrDefExt = L"x";
        wchar_t fileTitle[0x100];
        ofn.lpstrFile = fileBuf;
        ofn.nMaxFile = 0x100;
        ofn.lpstrFileTitle = fileTitle;
        ofn.nMaxFileTitle = 0x100;
        ofn.Flags = OFN_FILEMUSTEXIST;
        if (!GetOpenFileNameW(&ofn)) {
            break;
        }
        if ((GetMenuState(GetMenu(hwnd), 0x12D, 0) & 8) != 0) {
            // ExtractDirFromPath(app+0x9F338, fileBuf) then copy into DirAccs
            wchar_t* dir =
                ExtractDirFromPath(app->PathWorkspace().projectDirectory,
                                   fileBuf);
            CopyDirPathW(app->DirAccs(), dir);
        }
        // original thiscall: LoadAccessoryFile(app, fileBuf)
        LoadAccessoryFile(fileBuf);
        break;
    }

    // ------------------------------------------------------------------
    // 473 (0x00486BA7): delete accessory selected in combo 0x1D7.
    // CB_GETCURSEL(0x1D7) -> find slot (table 0x9DD70, id byte acc+0x49D);
    // confirm box (EN/JP, MB_OKCANCEL + 0x40000 when app+0xA0D38 != 0);
    // dispose slot blob, allocate fresh 0x927C0 blob, fix up indices,
    // rebuild combos 0x1D7/0x1DA/0x1DB, clear checkbox labels 0x1DE..0x1E4,
    // EnableMenuItem 0xF9, RebuildCameraModePanel, Refresh(-1), PostLanguageSweep2.
    // ------------------------------------------------------------------
    case 473: {
        const LRESULT sel =
            SendMessageA(GetDlgItem(hwnd, panel::kAccessoryCombo), CB_GETCURSEL, 0, 0);
        std::int32_t found = -1;
        for (int i = 0; i < 0xFF; ++i) {
            mdl::AccessoryRecord* acc = app->AccessorySlot(i);
            if (acc != nullptr &&
                static_cast<int>(acc->order) == static_cast<int>(sel)) {
                found = i;
                break;
            }
        }
        if (found < 0) {
            break;
        }
        app->state.enterKeyState = 1;  // 0xBC
        mdl::AccessoryRecord* acc = app->AccessorySlot(found);

        char text[0x100];
        if (app->state.englishUI != 0) {
            sprintf_s(text, 0x100,
                      "Trying to delete Accessory(%s).\n"
                      "All flame data about this accessory will be deleted "
                      "too.\n(This operation cannot undo!!)\n\nAre you OK?",
                      acc->name);
        } else {
            sprintf_s(text, 0x100, kMsgDelAccessoryJp,
                      acc->name);
        }
        const std::uint32_t flags =
            app->state.floatingWindow != 0 ? (MB_OKCANCEL | MB_TOPMOST) : MB_OKCANCEL;
        const char* caption = app->state.englishUI != 0
                                  ? "delete accessory"
                                  : kCaptionDelAccessoryJp;
        if (MessageBoxA(hwnd, text, caption, flags) != IDOK) {
            break;
        }

        const std::int32_t oldSel = acc->order;
        // dispose the accessory blob
        if (acc != nullptr) {
            DeleteAccessory(acc, 1);
        }
        app->AccessorySlot(found) = nullptr;
        // dispose the previous frame blob of this slot
        if (app->AccessoryKeys(found) != nullptr) {
            ::operator delete(app->AccessoryKeys(found));
            app->AccessoryKeys(found) = nullptr;
        }
        // fresh 0x927C0 frame blob with an identity frame seeded
        app->SceneModified() = 1;
        unsigned char* blob = static_cast<unsigned char*>(::operator new(0x927C0));
        app->AccessoryKeys(found) = reinterpret_cast<mdl::AccessoryKey*>(blob);
        memset(blob, 0, 0x927C0);
        blob[0x0C] = 1;
        *reinterpret_cast<std::int32_t*>(blob + 0x10) = -1;
        *reinterpret_cast<float*>(blob + 0x34) = 1.0f;
        *reinterpret_cast<float*>(blob + 0x38) = 1.0f;
        // combo rebuild: 0x1D7 select = oldSel, 0x1B2 index shifted
        SendMessageA(GetDlgItem(hwnd, panel::kAccessoryCombo), CB_DELETESTRING,
                     static_cast<WPARAM>(sel), 0);
        SendMessageA(GetDlgItem(hwnd, panel::kRegisterScopeCombo), CB_DELETESTRING,
                     static_cast<WPARAM>(sel + 4), 0);
        SendMessageA(GetDlgItem(hwnd, panel::kAccessoryCombo), CB_SETCURSEL, 0, 0);
        // fix indices above the deleted one
        for (int i = 0; i < 0xFF; ++i) {
            mdl::AccessoryRecord* p = app->AccessorySlot(i);
            if (p != nullptr && static_cast<int>(p->order) > oldSel) {
                --p->order;
            }
        }
        // find the first slot with index 0 (new selection)
        std::int32_t firstFree = 0x100;
        for (int i = 0; i < 0xFF; ++i) {
            mdl::AccessoryRecord* p = app->AccessorySlot(i);
            if (p != nullptr && p->order == 0) {
                firstFree = i;
                break;
            }
        }
        if (firstFree == 0x100) {
            app->state.selectedObjectSlot = 0;
            SendMessageA(GetDlgItem(hwnd, panel::kMainComboGround), CB_SETCURSEL, -1, 0);
        } else {
            SendMessageA(GetDlgItem(hwnd, panel::kAccessoryCombo), CB_SETCURSEL, 0, 0);
            app->state.selectedObjectSlot =
                static_cast<std::uint8_t>(firstFree);
            SyncAccessoryEditPanel(app);
        }
        SendMessageA(GetDlgItem(hwnd, panel::kAttachBoneCombo), CB_RESETCONTENT, 0, 0);
        // clear the checkbox labels 0x1DE..0x1E4
        char empty[0x100];
        strcpy_s(empty, 0x100, "");
        for (int ctl = 0x1DE; ctl <= 0x1E4; ++ctl) {
            HWND edit = GetDlgItem(hwnd, ctl);
            SendMessageA(edit, EM_SETSEL, 0,
                         static_cast<LPARAM>(GetWindowTextLengthA(edit)));
            SendMessageA(edit, EM_REPLACESEL, 0,
                         reinterpret_cast<LPARAM>(empty));
        }
        EnableMenuItem(GetMenu(hwnd), 0xF9, 1);
        RebuildCameraModePanel(app);
        SendMessageA(GetDlgItem(hwnd, panel::kInterpCurveCombo), CB_DELETESTRING, 5, 0);
        SendMessageA(GetDlgItem(hwnd, panel::kInterpCurveCombo), CB_DELETESTRING, 4, 0);
        PostModelReload2(app);
        app->GlobalTrackSelected(GlobalTimelineTrack::Camera) = 0;
        RefreshRequest(-1);
        InvalidateRect(hwnd, nullptr, FALSE);
        PostLanguageSweep2(app);
        break;
    }

    // ------------------------------------------------------------------
    // 476 (0x00486F91): toggle accessory display flag byte acc+0x210,
    // BM_SETCHECK into checkbox 0x1DC, Refresh(byte 0x9E170).
    // ------------------------------------------------------------------
    case 476: {
        const std::uint8_t idx = app->state.selectedObjectSlot;
        mdl::AccessoryRecord* acc = app->AccessorySlot(idx);
        if (acc == nullptr) {
            break;
        }
        if (acc->visible != 0) {
            acc->visible = 0;
            SendMessageA(GetDlgItem(hwnd, panel::kAccessoryVisibleCheckbox), BM_SETCHECK, 0, 0);
        } else {
            acc->visible = 1;
            SendMessageA(GetDlgItem(hwnd, panel::kAccessoryVisibleCheckbox), BM_SETCHECK, 1, 0);
        }
        RefreshRequest(idx);
        break;
    }

    // ------------------------------------------------------------------
    // 477 (0x0048A199): toggle accessory flag byte acc+0x49E; the case
    // then falls into def_47E903, which is a no-op for this id (see the
    // default note) - equivalent to break here.
    // ------------------------------------------------------------------
    case 477: {
        const std::uint8_t idx = app->state.selectedObjectSlot;
        mdl::AccessoryRecord* acc = app->AccessorySlot(idx);
        if (acc == nullptr) {
            break;
        }
        acc->additiveBlend = acc->additiveBlend == 0 ? 1 : 0;
        break;
    }

    // ------------------------------------------------------------------
    // 486 (0x00487011): toggle accessory flag byte acc+0x49C,
    // BM_SETCHECK into checkbox 0x1E6, Refresh(byte 0x9E170).
    // ------------------------------------------------------------------
    case 486: {
        const std::uint8_t idx = app->state.selectedObjectSlot;
        mdl::AccessoryRecord* acc = app->AccessorySlot(idx);
        if (acc == nullptr) {
            break;
        }
        if (acc->shadowEnabled != 0) {
            acc->shadowEnabled = 0;
            SendMessageA(GetDlgItem(hwnd, panel::kAccessoryShadowCheckbox), BM_SETCHECK, 0, 0);
        } else {
            acc->shadowEnabled = 1;
            SendMessageA(GetDlgItem(hwnd, panel::kAccessoryShadowCheckbox), BM_SETCHECK, 1, 0);
        }
        RefreshRequest(idx);
        break;
    }

    // ------------------------------------------------------------------
    // 487 (0x0048696B): sync the per-accessory checkboxes 0x1DE..0x1E4
    // (CommitEditControl), clear the frame selection, then Refresh(byte 0x9E170)
    // + RegisterAccessoryKey(app, app+0x980, byte 0x9E170) + PanelPaint.
    // ------------------------------------------------------------------
    case 487: {
        for (int ctl = 0x1DE; ctl <= 0x1E4; ++ctl) {
            CommitEditControl(app, GetDlgItem(hwnd, ctl));
        }
        app->SceneModified() = 1;
        const std::uint8_t idx = app->state.selectedObjectSlot;
        if (app->AccessorySlot(idx) == nullptr) {
            break;
        }
        ClearFrameSelection(app);
        RefreshRequest(idx);
        RegisterAccessoryKey(app, app->state.currentFrame, idx);
        PanelPaint(app);
        break;
    }

    // ------------------------------------------------------------------
    // 490..493 (0x0047ECD6 / 0x0047ED57 / 0x0047EDDA / 0x0047EE5E):
    // physics-mode radios 0x1EA..0x1ED.  The clicked radio stays checked;
    // the other three are unchecked and app+0x914 stores the mode
    // (0/1/3/4 when the corresponding radio is checked, 2 when none).
    // ------------------------------------------------------------------
    case 490: {
        SendMessageA(GetDlgItem(hwnd, panel::kBoxSelectRadio), BM_SETCHECK, 0, 0);
        SendMessageA(GetDlgItem(hwnd, panel::kBoneMoveRadio), BM_SETCHECK, 0, 0);
        SendMessageA(GetDlgItem(hwnd, panel::kBoneRotateRadio), BM_SETCHECK, 0, 0);
        app->EditMode() = IsDlgButtonChecked(hwnd, panel::kBoneSelectRadio) != 0
            ? ViewportEditMode::Bone : ViewportEditMode::None;
        break;
    }

    case 491: {
        SendMessageA(GetDlgItem(hwnd, panel::kBoneSelectRadio), BM_SETCHECK, 0, 0);
        SendMessageA(GetDlgItem(hwnd, panel::kBoneMoveRadio), BM_SETCHECK, 0, 0);
        SendMessageA(GetDlgItem(hwnd, panel::kBoneRotateRadio), BM_SETCHECK, 0, 0);
        app->EditMode() = IsDlgButtonChecked(hwnd, panel::kBoxSelectRadio) != 0
            ? ViewportEditMode::BoneBox : ViewportEditMode::None;
        break;
    }

    case 492: {
        SendMessageA(GetDlgItem(hwnd, panel::kBoxSelectRadio), BM_SETCHECK, 0, 0);
        SendMessageA(GetDlgItem(hwnd, panel::kBoneSelectRadio), BM_SETCHECK, 0, 0);
        SendMessageA(GetDlgItem(hwnd, panel::kBoneRotateRadio), BM_SETCHECK, 0, 0);
        app->EditMode() = IsDlgButtonChecked(hwnd, panel::kBoneMoveRadio) != 0
            ? ViewportEditMode::Light : ViewportEditMode::None;
        break;
    }

    case 493: {
        SendMessageA(GetDlgItem(hwnd, panel::kBoxSelectRadio), BM_SETCHECK, 0, 0);
        SendMessageA(GetDlgItem(hwnd, panel::kBoneMoveRadio), BM_SETCHECK, 0, 0);
        SendMessageA(GetDlgItem(hwnd, panel::kBoneSelectRadio), BM_SETCHECK, 0, 0);
        app->EditMode() = IsDlgButtonChecked(hwnd, panel::kBoneRotateRadio) != 0
            ? ViewportEditMode::Camera : ViewportEditMode::None;
        break;
    }

    // ------------------------------------------------------------------
    // 494 (0x0047FB24): select all bones.  A bone is selected when
    // (flag word +0x1F4 has bit 0x400 and type byte +0x1E4 == 4) or
    // type == 8 or type <= 6; then check radio 0x1EA, re-dispatch
    // WM_COMMAND(0x1EA) and run PostLanguageSweep + PostLanguageSweep2.
    // ------------------------------------------------------------------
    case 494: {
        unsigned char* model = ActiveModel(app);
        if (model == nullptr) {
            // Selected slot empty: walking the null record here is fatal;
            // keep the radio bookkeeping below and skip the scan.
            SendMessageA(GetDlgItem(hwnd, panel::kBoneSelectRadio), BM_SETCHECK, 1, 0);
            SendMessageA(hwnd, WM_COMMAND, 0x1EA, 0);
            PostLanguageSweep(app);
            PostLanguageSweep2(app);
            break;
        }
        auto* const modelRecord = mikudancestudio::mdl::Mdl(model);
        const std::int32_t boneCount = modelRecord->boneCount;
        mikudancestudio::mdl::BoneRecord* const bones = modelRecord->boneTable;
        unsigned char* const sel = modelRecord->boneSelection;
        for (std::int32_t i = 0; i < boneCount; ++i) {
            const mdl::BoneType type = bones[i].type;
            const std::uint16_t flag = bones[i].flags;
            const bool hit =
                ((flag & mdl::kBoneFlagFixedAxis) == mdl::kBoneFlagFixedAxis &&
                 type == mdl::BoneType::UnderIk) ||
                type == mdl::BoneType::FixedAxis ||
                type <= mdl::BoneType::Effector;
            if (hit) {
                sel[i] = 1;
            }
        }
        // raw command id kept: 0x1EA is the original's bone-select radio
        // command; the WM_COMMAND switch below keys the same literal.
        SendMessageA(GetDlgItem(hwnd, panel::kBoneSelectRadio), BM_SETCHECK, 1, 0);
        SendMessageA(hwnd, WM_COMMAND, 0x1EA, 0);
        PostLanguageSweep(app);
        PostLanguageSweep2(app);
        break;
    }

    // ------------------------------------------------------------------
    // 495 (0x0047F3FA): insert keyframes at the current frame for all
    // selected bones.  Bone 0 seeds identity; a selected bone without
    // parent bakes its current transform via D3DXMatrixRotationQuaternion
    // of its stored quaternion (rotation applied to the bone's own
    // position, frame-0 position added, minus the position offset); a
    // parented bone gets identity written.  Values are only written when
    // they differ from the stored frame (fucompp change detection), and
    // the frame-flag array model+0x2D98 marks inserted frames.  Float
    // math follows the original x87 shape (double intermediates).
    // ------------------------------------------------------------------
    case 495: {
        PushBoneEditUndo(app);  // prep (thiscall)
        unsigned char* model = ActiveModel(app);
        auto* modelRecord = mikudancestudio::mdl::Mdl(model);
        unsigned char* boneSel = modelRecord->boneSelection;
        if (boneSel[0] != 0) {
            // bone-0 seed: pos/rot identity (0x144, 0x14C..0x154 = 0,
            // 0x158 = 1); the x87 0.0/1.0 pair is kept live across the
            // stores in the original
            auto& root = modelRecord->boneTable[0];
            root.trans[1] = 0.0f;
            root.rotQuat[0] = 0.0f;
            root.rotQuat[1] = 0.0f;
            root.rotQuat[2] = 0.0f;
            root.rotQuat[3] = 1.0f;
        }
        const std::int32_t boneCount = modelRecord->boneCount;
        unsigned char* frameFlag = modelRecord->bonePhysicsState;
        for (std::int32_t i = 1; i < boneCount; ++i) {
            model = ActiveModel(app);
            modelRecord = mikudancestudio::mdl::Mdl(model);
            boneSel = modelRecord->boneSelection;
            frameFlag = modelRecord->bonePhysicsState;
            if (boneSel[i] == 0) {
                continue;
            }
            mikudancestudio::mdl::BoneRecord* const bones = modelRecord->boneTable;
            mikudancestudio::mdl::BoneRecord* bone = &bones[i];
            if (bone->parent == -1) {
                // ---- no parent: bake via own quaternion ----------------
                d3dx::D3DXMATRIXF mat{};
                auto* d3dx = &d3dx::Get();
                {
                    d3dx->matrixRotationQuaternion(
                        &mat, reinterpret_cast<const float*>(bone->rotQuat));
                }
                const double posX =
                    *reinterpret_cast<float*>(bone->position);
                const double posY =
                    bone->position[1];
                const double posZ =
                    bone->position[2];
                // var_9CC = _11*x + _21*y + _31*z + _41 - x + frame0.x
                double nX = posY * mat.m[1][0];
                nX += posX * mat.m[0][0];
                nX += posZ * mat.m[2][0];
                nX += mat.m[3][0];
                nX -= posX;
                nX += bones[0].trans[0];
                // var_9C8 = _12*x + _22*y + _32*z + _42 - y + frame0.y
                double nY = posY * mat.m[1][1];
                nY += posX * mat.m[0][1];
                nY += posZ * mat.m[2][1];
                nY += mat.m[3][1];
                nY -= posY;
                nY += bones[0].trans[1];
                // var_9C4 = _13*x + _23*y + _33*z + _43 - z + frame0.z
                double nZ = posY * mat.m[1][2];
                nZ += posX * mat.m[0][2];
                nZ += posZ * mat.m[2][2];
                nZ += mat.m[3][2];
                nZ -= posZ;
                nZ += bones[0].trans[2];
                // change detection vs stored keyframe
                const int f0 =
                    (*reinterpret_cast<float*>(bone->trans) != nX) ? 1 : 0;
                const int f1 =
                    (bone->trans[1] != nY) ? 1 : 0;
                const int f2 =
                    (bone->trans[2] != nZ) ? 1 : 0;
                const int f3 = (*reinterpret_cast<float*>(bone->rotQuat) !=
                                bones[0].rotQuat[0])
                                   ? 1
                                   : 0;
                const int f4 = (bone->rotQuat[1] !=
                                bones[0].rotQuat[1])
                                   ? 1
                                   : 0;
                const int f5 = (bone->rotQuat[2] !=
                                bones[0].rotQuat[2])
                                   ? 1
                                   : 0;
                const int f6 = (bone->rotQuat[3] !=
                                bones[0].rotQuat[3])
                                   ? 1
                                   : 0;
                if ((f0 | f1 | f2 | f3 | f4 | f5 | f6) != 0) {
                    *reinterpret_cast<float*>(bone->trans) =
                        static_cast<float>(nX);
                    bone->trans[1] =
                        static_cast<float>(nY);
                    bone->trans[2] =
                        static_cast<float>(nZ);
                    // frame-0 quaternion copied into this bone's frame
                    memcpy(bone->rotQuat, bones[0].rotQuat,
                           sizeof bone->rotQuat);
                    frameFlag[i] = 1;
                }
            } else {
                // ---- has parent: identity written ----------------------
                // comparisons are made against 0.0 for pos + quat x/y/z and
                // 1.0 for quat w (the x87 keeps a live [1.0, 0.0] pair)
                const int g0 =
                    (*reinterpret_cast<float*>(bone->trans) != 0.0) ? 1 : 0;
                const int g1 =
                    (bone->trans[1] != 0.0) ? 1 : 0;
                const int g2 =
                    (bone->trans[2] != 0.0) ? 1 : 0;
                const int g3 =
                    (*reinterpret_cast<float*>(bone->rotQuat) != 0.0) ? 1 : 0;
                const int g4 =
                    (bone->rotQuat[1] != 0.0) ? 1 : 0;
                const int g5 =
                    (bone->rotQuat[2] != 0.0) ? 1 : 0;
                const int g6 =
                    (bone->rotQuat[3] != 1.0) ? 1 : 0;
                if ((g0 | g1 | g2 | g3 | g4 | g5 | g6) != 0) {
                    float* b = reinterpret_cast<float*>(bone->trans);
                    b[0] = 0.0f;  // 0x140
                    b[1] = 0.0f;  // 0x144
                    b[2] = 0.0f;  // 0x148
                    b[3] = 0.0f;  // 0x14C
                    b[4] = 0.0f;  // 0x150
                    // 0x154 untouched by the original
                    b[6] = 1.0f;  // 0x158
                    frameFlag[i] = 1;
                }
            }
        }
        PostViewRefresh(app);
        break;
    }

    // ------------------------------------------------------------------
    // 496 (0x00481926): bone copy - count the selected bones, enable the
    // paste controls 0x1F1/0x1F2, snapshot each selected bone's name,
    // stored pos (0x140..0x148) and stored quat (0x14C..0x158) into a
    // 0x30-stride record array at app+0x350 (name[20], pos[3], quat[4]).
    // ------------------------------------------------------------------
    case 496: {
        unsigned char* model = ActiveModel(app);
        auto* modelRecord = mikudancestudio::mdl::Mdl(model);
        const std::int32_t boneCount = modelRecord->boneCount;
        std::int32_t count = 0;
        if (boneCount > 0) {
            unsigned char* sel = modelRecord->boneSelection;
            for (std::int32_t i = 0; i < boneCount; ++i) {
                if (sel[i] != 0) {
                    ++count;
                }
            }
        }
        if (count == 0) {
            break;  // jz def_47E903 (no-op)
        }
        EnableWindow(GetDlgItem(hwnd, panel::kBonePasteButton), TRUE);
        EnableWindow(GetDlgItem(hwnd, panel::kBoneReversePasteButton), TRUE);
        app->state.copiedBoneCount = count;
        auto& records =
            reinterpret_cast<BoneCopyRecord*&>(app->BoneCopyRecords());
        if (records != nullptr) {
            free(records);
            records = nullptr;
        }
        auto* block = static_cast<BoneCopyRecord*>(::operator new(
            MulOrMax(static_cast<std::uint32_t>(count),
                     sizeof(BoneCopyRecord))));
        if (block != nullptr) {
            ConstructArrayElements(block, sizeof(BoneCopyRecord),
                                   static_cast<std::uint32_t>(count),
                                   &IdentityCtor);
        }
        records = block;
        memset(block, 0,
               static_cast<std::size_t>(count) * sizeof(BoneCopyRecord));
        app->state.copiedBoneCount = 0;  // fill cursor
        model = ActiveModel(app);
        modelRecord = mikudancestudio::mdl::Mdl(model);
        const std::int32_t nBones = modelRecord->boneCount;
        mikudancestudio::mdl::BoneRecord* const bones = modelRecord->boneTable;
        unsigned char* sel = modelRecord->boneSelection;
        std::int32_t cursor = 0;
        for (std::int32_t i = 0; i < nBones; ++i) {
            if (sel[i] == 0) {
                continue;
            }
            BoneCopyRecord& rec = records[cursor];
            strcpy_s(rec.name, bones[i].name);
            memcpy(rec.position, bones[i].trans, sizeof rec.position);
            memcpy(rec.rotation, bones[i].rotQuat, sizeof rec.rotation);
            ++cursor;
            // 0x481B1E updates the shared copy count after every emitted
            // record.  Paste (497/498) gates directly on this field.
            app->state.copiedBoneCount = cursor;
        }
        break;
    }

    // ------------------------------------------------------------------
    // 497 (0x00481B4B): bone paste - clear the selection, bump the paste
    // counter (model+0x31B4, wraps at 0x1E), allocate a 0x24-stride undo
    // record blob (model+0x26FC, 28-byte stride per paste step) and paste
    // each copied bone by exact name match: undo data (old pos/quat) into
    // the record, new values into the bone frames, selection + frame
    // flags, current bone = pasted index, then the language sweeps.
    // ------------------------------------------------------------------
    case 497: {
        if (app->state.copiedBoneCount == 0) {
            break;  // jz def_47E903 (no-op)
        }
        unsigned char* model = ActiveModel(app);
        auto* modelRecord = mikudancestudio::mdl::Mdl(model);
        const std::int32_t boneCount = modelRecord->boneCount;
        unsigned char* sel = modelRecord->boneSelection;
        for (std::int32_t i = 0; i < boneCount; ++i) {
            sel[i] = 0;
        }
        EnableWindow(GetDlgItem(hwnd, panel::kUndoButton), TRUE);
        EnableWindow(GetDlgItem(hwnd, panel::kRedoButton), FALSE);
        modelRecord->undoDirty = 1;
        modelRecord->redoDirty = 0;
        ++modelRecord->undoState[0];
        if (modelRecord->undoState[0] >= 0x1E) {
            modelRecord->undoState[0] = 0;
        }
        const std::int32_t pasteIdx = modelRecord->undoState[0];
        modelRecord->undoState[1] = pasteIdx;
        auto& undo = modelRecord->undoRings[0].slots[pasteIdx];
        undo.operation = 1;
        // count stored 28*(pasteIdx+0x164) dwords past the undo-table base
        undo.dirty = app->state.copiedBoneCount;
        unsigned char* pasteBlob =
            reinterpret_cast<unsigned char*>(undo.bonePose);
        if (pasteBlob != nullptr) {
            ::operator delete(pasteBlob);
            undo.bonePose = nullptr;
        }
        const std::int32_t count =
            app->state.copiedBoneCount;
        pasteBlob = static_cast<unsigned char*>(::operator new(
            MulOrMax(static_cast<std::uint32_t>(count), 0x24u)));
        if (pasteBlob != nullptr) {
            ConstructArrayElements(pasteBlob, 0x24,
                                   static_cast<std::uint32_t>(count),
                                   &IdentityCtor);
        }
        undo.bonePose = reinterpret_cast<mikudancestudio::mdl::BonePoseSnapshot*>(
            pasteBlob);
        memset(pasteBlob, 0, static_cast<std::size_t>(count) * 0x24u);

        auto& records =
            reinterpret_cast<BoneCopyRecord*&>(app->BoneCopyRecords());
        for (std::int32_t j = 0; j < count; ++j) {
            model = ActiveModel(app);
            modelRecord = mikudancestudio::mdl::Mdl(model);
            const std::int32_t nBones = modelRecord->boneCount;
            sel = modelRecord->boneSelection;
            mikudancestudio::mdl::BoneRecord* const bones = modelRecord->boneTable;
            unsigned char* frameFlag = modelRecord->bonePhysicsState;
            const BoneCopyRecord& src = records[j];
            // exact name match (the original's inline 2-byte-step compare
            // is byte-identical to strcmp)
            std::int32_t found = -1;
            for (std::int32_t k = 0; k < nBones; ++k) {
                if (strcmp(bones[k].name, src.name) == 0) {
                    found = k;
                    break;
                }
            }
            if (found < 0) {
                continue;
            }
            pasteBlob = reinterpret_cast<unsigned char*>(undo.bonePose);
            auto& rec = reinterpret_cast<mikudancestudio::mdl::BonePoseSnapshot*>(
                pasteBlob)[j];
            mikudancestudio::mdl::BoneRecord* bone = &bones[found];
            rec.boneIndex = found;
            sel[found] = 1;
            // old pos into the undo record
            memcpy(rec.position, bone->trans, sizeof rec.position);
            // new pos from the copy record
            memcpy(bone->trans, src.position, sizeof bone->trans);
            // old quat into the undo record, new quat into the bone
            memcpy(rec.rotation, bone->rotQuat, sizeof rec.rotation);
            memcpy(bone->rotQuat, src.rotation, sizeof bone->rotQuat);
            rec.physicsDisabled = frameFlag[found];
            frameFlag[found] = 1;
            modelRecord->selectedBone = found;
            PostLanguageSweep(app);
            PostLanguageSweep2(app);
        }
        SetFocus(hwnd);
        app->PhysicsResetPending() = 1;
        break;
    }

    // ------------------------------------------------------------------
    // 498 (0x0048203D): bone paste, delete-tag variant.  Same undo-record
    // bookkeeping as 497; the paste target is resolved through the
    // "ボーン削除" name tags instead of a plain name match, and the pasted
    // transform is mirrored (X position and quat Y/Z negated).  When the
    // tag search fails the exact-name match index (k1) is used as the
    // fallback target.
    // ------------------------------------------------------------------
    case 498: {
        if (app->state.copiedBoneCount == 0) {
            break;  // jz def_47E903 (no-op)
        }
        unsigned char* model = ActiveModel(app);
        auto* modelRecord = mikudancestudio::mdl::Mdl(model);
        const std::int32_t boneCount = modelRecord->boneCount;
        unsigned char* sel = modelRecord->boneSelection;
        for (std::int32_t i = 0; i < boneCount; ++i) {
            sel[i] = 0;
        }
        EnableWindow(GetDlgItem(hwnd, panel::kUndoButton), TRUE);
        EnableWindow(GetDlgItem(hwnd, panel::kRedoButton), FALSE);
        modelRecord->undoDirty = 1;
        modelRecord->redoDirty = 0;
        ++modelRecord->undoState[0];
        if (modelRecord->undoState[0] >= 0x1E) {
            modelRecord->undoState[0] = 0;
        }
        const std::int32_t pasteIdx = modelRecord->undoState[0];
        modelRecord->undoState[1] = pasteIdx;
        auto& undo = modelRecord->undoRings[0].slots[pasteIdx];
        undo.operation = 1;
        undo.dirty = app->state.copiedBoneCount;
        unsigned char* pasteBlob =
            reinterpret_cast<unsigned char*>(undo.bonePose);
        if (pasteBlob != nullptr) {
            ::operator delete(pasteBlob);
            undo.bonePose = nullptr;
        }
        const std::int32_t count =
            app->state.copiedBoneCount;
        pasteBlob = static_cast<unsigned char*>(::operator new(
            MulOrMax(static_cast<std::uint32_t>(count), 0x24u)));
        if (pasteBlob != nullptr) {
            ConstructArrayElements(pasteBlob, 0x24,
                                   static_cast<std::uint32_t>(count),
                                   &IdentityCtor);
        }
        undo.bonePose = reinterpret_cast<mikudancestudio::mdl::BonePoseSnapshot*>(
            pasteBlob);
        memset(pasteBlob, 0, static_cast<std::size_t>(count) * 0x24u);

        auto& records =
            reinterpret_cast<BoneCopyRecord*&>(app->BoneCopyRecords());
        for (std::int32_t j = 0; j < count; ++j) {
            model = ActiveModel(app);
            modelRecord = mikudancestudio::mdl::Mdl(model);
            const std::int32_t nBones = modelRecord->boneCount;
            sel = modelRecord->boneSelection;
            mikudancestudio::mdl::BoneRecord* const bones = modelRecord->boneTable;
            unsigned char* frameFlag = modelRecord->bonePhysicsState;
            const BoneCopyRecord& src = records[j];
            // step 1: exact name match -> k1
            std::int32_t k1 = -1;
            for (std::int32_t k = 0; k < nBones; ++k) {
                if (strcmp(bones[k].name, src.name) == 0) {
                    k1 = k;
                    break;
                }
            }
            if (k1 < 0) {
                continue;  // no exact-name bone: record skipped
            }
            std::int32_t target = -1;
            const char* tag1 = strstr(bones[k1].name, kBoneNameNeedleModelDelete);
            if (tag1 != nullptr) {
                // k1 carries the "ボーン削除" tag: find the "\x89\x45"-tagged
                // bone whose suffix matches the tag suffix
                const char* suffix = tag1 + 2;
                for (std::int32_t k = 0; k < nBones; ++k) {
                    const char* q = strstr(bones[k].name, kBoneNameNeedleRight);
                    if (q != nullptr && strcmp(q + 2, suffix) == 0) {
                        target = k;
                        break;
                    }
                }
            }
            if (target < 0) {
                // second search: needle from frames+j (raw byte offset, as
                // in the original) against the "ボーン削除" tag
                const char* suffix = strstr(
                    reinterpret_cast<const char*>(bones) + j, kBoneNameNeedleRight);
                if (suffix != nullptr) {
                    suffix += 2;
                    for (std::int32_t k = 0; k < nBones; ++k) {
                        const char* q = strstr(bones[k].name, kBoneNameNeedleModelDelete);
                        if (q != nullptr && strcmp(q + 2, suffix) == 0) {
                            target = k;
                            break;
                        }
                    }
                }
            }
            if (target < 0) {
                target = k1;  // fallback to the exact-name match
            }
            pasteBlob = reinterpret_cast<unsigned char*>(undo.bonePose);
            auto& rec = reinterpret_cast<mikudancestudio::mdl::BonePoseSnapshot*>(
                pasteBlob)[j];
            mikudancestudio::mdl::BoneRecord* bone = &bones[target];
            rec.boneIndex = target;
            sel[target] = 1;
            // old pos into the undo record
            memcpy(rec.position, bone->trans, sizeof rec.position);
            // new pos from the copy record - X negated (fchs)
            bone->trans[0] = -src.position[0];
            bone->trans[1] = src.position[1];
            bone->trans[2] = src.position[2];
            // old quat into the undo record
            memcpy(rec.rotation, bone->rotQuat, sizeof rec.rotation);
            // new quat from the copy record - Y and Z negated (fchs)
            bone->rotQuat[0] = src.rotation[0];
            bone->rotQuat[1] = -src.rotation[1];
            bone->rotQuat[2] = -src.rotation[2];
            bone->rotQuat[3] = src.rotation[3];
            rec.physicsDisabled = frameFlag[target];
            frameFlag[target] = 1;
            modelRecord->selectedBone = target;
            PostLanguageSweep(app);
            PostLanguageSweep2(app);
        }
        SetFocus(hwnd);
        app->PhysicsResetPending() = 1;
        break;
    }

    default:
        // def_47E903 (0x482897): only reachable actions are gated on
        // notify==BN_CLICKED and the sending control being 0x1B4/0x1BB..., a
        // compare chain that can never match ids 450/452/455-466/471/474/
        // 475/478-485/499 - no-op for the whole family.
        break;
    }
}

}  // namespace mikudancestudio
