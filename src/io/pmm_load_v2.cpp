// ===========================================================================
// VA 0x00450000 - LoadSceneV2  (; original: sub_450000,
//                            0x9796 bytes - the v2 (.pmm "0002") loader body)
// ===========================================================================
// Called by the load shell sub_458F80 (0x459106) as sub_450000(app, fd).
// Full body:
//   0x450040  dispose all 100 model slots + scene-state reset run + AVI
//             teardown + header-field reads (phase 1)
//   0x450334  UI clear run: 7 edits 0x1DE..0x1E4 (select-all + replace
//             with ""), uncheck 0x1B8/0x1B9/0x1DD, CheckMenuItem 0xFE,
//             EnableMenuItem 0x120/0x121 grayed
//   0x45044E  9ED9A=0; read slot byte -> app+0x910; read model count;
//             Block = new(count * 0x238) + memset - the per-model record
//             array (0x238-stride: +0 slot, +4 skip flag, +5 name1[0x100],
//             +0x105 name2[0x100], +0x205 bone count, +0x206 bone-match,
//             +0x208 disp count, +0x20C disp translation array,
//             +0x210 disp-match, +0x214 morph count, +0x218 morph
//             translation array, +0x21C morph-match, +0x220 IK count,
//             +0x224 IK remap array, +0x228 rigid count, +0x22C rigid
//             remap array, +0x230 order byte)
//   0x4504BA  per-model loop: read slot; allocate/init ModelRecord,
//             IdentityCtor ctor + memset + ModelInitDefaults(0x4A8DC0);
//             names -> record; path -> ResolveAnsiUserFile(0x407BA0);
//             ModelLoadPMD(0x4BF3E0, a6=0 no-box); failure -> dialog
//             0x32E(JP)/0x32F(EN): 2/4 abort, 1 locate file via
//             GetOpenFileNameW and retry, 3 skip (consume record),
//             other proceed; success: read bone count, compare with the
//             PMD (0x26D4), build 264-byte disp/morph name translation
//             arrays (+256 mapped index) and 8-byte IK/rigid remap
//             arrays, structure-difference dialog ring 0x330/0x331
//             (2/5 abort, 4 skip, other re-load), then the full state
//             load into the model (order byte, disp keys with remap,
//             morph keys, IK bools, rigid pairs, physics keys, bone bool
//             table, current-pose dump per disp frame / morph / IK /
//             rigid)
//   0x45413B  free + NULL the four tracks (0x374/0x378/0x37C/0x380) and
//             the 255 accessory objects/tracks (0x9DD70 via 0x4C4700 +
//             0x384)
//   0x454230  UI combo reset run: 436/474/449/450/433 CB_RESETCONTENT +
//             initial entries (EN/JP), model re-population by order byte
//             (0x2D9C), register list per edit mode, CB_SETCURSEL,
//             0x49C850 (0x454C72)
//   0x454C9A  re-allocate: camera 840000 / light 400000 / selection
//             240000 / self-shadow 360000 + memset; shadow mode byte
//             from the D3D wrapper (0x1D544); default +0x10 float
//             (flt_52A1D8); A0D30=1; A0188=0; gravity defaults on the
//             shadow track (+28=10, +12=9.8f, +20=-1.0f); camera track
//             +76 dword = -1 sweep; 255 accessory tracks 600000 each
//             (+12=1, +16=-1, +52=+56=1.0f, slot null)
//   0x454E07  camera track read (84-byte records, record 0 + sparse
//             frame-prefixed keys) + camera misc W4s + frame UI
//   0x455286  light track read (40-byte records) + light misc W4s +
//             rgb/direction slider UI
//   0x45593E  9E170 byte + 9DA48 dword; CB_RESETCONTENT 0x1D7/0x1DB;
//             accessory-shadow name list; accessory block (0x4B0
//             objects, InitAccessoryRecord, LoadAccessoryObject 0x4C5F40 with
//             locate-and-retry dialog, name 0x64 + path 0x100 + 60-byte
//             track records with the transparency quirk byte decoded
//             as (b&1) flag + (100 - b/2)/100 scale)
//   0x456617  config block: 0x980/0x97C/0x9E16C dwords, frame edit 417,
//             refresh chain, radio 0x914 switch, checkbox bytes, edit
//             409/410, ClearTimelineAndCurveDCs, wave path (0xD0), AVI path block
//             (0x91C read-before-test), picture block (9E428/9E434..),
//             0x31E/0x31D/0x918 menu checks, 0x9EB84 switch,
//             physics-menu defaults, 0xA0B20 + shadow distance copies
//   0x457713  physics reads + selection track (36-byte records) +
//             self-shadow track (24-byte records) + A0D30/A0188 +
//             model color sweep (SetModelColor) + A0194 + A0430/A0434
//             register re-select (RefillBoneRegisterCombo) + 16 config dwords
//             (0xA0438..0xA0474) + 0xF7/535 + A0478 + 0x11D + optional
//             per-model 0x4CCF0 dword block + _close (0x458112)
//   0x458120  success tail: shadow-mode gate (RefreshSelfShadowPanel), menu 0x117,
//             light direction to the physics scene (vtable[13]),
//             window title, record post-processing (disp remap for the
//             camera/accessory tracks or reference removal for skipped
//             models), key-chain integrity boxes, combo 434
//             population, 0xA0D38 child refresh, frame edit 554,
//             edit-mode repaint, record array free, InvalidateRect,
//             RelayoutSidebarControls, 9EDB5=1, A442C=1, PostViewRefresh
//
// Split: the body is divided into static LoadSceneV2_* segment functions
// along this VA map (each carries its address-range banner); the shared
// frame - window handles, slot arrays, scratch buffers, track pointers -
// lives in PmmV2LoadContext, mirroring sub_450000's single stack frame.
//
// Deviations (docs/ARCHITECTURE.md section 8):
//   * the %s-no-vararg swprintf_s quirk mirrors the save-side deviation
//     (buf passed as its own argument, idempotent).
//   * DialogFunc (0x40FF80) is the real migration-dialog proc (stubs.cpp);
//     the dialogs only appear on the missing-model / structure-difference
//     paths.
// ===========================================================================
#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <Windows.h>
#include <commdlg.h>
#include <commctrl.h>
#include <btBulletDynamicsCommon.h>
#include <cmath>

#include <cstdint>
#include <cstdio>
#include <cstring>
#include <cstdlib>
#include <io.h>

#include "mikudancestudio/accessory_layout.hpp"
#include "mikudancestudio/d3dx_dyn.hpp"
#include "mikudancestudio/global_key_layout.hpp"
#include "mikudancestudio/mme_bridge.hpp"
#include "mikudancestudio/mmd_app.hpp"
#include "mikudancestudio/ported_funcs.hpp"
#include "mikudancestudio/scene_ownership.hpp"
#include "mikudancestudio/model.hpp"
#include "mikudancestudio/panel_controls.hpp"

// Shared PMM record readers, the Rd helper and the byte-identical halves of
// the loader string tables live here (see the header comment for the
// v1/v2 parameterization points).
#include "pmm_io_common.hpp"

#pragma comment(lib, "avifil32.lib")

namespace mikudancestudio {

using namespace pmm_io;

namespace {

// flt_52A1D8 (0x3C3851EC = 0.01125f) - the float the original seeds into
// A0D2C/physicsInterval (see the reset run below).  C++17 has no
// std::bit_cast; MSVC's __builtin_bit_cast is accepted in constant
// expressions, so the static_assert pins the literal to the original
// .rdata bit pattern.
constexpr float kPhysicsIntervalDefault = 0.01125f;
static_assert(
    __builtin_bit_cast(std::uint32_t, kPhysicsIntervalDefault) == 0x3C3851ECu,
    "flt_52A1D8 bit-exact");

struct NameMapping {
    char name[256]{};
    std::int32_t mappedIndex = -1;
    std::int32_t frameOffset = 0;
};

struct IndexMapping {
    std::int32_t mappedIndex = -1;
    std::int32_t sourceIndex = -1;
};

struct SelectorStateSnapshot {
    std::uint32_t windowStart = 0;
    std::uint32_t windowEnd = 0;
    std::int32_t linkedModel = -1;
    std::int32_t linkedBone = 0;
};

static_assert(sizeof(NameMapping) == 264, "PMM name mapping size");
static_assert(sizeof(IndexMapping) == 8, "PMM index mapping size");
static_assert(sizeof(SelectorStateSnapshot) == 16,
              "PMM selector state size");

// Per-model reconciliation workspace.  The original source keeps ordinary
// pointers here; treating their Win32 offsets as a permanent byte ABI made
// the x64 build overwrite the counters following each pointer.
struct PmmModelLoadWorkspace {
    std::int32_t modelSlot = 0;
    std::uint8_t skipped = 0;
    char modelName[0x100]{};
    char sourceName[0x100]{};
    std::uint8_t boneGroupCount = 0;
    std::uint8_t boneGroupsMatch = 0;
    std::int32_t displayCount = 0;
    std::uint8_t displaysMatch = 0;
    std::int32_t morphCount = 0;
    std::uint8_t morphsMatch = 0;
    std::int32_t ikCount = 0;
    std::int32_t rigidBodyCount = 0;
    std::int32_t displayOrder = 0;
    std::int32_t previousDisplayOrder = 0;
    NameMapping* boneNameMap = nullptr;
    NameMapping* morphNameMap = nullptr;
    IndexMapping* ikIndexMap = nullptr;
    IndexMapping* rigidIndexMap = nullptr;
};

// AVIFIL32 imports, the Rd helper, the record readers (ReadPmmBoneKey /
// ReadPmmMorphKey / ReadPmmCameraKey / ReadPmmLightKey / ReadPmmSelfShadowKey
// / ReadPmmGravityKey / ReadPmmAccessoryKey) and the skip-path Discard*
// family live in pmm_io_common.hpp, shared with the v1 loader body.

// Porting-era traces under MIKUDANCESTUDIO_PMM_TRACE_DIR (CMake option
// MIKUDANCESTUDIO_DIAG, default OFF); the OFF stubs keep the call sites
// valid and inline away to nothing.
#ifdef MIKUDANCESTUDIO_DIAG
void LogPmmModelAttempt(const char* rawPath, const wchar_t* resolvedPath,
                        D3DRenderer* wrap, void* physics,
                        bool loaded) {
    const char* dir = std::getenv("MIKUDANCESTUDIO_PMM_TRACE_DIR");
    if (dir == nullptr || dir[0] == '\0')
        return;
    char path[MAX_PATH];
    sprintf_s(path, "%s\\pmm_model_load.log", dir);
    FILE* stream = nullptr;
    if (fopen_s(&stream, path, "ab") != 0 || stream == nullptr)
        return;
    void* device = wrap != nullptr ? wrap->device : nullptr;
    const DWORD attrs = resolvedPath != nullptr && resolvedPath[0] != L'\0'
        ? GetFileAttributesW(resolvedPath) : INVALID_FILE_ATTRIBUTES;
    fprintf(stream,
            "raw=%s\r\nresolved=%ls\r\nattrs=0x%08lX "
            "wrap=%p device=%p physics=%p loaded=%d\r\n",
            rawPath != nullptr ? rawPath : "", resolvedPath != nullptr
                ? resolvedPath : L"", attrs, wrap, device, physics,
            loaded ? 1 : 0);
    fclose(stream);
}

void LogPmmModelStage(int fd, const char* stage, std::int32_t fileCount,
                      std::int32_t modelCount, const void* modelData) {
    const char* dir = std::getenv("MIKUDANCESTUDIO_PMM_TRACE_DIR");
    if (dir == nullptr || dir[0] == '\0')
        return;
    char path[MAX_PATH];
    sprintf_s(path, "%s\\pmm_model_load.log", dir);
    FILE* stream = nullptr;
    if (fopen_s(&stream, path, "ab") != 0 || stream == nullptr)
        return;
    fprintf(stream, "stage=%s file_pos=%ld file_count=%ld model_count=%ld "
                    "model_data=%p\r\n",
            stage, fd >= 0 ? _tell(fd) : -1L, static_cast<long>(fileCount),
            static_cast<long>(modelCount), modelData);
    fclose(stream);
}

void LogPmmHeapState(int fd, const char* stage) {
    const char* dir = std::getenv("MIKUDANCESTUDIO_PMM_TRACE_DIR");
    if (dir == nullptr || dir[0] == '\0')
        return;
    char path[MAX_PATH];
    sprintf_s(path, "%s\\pmm_model_load.log", dir);
    FILE* stream = nullptr;
    if (fopen_s(&stream, path, "ab") != 0 || stream == nullptr)
        return;
    const BOOL intact = HeapValidate(GetProcessHeap(), 0, nullptr);
    fprintf(stream, "stage=heap-%s intact=%d file_pos=%ld\r\n", stage,
            intact != FALSE ? 1 : 0, _tell(fd));
    fclose(stream);
}
#else
inline void LogPmmModelAttempt(const char*, const wchar_t*, D3DRenderer*,
                               void*, bool) {}
inline void LogPmmModelStage(int, const char*, std::int32_t, std::int32_t,
                             const void*) {}
inline void LogPmmHeapState(int, const char*) {}
#endif

// ---- UI strings: see pmm_io_common.hpp (the byte-identical wide .rdata
// mirrors, kNon, kJpCannotOpenModel, kJpOpenCaption and kJpChainCapPhys
// moved there). --------------------------------------------------------------
//
// ---- SJIS box texts (byte-exact; VA recorded) ----------------------------
// Wave5-C IDA verdict (x86 original): every text below exists ONCE in
// .rdata - kJpChainCapDisp 0x52D770, kJpChainFmtPhys 0x52D788,
// kJpChainFmtDisp 0x52D848, kJpCannotOpenAcc 0x52DAE0 - and BOTH loader
// bodies reference those single copies (this body at 0x458B54 / 0x458B32 /
// 0x458A22 / 0x455B8C, the v1 body at 0x45E4FC / 0x45E4DA / 0x45E37A /
// 0x45B9B7).  The former v2 variants (以下 for 以降, ブ for プ,
// \x91\xB7=替 for \x91\xD6=換, and a dropped し直 in the display format)
// were transcription errors - those byte sequences exist nowhere in the
// original - and are now byte-identical to the v1 copies.  They stay
// duplicated per-body only while the loader split is in flight; merge
// into pmm_io_common.hpp afterwards.
const char kJpQuoted[] = "\"%s\"";                               // 0x52DDB8
// x64 0x7FF7CB550948 - the JP "This model structure is different from
// pmm TOO" re-test text (r==3 re-load ring only; the first-pass gate
// prints kJpQuoted, see LoadSceneV2_ModelBlock).
const char kJpStructFmt[] =                                      // 0x52DBB8
    "\x82\xB1\x82\xCC\x83\x82\x83\x66\x83\x8B\x82\xCC\x8D\x5C\x91\xA2"
    "\x82\xE0\x70\x6D\x6D\x95\xDB\x91\xB6\x8E\x9E\x82\xCC\x22\x25\x73"
    "\x22\x82\xCC\x82\xE0\x82\xCC\x82\xC6\x88\xD9\x82\xC8\x82\xE8"
    "\x82\xDC\x82\xB7";
const char kJpCannotOpenAcc[] =                                  // 0x52DAE0
    "\x83\x41\x83\x4E\x83\x5A\x83\x54\x83\x8A\x83\x74\x83\x40\x83\x43"
    "\x83\x8B\x28\x25\x73\x29\x82\xAA\x8C\xA9\x82\xC2\x82\xA9\x82\xE8"
    "\x82\xDC\x82\xB9\x82\xF1\x0A\x0A\x25\x73\x82\xCC\x8F\xEA\x8F\x8A"
    "\x82\xF0\x8E\x77\x92\xE8\x82\xB5\x82\xC4\x89\xBA\x82\xB3\x82\xA2"
    "\x28\x91\xE3\x91\xD6\x89\xC2\x29";
const char kJpChainCapDisp[] =                                   // 0x52D770
    "\x83\x5A\x81\x5B\x83\x75\x83\x66\x81\x5B\x83\x5E\x82\xCC\x88\xD9"
    "\x8F\xED";
const char kJpChainFmtPhys[] =                                   // 0x52D788
    "\x22\x25\x73\x22\x83\x82\x83\x66\x83\x8B\x81\x41\x83\x7B\x81\x5B"
    "\x83\x93\x22\x25\x73\x22\x82\xCC\x83\x74\x83\x8C\x81\x5B\x83\x80"
    "\x83\x66\x81\x5B\x83\x5E\x82\xC9\x88\xD9\x8F\xED\x82\xAA\x8C\xA9"
    "\x82\xC2\x82\xA9\x82\xE8\x82\xDC\x82\xB5\x82\xBD\x0A\x0A\x88\xD9"
    "\x8F\xED\x82\xC8\x83\x74\x83\x8C\x81\x5B\x83\x80\x28\x83\x74\x83"
    "\x8C\x81\x5B\x83\x80\x94\xD4\x8D\x86\x25\x64\x88\xC8\x8D\x7E\x29"
    "\x82\xF0\x8D\xED\x8F\x9C\x82\xB5\x82\xDC\x82\xB7\x0A\x25\x64\x83"
    "\x74\x83\x8C\x81\x5B\x83\x80\x88\xC8\x8D\x7E\x82\xCC\x25\x73\x82"
    "\xCC\x83\x82\x81\x5B\x83\x56\x83\x87\x83\x93\x82\xF0\x8D\xC4\x93"
    "\x78\x90\xDD\x92\xE8\x82\xB5\x92\xBC\x82\xB5\x82\xC4\x89\xBA\x82"
    "\xB3\x82\xA2";
const char kJpChainFmtDisp[] =                                   // 0x52D848
    "\x22\x25\x73\x22\x83\x82\x83\x66\x83\x8B\x81\x41\x22\x95\x5C\x8E"
    "\xA6\xA5\x49\x4B\xA5\x8A\x4F\x90\x65\x22\x82\xCC\x83\x74\x83\x8C"
    "\x81\x5B\x83\x80\x83\x66\x81\x5B\x83\x5E\x82\xC9\x88\xD9\x8F\xED"
    "\x82\xAA\x8C\xA9\x82\xC2\x82\xA9\x82\xE8\x82\xDC\x82\xB5\x82\xBD"
    "\x0A\x0A\x88\xD9\x8F\xED\x82\xC8\x22\x95\x5C\x8E\xA6\xA5\x49\x4B"
    "\xA5\x8A\x4F\x90\x65\x22\x83\x74\x83\x8C\x81\x5B\x83\x80\x81\x69"
    "\x83\x74\x83\x8C\x81\x5B\x83\x80\x94\xD4\x8D\x86\x25\x64\x88\xC8"
    "\x8D\x7E\x81\x6A\x82\xF0\x8D\xED\x8F\x9C\x82\xB5\x82\xDC\x82\xB7"
    "\x0A\x90\x5C\x82\xB5\x96\xF3\x82\xA0\x82\xE8\x82\xDC\x82\xB9\x82"
    "\xF1\x82\xAA\x81\x41\x25\x73\x83\x82\x83\x66\x83\x8B\x82\xCC\x25"
    "\x64\x83\x74\x83\x8C\x81\x5B\x83\x80\x88\xC8\x8D\x7E\x82\xCC\x22"
    "\x95\x5C\x8E\xA6\xA5\x49\x4B\x22\x82\xF0\x8D\xC4\x93\x78\x90\xDD"
    "\x92\xE8\x82\xB5\x92\xBC\x82\xB5\x82\xC4\x89\xBA\x82\xB3\x82\xA2";

constexpr std::size_t kGlobalKeyCapacity = mdl::kTimelineKeyCapacity;

}  // namespace

namespace {

// Per-load frame shared by the LoadSceneV2 segment functions: the
// original keeps every one of these in sub_450000's single stack frame;
// the split passes them as one context (the PmmModelLoadWorkspace
// pattern extended).
struct PmmV2LoadContext {
    MMDApp* s;
    HWND main;
    HINSTANCE hInst;
    PathResolutionWorkspace* paths;
    D3DRenderer* wrap;
    unsigned char** slots;
    mdl::AccessoryRecord** accs;
    mdl::AccessoryKey** accTracks;
    // shared scratch buffers (frame 0x218 / 0x318 / 0x380 / 0xdb8 in the
    // original stack frame)
    char text[0x100];          // name/EM_REPLACESEL scratch
    char mbPath[0x100];        // accessory/background path scratch
    wchar_t widePath[0x100];   // model path
    wchar_t wideTmp[0x100];    // accessory path
    wchar_t ofnFile[0x100];    // GetOpenFileName buffer
    wchar_t ofnTitle[0x100];
    wchar_t wndText[0x100];    // window-title sprintf target
    char lbText[0x100];        // CB_GETLBTEXT buffer
    // the four re-allocated global tracks (0x454C9A segment)
    mdl::CameraKey* cameraKeys = nullptr;
    mdl::LightKey* lightKeys = nullptr;
    mdl::SelfShadowKey* selfShadowKeys = nullptr;
    mdl::GravityKey* gravityKeys = nullptr;
    std::int32_t maxFrame = 0;  // 0x458098 read, success-tail frame edit

