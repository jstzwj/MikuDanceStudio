#pragma once
#include "rhi_frame_passes.hpp"
#include <map>

namespace mikudancestudio::rhi {
// Identity comes from the CPU resource owner, never a name or a native handle.
// A new resource revision must receive a new identity. Contents are frame-local.
inline std::uint64_t SharedFrameTargetId(std::uint64_t identity) {
    return identity && identity < (std::uint64_t(1) << 62)
               ? (std::uint64_t(1) << 62) | identity : 0;
}
struct SharedTargetLease {
    std::uint64_t identity = 0, epoch = 0;
};
class SharedFrameTargets {
public:
    explicit SharedFrameTargets(RenderFrame&);
    bool Declare(std::uint64_t identity, const RenderTextureDesc&, SharedTargetLease&,
                 std::string& reason);
    bool Resolve(SharedTargetLease, RenderTargetReference&, std::string& reason) const;
    // Invalidates all leases, including when a caller starts a replacement frame.
    void Reset();
private:
    RenderFrame& frame;
    std::uint64_t epoch;
    std::map<std::uint64_t, RenderTextureDesc> declared;
};
}
