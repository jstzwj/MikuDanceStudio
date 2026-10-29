#pragma once
namespace mikudancestudio {
#ifdef MIKUDANCESTUDIO_CPU_RENDERING
// CPU geometry/material inputs for the live editor viewport. Diagnostic frame
// assembly is separate and can be enabled only in a diagnostic build.
bool CpuSceneInputsRequested();
void ReleaseCpuSceneFrame();
#else
inline bool CpuSceneInputsRequested() {
    return false;
}
inline void ReleaseCpuSceneFrame() {
}
#endif
}
