#pragma once

#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <Windows.h>
#include <mmsystem.h>
#include <dsound.h>

#include <cstddef>
#include <cstdint>
#include <cstdio>

namespace mikudancestudio {

struct WaveAudioContext {
    unsigned char* waveformMax;
    unsigned char* waveformMin;
    HDC timelineDC;
    HWND mainWindow;
    IDirectSound* directSound;
    IDirectSoundBuffer* streamingBuffer;
    WAVEFORMATEX format;
    FILE* parseStream;
    wchar_t path[256];
    std::int32_t bufferBytes;
    std::uintptr_t feedThread;
    std::int32_t dataSize;
    std::int32_t readCursor;
    std::int32_t dataOffset;
    HANDLE fileHandle;
    // Shared 0=running, 1=stop requested, 2=worker completed protocol.
    // Interlocked access preserves the original state machine while making
    // publication of streamingBuffer=nullptr safe across the two threads.
    LONG stopFlag;
    std::int32_t failureCount;
    std::int32_t waveformColumns;
    std::uint8_t englishUI;
    std::uint8_t reserved[3];
    LONG volume;

    LONG ReadStopFlag() {
        return InterlockedCompareExchange(&stopFlag, 0, 0);
    }
    void WriteStopFlag(LONG value) {
        InterlockedExchange(&stopFlag, value);
    }
};

#if !defined(_WIN64)
static_assert(sizeof(WaveAudioContext) == 0x25C, "x86 WaveAudioContext ABI");
static_assert(offsetof(WaveAudioContext, format) == 0x18, "x86 wave format");
static_assert(offsetof(WaveAudioContext, path) == 0x30, "x86 wave path");
static_assert(offsetof(WaveAudioContext, volume) == 0x258, "x86 wave volume");
#endif

}  // namespace mikudancestudio
