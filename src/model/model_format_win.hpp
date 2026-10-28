#pragma once
#include <io.h>
#include <limits>
#include "mikudancestudio/model_format_header.hpp"
#include "mikudancestudio/model_sections.hpp"

namespace mikudancestudio {
inline bool ReadModelFileBytes(void* context, void* destination, std::size_t size) {
    if (size > static_cast<std::size_t>(std::numeric_limits<int>::max()))
        return false;
    return _read(*static_cast<int*>(context), destination, static_cast<unsigned>(size)) ==
           static_cast<int>(size);
}
inline int ReadModelFileBytesResult(void* context, void* destination, std::size_t size) {
    if (size > static_cast<std::size_t>(std::numeric_limits<int>::max()))
        return -1;
    return _read(*static_cast<int*>(context), destination, static_cast<unsigned>(size));
}
template <class Count> bool ReadModelFileCount(int& file, std::uint8_t width, Count& count) {
    std::uint32_t parsed = 0;
    if (!format::ReadModelSectionCount(ReadModelFileBytes, &file, width, parsed))
        return false;
    count = static_cast<Count>(parsed);
    return true;
}
} // namespace mikudancestudio
