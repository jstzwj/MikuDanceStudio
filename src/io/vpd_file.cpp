// ===========================================================================
// VA 0x00418750 - SaveVpdFile  (0x2B2 bytes)
// VA 0x00418A10 - LoadVpdFile  (0x95C bytes)
// ===========================================================================
// Both are __thiscall(app, path) on the app block; the port keeps the
// free-function shape used by the file-dialog command layer (g_Block), the
// same convention as LoadVmdFile (src/model/vmd_load.cpp).
//
// SAVE (0x418750): _wfopen(path, L"wt") then a fixed fprintf sequence with
// byte-exact .rdata formats (0x52BBDC..0x52BC6C):
//   "Vocaloid Pose Data file\n\n"
//   "%s.osm;\t\t// <SJIS 親ファイル名>"   arg = model+0x2248 name
//   "%d;\t\t\t\t// <SJIS 総ポーズボーン数>" arg = count of selected bones
//   per selected bone i (ModelRecord selection bitmap/count/BoneRecord table):
//     "Bone%d{%s\n"      running selected index, bone name at record +0
//     pose source pick (0x418860): bone->hasRigidBody != 0 && bone->physicsDisabled == 0 &&
//         physMode(app+0xA0CC4) >= 2  ->  physics pose
//         (+0x188 trans / +0x194 quat), else raw pose
//         (+0x140 trans / +0x14C quat)
//     "  %f,%f,%f;\t\t\t\t// trans x,y,z\n"
//     "  %f,%f,%f,%f;\t\t// Quaternion x,y,z,w\n"
//     "}\n\n"
//   fclose.  (floats are promoted to double by varargs, as in the original)
//
// LOAD (0x418A10): _wfopen_s(&fp, path, L"r"); 3 discarded fgets (header,
// blank, model line); fscanf(fp, "%d;", &count); 2 more discarded fgets
// (count-line comment tail + blank).  UI: EnableWindow(400,true)/
// EnableWindow(401,false) like the VMD loader.  Undo ring at model+0x31B4
// (30 slots of 0x1C at model+0x26EC; slot+0 = type 1 (pose), slot+4 = count,
// slot+16 = new(0x24*count) snapshot {int boneIdx, float3 pos, quat, flag}).
// Per VPD record: fgets(name line), strstr "{", sprintf_s(name),
// strchr(name,'\n')->0, fscanf "  %f,%f,%f;" (trans), fgets (comment tail),
// fscanf "  %f,%f,%f,%f;" (quat), then 3 discarded fgets (comment/}/blank).
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
#include "mikudancestudio/model.hpp"
#include "mikudancestudio/globals.hpp"
#include "mikudancestudio/mmd_app.hpp"
#include "mikudancestudio/panel_controls.hpp"

