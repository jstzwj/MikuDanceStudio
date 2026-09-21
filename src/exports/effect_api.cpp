// ===========================================================================
// VA 0x004C3590..0x004C3A00 - MMD effect/accessory plugin API (37 exports)
// ===========================================================================
// The original exports (RVA 0x00141328, ordinals 1..37) let MME-style
// effect DLLs query scene state.  Every export is a thin __cdecl thunk that
// loads `Block` (0x54593C) into ecx and forwards to a __thiscall query in
// the 0x42A0D0..0x42AE10 model-query family (verified on the live
// disassembly of 0x4C3590..0x4C3A10).
//
// GetPmdNum (0x42A110) is the canonical full port in src/app/late_ports.cpp;
// the former file-local CountModels twin was removed and its callers
// (ExpGetPmdNum + the two ordering helpers) now forward to GetPmdNum.
//
// Model-object layout used below (model = slot array app+0x780 [100]).
// Every model field goes through the mdl::ModelRecord members or the
// typed BoneRecord/MorphRecord/ModelMaterialRecord views, so one field
// name compiles to the correct offset on both ABIs.  x86 offset ->
// x64 twin (each verified on the x64 export twins in MikuMikuDance.exe):
//   materialCount   +0x001C -> +0x0038  (0x4A48D0)
//   materials       +0x0020 -> +0x0040  2292 B records, first 68 B = 17
//                                       floats (0x4A48E0; the 0x42AC50
//                                       accessory twin uses 68 B records)
//   path            +0x24BC -> +0x2548  wchar_t[256] (resolved model path)
//   boneTable       +0x26BC -> +0x2748  BoneRecord 604/624 B, name at +0
//   morphs          +0x26C4 -> +0x2758  MorphRecord 136/192 B, name at +0,
//                                       morph value (float) +0x30/+0x38
//   comboSelIndex   +0x2D7C -> +0x3108  combo/draw order byte (+1 based)
//   morphCount      +0x2D80 -> +0x310C  (dword)
//   boneCount       +0x2D84 -> +0x3110  (dword)
//   loadComplete    +0x2D8D -> +0x3119  display flag byte
//   toonShared      +0x37C4 -> +0x3B6C  current material index (render
//                                       state; see ExpGetCurrentMaterial)
//   +0x31C0  current-FPS float (0x41E950/0x41E980)
//   +0x33D8  enhance-model name block, 10 x char[100]
// Bone record sub-fields: matInit 4x4 at +0x34/+0x3C, position (x,y,z)
// floats at +0x134/+0x138/+0x13C (x64 +0x13C..+0x144).
//
// Accessory-object offsets (acc = slot array app+0x9DD70 [255]):
//   +0x0004  material records base pointer, 68 B (17 float) stride
//   +0x0210  display flag byte
//   +0x0214/+0x0218/+0x021C  X / Y / Z translation
//   +0x0220/+0x0224/+0x0228  Rx / Ry / Rz (radians)
//   +0x022C  size (Si; the export multiplies by dbl 10.0 @ 0x52C170)
//   +0x0230  parent model slot (-1 = none), +0x0234 parent bone index
//   +0x029C  wchar_t path[256]
//   +0x049D  order byte
//   +0x04A0  transparency (Tr)
//   +0x04A4  material count (dword), +0x04A8 current material index
//
// .rdata constants: dbl 0x52B8E0 = 100.0 (percent), dbl 0x52C170 = 10.0
// (Si scaling), dbl 0x52BA68 = 30.0 (frame rate), flt 0x52B9F0 =
// 4294967296.0 (negative-frame fixup).
// =========================================================================//
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
}  // namespace mikudancestudio

