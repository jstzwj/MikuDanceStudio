#pragma once
#include <io.h>
#include "mikudancestudio/pmm_format.hpp"
namespace mikudancestudio {
inline bool ReadPmmFileBytes(void* context, void* destination, std::size_t size) {
    return _read(*static_cast<int*>(context), destination, static_cast<unsigned>(size)) ==
           static_cast<int>(size);
}
inline int ReadPmmFileBytesResult(void* context, void* destination, std::size_t size) {
    return _read(*static_cast<int*>(context), destination, static_cast<unsigned>(size));
}
}
