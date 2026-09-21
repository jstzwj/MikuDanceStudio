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
//     11296-11317). On failure exactly one segment is appended to the error
//     text: the compiler text when the error buffer is non-empty, otherwise
//     "DirectX Error: <desc> [%08X]\n" (0x18000c1ab-0x18000c376). On success
//     the error buffer is released unread (0x18000c3d3).
//   - FUN_18000B880: apply entry. First use loads the file; load failure logs
//     errorText + "\n" (0x18000ba10), unloads (0x18000ba43), then message-boxes
//     "Failed to load effect file:<path>\n\n" (English 0x1800b3b18) or the
//     localized equivalent (Shift-JIS bytes at 0x1800b3ae8 - ExpGetEnglishMode
//     selects, fallback byte_1800D99DD=0 is Japanese) via MessageBoxA(main,
//     ..., MB_ICONERROR) dedup-gated by DAT_1800d99d8. A fresh successful
//     load/reload writes "done.\n\n" (0x1800b3ae0) at 0x18000bc36.
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
    // Instances retain their compiled source; their parameters, scripts and
    // non-shared render targets belong to this instance alone.
    std::shared_ptr<LoadedEffect> source;
    bool assigned = false;  // first assignment uses the freshly compiled instance
    ID3DXEffect*  effect = nullptr;   // created by D3DXCreateEffectFromFileW
    std::string   path;               // narrow (CP 0) path as requested
    std::string   errorText;          // last load error (logged by the apply path)
    unsigned long fileStamp = 0;      // FUN_18000B7F0 validity token (0 = invalid)
    SasEffect*    sas = nullptr;      // [FUN_18000c470] parsed SAS model (null when absent/invalid)
    bool          doneLogged = false; // [0x18000bc10] sub_18000B880's "done.\n\n" latch:
                                      // written once per actual load/reload of this
                                      // file (v11 && *a1), not on cache hits (the
                                      // stamp-unchanged path keeps v11 clear).

    // [0x18000b210] FUN_18000B210 - 条目"卸载"语义的完整承载。原版在缓存条目
    // 的最后一个持有者（绑定对象 +0x08/+0x10 的 boost::shared_ptr 控制块，
    // 0x18000b5e9-0x18000b611 的 InterlockedDecrement 链）死亡时执行：
    //   1. [0x18000b24b-0x18000b316] effect(+0x00) 非空 → 先记日志
    //      "Unload effect file: <path>\n\n"（在一切资源释放之前；失败条目
    //      effect==null 静默卸载）；
    //   2. [0x18000b31b-0x18000b5de] 逐语义资源释放（0x26/0x27/0x28/0x2C/
    //      0x2D/0x2E/0x33：资源对象、offscreen 深度、动画纹理 vtable 释放，
    //      容器逐节点 erase）——移植等价物 = SasUnload(sas)；
    //   3. [0x18000b614-0x18000b623] ID3DXEffect::Release()（IUnknown
    //      vtable+0x10）并置空。
    // 移植把这套语义放进最后一个 shared_ptr 引用死亡时的析构函数，引用
    // 计数时序与原版控制块 1:1（cache 与 MaterialBinding 各持一份引用）。
    ~LoadedEffect();
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
std::shared_ptr<LoadedEffect> MmeEngineCreateEffectInstance(
    IDirect3DDevice9* device, const std::shared_ptr<LoadedEffect>& source);

// [0x18000b210] FUN_18000b210: drop the cache entry for `pathAnsi`, logging
// "Unload effect file: <path>\n\n" first. No-op when the file is not cached.
void MmeEngineUnloadEffectFile(const std::string& pathAnsi);

// [0x18000a990] sub_18000A990（40004 Reload All = FUN_18002DEF0 的缓存侧）：
// 清空效果缓存与纹理缓存两棵树（原版表头 0x1800D9C68 / 0x1800D9C88，逐节点
// 销毁走 sub_1800203F0 / sub_180020580）。只丢弃缓存侧引用——仍被
// MaterialBinding::owner 持有的条目活到该绑定重解析切换引用，最后一个引用
// 死亡时经 ~LoadedEffect（FUN_18000B210 语义：Unload 日志 / SasUnload /
// effect->Release）；无引用的条目当场死亡。缓存键差异说明：原版 DXEffectCache
// 按 (path, stamp) 键控（sub_18000AB40 的查找键含 stamp，stamp 变化即失配
// 重编译，旧条目滞留缓存直到本清理——原版的累积泄漏），移植按 path 键控、
// 由卸载方显式 erase 补偿，可观察行为（变化即重载、Reload All 全清）一致。
void MmeEngineClearCaches();

// Texture-cache probe (the DXTextureCache half). Returns the cached texture or
// nullptr when the file is not cached; loading lives with the SAS resource
// machinery (Phase 3) - Phase 2 registers/unregisters cache entries only.
IDirect3DBaseTexture9* MmeEngineFindCachedTexture(const std::string& pathAnsi);
void MmeEngineCacheTexture(const std::string& pathAnsi, IDirect3DBaseTexture9* texture);
void MmeEngineUncacheTexture(const std::string& pathAnsi);

// Visit the parsed SAS model of each live source/assignment that has
// one (post-effect chains, pass_planner). Return false from `visit` to stop
// early. Returns false when the iteration was stopped, true otherwise.
bool MmeEngineForEachSas(bool (*visit)(void* user, SasEffect* sas), void* user);

} // namespace mme
