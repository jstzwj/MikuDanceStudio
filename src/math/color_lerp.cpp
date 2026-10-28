// ===========================================================================
// VA 0x0040AD00 - ColorLerp  (original: sub_40AD00)
// ===========================================================================
// __stdcall ColorLerp(COLORREF a, COLORREF b, float t)
// Per-channel `a - (a - b) * t` with double intermediate math and
// truncation to int, matching the original x87 code exactly.
// COLORREF layout: 0x00BBGGRR (R = byte0, G = byte1, B = byte2).
// ===========================================================================
#include <cstdint>

#include "mikudancestudio/ported_funcs.hpp"

namespace mikudancestudio {

std::uint32_t ColorLerp(std::uint32_t a, std::uint32_t b, float t) {
    // Original computes G and B through one 16-bit register pair; the
    // observable result is per-channel truncation, preserved here.
    const unsigned char rA = static_cast<unsigned char>(a & 0xFF);
    const unsigned char gA = static_cast<unsigned char>((a >> 8) & 0xFF);
    const unsigned char bA = static_cast<unsigned char>((a >> 16) & 0xFF);
    const unsigned char rB = static_cast<unsigned char>(b & 0xFF);
    const unsigned char gB = static_cast<unsigned char>((b >> 8) & 0xFF);
    const unsigned char bB = static_cast<unsigned char>((b >> 16) & 0xFF);

    const int r = static_cast<int>(static_cast<double>(rA) - static_cast<double>(rA - rB) * t);
    const int g = static_cast<int>(static_cast<double>(gA) - static_cast<double>(gA - gB) * t);
    const int bl = static_cast<int>(static_cast<double>(bA) - static_cast<double>(bA - bB) * t);

    return static_cast<std::uint32_t>((r & 0xFF) | ((g & 0xFF) << 8) | ((bl & 0xFF) << 16));
}

} // namespace mikudancestudio
