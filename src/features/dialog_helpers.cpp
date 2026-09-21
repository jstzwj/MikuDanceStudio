// ===========================================================================
// dialog_helpers.cpp - remaining dialog/feature gap functions (round N)
// ===========================================================================
// Ported here (Hex-Rays pseudocode is ground truth; x87 operand order and
// store rounding are reproduced with explicit double intermediates wherever
// the original keeps values on the FPU stack):
//   0x004999E0  sign-corrected quaternion nlerp + normalize
//   0x0048F7C0  16-dword matrix copy into object+16 (thiscall)
//   0x004B2210  bone physics-mode broadcast over the rigid notify
//   0x004A8BC0  rolling vertex-position history push + average
//   0x004B75C0  24-channel model history push (dispatch of 0x4A8BC0)
//   0x00490230  PMD model writer (thiscall(model, path, locale table))
//   0x004B5760  per-frame standard-pose setup + physics trace recorder
//
// Already covered elsewhere (verified this round - NOT re-ported):
//   0x004B0C50  UpdateModelVertexBuffers - src/model/model_skinning.cpp:318,
//               declared include/mikudancestudio/ported_funcs.hpp:133
//   0x004912F0  absorbed into src/render/model_renderers.cpp:
//               ConfigureMaterialStages (:430, texture cascade 0x491470..
//               0x491B38/0x491E41..0x492163), DrawModelMaterials (:817,
//               blend prefix + cull/draw branches 0x4921D1..0x4924CE),
//               ResetSphereStageAfterDraw (:596, 0x4925CE..0x4925E5),
//               BuildToonTransform (:382, D3DX toon matrix block)
//   0x00492640  DrawModelEdgeGeometry - src/render/model_renderers.cpp:965
//               (edge-subset DrawIndexedPrimitive loop, EgColor effect pass)
//   0x00492860  shadow material pass absorbed into DrawModelMaterials
//               (shadowOnly path, src/render/model_renderers.cpp:844-848) and
//               reached from the RenderShadowMap port (:1392, 0x426CD0);
//               the skip test includes the original's PMX shadow-cast-bit
//               term (~(flags>>2) & pmx, 0x492914..0x492927) in
//               model_renderers.cpp DrawModelMaterials.
// ===========================================================================
#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <Windows.h>

#include <cerrno>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <cwchar>
#include <io.h>
#include <new>
#include <share.h>
#include <sys/stat.h>

#include "mikudancestudio/charset_conv.hpp"
#include "mikudancestudio/globals.hpp"
#include "mikudancestudio/ported_funcs.hpp"
#include "mikudancestudio/model.hpp"

