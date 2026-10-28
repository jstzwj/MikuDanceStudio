// ===========================================================================
// VA 0x0047A5B0 - InitMainWindowAndD3D  (original: sub_47A5B0, 0x1AE4 bytes)
// ===========================================================================
// __thiscall(MMDApp* this, HINSTANCE hInstance, int nShowCmd) -> bool
//
// Phases (address-annotated against the original):
//   1. 0x47A5F2..0x47A771  allocate + init 5 subsystems:
//        0x1D574 obj -> this+657092 (locale/font; slots +120036.. store
//                       screen size, +120004.. hold _locale_t handles)
//        0x25C   obj -> this+204
//        0x6C    obj -> this+657088
//        0x4B0   obj -> this+650656
//        0x48    obj -> this+650672
//   2. 0x47A785..0x47A8A9  exe dir, japanese locale, UserFile dir defaults
//   3. 0x47A8B3..0x47B3C8  UI colour defaults + Data\color.txt override
//   4. 0x47B3EA..0x47B590  11 gradient brushes via ColorLerp (0x40AD00)
//   5. 0x47B59A..0x47BB90  window metrics + Data\mmconfig.ini load
//   6. 0x47BB90..0x47BD53  3 window classes + CreateWindowExA
//   7. 0x47BD61..0x47C068  D3D init (0x441AD0), startup-file dispatch,
//                          SetWindowPlacement, menu checks, InvalidateRect
// =========================================================================//
#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include "mikudancestudio/menu_commands.h"
#include <Windows.h>
#include <commctrl.h>
#include <stdio.h>
#include <wchar.h>

#include <cstdint>
#include <cstring>
#include <new>

#include "mikudancestudio/accessory_layout.hpp"
#include "mikudancestudio/mmd_app.hpp"
#include "mikudancestudio/ported_funcs.hpp"
#include "mikudancestudio/panel_controls.hpp"

