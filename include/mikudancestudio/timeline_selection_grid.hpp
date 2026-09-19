#pragma once

#include <cstddef>

namespace mikudancestudio {

inline constexpr std::size_t kTimelineHitGridStride = 200;

// A frame row owns an independent column cursor. Advancing the cursor in
// one row must not shift the selected columns in the following frame.
template<class Visit>
void VisitTimelineSelectionCells(int firstRow, int lastRow,
                                 int firstColumn, int lastColumn,
                                 Visit&& visit) {
    for (int row = firstRow; row < lastRow; ++row)
        for (int column = firstColumn; column < lastColumn; ++column)
            visit(static_cast<std::size_t>(row) * kTimelineHitGridStride +
                      static_cast<std::size_t>(column), column);
}

}  // namespace mikudancestudio
