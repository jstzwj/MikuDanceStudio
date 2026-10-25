#include "mikudancestudio/mmd_app.hpp"
#include "mikudancestudio/model.hpp"
#include "mikudancestudio/d3d_wrapper.hpp"

#include <array>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <memory>

namespace mikudancestudio {
void SetupFrameWorldTransform(MMDApp* app);
void PostModelReload(MMDApp* app);
}

using namespace mikudancestudio;

namespace {
enum Flags {
    Camera = 1,
    Track = 2,
    Playing = 4,
    Parent = 8,
    OtherModel = 16,
    Suppressed = 32,
    NoSelectedBone = 64,
    Reload = 128,
    StoredRotation = 256,
};
enum Basis {
    Identity,
    PositiveZ,
    NegativeZ
};
using Reference = CameraAttachmentReference;
struct Case {
    const char* name;
    unsigned flags;
    Reference reference;
    Basis worldBasis;
    float worldTranslation[3];
    float eye[3];
    Basis savedBasis;
};

// Original v9.32 x64, SHA-256 07516fd3bf1e6b1339836b6773a156f61
// bdd6f848eeb621fdda012375df313a1, matrix block RVA 0x2767B..0x28010.
// The first seven cases were captured by executing that machine code with
// native D3DX9_43 calls. Remaining cases cover the original branch conditions
// and the saved-basis writes (RVA 0x27C72), with exact quarter-turn matrices.
// These expectations must not be regenerated from the port.
const Case cases[] = {
    {"camera", Camera, Reference::None, Identity, {-10, -20, -30}, {0, 0, -45}, Identity},
    {"track, origin",
     Track | Reload,
     Reference::None,
     Identity,
     {0, 0, 0},
     {10, 20, -15},
     Identity},
    {"track, model root",
     Track | Reload,
     Reference::ModelRoot,
     Identity,
     {-7, -11, -13},
     {3, 9, -28},
     Identity},
    {"track, selected bone",
     Track | Reload,
     Reference::SelectedBone,
     Identity,
     {-9, -14, -18},
     {1, 6, -33},
     Identity},
    {"camera, rotated parent",
     Camera | Parent,
     Reference::None,
     NegativeZ,
     {-13, 4, -18},
     {10, 20, 30},
     Identity},
    {"track, parent selected",
     Track | Parent | Reload,
     Reference::None,
     NegativeZ,
     {-13, 4, -18},
     {10, 20, 30},
     NegativeZ},
    {"track, other model root",
     Track | Parent | OtherModel | Reload,
     Reference::ModelRoot,
     NegativeZ,
     {-11, 7, -13},
     {12, 23, 35},
     NegativeZ},
    {"free view, parent",
     Parent | StoredRotation,
     Reference::ModelRoot,
     PositiveZ,
     {11, -7, -13},
     {0, 0, -45},
     PositiveZ},
    {"free view, no bone selected",
     StoredRotation | NoSelectedBone,
     Reference::SelectedBone,
     PositiveZ,
     {0, 0, 0},
     {0, 0, -45},
     PositiveZ},
    {"track playing, other model",
     Track | Playing | Parent | OtherModel | StoredRotation,
     Reference::ModelRoot,
     NegativeZ,
     {-13, 4, -18},
     {10, 20, 30},
     PositiveZ},
    {"track, other model origin",
     Track | Parent | OtherModel | StoredRotation,
     Reference::None,
     NegativeZ,
     {0, 0, 0},
     {0, 0, -45},
     PositiveZ},
    {"track, other model bone",
     Track | Parent | OtherModel | StoredRotation,
     Reference::SelectedBone,
     NegativeZ,
     {-13, 4, -18},
     {0, 0, -45},
     PositiveZ},
    {"track, suppressed parent",
     Track | Parent | Suppressed | StoredRotation,
     Reference::ModelRoot,
     Identity,
     {-4, -13, -18},
     {10, 20, 30},
     Identity},
    {"track, no parent resets saved basis",
     Track | StoredRotation,
     Reference::None,
     Identity,
     {0, 0, 0},
     {0, 0, -45},
     Identity},
    {"camera, no parent resets saved basis",
     Camera | StoredRotation,
     Reference::None,
     Identity,
     {-10, -20, -30},
     {0, 0, -45},
     Identity},
    {"track, no bone selected",
     Track | NoSelectedBone | StoredRotation,
     Reference::SelectedBone,
     Identity,
     {0, 0, 0},
     {0, 0, -45},
     Identity},
    {"camera, suppressed parent",
     Camera | Parent | Suppressed | StoredRotation,
     Reference::None,
     Identity,
     {-4, -13, -18},
     {10, 20, 30},
     Identity},
    {"free view, parent and origin",
     Parent | StoredRotation,
     Reference::None,
     PositiveZ,
     {0, 0, 0},
     {0, 0, -45},
     PositiveZ},
    {"playing without track",
     Playing | Parent | OtherModel | StoredRotation,
     Reference::ModelRoot,
     PositiveZ,
     {11, -7, -13},
     {0, 0, -45},
     PositiveZ},
};

D3DMATRIX MakeMatrix(Basis basis, const float* translation = nullptr) {
    D3DMATRIX result{};
    result.m[0][0] = result.m[1][1] = result.m[2][2] = result.m[3][3] = 1;
    if (basis != Identity) {
        result.m[0][0] = result.m[1][1] = 0;
        result.m[0][1] = basis == PositiveZ ? 1.0f : -1.0f;
        result.m[1][0] = -result.m[0][1];
    }
    if (translation)
        std::memcpy(result.m[3], translation, 3 * sizeof(float));
    return result;
}

// Capture the actual production D3D boundary without a window or GPU. The
// product uses just these two IDirect3DDevice9 slots in this path.
struct Capture {
    void** vtable;
    D3DMATRIX world{}, view{};
    unsigned worlds = 0, views = 0, lights = 0, unexpected = 0;
};
HRESULT WINAPI SetTransform(IDirect3DDevice9* device, D3DTRANSFORMSTATETYPE kind,
                            const D3DMATRIX* value) {
    auto& capture = *reinterpret_cast<Capture*>(device);
    if (kind == D3DTS_WORLD) {
        capture.world = *value;
        ++capture.worlds;
    } else if (kind == D3DTS_VIEW) {
        capture.view = *value;
        ++capture.views;
    } else
        ++capture.unexpected;
    return S_OK;
}
HRESULT WINAPI SetLight(IDirect3DDevice9* device, DWORD, const D3DLIGHT9*) {
    ++reinterpret_cast<Capture*>(device)->lights;
    return S_OK;
}

bool EqualMatrix(const D3DMATRIX& actual, const D3DMATRIX& expected) {
    for (int row = 0; row < 4; ++row)
        for (int col = 0; col < 4; ++col)
            if (!std::isfinite(actual.m[row][col]) ||
                std::fabs(actual.m[row][col] - expected.m[row][col]) > 1e-5f)
                return false;
    return true;
}
}

