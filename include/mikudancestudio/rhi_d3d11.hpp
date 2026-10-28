#pragma once
#include "mikudancestudio/rhi_swapchain.hpp"
#include "mikudancestudio/rhi_image.hpp"
#include <string>
#include "rhi_model_pipeline.hpp"
#include "rhi_model_replay.hpp"
struct ID3D11Texture2D;
namespace mikudancestudio::rhi {
enum class D3D11Driver {
    Hardware,
    Warp
};
enum class D3D11CreationStage {
    Parameters,
    NativeDevice,
    Shaders,
    Targets,
    Ready,
    Compatibility
};
// Measured DX9 arithmetic compatibility, opt-in and single-sample only. This
// describes implemented math; equivalence still requires a per-GPU image gate.
using D3D11ModelCompatibility = ModelCompatibility;
using D3D11CompatibilityResult = ModelCompatibilityResult;
constexpr D3D11CompatibilityResult CheckD3D11ModelCompatibility(D3D11ModelCompatibility mode,
                                                                bool shaderDoubles) {
    return CheckModelCompatibility(mode, shaderDoubles);
}
using D3D11ModelOptions = ModelOptions;
using D3D11ModelSupport = ModelSupport;
struct D3D11ReplayTiming {
    bool valid = false, disjoint = false;
    std::uint64_t frequency = 0;
    double gpuSeconds = 0;
};
// Destruction order: swapchain before device. Cache owners must die first.
struct D3D11Bundle {
    std::unique_ptr<Device> device;
    std::unique_ptr<Swapchain> swapchain;
    std::int64_t creationError = 0;
    D3D11CreationStage stage = D3D11CreationStage::Parameters;
    std::string adapterName;
    std::uint32_t vendorId = 0, deviceId = 0;
    D3D11ModelSupport modelSupport;
    std::string creationReason;
    explicit operator bool() const { return bool(device); }
};
#ifdef MIKUDANCESTUDIO_RHI_D3D11
D3D11Bundle CreateD3D11Device(const SwapchainDesc&, D3D11Driver, D3D11ModelOptions = {});
// No HWND, swapchain, presentation or desktop interaction. swapchain is null.
D3D11Bundle CreateD3D11OffscreenDevice(std::uint32_t width, std::uint32_t height, D3D11Driver,
                                       D3D11ModelOptions = {});
D3D11ModelSupport QueryD3D11ModelSupport(Device&, D3D11ModelCompatibility);
// This backend clears texture bindings on every fixed/model pipeline change.
// Bind all active material stages after SetModelPipeline (or slot 0 after HUD).
// Diagnostic readback, after EndFrame and before Present; single-sample RGBA8 only.
bool ReadD3D11Swapchain(Device&, RgbaImage&);
// Single-sample RGBA8/D24S8 target. Resize advances epoch, retains uploads.
bool ResizeD3D11Offscreen(Device&, std::uint32_t width, std::uint32_t height);
bool ReadD3D11Offscreen(Device&, RgbaImage&);
Handle CreateD3D11ModelTexture(Device&, const ModelTextureData&);
// Rebuilds the single-sample target, uploads private immutable versions, and
// replays from clear through drawCount (zero means clear only).
// Full validation precedes target changes, including for the clear-only boundary.
// Optional timestamps cover clear/draw GPU work, excluding uploads, target
// rebuild and readback. Invalid/disjoint measurements must not be reported.
ModelReplayResult ReplayD3D11ModelPrefix(Device&, const ModelReplayStream&, std::size_t drawCount,
                                         RgbaImage&, D3D11ReplayTiming* timing = nullptr);
// Diagnostic reference: caller must Release before ResizeBuffers can succeed.
bool AcquireD3D11Backbuffer(Device&, ID3D11Texture2D**);
#endif
}
