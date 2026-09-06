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
//       0x18 Diffuse / 0x19 Ambient / 0x1A Specular / 0x1B Emissive
//       (mme_material_semantics.csv; from device GetMaterial for draw types
//       1/3, zeroed material for 4, ExpGetAcsMaterial/ExpGetPmdMaterial
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
// per-technique pass records; the binding application here performs the
// standard-parameter set through the 17 cached D3DXHANDLEs plus the
// name-resolved extra parameters (use_*/opadd/_INDEX/VertexCount/SubsetCount,
// MATERIALTEXTURE/MATERIALSPHEREMAP/MATERIALTOONTEXTURE). CONTROLOBJECT
// parameter values are deferred to Phase 3.
#pragma once

#include <cstddef>
#include <cstring>
#include <map>
#include <string>

#include <d3d9.h>
#include <d3dx9.h>

#include "render_snapshot.h"

namespace mme {

class ModelData;

// One binding object (the manager map value; original node+0x30). It caches
// the per-effect handles that the standard apply resolves by name.
struct MaterialBinding {
    ID3DXEffect* effect;                              // the target effect
    std::string  effectPath;                          // assigned file ("" = pool effect)
    std::map<std::string, D3DXHANDLE> namedHandles;   // use_*/opadd/_INDEX/... by name
    D3DXHANDLE   technique;                           // selected MainTec technique
    unsigned int passCount;                           // passes in the selected technique

    MaterialBinding() : effect(nullptr), technique(nullptr), passCount(0) {}
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
// manager and the Phase 3 pass records.
MaterialBinding* MmeFindMaterialBinding(unsigned int materialCount, ModelData* model,
                                        int subsetIndex, bool allowWholeObject);
MaterialBinding* MmeEnsureMaterialBinding(unsigned int materialCount, ModelData* model,
                                          int subsetIndex, ID3DXEffect* effect,
                                          const std::string& effectPath);
void MmeDropMaterialBindings(ModelData* model);

// Resolve (and cache) the whole-object binding for the model's assigned
// effect: load the file through the engine cache, create the (0, model, -1)
// binding and select the MainTec technique. Returns null when the model has
// no assigned effect or the effect/technique is unavailable.
MaterialBinding* MmeResolveModelEffectBinding(ModelData* model);

// The whole-object binding lookup used by the draw path (the same (0, model,
// -1) key MmeResolveModelEffectBinding creates). Returns null when the model
// has no binding.
MaterialBinding* MmeActiveModelBinding(ModelData* model);

} // namespace mme
