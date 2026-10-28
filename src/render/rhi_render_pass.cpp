#include "mikudancestudio/rhi_render_pass.hpp"
#include <cmath>
#include <set>

namespace mikudancestudio::rhi {
namespace {
bool Has(const RenderTextureDesc& d, TextureAccess a) {
    return (d.access & std::uint32_t(a)) != 0;
}
bool ValidColor(ClearColor c) {
    for (float v : {c.red, c.green, c.blue, c.alpha})
        if (!std::isfinite(v) || v < 0 || v > 1)
            return false;
    return true;
}
}
bool ValidRenderTextureDesc(const RenderTextureDesc& d) {
    if (!d.width || !d.height || d.width > 4096 || d.height > 4096 || d.mips != 1 ||
        d.layers != 1 || d.samples != 1 || !d.access || (d.access & ~15u))
        return false;
    const bool depth = d.format == TextureFormat::Depth24Stencil8;
    if (!depth && d.format != TextureFormat::Rgba8 && d.format != TextureFormat::R32Float)
        return false;
    if (depth)
        return Has(d, TextureAccess::DepthStencilAttachment) &&
               !Has(d, TextureAccess::ColorAttachment) && !Has(d, TextureAccess::Sampled);
    return Has(d, TextureAccess::ColorAttachment) && !Has(d, TextureAccess::DepthStencilAttachment);
}
bool EqualRenderTextureDesc(const RenderTextureDesc& a, const RenderTextureDesc& b) {
    return a.width == b.width && a.height == b.height && a.format == b.format &&
           a.access == b.access && a.mips == b.mips && a.layers == b.layers &&
           a.samples == b.samples;
}
bool RenderPassState::Register(Handle h, const RenderTextureDesc& d) {
    return !active_ && h && ValidRenderTextureDesc(d) && entries_.emplace(key(h), Entry{d}).second;
}
bool RenderPassState::Erase(Handle h) {
    return !active_ && entries_.erase(key(h)) != 0;
}
const RenderTextureDesc* RenderPassState::Description(Handle h) const {
    const auto i = entries_.find(key(h));
    return i == entries_.end() ? nullptr : &i->second.desc;
}
bool RenderPassState::Begin(const RenderPassDesc& p) {
    const auto fail = [&](const char* why) {
        reason_ = why;
        return false;
    };
    if (active_)
        return fail("nested_render_pass");
    if (!p.color && !p.depthStencil)
        return fail("missing_attachment");
    std::set<Key> writes, reads;
    std::uint32_t width = 0, height = 0;
    for (const auto* a :
         {p.color ? &*p.color : nullptr, p.depthStencil ? &*p.depthStencil : nullptr}) {
        if (!a)
            continue;
        const auto i = entries_.find(key(a->texture));
        if (i == entries_.end())
            return fail("invalid_attachment_handle");
        const auto& d = i->second.desc;
        const bool depth = a == (p.depthStencil ? &*p.depthStencil : nullptr);
        if (!Has(d, depth ? TextureAccess::DepthStencilAttachment : TextureAccess::ColorAttachment))
            return fail("attachment_usage_mismatch");
        if (!writes.insert(i->first).second)
            return fail("duplicate_attachment");
        if (width && (d.width != width || d.height != height))
            return fail("attachment_size_mismatch");
        width = d.width;
        height = d.height;
        if (unsigned(a->load) > unsigned(AttachmentLoad::Discard) ||
            unsigned(a->store) > unsigned(AttachmentStore::Discard))
            return fail("invalid_attachment_operation");
        // A discarded load does not initialize all texels; without an explicit
        // full-coverage promise it must remain discarded at the pass boundary.
        if (a->load == AttachmentLoad::Discard && a->store == AttachmentStore::Store)
            return fail("undefined_attachment_store");
        if (a->load == AttachmentLoad::Load && !i->second.initialized)
            return fail("load_before_write");
        if (a->load == AttachmentLoad::Clear &&
            (depth ? !std::isfinite(a->clearDepth) || a->clearDepth < 0 || a->clearDepth > 1
                   : !ValidColor(a->clear)))
            return fail("invalid_attachment_clear");
    }
    for (const auto h : p.sampled) {
        const auto i = entries_.find(key(h));
        if (i == entries_.end())
            return fail("invalid_sampled_handle");
        if (!reads.insert(i->first).second)
            return fail("duplicate_sampled_resource");
        if (writes.count(i->first))
            return fail("attachment_feedback");
        if (!Has(i->second.desc, TextureAccess::Sampled))
            return fail("sample_usage_mismatch");
        if (!i->second.initialized)
            return fail("sample_before_write");
    }
    current_ = p;
    active_ = true;
    reason_.clear();
    return true;
}
bool RenderPassState::End() {
    if (!active_) {
        reason_ = "no_render_pass";
        return false;
    }
    for (const auto* a : {current_.color ? &*current_.color : nullptr,
                          current_.depthStencil ? &*current_.depthStencil : nullptr})
        if (a)
            entries_.at(key(a->texture)).initialized = a->store == AttachmentStore::Store;
    active_ = false;
    current_ = {};
    return true;
}
bool RenderPassState::CanSample(Handle h) const {
    if (!active_)
        return false;
    for (auto r : current_.sampled)
        if (key(r) == key(h))
            return true;
    return false;
}
bool RenderPassState::CanRead(Handle h) const {
    const auto i = entries_.find(key(h));
    return !active_ && i != entries_.end() && i->second.initialized &&
           Has(i->second.desc, TextureAccess::CopySource);
}
void RenderPassState::InvalidateContents() {
    for (auto& i : entries_)
        i.second.initialized = false;
    active_ = false;
    current_ = {};
}
}
