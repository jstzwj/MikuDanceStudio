// ===========================================================================
// VA 0x004BF3E0 - ModelLoadPMD  (original: sub_4BF3E0, 0x2C95 bytes)
// ===========================================================================
// The PMD binary parser, __thiscall on the freshly initialised 0x4CCF4
// model block.  Signature (verified against the 0x46050F push sequence):
//
//   model->LoadPMD(a2 hwnd, a3 widePath, a4 sub1d574, a5 app+0xA06CE,
//                  a6 = 1, a7 englishUI, a8 sub048ptr, a9 appPathBuf)
//
// a7 (EnglishUI, *(app+0xA0B4C)) is stored to m+12740 and gates all
// English-vs-Japanese message paths; a8 (*(app+0x9EDB0), the 0x48-byte
// subsystem object) is stored to m+60 and doubles as the physics scene
// wrapper (gizmo buffers +4..44, constraint-id counter +56,
// btDiscreteDynamicsWorld* +64).  The loader's physics section passes
// m+60 to 0x4064F0/0x406010.
//
// The file is opened with the wide CRT entry (0x50747B - actually
// _wsopen_s; the FLIRT name __sopen_s is a mislabel, see
// docs/ARCHITECTURE.md).  Material/texture names are narrow Shift-JIS
// and converted via 0x407DA0; file opens probe through 0x4089F0 and the
// shared texture cache 0x407490.
//
// Physics (rigid bodies + joints) is parsed and its transform chains
// computed with the same D3DX calls as the original; body/constraint
// creation goes through the ported 0x4064F0/0x406010 (null-world
// guarded until the 0x4032B0 scene constructor lands).
// =========================================================================//
#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <Windows.h>
#include <d3d9.h>

#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <cwchar>
#include <cmath>

#include <fcntl.h>
#include <io.h>
#include <share.h>
#include <sys/stat.h>

#include "mikudancestudio/d3dx_dyn.hpp"
#include "mikudancestudio/d3d_wrapper.hpp"
#include "mikudancestudio/model.hpp"
#include "mikudancestudio/model_format_header.hpp"
#include "mikudancestudio/path_workspace.hpp"
#include "mikudancestudio/physics_scene.hpp"
#include "mikudancestudio/ported_funcs.hpp"
#include "mikudancestudio/model_cpu_bridge.hpp"
#include "model_format_win.hpp"
#include "mikudancestudio/model_geometry.hpp"
#include "mikudancestudio/model_sections.hpp"

