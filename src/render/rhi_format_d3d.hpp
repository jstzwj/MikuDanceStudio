#pragma once
#include "mikudancestudio/rhi.hpp"
#include <dxgiformat.h>
namespace mikudancestudio::rhi::detail {
inline DXGI_FORMAT DxgiFormat(TextureFormat f) {
    switch (f) {
    case TextureFormat::Rgba8:
        return DXGI_FORMAT_R8G8B8A8_UNORM;
    case TextureFormat::R32Float:
        return DXGI_FORMAT_R32_FLOAT;
    case TextureFormat::Rgba16Float:
        return DXGI_FORMAT_R16G16B16A16_FLOAT;
    case TextureFormat::Depth24Stencil8:
        return DXGI_FORMAT_D24_UNORM_S8_UINT;
    }
    return DXGI_FORMAT_UNKNOWN;
}
}
