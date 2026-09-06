// material_bind.cpp - see material_bind.h
#include "material_bind.h"

#include <cstdio>
#include <cstring>
#include <memory>

#include "MMDExport.h"   // ExpGetPmdMaterial / ExpGetAcsMaterial
#include "mmhack_api.h"  // GetBlendMode / GetCurrentEffect

#include "effect_engine.h"
#include "mme_context.h"
#include "mme_log.h"
#include "mme_globals.h"
#include "model_data.h"

namespace mme {

namespace {

// [0x18005ee60] FUN_18005ee60 - fixed-function edge-color read: walks
// index[start_index] of the bound index buffer, then reads the first DWORD of
// that vertex from stream 0 (MMD edge vertices carry the edge color as the
// vertex diffuse). Ported with the standard device calls
// (GetIndices/GetStreamSource + Lock with D3DLOCK_READONLY|NOSYSLOCK = 0x1010,
// matching big-C 76042-76067). Returns 0 when the read is impossible.
unsigned int MmeReadVertexDiffuseColor(IDirect3DDevice9* device, int baseVertexIndex,
                                       int startIndex)
{
    if (device == nullptr) {
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
        value = *static_cast<unsigned int*>(locked);
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
        // object during the draw-type-1 pass scales rgba by DAT_1800b5c20
        // (= 10.0f). UNCERTAIN rationale; ported verbatim.
        if (semantic == 0x18 && model != nullptr && model->kind() == 0 && drawType == 1) {
            material.Diffuse.r *= 10.0f;
            material.Diffuse.g *= 10.0f;
            material.Diffuse.b *= 10.0f;
            material.Diffuse.a *= 10.0f;
        }
    } else if (drawType == 4) {
        // [big-C 156-160] the edge pass zeroes the material (0x44 memset).
        memset(&material, 0, sizeof(material));
    } else {
        // [big-C 161-188] ExpGetAcsMaterial / ExpGetPmdMaterial(hostIndex,
        // subsetIndex). hostIndex lives at ModelData+0xf0 (the passKey
        // scratch, filled by MmeRefreshObjectPlan), subset at snapshot+0x28.
        int hostIndex = model != nullptr ? model->passPlanScratch().passKey : -1;
        int subset = snap.subset_index;
        if (model != nullptr && model->kind() == 0) {
            material = ExpGetAcsMaterial(hostIndex, subset);
        } else {
            material = ExpGetPmdMaterial(hostIndex, subset);
        }
    }

    // [big-C 189-218] slice selection:
    //   0x18 -> +0x00 Diffuse, 0x19 -> +0x10 Ambient, 0x1A -> +0x20 Specular
    //   (the switch selects the third block), 0x1B -> +0x30 Emissive.
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
        out[0] = material.Specular.r; out[1] = material.Specular.g;
        out[2] = material.Specular.b; out[3] = material.Specular.a;
        break;
    case 0x1B:
        out[0] = material.Emissive.r; out[1] = material.Emissive.g;
        out[2] = material.Emissive.b; out[3] = material.Emissive.a;
        break;
    default:
        out[0] = out[1] = out[2] = out[3] = 0.0f;
        break;
    }
    *power = material.Power;
}

} // namespace

// ---------------------------------------------------------------------------
// Effect-owner manager map (DAT_1800d9a40 + 0xa0)
// ---------------------------------------------------------------------------

MaterialBinding* MmeFindMaterialBinding(unsigned int materialCount, ModelData* model,
                                        int subsetIndex, bool allowWholeObject)
{
    // [0x18002d910] FUN_18002d910: exact (count, model, subset) lookup; when
    // missing, subset >= 0 and the flag is set, retry once with subset = -1.
    EffectOwnerManager* manager = g_ownerManager;
    if (manager == nullptr || model == nullptr) {
        return nullptr;
    }
    (void)allowWholeObject;   // the original's flag is always 1 at both call sites
    int subset = subsetIndex < 0 ? -1 : subsetIndex;
    bool retry = true;
    while (true) {
        std::map<EffectOwnerManager::BindingKey, MaterialBinding*>::const_iterator it =
            manager->bindings.find(
                EffectOwnerManager::BindingKey(materialCount, model, subset));
        if (it != manager->bindings.end()) {
            return it->second;
        }
        // [big-C 36951-36956] `if ((param_4 < 0) || (param_5 == 0)) return 0;`
        if (subset < 0 || !retry) {
            return nullptr;
        }
        retry = false;   // [big-C 36955] param_5 = 1; param_4 = -1
        subset = -1;
    }
}

