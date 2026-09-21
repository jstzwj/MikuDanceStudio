// ===========================================================================
// Enhance-model IO: toon collect (0x41EA20), save model (0x41EC10), model colour (0x4A4850)
// ===========================================================================
// Split out of src/window/command_view_menu.cpp (the menu-251..302 command
// family) so the dialog's helper bodies can be ported independently.
// Every function keeps its original x86 VA; behaviour notes live in the
// per-function comments.  Ported against the x64 twin
// (MikuMikuDanceE_v932x64, session 99824acb):
//   0x41EA20 -> sub_7FF7CB4AD5D0   0x41EC10 -> sub_7FF7CB4D59B0
//   0x4A4850 -> sub_7FF7CB4F2240   0x40B5A0 -> sub_7FF7CB43AB40
//   0x4076E0 -> sub_7FF7CB428F30
// =========================================================================//
#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <Windows.h>
#include <commdlg.h>
#include <d3d9.h>

#include <cmath>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <cwchar>

#include <fcntl.h>
#include <io.h>
#include <share.h>
#include <sys/stat.h>

#include "mikudancestudio/d3dx_dyn.hpp"
#include "mikudancestudio/d3d_wrapper.hpp"
#include "mikudancestudio/global_key_layout.hpp"
#include "mikudancestudio/mmd_app.hpp"
#include "mikudancestudio/model.hpp"
#include "mikudancestudio/ported_funcs.hpp"

