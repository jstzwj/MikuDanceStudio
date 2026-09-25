// ===========================================================================
// VA 0x004B77E0 - LoadPMX  (original: sub_4B77E0, 32KB)
// ===========================================================================
// The PMX 2.0 binary parser, __thiscall on the 0x4CCF4 model block;
// reached from ModelLoadPMD when the magic is "PMX".  Signature:
//
//   LoadPMX(model, renderSub, showInfoByte, a4, appPathBuf, fileHandle)
//
// Structure (annotated with the model offsets the original stores):
//   header   version==2.0, 8 index-size bytes -> m+8624..8631 in the
//            original's shuffled store order (enc, addlUV, vert, tex,
//            MATERIAL@8630, bone@8628, morph@8629, rigid@8631); UTF16
//            only (UTF8 is rejected); physicsMode is initialised to 2.
//   names    4 size-prefixed UTF16 buffers -> m+9388/9392/9396/9400 with
//            Shift-JIS mirrors at m+8776/8826/8876/9132 (0x407910).
//   vertices m+4 count; vertex buffer stride/FVF ladder by additional-UV
//            count (32/48/64/80/96 bytes, FVF 274/524818/2622226/
//            11011090/44565778); 188-byte records at m+9932 with SOA
//            additional UVs and BDEF1/2/4 + SDEF weights (SDEF rebases
//            R1/C around the weighted midpoint); shadow VB (FVF 66).
//   faces    m+20 count; index buffer 32/16-bit by m+8626; array m+24.
//   textures local path table, freed after materials are composed.
//   materials m+28 count, 2292-byte records at m+9924... (m+32), then a
//            per-vertex material assignment pass by smallest edge size.
//   bones    m+11652 count, 604-byte records at m+9916 (+ side arrays
//            m+36/m+40/m+11668/m+11672), flags word +500 gates
//            tail/inherit/axis/local/external/IK fields; IK converted to
//            the 24-byte chain records at m+9920 (count m+11656);
//            InitBoneSortOrder (0x490070) runs next.
//   morphs   m+11648 count, 136-byte records at m+9924, types 0..8
//            (group/vertex/bone/UV+4 extra/material); post-parse
//            bookkeeping arrays and counts m+8684..8704 / m+8724..8752.
//   mat pools three 128-byte-per-material pools m+8756/8760/8764.
//   frames   display frames -> facial group records m+9948 (m+11692
//            count), group table m+9936 (m+9940 count), center bone
//            m+14592, frame records m+9944 (m+11696 count).
//   physics  rigid bodies m+12752 -> 172-byte records m+12744 +
//            CreateRigidBody (0x4064F0) with scene *(m+60); joints
//            m+12756 -> 140-byte records m+12748 + CreatePhysJoint
//            (0x406010).  The original reads the joint limit floats in
//            the shuffled order +60/+64/+68, +96/+100/+104, +84/+88/+92,
//            +108..128 (m+72..80 stays zero) - ported verbatim.
//   tail     animation pools m+9952/9956/9960, 1000-frame registry
//            (28 bytes), interpolation defaults, m+11661 = 1,
//            PostLoadInit (0x49C850).
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

#include "mikudancestudio/d3dx_dyn.hpp"
#include "mikudancestudio/d3d_wrapper.hpp"
#include "mikudancestudio/model.hpp"
#include "mikudancestudio/ported_funcs.hpp"

