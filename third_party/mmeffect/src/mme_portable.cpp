#include "../include/mme_portable_api.h"
#include "../include/mme_standard_api.h"
#include "mmhack/mmhack_state.h"
#include "../include/mme_host_api.h"
#include "mmeffect/mme_context.h"
#include "mmeffect/mme_globals.h"
#include "mmeffect/material_bind.h"
#include "mmeffect/model_data.h"
#include "mmeffect/effect_engine.h"
#include "mmeffect/sas_interpreter.h"
#include "mmeffect/emm_manager.h"
#include "mmeffect/pass_planner.h"
#include "mmeffect/sas_exec.h"
#include "../include/mmhack_api.h"
#include "../include/MMDExport.h"
#include <algorithm>
#include <cctype>
#include <cmath>
#include <set>
#include <cstring>
#include <sstream>
namespace {
std::vector<mme::MmeRenderPassItem> portableTurnPlan;
const mme::MmeRenderPassItem* FindTurn(unsigned turn) {
    for (const auto& item : portableTurnPlan)
        if (item.turnId == turn)
            return &item;
    return nullptr;
}
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
bool ControlAnnotations(ID3DXEffect* fx, D3DXHANDLE handle, unsigned count) {
    bool name = false, item = false;
    for (unsigned i = 0; i < count; ++i) {
        auto a = fx->GetAnnotation(handle, i);
        D3DXPARAMETER_DESC desc{};
        const char* text = nullptr;
        if (!a || FAILED(fx->GetParameterDesc(a, &desc)) || !desc.Name ||
            desc.Type != D3DXPT_STRING || FAILED(fx->GetString(a, &text)))
            return false;
        if (!_stricmp(desc.Name, "Name") && !name)
            name = true;
        else if (!_stricmp(desc.Name, "Item") && !item)
            item = true;
        else
            return false;
    }
    return name;
}
bool ReadParameter(ID3DXEffect* fx, D3DXHANDLE handle, const D3DXPARAMETER_DESC& p,
                   MmePortableParameter& out) {
    if ((p.Type != D3DXPT_FLOAT && p.Type != D3DXPT_INT && p.Type != D3DXPT_BOOL) || !p.Rows ||
        p.Rows > 4 || !p.Columns || p.Columns > 4 || p.StructMembers || p.Elements > 256)
        return false;
    out.name = p.Name;
    out.semantic = p.Semantic ? p.Semantic : "";
    out.rows = p.Rows;
    out.columns = p.Columns;
    out.elements = p.Elements ? p.Elements : 1;
    out.shared = (p.Flags & D3DX_PARAMETER_SHARED) != 0;
    out.type = p.Type == D3DXPT_FLOAT  ? MmePortableParameter::Type::Float
               : p.Type == D3DXPT_BOOL ? MmePortableParameter::Type::Bool
                                       : MmePortableParameter::Type::Int;
    const auto components = out.rows * out.columns * out.elements;
    out.value.resize(components * 4);
    if (p.Type == D3DXPT_FLOAT) {
        std::vector<float> values(components);
        if (FAILED(fx->GetFloatArray(handle, values.data(), components)))
            return false;
        std::memcpy(out.value.data(), values.data(), out.value.size());
    } else {
        std::vector<int> values(components);
        const auto hr = p.Type == D3DXPT_BOOL ? fx->GetBoolArray(handle, values.data(), components)
                                              : fx->GetIntArray(handle, values.data(), components);
        if (FAILED(hr))
            return false;
        std::memcpy(out.value.data(), values.data(), out.value.size());
    }
    return true;
}
bool Visible(const mme::ModelData& object) {
    const bool accessory = object.kind() == 0;
    const int count = accessory ? ExpGetAcsNum() : ExpGetPmdNum();
    for (int i = 0; i < count; ++i) {
        const auto id =
            reinterpret_cast<unsigned long long>(accessory ? ExpGetAcsID(i) : ExpGetPmdID(i));
        if (id == object.objectId())
            return accessory ? ExpGetAcsDisp(i) != 0 : ExpGetPmdDisp(i) != 0;
    }
    return false;
}
bool ExportScript(mme::SasEffect& sas, MmePortableEffect& out, std::string& reason) {
    const auto color = GetClearColor();
    out.scriptClearColor = {float((color >> 16) & 255) / 255, float((color >> 8) & 255) / 255,
                            float(color & 255) / 255, float((color >> 24) & 255) / 255};
    if (sas.techniques.size() != 1 || !sas.techniques[0].hasScript || sas.hasErrors) {
        reason = "MME_scene_technique_script";
        return false;
    }
    using K = MmePortableCommand::Kind;
    for (const auto& cmd : sas.techniques[0].cmds) {
        MmePortableCommand value{K::Pass};
        value.value = cmd.value;
        if (cmd.param && cmd.id != mme::kSasCmdPass) {
            D3DXPARAMETER_DESC p{};
            if (FAILED(sas.effect->GetParameterDesc(cmd.param, &p)) || !p.Name) {
                reason = "MME_scene_command_parameter";
                return false;
            }
            value.parameter = p.Name;
        }
        switch (cmd.id) {
        case mme::kSasCmdPass:
            value.kind = K::Pass;
            break;
        case mme::kSasCmdRenderColorTarget0:
        case mme::kSasCmdRenderColorTarget1:
        case mme::kSasCmdRenderColorTarget2:
        case mme::kSasCmdRenderColorTarget3:
            if (cmd.param && cmd.value >= 0) {
                reason = "MME_scene_texture_subresource";
                return false;
            }
            value.kind = K::ColorTarget;
            value.value = cmd.id - mme::kSasCmdRenderColorTarget0;
            break;
        case mme::kSasCmdRenderDepthTarget:
            value.kind = K::DepthTarget;
            break;
        case mme::kSasCmdClearSetColor:
            value.kind = K::ClearColorValue;
            break;
        case mme::kSasCmdClearSetDepth:
            value.kind = K::ClearDepthValue;
            break;
        case mme::kSasCmdClear:
            value.kind = K::Clear;
            break;
        case mme::kSasCmdScriptExternal:
            value.kind = K::Capture;
            break;
        case mme::kSasCmdLoopByCount:
            value.kind = K::Loop;
            break;
        case mme::kSasCmdLoopGetIndex:
            value.kind = K::LoopIndex;
            break;
        case mme::kSasCmdLoopEnd:
            value.kind = K::LoopEnd;
            break;
        default:
            reason = "MME_scene_command_not_supported";
            return false;
        }
        out.script.push_back(std::move(value));
    }
    return MmePortableSceneLayout(out, reason);
}
// Resolve the live pool texture to the SAS resource that actually published it.
// Resource revision IDs are CPU tokens; COM addresses stay inside this bridge.
bool ExportTarget(ID3DXEffect* fx, const MmeLegacyTexture& declaration, MmePortableTarget& out,
                  std::string& reason) {
    auto handle = fx->GetParameterByName(nullptr, declaration.name.c_str());
    D3DXPARAMETER_DESC parameter{};
    IDirect3DBaseTexture9* texture = nullptr;
    if (!handle || FAILED(fx->GetParameterDesc(handle, &parameter))) {
        reason = "MME_shared_target_parameter_missing:" + declaration.name;
        return false;
    }
    if (bool(parameter.Flags & D3DX_PARAMETER_SHARED) != declaration.shared) {
        reason = "MME_shared_target_shared_flag:" + declaration.name;
        return false;
    }
    if (FAILED(fx->GetTexture(handle, &texture)) || !texture) {
        reason = "MME_shared_target_unbound:" + declaration.name;
        return false;
    }
    struct Find {
        IDirect3DBaseTexture9* texture;
        mme::SasResource* resource = nullptr;
        ID3DXEffect* publisher = nullptr;
    } query{texture};
    mme::MmeEngineForEachSas(
        [](void* data, mme::SasEffect* sas) {
            auto& query = *static_cast<Find*>(data);
            for (auto& resource : sas->resources)
                if (resource.texture == query.texture && resource.portableIdentity &&
                    (resource.semanticId == 0x26 || resource.semanticId == 0x27 ||
                     resource.semanticId == 0x2E)) {
                    query.resource = &resource;
                    query.publisher = sas->effect;
                    return false;
                }
            return true;
        },
        &query);
    texture->Release();
    if (!query.resource) {
        reason = "MME_shared_target_publisher_missing";
        return false;
    }
    const auto& resource = *query.resource;
    D3DXPARAMETER_DESC publication{};
    static const std::set<std::string> annotations = {
        "FORMAT",     "MIPLEVELS", "LEVELS", "DEFAULTEFFECT", "CLEARCOLOR",
        "CLEARDEPTH", "WIDTH",     "HEIGHT", "VIEWPORTRATIO"};
    if (FAILED(query.publisher->GetParameterDesc(resource.param, &publication))) {
        reason = "MME_shared_target_publisher_description";
        return false;
    }
    for (unsigned i = 0; i < publication.Annotations; ++i) {
        D3DXPARAMETER_DESC a{};
        if (FAILED(query.publisher->GetParameterDesc(
                query.publisher->GetAnnotation(resource.param, i), &a)) ||
            !a.Name)
            return false;
        std::string name = a.Name;
        std::transform(name.begin(), name.end(), name.begin(),
                       [](unsigned char c) { return char(std::toupper(c)); });
        if (!annotations.count(name)) {
            reason = "MME_shared_target_publisher_extent_or_annotations";
            return false;
        }
    }
    const bool depth = resource.semanticId == 0x27;
    if (resource.textureType != D3DXPT_TEXTURE && resource.textureType != D3DXPT_TEXTURE2D) {
        reason = "MME_target_dimension_not_supported";
        return false;
    }
    const bool mipmapped = resource.texMips <= 0;
    if ((resource.texMips > 1) ||
        (mipmapped && (resource.semanticId != 0x2E || resource.format != D3DFMT_A8R8G8B8)) ||
        resource.reqWidth <= 0 || resource.reqHeight <= 0 ||
        (depth ? resource.format != D3DFMT_D24S8
               : resource.format != D3DFMT_A8R8G8B8 && resource.format != D3DFMT_A16B16G16R16F)) {
        reason = "MME_target_format_or_mips_not_supported";
        return false;
    }
    // Viewport-relative targets follow the current RHI extent; explicit paired
    // Width/Height annotations retain their native fixed extent.
    if (resource.hasViewportRatio &&
        (!std::isfinite(resource.viewportRatio[0]) || !std::isfinite(resource.viewportRatio[1]) ||
         resource.viewportRatio[0] <= 0 || resource.viewportRatio[1] <= 0)) {
        reason = "MME_target_ratio_not_supported";
        return false;
    }
    if (!declaration.semantic.empty() &&
        declaration.semantic != (depth                         ? "RENDERDEPTHSTENCILTARGET"
                                 : resource.semanticId == 0x2E ? "OFFSCREENRENDERTARGET"
                                                               : "RENDERCOLORTARGET")) {
        reason = "MME_shared_target_semantic_mismatch";
        return false;
    }
    if (declaration.annotations.count("FORMAT")) {
        const auto format = declaration.annotations.find("FORMAT");
        if (format == declaration.annotations.end() ||
            format->second != (depth                                     ? "D24S8"
                               : resource.format == D3DFMT_A16B16G16R16F ? "A16B16G16R16F"
                                                                         : "A8R8G8B8")) {
            reason = "MME_shared_target_declaration_conflict";
            return false;
        }
    }
    out = {declaration.name,
           resource.format == D3DFMT_A16B16G16R16F ? MmePortableFormat::Rgba16Float
                                                   : MmePortableFormat::Rgba8,
           depth, resource.portableIdentity};
    const bool fixedWidth =
        query.publisher->GetAnnotationByName(resource.param, "Width") != nullptr;
    const bool fixedHeight =
        query.publisher->GetAnnotationByName(resource.param, "Height") != nullptr;
    if (!resource.hasViewportRatio && (fixedWidth || fixedHeight)) {
        if (!fixedWidth || !fixedHeight || resource.reqWidth <= 0 || resource.reqHeight <= 0 ||
            query.publisher->GetAnnotationByName(resource.param, "ViewPortRatio")) {
            reason = "MME_target_partial_extent";
            return false;
        }
        out.width = unsigned(resource.reqWidth);
        out.height = unsigned(resource.reqHeight);
    }
    out.mipmapped = mipmapped;
    out.offscreen = resource.semanticId == 0x2E;
    out.relative = resource.hasViewportRatio;
    out.viewportRatio = {resource.viewportRatio[0], resource.viewportRatio[1]};
    return true;
}
bool DescribeLegacyScene(const mme::EffectOwnerManager::BindingKey& key,
                         mme::MaterialBinding& binding, MmePortableEffect& out,
                         std::string& reason) {
    if (key.subset != -1 || mme::SasGetScriptClass(binding.sas) != mme::kSasClassScene ||
        mme::SasGetScriptOrder(binding.sas) != mme::kSasOrderPostprocess ||
        binding.sas->techniqueOrder.size() != 1 || binding.sas->techniques.size() != 1) {
        reason = "MME_legacy_scene_selection";
        return false;
    }
    auto owner = binding.owner ? binding.owner : binding.instance;
    if (!owner->legacyImportChecked) {
        owner->legacyImportChecked = true;
        auto program = std::make_shared<MmeLegacyProgram>();
        if (owner->legacySource &&
            MmeLegacyImportScene(*owner->legacySource, *program, owner->legacyImportReason))
            owner->legacyImport = std::move(program);
        else if (!owner->legacySource)
            owner->legacyImportReason = "MME_legacy_expanded_source_missing";
    }
    if (!owner->legacyImport) {
        reason = owner->legacyImportReason;
        return false;
    }
    out.object = key.model->objectId();
    out.subset = -1;
    out.post = true;
    out.profile = "legacy-scene-v1";
    out.source = binding.instance->originalSource;
    out.legacySource = binding.instance->legacySource;
    out.legacy = owner->legacyImport;
    for (const auto& declaration : out.legacy->textures) {
        MmePortableTarget target;
        if (!ExportTarget(binding.effect, declaration, target, reason)) {
            reason += ":scene";
            return false;
        }
        out.targets.push_back(std::move(target));
    }
    for (const auto& v : out.legacy->variables) {
        D3DXPARAMETER_DESC desc{};
        MmePortableParameter parameter;
        auto handle = binding.effect->GetParameterByName(nullptr, v.name.c_str());
        // Scene standard setters beyond viewport/control/numeric values need
        // their own pass-time provider and are deliberately not inherited.
        if (!v.semantic.empty() && v.semantic != "VIEWPORTPIXELSIZE" &&
            v.semantic != "CONTROLOBJECT") {
            reason = "MME_legacy_scene_parameter_semantic";
            return false;
        }
        if (!handle || FAILED(binding.effect->GetParameterDesc(handle, &desc)) ||
            !ReadParameter(binding.effect, handle, desc, parameter)) {
            reason = "MME_legacy_scene_parameter_layout";
            return false;
        }
        out.parameters.push_back(std::move(parameter));
    }
    const auto& technique = binding.sas->techniques[0];
    if (!technique.hardwareOk || !technique.shaderMixOk || !technique.hasScript ||
        technique.passes.empty()) {
        reason = "MME_legacy_scene_technique";
        return false;
    }
    for (const auto& pass : technique.passes) {
        if (!pass.hasScript || pass.cmds.size() != 1 || pass.cmds[0].id != mme::kSasCmdDrawBuffer) {
            reason = "MME_legacy_scene_pass_script";
            return false;
        }
        const auto source =
            std::find_if(out.legacy->passes.begin(), out.legacy->passes.end(), [&](const auto& p) {
                return p.technique == technique.name && p.name == pass.name;
            });
        if (source == out.legacy->passes.end() ||
            !MmeLegacySamplers(*out.legacy, *source, true).empty()) {
            reason = "MME_legacy_scene_pass_source";
            return false;
        }
        MmePortablePass value{technique.name, pass.name, source->vertex, source->pixel};
        for (auto slot : MmeLegacySamplers(*out.legacy, *source)) {
            value.sampled.push_back(out.legacy->samplers[slot].texture);
            value.sampleSlots.push_back(slot);
        }
        out.passes.push_back(std::move(value));
    }
    return ExportScript(*binding.sas, out, reason);
}
bool DescribeLegacy(const mme::EffectOwnerManager::BindingKey& key, mme::MaterialBinding& binding,
                    MmePortableEffect& out, std::string& reason) {
    const auto fail = [&](const char* why) {
        reason = why;
        return false;
    };
    if (mme::SasGetScriptClass(binding.sas) == mme::kSasClassScene)
        return DescribeLegacyScene(key, binding, out, reason);
    if (mme::SasGetScriptClass(binding.sas) != mme::kSasClassObject ||
        mme::SasGetScriptOrder(binding.sas) != mme::kSasOrderStandard)
        return fail("MME_legacy_scene_not_supported");
    if (key.model->kind() != 1)
        return fail("MME_legacy_geometry_not_supported");
    auto owner = binding.owner ? binding.owner : binding.instance;
    if (!owner->legacyImportChecked) {
        owner->legacyImportChecked = true;
        auto program = std::make_shared<MmeLegacyProgram>();
        if (owner->legacySource &&
            MmeLegacyImport(*owner->legacySource, *program, owner->legacyImportReason))
            owner->legacyImport = std::move(program);
        else if (!owner->legacySource)
            owner->legacyImportReason = "MME_legacy_expanded_source_missing";
    }
    if (!owner->legacyImport) {
        reason = owner->legacyImportReason;
        return false;
    }
    out.object = key.model->objectId();
    out.subset = key.subset;
    out.profile = "legacy-material-v1";
    out.source = binding.instance->originalSource;
    out.legacySource = binding.instance->legacySource;
    out.modernSource = binding.instance->modernSource;
    out.legacy = owner->legacyImport;
    out.clock = {mme::g_frameTimeBase, mme::g_deltaSeconds, mme::g_lastFrameTime,
                 mme::g_frameDelta};
    out.clockReady = mme::g_timeInitialized != 0;
    out.counts = {-1, key.model->materialCount()}; // CPU producer supplies vertex count.
    for (const auto& v : out.legacy->variables) {
        MmeStandardBinding standard;
        if (!MmeStandardClassify(v, standard, reason))
            return false;
        MmePortableParameter p;
        D3DXPARAMETER_DESC d{};
        auto h = binding.effect->GetParameterByName(nullptr, v.name.c_str());
        if (!h || FAILED(binding.effect->GetParameterDesc(h, &d)) ||
            !ReadParameter(binding.effect, h, d, p) || p.rows != v.rows || p.columns != v.columns ||
            p.elements != v.elements)
            return fail("MME_legacy_parameter_layout");
        if (p.shared && standard.kind != MmeStandardBinding::Kind::Value)
            return fail("MME_legacy_shared_draw_semantic");
        out.parameters.push_back(std::move(p));
    }
    for (const auto& declaration : out.legacy->textures) {
        if ((!declaration.shared && declaration.semantic != "OFFSCREENRENDERTARGET") ||
            (declaration.semantic != "RENDERCOLORTARGET" &&
             declaration.semantic != "OFFSCREENRENDERTARGET" && !declaration.semantic.empty()))
            continue;
        MmePortableTarget target;
        if (!ExportTarget(binding.effect, declaration, target, reason)) {
            reason += ":material";
            return false;
        }
        out.targets.push_back(std::move(target));
    }
    const int count = mme::SasGetTechniqueCount(binding.sas);
    for (int n = 0; n < count; ++n) {
        mme::SasTechniqueInfo tech{};
        if (!mme::SasGetTechniqueInfo(binding.sas, n, &tech) || !tech.hardwareOk ||
            !tech.shaderMixOk || tech.hasScript || tech.passCount != 1 ||
            (tech.mmdPass != 0 && tech.mmdPass != 1))
            return fail("MME_legacy_material_technique");
        D3DXPASS_DESC pass{};
        const auto h = binding.effect->GetPass(tech.handle, 0);
        if (FAILED(binding.effect->GetPassDesc(h, &pass)) || !pass.Name)
            return fail("MME_pass_description");
        const auto source =
            std::find_if(out.legacy->passes.begin(), out.legacy->passes.end(), [&](const auto& p) {
                return p.technique == tech.name && p.name == pass.Name;
            });
        if (source == out.legacy->passes.end())
            return fail("MME_legacy_pass_source");
        out.passes.push_back({tech.name, pass.Name, source->vertex, source->pixel});
    }
    return !out.passes.empty() || fail("MME_empty_effect");
}
bool Describe(const mme::EffectOwnerManager::BindingKey& key, mme::MaterialBinding& binding,
              MmePortableEffect& out, std::string& reason) {
    const auto fail = [&](const char* r) {
        reason = r;
        return false;
    };
    auto* fx = binding.effect;
    if (!binding.instance || !binding.instance->originalSource || !binding.sas)
        return fail("MME_portable_source_or_turn");
    const auto profile = Text(fx, fx->GetParameterByName(nullptr, "MDSRhiProfile"));
    if (profile.empty())
        return DescribeLegacy(key, binding, out, reason);
    const bool hdr = profile == "mrt-hdr-v1";
    const bool scene = profile == "scene-v2";
    if (profile != "material-v1" && profile != "post-v1" && !hdr && !scene)
        return fail("MME_profile_not_supported");
    out.object = key.model->objectId();
    out.subset = key.subset;
    out.profile = profile;
    out.post = profile != "material-v1";
    out.source = binding.instance->originalSource;
    out.legacySource = binding.instance->legacySource;
    out.modernSource = binding.instance->modernSource;
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
    std::set<std::string> post = {"MDSRhiProfile", "Script",       "SceneColor",    "Ping",
                                  "Depth",         "viewportSize", "multiplyColor", "addColor",
                                  "SceneSampler",  "PingSampler"};
    if (hdr) {
        post.erase("Ping");
        post.erase("PingSampler");
        for (const auto* name : {"LayerA", "LayerB", "Hdr", "Tone", "LayerASampler",
                                 "LayerBSampler", "HdrSampler", "ToneSampler"})
            post.insert(name);
    }
    D3DXEFFECT_DESC effect{};
    if (FAILED(fx->GetDesc(&effect)))
        return fail("MME_effect_description");
    for (UINT n = 0; n < effect.Parameters; ++n) {
        D3DXPARAMETER_DESC p{};
        auto h = fx->GetParameter(nullptr, n);
        if (FAILED(fx->GetParameterDesc(h, &p)) || !p.Name)
            return fail("MME_parameter_not_supported");
        const std::string name = p.Name;
        if (scene && p.Type >= D3DXPT_SAMPLER && p.Type <= D3DXPT_SAMPLERCUBE) {
            if ((p.Type != D3DXPT_SAMPLER && p.Type != D3DXPT_SAMPLER2D) || p.Annotations ||
                p.Elements)
                return fail("MME_sampler_parameter_layout");
            continue;
        }
        if (scene && p.Type >= D3DXPT_TEXTURE && p.Type <= D3DXPT_TEXTURECUBE) {
            if ((p.Type != D3DXPT_TEXTURE && p.Type != D3DXPT_TEXTURE2D) || p.Elements ||
                p.Annotations != 1)
                return fail("MME_scene_target_annotations");
            const bool depth = p.Semantic && !_stricmp(p.Semantic, "RENDERDEPTHSTENCILTARGET");
            if (!depth && (!p.Semantic || _stricmp(p.Semantic, "RENDERCOLORTARGET")))
                return fail("MME_scene_resource_semantic");
            const auto format = Text(fx, h, "Format");
            if ((depth && format != "D24S8") ||
                (!depth && format != "A8R8G8B8" && format != "A16B16G16R16F"))
                return fail("MME_target_format_or_extent_not_supported");
            out.targets.push_back({name,
                                   format == "A16B16G16R16F" ? MmePortableFormat::Rgba16Float
                                                             : MmePortableFormat::Rgba8,
                                   depth});
            continue;
        }
        if (!(out.post ? post : material).count(name)) {
            const bool control = p.Semantic && !_stricmp(p.Semantic, "CONTROLOBJECT");
            if (p.Semantic && *p.Semantic && !control)
                return fail("MME_parameter_semantic_not_supported");
            if ((!control && p.Annotations) ||
                (control && !ControlAnnotations(fx, h, p.Annotations)))
                return fail("MME_parameter_annotations_not_supported");
            MmePortableParameter value;
            if (!ReadParameter(fx, h, p, value))
                return fail("MME_constant_parameter_layout");
            out.parameters.push_back(std::move(value));
            continue;
        }
        const char* semantic = name == "worldViewProjection" ? "WORLDVIEWPROJECTION"
                               : name == "materialDiffuse"   ? "DIFFUSE"
                               : name == "viewportSize"      ? "VIEWPORTPIXELSIZE"
                               : name == "baseTexture"       ? "MATERIALTEXTURE"
                               : name == "Script"            ? "STANDARDSGLOBAL"
                               : (name == "SceneColor" || name == "Ping" || name == "LayerA" ||
                                  name == "LayerB" || name == "Hdr" || name == "Tone")
                                   ? "RENDERCOLORTARGET"
                               : name == "Depth" ? "RENDERDEPTHSTENCILTARGET"
                                                 : "";
        if (p.Elements || _stricmp(p.Semantic ? p.Semantic : "", semantic))
            return fail("MME_parameter_semantic_not_supported");
        if (name == "MDSRhiProfile") {
            if (p.Type != 4 || p.Annotations)
                return fail("MME_profile_parameter_layout");
        } else if (name == "baseSampler" || name.find("Sampler") != std::string::npos) {
            if ((p.Type != 10 && p.Type != 12) || p.Annotations)
                return fail("MME_sampler_parameter_layout");
        } else if (name == "baseTexture" || name == "SceneColor" || name == "Ping" ||
                   name == "LayerA" || name == "LayerB" || name == "Hdr" || name == "Tone" ||
                   name == "Depth") {
            if (p.Type != 5 && p.Type != 7)
                return fail("MME_texture_dimension_not_supported");
            if (name == "baseTexture") {
                if (p.Annotations)
                    return fail("MME_texture_annotations_not_supported");
            } else if (p.Annotations != 1 ||
                       Text(fx, h, "Format") !=
                           (name == "Depth" ? "D24S8"
                            : (name == "LayerA" || name == "LayerB" || name == "Hdr")
                                ? "A16B16G16R16F"
                                : "A8R8G8B8"))
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
            (!scene && t.passCount != (out.post ? (hdr ? 4 : 2) : 1)) ||
            (!out.post && (t.hasScript || (t.mmdPass != 0 && t.mmdPass != 1))))
            return fail("MME_technique_not_supported");
        if (out.post && !scene &&
            Compact(Text(fx, t.handle, "Script")) !=
                (hdr ? "RenderColorTarget0=SceneColor;RenderDepthStencilTarget=Depth;Clear=Color;Clear=Depth;ScriptExternal=Color;RenderColorTarget0=LayerA;RenderColorTarget1=LayerB;RenderDepthStencilTarget=;Pass=Split;RenderColorTarget1=;RenderColorTarget0=Hdr;Pass=Combine;RenderColorTarget0=Tone;Pass=ToneMap;RenderColorTarget0=;Pass=Finish;"
                     : "RenderColorTarget0=SceneColor;RenderDepthStencilTarget=Depth;Clear=Color;Clear=Depth;ScriptExternal=Color;RenderColorTarget0=Ping;Pass=Grade;RenderColorTarget0=;RenderDepthStencilTarget=;Pass=Finish;"))
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
            const char* hdrNames[] = {"Split", "Combine", "ToneMap", "Finish"};
            if (value.vertex.empty() || value.pixel.empty() ||
                (out.post && !scene &&
                 value.name != (hdr ? hdrNames[p] : (p ? "Finish" : "Grade"))))
                return fail("MME_pass_not_supported");
            if (scene) {
                std::istringstream inputs(Compact(Text(fx, h, "MDSRhiInputs")));
                std::string input;
                while (std::getline(inputs, input, ',')) {
                    if (input.empty())
                        return fail("MME_scene_input_layout");
                    value.sampled.push_back(input);
                }
                if (value.sampled.size() > 4)
                    return fail("MME_scene_input_layout");
            }
            out.passes.push_back(std::move(value));
        }
    }
    if (scene)
        return ExportScript(*binding.sas, out, reason);
    if (out.post && !MmePortablePostLayout(out, reason))
        return false;
    return !out.passes.empty() || fail("MME_empty_effect");
}
}
void MmePortableBeginFrameClock(int notEditMode) {
    if (!MmeHostEffectsEnabled())
        return;
    g_mmh.notEditMode = notEditMode;
    g_mmh.frameTime = ExpGetFrameTime();
    mme::MmeUpdateFrameTime();
    mme::g_frameClockFrozen = true;
}
void MmePortableEndFrameClock() {
    mme::g_frameClockFrozen = false;
}
void MmePortablePrepareParameters() {
    mme::MmeRefreshPortableParameters();
}
void MmePortableCommitParameters(const MmePortableEffect& effect) {
    if (!MmeHostEffectsEnabled() || !mme::g_ownerManager || effect.writes.empty())
        return;
    for (const auto& entry : mme::g_ownerManager->bindings) {
        const auto* binding = entry.second;
        if (entry.first.turnId != effect.turn || !entry.first.model || !binding ||
            !binding->effect || entry.first.model->objectId() != effect.object ||
            entry.first.subset != effect.subset || !binding->instance ||
            binding->instance->originalSource != effect.source)
            continue;
        for (const auto& p : effect.writes) {
            if (p.shared || p.type != MmePortableParameter::Type::Int || p.value.size() != 4)
                continue;
            int value = 0;
            std::memcpy(&value, p.value.data(), 4);
            binding->effect->SetInt(p.name.c_str(), value);
        }
        return;
    }
}
bool MmePortablePostLayout(MmePortableEffect& effect, std::string& reason) {
    const bool hdr = effect.profile == "mrt-hdr-v1";
    const std::vector<std::string> names =
        hdr ? std::vector<std::string>{"Split", "Combine", "ToneMap", "Finish"}
            : std::vector<std::string>{"Grade", "Finish"};
    if (!effect.post || (!hdr && effect.profile != "post-v1") ||
        effect.passes.size() != names.size()) {
        reason = "MME_post_layout_not_supported";
        return false;
    }
    for (std::size_t i = 0; i < names.size(); ++i)
        if (effect.passes[i].name != names[i]) {
            reason = "MME_post_layout_not_supported";
            return false;
        }
    using F = MmePortableFormat;
    effect.targets = {{"SceneColor", F::Rgba8}};
    if (hdr) {
        for (const auto* name : {"LayerA", "LayerB", "Hdr"})
            effect.targets.push_back({name, F::Rgba16Float});
        effect.targets.push_back({"Tone", F::Rgba8});
        effect.passes[0].sampled = {"SceneColor"};
        effect.passes[0].colors = {"LayerA", "LayerB"};
        effect.passes[1].sampled = {"LayerA", "LayerB"};
        effect.passes[1].colors = {"Hdr"};
        effect.passes[2].sampled = {"Hdr"};
        effect.passes[2].colors = {"Tone"};
        effect.passes[3].sampled = {"Tone"};
        effect.passes[3].colors = {"FinalColor"};
    } else {
        effect.targets.push_back({"Ping", F::Rgba8});
        effect.passes[0].sampled = {"SceneColor"};
        effect.passes[0].colors = {"Ping"};
        effect.passes[1].sampled = {"Ping"};
        effect.passes[1].colors = {"FinalColor"};
    }
    effect.targets.push_back({"FinalColor", F::Rgba8});
    return true;
}
bool MmePortableEffects(std::vector<MmePortableEffect>& out, std::string& reason, unsigned turn) {
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
    const auto* turnItem = turn ? FindTurn(turn) : nullptr;
    if (turn && !turnItem) {
        reason = "MME_stale_turn";
        return false;
    }
    std::map<int, mme::ModelData*> sceneOrder;
    if (mme::g_context) {
        for (bool accessory : {false, true}) {
            const int count = accessory ? ExpGetAcsNum() : ExpGetPmdNum();
            for (int i = 0; i < count; ++i) {
                const auto id = reinterpret_cast<unsigned long long>(accessory ? ExpGetAcsID(i)
                                                                               : ExpGetPmdID(i));
                const auto found = mme::g_context->modelRegistry.find(id);
                if (found != mme::g_context->modelRegistry.end() && found->second)
                    sceneOrder[found->second->passPlanScratch().renderOrder] = found->second;
            }
        }
    }
    for (const auto& entry : mme::g_ownerManager->bindings) {
        // Turn discovery retires derived bindings whose owner disappeared.
        // Each scope exports only the assignments from that native turn.
        if (entry.first.turnId != turn)
            continue;
        if (turnItem && !mme::MmeOffscreenObjectShown(*turnItem->offscreen, entry.first.model,
                                                      turnItem->carrier, entry.first.subset))
            continue;
        const auto* b = entry.second;
        if (!b || !b->effect || b->effectPath.empty())
            continue;
        MmePortableEffect value;
        if (!Describe(entry.first, *entry.second, value, reason))
            return false;
        // Native scene walks use the base frame's carrier list. A child
        // assignment cannot promote an ordinary model into that list.
        if (turn && value.post && entry.first.model->renderClass() != 2) {
            reason = "MME_offscreen_scene_carrier_not_supported";
            return false;
        }
        if (value.post && !Visible(*entry.first.model))
            continue;
        if (value.post) {
            const auto position = sceneOrder.find(entry.first.model->passPlanScratch().renderOrder);
            if (position == sceneOrder.end() || position->second != entry.first.model)
                continue;
        }
        value.turn = turn;
        out.push_back(std::move(value));
    }
    for (const auto& effect : out)
        if (effect.post)
            for (const auto& entry : mme::g_ownerManager->bindings)
                if (entry.first.turnId == turn && entry.first.model &&
                    entry.first.model->objectId() == effect.object && entry.first.subset >= 0) {
                    reason = "MME_scene_subset_override_not_supported";
                    return false;
                }
    std::stable_sort(out.begin(), out.end(), [&](const auto& a, const auto& b) {
        const auto order = [&](auto id) {
            for (const auto& row : sceneOrder)
                if (row.second->objectId() == id)
                    return row.first;
            return 0;
        };
        return order(a.object) < order(b.object);
    });
    return true;
}
bool MmePortableTurns(std::vector<MmePortableTurn>& out, std::string& reason) {
    out.clear();
    portableTurnPlan.clear();
    if (!MmeHostEffectsEnabled() || !mme::g_ownerManager)
        return true;
    if (!mme::MmePortableTurnPlan(portableTurnPlan)) {
        reason = "MME_offscreen_graph_limit";
        return false;
    }
    for (const auto& item : portableTurnPlan) {
        MmePortableTurn turn;
        turn.id = item.turnId;
        if (!item.assignment || !item.offscreen || !item.offscreen->hasClearColor ||
            !item.offscreen->hasClearDepth) {
            reason = "MME_offscreen_requires_clear";
            return false;
        }
        const auto& resource = *item.offscreen;
        MmeLegacyTexture declaration{resource.name, "OFFSCREENRENDERTARGET"};
        D3DXPARAMETER_DESC d{};
        item.assignment->effect->GetParameterDesc(resource.param, &d);
        declaration.shared = bool(d.Flags & D3DX_PARAMETER_SHARED);
        if (!ExportTarget(item.assignment->effect, declaration, turn.target, reason))
            return false;
        const auto c = resource.clearColorArgb;
        turn.clearColor = {float((c >> 16) & 255) / 255, float((c >> 8) & 255) / 255,
                           float(c & 255) / 255, float((c >> 24) & 255) / 255};
        turn.clearDepth = resource.clearDepth;
        if (!MmePortableEffects(turn.effects, reason, turn.id))
            return false;
        out.push_back(std::move(turn));
    }
    return true;
}
int MmePortableDraw(unsigned long long id, int subset, int mode, unsigned flags,
                    MmePortableEffect& out, std::string& reason, unsigned turn) {
    out = {};
    reason.clear();
    if (!MmeHostEffectsEnabled() || !mme::g_ownerManager)
        return 0;
    auto& manager = *mme::g_ownerManager;
    for (auto* object : manager.registeredObjects) {
        if (!object || object->objectId() != id)
            continue;
        if (turn) {
            const auto* item = FindTurn(turn);
            if (!item) {
                reason = "MME_stale_turn";
                return -1;
            }
            if (!mme::MmeOffscreenObjectShown(*item->offscreen, object, item->carrier, subset))
                return 2;
            const auto* value =
                mme::MmeOffscreenEffectValue(*item->offscreen, object, item->carrier, subset);
            if (!value)
                return 2; // Native offscreen lookup misses swallow the draw.
            if (value && *value != "none" && *value != "hide" &&
                !mme::MmeResolveTurnEffectBinding(turn, *item->offscreen, item->carrier, object,
                                                  subset) &&
                (*value != "main_default" || !object->effectFile().empty())) {
                reason = "MME_offscreen_assignment_failed";
                return -1;
            }
        }
        auto found = manager.bindings.find({turn, object, subset});
        if (found == manager.bindings.end())
            found = manager.bindings.find({turn, object, -1});
        if (found == manager.bindings.end() || !found->second || !found->second->effect ||
            found->second->effectPath.empty())
            return 0;
        if (!Describe(found->first, *found->second, out, reason))
            return -1;
        out.turn = turn;
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

bool MmePortableExportScene(mme::SasEffect& sas, MmePortableEffect& out, std::string& reason) {
    return ExportScript(sas, out, reason);
}
