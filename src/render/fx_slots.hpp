// Typed D3DX effect calls shared by the host renderers.
// The shared COM declaration handles x86/x64 dispatch. Call-site comments
// retain original addresses where ordering or behavior needs explanation.
#pragma once

#include <cstring>
#include <d3dx9.h>
#include "mikudancestudio/d3dx_api.hpp"

namespace mikudancestudio::fx {

inline HRESULT SetTechnique(ID3DXEffect* effect, D3DXHANDLE technique) {
    return effect->SetTechnique(technique);
}
inline HRESULT SetBool(ID3DXEffect* effect, D3DXHANDLE parameter, int value) {
    return effect->SetBool(parameter, value);
}
inline HRESULT SetFloatArray(ID3DXEffect* effect, D3DXHANDLE parameter, const float* values,
                             UINT count) {
    return effect->SetFloatArray(parameter, values, count);
}
inline HRESULT SetMatrix(ID3DXEffect* effect, D3DXHANDLE parameter,
                         const d3dx::D3DXMATRIXF* matrix) {
    D3DMATRIX value;
    static_assert(sizeof(value) == sizeof(*matrix), "D3DX matrix ABI");
    std::memcpy(&value, matrix, sizeof(value));
    return effect->SetMatrix(parameter, &value);
}
inline HRESULT GetMatrix(ID3DXEffect* effect, D3DXHANDLE parameter, d3dx::D3DXMATRIXF* matrix) {
    D3DMATRIX value;
    std::memcpy(&value, matrix, sizeof(value));
    const HRESULT hr = effect->GetMatrix(parameter, &value);
    std::memcpy(matrix, &value, sizeof(value));
    return hr;
}
inline HRESULT Begin(ID3DXEffect* effect, UINT* passes) {
    return effect->Begin(passes, 0);
}
inline HRESULT Begin(ID3DXEffect* effect) {
    UINT passes = 0;
    return Begin(effect, &passes);
}
inline HRESULT BeginPass(ID3DXEffect* effect, UINT pass = 0) {
    return effect->BeginPass(pass);
}
inline HRESULT EndPass(ID3DXEffect* effect) {
    return effect->EndPass();
}
inline HRESULT End(ID3DXEffect* effect) {
    return effect->End();
}
inline HRESULT OnLostDevice(ID3DXEffect* effect) {
    return effect->OnLostDevice();
}
inline HRESULT OnResetDevice(ID3DXEffect* effect) {
    return effect->OnResetDevice();
}

} // namespace mikudancestudio::fx
