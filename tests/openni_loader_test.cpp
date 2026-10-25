#include "mikudancestudio/mmd_app.hpp"
#include "mikudancestudio/ported_funcs.hpp"

#include <cstdio>
#include <filesystem>
#include <memory>
#include <string>
#include <cwchar>

using namespace mikudancestudio;
namespace {
int failures, messages;
void Check(bool ok, const char* message) {
    if (!ok) {
        std::fprintf(stderr, "FAIL: %s\n", message);
        ++failures;
    }
}
LRESULT CALLBACK DismissMessage(int code, WPARAM wp, LPARAM lp) {
    if (code == HCBT_CREATEWND) {
        wchar_t cls[32]{};
        GetClassNameW(reinterpret_cast<HWND>(wp), cls, 32);
        if (std::wcscmp(cls, L"#32770") == 0) {
            ++messages;
            PostMessageW(reinterpret_cast<HWND>(wp), WM_CLOSE, 0, 0);
        }
    }
    return CallNextHookEx(nullptr, code, wp, lp);
}
}
int wmain(int argc, wchar_t** argv) {
    if (argc != 3)
        return 2;
    namespace fs = std::filesystem;
    const auto oldDirectory = fs::current_path();
    const auto directory =
        fs::temp_directory_path() / (L"mds-openni-" + std::to_wstring(GetCurrentProcessId()));
    fs::create_directories(directory / L"Data");
    auto app = std::make_unique<MMDApp>();
    app->state = {};
    app->CameraMode() = 1; // Avoid a model reset; loader ABI does not require a model.
    app->EnglishUI() = 1;
    app->state.fpsLimit = 60;
    wcscpy_s(app->state.exeDir, directory.c_str());
    const HHOOK hook = SetWindowsHookExW(WH_CBT, DismissMessage, nullptr, GetCurrentThreadId());
    Check(hook != nullptr, "message-box hook installed");
    OpenNiInit(app.get(), nullptr);
    Check(!app->state.oniModule && !app->state.depthDeviceEnabled && messages == 1,
          "missing DLL reports error without enabling tracking");

    // A real DLL missing the version export verifies all-seven-export rejection.
    const auto target = directory / L"Data" / L"DxOpenNI.dll";
    fs::copy_file(argv[2], target);
    OpenNiInit(app.get(), nullptr);
    Check(!app->state.oniModule && !app->OpenNi().initialize && messages == 2,
          "missing export unloads module and clears callable pointers");
    fs::remove(target);
    fs::copy_file(argv[1], target);
    HMODULE fixture = LoadLibraryW(target.c_str());
    if (!fixture)
        return 3;
    auto configure =
        reinterpret_cast<void(WINAPI*)(float, bool)>(GetProcAddress(fixture, "FixtureConfigure"));
    auto calls = reinterpret_cast<int(WINAPI*)(bool)>(GetProcAddress(fixture, "FixtureCalls"));
    if (!configure || !calls)
        return 4;
    configure(1.6f, true);
    OpenNiInit(app.get(), nullptr);
    Check(!app->state.oniModule && !app->OpenNi().Complete() && calls(false) == 0 && messages == 3,
          "unsupported version rejected before initialization");
    configure(1.5f, false);
    OpenNiInit(app.get(), nullptr);
    Check(!app->state.oniModule && !app->OpenNi().Complete() && calls(false) == 1 &&
              calls(true) == 0,
          "failed SDK initialization unloads without cleaning an uninitialized session");
    for (float version : {1.3f, 1.4f, 1.5f}) {
        configure(version, true);
        OpenNiInit(app.get(), "fixture.oni");
        Check(app->state.oniModule && app->OpenNi().Complete() && app->state.depthDeviceEnabled &&
                  app->state.kinectCaptureActive && app->state.fpsLimit == 30 &&
                  app->state.fpsLimitSaved == 60 && app->state.autoRepeat == 0,
              "accepted ABI initializes and arms file capture");
        if (!app->OpenNi().Complete())
            break;
        bool active = false;
        app->OpenNi().isTracking(&active);
        float joint[3]{};
        app->OpenNi().jointPosition(7, joint);
        IDirect3DTexture9* texture = reinterpret_cast<IDirect3DTexture9*>(1);
        app->DepthTextureCallback()(&texture);
        Check(active && joint[0] == 7 && !texture, "bool, joint and texture output ABI");
        DisableKinect(app.get());
        Check(calls(true) == 1 && app->state.oniModule && !app->state.depthDeviceEnabled &&
                  app->state.fpsLimit == 60,
              "stop cleans once and retains the library");
        FreeLibrary(app->state.oniModule);
        app->state.oniModule = nullptr;
        app->OpenNi() = {};
    }
    FreeLibrary(fixture);
    UnhookWindowsHookEx(hook);
    fs::current_path(oldDirectory);
    if (!fs::equivalent(directory.parent_path(), fs::temp_directory_path()) ||
        directory.filename() != L"mds-openni-" + std::to_wstring(GetCurrentProcessId()))
        return 5;
    fs::remove_all(directory);
    return failures ? 1 : 0;
}