namespace mikudancestudio {

// VA 0x004A6520 - InitStandardSkeletonQuats: standard-pose
// quaternion derivation from the leg/arm bone
// chain (fills model+64..332).  Defined in src/features/audio_pose_helpers.cpp
// (same namespace/build); declared here for the 0x4B5760 call below.
// was: int 0x4A6520(unsigned char* model, unsigned char a2); with a
// "body not yet ported" note - stale, and the int return disagreed with the
// definition (ODR).  The original's eax is dead at its sole caller
// (0x4B5850; the next instruction is fld, no eax read), hence void.
void InitStandardSkeletonQuats(unsigned char* model, unsigned char flag);

namespace {

template <typename T>
inline T& At(unsigned char* p, std::size_t offset) {
    return *reinterpret_cast<T*>(p + offset);
}

template <typename T>
inline const T& At(const unsigned char* p, std::size_t offset) {
    return *reinterpret_cast<const T*>(p + offset);
}

}  // namespace

// ---------------------------------------------------------------------------
// VA 0x004999E0 - sign-corrected quaternion lerp + normalize ("nlerp").
// __stdcall(float out[4], float q0x, float q0y, float q0z, float q0w,
//           float q1x, float q1y, float q1z, float q1w, float t) -> out.
// Callers: the 0x4A9400-family OpenMP skinning workers (ported in
// src/model/model_skinning.cpp behind the 0x4B0C50 dispatcher).  All arithmetic runs on the x87 stack: products and
// differences are computed in double and truncated only at each float store,
// which the explicit double temporaries below reproduce.
// ---------------------------------------------------------------------------
float* QuaternionNlerp(float* out, float q0x, float q0y, float q0z, float q0w,
                       float q1x, float q1y, float q1z, float q1w, float t) {
    const double dot =
        (double)q1x * (double)q0x + (double)q1w * (double)q0w +
        (double)q1y * (double)q0y + (double)q1z * (double)q0z;  // 0x499A23
    const float dotF = (float)dot;
    float tt;                                                   // st(6) mirror
    if (dotF >= 0.0f) {                                         // 0x499A36
        tt = t;
        out[3] = (float)((double)q0w + ((double)q1w - (double)q0w) * (double)t);
        out[0] = (float)((double)q0x + ((double)q1x - (double)q0x) * (double)t);
        out[1] = (float)((double)q0y + ((double)q1y - (double)q0y) * (double)t);
        out[2] = (float)((double)q0z + (double)t * ((double)q1z - (double)q0z));
    } else {
        const float nt = -t;                                    // 0x499A3E
        tt = nt;
        out[3] = (float)((double)q0w + ((double)q1w + (double)q0w) * (double)nt);
        out[0] = (float)((double)q0x + ((double)q1x + (double)q0x) * (double)nt);
        out[1] = (float)((double)q0y + ((double)q1y + (double)q0y) * (double)nt);
        out[2] = (float)((double)q0z + (double)nt * ((double)q1z + (double)q0z));
    }
    const float norm = (float)((double)out[1] * (double)out[1] +
                               (double)out[0] * (double)out[0] +
                               (double)out[2] * (double)out[2] +
                               (double)out[3] * (double)out[3]);  // 0x499ADE
    const float len = (float)std::sqrt((double)norm);            // 0x499AEB
    if (len <= 0.0f) {                                          // 0x499B06
        out[3] = 1.0f;
        out[2] = 0.0f;
        out[1] = 0.0f;
        out[0] = 0.0f;
        return out;
    }
    const float inv = (float)(1.0 / (double)len);                // 0x499B0E
    out[0] = out[0] * inv;
    out[1] = out[1] * inv;
    out[2] = out[2] * inv;
    out[3] = inv * out[3];                                      // operand order
    return out;
}

// ---------------------------------------------------------------------------
// VA 0x0048F7C0 - copy a 4x4 matrix into this+16 (16 dwords, plain copy).
// __thiscall(receiver, const float matrix[16]) -> eax = last element.
// Callers (unported): 0x4DD7C0 (3 sites), 0x4FC760.  The receiver object is
// the one carrying its transform matrix at offset +16.
// ---------------------------------------------------------------------------
void CopyMatrixToObject(unsigned char* self, const void* matrix16) {
    std::memcpy(self + 16, matrix16, 16 * 4);
}

// ---------------------------------------------------------------------------
// VA 0x004B2210 - broadcast the physics view mode to every bone-flagged
// rigid notify.  __thiscall(model, int mode) where mode is app+0xA0CC4
// (view menu 0x109/0x10D/0x10E/0x110).
//   mode == 0 : physics off  -> NotifyBonePhysicsMode(model, bone, 1)  (kinematic)
//   mode == 1 : physics on   -> NotifyBonePhysicsMode(model, bone, 0)  (dynamic)
//   mode >= 2 : per-bone flag at &bone->physicsDisabled selects 1/0
// Only bones with the flag at &bone->hasRigidBody participate.  Bones live at
// model+9916 (stride 604).  Note: the existing ModelKinematicSync
// (physics_frame.cpp, VA 0x4B22F0) is a different function - this body was
// missing before.  Callers: 0x450000 (v2 loader), 0x47E8A0 (command dispatch).
// ---------------------------------------------------------------------------
void ModelBonePhysicsModeApply(unsigned char* model, int mode) {
    const int boneCount = mikudancestudio::mdl::Mdl(model)->boneCount;        // 0x4B2218
    mikudancestudio::mdl::BoneRecord* bones = mikudancestudio::mdl::Bones(model);
    if (mode == 0) {
        for (int i = 0; boneCount > 0 && i < boneCount; ++i)     // 0x4B221A
            if (bones[i].hasRigidBody != 0)
                NotifyBonePhysicsMode(model, i, 1);                          // 0x4B2243
    } else if (mode == 1) {
        for (int i = 0; boneCount > 0 && i < boneCount; ++i)     // 0x4B2263
            if (bones[i].hasRigidBody != 0)
                NotifyBonePhysicsMode(model, i, 0);                          // 0x4B2283
    } else if (mode >= 2) {
        for (int i = 0; boneCount > 0 && i < boneCount; ++i) {   // 0x4B22A3
            mikudancestudio::mdl::BoneRecord* bone = &bones[i];
            if (bone->hasRigidBody != 0)
                NotifyBonePhysicsMode(model, i, bone->physicsDisabled != 0);             // 0x4B22D1
        }
    }
}

// ---------------------------------------------------------------------------
// VA 0x004A8BC0 - push a tracked position into its rolling history and
// re-average.  __stdcall(float current[3], float history[], int count).
//   1. shift the `count` entries (3 floats each) one slot toward index 0
//      (rep movsd in the original; memmove here - same byte result)
//   2. store `current` into the last slot
//   3. average every entry whose Y component != -999.0f and write the
//      average back into `current` (accumulate in float, one rounding per
//      add exactly like the fstp-per-iteration original; the 1/count and
//      product run through double).
// Sole caller: 0x4B75C0.
// ---------------------------------------------------------------------------
void VertexHistoryPushAverage(float* current, float* history, int count) {
    mdl::PushSkeletonJointHistory(current, history, count);
}

// ---------------------------------------------------------------------------
// VA 0x004B75C0 - advance the 24 model position-tracker channels.
// __thiscall(model, int samples) - samples clamped to 30; each channel pairs
// a 3-float "current" slot (model+14280..14556, float-index 3570..3636) with
// a rolling history at model+336.. (float-index 84..2064, 90-float stride;
// the first channel reserves a 180-float slot).  The dispatch order below is
// the original's scrambled order (0x4B75E3..0x4B77C7).  Sole caller: the
// FrameDriver 0x46FB4B site.
// ---------------------------------------------------------------------------
void ModelVertexHistoryPush(unsigned char* model, int samples) {
    if (model == nullptr || samples <= 0)
        return;
    const int n = samples > 30 ? 30 : samples;
    auto& state = *mdl::Mdl(model);
    for (std::size_t joint = 0; joint < mdl::kTrackedJointCount; ++joint)
        VertexHistoryPushAverage(state.currentJoints.positions[joint],
                                 state.skeletonHistory.positions[joint], n);
}

// ---------------------------------------------------------------------------
// VA 0x00490230 - PMD model writer.
// __thiscall(model, char* path, _locale_t* localeTable) -> errno_t-ish int;
// model+0 is the owner HWND for the error box.  The original writes the file
// through _sopen_s/_write/_close exactly in PMD 1.0 field order.  The locale
// table argument is only forwarded to the wide->SJIS converter (0x407910);
// Sole caller: 0x41EC10
// (the "save model file" command).
// ---------------------------------------------------------------------------
int SavePmdFile(unsigned char* model, char* path, void* localeTable) {
    mdl::ModelRecord& state = *mdl::Mdl(model);
    int fh = -1;
    const errno_t openErr = _sopen_s(&fh, path, 33537 /*_O_BINARY|_O_WRONLY|
                                   _O_CREAT|_O_TRUNC*/, 64 /*_SH_DENYNO*/,
                                     128 /*_S_IREAD*/);          // 0x49026F
    if (openErr != 0) {
        char text[256];
        if (mikudancestudio::mdl::Mdl(model)->physicsFlags != 0)                                   // English UI
            sprintf_s(text, 0x100u, "Cannot save file:%d", openErr);
        else
            sprintf_s(text, 0x100u,
                      "\203t\203C\203\213\202\252\225\333\221\266"
                      "\202\305\202\253\202\334\202\271\202\361:%d",
                      openErr);  // JP "cannot save file:%d" @0x52BCE4
                      // (octal escapes; one SJIS byte per escape)
        return MessageBoxA(*reinterpret_cast<HWND*>(model), text,
                           g_Locale, 0);                         // 0x4902D7
    }

    _write(fh, "Pmd", 3);                                        // 0x4902EC
    const float version = 1.0f;
    _write(fh, &version, 4);
    _write(fh, state.name, sizeof(state.name));                  // SJIS name
    _write(fh, state.comment, sizeof(state.comment));            // SJIS comment

    // Fold the base vertex morph back into the vertex positions so the
    // written PMD carries the current shape (0x490345..0x490395).
    if (mdl::Morphs(model) != nullptr && state.morph0Count != 0) {
        for (std::uint32_t i = 0; i < state.morph0Count; ++i) {
            const mdl::PmdVertexMorphEntry& entry = state.morph0Table[i];
            mdl::PmdVertex& vertex = mdl::PmdVertices(model)[entry.vertexIndex];
            vertex.position[0] = entry.offset[0];
            vertex.position[1] = entry.offset[1];
            vertex.position[2] = entry.offset[2];
        }
    }

    // Vertices (0x4903A2..0x4904F4): pos, normal, uv, bone0/bone1 u16,
    // weight u8, edge bool - read from the 40-byte runtime records.
    _write(fh, &state.vertexCount, sizeof(state.vertexCount));
    for (std::uint32_t i = 0; i < state.vertexCount; ++i) {
        const mdl::PmdVertex& vertex = mdl::PmdVertices(model)[i];
        _write(fh, vertex.position, sizeof(vertex.position));
        _write(fh, vertex.normal, sizeof(vertex.normal));
        _write(fh, vertex.uv, sizeof(vertex.uv));
        _write(fh, vertex.bone, sizeof(vertex.bone));
        _write(fh, &vertex.weightPercent, sizeof(vertex.weightPercent));
        const char edge = vertex.edgeDisabled != 0;               // 0x4904D3
        _write(fh, &edge, 1);
    }

    // Indices (0x490505): the original walks a DWORD-strided table
    // (lea (%edx,%edi,4)) and writes the low 16 bits of each element.
    _write(fh, &state.indexCount, sizeof(state.indexCount));
    for (std::uint32_t i = 0; i < state.indexCount; ++i) {
        const std::uint16_t index =
            static_cast<std::uint16_t>(mdl::PmxIndices(model)[i]);
        _write(fh, &index, sizeof(index));
    }

    // Materials (0x490547..0x49079C), 2292-byte records at model+32.
    _write(fh, &state.materialCount, sizeof(state.materialCount));
    for (std::uint32_t i = 0; i < state.materialCount; ++i) {
        const mdl::ModelMaterialRecord& mat = mdl::Materials(model)[i];
        _write(fh, &mat.diffuse[0], sizeof(mat.diffuse[0]));
        _write(fh, &mat.diffuse[1], sizeof(mat.diffuse[1]));
        _write(fh, &mat.diffuse[2], sizeof(mat.diffuse[2]));
        _write(fh, &mat.diffuse[3], sizeof(mat.diffuse[3]));  // 0x490598
        _write(fh, &mat.specularPower, sizeof(mat.specularPower));
        _write(fh, mat.specular, sizeof(mat.specular));
        _write(fh, mat.ambient, sizeof(mat.ambient));
        _write(fh, &mat.toonReference, sizeof(mat.toonReference));
        const char edgeFlag = mat.doubleSided != 0;              // 0x490660
        _write(fh, &edgeFlag, 1);
        _write(fh, &mat.faceVertexCount, sizeof(mat.faceVertexCount));

        // Texture path (20 SJIS bytes).  The wide name/sphere fields both
        // start with the model directory prefix; the save strips
        // wcslen(prefix) leading wchars before converting (0x490693..0x490778).
        const wchar_t* prefix = state.modelDirectory;
        const std::size_t skip = wcslen(prefix);
        wchar_t wide[20];
        char sjis[256];
        if (mat.spherePath[0] != 0) {                            // sphere used
            wcscpy_s(wide, 0x14u, mat.texturePath + skip);
            wcscat_s(wide, 0x14u, L"*");
            wcscat_s(wide, 0x14u, mat.spherePath + skip);
            WideToSjis(static_cast<D3DRenderer*>(localeTable), sjis, wide, 0x100);
        } else {
            const wchar_t* name = mat.texturePath;
            if (name[0] != 0)
                name += skip;
            WideToSjis(static_cast<D3DRenderer*>(localeTable), sjis, name, 0x100);
        }
        _write(fh, sjis, 0x14);
    }

    // Bones (0x4907B0..0x4908BC), 604-byte records at model+9916.
    _write(fh, &state.boneCount, 2);
    for (int i = 0; i < state.boneCount; ++i) {
        const mdl::BoneRecord* bone = &mdl::Bones(model)[i];
        _write(fh, bone->name, 0x14);
        _write(fh, &bone->parent, 2);
        _write(fh, &bone->tailBone, 2);
        _write(fh, &bone->type, 1);
        _write(fh, &bone->tailIdx, 2);
        _write(fh, bone->position, 4);
        _write(fh, &bone->position[1], 4);
        _write(fh, &bone->position[2], 4);
    }

    // IK lists (0x4908D0..0x4909BA), 24-byte records at model+9920.
    _write(fh, &state.ikChainCount, 2);
    for (int i = 0; i < state.ikChainCount; ++i) {
        const mdl::IkChain& ik = mdl::IkChains(model)[i];
        _write(fh, &ik.boneIndex, 2);
        _write(fh, &ik.targetBone, 2);
        _write(fh, &ik.linkCount, 1);
        _write(fh, &ik.iterations, 2);
        _write(fh, &ik.maxAngle, 4);
        for (unsigned char j = 0; j < ik.linkCount; ++j)
            _write(fh, &ik.links[j], 2);
    }

    // Morphs (0x4909CE..0x490B37), 136-byte records at model+9924; each
    // morph vertex index is remapped through the base morph's entry table
    // (value-identical in both paths, kept for structural fidelity).
    _write(fh, &state.morphCount, 2);
    for (int i = 0; i < static_cast<int>(state.morphCount); ++i) {
        const mdl::MorphRecord& morph = mdl::Morphs(model)[i];
        _write(fh, morph.name, sizeof(morph.name));
        _write(fh, &morph.offsetCount, sizeof(morph.offsetCount));
        _write(fh, &morph.panel, sizeof(morph.panel));
        if (morph.offsetCount == 0)
            continue;
        const mdl::MorphRecord& baseMorph = mdl::Morphs(model)[0];
        const std::uint32_t baseCount =
            static_cast<std::uint32_t>(baseMorph.offsetCount);
        for (std::uint32_t k = 0;
             k < static_cast<std::uint32_t>(morph.offsetCount); ++k) {
            const mdl::PmdVertexMorphEntry& entry = morph.vertexEntries[k];
            const std::uint32_t index = entry.vertexIndex;
            const mdl::PmdVertexMorphEntry* indexEntry = &entry;
            for (std::uint32_t j = 0; j < baseCount; ++j) {
                if (baseMorph.vertexEntries[j].vertexIndex == index) {
                    indexEntry = &baseMorph.vertexEntries[j];
                    break;
                }
            }
            _write(fh, &indexEntry->vertexIndex,
                   sizeof(indexEntry->vertexIndex));             // 0x490AA1
            _write(fh, entry.offset, sizeof(entry.offset));
        }
    }

    // Display frames / blend masters (0x490B3D..0x490C8E).  model+11692 is
    // the frame count byte, model+9940 the toon texture count byte.
    const unsigned char frameCount = state.facialFrameCount;
    _write(fh, &state.facialFrameCount, sizeof(state.facialFrameCount));
    for (unsigned char i = 0; i < frameCount; ++i)
        _write(fh, &state.displayFrames[i].targetIndex,
               sizeof(state.displayFrames[i].targetIndex));
    const char upper = static_cast<char>(
        state.groupCount - 2 - (frameCount != 0 ? 1 : 0));
    _write(fh, &upper, 1);
    if (frameCount != 0)
        for (unsigned char k = 3; k < state.groupCount; ++k)
            _write(fh, mdl::DisplayGroups(model)[k].name,
                   sizeof(mdl::DisplayGroups(model)[k].name));
    else
        for (unsigned char group = 2; group < state.groupCount; ++group)
            _write(fh, mdl::DisplayGroups(model)[group].name,
                   sizeof(mdl::DisplayGroups(model)[group].name));

    // Display groups (0x490C29..0x490C8E), 46-byte records at model+9944.
    _write(fh, &state.rigidBodyCount, sizeof(state.rigidBodyCount));
    for (std::uint32_t i = 0; i < state.rigidBodyCount; ++i) {
        const mdl::FrameGroup& record = mdl::RigidGroups(model)[i];
        const char count = static_cast<char>(record.groupIndex - 1 -
                                             (frameCount != 0 ? 1 : 0));
        _write(fh, &record.targetIndex, sizeof(record.targetIndex));
        _write(fh, &count, 1);
    }

    // English block (0x490CA0..0x490DEB).
    const char english = 1;
    _write(fh, &english, 1);
    _write(fh, state.nameEn, sizeof(state.nameEn));
    _write(fh, state.commentEn, sizeof(state.commentEn));
    for (int i = 0; i < mikudancestudio::mdl::Mdl(model)->boneCount; ++i)
        _write(fh, &mikudancestudio::mdl::Bones(model)[i].nameEn[0], 0x14);
    if (mikudancestudio::mdl::Mdl(model)->morphCount > 1)
        for (int i = 1; i < mikudancestudio::mdl::Mdl(model)->morphCount; ++i)
            _write(fh, mdl::Morphs(model)[i].nameEn,
                   sizeof(mdl::Morphs(model)[i].nameEn));
    if (frameCount != 0)
        for (unsigned char n = 3; n < state.groupCount; ++n)
            _write(fh, mdl::DisplayGroups(model)[n].nameEn,
                   sizeof(mdl::DisplayGroups(model)[n].nameEn));
    else
        for (unsigned char n = 2; n < state.groupCount; ++n)
            _write(fh, mdl::DisplayGroups(model)[n].nameEn,
                   sizeof(mdl::DisplayGroups(model)[n].nameEn));

    // Toon texture file names (0x490DCB): 10 x 100-byte PMD strings.
    for (int i = 0; i < 10; ++i)
        _write(fh, mdl::PmdToonFileNames(model)[i], 100);

    // Rigid bodies (0x490DFB..0x490FF2): name(20), bone(2)@+0x1C,
    // group(1)@+0x20, noc(2)@+0x22, shape(1)@+0x24, 14 floats
    // (+0x28..+0x4C, +0x58..+0x64), then 1 byte mode@+0x50.  No pointer
    // write exists in the original stream.
    _write(fh, &state.rigidCount, sizeof(state.rigidCount));
    for (int i = 0; i < state.rigidCount; ++i) {
        const mdl::RigidRecord& rigid = mdl::Rigids(model)[i];
        const std::int16_t boneIndex = static_cast<std::int16_t>(rigid.boneIndex);
        _write(fh, rigid.name, sizeof(rigid.name));
        _write(fh, &boneIndex, sizeof(boneIndex));
        _write(fh, &rigid.group, sizeof(rigid.group));
        _write(fh, &rigid.noCollapse, sizeof(rigid.noCollapse));
        _write(fh, &rigid.shape, sizeof(rigid.shape));
        _write(fh, rigid.size, sizeof(rigid.size));
        _write(fh, rigid.position, sizeof(rigid.position));
        _write(fh, rigid.rotation, sizeof(rigid.rotation));
        _write(fh, &rigid.mass, sizeof(rigid.mass));
        _write(fh, &rigid.linearDamping, sizeof(rigid.linearDamping));
        _write(fh, &rigid.angularDamping, sizeof(rigid.angularDamping));
        _write(fh, &rigid.restitution, sizeof(rigid.restitution));
        _write(fh, &rigid.friction, sizeof(rigid.friction));
        _write(fh, &rigid.mode, sizeof(rigid.mode));
    }

    // Joints (0x491006..0x4912A9).  limits[] intentionally follows the
    // original in-memory shuffle; this is the byte order of PMD output.
    _write(fh, &state.jointCount, sizeof(state.jointCount));
    for (int i = 0; i < state.jointCount; ++i) {
        const mdl::JointRecord& joint = mdl::Joints(model)[i];
        _write(fh, joint.name, sizeof(joint.name));
        _write(fh, &joint.rigidA, sizeof(joint.rigidA));
        _write(fh, &joint.rigidB, sizeof(joint.rigidB));
        _write(fh, joint.position, sizeof(joint.position));
        _write(fh, joint.rotation, sizeof(joint.rotation));

        constexpr int kPmdLimitOrder[] = {3, 4, 5, 0, 1, 2, 9, 10, 11, 6, 7, 8};
        for (int component : kPmdLimitOrder)
            _write(fh, &joint.limits[component], sizeof(joint.limits[component]));
        _write(fh, joint.springs, sizeof(joint.springs));
    }

    _close(fh);                                                  // 0x4912B4
    // The original casts the ANSI path pointer to wchar_t* here; the caller
    // passes a dual-purpose buffer, so the reinterpret_cast is faithful.
    return wcscpy_s(state.path, sizeof(state.path) / sizeof(state.path[0]),
                    reinterpret_cast<const wchar_t*>(path));
}

// ---------------------------------------------------------------------------
// VA 0x004B5760 - per-frame standard-pose setup + physics trace recorder.
// __thiscall(model, bool recordEnable(a2), char mirrorLeftRight(a3),
//             char skeletonFlag(a4)) -> char
// (returns 1 when recording was previously active and is now stopped).
// Callers: 0x46FBB9 in the FrameDriver - args are (app+0xA0D68 == 4),
// app+0xA03DC, app+0xA03DD.  Runs after InitStandardSkeletonQuats (standard-pose quaternions
// into model+64..332) and mirrors, per named standard bone, a quaternion
// (bone->trans..336) and position (bone->rotQuat..344) with left/right
// mirroring on the mirrorLeftRight flag; センター is additionally height-scaled.  While model+8616 is
// set, each processed bone appends a 308-byte record into the model+8620
// trace buffer at cursor model+14584.
// Bone-name constants are raw Shift-JIS from the original .rdata (compare
// lengths as in the binary; 0x52B7F4/0x52B80C compare 7 bytes, which
// includes one byte past the NUL - reproduced verbatim).
// ---------------------------------------------------------------------------
namespace {

const unsigned char kCenter[9] =                                 // 0x531184
    {0x83, 0x5A, 0x83, 0x93, 0x83, 0x5E, 0x81, 0x5B, 0x00};      // センター
const unsigned char kUpperBody[7] =                              // 0x53117C
    {0x8F, 0xE3, 0x94, 0xBC, 0x90, 0x67, 0x00};                  // 上半身
const unsigned char kNeck[3] =                                   // 0x531178
    {0x8E, 0xF1, 0x00};                                          // 首
const unsigned char kArmL[5] =                                   // 0x52B81C
    {0x8D, 0xB6, 0x98, 0x72, 0x00};                              // 左腕
const unsigned char kElbowL[7] =                                 // 0x52B814
    {0x8D, 0xB6, 0x82, 0xD0, 0x82, 0xB6, 0x00};                  // 左ひじ
const unsigned char kArmR[5] =                                   // 0x52B804
    {0x89, 0x45, 0x98, 0x72, 0x00};                              // 右腕
const unsigned char kElbowR[7] =                                 // 0x52B7FC
    {0x89, 0x45, 0x82, 0xD0, 0x82, 0xB6, 0x00};                  // 右ひじ
const unsigned char kLowerBody[7] =                              // 0x531170
    {0x89, 0xBA, 0x94, 0xBC, 0x90, 0x67, 0x00};                  // 下半身
const unsigned char kLegL[5] =                                   // 0x531168
    {0x8D, 0xB6, 0x91, 0xAB, 0x00};                              // 左足
const unsigned char kLegR[5] =                                   // 0x531160
    {0x89, 0x45, 0x91, 0xAB, 0x00};                              // 右足
const unsigned char kKneeL[7] =                                  // 0x530F60
    {0x8D, 0xB6, 0x82, 0xD0, 0x82, 0xB4, 0x00};                  // 左ひざ
const unsigned char kKneeR[7] =                                  // 0x530F58
    {0x89, 0x45, 0x82, 0xD0, 0x82, 0xB4, 0x00};                  // 右ひざ
const unsigned char kLegIkL[9] =                                 // 0x531154
    {0x8D, 0xB6, 0x91, 0xAB, 0x82, 0x68, 0x82, 0x6A, 0x00};      // 左足ＩＫ
const unsigned char kLegIkR[9] =                                 // 0x531148
    {0x89, 0x45, 0x91, 0xAB, 0x82, 0x68, 0x82, 0x6A, 0x00};      // 右足ＩＫ
const unsigned char kTwistL[7] =                                 // 0x52B80C
    {0x8D, 0xB6, 0x8E, 0xEA, 0x00, 0x00, 0x8D};                  // 左捩 (7)
const unsigned char kTwistR[7] =                                 // 0x52B7F4
    {0x89, 0x45, 0x8E, 0xEA, 0x00, 0x00, 0x89};                  // 右捩 (7)
const unsigned char kShoulderL[5] =                              // 0x531140
    {0x8D, 0xB6, 0x8C, 0xA8, 0x00};                              // 左肩
const unsigned char kShoulderR[5] =                              // 0x531138
    {0x89, 0x45, 0x8C, 0xA8, 0x00};                              // 右肩
const unsigned char kAnkleL[7] =                                 // 0x5311D8
    {0x8D, 0xB6, 0x91, 0xAB, 0x8E, 0xF1, 0x00};                  // 左足首

// The original writes the trace record slots through this exact expression;
// the cursor is re-read for every field.
inline float& TraceSlot(unsigned char* model, std::size_t fieldOffset) {
    return At<float>(
        static_cast<unsigned char*>(mikudancestudio::mdl::PoseTraceBuffer(model)),
        308 * (std::size_t)mikudancestudio::mdl::Mdl(model)->matMisc +
            fieldOffset);
}

// Mirror of one standard-bone quat/pos group (record slot offset, source
// quaternion/position group base in model+64..332).  sign = -1 negates the
// middle two components (left/right mirroring).
void WritePoseGroup(unsigned char* model, mikudancestudio::mdl::BoneRecord* bone,
                    std::size_t slot, int quatBase, float s1, float s2,
                    float s3, float s4) {
    bone->rotQuat[3] = s4;
    bone->rotQuat[0] = s1;
    bone->rotQuat[1] = s2;
    bone->rotQuat[2] = s3;
    if (mikudancestudio::mdl::PoseTraceFlag(model) != 0) {
        TraceSlot(model, slot) = bone->rotQuat[3];
        TraceSlot(model, slot + 4) = bone->rotQuat[0];
        TraceSlot(model, slot + 8) = bone->rotQuat[1];
        TraceSlot(model, slot + 12) = bone->rotQuat[2];
    }
}

}  // namespace

char ModelStandardPoseSetup(unsigned char* model, bool recordEnable,
                            unsigned char mirrorLeftRight, unsigned char skeletonFlag) {
    char wasRecording = 0;
    if (mikudancestudio::mdl::PoseTraceFlag(model) == 0 && recordEnable) {  // 0x4B577A
        mikudancestudio::mdl::PoseTraceFlag(model) = 1;
        if (mikudancestudio::mdl::PoseTraceBuffer(model) != nullptr) {
            ::operator delete(mikudancestudio::mdl::PoseTraceBuffer(model));   // 0x4B5795
            mikudancestudio::mdl::PoseTraceBuffer(model) = nullptr;
        }
        mikudancestudio::mdl::PoseTraceBuffer(model) =
            ::operator new(0x3B3760);                             // 0x4B57B0
        mikudancestudio::mdl::Mdl(model)->matMisc = 0;
    }
    if (!recordEnable) {                                         // 0x4B57BE
        if (mikudancestudio::mdl::PoseTraceFlag(model) != 0)
            wasRecording = 1;
        mikudancestudio::mdl::PoseTraceFlag(model) = 0;
    }

    // IK scan (0x4B57DA..0x4B5847): pick up the leg-IK "add" flags that the
    // knee branches below special-case.  recordEnable (a2) is overwritten
    // like the original.
    bool leftIkAdd = recordEnable;
    bool rightIkAdd = false;
    if (mikudancestudio::mdl::Mdl(model)->ikChainCount > 0) {
        for (std::uint16_t i = 0; i < (std::uint16_t)mikudancestudio::mdl::Mdl(model)->ikChainCount; ++i) {
            const mikudancestudio::mdl::IkChain& ik = mikudancestudio::mdl::IkChains(model)[i];
            const mikudancestudio::mdl::BoneRecord* bone =
                &mikudancestudio::mdl::Bones(model)[ik.boneIndex];
            if (std::memcmp(bone, kLegIkL, 9) == 0)
                leftIkAdd = ik.enabled != 0;                     // 0x4B5814
            if (std::memcmp(bone, kLegIkR, 9) == 0)
                rightIkAdd = ik.enabled != 0;                    // 0x4B582E
        }
    }

    InitStandardSkeletonQuats(model, skeletonFlag);                        // 0x4B5850

    // Height-scale normalization (0x4B5854..0x4B5994): when the scale slot
    // still holds the 90.0 sentinel and a leg height exists, derive the
    // scale from the 左足 bind pose and the offsets from 左足首.
    if ((double)mikudancestudio::mdl::Mdl(model)->lightDir[1] == 90.0 &&
        (double)mikudancestudio::mdl::Mdl(model)->lightDir[0] != -1.0) {
        const int boneCount = mikudancestudio::mdl::Mdl(model)->boneCount;
        mikudancestudio::mdl::BoneRecord* bones = mikudancestudio::mdl::Bones(model);
        const int leg = mdl::FindSkeletonBone(bones, boneCount, kLegL, sizeof kLegL);
        if (leg >= 0) {
            const float legY = bones[leg].position[1];
            if ((double)legY != -1.0) {
                const float scale = mikudancestudio::mdl::Mdl(model)->lightDir[0] / legY;  // 0x4B5928
                const double scaleD = scale;                     // held st(6)
                mikudancestudio::mdl::Mdl(model)->lightDir[1] = scale;
                const float offset =
                    -mdl::Mdl(model)->currentJoints[mdl::TrackedJoint::RightAnkle][1] / scale;                // 0x4B5954
                mikudancestudio::mdl::Mdl(model)->lightDir[2] = offset;
                const int ankle = mdl::FindSkeletonBone(
                    bones, boneCount, kAnkleL, sizeof kAnkleL);
                if (ankle >= 0) {
                    const float ankleY = bones[ankle].position[1];
                    if ((double)ankleY == -1.0) {
                        mikudancestudio::mdl::Mdl(model)->legIkXOffset = (float)(80.0 / scaleD);
                    } else {
                        mikudancestudio::mdl::Mdl(model)->lightDir[2] =
                            (float)((double)ankleY / scaleD + (double)offset);
                        mikudancestudio::mdl::Mdl(model)->legIkXOffset = (float)(80.0 / scaleD);    // 0x4B59B6
                    }
                } else {
                    mikudancestudio::mdl::Mdl(model)->legIkXOffset =
                        (float)(80.0 / scaleD);                  // 0x4B5994
                }
            }
        }
    }

    // Mirror/emit the standard bones (0x4B5994..0x4B77C7).  v45..47 are the
    // センター position snapshot reused by the leg-IK "keep pose" fallback;
    // the original leaves them as stale stack bytes when the model has no
    // センター - the port zero-inits them (deviation noted).
    float centerPos[3] = {0.0f, 0.0f, 0.0f};
    const int boneCount = mikudancestudio::mdl::Mdl(model)->boneCount;
    mikudancestudio::mdl::BoneRecord* bones = mikudancestudio::mdl::Bones(model);
    for (int i = 0; i < boneCount; ++i) {
        mikudancestudio::mdl::BoneRecord* bone = &bones[i];
        if (std::memcmp(bone, kCenter, 9) == 0) {
            if ((double)mdl::Mdl(model)->currentJoints[mdl::TrackedJoint::Center][1] != -999.0) {
                const double inv = 1.0 / (double)mikudancestudio::mdl::Mdl(model)->lightDir[1];
                bone->trans[0] =
                    (float)((double)mdl::Mdl(model)->currentJoints[mdl::TrackedJoint::Center][0] * inv);
                bone->trans[1] =
                    (float)((double)mdl::Mdl(model)->currentJoints[mdl::TrackedJoint::Center][1] * inv);
                bone->trans[2] =
                    (float)(inv * (double)mdl::Mdl(model)->currentJoints[mdl::TrackedJoint::Center][2]);
                if (mirrorLeftRight == 0)
                    bone->trans[0] = -bone->trans[0];
            }
            bone->rotQuat[3] = 1.0f;
            bone->rotQuat[0] = 0.0f;
            bone->rotQuat[1] = 0.0f;
            bone->rotQuat[2] = 0.0f;
            if (mdl::PoseTraceFlag(model) != 0) {
                TraceSlot(model, 0) = bone->trans[0];
                TraceSlot(model, 4) = bone->trans[1];
                TraceSlot(model, 8) = bone->trans[2];
            }
            centerPos[0] = bone->trans[0];
            centerPos[1] = bone->trans[1];
            centerPos[2] = bone->trans[2];
        } else if (std::memcmp(bone, kUpperBody, 7) == 0) {
            WritePoseGroup(model, bone, 12, 0,
                mdl::Mdl(model)->standardPose.upperBody[0], mirrorLeftRight ? mdl::Mdl(model)->standardPose.upperBody[1] : -mdl::Mdl(model)->standardPose.upperBody[1],
                mirrorLeftRight ? mdl::Mdl(model)->standardPose.upperBody[2] : -mdl::Mdl(model)->standardPose.upperBody[2],
                mdl::Mdl(model)->standardPose.upperBody[3]);
        } else if (std::memcmp(bone, kNeck, 3) == 0) {
            WritePoseGroup(model, bone, 28, 0,
                mdl::Mdl(model)->standardPose.neck[0], mirrorLeftRight ? mdl::Mdl(model)->standardPose.neck[1] : -mdl::Mdl(model)->standardPose.neck[1],
                mirrorLeftRight ? mdl::Mdl(model)->standardPose.neck[2] : -mdl::Mdl(model)->standardPose.neck[2],
                mdl::Mdl(model)->standardPose.neck[3]);
        } else if (std::memcmp(bone, kArmL, 5) == 0) {
            if (mirrorLeftRight != 0)
                WritePoseGroup(model, bone, 44, 0, mdl::Mdl(model)->standardPose.leftArm[0],
                    mdl::Mdl(model)->standardPose.leftArm[1], mdl::Mdl(model)->standardPose.leftArm[2],
                    mdl::Mdl(model)->standardPose.leftArm[3]);
            else
                WritePoseGroup(model, bone, 44, 0, mdl::Mdl(model)->standardPose.rightArm[0],
                    -mdl::Mdl(model)->standardPose.rightArm[1], -mdl::Mdl(model)->standardPose.rightArm[2],
                    mdl::Mdl(model)->standardPose.rightArm[3]);
        } else if (std::memcmp(bone, kElbowL, 7) == 0) {
            if (mirrorLeftRight != 0)
                WritePoseGroup(model, bone, 60, 0, mdl::Mdl(model)->standardPose.leftElbow[0],
                    mdl::Mdl(model)->standardPose.leftElbow[1], mdl::Mdl(model)->standardPose.leftElbow[2],
                    mdl::Mdl(model)->standardPose.leftElbow[3]);
            else
                WritePoseGroup(model, bone, 60, 0, mdl::Mdl(model)->standardPose.rightElbow[0],
                    -mdl::Mdl(model)->standardPose.rightElbow[1], -mdl::Mdl(model)->standardPose.rightElbow[2],
                    mdl::Mdl(model)->standardPose.rightElbow[3]);
        } else if (std::memcmp(bone, kArmR, 5) == 0) {
            if (mirrorLeftRight != 0)
                WritePoseGroup(model, bone, 76, 0, mdl::Mdl(model)->standardPose.rightArm[0],
                    mdl::Mdl(model)->standardPose.rightArm[1], mdl::Mdl(model)->standardPose.rightArm[2],
                    mdl::Mdl(model)->standardPose.rightArm[3]);
            else
                WritePoseGroup(model, bone, 76, 0, mdl::Mdl(model)->standardPose.leftArm[0],
                    -mdl::Mdl(model)->standardPose.leftArm[1], -mdl::Mdl(model)->standardPose.leftArm[2],
                    mdl::Mdl(model)->standardPose.leftArm[3]);
        } else if (std::memcmp(bone, kElbowR, 7) == 0) {
            if (mirrorLeftRight != 0)
                WritePoseGroup(model, bone, 92, 0, mdl::Mdl(model)->standardPose.rightElbow[0],
                    mdl::Mdl(model)->standardPose.rightElbow[1], mdl::Mdl(model)->standardPose.rightElbow[2],
                    mdl::Mdl(model)->standardPose.rightElbow[3]);
            else
                WritePoseGroup(model, bone, 92, 0, mdl::Mdl(model)->standardPose.leftElbow[0],
                    -mdl::Mdl(model)->standardPose.leftElbow[1], -mdl::Mdl(model)->standardPose.leftElbow[2],
                    mdl::Mdl(model)->standardPose.leftElbow[3]);
        } else if (std::memcmp(bone, kLowerBody, 7) == 0) {
            WritePoseGroup(model, bone, 108, 0, mdl::Mdl(model)->standardPose.lowerBody[0],
                mirrorLeftRight ? mdl::Mdl(model)->standardPose.lowerBody[1] : -mdl::Mdl(model)->standardPose.lowerBody[1],
                mirrorLeftRight ? mdl::Mdl(model)->standardPose.lowerBody[2] : -mdl::Mdl(model)->standardPose.lowerBody[2],
                mdl::Mdl(model)->standardPose.lowerBody[3]);
        } else if (std::memcmp(bone, kLegL, 5) == 0) {
            if (mirrorLeftRight != 0)
                WritePoseGroup(model, bone, 124, 0, mdl::Mdl(model)->standardPose.leftLeg[0],
                    mdl::Mdl(model)->standardPose.leftLeg[1], mdl::Mdl(model)->standardPose.leftLeg[2],
                    mdl::Mdl(model)->standardPose.leftLeg[3]);
            else
                WritePoseGroup(model, bone, 124, 0, mdl::Mdl(model)->standardPose.rightLeg[0],
                    -mdl::Mdl(model)->standardPose.rightLeg[1], -mdl::Mdl(model)->standardPose.rightLeg[2],
                    mdl::Mdl(model)->standardPose.rightLeg[3]);
        } else if (std::memcmp(bone, kLegR, 5) == 0) {
            if (mirrorLeftRight != 0)
                WritePoseGroup(model, bone, 140, 0, mdl::Mdl(model)->standardPose.rightLeg[0],
                    mdl::Mdl(model)->standardPose.rightLeg[1], mdl::Mdl(model)->standardPose.rightLeg[2],
                    mdl::Mdl(model)->standardPose.rightLeg[3]);
            else
                WritePoseGroup(model, bone, 140, 0, mdl::Mdl(model)->standardPose.leftLeg[0],
                    -mdl::Mdl(model)->standardPose.leftLeg[1], -mdl::Mdl(model)->standardPose.leftLeg[2],
                    mdl::Mdl(model)->standardPose.leftLeg[3]);
        } else if (std::memcmp(bone, kKneeL, 7) == 0) {           // 0x530F60
            if (leftIkAdd)
                WritePoseGroup(model, bone, 156, 0, 0.0f, 0.0f, 0.0f, 1.0f);
            else if (mirrorLeftRight != 0)
                WritePoseGroup(model, bone, 156, 0, mdl::Mdl(model)->standardPose.leftKnee[0],
                    mdl::Mdl(model)->standardPose.leftKnee[1], mdl::Mdl(model)->standardPose.leftKnee[2],
                    mdl::Mdl(model)->standardPose.leftKnee[3]);
            else
                WritePoseGroup(model, bone, 156, 0, mdl::Mdl(model)->standardPose.rightKnee[0],
                    -mdl::Mdl(model)->standardPose.rightKnee[1], -mdl::Mdl(model)->standardPose.rightKnee[2],
                    mdl::Mdl(model)->standardPose.rightKnee[3]);
        } else if (std::memcmp(bone, kKneeR, 7) == 0) {           // 0x530F58
            if (rightIkAdd)
                WritePoseGroup(model, bone, 172, 0, 0.0f, 0.0f, 0.0f, 1.0f);
            else if (mirrorLeftRight != 0)
                WritePoseGroup(model, bone, 172, 0, mdl::Mdl(model)->standardPose.rightKnee[0],
                    mdl::Mdl(model)->standardPose.rightKnee[1], mdl::Mdl(model)->standardPose.rightKnee[2],
                    mdl::Mdl(model)->standardPose.rightKnee[3]);
            else
                WritePoseGroup(model, bone, 172, 0, mdl::Mdl(model)->standardPose.leftKnee[0],
                    -mdl::Mdl(model)->standardPose.leftKnee[1], -mdl::Mdl(model)->standardPose.leftKnee[2],
                    mdl::Mdl(model)->standardPose.leftKnee[3]);
        } else if (std::memcmp(bone, kLegIkL, 9) == 0) {
            if (mirrorLeftRight != 0) {
                if ((double)mdl::Mdl(model)->currentJoints[mdl::TrackedJoint::LeftAnkle][1] != -999.0) {
                    const double inv = 1.0 / (double)mikudancestudio::mdl::Mdl(model)->lightDir[1];
                    bone->trans[0] =
                        (float)(inv * (double)mdl::Mdl(model)->currentJoints[mdl::TrackedJoint::LeftAnkle][0]);
                    bone->trans[1] =
                        (float)((double)mdl::Mdl(model)->currentJoints[mdl::TrackedJoint::LeftAnkle][1] * inv);
                    bone->trans[2] =
                        (float)(inv * (double)mdl::Mdl(model)->currentJoints[mdl::TrackedJoint::LeftAnkle][2]);
                    bone->trans[1] = mikudancestudio::mdl::Mdl(model)->lightDir[2] +
                        bone->trans[1];
                    bone->trans[0] = bone->trans[0] -
                        mikudancestudio::mdl::Mdl(model)->legIkXOffset;
                    bone->rotQuat[3] = mdl::Mdl(model)->standardPose.leftFoot[3];
                    bone->rotQuat[0] = mdl::Mdl(model)->standardPose.leftFoot[0];
                    bone->rotQuat[1] = mdl::Mdl(model)->standardPose.leftFoot[1];
                    bone->rotQuat[2] = mdl::Mdl(model)->standardPose.leftFoot[2];
                } else if (skeletonFlag != 0) {
                    bone->trans[0] = centerPos[0];
                    bone->trans[1] = centerPos[1];
                    bone->trans[2] = centerPos[2];
                }
            } else if ((double)mdl::Mdl(model)->currentJoints[mdl::TrackedJoint::RightAnkle][1] != -999.0) {
                const double inv = 1.0 / (double)mikudancestudio::mdl::Mdl(model)->lightDir[1];
                bone->trans[0] =
                    (float)(inv * (double)mdl::Mdl(model)->currentJoints[mdl::TrackedJoint::RightAnkle][0]);
                bone->trans[1] =
                    (float)((double)mdl::Mdl(model)->currentJoints[mdl::TrackedJoint::RightAnkle][1] * inv);
                bone->trans[2] =
                    (float)(inv * (double)mdl::Mdl(model)->currentJoints[mdl::TrackedJoint::RightAnkle][2]);
                bone->trans[1] = mikudancestudio::mdl::Mdl(model)->lightDir[2] +
                    bone->trans[1];
                bone->trans[0] = mikudancestudio::mdl::Mdl(model)->legIkXOffset +
                    bone->trans[0];
                bone->trans[0] = -bone->trans[0];
                bone->rotQuat[3] = mdl::Mdl(model)->standardPose.rightFoot[3];
                bone->rotQuat[0] = mdl::Mdl(model)->standardPose.rightFoot[0];
                bone->rotQuat[1] = -mdl::Mdl(model)->standardPose.rightFoot[1];
                bone->rotQuat[2] = -mdl::Mdl(model)->standardPose.rightFoot[2];
            } else if (skeletonFlag != 0) {
                bone->trans[0] = centerPos[0];
                bone->trans[1] = centerPos[1];
                bone->trans[2] = centerPos[2];
            }
            if (mdl::PoseTraceFlag(model) != 0) {
                TraceSlot(model, 188) = bone->trans[0];
                TraceSlot(model, 192) = bone->trans[1];
                TraceSlot(model, 196) = bone->trans[2];
                TraceSlot(model, 200) = bone->rotQuat[3];
                TraceSlot(model, 204) = bone->rotQuat[0];
                TraceSlot(model, 208) = bone->rotQuat[1];
                TraceSlot(model, 212) = bone->rotQuat[2];
            }
        } else if (std::memcmp(bone, kLegIkR, 9) == 0) {
            if (mirrorLeftRight != 0) {
                if ((double)mdl::Mdl(model)->currentJoints[mdl::TrackedJoint::RightAnkle][1] != -999.0) {
                    const double inv = 1.0 / (double)mikudancestudio::mdl::Mdl(model)->lightDir[1];
                    bone->trans[0] =
                        (float)(inv * (double)mdl::Mdl(model)->currentJoints[mdl::TrackedJoint::RightAnkle][0]);
                    bone->trans[1] =
                        (float)((double)mdl::Mdl(model)->currentJoints[mdl::TrackedJoint::RightAnkle][1] * inv);
                    bone->trans[2] =
                        (float)(inv * (double)mdl::Mdl(model)->currentJoints[mdl::TrackedJoint::RightAnkle][2]);
                    bone->trans[1] = mikudancestudio::mdl::Mdl(model)->lightDir[2] +
                        bone->trans[1];
                bone->trans[0] = mikudancestudio::mdl::Mdl(model)->legIkXOffset +
                        bone->trans[0];
                    bone->rotQuat[3] = mdl::Mdl(model)->standardPose.rightFoot[3];
                    bone->rotQuat[0] = mdl::Mdl(model)->standardPose.rightFoot[0];
                    bone->rotQuat[1] = mdl::Mdl(model)->standardPose.rightFoot[1];
                    bone->rotQuat[2] = mdl::Mdl(model)->standardPose.rightFoot[2];
                } else if (skeletonFlag != 0) {
                    bone->trans[0] = centerPos[0];
                    bone->trans[1] = centerPos[1];
                    bone->trans[2] = centerPos[2];
                }
            } else if ((double)mdl::Mdl(model)->currentJoints[mdl::TrackedJoint::LeftAnkle][1] != -999.0) {
                const double inv = 1.0 / (double)mikudancestudio::mdl::Mdl(model)->lightDir[1];
                bone->trans[0] =
                    (float)(inv * (double)mdl::Mdl(model)->currentJoints[mdl::TrackedJoint::LeftAnkle][0]);
                bone->trans[1] =
                    (float)((double)mdl::Mdl(model)->currentJoints[mdl::TrackedJoint::LeftAnkle][1] * inv);
                bone->trans[2] =
                    (float)(inv * (double)mdl::Mdl(model)->currentJoints[mdl::TrackedJoint::LeftAnkle][2]);
                bone->trans[1] = mikudancestudio::mdl::Mdl(model)->lightDir[2] +
                    bone->trans[1];
                bone->trans[0] = bone->trans[0] -
                    mikudancestudio::mdl::Mdl(model)->legIkXOffset;
                bone->trans[0] = -bone->trans[0];
                bone->rotQuat[3] = mdl::Mdl(model)->standardPose.leftFoot[3];
                bone->rotQuat[0] = mdl::Mdl(model)->standardPose.leftFoot[0];
                bone->rotQuat[1] = -mdl::Mdl(model)->standardPose.leftFoot[1];
                bone->rotQuat[2] = -mdl::Mdl(model)->standardPose.leftFoot[2];
            } else if (skeletonFlag != 0) {
                bone->trans[0] = centerPos[0];
                bone->trans[1] = centerPos[1];
                bone->trans[2] = centerPos[2];
            }
            if (mdl::PoseTraceFlag(model) != 0) {
                TraceSlot(model, 216) = bone->trans[0];
                TraceSlot(model, 220) = bone->trans[1];
                TraceSlot(model, 224) = bone->trans[2];
                TraceSlot(model, 228) = bone->rotQuat[3];
                TraceSlot(model, 232) = bone->rotQuat[0];
                TraceSlot(model, 236) = bone->rotQuat[1];
                TraceSlot(model, 240) = bone->rotQuat[2];
            }
        } else if (std::memcmp(bone, kTwistL, 7) == 0) {         // 0x52B80C
            if (mirrorLeftRight != 0)
                WritePoseGroup(model, bone, 244, 0, mdl::Mdl(model)->standardPose.leftWrist[0],
                    mdl::Mdl(model)->standardPose.leftWrist[1], mdl::Mdl(model)->standardPose.leftWrist[2],
                    mdl::Mdl(model)->standardPose.leftWrist[3]);
            else
                WritePoseGroup(model, bone, 244, 0, mdl::Mdl(model)->standardPose.rightWrist[0],
                    -mdl::Mdl(model)->standardPose.rightWrist[1], -mdl::Mdl(model)->standardPose.rightWrist[2],
                    mdl::Mdl(model)->standardPose.rightWrist[3]);
        } else if (std::memcmp(bone, kTwistR, 7) == 0) {         // 0x52B7F4
            if (mirrorLeftRight != 0)
                WritePoseGroup(model, bone, 260, 0, mdl::Mdl(model)->standardPose.rightWrist[0],
                    mdl::Mdl(model)->standardPose.rightWrist[1], mdl::Mdl(model)->standardPose.rightWrist[2],
                    mdl::Mdl(model)->standardPose.rightWrist[3]);
            else
                WritePoseGroup(model, bone, 260, 0, mdl::Mdl(model)->standardPose.leftWrist[0],
                    -mdl::Mdl(model)->standardPose.leftWrist[1], -mdl::Mdl(model)->standardPose.leftWrist[2],
                    mdl::Mdl(model)->standardPose.leftWrist[3]);
        } else if (std::memcmp(bone, kShoulderL, 5) == 0) {
            if (mirrorLeftRight != 0)
                WritePoseGroup(model, bone, 276, 0, mdl::Mdl(model)->standardPose.leftShoulder[0],
                    mdl::Mdl(model)->standardPose.leftShoulder[1], mdl::Mdl(model)->standardPose.leftShoulder[2],
                    mdl::Mdl(model)->standardPose.leftShoulder[3]);
            else
                WritePoseGroup(model, bone, 276, 0, mdl::Mdl(model)->standardPose.rightShoulder[0],
                    -mdl::Mdl(model)->standardPose.rightShoulder[1], -mdl::Mdl(model)->standardPose.rightShoulder[2],
                    mdl::Mdl(model)->standardPose.rightShoulder[3]);
        } else if (std::memcmp(bone, kShoulderR, 5) == 0) {
            if (mirrorLeftRight != 0)
                WritePoseGroup(model, bone, 292, 0, mdl::Mdl(model)->standardPose.rightShoulder[0],
                    mdl::Mdl(model)->standardPose.rightShoulder[1], mdl::Mdl(model)->standardPose.rightShoulder[2],
                    mdl::Mdl(model)->standardPose.rightShoulder[3]);
            else
                WritePoseGroup(model, bone, 292, 0, mdl::Mdl(model)->standardPose.leftShoulder[0],
                    -mdl::Mdl(model)->standardPose.leftShoulder[1], -mdl::Mdl(model)->standardPose.leftShoulder[2],
                    mdl::Mdl(model)->standardPose.leftShoulder[3]);
        }
    }
    if (mdl::PoseTraceFlag(model) != 0)
        ++mikudancestudio::mdl::Mdl(model)->matMisc;                        // 0x4B77C7
    return wasRecording;
}

}  // namespace mikudancestudio
