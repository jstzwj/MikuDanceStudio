#define NOMINMAX
#include "mme_native_oracle.hpp"
#include "mme_abi.h"
#include "mmhack_api.h"
#include "MMDExport.h"
#include <bcrypt.h>
#include <array>
#include <fstream>
#include <iterator>
#include <cstring>
#include <cstdint>
#include <stdexcept>
#include <vector>

namespace {
NativeMmeOracle* active = nullptr;
// The original x86 DLL uses stdcall and pointer-width object IDs. The embedded
// port exposes cdecl callbacks with 64-bit IDs on both architectures.
#if defined(_M_X64)
using NativeObjectId = unsigned long long;
#else
using NativeObjectId = unsigned;
#endif
void Require(bool value, const char* message) {
    if (!value)
        throw std::runtime_error(message);
}
// Native MMHack registers zero-extended 32-bit object IDs even on x64.
// Keep this ABI conversion at the boundary; the port uses full pointer IDs.
unsigned long long NativeId() {
    return static_cast<unsigned>(GetCurrentModelID());
}
void* NativePmdId(int index) {
    return reinterpret_cast<void*>(static_cast<std::uintptr_t>(
        static_cast<unsigned>(reinterpret_cast<std::uintptr_t>(ExpGetPmdID(index)))));
}
void* NativeAcsId(int index) {
    return reinterpret_cast<void*>(static_cast<std::uintptr_t>(
        static_cast<unsigned>(reinterpret_cast<std::uintptr_t>(ExpGetAcsID(index)))));
}
unsigned long long HostId(unsigned long long id) {
    for (int i = 0; i < ExpGetPmdNum(); ++i) {
        auto value = reinterpret_cast<unsigned long long>(ExpGetPmdID(i));
        if (static_cast<unsigned>(value) == id)
            return value;
    }
    for (int i = 0; i < ExpGetAcsNum(); ++i) {
        auto value = reinterpret_cast<unsigned long long>(ExpGetAcsID(i));
        if (static_cast<unsigned>(value) == id)
            return value;
    }
    return id;
}
const wchar_t* WINAPI NativeMaterial(NativeObjectId id, unsigned long subset, int kind) {
    return GetMaterialName(HostId(id), subset, kind);
}
BOOL WINAPI NativeAttachment(NativeObjectId id, NativeObjectId* parent, int* bone) {
    unsigned long long hostParent = 0;
    const BOOL result = GetAcsAttachedPmd(HostId(id), &hostParent, bone);
    if (result)
        *parent = static_cast<unsigned>(hostParent);
    return result;
}
void WINAPI NativeLightMatrices(D3DMATRIX* view, D3DMATRIX* projection, D3DMATRIX* combined) {
    GetLightViewProjMatrix(view, projection, combined);
}
FARPROC Query(const char* library, const char* name) {
#define QUERY(fn)                                                                                  \
    if (std::strcmp(name, #fn) == 0)                                                               \
    return reinterpret_cast<FARPROC>(&fn)
    if (_stricmp(library, "MMHack.dll") == 0) {
        if (std::strcmp(name, "GetCurrentModelID") == 0)
            return reinterpret_cast<FARPROC>(&NativeId);
        if (std::strcmp(name, "GetMaterialName") == 0)
            return reinterpret_cast<FARPROC>(&NativeMaterial);
        if (std::strcmp(name, "GetAcsAttachedPmd") == 0)
            return reinterpret_cast<FARPROC>(&NativeAttachment);
        if (std::strcmp(name, "GetLightViewProjMatrix") == 0)
            return reinterpret_cast<FARPROC>(&NativeLightMatrices);
        QUERY(IsEditMode);
        QUERY(GetClearColor);
        QUERY(IsDebugMode);
        QUERY(GetMMDMainWindow);
        QUERY(GetCurrentEffect);
        QUERY(GetCurrentFrameTime);
        QUERY(SavedPMMFile);
        QUERY(GetDrawnWindow);
        QUERY(GetCurrentDrawType);
        QUERY(GetLightViewProjMatrix);
        QUERY(IsEffectFileUsed);
        QUERY(GetTexture);
        QUERY(GetToonTexture);
        QUERY(GetSphereMapTexture);
        QUERY(GetSphereMapMode);
        QUERY(IsToonUsed);
        QUERY(GetCurrentSubsetIndex);
        QUERY(LoadedPMMFile);
        QUERY(GetBlendMode);
    } else if (_stricmp(library, "MikuMikuDance.exe") == 0) {
        if (std::strcmp(name, "ExpGetPmdID") == 0)
            return reinterpret_cast<FARPROC>(&NativePmdId);
        if (std::strcmp(name, "ExpGetAcsID") == 0)
            return reinterpret_cast<FARPROC>(&NativeAcsId);
        QUERY(ExpGetPmdMaterial);
        QUERY(ExpGetAcsMaterial);
        QUERY(ExpGetRenderRepeatCount);
        QUERY(ExpSetRenderRepeatCount);
        QUERY(ExpGetAcsID);
        QUERY(ExpGetAcsNum);
        QUERY(ExpGetAcsDisp);
        QUERY(ExpGetAcsWorldMat);
        QUERY(ExpGetAcsOrder);
        QUERY(ExpGetPmdDisp);
        QUERY(ExpGetPmdOrder);
        QUERY(ExpGetAcsTr);
        QUERY(ExpGetAcsSi);
        QUERY(ExpGetAcsRz);
        QUERY(ExpGetAcsRy);
        QUERY(ExpGetAcsRx);
        QUERY(ExpGetAcsZ);
        QUERY(ExpGetAcsY);
        QUERY(ExpGetAcsX);
        QUERY(ExpGetPmdMorphValue);
        QUERY(ExpGetPmdMorphName);
        QUERY(ExpGetPmdMorphNum);
        QUERY(ExpGetPmdBoneName);
        QUERY(ExpGetPmdBoneNum);
        QUERY(ExpGetPmdID);
        QUERY(ExpGetPmdNum);
        QUERY(ExpGetPmdBoneWorldMat);
    }
#undef QUERY
    return nullptr;
}
template <class R, class... Args> auto Callback(const char* name, R (*)(Args...)) {
    auto result = GetProcAddress(static_cast<HMODULE>(active->Module()), name);
    Require(result != nullptr, "missing native MME callback");
    return reinterpret_cast<R(WINAPI*)(Args...)>(result);
}
template <class R, class... Args, class... Values>
R Invoke(const char* name, R (*port)(Args...), Values... values) {
    if (!active)
        return port(values...);
    return Callback(name, port)(values...);
}
}

struct NativeMmeOracle::Impl {
    HMODULE image = nullptr;
    std::vector<HMODULE> imports;
    using Entry = BOOL(WINAPI*)(HINSTANCE, DWORD, LPVOID);
    Entry entry = nullptr;
    bool attached = false;
    ~Impl() {
        if (attached)
            entry(image, DLL_PROCESS_DETACH, nullptr);
        if (image)
            FreeLibrary(image);
        for (auto it = imports.rbegin(); it != imports.rend(); ++it)
            FreeLibrary(*it);
    }
};
NativeMmeOracle::NativeMmeOracle(const std::filesystem::path& path, QueryResolver resolver)
    : impl(std::make_unique<Impl>()) {
#if defined(_M_X64)
    const std::array<unsigned char, 32> expected = {0x74, 0xb3, 0xf5, 0x88, 0x2a, 0x3b, 0xad, 0x28,
                                                    0x13, 0x1c, 0xbc, 0x57, 0x99, 0x4e, 0x2f, 0x96,
                                                    0x09, 0x38, 0xe6, 0xca, 0x85, 0x0f, 0xc7, 0x13,
                                                    0x90, 0x18, 0xce, 0x48, 0x22, 0xbd, 0x87, 0xdc};
#else
    const std::array<unsigned char, 32> expected = {0x2b, 0xb2, 0x40, 0xfd, 0x15, 0x72, 0x32, 0xa1,
                                                    0x0b, 0x75, 0x6a, 0x68, 0xee, 0x3d, 0x1b, 0x43,
                                                    0xcb, 0x6a, 0x1e, 0xf2, 0xfe, 0x37, 0xc0, 0x32,
                                                    0x1d, 0x83, 0x44, 0x5b, 0x91, 0x05, 0x89, 0xc4};
#endif
    std::ifstream input(path, std::ios::binary);
    std::vector<unsigned char> bytes((std::istreambuf_iterator<char>(input)), {});
    BCRYPT_ALG_HANDLE algorithm = nullptr;
    std::array<unsigned char, 32> hash{};
    Require(BCryptOpenAlgorithmProvider(&algorithm, BCRYPT_SHA256_ALGORITHM, nullptr, 0) == 0,
            "native MME SHA-256 provider");
    const auto status = BCryptHash(algorithm, nullptr, 0, bytes.data(),
                                   static_cast<ULONG>(bytes.size()), hash.data(), 32);
    BCryptCloseAlgorithmProvider(algorithm, 0);
    Require(status == 0 && hash == expected, "native MME SHA-256 mismatch");
    impl->image = LoadLibraryExW(path.c_str(), nullptr, DONT_RESOLVE_DLL_REFERENCES);
    Require(impl->image != nullptr, "map original MMEffect.dll");
    auto* base = reinterpret_cast<unsigned char*>(impl->image);
    auto* dos = reinterpret_cast<IMAGE_DOS_HEADER*>(base);
    auto* nt = reinterpret_cast<IMAGE_NT_HEADERS*>(base + dos->e_lfanew);
    Require(nt->OptionalHeader.DataDirectory[IMAGE_DIRECTORY_ENTRY_TLS].Size == 0,
            "unexpected native MME TLS");
    auto* descriptor = reinterpret_cast<IMAGE_IMPORT_DESCRIPTOR*>(
        base + nt->OptionalHeader.DataDirectory[IMAGE_DIRECTORY_ENTRY_IMPORT].VirtualAddress);
    for (; descriptor->Name; ++descriptor) {
        const auto* name = reinterpret_cast<const char*>(base + descriptor->Name);
        const bool host =
            _stricmp(name, "MMHack.dll") == 0 || _stricmp(name, "MikuMikuDance.exe") == 0;
        HMODULE library = host ? nullptr : LoadLibraryA(name);
        Require(host || library != nullptr, name);
        if (library)
            impl->imports.push_back(library);
        auto* names = reinterpret_cast<IMAGE_THUNK_DATA*>(base + descriptor->OriginalFirstThunk);
        auto* slots = reinterpret_cast<IMAGE_THUNK_DATA*>(base + descriptor->FirstThunk);
        for (; names->u1.AddressOfData; ++names, ++slots) {
            const auto* symbol =
                IMAGE_SNAP_BY_ORDINAL(names->u1.Ordinal)
                    ? MAKEINTRESOURCEA(IMAGE_ORDINAL(names->u1.Ordinal))
                    : reinterpret_cast<IMAGE_IMPORT_BY_NAME*>(base + names->u1.AddressOfData)->Name;
            auto address = host ? (resolver ? resolver(name, symbol) : Query(name, symbol))
                                : GetProcAddress(library, symbol);
            Require(address != nullptr, "resolve native MME import");
            DWORD protection;
            Require(VirtualProtect(slots, sizeof(*slots), PAGE_READWRITE, &protection) != 0,
                    "native MME import protection");
            slots->u1.Function = reinterpret_cast<ULONG_PTR>(address);
            Require(VirtualProtect(slots, sizeof(*slots), protection, &protection) != 0,
                    "restore native MME import protection");
        }
    }
    impl->entry = reinterpret_cast<Impl::Entry>(base + nt->OptionalHeader.AddressOfEntryPoint);
    Require(impl->entry(impl->image, DLL_PROCESS_ATTACH, nullptr) != 0, "native MME CRT attach");
    impl->attached = true;
}
NativeMmeOracle::~NativeMmeOracle() = default;
void* NativeMmeOracle::Module() const {
    return impl->image;
}
void SetMmeOracle(NativeMmeOracle* oracle) {
    active = oracle;
}
void NativeMmeOracle::LoadMapping(const std::filesystem::path& path, HWND window) {
#if defined(_M_X64)
    // IDA RVA 2E8D0: full production EMM parser and assignment, char* + HWND.
    const auto load = reinterpret_cast<bool (*)(const char*, HWND)>(
        reinterpret_cast<unsigned char*>(impl->image) + 0x2E8D0);
    Require(load(path.string().c_str(), window), "native EMM load");
#else
    // IDA RVA 2B970: path in ECX, HWND on the stack, callee pops four bytes.
    const auto load = reinterpret_cast<bool(__thiscall*)(const char*, HWND)>(
        reinterpret_cast<unsigned char*>(impl->image) + 0x2B970);
    Require(load(path.string().c_str(), window), "native EMM load");
#endif
}

extern "C" {
int OracleInitialize(IDirect3DDevice9* d) {
    return Invoke("Initialize", &Initialize, d);
}
void OracleCleanup(IDirect3DDevice9* d) {
    Invoke("Cleanup", &Cleanup, d);
}
void OracleOnBeginScene(IDirect3DDevice9* d) {
    Invoke("OnBeginScene", &OnBeginScene, d);
}
void OracleOnEndScene(IDirect3DDevice9* d) {
    Invoke("OnEndScene", &OnEndScene, d);
}
void OracleOnLostDevice(IDirect3DDevice9* d) {
    Invoke("OnLostDevice", &OnLostDevice, d);
}
void OracleOnResetDevice(IDirect3DDevice9* d) {
    Invoke("OnResetDevice", &OnResetDevice, d);
}
void OracleOnCreateModel(IDirect3DDevice9* d, unsigned long long id, const char* file, int kind,
                         unsigned long count, void* reserved, IUnknown* object) {
    if (!active) {
        OnCreateModel(d, id, file, kind, count, reserved, object);
        return;
    }
    using Create = void(WINAPI*)(IDirect3DDevice9*, NativeObjectId, const char*, int, unsigned long,
                                 void*, IUnknown*);
    const auto create = reinterpret_cast<Create>(
        GetProcAddress(static_cast<HMODULE>(active->Module()), "OnCreateModel"));
    Require(create != nullptr, "missing native MME create callback");
    create(d, static_cast<unsigned>(id), file, kind, count, reserved, object);
}
void OracleOnDeleteModel(IDirect3DDevice9* d, unsigned long long id) {
    if (!active) {
        OnDeleteModel(d, id);
        return;
    }
    using Delete = void(WINAPI*)(IDirect3DDevice9*, NativeObjectId);
    const auto remove = reinterpret_cast<Delete>(
        GetProcAddress(static_cast<HMODULE>(active->Module()), "OnDeleteModel"));
    Require(remove != nullptr, "missing native MME delete callback");
    remove(d, static_cast<unsigned>(id));
}
HRESULT OracleOnClear(IDirect3DDevice9* d, unsigned long count, const D3DRECT* rect,
                      unsigned long flags, D3DCOLOR color, float z, unsigned long stencil,
                      int main) {
    return Invoke("OnClear", &OnClear, d, count, rect, flags, color, z, stencil, main);
}
HRESULT OracleOnDrawPrimitive(IDirect3DDevice9* d, D3DPRIMITIVETYPE type, unsigned start,
                              unsigned count) {
    return Invoke("OnDrawPrimitive", &OnDrawPrimitive, d, type, start, count);
}
HRESULT OracleOnDrawIndexedPrimitive(IDirect3DDevice9* d, D3DPRIMITIVETYPE type, int base,
                                     unsigned min, unsigned vertices, unsigned start,
                                     unsigned count) {
    return Invoke("OnDrawIndexedPrimitive", &OnDrawIndexedPrimitive, d, type, base, min, vertices,
                  start, count);
}
}
