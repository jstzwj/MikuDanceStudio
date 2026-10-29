#include "../include/mme_portable_api.h"
#include "../include/mme_host_api.h"
#include "mmeffect/mme_context.h"
#include "mmeffect/mme_globals.h"
#include "mmeffect/material_bind.h"
#include "mmeffect/model_data.h"
#include "mmeffect/effect_engine.h"
#include "mmeffect/sas_interpreter.h"
#include "mmeffect/emm_manager.h"
#include <algorithm>
#include <cctype>
#include <set>
namespace {
std::string Text(ID3DXEffect* fx, D3DXHANDLE handle, const char* annotation = nullptr) {
    const char* value = nullptr;
    if (annotation)
        handle = fx->GetAnnotationByName(handle, annotation);
    return handle && SUCCEEDED(fx->GetString(handle, &value)) && value ? value : "";
}
std::string Compact(std::string text) {
    text.erase(std::remove_if(text.begin(), text.end(),
                              [](unsigned char c) { return std::isspace(c) != 0; }),
               text.end());
    return text;
}
bool Describe(const mme::EffectOwnerManager::BindingKey& key, mme::MaterialBinding& binding,
              MmePortableEffect& out, std::string& reason) {
    const auto fail = [&](const char* r) {
        reason = r;
        return false;
    };
    auto* fx = binding.effect;
    if (!binding.instance || !binding.instance->originalSource || !binding.sas || key.turnId)
        return fail("MME_portable_source_or_turn");
    const auto profile = Text(fx, fx->GetParameterByName(nullptr, "MDSRhiProfile"));
    if (profile != "material-v1" && profile != "post-v1")
        return fail("MME_profile_not_supported");
    out.object = key.model->objectId();
    out.subset = key.subset;
    out.post = profile == "post-v1";
    out.source = binding.instance->originalSource;
    if (out.post) {
        if (key.subset != -1 || mme::SasGetScriptClass(binding.sas) != mme::kSasClassScene ||
            mme::SasGetScriptOrder(binding.sas) != mme::kSasOrderPostprocess)
            return fail("MME_post_class_not_supported");
    } else if (mme::SasGetScriptClass(binding.sas) != mme::kSasClassObject ||
               mme::SasGetScriptOrder(binding.sas) != mme::kSasOrderStandard)
        return fail("MME_material_class_not_supported");
    const std::set<std::string> material = {
        "MDSRhiProfile", "worldViewProjection", "materialDiffuse", "tint",
        "viewportSize",  "baseTexture",         "baseSampler"};
    const std::set<std::string> post = {
        "MDSRhiProfile", "Script",        "SceneColor", "Ping",         "Depth",
        "viewportSize",  "multiplyColor", "addColor",   "SceneSampler", "PingSampler"};
    D3DXEFFECT_DESC effect{};
    if (FAILED(fx->GetDesc(&effect)))
        return fail("MME_effect_description");
    for (UINT n = 0; n < effect.Parameters; ++n) {
        D3DXPARAMETER_DESC p{};
        auto h = fx->GetParameter(nullptr, n);
        if (FAILED(fx->GetParameterDesc(h, &p)) || !p.Name ||
            !(out.post ? post : material).count(p.Name))
            return fail("MME_parameter_not_supported");
        const std::string name = p.Name;
        const char* semantic = name == "worldViewProjection"              ? "WORLDVIEWPROJECTION"
                               : name == "materialDiffuse"                ? "DIFFUSE"
                               : name == "viewportSize"                   ? "VIEWPORTPIXELSIZE"
                               : name == "baseTexture"                    ? "MATERIALTEXTURE"
                               : name == "Script"                         ? "STANDARDSGLOBAL"
                               : (name == "SceneColor" || name == "Ping") ? "RENDERCOLORTARGET"
                               : name == "Depth" ? "RENDERDEPTHSTENCILTARGET"
                                                 : "";
        if (p.Elements || _stricmp(p.Semantic ? p.Semantic : "", semantic))
            return fail("MME_parameter_semantic_not_supported");
        if (name == "MDSRhiProfile") {
            if (p.Type != 4 || p.Annotations)
                return fail("MME_profile_parameter_layout");
        } else if (name == "baseSampler" || name == "SceneSampler" || name == "PingSampler") {
            if ((p.Type != 10 && p.Type != 12) || p.Annotations)
                return fail("MME_sampler_parameter_layout");
        } else if (name == "baseTexture" || name == "SceneColor" || name == "Ping" ||
                   name == "Depth") {
            if (p.Type != 5 && p.Type != 7)
                return fail("MME_texture_dimension_not_supported");
            if (name == "baseTexture") {
                if (p.Annotations)
                    return fail("MME_texture_annotations_not_supported");
            } else if (p.Annotations != 1 ||
                       Text(fx, h, "Format") != (name == "Depth" ? "D24S8" : "A8R8G8B8"))
                return fail("MME_target_format_or_extent_not_supported");
        } else {
            const unsigned columns = name == "viewportSize" ? 2 : name == "Script" ? 1 : 4;
            const unsigned rows = name == "worldViewProjection" ? 4 : 1;
            if (p.Type != 3 || p.Rows != rows || p.Columns != columns)
                return fail("MME_constant_parameter_layout");
            if (name == "Script") {
                if (p.Annotations != 3 || Text(fx, h, "ScriptClass") != "scene" ||
                    Text(fx, h, "ScriptOrder") != "postprocess" ||
                    Text(fx, h, "ScriptOutput") != "color")
                    return fail("MME_standards_global_not_supported");
            } else if (name == "materialDiffuse") {
                if (p.Annotations != 1 || Text(fx, h, "Object") != "Geometry")
                    return fail("MME_diffuse_object_not_supported");
            } else if (p.Annotations)
                return fail("MME_parameter_annotations_not_supported");
        }
    }
    if (FAILED(
            fx->GetFloatArray(fx->GetParameterByName(nullptr, out.post ? "multiplyColor" : "tint"),
                              out.multiply.data(), 4)))
        return fail("MME_parameter_value");
    if (out.post &&
        FAILED(fx->GetFloatArray(fx->GetParameterByName(nullptr, "addColor"), out.add.data(), 4)))
        return fail("MME_parameter_value");
    const int count = mme::SasGetTechniqueCount(binding.sas);
    if (out.post && count != 1)
        return fail("MME_post_technique_count");
    for (int n = 0; n < count; ++n) {
        mme::SasTechniqueInfo t{};
        if (!mme::SasGetTechniqueInfo(binding.sas, n, &t) || !t.hardwareOk || !t.shaderMixOk ||
            t.passCount != (out.post ? 2 : 1) ||
            (!out.post && (t.hasScript || (t.mmdPass != 0 && t.mmdPass != 1))))
            return fail("MME_technique_not_supported");
        if (out.post &&
            Compact(Text(fx, t.handle, "Script")) !=
                "RenderColorTarget0=SceneColor;RenderDepthStencilTarget=Depth;Clear=Color;Clear=Depth;ScriptExternal=Color;RenderColorTarget0=Ping;Pass=Grade;RenderColorTarget0=;RenderDepthStencilTarget=;Pass=Finish;")
            return fail("MME_script_not_supported");
        for (int p = 0; p < t.passCount; ++p) {
            auto h = fx->GetPass(t.handle, p);
            D3DXPASS_DESC pass{};
            if (FAILED(fx->GetPassDesc(h, &pass)))
                return fail("MME_pass_description");
            const auto script = Compact(Text(fx, h, "Script"));
            if (script != (out.post ? "Draw=Buffer;" : ""))
                return fail("MME_pass_script_not_supported");
            MmePortablePass value{t.name, pass.Name ? pass.Name : "", Text(fx, h, "MDSRhiVS"),
                                  Text(fx, h, "MDSRhiPS")};
            if (value.vertex.empty() || value.pixel.empty() ||
                (out.post && value.name != (p ? "Finish" : "Grade")))
                return fail("MME_pass_not_supported");
            out.passes.push_back(std::move(value));
        }
    }
    return !out.passes.empty() || fail("MME_empty_effect");
}
}
bool MmePortableEffects(std::vector<MmePortableEffect>& out, std::string& reason) {
    out.clear();
    reason.clear();
    if (!MmeHostEffectsEnabled() || !mme::g_ownerManager)
        return true;
    for (const auto* object : mme::g_ownerManager->registeredObjects)
        if (object)
            for (int subset = 0; subset < object->materialCount(); ++subset)
                if (!mme::MmeEmmEffectiveSubsetShown(object, subset)) {
                    reason = "MME_visibility_not_supported";
                    return false;
                }
    unsigned posts = 0;
    for (const auto& entry : mme::g_ownerManager->bindings) {
        // Derived offscreen bindings are a DX9 cache and can survive removal
        // of their scene owner. Eligibility follows live base assignments:
        // each supported profile excludes offscreen child turns itself.
        if (entry.first.turnId)
            continue;
        const auto* b = entry.second;
        if (!b || !b->effect || b->effectPath.empty())
            continue;
        MmePortableEffect value;
        if (!Describe(entry.first, *entry.second, value, reason))
            return false;
        if (value.post && ++posts > 1) {
            reason = "MME_multiple_post_effects";
            return false;
        }
        out.push_back(std::move(value));
    }
    for (const auto& effect : out)
        if (effect.post)
            for (const auto& entry : mme::g_ownerManager->bindings)
                if (!entry.first.turnId && entry.first.model &&
                    entry.first.model->objectId() == effect.object && entry.first.subset >= 0) {
                    reason = "MME_scene_subset_override_not_supported";
                    return false;
                }
    return true;
}
int MmePortableDraw(unsigned long long id, int subset, int mode, unsigned flags,
                    MmePortableEffect& out, std::string& reason) {
    out = {};
    reason.clear();
    if (!MmeHostEffectsEnabled() || !mme::g_ownerManager)
        return 0;
    auto& manager = *mme::g_ownerManager;
    for (auto* object : manager.registeredObjects) {
        if (!object || object->objectId() != id)
            continue;
        auto found = manager.bindings.find({0, object, subset});
        if (found == manager.bindings.end())
            found = manager.bindings.find({0, object, -1});
        if (found == manager.bindings.end() || !found->second || !found->second->effect ||
            found->second->effectPath.empty())
            return 0;
        if (!Describe(found->first, *found->second, out, reason))
            return -1;
        if (out.post)
            return 2;
        bool valid = false;
        auto selected = mme::SasSelectTechnique(found->second->sas, mode, subset, (flags & 1) != 0,
                                                (flags & 2) != 0, (flags & 4) != 0, &valid);
        if (!valid) {
            reason = "MME_invalid_technique_selection";
            return -1;
        }
        if (!selected)
            return 0;
        D3DXTECHNIQUE_DESC t{};
        found->second->effect->GetTechniqueDesc(selected, &t);
        out.passes.erase(std::remove_if(out.passes.begin(), out.passes.end(),
                                        [&](const auto& p) { return p.technique != t.Name; }),
                         out.passes.end());
        return out.passes.size() == 1 ? 1 : -1;
    }
    return 0;
}
