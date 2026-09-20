// material_bind.h - the material/effect parameter binding of MMEffect.dll.
//
// Evidence:
//   - MME_ApplyModelRenderSnapshot [0x18005a1e0] (kit decompile):
//       memcpy(ModelData+0x138, snapshot, 0x220);
//       if (ModelData+0xe8 != snapshot+0x30 draw_type_index) {
//           ModelData+0xe8 = draw_type_index;
//           MME_SelectMaterialEffectBinding(model, bindingContext, ModelData+0x138);
//       }
//       count = bindingContext ? *(u32*)(bindingContext+0x38) : 0;
//       binding = FUN_18002d910(manager, count, model, snapshot+0x28 subset, 1);
//       if (binding) FUN_18001b940(binding, draw_type_index, subset,
//                                  snapshot+0x55 effect_file_used,
//                                  0 < snapshot+0x50 sphere_mode,
//                                  snapshot+0x54 toon_used, model+0x08);
//   - MME_SelectMaterialEffectBinding [0x18005a020]: sets ModelData+0xe0 =
//     &ModelData+0x138, looks up the (count, model, subset) node of the
//     manager's std::map (+0xa0) and re-applies it (FUN_18001b340); then for
//     renderClass == 0 with materialCount > 0 walks subsets 0..count-1
//     re-applying each existing (count, model, i) node.
//   - FUN_18002d910: the std::map lower_bound over the key
//     (u32 materialCount, ModelData*, int subset); when the exact subset is
//     missing and the flag is set, retries once with subset = -1 (the
//     "whole object" binding).
//   - MME_BindMaterialParameter [0x18005eff0]: semantic ids
//       0x18 Diffuse / 0x19 Ambient / 0x1A Emissive / 0x1B Specular
//       ([0x18005eff0 tail switch] 0x1A selects the material +0x30 block
//       (Emissive), 0x1B the +0x20 block (Specular) - swapped relative to
//       the D3DMATERIAL9 field order; [0x18005f550] 0x1B also slices the
//       light Specular. From device GetMaterial for draw types 1/3, zeroed
//       material for 4, ExpGetAcsMaterial/ExpGetPmdMaterial
//       otherwise), 0x1C ToonColor (snapshot+0x1a8 on the effect path),
//       0x1D EdgeColor (snapshot+0x198 on the effect path, edge pass only),
//       0x1E SpecularPower (Power, 0.0f -> 0.1f), 0x4E GroundShadowColor
//       (shadow pass only, material x light composition).
//   - MME_BindBooleanParameter [0x18005fb40]: semantic ids
//       0x3C parthf (read back from the current effect), 0x3D spadd
//       (sphere_mode == 2), 0x3E transp (snapshot+0x218), 0x3F use_texture
//       (snapshot+0x56), 0x40 use_spheremap (0 < sphere_mode), 0x41
//       use_subtexture (sphere_mode == 3), 0x42 use_toon (snapshot+0x54),
//       0x45 opadd (GetBlendMode() == 1).
//   - The effect-owner manager std::map (+0xa0): key (u32 count, ModelData*,
//     int subset), value = the binding object (globals_structures.md section 4).
//
// Phase 2 boundary: the SAS script/technique machinery (Phase 3) owns the
// render-turn records (one per OFFSCREENRENDERTARGET resource of the
// binding's effect - the original's binding+0x1B8 vector, filled by the SAS
// parser for semanticId 0x2E only; see MmeCollectSceneOffscreens in
// pass_planner.cpp); the binding application here performs the
// standard-parameter set through the name-table handles (the original's
// 0x1800B36A0 table: the 17 matWorld..SphCMul names plus use_*/opadd/
// VertexCount/SubsetCount) resolved by enumeration + _stricmp.
// MATERIALTEXTURE/MATERIALSPHEREMAP/MATERIALTOONTEXTURE bind by semantic.
// CONTROLOBJECT parameter values are deferred to Phase 3.
#pragma once

#include <cstddef>
#include <cstring>
#include <map>
#include <memory>
#include <string>
#include <vector>

#include <d3d9.h>
#include <d3dx9.h>

#include "render_snapshot.h"

