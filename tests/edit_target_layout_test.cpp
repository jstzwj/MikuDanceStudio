#include "mikudancestudio/mmd_app.hpp"
#include "mikudancestudio/model.hpp"
#include "mikudancestudio/panel_controls.hpp"
#include "mikudancestudio/ported_funcs.hpp"

#include <array>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <memory>

namespace {
int failures = 0;
int refreshes = 0;
int invalidations = 0;
int accessoryUpdates = 0;
int scrollUp = 0;
int scrollDown = 0;

void Check(bool condition, const char* message) {
    if (!condition) {
        std::fprintf(stderr, "%s\n", message);
        ++failures;
    }
}

LRESULT CALLBACK WindowProc(HWND hwnd, UINT message, WPARAM wp, LPARAM lp) {
    if (message == WM_VSCROLL) {
        if (LOWORD(wp) == SB_LINEUP)
            ++scrollUp;
        if (LOWORD(wp) == SB_LINEDOWN)
            ++scrollDown;
        return 0;
    }
    return DefWindowProcA(hwnd, message, wp, lp);
}
}

namespace mikudancestudio {
MMDApp::MMDApp() : state{} {
}
void RebuildTimelineTrackPanel(MMDApp*) {
    ++refreshes;
}
void InvalidateSelectionReadouts(MMDApp*) {
    ++invalidations;
}
void SyncAccessoryEditPanel(MMDApp*) {
    ++accessoryUpdates;
}
void SelectPrevEditTarget(MMDApp*);
void SelectNextEditTarget(MMDApp*);

// ui_init.cpp also contains the full startup path. This test only refills
// existing controls; reaching any startup dependency is a test failure.
void MMDApp::ResetSceneLight() noexcept {
    std::abort();
}
std::uint32_t ColorLerp(std::uint32_t, std::uint32_t, float) {
    std::abort();
}
bool InitAxisMesh(MMDApp*) {
    std::abort();
}
bool MakeLineGeometry(MMDApp*) {
    std::abort();
}
bool SceneConstruct(PhysicsScene*, D3DRenderer*) {
    std::abort();
}
void InstallControlSubclass(MMDApp*, HWND, int) {
    std::abort();
}
void CreateUiFont(MMDApp*, HWND) {
    std::abort();
}
void ApplyUiFontToControl(MMDApp*, HWND, int) {
    std::abort();
}
bool InitToonTextures(MMDApp*) {
    std::abort();
}
bool InitSceneFontTexture(MMDApp*) {
    std::abort();
}
bool InitD3D(MMDApp*, HWND, bool, HINSTANCE) {
    std::abort();
}
}

