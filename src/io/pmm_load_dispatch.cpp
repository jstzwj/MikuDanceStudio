// ===========================================================================
// VA 0x00408870 - PathToProjectDir  (original: sub_408870, 0xEF bytes)
// VA 0x00458F80 - LoadSceneFile    (original: sub_458F80, 0x589B bytes,
//                                    PHASE A: shell + version dispatch)
// ===========================================================================
// sub_408870 (thiscall, ecx = Destination wchar_t[0x100]): copies the scene
// path to a local, then truncates at the first L"UserFile" occurrence; if
// the result is empty or does not end in L'\\' the destination is cleared.
// Called from the PMM load shell (dst app+0x9F338, src app+0xA0900) and
// from FontSubInit 0x408E70.
//
// sub_458F80 shell (PHASE A - this file):
//   0x458FE6  _wsopen_s(&fd, app+0xA0900, 0x8000, 0x40, 0x80); on error
//             sprintf_s + MessageBoxA (JP 0x52CDD4/0x52DB80, EN
//             "Cannot open file:%d"/"open file").
//   0x459055  app+0xA442C = 0; PathToProjectDir(app+0x9F338, path).
//   0x459070  _read(fd, hdr, 0x1E); strstr(hdr, "Polygon Movie maker")
//             (0x45908A: jnz continue - Ghidra prints the polarity
//             backwards); on miss MessageBoxA "This isn't the data for
//             Polygon Movie Maker!" / JP 0x52DF78 + _close + return.
//   0x4590D3  version field at strstr match + 0x14: "0001" -> inline v1 loader body
//             (0x45916D..0x45E7F7, fully ported as LoadSceneV1 in
//             pmm_load_v1.cpp; that body owns the fd and the _close on
//             every path.  Verified 2026-08-28: all three stock v1 scenes
//             load with bit-identical kinematic pose blocks (K hashes on
//             every slot), and v1-load -> save-as products are equivalent
//             through the orig round-trip (only orig's own stack-residue
//             nondeterminism remains, same class as the v2 verdict);
//             "0002" -> sub_450000(app, fd) which owns the rest of the
//             stream AND the _close + success tail (its epilogue at
//             0x45E7F7 only restores SEH state); anything else ->
//             "Cannot read this version of pmm!" box + _close.
// ===========================================================================
#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <Windows.h>

#include <cstdint>
#include <cstdio>
#include <cstring>
#include <io.h>
#include <cstdlib>

#include "mikudancestudio/mmd_app.hpp"
#include "mikudancestudio/ported_funcs.hpp"

