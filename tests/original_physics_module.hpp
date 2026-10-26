#pragma once

// Optional local oracle. No reference executable or third-party model is distributed.
#include <bcrypt.h>
#include <array>
#include <float.h>

#if defined(_M_IX86)
// Reserve the fixed reference image before argv/stdio initialization can map
// an OEM code-page file over 0x400000. DONT_RESOLVE executes no reference code.
namespace {
HMODULE originalPreloadedImage = nullptr;
int __cdecl PreloadOriginalImage() {
    const wchar_t* command = GetCommandLineW();
    const wchar_t option[] = L"--original";
    for (const wchar_t* p = command; *p; ++p) {
        unsigned i = 0;
        while (option[i] && p[i] == option[i])
            ++i;
        if (option[i] || (p[i] != L' ' && p[i] != L'\t'))
            continue;
        p += i;
        while (*p == L' ' || *p == L'\t')
            ++p;
        const bool quoted = *p == L'"';
        if (quoted)
            ++p;
        wchar_t path[32768]{};
        unsigned n = 0;
        while (*p && (quoted ? *p != L'"' : *p != L' ' && *p != L'\t') && n + 1 < 32768)
            path[n++] = *p++;
        wchar_t reserved[2]{};
        if (GetEnvironmentVariableW(L"MDS_ORIGINAL_IMAGE_RESERVED", reserved, 2) == 1 &&
            reserved[0] == L'1') {
            MEMORY_BASIC_INFORMATION memory{};
            VirtualQuery(reinterpret_cast<void*>(0x400000), &memory, sizeof memory);
            if (memory.AllocationBase == reinterpret_cast<void*>(0x400000) &&
                memory.State == MEM_RESERVE && memory.Type == MEM_PRIVATE &&
                memory.RegionSize == 0x1A3000 && memory.AllocationProtect == PAGE_NOACCESS)
                VirtualFree(memory.AllocationBase, 0, MEM_RELEASE);
        }
        originalPreloadedImage = LoadLibraryExW(path, nullptr, DONT_RESOLVE_DLL_REFERENCES);
        break;
    }
    return 0;
}
#pragma section(".CRT$XIAA", long, read)
}
extern "C" __declspec(allocate(".CRT$XIAA")) int(__cdecl* originalImageInitializer)() =
    PreloadOriginalImage;
#pragma comment(linker, "/include:_originalImageInitializer")
#endif

