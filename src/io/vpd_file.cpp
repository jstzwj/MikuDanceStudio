// ===========================================================================
// VA 0x00418750 - SaveVpdFile  (0x2B2 bytes)
// VA 0x00418A10 - LoadVpdFile  (0x95C bytes)
// ===========================================================================
// Both are __thiscall(app, path) on the app block; the port keeps the
// free-function shape used by the file-dialog command layer (g_Block), the
// same convention as LoadVmdFile (src/model/vmd_load.cpp).
//
// SAVE (0x418750): _wfopen(path, L"wt") then a fixed fprintf sequence with
// byte-exact .rdata formats (0x52BBDC..0x52BC6C). The format writer now
// performs that sequence from a document populated by this Windows adapter:
//   "Vocaloid Pose Data file\n\n"
//   "%s.osm;\t\t// <SJIS 親ファイル名>"   arg = model+0x2248 name
//   "%d;\t\t\t\t// <SJIS 総ポーズボーン数>" arg = count of selected bones
//   per selected bone i (ModelRecord selection bitmap/count/BoneRecord table):
//     "Bone%d{%s\n"      running selected index, bone name at record +0
//     pose source pick (0x418860): (bone->hasRigidBody & 1) != 0 && bone->physicsDisabled == 0 &&
//         physMode(app+0xA0CC4) >= 2  ->  physics pose
//         (+0x188 trans / +0x194 quat), else raw pose
//         (+0x140 trans / +0x14C quat)
//     "  %f,%f,%f;\t\t\t\t// trans x,y,z\n"
//     "  %f,%f,%f,%f;\t\t// Quaternion x,y,z,w\n"
//     "}\n\n"
//   fclose.  (floats are promoted to double by varargs, as in the original)
//
// LOAD (0x418A10): the original parsed FILE* text inline; the format reader
// now validates it before this adapter changes model or UI state. UI:
// EnableWindow(400,true)/
// EnableWindow(401,false) like the VMD loader.  Undo ring at model+0x31B4
// (30 slots of 0x1C at model+0x26EC; slot+0 = type 1 (pose), slot+4 = count,
// slot+16 = new(0x24*count) snapshot {int boneIdx, float3 pos, quat, flag}).
// Name matched byte-wise against every model bone; on hit: snapshot the old
// pose into undo, twist-correct type-8 / type-4+0x400 bones exactly like the
// key registrar 0x49D880 (axis = PMX bone->axis / PMD tail-bone rest delta),
// write trans -> bone->trans, quat -> bone->rotQuat, and set both the
// selection and physics-state bytes to 1.
// =========================================================================//
#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <Windows.h>

#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>

#include "mikudancestudio/d3dx_dyn.hpp"
#include "mikudancestudio/bone_axis.hpp"
#include "mikudancestudio/model.hpp"
#include "mikudancestudio/globals.hpp"
#include "mikudancestudio/mmd_app.hpp"
#include "mikudancestudio/panel_controls.hpp"
#include "mikudancestudio/vpd_format.hpp"

