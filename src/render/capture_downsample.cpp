// ===========================================================================
// VA 0x00425970 - DownsampleCaptureSurface (original: sub_425970)
// ===========================================================================
// Reads the full-size capture render target back to system memory, performs
// the original two-pass rational BGRA box filter in place, then uploads the
// requested rectangle to the secondary-window back buffer.
// ===========================================================================
#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <Windows.h>
#include <d3d9.h>

#include <cstdint>

#include "mikudancestudio/mmd_app.hpp"
#include "mikudancestudio/mme_bridge.hpp"
#include "mikudancestudio/ported_funcs.hpp"

namespace mikudancestudio {
namespace {

std::int32_t GcdBySubtraction(std::int32_t a, std::int32_t b) {
    while (a != b) {
        if (a <= b)
            b -= a;
        else
            a -= b;
    }
    return a;
}

std::uint32_t PackAverage(std::int32_t b, std::int32_t g,
                          std::int32_t r, std::int32_t a,
                          std::int32_t divisor) {
    return static_cast<std::uint32_t>(b / divisor) |
           (static_cast<std::uint32_t>(g / divisor) << 8) |
           (static_cast<std::uint32_t>(r / divisor) << 16) |
           (static_cast<std::uint32_t>(a / divisor) << 24);
}

}  // namespace

ULONG DownsampleCaptureSurface(MMDApp* app) {
    D3DRenderer* sub = app->Renderer();
    auto* device = sub->device;
    const std::int32_t sourceWidth = sub->screenWidth;
    const std::int32_t sourceHeight = sub->screenHeight;
    const auto format = sub->backbufferFormat;

    IDirect3DSurface9* systemSurface = nullptr;
    device->CreateOffscreenPlainSurface(sourceWidth, sourceHeight, format,
                                        D3DPOOL_SYSTEMMEM, &systemSurface,
                                        nullptr);
    mme::PreRenderTargetCopy(app, device, sub->captureSurface);
    device->GetRenderTargetData(sub->captureSurface,
                                systemSurface);

    D3DLOCKED_RECT locked{};
    systemSurface->LockRect(&locked, nullptr, 0);

    const std::int32_t targetWidth =
        app->state.renderW;
    const std::int32_t targetHeight =
        app->state.renderH;
    const std::int32_t widthGcd =
        GcdBySubtraction(sourceWidth, targetWidth);
    const std::int32_t targetWidthRatio = targetWidth / widthGcd;
    const std::int32_t sourceWidthRatio = sourceWidth / widthGcd;
    const std::int32_t heightGcd =
        GcdBySubtraction(sourceHeight, targetHeight);
    const std::int32_t targetHeightRatio = targetHeight / heightGcd;
    const std::int32_t sourceHeightRatio = sourceHeight / heightGcd;

    auto* bits = static_cast<std::uint8_t*>(locked.pBits);
    std::int32_t phase = 0;
    for (std::int32_t y = 0; y < sourceHeight; ++y) {
        auto* source = bits + y * locked.Pitch;
        auto* destination = source;
        for (std::int32_t x = 0; x < targetWidth; ++x) {
            std::int32_t b = source[0];
            std::int32_t g = source[1];
            std::int32_t r = source[2];
            std::int32_t a = source[3];
            std::int32_t sumB = 0;
            std::int32_t sumG = 0;
            std::int32_t sumR = 0;
            std::int32_t sumA = 0;
            for (std::int32_t n = 0; n < sourceWidthRatio; ++n) {
                if (phase >= targetWidthRatio) {
                    b = source[4];
                    g = source[5];
                    r = source[6];
                    a = source[7];
                    source += 4;
                    phase = 0;
                }
                sumB += b;
                sumG += g;
                sumR += r;
                sumA += a;
                ++phase;
            }
            *reinterpret_cast<std::uint32_t*>(destination) =
                PackAverage(sumB, sumG, sumR, sumA, sourceWidthRatio);
            destination += 4;
        }
    }

    const std::int32_t pitchPixels = locked.Pitch / 4;
    for (std::int32_t x = 0; x < targetWidth; ++x) {
        auto* source = bits + x * 4;
        auto* destination = source;
        phase = 0;
        for (std::int32_t y = 0; y < targetHeight; ++y) {
            std::int32_t b = source[0];
            std::int32_t g = source[1];
            std::int32_t r = source[2];
            std::int32_t a = source[3];
            std::int32_t sumB = 0;
            std::int32_t sumG = 0;
            std::int32_t sumR = 0;
            std::int32_t sumA = 0;
            for (std::int32_t n = 0; n < sourceHeightRatio; ++n) {
                if (phase >= targetHeightRatio) {
                    source += locked.Pitch;
                    phase = 0;
                    b = source[0];
                    g = source[1];
                    r = source[2];
                    a = source[3];
                }
                sumB += b;
                sumG += g;
                sumR += r;
                sumA += a;
                ++phase;
            }
            *reinterpret_cast<std::uint32_t*>(destination) =
                PackAverage(sumB, sumG, sumR, sumA, sourceHeightRatio);
            destination += pitchPixels * 4;
        }
    }

    systemSurface->UnlockRect();
    RECT sourceRect{0, 0, targetWidth, targetHeight};
    POINT destinationPoint{0, 0};
    device->UpdateSurface(systemSurface, &sourceRect,
                          sub->backbufferSurface,
                          &destinationPoint);
    return systemSurface->Release();
}

}  // namespace mikudancestudio
