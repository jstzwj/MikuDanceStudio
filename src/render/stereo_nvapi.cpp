// ===========================================================================
// VA 0x00407400 / 0x00407450 / 0x004CB430 - NVIDIA stereo bridge
// ===========================================================================
#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <Windows.h>

#include <cstdint>

#include "mikudancestudio/mmd_app.hpp"
#include "mikudancestudio/ported_funcs.hpp"

namespace mikudancestudio {
namespace {

using QueryInterface = void*(__cdecl*)(std::uint32_t);

// NVAPI interface ids resolved through nvapi_QueryInterface (opaque
// selectors, kept in hex; names follow NVIDIA's nvapi headers).
constexpr std::uint32_t kNvapiInitializeId = 0x0150E828u;
constexpr std::uint32_t kNvapiStereoCreateHandleId = 0xAC7E37F4u;
constexpr std::uint32_t kNvapiStereoIsActivatedId = 0x1FB0BC30u;
constexpr std::uint32_t kNvapiStereoActivateId = 0xF6A1AD68u;
constexpr std::uint32_t kNvapiStereoSetConvergenceId = 0x3DD6B54Bu;
constexpr std::uint32_t kNvapiStereoCapsId = 0xBE7692ECu;
constexpr std::uint32_t kNvapiStereoSupportId = 0x239C4545u;

// Mirrors the original globals: hLibModule @0x545940 and the init-time
// nvapi_QueryInterface pointer @0x545944 established by sub_4C6940.
HMODULE g_nvapiModule = nullptr;
QueryInterface g_nvapiQuery = nullptr;
bool g_nvapiAttempted = false;

int NvapiInitImpl() { // 0x4C6940
    if (g_nvapiModule == nullptr) {
        // x86 loads "nvapi.dll" (0x4C695F); the x64 twin sub_7FF7CB4FEA80
        // loads "nvapi64.dll" (@0x4FEAA4).
        g_nvapiModule = LoadLibraryA(sizeof(void*) == 8 ? "nvapi64.dll" : "nvapi.dll");
        if (g_nvapiModule == nullptr)
            return -2; // 0x4C696D
    }
    g_nvapiQuery =
        reinterpret_cast<QueryInterface>(GetProcAddress(g_nvapiModule, "nvapi_QueryInterface"));
    if (g_nvapiQuery != nullptr) {
        using Initialize = int(__cdecl*)();
        auto initialize = reinterpret_cast<Initialize>(g_nvapiQuery(kNvapiInitializeId));
        if (initialize == nullptr || initialize() != 0) {
            g_nvapiQuery = nullptr;
            FreeLibrary(g_nvapiModule); // 0x4C69DD
            g_nvapiModule = nullptr;
            return -1; // 0x4C69ED
        }
        // 0x4C6A2F..0x4C6A75: the two trace hooks (interface ids
        // 0x33C7358C / 0x593E8644 -> dword_545948/dword_54594C) are pure
        // nvapi-side instrumentation in the original and are omitted.
        return 0;
    }
    FreeLibrary(g_nvapiModule); // 0x4C699D
    g_nvapiModule = nullptr;
    return -1; // 0x4C69AD
}

QueryInterface NvQuery() {
    if (!g_nvapiAttempted) {
        g_nvapiAttempted = true;
        NvapiInitImpl();
    }
    return g_nvapiQuery;
}

template <typename Fn> Fn Resolve(std::uint32_t id) {
    QueryInterface query = NvQuery();
    return query != nullptr ? reinterpret_cast<Fn>(query(id)) : nullptr;
}

bool ActivateStereo(D3DRenderer* wrapper) { // 0x407400
    void* handle = wrapper->stereoHandle;   // +120020
    if (handle == nullptr)
        return false;
    using IsActivated = int(__cdecl*)(void*, std::uint8_t*);
    using Activate = int(__cdecl*)(void*);
    auto isActivated = Resolve<IsActivated>(kNvapiStereoIsActivatedId);
    auto activate = Resolve<Activate>(kNvapiStereoActivateId);
    std::uint8_t active = 0;
    if (isActivated != nullptr)
        isActivated(handle, &active);
    return active != 0 || (activate != nullptr && activate(handle) == 0);
}

void SetStereoConvergence(D3DRenderer* wrapper, float value) { // 0x407450
    void* handle = wrapper->stereoHandle;                      // +120020
    using SetConvergence = int(__cdecl*)(void*, float);
    auto set = Resolve<SetConvergence>(kNvapiStereoSetConvergenceId);
    if (handle != nullptr && set != nullptr)
        set(handle, value);
}

} // namespace

bool ProbeStereo3D(void* device, void* handleSlot) { // 0x4CB430 wrapper
    using CreateHandle = int(__cdecl*)(void*, void**);
    auto create = Resolve<CreateHandle>(kNvapiStereoCreateHandleId);
    // The original caller clears wrapper+120166 when the NVAPI status is
    // non-zero, so this bool intentionally means "probe failed".
    return create == nullptr || create(device, static_cast<void**>(handleSlot)) != 0;
}

// VA 0x004CAF10 - stereo capability query, interface id 0xBE7692EC.
// One-shot resolution into the cached slot (dword_542548 + byte_54254C in
// the original), __cdecl(int) -> NvAPI status, -3 when unresolvable.
int NvapiStereoCaps(int arg) {
    using Caps = int(__cdecl*)(int);
    static Caps call = nullptr;
    static bool resolved = false;
    if (!resolved) {
        resolved = true;
        QueryInterface query = NvQuery();
        if (query != nullptr)
            call = reinterpret_cast<Caps>(query(kNvapiStereoCapsId));
    }
    if (call == nullptr)
        return -3;    // 0x4CAF56
    return call(arg); // 0x4CAF99
}

// VA 0x004CB210 - stereo support gate, interface id 0x239C4545.
// One-shot resolution (dword_542578 + byte_54257C), __cdecl(void) ->
// NvAPI status, -3 when unresolvable.
int NvapiStereoSupportGate() {
    using Gate = int(__cdecl*)();
    static Gate call = nullptr;
    static bool resolved = false;
    if (!resolved) {
        resolved = true;
        QueryInterface query = NvQuery();
        if (query != nullptr)
            call = reinterpret_cast<Gate>(query(kNvapiStereoSupportId));
    }
    if (call == nullptr)
        return -3; // 0x4CB256
    return call(); // 0x4CB292
}

int NvapiInitChain() {
    return NvapiInitImpl();
}

void UpdateFrameStereo(MMDApp* app) { // 0x46DC75..0x46DC9F
    D3DRenderer* wrapper = app->Renderer();
    if (wrapper == nullptr || wrapper->stereoEnabled == 0)
        return;
    if (app->state.stereoActivated == 0 && app->state.recordFullscreenActive != 0) {
        app->state.stereoActivated = ActivateStereo(wrapper) ? 1 : 0;
    }
    if (app->state.stereoActivated != 0)
        SetStereoConvergence(wrapper, -app->CameraDistance());
}

} // namespace mikudancestudio
