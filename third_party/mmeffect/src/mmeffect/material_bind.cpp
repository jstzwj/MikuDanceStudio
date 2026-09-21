// material_bind.cpp - see material_bind.h
#include "material_bind.h"

#include <algorithm>
#include <cctype>
#include <cmath>
#include <cstring>
#include <memory>

#include "MMDExport.h"   // ExpGetPmdMaterial / ExpGetAcsMaterial
#include "mmhack_api.h"  // GetBlendMode / GetCurrentEffect

#include "effect_engine.h"
#include "emm_manager.h"
#include "mme_context.h"
#include "mme_globals.h"
#include "mme_log.h"       // MmeLogWrite (the name-table shape-error report)
#include "model_data.h"
#include "sas_exec.h"     // SasEffect/SasTechnique + the control list

namespace mme {

namespace {

// [0x18005ee60] FUN_18005ee60 - fixed-function edge-color read: requires the
// current FVF to be exactly 66 (D3DFVF_XYZ | D3DFVF_DIFFUSE), then walks
// index[start_index] of the bound index buffer and reads the DWORD at vertex
// offset +12 (the diffuse slot of that 16-byte XYZ+DIFFUSE layout). Ported
// with the standard device calls (GetIndices/GetStreamSource + Lock with
// D3DLOCK_READONLY|NOSYSLOCK = 0x1010, matching big-C 76042-76067). Returns
// 0 when the read is impossible.
unsigned int MmeReadVertexDiffuseColor(IDirect3DDevice9* device, int baseVertexIndex,
                                       int startIndex)
{
    if (device == nullptr) {
        return 0;
    }
    DWORD fvf = 0;
    if (FAILED(device->GetFVF(&fvf)) ||
        fvf != (D3DFVF_XYZ | D3DFVF_DIFFUSE)) {   // [big-C 76036-76038] FVF != 66
        return 0;
    }
    IDirect3DIndexBuffer9* indexBuffer = nullptr;
    if (FAILED(device->GetIndices(&indexBuffer)) || indexBuffer == nullptr) {
        return 0;
    }
    D3DINDEXBUFFER_DESC indexDesc;
    memset(&indexDesc, 0, sizeof(indexDesc));
    indexBuffer->GetDesc(&indexDesc);            // buffer slot 0x68 [big-C 76046]
    if (indexDesc.Format != D3DFMT_INDEX16 && indexDesc.Format != D3DFMT_INDEX32) {
        indexBuffer->Release();
        return 0;
    }
    unsigned int indexStride =
        (indexDesc.Format == D3DFMT_INDEX32) ? 4u : 2u;   // [big-C 76047-76050]

    unsigned int vertexIndex = 0;
    {
        void* locked = nullptr;
        // Lock(start_index * stride, stride, &ptr, 0x1010) [big-C 76051-76052]
        if (FAILED(indexBuffer->Lock(static_cast<UINT>(startIndex) * indexStride,
                                     indexStride, &locked, 0x1010)) || locked == nullptr) {
            indexBuffer->Release();
            return 0;
        }
        if (indexStride == 4u) {
            vertexIndex = *static_cast<unsigned int*>(locked);       // [big-C 76055]
        } else {
            vertexIndex = *static_cast<unsigned short*>(locked);     // [big-C 76058]
        }
        indexBuffer->Unlock();                                       // [big-C 76060]
        indexBuffer->Release();
        indexBuffer = nullptr;
    }

    IDirect3DVertexBuffer9* vertexBuffer = nullptr;
    UINT streamOffset = 0;
    UINT streamStride = 0;
    if (FAILED(device->GetStreamSource(0, &vertexBuffer, &streamOffset, &streamStride)) ||
        vertexBuffer == nullptr || streamStride == 0) {
        if (vertexBuffer != nullptr) {
            vertexBuffer->Release();
        }
        return 0;
    }
    unsigned int value = 0;
    {
        void* locked = nullptr;
        // Lock(stride*vertex + base_vertex_index + offset, stride, 0x1010)
        // [big-C 76063-76066; the original adds base_vertex_index raw].
        UINT offset = streamOffset + streamStride * vertexIndex +
                      static_cast<UINT>(baseVertexIndex);
        if (FAILED(vertexBuffer->Lock(offset, streamStride, &locked, 0x1010)) ||
            locked == nullptr) {
            vertexBuffer->Release();
            return 0;
        }
        // [big-C 76067] v12[3]: the DWORD at vertex offset +12, the diffuse
        // slot of the FVF-66 (XYZ + DIFFUSE) vertex layout.
        value = static_cast<unsigned int*>(locked)[3];
        vertexBuffer->Unlock();
        vertexBuffer->Release();
    }
    return value;
}

void MmeD3DColorToFloat4(float out[4], unsigned int color)
{
    // D3DCOLOR (ARGB) -> (r, g, b, a) * 1/255 [DAT_1800b5af0].
    const float scale = 0.003921569f;
    out[0] = static_cast<float>(color & 0xff) * scale;
    out[1] = static_cast<float>((color >> 8) & 0xff) * scale;
    out[2] = static_cast<float>((color >> 16) & 0xff) * scale;
    out[3] = static_cast<float>((color >> 24) & 0xff) * scale;
}

// The fixed-function material composition of MME_BindMaterialParameter's
// default branch. Writes the requested 16-byte slice of the composed
// D3DMATERIAL9 into `out` (float4) and the Power into `power`.
void MmeComposeMaterialColor(ModelData* model, const RenderSnapshot& snap, int semantic,
                             float out[4], float* power)
{
    D3DMATERIAL9 material;
    memset(&material, 0, sizeof(material));

    int drawType = snap.draw_type;                       // snapshot+0x2c (raw)
    if (drawType == 1 || drawType == 3) {
        // device slot 400 = GetMaterial [big-C 74844]
        IDirect3DDevice9* device = model != nullptr ? model->device() : nullptr;
        if (device != nullptr) {
            device->GetMaterial(&material);
        }
        // [big-C 147-153] semantic 0x18 (Diffuse) on a kind==0 (accessory)
        // object during the draw-type-1 pass scales floats 0..2 (r,g,b) by
        // DAT_1800b5c20 (= 10.0f); the alpha is NOT scaled. UNCERTAIN
        // rationale; ported verbatim.
        if (semantic == 0x18 && model != nullptr && model->kind() == 0 &&
            drawType == 1) {
            material.Diffuse.r *= 10.0f;
            material.Diffuse.g *= 10.0f;
            material.Diffuse.b *= 10.0f;
        }
    } else if (drawType == 4) {
        // [big-C 156-160] the edge pass zeroes the material (0x44 memset)
        // then sets the Diffuse.a and Ambient.a slots to 1.0f.
        memset(&material, 0, sizeof(material));
        material.Diffuse.a = 1.0f;
        material.Ambient.a = 1.0f;
    } else {
        // [big-C 161-188] ExpGetAcsMaterial / ExpGetPmdMaterial(hostIndex,
        // subsetIndex). hostIndex lives at ModelData+0xf0 (the passKey
        // scratch, filled by MmeRefreshObjectPlan), subset at snapshot+0x28.
        int hostIndex = model != nullptr ? model->passPlanScratch().passKey : -1;
        int subset = snap.subset_index;
        if (model != nullptr && model->kind() == 0) {
            material = ExpGetAcsMaterial(hostIndex, subset);
            // [big-C 190-198] the accessory (kind==0) ExpGet path also scales
            // the Diffuse r,g,b by 10.0f for semantic 0x18, with no draw-type
            // restriction (the PMD path never scales).
            if (semantic == 0x18) {
                material.Diffuse.r *= 10.0f;
                material.Diffuse.g *= 10.0f;
                material.Diffuse.b *= 10.0f;
            }
        } else {
            material = ExpGetPmdMaterial(hostIndex, subset);
        }
    }

    // [big-C 189-218] slice selection:
    //   0x18 -> +0x00 Diffuse, 0x19 -> +0x10 Ambient, 0x1A -> +0x30 Emissive,
    //   0x1B -> +0x20 Specular (the switch's third block is +0x30, fourth
    //   is +0x20 - swapped relative to the D3DMATERIAL9 field order).
    switch (semantic) {
    case 0x18:
        out[0] = material.Diffuse.r; out[1] = material.Diffuse.g;
        out[2] = material.Diffuse.b; out[3] = material.Diffuse.a;
        break;
    case 0x19:
        out[0] = material.Ambient.r; out[1] = material.Ambient.g;
        out[2] = material.Ambient.b; out[3] = material.Ambient.a;
        break;
    case 0x1A:
        out[0] = material.Emissive.r; out[1] = material.Emissive.g;
        out[2] = material.Emissive.b; out[3] = material.Emissive.a;
        break;
    case 0x1B:
        out[0] = material.Specular.r; out[1] = material.Specular.g;
        out[2] = material.Specular.b; out[3] = material.Specular.a;
        break;
    default:
        out[0] = out[1] = out[2] = out[3] = 0.0f;
        break;
    }
    *power = material.Power;
}

// [0x1800B2550] sub_180056C30 的 QI 目标：IID_IDirect3DTexture9（字节与
// sas_exec.cpp 的 kIidD3DTexture9 一致）。本地定义，链接不依赖 dxguid.lib。
static const GUID kToonIidD3DTexture9 = {
    0x85c31227, 0x3de5, 0x4f00, {0x9b, 0x3a, 0xf1, 0x1a, 0xc3, 0x8c, 0x18, 0xb5}};

// [sub_180056C30 0x180056c30] FUN_180056c30 - 固定函数路径 TOONCOLOR 的取色。
// 先按纹理指针查一张静态记忆化哈希表（原版 16 桶起步、节点内缓存 64 位
// 混合哈希的单链侵入式表：插入 sub_180061780、扩容 sub_1800622D0、
// atexit(sub_1800A3A10 -> sub_180062000) 时统一释放）。对表全局变量的全部
// 交叉引用仅这组函数：进程生命期内没有任何清除/失效路径，设备 Reset 也
// 不清表——正确性完全依赖指针身份，Reset 后新纹理得到新指针，旧条目只是
// 留驻内存。移植用 unordered_map 等价表达（哈希函数不影响语义，不复刻）。
// 未命中时从纹理内容取色：
//   QueryInterface(IID_IDirect3DTexture9) -> GetSurfaceLevel(0)（最高分辨率
//   级别，v表+0x90）-> GetDesc（v表+0x60），Format 仅接受 D3DFMT_R8G8B8
//   (20) / D3DFMT_A8R8G8B8 (21) / D3DFMT_X8R8G8B8 (22) [0x180056e53]；
//   LockRect(RECT{0, Height-1, 1, Height}, D3DLOCK_READONLY=0x10) [v表+0x68]
//   只锁第 0 列最底行的单个像素（toon ramp 受光最强的一端），随后
//   UnlockRect [v表+0x70]。内存字节序 B,G,R(,A)：r=pBits[2]、g=pBits[1]、
//   b=pBits[0] 各 /255 [0x180056ea6-0x180056ecd]；a 仅 A8R8G8B8 取
//   pBits[3]/255 [0x180056ed6-0x180056ee7]，R8G8B8/X8R8G8B8 固定 1.0。
// 任何一步失败（QI、GetSurfaceLevel、格式不符、LockRect）保持回退
// (1,1,1,1)（初值 0x180056dac）；除纹理为 null 外，失败结果同样写入记忆
// 化表（null 在插入路径之前提前返回，0x180056dc3）。
void MmeComputeFixedFunctionToonColor(float out[4], IDirect3DBaseTexture9* texture)
{
    struct ToonColorEntry { float c[4]; };
    static std::unordered_map<IDirect3DBaseTexture9*, ToonColorEntry> cache;

    std::unordered_map<IDirect3DBaseTexture9*, ToonColorEntry>::const_iterator it =
        cache.find(texture);
    if (it != cache.end()) {                          // 命中：直接回缓存值
        out[0] = it->second.c[0];
        out[1] = it->second.c[1];
        out[2] = it->second.c[2];
        out[3] = it->second.c[3];
        return;
    }

    float r = 1.0f, g = 1.0f, b = 1.0f, a = 1.0f;     // 回退初值 [0x180056dac]
    if (texture != nullptr) {
        IDirect3DTexture9* tex9 = nullptr;
        if (SUCCEEDED(texture->QueryInterface(kToonIidD3DTexture9,
                                              reinterpret_cast<void**>(&tex9))) &&
            tex9 != nullptr) {
            IDirect3DSurface9* surface = nullptr;
            if (SUCCEEDED(tex9->GetSurfaceLevel(0, &surface)) &&
                surface != nullptr) {
                D3DSURFACE_DESC desc;
                memset(&desc, 0, sizeof(desc));
                surface->GetDesc(&desc);
                if (desc.Format == D3DFMT_R8G8B8 ||     // 20 [0x180056e53]
                    desc.Format == D3DFMT_A8R8G8B8 ||   // 21
                    desc.Format == D3DFMT_X8R8G8B8) {   // 22
                    // 只锁 (0, Height-1) 单像素：left=0, top=H-1, right=1,
                    // bottom=H [0x180056e59-0x180056e6c]。
                    RECT rc;
                    rc.left = 0;
                    rc.top = static_cast<LONG>(desc.Height) - 1;
                    rc.right = 1;
                    rc.bottom = static_cast<LONG>(desc.Height);
                    D3DLOCKED_RECT locked;
                    memset(&locked, 0, sizeof(locked));
                    if (SUCCEEDED(surface->LockRect(&locked, &rc,
                                                    D3DLOCK_READONLY)) &&
                        locked.pBits != nullptr) {
                        const unsigned char* bits =
                            static_cast<const unsigned char*>(locked.pBits);
                        r = static_cast<float>(bits[2]) / 255.0f;  // [0x180056ea6]
                        g = static_cast<float>(bits[1]) / 255.0f;  // [0x180056eba]
                        b = static_cast<float>(bits[0]) / 255.0f;  // [0x180056ecd]
                        a = (desc.Format == D3DFMT_A8R8G8B8)
                                ? static_cast<float>(bits[3]) / 255.0f  // [0x180056ee7]
                                : 1.0f;                                  // [0x180056eee]
                        surface->UnlockRect();                          // [0x180056efa]
                    }
                }
                surface->Release();                   // [0x180056f09]
            }
            tex9->Release();                          // [0x180056f4a]
        }
        // 失败回退值同样入表（原版插入在 null 检查之后无条件执行）。
        ToonColorEntry entry = { { r, g, b, a } };
        cache[texture] = entry;
    }
    out[0] = r;
    out[1] = g;
    out[2] = b;
    out[3] = a;
}

} // namespace

// ---------------------------------------------------------------------------
// Effect-owner manager map (DAT_1800d9a40 + 0xa0)
// ---------------------------------------------------------------------------

MaterialBinding* MmeFindMaterialBinding(unsigned int turnId, ModelData* model,
                                        int subsetIndex, bool allowWholeObject)
{
    // [0x18002d910] FUN_18002d910: exact (count, model, subset) lookup; when
    // missing, subset >= 0 and the flag is set, retry once with subset = -1.
    EffectOwnerManager* manager = g_ownerManager;
    if (manager == nullptr || model == nullptr) {
        return nullptr;
    }
    // Nonzero turn IDs resolve the turn's own DefaultEffect assignments.
    // Declaring its OFFSCREENRENDERTARGET only registers a render target;
    // it does not copy the declaring object's root effect into that turn.
    if (turnId == 0 && MmeInOffscreenRenderTurn()) {
        return MmeResolveOffscreenDefaultBinding(model, subsetIndex);
    }
    int subset = subsetIndex < 0 ? -1 : subsetIndex;
    bool retry = true;
    while (true) {
        std::map<EffectOwnerManager::BindingKey, MaterialBinding*>::const_iterator it =
            manager->bindings.find(
                EffectOwnerManager::BindingKey(turnId, model, subset));
        if (it != manager->bindings.end()) {
            return it->second;
        }
        // [big-C 36951-36956] `if ((param_4 < 0) || (param_5 == 0)) return 0;`
        if (subset < 0 || !retry || !allowWholeObject) {
            return nullptr;
        }
        retry = false;   // [big-C 36955] param_5 = 1; param_4 = -1
        subset = -1;
    }
}

MaterialBinding* MmeEnsureMaterialBinding(unsigned int turnId, ModelData* model,
                                          int subsetIndex, ID3DXEffect* effect,
                                          const std::string& effectPath,
                                          const std::shared_ptr<LoadedEffect>& owner)
{
    EffectOwnerManager* manager = g_ownerManager;
    if (manager == nullptr || model == nullptr) {
        return nullptr;
    }
    int subset = subsetIndex < 0 ? -1 : subsetIndex;
    EffectOwnerManager::BindingKey key(turnId, model, subset);
    auto it = manager->bindings.find(key);
    if (it != manager->bindings.end() && it->second &&
        it->second->owner == owner && it->second->effectPath == effectPath &&
        (owner || it->second->effect == effect)) {
        return it->second;
    }
    auto binding = std::make_unique<MaterialBinding>();
    binding->owner = owner;
    binding->effectPath = effectPath;
    if (owner) {
        binding->instance = MmeEngineCreateEffectInstance(
            g_context ? g_context->device : nullptr, owner);
        if (!binding->instance) return nullptr;
        binding->effect = binding->instance->effect;
        binding->sas = binding->instance->sas;
    } else {
        binding->effect = effect;
    }
    MaterialBinding*& slot = manager->bindings[key];
    if (slot) binding->offscreenTurns = std::move(slot->offscreenTurns);
    delete slot;
    slot = binding.release();
    return slot;
}

void MmeDropMaterialBindings(ModelData* model)
{
    // [0x18002a430 / FUN_180058740] erase every binding of the model; a
    // scene-effect carrier also loses its SAS class flags (the planner must
    // drop it from passPlanA/B/renderPassList).
    EffectOwnerManager* manager = g_ownerManager;
    if (manager == nullptr || model == nullptr) {
        return;
    }
    std::map<EffectOwnerManager::BindingKey, MaterialBinding*>::iterator it =
        manager->bindings.begin();
    while (it != manager->bindings.end()) {
        if (it->first.model == model) {
            delete it->second;
            it = manager->bindings.erase(it);
        } else {
            ++it;
        }
    }
    model->ClearSasBinding();
}

// ---------------------------------------------------------------------------
// Technique selection [sub_18001DB50 / sub_18001DD20 / FUN_18001b940].
//
// The original precomputes five per-MmdPass handle vectors on the SAS object
// at effect load (sub_18001DD20, driven from sub_18000BC90 with the model's
// material count) and the per-draw apply (FUN_18001b940, called from
// MME_ApplyModelRenderSnapshot) only INDEXES them: modes 0/1 (object,
// object_ss) with subset*8 + (useTexture | useSpheremap<<1 | useToon<<2),
// modes 2/3/4 (shadow, edge, zplot) with the bare subset (their table was
// selected with every Use* state false). The port keeps the same two steps
// on the binding: the table below (built once per effect assignment) and the
// per-draw refresh (MmeRefreshDrawTechnique).
// ---------------------------------------------------------------------------

// Pass count of a technique handle (the table's per-entry pass count; the
// original reads it from the effect's technique map record).
static unsigned int MmeTechniquePassCount(const SasEffect* sas, D3DXHANDLE handle)
{
    if (sas == nullptr || handle == nullptr) {
        return 0;
    }
    for (size_t i = 0; i < sas->techniques.size(); ++i) {
        if (sas->techniques[i].handle == handle) {
            return static_cast<unsigned int>(sas->techniques[i].passes.size());
        }
    }
    return 0;
}

// [sub_18001DD20] the load-time selection-table precompute. Scene-class
// effects (ScriptClass scene) and any pre/post-process order clamp the
// subset count to one, and pre/post effects build ONLY the object-mode
// table [0x18001ddab-0x18001ddbe / 0x18001de1d]. The original's trailing
// "Error: some techniques cannot run on this hardware:" report is dead code
// in the shipped binary (its flag is only ever cleared - 0x18001DDBE /
// 0x18001E2E8 / 0x18001E3C8 - so sub_18001DD20 always returns 0 and never
// fails the load); a selection that resolves only to an invalid technique is
// still stored and drawn. The same applies here.
static void MmeBuildDrawTechniqueTable(MaterialBinding* binding, SasEffect* sas,
                                       ModelData* model)
{
    for (int i = 0; i < 5; ++i) {
        binding->drawTechniques[i].clear();
    }
    binding->drawTableBuilt = true;
    if (binding->effect == nullptr || sas == nullptr) {
        return;   // [!*a1] no effect: the empty table selects nothing
    }
    int materialCount = (model != nullptr) ? model->materialCount() : 0;
    if (materialCount <= 0) {
        return;   // [a2 <= 0] every lookup misses -> the raw host draw
    }
    if (sas->scriptClass == kSasClassScene ||
        sas->scriptOrder != kSasOrderStandard) {
        materialCount = 1;
    }
    for (int mode = 0; mode < 5; ++mode) {
        if (mode != 0 && sas->scriptOrder != kSasOrderStandard) {
            continue;   // pre/post effects never draw the other four modes
        }
        for (int subset = 0; subset < materialCount; ++subset) {
            if (mode <= 1) {
                // [0x18001df12-0x18001df40] the eight Use* states: combo bit
                // 0 = useTexture, bit 1 = useSpheremap, bit 2 = useToon.
                for (int combo = 0; combo < 8; ++combo) {
                    D3DXHANDLE handle = SasSelectTechnique(
                        sas, mode, subset, (combo & 1) != 0, (combo & 2) != 0,
                        (combo & 4) != 0, nullptr);
                    binding->drawTechniques[mode].push_back(std::make_pair(
                        handle, MmeTechniquePassCount(sas, handle)));
                }
            } else {
                // [0x18001de40-0x18001de5c] shadow/edge/zplot select with
                // every Use* state false.
                D3DXHANDLE handle =
                    SasSelectTechnique(sas, mode, subset, false, false, false,
                                       nullptr);
                binding->drawTechniques[mode].push_back(std::make_pair(
                    handle, MmeTechniquePassCount(sas, handle)));
            }
        }
    }
}

// [FUN_18001b940 0x18001b955-0x18001b978] the per-draw table lookup. A
// negative subset or an out-of-range index resolves to the null technique
// (the original leaves v8 = 0 and forwards the raw draw); the selected
// entry (a null handle included) is installed into the slot the draw
// wrapper reads (host draw type 1..5 = mode 0..4 + 1).
static void MmeRefreshDrawTechnique(MaterialBinding* binding,
                                    const RenderSnapshot& snap)
{
    if (binding == nullptr) {
        return;
    }
    const int mode = snap.draw_type_index;   // snapshot+0x30, normalized 0..4
    if (mode < 0 || mode > 4) {
        return;
    }
    D3DXHANDLE technique = nullptr;
    unsigned int passes = 0;
    int index = snap.subset_index;           // snapshot+0x28
    if (index >= 0) {
        if (mode <= 1) {
            // object/object_ss fold the material state into the index:
            // base_texture_present (+0x56), 0 < sphere_mode (+0x50),
            // toon_used (+0x54).
            index = index * 8
                  | ((snap.base_texture_present != 0) ? 1 : 0)
                  | ((snap.sphere_mode > 0) ? 2 : 0)
                  | ((snap.toon_used != 0) ? 4 : 0);
        }
        const std::vector<std::pair<D3DXHANDLE, unsigned int> >& table =
            binding->drawTechniques[mode];
        if (index < static_cast<int>(table.size())) {
            technique = table[static_cast<size_t>(index)].first;
            passes = table[static_cast<size_t>(index)].second;
        }
    }
    binding->techniques[mode + 1] = technique;
    binding->passCounts[mode + 1] = passes;
}

// ---------------------------------------------------------------------------
// MME_SelectMaterialEffectBinding [0x18005a020]
// ---------------------------------------------------------------------------

void MmeSelectMaterialEffectBinding(ModelData* model, void* bindingContext,
                                    const RenderSnapshot& snap)
{
    // The original stores &ModelData+0x138 (the cached snapshot) at
    // ModelData+0xe0 [kit L23]; the port reads the snapshot through the model.
    (void)bindingContext;
    if (model == nullptr) {
        return;
    }

    // [kit L25-27 / 0x18005a252] count = bindingContext ?
    // *(u32*)(bindingContext+0x38) : 0 —— 原版 bindingContext 是 0x48 回合
    // 包装（wrapper），+0x38（=56）是【当前 turn id】而非 materialCount：
    // 基回合 wrapper == null → 0（场景键 0），离屏回合 → 该回合的 owner id
    // （模型自身的 (0, model, *) 条目对它不可见）。本移植没有数值 turn id，
    // 且离屏回合内的查找已由 MmeFindMaterialBinding 的离屏短路接管
    // （carrier / DefaultEffect 行），manager-map 键只余基回合成分——恒 0。
    // 旧实现把 bindingContext 当 ModelData* 读 materialCount()，在
    // ScriptExternal 挂起窗口内 carrier 的 materialCount != 0 时会以
    // (materialCount, model, subset) 错键查找（查到或撞错 owner 条目），
    // 与原版 (turnId, model, subset) 语义不符——2026-09 修正。
    const unsigned int count = 0;

    // [kit L56-58] re-apply the existing binding for the snapshot subset.
    MaterialBinding* binding = MmeFindMaterialBinding(count, model, snap.subset_index, true);
    if (binding != nullptr) {
        MmeBindStandardParameters(model, binding, snap);
    }

    // [kit L59-100] renderClass == 0 with materialCount > 0: walk the subsets
    // and re-apply each per-material binding.
    if (model->renderClass() == 0 && model->materialCount() > 0) {
        for (int i = 0; i < model->materialCount(); ++i) {
            MaterialBinding* subsetBinding = MmeFindMaterialBinding(count, model, i, true);
            if (subsetBinding != nullptr) {
                MmeBindStandardParameters(model, subsetBinding, snap);
            }
        }
    }
}

// ---------------------------------------------------------------------------
// MME_ApplyModelRenderSnapshot [0x18005a1e0]
// ---------------------------------------------------------------------------

void MmeApplyModelRenderSnapshot(ModelData* model, void* bindingContext,
                                 const RenderSnapshot& snap)
{
    if (model == nullptr) {
        return;
    }

    // [kit L14-15] memcpy(ModelData+0x138, snapshot, 0x220).
    int drawTypeIndex = snap.draw_type_index;            // snapshot+0x30 [kit L14]
    model->snapshot() = snap;

    // [kit L16-24] technique (re)selection when the draw-type index changed.
    if (model->drawTypeIndex() != drawTypeIndex) {
        model->setDrawTypeIndex(drawTypeIndex);
        MmeSelectMaterialEffectBinding(model, bindingContext, model->snapshot());
    }

    // [kit L25-33] count from the binding context (+0x38, 0 when null).
    // 同 MmeSelectMaterialEffectBinding 的键语义核验：原版 wrapper+0x38 是
    // 【turn id】（基回合 0），不是 materialCount——离屏回合的 owner 键
    // 查找由 MmeFindMaterialBinding 的离屏短路接管（carrier / DefaultEffect
    // 行展开），这里只剩基回合的场景键 0。旧实现以 carrier 的
    // materialCount() 构键，ScriptExternal 挂起窗口内键位错开（P2，修正）。
    (void)bindingContext;
    const unsigned int count = 0;

    // [kit L31-37] FUN_18002d910 + FUN_18001b940(binding, draw_type_index,
    // subset, base_texture_present, 0 < sphere_mode, toon_used, model+0x08).
    MaterialBinding* binding = MmeFindMaterialBinding(count, model, snap.subset_index, true);
    if (binding == nullptr) {
        // [offscreen DefaultEffect window; sub_18005A1E0] Inside the window
        // a null resolution is the original's EMPTY binding (an unlisted or
        // "none" row) - the lazy creation of the model's own binding must
        // not run: the owner-keyed entry exists as a null/empty binding and
        // the draw replays through the host pipeline (the draw wrapper's
        // raw forward), never through the main effect.
        // [2026-09 P1] 窗口判定同步扩为"离屏渲染回合"（原版 wrapper 非
        // null）：无 DefaultEffect 行的回合里 owner 键同样 miss（原版
        // sub_18002D910 返回 0、sub_18005A1E0 不调 FUN_18001b940），模型
        // 自身的 (0, model, -1) 绑定不可见——lazy resolve 同样不得运行
        // （否则会把自身效果带给离屏目标）。carrier 的窗口查找在
        // MmeFindMaterialBinding 的短路内已返回自身绑定，不会走到这里。
        MmeContext* windowCtx = g_context;
        if (windowCtx != nullptr && MmeInOffscreenRenderTurn()) {
            return;
        }
        // The Phase 2 binding creation: resolve the model's assigned effect
        // through the engine cache and create the whole-object binding (the
        // original's FUN_18001b7b0/binding-ctor work at assign time). Without
        // this the map stays empty and no effect ever reaches the device.
        binding = MmeResolveModelEffectBinding(model);
        if (binding != nullptr) {
            binding = MmeFindMaterialBinding(count, model, snap.subset_index, true);
        }
    }
    if (binding != nullptr) {
        // [kit L31-37 / FUN_18001b940 0x18001b955-0x18001b978] the per-draw
        // technique table lookup: index the precomputed per-MmdPass table
        // with the snapshot's subset and material Use* state (mode 0/1 fold
        // them into subset*8 + tex | sph<<1 | toon<<2) and install the
        // selected technique/pass count into the slot the draw wrapper
        // reads. A miss installs null - the wrapper then forwards the raw
        // host draw, the original's v8 == 0 path.
        MmeRefreshDrawTechnique(binding, snap);
        // FUN_18001b940's op walk reduces to the standard parameter set in
        // Phase 2; the technique/pass state itself is executed by the object
        // draw's SAS walk in callbacks.cpp (SasExecuteTechnique - the walk
        // runs the technique/pass Script annotations and replays the
        // recorded draw per pass through the host kind-0 callback
        // FUN_18005a740: Begin/BeginPass/DIP/EndPass/End, the observable
        // equivalent of the original's op array).
        MmeBindStandardParameters(model, binding, snap);

    }
}

// ---------------------------------------------------------------------------
// MmeResolveModelEffectBinding - the binding creation. The original builds
// the binding (and its per-technique op arrays) when an effect is assigned to
// an object [FUN_18002ca80]; the port resolves lazily on the first draw.
// Technique selection runs through the per-MmdPass selector table
// (sub_18001DB50/sub_18001DD20 - MmdPass + Subset + UseTexture/UseSpheremap/
// UseToon against the material state); there is NO name-based fallback: a
// mode/subset/state combination that matches nothing draws through the host
// pipeline (FUN_18001b940's null-technique forward; the "MainTec0" convention
// does not exist in the binary). A scene/sceneorobject-class effect
// additionally writes its scriptClass/scriptOrder into the ModelData
// (+0x360/+0x364/+0x368) - the pass planner then routes the object through
// passPlanA/B and renderPassList [FUN_18002ca80 L292-320].
// ---------------------------------------------------------------------------
static int MmeFirstSceneTechnique(SasEffect* sas) {
    if (sas == nullptr || sas->scriptClass == kSasClassObject) return -1;
    for (D3DXHANDLE handle : sas->techniqueOrder) {
        for (size_t index = 0; index < sas->techniques.size(); ++index) {
            const SasTechnique& technique = sas->techniques[index];
            if (technique.handle == handle && !technique.empty && technique.hardwareOk)
                return static_cast<int>(index);
        }
    }
    return -1;
}

MaterialBinding* MmeResolveModelEffectBinding(ModelData* model)
{
    if (model == nullptr) {
        return nullptr;
    }
    const std::string& path = model->effectFile();
    if (path.empty()) {
        return nullptr;
    }
    MmeContext* ctx = g_context;
    IDirect3DDevice9* device = (ctx != nullptr) ? ctx->device : nullptr;
    std::shared_ptr<LoadedEffect> loaded = MmeEngineLoadEffectFile(device, path);
    if (loaded == nullptr || loaded->effect == nullptr) {
        return nullptr;
    }
    // The draw path looks the binding up with count = 0 (null binding
    // context) and the whole-object subset (-1).
    MaterialBinding* binding = MmeEnsureMaterialBinding(0, model, -1,
                                                        loaded->effect, path,
                                                        loaded);
    if (binding == nullptr || binding->effect == nullptr) {
        return binding;
    }
    if (!binding->drawTableBuilt) {
        MmeBuildDrawTechniqueTable(binding, binding->sas, model);
    }

    // Scene-class wiring: the planner picks the object up as a pass record.
    if (binding->sas != nullptr &&
        binding->sas->scriptClass != kSasClassObject) {
        model->setScriptClass(
            binding->sas->scriptClass);
        model->setRenderClass(binding->sas->scriptOrder == kSasOrderPreprocess ? 1
                          : binding->sas->scriptOrder == kSasOrderPostprocess ? 2
                          : 0);
        model->setDrawsGeometry(binding->sas->drawsGeometry);
        // The stepped/resumed scene technique: the first hardware-valid
        // technique in the MME order.
        binding->sceneTechIndex = MmeFirstSceneTechnique(binding->sas);
    } else {
        model->ClearSasBinding();
    }
    return binding;
}

MaterialBinding* MmeActiveModelBinding(ModelData* model)
{
    if (model == nullptr) {
        return nullptr;
    }
    // Explicit root-binding lookup, independent of the active offscreen turn.
    EffectOwnerManager* manager = g_ownerManager;
    if (manager == nullptr) {
        return nullptr;
    }
    std::map<EffectOwnerManager::BindingKey, MaterialBinding*>::const_iterator it =
        manager->bindings.find(EffectOwnerManager::BindingKey(0, model, -1));
    if (it != manager->bindings.end()) {
        return it->second;
    }
    return nullptr;
}

bool MmeInOffscreenRenderTurn()
{
    // [sub_18005A1E0 0x18005a24c-0x18005a268 / FUN_18005d130 L74740-74745]
    // 原版离屏回合判定是 ctx+0x168 的 0x48 回合包装（wrapper）非空：
    // wrapper+0x38 即绘制查找的 owner turn id（基回合 wrapper == null →
    // 键 0）。移植的 ctx->currentBindingOffscreen 是 wrapper 的资源身份
    // 对应物——pass_planner 在回合应用时按
    // renderPassList[lastRepeatCount-1] 设置，基回合（lastRepeatCount < 1）
    // 与回合切换的清空点写 null，非 null 即窗口。行 staging
    // （ctx->offscreenDefaultEffect）只覆盖"行存在"的子集，不再作为窗口
    // 判定：无 DefaultEffect 行的离屏目标同样处于窗口内。
    MmeContext* ctx = g_context;
    return ctx != nullptr && ctx->currentBindingOffscreen != nullptr;
}

MaterialBinding* MmeResolveSubsetEffectBinding(ModelData* model, int subsetIndex,
                                                const std::string& effectPath)
{
    if (model == nullptr || subsetIndex < 0 || effectPath.empty()) {
        return nullptr;
    }
    MmeContext* ctx = g_context;
    IDirect3DDevice9* device = ctx != nullptr ? ctx->device : nullptr;
    std::shared_ptr<LoadedEffect> loaded =
        MmeEngineLoadEffectFile(device, effectPath);
    if (loaded == nullptr || loaded->effect == nullptr) {
        return nullptr;
    }
    MaterialBinding* binding = MmeEnsureMaterialBinding(
        0, model, subsetIndex, loaded->effect, effectPath, loaded);
    if (binding == nullptr) {
        return nullptr;
    }
    if (!binding->drawTableBuilt) {
        // [sub_18001DD20] same per-MmdPass selection table as the whole-object
        // binding; the per-draw apply indexes it (FUN_18001b940). No
        // name-based fallback (see MmeResolveModelEffectBinding).
        MmeBuildDrawTechniqueTable(binding, binding->sas, model);
    }
    return binding;
}

// ---------------------------------------------------------------------------
// Offscreen DefaultEffect (sub_180011960 offscreen record +0x78 rows). While
// a suspended scene technique renders into the offscreen target, EVERY model
// draw consults the staged rows: the FIRST row whose key matches the model
// wins (annotation order - the original's vector append order).
//
// Key matching [VERIFIED, sub_18002ACE0 0x18002b7c5-0x18002b981] per row:
//   - key == "self" (std::string::compare vs the 4-char literal = memcmp,
//     CASE-SENSITIVE): the row matches iff the drawn ModelData POINTER equals
//     the offscreen owner (the model the scene-class effect declaring this
//     OFFSCREENRENDERTARGET is assigned to; the original reads it from the
//     scene-effect assignment context, map entry +32 -> +48). "self" never
//     goes through the wildcard matcher, and the wildcard branch never
//     matches the owner by name - only this pointer compare.
//   - every other key: sub_1800676F0(name, nameLen, key, keyLen, 1) - the
//     self-written recursive wildcard matcher (the DLL imports no
//     PathMatchSpec), applied to the model's BASENAME+EXTENSION ONLY
//     (ModelData+0x48). The full path is never compared by the original.
// The port keeps its earlier exact full-path stricmp as an additional branch
// so pre-wildcard scenes with absolute-path keys do not regress.
// ---------------------------------------------------------------------------

// [sub_1800676F0] the original's wildcard matcher, ported 1:1. (name,nameLen)
// and (pat,patLen) are length-delimited (the caller feeds std::string data,
// which is NUL-terminated at [len] - the original's star-skip relies on that
// terminator; the port bounds-checks instead, which is equivalent because
// '\0' can never equal '*'). Semantics:
//   - pattern empty: match iff name is empty;
//   - '?': eats exactly one character (any);
//   - '*': collapses consecutive stars; an all-star remainder matches
//     anything, otherwise the first following pattern char is scanned
//     forward in the name (raw or tolower equality) and the tail is matched
//     recursively - the scan is skipped for a '?'/'\\' first char, the
//     recursion then tries every position;
//   - '\\' ESCAPES the next pattern character ('\*' matches a literal
//     asterisk); a trailing lone backslash fails the match;
//   - literal characters compare through tolower() on both sides when
//     ignoreCase is set (the DefaultEffect caller passes 1).
static bool MmeWildcardMatch(const char* name, size_t nameLen,
                             const char* pat, size_t patLen, bool ignoreCase)
{
    const char* np = name;
    const char* const nend = name + nameLen;
    const char* pp = pat;
    const char* const pend = pat + patLen;
    if (pp == pend) {
        return np == nend;
    }
    char pc = *pp;
    while (true) {
        pc = *pp;
        if (np == nend) {
            break;
        }
        if (pc == '*') {
            goto star;
        }
        if (pc == '?') {
            ++pp;
            ++np;
        } else {
            if (pc == '\\' && ++pp == pend) {
                return false;
            }
            if (ignoreCase) {
                if (tolower(static_cast<unsigned char>(*np)) !=
                    tolower(static_cast<unsigned char>(*pp))) {
                    return false;
                }
            } else if (*np != *pp) {
                return false;
            }
            ++np;
            ++pp;
        }
        if (pp == pend) {
            return np == nend;
        }
    }
    if (pc != '*') {
        return false;
    }
star:
    do {
        ++pp;
    } while (pp != pend && *pp == '*');
    if (pp == pend) {
        return true;
    }
    char first = *pp;
    if (ignoreCase) {
        first = static_cast<char>(
            tolower(static_cast<unsigned char>(first)));
    }
    const size_t tailLen = static_cast<size_t>(pend - pp);
    while (true) {
        if (first != '?' && first != '\\') {
            for (; np < nend; ++np) {
                if (first == *np) {
                    break;
                }
                if (ignoreCase &&
                    first ==
                        static_cast<char>(
                            tolower(static_cast<unsigned char>(*np)))) {
                    break;
                }
            }
        }
        if (MmeWildcardMatch(np, static_cast<size_t>(nend - np), pp, tailLen,
                             ignoreCase)) {
            return true;
        }
        if (np == nend) {
            return false;
        }
        ++np;
    }
}

// [sub_18002ACE0 0x18002b890] the "self" target: the owner of the effect
// that declares this OFFSCREENRENDERTARGET (the scene carrier whose
// technique is suspended and rendering into the target). The original reads
// the pointer from its scene-effect assignment context; the port recovers
// it from the staged rows pointer instead: the whole-object binding whose
// SAS model contains the 0x2E resource owning the staged vector. A
// scene-class carrier is required (sceneTechIndex >= 0 - the planner's
// stepped/resumed records); an OBJECT-class assignment of the same .fx
// shares the engine-cached SasEffect and must not win. Returns null when
// no carrier is bound (a "self" row then matches nothing - the original's
// missing-map-entry path behaves the same).
static ModelData* MmeOffscreenDefaultEffectOwner(MmeContext* ctx)
{
    if (ctx != nullptr && ctx->currentBindingOffscreen != nullptr)
        return ctx->currentBindingObject;
    EffectOwnerManager* manager = g_ownerManager;
    if (manager == nullptr) {
        return nullptr;
    }
    ModelData* fallback = nullptr;
    for (std::map<EffectOwnerManager::BindingKey,
                  MaterialBinding*>::const_iterator it =
             manager->bindings.begin();
         it != manager->bindings.end(); ++it) {
        MaterialBinding* binding = it->second;
        if (binding == nullptr || binding->sas == nullptr) {
            continue;
        }
        SasEffect* sas = binding->sas;
        for (size_t r = 0; r < sas->resources.size(); ++r) {
            if (sas->resources[r].semanticId == 0x2E &&
                &sas->resources[r].defaultEffectMap ==
                    ctx->offscreenDefaultEffect) {
                if (binding->sceneTechIndex >= 0) {
                    return it->first.model;
                }
                if (fallback == nullptr) {
                    fallback = it->first.model;
                }
            }
        }
    }
    return fallback;
}

const std::pair<std::string, std::string>* MmeFindDefaultEffectRow(
    const std::vector<std::pair<std::string, std::string>>& rows,
    ModelData* model, ModelData* owner)
{
    if (model == nullptr) return nullptr;
    const std::string& name = model->name();
    for (size_t i = 0; i < rows.size(); ++i) {
        const std::string& key = rows[i].first;
        if (key.empty()) {
            continue;
        }
        // [sub_18002ACE0 0x18002b880] "self": exact case-sensitive keyword,
        // then a POINTER comparison against the offscreen owner.
        if (key == "self") {
            if (model == owner) {
                return &rows[i];
            }
            continue;
        }
        // [sub_18002ACE0 0x18002b899-0x18002b8c5] wildcard row: glob against
        // the basename+extension, case-insensitive ("sky*box*.*",
        // "GroundFog*.*", "*.pmx" ...). An exact key is the degenerate
        // pattern, so precise rows (e.g. "DirectionalLight.pmx") keep
        // matching exactly like the previous stricmp port.
        if (MmeWildcardMatch(name.c_str(), name.size(), key.c_str(),
                             key.size(), true)) {
            return &rows[i];
        }

    }
    return nullptr;
}

bool MmeDefaultEffectRowShown(const std::pair<std::string, std::string>* row)
{
    return row == nullptr || row->second != "hide";
}

const std::string* MmeOffscreenEffectValue(const SasResource& resource,
    ModelData* model, ModelData* owner, int subsetIndex)
{
    if (!model) return nullptr;
    auto it = resource.effectOverrides.find({model->objectId(), subsetIndex});
    if (it == resource.effectOverrides.end() && subsetIndex >= 0)
        it = resource.effectOverrides.find({model->objectId(), -1});
    if (it != resource.effectOverrides.end()) return &it->second;
    const auto* row = MmeFindDefaultEffectRow(resource.defaultEffectMap, model, owner);
    return row ? &row->second : nullptr;
}

bool MmeOffscreenObjectShown(const SasResource& resource,
    ModelData* model, ModelData* owner, int subsetIndex)
{
    if (!model) return true;
    auto it = resource.shownOverrides.find({model->objectId(), subsetIndex});
    if (it == resource.shownOverrides.end() && subsetIndex >= 0)
        it = resource.shownOverrides.find({model->objectId(), -1});
    if (it != resource.shownOverrides.end()) return it->second;
    // Scene carriers are hidden by default outside the base scene.
    if (model->renderClass() != 0) return false;
    const auto* value = MmeOffscreenEffectValue(resource, model, owner, subsetIndex);
    if (value && *value == "main_default")
        return MmeEmmEffectiveSubsetShown(model, subsetIndex);
    return !value || *value != "hide";
}

// The value of the first staged row whose key matches the model (null when
// no rows are staged or none match). Keep turn/owner discovery separate from
// the shared query so the mapping UI can inspect any target without changing
// the active render turn or loading an effect.
static const std::string* MmeOffscreenDefaultEffectRow(ModelData* model, int subsetIndex = -1)
{
    MmeContext* ctx = g_context;
    if (model == nullptr || ctx == nullptr) return nullptr;
    if (ctx->currentBindingOffscreen)
        return MmeOffscreenEffectValue(*ctx->currentBindingOffscreen, model,
                                      ctx->currentBindingObject, subsetIndex);
    const auto* rowSource = ctx->currentBindingOffscreen != nullptr
        ? &ctx->currentBindingOffscreen->defaultEffectMap : ctx->offscreenDefaultEffect;
    if (rowSource == nullptr) return nullptr;
    ModelData* owner = nullptr;
    for (const auto& row : *rowSource) {
        if (row.first == "self") {
            owner = MmeOffscreenDefaultEffectOwner(ctx);
            break;
        }
    }
    const auto* row = MmeFindDefaultEffectRow(*rowSource, model, owner);
    return row != nullptr ? &row->second : nullptr;
}

MaterialBinding* MmeResolveTurnEffectBinding(unsigned int turnId,
    SasResource& resource, ModelData* offscreenOwner, ModelData* model, int subsetIndex)
{
    if (!model || !g_ownerManager || !MmeOffscreenObjectShown(
            resource, model, offscreenOwner, subsetIndex)) return nullptr;
    const std::string* value = MmeOffscreenEffectValue(
        resource, model, offscreenOwner, subsetIndex);
    if (!value || *value == "none" || *value == "hide") return nullptr;

    const bool explicitSubset = resource.effectOverrides.find(
        {model->objectId(), subsetIndex}) != resource.effectOverrides.end();
    // Ordinary annotation rows assign the whole object; material rows inherit
    // it. main_default separately resolves the base scene's material mapping.
    if (subsetIndex >= 0 && !explicitSubset && *value != "main_default")
        return MmeResolveTurnEffectBinding(turnId, resource, offscreenOwner, model, -1);

    std::string path = *value;
    if (path == "main_default") {
        auto assigned = model->subsetEffects().find(subsetIndex);
        if (subsetIndex >= 0 && assigned != model->subsetEffects().end()) {
            path = assigned->second;
        } else if (subsetIndex >= 0) {
            return MmeResolveTurnEffectBinding(turnId, resource, offscreenOwner, model, -1);
        } else {
            path = model->effectFile();
        }
        if (path.empty()) return nullptr;
    }
    auto loaded = MmeEngineLoadEffectFile(g_context ? g_context->device : nullptr, path);
    if (!loaded || !loaded->effect) return nullptr;
    auto* binding = MmeEnsureMaterialBinding(turnId, model, subsetIndex,
        loaded->effect, path, loaded);
    if (!binding) return nullptr;
    binding->sceneTechIndex = MmeFirstSceneTechnique(binding->sas);
    if (!binding->drawTableBuilt) MmeBuildDrawTechniqueTable(binding, binding->sas, model);
    return binding;
}

MaterialBinding* MmeResolveOffscreenDefaultBinding(ModelData* model, int subsetIndex)
{
    MmeContext* ctx = g_context;
    if (!ctx || !ctx->currentBindingOffscreen) return nullptr;
    return MmeResolveTurnEffectBinding(ctx->currentBindingTurnId,
        *ctx->currentBindingOffscreen, ctx->currentBindingObject, model, subsetIndex);
}

bool MmeHasOffscreenDefaultEffectRow(ModelData* model, int subsetIndex)
{
    return MmeOffscreenDefaultEffectRow(model, subsetIndex) != nullptr;
}

bool MmeOffscreenDefaultEffectHides(ModelData* model, int subsetIndex)
{
    if (g_context && g_context->currentBindingOffscreen)
        return !MmeOffscreenObjectShown(*g_context->currentBindingOffscreen,
            model, g_context->currentBindingObject, subsetIndex);
    const std::string* value = MmeOffscreenDefaultEffectRow(model, subsetIndex);
    return value != nullptr && *value == "hide";
}



// ---------------------------------------------------------------------------
// MME_BindMaterialParameter [0x18005eff0]
// ---------------------------------------------------------------------------

void MmeBindMaterialParameter(ModelData* model, ID3DXEffect* effect, int semantic,
                              D3DXHANDLE handle, int count)
{
    if (model == nullptr || effect == nullptr || handle == nullptr) {
        return;
    }
    (void)count;   // the wrapper's SetVector count; the standard interface
                   // writes what the declared parameter type consumes.
    const RenderSnapshot& snap = model->snapshot();   // ModelData+0xe0 target

    float value[4] = { 0.0f, 0.0f, 0.0f, 0.0f };

    switch (semantic) {
    case 0x1C: {
        // ToonColor [big-C 53-79]. Effect path: snapshot+0x1a8 when the model
        // is a PMD/PMX model (kind 1) in the draw-type-2 pass; otherwise the
        // all-1.0 fallback (DAT_1800b5b28). Fixed-function path
        // [0x18005f069-0x18005f0ae]: only for a kind-1 model in the
        // draw-type-1 pass - GetTexture(0) (device slot 0x200) feeds
        // FUN_180056c30 (ported as MmeComputeFixedFunctionToonColor), which
        // memoizes per texture pointer and derives the color from the
        // bottom-left pixel of the level-0 surface; everything else falls
        // back to the all-1.0 vector.
        if (snap.effect_file_used != 0) {
            if (model->kind() == 1 && snap.draw_type == 2) {
                value[0] = snap.toon_color.r;
                value[1] = snap.toon_color.g;
                value[2] = snap.toon_color.b;
                value[3] = snap.toon_color.a;
            } else {
                value[0] = value[1] = value[2] = value[3] = 1.0f;
            }
        } else if (model->kind() == 1 && snap.draw_type == 1) {
            // [0x18005f069-0x18005f0ae] 无宿主 .fx 的固定函数分支：stage-0
            // 纹理交给 FUN_180056c30 的移植取色后 Release。
            IDirect3DDevice9* device = model->device();
            IDirect3DBaseTexture9* texture = nullptr;
            if (device != nullptr) {
                device->GetTexture(0, &texture);
            }
            MmeComputeFixedFunctionToonColor(value, texture);
            if (texture != nullptr) {
                texture->Release();               // [0x18005f0a6]
            }
        } else {
            value[0] = value[1] = value[2] = value[3] = 1.0f;
        }
        break;
    }
    case 0x1D: {
        // EdgeColor [big-C 80-99]. Only during the edge pass (raw draw
        // type 4); effect path = snapshot+0x198, fixed-function = the drawn
        // vertex diffuse (FUN_18005ee60 port) scaled by 1/255.
        if (snap.draw_type != 4) {
            return;
        }
        if (snap.effect_file_used != 0) {
            value[0] = snap.edge_color.r;
            value[1] = snap.edge_color.g;
            value[2] = snap.edge_color.b;
            value[3] = snap.edge_color.a;
        } else {
            IDirect3DDevice9* device = model->device();
            unsigned int color = MmeReadVertexDiffuseColor(
                device, snap.base_vertex_index, static_cast<int>(snap.start_index));
            MmeD3DColorToFloat4(value, color);
        }
        break;
    }
    case 0x4E: {
        // GroundShadowColor [big-C 100-141; 0x18005eff0 case 78]. Only during
        // the shadow pass (raw draw type 3). The rgb base is the Ambient
        // slice of the CURRENT device material (GetMaterial - draw type 3
        // routes through that branch here), multiplied by light0's Ambient
        // rgb (GetLight(0), D3DLIGHT9 +0x24). kind==1 alpha = the fixed-
        // function vertex diffuse alpha (FUN_18005ee60, snapshot+0x08 /
        // snapshot+0x14) scaled 1/255 then by DAT_1800b68b8 (= 0.65f);
        // kind==0 alpha = the material Diffuse.a - the light alpha never
        // enters the composition.
        if (snap.draw_type != 3) {
            return;
        }
        float ambientValue[4];
        float diffuseValue[4];
        float power = 0.0f;
        MmeComposeMaterialColor(model, snap, 0x19, ambientValue, &power);
        MmeComposeMaterialColor(model, snap, 0x18, diffuseValue, &power);
        if (model->kind() == 1) {
            IDirect3DDevice9* device = model->device();
            unsigned int color = MmeReadVertexDiffuseColor(
                device, snap.base_vertex_index, static_cast<int>(snap.start_index));
            value[3] = static_cast<float>((color >> 24) & 0xff) * 0.0039215689f;
            value[3] *= 0.64999998f;                        // [0x1800b68b8]
        } else {
            value[3] = diffuseValue[3];                     // material Diffuse.a
        }
        D3DLIGHT9 light;
        memset(&light, 0, sizeof(light));
        IDirect3DDevice9* device = model->device();
        if (device == nullptr || FAILED(device->GetLight(0, &light))) {
            light = g_cachedLight;                          // DAT_1800d9890
        }
        value[0] = ambientValue[0] * light.Ambient.r;
        value[1] = ambientValue[1] * light.Ambient.g;
        value[2] = ambientValue[2] * light.Ambient.b;
        break;
    }
    case 0x18:
    case 0x19:
    case 0x1A:
    case 0x1B: {
        // Diffuse / Ambient / Emissive / Specular (Geometry) [big-C 142-218;
        // 0x1A is the Emissive id, 0x1B the Specular id - see the slice
        // switch in MmeComposeMaterialColor].
        float power = 0.0f;
        MmeComposeMaterialColor(model, snap, semantic, value, &power);
        break;
    }
    case 0x1E: {
        // SpecularPower [big-C 202-215]: the material Power; 0.0f -> 0.1f
        // [DAT_1800b5c1c/DAT_1800b5c18]; count 1 = scalar, else splat. The
        // standard ID3DXEffect::SetVector writes what the declared parameter
        // type consumes (a float1 parameter takes the x component), so the
        // scalar case is served by the same splat.
        float power = 0.0f;
        float unused[4];
        MmeComposeMaterialColor(model, snap, 0x18, unused, &power);
        (void)unused;
        if (power == 0.0f) {
            power = 0.1f;
        }
        value[0] = value[1] = value[2] = value[3] = power;
        break;
    }
    default:
        return;
    }

    // [big-C 220-222] the SetVector tail (**(effect+0x100))(effect, handle,
    // value, count) - the standard interface drops the count argument.
    effect->SetVector(handle,
                      reinterpret_cast<const D3DXVECTOR4*>(value));
}

// ---------------------------------------------------------------------------
// MME_BindBooleanParameter [0x18005fb40]
// ---------------------------------------------------------------------------

void MmeBindBooleanParameter(ModelData* model, ID3DXEffect* effect, int semantic,
                             D3DXHANDLE handle)
{
    if (model == nullptr || effect == nullptr || handle == nullptr) {
        return;
    }
    const RenderSnapshot& snap = model->snapshot();   // lVar1 = ModelData+0xe0

    BOOL value = FALSE;
    switch (semantic) {
    case 0x3C: {
        // parthf [big-C 19-23]: read the CURRENT effect's parthf handle and
        // forward it (the pool shares the value; the copy keeps pool-less
        // effects correct).
        ID3DXEffect* current = static_cast<ID3DXEffect*>(GetCurrentEffect());
        BOOL parthf = FALSE;
        if (current != nullptr) {
            current->GetBool(g_paramHandles[kParamParthf], &parthf);
        }
        value = parthf;
        break;
    }
    case 0x3D:
        value = (snap.sphere_mode == 2) ? TRUE : FALSE;    // spadd [big-C 25]
        break;
    case 0x3E:
        value = snap.transparent != 0 ? TRUE : FALSE;      // transp [big-C 30]
        break;
    case 0x3F:
        value = snap.base_texture_present != 0 ? TRUE : FALSE;   // use_texture [L35]
        break;
    case 0x40:
        value = (static_cast<int>(snap.sphere_mode) > 0) ? TRUE : FALSE;  // use_spheremap [L40]
        break;
    case 0x41:
        value = (snap.sphere_mode == 3) ? TRUE : FALSE;    // use_subtexture [L43]
        break;
    case 0x42:
        value = snap.toon_used != 0 ? TRUE : FALSE;        // use_toon [L46]
        break;
    case 0x45: {
        // opadd [big-C 50-54]: GetBlendMode() == 1.
        int blendMode = GetBlendMode();
        value = (blendMode == 1) ? TRUE : FALSE;
        break;
    }
    default:
        return;
    }

    // [big-C 22/30/58] the SetBool tail (**(effect+0xb0))(effect, handle, v).
    effect->SetBool(handle, value);
}

// ---------------------------------------------------------------------------
// The Phase 2 standard apply
// ---------------------------------------------------------------------------

namespace {

// [0x1800B2FA0..0x1800B36A0] the original's SEMANTIC table, in table order.
// A parameter whose desc.Semantic appears here is claimed by the semantic
// family during the parameter enumeration (the semantic-table walk at
// 0x18000e492 runs FIRST and never falls through to the name table on a
// hit), so the name resolver below must skip it.
const char* const kSemanticNames[] = {
    "World", "WorldInverse", "WorldTranspose", "WorldInverseTranspose",
    "View", "ViewInverse", "ViewTranspose", "ViewInverseTranspose",
    "Projection", "ProjectionInverse", "ProjectionTranspose",
    "ProjectionInverseTranspose",
    "WorldView", "WorldViewInverse", "WorldViewTranspose",
    "WorldViewInverseTranspose",
    "ViewProjection", "ViewProjectionInverse", "ViewProjectionTranspose",
    "ViewProjectionInverseTranspose",
    "WorldViewProjection", "WorldViewProjectionInverse",
    "WorldViewProjectionTranspose", "WorldViewProjectionInverseTranspose",
    "Diffuse", "Ambient", "Emissive", "Specular",
    "Position", "Direction",
    "ToonColor", "EdgeColor", "SpecularPower",
    "ViewportPixelSize",
    "ElapsedTime", "Time", "Time2", "ElapsedTime2",
    "RenderColorTarget", "RenderDepthStencilTarget",
    "ControlObject",
    "MaterialTexture", "MaterialSphereMap", "MaterialToonTexture",
    "MousePosition", "LeftMouseDown", "MiddleMouseDown", "RightMouseDown",
    "AnimatedTexture", "OffScreenRenderTarget",
    "TextureValue",
    "AddingTexture", "MultiplyingTexture", "AddingSphereTexture",
    "MultiplyingSphereTexture",
    "GroundShadowColor",
};

// [0x1800B36A0..0x1800B39A0] the original's parameter-NAME table, in table
// order (stride 0x20: {id, name, class, type, rows*columns}). Verified
// against the per-id dispatchers: sub_18001B340 cases 52/53/54 (matWorld/
// matLightViewProj/matRotate - 0x34/0x35/0x36, case 53 re-dispatches id 20's
// light product), cases 60/62/69 (parthf/transp/opadd -> the boolean
// provider 0x18005fb40), cases 67/68 (VertexCount/SubsetCount -> the int
// provider); sub_18001B5B0 cases 57-59 (EgColor/SpcColor/DifColor ->
// 0x39/0x3A/0x3B, the snapshot-vector provider) and cases 70-77
// (TexCAdd..SphCMul 0x46-0x49, same provider). Place (0x38) and LightDir
// (0x37) appear in NO setter dispatch. matWorldViewProj shares id 0x14 with
// the WORLDVIEWPROJECTION semantic; ToonColor shares id 0x1C (the material
// binder). "DifColor" at 0x1800b2aa0 has a SINGLE f (verified via
// get_string; the table-B entry id 0x3B points exactly there).
struct NameParamEntry {
    const char*        name;
    int                id;
    D3DXPARAMETER_CLASS cls;
    D3DXPARAMETER_TYPE type;
    unsigned int       count;   // rows * columns
};

const NameParamEntry kNameParams[] = {
    { "matWorld",         0x34, D3DXPC_MATRIX_ROWS, D3DXPT_FLOAT, 16 },
    { "matWorldViewProj", 0x14, D3DXPC_MATRIX_ROWS, D3DXPT_FLOAT, 16 },
    { "matLightViewProj", 0x35, D3DXPC_MATRIX_ROWS, D3DXPT_FLOAT, 16 },
    { "matRotate",        0x36, D3DXPC_MATRIX_ROWS, D3DXPT_FLOAT, 16 },
    { "EgColor",          0x39, D3DXPC_VECTOR,      D3DXPT_FLOAT, 4 },
    { "ToonColor",        0x1C, D3DXPC_VECTOR,      D3DXPT_FLOAT, 4 },
    { "LightDir",         0x37, D3DXPC_VECTOR,      D3DXPT_FLOAT, 4 },
    { "SpcColor",         0x3A, D3DXPC_VECTOR,      D3DXPT_FLOAT, 4 },
    { "Place",            0x38, D3DXPC_VECTOR,      D3DXPT_FLOAT, 4 },
    { "DifColor",         0x3B, D3DXPC_VECTOR,      D3DXPT_FLOAT, 4 },
    { "parthf",           0x3C, D3DXPC_SCALAR,      D3DXPT_BOOL,  1 },
    { "spadd",            0x3D, D3DXPC_SCALAR,      D3DXPT_BOOL,  1 },
    { "transp",           0x3E, D3DXPC_SCALAR,      D3DXPT_BOOL,  1 },
    { "use_texture",      0x3F, D3DXPC_SCALAR,      D3DXPT_BOOL,  1 },
    { "use_spheremap",    0x40, D3DXPC_SCALAR,      D3DXPT_BOOL,  1 },
    { "use_subtexture",   0x41, D3DXPC_SCALAR,      D3DXPT_BOOL,  1 },
    { "use_toon",         0x42, D3DXPC_SCALAR,      D3DXPT_BOOL,  1 },
    { "VertexCount",      0x43, D3DXPC_SCALAR,      D3DXPT_INT,   1 },
    { "SubsetCount",      0x44, D3DXPC_SCALAR,      D3DXPT_INT,   1 },
    { "opadd",            0x45, D3DXPC_SCALAR,      D3DXPT_BOOL,  1 },
    { "TexCAdd",          0x46, D3DXPC_VECTOR,      D3DXPT_FLOAT, 4 },
    { "TexCMul",          0x47, D3DXPC_VECTOR,      D3DXPT_FLOAT, 4 },
    { "SphCAdd",          0x48, D3DXPC_VECTOR,      D3DXPT_FLOAT, 4 },
    { "SphCMul",          0x49, D3DXPC_VECTOR,      D3DXPT_FLOAT, 4 },
};

// [0x18000e45e-0x18000e878] the original resolves the name-table parameters
// while ENUMERATING the effect's top-level parameters: GetParameter(NULL, i)
// -> GetParameterDesc, skip the parameters a recognized semantic claims,
// then match desc.Name against the name table with _stricmp (case variants
// like `Matworld`/`MATWORLD` bind exactly like `matWorld`) and register the
// ENUMERATED handle under the table id - never GetParameterByName (the D3DX
// name lookup is case-sensitive and would miss the variants). The shape gate
// (0x18000e8a2-0x18000e8e9): desc.Class with MATRIX_COLUMNS normalized to
// MATRIX_ROWS must equal the table class, desc.Type the table type,
// rows*columns the table count, and matrices must have rows == 4 / vectors
// rows == 1. A mismatch skips the binding silently except the four use_*
// flags, which report "Error: type of parameter '<name>' is invalid."
// (0x18000e901-0x18000e955; prefix 0x1800b3e20). Note the name-table path
// has NO desc.Elements gate (that gate only exists on the semantic path).
void MmeResolveNameTableHandles(MaterialBinding* binding)
{
    binding->namesResolved = true;
    ID3DXEffect* effect = binding->effect;
    if (effect == nullptr) {
        return;
    }
    D3DXEFFECT_DESC effectDesc;
    memset(&effectDesc, 0, sizeof(effectDesc));
    if (FAILED(effect->GetDesc(&effectDesc))) {
        return;
    }
    for (UINT i = 0; i < effectDesc.Parameters; ++i) {
        D3DXHANDLE h = effect->GetParameter(nullptr, i);
        D3DXPARAMETER_DESC pd;
        memset(&pd, 0, sizeof(pd));
        if (h == nullptr || FAILED(effect->GetParameterDesc(h, &pd)) ||
            pd.Name == nullptr) {
            continue;
        }
        // A recognized semantic claims the parameter for the semantic family.
        if (pd.Semantic != nullptr) {
            bool claimed = false;
            for (size_t s = 0; s < sizeof(kSemanticNames) / sizeof(kSemanticNames[0]); ++s) {
                if (_stricmp(pd.Semantic, kSemanticNames[s]) == 0) {
                    claimed = true;
                    break;
                }
            }
            if (claimed) {
                continue;
            }
        }
        const NameParamEntry* entry = nullptr;
        for (size_t n = 0; n < sizeof(kNameParams) / sizeof(kNameParams[0]); ++n) {
            if (_stricmp(pd.Name, kNameParams[n].name) == 0) {
                entry = &kNameParams[n];
                break;
            }
        }
        if (entry == nullptr) {
            continue;
        }
        int cls = pd.Class;
        if (cls == D3DXPC_MATRIX_COLUMNS) {
            cls = D3DXPC_MATRIX_ROWS;   // [0x18000e8a2] the 3 -> 2 normalize
        }
        const bool mismatch =
            cls != static_cast<int>(entry->cls) ||
            pd.Type != entry->type ||
            pd.Rows * pd.Columns != static_cast<INT>(entry->count) ||
            (cls == D3DXPC_MATRIX_ROWS && pd.Rows != 4) ||
            (cls == D3DXPC_VECTOR && pd.Rows != 1);
        if (mismatch) {
            if (entry->id >= 0x3F && entry->id <= 0x42) {
                MmeLogWrite((std::string("Error: type of parameter '") +
                             pd.Name + "' is invalid.\n").c_str(), 0);
            }
            continue;
        }
        binding->namedHandles[entry->name] = h;
    }
}

}  // namespace

void MmeBindStandardParameters(ModelData* model, MaterialBinding* binding,
                               const RenderSnapshot& snap)
{
    if (model == nullptr || binding == nullptr || binding->effect == nullptr) {
        return;
    }
    ID3DXEffect* effect = binding->effect;

    // Handles must belong to the target effect: the g_paramHandles cache was
    // resolved from the host effect at Initialize and is meaningless for a
    // model effect. The port resolves the whole 24-entry name table once per
    // binding with the original's enumeration + _stricmp mechanism (see
    // MmeResolveNameTableHandles).
    if (!binding->namesResolved) {
        MmeResolveNameTableHandles(binding);
    }
    auto H = [&](const char* name) -> D3DXHANDLE {
        std::map<std::string, D3DXHANDLE>::const_iterator it =
            binding->namedHandles.find(name);
        return it != binding->namedHandles.end() ? it->second : nullptr;
    };

    // --- the 17 cached standard parameters (DAT_1800d9b30..DAT_1800d9bb0) ---
    // Matrices from the snapshot fields (+0x98/+0x118/+0x158/+0xd8); ids
    // 0x34/0x14/0x35/0x36 (sub_18001B340 cases 52/53/54 - case 53
    // re-dispatches matLightViewProj as id 20's light WORLDVIEWPROJECTION).
    effect->SetMatrix(H("matWorld"),
                      reinterpret_cast<const D3DXMATRIX*>(&snap.effect_world));
    effect->SetMatrix(H("matWorldViewProj"),
                      reinterpret_cast<const D3DXMATRIX*>(&snap.world_view_projection));
    effect->SetMatrix(H("matLightViewProj"),
                      reinterpret_cast<const D3DXMATRIX*>(&snap.light_view_projection));
    effect->SetMatrix(H("matRotate"),
                      reinterpret_cast<const D3DXMATRIX*>(&snap.rotation));

    // Colors from the snapshot (+0x198..+0x217) except LightDir, which comes
    // from the GetLight(0) cache (DAT_1800d9890). Ids 0x39/0x3A/0x3B and
    // 0x46-0x49 (the sub_18001B5B0 cases 57-59/70-77 snapshot-vector
    // provider). ToonColor is id 0x1C - the MATERIAL binder
    // (sub_18001B5B0 case 24 -> MmeBindMaterialParameter), not a plain
    // snapshot vector. Place (id 0x38) is resolved by the table but consumed
    // by no setter dispatch in the original - never set.
    // [sub_18005F830] EgColor/SpcColor/DifColor: with effect_file_used
    // (+0x55) the snapshot vectors win (+0x198/+0x1B8/+0x1C8); otherwise the
    // original synthesizes from LIVE GetMaterial + GetLight(0) device state:
    // DifColor = lightDiffuse*matDiffuse (a=1), SpcColor =
    // lightSpecular*matSpecular (a = mat.Power==0 ? 0.1f : mat.Power),
    // EgColor = lightAmbient*(kind==1 ? matDiffuse : matAmbient) +
    // matEmissive (a = mat.Diffuse.a).
    if (snap.effect_file_used != 0) {
        effect->SetVector(H("EgColor"),
                          reinterpret_cast<const D3DXVECTOR4*>(&snap.edge_color));
        effect->SetVector(H("SpcColor"),
                          reinterpret_cast<const D3DXVECTOR4*>(&snap.specular_color));
        effect->SetVector(H("DifColor"),
                          reinterpret_cast<const D3DXVECTOR4*>(&snap.diffuse_color));
    } else {
        IDirect3DDevice9* bindDevice = nullptr;
        if (SUCCEEDED(effect->GetDevice(&bindDevice)) && bindDevice != nullptr) {
            D3DMATERIAL9 mat;
            D3DLIGHT9 light;
            memset(&mat, 0, sizeof(mat));
            memset(&light, 0, sizeof(light));
            bindDevice->GetMaterial(&mat);
            bindDevice->GetLight(0, &light);
            float eg[4];
            float spc[4];
            float dif[4];
            if (model->kind() == 1) {
                eg[0] = light.Ambient.r * mat.Diffuse.r + mat.Emissive.r;
                eg[1] = light.Ambient.g * mat.Diffuse.g + mat.Emissive.g;
                eg[2] = light.Ambient.b * mat.Diffuse.b + mat.Emissive.b;
            } else {
                eg[0] = mat.Ambient.r * light.Ambient.r + mat.Emissive.r;
                eg[1] = mat.Ambient.g * light.Ambient.g + mat.Emissive.g;
                eg[2] = mat.Ambient.b * light.Ambient.b + mat.Emissive.b;
            }
            eg[3] = mat.Diffuse.a;
            spc[0] = light.Specular.r * mat.Specular.r;
            spc[1] = light.Specular.g * mat.Specular.g;
            spc[2] = light.Specular.b * mat.Specular.b;
            spc[3] = (mat.Power == 0.0f) ? 0.1f : mat.Power;  // 0x3DCCCCCD
            dif[0] = light.Diffuse.r * mat.Diffuse.r;
            dif[1] = light.Diffuse.g * mat.Diffuse.g;
            dif[2] = light.Diffuse.b * mat.Diffuse.b;
            dif[3] = 1.0f;                                    // 0x3F800000
            effect->SetVector(H("EgColor"),
                              reinterpret_cast<const D3DXVECTOR4*>(eg));
            effect->SetVector(H("SpcColor"),
                              reinterpret_cast<const D3DXVECTOR4*>(spc));
            effect->SetVector(H("DifColor"),
                              reinterpret_cast<const D3DXVECTOR4*>(dif));
            bindDevice->Release();
        }
    }
    MmeBindMaterialParameter(model, effect, 0x1C, H("ToonColor"), 4);
    {
        float lightDir[4];
        lightDir[0] = g_cachedLight.Direction.x;
        lightDir[1] = g_cachedLight.Direction.y;
        lightDir[2] = g_cachedLight.Direction.z;
        lightDir[3] = 0.0f;
        effect->SetVector(H("LightDir"),
                          reinterpret_cast<const D3DXVECTOR4*>(lightDir));
    }
    effect->SetVector(H("TexCAdd"),
                      reinterpret_cast<const D3DXVECTOR4*>(&snap.texture_add));
    effect->SetVector(H("TexCMul"),
                      reinterpret_cast<const D3DXVECTOR4*>(&snap.texture_multiply));
    effect->SetVector(H("SphCAdd"),
                      reinterpret_cast<const D3DXVECTOR4*>(&snap.sphere_add));
    effect->SetVector(H("SphCMul"),
                      reinterpret_cast<const D3DXVECTOR4*>(&snap.sphere_multiply));

    // Booleans through the name-table handles (ids 0x3C-0x42 and 0x45; the
    // sub_18001B340 cases 60/62/69 + sub_18001B5B0 cases 61/63-66 boolean
    // provider 0x18005fb40). MmeBindBooleanParameter no-ops a null handle.
    MmeBindBooleanParameter(model, effect, 0x3C, H("parthf"));
    MmeBindBooleanParameter(model, effect, 0x3D, H("spadd"));
    MmeBindBooleanParameter(model, effect, 0x3E, H("transp"));
    MmeBindBooleanParameter(model, effect, 0x3F, H("use_texture"));
    MmeBindBooleanParameter(model, effect, 0x40, H("use_spheremap"));
    MmeBindBooleanParameter(model, effect, 0x41, H("use_subtexture"));
    MmeBindBooleanParameter(model, effect, 0x42, H("use_toon"));
    MmeBindBooleanParameter(model, effect, 0x45, H("opadd"));

    // [sub_18001B340 cases 67/68 - ids 0x43/0x44] the int provider.
    D3DXHANDLE handle = H("VertexCount");
    if (handle != nullptr) {
        effect->SetInt(handle, model->cachedPerVertexValue() >= 0
                                   ? model->cachedPerVertexValue() : 0);
    }
    handle = H("SubsetCount");
    if (handle != nullptr) {
        effect->SetInt(handle, model->materialCount());
    }

    // --- MATERIALTEXTURE / MATERIALSPHEREMAP / MATERIALTOONTEXTURE are bound
    // BY SEMANTIC in MmeBindSemanticParameters [sub_18005F7C0, dispatched per
    // effect Begin through sub_18001B5B0 cases 41-43 at 0x18001b662]: the
    // snapshot slots (+0x38/+0x48/+0x40) were staged from the MMHack getters
    // GetTexture/GetSphereMapTexture/GetToonTexture [sub_180059C60
    // 0x180059c99-0x180059cbe]. The original's parameter-name table
    // (0x1800B36A0, matWorld..SphCMul) has NO "ObjectTexture"-style entry, so
    // there is no name-based fallback to keep here. ---

    MmeBindSemanticParameters(model, binding, snap);
}

// ---------------------------------------------------------------------------
// The SAS semantic binding layer. ray-mmd style effects declare the standard
// parameters by SEMANTIC (float4x4 matWorldViewProject : WORLDVIEWPROJECTION,
// float3 CameraPosition : POSITION<Object=Camera>, ...), never by the MME
// legacy names - the original resolves both through its semantic/name tables
// (0x1800B2FA0/0x1800B36A0). Handles resolve once per binding via
// GetParameterBySemantic; the values come from the snapshot, the BeginScene
// transform caches (g_worldViewMatrix/g_projMatrix/...) and the light cache.
// ---------------------------------------------------------------------------

namespace {

// The "Object" annotation of a semantic parameter ("Camera" / "Light" /
// "Geometry" / absent).
std::string SemanticObjectAnnotation(ID3DXEffect* effect, D3DXHANDLE param)
{
    D3DXHANDLE ann = effect->GetAnnotationByName(param, "Object");
    if (ann == nullptr) {
        return std::string();
    }
    LPCSTR s = nullptr;
    if (effect->GetString(ann, &s) == S_OK && s != nullptr) {
        return std::string(s);
    }
    return std::string();
}

D3DXMATRIX InverseOf(const D3DMATRIX& m)
{
    D3DXMATRIX out;
    D3DXMatrixInverse(&out, nullptr, reinterpret_cast<const D3DXMATRIX*>(&m));
    return out;
}

// [0x18000e481-0x18000e4a8 / 0x18000e811-0x18000e835] the original walks
// BOTH lookup tables (semantic 0x1800B2FA0, parameter name 0x1800B36A0)
// with _stricmp - the matching is case-insensitive end to end, so a
// lowercase "worldviewprojection" binds like WORLDVIEWPROJECTION.
bool SemanticEquals(const std::string& sem, const char* name)
{
    return _stricmp(sem.c_str(), name) == 0;
}

// ---------------------------------------------------------------------------
// [零分配热路径] The per-semantic dispatch classification, resolved ONCE per
// binding (the resolve pass of MmeBindSemanticParameters) and consumed by the
// per-pass switch. The kind/base/flags/light encoding mirrors the original's
// string dispatch order exactly: suffix decode (INVERSETRANSPOSE → TRANSPOSE
// → INVERSE) → matrix family → POSITION/DIRECTION → the Light-family color
// composition → material ids → times → texture slots → mouse → modulation →
// viewport/mouse-position.
// ---------------------------------------------------------------------------
enum {
    kSemNone = 0,
    kSemMatrix,             // base 0..5 = WORLD/VIEW/PROJECTION/WORLDVIEW/
                            // VIEWPROJECTION/WORLDVIEWPROJECTION; flags
                            // bit0=INVERSE bit1=TRANSPOSE
    kSemPositionDirection,  // base 0=POSITION 1=DIRECTION; light selects family
    kSemLightColor,         // base 0=DIFFUSE 1=AMBIENT 2=SPECULAR (light only)
    kSemMaterialId,         // base = the 0x18..0x1E/0x4E material id
    kSemTime,               // base 0=TIME 1=ELAPSEDTIME 2=TIME2 3=ELAPSEDTIME2
    kSemTextureSlot,        // base 0=MaterialTexture 1=SphereMap 2=ToonTexture
    kSemMouseDown,          // base 0=Left 1=Right 2=Middle (g_mouseClick* order)
    kSemTextureModulation,  // flags bit0=mul bit1=sphere
    kSemViewportPixelSize,
    kSemMousePosition
};

void ClassifySemantic(ID3DXEffect* effect, D3DXHANDLE h,
                      const D3DXPARAMETER_DESC& pd,
                      MaterialBinding::SemanticBind* sb)
{
    sb->kind = kSemNone;
    sb->base = 0;
    sb->flags = 0;
    sb->light = 0;
    const char* sem = pd.Semantic != nullptr ? pd.Semantic : "";
    // [0x18000f3a0] the annotation is matched case-insensitively against
    // "camera"/"light"/"light0"/"geometry"; "light" and "light0" both select
    // the light family.
    const std::string objectAnn = SemanticObjectAnnotation(effect, h);
    if (_stricmp(objectAnn.c_str(), "Light") == 0 ||
        _stricmp(objectAnn.c_str(), "Light0") == 0) {
        sb->light = 1;
    }
    bool inverse = false, transpose = false;
    std::string base = sem;
    if (base.size() >= 15 &&
        _stricmp(base.c_str() + base.size() - 15, "INVERSETRANSPOSE") == 0) {
        inverse = transpose = true;
        base.erase(base.size() - 15);
    } else if (base.size() >= 9 &&
               _stricmp(base.c_str() + base.size() - 9, "TRANSPOSE") == 0) {
        transpose = true;
        base.erase(base.size() - 9);
    } else if (base.size() >= 7 &&
               _stricmp(base.c_str() + base.size() - 7, "INVERSE") == 0) {
        inverse = true;
        base.erase(base.size() - 7);
    }
    sb->flags = static_cast<unsigned char>((inverse ? 1 : 0) | (transpose ? 2 : 0));
    struct MatrixBase { const char* name; unsigned char base; };
    static const MatrixBase kMatrices[] = {
        {"WORLD", 0}, {"VIEW", 1}, {"PROJECTION", 2},
        {"WORLDVIEW", 3}, {"VIEWPROJECTION", 4}, {"WORLDVIEWPROJECTION", 5},
    };
    for (size_t i = 0; i < sizeof(kMatrices) / sizeof(kMatrices[0]); ++i) {
        if (_stricmp(base.c_str(), kMatrices[i].name) == 0) {
            sb->kind = kSemMatrix;
            sb->base = kMatrices[i].base;
            return;
        }
    }
    if (_stricmp(sem, "POSITION") == 0 || _stricmp(sem, "DIRECTION") == 0) {
        sb->kind = kSemPositionDirection;
        sb->base = _stricmp(sem, "POSITION") == 0 ? 0 : 1;
        return;
    }
    if (sb->light != 0 &&
        (_stricmp(sem, "DIFFUSE") == 0 || _stricmp(sem, "AMBIENT") == 0 ||
         _stricmp(sem, "SPECULAR") == 0)) {
        sb->kind = kSemLightColor;
        sb->base = _stricmp(sem, "DIFFUSE") == 0 ? 0
                 : (_stricmp(sem, "AMBIENT") == 0 ? 1 : 2);
        return;
    }
    struct MaterialSem { const char* name; unsigned char id; };
    static const MaterialSem kMaterials[] = {
        {"DIFFUSE", 0x18}, {"AMBIENT", 0x19}, {"SPECULAR", 0x1B},
        {"EMISSIVE", 0x1A}, {"TOONCOLOR", 0x1C}, {"EDGECOLOR", 0x1D},
        {"SPECULARPOWER", 0x1E}, {"GROUNDSHADOWCOLOR", 0x4E},
    };
    for (size_t i = 0; i < sizeof(kMaterials) / sizeof(kMaterials[0]); ++i) {
        if (_stricmp(sem, kMaterials[i].name) == 0) {
            sb->kind = kSemMaterialId;
            sb->base = kMaterials[i].id;
            return;
        }
    }
    if (_stricmp(sem, "TIME") == 0)         { sb->kind = kSemTime; sb->base = 0; return; }
    if (_stricmp(sem, "ELAPSEDTIME") == 0)  { sb->kind = kSemTime; sb->base = 1; return; }
    if (_stricmp(sem, "Time2") == 0)        { sb->kind = kSemTime; sb->base = 2; return; }
    if (_stricmp(sem, "ElapsedTime2") == 0) { sb->kind = kSemTime; sb->base = 3; return; }
    if (_stricmp(sem, "MaterialTexture") == 0)     { sb->kind = kSemTextureSlot; sb->base = 0; return; }
    if (_stricmp(sem, "MaterialSphereMap") == 0)   { sb->kind = kSemTextureSlot; sb->base = 1; return; }
    if (_stricmp(sem, "MaterialToonTexture") == 0) { sb->kind = kSemTextureSlot; sb->base = 2; return; }
    if (_stricmp(sem, "LeftMouseDown") == 0)   { sb->kind = kSemMouseDown; sb->base = 0; return; }
    if (_stricmp(sem, "RightMouseDown") == 0)  { sb->kind = kSemMouseDown; sb->base = 1; return; }
    if (_stricmp(sem, "MiddleMouseDown") == 0) { sb->kind = kSemMouseDown; sb->base = 2; return; }
    if (_stricmp(sem, "AddingTexture") == 0)            { sb->kind = kSemTextureModulation; sb->flags = 0; return; }
    if (_stricmp(sem, "MultiplyingTexture") == 0)       { sb->kind = kSemTextureModulation; sb->flags = 1; return; }
    if (_stricmp(sem, "AddingSphereTexture") == 0)      { sb->kind = kSemTextureModulation; sb->flags = 2; return; }
    if (_stricmp(sem, "MultiplyingSphereTexture") == 0) { sb->kind = kSemTextureModulation; sb->flags = 3; return; }
    if (_stricmp(sem, "VIEWPORTPIXELSIZE") == 0) { sb->kind = kSemViewportPixelSize; return; }
    if (_stricmp(sem, "MOUSEPOSITION") == 0)     { sb->kind = kSemMousePosition; return; }
    sb->kind = kSemNone;
    sb->flags = 0;
}

}  // namespace

void MmeBindSemanticParameters(ModelData* model, MaterialBinding* binding,
                               const RenderSnapshot& snap)
{
    if (binding == nullptr || binding->effect == nullptr) {
        return;
    }
    ID3DXEffect* effect = binding->effect;
    // [零分配热路径] 首次绑定把每个语义参数的分发决策定型进 semanticFast
    // （kind/base/flags/light），此后每 pass 只走 switch——本函数在 rayMMD
    // 下每帧被调用 300+ 次（~31 pass × 10 轮），原版 sub_18001B5B0 的预编
    // 译 id 表是零分配的，任何每调用的 std::string/注解读取都会把渲染线程
    // 变成每秒数十万次 malloc/free 的堆竞争源（曾在 free() 的堆锁上死锁，
    // 见 2026-09-18 rayMMD 冻结分析）。
    if (!binding->semanticsResolved) {
        binding->semanticsResolved = true;
        D3DXEFFECT_DESC effectDesc{};
        if (SUCCEEDED(effect->GetDesc(&effectDesc))) {
            for (UINT i = 0; i < effectDesc.Parameters; ++i) {
                D3DXHANDLE h = effect->GetParameter(nullptr, i);
                D3DXPARAMETER_DESC pd{};
                if (h == nullptr || FAILED(effect->GetParameterDesc(h, &pd)) ||
                    pd.Semantic == nullptr) {
                    continue;
                }
                binding->semanticHandles.push_back(h);
                MaterialBinding::SemanticBind sb{};
                sb.handle = h;
                // kind/base/flags/light 的定型逻辑与下方分发一一对应（沿
                // 用原实现的字符串语义，仅在首次执行）。
                ClassifySemantic(effect, h, pd, &sb);
                binding->semanticFast.push_back(sb);
            }
        }
    }
    if (binding->semanticFast.empty()) {
        return;
    }
    // Camera and light matrix families. The Object annotation selects which
    // family VIEW/PROJECTION and their products use.
    // [0x18005ea40 / 0x18005ebe0] the WORLD base of both the geometry and
    // light families is snapshot+0x58 (model_world, translation intact) -
    // NOT the +0x98 effect_world whose fixed-function variant zeroes the
    // translation row.
    D3DMATRIX world = snap.model_world;
    // [0x1800574e0 / 0x180057720] the camera VIEW cache DAT_1800d9db0 is
    // world@BeginScene * view, computed once in OnBeginScene; the original
    // never multiplies the inverse world back in.
    D3DXMATRIX cameraView =
        *reinterpret_cast<const D3DXMATRIX*>(&g_worldViewMatrix);
    const D3DXMATRIX cameraProj = g_projMatrix;
    const D3DXMATRIX lightView = g_lightViewMatrix;
    const D3DXMATRIX lightProj = g_lightProjMatrix;

    for (size_t si = 0; si < binding->semanticFast.size(); ++si) {
        const MaterialBinding::SemanticBind& sb = binding->semanticFast[si];
        D3DXHANDLE h = sb.handle;
        D3DXPARAMETER_DESC pd{};
        if (FAILED(effect->GetParameterDesc(h, &pd))) continue;
        const D3DXMATRIX view = sb.light ? lightView : cameraView;
        const D3DXMATRIX proj = sb.light ? lightProj : cameraProj;
        // [0x180057720 case 8/16] the camera VIEWPROJECTION is (W*V)*P; the
        // light family reads the GetLightViewProjMatrix product
        // (DAT_1800d9ef0) for the plain VIEWPROJECTION.
        D3DXMATRIX viewProj{}, worldView{}, worldViewProj{};
        D3DXMatrixMultiply(&viewProj, &view, &proj);
        D3DXMatrixMultiply(&worldView, reinterpret_cast<const D3DXMATRIX*>(&world), &view);
        if (sb.light) {
            // [0x18005ebe0 case 20] the light WORLDVIEWPROJECTION is the
            // precomputed snapshot+0x158 cache, not a live product.
            worldViewProj = *reinterpret_cast<const D3DXMATRIX*>(
                &snap.light_view_projection);
        } else {
            // [0x18005ea40 case 20] geometry: objWorld * (W*V*P).
            D3DXMatrixMultiply(&worldViewProj,
                               reinterpret_cast<const D3DXMATRIX*>(&world), &viewProj);
        }

        auto setVector = [&](float x, float y, float z, float w) {
            float values[4] = {x, y, z, w};
            effect->SetValue(h, values, (pd.Columns >= 4 ? 4u : pd.Columns) * sizeof(float));
        };
        switch (sb.kind) {
        case kSemMatrix: {
            const D3DXMATRIX* matrix = nullptr;
            switch (sb.base) {
            case 0: matrix = reinterpret_cast<const D3DXMATRIX*>(&world); break;
            case 1: matrix = &view; break;
            case 2: matrix = &proj; break;
            case 3: matrix = &worldView; break;
            case 4: matrix = &viewProj; break;
            default: matrix = &worldViewProj; break;
            }
            D3DXMATRIX value = (sb.flags & 1) ? InverseOf(*matrix) : *matrix;
            if (sb.flags & 2) {
                // [子项3; sub_18005EA40] the transpose runs IN PLACE
                // (D3DXMatrixTranspose(&v11, &v11) at 0x18005eba4), after the
                // optional Inverse (0x18005eb90) and before SetMatrix
                // (vtbl+304). The flags are NOT annotations: sub_18001B340
                // (0x18001b3d9/0x18001b3de) derives them from the
                // semantic-id variant bits (inverse = id&1, transpose = id&2)
                // - i.e. the semantic-NAME suffix (WorldInverseTranspose
                // etc.) that the classify pass already decodes.
                D3DXMatrixTranspose(&value, &value);
            }
            effect->SetMatrix(h, &value);
            continue;
        }
        case kSemPositionDirection: {
            if (sb.light) {
                // [0x180057a20] t = TransformNormal(lightDir, the inverse of
                // the BeginScene world matrix [DAT_1800d9d70]); POSITION =
                // (-t, 0), DIRECTION = (normalize(t), 0). No 100000 scale -
                // the old port's -dir*100000 followed ray-mmd's HLSL, not
                // the MME binary.
                D3DXVECTOR3 t(g_cachedLight.Direction.x,
                              g_cachedLight.Direction.y,
                              g_cachedLight.Direction.z);
                D3DXVec3TransformNormal(
                    &t, &t,
                    reinterpret_cast<const D3DXMATRIX*>(&g_invWorldAtBegin));
                if (sb.base == 0) {
                    setVector(-t.x, -t.y, -t.z, 0.0f);
                } else {
                    D3DXVec3Normalize(&t, &t);
                    setVector(t.x, t.y, t.z, 0.0f);
                }
            } else {
                // [0x180057920] camera: POSITION = the translation of
                // inverse(worldView) with w 1; DIRECTION = normalize(the
                // inverse's row 2) with w 0.
                D3DXMATRIX inv = InverseOf(cameraView);
                if (sb.base == 0) {
                    setVector(inv.m[3][0], inv.m[3][1], inv.m[3][2], 1.0f);
                } else {
                    D3DXVECTOR3 d(inv.m[2][0], inv.m[2][1], inv.m[2][2]);
                    D3DXVec3Normalize(&d, &d);
                    setVector(d.x, d.y, d.z, 0.0f);
                }
            }
            continue;
        }
        case kSemLightColor: {
            // [0x18005f550] the Light-object color composition. Draw types
            // 1/3 read the LIVE GetLight(0); every other draw uses the
            // OnBeginScene light0 cache (DAT_1800d9890). The kind==0
            // (accessory) rewrites apply on the cached branch: draw 1 with
            // the DIFFUSE semantic divides the Diffuse rgb by
            // DAT_1800b5c20 (= 10.0f); the non-1/3/4 draws set Diffuse.rgb
            // to 1.0f (DAT_1800b5b28) and subtract DAT_1800b5c24 (= 0.3f)
            // from Ambient.rgb regardless of the semantic. The output w is
            // unconditionally 1.0f.
            D3DLIGHT9 lightState;
            memset(&lightState, 0, sizeof(lightState));
            int drawType = snap.draw_type;
            if (drawType == 1 || drawType == 3) {
                IDirect3DDevice9* device =
                    model != nullptr ? model->device() : nullptr;
                if (device == nullptr || FAILED(device->GetLight(0, &lightState))) {
                    lightState = g_cachedLight;
                }
                if (sb.base == 0 && model != nullptr &&
                    model->kind() == 0 && drawType == 1) {
                    lightState.Diffuse.r /= 10.0f;
                    lightState.Diffuse.g /= 10.0f;
                    lightState.Diffuse.b /= 10.0f;
                }
            } else {
                lightState = g_cachedLight;
                if (model != nullptr && model->kind() == 0 && drawType != 4) {
                    lightState.Diffuse.r = lightState.Diffuse.g =
                        lightState.Diffuse.b = 1.0f;
                    lightState.Ambient.r -= 0.30000001f;
                    lightState.Ambient.g -= 0.30000001f;
                    lightState.Ambient.b -= 0.30000001f;
                }
            }
            if (sb.base == 0) {
                setVector(lightState.Diffuse.r, lightState.Diffuse.g,
                          lightState.Diffuse.b, 1.0f);
            } else if (sb.base == 1) {
                setVector(lightState.Ambient.r, lightState.Ambient.g,
                          lightState.Ambient.b, 1.0f);
            } else {
                setVector(lightState.Specular.r, lightState.Specular.g,
                          lightState.Specular.b, 1.0f);
            }
            continue;
        }
        case kSemMaterialId:
            MmeBindMaterialParameter(model, effect, sb.base, h, pd.Columns);
            continue;
        case kSemTime:
            // [sub_180057B30] TIME reads g_frameTimeBase; ELAPSEDTIME the
            // frame delta; the "2" variants read the RAW frame clock,
            // bypassing the edit/play switch of the sub_180056F80 machine
            // (TIME2 = g_lastFrameTime, ELAPSEDTIME2 = g_frameDelta).
            switch (sb.base) {
            case 0: effect->SetFloat(h, g_frameTimeBase); break;
            case 1: effect->SetFloat(h, g_deltaSeconds); break;
            case 2: effect->SetFloat(h, g_lastFrameTime); break;
            default: effect->SetFloat(h, g_frameDelta); break;
            }
            continue;
        case kSemTextureSlot: {
            // [子项1; sub_18005F7C0] the material texture semantics bind BY
            // SEMANTIC to the snapshot texture slots; the SetTexture is
            // unconditional (a null texture clears the sampler).
            switch (sb.base) {
            case 0: effect->SetTexture(h, snap.base_texture); break;
            case 1: effect->SetTexture(h, snap.sphere_texture); break;
            default: effect->SetTexture(h, snap.toon_texture); break;
            }
            continue;
        }
        case kSemMouseDown: {
            // [子项2; sub_180058340] the mouse button semantics are float4s
            // frozen at the press; the port's g_mouseClick* arrays keep the
            // original's slot order (left, right, middle).
            setVector(g_mouseClickPos[sb.base][0], g_mouseClickPos[sb.base][1],
                      g_mouseClickZ[sb.base], g_mouseClickTime[sb.base]);
            continue;
        }
        case kSemTextureModulation: {
            // [子项2; sub_18005F830] the texture modulation semantics are
            // the SEMANTIC twins of the TexCAdd/TexCMul/SphCAdd/SphCMul
            // names and read the SAME snapshot slots. When the effect path
            // is unused or the object is not a PMD model the Add twins
            // splat 0.0 and the Mul twins splat 1.0.
            const bool mul = (sb.flags & 1) != 0;
            const bool sphere = (sb.flags & 2) != 0;
            if (snap.effect_file_used != 0 && model != nullptr && model->kind() == 1) {
                const D3DCOLORVALUE& v =
                    sphere ? (mul ? snap.sphere_multiply : snap.sphere_add)
                           : (mul ? snap.texture_multiply : snap.texture_add);
                setVector(v.r, v.g, v.b, v.a);
            } else {
                const float splat = mul ? 1.0f : 0.0f;
                setVector(splat, splat, splat, splat);
            }
            continue;
        }
        case kSemViewportPixelSize: {
            // [sub_18005EDD0 0x18005edf8-0x18005ee35] this semantic is
            // the BeginScene size, or the CURRENT OFFSCREEN TURN's size
            // (wrapper+0x40/+0x44), never the live device viewport. A script
            // may redirect to smaller intermediate targets; Ray explicitly
            // scales offsets from this stable size for those targets.
            UINT width = g_beginViewport.Width;
            UINT height = g_beginViewport.Height;
            const SasResource* offscreen = g_context != nullptr
                ? g_context->currentBindingOffscreen : nullptr;
            if (offscreen != nullptr) {
                // [0x18002d41c-0x18002d43b] the wrapper starts with zero
                // dimensions and receives the offscreen surface descriptor.
                // Re-query the same source, as MmeApplyPassRecord does, so
                // a replacement surface after reset supplies its new size.
                D3DSURFACE_DESC desc{};
                if (offscreen->surface != nullptr) {
                    offscreen->surface->GetDesc(&desc);
                }
                width = desc.Width;
                height = desc.Height;
            }
            setVector(static_cast<float>(width), static_cast<float>(height), 0, 0);
            continue;
        }
        case kSemMousePosition: {
            // [0x180055b10 mouse tail] the live cursor in NDC.
            D3DVIEWPORT9 vp{}; POINT pt{};
            if (model && model->device()) model->device()->GetViewport(&vp);
            GetCursorPos(&pt); if (g_mainWindow) ScreenToClient(g_mainWindow, &pt);
            const float x = vp.Width ? 2.0f * (pt.x - static_cast<float>(vp.X)) / vp.Width - 1.0f : 0.0f;
            const float y = vp.Height ? 1.0f - 2.0f * (pt.y - static_cast<float>(vp.Y)) / vp.Height : 0.0f;
            setVector(x, y, 0, 0);
            continue;
        }
        default:
            continue;
        }
    }
}

void MmeResolveAccessoryAttach(ModelData* model, unsigned long long accessoryId)
{
    // [mmhack GetAcsAttachedPmd] which PMD (and bone) the accessory is
    // attached to; "(self)" / "(AttachedModel)" / "(AttachedBone)" CONTROLOBJECT
    // naming in Phase 3 keys off this state (strings 0x1800b580c/18/28).
    if (model == nullptr || model->kind() == 1) {
        return;   // PMD/PMX models are never attached accessories
    }
    unsigned long long attachedModelId = 0;
    int attachedBoneIndex = -1;
    BOOL attached = GetAcsAttachedPmd(accessoryId, &attachedModelId, &attachedBoneIndex);
    model->setAttachInfo(attached != FALSE, attachedModelId, attachedBoneIndex);
}

// ---------------------------------------------------------------------------
// MmeUpdateControlObjects - the per-frame CONTROLOBJECT resolution
// (the original's EffectFrameParamSetter, sub_180057BC0). Every collected
// control parameter resolves its object by name against the live ModelData
// list and its item through the name table (bones 0..n-1, morphs -1,-2,...,
// accessory pseudo-bones), then the value is pushed by parameter type.
// ---------------------------------------------------------------------------

namespace {

// [0x180057BC0] Names are resolved in the per-frame drawing-order tree.
// +0xec is abs(ExpGetPmd/AcsOrder), not a model loading serial.
ModelData* FindModelByName(const std::string& objectName, ModelData* owner)
{
    if (g_context == nullptr || owner == nullptr)
        return nullptr;
    return g_context->modelsByName.Find(objectName, owner->passPlanScratch().renderOrder);
}

// [0x180058133-0x18005817e] the object-not-found kind guess: the registry
// entry's first model kind when the name is loaded, else the name suffix
// (".x"/".X" -> accessory 0, otherwise model 1).
int KindGuessForName(const std::string& objectName)
{
    if (g_context != nullptr) {
        ModelData* first = g_context->modelsByName.First(objectName);
        if (first != nullptr)
            return first->kind();
    }
    const size_t len = objectName.size();
    if (len >= 2 && (objectName[len - 1] == 'x' || objectName[len - 1] == 'X') &&
        objectName[len - 2] == '.') {
        return 0;   // accessory file extension
    }
    return 1;
}

// [0x1800555c0] the attached-pmd id -> ModelData lookup (the original reads
// the manager's id map at DAT_1800d9bb8+280; the port's registry is that
// map, keyed by the host ExpGetPmdID/ExpGetAcsID pointer).
ModelData* FindModelById(unsigned long long objectId)
{
    MmeContext* ctx = g_context;
    if (ctx == nullptr || objectId == 0) {
        return nullptr;
    }
    std::unordered_map<unsigned long long, ModelData*>::const_iterator it =
        ctx->modelRegistry.find(objectId);
    return (it != ctx->modelRegistry.end()) ? it->second : nullptr;
}

// [0x180058042 arg_30] the parameter-shape code the original registered per
// control parameter: 0 = bool scalar, 1 = float scalar, 3 = float3 vector,
// 4 = float4 vector, 16 = matrix.
int ControlParameterTypeCode(const D3DXPARAMETER_DESC& pd)
{
    if (pd.Class == D3DXPC_MATRIX_ROWS || pd.Class == D3DXPC_MATRIX_COLUMNS) {
        return 16;
    }
    if (pd.Class == D3DXPC_VECTOR) {
        if (pd.Type == D3DXPT_BOOL) {
            return 0;
        }
        return (pd.Columns >= 4) ? 4 : 3;
    }
    return (pd.Type == D3DXPT_BOOL) ? 0 : 1;
}

// [0x180058189-0x18005830d] the object-not-found writes, by parameter shape.
// The float default depends on the item presence: whole-object references
// read the "default object" scale (10.0f accessories [DAT_1800b5c20] / 1.0f
// models [DAT_1800b5b28]); item-named references read 0.0f. The matrix
// default is the identity for models and D3DXMatrixScaling(10,10,10) for
// accessories.
void SetControlDefaults(ID3DXEffect* effect, D3DXHANDLE param, int typeCode,
                        bool hasItem, int kindGuess)
{
    switch (typeCode) {
    case 0:
        effect->SetBool(param, FALSE);                                 // [0x1800582fb]
        break;
    case 1:
        effect->SetFloat(param, hasItem
                                ? 0.0f
                                : ((kindGuess == 0) ? 10.0f : 1.0f));  // [0x1800582b2]
        break;
    case 3: {
        float v[4] = { 0.0f, 0.0f, 0.0f, 0.0f };                       // [0x180058285]
        effect->SetVector(param, reinterpret_cast<const D3DXVECTOR4*>(v));
        break;
    }
    case 4: {
        float v[4] = { 0.0f, 0.0f, 0.0f, 1.0f };                       // [0x180058243]
        effect->SetVector(param, reinterpret_cast<const D3DXVECTOR4*>(v));
        break;
    }
    default: {
        D3DXMATRIX m;
        if (kindGuess == 0) {
            D3DXMatrixScaling(&m, 10.0f, 10.0f, 10.0f);                // [0x1800581b6]
        } else {
            D3DXMatrixIdentity(&m);                                   // [0x1800581d0]
        }
        effect->SetMatrix(param, &m);                                  // [0x18005822b]
        break;
    }
    }
}

void ResolveOneControl(SasEffect* sas, const SasControlObject& control,
                       ModelData* owner, ModelData* offscreenOwner)
{
    ID3DXEffect* effect = sas->effect;
    if (effect == nullptr || control.param == nullptr) {
        return;
    }

    // --- object resolution [0x180057c0f-0x180057ea2] ---
    ModelData* target = FindModelByName(control.objectName, owner);
    if (target == nullptr) {
        if (control.objectName == "(OffscreenOwner)" &&
            offscreenOwner != nullptr) {
            // [0x180057d58-0x180057da8] the offscreen owner: the setter's
            // +24 field [0x18002d0ca], which sub_18002CA80 fills at
            // 0x18002d0a2 ONLY while the assignment walk renders into an
            // offscreen scene (scene id a4 != 0): the id -> offscreen-record
            // registration (map@ctx+0xB8) yields the record whose +0x30 is
            // the object carrying the effect that declares the
            // OFFSCREENRENDERTARGET (e.g. ray.x). Outside the window the
            // field is null, the name falls through "(self)" /
            // "(AttachedModel)" and lands in the not-found defaults below -
            // the original's failure behavior, kept verbatim.
            target = offscreenOwner;
        } else if (control.objectName == "(self)") {
            // [0x180057e02] the object the effect is assigned to (the
            // setter's +16 field [0x18002d0c6]): the DRAWN object - for an
            // offscreen DefaultEffect row that is the row-matched model, not
            // the offscreen owner.
            target = owner;
        } else if (control.objectName == "(AttachedModel)") {
            // [0x180057e63-0x180057e9a] only when the owner itself is an
            // accessory (kind +0x28 == 0): GetAcsAttachedPmd(owner id) ->
            // the attached model via the id map (sub_1800555c0).
            if (owner != nullptr && owner->kind() == 0) {
                unsigned long long attachedId = 0;
                int attachedBone = -1;
                if (GetAcsAttachedPmd(owner->objectId(), &attachedId,
                                      &attachedBone)) {
                    target = FindModelById(attachedId);
                }
            }
        }
    }

    D3DXPARAMETER_DESC pd;
    memset(&pd, 0, sizeof(pd));
    if (effect->GetParameterDesc(control.param, &pd) != S_OK) {
        return;
    }
    const int typeCode = ControlParameterTypeCode(pd);

    if (target == nullptr) {
        // [0x180058133-0x18005830d] object not found: write the defaults.
        SetControlDefaults(effect, control.param, typeCode,
                           !control.itemName.empty(),
                           KindGuessForName(control.objectName));
        return;
    }

    const int hostIndex = target->passPlanScratch().passKey;   // +0xf0
    const bool hasItem = !control.itemName.empty();

    // --- value staging [sub_180059690's 16-float buffer]: either a full
    // matrix (matrixResult) or a value (triple) in floats 12-14 of an
    // identity matrix, float 15 keeping the 1.0. ---
    float buffer[16];
    for (int r = 0; r < 4; ++r) {
        for (int c = 0; c < 4; ++c) {
            buffer[r * 4 + c] = (r == c) ? 1.0f : 0.0f;
        }
    }
    bool matrixResult = false;

    if (!hasItem) {
        // [0x180057eb2] whole-object reference: the object world matrix at
        // ModelData+0xf8 (the port's planMatrix); eax = 0x10 marks the
        // matrix result (matrix/vector/scalar all derive from it).
        memcpy(buffer, &target->planMatrix(), sizeof(buffer));
        matrixResult = true;
    } else if (owner != nullptr && owner->kind() == 0 &&
               control.itemName == "(AttachedBone)") {
        // [0x180057eef-0x18005802c] the "(AttachedBone)" item on an
        // accessory owner: the attached bone's world matrix when the named
        // object IS the owner's attached model (and a bone exists); the
        // zero/identity buffer otherwise (result 0).
        unsigned long long attachedId = 0;
        int attachedBone = -1;
        ModelData* attached = nullptr;
        if (GetAcsAttachedPmd(owner->objectId(), &attachedId, &attachedBone)) {
            attached = FindModelById(attachedId);
        }
        if (attached == target && attachedBone >= 0 && hostIndex >= 0) {
            D3DMATRIX world = ExpGetPmdBoneWorldMat(hostIndex, attachedBone);
            memcpy(buffer, &world, sizeof(buffer));
            matrixResult = true;
        }
    } else if (const int* index = target->findNameIndex(control.itemName)) {
        if (target->kind() == 1) {
            if (*index < 0) {
                // [0x180059761] morph (encoded -1, -2, ...): the value lands
                // in float 12.
                buffer[12] = ExpGetPmdMorphValue(hostIndex, -(*index) - 1);
            } else if (hostIndex >= 0) {
                // [0x180059728] bone: the full world matrix.
                D3DMATRIX world = ExpGetPmdBoneWorldMat(hostIndex, *index);
                memcpy(buffer, &world, sizeof(buffer));
                matrixResult = true;
            }
        } else if (hostIndex >= 0) {
            // [0x180059790] accessory pseudo-bones, read live from the
            // panel. 0/1/2 = X/Y/Z, 4/5/6 = Rx/Ry/Rz, 8 = Si, 9 = Tr write
            // float 12; the combos 3 (XYZ) and 7 (Rxyz) write the triple
            // into floats 12-14.
            switch (*index) {
            case 0: buffer[12] = ExpGetAcsX(hostIndex); break;
            case 1: buffer[12] = ExpGetAcsY(hostIndex); break;
            case 2: buffer[12] = ExpGetAcsZ(hostIndex); break;
            case 3:
                buffer[12] = ExpGetAcsX(hostIndex);
                buffer[13] = ExpGetAcsY(hostIndex);
                buffer[14] = ExpGetAcsZ(hostIndex);
                break;
            case 4: buffer[12] = ExpGetAcsRx(hostIndex); break;
            case 5: buffer[12] = ExpGetAcsRy(hostIndex); break;
            case 6: buffer[12] = ExpGetAcsRz(hostIndex); break;
            case 7:
                buffer[12] = ExpGetAcsRx(hostIndex);
                buffer[13] = ExpGetAcsRy(hostIndex);
                buffer[14] = ExpGetAcsRz(hostIndex);
                break;
            case 8: buffer[12] = ExpGetAcsSi(hostIndex); break;
            case 9: buffer[12] = ExpGetAcsTr(hostIndex); break;
            default: break;   // unknown pseudo index: the identity stays
            }
        }
    }
    // else: unknown item name - sub_180059690 returns 0 leaving its identity
    // buffer, which yields the 0 / zero-vector / identity defaults below.

    // --- push by parameter shape [0x180058042] ---
    switch (typeCode) {
    case 0:
        // [0x180058125] bool = the OBJECT's display flag (+0xf4), read with
        // or without an item name (the item is ignored for bools).
        effect->SetBool(control.param,
                        (hostIndex >= 0)
                            ? ((target->kind() == 1)
                                   ? (ExpGetPmdDisp(hostIndex) ? TRUE : FALSE)
                                   : (ExpGetAcsDisp(hostIndex) ? TRUE : FALSE))
                            : FALSE);
        break;
    case 1:
        // [0x1800580cd] scalar: sqrt of the buffer's FIRST ROW floats for a
        // matrix result, else the value slot (float 12).
        effect->SetFloat(control.param, matrixResult
            ? sqrtf(buffer[0] * buffer[0] + buffer[1] * buffer[1] +
                    buffer[2] * buffer[2])
            : buffer[12]);
        break;
    case 3:
        // [0x1800580b4] float3: the buffer floats 12-14.
        {
            float v[4] = { buffer[12], buffer[13], buffer[14], 0.0f };
            effect->SetVector(control.param,
                              reinterpret_cast<const D3DXVECTOR4*>(v));
        }
        break;
    case 4:
        // [0x18005806e] float4: the buffer floats 12-15 (w = the buffer's
        // float 15 - 1.0 unless a bone/object matrix overwrote it).
        {
            float v[4] = { buffer[12], buffer[13], buffer[14], buffer[15] };
            effect->SetVector(control.param,
                              reinterpret_cast<const D3DXVECTOR4*>(v));
        }
        break;
    default:
        // [0x18005822b] matrix: the buffer as-is.
        {
            D3DXMATRIX m;
            memcpy(&m, buffer, sizeof(m));
            effect->SetMatrix(control.param, &m);
        }
        break;
    }
}

}  // namespace

// Evaluated once per assignment in the planner's traversal order. CloneEffect
// preserves private state; explicitly shared parameters still follow pool order.
void MmeUpdateControlObjects(ModelData* model, MaterialBinding* binding,
                             ModelData* offscreenOwner)
{
    if (!binding || !binding->sas) return;
    SasEffect* sas = binding->sas;
    for (const auto& control : sas->controls)
        ResolveOneControl(sas, control, model, offscreenOwner);
}

} // namespace mme
