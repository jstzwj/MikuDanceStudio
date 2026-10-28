#pragma once

#include <cstddef>

namespace mikudancestudio::editor {

template <typename Key> inline void ClearModelKeyMarks(Key* keys, std::size_t capacity) {
    for (std::size_t i = 0; i < capacity; ++i) {
        keys[i].allocated = 0;
    }
}

} // namespace mikudancestudio::editor
