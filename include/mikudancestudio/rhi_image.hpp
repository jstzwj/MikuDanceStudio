#pragma once
#include <cstddef>
#include <cstdint>
#include <vector>
#include <memory>
#include "rhi_snapshot_bytes.hpp"

namespace mikudancestudio::rhi {
struct RgbaImage {
    std::uint32_t width = 0;
    std::uint32_t height = 0;
    SnapshotBytes pixels;
};
// Explicit levels are immutable CPU upload data, even when an optional backend
// service prepared them. Native generation is a distinct compatibility recipe,
// never an alias for the portable box filter or a captured scene texture.
enum class MipPreparationPolicy {
    SingleLevel,
    BoxLinear,
    Dx9AutoLinear
};
struct PreparedMipChain {
    MipPreparationPolicy policy = MipPreparationPolicy::SingleLevel;
    std::uint64_t sourceRevision = 0, providerRevision = 0, revision = 0;
    std::vector<RgbaImage> levels;
};
bool PreparePortableMipChain(const RgbaImage&, MipPreparationPolicy, PreparedMipChain&);
bool SealPreparedMipChain(const RgbaImage&, MipPreparationPolicy, std::uint64_t providerRevision,
                          std::vector<RgbaImage>, PreparedMipChain&);
bool ValidatePreparedMipChain(const RgbaImage&, const PreparedMipChain&);
// Optional backend service. Raw decode and explicit target size remain owned
// by the caller. Failure clears output; it never authorizes a parity gate.
class TexturePreparer {
public:
    virtual ~TexturePreparer() = default;
    virtual bool Prepare(const RgbaImage& source, std::uint32_t width, std::uint32_t height,
                         RgbaImage& prepared) = 0;
    virtual bool SupportsMipPolicy(MipPreparationPolicy) const { return false; }
    // Backend receives only CPU source bytes. sceneActive describes submission
    // scope for state restoration; output owns every level and no native handle.
    virtual bool PrepareMips(const RgbaImage&, MipPreparationPolicy, PreparedMipChain& output,
                             bool sceneActive = false) {
        output = {};
        return false;
    }
};

// Decode a PNG asset at the portable boundary. Pixels are tightly packed RGBA8.
bool DecodePngRgba(const void* bytes, std::size_t length, RgbaImage& image);
// Content detection, independent of filename extension. Supports PNG, BMP and true-color TGA (raw/RLE).
bool DecodeTextureRgba(const void* bytes, std::size_t length, RgbaImage& image);
std::uint64_t ImageRevision(const RgbaImage& image);
// Experimental legacy resize candidate. Integrates the periodic bilinear
// reconstruction over destination texels, then applies ordered 4x4 dithering.
// Explicit dimensions come from a host policy, never from the decoder. It is
// NOT a claim of bit-identical D3DX filtering; callers must validate before use.
bool PrepareTriangleDitherWrap(const RgbaImage& source, std::uint32_t width, std::uint32_t height,
                               RgbaImage& prepared);
bool NextPowerOfTwoTextureSize(std::uint32_t source, std::uint32_t limit, std::uint32_t& target);
}
