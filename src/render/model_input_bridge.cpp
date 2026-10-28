#include "legacy_model_cpu_math.hpp"
#include "mikudancestudio/model_input_bridge.hpp"
#include "mikudancestudio/mmd_app.hpp"
#include "mikudancestudio/model.hpp"
#include "mikudancestudio/d3dx_dyn.hpp"
#include "mikudancestudio/model_cpu_preview.hpp"
#include "mikudancestudio/mme_bridge.hpp"
#include "mikudancestudio/render_snapshot.hpp"
#include "mikudancestudio/render_scene_passes.hpp"
#include "mikudancestudio/rhi_history_replay.hpp"
#include "mikudancestudio/rhi_d3d9.hpp"
#include <fstream>
#include <cstring>
#include <algorithm>
#include <cstdio>

namespace mikudancestudio {
void SetCpuRenderViewport(MMDApp* app, const rhi::ModelViewport& view) {
    app->CpuRenderFrame().view.viewport = view;
}
void SetCpuProjectionPolicy(MMDApp* app, render::FovConversion policy) {
    app->CpuRenderFrame().view.fovConversion = policy;
}
void UpdateCpuRenderFrame(MMDApp* app, const float* world, const float* lightRotation) {
    app->CpuRenderFrameReady() = false;
    const auto* session = app->PortableSceneSession();
    if (!session || !session->Loaded())
        return;
    auto& frame = app->CpuRenderFrame();
    const auto& pose = session->Pose();
    frame.scene.frame = pose.frame;
    frame.scene.camera = session->RuntimeAppliedCamera().value_or(pose.camera);
    frame.scene.lightState = session->RuntimeAppliedLight().value_or(pose.lightState);
    std::copy_n(world, 16, frame.world.begin());
    std::copy_n(lightRotation, 16, frame.lightRotation.begin());
    frame.view.aspect = app->Renderer()->aspectRatio;
    frame.view.editorNavigation = app->CameraMode() == 0;
    frame.view.editorEye = {app->ViewOffsetX(), app->ViewOffsetY(), app->CameraDistance()};
    frame.view.editorFov = app->CameraFov();
    frame.view.editorPerspective = app->CameraPerspective();
    app->CpuRenderFrameReady() = true;
}
#ifdef MIKUDANCESTUDIO_CPU_RENDERING
namespace {
bool Requested(const char* name) {
    char flag[2]{};
    return GetEnvironmentVariableA(name, flag, 2) == 1 && flag[0] == '1';
}
render::ModelInputMath LegacyCpuMath() {
    return LegacyCpuRenderMath();
}
// Asset decoding is shared with non-Windows hosts; this callback only provides
// the raw builtin bytes. No texture object or native resource cache is read.
rhi::CallbackAssetSource Builtin(MMDApp* app) {
    return rhi::CallbackAssetSource([app](rhi::AssetId id, std::vector<std::uint8_t>& bytes) {
        const auto instance = static_cast<HMODULE>(app->HInstance());
        const auto resource = FindResourceA(instance, MAKEINTRESOURCEA(unsigned(id)), "PNG");
        if (!resource)
            return false;
        const auto size = SizeofResource(instance, resource);
        const auto loaded = LoadResource(instance, resource);
        const auto* data =
            static_cast<const std::uint8_t*>(loaded ? LockResource(loaded) : nullptr);
        if (!data || !size)
            return false;
        bytes.assign(data, data + size);
        return true;
    });
}
}
bool BuildLiveCpuModelInputs(MMDApp* app, mdl::ModelRecord* model, unsigned materialIndex,
                             rhi::ModelPipeline& pipeline,
                             std::array<rhi::MaterialTextureAsset, 3>& assets, const char*& reason,
                             bool finalSolvedGeometry) {
    reason = "cpu_frame_unavailable";
    if (!app->CpuRenderFrameReady())
        return false;
    if (app->state.stereoActivated) {
        reason = "stereo_not_supported";
        return false;
    }
    const auto found = app->CpuModelFrames().find(model);
    if (found == app->CpuModelFrames().end())
        return false;
    const auto& state = *model;
    if (materialIndex >= state.materialCount) {
        reason = "material_range";
        return false;
    }
    if (state.displayState || state.postLoadFlag2) {
        reason = "material_pass_override";
        return false;
    }
    if (state.physicsMode == 2) {
        reason = "pmx_live_inputs_not_validated";
        return false;
    }
    const auto* session = app->PortableSceneSession();
    if (!session) {
        reason = "scene_session_unavailable";
        return false;
    }
    bool independent = false;
    for (std::size_t index = 0; index < session->Document().models.size(); ++index)
        if (app->ModelSlot(session->Document().models[index].slot) == model &&
            session->Models()[index]) {
            unsigned first = 0;
            for (unsigned m = 0; m < materialIndex; ++m)
                first += (model)->materials[m].faceVertexCount;
            independent = render::ClassifyBatchDependency(
                              *session->Models()[index], first,
                              (model)->materials[materialIndex].faceVertexCount) ==
                          render::BatchDependency::Independent;
            break;
        }
    if (!independent && !finalSolvedGeometry) {
        reason = "dependent_model_batch";
        return false;
    }
    const auto& record = (model)->materials[materialIndex];
    render::ModelMaterialInputs material;
    std::copy_n(record.diffuse, 4, material.diffuse.begin());
    std::copy_n(record.diffuseMirror, 4, material.ambient.begin());
    std::copy_n(record.specular, 3, material.specular.begin());
    std::memcpy(&material.specular[3], record.gap0, 4);
    std::copy_n(record.ambient, 3, material.emissive.begin());
    std::memcpy(&material.emissive[3], record.gap1, 4);
    material.shininess = record.specularPower;
    material.pmd = state.physicsMode != 2;
    material.doubleSided = !material.pmd && (record.flags & 1);
    material.base = record.texturePath;
    material.sphere = record.spherePath;
    material.sphereMode = record.sphereMode;
    auto& saved = found->second->materials[materialIndex];
    rhi::ToonRequest request;
    request.modelDirectory = state.modelDirectory;
    request.missing =
        material.pmd ? rhi::MissingToonPolicy::Unbound : rhi::MissingToonPolicy::Default;
    if (record.toonReference != 255) {
        request.kind = rhi::ToonKind::ModelFile;
        if (material.pmd && record.toonReference < 10) {
            const auto* name = (model)->pmdToonFileNames[record.toonReference];
            const int length = MultiByteToWideChar(932, 0, name, -1, nullptr, 0);
            if (length <= 0) {
                reason = "toon_encoding";
                return false;
            }
            std::wstring path(length, L'\0');
            MultiByteToWideChar(932, 0, name, -1, path.data(), length);
            path.pop_back();
            request.path = path;
            wchar_t shared[20]{};
            swprintf_s(shared, L"toon%02u.bmp", unsigned(record.toonReference) + 1);
            if (path == shared) {
                request.kind = rhi::ToonKind::Shared;
                request.sharedSlot = unsigned(record.toonReference) + 1;
            }
        } else if (material.pmd) {
            reason = "toon_slot";
            return false;
        } else {
            request.path = record.toonPath;
            if ((record.flags & 0x80) != 0) {
                request.kind = rhi::ToonKind::Shared;
                request.sharedSlot = unsigned(record.toonReference) + 1;
            }
        }
    }
    const bool same = saved.assets[0].content.revision && saved.request.kind == request.kind &&
                      saved.request.sharedSlot == request.sharedSlot &&
                      saved.request.path == request.path &&
                      saved.request.modelDirectory == request.modelDirectory &&
                      saved.base == material.base && saved.sphere == material.sphere;
    if (!same) {
        auto builtin = Builtin(app);
        rhi::ToonTextureResolver resolver(std::filesystem::path(app->ExeDir()) / L"data", builtin);
        render::ModelMaterialTextureSources next;
        next.request = request;
        next.base = material.base;
        next.sphere = material.sphere;
        if (!resolver.Load(request, next.assets[0])) {
            reason = "missing_cpu_toon";
            return false;
        }
        if (!material.base.empty() &&
            rhi::LoadMaterialTexture(request.modelDirectory, material.base, next.assets[1]) !=
                rhi::MaterialResourceStatus::Ready) {
            reason = "missing_cpu_base";
            return false;
        }
        if (!material.sphere.empty() &&
            rhi::LoadMaterialTexture(request.modelDirectory, material.sphere, next.assets[2]) !=
                rhi::MaterialResourceStatus::Ready) {
            reason = "missing_cpu_sphere";
            return false;
        }
        char mipMode[16]{};
        GetEnvironmentVariableA("MDS_RHI_CPU_MIP_PREPARE", mipMode, 16);
        if (!mipMode[0])
            std::strcpy(mipMode, "box");
        if (mipMode[0]) {
            const auto policy = std::strcmp(mipMode, "dx9") == 0
                                    ? rhi::MipPreparationPolicy::Dx9AutoLinear
                                    : rhi::MipPreparationPolicy::BoxLinear;
            auto preparer = policy == rhi::MipPreparationPolicy::Dx9AutoLinear
                                ? rhi::CreateD3D9TexturePreparer(app->Renderer()->device)
                                : nullptr;
            for (unsigned assetIndex = 1; assetIndex < 3; ++assetIndex)
                if (next.assets[assetIndex].content.revision) {
                    auto& asset = next.assets[assetIndex];
                    if (policy == rhi::MipPreparationPolicy::Dx9AutoLinear) {
                        const auto& raw = asset.content.image;
                        std::uint32_t width = 0, height = 0;
                        if (!preparer || !rhi::NextPowerOfTwoTextureSize(raw.width, 4096, width) ||
                            !rhi::NextPowerOfTwoTextureSize(raw.height, 4096, height)) {
                            reason = "cpu_texture_size_policy";
                            return false;
                        }
                        if (width != raw.width || height != raw.height) {
                            if (!preparer->Prepare(raw, width, height, asset.prepared.image)) {
                                reason = "cpu_texture_resize_failed";
                                return false;
                            }
                            asset.prepared.revision = rhi::ImageRevision(asset.prepared.image);
                            asset.usePrepared = true;
                        }
                    }
                    if (!rhi::PrepareMaterialMipChain(asset, policy, preparer.get(), true)) {
                        reason = "cpu_mip_preparation_failed";
                        return false;
                    }
#ifdef MIKUDANCESTUDIO_DIAG
                    char directory[32768]{};
                    GetEnvironmentVariableA("MDS_RHI_CPU_GEOMETRY_DIR", directory,
                                            sizeof(directory));
                    if (directory[0]) {
                        const auto name = std::string(directory) + "/mip_preparation." +
                                          std::to_string(GetCurrentProcessId()) + ".csv";
                        const bool existed = std::filesystem::exists(name);
                        std::ofstream report(name, std::ios::app);
                        if (!existed)
                            report
                                << "model,material,asset,sourceRevision,providerRevision,preparedRevision,policy,levels,origin,decodedRevision,width,height\n";
                        unsigned slot = 0;
                        for (; slot < 255; ++slot)
                            if (app->ModelSlot(slot) == model)
                                break;
                        const auto& chain = *asset.mipChain;
                        report << slot << "," << materialIndex << "," << asset.identity << ","
                               << chain.sourceRevision << "," << chain.providerRevision << ","
                               << chain.revision << "," << mipMode << "," << chain.levels.size()
                               << ",independent_cpu_source," << asset.content.revision << ","
                               << chain.levels[0].width << "," << chain.levels[0].height << "\n";
                    }
#endif
                }
        }
        saved = std::move(next);
    }
    render::ModelPassSettings pass;
    pass.alphaTest = app->CpuAlphaTestSupported();
    pass.stencil = app->Renderer()->d3dInitialized != 0;
    pass.anisotropic = app->Renderer()->runtimeToggle != 0;
    pass.maxAnisotropy = pass.anisotropic ? app->Renderer()->shaderModelCaps : 1;
    if (!render::BuildModelInputs(app->CpuRenderFrame(), material, pass, pipeline,
                                  LegacyCpuMath())) {
        reason = "unsupported_cpu_pipeline";
        return false;
    }
    assets = {};
    assets[0] = saved.assets[0];
    for (unsigned stage = 1; stage < 3; ++stage) {
        const auto role = pipeline.shader.layout.stages[stage].role;
        assets[stage] = role == rhi::MaterialTextureRole::Sphere && !material.sphere.empty()
                            ? saved.assets[2]
                            : saved.assets[1];
        if (role != rhi::MaterialTextureRole::None &&
            assets[stage].mips != rhi::TextureDesc::Mips::SingleLevel) {
            reason = "explicit_cpu_mips_required";
            return false;
        }
    }
    reason = nullptr;
    return true;
}
// Native reference capture lives in model_batch_bridge.cpp, outside production.
bool AuditLiveCpuModelInputs(MMDApp*, mdl::ModelRecord*, unsigned, unsigned, unsigned,
                             const rhi::ModelPipeline&,
                             const std::array<rhi::MaterialTextureAsset, 3>&, rhi::ModelPipeline&);
void SubmitLiveCpuModelInputs(MMDApp* app, mdl::ModelRecord* model, unsigned material,
                              unsigned first, unsigned count, unsigned stride) {
    if (!Requested("MDS_RHI_CPU_INPUTS") && !CpuScenePreviewRequested())
        return;
    if (CpuSceneFamilyRequested(render::SceneDrawFamily::ModelMaterial) &&
        CpuSceneHistoryRecording() &&
        (!CpuMaterialSelected(material) || Requested("MDS_RHI_CPU_FULL_SCENE") ||
         CpuScenePreviewRequested())) {
        const auto found = app->CpuModelFrames().find(model);
        rhi::ModelPipeline cpuPipeline;
        std::array<rhi::MaterialTextureAsset, 3> cpuAssets;
        const char* why = nullptr;
        if (found == app->CpuModelFrames().end() || !found->second->ready || stride != 32 ||
            !BuildLiveCpuModelInputs(app, model, material, cpuPipeline, cpuAssets, why, true)) {
            BlockHistoryReplay(why ? why : "cpu_prefix_model_geometry");
            return;
        }
        render::ModelSubmissionBuilder builder;
        render::SceneDrawReplacement replacement;
        replacement.family = render::SceneDrawFamily::ModelMaterial;
        replacement.material = material;
        for (unsigned slot = 0; slot < 255; ++slot)
            if (app->ModelSlot(slot) == model)
                replacement.model = slot;
        rhi::HistoryDesc target;
        const auto& pp = app->Renderer()->presentParameters;
        target.width = pp.BackBufferWidth;
        target.height = pp.BackBufferHeight;
        const auto result =
            builder.Build(found->second->geometry.Snapshot(), target, {1, 1, 1, 1},
                          {{material, first, count, cpuPipeline, cpuAssets}}, replacement.cpu);
        for (unsigned slot = 0; slot < 3; ++slot)
            replacement.textureIdentities[slot] = cpuAssets[slot].identity;
        if (!result) {
            BlockHistoryReplay(result.reason.c_str());
            return;
        }
        replacement.cpu.source.frame = app->CpuRenderFrame().scene.frame;
        StageCpuSceneHistoryDraw(std::move(replacement));
    }

#ifdef MIKUDANCESTUDIO_DIAG
    // Ordinary resident frames produce CPU data only. Native field/image
    // audits and isolated subset probes run at explicit diagnostic checkpoints.
    if (CpuScenePreviewRequested() && !CpuSceneNativeCheckpointPending())
        return;

    if (!CpuMaterialSelected(material))
        return;
    const auto host = mme::ProbeDrawSnapshot();
    if (stride != 32 || (mme::EffectsActive() && !host.effectsDisabled)) {
        BlockCpuModelPreview(app, model, material, "unsupported_live_pass");
        return;
    }
    rhi::ModelPipeline pipeline;
    std::array<rhi::MaterialTextureAsset, 3> assets;
    const char* reason = nullptr;
    if (!BuildLiveCpuModelInputs(app, model, material, pipeline, assets, reason,
                                 CpuSceneFamilyRequested(render::SceneDrawFamily::ModelMaterial))) {
        BlockCpuModelPreview(app, model, material, reason);
        return;
    }
    rhi::ModelPipeline reference;
    if (Requested("MDS_RHI_CPU_INPUTS_AUDIT") &&
        !AuditLiveCpuModelInputs(app, model, material, first, count, pipeline, assets, reference)) {
        BlockCpuModelPreview(app, model, material, "cpu_input_audit_failed");
        return;
    }
    // Direct mode requires a concrete externally recorded parity gate. It is
    // diagnostic only and never enables replacement of the application's draw.
    if (!Requested("MDS_RHI_CPU_INPUTS_AUDIT") && !Requested("MDS_RHI_CPU_INPUTS_ACCEPTED")) {
        BlockCpuModelPreview(app, model, material, "cpu_input_gate_required");
        return;
    }
    const auto& v = *pipeline.viewport;
    rhi::HistoryDesc target;
    target.width = v.x + v.width;
    target.height = v.y + v.height;
    SubmitCpuModelPreview(app, model, material, first, count, target, pipeline, assets,
                          Requested("MDS_RHI_CPU_INPUTS_AUDIT") ? &reference : nullptr);
#endif
}
namespace {
render::ScenePassSettings CpuPassSettings(MMDApp* app) {
    render::ScenePassSettings p;
    p.stencil = app->Renderer()->d3dInitialized != 0;
    p.alphaTest = app->CpuAlphaTestSupported();
    p.maxAnisotropy = app->Renderer()->runtimeToggle ? app->Renderer()->shaderModelCaps : 1;
    p.shadowBlend = app->ProjectedShadowBlendEnabled() != 0;
    const auto material = app->ProjectedShadowMaterial();
    std::memcpy(p.shadowMaterial.diffuse.data(), &material.Diffuse, 16);
    std::memcpy(p.shadowMaterial.ambient.data(), &material.Ambient, 16);
    std::memcpy(p.shadowMaterial.specular.data(), &material.Specular, 16);
    std::memcpy(p.shadowMaterial.emissive.data(), &material.Emissive, 16);
    p.shadowMaterial.shininess = material.Power;
    return p;
}
rhi::HistoryDesc CpuTarget(MMDApp* app) {
    rhi::HistoryDesc d;
    d.width = app->Renderer()->presentParameters.BackBufferWidth;
    d.height = app->Renderer()->presentParameters.BackBufferHeight;
    return d;
}
}
void StageCpuGroundDraw(MMDApp* app, bool plane) {
    const auto family =
        plane ? render::SceneDrawFamily::GroundPlane : render::SceneDrawFamily::GroundLines;
    if (!CpuSceneFamilyRequested(family) || !CpuSceneHistoryRecording())
        return;
    render::SceneDrawReplacement replacement;
    replacement.family = family;
    if (!app->CpuRenderFrameReady() ||
        !render::BuildGroundDraw(app->CpuRenderFrame(), family, CpuPassSettings(app),
                                 CpuTarget(app), {1, 1, 1, 1}, replacement.cpu, LegacyCpuMath())) {
        BlockHistoryReplay("cpu_ground_producer");
        return;
    }
    StageCpuSceneHistoryDraw(std::move(replacement));
}
void StageCpuProjectedModelShadow(MMDApp* app, mdl::ModelRecord* model, unsigned material,
                                  unsigned first, unsigned count) {
    if (!CpuSceneFamilyRequested(render::SceneDrawFamily::ProjectedModelShadow) ||
        !CpuSceneHistoryRecording())
        return;
    const auto found = app->CpuModelFrames().find(model);
    render::SceneDrawReplacement replacement;
    replacement.family = render::SceneDrawFamily::ProjectedModelShadow;
    replacement.material = material;
    for (unsigned slot = 0; slot < 255; ++slot)
        if (app->ModelSlot(slot) == model)
            replacement.model = slot;
    rhi::ModelReplayDraw draw;
    if (!app->CpuRenderFrameReady() || found == app->CpuModelFrames().end() ||
        !found->second->ready ||
        !render::BuildScenePassInputs(app->CpuRenderFrame(), replacement.family,
                                      CpuPassSettings(app), draw.pipeline, LegacyCpuMath())) {
        BlockHistoryReplay("cpu_shadow_producer");
        return;
    }
    replacement.cpu.target = CpuTarget(app);
    replacement.cpu.source.frame = app->CpuRenderFrame().scene.frame;
    replacement.cpu.events.push_back({1, rhi::ModelReplayOp::Clear});
    auto builtin = Builtin(app);
    rhi::AtlasAsset helper;
    if (!rhi::LoadAtlas(builtin, rhi::AssetId::AccessoryHelper, helper)) {
        BlockHistoryReplay("cpu_shadow_helper_asset");
        return;
    }
    rhi::ModelReplayResource texture;
    texture.key = {0x8000000000000001ull, helper.revision};
    texture.kind = rhi::ReplayResourceKind::Texture;
    texture.texture.levels = {std::move(helper.image)};
    replacement.cpu.resources.push_back(texture);
    replacement.textureIdentities[0] = "builtin:accessory_helper";
    replacement.cpu.events.push_back({2, rhi::ModelReplayOp::PublishResource, texture.key});
    draw.textures[0] = texture.key;
    draw.firstIndex = first;
    draw.indexCount = count;
    const auto result = render::AppendModelGeometry(replacement.cpu,
                                                    found->second->geometry.Snapshot(), draw, true);
    replacement.cpu.complete = true;
    if (!result || !rhi::ValidateModelReplay(replacement.cpu)) {
        BlockHistoryReplay("cpu_shadow_geometry");
        return;
    }
    StageCpuSceneHistoryDraw(std::move(replacement));
}
void StageCpuModelOutline(MMDApp* app, mdl::ModelRecord* model, unsigned material, unsigned first,
                          unsigned count) {
    if (!CpuSceneFamilyRequested(render::SceneDrawFamily::ModelOutline) ||
        !CpuSceneHistoryRecording())
        return;
    const auto found = app->CpuModelFrames().find(model);
    render::SceneDrawReplacement replacement;
    replacement.family = render::SceneDrawFamily::ModelOutline;
    replacement.material = material;
    for (unsigned slot = 0; slot < 255; ++slot)
        if (app->ModelSlot(slot) == model)
            replacement.model = slot;
    rhi::ModelReplayDraw draw;
    if (!app->CpuRenderFrameReady() || found == app->CpuModelFrames().end() ||
        !found->second->ready || model->physicsMode == 2 ||
        !render::BuildScenePassInputs(app->CpuRenderFrame(), replacement.family,
                                      CpuPassSettings(app), draw.pipeline, LegacyCpuMath())) {
        BlockHistoryReplay("cpu_outline_producer");
        return;
    }
    replacement.cpu.target = CpuTarget(app);
    replacement.cpu.source.frame = app->CpuRenderFrame().scene.frame;
    replacement.cpu.events.push_back({1, rhi::ModelReplayOp::Clear});
    draw.firstIndex = first;
    draw.indexCount = count;
    const auto result = render::AppendModelGeometry(replacement.cpu,
                                                    found->second->geometry.Snapshot(), draw, true);
    replacement.cpu.complete = true;
    if (!result || !rhi::ValidateModelReplay(replacement.cpu)) {
        BlockHistoryReplay("cpu_outline_geometry");
        return;
    }
    StageCpuSceneHistoryDraw(std::move(replacement));
}
void StageCpuAccessoryDraw(MMDApp* app, void* accessory, unsigned materialIndex,
                           const float* placement, bool projected) {
    if (!CpuSceneFamilyRequested(render::SceneDrawFamily::AccessoryMaterial) ||
        !CpuSceneHistoryRecording())
        return;
    if (projected) {
        BlockHistoryReplay("cpu_accessory_projected_shadow_not_supported");
        return;
    }
    const auto found = app->CpuAccessoryResources().find(accessory);
    if (!app->CpuRenderFrameReady() || found == app->CpuAccessoryResources().end() ||
        !found->second.failure.empty()) {
        BlockHistoryReplay(found != app->CpuAccessoryResources().end()
                               ? found->second.failure.c_str()
                               : "cpu_accessory_import_unavailable");
        return;
    }
    auto& resource = found->second;
    const auto& record = *mdl::Accessory(accessory);
    if (materialIndex >= record.materialCount || materialIndex >= resource.materials.size() ||
        materialIndex >= resource.texturesReady.size() || !record.materials) {
        BlockHistoryReplay("cpu_accessory_material_range");
        return;
    }
    auto frame = app->CpuRenderFrame();
    render::ModelMatrix local;
    std::copy_n(placement, 16, local.begin());
    LegacyCpuMath().multiply(frame.world, local, frame.world);
    const auto* nativeMaterial = static_cast<const D3DMATERIAL9*>(record.materials) + materialIndex;
    render::AccessoryPassInputs input;
    std::memcpy(input.material.diffuse.data(), &nativeMaterial->Diffuse, 16);
    std::memcpy(input.material.ambient.data(), &nativeMaterial->Ambient, 16);
    std::memcpy(input.material.specular.data(), &nativeMaterial->Specular, 16);
    std::memcpy(input.material.emissive.data(), &nativeMaterial->Emissive, 16);
    input.afterModelPass = record.order >= app->AccessoryRenderSplitOrder();
    input.material.shininess = nativeMaterial->Power;
    input.opacity = record.opacity;
    input.additive = record.additiveBlend != 0;
    input.textureType = record.textureTypes ? record.textureTypes[materialIndex] : 0;
    if (input.textureType == 3) {
        input.unboundScreen = !(app->CaptureMode() == ScreenCaptureMode::BackgroundRefresh
                                    ? app->CpuNativeBackgroundTexturePresent()
                                    : app->CpuNativeCaptureTexturePresent());
        if (!input.unboundScreen) {
            BlockHistoryReplay("cpu_accessory_screen_source_native_only");
            return;
        }
    }
    auto& sources = resource.materials.at(materialIndex);
    if (!resource.texturesReady.at(materialIndex) && record.texturePaths) {
        const std::filesystem::path paths[]{record.texturePaths[materialIndex].primary,
                                            record.texturePaths[materialIndex].sphere};
        for (unsigned slot = 0; slot < 2; ++slot)
            if (!paths[slot].empty()) {
                auto& asset = sources.assets[slot];
                if (rhi::LoadMaterialTexture(record.directory, paths[slot], asset) !=
                    rhi::MaterialResourceStatus::Ready) {
                    BlockHistoryReplay("cpu_accessory_texture_missing");
                    return;
                }
                char mode[16]{};
                GetEnvironmentVariableA("MDS_RHI_CPU_MIP_PREPARE", mode, 16);
                const bool measured = std::strcmp(mode, "dx9") == 0;
                auto preparer =
                    measured ? rhi::CreateD3D9TexturePreparer(app->Renderer()->device) : nullptr;
                std::uint32_t width = 0, height = 0;
                const auto& raw = asset.content.image;
                if (measured &&
                    (!preparer || !rhi::NextPowerOfTwoTextureSize(raw.width, 4096, width) ||
                     !rhi::NextPowerOfTwoTextureSize(raw.height, 4096, height))) {
                    BlockHistoryReplay("cpu_accessory_texture_size");
                    return;
                }
                if (measured && (width != raw.width || height != raw.height)) {
                    if (!preparer->Prepare(raw, width, height, asset.prepared.image)) {
                        BlockHistoryReplay("cpu_accessory_texture_prepare");
                        return;
                    }
                    asset.prepared.revision = rhi::ImageRevision(asset.prepared.image);
                    asset.usePrepared = true;
                }
                if (!rhi::PrepareMaterialMipChain(asset,
                                                  measured
                                                      ? rhi::MipPreparationPolicy::Dx9AutoLinear
                                                      : rhi::MipPreparationPolicy::BoxLinear,
                                                  preparer.get(), true)) {
                    BlockHistoryReplay("cpu_accessory_mips");
                    return;
                }
            }
    }
    resource.texturesReady[materialIndex] = true;
    input.primaryTexture = sources.assets[0].content.revision != 0;
    input.sphereTexture = sources.assets[1].content.revision != 0;
    rhi::ModelPipeline pipeline;
    if (!render::BuildAccessoryPassInputs(frame, input, CpuPassSettings(app), pipeline,
                                          LegacyCpuMath())) {
#ifdef MIKUDANCESTUDIO_DIAG
        char directory[32768]{};
        GetEnvironmentVariableA("MDS_RHI_CPU_GEOMETRY_DIR", directory, sizeof(directory));
        std::ofstream report(std::string(directory) + "/cpu_accessory_rejected." +
                             std::to_string(GetCurrentProcessId()) + ".csv");
        report.precision(17);
        report << "field,component,value\n";
        report << "material,0," << materialIndex << "\nopacity,0," << input.opacity
               << "\ntextureType,0," << input.textureType << "\nscale,0," << record.scale
               << "\nshininess,0," << input.material.shininess << "\naspect,0," << frame.view.aspect
               << "\nanisotropy,0," << CpuPassSettings(app).maxAnisotropy << "\n";
        const auto values = [&](const char* field, const auto& data) {
            for (unsigned n = 0; n < data.size(); ++n)
                report << field << "," << n << "," << data[n] << "\n";
        };
        values("world", frame.world);
        values("placement", local);
        values("diffuse", input.material.diffuse);
        values("ambient", input.material.ambient);
        values("specular", input.material.specular);
        values("emissive", input.material.emissive);
        values("light", frame.scene.lightState);
#endif
        BlockHistoryReplay("cpu_accessory_pipeline");
        return;
    }
    const auto subset =
        std::find_if(resource.subsets.begin(), resource.subsets.end(),
                     [&](const auto& item) { return item.material == materialIndex; });
    if (subset == resource.subsets.end()) {
        ExpectCpuSceneEmptyMesh();
        return;
    } // DrawSubset with no faces is a no-op.
    if (std::count_if(resource.subsets.begin(), resource.subsets.end(),
                      [&](const auto& item) { return item.material == materialIndex; }) != 1) {
        BlockHistoryReplay("cpu_accessory_subset_missing");
        return;
    }
    render::ModelSubmissionBuilder builder;
    render::SceneDrawReplacement replacement;
    replacement.family = render::SceneDrawFamily::AccessoryMaterial;
    replacement.material = materialIndex;
    for (unsigned slot = 0; slot < 255; ++slot)
        if (app->AccessorySlot(slot) == accessory)
            replacement.model = slot;
    std::array<rhi::MaterialTextureAsset, 3> assets{sources.assets[0], sources.assets[1], {}};
    auto result =
        builder.Build(resource.geometry, CpuTarget(app), {1, 1, 1, 1},
                      {{materialIndex, subset->firstIndex, subset->indexCount, pipeline, assets}},
                      replacement.cpu);
    for (unsigned slot = 0; slot < 3; ++slot)
        replacement.textureIdentities[slot] = assets[slot].identity;
    if (!result) {
        BlockHistoryReplay(result.reason.c_str());
        return;
    }
    replacement.cpu.source.frame = frame.scene.frame;
    StageCpuSceneHistoryDraw(std::move(replacement));
}

#endif
}
