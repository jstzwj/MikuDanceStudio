// MMD plugin queries (original x86 exports 4C3590..4C3A00;
// x64 export RVAs 0xDB5B0..0xDBBE0).
// Public C signatures retain the plugin ABI. Internally, model queries use
// ModelRecord and its concrete bone/morph/material tables.
//
// A query index counts occupied slots in slot order, independently of the
// object's draw order. Combined draw order places accessories before/after
// models according to accessoryRenderSplitOrder. The active object remains
// an opaque identity until it matches a typed model or accessory slot.
// Material output is the fixed 17-float D3DMATERIAL9 prefix, not the complete
// runtime material. Preserve the reference's different material bounds:
// unsigned <= count for models, signed < count for accessories.
#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <Windows.h>

#include <cmath>
#include <cstring>
#include <cstdint>

#include "mikudancestudio/charset_conv.hpp"
#include "mikudancestudio/d3dx_dyn.hpp"
#include "mikudancestudio/mmd_app.hpp"
#include "mikudancestudio/model.hpp"

// VA 0x0042A110 - model-slot count; real body in src/app/late_ports.cpp.
namespace mikudancestudio {
int GetPmdNum(MMDApp* app);
} // namespace mikudancestudio

namespace {

using mikudancestudio::MMDApp;
namespace mdl = mikudancestudio::mdl;

// ---- slot resolution (shared shape of the 0x42A1xx family) ----------------
// Occupied-slot counter: index counts only non-null slots, matching the
// original `cmp (%esi); je skip / inc edi; cmp edi, arg` loops.

mdl::ModelRecord* ModelByIndex(MMDApp* app, int index) { // 0x780/x64 0xBE8
    int occupied = -1;
    for (int i = 0; i < mikudancestudio::kModelSlotCount; ++i) {
        mdl::ModelRecord* slot = app->ModelSlot(i);
        if (slot != nullptr && ++occupied == index)
            return slot;
    }
    return nullptr;
}

mikudancestudio::mdl::AccessoryRecord* AcsByIndex(MMDApp* app, int index) {
    int occupied = -1;
    for (int i = 0; i < 0xFF; ++i) {
        mikudancestudio::mdl::AccessoryRecord* slot = app->AccessorySlot(i);
        if (slot != nullptr && ++occupied == index)
            return slot;
    }
    return nullptr;
}

// VA 0x0042A5F0 - count occupied accessory slots (51 x 5 unrolled).
int CountAcs(MMDApp* app) {
    int count = 0;
    for (int i = 0; i < 0xFF; ++i) {
        if (app->AccessorySlot(i) != nullptr)
            ++count;
    }
    return count;
}

// Model/accessory fields are reached through the typed records
// and their members, never through
// numeric offsets - the layouts differ between the x86 and x64 ABIs.

// Imported D3DX operations used by the matrix queries.
void Identity(mikudancestudio::d3dx::D3DXMATRIXF* m) {
    std::memset(m, 0, sizeof(*m));
    m->m[0][0] = m->m[1][1] = m->m[2][2] = m->m[3][3] = 1.0f;
}

struct D3dx {
    mikudancestudio::d3dx::Api& api;
    D3dx() : api(mikudancestudio::d3dx::Get()) {}
    void Translation(mikudancestudio::d3dx::D3DXMATRIXF* m, float x, float y, float z) {
        api.translation(m, x, y, z);
    }
    void Multiply(mikudancestudio::d3dx::D3DXMATRIXF* o,
                  const mikudancestudio::d3dx::D3DXMATRIXF* a,
                  const mikudancestudio::d3dx::D3DXMATRIXF* b) {
        api.multiply(o, a, b);
    }
    void Rot(float angle, int axis, mikudancestudio::d3dx::D3DXMATRIXF* m) {
        (axis == 0 ? api.rotX : axis == 1 ? api.rotY : api.rotZ)(m, angle);
    }
    void Scaling(mikudancestudio::d3dx::D3DXMATRIXF* m, float x, float y, float z) {
        api.scaling(m, x, y, z);
    }
};

// ===========================================================================
// VA 0x004C5D00 - accessory local-to-world matrix (callee of 0x42A720)
// ===========================================================================
// dst = Scaling(Si*10) * Rz * Rx * Ry * T(local xyz) * T(parent xyz) * bone
// (each step is D3DXMatrixMultiply(dst, dst, step); the original builds the
// result directly into the caller's buffer, zero-filled first).
float* AccWorldMatrix(const mikudancestudio::mdl::AccessoryRecord& acc, float* dst16, float px,
                      float py, float pz, const float* boneMat16) {
    auto* dst = reinterpret_cast<mikudancestudio::d3dx::D3DXMATRIXF*>(dst16);
    auto* bone = reinterpret_cast<const mikudancestudio::d3dx::D3DXMATRIXF*>(boneMat16);
    mikudancestudio::d3dx::D3DXMATRIXF step;
    D3dx d3;

    const float si10 = static_cast<float>(acc.scale * 10.0);
    d3.Scaling(dst, si10, si10, si10); // dbl 0x52C170 = 10.0
    d3.Rot(acc.rotation[2], 2, &step); // Rz
    d3.Multiply(dst, dst, &step);
    d3.Rot(acc.rotation[0], 0, &step); // Rx
    d3.Multiply(dst, dst, &step);
    d3.Rot(acc.rotation[1], 1, &step); // Ry
    d3.Multiply(dst, dst, &step);
    d3.Translation(&step, acc.position[0], acc.position[1], acc.position[2]);
    d3.Multiply(dst, dst, &step);
    d3.Translation(&step, px, py, pz);
    d3.Multiply(dst, dst, &step);
    d3.Multiply(dst, dst, bone);
    return dst16;
}

} // namespace

