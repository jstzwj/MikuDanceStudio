#pragma once

#include <cstdint>
#include <cstdio>

namespace mikudancestudio {

bool ScanWaveDataChunk(std::FILE* stream, std::int32_t& size,
                       std::int32_t& offset);

}
