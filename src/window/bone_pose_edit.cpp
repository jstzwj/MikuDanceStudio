// Selected bone-pose reset (control 495, x86 0x0047F3FA).
#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <Windows.h>

#include <cstdint>
#include <cstring>

#include "mikudancestudio/d3dx_dyn.hpp"
#include "mikudancestudio/mmd_app.hpp"
#include "mikudancestudio/model.hpp"
#include "mikudancestudio/ported_funcs.hpp"

namespace mikudancestudio {
namespace {
// The x64 original uses MULSS/ADDSS at each step; x86 retains its existing
// double intermediates. Do not combine the two evaluation paths.
#if defined(_M_X64) || defined(__x86_64__)
using ResetPositionScalar = float;
#else
using ResetPositionScalar = double;
#endif
} // namespace

void ResetSelectedBonePose(MMDApp* app) {
    PushBoneEditUndo(app); // prep (thiscall)
    mdl::ModelRecord* model = app->SelectedModel();
    unsigned char* selected = model->boneSelection;
    if (selected[0] != 0) {
        // Bone 0: clear Y and reset the quaternion; X/Z stay unchanged.
        // Original stores: 0x144, 0x14C..0x154 = 0,
        // 0x158 = 1; the x87 0.0/1.0 pair is kept live across the
        // stores in the original
        auto& root = model->boneTable[0];
        root.trans[1] = 0.0f;
        root.rotQuat[0] = 0.0f;
        root.rotQuat[1] = 0.0f;
        root.rotQuat[2] = 0.0f;
        root.rotQuat[3] = 1.0f;
    }
    const std::int32_t boneCount = model->boneCount;
    unsigned char* editFlags = model->bonePhysicsState;
    for (std::int32_t i = 1; i < boneCount; ++i) {
        model = app->SelectedModel();
        selected = model->boneSelection;
        editFlags = model->bonePhysicsState;
        if (selected[i] == 0) {
            continue;
        }
        mikudancestudio::mdl::BoneRecord* const bones = model->boneTable;
        mikudancestudio::mdl::BoneRecord* bone = &bones[i];
        if (bone->parent == -1) {
            // ---- no parent: bake via own quaternion ----------------
            d3dx::D3DXMATRIXF mat{};
            auto* d3dx = &d3dx::Get();
            { d3dx->matrixRotationQuaternion(&mat, bone->rotQuat); }
            const ResetPositionScalar posX = bone->position[0];
            const ResetPositionScalar posY = bone->position[1];
            const ResetPositionScalar posZ = bone->position[2];
            // var_9CC = _11*x + _21*y + _31*z + _41 - x + frame0.x
#if defined(_M_X64) || defined(__x86_64__)
            ResetPositionScalar resetPositionX = posX * mat.m[0][0];
            resetPositionX += posY * mat.m[1][0];
#else
            ResetPositionScalar resetPositionX = posY * mat.m[1][0];
            resetPositionX += posX * mat.m[0][0];
#endif
            resetPositionX += posZ * mat.m[2][0];
            resetPositionX += mat.m[3][0];
            resetPositionX -= posX;
            resetPositionX += bones[0].trans[0];
            // var_9C8 = _12*x + _22*y + _32*z + _42 - y + frame0.y
#if defined(_M_X64) || defined(__x86_64__)
            ResetPositionScalar resetPositionY = posX * mat.m[0][1];
            resetPositionY += posY * mat.m[1][1];
#else
            ResetPositionScalar resetPositionY = posY * mat.m[1][1];
            resetPositionY += posX * mat.m[0][1];
#endif
            resetPositionY += posZ * mat.m[2][1];
            resetPositionY += mat.m[3][1];
            resetPositionY -= posY;
            resetPositionY += bones[0].trans[1];
            // var_9C4 = _13*x + _23*y + _33*z + _43 - z + frame0.z
#if defined(_M_X64) || defined(__x86_64__)
            ResetPositionScalar resetPositionZ = posX * mat.m[0][2];
            resetPositionZ += posY * mat.m[1][2];
#else
            ResetPositionScalar resetPositionZ = posY * mat.m[1][2];
            resetPositionZ += posX * mat.m[0][2];
#endif
            resetPositionZ += posZ * mat.m[2][2];
            resetPositionZ += mat.m[3][2];
            resetPositionZ -= posZ;
            resetPositionZ += bones[0].trans[2];
            // Compare against the current stored pose.
            const int positionXChanged = (bone->trans[0] != resetPositionX) ? 1 : 0;
            const int positionYChanged = (bone->trans[1] != resetPositionY) ? 1 : 0;
            const int positionZChanged = (bone->trans[2] != resetPositionZ) ? 1 : 0;
            const int rotationXChanged = (bone->rotQuat[0] != bones[0].rotQuat[0]) ? 1 : 0;
            const int rotationYChanged = (bone->rotQuat[1] != bones[0].rotQuat[1]) ? 1 : 0;
            const int rotationZChanged = (bone->rotQuat[2] != bones[0].rotQuat[2]) ? 1 : 0;
            const int rotationWChanged = (bone->rotQuat[3] != bones[0].rotQuat[3]) ? 1 : 0;
            if ((positionXChanged | positionYChanged | positionZChanged | rotationXChanged |
                 rotationYChanged | rotationZChanged | rotationWChanged) != 0) {
                bone->trans[0] = static_cast<float>(resetPositionX);
                bone->trans[1] = static_cast<float>(resetPositionY);
                bone->trans[2] = static_cast<float>(resetPositionZ);
                // Root quaternion copied into this bone's pose.
                memcpy(bone->rotQuat, bones[0].rotQuat, sizeof bone->rotQuat);
                editFlags[i] = 1;
            }
        } else {
            // ---- has parent: identity written ----------------------
            // comparisons are made against 0.0 for pos + quat x/y/z and
            // 1.0 for quat w (the x87 keeps a live [1.0, 0.0] pair)
            const int positionXChanged = (bone->trans[0] != 0.0) ? 1 : 0;
            const int positionYChanged = (bone->trans[1] != 0.0) ? 1 : 0;
            const int positionZChanged = (bone->trans[2] != 0.0) ? 1 : 0;
            const int rotationXChanged = (bone->rotQuat[0] != 0.0) ? 1 : 0;
            const int rotationYChanged = (bone->rotQuat[1] != 0.0) ? 1 : 0;
            const int rotationZChanged = (bone->rotQuat[2] != 0.0) ? 1 : 0;
            const int rotationWChanged = (bone->rotQuat[3] != 1.0) ? 1 : 0;
            if ((positionXChanged | positionYChanged | positionZChanged | rotationXChanged |
                 rotationYChanged | rotationZChanged | rotationWChanged) != 0) {
                bone->trans[0] = 0.0f;
                bone->trans[1] = 0.0f;
                bone->trans[2] = 0.0f;
                bone->rotQuat[0] = 0.0f;
                bone->rotQuat[1] = 0.0f;
                // x64 RVA 0x405AA clears quaternion Z too (+0x15C).
                bone->rotQuat[2] = 0.0f;
                bone->rotQuat[3] = 1.0f;
                editFlags[i] = 1;
            }
        }
    }
    PostViewRefresh(app);
}

} // namespace mikudancestudio
