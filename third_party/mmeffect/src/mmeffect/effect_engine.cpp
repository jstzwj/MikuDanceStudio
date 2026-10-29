// effect_engine.cpp - see effect_engine.h
#include "effect_engine.h"

#include <direct.h>
#include <map>
#include <vector>
#include <set>
#include <algorithm>
#include <fstream>
#include <filesystem>
#include <stdio.h>
#include <stdlib.h>

#include "mme_globals.h"
#include "mme_log.h"
#include "mme_util.h"
#include "sas_interpreter.h"   // [PHASE3 wiring] SasParse/SasUnload/SasGetLog

namespace mme {

namespace {

// DXEffectCache (TD 0x1800D7220): shared_ptr-managed, keyed by file path.
// The original keyed by boost::shared_ptr<DXEffectCache>; std::shared_ptr has
// identical ownership semantics (documented divergence).
typedef std::map<std::string, std::shared_ptr<LoadedEffect>> EffectCache;
typedef std::map<std::string, std::shared_ptr<IDirect3DBaseTexture9>> TextureCache;

EffectCache& EffectCacheRef()
{
    static EffectCache cache;   // constructed on first use, like the original's
    return cache;               // function-local static engine containers
}

TextureCache& TextureCacheRef()
{
    static TextureCache cache;
    return cache;
}

std::vector<std::weak_ptr<LoadedEffect>>& EffectInstances()
{
    static std::vector<std::weak_ptr<LoadedEffect>> instances;
    return instances;
}

std::vector<std::shared_ptr<LoadedEffect>> LiveEffects()
{
    std::vector<std::shared_ptr<LoadedEffect>> result;
    for (const auto& entry : EffectCacheRef()) result.push_back(entry.second);
    auto& instances = EffectInstances();
    for (auto it = instances.begin(); it != instances.end();) {
        if (auto instance = it->lock()) {
            result.push_back(instance);
            ++it;
        } else {
            it = instances.erase(it);
        }
    }
    // A freshly compiled effect is also its first assignment. Deduplicate
    // that shared cache reference; later assignments own independent clones.
    std::set<LoadedEffect*> present;
    result.erase(std::remove_if(result.begin(), result.end(), [&](const auto& entry) {
        return !present.insert(entry.get()).second;
    }), result.end());
    // Cache invalidation can leave an old source retained by live instances.
    for (size_t i = 0; i < result.size(); ++i) {
        auto source = result[i]->source;
        if (source && present.insert(source.get()).second) result.push_back(source);
    }
    // Restore cached sources before the assignments that share their pool.
    std::stable_partition(result.begin(), result.end(),
        [](const auto& entry) { return !entry->source; });
    return result;
}

// [big-C 11258-11327] the loader switches the CWD to the effect directory so
// that relative resource paths inside the effect resolve, then restores it.
class ScopedChdir {
public:
    explicit ScopedChdir(const std::string& effectPath)
        : previous_(nullptr)
    {
        char* cwd = _getcwd(nullptr, 0);
        previous_ = cwd;   // may be null; the original guards this too
        char drive[3] = { 0 };
        char dir[0x100] = { 0 };
        if (_splitpath_s(effectPath.c_str(), drive, sizeof(drive), dir, sizeof(dir),
                         nullptr, 0, nullptr, 0) == 0) {
            char effectDir[0x104];
            if (_makepath_s(effectDir, sizeof(effectDir), drive, dir, nullptr, nullptr) == 0) {
                _chdir(effectDir);
            }
        }
    }
    ~ScopedChdir()
    {
        if (previous_ != nullptr) {
            _chdir(previous_);
            free(previous_);
            previous_ = nullptr;
        }
    }
    ScopedChdir(const ScopedChdir&) = delete;
    ScopedChdir& operator=(const ScopedChdir&) = delete;

private:
    char* previous_;
};

// [0x18000c08e / 0x18000c0fa] Named SDK constants for the original immediate flags
// (d3dx9shader.h; the port's d3dx9.h defines only D3DXFX_*, not D3DXSHADER_*,
// so name the SDK values locally here):
//   0x1000 = D3DXSHADER_ENABLE_BACKWARDS_COMPATIBILITY (1<<12), not
//            D3DXFX_NOT_CLONEABLE (=1<<11=0x800, consistent with DXSDK/Wine/ReactOS headers);
//   0xC0   = D3DXSHADER_FORCE_VS_SOFTWARE_NOOPT(0x40) |
//            D3DXSHADER_FORCE_PS_SOFTWARE_NOOPT(0x80), not
//            SKIPVALIDATION|SKIPOPTIMIZATION (those are 0x2|0x4=0x6).
const unsigned long kD3dxShaderEnableBackwardsCompatibility = 1u << 12; // 0x1000
const unsigned long kD3dxShaderForceVsSoftwareNoOpt = 1u << 6;          // 0x40
const unsigned long kD3dxShaderForcePsSoftwareNoOpt = 1u << 7;          // 0x80

} // namespace

// [0x180093660] FUN_180093660 - the DXErr9 HRESULT description lookup. The
// original walks the static description tables at 0x1800b2018-0x1800b2530 (the
// full dxerr9 catalog). Phase 2 ships a compact subset of the D3D/D3DX HRESULTs
// the engine path can actually produce; unknown codes fall back to the format
// "DirectX Error:  [%08X]" text. Divergence documented in
// PHASE2_IMPLEMENTATION_NOTES.md.
const char* MmeDxErrDescription(unsigned long hr)
{
    switch (hr) {
    case 0x00000000: return "S_OK";
    case 0x00000001: return "S_FALSE";
    case 0x80070005: return "E_ACCESSDENIED";
    case 0x8007000E: return "E_OUTOFMEMORY";
    case 0x80070057: return "E_INVALIDARG";
    case 0x80004001: return "E_NOTIMPL";
    case 0x80004002: return "E_NOINTERFACE";
    case 0x80004003: return "E_POINTER";
    case 0x80004004: return "E_ABORT";
    case 0x80004005: return "E_FAIL";
    case 0x80004006: return "E_UNEXPECTED";
    case 0x8876086A: return "D3DERR_INVALIDCALL";
    case 0x88760868: return "D3DERR_NOTAVAILABLE";
    case 0x8876086C: return "D3DERR_OUTOFVIDEOMEMORY";
    case 0x8876086B: return "D3DERR_WASSTILLDRAWING";
    case 0x88760829: return "D3DERR_DEVICELOST";
    case 0x88760824: return "D3DERR_DEVICENOTRESET";
    case 0x8876082B: return "D3DERR_DEVICEHUNG";
    case 0x88760817: return "D3DERR_DRIVERINTERNALERROR";
    case 0x88760B59: return "D3DXERR_INVALIDDATA";
    default: return nullptr;
    }
}

void MmeEngineInit(IDirect3DDevice9* device, bool mipFilterAvailable)
{
    // [0x18000a8e0] FUN_18000a8e0.
    MmeEngineTerm();                           // FUN_18001eeb0 [L10126]

    if (g_effectPool != nullptr) {             // DAT_1800d9a30
        g_effectPool->Release();
        g_effectPool = nullptr;
    }
    if (g_offscreenSurface != nullptr) {       // DAT_1800d9a38
        g_offscreenSurface->Release();
        g_offscreenSurface = nullptr;
    }

    // [L10135] D3DXCreateEffectPool(&DAT_1800d9a30).
    if (device != nullptr && D3DXCreateEffectPool(&g_effectPool) == D3D_OK) {
        // [L10137] CreateRenderTarget(0x10, 0x10, 0x16, 0, 0, 0, &surface, 0).
        // The arg shape matches the SDK signature; format 0x16 constant
        // UNCERTAIN (globals_structures.md section 11; 22 = X8R8G8B8 vs
        // 21 = A8R8G8B8).
        if (device->CreateRenderTarget(0x10, 0x10, static_cast<D3DFORMAT>(0x16),
                                       D3DMULTISAMPLE_NONE, 0, FALSE,
                                       &g_offscreenSurface, nullptr) == D3D_OK) {
            g_engineMipFilterOk = mipFilterAvailable ? 1 : 0;   // [L10139]
        }
    }
}

LoadedEffect::~LoadedEffect()
{
    // [0x18000b210] Complete FUN_18000B210 unload semantics, preserving original address-level ordering:
    //
    // 1. [0x18000b24b-0x18000b316] Log first: only if effect(+0x00) is non-null, record
    //    "Unload effect file: <path>\n\n" (sub_180009080(x, 0) only writes a log line);
    //    failed-load entries (effect == null) unload silently, matching the original `if (*a1)` gate.
    if (effect != nullptr) {
        MmeLogWrite(("Unload effect file: " + path + "\n\n").c_str(), 0);
    }
    // 2. [0x18000b31b-0x18000b5de] Release resources by semantic: the original traverses the semantic array
    //    (0x28 bytes/entry, tags 0x26/0x27/0x28/0x2C/0x2D/0x2E/0x33), destroying resource
    //    objects (sub_18000B660), offscreen depth (sub_18000B750), animated textures
    //    (vtable calls), and clearing render-turn vector(+0x1B8) plus both handle
    //    maps(+0xB8/+0xD8). All ported semantic resources (offscreen surface/depth/
    //    texture, CONTROLOBJECT tables, technique models) belong to SasEffect;
    //    SasUnload is their equivalent destruction chain.
    if (sas != nullptr) {
        SasUnload(sas);
        sas = nullptr;
    }
    // 3. [0x18000b614-0x18000b623] ID3DXEffect::Release()（(*a1)->vtbl+0x10）
    //    and clear the pointer, the original chain's last step.
    if (effect != nullptr) {
        effect->Release();
        effect = nullptr;
    }
}

void MmeEngineTerm()
{
    // [0x18001eeb0] FUN_18001eeb0: release every cache entry, then drop the
    // containers. Reference-count semantics: cache.clear() drops the cache reference; an entry is destroyed when
    // its last holder (the surviving MaterialBinding owner references) dies,
    // and ~LoadedEffect executes FUN_18000B210 semantics (log / SasUnload /
    // effect->Release), matching the original boost shared_ptr control block's disposal timing
    // exactly. Cleanup call order (callbacks.cpp: delete g_ownerManager /
    // g_context before MmeEngineTerm) ensures references are already zero at termination, so entries
    // are destroyed immediately here, matching original term's observable immediate per-entry unload behavior.
    EffectCacheRef().clear();

    TextureCache& textures = TextureCacheRef();
    for (TextureCache::iterator it = textures.begin(); it != textures.end(); ++it) {
        it->second.reset();
    }
    textures.clear();
}

void MmeEngineOnLostDevice()
{
    // [0x18005e640] the device-dependent resource release half: only the
    // ID3DXEffect objects expose OnLostDevice. Cached D3D textures survive a
    // device loss as objects (D3DPOOL_DEFAULT contents become invalid and are
    // re-created by the texture engine, Phase 3), so they are left in place.
    for (const auto& live : LiveEffects()) {
        LoadedEffect& entry = *live;
        if (entry.sas != nullptr) {
            SasReleaseDeviceResources(entry.sas);
        }
        if (entry.effect != nullptr) {
            entry.effect->OnLostDevice();
        }
    }
}

HRESULT MmeEngineOnResetDevice(IDirect3DDevice9* device)
{
    // [0x18002de40] the reacquire half. (void)device: the original's reacquire
    // walks the effect objects only; the 16x16 target is re-created at the
    // OnResetDevice call site (callbacks.cpp), matching the original order.
    HRESULT hr = S_OK;
    for (const auto& live : LiveEffects()) {
        LoadedEffect& entry = *live;
        if (entry.effect != nullptr) {
            HRESULT step = entry.effect->OnResetDevice();
            if (SUCCEEDED(hr) && FAILED(step)) {
                hr = step;   // first failure wins, like OnResetDevice's chain
            }
        }
        // D3DPOOL_DEFAULT SAS resources (offscreen render targets / depth
        // stencils) do not survive the reset - drop and re-create them, then
        // re-bind the texture parameters (PHASE3 note #7).
        if (entry.sas != nullptr && device != nullptr) {
            SasRecreateResources(entry.sas, device);
        }
    }
    return hr;
}

unsigned long MmeEngineQueryFileStamp(const std::string& pathAnsi)
{
    // [0x18000b7f0] the original opens the file and derives a validity token
    // from its content/size; the hot-reload check re-runs it every 100 ms. The
    // stamp value only needs to change when the file changes, so the file
    // length + mtime pair folded into one 32-bit value is equivalent for the
    // observable behavior (reload on change, skip on match).
    HANDLE handle = CreateFileA(pathAnsi.c_str(), GENERIC_READ, FILE_SHARE_READ,
                                nullptr, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, nullptr);
    if (handle == INVALID_HANDLE_VALUE) {
        return 0;
    }
    BY_HANDLE_FILE_INFORMATION info;
    memset(&info, 0, sizeof(info));
    unsigned long stamp = 0;
    if (GetFileInformationByHandle(handle, &info)) {
        stamp = info.nFileSizeLow ^ (info.nFileSizeHigh * 2654435761u) ^
                info.ftLastWriteTime.dwLowDateTime ^
                (info.ftLastWriteTime.dwHighDateTime * 40503u);
        if (stamp == 0) {
            stamp = 1;   // 0 means "invalid file"
        }
    }
    CloseHandle(handle);
    return stamp;
}

static std::shared_ptr<LoadedEffect> LoadCachedEffectFile(IDirect3DDevice9* device,
                                                      const std::string& pathAnsi)
{
    EffectCache& cache = EffectCacheRef();
    EffectCache::iterator existing = cache.find(pathAnsi);
    if (existing != cache.end()) {
        return existing->second;
    }

    std::shared_ptr<LoadedEffect> entry(new LoadedEffect());
    entry->path = pathAnsi;

    // [big-C 11211-11223] FUN_18000B7F0 gate: an unreadable file leaves the
    // entry with a null effect (the caller then reports the load failure).
    unsigned long stamp = MmeEngineQueryFileStamp(pathAnsi);
    entry->fileStamp = stamp;
    if (stamp == 0) {
        cache[pathAnsi] = entry;
        return entry;
    }

    // [big-C 11227-11229] "Loading effect file: <path>"
    MmeLogWrite(("Loading effect file: " + pathAnsi).c_str(), 0);

    // [big-C 11239-11240] the wide path for D3DXCreateEffectFromFileW
    // (FUN_180006610 = CP 0 ansi->wide, same as MmeAnsiToWide).
    std::wstring pathWide = MmeAnsiToWide(pathAnsi.c_str());
    {
        std::ifstream source(std::filesystem::path(pathWide), std::ios::binary | std::ios::ate);
        const auto size=source.tellg();
        if(source && size>0 && size<=1024*1024) {
            std::string text(static_cast<std::size_t>(size), '\0');
            source.seekg(0); source.read(text.data(), size);
            if(source) entry->originalSource=std::make_shared<const std::string>(std::move(text));
        }
    }

    // [big-C 11296-11305 / 0x18000c0a4-0x18000c0f3] The macro array contains only two entries:
    //   defines[0] = {"_INDEX", "PSIZE15"}: _INDEX's value is the string
    //     "PSIZE15" (0x18000c0b2); PSIZE15 is not a separate macro name;
    //   defines[1] = {"MME_MIPMAP", ""} is populated only when DAT_1800d99da (the mip latch set during engine
    //     initialization, tested for !=0 at 0x18000c0d5) is set. Definition points to the empty NUL string
    //     unk_1800b3b83 (0x18000c0f3). Zeroed remaining slots form the {NULL,NULL} terminator.
    D3DXMACRO defines[4];
    memset(&defines, 0, sizeof(defines));
    int defineCount = 0;
    defines[defineCount].Name = "_INDEX";
    defines[defineCount].Definition = "PSIZE15";   // [0x18000c0b2]
    ++defineCount;
    if (g_engineMipFilterOk != 0) {                // [0x18000c0d5] DAT_1800d99da
        defines[defineCount].Name = "MME_MIPMAP";
        defines[defineCount].Definition = "";      // [0x18000c0f3] unk_1800b3b83 (empty string)
        ++defineCount;
    }

    // [big-C 11258-11327] CWD switch around the load (relative resources).
    ScopedChdir chdirScope(pathAnsi);

    // [0x18000c08e-0x18000c0fa] Original flags = 0x1000 | (byte_1800D99DE ? 0xC0 : 0):
    //   bit12 is always set (bts edi,0Ch) = D3DXSHADER_ENABLE_BACKWARDS_COMPATIBILITY;
    //   0xC0 is included only in debug mode (cmovnz), byte_1800D99DE = g_debugMode
    //   (latched once by Initialize at 0x1800564f3 via MMHack import IsDebugMode(),
    //   meaning MMEffect.debug exists in the host EXE directory).
    // The x86 0.37 DLL (RVA 0xC970, call at 0xCD10) passes zero outside
    // debug mode. Enabling the x64 compatibility bit changes shader bytecode.
#if defined(_M_X64)
    unsigned long loadFlags = kD3dxShaderEnableBackwardsCompatibility; // 0x1000
#else
    unsigned long loadFlags = 0;
#endif
    if (g_debugMode != 0) {
        loadFlags |= kD3dxShaderForceVsSoftwareNoOpt | kD3dxShaderForcePsSoftwareNoOpt; // 0xC0
    }

    ID3DXBuffer* errors = nullptr;
    HRESULT hr = D3DXCreateEffectFromFileW(device, pathWide.c_str(), defines, nullptr, loadFlags,
                                           g_effectPool, &entry->effect, &errors);
    // [0x18000c1ab-0x18000c376] on failure exactly ONE segment is appended to
    // the error text: the compiler text when the error buffer carries bytes,
    // otherwise "DirectX Error: <desc> [%08X]\n" (the !errors ||
    // !GetBufferSize() branch at 0x18000c1be). On success the buffer is
    // released unread (0x18000c3d3).
    if (FAILED(hr)) {
        if (errors != nullptr && errors->GetBufferSize() != 0) {
            // [0x18000c1cd] compiler error text only, appended verbatim.
            const char* text = static_cast<const char*>(errors->GetBufferPointer());
            if (text != nullptr) {
                entry->errorText += text;
            }
        } else {
            // [0x18000c21c-0x18000c29f] "DirectX Error: <%s> [%08X]\n" into the
            // error text; the apply path (FUN_18000B880) logs + message-boxes it.
            const char* desc = MmeDxErrDescription(static_cast<unsigned long>(hr));
            char hex[16];
            sprintf_s(hex, sizeof(hex), "%08X", static_cast<unsigned int>(hr));
            entry->errorText += "DirectX Error: ";
            entry->errorText += (desc != nullptr ? desc : "");
            entry->errorText += " [";
            entry->errorText += hex;
            entry->errorText += "]\n";
        }
        if (entry->effect != nullptr) {
            entry->effect->Release();
            entry->effect = nullptr;
        }
    }
    if (errors != nullptr) {
        errors->Release();
        errors = nullptr;
    }

    // [0x18000c417-0x18000c432] SAS parse right after the compile succeeds,
    // exactly like FUN_18000BC90's load chain: the original calls
    // MME_SasParseStandardsGlobal (0x18000c470) then sub_180016900 and treats
    // ANY nonzero result as a load failure - the code returns straight to
    // FUN_18000B880 at 0x18000c41e/0x18000c42a, which logs errorText + "\n"
    // (0x18000ba10), unloads (sub_18000B210 at 0x18000ba43) and message-boxes
    // "Failed to load effect file:" + path + "\n\n" + errorText (0x18000bbcf,
    // dedup-gated). The host-side apply path (emm_manager.cpp) implements that
    // half and gates on effect == nullptr with a non-empty errorText - the
    // same entry shape the compile failure above leaves behind - so rejecting
    // here reproduces the original's popup + reject without adding a
    // MessageBox at this layer.
    if (entry->effect != nullptr) {
        std::string parseFailureLog;
        entry->sas = SasParse(entry->effect, pathAnsi, device, &parseFailureLog);
        if (entry->sas == nullptr) {
            // Hard parse failure (invalid SAS version / ScriptClass /
            // ScriptOrder / Script annotation / technique scan - the
            // 0x18000e435 / 0x18000e364 / 0x18000c940 / 0x18000ea41 family).
            // SasParse hands out the exact "Error: ..." lines it appended to
            // the destroyed parse model's log (the a1+0x70 report the
            // original's loader shows verbatim); the stand-in only covers a
            // pathological empty log.
            if (!parseFailureLog.empty()) {
                entry->errorText += parseFailureLog;
            } else {
                entry->errorText += "Error: failed to parse the effect.\n";
            }
            entry->effect->Release();
            entry->effect = nullptr;
        } else if (SasHadErrors(entry->sas)) {
            // [0x18000c41e] any error recorded during the parse walk made the
            // original fail the load: parameter validation, the resource
            // build (FUN_180011960 via sub_18000F3A0 - e.g. "Error: failed
            // to open file: ..." for a moved ResourceName asset, the RayMMD
            // killer), ANIMATEDTEXTURE construction, or the eager texture
            // creation whose failure only sets the sas+0x38 error flag. The
            // error lines sit in the per-effect log (a1+0x70) which
            // FUN_18000B880 shows verbatim in the failure report, so the
            // whole accumulated log becomes the error text.
            entry->errorText += SasGetLog(entry->sas);
            SasUnload(entry->sas);
            entry->sas = nullptr;
            entry->effect->Release();
            entry->effect = nullptr;
        } else {
            // Clean parse: the per-effect log (Info/Warning lines) goes to
            // the shared log dialog/history, like the original's sas+0x70 dump.
            const char* sasLog = SasGetLog(entry->sas);
            if (sasLog != nullptr && sasLog[0] != '\0') {
                MmeLogWrite(sasLog, 0);
            }
        }
    }

    cache[pathAnsi] = entry;
    return entry;
}

std::shared_ptr<LoadedEffect> MmeEngineLoadEffectFile(IDirect3DDevice9* device,
                                                     const std::string& pathAnsi,
                                                     int materialCount)
{
    auto loaded = LoadCachedEffectFile(device, pathAnsi);
    if (!loaded || !loaded->effect) {
        return loaded;
    }
    const auto key = std::make_pair(materialCount, g_skipValidation);
    auto validation = loaded->techniqueValidation.find(key);
    if (validation == loaded->techniqueValidation.end()) {
        validation = loaded->techniqueValidation.emplace(
            key, SasValidateTechniqueSelections(loaded->sas, materialCount)).first;
    }
    if (validation->second.empty()) {
        return loaded;
    }

    // Native load tail: x64 0x18000C432 / x86 0x1000CF5E rejects a nonzero
    // selection-table result. Keep the compiled cache entry intact: subset
    // restrictions can make this assignment invalid while another model's
    // smaller material range remains valid. The existing caller owns error
    // reporting and assignment/reload failure handling.
    auto failure = std::make_shared<LoadedEffect>();
    failure->path = pathAnsi;
    failure->fileStamp = loaded->fileStamp;
    failure->errorText = validation->second;
    return failure;
}

std::shared_ptr<LoadedEffect> MmeEngineCreateEffectInstance(
    IDirect3DDevice9* device, const std::shared_ptr<LoadedEffect>& source)
{
    if (!source || !source->effect) return nullptr;
    if (!source->assigned) {
        source->assigned = true;
        EffectInstances().push_back(source);
        return source;
    }
    auto instance = std::make_shared<LoadedEffect>();
    instance->source = source;
    instance->originalSource = source->originalSource;
    instance->path = source->path;
    instance->fileStamp = source->fileStamp;
    // Original cache-hit loader: CloneEffect, then parse this assignment's SAS.
    if (FAILED(source->effect->CloneEffect(device, &instance->effect))) return nullptr;
    ScopedChdir directory(source->path);
    instance->sas = SasParse(instance->effect, instance->path, device,
                             &instance->errorText);
    if (!instance->sas || SasHadErrors(instance->sas)) return nullptr;
    EffectInstances().push_back(instance);
    return instance;
}

void MmeEngineUnloadEffectFile(const std::string& pathAnsi,
                               const std::shared_ptr<LoadedEffect>& expected)
{
    // [0x18000b210] Reference-counted equivalent of FUN_18000b210: the original "unload effect file" operation
    // lets the cached entry's shared_ptr references converge: the cache map erases its own reference,
    // while the entry survives until the last binding that references it is destroyed (0x18000b5e9-0x18000b611
    // control-block InterlockedDecrement -> dispose). The port's cache.erase drops the cache
    // reference; actual resource release ("Unload effect file: <path>\n\n" log,
    // SasUnload, effect->Release) occurs in ~LoadedEffect when the last MaterialBinding owner
    // reference dies, matching original timing exactly.
    //
    // Callers (emm_manager.cpp failed-load cleanup / mme_ui.cpp Reload-All and automatic
    // hot reload) reassign effects immediately after unloading: reloading creates a new entry, binding resolution in
    // MmeEnsureMaterialBinding switches owner references, and the old entry then dies through its destruction chain.
    // No rendering frame occurs between unload and reassignment (synchronous within one message handler); the binding's
    // borrowed effect/sas pointers are updated to the new entry before the old one dies.
    auto& cache = EffectCacheRef();
    auto found = cache.find(pathAnsi);
    if (found != cache.end() && (!expected || found->second == expected)) {
        cache.erase(found);
    }
}

void MmeEngineClearCaches()
{
    // [0x18000a990] sub_18000A990: sub_1800203F0 destroys effect-cache tree nodes
    // (head 0x1800D9C68, count 0x1800D9C70); sub_180020580 destroys texture-cache tree nodes
    // (head 0x1800D9C88, count 0x1800D9C90). Clearing either tree drops only cache references:
    // entries with no binding references die immediately through ~LoadedEffect (unload log / SasUnload /
    // effect->Release); entries held by MaterialBinding::owner survive until that binding
    // resolves again and switches owner, matching original boost::shared_ptr control-block disposal
    // timing. The texture cache currently has no binding-side holders (MmeEngineCacheTexture only registers/
    // queries), so clearing releases everything, equivalent to original DXTextureCache's per-node destruction.
    EffectCacheRef().clear();
    TextureCacheRef().clear();
}

IDirect3DBaseTexture9* MmeEngineFindCachedTexture(const std::string& pathAnsi)
{
    TextureCache& cache = TextureCacheRef();
    TextureCache::iterator it = cache.find(pathAnsi);
    if (it == cache.end()) {
        return nullptr;
    }
    return it->second.get();
}

void MmeEngineCacheTexture(const std::string& pathAnsi, IDirect3DBaseTexture9* texture)
{
    TextureCache& cache = TextureCacheRef();
    TextureCache::iterator it = cache.find(pathAnsi);
    if (it != cache.end()) {
        it->second.reset(texture);   // replace (the cache owns via shared_ptr)
        return;
    }
    cache[pathAnsi].reset(texture);
}

void MmeEngineUncacheTexture(const std::string& pathAnsi)
{
    TextureCache& cache = TextureCacheRef();
    cache.erase(pathAnsi);
}

// [PHASE3 wiring] visit the parsed SAS model of every cached effect.
bool MmeEngineForEachSas(bool (*visit)(void* user, SasEffect* sas), void* user)
{
    if (visit == nullptr) {
        return true;
    }
    for (const auto& live : LiveEffects()) {
        if (live->sas == nullptr) {
            continue;
        }
        if (!visit(user, live->sas)) {
            return false;
        }
    }
    return true;
}

} // namespace mme
