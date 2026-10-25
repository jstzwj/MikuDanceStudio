#pragma once

// Optional local oracle. No reference executable or third-party model is distributed.
#if defined(_M_X64)
#include <bcrypt.h>
#include <array>

class OriginalPhysicsModule {
    HMODULE image_ = nullptr;
    HANDLE activation_ = INVALID_HANDLE_VALUE;
    ULONG_PTR cookie_ = 0;
    std::vector<HMODULE> imports_;
    void Release() noexcept {
        if (image_) {
            FreeLibrary(image_);
            image_ = nullptr;
        }
        for (auto it = imports_.rbegin(); it != imports_.rend(); ++it)
            FreeLibrary(*it);
        imports_.clear();
        if (cookie_) {
            DeactivateActCtx(0, cookie_);
            cookie_ = 0;
        }
        if (activation_ != INVALID_HANDLE_VALUE) {
            ReleaseActCtx(activation_);
            activation_ = INVALID_HANDLE_VALUE;
        }
    }

public:
    template <class T> T Function(std::size_t rva) const {
        return reinterpret_cast<T>(reinterpret_cast<unsigned char*>(image_) + rva);
    }
    OriginalPhysicsModule(const OriginalPhysicsModule&) = delete;
    OriginalPhysicsModule& operator=(const OriginalPhysicsModule&) = delete;
    explicit OriginalPhysicsModule(const std::filesystem::path& path) {
        try {
            const std::array<unsigned char, 32> expected = {
                0x07, 0x51, 0x6f, 0xd3, 0xbf, 0x1e, 0x6b, 0x13, 0x39, 0x83, 0x6b,
                0x67, 0x73, 0xa1, 0x56, 0xf6, 0x1b, 0xdd, 0x6f, 0x84, 0x8e, 0xeb,
                0x62, 0x1f, 0xdd, 0xa0, 0x12, 0x37, 0x5d, 0xf3, 0x13, 0xa1};
            std::ifstream stream(path, std::ios::binary);
            std::vector<unsigned char> bytes((std::istreambuf_iterator<char>(stream)), {});
            BCRYPT_ALG_HANDLE algorithm = nullptr;
            std::array<unsigned char, 32> digest{};
            Check(BCryptOpenAlgorithmProvider(&algorithm, BCRYPT_SHA256_ALGORITHM, nullptr, 0) == 0,
                  "open reference hash algorithm");
            const auto hashed = BCryptHash(algorithm, nullptr, 0, bytes.data(),
                                           static_cast<ULONG>(bytes.size()), digest.data(), 32);
            BCryptCloseAlgorithmProvider(algorithm, 0);
            Check(hashed == 0 && digest == expected, "reference executable SHA-256 mismatch");

            // Activate the executable's VC9 assembly before loading its CRT. Loading
            // msvcr90.dll by an absolute path without this context triggers R6034.
            ACTCTXW context{};
            context.cbSize = sizeof(context);
            context.dwFlags = ACTCTX_FLAG_RESOURCE_NAME_VALID;
            context.lpSource = path.c_str();
            context.lpResourceName = MAKEINTRESOURCEW(1);
            activation_ = CreateActCtxW(&context);
            Check(activation_ != INVALID_HANDLE_VALUE && ActivateActCtx(activation_, &cookie_),
                  "activate original VC9 runtime manifest");
            image_ = LoadLibraryExW(path.c_str(), nullptr, DONT_RESOLVE_DLL_REFERENCES);
            Check(image_ != nullptr, "map reference executable");
            auto* base = reinterpret_cast<unsigned char*>(image_);
            auto* dos = reinterpret_cast<IMAGE_DOS_HEADER*>(base);
            auto* nt = reinterpret_cast<IMAGE_NT_HEADERS64*>(base + dos->e_lfanew);
            auto* descriptor = reinterpret_cast<IMAGE_IMPORT_DESCRIPTOR*>(
                base +
                nt->OptionalHeader.DataDirectory[IMAGE_DIRECTORY_ENTRY_IMPORT].VirtualAddress);
            for (; descriptor->Name; ++descriptor) {
                const auto* name = reinterpret_cast<const char*>(base + descriptor->Name);
                HMODULE library = LoadLibraryA(name);
                if (!library && _stricmp(name, "VCOMP90.DLL") == 0)
                    library = LoadLibraryW(L"vcomp140.dll");
                Check(library != nullptr, name);
                imports_.push_back(library);
                auto* names =
                    reinterpret_cast<IMAGE_THUNK_DATA64*>(base + descriptor->OriginalFirstThunk);
                auto* slots = reinterpret_cast<IMAGE_THUNK_DATA64*>(base + descriptor->FirstThunk);
                for (; names->u1.AddressOfData; ++names, ++slots) {
                    const char* symbol = IMAGE_SNAP_BY_ORDINAL64(names->u1.Ordinal)
                                             ? MAKEINTRESOURCEA(IMAGE_ORDINAL64(names->u1.Ordinal))
                                             : reinterpret_cast<IMAGE_IMPORT_BY_NAME*>(
                                                   base + names->u1.AddressOfData)
                                                   ->Name;
                    const auto address = GetProcAddress(library, symbol);
                    Check(address != nullptr, "resolve reference import");
                    DWORD protection;
                    Check(VirtualProtect(slots, sizeof(*slots), PAGE_READWRITE, &protection) != 0,
                          "make reference import slot writable");
                    slots->u1.Function = reinterpret_cast<ULONGLONG>(address);
                    VirtualProtect(slots, sizeof(*slots), protection, &protection);
                }
            }
        } catch (...) {
            Release();
            throw;
        }
    }
    ~OriginalPhysicsModule() { Release(); }
};
#endif