extern "C" {

// --- model (PMD) queries ---------------------------------------------------
// 0x4C35A0 -> 0x42A110
__declspec(dllexport) int ExpGetPmdNum() {
    return mikudancestudio::GetPmdNum(mikudancestudio::g_Block);
}

// 0x4C35B0 -> 0x42A160: wide model path -> SJIS into app+0xA02B7.
__declspec(dllexport) char* ExpGetPmdFilename(int index) {
    MMDApp* app = mikudancestudio::g_Block;
    mdl::ModelRecord* model = ModelByIndex(app, index);
    if (model == nullptr)
        return nullptr; // original: xor eax, eax
    char* out = reinterpret_cast<char*>(app->state.sjisOut);
    mikudancestudio::WideToSjis(app->Renderer(), out, model->path, 0x100);
    return out;
}

// Internal direct access (not exported): the model's original wide-character path (model+0x24BC wchar_t[256],
// the path used by D3DXLoadMeshFromXW). Original MMHack's D3DXLoadMeshFromXW hook
// [0x1800043b0] registers that wide path unchanged in ObjData+0x30 (sub_18000f150 directly copies
// the wide-string argument); sub_18000f340 reads it and passes it to FUN_18000e020 (wfopen_s uses
// wide characters throughout, without an ANSI round trip). Embedded ObjData.modelName must use this accessor:
// ExpGetPmdFilename's wide->SJIS conversion loses non-SJIS characters; converting back through CP_ACP
// corrupts material-table/toon-file parsing.
const wchar_t* MmdModelPathW(int index) {
    mdl::ModelRecord* model = ModelByIndex(mikudancestudio::g_Block, index);
    return (model != nullptr) ? model->path : nullptr;
}

// 0x4C35D0 -> 0x441000: order byte + min(AcsNum, PreAcsNum).
__declspec(dllexport) int ExpGetPmdOrder(int index) {
    MMDApp* app = mikudancestudio::g_Block;
    int base = CountAcs(app);
    const int pre = app->state.accessoryRenderSplitOrder;
    if (pre < base)
        base = pre;
    int occupied = -1;
    for (int i = 0; i < mikudancestudio::kModelSlotCount; ++i) {
        mdl::ModelRecord* slot = app->ModelSlot(i);
        if (slot != nullptr && ++occupied == index)
            return slot->comboSelIndex + base;
    }
    return 0;
}

// 0x4C35F0 -> 0x42A1C0 -> 0x4A48D0: dword model+0x1C.
__declspec(dllexport) int ExpGetPmdMatNum(int index) {
    mdl::ModelRecord* model = ModelByIndex(mikudancestudio::g_Block, index);
    if (model == nullptr)
        return 0;
    return static_cast<int>(model->materialCount);
}

// 0x4C3610 -> 0x42A210 -> 0x4A48E0: copy 17 material floats into `out`.
// Out-of-range zero-fills (0x4A48EE memset path); the original's inclusive
// `jbe` also admits mat == count - one record past the end - preserved here.
__declspec(dllexport) float* ExpGetPmdMaterial(float* out, int index, int mat) {
    std::memset(out, 0, 0x44);
    mdl::ModelRecord* model = ModelByIndex(mikudancestudio::g_Block, index);
    if (model == nullptr)
        return out;
    const std::uint32_t count = model->materialCount;
    if (static_cast<std::uint32_t>(mat) <= count) {
        const mikudancestudio::mdl::ModelMaterialRecord& rec =
            model->materials[static_cast<std::size_t>(mat)];
        std::memcpy(out, &rec, 0x44);
    }
    return out;
}

// 0x4C3650 -> 0x42A290: dword model+0x2D84.
__declspec(dllexport) int ExpGetPmdBoneNum(int index) {
    mdl::ModelRecord* model = ModelByIndex(mikudancestudio::g_Block, index);
    if (model == nullptr)
        return 0;
    return static_cast<int>(model->boneCount);
}

// 0x4C3670 -> 0x42A2E0: SJIS bone name = bone record base (name at +0).
__declspec(dllexport) char* ExpGetPmdBoneName(int index, int bone) {
    mdl::ModelRecord* model = ModelByIndex(mikudancestudio::g_Block, index);
    if (model == nullptr)
        return nullptr;
    return model->boneTable[bone].name;
}

// 0x4C3690 -> 0x42A330: out = T(bone position) * bone world matrix;
// identity on a failed slot lookup.
__declspec(dllexport) float* ExpGetPmdBoneWorldMat(float* out, int index, int bone) {
    mikudancestudio::d3dx::D3DXMATRIXF m;
    Identity(&m);
    mdl::ModelRecord* model = ModelByIndex(mikudancestudio::g_Block, index);
    if (model != nullptr) {
        mikudancestudio::mdl::BoneRecord& boneRec = model->boneTable[bone];
        mikudancestudio::d3dx::D3DXMATRIXF t;
        D3dx d3;
        d3.Translation(&t, boneRec.position[0], boneRec.position[1], boneRec.position[2]);
        d3.Multiply(&m, &m, &t);
        d3.Multiply(&m, &m,
                    reinterpret_cast<const mikudancestudio::d3dx::D3DXMATRIXF*>(boneRec.matInit));
    }
    std::memcpy(out, &m, 0x40);
    return out;
}

// 0x4C36D0 -> 0x42A460: dword model+0x2D80.
__declspec(dllexport) int ExpGetPmdMorphNum(int index) {
    mdl::ModelRecord* model = ModelByIndex(mikudancestudio::g_Block, index);
    if (model == nullptr)
        return 0;
    return static_cast<int>(model->morphCount);
}

// 0x4C36F0 -> 0x42A4B0: SJIS morph name = morph record base (name at +0).
__declspec(dllexport) char* ExpGetPmdMorphName(int index, int morph) {
    mdl::ModelRecord* model = ModelByIndex(mikudancestudio::g_Block, index);
    if (model == nullptr)
        return nullptr;
    return model->morphs[morph].name;
}

// 0x4C3710 -> 0x42A500: morph value (float) at record +0x30.
__declspec(dllexport) float ExpGetPmdMorphValue(int index, int morph) {
    mdl::ModelRecord* model = ModelByIndex(mikudancestudio::g_Block, index);
    if (model == nullptr)
        return 0.0f;
    return model->morphs[morph].value;
}

// 0x4C3730 -> 0x42A560: display byte model+0x2D8D.
__declspec(dllexport) int ExpGetPmdDisp(int index) {
    mdl::ModelRecord* model = ModelByIndex(mikudancestudio::g_Block, index);
    if (model == nullptr)
        return 0;
    return static_cast<unsigned char>(model->loadComplete);
}

// 0x4C3750 -> 0x42A5B0: the model object pointer itself.
__declspec(dllexport) void* ExpGetPmdID(int index) {
    return ModelByIndex(mikudancestudio::g_Block, index);
}

// --- accessory (ACS/stage) queries -----------------------------------------
// 0x4C3770 -> 0x42A5F0.
__declspec(dllexport) int ExpGetAcsNum() {
    return CountAcs(mikudancestudio::g_Block);
}

// 0x4C3780 -> 0x42A640.
__declspec(dllexport) int ExpGetPreAcsNum() {
    return mikudancestudio::g_Block->state.accessoryRenderSplitOrder;
}

// 0x4C3790 -> 0x42A650: wide path (acc+0x29C) -> SJIS into app+0xA02B7.
__declspec(dllexport) char* ExpGetAcsFilename(int index) {
    MMDApp* app = mikudancestudio::g_Block;
    mikudancestudio::mdl::AccessoryRecord* acc = AcsByIndex(app, index);
    if (acc == nullptr)
        return nullptr;
    char* out = reinterpret_cast<char*>(app->state.sjisOut);
    mikudancestudio::WideToSjis(app->Renderer(), out, acc->sourcePath, 0x100);
    return out;
}

// Internal direct access (not exported): the accessory's original wide-character path (acc+0x29C wchar_t[256]); like
// MmdModelPathW, the original stores and uses the wide path directly; the embedded version avoids an SJIS round trip.
const wchar_t* MmdAcsPathW(int index) {
    mikudancestudio::mdl::AccessoryRecord* acc = AcsByIndex(mikudancestudio::g_Block, index);
    return (acc != nullptr) ? acc->sourcePath : nullptr;
}

// 0x4C37B0 -> 0x42A6B0: -(order+1) for accessories ordered before the
// stage block, order+1+PmdNum for those after it.
__declspec(dllexport) int ExpGetAcsOrder(int index) {
    MMDApp* app = mikudancestudio::g_Block;
    mikudancestudio::mdl::AccessoryRecord* acc = AcsByIndex(app, index);
    if (acc == nullptr)
        return 0;
    int order = acc->order + 1;
    if (order < app->state.accessoryRenderSplitOrder + 1)
        return -order;
    return order + mikudancestudio::GetPmdNum(app);
}

// 0x4C37D0 -> 0x42A720 + 0x4C5D00: out zero-filled; a parented accessory
// uses the parent bone position/matrix, otherwise T(0,0,0) * identity.
__declspec(dllexport) float* ExpGetAcsWorldMat(float* out, int index) {
    MMDApp* app = mikudancestudio::g_Block;
    std::memset(out, 0, 0x40);
    mikudancestudio::mdl::AccessoryRecord* acc = AcsByIndex(app, index);
    if (acc != nullptr) {
        const std::int32_t parentSlot = acc->parentModel;
        float px = 0.0f, py = 0.0f, pz = 0.0f;
        mikudancestudio::d3dx::D3DXMATRIXF bone;
        if (parentSlot != -1) {
            mdl::ModelRecord* model = app->ModelSlot(parentSlot);
            mikudancestudio::mdl::BoneRecord& boneRec = model->boneTable[acc->parentBone];
            px = boneRec.position[0];
            py = boneRec.position[1];
            pz = boneRec.position[2];
            std::memcpy(&bone, boneRec.matInit, sizeof(bone));
        } else {
            Identity(&bone);
        }
        AccWorldMatrix(*acc, out, px, py, pz, &bone.m[0][0]);
    }
    return out;
}

// 0x4C3800 -> 0x42A8F0 (float acc+0x214; 0 when the index never matches).
__declspec(dllexport) float ExpGetAcsX(int index) {
    const auto* acc = AcsByIndex(mikudancestudio::g_Block, index);
    return acc != nullptr ? acc->position[0] : 0.0f;
}
// 0x4C3820 -> 0x42A940 (acc+0x218).
__declspec(dllexport) float ExpGetAcsY(int index) {
    const auto* acc = AcsByIndex(mikudancestudio::g_Block, index);
    return acc != nullptr ? acc->position[1] : 0.0f;
}
// 0x4C3840 -> 0x42A990 (acc+0x21C).
__declspec(dllexport) float ExpGetAcsZ(int index) {
    const auto* acc = AcsByIndex(mikudancestudio::g_Block, index);
    return acc != nullptr ? acc->position[2] : 0.0f;
}
// 0x4C3860 -> 0x42A9E0 (acc+0x220).
__declspec(dllexport) float ExpGetAcsRx(int index) {
    const auto* acc = AcsByIndex(mikudancestudio::g_Block, index);
    return acc != nullptr ? acc->rotation[0] : 0.0f;
}
// 0x4C3880 -> 0x42AA30 (acc+0x224).
__declspec(dllexport) float ExpGetAcsRy(int index) {
    const auto* acc = AcsByIndex(mikudancestudio::g_Block, index);
    return acc != nullptr ? acc->rotation[1] : 0.0f;
}
// 0x4C38A0 -> 0x42AA80 (acc+0x228).
__declspec(dllexport) float ExpGetAcsRz(int index) {
    const auto* acc = AcsByIndex(mikudancestudio::g_Block, index);
    return acc != nullptr ? acc->rotation[2] : 0.0f;
}
// 0x4C38C0 -> 0x42AAD0 (acc+0x22C scaled by dbl 10.0 @ 0x52C170).
__declspec(dllexport) float ExpGetAcsSi(int index) {
    const auto* acc = AcsByIndex(mikudancestudio::g_Block, index);
    return acc != nullptr ? static_cast<float>(acc->scale * 10.0) : 0.0f;
}
// 0x4C38E0 -> 0x42AB20 (acc+0x4A0).
__declspec(dllexport) float ExpGetAcsTr(int index) {
    const auto* acc = AcsByIndex(mikudancestudio::g_Block, index);
    return acc != nullptr ? acc->opacity : 0.0f;
}

// 0x4C3900 -> 0x42AB70: display byte acc+0x210.
__declspec(dllexport) int ExpGetAcsDisp(int index) {
    const auto* acc = AcsByIndex(mikudancestudio::g_Block, index);
    if (acc == nullptr)
        return 0;
    return acc->visible;
}

// 0x4C3920 -> 0x42ABC0: the accessory object pointer itself.
__declspec(dllexport) void* ExpGetAcsID(int index) {
    return AcsByIndex(mikudancestudio::g_Block, index);
}

// 0x4C3940 -> 0x42AC00: dword acc+0x4A4.
__declspec(dllexport) int ExpGetAcsMatNum(int index) {
    const auto* acc = AcsByIndex(mikudancestudio::g_Block, index);
    if (acc == nullptr)
        return 0;
    return static_cast<int>(acc->materialCount);
}

// 0x4C3960 -> 0x42AC50 -> 0x4C5EE0: copy 17 material floats into `out`
// (68-byte records at acc+4); out-of-range zero-fills (strict `<`, unlike
// the model twin's inclusive `jbe`).
__declspec(dllexport) float* ExpGetAcsMaterial(float* out, int index, int mat) {
    std::memset(out, 0, 0x44);
    const auto* acc = AcsByIndex(mikudancestudio::g_Block, index);
    if (acc == nullptr)
        return out;
    // 0x4C5EE0: `cmp/jl` - SIGNED `<` (a negative index copies the
    // out-of-bounds record before the table, exactly like the original).
    if (mat < static_cast<std::int32_t>(acc->materialCount)) {
        const float* rec =
            static_cast<const float*>(acc->materials) + 17 * static_cast<std::size_t>(mat);
        std::memcpy(out, rec, 0x44);
    }
    return out;
}

// --- render state ----------------------------------------------------------
// 0x4C39A0 -> 0x42ACD0: encode app+0xA0268 (object being rendered) as the
// combined model+accessory order index (same encoding as ExpGetAcsOrder /
// ExpGetPmdOrder); 0 when null or unmatched.
__declspec(dllexport) int ExpGetCurrentObject() {
    MMDApp* app = mikudancestudio::g_Block;
    const void* cur = app->ActiveRenderObject();
    if (cur == nullptr)
        return 0;
    // accessory slots first
    for (int i = 0; i < 0xFF; ++i) {
        mikudancestudio::mdl::AccessoryRecord* slot = app->AccessorySlot(i);
        if (slot == cur) {
            int order = slot->order + 1;
            if (order >= app->state.accessoryRenderSplitOrder + 1)
                return order + mikudancestudio::GetPmdNum(app);
            return -order;
        }
    }
    // then model slots, offset by min(AcsNum, PreAcsNum)
    int base = CountAcs(app);
    const int pre = app->state.accessoryRenderSplitOrder;
    if (pre < base)
        base = pre;
    for (int i = 0; i < mikudancestudio::kModelSlotCount; ++i) {
        mdl::ModelRecord* slot = app->ModelSlot(i);
        if (slot == cur)
            return slot->comboSelIndex + base;
    }
    return 0;
}

// 0x4C39B0 -> 0x42AD70: current material index of the object being
// rendered (accessory currentMaterial / model currentMaterial, the render-state
// material cursor at +0x37C4/+0x3B6C); -1 when null or unmatched.
__declspec(dllexport) int ExpGetCurrentMaterial() {
    MMDApp* app = mikudancestudio::g_Block;
    const void* cur = app->ActiveRenderObject();
    if (cur == nullptr)
        return -1;
    for (int i = 0; i < 0xFF; ++i) {
        mikudancestudio::mdl::AccessoryRecord* slot = app->AccessorySlot(i);
        if (slot == cur)
            return slot->currentMaterial;
    }
    for (int i = 0; i < mikudancestudio::kModelSlotCount; ++i) {
        mdl::ModelRecord* slot = app->ModelSlot(i);
        if (slot == cur)
            return static_cast<std::int32_t>(slot->currentMaterial);
    }
    return -1;
}

// 0x4C39C0 -> 0x42ADE0.
__declspec(dllexport) int ExpGetCurrentTechnic() {
    return mikudancestudio::g_Block->state.activeRenderPass;
}

// 0x4C39D0 -> 0x42ADF0.
__declspec(dllexport) void ExpSetRenderRepeatCount(int count) {
    mikudancestudio::g_Block->state.renderPassCount = count;
}

// 0x4C39F0 -> 0x42AE00.
__declspec(dllexport) int ExpGetRenderRepeatCount() {
    return mikudancestudio::g_Block->state.renderPassCount;
}

// 0x4C3A00 -> 0x42AE10: English UI flag byte (app+0xA0B4C).
__declspec(dllexport) int ExpGetEnglishMode() {
    return mikudancestudio::g_Block->state.englishUI;
}

// 0x4C3590 -> 0x42A0D0: physics cursor (0x9E64C) while playing, else the
// current frame (0x980; negative values re-read as unsigned via the 2^32
// add) divided by 30.
__declspec(dllexport) float ExpGetFrameTime() {
    MMDApp* app = mikudancestudio::g_Block;
    if (app->state.playbackActive != 0)
        return app->state.playbackCursorSeconds;
    const std::int32_t frame = app->state.currentFrame;
    // fild / fadds flt_52B9F0 (2^32) when negative / fdivl dbl_52BA68 (30.0)
    return static_cast<float>(static_cast<double>(static_cast<std::uint32_t>(frame)) / 30.0);
}

} // extern "C"