MaterialBinding* MmeEnsureMaterialBinding(unsigned int materialCount, ModelData* model,
                                          int subsetIndex, ID3DXEffect* effect,
                                          const std::string& effectPath)
{
    EffectOwnerManager* manager = g_ownerManager;
    if (manager == nullptr || model == nullptr) {
        return nullptr;
    }
    int subset = subsetIndex < 0 ? -1 : subsetIndex;
    EffectOwnerManager::BindingKey key(materialCount, model, subset);
    std::map<EffectOwnerManager::BindingKey, MaterialBinding*>::iterator it =
        manager->bindings.find(key);
    if (it != manager->bindings.end() && it->second != nullptr) {
        it->second->effect = effect;
        it->second->effectPath = effectPath;
        return it->second;
    }
    MaterialBinding* binding = new MaterialBinding();
    binding->effect = effect;
    binding->effectPath = effectPath;
    manager->bindings[key] = binding;
    return binding;
}

void MmeDropMaterialBindings(ModelData* model)
{
    // [0x18002a430 / FUN_180058740] erase every binding of the model.
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

    // [kit L25-27] count = bindingContext ? *(u32*)(bindingContext+0x38) : 0.
    unsigned int count = 0;
    if (bindingContext != nullptr) {
        count = *reinterpret_cast<unsigned int*>(
            reinterpret_cast<unsigned char*>(bindingContext) + 0x38);
    }

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
    memcpy(&model->snapshot(), &snap, 0x220);

    // [kit L16-24] technique (re)selection when the draw-type index changed.
    if (model->drawTypeIndex() != drawTypeIndex) {
        model->setDrawTypeIndex(drawTypeIndex);
        MmeSelectMaterialEffectBinding(model, bindingContext, model->snapshot());
    }

    // [kit L25-33] count from the binding context (+0x38, 0 when null).
    unsigned int count = 0;
    if (bindingContext != nullptr) {
        count = *reinterpret_cast<unsigned int*>(
            reinterpret_cast<unsigned char*>(bindingContext) + 0x38);
    }

    // [kit L31-37] FUN_18002d910 + FUN_18001b940(binding, draw_type_index,
    // subset, effect_file_used, 0 < sphere_mode, toon_used, model+0x08).
    MaterialBinding* binding = MmeFindMaterialBinding(count, model, snap.subset_index, true);
    if (binding == nullptr) {
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
        // FUN_18001b940's op walk reduces to the standard parameter set in
        // Phase 2; the pass state itself is applied by the draw wrapper in
        // callbacks.cpp (ID3DXEffect::Begin/BeginPass around the forwarded
        // draw, the observable equivalent of the original's op array).
        MmeBindStandardParameters(model, binding, snap);
    }
}

// ---------------------------------------------------------------------------
// MmeResolveModelEffectBinding - the Phase 2 binding creation. The original
// builds the binding (and its per-technique op arrays) when an effect is
// assigned to an object; the port resolves lazily on the first draw. The
// technique selection follows the MME convention (MainTec0, then the numbered
// MainTec variants); a MainTec0-less effect binds with no technique and the
// draws fall back to the host pipeline.
// ---------------------------------------------------------------------------
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
                                                        loaded->effect, path);
    if (binding == nullptr || binding->effect == nullptr) {
        MmeLogWrite("BindResolve: no binding/effect\n", 0);
        return binding;
    }
    if (binding->technique == nullptr) {
        D3DXHANDLE technique = binding->effect->GetTechniqueByName("MainTec0");
        if (technique != nullptr) {
            D3DXTECHNIQUE_DESC desc;
            memset(&desc, 0, sizeof(desc));
            if (SUCCEEDED(binding->effect->GetTechniqueDesc(technique, &desc))) {
                binding->passCount = desc.Passes;
            }
            binding->technique = technique;
        }
    }
    {
        static std::string loggedPath;
        if (loggedPath != path) {
            loggedPath = path;
            char line[0x220];
            sprintf_s(line, sizeof(line),
                      "BindResolve: effect=%p technique=%p passes=%u\n",
                      (void*)binding->effect, (void*)binding->technique,
                      binding->passCount);
            MmeLogWrite(line, 0);
        }
    }
    return binding;
}

