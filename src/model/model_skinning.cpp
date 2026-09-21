// ===========================================================================
// VA 0x004B0C50 / 0x004A9400 - per-frame model VB deformation
// ===========================================================================
// 0x4B0C50 restores and accumulates vertex morphs, locks model+8/model+12,
// and dispatches one of the 0x4A9400-family OpenMP skinning workers. This
// file implements the PMD path and the x64 PMX twin family:
// the five stride workers selected by the additional-UV count (x64
// RVA 0x11F3A0 / 0x120D20 / 0x122720 / 0x124150 / 0x125BF0),
// the per-frame PMX vertex/UV morph restore+apply pass that feeds them
// (x64 RVA 0xC1415..0xC2670), and the SDEF quaternion blend helper
// (x64 RVA 0xC2D20). Binary identity and address verification are recorded
// in reports/fix21_source_evidence.md. The PMD arithmetic keeps its x87 __asm bit-parity
// branch; the PMX arithmetic follows the x64 SSE orderings, which is the
// behavior baseline for this port.
// ===========================================================================
#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <Windows.h>
#include <d3d9.h>

#include <cmath>
#include <cstdint>
#include <cstdio>
#include <cstring>

#include "mikudancestudio/d3dx_dyn.hpp"
#include "mikudancestudio/mmd_app.hpp"
#include "mikudancestudio/model.hpp"
#include "mikudancestudio/ported_funcs.hpp"

