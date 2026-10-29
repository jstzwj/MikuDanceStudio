#include "mikudancestudio/rhi_image.hpp"
#include <algorithm>

namespace mikudancestudio::rhi {
namespace {
bool ImageValid(const RgbaImage& i) {
    return i.width && i.height && i.width <= 4096 && i.height <= 4096 &&
           i.pixels.size() == std::size_t(i.width) * i.height * 4;
}
void Hash(std::uint64_t& hash, std::uint64_t value) {
    for (unsigned i = 0; i < 8; ++i) {
        hash ^= (value >> (i * 8)) & 255;
        hash *= 1099511628211ull;
    }
}
std::uint64_t Revision(const PreparedMipChain& chain) {
    std::uint64_t h = 14695981039346656037ull;
    Hash(h, 1); // recipe schema
    Hash(h, unsigned(chain.policy));
    Hash(h, chain.sourceRevision);
    Hash(h, chain.providerRevision);
    Hash(h, chain.levels.size());
    for (const auto& i : chain.levels)
        Hash(h, ImageRevision(i));
    return h ? h : 1;
}
bool Shape(const RgbaImage& source, const PreparedMipChain& chain) {
    if (!ImageValid(source) || chain.sourceRevision != ImageRevision(source) ||
        !chain.providerRevision || chain.levels.empty())
        return false;
    if (chain.policy != MipPreparationPolicy::SingleLevel &&
        chain.policy != MipPreparationPolicy::BoxLinear &&
        chain.policy != MipPreparationPolicy::Dx9AutoLinear)
        return false;
    unsigned w = source.width, h = source.height;
    for (std::size_t n = 0; n < chain.levels.size(); ++n) {
        const auto& i = chain.levels[n];
        if (!ImageValid(i) || i.width != w || i.height != h ||
            (n == 0 && i.pixels != source.pixels))
            return false;
        if (n + 1 < chain.levels.size() &&
            ((w == 1 && h == 1) || chain.policy == MipPreparationPolicy::SingleLevel))
            return false;
        if (n + 1 == chain.levels.size())
            return chain.policy == MipPreparationPolicy::SingleLevel || (w == 1 && h == 1);
        w = (std::max)(1u, w / 2);
        h = (std::max)(1u, h / 2);
    }
    return false;
}
}
bool SealPreparedMipChain(const RgbaImage& source, MipPreparationPolicy policy,
                          std::uint64_t provider, std::vector<RgbaImage> levels,
                          PreparedMipChain& output) {
    PreparedMipChain candidate;
    candidate.policy = policy;
    candidate.sourceRevision = ImageRevision(source);
    candidate.providerRevision = provider;
    candidate.levels = std::move(levels);
    if (!Shape(source, candidate)) {
        output = {};
        return false;
    }
    candidate.revision = Revision(candidate);
    for (auto& level : candidate.levels)
        level.pixels.Seal();
    output = std::move(candidate);
    return true;
}
bool ValidatePreparedMipChain(const RgbaImage& source, const PreparedMipChain& chain) {
    return Shape(source, chain) && chain.revision == Revision(chain);
}
bool PreparePortableMipChain(const RgbaImage& source, MipPreparationPolicy policy,
                             PreparedMipChain& output) {
    if (!ImageValid(source) || (policy != MipPreparationPolicy::SingleLevel &&
                                policy != MipPreparationPolicy::BoxLinear)) {
        output = {};
        return false;
    }
    std::vector<RgbaImage> levels{source};
    while (policy == MipPreparationPolicy::BoxLinear &&
           (levels.back().width > 1 || levels.back().height > 1)) {
        const auto& from = levels.back();
        RgbaImage to{(std::max)(1u, from.width / 2), (std::max)(1u, from.height / 2), {}};
        to.pixels.resize(std::size_t(to.width) * to.height * 4);
        // Integer area partitions include odd-size edge texels. Linear UNORM,
        // independent channels (including alpha), round half up, no hidden gamma.
        for (unsigned y = 0; y < to.height; ++y)
            for (unsigned x = 0; x < to.width; ++x) {
                const auto x0 = x * from.width / to.width, x1 = (x + 1) * from.width / to.width;
                const auto y0 = y * from.height / to.height, y1 = (y + 1) * from.height / to.height;
                const auto count = (x1 - x0) * (y1 - y0);
                for (unsigned c = 0; c < 4; ++c) {
                    unsigned sum = 0;
                    for (unsigned sy = y0; sy < y1; ++sy)
                        for (unsigned sx = x0; sx < x1; ++sx)
                            sum += from.pixels[(std::size_t(sy) * from.width + sx) * 4 + c];
                    to.pixels[(std::size_t(y) * to.width + x) * 4 + c] =
                        std::uint8_t((sum + count / 2) / count);
                }
            }
        levels.push_back(std::move(to));
    }
    return SealPreparedMipChain(source, policy, 1, std::move(levels), output);
}
}
