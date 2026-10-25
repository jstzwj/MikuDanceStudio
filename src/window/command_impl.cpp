// ===========================================================================
// Eight File menu commands + PMM save: WM_COMMAND 202..206/209/210/213 (and 208)
// ===========================================================================
// Exact reconstruction of x64 sub_7FF7CB45F550 cases 0xCA..0xD5 (jump table 0x7FF7CB45F5DD),
// using x64 as the behavioral reference (x86 only confirms intent). Shared structure:
//   * Set letterKeyState[i]=1 / enterKeyState=1 on entry (x64 dword [app+0x34+4i] /
//     [app+0xC0]; indices calibrated against cases 221/233/248: 0x34+4*12=0x64, 0x34+4*17=0x78).
//   * SetCurrentDirectoryW(exe directory at x64 app+0xA16EE) -> swprintf_s(empty path)
//     -> OPENFILENAMEW (original filter/title strings, nMaxFile=nMaxFileTitle=0x100,
//     lpstrFileTitle points to a stack buffer).
//   * lpstrInitialDir: when menu 0x12D (GetMenuState&8, "remember opened-file directories")
//     is checked, use the per-type directory field; otherwise use the "UserFile\<Type>" literal.
//   * Store the directory after confirmation only when 0x12D is checked (copy the path into pathWorkspace's
//     projectDirectory temporary -> ExtractDirFromPath strips the filename -> copy back to the directory field;
//     inline pair in x64 0x7FF7CB429B80).
//   * Open dialog Flags=0x1000 (FILEMUSTEXIST only), owner=floating window (x64
//     app+0xA1DE0) or main window; save dialog Flags=6, nFilterIndex=1, owner=main window.
//   * Trailing writes differ by case (see comments in each function): loading VPD only requests reseat
//     (x64 0x9FCC1); loading VMD sets dirty+reseat; loading WAV/AVI sets dirty; saving
//     VPD/VMD or opening/saving PMM writes no flags afterward.
// =========================================================================//
#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include "mikudancestudio/menu_commands.h"
#include <Windows.h>
#include <commdlg.h>

#include <cstdint>
#include <cstdio>
#include <cstring>
#include <cwchar>

#include "mikudancestudio/mmd_app.hpp"
#include "mikudancestudio/model.hpp"
#include "mikudancestudio/ported_funcs.hpp"