namespace mikudancestudio {
namespace {

// Active model = slot array at this+0x780 indexed by byte this+0x910.
mdl::ModelRecord* ActiveModel(MMDApp* app) {
    return app->SelectedModel();
}

const wchar_t kModeWrite[] = L"wt"; // 0x52BC88
const wchar_t kModeRead[] = L"r";   // 0x52BCB4

} // namespace

// ---- VA 0x00418750 --------------------------------------------------------
void SaveVpdFile(const wchar_t* path) {
    MMDApp* app = g_Block;
    if (app == nullptr)
        return;
    mdl::ModelRecord* const model = ActiveModel(app);
    if (model == nullptr)
        return;

    FILE* fp = nullptr;
    // Original never checks the _wfopen_s result: on failure it runs
    // straight into fprintf(NULL, ...) (x64 0x7FF7CB48B73C open ->
    // 0x7FF7CB48B74E first fprintf) and crashes.  Port hardening: return
    // silently. (documented deviation)
    if (_wfopen_s(&fp, path, kModeWrite) != 0 || fp == nullptr)
        return;

    mdl::ModelRecord& modelRecord = *model;
    format::VpdDocument document;
    document.modelName = modelRecord.name;

    const int boneCnt = modelRecord.boneCount;
    unsigned char* const selBits = modelRecord.boneSelection;
    mikudancestudio::mdl::BoneRecord* const bones = (model)->boneTable;
    const int physMode = app->PlaybackPhysicsMode();
    for (int i = 0; i < boneCnt; ++i) {
        if (selBits[i] == 0)
            continue;
        mikudancestudio::mdl::BoneRecord* const bone = bones + i;
        format::VpdBone pose;
        pose.name = bone->name;
        // 0x418860: physics-pose source only for flagged bones with the
        // physics mode >= 2; raw kinematic pose otherwise.
        const float* trans;
        const float* quat;
        if ((bone->hasRigidBody & 1) != 0 && bone->physicsDisabled == 0 && physMode >= 2) {
            trans = bone->ikBackup;
            quat = bone->ikBackup + 3;
        } else {
            trans = bone->trans;
            quat = bone->rotQuat;
        }
        std::memcpy(pose.translation, trans, sizeof pose.translation);
        std::memcpy(pose.rotation, quat, sizeof pose.rotation);
        document.bones.push_back(pose);
    }
    format::WriteVpd(fp, document);
    std::fclose(fp);
}

// ---- VA 0x00418A10 --------------------------------------------------------
void LoadVpdFile(const wchar_t* path) {
    MMDApp* app = g_Block;
    if (app == nullptr)
        return;
    mdl::ModelRecord* const model = ActiveModel(app);
    if (model == nullptr)
        return;

    FILE* fp = nullptr;
    if (_wfopen_s(&fp, path, kModeRead) != 0 || fp == nullptr)
        return;
    format::VpdDocument document;
    const bool parsed = format::ReadVpd(fp, document);
    std::fclose(fp);
    if (!parsed)
        return;
    const int count = static_cast<int>(document.bones.size());

    const HWND hwnd = static_cast<HWND>(app->Hwnd());
    EnableWindow(GetDlgItem(hwnd, panel::kUndoButton), TRUE);
    EnableWindow(GetDlgItem(hwnd, panel::kRedoButton), FALSE);

    // ---- undo ring (model+0x31B4 wraps at 30; slots at model+0x26EC) ---
    mdl::ModelRecord& modelRecord = *model;
    modelRecord.undoDirty = 1; // 0x418AE9
    modelRecord.redoDirty = 0; // 0x418AF0
    int ring = ++model->UndoCursor();
    if (ring >= mdl::kUndoHistoryDepth)
        model->UndoCursor() = 0;
    ring = model->UndoCursor();
    model->LatestUndoCursor() = ring;

    mdl::UndoRecord& undo = model->undoRings[0].slots[ring];
    undo.operation = mdl::UndoOperation::BonePose;
    undo.recordCount = count;
    if (undo.bonePose != nullptr) {
        ::operator delete(undo.bonePose);
        undo.bonePose = nullptr;
    }
    mdl::BonePoseSnapshot* const snap =
        static_cast<mdl::BonePoseSnapshot*>(operator new(sizeof(mdl::BonePoseSnapshot) * count));
    undo.bonePose = snap;
    std::memset(snap, 0, sizeof(mdl::BonePoseSnapshot) * count);
    undo.recordCount = count;

    const int boneCnt = modelRecord.boneCount;
    mikudancestudio::mdl::BoneRecord* const bones = (model)->boneTable;
    unsigned char* const selBits = modelRecord.boneSelection;
    unsigned char* const regBits = modelRecord.bonePhysicsState;
    const bool pmx = modelRecord.physicsMode == 2; // 0x418FC4

    for (int k = 0; k < count; ++k) {
        const format::VpdBone& pose = document.bones[k];
        float trans[3];
        float quat[4];
        std::memcpy(trans, pose.translation, sizeof trans);
        std::memcpy(quat, pose.rotation, sizeof quat);

        for (int i = 0; i < boneCnt; ++i) {
            mikudancestudio::mdl::BoneRecord* const bone = bones + i;
            if (std::strcmp(pose.name.c_str(), bone->name) != 0)
                continue;

            // snapshot the pre-load pose into the undo record (0x418E2D..)
            mdl::BonePoseSnapshot& rec = snap[k];
            rec.boneIndex = i;
            std::memcpy(rec.position, bone->trans, sizeof rec.position);
            std::memcpy(rec.rotation, bone->rotQuat, sizeof rec.rotation);
            rec.poseEdited = regBits[i];

            // twist correction - same algorithm as registrar 0x49D880
            // (0x418F5C): ((u16@&bone->flags & 0x400) && type==4) || type==8
            const mdl::BoneType btype = bone->type;
            if (btype == mdl::BoneType::FixedAxis ||
                (btype == mdl::BoneType::UnderIk &&
                 (bone->flags & mdl::kBoneFlagFixedAxis) == mdl::kBoneFlagFixedAxis)) {
                float boneAxis[3];
                if (pmx) {
                    std::memcpy(boneAxis, bone->axis, 12);
                } else {
                    const mdl::BoneRecord& tailBone = bones[bone->tailBone];
                    for (int c = 0; c < 3; ++c)
                        boneAxis[c] = tailBone.position[c] - bone->position[c];
                    auto* d3 = &d3dx::Get();
                    d3->vec3Normalize(boneAxis, boneAxis);
                }
                mdl::ConstrainBoneRotationAxis(quat, boneAxis);
            }

            selBits[i] = 1; // 0x419226
            regBits[i] = 1;
            std::memcpy(bone->trans, trans, 12); // 0x419256
            std::memcpy(bone->rotQuat, quat, 16);
            // No break: 0x419240's bone loop keeps scanning after a match,
            // so EVERY bone whose name equals the VPD bone gets the pose.
        }
    }
}

} // namespace mikudancestudio