int main() {
    std::array<void*, 119> vtable{};
    vtable[44] = reinterpret_cast<void*>(&SetTransform);
    vtable[51] = reinterpret_cast<void*>(&SetLight);
    auto app = std::make_unique<MMDApp>();
    auto model = std::make_unique<mdl::ModelRecord>();
    auto renderer = std::make_unique<D3DRenderer>();
    int failures = 0;
    for (const auto& test : cases) {
        Capture capture{vtable.data()};
        renderer->device = reinterpret_cast<IDirect3DDevice9*>(&capture);
        std::memset(&app->state, 0, sizeof(app->state));
        app->Renderer() = renderer.get();
        mdl::BoneRecord bone{};
        const float boneTranslation[] = {7, 11, 13};
        const auto boneMatrix =
            MakeMatrix(test.flags & Parent ? PositiveZ : Identity, boneTranslation);
        std::memcpy(bone.matInit, &boneMatrix, sizeof(boneMatrix));
        bone.position[0] = 2;
        bone.position[1] = 3;
        bone.position[2] = 5;
        model->boneTable = &bone;
        model->boneCount = 1;
        model->centerBone = 0;
        model->selectedBone = test.flags & NoSelectedBone ? -1 : 0;
        app->ModelSlot(0) = app->ModelSlot(1) = model.get();
        app->SetSelectedModelSlot(test.flags & OtherModel ? 1 : 0);
        app->CameraMode() = (test.flags & Camera) != 0;
        app->state.followCameraEnabled = (test.flags & Track) != 0;
        app->PlaybackActive() = (test.flags & Playing) != 0;
        app->CameraParentModel() = test.flags & Parent ? 0 : -1;
        app->CameraParentBone() = 0;
        app->CameraReferenceMode() = test.reference;
        app->CameraAttachmentTransformSuppressed() = (test.flags & Suppressed) != 0;
        app->CameraAttachmentBasis() =
            MakeMatrix(test.flags & StoredRotation ? PositiveZ : Identity);
        app->CameraPositionX() = 10;
        app->CameraPositionY() = 20;
        app->CameraPositionZ() = 30;
        app->CameraDistance() = -45;
        if (test.flags & Reload)
            PostModelReload(app.get());

        SetupFrameWorldTransform(app.get());
        const auto check = [&](bool ok, const char* field) {
            if (!ok) {
                ++failures;
                std::fprintf(stderr, "%s: %s\n", test.name, field);
            }
        };
        check(EqualMatrix(capture.world, MakeMatrix(test.worldBasis, test.worldTranslation)),
              "world");
        const float viewTranslation[] = {-test.eye[0], -test.eye[1], -test.eye[2]};
        check(EqualMatrix(capture.view, MakeMatrix(Identity, viewTranslation)), "view");
        check(app->ViewOffsetX() == test.eye[0] && app->ViewOffsetY() == test.eye[1] &&
                  app->CameraDistance() == test.eye[2],
              "eye state");
        check(EqualMatrix(app->CameraAttachmentBasis(), MakeMatrix(test.savedBasis)),
              "saved basis");
        check(capture.worlds == 1 && capture.views == 1 && capture.lights == 1 &&
                  capture.unexpected == 0,
              "device calls");
    }
    return failures ? 1 : 0;
}