namespace mikudancestudio {

namespace {

// Shift-JIS texts from the original .rdata (embedded byte-exact).
const char kJpOpenFailFmt[] =         // 0x52CDD4
    "\x83\x74\x83\x40\x83\x43\x83\x8b\x82\xaa\x93\xc7\x82\xdd\x8d\x9e"
    "\x82\xdf\x82\xdc\x82\xb9\x82\xf1:%d";
const char kJpOpenCaption[] =         // 0x52DB80
    "\x83\x74\x83\x40\x83\x43\x83\x8b\x93\xc7\x8d\x9e";
const char kJpNotPmmText[] =          // 0x52DF78
    "\x82\xb1\x82\xcc\x83\x74\x83\x40\x83\x43\x83\x8b\x82\xcd"
    "Polygon Movie Maker"
    "\x97\x70\x83\x74\x83\x40\x83\x43\x83\x8b\x82\xc5\x82\xcd\x82\xa0"
    "\x82\xe8\x82\xdc\x82\xb9\x82\xf1";
const char kJpBadVersionText[] =      // 0x52DF1C
    "\x82\xb1\x82\xcc\x83\x6f\x81\x5b\x83\x57\x83\x87\x83\x93\x82\xcc"
    "\x70\x6d\x6d\x83\x74\x83\x40\x83\x43\x83\x8b\x82\xcd\x93\xc7\x82\xdf"
    "\x82\xdc\x82\xb9\x82\xf1";

}  // namespace

// VA 0x00408870 - wide path -> project directory (truncate at "UserFile").
void PathToProjectDir(wchar_t* destination, const wchar_t* source) {
    wchar_t local[0x100];
    wcscpy_s(local, 0x100, source);
    if (local[0] == L'\0') {
        destination[0] = L'\0';
        return;
    }
    wchar_t* hit = wcsstr(local, L"UserFile");
    if (hit != nullptr) {
        *hit = L'\0';
        wcscpy_s(destination, 0x100, local);
        return;
    }
    const size_t len = wcslen(local);
    if (local[len - 1] == L'\\') {
        wcscpy_s(destination, 0x100, local);
        return;
    }
    destination[0] = L'\0';
}

// VA 0x00408E70.  The original `this` is the inline path context at
// app+0x9F338, not the MMDApp base.  Keep the public port signature while
// restoring the two 256-wchar directory fields exactly:
//   +0   project/UserFile base, +512 executable directory.
void FontSubInit(MMDApp* app, const wchar_t* directory) {
    if (app == nullptr || directory == nullptr)
        return;
    PathResolutionWorkspace& paths = app->PathWorkspace();
    wcscpy_s(paths.executableDirectory, 0x100, directory);
    PathToProjectDir(paths.projectDirectory, paths.executableDirectory);
}

// VA 0x00458F80 - PMM load shell (phase A).
void LoadSceneFile() {

    MMDApp* app = g_Block;
    char box[0x100];

    app->SceneModified() = 0;                                  // 0x458FE6
    int fd = -1;
    const errno_t err =
        _wsopen_s(&fd, app->EnvFileName(), 0x8000, 0x40, 0x80);
    if (err != 0) {
        if (app->EnglishUI() == 0)
            sprintf_s(box, 0x100, kJpOpenFailFmt, err);
        else
            sprintf_s(box, 0x100, "Cannot open file:%d", err);
        MessageBoxA(reinterpret_cast<HWND>(app->Hwnd()), box,
                    app->EnglishUI() == 0 ? kJpOpenCaption : "open file", 0);
        return;
    }

    app->state.windowLayoutReady = 0;              // 0x459055
    PathToProjectDir(app->PathWorkspace().projectDirectory,     // 0x45905C
                      app->EnvFileName());

    char hdr[0x1E];
    _read(fd, hdr, 0x1E);                                       // 0x459070
    // x64 @0x7FF7CB4A2D1E: the signature is located via strstr, and the
    // version field sits at the match point + 0x14 (0x7FF7CB4A2D91/DA5
    // `lea rsi,[rax+14h]`), not at the buffer head - tolerant of any
    // leading garbage before "Polygon Movie maker".
    char* const signature = strstr(hdr, "Polygon Movie maker");
    if (signature == nullptr) {
        MessageBoxA(reinterpret_cast<HWND>(app->Hwnd()),
                    app->EnglishUI() != 0
                        ? "This isn't the data for Polygon Movie Maker!"
                        : kJpNotPmmText,
                    app->EnglishUI() == 0 ? kJpOpenCaption : "open file", 0);
        _close(fd);                                             // 0x4590C1
        return;
    }

    // version field at signature+0x14 (5-byte compare incl. NUL as the
    // original's `repe cmpsb ecx,5`).
    if (strncmp(signature + 0x14, "0001", 5) == 0) {
        // The inline v1 loader body (0x45916D..0x45E7F7) owns the fd and
        // closes it on every path itself (pmm_load_v1.cpp).
        LoadSceneV1(app, fd);
        return;
    }
    if (strncmp(signature + 0x14, "0002", 5) == 0) {
        LoadSceneV2(app, fd);                                     // 0x459106
        return;                                                 // 0x45E7F7
    }
    MessageBoxA(reinterpret_cast<HWND>(app->Hwnd()),
                app->EnglishUI() != 0
                    ? "Cannot read this version of pmm!"
                    : kJpBadVersionText,
                app->EnglishUI() == 0 ? kJpOpenCaption : "open file", 0);
    _close(fd);                                                 // 0x459149
}

}  // namespace mikudancestudio
