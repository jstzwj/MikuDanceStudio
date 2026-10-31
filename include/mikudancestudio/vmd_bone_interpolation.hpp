#pragma once
#include <array>
#include <cstdint>
namespace mikudancestudio::format {
// Preserve the native VMD loader's four 16-byte channel blocks; the four
// control points become PMM's channel-major rows, not VMD's first 16 bytes.
inline std::array<std::uint8_t,16>
DecodeVmdBoneInterpolation(const std::array<std::uint8_t,64>& source) {
    std::array<std::uint8_t,16> result;
    for (int channel=0; channel<4; ++channel)
        for (int point=0; point<4; ++point)
            result[point*4+channel]=source[channel*16+point*4];
    return result;
}
}
