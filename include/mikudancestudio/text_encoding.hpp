#pragma once
#include <cstdint>
#include <string>
#include <string_view>

namespace mikudancestudio::text {
// Explicit disk encodings. Failure preserves output; no locale or ACP lookup.
bool DecodeCp932(std::string_view bytes, std::string& utf8);
bool DecodeUtf16Le(const std::uint8_t* bytes, std::size_t size, std::string& utf8);
}
