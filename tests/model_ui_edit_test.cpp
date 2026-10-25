#include "mikudancestudio/d3dx_dyn.hpp"
#include "mikudancestudio/global_key_layout.hpp"
#include "mikudancestudio/mmd_app.hpp"
#include "mikudancestudio/model.hpp"
#include "mikudancestudio/panel_controls.hpp"
#include "mikudancestudio/ported_funcs.hpp"

#include <commctrl.h>
#include <array>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <memory>
#include <vector>

namespace mds = mikudancestudio;
namespace mdl = mikudancestudio::mdl;
namespace {
int failures, paints;
void Check(bool ok, const char* message) {
    if (!ok) {
        std::fprintf(stderr, "FAIL: %s\n", message);
        ++failures;
    }
}
HWND Child(HWND parent, int id, const char* cls = "EDIT", DWORD style = 0) {
    HWND window = CreateWindowExA(0, cls, "", WS_CHILD | style, 0, 0, 1, 1, parent,
                                  reinterpret_cast<HMENU>(static_cast<INT_PTR>(id)),
                                  GetModuleHandleA(nullptr), nullptr);
    if (!window)
        std::abort();
    return window;
}
void Add(HWND combo, const char* text) {
    SendMessageA(combo, CB_ADDSTRING, 0, reinterpret_cast<LPARAM>(text));
}
void Pick(HWND parent, int id, int index) {
    SendDlgItemMessageA(parent, id, CB_SETCURSEL, index, 0);
}
bool TextIs(HWND parent, int id, const char* expected) {
    char text[100]{};
    GetDlgItemTextA(parent, id, text, sizeof text);
    const bool equal = std::strcmp(text, expected) == 0;
    if (!equal)
        std::fprintf(stderr, "control %d: expected %s, got %s\n", id, expected, text);
    return equal;
}
bool NumberNear(HWND parent, int id, float expected) {
    char text[100]{};
    GetDlgItemTextA(parent, id, text, sizeof text);
    return std::fabs(std::atof(text) - expected) < 0.00002;
}
bool PositionNear(const float* actual, float x, float y, float z) {
    return std::fabs(actual[0] - x) < 0.00002f && std::fabs(actual[1] - y) < 0.00002f &&
           std::fabs(actual[2] - z) < 0.00002f;
}
struct Fixture {
    std::unique_ptr<mds::MMDApp> app = std::make_unique<mds::MMDApp>();
    std::unique_ptr<mdl::ModelRecord> model = std::make_unique<mdl::ModelRecord>();
    std::array<mdl::BoneRecord, 3> bones{};
    std::array<unsigned char, 3> selected{}, edited{};
    std::array<std::int32_t, 3> heads{0, 1, 2};
    std::vector<mdl::BoneKey> keys{mdl::kBoneKeyCapacity};
    std::vector<mdl::MorphKey> morphKeys{mdl::kMorphKeyCapacity};
    std::vector<mdl::DisplayKey> displayKeys{mdl::kDisplayKeyCapacity};
    std::array<mdl::MorphRecord, 5> morphs{};
    std::array<mdl::FrameGroup, 4> faces{};
    HWND window = CreateWindowExA(0, "STATIC", "", WS_POPUP, 0, 0, 900, 900, nullptr, nullptr,
                                  GetModuleHandleA(nullptr), nullptr);
    HBITMAP bitmap = CreateBitmap(128, 128, 1, 32, nullptr);
    HDC dc = CreateCompatibleDC(nullptr);
    HGDIOBJ previous;
    Fixture() {
        if (!window || !bitmap || !dc)
            std::abort();
        std::memset(&app->state, 0, sizeof app->state);
        previous = SelectObject(dc, bitmap);
        app->CurveDC() = dc;
        app->MainWindow() = window;
        app->SidebarWidth() = 300;
        app->state.slotIdx = 7;
        app->ModelSlot(7) = model.get();
        app->CameraParentModel() = -1;
        mds::g_Block = app.get();
        model->hwnd = window;
        model->boneCount = 3;
        model->boneTable = bones.data();
        model->boneSelection = selected.data();
        model->bonePhysicsState = edited.data();
        model->boneTimelineRows = heads.data();
        model->boneKeys = keys.data();
        model->morphKeys = morphKeys.data();
        model->displayKeys = displayKeys.data();
        model->morphs = morphs.data();
        model->displayFrames = faces.data();
        model->selectedBone = 2;
        model->UndoCursor() = model->LatestUndoCursor() = mdl::kUndoHistoryDepth - 1;
        for (int i = 0; i < 3; ++i) {
            bones[i].parent = -1;
            bones[i].rotQuat[3] = bones[i].ikBackup[6] = keys[i].rotation[3] = 1;
            bones[i].trans[0] = 10.0f + i;
            edited[i] = static_cast<unsigned char>(i + 2);
            bones[i].matInit[0] = bones[i].matInit[5] = bones[i].matInit[10] =
                bones[i].matInit[15] = 1;
        }
        for (int id = mds::panel::kReadoutPosXEdit; id <= mds::panel::kReadoutDistEdit; ++id)
            Child(window, id);
        for (int id :
             {mds::panel::kCurveCopyButton, mds::panel::kCurvePasteButton,
              mds::panel::kCurveLinearButton, mds::panel::kUndoButton, mds::panel::kRedoButton})
            Child(window, id, "BUTTON");
        HWND combo = Child(window, mds::panel::kInterpCurveCombo, "COMBOBOX", CBS_DROPDOWNLIST);
        for (int i = 0; i < 7; ++i)
            Add(combo, "channel");
        Pick(window, mds::panel::kInterpCurveCombo, 0);
        paints = 0;
    }
    ~Fixture() {
        app->ReleaseTimelineSelectionRecords();
        delete[] app->SelectNavRecords();
        app->SelectNavRecords() = nullptr;
        ::operator delete(app->AccessoryEditArray());
        app->AccessoryEditArray() = nullptr;
        for (auto& ring : model->undoRings)
            for (auto& slot : ring.slots) {
                ::operator delete(slot.bonePose);
                ::operator delete(slot.keySnapshots);
            }
        SelectObject(dc, previous);
        DeleteObject(bitmap);
        DeleteDC(dc);
        DestroyWindow(window);
        mds::g_Block = nullptr;
    }
    mdl::UndoRecord& Undo() { return model->undoRings[0].slots[model->UndoCursor()]; }
    void Click(int x, int y) {
        app->MouseX() = x;
        app->MouseY() = y;
        mds::HandleLButtonDown(app.get());
    }
};
}