namespace mikudancestudio {
namespace {

// color.txt order after the leading text RGB row. The file groups colors by
// UI section, so it is deliberately not the same as their storage order.
const int kThemeFileOrder[] = {
    0,  1,  4,  5,  2,  3,  6,  7,  8,  9,  10, 11, 12, 13, 14, 15, 16, 17,
    18, 19, 22, 23, 24, 27, 28, 29, 25, 26, 32, 31, 30, 33, 34, 20, 21,
};

const COLORREF kDefaultThemeColors[] = {
    8421504,  0x545454, 0x464646, 1315860, 3289700,  1315880, 0x464646, 1315860, 3289700,
    1315880,  6566450,  1971220,  3302450, 1318420,  3302500, 1321000,  6566450, 2626580,
    0x464646, 1315860,  0x464646, 1315860, 1250067,  3946295, 868768,   1250132, 3946345,
    5401478,  4418967,  100,      9895830, 16750230, 4934475, 0,        6579455,
};

// Strip the trailing '\n' the way the original does: scan for L'\n' and
// zero it (fgetws keeps it).
void StripNewline(wchar_t* s) {
    while (*s != L'\n')
        ++s;
    *s = L'\0';
}

// JP caption shared by every window-creation failure box of the init path
// (x64 0x7FF7CB54B0E8 = create main window).
constexpr const char kJpCaptionCreateMainWindow[] =
    "\x83\x81\x83\x43\x83\x93\x83\x45\x83\x42\x83\x93\x83\x68\x83\x45"
    "\x8d\xec\x90\xac";

// JP box text for the RecWindow RegisterClassA failure (x64 0x7FF7CB54B128
// = failed to register recording window, selected at 0x7FF7CB42EBD9).
constexpr const char kJpRecWindowRegFailed[] =
    "\x98\x5e\x89\xe6\x97\x70\x83\x45\x83\x42\x83\x93\x83\x68\x83\x45"
    "\x93\x6f\x98\x5e\x8e\xb8\x94\x73";

// JP box text for the MicWindow RegisterClassA failure (x64 0x7FF7CB54B170
// = failed to register separate window, selected at 0x7FF7CB42EC45).
constexpr const char kJpMicWindowRegFailed[] = "\x95\xca\x91\x8b\x93\x6f\x98\x5e\x8e\xb8\x94\x73";

} // namespace

// VA 0x00408EA0 - InitDShowRecorder: zeroing initializer of the 0x6C AVI-codec
// subsystem object behind app+0xA06C0.  The original zeroes dwords 0..24
// and 26, skipping dword 25 (bytes 100..103) - a VC9-unrolled quirk kept
// verbatim.  The +0x68 capture-interface pointer stays null until the
// AVI-dump dialog fills it; the 0x9EDD4 step-flag byte is allocated by
// CreateUIControls 0x467336 (see ui_init.cpp) and must never be null -
// 0x46E8F5/0x46F08C/0x464A28 dereference it unconditionally.
void InitDShowRecorder(DShowRecorder* recorder) {
    const std::int32_t selectedCodec = recorder->selectedCodec;
    std::memset(recorder, 0, sizeof(*recorder));
    recorder->selectedCodec = selectedCodec;
}

bool InitMainWindowAndD3D(MMDApp* app, void* hInstanceIn, int nShowCmd) {
    (void)nShowCmd;
    HINSTANCE hInstance = static_cast<HINSTANCE>(hInstanceIn);
    auto& s = *app;

    // ---- phase 1: subsystem allocations (ctor chains are no-ops) -------
    // 0x47A5F2: allocation size follows the restored D3DRenderer layout
    // (x86 0x1D574 / x64 0x3AA88, pinned by static_assert).
    auto* renderer = static_cast<D3DRenderer*>(operator new(sizeof(D3DRenderer), std::nothrow));
    s.Renderer() = renderer;
    std::memset(renderer, 0, sizeof(D3DRenderer));
    RendererInit(renderer); // 0x406D40

    auto* audioContext = static_cast<WaveAudioContext*>(operator new(sizeof(WaveAudioContext),
                                                                     std::nothrow)); // 0x47A648
    s.Audio() = audioContext;
    std::memset(audioContext, 0, sizeof(WaveAudioContext));
    InitAudioContext(audioContext); // 0x4C2450

    auto* recorder =
        static_cast<DShowRecorder*>(operator new(sizeof(DShowRecorder), std::nothrow)); // 0x47A698
    s.Recorder() = recorder;
    recorder->compressor = nullptr;
    InitDShowRecorder(recorder); // 0x408EA0

    void* sub04b0 = operator new(sizeof(mdl::AccessoryRecord),
                                 std::nothrow); // 0x47A6E1
    s.AxisMeshObject() = sub04b0;
    *static_cast<std::uint32_t*>(sub04b0) = 0;
    InitAccessoryRecord(sub04b0); // 0x4C4760

    // 0x47A727: allocation size follows the restored PhysicsScene layout
    // (x86 0x48 / x64 0x90, pinned by static_assert).
    auto* physics = static_cast<PhysicsScene*>(operator new(sizeof(PhysicsScene), std::nothrow));
    s.Physics() = physics;
    std::memset(physics, 0, sizeof(PhysicsScene));
    PhysicsSceneInit(physics); // 0x401360

    // ---- phase 2: exe dir + locale + UserFile defaults ------------------
    wchar_t exePath[MAX_PATH]; // 0x47A785
    GetModuleFileNameW(nullptr, exePath, 0x104);
    wchar_t drive[4], dir[256], fname[256], ext[256];
    _wsplitpath_s(exePath, drive, 3, dir, 0x100, fname, 0x100, ext, 0x100);
    swprintf_s(app->ExeDir(), 0x100, L"%s%s", drive, dir);
    SetCurrentDirectoryW(app->ExeDir());
    FontSubInit(app, app->ExeDir()); // 0x408E70
    _wsetlocale(0, L"japanese");     // LC_ALL

    wcscpy_s(app->DirModel(), 0x3E8, L"UserFile\\Model"); // 0x47A822
    wcscpy_s(app->DirAccs(), 0x3E8, L"UserFile\\Accessory");
    wcscpy_s(app->DirBg(), 0x3E8, L"UserFile\\BackGround");
    wcscpy_s(app->DirWave(), 0x3E8, L"UserFile\\Wave");
    wcscpy_s(app->DirPose(), 0x3E8, L"UserFile\\Pose");
    wcscpy_s(app->DirMotion(), 0x3E8, L"UserFile\\Motion");
    wcscpy_s(app->DirUser(), 0x3E8, L"UserFile");

    // ---- phase 3: UI colour defaults, then Data\color.txt override ------
    s.UiTextRed() = 0xFF;
    s.UiTextGreen() = 0xFF;
    s.UiTextBlue() = 0xFF;
    for (int i = 0; i < static_cast<int>(_countof(kDefaultThemeColors)); ++i)
        s.ThemeColorAt(i) = kDefaultThemeColors[i];

    FILE* stream = nullptr; // 0x47AA1B
    if (fopen_s(&stream, "Data\\color.txt", "rt") == 0) {
        char line[256];
        // The stock file separates every colour group with a blank line and
        // a descriptive heading.  Consume only syntactically valid RGB rows;
        // the 36 values themselves remain in the exact original field order.
        int colorIndex = -1;
        const int colorCount = static_cast<int>(_countof(kThemeFileOrder));
        while (colorIndex < colorCount && fgets(line, sizeof(line), stream)) {
            int r = 0, g = 0, b = 0;
            if (sscanf_s(line, " %d,%d,%d", &r, &g, &b) != 3)
                continue;

            if (colorIndex < 0) {
                s.UiTextRed() = static_cast<unsigned char>(r);
                s.UiTextGreen() = static_cast<unsigned char>(g);
                s.UiTextBlue() = static_cast<unsigned char>(b);
            } else {
                // Original packs r | g<<8 | b<<16 (COLORREF).
                s.ThemeColorAt(kThemeFileOrder[colorIndex]) =
                    static_cast<std::uint32_t>((r & 0xFF) | ((g & 0xFF) << 8) | ((b & 0xFF) << 16));
            }
            ++colorIndex;
        }
        fclose(stream);
    }

    // ---- phase 4: gradient brushes (0x40AD00 lerp factors kept exact) ---
    const float factors[8] = {0.16883117f, 0.2857143f,  0.4025974f,  0.53246754f,
                              0.64935064f, 0.76623374f, 0.49350649f, 0.90909094f};
    for (int i = 0; i < 8; ++i) // 0x47B3EA..
        s.SetUiBrush(
            i, CreateSolidBrush(ColorLerp(s.ThemeColorAt(12), s.ThemeColorAt(13), factors[i])));
    s.SetUiBrush(8,
                 CreateSolidBrush(ColorLerp(s.ThemeColorAt(20), s.ThemeColorAt(21), 0.58441556f)));
    s.SetUiBrush(9,
                 CreateSolidBrush(ColorLerp(s.ThemeColorAt(10), s.ThemeColorAt(11), 0.4025974f)));
    s.SetUiBrush(10,
                 CreateSolidBrush(ColorLerp(s.ThemeColorAt(18), s.ThemeColorAt(19), 0.21052632f)));

    // ---- phase 5: window metrics + Data\mmconfig.ini --------------------
    int screenW = GetSystemMetrics(0); // 0x47B59A
    int screenH = GetSystemMetrics(1);
    RECT rect = {100, 100, 870, 580};
    int runFlagSubsystem = 0; // -> sub_461E00 at the end
    char checkMenu119 = 0;    // -> CheckMenuItem 0x119
    AdjustWindowRect(
        &rect, WS_POPUP | WS_CAPTION | WS_SYSMENU | WS_THICKFRAME | WS_MINIMIZEBOX | WS_MAXIMIZEBOX,
        FALSE);
    LONG winL = rect.left, winT = rect.top, winR = rect.right;
    s.SeparateWindowMaximized() = 0;
    s.SeparateWindowX() = winL;
    s.SeparateWindowY() = winT;
    s.SeparateWindowWidth() = winR - winL;
    s.SeparateWindowHeight() = rect.bottom - winT;
    char checkMenu12D = 1;     // -> CheckMenuItem 0x12D
    int checkPhysicsFrame = 1; // -> auto-click dlg item 530
    int startMaximized = 0;    // maximized

    int X = 0, Y = 0, nWidth = 0, nHeight = 0;
    int tmp = 0;
    if (fopen_s(&stream, "Data\\mmconfig.ini", "rt") != 0) { // 0x47B642
        // no config: full-screen defaults
        nWidth = screenW;
        nHeight = screenH - 30;
        s.Renderer()->screenWidth = screenW;
        s.Renderer()->screenHeight = screenH;
        s.RenderWidth() = 640;
        s.RenderHeight() = 360;
        s.SidebarWidth() = 250;
        if (GetUserDefaultLCID() == 1041) // ja-JP
            s.EnglishUI() = 0;
        s.FrameVolumeControlEnabled() = 0;
        s.FrameNormalization() = 100;
    } else {
        fscanf_s(stream, "%d\n", &X); // 0x47B6DC
        fscanf_s(stream, "%d\n", &Y);
        fscanf_s(stream, "%d\n", &nWidth);
        fscanf_s(stream, "%d\n", &nHeight);
        fscanf_s(stream, "%d\n", &tmp);
        startMaximized = (tmp != 0);
        fscanf_s(stream, "%d\n", &s.RenderWidth());
        fscanf_s(stream, "%d\n", &s.RenderHeight());
        fscanf_s(stream, "%d\n", &s.SidebarWidth());
        if (fscanf_s(stream, "%d\n", &tmp) == -1) {
            if (GetUserDefaultLCID() == 1041)
                s.EnglishUI() = 0;
        } else {
            if (tmp == 0)
                s.EnglishUI() = 0;
            if (fscanf_s(stream, "%d\n", &runFlagSubsystem) != -1) {
                int maximized = 0;
                fscanf_s(stream, "%d\n", &maximized);
                s.SeparateWindowMaximized() = static_cast<std::uint8_t>(maximized);
                fscanf_s(stream, "%d\n", &s.SeparateWindowX());
                fscanf_s(stream, "%d\n", &s.SeparateWindowY());
                fscanf_s(stream, "%d\n", &s.SeparateWindowWidth());
                fscanf_s(stream, "%d\n", &s.SeparateWindowHeight());
                if (fscanf_s(stream, "%d\n", &tmp) != -1) {
                    if (tmp == 0)
                        checkPhysicsFrame = 0;
                    if (fscanf_s(stream, "%d\n", &tmp) != -1) {
                        fgetws(app->DirModel(), 1000, stream); // 0x47B8A3
                        StripNewline(app->DirModel());
                        fgetws(app->DirAccs(), 1000, stream);
                        StripNewline(app->DirAccs());
                        fgetws(app->DirWave(), 1000, stream);
                        StripNewline(app->DirWave());
                        fgetws(app->DirPose(), 1000, stream);
                        StripNewline(app->DirPose());
                        fgetws(app->DirMotion(), 1000, stream);
                        StripNewline(app->DirMotion());
                        fgetws(app->DirUser(), 1000, stream);
                        StripNewline(app->DirUser());
                        if (fscanf_s(stream, "%d\n", &tmp) != -1) {
                            fscanf_s(stream, "%d\n", &tmp);
                            s.FrameVolumeControlEnabled() = (tmp != 0);
                            fscanf_s(stream, "%d\n", &s.FrameNormalization());
                            if (fscanf_s(stream, "%d\n", &tmp) != -1) {
                                fscanf_s(stream, "%f\n", &s.SidebarRatio());
                                if (fscanf_s(stream, "%d\n", &tmp) != -1) {
                                    fgetws(app->DirBg(), 1000, stream);
                                    StripNewline(app->DirBg());
                                    if (fscanf_s(stream, "%d\n", &tmp) != -1) {
                                        fscanf_s(stream, "%d\n", &tmp);
                                        if (tmp == 1)
                                            checkMenu119 = 1;
                                        if (fscanf_s(stream, "%d\n", &tmp) != -1) {
                                            fscanf_s(stream, "%d\n", &tmp);
                                            if (tmp == 0)
                                                checkMenu12D = 0;
                                        }
                                    }
                                }
                            }
                        }
                    }
                }
            }
        }
        fclose(stream);
    }

    if (!nWidth)
        nWidth = screenW; // 0x47BB27
    if (!nHeight)
        nHeight = screenH - 30;
    if (s.RenderWidth() == 0)
        s.RenderWidth() = 640;
    if (s.RenderHeight() == 0)
        s.RenderHeight() = 360;
    if (s.SidebarWidth() < 250)
        s.SidebarWidth() = 250;
    if (s.SidebarRatio() < 0.0f)
        s.SidebarRatio() =
            static_cast<float>(static_cast<double>(s.SidebarWidth()) / static_cast<double>(nWidth));

    // ---- phase 6: window classes + CreateWindowExA ----------------------
    InitCommonControls();      // 0x47BB90
    s.HInstance() = hInstance; // this+0

    WNDCLASSA wc; // 0x47BBA2
    std::memset(&wc, 0, sizeof(wc));
    wc.style = 11;                // CS_HREDRAW|CS_VREDRAW|CS_DBLCLKS
    wc.lpfnWndProc = MainWndProc; // 0x4C3A10
    wc.cbClsExtra = 0;
    wc.cbWndExtra = 4;
    wc.hInstance = hInstance;
    wc.hIcon = LoadIconA(hInstance, MAKEINTRESOURCEA(0x64));
    wc.hCursor = LoadCursorA(nullptr, MAKEINTRESOURCEA(0x7F00)); // IDC_ARROW
    wc.hbrBackground = static_cast<HBRUSH>(GetStockObject(4));   // BLACK_BRUSH
    wc.lpszClassName = "Polygon Movie Maker";
    wc.lpszMenuName = s.EnglishUI() ? "SAMPLE02E" : "SAMPLE02";

    if (!RegisterClassA(&wc)) { // 0x47BC14
        // Original swaps only the caption on JP UI (text stays English):
        // x64 0x7FF7CB54B0E8 = "Create main window".
        MessageBoxA(nullptr, "RegisterClass failed",
                    s.EnglishUI() ? "create main window" : kJpCaptionCreateMainWindow, MB_OK);
        return false;
    }

    std::memset(&wc, 0, sizeof(wc));
    wc.style = 11;
    wc.lpfnWndProc = RecWndProc; // 0x479DA0
    wc.hInstance = hInstance;
    wc.lpszClassName = "RecWindow";
    if (!RegisterClassA(&wc)) {
        // x64 0x7FF7CB42EBAF: EN keeps "RecWindow failed"/"create main
        // window"; JP swaps both text and caption (0x7FF7CB42EBD2).
        MessageBoxA(nullptr, s.EnglishUI() ? "RecWindow failed" : kJpRecWindowRegFailed,
                    s.EnglishUI() ? "create main window" : kJpCaptionCreateMainWindow, MB_OK);
        return false;
    }

    std::memset(&wc, 0, sizeof(wc));
    wc.style = 11;
    wc.lpfnWndProc = MicWndProc; // 0x466A10
    wc.hInstance = hInstance;
    wc.lpszClassName = "MicWindow";
    if (!RegisterClassA(&wc)) {
        // x64 0x7FF7CB42EC1B: EN keeps "MicWindow failed"/"create main
        // window"; JP swaps both text and caption (0x7FF7CB42EC3E).
        MessageBoxA(nullptr, s.EnglishUI() ? "MicWindow failed" : kJpMicWindowRegFailed,
                    s.EnglishUI() ? "create main window" : kJpCaptionCreateMainWindow, MB_OK);
        return false;
    }

    char windowName[52]; // 0x47BD16
    strcpy_s(windowName, 0x32, "MikuDanceStudio");
    HWND hwnd = CreateWindowExA( // 0x47BD4B
        0, "Polygon Movie Maker", windowName, 0xCF0000u /*WS_OVERLAPPEDWINDOW*/, X, Y, nWidth,
        nHeight, nullptr, nullptr, hInstance, nullptr);
    s.Hwnd() = hwnd;
    if (!hwnd) {
        MessageBoxA(nullptr, "RegisterClass failed", "create main window", MB_OK);
        return false;
    }

    // ---- phase 7: UI language pass + startup file dispatch + placement --
    LocalizeUI(app); // 0x441AD0

    if (app->EnvFileName()[0] != L'\0') { // 0x47BD66
        SetCurrentDirectoryW(app->ExeDir());
        wchar_t destination[1000]; // original stack-local reuse, see
                                   // ExtractDirFromPath deviation note
        wcscpy_s(destination, 0x100, app->EnvFileName());
        if (app->EnvFileName()[0] == 34 /* L'"' */) {
            wchar_t* close = wcsstr(app->EnvFileName() + 1, L"\"");
            if (close)
                *close = L'\0';
            wcscpy_s(destination, 0x100, app->EnvFileName() + 1);
        }
        // TODO(port): verify aP_7/aP_8/aP_9 (Shift-JIS) are the upper-case
        // extensions ".PMM"/".PMD"/".PMX"; assumed from context.
        // The original passes a separate 256-wchar stack local as
        // ExtractDirFromPath's output (its +512 offset lands in an adjacent
        // local); `destination` keeps the full path.  Port fix found by
        // desktop runtime testing: use a dedicated dir buffer instead of
        // truncating `destination` in place (docs/ARCHITECTURE.md §8).
        wchar_t dirBuf[1000];
        if (wcsstr(destination, L".pmm") || wcsstr(destination, L".PMM")) {
            wcscpy_s(app->DirUser(), 0x3E8, ExtractDirFromPath(dirBuf, destination));
            wcscpy_s(app->EnvFileName(), 0x100, destination);
            LoadSceneFile(); // 0x458F80
        } else if (wcsstr(destination, L".pmd") || wcsstr(destination, L".PMD") ||
                   wcsstr(destination, L".pmx") || wcsstr(destination, L".PMX")) {
            wcscpy_s(app->DirModel(), 0x3E8, ExtractDirFromPath(dirBuf, destination));
            LoadModelFile(app, destination); // 0x460430
        } else if (wcsstr(destination, L".x") || wcsstr(destination, L".X")) {
            wcscpy_s(app->DirAccs(), 0x3E8, ExtractDirFromPath(dirBuf, destination));
            LoadAccessoryFile(destination); // 0x460B30
        }
    }

    WINDOWPLACEMENT wndpl; // 0x47BF79
    std::memset(&wndpl, 0, sizeof(wndpl));
    wndpl.length = 44;
    wndpl.flags = 2;                        // WPF_SETMINPOSITION? original: 2
    wndpl.showCmd = 2 * startMaximized + 1; // SW_SHOWNORMAL / SW_SHOWMAXIMIZED
    wndpl.rcNormalPosition.left = X;
    wndpl.rcNormalPosition.top = Y;
    wndpl.rcNormalPosition.right = X + nWidth;
    wndpl.rcNormalPosition.bottom = Y + nHeight;
    SetWindowPlacement(static_cast<HWND>(s.Hwnd()), &wndpl);
    UpdateWindow(static_cast<HWND>(s.Hwnd()));

    if (checkMenu119) { // 0x47BFF5
        HMENU menu = GetMenu(static_cast<HWND>(s.Hwnd()));
        CheckMenuItem(menu, MDS_CMD_TOGGLE_ALWAYS_ON_TOP, MF_CHECKED);
    }
    if (runFlagSubsystem)
        InitFlagSubsystem(app); // 0x461E00
    if (checkMenu12D) {         // 0x47C01C
        HMENU menu = GetMenu(static_cast<HWND>(s.Hwnd()));
        CheckMenuItem(menu, MDS_CMD_TOGGLE_PREVIOUS_FOLDER, MF_CHECKED);
    }
    if (checkPhysicsFrame == 1) { // 0x47C036
        HWND item = GetDlgItem(static_cast<HWND>(s.Hwnd()), panel::kPhysicsFrameCheckbox);
        SendMessageA(item, BM_SETCHECK, 1, 0);
    }
    InvalidateRect(static_cast<HWND>(s.Hwnd()), nullptr, FALSE);
    return true;
}

} // namespace mikudancestudio
