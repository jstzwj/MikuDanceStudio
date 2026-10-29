#include "mikudancestudio/model_cpu_preview.hpp"
#ifdef MIKUDANCESTUDIO_CPU_RENDERING
#include "mikudancestudio/mmd_app.hpp"
#include "mikudancestudio/render_scene_frame.hpp"
#ifdef MIKUDANCESTUDIO_DIAG
#include "mikudancestudio/mme_bridge.hpp"
#endif

namespace mikudancestudio {
namespace {
render::CpuSceneFrameAssembler assembler;
rhi::ModelReplayStream frame;
bool frameReady = false, expectedDraw = false, expectedEmptyMesh = false;
#ifdef MIKUDANCESTUDIO_DIAG
bool Flag(const char* name) {
    char value[2]{};
    return GetEnvironmentVariableA(name, value, 2) == 1 && value[0] == '1';
}
#endif
}
bool CpuSceneInputsRequested() {
    return EditorRhiRequested();
}
bool CpuSceneFrameRecording() {
    return assembler.Recording();
}
void BeginCpuSceneFrame(MMDApp* app, unsigned flags, unsigned color) {
    frameReady = expectedDraw = expectedEmptyMesh = false;
#ifdef MIKUDANCESTUDIO_DIAG
    // This legacy ordered-stream assembler serves explicit native checkpoints.
    // The editor viewport has its own live RenderFrame producer and no second
    // preview renderer, window, menu or environment-selected backend.
    if (!Flag("MDS_RHI_CPU_FULL_SCENE"))
        return;
    if (Flag("MDS_RHI_CPU_LIVE_HISTORY") && !CpuSceneNativeCheckpointPending())
        return;
    render::SceneFrameDesc desc;
    const auto& pp = app->Renderer()->presentParameters;
    desc.target.width = pp.BackBufferWidth;
    desc.target.height = pp.BackBufferHeight;
    desc.frame = app->CpuRenderFrame().scene.frame;
    desc.initializedRegion = app->CpuRenderFrame().view.viewport;
    desc.clear = {((color >> 16) & 255) / 255.f, ((color >> 8) & 255) / 255.f,
                  (color & 255) / 255.f, ((color >> 24) & 255) / 255.f};
    assembler.Begin(desc);
    if (!app->CpuRenderFrameReady())
        assembler.Block("cpu_scene_frame_unavailable");
    if ((flags & 7) != 7 || pp.MultiSampleType != D3DMULTISAMPLE_NONE)
        assembler.Block("single_sample_color_depth_stencil_required");
    if (mme::EffectsEnabled() && !Flag("MDS_RHI_MODEL_NO_MME"))
        assembler.Block("MME_not_supported");
    if (app->PictureBackgroundEnabled() || app->AviBackgroundEnabled())
        assembler.Block("background_not_supported");
    if (app->WireframeRenderingEnabled())
        assembler.Block("wireframe_not_supported");
    if (app->SelfShadowCompositionEnabled() && app->SelfShadowMode() > 0)
        assembler.Block("self_shadow_composition_not_supported");
    if (app->DepthTextureCompositionEnabled() && app->DepthDeviceEnabled() &&
        app->DepthTextureCallback())
        assembler.Block("depth_callback_composition_not_supported");
#endif
}
void AppendCpuSceneFrame(render::SceneDrawReplacement draw) {
    if (!assembler.Recording())
        return;
    if (expectedDraw || expectedEmptyMesh) {
        assembler.Block("cpu_draw_without_submission");
        return;
    }
    assembler.Append(std::move(draw));
    expectedDraw = assembler.Recording();
}
void ObserveCpuSceneDraw(const char* pass) {
    if (!assembler.Recording())
        return;
    if (!expectedDraw) {
        const auto reason = std::string("draw_without_cpu_producer_") + (pass ? pass : "unknown");
        assembler.Block(reason.c_str());
    }
    expectedDraw = false;
}
void ExpectCpuSceneEmptyMesh() {
    if (assembler.Recording()) {
        if (expectedDraw || expectedEmptyMesh)
            assembler.Block("cpu_draw_without_submission");
        else
            expectedEmptyMesh = true;
    }
}
bool ConsumeCpuSceneEmptyMesh() {
    const bool empty = expectedEmptyMesh;
    expectedEmptyMesh = false;
    return empty;
}
void BlockCpuSceneFrame(const char* reason) {
    assembler.Block(reason);
}
void FinishCpuSceneFrame(MMDApp*) {
    if (expectedDraw || expectedEmptyMesh)
        assembler.Block("cpu_draw_without_submission");
    expectedDraw = expectedEmptyMesh = false;
    frameReady = bool(assembler.Finish(frame));
}
bool CopyAssembledCpuSceneFrame(rhi::ModelReplayStream& output) {
    if (!frameReady)
        return false;
    output = frame;
    return true;
}
void ReleaseCpuSceneFrame() {
    assembler.Reset();
    frame = {};
    frameReady = expectedDraw = expectedEmptyMesh = false;
}
}
#endif
