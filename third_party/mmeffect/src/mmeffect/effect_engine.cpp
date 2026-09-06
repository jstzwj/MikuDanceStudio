// effect_engine.cpp - see effect_engine.h
#include "effect_engine.h"

#include <direct.h>
#include <map>
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

void MmeEngineTerm()
{
    // [0x18001eeb0] FUN_18001eeb0: release every cache entry, then drop the
    // containers (the originals' shared_ptr refcounts reach zero here).
    EffectCache& effects = EffectCacheRef();
    for (EffectCache::iterator it = effects.begin(); it != effects.end(); ++it) {
        // [PHASE3 wiring] release the parsed SAS model with each entry.
        if (it->second != nullptr && it->second->sas != nullptr) {
            SasUnload(it->second->sas);
            it->second->sas = nullptr;
        }
        it->second.reset();
    }
    effects.clear();

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
    EffectCache& effects = EffectCacheRef();
    for (EffectCache::iterator it = effects.begin(); it != effects.end(); ++it) {
        LoadedEffect& entry = *it->second;
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
    EffectCache& effects = EffectCacheRef();
    for (EffectCache::iterator it = effects.begin(); it != effects.end(); ++it) {
        LoadedEffect& entry = *it->second;
        if (entry.effect != nullptr) {
            HRESULT step = entry.effect->OnResetDevice();
            if (SUCCEEDED(hr) && FAILED(step)) {
                hr = step;   // first failure wins, like OnResetDevice's chain
            }
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

std::shared_ptr<LoadedEffect> MmeEngineLoadEffectFile(IDirect3DDevice9* device,
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

    // [big-C 11296-11305] the preprocessor define block:
    //   "_INDEX", "PSIZE15", plus "MME_MIPMAP" when DAT_1800d99da is set.
    D3DXMACRO defines[4];
    memset(&defines, 0, sizeof(defines));
    int defineCount = 0;
    defines[defineCount].Name = "_INDEX";
    defines[defineCount].Definition = "1";
    ++defineCount;
    defines[defineCount].Name = "PSIZE15";
    defines[defineCount].Definition = "1";
    ++defineCount;
    if (g_engineMipFilterOk != 0) {
        defines[defineCount].Name = "MME_MIPMAP";
        defines[defineCount].Definition = "1";   // [big-C 11303-11304] DAT_1800b3b83
        ++defineCount;
    }

    // [big-C 11258-11327] CWD switch around the load (relative resources).
    ScopedChdir chdirScope(pathAnsi);

    ID3DXBuffer* errors = nullptr;
    HRESULT hr = D3DXCreateEffectFromFileW(device, pathWide.c_str(), defines,
                                           nullptr, D3DXFX_NOT_CLONEABLE,
                                           g_effectPool, &entry->effect, &errors);
    if (errors != nullptr) {
        const char* text = static_cast<const char*>(errors->GetBufferPointer());
        if (text != nullptr) {
            entry->errorText += text;
            SIZE_T len = errors->GetBufferSize();
            if (len > 0 && entry->errorText.size() > 0 &&
                entry->errorText[entry->errorText.size() - 1] != '\n') {
                entry->errorText += "\n";
            }
        }
        errors->Release();
        errors = nullptr;
    }

    if (FAILED(hr)) {
        // [big-C 11328-11364] "DirectX Error: <desc> [%08X]\n" into the error
        // text; the apply path (FUN_18000B880) logs + message-boxes it.
        const char* desc = MmeDxErrDescription(static_cast<unsigned long>(hr));
        char hex[16];
        sprintf_s(hex, sizeof(hex), "%08X", static_cast<unsigned int>(hr));
        entry->errorText += "DirectX Error: ";
        entry->errorText += (desc != nullptr ? desc : "");
        entry->errorText += " [";
        entry->errorText += hex;
        entry->errorText += "]\n";
        if (entry->effect != nullptr) {
            entry->effect->Release();
            entry->effect = nullptr;
        }
    }

    // [PHASE3 wiring; original: SAS parse right after the effect is usable]
    // FUN_18000c470 runs once per loaded effect; its log text goes to the
    // effect log exactly like the original's per-effect log string (sas+0x70).
    if (entry->effect != nullptr) {
        entry->sas = SasParse(entry->effect, pathAnsi, device);
        if (entry->sas != nullptr) {
            const char* sasLog = SasGetLog(entry->sas);
            if (sasLog != nullptr && sasLog[0] != '\0') {
                MmeLogWrite(sasLog, 0);
            }
        }
    }

    cache[pathAnsi] = entry;
    return entry;
}

void MmeEngineUnloadEffectFile(const std::string& pathAnsi)
{
    // [0x18000b210] "Unload effect file: <path>\n\n" then release.
    EffectCache& cache = EffectCacheRef();
    EffectCache::iterator it = cache.find(pathAnsi);
    if (it == cache.end()) {
        return;
    }
    MmeLogWrite(("Unload effect file: " + pathAnsi + "\n\n").c_str(), 0);
    // [PHASE3 wiring] FUN_18000b210 releases the parsed SAS model too.
    if (it->second != nullptr && it->second->sas != nullptr) {
        SasUnload(it->second->sas);
        it->second->sas = nullptr;
    }
    cache.erase(it);
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
    EffectCache& effects = EffectCacheRef();
    for (EffectCache::iterator it = effects.begin(); it != effects.end(); ++it) {
        if (it->second == nullptr || it->second->sas == nullptr) {
            continue;
        }
        if (!visit(user, it->second->sas)) {
            return false;
        }
    }
    return true;
}

} // namespace mme
