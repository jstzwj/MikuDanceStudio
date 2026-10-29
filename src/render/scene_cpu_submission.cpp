#include "mikudancestudio/rhi_history_replay.hpp"
#if defined(MIKUDANCESTUDIO_CPU_RENDERING) && !defined(MIKUDANCESTUDIO_DIAG)
#include "mikudancestudio/model_cpu_preview.hpp"
#include "mikudancestudio/render_scene_passes.hpp"
namespace mikudancestudio {
namespace {
const char* pass = "unspecified";
}
bool CpuSceneFamilyRequested(render::SceneDrawFamily) {
    return CpuSceneFrameRecording();
}
bool CpuSceneHistoryRecording() {
    return CpuSceneFrameRecording();
}
void StageCpuSceneHistoryDraw(render::SceneDrawReplacement draw) {
    AppendCpuSceneFrame(std::move(draw));
}
void RecordHistoryDraw(IDirect3DDevice9*, unsigned, bool, int, unsigned, unsigned, unsigned,
                       unsigned) {
    ObserveCpuSceneDraw(pass);
}
void RecordHistoryMesh(IDirect3DDevice9*, ID3DXMesh*, unsigned) {
    if (!ConsumeCpuSceneEmptyMesh())
        ObserveCpuSceneDraw(pass);
}
void BlockHistoryReplay(const char* reason) {
    BlockCpuSceneFrame(reason);
}
void SetHistoryReplayPass(const char* name) {
    pass = name;
}
}
#endif
