#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <Windows.h>
#include <d3d12.h>
#include <d3d12sdklayers.h>
#include <dxgi1_6.h>
#include <d3dcompiler.h>
#include <wrl/client.h>
#include "mikudancestudio/rhi_d3d12.hpp"
#include "mikudancestudio/rhi_render_pass.hpp"
#include "rhi_model_constants.hpp"
#include "rhi_model_shaders.hpp"
#include "rhi_shader_d3d.hpp"
#include "rhi_format_d3d.hpp"
#include <algorithm>
#include <cstring>
#include <cstdio>
#include <limits>
#include <map>
#include <mutex>
#include <vector>

using Microsoft::WRL::ComPtr;
namespace mikudancestudio::rhi {
namespace {
constexpr unsigned FrameCount = 3, BackbufferCount = 2, MaxDraws = 512,
                   DescriptorCount = MaxDraws * 4, MaxResources = 8192;
constexpr UINT64 ConstantBytes = 2 * 1024 * 1024;
D3D12_HEAP_PROPERTIES Heap(D3D12_HEAP_TYPE type) {
    D3D12_HEAP_PROPERTIES p{};
    p.Type = type;
    p.CreationNodeMask = p.VisibleNodeMask = 1;
    return p;
}
D3D12_RESOURCE_DESC Buffer(UINT64 bytes) {
    D3D12_RESOURCE_DESC d{};
    d.Dimension = D3D12_RESOURCE_DIMENSION_BUFFER;
    d.Width = bytes;
    d.Height = d.DepthOrArraySize = d.MipLevels = 1;
    d.SampleDesc.Count = 1;
    d.Layout = D3D12_TEXTURE_LAYOUT_ROW_MAJOR;
    return d;
}
D3D12_RESOURCE_DESC Texture(unsigned w, unsigned h, DXGI_FORMAT format, unsigned mips = 1) {
    D3D12_RESOURCE_DESC d{};
    d.Dimension = D3D12_RESOURCE_DIMENSION_TEXTURE2D;
    d.Width = w;
    d.Height = h;
    d.DepthOrArraySize = 1;
    d.MipLevels = static_cast<UINT16>(mips);
    d.Format = format;
    d.SampleDesc.Count = 1;
    return d;
}
DXGI_FORMAT Depth(SurfaceDepth d) {
    return d == SurfaceDepth::Depth16 ? DXGI_FORMAT_D16_UNORM : DXGI_FORMAT_D24_UNORM_S8_UINT;
}
D3D12_BLEND Blend(BlendFactor b) {
    static constexpr D3D12_BLEND values[] = {D3D12_BLEND_ZERO,         D3D12_BLEND_ONE,
                                             D3D12_BLEND_SRC_COLOR,    D3D12_BLEND_INV_SRC_COLOR,
                                             D3D12_BLEND_SRC_ALPHA,    D3D12_BLEND_INV_SRC_ALPHA,
                                             D3D12_BLEND_DEST_ALPHA,   D3D12_BLEND_INV_DEST_ALPHA,
                                             D3D12_BLEND_DEST_COLOR,   D3D12_BLEND_INV_DEST_COLOR,
                                             D3D12_BLEND_SRC_ALPHA_SAT};
    return unsigned(b) < std::size(values) ? values[unsigned(b)] : static_cast<D3D12_BLEND>(0);
}
bool AlphaFactor(BlendFactor b) {
    return b != BlendFactor::SourceColor && b != BlendFactor::InvSourceColor &&
           b != BlendFactor::DestinationColor && b != BlendFactor::InvDestinationColor &&
           b != BlendFactor::SourceAlphaSaturate;
}
struct Resource {
    std::uint32_t generation = 1;
    bool alive = false, texture = false, srgb = false, uploaded = false;
    BufferDesc buffer{};
    TextureDesc image{};
    SnapshotBytes indices;
    ComPtr<ID3D12Resource> native;
    ComPtr<ID3D12Resource> dynamicUpload;
    UINT64 uploadUse = 0;
    bool uploadQueued = false;
    D3D12_RESOURCE_STATES state = D3D12_RESOURCE_STATE_COPY_DEST;
};
struct UploadJob {
    ComPtr<ID3D12Resource> destination, source;
    Handle retainedSource;
    std::vector<D3D12_PLACED_SUBRESOURCE_FOOTPRINT> footprints;
    UINT64 offset = 0, bytes = 0;
    D3D12_RESOURCE_STATES before = D3D12_RESOURCE_STATE_COPY_DEST,
                          after = D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE;
};
struct Retirement {
    UINT64 fence = 0;
    ComPtr<IUnknown> object;
};
struct Frame {
    ComPtr<ID3D12CommandAllocator> allocator;
    ComPtr<ID3D12GraphicsCommandList> commands;
    ComPtr<ID3D12DescriptorHeap> views, samplers;
    ComPtr<ID3D12Resource> constants;
    std::uint8_t* mapped = nullptr;
    UINT64 fence = 0, constantOffset = 0;
    unsigned drawCount = 0;
    std::vector<ComPtr<ID3D12Resource>> uploads;
};
class D3D12Device final : public Device {
public:
    ComPtr<ID3D12Device> native;
    ComPtr<IDXGISwapChain3> chain;
    ComPtr<ID3D12CommandQueue> queue;
    ComPtr<ID3D12Fence> fence;
    ComPtr<ID3D12InfoQueue> debug;
    ComPtr<ID3D12InfoQueue1> debugCallback;
    DWORD debugCookie = 0;
    ComPtr<ID3D12DescriptorHeap> rtvs, dsvs, cpuViews;
    ComPtr<ID3D12RootSignature> root;
    ComPtr<ID3D12RootSignature> genericRoot;
    std::map<std::string, detail::D3DShaderProgram> genericPrograms;
    ShaderPipeline genericPipeline;
    ShaderResourceBindings genericBindings;
    bool genericReady = false, genericBound = false;
    std::string genericError;
    ShaderCapabilities ShaderFeatures() const override {
        auto caps = GraphicsShaderProfile(true, false);
        caps.maxColorAttachments = 4;
        caps.constantAlignment = 256;
        caps.legacyDrawConstantBytes = 4096;
        caps.maxFrameConstantBytes = ConstantBytes;
        return caps;
    }
    TextureFormatCapabilities FormatFeatures(TextureFormat f) const override {
        D3D12_FEATURE_DATA_FORMAT_SUPPORT data{detail::DxgiFormat(f)};
        if (!native ||
            FAILED(native->CheckFeatureSupport(D3D12_FEATURE_FORMAT_SUPPORT, &data, sizeof(data))))
            return {};
        const bool depth = f == TextureFormat::Depth24Stencil8;
        return {bool(data.Support1 & (depth ? D3D12_FORMAT_SUPPORT1_DEPTH_STENCIL
                                            : D3D12_FORMAT_SUPPORT1_RENDER_TARGET)),
                !depth && bool(data.Support1 & D3D12_FORMAT_SUPPORT1_SHADER_SAMPLE),
                !depth && bool(data.Support1 & D3D12_FORMAT_SUPPORT1_BLENDABLE),
                !depth && bool(data.Support1 & D3D12_FORMAT_SUPPORT1_TEXTURE2D)};
    }
    const char* ShaderDiagnostic() const override { return genericError.c_str(); }
    bool PrepareShaderPipeline(const ShaderPipeline&) override;
    bool SetShaderPipeline(const ShaderPipeline&) override;
    bool BindShaderResources(const ShaderResourceBindings& values) override {
        genericBound = false;
        if (!Live() || !inFrame || !modelReady || !genericReady)
            return false;
        const auto result = ValidateShaderBindings(genericPipeline, values, [&](Handle h) {
            const auto* r = Find(h);
            const auto* t = passes.Description(h);
            return ShaderTextureInfo{r && r->texture, r && r->uploaded,
                                     r && (t ? passes.CanSample(h) : r->uploaded), r && r->srgb,
                                     t ? t->format : TextureFormat::Rgba8};
        });
        genericError = result.reason;
        if (!result)
            return false;
        genericBindings = values;
        genericBound = true;
        return true;
    }
    bool BindGenericDescriptors() {
        if (!genericBound || !BindShaderResources(genericBindings))
            return false;
        auto& f = frames[frameIndex];
        if (f.drawCount >= MaxDraws)
            return false;
        const std::array<float, 4> zero{};
        const auto empty = Constants(zero.data(), sizeof(zero));
        if (!empty)
            return false;
        for (unsigned i = 0; i < 4; ++i)
            f.commands->SetGraphicsRootConstantBufferView(i, empty);
        for (const auto& c : genericBindings.constants) {
            const auto address = Constants(c.bytes.data(), c.bytes.size());
            if (!address)
                return false;
            f.commands->SetGraphicsRootConstantBufferView(c.slot, address);
        }
        auto views = f.views->GetCPUDescriptorHandleForHeapStart(),
             samplers = f.samplers->GetCPUDescriptorHandleForHeapStart();
        const auto offset = f.drawCount * 4;
        views.ptr += SIZE_T(offset) * viewStride;
        samplers.ptr += SIZE_T(offset) * samplerStride;
        for (unsigned i = 0; i < 4; ++i) {
            Handle h;
            for (const auto& t : genericBindings.textures)
                if (t.slot == i)
                    h = t.texture;
            if (h)
                native->CopyDescriptorsSimple(1, views, CpuView(h.index),
                                              D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV);
            else {
                D3D12_SHADER_RESOURCE_VIEW_DESC v{};
                v.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
                v.ViewDimension = D3D12_SRV_DIMENSION_TEXTURE2D;
                v.Shader4ComponentMapping = D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING;
                v.Texture2D.MipLevels = 1;
                native->CreateShaderResourceView(nullptr, &v, views);
            }
            SamplerDesc a;
            a.min = a.mag = a.mip = SamplerFilter::Point;
            for (const auto& s : genericBindings.samplers)
                if (s.slot == i)
                    a = s.sampler;
            D3D12_SAMPLER_DESC s{};
            s.Filter = a.min == SamplerFilter::Anisotropic || a.mag == SamplerFilter::Anisotropic
                           ? D3D12_FILTER_ANISOTROPIC
                           : D3D12_FILTER((a.min == SamplerFilter::Linear ? 16 : 0) |
                                          (a.mag == SamplerFilter::Linear ? 4 : 0) |
                                          (a.mip == SamplerFilter::Linear ? 1 : 0));
            s.AddressU = a.u == SamplerAddress::Wrap ? D3D12_TEXTURE_ADDRESS_MODE_WRAP
                                                     : D3D12_TEXTURE_ADDRESS_MODE_CLAMP;
            s.AddressV = a.v == SamplerAddress::Wrap ? D3D12_TEXTURE_ADDRESS_MODE_WRAP
                                                     : D3D12_TEXTURE_ADDRESS_MODE_CLAMP;
            s.AddressW = D3D12_TEXTURE_ADDRESS_MODE_CLAMP;
            s.ComparisonFunc = D3D12_COMPARISON_FUNC_ALWAYS;
            s.MaxAnisotropy = a.maxAnisotropy;
            s.MaxLOD = D3D12_FLOAT32_MAX;
            native->CreateSampler(&s, samplers);
            views.ptr += viewStride;
            samplers.ptr += samplerStride;
        }
        auto gpuViews = f.views->GetGPUDescriptorHandleForHeapStart(),
             gpuSamplers = f.samplers->GetGPUDescriptorHandleForHeapStart();
        gpuViews.ptr += UINT64(offset) * viewStride;
        gpuSamplers.ptr += UINT64(offset) * samplerStride;
        f.commands->SetGraphicsRootDescriptorTable(4, gpuViews);
        f.commands->SetGraphicsRootDescriptorTable(5, gpuSamplers);
        ++f.drawCount;
        return true;
    }
    ComPtr<ID3DBlob> vertex, colorVertex, pixel;
    ComPtr<ID3DBlob> screenVertex, screenPixel, shadowVertex, shadowOutlineVertex, shadowPixel;
    ComPtr<ID3DBlob> postVertex, postPixel;
    Handle shadowTexture;
    std::map<std::string, ComPtr<ID3D12PipelineState>> pipelines;
    std::array<Frame, FrameCount> frames;
    std::array<ComPtr<ID3D12Resource>, BackbufferCount> backbuffers;
    ComPtr<ID3D12Resource> offscreen, depth;
    std::vector<Resource> resources;
    RenderPassState passes;
    unsigned RenderWidth() const {
        const auto& p = passes.Current();
        const auto* d =
            passes.Description(PrimaryAttachment(p) ? PrimaryAttachment(p)->texture : Handle{});
        return d ? d->width : targetWidth;
    }
    unsigned RenderHeight() const {
        const auto& p = passes.Current();
        const auto* d =
            passes.Description(PrimaryAttachment(p) ? PrimaryAttachment(p)->texture : Handle{});
        return d ? d->height : targetHeight;
    }
    DXGI_FORMAT ActiveDepthFormat() const {
        if (passes.Active())
            return passes.Current().depthStencil ? DXGI_FORMAT_D24_UNORM_S8_UINT
                                                 : DXGI_FORMAT_UNKNOWN;
        return depth ? depth->GetDesc().Format : DXGI_FORMAT_UNKNOWN;
    }
    std::vector<DXGI_FORMAT> ActiveColorFormats() const {
        if (!passes.Active())
            return {DXGI_FORMAT_R8G8B8A8_UNORM};
        std::vector<DXGI_FORMAT> result;
        for (auto f : passes.ColorFormats())
            result.push_back(detail::DxgiFormat(f));
        return result;
    }
    DXGI_FORMAT ActiveColorFormat() const {
        const auto f = ActiveColorFormats();
        return f.empty() ? DXGI_FORMAT_UNKNOWN : f[0];
    }
    std::vector<unsigned> freeSlots;
    std::vector<UploadJob> pendingUploads;
    std::vector<Retirement> retiring;
    SwapchainDesc desc;
    ModelSupport support;
    ModelPipeline pipeline;
    std::array<Handle, 3> textures{};
    std::array<D3D12_SAMPLER_DESC, 4> samplerDescriptions{};
    HANDLE completionEvent = nullptr;
    HRESULT lastError = S_OK;
    UINT64 identity = detail::NextDeviceIdentity(), epoch = 1, issued = 0;
    unsigned frameIndex = FrameCount - 1, activeBackbuffer = 0, lastBackbuffer = 0, targetWidth = 0,
             targetHeight = 0;
    unsigned viewStride = 0, samplerStride = 0, rtvStride = 0;
    bool inFrame = false, pending = false, retired = false, modelReady = false, presentable = false,
         untracked = false, screenReady = false;
    D3D12Stats stats;
    ~D3D12Device() override {
        if (!inFrame && issued)
            WaitCompletion({identity, issued}, 5000);
        for (auto& f : frames)
            if (f.mapped && f.constants)
                f.constants->Unmap(0, nullptr);
        if (completionEvent)
            CloseHandle(completionEvent);
        if (debugCallback && debugCookie)
            debugCallback->UnregisterMessageCallback(debugCookie);
    }
    bool Live() const {
        return !pending && !retired && native && native->GetDeviceRemovedReason() == S_OK;
    }
    Backend BackendType() const override { return Backend::D3D12; }
    Capabilities Features() const override { return {true, false, true, true}; }
    DeviceStatus Status() const override {
        return {identity, epoch,
                native && native->GetDeviceRemovedReason() != S_OK ? DeviceState::Removed
                : Live()                                           ? DeviceState::Ready
                                                                   : DeviceState::ResetPending};
    }
    ResourceLifecycleCapabilities ResourceLifecycleFeatures() const override {
        return {true, true, true, false, true};
    }
    ModelCapabilities ModelFeatures() const override {
        return {true, true, true, true, true, true, true, false, true, true, bool(chain), MaxDraws};
    }
    D3D12_CPU_DESCRIPTOR_HANDLE CpuView(unsigned n) const {
        auto h = cpuViews->GetCPUDescriptorHandleForHeapStart();
        h.ptr += SIZE_T(n) * viewStride;
        return h;
    }
    D3D12_CPU_DESCRIPTOR_HANDLE Rtv(unsigned n) const {
        auto h = rtvs->GetCPUDescriptorHandleForHeapStart();
        h.ptr += SIZE_T(n) * rtvStride;
        return h;
    }
    D3D12_CPU_DESCRIPTOR_HANDLE Dsv(unsigned n = 0) const {
        auto h = dsvs->GetCPUDescriptorHandleForHeapStart();
        h.ptr +=
            SIZE_T(n) * native->GetDescriptorHandleIncrementSize(D3D12_DESCRIPTOR_HEAP_TYPE_DSV);
        return h;
    }
    Handle CreateRenderTexture(const RenderTextureDesc& d) override {
        if (!Live() || inFrame || !QueryShaderTextureSupport(*this, d))
            return {};
        const bool isDepth = d.format == TextureFormat::Depth24Stencil8;
        const auto format = detail::DxgiFormat(d.format);
        auto desc = Texture(d.width, d.height, format);
        desc.Flags = isDepth ? D3D12_RESOURCE_FLAG_ALLOW_DEPTH_STENCIL
                             : D3D12_RESOURCE_FLAG_ALLOW_RENDER_TARGET;
        D3D12_CLEAR_VALUE clear{};
        clear.Format = format;
        if (isDepth)
            clear.DepthStencil.Depth = 1;
        Resource r;
        r.texture = true;
        r.uploaded = true;
        r.image = {d.width, d.height, d.format};
        r.state = isDepth ? D3D12_RESOURCE_STATE_DEPTH_WRITE : D3D12_RESOURCE_STATE_RENDER_TARGET;
        if (!Allocate(desc, D3D12_HEAP_TYPE_DEFAULT, r.state, r.native, &clear))
            return {};
        const auto h = Insert(std::move(r));
        if (!h)
            return {};
        if (isDepth)
            native->CreateDepthStencilView(Find(h)->native.Get(), nullptr, Dsv(h.index + 1));
        else
            native->CreateRenderTargetView(Find(h)->native.Get(), nullptr,
                                           Rtv(BackbufferCount + 1 + h.index));
        if (d.access & std::uint32_t(TextureAccess::Sampled))
            MakeView(h, 1);
        return passes.Register(h, d) ? h : Handle{};
    }
    bool BeginRenderPass(const RenderPassDesc& p) override {
        if (!Live() || !inFrame || !passes.Begin(p))
            return false;
        auto* list = frames[frameIndex].commands.Get();
        for (auto h : p.sampled) {
            auto& r = *Find(h);
            Barrier(list, r.native.Get(), r.state, D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE);
            r.state = D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE;
        }
        D3D12_CPU_DESCRIPTOR_HANDLE ds{};
        const auto colors = ColorAttachments(p);
        std::vector<D3D12_CPU_DESCRIPTOR_HANDLE> rt;
        for (const auto& a : colors) {
            auto& r = *Find(a.texture);
            Barrier(list, r.native.Get(), r.state, D3D12_RESOURCE_STATE_RENDER_TARGET);
            r.state = D3D12_RESOURCE_STATE_RENDER_TARGET;
            rt.push_back(Rtv(BackbufferCount + 1 + a.texture.index));
        }
        if (p.depthStencil)
            ds = Dsv(p.depthStencil->texture.index + 1);
        list->OMSetRenderTargets(UINT(rt.size()), rt.data(), FALSE, p.depthStencil ? &ds : nullptr);
        for (std::size_t i = 0; i < colors.size(); ++i)
            if (colors[i].load == AttachmentLoad::Clear) {
                const auto& c = colors[i].clear;
                const float clear[] = {c.red, c.green, c.blue, c.alpha};
                list->ClearRenderTargetView(rt[i], clear, 0, nullptr);
            }
        if (p.depthStencil && p.depthStencil->load == AttachmentLoad::Clear)
            list->ClearDepthStencilView(
                ds,
                static_cast<D3D12_CLEAR_FLAGS>(D3D12_CLEAR_FLAG_DEPTH | D3D12_CLEAR_FLAG_STENCIL),
                p.depthStencil->clearDepth, p.depthStencil->clearStencil, 0, nullptr);
        const D3D12_RECT scissor{0, 0, LONG(RenderWidth()), LONG(RenderHeight())};
        list->RSSetScissorRects(1, &scissor);
        modelReady = screenReady = false;
        textures = {};
        return true;
    }
    bool EndRenderPass() override {
        if (!inFrame || !passes.End())
            return false;
        frames[frameIndex].commands->OMSetRenderTargets(0, nullptr, FALSE, nullptr);
        modelReady = screenReady = false;
        textures = {};
        return true;
    }
    bool ReadRenderTexture(Handle h, AttachmentReadback& out) override {
        if (!Live() || inFrame || !passes.CanRead(h))
            return false;
        // Read() copies format-sized texels without color conversion. Native
        // D24S8 is planar in DX12 and deliberately not exposed by this path.
        const auto d = *passes.Description(h);
        if (d.format == TextureFormat::Depth24Stencil8)
            return false;
        RgbaImage bytes;
        auto& r = *Find(h);
        if (!Read(r.native.Get(), r.state, bytes, TexturePixelBytes(d.format)))
            return false;
        out = {d, std::move(bytes.pixels)};
        return true;
    }
    Resource* Find(Handle h) {
        return h && h.device == identity && h.index < resources.size() &&
                       resources[h.index].alive && resources[h.index].generation == h.generation
                   ? &resources[h.index]
                   : nullptr;
    }
    Handle Insert(Resource r) {
        unsigned n;
        if (freeSlots.empty()) {
            if (resources.size() >= MaxResources)
                return {};
            n = static_cast<unsigned>(resources.size());
            resources.push_back(std::move(r));
        } else {
            n = freeSlots.back();
            freeSlots.pop_back();
            r.generation = resources[n].generation;
            resources[n] = std::move(r);
        }
        resources[n].alive = true;
        return {n, resources[n].generation, identity};
    }
    bool Allocate(const D3D12_RESOURCE_DESC& d, D3D12_HEAP_TYPE type, D3D12_RESOURCE_STATES state,
                  ComPtr<ID3D12Resource>& out, const D3D12_CLEAR_VALUE* clear = nullptr) {
        const auto heap = Heap(type);
        lastError = native->CreateCommittedResource(&heap, D3D12_HEAP_FLAG_NONE, &d, state, clear,
                                                    IID_PPV_ARGS(&out));
        if (SUCCEEDED(lastError)) {
            if (d.Dimension == D3D12_RESOURCE_DIMENSION_BUFFER) {
                stats.bufferAllocations += type == D3D12_HEAP_TYPE_DEFAULT;
                stats.uploadBufferAllocations += type == D3D12_HEAP_TYPE_UPLOAD;
            }
            const auto name = std::wstring(type == D3D12_HEAP_TYPE_UPLOAD     ? L"Upload"
                                           : type == D3D12_HEAP_TYPE_READBACK ? L"Readback"
                                                                              : L"Default") +
                              L"_" + std::to_wstring(d.Width) + L"x" + std::to_wstring(d.Height);
            out->SetName(name.c_str());
        }
        return SUCCEEDED(lastError);
    }
    void Barrier(ID3D12GraphicsCommandList* list, ID3D12Resource* r, D3D12_RESOURCE_STATES before,
                 D3D12_RESOURCE_STATES after) {
        if (before == after)
            return;
        D3D12_RESOURCE_BARRIER b{};
        b.Type = D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;
        b.Transition = {r, D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES, before, after};
        list->ResourceBarrier(1, &b);
    }
    void Defer(ComPtr<IUnknown> object, UINT64 point) {
        if (object)
            retiring.push_back({point, std::move(object)});
    }
    bool Collect() {
        if (native->GetDeviceRemovedReason() != S_OK) {
            retiring.clear();
            return true;
        }
        if (untracked)
            return false;
        const auto completed = fence->GetCompletedValue();
        if (completed == UINT64_MAX)
            return false;
        retiring.erase(std::remove_if(retiring.begin(), retiring.end(),
                                      [&](const Retirement& r) { return r.fence <= completed; }),
                       retiring.end());
        return true;
    }
    bool WriteUpload(ID3D12Resource* upload, const void* data, UINT64 bytes) {
        void* mapped = nullptr;
        D3D12_RANGE read{0, 0};
        lastError = upload->Map(0, &read, &mapped);
        if (FAILED(lastError))
            return false;
        if (data)
            std::memcpy(mapped, data, static_cast<std::size_t>(bytes));
        upload->Unmap(0, nullptr);
        stats.uploadBytes += data ? bytes : 0;
        return true;
    }
    bool UploadData(ComPtr<ID3D12Resource>& upload, const void* data, UINT64 bytes) {
        return Allocate(Buffer(bytes), D3D12_HEAP_TYPE_UPLOAD, D3D12_RESOURCE_STATE_GENERIC_READ,
                        upload) &&
               WriteUpload(upload.Get(), data, bytes);
    }
    Handle CreateBuffer(BufferDesc d, const void* initial) override {
        if (!Live() || inFrame || !d.size || d.size > UINT_MAX ||
            (d.usage != BufferUsage::Vertex && d.usage != BufferUsage::Index) ||
            (d.usage == BufferUsage::Index && d.indexFormat != IndexFormat::UInt16 &&
             d.indexFormat != IndexFormat::UInt32))
            return {};
        Resource r;
        r.buffer = d;
        r.uploaded = initial != nullptr;
        if (!Allocate(Buffer(d.size), D3D12_HEAP_TYPE_DEFAULT, D3D12_RESOURCE_STATE_COPY_DEST,
                      r.native))
            return {};
        if (initial && d.usage == BufferUsage::Index)
            r.indices.assign(static_cast<const std::uint8_t*>(initial),
                             static_cast<const std::uint8_t*>(initial) + d.size);
        r.indices.Seal();
        UploadJob job;
        job.destination = r.native;
        job.bytes = d.size;
        job.after = d.usage == BufferUsage::Index ? D3D12_RESOURCE_STATE_INDEX_BUFFER
                                                  : D3D12_RESOURCE_STATE_VERTEX_AND_CONSTANT_BUFFER;
        if (initial && !UploadData(job.source, initial, d.size))
            return {};
        if (initial && d.dynamic) {
            r.dynamicUpload = job.source;
            r.uploadQueued = true;
        }
        r.state = initial ? job.after : D3D12_RESOURCE_STATE_COPY_DEST;
        const auto h = Insert(std::move(r));
        if (!h)
            return {};
        if (initial) {
            if (d.dynamic)
                job.retainedSource = h;
            pendingUploads.push_back(std::move(job));
            ++stats.resourceUploads;
        }
        return h;
    }
    bool Upload(Handle h, std::size_t offset, const void* data, std::size_t bytes) override {
        auto* r = Find(h);
        if (!Live() || inFrame || !r || r->texture || !r->buffer.dynamic || !data || !bytes ||
            offset > r->buffer.size || bytes > r->buffer.size - offset)
            return false;
        UploadJob job;
        job.destination = r->native;
        job.before = r->state;
        job.after = r->buffer.usage == BufferUsage::Index
                        ? D3D12_RESOURCE_STATE_INDEX_BUFFER
                        : D3D12_RESOURCE_STATE_VERTEX_AND_CONSTANT_BUFFER;
        job.offset = offset;
        job.bytes = bytes;
        // Each reusable DEFAULT buffer retains a matching upload allocation.
        // An unfinished copy or another queued update must keep its source
        // immutable; direct busy/partial callers get a separate upload job.
        const auto completed = fence->GetCompletedValue();
        const bool reusable = !offset && bytes == r->buffer.size && !r->uploadQueued &&
                              completed != UINT64_MAX && r->uploadUse <= completed && !untracked;
        if (reusable) {
            if (r->dynamicUpload) {
                if (!WriteUpload(r->dynamicUpload.Get(), data, bytes))
                    return false;
                ++stats.uploadBufferReuses;
            } else if (!UploadData(r->dynamicUpload, data, bytes))
                return false;
            job.source = r->dynamicUpload;
            job.retainedSource = h;
            r->uploadQueued = true;
        } else if (!UploadData(job.source, data, bytes))
            return false;
        if (r->buffer.usage == BufferUsage::Index) {
            if (r->indices.empty())
                r->indices.resize(r->buffer.size);
            std::memcpy(r->indices.data() + offset, data, bytes);
            r->indices.Seal();
        }
        pendingUploads.push_back(std::move(job));
        r->state = pendingUploads.back().after;
        r->uploaded = true;
        ++stats.resourceUploads;
        return true;
    }
    Handle CreateTexture(TextureDesc d) override {
        if (!Live() || inFrame || d.format != TextureFormat::Rgba8 ||
            d.mips != TextureDesc::Mips::SingleLevel || !d.width || !d.height || d.width > 16384 ||
            d.height > 16384)
            return {};
        Resource r;
        r.texture = true;
        r.image = d;
        if (!Allocate(Texture(d.width, d.height, DXGI_FORMAT_R8G8B8A8_UNORM),
                      D3D12_HEAP_TYPE_DEFAULT, D3D12_RESOURCE_STATE_COPY_DEST, r.native))
            return {};
        const auto h = Insert(std::move(r));
        if (h)
            MakeView(h, 1);
        return h;
    }
    void MakeView(Handle h, unsigned levels) {
        auto* r = Find(h);
        D3D12_SHADER_RESOURCE_VIEW_DESC v{};
        v.Format = r->srgb ? DXGI_FORMAT_R8G8B8A8_UNORM_SRGB : detail::DxgiFormat(r->image.format);
        v.ViewDimension = D3D12_SRV_DIMENSION_TEXTURE2D;
        v.Shader4ComponentMapping = D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING;
        v.Texture2D.MipLevels = levels;
        native->CreateShaderResourceView(r->native.Get(), &v, CpuView(h.index));
    }
    bool QueueTexture(Resource& r, const std::vector<RgbaImage>& levels) {
        UploadJob job;
        job.destination = r.native;
        job.before = r.state;
        job.footprints.resize(levels.size());
        std::vector<UINT> rows(levels.size());
        std::vector<UINT64> lengths(levels.size());
        UINT64 total = 0;
        const auto d = r.native->GetDesc();
        native->GetCopyableFootprints(&d, 0, static_cast<UINT>(levels.size()), 0,
                                      job.footprints.data(), rows.data(), lengths.data(), &total);
        if (!Allocate(Buffer(total), D3D12_HEAP_TYPE_UPLOAD, D3D12_RESOURCE_STATE_GENERIC_READ,
                      job.source))
            return false;
        void* mapped = nullptr;
        D3D12_RANGE read{0, 0};
        lastError = job.source->Map(0, &read, &mapped);
        if (FAILED(lastError))
            return false;
        for (unsigned i = 0; i < levels.size(); ++i)
            for (unsigned y = 0; y < levels[i].height; ++y)
                std::memcpy(static_cast<std::uint8_t*>(mapped) + job.footprints[i].Offset +
                                SIZE_T(y) * job.footprints[i].Footprint.RowPitch,
                            levels[i].pixels.data() + SIZE_T(y) * levels[i].width * 4,
                            SIZE_T(levels[i].width) * 4);
        job.source->Unmap(0, nullptr);
        pendingUploads.push_back(std::move(job));
        r.state = D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE;
        r.uploaded = true;
        ++stats.resourceUploads;
        return true;
    }
    Handle CreateModelTexture(const ModelTextureData& data) override {
        // Explicit mip contents are portable. API-specific automatic filtering
        // has no equivalence contract and is deliberately not accepted here.
        if (!Live() || inFrame || data.generateMips || data.levels.empty() ||
            data.levels.size() > 15)
            return {};
        unsigned w = data.levels[0].width, h = data.levels[0].height;
        if (!w || !h || w > 16384 || h > 16384)
            return {};
        for (unsigned i = 0; i < data.levels.size(); ++i) {
            const auto& l = data.levels[i];
            if (l.width != w || l.height != h || l.pixels.size() != UINT64(w) * h * 4 ||
                (w == 1 && h == 1 && i + 1 < data.levels.size()))
                return {};
            w = std::max(1u, w / 2);
            h = std::max(1u, h / 2);
        }
        const auto& top = data.levels[0];
        Resource r;
        r.texture = true;
        r.srgb = data.srgb;
        r.image = {top.width, top.height};
        if (!Allocate(
                Texture(top.width, top.height,
                        data.srgb ? DXGI_FORMAT_R8G8B8A8_UNORM_SRGB : DXGI_FORMAT_R8G8B8A8_UNORM,
                        static_cast<unsigned>(data.levels.size())),
                D3D12_HEAP_TYPE_DEFAULT, D3D12_RESOURCE_STATE_COPY_DEST, r.native))
            return {};
        const auto handle = Insert(std::move(r));
        if (!handle)
            return {};
        if (!QueueTexture(*Find(handle), data.levels)) {
            Destroy(handle);
            return {};
        }
        MakeView(handle, static_cast<unsigned>(data.levels.size()));
        return handle;
    }
    bool UploadTexture(Handle h, TexturePixels pixels) override {
        auto* r = Find(h);
        if (!Live() || inFrame || !r || !r->texture || r->native->GetDesc().MipLevels != 1 ||
            !pixels.data || pixels.rowPitch < SIZE_T(r->image.width) * 4 ||
            pixels.bytes <
                SIZE_T(r->image.height - 1) * pixels.rowPitch + SIZE_T(r->image.width) * 4)
            return false;
        RgbaImage image{r->image.width, r->image.height, {}};
        image.pixels.resize(SIZE_T(image.width) * image.height * 4);
        for (unsigned y = 0; y < image.height; ++y)
            std::memcpy(image.pixels.data() + SIZE_T(y) * image.width * 4,
                        static_cast<const std::uint8_t*>(pixels.data) + SIZE_T(y) * pixels.rowPitch,
                        SIZE_T(image.width) * 4);
        return QueueTexture(*r, {std::move(image)});
    }
    bool Destroy(Handle h) override {
        auto* r = Find(h);
        if (inFrame || !r)
            return false;
        pendingUploads.erase(std::remove_if(pendingUploads.begin(), pendingUploads.end(),
                                            [&](const UploadJob& job) {
                                                return job.destination.Get() == r->native.Get();
                                            }),
                             pendingUploads.end());
        if (passes.Description(h))
            passes.Erase(h);
        Defer(r->native, issued + (inFrame ? 1 : 0));
        Defer(r->dynamicUpload, r->uploadUse);
        r->dynamicUpload.Reset();
        r->native.Reset();
        r->indices.clear();
        r->alive = false;
        if (++r->generation == 0)
            ++r->generation;
        freeSlots.push_back(h.index);
        Collect();
        return true;
    }
    CompletionState PollCompletion(CompletionToken t) override {
        if (t.device != identity || !t.value || t.value > issued)
            return CompletionState::Invalid;
        const auto value = fence->GetCompletedValue();
        if (value == UINT64_MAX || native->GetDeviceRemovedReason() != S_OK)
            return CompletionState::DeviceLost;
        Collect();
        return value >= t.value ? CompletionState::Complete : CompletionState::Pending;
    }
    bool WaitCompletion(CompletionToken t, std::uint32_t timeout) override {
        const auto state = PollCompletion(t);
        if (state == CompletionState::Complete)
            return true;
        if (state != CompletionState::Pending)
            return false;
        lastError = fence->SetEventOnCompletion(t.value, completionEvent);
        if (FAILED(lastError))
            return false;
        return WaitForSingleObject(completionEvent, timeout) == WAIT_OBJECT_0 &&
               PollCompletion(t) == CompletionState::Complete;
    }
    bool SignalQueue() {
        lastError = queue->Signal(fence.Get(), issued + 1);
        if (FAILED(lastError)) {
            untracked = true;
            return false;
        }
        ++issued;
        untracked = false;
        return true;
    }
    bool WaitIdle() { return SignalQueue() && WaitCompletion({identity, issued}, 5000); }
    CompletionToken SignalCompletion() override {
        return !inFrame && !untracked && issued ? CompletionToken{identity, issued}
                                                : CompletionToken{};
    }
    bool InitializeFrames() {
        for (auto& f : frames) {
            lastError = native->CreateCommandAllocator(D3D12_COMMAND_LIST_TYPE_DIRECT,
                                                       IID_PPV_ARGS(&f.allocator));
            if (FAILED(lastError))
                return false;
            lastError =
                native->CreateCommandList(0, D3D12_COMMAND_LIST_TYPE_DIRECT, f.allocator.Get(),
                                          nullptr, IID_PPV_ARGS(&f.commands));
            if (FAILED(lastError))
                return false;
            lastError = f.commands->Close();
            if (FAILED(lastError))
                return false;
            D3D12_DESCRIPTOR_HEAP_DESC d{};
            d.Type = D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV;
            d.NumDescriptors = DescriptorCount;
            d.Flags = D3D12_DESCRIPTOR_HEAP_FLAG_SHADER_VISIBLE;
            lastError = native->CreateDescriptorHeap(&d, IID_PPV_ARGS(&f.views));
            if (FAILED(lastError))
                return false;
            d.Type = D3D12_DESCRIPTOR_HEAP_TYPE_SAMPLER;
            lastError = native->CreateDescriptorHeap(&d, IID_PPV_ARGS(&f.samplers));
            if (FAILED(lastError))
                return false;
            if (!Allocate(Buffer(ConstantBytes), D3D12_HEAP_TYPE_UPLOAD,
                          D3D12_RESOURCE_STATE_GENERIC_READ, f.constants))
                return false;
            D3D12_RANGE read{0, 0};
            lastError = f.constants->Map(0, &read, reinterpret_cast<void**>(&f.mapped));
            if (FAILED(lastError))
                return false;
        }
        return true;
    }
    bool MakeTargets(bool model, unsigned width, unsigned height, SurfaceDepth format) {
        offscreen.Reset();
        depth.Reset();
        for (auto& b : backbuffers)
            b.Reset();
        modelReady = presentable = false;
        for (unsigned i = 0; chain && i < BackbufferCount; ++i) {
            lastError = chain->GetBuffer(i, IID_PPV_ARGS(&backbuffers[i]));
            if (FAILED(lastError))
                return false;
            native->CreateRenderTargetView(backbuffers[i].Get(), nullptr, Rtv(i));
        }
        if (model) {
            auto d = Texture(width, height, DXGI_FORMAT_R8G8B8A8_UNORM);
            d.Flags = D3D12_RESOURCE_FLAG_ALLOW_RENDER_TARGET;
            D3D12_CLEAR_VALUE clear{};
            clear.Format = d.Format;
            clear.Color[3] = 1;
            if (!Allocate(d, D3D12_HEAP_TYPE_DEFAULT, D3D12_RESOURCE_STATE_RENDER_TARGET, offscreen,
                          &clear))
                return false;
            native->CreateRenderTargetView(offscreen.Get(), nullptr, Rtv(BackbufferCount));
        }
        if (format != SurfaceDepth::None) {
            auto d = Texture(width, height, Depth(format));
            d.Flags = D3D12_RESOURCE_FLAG_ALLOW_DEPTH_STENCIL;
            D3D12_CLEAR_VALUE clear{};
            clear.Format = d.Format;
            clear.DepthStencil.Depth = 1;
            if (!Allocate(d, D3D12_HEAP_TYPE_DEFAULT, D3D12_RESOURCE_STATE_DEPTH_WRITE, depth,
                          &clear))
                return false;
            native->CreateDepthStencilView(depth.Get(), nullptr, Dsv());
        }
        targetWidth = width;
        targetHeight = height;
        return true;
    }
    bool PrepareForReset() override {
        if (inFrame || retired || pending)
            return false;
        pending = true;
        passes.InvalidateContents();
        presentable = modelReady = false;
        textures = {};
        // Present can enqueue work after the frame fence. Establish a new
        // queue boundary before releasing any DXGI-owned backbuffer.
        if (!WaitIdle())
            return false;
        for (auto& f : frames)
            f.uploads.clear();
        offscreen.Reset();
        depth.Reset();
        for (auto& b : backbuffers)
            b.Reset();
        return true;
    }
    bool RestoreAfterReset() override {
        if (!pending || retired || (!backbuffers[0] && !offscreen) ||
            native->GetDeviceRemovedReason() != S_OK)
            return false;
        pending = false;
        if (++epoch == 0)
            ++epoch;
        return true;
    }
    bool EnsureModelTarget(const HistoryDesc& d) override {
        if (!ValidHistoryDesc(d) || d.samples != 1 ||
            d.representation != HistoryRepresentation::SingleSample || !d.color || !d.depth ||
            !d.stencil || d.width > 4096 || d.height > 4096 || inFrame || !Live())
            return false;
        if (offscreen && d.width == targetWidth && d.height == targetHeight)
            return true;
        return PrepareForReset() &&
               MakeTargets(true, d.width, d.height, SurfaceDepth::Depth24Stencil8) &&
               RestoreAfterReset();
    }
    bool EnsurePresentationTarget(const HistoryDesc& d) override {
        if (!ValidHistoryDesc(d) || d.samples != 1 ||
            d.representation != HistoryRepresentation::SingleSample || !d.color || !d.depth ||
            !d.stencil || d.width != desc.width || d.height != desc.height ||
            desc.depth != SurfaceDepth::Depth24Stencil8 || !Live() || inFrame)
            return false;
        if (!offscreen && backbuffers[0])
            return true;
        return PrepareForReset() && MakeTargets(false, desc.width, desc.height, desc.depth) &&
               RestoreAfterReset();
    }
    bool BeginFrame(ClearColor color) override {
        if (!Live() || inFrame || (!backbuffers[0] && !offscreen) ||
            (!offscreen && IsIconic(reinterpret_cast<HWND>(desc.window.handle))))
            return false;
        for (float c : {color.red, color.green, color.blue, color.alpha})
            if (!std::isfinite(c) || c < 0 || c > 1)
                return false;
        const auto index = (frameIndex + 1) % FrameCount;
        auto& f = frames[index];
        if (f.fence && !WaitCompletion({identity, f.fence}, 5000))
            return false;
        f.uploads.clear();
        f.constantOffset = 0;
        f.drawCount = 0;
        lastError = f.allocator->Reset();
        if (SUCCEEDED(lastError))
            lastError = f.commands->Reset(f.allocator.Get(), nullptr);
        if (FAILED(lastError))
            return false;
        frameIndex = index;
        auto* list = f.commands.Get();
        for (auto& job : pendingUploads) {
            Barrier(list, job.destination.Get(), job.before, D3D12_RESOURCE_STATE_COPY_DEST);
            if (job.footprints.empty())
                list->CopyBufferRegion(job.destination.Get(), job.offset, job.source.Get(), 0,
                                       job.bytes);
            else
                for (unsigned i = 0; i < job.footprints.size(); ++i) {
                    D3D12_TEXTURE_COPY_LOCATION dst{}, src{};
                    dst.pResource = job.destination.Get();
                    dst.Type = D3D12_TEXTURE_COPY_TYPE_SUBRESOURCE_INDEX;
                    dst.SubresourceIndex = i;
                    src.pResource = job.source.Get();
                    src.Type = D3D12_TEXTURE_COPY_TYPE_PLACED_FOOTPRINT;
                    src.PlacedFootprint = job.footprints[i];
                    list->CopyTextureRegion(&dst, 0, 0, 0, &src, nullptr);
                }
            Barrier(list, job.destination.Get(), D3D12_RESOURCE_STATE_COPY_DEST, job.after);
            if (auto* r = Find(job.retainedSource)) {
                r->uploadQueued = false;
                r->uploadUse = issued + 1;
            }
            f.uploads.push_back(std::move(job.source));
            f.uploads.push_back(std::move(job.destination));
        }
        pendingUploads.clear();
        activeBackbuffer = chain ? chain->GetCurrentBackBufferIndex() : 0;
        if (!offscreen)
            Barrier(list, backbuffers[activeBackbuffer].Get(), D3D12_RESOURCE_STATE_PRESENT,
                    D3D12_RESOURCE_STATE_RENDER_TARGET);
        auto rtv = Rtv(offscreen ? BackbufferCount : activeBackbuffer);
        auto dsv = Dsv();
        list->OMSetRenderTargets(1, &rtv, FALSE, depth ? &dsv : nullptr);
        const float clear[] = {color.red, color.green, color.blue, color.alpha};
        list->ClearRenderTargetView(rtv, clear, 0, nullptr);
        if (depth)
            list->ClearDepthStencilView(
                dsv,
                static_cast<D3D12_CLEAR_FLAGS>(
                    D3D12_CLEAR_FLAG_DEPTH |
                    (depth->GetDesc().Format == DXGI_FORMAT_D24_UNORM_S8_UINT
                         ? D3D12_CLEAR_FLAG_STENCIL
                         : 0)),
                1, 0, 0, nullptr);
        ID3D12DescriptorHeap* heaps[] = {f.views.Get(), f.samplers.Get()};
        list->SetDescriptorHeaps(2, heaps);
        list->SetGraphicsRootSignature(root.Get());
        const D3D12_RECT rect{0, 0, static_cast<LONG>(targetWidth),
                              static_cast<LONG>(targetHeight)};
        list->RSSetScissorRects(1, &rect);
        inFrame = true;
        presentable = modelReady = screenReady = false;
        textures = {};
        return true;
    }
    bool CopyToFrameOutput(Handle h) override {
        auto* r = Find(h);
        const auto* t = passes.Description(h);
        if (!Live() || !inFrame || !r || !t || t->format != TextureFormat::Rgba8 ||
            t->width != targetWidth || t->height != targetHeight || !passes.CanRead(h))
            return false;
        auto* output = offscreen ? offscreen.Get() : backbuffers[activeBackbuffer].Get();
        if (!output)
            return false;
        auto* cmd = frames[frameIndex].commands.Get();
        Barrier(cmd, r->native.Get(), r->state, D3D12_RESOURCE_STATE_COPY_SOURCE);
        Barrier(cmd, output, D3D12_RESOURCE_STATE_RENDER_TARGET, D3D12_RESOURCE_STATE_COPY_DEST);
        cmd->CopyResource(output, r->native.Get());
        Barrier(cmd, r->native.Get(), D3D12_RESOURCE_STATE_COPY_SOURCE, r->state);
        Barrier(cmd, output, D3D12_RESOURCE_STATE_COPY_DEST, D3D12_RESOURCE_STATE_RENDER_TARGET);
        modelReady = false;
        return true;
    }
    bool EndFrame() override {
        if (!inFrame || passes.Active())
            return false;
        auto& f = frames[frameIndex];
        if (!offscreen)
            Barrier(f.commands.Get(), backbuffers[activeBackbuffer].Get(),
                    D3D12_RESOURCE_STATE_RENDER_TARGET, D3D12_RESOURCE_STATE_PRESENT);
        lastError = f.commands->Close();
        inFrame = modelReady = false;
        if (FAILED(lastError)) {
            pending = true;
            return false;
        }
        ID3D12CommandList* lists[] = {f.commands.Get()};
        queue->ExecuteCommandLists(1, lists);
        if (!SignalQueue()) {
            pending = true;
            return false;
        }
        f.fence = issued;
        lastBackbuffer = activeBackbuffer;
        presentable = true;
        ++stats.submissions;
        return native->GetDeviceRemovedReason() == S_OK;
    }
    bool Present() override {
        if (!Live() || offscreen || inFrame || !presentable ||
            IsIconic(reinterpret_cast<HWND>(desc.window.handle)))
            return false;
        lastError = chain->Present(desc.present == PresentMode::VSync ? 1 : 0, 0);
        presentable = false;
        if (SUCCEEDED(lastError))
            SignalQueue();
        if (FAILED(lastError))
            pending = true;
        return SUCCEEDED(lastError);
    }
    bool SetFixedPipeline(FixedPipelineState) override;
    bool BindTexture(std::uint32_t slot, Handle h) override {
        if (genericReady)
            return false;
        auto* r = Find(h);
        if (!inFrame || !modelReady || slot >= 3 || (screenReady && slot) || !r || !r->texture ||
            (passes.Description(h) ? !passes.CanSample(h) : !r->uploaded) ||
            r->srgb != pipeline.shader.srgbSample[slot])
            return false;
        textures[slot] = h;
        return true;
    }
    D3D12_GPU_VIRTUAL_ADDRESS Constants(const void* data, UINT64 bytes) {
        auto& f = frames[frameIndex];
        const auto allocated = (bytes + 255) & ~UINT64(255);
        if (f.constantOffset + allocated > ConstantBytes)
            return 0;
        std::memcpy(f.mapped + f.constantOffset, data, static_cast<std::size_t>(bytes));
        const auto address = f.constants->GetGPUVirtualAddress() + f.constantOffset;
        f.constantOffset += allocated;
        return address;
    }
    bool MakeShaders() {
        const bool measured = support.selected == ModelCompatibility::Dx9Measured;
        const std::string source =
            measured ? std::string(modelDx9CompatibilitySource) + modelVertexSource
                     : std::string(modelVertexSource);
        ComPtr<ID3DBlob> errors;
        const D3D_SHADER_MACRO vs[] = {
            {"MDS_MODEL_DX9_COMPATIBILITY", measured ? "1" : "0"},
            {"MDS_MODEL_TEXTURE_FMA_DIAGNOSTIC", support.diagnosticTextureFma ? "1" : "0"},
            {"MDS_MODEL_SPECULAR_FMA_DIAGNOSTIC", support.diagnosticSpecularFma ? "1" : "0"},
            {nullptr, nullptr}};
        const D3D_SHADER_MACRO cvs[] = {
            {"MDS_MODEL_VERTEX_COLOR", "1"},
            {"MDS_MODEL_DX9_COMPATIBILITY", measured ? "1" : "0"},
            {"MDS_MODEL_TEXTURE_FMA_DIAGNOSTIC", support.diagnosticTextureFma ? "1" : "0"},
            {"MDS_MODEL_SPECULAR_FMA_DIAGNOSTIC", support.diagnosticSpecularFma ? "1" : "0"},
            {nullptr, nullptr}};
        const D3D_SHADER_MACRO ps[] = {
            {"MDS_MODEL_SPECULAR", "1"},
            {"MDS_MODEL_DX9_COMPATIBILITY", measured ? "1" : "0"},
            {"MDS_MODEL_ALPHA12_DIAGNOSTIC", support.diagnosticAlpha12 ? "1" : "0"},
            {nullptr, nullptr}};
        lastError = D3DCompile(source.data(), source.size(), nullptr, vs, nullptr, "main", "vs_5_0",
                               D3DCOMPILE_IEEE_STRICTNESS, 0, &vertex, &errors);
        if (SUCCEEDED(lastError))
            lastError = D3DCompile(source.data(), source.size(), nullptr, cvs, nullptr, "main",
                                   "vs_5_0", D3DCOMPILE_IEEE_STRICTNESS, 0, &colorVertex, &errors);
        if (SUCCEEDED(lastError))
            lastError =
                D3DCompile(modelPixelSource, std::strlen(modelPixelSource), nullptr, ps, nullptr,
                           "main", "ps_5_0", D3DCOMPILE_IEEE_STRICTNESS, 0, &pixel, &errors);
        if (SUCCEEDED(lastError))
            lastError = D3DCompile(fixedScreenSource, std::strlen(fixedScreenSource), nullptr,
                                   nullptr, nullptr, "vs", "vs_4_0", 0, 0, &screenVertex, &errors);
        if (SUCCEEDED(lastError))
            lastError = D3DCompile(fixedScreenSource, std::strlen(fixedScreenSource), nullptr,
                                   nullptr, nullptr, "ps", "ps_4_0", 0, 0, &screenPixel, &errors);
        if (SUCCEEDED(lastError))
            lastError = D3DCompile(builtinShadowSource, std::strlen(builtinShadowSource), nullptr,
                                   nullptr, nullptr, "shadow_vs", "vs_5_0",
                                   D3DCOMPILE_IEEE_STRICTNESS, 0, &shadowVertex, &errors);
        if (SUCCEEDED(lastError))
            lastError = D3DCompile(builtinShadowSource, std::strlen(builtinShadowSource), nullptr,
                                   nullptr, nullptr, "shadow_outline_vs", "vs_5_0",
                                   D3DCOMPILE_IEEE_STRICTNESS, 0, &shadowOutlineVertex, &errors);
        if (SUCCEEDED(lastError))
            lastError = D3DCompile(builtinShadowSource, std::strlen(builtinShadowSource), nullptr,
                                   nullptr, nullptr, "shadow_ps", "ps_5_0",
                                   D3DCOMPILE_IEEE_STRICTNESS, 0, &shadowPixel, &errors);
        if (SUCCEEDED(lastError))
            lastError = D3DCompile(postProcessSource, std::strlen(postProcessSource), nullptr,
                                   nullptr, nullptr, "post_vs", "vs_5_0",
                                   D3DCOMPILE_IEEE_STRICTNESS, 0, &postVertex, &errors);
        if (SUCCEEDED(lastError))
            lastError = D3DCompile(postProcessSource, std::strlen(postProcessSource), nullptr,
                                   nullptr, nullptr, "post_ps", "ps_5_0",
                                   D3DCOMPILE_IEEE_STRICTNESS, 0, &postPixel, &errors);
        if (FAILED(lastError)) {
            if (errors)
                std::fprintf(stderr, "builtin shader: %s\n",
                             static_cast<const char*>(errors->GetBufferPointer()));
            return false;
        }
        D3D12_DESCRIPTOR_RANGE ranges[2]{};
        ranges[0] = {D3D12_DESCRIPTOR_RANGE_TYPE_SRV, 4, 0, 0, 0};
        ranges[1] = {D3D12_DESCRIPTOR_RANGE_TYPE_SAMPLER, 4, 0, 0, 0};
        D3D12_ROOT_PARAMETER parameters[6]{};
        for (unsigned i = 0; i < 3; ++i) {
            parameters[i].ParameterType = D3D12_ROOT_PARAMETER_TYPE_CBV;
            parameters[i].Descriptor.ShaderRegister = i == 2 ? 1 : 0;
            parameters[i].ShaderVisibility =
                i == 1 ? D3D12_SHADER_VISIBILITY_PIXEL : D3D12_SHADER_VISIBILITY_VERTEX;
        }
        for (unsigned i = 0; i < 2; ++i) {
            parameters[i + 3].ParameterType = D3D12_ROOT_PARAMETER_TYPE_DESCRIPTOR_TABLE;
            parameters[i + 3].DescriptorTable = {1, &ranges[i]};
            parameters[i + 3].ShaderVisibility = D3D12_SHADER_VISIBILITY_PIXEL;
        }
        parameters[5].ParameterType = D3D12_ROOT_PARAMETER_TYPE_CBV;
        parameters[5].Descriptor.ShaderRegister = 2;
        parameters[5].ShaderVisibility = D3D12_SHADER_VISIBILITY_ALL;
        D3D12_ROOT_SIGNATURE_DESC signature{};
        signature.NumParameters = 6;
        signature.pParameters = parameters;
        signature.Flags = D3D12_ROOT_SIGNATURE_FLAG_ALLOW_INPUT_ASSEMBLER_INPUT_LAYOUT;
        ComPtr<ID3DBlob> blob;
        lastError =
            D3D12SerializeRootSignature(&signature, D3D_ROOT_SIGNATURE_VERSION_1, &blob, &errors);
        if (SUCCEEDED(lastError))
            lastError = native->CreateRootSignature(0, blob->GetBufferPointer(),
                                                    blob->GetBufferSize(), IID_PPV_ARGS(&root));
        return SUCCEEDED(lastError);
    }
    bool BindShadowTexture(Handle h) override {
        const auto* d = passes.Description(h);
        if (!Live() || !inFrame || !modelReady ||
            pipeline.shadow.mode != BuiltinShadowMode::Material || !passes.CanSample(h) || !d ||
            d->format != TextureFormat::R32Float || !Find(h))
            return false;
        shadowTexture = h;
        return true;
    }
    bool SetModelPipeline(const ModelPipeline& p) override;
    bool UsePipeline(const ModelPipeline&, VertexLayout, ID3DBlob*, ID3DBlob*,
                     const ShaderProgram* = nullptr);
    bool BindDrawDescriptors() {
        if (genericReady)
            return BindGenericDescriptors();
        auto& f = frames[frameIndex];
        if (f.drawCount >= MaxDraws)
            return false;
        const unsigned offset = f.drawCount * 4;
        auto views = f.views->GetCPUDescriptorHandleForHeapStart(),
             samplers = f.samplers->GetCPUDescriptorHandleForHeapStart();
        views.ptr += SIZE_T(offset) * viewStride;
        samplers.ptr += SIZE_T(offset) * samplerStride;
        for (unsigned i = 0; i < 4; ++i) {
            const bool shadow = i == 3;
            if (shadow ? pipeline.shadow.mode != BuiltinShadowMode::Material
                       : pipeline.shader.layout.stages[i].role == MaterialTextureRole::None) {
                D3D12_SHADER_RESOURCE_VIEW_DESC d{};
                d.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
                d.ViewDimension = D3D12_SRV_DIMENSION_TEXTURE2D;
                d.Shader4ComponentMapping = D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING;
                d.Texture2D.MipLevels = 1;
                native->CreateShaderResourceView(nullptr, &d, views);
            } else {
                const auto h = shadow ? shadowTexture : textures[i];
                auto* r = Find(h);
                if (!r || !r->uploaded || !r->texture ||
                    (shadow ? !passes.CanSample(h) : r->srgb != pipeline.shader.srgbSample[i]))
                    return false;
                native->CopyDescriptorsSimple(1, views, CpuView(h.index),
                                              D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV);
            }
            native->CreateSampler(&samplerDescriptions[i], samplers);
            views.ptr += viewStride;
            samplers.ptr += samplerStride;
        }
        auto gpuViews = f.views->GetGPUDescriptorHandleForHeapStart(),
             gpuSamplers = f.samplers->GetGPUDescriptorHandleForHeapStart();
        gpuViews.ptr += UINT64(offset) * viewStride;
        gpuSamplers.ptr += UINT64(offset) * samplerStride;
        f.commands->SetGraphicsRootDescriptorTable(3, gpuViews);
        f.commands->SetGraphicsRootDescriptorTable(4, gpuSamplers);
        ++f.drawCount;
        return true;
    }
    bool Draw(NonIndexedDraw d) override {
        auto* v = Find(d.vertices);
        const auto stride = screenReady ? 28u : ModelVertexStride(pipeline.vertexLayout),
                   arity = ModelPrimitiveArity(pipeline.topology);
        if (!Live() || !inFrame || !modelReady || !IsCurrentSubmission(Status(), d.submission) ||
            !v || v->texture || !v->uploaded || v->buffer.usage != BufferUsage::Vertex ||
            (d.layout && *d.layout != (screenReady ? VertexLayout::ScreenPositionColorUv
                                                   : pipeline.vertexLayout)) ||
            !stride || !arity || !d.vertexCount || d.vertexCount % arity ||
            d.firstVertex > v->buffer.size / stride ||
            d.vertexCount > v->buffer.size / stride - d.firstVertex)
            return false;
        if (!BindDrawDescriptors())
            return false;
        D3D12_VERTEX_BUFFER_VIEW vb{v->native->GetGPUVirtualAddress(),
                                    static_cast<UINT>(v->buffer.size), stride};
        auto* list = frames[frameIndex].commands.Get();
        list->IASetVertexBuffers(0, 1, &vb);
        list->DrawInstanced(d.vertexCount, 1, d.firstVertex, 0);
        return true;
    }
    bool DrawIndexed(IndexedDraw d) override {
        auto* v = Find(d.vertices);
        auto* i = Find(d.indices);
        const auto stride = ModelVertexStride(d.layout),
                   arity = ModelPrimitiveArity(pipeline.topology);
        if (!Live() || !inFrame || !modelReady || screenReady ||
            !IsCurrentSubmission(Status(), d.submission) || !v || !i || v->texture || i->texture ||
            !v->uploaded || !i->uploaded || v->buffer.usage != BufferUsage::Vertex ||
            i->buffer.usage != BufferUsage::Index || d.layout != pipeline.vertexLayout || !stride ||
            !arity || !d.indexCount || d.indexCount % arity || !d.vertexCount ||
            d.vertexCount > v->buffer.size / stride)
            return false;
        const auto step = i->buffer.indexFormat == IndexFormat::UInt16 ? 2u : 4u;
        if (d.firstIndex > i->indices.size() / step ||
            d.indexCount > i->indices.size() / step - d.firstIndex)
            return false;
        if (!i->indices.IndicesFit(d.firstIndex, d.indexCount, step, d.vertexCount))
            return false;
        if (!BindDrawDescriptors())
            return false;
        D3D12_VERTEX_BUFFER_VIEW vb{v->native->GetGPUVirtualAddress(),
                                    static_cast<UINT>(v->buffer.size), stride};
        D3D12_INDEX_BUFFER_VIEW ib{i->native->GetGPUVirtualAddress(),
                                   static_cast<UINT>(i->buffer.size),
                                   step == 2 ? DXGI_FORMAT_R16_UINT : DXGI_FORMAT_R32_UINT};
        auto* list = frames[frameIndex].commands.Get();
        list->IASetVertexBuffers(0, 1, &vb);
        list->IASetIndexBuffer(&ib);
        list->DrawIndexedInstanced(d.indexCount, 1, d.firstIndex, 0, 0);
        return true;
    }
    bool Read(ID3D12Resource* image, D3D12_RESOURCE_STATES state, RgbaImage& out,
              unsigned pixelBytes = 4) {
        out = {};
        if (!Live() || inFrame || !image)
            return false;
        const auto d = image->GetDesc();
        D3D12_PLACED_SUBRESOURCE_FOOTPRINT footprint{};
        UINT rows = 0;
        UINT64 rowBytes = 0, total = 0;
        native->GetCopyableFootprints(&d, 0, 1, 0, &footprint, &rows, &rowBytes, &total);
        ComPtr<ID3D12Resource> cpu;
        if (!Allocate(Buffer(total), D3D12_HEAP_TYPE_READBACK, D3D12_RESOURCE_STATE_COPY_DEST, cpu))
            return false;
        ComPtr<ID3D12CommandAllocator> allocator;
        ComPtr<ID3D12GraphicsCommandList> list;
        lastError = native->CreateCommandAllocator(D3D12_COMMAND_LIST_TYPE_DIRECT,
                                                   IID_PPV_ARGS(&allocator));
        if (SUCCEEDED(lastError))
            lastError = native->CreateCommandList(0, D3D12_COMMAND_LIST_TYPE_DIRECT,
                                                  allocator.Get(), nullptr, IID_PPV_ARGS(&list));
        if (FAILED(lastError))
            return false;
        Barrier(list.Get(), image, state, D3D12_RESOURCE_STATE_COPY_SOURCE);
        D3D12_TEXTURE_COPY_LOCATION source{}, target{};
        source.pResource = image;
        source.Type = D3D12_TEXTURE_COPY_TYPE_SUBRESOURCE_INDEX;
        target.pResource = cpu.Get();
        target.Type = D3D12_TEXTURE_COPY_TYPE_PLACED_FOOTPRINT;
        target.PlacedFootprint = footprint;
        list->CopyTextureRegion(&target, 0, 0, 0, &source, nullptr);
        Barrier(list.Get(), image, D3D12_RESOURCE_STATE_COPY_SOURCE, state);
        lastError = list->Close();
        if (FAILED(lastError))
            return false;
        ID3D12CommandList* lists[] = {list.Get()};
        queue->ExecuteCommandLists(1, lists);
        if (!SignalQueue()) {
            pending = true;
            Defer(cpu, issued + 1);
            Defer(allocator, issued + 1);
            Defer(list, issued + 1);
            return false;
        }
        if (!WaitCompletion({identity, issued}, 5000)) {
            Defer(cpu, issued);
            Defer(allocator, issued);
            Defer(list, issued);
            return false;
        }
        void* mapped = nullptr;
        D3D12_RANGE read{0, static_cast<SIZE_T>(total)};
        lastError = cpu->Map(0, &read, &mapped);
        if (FAILED(lastError))
            return false;
        out.width = static_cast<unsigned>(d.Width);
        out.height = d.Height;
        out.pixels.resize(SIZE_T(out.width) * out.height * pixelBytes);
        for (unsigned y = 0; y < out.height; ++y)
            std::memcpy(out.pixels.data() + SIZE_T(y) * out.width * pixelBytes,
                        static_cast<const std::uint8_t*>(mapped) + footprint.Offset +
                            SIZE_T(y) * footprint.Footprint.RowPitch,
                        SIZE_T(out.width) * pixelBytes);
        D3D12_RANGE written{0, 0};
        cpu->Unmap(0, &written);
        return true;
    }
    bool ReadModelTarget(RgbaImage& out) override {
        return Read(offscreen.Get(), D3D12_RESOURCE_STATE_RENDER_TARGET, out);
    }
};

bool D3D12Device::UsePipeline(const ModelPipeline& p, VertexLayout layout, ID3DBlob* vs,
                              ID3DBlob* pixel, const ShaderProgram* generic) {
    D3D12_GRAPHICS_PIPELINE_STATE_DESC d;
    std::memset(&d, 0, sizeof(d));
    d.pRootSignature = generic ? genericRoot.Get() : root.Get();
    d.VS = {vs->GetBufferPointer(), vs->GetBufferSize()};
    d.PS = {pixel->GetBufferPointer(), pixel->GetBufferSize()};
    auto& b = d.BlendState.RenderTarget[0];
    b.BlendEnable = p.raster.alphaBlend;
    b.SrcBlend = b.SrcBlendAlpha = Blend(p.raster.sourceBlend);
    b.DestBlend = b.DestBlendAlpha = Blend(p.raster.destinationBlend);
    b.BlendOp = static_cast<D3D12_BLEND_OP>(p.colorOperation);
    b.BlendOpAlpha =
        static_cast<D3D12_BLEND_OP>(p.separateAlpha ? p.alphaOperation : p.colorOperation);
    if (p.separateAlpha) {
        b.SrcBlendAlpha = Blend(p.sourceAlpha);
        b.DestBlendAlpha = Blend(p.destinationAlpha);
    }
    b.LogicOp = D3D12_LOGIC_OP_NOOP;
    b.RenderTargetWriteMask = p.colorWriteMask;
    d.RasterizerState.FillMode = D3D12_FILL_MODE_SOLID;
    d.RasterizerState.CullMode =
        p.raster.cull == BatchCull::None ? D3D12_CULL_MODE_NONE : D3D12_CULL_MODE_BACK;
    d.RasterizerState.FrontCounterClockwise = p.raster.cull == BatchCull::Clockwise;
    d.RasterizerState.DepthClipEnable = TRUE;
    auto& ds = d.DepthStencilState;
    ds.DepthEnable = p.raster.depthTest;
    ds.DepthWriteMask =
        p.raster.depthWrite ? D3D12_DEPTH_WRITE_MASK_ALL : D3D12_DEPTH_WRITE_MASK_ZERO;
    ds.DepthFunc = static_cast<D3D12_COMPARISON_FUNC>(unsigned(p.raster.depthCompare) + 1);
    ds.StencilEnable = p.stencil;
    ds.StencilReadMask = p.stencilReadMask;
    ds.StencilWriteMask = p.stencilWriteMask;
    ds.FrontFace = {static_cast<D3D12_STENCIL_OP>(p.stencilFail),
                    static_cast<D3D12_STENCIL_OP>(p.stencilDepthFail),
                    static_cast<D3D12_STENCIL_OP>(p.stencilPass),
                    static_cast<D3D12_COMPARISON_FUNC>(unsigned(p.stencilCompare) + 1)};
    ds.BackFace = ds.FrontFace;
    d.SampleMask = UINT_MAX;
    d.PrimitiveTopologyType = p.topology == PrimitiveTopology::LineList
                                  ? D3D12_PRIMITIVE_TOPOLOGY_TYPE_LINE
                                  : D3D12_PRIMITIVE_TOPOLOGY_TYPE_TRIANGLE;
    const auto formats = ActiveColorFormats();
    d.NumRenderTargets = UINT(formats.size());
    for (std::size_t i = 0; i < formats.size(); ++i)
        d.RTVFormats[i] = formats[i];
    d.DSVFormat = ActiveDepthFormat();
    d.SampleDesc.Count = 1;
    const D3D12_INPUT_ELEMENT_DESC elements[] = {{"POSITION", 0, DXGI_FORMAT_R32G32B32_FLOAT, 0, 0,
                                                  D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA, 0},
                                                 {"NORMAL", 0, DXGI_FORMAT_R32G32B32_FLOAT, 0, 12,
                                                  D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA, 0},
                                                 {"TEXCOORD", 0, DXGI_FORMAT_R32G32_FLOAT, 0, 24,
                                                  D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA, 0}};
    const D3D12_INPUT_ELEMENT_DESC colors[] = {{"POSITION", 0, DXGI_FORMAT_R32G32B32_FLOAT, 0, 0,
                                                D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA, 0},
                                               {"COLOR", 0, DXGI_FORMAT_R8G8B8A8_UNORM, 0, 12,
                                                D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA, 0}};
    const D3D12_INPUT_ELEMENT_DESC screen[] = {{"POSITION", 0, DXGI_FORMAT_R32G32B32A32_FLOAT, 0, 0,
                                                D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA, 0},
                                               {"COLOR", 0, DXGI_FORMAT_R8G8B8A8_UNORM, 0, 16,
                                                D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA, 0},
                                               {"TEXCOORD", 0, DXGI_FORMAT_R32G32_FLOAT, 0, 20,
                                                D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA, 0}};
    // Descriptors are fully zero-initialized before fields are written. The key
    // excludes pointers, constants, texture identities and dynamic stencil ref.
    std::string key;
    auto append = [&](const auto& v) { key.append(reinterpret_cast<const char*>(&v), sizeof(v)); };
    append(d.BlendState);
    append(d.RasterizerState);
    append(d.DepthStencilState);
    append(d.PrimitiveTopologyType);
    append(d.DSVFormat);
    for (auto format : d.RTVFormats)
        append(format);
    append(d.NumRenderTargets);
    append(layout);
    append(p.shadow.mode);
    append(p.postProcess.mode);
    if (generic)
        key.append(generic->key);
    auto state = pipelines.find(key);
    if (state == pipelines.end()) {
        d.InputLayout =
            layout == VertexLayout::ScreenPositionColorUv ? D3D12_INPUT_LAYOUT_DESC{screen, 3}
            : layout == VertexLayout::PositionColor       ? D3D12_INPUT_LAYOUT_DESC{colors, 2}
                                                          : D3D12_INPUT_LAYOUT_DESC{elements, 3};
        std::vector<D3D12_INPUT_ELEMENT_DESC> genericInputs;
        if (generic) {
            for (const auto& i : generic->description.inputs) {
                const auto format =
                    i.format == ShaderVertexFormat::Float2   ? DXGI_FORMAT_R32G32_FLOAT
                    : i.format == ShaderVertexFormat::Float3 ? DXGI_FORMAT_R32G32B32_FLOAT
                    : i.format == ShaderVertexFormat::Float4 ? DXGI_FORMAT_R32G32B32A32_FLOAT
                                                             : DXGI_FORMAT_R8G8B8A8_UNORM;
                genericInputs.push_back({i.semantic.c_str(), i.semanticIndex, format, 0, i.offset,
                                         D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA, 0});
            }
            d.InputLayout = {genericInputs.data(), UINT(genericInputs.size())};
        }
        ComPtr<ID3D12PipelineState> created;
        lastError = native->CreateGraphicsPipelineState(&d, IID_PPV_ARGS(&created));
        if (FAILED(lastError))
            return false;
        state = pipelines.emplace(std::move(key), std::move(created)).first;
        ++stats.pipelineCreations;
    }
    frames[frameIndex].commands->SetPipelineState(state->second.Get());
    return true;
}

bool D3D12Device::PrepareShaderPipeline(const ShaderPipeline& p) {
    const auto result = ValidateShaderTargetSupport(*this, p);
    genericError = result.reason;
    if (!result || !Live())
        return false;
    if (!genericRoot) {
        D3D12_ROOT_PARAMETER parameters[6]{};
        D3D12_DESCRIPTOR_RANGE ranges[2]{};
        for (unsigned i = 0; i < 4; ++i) {
            parameters[i].ParameterType = D3D12_ROOT_PARAMETER_TYPE_CBV;
            parameters[i].Descriptor.ShaderRegister = i;
            parameters[i].ShaderVisibility = D3D12_SHADER_VISIBILITY_ALL;
        }
        ranges[0] = {D3D12_DESCRIPTOR_RANGE_TYPE_SRV, 4, 0, 0, 0};
        ranges[1] = {D3D12_DESCRIPTOR_RANGE_TYPE_SAMPLER, 4, 0, 0, 0};
        for (unsigned i = 0; i < 2; ++i) {
            parameters[i + 4].ParameterType = D3D12_ROOT_PARAMETER_TYPE_DESCRIPTOR_TABLE;
            parameters[i + 4].DescriptorTable = {1, &ranges[i]};
            parameters[i + 4].ShaderVisibility = D3D12_SHADER_VISIBILITY_PIXEL;
        }
        D3D12_ROOT_SIGNATURE_DESC signature{};
        signature.NumParameters = 6;
        signature.pParameters = parameters;
        signature.Flags = D3D12_ROOT_SIGNATURE_FLAG_ALLOW_INPUT_ASSEMBLER_INPUT_LAYOUT;
        ComPtr<ID3DBlob> blob, errors;
        lastError =
            D3D12SerializeRootSignature(&signature, D3D_ROOT_SIGNATURE_VERSION_1, &blob, &errors);
        if (SUCCEEDED(lastError))
            lastError = native->CreateRootSignature(
                0, blob->GetBufferPointer(), blob->GetBufferSize(), IID_PPV_ARGS(&genericRoot));
        if (FAILED(lastError)) {
            genericError = "shader_root_layout_failed";
            return false;
        }
    }
    auto compiled = genericPrograms.find(p.program->key);
    if (compiled == genericPrograms.end()) {
        detail::D3DShaderProgram binary;
        if (!detail::CompileD3DProgram(*p.program, binary, genericError))
            return false;
        compiled = genericPrograms.emplace(p.program->key, std::move(binary)).first;
    }
    genericError.clear();
    return true;
}
bool D3D12Device::SetShaderPipeline(const ShaderPipeline& p) {
    genericReady = genericBound = modelReady = screenReady = false;
    const auto result = ValidateShaderTargetSupport(*this, p);
    genericError = result.reason;
    const auto view = p.viewport.value_or(ModelViewport{0, 0, RenderWidth(), RenderHeight(), 0, 1});
    if (!result || !Live() || !inFrame ||
        !ValidModelViewport(view, RenderWidth(), RenderHeight()) ||
        ((p.raster.depthTest || p.raster.depthWrite) && ActiveDepthFormat() == DXGI_FORMAT_UNKNOWN))
        return false;
    const auto layout = ValidateShaderAttachments(
        p,
        passes.Active() ? passes.ColorFormats() : std::vector<TextureFormat>{TextureFormat::Rgba8});
    if (!layout) {
        genericError = layout.reason;
        return false;
    }
    if (!PrepareShaderPipeline(p))
        return false;
    auto compiled = genericPrograms.find(p.program->key);
    const auto adapter = ShaderRasterAdapter(p);
    if (!UsePipeline(adapter, p.vertexLayout, compiled->second.vertex.Get(),
                     compiled->second.pixel.Get(), p.program.get()))
        return false;
    auto* list = frames[frameIndex].commands.Get();
    list->SetGraphicsRootSignature(genericRoot.Get());
    list->IASetPrimitiveTopology(p.topology == PrimitiveTopology::LineList
                                     ? D3D_PRIMITIVE_TOPOLOGY_LINELIST
                                     : D3D_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
    D3D12_VIEWPORT viewport{float(view.x),      float(view.y), float(view.width),
                            float(view.height), view.minDepth, view.maxDepth};
    list->RSSetViewports(1, &viewport);
    pipeline = adapter;
    genericPipeline = p;
    genericReady = modelReady = true;
    genericError.clear();
    return true;
}
bool D3D12Device::SetModelPipeline(const ModelPipeline& p) {
    genericReady = genericBound = false;
    if (ActiveColorFormats().size() > 1)
        return false;
    if (inFrame)
        frames[frameIndex].commands->SetGraphicsRootSignature(root.Get());
    modelReady = screenReady = false;
    textures = {};
    shadowTexture = {};
    const auto& s = p.shader;
    const auto view = p.viewport.value_or(ModelViewport{0, 0, RenderWidth(), RenderHeight(), 0, 1});
    std::array<float, 4> evaluated;
    std::array<std::array<float, 4>, 3> samples{};
    if (!Live() || !inFrame || !ValidPostProcess(p) || !ValidBuiltinShadowState(p.shadow) ||
        (p.shadow.mode != BuiltinShadowMode::None &&
         (!passes.Active() || p.vertexLayout != BuiltinShadowVertexLayout(p.shadow.mode) ||
          p.topology != PrimitiveTopology::TriangleList)) ||
        !ValidModelViewport(view, RenderWidth(), RenderHeight()) ||
        !ModelVertexStride(p.vertexLayout) || !ModelPrimitiveArity(p.topology) ||
        !ValidateMaterialShaderInputs(s) ||
        !EvaluateMaterialTextureCascade(s.layout, {1, 1, 1, 1}, samples, evaluated) ||
        unsigned(p.raster.depthCompare) > 7 || unsigned(p.raster.cull) > 2 ||
        !Blend(p.raster.sourceBlend) || !Blend(p.raster.destinationBlend) ||
        !Blend(p.sourceAlpha) || !Blend(p.destinationAlpha) || unsigned(p.colorOperation) < 1 ||
        unsigned(p.colorOperation) > 5 || unsigned(p.alphaOperation) < 1 ||
        unsigned(p.alphaOperation) > 5 || p.colorWriteMask > 15 || unsigned(p.stencilCompare) > 7 ||
        unsigned(p.stencilFail) < 1 || unsigned(p.stencilFail) > 8 ||
        unsigned(p.stencilDepthFail) < 1 || unsigned(p.stencilDepthFail) > 8 ||
        unsigned(p.stencilPass) < 1 || unsigned(p.stencilPass) > 8 ||
        ((p.raster.depthTest || p.stencil) && ActiveDepthFormat() == DXGI_FORMAT_UNKNOWN) ||
        (p.stencil && ActiveDepthFormat() != DXGI_FORMAT_D24_UNORM_S8_UINT))
        return false;
    if (p.raster.alphaBlend &&
        (!AlphaFactor(p.separateAlpha ? p.sourceAlpha : p.raster.sourceBlend) ||
         !AlphaFactor(p.separateAlpha ? p.destinationAlpha : p.raster.destinationBlend)))
        return false;
    for (unsigned i = 0; i < 3; ++i) {
        const auto& t = s.layout.stages[i];
        const auto& a = s.samplers[i];
        D3D12_SAMPLER_DESC d{};
        if (t.role != MaterialTextureRole::None &&
            (t.coordinates == MaterialCoordinates::MeshUv1 || a.maxAnisotropy > 16 ||
             (p.vertexLayout == VertexLayout::PositionColor &&
              t.coordinates != MaterialCoordinates::MeshUv0)))
            return false;
        d.Filter = a.min == SamplerFilter::Anisotropic || a.mag == SamplerFilter::Anisotropic
                       ? D3D12_FILTER_ANISOTROPIC
                       : static_cast<D3D12_FILTER>((a.min == SamplerFilter::Linear ? 16 : 0) |
                                                   (a.mag == SamplerFilter::Linear ? 4 : 0) |
                                                   (a.mip == SamplerFilter::Linear ? 1 : 0));
        if (t.role != MaterialTextureRole::None && a.mip == SamplerFilter::Anisotropic &&
            d.Filter != D3D12_FILTER_ANISOTROPIC)
            return false;
        d.AddressU = a.u == SamplerAddress::Wrap ? D3D12_TEXTURE_ADDRESS_MODE_WRAP
                                                 : D3D12_TEXTURE_ADDRESS_MODE_CLAMP;
        d.AddressV = a.v == SamplerAddress::Wrap ? D3D12_TEXTURE_ADDRESS_MODE_WRAP
                                                 : D3D12_TEXTURE_ADDRESS_MODE_CLAMP;
        d.AddressW = D3D12_TEXTURE_ADDRESS_MODE_CLAMP;
        d.MaxAnisotropy = std::clamp(a.maxAnisotropy, 1u, 16u);
        d.ComparisonFunc = D3D12_COMPARISON_FUNC_ALWAYS;
        d.MaxLOD = D3D12_FLOAT32_MAX;
        samplerDescriptions[i] = d;
    }
    auto* vs = p.vertexLayout == VertexLayout::PositionColor ? colorVertex.Get() : vertex.Get();
    if (p.shadow.mode != BuiltinShadowMode::None)
        vs = p.shadow.mode == BuiltinShadowMode::Outline ? shadowOutlineVertex.Get()
                                                         : shadowVertex.Get();
    const bool post = p.postProcess.mode != PostProcessMode::None;
    if (!UsePipeline(p, p.vertexLayout, post ? postVertex.Get() : vs,
                     post                                       ? postPixel.Get()
                     : p.shadow.mode == BuiltinShadowMode::None ? pixel.Get()
                                                                : shadowPixel.Get()))
        return false;
    detail::ModelConstants constants;
    const bool measured = support.selected == ModelCompatibility::Dx9Measured;
    if (!detail::BuildModelConstants(p, view, measured, constants))
        return false;
    const auto vc = Constants(&constants.vertex, sizeof(constants.vertex)),
               pc = Constants(&constants.pixel, sizeof(constants.pixel)),
               combined = Constants(&constants.compatibility, sizeof(constants.compatibility));
    if (!vc || !pc || !combined)
        return false;
    auto* list = frames[frameIndex].commands.Get();
    list->SetGraphicsRootConstantBufferView(0, vc);
    list->SetGraphicsRootConstantBufferView(1, pc);
    list->SetGraphicsRootConstantBufferView(2, combined);
    if (post) {
        const auto c = detail::BuildPostProcessConstants(p);
        const auto cb = Constants(&c, sizeof(c));
        if (!cb)
            return false;
        list->SetGraphicsRootConstantBufferView(5, cb);
    }
    if (p.shadow.mode != BuiltinShadowMode::None) {
        const auto shadow = detail::BuildBuiltinShadowConstants(p, view);
        const auto cb = Constants(&shadow, sizeof(shadow));
        if (!cb)
            return false;
        list->SetGraphicsRootConstantBufferView(5, cb);
    }
    D3D12_SAMPLER_DESC shadowSampler{};
    shadowSampler.Filter = D3D12_FILTER_MIN_MAG_LINEAR_MIP_POINT;
    shadowSampler.AddressU = shadowSampler.AddressV = shadowSampler.AddressW =
        D3D12_TEXTURE_ADDRESS_MODE_CLAMP;
    shadowSampler.MaxAnisotropy = 1;
    shadowSampler.ComparisonFunc = D3D12_COMPARISON_FUNC_ALWAYS;
    samplerDescriptions[3] = shadowSampler;
    list->IASetPrimitiveTopology(p.topology == PrimitiveTopology::LineList
                                     ? D3D_PRIMITIVE_TOPOLOGY_LINELIST
                                     : D3D_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
    list->OMSetStencilRef(p.stencilReference);
    D3D12_VIEWPORT viewport{float(view.x),      float(view.y), float(view.width),
                            float(view.height), view.minDepth, view.maxDepth};
    if (p.postProcess.mode == PostProcessMode::None &&
        (measured || p.shadow.mode != BuiltinShadowMode::None)) {
        viewport.TopLeftX += .5f;
        viewport.TopLeftY += .5f;
    }
    list->RSSetViewports(1, &viewport);
    pipeline = p;
    modelReady = true;
    return true;
}
bool D3D12Device::SetFixedPipeline(FixedPipelineState f) {
    genericReady = genericBound = false;
    if (ActiveColorFormats().size() > 1)
        return false;
    if (inFrame)
        frames[frameIndex].commands->SetGraphicsRootSignature(root.Get());
    if (passes.Active())
        return false;
    modelReady = screenReady = false;
    textures = {};
    if (!Live() || !inFrame || f.topology != PrimitiveTopology::TriangleList ||
        f.layout != VertexLayout::ScreenPositionColorUv || f.lighting || f.alphaTest ||
        f.stencilTest || f.cull != RasterCull::None ||
        f.colorStage != FixedColorStage::TextureDiffuseModulate ||
        (f.depthCompare != CompareOp::Always && f.depthCompare != CompareOp::LessEqual) ||
        (f.depthTest && !depth) ||
        (f.sampler.min != SamplerFilter::Point && f.sampler.min != SamplerFilter::Linear) ||
        (f.sampler.mag != SamplerFilter::Point && f.sampler.mag != SamplerFilter::Linear) ||
        (f.sampler.mip != SamplerFilter::Point && f.sampler.mip != SamplerFilter::Linear) ||
        (f.sampler.u != SamplerAddress::Wrap && f.sampler.u != SamplerAddress::Clamp) ||
        (f.sampler.v != SamplerAddress::Wrap && f.sampler.v != SamplerAddress::Clamp))
        return false;
    ModelPipeline p;
    p.raster = {f.depthTest,
                f.depthWrite,
                f.alphaBlend,
                f.depthCompare == CompareOp::Always ? DepthComparison::Always
                                                    : DepthComparison::LessEqual,
                BlendFactor::SourceAlpha,
                BlendFactor::InvSourceAlpha,
                BatchCull::None};
    p.shader.layout.stages[0].role = MaterialTextureRole::Base;
    if (!UsePipeline(p, VertexLayout::ScreenPositionColorUv, screenVertex.Get(), screenPixel.Get()))
        return false;
    D3D12_SAMPLER_DESC s{};
    s.Filter = static_cast<D3D12_FILTER>((f.sampler.min == SamplerFilter::Linear ? 16 : 0) |
                                         (f.sampler.mag == SamplerFilter::Linear ? 4 : 0) |
                                         (f.sampler.mip == SamplerFilter::Linear ? 1 : 0));
    s.AddressU = f.sampler.u == SamplerAddress::Wrap ? D3D12_TEXTURE_ADDRESS_MODE_WRAP
                                                     : D3D12_TEXTURE_ADDRESS_MODE_CLAMP;
    s.AddressV = f.sampler.v == SamplerAddress::Wrap ? D3D12_TEXTURE_ADDRESS_MODE_WRAP
                                                     : D3D12_TEXTURE_ADDRESS_MODE_CLAMP;
    s.AddressW = D3D12_TEXTURE_ADDRESS_MODE_CLAMP;
    s.MaxAnisotropy = 1;
    s.ComparisonFunc = D3D12_COMPARISON_FUNC_NEVER;
    s.MaxLOD = D3D12_FLOAT32_MAX;
    samplerDescriptions.fill(s);
    const float dims[] = {float(targetWidth), float(targetHeight), 0, 0};
    const auto constant = Constants(dims, sizeof(dims));
    if (!constant)
        return false;
    auto* list = frames[frameIndex].commands.Get();
    list->SetGraphicsRootConstantBufferView(0, constant);
    list->IASetPrimitiveTopology(D3D_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
    list->OMSetStencilRef(0);
    const D3D12_VIEWPORT v{0, 0, float(targetWidth), float(targetHeight), 0, 1};
    list->RSSetViewports(1, &v);
    pipeline = p;
    modelReady = screenReady = true;
    return true;
}

class D3D12Swapchain final : public Swapchain {
    D3D12Device& device;
    bool retired = false;

public:
    explicit D3D12Swapchain(D3D12Device& d) : device(d) {}
    SwapchainDesc Description() const override { return device.desc; }
    std::int64_t LastNativeError() const override { return device.lastError; }
    SwapchainSupport Query(const SwapchainDesc& d) const override {
        SwapchainSupport out;
        if (!ValidSwapchainDesc(d) || d.window.system != WindowSystem::Win32 ||
            !IsWindow(reinterpret_cast<HWND>(d.window.handle)) ||
            d.window.handle != device.desc.window.handle || d.color != SurfaceColor::Rgba8 ||
            d.depth == SurfaceDepth::Depth24 || !d.windowed || d.swap != SurfaceSwap::Discard ||
            d.samples != 1 || d.quality || d.width > 16384 || d.height > 16384)
            return out;
        out.supported = out.resize = out.controllerOwnsReset = true;
        out.qualityLevels = 1;
        return out;
    }
    SurfaceHealth Health() const override {
        if (retired || device.native->GetDeviceRemovedReason() != S_OK)
            return SurfaceHealth::Removed;
        return IsIconic(reinterpret_cast<HWND>(device.desc.window.handle))
                   ? SurfaceHealth::Suspended
                   : SurfaceHealth::Ready;
    }
    RebuildResult Rebuild(const SwapchainDesc& d, RebuildReason reason) override {
        if (retired)
            return RebuildResult::ReplacementRequired;
        if (reason == RebuildReason::DeviceReplacement)
            return Retire() ? RebuildResult::ReplacementRequired : RebuildResult::Busy;
        if (device.native->GetDeviceRemovedReason() != S_OK) {
            device.pending = true;
            return RebuildResult::ReplacementRequired;
        }
        if (!ValidSwapchainDesc(d))
            return RebuildResult::Invalid;
        if (!Query(d).supported || reason != RebuildReason::Resize)
            return RebuildResult::Unsupported;
        if (Health() == SurfaceHealth::Suspended)
            return RebuildResult::RetryLater;
        if (device.inFrame)
            return RebuildResult::Busy;
        if (!device.pending && !device.PrepareForReset())
            return RebuildResult::Failed;
        if (!device.WaitIdle())
            return RebuildResult::Failed;
        // No allocator or frame descriptor retains a backbuffer view. All
        // submitted lists are complete before releasing these native buffers.
        device.offscreen.Reset();
        device.depth.Reset();
        for (auto& b : device.backbuffers)
            b.Reset();
        device.lastError = device.chain->ResizeBuffers(BackbufferCount, d.width, d.height,
                                                       DXGI_FORMAT_R8G8B8A8_UNORM, 0);
        if (FAILED(device.lastError))
            return RebuildResult::Failed;
        if (!device.MakeTargets(false, d.width, d.height, d.depth))
            return RebuildResult::Failed;
        device.desc = d;
        return device.RestoreAfterReset() ? RebuildResult::Success : RebuildResult::Failed;
    }
    bool Retire() override {
        if (device.inFrame)
            return false;
        if (device.native->GetDeviceRemovedReason() != S_OK) {
            device.pending = device.retired = retired = true;
            return true;
        }
        if (!device.pending && !device.PrepareForReset())
            return false;
        if (!device.WaitIdle())
            return false;
        retired = device.retired = true;
        return true;
    }
};
bool ValidCreation(const SwapchainDesc& d) {
    return ValidSwapchainDesc(d) && d.window.system == WindowSystem::Win32 &&
           IsWindow(reinterpret_cast<HWND>(d.window.handle)) && d.color == SurfaceColor::Rgba8 &&
           d.depth != SurfaceDepth::Depth24 && d.windowed && d.samples == 1 && !d.quality &&
           d.swap == SurfaceSwap::Discard && d.width <= 16384 && d.height <= 16384;
}
std::string AdapterName(const wchar_t* name) {
    const auto bytes = WideCharToMultiByte(CP_UTF8, 0, name, -1, nullptr, 0, nullptr, nullptr);
    if (bytes <= 1)
        return {};
    std::string out(bytes, '\0');
    WideCharToMultiByte(CP_UTF8, 0, name, -1, out.data(), bytes, nullptr, nullptr);
    out.pop_back();
    return out;
}
HRESULT ConfigureDebug(bool requested) {
    // EnableDebugLayer after a device exists can remove that device, including
    // when replacing it. Debug configuration is a process startup decision.
    static std::mutex mutex;
    static bool configured = false, enabled = false;
    std::lock_guard<std::mutex> guard(mutex);
    if (configured)
        return requested && !enabled ? DXGI_ERROR_INVALID_CALL : S_OK;
    if (requested) {
        ComPtr<ID3D12Debug> debug;
        const auto hr = D3D12GetDebugInterface(IID_PPV_ARGS(&debug));
        if (FAILED(hr))
            return hr;
        debug->EnableDebugLayer();
        enabled = true;
    }
    configured = true;
    return S_OK;
}
void WINAPI DebugMessage(D3D12_MESSAGE_CATEGORY, D3D12_MESSAGE_SEVERITY severity,
                         D3D12_MESSAGE_ID id, LPCSTR message, void*) {
    if (severity == D3D12_MESSAGE_SEVERITY_ERROR || severity == D3D12_MESSAGE_SEVERITY_CORRUPTION)
        std::fprintf(stderr, "DX12 validation %u: %s\n", unsigned(id), message);
}
}
static D3D12Bundle CreateD3D12DeviceInternal(const SwapchainDesc& desc, D3D12Driver driver,
                                             D3D12Options options, bool headless) {
    D3D12Bundle out;
    out.creationError = E_INVALIDARG;
    if ((!headless && !ValidCreation(desc)) || !desc.width || !desc.height || desc.width > 4096 ||
        desc.height > 4096 || (driver != D3D12Driver::Hardware && driver != D3D12Driver::Warp)) {
        out.creationReason = "unsupported_parameters";
        return out;
    }
    if (options.model.diagnosticAlpha12 &&
        options.model.compatibility != ModelCompatibility::Dx9Measured) {
        out.stage = D3D12CreationStage::Compatibility;
        out.creationReason = "diagnostic_alpha_requires_dx9_measured";
        return out;
    }
    if (options.model.diagnosticSpecularFma &&
        options.model.compatibility != ModelCompatibility::Dx9Measured) {
        out.stage = D3D12CreationStage::Compatibility;
        out.creationReason = "diagnostic_specular_requires_dx9_measured";
        return out;
    }
    if (options.model.diagnosticTextureFma &&
        options.model.compatibility != ModelCompatibility::Dx9Measured) {
        out.stage = D3D12CreationStage::Compatibility;
        out.creationReason = "diagnostic_texture_requires_dx9_measured";
        return out;
    }
    if (CheckModelCompatibility(options.model.compatibility, true) ==
        ModelCompatibilityResult::InvalidMode) {
        out.stage = D3D12CreationStage::Compatibility;
        out.creationReason = "invalid_model_compatibility";
        return out;
    }
    out.creationError = ConfigureDebug(options.debugLayer);
    if (FAILED(out.creationError)) {
        out.creationReason = out.creationError == DXGI_ERROR_INVALID_CALL
                                 ? "debug_layer_requires_first_device"
                                 : "debug_layer_unavailable";
        return out;
    }
    auto device = std::make_unique<D3D12Device>();
    device->desc = desc;
    auto fail = [&](const char* reason) {
        out.creationError = device->lastError;
        out.creationReason = reason;
        return std::move(out);
    };
    out.stage = D3D12CreationStage::NativeDevice;
    ComPtr<IDXGIFactory4> factory;
    device->lastError = CreateDXGIFactory2(options.debugLayer ? DXGI_CREATE_FACTORY_DEBUG : 0,
                                           IID_PPV_ARGS(&factory));
    if (FAILED(device->lastError))
        return fail("factory_failed");
    ComPtr<IDXGIAdapter1> adapter;
    if (driver == D3D12Driver::Warp) {
        device->lastError = factory->EnumWarpAdapter(IID_PPV_ARGS(&adapter));
        if (FAILED(device->lastError))
            return fail("warp_unavailable");
    } else {
        for (unsigned n = 0;; ++n) {
            ComPtr<IDXGIAdapter1> candidate;
            const auto hr = factory->EnumAdapters1(n, &candidate);
            if (hr == DXGI_ERROR_NOT_FOUND)
                break;
            if (FAILED(hr)) {
                device->lastError = hr;
                return fail("adapter_enumeration_failed");
            }
            DXGI_ADAPTER_DESC1 a{};
            candidate->GetDesc1(&a);
            if (a.Flags & DXGI_ADAPTER_FLAG_SOFTWARE)
                continue;
            if (SUCCEEDED(D3D12CreateDevice(candidate.Get(), D3D_FEATURE_LEVEL_11_0,
                                            __uuidof(ID3D12Device), nullptr))) {
                adapter = candidate;
                break;
            }
        }
        if (!adapter) {
            device->lastError = DXGI_ERROR_UNSUPPORTED;
            return fail("hardware_unavailable");
        }
    }
    DXGI_ADAPTER_DESC1 a{};
    adapter->GetDesc1(&a);
    out.adapterName = AdapterName(a.Description);
    out.vendorId = a.VendorId;
    out.deviceId = a.DeviceId;
    device->lastError =
        D3D12CreateDevice(adapter.Get(), D3D_FEATURE_LEVEL_11_0, IID_PPV_ARGS(&device->native));
    if (FAILED(device->lastError))
        return fail("device_failed");
    if (options.debugLayer) {
        device->native.As(&device->debug);
        if (device->debug) {
            device->debug->SetBreakOnSeverity(D3D12_MESSAGE_SEVERITY_CORRUPTION, FALSE);
            device->debug->SetBreakOnSeverity(D3D12_MESSAGE_SEVERITY_ERROR, FALSE);
        }
        if (SUCCEEDED(device->native.As(&device->debugCallback)))
            device->debugCallback->RegisterMessageCallback(
                DebugMessage, D3D12_MESSAGE_CALLBACK_FLAG_NONE, nullptr, &device->debugCookie);
    }
    D3D12_FEATURE_DATA_D3D12_OPTIONS features{};
    device->lastError = device->native->CheckFeatureSupport(D3D12_FEATURE_D3D12_OPTIONS, &features,
                                                            sizeof(features));
    if (FAILED(device->lastError))
        return fail("feature_query_failed");
    device->support.shaderDoubles = features.DoublePrecisionFloatShaderOps != FALSE;
    device->support.selected = options.model.compatibility;
    device->support.diagnosticAlpha12 = options.model.diagnosticAlpha12;
    device->support.diagnosticTextureFma = options.model.diagnosticTextureFma;
    device->support.diagnosticSpecularFma = options.model.diagnosticSpecularFma;
    device->support.requestedSupport =
        CheckModelCompatibility(options.model.compatibility, device->support.shaderDoubles);
    out.modelSupport = device->support;
    if (device->support.requestedSupport != ModelCompatibilityResult::Supported) {
        out.stage = D3D12CreationStage::Compatibility;
        device->lastError = DXGI_ERROR_UNSUPPORTED;
        return fail("shader_doubles_required");
    }
    out.stage = D3D12CreationStage::Queue;
    D3D12_COMMAND_QUEUE_DESC q{};
    q.Type = D3D12_COMMAND_LIST_TYPE_DIRECT;
    device->lastError = device->native->CreateCommandQueue(&q, IID_PPV_ARGS(&device->queue));
    if (FAILED(device->lastError))
        return fail("queue_failed");
    device->lastError =
        device->native->CreateFence(0, D3D12_FENCE_FLAG_NONE, IID_PPV_ARGS(&device->fence));
    if (FAILED(device->lastError))
        return fail("fence_failed");
    device->completionEvent = CreateEventW(nullptr, FALSE, FALSE, nullptr);
    if (!device->completionEvent) {
        device->lastError = HRESULT_FROM_WIN32(GetLastError());
        return fail("fence_event_failed");
    }
    device->viewStride =
        device->native->GetDescriptorHandleIncrementSize(D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV);
    device->samplerStride =
        device->native->GetDescriptorHandleIncrementSize(D3D12_DESCRIPTOR_HEAP_TYPE_SAMPLER);
    device->rtvStride =
        device->native->GetDescriptorHandleIncrementSize(D3D12_DESCRIPTOR_HEAP_TYPE_RTV);
    if (!device->InitializeFrames())
        return fail("frame_resources_failed");
    D3D12_DESCRIPTOR_HEAP_DESC heap{};
    heap.Type = D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV;
    heap.NumDescriptors = MaxResources;
    device->lastError =
        device->native->CreateDescriptorHeap(&heap, IID_PPV_ARGS(&device->cpuViews));
    if (FAILED(device->lastError))
        return fail("cpu_descriptors_failed");
    heap.Type = D3D12_DESCRIPTOR_HEAP_TYPE_RTV;
    heap.NumDescriptors = BackbufferCount + 1 + MaxResources;
    device->lastError = device->native->CreateDescriptorHeap(&heap, IID_PPV_ARGS(&device->rtvs));
    if (FAILED(device->lastError))
        return fail("rtv_descriptors_failed");
    heap.Type = D3D12_DESCRIPTOR_HEAP_TYPE_DSV;
    heap.NumDescriptors = 1 + MaxResources;
    device->lastError = device->native->CreateDescriptorHeap(&heap, IID_PPV_ARGS(&device->dsvs));
    if (FAILED(device->lastError))
        return fail("dsv_descriptors_failed");
    out.stage = D3D12CreationStage::Shaders;
    if (!device->MakeShaders())
        return fail("shared_shaders_failed");
    out.stage = D3D12CreationStage::Targets;
    if (!headless) {
        DXGI_SWAP_CHAIN_DESC1 surface{};
        surface.Width = desc.width;
        surface.Height = desc.height;
        surface.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
        surface.SampleDesc.Count = 1;
        surface.BufferUsage = DXGI_USAGE_RENDER_TARGET_OUTPUT;
        surface.BufferCount = BackbufferCount;
        surface.SwapEffect = DXGI_SWAP_EFFECT_FLIP_DISCARD;
        ComPtr<IDXGISwapChain1> chain;
        device->lastError = factory->CreateSwapChainForHwnd(
            device->queue.Get(), reinterpret_cast<HWND>(desc.window.handle), &surface, nullptr,
            nullptr, &chain);
        if (FAILED(device->lastError))
            return fail("swapchain_failed");
        device->lastError = chain.As(&device->chain);
        if (FAILED(device->lastError))
            return fail("swapchain_interface_failed");
        factory->MakeWindowAssociation(reinterpret_cast<HWND>(desc.window.handle),
                                       DXGI_MWA_NO_ALT_ENTER);
    }
    if (!device->MakeTargets(headless, desc.width, desc.height, desc.depth))
        return fail("targets_failed");
    out.creationError = S_OK;
    out.stage = D3D12CreationStage::Ready;
    out.creationReason = "ready";
    if (!headless)
        out.swapchain = std::make_unique<D3D12Swapchain>(*device);
    out.device = std::move(device);
    return out;
}
D3D12Bundle CreateD3D12Device(const SwapchainDesc& d, D3D12Driver driver, D3D12Options options) {
    return CreateD3D12DeviceInternal(d, driver, options, false);
}
D3D12Bundle CreateD3D12OffscreenDevice(std::uint32_t width, std::uint32_t height,
                                       D3D12Driver driver, D3D12Options options) {
    SwapchainDesc d;
    d.width = width;
    d.height = height;
    return CreateD3D12DeviceInternal(d, driver, options, true);
}
D3D12Stats QueryD3D12Stats(Device& base) {
    auto* d = dynamic_cast<D3D12Device*>(&base);
    if (!d)
        return {};
    auto out = d->stats;
    out.completed = d->fence->GetCompletedValue();
    out.retiringResources = d->retiring.size();
    if (d->debug)
        for (UINT64 i = 0; i < d->debug->GetNumStoredMessagesAllowedByRetrievalFilter(); ++i) {
            SIZE_T bytes = 0;
            d->debug->GetMessage(i, nullptr, &bytes);
            std::vector<std::uint8_t> storage(bytes);
            auto* m = reinterpret_cast<D3D12_MESSAGE*>(storage.data());
            if (SUCCEEDED(d->debug->GetMessage(i, m, &bytes)) &&
                (m->Severity == D3D12_MESSAGE_SEVERITY_ERROR ||
                 m->Severity == D3D12_MESSAGE_SEVERITY_CORRUPTION))
                ++out.debugErrors;
        }
    return out;
}
bool ReadD3D12Swapchain(Device& base, RgbaImage& out) {
    auto* d = dynamic_cast<D3D12Device*>(&base);
    if (!d || d->offscreen) {
        out = {};
        return false;
    }
    return d->Read(d->backbuffers[d->lastBackbuffer].Get(), D3D12_RESOURCE_STATE_PRESENT, out);
}
bool RemoveD3D12DeviceForTest(Device& base) {
    auto* d = dynamic_cast<D3D12Device*>(&base);
    if (!d || d->inFrame)
        return false;
    ComPtr<ID3D12Device5> diagnostic;
    if (FAILED(d->native.As(&diagnostic)))
        return false;
    diagnostic->RemoveDevice();
    return d->Status().state == DeviceState::Removed;
}
}
