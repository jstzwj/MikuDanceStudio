#include "mikudancestudio/text_encoding.hpp"
#include <iostream>
#include <vector>
using namespace mikudancestudio::text;
#define CHECK(x)                                                                                   \
    do {                                                                                           \
        if (!(x)) {                                                                                \
            std::cerr << "line " << __LINE__ << ": " << #x << '\n';                                \
            return 1;                                                                              \
        }                                                                                          \
    } while (0)
int main() {
    std::string out = "retained";
    CHECK(DecodeCp932("\x83\x5a\x83\x93\x83\x5e\x81\x5b", out) && out == u8"センター");
    CHECK(DecodeCp932("\x8d\xb6\x82\xd0\x82\xb4", out) && out == u8"左ひざ");
    CHECK(DecodeCp932("\x81\x60\x87\x40\xfa\x40", out) && out == u8"～①ⅰ");
    CHECK(DecodeCp932("\xa6", out) && out == u8"ｦ");
    const auto retained = out;
    CHECK(!DecodeCp932("\x81", out) && out == retained);
    CHECK(!DecodeCp932("\x81\x7f", out) && out == retained);
    CHECK(!DecodeCp932("\xa0", out) && out == retained);
    const std::vector<std::uint8_t> supplementary{0x3d, 0xd8, 0x00, 0xde};
    CHECK(DecodeUtf16Le(supplementary.data(), supplementary.size(), out) && out == u8"😀");
    const auto valid = out;
    CHECK(!DecodeUtf16Le(supplementary.data(), 2, out) && out == valid);
    CHECK(!DecodeUtf16Le(supplementary.data() + 2, 2, out) && out == valid);
    CHECK(!DecodeUtf16Le(nullptr, 1, out) && out == valid);
    CHECK(DecodeCp932({}, out) && out.empty());
    CHECK(DecodeUtf16Le(nullptr, 0, out) && out.empty());
    std::cout
        << "explicit CP932 and UTF-16LE, extensions, malformed sequences and retained output passed\n";
}
