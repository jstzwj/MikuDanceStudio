#include "mikudancestudio/rhi_image.hpp"

#include <climits>
#include <cstring>
#include <algorithm>
#include <array>
#include <cmath>

#define STBI_ONLY_PNG
#define STBI_ONLY_BMP
#define STBI_NO_STDIO
#define STB_IMAGE_IMPLEMENTATION
#if defined(__GNUC__)
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wunused-function"
#endif
#include "../../third_party/stb/stb_image.h"
#if defined(__GNUC__)
#pragma GCC diagnostic pop
#endif

namespace mikudancestudio::rhi {
namespace {
// True-color TGA, including RLE, with explicit raster bounds/origin. Keep
// unsupported palettes/bit depths separate rather than guess their semantics.
bool DecodeTrueColorTga(const std::uint8_t* source, std::size_t length, RgbaImage& output) {
    if (length < 18 || source[1] != 0 || (source[2] != 2 && source[2] != 10))
        return false;
    const unsigned width = source[12] | unsigned(source[13]) << 8,
                   height = source[14] | unsigned(source[15]) << 8;
    const unsigned bits = source[16], attributes = source[17] & 15;
    if (!width || !height || width > 4096 || height > 4096 || (bits != 24 && bits != 32) ||
        (source[17] & 0xc0) || (bits == 24 && attributes) ||
        (bits == 32 && attributes != 0 && attributes != 8))
        return false;
    const unsigned stride = bits / 8;
    std::size_t cursor = 18 + source[0], pixel = 0, total = std::size_t(width) * height;
    if (cursor > length)
        return false;
    RgbaImage result{width, height, std::vector<std::uint8_t>(total * 4)};
    const auto write = [&](const std::uint8_t* value) {
        auto x = pixel % width, y = pixel / width;
        if (source[17] & 0x10)
            x = width - 1 - x;
        if (!(source[17] & 0x20))
            y = height - 1 - y;
        auto* target = result.pixels.data() + (y * width + x) * 4;
        target[0] = value[2];
        target[1] = value[1];
        target[2] = value[0];
        target[3] = bits == 32 && attributes == 8 ? value[3] : 255;
        ++pixel;
    };
    while (pixel < total) {
        std::size_t count = 1;
        bool repeated = false;
        if (source[2] == 10) {
            if (cursor >= length)
                return false;
            const auto packet = source[cursor++];
            count = (packet & 127) + 1;
            repeated = (packet & 128) != 0;
        }
        if (count > total - pixel)
            return false;
        const auto bytes = (repeated ? 1 : count) * stride;
        if (bytes > length - cursor)
            return false;
        for (std::size_t n = 0; n < count; ++n)
            write(source + cursor + (repeated ? 0 : n * stride));
        cursor += bytes;
    }
    output = std::move(result);
    return true;
}
}
bool DecodeTextureRgba(const void* bytes, std::size_t length, RgbaImage& image) {
    image = {};
    if (!bytes || length == 0 || length > INT_MAX)
        return false;
    const auto* input = static_cast<const stbi_uc*>(bytes);
    const bool png = length >= 8 && std::memcmp(input, "\x89PNG\r\n\x1a\n", 8) == 0;
    const bool bmp = length >= 54 && input[0] == 'B' && input[1] == 'M';
    if (!png && !bmp)
        return DecodeTrueColorTga(input, length, image);
    if (bmp) {
        const auto u32 = [&](unsigned at) {
            return static_cast<std::uint32_t>(input[at]) |
                   static_cast<std::uint32_t>(input[at + 1]) << 8 |
                   static_cast<std::uint32_t>(input[at + 2]) << 16 |
                   static_cast<std::uint32_t>(input[at + 3]) << 24;
        };
        const auto declared = u32(2), offset = u32(10), header = u32(14);
        if (declared > length || offset >= length || header < 40 || header > length - 14 ||
            offset < header + 14)
            return false;
        // stb can fill missing BMP rows with zeroes. Reject truncated raster data.
        const auto width = u32(18);
        const auto rawHeight = u32(22);
        const auto height = rawHeight & 0x80000000u ? 0u - rawHeight : rawHeight;
        const unsigned bits = input[28] | input[29] << 8;
        if (!width || width > 4096 || !height || height > 4096 ||
            (bits != 1 && bits != 4 && bits != 8 && bits != 16 && bits != 24 && bits != 32) ||
            (static_cast<std::uint64_t>(width) * bits + 31) / 32 * 4 * height > length - offset)
            return false;
    }
    int width = 0, height = 0, channels = 0;
    if (!stbi_info_from_memory(input, static_cast<int>(length), &width, &height, &channels))
        return false;
    if (bmp && height < 0 && height != INT_MIN)
        height = -height;
    if (width <= 0 || height <= 0 || width > 4096 || height > 4096)
        return false;
    stbi_uc* decoded =
        stbi_load_from_memory(input, static_cast<int>(length), &width, &height, &channels, 4);
    if (!decoded)
        return false;
    image.width = static_cast<std::uint32_t>(width);
    image.height = static_cast<std::uint32_t>(height);
    const std::size_t count = static_cast<std::size_t>(width) * height * 4;
    image.pixels.assign(decoded, decoded + count);
    stbi_image_free(decoded);
    return true;
}
bool DecodePngRgba(const void* bytes, std::size_t length, RgbaImage& image) {
    image = {};
    return bytes && length >= 8 && std::memcmp(bytes, "\x89PNG\r\n\x1a\n", 8) == 0 &&
           DecodeTextureRgba(bytes, length, image);
}
std::uint64_t ImageRevision(const RgbaImage& image) {
    std::uint64_t revision = image.pixels.Fingerprint();
    revision = (revision ^ image.width) * 1099511628211ull;
    revision = (revision ^ image.height) * 1099511628211ull;
    return revision ? revision : 1;
}
bool NextPowerOfTwoTextureSize(std::uint32_t source, std::uint32_t limit, std::uint32_t& target) {
    target = 0;
    if (!source || !limit || source > 4096 || limit > 4096)
        return false;
    std::uint32_t rounded = 1;
    while (rounded < source)
        rounded *= 2;
    if (rounded > limit)
        return false;
    target = rounded;
    return true;
}
bool PrepareTriangleDitherWrap(const RgbaImage& source, std::uint32_t width, std::uint32_t height,
                               RgbaImage& prepared) {
    // Clear failure output, and allow source/output aliasing without clearing input.
    if (&source == &prepared) {
        const RgbaImage retained = source;
        return PrepareTriangleDitherWrap(retained, width, height, prepared);
    }
    prepared = {};
    if (!source.width || !source.height || source.width > 4096 || source.height > 4096 || !width ||
        !height || width > 4096 || height > 4096 || width < source.width ||
        height < source.height || width > source.width * 2 || height > source.height * 2 ||
        source.pixels.size() != static_cast<std::size_t>(source.width) * source.height * 4)
        return false;
    if (width == source.width && height == source.height) {
        prepared = source;
        return true;
    }
    struct Tap {
        unsigned index;
        double weight;
    };
    using Taps = std::vector<Tap>;
    const auto build = [](unsigned from, unsigned to) {
        std::vector<Taps> result(to);
        for (unsigned at = 0; at < to; ++at) {
            // An unchanged axis must not introduce a blur into a 1D resize.
            if (from == to) {
                result[at].push_back({at, 1});
                continue;
            }
            const double low = static_cast<double>(at) * from / to - 0.5;
            const double high = static_cast<double>(at + 1) * from / to - 0.5;
            const auto add = [&](int index, double value) {
                const auto wrapped = static_cast<unsigned>((index + static_cast<int>(from)) % from);
                for (auto& tap : result[at])
                    if (tap.index == wrapped) {
                        tap.weight += value;
                        return;
                    }
                result[at].push_back({wrapped, value});
            };
            for (int i = static_cast<int>(std::floor(low)); i < std::ceil(high); ++i) {
                const double a = std::max(low, static_cast<double>(i)) - i;
                const double b = std::min(high, static_cast<double>(i + 1)) - i;
                const double upper = (b * b - a * a) / (2 * (high - low));
                add(i, (b - a) / (high - low) - upper);
                add(i + 1, upper);
            }
        }
        return result;
    };
    const auto horizontal = build(source.width, width), vertical = build(source.height, height);
    prepared.width = width;
    prepared.height = height;
    prepared.pixels.resize(static_cast<std::size_t>(width) * height * 4);
    // Measured D3DX ordered thresholds. The pattern is absolute destination
    // texel position, shared by R/G/B/A, with no random or content-specific fix.
    constexpr unsigned thresholds[4][4] = {
        {31, 15, 27, 11}, {19, 3, 23, 7}, {25, 9, 29, 13}, {21, 5, 17, 1}};
    // Identity-size RGBA32F measurements match reciprocal multiplication,
    // not division (e.g. byte 127 differs by one float ULP). This aligns the
    // conversion stage only; legacy coefficient arithmetic is still gated.
    constexpr float byteToFloat = 1.0f / 255.0f;
    for (unsigned y = 0; y < height; ++y)
        for (unsigned x = 0; x < width; ++x) {
            std::array<float, 4> value{};
            for (const auto& row : vertical[y]) {
                std::array<float, 4> scanline{};
                for (const auto& column : horizontal[x]) {
                    const auto* pixel =
                        source.pixels.data() +
                        (static_cast<std::size_t>(row.index) * source.width + column.index) * 4;
                    for (unsigned channel = 0; channel < 4; ++channel)
                        scanline[channel] +=
                            (pixel[channel] * byteToFloat) * static_cast<float>(column.weight);
                }
                for (unsigned channel = 0; channel < 4; ++channel)
                    value[channel] += scanline[channel] * static_cast<float>(row.weight);
            }
            auto* pixel = prepared.pixels.data() + (static_cast<std::size_t>(y) * width + x) * 4;
            for (unsigned channel = 0; channel < 4; ++channel) {
                const float quantized =
                    std::floor(value[channel] * 255.0f + thresholds[y % 4][x % 4] / 32.0f);
                pixel[channel] = static_cast<std::uint8_t>(std::clamp(quantized, 0.0f, 255.0f));
            }
        }
    return true;
}
}