    explicit PmmV2LoadContext(MMDApp* app)
        : s(app),
          main(reinterpret_cast<HWND>(app->Hwnd())),
          hInst(static_cast<HINSTANCE>(app->HInstance())),
          paths(&app->PathWorkspace()),
          wrap(app->Renderer()),
          slots(app->ModelSlots()),
          accs(app->AccessorySlots()),
          accTracks(app->AccessoryKeyTracks()) {}
};

// common abort: close + free record arrays + free Block + 44E540 +
// 443300 (0x45426C / 0x45446A / 0x4542F4 -> 0x454305); the split-out
// twin of the abortLoad lambda the inline body carried.
static void FreeRecordArrays(PmmModelLoadWorkspace* workspaces,
                             unsigned char modelCount) {
    for (unsigned char i = 0; i < modelCount; ++i) {
        PmmModelLoadWorkspace& workspace = workspaces[i];
        free(workspace.boneNameMap);
        free(workspace.morphNameMap);
        free(workspace.ikIndexMap);
        free(workspace.rigidIndexMap);
        workspace.boneNameMap = nullptr;
        workspace.morphNameMap = nullptr;
        workspace.ikIndexMap = nullptr;
        workspace.rigidIndexMap = nullptr;
    }
}

static void AbortV2Load(MMDApp* s, int fd,
                        PmmModelLoadWorkspace* workspaces,
                        unsigned char modelCount) {
    _close(fd);
    FreeRecordArrays(workspaces, modelCount);
    delete[] workspaces;
    ResetAppState(s);                                       // 0x44E540
    HandleWindowSize(s);                                    // 0x443300
}

// ---- 0x450040..0x45044C: dispose + scene-state reset + menu/checkbox
// reset + AVI teardown + header-field reads + UI clear run -------------
static void LoadSceneV2_DisposeAndHeader(PmmV2LoadContext& ctx, int fd) {
    auto* const s = ctx.s;
    HWND const main = ctx.main;
    char* const text = ctx.text;
    ReleaseSceneModels(*s);                                      // 0x450040

    // ---- scene-state reset run (0x450093..0x450121) ----------------------
    s->state.accessoryRenderSplitOrder = 0;
    s->SelectGlobalTimelineTrack(GlobalTimelineTrack::Camera);
    s->state.mainModelComboSelection = 0;
    s->state.cameraParentModel = -1;
    s->state.cameraParentBone = 0;
    s->state.cameraAttachmentBasis[12] = 0.0f;                   // 0xA0468
    s->state.cameraAttachmentBasis[11] = 0.0f;                   // 0xA0464
    s->state.cameraAttachmentBasis[9] = 0.0f;                    // 0xA045C
    s->state.cameraAttachmentBasis[8] = 0.0f;                    // 0xA0458
    s->state.cameraAttachmentBasis[7] = 0.0f;                    // 0xA0454
    s->state.cameraAttachmentBasis[6] = 0.0f;                    // 0xA0450
    s->state.cameraAttachmentBasis[4] = 0.0f;                    // 0xA0448
    s->state.cameraAttachmentBasis[3] = 0.0f;                    // 0xA0444
    s->state.cameraAttachmentBasis[2] = 0.0f;                    // 0xA0440
    s->state.cameraAttachmentBasis[1] = 0.0f;                    // 0xA043C
    s->state.cameraAttachmentBasis[15] = 1.0f;                   // 0xA0474
    s->state.cameraAttachmentBasis[10] = 1.0f;                    // 0xA0460
    s->state.cameraAttachmentBasis[5] = 1.0f;                // 0xA044C
    s->state.cameraAttachmentBasis[0] = 1.0f;                   // 0xA0438
    s->state.followCameraEnabled = 0;               // 0x9ED98

    // ---- menu/checkbox reset (0x4500C4..0x45015B) ------------------------
    CheckMenuItem(GetMenu(main), 0xF7, 0);
    SendMessageA(GetDlgItem(main, panel::kFollowCameraCheckbox), BM_SETCHECK, 0, 0);

    s->state.selfShadowMode = 0;                // 0x450161
    s->state.physicsInterval = kPhysicsIntervalDefault;     // flt_52A1D8

    // ---- AVI teardown trio (0x45016D..0x4501B5) --------------------------
    if (s->AviFrameReader() != nullptr) {
        AVIStreamGetFrameClose(s->AviFrameReader());
        s->AviFrameReader() = nullptr;
    }
    if (s->AviStream() != nullptr) {
        AVIStreamRelease(s->AviStream());
        s->AviStream() = nullptr;
    }
    if (s->AviFile() != nullptr) {
        AVIFileRelease(s->AviFile());
        s->AviFile() = nullptr;
    }

    // swprintf_s(9E1EC, 0x100, L"%s") with no vararg in the original.
    {
        wchar_t* const aviPath = s->AviBackgroundPath();
        swprintf_s(aviPath, 0x100, L"%s", aviPath);
    }

    if (s->AviBackgroundTexture() != nullptr) {                  // 0x4501F0
        s->AviBackgroundTexture()->Release();
        s->AviBackgroundTexture() = nullptr;
    }

    // ---- header-field reads (0x450208..0x450331) -------------------------
    Rd(fd, &s->state.renderW, 4);      // A08D4
    Rd(fd, &s->state.renderH, 4);      // A08D8
    {
        std::int32_t editFlag = 0;
        Rd(fd, &editFlag, 4);
        if (s->state.floatingWindow == 0)
            s->state.sidebarWidth = editFlag;   // A06C8
        else
            s->state.separateWindowSidebarWidth = editFlag;   // A0D3C
    }
    Rd(fd, &s->state.cameraFov, 4);        // fov
    for (int f = 0; f < 7; ++f) {
        unsigned char b = 0;
        Rd(fd, &b, 1);
        s->state.optflag[f] = (b == 1) ? 1 : 0;      // 0x2F8..2FE
    }
strcpy_s(text, 0x100, "");                                  // 0x450331

    // ---- UI clear run (0x450334..0x45044C) -------------------------------
    for (int id = 478; id <= 484; ++id) {                       // 0x450340
        HWND item = GetDlgItem(main, id);
        const int len = GetWindowTextLengthA(item);
        SendMessageA(GetDlgItem(main, id), EM_SETSEL, 0, len);
        SendMessageA(GetDlgItem(main, id), EM_REPLACESEL, 0,
                     reinterpret_cast<LPARAM>(text));
    }
    SendMessageA(GetDlgItem(main, panel::kShadowCheckbox), BM_SETCHECK, 0, 0);     // 0x4503BF
    SendMessageA(GetDlgItem(main, panel::kAddBlendCheckbox), BM_SETCHECK, 0, 0);     // 0x4503DD
    SendMessageA(GetDlgItem(main, panel::kAccessoryAddBlendCheckbox), BM_SETCHECK, 0, 0);     // 0x4503FB
    CheckMenuItem(GetMenu(main), 0xFE, 0);                      // 0x450418
    EnableMenuItem(GetMenu(main), 0x120, 1);                    // 0x450435
    EnableMenuItem(GetMenu(main), 0x121, 1);                    // 0x45044C
}


// ---- 0x450947..0x4511E7: skip-path stream consumption (the r==4
// variant starts at 0x452363 with the counts/names already consumed) ---
static void LoadSceneV2_SkipRecordConsumption(
    int fd, PmmModelLoadWorkspace& workspace, char* text, bool skipFrom4) {
                // ---- skip consumption (0x450947..0x4511E7; the r==4
                // variant starts at 0x452363 with the counts/names and
                // the &workspace.displayOrder byte already consumed) ----------------------
                if (!skipFrom4) {
                Rd(fd, &workspace.displayCount, 4);                         // 0x450951
                for (std::int32_t i = 0; i < workspace.displayCount; ++i) {
                    unsigned char len = 0;
                    Rd(fd, &len, 1);
                    Rd(fd, text, len);
                }
                Rd(fd, &workspace.morphCount, 4);                         // 0x4509B1
                for (std::int32_t i = 0; i < workspace.morphCount; ++i) {
                    unsigned char len = 0;
                    Rd(fd, &len, 1);
                    Rd(fd, text, len);
                }
                Rd(fd, &workspace.ikCount, 4);                         // 0x450A11
                for (std::int32_t i = 0; i < workspace.ikCount; ++i) {
                    std::int32_t v = 0;
                    Rd(fd, &v, 4);
                }
                Rd(fd, &workspace.rigidBodyCount, 4);                         // 0x450A5D
                for (std::int32_t i = 0; i < workspace.rigidBodyCount; ++i) {
                    std::int32_t v = 0;
                    Rd(fd, &v, 4);
                }
                Rd(fd, &workspace.displayOrder, 1);                         // 0x450AA3
                } else {
                    // x64 0x7FF7CB49B2F3: the r==4 variant stores the byte
                    // at the comboSelIndex file position into the record
                    // (workspace +584) - it is not discarded; the
                    // success-tail sweep compares every surviving model's
                    // comboSelIndex against it.
                    Rd(fd, &workspace.displayOrder, 1);
                }
                {
                    unsigned char b = 0;
                    std::int32_t v = 0;
                    Rd(fd, &b, 1);                              // 0x450AB0
                    Rd(fd, &v, 4);                              // 0x450ABD
                    for (int i = 0; i < 4; ++i) Rd(fd, &v, 4);
                    Rd(fd, &b, 1);                              // 0x450AEF
                    for (int i = 0; i < workspace.boneGroupCount; ++i) Rd(fd, &b, 1);
                    Rd(fd, &v, 4);                              // 0x450B2F
                    Rd(fd, &v, 4);
                }
                for (std::int32_t i = 0; i < workspace.displayCount; ++i) {
                    DiscardPmmBoneKey(fd);
                }
                {
                    std::int32_t cnt = 0;
                    Rd(fd, &cnt, 4);                            // 0x450C6A
                    for (std::int32_t i = 0; i < cnt; ++i) {
                        std::int32_t discardedKeyIndex = 0;
                        Rd(fd, &discardedKeyIndex, 4);
                        DiscardPmmBoneKey(fd);
                    }
                }
                for (std::int32_t i = 0; i < workspace.morphCount; ++i) {
                    DiscardPmmMorphKey(fd);
                }
                {
                    std::int32_t cnt = 0;
                    Rd(fd, &cnt, 4);                            // 0x450E02
                    for (std::int32_t i = 0; i < cnt; ++i) {
                        std::int32_t discardedKeyIndex = 0;
                        Rd(fd, &discardedKeyIndex, 4);
                        DiscardPmmMorphKey(fd);
                    }
                }
                {
                    std::int32_t v = 0;
                    unsigned char b = 0;
                    Rd(fd, &v, 4); Rd(fd, &v, 4); Rd(fd, &v, 4);
                    Rd(fd, &b, 1);                              // 0x450EB1
                }
                for (std::int32_t i = 0; i < workspace.ikCount; ++i) {
                    unsigned char b = 0;
                    Rd(fd, &b, 1);                              // 0x450ED8
                }
                for (std::int32_t i = 0; i < workspace.rigidBodyCount; ++i) {
                    std::int32_t v = 0;
                    Rd(fd, &v, 4); Rd(fd, &v, 4);               // 0x450F18
                }
                {
                    std::int32_t cnt = 0;
                    DiscardPmmDisplayKey(fd, workspace.ikCount,
                                         workspace.rigidBodyCount);
                    Rd(fd, &cnt, 4);                            // 0x450F50
                    for (std::int32_t i = 0; i < cnt; ++i) {
                        std::int32_t discardedKeyIndex = 0;
                        Rd(fd, &discardedKeyIndex, 4);
                        DiscardPmmDisplayKey(fd, workspace.ikCount,
                                             workspace.rigidBodyCount);
                    }
                }
                for (std::int32_t i = 0; i < workspace.displayCount; ++i) {
                    std::int32_t v = 0;
                    unsigned char b = 0;
                    for (int k = 0; k < 7; ++k) Rd(fd, &v, 4);
                    Rd(fd, &b, 1); Rd(fd, &b, 1); Rd(fd, &b, 1);
                }
                for (std::int32_t i = 0; i < workspace.morphCount; ++i) {
                    std::int32_t v = 0;
                    Rd(fd, &v, 4);                              // 0x45110D
                }
                for (std::int32_t i = 0; i < workspace.ikCount; ++i) {
                    unsigned char b = 0;
                    Rd(fd, &b, 1);                              // 0x45113F
                }
                for (std::int32_t i = 0; i < workspace.rigidBodyCount; ++i) {
                    std::int32_t v = 0;
                    Rd(fd, &v, 4); Rd(fd, &v, 4);
                    Rd(fd, &v, 4); Rd(fd, &v, 4);               // 0x451168
                }
                {
                    unsigned char b = 0;
                    std::int32_t v = 0;
                    Rd(fd, &b, 1);                              // 0x4511AE
                    Rd(fd, &v, 4);                              // 0x4511BB
                    Rd(fd, &b, 1);                              // 0x4511C8
                    // x64 0x7FF7CB49A281 (LABEL_97 tail, shared by both
                    // skip variants): the last byte lands in the record's
                    // +588 slot (the comboSelIndex2 file position) and
                    // feeds the success-tail comboSelIndex2 decrement.
                    Rd(fd, &workspace.previousDisplayOrder, 1);
                }
}


// ---- 0x452A77..0x45410A: full state load into a (re)loaded model ----
static void LoadSceneV2_ModelStateLoad(PmmV2LoadContext& ctx, int fd,
                                     unsigned char* model,
                                     PmmModelLoadWorkspace& workspace,
                                     unsigned char modelIdx,
                                     unsigned char modelCount) {
    auto* const s = ctx.s;
    HWND const main = ctx.main;
                // ---- state load into the model (LABEL_275, 0x452A77..) ---
                LogPmmModelStage(fd, "state-load", 0, 0, model);
                mdl::ModelRecord* modelRecord = mdl::Mdl(model);
                NameMapping* const boneMappings =
                    workspace.boneNameMap;
                NameMapping* const morphMappings =
                    workspace.morphNameMap;
                IndexMapping* const ikMappings =
                    workspace.ikIndexMap;
                IndexMapping* const rigidMappings =
                    workspace.rigidIndexMap;
                Rd(fd, &modelRecord->comboSelIndex,
                   sizeof modelRecord->comboSelIndex);          // 0x452A77
                modelRecord->comboSelIndex2 = modelRecord->comboSelIndex;
                s->state.cameraParentBone =
                    modelRecord->comboSelIndex;
                {
                    unsigned char b = 0;
                    Rd(fd, &b, 1);                              // 0x452ACB
                    modelRecord->loadComplete = (b == 1) ? 1 : 0;
                }
                Rd(fd, &modelRecord->selectedBone,
                   sizeof modelRecord->selectedBone);           // 0x452B18
                if (modelRecord->selectedBone >= 0) {
                    const std::int32_t m = boneMappings[
                        modelRecord->selectedBone].mappedIndex;
                    modelRecord->selectedBone = m >= 0 ? m : -1;
                }
                LogPmmModelStage(fd, "selected-display",
                                 modelRecord->selectedBone,
                                 workspace.displayCount,
                                 workspace.boneNameMap);
                for (std::int32_t& selectedMorph :
                     mdl::Mdl(model)->selectedMorphs) {
                    std::int32_t v = 0;
                    Rd(fd, &v, 4);                              // 0x452B6F
                    if (workspace.morphsMatch != 0) selectedMorph = v;
                }
                {
                    unsigned char b = 0;
                    Rd(fd, &b, 1);                              // 0x452BAD
                    LogPmmModelStage(fd, "bone-flags", workspace.boneGroupCount,
                                     workspace.boneGroupsMatch,
                                     mdl::DisplayGroups(model));
                    if (workspace.boneGroupsMatch != 0) {
                        for (int i = 0; i < workspace.boneGroupCount; ++i) {
                            Rd(fd, &b, 1);                      // 0x452BD9
                            mdl::DisplayGroups(model)[i].flags =
                                (b == 1) ? 1 : 0;
                        }
                    } else {
                        for (int i = 0; i < workspace.boneGroupCount; ++i) Rd(fd, &b, 1);
                    }
                }
                Rd(fd, &modelRecord->boneListPos,
                   sizeof modelRecord->boneListPos);            // 0x452C77
                Rd(fd, &modelRecord->maxFrame,
                   sizeof modelRecord->maxFrame);               // 0x452C92
                LogPmmModelStage(fd, "display-keys",
                                 modelRecord->boneListPos,
                                 modelRecord->maxFrame,
                                 modelRecord->boneKeys);
                // dense display-frame keys with remap (0x452C97..0x453086)
                const std::int32_t extraDisp =
                    static_cast<std::int32_t>(modelRecord->boneCount) -
                    workspace.displayCount;
                for (std::int32_t i = 0; i < workspace.displayCount; ++i) {
                    mdl::BoneKey* keys = mdl::BoneKeys(model);
                    NameMapping& mapping = boneMappings[i];
                    LogPmmModelStage(fd, "display-key-item", i,
                                     workspace.displaysMatch, &keys[i]);
                    if (workspace.displaysMatch != 0) {
                        ReadPmmBoneKey<PmmStream::V2>(fd, keys[i]);
                    } else {
                        const std::int32_t m = mapping.mappedIndex;
                        if (m < 0) {
                            mdl::BoneKey discardedKey{};
                            ReadPmmBoneKey<PmmStream::V2>(fd, discardedKey);
                            mapping.frameOffset =
                                static_cast<std::int32_t>(discardedKey.next);
                            if (mapping.frameOffset != 0)
                                mapping.frameOffset += extraDisp;
                        } else {
                            ReadPmmBoneKey<PmmStream::V2>(fd, keys[m]);
                            if (keys[m].next != 0) keys[m].next += extraDisp;
                        }
                    }
                    LogPmmModelStage(fd, "display-key-done", i,
                                     workspace.displaysMatch, &keys[i]);
                }
                // sparse display-frame keys (0x453087..0x453265)
                {
                    std::int32_t cnt = 0;
                    Rd(fd, &cnt, 4);
                    for (std::int32_t i = 0; i < cnt; ++i) {
                        std::int32_t frame = 0;
                        Rd(fd, &frame, 4);
                        frame += extraDisp;
                        mdl::BoneKey* keys = mdl::BoneKeys(model);
                        LogPmmModelStage(fd, "sparse-display-item", i,
                                         frame, &keys[frame]);
                        ReadPmmBoneKey<PmmStream::V2>(fd, keys[frame]);
                        if (keys[frame].previous >=
                            static_cast<std::uint32_t>(workspace.displayCount))
                            keys[frame].previous += extraDisp;
                        if (keys[frame].next != 0)
                            keys[frame].next += extraDisp;
                        LogPmmModelStage(fd, "sparse-display-done", i,
                                         frame, &keys[frame]);
                    }
                }
                LogPmmModelStage(fd, "display-section-complete", 0, 0,
                                 mdl::BoneKeys(model));
                // chain-head fixups when the table did not match
                if (workspace.displaysMatch == 0) {                           // 0x45326F
                    mdl::BoneKey* keys = mdl::BoneKeys(model);
                    for (std::int32_t i = 0; i < workspace.displayCount; ++i) {
                        const NameMapping& mapping = boneMappings[i];
                        const std::int32_t m = mapping.mappedIndex;
                        if (m < 0) {
                            for (std::int32_t j = mapping.frameOffset;
                                 j != 0;) {
                                const std::int32_t next = keys[j].next;
                                // x64 0x7FF7CB49C470..0x7FF7CB49C563:
                                // the dead-key sweep also zeroes next
                                // (+8) and seeds rotation w with the
                                // 0x3F800000 literal; only then is the
                                // saved next used to advance.
                                keys[j].frame = 0;
                                keys[j].previous = 0;
                                keys[j].next = 0;
                                keys[j].allocated = 0;
                                keys[j].position[0] = keys[j].position[1] =
                                    keys[j].position[2] = 0.0f;
                                keys[j].rotation[0] = keys[j].rotation[1] =
                                    keys[j].rotation[2] = 0.0f;
                                keys[j].rotation[3] = 1.0f;
                                j = next;
                            }
                        } else {
                            keys[keys[m].next].previous = m;
                        }
                    }
                }
                // dense morph keys (0x453427..0x4535B6) + sparse (0x4535C2..)
                const std::int32_t extraMorph =
                    static_cast<std::int32_t>(modelRecord->morphCount) -
                    workspace.morphCount;
                for (std::int32_t i = 0; i < workspace.morphCount; ++i) {
                    mdl::MorphKey* keys = mdl::MorphKeys(model);
                    NameMapping& mapping = morphMappings[i];
                    if (workspace.morphsMatch != 0) {
                        ReadPmmMorphKey(fd, keys[i]);
                    } else {
                        const std::int32_t m = mapping.mappedIndex;
                        if (m < 0) {
                            mdl::MorphKey discardedKey{};
                            ReadPmmMorphKey(fd, discardedKey);
                            mapping.frameOffset =
                                static_cast<std::int32_t>(discardedKey.next);
                            if (mapping.frameOffset != 0)
                                mapping.frameOffset += extraMorph;
                        } else {
                            ReadPmmMorphKey(fd, keys[m]);
                            if (keys[m].next != 0) keys[m].next += extraMorph;
                        }
                    }
                }
                {
                    std::int32_t cnt = 0;
                    Rd(fd, &cnt, 4);                            // 0x4535C2
                    LogPmmModelStage(fd, "sparse-morph-count", cnt,
                                     workspace.morphCount, mdl::MorphKeys(model));
                    for (std::int32_t i = 0; i < cnt; ++i) {
                        std::int32_t frame = 0;
                        Rd(fd, &frame, 4);
                        frame += extraMorph;
                        mdl::MorphKey* keys = mdl::MorphKeys(model);
                        ReadPmmMorphKey(fd, keys[frame]);
                        if (keys[frame].previous >=
                            static_cast<std::uint32_t>(workspace.morphCount))
                            keys[frame].previous += extraMorph;
                        if (keys[frame].next != 0)
                            keys[frame].next += extraMorph;
                    }
                }
                if (workspace.morphsMatch == 0) {                           // 0x45369C
                    mdl::MorphKey* keys = mdl::MorphKeys(model);
                    for (std::int32_t i = 0; i < workspace.morphCount; ++i) {
                        const NameMapping& mapping = morphMappings[i];
                        const std::int32_t m = mapping.mappedIndex;
                        if (m < 0) {
                            for (std::int32_t j = mapping.frameOffset;
                                 j != 0;) {
                                const std::int32_t next = keys[j].next;
                                // x64 0x7FF7CB49C8A1..0x7FF7CB49C8E8:
                                // frame/previous/next (+0/+4/+8), the
                                // value dword (+12) and the allocated
                                // byte (+16) are all cleared.
                                keys[j].frame = 0;
                                keys[j].previous = 0;
                                keys[j].next = 0;
                                keys[j].allocated = 0;
                                keys[j].value = 0.0f;
                                j = next;
                            }
                        } else {
                            keys[keys[m].next].previous = m;
                        }
                    }
                }
                LogPmmModelStage(fd, "morph-section-complete", 0, 0,
                                 mdl::MorphKeys(model));
                // physics key record 0 + sparse (0x4537D6..0x453B57)
                {
                    mdl::DisplayKey* keys = mdl::DisplayKeys(model);
                    LogPmmModelStage(fd, "physics-section", workspace.ikCount,
                                     workspace.rigidBodyCount, keys);
                    Rd(fd, &keys[0].frame, 4);
                    Rd(fd, &keys[0].previous, 4);
                    Rd(fd, &keys[0].next, 4);
                    unsigned char visible = 0;
                    Rd(fd, &visible, 1);
                    keys[0].visible = visible == 1;
                    for (std::int32_t i = 0; i < workspace.ikCount; ++i) {
                        unsigned char b = 0;
                        Rd(fd, &b, 1);                          // 0x4537FB
                        const std::int32_t m = ikMappings[i].mappedIndex;
                        if (m >= 0)
                            mdl::IkStates(keys[0])[m] =
                                b == 1;
                    }
                    for (std::int32_t i = 0; i < workspace.rigidBodyCount; ++i) {
                        std::int32_t bone = 0, val = 0;
                        Rd(fd, &bone, 4);                       // 0x45386D
                        Rd(fd, &val, 4);
                        const std::int32_t m = rigidMappings[i].mappedIndex;
                        if (m >= 0) {
                            auto* tbl = mdl::SelectorStates(keys[0]);
                            LogPmmModelStage(fd, "physics-rigid-item", i, m,
                                             tbl);
                            tbl[m].modelIndex = bone;
                            std::memcpy(&tbl[m].boneIndex, &val, sizeof(val));
                        }
                    }
                    {
                        unsigned char b = 0;
                        Rd(fd, &b, 1);                          // 0x4538F4
                        keys[0].allocated = b == 1;
                    }
                    std::int32_t cnt = 0;
                    Rd(fd, &cnt, 4);                            // 0x453927
                    LogPmmModelStage(fd, "sparse-physics-count", cnt,
                                     workspace.rigidBodyCount, keys);
                    for (std::int32_t i = 0; i < cnt; ++i) {
                        std::int32_t frame = 0;
                        Rd(fd, &frame, 4);
                        mdl::DisplayKey& k = keys[frame];
                        Rd(fd, &k.frame, 4);
                        Rd(fd, &k.previous, 4);
                        Rd(fd, &k.next, 4);
                        unsigned char visible = 0;
                        Rd(fd, &visible, 1);
                        k.visible = visible == 1;
                        for (std::int32_t j = 0; j < workspace.ikCount; ++j) {
                            unsigned char b = 0;
                            Rd(fd, &b, 1);
                            const std::int32_t m = ikMappings[j].mappedIndex;
                            if (m >= 0)
                                mdl::IkStates(k)[m] =
                                    b == 1;
                        }
                        for (std::int32_t j = 0; j < workspace.rigidBodyCount; ++j) {
                            std::int32_t bone = 0, val = 0;
                            Rd(fd, &bone, 4);
                            Rd(fd, &val, 4);
                            const std::int32_t m = rigidMappings[j].mappedIndex;
                            if (m >= 0) {
                                auto* tbl = mdl::SelectorStates(k);
                                tbl[m].modelIndex = bone;
                                std::memcpy(&tbl[m].boneIndex, &val,
                                            sizeof(val));
                            }
                        }
                        unsigned char b = 0;
                        Rd(fd, &b, 1);                          // 0x453B02
                        k.allocated = b == 1;
                    }
                }
                LogPmmModelStage(fd, "physics-section-complete", 0, 0,
                                 mdl::DisplayKeys(model));
                // current pose: per display frame (0x453B72..0x453E15)
                {
                    LogPmmModelStage(fd, "pose-display", workspace.displayCount,
                                     static_cast<std::int32_t>(modelRecord->boneCount),
                                     mdl::Bones(model));
                    for (std::int32_t i = 0; i < workspace.displayCount; ++i) {
                        const std::int32_t m = boneMappings[i].mappedIndex;
                        if (m < 0) {
                            std::int32_t v = 0;
                            unsigned char b = 0;
                            for (int k = 0; k < 7; ++k) Rd(fd, &v, 4);
                            Rd(fd, &b, 1); Rd(fd, &b, 1); Rd(fd, &b, 1);
                        } else {
                            mdl::BoneRecord& bone = mdl::Bones(model)[m];
                            Rd(fd, bone.trans, sizeof bone.trans);
                            Rd(fd, bone.rotQuat, sizeof bone.rotQuat);
                            unsigned char b = 0;
                            Rd(fd, &b, 1);
                            bone.physicsDisabled = (b == 1) ? 1 : 0;
                            Rd(fd, &b, 1);
                            mdl::Mdl(model)->bonePhysicsState[m] =
                                (b == 1) ? 1 : 0;
                            Rd(fd, &b, 1);
                            mdl::Mdl(model)->boneSelection[m] =
                                (b == 1) ? 1 : 0;
                        }
                    }
                }
                LogPmmModelStage(fd, "pose-display-complete", 0, 0,
                                 mdl::Bones(model));
                // current pose: per morph (0x453E19..0x453E89)
                {
                    for (std::int32_t i = 0; i < workspace.morphCount; ++i) {
                        const std::int32_t m = morphMappings[i].mappedIndex;
                        if (m < 0) {
                            std::int32_t v = 0;
                            Rd(fd, &v, 4);
                        } else {
                            Rd(fd, &mdl::Morphs(model)[m].value,
                               sizeof(float));
                        }
                    }
                }
                LogPmmModelStage(fd, "pose-morph-complete", 0, 0,
                                 mdl::Morphs(model));
                // current pose: per IK (0x453E8B..0x453F0F)
                {
                    for (std::int32_t i = 0; i < workspace.ikCount; ++i) {
                        unsigned char b = 0;
                        Rd(fd, &b, 1);
                        const std::int32_t m = ikMappings[i].mappedIndex;
                        if (m >= 0)
                            mdl::IkChains(model)[m].enabled =
                                (b == 1) ? 1 : 0;
                    }
                }
                LogPmmModelStage(fd, "pose-ik-complete", 0, 0,
                                 mdl::IkChains(model));
                EnableMenuItem(GetMenu(main), 0x120, 0);        // 0x453F26
                EnableMenuItem(GetMenu(main), 0x121, 0);        // 0x453F41
                // current pose: per rigid (0x453F47..0x454040)
                {
                    LogPmmModelStage(fd, "pose-rigid", workspace.rigidBodyCount,
                                     mdl::BoneOrderCount(model),
                                     mdl::BoneOrder(model));
                    for (std::int32_t i = 0; i < workspace.rigidBodyCount; ++i) {
                        SelectorStateSnapshot saved;
                        Rd(fd, &saved.windowStart, 4);            // 0x453F6B
                        Rd(fd, &saved.windowEnd, 4);
                        Rd(fd, &saved.linkedModel, 4);
                        Rd(fd, &saved.linkedBone, 4);
                        const std::int32_t m = rigidMappings[i].mappedIndex;
                        LogPmmModelStage(fd, "pose-rigid-item", i, m,
                                         mdl::BoneOrder(model));
                        if (m >= 0) {
                            mdl::BoneOrderEntry& selector =
                                mdl::BoneOrder(model)[m];
                            selector.windowStart = saved.windowStart;
                            selector.windowEnd = saved.windowEnd;
                            selector.linkedModel = saved.linkedModel;
                            selector.linkedBone = saved.linkedBone;
                        }
                    }
                }
                LogPmmModelStage(fd, "pose-rigid-complete", 0, 0,
                                  mdl::BoneOrder(model));
                {
                    unsigned char b = 0;
                    Rd(fd, &b, 1);                              // 0x45404E
                    mdl::Mdl(model)->postLoadFlag2 = (b == 1) ? 1 : 0;
                }
                Rd(fd, &mdl::Mdl(model)->edgeScale, sizeof(float));
                {
                    unsigned char b = 0;
                    Rd(fd, &b, 1);                              // 0x4540A7
                    mdl::Mdl(model)->toonFlag = (b == 1) ? 1 : 0;
                }
                Rd(fd, &mdl::Mdl(model)->comboSelIndex2,
                   sizeof(std::uint8_t));
                LogPmmModelStage(fd, "model-state-complete", modelIdx,
                                 modelCount, model);
                LogPmmHeapState(fd, "after-model-state");
}


// ---- 0x4504BA..0x45410A: one per-model record - allocate/init, load
// the PMD (missing-file dialog + locate-and-retry), the reconciliation
// ring (structure-difference dialog + re-map against a re-loaded file),
// then skip consumption or the state load.  Returns false after
// AbortV2Load ran.
static bool LoadSceneV2_ModelBlock(PmmV2LoadContext& ctx, int fd,
                                   PmmModelLoadWorkspace* workspaces,
                                   unsigned char modelCount,
                                   unsigned char modelIdx) {
    auto* const s = ctx.s;
    HWND const main = ctx.main;
    HINSTANCE const hInst = ctx.hInst;
    PathResolutionWorkspace& paths = *ctx.paths;
    D3DRenderer* const wrap = ctx.wrap;
    unsigned char** const slots = ctx.slots;
    char* const text = ctx.text;
    wchar_t* const widePath = ctx.widePath;
    wchar_t* const ofnFile = ctx.ofnFile;
    wchar_t* const ofnTitle = ctx.ofnTitle;
            unsigned char slotByte = 0;
            Rd(fd, &slotByte, 1);                               // 0x4504C2
            if (slotByte >= kModelSlotCount) {
                AbortV2Load(s, fd, workspaces, modelCount);
                return false;
            }
            unsigned char* nm =
                static_cast<unsigned char*>(operator new(mdl::kSize));
            if (nm != nullptr) IdentityCtor(nm);                   // 0x4504E9
            slots[slotByte] = nm;                               // 0x4504F7
            std::memset(slots[slotByte], 0, mdl::kSize);        // 0x45051D
            ModelInitDefaults(slots[slotByte]);                 // 0x450531
            PmmModelLoadWorkspace& workspace = workspaces[modelIdx];
            workspace.modelSlot = slotByte;                      // 0x450559

            {  // stored model name (0x45054D..0x450592)
                unsigned char len = 0;
                Rd(fd, &len, 1);
                Rd(fd, text, len);
                text[len] = 0;
                strcpy_s(workspace.modelName, 0x100, text);
            }
            {  // source model name (0x450597..0x4505D9)
                unsigned char len = 0;
                Rd(fd, &len, 1);
                Rd(fd, text, len);
                text[len] = 0;
                strcpy_s(workspace.sourceName, 0x100, text);
            }
            Rd(fd, text, 0x100);                                // 0x4505EF
            ResolveAnsiUserFile(reinterpret_cast<unsigned char*>(wrap),
                                text, widePath, 0x100,
                                paths);

            unsigned char* model = slots[slotByte];
            LogPmmModelAttempt(text, widePath, wrap,
                               s->Physics(), false);
            bool loaded = ModelLoadPMD(                          // 0x45065F
                model, main, widePath, wrap,
                static_cast<int>(
                    reinterpret_cast<std::uintptr_t>(s->state.exeDir)),
                0, s->EnglishUI(), s->Physics(),
                paths);
            LogPmmModelAttempt(text, widePath, wrap,
                               s->Physics(), loaded);
            LogPmmModelStage(fd, "model-loaded", 0, 0, model);
            LogPmmHeapState(fd, "after-model-load");

            bool doSkip = false;
            bool skipFrom4 = false;  // r==4: counts/names already consumed
            if (!loaded) {                                      // 0x45066C
                if (s->EnglishUI() != 0)
                    sprintf_s(s->state.statusText,
                              0x100, "Cannot open the model file:%s",
                              workspace.sourceName);
                else
                    sprintf_s(s->state.statusText,
                              0x100, kJpQuoted,
                              workspace.modelName);
                const INT_PTR r = DialogBoxParamA(
                    hInst,
                    MAKEINTRESOURCEA(s->EnglishUI() != 0 ? 0x32F : 0x32E),
                    main, DialogFuncStub, 0);
                if (r == 2 || r == 4) { AbortV2Load(s, fd, workspaces, modelCount); return false; }   // 0x45426C
                if (r == 1) {                                   // 0x4506FD
                    SetCurrentDirectoryW(reinterpret_cast<const wchar_t*>(
                        s->state.exeDir));
                    swprintf_s(ofnFile, 0x100, L"%s", ofnFile); // quirk
                    OPENFILENAMEW ofn;
                    std::memset(&ofn, 0, sizeof(ofn));
                    ofn.lStructSize = sizeof(ofn);
                    ofn.hwndOwner =
                        s->state.floatingWindow
                            ? reinterpret_cast<HWND>(
                                  s->state.floatingWindow)
                            : main;
                    ofn.lpstrFilter = kWFilterModel;
                    ofn.lpstrFile = ofnFile;
                    ofn.nMaxFile = 0x100;
                    ofn.Flags = OFN_FILEMUSTEXIST;
                    ofn.lpstrInitialDir =
                        (GetMenuState(GetMenu(main), 0x12D, 0) & 8)
                            ? reinterpret_cast<LPCWSTR>(s->state.dirModel)
                            : kWUserModel;
                    ofn.lpstrDefExt = L"pmd;pmx";
                    ofn.nMaxFileTitle = 0x100;
                    ofn.lpstrFileTitle = ofnTitle;
                    ofn.lpstrTitle =
                        s->EnglishUI() != 0
                            ? L"load model"
                            : reinterpret_cast<LPCWSTR>(kWOpenFile);
                    if (!GetOpenFileNameW(&ofn)) { AbortV2Load(s, fd, workspaces, modelCount); return false; }
                    if (GetMenuState(GetMenu(main), 0x12D, 0) & 8) {
                        wchar_t* d = ExtractDirFromPath(
                            paths.projectDirectory,
                            ofnFile);
                        wcscpy_s(reinterpret_cast<wchar_t*>(s->state.dirModel),
                                 0x3E8, d);
                    }
                    if (!ModelLoadPMD(
                            model, main, ofnFile, wrap,
                            static_cast<int>(reinterpret_cast<
                                std::uintptr_t>(s->state.exeDir)),
                            0, s->EnglishUI(),
                            s->Physics(),
                            paths)) {                             // 0x45431A
                        if (s->EnglishUI() != 0)
                            MessageBoxA(main, "Cannot open the model file",
                                        "open file", 0);
                        else
                            MessageBoxA(main, kJpCannotOpenModel,
                                        kJpOpenCaption, 0);
                        AbortV2Load(s, fd, workspaces, modelCount);
                        return false;
                    }
                } else if (r == 3) {
                    doSkip = true;                              // 0x4508FC
                }
                // other results fall through to the success path
            }

            if (doSkip) {
                if (model != nullptr) {
                    ModelDispose(model);                        // 0x450912
                    ::operator delete(model);
                }
                slots[slotByte] = nullptr;
                workspace.skipped = 1;                                     // 0x45093E
                if (!skipFrom4) Rd(fd, &workspace.boneGroupCount, 1);         // 0x450942
            } else {
                Rd(fd, &workspace.boneGroupCount, 1);                         // 0x4508C5
                workspace.boneGroupsMatch = workspace.boneGroupCount == mikudancestudio::mdl::Mdl(model)->groupCount ? 1 : 0; // 0x4508DF
                LogPmmModelStage(fd, "model-marker", workspace.boneGroupCount,
                                 mikudancestudio::mdl::Mdl(model)->groupCount, model);
            }

            // ---- reconciliation + state load ------------------------------
            if (!doSkip) {
                mdl::ModelRecord* const loadedModel = mdl::Mdl(model);
                bool firstPass = true;  // counts/names are read only once;
                bool reloaded = false;  // r==3 re-load switches the status
                                        // text to the "too" variants
                for (;;) {              // ring retries only re-map
                    if (firstPass) {
                    // disp count + translation array (0x4511F5..0x4514B9)
                    Rd(fd, &workspace.displayCount, 4);
                    LogPmmModelStage(fd, "display-count", workspace.displayCount,
                                     loadedModel->boneCount,
                                     loadedModel->boneTable);
                    workspace.displaysMatch =
                        workspace.displayCount == loadedModel->boneCount ? 1 : 0;
                    workspace.boneNameMap = static_cast<NameMapping*>(
                        std::malloc(sizeof(NameMapping) *
                                    workspace.displayCount));
                    std::memset(workspace.boneNameMap, 0,
                                sizeof(NameMapping) * workspace.displayCount);
                    NameMapping* const boneMappings =
                        workspace.boneNameMap;
                    for (std::int32_t i = 0; i < workspace.displayCount; ++i) {
                        unsigned char len = 0;
                        Rd(fd, &len, 1);
                        NameMapping& mapping = boneMappings[i];
                        Rd(fd, mapping.name, len);
                        mapping.name[len] = 0;
                        mapping.mappedIndex = i;
                        if (workspace.displayCount == loadedModel->boneCount) {
                            if (strcmp(mapping.name,
                                       mdl::Bones(model)[i].name) != 0) {
                                workspace.displaysMatch = 0;
                                mapping.mappedIndex = -1;
                                for (std::int32_t j = 0;
                                     j < static_cast<std::int32_t>(
                                             loadedModel->boneCount); ++j)
                                    if (strcmp(mapping.name,
                                               mdl::Bones(model)[j].name) == 0) {
                                        mapping.mappedIndex = j;
                                        break;
                                    }
                            }
                        } else {
                            mapping.mappedIndex = -1;
                            for (std::int32_t j = 0;
                                 j < static_cast<std::int32_t>(
                                         loadedModel->boneCount); ++j)
                                if (strcmp(mapping.name,
                                           mdl::Bones(model)[j].name) == 0) {
                                    mapping.mappedIndex = j;
                                    break;
                                }
                        }
                    }
                    // morph count + translation array (0x4514C5..0x451789)
                    Rd(fd, &workspace.morphCount, 4);
                    LogPmmModelStage(fd, "morph-count", workspace.morphCount,
                                     loadedModel->morphCount,
                                     loadedModel->morphs);
                    workspace.morphsMatch =
                        workspace.morphCount == loadedModel->morphCount ? 1 : 0;
                    workspace.morphNameMap = static_cast<NameMapping*>(
                        std::malloc(sizeof(NameMapping) *
                                    workspace.morphCount));
                    std::memset(workspace.morphNameMap, 0,
                                sizeof(NameMapping) * workspace.morphCount);
                    NameMapping* const morphMappings =
                        workspace.morphNameMap;
                    for (std::int32_t i = 0; i < workspace.morphCount; ++i) {
                        unsigned char len = 0;
                        Rd(fd, &len, 1);
                        NameMapping& mapping = morphMappings[i];
                        Rd(fd, mapping.name, len);
                        mapping.name[len] = 0;
                        mapping.mappedIndex = i;
                        if (workspace.morphCount == loadedModel->morphCount) {
                            if (strcmp(mapping.name,
                                       mdl::Morphs(model)[i].name) != 0) {
                                workspace.morphsMatch = 0;
                                mapping.mappedIndex = -1;
                                for (std::int32_t j = 0;
                                     j < static_cast<std::int32_t>(
                                             loadedModel->morphCount); ++j)
                                    if (strcmp(mapping.name,
                                            mdl::Morphs(model)[j].name) == 0) {
                                        mapping.mappedIndex = j;
                                        break;
                                    }
                            }
                        } else {
                            mapping.mappedIndex = -1;
                            for (std::int32_t j = 0;
                                 j < static_cast<std::int32_t>(
                                         loadedModel->morphCount); ++j)
                                if (strcmp(mapping.name,
                                        mdl::Morphs(model)[j].name) == 0) {
                                    mapping.mappedIndex = j;
                                    break;
                                }
                        }
                    }
                    // IK remap array (0x451795..0x451886).  x64
                    // 0x7FF7CB49A87A..0x7FF7CB49A8C6: the stored IK number
                    // is a bone index in the SAVED model's numbering - the
                    // original maps it through the bone name translation
                    // array first (movsxd rcx,[r11+rsi+4]; imul rcx,108h;
                    // mov r9d,[rcx+rax+100h]; cmp r9d,[rax]) before
                    // comparing with the loaded IK chain's bone index.
                    Rd(fd, &workspace.ikCount, 4);
                    LogPmmModelStage(fd, "ik-count", workspace.ikCount,
                                     loadedModel->ikChainCount,
                                     loadedModel->ikChains);
                    workspace.ikIndexMap = static_cast<IndexMapping*>(
                        std::malloc(sizeof(IndexMapping) * workspace.ikCount));
                    IndexMapping* const ikMappings =
                        workspace.ikIndexMap;
                    for (std::int32_t i = 0; i < workspace.ikCount; ++i) {
                        IndexMapping& mapping = ikMappings[i];
                        Rd(fd, &mapping.sourceIndex, 4);
                        mapping.mappedIndex = -1;
                        for (std::int32_t j = 0;
                             j < static_cast<std::int32_t>(
                                     loadedModel->ikChainCount); ++j)
                            if (boneMappings[mapping.sourceIndex]
                                    .mappedIndex ==
                                mdl::IkChains(model)[j].boneIndex) {
                                mapping.mappedIndex = j;
                                break;
                            }
                    }
                    // rigid remap array, 1-based (0x451892..0x45199E)
                    Rd(fd, &workspace.rigidBodyCount, 4);
                    LogPmmModelStage(fd, "rigid-count", workspace.rigidBodyCount,
                                     loadedModel->boneOrderCount,
                                     loadedModel->boneOrderTable);
                    workspace.rigidIndexMap = static_cast<IndexMapping*>(
                        std::malloc(sizeof(IndexMapping) *
                                    workspace.rigidBodyCount));
                    IndexMapping* const rigidMappings =
                        workspace.rigidIndexMap;
                    for (std::int32_t i = 0; i < workspace.rigidBodyCount; ++i) {
                        IndexMapping& mapping = rigidMappings[i];
                        Rd(fd, &mapping.sourceIndex, 4);
                        if (mapping.sourceIndex == -1) {
                            mapping.mappedIndex = 0;
                        } else {
                            mapping.mappedIndex = -1;
                            if (loadedModel->boneOrderCount > 1) {
                                for (std::int32_t j = 1;
                                     j < static_cast<std::int32_t>(
                                             loadedModel->boneOrderCount); ++j)
                                    if (boneMappings[mapping.sourceIndex]
                                            .mappedIndex ==
                                        mdl::BoneOrder(model)[j].boneIndex) {
                                        mapping.mappedIndex = j;
                                        break;
                                    }
                            }
                        }
                        LogPmmModelStage(fd, "rigid-item", i,
                                         mapping.mappedIndex, &mapping);
                    }
                    LogPmmModelStage(fd, "mapping-complete", 0, 0, model);

                    }  // firstPass guard

                    // structure-difference gate (0x4519AF..0x451A44).
                    // x64 0x7FF7CB49A9E4 (first pass): EN prints the plain
                    // structure text against sourceName, JP a bare quoted
                    // name (aS_8 "\"%s\"", x64 0x7FF7CB5508E0) - NOT the
                    // long kJpStructFmt text.  Every re-test after an
                    // r==3 re-load (x64 0x7FF7CB49B265..0x7FF7CB49B294)
                    // switches to the "too" variants: EN "This model
                    // structure is different from pmm too." (0x7FF7CB550910)
                    // / JP kJpStructFmt (0x7FF7CB550948).
                    if (s->EnglishUI() != 0)
                        sprintf_s(s->state.statusText,
                                  0x100,
                                  reloaded
                                      ? "This model structure is different "
                                        "from pmm too. file:%s"
                                      : "Model structure is different from "
                                        "pmm. file:%s",
                                  workspace.sourceName);
                    else
                        sprintf_s(s->state.statusText,
                                  0x100,
                                  reloaded ? kJpStructFmt : kJpQuoted,
                                  workspace.modelName);
                    if (workspace.morphsMatch != 0 && workspace.displaysMatch != 0) break;

                    const INT_PTR r = DialogBoxParamA(
                        hInst,
                        MAKEINTRESOURCEA(s->EnglishUI() != 0 ? 0x331
                                                           : 0x330),
                        main, DialogFuncStub, 0);
                    // x64 分派表（0x7FF7CB49AA72..0x7FF7CB49BB78）：
                    //   r==2/r==5 -> 中止装载（0x49AA88 jnz 0x49D73F）
                    //   r==3      -> 丢弃当前模型、弹文件对话框重载（0x49AA8E）
                    //   r==4      -> 跳过该模型（0x49B29D cmp 4）
                    //   其余值    -> 0x49B2A1 jnz 0x49BB78，与双匹配成立时
                    //                （0x49AA2F 的 jz）同一出口：继续状态装载
                    if (r == 2 || r == 5) { AbortV2Load(s, fd, workspaces, modelCount); return false; }
                    if (r == 4) {                                // 0x45231C
                        if (model != nullptr) {
                            ModelDispose(model);
                            ::operator delete(model);
                        }
                        slots[slotByte] = nullptr;
                        workspace.skipped = 1;
                        doSkip = true;
                        skipFrom4 = true;
                        firstPass = false;  // r==4: counts/names consumed
                        break;
                    }
                    if (r != 3)
                        break;  // 非 3 非 4：直接走状态装载，不重载
                    // r == 3: re-load a different file
                    if (model != nullptr) {                      // 0x451A5A
                        ModelDispose(model);
                        ::operator delete(model);
                    }
                    {
                        unsigned char* nm2 = static_cast<unsigned char*>(
                            operator new(mdl::kSize));
                        if (nm2 != nullptr) IdentityCtor(nm2);
                        slots[slotByte] = nm2;
                        std::memset(slots[slotByte], 0, mdl::kSize);
                        ModelInitDefaults(slots[slotByte]);
                        model = slots[slotByte];
                    }
                    SetCurrentDirectoryW(reinterpret_cast<const wchar_t*>(
                        s->state.exeDir));
                    swprintf_s(ofnFile, 0x100, L"%s", ofnFile); // quirk
                    {
                        OPENFILENAMEW ofn;
                        std::memset(&ofn, 0, sizeof(ofn));
                        ofn.lStructSize = sizeof(ofn);
                        ofn.hwndOwner =
                            s->state.floatingWindow
                                ? reinterpret_cast<HWND>(
                                      s->state.floatingWindow)
                                : main;
                        ofn.lpstrFilter = kWFilterModel;
                        ofn.lpstrFile = ofnFile;
                        ofn.nMaxFile = 0x100;
                        ofn.Flags = OFN_FILEMUSTEXIST;
                        ofn.lpstrInitialDir =
                            (GetMenuState(GetMenu(main), 0x12D, 0) & 8)
                                ? reinterpret_cast<LPCWSTR>(
                                      s->state.dirModel)
                                : kWUserModel;
                        ofn.lpstrDefExt = L"pmd;pmx";
                        ofn.nMaxFileTitle = 0x100;
                        ofn.lpstrFileTitle = ofnTitle;
                        ofn.lpstrTitle =
                            s->EnglishUI() != 0
                                ? L"load model"
                                : reinterpret_cast<LPCWSTR>(kWOpenFile);
                        if (!GetOpenFileNameW(&ofn)) { AbortV2Load(s, fd, workspaces, modelCount); return false; }
                        if (GetMenuState(GetMenu(main), 0x12D, 0) & 8) {
                            wchar_t* d = ExtractDirFromPath(
                            paths.projectDirectory,
                                ofnFile);
                            wcscpy_s(
                                reinterpret_cast<wchar_t*>(s->state.dirModel),
                                0x3E8, d);
                        }
                    }
                    if (!ModelLoadPMD(
                            model, main, ofnFile, wrap,
                            static_cast<int>(reinterpret_cast<
                                std::uintptr_t>(s->state.exeDir)),
                            0, s->EnglishUI(),
                            s->Physics(),
                            paths)) {                             // 0x4544FD
                        if (s->EnglishUI() != 0)
                            MessageBoxA(main, "Cannot open the model file",
                                        "open file", 0);
                        else
                            MessageBoxA(main, kJpCannotOpenModel,
                                        kJpOpenCaption, 0);
                        AbortV2Load(s, fd, workspaces, modelCount);
                        return false;
                    }
                    // re-run the mapping against the new model
                    // (0x451CCB..0x4522DC), then re-test the gate
                    mdl::ModelRecord* const reloadedModel = mdl::Mdl(model);
                    workspace.boneGroupsMatch =
                        workspace.boneGroupCount == reloadedModel->groupCount ? 1 : 0;
                    workspace.displaysMatch =
                        workspace.displayCount == reloadedModel->boneCount ? 1 : 0;
                    NameMapping* const boneMappings =
                        workspace.boneNameMap;
                    for (std::int32_t i = 0; i < workspace.displayCount; ++i) {
                        NameMapping& mapping = boneMappings[i];
                        mapping.mappedIndex = i;
                        if (workspace.displayCount == reloadedModel->boneCount) {
                            if (strcmp(mapping.name,
                                       mdl::Bones(model)[i].name) != 0) {
                                workspace.displaysMatch = 0;
                                mapping.mappedIndex = -1;
                                for (std::int32_t j = 0;
                                     j < static_cast<std::int32_t>(
                                             reloadedModel->boneCount); ++j)
                                    if (strcmp(mapping.name,
                                               mdl::Bones(model)[j].name) == 0) {
                                        mapping.mappedIndex = j;
                                        break;
                                    }
                            }
                        } else {
                            mapping.mappedIndex = -1;
                            for (std::int32_t j = 0;
                                 j < static_cast<std::int32_t>(
                                         reloadedModel->boneCount); ++j)
                                if (strcmp(mapping.name,
                                           mdl::Bones(model)[j].name) == 0) {
                                    mapping.mappedIndex = j;
                                    break;
                                }
                        }
                    }
                    workspace.morphsMatch =
                        workspace.morphCount == reloadedModel->morphCount ? 1 : 0;
                    NameMapping* const morphMappings =
                        workspace.morphNameMap;
                    for (std::int32_t i = 0; i < workspace.morphCount; ++i) {
                        NameMapping& mapping = morphMappings[i];
                        mapping.mappedIndex = i;
                        if (workspace.morphCount == reloadedModel->morphCount) {
                            if (strcmp(mapping.name,
                                       mdl::Morphs(model)[i].name) != 0) {
                                workspace.morphsMatch = 0;
                                mapping.mappedIndex = -1;
                                for (std::int32_t j = 0;
                                     j < static_cast<std::int32_t>(
                                             reloadedModel->morphCount); ++j)
                                    if (strcmp(mapping.name,
                                               mdl::Morphs(model)[j].name) == 0) {
                                        mapping.mappedIndex = j;
                                        break;
                                    }
                            }
                        } else {
                            mapping.mappedIndex = -1;
                            for (std::int32_t j = 0;
                                 j < static_cast<std::int32_t>(
                                         reloadedModel->morphCount); ++j)
                                if (strcmp(mapping.name,
                                           mdl::Morphs(model)[j].name) == 0) {
                                    mapping.mappedIndex = j;
                                    break;
                                }
                        }
                    }
                    IndexMapping* const ikMappings =
                        workspace.ikIndexMap;
                    for (std::int32_t i = 0; i < workspace.ikCount; ++i) {
                        IndexMapping& mapping = ikMappings[i];
                        mapping.mappedIndex = -1;
                        // x64 0x7FF7CB49B186: same boneMappings
                        // indirection as the first pass above.
                        for (std::int32_t j = 0;
                             j < static_cast<std::int32_t>(
                                     reloadedModel->ikChainCount); ++j)
                            if (boneMappings[mapping.sourceIndex]
                                    .mappedIndex ==
                                mdl::IkChains(model)[j].boneIndex) {
                                mapping.mappedIndex = j;
                                break;
                            }
                    }
                    IndexMapping* const rigidMappings =
                        workspace.rigidIndexMap;
                    for (std::int32_t i = 0; i < workspace.rigidBodyCount; ++i) {
                        IndexMapping& mapping = rigidMappings[i];
                        if (mapping.sourceIndex == -1) {
                            mapping.mappedIndex = 0;
                        } else {
                            mapping.mappedIndex = -1;
                            if (mdl::BoneOrderCount(model) > 1) {
                                mdl::BoneOrderEntry* const selectors =
                                    mdl::BoneOrder(model);
                                for (std::int32_t j = 1;
                                     j < static_cast<std::int32_t>(
                                             mdl::BoneOrderCount(model)); ++j)
                                    if (boneMappings[mapping.sourceIndex]
                                            .mappedIndex ==
                                        selectors[j].boneIndex) {
                                        mapping.mappedIndex = j;
                                        break;
                                    }
                            }
                        }
                    }
                    firstPass = false;
                    reloaded = true;  // gate text switches to the "too"
                                      // variants for every further re-test
                }
            }

            if (doSkip) {
                LoadSceneV2_SkipRecordConsumption(fd, workspace, text,
                                                 skipFrom4);
            } else {
                LoadSceneV2_ModelStateLoad(ctx, fd, model, workspace,
                                           modelIdx, modelCount);
            }

    return true;
}


// ---- 0x454127..0x454213: post-loop quirk + free/NULL the four tracks
// and the 255 accessory objects/tracks ---------------------------------
static void LoadSceneV2_ReleaseTracks(PmmV2LoadContext& ctx, int fd,
                                    PmmModelLoadWorkspace* workspaces,
                                    unsigned char modelIdx,
                                    unsigned char modelCount) {
    auto* const s = ctx.s;
    unsigned char** const slots = ctx.slots;
    // ---- post-loop quirk (0x454127) --------------------------------------
    if (slots[s->SelectedModelSlot()] == nullptr &&
        s->state.optflag[0] == 0)
        s->state.optflag[0] = 1;

    // ---- track free + null (0x45413B..0x454213) ---------------------------
    LogPmmModelStage(fd, "model-loop-complete", modelIdx,
                     modelCount, workspaces);
    LogPmmModelStage(fd, "accessory-track-174-after-model-loop", 0, 0,
                     s->AccessoryKeys(174));
    LogPmmHeapState(fd, "after-model-loop");
    ReleaseGlobalTimelineTracks(*s);                              // 0x45413B
    LogPmmModelStage(fd, "global-track-clear-complete", 0, 0, nullptr);
    ReleaseAccessoriesAndTracks(*s);                              // 0x4541D9
    LogPmmModelStage(fd, "track-clear-complete", 0, 0, nullptr);
    LogPmmModelStage(fd, "overlay-buffer-after-track-clear", 0, 0,
                     s->OverlayVertices());

}


// ---- 0x454230..0x454C8A: UI combo reset run + model re-population by
// order byte + register list per edit mode ------------------------------
static void LoadSceneV2_ResetCombos(PmmV2LoadContext& ctx) {
    auto* const s = ctx.s;
    HWND const main = ctx.main;
    unsigned char** const slots = ctx.slots;
    // ---- UI combo reset run (0x454230..0x454C8A) --------------------------
    SendMessageA(GetDlgItem(main, panel::kMainComboModel), CB_RESETCONTENT, 0, 0);
    if (s->EnglishUI() != 0)
        SendMessageA(GetDlgItem(main, panel::kMainComboModel), CB_ADDSTRING, 0,
                     reinterpret_cast<LPARAM>("camera/light/accessory"));
    else
        SendMessageW(GetDlgItem(main, panel::kMainComboModel), CB_ADDSTRING, 0,
                     reinterpret_cast<LPARAM>(kWCamLight));
    SendMessageA(GetDlgItem(main, panel::kMainComboGround), CB_RESETCONTENT, 0, 0);
    if (s->EnglishUI() != 0)
        SendMessageA(GetDlgItem(main, panel::kMainComboGround), CB_ADDSTRING, 0,
                     reinterpret_cast<LPARAM>("ground"));
    else
        SendMessageW(GetDlgItem(main, panel::kMainComboGround), CB_ADDSTRING, 0,
                     reinterpret_cast<LPARAM>(kWGround));
    SendMessageA(GetDlgItem(main, panel::kMainComboNormal), CB_RESETCONTENT, 0, 0);
    if (s->EnglishUI() != 0)
        SendMessageA(GetDlgItem(main, panel::kMainComboNormal), CB_ADDSTRING, 0,
                     reinterpret_cast<LPARAM>(kNon));
    else
        SendMessageW(GetDlgItem(main, panel::kMainComboNormal), CB_ADDSTRING, 0,
                     reinterpret_cast<LPARAM>(kWNashi));
    SendMessageA(GetDlgItem(main, panel::kBoneRegisterCombo), CB_RESETCONTENT, 0, 0);
    // x64 load twin sub_7FF7CB498E30: every post-read slot walk runs to 255
    // (0xFF counters at 0x7FF7CB49D33A..0x7FF7CB4A2A3A in the load tail).
    for (int j = 0; j < kModelSlotCount; ++j) {                 // 0x454766
        int found = 0;
        while (found < kModelSlotCount &&
               (slots[found] == nullptr ||
                mdl::Mdl(slots[found])->comboSelIndex != j))
            ++found;
        // 0x45478A..0x454897: a missing display-order value advances to
        // the next value; it does not terminate the rebuild.  PMM model IDs
        // normally start at one because combo item zero is camera mode, so
        // breaking here discarded every loaded model at the very first pass.
        if (found >= kModelSlotCount) continue;
        const char* name =
            s->EnglishUI() != 0
                ? mdl::Mdl(slots[found])->nameEn
                : mdl::Mdl(slots[found])->name;
        SendMessageA(GetDlgItem(main, panel::kMainComboModel), CB_ADDSTRING, 0,
                     reinterpret_cast<LPARAM>(name));
        SendMessageA(GetDlgItem(main, panel::kMainComboGround), CB_ADDSTRING, 0,
                     reinterpret_cast<LPARAM>(name));
        SendMessageA(GetDlgItem(main, panel::kMainComboNormal), CB_ADDSTRING, 0,
                     reinterpret_cast<LPARAM>(name));
    }
    SendMessageA(GetDlgItem(main, panel::kInterpCurveCombo), CB_RESETCONTENT, 0, 0); // 0x4548BB
    if (s->state.optflag[0] != 0) {
        if (s->EnglishUI() != 0) {
            SendMessageA(GetDlgItem(main, panel::kInterpCurveCombo), CB_ADDSTRING, 0,
                         reinterpret_cast<LPARAM>("x axis move"));
            SendMessageA(GetDlgItem(main, panel::kInterpCurveCombo), CB_ADDSTRING, 0,
                         reinterpret_cast<LPARAM>("y axis move"));
            SendMessageA(GetDlgItem(main, panel::kInterpCurveCombo), CB_ADDSTRING, 0,
                         reinterpret_cast<LPARAM>("z axis move"));
            SendMessageA(GetDlgItem(main, panel::kInterpCurveCombo), CB_ADDSTRING, 0,
                         reinterpret_cast<LPARAM>("rotation"));
            SendMessageA(GetDlgItem(main, panel::kInterpCurveCombo), CB_ADDSTRING, 0,
                         reinterpret_cast<LPARAM>("distance"));
            SendMessageA(GetDlgItem(main, panel::kInterpCurveCombo), CB_ADDSTRING, 0,
                         reinterpret_cast<LPARAM>("view angle"));
            SendMessageA(GetDlgItem(main, panel::kInterpCurveCombo), CB_ADDSTRING, 0,
                         reinterpret_cast<LPARAM>("all"));
        } else {
            SendMessageW(GetDlgItem(main, panel::kInterpCurveCombo), CB_ADDSTRING, 0,
                         reinterpret_cast<LPARAM>(kWXMove));
            SendMessageW(GetDlgItem(main, panel::kInterpCurveCombo), CB_ADDSTRING, 0,
                         reinterpret_cast<LPARAM>(kWYMove));
            SendMessageW(GetDlgItem(main, panel::kInterpCurveCombo), CB_ADDSTRING, 0,
                         reinterpret_cast<LPARAM>(kWZMove));
            SendMessageW(GetDlgItem(main, panel::kInterpCurveCombo), CB_ADDSTRING, 0,
                         reinterpret_cast<LPARAM>(kWRot));
            SendMessageW(GetDlgItem(main, panel::kInterpCurveCombo), CB_ADDSTRING, 0,
                         reinterpret_cast<LPARAM>(kWDist));
            SendMessageW(GetDlgItem(main, panel::kInterpCurveCombo), CB_ADDSTRING, 0,
                         reinterpret_cast<LPARAM>(kWViewAng));
            SendMessageW(GetDlgItem(main, panel::kInterpCurveCombo), CB_ADDSTRING, 0,
                         reinterpret_cast<LPARAM>(kWAll));
        }
        SendMessageA(GetDlgItem(main, panel::kMainComboModel), CB_SETCURSEL, 0, 0);
    } else {
        if (s->EnglishUI() != 0) {
            SendMessageA(GetDlgItem(main, panel::kInterpCurveCombo), CB_ADDSTRING, 0,
                         reinterpret_cast<LPARAM>("x axis move"));
            SendMessageA(GetDlgItem(main, panel::kInterpCurveCombo), CB_ADDSTRING, 0,
                         reinterpret_cast<LPARAM>("y axis move"));
            SendMessageA(GetDlgItem(main, panel::kInterpCurveCombo), CB_ADDSTRING, 0,
                         reinterpret_cast<LPARAM>("z axis move"));
            SendMessageA(GetDlgItem(main, panel::kInterpCurveCombo), CB_ADDSTRING, 0,
                         reinterpret_cast<LPARAM>("rotation"));
            SendMessageA(GetDlgItem(main, panel::kInterpCurveCombo), CB_ADDSTRING, 0,
                         reinterpret_cast<LPARAM>("all"));
        } else {
            SendMessageW(GetDlgItem(main, panel::kInterpCurveCombo), CB_ADDSTRING, 0,
                         reinterpret_cast<LPARAM>(kWXMove));
            SendMessageW(GetDlgItem(main, panel::kInterpCurveCombo), CB_ADDSTRING, 0,
                         reinterpret_cast<LPARAM>(kWYMove));
            SendMessageW(GetDlgItem(main, panel::kInterpCurveCombo), CB_ADDSTRING, 0,
                         reinterpret_cast<LPARAM>(kWZMove));
            SendMessageW(GetDlgItem(main, panel::kInterpCurveCombo), CB_ADDSTRING, 0,
                         reinterpret_cast<LPARAM>(kWRot));
            SendMessageW(GetDlgItem(main, panel::kInterpCurveCombo), CB_ADDSTRING, 0,
                         reinterpret_cast<LPARAM>(kWAll));
        }
        SendMessageA(GetDlgItem(main, panel::kMainComboModel), CB_SETCURSEL,
                     mdl::Mdl(slots[s->SelectedModelSlot()])->comboSelIndex,
                     0);
        PostLoadInit(slots[s->SelectedModelSlot()]);
    }
    SendMessageA(GetDlgItem(main, panel::kInterpCurveCombo), CB_SETCURSEL, 3, 0);    // 0x454C8A

}


// ---- 0x454C9A..0x454DFB: track re-allocation + defaults + 255
// accessory tracks ------------------------------------------------------
static void LoadSceneV2_ReallocateTracks(PmmV2LoadContext& ctx) {
    auto* const s = ctx.s;
    D3DRenderer* const wrap = ctx.wrap;
    mdl::AccessoryRecord** const accs = ctx.accs;
    mdl::AccessoryKey** const accTracks = ctx.accTracks;
    // ---- track re-allocation (0x454C95..0x454DFB) -------------------------
    ctx.cameraKeys = static_cast<mdl::CameraKey*>(operator new(
        sizeof(mdl::CameraKey) * kGlobalKeyCapacity));
    s->CameraKeys() = ctx.cameraKeys;
    std::memset(ctx.cameraKeys, 0,
                sizeof(mdl::CameraKey) * kGlobalKeyCapacity);
    ctx.lightKeys = static_cast<mdl::LightKey*>(operator new(
        sizeof(mdl::LightKey) * kGlobalKeyCapacity));
    s->LightKeys() = ctx.lightKeys;
    std::memset(ctx.lightKeys, 0,
                sizeof(mdl::LightKey) * kGlobalKeyCapacity);
    ctx.selfShadowKeys = static_cast<mdl::SelfShadowKey*>(
        operator new(sizeof(mdl::SelfShadowKey) * kGlobalKeyCapacity));
    s->ShadowKeys() = ctx.selfShadowKeys;
    std::memset(ctx.selfShadowKeys, 0,
                sizeof(mdl::SelfShadowKey) * kGlobalKeyCapacity);
    ctx.gravityKeys = static_cast<mdl::GravityKey*>(operator new(
        sizeof(mdl::GravityKey) * kGlobalKeyCapacity));
    s->GravityKeys() = ctx.gravityKeys;
    std::memset(ctx.gravityKeys, 0,
                sizeof(mdl::GravityKey) * kGlobalKeyCapacity);
    ctx.selfShadowKeys[0].mode =
        wrap->postProcessEnabled ? 1 : 0;                        // 0x454D0D
    ctx.selfShadowKeys[0].distance = 0.01125f;                       // flt_52A1D8
    s->state.selfShadowMode = 1;                // 0x454D3B
    s->state.selfShadowEnabled = 0;
    ctx.gravityKeys[0].noise = 10;
    ctx.gravityKeys[0].acceleration = 9.8000002f;
    ctx.gravityKeys[0].direction[1] = -1.0f;
    for (std::size_t i = 0; i < kGlobalKeyCapacity; ++i)        // 0x454D92
        ctx.cameraKeys[i].parentModel = -1;
    for (int i = 0; i < 255; ++i) {                             // 0x454D9A
        auto* const keys = static_cast<mdl::AccessoryKey*>(
            operator new(sizeof(mdl::AccessoryKey) * kGlobalKeyCapacity));
        accTracks[i] = keys;
        std::memset(keys, 0, sizeof(mdl::AccessoryKey) * kGlobalKeyCapacity);
        keys[0].visible = 1;
        keys[0].parentModel = -1;
        keys[0].scale = 1.0f;
        keys[0].opacity = 1.0f;
        accs[i] = nullptr;
    }

}


// ---- 0x454E07..0x455286: camera track read + camera misc + frame UI --
static void LoadSceneV2_CameraTrack(PmmV2LoadContext& ctx, int fd) {
    auto* const s = ctx.s;
    HWND const main = ctx.main;
    char* const text = ctx.text;
    mdl::CameraKey* const cameraKeys = ctx.cameraKeys;
    // ---- camera track read (0x454E07..0x455286) ---------------------------
    {
        // Record shape (0x28 head + parent dwords + interpolation + tail)
        // lives in pmm_io_common.hpp ReadPmmCameraKey.
        ReadPmmCameraKey<PmmStream::V2>(fd, cameraKeys[0]);
        std::int32_t cnt = 0;
        Rd(fd, &cnt, 4);                                        // 0x454F23
        for (std::int32_t i = 0; i < cnt; ++i) {
            std::int32_t frame = 0;
            Rd(fd, &frame, 4);
            ReadPmmCameraKey<PmmStream::V2>(fd, cameraKeys[frame]);
        }
    }
    // camera misc (0x4550F5..0x45518C)
    Rd(fd, &s->state.cameraPosition[0], 4);
    Rd(fd, &s->state.cameraPosition[1], 4);
    Rd(fd, &s->state.cameraPosition[2], 4);
    Rd(fd, &s->state.viewOffsetX, 4);
    Rd(fd, &s->state.viewOffsetY, 4);
    Rd(fd, &s->state.cameraDistance, 4);
    Rd(fd, &s->state.cameraPitch, 4);
    Rd(fd, &s->state.cameraYaw, 4);
    Rd(fd, &s->state.cameraRoll, 4);
    {
        unsigned char b = 0;
        Rd(fd, &b, 1);
        s->state.cameraPerspective = (b == 1) ? 1 : 0;
    }
    // frame UI (0x4551AD..0x455276)
    {
        const int frame =
            static_cast<int>(s->state.cameraFov);    // 0x9E1E8
        // 0x405 = TBM_SETPOS (0x4551B0: wParam 1 = redraw, lParam frame).
        SendMessageA(GetDlgItem(main, panel::kFovSlider), TBM_SETPOS, 1,
                     frame);
        SendMessageA(GetDlgItem(main, panel::kFovEdit), EM_SETSEL, 0,
                     GetWindowTextLengthA(GetDlgItem(main, panel::kFovEdit)));
        sprintf_s(text, 0x100, "%3d", frame);
        SendMessageA(GetDlgItem(main, panel::kFovEdit), EM_REPLACESEL, 0,
                     reinterpret_cast<LPARAM>(text));
        SendMessageA(GetDlgItem(main, panel::kPerspectiveCheckbox), BM_SETCHECK,
                     s->state.cameraPerspective != 0 ? 1 : 0, 0);
    }

}


// ---- 0x455286..0x45593E: light track read + light misc + rgb/direction
// slider UI --------------------------------------------------------------
static void LoadSceneV2_LightTrack(PmmV2LoadContext& ctx, int fd) {
    auto* const s = ctx.s;
    HWND const main = ctx.main;
    char* const text = ctx.text;
    mdl::LightKey* const lightKeys = ctx.lightKeys;
    // ---- light track read (0x455286..0x4554D3) ----------------------------
    {
        // Record shape (37 stream bytes) lives in pmm_io_common.hpp
        // ReadPmmLightKey - byte-identical to the v1 reader.
        ReadPmmLightKey(fd, lightKeys[0]);
        std::int32_t cnt = 0;
        Rd(fd, &cnt, 4);                                        // 0x455353
        for (std::int32_t i = 0; i < cnt; ++i) {
            std::int32_t frame = 0;
            Rd(fd, &frame, 4);
            ReadPmmLightKey(fd, lightKeys[frame]);
        }
    }
    // light misc + rgb/direction UI (0x4554D3..0x45592E)
    Rd(fd, s->LightColor() + 0, sizeof(float));
    Rd(fd, s->LightColor() + 1, sizeof(float));
    Rd(fd, s->LightColor() + 2, sizeof(float));
    Rd(fd, s->LightDirection() + 0, sizeof(float));
    Rd(fd, s->LightDirection() + 1, sizeof(float));
    Rd(fd, s->LightDirection() + 2, sizeof(float));
    {
        const int sliderIds[] = {455, 456, 457, 458, 459, 460};
        const int editIds[] = {461, 462, 463, 464, 465, 466};
        const float values[] = {
            s->LightColor()[0], s->LightColor()[1], s->LightColor()[2],
            s->LightDirection()[0], s->LightDirection()[1],
            s->LightDirection()[2]};
        for (int k = 0; k < 6; ++k) {
            const double scale = k < 3 ? 256.0 : 100.0;
            // 0x405 = TBM_SETPOS, same shape as the frame slider.
            SendMessageA(GetDlgItem(main, sliderIds[k]),
                         TBM_SETPOS, 1,
                         static_cast<int>(values[k] * scale));
            HWND item = GetDlgItem(main, editIds[k]);
            SendMessageA(item, EM_SETSEL, 0, GetWindowTextLengthA(item));
            if (k < 3)
                sprintf_s(text, 0x100, "%3d",
                          static_cast<int>(values[k] * 256.0));
            else
                sprintf_s(text, 0x100, "%+3.1f", values[k]);
            SendMessageA(item, EM_REPLACESEL, 0,
                         reinterpret_cast<LPARAM>(text));
        }
    }

}


// ---- 0x45593E..0x456617: accessory-shadow name list + accessory block
// (locate-and-retry dialog; the two abort paths return false) -----------
static bool LoadSceneV2_AccessoryBlock(PmmV2LoadContext& ctx, int fd) {
    auto* const s = ctx.s;
    HWND const main = ctx.main;
    PathResolutionWorkspace& paths = *ctx.paths;
    D3DRenderer* const wrap = ctx.wrap;
    mdl::AccessoryRecord** const accs = ctx.accs;
    mdl::AccessoryKey** const accTracks = ctx.accTracks;
    unsigned char** const slots = ctx.slots;
    char* const text = ctx.text;
    char* const mbPath = ctx.mbPath;
    wchar_t* const wideTmp = ctx.wideTmp;
    wchar_t* const ofnFile = ctx.ofnFile;
    wchar_t* const ofnTitle = ctx.ofnTitle;
    // ---- light-misc tail (0x45593E..0x45595E) -----------------------------
    Rd(fd, &s->SelectedAccessorySlot(), 1);
    Rd(fd, &s->DisplayObjectListScrollPosition(), 4);
    if (s->SelectedAccessorySlot() >= kModelSlotCount) {
        _close(fd);
        ResetAppState(s);
        HandleWindowSize(s);
        return false;
    }

    // ---- accessory block (0x455955..0x456617) ------------------------------
    SendMessageA(GetDlgItem(main, panel::kAccessoryCombo), CB_RESETCONTENT, 0, 0);
    SendMessageA(GetDlgItem(main, panel::kAttachBoneCombo), CB_RESETCONTENT, 0, 0);
    unsigned char accCount = 0;
    Rd(fd, &accCount, 1);                                       // 0x455999
    EnableMenuItem(GetMenu(main), 0xF9, accCount == 0 ? 1 : 0);
    LogPmmModelStage(fd, "accessory-count", accCount, 0,
                     s->OverlayVertices());
    for (unsigned char i = 0; i < accCount; ++i) {
        Rd(fd, text, 0x64);                                     // 0x4559EB
        SendMessageA(GetDlgItem(main, panel::kAccessoryCombo), CB_ADDSTRING, 0,
                     reinterpret_cast<LPARAM>(text));
    }
    char accName[0x64];
    for (unsigned char i = 0; i < accCount; ++i) {
        unsigned char accSlot = 0;
        Rd(fd, &accSlot, 1);                                    // 0x455A3F
        if (accSlot >= kModelSlotCount) {
            _close(fd);
            ResetAppState(s);
            HandleWindowSize(s);
            return false;
        }
        auto* acc = static_cast<mdl::AccessoryRecord*>(operator new(
            sizeof(mdl::AccessoryRecord)));
        if (acc != nullptr) IdentityCtor(acc);
        accs[accSlot] = acc;
        std::memset(accs[accSlot], 0, sizeof(mdl::AccessoryRecord));
        InitAccessoryRecord(accs[accSlot]);                             // 0x4C4760
        Rd(fd, accName, 0x64);                                  // 0x455ABE
        Rd(fd, mbPath, 0x100);                                  // 0x455AD1
        ResolveAnsiUserFile(reinterpret_cast<unsigned char*>(wrap),
                            mbPath, wideTmp, 0x100,
                            paths);
        if (!LoadAccessoryObject(s, accs[accSlot], wideTmp)) {  // 0x4C5F40
            if (s->EnglishUI() != 0)
                sprintf_s(text, 0x100,
                          "Cannot open file:%s.\n\nPlease select accessory "
                          "of %s.",
                          accName, accName);
            else
                sprintf_s(text, 0x100, kJpCannotOpenAcc, accName, accName);
            MessageBoxA(main, text,
                        s->EnglishUI() != 0 ? "open file" : kJpOpenCaption,
                        0);
            SetCurrentDirectoryW(reinterpret_cast<const wchar_t*>(
                s->state.exeDir));
            swprintf_s(ofnFile, 0x100, L"%s", ofnFile);         // quirk
            OPENFILENAMEW ofn;
            std::memset(&ofn, 0, sizeof(ofn));
            ofn.lStructSize = sizeof(ofn);
            ofn.hwndOwner =
                s->state.floatingWindow
                    ? reinterpret_cast<HWND>(
                          s->state.floatingWindow)
                    : main;
            ofn.lpstrFilter =
                s->EnglishUI() != 0 ? kWFilterAccEn : kWFilterAccJp;
            ofn.lpstrFile = ofnFile;
            ofn.nMaxFile = 0x100;
            ofn.Flags = OFN_FILEMUSTEXIST;
            ofn.lpstrInitialDir =
                (GetMenuState(GetMenu(main), 0x12D, 0) & 8)
                    ? reinterpret_cast<LPCWSTR>(s->state.dirAccs)
                    : kWUserAcc;                     // 0x7FF7CB49F294
            ofn.lpstrDefExt = L"x";
            ofn.nMaxFileTitle = 0x100;
            ofn.lpstrFileTitle = ofnTitle;
            ofn.lpstrTitle =
                s->EnglishUI() != 0
                    ? L"open file"
                    : reinterpret_cast<LPCWSTR>(kWOpenFile);
            if (!GetOpenFileNameW(&ofn)) {                      // 0x456779
                _close(fd);
                ResetAppState(s);                               // 0x44E540
                HandleWindowSize(s);                            // 0x443300
                return false;
            }
            if (GetMenuState(GetMenu(main), 0x12D, 0) & 8) {
                wchar_t* d = ExtractDirFromPath(
                    paths.projectDirectory,
                    ofnFile);
                wcscpy_s(reinterpret_cast<wchar_t*>(s->state.dirAccs),
                         0x3E8, d);                  // 0x7FF7CB49F33C
            }
            if (!LoadAccessoryObject(s, accs[accSlot], ofnFile)) {
                _close(fd);                                     // 0x456764
                ResetAppState(s);
                return false;
            }
        }
        mdl::AccessoryRecord& accessory =
            *mdl::Accessory(accs[accSlot]);
        Rd(fd, &accessory.order, 1);                             // 0x455D93
        strcpy_s(accessory.name, sizeof accessory.name, accName);
        // accessory track record 0 + sparse keys (0x455DCA..0x4562E5);
        // record shape (55 stream bytes, transparency quirk included) lives
        // in pmm_io_common.hpp ReadPmmAccessoryKey - byte-identical to v1.
        auto* const accessoryKeys =
            reinterpret_cast<mdl::AccessoryKey*>(accTracks[accSlot]);
        ReadPmmAccessoryKey(fd, accessoryKeys[0]);
        {
            std::int32_t cnt = 0;
            Rd(fd, &cnt, 4);                                    // 0x455FC6
            for (std::int32_t k = 0; k < cnt; ++k) {
                std::int32_t frame = 0;
                Rd(fd, &frame, 4);
                ReadPmmAccessoryKey(fd, accessoryKeys[frame]);
            }
        }
        {
            unsigned char b = 0;                                // 0x4562E5
            Rd(fd, &b, 1);
            accessory.visible = b & 1;
            accessory.opacity =
                static_cast<float>(100 - (b >> 1)) / 100.0f;
        }
        Rd(fd, &accessory.parentModel, 4);
        Rd(fd, &accessory.parentBone, 4);
        Rd(fd, &accessory.rotation[0], 4);
        Rd(fd, &accessory.rotation[1], 4);
        Rd(fd, &accessory.rotation[2], 4);
        Rd(fd, &accessory.scale, 4);
        Rd(fd, &accessory.position[0], 4);
        Rd(fd, &accessory.position[1], 4);
        Rd(fd, &accessory.position[2], 4);
        {
            unsigned char b = 0;
            Rd(fd, &b, 1);
            accessory.shadowEnabled = (b == 1) ? 1 : 0;
            Rd(fd, &b, 1);
            accessory.additiveBlend = (b == 1) ? 1 : 0;
        }
        LogPmmModelStage(fd, "accessory-record-complete", i, accSlot,
                         s->OverlayVertices());
    }
    // current accessory selection UI (0x4564DD..0x456617)
    {
        const unsigned char cur = s->SelectedAccessorySlot();
        if (accs[cur] != nullptr) {
            mdl::AccessoryRecord& accessory = *mdl::Accessory(accs[cur]);
            SendMessageA(GetDlgItem(main, panel::kAccessoryCombo), CB_SETCURSEL,
                         accessory.order, 0);
            const std::int32_t parentSlot = accessory.parentModel;
            if (parentSlot >= 0 && parentSlot < kModelSlotCount &&
                slots[parentSlot] != nullptr &&
                mdl::Mdl(slots[parentSlot])->boneCount > 0) {
                for (std::int32_t b = 0;
                     b < static_cast<std::int32_t>(
                             mdl::Mdl(slots[parentSlot])->boneCount); ++b) {
                    const mdl::BoneRecord& bone =
                        mdl::Bones(slots[parentSlot])[b];
                    if (bone.type < mdl::BoneType::InertTip ||
                        bone.type == mdl::BoneType::FixedAxis)
                        SendMessageA(GetDlgItem(main, panel::kAttachBoneCombo), CB_ADDSTRING,
                                     0,
                                     reinterpret_cast<LPARAM>(bone.name));
                }
            }
            SyncAccessoryEditPanel(s);                                       // 0x456608
        }
    }

    return true;
}


// ---- 0x456617..0x45776E: config block + label_708 menu/checkbox run
// (the goto stays inside this function) ---------------------------------
static void LoadSceneV2_ConfigBlock(PmmV2LoadContext& ctx, int fd) {
    auto* const s = ctx.s;
    HWND const main = ctx.main;
    PathResolutionWorkspace& paths = *ctx.paths;
    D3DRenderer* const wrap = ctx.wrap;
    unsigned char** const slots = ctx.slots;
    mdl::AccessoryRecord** const accs = ctx.accs;
    char* const text = ctx.text;
    char* const mbPath = ctx.mbPath;
    // ---- config block head (0x456617..0x4566CB) ---------------------------
    Rd(fd, &s->state.currentFrame, 4);
    Rd(fd, &s->state.timelineStartFrame, 4);
    Rd(fd, &s->state.lastRegisteredFrame, 4);
    {
        HWND item = GetDlgItem(main, panel::kCurrentFrameEdit);
        SendMessageA(item, EM_SETSEL, 0, GetWindowTextLengthA(item));
        sprintf_s(text, 0x100, "%d",
                  s->state.currentFrame);
        SendMessageA(item, EM_REPLACESEL, 0,
                     reinterpret_cast<LPARAM>(text));
    }
    PostModelReload2(s);                                        // 0x40D940
    PostLanguageSweep(s);                                       // 0x42F1E0
    HandleWindowSize(s);                                        // 0x443300
    ApplyModelComboSelection(s);

    // register radios (0x4566DA..0x456924)
    std::int32_t savedEditMode = 0;
    Rd(fd, &savedEditMode, 4);                                  // 0x4566DA
    s->EditMode() = static_cast<ViewportEditMode>(savedEditMode);
    {
        const ViewportEditMode mode = s->EditMode();
        switch (mode) {
            case ViewportEditMode::Bone:
                SendMessageA(GetDlgItem(main, panel::kBoneSelectRadio), BM_SETCHECK, 1, 0);
                SendMessageA(GetDlgItem(main, panel::kBoxSelectRadio), BM_SETCHECK, 0, 0);
                SendMessageA(GetDlgItem(main, panel::kBoneMoveRadio), BM_SETCHECK, 0, 0);
                SendMessageA(GetDlgItem(main, panel::kBoneRotateRadio), BM_SETCHECK, 0, 0);
                break;
            case ViewportEditMode::BoneBox:
                SendMessageA(GetDlgItem(main, panel::kBoneSelectRadio), BM_SETCHECK, 0, 0);
                SendMessageA(GetDlgItem(main, panel::kBoxSelectRadio), BM_SETCHECK, 1, 0);
                SendMessageA(GetDlgItem(main, panel::kBoneMoveRadio), BM_SETCHECK, 0, 0);
                SendMessageA(GetDlgItem(main, panel::kBoneRotateRadio), BM_SETCHECK, 0, 0);
                break;
            case ViewportEditMode::None:
                // 0x4567C3..0x45682C: case 2 unchecks ALL four radios.
                SendMessageA(GetDlgItem(main, panel::kBoneSelectRadio), BM_SETCHECK, 0, 0);
                SendMessageA(GetDlgItem(main, panel::kBoxSelectRadio), BM_SETCHECK, 0, 0);
                SendMessageA(GetDlgItem(main, panel::kBoneMoveRadio), BM_SETCHECK, 0, 0);
                SendMessageA(GetDlgItem(main, panel::kBoneRotateRadio), BM_SETCHECK, 0, 0);
                break;
            case ViewportEditMode::Camera:
                // 0x456862..0x45674D: case 3 checks 0x1ED (493) only.
                SendMessageA(GetDlgItem(main, panel::kBoneSelectRadio), BM_SETCHECK, 0, 0);
                SendMessageA(GetDlgItem(main, panel::kBoxSelectRadio), BM_SETCHECK, 0, 0);
                SendMessageA(GetDlgItem(main, panel::kBoneMoveRadio), BM_SETCHECK, 0, 0);
                SendMessageA(GetDlgItem(main, panel::kBoneRotateRadio), BM_SETCHECK, 1, 0);
                break;
            case ViewportEditMode::Light:
                SendMessageA(GetDlgItem(main, panel::kBoneSelectRadio), BM_SETCHECK, 0, 0);
                SendMessageA(GetDlgItem(main, panel::kBoxSelectRadio), BM_SETCHECK, 0, 0);
                SendMessageA(GetDlgItem(main, panel::kBoneMoveRadio), BM_SETCHECK, 1, 0);
                SendMessageA(GetDlgItem(main, panel::kBoneRotateRadio), BM_SETCHECK, 0, 0);
                break;
            default:
                break;  // 0x4568B1: straight to LABEL_668
        }
    }
    {
        unsigned char b = 0;
        Rd(fd, &b, 1);                                          // 0x45692B
        if (b == 1) {
            SendMessageA(GetDlgItem(main, panel::kCameraRefModelCheckbox), BM_SETCHECK, 1, 0);
            SendMessageA(GetDlgItem(main, panel::kCameraRefBoneCheckbox), BM_SETCHECK, 0, 0);
        } else if (b == 2) {
            SendMessageA(GetDlgItem(main, panel::kCameraRefModelCheckbox), BM_SETCHECK, 0, 0);
            SendMessageA(GetDlgItem(main, panel::kCameraRefBoneCheckbox), BM_SETCHECK, 1, 0);
        } else {
            SendMessageA(GetDlgItem(main, panel::kCameraRefModelCheckbox), BM_SETCHECK, 0, 0);
            SendMessageA(GetDlgItem(main, panel::kCameraRefBoneCheckbox), BM_SETCHECK, 0, 0);
        }
        Rd(fd, &b, 1);                                          // 0x4569E9
        s->state.playbackLoopEnabled = b ? 1 : 0;
        SendMessageA(GetDlgItem(main, 411), BM_SETCHECK, b ? 1 : 0, 0);
        Rd(fd, &b, 1);                                          // 0x456A41
        s->state.playbackReturnsToStartFrame = b ? 1 : 0;
        SendMessageA(GetDlgItem(main, 413), BM_SETCHECK, b ? 1 : 0, 0);
        Rd(fd, &b, 1);                                          // 0x456A99
        s->state.playbackStartsAtCurrentFrame = b ? 1 : 0;
        SendMessageA(GetDlgItem(main, 414), BM_SETCHECK, b ? 1 : 0, 0);
        std::int32_t playStartFrame = 0, playStopFrame = 0;
        Rd(fd, &playStartFrame, 4);                                         // 0x456AF4
        {
            HWND item = GetDlgItem(main, panel::kPlayStartFrameEdit);
            SendMessageA(item, EM_SETSEL, 0, GetWindowTextLengthA(item));
            sprintf_s(text, 0x100, "%d", playStartFrame);
            SendMessageA(item, EM_REPLACESEL, 0,
                         reinterpret_cast<LPARAM>(text));
        }
        Rd(fd, &playStopFrame, 4);                                         // 0x456B83
        {
            HWND item = GetDlgItem(main, panel::kPlayStopFrameEdit);
            SendMessageA(item, EM_SETSEL, 0, GetWindowTextLengthA(item));
            sprintf_s(text, 0x100, "%d", playStopFrame);
            SendMessageA(item, EM_REPLACESEL, 0,
                         reinterpret_cast<LPARAM>(text));
        }
        ClearTimelineAndCurveDCs(s);                                           // 0x456C09
        Rd(fd, &b, 1);                                          // 0x456C16
        s->state.waveEnabled = b ? 1 : 0;
        Rd(fd, mbPath, 0x100);                                  // 0x456C43
        ResolveAnsiUserFile(reinterpret_cast<unsigned char*>(wrap),
                            mbPath,
                            reinterpret_cast<wchar_t*>(s->state.wavPath),
                            0x100, paths);
        if (s->state.waveEnabled != 0)
            LoadWaveFile(s);   // 0x418500 (app-taking; path at +0xD0)
        std::int32_t w1 = 0, w2 = 0, w3 = 0;
        Rd(fd, &w1, 4);                                         // 0x456C8C
        Rd(fd, &w2, 4);
        Rd(fd, &w3, 4);
        Rd(fd, mbPath, 0x100);                                  // 0x456CBF
        ResolveAnsiUserFile(reinterpret_cast<unsigned char*>(wrap),
                            mbPath,
                            s->AviBackgroundPath(),
                            0x100, paths);
        if (mbPath[0] != 0)
            LoadAviFile(s);    // 0x433250 (app-taking; path at +0x9E1EC)
        // AVI block (0x456D0D..0x456E25)
        if (s->AviBackgroundEnabled() == 1) {
            Rd(fd, &s->AviBackgroundEnabled(), 4);
            s->AviOffsetX() = w1;
            s->AviOffsetY() = w2;
            std::memcpy(&s->AviScale(), &w3, sizeof w3);
            if (s->AviBackgroundEnabled() == 1) {
                CheckMenuItem(GetMenu(main), 0xD8, 8);
                AviBgOverlayRefresh(s);
            } else {
                CheckMenuItem(GetMenu(main), 0xD8, 0);
            }
        } else {
            Rd(fd, &s->AviBackgroundEnabled(), 4);
            s->AviBackgroundEnabled() = 0;
            CheckMenuItem(GetMenu(main), 0xD8, 0);
            if (s->AviFrameReader() != nullptr) {
                AVIStreamGetFrameClose(s->AviFrameReader());
                s->AviFrameReader() = nullptr;
            }
            if (s->AviStream() != nullptr) {
                AVIStreamRelease(s->AviStream());
                s->AviStream() = nullptr;
            }
            if (s->AviFile() != nullptr) {
                AVIFileRelease(s->AviFile());
                s->AviFile() = nullptr;
            }
        }
        std::int32_t p1 = 0, p2 = 0, p3 = 0;
        Rd(fd, &p1, 4);                                         // 0x456E25
        Rd(fd, &p2, 4);
        Rd(fd, &p3, 4);
        Rd(fd, mbPath, 0x100);                                  // 0x456E58
        ResolveAnsiUserFile(reinterpret_cast<unsigned char*>(wrap),
                            mbPath,
                            s->PictureBackgroundPath(),
                            0x100, paths);
        s->PictureBackgroundEnabled() = 0;
        if (mbPath[0] != 0) LoadBackgroundPicture(s);   // 0x4337A0
        if (s->PictureBackgroundEnabled() != 0) {
            Rd(fd, &b, 1);                                      // 0x456EAF
            s->PictureBackgroundEnabled() = b ? 1 : 0;
            s->PictureOffsetX() = p1;
            s->PictureOffsetY() = p2;
            std::memcpy(&s->PictureScale(), &p3, sizeof p3);
            if (s->PictureBackgroundEnabled() != 0) {
                CheckMenuItem(GetMenu(main), 0xE9, 8);
                PicBgOverlayRefresh(s);
            } else {
                CheckMenuItem(GetMenu(main), 0xE9, 0);
            }
        } else {
            Rd(fd, &b, 1);                                      // 0x456F29
            s->PictureBackgroundEnabled() = 0;
            CheckMenuItem(GetMenu(main), 0xE9, 0);
        }
    }
    {
        unsigned char b = 0;
        const HWND owner =
            s->state.floatingWindow
                ? reinterpret_cast<HWND>(s->state.floatingWindow)
                : main;
        Rd(fd, &b, 1);                                          // 0x456F53
        if (b != 0) {
            s->FpsOverlayEnabled() = 1;
            CheckMenuItem(GetMenu(main), 0xD3, 8);
            SendMessageA(GetDlgItem(owner, panel::kInfoCheckbox), BM_SETCHECK, 1, 0);
        } else {
            s->FpsOverlayEnabled() = 0;
            CheckMenuItem(GetMenu(main), 0xD3, 0);
            SendMessageA(GetDlgItem(owner, panel::kInfoCheckbox), BM_SETCHECK, 0, 0);
        }
        Rd(fd, &b, 1);                                          // 0x456FEC
        if (b != 0) {
            s->GroundGridEnabled() = 1;
            CheckMenuItem(GetMenu(main), 0xD7, 8);
            SendMessageA(GetDlgItem(owner, panel::kCoordAxisCheckbox), BM_SETCHECK, 1, 0);
        } else {
            s->GroundGridEnabled() = 0;
            CheckMenuItem(GetMenu(main), 0xD7, 0);
            SendMessageA(GetDlgItem(owner, panel::kCoordAxisCheckbox), BM_SETCHECK, 0, 0);
        }
        Rd(fd, &b, 1);                                          // 0x45707D
        s->state.groundShadowEnabled = b ? 1 : 0;                // 0x918
        CheckMenuItem(GetMenu(main), 0xDD, b ? 8 : 0);
        Rd(fd, &s->state.fpsLimit, 4); // 0x4570CF
        {
            const float v = s->state.fpsLimit;
            if (v == 1000.0f) {
                CheckMenuItem(GetMenu(main), 0xEB, 0);
                CheckMenuItem(GetMenu(main), 0xEC, 0);
                CheckMenuItem(GetMenu(main), 0xEA, 8);
            } else if (v == 30.0f) {
                CheckMenuItem(GetMenu(main), 0xEA, 0);
                CheckMenuItem(GetMenu(main), 0xEC, 0);
                CheckMenuItem(GetMenu(main), 0xEB, 8);
            } else {
                CheckMenuItem(GetMenu(main), 0xEB, 0);
                CheckMenuItem(GetMenu(main), 0xEA, 0);
                CheckMenuItem(GetMenu(main), 0xEC, 8);
            }
        }
        std::int32_t sm = 0;
        Rd(fd, &sm, 4);                                         // 0x4571CF
        // The original reads the four bytes directly into app+0x9EB84.
        // Keeping the value only in a local makes the menu look correct but
        // leaves the renderer in mode 0, so accessories using screen.bmp see
        // a null screen texture instead of the previous-frame capture.
        s->state.captureMode = static_cast<std::uint32_t>(sm);
        switch (sm) {
            case 0:
                CheckMenuItem(GetMenu(main), 0xF3, 8);
                CheckMenuItem(GetMenu(main), 0xF4, 0);
                CheckMenuItem(GetMenu(main), 0xF5, 0);
                CheckMenuItem(GetMenu(main), 0xF6, 0);
                break;
            case 1:
                CheckMenuItem(GetMenu(main), 0xF3, 0);
                CheckMenuItem(GetMenu(main), 0xF4, 8);
                CheckMenuItem(GetMenu(main), 0xF5, 0);
                CheckMenuItem(GetMenu(main), 0xF6, 0);
                break;
            case 2:
                CheckMenuItem(GetMenu(main), 0xF3, 0);
                CheckMenuItem(GetMenu(main), 0xF4, 0);
                CheckMenuItem(GetMenu(main), 0xF5, 8);
                CheckMenuItem(GetMenu(main), 0xF6, 0);
                break;
            default:
                CheckMenuItem(GetMenu(main), 0xF3, 0);
                CheckMenuItem(GetMenu(main), 0xF4, 0);
                CheckMenuItem(GetMenu(main), 0xF5, 0);
                CheckMenuItem(GetMenu(main), 0xF6, 8);
                break;
        }
        // physics defaults + combos (0x45736C..0x4573FE)
        s->state.gravityNoiseEnabled = 0;
        s->state.gravityNoise = 10;
        s->state.gravityMagnitude = 9.8000002f;
        s->state.rigidBodyDisplayEnabled = 0;
        s->state.gravityX = 0.0f;
        s->state.gravityY = -1.0f;
        s->state.gravityZ = 0.0f;
        s->state.modelOutlineColorRed = 0;
        s->state.modelOutlineColorGreen = 0;
        s->state.modelOutlineColorBlue = 0;
        SendMessageA(GetDlgItem(main, panel::kMainComboNormal), CB_SETCURSEL, 0, 0);
        SendMessageA(GetDlgItem(main, panel::kBoneRegisterCombo), CB_SETCURSEL, 0, 0);
        // 0xA0B20 + shadow distance copies (0x45740E..0x457443)
        Rd(fd, &s->state.accessoryRenderSplitOrder, 4);
        Rd(fd, &s->ProjectedShadowAmbientIntensity(), 4);
        s->SetProjectedShadowAmbientRgb(s->ProjectedShadowAmbientIntensity());
        {
            unsigned char* m = slots[s->SelectedModelSlot()];
            if (m != nullptr && s->state.optflag[0] == 0 &&
                mdl::Mdl(m)->postLoadFlag2 != 0)
                SendMessageA(GetDlgItem(main, panel::kAddBlendCheckbox), BM_SETCHECK, 1, 0);
        }
        {
            const unsigned char cur = s->SelectedAccessorySlot();
            if (accs[cur] != nullptr &&
                mdl::Accessory(accs[cur])->additiveBlend != 0)
                SendMessageA(GetDlgItem(main, panel::kAccessoryAddBlendCheckbox), BM_SETCHECK, 1, 0);
        }
        Rd(fd, &b, 1);                                          // 0x4574DF
        if (b == 1) {
            s->state.projectedShadowBlendEnabled = 1;
            CheckMenuItem(GetMenu(main), 0xFE, 8);
        } else {
            s->state.projectedShadowBlendEnabled = 0;
        }
        unsigned char pmode = 0;
        Rd(fd, &pmode, 1);                                      // 0x457523
        // 0x457523 targets app+0xA0CC4 itself.  The upper bytes are already
        // zero in the original/default state; assign the complete dword here
        // so a previously loaded scene cannot leak stale physics mode bits.
        s->PlaybackPhysicsMode() = pmode;
        switch (pmode) {
            case 0:
                CheckMenuItem(GetMenu(main), 0x10D, 0);
                CheckMenuItem(GetMenu(main), 0x109, 0);
                CheckMenuItem(GetMenu(main), 0x10E, 0);
                CheckMenuItem(GetMenu(main), 0x110, 8);
                break;
            case 1:
                CheckMenuItem(GetMenu(main), 0x10D, 0);
                CheckMenuItem(GetMenu(main), 0x109, 8);
                CheckMenuItem(GetMenu(main), 0x10E, 0);
                CheckMenuItem(GetMenu(main), 0x110, 0);
                break;
            case 2:
                CheckMenuItem(GetMenu(main), 0x10D, 8);
                CheckMenuItem(GetMenu(main), 0x109, 0);
                CheckMenuItem(GetMenu(main), 0x10E, 0);
                CheckMenuItem(GetMenu(main), 0x110, 0);
                break;
            default:
                CheckMenuItem(GetMenu(main), 0x10D, 0);
                CheckMenuItem(GetMenu(main), 0x109, 0);
                CheckMenuItem(GetMenu(main), 0x10E, 8);
                CheckMenuItem(GetMenu(main), 0x110, 0);
                break;
        }
        // x64 twin: 255-slot walk (0xFF counter at 0x7FF7CB4A1131).
        for (int i = 0; i < kModelSlotCount; ++i)               // 0x4576C7
            if (slots[i] != nullptr) ModelKinematicSync(slots[i]);
        // physics reads (0x457713..0x45776E)
        Rd(fd, &s->state.gravityMagnitude, 4);
        Rd(fd, &s->state.gravityNoise, 4);
        Rd(fd, &s->state.gravityX, 4);
        Rd(fd, &s->state.gravityY, 4);
        Rd(fd, &s->state.gravityZ, 4);
        Rd(fd, &b, 1);                                          // 0x45775C
#ifdef MIKUDANCESTUDIO_DIAG
        if (std::getenv("MIKUDANCESTUDIO_TRACE_NOISE_OFF") != nullptr)
            std::fprintf(stderr, "noise byte file offset=%ld value=%d\n",
                         static_cast<long>(_tell(fd)), static_cast<int>(b));
#endif
        s->state.gravityNoiseEnabled = (b == 1) ? 1 : 0;
    }
}


// ---- 0x45777C..0x458112: selection + self-shadow tracks, model color
// sweep, camera parent re-register, 16 config dwords, optional per-model
// 0x4CCF0 block, _close --------------------------------------------------
static void LoadSceneV2_PhysicsTracksAndClose(PmmV2LoadContext& ctx, int fd,
                                            unsigned char modelCount) {
    auto* const s = ctx.s;
    HWND const main = ctx.main;
    unsigned char** const slots = ctx.slots;
    char* const lbText = ctx.lbText;
    mdl::GravityKey* const gravityKeys = ctx.gravityKeys;
    mdl::SelfShadowKey* const selfShadowKeys = ctx.selfShadowKeys;
    // gravity/physics track read (0x45777C..0x4579FA).  This is the
    // 36-byte app+0x380 table; using the 24-byte app+0x37C
    // self-shadow table) cross-contaminates both tracks and overruns its
    // record shape.
    {
        LogPmmModelStage(fd, "selection-track", 0, 0, gravityKeys);
        // Record shape (34 stream bytes) lives in pmm_io_common.hpp
        // ReadPmmGravityKey (v2-only track).
        ReadPmmGravityKey(fd, gravityKeys[0]);
        std::int32_t cnt = 0;
        Rd(fd, &cnt, 4);                                        // 0x457861
        LogPmmModelStage(fd, "selection-count", cnt, 0, gravityKeys);
        for (std::int32_t i = 0; i < cnt; ++i) {
            std::int32_t frame = 0;
            Rd(fd, &frame, 4);
            LogPmmModelStage(fd, "selection-frame", i, frame,
                             &gravityKeys[frame]);
            ReadPmmGravityKey(fd, gravityKeys[frame]);
        }
    }
    {
        unsigned char b = 0;                                    // 0x4579FA
        Rd(fd, &b, 1);
        s->state.selfShadowMode = b;
        s->state.selfShadowEnabled = (b != 0) ? 1 : 0;
    }
    Rd(fd, &s->state.physicsInterval, 4);   // 0x457A22
    selfShadowKeys[0].mode =
        static_cast<unsigned char>(s->state.selfShadowMode);
    selfShadowKeys[0].distance = s->state.physicsInterval;
    // self-shadow track read (0x457A4F..0x457BDE), 24-byte app+0x37C;
    // record shape lives in pmm_io_common.hpp ReadPmmSelfShadowKey.
    {
        ReadPmmSelfShadowKey(fd, selfShadowKeys[0]);
        std::int32_t cnt = 0;
        Rd(fd, &cnt, 4);                                        // 0x457AD4
        for (std::int32_t i = 0; i < cnt; ++i) {
            std::int32_t frame = 0;
            Rd(fd, &frame, 4);
            ReadPmmSelfShadowKey(fd, selfShadowKeys[frame]);
        }
    }
    // model color sweep (0x457BEE..0x457C76)
    Rd(fd, &s->state.modelOutlineColorRed, 4);
    Rd(fd, &s->state.modelOutlineColorGreen, 4);
    Rd(fd, &s->state.modelOutlineColorBlue, 4);
    if (s->state.modelOutlineColorRed != 0 ||
        s->state.modelOutlineColorGreen != 0 ||
        s->state.modelOutlineColorBlue != 0) {
        // x64 twin: 255-slot color sweep (0xFF counter at 0x7FF7CB4A1792).
        for (int i = 0; i < kModelSlotCount; ++i)
            if (slots[i] != nullptr)
                SetModelColor(reinterpret_cast<MMDApp*>(slots[i]),
                          s->state.modelOutlineColorRed,
                          s->state.modelOutlineColorGreen,
                          s->state.modelOutlineColorBlue);
    }
    {
        unsigned char b = 0;
        Rd(fd, &b, 1);                                          // 0x457C80
        s->state.blackBackgroundEnabled = b ? 1 : 0;
        CheckMenuItem(GetMenu(main), 0x11A, b ? 8 : 0);
    }
    Rd(fd, &s->state.cameraParentModel, 4);        // 0x457CD2
    Rd(fd, &s->state.cameraParentBone, 4);
    if (s->state.cameraParentModel >= 0) {
        SendMessageA(GetDlgItem(main, panel::kMainComboNormal), CB_SETCURSEL,
                     mdl::Mdl(slots[s->state.cameraParentModel])->comboSelIndex,
                     0);
    }
    RefillBoneRegisterCombo(s, s->state.cameraParentModel);       // 0x457D27
    if (s->state.cameraParentModel >= 0) {
        const LRESULT n = SendMessageA(GetDlgItem(main, panel::kBoneRegisterCombo), CB_GETCOUNT, 0,
                                       0);
        unsigned char* m = slots[s->state.cameraParentModel];
        for (LRESULT i = 0; i < n; ++i) {
            SendMessageA(GetDlgItem(main, panel::kBoneRegisterCombo), CB_GETLBTEXT,
                         static_cast<WPARAM>(i),
                         reinterpret_cast<LPARAM>(lbText));
            const mdl::BoneRecord& bone = mdl::Bones(m)[
                s->state.cameraParentBone];
            if (strcmp(lbText, bone.name) == 0)
                SendMessageA(GetDlgItem(main, panel::kBoneRegisterCombo), CB_SETCURSEL,
                             static_cast<WPARAM>(i), 0);
        }
    }
    // 16 config dwords (0x457E79..0x457F60)
    Rd(fd, &s->state.cameraAttachmentBasis, 4);      // 0xA0438
    for (int i = 0; i < 15; ++i)
        Rd(fd, &s->state.cameraAttachmentBasis[1 + i], 4);      // 0xA043C..
    {
        unsigned char b = 0;
        Rd(fd, &b, 1);                                          // 0x457F6D
        if (b == 1) {
            s->state.followCameraEnabled = 1;       // 0x9ED98
            CheckMenuItem(GetMenu(main), 0xF7, 8);
            SendMessageA(GetDlgItem(main, panel::kFollowCameraCheckbox), BM_SETCHECK, 1, 0);
        } else {
            s->state.followCameraEnabled = 0;
            CheckMenuItem(GetMenu(main), 0xF7, 0);
            SendMessageA(GetDlgItem(main, panel::kFollowCameraCheckbox), BM_SETCHECK, 0, 0);
        }
        Rd(fd, &b, 1);                                          // 0x457FFD
        s->state.cameraAttachmentTransformSuppressed = (b == 1) ? 1 : 0;
        Rd(fd, &b, 1);                                          // 0x458018
        btRigidBody* const groundBody = s->Physics()->groundBody;
        if (b == 1) {
            CheckMenuItem(GetMenu(main), 0x11D, 8);
            s->state.floorVisible = 1;
            groundBody->setDeactivationTime(1.0f);
        } else {
            CheckMenuItem(GetMenu(main), 0x11D, 0);
            s->state.floorVisible = 0;
            groundBody->setDeactivationTime(-1.0f);
        }
    }
    {
        unsigned char b = 0;
        Rd(fd, &ctx.maxFrame, 4);                                   // 0x458098
        const int got = Rd(fd, &b, 1);                          // 0x4580A5
        if (got > 0 && b == 1) {
            for (unsigned char i = 0; i < modelCount; ++i) {
                unsigned char mslot = 0;
                Rd(fd, &mslot, 1);                              // 0x4580DB
                Rd(fd,
                   &mikudancestudio::mdl::Mdl(slots[mslot])
                        ->frameRegistrationSelection,
                   sizeof(std::int32_t));
            }
        }
    }
    _close(fd);                                                 // 0x458112

}


// ---- 0x458120..0x458F53: success tail - shadow-mode gate, light
// direction to the physics scene, window title, record post-processing
// (remap or reference removal), key-chain integrity boxes, combo 434,
// frame edit, record array free, repaint -------------------------------
static void LoadSceneV2_SuccessTail(PmmV2LoadContext& ctx,
                                  PmmModelLoadWorkspace* workspaces,
                                  unsigned char modelCount) {
    auto* const s = ctx.s;
    HWND const main = ctx.main;
    D3DRenderer* const wrap = ctx.wrap;
    unsigned char** const slots = ctx.slots;
    mdl::AccessoryRecord** const accs = ctx.accs;
    mdl::AccessoryKey** const accTracks = ctx.accTracks;
    char* const text = ctx.text;
    char* const lbText = ctx.lbText;
    wchar_t* const wndText = ctx.wndText;
    mdl::CameraKey* const cameraKeys = ctx.cameraKeys;
    // ---- success tail (0x458120..0x458F53) --------------------------------
    if (wrap->postProcessEnabled != 0) {
        RefreshSelfShadowPanel(s);                                           // 0x45813E
    } else {
        s->state.selfShadowEnabled = 0;
        s->state.selfShadowMode = 0;
    }
    CheckMenuItem(GetMenu(main), 0x117,
                  s->state.selfShadowEnabled != 0 ? 8 : 0);
    {
        unsigned char* m = slots[s->SelectedModelSlot()];
        if (m != nullptr && s->state.optflag[0] == 0 &&
            mdl::Mdl(m)->toonFlag != 0)
            SendMessageA(GetDlgItem(main, panel::kShadowCheckbox), BM_SETCHECK, 1, 0);
    }
    {  // light direction into the physics scene (0x4581D1..0x4582B6)
        float dir[3] = {s->state.gravityX,
                        s->state.gravityY,
                        s->state.gravityZ};
        auto& d3dxApi = d3dx::Get();
        // Preserve the imported runtime's normalization rounding.
        d3dxApi.vec3Normalize(dir, dir);
        const float mag = s->state.gravityMagnitude;
        const float v[4] = {dir[0] * mag * 10.0f, dir[1] * mag * 10.0f,
                            dir[2] * mag * 10.0f, 0.0f};
        PhysicsScene* const physics = s->Physics();
        if (physics != nullptr && physics->world != nullptr) {
            physics->world->setGravity(btVector3(v[0], v[1], v[2]));
        }
    }
    swprintf_s(wndText, 0x100, kAppTitleFormat,
               reinterpret_cast<const wchar_t*>(s->state.envFileName));
    SetWindowTextW(main, wndText);

    // 内置 MMEffect：场景加载成功通知（EMM 自动加载）。
    mme::NotifyPmmLoaded(s);

    // record post-processing (0x458309..0x458996)
    for (unsigned char i = 0; i < modelCount; ++i) {
        PmmModelLoadWorkspace& workspace = workspaces[i];
        if (workspace.skipped == 0) {
            if (workspace.displaysMatch != 0) continue;  // table matched: identity map
            NameMapping* const boneMappings =
                workspace.boneNameMap;
            const auto remapBoneIndex = [&](std::int32_t sourceIndex) {
                const std::int32_t mapped =
                    boneMappings[sourceIndex].mappedIndex;
                return mapped < 0 ? 0 : mapped;
            };
            // remap display indices through the translation arrays
            for (int mi = 0; mi < kModelSlotCount; ++mi) {
                unsigned char* m = slots[mi];
                if (m == nullptr) continue;
                mdl::BoneOrderEntry* const selectors = mdl::BoneOrder(m);
                for (std::uint32_t r = 0; r < mdl::BoneOrderCount(m); ++r) {
                    mdl::BoneOrderEntry& selector = selectors[r];
                    if (selector.linkedModel == workspace.modelSlot) {
                        selector.linkedBone =
                            remapBoneIndex(selector.linkedBone);
                    }
                }
                mdl::DisplayKey* const displayKeys = mdl::DisplayKeys(m);
                for (std::size_t keyIndex = 0;
                     keyIndex < mdl::kDisplayKeyCapacity; ++keyIndex) {
                    mdl::BoneReference* const references =
                        mdl::SelectorStates(displayKeys[keyIndex]);
                    for (std::uint32_t k = 0; k < mdl::BoneOrderCount(m); ++k) {
                        mdl::BoneReference& reference = references[k];
                        if (reference.modelIndex == workspace.modelSlot) {
                            reference.boneIndex =
                                remapBoneIndex(reference.boneIndex);
                        }
                    }
                }
            }
            if (s->state.cameraParentModel == workspace.modelSlot) {
                unsigned char* m = slots[workspace.modelSlot];
                s->state.cameraParentBone = remapBoneIndex(
                    s->state.cameraParentBone);
                const LRESULT n = SendMessageA(GetDlgItem(main, panel::kBoneRegisterCombo),
                                               CB_GETCOUNT, 0, 0);
                for (LRESULT k = 0; k < n; ++k) {
                    SendMessageA(GetDlgItem(main, panel::kBoneRegisterCombo), CB_GETLBTEXT,
                                 static_cast<WPARAM>(k),
                                 reinterpret_cast<LPARAM>(lbText));
                    const mdl::BoneRecord& bone = mdl::Bones(m)[
                        s->state.cameraParentBone];
                    if (strcmp(lbText, bone.name) == 0)
                        SendMessageA(GetDlgItem(main, panel::kBoneRegisterCombo), CB_SETCURSEL,
                                     static_cast<WPARAM>(k), 0);
                }
            }
            for (std::size_t keyIndex = 0;
                 keyIndex < kGlobalKeyCapacity; ++keyIndex) {
                mdl::CameraKey& key = cameraKeys[keyIndex];
                if (key.parentModel == workspace.modelSlot) {
                    key.parentBone = remapBoneIndex(key.parentBone);
                }
            }
            for (int ai = 0; ai < 255; ++ai) {
                if (accs[ai] == nullptr) continue;
                mdl::AccessoryRecord& accessory =
                    *mdl::Accessory(accs[ai]);
                if (accessory.parentModel == workspace.modelSlot) {
                    accessory.parentBone =
                        remapBoneIndex(accessory.parentBone);
                }
                auto* const keys = reinterpret_cast<mdl::AccessoryKey*>(
                    accTracks[ai]);
                for (std::size_t i = 0; i < kGlobalKeyCapacity; ++i) {
                    if (keys[i].parentModel == workspace.modelSlot) {
                        keys[i].parentBone = remapBoneIndex(keys[i].parentBone);
                    }
                }
            }
        } else {
            // skipped model: gray menus + reference removal sweep
            EnableMenuItem(GetMenu(main), 0x120, 1);            // 0x45833E
            EnableMenuItem(GetMenu(main), 0x121, 1);
            for (int mi = 0; mi < kModelSlotCount; ++mi) {
                unsigned char* m = slots[mi];
                if (m == nullptr) continue;
                EnableMenuItem(GetMenu(main), 0x120, 0);
                EnableMenuItem(GetMenu(main), 0x121, 0);
                mdl::ModelRecord* const modelRecord = mdl::Mdl(m);
                if (modelRecord->comboSelIndex > workspace.displayOrder)
                    modelRecord->comboSelIndex -= 1;
                if (modelRecord->comboSelIndex2 > workspace.previousDisplayOrder)
                    modelRecord->comboSelIndex2 -= 1;
                mdl::BoneOrderEntry* const selectors = mdl::BoneOrder(m);
                for (std::uint32_t r = 0; r < mdl::BoneOrderCount(m); ++r) {
                    mdl::BoneOrderEntry& selector = selectors[r];
                    if (selector.linkedModel == workspace.modelSlot) {
                        selector.linkedModel = -1;
                        selector.linkedBone = 0;
                    }
                }
                mdl::DisplayKey* const displayKeys = mdl::DisplayKeys(m);
                for (std::size_t keyIndex = 0;
                     keyIndex < mdl::kDisplayKeyCapacity; ++keyIndex) {
                    mdl::BoneReference* const references =
                        mdl::SelectorStates(displayKeys[keyIndex]);
                    for (std::uint32_t k = 0; k < mdl::BoneOrderCount(m); ++k) {
                        mdl::BoneReference& reference = references[k];
                        if (reference.modelIndex == workspace.modelSlot) {
                            reference.modelIndex = -1;
                            reference.boneIndex = 0;
                        }
                    }
                }
            }
            if (s->state.cameraParentModel == workspace.modelSlot) {
                s->state.cameraParentModel = -1;
                s->state.cameraParentBone = 0;
                SendMessageA(GetDlgItem(main, panel::kMainComboNormal), CB_SETCURSEL, 0, 0);
                SendMessageA(GetDlgItem(main, panel::kBoneRegisterCombo), CB_RESETCONTENT, 0, 0);
            }
            for (std::size_t keyIndex = 0;
                 keyIndex < kGlobalKeyCapacity; ++keyIndex) {
                mdl::CameraKey& key = cameraKeys[keyIndex];
                if (key.parentModel == workspace.modelSlot) {
                    key.parentModel = -1;
                    key.parentBone = 0;
                }
            }
            for (int ai = 0; ai < 255; ++ai) {
                if (accs[ai] == nullptr) continue;
                mdl::AccessoryRecord& accessory =
                    *mdl::Accessory(accs[ai]);
                if (accessory.parentModel == workspace.modelSlot) {
                    accessory.parentModel = -1;
                    accessory.parentBone = 0;
                }
                auto* const keys = reinterpret_cast<mdl::AccessoryKey*>(
                    accTracks[ai]);
                for (std::size_t i = 0; i < kGlobalKeyCapacity; ++i) {
                    if (keys[i].parentModel == workspace.modelSlot) {
                        keys[i].parentModel = -1;
                        keys[i].visible = 0;
                    }
                }
            }
        }
    }
    // key-chain integrity checks (0x458996..0x458BB8)
    for (int mi = 0; mi < kModelSlotCount; ++mi) {
        unsigned char* m = slots[mi];
        if (m == nullptr) continue;
        mdl::DisplayKey* keys = mdl::DisplayKeys(m);
        keys[0].previous = 0;
        std::int32_t cur = static_cast<std::int32_t>(keys[0].next);
        if (cur != 0) {
            std::int32_t prev = 0;
            bool done = false;
            while (static_cast<std::int32_t>(keys[cur].previous) == prev) {
                prev = cur;
                cur = static_cast<std::int32_t>(keys[cur].next);
                if (cur == 0) {
                    done = true;
                    break;
                }
            }
            if (!done) {
                sprintf_s(text, 0x100, kJpChainFmtDisp,
                          mdl::Mdl(m)->name,
                          keys[prev].frame,
                          mdl::Mdl(m)->name,
                          keys[prev].frame);
                MessageBoxA(main, text, kJpChainCapPhys, 0);
                keys[prev].next = 0;
            }
        }
    }
    for (int mi = 0; mi < kModelSlotCount; ++mi) {
        unsigned char* m = slots[mi];
        if (m == nullptr) continue;
        if (mdl::Mdl(m)->boneCount == 0) continue;
        mdl::BoneKey* keys = mdl::BoneKeys(m);
        for (std::uint32_t b = 0; b < mdl::Mdl(m)->boneCount; ++b) {
            keys[b].previous = 0;
            std::int32_t cur = static_cast<std::int32_t>(keys[b].next);
            if (cur == 0) continue;
            // 0x458B80 reloads EAX from the bone-loop index before advancing
            // to the next root, so a root's first sparse key points to b.
            std::int32_t prev = b;
            bool done = false;
            while (static_cast<std::int32_t>(keys[cur].previous) == prev) {
                prev = cur;
                cur = static_cast<std::int32_t>(keys[cur].next);
                if (cur == 0) {
                    done = true;
                    break;
                }
            }
            if (!done) {
                LogPmmModelStage(-1, "display-chain-error", b, cur,
                                 reinterpret_cast<void*>(
                                     static_cast<std::uintptr_t>(prev)));
                LogPmmModelStage(-1, "display-chain-links",
                                 keys[cur].previous,
                                 keys[cur].next,
                                 reinterpret_cast<void*>(static_cast<
                                     std::uintptr_t>(keys[cur].frame)));
                mikudancestudio::mdl::BoneRecord* bone = mdl::Bones(m) + b;
                // 0x458B49: the format argument and the chain cut both
                // use the EXPECTED-PREV record (iVar13's byte offset),
                // not the mismatching cur record.
                sprintf_s(text, 0x100, kJpChainFmtPhys,
                          mdl::Mdl(m)->name,
                          reinterpret_cast<char*>(bone),
                          keys[prev].frame,
                          keys[prev].frame,
                          reinterpret_cast<char*>(bone));
                MessageBoxA(main, text, kJpChainCapDisp, 0);
                keys[prev].next = 0;
            }
        }
    }
    // combo 434 population (0x458BD3..0x458DF0)
    {
        const LRESULT sel =
            SendMessageA(GetDlgItem(main, panel::kMainComboModel), CB_GETCURSEL, 0, 0);
        SendMessageA(GetDlgItem(main, panel::kRegisterScopeCombo), CB_RESETCONTENT, 0, 0);
        if (sel == 0) {
            if (s->EnglishUI() != 0) {
                SendMessageA(GetDlgItem(main, panel::kRegisterScopeCombo), CB_ADDSTRING, 0,
                             reinterpret_cast<LPARAM>("camera"));
                SendMessageA(GetDlgItem(main, panel::kRegisterScopeCombo), CB_ADDSTRING, 0,
                             reinterpret_cast<LPARAM>("light"));
                SendMessageA(GetDlgItem(main, panel::kRegisterScopeCombo), CB_ADDSTRING, 0,
                             reinterpret_cast<LPARAM>("s shadow"));
                SendMessageA(GetDlgItem(main, panel::kRegisterScopeCombo), CB_ADDSTRING, 0,
                             reinterpret_cast<LPARAM>("gravity"));
            } else {
                SendMessageW(GetDlgItem(main, panel::kRegisterScopeCombo), CB_ADDSTRING, 0,
                             reinterpret_cast<LPARAM>(kWCamera));
                SendMessageW(GetDlgItem(main, panel::kRegisterScopeCombo), CB_ADDSTRING, 0,
                             reinterpret_cast<LPARAM>(kWLight));
                SendMessageW(GetDlgItem(main, panel::kRegisterScopeCombo), CB_ADDSTRING, 0,
                             reinterpret_cast<LPARAM>(kWSelfSh));
                SendMessageW(GetDlgItem(main, panel::kRegisterScopeCombo), CB_ADDSTRING, 0,
                             reinterpret_cast<LPARAM>(kWGravity));
            }
            const LRESULT n =
                SendMessageA(GetDlgItem(main, panel::kAccessoryCombo), CB_GETCOUNT, 0, 0);
            for (LRESULT i = 0; i < n; ++i) {
                SendMessageA(GetDlgItem(main, panel::kAccessoryCombo), CB_GETLBTEXT,
                             static_cast<WPARAM>(i),
                             reinterpret_cast<LPARAM>(lbText));
                SendMessageA(GetDlgItem(main, panel::kRegisterScopeCombo), CB_ADDSTRING, 0,
                             reinterpret_cast<LPARAM>(lbText));
            }
            SendMessageA(GetDlgItem(main, panel::kRegisterScopeCombo), CB_SETCURSEL, 0, 0);
        } else {
            int found = 0;
        while (found < kModelSlotCount &&
               (slots[found] == nullptr ||
                    mdl::Mdl(slots[found])->comboSelIndex !=
                        static_cast<unsigned char>(sel)))
                ++found;
            if (found < kModelSlotCount) {
                s->SetSelectedModelSlot(static_cast<unsigned char>(found));
                PostLoadInit(slots[found]);                      // 0x49C850
            }
        }
    }
    // child window refresh + frame edit (0x458DF7..0x458E86)
    if (s->state.floatingWindow != 0) {
        RefreshSeparateWindowViewport(s);                                           // 0x4290F0
        InvalidateRect(
            reinterpret_cast<HWND>(s->state.floatingWindow),
            nullptr, FALSE);
    }
    {
        sprintf_s(text, 0x100, "%d", ctx.maxFrame);
        SetWindowTextA(
            GetDlgItem(s->state.floatingWindow
                           ? reinterpret_cast<HWND>(
                                 s->state.floatingWindow)
                           : main,
                       panel::kGotoFrameEdit),
            text);
    }
    if (s->state.optflag[0] != 0) {
        RefreshLightPanel(s);                                           // 0x411070
        PanelPaint(s);                                          // 0x414610
    }
    // record array free (0x458EB2..0x458F25)
    FreeRecordArrays(workspaces, modelCount);
    delete[] workspaces;
    InvalidateRect(main, nullptr, FALSE);                       // 0x458F36
    RelayoutSidebarControls(s);                                               // 0x442EB0
    s->PhysicsResetPending() = 1;                               // 0x458F43
    s->state.windowLayoutReady = 1;                // 0x458F4C

    s->ApplyTimelineLightState();
    TraceSceneLightState(s, "pmm-load-tail");
    PostViewRefresh(s);                                         // 0x40D130
}


}  // namespace

void LoadSceneV2(MMDApp* app, int fd) {  // VA 0x00450000
    auto* s = app;
    PmmV2LoadContext ctx(app);

    LoadSceneV2_DisposeAndHeader(ctx, fd);            // 0x450040..0x45044C

    // ---- model count / record array (0x45044E..0x4504B4) ----------------
    s->state.projectedShadowBlendEnabled = 0;                 // 0x450458
    Rd(fd, &s->SelectedModelSlot(), 1);                         // 0x45045F
    unsigned char modelCount = 0;
    Rd(fd, &modelCount, 1);                                     // 0x45046C
    if (s->SelectedModelSlot() >= kModelSlotCount) {
        _close(fd);
        ResetAppState(s);
        HandleWindowSize(s);
        return;
    }
    auto* const workspaces = new PmmModelLoadWorkspace[modelCount];
    unsigned char modelIdx = 0;

    LogPmmModelStage(fd, "accessory-track-174-at-load-entry", 0, 0,
                     s->AccessoryKeys(174));
    LogPmmModelStage(fd, "overlay-buffer-at-load-entry", 0, 0,
                     s->OverlayVertices());

    if (modelCount != 0) {
        for (;;) {  // 0x4504BA
            if (!LoadSceneV2_ModelBlock(ctx, fd, workspaces, modelCount,
                                        modelIdx))
                return;  // AbortV2Load already ran (0x45426C family)
            if (++modelIdx >= modelCount) break;                // 0x45410A
        }
    }

    LoadSceneV2_ReleaseTracks(ctx, fd, workspaces, modelIdx, modelCount);  // 0x454127..0x454213
    LoadSceneV2_ResetCombos(ctx);                       // 0x454230..0x454C8A
    LoadSceneV2_ReallocateTracks(ctx);                  // 0x454C9A..0x454DFB
    LoadSceneV2_CameraTrack(ctx, fd);                   // 0x454E07..0x455286
    LoadSceneV2_LightTrack(ctx, fd);                    // 0x455286..0x45593E
    if (!LoadSceneV2_AccessoryBlock(ctx, fd)) {
        FreeRecordArrays(workspaces, modelCount);
        delete[] workspaces;
        return;
    }
    LoadSceneV2_ConfigBlock(ctx, fd);                   // 0x456617..0x45776E
    LoadSceneV2_PhysicsTracksAndClose(ctx, fd, modelCount);  // 0x45777C..0x458112
    LoadSceneV2_SuccessTail(ctx, workspaces, modelCount);    // 0x458120..0x458F53
}

}  // namespace mikudancestudio