namespace mikudancestudio {
namespace {

constexpr UINT kD3dxDefault = 0xFFFFFFFFu;

}  // namespace

// ===========================================================================
// 0x41EA20 (x64 sub_7FF7CB4AD5D0) - OK collector of the enhance-model toon
// dialog (menu 261, edits 709..718).  For each of the ten slots:
//   * a name equal to the default "toonNN.bmp" is accepted as-is (skip);
//   * otherwise the name is converted to wide and probed as
//     <model directory>\<name> through the shared texture cache
//     (LoadTextureShared, 0x407490); on success the SJIS name is stored
//     into the model's pmdToonFileNames slot;
//   * on failure the EN/JP "Cannot find ..." box appears and the function
//     will report failure.
// Returns 1 when every non-default name resolved.
// ===========================================================================
int CollectToonFileNames(HWND hDlg) {  // VA 0x0041EA20
    MMDApp* app = g_Block;
    mdl::ModelRecord& model = *mdl::Mdl(app->SelectedModel());
    D3DRenderer* renderer = app->Renderer();
    int allFound = 1;
    for (int i = 1; i <= 10; ++i) {
        char name[0x100];
        GetWindowTextA(GetDlgItem(hDlg, 708 + i), name, 0x100);
        char def[0x100];
        sprintf_s(def, 0x100, "toon%02d.bmp", i);
        if (std::strcmp(name, def) == 0)
            continue;  // default toon name: no file probe, no store
        wchar_t rel[0x100];
        wcscpy_s(rel, 0x100, L"");
        if (name[0] != '\0')
            ConvertAnsiToWide(renderer, name, rel, 0x100);
        wchar_t path[0x100];
        swprintf_s(path, 0x100, L"%s%s", model.modelDirectory, rel);
        if (path[0] != L'\0' &&
            LoadTextureShared(reinterpret_cast<unsigned char*>(renderer),
                              path) != 0) {
            strcpy_s(model.pmdToonFileNames[i - 1], 0x64, name);
        } else {
            allFound = 0;
            char text[0x100];
            const char* caption;
            if (app->state.englishUI != 0) {
                sprintf_s(text, 0x100,
                          "Cannot find '%s'.\n\n"
                          "Please put '%s' in the same folder as model file "
                          "(*.pmd).",
                          name, name);
                caption = "toon texture";
            } else {
                // 0x52F0F0: "ﾂｰﾝﾃｷｽﾁｬｱ:%sが見つかりません。\n\n
                //            %sをモデルファイル(*.pmd)と同じフォルダ内に
                //            置いてください。"
                sprintf_s(text, 0x100,
                          "\xc3\xb8\xbd\xc1\xac\xcc\xa7\xb2\xd9\x81\x46%s"
                          "\x82\xaa\x8c\xa9\x82\xc2\x82\xa9\x82\xe8\x82\xdc"
                          "\x82\xb9\x82\xf1\x81\x42\n\n"
                          "%s\x82\xf0\xd3\xc3\xde\xd9\xcc\xa7\xb2\xd9"
                          "(*.pmd)\x82\xc6\x93\xaf\x82\xb6\x83\x74\x83\x48"
                          "\x83\x8b\x83\x5f\x93\xe0\x82\xc9\x92\x75\x82\xa2"
                          "\x82\xc4\x89\xba\x82\xb3\x82\xa2\x81\x42",
                          name, name);
                // 0x52F148: "トゥーンテクスチャ"
                caption = "\x83\x67\x83\x44\x81\x5B\x83\x93\x83\x65\x83\x4E"
                          "\x83\x58\x83\x60\x83\x83";
            }
            MessageBoxA(hDlg, text, caption, 0);
        }
    }
    return allFound;
}

// ===========================================================================
// 0x41EC10 (x64 sub_7FF7CB4D59B0) - "save enhanced model" writer.  The x64
// original is a method of the model (thiscall(model, path, renderer)); the
// dispatcher passes the selected model, so pick it from the app here.
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
void SaveEnhancedModel(MMDApp* app, const wchar_t* path) {  // 0x41EC10
    mdl::ModelRecord& model = *mdl::Mdl(app->SelectedModel());
    D3DRenderer* renderer = app->Renderer();

    int fh = -1;
    const errno_t openErr = _wsopen_s(&fh, path,
                                      _O_BINARY | _O_WRONLY | _O_CREAT |
                                          _O_TRUNC,
                                      _SH_DENYNO, _S_IREAD);
    if (openErr != 0 || fh == -1) {
        char text[0x100];
        if (model.physicsFlags != 0)  // English-data byte (set at load)
            sprintf_s(text, 0x100, "Cannot save file:%d", openErr);
        else  // 0x54FED0: "ファイルが保存できません:%d"
            sprintf_s(text, 0x100,
                      "\x83\x74\x83\x40\x83\x43\x83\x8B\x82\xaa\x95\xdb"
                      "\x91\xb6\x82\xc5\x82\xab\x82\xdc\x82\xb9\x82\xf1"
                      ":%d",
                      openErr);
        MessageBoxA(static_cast<HWND>(model.hwnd), text, "", 0);
        return;
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
            std::memcpy(model.rawVertices[e.vertexIndex].position, e.offset,
                        12);
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
    std::uint16_t* const indices =
        mdl::Indices(reinterpret_cast<unsigned char*>(&model));
    for (std::uint32_t i = 0; i < model.indexCount; ++i)
        _write(fh, &indices[i], 2);

    // ---- materials -------------------------------------------------------
    _write(fh, &model.materialCount, 4);
    mdl::ModelMaterialRecord* const materials = mdl::Materials(
        reinterpret_cast<unsigned char*>(&model));
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
            name = mat.texturePath[0] != L'\0' ? mat.texturePath + dirLen
                                               : mat.texturePath;
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
        const std::uint16_t parent =
            static_cast<std::uint16_t>(bone.parent);
        _write(fh, &parent, 2);
        const std::uint16_t tail =
            static_cast<std::uint16_t>(bone.tailBone);
        _write(fh, &tail, 2);
        _write(fh, &bone.type, 1);
        const std::uint16_t tailIdx =
            static_cast<std::uint16_t>(bone.tailIdx);
        _write(fh, &tailIdx, 2);
        _write(fh, bone.position, 12);
    }

    // ---- IK chains -------------------------------------------------------
    _write(fh, &model.ikChainCount, 2);
    for (std::uint32_t i = 0; i < model.ikChainCount; ++i) {
        const mdl::IkChain& ik = model.ikChains[i];
        const std::uint16_t bone =
            static_cast<std::uint16_t>(ik.boneIndex);
        _write(fh, &bone, 2);
        const std::uint16_t target =
            static_cast<std::uint16_t>(ik.targetBone);
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
        const mdl::PmdVertexMorphEntry* const base =
            model.morphs[0].vertexEntries;
        const std::int32_t baseCount = model.morphs[0].offsetCount;
        for (std::int32_t k = 0; k < morph.offsetCount; ++k) {
            const mdl::PmdVertexMorphEntry& e = morph.vertexEntries[k];
            if (i == 0) {
                _write(fh, &e.vertexIndex, 4);  // morph 0: raw index
            } else if (baseCount != 0) {
                // later morphs: remap through morph 0's table; an unfound
                // index skips the dword but still writes its offsets
                std::int32_t j = 0;
                while (j < baseCount &&
                       base[j].vertexIndex != e.vertexIndex)
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
    const std::uint8_t groupByte = static_cast<std::uint8_t>(
        model.groupCount - (hasFacial ? 3 : 2));
    _write(fh, &groupByte, 1);
    mdl::DisplayGroup* const groups = mdl::DisplayGroups(
        reinterpret_cast<unsigned char*>(&model));
    for (int m = hasFacial ? 3 : 2; m < model.groupCount; ++m)
        _write(fh, groups[m].name, 0x32);

    // ---- rigid-body display list -------------------------------------------
    _write(fh, &model.rigidBodyCount, 4);
    mdl::FrameGroup* const rbGroups = mdl::RigidGroups(
        reinterpret_cast<unsigned char*>(&model));
    for (std::uint32_t k = 0; k < model.rigidBodyCount; ++k) {
        const mdl::FrameGroup& rb = rbGroups[k];
        _write(fh, &rb.targetIndex, 2);
        // reader adds 1 (+1 with the facial group) back
        const std::uint8_t groupIdx = static_cast<std::uint8_t>(
            rb.groupIndex - (hasFacial ? 2 : 1));
        _write(fh, &groupIdx, 1);
    }

    // ---- English section ---------------------------------------------------
    const std::uint8_t hasEnglish = 1;
    _write(fh, &hasEnglish, 1);
    _write(fh, model.nameEn, 0x14);
    _write(fh, model.commentEn, 0x100);
    for (std::uint32_t i = 0; i < model.boneCount; ++i)
        _write(fh, model.boneTable[i].nameEn, 0x14);
    for (std::uint32_t i = 1; i < model.morphCount; ++i)  // skips morph 0
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
        const std::uint16_t bone =
            static_cast<std::uint16_t>(rigid.boneIndex);
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
    wcscpy_s(model.path, 0x100, path);
}

// ===========================================================================
// 0x4A4850 (x64 sub_7FF7CB4F2240) - SetModelColor:
// set model colour (ground-shadow tint sweep; called from pmm_load_v1/v2
// and the menu-286 colour picker).  Skips models whose physicsMode byte is
// 2, then locks the 16-byte-stride secondary vertex buffer (FVF 0x42:
// position + diffuse) and fills every vertex's colour with opaque
// ARGB(r, g, b).
// ===========================================================================
void SetModelColor(MMDApp* modelPtr, int r, int g, int b) {
    mdl::ModelRecord& model =
        *mdl::Mdl(reinterpret_cast<unsigned char*>(modelPtr));
    if (model.physicsMode == 2)
        return;
    if (model.vertexCount == 0)
        return;
    IDirect3DVertexBuffer9* vb =
        mdl::ResourceAs<IDirect3DVertexBuffer9>(model.vertexBuffer2);
    if (vb == nullptr)
        return;
    void* bits = nullptr;
    vb->Lock(0, 16 * model.vertexCount, &bits, 0);
    if (bits == nullptr) {
        vb->Unlock();
        return;
    }
    // the original widens r to a full register but takes g/b as bytes
    const std::uint32_t color = 0xFF000000u |
                                (static_cast<std::uint32_t>(r) << 16) |
                                (static_cast<std::uint32_t>(g & 0xFF) << 8) |
                                static_cast<std::uint32_t>(b & 0xFF);
    for (std::uint32_t i = 0; i < model.vertexCount; ++i) {
        *reinterpret_cast<std::uint32_t*>(
            static_cast<unsigned char*>(bits) + 16 * i + 12) = color;
    }
    vb->Unlock();
}

// ===========================================================================
// 0x40B5A0 (x64 sub_7FF7CB43AB40) - menu-bar language refresh after the
// Japanese/English switch (menu 260).  Re-titles all eight top menus and
// every menu item with the EN literals / JP Shift-JIS strings of the
// original (the "full screen" item becomes "NVIDIA 3D Vision" when the
// renderer's stereo flag is set), then redraws the menu bar.
// ===========================================================================
namespace {

struct MenuText {
    int pos;
    const char* en;
    const char* jp;
};

constexpr MenuText kFileMenu[] = {
    {0, "new(&N)", "\x90\x56\x8b\x4b\x28\x26\x4e\x29"},
    {1, "open(&O)", "\x8a\x4a\x82\xad\x28\x26\x4f\x29"},
    {2, "save(&S)", "\x8f\xe3\x8f\x91\x82\xab\x95\xdb\x91\xb6\x28\x26\x53\x29"},
    {3, "save as(&A)",
     "\x96\xbc\x91\x4f\x82\xf0\x95\x74\x82\xaf\x82\xc4\x95\xdb\x91\xb6"
     "\x28\x26\x41\x29"},
    {5, "render to AVI file(&V)",
     "\x41\x56\x49\x83\x74\x83\x40\x83\x43\x83\x8b\x82\xc9\x8f\x6f\x97"
     "\xcd\x28\x26\x56\x29"},
    {6, "render to picture file(&B)",
     "\x89\xe6\x91\x9c\x83\x74\x83\x40\x83\x43\x83\x8b\x82\xc9\x8f\x6f"
     "\x97\xcd\x28\x26\x42\x29"},
    {8, "load pose data(&P)",
     "\x83\x7c\x81\x5b\x83\x59\x83\x66\x81\x5b\x83\x5e\x93\xc7\x8d\x9e"
     "\x28\x26\x50\x29"},
    {9, "save pose data(&Q)",
     "\x83\x7c\x81\x5b\x83\x59\x83\x66\x81\x5b\x83\x5e\x95\xdb\x91\xb6"
     "\x28\x26\x51\x29"},
    {0xB, "load motion data(&M)",
     "\x83\x82\x81\x5b\x83\x56\x83\x87\x83\x93\x83\x66\x81\x5b\x83\x5e"
     "\x93\xc7\x8d\x9e\x28\x26\x4d\x29"},
    {0xC, "save motion data(&L)",
     "\x83\x82\x81\x5b\x83\x56\x83\x87\x83\x93\x83\x66\x81\x5b\x83\x5e"
     "\x95\xdb\x91\xb6\x28\x26\x4c\x29"},
    {0xE, "load WAV file(&W)",
     "\x57\x41\x56\x83\x74\x83\x40\x83\x43\x83\x8b\x93\xc7\x8d\x9e\x28"
     "\x26\x57\x29"},
    {0xF, "play WAV with frame(&F)",
     "\xcc\xda\xb0\xd1\x88\xda\x93\xae\x8e\x9e\x57\x41\x56\x82\xf0\x96"
     "\xc2\x82\xe7\x82\xb7\x28\x26\x46\x29"},
    {0x10, "not play WAV file(&D)",
     "\x57\x41\x56\x83\x74\x83\x40\x83\x43\x83\x8b\x82\xf0\x96\xc2\x82"
     "\xe7\x82\xb3\x82\xc8\x82\xa2\x28\x26\x44\x29"},
    {0x12, "set default folder to previous(&E)",
     "\xc3\xde\xcc\xab\xd9\xc4\xcc\xab\xd9\xc0\xde\x82\xf0\x91\x4f\x89"
     "\xf1\x88\xca\x92\x75\x82\xc6\x82\xb7\x82\xe9\x28\x26\x45\x29"},
    {0x14, "Exit(&X)", "\x8f\x49\x97\xb9\x28\x26\x58\x29"},
};

constexpr MenuText kEditMenu[] = {
    {0, "bone camera numeric input(&O)",
     "\x83\x7b\x81\x5b\x83\x93\x81\x45\x83\x4a\x83\x81\x83\x89\x90\x94"
     "\x92\x6c\x93\xfc\x97\xcd\x28\x26\x4f\x29"},
    {1, "bone camera angle initialize(&Z)",
     "\x83\x7b\x81\x5b\x83\x93\x81\x45\x83\x4a\x83\x81\x83\x89\x8a\x70"
     "\x93\x78\x30\x89\xbb\x28\x26\x5a\x29"},
    {3, "delete unused frame(&D)",
     "\x95\x73\x97\x76\xcc\xda\xb0\xd1\x8d\xed\x8f\x9c\x28\x26\x44\x29"},
    {5, "select all camera frame(&C)",
     "\xb6\xd2\xd7\xcc\xda\xb0\xd1\x82\xb7\x82\xd7\x82\xc4\x91\x49\x91"
     "\xf0\x28\x26\x43\x29"},
    {6, "select all light frame(&L)",
     "\x8f\xc6\x96\xbe\xcc\xda\xb0\xd1\x82\xb7\x82\xd7\x82\xc4\x91\x49"
     "\x91\xf0\x28\x26\x4c\x29"},
    {7, "select all self shadow frame(&S)",
     "\xbe\xd9\xcc\x89\x65\xcc\xda\xb0\xd1\x82\xb7\x82\xd7\x82\xc4\x91"
     "\x49\x91\xf0\x28\x26\x53\x29"},
    {8, "select all gravity frame(&V)",
     "\x8f\x64\x97\xcd\xcc\xda\xb0\xd1\x82\xb7\x82\xd7\x82\xc4\x91\x49"
     "\x91\xf0\x28\x26\x56\x29"},
    {9, "select all accessory frame(&A)",
     "\xb1\xb8\xbe\xbb\xd8\xcc\xda\xb0\xd1\x82\xb7\x82\xd7\x82\xc4\x91"
     "\x49\x91\xf0\x28\x26\x41\x29"},
    {0xB, "multiply of camera frame position-angle(&G)",
     "\xb6\xd2\xd7\xcc\xda\xb0\xd1\x88\xca\x92\x75\x8a\x70\x93\x78\x95"
     "\xe2\x90\xb3\x28\x26\x47\x29"},
    {0xD, "select all bone frame(&N)",
     "\xce\xde\xb0\xdd\xcc\xda\xb0\xd1\x82\xb7\x82\xd7\x82\xc4\x91\x49"
     "\x91\xf0\x28\x26\x4e\x29"},
    {0xE, "select all facial frames(&E)",
     "\x95\x5c\x8f\xee\xcc\xda\xb0\xd1\x82\xb7\x82\xd7\x82\xc4\x91\x49"
     "\x91\xf0\x28\x26\x45\x29"},
    {0xF, "select all disp/IK/OP frame(&M)",
     "\x95\x5c\x8e\xa6\xa5\x49\x4b\xa5\x8a\x4f\x90\x65\xcc\xda\xb0\xd1"
     "\x82\xb7\x82\xd7\x82\xc4\x91\x49\x91\xf0\x28\x26\x4d\x29"},
    {0x11, "paste to different flame(F_key)",
     "\x95\xca\xcc\xda\xb0\xd1\x82\xd6\xcd\xdf\xb0\xbd\xc4\x28\x46\xb7"
     "\xb0\x29"},
    {0x13, "insert frame line(bone or camera)(I_key)",
     "\x8b\xf3\xcc\xda\xb0\xd1\x91\x7d\x93\xfc\x28\xce\xde\xb0\xdd\x6f"
     "\x72\xb6\xd2\xd7\x29\x28\x49\xb7\xb0\x29"},
    {0x14, "delete frame line(bone or camera)(K_key)",
     "\x97\xf1\xcc\xda\xb0\xd1\x8d\xed\x8f\x9c\x28\xce\xde\xb0\xdd\x6f"
     "\x72\xb6\xd2\xd7\x29\x28\x4b\xb7\xb0\x29"},
    {0x15, "insert frame line(facial or light)(U_key)",
     "\x8b\xf3\xcc\xda\xb0\xd1\x91\x7d\x93\xfc\x28\x95\x5c\x8f\xee\x6f"
     "\x72\x8f\xc6\x96\xbe\x29\x28\x55\xb7\xb0\x29"},
    {0x16, "delete frame line(facial or light)(J_key)",
     "\x97\xf1\xcc\xda\xb0\xd1\x8d\xed\x8f\x9c\x28\x95\x5c\x8f\xee\x6f"
     "\x72\x8f\xc6\x96\xbe\x29\x28\x4a\xb7\xb0\x29"},
    {0x18, "multiply of bone frame position-angle(R_key)",
     "\xce\xde\xb0\xdd\xcc\xda\xb0\xd1\x88\xca\x92\x75\x8a\x70\x93\x78"
     "\x95\xe2\x90\xb3\x28\x52\xb7\xb0\x29"},
    {0x19, "multiply of facial expression(&T)",
     "\x95\x5c\x8f\xee\x91\xe5\x82\xab\x82\xb3\x95\xe2\x90\xb3\x28\x26"
     "\x54\x29"},
    {0x1B, "apply center position bias(&B)",
     "\xbe\xdd\xc0\xb0\x88\xca\x92\x75\xca\xde\xb2\xb1\xbd\x95\x74\x89"
     "\xc1\x28\x26\x42\x29"},
};

constexpr MenuText kViewMenu[] = {
    {0, "screen size(&O)",
     "\x8f\x6f\x97\xcd\x83\x54\x83\x43\x83\x59\x28\x26\x4f\x29"},
    {2, "separate window(&W)", "\x95\xca\x91\x8b\x28\x26\x57\x29"},
    {3, "to the fore(&F)",
     "\x95\xca\x91\x8b\x8d\xc5\x91\x4f\x97\xf1\x95\x5c\x8e\xa6\x28\x26"
     "\x46\x29"},
    {5, "camera & lighting tracking(&C)",
     "\xd3\xc3\xde\xd9\x95\xd2\x8f\x57\x8e\x9e\xb6\xd2\xd7\xa5\x8f\xc6"
     "\x96\xbe\x92\xc7\x8f\x5d\x28\x26\x43\x29"},
    {7, "information display(&D)",
     "\x8f\xee\x95\xf1\x95\x5c\x8e\xa6\x28\x26\x44\x29"},
    {8, "display coordinate axis(&G)",
     "\x8d\xc0\x95\x57\x8e\xb2\x95\x5c\x8e\xa6\x28\x26\x47\x29"},
    {0xA, "display ground shadow(&S)",
     "\x92\x6e\x96\xca\x89\x65\x95\x5c\x8e\xa6\x28\x26\x53\x29"},
    {0xB, "ground shadow color(&X)",
     "\x92\x6e\x96\xca\x89\x65\x90\x46\x90\xdd\x92\xe8\x28\x26\x58\x29"},
    {0xC, "transparent ground shadow(&T)",
     "\x92\x6e\x96\xca\x89\x65\x90\x46\x93\xa7\x96\xbe\x89\xbb\x28\x26"
     "\x54\x29"},
    {0xE, "character transparent mode(V_key)",
     "\x94\xbc\x93\xa7\x96\xbe\x89\xbb\x28\x56\xb7\xb0\x29"},
    {0xF, "character Non-display mode(&A)",
     "\x83\x82\x83\x66\x83\x8b\x94\xf1\x95\x5c\x8e\xa6\x28\x26\x41\x29"},
    {0x11, "thickness of edge line(&E)",
     "\x83\x47\x83\x62\x83\x57\x91\xbe\x82\xb3\x28\x26\x45\x29"},
    {0x12, "edge line color(&B)",
     "\x83\x47\x83\x62\x83\x57\x90\x46\x28\x26\x42\x29"},
    {0x14, "anti-aliasing(&H)",
     "\x83\x41\x83\x93\x83\x60\x83\x47\x83\x43\x83\x8a\x83\x41\x83\x58"
     "\x28\x26\x48\x29"},
    {0x16, "mipmap(anisotropic)(&M)",
     "\x83\x7e\x83\x62\x83\x76\x83\x7d\x83\x62\x83\x76\x28\x88\xd9\x95"
     "\xfb\x90\xab\x83\x74\x83\x42\x83\x8b\x83\x5e\x29\x28\x26\x4d\x29"},
    {0x18, "self-shadow(&P)",
     "\x83\x5a\x83\x8b\x83\x74\x83\x56\x83\x83\x83\x68\x83\x45\x95\x5c"
     "\x8e\xa6\x28\x26\x50\x29"},
    {0x1A, "wire frame(&R)",
     "\x83\x8f\x83\x43\x83\x84\x81\x5b\x83\x74\x83\x8c\x81\x5b\x83\x80"
     "\x95\x5c\x8e\xa6\x28\x26\x52\x29"},
    {0x1C, "full screen(Alt+Enter)",  // replaced below when stereo is on
     "\x83\x74\x83\x8b\x83\x58\x83\x4e\x83\x8a\x81\x5b\x83\x93\x95\x5c"
     "\x8e\xa6\x28\x41\x6c\x74\x2b\x45\x6e\x74\x65\x72\x29"},
    {0x1E, "fps no limit", "\x66\x70\x73\x96\xb3\x90\xa7\x8c\xc0"},
    {0x1F, "max fps restricted to 30fps",
     "\x33\x30\x66\x70\x73\x90\xa7\x8c\xc0"},
    {0x20, "max fps restricted to 60fps",
     "\x36\x30\x66\x70\x73\x90\xa7\x8c\xc0"},
    {0x22, "save CPU power",
     "\x8f\xc8\x83\x47\x83\x6c\x83\x82\x81\x5b\x83\x68"},
};

constexpr MenuText kBackgroundMenu[] = {
    {0, "accessories edit(&A)",
     "\x83\x41\x83\x4e\x83\x5a\x83\x54\x83\x8a\x95\xd2\x8f\x57\x28\x26"
     "\x41\x29"},
    {1, "model draw order(&O)",
     "\x83\x82\x83\x66\x83\x8b\x95\x60\x89\xe6\x8f\x87\x28\x26\x4f\x29"},
    {2, "model calculate order(&C)",
     "\x83\x82\x83\x66\x83\x8b\x8c\x76\x8e\x5a\x8f\x87\x28\x26\x43\x29"},
    {4, "black background(&D)",
     "\x94\x77\x8c\x69\x8d\x95\x89\xbb\x28\x26\x44\x29"},
    {6, "load background AVI file(&L)",
     "\x94\x77\x8c\x69\x41\x56\x49\x83\x74\x83\x40\x83\x43\x83\x8b\x93"
     "\xc7\x8d\x9e\x28\x26\x4c\x29"},
    {7, "load background picture file(&R)",
     "\x94\x77\x8c\x69\x89\xe6\x91\x9c\x83\x74\x83\x40\x83\x43\x83\x8b"
     "\x93\xc7\x8d\x9e\x28\x26\x52\x29"},
    {9, "show background AVI file(&A)",
     "\x94\x77\x8c\x69\x41\x56\x49\x95\x5c\x8e\xa6\x28\x26\x41\x29"},
    {0xA, "show background picture file(&P)",
     "\x94\x77\x8c\x69\x89\xe6\x91\x9c\x95\x5c\x8e\xa6\x28\x26\x50\x29"},
    {0xC, "screen capture mode OFF(&V)",
     "\xbd\xb8\xd8\xb0\xdd\x97\x70\xb7\xac\xcc\xdf\xc1\xac\x4f\x46\x46"
     "\x28\x26\x56\x29"},
    {0xD, "ON.mode01(&M)",
     "\x4f\x4e\xa5\x83\x82\x81\x5b\x83\x68\x82\x50\x28\x91\x53\x89\xe6"
     "\x96\xca\x29\x20\x28\x26\x4d\x29"},
    {0xE, "ON.mode02(&N)",
     "\x4f\x4e\xa5\x83\x82\x81\x5b\x83\x68\x82\x51\x28\x34\x3a\x33\x94"
     "\xe4\x97\xa6\x29\x20\x28\x26\x4e\x29"},
    {0xF, "ON.mode03(&B)",
     "\x4f\x4e\xa5\x83\x82\x81\x5b\x83\x68\x82\x52\x28\x94\x77\x8c\x69"
     "\x41\x56\x49\x29\x20\x28\x26\x42\x29"},
};

constexpr MenuText kFacialMenu[] = {
    {0, "delete all mouse frame",
     "\x83\x8a\x83\x62\x83\x76\x83\x74\x83\x8c\x81\x5b\x83\x80\x91\x53"
     "\x82\xc4\x8d\xed\x8f\x9c"},
    {1, "lip-sync with .VSQ file",
     "\x76\x73\x71\x82\xc9\x82\xe6\x82\xe9\x83\x8a\x83\x62\x83\x76\x83"
     "\x56\x83\x93\x83\x4e"},
    {2, "time shifting mouse frame",
     "\x83\x8a\x83\x62\x83\x76\x83\x74\x83\x8c\x81\x5b\x83\x80\x8e\x9e"
     "\x8a\xd4\x83\x56\x83\x74\x83\x67"},
    {4, "delete all eye frame",
     "\x96\xda\x83\x74\x83\x8c\x81\x5b\x83\x80\x91\x53\x82\xc4\x8d\xed"
     "\x8f\x9c"},
    {5, "randomly register blinking",
     "\x82\xdc\x82\xce\x82\xbd\x82\xab\x83\x89\x83\x93\x83\x5f\x83\x80"
     "\x93\x6f\x98\x5e"},
    {7, "delete all eyebrow frame",
     "\x82\xdc\x82\xe4\x83\x74\x83\x8c\x81\x5b\x83\x80\x91\x53\x82\xc4"
     "\x8d\xed\x8f\x9c"},
    {9, "reset all facial value",
     "\x91\x53\x82\xc4\x82\xcc\x95\x5c\x8f\xee\x83\x8a\x83\x5a\x83\x62"
     "\x83\x67"},
    {0xB, "regist all facial frame(H_key)",
     "\x91\x53\x82\xc4\x82\xcc\x95\x5c\x8f\xee\x83\x74\x83\x8c\x81\x5b"
     "\x83\x80\x93\x6f\x98\x5e\x28\x48\xb7\xb0\x29"},
};

constexpr MenuText kPhysicsMenu[] = {
    {0, "on/off mode(&O)",
     "\x83\x49\x83\x93\x2f\x83\x49\x83\x74\x83\x82\x81\x5b\x83\x68\x28"
     "\x26\x4f\x29"},
    {1, "anytime(&E)", "\x8f\xed\x82\xc9\x89\x89\x8e\x5a\x28\x26\x45\x29"},
    {2, "trace mode(&T)",
     "\x83\x67\x83\x8c\x81\x5b\x83\x58\x83\x82\x81\x5b\x83\x68\x28\x26"
     "\x54\x29"},
    {3, "no calculation(&N)",
     "\x89\x89\x8e\x5a\x82\xb5\x82\xc8\x82\xa2\x28\x26\x4e\x29"},
    {5, "playtime use on/off mode(&P)",
     "\x8d\xc4\x90\xb6\x8e\x9e\x82\xcd\x8f\xed\x82\xc9\x83\x49\x83\x93"
     "\x2f\x83\x49\x83\x74\x83\x82\x81\x5b\x83\x68\x82\xc9\x82\xb7\x82"
     "\xe9\x28\x26\x50\x29"},
    {7, "display bodies(&D)",
     "\x8d\x84\x91\xcc\x95\x5c\x8e\xa6\x28\x26\x44\x29"},
    {9, "gravity setting(&G)",
     "\x8f\x64\x97\xcd\x90\xdd\x92\xe8\x28\x26\x47\x29"},
    {0xA, "initialize bodies position(&I)",
     "\x8d\x84\x91\xcc\x88\xca\x92\x75\x8f\x89\x8a\xfa\x89\xbb\x28\x26"
     "\x49\x29"},
    {0xC, "floor(&F)", "\x8f\xb0\x28\x26\x46\x29"},
    {0xE, "select physical bone(&B)",
     "\x95\xa8\x97\x9d\x89\x65\x8b\xbf\x83\x7b\x81\x5b\x83\x93\x91\x49"
     "\x91\xf0\x28\x26\x42\x29"},
    {0xF, "select physics ON frame(X mark)(&X)",
     "\x91\x53\x82\xc4\x82\xcc\x95\xa8\x97\x9d\x4f\x4e\x83\x74\x83\x8c"
     "\x81\x5b\x83\x80\x28\x58\x88\xf3\x29\x91\x49\x91\xf0\x28\x26\x58"
     "\x29"},
    {0x10, "change physics ON/OFF frame(&C)",
     "\x95\xa8\x97\x9d\x4f\x4e\x2f\x4f\x46\x46\x83\x74\x83\x8c\x81\x5b"
     "\x83\x80\x95\xcf\x8a\xb7\x28\x26\x43\x29"},
    {0x12, "about physical engine(&A)",
     "\x95\xa8\x97\x9d\x83\x47\x83\x93\x83\x57\x83\x93\x82\xc9\x82\xc2"
     "\x82\xa2\x82\xc4\x28\x26\x41\x29"},
};

constexpr MenuText kMocapMenu[] = {
    {0, "Kinect(&K)", "Kinect(&K)"},  // one shared literal in the original
    {2, "capture(&C)", "\xb7\xac\xcc\xdf\xc1\xac\x28\x26\x43\x29"},
    {4, "L-R reversing(&R)",
     "\x8d\xb6\x89\x45\x94\xbd\x93\x5d\x28\x26\x52\x29"},
    {5, "initialize lost bone(&L)",
     "\xdb\xbd\xc4\xce\xde\xb0\xdd\x8f\x89\x8a\xfa\x89\xbb\x28\x26\x4c"
     "\x29"},
    {6, "display red man(&D)",
     "\x90\xd4\x90\x6c\x95\x5c\x8e\xa6\x28\x26\x44\x29"},
    {8, "load oni-file(&O)",
     "\x6f\x6e\x69\xcc\xa7\xb2\xd9\x93\xc7\x8d\x9e\x28\x26\x4f\x29"},
};

constexpr MenuText kHelpMenu[] = {
    {0, "Japanese Mode(&J)", "English Mode(&E)"},
    {2, "enhance model(&M)",
     "\x83\x82\x83\x66\x83\x8b\x8a\x67\x92\xa3\x28\x26\x4d\x29"},
    {4, "restore texture(&R)",
     "\xc3\xb8\xbd\xc1\xac\x93\xc7\x92\xbc\x82\xb5\x28\x26\x52\x29"},
    {6, "About(&A)",
     "\xca\xde\xb0\xbc\xde\xae\xdd\x8f\xee\x95\xf1\x28\x26\x49\x29"},
};

// the "enhance model" submenu below help item 2
constexpr MenuText kEnhanceMenu[] = {
    {0, "edit English name(&E)",
     "\x89\x70\x8c\xea\x96\xbc\x95\xd2\x8f\x57\x28\x26\x45\x29"},
    {1, "toon texture(&T)",
     "\xc4\xa9\xb0\xdd\xc3\xb8\xbd\xc1\xac\x95\xcf\x8d\x58\x28\x26\x54"
     "\x29"},
    {2, "physics model(&B)",
     "\x95\xa8\x97\x9d\x89\x89\x8e\x5a\x95\xd2\x8f\x57\x28\x26\x42\x29"},
    {4, "save enhanced model(&P)",
     "\x8a\x67\x92\xa3\x83\x82\x83\x66\x83\x8b\x95\xdb\x91\xb6\x28\x26"
     "\x50\x29"},
};

struct MenuBlock {
    int topPos;
    const char* titleEn;
    const char* titleJp;
    const MenuText* items;
    int itemCount;
};

constexpr MenuBlock kMenus[] = {
    {0, "file(&F)",
     "\x83\x74\x83\x40\x83\x43\x83\x8b\x28\x26\x46\x29", kFileMenu,
     static_cast<int>(sizeof(kFileMenu) / sizeof(kFileMenu[0]))},
    {1, "edit(&D)",
     "\x95\xd2\x8f\x57\x28\x26\x44\x29", kEditMenu,
     static_cast<int>(sizeof(kEditMenu) / sizeof(kEditMenu[0]))},
    {2, "view(&V)",
     "\x95\x5c\x8e\xa6\x28\x26\x56\x29", kViewMenu,
     static_cast<int>(sizeof(kViewMenu) / sizeof(kViewMenu[0]))},
    {3, "background(&B)",
     "\x94\x77\x8c\x69\x28\x26\x42\x29", kBackgroundMenu,
     static_cast<int>(sizeof(kBackgroundMenu) / sizeof(kBackgroundMenu[0]))},
    {4, "facial expression(&M)",
     "\x95\x5c\x8f\xee\x28\x26\x4d\x29", kFacialMenu,
     static_cast<int>(sizeof(kFacialMenu) / sizeof(kFacialMenu[0]))},
    {5, "physical operation(&P)",
     "\x95\xa8\x97\x9d\x89\x89\x8e\x5a\x28\x26\x50\x29", kPhysicsMenu,
     static_cast<int>(sizeof(kPhysicsMenu) / sizeof(kPhysicsMenu[0]))},
    {6, "motion capture(&K)",
     "\xd3\xb0\xbc\xae\xdd\xb7\xac\xcc\xdf\xc1\xac\x28\x26\x4b\x29",
     kMocapMenu,
     static_cast<int>(sizeof(kMocapMenu) / sizeof(kMocapMenu[0]))},
    {7, "help(&H)",
     "\x83\x77\x83\x8b\x83\x76\x28\x26\x48\x29", kHelpMenu,
     static_cast<int>(sizeof(kHelpMenu) / sizeof(kHelpMenu[0]))},
};

}  // namespace

void RefreshMenuLanguage(MMDApp* app) {  // VA 0x0040B5A0
    HWND hwnd = app->state.hwnd;
    HMENU menu = GetMenu(hwnd);
    MENUITEMINFOA mii;
    std::memset(&mii, 0, sizeof(mii));
    mii.cbSize = sizeof(mii);
    mii.fMask = 0x40 /*MIIM_STRING*/;
    const bool english = app->state.englishUI != 0;

    for (const MenuBlock& block : kMenus) {
        ModifyMenuA(menu, block.topPos, 0x400 /*MF_BYPOSITION*/, 0,
                    english ? block.titleEn : block.titleJp);
        HMENU sub = GetSubMenu(menu, block.topPos);
        for (int i = 0; i < block.itemCount; ++i) {
            const MenuText& item = block.items[i];
            mii.dwTypeData =
                const_cast<char*>(english ? item.en : item.jp);
            SetMenuItemInfoA(sub, static_cast<UINT>(item.pos), TRUE, &mii);
        }
    }

    // the full-screen item becomes the 3D-Vision variant when the renderer
    // has NVAPI stereo enabled
    D3DRenderer* renderer = app->Renderer();
    if (renderer != nullptr && renderer->stereoEnabled != 0) {
        HMENU sub = GetSubMenu(menu, 2);
        mii.dwTypeData = const_cast<char*>(
            english ? "NDIVIA 3D Vision(Alt+Enter)"
                    : "\x4e\x56\x49\x44\x49\x41\x20\x33\x44\x20\x56\x69"
                      "\x73\x69\x6f\x6e\x95\x5c\x8e\xa6\x28\x41\x6c\x74"
                      "\x2b\x45\x6e\x74\x65\x72\x29");
        SetMenuItemInfoA(sub, 0x1C, TRUE, &mii);
    }

    // the "enhance model" submenu hangs below help item 2
    {
        HMENU sub = GetSubMenu(GetSubMenu(menu, 7), 2);
        for (const MenuText& item : kEnhanceMenu) {
            mii.dwTypeData =
                const_cast<char*>(english ? item.en : item.jp);
            SetMenuItemInfoA(sub, static_cast<UINT>(item.pos), TRUE, &mii);
        }
    }

    DrawMenuBar(hwnd);
}
// VA 0x0042AE20 - path copy: real port moved to src/media/media_load.cpp.

// ===========================================================================
// 0x4076E0 (x64 sub_7FF7CB428F30) - ReloadTextureCache:
// renderer refresh after the toon reload (menu 278, thiscall on the
// renderer object).  Walks the shared 10000-entry texture cache and
// reloads every named entry from disk via D3DXCreateTextureFromFileExW
// (1024 mipmaps with the 1x1 no-mipmap retry), re-sampling the
// bottom-left pixel colour into the entry's tag bytes; entries that fail
// to reload keep a null texture with zeroed colour.
// ===========================================================================
void ReloadTextureCache(void* rendererArg) {
    D3DRenderer* renderer = static_cast<D3DRenderer*>(rendererArg);
    IDirect3DDevice9* device = renderer->device;
    auto* d3dx = &d3dx::Get();

    struct ImgInfo {  // first fields of D3DXIMAGE_INFO
        UINT Width, Height, Depth, MipLevels;
        UINT rest[16];
    };

    for (int i = 0; i < 10000; ++i) {
        ResourcePoolEntry& entry = renderer->resourcePool[i];
        if (entry.heapBuffer == nullptr)
            break;  // end of the used region
        auto* name = static_cast<wchar_t*>(entry.heapBuffer);
        auto*& texture =
            reinterpret_cast<IDirect3DTexture9*&>(entry.comObject);
        if (texture != nullptr) {
            texture->Release();
            texture = nullptr;
        }
        unsigned char* rgb =
            reinterpret_cast<unsigned char*>(&entry.tag);
        ImgInfo info = {};
        HRESULT hr = d3dx->fromFileExW(
            device, name, kD3dxDefault, kD3dxDefault, 1, 1024,
            D3DFMT_A8R8G8B8, D3DPOOL_MANAGED, kD3dxDefault, kD3dxDefault, 0,
            &info, nullptr, &texture);
        if (info.Width == 1 && info.Height == 1 && SUCCEEDED(hr)) {
            if (texture != nullptr) {
                texture->Release();
                texture = nullptr;
            }
            hr = d3dx->fromFileExW(
                device, name, kD3dxDefault, kD3dxDefault, 1, 0,
                D3DFMT_A8R8G8B8, D3DPOOL_MANAGED, kD3dxDefault,
                kD3dxDefault, 0, &info, nullptr, &texture);
        }
        if (FAILED(hr) &&
            FAILED(d3dx->fromFileExW(
                device, name, kD3dxDefault, kD3dxDefault, 1, 0,
                D3DFMT_A8R8G8B8, D3DPOOL_MANAGED, kD3dxDefault,
                kD3dxDefault, 0, &info, nullptr, &texture))) {
            texture = nullptr;
            rgb[0] = rgb[1] = rgb[2] = 0;
            continue;
        }
        D3DLOCKED_RECT rect;
        if (texture != nullptr &&
            SUCCEEDED(texture->LockRect(0, &rect, nullptr, 0))) {
            const unsigned char* row =
                static_cast<const unsigned char*>(rect.pBits) +
                rect.Pitch * (info.Height ? info.Height - 1 : 0);
            rgb[2] = row[0];  // blue
            rgb[1] = row[1];  // green
            rgb[0] = row[2];  // red
            texture->UnlockRect(0);
        }
    }
}

// ---- helpers ported in other translation units (declared here with their
//      original VAs; not yet registered in ported_funcs.hpp) --------------

// VA 0x004629D0 - fullscreen enter/restore window manager; the real body
// now lives in src/app/avi_record_start.cpp (declared in ported_funcs.hpp).

}  // namespace mikudancestudio
