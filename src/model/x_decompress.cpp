#include "x_decompress.hpp"
#include <algorithm>
#include <cstring>
// Use the bundled DEFLATE decoder privately. MSZIP blocks can reference the
// preceding 32 KiB; the public single-stream stb API cannot supply that history.
#define STB_IMAGE_STATIC
#define STB_IMAGE_IMPLEMENTATION
#define STBI_ONLY_PNG
#define STBI_NO_STDIO
#define STBI_NO_HDR
#define STBI_NO_LINEAR
#include "../../third_party/stb/stb_image.h"

namespace mikudancestudio::runtime {
bool DecompressX(const unsigned char* bytes, std::size_t size, std::vector<unsigned char>& output,
                 std::string& reason) {
    const auto fail = [&](const char* message) {
        reason = message;
        return false;
    };
    if (size < 20)
        return fail("x-truncated-compression-header");
    const auto u32 = [&](std::size_t at) {
        return unsigned(bytes[at]) | unsigned(bytes[at + 1]) << 8 | unsigned(bytes[at + 2]) << 16 |
               unsigned(bytes[at + 3]) << 24;
    };
    const auto u16 = [&](std::size_t at) {
        return unsigned(bytes[at]) | unsigned(bytes[at + 1]) << 8;
    };
    const std::size_t total = u32(16);
    if (total <= 16 || total > 64 * 1024 * 1024)
        return fail("x-decompressed-size-limit");
    std::vector<unsigned char> decoded(total);
    std::copy_n(bytes, 16, decoded.begin());
    std::memcpy(decoded.data() + 8, bytes[8] == 't' ? "txt " : "bin ", 4);
    std::size_t input = 20, position = 16;
    while (input < size) {
        if (size - input < 4)
            return fail("x-truncated-compression-block");
        const auto unpacked = u16(input), packed = u16(input + 2);
        input += 4;
        if (!unpacked || unpacked > 32768 || unpacked > total - position || packed < 3 ||
            packed > size - input)
            return fail("x-invalid-compression-block-size");
        if (bytes[input] != 'C' || bytes[input + 1] != 'K')
            return fail("x-invalid-mszip-magic");
        stbi__zbuf stream{};
        stream.zbuffer = const_cast<unsigned char*>(bytes + input + 2);
        stream.zbuffer_end = const_cast<unsigned char*>(bytes + input + packed);
        stream.zout_start = reinterpret_cast<char*>(
            decoded.data() + std::max<std::size_t>(16, position > 32768 ? position - 32768 : 16));
        stream.zout = reinterpret_cast<char*>(decoded.data() + position);
        stream.zout_end = stream.zout + unpacked;
        stream.z_expandable = 0;
        if (!stbi__parse_zlib(&stream, 0) || stream.zout != stream.zout_end)
            return fail("x-invalid-mszip-payload");
        position += unpacked;
        input += packed;
    }
    if (position != total)
        return fail("x-decompressed-size-mismatch");
    output = std::move(decoded);
    reason.clear();
    return true;
}
}