namespace mme {

struct SasEffect;
struct LoadedEffect;
class ModelData;

// One binding object (the manager map value; original node+0x30). It caches
// the per-effect handles that the standard apply resolves by name.
struct MaterialBinding {
    ID3DXEffect* effect;                              // the target effect
    std::string  effectPath;                          // assigned file ("" = pool effect)
    // [原版 0x1D8 绑定对象 +0x08/+0x10] 对缓存条目（LoadedEffect）的
    // boost::shared_ptr 引用（px/pn 两个字）。原版绑定每次重建销毁时在
    // FUN_18000B210 尾部释放该引用（0x18000b5e9-0x18000b611 的控制块
    // InterlockedDecrement → dispose/destroy 链），使条目活到“最后一个引
    // 用它的绑定销毁”。移植以 std::shared_ptr 承载同一语义：上面的
    // effect/sas 是 owner 内部指针的借用缓存（owner 活着则必然有效），
    // 绑定析构（delete binding，含所有 MmeDropMaterialBindings /
    // manager dtor / ctx dtor / offscreen 瞬态清空路径）释放引用，最后
    // 一个引用死亡时 ~LoadedEffect 执行 FUN_18000B210 的完整卸载语义
    // （日志 / SasUnload / effect->Release）。
    std::shared_ptr<LoadedEffect> owner;
    // The name-table handles (the original's 0x1800B36A0 table: matWorld ..
    // SphCMul plus use_*/opadd/VertexCount/SubsetCount). Keyed by the table's
    // canonical spelling and resolved once per binding by ENUMERATING the
    // effect's parameters and _stricmp-matching desc.Name (0x18000e811) -
    // case variants like `Matworld` bind, exactly like the original.
    // [零分配热路径] std::less<> 透明比较器：find(字面量) 走 string 与
    // const char* 的 operator< 直接比较，不构造临时 std::string（原版
    // sub_18001B5B0 走预编译 id 表，每 pass 零分配——本表对齐该成本模型；
    // rayMMD 的 ~31-pass × 10-轮 × 30fps 会把任何每 pass 堆分配放大成
    // 每秒数十万次 malloc/free，见 MmeBindSemanticParameters 的同向注释）。
    std::map<std::string, D3DXHANDLE, std::less<>> namedHandles;
    bool namesResolved;                               // namedHandles enumeration ran
    D3DXHANDLE   technique;                           // legacy slot (kept null: the
                                                     // original has no MainTec0
                                                     // convention - a null per-draw
                                                     // selection draws raw)
    unsigned int passCount;                           // passes in the selected technique

    // The parsed SAS model of the assigned effect (null for plain effects)
    // and the technique the scene-effect drivers step/resume - EVERY render
    // turn of the carrier walks this one (first script) technique; the
    // original's (turn id, carrier) binding records store no per-turn
    // technique choice (sub_18005A410 reads the record's techniques[0]).
    // For object effects the technique table below drives the draw instead.
    SasEffect* sas;
    int        sceneTechIndex;

    // Per-draw-type technique slot indexed by the host's
    // ExpGetCurrentTechnic value (1 = object, 2 = object_ss, 3 = shadow,
    // 4 = edge, 5 = z-plot). MmeApplyModelRenderSnapshot refreshes it per
    // draw from the selection table below [FUN_18001b940's table lookup];
    // a null entry falls back to `technique` (also null) -> the raw host
    // draw, exactly the original's no-selection forward.
    D3DXHANDLE  techniques[6];
    unsigned int passCounts[6];

    // [sub_18001DD20 precompute / FUN_18001b940 lookup] the per-MmdPass
    // technique-selection table, built once per effect assignment with the
    // model's material count. Modes 0/1 (object, object_ss) hold
    // materialCount*8 (handle, passCount) entries indexed subset*8 +
    // (useTexture | useSpheremap<<1 | useToon<<2); modes 2/3/4 (shadow,
    // edge, zplot) hold materialCount entries indexed by subset alone
    // (selected with every Use* state false). A null handle entry is "no
    // technique -> raw host draw". The table stays empty when the effect
    // has no SAS model or the material count is 0 - every lookup then
    // misses, which is the original's empty-table behavior (sub_18001DD20's
    // a2 <= 0 early-out).
    std::vector<std::pair<D3DXHANDLE, unsigned int> > drawTechniques[5];
    bool drawTableBuilt;