namespace {

using mikudancestudio::MMDApp;

// ---- slot resolution (shared shape of the 0x42A1xx family) ----------------
// Occupied-slot counter: index counts only non-null slots, matching the
// original `cmp (%esi); je skip / inc edi; cmp edi, arg` loops.

unsigned char* ModelByIndex(MMDApp* app, int index) {  // 0x780/x64 0xBE8
    int occupied = -1;
    for (int i = 0; i < mikudancestudio::kModelSlotCount; ++i) {
        unsigned char* slot = app->ModelSlot(i);
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
// (mdl::Mdl / mdl::Bones / mdl::Morphs / mdl::Materials), never through
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
    void Multiply(mikudancestudio::d3dx::D3DXMATRIXF* o, const mikudancestudio::d3dx::D3DXMATRIXF* a,
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
float* AccWorldMatrix(const mikudancestudio::mdl::AccessoryRecord& acc, float* dst16,
                      float px, float py, float pz,
                      const float* boneMat16) {
    auto* dst = reinterpret_cast<mikudancestudio::d3dx::D3DXMATRIXF*>(dst16);
    auto* bone = reinterpret_cast<const mikudancestudio::d3dx::D3DXMATRIXF*>(boneMat16);
    mikudancestudio::d3dx::D3DXMATRIXF step;
    D3dx d3;

    const float si10 = static_cast<float>(acc.scale * 10.0);
    d3.Scaling(dst, si10, si10, si10);            // dbl 0x52C170 = 10.0
    d3.Rot(acc.rotation[2], 2, &step);            // Rz
    d3.Multiply(dst, dst, &step);
    d3.Rot(acc.rotation[0], 0, &step);            // Rx
    d3.Multiply(dst, dst, &step);
    d3.Rot(acc.rotation[1], 1, &step);            // Ry
    d3.Multiply(dst, dst, &step);
    d3.Translation(&step, acc.position[0], acc.position[1], acc.position[2]);
    d3.Multiply(dst, dst, &step);
    d3.Translation(&step, px, py, pz);
    d3.Multiply(dst, dst, &step);
    d3.Multiply(dst, dst, bone);
    return dst16;
}

}  // namespace

extern "C" {

// --- model (PMD) queries ---------------------------------------------------
// 0x4C35A0 -> 0x42A110
__declspec(dllexport) int ExpGetPmdNum() { return mikudancestudio::GetPmdNum(mikudancestudio::g_Block); }

// 0x4C35B0 -> 0x42A160: wide model path -> SJIS into app+0xA02B7.
__declspec(dllexport) char* ExpGetPmdFilename(int index) {
    MMDApp* app = mikudancestudio::g_Block;
    unsigned char* model = ModelByIndex(app, index);
    if (model == nullptr)
        return nullptr;                      // original: xor eax, eax
    char* out = reinterpret_cast<char*>(app->state.sjisOut);
    mikudancestudio::WideToSjis(app->Renderer(), out, mikudancestudio::mdl::Mdl(model)->path, 0x100);
    return out;
}

// 内部直取（非导出）：模型的原始宽字符路径（model+0x24BC wchar_t[256]，即
// D3DXLoadMeshFromXW 加载所用路径）。原版 MMHack 由 D3DXLoadMeshFromXW hook
// [0x1800043b0] 把该宽路径原样登记进 ObjData+0x30（sub_18000f150 直接拷贝
// 宽字符串参数），sub_18000f340 读回后交 FUN_18000e020 解析（wfopen_s 全程
// 宽字符、无 ANSI 往返）。内置版 ObjData.modelName 须经本函数直取——
// ExpGetPmdFilename 的 Wide→SJIS 转换对非 SJIS 字符有损，再经 CP_ACP 回宽
// 会破坏材质表/toon 文件解析。
const wchar_t* MmdModelPathW(int index) {
    unsigned char* model = ModelByIndex(mikudancestudio::g_Block, index);
    return (model != nullptr) ? mikudancestudio::mdl::Mdl(model)->path : nullptr;
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
        unsigned char* slot = app->ModelSlot(i);
        if (slot != nullptr && ++occupied == index)
            return mikudancestudio::mdl::Mdl(slot)->comboSelIndex + base;
    }
    return 0;
}

// 0x4C35F0 -> 0x42A1C0 -> 0x4A48D0: dword model+0x1C.
__declspec(dllexport) int ExpGetPmdMatNum(int index) {
    unsigned char* model = ModelByIndex(mikudancestudio::g_Block, index);
    if (model == nullptr)
        return 0;
    return static_cast<int>(mikudancestudio::mdl::Mdl(model)->materialCount);
}

// 0x4C3610 -> 0x42A210 -> 0x4A48E0: copy 17 material floats into `out`.
// Out-of-range zero-fills (0x4A48EE memset path); the original's inclusive
// `jbe` also admits mat == count - one record past the end - preserved here.
__declspec(dllexport) float* ExpGetPmdMaterial(float* out, int index,
                                               int mat) {
    std::memset(out, 0, 0x44);
    unsigned char* model = ModelByIndex(mikudancestudio::g_Block, index);
    if (model == nullptr)
        return out;
    const std::uint32_t count =
        mikudancestudio::mdl::Mdl(model)->materialCount;
    if (static_cast<std::uint32_t>(mat) <= count) {
        const mikudancestudio::mdl::ModelMaterialRecord& rec =
            mikudancestudio::mdl::Materials(model)[
                static_cast<std::size_t>(mat)];
        std::memcpy(out, &rec, 0x44);
    }
    return out;
}

// 0x4C3650 -> 0x42A290: dword model+0x2D84.
__declspec(dllexport) int ExpGetPmdBoneNum(int index) {
    unsigned char* model = ModelByIndex(mikudancestudio::g_Block, index);
    if (model == nullptr)
        return 0;
    return static_cast<int>(mikudancestudio::mdl::Mdl(model)->boneCount);
}

// 0x4C3670 -> 0x42A2E0: SJIS bone name = bone record base (name at +0).
__declspec(dllexport) char* ExpGetPmdBoneName(int index, int bone) {
    unsigned char* model = ModelByIndex(mikudancestudio::g_Block, index);
    if (model == nullptr)
        return nullptr;
    return mikudancestudio::mdl::Bones(model)[bone].name;
}

// 0x4C3690 -> 0x42A330: out = T(bone position) * bone world matrix;
// identity on a failed slot lookup.
__declspec(dllexport) float* ExpGetPmdBoneWorldMat(float* out, int index,
                                                   int bone) {
    mikudancestudio::d3dx::D3DXMATRIXF m;
    Identity(&m);
    unsigned char* model = ModelByIndex(mikudancestudio::g_Block, index);
    if (model != nullptr) {
        mikudancestudio::mdl::BoneRecord& boneRec =
            mikudancestudio::mdl::Bones(model)[bone];
        mikudancestudio::d3dx::D3DXMATRIXF t;
        D3dx d3;
        d3.Translation(&t, boneRec.position[0], boneRec.position[1],
                       boneRec.position[2]);
        d3.Multiply(&m, &m, &t);
        d3.Multiply(&m, &m,
                    reinterpret_cast<const mikudancestudio::d3dx::D3DXMATRIXF*>(boneRec.matInit));
    }
    std::memcpy(out, &m, 0x40);
    return out;
}

// 0x4C36D0 -> 0x42A460: dword model+0x2D80.
__declspec(dllexport) int ExpGetPmdMorphNum(int index) {
    unsigned char* model = ModelByIndex(mikudancestudio::g_Block, index);
    if (model == nullptr)
        return 0;
    return static_cast<int>(mikudancestudio::mdl::Mdl(model)->morphCount);
}

// 0x4C36F0 -> 0x42A4B0: SJIS morph name = morph record base (name at +0).
__declspec(dllexport) char* ExpGetPmdMorphName(int index, int morph) {
    unsigned char* model = ModelByIndex(mikudancestudio::g_Block, index);
    if (model == nullptr)
        return nullptr;
    return mikudancestudio::mdl::Morphs(model)[morph].name;
}

// 0x4C3710 -> 0x42A500: morph value (float) at record +0x30.
__declspec(dllexport) float ExpGetPmdMorphValue(int index, int morph) {
    unsigned char* model = ModelByIndex(mikudancestudio::g_Block, index);
    if (model == nullptr)
        return 0.0f;
    return mikudancestudio::mdl::Morphs(model)[morph].value;
}

// 0x4C3730 -> 0x42A560: display byte model+0x2D8D.
__declspec(dllexport) int ExpGetPmdDisp(int index) {
    unsigned char* model = ModelByIndex(mikudancestudio::g_Block, index);
    if (model == nullptr)
        return 0;
    return static_cast<unsigned char>(mikudancestudio::mdl::Mdl(model)->loadComplete);
}

// 0x4C3750 -> 0x42A5B0: the model object pointer itself.
__declspec(dllexport) void* ExpGetPmdID(int index) {
    return ModelByIndex(mikudancestudio::g_Block, index);
}

// --- accessory (ACS/stage) queries -----------------------------------------
// 0x4C3770 -> 0x42A5F0.
__declspec(dllexport) int ExpGetAcsNum() { return CountAcs(mikudancestudio::g_Block); }

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

// 内部直取（非导出）：附件原始宽字符路径（acc+0x29C wchar_t[256]）；同
// MmdModelPathW——原版宽路径直存直用，内置版不经 SJIS 往返。
const wchar_t* MmdAcsPathW(int index) {
    mikudancestudio::mdl::AccessoryRecord* acc =
        AcsByIndex(mikudancestudio::g_Block, index);
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
            unsigned char* model = app->ModelSlot(parentSlot);
            mikudancestudio::mdl::BoneRecord& boneRec =
                mikudancestudio::mdl::Bones(model)[acc->parentBone];
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
    return acc != nullptr
               ? static_cast<float>(acc->scale * 10.0)
               : 0.0f;
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
__declspec(dllexport) float* ExpGetAcsMaterial(float* out, int index,
                                               int mat) {
    std::memset(out, 0, 0x44);
    const auto* acc = AcsByIndex(mikudancestudio::g_Block, index);
    if (acc == nullptr)
        return out;
    // 0x4C5EE0: `cmp/jl` - SIGNED `<` (a negative index copies the
    // out-of-bounds record before the table, exactly like the original).
    if (mat < static_cast<std::int32_t>(acc->materialCount)) {
        const float* rec = static_cast<const float*>(acc->materials) +
                           17 * static_cast<std::size_t>(mat);
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
    unsigned char* cur = static_cast<unsigned char*>(app->state.activeRenderObject);
    if (cur == nullptr)
        return 0;
    // accessory slots first
    for (int i = 0; i < 0xFF; ++i) {
        mikudancestudio::mdl::AccessoryRecord* slot = app->AccessorySlot(i);
        if (slot == reinterpret_cast<mikudancestudio::mdl::AccessoryRecord*>(cur)) {
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
        unsigned char* slot = app->ModelSlot(i);
        if (slot == cur)
            return mikudancestudio::mdl::Mdl(slot)->comboSelIndex + base;
    }
    return 0;
}

// 0x4C39B0 -> 0x42AD70: current material index of the object being
// rendered (accessory currentMaterial / model toonShared, the render-state
// material cursor at +0x37C4/+0x3B6C); -1 when null or unmatched.
__declspec(dllexport) int ExpGetCurrentMaterial() {
    MMDApp* app = mikudancestudio::g_Block;
    unsigned char* cur = static_cast<unsigned char*>(app->state.activeRenderObject);
    if (cur == nullptr)
        return -1;
    for (int i = 0; i < 0xFF; ++i) {
        mikudancestudio::mdl::AccessoryRecord* slot = app->AccessorySlot(i);
        if (slot == reinterpret_cast<mikudancestudio::mdl::AccessoryRecord*>(cur))
            return slot->currentMaterial;
    }
    for (int i = 0; i < mikudancestudio::kModelSlotCount; ++i) {
        unsigned char* slot = app->ModelSlot(i);
        if (slot == cur)
            return static_cast<std::int32_t>(
                mikudancestudio::mdl::Mdl(cur)->toonShared);
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
    return static_cast<float>(static_cast<double>(
                                  static_cast<std::uint32_t>(frame)) /
                              30.0);
}

}  // extern "C"
