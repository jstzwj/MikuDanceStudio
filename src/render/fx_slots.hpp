// Typed D3DX effect calls shared by the host renderers.
// The COM declaration handles x86/x64 dispatch; original VA/slot evidence is
// retained in reports/audit10_mme_render.md and the owning call-site notes.
#pragma once

#include <cstring>
#include <d3dx9.h>
#include "mikudancestudio/d3dx_dyn.hpp"

namespace mikudancestudio::fx {

inline ID3DXEffect* Effect(void* effect) {
    return static_cast<ID3DXEffect*>(effect);
}
inline HRESULT SetTechnique(void* effect, const char* technique) {
    return Effect(effect)->SetTechnique(technique);
}
inline HRESULT SetBool(void* effect, const char* parameter, int value) {
    return Effect(effect)->SetBool(parameter, value);
}
inline HRESULT SetFloatArray(void* effect, const char* parameter,
                             const float* values, UINT count) {
    return Effect(effect)->SetFloatArray(parameter, values, count);
}
inline HRESULT SetMatrix(void* effect, const char* parameter,
                         const d3dx::D3DXMATRIXF* matrix) {
    D3DMATRIX value;
    static_assert(sizeof(value) == sizeof(*matrix), "D3DX matrix ABI");
    std::memcpy(&value, matrix, sizeof(value));
    return Effect(effect)->SetMatrix(parameter, &value);
}
inline HRESULT GetMatrix(void* effect, const char* parameter,
                         d3dx::D3DXMATRIXF* matrix) {
    D3DMATRIX value;
    std::memcpy(&value, matrix, sizeof(value));
    const HRESULT hr = Effect(effect)->GetMatrix(parameter, &value);
    std::memcpy(matrix, &value, sizeof(value));
    return hr;
}
inline HRESULT Begin(void* effect, UINT* passes) {
    return Effect(effect)->Begin(passes, 0);
}
inline HRESULT Begin(void* effect) {
    UINT passes = 0;
    return Begin(effect, &passes);
}
inline HRESULT BeginPass(void* effect, UINT pass = 0) {
    return Effect(effect)->BeginPass(pass);
}
inline HRESULT EndPass(void* effect) {
    return Effect(effect)->EndPass();
}
inline HRESULT End(void* effect) {
    return Effect(effect)->End();
}
inline HRESULT OnLostDevice(void* effect) {
    return Effect(effect)->OnLostDevice();
}
inline HRESULT OnResetDevice(void* effect) {
    return Effect(effect)->OnResetDevice();
}

} // namespace mikudancestudio::fx
