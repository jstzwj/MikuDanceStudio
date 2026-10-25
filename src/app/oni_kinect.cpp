// ===========================================================================
// VA 0x00429CB0 / 0x0042A020 - DxOpenNI (Kinect) plugin loader and disable
// ===========================================================================
// 0x429CB0 OpenNiInit(app, sjisPath)   - thiscall, one stack arg (ret 4).
// 0x42A020 DisableKinect(app)          - thiscall, no stack args (plain ret).
//
// State (all verified from the live disassembly):
//   app+0xA03B8  Kinect-enabled byte (set 1 on success, 0 in disable)
//   app+0xA03BC  DxOpenNI.dll HMODULE (nulled on every failure path)
//   app+0xA03C0  ?OpenNIInit@@YG_NPAUHWND__@@_NPAUIDirect3DDevice9@@PA_WPAD@Z
//   app+0xA03C4  ?OpenNIClean@@YGXXZ
//   app+0xA03C8  ?OpenNIDrawDepthMap@@YGX_N@Z
//   app+0xA03CC  ?OpenNIDepthTexture@@YGXPAPAUIDirect3DTexture9@@@Z
//   app+0xA03D0  ?OpenNIGetSkeltonJointPosition@@YGXHPAUD3DXVECTOR3@@@Z
//   app+0xA03D4  ?OpenNIIsTracking@@YGXPA_N@Z
//   app+0xA03D8  ?OpenNIGetVersion@@YGXPAM@Z
//   app+0xA03DF  capture-mode byte (1 while a capture file drives the fps cap)
//   app+0xA03E0  fps-limit saved across Kinect capture (restored on disable)
//   app+0xA03EA  DxOpenNI version code: 0x0D=1.30 / 0x0E=1.40 / 0x0F=1.50
//                (consumed by menu command 292 for per-slot model+0x38FD)
//   app+0xA08E0  current fps limit (60.0 default, 30.0 while capturing)
//   app+0xA0D68  auto-frame-record byte (cleared by both functions)
//   app+0x9EDB5  post-disable re-arm byte (set 1 by the disable path)
//
// 0x429CB0 flow:
//   SetCurrentDirectoryW(app+0xA06CE exe dir)
//   _wsopen_s(&fd, L"Data\\DxOpenNI.dll", _O_BINARY, _SH_DENYNO, _S_IWRITE)
//     errno != 0 -> EN/JP "cannot find" box, return
//   _close(fd); LoadLibraryA("Data\\DxOpenNI.dll") -> +0xA03BC
//     NULL -> ShowWin32ErrorMessage(app, "GetProcAddress", GetLastError())
//             + EN/JP "Cannot move OpenNI" box, return
//   7x GetProcAddress -> +0xA03C0..0xA03D8; any NULL -> EN/JP version box
//   OpenNIGetVersion(&ver): 1.30f -> 0x0D, 1.40f -> 0x0E, 1.50f -> 0x0F
//     (each compare is fucom + test 0x44/jp: unordered also matches, which
//      == reproduces for the reachable non-NaN versions)
//     no match -> EN/JP "1.30 or 1.40 or 1.50" box -> FreeLibrary, return
//   OpenNIInit(hwnd, english, d3d9 device @sub1d574+0x1D4E0, exe dir, path)
//     FALSE -> FreeLibrary(+0xA03BC), +0xA03BC = 0, return
//   CheckMenuItem(GetMenu(hwnd), 0x123, MF_CHECKED); +0xA03B8 = 1
//   slot refresh: +0x2F8 == 0 -> ModelInitMorphSlots(slot[byte +0x910])
//   +0xA03E0 <- fps limit (+0xA08E0); sjisPath != NULL -> +0xA03DF = 1 and
//   +0xA08E0 <- 30.0f (0x52997C); +0xA0D68 = 0
//
// 0x42A020 flow:
//   CheckMenuItem(GetMenu(hwnd), 0x123, MF_UNCHECKED)
//   EnableMenuItem(GetMenu(hwnd), 0x124, 1)
//   CheckMenuItem(GetMenu(hwnd), 0x124, MF_UNCHECKED)
//   OpenNIClean() through +0xA03C4 - unconditional, like the original
//   +0xA03B8 = 0, +0xA0D68 = 0
//   model slot (byte +0x910) non-empty -> SeekModelFrame(model, +0x980, +0xA0CC4)
//   +0xA08E0 <- saved +0xA03E0; +0x9EDB5 = 1; +0xA03DF = 0
// =========================================================================//
#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include "mikudancestudio/menu_commands.h"
#include <Windows.h>
#include <d3d9.h>
#include <fcntl.h>
#include <share.h>
#include <sys/stat.h>
#include <io.h>