namespace {
namespace mds = mikudancestudio;
namespace mdl = mds::mdl;

void TestBoneRegisterCombo(mds::MMDApp& app, HWND combo) {
    // Original x64 0x7FF7CB479B00: signed count at +0x3110, 624-byte
    // bones, signed type < 7 or type == 8, JP/EN names at +0/+20.
    auto model = std::make_unique<mdl::ModelRecord>();
    std::array<mdl::BoneRecord, 12> bones{};
    for (int i = 0; i < static_cast<int>(bones.size()); ++i) {
        const auto type = i < 10 ? i : (i == 10 ? 0x80 : 0xFF);
        bones[i].type = static_cast<mdl::BoneType>(type);
        std::snprintf(bones[i].name, sizeof(bones[i].name), "JP%d", i);
        std::snprintf(bones[i].nameEn, sizeof(bones[i].nameEn), "EN%d", i);
    }
    model->boneCount = static_cast<std::uint32_t>(bones.size());
    model->boneTable = bones.data();
    const int slot = mds::kModelSlotCount - 1;
    app.ModelSlot(slot) = model.get();
    constexpr int included[] = {0, 1, 2, 3, 4, 5, 6, 8, 10, 11};
    for (int english : {0, 1}) {
        app.EnglishUI() = static_cast<unsigned char>(english);
        invalidations = 0;
        mds::RefillBoneRegisterCombo(&app, slot);
        Check(SendMessageA(combo, CB_GETCOUNT, 0, 0) == std::size(included),
              "Bone combo uses the native count and the original signed type filter");
        for (std::size_t row = 0; row < std::size(included); ++row) {
            char actual[32]{}, expected[32]{};
            SendMessageA(combo, CB_GETLBTEXT, row, reinterpret_cast<LPARAM>(actual));
            std::snprintf(expected, sizeof(expected), "%s%d", english ? "EN" : "JP", included[row]);
            Check(std::strcmp(actual, expected) == 0,
                  "Bone combo preserves bone order and language");
        }
        Check(invalidations == 1, "Bone combo invalidates readouts once");
    }
    mds::RefillBoneRegisterCombo(&app, -1);
    Check(SendMessageA(combo, CB_GETCOUNT, 0, 0) == 0, "Negative model slot clears the combo");
    mds::RefillBoneRegisterCombo(&app, 0);
    Check(SendMessageA(combo, CB_GETCOUNT, 0, 0) == 0, "Empty model slot leaves the combo empty");
    model->boneCount = 0;
    mds::RefillBoneRegisterCombo(&app, slot);
    Check(SendMessageA(combo, CB_GETCOUNT, 0, 0) == 0, "Zero bone count leaves the combo empty");
    model->boneCount = 0xFFFFFFFFu;
    mds::RefillBoneRegisterCombo(&app, slot);
    Check(SendMessageA(combo, CB_GETCOUNT, 0, 0) == 0,
          "Original signed negative count skips all bones");
    app.ModelSlot(slot) = nullptr;
}

void TestEditTargetNavigation(mds::MMDApp& app, HWND combo) {
    // Original x64 prev/next/scroll: 0x7FF7CB4919D0 / 0x7FF7CB491BF0 /
    // 0x7FF7CB491E40. Slot order is independent of accessory display order.
    mdl::AccessoryRecord first{}, second{}, third{};
    first.order = 0;
    second.order = 1;
    third.order = 2;
    first.shadowEnabled = 1;
    third.shadowEnabled = 1;
    app.AccessorySlot(254) = &first;
    app.AccessorySlot(3) = &second;
    app.AccessorySlot(128) = &third;
    for (const char* text : {"first", "second", "third"})
        SendMessageA(combo, CB_ADDSTRING, 0, reinterpret_cast<LPARAM>(text));

    const auto checkShadows = [&] {
        Check(first.shadowEnabled == 1 && second.shadowEnabled == 0 && third.shadowEnabled == 1,
              "Target navigation preserves every accessory shadow flag");
    };
    const auto navigate = [&](bool next) {
        refreshes = invalidations = accessoryUpdates = scrollUp = scrollDown = 0;
        if (next)
            mds::SelectNextEditTarget(&app);
        else
            mds::SelectPrevEditTarget(&app);
        Check(refreshes == 1 && invalidations == 1, "Navigation refreshes both panels once");
        checkShadows();
    };
    const auto checkTrack = [&](mds::GlobalTimelineTrack expected) {
        for (int i = 0; i < 4; ++i) {
            const auto track = static_cast<mds::GlobalTimelineTrack>(i);
            Check(app.GlobalTrackSelected(track) == (track == expected),
                  "Global target is exclusive");
        }
        Check(!first.rowSelected && !second.rowSelected && !third.rowSelected,
              "Global targets clear accessory row selection");
    };
    const auto checkAccessory = [&](int slot, int order) {
        Check(app.SelectedObjectSlot() == slot,
              "Navigation selects the slot with the adjacent order");
        Check(first.rowSelected == (&first == app.AccessorySlot(slot)) &&
                  second.rowSelected == (&second == app.AccessorySlot(slot)) &&
                  third.rowSelected == (&third == app.AccessorySlot(slot)),
              "Exactly the destination accessory row is selected");
        for (int i = 0; i < 4; ++i)
            Check(!app.GlobalTrackSelected(static_cast<mds::GlobalTimelineTrack>(i)),
                  "Accessory target clears global target flags");
        Check(SendMessageA(combo, CB_GETCURSEL, 0, 0) == order,
              "Accessory combo follows display order");
    };

    app.SelectGlobalTimelineTrack(mds::GlobalTimelineTrack::Camera);
    first.rowSelected = second.rowSelected = third.rowSelected = 1;
    navigate(false);
    checkTrack(mds::GlobalTimelineTrack::Camera);
    for (int i = 1; i < 4; ++i) {
        navigate(true);
        checkTrack(static_cast<mds::GlobalTimelineTrack>(i));
    }
    navigate(true);
    checkAccessory(254, 0);
    Check(accessoryUpdates == 1, "Changing accessory updates its edit controls");
    navigate(true);
    checkAccessory(3, 1);
    navigate(true);
    checkAccessory(128, 2);
    navigate(true);
    checkAccessory(128, 2);
    Check(accessoryUpdates == 0,
          "Next after the last accessory restores its row without a combo update");
    navigate(false);
    checkAccessory(3, 1);
    navigate(false);
    checkAccessory(254, 0);
    navigate(false);
    checkTrack(mds::GlobalTimelineTrack::Gravity);
    for (int i = 2; i >= 0; --i) {
        navigate(false);
        checkTrack(static_cast<mds::GlobalTimelineTrack>(i));
    }
    for (bool next : {false, true}) {
        app.ClearGlobalTimelineTrackSelection();
        app.SelectedObjectSlot() = 50;
        first.rowSelected = second.rowSelected = third.rowSelected = 1;
        navigate(next);
        checkTrack(mds::GlobalTimelineTrack::Camera);
        Check(app.SelectedObjectSlot() == 0, "Missing current accessory resets the slot");
    }

    first.order = 4;
    app.SelectGlobalTimelineTrack(mds::GlobalTimelineTrack::Gravity);
    navigate(true);
    checkTrack(mds::GlobalTimelineTrack::Gravity);
    Check(accessoryUpdates == 0,
          "Missing order zero keeps gravity selected without editing controls");
    first.order = 0;

    // An absent adjacent order is not skipped. The original previous path
    // leaves rows clear; the next path restores the current row.
    app.ClearGlobalTimelineTrackSelection();
    app.SelectedObjectSlot() = 128;
    second.order = 6;
    navigate(false);
    Check(app.SelectedObjectSlot() == 128 && !first.rowSelected && !second.rowSelected &&
              !third.rowSelected,
          "Missing previous order leaves accessory rows clear");
    app.SelectedObjectSlot() = 254;
    navigate(true);
    checkAccessory(254, 0);

    // The comparison adds in int: order 255 must not wrap to order zero.
    second.order = 255;
    app.SelectedObjectSlot() = 3;
    navigate(true);
    Check(app.SelectedObjectSlot() == 3 && second.rowSelected && !first.rowSelected,
          "Next order comparison does not wrap a byte at 255");

    first.order = 9;
    second.order = 10;
    third.order = 11;
    app.SelectedObjectSlot() = 254;
    app.DisplayObjectListScrollPosition() = 0;
    RECT client{};
    GetClientRect(static_cast<HWND>(app.Hwnd()), &client);
    navigate(true);
    const int down = 2 - ((client.bottom - 452) / 14 - 10);
    Check(scrollDown == (down > 0 ? down : 0) && scrollUp == 0,
          "Accessory row scroll uses the original visible-row budget");
    app.SelectedObjectSlot() = 254;
    app.DisplayObjectListScrollPosition() = 20;
    navigate(true);
    Check(scrollUp == 10 && scrollDown == 0,
          "Accessory row above the view scrolls upward by its delta");
    app.AccessorySlot(254) = app.AccessorySlot(3) = app.AccessorySlot(128) = nullptr;
}
}

