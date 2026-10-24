#pragma once

#include <cstdint>

namespace mikudancestudio {
class MMDApp;

int CompareFunction(const void* a, const void* b);
void ClearGlobalTimelineKeySelection(MMDApp& app);
void ClearModelTimelineKeySelection(MMDApp& app);

// The -1/-2 hit-map sentinels stage a half-open cell rectangle, in screen
// column/row coordinates; they never encode key-buffer addresses.
void StageModelTimelineRangeCell(MMDApp& app, std::int32_t hit, std::int32_t column,
                                 std::int32_t row, bool shiftActive);

// The four global tracks and every accessory track are allocated by UI init.
// Caller clears timeline selection counts before collecting new records.
void CollectGlobalTimelineSelection(MMDApp& app);
}
