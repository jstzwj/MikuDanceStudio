// Exercise the production loader with real D3DX, files and include resolution.
// Native references: x86 MMEffect RVA 0xCD10 (A), x64 RVA 0xC16A (W).
#include "effect_engine.h"
#include "mme_context.h"
#include "mme_globals.h"
#include "mme_util.h"

#include <cstdio>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <stdexcept>

namespace {
void Check(bool ok, const char* message) {
    if (!ok)
        throw std::runtime_error(message);
}
template <class T> struct Com {
    T* p = nullptr;
    ~Com() {
        if (p)
            p->Release();
    }
};
struct Window {
    HWND handle = CreateWindowW(L"STATIC", L"Effect path regression", WS_OVERLAPPED, 0, 0, 64, 64,
                                nullptr, nullptr, GetModuleHandleW(nullptr), nullptr);
    ~Window() {
        if (handle)
            DestroyWindow(handle);
    }
};
struct Runtime {
    explicit Runtime(IDirect3DDevice9* device) {
        mme::g_context = new mme::MmeContext(device);
        mme::MmeEngineInit(device, false);
    }
    ~Runtime() {
        mme::MmeEngineTerm();
        if (mme::g_effectPool) {
            mme::g_effectPool->Release();
            mme::g_effectPool = nullptr;
        }
        if (mme::g_offscreenSurface) {
            mme::g_offscreenSurface->Release();
            mme::g_offscreenSurface = nullptr;
        }
        delete mme::g_context;
        mme::g_context = nullptr;
    }
};
struct Files {
    std::filesystem::path directory = std::filesystem::temp_directory_path() /
                                      ("mme-path-" + std::to_string(GetCurrentProcessId()));
    ~Files() {
        std::error_code error;
        std::filesystem::remove_all(directory, error);
    }
};
void Write(const std::filesystem::path& path, const char* text) {
    std::ofstream output(path, std::ios::binary);
    output << text;
    Check(output.good(), "write effect fixture");
}
bool Representable(const std::wstring& value) {
    return mme::MmeAnsiToWide(mme::MmeWideToAnsi(value.c_str()).c_str()) == value;
}
std::wstring NonAsciiName() {
    // Always exercise Japanese paths when the process ACP supports them.
    // On other legacy locales, use a representable non-ASCII name to catch
    // the same UTF-8/ANSI mismatch without changing the machine's locale.
    for (const wchar_t* candidate : {L"\u65e5\u672c\u8a9e", L"\u00e9", L"\u0416", L"\u03a9",
                                     L"\u011f", L"\u05d0", L"\u0639", L"\u0e01"}) {
        if (Representable(candidate))
            return candidate;
    }
    throw std::runtime_error("no non-ASCII name representable in the process ACP");
}

void CompilePaths() {
    // This checks the native file/compiler path even on a locked desktop.
    // IUnknown is the SDK compiler interface's prefix; only Release is used.
    const auto runtime = GetModuleHandleW(sizeof(void*) == 8 ? L"d3dx9_43.dll" : L"d3dx9_32.dll");
    using CompileA =
        HRESULT(WINAPI*)(LPCSTR, const D3DXMACRO*, ID3DXInclude*, DWORD, IUnknown**, ID3DXBuffer**);
    using CompileW = HRESULT(WINAPI*)(LPCWSTR, const D3DXMACRO*, ID3DXInclude*, DWORD, IUnknown**,
                                      ID3DXBuffer**);
    const auto compileA =
        reinterpret_cast<CompileA>(GetProcAddress(runtime, "D3DXCreateEffectCompilerFromFileA"));
    const auto compileW =
        reinterpret_cast<CompileW>(GetProcAddress(runtime, "D3DXCreateEffectCompilerFromFileW"));
    Check(compileA && compileW, "native compiler exports are available");
    Files files;
    struct RestoreDirectory {
        std::filesystem::path previous = std::filesystem::current_path();
        ~RestoreDirectory() {
            std::error_code error;
            std::filesystem::current_path(previous, error);
        }
    } restore;
    const auto nonAscii = NonAsciiName();
    for (const auto& folder : {std::wstring(L"ascii"), nonAscii}) {
        const auto directory = files.directory / folder;
        std::filesystem::create_directories(directory);
        // Original MMEffect supplies this CWD to the older native compiler.
        Check(SetCurrentDirectoryA(mme::MmeWideToAnsi(directory.c_str()).c_str()) != FALSE,
              "set original ANSI include directory");
        Write(directory / "common.fxh", "#define VALUE 42\n");
        for (const auto& name : {std::wstring(L"plain.fx"), nonAscii + L".fx"}) {
            const auto path = directory / name;
            Check(Representable(path.wstring()), "native compiler path roundtrips through ACP");
            for (bool missingInclude : {false, true}) {
                Write(
                    path,
                    missingInclude
                        ? "#include \"missing_include.fxh\"\ntechnique Main { pass P {} }\n"
                        : "#include \"common.fxh\"\nfloat Marker = VALUE;\ntechnique Main { pass P {} }\n");
                const auto ansi = mme::MmeWideToAnsi(path.c_str());
                Com<IUnknown> compiler;
                Com<ID3DXBuffer> errors;
#if defined(_M_X64)
                const HRESULT result = compileW(mme::MmeAnsiToWide(ansi.c_str()).c_str(), nullptr,
                                                nullptr, 0x1000, &compiler.p, &errors.p);
#else
                const HRESULT result =
                    compileA(ansi.c_str(), nullptr, nullptr, 0, &compiler.p, &errors.p);
#endif
                if (!missingInclude) {
                    if (FAILED(result) && errors.p)
                        std::fprintf(stderr, "%s\n",
                                     static_cast<const char*>(errors.p->GetBufferPointer()));
                    Check(result == S_OK && compiler.p,
                          "native compiler opens non-ASCII effect and relative include");
                } else {
                    Check(FAILED(result) && !compiler.p && errors.p &&
                              std::strstr(static_cast<const char*>(errors.p->GetBufferPointer()),
                                          "missing_include.fxh"),
                          "native compiler preserves include failure diagnostic");
                }
            }
        }
    }
    std::printf("Native compiler paths: ACP=%u, 4 paths and include failures passed\n", GetACP());
}
float Read(ID3DXEffect* effect, const char* name) {
    float value = -1;
    Check(SUCCEEDED(effect->GetFloat(name, &value)), "read compiled effect parameter");
    return value;
}

void Exercise(IDirect3DDevice9* device) {
    Files files;
    const auto nonAscii = NonAsciiName();
    const auto originalDirectory = std::filesystem::current_path();
    for (const auto& folder : {std::wstring(L"ascii"), nonAscii}) {
        const auto directory = files.directory / folder;
        std::filesystem::create_directories(directory);
        Write(directory / "common.fxh", "#define INCLUDED_VALUE 42\n");
        for (const auto& name : {std::wstring(L"plain.fx"), nonAscii + L".fx"}) {
            const auto path = directory / name;
            Write(path, "#include \"common.fxh\"\n"
                        "#ifndef _INDEX\n#error missing original _INDEX macro\n#endif\n"
                        "float Marker = INCLUDED_VALUE;\n"
                        "#ifdef MME_MIPMAP\nfloat Mipmap = 1;\n#else\nfloat Mipmap = 0;\n#endif\n"
                        "technique Main { pass P {} }\n");
            Check(Representable(path.wstring()), "effect path roundtrips through original ACP");
            const auto ansi = mme::MmeWideToAnsi(path.c_str());
            for (unsigned char debug : {0, 1}) {
                for (unsigned char mipmap : {0, 1}) {
                    mme::MmeEngineClearCaches();
                    mme::g_debugMode = debug;
                    mme::g_engineMipFilterOk = mipmap;
                    auto loaded = mme::MmeEngineLoadEffectFile(device, ansi);
                    if (!loaded || !loaded->effect) {
                        std::fprintf(stderr, "ACP=%u debug=%u mipmap=%u: %s\n", GetACP(), debug,
                                     mipmap, loaded ? loaded->errorText.c_str() : "null entry");
                        Check(false, "load effect from representable non-ASCII paths");
                    }
                    Check(loaded->fileStamp != 0 && loaded->errorText.empty(),
                          "successful file gate and compile");
                    Check(Read(loaded->effect, "Marker") == 42 &&
                              Read(loaded->effect, "Mipmap") == mipmap,
                          "relative include and original macro definitions");
                    Check(loaded->originalSource && loaded->legacySource && loaded->modernSource,
                          "portable import retains native source snapshots for every path");
                    Check(loaded->legacySource->find("INCLUDED_VALUE") == std::string::npos &&
                              loaded->modernSource->find("INCLUDED_VALUE") == std::string::npos,
                          "portable source snapshots resolve the same relative include");
                    Check(mme::MmeEngineLoadEffectFile(device, ansi) == loaded,
                          "repeated load reuses the compiled source");
                    Check(std::filesystem::current_path() == originalDirectory,
                          "successful load restores working directory");
                }
            }
            mme::MmeEngineClearCaches();
            mme::g_debugMode = mme::g_engineMipFilterOk = 0;
            Write(path, "#include \"missing_include.fxh\"\ntechnique Main { pass P {} }\n");
            auto failure = mme::MmeEngineLoadEffectFile(device, ansi);
            Check(failure && !failure->effect && failure->fileStamp != 0 &&
                      failure->errorText.find("missing_include.fxh") != std::string::npos,
                  "include failure retains native compiler diagnostic");
            Check(std::filesystem::current_path() == originalDirectory,
                  "failed load restores working directory");
            mme::MmeEngineUnloadEffectFile(ansi, failure);
            Write(path, "float Marker = 7; technique Main { pass P {} }\n");
            auto retry = mme::MmeEngineLoadEffectFile(device, ansi);
            Check(retry && retry->effect && Read(retry->effect, "Marker") == 7,
                  "corrected effect loads after failed entry invalidation");
        }
    }
    const auto missing = mme::MmeWideToAnsi((files.directory / "missing.fx").c_str());
    auto entry = mme::MmeEngineLoadEffectFile(device, missing);
    Check(entry && !entry->effect && entry->fileStamp == 0 && entry->errorText.empty(),
          "missing files stop at the original ANSI file gate");
    // The original also rejects paths that the active ANSI code page cannot
    // represent; switching only the compiler to W must not bypass that gate.
    if (!Representable(L"\u65e5\u672c\u8a9e")) {
        const auto path = files.directory / L"\u65e5\u672c\u8a9e.fx";
        Write(path, "technique Main { pass P {} }\n");
        const auto ansi = mme::MmeWideToAnsi(path.c_str());
        auto rejected = mme::MmeEngineLoadEffectFile(device, ansi);
        Check(rejected && !rejected->effect && rejected->fileStamp == 0,
              "unrepresentable Japanese paths retain the original ACP limitation");
    }
    mme::MmeEngineClearCaches();
    std::printf("Effect paths: ACP=%u, 16 loads, 4 include failures and retries passed\n",
                GetACP());
}
} // namespace