#include <cstdint>

#include "mikudancestudio/globals.hpp"
#include "mikudancestudio/mmd_app.hpp"
#include "mikudancestudio/ported_funcs.hpp"
#include "mikudancestudio/model.hpp"

namespace mikudancestudio {

// [sub1d574 + 0x1D4E0] D3D device, same fetch as RenderDeviceOf in
// app_utilities.cpp (0x429f33: mov 0xa06c4; mov 0x1d4e0).
static IDirect3DDevice9* OniRenderDevice(MMDApp* app) {
    D3DRenderer* r = app->Renderer();
    if (r == nullptr)
        return nullptr;
    return r->device; // +0x1D4E0
}

namespace {

// DxOpenNI.dll entry points, in GetProcAddress order (0x429dad..0x429e2b).
const char* const kOniExports[static_cast<std::size_t>(OniExport::Count)] = {
    "?OpenNIInit@@YG_NPAUHWND__@@_NPAUIDirect3DDevice9@@PA_WPAD@Z",
    "?OpenNIClean@@YGXXZ",
    "?OpenNIDrawDepthMap@@YGX_N@Z",
    "?OpenNIDepthTexture@@YGXPAPAUIDirect3DTexture9@@@Z",
    "?OpenNIGetSkeltonJointPosition@@YGXHPAUD3DXVECTOR3@@@Z",
    "?OpenNIIsTracking@@YGXPA_N@Z",
    "?OpenNIGetVersion@@YGXPAM@Z",
};

void KinMessageBox(MMDApp* app, const char* en, const char* jp) {
    MessageBoxA(static_cast<HWND>(app->Hwnd()), app->EnglishUI() != 0 ? en : jp, "Kinect",
                MB_OK); // 0x5292a8
}

// bool __stdcall OpenNIInit(HWND, bool, IDirect3DDevice9*, wchar_t*, char*)
using OpenNiInitFn = unsigned char(__stdcall*)(HWND, unsigned char, IDirect3DDevice9*,
                                               const wchar_t*, const char*);

// JP-locale MessageBox texts - verbatim Shift-JIS bytes from .rdata.
// 0x52C528 "DxOpenNI.dll is missing from the Data folder..." (DLL missing)
const char kJpOniNotFound[] = "\x44\x78\x4f\x70\x65\x6e\x4e\x49\x2e\x64\x6c\x6c\x82\xaa\x44\x61"
                              "\x74\x61\x83\x74\x83\x48\x83\x8b\x83\x5f\x93\xe0\x82\xc9\x82\xa0"
                              "\x82\xe8\x82\xdc\x82\xb9\x82\xf1\x81\x42\x0a\x0a\x68\x74\x74\x70"
                              "\x3a\x2f\x2f\x77\x77\x77\x2e\x67\x65\x6f\x63\x69\x74\x69\x65\x73"
                              "\x2e\x6a\x70\x2f\x68\x69\x67\x75\x63\x68\x75\x75\x34\x2f\x20\x82"
                              "\xe6\x82\xe8\x44\x78\x4f\x70\x65\x6e\x4e\x49\x2e\x64\x6c\x6c\x82"
                              "\xf0\x83\x5f\x83\x45\x83\x93\x83\x8d\x81\x5b\x83\x68\x82\xb5\x82"
                              "\xc4\x0a\x44\x61\x74\x61\x83\x74\x83\x48\x83\x8b\x83\x5f\x93\xe0"
                              "\x82\xc9\x92\x75\x82\xa2\x82\xc4\x89\xba\x82\xb3\x82\xa2\x81\x42";
// 0x52C430 "OpenNI does not work..." (LoadLibrary failed)
const char kJpOniCannotMove[] = "\x4f\x70\x65\x6e\x4e\x49\x82\xaa\x93\xae\x8d\xec\x82\xb5\x82\xdc"
                                "\x82\xb9\x82\xf1\x81\x42\x82\xa8\x82\xbb\x82\xe7\x82\xad\x4f\x70"
                                "\x65\x6e\x4e\x49\x82\xcc\x83\x43\x83\x93\x83\x58\x83\x67\x81\x5b"
                                "\x83\x8b\x82\xc9\x8e\xb8\x94\x73\x82\xb5\x82\xc4\x82\xa2\x82\xdc"
                                "\x82\xb7\x81\x42\x0a\x4f\x70\x65\x6e\x4e\x49\x82\xcc\x83\x54\x83"
                                "\x93\x83\x76\x83\x8b\x81\x41\x4e\x69\x55\x73\x65\x72\x54\x72\x61"
                                "\x63\x6b\x65\x72\x82\xaa\x90\xb3\x82\xb5\x82\xad\x93\xae\x8d\xec"
                                "\x82\xb7\x82\xe9\x82\xe6\x82\xa4\x82\xc9\x4f\x70\x65\x6e\x4e\x49"
                                "\x82\xf0\x83\x43\x83\x93\x83\x58\x83\x67\x81\x5b\x83\x8b\x82\xb5"
                                "\x92\xbc\x82\xb5\x82\xc4\x89\xba\x82\xb3\x82\xa2\x81\x42";
// 0x52C298 "DxOpenNI.dll version mismatch\n(Version 1.30 is required)"
const char kJpOniVersion130[] = "\x44\x78\x4f\x70\x65\x6e\x4e\x49\x2e\x64\x6c\x6c\x82\xcc\x83\x6f"
                                "\x81\x5b\x83\x57\x83\x87\x83\x93\x82\xaa\x88\xd9\x82\xc8\x82\xe8"
                                "\x82\xdc\x82\xb7\x0a\x28\x83\x6f\x81\x5b\x83\x57\x83\x87\x83\x93"
                                "\x31\x2e\x33\x30\x82\xaa\x95\x4b\x97\x76\x82\xc5\x82\xb7\x29";
// 0x52C1F8 "DxOpenNI.dll version mismatch\n(Any version from 1.30 to 1.50)"
const char kJpOniVersionRange[] = "\x44\x78\x4f\x70\x65\x6e\x4e\x49\x2e\x64\x6c\x6c\x82\xcc\x83\x6f"
                                  "\x81\x5b\x83\x57\x83\x87\x83\x93\x82\xaa\x88\xd9\x82\xc8\x82\xe8"
                                  "\x82\xdc\x82\xb7\x0a\x28\x83\x6f\x81\x5b\x83\x57\x83\x87\x83\x93"
                                  "\x31\x2e\x33\x30\x81\x60\x31\x2e\x35\x30\x82\xdc\x82\xc5\x82\xcc"
                                  "\x82\xc7\x82\xea\x82\xa9\x29";

} // namespace

// VA 0x00429CB0 - load Data\DxOpenNI.dll and arm Kinect capture.
void OpenNiInit(MMDApp* app, const char* sjisPath) {
    auto& s = *app;
    HWND hwnd = static_cast<HWND>(s.Hwnd()); // 0xa06b8

    SetCurrentDirectoryW(s.state.exeDir); // 0x429cbe

    // 0x429cc4..0x429ce4: probe the DLL with _wsopen_s (CRT 0x50747B).
    int fd = -1;
    if (_wsopen_s(&fd, L"Data\\DxOpenNI.dll", _O_BINARY /*0x8000*/, _SH_DENYNO /*0x40*/,
                  _S_IWRITE /*0x80*/) != 0) {
        KinMessageBox(app,
                      "DxOpenNI.dll cannot find in Data folder.\n\n"
                      "Please download it from http://www.geocities.jp/higuchuu4/"
                      "index_e.htm \nand set it into data folder.", // 0x52c5c0
                      kJpOniNotFound);                              // 0x52c528
        return;
    }
    _close(fd); // 0x506b88

    HMODULE module = LoadLibraryA("Data\\DxOpenNI.dll"); // 0x429d3d
    s.state.oniModule = module;
    if (module == nullptr) {
        ShowWin32ErrorMessage(app, "GetProcAddress", GetLastError());
        KinMessageBox(app,
                      "Cannot move OpenNI.\nOpenNI might be failed install.", // 0x52c4d0
                      kJpOniCannotMove);                                      // 0x52c430
        return;
    }

    // 0x429da6..0x429e86: seven GetProcAddress fills + any-null test.
    // The named entries retain the DLL lookup order in native runtime storage.
    for (int i = 0; i < static_cast<int>(OniExport::Count); ++i)
        s.OniExportSlot(static_cast<OniExport>(i)) =
            reinterpret_cast<void*>(GetProcAddress(module, kOniExports[i]));
    bool anyNull = false;
    for (int i = 0; i < static_cast<int>(OniExport::Count); ++i)
        anyNull = anyNull || s.OniExportSlot(static_cast<OniExport>(i)) == nullptr;
    if (anyNull) {
        KinMessageBox(app,
                      "\"DxOpenNI.dll\" version is wrong.\n"
                      "(Version1.30 is necessary)", // 0x52c2d8
                      kJpOniVersion130);            // 0x52c298
        FreeLibrary(module);                        // 0x42a008
        s.state.oniModule = nullptr;
        return;
    }

    // 0x429ed4..0x429f2d: version gate (1.30 / 1.40 / 1.50 bit-exact).
    auto getVersion =
        reinterpret_cast<void(__stdcall*)(float*)>(s.OniExportSlot(OniExport::Version));
    float version = 0.0f;
    getVersion(&version);
    std::uint8_t versionCode;
    if (version == 1.3f) { // 0x52c290
        versionCode = 0x0D;
    } else if (version == 1.4f) { // 0x52c28c
        versionCode = 0x0E;
    } else if (version == 1.5f) { // 0x52bf5c
        versionCode = 0x0F;
    } else {
        KinMessageBox(app,
                      "\"DxOpenNI.dll\" version is wrong.\n"
                      "(Version1.30 or 1.40 or 1.50 is necessary)", // 0x52c240
                      kJpOniVersionRange);                          // 0x52c1f8
        FreeLibrary(module);
        s.state.oniModule = nullptr;
        return;
    }
    s.state.openniVersion = versionCode;

    // 0x429f2f..0x429f5b: OpenNIInit(hwnd, english, device, exedir, path).
    auto init = reinterpret_cast<OpenNiInitFn>(s.OniExportSlot(OniExport::Initialize));
    const unsigned char ok =
        init(hwnd, s.EnglishUI(), OniRenderDevice(app), s.state.exeDir, sjisPath);
    if (ok == 0) {
        FreeLibrary(module); // 0x42a001
        s.state.oniModule = nullptr;
        return;
    }

    CheckMenuItem(GetMenu(hwnd), MDS_CMD_TOGGLE_KINECT, MF_CHECKED); // 0x429f61
    s.state.depthDeviceEnabled = 1;                                  // 0x429f85

    if (s.CameraMode() == 0) { // 0x429f8b
        unsigned char* model = s.SelectedModel();
        ModelInitMorphSlots(model); // 0x4a89b0
    }

    // 0x429fa0..0x429fbc: save the fps limit; a capture file caps it to 30.
    s.state.fpsLimitSaved = s.state.fpsLimit;
    if (sjisPath != nullptr) {
        s.state.kinectCaptureActive = 1;
        s.state.fpsLimit = 30.0f; // 0x52997c
    }
    s.state.autoRepeat = 0; // 0x429fc4
}

// VA 0x0042A020 - stop Kinect capture and restore the saved fps limit.
void DisableKinect(MMDApp* app) {
    auto& s = *app;
    HWND hwnd = static_cast<HWND>(s.Hwnd());

    CheckMenuItem(GetMenu(hwnd), MDS_CMD_TOGGLE_KINECT, MF_UNCHECKED);         // 0x42a029
    EnableMenuItem(GetMenu(hwnd), MDS_CMD_TOGGLE_MOTION_CAPTURE, 1);           // 0x42a044
    CheckMenuItem(GetMenu(hwnd), MDS_CMD_TOGGLE_MOTION_CAPTURE, MF_UNCHECKED); // 0x42a05b

    // Unconditional call through the stored OpenNIClean pointer, exactly
    // like the original (reachable only after a successful OpenNiInit).
    auto clean = reinterpret_cast<void(__stdcall*)()>(s.OniExportSlot(OniExport::Clean));
    clean(); // 0x42a074

    s.state.depthDeviceEnabled = 0; // 0x42a084
    s.state.autoRepeat = 0;         // 0x42a08b

    unsigned char* model = s.SelectedModel();
    if (model != nullptr) // 0x42a092
        SeekModelFrame(*mdl::Mdl(model), s.state.currentFrame, s.PlaybackPhysicsMode());

    s.state.fpsLimit = // 0x42a0ac
        s.state.fpsLimitSaved;
    s.PhysicsResetPending() = 1;     // 0x42a0b9
    s.state.kinectCaptureActive = 0; // 0x42a0c0
}

} // namespace mikudancestudio