namespace mikudancestudio {
namespace {

// Shared Shift-JIS byte literals, maintained in model_labels.inc.
// Preserve the original encoded bytes when editing; no generator is shipped.
#include "model_labels.inc"

IDirect3DDevice9* DevOf(D3DRenderer* sub) {
    return sub ? sub->device : nullptr; // wrapper + 120032
}

bool FailBox(HWND hwnd, bool en, bool enData, const char* enMsg, const char* jpMsg,
             const char* enTitle, const char* jpTitle) {
    char text[256];
    if (enData) {
        sprintf_s(text, 0x100, "%s", enMsg);
        MessageBoxA(hwnd, text, enTitle, 0);
    } else {
        sprintf_s(text, 0x100, "%s", jpMsg);
        MessageBoxA(hwnd, text, jpTitle, 0);
    }
    return false;
}

// Porting-era trace under MIKUDANCESTUDIO_PMM_TRACE_DIR (CMake option
// MIKUDANCESTUDIO_DIAG, default OFF); the OFF stub keeps the call site
// valid and inlines away to nothing.
#ifdef MIKUDANCESTUDIO_DIAG
void LogPmdRigid(const wchar_t* path, std::uint32_t rigidIndex, std::int32_t boneIndex,
                 std::uint32_t boneCount, const void* bones) {
    const char* dir = std::getenv("MIKUDANCESTUDIO_PMM_TRACE_DIR");
    if (dir == nullptr || dir[0] == '\0')
        return;
    char logPath[MAX_PATH];
    sprintf_s(logPath, "%s\\pmm_model_load.log", dir);
    FILE* stream = nullptr;
    if (fopen_s(&stream, logPath, "ab") != 0 || stream == nullptr)
        return;
    fprintf(stream,
            "pmd-rigid path=%ls rigid=%lu bone=%ld bone_count=%lu "
            "bones=%p\r\n",
            path != nullptr ? path : L"", static_cast<unsigned long>(rigidIndex),
            static_cast<long>(boneIndex), static_cast<unsigned long>(boneCount), bones);
    fclose(stream);
}
#else
inline void LogPmdRigid(const wchar_t*, std::uint32_t, std::int32_t, std::uint32_t, const void*) {
}
#endif

} // namespace

bool ModelLoadPMD(mdl::ModelRecord* m, HWND hwnd, const wchar_t* path, D3DRenderer* sub,
                  int unusedA06CE, std::uint8_t boxGate, std::uint8_t englishUi,
                  PhysicsScene* sub048, PathResolutionWorkspace& paths,
                  ModelResourceMode resources) { // 0x4BF3E0
    // Arg map (original 0x46050F call site, pushes -0x20..-0x4; the
    // trailing (aN) tags are the original decompiler parameter names):
    //   a2 hwnd, a3 path, a4 sub1d574 (device), unusedA06CE (a5) app+0xA06CE
    //   (unused), boxGate (a6) constant 1 (message-box gate), englishUi
    //   (a7) EnglishUI *(app+0xA0B4C) (stored to m+12740), sub048 (a8) the
    //   0x048 pointer *(app+0x9EDB0), typed as PhysicsScene* (stored to
    //   m+60 - the physics scene wrapper: gizmos +4..44, constraint-id
    //   counter +56, btDiscreteDynamicsWorld* +64), a9 appPathBuf.
    const bool enData = englishUi != 0;
    const bool cpuOnly = resources == ModelResourceMode::CpuOnly;
    mdl::ModelRecord& model = *m;
    char text[256];
    wchar_t wide1[256], wide2[384], wide3[384];

    model.hwnd = hwnd;
    model.scenePtr = sub048;        // 0x4BF42B
    model.physicsFlags = englishUi; // 0x4BF431: English-data byte

    ResolveUserFilePath(paths, path); // 0x4089F0
    wcscpy_s(model.path, 0x100, paths.resolvedPath);

    int fh = -1;
    errno_t err = _wsopen_s(&fh, model.path, _O_BINARY, _SH_DENYNO, _S_IREAD);
    if (err != 0) {
        if (boxGate) {
            if (enData)
                sprintf_s(text, 0x100,
                          "Cannot open file:%d\n"
                          "\n"
                          "If Japanese font is included in the filename"
                          "(*.pmd),please rewrite it in English font.",
                          err),
                    MessageBoxA(hwnd, text, "open file", 0);
            else
                sprintf_s(text, 0x100, "%s:%d", kMsgOpenFailJp, err),
                    MessageBoxA(hwnd, text, kTitleOpenFailJp, 0);
        }
        return false;
    }

    // directory of the model file -> current directory
    wcscpy_s(model.modelDirectory, 0x100, model.path);
    {
        wchar_t* p = model.modelDirectory;
        while (*p)
            ++p;
        if (*p != L'\\')
            do {
                --p;
            } while (p != model.modelDirectory && *p != L'\\');
        p[1] = L'\0';
        if (!cpuOnly)
            SetCurrentDirectoryW(model.modelDirectory);
    }

    // ---- header -----------------------------------------------------------
    if (_read(fh, text, 3) != 3) {
        _close(fh);
        return false;
    }
    const auto kind = format::IdentifyModel({text[0], text[1], text[2]});
    if (kind != format::ModelKind::Pmd) {
        if (kind == format::ModelKind::Pmx)
            return LoadPMX(m, sub, boxGate, englishUi, paths, fh, resources); // 0x4B77E0
        _close(fh);
        if (enData) {
            sprintf_s(text, 0x100, "This file is not for Ploygon Movie Maker !!");
            MessageBoxA(hwnd, text, "open file", 0);
        } else {
            sprintf_s(text, 0x100, "%s", kMsgNotPmdJp);
            MessageBoxA(hwnd, text, kTitleOpenFailJp, 0);
        }
        return false;
    }
    format::PmdHeader header;
    if (format::ReadPmdHeader(ReadModelFileBytes, &fh, header) != format::ModelHeaderStatus::Ok) {
        _close(fh);
        return false;
    }
    std::memcpy(model.name, header.name.data(), header.name.size());
    std::memcpy(model.comment, header.comment.data(), header.comment.size());
    if (!enData && boxGate && MessageBoxA(hwnd, model.comment, kTitleModelInfoJp, 0x40001u) != 1) {
        _close(fh);
        return false;
    }

    IDirect3DDevice9* dev = cpuOnly ? nullptr : DevOf(sub);
    if (!cpuOnly && !dev) {
        _close(fh);
        return false;
    }
    auto* d3dx = &d3dx::Get();

    // ---- vertices ---------------------------------------------------------
    if (!format::ReadGeometryCount(ReadModelFileBytes, &fh, model.vertexCount)) {
        _close(fh);
        return false;
    }
    const std::uint32_t vertCount = model.vertexCount;
    if (vertCount != 0) {
        if (dev && FAILED(dev->CreateVertexBuffer(
                       32 * vertCount, 8, 274 /*FVF*/, D3DPOOL_MANAGED,
                       &mdl::ResourceAs<IDirect3DVertexBuffer9>(model.vertexBuffer), nullptr))) {
            FailBox(hwnd, true, enData, "The performance of the graphics card doesn't suffice.",
                    kMsgNoPerfJp, "open file", kTitleOpenFailJp);
            _close(fh);
            return false;
        }
        if (dev && FAILED(dev->CreateVertexBuffer(
                       16 * vertCount, 8, 66 /*FVF*/, D3DPOOL_MANAGED,
                       &mdl::ResourceAs<IDirect3DVertexBuffer9>(model.vertexBuffer2), nullptr))) {
            FailBox(hwnd, true, enData, "The performance of the graphics card doesn't suffice.",
                    kMsgNoPerfJp, "open file", kTitleLoad2Jp);
            _close(fh);
            return false;
        }
        mdl::PmdVertex* recs =
            static_cast<mdl::PmdVertex*>(operator new(sizeof(mdl::PmdVertex) * vertCount));
        model.rawVertices = recs;
        std::memset(recs, 0, sizeof(mdl::PmdVertex) * vertCount);
        for (std::uint32_t i = 0; i < vertCount; ++i) {
            format::PmdVertexData vertex;
            if (!format::ReadPmdVertex(ReadModelFileBytes, &fh, vertex)) {
                _close(fh);
                return false;
            }
            std::memcpy(recs[i].position, vertex.position, sizeof(vertex.position));
            std::memcpy(recs[i].normal, vertex.normal, sizeof(vertex.normal));
            std::memcpy(recs[i].uv, vertex.uv, sizeof(vertex.uv));
            std::memcpy(recs[i].bone, vertex.bone, sizeof(vertex.bone));
            recs[i].weightPercent = static_cast<std::int8_t>(vertex.weightPercent);
            recs[i].edgeDisabled = vertex.edgeDisabled;
        }

        float* p1 = nullptr;
        if (dev && SUCCEEDED(mdl::ResourceAs<IDirect3DVertexBuffer9>(m->vertexBuffer)
                                 ->Lock(0, 32 * vertCount, reinterpret_cast<void**>(&p1), 0))) {
            for (std::uint32_t i = 0; i < vertCount; ++i) {
                float* dst =
                    reinterpret_cast<float*>(reinterpret_cast<unsigned char*>(p1) + 32 * i);
                std::memcpy(dst, &recs[i], 32);
            }
            mdl::ResourceAs<IDirect3DVertexBuffer9>(model.vertexBuffer)->Unlock();
        }
        float* p2 = nullptr;
        if (dev && SUCCEEDED(mdl::ResourceAs<IDirect3DVertexBuffer9>(m->vertexBuffer2)
                                 ->Lock(0, 16 * vertCount, reinterpret_cast<void**>(&p2), 0))) {
            for (std::uint32_t i = 0; i < vertCount; ++i) {
                unsigned char* dst = reinterpret_cast<unsigned char*>(p2) + 16 * i;
                std::memcpy(dst, recs[i].position, 12);
                *reinterpret_cast<std::uint32_t*>(dst + 12) = 0xFF000000u;
            }
            mdl::ResourceAs<IDirect3DVertexBuffer9>(model.vertexBuffer2)->Unlock();
        }
    }

    // ---- indices ----------------------------------------------------------
    if (!format::ReadGeometryCount(ReadModelFileBytes, &fh, model.indexCount)) {
        _close(fh);
        return false;
    }
    const std::uint32_t idxCount = model.indexCount;
    if (idxCount != 0) {
        if (dev && FAILED(dev->CreateIndexBuffer(
                       2 * idxCount, 0, static_cast<D3DFORMAT>(101), D3DPOOL_MANAGED,
                       &mdl::ResourceAs<IDirect3DIndexBuffer9>(model.indexBuffer), nullptr))) {
            FailBox(hwnd, true, enData, "The performance of the graphics card doesn't suffice.",
                    kMsgNoPerfJp, "CreateIndexBuffer", "CreateIndexBuffer");
            _close(fh);
            return false;
        }
        std::int32_t*& idx = model.indices;
        idx = static_cast<std::int32_t*>(operator new(sizeof(*idx) * idxCount));
        std::memset(idx, 0, sizeof(*idx) * idxCount);
        void* ib = nullptr;
        if (dev &&
            FAILED(mdl::ResourceAs<IDirect3DIndexBuffer9>(m->indexBuffer)->Lock(0, 0, &ib, 0))) {
            FailBox(hwnd, true, enData, "The performance of the graphics card doesn't suffice.",
                    kMsgNoPerfJp, "pIndBuf->Lock", "pIndBuf->Lock");
            _close(fh);
            return false;
        }
        for (std::uint32_t j = 0; j < idxCount; ++j) {
            std::uint16_t index = 0;
            if (!format::ReadPmdIndex(ReadModelFileBytes, &fh, index)) {
                if (dev)
                    mdl::ResourceAs<IDirect3DIndexBuffer9>(m->indexBuffer)->Unlock();
                _close(fh);
                return false;
            }
            idx[j] = index;
            if (ib)
                static_cast<std::uint16_t*>(ib)[j] = index;
        }
        if (dev)
            mdl::ResourceAs<IDirect3DIndexBuffer9>(m->indexBuffer)->Unlock();
    }

    // ---- materials --------------------------------------------------------
    if (!ReadModelFileCount(fh, 4, m->materialCount)) {
        _close(fh);
        return false;
    }
    const std::uint32_t matCount = m->materialCount;
    if (matCount != 0) {
        mdl::ModelMaterialRecord*& materials = model.materials;
        materials =
            static_cast<mdl::ModelMaterialRecord*>(operator new(sizeof(*materials) * matCount));
        std::memset(materials, 0, sizeof(*materials) * matCount);
        for (std::uint32_t i = 0; i < matCount; ++i) {
            mdl::ModelMaterialRecord& mat = materials[i];
            format::MaterialData disk;
            if (!format::ReadPmdMaterial(ReadModelFileBytes, &fh, disk)) {
                _close(fh);
                return false;
            }
            std::memcpy(mat.diffuse, disk.diffuse, sizeof(mat.diffuse));
            std::memcpy(mat.diffuseMirror, mat.diffuse, sizeof(mat.diffuseMirror));
            mat.specularPower = disk.specularPower;
            std::memcpy(mat.specular, disk.specular, sizeof(mat.specular));
            std::memcpy(mat.ambient, disk.ambient, sizeof(mat.ambient));
            mat.toonReference = disk.toonReference;
            mat.doubleSided = disk.flags ? 1 : 0;
            mat.faceVertexCount = disk.faceVertexCount;
            char texName[24] = {};
            std::memcpy(texName, disk.pmdTexture, 20);
            if (texName[0] != '\0') {
                const wchar_t* dirW = model.modelDirectory;
                char* star = strstr(texName, "*");
                if (star != nullptr) {
                    *star = '\0';
                    D3DRenderer* subBytes = sub;
                    ConvertMaterialName(cpuOnly ? nullptr : subBytes, texName, wide1, 0x100, dirW);
                    swprintf_s(wide2, 0x180, L"%s%s", dirW, wide1);
                    if (!cpuOnly)
                        LoadTextureShared(subBytes, wide2); // 0x407490
                    ConvertMaterialName(cpuOnly ? nullptr : subBytes, star + 1, wide1, 0x100, dirW);
                    swprintf_s(wide3, 0x180, L"%s%s", dirW, wide1);
                    if (cpuOnly || LoadTextureShared(subBytes, wide3))
                        wcscpy_s(mat.spherePath, 0x118, wide3);
                    wcscpy_s(mat.texturePath, 0x118, wide2);
                } else {
                    D3DRenderer* subBytes = sub;
                    ConvertMaterialName(cpuOnly ? nullptr : subBytes, texName, wide1, 0x100, dirW);
                    swprintf_s(wide2, 0x180, L"%s%s", dirW, wide1);
                    if (!cpuOnly)
                        LoadTextureShared(subBytes, wide2);
                    wcscpy_s(mat.texturePath, 0x118, wide2);
                }
            }
        }
    }

    // ---- bones ------------------------------------------------------------
    if (!ReadModelFileCount(fh, 2, model.boneCount)) {
        _close(fh);
        return false;
    }
    const std::uint32_t boneCount = model.boneCount;
    mikudancestudio::mdl::BoneRecord* bones =
        static_cast<mikudancestudio::mdl::BoneRecord*>(operator new(
            sizeof(mikudancestudio::mdl::BoneRecord) * (boneCount ? boneCount : 1)));
    model.boneTable = bones;
    std::memset(bones, 0, sizeof(mikudancestudio::mdl::BoneRecord) * (boneCount ? boneCount : 1));
    {
        void* t1 = operator new(4 * (boneCount ? boneCount : 1));
        std::memset(t1, 0, 4 * (boneCount ? boneCount : 1));
        model.boneKeyCursors = static_cast<std::uint32_t*>(t1);
        void* t2 = operator new(boneCount ? boneCount : 1);
        std::memset(t2, 0, boneCount ? boneCount : 1);
        model.boneTrackActive = static_cast<unsigned char*>(t2);
        void* t3 = operator new(boneCount ? boneCount : 1);
        std::memset(t3, 0, boneCount ? boneCount : 1);
        model.boneSelection = static_cast<unsigned char*>(t3);
        static_cast<unsigned char*>(t3)[0] = 1;
        void* t4 = operator new(boneCount ? boneCount : 1);
        std::memset(t4, 0, boneCount ? boneCount : 1);
        model.bonePhysicsState = static_cast<unsigned char*>(t4);
    }
    std::uint8_t centerSeen = 0;
    for (std::uint32_t i = 0; i < boneCount; ++i) {
        mikudancestudio::mdl::BoneRecord* bone = &bones[i];
        format::BoneData disk;
        if (!format::ReadPmdBone(ReadModelFileBytes, &fh, disk)) {
            _close(fh);
            return false;
        }
        std::memcpy(bone->name, disk.pmdName, sizeof(bone->name));
        if (!std::memcmp(bone->name, kNameCenter2Jp, 9)) {
            model.centerBone = static_cast<std::int32_t>(i);
            centerSeen = 1;
        }
        if (!std::memcmp(bone->name, kNameCenterJp, 9) && !centerSeen)
            model.centerBone = static_cast<std::int32_t>(i);
        bone->parent = disk.parent;
        bone->tailBone = disk.tailBone;
        bone->type = static_cast<mdl::BoneType>(disk.type);
        bone->tailIdx = disk.tailIdx;
        std::memcpy(bone->position, disk.position, sizeof(bone->position));
        bone->rotQuat[3] = 1.0f; // scale x/y
        bone->rotQuat2[3] = 1.0f;
        for (int f = 0; f < 16; ++f) // +180 identity (x64 0x7FF7CB4D34B0)
            bone->matWorld[f] = (f % 5 == 0) ? 1.0f : 0.0f;
        bone->slotIndex = -1;
        bone->rigidIdx = -296;
        bone->flags |= mikudancestudio::mdl::kBoneFlagVisible;
    }

    // ---- IK chains --------------------------------------------------------
    if (!ReadModelFileCount(fh, 2, model.ikChainCount)) {
        _close(fh);
        return false;
    }
    const std::uint32_t ikCount = model.ikChainCount;
    if (ikCount > 0) {
        model.ikChains = static_cast<mdl::IkChain*>(operator new(sizeof(mdl::IkChain) * ikCount));
        std::memset(model.ikChains, 0, sizeof(mdl::IkChain) * ikCount);
        for (std::uint32_t i = 0; i < ikCount; ++i) {
            mdl::IkChain& ik = (m)->ikChains[i];
            format::PmdIkData disk;
            if (!format::ReadPmdIk(ReadModelFileBytes, &fh, disk)) {
                _close(fh);
                return false;
            }
            ik.boneIndex = disk.boneIndex;
            ik.targetBone = disk.targetBone;
            ik.linkCount = static_cast<std::uint8_t>(disk.links.size());
            ik.iterations = disk.iterations;
            ik.maxAngle = disk.maxAngle;
            ik.enabled = 1;
            const std::uint8_t len = ik.linkCount;
            ik.links = static_cast<std::uint16_t*>(operator new(sizeof(std::uint16_t) * len));
            for (std::uint8_t k = 0; k < len; ++k)
                ik.links[k] = disk.links[k];
        }
    }

    // physics-driven bone sweep (0x4C0258, C precedence kept verbatim:
    // (parent flagged) || ((RotateGrant || (inherit-flags && physMode==2))
    // && tail is Ik/UnderIk) - RotateGrant is NOT gated by physMode)
    for (std::uint32_t i = 0; i < boneCount; ++i) {
        mikudancestudio::mdl::BoneRecord* bone = &bones[i];
        bone->hasFlag = 0;
        const std::int32_t parent = bone->parent;
        const std::int32_t tail = bone->tailIdx;
        const mikudancestudio::mdl::BoneType tailType = bones[tail].type;
        const bool arm1 = parent >= 0 && model.boneTable[parent].hasFlag != 0;
        const bool arm2 = (bone->type == mikudancestudio::mdl::BoneType::RotateGrant ||
                           ((bone->flags & mikudancestudio::mdl::kBoneFlagInheritMask) != 0 &&
                            model.physicsMode == 2)) &&
                          (tailType == mikudancestudio::mdl::BoneType::Ik ||
                           tailType == mikudancestudio::mdl::BoneType::UnderIk);
        bone->hasFlag = (arm1 || arm2) ? 1 : 0;
    }
    InitBoneSortOrder(m); // x86 0x490070; bone_sort.cpp

    // ---- morphs -----------------------------------------------------------
    // (original 0x4C0330..0x4C0613: per-morph offset table of 16-byte
    //  entries is allocated at +92 and read from the file - four separate
    //  4-byte reads.  No group byte and no bone-style init; the earlier
    //  port invented both, writing past the 136-byte records and
    //  desynchronising the stream - heap corruption with real models,
    //  docs/ARCHITECTURE.md §8.)
    if (!ReadModelFileCount(fh, 2, model.morphCount)) {
        _close(fh);
        return false;
    }
    const std::uint32_t morphCount = model.morphCount;
    if (morphCount > 0) {
        mdl::MorphRecord* morphs =
            static_cast<mdl::MorphRecord*>(operator new(sizeof(mdl::MorphRecord) * morphCount));
        model.morphs = morphs;
        std::memset(morphs, 0, sizeof(mdl::MorphRecord) * morphCount);
        void* w1 = operator new(4 * morphCount);
        std::memset(w1, 0, 4 * morphCount);
        model.morphKeyCursors = static_cast<std::uint32_t*>(w1);
        void* w2 = operator new(morphCount);
        std::memset(w2, 0, morphCount);
        model.morphTrackActive = static_cast<unsigned char*>(w2);
        for (std::uint32_t i = 0; i < morphCount; ++i) {
            mdl::MorphRecord& morph = morphs[i];
            format::PmdMorphHeaderData header;
            if (!format::ReadPmdMorphHeader(ReadModelFileBytes, &fh, header)) {
                _close(fh);
                return false;
            }
            std::memcpy(morph.name, header.name, sizeof(morph.name));
            morph.offsetCount = header.offsetCount;
            morph.panel = static_cast<mdl::MorphPanel>(header.panel);
            const std::uint32_t n = morph.offsetCount;
            morph.vertexEntries = static_cast<mdl::PmdVertexMorphEntry*>(operator new(
                sizeof(mdl::PmdVertexMorphEntry) * n));
            std::memset(morph.vertexEntries, 0, sizeof(mdl::PmdVertexMorphEntry) * n);
            for (std::uint32_t k = 0; k < n; ++k) {
                format::PmdMorphOffsetData disk;
                if (!format::ReadPmdMorphOffset(ReadModelFileBytes, &fh, disk)) {
                    _close(fh);
                    return false;
                }
                morph.vertexEntries[k].vertexIndex = disk.vertexIndex;
                std::memcpy(morph.vertexEntries[k].offset, disk.offset, sizeof(disk.offset));
            }
        }
    }
    // first-morph table shared copy (this[2171] count / this[2181] table),
    // then remap every later morph's entry index through morph 0's table
    // (0x4C05EB: entry[0] = firstTable[16 * entry[0]]).
    {
        mdl::MorphRecord* morphs = model.morphs;
        if (morphs != nullptr) {
            const std::uint32_t n = morphs[0].offsetCount;
            model.morph0Count = n;
            model.morph0Table = static_cast<mdl::PmdVertexMorphEntry*>(operator new(
                sizeof(mdl::PmdVertexMorphEntry) * n));
            std::memset(model.morph0Table, 0, sizeof(mdl::PmdVertexMorphEntry) * n);
            std::memcpy(model.morph0Table, morphs[0].vertexEntries,
                        sizeof(mdl::PmdVertexMorphEntry) * n);
            for (std::uint32_t i = 1; i < morphCount; ++i) {
                mdl::MorphRecord& morph = morphs[i];
                for (std::uint32_t k = 0; k < static_cast<std::uint32_t>(morph.offsetCount); ++k) {
                    mdl::PmdVertexMorphEntry& entry = morph.vertexEntries[k];
                    entry.vertexIndex = model.morph0Table[entry.vertexIndex].vertexIndex;
                }
            }
        }
    }

    // ---- display frames / face groups -------------------------------------
    if (!ReadModelFileCount(fh, 1, model.facialFrameCount)) {
        _close(fh);
        return false;
    }
    if (model.facialFrameCount != 0) {
        model.displayFrames = static_cast<mdl::FrameGroup*>(operator new(
            sizeof(*model.displayFrames) * model.facialFrameCount));
        std::memset(model.displayFrames, 0, sizeof(*model.displayFrames) * model.facialFrameCount);
        for (std::uint8_t i = 0; i < model.facialFrameCount; ++i) {
            mdl::FrameGroup& frame = model.displayFrames[i];
            if (!format::ReadPmdDisplayTarget(ReadModelFileBytes, &fh, frame.targetIndex)) {
                _close(fh);
                return false;
            }
            frame.groupIndex = 1;
            strcpy_s(frame.name, sizeof(frame.name), model.morphs[frame.targetIndex].name);
        }
    }
    if (!ReadModelFileCount(fh, 1, model.groupCount)) {
        _close(fh);
        return false;
    }
    model.groupCount += 2;
    if (model.facialFrameCount != 0)
        ++model.groupCount;
    const std::uint8_t grpCount = model.groupCount;
    mdl::DisplayGroup*& groups = model.displayGroups;
    groups =
        static_cast<mdl::DisplayGroup*>(operator new(sizeof(*groups) * (grpCount ? grpCount : 1)));
    std::memset(groups, 0, sizeof(*groups) * (grpCount ? grpCount : 1));
    strcpy_s(groups[0].name, sizeof(groups[0].name), bones[0].name);
    strcpy_s(groups[1].name, sizeof(groups[1].name), kLblDispIkOpJp);
    strcpy_s(groups[0].nameEn, sizeof(groups[0].nameEn), bones[0].nameEn);
    strcpy_s(groups[1].nameEn, sizeof(groups[1].nameEn), "Disp/IK/OP");
    if (model.facialFrameCount != 0) {
        strcpy_s(groups[2].name, sizeof(groups[2].name), kLblFacialJp);
        strcpy_s(groups[2].nameEn, sizeof(groups[2].nameEn), "Facial");
        for (std::uint8_t i = 3; i < grpCount; ++i)
            if (!format::ReadPmdGroupName(ReadModelFileBytes, &fh, groups[i].name)) {
                _close(fh);
                return false;
            }
    } else {
        for (std::uint8_t i = 2; i < grpCount; ++i)
            if (!format::ReadPmdGroupName(ReadModelFileBytes, &fh, groups[i].name)) {
                _close(fh);
                return false;
            }
    }

    // ---- rigid-body group list --------------------------------------------
    if (!ReadModelFileCount(fh, 4, model.displayBoneCount)) {
        _close(fh);
        return false;
    }
    const std::uint32_t rbGroupCount = model.displayBoneCount;
    mdl::FrameGroup*& displayBones = (m)->displayBones;
    displayBones = static_cast<mdl::FrameGroup*>(operator new(sizeof(*displayBones) *
                                                              (rbGroupCount ? rbGroupCount : 1)));
    std::memset(displayBones, 0, sizeof(*displayBones) * (rbGroupCount ? rbGroupCount : 1));
    for (std::uint32_t i = 0; i < rbGroupCount; ++i) {
        mdl::FrameGroup& rb = displayBones[i];
        std::uint8_t diskGroup = 0;
        if (!format::ReadPmdBoneFrame(ReadModelFileBytes, &fh, rb.targetIndex, diskGroup)) {
            _close(fh);
            return false;
        }
        rb.groupIndex = diskGroup;
        ++rb.groupIndex;
        if (model.facialFrameCount != 0)
            ++rb.groupIndex;
        strcpy_s(rb.name, sizeof(rb.name), bones[rb.targetIndex].name);
    }

    // ---- English names section --------------------------------------------
    format::PmdEnglishPresence englishPresence;
    if (!format::ReadPmdEnglishPresence(ReadModelFileBytesResult, &fh, englishPresence)) {
        _close(fh);
        return false;
    }
    if (!englishPresence.hasEnglish) {
        if ((boxGate & model.physicsFlags) != 0)
            MessageBoxA(hwnd,
                        "For this model, there is no English data.\n"
                        "\n"
                        "The model name is expressed in Japanese.\n"
                        "If Japanese Font is not installed in your PC, "
                        "the name display will become abnormal.",
                        "warning!", 0);
        strcpy_s(model.nameEn, 0x32, model.name);
        strcpy_s(model.commentEn, sizeof(model.commentEn), model.comment);
        for (std::uint32_t i = 0; i < boneCount; ++i)
            strcpy_s(reinterpret_cast<char*>(&bones[i].nameEn[0]), 0x14,
                     reinterpret_cast<const char*>(&bones[i]));
        mdl::MorphRecord* morphs = model.morphs;
        for (std::uint32_t i = 0; i < morphCount; ++i)
            strcpy_s(morphs[i].nameEn, sizeof(morphs[i].nameEn), morphs[i].name);
        if (model.facialFrameCount != 0)
            for (std::uint8_t i = 3; i < grpCount; ++i)
                strcpy_s(groups[i].nameEn, sizeof(groups[i].nameEn), groups[i].name);
        else
            for (std::uint8_t i = 2; i < grpCount; ++i)
                strcpy_s(groups[i].nameEn, sizeof(groups[i].nameEn), groups[i].name);
    } else {
        format::PmdEnglishModelData english;
        if (!format::ReadPmdEnglishModel(ReadModelFileBytes, &fh, english)) {
            _close(fh);
            return false;
        }
        std::memcpy(model.nameEn, english.name, sizeof(english.name));
        model.nameEn[19] = 0;
        std::memcpy(model.commentEn, english.comment, sizeof(english.comment));
        model.commentEn[255] = 0;
        if (model.physicsFlags != 0 && boxGate &&
            MessageBoxA(hwnd, model.commentEn, "model infomation", 0x40001u) != IDOK) {
            _close(fh);
            return false;
        }
        for (std::uint32_t i = 0; i < boneCount; ++i) {
            if (!format::ReadPmdEnglishName(ReadModelFileBytes, &fh, bones[i].nameEn)) {
                _close(fh);
                return false;
            }
            bones[i].nameEn[19] = 0;
        }
        mdl::MorphRecord* morphs = model.morphs;
        for (std::uint32_t i = 1; i < morphCount; ++i) {
            if (!format::ReadPmdEnglishName(ReadModelFileBytes, &fh, morphs[i].nameEn)) {
                _close(fh);
                return false;
            }
            morphs[i].nameEn[sizeof(morphs[i].nameEn) - 1] = 0;
        }
        if (model.facialFrameCount != 0)
            for (std::uint8_t i = 3; i < grpCount; ++i) {
                if (!format::ReadPmdEnglishGroupName(ReadModelFileBytes, &fh, groups[i].nameEn)) {
                    _close(fh);
                    return false;
                }
                groups[i].nameEn[sizeof(groups[i].nameEn) - 1] = 0;
            }
        else
            for (std::uint8_t i = 2; i < grpCount; ++i) {
                if (!format::ReadPmdEnglishGroupName(ReadModelFileBytes, &fh, groups[i].nameEn)) {
                    _close(fh);
                    return false;
                }
                groups[i].nameEn[sizeof(groups[i].nameEn) - 1] = 0;
            }
    }
    for (std::uint8_t i = 0; i < model.facialFrameCount; ++i) {
        mdl::FrameGroup& frame = model.displayFrames[i];
        strcpy_s(frame.nameEn, sizeof(frame.nameEn), model.morphs[frame.targetIndex].nameEn);
    }
    for (std::uint32_t i = 0; i < rbGroupCount; ++i)
        strcpy_s(displayBones[i].nameEn, sizeof(displayBones[i].nameEn),
                 bones[displayBones[i].targetIndex].nameEn);

    // ---- toon references --------------------------------------------------
    if (englishPresence.hasToonFields) {
        auto* toon = model.pmdToonFileNames;
        for (int i = 0; i < 10; ++i) {
            if (!format::ReadPmdToonName(ReadModelFileBytes, &fh, toon[i])) {
                _close(fh);
                return false;
            }
            std::uint8_t matched = 0;
            for (int k = 0; k < 10; ++k) {
                sprintf_s(text, 0x100, "toon%02d.bmp", k + 1);
                if (!strcmp(toon[i], text))
                    matched = 1;
            }
            if (!matched && !cpuOnly) {
                ConvertAnsiToWide(sub, toon[i], wide1, 0x100);
                swprintf_s(wide2, 0x180, L"%s%s", model.modelDirectory, wide1);
                if (!LoadTextureShared(sub, wide2))
                    strcpy_s(toon[i], sizeof(toon[i]), text);
            }
        }
    } else {
        for (int i = 0; i < 10; ++i) {
            sprintf_s(text, 0x100, "toon%02d.bmp", i + 1);
            strcpy_s(model.pmdToonFileNames[i], sizeof(model.pmdToonFileNames[i]), text);
        }
    }

    // ---- rigid bodies + joints (physics) ----------------------------------
    // Preserve the original -1 gate; EOF (0 bytes) enters with zero bodies.
    const auto rigidRead =
        format::ReadPmdOptionalCount(ReadModelFileBytesResult, &fh, model.rigidCount);
    if (rigidRead == format::PmdOptionalRead::Truncated) {
        _close(fh);
        return false;
    }
    const std::uint32_t rigidCount = model.rigidCount;
    if (rigidRead != format::PmdOptionalRead::ReadError) {
        PhysicsScene* physScene = m->scenePtr;
        SetPhysicsMode(m, 0, nullptr, 0); // 0x4A9220
        SetPhysicsMode(m, 1, nullptr, 0);
        mdl::RigidRecord* rigids = static_cast<mdl::RigidRecord*>(operator new(
            sizeof(mdl::RigidRecord) * (rigidCount ? rigidCount : 1)));
        model.rigidTable = rigids;
        std::memset(rigids, 0, sizeof(mdl::RigidRecord) * (rigidCount ? rigidCount : 1));
        for (std::uint32_t i = 0; i < rigidCount; ++i) {
            mdl::RigidRecord& rigid = rigids[i];
            format::RigidData disk;
            if (!format::ReadPmdRigid(ReadModelFileBytes, &fh, disk)) {
                _close(fh);
                return false;
            }
            std::memcpy(rigid.name, disk.pmdName, sizeof(rigid.name));
            rigid.boneIndex = disk.boneIndex;
            rigid.group = disk.group;
            rigid.noCollapse = disk.noCollapse;
            rigid.shape = disk.shape;
            std::memcpy(rigid.size, disk.size, sizeof(rigid.size));
            std::memcpy(rigid.position, disk.position, sizeof(rigid.position));
            std::memcpy(rigid.rotation, disk.rotation, sizeof(rigid.rotation));
            rigid.mass = disk.mass;
            rigid.linearDamping = disk.linearDamping;
            rigid.angularDamping = disk.angularDamping;
            rigid.restitution = disk.restitution;
            rigid.friction = disk.friction;
            rigid.mode = disk.mode;
            rigid.kinematicFlag = rigid.mode == 2 ? 1 : 0;
            rigid.staticFlag = rigid.mode == 0 ? 1 : 0;

            {
                using d3dx::D3DXMATRIXF;
                D3DXMATRIXF mat, tmp;
                d3dx->rotZ(&mat, rigid.rotation[2]);
                d3dx->rotX(&tmp, rigid.rotation[0]);
                d3dx->multiply(&mat, &mat, &tmp);
                d3dx->rotY(&tmp, rigid.rotation[1]);
                d3dx->multiply(&mat, &mat, &tmp);
                d3dx->translation(&tmp, rigid.position[0], rigid.position[1], rigid.position[2]);
                d3dx->multiply(&mat, &mat, &tmp);
                const std::int32_t boneIdx = rigid.boneIndex;
                LogPmdRigid(path, i, boneIdx, boneCount, bones);
                const float* bp;
                if (boneIdx < 0)
                    bp = reinterpret_cast<const float*>(bones->position);
                else
                    bp = reinterpret_cast<const float*>(&bones[boneIdx].position[0]);
                d3dx->translation(&tmp, bp[0], bp[1], bp[2]);
                d3dx->multiply(&mat, &mat, &tmp);
                // create the Bullet body (shape + limits + world add)
                RigidBodyCreationResult bodyOut{};
                CreateRigidBody(physScene, bodyOut, rigid.shape, rigid.size[0], rigid.size[1],
                                rigid.size[2], reinterpret_cast<const float*>(&mat), rigid.mode,
                                rigid.mass, rigid.linearDamping, rigid.angularDamping,
                                rigid.restitution, rigid.friction, static_cast<char>(rigid.group),
                                rigid.noCollapse); // 0x4064F0
                rigid.bodyId = bodyOut.id;
                rigid.body = bodyOut.body;
                const std::int32_t bi = rigid.boneIndex;
                if (rigid.mode > 0 && bi >= 0)
                    bones[bi].hasRigidBody = 1;
                D3DXMATRIXF inv, t2;
                // The original indexes the x86 bone record as float[77..79]
                // (its `position` member).  That indexing crosses the two
                // widened text pointers on x64, so it reads the tail of
                // matExtra instead of the rest position.  The inverse rigid
                // transform is also the basis for every joint anchor and for
                // physics readback; use the ABI-independent member here.
                const mdl::BoneRecord& linkedBone = bones[boneIdx < 0 ? 0 : boneIdx];
                const float* bonePosition = linkedBone.position;
                d3dx->translation(&inv, -bonePosition[0], -bonePosition[1], -bonePosition[2]);
                d3dx->translation(&t2, -rigid.position[0], -rigid.position[1], -rigid.position[2]);
                d3dx->multiply(&inv, &inv, &t2);
                d3dx->rotY(&t2, -rigid.rotation[1]);
                d3dx->multiply(&inv, &inv, &t2);
                d3dx->rotX(&t2, -rigid.rotation[0]);
                d3dx->multiply(&inv, &inv, &t2);
                d3dx->rotZ(&t2, -rigid.rotation[2]);
                d3dx->multiply(&inv, &inv, &t2);
                std::memcpy(rigid.invTransform, &inv, sizeof(inv));
            }
        }
        const auto jointRead =
            format::ReadPmdOptionalCount(ReadModelFileBytesResult, &fh, model.jointCount);
        if (jointRead == format::PmdOptionalRead::Truncated) {
            _close(fh);
            return false;
        }
        const std::uint32_t jointCount = model.jointCount;
        mdl::JointRecord* joints = static_cast<mdl::JointRecord*>(operator new(
            sizeof(mdl::JointRecord) * (jointCount ? jointCount : 1)));
        model.jointTable = joints;
        std::memset(joints, 0, sizeof(mdl::JointRecord) * (jointCount ? jointCount : 1));
        for (std::uint32_t i = 0; i < jointCount; ++i) {
            mdl::JointRecord& joint = joints[i];
            format::JointData disk;
            if (!format::ReadPmdJoint(ReadModelFileBytes, &fh, disk)) {
                _close(fh);
                return false;
            }
            std::memcpy(joint.name, disk.pmdName, sizeof(joint.name));
            joint.rigidA = disk.rigidA;
            joint.rigidB = disk.rigidB;
            std::memcpy(joint.position, disk.position, sizeof(joint.position));
            std::memcpy(joint.rotation, disk.rotation, sizeof(joint.rotation));
            std::memcpy(joint.limits, disk.limits, sizeof(joint.limits));
            std::memcpy(joint.springs, disk.springs, sizeof(joint.springs));
            {
                using d3dx::D3DXMATRIXF;
                mdl::RigidRecord* rigids2 = model.rigidTable;
                const std::int32_t a = joint.rigidA;
                const std::int32_t b = joint.rigidB;
                mdl::RigidRecord& rigidA = rigids2[a];
                mdl::RigidRecord& rigidB = rigids2[b];
                D3DXMATRIXF ma, mb, rot;
                std::memcpy(&ma, rigidA.invTransform, sizeof(ma));
                std::memcpy(&mb, rigidB.invTransform, sizeof(mb));
                // pivot A
                const float* point = joint.position;
                float pivotA[3], pivotB[3];
                TransformJointPointOriginal(pivotA, point, &ma.m[0][0]);
                TransformJointPointOriginal(pivotB, point, &mb.m[0][0]);
                const float ax = pivotA[0], ay = pivotA[1], az = pivotA[2];
                const float bx = pivotB[0], by = pivotB[1], bz = pivotB[2];
                const float distA = std::sqrt(ax * ax + ay * ay + az * az);
                const float distB = std::sqrt(bx * bx + by * by + bz * bz);
                // Overstretch limit radius: limit norm plus the distances to both anchors
                // (x64 at 0x7FF7CB4D51B0..0x4D526D; see JointRadiusBound)
                joint.radiusBound = JointRadiusBound(joint.limits, distA, distB);
                d3dx->rotZ(&rot, joint.rotation[2]);
                D3DXMATRIXF t2;
                d3dx->rotX(&t2, joint.rotation[0]);
                d3dx->multiply(&rot, &rot, &t2);
                d3dx->rotY(&t2, joint.rotation[1]);
                d3dx->multiply(&rot, &rot, &t2);
                d3dx->rotY(&t2, -rigidA.rotation[1]);
                d3dx->multiply(&rot, &rot, &t2);
                d3dx->rotX(&t2, -rigidA.rotation[0]);
                d3dx->multiply(&rot, &rot, &t2);
                d3dx->rotZ(&t2, -rigidA.rotation[2]);
                d3dx->multiply(&rot, &rot, &t2);
                float qa[4];
                d3dx->quatFromMatrix(qa, &rot);
                d3dx->rotZ(&rot, joint.rotation[2]);
                d3dx->rotX(&t2, joint.rotation[0]);
                d3dx->multiply(&rot, &rot, &t2);
                d3dx->rotY(&t2, joint.rotation[1]);
                d3dx->multiply(&rot, &rot, &t2);
                d3dx->rotY(&t2, -rigidB.rotation[1]);
                d3dx->multiply(&rot, &rot, &t2);
                d3dx->rotX(&t2, -rigidB.rotation[0]);
                d3dx->multiply(&rot, &rot, &t2);
                d3dx->rotZ(&t2, -rigidB.rotation[2]);
                d3dx->multiply(&rot, &rot, &t2);
                float qb[4];
                d3dx->quatFromMatrix(qb, &rot);
                joint.constraint = CreatePhysJoint(
                    physScene, rigidA.body, rigidB.body, ax, ay, az, qa[0], qa[1], qa[2], qa[3], bx,
                    by, bz, qb[0], qb[1], qb[2], qb[3], joint.limits[0], joint.limits[1],
                    joint.limits[2], joint.limits[3], joint.limits[4], joint.limits[5],
                    joint.limits[6], joint.limits[7], joint.limits[8], joint.limits[9],
                    joint.limits[10], joint.limits[11], joint.springs[0], joint.springs[1],
                    joint.springs[2], joint.springs[3], joint.springs[4],
                    joint.springs[5]); // 0x406010
            }
        }
    }

    _close(fh);

    // ---- animation pools --------------------------------------------------
    {
        model.boneKeys =
            static_cast<mdl::BoneKey*>(operator new(sizeof(mdl::BoneKey) * mdl::kBoneKeyCapacity));
        std::memset(model.boneKeys, 0, sizeof(mdl::BoneKey) * mdl::kBoneKeyCapacity);
        model.morphKeys = static_cast<mdl::MorphKey*>(operator new(sizeof(mdl::MorphKey) *
                                                                   mdl::kMorphKeyCapacity));
        std::memset(model.morphKeys, 0, sizeof(mdl::MorphKey) * mdl::kMorphKeyCapacity);
        model.displayKeys = static_cast<mdl::DisplayKey*>(operator new(sizeof(mdl::DisplayKey) *
                                                                       mdl::kDisplayKeyCapacity));
        std::memset(model.displayKeys, 0, sizeof(mdl::DisplayKey) * mdl::kDisplayKeyCapacity);
        const std::uint32_t ikn = model.ikChainCount;
        const std::uint32_t ctr = model.boneOrderCount;
        for (std::size_t keyIndex = 0; keyIndex < mdl::kDisplayKeyCapacity; ++keyIndex) {
            mdl::DisplayKey& key = model.displayKeys[keyIndex];
            if (ikn > 0) {
                auto* flags = static_cast<std::uint8_t*>(operator new(ikn));
                mdl::IkStates(key) = flags;
                for (std::uint32_t k = 0; k < ikn; ++k)
                    flags[k] = 1;
            }
            key.visible = 1;
            if (static_cast<std::int32_t>(ctr) > 0) {
                auto* pairs = static_cast<mdl::BoneReference*>(operator new(
                    sizeof(mdl::BoneReference) * ctr));
                mdl::SelectorStates(key) = pairs;
                for (std::uint32_t k = 0; k < ctr; ++k) {
                    pairs[k].modelIndex = -1;
                    pairs[k].boneIndex = 0;
                }
            }
        }
        model.boneTimelineRows =
            static_cast<std::int32_t*>(operator new(sizeof(*model.boneTimelineRows) * boneCount));
        model.morphTimelineRows = static_cast<std::int32_t*>(operator new(
            sizeof(*model.morphTimelineRows) * model.morphCount));
        for (std::uint32_t i = 0; i < boneCount; ++i) {
            mdl::BoneKey& row = model.boneKeys[i];
            for (int k = 0; k < 4; ++k) {
                row.interpolation[k] = 20;
                row.interpolation[k + 4] = 20;
                row.interpolation[k + 8] = 107;
                row.interpolation[k + 12] = 107;
            }
            row.rotation[3] = 1.0f;
        }
    }
    if (!cpuOnly)
        InitializeModelCpuGeometry(g_Block, m);
    model.loadComplete = 1;
    if (!cpuOnly)
        PostLoadInit(m); // 0x49C850
    return true;
}

} // namespace mikudancestudio
