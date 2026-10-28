#pragma once
#include <d3d9.h>
#include "rhi_model_pipeline.hpp"
namespace mikudancestudio::rhi {
// DX9 boundary: materialize hidden AUTOGEN levels through explicit
// point-sampled LOD reads. No DX11/CPU mip-generation approximation. Restores
// render targets, viewport and state; sceneActive specifies the caller's state.
// Rejects unsupported formats/LOD/filter/caps and clears output on any failure.
// Diagnostics inspect an application texture; the optional preparation service
// uses only its own scratch texture populated from original CPU bytes.
HRESULT CaptureD3D9GeneratedMips(IDirect3DDevice9*, IDirect3DTexture9*, bool sceneActive,
                                 ModelTextureData&);
}
