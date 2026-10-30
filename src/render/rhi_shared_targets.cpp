#include "mikudancestudio/rhi_shared_targets.hpp"
#include <algorithm>
#include <atomic>
namespace mikudancestudio::rhi {
namespace {
std::uint64_t Epoch() {
    static std::atomic<std::uint64_t> next{1};
    return next++;
}
bool Same(const RenderTextureDesc& a, const RenderTextureDesc& b) {
    return EqualRenderTextureDesc(a, b);
}
}
SharedFrameTargets::SharedFrameTargets(RenderFrame& f) : frame(f), epoch(Epoch()) {
}
void SharedFrameTargets::Reset() {
    declared.clear();
    epoch = Epoch();
}
bool SharedFrameTargets::Declare(std::uint64_t identity, const RenderTextureDesc& desc,
                                 SharedTargetLease& lease, std::string& reason) {
    lease = {};
    const auto id = SharedFrameTargetId(identity);
    if (!id || !ValidRenderTextureDesc(desc)) {
        reason = "invalid_shared_target_declaration";
        return false;
    }
    auto found = std::find_if(frame.targets.begin(), frame.targets.end(),
                              [&](const auto& t) { return t.id == id; });
    if (found != frame.targets.end() && (!Same(found->description, desc) || found->history)) {
        reason = "conflicting_shared_target_declaration";
        return false;
    }
    if (found == frame.targets.end())
        frame.targets.push_back({id, desc});
    declared[identity] = desc;
    lease = {identity, epoch};
    reason.clear();
    return true;
}
bool SharedFrameTargets::Resolve(SharedTargetLease lease, RenderTargetReference& ref,
                                 std::string& reason) const {
    ref = {};
    const auto found = declared.find(lease.identity);
    if (lease.epoch != epoch || found == declared.end()) {
        reason = "stale_shared_target_reference";
        return false;
    }
    const auto id = SharedFrameTargetId(lease.identity);
    const auto target = std::find_if(frame.targets.begin(), frame.targets.end(),
                                     [&](const auto& t) { return t.id == id; });
    if (target == frame.targets.end() || target->history ||
        !Same(target->description, found->second)) {
        reason = "stale_shared_target_reference";
        return false;
    }
    ref = {id, found->second.width, found->second.height};
    reason.clear();
    return true;
}
}
