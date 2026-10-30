#pragma once
#include "rhi.hpp"
#include <map>
#include <string>
#include <tuple>

namespace mikudancestudio::rhi {
enum class TextureAccess : std::uint32_t {
    ColorAttachment = 1,
    DepthStencilAttachment = 2,
    Sampled = 4,
    CopySource = 8
};
constexpr std::uint32_t operator|(TextureAccess a, TextureAccess b) {
    return std::uint32_t(a) | std::uint32_t(b);
}
constexpr std::uint32_t operator|(std::uint32_t a, TextureAccess b) {
    return a | std::uint32_t(b);
}
struct RenderTextureDesc {
    std::uint32_t width = 0, height = 0;
    TextureFormat format = TextureFormat::Rgba8;
    std::uint32_t access = std::uint32_t(TextureAccess::ColorAttachment);
    // Single layer/sample. Color targets may expose a complete mip chain.
    // Unsupported ranges are rejected, never silently narrowed.
    std::uint32_t mips = 1, layers = 1, samples = 1;
};
bool ValidRenderTextureDesc(const RenderTextureDesc&);
bool EqualRenderTextureDesc(const RenderTextureDesc&, const RenderTextureDesc&);
enum class AttachmentLoad {
    Load,
    Clear,
    Discard
};
enum class AttachmentStore {
    Store,
    Discard
};
struct PassAttachment {
    Handle texture;
    AttachmentLoad load = AttachmentLoad::Clear;
    AttachmentStore store = AttachmentStore::Store;
    ClearColor clear;
    float clearDepth = 1;
    std::uint8_t clearStencil = 0;
};
struct RenderPassDesc {
    // Legacy single-color spelling. Do not combine with colors.
    std::optional<PassAttachment> color, depthStencil;
    // Every render-texture sample must be declared here, including Load passes.
    // Same-pass feedback and reading undefined/discarded contents are invalid.
    std::vector<Handle> sampled;
    // Dense ordered attachments, mapped to SV_Target0..N-1. Common blend and
    // write-mask state applies to every target; independent blending is absent.
    std::vector<PassAttachment> colors;
};
template <class Pass> auto ColorAttachments(const Pass& p) {
    auto result = p.colors;
    if (p.color)
        result.insert(result.begin(), *p.color);
    return result;
}
template <class Pass> const auto* PrimaryAttachment(const Pass& p) {
    return p.color             ? &*p.color
           : !p.colors.empty() ? &p.colors.front()
           : p.depthStencil    ? &*p.depthStencil
                               : nullptr;
}
struct AttachmentReadback {
    RenderTextureDesc description;
    // Tightly packed native-value rows: RGBA8, IEEE binary16 RGBA (8 bytes),
    // IEEE float32 R32, or D24(low)/S8(high). No UNORM conversion for floats.
    std::vector<std::uint8_t> bytes;
};
// Shared validation for Null and native backends. The owner registers only
// successfully created native resources. Validation has no native side effects;
// initialization is committed at End, after the backend accepts the pass.
class RenderPassState {
public:
    bool Register(Handle, const RenderTextureDesc&);
    bool Erase(Handle);
    bool Begin(const RenderPassDesc&);
    bool End();
    bool CanGenerateMips(Handle) const;
    bool GenerateMips(Handle);
    bool Active() const { return active_; }
    bool CanSample(Handle) const;
    bool CanRead(Handle) const;
    const RenderTextureDesc* Description(Handle) const;
    std::vector<TextureFormat> ColorFormats() const;
    const RenderPassDesc& Current() const { return current_; }
    const std::string& Reason() const { return reason_; }
    void InvalidateContents();

private:
    using Key = std::tuple<std::uint64_t, std::uint32_t, std::uint32_t>;
    struct Entry {
        RenderTextureDesc desc;
        bool initialized = false;
        bool mipsReady = false;
    };
    static Key key(Handle h) { return {h.device, h.index, h.generation}; }
    std::map<Key, Entry> entries_;
    bool active_ = false;
    RenderPassDesc current_;
    std::string reason_;
};
}
