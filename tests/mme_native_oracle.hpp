#pragma once

#include <windows.h>
#include <filesystem>
#include <memory>
#include <string>
#include <functional>

// Optional native x64/x86 effect engine. Neither the DLL nor its proxy/hook loader
// is distributed. The fixture supplies the host's public query boundary.
class NativeMmeOracle {
    struct Impl;
    std::unique_ptr<Impl> impl;

public:
    using QueryResolver = std::function<FARPROC(const char*, const char*)>;
    explicit NativeMmeOracle(const std::filesystem::path& path, QueryResolver resolver = {});
    ~NativeMmeOracle();
    NativeMmeOracle(const NativeMmeOracle&) = delete;
    NativeMmeOracle& operator=(const NativeMmeOracle&) = delete;
    void LoadMapping(const std::filesystem::path& path, HWND window);
    void SaveMapping(const std::filesystem::path& path);
    void* Module() const;
};

void SetMmeOracle(NativeMmeOracle* oracle);
