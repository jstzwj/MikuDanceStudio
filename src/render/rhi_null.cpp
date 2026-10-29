#include "mikudancestudio/rhi.hpp"
#include "mikudancestudio/rhi_render_pass.hpp"
#include "mikudancestudio/rhi_shader.hpp"
#include <algorithm>
#include <cstring>
#include <cmath>
#include <iterator>
#include <limits>
#include <vector>

namespace mikudancestudio::rhi {
namespace {
struct Resource {
    std::uint32_t generation = 1;
    bool alive = false, texture = false, uploaded = false;
    BufferDesc buffer;
    TextureDesc textureDescription;
    std::vector<std::uint8_t> bytes;
};
class NullDevice final : public Device {
public:
    Backend BackendType() const override { return Backend::Null; }
    Capabilities Features() const override { return {}; }
    ShaderCapabilities ShaderFeatures() const override { return GraphicsShaderProfile(true, true); }
    bool PrepareShaderPipeline(const ShaderPipeline& p) override {
        const auto result = ValidateShaderPipeline(p, ShaderFeatures());
        shaderError_ = result.reason;
        return bool(result);
    }
    const char* ShaderDiagnostic() const override { return shaderError_.c_str(); }
    bool SetShaderPipeline(const ShaderPipeline& p) override {
        shaderReady_ = bindingsReady_ = linePipelineReady_ = false;
        const auto result = ValidateShaderPipeline(p, ShaderFeatures());
        shaderError_ = result.reason;
        if (!inFrame_ || resetPending_ || !result)
            return false;
        const auto& pass = passes_.Current();
        const auto* target =
            passes_.Active()
                ? passes_.Description(pass.color ? pass.color->texture : pass.depthStencil->texture)
                : nullptr;
        if (p.viewport &&
            (!target || !ValidModelViewport(*p.viewport, target->width, target->height)))
            return false;
        if ((p.raster.depthTest || p.raster.depthWrite) &&
            (!passes_.Active() || !pass.depthStencil))
            return false;
        shader_ = p;
        shaderReady_ = true;
        return true;
    }
    bool BindShaderResources(const ShaderResourceBindings& bindings) override {
        bindingsReady_ = false;
        if (!inFrame_ || !shaderReady_)
            return false;
        const auto result = ValidateShaderBindings(shader_, bindings, [&](Handle h) {
            const auto* r = Find(h);
            const auto* target = passes_.Description(h);
            return ShaderTextureInfo{r && r->texture,
                                     r && (r->uploaded || (target && passes_.CanSample(h))),
                                     r && (target ? passes_.CanSample(h) : r->uploaded), false,
                                     target ? target->format : TextureFormat::Rgba8};
        });
        shaderError_ = result.reason;
        if (!result)
            return false;
        shaderBindings_ = bindings;
        bindingsReady_ = true;
        return true;
    }
    DeviceStatus Status() const override {
        return {identity_, epoch_, resetPending_ ? DeviceState::ResetPending : DeviceState::Ready};
    }
    ResourceLifecycleCapabilities ResourceLifecycleFeatures() const override {
        return {true, true, true, true, false};
    }
    Handle CreateRenderTexture(const RenderTextureDesc& d) override {
        if (inFrame_ || resetPending_ || !ValidRenderTextureDesc(d))
            return {};
        Resource r;
        r.alive = r.texture = true;
        r.bytes.resize(std::size_t(d.width) * d.height * 4);
        const auto h = Insert(std::move(r));
        return passes_.Register(h, d) ? h : Handle{};
    }
    bool BeginRenderPass(const RenderPassDesc& p) override {
        if (!inFrame_ || resetPending_ || !passes_.Begin(p))
            return false;
        linePipelineReady_ = false;
        shaderReady_ = bindingsReady_ = false;
        boundTexture_ = {};
        for (const auto* a :
             {p.color ? &*p.color : nullptr, p.depthStencil ? &*p.depthStencil : nullptr}) {
            if (!a || a->load != AttachmentLoad::Clear)
                continue;
            const auto& d = *passes_.Description(a->texture);
            std::uint32_t value = 0;
            if (d.format == TextureFormat::R32Float)
                std::memcpy(&value, &a->clear.red, 4);
            else if (d.format == TextureFormat::Depth24Stencil8)
                value = std::uint32_t(std::round(double(a->clearDepth) * 16777215.0)) |
                        (std::uint32_t(a->clearStencil) << 24);
            else {
                unsigned shift = 0;
                for (float c : {a->clear.red, a->clear.green, a->clear.blue, a->clear.alpha}) {
                    value |= std::uint32_t(std::round(c * 255)) << shift;
                    shift += 8;
                }
            }
            auto& bytes = Find(a->texture)->bytes;
            for (std::size_t n = 0; n < bytes.size(); n += 4)
                std::memcpy(bytes.data() + n, &value, 4);
        }
        return true;
    }
    bool EndRenderPass() override {
        if (!inFrame_ || !passes_.End())
            return false;
        linePipelineReady_ = false;
        shaderReady_ = bindingsReady_ = false;
        boundTexture_ = {};
        return true;
    }
    bool ReadRenderTexture(Handle h, AttachmentReadback& out) override {
        if (inFrame_ || resetPending_ || !passes_.CanRead(h))
            return false;
        out = {*passes_.Description(h), Find(h)->bytes};
        return true;
    }
    Handle CreateBuffer(BufferDesc description, const void* initialData) override {
        if (resetPending_ || !description.size ||
            description.size > std::numeric_limits<std::uint32_t>::max() ||
            description.usage == BufferUsage::Uniform ||
            (description.usage == BufferUsage::Index &&
             description.size % (description.indexFormat == IndexFormat::UInt32 ? 4u : 2u) != 0))
            return {};
        Resource resource;
        resource.alive = true;
        resource.buffer = description;
        resource.bytes.resize(description.size);
        if (initialData)
            std::memcpy(resource.bytes.data(), initialData, description.size);
        return Insert(std::move(resource));
    }
    Handle CreateTexture(TextureDesc description) override {
        if (resetPending_ || !description.width || !description.height ||
            description.width > std::numeric_limits<std::size_t>::max() / 4u ||
            description.height > std::numeric_limits<std::size_t>::max() /
                                     (static_cast<std::size_t>(description.width) * 4u) ||
            description.format != TextureFormat::Rgba8 ||
            (description.mips != TextureDesc::Mips::SingleLevel &&
             description.mips != TextureDesc::Mips::GenerateLinear))
            return {};
        Resource resource;
        resource.alive = true;
        resource.texture = true;
        resource.textureDescription = description;
        resource.bytes.resize(static_cast<std::size_t>(description.width) * description.height *
                              4u);
        return Insert(std::move(resource));
    }
    bool UploadTexture(Handle handle, TexturePixels pixels) override {
        auto* resource = Find(handle);
        if (!resource || !resource->texture || passes_.Description(handle) || inFrame_ ||
            resetPending_ || !pixels.data)
            return false;
        const std::size_t rowBytes =
            static_cast<std::size_t>(resource->textureDescription.width) * 4u;
        const std::size_t height = resource->textureDescription.height;
        if (pixels.rowPitch < rowBytes ||
            pixels.rowPitch > (std::numeric_limits<std::size_t>::max() - rowBytes) /
                                  (height - 1 ? height - 1 : 1) ||
            pixels.bytes < (height - 1) * pixels.rowPitch + rowBytes)
            return false;
        const auto* source = static_cast<const std::uint8_t*>(pixels.data);
        for (std::size_t row = 0; row < height; ++row)
            std::memcpy(resource->bytes.data() + row * rowBytes, source + row * pixels.rowPitch,
                        rowBytes);
        resource->uploaded = true;
        return true;
    }
    bool BindTexture(std::uint32_t slot, Handle handle) override {
        auto* resource = Find(handle);
        if (!inFrame_ || !linePipelineReady_ || slot != 0 || !resource || !resource->texture ||
            (passes_.Description(handle) ? !passes_.CanSample(handle) : !resource->uploaded))
            return false;
        boundTexture_ = handle;
        return true;
    }
    bool Upload(Handle handle, std::size_t offset, const void* data, std::size_t size) override {
        auto* resource = Find(handle);
        if (!resource || resetPending_ || resource->texture || !resource->buffer.dynamic ||
            (size && !data) || offset > resource->bytes.size() ||
            size > resource->bytes.size() - offset)
            return false;
        if (size)
            std::memcpy(resource->bytes.data() + offset, data, size);
        return true;
    }
    bool Destroy(Handle handle) override {
        auto* resource = Find(handle);
        if (!resource || inFrame_)
            return false;
        if (passes_.Description(handle))
            passes_.Erase(handle);
        resource->alive = false;
        ++resource->generation;
        if (!resource->generation)
            ++resource->generation;
        resource->bytes.clear();
        if (boundTexture_.device == handle.device && boundTexture_.index == handle.index &&
            boundTexture_.generation == handle.generation)
            boundTexture_ = {};
        return true;
    }
    bool BeginFrame(ClearColor clear) override {
        shaderReady_ = bindingsReady_ = false;
        if (inFrame_ || ready_ || resetPending_ || !ValidColor(clear))
            return false;
        inFrame_ = true;
        linePipelineReady_ = false;
        return true;
    }
    bool SetFixedPipeline(FixedPipelineState pipeline) override {
        shaderReady_ = bindingsReady_ = false;
        const bool lines = pipeline.topology == PrimitiveTopology::LineList &&
                           pipeline.layout == VertexLayout::ScreenPositionColor;
        const bool texturedTriangles = pipeline.topology == PrimitiveTopology::TriangleList &&
                                       pipeline.layout == VertexLayout::ScreenPositionColorUv;
        if (!inFrame_ || (!lines && !texturedTriangles) ||
            (pipeline.depthCompare != CompareOp::Always &&
             pipeline.depthCompare != CompareOp::LessEqual &&
             pipeline.depthCompare != CompareOp::Greater &&
             pipeline.depthCompare != CompareOp::GreaterEqual) ||
            (pipeline.alphaCompare != CompareOp::Always &&
             pipeline.alphaCompare != CompareOp::LessEqual &&
             pipeline.alphaCompare != CompareOp::Greater &&
             pipeline.alphaCompare != CompareOp::GreaterEqual) ||
            (pipeline.stencilCompare != CompareOp::Always &&
             pipeline.stencilCompare != CompareOp::LessEqual &&
             pipeline.stencilCompare != CompareOp::Greater &&
             pipeline.stencilCompare != CompareOp::GreaterEqual) ||
            (pipeline.cull != RasterCull::None && pipeline.cull != RasterCull::CounterClockwise) ||
            (pipeline.colorStage != FixedColorStage::DiffuseOnly &&
             pipeline.colorStage != FixedColorStage::TextureDiffuseModulate) ||
            (pipeline.stencilPass != StencilOperation::Keep &&
             pipeline.stencilPass != StencilOperation::Replace))
            return false;
        linePipelineReady_ = true;
        currentPipeline_ = pipeline;
        boundTexture_ = {};
        return true;
    }
    bool Draw(NonIndexedDraw draw) override {
        if (shaderReady_) {
            auto* v = Find(draw.vertices);
            const auto stride = ModelVertexStride(shader_.vertexLayout),
                       arity = ModelPrimitiveArity(shader_.topology);
            return inFrame_ && bindingsReady_ && BindShaderResources(shaderBindings_) &&
                   IsCurrentSubmission(Status(), draw.submission) && v && !v->texture &&
                   v->buffer.usage == BufferUsage::Vertex &&
                   (!draw.layout || *draw.layout == shader_.vertexLayout) && draw.vertexCount &&
                   draw.vertexCount % arity == 0 && draw.firstVertex <= v->bytes.size() / stride &&
                   draw.vertexCount <= v->bytes.size() / stride - draw.firstVertex;
        }
        if (!IsCurrentSubmission(Status(), draw.submission))
            return false;
        if (draw.layout && *draw.layout != currentPipeline_.layout)
            return false;
        auto* vertices = Find(draw.vertices);
        const bool texturedTriangles = currentPipeline_.topology == PrimitiveTopology::TriangleList;
        const std::size_t stride = texturedTriangles ? 28u : 20u;
        const std::size_t verticesPerPrimitive = texturedTriangles ? 3u : 2u;
        return inFrame_ && linePipelineReady_ && vertices && !vertices->texture &&
               vertices->buffer.usage == BufferUsage::Vertex &&
               (!texturedTriangles || Find(boundTexture_)) && draw.vertexCount != 0 &&
               draw.vertexCount % verticesPerPrimitive == 0 &&
               draw.firstVertex <= vertices->bytes.size() / stride &&
               draw.vertexCount <= vertices->bytes.size() / stride - draw.firstVertex;
    }
    bool DrawIndexed(IndexedDraw draw) override {
        if (shaderReady_) {
            auto* v = Find(draw.vertices);
            auto* i = Find(draw.indices);
            const auto stride = ModelVertexStride(shader_.vertexLayout),
                       arity = ModelPrimitiveArity(shader_.topology);
            const auto step = i && i->buffer.indexFormat == IndexFormat::UInt32 ? 4u : 2u;
            if (!inFrame_ || !bindingsReady_ || !BindShaderResources(shaderBindings_) ||
                !IsCurrentSubmission(Status(), draw.submission) || !v || !i || v->texture ||
                i->texture || v->buffer.usage != BufferUsage::Vertex ||
                i->buffer.usage != BufferUsage::Index || draw.layout != shader_.vertexLayout ||
                !draw.vertexCount || !draw.indexCount || draw.indexCount % arity ||
                draw.vertexCount > v->bytes.size() / stride ||
                draw.firstIndex > i->bytes.size() / step ||
                draw.indexCount > i->bytes.size() / step - draw.firstIndex)
                return false;
            for (unsigned n = 0; n < draw.indexCount; ++n) {
                std::uint32_t index = 0;
                std::memcpy(&index, i->bytes.data() + (draw.firstIndex + n) * step, step);
                if (index >= draw.vertexCount)
                    return false;
            }
            return true;
        }
        if (!IsCurrentSubmission(Status(), draw.submission))
            return false;
        auto* vertices = Find(draw.vertices);
        auto* indices = Find(draw.indices);
        const auto stride = draw.layout == VertexLayout::PositionColor ? 16u : 32u;
        const auto indexSize =
            indices && indices->buffer.indexFormat == IndexFormat::UInt32 ? 4u : 2u;
        return inFrame_ && !linePipelineReady_ &&
               draw.layout != VertexLayout::ScreenPositionColor && vertices && indices &&
               !vertices->texture && !indices->texture &&
               vertices->buffer.usage == BufferUsage::Vertex &&
               indices->buffer.usage == BufferUsage::Index && draw.indexCount && draw.vertexCount &&
               draw.vertexCount <= vertices->bytes.size() / stride &&
               draw.firstIndex <= indices->bytes.size() / indexSize &&
               draw.indexCount <= indices->bytes.size() / indexSize - draw.firstIndex &&
               draw.indexCount % 3 == 0;
    }
    bool EndFrame() override {
        if (!inFrame_ || passes_.Active())
            return false;
        inFrame_ = false;
        linePipelineReady_ = false;
        shaderReady_ = bindingsReady_ = false;
        boundTexture_ = {};
        ready_ = true;
        return true;
    }
    bool Present() override {
        if (!ready_)
            return false;
        ready_ = false;
        return true;
    }
    bool PrepareForReset() override {
        if (inFrame_ || resetPending_)
            return false;
        for (auto& resource : resources_)
            if (resource.alive && !resource.texture && resource.buffer.dynamic) {
                resource.alive = false;
                if (++resource.generation == 0)
                    ++resource.generation;
                resource.bytes.clear();
            }
        ready_ = false;
        resetPending_ = true;
        passes_.InvalidateContents();
        return true;
    }
    bool RestoreAfterReset() override {
        if (!resetPending_)
            return false;
        resetPending_ = false;
        if (++epoch_ == 0)
            ++epoch_;
        return true;
    }

private:
    Handle Insert(Resource resource) {
        for (std::size_t index = 0; index < resources_.size(); ++index)
            if (!resources_[index].alive) {
                resource.generation = resources_[index].generation;
                resources_[index] = std::move(resource);
                return {static_cast<std::uint32_t>(index + 1), resources_[index].generation,
                        identity_};
            }
        if (resources_.size() >= std::numeric_limits<std::uint32_t>::max())
            return {};
        resources_.push_back(std::move(resource));
        return {static_cast<std::uint32_t>(resources_.size()), 1, identity_};
    }
    Resource* Find(Handle handle) {
        if (!handle || handle.device != identity_ || handle.index > resources_.size() ||
            !handle.index)
            return nullptr;
        auto& resource = resources_[handle.index - 1];
        return resource.alive && resource.generation == handle.generation ? &resource : nullptr;
    }
    static bool ValidColor(ClearColor color) {
        const float channels[] = {color.red, color.green, color.blue, color.alpha};
        return std::all_of(std::begin(channels), std::end(channels), [](float value) {
            return std::isfinite(value) && value >= 0 && value <= 1;
        });
    }
    std::vector<Resource> resources_;
    RenderPassState passes_;
    Handle boundTexture_{};
    FixedPipelineState currentPipeline_{};
    const std::uint64_t identity_ = detail::NextDeviceIdentity();
    std::uint64_t epoch_ = 1;
    bool inFrame_ = false, ready_ = false;
    bool resetPending_ = false, linePipelineReady_ = false;
    bool shaderReady_ = false, bindingsReady_ = false;
    ShaderPipeline shader_;
    ShaderResourceBindings shaderBindings_;
    std::string shaderError_;
};
}
std::unique_ptr<Device> CreateNullDevice() {
    return std::make_unique<NullDevice>();
}
}