namespace mikudancestudio {
namespace {

// Shared Shift-JIS byte literals, maintained in model_labels.inc.
// Preserve the original encoded bytes when editing; no generator is shipped.
#include "model_labels.inc"

using mdl::At;
using d3dx::D3DXMATRIXF;

// ---- JP header-error texts (byte-exact Shift-JIS as in the x64 .rdata) ----
// x64 0x7FF7CB551D38 / x86 0x53140C: "PMXファイルのバージョンは2.0しか読み
// 込めません" - the version-gate message (JP branch of "PMX files version is
// different from 2.0.").  The x64 lists the blob as "PMX"; the SJIS tail
// follows inline.
static const char kMsgPmxVersionJp[] =
    "PMX"
    "\x83\x74\x83\x40\x83\x43\x83\x8B"        // ファイル
    "\x82\xCC"                                // の
    "\x83\x6F\x81\x5B\x83\x57\x83\x87\x83\x93"  // バージョン
    "\x82\xCD"                                // は
    "2.0"
    "\x82\xB5\x82\xA9"                        // しか
    "\x93\xC7\x82\xDD\x8D\x9E\x82\xDF"        // 読み込め
    "\x82\xDC\x82\xB9\x82\xF1";               // ません
// x64 0x7FF7CB551DA8 / x86 0x531390: "MMDではエンコード方式がUTF16のPMXファ
// イルしか読み込めません" - the encoding-gate message (JP branch of
// "MMD can't read UTF8 encorded PMX.\nPlease exchange it to UTF16.").
static const char kMsgPmxUtf8Jp[] =
    "MMD"
    "\x82\xC5\x82\xCD"                        // では
    "\x83\x47\x83\x93\x83\x52\x81\x5B\x83\x68"  // エンコード
    "\x95\xFB\x8E\xAE"                        // 方式
    "\x82\xAA"                                // が
    "UTF16"
    "\x82\xCC"                                // の
    "PMX"
    "\x83\x74\x83\x40\x83\x43\x83\x8B"        // ファイル
    "\x82\xB5\x82\xA9"                        // しか
    "\x93\xC7\x82\xDD\x8D\x9E\x82\xDF"        // 読み込め
    "\x82\xDC\x82\xB9\x82\xF1";               // ません
// JP caption of both header errors: "ファイル読込" - same blob the PMD open
// path uses (x64 0x7FF7CB550880, x86 0x52DB80; = kTitleOpenFailJp).
// x64 0x7FF7CB551FF0: モデル"%s"の表情"%s"に不正なデータが含まれています
// \n強制的に変換します - the wrong-morph-data message (JP branch of
// "%s include wrong data in morph %s"; 表情 is MMD's word for a morph).
static const wchar_t kFmtPmxWrongMorphDataJp[] =
    L"\x30E2\x30C7\x30EB"                    // モデル
    L"\"%s\""
    L"\x306E"                                // の
    L"\x8868\x60C5"                          // 表情
    L"\"%s\""
    L"\x306B"                                // に
    L"\x4E0D\x6B63\x306A"                    // 不正な
    L"\x30C7\x30FC\x30BF"                    // データ
    L"\x304C"                                // が
    L"\x542B\x307E\x308C\x3066"              // 含まれて
    L"\x304A\x308A\x307E\x3059"              // おります
    L"\n"
    L"\x5F37\x5236\x7684\x306B"              // 強制的に
    L"\x5909\x63DB\x3057\x307E\x3059";       // 変換します

IDirect3DDevice9* DevOf(D3DRenderer* sub) {
    return sub->device;                       // wrapper + 120032
}

void InitializePmxMainVertices(const mdl::PmxVertex* source,
                               std::int32_t count,
                               mdl::SkinnedVertexBase* destination) {
    for (std::int32_t i = 0; i < count; ++i) {
        const mdl::PmxVertex& vertex = source[i];
        mdl::SkinnedVertexBase& output = destination[i];
        std::memcpy(output.position, vertex.position, sizeof(output.position));
        std::memcpy(output.normal, vertex.normal, sizeof(output.normal));
        std::memcpy(output.uv, vertex.uv, sizeof(output.uv));
    }
}

template <std::size_t AdditionalUvCount>
void InitializePmxMainVertices(const mdl::PmxVertex* source,
                               std::int32_t count,
                               mdl::SkinnedVertex<AdditionalUvCount>* destination) {
    for (std::int32_t i = 0; i < count; ++i) {
        const mdl::PmxVertex& vertex = source[i];
        mdl::SkinnedVertex<AdditionalUvCount>& output = destination[i];
        std::memcpy(output.base.position, vertex.position,
                    sizeof(output.base.position));
        std::memcpy(output.base.normal, vertex.normal,
                    sizeof(output.base.normal));
        std::memcpy(output.base.uv, vertex.uv, sizeof(output.base.uv));
        for (std::size_t uv = 0; uv < AdditionalUvCount; ++uv)
            for (std::size_t component = 0; component < 4; ++component)
                output.additionalUv[uv][component] =
                    vertex.additionalUvByComponent[component][uv];
    }
}

void InitializePmxEdgeVertices(const mdl::PmxVertex* source,
                               std::int32_t count,
                               mdl::EdgeVertex* destination) {
    for (std::int32_t i = 0; i < count; ++i) {
        const mdl::PmxVertex& vertex = source[i];
        mdl::EdgeVertex& output = destination[i];
        std::memcpy(output.position, vertex.position, sizeof(output.position));
        output.diffuse = 0xFF000000u;
    }
}

// Size-prefixed UTF16 buffer (common shape).  Empty -> "Null_%02d".
wchar_t* ReadTextBuf(int fh, int& nullIdx) {
    std::uint32_t len = 0;
    _read(fh, &len, 4);
    if (len != 0) {
        wchar_t* p = static_cast<wchar_t*>(operator new(2 * len));
        std::memset(p, 0, 2 * len);
        _read(fh, p, len);
        return p;
    }
    wchar_t* p = static_cast<wchar_t*>(operator new(0x28));
    swprintf_s(p, 0x14, L"Null_%02d", nullIdx++);
    return p;
}

// Size-prefixed UTF16 buffer, contents discarded (names not stored).
void SkipTextBuf(int fh) {
    std::uint32_t len = 0;
    _read(fh, &len, 4);
    if (len != 0) {
        void* p = operator new(2 * len);
        std::memset(p, 0, 2 * len);
        _read(fh, p, len);
        operator delete(p);
    }
}

// Porting-era trace under MIKUDANCESTUDIO_PMX_TRACE_DIR (CMake option
// MIKUDANCESTUDIO_DIAG, default OFF); the OFF stub keeps the call sites
// valid and inlines away to nothing.
#ifdef MIKUDANCESTUDIO_DIAG
void TracePmxOffset(int fh, const char* phase, int index, int value) {
    char directory[MAX_PATH];
    const DWORD length = GetEnvironmentVariableA(
        "MIKUDANCESTUDIO_PMX_TRACE_DIR", directory, MAX_PATH);
    if (length == 0 || length >= MAX_PATH)
        return;
    char path[MAX_PATH];
    sprintf_s(path, "%s\\pmx_load_trace.log", directory);
    FILE* stream = nullptr;
    if (fopen_s(&stream, path, "a") != 0 || stream == nullptr)
        return;
    fprintf(stream, "%s index=%d value=%d offset=%ld\n", phase, index,
            value, _tell(fh));
    fclose(stream);
}
#else
inline void TracePmxOffset(int, const char*, int, int) {}
#endif

}  // namespace

bool LoadPMX(unsigned char* m, D3DRenderer* sub, std::uint8_t showInfo,
             std::uint8_t englishUI, PathResolutionWorkspace& paths, int fh) {
    (void)englishUI;   // dispatch argument mirrors model.physicsFlags
    (void)paths;
    mdl::ModelRecord& model = *mdl::Mdl(m);
    const bool enData = model.physicsFlags != 0;
    HWND hwnd = static_cast<HWND>(model.hwnd);
    char text[256];
    int nullIdx = 0;

    auto& d = d3dx::Get();

    // ---- header -----------------------------------------------------------
    _read(fh, text, 1);                                     // 0x4B7806
    float version = 0.0f;
    _read(fh, &version, 4);
    if (version != 2.0f) {
        if (enData)
            sprintf_s(text, 0x100,
                      "PMX files version is different from 2.0.");
        else
            sprintf_s(text, 0x100, kMsgPmxVersionJp);
        MessageBoxA(hwnd, text,
                    enData ? "open file" : kTitleOpenFailJp, 0);
        _close(fh);
        return false;
    }
    model.physicsMode = 2;
    _read(fh, text, 1);
    _read(fh, &model.pmxTextEncoding, 1);
    _read(fh, &model.pmxAdditionalUvCount, 1);
    _read(fh, &model.pmxVertexIndexSize, 1);
    _read(fh, &model.pmxTextureIndexSize, 1);
    _read(fh, &model.pmxMaterialIndexSize, 1);
    _read(fh, &model.pmxBoneIndexSize, 1);
    _read(fh, &model.pmxMorphIndexSize, 1);
    _read(fh, &model.pmxRigidIndexSize, 1);
    if (model.pmxTextEncoding != 0) {
        sprintf_s(text, 0x100, enData
            ? "MMD can't read UTF8 encorded PMX.\nPlease exchange it to "
              "UTF16."
            : kMsgPmxUtf8Jp);
        MessageBoxA(hwnd, text,
                    enData ? "open file" : kTitleOpenFailJp, 0);
        _close(fh);
        return false;
    }

    // ---- model / comment text buffers --------------------------------------
    constexpr mdl::PmxTextBufferSlot kTextSlots[] = {
        mdl::PmxTextBufferSlot::japaneseName,
        mdl::PmxTextBufferSlot::englishName,
        mdl::PmxTextBufferSlot::japaneseComment,
        mdl::PmxTextBufferSlot::englishComment,
    };
    char* const sjisMirrors[] = {
        model.name, model.nameEn, model.comment, model.commentEn};
    const rsize_t kSjisSizes[4] = {
        sizeof model.name, sizeof model.nameEn,
        sizeof model.comment, sizeof model.commentEn};
    for (int i = 0; i < 4; ++i) {
        std::uint32_t len = 0;
        _read(fh, &len, 4);
        wchar_t* buf;
        if (len != 0) {
            buf = static_cast<wchar_t*>(operator new(2 * len));
            mdl::PmxTextBuffer(m, kTextSlots[i]) = buf;
            std::memset(buf, 0, 2 * len);
            _read(fh, buf, len);
        } else {
            buf = static_cast<wchar_t*>(operator new(0x28));
            mdl::PmxTextBuffer(m, kTextSlots[i]) = buf;
            if (i == 3)
                swprintf_s(buf, 0x14, L"NoInfo");
            else if (i == 2)  // empty JP comment slot (x64 0x7FF7CB4C9F5A)
                swprintf_s(buf, 0x14, L"\x60C5\x5831\x306A\x3057");  // 情報なし
            else
                swprintf_s(buf, 0x14, L"Null_%02d", nullIdx++);
        }
        WideToSjis(sub, sjisMirrors[i], buf, kSjisSizes[i]);
    }
    if (showInfo) {                                         // 0x4B7A56
        const int r = MessageBoxW(
            hwnd,
            mdl::PmxTextBuffer(
                m, enData ? mdl::PmxTextBufferSlot::englishComment
                          : mdl::PmxTextBufferSlot::japaneseComment),
            enData ? L"Model Infomation" : L"\x30E2\x30C7\x30EB\x60C5\x5831",
            0x40001u);
        if (r != 1) {
            _close(fh);
            return false;
        }
    }

    // ---- vertices -----------------------------------------------------------
    _read(fh, &model.vertexCount, sizeof(model.vertexCount));
    const std::int32_t vertCount = static_cast<std::int32_t>(model.vertexCount);
    if (vertCount != 0) {
        IDirect3DDevice9* dev = DevOf(sub);
        struct VbSpec { std::uint32_t stride, fvf; };
        static const VbSpec kSpec[5] = {
            {32, 274}, {48, 524818}, {64, 2622226}, {80, 11011090},
            {96, 44565778}};
        // additional-UV 数正常域 0..4；畸形文件的 5..7 回落 0 档（原版
        // default 分支同取 32/274，掩码本身是原版的 UB，不复制越界读）。
        const std::uint32_t uvLadder =
            model.pmxAdditionalUvCount < 5 ? model.pmxAdditionalUvCount : 0;
        const VbSpec& s = kSpec[uvLadder];
        IDirect3DVertexBuffer9* vb = nullptr;
        // pool = D3DPOOL_MANAGED: every addl-UV ladder branch writes the
        // 5th argument slot with 1 (x64 0x7FF7CB4CA1CF)
        if (FAILED(dev->CreateVertexBuffer(s.stride * vertCount, 8, s.fvf,
                                           D3DPOOL_MANAGED, &vb, nullptr))
            || vb == nullptr) {
            sprintf_s(text, 0x100, enData
                ? "The performance of the graphics card doesn't suffice."
                : kMsgNoPerfJp);
            MessageBoxA(hwnd, text,
                        enData ? "open file" : kTitleOpenFailJp, 0);
            _close(fh);
            return false;
        }
        mdl::ResourceAs<IDirect3DVertexBuffer9>(mdl::Mdl(m)->vertexBuffer) = vb;
        IDirect3DVertexBuffer9* vb2 = nullptr;
        // edge VB: pool = 1 too (x64 0x7FF7CB4CA272)
        if (FAILED(dev->CreateVertexBuffer(16 * vertCount, 8, 66,
                                           D3DPOOL_MANAGED, &vb2, nullptr))
            || vb2 == nullptr) {
            sprintf_s(text, 0x100, enData
                ? "The performance of the graphics card doesn't suffice."
                : kMsgNoPerfJp);
            MessageBoxA(hwnd, text,
                        enData ? "open file" : kTitleLoad2Jp, 0);
            _close(fh);
            return false;
        }
        mdl::ResourceAs<IDirect3DVertexBuffer9>(mdl::Mdl(m)->vertexBuffer2) = vb2;

        mdl::PmxVertex* verts = static_cast<mdl::PmxVertex*>(
            operator new(sizeof(*verts) * vertCount));
        model.pmxVertices = verts;
        // The reference clears only the leading position/normal/UV block;
        // later fields are initialized by their respective PMX readers.
        std::memset(verts, 0, 40 * vertCount);

        const std::uint8_t boneIdxSize = model.pmxBoneIndexSize;
        auto readIdx = [fh](int size) -> std::int32_t {
            switch (size) {
            case 1: { std::int8_t v = 0; _read(fh, &v, 1); return v; }
            case 2: { std::uint16_t v = 0; _read(fh, &v, 2);
                      return static_cast<std::int16_t>(v); }
            default: { std::int32_t v = 0; _read(fh, &v, 4); return v; }
            }
        };

        for (std::int32_t i = 0; i < vertCount; ++i) {
            mdl::PmxVertex& rec = verts[i];
            _read(fh, &rec, 0x20);                          // pos+normal+uv
            const int addl = model.pmxAdditionalUvCount;
            for (int j = 0; j < addl; ++j)                  // SOA addl UVs
                for (int c = 0; c < 4; ++c)
                    _read(fh, &rec.additionalUvByComponent[c][j], 4);
            _read(fh, &rec.weightType, 1);
            switch (rec.weightType) {
            case mdl::PmxWeightType::bdef2:
                rec.bone[0] = readIdx(boneIdxSize);
                rec.bone[1] = readIdx(boneIdxSize);
                _read(fh, rec.weight, 4);
                break;
            case mdl::PmxWeightType::bdef4:
                for (int b = 0; b < 4; ++b)
                    rec.bone[b] = readIdx(boneIdxSize);
                _read(fh, rec.weight, 0x10);
                break;
            case mdl::PmxWeightType::sdef: {
                rec.bone[0] = readIdx(boneIdxSize);
                rec.bone[1] = readIdx(boneIdxSize);
                _read(fh, rec.weight, 4);                   // weight
                _read(fh, rec.sdef.center, 0xC);            // PMX C
                _read(fh, rec.sdef.r0Offset, 0xC);          // PMX R0
                _read(fh, rec.sdef.r1Offset, 0xC);          // PMX R1
                const float w = rec.weight[0];
                float mid[3];
                for (int c = 0; c < 3; ++c)
                    mid[c] = w * rec.sdef.r0Offset[c]
                           + (1.0f - w) * rec.sdef.r1Offset[c];
                for (int c = 0; c < 3; ++c) {
                    rec.sdef.r0Offset[c] -= mid[c];
                    rec.sdef.r1Offset[c] -= mid[c];
                }
                break;
            }
            case mdl::PmxWeightType::bdef1:
                rec.bone[0] = readIdx(boneIdxSize);
                break;
            default:
                // Illegal weight type: the reference has no reader here.
                // x86 0x4B80A3/0x4B813E/0x4B833B all jump straight to the
                // 0x4B85AB edge-scale read, consuming no bytes (x64 mirrors:
                // 0x7FF7CB4CA536/CA5D5/CA7D4 -> 0x7FF7CB4CAA2B); the weight
                // fields keep their zeroed state.
                break;
            }
            _read(fh, &rec.edgeScale, 4);
            rec.hasPmxEdgeData = 1;
        }

        // Main VB fill: the loader stores additional UVs component-first,
        // whereas the GPU records keep each float4 contiguous.
        switch (model.pmxAdditionalUvCount) {
        case 0: {
            mdl::SkinnedVertexBase* output = nullptr;
            if (SUCCEEDED(vb->Lock(0, 0,
                                   reinterpret_cast<void**>(&output), 0))) {
                InitializePmxMainVertices(verts, vertCount, output);
                vb->Unlock();
            }
            break;
        }
        case 1: {
            mdl::SkinnedVertex<1>* output = nullptr;
            if (SUCCEEDED(vb->Lock(0, 0,
                                   reinterpret_cast<void**>(&output), 0))) {
                InitializePmxMainVertices(verts, vertCount, output);
                vb->Unlock();
            }
            break;
        }
        case 2: {
            mdl::SkinnedVertex<2>* output = nullptr;
            if (SUCCEEDED(vb->Lock(0, 0,
                                   reinterpret_cast<void**>(&output), 0))) {
                InitializePmxMainVertices(verts, vertCount, output);
                vb->Unlock();
            }
            break;
        }
        case 3: {
            mdl::SkinnedVertex<3>* output = nullptr;
            if (SUCCEEDED(vb->Lock(0, 0,
                                   reinterpret_cast<void**>(&output), 0))) {
                InitializePmxMainVertices(verts, vertCount, output);
                vb->Unlock();
            }
            break;
        }
        case 4: {
            mdl::SkinnedVertex<4>* output = nullptr;
            if (SUCCEEDED(vb->Lock(0, 0,
                                   reinterpret_cast<void**>(&output), 0))) {
                InitializePmxMainVertices(verts, vertCount, output);
                vb->Unlock();
            }
            break;
        }
        default:
            break;  // PMX 2.0 header only permits zero through four sets.
        }

        // Edge VB fill (position + opaque black diffuse).
        mdl::EdgeVertex* edgeVertices = nullptr;
        if (SUCCEEDED(vb2->Lock(0, 0,
                                reinterpret_cast<void**>(&edgeVertices), 0))) {
            InitializePmxEdgeVertices(verts, vertCount, edgeVertices);
            vb2->Unlock();
        }
    }

    // ---- faces ---------------------------------------------------------------
    _read(fh, &model.indexCount, sizeof(model.indexCount));
    const std::int32_t faceCount = static_cast<std::int32_t>(model.indexCount);
    if (faceCount != 0) {
        IDirect3DDevice9* dev = DevOf(sub);
        const bool wideIdx = model.pmxVertexIndexSize >= 4;
        IDirect3DIndexBuffer9* ib = nullptr;
        if (FAILED(dev->CreateIndexBuffer(
                (wideIdx ? 4 : 2) * faceCount, 0,
                wideIdx ? D3DFMT_INDEX32 : D3DFMT_INDEX16,
                // x64 LoadPMX 0x7FF7CB4CB3D6: Pool immediate is 1
                // (D3DPOOL_MANAGED), not SYSTEMMEM.
                D3DPOOL_MANAGED, &ib, nullptr))) {
            sprintf_s(text, 0x100, enData
                ? "The performance of the graphics card doesn't suffice."
                : kMsgNoPerfJp);
            MessageBoxA(hwnd, text, "CreateIndexBuffer", 0);
            _close(fh);
            return false;
        }
        mdl::ResourceAs<IDirect3DIndexBuffer9>(mdl::Mdl(m)->indexBuffer) = ib;
        auto* indices = static_cast<std::int32_t*>(
            operator new(sizeof(std::int32_t) * faceCount));
        mdl::PmxIndices(m) = indices;
        std::memset(indices, 0, sizeof(std::int32_t) * faceCount);
        const auto readVertexIndex = [&]() -> std::int32_t {
            const int idxSize = model.pmxVertexIndexSize;
            std::int32_t value = 0;
            switch (idxSize) {
            case 1: { std::uint8_t input; _read(fh, &input, 1); return input; }
            case 2: { std::uint16_t input; _read(fh, &input, 2); return input; }
            default: _read(fh, &value, 4); return value;
            }
        };
        const auto showIndexLockFailure = [&]() {
            sprintf_s(text, 0x100, enData
                ? "The performance of the graphics card doesn't suffice."
                : kMsgNoPerfJp);
            MessageBoxA(hwnd, text, "pIndBuf->Lock", 0);
            _close(fh);
        };
        if (wideIdx) {
            std::uint32_t* gpuIndices = nullptr;
            if (SUCCEEDED(ib->Lock(0, 0,
                                   reinterpret_cast<void**>(&gpuIndices), 0))) {
                for (std::int32_t i = 0; i < faceCount; ++i) {
                    const std::int32_t value = readVertexIndex();
                    indices[i] = value;
                    gpuIndices[i] = static_cast<std::uint32_t>(value);
                }
                ib->Unlock();
            } else {
                showIndexLockFailure();
                return false;
            }
        } else {
            std::uint16_t* gpuIndices = nullptr;
            if (SUCCEEDED(ib->Lock(0, 0,
                                   reinterpret_cast<void**>(&gpuIndices), 0))) {
                for (std::int32_t i = 0; i < faceCount; ++i) {
                    const std::int32_t value = readVertexIndex();
                    indices[i] = value;
                    gpuIndices[i] = static_cast<std::uint16_t>(value);
                }
                ib->Unlock();
            } else {
                showIndexLockFailure();
                return false;
            }
        }
    }

    // ---- textures (local path table) ----------------------------------------
    std::uint32_t texCount = 0;
    _read(fh, &texCount, 4);
    wchar_t** texPaths = static_cast<wchar_t**>(
        texCount ? operator new(sizeof(*texPaths) * texCount) : nullptr);
    for (std::uint32_t t = 0; t < texCount; ++t) {
        texPaths[t] = nullptr;
        std::uint32_t len = 0;
        _read(fh, &len, 4);
        if (static_cast<std::int32_t>(len) > 0) {
            texPaths[t] = static_cast<wchar_t*>(operator new(2 * len));
            std::memset(texPaths[t], 0, 2 * len);
            _read(fh, texPaths[t], len);
        }
    }
    static const char* kToonNames[10] = {
        "toon01.bmp", "toon02.bmp", "toon03.bmp", "toon04.bmp", "toon05.bmp",
        "toon06.bmp", "toon07.bmp", "toon08.bmp", "toon09.bmp", "toon10.bmp"};
    for (int i = 0; i < 10; ++i)
        strcpy_s(model.pmdToonFileNames[i],
                 sizeof(model.pmdToonFileNames[i]), kToonNames[i]);

    // ---- materials -------------------------------------------------------------
    _read(fh, &model.materialCount, sizeof(model.materialCount));
    const std::int32_t matCount = static_cast<std::int32_t>(model.materialCount);
    mdl::ModelMaterialRecord*& materials = model.materials;
    if (matCount != 0) {
        materials = static_cast<mdl::ModelMaterialRecord*>(
            operator new(sizeof(*materials) * matCount));
        std::memset(materials, 0, sizeof(*materials) * matCount);
        const int texIdxSize = model.pmxTextureIndexSize;
        auto readTexIdx = [fh, texIdxSize]() -> std::int32_t {
            switch (texIdxSize) {
            case 1: { std::int8_t v; _read(fh, &v, 1); return v; }
            case 2: { std::uint16_t v; _read(fh, &v, 2);
                      return static_cast<std::int16_t>(v); }
            default: { std::int32_t v; _read(fh, &v, 4); return v; }
            }
        };
        wchar_t emptyPath[2] = L"";
        unsigned char* subBytes = reinterpret_cast<unsigned char*>(sub);
        for (std::int32_t i = 0; i < matCount; ++i) {
            mdl::ModelMaterialRecord& mat = materials[i];
            SkipTextBuf(fh);                                // JP name
            SkipTextBuf(fh);                                // EN name
            _read(fh, mat.diffuse, sizeof(mat.diffuse));
            std::memcpy(mat.diffuseMirror, mat.diffuse,
                        sizeof(mat.diffuseMirror));
            _read(fh, mat.specular, sizeof(mat.specular));
            _read(fh, &mat.specularPower, sizeof(mat.specularPower));
            _read(fh, mat.ambient, sizeof(mat.ambient));
            _read(fh, &mat.flags, 1);
            _read(fh, mat.edgeColor, sizeof(mat.edgeColor));
            _read(fh, &mat.edgeSize, sizeof(mat.edgeSize));
            mat.doubleSided = (mat.flags & 0x10) != 0;
            // texture path
            {
                const std::int32_t idx = readTexIdx();
                if (idx < 0 || idx >= static_cast<std::int32_t>(texCount)) {
                    wcscpy_s(mat.texturePath, 0x118, emptyPath);
                } else {
                    swprintf_s(mat.texturePath, 0x118,
                               L"%s%s",
                               model.modelDirectory,
                               texPaths[idx]);
                    if (!LoadTextureShared(
                            subBytes,
                            mat.texturePath))
                        wcscpy_s(mat.texturePath, 0x118, emptyPath);
                }
            }
            // sphere path
            {
                const std::int32_t idx = readTexIdx();
                if (idx < 0 || idx >= static_cast<std::int32_t>(texCount)
                    || (swprintf_s(mat.spherePath,
                                   0x118, L"%s%s",
                                   model.modelDirectory,
                                   texPaths[idx]),
                        !LoadTextureShared(
                            subBytes,
                            mat.spherePath))) {
                    wcscpy_s(mat.spherePath, 0x118, emptyPath);
                }
            }
            _read(fh, &mat.sphereMode, 1);
            std::uint8_t sharedToon = 0;
            _read(fh, &sharedToon, 1);
            if (!sharedToon) {                              // own toon file
                mat.toonReference = 0xFE;
                const std::int32_t idx = readTexIdx();
                if (idx >= 0) {
                    swprintf_s(mat.toonPath, 0x200, L"%s%s",
                               model.modelDirectory,
                               texPaths[idx]);
                    if (!LoadTextureShared(
                            subBytes,
                            mat.toonPath)) {
                        wcscpy_s(mat.toonPath, 0x200, emptyPath);
                        mat.toonReference = 0xFF;
                    }
                } else {
                    mat.toonReference = 0xFF;
                }
            } else {
                _read(fh, &mat.toonReference, 1);
            }
            SkipTextBuf(fh);                                // memo
            _read(fh, &mat.faceVertexCount, sizeof(mat.faceVertexCount));
        }
    }
    // per-vertex material assignment by smallest edge size.  Runs whenever
    // the model has vertices, even with matCount == 0: vertMat stays zeroed
    // and every vertex gets materialIndex 0 (x64 0x7FF7CB4CBFCC gates this
    // whole block only on vertexCount; matCount gates the loop below).
    if (vertCount > 0) {
        float* edgeKey = static_cast<float*>(
            operator new(sizeof(*edgeKey) * vertCount));
        std::int32_t* vertMat = static_cast<std::int32_t*>(
            operator new(sizeof(*vertMat) * vertCount));
        std::memset(vertMat, 0, sizeof(*vertMat) * vertCount);
        for (std::int32_t v = 0; v < vertCount; ++v)
            edgeKey[v] = 999.79999f;
        std::int32_t faceCursor = 0;
        for (std::int32_t mi = 0; mi < matCount; ++mi) {
            const mdl::ModelMaterialRecord& mat = materials[mi];
            const std::int32_t faces = mat.faceVertexCount;
            for (std::int32_t f = 0; f < faces; ++f) {
                const std::int32_t vi = mdl::PmxIndices(m)[faceCursor++];
                if (mat.edgeSize < edgeKey[vi]) {
                    edgeKey[vi] = mat.edgeSize;
                    vertMat[vi] = mi;
                }
            }
        }
        for (std::int32_t v = 0; v < vertCount; ++v)
            model.pmxVertices[v].materialIndex = vertMat[v];
        operator delete(edgeKey);
        operator delete(vertMat);
    }
    for (std::uint32_t t = 0; t < texCount; ++t) {
        if (texPaths[t]) {
            operator delete(texPaths[t]);
            texPaths[t] = nullptr;
        }
    }
    if (texPaths)
        operator delete(texPaths);

    TracePmxOffset(fh, "materials-end", matCount,
                   static_cast<int>(texCount));

    // ---- bones ---------------------------------------------------------------
    _read(fh, &model.boneCount, sizeof(model.boneCount));
    const std::int32_t boneCount = static_cast<std::int32_t>(model.boneCount);
    TracePmxOffset(fh, "bones-begin", boneCount, 0);
    const std::uint8_t boneIdxSize = model.pmxBoneIndexSize;
    auto readBoneIdx = [fh, boneIdxSize]() -> std::int32_t {
        switch (boneIdxSize) {
        case 1: { std::int8_t v; _read(fh, &v, 1); return v; }
        case 2: { std::uint16_t v; _read(fh, &v, 2);
                  return static_cast<std::int16_t>(v); }
        default: { std::int32_t v; _read(fh, &v, 4); return v; }
        }
    };
    std::int32_t ikBoneCount = 0;
    std::int32_t centerBone = -1;
    bool centerFound = false;
    if (boneCount > 0) {
        mikudancestudio::mdl::Bones(m) = static_cast<mikudancestudio::mdl::BoneRecord*>(
            operator new(sizeof(mikudancestudio::mdl::BoneRecord) * boneCount));
        std::memset(mikudancestudio::mdl::Bones(m), 0, sizeof(mikudancestudio::mdl::BoneRecord) * boneCount);
        mdl::Mdl(m)->boneKeyCursors = static_cast<std::uint32_t*>(
            operator new(sizeof(std::uint32_t) * boneCount));
        std::memset(mdl::Mdl(m)->boneKeyCursors, 0,
                    sizeof(std::uint32_t) * boneCount);
        mdl::Mdl(m)->boneTrackActive = static_cast<unsigned char*>(
            operator new(boneCount));
        std::memset(mdl::Mdl(m)->boneTrackActive, 0, boneCount);
        mdl::Mdl(m)->boneSelection = static_cast<unsigned char*>(
            operator new(boneCount));
        std::memset(mdl::Mdl(m)->boneSelection, 0, boneCount);
        mdl::Mdl(m)->boneSelection[0] = 1;
        mdl::Mdl(m)->bonePhysicsState = static_cast<unsigned char*>(
            operator new(boneCount));
        std::memset(mdl::Mdl(m)->bonePhysicsState, 0, boneCount);

        mikudancestudio::mdl::BoneRecord* bones = mikudancestudio::mdl::Bones(m);
        for (std::int32_t i = 0; i < boneCount; ++i) {
            mikudancestudio::mdl::BoneRecord* bone = &bones[i];
            bone->jpText = ReadTextBuf(fh, nullIdx); // JP name
            WideToSjis(sub, reinterpret_cast<char*>(bone),
                       reinterpret_cast<const wchar_t*>(
                           bone->jpText), 0x14);
            // center bone detection (SJIS memcmp, 9 bytes with NUL; x64
            // 0x7FF7CB4CC36D: 操作中心 first, センター as pre-flag
            // fallback - same pair/order as the PMD path)
            if (!std::memcmp(bone->name, kNameCenter2Jp, 9)) {
                model.centerBone = i;
                centerFound = true;
            } else if (!centerFound
                       && !std::memcmp(bone->name, kNameCenterJp, 9)) {
                model.centerBone = i;
            }
            bone->enText = ReadTextBuf(fh, nullIdx); // EN name
            WideToSjis(sub, reinterpret_cast<char*>(bone->nameEn),
                       reinterpret_cast<const wchar_t*>(
                           bone->enText), 0x14);
            _read(fh, bone->position, 4);                       // position
            _read(fh, &bone->position[1], 4);
            _read(fh, &bone->position[2], 4);
            bone->parent = readBoneIdx();     // parent
            _read(fh, &bone->layer, 4);                       // layer
            if (mdl::Mdl(m)->maxBoneLayer < bone->layer)
                mdl::Mdl(m)->maxBoneLayer = bone->layer;
            _read(fh, &bone->flags, 2);                       // flags
            if (bone->flags & mdl::kBoneFlagTailIsBone) {
                bone->tailBone = readBoneIdx();
            } else {                                        // tail offset
                _read(fh, bone->tailOffset, 4);
                _read(fh, &bone->tailOffset[1], 4);
                _read(fh, &bone->tailOffset[2], 4);
                bone->tailOffset[0] += bone->position[0];
                bone->tailOffset[1] += bone->position[1];
                bone->tailOffset[2] += bone->position[2];
            }
            if (bone->flags & mdl::kBoneFlagMovable)
                bone->type = mdl::BoneType::Move;             // 0x4BA033
            if (bone->flags & mdl::kBoneFlagInheritMask) {    // inheritance
                bone->tailIdx = readBoneIdx();
                _read(fh, &bone->inheritRatio, 4);
            }
            if (bone->flags & mdl::kBoneFlagFixedAxis) {
                bone->type = mdl::BoneType::FixedAxis;        // 0x4BA127
                _read(fh, bone->axis, 4);
                _read(fh, &bone->axis[1], 4);
                _read(fh, &bone->axis[2], 4);
                float axis[3] = {bone->axis[0], bone->axis[1],
                                 bone->axis[2]};
                d.vec3Normalize(axis, axis);
                bone->axis[0] = axis[0];
                bone->axis[1] = axis[1];
                bone->axis[2] = axis[2];
            }
            if (bone->flags & mdl::kBoneFlagLocalAxes) {
                for (int f = 0; f < 6; ++f)
                    _read(fh, bone->localAxes + f, 4);
            }
            if (bone->flags & mdl::kBoneFlagExternalParent)
                _read(fh, &bone->extParent, 4);
            if (bone->flags & mdl::kBoneFlagIk) {             // IK
                ++ikBoneCount;
                bone->ikTarget = readBoneIdx();  // target
                _read(fh, &bone->ikLoop, 4);                   // loop count
                _read(fh, &bone->ikAngle, 4);                   // angle
                _read(fh, &bone->ikLinkCount, 4);                   // link count
                if (bone->ikLinkCount > 0) {
                    bone->ikLinks = static_cast<mdl::PmxIkLinkRecord*>(
                        operator new(sizeof(*bone->ikLinks) * bone->ikLinkCount));
                    std::memset(bone->ikLinks, 0,
                                sizeof(*bone->ikLinks) * bone->ikLinkCount);
                    for (int l = 0; l < bone->ikLinkCount; ++l) {
                        mdl::PmxIkLinkRecord& link = bone->ikLinks[l];
                        link.boneIndex = readBoneIdx();
                        _read(fh, &link.hasLimits, 1);
                        if (link.hasLimits == 1) {
                            _read(fh, link.minimum, sizeof(link.minimum));
                            _read(fh, link.maximum, sizeof(link.maximum));
                        }
                    }
                }
            }
            if (!(bone->flags & mdl::kBoneFlagVisible))
                bone->type = mdl::BoneType::InertTip;        // 0x4BA5DF
            bone->rotQuat[3] = 1.0f;
            bone->rotQuat2[3] = 1.0f;
            bone->matWorld[14] = 0.0f;
            for (int f = 0; f < 16; ++f)                    // +180 identity
                bone->matWorld[f] =
                    (f % 5 == 0) ? 1.0f : 0.0f;
            bone->slotIndex = -1;
            bone->rigidIdx = -296;
            TracePmxOffset(fh, "bone-end", i,
                           bone->flags);
        }

        // IK -> 24-byte chain records (m+9920, count m+11656) ---------------
        model.ikChainCount = ikBoneCount;
        if (ikBoneCount > 0) {
            mdl::Mdl(m)->ikChains = static_cast<mdl::IkChain*>(operator new(
                sizeof(mdl::IkChain) * ikBoneCount));
            std::memset(mdl::Mdl(m)->ikChains, 0,
                        sizeof(mdl::IkChain) * ikBoneCount);
            int chain = 0;
            for (std::int32_t i = 0; i < boneCount; ++i) {
                mikudancestudio::mdl::BoneRecord* bone = &bones[i];
                if (!(bone->flags & mdl::kBoneFlagIk))
                    continue;
                bone->type = mdl::BoneType::Ik;               // 0x4BA740
                bones[bone->ikTarget].type = mdl::BoneType::Effector;
                mdl::IkChain& ch = mdl::IkChains(m)[chain];
                ch.boneIndex = i;
                ch.targetBone = bone->ikTarget;
                ch.linkCount = static_cast<std::uint8_t>(bone->ikLinkCount);
                ch.iterations = static_cast<std::uint16_t>(bone->ikLoop);
                ch.maxAngle = bone->ikAngle * 0.25f;
                if (ch.maxAngle != 0.0f) {
                    float lim = 3.141592025756836f / ch.maxAngle;
                    if (lim > 360.0f)
                        lim = 360.0f;
                    if (ch.iterations < static_cast<int>(lim))
                        ch.iterations =
                            static_cast<std::uint16_t>(static_cast<int>(lim));
                }
                ch.enabled = 1;
                const int linkCount = ch.linkCount;
                if (linkCount != 0) {
                    ch.links = static_cast<std::uint16_t*>(
                        operator new(sizeof(std::uint16_t) * linkCount));
                    for (int l = 0; l < linkCount; ++l) {
                        const mdl::PmxIkLinkRecord& link = bone->ikLinks[l];
                        bones[link.boneIndex].type =
                            mdl::BoneType::UnderIk;           // IK link
                        ch.links[l] =
                            static_cast<std::uint16_t>(link.boneIndex);
                        if (link.hasLimits == 1) {
                            mikudancestudio::mdl::BoneRecord* lb = &bones[link.boneIndex];
                            lb->twistEnable = 1;
                            std::memcpy(lb->ikLimitMin, link.minimum,
                                        sizeof(lb->ikLimitMin));
                            std::memcpy(lb->ikLimitMax, link.maximum,
                                        sizeof(lb->ikLimitMax));
                        }
                    }
                }
                ++chain;
            }
        }

        // transform-layer inheritance pass (&bone->hasFlag flag, +496 layers) ------
        for (std::int32_t i = 0; i < boneCount; ++i) {
            mikudancestudio::mdl::BoneRecord* bone = &bones[i];
            bone->hasFlag = 0;
            const std::int32_t parent = bone->parent;
            if (parent >= 0 && bones[parent].hasFlag) {
                bone->hasFlag = 1;
            } else if ((bone->type == mdl::BoneType::RotateGrant
                        || ((bone->flags & mdl::kBoneFlagInheritMask) != 0
                            && model.physicsMode == 2))
                       && bones[bone->tailIdx].type
                              == mdl::BoneType::UnderIk) {
                bone->hasFlag = 1;
            } else if (parent >= 0) {
                const std::int32_t pl = bones[parent].layer;
                if (bone->layer < pl)
                    bone->layer = pl;
                const mdl::BoneType pt = bones[parent].type;
                const bool parentIk = (pt == mdl::BoneType::UnderIk ||
                                       pt == mdl::BoneType::Effector);
                if (bone->type != mdl::BoneType::UnderIk &&
                    bone->type != mdl::BoneType::Effector
                    && model.physicsMode == 2 && parentIk) {
                    ++bone->layer;
                    if (mdl::Mdl(m)->maxBoneLayer < bone->layer)
                        mdl::Mdl(m)->maxBoneLayer = bone->layer;
                }
            }
        }
    }
    InitBoneSortOrder(m);                                   // 0x490070

    // ---- morphs -----------------------------------------------------------
    std::uint8_t* vmap = static_cast<std::uint8_t*>(vertCount
                                                        ? operator new(
                                                              vertCount)
                                                        : nullptr);
    std::memset(vmap, 0, vertCount);
    _read(fh, &model.morphCount, sizeof(model.morphCount));
    const std::int32_t morphCount = static_cast<std::int32_t>(model.morphCount);
    TracePmxOffset(fh, "morphs-begin", morphCount, 0);
    std::int32_t boneMorphTotal = 0, uvMorphTotal = 0, uv2Total = 0,
                 uv3Total = 0, uv4Total = 0, uv5Total = 0;
    const std::uint8_t vertIdxSize = model.pmxVertexIndexSize;
    const std::uint8_t morphIdxSize = model.pmxMorphIndexSize;
    const std::uint8_t matIdxSize = model.pmxMaterialIndexSize;
    auto readIdxS = [fh](int size) -> std::int32_t {
        switch (size) {
        case 1: { std::int8_t v; _read(fh, &v, 1); return v; }
        case 2: { std::uint16_t v; _read(fh, &v, 2);
                  return static_cast<std::int16_t>(v); }
        default: { std::int32_t v; _read(fh, &v, 4); return v; }
        }
    };
    auto readVertexIdx = [fh](int size) -> std::int32_t {
        switch (size) {
        case 1: { std::uint8_t value; _read(fh, &value, 1); return value; }
        case 2: { std::uint16_t value; _read(fh, &value, 2); return value; }
        default: { std::int32_t value; _read(fh, &value, 4); return value; }
        }
    };
    if (morphCount > 0) {
        mdl::Morphs(m) = static_cast<mdl::MorphRecord*>(
            operator new(sizeof(mdl::MorphRecord) * morphCount));
        std::memset(mdl::Morphs(m), 0,
                    sizeof(mdl::MorphRecord) * morphCount);
        // track cursors/flags beside the table (x64 0x7FF7CB4CD40D/CD42A
        // -> m+88/96) - ModelKeyframeAdvance writes morphTrackActive[morph]
        void* w1 = operator new(4 * morphCount);
        std::memset(w1, 0, 4 * morphCount);
        mdl::Mdl(m)->morphKeyCursors = static_cast<std::uint32_t*>(w1);
        void* w2 = operator new(morphCount);
        std::memset(w2, 0, morphCount);
        mdl::Mdl(m)->morphTrackActive = static_cast<unsigned char*>(w2);
        for (std::int32_t i = 0; i < morphCount; ++i) {
            TracePmxOffset(fh, "morph-record-begin", i, 0);
            mdl::MorphRecord& morph = mdl::Morphs(m)[i];
            morph.jpText = ReadTextBuf(fh, nullIdx);
            WideToSjis(sub, morph.name, morph.jpText,
                       0x14);
            morph.enText = ReadTextBuf(fh, nullIdx);
            WideToSjis(sub, morph.nameEn, morph.enText,
                       0x14);
            _read(fh, &morph.panel, 1);
            _read(fh, &morph.type, 1);
            _read(fh, &morph.offsetCount, 4);
            const std::int32_t cnt = morph.offsetCount;
            const std::uint8_t type = morph.type;
            TracePmxOffset(fh, "morph-header", i,
                           (static_cast<int>(type) << 24) |
                               (cnt & 0x00FFFFFF));
            if (cnt == 0) {
                TracePmxOffset(fh, "morph-record-end", i, 0);
                continue;
            }
            switch (type) {
            case 0: {                                       // group
                morph.groupCount = cnt;
                morph.groupEntries = static_cast<mdl::PmxGroupMorphEntry*>(
                    operator new(sizeof(*morph.groupEntries) * cnt));
                std::memset(morph.groupEntries, 0,
                            sizeof(*morph.groupEntries) * cnt);
                for (int o = 0; o < cnt; ++o) {
                    mdl::PmxGroupMorphEntry& entry = morph.groupEntries[o];
                    entry.morphIndex = readIdxS(morphIdxSize);
                    if (entry.morphIndex < 0) {
                        wchar_t wbuf[256];
                        if (enData)
                            swprintf_s(wbuf, 0x100,
                                       L"%s include wrong data in morph %s",
                                       mdl::PmxTextBuffer(
                                           m, mdl::PmxTextBufferSlot::englishName),
                                       reinterpret_cast<LPCWSTR>(
                                           morph.enText));
                        else
                            swprintf_s(wbuf, 0x100,
                                       kFmtPmxWrongMorphDataJp,
                                       mdl::PmxTextBuffer(
                                           m, mdl::PmxTextBufferSlot::japaneseName),
                                       reinterpret_cast<LPCWSTR>(
                                           morph.jpText));
                        MessageBoxW(hwnd, wbuf, L"load pmx model", 0);
                        entry.morphIndex = 0;
                    }
                    _read(fh, &entry.weight, 4);
                }
                morph.offsetCount = 0;
                break;
            }
            case 1: {                                       // vertex
                morph.vertexEntries = static_cast<mdl::PmdVertexMorphEntry*>(
                    operator new(sizeof(*morph.vertexEntries) * cnt));
                std::memset(morph.vertexEntries, 0,
                            sizeof(*morph.vertexEntries) * cnt);
                for (int o = 0; o < cnt; ++o) {
                    mdl::PmdVertexMorphEntry& entry = morph.vertexEntries[o];
                    entry.vertexIndex = readVertexIdx(vertIdxSize);
                    _read(fh, entry.offset, sizeof(entry.offset));
                    vmap[entry.vertexIndex] = 1;
                }
                break;
            }
            case 2: {                                       // bone
                morph.boneCount = cnt;
                morph.boneEntries = static_cast<mdl::PmxBoneMorphEntry*>(
                    operator new(sizeof(*morph.boneEntries) * cnt));
                std::memset(morph.boneEntries, 0,
                            sizeof(*morph.boneEntries) * cnt);
                for (int o = 0; o < cnt; ++o) {
                    mdl::PmxBoneMorphEntry& entry = morph.boneEntries[o];
                    entry.boneIndex = readIdxS(boneIdxSize);
                    if (entry.boneIndex < 0) {
                        wchar_t wbuf[256];
                        if (enData)
                            swprintf_s(wbuf, 0x100,
                                       L"%s include wrong data in morph %s",
                                       mdl::PmxTextBuffer(
                                           m, mdl::PmxTextBufferSlot::englishName),
                                       reinterpret_cast<LPCWSTR>(
                                           morph.enText));
                        else
                            swprintf_s(wbuf, 0x100,
                                       kFmtPmxWrongMorphDataJp,
                                       mdl::PmxTextBuffer(
                                           m, mdl::PmxTextBufferSlot::japaneseName),
                                       reinterpret_cast<LPCWSTR>(
                                           morph.jpText));
                        MessageBoxW(hwnd, wbuf, L"load pmx model", 0);
                        entry.boneIndex = 0;
                    }
                    _read(fh, entry.translation, sizeof(entry.translation));
                    _read(fh, entry.rotation, sizeof(entry.rotation));
                    // load-time dedup: a bone already offered by an
                    // EARLIER bone morph is not counted, so the baseline
                    // count equals the fill and leaves no zeroed tail
                    // records (x64 0x7FF7CB4CDD2E..CDD7E scans morphs
                    // j < i before the counter increment)
                    bool seen = false;
                    for (std::int32_t j = 0; j < i && !seen; ++j) {
                        const mdl::MorphRecord& prev = mdl::Morphs(m)[j];
                        if (prev.type != 2)
                            continue;
                        for (int p = 0; p < prev.boneCount; ++p) {
                            if (prev.boneEntries[p].boneIndex
                                    == entry.boneIndex) {
                                seen = true;
                                break;
                            }
                        }
                    }
                    if (!seen)
                        ++boneMorphTotal;
                }
                morph.offsetCount = 0;
                break;
            }
            case 3:                                         // UV
            case 4:                                         // addl UV1
            case 5:                                         // addl UV2
            case 6:                                         // addl UV3
            case 7: {                                       // addl UV4
                const int family = type - 3;
                morph.uvCounts[family] = cnt;
                morph.uvEntries[family] = static_cast<mdl::PmxUvMorphEntry*>(
                    operator new(sizeof(*morph.uvEntries[family]) * cnt));
                std::memset(morph.uvEntries[family], 0,
                            sizeof(*morph.uvEntries[family]) * cnt);
                for (int o = 0; o < cnt; ++o) {
                    mdl::PmxUvMorphEntry& entry = morph.uvEntries[family][o];
                    entry.vertexIndex = readVertexIdx(vertIdxSize);
                    _read(fh, entry.offset, sizeof(entry.offset));
                    // load-time dedup within the family: a vertex
                    // already offered by an earlier same-family morph
                    // is not counted (x64 0x7FF7CB4CDF5F..CDFAF scans
                    // morphs j < i of the same type before the family
                    // counter increment)
                    bool seen = false;
                    for (std::int32_t j = 0; j < i && !seen; ++j) {
                        const mdl::MorphRecord& prev = mdl::Morphs(m)[j];
                        if (prev.type != type)
                            continue;
                        for (int p = 0; p < prev.uvCounts[family]; ++p) {
                            if (prev.uvEntries[family][p].vertexIndex
                                    == entry.vertexIndex) {
                                seen = true;
                                break;
                            }
                        }
                    }
                    if (seen)
                        continue;
                    switch (type) {
                    case 3: ++uvMorphTotal; break;
                    case 4: ++uv2Total; break;
                    case 5: ++uv3Total; break;
                    case 6: ++uv4Total; break;
                    case 7: ++uv5Total; break;
                    }
                }
                morph.offsetCount = 0;
                break;
            }
            case 8: {                                       // material
                morph.materialCount = cnt;
                morph.materialEntries =
                    static_cast<mdl::PmxMaterialMorphEntry*>(
                        operator new(sizeof(*morph.materialEntries) * cnt));
                std::memset(morph.materialEntries, 0,
                            sizeof(*morph.materialEntries) * cnt);
                for (int o = 0; o < cnt; ++o) {
                    mdl::PmxMaterialMorphEntry& entry =
                        morph.materialEntries[o];
                    entry.materialIndex = readIdxS(matIdxSize);
                    _read(fh, &entry.operation, 1);
                    _read(fh, entry.channels.diffuse,
                          sizeof(entry.channels.diffuse));
                    _read(fh, entry.channels.specular,
                          sizeof(entry.channels.specular));
                    _read(fh, &entry.channels.specularPower,
                          sizeof(entry.channels.specularPower));
                    _read(fh, entry.channels.ambient,
                          sizeof(entry.channels.ambient));
                    _read(fh, entry.channels.edgeColor,
                          sizeof(entry.channels.edgeColor));
                    _read(fh, &entry.channels.edgeSize,
                          sizeof(entry.channels.edgeSize));
                    _read(fh, entry.channels.textureTint,
                          sizeof(entry.channels.textureTint));
                    _read(fh, entry.channels.sphereTint,
                          sizeof(entry.channels.sphereTint));
                    _read(fh, entry.channels.toonTint,
                          sizeof(entry.channels.toonTint));
                }
                morph.offsetCount = 0;
                break;
            }
            default:                                        // 9/10 skipped
                break;
            }
            TracePmxOffset(fh, "morph-record-end", i,
                           (static_cast<int>(type) << 24) |
                               (cnt & 0x00FFFFFF));
        }
    }

    // ---- morph bookkeeping arrays --------------------------------------------
    std::int32_t vertMorphCount = 0;
    for (std::int32_t v = 0; v < vertCount; ++v)
        if (vmap[v])
            ++vertMorphCount;
    if (vertMorphCount > 0) {
        mdl::BaseVertexMorphCount(m) = vertMorphCount;
        mdl::BaseVertexMorphTable(m) = static_cast<mdl::PmdVertexMorphEntry*>(
            operator new(sizeof(*mdl::BaseVertexMorphTable(m)) * vertMorphCount));
        std::memset(mdl::BaseVertexMorphTable(m), 0,
                    sizeof(*mdl::BaseVertexMorphTable(m)) * vertMorphCount);
        int w = 0;
        for (std::int32_t v = 0; v < vertCount; ++v) {
            if (!vmap[v])
                continue;
            mdl::PmdVertexMorphEntry& rec = mdl::BaseVertexMorphTable(m)[w];
            rec.vertexIndex = v;
            std::memcpy(rec.offset, model.pmxVertices[v].position,
                        sizeof(rec.offset));
            ++w;
        }
    }
    if (vmap)
        operator delete(vmap);

    if (boneMorphTotal > 0) {                               // 32B records
        mdl::BoneMorphOffsetCount(m) = boneMorphTotal;
        mdl::BoneMorphOffsets(m) = static_cast<mdl::BoneMorphOffsetRecord*>(
            operator new(sizeof(mdl::BoneMorphOffsetRecord) * boneMorphTotal));
        std::memset(mdl::BoneMorphOffsets(m), 0,
                    sizeof(mdl::BoneMorphOffsetRecord) * boneMorphTotal);
        int w = 0;
        for (std::int32_t i = 0; i < morphCount; ++i) {
            const mdl::MorphRecord& morph = mdl::Morphs(m)[i];
            if (morph.type != 2)
                continue;
            for (int o = 0; o < morph.boneCount; ++o) {
                // unique-bone table: skip a bone already offered by an
                // EARLIER bone morph (x64 0x7FF7CB4CEFA0 scans j < i only)
                bool seen = false;
                for (std::int32_t j = 0; j < i && !seen; ++j) {
                    const mdl::MorphRecord& prev = mdl::Morphs(m)[j];
                    if (prev.type != 2)
                        continue;
                    for (int p = 0; p < prev.boneCount; ++p) {
                        if (prev.boneEntries[p].boneIndex
                                == morph.boneEntries[o].boneIndex) {
                            seen = true;
                            break;
                        }
                    }
                }
                if (seen)
                    continue;
                mdl::BoneMorphOffsetRecord& rec =
                    mdl::BoneMorphOffsets(m)[w];
                rec.boneIndex = morph.boneEntries[o].boneIndex;
                ++w;
            }
        }
        // entries index the deduped table, not the bone list (x64
        // 0x7FF7CB4CF090 rewrites to the first matching record)
        for (std::int32_t i = 0; i < morphCount; ++i) {
            mdl::MorphRecord& morph = mdl::Morphs(m)[i];
            if (morph.type != 2)
                continue;
            for (int o = 0; o < morph.boneCount; ++o) {
                mdl::PmxBoneMorphEntry& entry = morph.boneEntries[o];
                for (int r = 0; r < boneMorphTotal; ++r) {
                    if (mdl::BoneMorphOffsets(m)[r].boneIndex
                            == entry.boneIndex) {
                        entry.boneIndex = r;
                        break;
                    }
                }
            }
        }
        // record tails: pos zero, quat identity (+28 = 1)
        for (int r = 0; r < boneMorphTotal; ++r) {
            mdl::BoneMorphOffsetRecord& rec = mdl::BoneMorphOffsets(m)[r];
            rec.translation[0] = rec.translation[1] =
                rec.translation[2] = 0.0f;
            rec.rotation[0] = rec.rotation[1] = rec.rotation[2] = 0.0f;
            rec.rotation[3] = 1.0f;
        }
    }

    // UV / additional-UV morph slot records (20B: {vertIdx, vert data})
    struct UvFamily { std::size_t type, morphFamily; };
    static const UvFamily kUvFamilies[5] = {
        {3, 0}, {4, 1}, {5, 2}, {6, 3}, {7, 4}};
    const int uvTotals[5] = {uvMorphTotal, uv2Total, uv3Total, uv4Total,
                             uv5Total};
    for (int fam = 0; fam < 5; ++fam) {
        if (uvTotals[fam] <= 0)
            continue;
        auto* records = static_cast<mdl::PmxUvMorphEntry*>(
            operator new(sizeof(mdl::PmxUvMorphEntry) * uvTotals[fam]));
        mdl::UvMorphCounts(m).byFamily[fam] = uvTotals[fam];
        mdl::UvMorphTables(m).byFamily[fam] = records;
        std::memset(records, 0,
                    sizeof(mdl::PmxUvMorphEntry) * uvTotals[fam]);
        int w = 0;
        for (std::int32_t i = 0; i < morphCount; ++i) {
            const mdl::MorphRecord& morph = mdl::Morphs(m)[i];
            if (morph.type != kUvFamilies[fam].type)
                continue;
            const int cnt = morph.uvCounts[kUvFamilies[fam].morphFamily];
            for (int o = 0; o < cnt; ++o) {
                const std::int32_t vi =
                    morph.uvEntries[kUvFamilies[fam].morphFamily][o].vertexIndex;
                // skip a vertex an earlier same-family morph already
                // offered (x64 0x7FF7CB4CF200 scans j < i only)
                bool seen = false;
                for (std::int32_t j = 0; j < i && !seen; ++j) {
                    const mdl::MorphRecord& prev = mdl::Morphs(m)[j];
                    if (prev.type != kUvFamilies[fam].type)
                        continue;
                    const int pcnt =
                        prev.uvCounts[kUvFamilies[fam].morphFamily];
                    for (int p = 0; p < pcnt; ++p) {
                        if (prev.uvEntries[kUvFamilies[fam].morphFamily][p]
                                .vertexIndex == vi) {
                            seen = true;
                            break;
                        }
                    }
                }
                if (seen)
                    continue;
                mdl::PmxUvMorphEntry& rec = records[w];
                rec.vertexIndex = vi;
                const mdl::PmxVertex& vertex = model.pmxVertices[vi];
                if (fam == 0) {
                    // base-UV family: uv.xy plus the ZW morph base (x64
                    // 0x7FF7CB4CF242..CF2DF reads vertex +0x18/+0x1C/
                    // +0xB0/+0xB4)
                    rec.offset[0] = vertex.uv[0];
                    rec.offset[1] = vertex.uv[1];
                    rec.offset[2] = vertex.uvMorphBaseZW[0];
                    rec.offset[3] = vertex.uvMorphBaseZW[1];
                } else {
                    // additional-UV families: the four components of UV
                    // fam-1 out of the component-major block (x64
                    // 0x7FF7CB4CF472..CF505 steps +0x20/+0x30/+0x40/
                    // +0x50, +4 per family)
                    for (int c = 0; c < 4; ++c)
                        rec.offset[c] =
                            vertex.additionalUvByComponent[c][fam - 1];
                }
                ++w;
            }
        }
    }

    // ---- material pools ------------------------------------------------------
    if (matCount > 0) {
        const auto allocatePool = [matCount]() {
            auto* pool = static_cast<mdl::MaterialMorphPool*>(
                operator new(sizeof(mdl::MaterialMorphPool) * matCount));
            std::memset(pool, 0, sizeof(mdl::MaterialMorphPool) * matCount);
            return pool;
        };
        mdl::MaterialMorphBase(m) = allocatePool();
        mdl::MaterialMorphAdd(m) = allocatePool();
        mdl::MaterialMorphMul(m) = allocatePool();
        for (std::int32_t i = 0; i < matCount; ++i) {
            const mdl::ModelMaterialRecord& mat = materials[i];
            mdl::MaterialMorphChannels& channels =
                mdl::MaterialMorphBase(m)[i].channels;
            std::memcpy(channels.ambient, mat.ambient, sizeof(channels.ambient));
            std::memcpy(channels.diffuse, mat.diffuseMirror,
                        sizeof(channels.diffuse));
            channels.diffuse[3] = mat.diffuse[3];
            std::memcpy(channels.edgeColor, mat.edgeColor,
                        sizeof(channels.edgeColor));
            channels.edgeSize = mat.edgeSize;
            std::memcpy(channels.specular, mat.specular,
                        sizeof(channels.specular));
            channels.specularPower = mat.specularPower;
            for (float& value : channels.textureTint)
                value = 1.0f;
            for (float& value : channels.sphereTint)
                value = 1.0f;
            for (float& value : channels.toonTint)
                value = 1.0f;
        }
    }

    // ---- display frames --------------------------------------------------------
    std::uint32_t frameCount = 0;
    _read(fh, &frameCount, 4);
    struct FrameTmp {
        std::uint8_t special;
        std::int32_t count;
        mdl::PmxDisplayFrameEntry* entries;
        wchar_t* name;
        wchar_t* nameEn;
    };
    FrameTmp* frames = static_cast<FrameTmp*>(
        frameCount ? operator new(sizeof(FrameTmp) * frameCount) : nullptr);
    std::memset(frames, 0, sizeof(FrameTmp) * frameCount);
    std::int32_t boneEntryTotal = 0, morphEntryTotal = 0;
    for (std::uint32_t f = 0; f < frameCount; ++f) {
        frames[f].name = ReadTextBuf(fh, nullIdx);
        frames[f].nameEn = ReadTextBuf(fh, nullIdx);
        _read(fh, &frames[f].special, 1);
        _read(fh, &frames[f].count, 4);
        if (frames[f].count > 0) {
            frames[f].entries = static_cast<mdl::PmxDisplayFrameEntry*>(
                operator new(sizeof(*frames[f].entries) * frames[f].count));
            std::memset(frames[f].entries, 0,
                        sizeof(*frames[f].entries) * frames[f].count);
            for (int e = 0; e < frames[f].count; ++e) {
                mdl::PmxDisplayFrameEntry& entry = frames[f].entries[e];
                _read(fh, &entry.type, 1);
                if (entry.type)
                    entry.index = readIdxS(morphIdxSize);
                else
                    entry.index = readIdxS(boneIdxSize);
            }
            for (int e = 0; e < frames[f].count; ++e)
                if (frames[f].entries[e].type)
                    ++morphEntryTotal;
                else
                    ++boneEntryTotal;
        }
    }

    // facial group records (m+9948): morph entries of ALL frames; the count
    // is stored byte-truncated with no special gate (x64 0x7FF7CB4D0234)
    const int morphGroupCount =
        static_cast<unsigned char>(morphEntryTotal);
    model.facialFrameCount = static_cast<std::uint8_t>(morphGroupCount);
    if (morphGroupCount != 0) {
        model.displayFrames = static_cast<mdl::FrameGroup*>(
            operator new(sizeof(mdl::FrameGroup) * morphGroupCount));
        std::memset(model.displayFrames, 0,
                    sizeof(mdl::FrameGroup) * morphGroupCount);
        int w = 0;
        for (std::uint32_t f = 0; f < frameCount; ++f) {
            for (int e = 0; e < frames[f].count; ++e) {
                if (w >= morphGroupCount)
                    break;  // >255 morph entries overflow in the reference
                const mdl::PmxDisplayFrameEntry& entry = frames[f].entries[e];
                if (entry.type != 1)
                    continue;
                mdl::FrameGroup& rec = model.displayFrames[w];
                rec.groupIndex = 1;                         // facial group
                rec.targetIndex = static_cast<std::uint16_t>(entry.index);
                const mdl::MorphRecord& morph = mdl::Morphs(m)[entry.index];
                strcpy_s(rec.name, 0x14, morph.name);
                strcpy_s(rec.nameEn, 0x14, morph.nameEn);
                ++w;
            }
        }
    }
    // Root bone from the first PMX display frame's first entry, taken only
    // when that entry is a bone entry (type==0); the special flag byte is
    // never consulted (x64 0x7FF7CB4D03A7..0x4D03B9).
    if (frameCount > 0 && frames[0].count > 0
        && frames[0].entries[0].type == 0) {
        model.displayRootBone = frames[0].entries[0].index;
    }
    // group name table (m+9936)
    model.groupCount = static_cast<std::uint8_t>(frameCount);
    if (model.facialFrameCount)
        model.groupCount =
            static_cast<std::uint8_t>(frameCount + 1);
    {
        const int groups = model.groupCount;
        mdl::DisplayGroup*& displayGroups = mdl::DisplayGroups(m);
        displayGroups = static_cast<mdl::DisplayGroup*>(
            operator new(sizeof(*displayGroups) * (groups ? groups : 1)));
        std::memset(displayGroups, 0,
                    sizeof(*displayGroups) * (groups ? groups : 1));
        const mdl::BoneRecord& rootBone =
            mdl::Bones(m)[model.displayRootBone];
        strcpy_s(displayGroups[0].name, sizeof(displayGroups[0].name),
                 rootBone.name);
        strcpy_s(displayGroups[1].name, sizeof(displayGroups[1].name),
                 "Root");
        strcpy_s(displayGroups[0].nameEn, sizeof(displayGroups[0].nameEn),
                 rootBone.nameEn);
        strcpy_s(displayGroups[1].nameEn, sizeof(displayGroups[1].nameEn),
                 "Disp/IK/OP");
        int start = 2;
        if (model.facialFrameCount) {
            strcpy_s(displayGroups[2].name, sizeof(displayGroups[2].name),
                     kLblFacialJp);
            strcpy_s(displayGroups[2].nameEn,
                     sizeof(displayGroups[2].nameEn), "Facial");
            start = 3;
        }
        for (int g = start; g < groups; ++g) {
            const int frameIndex = model.facialFrameCount ? g - 1 : g;
            WideToSjis(sub, displayGroups[g].name, frames[frameIndex].name,
                       sizeof(displayGroups[g].name));
            WideToSjis(sub, displayGroups[g].nameEn, frames[frameIndex].nameEn,
                       sizeof(displayGroups[g].nameEn));
        }
    }
    // bone frame records (m+9944): bone entries from frame 2 on
    model.rigidBodyCount = boneEntryTotal;
    if (boneEntryTotal > 0) {
        auto* boneFrames = static_cast<mdl::FrameGroup*>(
            operator new(sizeof(mdl::FrameGroup) * boneEntryTotal));
        model.rbGroups = boneFrames;
        std::memset(boneFrames, 0,
                    sizeof(mdl::FrameGroup) * boneEntryTotal);
        int w = 0;
        int groupIdx = 2;
        for (std::uint32_t f = 2; f < frameCount; ++f) {
            for (int e = 0; e < frames[f].count; ++e) {
                const mdl::PmxDisplayFrameEntry& entry = frames[f].entries[e];
                if (entry.type)
                    continue;
                mdl::FrameGroup& rec = boneFrames[w];
                rec.groupIndex = static_cast<std::uint16_t>(
                    model.facialFrameCount ? groupIdx + 1 : groupIdx);
                rec.targetIndex = static_cast<std::uint16_t>(entry.index);
                const mdl::BoneRecord& bone = mdl::Bones(m)[rec.targetIndex];
                strcpy_s(rec.name, 0x14, bone.name);
                strcpy_s(rec.nameEn, 0x14, bone.nameEn);
                ++w;
            }
            ++groupIdx;
        }
    }
    for (std::uint32_t f = 0; f < frameCount; ++f) {
        if (frames[f].name)
            operator delete(frames[f].name);
        if (frames[f].nameEn)
            operator delete(frames[f].nameEn);
        if (frames[f].entries)
            operator delete(frames[f].entries);
    }
    if (frames)
        operator delete(frames);

    // ---- rigid bodies -----------------------------------------------------------
    _read(fh, &model.rigidCount, sizeof(model.rigidCount));
    if (model.rigidCount > 0) {
        PhysicsScene* physScene = model.scenePtr;
        SetPhysicsMode(m, 0, nullptr, 0);                   // 0x4A9220
        SetPhysicsMode(m, 1, nullptr, 0);
        const std::uint8_t rigidIdxSize = model.pmxRigidIndexSize;
        auto readRigidIdx = [fh, rigidIdxSize]() -> std::int32_t {
            switch (rigidIdxSize) {
            case 1: { std::int8_t v; _read(fh, &v, 1); return v; }
            case 2: { std::uint16_t v; _read(fh, &v, 2);
                      return static_cast<std::int16_t>(v); }
            default: { std::int32_t v; _read(fh, &v, 4); return v; }
            }
        };
        const std::int32_t rcount = static_cast<std::int32_t>(model.rigidCount);
        model.rigidTable = static_cast<mdl::RigidRecord*>(
            operator new(sizeof(mdl::RigidRecord) * rcount));
        std::memset(model.rigidTable, 0,
                    sizeof(mdl::RigidRecord) * rcount);
        mikudancestudio::mdl::BoneRecord* bones = mikudancestudio::mdl::Bones(m);
        for (std::int32_t i = 0; i < rcount; ++i) {
            TracePmxOffset(fh, "rigid-record-begin", i, rcount);
            mdl::RigidRecord& rb = model.rigidTable[i];
            rb.jpText = ReadTextBuf(fh, nullIdx);
            WideToSjis(sub, rb.name, rb.jpText, 0x14);
            rb.enText = ReadTextBuf(fh, nullIdx);
            rb.boneIndex = readBoneIdx();
            _read(fh, &rb.group, 1);
            _read(fh, &rb.noCollapse, 2);
            _read(fh, &rb.shape, 1);
            _read(fh, rb.size, sizeof(rb.size));
            _read(fh, rb.position, sizeof(rb.position));
            _read(fh, rb.rotation, sizeof(rb.rotation));
            _read(fh, &rb.mass, 4);
            _read(fh, &rb.linearDamping, 4);
            _read(fh, &rb.angularDamping, 4);
            _read(fh, &rb.restitution, 4);
            _read(fh, &rb.friction, 4);
            _read(fh, &rb.mode, 1);
            if (rb.mode == 2)
                rb.kinematicFlag = 1;
            if (rb.mode == 0)
                rb.staticFlag = 1;
            // PMX positions are bone-relative - subtract the bone pos.
            // 负 boneIndex 时原版同样以 bones[0].position 做重定基并在正向
            // 平移加回：sub_7FF7CB4C9AC0 @0x7FF7CB4D0DF4..0x7FF7CB4D0E9D
            // （重定基三分量 +13Ch/+140h/+144h）、@0x7FF7CB4D0FEA（正向平移）
            const std::int32_t bi = rb.boneIndex;
            const float* bp = bones[bi >= 0 ? bi : 0].position;
            rb.position[0] -= bp[0];
            rb.position[1] -= bp[1];
            rb.position[2] -= bp[2];
            {
                D3DXMATRIXF mat, tmp;
                d.rotZ(&mat, rb.rotation[2]);
                d.rotX(&tmp, rb.rotation[0]);
                d.multiply(&mat, &mat, &tmp);
                d.rotY(&tmp, rb.rotation[1]);
                d.multiply(&mat, &mat, &tmp);
                d.translation(&tmp, rb.position[0], rb.position[1],
                              rb.position[2]);
                d.multiply(&mat, &mat, &tmp);
                d.translation(&tmp, bp[0], bp[1], bp[2]);
                d.multiply(&mat, &mat, &tmp);
                void* bodyOut[2] = {nullptr, nullptr};
                CreateRigidBody(physScene, bodyOut, rb.shape,
                                rb.size[0], rb.size[1], rb.size[2],
                                reinterpret_cast<const float*>(&mat),
                                rb.mode, rb.mass, rb.linearDamping,
                                rb.angularDamping, rb.restitution, rb.friction,
                                static_cast<char>(rb.group), rb.noCollapse);
                rb.keyData = bodyOut[0];
                rb.body = bodyOut[1];
                if (rb.mode > 0 && bi >= 0)
                    bones[bi].hasRigidBody = 1;
                // inverse transform into rb+108
                // `float[77..79]` is the x86 spelling of BoneRecord::position.
                // It is not valid after the x64 text-pointer expansion and
                // corrupts the rigid inverse/constraint coordinate system.
                const mdl::BoneRecord& linkedBone =
                    bones[bi < 0 ? 0 : bi];
                const float* bonePosition = linkedBone.position;
                D3DXMATRIXF inv, t2;
                d.translation(&inv, -bonePosition[0], -bonePosition[1],
                              -bonePosition[2]);
                d.translation(&t2, -rb.position[0], -rb.position[1],
                              -rb.position[2]);
                d.multiply(&inv, &inv, &t2);
                d.rotY(&t2, -rb.rotation[1]);
                d.multiply(&inv, &inv, &t2);
                d.rotX(&t2, -rb.rotation[0]);
                d.multiply(&inv, &inv, &t2);
                d.rotZ(&t2, -rb.rotation[2]);
                d.multiply(&inv, &inv, &t2);
                std::memcpy(rb.invTransform, &inv, sizeof(rb.invTransform));
            }
            TracePmxOffset(fh, "rigid-record-end", i, rcount);
        }

        // ---- joints ------------------------------------------------------------
        _read(fh, &model.jointCount, sizeof(model.jointCount));
        TracePmxOffset(fh, "joints-begin", model.jointCount, rcount);
        if (model.jointCount > 0) {
            const std::int32_t jcount = static_cast<std::int32_t>(model.jointCount);
            model.jointTable = static_cast<mdl::JointRecord*>(
                operator new(sizeof(mdl::JointRecord) * jcount));
            std::memset(model.jointTable, 0,
                        sizeof(mdl::JointRecord) * jcount);
            for (std::int32_t i = 0; i < jcount; ++i) {
                TracePmxOffset(fh, "joint-record-begin", i, jcount);
                mdl::JointRecord& jt = model.jointTable[i];
                jt.jpText = ReadTextBuf(fh, nullIdx);
                WideToSjis(sub, jt.name, jt.jpText, 0x14);
                jt.enText = ReadTextBuf(fh, nullIdx);
                std::uint8_t jointType = 0;
                _read(fh, &jointType, 1);                    // ignored by v9.32
                jt.rigidA = readRigidIdx();
                jt.rigidB = readRigidIdx();
                _read(fh, jt.position, sizeof(jt.position));
                _read(fh, jt.rotation, sizeof(jt.rotation));
                // Original stores the four limit vectors in a shuffled order.
                _read(fh, &jt.limits[3], 3 * sizeof(float));
                _read(fh, &jt.limits[0], 3 * sizeof(float));
                _read(fh, &jt.limits[9], 3 * sizeof(float));
                _read(fh, &jt.limits[6], 3 * sizeof(float));
                _read(fh, jt.springs, sizeof(jt.springs));
                {
                    using d3dx::D3DXMATRIXF;
                    const std::int32_t a = jt.rigidA;
                    const std::int32_t b = jt.rigidB;
                    mdl::RigidRecord& rigidA = mdl::Rigids(m)[a];
                    mdl::RigidRecord& rigidB = mdl::Rigids(m)[b];
                    D3DXMATRIXF ma, mb;
                    std::memcpy(&ma, rigidA.invTransform, sizeof(ma));
                    std::memcpy(&mb, rigidB.invTransform, sizeof(mb));
                    const float* point = jt.position;
                    float pivotA[3], pivotB[3];
                    TransformJointPointOriginal(pivotA, point, &ma.m[0][0]);
                    TransformJointPointOriginal(pivotB, point, &mb.m[0][0]);
                    const float ax = pivotA[0], ay = pivotA[1], az = pivotA[2];
                    const float bx = pivotB[0], by = pivotB[1], bz = pivotB[2];
                    const float distA =
                        std::sqrt(ax * ax + ay * ay + az * az);
                    const float distB =
                        std::sqrt(bx * bx + by * by + bz * bz);
                    // 过拉伸限位半径：限位范数叠加两锚点距离
                    // （x64 @0x7FF7CB4D1B8A..0x4D1C4A，见 JointRadiusBound）
                    jt.radiusBound = JointRadiusBound(jt.limits, distA, distB);
                    D3DXMATRIXF rot, t2;
                    float qa[4], qb[4];
                    d.rotZ(&rot, jt.rotation[2]);
                    d.rotX(&t2, jt.rotation[0]);
                    d.multiply(&rot, &rot, &t2);
                    d.rotY(&t2, jt.rotation[1]);
                    d.multiply(&rot, &rot, &t2);
                    d.rotY(&t2, -rigidA.rotation[1]);
                    d.multiply(&rot, &rot, &t2);
                    d.rotX(&t2, -rigidA.rotation[0]);
                    d.multiply(&rot, &rot, &t2);
                    d.rotZ(&t2, -rigidA.rotation[2]);
                    d.multiply(&rot, &rot, &t2);
                    d.quatFromMatrix(qa, &rot);
                    d.rotZ(&rot, jt.rotation[2]);
                    d.rotX(&t2, jt.rotation[0]);
                    d.multiply(&rot, &rot, &t2);
                    d.rotY(&t2, jt.rotation[1]);
                    d.multiply(&rot, &rot, &t2);
                    d.rotY(&t2, -rigidB.rotation[1]);
                    d.multiply(&rot, &rot, &t2);
                    d.rotX(&t2, -rigidB.rotation[0]);
                    d.multiply(&rot, &rot, &t2);
                    d.rotZ(&t2, -rigidB.rotation[2]);
                    d.multiply(&rot, &rot, &t2);
                    d.quatFromMatrix(qb, &rot);
                    jt.constraint = CreatePhysJoint(
                        physScene,
                        rigidA.body, rigidB.body,
                        ax, ay, az, qa[0], qa[1], qa[2], qa[3],
                        bx, by, bz, qb[0], qb[1], qb[2], qb[3],
                        jt.limits[0], jt.limits[1], jt.limits[2],
                        jt.limits[3], jt.limits[4], jt.limits[5],
                        jt.limits[6], jt.limits[7], jt.limits[8],
                        jt.limits[9], jt.limits[10], jt.limits[11],
                        jt.springs[0], jt.springs[1], jt.springs[2],
                        jt.springs[3], jt.springs[4], jt.springs[5]);
                }
                TracePmxOffset(fh, "joint-record-end", i, jcount);
            }
        }
    }
    _close(fh);

    // ---- animation pools -------------------------------------------------------
    model.boneKeys = static_cast<mdl::BoneKey*>(
        operator new(sizeof(mdl::BoneKey) * mdl::kBoneKeyCapacity));
    std::memset(model.boneKeys, 0,
                  sizeof(mdl::BoneKey) * mdl::kBoneKeyCapacity);
    model.morphKeys = static_cast<mdl::MorphKey*>(
        operator new(sizeof(mdl::MorphKey) * 20000));
    std::memset(model.morphKeys, 0, sizeof(mdl::MorphKey) * 20000);
    model.displayKeys = static_cast<mdl::DisplayKey*>(
        operator new(sizeof(mdl::DisplayKey) * 1000));
    std::memset(model.displayKeys, 0, sizeof(mdl::DisplayKey) * 1000);
    for (int frame = 0; frame < 1000; ++frame) {
        mdl::DisplayKey& fr = model.displayKeys[frame];
        if (model.ikChainCount > 0) {
            unsigned char* ikFlags = static_cast<unsigned char*>(
                operator new(model.ikChainCount));
            mdl::IkStates(fr) = ikFlags;
            for (int i = 0; i < static_cast<int>(model.ikChainCount); ++i)
                ikFlags[i] = 1;
        }
        fr.visible = 1;
        if (model.boneOrderCount > 0) {
            mdl::SelectorStates(fr) = static_cast<mdl::BoneReference*>(
                operator new(sizeof(mdl::BoneReference) *
                             model.boneOrderCount));
            for (int s = 0; s < static_cast<int>(model.boneOrderCount); ++s) {
                auto* pair = mdl::SelectorStates(fr);
                pair[s].modelIndex = -1;
                pair[s].boneIndex = 0;
            }
        }
    }
    mdl::BoneKeyIndices(m) = static_cast<std::uint32_t*>(
        operator new(sizeof(*mdl::BoneKeyIndices(m)) * model.boneCount));
    mdl::MorphKeyIndices(m) = static_cast<std::uint32_t*>(
        operator new(sizeof(*mdl::MorphKeyIndices(m)) * model.morphCount));
    for (std::int32_t i = 0; i < static_cast<std::int32_t>(model.boneCount); ++i) {
        mdl::BoneKey& key = model.boneKeys[i];
        for (int c = 0; c < 4; ++c) {
            key.interpolation[c] = 20;
            key.interpolation[c + 4] = 20;
            key.interpolation[c + 8] = 107;
            key.interpolation[c + 12] = 107;
        }
        key.rotation[3] = 1.0f;
    }
    model.loadComplete = 1;
    PostLoadInit(m);                                        // 0x49C850
    return true;
}

}  // namespace mikudancestudio