int main(int argc, char** argv) {
    SetErrorMode(SEM_FAILCRITICALERRORS | SEM_NOGPFAULTERRORBOX);
    try {
        if (argc == 2 && std::strcmp(argv[1], "--compiler") == 0) {
            CompilePaths();
            return 0;
        }
        Window window;
        Com<IDirect3D9> d3d;
        d3d.p = Direct3DCreate9(D3D_SDK_VERSION);
        if (!window.handle || !d3d.p)
            return 77;
        D3DPRESENT_PARAMETERS pp{};
        pp.Windowed = TRUE;
        pp.SwapEffect = D3DSWAPEFFECT_DISCARD;
        pp.hDeviceWindow = window.handle;
        pp.BackBufferWidth = pp.BackBufferHeight = 64;
        pp.BackBufferFormat = D3DFMT_A8R8G8B8;
        Com<IDirect3DDevice9> device;
        // The full SAS loader needs HAL capabilities, unlike the raw compile
        // probe; do not substitute NULLREF and claim an equivalent result.
        const HRESULT result = d3d.p->CreateDevice(
            0, D3DDEVTYPE_HAL, window.handle, D3DCREATE_SOFTWARE_VERTEXPROCESSING, &pp, &device.p);
        if (FAILED(result)) {
            std::printf("SKIP: HAL device unavailable (%08lx)\n", result);
            return 77;
        }
        Runtime runtime(device.p);
        Exercise(device.p);
        return 0;
    } catch (const std::exception& error) {
        std::fprintf(stderr, "Effect paths: %s\n", error.what());
        return 1;
    }
}