namespace mikudancestudio {
namespace {

// Main-window handle (x64 app+0xA16C8). File-local use; command_file_menu.cpp has another
// non-static definition of the same name, so retain internal linkage here.
HWND MainHwnd(MMDApp* app) {
    return static_cast<HWND>(app->Hwnd());
}

// Scan model frame tables for allocated records (save-VMD selected-frame gate).
template <typename Key> bool AnyAllocatedKey(const Key* keys, std::size_t count) {
    if (keys == nullptr)
        return false;
    for (std::size_t i = 0; i < count; ++i) {
        if (keys[i].allocated != 0)
            return true;
    }
    return false;
}

// Menu item 0x12D: "remember opened-file directories". When checked (MF_CHECKED=8), each file
// dialog starts in its per-type directory and updates that directory after confirmation.
bool RememberFileDialogDirs(MMDApp* app) {
    return (GetMenuState(GetMenu(MainHwnd(app)), MDS_CMD_TOGGLE_PREVIOUS_FOLDER, 0) & 8) != 0;
}

// -----------------------------------------------------------------------
// Japanese text, byte-identical to x64 .rdata (Shift-JIS).
// -----------------------------------------------------------------------
// x64 0x7FF7CB54E790: "Save pose data" (save-VPD bone-selection prompt caption)
const char kCaptionSavePoseJp[] = "\x83\x7C\x81\x5B\x83\x59\x83\x66\x81\x5B\x83\x5E" // ポーズデータ
                                  "\x95\xDB\x91\xB6";                                // save
// x64 0x7FF7CB54E7B0: "Only selected bones are saved in pose data.\n
//                      Select some or all bones."
const char kMsgSavePoseJp[] =
    "\x83\x7C\x81\x5B\x83\x59\x83\x66\x81\x5B\x83\x5E\x82\xCD\x91\x49\x91\xF0"
    "\x82\xB3\x82\xEA\x82\xBD\x83\x7B\x81\x5B\x83\x93\x82\xCC\x82\xDD"
    "\x82\xAA\x83\x5A\x81\x5B\x83\x75\x82\xB3\x82\xEA\x82\xDC\x82\xB7\n"
    "\x83\x7B\x81\x5B\x83\x93\x82\xCC\x88\xEA\x95\x94\x81\x41\x82\xE0"
    "\x82\xB5\x82\xAD\x82\xCD\x91\x53\x82\xC4\x82\xF0\x91\x49\x91\xF0"
    "\x82\xB5\x82\xC4\x82\xAD\x82\xBE\x82\xB3\x82\xA2";
// x64 0x7FF7CB54E970: "Save motion data" (save-VMD frame-selection prompt caption)
const char kCaptionSaveMotionJp[] =
    "\x83\x82\x81\x5B\x83\x56\x83\x87\x83\x93\x83\x66\x81\x5B\x83\x5E" // モーションデータ
    "\x95\xDB\x91\xB6";                                                // save
// x64 0x7FF7CB54E990: "Selected frame points are
//                      saved as motion data.\nSelect some or all
//                      frames."
const char kMsgSaveMotionJp[] = "\x83\x82\x81\x5B\x83\x56\x83\x87\x83\x93\x83\x66\x81\x5B\x83\x5E"
                                "\x82\xCD\x91\x49\x91\xF0\x82\xB3\x82\xEA\x82\xBD"
                                "\x83\x74\x83\x8C\x81\x5B\x83\x80\x83\x7C\x83\x43\x83\x93\x83\x67"
                                "\x82\xAA\x83\x5A\x81\x5B\x83\x75\x82\xB3\x82\xEA\x82\xDC\x82\xB7\n"
                                "\x83\x74\x83\x8C\x81\x5B\x83\x80\x82\xCC\x88\xEA\x95\x94\x81\x41"
                                "\x82\xE0\x82\xB5\x82\xAD\x82\xCD\x91\x53\x82\xC4\x82\xF0\x91\x49"
                                "\x91\xF0\x82\xB5\x82\xC4\x82\xAD\x82\xBE\x82\xB3\x82\xA2";
// x64 0x7FF7CB54ED60: "New" (JP caption of File: New confirmation)
const char kCaptionNewJp[] = "\x90\x56\x8B\x4B\x8D\xEC\x90\xAC";
// x64 0x7FF7CB54EFF0: "The current state will be discarded.\n\nAre you sure?"
const char kMsgNewJp[] = "\x8C\xBB\x8D\xDD\x82\xCC\x8F\xF3\x91\xD4\x82\xCD\x94\x6A\x8A\xFC"
                         "\x82\xB3\x82\xEA\x82\xDC\x82\xB7\n\n"
                         "\x82\xE6\x82\xEB\x82\xB5\x82\xA2\x82\xC5\x82\xB7\x82\xA9\x81\x48";
// x64 0x7FF7CB54F068: "Open file" (JP caption of open-PMM dirty-scene confirmation)
const char kCaptionOpenFileJp[] = "\x83\x74\x83\x40\x83\x43\x83\x8B\x82\xF0\x8A\x4A\x82\xAD";
// x64 0x7FF7CB54F080: "There are unsaved changes.\n\nContinue
//                      loading anyway?"
const char kMsgOpenDirtyJp[] =
    "\x95\xDB\x91\xB6\x82\xB5\x82\xC4\x82\xA2\x82\xC8\x82\xA2\x95\xCF\x8D\x58"
    "\x93\x5F\x82\xAA\x82\xA0\x82\xE8\x82\xDC\x82\xB7\n\n"
    "\x82\xB1\x82\xCC\x82\xDC\x82\xDC\x83\x8D\x81\x5B\x83\x68\x82\xB5"
    "\x82\xC4\x82\xE6\x82\xEB\x82\xB5\x82\xA2\x82\xC5\x82\xB7\x82\xA9"
    "\x81\x48";

// -----------------------------------------------------------------------
// Dialog titles (wide characters, OPENFILENAMEW.lpstrTitle). Japanese strings match x64 .rdata's
// UTF-16 bytes; the original x64 text uses the abbreviated noun "load" without the Japanese particle/verb endings.
// -----------------------------------------------------------------------
// x64 0x7FF7CB54E8E8: JP L"Load pose data" (load VPD)
const wchar_t kTitleLoadPoseJp[] = L"\x30DD\x30FC\x30BA\x30C7\x30FC\x30BF\x8AAD\x8FBC";
// x64 0x7FF7CB54E8B0: JP L"Save pose data" (save VPD)
const wchar_t kTitleSavePoseJp[] = L"\x30DD\x30FC\x30BA\x30C7\x30FC\x30BF\x4FDD\x5B58";
// x64 0x7FF7CB54EAF0: JP L"Load motion data" (load VMD)
const wchar_t kTitleLoadMotionJp[] =
    L"\x30E2\x30FC\x30B7\x30E7\x30F3\x30C7\x30FC\x30BF\x8AAD\x8FBC";
// x64 0x7FF7CB54EAB0: JP L"Save motion data" (save VMD)
const wchar_t kTitleSaveMotionJp[] =
    L"\x30E2\x30FC\x30B7\x30E7\x30F3\x30C7\x30FC\x30BF\x4FDD\x5B58";
// x64 0x7FF7CB54D840: JP L"Load file" (open PMM)
const wchar_t kTitleOpenFileJp[] = L"\x30D5\x30A1\x30A4\x30EB\x3092\x8AAD\x8FBC";
// x64 0x7FF7CB54F188: JP L"Save file" (save PMM)
const wchar_t kTitleSaveFileJp[] = L"\x30D5\x30A1\x30A4\x30EB\x3092\x4FDD\x5B58\x3059\x308B";
// x64 aWaveFile / aAvi_0: WAV/AVI Japanese titles are plain ASCII strings
const wchar_t kTitleOpenWaveJp[] = L"WAVE File";
const wchar_t kTitleLoadAviJp[] = L"AVI";

// x64 0x7FF7CB54A3BC: swprintf_s format L"\0\0%s%s"; the leading NUL makes an argument-free
// call write an empty string (the path-buffer clearing idiom used by all file commands).
const wchar_t kFmtEmptyPath[] = L"\x0\x0%s%s";

// Shared file-dialog trailing parameters: nMaxFile/nMaxFileTitle are both 0x100.
constexpr DWORD kDialogPathLen = 0x100;

} // namespace

// ---------------------------------------------------------------------------
// 202 (0xCA, entry 0x7FF7CB46BCB5): File: load VPD pose.
//   [letterKeyState[12]]=1; enterKeyState=1 -> camera/accessory mode gate (nonzero x64 app+0x328
//   exits immediately) -> open dialog -> load. The tail only requests reseat
//   (0x9FCC1, consumed by three rigid-body resets in the pump), without setting dirty.
// ---------------------------------------------------------------------------
void CmdLoadPose(MMDApp* app) {
    app->LetterKeyState(LetterKey::P) = 1; // 0x46BCB5 [app+0x64]
    app->state.enterKeyState = 1;          // 0x46BCBC [app+0xC0]
    if (app->CameraMode() != 0) { // 0x46BCC6 gate: do not open the dialog in camera/accessory mode
        return;
    }
    SetCurrentDirectoryW(app->ExeDir()); // 0xA16EE
    wchar_t path[kDialogPathLen];
    swprintf_s(path, kDialogPathLen, kFmtEmptyPath, L"", L"");
    wchar_t fileTitle[kDialogPathLen];
    OPENFILENAMEW ofn;
    memset(&ofn, 0, sizeof(ofn));
    ofn.lStructSize = sizeof(ofn);
    ofn.hwndOwner = app->FloatingWindow() != nullptr // 0xA1DE0, otherwise main window
                        ? app->FloatingWindow()
                        : MainHwnd(app);
    ofn.lpstrFilter = L"Vocaloid Pose Data files(*.vpd)\0*.vpd\0";
    ofn.lpstrFile = path;
    ofn.nMaxFile = kDialogPathLen;
    ofn.Flags = OFN_FILEMUSTEXIST;                                     // 0x1000
    ofn.lpstrInitialDir = RememberFileDialogDirs(app) ? app->DirPose() // 0xA3D5C
                                                      : L"UserFile\\Pose";
    ofn.lpstrDefExt = L"vpd";
    ofn.nMaxFileTitle = kDialogPathLen;
    ofn.lpstrFileTitle = fileTitle;
    ofn.lpstrTitle = app->EnglishUI() != 0 ? L"load pose data" : kTitleLoadPoseJp;
    if (!GetOpenFileNameW(&ofn)) {
        return;
    }
    if (RememberFileDialogDirs(app)) { // Store directory only when 0x12D is checked
        wchar_t* dir = ExtractDirFromPath(app->PathWorkspace().projectDirectory, path);
        CopyDirPathW(app->DirPose(), dir); // 0x42AE20
    }
    LoadVpdFile(path);              // 0x46BE46 -> 0x418A10
    app->PhysicsResetPending() = 1; // 0x469B0D [0x9FCC1], without setting dirty
}

// ---------------------------------------------------------------------------
// 203 (0xCB, entry 0x7FF7CB46BAAA): File: save VPD pose.
//   Camera/accessory gate first (0x46BAB1) -> scan current model's bone-selection bitmap (model+0x3120,
//   count at +0x3110) -> enterKeyState=1 -> if no bones are selected, show 0x40000 prompt and exit; otherwise save
//   dialog (owner=main window, nFilterIndex=1, Flags=6). No flag writes follow SaveVpdFile
//   (no dirty flag).
// ---------------------------------------------------------------------------
void CmdSavePose(MMDApp* app) {
    if (app->CameraMode() != 0) { // 0x46BAAA gate comes first
        return;
    }
    // Bone-selection scan: the active model's selection bitmap (x64 app+0xBE8 slot table, index at +0x13E0);
    // any nonzero byte means a bone is selected.
    bool anySelected = false;
    unsigned char* const model = mdl::ModelBytes(app->SelectedModel());
    if (model != nullptr) {
        const int count = static_cast<int>(mdl::Mdl(model)->boneCount);
        unsigned char* const selBits = mdl::Mdl(model)->boneSelection;
        if (selBits != nullptr) {
            for (int b = 0; b < count; ++b) {
                if (selBits[b] != 0) {
                    anySelected = true;
                    break;
                }
            }
        }
    }
    app->state.enterKeyState = 1; // 0x46BAF4 [app+0xC0]
    if (!anySelected) {
        MessageBoxA(MainHwnd(app),
                    app->EnglishUI() != 0 ? "Pose data will be saved only selected bone.\n"
                                            "Please select bone."
                                          : kMsgSavePoseJp,
                    app->EnglishUI() != 0 ? "save pose data" : kCaptionSavePoseJp,
                    MB_TOPMOST); // 0x40000
        return;
    }
    SetCurrentDirectoryW(app->ExeDir());
    wchar_t path[kDialogPathLen];
    swprintf_s(path, kDialogPathLen, kFmtEmptyPath, L"", L"");
    wchar_t fileTitle[kDialogPathLen];
    OPENFILENAMEW ofn;
    memset(&ofn, 0, sizeof(ofn));
    ofn.lStructSize = sizeof(ofn);
    ofn.hwndOwner = MainHwnd(app); // Save dialog owner is always the main window
    ofn.lpstrFilter = L"Vocaloid Pose Data files(*.vpd)\0*.vpd\0";
    ofn.lpstrFile = path;
    ofn.nFilterIndex = 1;
    ofn.nMaxFile = kDialogPathLen;
    ofn.Flags = OFN_OVERWRITEPROMPT | OFN_HIDEREADONLY; // 6
    ofn.lpstrInitialDir = RememberFileDialogDirs(app) ? app->DirPose() : L"UserFile\\Pose";
    ofn.lpstrDefExt = L"vpd";
    ofn.nMaxFileTitle = kDialogPathLen;
    ofn.lpstrFileTitle = fileTitle;
    ofn.lpstrTitle = app->EnglishUI() != 0 ? L"save pose data" : kTitleSavePoseJp;
    if (!GetSaveFileNameW(&ofn)) {
        return;
    }
    if (RememberFileDialogDirs(app)) {
        wchar_t* dir = ExtractDirFromPath(app->PathWorkspace().projectDirectory, path);
        CopyDirPathW(app->DirPose(), dir);
    }
    SaveVpdFile(path); // 0x46BCAB -> 0x418750, no flag writes
}

// ---------------------------------------------------------------------------
// 204 (0xCC, entry 0x7FF7CB46DE02): File: New.
//   enterKeyState=1 -> MessageBox(main window, 0x40001):
//     EN caption "new" (0x7FF7CB54EFB0) / body "The existing state will be
//         annulled.\n\nAre you OK?"
//     JP caption "New" (0x54ED60) / body 0x54EFF0
//   Call ResetAppState only for ==IDOK; no flag writes afterward.
// ---------------------------------------------------------------------------
void CmdResetState(MMDApp* app) {
    app->state.enterKeyState = 1; // 0x46DE02 [app+0xC0]
    const int r = MessageBoxA(
        MainHwnd(app),
        app->EnglishUI() != 0 ? "The existing state will be annulled.\n\nAre you OK?" : kMsgNewJp,
        app->EnglishUI() != 0 ? "new" : kCaptionNewJp,
        MB_OKCANCEL | MB_TOPMOST); // 0x40001
    if (r == IDOK) {
        ResetAppState(app); // 0x46DE52 -> 0x44E540
    }
}

// ---------------------------------------------------------------------------
// 205 (0xCD, entry 0x7FF7CB46DE5C): File: open PMM scene.
//   enterKeyState=1 -> dirty-scene gate (if 0xA1B31 is nonzero, confirm with 0x40001, EN caption
//   "open file data"/JP 0x54F068, exit for !=IDOK) -> open dialog (owner=floating window
//   or main window, per-type directory DirUser/0xA25EC or "UserFile", defExt "pmm")
//   -> on confirmation, update directory, store full path in EnvFileName, call LoadSceneFile.
//   The dispatcher writes no flags after LoadSceneFile (no dirty flag).
// ---------------------------------------------------------------------------
void CmdOpenScene(MMDApp* app) {
    app->state.enterKeyState = 1;    // 0x46DE5C [app+0xC0]
    if (app->SceneModified() != 0) { // 0x46DE66 dirty-scene confirmation
        const int r = MessageBoxA(MainHwnd(app),
                                  app->EnglishUI() != 0
                                      ? "There is a change point not preserved.\n\nAre you OK?"
                                      : kMsgOpenDirtyJp,
                                  app->EnglishUI() != 0 ? "open file data" : kCaptionOpenFileJp,
                                  MB_OKCANCEL | MB_TOPMOST); // 0x40001
        if (r != IDOK) {
            return;
        }
    }
    SetCurrentDirectoryW(app->ExeDir());
    wchar_t path[kDialogPathLen];
    swprintf_s(path, kDialogPathLen, kFmtEmptyPath, L"", L"");
    wchar_t fileTitle[kDialogPathLen];
    OPENFILENAMEW ofn;
    memset(&ofn, 0, sizeof(ofn));
    ofn.lStructSize = sizeof(ofn);
    ofn.hwndOwner = app->FloatingWindow() != nullptr ? app->FloatingWindow() : MainHwnd(app);
    ofn.lpstrFilter = L"PolygonMovieMaker files(*.pmm)\0*.pmm\0";
    ofn.lpstrFile = path;
    ofn.nMaxFile = kDialogPathLen;
    ofn.Flags = OFN_FILEMUSTEXIST;                                     // 0x1000
    ofn.lpstrInitialDir = RememberFileDialogDirs(app) ? app->DirUser() // 0xA25EC
                                                      : L"UserFile";
    ofn.lpstrDefExt = L"pmm";
    ofn.nMaxFileTitle = kDialogPathLen;
    ofn.lpstrFileTitle = fileTitle;
    ofn.lpstrTitle = app->EnglishUI() != 0 ? L"open file data" : kTitleOpenFileJp;
    if (!GetOpenFileNameW(&ofn)) {
        return;
    }
    if (RememberFileDialogDirs(app)) {
        wchar_t* dir = ExtractDirFromPath(app->PathWorkspace().projectDirectory, path);
        CopyDirPathW(app->DirUser(), dir);
    }
    wcscpy_s(app->EnvFileName(), kDialogPathLen, path); // 0x46E01B
    LoadSceneFile();                                    // 0x46E037 -> 0x4A2C10, no flag writes
}

// ---------------------------------------------------------------------------
// 206 (0xCE, entry 0x7FF7CB46B8BC): File: open WAV.
//   enterKeyState=1 -> DirectSound availability gate (if x64 app+0x2D8 is 0,
//   MessageBox(main window, uType=0): EN "DirectSound error"/"You cannot play WAVE
//   because failed initialization of DirectSound!"; both JP caption and body are
//   "DirectSound", then exit) -> open dialog -> store full path in wavPath, then update
//   DirWave (0xA452C) -> LoadWaveFile -> set dirty. Do not request reseat.
// ---------------------------------------------------------------------------
void CmdOpenWave(MMDApp* app) {
    app->state.enterKeyState = 1;           // 0x46B8BC [app+0xC0]
    if (app->DirectSoundAvailable() == 0) { // 0x46B8C6 gate
        MessageBoxA(MainHwnd(app),
                    app->EnglishUI() != 0 ? "You cannot play WAVE because failed initialization of "
                                            "DirectSound!"
                                          : "DirectSound",
                    app->EnglishUI() != 0 ? "DirectSound error" : "DirectSound", 0);
        return;
    }
    SetCurrentDirectoryW(app->ExeDir());
    wchar_t path[kDialogPathLen];
    swprintf_s(path, kDialogPathLen, kFmtEmptyPath, L"", L"");
    wchar_t fileTitle[kDialogPathLen];
    OPENFILENAMEW ofn;
    memset(&ofn, 0, sizeof(ofn));
    ofn.lStructSize = sizeof(ofn);
    ofn.hwndOwner = app->FloatingWindow() != nullptr ? app->FloatingWindow() : MainHwnd(app);
    ofn.lpstrFilter = L"Wave files(*.wav)\0*.wav\0";
    ofn.lpstrFile = path;
    ofn.nMaxFile = kDialogPathLen;
    ofn.Flags = OFN_FILEMUSTEXIST;                                     // 0x1000
    ofn.lpstrInitialDir = RememberFileDialogDirs(app) ? app->DirWave() // 0xA452C
                                                      : L"UserFile\\Wave";
    ofn.lpstrDefExt = L"wav";
    ofn.nMaxFileTitle = kDialogPathLen;
    ofn.lpstrFileTitle = fileTitle;
    ofn.lpstrTitle = app->EnglishUI() != 0 ? L"open WAVE File" : kTitleOpenWaveJp;
    if (!GetOpenFileNameW(&ofn)) {
        return;
    }
    wcscpy_s(app->WavePath(), kDialogPathLen, path); // 0x46BA13 -> app+0xD8
    if (RememberFileDialogDirs(app)) {
        wchar_t* dir = ExtractDirFromPath(app->PathWorkspace().projectDirectory, path);
        CopyDirPathW(app->DirWave(), dir);
    }
    LoadWaveFile(app);        // 0x46BA99 -> 0x418500
    app->SceneModified() = 1; // 0x46BA9E [0xA1B31]
}

// ---------------------------------------------------------------------------
// 208 (0xD0, entry 0x7FF7CB46E05F): File: save PMM scene as (0xCF quick save also
//   falls through here when no filename exists).
//   [letterKeyState[5]]=1; enterKeyState=1 -> save dialog (owner=main window,
//   nFilterIndex=1, Flags=6, DirUser/0xA25EC or "UserFile", defExt "pmm",
//   EN caption "save file"/JP 0x54F188) -> on confirmation, update directory, store full path in
//   EnvFileName, call SaveSceneFile. The dispatcher writes no flags (does not clear dirty;
//   SaveSceneFile handles the dirty flag internally).
// ---------------------------------------------------------------------------
void CmdSaveScene(MMDApp* app) {
    app->LetterKeyState(LetterKey::A) = 1; // 0x46E05F [app+0x48]
    app->state.enterKeyState = 1;          // 0x46E066 [app+0xC0]
    SetCurrentDirectoryW(app->ExeDir());
    wchar_t path[kDialogPathLen];
    swprintf_s(path, kDialogPathLen, kFmtEmptyPath, L"", L"");
    wchar_t fileTitle[kDialogPathLen];
    OPENFILENAMEW ofn;
    memset(&ofn, 0, sizeof(ofn));
    ofn.lStructSize = sizeof(ofn);
    ofn.hwndOwner = MainHwnd(app); // Save dialog owner is always the main window
    ofn.lpstrFilter = L"PolygonMovieMaker files(*.pmm)\0*.pmm\0";
    ofn.lpstrFile = path;
    ofn.nFilterIndex = 1;
    ofn.nMaxFile = kDialogPathLen;
    ofn.Flags = OFN_OVERWRITEPROMPT | OFN_HIDEREADONLY; // 6
    ofn.lpstrInitialDir = RememberFileDialogDirs(app) ? app->DirUser() : L"UserFile";
    ofn.lpstrDefExt = L"pmm";
    ofn.nMaxFileTitle = kDialogPathLen;
    ofn.lpstrFileTitle = fileTitle;
    ofn.lpstrTitle = app->EnglishUI() != 0 ? L"save file" : kTitleSaveFileJp;
    if (!GetSaveFileNameW(&ofn)) {
        return;
    }
    if (RememberFileDialogDirs(app)) {
        wchar_t* dir = ExtractDirFromPath(app->PathWorkspace().projectDirectory, path);
        CopyDirPathW(app->DirUser(), dir);
    }
    wcscpy_s(app->EnvFileName(), kDialogPathLen, path); // 0x46E1CE
    SaveSceneFile(app);                                 // 0x46E1EA -> 0x4950A0, no flag writes
}

// ---------------------------------------------------------------------------
// 209 (0xD1, entry 0x7FF7CB46C11E): File: load VMD motion.
//   enterKeyState=1 (no mode gate) -> open dialog (owner=floating window or main window,
//   DirMotion/0xA358C or "UserFile\Motion", defExt "vmd") -> load.
//   The tail first sets dirty (0x46C2A0 [0xA1B31]), then requests reseat (0x469B0D [0x9FCC1]).
// ---------------------------------------------------------------------------
void CmdLoadMotion(MMDApp* app) {
    app->state.enterKeyState = 1; // 0x46C11E [app+0xC0]
    SetCurrentDirectoryW(app->ExeDir());
    wchar_t path[kDialogPathLen];
    swprintf_s(path, kDialogPathLen, kFmtEmptyPath, L"", L"");
    wchar_t fileTitle[kDialogPathLen];
    OPENFILENAMEW ofn;
    memset(&ofn, 0, sizeof(ofn));
    ofn.lStructSize = sizeof(ofn);
    ofn.hwndOwner = app->FloatingWindow() != nullptr ? app->FloatingWindow() : MainHwnd(app);
    ofn.lpstrFilter = L"Vocaloid Motion Data files(*.vmd)\0*.vmd\0";
    ofn.lpstrFile = path;
    ofn.nMaxFile = kDialogPathLen;
    ofn.Flags = OFN_FILEMUSTEXIST;                                       // 0x1000
    ofn.lpstrInitialDir = RememberFileDialogDirs(app) ? app->DirMotion() // 0xA358C
                                                      : L"UserFile\\Motion";
    ofn.lpstrDefExt = L"vmd";
    ofn.nMaxFileTitle = kDialogPathLen;
    ofn.lpstrFileTitle = fileTitle;
    ofn.lpstrTitle = app->EnglishUI() != 0 ? L"load motion data" : kTitleLoadMotionJp;
    if (!GetOpenFileNameW(&ofn)) {
        return;
    }
    if (RememberFileDialogDirs(app)) {
        wchar_t* dir = ExtractDirFromPath(app->PathWorkspace().projectDirectory, path);
        CopyDirPathW(app->DirMotion(), dir);
    }
    LoadVmdFile(path);              // 0x46C29B -> 0x434B60
    app->SceneModified() = 1;       // 0x46C2A0 [0xA1B31]
    app->PhysicsResetPending() = 1; // 0x469B0D [0x9FCC1]
}

// ---------------------------------------------------------------------------
// 210 (0xD2, entry 0x7FF7CB46BE50): File: save VMD motion.
//   [letterKeyState[17]]=1; enterKeyState=1 -> selected-frame-point gate:
//     * Camera/accessory mode (optflag[0] nonzero): scan three app tables: camera (+0x48/0x54 stride, 10000),
//       lighting (+0x24/0x28, 10000) and self-shadow (+0x14/0x18, 10000);
//       a self-shadow hit enters the dialog directly;
//     * Model mode: scan three active-model tables: bones (+0x38/0x3C stride), morphs (+0x10/0x14, limit
//       20000) and display frames (+0x18/0x28, limit 1000).
//   With no selected frame points, show 0x40000 prompt and exit; otherwise open the save dialog (owner=main window,
//   nFilterIndex=1, Flags=6, DirMotion or "UserFile\Motion", defExt
//   "vmd"). No flag writes follow SaveVmdFile (no dirty flag).
// ---------------------------------------------------------------------------
void CmdSaveMotion(MMDApp* app) {
    app->LetterKeyState(LetterKey::L) = 1; // 0x46BE50 [app+0x78]
    app->state.enterKeyState = 1;          // 0x46BE57 [app+0xC0]
    // Selected-frame-point scan (0x46BE61..0x46BF64).
    auto anySelectedByte = [](unsigned char* table, std::size_t off, std::size_t stride,
                              int cap) -> bool {
        if (table == nullptr) {
            return false;
        }
        for (int i = 0; i < cap; ++i) {
            if (table[off + stride * i] != 0) {
                return true;
            }
        }
        return false;
    };
    auto anyAllocatedKey = [](const auto* keys, std::size_t count) {
        return AnyAllocatedKey(keys, count);
    };
    bool any;
    if (app->CameraMode() != 0) {
        // Camera/accessory mode: scan only the three app tables (from 0x46BE76), not model tables.
        any = anySelectedByte(reinterpret_cast<unsigned char*>(app->state.cameraKeyTrack), 0x48,
                              0x54, 10000) ||
              anySelectedByte(reinterpret_cast<unsigned char*>(app->state.lightKeyTrack), 0x24,
                              0x28, 10000) ||
              anySelectedByte(reinterpret_cast<unsigned char*>(app->state.selfShadowKeyTrack), 0x14,
                              0x18, 10000);
    } else {
        // Model mode: three model frame tables for bones/morphs/display (x64 model +0x2790/+0x2798/+0x27A0).
        any = false;
        unsigned char* const model = mdl::ModelBytes(app->SelectedModel());
        if (model != nullptr) {
            any = anyAllocatedKey(mdl::BoneKeys(model), mdl::kBoneKeyCapacity) ||
                  anyAllocatedKey(mdl::MorphKeys(model), mdl::kMorphKeyCapacity) ||
                  anyAllocatedKey(mdl::DisplayKeys(model), mdl::kDisplayKeyCapacity);
        }
    }
    if (!any) {
        // 0x46BF66: set enterKeyState again before the prompt (already set on entry; idempotent).
        MessageBoxA(MainHwnd(app),
                    app->EnglishUI() != 0 ? "Motion data will be saved only selected frame point.\n"
                                            "Please select frame point."
                                          : kMsgSaveMotionJp,
                    app->EnglishUI() != 0 ? "save motion data" : kCaptionSaveMotionJp,
                    MB_TOPMOST); // 0x40000
        return;
    }
    SetCurrentDirectoryW(app->ExeDir());
    wchar_t path[kDialogPathLen];
    swprintf_s(path, kDialogPathLen, kFmtEmptyPath, L"", L"");
    wchar_t fileTitle[kDialogPathLen];
    OPENFILENAMEW ofn;
    memset(&ofn, 0, sizeof(ofn));
    ofn.lStructSize = sizeof(ofn);
    ofn.hwndOwner = MainHwnd(app); // Save dialog owner is always the main window
    ofn.lpstrFilter = L"Vocaloid Motion Data files(*.vmd)\0*.vmd\0";
    ofn.lpstrFile = path;
    ofn.nFilterIndex = 1;
    ofn.nMaxFile = kDialogPathLen;
    ofn.Flags = OFN_OVERWRITEPROMPT | OFN_HIDEREADONLY; // 6
    ofn.lpstrInitialDir = RememberFileDialogDirs(app) ? app->DirMotion() : L"UserFile\\Motion";
    ofn.lpstrDefExt = L"vmd";
    ofn.nMaxFileTitle = kDialogPathLen;
    ofn.lpstrFileTitle = fileTitle;
    ofn.lpstrTitle = app->EnglishUI() != 0 ? L"save motion data" : kTitleSaveMotionJp;
    if (!GetSaveFileNameW(&ofn)) {
        return;
    }
    if (RememberFileDialogDirs(app)) {
        wchar_t* dir = ExtractDirFromPath(app->PathWorkspace().projectDirectory, path);
        CopyDirPathW(app->DirMotion(), dir);
    }
    SaveVmdFile(path); // 0x46C114 -> 0x419370, no flag writes
}

// ---------------------------------------------------------------------------
// 213 (0xD5, entry 0x7FF7CB46B1BF): File: load AVI background.
//   [letterKeyState[17]]=1; enterKeyState=1 (no mode gate) -> open dialog (owner=
//   floating window or main window, DirBg/0xA4CFC or "UserFile\BackGround", defExt "avi",
//   EN caption "load AVI file"/JP L"AVI") -> update directory, store full path in
//   aviBackgroundPath (0x9F0B8) -> LoadAviFile -> set dirty. Do not request reseat.
// ---------------------------------------------------------------------------
void CmdLoadAvi(MMDApp* app) {
    app->LetterKeyState(LetterKey::L) = 1; // 0x46B1BF [app+0x78]
    app->state.enterKeyState = 1;          // 0x46B1C6 [app+0xC0]
    SetCurrentDirectoryW(app->ExeDir());
    wchar_t path[kDialogPathLen];
    swprintf_s(path, kDialogPathLen, kFmtEmptyPath, L"", L"");
    wchar_t fileTitle[kDialogPathLen];
    OPENFILENAMEW ofn;
    memset(&ofn, 0, sizeof(ofn));
    ofn.lStructSize = sizeof(ofn);
    ofn.hwndOwner = app->FloatingWindow() != nullptr ? app->FloatingWindow() : MainHwnd(app);
    ofn.lpstrFilter = L"avi files(*.avi)\0*.avi\0";
    ofn.lpstrFile = path;
    ofn.nMaxFile = kDialogPathLen;
    ofn.Flags = OFN_FILEMUSTEXIST;                                   // 0x1000
    ofn.lpstrInitialDir = RememberFileDialogDirs(app) ? app->DirBg() // 0xA4CFC
                                                      : L"UserFile\\BackGround";
    ofn.lpstrDefExt = L"avi";
    ofn.nMaxFileTitle = kDialogPathLen;
    ofn.lpstrFileTitle = fileTitle;
    ofn.lpstrTitle = app->EnglishUI() != 0 ? L"load AVI file" : kTitleLoadAviJp;
    if (!GetOpenFileNameW(&ofn)) {
        return;
    }
    if (RememberFileDialogDirs(app)) {
        wchar_t* dir = ExtractDirFromPath(app->PathWorkspace().projectDirectory, path);
        CopyDirPathW(app->DirBg(), dir);
    }
    wcscpy_s(app->AviBackgroundPath(), kDialogPathLen, path); // 0x46B339
    LoadAviFile(app);                                         // 0x46B355 -> 0x433250
    app->SceneModified() = 1;                                 // 0x46B35A [0xA1B31]
}

} // namespace mikudancestudio
