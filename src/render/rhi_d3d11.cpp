#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <Windows.h>
#include <d3d11.h>
#include <dxgi.h>
#include <d3dcompiler.h>
#include <wrl/client.h>
#include "mikudancestudio/rhi_d3d11.hpp"
#include "mikudancestudio/rhi_render_pass.hpp"
#include <algorithm>
#include "mikudancestudio/rhi_model_shader_constants.hpp"
#include "rhi_model_shaders.hpp"
#include "rhi_model_constants.hpp"
#include <cmath>
#include <cstring>
#include <limits>
#include <map>
using Microsoft::WRL::ComPtr;
namespace mikudancestudio::rhi {
namespace {
DXGI_FORMAT Depth(SurfaceDepth d) {
    return d == SurfaceDepth::Depth16 ? DXGI_FORMAT_D16_UNORM : DXGI_FORMAT_D24_UNORM_S8_UINT;
}
struct Resource {
    std::uint32_t generation = 1;
    bool alive = false, uploaded = false;
    std::vector<std::uint8_t> indices;
    bool srgb = false;
    BufferDesc description{};
    TextureDesc textureDescription{};
    ComPtr<ID3D11Buffer> buffer;
    ComPtr<ID3D11Texture2D> texture;
    ComPtr<ID3D11ShaderResourceView> view;
    ComPtr<ID3D11RenderTargetView> renderView;
    ComPtr<ID3D11DepthStencilView> depthView;
};
class D3D11Device final : public Device {
public:
    ComPtr<ID3D11Device> native;
    ComPtr<ID3D11DeviceContext> context;
    ComPtr<IDXGISwapChain> chain;
    ComPtr<ID3D11RenderTargetView> target;
    ComPtr<ID3D11Texture2D> depth;
    ComPtr<ID3D11DepthStencilView> depthView;
    SwapchainDesc desc{};
    SwapchainDesc presentation{};
    D3D11ModelSupport modelSupport;
    ComPtr<ID3D11Buffer> dx9Constants;
    ComPtr<ID3D11Texture2D> offscreen;
    bool modelReady = false, retired = false;
    std::array<Handle, 3> modelTextures{};
    ModelPipeline modelPipeline;
    Handle shadowTexture;
    ComPtr<ID3D11VertexShader> shadowVs, shadowOutlineVs;
    ComPtr<ID3D11PixelShader> shadowPs;
    ComPtr<ID3D11InputLayout> shadowLayout, shadowOutlineLayout;
    ComPtr<ID3D11Buffer> shadowConstants;
    ComPtr<ID3D11SamplerState> shadowSampler;
    bool SetBuiltinShadow(const ModelPipeline& p, const ModelViewport& viewport) {
        shadowTexture = {};
        ID3D11ShaderResourceView* empty = nullptr;
        context->PSSetShaderResources(3, 1, &empty);
        if (p.shadow.mode == BuiltinShadowMode::None)
            return true;
        if (!passes.Active() || !ValidBuiltinShadowState(p.shadow) ||
            p.vertexLayout != BuiltinShadowVertexLayout(p.shadow.mode) ||
            p.topology != PrimitiveTopology::TriangleList)
            return false;
        if (!shadowVs) {
            ComPtr<ID3DBlob> vertex, outline, pixel, errors;
            lastError = D3DCompile(builtinShadowSource, std::strlen(builtinShadowSource), nullptr,
                                   nullptr, nullptr, "shadow_vs", "vs_5_0",
                                   D3DCOMPILE_IEEE_STRICTNESS, 0, &vertex, &errors);
            if (SUCCEEDED(lastError))
                lastError = D3DCompile(builtinShadowSource, std::strlen(builtinShadowSource),
                                       nullptr, nullptr, nullptr, "shadow_outline_vs", "vs_5_0",
                                       D3DCOMPILE_IEEE_STRICTNESS, 0, &outline, &errors);
            if (SUCCEEDED(lastError))
                lastError = D3DCompile(builtinShadowSource, std::strlen(builtinShadowSource),
                                       nullptr, nullptr, nullptr, "shadow_ps", "ps_5_0",
                                       D3DCOMPILE_IEEE_STRICTNESS, 0, &pixel, &errors);
            if (FAILED(lastError))
                return false;
            ComPtr<ID3D11VertexShader> vs;
            ComPtr<ID3D11PixelShader> ps;
            ComPtr<ID3D11InputLayout> layout;
            const D3D11_INPUT_ELEMENT_DESC elements[] = {
                {"POSITION", 0, DXGI_FORMAT_R32G32B32_FLOAT, 0, 0, D3D11_INPUT_PER_VERTEX_DATA, 0},
                {"NORMAL", 0, DXGI_FORMAT_R32G32B32_FLOAT, 0, 12, D3D11_INPUT_PER_VERTEX_DATA, 0},
                {"TEXCOORD", 0, DXGI_FORMAT_R32G32_FLOAT, 0, 24, D3D11_INPUT_PER_VERTEX_DATA, 0}};
            lastError = native->CreateVertexShader(vertex->GetBufferPointer(),
                                                   vertex->GetBufferSize(), nullptr, &vs);
            if (SUCCEEDED(lastError))
                lastError = native->CreatePixelShader(pixel->GetBufferPointer(),
                                                      pixel->GetBufferSize(), nullptr, &ps);
            if (SUCCEEDED(lastError))
                lastError = native->CreateInputLayout(elements, 3, vertex->GetBufferPointer(),
                                                      vertex->GetBufferSize(), &layout);
            if (SUCCEEDED(lastError))
                lastError =
                    native->CreateVertexShader(outline->GetBufferPointer(),
                                               outline->GetBufferSize(), nullptr, &shadowOutlineVs);
            if (SUCCEEDED(lastError))
                lastError =
                    native->CreateInputLayout(elements, 1, outline->GetBufferPointer(),
                                              outline->GetBufferSize(), &shadowOutlineLayout);
            D3D11_BUFFER_DESC b{};
            b.ByteWidth = sizeof(BuiltinShadowConstants);
            b.BindFlags = D3D11_BIND_CONSTANT_BUFFER;
            if (SUCCEEDED(lastError))
                lastError = native->CreateBuffer(&b, nullptr, &shadowConstants);
            D3D11_SAMPLER_DESC s{};
            s.Filter = D3D11_FILTER_MIN_MAG_LINEAR_MIP_POINT;
            s.AddressU = s.AddressV = s.AddressW = D3D11_TEXTURE_ADDRESS_CLAMP;
            s.MaxLOD = 0;
            if (SUCCEEDED(lastError))
                lastError = native->CreateSamplerState(&s, &shadowSampler);
            if (FAILED(lastError))
                return false;
            shadowVs = vs;
            shadowPs = ps;
            shadowLayout = layout;
        }
        const auto c = detail::BuildBuiltinShadowConstants(p, viewport);
        context->UpdateSubresource(shadowConstants.Get(), 0, nullptr, &c, 0, 0);
        auto* cb = shadowConstants.Get();
        context->VSSetConstantBuffers(2, 1, &cb);
        context->PSSetConstantBuffers(2, 1, &cb);
        auto* sampler = shadowSampler.Get();
        context->PSSetSamplers(3, 1, &sampler);
        const bool outline = p.shadow.mode == BuiltinShadowMode::Outline;
        context->IASetInputLayout(outline ? shadowOutlineLayout.Get() : shadowLayout.Get());
        context->VSSetShader(outline ? shadowOutlineVs.Get() : shadowVs.Get(), nullptr, 0);
        context->PSSetShader(shadowPs.Get(), nullptr, 0);
        return true;
    }
    bool BindShadowTexture(Handle h) override {
        auto* r = Find(h);
        const auto* desc = passes.Description(h);
        if (!Live() || !inFrame || !modelReady ||
            modelPipeline.shadow.mode != BuiltinShadowMode::Material || !passes.CanSample(h) ||
            !desc || desc->format != TextureFormat::R32Float || !r || !r->view)
            return false;
        auto* view = r->view.Get();
        context->PSSetShaderResources(3, 1, &view);
        shadowTexture = h;
        return true;
    }
    ComPtr<ID3D11VertexShader> modelVs;
    ComPtr<ID3D11PixelShader> modelPs;
    ComPtr<ID3D11InputLayout> modelLayout;
    ComPtr<ID3D11VertexShader> modelColorVs;
    ComPtr<ID3D11InputLayout> modelColorLayout;
    ComPtr<ID3D11Buffer> modelVertexConstants, modelPixelConstants;
#ifdef MDS_RHI_TEST_W_CANDIDATE
    // Compiled only by the isolated test backend translation unit.
    ComPtr<ID3D11Buffer> wCandidateConstants;
    unsigned wCandidatePolicy = 1, lightingCandidatePolicy = 0, normalCandidatePolicy = 0;
    bool viewportCenterCandidate = false;
    bool fusedXYCandidate = false;
#endif
    std::array<ComPtr<ID3D11SamplerState>, 3> modelSamplers;
    std::map<std::string, ComPtr<ID3D11SamplerState>> samplerCache;
    std::map<std::string, ComPtr<ID3D11BlendState>> blendCache;
    std::map<std::string, ComPtr<ID3D11DepthStencilState>> depthCache;
    std::map<std::string, ComPtr<ID3D11RasterizerState>> rasterCache;
    HRESULT lastError = S_OK;
    bool inFrame = false, pending = false, presentable = false, pipelineReady = false;
    std::uint64_t identity = detail::NextDeviceIdentity(), epoch = 1;
    std::vector<Resource> resources;
    RenderPassState passes;
    unsigned RenderWidth() const {
        const auto& p = passes.Current();
        const auto* d = passes.Description(p.color          ? p.color->texture
                                           : p.depthStencil ? p.depthStencil->texture
                                                            : Handle{});
        return d ? d->width : desc.width;
    }
    unsigned RenderHeight() const {
        const auto& p = passes.Current();
        const auto* d = passes.Description(p.color          ? p.color->texture
                                           : p.depthStencil ? p.depthStencil->texture
                                                            : Handle{});
        return d ? d->height : desc.height;
    }
    ID3D11DepthStencilView* ActiveDepth() {
        if (!passes.Active())
            return depthView.Get();
        const auto& p = passes.Current();
        return p.depthStencil ? Find(p.depthStencil->texture)->depthView.Get() : nullptr;
    }
    Handle CreateRenderTexture(const RenderTextureDesc& d) override {
        if (!Live() || inFrame || !ValidRenderTextureDesc(d))
            return {};
        D3D11_TEXTURE2D_DESC t{};
        t.Width = d.width;
        t.Height = d.height;
        t.ArraySize = t.MipLevels = t.SampleDesc.Count = 1;
        t.Format = d.format == TextureFormat::Rgba8      ? DXGI_FORMAT_R8G8B8A8_UNORM
                   : d.format == TextureFormat::R32Float ? DXGI_FORMAT_R32_FLOAT
                                                         : DXGI_FORMAT_D24_UNORM_S8_UINT;
        t.BindFlags = d.format == TextureFormat::Depth24Stencil8 ? D3D11_BIND_DEPTH_STENCIL
                                                                 : D3D11_BIND_RENDER_TARGET;
        if (d.access & std::uint32_t(TextureAccess::Sampled))
            t.BindFlags |= D3D11_BIND_SHADER_RESOURCE;
        Resource r;
        r.alive = r.uploaded = true;
        lastError = native->CreateTexture2D(&t, nullptr, &r.texture);
        if (SUCCEEDED(lastError))
            lastError =
                d.format == TextureFormat::Depth24Stencil8
                    ? native->CreateDepthStencilView(r.texture.Get(), nullptr, &r.depthView)
                    : native->CreateRenderTargetView(r.texture.Get(), nullptr, &r.renderView);
        if (SUCCEEDED(lastError) && (d.access & std::uint32_t(TextureAccess::Sampled)))
            lastError = native->CreateShaderResourceView(r.texture.Get(), nullptr, &r.view);
        if (FAILED(lastError))
            return {};
        const auto h = Insert(std::move(r));
        return passes.Register(h, d) ? h : Handle{};
    }
    bool BeginRenderPass(const RenderPassDesc& p) override {
        if (!Live() || !inFrame || !passes.Begin(p))
            return false;
        ID3D11ShaderResourceView* empty[4]{};
        context->PSSetShaderResources(0, 4, empty);
        ID3D11RenderTargetView* rt = p.color ? Find(p.color->texture)->renderView.Get() : nullptr;
        auto* ds = ActiveDepth();
        context->OMSetRenderTargets(rt ? 1 : 0, rt ? &rt : nullptr, ds);
        if (p.color && p.color->load == AttachmentLoad::Clear) {
            const auto& c = p.color->clear;
            const float clear[] = {c.red, c.green, c.blue, c.alpha};
            context->ClearRenderTargetView(rt, clear);
        }
        if (p.depthStencil && p.depthStencil->load == AttachmentLoad::Clear)
            context->ClearDepthStencilView(ds, D3D11_CLEAR_DEPTH | D3D11_CLEAR_STENCIL,
                                           p.depthStencil->clearDepth,
                                           p.depthStencil->clearStencil);
        const D3D11_VIEWPORT v{0, 0, float(RenderWidth()), float(RenderHeight()), 0, 1};
        context->RSSetViewports(1, &v);
        modelReady = pipelineReady = false;
        modelTextures = {};
        boundTexture = {};
        return true;
    }
    bool EndRenderPass() override {
        if (!inFrame || !passes.Active())
            return false;
        context->OMSetRenderTargets(0, nullptr, nullptr);
        ID3D11ShaderResourceView* empty[4]{};
        context->PSSetShaderResources(0, 4, empty);
        modelReady = pipelineReady = false;
        modelTextures = {};
        boundTexture = {};
        return passes.End();
    }
    bool ReadRenderTexture(Handle h, AttachmentReadback& out) override {
        if (!Live() || inFrame || !passes.CanRead(h))
            return false;
        auto* r = Find(h);
        auto d = *passes.Description(h);
        D3D11_TEXTURE2D_DESC t{};
        r->texture->GetDesc(&t);
        t.BindFlags = t.MiscFlags = 0;
        t.Usage = D3D11_USAGE_STAGING;
        t.CPUAccessFlags = D3D11_CPU_ACCESS_READ;
        ComPtr<ID3D11Texture2D> cpu;
        lastError = native->CreateTexture2D(&t, nullptr, &cpu);
        if (FAILED(lastError))
            return false;
        context->CopyResource(cpu.Get(), r->texture.Get());
        D3D11_MAPPED_SUBRESOURCE mapped{};
        lastError = context->Map(cpu.Get(), 0, D3D11_MAP_READ, 0, &mapped);
        if (FAILED(lastError))
            return false;
        AttachmentReadback data{d, std::vector<std::uint8_t>(std::size_t(d.width) * d.height * 4)};
        for (unsigned y = 0; y < d.height; ++y)
            std::memcpy(data.bytes.data() + std::size_t(y) * d.width * 4,
                        static_cast<const std::uint8_t*>(mapped.pData) +
                            std::size_t(y) * mapped.RowPitch,
                        std::size_t(d.width) * 4);
        context->Unmap(cpu.Get(), 0);
        out = std::move(data);
        return true;
    }
    std::map<std::uint64_t, ComPtr<ID3D11Query>> completions;
    std::uint64_t nextCompletion = 0, completed = 0;
    Handle boundTexture{};
    ComPtr<ID3D11VertexShader> vs;
    ComPtr<ID3D11PixelShader> ps;
    ComPtr<ID3D11InputLayout> layout;
    ComPtr<ID3D11Buffer> constants;
    ComPtr<ID3D11SamplerState> sampler;
    ComPtr<ID3D11BlendState> blend;
    ComPtr<ID3D11DepthStencilState> depthState;
    ComPtr<ID3D11RasterizerState> raster;
    ~D3D11Device() override {
        if (context) {
            context->ClearState();
            context->Flush();
        }
    }
    Handle CreateModelTexture(const ModelTextureData& data) override {
        return CreateD3D11ModelTexture(*this, data);
    }
    bool EnsureModelTarget(const HistoryDesc& t) override {
        if (!ValidHistoryDesc(t) || t.width > 4096 || t.height > 4096 || t.samples != 1 ||
            t.representation != HistoryRepresentation::SingleSample || !t.color || !t.depth ||
            !t.stencil || inFrame || !Live())
            return false;
        if (offscreen && desc.width == t.width && desc.height == t.height)
            return true;
        return ResizeD3D11Offscreen(*this, t.width, t.height);
    }
    bool ReadModelTarget(RgbaImage& image) override { return ReadD3D11Offscreen(*this, image); }
    CompletionToken SignalCompletion() override {
        if (inFrame || !Live())
            return {};
        D3D11_QUERY_DESC desc{D3D11_QUERY_EVENT, 0};
        ComPtr<ID3D11Query> q;
        lastError = native->CreateQuery(&desc, &q);
        if (FAILED(lastError))
            return {};
        context->End(q.Get());
        context->Flush();
        completions.emplace(++nextCompletion, q);
        return {identity, nextCompletion};
    }
    CompletionState PollCompletion(CompletionToken token) override {
        if (token.device != identity || !token.value || token.value > nextCompletion)
            return CompletionState::Invalid;
        if (native->GetDeviceRemovedReason() != S_OK)
            return CompletionState::DeviceLost;
        if (token.value <= completed)
            return CompletionState::Complete;
        auto at = completions.find(token.value);
        if (at == completions.end())
            return CompletionState::Invalid;
        BOOL done = FALSE;
        const auto result =
            context->GetData(at->second.Get(), &done, sizeof(done), D3D11_ASYNC_GETDATA_DONOTFLUSH);
        if (FAILED(result))
            return CompletionState::DeviceLost;
        if (result == S_FALSE || !done)
            return CompletionState::Pending;
        completed = token.value;
        completions.erase(completions.begin(), completions.upper_bound(completed));
        return CompletionState::Complete;
    }
    bool WaitCompletion(CompletionToken token, std::uint32_t timeout) override {
        const auto deadline = GetTickCount64() + timeout;
        do {
            const auto state = PollCompletion(token);
            if (state != CompletionState::Pending)
                return state == CompletionState::Complete;
            Sleep(1);
        } while (GetTickCount64() < deadline);
        return false;
    }
    Backend BackendType() const override { return Backend::D3D11; }
    Capabilities Features() const override { return {true, false, true, true}; }
    DeviceStatus Status() const override {
        return {identity, epoch,
                native->GetDeviceRemovedReason() != S_OK ? DeviceState::Removed
                : pending || retired                     ? DeviceState::ResetPending
                                                         : DeviceState::Ready};
    }
    ResourceLifecycleCapabilities ResourceLifecycleFeatures() const override {
        return {true, true, true, false, false};
    }
    ModelCapabilities ModelFeatures() const override {
        return {true, true, true, true, true, true, true, true, true, true, bool(chain), UINT_MAX};
    }
    bool Live() const { return !retired && !pending && native->GetDeviceRemovedReason() == S_OK; }
    Resource* Find(Handle h) {
        if (h.device != identity || h.index >= resources.size())
            return nullptr;
        auto& r = resources[h.index];
        return r.alive && h.generation == r.generation ? &r : nullptr;
    }
    Handle Insert(Resource r) {
        for (std::uint32_t i = 0; i < resources.size(); ++i)
            if (!resources[i].alive) {
                r.generation = resources[i].generation;
                resources[i] = std::move(r);
                return {i, resources[i].generation, identity};
            }
        resources.push_back(std::move(r));
        return {static_cast<std::uint32_t>(resources.size() - 1), 1, identity};
    }
    Handle CreateBuffer(BufferDesc d, const void* data) override {
        if (!Live() || (d.usage != BufferUsage::Vertex && d.usage != BufferUsage::Index) ||
            !d.size || d.size > std::numeric_limits<UINT>::max())
            return {};
        if (d.usage == BufferUsage::Index &&
            (d.indexFormat != IndexFormat::UInt16 && d.indexFormat != IndexFormat::UInt32))
            return {};
        if (d.usage == BufferUsage::Index &&
            d.size % (d.indexFormat == IndexFormat::UInt16 ? 2 : 4))
            return {};
        D3D11_BUFFER_DESC b{};
        b.ByteWidth = static_cast<UINT>(d.size);
        b.BindFlags =
            d.usage == BufferUsage::Index ? D3D11_BIND_INDEX_BUFFER : D3D11_BIND_VERTEX_BUFFER;
        b.Usage = d.dynamic ? D3D11_USAGE_DYNAMIC : D3D11_USAGE_DEFAULT;
        b.CPUAccessFlags = d.dynamic ? D3D11_CPU_ACCESS_WRITE : 0;
        D3D11_SUBRESOURCE_DATA initial{};
        initial.pSysMem = data;
        Resource r;
        r.description = d;
        r.alive = true;
        if (d.usage == BufferUsage::Index) {
            r.indices.resize(d.size);
            if (data)
                std::memcpy(r.indices.data(), data, d.size);
            else
                return {};
        }
        lastError = native->CreateBuffer(&b, data ? &initial : nullptr, &r.buffer);
        return SUCCEEDED(lastError) ? Insert(std::move(r)) : Handle{};
    }
    Handle CreateTexture(TextureDesc d) override {
        if (!Live() || !d.width || !d.height || d.width > 16384 || d.height > 16384 ||
            d.format != TextureFormat::Rgba8 || d.mips != TextureDesc::Mips::SingleLevel)
            return {};
        D3D11_TEXTURE2D_DESC t{};
        t.Width = d.width;
        t.Height = d.height;
        t.ArraySize = t.MipLevels = 1;
        t.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
        t.SampleDesc.Count = 1;
        t.Usage = D3D11_USAGE_DEFAULT;
        t.BindFlags = D3D11_BIND_SHADER_RESOURCE;
        Resource r;
        r.alive = true;
        r.textureDescription = d;
        lastError = native->CreateTexture2D(&t, nullptr, &r.texture);
        if (SUCCEEDED(lastError))
            lastError = native->CreateShaderResourceView(r.texture.Get(), nullptr, &r.view);
        return SUCCEEDED(lastError) ? Insert(std::move(r)) : Handle{};
    }
    bool UploadTexture(Handle h, TexturePixels pixels) override {
        auto* r = Find(h);
        if (!Live() || inFrame || !r || !r->texture || passes.Description(h) || !pixels.data)
            return false;
        const auto width = static_cast<std::size_t>(r->textureDescription.width) * 4;
        const auto height = r->textureDescription.height;
        if (pixels.rowPitch < width || pixels.rowPitch > std::numeric_limits<UINT>::max() ||
            pixels.rowPitch > std::numeric_limits<std::size_t>::max() / height ||
            pixels.bytes < (height - 1) * pixels.rowPitch + width)
            return false;
        context->UpdateSubresource(r->texture.Get(), 0, nullptr, pixels.data,
                                   static_cast<UINT>(pixels.rowPitch), 0);
        r->uploaded = true;
        return native->GetDeviceRemovedReason() == S_OK;
    }
    bool BindTexture(std::uint32_t slot, Handle h) override {
        auto* r = Find(h);
        if (!Live() || !inFrame || slot >= (modelReady ? 3u : 1u) || !r || !r->view ||
            (passes.Description(h) ? !passes.CanSample(h) : !r->uploaded))
            return false;
        auto* view = r->view.Get();
        context->PSSetShaderResources(slot, 1, &view);
        if (modelReady)
            modelTextures[slot] = h;
        else
            boundTexture = h;
        return true;
    }
    bool Upload(Handle h, std::size_t offset, const void* data, std::size_t size) override {
        auto* r = Find(h);
        if (!Live() || inFrame || !r || !r->buffer || (!data && size) ||
            offset > r->description.size || size > r->description.size - offset)
            return false;
        if (!size)
            return true;
        if (r->description.dynamic) {
            // DISCARD is a whole-buffer replacement; partial writes unsupported.
            if (offset || size != r->description.size)
                return false;
            D3D11_MAPPED_SUBRESOURCE mapped{};
            lastError = context->Map(r->buffer.Get(), 0, D3D11_MAP_WRITE_DISCARD, 0, &mapped);
            if (FAILED(lastError))
                return false;
            std::memcpy(mapped.pData, data, size);
            context->Unmap(r->buffer.Get(), 0);
        } else {
            D3D11_BOX box{static_cast<UINT>(offset), 0, 0, static_cast<UINT>(offset + size), 1, 1};
            context->UpdateSubresource(r->buffer.Get(), 0, &box, data, 0, 0);
        }
        if (!r->indices.empty())
            std::memcpy(r->indices.data() + offset, data, size);
        return native->GetDeviceRemovedReason() == S_OK;
    }
    bool Destroy(Handle h) override {
        auto* r = Find(h);
        if (inFrame || !r || (pending && native->GetDeviceRemovedReason() == S_OK))
            return false;
        if (passes.Description(h))
            passes.Erase(h);
        r->renderView.Reset();
        r->depthView.Reset();
        r->buffer.Reset();
        r->texture.Reset();
        r->view.Reset();
        r->alive = false;
        if (++r->generation == 0)
            ++r->generation;
        return true;
    }
    bool InitializeShaders() {
        const char* source = fixedScreenSource;
        ComPtr<ID3DBlob> vertex, pixel, error;
        lastError = D3DCompile(source, std::strlen(source), nullptr, nullptr, nullptr, "vs",
                               "vs_4_0", 0, 0, &vertex, &error);
        if (SUCCEEDED(lastError))
            lastError = D3DCompile(source, std::strlen(source), nullptr, nullptr, nullptr, "ps",
                                   "ps_4_0", 0, 0, &pixel, &error);
        if (FAILED(lastError))
            return false;
        lastError = native->CreateVertexShader(vertex->GetBufferPointer(), vertex->GetBufferSize(),
                                               nullptr, &vs);
        if (SUCCEEDED(lastError))
            lastError = native->CreatePixelShader(pixel->GetBufferPointer(), pixel->GetBufferSize(),
                                                  nullptr, &ps);
        const D3D11_INPUT_ELEMENT_DESC elements[] = {
            {"POSITION", 0, DXGI_FORMAT_R32G32B32A32_FLOAT, 0, 0, D3D11_INPUT_PER_VERTEX_DATA, 0},
            {"COLOR", 0, DXGI_FORMAT_R8G8B8A8_UNORM, 0, 16, D3D11_INPUT_PER_VERTEX_DATA, 0},
            {"TEXCOORD", 0, DXGI_FORMAT_R32G32_FLOAT, 0, 20, D3D11_INPUT_PER_VERTEX_DATA, 0}};
        if (SUCCEEDED(lastError))
            lastError = native->CreateInputLayout(elements, 3, vertex->GetBufferPointer(),
                                                  vertex->GetBufferSize(), &layout);
        D3D11_BUFFER_DESC b{};
        b.ByteWidth = 16;
        b.Usage = D3D11_USAGE_DEFAULT;
        b.BindFlags = D3D11_BIND_CONSTANT_BUFFER;
        if (SUCCEEDED(lastError))
            lastError = native->CreateBuffer(&b, nullptr, &constants);
        return SUCCEEDED(lastError);
    }
    bool CreateTargets() {
        ComPtr<ID3D11Texture2D> back;
        lastError = chain->GetBuffer(0, IID_PPV_ARGS(&back));
        if (SUCCEEDED(lastError))
            lastError = native->CreateRenderTargetView(back.Get(), nullptr, &target);
        if (SUCCEEDED(lastError) && desc.depth != SurfaceDepth::None) {
            D3D11_TEXTURE2D_DESC d{};
            d.Width = desc.width;
            d.Height = desc.height;
            d.ArraySize = d.MipLevels = 1;
            d.Format = Depth(desc.depth);
            d.SampleDesc.Count = 1;
            d.BindFlags = D3D11_BIND_DEPTH_STENCIL;
            lastError = native->CreateTexture2D(&d, nullptr, &depth);
            if (SUCCEEDED(lastError))
                lastError = native->CreateDepthStencilView(depth.Get(), nullptr, &depthView);
        }
        return SUCCEEDED(lastError);
    }
    bool EnsurePresentationTarget(const HistoryDesc& d) override {
        if (!ValidHistoryDesc(d) || d.samples != 1 ||
            d.representation != HistoryRepresentation::SingleSample || !d.color || !d.depth ||
            !d.stencil || d.width != presentation.width || d.height != presentation.height ||
            presentation.depth != SurfaceDepth::Depth24Stencil8 || !Live() || inFrame)
            return false;
        if (!offscreen && target)
            return true;
        if (!PrepareForReset())
            return false;
        desc = presentation;
        return CreateTargets() && RestoreAfterReset();
    }
    bool BeginFrame(ClearColor c) override {
        for (float value : {c.red, c.green, c.blue, c.alpha})
            if (!std::isfinite(value) || value < 0 || value > 1)
                return false;
        if (!Live() || inFrame || !target ||
            (!offscreen && IsIconic(reinterpret_cast<HWND>(desc.window.handle))))
            return false;
        // Bound asynchronous resident frames, including obsolete geometry
        // versions, rather than letting a slow GPU accumulate every update.
        while (completions.size() >= 3)
            if (!WaitCompletion({identity, completions.begin()->first}, 5000))
                return false;
        auto* t = target.Get();
        context->OMSetRenderTargets(1, &t, depthView.Get());
        const float rgba[] = {c.red, c.green, c.blue, c.alpha};
        context->ClearRenderTargetView(t, rgba);
        if (depthView)
            context->ClearDepthStencilView(
                depthView.Get(),
                D3D11_CLEAR_DEPTH |
                    (desc.depth == SurfaceDepth::Depth24Stencil8 ? D3D11_CLEAR_STENCIL : 0),
                1, 0);
        D3D11_VIEWPORT viewport{
            0, 0, static_cast<float>(desc.width), static_cast<float>(desc.height), 0, 1};
        context->RSSetViewports(1, &viewport);
        inFrame = true;
        presentable = false;
        pipelineReady = false;
        modelReady = false;
        modelTextures = {};
        boundTexture = {};
        return true;
    }
    bool SetFixedPipeline(FixedPipelineState p) override {
        if (passes.Active())
            return false;
        pipelineReady = false;
        modelReady = false;
        boundTexture = {};
        modelTextures = {};
        if (!Live() || !inFrame || p.topology != PrimitiveTopology::TriangleList ||
            p.layout != VertexLayout::ScreenPositionColorUv || p.lighting || p.alphaTest ||
            p.stencilTest || p.cull != RasterCull::None ||
            p.colorStage != FixedColorStage::TextureDiffuseModulate ||
            (p.depthCompare != CompareOp::Always && p.depthCompare != CompareOp::LessEqual) ||
            (p.depthTest && !depthView) ||
            (p.sampler.min != SamplerFilter::Point && p.sampler.min != SamplerFilter::Linear) ||
            (p.sampler.mag != SamplerFilter::Point && p.sampler.mag != SamplerFilter::Linear) ||
            (p.sampler.mip != SamplerFilter::Point && p.sampler.mip != SamplerFilter::Linear) ||
            (p.sampler.u != SamplerAddress::Wrap && p.sampler.u != SamplerAddress::Clamp) ||
            (p.sampler.v != SamplerAddress::Wrap && p.sampler.v != SamplerAddress::Clamp))
            return false;
        D3D11_SAMPLER_DESC s{};
        s.Filter = static_cast<D3D11_FILTER>((p.sampler.min == SamplerFilter::Linear ? 0x10 : 0) |
                                             (p.sampler.mag == SamplerFilter::Linear ? 0x4 : 0) |
                                             (p.sampler.mip == SamplerFilter::Linear ? 1 : 0));
        s.AddressU = p.sampler.u == SamplerAddress::Clamp ? D3D11_TEXTURE_ADDRESS_CLAMP
                                                          : D3D11_TEXTURE_ADDRESS_WRAP;
        s.AddressV = p.sampler.v == SamplerAddress::Clamp ? D3D11_TEXTURE_ADDRESS_CLAMP
                                                          : D3D11_TEXTURE_ADDRESS_WRAP;
        s.AddressW = D3D11_TEXTURE_ADDRESS_CLAMP;
        s.MaxLOD = D3D11_FLOAT32_MAX;
        s.ComparisonFunc = D3D11_COMPARISON_NEVER;
        s.MaxAnisotropy = 1;
        D3D11_BLEND_DESC b{};
        auto& color = b.RenderTarget[0];
        color.RenderTargetWriteMask = D3D11_COLOR_WRITE_ENABLE_ALL;
        color.BlendEnable = p.alphaBlend;
        color.SrcBlend = D3D11_BLEND_SRC_ALPHA;
        color.DestBlend = D3D11_BLEND_INV_SRC_ALPHA;
        color.BlendOp = color.BlendOpAlpha = D3D11_BLEND_OP_ADD;
        color.SrcBlendAlpha = D3D11_BLEND_SRC_ALPHA;
        color.DestBlendAlpha = D3D11_BLEND_INV_SRC_ALPHA;
        D3D11_DEPTH_STENCIL_DESC d{};
        d.DepthEnable = p.depthTest;
        d.DepthWriteMask = p.depthWrite ? D3D11_DEPTH_WRITE_MASK_ALL : D3D11_DEPTH_WRITE_MASK_ZERO;
        d.DepthFunc = p.depthCompare == CompareOp::Always ? D3D11_COMPARISON_ALWAYS
                                                          : D3D11_COMPARISON_LESS_EQUAL;
        D3D11_RASTERIZER_DESC r{};
        r.FillMode = D3D11_FILL_SOLID;
        r.CullMode = D3D11_CULL_NONE;
        r.DepthClipEnable = TRUE;
        sampler.Reset();
        blend.Reset();
        depthState.Reset();
        raster.Reset();
        lastError = native->CreateSamplerState(&s, &sampler);
        if (SUCCEEDED(lastError))
            lastError = native->CreateBlendState(&b, &blend);
        if (SUCCEEDED(lastError))
            lastError = native->CreateDepthStencilState(&d, &depthState);
        if (SUCCEEDED(lastError))
            lastError = native->CreateRasterizerState(&r, &raster);
        if (FAILED(lastError))
            return false;
        const float size[] = {static_cast<float>(desc.width), static_cast<float>(desc.height), 0,
                              0};
        context->UpdateSubresource(constants.Get(), 0, nullptr, size, 0, 0);
        auto* cb = constants.Get();
        auto* sampling = sampler.Get();
        context->VSSetConstantBuffers(0, 1, &cb);
        context->VSSetShader(vs.Get(), nullptr, 0);
        context->PSSetShader(ps.Get(), nullptr, 0);
        context->GSSetShader(nullptr, nullptr, 0);
        context->IASetInputLayout(layout.Get());
        context->IASetPrimitiveTopology(D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
        context->PSSetSamplers(0, 1, &sampling);
        context->RSSetState(raster.Get());
        context->OMSetDepthStencilState(depthState.Get(), 0);
        context->OMSetBlendState(blend.Get(), nullptr, 0xffffffff);
        const D3D11_VIEWPORT fullViewport{0, 0, float(desc.width), float(desc.height), 0, 1};
        context->RSSetViewports(1, &fullViewport);
        // Pipeline changes require explicit bindings. Native and logical slots
        // must agree after model -> HUD -> model transitions.
        ID3D11ShaderResourceView* emptyViews[3]{};
        context->PSSetShaderResources(0, 3, emptyViews);
        pipelineReady = true;
        return true;
    }
    bool Draw(NonIndexedDraw command) override {
        if (modelReady && modelPipeline.shadow.mode == BuiltinShadowMode::Material &&
            !shadowTexture)
            return false;
        if (modelReady) {
            auto* v = Find(command.vertices);
            const auto stride = ModelVertexStride(modelPipeline.vertexLayout);
            const auto arity = ModelPrimitiveArity(modelPipeline.topology);
            if (!Live() || !inFrame || !IsCurrentSubmission(Status(), command.submission) ||
                !command.layout || *command.layout != modelPipeline.vertexLayout || !stride ||
                !arity || !v || !v->buffer || v->description.usage != BufferUsage::Vertex ||
                !command.vertexCount || command.vertexCount % arity ||
                command.firstVertex > v->description.size / stride ||
                command.vertexCount > v->description.size / stride - command.firstVertex)
                return false;
            for (unsigned stage = 0; stage < 3; ++stage)
                if (modelPipeline.shader.layout.stages[stage].role != MaterialTextureRole::None) {
                    auto* t = Find(modelTextures[stage]);
                    if (!t || !t->uploaded || t->srgb != modelPipeline.shader.srgbSample[stage])
                        return false;
                }
            auto* b = v->buffer.Get();
            UINT nativeStride = stride, offset = 0;
            context->IASetVertexBuffers(0, 1, &b, &nativeStride, &offset);
            context->IASetIndexBuffer(nullptr, DXGI_FORMAT_UNKNOWN, 0);
            context->Draw(command.vertexCount, command.firstVertex);
            return native->GetDeviceRemovedReason() == S_OK;
        }
        auto* r = Find(command.vertices);
        auto* t = Find(boundTexture);
        if (!Live() || !inFrame || !pipelineReady ||
            !IsCurrentSubmission(Status(), command.submission) || !r || !r->buffer ||
            (command.layout && *command.layout != VertexLayout::ScreenPositionColorUv) ||
            r->description.usage != BufferUsage::Vertex || !t || !t->uploaded ||
            !command.vertexCount || command.vertexCount % 3 ||
            command.firstVertex > r->description.size / 28 ||
            command.vertexCount > r->description.size / 28 - command.firstVertex)
            return false;
        auto* b = r->buffer.Get();
        UINT stride = 28, offset = 0;
        context->IASetVertexBuffers(0, 1, &b, &stride, &offset);
        context->Draw(command.vertexCount, command.firstVertex);
        return native->GetDeviceRemovedReason() == S_OK;
    }
    bool DrawIndexed(IndexedDraw command) override {
        if (modelReady && modelPipeline.shadow.mode == BuiltinShadowMode::Material &&
            !shadowTexture)
            return false;
        auto* v = Find(command.vertices);
        auto* i = Find(command.indices);
        const auto stride = ModelVertexStride(command.layout);
        const auto arity = ModelPrimitiveArity(modelPipeline.topology);
        if (!Live() || !inFrame || !modelReady ||
            !IsCurrentSubmission(Status(), command.submission) || !stride ||
            command.layout != modelPipeline.vertexLayout || !v || !i || !v->buffer || !i->buffer ||
            v->description.usage != BufferUsage::Vertex ||
            i->description.usage != BufferUsage::Index || !arity || !command.indexCount ||
            command.indexCount % arity || !command.vertexCount ||
            command.vertexCount > v->description.size / stride)
            return false;
        const auto step = i->description.indexFormat == IndexFormat::UInt16 ? 2u : 4u;
        if (command.firstIndex > i->indices.size() / step ||
            command.indexCount > i->indices.size() / step - command.firstIndex)
            return false;
        for (std::size_t index = command.firstIndex;
             index < static_cast<std::size_t>(command.firstIndex) + command.indexCount; ++index) {
            std::uint32_t value = 0;
            std::memcpy(&value, i->indices.data() + index * step, step);
            if (value >= command.vertexCount)
                return false;
        }
        for (unsigned stage = 0; stage < 3; ++stage)
            if (modelPipeline.shader.layout.stages[stage].role != MaterialTextureRole::None) {
                auto* t = Find(modelTextures[stage]);
                if (!t || !t->uploaded || t->srgb != modelPipeline.shader.srgbSample[stage])
                    return false;
            }
        auto* buffer = v->buffer.Get();
        UINT nativeStride = stride, offset = 0;
        context->IASetVertexBuffers(0, 1, &buffer, &nativeStride, &offset);
        context->IASetIndexBuffer(i->buffer.Get(),
                                  step == 2 ? DXGI_FORMAT_R16_UINT : DXGI_FORMAT_R32_UINT, 0);
        context->DrawIndexed(command.indexCount, command.firstIndex, 0);
        return native->GetDeviceRemovedReason() == S_OK;
    }
    bool SetModelPipeline(const ModelPipeline& p) override;

    bool EndFrame() override {
        if (!inFrame || passes.Active())
            return false;
        inFrame = false;
        presentable = true;
        return true;
    }
    bool Present() override {
        if (offscreen || !Live() || inFrame || !presentable ||
            (!offscreen && IsIconic(reinterpret_cast<HWND>(desc.window.handle))))
            return false;
        presentable = false;
        lastError = chain->Present(desc.present == PresentMode::VSync ? 1 : 0, 0);
        if (FAILED(lastError))
            pending = true;
        return SUCCEEDED(lastError);
    }
    bool PrepareForReset() override {
        if (inFrame || pending)
            return false;
        pending = true;
        presentable = false;
        pipelineReady = false;
        modelReady = false;
        modelTextures = {};
        boundTexture = {};
        // Unbind before releasing *all* swapchain views, including references
        // kept by the immediate context. CPU/GPU uploads remain retained.
        context->ClearState();
        passes.InvalidateContents();
        target.Reset();
        depthView.Reset();
        depth.Reset();
        offscreen.Reset();
        context->Flush();
        D3D11_QUERY_DESC q{D3D11_QUERY_EVENT, 0};
        ComPtr<ID3D11Query> fence;
        lastError = native->CreateQuery(&q, &fence);
        if (FAILED(lastError))
            return false;
        context->End(fence.Get());
        context->Flush();
        const auto deadline = GetTickCount64() + 5000;
        do {
            lastError = context->GetData(fence.Get(), nullptr, 0, 0);
            if (lastError != S_FALSE)
                break;
            Sleep(1);
        } while (GetTickCount64() < deadline);
        if (lastError == S_FALSE)
            lastError = DXGI_ERROR_WAS_STILL_DRAWING;
        return lastError == S_OK;
    }
    bool RestoreAfterReset() override {
        if (retired || !pending || !target || native->GetDeviceRemovedReason() != S_OK)
            return false;
        pending = false;
        if (++epoch == 0)
            ++epoch;
        return true;
    }
};
D3D11_BLEND Blend(BlendFactor b) {
    switch (b) {
    case BlendFactor::Zero:
        return D3D11_BLEND_ZERO;
    case BlendFactor::One:
        return D3D11_BLEND_ONE;
    case BlendFactor::SourceColor:
        return D3D11_BLEND_SRC_COLOR;
    case BlendFactor::InvSourceColor:
        return D3D11_BLEND_INV_SRC_COLOR;
    case BlendFactor::SourceAlpha:
        return D3D11_BLEND_SRC_ALPHA;
    case BlendFactor::InvSourceAlpha:
        return D3D11_BLEND_INV_SRC_ALPHA;
    case BlendFactor::DestinationAlpha:
        return D3D11_BLEND_DEST_ALPHA;
    case BlendFactor::InvDestinationAlpha:
        return D3D11_BLEND_INV_DEST_ALPHA;
    case BlendFactor::DestinationColor:
        return D3D11_BLEND_DEST_COLOR;
    case BlendFactor::InvDestinationColor:
        return D3D11_BLEND_INV_DEST_COLOR;
    case BlendFactor::SourceAlphaSaturate:
        return D3D11_BLEND_SRC_ALPHA_SAT;
    }
    return static_cast<D3D11_BLEND>(0);
}
bool D3D11Device::SetModelPipeline(const ModelPipeline& p) {
    pipelineReady = modelReady = false;
    modelTextures = {};
    boundTexture = {};
    const auto viewport =
        p.viewport.value_or(ModelViewport{0, 0, RenderWidth(), RenderHeight(), 0, 1});
    if (!ValidModelViewport(viewport, RenderWidth(), RenderHeight()))
        return false;
    const auto& s = p.shader;
    std::array<float, 4> evaluated;
    std::array<std::array<float, 4>, 3> samples{};
    if (!ModelVertexStride(p.vertexLayout) || !ModelPrimitiveArity(p.topology) || !Live() ||
        !inFrame || !ValidateMaterialShaderInputs(s) ||
        !EvaluateMaterialTextureCascade(s.layout, {1, 1, 1, 1}, samples, evaluated) ||
        static_cast<unsigned>(p.raster.depthCompare) > 7 ||
        static_cast<unsigned>(p.raster.cull) > 2 || !Blend(p.raster.sourceBlend) ||
        !Blend(p.raster.destinationBlend) || !Blend(p.sourceAlpha) || !Blend(p.destinationAlpha) ||
        static_cast<unsigned>(p.colorOperation) < 1 ||
        static_cast<unsigned>(p.colorOperation) > 5 ||
        static_cast<unsigned>(p.alphaOperation) < 1 ||
        static_cast<unsigned>(p.alphaOperation) > 5 || p.colorWriteMask > 15 ||
        static_cast<unsigned>(p.stencilCompare) > 7 || static_cast<unsigned>(p.stencilFail) < 1 ||
        static_cast<unsigned>(p.stencilFail) > 8 || static_cast<unsigned>(p.stencilDepthFail) < 1 ||
        static_cast<unsigned>(p.stencilDepthFail) > 8 || static_cast<unsigned>(p.stencilPass) < 1 ||
        static_cast<unsigned>(p.stencilPass) > 8 ||
        ((p.raster.depthTest || p.stencil) && !ActiveDepth()) ||
        (p.stencil && !passes.Active() && desc.depth != SurfaceDepth::Depth24Stencil8))
        return false;
    // PositionNormalUv has no COLOR0/COLOR1; native fixed-function falls
    // back to the material for these source selections, as the reference VS does.
    for (unsigned stage = 0; stage < 3; ++stage) {
        const auto& t = s.layout.stages[stage];
        if (t.role != MaterialTextureRole::None &&
            (t.coordinates == MaterialCoordinates::MeshUv1 ||
             s.samplers[stage].maxAnisotropy > 16 ||
             (p.vertexLayout == VertexLayout::PositionColor &&
              t.coordinates != MaterialCoordinates::MeshUv0)))
            return false;
    }
    if (!modelVs) {
        ComPtr<ID3DBlob> vertex, pixel, error;
        const bool compatible = modelSupport.selected == D3D11ModelCompatibility::Dx9Measured;
        const D3D_SHADER_MACRO macros[] = {
            {"MDS_MODEL_SPECULAR", "1"},
            {"MDS_MODEL_DX9_COMPATIBILITY", compatible ? "1" : "0"},
            {"MDS_MODEL_ALPHA12_DIAGNOSTIC", modelSupport.diagnosticAlpha12 ? "1" : "0"},
            {nullptr, nullptr}};
        const std::string vertexSource =
            compatible ? std::string(modelDx9CompatibilitySource) + modelVertexSource
                       : std::string(modelVertexSource);
        const D3D_SHADER_MACRO vertexMacros[] = {
            {"MDS_MODEL_DX9_COMPATIBILITY", compatible ? "1" : "0"},
            {"MDS_MODEL_TEXTURE_FMA_DIAGNOSTIC", modelSupport.diagnosticTextureFma ? "1" : "0"},
            {"MDS_MODEL_SPECULAR_FMA_DIAGNOSTIC", modelSupport.diagnosticSpecularFma ? "1" : "0"},
            {nullptr, nullptr}};
        lastError =
            D3DCompile(vertexSource.data(), vertexSource.size(), nullptr, vertexMacros, nullptr,
                       "main", "vs_5_0", D3DCOMPILE_IEEE_STRICTNESS, 0, &vertex, &error);
        if (SUCCEEDED(lastError))
            lastError = D3DCompile(modelPixelSource, std::strlen(modelPixelSource), nullptr, macros,
                                   nullptr, "main", "ps_5_0", D3DCOMPILE_IEEE_STRICTNESS, 0, &pixel,
                                   &error);
        if (FAILED(lastError))
            return false;
        ComPtr<ID3D11VertexShader> vshader;
        ComPtr<ID3D11PixelShader> pshader;
        ComPtr<ID3D11InputLayout> input;
        D3D11_INPUT_ELEMENT_DESC elements[] = {
            {"POSITION", 0, DXGI_FORMAT_R32G32B32_FLOAT, 0, 0, D3D11_INPUT_PER_VERTEX_DATA, 0},
            {"NORMAL", 0, DXGI_FORMAT_R32G32B32_FLOAT, 0, 12, D3D11_INPUT_PER_VERTEX_DATA, 0},
            {"TEXCOORD", 0, DXGI_FORMAT_R32G32_FLOAT, 0, 24, D3D11_INPUT_PER_VERTEX_DATA, 0}};
        lastError = native->CreateVertexShader(vertex->GetBufferPointer(), vertex->GetBufferSize(),
                                               nullptr, &vshader);
        if (SUCCEEDED(lastError))
            lastError = native->CreatePixelShader(pixel->GetBufferPointer(), pixel->GetBufferSize(),
                                                  nullptr, &pshader);
        if (SUCCEEDED(lastError))
            lastError = native->CreateInputLayout(elements, 3, vertex->GetBufferPointer(),
                                                  vertex->GetBufferSize(), &input);
        if (FAILED(lastError))
            return false;
        modelVs = vshader;
        modelPs = pshader;
        modelLayout = input;
    }
    if (p.vertexLayout == VertexLayout::PositionColor && !modelColorVs) {
        const bool compatible = modelSupport.selected == D3D11ModelCompatibility::Dx9Measured;
        const std::string vertexSource =
            compatible ? std::string(modelDx9CompatibilitySource) + modelVertexSource
                       : std::string(modelVertexSource);
        ComPtr<ID3DBlob> vertex, error;
        const D3D_SHADER_MACRO macros[] = {
            {"MDS_MODEL_VERTEX_COLOR", "1"},
            {"MDS_MODEL_DX9_COMPATIBILITY", compatible ? "1" : "0"},
            {"MDS_MODEL_TEXTURE_FMA_DIAGNOSTIC", modelSupport.diagnosticTextureFma ? "1" : "0"},
            {"MDS_MODEL_SPECULAR_FMA_DIAGNOSTIC", modelSupport.diagnosticSpecularFma ? "1" : "0"},
            {nullptr, nullptr}};
        lastError = D3DCompile(vertexSource.data(), vertexSource.size(), nullptr, macros, nullptr,
                               "main", "vs_5_0", D3DCOMPILE_IEEE_STRICTNESS, 0, &vertex, &error);
        if (FAILED(lastError))
            return false;
        const D3D11_INPUT_ELEMENT_DESC elements[] = {
            {"POSITION", 0, DXGI_FORMAT_R32G32B32_FLOAT, 0, 0, D3D11_INPUT_PER_VERTEX_DATA, 0},
            {"COLOR", 0, DXGI_FORMAT_R8G8B8A8_UNORM, 0, 12, D3D11_INPUT_PER_VERTEX_DATA, 0}};
        lastError = native->CreateVertexShader(vertex->GetBufferPointer(), vertex->GetBufferSize(),
                                               nullptr, &modelColorVs);
        if (SUCCEEDED(lastError))
            lastError = native->CreateInputLayout(elements, 2, vertex->GetBufferPointer(),
                                                  vertex->GetBufferSize(), &modelColorLayout);
        if (FAILED(lastError)) {
            modelColorVs.Reset();
            modelColorLayout.Reset();
            return false;
        }
    }
    detail::ModelConstants inputs;
    const bool measured = modelSupport.selected == D3D11ModelCompatibility::Dx9Measured;
    if (!detail::BuildModelConstants(p, viewport, measured, inputs))
        return false;
    auto& vc = inputs.vertex;
    auto& pc = inputs.pixel;
    if (measured) {
        if (!dx9Constants) {
            D3D11_BUFFER_DESC d{};
            d.ByteWidth = sizeof(inputs.compatibility);
            d.BindFlags = D3D11_BIND_CONSTANT_BUFFER;
            lastError = native->CreateBuffer(&d, nullptr, &dx9Constants);
            if (FAILED(lastError))
                return false;
        }
        context->UpdateSubresource(dx9Constants.Get(), 0, nullptr, &inputs.compatibility, 0, 0);
        auto* constants = dx9Constants.Get();
        context->VSSetConstantBuffers(1, 1, &constants);
    }
#ifdef MDS_RHI_TEST_W_CANDIDATE
    auto diagnosticInputs = w_shader_diagnostic::Inputs(vc.w, vc.v, vc.p, wCandidatePolicy);
    diagnosticInputs.policy[1] = lightingCandidatePolicy;
    diagnosticInputs.policy[2] = normalCandidatePolicy;
    diagnosticInputs.policy[3] = (viewportCenterCandidate ? 1u : 0u) | (fusedXYCandidate ? 2u : 0u);
    if (normalCandidatePolicy && p.vertexLayout != VertexLayout::PositionColor) {
        const auto rounded =
            depth_diagnostic::Multiply(vc.w, vc.v, depth_diagnostic::Rounding::Sequential);
        const std::array<float, 16> identity{1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1};
        if (!BuildShaderNormalMatrix(rounded, identity, vc.n))
            return false;
    }
    D3D11_BUFFER_DESC diagnosticDesc{};
    diagnosticDesc.ByteWidth = sizeof(diagnosticInputs);
    diagnosticDesc.BindFlags = D3D11_BIND_CONSTANT_BUFFER;
    if (!wCandidateConstants) {
        lastError = native->CreateBuffer(&diagnosticDesc, nullptr, &wCandidateConstants);
        if (FAILED(lastError))
            return false;
    }
    context->UpdateSubresource(wCandidateConstants.Get(), 0, nullptr, &diagnosticInputs, 0, 0);
    auto* diagnosticBuffer = wCandidateConstants.Get();
    context->VSSetConstantBuffers(1, 1, &diagnosticBuffer);
#endif
    auto constant = [&](const void* data, UINT size, ComPtr<ID3D11Buffer>& buffer) {
        if (!buffer) {
            D3D11_BUFFER_DESC d{};
            d.ByteWidth = size;
            d.BindFlags = D3D11_BIND_CONSTANT_BUFFER;
            lastError = native->CreateBuffer(&d, nullptr, &buffer);
            if (FAILED(lastError))
                return false;
        }
        context->UpdateSubresource(buffer.Get(), 0, nullptr, data, 0, 0);
        return true;
    };
    if (!constant(&vc, sizeof(vc), modelVertexConstants) ||
        !constant(&pc, sizeof(pc), modelPixelConstants))
        return false;
    for (unsigned i = 0; i < 3; ++i) {
        modelSamplers[i].Reset();
        if (s.layout.stages[i].role == MaterialTextureRole::None)
            continue;
        const auto& sampler = s.samplers[i];
        D3D11_SAMPLER_DESC d;
        std::memset(&d, 0, sizeof(d));
        d.Filter =
            sampler.min == SamplerFilter::Anisotropic || sampler.mag == SamplerFilter::Anisotropic
                ? D3D11_FILTER_ANISOTROPIC
                : static_cast<D3D11_FILTER>((sampler.min == SamplerFilter::Linear ? 16 : 0) |
                                            (sampler.mag == SamplerFilter::Linear ? 4 : 0) |
                                            (sampler.mip == SamplerFilter::Linear ? 1 : 0));
        if (sampler.mip == SamplerFilter::Anisotropic && d.Filter != D3D11_FILTER_ANISOTROPIC)
            return false;
        d.AddressU = sampler.u == SamplerAddress::Wrap ? D3D11_TEXTURE_ADDRESS_WRAP
                                                       : D3D11_TEXTURE_ADDRESS_CLAMP;
        d.AddressV = sampler.v == SamplerAddress::Wrap ? D3D11_TEXTURE_ADDRESS_WRAP
                                                       : D3D11_TEXTURE_ADDRESS_CLAMP;
        d.AddressW = D3D11_TEXTURE_ADDRESS_CLAMP;
        d.MaxAnisotropy = sampler.maxAnisotropy;
        d.MaxLOD = D3D11_FLOAT32_MAX;
        const std::string key(reinterpret_cast<const char*>(&d), sizeof(d));
        auto found = samplerCache.find(key);
        if (found == samplerCache.end()) {
            ComPtr<ID3D11SamplerState> state;
            lastError = native->CreateSamplerState(&d, &state);
            if (FAILED(lastError))
                return false;
            found = samplerCache.emplace(key, std::move(state)).first;
        }
        modelSamplers[i] = found->second;
    }
    D3D11_DEPTH_STENCIL_DESC ds;
    std::memset(&ds, 0, sizeof(ds));
    ds.DepthEnable = p.raster.depthTest;
    ds.DepthWriteMask =
        p.raster.depthWrite ? D3D11_DEPTH_WRITE_MASK_ALL : D3D11_DEPTH_WRITE_MASK_ZERO;
    ds.DepthFunc =
        static_cast<D3D11_COMPARISON_FUNC>(static_cast<unsigned>(p.raster.depthCompare) + 1);
    ds.StencilEnable = p.stencil;
    ds.StencilReadMask = p.stencilReadMask;
    ds.StencilWriteMask = p.stencilWriteMask;
    ds.FrontFace = {
        static_cast<D3D11_STENCIL_OP>(p.stencilFail),
        static_cast<D3D11_STENCIL_OP>(p.stencilDepthFail),
        static_cast<D3D11_STENCIL_OP>(p.stencilPass),
        static_cast<D3D11_COMPARISON_FUNC>(static_cast<unsigned>(p.stencilCompare) + 1)};
    ds.BackFace = ds.FrontFace;
    D3D11_BLEND_DESC bd;
    std::memset(&bd, 0, sizeof(bd));
    auto& b = bd.RenderTarget[0];
    b.BlendEnable = p.raster.alphaBlend;
    b.SrcBlend = b.SrcBlendAlpha = Blend(p.raster.sourceBlend);
    b.DestBlend = b.DestBlendAlpha = Blend(p.raster.destinationBlend);
    b.BlendOp = static_cast<D3D11_BLEND_OP>(p.colorOperation);
    b.BlendOpAlpha =
        static_cast<D3D11_BLEND_OP>(p.separateAlpha ? p.alphaOperation : p.colorOperation);
    if (p.separateAlpha) {
        b.SrcBlendAlpha = Blend(p.sourceAlpha);
        b.DestBlendAlpha = Blend(p.destinationAlpha);
    }
    b.RenderTargetWriteMask = p.colorWriteMask;
    D3D11_RASTERIZER_DESC rs;
    std::memset(&rs, 0, sizeof(rs));
    rs.FillMode = D3D11_FILL_SOLID;
    rs.CullMode = p.raster.cull == BatchCull::None ? D3D11_CULL_NONE : D3D11_CULL_BACK;
    rs.FrontCounterClockwise = p.raster.cull == BatchCull::Clockwise;
    rs.DepthClipEnable = TRUE;
    const std::string bk(reinterpret_cast<const char*>(&bd), sizeof(bd)),
        dk(reinterpret_cast<const char*>(&ds), sizeof(ds)),
        rk(reinterpret_cast<const char*>(&rs), sizeof(rs));
    auto bi = blendCache.find(bk);
    if (bi == blendCache.end()) {
        ComPtr<ID3D11BlendState> state;
        lastError = native->CreateBlendState(&bd, &state);
        if (FAILED(lastError))
            return false;
        bi = blendCache.emplace(bk, std::move(state)).first;
    }
    auto di = depthCache.find(dk);
    if (di == depthCache.end()) {
        ComPtr<ID3D11DepthStencilState> state;
        lastError = native->CreateDepthStencilState(&ds, &state);
        if (FAILED(lastError))
            return false;
        di = depthCache.emplace(dk, std::move(state)).first;
    }
    auto ri = rasterCache.find(rk);
    if (ri == rasterCache.end()) {
        ComPtr<ID3D11RasterizerState> state;
        lastError = native->CreateRasterizerState(&rs, &state);
        if (FAILED(lastError))
            return false;
        ri = rasterCache.emplace(rk, std::move(state)).first;
    }
    blend = bi->second;
    depthState = di->second;
    raster = ri->second;
    context->OMSetBlendState(blend.Get(), nullptr, 0xffffffff);
    context->OMSetDepthStencilState(depthState.Get(), p.stencilReference);
    context->RSSetState(raster.Get());
    context->IASetInputLayout(p.vertexLayout == VertexLayout::PositionColor ? modelColorLayout.Get()
                                                                            : modelLayout.Get());
    context->IASetPrimitiveTopology(p.topology == PrimitiveTopology::LineList
                                        ? D3D11_PRIMITIVE_TOPOLOGY_LINELIST
                                        : D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
    context->VSSetShader(p.vertexLayout == VertexLayout::PositionColor ? modelColorVs.Get()
                                                                       : modelVs.Get(),
                         nullptr, 0);
    context->PSSetShader(modelPs.Get(), nullptr, 0);
    context->GSSetShader(nullptr, nullptr, 0);
    auto* vcb = modelVertexConstants.Get();
    auto* pcb = modelPixelConstants.Get();
    context->VSSetConstantBuffers(0, 1, &vcb);
    context->PSSetConstantBuffers(0, 1, &pcb);
    std::array<ID3D11SamplerState*, 3> samplers{};
    std::array<ID3D11ShaderResourceView*, 3> views{};
    for (unsigned i = 0; i < 3; ++i)
        samplers[i] = modelSamplers[i].Get();
    context->PSSetSamplers(0, 3, samplers.data());
    context->PSSetShaderResources(0, 3, views.data());
    D3D11_VIEWPORT nativeViewport{float(viewport.x),      float(viewport.y), float(viewport.width),
                                  float(viewport.height), viewport.minDepth, viewport.maxDepth};
    if (modelSupport.selected == D3D11ModelCompatibility::Dx9Measured ||
        p.shadow.mode != BuiltinShadowMode::None) {
        nativeViewport.TopLeftX += .5f;
        nativeViewport.TopLeftY += .5f;
    }
#ifdef MDS_RHI_TEST_W_CANDIDATE
    // Test backend only: retain raw clip XY and move pixel-center correction
    // after native clipping. Production never exposes this candidate.
    if (viewportCenterCandidate) {
        nativeViewport.TopLeftX += .5f;
        nativeViewport.TopLeftY += .5f;
    }
#endif
    context->RSSetViewports(1, &nativeViewport);
    if (!SetBuiltinShadow(p, viewport))
        return false;
    modelPipeline = p;
    modelReady = true;
    return true;
}
class D3D11Swapchain final : public Swapchain {
    D3D11Device& device;
    bool retired = false;
    SwapchainDesc surface;

public:
    explicit D3D11Swapchain(D3D11Device& d) : device(d), surface(d.desc) {}
    SwapchainDesc Description() const override { return surface; }
    std::int64_t LastNativeError() const override { return device.lastError; }
    SwapchainSupport Query(const SwapchainDesc& d) const override {
        SwapchainSupport out;
        if (!ValidSwapchainDesc(d) || d.window.system != WindowSystem::Win32 ||
            !IsWindow(reinterpret_cast<HWND>(d.window.handle)) ||
            d.window.handle != device.desc.window.handle || !d.windowed ||
            d.color != SurfaceColor::Rgba8 || d.samples != 1 || d.quality ||
            d.swap != SurfaceSwap::Discard || d.width > 16384 || d.height > 16384 ||
            d.depth == SurfaceDepth::Depth24)
            return out;
        UINT color = 0, depth = 0;
        bool ok =
            SUCCEEDED(device.native->CheckFormatSupport(DXGI_FORMAT_R8G8B8A8_UNORM, &color)) &&
            (color & D3D11_FORMAT_SUPPORT_RENDER_TARGET);
        if (d.depth != SurfaceDepth::None)
            ok = ok && SUCCEEDED(device.native->CheckFormatSupport(Depth(d.depth), &depth)) &&
                 (depth & D3D11_FORMAT_SUPPORT_DEPTH_STENCIL);
        out.supported = out.resize = out.reset = out.controllerOwnsReset = ok;
        out.qualityLevels = ok ? 1 : 0;
        return out;
    }
    SurfaceHealth Health() const override {
        if (retired || device.native->GetDeviceRemovedReason() != S_OK)
            return SurfaceHealth::Removed;
        if (IsIconic(reinterpret_cast<HWND>(device.desc.window.handle)))
            return SurfaceHealth::Suspended;
        return SurfaceHealth::Ready;
    }
    RebuildResult Rebuild(const SwapchainDesc& next, RebuildReason reason) override {
        if (retired)
            return RebuildResult::ReplacementRequired;
        if (reason == RebuildReason::DeviceReplacement)
            return Retire() ? RebuildResult::ReplacementRequired : RebuildResult::Busy;
        if (device.native->GetDeviceRemovedReason() != S_OK) {
            device.pending = true;
            return RebuildResult::ReplacementRequired;
        }
        if (!ValidSwapchainDesc(next))
            return RebuildResult::Invalid;
        if (!Query(next).supported)
            return RebuildResult::Unsupported;
        if (reason != RebuildReason::Resize)
            return RebuildResult::Unsupported;
        if (Health() == SurfaceHealth::Suspended)
            return RebuildResult::RetryLater;
        if (device.inFrame)
            return RebuildResult::Busy;
        if (!device.pending && !device.PrepareForReset())
            return RebuildResult::Failed;
        // Retry also releases partially restored views from a failed attempt.
        device.target.Reset();
        device.depthView.Reset();
        device.depth.Reset();
        device.lastError =
            device.chain->ResizeBuffers(1, next.width, next.height, DXGI_FORMAT_R8G8B8A8_UNORM, 0);
        if (FAILED(device.lastError))
            return device.native->GetDeviceRemovedReason() == S_OK
                       ? RebuildResult::Failed
                       : RebuildResult::ReplacementRequired;
        const auto previous = device.desc;
        device.desc = next;
        if (!device.CreateTargets() || !device.RestoreAfterReset()) {
            device.desc = previous;
            return RebuildResult::Failed;
        }
        surface = next;
        device.presentation = next;
        return RebuildResult::Success;
    }
    bool Retire() override {
        if (retired)
            return true;
        if (device.inFrame)
            return false;
        if (device.native->GetDeviceRemovedReason() != S_OK) {
            device.pending = true;
            device.context->ClearState();
            device.target.Reset();
            device.depthView.Reset();
            device.depth.Reset();
            retired = true;
            device.retired = true;
            return true;
        }
        if (!device.pending && !device.PrepareForReset())
            return false;
        retired = true;
        device.retired = true;
        return true;
    }
};
}
static D3D11Bundle CreateD3D11DeviceInternal(const SwapchainDesc& d, D3D11Driver driver,
                                             D3D11ModelOptions options, bool headless) {
    D3D11Bundle out;
    out.modelSupport.selected = options.compatibility;
    out.modelSupport.diagnosticAlpha12 = options.diagnosticAlpha12;
    out.modelSupport.diagnosticTextureFma = options.diagnosticTextureFma;
    out.modelSupport.diagnosticSpecularFma = options.diagnosticSpecularFma;
    if (options.diagnosticSpecularFma && options.compatibility != ModelCompatibility::Dx9Measured) {
        out.creationError = E_INVALIDARG;
        out.creationReason = "diagnostic_specular_requires_dx9_measured";
        return out;
    }
    if (options.diagnosticTextureFma && options.compatibility != ModelCompatibility::Dx9Measured) {
        out.creationError = E_INVALIDARG;
        out.creationReason = "diagnostic_texture_requires_dx9_measured";
        return out;
    }
    if (options.diagnosticAlpha12 && options.compatibility != ModelCompatibility::Dx9Measured) {
        out.creationError = E_INVALIDARG;
        out.creationReason = "diagnostic_alpha_requires_dx9_measured";
        return out;
    }
    if (CheckD3D11ModelCompatibility(options.compatibility, true) ==
        D3D11CompatibilityResult::InvalidMode) {
        out.creationError = E_INVALIDARG;
        out.creationReason = "invalid_model_compatibility";
        return out;
    }
    if ((!headless && (!ValidSwapchainDesc(d) || d.window.system != WindowSystem::Win32 ||
                       !IsWindow(reinterpret_cast<HWND>(d.window.handle)))) ||
        !d.width || !d.height || !d.windowed || d.color != SurfaceColor::Rgba8 || d.samples != 1 ||
        d.quality || d.swap != SurfaceSwap::Discard || d.depth == SurfaceDepth::Depth24 ||
        d.width > 16384 || d.height > 16384 ||
        (driver != D3D11Driver::Hardware && driver != D3D11Driver::Warp)) {
        out.creationError = E_INVALIDARG;
        return out;
    }
    auto device = std::make_unique<D3D11Device>();
    device->desc = device->presentation = d;
    DXGI_SWAP_CHAIN_DESC swap{};
    swap.BufferDesc.Width = d.width;
    swap.BufferDesc.Height = d.height;
    swap.BufferDesc.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
    swap.SampleDesc.Count = 1;
    swap.BufferUsage = DXGI_USAGE_RENDER_TARGET_OUTPUT;
    swap.BufferCount = 1;
    swap.OutputWindow = reinterpret_cast<HWND>(d.window.handle);
    swap.Windowed = TRUE;
    swap.SwapEffect = DXGI_SWAP_EFFECT_DISCARD;
    const D3D_FEATURE_LEVEL levels[] = {D3D_FEATURE_LEVEL_11_0};
    D3D_FEATURE_LEVEL obtained{};
    out.stage = D3D11CreationStage::NativeDevice;
    device->lastError =
        headless
            ? D3D11CreateDevice(nullptr,
                                driver == D3D11Driver::Warp ? D3D_DRIVER_TYPE_WARP
                                                            : D3D_DRIVER_TYPE_HARDWARE,
                                nullptr, 0, levels, 1, D3D11_SDK_VERSION, &device->native,
                                &obtained, &device->context)
            : D3D11CreateDeviceAndSwapChain(
                  nullptr,
                  driver == D3D11Driver::Warp ? D3D_DRIVER_TYPE_WARP : D3D_DRIVER_TYPE_HARDWARE,
                  nullptr, 0, levels, 1, D3D11_SDK_VERSION, &swap, &device->chain, &device->native,
                  &obtained, &device->context);
    if (SUCCEEDED(device->lastError)) {
        ComPtr<IDXGIDevice> dxgi;
        ComPtr<IDXGIAdapter> adapter;
        ComPtr<IDXGIFactory> factory;
        if (SUCCEEDED(device->native.As(&dxgi)) && SUCCEEDED(dxgi->GetAdapter(&adapter))) {
            DXGI_ADAPTER_DESC info{};
            if (SUCCEEDED(adapter->GetDesc(&info))) {
                out.vendorId = info.VendorId;
                out.deviceId = info.DeviceId;
                char name[512]{};
                WideCharToMultiByte(CP_UTF8, 0, info.Description, -1, name, sizeof(name), nullptr,
                                    nullptr);
                out.adapterName = name;
            }
            if (!headless && SUCCEEDED(adapter->GetParent(IID_PPV_ARGS(&factory))))
                factory->MakeWindowAssociation(swap.OutputWindow, DXGI_MWA_NO_ALT_ENTER);
        }
        out.stage = D3D11CreationStage::Compatibility;
        D3D11_FEATURE_DATA_DOUBLES doubles{};
        out.modelSupport.shaderDoubles = SUCCEEDED(device->native->CheckFeatureSupport(
                                             D3D11_FEATURE_DOUBLES, &doubles, sizeof(doubles))) &&
                                         doubles.DoublePrecisionFloatShaderOps;
        out.modelSupport.requestedSupport =
            CheckD3D11ModelCompatibility(options.compatibility, out.modelSupport.shaderDoubles);
        device->modelSupport = out.modelSupport;
        if (out.modelSupport.requestedSupport != D3D11CompatibilityResult::Supported) {
            out.creationError = DXGI_ERROR_UNSUPPORTED;
            out.creationReason = "shader_doubles_required";
            return out;
        }
        out.stage = D3D11CreationStage::Shaders;
        if (device->InitializeShaders()) {
            out.stage = D3D11CreationStage::Targets;
            if (headless)
                ResizeD3D11Offscreen(*device, d.width, d.height);
            else
                device->CreateTargets();
        }
    }
    out.creationError = device->lastError;
    if (FAILED(device->lastError))
        return out;
    auto controller =
        headless ? std::unique_ptr<D3D11Swapchain>{} : std::make_unique<D3D11Swapchain>(*device);
    if (controller && !controller->Query(d).supported) {
        out.creationError = DXGI_ERROR_UNSUPPORTED;
        return out;
    }
    out.stage = D3D11CreationStage::Ready;
    out.device = std::move(device);
    out.swapchain = std::move(controller);
    return out;
}
D3D11Bundle CreateD3D11Device(const SwapchainDesc& d, D3D11Driver driver,
                              D3D11ModelOptions options) {
    return CreateD3D11DeviceInternal(d, driver, options, false);
}
D3D11Bundle CreateD3D11OffscreenDevice(std::uint32_t width, std::uint32_t height,
                                       D3D11Driver driver, D3D11ModelOptions options) {
    SwapchainDesc d;
    d.width = width;
    d.height = height;
    return CreateD3D11DeviceInternal(d, driver, options, true);
}
Handle CreateD3D11ModelTexture(Device& base, const ModelTextureData& data) {
    auto* d = dynamic_cast<D3D11Device*>(&base);
    if (!d || !d->Live() || d->inFrame || data.levels.empty() || data.levels.size() > 15 ||
        (data.generateMips && data.levels.size() != 1))
        return {};
    const auto& top = data.levels[0];
    if (!top.width || !top.height || top.width > 16384 || top.height > 16384)
        return {};
    std::vector<D3D11_SUBRESOURCE_DATA> initial;
    unsigned width = top.width, height = top.height;
    for (std::size_t i = 0; i < data.levels.size(); ++i) {
        const auto& level = data.levels[i];
        if (level.width != width || level.height != height ||
            level.pixels.size() != static_cast<std::size_t>(width) * height * 4)
            return {};
        initial.push_back({level.pixels.data(), width * 4, 0});
        if (width == 1 && height == 1 && i + 1 < data.levels.size())
            return {};
        width = std::max(1u, width / 2);
        height = std::max(1u, height / 2);
    }
    D3D11_TEXTURE2D_DESC desc{};
    desc.Width = top.width;
    desc.Height = top.height;
    desc.MipLevels = data.generateMips ? 0 : static_cast<UINT>(initial.size());
    desc.ArraySize = 1;
    desc.Format = data.srgb ? DXGI_FORMAT_R8G8B8A8_UNORM_SRGB : DXGI_FORMAT_R8G8B8A8_UNORM;
    desc.SampleDesc.Count = 1;
    desc.BindFlags =
        D3D11_BIND_SHADER_RESOURCE | (data.generateMips ? D3D11_BIND_RENDER_TARGET : 0);
    desc.MiscFlags = data.generateMips ? D3D11_RESOURCE_MISC_GENERATE_MIPS : 0;
    Resource r;
    r.alive = r.uploaded = true;
    r.srgb = data.srgb;
    r.textureDescription = {top.width, top.height};
    d->lastError =
        d->native->CreateTexture2D(&desc, data.generateMips ? nullptr : initial.data(), &r.texture);
    if (SUCCEEDED(d->lastError))
        d->lastError = d->native->CreateShaderResourceView(r.texture.Get(), nullptr, &r.view);
    if (FAILED(d->lastError))
        return {};
    if (data.generateMips) {
        d->context->UpdateSubresource(r.texture.Get(), 0, nullptr, top.pixels.data(), top.width * 4,
                                      0);
        d->context->GenerateMips(r.view.Get());
    }
    return d->Insert(std::move(r));
}
bool ResizeD3D11Offscreen(Device& base, std::uint32_t width, std::uint32_t height) {
    auto* d = dynamic_cast<D3D11Device*>(&base);
    if (!d || d->retired || !width || !height || width > 4096 || height > 4096 || d->inFrame ||
        d->native->GetDeviceRemovedReason() != S_OK)
        return false;
    if (!d->pending && !d->PrepareForReset())
        return false;
    d->context->ClearState();
    d->offscreen.Reset();
    d->target.Reset();
    d->depthView.Reset();
    d->depth.Reset();
    D3D11_TEXTURE2D_DESC t{};
    t.Width = width;
    t.Height = height;
    t.MipLevels = t.ArraySize = 1;
    t.SampleDesc.Count = 1;
    t.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
    t.BindFlags = D3D11_BIND_RENDER_TARGET;
    d->lastError = d->native->CreateTexture2D(&t, nullptr, &d->offscreen);
    if (SUCCEEDED(d->lastError))
        d->lastError = d->native->CreateRenderTargetView(d->offscreen.Get(), nullptr, &d->target);
    t.Format = DXGI_FORMAT_D24_UNORM_S8_UINT;
    t.BindFlags = D3D11_BIND_DEPTH_STENCIL;
    if (SUCCEEDED(d->lastError))
        d->lastError = d->native->CreateTexture2D(&t, nullptr, &d->depth);
    if (SUCCEEDED(d->lastError))
        d->lastError = d->native->CreateDepthStencilView(d->depth.Get(), nullptr, &d->depthView);
    if (FAILED(d->lastError))
        return false;
    d->desc.width = width;
    d->desc.height = height;
    d->desc.depth = SurfaceDepth::Depth24Stencil8;
    return d->RestoreAfterReset();
}
bool ReadD3D11Offscreen(Device& base, RgbaImage& image) {
    image = {};
    auto* d = dynamic_cast<D3D11Device*>(&base);
    if (!d || !d->Live() || d->inFrame || !d->offscreen)
        return false;
    D3D11_TEXTURE2D_DESC desc{};
    d->offscreen->GetDesc(&desc);
    desc.BindFlags = 0;
    desc.Usage = D3D11_USAGE_STAGING;
    desc.CPUAccessFlags = D3D11_CPU_ACCESS_READ;
    ComPtr<ID3D11Texture2D> cpu;
    d->lastError = d->native->CreateTexture2D(&desc, nullptr, &cpu);
    if (FAILED(d->lastError))
        return false;
    d->context->CopyResource(cpu.Get(), d->offscreen.Get());
    D3D11_MAPPED_SUBRESOURCE data{};
    d->lastError = d->context->Map(cpu.Get(), 0, D3D11_MAP_READ, 0, &data);
    if (FAILED(d->lastError))
        return false;
    image.width = desc.Width;
    image.height = desc.Height;
    image.pixels.resize(static_cast<std::size_t>(desc.Width) * desc.Height * 4);
    for (UINT y = 0; y < desc.Height; ++y)
        std::memcpy(image.pixels.data() + static_cast<std::size_t>(y) * desc.Width * 4,
                    static_cast<const std::uint8_t*>(data.pData) +
                        static_cast<std::size_t>(y) * data.RowPitch,
                    desc.Width * 4);
    d->context->Unmap(cpu.Get(), 0);
    return true;
}
bool AcquireD3D11Backbuffer(Device& base, ID3D11Texture2D** output) {
    if (!output)
        return false;
    *output = nullptr;
    auto* d = dynamic_cast<D3D11Device*>(&base);
    return d && d->Live() && !d->inFrame && SUCCEEDED(d->chain->GetBuffer(0, IID_PPV_ARGS(output)));
}
// The diagnostic TU has a private copy of the adapter; these queries describe
// production devices only and must not create duplicate exported symbols.
#ifndef MDS_RHI_TEST_W_CANDIDATE
D3D11ModelSupport QueryD3D11ModelSupport(Device& base, D3D11ModelCompatibility requested) {
    auto* d = dynamic_cast<D3D11Device*>(&base);
    if (!d)
        return {};
    auto out = d->modelSupport;
    out.requestedSupport = CheckD3D11ModelCompatibility(requested, out.shaderDoubles);
    return out;
}
#endif
ModelReplayResult ReplayD3D11ModelPrefix(Device& base, const ModelReplayStream& stream,
                                         std::size_t count, RgbaImage& image,
                                         D3D11ReplayTiming* timing) {
    if (timing)
        *timing = {};
    image = {};
    auto result = ValidateModelReplay(stream);
    if (!result)
        return result;
    if (count > result.draws)
        return {false, 0, 0, "invalid_draw_boundary"};
    auto* d = dynamic_cast<D3D11Device*>(&base);
    if (!d || !d->Live() || d->inFrame)
        return {false, 0, 0, "device_not_ready"};
    // Allocate every immutable version before touching the caller's target.
    // Handles belong only to this replay and are released on all exit paths.
    struct Uploads {
        Device& device;
        std::map<ReplayResourceKey, Handle> handles;
        ~Uploads() {
            for (const auto& entry : handles)
                device.Destroy(entry.second);
        }
    } uploads{base, {}};
    for (const auto& r : stream.resources) {
        Handle h;
        if (r.kind == ReplayResourceKind::Texture)
            h = CreateD3D11ModelTexture(base, r.texture);
        else {
            BufferDesc desc{r.kind == ReplayResourceKind::Vertices ? BufferUsage::Vertex
                                                                   : BufferUsage::Index,
                            r.bytes.size()};
            desc.indexFormat = r.indexFormat;
            h = base.CreateBuffer(desc, r.bytes.data());
        }
        if (!h)
            return {false, 0, 0, "resource_upload_failed"};
        uploads.handles.emplace(r.key, h);
    }
    if (!ResizeD3D11Offscreen(base, stream.target.width, stream.target.height))
        return {false, 0, 0, "target_rebuild_failed"};
    ComPtr<ID3D11Query> disjoint, start, finish;
    struct ClockScope {
        ID3D11DeviceContext* context;
        ComPtr<ID3D11Query>& finish;
        ComPtr<ID3D11Query>& disjoint;
        bool active = false;
        void Stop() {
            if (active) {
                context->End(finish.Get());
                context->End(disjoint.Get());
                active = false;
            }
        }
        ~ClockScope() { Stop(); }
    } clock{d->context.Get(), finish, disjoint};
    if (timing) {
        D3D11_QUERY_DESC query{D3D11_QUERY_TIMESTAMP_DISJOINT, 0};
        if (FAILED(d->native->CreateQuery(&query, &disjoint)))
            return {false, 0, 0, "timing_query_failed"};
        query.Query = D3D11_QUERY_TIMESTAMP;
        if (FAILED(d->native->CreateQuery(&query, &start)) ||
            FAILED(d->native->CreateQuery(&query, &finish)))
            return {false, 0, 0, "timing_query_failed"};
        d->context->Begin(disjoint.Get());
        d->context->End(start.Get());
        clock.active = true;
    }
    if (!base.BeginFrame(stream.clear))
        return {false, 0, 0, "begin_frame_failed"};
    struct Frame {
        Device& device;
        bool active = true;
        ~Frame() {
            if (active)
                device.EndFrame();
        }
    } frame{base};
    result = {true, 0, 0, "replayed"};
    for (const auto& event : stream.events) {
        if (!count)
            break; // Explicit clear-only boundary; still validate the full stream.
        if (event.op != ModelReplayOp::Draw)
            continue;
        const auto& draw = event.draw;
        result.sequence = event.sequence;
        if (!base.SetModelPipeline(draw.pipeline))
            return {false, event.sequence, result.draws, "pipeline_failed"};
        for (unsigned slot = 0; slot < 3; ++slot)
            if (draw.pipeline.shader.layout.stages[slot].role != MaterialTextureRole::None &&
                !base.BindTexture(slot, uploads.handles.at(draw.textures[slot])))
                return {false, event.sequence, result.draws, "binding_failed"};
        bool drawn = false;
        if (draw.indexed) {
            auto command = base.Record(IndexedDraw{
                uploads.handles.at(draw.vertices), uploads.handles.at(draw.indices),
                draw.firstIndex, draw.indexCount, draw.vertexCount, draw.pipeline.vertexLayout});
            drawn = base.DrawIndexed(command);
        } else {
            NonIndexedDraw command{uploads.handles.at(draw.vertices), draw.firstVertex,
                                   draw.drawVertexCount};
            command.layout = draw.pipeline.vertexLayout;
            drawn = base.Draw(base.Record(command));
        }
        if (!drawn)
            return {false, event.sequence, result.draws, "draw_failed"};
        if (++result.draws == count)
            break;
    }
    if (!base.EndFrame())
        return {false, result.sequence, result.draws, "end_frame_failed"};
    frame.active = false;
    clock.Stop();
    if (!ReadD3D11Offscreen(base, image))
        return {false, result.sequence, result.draws, "readback_failed"};
    if (timing) {
        D3D11_QUERY_DATA_TIMESTAMP_DISJOINT data{};
        UINT64 a = 0, b = 0;
        HRESULT ready = S_FALSE;
        const auto deadline = GetTickCount64() + 5000;
        do {
            ready = d->context->GetData(disjoint.Get(), &data, sizeof(data), 0);
            if (ready == S_FALSE)
                Sleep(1);
        } while (ready == S_FALSE && GetTickCount64() < deadline);
        if (ready != S_OK || d->context->GetData(start.Get(), &a, sizeof(a), 0) != S_OK ||
            d->context->GetData(finish.Get(), &b, sizeof(b), 0) != S_OK)
            return {false, result.sequence, result.draws, "timing_read_failed"};
        timing->disjoint = data.Disjoint != FALSE;
        timing->frequency = data.Frequency;
        timing->valid = !timing->disjoint && data.Frequency && b >= a;
        if (timing->valid)
            timing->gpuSeconds = double(b - a) / data.Frequency;
    }
    return result;
}
bool ReadD3D11Swapchain(Device& base, RgbaImage& image) {
    image = {};
    auto* d = dynamic_cast<D3D11Device*>(&base);
    if (!d || !d->Live() || d->inFrame || d->offscreen)
        return false;
    ComPtr<ID3D11Texture2D> back, cpu;
    d->lastError = d->chain->GetBuffer(0, IID_PPV_ARGS(&back));
    if (FAILED(d->lastError))
        return false;
    D3D11_TEXTURE2D_DESC desc{};
    back->GetDesc(&desc);
    if (desc.SampleDesc.Count != 1 || desc.Format != DXGI_FORMAT_R8G8B8A8_UNORM)
        return false;
    desc.Usage = D3D11_USAGE_STAGING;
    desc.BindFlags = 0;
    desc.CPUAccessFlags = D3D11_CPU_ACCESS_READ;
    desc.MiscFlags = 0;
    d->lastError = d->native->CreateTexture2D(&desc, nullptr, &cpu);
    if (FAILED(d->lastError))
        return false;
    d->context->CopyResource(cpu.Get(), back.Get());
    D3D11_MAPPED_SUBRESOURCE data{};
    d->lastError = d->context->Map(cpu.Get(), 0, D3D11_MAP_READ, 0, &data);
    if (FAILED(d->lastError))
        return false;
    image.width = desc.Width;
    image.height = desc.Height;
    image.pixels.resize(static_cast<std::size_t>(desc.Width) * desc.Height * 4);
    for (UINT y = 0; y < desc.Height; ++y)
        std::memcpy(image.pixels.data() + static_cast<std::size_t>(y) * desc.Width * 4,
                    static_cast<const std::uint8_t*>(data.pData) +
                        static_cast<std::size_t>(y) * data.RowPitch,
                    desc.Width * 4);
    d->context->Unmap(cpu.Get(), 0);
    return true;
}
}
