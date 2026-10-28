#pragma once

// Test-only selection of the unmodified June 2010 x64 runtime's CPU kernels.
// Production must retain the SDK's native dispatch, as the original MMD does.
#if defined(_M_X64)
class ScopedD3dxDispatch {
    static constexpr std::size_t kTableBytes = 0x250;
    unsigned char* table_ = nullptr;
    unsigned* optimization_ = nullptr;
    std::array<unsigned char, kTableBytes> savedTable_{};
    unsigned savedOptimization_ = 0;

public:
    explicit ScopedD3dxDispatch(const std::string& name) {
        Check(name == "intel" || name == "amd", "unknown D3DX dispatch");
        const auto module = GetModuleHandleW(L"d3dx9_43.dll");
        Check(module != nullptr, "loaded x64 D3DX runtime");
        wchar_t path[32768]{};
        const auto length = GetModuleFileNameW(module, path, 32768);
        Check(length > 0 && length < 32768, "D3DX runtime path");
        std::ifstream stream(std::filesystem::path(path), std::ios::binary);
        Check(stream.good(), "open D3DX runtime for verification");
        std::vector<unsigned char> bytes((std::istreambuf_iterator<char>(stream)), {});
        const std::array<unsigned char, 32> expected = {
            0x84, 0xb9, 0x00, 0xdb, 0xd7, 0xfa, 0x97, 0x8d, 0x6e, 0x0c, 0xae,
            0xe2, 0x6f, 0xc5, 0x4f, 0x2f, 0x61, 0xd9, 0x2c, 0x9c, 0x75, 0xd1,
            0x0b, 0x35, 0xf0, 0x0e, 0x3e, 0x82, 0xcd, 0x1d, 0x67, 0xb4};
        BCRYPT_ALG_HANDLE algorithm = nullptr;
        std::array<unsigned char, 32> digest{};
        Check(BCryptOpenAlgorithmProvider(&algorithm, BCRYPT_SHA256_ALGORITHM, nullptr, 0) == 0,
              "open D3DX hash algorithm");
        const auto result = BCryptHash(algorithm, nullptr, 0, bytes.data(),
                                       static_cast<ULONG>(bytes.size()), digest.data(), 32);
        BCryptCloseAlgorithmProvider(algorithm, 0);
        Check(result == 0 && digest == expected, "D3DX dispatch oracle SHA-256 mismatch");
        int cpu[4];
        __cpuid(cpu, 1);
        Check(name != "intel" || (cpu[2] & 1), "Intel D3DX kernel requires SSE3");

        D3DXVECTOR3 input(1, 2, 3), output;
        D3DXVec3Normalize(&output, &input); // Finish the runtime's lazy initialization.
        auto* base = reinterpret_cast<unsigned char*>(module);
        // IDA: D3DXCpuOptimizations RVA 0x5E198 first copies the C table,
        // then calls Intel RVA 0x211864 or AMD RVA 0x211688. The AMD initializer
        // writes the global table directly; the Intel one takes its address.
        table_ = base + 0x23B380;
        optimization_ = reinterpret_cast<unsigned*>(base + 0x22F8D4);
        std::memcpy(savedTable_.data(), table_, kTableBytes);
        savedOptimization_ = *optimization_;
        std::memcpy(table_, base + 0x23B5D0, kTableBytes);
        reinterpret_cast<void (*)(void*)>(base + (name == "intel" ? 0x211864 : 0x211688))(table_);
        *optimization_ = name == "intel" ? 0 : 2;
        std::printf("test-only D3DX dispatch: %s\n", name.c_str());
    }

    ~ScopedD3dxDispatch() {
        std::memcpy(table_, savedTable_.data(), kTableBytes);
        *optimization_ = savedOptimization_;
    }

    ScopedD3dxDispatch(const ScopedD3dxDispatch&) = delete;
    ScopedD3dxDispatch& operator=(const ScopedD3dxDispatch&) = delete;
};
#endif
