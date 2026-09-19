// mmhack_getters.cpp - the 22 exported state-query functions.
//
// In the original binary every getter is a tiny reader over MMHack's .data
// state block; the per-export evidence address is cited above each function.
#include "mmhack_state.h"
#include "mmhack_api.h"

// 宿主附件对象布局：GetAcsAttachedPmd 的借位读取面（+0x240/+0x244）。
#include "mikudancestudio/accessory_layout.hpp"

MmhState g_mmh;

extern "C" {

// --- ordinal 1 [0x1800012d0 GetAcsAttachedPmd] ------------------------------
BOOL __cdecl GetAcsAttachedPmd(unsigned long long accessory_id,
                               unsigned long long* model_id, int* bone_index)
{
    // 原版数据流：[0x1800012ea] sub_18000F2B0 对不在 objData 图中的 id 在
    // [0x18000f331] 直接 return a1——把 id 当宿主附件对象指针借位使用，随后
    // [0x1800012ef] mov ecx,[rax+240h] / [0x1800012f5] mov ebp,[rax+244h] 从
    // 该对象内存实时抽取附属模型索引（+0x240，-1 = 未附属，宿主侧即
    // AccessoryRecord::parentModel）与附属骨骼索引（+0x244 =
    // AccessoryRecord::parentBone）。原版 ObjData 自身从不写这两个字段
    // （MMHack.dll 全库无 +0x240 写入点，仅有本函数一处读取），借位读取是
    // 唯一真实数据源；本工程对象 id 同为宿主附件对象指针（ExpGetAcsID 返回
    // AccessoryRecord*，x64 侧不再 32 位截断），因此同样直接借用、每调用
    // 实时读取。
    // [0x1800012fd] 仅精确 -1 判未附属；非 -1 时 [0x180001306] ecx 保持
    // attachPmdIndex 调 ExpGetPmdID（槽枚举失败返回 0 同样落 FALSE），
    // [0x18000130c] mov eax,eax 零扩展后写 64 位模型 id 出参、[0x18000131e]
    // 写 32 位骨骼出参（原版两个出参无条件写，仅 FALSE 时模型 id 为 0），
    // [0x180001325] setnz 返回 (PmdID != 0)。
    int pmdIndex = -1;
    int boneIndex = 0;
    if (accessory_id != 0) {
        const mikudancestudio::mdl::AccessoryRecord* acs =
            mikudancestudio::mdl::Accessory(
                reinterpret_cast<const void*>(accessory_id));
        pmdIndex = acs->parentModel;    // +0x240
        boneIndex = acs->parentBone;    // +0x244
    }
    unsigned long long modelId = 0;
    if (pmdIndex != -1)
        modelId = (unsigned long long)(uintptr_t)ExpGetPmdID(pmdIndex);
    if (model_id)
        *model_id = modelId;
    if (bone_index)
        *bone_index = boneIndex;
    return modelId != 0 ? TRUE : FALSE;
}

// --- ordinal 2 [0x1800012c0 GetBlendMode; DAT_18006e7b8] --------------------
int __cdecl GetBlendMode(void)
{
    return g_mmh.blendMode;
}

// --- ordinal 3 [0x180001780 GetClearColor; DAT_18006ccc4] -------------------
D3DCOLOR __cdecl GetClearColor(void)
{
    return g_mmh.clearColor;
}

// --- ordinal 4 [0x180001050 GetCurrentDrawType; DAT_18006e798] --------------
int __cdecl GetCurrentDrawType(void)
{
    return g_mmh.currentDrawType;
}

// --- ordinal 5 [0x180001290 GetCurrentEffect; DAT_18006e7a0] ----------------
void* __cdecl GetCurrentEffect(void)
{
    return g_mmh.currentEffect;
}

// --- ordinal 6 [0x180001090 GetCurrentFrameTime; DAT_18006e79c] -------------
float __cdecl GetCurrentFrameTime(void)
{
    return g_mmh.frameTime;
}

// --- ordinal 7 [0x180001040 GetCurrentModelID; DAT_18006e868] ---------------
unsigned long long __cdecl GetCurrentModelID(void)
{
    return g_mmh.currentModelID;
}

// --- ordinal 8 [0x1800012b0 GetCurrentSubsetIndex; DAT_18006ccc0] -----------
int __cdecl GetCurrentSubsetIndex(void)
{
    return g_mmh.currentSubsetIndex;
}

// --- ordinal 9 [0x180001070 GetDrawnWindow; DAT_18006e7c0] ------------------
HWND __cdecl GetDrawnWindow(void)
{
    return g_mmh.presentWindow;
}

// --- ordinal 10 [0x180001330 GetLightViewProjMatrix] ------------------------
void __cdecl GetLightViewProjMatrix(D3DMATRIX* light_view,
                                    D3DMATRIX* light_projection,
                                    D3DMATRIX* light_view_projection)
{
    MmhComputeLightViewProjMatrix(light_view, light_projection, light_view_projection);
}

// --- ordinal 11 [0x180001060 GetMMDMainWindow; DAT_18006e7e0] ---------------
HWND __cdecl GetMMDMainWindow(void)
{
    return g_mmh.mainWindow;
}

// --- ordinal 12 [0x180001130 GetMaterialName] -------------------------------
const wchar_t* __cdecl GetMaterialName(unsigned long long object_id,
                                       unsigned long material_index, int name_kind)
{
    return MmhGetMaterialName(object_id, material_index, name_kind);
}

// --- ordinal 13 [0x1800010e0 GetSphereMapMode; bytes @0x18006e789/78a] ------
unsigned char __cdecl GetSphereMapMode(void)
{
    // 0 = none, 1 = multiply, 2 = add, 3 = sub-texture (cd2 techniques).
    if (g_mmh.sphereUsed == 0)
        return 0;
    if (g_mmh.subTexUsed != 0)
        return 3;
    return (unsigned char)(1 + (g_mmh.sphereAdd != 0 ? 1 : 0));
}

// --- ordinal 14 [0x1800010d0 GetSphereMapTexture; DAT_18006e7b0] ------------
IDirect3DBaseTexture9* __cdecl GetSphereMapTexture(void)
{
    return g_mmh.sphereTexture;
}

// --- ordinal 15 [0x1800010a0 GetTexture; DAT_18006e7a8] ---------------------
IDirect3DBaseTexture9* __cdecl GetTexture(void)
{
    return g_mmh.baseTexture;
}

// --- ordinal 16 [0x1800010b0 GetToonTexture] ---------------------------------
IDirect3DBaseTexture9* __cdecl GetToonTexture(void)
{
    // Only for PMD models (DAT_18006e870 == 1); texture comes from the
    // toon cache for the current material. [FUN_18000f810]
    if (g_mmh.currentObjectKind != 1)
        return nullptr;
    return MmhGetCurrentToonTexture(g_mmh.currentModelID);
}

// --- ordinal 17 [0x180001770 IsDebugMode; DAT_18006e7bc] --------------------
BOOL __cdecl IsDebugMode(void)
{
    return g_mmh.debugMode ? TRUE : FALSE;
}

// --- ordinal 18 [0x180001080 IsEditMode; DAT_18006e796 == 0] ----------------
BOOL __cdecl IsEditMode(void)
{
    return g_mmh.notEditMode == 0 ? TRUE : FALSE;
}

// --- ordinal 19 [0x1800012a0 IsEffectFileUsed; DAT_18006e795] ---------------
BOOL __cdecl IsEffectFileUsed(void)
{
    return g_mmh.effectFileUsed ? TRUE : FALSE;
}

// --- ordinal 20 [0x180001110 IsToonUsed] -------------------------------------
BOOL __cdecl IsToonUsed(void)
{
    // [0x180001122] 原版是纯 bool:
    //   (DAT_18006e798 == 1 || DAT_18006e798 == 2) && BYTE3(qword_18006e788) == 0
    // drawType 非 1/2 时恒 FALSE（原版没有 technic 高位数值返回路径）。
    return ((g_mmh.currentDrawType == 1 || g_mmh.currentDrawType == 2) &&
            g_mmh.diffuseFlag == 0)
               ? TRUE
               : FALSE;
}

// --- ordinal 21 [0x180001790 LoadedPMMFile] ----------------------------------
const wchar_t* __cdecl LoadedPMMFile(void)
{
    // One-shot: returns the last loaded .pmm path once, then clears the flag.
    static std::wstring ret;
    ret.clear();
    if (!g_mmh.loadedPmmFlag)
        return nullptr;
    ret = g_mmh.loadedPMMFile;
    g_mmh.loadedPMMFile.clear();
    g_mmh.loadedPmmFlag = false;
    return ret.c_str();
}

// --- ordinal 22 [0x180001840 SavedPMMFile] -----------------------------------
const wchar_t* __cdecl SavedPMMFile(void)
{
    // Pops the front of the save list; nullptr when empty.
    static std::wstring ret;
    ret.clear();
    if (g_mmh.savedPmmList.empty())
        return nullptr;
    ret = g_mmh.savedPmmList.front();
    g_mmh.savedPmmList.pop_front();
    return ret.c_str();
}

} // extern "C"
