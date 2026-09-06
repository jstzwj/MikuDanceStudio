// effect_engine.h - the D3DX effect engine integration of MMEffect.dll.
//
// Evidence (subsystems.md section 3, 0x18000A000-0x18000C000):
//   - FUN_18000A8E0 (called by Initialize): releases the previous engine state,
//     D3DXCreateEffectPool -> DAT_1800d9a30, CreateRenderTarget(16,16,fmt 0x16)
//     -> DAT_1800d9a38, latches the mip-filter flag DAT_1800d99da.
//   - FUN_18000BC90: per-file effect loader. Logs "Loading effect file: <path>",
//     switches the CWD to the effect directory for the duration of the load
//     (getcwd/_chdir pair, big-C 11258-11327), then D3DXCreateEffectFromFileW
//     with the preprocessor defines {"_INDEX", "PSIZE15"} plus "MME_MIPMAP"
//     when the engine mip filter is available (0x1800b3b68/70/78, big-C
//     11296-11317). Failure appends "DirectX Error: <desc> [%08X]\n" to the
//     effect's error text (big-C 11328-11364).
//   - FUN_18000B880: apply entry. First use loads the file; load failure logs
//     the error text, then "Failed to load effect file:<path>\n\n" (English) or
//     the localized equivalent (GBK bytes at 0x1800b3ae8, see
//     PHASE2_IMPLEMENTATION_NOTES.md) via MessageBoxA(main, ..., MB_ICONERROR)
//     dedup-gated by DAT_1800d99d8, and unloads the failed effect.
//   - FUN_18000B210: unload. Logs "Unload effect file: <path>\n\n" (0x1800b3ac8)
//     when the object holds a path, then releases the effect/texture objects.
//   - FUN_18001EEB0: engine teardown (releases every cache entry + the pool).
//   - FUN_18005E640 / FUN_18002DE40: device lost/reset - release and reacquire
//     device-dependent resources (the loaded effects' OnLostDevice/OnResetDevice).
//   - DXEffectCache / DXTextureCache RTTI (TD 0x1800D7220 / 0x1800D7190): the
//     engine keeps shared_ptr-style caches keyed by file. The original used
//     boost::shared_ptr; this port uses std::shared_ptr (documented divergence).
#pragma once

#include <memory>
#include <string>

#include <d3d9.h>
#include <d3dx9.h>

namespace mme {

struct SasEffect;   // parsed SAS model (sas_interpreter.h; Phase 3)

// One cached effect file (the DXEffectCache entry).
struct LoadedEffect {
    ID3DXEffect*  effect = nullptr;   // created by D3DXCreateEffectFromFileW
    std::string   path;               // narrow (CP 0) path as requested
    std::string   errorText;          // last load error (logged by the apply path)
    unsigned long fileStamp = 0;      // FUN_18000B7F0 validity token (0 = invalid)
    SasEffect*    sas = nullptr;      // [FUN_18000c470] parsed SAS model (null when absent/invalid)
};

// [0x180093660] FUN_180093660: DXErr9 HRESULT description lookup (compact
// subset of the original's static tables; unknown codes return nullptr).
// Shared with the post-effect error reporter (pass_planner).
const char* MmeDxErrDescription(unsigned long hr);

// [0x18000a8e0] FUN_18000a8e0: tear the engine down, (re)create the effect pool
// and the 16x16 offscreen target, latch the mip-filter flag. Called by
// Initialize with the GetSamplerState(1,7) query result.
void MmeEngineInit(IDirect3DDevice9* device, bool mipFilterAvailable);

// [0x18001eeb0] FUN_18001eeb0: release every cached effect/texture and drop the
// engine containers (the pool/surface COM releases stay at the call sites,
// matching the original's Cleanup/Initialize order).
void MmeEngineTerm();

// [0x18005e640] FUN_18005e640: device-dependent resource release (the
// OnLostDevice half): ID3DXEffect::OnLostDevice for every cached effect and
// texture-cache OnLostDevice.
void MmeEngineOnLostDevice();

// [0x18002de40] FUN_18002de40: device-dependent resource reacquire (the
// OnResetDevice half): ID3DXEffect::OnResetDevice for every cached effect.
// Returns S_OK when every effect reacquired.
HRESULT MmeEngineOnResetDevice(IDirect3DDevice9* device);

// [0x18000b7f0] FUN_18000b7f0: file validity token (0 when the file cannot be
// opened; a nonzero size-derived stamp otherwise; used by the hot-reload check
// and as the "does the file exist" probe).
unsigned long MmeEngineQueryFileStamp(const std::string& pathAnsi);

// [0x18000bc90 / 0x18000b880] Load (or fetch from cache) the effect file
// `pathAnsi`. On a fresh load logs "Loading effect file: <path>", compiles with
// the _INDEX/PSIZE15/MME_MIPMAP defines through the shared effect pool, and
// records compile errors into the entry. Returns the cache entry; on failure
// the entry's `effect` stays null and `errorText` carries the reason.
std::shared_ptr<LoadedEffect> MmeEngineLoadEffectFile(IDirect3DDevice9* device,
                                                      const std::string& pathAnsi);

// [0x18000b210] FUN_18000b210: drop the cache entry for `pathAnsi`, logging
// "Unload effect file: <path>\n\n" first. No-op when the file is not cached.
void MmeEngineUnloadEffectFile(const std::string& pathAnsi);

// Texture-cache probe (the DXTextureCache half). Returns the cached texture or
// nullptr when the file is not cached; loading lives with the SAS resource
// machinery (Phase 3) - Phase 2 registers/unregisters cache entries only.
IDirect3DBaseTexture9* MmeEngineFindCachedTexture(const std::string& pathAnsi);
void MmeEngineCacheTexture(const std::string& pathAnsi, IDirect3DBaseTexture9* texture);
void MmeEngineUncacheTexture(const std::string& pathAnsi);

// [PHASE3 wiring] visit the parsed SAS model of every cached effect that has
// one (post-effect chains, pass_planner). Return false from `visit` to stop
// early. Returns false when the iteration was stopped, true otherwise.
bool MmeEngineForEachSas(bool (*visit)(void* user, SasEffect* sas), void* user);

} // namespace mme
