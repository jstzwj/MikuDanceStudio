#include "../src/render/background_quad.hpp"
#include <array>
#include <cstdint>
#include <cstdio>
#include <cstring>

int main() {
    using namespace mikudancestudio;
    std::array<TexturedScreenVertex, 6> vertices;
    std::memset(vertices.data(), 0xCD, sizeof vertices);
    WriteBackgroundQuad(vertices.data(), -2.25, 3.5, 10.75, 20.125);
    // Expected bytes decoded from the original six 28-byte vertex stores, including RHW0.
    constexpr std::uint32_t expected[42] = {
        0x412C0000, 0x40600000, 0, 0x3F800000, 0xFFFFFFFF, 0x3F800000, 0,
        0x412C0000, 0x41A10000, 0, 0x3F800000, 0xFFFFFFFF, 0x3F800000, 0x3F800000,
        0xC0100000, 0x40600000, 0, 0x3F800000, 0xFFFFFFFF, 0,          0,
        0xC0100000, 0x40600000, 0, 0x3F800000, 0xFFFFFFFF, 0,          0,
        0x412C0000, 0x41A10000, 0, 0x3F800000, 0xFFFFFFFF, 0x3F800000, 0x3F800000,
        0xC0100000, 0x41A10000, 0, 0x3F800000, 0xFFFFFFFF, 0,          0x3F800000};
    if (std::memcmp(vertices.data(), expected, sizeof expected) != 0) {
        std::fputs("Background vertices differ from the original byte layout\n", stderr);
        return 1;
    }
    return 0;
}
