// Frame-driver viewport input (original x86 sub_46B090, x64 sub_7FF7CB4474F0).
// Poll key edges, update picking/box selection, then consume operation-panel
// drags. ViewportDragMode describes the second-stage drag, not the tool hit
// code in ViewportToolAction. Undo snapshots belong to the press edge;
// box-selection readouts and the active bone are finalized on release.
#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include "mikudancestudio/panel_controls.hpp"
#include <Windows.h>

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>
#include <cstdio>

#include "mikudancestudio/d3dx_dyn.hpp"
#include "mikudancestudio/globals.hpp"
#include "mikudancestudio/mmd_app.hpp"
#include "mikudancestudio/model.hpp"
#include "mikudancestudio/ported_funcs.hpp"
#include "frame_state_dump.hpp"
#include "bone_selection.hpp"

namespace mikudancestudio {

// Exact doubles from the original image.  These stay double until the final
// store, matching the x87 instruction stream at 0x470E4B..0x470EDF.
double g_MouseScaleA = 0.5;                  // VA 0x0052B8F0
double g_MouseScaleB = 0.004999999888241291; // VA 0x0052E9C0
double g_MouseScaleC = 0.05000000074505806;  // VA 0x0052D738
float g_MouseScaleD = 0.0020000000949949f;   // VA 0x0052E8C0 (true
                                             // .rdata double; the old
                                             // PI/360 guess was wrong)

static void ViewRefreshGate(MMDApp* s) {
    // Original dword gates (Ghidra: this+0x9ED98 select-state gate,
    // this+0xA0430 frame cursor).  The old literals 650136/657456 were
    // decimal slips - 650136 landed inside aviOutputPath and 657456 inside
    // exeDir.  0x9ED98 is a dword over followCameraEnabled and the two shadow bytes;
    // 0x2F8 is a dword over the first four radio flags.
    const bool frameGate =
        (s->state.followCameraEnabled | s->state.playbackStartsAtCurrentFrame |
         s->state.projectedShadowBlendEnabled) != 0 &&
        s->state.slotIdx == s->state.cameraParentModel &&
        (s->CameraMode() | s->PanelExpanded(UiPanel::Camera) | s->PanelExpanded(UiPanel::Light) |
         s->PanelExpanded(UiPanel::Accessory)) == 0 &&
        s->state.cameraParentModel >= 0 && s->state.playbackActive == 0;
    if (frameGate) {
        // 0xA05D1 = viewDirty view-dirty byte (the old literal 658257 was a
        // decimal slip that landed on the English-name dialog HWND's byte 1)
        s->state.viewDirty = 1; // 0xA05D1
        s->state.windowLayoutReady = 0;
        InvalidateSelectionReadouts(s); // 0x40D070
        s->state.windowLayoutReady = 1;
    }
    PostViewRefresh(s); // 0x40D130
}

namespace {

void PollKey(MMDApp* app, std::int32_t& keyState, int virtualKey) {
    const std::int32_t oldState = keyState;
    const bool down = (GetKeyState(virtualKey) & 0x8000) != 0;
    std::int32_t state = 0;
    if (down)
        state = oldState == 0 ? 1 : 3;
    else if (oldState == 1 || oldState == 3)
        state = 2;
    keyState = state;
    if (state == 1 || state == 2)
        app->state.messageSeen = 1;
}

float DragScale(MMDApp* app) {
    if (app->ShiftModifierActive())
        return 0.5f; // flt_52960C / dbl_52B8F0
    if (app->CtrlModifierActive())
        return 0.005f; // flt_529604 / dbl_52E9C0
    return 0.05f;      // flt_52E9A4 / dbl_52D738
}

void PanCameraPosition(MMDApp* app, int dx, int dy) {
    const float scale = DragScale(app);
    auto& api = d3dx::Get();
    d3dx::D3DXMATRIXF mx{}, my{}, mz{}, matrix{};
    api.rotY(&my, app->CameraYaw());
    api.rotX(&mx, app->CameraPitch());
    api.multiply(&matrix, &mx, &my);
    api.rotZ(&mz, -app->CameraRoll());
    api.multiply(&matrix, &mz, &matrix);

    const float sx = static_cast<float>(-dx) * scale;
    const float sy = static_cast<float>(dy) * scale;
    app->CameraPositionX() += sx * matrix.m[0][0] + sy * matrix.m[1][0] + matrix.m[3][0];
    app->CameraPositionY() += sx * matrix.m[0][1] + sy * matrix.m[1][1] + matrix.m[3][1];
    app->CameraPositionZ() -= sx * matrix.m[0][2] + sy * matrix.m[1][2] + matrix.m[3][2];
}

// PanSelectedCameraKey (parented-camera-key MMB pan) moved to
// pump_navigation.cpp with the rest of the x86 0x470BF5..0x47133D block.

ViewportToolAction ViewportToolAtPoint(MMDApp* app) {
    app->ViewportToolHovered() = 0;
    app->ViewportToolOperation() = ViewportToolAction::None;
    if (app->ViewportInputActive() == 0)
        return ViewportToolAction::None;

    const int x = app->MouseX();
    const int y = app->MouseY();
    const RECT view = app->ViewportRect();
    HWND window = app->FloatingWindow();
    if (window == nullptr)
        window = app->state.hwnd;
    RECT client{};
    GetClientRect(window, &client);

    ViewportToolAction operation = ViewportToolAction::None;
    if (y > view.top - 25 && y < view.top) {
        if (x > client.right - 60 && x < client.right - 36)
            operation = ViewportToolAction::CameraOrbit;
        else if (x > client.right - 30 && x < client.right - 6)
            operation = ViewportToolAction::CameraPan;
    }

    float scale = 1.0f;
    if (D3DRenderer* r = app->Renderer())
        scale = r->viewScale; // wrapper+120048
    if (!(scale > 0.0f))
        scale = 1.0f;

    if (operation == ViewportToolAction::None && app->PlaybackActive() == 0) {
        const float fx = static_cast<float>(x);
        const float fy = static_cast<float>(y);
        const float x0 = view.right - 127.0f * scale;
        const float x1 = view.right - 92.0f * scale;
        const float x2 = view.right - 86.0f * scale;
        const float x3 = view.right - 51.0f * scale;
        const float x4 = view.right - 48.0f * scale;
        const float x5 = view.right - 13.0f * scale;
        const float upperTop = view.bottom - 80.0f * scale;
        const float upperBottom = view.bottom - 50.0f * scale;
        const float lowerTop = view.bottom - 40.0f * scale;
        const float lowerBottom = view.bottom - 10.0f * scale;
        int column = -1;
        if (fx > x0 && fx < x1)
            column = 0;
        else if (fx > x2 && fx < x3)
            column = 1;
        else if (fx > x4 && fx < x5)
            column = 2;
        if (column >= 0) {
            const bool modelPanel = app->CameraMode() != 0;
            const std::uint32_t axisMode = app->state.v32c;
            if (fy > upperTop && fy < upperBottom) {
                if (modelPanel)
                    operation = std::array{ViewportToolAction::AngleAdjustX,
                                           ViewportToolAction::AngleAdjustY,
                                           ViewportToolAction::AngleAdjustZ}[column];
                else if (axisMode == 0 || axisMode == 1)
                    operation = std::array{ViewportToolAction::TransformAxisX,
                                           ViewportToolAction::TransformAxisY,
                                           ViewportToolAction::TransformAxisZ}[column];
            } else if (fy > lowerTop && fy < lowerBottom) {
                if (modelPanel)
                    operation = std::array{ViewportToolAction::CameraAdjustX,
                                           ViewportToolAction::CameraAdjustY,
                                           ViewportToolAction::CameraAdjustZ}[column];
                else if (axisMode == 0 || axisMode == 2)
                    operation = std::array{ViewportToolAction::PhysicsAxisX,
                                           ViewportToolAction::PhysicsAxisY,
                                           ViewportToolAction::PhysicsAxisZ}[column];
            }
        }

        if (operation == ViewportToolAction::None && app->EditMode() == ViewportEditMode::Rotate) {
            const int cx = app->ViewportToolCenterX();
            const int cy = app->ViewportToolCenterY();
            const int px = x - cx;
            const int py = y - cy;
            const bool horizontal =
                ((px > 7 && px < 35) || (px > -35 && px < -7)) && py > -7 && py < 7;
            const bool vertical =
                ((py > 7 && py < 35) || (py > -35 && py < -7)) && px > -7 && px < 7;
            if (horizontal)
                operation = ViewportToolAction::BoneRotateHorizontal;
            else if (vertical)
                operation = ViewportToolAction::BoneRotateVertical;
            else {
                const int radius2 = px * px + py * py;
                if (radius2 > 1225 && radius2 < 2025)
                    operation = ViewportToolAction::BoneRotateRing;
            }
        } else if (operation == ViewportToolAction::None &&
                   app->EditMode() == ViewportEditMode::Move) {
            const int px = x - app->ViewportToolCenterX();
            const int py = y - app->ViewportToolCenterY();
            if (px > 41 && px < 61 && py > -8 && py < 8)
                operation = ViewportToolAction::BoneMoveHorizontalHandle;
            else if (px > -13 && px < 8 && py > -61 && py < -42)
                operation = ViewportToolAction::BoneMoveVerticalHandle;
            else if (px > -13 && px < 11 && py > -16 && py < 8)
                operation = ViewportToolAction::BoneMovePlaneHandle;
        }

        if (operation == ViewportToolAction::None &&
            app->InteractionDragMode() == ViewportDragMode::None) {
            const float coordLeft = view.right - 120.0f * scale;
            const float coordTop = view.bottom - 105.0f * scale;
            if (fx > coordLeft && fx < coordLeft + 25.0f * scale && fy > coordTop &&
                fy < coordTop + 25.0f * scale)
                operation = ViewportToolAction::CycleCoordinateSystem;
        }
    }

    if (operation != ViewportToolAction::None) {
        app->ViewportToolHovered() = 1;
        app->ViewportToolOperation() = operation;
    }
    return operation;
}

HWND ViewportWindow(MMDApp* app) {
    HWND window = app->FloatingWindow();
    if (window == nullptr)
        window = app->state.hwnd;
    return window;
}

float ViewportScale(MMDApp* app) {
    D3DRenderer* r = app->Renderer();
    const float scale = r != nullptr ? r->viewScale : 1.0f; // +120048
    return scale > 0.0f ? scale : 1.0f;
}

void WarpCursor(MMDApp* app, POINT point) {
    HWND window = ViewportWindow(app);
    if (window == nullptr)
        return;
    ClientToScreen(window, &point);
    SetCursorPos(point.x, point.y);
    app->state.separateWindowMouseSeen = 1;
}

void BeginCenteredDrag(MMDApp* app, ViewportToolAction operation) {
    const RECT view = app->ViewportRect();
    POINT point{(view.left + view.right) / 2, (view.top + view.bottom) / 2};
    app->MouseX() = point.x;
    app->MouseY() = point.y;
    app->PreviousMouseX() = point.x;
    app->PreviousMouseY() = point.y;
    app->ViewToolDragOperation() = operation;
    app->state.mouseJumped = 0;
    WarpCursor(app, point);
    while (ShowCursor(FALSE) >= 0) {}
}

void RestoreViewportToolCursor(MMDApp* app, ViewportToolAction operation) {
    const RECT view = app->ViewportRect();
    const float scale = ViewportScale(app);
    POINT point{};
    if (operation == ViewportToolAction::CameraOrbit ||
        operation == ViewportToolAction::CameraPan) {
        HWND mainWindow = app->state.hwnd;
        RECT client{};
        GetClientRect(mainWindow, &client);
        point.x = client.right - (operation == ViewportToolAction::CameraOrbit ? 48 : 18);
        point.y = view.top - 12;
    } else {
        const int column = operation == ViewportToolAction::TransformAxisX ||
                                   operation == ViewportToolAction::PhysicsAxisX ||
                                   operation == ViewportToolAction::CameraAdjustX ||
                                   operation == ViewportToolAction::AngleAdjustX
                               ? 0
                           : operation == ViewportToolAction::TransformAxisY ||
                                   operation == ViewportToolAction::PhysicsAxisY ||
                                   operation == ViewportToolAction::CameraAdjustY ||
                                   operation == ViewportToolAction::AngleAdjustY
                               ? 1
                               : 2;
        const bool upper = operation == ViewportToolAction::TransformAxisX ||
                           operation == ViewportToolAction::TransformAxisY ||
                           operation == ViewportToolAction::TransformAxisZ ||
                           operation == ViewportToolAction::AngleAdjustX ||
                           operation == ViewportToolAction::AngleAdjustY ||
                           operation == ViewportToolAction::AngleAdjustZ;
        static constexpr float kCenterX[3] = {110.0f, 70.0f, 30.0f};
        point.x = view.right - static_cast<int>(kCenterX[column] * scale);
        point.y = view.bottom - static_cast<int>((upper ? 65.0f : 25.0f) * scale);
    }
    WarpCursor(app, point);
    while (ShowCursor(TRUE) < 0) {}
}

mdl::ModelRecord* ActiveBoneModel(MMDApp* app) {
    return app->SelectedModel();
}

void PickBoneAtCursor(MMDApp* app) {
    auto* model = ActiveBoneModel(app);
    if (model) {
        PickBoneAtPoint(*model, app->MouseX(), app->MouseY(), app->PlaybackPhysicsMode(),
                        app->ShiftModifierActive());
        // The original refreshes the panel even when the click misses.
        RebuildTimelineTrackPanel(app);
        InvalidateSelectionReadouts(app);
    }
}

void UpdateBoneBoxSelection(MMDApp* app) {
    if (app->BoneBoxSelectionActive() == 0)
        return;
    mdl::ModelRecord* model = ActiveBoneModel(app);
    if (model == nullptr)
        return;
    const int left = std::min(app->BoneBoxStartX(), app->MouseX());
    const int right = std::max(app->BoneBoxStartX(), app->MouseX());
    const int top = std::min(app->BoneBoxStartY(), app->MouseY());
    const int bottom = std::max(app->BoneBoxStartY(), app->MouseY());
    SelectBonesInScreenRect(*model, left, top, right, bottom, app->ShiftModifierActive());
    // Selection bytes update while held; the panel refresh belongs to release.
}

} // namespace

void BeginOrEndViewportToolDrag(MMDApp* app, ViewportToolAction operation) {
    const int leftState = app->LeftMouseButtonState();
    if (leftState == 1) {
        if (operation == ViewportToolAction::CameraOrbit ||
            operation == ViewportToolAction::CameraPan) {
            BeginCenteredDrag(app, operation);
            return;
        }
        static constexpr ViewportDragMode kDragMode[] = {ViewportDragMode::None,
                                                         ViewportDragMode::None,
                                                         ViewportDragMode::None,
                                                         ViewportDragMode::ViewAxisRotateX,
                                                         ViewportDragMode::ViewAxisRotateY,
                                                         ViewportDragMode::ViewAxisRotateZ,
                                                         ViewportDragMode::BoneMoveHorizontal,
                                                         ViewportDragMode::BoneMoveVertical,
                                                         ViewportDragMode::BoneMoveScreenPlane,
                                                         ViewportDragMode::AxisRotateX,
                                                         ViewportDragMode::AxisRotateY,
                                                         ViewportDragMode::AxisRotateZ,
                                                         ViewportDragMode::PhysicsAxisX,
                                                         ViewportDragMode::PhysicsAxisY,
                                                         ViewportDragMode::PhysicsAxisZ,
                                                         ViewportDragMode::CameraAdjustX,
                                                         ViewportDragMode::CameraAdjustY,
                                                         ViewportDragMode::CameraAdjustZ,
                                                         ViewportDragMode::AngleAdjustX,
                                                         ViewportDragMode::AngleAdjustY,
                                                         ViewportDragMode::AngleAdjustZ};
        if (operation >= ViewportToolAction::BoneRotateHorizontal &&
            operation <= ViewportToolAction::AngleAdjustZ) {
            const ViewportDragMode mode = kDragMode[static_cast<int>(operation)];
            if (operation >= ViewportToolAction::TransformAxisX)
                BeginCenteredDrag(app, operation);
            app->InteractionDragMode() = mode;
            if (operation == ViewportToolAction::BoneRotateRing) {
                app->ViewportToolDragOriginX() = app->ViewportToolCenterX();
                app->ViewportToolDragOriginY() = app->ViewportToolCenterY();
            }
            if (operation <= ViewportToolAction::BoneMovePlaneHandle) {
                app->EditMode() = ViewportEditMode::ToolDrag;
                PushBoneEditUndo(app);
            } else if (operation <= ViewportToolAction::PhysicsAxisZ) {
                PushBoneEditUndo(app);
                app->state.a06B5 = 1;
            } else {
                const int target = app->state.coordinateSystem;
                if (target == 2)
                    SelectAccessoryTimelineTrack(app->state.selectedObjectSlot);
                else
                    SelectTimelineTrack(GlobalTimelineTrack::Camera);
                app->state.a06B5 = 1;
            }
        } else if (operation == ViewportToolAction::CycleCoordinateSystem) {
            int& target = app->state.coordinateSystem;
            const int limit = app->CameraMode() != 0 ? 3 : 2;
            if (++target >= limit)
                target = 0;
            RebuildTimelineTrackPanel(app);
        } else if (operation == ViewportToolAction::None && app->PlaybackActive() == 0) {
            const RECT view = app->ViewportRect();
            const int x = app->MouseX();
            const int y = app->MouseY();
            if (x > view.left && x < view.right && y > view.top && y < view.bottom) {
                const ViewportEditMode panelMode = app->EditMode();
                if (panelMode == ViewportEditMode::Bone)
                    PickBoneAtCursor(app);
                else if (panelMode == ViewportEditMode::BoneBox) {
                    app->BoneBoxStartX() = x;
                    app->BoneBoxStartY() = y;
                    app->BoneBoxSelectionActive() = 1;
                }
            }
        }
    } else if (leftState == 2) {
        app->DraggedInterpolationPoint() = InterpolationControlPoint::None;
        const ViewportToolAction viewOperation = app->ViewToolDragOperation();
        const ViewportDragMode mode = app->InteractionDragMode();
        if (viewOperation == ViewportToolAction::CameraOrbit ||
            viewOperation == ViewportToolAction::CameraPan) {
            RestoreViewportToolCursor(app, viewOperation);
            app->ViewToolDragOperation() = ViewportToolAction::None;
        } else if (mode != ViewportDragMode::None) {
            if (mode >= ViewportDragMode::ViewAxisRotateX &&
                mode <= ViewportDragMode::ViewAxisRotateZ)
                app->EditMode() = ViewportEditMode::Rotate;
            else if (mode >= ViewportDragMode::BoneMoveHorizontal &&
                     mode <= ViewportDragMode::BoneMoveScreenPlane)
                app->EditMode() = ViewportEditMode::Move;
            if (viewOperation >= ViewportToolAction::TransformAxisX &&
                viewOperation <= ViewportToolAction::AngleAdjustZ) {
                RestoreViewportToolCursor(app, viewOperation);
                app->ViewToolDragOperation() = ViewportToolAction::None;
            }
            app->InteractionDragMode() = ViewportDragMode::None;
            app->state.a06B5 = 0;
        } else if (app->BoneBoxSelectionActive() != 0) {
            app->BoneBoxSelectionActive() = 0;
            if (auto* model = app->SelectedModel()) {
                FinishBoneBoxSelection(*model, app->ShiftModifierActive());
                RebuildTimelineTrackPanel(app);
                InvalidateSelectionReadouts(app);
            }
        }
    }
}

bool PickBoneAtPoint(mdl::ModelRecord& model, int x, int y, int physicsMode, bool additive) {
    if (!model.boneTable || !model.boneSelection || model.boneCount == 0)
        return false;
    int first = -1;
    int candidate = -1;
    bool changed = false;
    for (std::uint32_t i = 0; i < model.boneCount; ++i) {
        const mdl::BoneRecord& bone = model.boneTable[i];
        // Original x64 0x7FF7CB451B92..0x7FF7CB451BFE: the physics
        // gate reads hasRigidBody, independently of the bone type byte.
        if (!(bone.flags & mdl::kBoneFlagVisible) ||
            !(static_cast<std::int8_t>(bone.type) < static_cast<int>(mdl::BoneType::InertTip) ||
              bone.type == mdl::BoneType::FixedAxis))
            continue;
        if (bone.hasRigidBody && (physicsMode == 1 || (physicsMode == 2 && !bone.physicsDisabled)))
            continue;
        const int dx = bone.screenX - x;
        const int dy = bone.screenY - y;
        if (dx * dx + dy * dy >= 64)
            continue;
        if (additive) {
            model.boneSelection[i] = model.boneSelection[i] == 0 ? 1 : 0;
            if (model.boneSelection[i])
                model.selectedBone = static_cast<int>(i);
            else if (model.selectedBone == static_cast<int>(i))
                model.selectedBone = -1;
            changed = true;
        } else {
            if (first < 0)
                first = static_cast<int>(i);
            // Repeated clicks cycle through overlapping markers, then wrap.
            if (static_cast<int>(i) > model.selectedBone) {
                candidate = static_cast<int>(i);
                break;
            }
        }
    }
    if (!additive) {
        if (candidate < 0)
            candidate = first;
        if (candidate >= 0) {
            std::memset(model.boneSelection, 0, model.boneCount);
            model.boneSelection[candidate] = 1;
            model.selectedBone = candidate;
            changed = true;
        }
    }
    return changed;
}

void SelectBonesInScreenRect(mdl::ModelRecord& model, int left, int top, int right, int bottom,
                             bool additive) {
    if (!model.boneTable || !model.boneSelection)
        return;
    for (std::uint32_t i = 0; i < model.boneCount; ++i) {
        const auto& bone = model.boneTable[i];
        // x64 452EA6..452EC4: unlike clicking, box selection includes hidden
        // types <= 6 and fixed-axis bones, without the rigid-body gate.
        const bool eligible =
            static_cast<std::int8_t>(bone.type) <= static_cast<int>(mdl::BoneType::Effector) ||
            bone.type == mdl::BoneType::FixedAxis;
        if (eligible && bone.screenX > left && bone.screenX < right && bone.screenY > top &&
            bone.screenY < bottom)
            model.boneSelection[i] = 1;
        else if (!additive)
            model.boneSelection[i] = 0;
    }
}

void FinishBoneBoxSelection(mdl::ModelRecord& model, bool additive) {
    if (!model.boneSelection || model.boneCount == 0)
        return;
    // x64 452136..4521F7: preserve the active bone if it remains selected.
    if (model.selectedBone < 0 || !model.boneSelection[model.selectedBone]) {
        if (!additive)
            model.selectedBone = -1;
        if (model.boneSelection[0])
            model.selectedBone = 0;
    }
    int count = 0, last = -1;
    for (std::uint32_t i = 0; i < model.boneCount; ++i)
        if (model.boneSelection[i]) {
            ++count;
            last = static_cast<int>(i);
        }
    if (count == 1)
        model.selectedBone = last;
}

void MouseInteractionBegin(MMDApp* app) {
    // 0x46FF02 -> 0x42D3A0 -> 0x40E3D0.  State values are exactly
    // 0 idle, 1 pressed, 2 released, 3 held.
    struct KeySlot {
        int key;
        std::int32_t MMDAppState::* state;
    };
    static constexpr KeySlot kKeySlots[] = {
        {VK_UP, &MMDAppState::upKeyState},        {VK_DOWN, &MMDAppState::downKeyState},
        {VK_LEFT, &MMDAppState::leftKeyState},    {VK_RIGHT, &MMDAppState::rightKeyState},
        {VK_SPACE, &MMDAppState::spaceKeyState},  {VK_DELETE, &MMDAppState::deleteKeyState},
        {VK_ESCAPE, &MMDAppState::escKeyState},   {VK_TAB, &MMDAppState::tabKeyState},
        {VK_RETURN, &MMDAppState::enterKeyState}, {VK_MENU, &MMDAppState::menuKeyState},
        {221, &MMDAppState::keyState221},         {226, &MMDAppState::keyState226},
    };
    for (const KeySlot& slot : kKeySlots)
        PollKey(app, app->state.*slot.state, slot.key);

    // Letter hotkeys share the dialog re-entry guard ints (see
    // MMDAppState::letterKeyState): lowercase and uppercase VK pairs fold
    // onto the same slot.
    static constexpr struct {
        int key;
        LetterKey letterKey;
    } kLetterKeys[] = {
        {'x', LetterKey::X}, {'X', LetterKey::X}, {'z', LetterKey::Z}, {'Z', LetterKey::Z},
        {'c', LetterKey::C}, {'C', LetterKey::C}, {'v', LetterKey::V}, {'V', LetterKey::V},
        {'d', LetterKey::D}, {'D', LetterKey::D}, {'a', LetterKey::A}, {'A', LetterKey::A},
        {'b', LetterKey::B}, {'B', LetterKey::B}, {'g', LetterKey::G}, {'G', LetterKey::G},
        {'s', LetterKey::S}, {'S', LetterKey::S}, {'i', LetterKey::I}, {'I', LetterKey::I},
        {'h', LetterKey::H}, {'H', LetterKey::H}, {'k', LetterKey::K}, {'K', LetterKey::K},
        {'p', LetterKey::P}, {'P', LetterKey::P}, {'u', LetterKey::U}, {'U', LetterKey::U},
        {'j', LetterKey::J}, {'J', LetterKey::J}, {'f', LetterKey::F}, {'F', LetterKey::F},
        {'r', LetterKey::R}, {'R', LetterKey::R}, {'l', LetterKey::L}, {'L', LetterKey::L},
    };
    for (const auto& slot : kLetterKeys)
        PollKey(app, app->LetterKeyState(slot.letterKey), slot.key);

    for (int i = 0; i < 10; ++i)
        PollKey(app, app->state.numpadKeyState[i], VK_NUMPAD0 + i);

    PollKey(app, app->ShiftModifierState(), VK_SHIFT);
    PollKey(app, app->CtrlModifierState(), VK_CONTROL);
    PollKey(app, app->LeftMouseButtonState(), VK_LBUTTON);
    PollKey(app, app->RightMouseButtonState(), VK_RBUTTON);
    PollKey(app, app->MiddleMouseButtonState(), VK_MBUTTON);

    // 0x46FF07..0x46FF32 immediately follows the 0x42D3A0 call: input is
    // active when the foreground HWND is A0D38, or the main HWND when A0D38
    // is null.
    HWND active = app->FloatingWindow();
    if (active == nullptr)
        active = app->state.hwnd;
    app->ViewportInputActive() = static_cast<std::uint8_t>(GetForegroundWindow() == active);
    const ViewportToolAction operation = ViewportToolAtPoint(app);
    BeginOrEndViewportToolDrag(app, operation);
    if (app->ViewportInputActive() == 0)
        return;

    if (app->LeftMouseButtonHeld())
        UpdateBoneBoxSelection(app);

    const int dx = app->MouseX() - app->PreviousMouseX();
    const int dy = app->MouseY() - app->PreviousMouseY();

    if (app->LeftMouseButtonHeld()) {
        const ViewportToolAction viewMode = app->ViewToolDragOperation();
        if (viewMode == ViewportToolAction::CameraOrbit) {
            app->CameraDistance() -= static_cast<float>(dy) * 0.1f;
            ViewRefreshGate(app);
        } else if (viewMode == ViewportToolAction::CameraPan) {
            if (app->CameraMode() != 0)
                PanCameraPosition(app, dx, dy);
            else {
                app->ViewOffsetX() -= static_cast<float>(dx) * DragScale(app);
                app->ViewOffsetY() += static_cast<float>(dy) * DragScale(app);
            }
            ViewRefreshGate(app);
        }
    }

    // Right/middle-button camera drags (x86 0x470BF5..0x47133D) live in
    // pump_navigation.cpp ConsumeRightButtonDrag / ConsumeMiddleButtonPan,
    // wired from FrameDriver before the letter ladder; the approximation
    // that used to live here was removed with their arrival.
}

void MouseInteractionEnd(MMDApp* app) {
    app->PreviousMouseX() = app->MouseX();
    app->PreviousMouseY() = app->MouseY();
}

// runtime-initialized in the original (.rdata image differs); the
// registry-echo conversion pair 0x52B760/0x52B768 turns the stored radian
// value into the "%3.4f" display figure (best evidence: PI/180 divisor)
double g_AngleDegreesScale = 180.0;            // VA 0x0052B760
double g_AnglePiTruncated = 3.141592025756836; // VA 0x0052B768

// ---- light/camera/registry chains (raw listing 10776..11260) --------------
// Common shape per axis mode (selector A=this+0x24==3, B=this+0xC0==3):
//   direct  (this+0x9ED9C == 1): app[axis] += dy * scale
//   registry(this+0x9ED9C == 2 && slot): slot[axis'] -=/+ dy * scale,
//     echo "%3.4f" into the axis edit box via SetWindowTextA.
// dy = (this+8) - (this+0x10);  slots: this+0x9DD70[this+0x9E170].
namespace {

// The x64 build rounds each operation to binary32; the x86 path retains
// its recovered double intermediates. Keep the distinction local to these
// operation-panel drag modes.
#ifdef _WIN64
using PanelDragScalar = float;
#else
using PanelDragScalar = double;
#endif
constexpr PanelDragScalar kPositionDragCoarse = 0.5f;
constexpr PanelDragScalar kPositionDragFine = 0.005f;
constexpr PanelDragScalar kPositionDragNormal = 0.05f;
constexpr PanelDragScalar kAngleDragCoarse = 0.2f;
constexpr PanelDragScalar kAngleDragFine = 0.002f;
constexpr PanelDragScalar kAngleDragNormal = 0.02f;
constexpr PanelDragScalar kDisplayPi = 3.141592025756836f;
constexpr PanelDragScalar kHalfTurnDegrees = 180.0f;

int DyOf(MMDApp* s) {
    return s->MouseY() - s->PreviousMouseY();
}

bool SelA3(MMDApp* s) {
    return s->ShiftModifierActive();
}
bool SelB3(MMDApp* s) {
    return s->CtrlModifierActive();
}

void EchoEdit(MMDApp* s, int dlgItem, const char* text) {
    HWND window = static_cast<HWND>(s->Hwnd());
    SetWindowTextA(GetDlgItem(window, dlgItem), text);
}

} // namespace

void ModeCameraAdjust(MMDApp* app, int axis) {
    // 0x4786FB..0x4790DF, modes 13..15.  The same operation widgets edit
    // either the camera target or the selected accessory position.
    static const int kEdit[3] = {panel::kAccPosXEdit, panel::kAccPosYEdit, panel::kAccPosZEdit};
    if (axis < 0 || axis > 2) {
        ViewRefreshGate(app);
        return;
    }
    const int target = app->state.coordinateSystem;
    const int dy = DyOf(app);
    PanelDragScalar scale;
    if (SelA3(app))
        scale = kPositionDragCoarse;
    else if (SelB3(app))
        scale = kPositionDragFine;
    else
        scale = kPositionDragNormal;
    if (target == 2 && app->CameraMode() != 0) {
        auto* accessory = app->AccessorySlot(app->state.selectedObjectSlot);
        if (accessory != nullptr) {
            float& value = accessory->position[axis];
            value = static_cast<float>(static_cast<PanelDragScalar>(value) -
                                       static_cast<PanelDragScalar>(dy) * scale);
            char buf[0x100];
            sprintf_s(buf, 0x100, "%3.4f", value);
            EchoEdit(app, kEdit[axis], buf);
        }
    } else if (app->state.cameraParentModel >= 0 || target == 1) {
        app->CameraPosition()[axis] =
            static_cast<float>(static_cast<PanelDragScalar>(app->CameraPosition()[axis]) +
                               static_cast<PanelDragScalar>(dy) * scale);
    } else {
        auto& api = d3dx::Get();
        d3dx::D3DXMATRIXF rx{}, ry{}, rz{}, rotation{};
        api.rotZ(&rz, -app->CameraRoll());
        api.rotX(&rx, -app->CameraPitch());
        api.multiply(&rotation, &rz, &rx);
        api.rotY(&ry, -app->CameraYaw());
        api.multiply(&rotation, &rotation, &ry);
        float local[3]{};
        local[axis] = static_cast<float>(static_cast<PanelDragScalar>(dy) * scale);
        float world[4]{};
        api.vec3Transform(world, local, &rotation);
        app->CameraPositionX() += world[0];
        app->CameraPositionY() += world[1];
        app->CameraPositionZ() += world[2];
    }
    ViewRefreshGate(app);
}

void ModeAngleAdjust(MMDApp* app, int axis) {
    // 0x4790E0..0x4798xx, modes 16..18.  Stored angles are radians;
    // accessory edit controls display degrees.
    static const int kEdit[3] = {panel::kAccRotXEdit, panel::kAccRotYEdit, panel::kAccRotZEdit};
    if (axis < 0 || axis > 2) {
        ViewRefreshGate(app);
        return;
    }
    const int target = app->state.coordinateSystem;
    const int dy = DyOf(app);
    PanelDragScalar scale;
    if (SelA3(app))
        scale = kAngleDragCoarse;
    else if (SelB3(app))
        scale = kAngleDragFine;
    else
        scale = kAngleDragNormal;
    // Unlike position dragging, the angle target does not depend on the
    // camera radio flag (x64 456788/456933/456A78; x86 479153).
    if (target == 2) {
        auto* accessory = app->AccessorySlot(app->state.selectedObjectSlot);
        if (accessory != nullptr) {
            float& value = accessory->rotation[axis];
            value = static_cast<float>(static_cast<PanelDragScalar>(value) +
                                       static_cast<PanelDragScalar>(dy) * scale);
            char buf[0x100];
            sprintf_s(buf, 0x100, "%3.4f", value / kDisplayPi * kHalfTurnDegrees);
            EchoEdit(app, kEdit[axis], buf);
        }
    } else {
        app->CameraRotation()[axis] =
            static_cast<float>(static_cast<PanelDragScalar>(app->CameraRotation()[axis]) +
                               static_cast<PanelDragScalar>(dy) * scale);
    }
    ViewRefreshGate(app);
}

void ModeRotate(MMDApp* app, int /*axis*/) {
    // app+0x34C modes 1..3: view-axis bone rotation.
    BoneEditModes(app);
    ViewRefreshGate(app);
}

void ModeTranslate(MMDApp* app, int /*axis*/) {
    // app+0x34C modes 4..6: selected-bone local-axis rotation.
    BoneEditModes(app);
    ViewRefreshGate(app);
}

void ModeScale(MMDApp* app) {
    // app+0x34C mode 7 is horizontal selected-root translation, not scaling.
    BoneEditModes(app);
    ViewRefreshGate(app);
}

void ModeBoneRotate(MMDApp* app, int) {
    // app+0x34C modes 8/9 add vertical or combined screen-plane movement.
    BoneEditModes(app);
    ViewRefreshGate(app);
}

void ModePhysicsBody(MMDApp* app, int) {
    // app+0x34C modes 10..12: local-axis translation or active record edits.
    BoneEditModes(app);
    ViewRefreshGate(app);
}

// ---------------------------------------------------------------------------
// VA 0x0041ACD0 - ApplyCameraReferenceModeChange(app, oldMode) (was
// 0x41ACD0): camera-reference switch re-anchor
// (command dispatch 0x47FA60/0x47FA88, control 0x213 family).  When the mode
// byte at +0x340 (kByte340) changes, the accessory ground position stored at
// app+0x308/+0x30C/+0xA08DC is re-anchored: both the OLD mode (the `mode`
// argument) and the NEW mode (current byte +0x340) resolve to a bone-record
// origin of the selected model, each origin matrix is rotated through the
// accessory rotation R = RotY(+0x314) * RotX(+0x310) * RotZ(+0x318) (and the
// parent transform at +0xA0438 when the parent index +0xA0430 >= 0), and the
// difference of the rotated translations is subtracted from the position.
//
// Bone-record resolution (selected model = slots[+0x910], records at
// *(model+0x26BC), 0x25C stride - the same array as physics_create.cpp):
//   mode 1: record index = *(model+0x4CCEC); origin = -(rec+100/104/108)
//   mode 2: record index = max(0, *(model+0x2D90)); origin = -(M * p) with
//           M the 4x4 matrix at rec+0x34 (floats 13..28) and p at rec+0x134
//           (floats 77..79); rows summed left-associative, single rounding
//           at the float store (x87 extended accumulation - commutative, so
//           the second block's commuted first add in the listing is
//           value-identical)
//   else:   origin = (0, 0, 0)
// ---------------------------------------------------------------------------
void ApplyCameraReferenceModeChange(MMDApp* app, int oldMode) {
    auto& api = d3dx::Get();
    // R = RotY * RotX * RotZ  (0x41ACEB..0x41AD5D)
    d3dx::D3DXMATRIXF ry{}, rot{}, rz{};
    api.rotY(&ry, app->CameraYaw());
    api.rotX(&rot, app->CameraPitch());
    api.multiply(&rot, &ry, &rot);
    api.rotZ(&rz, app->CameraRoll());
    api.multiply(&rot, &rot, &rz);
    if (app->CameraParentModel() >= 0)
        api.multiply(&rot, reinterpret_cast<d3dx::D3DXMATRIXF*>(&app->CameraAttachmentBasis()),
                     &rot);

    mdl::ModelRecord* model = app->SelectedModel();
    auto* bones = model->boneTable;

    auto modeOrigin = [&](int modeValue, d3dx::D3DXMATRIXF* out) {
        if (modeValue == 1) { // 0x41AD79
            const mdl::BoneRecord& rec = bones[model->centerBone];
            const float tx = -rec.matInit[12];
            const float ty = -rec.matInit[13];
            const float tz = -rec.matInit[14];
            api.translation(out, tx, ty, tz);
        } else if (modeValue == 2) { // 0x41ADC6
            std::int32_t idx = model->selectedBone;
            if (idx < 0)
                idx = 0;
            const mdl::BoneRecord& rec = bones[idx];
            const float z = rec.matInit[2] * rec.position[0] + rec.matInit[6] * rec.position[1] +
                            rec.matInit[10] * rec.position[2] + rec.matInit[14];
            const float y = rec.matInit[1] * rec.position[0] + rec.matInit[5] * rec.position[1] +
                            rec.matInit[9] * rec.position[2] + rec.matInit[13];
            const float x = rec.matInit[0] * rec.position[0] + rec.matInit[4] * rec.position[1] +
                            rec.matInit[8] * rec.position[2] + rec.matInit[12];
            api.translation(out, -x, -y, -z);
        } else {
            api.translation(out, 0.0f, 0.0f, 0.0f);
        }
    };

    d3dx::D3DXMATRIXF tOld{}, tNew{};
    modeOrigin(oldMode, &tOld); // a2 (old mode)
    modeOrigin(static_cast<int>(app->CameraReferenceMode()), &tNew);
    api.multiply(&tOld, &tOld, &rot); // 0x41B005
    api.multiply(&tNew, &tNew, &rot); // 0x41B01A
    app->ViewOffsetX() -= tOld.m[3][0] - tNew.m[3][0];
    app->ViewOffsetY() -= tOld.m[3][1] - tNew.m[3][1];
    app->CameraDistance() -= tOld.m[3][2] - tNew.m[3][2];
}

} // namespace mikudancestudio
