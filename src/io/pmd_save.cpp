// PMD 1.0 writer: x86 0x490230, x64 0x7FF7CB4D59B0.
#include "mikudancestudio/mmd_app.hpp"
#include "mikudancestudio/model.hpp"
#include "mikudancestudio/ported_funcs.hpp"

#include <cstdio>
#include <cstring>
#include <cwchar>
#include <fcntl.h>
#include <io.h>
#include <share.h>
#include <sys/stat.h>

namespace mikudancestudio {
// ===========================================================================
// 0x490230 (x64 sub_7FF7CB4D59B0) - "save enhanced model" writer.
// The original is a model method taking the destination path and renderer.
// Serialises the whole model as a PMD file (field order mirrors the reader
// in src/model/pmd_load.cpp):
//   header ("Pmd", 1.0f, name[20], comment[256]) - vertex count + 38-byte
//   records (edge flag normalised to 0/1) - index count + u16 indices -
//   material count + 70-byte records (texture+sphere names combined with
//   '*' after stripping the model-directory prefix, converted to SJIS) -
//   bone count + 39-byte records - IK count + chains - morph count +
//   entries (indices of morph 0 written verbatim, later morphs remapped
//   through morph 0's table) - facial display list - bone group names -
//   rigid-body display list - English section (flag always 1; morph names
//   skip morph 0, group names start at 2/3 like the reader) - the ten
//   100-byte toon file names - rigid bodies (83 bytes) - joints (124
//   bytes, limits in the reader's shuffled order).  On success the saved
//   path becomes model->path.
// Quirk kept verbatim: before writing vertices, every morph-0 entry
// OVERWRITES (not adds to) the raw vertex position with its offset.
// ===========================================================================
int SavePmdFile(mdl::ModelRecord& model, const wchar_t* path, D3DRenderer* renderer) {

    int fh = -1;
    const errno_t openErr =
        _wsopen_s(&fh, path, _O_BINARY | _O_WRONLY | _O_CREAT | _O_TRUNC, _SH_DENYNO, _S_IREAD);
    if (openErr != 0 || fh == -1) {
        char text[0x100];
        if (model.physicsFlags != 0) // English-data byte (set at load)
            sprintf_s(text, 0x100, "Cannot save file:%d", openErr);
        else // 0x54FED0: "Cannot save file:%d"
            sprintf_s(text, 0x100,
                      "\x83\x74\x83\x40\x83\x43\x83\x8B\x82\xaa\x95\xdb"
                      "\x91\xb6\x82\xc5\x82\xab\x82\xdc\x82\xb9\x82\xf1"
                      ":%d",
                      openErr);
        return MessageBoxA(static_cast<HWND>(model.hwnd), text, "", 0);
    }

    // ---- header ----------------------------------------------------------
    _write(fh, "Pmd", 3);
    const float version = 1.0f;
    _write(fh, &version, 4);
    _write(fh, model.name, 0x14);
    _write(fh, model.comment, 0x100);

    // ---- morph-0 pre-pass (positions overwritten, see note above) --------
    if (model.morphs != nullptr && model.morph0Count != 0) {
        for (std::uint32_t k = 0; k < model.morph0Count; ++k) {
            const mdl::PmdVertexMorphEntry& e = model.morph0Table[k];
            std::memcpy(model.rawVertices[e.vertexIndex].position, e.offset, 12);
        }
    }

    // ---- vertices --------------------------------------------------------
    _write(fh, &model.vertexCount, 4);
    for (std::uint32_t i = 0; i < model.vertexCount; ++i) {
        const mdl::PmdVertex& v = model.rawVertices[i];
        _write(fh, v.position, 12);
        _write(fh, v.normal, 12);
        _write(fh, v.uv, 8);
        _write(fh, &v.bone[0], 2);
        _write(fh, &v.bone[1], 2);
        _write(fh, &v.weightPercent, 1);
        const std::uint8_t edge = v.edgeDisabled != 0 ? 1 : 0;
        _write(fh, &edge, 1);
    }

    // ---- indices ---------------------------------------------------------
    _write(fh, &model.indexCount, 4);
    // Both loaders normalize CPU indices to 32 bits. The PMD stream keeps
    // only the low word (x64 0x7FF7CB4D5D34, x86 0x490523).
    for (std::uint32_t i = 0; i < model.indexCount; ++i) {
        const std::uint16_t index = static_cast<std::uint16_t>(model.indices[i]);
        _write(fh, &index, 2);
    }

    // ---- materials -------------------------------------------------------
    _write(fh, &model.materialCount, 4);
    mdl::ModelMaterialRecord* const materials = model.materials;
    const std::size_t dirLen = std::wcslen(model.modelDirectory);
    for (std::uint32_t i = 0; i < model.materialCount; ++i) {
        const mdl::ModelMaterialRecord& mat = materials[i];
        _write(fh, mat.diffuse, 16);
        _write(fh, &mat.specularPower, 4);
        _write(fh, mat.specular, 12);
        _write(fh, mat.ambient, 12);
        _write(fh, &mat.toonReference, 1);
        const std::uint8_t sphereFlag = mat.doubleSided != 0 ? 1 : 0;
        _write(fh, &sphereFlag, 1);
        _write(fh, &mat.faceVertexCount, 4);
        // combined texture[*sphere] name, stripped of the directory prefix
        wchar_t combined[20];
        const wchar_t* name;
        if (mat.spherePath[0] != L'\0') {
            wcscpy_s(combined, 0x14, mat.texturePath + dirLen);
            wcscat_s(combined, 0x14, L"*");
            wcscat_s(combined, 0x14, mat.spherePath + dirLen);
            name = combined;
        } else {
            name = mat.texturePath[0] != L'\0' ? mat.texturePath + dirLen : mat.texturePath;
        }
        char sjis[0x100] = "";
        WideToSjis(renderer, sjis, name, 0x100);
        _write(fh, sjis, 0x14);
    }

    // ---- bones -----------------------------------------------------------
    _write(fh, &model.boneCount, 2);
    for (std::uint32_t i = 0; i < model.boneCount; ++i) {
        const mdl::BoneRecord& bone = model.boneTable[i];
        _write(fh, bone.name, 0x14);
        const std::uint16_t parent = static_cast<std::uint16_t>(bone.parent);
        _write(fh, &parent, 2);
        const std::uint16_t tail = static_cast<std::uint16_t>(bone.tailBone);
        _write(fh, &tail, 2);
        _write(fh, &bone.type, 1);
        const std::uint16_t tailIdx = static_cast<std::uint16_t>(bone.tailIdx);
        _write(fh, &tailIdx, 2);
        _write(fh, bone.position, 12);
    }

    // ---- IK chains -------------------------------------------------------
    _write(fh, &model.ikChainCount, 2);
    for (std::uint32_t i = 0; i < model.ikChainCount; ++i) {
        const mdl::IkChain& ik = model.ikChains[i];
        const std::uint16_t bone = static_cast<std::uint16_t>(ik.boneIndex);
        _write(fh, &bone, 2);
        const std::uint16_t target = static_cast<std::uint16_t>(ik.targetBone);
        _write(fh, &target, 2);
        _write(fh, &ik.linkCount, 1);
        _write(fh, &ik.iterations, 2);
        _write(fh, &ik.maxAngle, 4);
        for (int k = 0; k < ik.linkCount; ++k)
            _write(fh, &ik.links[k], 2);
    }

    // ---- morphs ----------------------------------------------------------
    _write(fh, &model.morphCount, 2);
    for (std::uint32_t i = 0; i < model.morphCount; ++i) {
        const mdl::MorphRecord& morph = model.morphs[i];
        _write(fh, morph.name, 0x14);
        _write(fh, &morph.offsetCount, 4);
        _write(fh, &morph.panel, 1);
        const mdl::PmdVertexMorphEntry* const base = model.morphs[0].vertexEntries;
        const std::int32_t baseCount = model.morphs[0].offsetCount;
        for (std::int32_t k = 0; k < morph.offsetCount; ++k) {
            const mdl::PmdVertexMorphEntry& e = morph.vertexEntries[k];
            if (i == 0) {
                _write(fh, &e.vertexIndex, 4); // morph 0: raw index
            } else if (baseCount != 0) {
                // later morphs: remap through morph 0's table; an unfound
                // index skips the dword but still writes its offsets
                std::int32_t j = 0;
                while (j < baseCount && base[j].vertexIndex != e.vertexIndex)
                    ++j;
                if (j < baseCount)
                    _write(fh, &j, 4);
            }
            _write(fh, e.offset, 12);
        }
    }

    // ---- facial display list ---------------------------------------------
    _write(fh, &model.facialFrameCount, 1);
    for (int k = 0; k < model.facialFrameCount; ++k)
        _write(fh, &model.displayFrames[k].targetIndex, 2);

    // ---- bone group names --------------------------------------------------
    // reader adds 2 (+1 with the facial group) back to this byte
    const bool hasFacial = model.facialFrameCount != 0;
    const std::uint8_t groupByte =
        static_cast<std::uint8_t>(model.groupCount - (hasFacial ? 3 : 2));
    _write(fh, &groupByte, 1);
    mdl::DisplayGroup* const groups = model.displayGroups;
    for (int m = hasFacial ? 3 : 2; m < model.groupCount; ++m)
        _write(fh, groups[m].name, 0x32);

    // ---- rigid-body display list -------------------------------------------
    _write(fh, &model.displayBoneCount, 4);
    mdl::FrameGroup* const displayBones = model.displayBones;
    for (std::uint32_t k = 0; k < model.displayBoneCount; ++k) {
        const mdl::FrameGroup& rb = displayBones[k];
        _write(fh, &rb.targetIndex, 2);
        // reader adds 1 (+1 with the facial group) back
        const std::uint8_t groupIdx =
            static_cast<std::uint8_t>(rb.groupIndex - (hasFacial ? 2 : 1));
        _write(fh, &groupIdx, 1);
    }

    // ---- English section ---------------------------------------------------
    const std::uint8_t hasEnglish = 1;
    _write(fh, &hasEnglish, 1);
    _write(fh, model.nameEn, 0x14);
    _write(fh, model.commentEn, 0x100);
    for (std::uint32_t i = 0; i < model.boneCount; ++i)
        _write(fh, model.boneTable[i].nameEn, 0x14);
    for (std::uint32_t i = 1; i < model.morphCount; ++i) // skips morph 0
        _write(fh, model.morphs[i].nameEn, 0x14);
    for (int m = hasFacial ? 3 : 2; m < model.groupCount; ++m)
        _write(fh, groups[m].nameEn, 0x32);

    // ---- toon file names ----------------------------------------------------
    for (int i = 0; i < 10; ++i)
        _write(fh, model.pmdToonFileNames[i], 0x64);

    // ---- rigid bodies --------------------------------------------------------
    _write(fh, &model.rigidCount, 4);
    for (std::uint32_t i = 0; i < model.rigidCount; ++i) {
        const mdl::RigidRecord& rigid = model.rigidTable[i];
        _write(fh, rigid.name, 0x14);
        const std::uint16_t bone = static_cast<std::uint16_t>(rigid.boneIndex);
        _write(fh, &bone, 2);
        _write(fh, &rigid.group, 1);
        _write(fh, &rigid.noCollapse, 2);
        _write(fh, &rigid.shape, 1);
        _write(fh, rigid.size, 12);
        _write(fh, rigid.position, 12);
        _write(fh, rigid.rotation, 12);
        _write(fh, &rigid.mass, 4);
        _write(fh, &rigid.linearDamping, 4);
        _write(fh, &rigid.angularDamping, 4);
        _write(fh, &rigid.restitution, 4);
        _write(fh, &rigid.friction, 4);
        _write(fh, &rigid.mode, 1);
    }

    // ---- joints ---------------------------------------------------------------
    _write(fh, &model.jointCount, 4);
    for (std::uint32_t i = 0; i < model.jointCount; ++i) {
        const mdl::JointRecord& joint = model.jointTable[i];
        _write(fh, joint.name, 0x14);
        _write(fh, &joint.rigidA, 4);
        _write(fh, &joint.rigidB, 4);
        _write(fh, joint.position, 12);
        _write(fh, joint.rotation, 12);
        _write(fh, &joint.limits[3], 12);
        _write(fh, &joint.limits[0], 12);
        _write(fh, &joint.limits[9], 12);
        _write(fh, &joint.limits[6], 12);
        _write(fh, joint.springs, 24);
    }

    _close(fh);
    return wcscpy_s(model.path, 0x100, path);
}

} // namespace mikudancestudio
