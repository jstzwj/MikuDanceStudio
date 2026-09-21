// ===========================================================================
// VA 0x00461300 - HandleDropFiles  (original: sub_461300)
// ===========================================================================
// WM_DROPFILES handler (original __thiscall on `this` = g_Block).  Counts
// dropped files with DragQueryFileA(hDrop, -1, 0, 0), then loops
// DragQueryFileW(hDrop, i, szFile, 0x200) and dispatches on the extension
// via wcsstr with the original case-variant needles, in this order:
//
//   .pmm (.pmm/.PMM/.Pmm)  project file:
//       dirty byte@658189 != 0 -> MB_OKCANCEL confirm.  EN text
//       "There is a change point not preserved.\n\nAre you OK?" /
//       "open file data"; JP uses the byte constants 0x52E568 (text) /
//       0x52E5AC (caption).  Cancel returns immediately - WITHOUT
//       DragFinish (original quirk at 0x4613F9).
//       Menu 0x12D checked (MF_CHECKED) -> ExtractDirFromPath (0x408960,
//       writes the dir into app+652088+512w, the +512 slot the original
//       reaches from sub_408960's internal offset) copied into
//       DirUser (0xA1540, wcscpy_s 0x3E8); path -> EnvFile (0xA0900,
//       wcscpy_s 0x100); LoadSceneFile (0x458F80).
//   .pmd/.pmx (.pmd/.PMD/.Pmd | .pmx/.PMX/.Pmx)  model:
//       menu checked -> dir into DirModel (0xA0D70, 0x3E8);
//       LoadModelFile (0x460430).  No dirty set.
//   .vpd (.vpd/.VPD/.Vpd)  pose data:
//       byte@760 != 0 (no model) -> MB_OK message, EN "Not selected
//       model!"/"open pose data" or JP 0x52E49C/0x52E4CC; else menu ->
//       DirPose (0xA2CB0, 0x3E8) + LoadVpdFile (0x418A10).  No dirty set.
//   .vmd (.vmd/.VMD/.Vmd)  motion:
//       menu -> DirMotion (0xA24E0, 0x3E8); LoadVmdFile (0x434B60);
//       dirty = 1.
//   .x (.x/.X) / .vac (.vac/.VAC/Vac - the third needle has no leading
//       dot, original quirk):
//       menu -> DirAccs (0xA1D10, 0x3E8); LoadAccessoryFile (0x460B30,
//       shared original LABEL_31).  No dirty set.
//   .avi (.avi/.AVI/.Avi)  video:
//       menu -> DirBg (0xA3C50, 0x3E8); path -> app+647660 (0x9E1EC,
//       wcscpy_s 0x100); LoadAviFile (0x433250); dirty = 1.
//   .bmp/.jpg (.bmp/.BMP/.Bmp / .jpg/.JPG/.Jpg)  backdrop image:
//       bmp: menu -> DirBg; then both: path -> app+648264 (0x9E448,
//       wcscpy_s 0x100); LoadBackgroundPicture (0x4337A0); dirty = 1.
//   .wav (.wav/.WAV/.Wav)  wave audio:
//       path -> app+208 (0xD0, wcscpy_s 0x100); menu -> DirWave
//       (0xA3480, 0x3E8); LoadWaveFile (0x418500); dirty = 1.
//   .vsq (.vsq/.VSQ/.Vsq)  VOCALOID sequence (0x435FE0, NOT 0x4337A0):
//       byte@760 != 0 -> MB_OK message, EN "Not selected model!"/"open
//       vsq data" or JP 0x52E374/"vsq"; else LoadVsqFile + dirty = 1.
//   .oni (.oni/.ONI/.Oni)  OpenNI/Kinect data:
//       byte@760 != 0 -> MB_OK message, EN "Please select model!"/"open
//       oni data" or JP 0x52E2F4/"oni"; else: byte@656312 (0xA03B8) != 0
//       -> DisableKinect (0x42A020); wide->Shift-JIS of the path into a
//       char[256] (0x407910, locale table at 0xA06C4); OpenNiInit
//       (0x429CB0).  No dirty set.
//
// Tail: DragFinish(hDrop) on the normal completion path only.
//
// Reference: ../translated/MikuMikuDance/fcn_00461300.cpp - superseded by
// the live IDB where they disagree (the translated file's branch table,
// MessageBox uType values 0x01 and the .vsq/.x handler mapping are wrong;
// the disassembly at 0x461300 is authoritative).
// ===========================================================================
#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <Windows.h>
#include <shellapi.h>

