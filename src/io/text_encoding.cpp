#include "mikudancestudio/text_encoding.hpp"
#include <algorithm>
#include <iterator>

namespace mikudancestudio::text {
namespace {
#include "cp932_decode_table.inc"
void Append(std::string& out, std::uint32_t c) {
    if (c < 0x80)
        out += char(c);
    else if (c < 0x800) {
        out += char(0xc0 | (c >> 6));
        out += char(0x80 | (c & 63));
    } else if (c < 0x10000) {
        out += char(0xe0 | (c >> 12));
        out += char(0x80 | ((c >> 6) & 63));
        out += char(0x80 | (c & 63));
    } else {
        out += char(0xf0 | (c >> 18));
        out += char(0x80 | ((c >> 12) & 63));
        out += char(0x80 | ((c >> 6) & 63));
        out += char(0x80 | (c & 63));
    }
}
}
bool DecodeCp932(std::string_view bytes, std::string& output) {
    std::string result;
    for (std::size_t n = 0; n < bytes.size(); ++n) {
        const unsigned lead = static_cast<unsigned char>(bytes[n]);
        auto c = Cp932Single[lead];
        if (c == 0xffffffff) {
            if (++n == bytes.size())
                return false;
            const auto pair = (lead << 8) | static_cast<unsigned char>(bytes[n]);
            const auto found = std::lower_bound(
                std::begin(Cp932Pairs), std::end(Cp932Pairs), pair,
                [](const Cp932Pair& item, unsigned value) { return item.bytes < value; });
            if (found == std::end(Cp932Pairs) || found->bytes != pair)
                return false;
            c = found->unicode;
        }
        Append(result, c);
    }
    output = std::move(result);
    return true;
}
bool DecodeUtf16Le(const std::uint8_t* bytes, std::size_t size, std::string& output) {
    if (size % 2 || (!bytes && size))
        return false;
    std::string result;
    for (std::size_t n = 0; n < size; n += 2) {
        unsigned c = bytes[n] | (unsigned(bytes[n + 1]) << 8);
        if (c >= 0xd800 && c <= 0xdbff) {
            if (n + 3 >= size)
                return false;
            const unsigned low = bytes[n + 2] | (unsigned(bytes[n + 3]) << 8);
            if (low < 0xdc00 || low > 0xdfff)
                return false;
            c = 0x10000 + ((c - 0xd800) << 10) + (low - 0xdc00);
            n += 2;
        } else if (c >= 0xdc00 && c <= 0xdfff)
            return false;
        Append(result, c);
    }
    output = std::move(result);
    return true;
}
}
