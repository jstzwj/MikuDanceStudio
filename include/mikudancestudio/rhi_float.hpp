#pragma once
#include <cstdint>
#include <cstring>
namespace mikudancestudio::rhi {
// IEEE binary16, round to nearest with ties to even. Readback conversion only;
// GPU output remains raw binary16 and is never normalized through RGBA8.
inline std::uint16_t FloatToHalf(float value) {
    std::uint32_t bits;
    std::memcpy(&bits, &value, 4);
    const auto sign = (bits >> 16) & 0x8000u;
    const auto exponent = (bits >> 23) & 255u;
    auto mantissa = bits & 0x7fffffu;
    if (exponent == 255)
        return std::uint16_t(sign | 0x7c00u | (mantissa ? 0x200u : 0));
    int e = int(exponent) - 127 + 15;
    if (e >= 31)
        return std::uint16_t(sign | 0x7c00u);
    if (e < -10)
        return std::uint16_t(sign);
    if (e <= 0) {
        mantissa |= 0x800000u;
        const auto shift = unsigned(14 - e);
        const auto result = mantissa >> shift;
        const auto remainder = mantissa & ((1u << shift) - 1);
        const auto halfway = 1u << (shift - 1);
        return std::uint16_t(
            sign | (result + (remainder > halfway || (remainder == halfway && (result & 1)))));
    }
    const auto result = (unsigned(e) << 10) | (mantissa >> 13);
    const auto remainder = mantissa & 8191u;
    return std::uint16_t(sign |
                         (result + (remainder > 4096 || (remainder == 4096 && (result & 1)))));
}
inline float HalfToFloat(std::uint16_t h) {
    const auto sign = std::uint32_t(h & 0x8000u) << 16;
    unsigned e = (h >> 10) & 31u, m = h & 1023u;
    std::uint32_t bits;
    if (!e && m) {
        int exponent = -14;
        while (!(m & 1024u)) {
            m <<= 1;
            --exponent;
        }
        bits = sign | (unsigned(exponent + 127) << 23) | ((m & 1023u) << 13);
    } else if (!e)
        bits = sign;
    else if (e == 31)
        bits = sign | 0x7f800000u | (m << 13);
    else
        bits = sign | ((e + 112) << 23) | (m << 13);
    float result;
    std::memcpy(&result, &bits, 4);
    return result;
}
}