class OriginalPhysicsModule {
    HMODULE image_ = nullptr;
    HANDLE activation_ = INVALID_HANDLE_VALUE;
    ULONG_PTR cookie_ = 0;
    std::vector<HMODULE> imports_;
    HMODULE openmp_ = nullptr;
#if defined(_M_IX86)
    bool crtHeap_ = false, crtThreads_ = false, crtInitialized_ = false;
    void InitializeCrt() {
        // IDA: x86 __tmainCRTStartup, VA 50B0EA. The executable statically
        // links VC9 CRT, so resolving the IAT alone does not initialize it.
        Function<void (*)()>(0x11AF7D)(); // __security_init_cookie
#pragma warning(suppress : 4996)          // Match the original VC9 startup's OS globals.
        const DWORD version = GetVersion();
        *Function<DWORD*>(0x145C10) = VER_PLATFORM_WIN32_NT;
        *Function<DWORD*>(0x145C1C) = LOBYTE(LOWORD(version));
        *Function<DWORD*>(0x145C20) = HIBYTE(LOWORD(version));
        *Function<DWORD*>(0x145C18) = (LOBYTE(LOWORD(version)) << 8) | HIBYTE(LOWORD(version));
        *Function<DWORD*>(0x145C14) = HIWORD(version) & 0x7FFF;
        Check(Function<int (*)(int)>(0x11993D)(1) != 0, "original CRT heap init");
        crtHeap_ = true;
        Check(Function<int (*)()>(0x10DC97)() != 0, "original CRT thread init");
        crtThreads_ = true;
        Check(Function<int (*)()>(0x1130DA)() >= 0, "original CRT IO init");
        Check(Function<int (*)(int)>(0x10AE85)(1) == 0, "original CRT C/C++ init");
        crtInitialized_ = true;
    }
#endif
    void Release() noexcept {
        if (image_) {
#if defined(_M_IX86)
            if (crtInitialized_)
                Function<void (*)()>(0x10B01B)(); // _cexit, without terminating the test
            if (crtThreads_)
                Function<void (*)()>(0x10D982)(); // _mtterm: remove callbacks before unmapping
            if (crtHeap_)
                if (const auto heap = *Function<HANDLE*>(0x146470))
                    HeapDestroy(heap);
            crtHeap_ = crtInitialized_ = crtThreads_ = false;
#endif
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
    template <class T> T Export(const char* name) const {
        const auto address = GetProcAddress(image_, name);
        Check(address != nullptr, name);
        return reinterpret_cast<T>(address);
    }
    void RenderWorkers(int count) const {
        using SetCount = void(__cdecl*)(int);
        const auto set = reinterpret_cast<SetCount>(GetProcAddress(openmp_, "omp_set_num_threads"));
        const auto dynamic = reinterpret_cast<SetCount>(GetProcAddress(openmp_, "omp_set_dynamic"));
        Check(set != nullptr, "original OpenMP thread count export");
        Check(dynamic != nullptr, "original OpenMP dynamic thread export");
        dynamic(0);
        set(count);
    }
    template <class T> T Function(std::size_t rva) const {
        return reinterpret_cast<T>(reinterpret_cast<unsigned char*>(image_) + rva);
    }
    OriginalPhysicsModule(const OriginalPhysicsModule&) = delete;
    OriginalPhysicsModule& operator=(const OriginalPhysicsModule&) = delete;
    explicit OriginalPhysicsModule(const std::filesystem::path& path) {
        try {
            // Reserve the fixed x86 image address before hashing allocates a
            // large file buffer. No executable code runs before verification.
#if defined(_M_IX86)
            image_ = originalPreloadedImage;
            originalPreloadedImage = nullptr;
#endif
            if (!image_)
                image_ = LoadLibraryExW(path.c_str(), nullptr, DONT_RESOLVE_DLL_REFERENCES);
            Check(image_ != nullptr, "map reference executable");
            const std::array<unsigned char, 32> expected = {
#if defined(_M_X64)
                0x07, 0x51, 0x6f, 0xd3, 0xbf, 0x1e, 0x6b, 0x13, 0x39, 0x83, 0x6b,
                0x67, 0x73, 0xa1, 0x56, 0xf6, 0x1b, 0xdd, 0x6f, 0x84, 0x8e, 0xeb,
                0x62, 0x1f, 0xdd, 0xa0, 0x12, 0x37, 0x5d, 0xf3, 0x13, 0xa1
#else
                0x75, 0x66, 0xea, 0xfc, 0xbd, 0x10, 0x99, 0xce, 0x74, 0xd3, 0x4f,
                0x2c, 0x0f, 0xfc, 0x89, 0xea, 0x3b, 0xd3, 0x2e, 0x63, 0x63, 0x91,
                0x2f, 0xd0, 0x7f, 0xcf, 0xe8, 0x22, 0xb2, 0xe7, 0x8d, 0xbd
#endif
            };
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
            auto* base = reinterpret_cast<unsigned char*>(image_);
            auto* dos = reinterpret_cast<IMAGE_DOS_HEADER*>(base);
            auto* nt = reinterpret_cast<IMAGE_NT_HEADERS*>(base + dos->e_lfanew);
#if defined(_M_IX86)
            if (reinterpret_cast<std::uintptr_t>(base) != nt->OptionalHeader.ImageBase) {
                MEMORY_BASIC_INFORMATION memory{};
                VirtualQuery(reinterpret_cast<void*>(0x400000), &memory, sizeof memory);
                std::fprintf(stderr, "reference base=%p expected=%08X test=%p\n", base,
                             nt->OptionalHeader.ImageBase, GetModuleHandleW(nullptr));
                std::fprintf(stderr, "0x400000 allocation=%p size=%zu state=%x type=%x\n",
                             memory.AllocationBase, memory.RegionSize, memory.State, memory.Type);
                wchar_t mapped[MAX_PATH]{};
                using MappedFileName = DWORD(WINAPI*)(HANDLE, void*, wchar_t*, DWORD);
                const auto mappedName = reinterpret_cast<MappedFileName>(
                    GetProcAddress(GetModuleHandleW(L"kernel32.dll"), "K32GetMappedFileNameW"));
                if (mappedName && mappedName(GetCurrentProcess(), reinterpret_cast<void*>(0x400000),
                                             mapped, MAX_PATH))
                    std::fwprintf(stderr, L"mapped file: %ls\n", mapped);
            }
            Check(reinterpret_cast<std::uintptr_t>(base) == nt->OptionalHeader.ImageBase,
                  "fixed-base x86 reference must map at 0x400000");
#endif
            auto* descriptor = reinterpret_cast<IMAGE_IMPORT_DESCRIPTOR*>(
                base +
                nt->OptionalHeader.DataDirectory[IMAGE_DIRECTORY_ENTRY_IMPORT].VirtualAddress);
            for (; descriptor->Name; ++descriptor) {
                const auto* name = reinterpret_cast<const char*>(base + descriptor->Name);
                HMODULE library = LoadLibraryA(name);
                Check(library != nullptr, name);
                if (_stricmp(name, "VCOMP90.DLL") == 0) {
                    openmp_ = library;
                    wchar_t runtime[MAX_PATH]{};
                    GetModuleFileNameW(library, runtime, MAX_PATH);
                    std::wprintf(L"original OpenMP runtime: %ls\n", runtime);
                }
                imports_.push_back(library);
                auto* names =
                    reinterpret_cast<IMAGE_THUNK_DATA*>(base + descriptor->OriginalFirstThunk);
                auto* slots = reinterpret_cast<IMAGE_THUNK_DATA*>(base + descriptor->FirstThunk);
                for (; names->u1.AddressOfData; ++names, ++slots) {
                    const char* symbol = IMAGE_SNAP_BY_ORDINAL(names->u1.Ordinal)
                                             ? MAKEINTRESOURCEA(IMAGE_ORDINAL(names->u1.Ordinal))
                                             : reinterpret_cast<IMAGE_IMPORT_BY_NAME*>(
                                                   base + names->u1.AddressOfData)
                                                   ->Name;
                    const auto address = GetProcAddress(library, symbol);
                    Check(address != nullptr, "resolve reference import");
                    DWORD protection;
                    Check(VirtualProtect(slots, sizeof(*slots), PAGE_READWRITE, &protection) != 0,
                          "make reference import slot writable");
                    slots->u1.Function = reinterpret_cast<ULONG_PTR>(address);
                    VirtualProtect(slots, sizeof(*slots), protection, &protection);
                }
            }
#if defined(_M_IX86)
            InitializeCrt();
#endif
        } catch (...) {
            Release();
            throw;
        }
    }
    ~OriginalPhysicsModule() { Release(); }
};
