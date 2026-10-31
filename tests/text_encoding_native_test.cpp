#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <Windows.h>
#include "mikudancestudio/text_encoding.hpp"
#include <iostream>
int main() {
    unsigned checked = 0, mismatches = 0;
    for (unsigned length = 1; length <= 2; ++length)
        for (unsigned value = 0; value < (length == 1 ? 256u : 65536u); ++value) {
            const char bytes[]{char(value >> 8), char(value)};
            const char* source = bytes + (length == 1 ? 1 : 0);
            wchar_t wide[2]{};
            const int count =
                MultiByteToWideChar(932, MB_ERR_INVALID_CHARS, source, int(length), wide, 2);
            std::string native, portable;
            if (count &&
                !mikudancestudio::text::DecodeUtf16Le(reinterpret_cast<const std::uint8_t*>(wide),
                                                      std::size_t(count) * 2, native))
                return 1;
            const bool actual =
                mikudancestudio::text::DecodeCp932(std::string_view(source, length), portable);
            if (actual != (count != 0) || (actual && native != portable)) {
                if (mismatches < 20)
                    std::cerr << "CP932 disagreement length=" << length << " value=" << std::hex
                              << value << " native count=" << count
                              << " unicode=" << unsigned(wide[0]) << " portable=" << actual << '\n';
                ++mismatches;
            }
            ++checked;
        }
    if (mismatches) {
        std::cerr << "mismatches=" << std::dec << mismatches << "\n";
        return 1;
    }
    std::cout << "CP932 native Windows comparison passed for " << checked << " byte sequences\n";
}
