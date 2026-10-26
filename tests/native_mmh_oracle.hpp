#pragma once

#include <bcrypt.h>

// Invoke the original MMHack wrapper without installing its process-wide IAT
// hooks. Its own CRT constructs/destroys the VC9 containers; public D3D calls
// and engine callbacks cross the fixture boundary with their native ABI.
class NativeMmhOracle {
    HMODULE image = nullptr;
    std::vector<HMODULE> imports;
    bool initialized = false;
    alignas(void*) std::array<unsigned char, 128> storage{};
    IDirect3DDevice9* wrapper = nullptr;
    IDirect3DDevice9* real = nullptr;
    using Crt = BOOL(WINAPI*)(HINSTANCE, DWORD, void*);

public:
    void ReplaceImport(const char* symbol, FARPROC replacement) {
        auto* base = reinterpret_cast<unsigned char*>(image);
        const auto* dos = reinterpret_cast<IMAGE_DOS_HEADER*>(base);
        const auto* nt = reinterpret_cast<IMAGE_NT_HEADERS*>(base + dos->e_lfanew);
        auto* descriptor = reinterpret_cast<IMAGE_IMPORT_DESCRIPTOR*>(
            base + nt->OptionalHeader.DataDirectory[IMAGE_DIRECTORY_ENTRY_IMPORT].VirtualAddress);
        for (; descriptor->Name; ++descriptor) {
            auto* names =
                reinterpret_cast<IMAGE_THUNK_DATA*>(base + descriptor->OriginalFirstThunk);
            auto* slots = reinterpret_cast<IMAGE_THUNK_DATA*>(base + descriptor->FirstThunk);
            for (; names->u1.AddressOfData; ++names, ++slots) {
                if (IMAGE_SNAP_BY_ORDINAL(names->u1.Ordinal))
                    continue;
                const char* name =
                    reinterpret_cast<IMAGE_IMPORT_BY_NAME*>(base + names->u1.AddressOfData)->Name;
                if (std::strcmp(name, symbol))
                    continue;
                DWORD protection;
                Check(VirtualProtect(slots, sizeof(*slots), PAGE_READWRITE, &protection) != 0,
                      "MMHack import override");
                slots->u1.Function = reinterpret_cast<ULONG_PTR>(replacement);
                VirtualProtect(slots, sizeof(*slots), protection, &protection);
                return;
            }
        }
        Check(false, "MMHack import not found");
    }
    template <class T> T At(std::size_t rva) const {
        return reinterpret_cast<T>(reinterpret_cast<unsigned char*>(image) + rva);
    }
    template <class T> T Export(const char* name) const {
        const auto address = GetProcAddress(image, name);
        Check(address != nullptr, name);
        return reinterpret_cast<T>(address);
    }
    template <class T> void Set(std::size_t rva, T value) { *At<T*>(rva) = value; }
    NativeMmhOracle(const std::filesystem::path& path, NativeMmdQueries& queries) {
        try {
            const char* expected =
                sizeof(void*) == 8
                    ? "e450b25e5cd2bcf125ca4b8eb9e57f858ec3ca379633c7d25f9dd40e864b390d"
                    : "ee0df3b1d4429118e61df839e809d376e3597399187afdf315d515563d3c7f13";
            std::ifstream input(path, std::ios::binary);
            std::vector<unsigned char> bytes((std::istreambuf_iterator<char>(input)), {});
            BCRYPT_ALG_HANDLE algorithm = nullptr;
            unsigned char hash[32]{};
            Check(BCryptOpenAlgorithmProvider(&algorithm, BCRYPT_SHA256_ALGORITHM, nullptr, 0) == 0,
                  "MMHack hash provider");
            const auto status = BCryptHash(algorithm, nullptr, 0, bytes.data(),
                                           static_cast<ULONG>(bytes.size()), hash, sizeof hash);
            BCryptCloseAlgorithmProvider(algorithm, 0);
            char hex[65]{};
            for (unsigned i = 0; i < 32; ++i)
                sprintf_s(hex + 2 * i, 3, "%02x", hash[i]);
            Check(status == 0 && std::strcmp(expected, hex) == 0, "native MMHack SHA-256");
            image = LoadLibraryExW(path.c_str(), nullptr, DONT_RESOLVE_DLL_REFERENCES);
            Check(image != nullptr, "map original MMHack");
            auto* base = reinterpret_cast<unsigned char*>(image);
            const auto* dos = reinterpret_cast<IMAGE_DOS_HEADER*>(base);
            const auto* nt = reinterpret_cast<IMAGE_NT_HEADERS*>(base + dos->e_lfanew);
            auto* descriptor = reinterpret_cast<IMAGE_IMPORT_DESCRIPTOR*>(
                base +
                nt->OptionalHeader.DataDirectory[IMAGE_DIRECTORY_ENTRY_IMPORT].VirtualAddress);
            for (; descriptor->Name; ++descriptor) {
                const auto* name = reinterpret_cast<const char*>(base + descriptor->Name);
                const bool host = _stricmp(name, "MikuMikuDance.exe") == 0;
                HMODULE library = host ? nullptr : LoadLibraryA(name);
                Check(host || library != nullptr, name);
                if (library)
                    imports.push_back(library);
                auto* names =
                    reinterpret_cast<IMAGE_THUNK_DATA*>(base + descriptor->OriginalFirstThunk);
                auto* slots = reinterpret_cast<IMAGE_THUNK_DATA*>(base + descriptor->FirstThunk);
                for (; names->u1.AddressOfData; ++names, ++slots) {
                    const char* symbol = IMAGE_SNAP_BY_ORDINAL(names->u1.Ordinal)
                                             ? MAKEINTRESOURCEA(IMAGE_ORDINAL(names->u1.Ordinal))
                                             : reinterpret_cast<IMAGE_IMPORT_BY_NAME*>(
                                                   base + names->u1.AddressOfData)
                                                   ->Name;
                    const auto address =
                        host ? queries.Export(symbol) : GetProcAddress(library, symbol);
                    Check(address != nullptr, "native MMHack import");
                    DWORD protection;
                    Check(VirtualProtect(slots, sizeof(*slots), PAGE_READWRITE, &protection) != 0,
                          "MMHack IAT protection");
                    slots->u1.Function = reinterpret_cast<ULONG_PTR>(address);
                    VirtualProtect(slots, sizeof(*slots), protection, &protection);
                }
            }
            At<void(__cdecl*)()>(sizeof(void*) == 8 ? 0x1F788 : 0x1EE91)();
            Check(At<Crt>(sizeof(void*) == 8 ? 0x17FA0 : 0x1822B)(image, DLL_PROCESS_ATTACH,
                                                                  nullptr) != FALSE,
                  "MMHack CRT initialization");
            initialized = true;
        } catch (...) {
            Release();
            throw;
        }
    }
    ~NativeMmhOracle() { Release(); }
    void Release() {
        if (wrapper) {
            // Original wrapper cleanup and non-deleting destructor. Storage
            // belongs to the fixture, not the original operator delete.
#if defined(_M_X64)
            At<void (*)(void*)>(0x1DB0)(storage.data());
            At<void (*)(void*)>(0x1C90)(storage.data());
#else
            // Release-to-zero runs both operations and frees the allocation;
            // call the original cleanup + destructor separately instead.
            At<void(WINAPI*)(void*)>(0x1B60)(storage.data());
            At<void(WINAPI*)(void*)>(0x1A30)(storage.data());
#endif
            real->Release();
        }
        if (initialized)
            At<Crt>(sizeof(void*) == 8 ? 0x17FA0 : 0x1822B)(image, DLL_PROCESS_DETACH, nullptr);
        if (image)
            FreeLibrary(image);
        for (auto i = imports.rbegin(); i != imports.rend(); ++i)
            FreeLibrary(*i);
    }
    IDirect3DDevice9* Device(IDirect3DDevice9* device) {
        real = device;
        wrapper = reinterpret_cast<IDirect3DDevice9*>(storage.data());
#if defined(_M_X64)
        At<void* (*)(void*, IDirect3DDevice9*)>(0x1BD0)(storage.data(), device);
#else
        At<void*(WINAPI*)(void*, IDirect3DDevice9*)>(0x1950)(storage.data(), device);
#endif
        return wrapper;
    }
    void RegisterModel(mdl::ModelRecord& model) { RegisterObject(&model.indexBuffer, model.path); }
    void RegisterObject(void* field, const wchar_t* path) {
#if defined(_M_X64)
        At<void (*)(void*, const wchar_t*)>(0xF150)(field, path);
#else
        const auto fn = At<void*>(0xED60);
        __asm {
            push ebx
            mov ebx, path
            push field
            call fn
            add esp, 4
            pop ebx
        }
#endif
    }
};
