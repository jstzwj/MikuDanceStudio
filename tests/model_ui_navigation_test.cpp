#include "mikudancestudio/mmd_app.hpp"
#include "mikudancestudio/model.hpp"
#include "mikudancestudio/panel_controls.hpp"
#include "mikudancestudio/ported_funcs.hpp"
#include "../src/app/bone_selection.hpp"

#include <array>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <memory>
#include <string>
#include <vector>

namespace mds = mikudancestudio;
namespace mdl = mikudancestudio::mdl;
namespace {
int failures, paints, boxActiveAtPaint;
std::vector<unsigned> scrolls;
struct Label {
    std::string text;
    int x, y;
    COLORREF color;
};
std::vector<Label> labels;
void Check(bool ok, const char* message) {
    if (!ok) {
        std::fprintf(stderr, "FAIL: %s\n", message);
        ++failures;
    }
}
LRESULT CALLBACK WindowProc(HWND hwnd, UINT msg, WPARAM wp, LPARAM lp) {
    if (msg == WM_VSCROLL) {
        scrolls.push_back(LOWORD(wp));
        mds::HandleVScroll(lp, wp);
        return 0;
    }
    return DefWindowProcA(hwnd, msg, wp, lp);
}
HWND Child(HWND parent, int id, const char* cls, DWORD style = 0) {
    if (std::strcmp(cls, "EDIT") == 0)
        style |= ES_AUTOHSCROLL;
    HWND child = CreateWindowExA(0, cls, "", WS_CHILD | style, 0, 0, 16, 100, parent,
                                 reinterpret_cast<HMENU>(static_cast<INT_PTR>(id)),
                                 GetModuleHandleA(nullptr), nullptr);
    if (!child)
        std::abort();
    return child;
}
void Add(HWND combo, const char* text) {
    SendMessageA(combo, CB_ADDSTRING, 0, reinterpret_cast<LPARAM>(text));
}
struct Fixture {
    std::unique_ptr<mds::MMDApp> app = std::make_unique<mds::MMDApp>();
    std::unique_ptr<mds::D3DRenderer> renderer = std::make_unique<mds::D3DRenderer>();
    std::unique_ptr<mdl::ModelRecord> model = std::make_unique<mdl::ModelRecord>();
    std::array<mdl::BoneRecord, 6> bones{};
    std::array<mdl::DisplayGroup, 7> groups{};
    std::array<mdl::FrameGroup, 5> entries{};
    std::array<mdl::FrameGroup, 2> faces{};
    std::array<unsigned char, 6> selected{}, edited{};
    std::array<std::int32_t, 6> boneRows{};
    std::array<std::int32_t, 2> morphRows{};
    std::array<mdl::MorphRecord, 2> morphs{};
    HWND window;
    HDC panel = CreateCompatibleDC(nullptr);
    HBITMAP bitmap = CreateBitmap(320, 900, 1, 32, nullptr);
    HBITMAP spare = CreateBitmap(320, 900, 1, 32, nullptr);
    HGDIOBJ previous;
    Fixture() {
        WNDCLASSA cls{};
        cls.lpfnWndProc = WindowProc;
        cls.hInstance = GetModuleHandleA(nullptr);
        cls.lpszClassName = "ModelUiNavigationTest";
        RegisterClassA(&cls);
        window = CreateWindowExA(0, cls.lpszClassName, "", WS_POPUP, 0, 0, 900, 900, nullptr,
                                 nullptr, cls.hInstance, nullptr);
        if (!window || !panel || !bitmap || !spare)
            std::abort();
        previous = SelectObject(panel, bitmap);
        std::memset(&app->state, 0, sizeof app->state);
        app->MainWindow() = window;
        app->Renderer() = renderer.get();
        app->PanelDC() = panel;
        app->state.bmpPanelSpare = spare;
        renderer->screenHeight = 900;
        app->state.slotIdx = mds::kModelSlotCount - 1;
        app->ModelSlot(app->state.slotIdx) = model.get();
        app->CameraParentModel() = -1;
        mds::g_Block = app.get();
        model->boneCount = static_cast<int>(bones.size());
        model->morphCount = static_cast<int>(morphRows.size());
        model->boneTable = bones.data();
        model->boneSelection = selected.data();
        model->bonePhysicsState = edited.data();
        model->boneTimelineRows = boneRows.data();
        model->morphTimelineRows = morphRows.data();
        model->morphs = morphs.data();
        model->displayRootBone = 5;
        model->displayGroups = groups.data();
        model->groupCount = static_cast<unsigned char>(groups.size());
        model->displayBones = entries.data();
        model->displayBoneCount = static_cast<unsigned>(entries.size());
        model->displayFrames = faces.data();
        model->facialFrameCount = static_cast<unsigned char>(faces.size());
        model->selectedBone = -1;
        for (int i = 0; i < 6; ++i) {
            sprintf_s(bones[i].name, "bone%d", i);
            sprintf_s(bones[i].nameEn, "english%d", i);
            edited[i] = static_cast<unsigned char>(i + 2);
        }
        for (int i = 0; i < 7; ++i) {
            sprintf_s(groups[i].name, "group%d", i);
            sprintf_s(groups[i].nameEn, "frame%d", i);
            groups[i].flags = i != 1 && i != 4;
        }
        const int targets[] = {1, 2, 3, 4, 0}, frames[] = {3, 3, 4, 6, 6};
        for (int i = 0; i < 5; ++i) {
            entries[i].targetIndex = static_cast<std::uint16_t>(targets[i]);
            entries[i].groupIndex = static_cast<char>(frames[i]);
            sprintf_s(entries[i].name, "entry%d", targets[i]);
            sprintf_s(entries[i].nameEn, "display%d", targets[i]);
        }
        faces[0].targetIndex = 1;
        faces[1].targetIndex = 0;
        for (int i = 0; i < 2; ++i) {
            faces[i].groupIndex = 1;
            sprintf_s(faces[i].name, "morph%d", i);
        }
        Child(window, mds::panel::kTimelineVScroll, "SCROLLBAR", SBS_VERT);
        labels.clear();
        scrolls.clear();
        paints = 0;
        boxActiveAtPaint = 0;
    }
    ~Fixture() {
        SelectObject(panel, previous);
        DeleteObject(bitmap);
        DeleteObject(spare);
        DeleteDC(panel);
        DestroyWindow(window);
        mds::g_Block = nullptr;
    }
    void Sweep() {
        labels.clear();
        mds::RebuildTimelineTrackPanel(app.get());
    }
};
bool LabelAt(const char* text, int row) {
    for (const auto& label : labels)
        if (label.text == text && label.y == 14 * row + 17)
            return true;
    return false;
}
void TestRowsAndNavigation() {
    Fixture f;
    f.selected[5] = f.selected[3] = 1;
    f.faces[0].selected = 1;
    f.Sweep();
    Check(f.model->boneListRows == 13 && f.model->boneListRowRecord[0] == 5,
          "root is pinned above display group, morph and bone rows");
    Check(f.boneRows[1] == 6 && f.boneRows[2] == 7 && f.boneRows[3] == -8 && f.boneRows[4] == 11 &&
              f.boneRows[0] == 12,
          "expanded rows are positive and collapsed bone headers are negative");
    Check(f.morphRows[1] == 3 && f.morphRows[0] == 4 && f.model->boneListRowRecord[3] == -2 &&
              f.model->boneListRowRecord[4] == -1,
          "morph record order is distinct from morph index and negative bone-row encoding");
    Check(f.app->PanelRowFlags()[0] == 1 && f.app->PanelRowFlags()[3] == 1 &&
              f.app->PanelRowFlags()[8] == 1 && f.model->boneListRowRecord[13] == -999,
          "selected root, morph and collapsed bone header flags survive row reset");
    Check(LabelAt("bone5", 0) && LabelAt("entry1", 6), "labels use display root and entry names");
    f.app->EnglishUI() = 1;
    f.Sweep();
    Check(LabelAt("english5", 0) && LabelAt("display1", 6), "English names keep the same rows");
    f.groups[2].flags = 0;
    f.Sweep();
    Check(f.morphRows[0] == -2 && f.morphRows[1] == -2 && f.app->PanelRowFlags()[2] == 1,
          "collapsed morphs share their selected header");
    f.groups[2].flags = 1;
    f.model->boneListPos = 3;
    f.Sweep();
    Check(f.boneRows[1] == 3 && f.boneRows[3] == -5 && f.morphRows[1] == 0 && f.morphRows[0] == 1 &&
              f.model->boneListRowRecord[0] == 5,
          "scrolling preserves the root and clears maps for rows above the viewport");
    f.model->boneListPos = 0;
    mds::SelectNextDisplayBone(f.app.get());
    Check(f.model->selectedBone == 5, "no selection navigates to the display root");
    mds::SelectNextDisplayBone(f.app.get());
    Check(f.model->selectedBone == 1, "root navigates to first expanded bone group");
    mds::SelectNextDisplayBone(f.app.get());
    Check(f.model->selectedBone == 2, "next stays in display record order");
    mds::SelectNextDisplayBone(f.app.get());
    Check(f.model->selectedBone == 4, "next skips collapsed and empty groups");
    mds::SelectPreviousDisplayBone(f.app.get());
    Check(f.model->selectedBone == 4,
          "original previous navigation stops at an empty expanded group");
    f.groups[5].flags = 0;
    mds::SelectPreviousDisplayBone(f.app.get());
    Check(f.model->selectedBone == 2, "previous chooses the last bone in preceding expanded group");
    mds::SelectPreviousDisplayBone(f.app.get());
    mds::SelectPreviousDisplayBone(f.app.get());
    Check(f.model->selectedBone == 5 && f.selected[5] == 1 && f.selected[1] == 0 &&
              f.edited[5] == 7 && f.model->undoDirty == 0,
          "navigation changes selection without recording a pose edit or undo");
    f.model->selectedBone = 65537;
    mds::SelectNextDisplayBone(f.app.get());
    mds::SelectPreviousDisplayBone(f.app.get());
    Check(f.model->selectedBone == 65537,
          "navigation compares the full bone index without wrapping to a 16-bit display entry");
}
void TestScroll() {
    Fixture f;
    SetWindowPos(f.window, nullptr, 0, 0, 900, 450, SWP_NOACTIVATE | SWP_NOZORDER);
    f.Sweep();
    mds::ScrollDisplayBoneIntoView(f.app.get(), 2);
    Check(scrolls == std::vector<unsigned>(5, SB_LINEDOWN) && f.model->boneListPos == 5,
          "auto-scroll counts expanded morph rows and sends individual line messages");
    scrolls.clear();
    f.model->boneListPos = 8;
    mds::ScrollDisplayBoneIntoView(f.app.get(), 1);
    Check(scrolls == std::vector<unsigned>(3, SB_LINEUP) && f.model->boneListPos == 5,
          "auto-scroll moves upwards from the current model scroll position");
    scrolls.clear();
    mds::ScrollDisplayBoneIntoView(f.app.get(), 3);
    mds::ScrollDisplayBoneIntoView(f.app.get(), 5);
    mds::ScrollDisplayBoneIntoView(f.app.get(), -1);
    mds::ScrollDisplayBoneIntoView(f.app.get(), 65538);
    Check(scrolls.empty(), "collapsed bones, root and negative indices do not auto-scroll");
    mds::RefreshTimelineScrollbar(f.app.get(), 450);
    Check(f.app->state.scrollNPage == 2 && f.app->state.scrollNMax == 12 &&
              f.app->state.scrollNPos == 5 &&
              f.app->state.scrollFMask == (SIF_RANGE | SIF_PAGE | SIF_POS),
          "paint refresh publishes the original scrollbar range, page and thumb baseline");
    const LPARAM bar = reinterpret_cast<LPARAM>(GetDlgItem(f.window, mds::panel::kTimelineVScroll));
    mds::HandleVScroll(bar, MAKEWPARAM(SB_THUMBTRACK, 7));
    Check(f.model->boneListPos == 7,
          "thumb tracking applies a delta against the published position");
    mds::HandleVScroll(bar, SB_PAGEUP);
    Check(f.model->boneListPos == 5, "page up uses the shared page size");
    f.model->boneListPos = 100;
    mds::HandleVScroll(bar, SB_ENDSCROLL);
    Check(f.model->boneListPos == 11, "ignored scroll codes still clamp and rebuild the list");
    f.model->groupCount = 1;
    f.Sweep();
    mds::HandleVScroll(bar, SB_LINEUP);
    Check(f.model->boneListPos == -1,
          "root-only list preserves the original lower-then-upper clamp order");
}
void TestRowCapacity() {
    Fixture f;
    std::vector<mdl::BoneRecord> bones(206);
    std::vector<mdl::FrameGroup> entries(205);
    std::vector<unsigned char> selected(206);
    std::vector<std::int32_t> rows(206, 12345);
    for (int i = 0; i < 205; ++i) {
        entries[i].targetIndex = static_cast<std::uint16_t>(i);
        entries[i].groupIndex = 3;
    }
    f.model->boneCount = 206;
    f.model->boneTable = bones.data();
    f.model->boneSelection = selected.data();
    f.model->boneTimelineRows = rows.data();
    f.model->displayRootBone = 205;
    f.model->displayBones = entries.data();
    f.model->displayBoneCount = 205;
    f.model->groupCount = 4;
    f.model->facialFrameCount = 0;
    f.Sweep();
    Check(f.model->boneListRows == 209 && f.model->boneListRowRecord[199] == 195 &&
              rows[195] == 199 && rows[196] == 0 && rows[204] == 0,
          "list counts all entries while limiting row maps and records to 200 visible rows");
    f.model->boneListPos = 206;
    f.Sweep();
    Check(rows[203] == 1 && rows[204] == 2 && rows[195] == 0 &&
              f.model->boneListRowRecord[0] == 205,
          "last bone entries become visible after scrolling without losing the root");
}
void TestAccessoryParent() {
    Fixture f;
    mdl::AccessoryRecord accessory{};
    accessory.parentModel = mds::kModelSlotCount - 1;
    accessory.parentBone = 4;
    accessory.scale = 1;
    f.app->AccessorySlot(17) = &accessory;
    f.app->state.selectedObjectSlot = 17;
    f.model->comboSelIndex = 2;
    HWND modelCombo = Child(f.window, mds::panel::kMainComboGround, "COMBOBOX", CBS_DROPDOWNLIST);
    HWND boneCombo = Child(f.window, mds::panel::kAttachBoneCombo, "COMBOBOX", CBS_DROPDOWNLIST);
    for (const char* name : {"ground", "other", "parent"})
        Add(modelCombo, name);
    SendMessageA(modelCombo, CB_SETCURSEL, 0, 0);
    f.bones[1].type = mdl::BoneType::InertTip;
    f.bones[4].type = mdl::BoneType::FixedAxis;
    for (int id = mds::panel::kAccPosXEdit; id <= mds::panel::kAccOpacityEdit; ++id)
        Child(f.window, id, "EDIT");
    for (int id : {mds::panel::kAccessoryVisibleCheckbox, mds::panel::kAccessoryAddBlendCheckbox,
                   mds::panel::kAccessoryShadowCheckbox})
        Child(f.window, id, "BUTTON", BS_CHECKBOX);
    const std::uint32_t angleBits = 0x3C1D9A9B;
    std::memcpy(&accessory.rotation[0], &angleBits, sizeof angleBits);
    mds::SyncAccessoryEditPanel(f.app.get());
    Check(SendMessageA(modelCombo, CB_GETCURSEL, 0, 0) == 2 &&
              SendMessageA(boneCombo, CB_GETCOUNT, 0, 0) == 5 &&
              SendMessageA(boneCombo, CB_GETCURSEL, 0, 0) == 3,
          "accessory parent uses model order, filters bone types and finds the parent by name");
    Add(boneCombo, "sentinel");
    mds::SyncAccessoryEditPanel(f.app.get());
    Check(SendMessageA(boneCombo, CB_GETCOUNT, 0, 0) == 6,
          "unchanged parent model order preserves the existing bone list");
    char text[64]{};
    GetDlgItemTextA(f.window, mds::panel::kAccRotXEdit, text, sizeof text);
#if defined(_M_X64)
    const char* expected = "0.5511";
#else
    const char* expected = "0.5512";
#endif
    if (std::strcmp(text, expected) != 0)
        std::fprintf(stderr, "accessory angle: expected %s, got %s\n", expected, text);
    Check(std::strcmp(text, expected) == 0,
          "accessory rotation readout matches original conversion");
    accessory.parentModel = -1;
    mds::SyncAccessoryEditPanel(f.app.get());
    Check(SendMessageA(modelCombo, CB_GETCURSEL, 0, 0) == 0 &&
              SendMessageA(boneCombo, CB_GETCOUNT, 0, 0) == 0,
          "ground parent clears the bone list");
    Add(boneCombo, "unchanged ground");
    mds::SyncAccessoryEditPanel(f.app.get());
    Check(SendMessageA(boneCombo, CB_GETCOUNT, 0, 0) == 1,
          "unchanged ground parent does not repeatedly clear the bone list");
}
void TestAccessoryRows() {
    Fixture f;
    mdl::AccessoryRecord first{}, last{};
    strcpy_s(first.name, "first slot");
    strcpy_s(last.name, "last slot");
    first.order = 2;
    last.order = 0;
    last.rowSelected = 1;
    f.app->AccessorySlot(0) = &first;
    f.app->AccessorySlot(254) = &last;
    f.app->CameraMode() = 1;
    f.app->EnglishUI() = 1;
    f.app->GlobalTrackSelected(mds::GlobalTimelineTrack::Light) = 1;
    f.Sweep();
    Check(f.app->DisplayObjectListMatchCount() == 3 && f.app->state.jointLineMap[0] == 254 &&
              f.app->state.jointLineMap[1] == 0 && f.app->state.jointLineMap[2] == -1 &&
              f.app->PanelRowFlags()[1] == 1 && f.app->PanelRowFlags()[4] == 1 &&
              LabelAt("last slot", 4),
          "accessory rows follow display order across all slots below the four global tracks");
    f.app->DisplayObjectListScrollPosition() = 1;
    f.Sweep();
    Check(f.app->state.jointLineMap[0] == 0 && f.app->state.jointLineMap[1] == -1 &&
              f.app->PanelRowFlags()[4] == 0,
          "accessory scroll gate uses display order and clears stale row selection flags");
    mds::RefreshTimelineScrollbar(f.app.get(), 450);
    Check(f.app->state.scrollNPage == 2 && f.app->state.scrollNMax == 3 &&
              f.app->state.scrollNPos == 1,
          "accessory scrollbar range uses the accessory match count and its own position");
    mds::HandleVScroll(reinterpret_cast<LPARAM>(GetDlgItem(f.window, mds::panel::kTimelineVScroll)),
                       SB_PAGEUP);
    Check(f.app->DisplayObjectListScrollPosition() == 0,
          "accessory page scroll updates accessory state independently of the model list");
}
void TestBoxReleaseRefresh() {
    Fixture f;
    f.selected[2] = 1;
    f.model->selectedBone = 1;
    f.app->BoneBoxSelectionActive() = 1;
    f.app->LeftMouseButtonState() = 2;
    mds::BeginOrEndViewportToolDrag(f.app.get(), mds::ViewportToolAction::None);
    Check(f.model->selectedBone == 2 && f.app->BoneBoxSelectionActive() == 0 && paints == 1 &&
              boxActiveAtPaint == 0,
          "box release chooses the sole selected bone before refreshing the timeline panel");
    mds::BeginOrEndViewportToolDrag(f.app.get(), mds::ViewportToolAction::None);
    Check(paints == 1, "repeated release does not refresh an already finished box");
}

void TestNameSelectionRefresh() {
    Fixture f;
    f.app->SidebarWidth() = 250;
    f.app->state.themeColors[33] = RGB(12, 34, 56);
    f.app->state.themeColors[34] = RGB(231, 45, 67);
    f.model->selectedBone = -1;
    f.morphs[0].panel = mdl::MorphPanel::eyebrow;
    f.morphs[1].panel = mdl::MorphPanel::eye;
    strcpy_s(f.faces[0].nameEn, "expression0");
    strcpy_s(f.faces[1].nameEn, "expression1");
    const auto click = [&](int row, bool shift = false) {
        f.app->ShiftModifierState() = shift ? 3 : 0;
        f.app->MouseX() = 30;
        f.app->MouseY() = 160 + row * 14 + 7;
        labels.clear();
        mds::HandleLButtonDown(f.app.get());
    };
    const auto color = [&](const char* text, COLORREF expected) {
        // Header clicks repaint once before their toggle and again after it;
        // only the last draw determines the color left in the panel bitmap.
        for (auto it = labels.rbegin(); it != labels.rend(); ++it)
            if (it->text == text)
                return it->color == expected;
        return false;
    };
    for (int english : {0, 1}) {
        f.app->EnglishUI() = english;
        f.selected.fill(0);
        f.faces[0].selected = f.faces[1].selected = 0;
        f.Sweep();
        const char* bone = english ? "display1" : "entry1";
        const char* other = english ? "display2" : "entry2";
        const char* root = english ? "english5" : "bone5";
        click(f.boneRows[1]);
        Check(f.selected[1] == 1 && color(bone, f.app->state.themeColors[34]),
              "clicking a bone immediately redraws its selected name color");
        click(f.boneRows[2], true);
        Check(f.selected[1] && f.selected[2] && color(bone, f.app->state.themeColors[34]) &&
                  color(other, f.app->state.themeColors[34]),
              "Shift selection refreshes both bone names without clearing the first");
        click(f.boneRows[1], true);
        Check(!f.selected[1] && f.selected[2] && color(bone, f.app->state.themeColors[33]) &&
                  color(other, f.app->state.themeColors[34]),
              "Shift deselection immediately restores the ordinary name color");
        click(f.morphRows[1]);
        Check(f.faces[0].selected && !f.selected[2] &&
                  color(english ? "expression0" : "morph0", f.app->state.themeColors[34]) &&
                  color(other, f.app->state.themeColors[33]),
              "clicking a morph redraws its name and clears the previous bone highlight");
        click(f.morphRows[1], true);
        Check(!f.faces[0].selected &&
                  color(english ? "expression0" : "morph0", f.app->state.themeColors[33]),
              "deselecting a morph immediately restores its ordinary name color");
        click(0);
        Check(f.selected[5] && color(root, f.app->state.themeColors[34]),
              "the pinned root uses the same immediate selection refresh");
        click(5); // expanded bone group 3
        Check(f.selected[1] && f.selected[2] && !f.selected[5] &&
                  color(bone, f.app->state.themeColors[34]) &&
                  color(other, f.app->state.themeColors[34]),
              "the common refresh preserves a group header's single selection toggle");
        click(5, true);
        Check(!f.selected[1] && !f.selected[2] && color(bone, f.app->state.themeColors[33]),
              "a second group click deselects its bones exactly once");
        click(2); // morph header
        Check(f.faces[0].selected && f.faces[1].selected &&
                  color(english ? "expression0" : "morph0", f.app->state.themeColors[34]),
              "the morph header still selects and redraws its children");
        click(1); // display / IK / OP header
        Check(f.model->displayKeyframesPresent && !f.faces[0].selected && f.app->PanelRowFlags()[1],
              "the display header retains its original selection toggle and highlight");
    }
}

void TestSelectedBoneStatus() {
    Fixture f;
    f.app->SidebarWidth() = 250;
    f.app->state.hideTop = 90;
    f.app->state.hideBottom = 700;
    strcpy_s(f.model->name, "\x83\x7e\x83\x4e"); // Miku, CP932
    strcpy_s(f.model->nameEn, "Miku");
    strcpy_s(f.bones[1].name, "\x8f\xe3\x94\xbc\x90\x67"); // upper body, CP932
    strcpy_s(f.bones[1].nameEn, "upper body");
    const auto title = [&](const std::string& expected, bool playing = false) {
        labels.clear();
        mds::PaintViewportStatus(f.panel);
        Check(!labels.empty() && labels.front().text == expected &&
                  labels.front().x == f.app->SidebarWidth() + 19 &&
                  labels.front().y ==
                      f.app->state.hideTop - ((f.app->EnglishUI() || playing) ? 22 : 20) &&
                  labels.front().color == RGB(255, 255, 255),
              "status painter draws the original localized model/bone title");
    };
    const auto click = [&](int row) {
        f.app->MouseX() = 30;
        f.app->MouseY() = 160 + row * 14 + 7;
        mds::HandleLButtonDown(f.app.get());
    };
    for (int english : {0, 1}) {
        f.app->EnglishUI() = english;
        f.model->selectedBone = -1;
        f.model->boneTable = nullptr;
        const std::string model = english ? f.model->nameEn : f.model->name;
        // Native JP format bytes at EXE RVA 0x12D17C: 25 73 81 46 25 73 00.
        const char* separator = english ? " : " : "\x81\x46";
        title(model);
        f.model->boneTable = f.bones.data();
        f.Sweep();
        click(f.boneRows[1]);
        Check(f.model->selectedBone == 1, "timeline click selects the status title's bone");
        title(model + separator + (english ? f.bones[1].nameEn : f.bones[1].name));
        click(f.boneRows[2]);
        title(model + separator + (english ? f.bones[2].nameEn : f.bones[2].name));
        click(0);
        title(model + separator + (english ? f.bones[5].nameEn : f.bones[5].name));
        f.app->PlaybackActive() = 1;
        title(english ? "Playing" : "\x8d\xc4\x90\xb6\x92\x86", true);
        f.app->PlaybackActive() = 0;
    }
}
} // namespace

namespace mikudancestudio {
// Keep the real list construction and Win32 controls; observe only final drawing.
void PanelPaint(MMDApp* app) {
    ++paints;
    boxActiveAtPaint = app->BoneBoxSelectionActive();
}
int DrawGlyph(MMDApp*, const char* text, HDC, int, int x, int y, unsigned char r, unsigned char g,
              unsigned char b, int) {
    labels.push_back({text, x, y, RGB(r, g, b)});
    return 0;
}
} // namespace mikudancestudio

int main() {
    TestRowsAndNavigation();
    TestScroll();
    TestRowCapacity();
    TestAccessoryParent();
    TestAccessoryRows();
    TestBoxReleaseRefresh();
    TestNameSelectionRefresh();
    TestSelectedBoneStatus();
    return failures ? 1 : 0;
}
