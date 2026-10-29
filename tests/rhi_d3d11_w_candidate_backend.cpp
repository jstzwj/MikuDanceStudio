// A separate copy of the adapter exists only inside the diagnostic executable.
// No application can create this backend through the production factory.
#define CreateD3D11Device CreateD3D11WCandidateInternal
#define CreateD3D11OffscreenDevice CreateD3D11WCandidateOffscreenDevice
#define ReadD3D11Swapchain ReadD3D11WCandidateSwapchain
#define ResizeD3D11Offscreen ResizeD3D11WCandidateOffscreen
#define ReadD3D11Offscreen ReadD3D11WCandidateOffscreen
#define CreateD3D11ModelTexture CreateD3D11WCandidateTexture
#define ReplayD3D11ModelPrefix ReplayD3D11WCandidatePrefix
#define AcquireD3D11Backbuffer AcquireD3D11WCandidateBackbuffer
#include "mikudancestudio/rhi_d3d11.hpp"
#include "rhi_model_shaders.hpp"
#include "d3d11_w_candidate_shader.hpp"
#include "d3d11_lighting_candidate_shader.hpp"
#include "d3d11_normal_candidate_shader.hpp"
namespace {const std::string diagnosticModelSource=normal_shader_diagnostic::Source(lighting_shader_diagnostic::Source(w_shader_diagnostic::Source(modelVertexSource).c_str(),true).c_str());}
#define modelVertexSource diagnosticModelSource.c_str()
#define MDS_RHI_TEST_W_CANDIDATE 1
#include "../src/render/rhi_d3d11.cpp"

namespace mikudancestudio::rhi {
D3D11Bundle CreateD3D11WCandidateDevice(const SwapchainDesc& desc,D3D11Driver driver,unsigned policy) {
    if(policy<1 || policy>5)return {};
    auto bundle=CreateD3D11WCandidateInternal(desc,driver);
    if(!bundle)return bundle;
    auto& device=static_cast<D3D11Device&>(*bundle.device);
    D3D11_FEATURE_DATA_DOUBLES support{};
    if(FAILED(device.native->CheckFeatureSupport(D3D11_FEATURE_DOUBLES,&support,sizeof(support))) || !support.DoublePrecisionFloatShaderOps)return {};
    device.wCandidatePolicy=policy;return bundle;
}
D3D11Bundle CreateD3D11LightingCandidateDevice(const SwapchainDesc& desc,D3D11Driver driver) {
    auto bundle=CreateD3D11WCandidateDevice(desc,driver,3);
    if(bundle)static_cast<D3D11Device&>(*bundle.device).lightingCandidatePolicy=1;
    return bundle;
}
D3D11Bundle CreateD3D11NormalCandidateDevice(const SwapchainDesc& desc,D3D11Driver driver) {
    auto bundle=CreateD3D11LightingCandidateDevice(desc,driver);
    if(bundle)static_cast<D3D11Device&>(*bundle.device).normalCandidatePolicy=1;
    return bundle;
}
D3D11Bundle CreateD3D11ViewportCenterCandidateDevice(const SwapchainDesc& desc,D3D11Driver driver) {
    auto bundle=CreateD3D11NormalCandidateDevice(desc,driver);
    if(bundle)static_cast<D3D11Device&>(*bundle.device).viewportCenterCandidate=true;
    return bundle;
}
D3D11Bundle CreateD3D11FusedXYCandidateDevice(const SwapchainDesc& desc,D3D11Driver driver) {
    auto bundle=CreateD3D11ViewportCenterCandidateDevice(desc,driver);
    if(bundle)static_cast<D3D11Device&>(*bundle.device).fusedXYCandidate=true;
    return bundle;
}
}
