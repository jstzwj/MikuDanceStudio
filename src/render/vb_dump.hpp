// ===========================================================================
// Porting-era diagnostics: vertex-batch capture under
// MIKUDANCESTUDIO_VB_DUMP_DIR for A/B comparison against the original
// renderers.  Compiled only with -DMIKUDANCESTUDIO_DIAG (CMake option
// MIKUDANCESTUDIO_DIAG, default OFF); the OFF stub below keeps the call
// sites valid and inlines away to nothing.
// ===========================================================================
#pragma once

#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <Windows.h>

#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <cstring>

namespace mikudancestudio {

#ifdef MIKUDANCESTUDIO_DIAG

inline void DumpVertexBatch(const char* name, const void* bytes, std::uint32_t primitiveCount,
                            std::size_t bytesPerPrimitive) {
    char directory[MAX_PATH]{};
    const DWORD length =
        GetEnvironmentVariableA("MIKUDANCESTUDIO_VB_DUMP_DIR", directory, MAX_PATH);
    if (length == 0 || length >= MAX_PATH || bytes == nullptr)
        return;

    char stableCapture[2]{};
    if (GetEnvironmentVariableA("MIKUDANCESTUDIO_AB_STABLE_CAPTURE", stableCapture,
                                sizeof(stableCapture)) == 1 &&
        stableCapture[0] == '1') {
        char requireLine[2]{};
        const bool refreshedModelCapture =
            GetEnvironmentVariableA("MIKUDANCESTUDIO_AB_REQUIRE_LINE", requireLine,
                                    sizeof(requireLine)) == 1 &&
            requireLine[0] == '1';
        char gatePath[MAX_PATH]{};
        std::snprintf(gatePath, sizeof(gatePath), "%s\\%s", directory,
                      refreshedModelCapture ? "vb.capture.active" : "vb.capture.ready");
        if (GetFileAttributesA(gatePath) == INVALID_FILE_ATTRIBUTES)
            return;
    }

    // IDA removes each original breakpoint after its first hit. Mirror that
    // capture point so a six-second UI wait cannot replace the first-frame
    // batch with a later FPS/timeline frame.
    static LONG spriteDumped = 0;
    static LONG textDumped = 0;
    static LONG lineDumped = 0;
    static LONG selectionDumped = 0;
    LONG* dumped = nullptr;
    if (std::strcmp(name, "sprite") == 0) {
        dumped = &spriteDumped;
    } else if (std::strcmp(name, "text") == 0)
        dumped = &textDumped;
    else if (std::strcmp(name, "line") == 0)
        dumped = &lineDumped;
    else if (std::strcmp(name, "line_selection") == 0)
        dumped = &selectionDumped;
    if (dumped != nullptr && InterlockedCompareExchange(dumped, 1, 0) != 0)
        return;

    CreateDirectoryA(directory, nullptr);
    char dataPath[MAX_PATH]{};
    char countPath[MAX_PATH]{};
    std::snprintf(dataPath, sizeof(dataPath), "%s\\%s.bin", directory, name);
    std::snprintf(countPath, sizeof(countPath), "%s\\%s.count", directory, name);

    HANDLE file = CreateFileA(dataPath, GENERIC_WRITE, FILE_SHARE_READ, nullptr, CREATE_ALWAYS,
                              FILE_ATTRIBUTE_NORMAL, nullptr);
    if (file != INVALID_HANDLE_VALUE) {
        const std::size_t total = primitiveCount * bytesPerPrimitive;
        DWORD written = 0;
        WriteFile(file, bytes, static_cast<DWORD>(total), &written, nullptr);
        CloseHandle(file);
    }

    file = CreateFileA(countPath, GENERIC_WRITE, FILE_SHARE_READ, nullptr, CREATE_ALWAYS,
                       FILE_ATTRIBUTE_NORMAL, nullptr);
    if (file != INVALID_HANDLE_VALUE) {
        char value[32]{};
        const int chars = std::snprintf(value, sizeof(value), "%u\n", primitiveCount);
        DWORD written = 0;
        WriteFile(file, value, static_cast<DWORD>(chars), &written, nullptr);
        CloseHandle(file);
    }
}

#else // !MIKUDANCESTUDIO_DIAG

inline void DumpVertexBatch(const char*, const void*, std::uint32_t, std::size_t) {
}

#endif // MIKUDANCESTUDIO_DIAG

} // namespace mikudancestudio