namespace mikudancestudio {
namespace {

// Porting-era VB dump under MIKUDANCESTUDIO_VB_DUMP_DIR (CMake option
// MIKUDANCESTUDIO_DIAG, default OFF); the OFF stub below keeps the call
// site valid and inlines away to nothing.
#ifdef MIKUDANCESTUDIO_DIAG
bool ModelVbCaptureReady(char directory[MAX_PATH]) {
    const DWORD length = GetEnvironmentVariableA(
        "MIKUDANCESTUDIO_VB_DUMP_DIR", directory, MAX_PATH);
    if (length == 0 || length >= MAX_PATH)
        return false;

    char stable[2]{};
    if (GetEnvironmentVariableA("MIKUDANCESTUDIO_AB_STABLE_CAPTURE", stable,
                                sizeof(stable)) != 1 || stable[0] != '1')
        return true;

    char requireLine[2]{};
    const bool refreshed =
        GetEnvironmentVariableA("MIKUDANCESTUDIO_AB_REQUIRE_LINE", requireLine,
                                sizeof(requireLine)) == 1 &&
        requireLine[0] == '1';
    char gate[MAX_PATH]{};
    std::snprintf(gate, sizeof(gate), "%s\\%s", directory,
                  refreshed ? "vb.capture.active" : "vb.capture.ready");
    const DWORD attributes = GetFileAttributesA(gate);
    return attributes != INVALID_FILE_ATTRIBUTES &&
           (attributes & FILE_ATTRIBUTE_DIRECTORY) == 0;
}

void WriteBytes(const char* path, const void* bytes, DWORD size) {
    HANDLE file = CreateFileA(path, GENERIC_WRITE, FILE_SHARE_READ, nullptr,
                              CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, nullptr);
    if (file == INVALID_HANDLE_VALUE)
        return;
    DWORD written = 0;
    WriteFile(file, bytes, size, &written, nullptr);
    CloseHandle(file);
}

void DumpModelVertexBuffers(MMDApp* app, unsigned char* model,
                            const mdl::SkinnedVertexBase* mainVertices,
                            const mdl::EdgeVertex* edgeVertices,
                            std::uint32_t count,
                            std::uint32_t mainStride) {
    char directory[MAX_PATH]{};
    if (!ModelVbCaptureReady(directory))
        return;

    int slot = -1;
    // port diagnostic: bound = model-slot capacity (kModelSlotCount)
    for (int i = 0; i < kModelSlotCount; ++i) {
        if (app->ModelSlot(i) == model) {
            slot = i;
            break;
        }
    }
    if (slot < 0)
        return;

    // sized to the slot capacity: the old [100] let a slot >= 100 index past
    // the array (out-of-bounds InterlockedCompareExchange write)
    static LONG dumped[kModelSlotCount]{};
    if (InterlockedCompareExchange(&dumped[slot], 1, 0) != 0)
        return;

    CreateDirectoryA(directory, nullptr);
    char path[MAX_PATH]{};
    std::snprintf(path, sizeof(path), "%s\\model.%03d.main.bin",
                  directory, slot);
    WriteBytes(path, mainVertices, mainStride * count);
    std::snprintf(path, sizeof(path), "%s\\model.%03d.edge.bin",
                  directory, slot);
    WriteBytes(path, edgeVertices, sizeof(*edgeVertices) * count);
    std::snprintf(path, sizeof(path), "%s\\model.%03d.meta.json",
                  directory, slot);
    char metadata[160]{};
    const mdl::ModelRecord& state = *mdl::Mdl(model);
    const int chars = std::snprintf(
        metadata, sizeof(metadata),
        "{\"schema\":1,\"slot\":%d,\"format\":%u,"
        "\"vertex_count\":%u,\"main_stride\":%u,\"edge_stride\":16}\n",
        slot, static_cast<unsigned>(state.physicsMode),
        count, mainStride);
    WriteBytes(path, metadata, static_cast<DWORD>(chars));
}
#else
inline void DumpModelVertexBuffers(MMDApp*, unsigned char*,
                                   const mdl::SkinnedVertexBase*,
                                   const mdl::EdgeVertex*, std::uint32_t,
                                   std::uint32_t) {}
#endif

void TransformPosition(float out[3], const float in[3], const float* m) {
    out[0] = in[0] * m[0] + in[1] * m[4] + in[2] * m[8] + m[12];
    out[1] = in[0] * m[1] + in[1] * m[5] + in[2] * m[9] + m[13];
    out[2] = in[0] * m[2] + in[1] * m[6] + in[2] * m[10] + m[14];
}

void TransformNormal(float out[3], const float in[3], const float* m) {
    out[0] = in[0] * m[0] + in[1] * m[4] + in[2] * m[8];
    out[1] = in[0] * m[1] + in[1] * m[5] + in[2] * m[9];
    out[2] = in[0] * m[2] + in[1] * m[6] + in[2] * m[10];
}

void Blend3(float out[3], const float a[3], const float b[3], float wa) {
    const float wb = 1.0f - wa;
    out[0] = a[0] * wa + b[0] * wb;
    out[1] = a[1] * wa + b[1] * wb;
    out[2] = a[2] * wa + b[2] * wb;
}

float EdgeComponent(float position, float normal, float amount) {
#if defined(_M_IX86)
    float result = 0.0f;
    __asm {
        fld dword ptr [normal]
        fmul dword ptr [amount]
        fadd dword ptr [position]
        fstp dword ptr [result]
    }
    return result;
#else
    return position + normal * amount;
#endif
}

// x64 fetches one matInit per referenced bone and only tests the sign of
// the index; a negative reference contributes an all-zero matrix, not an
// identity. The upper bound is a port-side guard against corrupt files -
// every loader-produced index is inside the table.
constexpr float kPmxZeroMatrix[16] = {
    0.0f, 0.0f, 0.0f, 0.0f,
    0.0f, 0.0f, 0.0f, 0.0f,
    0.0f, 0.0f, 0.0f, 0.0f,
    0.0f, 0.0f, 0.0f, 0.0f,
};

const float* PmxSkinMatrix(const mdl::BoneRecord* bones, int boneCount,
                           std::int32_t boneIndex) {
    return boneIndex >= 0 && boneIndex < boneCount
        ? bones[boneIndex].matInit
        : kPmxZeroMatrix;
}

// x64 PMX worker RVA 0x11F991: BDEF1 transforms position with the full
// matrix and normal without translation into the 32-byte base record.
void SkinPmxBdef1(const mdl::PmxVertex& source,
                  const mdl::BoneRecord* bones, int boneCount,
                  mdl::SkinnedVertexBase& destination) {
    const float* matrix = PmxSkinMatrix(bones, boneCount, source.bone[0]);
    TransformPosition(destination.position, source.position, matrix);
    TransformNormal(destination.normal, source.normal, matrix);
}

float BlendTransformComponent(const float* input, const float* matrix0,
                              const float* matrix1, int component,
                              float weight0, bool position) {
#if defined(_M_IX86)
    float result = 0.0f;
    const int byteOffset = component * 4;
    const float weight1 = 1.0f - weight0;
    __asm {
        mov eax, input
        mov ecx, matrix0
        mov edx, matrix1
        mov ebx, byteOffset

        // Original 0x4A95E0 family: bone 1 is accumulated first, multiplied
        // by (1-weight), then bone 0 is accumulated and multiplied by
        // weight.  No transformed component is rounded to float in between.
        fld dword ptr [eax]
        fmul dword ptr [edx+ebx]
        fld dword ptr [eax+4]
        fmul dword ptr [edx+ebx+16]
        faddp st(1), st
        fld dword ptr [eax+8]
        fmul dword ptr [edx+ebx+32]
        faddp st(1), st
        cmp position, 0
        je no_translation_1
        fadd dword ptr [edx+ebx+48]
no_translation_1:
        fmul dword ptr [weight1]

        fld dword ptr [eax]
        fmul dword ptr [ecx+ebx]
        fld dword ptr [eax+4]
        fmul dword ptr [ecx+ebx+16]
        faddp st(1), st
        fld dword ptr [eax+8]
        fmul dword ptr [ecx+ebx+32]
        faddp st(1), st
        cmp position, 0
        je no_translation_0
        fadd dword ptr [ecx+ebx+48]
no_translation_0:
        fmul dword ptr [weight0]
        faddp st(1), st
        fstp dword ptr [result]
    }
    return result;
#else
    const float weight1 = 1.0f - weight0;
    const float a = input[0] * matrix0[component] +
                    input[1] * matrix0[component + 4] +
                    input[2] * matrix0[component + 8] +
                    (position ? matrix0[component + 12] : 0.0f);
    const float b = input[0] * matrix1[component] +
                    input[1] * matrix1[component + 4] +
                    input[2] * matrix1[component + 8] +
                    (position ? matrix1[component + 12] : 0.0f);
    return b * weight1 + a * weight0;
#endif
}

// x64 PMX worker RVA 0x11F513: BDEF2 uses bone 1 first with (1-weight),
// then adds bone 0 scaled by weight. BlendTransformComponent preserves that
// source order on the x64 path (and bit-exactly on the x87 path).
void SkinPmxBdef2(const mdl::PmxVertex& source,
                  const mdl::BoneRecord* bones, int boneCount,
                  mdl::SkinnedVertexBase& destination) {
    const float* matrix0 = PmxSkinMatrix(bones, boneCount, source.bone[0]);
    const float* matrix1 = PmxSkinMatrix(bones, boneCount, source.bone[1]);
    const float weight0 = source.weight[0];
    for (int component = 0; component < 3; ++component) {
        destination.position[component] = BlendTransformComponent(
            source.position, matrix0, matrix1, component, weight0, true);
        destination.normal[component] = BlendTransformComponent(
            source.normal, matrix0, matrix1, component, weight0, false);
    }
}

float TransformComponent(const float input[3], const float matrix[16],
                         int component, bool position) {
    float value = input[0] * matrix[component];
    value += input[1] * matrix[component + 4];
    value += input[2] * matrix[component + 8];
    if (position)
        value += matrix[component + 12];
    return value;
}

// x64 PMX worker RVA 0x11FB7F: BDEF4 retrieves all four matrices with the
// same negative-index zero fallback. The scalar chains are ordered
// differently for positions and normals in the original: the position
// accumulates bone 1 first and then bones 0, 2 and 3, while the normal
// starts from bone 0. Float addition does not associate, so both orders
// are spelled out instead of being hidden in one generic loop.
void SkinPmxBdef4(const mdl::PmxVertex& source,
                  const mdl::BoneRecord* bones, int boneCount,
                  mdl::SkinnedVertexBase& destination) {
    const float* matrix0 = PmxSkinMatrix(bones, boneCount, source.bone[0]);
    const float* matrix1 = PmxSkinMatrix(bones, boneCount, source.bone[1]);
    const float* matrix2 = PmxSkinMatrix(bones, boneCount, source.bone[2]);
    const float* matrix3 = PmxSkinMatrix(bones, boneCount, source.bone[3]);
    for (int component = 0; component < 3; ++component) {
        float position =
            TransformComponent(source.position, matrix1, component, true) *
            source.weight[1];
        position +=
            TransformComponent(source.position, matrix0, component, true) *
            source.weight[0];
        position +=
            TransformComponent(source.position, matrix2, component, true) *
            source.weight[2];
        position +=
            TransformComponent(source.position, matrix3, component, true) *
            source.weight[3];
        destination.position[component] = position;

        float normal =
            TransformComponent(source.normal, matrix0, component, false) *
            source.weight[0];
        normal +=
            TransformComponent(source.normal, matrix1, component, false) *
            source.weight[1];
        normal +=
            TransformComponent(source.normal, matrix2, component, false) *
            source.weight[2];
        normal +=
            TransformComponent(source.normal, matrix3, component, false) *
            source.weight[3];
        destination.normal[component] = normal;
    }
}

// x64 helper RVA 0xC2D20: SDEF rotates through a blended quaternion that
// the original builds with a dot-sign-corrected normalized lerp (not a
// true slerp). `t` reaches 1 - weight0, so weight0 = 1 keeps q0.
void BlendSdefQuaternions(float out[4], const float from[4],
                          const float to[4], float t) {
    const float dot = ((to[0] * from[0] + to[3] * from[3]) +
                       to[1] * from[1]) + to[2] * from[2];
    float delta[4];
    if (dot >= 0.0f) {
        delta[0] = to[0] - from[0];
        delta[1] = to[1] - from[1];
        delta[2] = to[2] - from[2];
        delta[3] = to[3] - from[3];
    } else {
        t = -t;
        delta[0] = to[0] + from[0];
        delta[1] = to[1] + from[1];
        delta[2] = to[2] + from[2];
        delta[3] = to[3] + from[3];
    }
    out[0] = delta[0] * t + from[0];
    out[1] = delta[1] * t + from[1];
    out[2] = delta[2] * t + from[2];
    out[3] = delta[3] * t + from[3];
    const float length = std::sqrt(
        ((out[0] * out[0] + out[1] * out[1]) + out[2] * out[2]) +
        out[3] * out[3]);
    if (length <= 0.0f) {
        out[0] = 0.0f;
        out[1] = 0.0f;
        out[2] = 0.0f;
        out[3] = 1.0f;
        return;
    }
    const float inverse = 1.0f / length;
    out[0] *= inverse;
    out[1] *= inverse;
    out[2] *= inverse;
    out[3] *= inverse;
}

// x64 PMX worker 0x140120285. The loader has already rebased R0/R1 onto the
// weighted SDEF midpoint, which makes the blend stable at the bind pose.
// For every component the original computes
//   Pc = M0(C)*w0 + M1(C)*(1-w0)            (full affine transforms)
//   U  = M0linear(R0)*w0 + M1linear(R1)*(1-w0)  (no translation)
//   center = ((U + Pc) + Pc) * 0.5          (exact x64 folding)
// and places the vertex at R * (position - C) + center, where R comes from
// D3DXQuaternionRotationMatrix on both bone matrices, the nlerp above, and
// D3DXMatrixRotationQuaternion - the very entry points the x64 imports, so
// even degenerate zero-matrix references produce identical bits.
void SkinPmxSdef(d3dx::Api& d3dxApi, const mdl::PmxVertex& source,
                 const mdl::BoneRecord* bones, int boneCount,
                 mdl::SkinnedVertexBase& destination) {
    const float* matrix0 = PmxSkinMatrix(bones, boneCount, source.bone[0]);
    const float* matrix1 = PmxSkinMatrix(bones, boneCount, source.bone[1]);
    const float weight0 = source.weight[0];
    const float weight1 = 1.0f - weight0;
    const float* center = source.sdef.center;
    const float* r0 = source.sdef.r0Offset;
    const float* r1 = source.sdef.r1Offset;

    float pivot[3];
    for (int component = 0; component < 3; ++component) {
        const float blendedCenter =
            TransformComponent(center, matrix0, component, true) * weight0 +
            TransformComponent(center, matrix1, component, true) * weight1;
        const float blendedArms =
            TransformComponent(r0, matrix0, component, false) * weight0 +
            TransformComponent(r1, matrix1, component, false) * weight1;
        pivot[component] = ((blendedArms + blendedCenter) + blendedCenter) *
                           0.5f;
    }

    float quaternion0[4];
    float quaternion1[4];
    float quaternion[4];
    d3dxApi.quatFromMatrix(
        quaternion0, reinterpret_cast<const d3dx::D3DXMATRIXF*>(matrix0));
    d3dxApi.quatFromMatrix(
        quaternion1, reinterpret_cast<const d3dx::D3DXMATRIXF*>(matrix1));
    BlendSdefQuaternions(quaternion, quaternion0, quaternion1, weight1);
    d3dx::D3DXMATRIXF blended;
    d3dxApi.matrixRotationQuaternion(&blended, quaternion);
    const float* rotation = &blended.m[0][0];

    const float delta[3] = {
        source.position[0] - center[0],
        source.position[1] - center[1],
        source.position[2] - center[2]};
    for (int component = 0; component < 3; ++component) {
        destination.position[component] =
            TransformComponent(delta, rotation, component, false) +
            pivot[component];
        destination.normal[component] =
            TransformComponent(source.normal, rotation, component, false);
    }
}

// x64 PMX common tail 0x140120A01/0x140120A9C: both branches truncate the
// material's edge channels to 8 bits. The self-shadow render pass forces an
// opaque alpha word; the regular pass keeps the material's own edge alpha.
std::uint32_t PackPmxEdgeColor(const mdl::ModelMaterialRecord& material,
                               bool opaqueAlpha) {
    const auto channel = [](float value) -> std::uint32_t {
        return static_cast<std::uint8_t>(static_cast<std::int32_t>(
            value * 255.0f));
    };
    const std::uint32_t red = channel(material.edgeColor[0]);
    const std::uint32_t green = channel(material.edgeColor[1]);
    const std::uint32_t blue = channel(material.edgeColor[2]);
    const std::uint32_t alpha =
        opaqueAlpha ? 0xFFu : channel(material.edgeColor[3]);
    return (alpha << 24) | (red << 16) | (green << 8) | blue;
}

// The x64 worker ladder uses a 32-byte base record plus one contiguous
// float4 for every PMX additional UV set, transposed out of the loader's
// component-first source storage.
template <std::size_t AdditionalUvCount>
void CopyPmxAdditionalUvs(const mdl::PmxVertex& source,
                          mdl::SkinnedVertex<AdditionalUvCount>& destination) {
    destination.base.uv[0] = source.uv[0];
    destination.base.uv[1] = source.uv[1];
    for (std::size_t uv = 0; uv < AdditionalUvCount; ++uv)
        for (std::size_t component = 0; component < 4; ++component)
            destination.additionalUv[uv][component] =
                source.additionalUvByComponent[component][uv];
}

// One PMX morph's vertex/UV contribution (the x64 direct ladder at
// RVA 0xC2030..0xC2659; group references RVA 0xC1870 reuse it with the
// group weight folded in after the morph value, one multiply at a time).
// Bone (2) and material (8) morphs live in ModelApplyMorphs instead.
void AccumPmxVertexMorph(mdl::PmxVertex* vertices,
                         const mdl::MorphRecord& morph, float value,
                         float groupWeight) {
    if (morph.type == 1) {
        const int count = morph.offsetCount;
        for (int k = 0; k < count; ++k) {
            const mdl::PmdVertexMorphEntry& entry = morph.vertexEntries[k];
            mdl::PmxVertex& vertex = vertices[entry.vertexIndex];
            vertex.position[0] += entry.offset[0] * value * groupWeight;
            vertex.position[1] += entry.offset[1] * value * groupWeight;
            vertex.position[2] += entry.offset[2] * value * groupWeight;
        }
        return;
    }
    if (morph.type < 3 || morph.type > 7)
        return;
    const int family = morph.type - 3;
    const int count = morph.uvCounts[family];
    const mdl::PmxUvMorphEntry* entries = morph.uvEntries[family];
    for (int k = 0; k < count; ++k) {
        const mdl::PmxUvMorphEntry& entry = entries[k];
        mdl::PmxVertex& vertex = vertices[entry.vertexIndex];
        if (family == 0) {
            vertex.uv[0] += entry.offset[0] * value * groupWeight;
            vertex.uv[1] += entry.offset[1] * value * groupWeight;
        } else {
            for (int component = 0; component < 4; ++component)
                vertex.additionalUvByComponent[component][family - 1] +=
                    entry.offset[component] * value * groupWeight;
        }
    }
}

// RVA 0xC1415..0xC17EB plus 0xC17FE..0xC2670: the PMX twin of
// the PMD morph pass. Morph zero's aggregated position table and the five
// flattened UV base tables restore every morph-targeted component first,
// then each morph (including morph zero - unlike PMD) accumulates its
// weighted offsets; group morphs scale the vertex/UV morphs they reference.
void ApplyPmxVertexMorphs(unsigned char* model) {
    mdl::ModelRecord& record = *mdl::Mdl(model);
    mdl::MorphRecord* morphs = mdl::Morphs(model);
    mdl::PmxVertex* vertices = record.pmxVertices;
    if (morphs == nullptr || vertices == nullptr)
        return;

    const mdl::PmdVertexMorphEntry* base = mdl::BaseVertexMorphTable(model);
    const std::uint32_t baseCount = mdl::BaseVertexMorphCount(model);
    if (base != nullptr) {
        for (std::uint32_t i = 0; i < baseCount; ++i) {
            mdl::PmxVertex& vertex = vertices[base[i].vertexIndex];
            vertex.position[0] = base[i].offset[0];
            vertex.position[1] = base[i].offset[1];
            vertex.position[2] = base[i].offset[2];
        }
    }

    for (int family = 0; family < 5; ++family) {
        const mdl::PmxUvMorphEntry* entries =
            mdl::UvMorphTables(model).byFamily[family];
        const std::int32_t count = mdl::UvMorphCounts(model).byFamily[family];
        if (entries == nullptr || count <= 0)
            continue;
        for (int i = 0; i < count; ++i) {
            mdl::PmxVertex& vertex = vertices[entries[i].vertexIndex];
            if (family == 0) {
                vertex.uv[0] = entries[i].offset[0];
                vertex.uv[1] = entries[i].offset[1];
            } else {
                for (int component = 0; component < 4; ++component)
                    vertex.additionalUvByComponent[component][family - 1] =
                        entries[i].offset[component];
            }
        }
    }

    const int morphCount = record.morphCount;
    for (int i = 0; i < morphCount; ++i) {
        const mdl::MorphRecord& morph = morphs[i];
        const float weight = morph.value;
        if (weight == 0.0f)
            continue;
        if (morph.type != 0) {
            AccumPmxVertexMorph(vertices, morph, weight, 1.0f);
            continue;
        }
        const int groupCount = morph.groupCount;
        for (int k = 0; k < groupCount; ++k) {
            const mdl::PmxGroupMorphEntry& ref = morph.groupEntries[k];
            AccumPmxVertexMorph(vertices, morphs[ref.morphIndex], weight,
                                ref.weight);
        }
    }
}

// 0x4B0C74..0x4B0CE9 and 0x4B1C9C..0x4B1D8B.  PMD morph zero is
// the base table; all following morphs contain offsets indexed through it.
void ApplyPmdVertexMorphs(unsigned char* model) {
    mdl::ModelRecord& record = *mdl::Mdl(model);
    mdl::PmdVertex* vertices = record.rawVertices;
    const mdl::PmdVertexMorphEntry* base = mdl::BaseVertexMorphTable(model);
    const std::uint32_t baseCount = mdl::BaseVertexMorphCount(model);
    if (vertices == nullptr || base == nullptr)
        return;

    for (std::uint32_t i = 0; i < baseCount; ++i) {
        const mdl::PmdVertexMorphEntry& entry = base[i];
        mdl::PmdVertex& vertex = vertices[entry.vertexIndex];
        vertex.position[0] = entry.offset[0];
        vertex.position[1] = entry.offset[1];
        vertex.position[2] = entry.offset[2];
    }

    mikudancestudio::mdl::MorphRecord* morphs = mikudancestudio::mdl::Morphs(model);
    const int morphCount = record.morphCount;
    if (morphs == nullptr || morphCount <= 1)
        return;
    for (int i = 1; i < morphCount; ++i) {
        mikudancestudio::mdl::MorphRecord* morph = &morphs[i];
        const float weight = morph->value;
        if (weight == 0.0f)
            continue;
        auto* entries = morph->vertexEntries;
        const std::uint32_t count = morph->offsetCount;
        if (entries == nullptr)
            continue;
        for (std::uint32_t k = 0; k < count; ++k) {
            const mdl::PmdVertexMorphEntry& entry = entries[k];
            mdl::PmdVertex& vertex = vertices[entry.vertexIndex];
            vertex.position[0] += entry.offset[0] * weight;
            vertex.position[1] += entry.offset[1] * weight;
            vertex.position[2] += entry.offset[2] * weight;
        }
    }
}

float PmdEdgeDistance(MMDApp* app, unsigned char* model,
                      const float frameWorld[16]) {
    const mdl::ModelRecord& record = *mdl::Mdl(model);
    auto* bones = mdl::Bones(model);
    const int boneCount = record.boneCount;
    float modelPoint[3]{};
    if (bones != nullptr && boneCount > 0) {
        mikudancestudio::mdl::BoneRecord* bone = &bones[(boneCount > 1 ? 1 : 0)];
        const float bind[3] = {bone->position[0], bone->position[1],
                               bone->position[2]};
        TransformPosition(modelPoint, bind,
                          bone->matInit);
    }
    float worldPoint[3];
    TransformPosition(worldPoint, modelPoint, frameWorld);
    const float* camera = app->CameraPosition();
    // x64 0x7FF7CB4E29C2..0x7FF7CB4E2A56 is single-precision throughout:
    // subss deltas, mulss squares, call sqrtf, then distance *
    // 4.5e-05f * (fov * 0.6f + 1.0f) * edgeScale (the two constants are
    // the .rdata floats 0x383CBE62 / 0x3F19999A - no double bit patterns).
    const float dx = worldPoint[0] - camera[0];
    const float dy = worldPoint[1] - camera[1];
    const float dz = worldPoint[2] - camera[2];
    const float distance = std::sqrt(dx * dx + dy * dy + dz * dz);
    return distance * 0.00004500000068219379f *
           (app->CameraFov() * 0.6000000238418579f + 1.0f) *
           record.edgeScale;
}

void SkinPmd(unsigned char* model, float edgeDistance,
             mdl::EdgeVertex* edgeVertices,
             mdl::SkinnedVertexBase* mainVertices) {
    const mdl::ModelRecord& record = *mdl::Mdl(model);
    const std::uint32_t vertexCount = record.vertexCount;
    auto* vertices = record.rawVertices;
    auto* bones = mdl::Bones(model);
    const int boneCount = record.boneCount;
    if (vertices == nullptr || bones == nullptr)
        return;

    // The original x64 worker (RVA 0x11F3A0) partitions PMD vertices with
    // the same default static OpenMP schedule as PMX. Inputs are read-only;
    // each iteration owns its main/edge output pair. MSVC OpenMP requires
    // a signed induction variable. Keep the unverified x86 schedule intact.
#if defined(_M_X64)
#pragma omp parallel for
    for (int i = 0; i < static_cast<int>(vertexCount); ++i) {
#else
    for (std::uint32_t i = 0; i < vertexCount; ++i) {
#endif
        const mdl::PmdVertex& src = vertices[i];
        mdl::SkinnedVertexBase& main = mainVertices[i];
        mdl::EdgeVertex& edge = edgeVertices[i];
        const float* position = src.position;
        const float* normal = src.normal;
        const int bone0 = src.bone[0];
        const int bone1 = src.bone[1];
        const float weight0 = static_cast<float>(
            src.weightPercent) * 0.009999999776482582f;
        const float* matrix0 = bone0 >= 0 && bone0 < boneCount
            ? bones[bone0].matInit
            : nullptr;
        const float* matrix1 = bone1 >= 0 && bone1 < boneCount
            ? bones[bone1].matInit
            : nullptr;
        static const float zero[16]{};
        if (matrix0 == nullptr) matrix0 = zero;
        if (matrix1 == nullptr) matrix1 = zero;

        for (int component = 0; component < 3; ++component) {
            main.position[component] = BlendTransformComponent(
                position, matrix0, matrix1, component, weight0, true);
            main.normal[component] = BlendTransformComponent(
                normal, matrix0, matrix1, component, weight0, false);
        }
        main.uv[0] = src.uv[0];
        main.uv[1] = src.uv[1];

        const float amount = src.edgeDisabled != 0
            ? -0.004999999888241291f
            : edgeDistance;
        edge.position[0] = EdgeComponent(main.position[0], main.normal[0], amount);
        edge.position[1] = EdgeComponent(main.position[1], main.normal[1], amount);
        edge.position[2] = EdgeComponent(main.position[2], main.normal[2], amount);
        // edge.diffuse is the load-time 0xFF000000 and is intentionally retained.
    }
}

// The x64 stride workers share one per-vertex shape: dispatch on the weight
// type (QDEF and any unknown type leave position/normal untouched, only the
// common tail runs), then copy UVs, pack the edge color and expand the
// outline. The vertex marker at +172 picks between the material-driven
// expansion normal * frame scale * vertex scale * material size and the
// fixed 0.005 shrink.
template <std::size_t AdditionalUvCount>
void SkinPmx(unsigned char* model, float edgeDistance, bool opaqueEdge,
             mdl::EdgeVertex* edgeVertices,
             mdl::SkinnedVertex<AdditionalUvCount>* mainVertices) {
    const mdl::ModelRecord& record = *mdl::Mdl(model);
    const std::uint32_t vertexCount = record.vertexCount;
    const mdl::PmxVertex* vertices = record.pmxVertices;
    const mdl::BoneRecord* bones = mdl::Bones(model);
    const mdl::ModelMaterialRecord* materials = mdl::Materials(model);
    const int boneCount = static_cast<int>(record.boneCount);
    d3dx::Api& d3dxApi = d3dx::Get();
    if (vertices == nullptr || bones == nullptr || materials == nullptr)
        return;

    // x64 forks the stride workers through VCOMP90 (_vcomp_for_static_simple_
    // init/end): the per-vertex loop is the original's default-schedule
    // parallel region. MSVC /openmp (OpenMP 2.0) only accepts signed loop
    // indexes, so this is one signed index where the port kept unsigned.
#pragma omp parallel for
    for (int i = 0; i < static_cast<int>(vertexCount); ++i) {
        const mdl::PmxVertex& source = vertices[i];
        mdl::SkinnedVertex<AdditionalUvCount>& main = mainVertices[i];
        mdl::EdgeVertex& edge = edgeVertices[i];
        switch (source.weightType) {
        case mdl::PmxWeightType::bdef2:
            SkinPmxBdef2(source, bones, boneCount, main.base);
            break;
        case mdl::PmxWeightType::bdef1:
            SkinPmxBdef1(source, bones, boneCount, main.base);
            break;
        case mdl::PmxWeightType::bdef4:
            SkinPmxBdef4(source, bones, boneCount, main.base);
            break;
        case mdl::PmxWeightType::sdef:
            SkinPmxSdef(d3dxApi, source, bones, boneCount, main.base);
            break;
        default:
            break;
        }
        CopyPmxAdditionalUvs(source, main);

        const mdl::ModelMaterialRecord& material =
            materials[source.materialIndex];
        edge.diffuse = PackPmxEdgeColor(material, opaqueEdge);
        if (source.hasPmxEdgeData == 0) {
            for (int component = 0; component < 3; ++component)
                edge.position[component] =
                    main.base.position[component] -
                    main.base.normal[component] * 0.004999999888241291f;
        } else {
            for (int component = 0; component < 3; ++component) {
                float amount = edgeDistance * main.base.normal[component];
                amount *= source.edgeScale;
                amount *= material.edgeSize;
                edge.position[component] =
                    main.base.position[component] + amount;
            }
        }
    }
}

// x64 RVA 0xC2A5F..0xC2CEC: the PMX branch locks the main buffer with
// 32 + 16 * additionalUvCount bytes per vertex and the edge buffer with 16,
// then dispatches the stride worker selected by the additional-UV count.
// A count above four matches no worker and the original leaves both
// buffers untouched that frame. The frame edge scale is the same
// camera-distance value the PMD path uses (x64 computes it once for both).
void UpdatePmxModelVertexBuffers(MMDApp* app, unsigned char* model,
                                 const float frameWorld[16]) {
    mdl::ModelRecord& state = *mdl::Mdl(model);
    ApplyPmxVertexMorphs(model);

    const unsigned int additionalUv = state.pmxAdditionalUvCount;
    if (additionalUv > 4)
        return;
    auto* mainVb = mdl::ResourceAs<IDirect3DVertexBuffer9>(
        state.vertexBuffer);
    auto* edgeVb = mdl::ResourceAs<IDirect3DVertexBuffer9>(
        state.vertexBuffer2);
    if (mainVb == nullptr || edgeVb == nullptr)
        return;
    d3dx::Api& d3dxApi = d3dx::Get();

    const UINT count = state.vertexCount;
    const UINT stride = 32 + 16 * additionalUv;
    mdl::SkinnedVertexBase* mainVertices = nullptr;
    mdl::EdgeVertex* edgeVertices = nullptr;
    if (FAILED(mainVb->Lock(0, stride * count,
                            reinterpret_cast<void**>(&mainVertices), 0)))
        return;
    if (FAILED(edgeVb->Lock(0, 16 * count,
                            reinterpret_cast<void**>(&edgeVertices), 0))) {
        mainVb->Unlock();
        return;
    }

    const float edgeDistance = PmdEdgeDistance(app, model, frameWorld);
    const bool opaqueEdge = app->state.selfShadowEnabled != 0;
    switch (additionalUv) {
    case 0:
        SkinPmx<0>(model, edgeDistance, opaqueEdge, edgeVertices,
                   reinterpret_cast<mdl::SkinnedVertex<0>*>(mainVertices));
        break;
    case 1:
        SkinPmx<1>(model, edgeDistance, opaqueEdge, edgeVertices,
                   reinterpret_cast<mdl::SkinnedVertex<1>*>(mainVertices));
        break;
    case 2:
        SkinPmx<2>(model, edgeDistance, opaqueEdge, edgeVertices,
                   reinterpret_cast<mdl::SkinnedVertex<2>*>(mainVertices));
        break;
    case 3:
        SkinPmx<3>(model, edgeDistance, opaqueEdge, edgeVertices,
                   reinterpret_cast<mdl::SkinnedVertex<3>*>(mainVertices));
        break;
    default:
        SkinPmx<4>(model, edgeDistance, opaqueEdge, edgeVertices,
                   reinterpret_cast<mdl::SkinnedVertex<4>*>(mainVertices));
        break;
    }
    DumpModelVertexBuffers(app, model, mainVertices, edgeVertices, count,
                           stride);
    mainVb->Unlock();
    edgeVb->Unlock();
}

}  // namespace

void UpdateModelVertexBuffers(MMDApp* app, unsigned char* model,
                              const float frameWorld[16]) {
    if (app == nullptr || model == nullptr || frameWorld == nullptr)
        return;
    mdl::ModelRecord& state = *mdl::Mdl(model);
    if (state.loadComplete == 0 || state.vertexCount == 0)
        return;

    // The PMX twin (x64 RVA 0xC13C0) runs its own restore+morph pass and
    // dispatches the additional-UV stride workers; PMD uses its own
    // morph pass and the matching per-vertex worker below.
    if (state.physicsMode == 2) {
        UpdatePmxModelVertexBuffers(app, model, frameWorld);
        return;
    }

    ApplyPmdVertexMorphs(model);
    auto* mainVb = mdl::ResourceAs<IDirect3DVertexBuffer9>(
        state.vertexBuffer);
    auto* edgeVb = mdl::ResourceAs<IDirect3DVertexBuffer9>(
        state.vertexBuffer2);
    if (mainVb == nullptr || edgeVb == nullptr)
        return;

    const UINT count = state.vertexCount;
    mdl::SkinnedVertexBase* mainVertices = nullptr;
    mdl::EdgeVertex* edgeVertices = nullptr;
    if (FAILED(mainVb->Lock(0, 32 * count,
                            reinterpret_cast<void**>(&mainVertices), 0)))
        return;
    if (FAILED(edgeVb->Lock(0, 16 * count,
                            reinterpret_cast<void**>(&edgeVertices), 0))) {
        mainVb->Unlock();
        return;
    }

    SkinPmd(model, PmdEdgeDistance(app, model, frameWorld), edgeVertices,
            mainVertices);
    DumpModelVertexBuffers(app, model, mainVertices, edgeVertices, count, 32);
    mainVb->Unlock();
    edgeVb->Unlock();
}

}  // namespace mikudancestudio
