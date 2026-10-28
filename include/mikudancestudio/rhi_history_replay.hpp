#pragma once
#include <vector>
struct IDirect3DDevice9;
struct ID3DXMesh;
namespace mikudancestudio::rhi {
struct ModelReplayStream;
struct RgbaImage;
}
namespace mikudancestudio::render {
struct SceneDrawReplacement;
enum class SceneDrawFamily;
}
namespace mikudancestudio {
#ifdef MIKUDANCESTUDIO_DIAG
void BeginHistoryReplay(IDirect3DDevice9*, unsigned clearFlags, unsigned color, float z,
                        unsigned stencil);
bool CpuSceneFamilyRequested(render::SceneDrawFamily);
bool CpuSceneHistoryRecording();
void StageCpuSceneHistoryDraw(render::SceneDrawReplacement);
// Audited CPU replacements; native captured draws remain the reference only.
bool CopyCpuSceneHistoryReplay(rhi::ModelReplayStream&);
void RecordHistoryDraw(IDirect3DDevice9*, unsigned type, bool indexed, int base, unsigned minimum,
                       unsigned vertices, unsigned start, unsigned count);
void RecordHistoryMesh(IDirect3DDevice9*, ID3DXMesh*, unsigned attribute);
void ResetHistoryReplay();
// Explicit new diagnostic frame; ordinary Reset preserves the one-shot gate.
void RestartHistoryReplay();
void SuspendHistoryReplay();
// Diagnostic owned copy; only available after a complete validated prefix.
bool CopyPortableHistoryReplay(rhi::ModelReplayStream&);
// Native boundary proof is distinct from syntactic stream validation.
// Images are tightly packed RGBA, cropped to the recorded initial viewport.
bool CopyVerifiedHistoryImage(rhi::RgbaImage& before);
// Initial clear, then each native draw boundary; last image equals 'before'.
bool CopyVerifiedHistoryBoundaries(std::vector<rhi::RgbaImage>&);
bool ReadVerifiedHistoryImage(IDirect3DDevice9*, rhi::RgbaImage&);
void BlockHistoryReplay(const char* reason);
void SetHistoryReplayPass(const char* name);
void CompareHistoryReplay(IDirect3DDevice9*, unsigned slot, unsigned material, unsigned indices);
void CompleteHistoryReplay(IDirect3DDevice9*, unsigned material);
#else
#ifdef MIKUDANCESTUDIO_CPU_RENDERING
bool CpuSceneFamilyRequested(render::SceneDrawFamily);
bool CpuSceneHistoryRecording();
void StageCpuSceneHistoryDraw(render::SceneDrawReplacement);
void RecordHistoryDraw(IDirect3DDevice9*, unsigned, bool, int, unsigned, unsigned, unsigned,
                       unsigned);
void RecordHistoryMesh(IDirect3DDevice9*, ID3DXMesh*, unsigned);
void BlockHistoryReplay(const char*);
void SetHistoryReplayPass(const char*);
#else
inline void SetHistoryReplayPass(const char*) {
}
#endif
inline void ResetHistoryReplay() {
}
#endif
}
