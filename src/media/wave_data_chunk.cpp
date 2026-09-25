#include "mikudancestudio/wave_data_chunk.hpp"

#include <climits>

namespace mikudancestudio {

bool ScanWaveDataChunk(std::FILE* stream, std::int32_t& size,
                       std::int32_t& offset) {
    int value;
    while ((value = std::getc(stream)) != EOF) {
        if (value != 'd') continue;
        value = std::getc(stream);
        if (value == EOF) return false;
        if (value != 'a') continue;
        value = std::getc(stream);
        if (value == EOF) return false;
        if (value != 't') continue;
        value = std::getc(stream);
        if (value == EOF) return false;
        if (value != 'a') continue;

        std::int32_t dataSize = 0;
        if (std::fread(&dataSize, sizeof(dataSize), 1, stream) != 1 ||
            dataSize < 0) return false;
        const long dataOffset = std::ftell(stream);
        if (dataOffset < 0 || dataOffset > INT32_MAX) return false;
        size = dataSize;
        offset = static_cast<std::int32_t>(dataOffset);
        return true;
    }
    return false;
}

}
