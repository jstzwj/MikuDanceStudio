// ===========================================================================
// Debug geometry and operation-axis renderer
//   0x00401400..0x00401D93  primitive helpers
//   0x00406950              physics collision debug shapes
//   0x00420F30              accessory/attachment debug shapes
//   0x0042DB10              selected-bone operation axis
//   0x004C4760/0x004C5150   embedded axis.x owner and loader
// ===========================================================================
#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <Windows.h>
#include <d3d9.h>

#include <cmath>
#include <cstdint>
#include <cstring>
#include <new>

#include "btBulletDynamicsCommon.h"

#include "mikudancestudio/accessory_layout.hpp"
#include "mikudancestudio/d3dx_dyn.hpp"
#include "mikudancestudio/mme_bridge.hpp"
#include "mikudancestudio/mmd_app.hpp"
#include "mikudancestudio/ported_funcs.hpp"
#include "mikudancestudio/model.hpp"

namespace mikudancestudio {
namespace {

using Matrix = d3dx::D3DXMATRIXF;

template <typename T>
T& At(void* base, std::size_t offset) {
    return *reinterpret_cast<T*>(static_cast<unsigned char*>(base) + offset);
}

template <typename T>
T& At(const void* base, std::size_t offset) {
    return *reinterpret_cast<T*>(
        const_cast<unsigned char*>(static_cast<const unsigned char*>(base)) +
        offset);
}

IDirect3DDevice9* DeviceFromScene(PhysicsScene* scene) {
    D3DRenderer* sub = scene->owner;   // slot 0
    return sub->device;
}

void Identity(Matrix* m) {
    std::memset(m, 0, sizeof(*m));
    m->m[0][0] = 1.0f;
    m->m[1][1] = 1.0f;
    m->m[2][2] = 1.0f;
    m->m[3][3] = 1.0f;
}

void SetDebugColor(PhysicsScene* scene, std::uint8_t r, std::uint8_t g,
                   std::uint8_t b) {
    const DWORD color = D3DCOLOR_ARGB(255, r, g, b);
    // gizmo vertex buffers (scene slots 4/12/20/28)
    IDirect3DVertexBuffer9* const vbs[] = {
        scene->gizmoSphereVB,   // 4
        scene->gizmoCubeVB,     // 12
        scene->gizmoSphere33VB, // 20
        scene->gizmoArrowVB};   // 28
    const UINT lengths[] = {928, 128, 528, 256};
    for (int n = 0; n < 4; ++n) {
        auto* vb = vbs[n];
        unsigned char* data = nullptr;
        vb->Lock(0, lengths[n], reinterpret_cast<void**>(&data), 0);
        for (UINT offset = 12; offset < lengths[n]; offset += 16)
            *reinterpret_cast<DWORD*>(data + offset) = color;
        vb->Unlock();
    }
}

void DrawIndexedLines(PhysicsScene* scene, IDirect3DVertexBuffer9* vb,
                      IDirect3DIndexBuffer9* ib, UINT vertices,
                      UINT primitives) {
    auto* device = DeviceFromScene(scene);
    device->SetFVF(D3DFVF_XYZ | D3DFVF_DIFFUSE);
    device->SetStreamSource(0, vb, 0, 16);
    device->SetIndices(ib);
    // [0x7FF7CB426FD8 球 / 0x7FF7CB4272F4+0x7FF7CB4273E2+0x7FF7CB4274B2 胶囊]
    // 经 MME 桥：ownerless DIP（无 ActiveRenderObject）→ drawType=0 背景
    // 语义，效果激活时 MME 在此先跑后期链再转发，调试线画在后期合成结果
    // 之上——与原版经包装设备虚表被 MMHack 拦截的行为一致。
    mme::DrawIndexedPrimitive(device, D3DPT_LINELIST, 0, 0, vertices, 0,
                              primitives);
}

void DrawSphere(PhysicsScene* scene, float scale, const Matrix& transform) {
    auto& api = d3dx::Get();
    auto* device = DeviceFromScene(scene);
    Matrix oldWorld;
    Matrix world;
    api.scaling(&world, scale, scale, scale);
    api.multiply(&world, &world, &transform);
    device->GetTransform(D3DTS_WORLD,
                         reinterpret_cast<D3DMATRIX*>(&oldWorld));
    api.multiply(&world, &world, &oldWorld);
    device->SetTransform(D3DTS_WORLD,
                         reinterpret_cast<const D3DMATRIX*>(&world));
    device->SetTexture(0, nullptr);
    DrawIndexedLines(scene, scene->gizmoSphereVB,   // 4
                     scene->gizmoSphereIB, 58, 120); // 8
    device->SetTransform(D3DTS_WORLD,
                         reinterpret_cast<const D3DMATRIX*>(&oldWorld));
}

void DrawBox(PhysicsScene* scene, float x, float y, float z,
             const Matrix& transform) {
    auto& api = d3dx::Get();
    auto* device = DeviceFromScene(scene);
    Matrix oldWorld;
    Matrix world;
    api.scaling(&world, x * 2.0f, y * 2.0f, z * 2.0f);
    api.multiply(&world, &world, &transform);
    device->GetTransform(D3DTS_WORLD,
                         reinterpret_cast<D3DMATRIX*>(&oldWorld));
    api.multiply(&world, &world, &oldWorld);
    device->SetTransform(D3DTS_WORLD,
                         reinterpret_cast<const D3DMATRIX*>(&world));
    device->SetFVF(D3DFVF_XYZ | D3DFVF_DIFFUSE);
    device->SetStreamSource(0, scene->gizmoCubeVB, 0, 16);  // 12
    device->SetIndices(scene->gizmoCubeIB);                 // 16
    // [0x7FF7CB427150] 经 MME 桥转发，ownerless DIP 语义同 DrawIndexedLines。
    mme::DrawIndexedPrimitive(device, D3DPT_LINELIST, 0, 0, 8, 0, 12);
    device->SetTransform(D3DTS_WORLD,
                         reinterpret_cast<const D3DMATRIX*>(&oldWorld));
}

void DrawCapsule(PhysicsScene* scene, float radius, float halfHeight,
                 const Matrix& transform) {
    auto& api = d3dx::Get();
    auto* device = DeviceFromScene(scene);
    Matrix oldWorld;
    Matrix base;
    device->GetTransform(D3DTS_WORLD,
                         reinterpret_cast<D3DMATRIX*>(&oldWorld));
    api.multiply(&base, &transform, &oldWorld);

    Matrix scale;
    Matrix translation;
    Matrix world;
    api.scaling(&scale, -radius, -radius, -radius);
    api.translation(&translation, 0.0f, -halfHeight, 0.0f);
    api.multiply(&world, &scale, &translation);
    api.multiply(&world, &world, &base);
    device->SetTransform(D3DTS_WORLD,
                         reinterpret_cast<const D3DMATRIX*>(&world));
    DrawIndexedLines(scene, scene->gizmoSphere33VB,    // 20
                     scene->gizmoSphere33IB, 33, 64);   // 24

    api.scaling(&scale, radius, radius, radius);
    api.translation(&translation, 0.0f, halfHeight, 0.0f);
    api.multiply(&world, &scale, &translation);
    api.multiply(&world, &world, &base);
    device->SetTransform(D3DTS_WORLD,
                         reinterpret_cast<const D3DMATRIX*>(&world));
    DrawIndexedLines(scene, scene->gizmoSphere33VB,    // 20
                     scene->gizmoSphere33IB, 33, 64);   // 24

    api.scaling(&world, radius, halfHeight * 2.0f, radius);
    api.multiply(&world, &world, &base);
    device->SetTransform(D3DTS_WORLD,
                         reinterpret_cast<const D3DMATRIX*>(&world));
    DrawIndexedLines(scene, scene->gizmoArrowVB,       // 28
                     scene->gizmoIdentityIB, 16, 8);    // 32
    device->SetTransform(D3DTS_WORLD,
                         reinterpret_cast<const D3DMATRIX*>(&oldWorld));
}

void DrawSelectionBox(PhysicsScene* scene) {
    auto* device = DeviceFromScene(scene);
    device->SetFVF(D3DFVF_XYZ | D3DFVF_DIFFUSE);
    device->SetStreamSource(0, scene->gizmoBoxSelVB, 0, 16);  // 36
    device->SetIndices(scene->gizmoBoxSelIB);                 // 40
    // [0x7FF7CB42771B] 选中盒（实心三角面）同样经 MME 桥转发，ownerless
    // DIP 语义同 DrawIndexedLines。
    mme::DrawIndexedPrimitive(device, D3DPT_TRIANGLELIST, 0, 0, 72, 0, 24);
}

void BulletTransformToMatrix(const btTransform& source, Matrix* out) {
    Identity(out);
    const btMatrix3x3& basis = source.getBasis();
    for (int row = 0; row < 3; ++row)
        for (int column = 0; column < 3; ++column)
            out->m[row][column] = basis[column][row];
    const btVector3& origin = source.getOrigin();
    out->m[3][0] = origin.x();
    out->m[3][1] = origin.y();
    out->m[3][2] = origin.z();
}

using BufferPointer = void*(WINAPI*)(void*);
using MeshDrawSubset = HRESULT(WINAPI*)(void*, DWORD);

void ReleaseCom(void* object) {
    if (object != nullptr)
        reinterpret_cast<IUnknown*>(object)->Release();  // vtable slot 2
}

void DrawAxisMesh(void* axis, MMDApp* app) {
    D3DRenderer* sub = app->Renderer();
    auto* device = sub->device;
    mdl::AccessoryRecord& gizmo = *mdl::Accessory(axis);
    const DWORD count = gizmo.materialCount;
    auto* materials = static_cast<D3DMATERIAL9*>(gizmo.materials);
    for (DWORD i = 0; i < count; ++i) {
        device->SetMaterial(&materials[i]);
        device->SetTexture(0, nullptr);
        mme::DrawAccessorySubset(app, axis, i);
    }
}

unsigned char* CurrentModel(MMDApp* app) {
    return app->SelectedModel();
}

Matrix* BoneMatrix(mikudancestudio::mdl::BoneRecord* bone) {
    return reinterpret_cast<Matrix*>(bone->matInit);
}

void AttachToBone(MMDApp* app, Matrix* world, int boneIndex) {
    auto& api = d3dx::Get();
    unsigned char* model = CurrentModel(app);
    auto* bones = mikudancestudio::mdl::Bones(model);
    mikudancestudio::mdl::BoneRecord* bone = &bones[(boneIndex < 0 ? 0 : boneIndex)];
    Matrix translation;
    api.translation(&translation, bone->position[0], bone->position[1],
                    bone->position[2]);
    api.multiply(world, world, &translation);
    api.multiply(world, world, BoneMatrix(bone));
}

}  // namespace

void InitAccessoryRecord(void* object) {
    mdl::AccessoryRecord& accessory = *mdl::Accessory(object);
    accessory.mesh = nullptr;
    accessory.materials = nullptr;
    accessory.texturePaths = nullptr;
    accessory.textureTypes = nullptr;
    accessory.visible = 1;
    accessory.position[0] = accessory.position[1] =
        accessory.position[2] = 0.0f;
    accessory.rotation[0] = accessory.rotation[1] =
        accessory.rotation[2] = 0.0f;
    accessory.scale = 1.0f;
    accessory.parentModel = -1;
    accessory.parentBone = 0;
    accessory.shadowEnabled = 0;
    accessory.order = 0;
    accessory.additiveBlend = 0;
    accessory.materialCount = 0;
    accessory.opacity = 1.0f;
    accessory.currentMaterial = -1;
    accessory.rowSelected = 0;
}

bool InitAxisMesh(MMDApp* app) {
    mdl::AccessoryRecord& gizmo = *mdl::Accessory(app->state.axisMeshObject);
    ReleaseCom(gizmo.mesh);
    gizmo.mesh = nullptr;

    HRSRC resource = FindResourceA(nullptr, MAKEINTRESOURCEA(0x73), "XFILE");
    DWORD size = SizeofResource(nullptr, resource);
    HGLOBAL loaded = LoadResource(nullptr, resource);
    void* data = LockResource(loaded);
    void* materialBuffer = nullptr;
    DWORD materialCount = 0;
    void* mesh = nullptr;
    D3DRenderer* sub = app->Renderer();
    auto* device = sub->device;
    auto& api = d3dx::Get();
    if (!api.Load() || api.loadMeshFromXInMemory(
            data, size, 544, device, nullptr, &materialBuffer, nullptr,
            &materialCount, &mesh) != D3D_OK) {
        MessageBoxA(nullptr, "failed load axis.x from memory!", "", MB_OK);
        return false;
    }

    gizmo.mesh = mesh;
    gizmo.materialCount = materialCount;
    auto* materials = static_cast<D3DMATERIAL9*>(
        ::operator new(68 * materialCount));
    gizmo.materials = materials;
    if (materials == nullptr)
        return false;
    auto getBufferPointer = reinterpret_cast<BufferPointer>(
        (*reinterpret_cast<void***>(materialBuffer))[3]);
    auto* source = static_cast<unsigned char*>(
        getBufferPointer(materialBuffer));
    for (DWORD i = 0; i < materialCount; ++i)
        std::memcpy(&materials[i], source + 72 * i, 68);
    ReleaseCom(materialBuffer);
    return true;
}

void DrawPhysicsCollisionDebug(PhysicsScene* scene) {
    auto* world = scene->world;   // slot 64 / 0x40
    SetDebugColor(scene, 255, 0, 0);
    for (int pass = 0; pass < 2; ++pass) {
        if (pass == 1)
            SetDebugColor(scene, 155, 255, 0);
        const int count = world->getNumCollisionObjects();
        for (int i = 0; i < count; ++i) {
            btCollisionObject* object = world->getCollisionObjectArray()[i];
            btRigidBody* body = btRigidBody::upcast(object);
            if (body == nullptr ||
                ((body->getInvMass() != 0.0f) != (pass == 0)))
                continue;
            btCollisionShape* shape = body->getCollisionShape();
            Matrix transform;
            BulletTransformToMatrix(body->getWorldTransform(), &transform);
            if (shape->getShapeType() == SPHERE_SHAPE_PROXYTYPE) {
                auto* sphere = static_cast<btSphereShape*>(shape);
                DrawSphere(scene, sphere->getRadius(), transform);
            } else if (shape->getShapeType() == BOX_SHAPE_PROXYTYPE) {
                const btVector3 extents =
                    static_cast<btBoxShape*>(shape)->getHalfExtentsWithMargin();
                DrawBox(scene, extents.x(), extents.y(), extents.z(),
                        transform);
            } else if (shape->getShapeType() == CAPSULE_SHAPE_PROXYTYPE) {
                auto* capsule = static_cast<btCapsuleShape*>(shape);
                DrawCapsule(scene, capsule->getRadius(),
                            capsule->getHalfHeight(), transform);
            }
        }
    }
}

// Physics-editor scratch-array capacity (must track the dialog's own
// kMaxEditRecords: x86 original 10,000 slots, x64 recompile 100,000).
#if defined(_M_X64)
constexpr int kDebugRecordCapacity = 100000;
#else
constexpr int kDebugRecordCapacity = 10000;
#endif

void DrawAccessoryDebug(MMDApp* app) {
    auto& api = d3dx::Get();
    PhysicsScene* scene = app->Physics();   // +650672 (kPtrSub048)
    // The physics-model editor's scratch arrays double as the draw source:
    // rigidScratchArray = rigid bodies (mdl::RigidRecord), jointScratchArray =
    // joints (mdl::JointRecord).  While the dialog is open the records'
    // keyData / constraint slots hold the combo index (-1 = deleted slot).
    auto* bodies =
        static_cast<mikudancestudio::mdl::RigidRecord*>(
            app->state.rigidScratchArray);
    auto* joints =
        static_cast<mikudancestudio::mdl::JointRecord*>(
            app->state.jointScratchArray);
    const bool dialogSelection = app->state.physicsEditorJointPage != 0;
    for (int index = 0; index < kDebugRecordCapacity; ++index) {
        mikudancestudio::mdl::RigidRecord* record = &bodies[index];
        const int comboSlot =
            static_cast<int>(reinterpret_cast<std::intptr_t>(record->keyData));
        if (comboSlot >= 0) {
            Matrix world;
            Matrix temp;
            api.rotZ(&world, record->rotation[2]);
            api.rotX(&temp, record->rotation[0]);
            api.multiply(&world, &world, &temp);
            api.rotY(&temp, record->rotation[1]);
            api.multiply(&world, &world, &temp);
            api.translation(&temp, record->position[0],
                            record->position[1], record->position[2]);
            api.multiply(&world, &world, &temp);
            AttachToBone(app, &world, record->boneIndex);

            bool selected = false;
            if (dialogSelection) {
                const int linkIndex = app->state.selectedJointIndex;
                if (linkIndex >= 0) {
                    const mikudancestudio::mdl::JointRecord* link =
                        &joints[linkIndex];
                    selected = index == link->rigidA ||
                               index == link->rigidB;
                }
            } else {
                selected = index == app->state.selectedRigidIndex;
            }
            if (selected)
                SetDebugColor(scene, 255, dialogSelection ? 75 : 0,
                              dialogSelection ? 75 : 0);

            const std::uint8_t type = record->shape;
            if (type == 0)
                DrawSphere(scene, record->size[0], world);
            else if (type == 1)
                DrawBox(scene, record->size[0], record->size[1],
                        record->size[2], world);
            else
                DrawCapsule(scene, record->size[0],
                            record->size[1] * 0.5f, world);
            if (selected)
                SetDebugColor(scene, 155, 255, 0);
        }

        mikudancestudio::mdl::JointRecord* link = &joints[index];
        if (link->constraint < 0)
            continue;
        Matrix world;
        Matrix temp;
        api.rotZ(&world, link->rotation[2]);
        api.rotX(&temp, link->rotation[0]);
        api.multiply(&world, &world, &temp);
        api.scaling(&temp, 1.5f, 1.5f, 1.5f);
        api.multiply(&world, &temp, &world);
        api.rotY(&temp, link->rotation[1]);
        api.multiply(&world, &world, &temp);
        api.translation(&temp, link->position[0], link->position[1],
                        link->position[2]);
        api.multiply(&world, &world, &temp);
        auto* device = DeviceFromScene(scene);
        Matrix oldWorld;
        device->GetTransform(D3DTS_WORLD,
                             reinterpret_cast<D3DMATRIX*>(&oldWorld));
        api.multiply(&world, &world, &oldWorld);
        device->SetTransform(D3DTS_WORLD,
                             reinterpret_cast<const D3DMATRIX*>(&world));
        if (dialogSelection && index == app->state.selectedJointIndex) {
            device->SetRenderState(D3DRS_ZENABLE, TRUE);
            device->SetRenderState(D3DRS_LIGHTING, TRUE);
            device->Clear(0, nullptr, D3DCLEAR_ZBUFFER, 0x00FFFFFF, 1.0f, 0);
            DrawAxisMesh(app->state.axisMeshObject, app);
            device->SetRenderState(D3DRS_ZENABLE, FALSE);
            device->SetRenderState(D3DRS_LIGHTING, FALSE);
        } else {
            DrawSelectionBox(scene);
        }
        device->SetTransform(D3DTS_WORLD,
                             reinterpret_cast<const D3DMATRIX*>(&oldWorld));
    }
}

void DrawBoneOperationAxis(MMDApp* app, const float frameMatrix[16]) {
    unsigned char* model = CurrentModel(app);
    const int selected = mikudancestudio::mdl::Mdl(model)->selectedBone;
    if (selected < 0)
        return;
    auto* bones = mikudancestudio::mdl::Bones(model);
    mikudancestudio::mdl::BoneRecord* bone = &bones[selected];
    const mdl::BoneType type = bone->type;
    if (type == mdl::BoneType::FixedAxis ||
        (type == mdl::BoneType::UnderIk &&
         (bone->flags & mdl::kBoneFlagFixedAxis) == mdl::kBoneFlagFixedAxis))
        return;
    const int operation = static_cast<int>(app->ViewportToolOperation());
    const int mode = static_cast<int>(app->InteractionDragMode());
    if (!((operation >= 9 && operation <= 14) ||
          (mode >= 4 && mode <= 7) || (mode >= 10 && mode <= 12)))
        return;

    auto& api = d3dx::Get();
    Matrix world;
    Matrix translation;
    api.translation(&translation, bone->position[0], bone->position[1],
                    bone->position[2]);
    if (app->state.coordinateSystem == 1) {
        api.multiply(&world, &translation, BoneMatrix(bone));
        const float x = world.m[3][0];
        const float y = world.m[3][1];
        const float z = world.m[3][2];
        Identity(&world);
        world.m[3][0] = x;
        world.m[3][1] = y;
        world.m[3][2] = z;
    } else {
        Matrix basis;
        BoneLocalAxes(app, &basis.m[0][0]);
        api.multiply(&world, &basis, &translation);
        api.multiply(&world, &world, BoneMatrix(bone));
    }
    const auto* frame = reinterpret_cast<const Matrix*>(frameMatrix);
    api.multiply(&world, &world, frame);

    const float px = bone->matInit[0] * bone->position[0] +
                     bone->matInit[4] * bone->position[1] +
                     bone->matInit[8] * bone->position[2] +
                     bone->matInit[12];
    const float py = bone->matInit[1] * bone->position[0] +
                     bone->matInit[5] * bone->position[1] +
                     bone->matInit[9] * bone->position[2] +
                     bone->matInit[13];
    const float pz = bone->matInit[2] * bone->position[0] +
                     bone->matInit[6] * bone->position[1] +
                     bone->matInit[10] * bone->position[2] +
                     bone->matInit[14];
    const float pw = bone->matInit[3] * bone->position[0] +
                     bone->matInit[7] * bone->position[1] +
                     bone->matInit[11] * bone->position[2] +
                     bone->matInit[15];
    const float tx = px * frame->m[0][0] + py * frame->m[1][0] +
                     pz * frame->m[2][0] + pw * frame->m[3][0];
    const float ty = px * frame->m[0][1] + py * frame->m[1][1] +
                     pz * frame->m[2][1] + pw * frame->m[3][1];
    const float tz = px * frame->m[0][2] + py * frame->m[1][2] +
                     pz * frame->m[2][2] + pw * frame->m[3][2];
    const float dx = tx - app->ViewOffsetX();
    const float dy = ty - app->ViewOffsetY();
    const float dz = tz - app->CameraDistance();
    const float scale = std::sqrt(dx * dx + dy * dy + dz * dz) *
                        (app->CameraFov() * 0.001f);
    Matrix scaleMatrix;
    api.scaling(&scaleMatrix, scale, scale, scale);
    api.multiply(&world, &scaleMatrix, &world);
    auto* device = DeviceFromScene(app->Physics());
    device->SetTransform(D3DTS_WORLD,
                         reinterpret_cast<const D3DMATRIX*>(&world));
    device->Clear(0, nullptr, D3DCLEAR_ZBUFFER, 0x00FFFFFF, 1.0f, 0);
    DrawAxisMesh(app->state.axisMeshObject, app);
}

void SetupFrameWorldTransform(MMDApp* app) {
    auto& api = d3dx::Get();
    Matrix translation;
    Matrix rotationX;
    Matrix rotationY;
    Matrix rotationZ;
    Matrix rotation;
    Matrix attachment;
    Identity(&attachment);

    // Display mode translates the world by the negative camera target.
    // A selected follow bone replaces that target with its world position.
    float focusX = 0.0f;
    float focusY = 0.0f;
    float focusZ = 0.0f;
    const int targetModel = app->CameraParentModel();
    const int targetBone = app->CameraParentBone();
    mdl::BoneRecord* target = nullptr;
    if (app->state.optflag[0] != 0 &&
        targetModel < 0) {
        focusX = app->CameraPositionX();
        focusY = app->CameraPositionY();
        focusZ = app->CameraPositionZ();
    } else if (targetModel >= 0) {
        auto* model = app->ModelSlot(targetModel);
        auto* bones = mikudancestudio::mdl::Bones(model);
        target = &bones[targetBone];
        const float x = target->position[0];
        const float y = target->position[1];
        const float z = target->position[2];
        focusX = target->matInit[0] * x + target->matInit[4] * y +
                 target->matInit[8] * z + target->matInit[12];
        focusY = target->matInit[1] * x + target->matInit[5] * y +
                 target->matInit[9] * z + target->matInit[13];
        focusZ = target->matInit[2] * x + target->matInit[6] * y +
                 target->matInit[10] * z + target->matInit[14];
        app->ViewOffsetX() = app->CameraPositionX();
        app->ViewOffsetY() = app->CameraPositionY();
        app->CameraDistance() = app->CameraPositionZ();
    }
    api.translation(&translation, -focusX, -focusY, -focusZ);

    api.rotY(&rotationY, app->CameraYaw());
    api.rotX(&rotationX, app->CameraPitch());
    api.multiply(&rotation, &rotationY, &rotationX);
    api.rotZ(&rotationZ, app->CameraRoll());
    api.multiply(&rotation, &rotation, &rotationZ);

    if (app->state.followCameraEnabled != 0 ||
        app->state.optflag[0] != 0) {
        if (target != nullptr &&
            app->CameraAttachmentTransformSuppressed() == 0) {
            std::memcpy(&attachment, BoneMatrix(target), sizeof(attachment));
            attachment.m[0][3] = 0.0f;
            attachment.m[1][3] = 0.0f;
            attachment.m[2][3] = 0.0f;
            attachment.m[3][0] = 0.0f;
            attachment.m[3][1] = 0.0f;
            attachment.m[3][2] = 0.0f;
            attachment.m[3][3] = 1.0f;
        }
    } else {
        std::memcpy(&attachment, &app->CameraAttachmentBasis(),
                    sizeof(attachment));
    }
    api.multiply(&rotation, &attachment, &rotation);
    std::memcpy(&app->ViewRotationTransform(), &rotation, sizeof(rotation));

    const float* const timelineDirection = app->LightDirection();
    D3DVECTOR& deviceDirection = app->SceneLight().Direction;
    deviceDirection.x = timelineDirection[0] * rotation.m[0][0] +
                        timelineDirection[1] * rotation.m[1][0] +
                        timelineDirection[2] * rotation.m[2][0];
    deviceDirection.y = timelineDirection[0] * rotation.m[0][1] +
                        timelineDirection[1] * rotation.m[1][1] +
                        timelineDirection[2] * rotation.m[2][1];
    deviceDirection.z = timelineDirection[0] * rotation.m[0][2] +
                        timelineDirection[1] * rotation.m[1][2] +
                        timelineDirection[2] * rotation.m[2][2];

    D3DRenderer* sub = app->Renderer();
    auto* device = sub->device;
    device->SetLight(0, &app->SceneLight());
    Matrix world;
    api.multiply(&world, &translation, &rotation);
    device->SetTransform(D3DTS_WORLD,
                         reinterpret_cast<const D3DMATRIX*>(&world));

    // 0x46BC20..0x46BC9F: the alternate projection used by the orthographic
    // view mode.  The normal perspective projection remains the one written
    // by RefreshMainWindowViewport.
    if (app->state.cameraPerspective != 0) {
        Matrix projection{};
        projection.m[0][0] = -2.0f / app->CameraDistance();
        projection.m[1][1] =
            sub->aspectRatio *
            projection.m[0][0];
        projection.m[2][2] = 0.0013f;
        projection.m[3][3] = 1.0f;
        device->SetTransform(D3DTS_PROJECTION,
            reinterpret_cast<const D3DMATRIX*>(&projection));
    }

    // 0x46BCAA..0x46BD72: MMD keeps camera translation in the world matrix
    // and still installs this fixed +Z look-at view.  Omitting it leaves the
    // D3D identity view active, moving the horizon and clipping the Y axis.
    const float eye[3] = {
        app->ViewOffsetX(), app->ViewOffsetY(), app->CameraDistance()};
    const float at[3] = {eye[0], eye[1], eye[2] + 80.0f};
    const float up[3] = {0.0f, 1.0f, 0.0f};
    Matrix view;
    api.lookAtLH(&view, eye, at, up);
    device->SetTransform(D3DTS_VIEW,
                         reinterpret_cast<const D3DMATRIX*>(&view));
}

}  // namespace mikudancestudio