#include <cstdint>
#include <cstdlib>
#include <cstring>
#include <cwchar>
#include <new>

#include "mikudancestudio/mmd_app.hpp"
#include "mikudancestudio/ported_funcs.hpp"

namespace mikudancestudio {

// ---- pending loaders called below ------------------------------------------
// 0x435FE0 (VSQ load), 0x42A020 (DisableKinect), 0x429CB0 (OpenNiInit) and
// 0x4337A0 (LoadBackgroundPicture) are declared in ported_funcs.hpp.

namespace {

// JP-locale MessageBox byte strings - verbatim Shift-JIS from .rdata.
// 0x52E568 "保存していない変更点があります\n\nこのままロードしてよろしいですか？"
const char kJpDirtyText[] =
    "\x95\xdb\x91\xb6\x82\xb5\x82\xc4\x82\xa2\x82\xc8\x82\xa2"
    "\x95\xcf\x8d\x58\x93\x5f\x82\xaa\x82\xa0\x82\xe8\x82\xdc\x82\xb7"
    "\x0a\x0a"
    "\x82\xb1\x82\xcc\x82\xdc\x82\xdc\x83\x8d\x81\x5b\x83\x68"
    "\x82\xb5\x82\xc4\x82\xe6\x82\xeb\x82\xb5\x82\xa2\x82\xc5\x82\xb7\x82\xa9\x81\x48";
// 0x52E5AC "ファイルを開く"
const char kJpDirtyCaption[] =
    "\x83\x74\x83\x40\x83\x43\x83\x8b\x82\xf0\x8a\x4a\x82\xad";
// 0x52E49C "ボーンを選択してからでは読みません"
const char kJpPoseText[] =
    "\x83\x4a\x83\x81\x83\x89\x81\x45\x8f\xc6\x96\xbe"
    "\x83\x82\x81\x5b\x83\x68\x82\xc5\x82\xcd"
    "\x93\xc7\x82\xdf\x82\xdc\x82\xb9\x82\xf1";
// 0x52E4CC "ポーズ読込"
const char kJpPoseCaption[] = "\x83\x7c\x81\x5b\x83\x59\x93\xc7\x8d\x9e";
// 0x52E374 "ボーンを選択してからではvsqポーズでは読みません" (ASCII "vsq" embedded)
const char kJpVsqText[] =
    "\x83\x4a\x83\x81\x83\x89\x81\x45\x8f\xc6\x96\xbe"
    "\x83\x82\x81\x5b\x83\x68\x82\xc5\x82\xcd" "vsq"
    "\x83\x66\x81\x5b\x83\x5e\x82\xcd"
    "\x93\xc7\x82\xdf\x82\xdc\x82\xb9\x82\xf1";
// 0x52E2F4 "ボーンを選択してからではoniポーズでは読みません" (ASCII "oni" embedded)
const char kJpOniText[] =
    "\x83\x4a\x83\x81\x83\x89\x81\x45\x8f\xc6\x96\xbe"
    "\x83\x82\x81\x5b\x83\x68\x82\xc5\x82\xcd" "oni"
    "\x83\x66\x81\x5b\x83\x5e\x82\xcd"
    "\x93\xc7\x82\xdf\x82\xdc\x82\xb9\x82\xf1";

HWND MainHwnd(MMDApp* app) {
    return static_cast<HWND>(app->Hwnd());
}

// Menu item 0x12D checked state - the "remember current directory" gate.
// Original: GetMenu(hwnd) -> GetMenuState(menu, 0x12D, 0) & 8.
bool DirMenuChecked(MMDApp* app) {
    return (GetMenuState(GetMenu(MainHwnd(app)), 0x12D, 0) & MF_CHECKED) != 0;
}

// VA 0x00408960 - the port writes at `destination` directly, while the
// original sub_408960 writes at `this + 512` wchars and returns that
// pointer.  Passing app+652088+512 reproduces the exact in-object write
// (the +512 slot at byte 653112, 0x9F738).
wchar_t* DirOf(MMDApp* app, const wchar_t* szFile) {
    return ExtractDirFromPath(app->PathWorkspace().executableDirectory,
                              szFile);
}

// VA 0x00407910 - wide -> Shift-JIS (same algorithm as pmx_load.cpp
// WideToSjis).  The original prefills the destination with a global
// "Locale" string first (0x407927) and then always overwrites it when the
// wide source is non-empty - the prefill is dead here because a dropped
// path is never empty, so it is not replicated.
void WideToSjisPath(char* dst, const wchar_t* src, rsize_t size) {
    dst[0] = '\0';
    if (src == nullptr || src[0] == L'\0')
        return;
    const int need = WideCharToMultiByte(932, 0, src, -1, nullptr, 0,
                                         nullptr, nullptr);
    char* tmp = static_cast<char*>(operator new(need));
    BOOL usedDefault = FALSE;
    const int n = WideCharToMultiByte(932, 0, src, -1, tmp, need, nullptr,
                                      &usedDefault);
    if (n == 0 || usedDefault) {
        size_t dummy = 0;
        if (wcstombs_s(&dummy, dst, size, src, _TRUNCATE) != 0)
            dst[0] = '\0';
    } else {
        strncpy_s(dst, size, tmp, _TRUNCATE);
    }
    operator delete(tmp);
}

}  // namespace

void HandleDropFiles(HDROP hDrop) {
    MMDApp* app = g_Block;

    const int fileCount = DragQueryFileA(hDrop, 0xFFFFFFFF, nullptr, 0);
    for (int iFile = 0; iFile < fileCount; ++iFile) {
        // 0x461360: the original passes cch = 0x200 for a 256-wchar buffer
        // (harmless for real paths); replicated verbatim.
        wchar_t szFile[256];
        DragQueryFileW(hDrop, iFile, szFile, static_cast<UINT>(sizeof(szFile) / sizeof(szFile[0])));

        // ---- .pmm - project file -----------------------------------------
        if (wcsstr(szFile, L".pmm") || wcsstr(szFile, L".PMM") ||
            wcsstr(szFile, L".Pmm")) {
            if (app->SceneModified() != 0) {
                int r;
                if (app->EnglishUI()) {
                    r = MessageBoxA(MainHwnd(app),
                                    "There is a change point not preserved.\n\nAre you OK?",
                                    "open file data", MB_OKCANCEL);
                } else {
                    r = MessageBoxA(MainHwnd(app), kJpDirtyText,
                                    kJpDirtyCaption, MB_OKCANCEL);
                }
                if (r != IDOK)
                    return;  // 0x4613F9: original returns, DragFinish skipped
            }
            if (DirMenuChecked(app)) {  // 0x46140D
                wcscpy_s(app->DirUser(), 0x3E8, DirOf(app, szFile));  // 0xA1540
            }
            wcscpy_s(app->EnvFileName(), 0x100, szFile);              // 0xA0900
            LoadSceneFile();                                          // 0x458F80
            continue;
        }

        // ---- .pmd / .pmx - model -----------------------------------------
        if ((wcsstr(szFile, L".pmd") || wcsstr(szFile, L".PMD") ||
             wcsstr(szFile, L".Pmd")) ||
            (wcsstr(szFile, L".pmx") || wcsstr(szFile, L".PMX") ||
             wcsstr(szFile, L".Pmx"))) {
            if (DirMenuChecked(app)) {
                wcscpy_s(app->DirModel(), 0x3E8, DirOf(app, szFile));  // 0xA0D70
            }
            LoadModelFile(app, szFile);                               // 0x460430
            continue;
        }

        // ---- .vpd - pose data ---------------------------------------------
        if (wcsstr(szFile, L".vpd") || wcsstr(szFile, L".VPD") ||
            wcsstr(szFile, L".Vpd")) {
            if (app->state.optflag[0] != 0) {  // 760
                if (app->EnglishUI())
                    MessageBoxA(MainHwnd(app), "Not selected model!",
                                "open pose data", MB_OK);
                else
                    MessageBoxA(MainHwnd(app), kJpPoseText, kJpPoseCaption,
                                MB_OK);
            } else {
                if (DirMenuChecked(app)) {
                    wcscpy_s(app->DirPose(), 0x3E8, DirOf(app, szFile));  // 0xA2CB0
                }
                LoadVpdFile(szFile);                                  // 0x418A10
            }
            continue;
        }

        // ---- .vmd - motion data -------------------------------------------
        if (wcsstr(szFile, L".vmd") || wcsstr(szFile, L".VMD") ||
            wcsstr(szFile, L".Vmd")) {
            if (DirMenuChecked(app)) {
                wcscpy_s(app->DirMotion(), 0x3E8, DirOf(app, szFile));  // 0xA24E0
            }
            LoadVmdFile(szFile);                                      // 0x434B60
            app->SceneModified() = 1;
            continue;
        }

        // ---- .x - DirectX accessory ---------------------------------------
        if (wcsstr(szFile, L".x") || wcsstr(szFile, L".X")) {
            if (DirMenuChecked(app)) {
                wcscpy_s(app->DirAccs(), 0x3E8, DirOf(app, szFile));   // 0xA1D10
            }
            LoadAccessoryFile(szFile);  // 0x460B30 (original LABEL_31)
            continue;
        }

        // ---- .vac - the third needle is L"Vac" (no dot), original quirk ---
        if (wcsstr(szFile, L".vac") || wcsstr(szFile, L".VAC") ||
            wcsstr(szFile, L"Vac")) {
            if (DirMenuChecked(app)) {
                wcscpy_s(app->DirAccs(), 0x3E8, DirOf(app, szFile));
            }
            LoadAccessoryFile(szFile);  // 0x460B30 (original LABEL_31)
            continue;
        }

        // ---- .avi - video ---------------------------------------------------
        if (wcsstr(szFile, L".avi") || wcsstr(szFile, L".AVI") ||
            wcsstr(szFile, L".Avi")) {
            if (DirMenuChecked(app)) {
                wcscpy_s(app->DirBg(), 0x3E8, DirOf(app, szFile));     // 0xA3C50
            }
            wcscpy_s(app->state.aviBackgroundPath, 0x100, szFile);              // 0x9E1EC
            // original: sub_433250(this) reads the path from app+647660;
            // the stub interface takes the path - pass the same buffer
            LoadAviFile(app);                                        // 0x433250
            app->SceneModified() = 1;
            continue;
        }

        // ---- .bmp / .jpg - backdrop image ----------------------------------
        if (wcsstr(szFile, L".bmp") || wcsstr(szFile, L".BMP") ||
            wcsstr(szFile, L".Bmp")) {
            if (DirMenuChecked(app)) {
                wcscpy_s(app->DirBg(), 0x3E8, DirOf(app, szFile));
            }
        } else {
            if (!(wcsstr(szFile, L".jpg") || wcsstr(szFile, L".JPG") ||
                  wcsstr(szFile, L".Jpg"))) {
                // ---- .wav - wave audio ----
                if (wcsstr(szFile, L".wav") || wcsstr(szFile, L".WAV") ||
                    wcsstr(szFile, L".Wav")) {
                    wcscpy_s(app->state.wavPath, 0x100, szFile);  // 0xD0
                    if (DirMenuChecked(app)) {
                        wcscpy_s(app->DirWave(), 0x3E8, DirOf(app, szFile));  // 0xA3480
                    }
                    // original: sub_418500(this) reads the path from app+208;
                    // the stub interface takes the path - pass the same buffer
                    LoadWaveFile(app);                                // 0x418500
                    app->SceneModified() = 1;
                } else {
                    // ---- .vsq - VOCALOID sequence ----
                    if (wcsstr(szFile, L".vsq") || wcsstr(szFile, L".VSQ") ||
                        wcsstr(szFile, L".Vsq")) {
                        if (app->state.optflag[0] != 0) {
                            if (app->EnglishUI())
                                MessageBoxA(MainHwnd(app), "Not selected model!",
                                            "open vsq data", MB_OK);
                            else
                                MessageBoxA(MainHwnd(app), kJpVsqText, "vsq",
                                            MB_OK);
                        } else {
                            LoadVsqFile(app, szFile);                   // 0x435FE0
                            app->SceneModified() = 1;
                        }
                    } else {
                        // ---- .oni - OpenNI/Kinect data ----
                        if (wcsstr(szFile, L".oni") || wcsstr(szFile, L".ONI") ||
                            wcsstr(szFile, L".Oni")) {
                            if (app->state.optflag[0] != 0) {
                                if (app->EnglishUI())
                                    MessageBoxA(MainHwnd(app), "Please select model!",
                                                "open oni data", MB_OK);
                                else
                                    MessageBoxA(MainHwnd(app), kJpOniText, "oni",
                                                MB_OK);
                            } else {
                                if (app->state.depthDeviceEnabled != 0)
                                    DisableKinect(app);                // 0x42A020
                                char sjisPath[256];
                                WideToSjisPath(sjisPath, szFile, 256);  // 0x407910
                                OpenNiInit(app, sjisPath);             // 0x429CB0
                            }
                        }
                    }
                }
                continue;  // 0x461A65 (LABEL_64)
            }
        }
        // .bmp or .jpg matched: backdrop tail shared at 0x46199F
        wcscpy_s(app->state.pictureBackgroundPath, 0x100, szFile);    // 0x9E448
        LoadBackgroundPicture(app);                                   // 0x4337A0
        app->SceneModified() = 1;
    }
    DragFinish(hDrop);  // 0x461BF7 (not reached by the cancel path above)
}

}  // namespace mikudancestudio