    // Semantic-resolved handles (the SAS standard parameters ray-mmd style
    // effects declare: float4x4 matWorldViewProject : WORLDVIEWPROJECTION
    // etc.). Resolved once per binding through GetParameterBySemantic.
    // [零分配热路径] semanticFast 把每个语义参数的分发决策（kind/base/
    // flags/light）在首次绑定时一次定型；每 pass 的绑定循环只做 switch
    // 分发，不做任何字符串构造/注解读取（对齐原版 sub_18001B5B0 的预编
    // 译 id 表成本模型）。semanticHandles 仅保留给旧路径兼容。
    struct SemanticBind {
        D3DXHANDLE    handle;
        unsigned char kind;    // 分发类别（kSem* 常量，material_bind.cpp）
        unsigned char base;    // 矩阵基底/材质 id/时间序号/纹理槽/鼠标键
        unsigned char flags;   // 矩阵：bit0=INVERSE bit1=TRANSPOSE；调制：bit0=mul bit1=sphere
        unsigned char light;   // 1 = Light/Light0 家族
    };
    std::vector<SemanticBind> semanticFast;
    std::vector<D3DXHANDLE> semanticHandles;
    bool semanticsResolved;

    MaterialBinding()
        : effect(nullptr), technique(nullptr), passCount(0),
          sas(nullptr), sceneTechIndex(-1), drawTableBuilt(false),
          namesResolved(false), semanticsResolved(false) {
        for (int i = 0; i < 6; ++i) {
            techniques[i] = nullptr;
            passCounts[i] = 0;
        }
    }
};

// [0x18005a020] MME_SelectMaterialEffectBinding: ensure the (count, model,
// subset) bindings exist for the model's assigned effect, then re-apply the
// binding for the snapshot subset and, for renderClass 0 models, every
// material subset.
void MmeSelectMaterialEffectBinding(ModelData* model, void* bindingContext,
                                    const RenderSnapshot& snap);

// [0x18005eff0] MME_BindMaterialParameter: SetVector/SetMatrix for the
// material-color semantics (0x18-0x1E, 0x4E). `count` follows the original's
// SetVector count parameter (4 = float4, 1 = float scalar splat for 0x1E).
void MmeBindMaterialParameter(ModelData* model, ID3DXEffect* effect, int semantic,
                              D3DXHANDLE handle, int count);

// [0x18005fb40] MME_BindBooleanParameter: SetBool for the boolean semantics
// (0x3C-0x45).
void MmeBindBooleanParameter(ModelData* model, ID3DXEffect* effect, int semantic,
                             D3DXHANDLE handle);

// The Phase 2 standard apply (what FUN_18001b940's op dispatch reduces to
// without the SAS op arrays): set the 17 cached standard parameters plus the
// name-resolved extras for the model/effect pair. Handles are resolved
// against the TARGET effect (the g_paramHandles cache belongs to the host
// effect captured at Initialize and is invalid for model effects).
void MmeBindStandardParameters(ModelData* model, MaterialBinding* binding,
                               const RenderSnapshot& snap);

// The SAS semantic binding layer (WORLD/WORLDVIEW/WORLDVIEWPROJECTION/
// VIEW.../POSITION/DIRECTION/OBJECT-annotated Light-Camera dispatch,
// VIEWPORTPIXELSIZE/TIME/ELAPSEDTIME/MOUSEPOSITION). Resolved through
// GetParameterBySemantic once per binding; called from the standard apply.
void MmeBindSemanticParameters(ModelData* model, MaterialBinding* binding,
                               const RenderSnapshot& snap);

// CONTROLOBJECT value staging (Phase 3 seam). The SAS interpreter resolves
// CONTROLOBJECT parameters from the host: accessory panel values
// (ExpGetAcsX/Y/Z/Rx/Ry/Rz/Si/Tr), bone world matrices (ExpGetPmdBoneWorldMat)
// and morph values (ExpGetPmdMorphValue). Phase 2 stages the raw host values
// per scanned object (called once per object from the pass planner) so the
// Phase 3 resolver only needs the name lookup. The staged table is read-only
// in Phase 2.
struct ControlObjectStage {
    bool      valid;        // staged for the current frame
    float     panel[8];     // X Y Z Rx Ry Rz Si Tr (accessories)
    D3DMATRIX boneWorld;    // ExpGetPmdBoneWorldMat (models, bone 0 placeholder)
    float     morphValue;   // ExpGetPmdMorphValue (models, morph 0 placeholder)
    ControlObjectStage() : valid(false), morphValue(0.0f)
    {
        for (int i = 0; i < 8; ++i) {
            panel[i] = 0.0f;
        }
        memset(&boneWorld, 0, sizeof(boneWorld));
        boneWorld.m[0][0] = boneWorld.m[1][1] = boneWorld.m[2][2] = boneWorld.m[3][3] = 1.0f;
    }
};
ControlObjectStage* MmeStageControlObjectValues(ModelData* model, int hostIndex);
ControlObjectStage* MmeFindControlObjectStage(ModelData* model);

// Accessory attach resolution (MMHack GetAcsAttachedPmd). Called by the pass
// planner for kind != 1 objects with the accessory ID; records the attached
// model id + bone index on the ModelData (the CONTROLOBJECT
// "(AttachedModel)"/"(AttachedBone)" naming consumes it in Phase 3).
void MmeResolveAccessoryAttach(ModelData* model, unsigned long long accessoryId);

// Manager-map accessors (the EffectOwnerManager +0xa0 tree). Implemented
// against mme_context.h's EffectOwnerManager; exposed here for the emm
// manager and the Phase 3 pass records. `owner` carries the engine-cache
// reference the binding borrows effect/sas from (the original 0x1D8 object's
// +0x08/+0x10 shared_ptr); a null owner leaves the binding effect-less.
MaterialBinding* MmeFindMaterialBinding(unsigned int materialCount, ModelData* model,
                                        int subsetIndex, bool allowWholeObject);
MaterialBinding* MmeEnsureMaterialBinding(unsigned int materialCount, ModelData* model,
                                          int subsetIndex, ID3DXEffect* effect,
                                          const std::string& effectPath,
                                          const std::shared_ptr<LoadedEffect>& owner);
void MmeDropMaterialBindings(ModelData* model);

// Resolve (and cache) the whole-object binding for the model's assigned
// effect: load the file through the engine cache, create the (0, model, -1)
// binding, build the per-MmdPass selection table (sub_18001DD20: MmdPass +
// Subset + UseTexture/UseSpheremap/UseToon against the material state,
// indexed per draw by FUN_18001b940) and, when the effect is a
// scene/sceneorobject class, write the SAS class/order into the ModelData
// (+0x360/+0x364/+0x368) so the pass planner picks the object up as a pass
// record. Returns null when the model has no assigned effect or the effect
// is unavailable.
MaterialBinding* MmeResolveModelEffectBinding(ModelData* model);

// Resolve one explicit per-material assignment. Unlike the whole-object
// binding this never changes the carrier's scene/pass classification.
MaterialBinding* MmeResolveSubsetEffectBinding(ModelData* model, int subsetIndex,
                                                const std::string& effectPath);

// The whole-object binding lookup used by the draw path (the same (0, model,
// -1) key MmeResolveModelEffectBinding creates). Returns null when the model
// has no binding.
MaterialBinding* MmeActiveModelBinding(ModelData* model);

// --- offscreen DefaultEffect (sub_180011960 offscreen record +0x78) ---
// Resolve the active turn's DefaultEffect assignment by first matching row.
// Resource declaration does not grant the object its root binding in a turn.
// Missing, none and hide return null; the caller distinguishes missing/hide
// (no draw) from none (host geometry) with the predicates below.
MaterialBinding* MmeResolveOffscreenDefaultBinding(ModelData* model, int subsetIndex = -1);

// [sub_18005A1E0 0x18005a24c-0x18005a268] 离屏渲染回合（offscreen render
// turn）判定：原版以 ctx+0x168 的 0x48 回合包装（wrapper）非空为窗口——
// wrapper+0x38 是当前 turn id（MME_ApplyModelRenderSnapshot 以它为 owner
// 键首字段查绑定），wrapper 为空即基回合（turn id 0，场景键 0 查找）。
// 移植以 ctx->currentBindingOffscreen（pass_planner 在回合应用时按
// renderPassList[lastRepeatCount-1] 设置、基回合清空——FUN_18005d130
// [L74740]/[L74743-74745] 的移植对应物）为 wrapper 的资源身份等价物：
// 非 null 即离屏回合。窗口内的绑定查找一律不得回落到场景键 0（模型
// 自身的 (0, model, -1) 绑定对 turn-id 键不可见）。
bool MmeInOffscreenRenderTurn();

// Distinguish an absent turn mapping (no draw) from an explicit "none"
// mapping (draw host geometry without an effect).
bool MmeHasOffscreenDefaultEffectRow(ModelData* model);

// True when the model's matched offscreen DefaultEffect row is "hide" - the
// model must not be drawn into the offscreen target at all.
bool MmeOffscreenDefaultEffectHides(ModelData* model);

// Destroy every transient offscreen-DefaultEffect binding and drop the staged
// row vector (the resume walk / plan reset entry).
void MmeClearOffscreenDefaultBindings();

// Per-frame CONTROLOBJECT resolution for every loaded effect (the original's
// EffectFrameParamSetter walk, sub_180057BC0): resolve each control
// parameter's object/item against the live ModelData name tables and push
// the value into the effect. Called once per frame from the pass planner.
void MmeUpdateControlObjects();

} // namespace mme
