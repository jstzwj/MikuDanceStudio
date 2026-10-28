#pragma once
#include <atomic>
#include <cstddef>
#include <cstdint>
#include <memory>
#include <optional>
#include <vector>

namespace mikudancestudio::rhi {
enum class Backend {
    Null,
    D3D9,
    D3D11,
    D3D12,
    Vulkan,
    Metal
};
enum class BufferUsage {
    Vertex,
    Index,
    Uniform
};
enum class IndexFormat {
    UInt16,
    UInt32
};
enum class VertexLayout {
    PositionColor,
    PositionNormalUv,
    ScreenPositionColor,
    ScreenPositionColorUv
};
enum class PrimitiveTopology {
    TriangleList,
    LineList
};
enum class CompareOp {
    Always,
    LessEqual,
    Greater,
    GreaterEqual
};

// Model batches use a separate, complete raster contract. Texture cascades,
// lights and material constants are still borrowed by the first DX9 adapter.
enum class DepthComparison {
    Never,
    Less,
    Equal,
    LessEqual,
    Greater,
    NotEqual,
    GreaterEqual,
    Always
};
enum class BlendFactor {
    Zero,
    One,
    SourceColor,
    InvSourceColor,
    SourceAlpha,
    InvSourceAlpha,
    DestinationAlpha,
    InvDestinationAlpha,
    DestinationColor,
    InvDestinationColor,
    SourceAlphaSaturate
};
enum class BatchCull {
    None,
    Clockwise,
    CounterClockwise
};
struct BatchRasterState {
    bool depthTest = true, depthWrite = true, alphaBlend = true;
    DepthComparison depthCompare = DepthComparison::LessEqual;
    BlendFactor sourceBlend = BlendFactor::SourceAlpha;
    BlendFactor destinationBlend = BlendFactor::InvSourceAlpha;
    BatchCull cull = BatchCull::CounterClockwise;
};
enum class RasterCull {
    None,
    CounterClockwise
};
enum class FixedColorStage {
    DiffuseOnly,
    TextureDiffuseModulate
};
enum class StencilOperation {
    Keep,
    Replace
};
enum class TextureFormat {
    Rgba8,
    Depth24Stencil8,
    R32Float
};

enum class HistoryRepresentation {
    SingleSample,
    PerSample,
    ResolvedColor
};
enum class ColorResolveRule {
    None,
    LinearUnorm,
    Srgb
};
enum class HistoryAccess {
    Unsupported,
    Native,
    DriverExtension,
    DrawReplay
};
enum class HistoryResult {
    Success,
    Unsupported,
    Invalid,
    DeviceLost
};
struct HistoryDesc {
    std::uint32_t width = 0, height = 0, samples = 1, quality = 0;
    HistoryRepresentation representation = HistoryRepresentation::SingleSample;
    ColorResolveRule resolve = ColorResolveRule::None;
    bool color = true, depth = true, stencil = true;
    // Numeric sample index alone does not identify sample locations. Backends
    // must agree on a nonzero pattern identity for exact per-sample transfer.
    std::uint64_t samplePattern = 0;
};
struct HistoryCapabilities {
    HistoryAccess colorCapture = HistoryAccess::Unsupported;
    HistoryAccess depthCapture = HistoryAccess::Unsupported;
    HistoryAccess stencilCapture = HistoryAccess::Unsupported;
    HistoryAccess colorRestore = HistoryAccess::Unsupported;
    HistoryAccess depthRestore = HistoryAccess::Unsupported;
    HistoryAccess stencilRestore = HistoryAccess::Unsupported;
    bool perSampleCapture = false, perSampleRestore = false;
    bool resolvedColorCapture = false;
    bool linearResolve = false, srgbResolve = false;
    // A replay planner can populate this only after ALL preceding draws,
    // clears and target changes have supported, ordered representations.
    bool completeDrawReplay = false;
    std::uint32_t maxSamples = 1;
};
struct HistoryIdentity {
    std::uint64_t scene = 0, frame = 0, drawSequence = 0, sourceDevice = 0, deviceEpoch = 0;
};
struct HistoryData {
    HistoryDesc description;
    HistoryIdentity identity;
    // Texel-major, then sample-major. RGBA8 and packed D24(low)/S8(high).
    // ResolvedColor stores ONE RGBA per pixel and no depth/stencil plane.
    std::vector<std::uint8_t> rgba;
    std::vector<std::uint32_t> depthStencil;
};
enum class HistoryReplayEvent {
    Clear,
    Draw,
    TargetChange,
    ResourceUpdate
};
struct HistoryReplayBoundary {
    HistoryIdentity identity;
    HistoryDesc target;
    std::uint64_t eventCount = 0;
    bool startsAtClear = false, allEventsRecorded = false;
    bool allResourcesVersioned = false, allEventsSupported = false;
};
inline bool ValidHistoryDesc(const HistoryDesc& d);
inline bool CanReplayHistory(const HistoryCapabilities& c, const HistoryReplayBoundary& boundary) {
    return c.completeDrawReplay && ValidHistoryDesc(boundary.target) &&
           boundary.target.representation != HistoryRepresentation::ResolvedColor &&
           boundary.target.samples <= c.maxSamples && boundary.startsAtClear &&
           boundary.allEventsRecorded && boundary.allResourcesVersioned &&
           boundary.allEventsSupported;
}
inline bool ValidHistoryDesc(const HistoryDesc& d) {
    if (!d.width || !d.height || d.width > 16384 || d.height > 16384 || !d.samples ||
        d.samples > 16 || (d.samples & (d.samples - 1)) || (!d.color && !d.depth && !d.stencil))
        return false;
    if (d.representation == HistoryRepresentation::SingleSample)
        return d.samples == 1 && d.quality == 0 && d.resolve == ColorResolveRule::None &&
               d.samplePattern == 0;
    if (d.representation == HistoryRepresentation::PerSample)
        return d.samples > 1 && d.resolve == ColorResolveRule::None && d.samplePattern != 0;
    if (d.representation == HistoryRepresentation::ResolvedColor)
        return d.samples > 1 && d.color && !d.depth && !d.stencil &&
               (d.resolve == ColorResolveRule::LinearUnorm || d.resolve == ColorResolveRule::Srgb);
    return false;
}
inline bool ValidHistoryData(const HistoryData& data) {
    const auto& d = data.description;
    if (!ValidHistoryDesc(d))
        return false;
    const std::uint64_t pixels =
        std::uint64_t(d.width) * d.height *
        (d.representation == HistoryRepresentation::PerSample ? d.samples : 1);
    return data.rgba.size() == (d.color ? pixels * 4 : 0) &&
           data.depthStencil.size() == (d.depth || d.stencil ? pixels : 0);
}
inline bool CanCaptureHistory(const HistoryCapabilities& c, const HistoryDesc& d) {
    if (!ValidHistoryDesc(d) || d.samples > c.maxSamples)
        return false;
    if (d.representation == HistoryRepresentation::ResolvedColor)
        return c.resolvedColorCapture &&
               (d.resolve == ColorResolveRule::Srgb ? c.srgbResolve : c.linearResolve);
    if (d.representation == HistoryRepresentation::PerSample && !c.perSampleCapture)
        return false;
    const auto readable = [](HistoryAccess a) {
        return a == HistoryAccess::Native || a == HistoryAccess::DriverExtension;
    };
    return (!d.color || readable(c.colorCapture)) && (!d.depth || readable(c.depthCapture)) &&
           (!d.stencil || readable(c.stencilCapture));
}
inline bool CanRestoreHistory(const HistoryCapabilities& c, const HistoryDesc& source,
                              const HistoryDesc& target) {
    if (!ValidHistoryDesc(source) || !ValidHistoryDesc(target) ||
        source.representation == HistoryRepresentation::ResolvedColor ||
        source.width != target.width || source.height != target.height ||
        source.samples != target.samples || source.quality != target.quality ||
        source.representation != target.representation ||
        source.samplePattern != target.samplePattern || target.samples > c.maxSamples ||
        (target.color && !source.color) || (target.depth && !source.depth) ||
        (target.stencil && !source.stencil))
        return false;
    if (target.representation == HistoryRepresentation::PerSample && !c.perSampleRestore)
        return false;
    const auto writable = [](HistoryAccess a) {
        return a == HistoryAccess::Native || a == HistoryAccess::DriverExtension;
    };
    return (!target.color || writable(c.colorRestore)) &&
           (!target.depth || writable(c.depthRestore)) &&
           (!target.stencil || writable(c.stencilRestore));
}
enum class SamplerFilter {
    Point,
    Linear,
    Anisotropic
};
enum class SamplerAddress {
    Wrap,
    Clamp
};
struct SamplerDesc {
    SamplerFilter min = SamplerFilter::Anisotropic;
    SamplerFilter mag = SamplerFilter::Anisotropic;
    SamplerFilter mip = SamplerFilter::Anisotropic;
    SamplerAddress u = SamplerAddress::Wrap;
    SamplerAddress v = SamplerAddress::Wrap;
    std::uint32_t maxAnisotropy = 1;
};
struct Capabilities {
    bool shaders = false;
    bool compute = false;
    bool textureRenderTargets = false;
    bool deviceLoss = false;
};
// Reports implemented adapter behavior, not all capabilities of its native API.
// Ready describes the adapter reset protocol; it is not a native GPU health probe.
enum class DeviceState {
    Unknown,
    Ready,
    ResetPending,
    Removed
};
struct DeviceStatus {
    std::uint64_t identity = 0, epoch = 0;
    DeviceState state = DeviceState::Unknown;
};
struct ResourceLifecycleCapabilities {
    bool resetProtocol = false;
    bool retainsStaticBuffers = false;
    // Existing Rgba8 textures only; this does not promise render-target retention.
    bool retainsTextures = false;
    bool invalidatesDynamicBuffers = false;
    // DX9 wrapper borrows the native device; the application performs Reset.
    bool ownsNativeReset = false;
};
struct SubmissionIdentity {
    std::uint64_t device = 0, epoch = 0;
};
inline bool IsCurrentSubmission(DeviceStatus status, SubmissionIdentity submission) {
    return status.state == DeviceState::Ready && status.identity != 0 && status.epoch != 0 &&
           submission.device == status.identity && submission.epoch == status.epoch;
}
struct BufferDesc {
    BufferUsage usage = BufferUsage::Vertex;
    std::size_t size = 0;
    bool dynamic = false;
    IndexFormat indexFormat = IndexFormat::UInt16;
};
struct TextureDesc {
    enum class Mips {
        SingleLevel,
        GenerateLinear
    };
    std::uint32_t width = 0, height = 0;
    TextureFormat format = TextureFormat::Rgba8;
    Mips mips = Mips::SingleLevel;
};
// Rgba8 uses four bytes per pixel in R,G,B,A order at the portable boundary.
// Backend upload code performs any native channel swizzle.
struct TexturePixels {
    const void* data = nullptr;
    std::size_t rowPitch = 0;
    std::size_t bytes = 0;
};
struct Handle {
    std::uint32_t index = 0, generation = 0;
    std::uint64_t device = 0;
    explicit operator bool() const { return generation != 0 && device != 0; }
};
namespace detail {
inline std::uint64_t NextDeviceIdentity() {
    static std::atomic<std::uint64_t> next{1};
    std::uint64_t identity = next.fetch_add(1, std::memory_order_relaxed);
    while (identity == 0)
        identity = next.fetch_add(1, std::memory_order_relaxed);
    return identity;
}
}
enum class TextureResidency {
    Unavailable,
    Retained,
    Reupload,
    DeviceReplaced
};
inline TextureResidency ClassifyTexture(DeviceStatus current, DeviceStatus uploaded,
                                        ResourceLifecycleCapabilities caps) {
    if (current.state != DeviceState::Ready || !current.identity || !current.epoch)
        return TextureResidency::Unavailable;
    if (current.identity != uploaded.identity)
        return TextureResidency::DeviceReplaced;
    if (current.epoch != uploaded.epoch && !caps.retainsTextures)
        return TextureResidency::Reupload;
    return TextureResidency::Retained;
}
struct ClearColor {
    float red = 0, green = 0, blue = 0, alpha = 1;
};
struct IndexedDraw {
    Handle vertices, indices;
    std::uint32_t firstIndex = 0, indexCount = 0;
    std::uint32_t vertexCount = 0;
    VertexLayout layout = VertexLayout::PositionColor;
    SubmissionIdentity submission{};
};
struct FixedPipelineState {
    PrimitiveTopology topology = PrimitiveTopology::LineList;
    VertexLayout layout = VertexLayout::ScreenPositionColor;
    bool alphaBlend = true;
    bool depthTest = false;
    bool depthWrite = false;
    CompareOp depthCompare = CompareOp::Always;
    RasterCull cull = RasterCull::None;
    bool lighting = false;
    FixedColorStage colorStage = FixedColorStage::DiffuseOnly;
    bool alphaTest = false;
    CompareOp alphaCompare = CompareOp::Always;
    std::uint8_t alphaReference = 0;
    bool stencilTest = false;
    CompareOp stencilCompare = CompareOp::Always;
    std::uint32_t stencilReference = 0;
    std::uint32_t stencilReadMask = 0xff;
    std::uint32_t stencilWriteMask = 0xffffffff;
    StencilOperation stencilPass = StencilOperation::Keep;
    SamplerDesc sampler{};
};
constexpr FixedPipelineState SelectionBoxPipeline() {
    FixedPipelineState state{};
    state.depthTest = true;
    state.depthWrite = true;
    state.depthCompare = CompareOp::LessEqual;
    state.cull = RasterCull::CounterClockwise;
    state.lighting = true;
    state.colorStage = FixedColorStage::TextureDiffuseModulate;
    state.alphaTest = true;
    state.alphaCompare = CompareOp::GreaterEqual;
    state.alphaReference = 1;
    state.stencilTest = true;
    state.stencilCompare = CompareOp::Always;
    state.stencilReference = 1;
    state.stencilPass = StencilOperation::Replace;
    return state;
}
constexpr FixedPipelineState HudSpritePipeline() {
    FixedPipelineState state = SelectionBoxPipeline();
    state.topology = PrimitiveTopology::TriangleList;
    state.layout = VertexLayout::ScreenPositionColorUv;
    return state;
}
struct NonIndexedDraw {
    Handle vertices;
    std::uint32_t firstVertex = 0, vertexCount = 0;
    SubmissionIdentity submission{};
    // Legacy fixed draws inherit their pipeline layout. Model draws must name
    // their layout explicitly; a 16-byte model vertex is not a HUD vertex.
    std::optional<VertexLayout> layout;
};
struct ModelCapabilities {
    bool supported = false, positionColor = false, positionNormalUv = false;
    bool indexed = false, nonIndexed = false, lines = false;
    bool explicitMips = false, generateMips = false, srgb = false;
    bool offscreen = false, presentation = false;
    std::uint32_t maxDraws = 0;
};
// Completion is independent of target epoch: resizing does not change which
// queue/device submitted prior work. Only the backend determines completion.
struct CompletionToken {
    std::uint64_t device = 0, value = 0;
    explicit operator bool() const { return device && value; }
};
enum class CompletionState {
    Invalid,
    Pending,
    Complete,
    DeviceLost
};

struct ModelPipeline;
struct ModelTextureData;
struct RgbaImage;
struct RenderTextureDesc;
struct RenderPassDesc;
struct AttachmentReadback;
class Device {
public:
    virtual ~Device() = default;
    virtual Backend BackendType() const = 0;
    virtual Capabilities Features() const = 0;
    virtual DeviceStatus Status() const { return {}; }
    virtual ResourceLifecycleCapabilities ResourceLifecycleFeatures() const { return {}; }
    // Unsupported by default. Capabilities describe implemented operations,
    // never an API's hypothetical ability or the presence of MSAA alone.
    virtual HistoryCapabilities HistoryFeatures() const { return {}; }
    virtual HistoryResult CaptureHistory(const HistoryDesc&, HistoryData& output) {
        output = {};
        return HistoryResult::Unsupported;
    }
    virtual HistoryResult RestoreHistory(const HistoryDesc&, const HistoryData&) {
        return HistoryResult::Unsupported;
    }
    virtual Handle CreateBuffer(BufferDesc description, const void* initialData = nullptr) = 0;
    virtual Handle CreateTexture(TextureDesc description) = 0;
    virtual bool UploadTexture(Handle texture, TexturePixels pixels) = 0;
    virtual bool BindTexture(std::uint32_t slot, Handle texture) = 0;
    virtual bool Upload(Handle buffer, std::size_t offset, const void* data, std::size_t size) = 0;
    virtual bool Destroy(Handle resource) = 0;
    virtual bool BeginFrame(ClearColor clear) = 0;
    virtual bool SetFixedPipeline(FixedPipelineState pipeline) = 0;
    // Stamp when recording, never when executing an already recorded command.
    NonIndexedDraw Record(NonIndexedDraw draw) const {
        const auto status = Status();
        draw.submission = {status.identity, status.epoch};
        return draw;
    }
    IndexedDraw Record(IndexedDraw draw) const {
        const auto status = Status();
        draw.submission = {status.identity, status.epoch};
        return draw;
    }
    virtual bool Draw(NonIndexedDraw draw) = 0;
    virtual bool DrawIndexed(IndexedDraw draw) = 0;
    virtual bool SetModelPipeline(const ModelPipeline&) { return false; }
    virtual ModelCapabilities ModelFeatures() const { return {}; }
    virtual Handle CreateModelTexture(const ModelTextureData&) { return {}; }
    // Idempotent for an unchanged target. Return failure before accepting
    // submissions for an unsupported attachment description.
    virtual bool EnsureModelTarget(const HistoryDesc&) { return false; }
    // The swapchain controller owns resizing. This selects its current
    // attachments only if their dimensions/formats match the scene contract.
    virtual bool EnsurePresentationTarget(const HistoryDesc&) { return false; }
    virtual bool ReadModelTarget(RgbaImage&) { return false; }
    // Explicit single-queue passes. Unsupported backends fail before recording.
    // Begin/EndRenderPass are nested inside Begin/EndFrame, never each other.
    virtual Handle CreateRenderTexture(const RenderTextureDesc&) { return {}; }
    virtual bool BeginRenderPass(const RenderPassDesc&) { return false; }
    virtual bool EndRenderPass() { return false; }
    virtual bool ReadRenderTexture(Handle, AttachmentReadback&) { return false; }
    virtual CompletionToken SignalCompletion() { return {}; }
    virtual CompletionState PollCompletion(CompletionToken) { return CompletionState::Invalid; }
    virtual bool WaitCompletion(CompletionToken, std::uint32_t) { return false; }
    virtual bool EndFrame() = 0;
    virtual bool Present() = 0;
    virtual bool PrepareForReset() = 0;
    virtual bool RestoreAfterReset() = 0;
};
std::unique_ptr<Device> CreateNullDevice();
}
