#include "mikudancestudio/wave_data_chunk.hpp"

#include <cstdint>
#include <cstdio>
#include <cstdlib>

using mikudancestudio::ScanWaveDataChunk;

void Check(bool condition) {
    if (!condition) std::abort();
}

int main() {
    std::FILE* stream = nullptr;
    Check(tmpfile_s(&stream) == 0);
    const char valid[] = "otherdata";
    const std::int32_t size = 16;
    Check(std::fwrite(valid, 1, sizeof(valid) - 1, stream) ==
          sizeof(valid) - 1);
    Check(std::fwrite(&size, sizeof(size), 1, stream) == 1);
    std::rewind(stream);
    std::int32_t foundSize = -1, offset = -1;
    Check(ScanWaveDataChunk(stream, foundSize, offset));
    Check(foundSize == size && offset == 13);
    std::fclose(stream);

    Check(tmpfile_s(&stream) == 0);
    Check(std::fwrite("dat", 1, 3, stream) == 3);
    std::rewind(stream);
    Check(!ScanWaveDataChunk(stream, foundSize, offset));
    std::fclose(stream);

    Check(tmpfile_s(&stream) == 0);
    Check(std::fwrite("data\x01\x02", 1, 6, stream) == 6);
    std::rewind(stream);
    Check(!ScanWaveDataChunk(stream, foundSize, offset));
    std::fclose(stream);

    Check(tmpfile_s(&stream) == 0);
    const std::int32_t invalidSize = -1;
    Check(std::fwrite("data", 1, 4, stream) == 4);
    Check(std::fwrite(&invalidSize, sizeof(invalidSize), 1, stream) == 1);
    std::rewind(stream);
    Check(!ScanWaveDataChunk(stream, foundSize, offset));
    std::fclose(stream);
}
