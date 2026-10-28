#pragma once

#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <Windows.h>
#include <d3d9.h>

namespace mikudancestudio {

// DxOpenNI's decorated export names fix the calling convention and bool ABI.
// The joint export writes three contiguous floats (D3DXVECTOR3).
struct OpenNiApi {
    using Initialize = bool(WINAPI*)(HWND, bool, IDirect3DDevice9*, const wchar_t*, const char*);
    using Clean = void(WINAPI*)();
    using DrawDepthMap = void(WINAPI*)(bool);
    using DepthTexture = void(WINAPI*)(IDirect3DTexture9**);
    using JointPosition = void(WINAPI*)(int, float*);
    using IsTracking = void(WINAPI*)(bool*);
    using Version = void(WINAPI*)(float*);

    Initialize initialize = nullptr;
    Clean clean = nullptr;
    DrawDepthMap drawDepthMap = nullptr;
    DepthTexture depthTexture = nullptr;
    JointPosition jointPosition = nullptr;
    IsTracking isTracking = nullptr;
    Version version = nullptr;

    bool Complete() const {
        return initialize && clean && drawDepthMap && depthTexture && jointPosition && isTracking &&
               version;
    }
};

} // namespace mikudancestudio