int main() {
    const HINSTANCE instance = GetModuleHandleA(nullptr);
    WNDCLASSA cls{};
    cls.lpfnWndProc = WindowProc;
    cls.hInstance = instance;
    cls.lpszClassName = "EditTargetLayoutTest";
    if (!RegisterClassA(&cls))
        return 1;
    HWND window = CreateWindowA(cls.lpszClassName, "", WS_OVERLAPPEDWINDOW, 0, 0, 800, 650, nullptr,
                                nullptr, instance, nullptr);
    const auto control = [&](const char* name, DWORD style, int id) {
        return CreateWindowA(name, "", WS_CHILD | style, 0, 0, 200, 200, window,
                             reinterpret_cast<HMENU>(static_cast<INT_PTR>(id)), instance, nullptr);
    };
    HWND bones = control("COMBOBOX", CBS_DROPDOWNLIST, mds::panel::kBoneRegisterCombo);
    HWND accessories = control("COMBOBOX", CBS_DROPDOWNLIST, mds::panel::kAccessoryCombo);
    HWND scroll = control("SCROLLBAR", SBS_VERT, mds::panel::kTimelineVScroll);
    Check(window && bones && accessories && scroll, "Create hidden Win32 controls");
    if (window && bones && accessories && scroll) {
        auto app = std::make_unique<mds::MMDApp>();
        app->state.hwnd = window;
        TestBoneRegisterCombo(*app, bones);
        TestEditTargetNavigation(*app, accessories);
    }
    DestroyWindow(window);
    UnregisterClassA(cls.lpszClassName, instance);
    return failures == 0 ? 0 : 1;
}