namespace mikudancestudio {
namespace {

// Active model = slot array at this+0x780 indexed by byte this+0x910.
unsigned char* ActiveModel(MMDApp* app) {
    return app->SelectedModel();
}

// ---- byte-exact .rdata strings --------------------------------------------
const char kHdr[] = "Vocaloid Pose Data file\n\n";               // 0x52BC6C
const char kModelFmt[] =                                         // 0x52BC50
    "%s.osm;\t\t// \x90\x65\x83\x74\x83\x40\x83\x43\x83\x8b\x96\xbc\n";
const char kCountFmt[] =                                         // 0x52BC30
    "%d;\t\t\t\t// \x91\x8d\x83\x7c\x81\x5b\x83\x59\x83\x7b\x81\x5b\x83"
    "\x93\x90\x94\n\n";
const char kBoneFmt[] = "Bone%d{%s\n";                           // 0x52BC24
const char kTransFmt[] =                                         // 0x52BC04
    "  %f,%f,%f;\t\t\t\t// trans x,y,z\n";
const char kQuatFmt[] =                                          // 0x52BBDC
    "  %f,%f,%f,%f;\t\t// Quaternion x,y,z,w\n";
const char kCloseFmt[] = "}\n\n";                                // 0x52BBD8
const wchar_t kModeWrite[] = L"wt";                              // 0x52BC88
const wchar_t kModeRead[] = L"r";                                // 0x52BCB4
const char kCountScan[] = "%d;";                                 // 0x52BCB0
const char kBrace[] = "{";                                       // 0x52BCAC
const char kTransScan[] = "  %f,%f,%f;";                         // 0x52BCA0
const char kQuatScan[] = "  %f,%f,%f,%f;";                       // 0x52BC90

}  // namespace

// ---- VA 0x00418750 --------------------------------------------------------
void SaveVpdFile(const wchar_t* path) {
    MMDApp* app = g_Block;
    if (app == nullptr) return;
    unsigned char* const model = ActiveModel(app);
    if (model == nullptr) return;

    FILE* fp = nullptr;
    // Original never checks the _wfopen_s result: on failure it runs
    // straight into fprintf(NULL, ...) (x64 0x7FF7CB48B73C open ->
    // 0x7FF7CB48B74E first fprintf) and crashes.  Port hardening: return
    // silently. (documented deviation)
    if (_wfopen_s(&fp, path, kModeWrite) != 0 || fp == nullptr) return;

    std::fprintf(fp, "%s", kHdr);
    mdl::ModelRecord& modelRecord = *mdl::Mdl(model);
    std::fprintf(fp, kModelFmt, modelRecord.name);

    const int boneCnt = modelRecord.boneCount;
    unsigned char* const selBits = modelRecord.boneSelection;
    int selected = 0;
    for (int i = 0; i < boneCnt; ++i)
        if (selBits[i] != 0) ++selected;
    std::fprintf(fp, kCountFmt, selected);

    mikudancestudio::mdl::BoneRecord* const bones = mikudancestudio::mdl::Bones(model);
    const int physMode = app->PlaybackPhysicsMode();
    int running = 0;
    for (int i = 0; i < boneCnt; ++i) {
        if (selBits[i] == 0) continue;
        mikudancestudio::mdl::BoneRecord* const bone = bones + i;
        std::fprintf(fp, kBoneFmt, running, bone->name);
        ++running;
        // 0x418860: physics-pose source only for flagged bones with the
        // physics mode >= 2; raw kinematic pose otherwise.
        const float* trans;
        const float* quat;
        if (bone->hasRigidBody != 0 && bone->physicsDisabled == 0 && physMode >= 2) {
            trans = bone->ikBackup;
            quat = bone->ikBackup + 3;
        } else {
            trans = bone->trans;
            quat = bone->rotQuat;
        }
        std::fprintf(fp, kTransFmt, trans[0], trans[1], trans[2]);
        std::fprintf(fp, kQuatFmt, quat[0], quat[1], quat[2], quat[3]);
        std::fprintf(fp, "%s", kCloseFmt);
    }
    std::fclose(fp);
}

// ---- VA 0x00418A10 --------------------------------------------------------
void LoadVpdFile(const wchar_t* path) {
    MMDApp* app = g_Block;
    if (app == nullptr) return;
    unsigned char* const model = ActiveModel(app);
    if (model == nullptr) return;

    FILE* fp = nullptr;
    if (_wfopen_s(&fp, path, kModeRead) != 0 || fp == nullptr) return;

    char line[256], name[256], tail[256];
    std::fgets(line, 0x100, fp);   // "Vocaloid Pose Data file"
    std::fgets(line, 0x100, fp);   // blank
    std::fgets(line, 0x100, fp);   // "<model>.osm;..."
    int count = 0;
    std::fscanf(fp, kCountScan, &count);
    std::fgets(tail, 0x100, fp);   // count-line comment tail
    std::fgets(tail, 0x100, fp);   // blank

    const HWND hwnd = static_cast<HWND>(app->Hwnd());
    EnableWindow(GetDlgItem(hwnd, panel::kUndoButton), TRUE);
    EnableWindow(GetDlgItem(hwnd, panel::kRedoButton), FALSE);

    // ---- undo ring (model+0x31B4 wraps at 30; slots at model+0x26EC) ---
    mdl::ModelRecord& modelRecord = *mdl::Mdl(model);
    modelRecord.undoDirty = 1;                          // 0x418AE9
    modelRecord.redoDirty = 0;                          // 0x418AF0
    int ring = ++mdl::Mdl(model)->undoState[0];
    if (ring >= 30) mdl::Mdl(model)->undoState[0] = 0;
    ring = mdl::Mdl(model)->undoState[0];
    mdl::Mdl(model)->undoState[1] = ring;

    mdl::UndoRecord& undo = mdl::Mdl(model)->undoRings[0].slots[ring];
    undo.operation = 1;
    undo.dirty = count;
    if (undo.bonePose != nullptr) {
        ::operator delete(undo.bonePose);
        undo.bonePose = nullptr;
    }
    mdl::BonePoseSnapshot* const snap =
        static_cast<mdl::BonePoseSnapshot*>(
            operator new(sizeof(mdl::BonePoseSnapshot) * count));
    undo.bonePose = snap;
    std::memset(snap, 0, sizeof(mdl::BonePoseSnapshot) * count);
    undo.dirty = count;

    const int boneCnt = modelRecord.boneCount;
    mikudancestudio::mdl::BoneRecord* const bones = mikudancestudio::mdl::Bones(model);
    unsigned char* const selBits = modelRecord.boneSelection;
    unsigned char* const regBits = modelRecord.bonePhysicsState;
    const bool pmx = modelRecord.physicsMode == 2;              // 0x418FC4

    for (int k = 0; k < count; ++k) {
        float trans[3] = {0, 0, 0};
        float quat[4] = {0, 0, 0, 1.0f};

        std::fgets(line, 0x100, fp);                    // "BoneN{name"
        const char* brace = std::strstr(line, kBrace);
        // Original has no NULL check here: it calls
        // sprintf_s(name, 0x100, v18 + 1) (x64 0x7FF7CB48BDC5) with the
        // format pointer at (char*)1 and crashes when the brace is missing.
        // Port hardening: skip the record. (documented deviation)
        if (brace == nullptr) continue;
        sprintf_s(name, 0x100, "%s", brace + 1);
        if (char* nl = strchr(name, 10)) *nl = 0;  // 0x507670 strchr

        std::fscanf(fp, kTransScan, &trans[0], &trans[1], &trans[2]);
        std::fgets(tail, 0x100, fp);                    // comment tail
        std::fscanf(fp, kQuatScan, &quat[0], &quat[1], &quat[2], &quat[3]);
        std::fgets(tail, 0x100, fp);                    // "}\n"
        std::fgets(tail, 0x100, fp);                    // "\n"
        std::fgets(tail, 0x100, fp);

        for (int i = 0; i < boneCnt; ++i) {
            mikudancestudio::mdl::BoneRecord* const bone = bones + i;
            if (std::strcmp(name, bone->name) != 0)
                continue;

            // snapshot the pre-load pose into the undo record (0x418E2D..)
            mdl::BonePoseSnapshot& rec = snap[k];
            rec.boneIndex = i;
            std::memcpy(rec.position, bone->trans, sizeof rec.position);
            std::memcpy(rec.rotation, bone->rotQuat, sizeof rec.rotation);
            rec.physicsDisabled = regBits[i];

            // twist correction - same algorithm as registrar 0x49D880
            // (0x418F5C): ((u16@&bone->flags & 0x400) && type==4) || type==8
            const mdl::BoneType btype = bone->type;
            if (btype == mdl::BoneType::FixedAxis ||
                (btype == mdl::BoneType::UnderIk &&
                 (bone->flags & mdl::kBoneFlagFixedAxis) ==
                     mdl::kBoneFlagFixedAxis)) {
                float boneAxis[3];
                if (pmx) {
                    std::memcpy(boneAxis, bone->axis, 12);
                } else {
                    const mdl::BoneRecord& tailBone = bones[bone->tailBone];
                    for (int c = 0; c < 3; ++c)
                        boneAxis[c] = tailBone.position[c] - bone->position[c];
                    auto* d3 = &d3dx::Get();
                    if (d3->module != nullptr)
                        d3->vec3Normalize(boneAxis, boneAxis);
                }
                auto* d3 = &d3dx::Get();
                if (d3->quatToAxisAngle != nullptr) {
                    float axisOut[3], angle;
                    d3->quatToAxisAngle(quat, axisOut, &angle);
                    if (!_finite(quat[3])) quat[3] = 1.0f;   // w (0x4190C4)
                    float sinHalf;
                    const float ww = quat[3] * quat[3];
                    if (ww <= 1.0f) {
                        sinHalf = sqrtf(1.0f - ww);     // 0x4190FD sqrt
                    } else {
                        quat[3] = 1.0f;
                        sinHalf = 0.0f;
                    }
                    const float dot = boneAxis[0] * axisOut[0] +
                                      boneAxis[1] * axisOut[1] +
                                      boneAxis[2] * axisOut[2];
                    const float nA = sqrtf(boneAxis[0] * boneAxis[0] +
                                           boneAxis[1] * boneAxis[1] +
                                           boneAxis[2] * boneAxis[2]);
                    const float nB = sqrtf(axisOut[0] * axisOut[0] +
                                           axisOut[1] * axisOut[1] +
                                           axisOut[2] * axisOut[2]);
                    const float cosT = dot / (nA * nB);
                    if (cosT < 0.0f) sinHalf = -sinHalf;    // 0x4191E2
                    quat[0] = sinHalf * axisOut[0];
                    quat[1] = sinHalf * axisOut[1];
                    quat[2] = sinHalf * axisOut[2];
                    if (d3->quatNormalize != nullptr)
                        d3->quatNormalize(quat, quat);
                }
            }

            selBits[i] = 1;                              // 0x419226
            regBits[i] = 1;
            std::memcpy(bone->trans, trans, 12);        // 0x419256
            std::memcpy(bone->rotQuat, quat, 16);
            // No break: 0x419240's bone loop keeps scanning after a match,
            // so EVERY bone whose name equals the VPD bone gets the pose.
        }
    }
    std::fclose(fp);
}

}  // namespace mikudancestudio