namespace mikudancestudio {
// Exercise the real edit/selection/undo code without requiring a D3D device.
// Only the final timeline paint is replaced by an observable sink.
void PanelPaint(MMDApp*) {
    ++paints;
}
}

namespace {
void TestCurves() {
    Fixture f;
    std::vector<mdl::CameraKey> camera{mdl::kTimelineKeyCapacity};
    f.app->CameraKeys() = camera.data();
    for (bool cameraMode : {true, false}) {
        f.app->CameraMode() = cameraMode;
        const int channels = cameraMode ? 6 : 4;
        for (auto point :
             {mds::InterpolationControlPoint::First, mds::InterpolationControlPoint::Second}) {
            for (int channel : {2, channels}) {
                camera.assign(camera.size(), {});
                std::fill(f.keys.begin(), f.keys.end(), mdl::BoneKey{});
                camera.front().selected = camera.back().selected = 1;
                f.keys.front().allocated = f.keys.back().allocated = 1;
                f.app->DraggedInterpolationPoint() = point;
                Pick(f.window, mds::panel::kInterpCurveCombo, channel);
                f.app->MouseX() = 48;
                f.app->MouseY() = 822; // y = 900 - 822 - 8 = 70
                mds::DragInterpolationControlPoint(f.app.get());
                for (int c = 0; c < channels; ++c) {
                    const int group = point == mds::InterpolationControlPoint::First ? 0 : 2;
                    const bool changed = channel == channels || channel == c;
                    for (int g = 0; g < 4; ++g) {
                        const int expected = changed && g == group       ? 40
                                             : changed && g == group + 1 ? 70
                                                                         : 0;
                        if (cameraMode) {
                            Check(
                                camera.front().interpolation[g][c] == expected &&
                                    camera.back().interpolation[g][c] == expected &&
                                    camera[1].interpolation[g][c] == 0,
                                "camera drag respects group, channel, selection and final pool slot");
                        } else {
                            Check(
                                f.keys.front().interpolation[g * 4 + c] == expected &&
                                    f.keys.back().interpolation[g * 4 + c] == expected &&
                                    f.keys[1].interpolation[g * 4 + c] == 0,
                                "bone drag respects group, channel, selection and final pool slot");
                        }
                    }
                }
                mds::SelectionReeval(f.app.get());
                Check(IsWindowEnabled(GetDlgItem(f.window, mds::panel::kCurveCopyButton)),
                      "matching selected curves enable copying after repaint");
                if (cameraMode)
                    camera.back().interpolation[0][2] = 17;
                else
                    f.keys.back().interpolation[2] = 17;
                mds::SelectionReeval(f.app.get());
                Check(!IsWindowEnabled(GetDlgItem(f.window, mds::panel::kCurveCopyButton)),
                      "different curves in final pool slot disable copying");
            }
        }
    }
    f.app->CameraMode() = 1;
    camera.assign(camera.size(), {});
    auto& last = camera.back();
    last.frame = 123;
    last.fov = 38;
    last.parentModel = 7;
    last.selected = 1;
    last.interpolation[2][2] = 200;
    last.interpolation[3][2] = 240;
    const auto before = last;
    Pick(f.window, mds::panel::kInterpCurveCombo, 2);
    f.app->DraggedInterpolationPoint() = mds::InterpolationControlPoint::First;
    f.app->MouseX() = -50;
    f.app->MouseY() = 999;
    mds::DragInterpolationControlPoint(f.app.get());
    Check(last.interpolation[0][2] == 0 && last.interpolation[1][2] == 0 &&
              f.app->state.interpCurveControlCache[2] == 200 &&
              f.app->state.interpCurveControlCache[3] == 143,
          "curve drag clamps at zero and reads the opposite point as signed bytes");
    f.app->DraggedInterpolationPoint() = mds::InterpolationControlPoint::Second;
    f.app->MouseX() = 999;
    f.app->MouseY() = -50;
    mds::DragInterpolationControlPoint(f.app.get());
    Check(last.interpolation[2][2] == 127 && last.interpolation[3][2] == 127,
          "curve drag clamps both coordinates at 127");
    auto actual = last;
    std::memcpy(actual.interpolation, before.interpolation, sizeof actual.interpolation);
    Check(std::memcmp(&actual, &before, sizeof before) == 0,
          "camera curve drag preserves every non-interpolation field");
    last.selected = 0;
    mds::SelectionReeval(f.app.get());
    Check(!IsWindowEnabled(GetDlgItem(f.window, mds::panel::kCurveLinearButton)) &&
              !IsWindowEnabled(GetDlgItem(f.window, mds::panel::kCurvePasteButton)),
          "empty curve selection disables linear and preset application");
    f.app->CameraKeys() = nullptr;
}

void TestTimelineUndo() {
    Fixture f;
    f.keys[1].next = 4;
    f.keys[4].previous = 1;
    f.keys[2].next = 6;
    f.keys[6].previous = 2;
    f.keys[4].frame = 20;
    f.keys[6].frame = 50;
    f.keys[4].rotation[3] = f.keys[6].rotation[3] = 1;
    f.keys[4].allocated = f.keys[6].allocated = 1;
    f.displayKeys[0].visible = 1;
    f.displayKeys[0].next = 1;
    f.displayKeys[1].frame = 30;
    f.displayKeys[1].allocated = 1;
    f.morphKeys.back().frame = 80;
    f.morphKeys.back().allocated = 1;
    f.app->state.rowHitBone[200] = 4;
    f.Click(120, 170);
    Check(f.app->TimelineSelectionCount(mds::TimelineSelectionBand::ModelBone) == 2 &&
              f.app->TimelineSelectionCount(mds::TimelineSelectionBand::ModelDisplay) == 1 &&
              f.app->TimelineSelectionCount(mds::TimelineSelectionBand::ModelMorph) == 1 &&
              f.app->TimelineSelectionChanged() == 1 && paints == 1,
          "clicking an already selected bone key captures the bone drag band");
    Check(f.Undo().operation == mdl::UndoOperation::BoneKeys && f.Undo().recordCount == 2,
          "drag snapshots selected key slots once before changing frames");
    for (int i = 0; i < 3; ++i)
        Check(f.Undo().bonePose[i].boneIndex == i &&
                  f.Undo().bonePose[i].position[0] == 10.0f + i &&
                  f.Undo().bonePose[i].poseEdited == i + 2,
              "drag undo retains each bone index, pose and independent edit marker");
    mds::HandleMouseMove(133, 170);
    Check(f.keys[4].frame == 21 && f.keys[6].frame == 51 && f.displayKeys[1].frame == 31 &&
              f.morphKeys.back().frame == 81 && f.model->maxFrame == 81 &&
              f.app->LastRegisteredFrame() == 81 && paints == 2 && f.model->UndoCursor() == 0,
          "mouse drag remaps selected keys and maxima without creating a second undo record");
    for (int i = 0; i < 3; ++i) {
        f.bones[i].trans[0] = 900;
        f.edited[i] = 9;
    }
    std::int32_t frame = 99;
    mds::UndoModelEdit(f.model.get(), frame);
    for (int i = 0; i < 3; ++i)
        Check(
            f.bones[i].trans[0] == 10.0f + i && f.edited[i] == i + 2,
            "undoing timeline drag restores all distinct bones rather than overwriting bone zero");
    Check(f.keys[4].frame == 20 && f.keys[6].frame == 50,
          "timeline drag undo restores both key frames");
}

void TestMorphRows() {
    Fixture f;
    f.model->facialFrameCount = 4;
    const int combos[] = {mds::panel::kMorphCombo0, mds::panel::kMorphCombo1,
                          mds::panel::kMorphCombo2, mds::panel::kMorphCombo3};
    const int sliders[] = {mds::panel::kMorphSlider0, mds::panel::kMorphSlider1,
                           mds::panel::kMorphSlider2, mds::panel::kMorphSlider3};
    const int edits[] = {mds::panel::kMorphEdit0, mds::panel::kMorphEdit1, mds::panel::kMorphEdit2,
                         mds::panel::kMorphEdit3};
    for (int lane = 0; lane < 4; ++lane) {
        auto& morph = f.morphs[lane + 1];
        morph.panel = static_cast<mdl::MorphPanel>(lane + 1);
        strcpy_s(morph.name, "jp");
        strcpy_s(morph.nameEn, "english");
        morph.value = 0.25f;
        f.faces[lane].targetIndex = lane + 1;
        HWND combo = Child(f.window, combos[lane], "COMBOBOX", CBS_DROPDOWNLIST);
        Add(combo, "other");
        Add(combo, lane % 2 == 0 ? "jp" : "english");
        Child(f.window, sliders[lane], TRACKBAR_CLASSA);
        Child(f.window, edits[lane]);
        f.selected[0] = 1;
        f.model->boneListRowRecord[0] = -2 - lane;
        f.Click(30, 170);
        Check(f.faces[lane].selected == 1 && f.model->selectedMorphs[lane] == lane + 1 &&
                  f.selected[0] == 0 && f.edited[0] == 2 && f.model->UndoCursor() == 29 &&
                  SendDlgItemMessageA(f.window, combos[lane], CB_GETCURSEL, 0, 0) == 1 &&
                  SendDlgItemMessageA(f.window, sliders[lane], TBM_GETPOS, 0, 0) == 25 &&
                  TextIs(f.window, edits[lane], "0.2500"),
              "ordinary morph row selects its panel and synchronizes JP/EN combo, slider and edit");
    }
    f.model->boneListRowRecord[0] = -999;
    f.Click(30, 170);
    Check(f.faces[3].selected == 0 && f.model->selectedMorphs[3] == 4,
          "empty row sentinel clears selection without selecting morph 998");
}

void TestNumericCommitAndReadout() {
    Fixture f;
    f.selected[0] = f.selected[2] = 1;
    for (int axis = 0; axis < 3; ++axis) {
        const int id = mds::panel::kReadoutPosXEdit + axis;
        SetDlgItemTextA(f.window, id, "42.5");
        const float before = f.bones[2].trans[axis];
        mds::CommitEditControlTail(f.app.get(), GetDlgItem(f.window, id));
        Check(f.bones[2].trans[axis] == 42.5f && f.edited[2] == 1 && f.edited[0] == 2 &&
                  f.Undo().operation == mdl::UndoOperation::BonePose && f.Undo().recordCount == 2 &&
                  f.Undo().bonePose[0].boneIndex == 0 && f.Undo().bonePose[1].boneIndex == 2 &&
                  f.Undo().bonePose[1].position[axis] == before &&
                  f.Undo().bonePose[1].poseEdited == 4,
              "numeric commit snapshots selected bones before changing only the active bone");
        std::int32_t frame = 0;
        mds::UndoModelEdit(f.model.get(), frame);
        Check(f.bones[2].trans[axis] == before && f.edited[2] == 4,
              "numeric commit undo restores the pose and pending edit marker");
    }
    for (int axis = 0; axis < 3; ++axis) {
        mds::PostViewRefresh(f.app.get());
        const int id = mds::panel::kReadoutRotXEdit + axis;
        SetDlgItemTextA(f.window, id, "90");
        mds::CommitEditControlTail(f.app.get(), GetDlgItem(f.window, id));
        const float sign = axis == 0 ? 1.0f : -1.0f;
        Check(
            std::fabs(f.bones[2].rotQuat[axis] - sign * 0.70710678f) < 0.000001f &&
                std::fabs(f.bones[2].rotQuat[3] - 0.70710678f) < 0.000001f && f.edited[2] == 1 &&
                f.Undo().bonePose[1].rotation[3] == 1,
            "rotation commit preserves Euler axis signs and snapshots before quaternion composition");
        std::int32_t frame = 0;
        mds::UndoModelEdit(f.model.get(), frame);
        Check(f.bones[2].rotQuat[3] == 1 && f.bones[2].rotQuat[axis] == 0 && f.edited[2] == 4,
              "rotation undo restores the original quaternion and edit marker");
    }
    f.app->PlaybackPhysicsMode() = 3;
    f.bones[2].ikBackup[0] = 72;
    for (int flag : {0, 1, 2, 3}) {
        f.bones[2].hasRigidBody = static_cast<unsigned char>(flag);
        mds::PostViewRefresh(f.app.get());
        Check(TextIs(f.window, mds::panel::kReadoutPosXEdit, flag & 1 ? "72.00" : "12.00"),
              "physics readout uses the original raw bit gate and correct backup pose");
    }
    f.app->CameraMode() = 1;
    const std::uint32_t angleBits = 0x3C00ADFB;
    float angle;
    std::memcpy(&angle, &angleBits, sizeof angle);
    f.app->CameraPitch() = -angle;
    f.app->CameraYaw() = f.app->CameraRoll() = angle;
    mds::PostViewRefresh(f.app.get());
#if defined(_M_X64)
    const char* expected = "0.4"; // Original divss/mulss, not double arithmetic.
#else
    const char* expected = "0.5"; // Original x87 result goes straight to printf's double slot.
#endif
    Check(TextIs(f.window, mds::panel::kReadoutRotXEdit, expected),
          "camera pitch retains each architecture's division and float store");
    Check(TextIs(f.window, mds::panel::kReadoutRotYEdit, expected) &&
              TextIs(f.window, mds::panel::kReadoutRotZEdit, expected),
          "camera readouts retain each architecture's original rounding stages");
}

void TestAuxiliaryReset() {
    for (int axis = 0; axis < 3; ++axis) {
        Fixture f;
        f.selected[0] = f.selected[2] = 1;
        const float position[3] = {12, 23, 34};
        std::memcpy(f.bones[2].trans, position, sizeof position);
        mds::DispatchSeparateWindowCommand(f.app.get(), mds::panel::kResetPositionXButton + axis);
        Check(f.bones[2].trans[axis] == 0 && f.edited[2] == 1 && f.edited[0] == 2,
              "auxiliary position reset edits only the selected bone and axis");
        Check(f.Undo().operation == mdl::UndoOperation::BonePose && f.Undo().recordCount == 2 &&
                  f.Undo().bonePose[1].boneIndex == 2 && f.Undo().bonePose[1].poseEdited == 4 &&
                  std::memcmp(f.Undo().bonePose[1].position, position, sizeof position) == 0,
              "position reset snapshots all selected bones before changing the pose edit flag");
    }
    // Inputs and expected radians were obtained by replaying the original
    // SSE mulss/divss and x87 double-constant/store sequences independently.
#if defined(_M_X64)
    const std::uint32_t expected[3][3] = {
        {0, 0xBE5CA4D5, 0x3FDCA4DF}, {0x3F2EBECB, 0, 0x3FDCA4DF}, {0x3F2EBECB, 0xBE5CA4D5, 0}};
#else
    const std::uint32_t expected[3][3] = {
        {0, 0xBE5CA4D6, 0x3FDCA4E0}, {0x3F2EBECB, 0, 0x3FDCA4E0}, {0x3F2EBECB, 0xBE5CA4D6, 0}};
#endif
    for (int axis = 0; axis < 3; ++axis) {
        Fixture f;
        f.selected[2] = 1;
        f.app->BoneRotationEditDegreesX() = 39.11f;
        f.app->BoneRotationEditDegreesY() = 12.34567f;
        f.app->BoneRotationEditDegreesZ() = -98.76543f;
        float radians[3];
        std::memcpy(radians, expected[axis], sizeof radians);
        auto& api = mds::d3dx::Get();
        mds::d3dx::D3DXMATRIXF z{}, x{}, y{}, matrix{};
        api.rotZ(&z, radians[2]);
        api.rotX(&x, radians[0]);
        api.rotY(&y, radians[1]);
        api.multiply(&matrix, &z, &x);
        api.multiply(&matrix, &matrix, &y);
        float quaternion[4];
        api.quatFromMatrix(quaternion, &matrix);
        mds::DispatchSeparateWindowCommand(f.app.get(), mds::panel::kResetRotationXButton + axis);
        Check(std::memcmp(f.bones[2].rotQuat, quaternion, sizeof quaternion) == 0,
              "rotation reset retains both original pi coefficients, signs and Z-X-Y order");
        Check(f.edited[2] == 1 && f.Undo().bonePose[0].rotation[3] == 1 &&
                  f.Undo().bonePose[0].poseEdited == 4,
              "rotation reset records the prior quaternion and edit flag before writing");
    }
    Fixture f;
    f.model->selectedBone = -1;
    mds::DispatchSeparateWindowCommand(f.app.get(), mds::panel::kResetRotationXButton);
    Check(f.model->UndoCursor() == mdl::kUndoHistoryDepth - 1 &&
              TextIs(f.window, mds::panel::kReadoutRotXEdit, "-----"),
          "reset with no selected bone only refreshes readouts");
}

void TestTimelineDoubleClick() {
    Fixture f;
    Child(f.window, mds::panel::kCurrentFrameEdit, "EDIT", ES_AUTOHSCROLL);
    f.app->state.currentFrame = 17;
    f.app->state.timelineStartFrame = 10;
    f.app->MouseX() = 126;
    f.app->MouseY() = 150;
    f.model->hwnd = f.window;
    for (auto& bone : f.bones)
        bone.slotIndex = -1;
    auto last = std::make_unique<mdl::ModelRecord>();
    mdl::BoneRecord lastBone{};
    mdl::BoneKey lastKey{};
    mdl::DisplayKey lastDisplay{};
    unsigned char lastEdited = 9;
    lastBone.rotQuat[3] = lastKey.rotation[3] = 1;
    lastBone.slotIndex = -1;
    lastBone.trans[0] = 99;
    last->boneCount = 1;
    last->boneTable = &lastBone;
    last->boneKeys = &lastKey;
    last->displayKeys = &lastDisplay;
    last->bonePhysicsState = &lastEdited;
    last->UndoCursor() = last->LatestUndoCursor() = mdl::kUndoHistoryDepth - 1;
    f.app->ModelSlot(mds::kModelSlotCount - 1) = last.get();
    const auto oldSelected = f.selected;
    mds::HandleLButtonDblClk(f.app.get());
    Check(f.app->CurrentFrame() == 12 && TextIs(f.window, mds::panel::kCurrentFrameEdit, "12"),
          "timeline double click computes and echoes the clicked frame");
    Check(f.Undo().operation == mdl::UndoOperation::BonePoseWithFrame && f.Undo().frame == 17 &&
              f.Undo().recordCount == 3 && f.Undo().bonePose[2].poseEdited == 4 &&
              f.Undo().bonePose[2].position[0] == 12 && f.edited[2] == 0 &&
              f.selected == oldSelected,
          "frame seek snapshots the old frame and edit flags before clearing edits");
    const auto& lastUndo = last->undoRings[0].slots[last->UndoCursor()];
    Check(lastUndo.operation == mdl::UndoOperation::BonePoseWithFrame && lastUndo.frame == 17 &&
              lastUndo.bonePose[0].position[0] == 99 && lastUndo.bonePose[0].poseEdited == 9 &&
              lastEdited == 0 && lastBone.trans[0] == 0,
          "both snapshot and seek include the last model slot");
    Check(f.app->PhysicsResetPending() == 1 && f.app->TimelineStartFrame() == 4 &&
              IsWindowEnabled(GetDlgItem(f.window, mds::panel::kUndoButton)) &&
              !IsWindowEnabled(GetDlgItem(f.window, mds::panel::kRedoButton)),
          "double click recenters the timeline and updates physics and undo controls");
    const int cursor = f.model->UndoCursor();
    mds::HandleLButtonDblClk(f.app.get());
    Check(f.model->UndoCursor() == cursor, "clean poses do not create another frame-change undo");
    f.app->PlaybackActive() = 1;
    f.app->MouseX() = 139;
    mds::HandleLButtonDblClk(f.app.get());
    Check(f.app->CurrentFrame() == 6, "playback blocks a subsequent double-click seek");
    f.app->ModelSlot(mds::kModelSlotCount - 1) = nullptr;
    for (auto& ring : last->undoRings)
        for (auto& undo : ring.slots)
            ::operator delete(undo.bonePose);
}

void TestBindingDialog() {
    Fixture f;
    auto other = std::make_unique<mdl::ModelRecord>();
    std::array<mdl::BoneRecord, 3> target{};
    other->boneTable = target.data();
    other->boneCount = 3;
    f.app->ModelSlot(mds::kModelSlotCount - 1) = other.get();
    f.model->comboSelIndex2 = 1;
    other->comboSelIndex2 = 0;
    strcpy_s(f.model->name, "current");
    strcpy_s(f.model->nameEn, "current-en");
    strcpy_s(other->name, "target");
    strcpy_s(other->nameEn, "target-en");
    for (int i = 0; i < 3; ++i) {
        target[i].type = i == 1 ? mdl::BoneType::InertTip : mdl::BoneType::FixedAxis;
        strcpy_s(target[i].name, "target-bone");
        strcpy_s(target[i].nameEn, "target-bone-en");
        target[i].matInit[0] = target[i].matInit[5] = target[i].matInit[10] =
            target[i].matInit[15] = 1;
        std::memcpy(target[i].matExtra, target[i].matInit, sizeof target[i].matInit);
    }
    // Noncommuting 90-degree rotations and translations give exact integer
    // position goldens, independently calculated with row-vector matrices.
    const float current[16] = {0, 1, 0, 0, -1, 0, 0, 0, 0, 0, 1, 0, 10, 20, 30, 1};
    const float parent[16] = {1, 0, 0, 0, 0, 0, 1, 0, 0, -1, 0, 0, 4, 5, 6, 1};
    const float preview[16] = {0, 0, -1, 0, 0, 1, 0, 0, 1, 0, 0, 0, 7, 11, 13, 1};
    const float applied[16] = {0, -1, 0, 0, 1, 0, 0, 0, 0, 0, 1, 0, 2, 3, 5, 1};
    std::memcpy(f.bones[2].matInit, current, sizeof current);
    std::memcpy(f.bones[0].matInit, parent, sizeof parent);
    std::memcpy(target[2].matInit, preview, sizeof preview);
    std::memcpy(target[2].matExtra, applied, sizeof applied);
    const float position[] = {1, 2, 3}, targetPosition[] = {6, 8, 10};
    std::memcpy(f.bones[2].position, position, sizeof position);
    std::memcpy(target[2].position, targetPosition, sizeof targetPosition);
    mdl::BoneOrderEntry bindings[] = {
        {-1, 0, 0, -1, 0}, {0, 0, 0, -1, 0}, {2, 0, 0, mds::kModelSlotCount - 1, 2}};
    f.model->boneOrderTable = bindings;
    f.model->boneOrderCount = 3;
    HWND mainCombo = Child(f.window, mds::panel::kMainComboModel, "COMBOBOX", CBS_DROPDOWNLIST);
    for (const char* text : {"camera", "target", "current"})
        Add(mainCombo, text);
    HWND dialog = Child(f.window, 1900, "STATIC");
    for (int id :
         {mds::panel::kBoneNameCombo, mds::panel::kMorphNameCombo, mds::panel::kGroupNameCombo})
        Child(dialog, id, "COMBOBOX", CBS_DROPDOWNLIST);
    for (int id = 688; id <= 693; ++id)
        Child(dialog, id);
    Child(dialog, mds::panel::kOrderUpButton, "BUTTON");
    for (int english : {0, 1}) {
        f.app->state.englishUI = english;
        mds::InitSelectNavDialog(f.app.get(), dialog);
        Check(
            f.app->DialogOrders().modelIndices[0] == mds::kModelSlotCount - 1 &&
                f.app->DialogOrders().modelIndices[1] == 7 &&
                SendDlgItemMessageA(dialog, mds::panel::kBoneNameCombo, CB_GETCURSEL, 0, 0) == 2 &&
                SendDlgItemMessageA(dialog, mds::panel::kMorphNameCombo, CB_GETCURSEL, 0, 0) == 2 &&
                SendDlgItemMessageA(dialog, mds::panel::kGroupNameCombo, CB_GETCOUNT, 0, 0) == 2 &&
                SendDlgItemMessageA(dialog, mds::panel::kGroupNameCombo, CB_GETCURSEL, 0, 0) == 1,
            "binding dialog maps display order, last model slot and filtered bone indices");
    }
    Pick(dialog, mds::panel::kGroupNameCombo, 0);
    mds::CommitTargetBonePick(f.app.get(), dialog);
    Check(f.app->SelectNavRecords()[2].linkedBone == 0,
          "target combo commits the mapped bone index");
    Pick(dialog, mds::panel::kGroupNameCombo, 1);
    mds::CommitTargetBonePick(f.app.get(), dialog);
    // The expected cross-model preview offset is computed independently below.
    Check(NumberNear(dialog, 688, -26) && NumberNear(dialog, 689, 2) && NumberNear(dialog, 690, -9),
          "binding preview preserves matrix order and uses target matInit");
    f.app->CurrentFrame() = 20;
    mds::ApplyBoneAttach(f.app.get(), dialog);
    Check(PositionNear(f.bones[2].trans, -24, -2, 18) &&
              std::fabs(std::fabs(f.bones[2].rotQuat[2]) - 1) < 0.000001f &&
              std::fabs(f.bones[2].rotQuat[3]) < 0.000001f,
          "binding apply preserves matrix order and uses target matExtra");
    Check(
        f.selected[0] == 0 && f.selected[1] == 0 && f.selected[2] == 1 && f.edited[2] == 0 &&
            f.edited[0] == 2 && f.model->maxFrame == 20 && f.app->LastRegisteredFrame() == 20 &&
            f.app->AccessoryApplyGate() == 1 && paints == 1,
        "binding apply selects and registers only its bone before refreshing and setting the apply gate");
    Pick(dialog, mds::panel::kMorphNameCombo, 0);
    f.bones[2].parent = 0;
    mds::RebuildTargetBoneList(f.app.get(), dialog, 0);
    Check(f.app->SelectNavRecords()[2].linkedModel == -1 && NumberNear(dialog, 688, 3) &&
              NumberNear(dialog, 689, 25) && NumberNear(dialog, 690, -19),
          "root preview uses the parent bone index and inverse parent matrix");
    mds::ApplyBoneAttach(f.app.get(), dialog);
    Check(PositionNear(f.bones[2].trans, 3, 25, -19),
          "root apply retains parent-relative matrix order");
    Pick(dialog, mds::panel::kMorphNameCombo, 1);
    mds::RebuildTargetBoneList(f.app.get(), dialog, 0);
    Check(f.app->SelectNavRecords()[2].linkedModel == -2 && NumberNear(dialog, 688, 7) &&
              NumberNear(dialog, 689, 19) && NumberNear(dialog, 690, 30),
          "ground preview keeps its sentinel and uses the bone matrix directly");
    mds::ApplyBoneAttach(f.app.get(), dialog);
    Check(PositionNear(f.bones[2].trans, 7, 19, 30), "ground apply keeps the bone matrix directly");
    f.bones[2].parent = -1;
    Pick(dialog, mds::panel::kMorphNameCombo, 0);
    mds::RebuildTargetBoneList(f.app.get(), dialog, 0);
    mds::ApplyBoneAttach(f.app.get(), dialog);
    Check(PositionNear(f.bones[2].trans, 7, 19, 30) &&
              std::memcmp(f.bones[2].matInit, current, sizeof current) == 0,
          "parentless root uses the bone matrix without mutating the source matrix");
    // This rotation separates float divss/mulss from an x87 double expression
    // even after formatting to six decimals (original dialog's precision).
    const float roundingMatrix[16] = {
        0.75f, 0.6614378094673157f, 0, 0, -0.6614378094673157f, 0.75f, 0, 0, 0, 0, 1, 0, 0, 0, 0,
        1};
    std::memcpy(f.bones[2].matInit, roundingMatrix, sizeof roundingMatrix);
    mds::RefreshSelectNavDisplay(f.app.get(), dialog);
#if defined(_M_X64)
    const char* expectedAngle = "-41.409626";
#else
    const char* expectedAngle = "-41.409630";
#endif
    Check(TextIs(dialog, 693, expectedAngle),
          "binding angle readout keeps original architecture-specific rounding");
    const float epsilonMatrix[16] = {1, 0,          0, 0, 0, 1, 0.000001f, 0,
                                     0, -0.000001f, 1, 0, 0, 0, 0,         1};
    std::memcpy(f.bones[2].matInit, epsilonMatrix, sizeof epsilonMatrix);
    mds::RefreshSelectNavDisplay(f.app.get(), dialog);
    Check(TextIs(dialog, 691, "0.000057"),
          "binding angle equal to the float epsilon is retained on both architectures");
    const float gimbalMatrix[16] = {1, 1.3e-15f, 1.3e-7f, 0, -1.3e-7f, 1e-8f, 1, 0,
                                    0, -1,       1e-8f,   0, 0,        0,     0, 1};
    std::memcpy(f.bones[2].matInit, gimbalMatrix, sizeof gimbalMatrix);
    mds::RefreshSelectNavDisplay(f.app.get(), dialog);
    Check(TextIs(dialog, 693, "-180.000015"),
          "binding gimbal correction adds the original float pi before converting to degrees");
    f.model->boneOrderTable = nullptr;
}
}

int main() {
    INITCOMMONCONTROLSEX controls{sizeof controls, ICC_BAR_CLASSES};
    if (!InitCommonControlsEx(&controls))
        return 1;
    TestCurves();
    TestTimelineUndo();
    TestMorphRows();
    TestNumericCommitAndReadout();
    TestAuxiliaryReset();
    TestTimelineDoubleClick();
    TestBindingDialog();
    return failures ? 1 : 0;
}