MaterialBinding* MmeActiveModelBinding(ModelData* model)
{
    if (model == nullptr) {
        return nullptr;
    }
    return MmeFindMaterialBinding(0, model, -1, true);
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
        // all-1.0 fallback (DAT_1800b5b28). Fixed-function path: the original
        // derives the color from the stage-0 texture (FUN_180056c30); Phase 2
        // approximates that fallback with the same all-1.0 vector
        // (PHASE2_IMPLEMENTATION_NOTES.md, UNCERTAIN).
        if (snap.effect_file_used != 0) {
            if (model->kind() == 1 && snap.draw_type == 2) {
                value[0] = snap.toon_color.r;
                value[1] = snap.toon_color.g;
                value[2] = snap.toon_color.b;
                value[3] = snap.toon_color.a;
            } else {
                value[0] = value[1] = value[2] = value[3] = 1.0f;
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
        // GroundShadowColor [big-C 100-141]. Only during the shadow pass
        // (raw draw type 3); material x light composition (kind==1 scales the
        // alpha by DAT_1800b68b8 = 0.65f).
        if (snap.draw_type != 3) {
            return;
        }
        float materialValue[4];
        float power = 0.0f;
        MmeComposeMaterialColor(model, snap, 0x18, materialValue, &power);
        D3DLIGHT9& light = g_cachedLight;   // DAT_1800d9890
        value[0] = materialValue[0] * light.Diffuse.r;
        value[1] = materialValue[1] * light.Diffuse.g;
        value[2] = materialValue[2] * light.Diffuse.b;
        value[3] = materialValue[3] *
                   ((model->kind() == 1) ? 0.65f : light.Diffuse.a);
        break;
    }
    case 0x18:
    case 0x19:
    case 0x1A:
    case 0x1B: {
        // Diffuse / Ambient / Specular / Emissive (Geometry) [big-C 142-218].
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

void MmeBindStandardParameters(ModelData* model, MaterialBinding* binding,
                               const RenderSnapshot& snap)
{
    if (model == nullptr || binding == nullptr || binding->effect == nullptr) {
        return;
    }
    ID3DXEffect* effect = binding->effect;

    // Handles must belong to the target effect: the g_paramHandles cache was
    // resolved from the host effect at Initialize and is meaningless for a
    // model effect. Resolve lazily once per binding and cache by name.
    if (binding->namedHandles.empty()) {
        static const char* const kNames[] = {
            "matWorld", "matWorldViewProj", "matLightViewProj", "matRotate",
            "EgColor", "ToonColor", "LightDir", "SpcColor", "Place",
            "DifColor", "parthf", "spadd", "transp",
            "TexCAdd", "TexCMul", "SphCAdd", "SphCMul",
        };
        for (int i = 0; i < kParamCount; ++i) {
            binding->namedHandles[kNames[i]] =
                effect->GetParameterByName(nullptr, kNames[i]);
        }
    }
    auto H = [&](int index) -> D3DXHANDLE {
        return binding->namedHandles[g_paramNames[index]];
    };

    // --- the 17 cached standard parameters (DAT_1800d9b30..DAT_1800d9bb0) ---
    // Matrices from the snapshot fields (+0x98/+0x118/+0x158/+0xd8).
    effect->SetMatrix(H(kParamMatWorld),
                      reinterpret_cast<const D3DXMATRIX*>(&snap.effect_world));
    effect->SetMatrix(H(kParamMatWorldViewProj),
                      reinterpret_cast<const D3DXMATRIX*>(&snap.world_view_projection));
    effect->SetMatrix(H(kParamMatLightViewProj),
                      reinterpret_cast<const D3DXMATRIX*>(&snap.light_view_projection));
    effect->SetMatrix(H(kParamMatRotate),
                      reinterpret_cast<const D3DXMATRIX*>(&snap.rotation));

    // Colors from the snapshot (+0x198..+0x217) except LightDir, which comes
    // from the GetLight(0) cache (DAT_1800d9890).
    effect->SetVector(H(kParamEgColor),
                      reinterpret_cast<const D3DXVECTOR4*>(&snap.edge_color));
    effect->SetVector(H(kParamToonColor),
                      reinterpret_cast<const D3DXVECTOR4*>(&snap.toon_color));
    {
        float lightDir[4];
        lightDir[0] = g_cachedLight.Direction.x;
        lightDir[1] = g_cachedLight.Direction.y;
        lightDir[2] = g_cachedLight.Direction.z;
        lightDir[3] = 0.0f;
        effect->SetVector(H(kParamLightDir),
                          reinterpret_cast<const D3DXVECTOR4*>(lightDir));
    }
    effect->SetVector(H(kParamSpcColor),
                      reinterpret_cast<const D3DXVECTOR4*>(&snap.specular_color));
    effect->SetVector(H(kParamDifColor),
                      reinterpret_cast<const D3DXVECTOR4*>(&snap.diffuse_color));
    effect->SetVector(H(kParamTexCAdd),
                      reinterpret_cast<const D3DXVECTOR4*>(&snap.texture_add));
    effect->SetVector(H(kParamTexCMul),
                      reinterpret_cast<const D3DXVECTOR4*>(&snap.texture_multiply));
    effect->SetVector(H(kParamSphCAdd),
                      reinterpret_cast<const D3DXVECTOR4*>(&snap.sphere_add));
    effect->SetVector(H(kParamSphCMul),
                      reinterpret_cast<const D3DXVECTOR4*>(&snap.sphere_multiply));

    // Booleans through the cached handles (semantics 0x3C/0x3D/0x3E).
    MmeBindBooleanParameter(model, effect, 0x3C, H(kParamParthf));
    MmeBindBooleanParameter(model, effect, 0x3D, H(kParamSpadd));
    MmeBindBooleanParameter(model, effect, 0x3E, H(kParamTransp));

    // --- name-resolved extras (the SAS scan resolves these per effect in the
    // original; Phase 2 resolves per apply through GetParameterByName) ---
    D3DXHANDLE handle = effect->GetParameterByName(nullptr, "use_texture");
    if (handle != nullptr) {
        MmeBindBooleanParameter(model, effect, 0x3F, handle);
    }
    handle = effect->GetParameterByName(nullptr, "use_spheremap");
    if (handle != nullptr) {
        MmeBindBooleanParameter(model, effect, 0x40, handle);
    }
    handle = effect->GetParameterByName(nullptr, "use_subtexture");
    if (handle != nullptr) {
        MmeBindBooleanParameter(model, effect, 0x41, handle);
    }
    handle = effect->GetParameterByName(nullptr, "use_toon");
    if (handle != nullptr) {
        MmeBindBooleanParameter(model, effect, 0x42, handle);
    }
    handle = effect->GetParameterByName(nullptr, "opadd");
    if (handle != nullptr) {
        MmeBindBooleanParameter(model, effect, 0x45, handle);
    }
    handle = effect->GetParameterByName(nullptr, "_INDEX");
    if (handle != nullptr) {
        effect->SetInt(handle, snap.subset_index);
    }
    handle = effect->GetParameterByName(nullptr, "VertexCount");
    if (handle != nullptr) {
        effect->SetInt(handle, model->cachedPerVertexValue() >= 0
                                   ? model->cachedPerVertexValue() : 0);
    }
    handle = effect->GetParameterByName(nullptr, "SubsetCount");
    if (handle != nullptr) {
        effect->SetInt(handle, model->materialCount());
    }

    // Material-color semantics for effects that expose the evidenced standard
    // parameter names (the SAS semantic table maps DIFFUSE/EDGECOLOR/TOONCOLOR
    // to these in the original; exact-name UNCERTAIN, see notes).
    struct NamedSemantic {
        const char* name;
        int semantic;
    };
    static const NamedSemantic kNamedSemantics[] = {
        { "DifColor",   0x18 },
        { "EgColor",    0x1D },
        { "ToonColor",  0x1C },
    };
    for (int i = 0; i < 3; ++i) {
        D3DXHANDLE semanticHandle =
            effect->GetParameterByName(nullptr, kNamedSemantics[i].name);
        if (semanticHandle != nullptr) {
            MmeBindMaterialParameter(model, effect, kNamedSemantics[i].semantic,
                                     semanticHandle, 4);
        }
    }

    // --- texture parameters (MATERIALTEXTURE / MATERIALSPHEREMAP /
    // MATERIALTOONTEXTURE) through the MMHack state queries (the snapshot
    // fields were captured from GetTexture/GetSphereMapTexture/
    // GetToonTexture) ---
    if (snap.base_texture != nullptr) {
        handle = effect->GetParameterByName(nullptr, "ObjectTexture");
        if (handle != nullptr) {
            effect->SetTexture(handle, snap.base_texture);
        }
    }
    if (snap.sphere_texture != nullptr) {
        handle = effect->GetParameterByName(nullptr, "ObjectSphereMap");
        if (handle != nullptr) {
            effect->SetTexture(handle, snap.sphere_texture);
        }
    }
    if (snap.toon_texture != nullptr) {
        handle = effect->GetParameterByName(nullptr, "ObjectToonTexture");
        if (handle != nullptr) {
            effect->SetTexture(handle, snap.toon_texture);
        }
    }
}

// ---------------------------------------------------------------------------
// CONTROLOBJECT staging (Phase 3 seam with live host calls)
// ---------------------------------------------------------------------------

ControlObjectStage* MmeStageControlObjectValues(ModelData* model, int hostIndex)
{
    // The SAS interpreter (Phase 3) resolves CONTROLOBJECT parameters from the
    // host exports. The staging runs per scanned object from the pass planner
    // (once per frame), keeping the host import surface live; the Phase 3
    // resolver consumes the staged values by object/bone/morph name.
    static std::map<ModelData*, ControlObjectStage> stageTable;
    if (model == nullptr || hostIndex < 0) {
        return nullptr;
    }
    ControlObjectStage& stage = stageTable[model];

    if (model->kind() == 0) {
        // Accessory panel values [ExpGetAcsX/Y/Z/Rx/Ry/Rz/Si/Tr].
        stage.panel[0] = ExpGetAcsX(hostIndex);
        stage.panel[1] = ExpGetAcsY(hostIndex);
        stage.panel[2] = ExpGetAcsZ(hostIndex);
        stage.panel[3] = ExpGetAcsRx(hostIndex);
        stage.panel[4] = ExpGetAcsRy(hostIndex);
        stage.panel[5] = ExpGetAcsRz(hostIndex);
        stage.panel[6] = ExpGetAcsSi(hostIndex);
        stage.panel[7] = ExpGetAcsTr(hostIndex);
    } else {
        // Bone world matrix + morph value placeholders (bone 0 / morph 0;
        // the Phase 3 resolver indexes by CONTROLOBJECT item name).
        stage.boneWorld = ExpGetPmdBoneWorldMat(hostIndex, 0);
        stage.morphValue = ExpGetPmdMorphValue(hostIndex, 0);
    }
    stage.valid = true;
    return &stage;
}

ControlObjectStage* MmeFindControlObjectStage(ModelData* model)
{
    static std::map<ModelData*, ControlObjectStage> stageTable;
    std::map<ModelData*, ControlObjectStage>::const_iterator it = stageTable.find(model);
    if (it == stageTable.end()) {
        return nullptr;
    }
    return const_cast<ControlObjectStage*>(&it->second);
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

} // namespace mme
