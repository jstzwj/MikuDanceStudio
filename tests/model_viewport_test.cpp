#include "mikudancestudio/d3dx_dyn.hpp"
#include "mikudancestudio/mmd_app.hpp"
#include "mikudancestudio/model.hpp"
#include "mikudancestudio/ported_funcs.hpp"
#include "../src/app/bone_selection.hpp"

#include <array>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <memory>

using namespace mikudancestudio;
namespace {
int failures;
void Check(bool ok, const char* message) {
    if (!ok) {
        ++failures;
        std::fprintf(stderr, "FAIL: %s\n", message);
    }
}
void Near(float actual, float expected, const char* message) {
    Check(std::isfinite(actual) && std::fabs(actual - expected) < 2e-5f, message);
}
void Identity(float* m) {
    std::memset(m, 0, 16 * sizeof(float));
    m[0] = m[5] = m[10] = m[15] = 1;
}
void QuarterTurn(float* m, bool positive) {
    Identity(m);
    m[0] = m[5] = 0;
    m[1] = positive ? 1.0f : -1.0f;
    m[4] = -m[1];
}
struct Capture {
    void** vtable;
    D3DMATRIX world{};
    unsigned worlds = 0, clears = 0, lights = 0;
};
HRESULT WINAPI SetTransform(IDirect3DDevice9* device, D3DTRANSFORMSTATETYPE type,
                            const D3DMATRIX* value) {
    auto& c = *reinterpret_cast<Capture*>(device);
    if (type == D3DTS_WORLD) {
        c.world = *value;
        ++c.worlds;
    }
    return S_OK;
}
HRESULT WINAPI Clear(IDirect3DDevice9* device, DWORD count, const D3DRECT*, DWORD flags,
                     D3DCOLOR color, float depth, DWORD stencil) {
    Check(count == 0 && flags == D3DCLEAR_ZBUFFER && color == 0x00FFFFFF && depth == 1 &&
              stencil == 0,
          "operation axis clears depth only");
    ++reinterpret_cast<Capture*>(device)->clears;
    return S_OK;
}
HRESULT WINAPI SetLight(IDirect3DDevice9* device, DWORD, const D3DLIGHT9*) {
    ++reinterpret_cast<Capture*>(device)->lights;
    return S_OK;
}
struct Fixture {
    std::unique_ptr<MMDApp> app = std::make_unique<MMDApp>();
    std::unique_ptr<mdl::ModelRecord> model = std::make_unique<mdl::ModelRecord>();
    std::unique_ptr<D3DRenderer> renderer = std::make_unique<D3DRenderer>();
    PhysicsScene scene{};
    mdl::AccessoryRecord mesh{};
    std::array<mdl::BoneRecord, 6> bones{};
    std::array<unsigned char, 6> selected{}, edited{};
    std::array<void*, 119> vtable{};
    Capture capture{vtable.data()};
    Fixture() {
        std::memset(&app->state, 0, sizeof app->state);
        app->SetSelectedModelSlot(kModelSlotCount - 1);
        app->ModelSlot(kModelSlotCount - 1) = model.get();
        model->boneTable = bones.data();
        model->boneCount = static_cast<unsigned>(bones.size());
        model->boneSelection = selected.data();
        model->bonePhysicsState = edited.data();
        model->selectedBone = 0;
        app->Renderer() = renderer.get();
        app->Physics() = &scene;
        scene.owner = renderer.get();
        app->AxisMeshObject() = &mesh;
        vtable[43] = reinterpret_cast<void*>(&Clear);
        vtable[44] = reinterpret_cast<void*>(&SetTransform);
        vtable[51] = reinterpret_cast<void*>(&SetLight);
        renderer->device = reinterpret_cast<IDirect3DDevice9*>(&capture);
        renderer->screenWidth = 100;
        renderer->screenHeight = 80;
        renderer->viewScale = 2;
        app->RenderWidth() = 400;
        app->RenderHeight() = 300;
        app->state.selectedClipW = 2;
        app->CameraDistance() = -45;
        app->CameraFov() = 30;
        app->CameraParentModel() = -1;
        Identity(&app->CameraAttachmentBasis().m[0][0]);
        for (auto& bone : bones) {
            bone.parent = -1;
            bone.type = mdl::BoneType::Move;
            Identity(bone.matInit);
            Identity(bone.matWorld);
            bone.rotQuat[3] = 1;
            for (int i = 0; i < 7; ++i)
                bone.ikBackup[i] = static_cast<float>(i + 10);
        }
        selected[0] = 1;
    }
    ~Fixture() {
        for (auto& ring : model->undoRings)
            for (auto& undo : ring.slots)
                if (undo.operation == mdl::UndoOperation::BonePose)
                    ::operator delete(undo.bonePose);
    }
    void Drag(ViewportDragMode mode) {
        app->InteractionDragMode() = mode;
        BoneEditModes(app.get());
    }
};

void TestRootTranslation() {
    Fixture f;
    f.selected = {1, 1, 1, 1, 0, 1};
    f.bones[1].parent = 0;
    f.bones[2].type = mdl::BoneType::Ik;
    f.bones[3].type = mdl::BoneType::RotateMove;
    QuarterTurn(f.bones[2].matWorld, false);
    f.app->state.coordinateSystem = 1;
    f.app->PreviousMouseY() = 20;
    f.Drag(ViewportDragMode::PhysicsAxisX);
    Near(f.bones[0].trans[0], 1, "world movement reaches selected root");
    Near(f.bones[2].trans[1], 1, "world delta converts through each root's inverse basis");
    Near(f.bones[5].trans[0], 1, "independent selected root moves once");
    Check(f.edited == std::array<unsigned char, 6>{1, 0, 1, 0, 0, 1},
          "parent selection, bone type and selection bytes gate pose edits");
    Near(f.bones[1].trans[0], 0, "child inherits selected parent without a second delta");
    for (const auto& b : f.bones)
        for (int i = 0; i < 7; ++i)
            Near(b.ikBackup[i], static_cast<float>(i + 10), "drag preserves physics backup pose");

    // Local X is rotated into world Y by the active bone, then converted
    // separately for the other roots. The selected child's position is unchanged.
    f.bones[0].trans[0] = 0;
    QuarterTurn(f.bones[0].matInit, true);
    f.app->state.coordinateSystem = 0;
    f.Drag(ViewportDragMode::PhysicsAxisX);
    Near(f.bones[0].trans[1], 1, "local movement uses active bone's forward basis");
    Near(f.bones[2].trans[0], -1, "local movement reaches the other root's own basis");
}

void TestRotation() {
    for (int coordinate : {0, 1}) {
        Fixture f;
        QuarterTurn(f.bones[0].matInit, true);
        QuarterTurn(f.bones[0].matWorld, false);
        f.app->state.coordinateSystem = coordinate;
        // Deliberately opposite to the coordinate selector: the editor page
        // must have no influence on bone axes.
        f.app->state.physicsEditorJointPage = static_cast<unsigned char>(1 - coordinate);
        f.app->PreviousMouseY() = 20;
        f.Drag(ViewportDragMode::AxisRotateX);
        Near(f.bones[0].rotQuat[0], coordinate ? 0 : -std::sin(0.1f), "local/world rotation X");
        Near(f.bones[0].rotQuat[1], coordinate ? std::sin(0.1f) : 0, "local/world rotation Y");
        Near(f.bones[0].rotQuat[3], std::cos(0.1f), "drag uses full angle in quaternion");
        Check(f.edited[0] == 1, "rotation sets pending pose-edit flag");
    }
    Fixture view;
    view.app->PreviousMouseY() = 20;
    view.app->CameraPitch() = 0.9f;
    view.app->CameraYaw() = 0.4f;
    view.Drag(ViewportDragMode::ViewAxisRotateY);
    Near(view.bones[0].rotQuat[0], std::sin(0.1f) * std::cos(0.4f),
         "view rotation uses yaw cosine");
    Near(view.bones[0].rotQuat[2], std::sin(0.1f) * std::sin(0.4f),
         "view rotation uses positive yaw sine");

    Fixture fixed;
    fixed.model->physicsMode = 2;
    fixed.bones[0].type = mdl::BoneType::FixedAxis;
    fixed.bones[0].axis[2] = 1;
    fixed.app->PreviousMouseX() = 20;
    fixed.Drag(ViewportDragMode::ViewAxisRotateX);
    Near(fixed.bones[0].rotQuat[0], 0, "type 8 restricts rotation even without a redundant flag");
    Near(fixed.bones[0].rotQuat[2], std::sin(0.1f), "PMX fixed-axis vector constrains rotation");

    Fixture group;
    group.model->physicsMode = 2;
    group.bones[0].type = mdl::BoneType::FixedAxis;
    group.bones[0].axis[2] = 1;
    group.bones[2].position[0] = 1;
    group.bones[1].parent = 0;
    group.selected = {1, 1, 1, 0, 0, 0};
    group.app->PreviousMouseX() = 20;
    group.Drag(ViewportDragMode::ViewAxisRotateX);
    Near(group.bones[2].trans[0], std::cos(0.2f) - 1, "other root rotates around active pivot X");
    Near(group.bones[2].trans[1], std::sin(0.2f), "other root rotates around active pivot Y");
    Check(group.edited[1] == 0 && group.edited[2] == 1,
          "rotation propagates to roots, not selected descendants");
}

void TestScreenMovement() {
    for (int ortho : {0, 1}) {
        Fixture f;
        f.app->CameraPerspective() = static_cast<unsigned char>(ortho);
        f.app->MouseX() = 6;
        f.app->PreviousMouseY() = 8;
        f.Drag(ViewportDragMode::BoneMoveScreenPlane);
        Near(f.bones[0].trans[0], ortho ? 3.24f : 0.08f, "horizontal screen scale and aspect");
        Near(f.bones[0].trans[1], ortho ? 4.275f : 0.105f, "vertical screen scale");
    }
}

void TestLocalAxes() {
    Fixture f;
    float basis[16];
    f.model->physicsMode = 2;
    f.bones[0].localAxes[0] = 2;
    f.bones[0].localAxes[5] = 3;
    BoneLocalAxes(f.model.get(), basis);
    Near(basis[0], 1, "PMX local X is normalized");
    Near(basis[5], -1, "PMX second axis preserves original cross-product order");
    Near(basis[10], -1, "PMX third axis preserves original handedness");
    // PMD arm names use the tail's rest position, not the parent position.
    f.model->physicsMode = 0;
    strcpy_s(f.bones[0].name, "\x89\x45\x98\x72");
    f.bones[0].tailBone = 1;
    f.bones[0].parent = 2;
    f.bones[1].position[0] = 3;
    f.bones[1].position[1] = 4;
    f.bones[2].position[1] = 9;
    BoneLocalAxes(f.model.get(), basis);
    Near(basis[0], 0.6f, "PMD arm axis follows tail X");
    Near(basis[1], 0.8f, "PMD arm axis follows tail Y");
    Near(basis[4], -0.8f, "PMD perpendicular axis");
}

void TestUndoTiming() {
    Fixture f;
    f.app->PreviousMouseX() = 0;
    f.app->LeftMouseButtonState() = 1;
    BeginOrEndViewportToolDrag(f.app.get(), ViewportToolAction::BoneMoveHorizontalHandle);
    const int cursor = f.model->UndoCursor();
    auto& record = f.model->undoRings[0].slots[cursor];
    Check(record.operation == mdl::UndoOperation::BonePose && record.recordCount == 1 &&
              record.bonePose[0].position[0] == 0,
          "press captures the pose before the first movement");
    f.app->LeftMouseButtonState() = 3;
    f.app->MouseX() = 6;
    BeginOrEndViewportToolDrag(f.app.get(), ViewportToolAction::BoneMoveHorizontalHandle);
    BoneEditModes(f.app.get());
    Check(f.model->UndoCursor() == cursor && record.bonePose[0].position[0] == 0,
          "held movement retains one original undo snapshot");
    f.app->LeftMouseButtonState() = 2;
    BeginOrEndViewportToolDrag(f.app.get(), ViewportToolAction::BoneMoveHorizontalHandle);
    Check(f.model->UndoCursor() == cursor &&
              f.app->InteractionDragMode() == ViewportDragMode::None &&
              f.app->EditMode() == ViewportEditMode::Move,
          "release ends movement without adding another undo record");
}

void TestOperationAxis() {
    Fixture f;
    auto& b = f.bones[0];
    QuarterTurn(b.matInit, true);
    b.matInit[12] = 7;
    b.matInit[13] = 11;
    b.matInit[14] = 13;
    b.position[0] = 2;
    b.position[1] = 3;
    b.position[2] = 5;
    // This point is (4,13,18) in model world space, regardless of axis basis.
    const float point[4] = {4, 13, 18, 1};
    for (auto reference : {CameraAttachmentReference::None, CameraAttachmentReference::ModelRoot,
                           CameraAttachmentReference::SelectedBone}) {
        f.app->CameraReferenceMode() = reference;
        for (int world : {0, 1}) {
            f.app->state.coordinateSystem = world;
            SetupFrameWorldTransform(f.app.get());
            const D3DMATRIX frame = f.capture.world;
            f.app->ViewportToolOperation() = ViewportToolAction::TransformAxisX;
            const unsigned before = f.capture.clears;
            DrawBoneOperationAxis(f.app.get(), f.model.get(), &frame.m[0][0]);
            Check(f.capture.clears == before + 1, "active operation axis reaches device");
            for (int c = 0; c < 3; ++c) {
                float anchor = 0;
                for (int i = 0; i < 4; ++i)
                    anchor += point[i] * frame.m[i][c];
                Near(f.capture.world.m[3][c], anchor,
                     "axis origin stays on bone after camera reference transform");
            }
        }
    }
    const auto frame = f.capture.world;
    for (auto type : {mdl::BoneType::FixedAxis, mdl::BoneType::UnderIk}) {
        b.type = type;
        b.flags = mdl::kBoneFlagFixedAxis;
        const unsigned before = f.capture.clears;
        DrawBoneOperationAxis(f.app.get(), f.model.get(), &frame.m[0][0]);
        Check(f.capture.clears == before, "constrained bone suppresses operation axis");
    }
    f.model->selectedBone = -1;
    DrawBoneOperationAxis(f.app.get(), f.model.get(), &frame.m[0][0]);
}
} // namespace

int main() {
    TestRootTranslation();
    TestRotation();
    TestScreenMovement();
    TestLocalAxes();
    TestUndoTiming();
    TestOperationAxis();
    return failures ? 1 : 0;
}
