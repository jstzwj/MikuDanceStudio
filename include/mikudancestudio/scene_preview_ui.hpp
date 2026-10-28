#pragma once
namespace mikudancestudio {
class MMDApp;
constexpr unsigned PreviewCloseCommand = 0xe100, PreviewDX11Command = 0xe110,
                   PreviewDX12Command = 0xe112, PreviewBothCommand = 0xe113;
#ifdef MIKUDANCESTUDIO_CPU_RENDERING
void InstallScenePreviewMenu(MMDApp*);
bool HandleScenePreviewCommand(MMDApp*, unsigned command);
bool CpuScenePreviewRequested();
void ReleaseCpuSceneFrame();
#else
inline void InstallScenePreviewMenu(MMDApp*) {
}
inline bool HandleScenePreviewCommand(MMDApp*, unsigned) {
    return false;
}
inline bool CpuScenePreviewRequested() {
    return false;
}
inline void ReleaseCpuSceneFrame() {
}
#endif
}
