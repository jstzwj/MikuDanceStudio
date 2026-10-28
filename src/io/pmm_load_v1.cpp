// ===========================================================================
// VA 0x0045916D..0x0045E7F7 - LoadSceneV1  (the inline v1 (.pmm "0001")
//                                     loader body of sub_458F80)
// ===========================================================================
// Entered from the load shell sub_458F80 when the 30-byte header version
// field reads "0001" (all three stock sample scenes).  The body owns the fd
// on every path: _close happens at 0x45E11D on success and at 0x45A7CA /
// 0x45C5D5 on the two abort paths.
//
// Layout (all addresses inside sub_458F80):
//   0x45916D  dispose 100 model slots + free the four global tracks
//             (0x374/0x378/0x37C/0x380) + free the 255 accessory objects
//             and tracks (0x9DD70 / 0x384)
//   0x45922D  scene-state reset run (same field set as v2 plus A046C/A0470)
//   0x4592F6  menu/checkbox reset (0xF7, 0x217), A0D2C=flt_52A1D8, A0D30=0,
//             AVI teardown trio, 0x91C=0, AVI-path clear, 0x9E3F0 release
//   0x4593F9  header reads: render w/h, edit flag (A06C8 only when
//             A0D38==0 - no A0D3C variant), fov, 7 option bytes
//   0x459526  UI clear run: 7 edits 0x1DE..0x1E4, uncheck 0x1B8/0x1B9/0x1DD,
//             CheckMenuItem 0xFE, EnableMenuItem 0x120/0x121 grayed
//   0x45964A  9ED9A=0; slot byte -> 0x910; model count; combo resets
//             0x1B4/0x1DA/0x1C1/0x1C2 + initial strings; per-model 20-byte
//             name pre-pass into 0x1B4/0x1DA/0x1C1
//   0x459861  per-model loop: slot byte, allocate/init ModelRecord,
//             ModelInitDefaults, 20-byte name + 0x100 path, ResolveAnsiUserFile,
//             ModelLoadPMD; on failure MessageBoxA + GetOpenFileNameW retry
//             (cancel -> 0x45A7C5 close/abort; retry failure -> box, CONTINUE)
//             then the direct-index state load into the model fields
//             (0x2D7C disp count/order, 0x2D8D, 0x2D90 + 0x2D9C..0x2DA8 W4s,
//             0x26D4 group count + 0x26D0/0x65 flags, 0x31AC/0x31B0,
//             dense+sparse bone keys 0x26E0/0x3C, morph keys 0x26E4/0x14,
//             physics keys 0x26E8/0x1C with bool arrays +0x10 / flag +0x14,
//             display-frame records 0x26BC/0x25C with the duplicated +0x14C
//             read and the 0x2D98/0x2D94 bool bytes, morph current 0x26C4/
//             0x88+0x30, IK current 0x26C0/0x18+0x12), EnableMenuItem
//             0x120/0x121 enable
//   0x45A6D3  combo 0x1B1 register list (5 entries in model mode incl.
//             PostLoadInit + CB_SETCURSEL(model 0x2D7C); 7 entries in camera
//             mode), CB_SETCURSEL(0x1B1, 3)
//   0x45AA93  track re-allocation (camera 840000 / light 400000 / selection
//             240000 / shadow 360000 + defaults; camera +0x4C dword = -1
//             sweep; 255 accessory tracks 600000) - v1 allocates through
//             the 0x4C6889 malloc-family helper
//   0x45AC10  camera track read (record 0 + sparse, 84-byte records with a
//             SHORTER field set than v2: 0x28 bytes + 4x4 interp bytes +
//             flag +0x40 + dword +0x44 + flag +0x48) + camera misc W4s +
//             frame UI (BM_SETCHECK 0x1BE uses 0x31C==0 - inverted vs v2)
//   0x45B090  light track read (40-byte records, same shape as v2) + light
//             misc + rgb/direction slider UI
//   0x45B761  9E170 byte + 9DA48 dword; combo 0x1D7/0x1DB resets; accessory
//             block (0x4B0 objects, GetOpenFileNameW retry with .x/.vac
//             filter; cancel -> 0x45A7C5, retry failure -> 0x45C5D5 close +
//             ResetAppState + return (NO HandleWindowSize); 60-byte track
//             records with the transparency quirk byte)
//   0x45C390  current accessory UI (CB_SETCURSEL 0x1D7, parent-model frame
//             entries with the (<7 || ==8) filter, SyncAccessoryEditPanel)
//   0x45C48C  config block: 0x980/0x97C/0x9E16C, frame edit 0x1A1, refresh
//             chain, radio 0x914 switch (v1 map: 0->0x1EA, 1->0x1EB,
//             2->none, 3->0x1ED, 4->0x1EC), checkbox bytes 0x340..0x342/
//             0x9ED99 + edits 0x199/0x19A, ClearTimelineAndCurveDCs, wave path (0xD0),
//             AVI block (0x91C read-before-test; w1/w2/w3 -> 9E414/9E418/
//             9E41C), picture block (9E428; p1/p2/p3 -> 9E434/9E438/9E43C),
//             0x31E/0x31D/0x918 menu checks with owner checkboxes
//             0x227/0x22D, FPS menu (1000/30 float compares), 0x9EB84
//             screen-mode switch, physics defaults, CB_SETCURSEL 0x14E on
//             0x1C1/0x1C2
//   0x45D278  read-gated tail (the v1 loader CHECKS _read results here):
//             A0B20 chain (shadow distance copies, per-model 0x31BE bytes,
//             BM_SETCHECK 0x1B9/0x1DD, 0x31C0 sweep, 9ED9A menu, A0CC4
//             physics-mode menu, gravity reads, A0CD4, A0D30/A0188 +
//             selection track 0x37C 24-byte records, model 0x37C0 bytes,
//             A0198..A01A0 color sweep, 0x11A, camera parent reregister +
//             A0430/A0434 + RefillBoneRegisterCombo + 0x1C2 strcmp loop, 16 config dwords
//             0xA0438..0xA0474, 0xF7/0x217, A0478, 0x11D scene +0xD4,
//             per-model 0x2D7D byte sweep)
//   0x45E11D  _close(fd)
//   0x45E149  success tail: shadow-mode gate (RefreshSelfShadowPanel), menu 0x117,
//             BM_SETCHECK 0x1B8, light direction to the physics scene
//             (D3DXVec3Normalize + vtable[13]), window title
//             ("MikuMikuDance [%s]"), two key-chain integrity boxes
//             (physics 0x26E8/0x1C chain and per-bone 0x26E0/0x3C chains -
//             v1 compares the chain link against 0, not the root index),
//             combo 0x1B2 population (camera/light/s shadow/gravity +
//             accessory names; else model re-select -> 0x910 +
//             PostLoadInit), RefreshSeparateWindowViewport + InvalidateRect(child), edit-mode
//             repaint (RefreshLightPanel + PanelPaint), InvalidateRect, RelayoutSidebarControls,
//             9EDB5=1, A442C=1, PostViewRefresh
//
// Split: the body is divided into static LoadSceneV1_* segment functions
// along this VA map (each carries its address-range banner); the shared
// frame - window handles, slot arrays, scratch buffers, track pointers -
// lives in PmmV1LoadContext.  The read-gated tail stays ONE function
// with the original nesting.
//
// Deviations: the v1-only quirks are kept and commented inline (inverted
//   0x1BE checkbox sense, duplicated +0x14C read, radio case 2 leaving all
//   four radios unchecked, physics-mode default leaving the menus untouched,
//   sprintf boxes whose format takes more arguments than the original
//   passed - reproduced argument-for-argument).
// ===========================================================================
#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include "mikudancestudio/menu_commands.h"
#include <Windows.h>
#include <commdlg.h>
#include <commctrl.h>
#include <btBulletDynamicsCommon.h>
#include <cmath>

#include <cstdint>
#include <cstdio>
#include <cstring>
#include <cstdlib>
#include <memory>
#include <io.h>

#include "mikudancestudio/accessory_layout.hpp"
#include "mikudancestudio/d3dx_dyn.hpp"
#include "mikudancestudio/mmd_app.hpp"
#include "mikudancestudio/model.hpp"
#include "mikudancestudio/ported_funcs.hpp"
#include "mikudancestudio/scene_ownership.hpp"
#include "mikudancestudio/panel_controls.hpp"
#include "mikudancestudio/scene_session.hpp"
#include "pmm_v1_layout_win.hpp"

// Shared PMM record readers, the Rd helper and the byte-identical halves of
// the loader string tables live here (see the header comment for the
// v1/v2 parameterization points).
#include "pmm_io_common.hpp"
#include "pmm_playback_options.hpp"

#pragma comment(lib, "avifil32.lib")

namespace mikudancestudio {

#ifdef MIKUDANCESTUDIO_DIAG
void WritePmmV1RuntimeParityReport(MMDApp* app);
void PreparePmmWorldParityReport(MMDApp* app);
void WritePmmWorldParityAtLoad(MMDApp* app);
#endif

using namespace pmm_io;

namespace {

// flt_52A1D8 (0x3C3851EC = 0.01125f) - the float the original seeds into
// A0D2C/selfShadowDistance at 0x45934D.  C++17 has no std::bit_cast; MSVC's
// __builtin_bit_cast is accepted in constant expressions, so the
// static_assert pins the literal to the original .rdata bit pattern.
constexpr float kSelfShadowDistanceDefault = 0.01125f;
static_assert(__builtin_bit_cast(std::uint32_t, kSelfShadowDistanceDefault) == 0x3C3851ECu,
              "flt_52A1D8 bit-exact");

// Tracks share the same ownership as initial/v2/rebuilt scene tracks.
// Pair the raw allocation with operator delete at scene teardown.
inline unsigned char* AllocBytes(std::size_t n) {
    return static_cast<unsigned char*>(::operator new(n));
}

// ---- UI strings: see pmm_io_common.hpp (byte-identical halves; the wide
// .rdata mirrors and kNon moved there). ------------------------------------

// Porting-era trace under MIKUDANCESTUDIO_PMM_TRACE_DIR (CMake option
// MIKUDANCESTUDIO_DIAG, default OFF); the OFF stub keeps the call sites
// valid and inlines away to nothing.
#ifdef MIKUDANCESTUDIO_DIAG
void LogV1Stage(const char* stage, long pos, std::int32_t a = 0, std::int32_t b = 0) {
    const char* dir = std::getenv("MIKUDANCESTUDIO_PMM_TRACE_DIR");
    if (dir == nullptr || dir[0] == '\0')
        return;
    char p[MAX_PATH];
    sprintf_s(p, "%s\\pmm_v1_load.log", dir);
    FILE* stream = nullptr;
    if (fopen_s(&stream, p, "ab") != 0 || stream == nullptr)
        return;
    fprintf(stream, "stage=%s file_pos=%ld a=%ld b=%ld\r\n", stage, pos, static_cast<long>(a),
            static_cast<long>(b));
    fclose(stream);
}
#else
inline void LogV1Stage(const char*, long, std::int32_t = 0, std::int32_t = 0) {
}
#endif

constexpr std::size_t kTrackCam = sizeof(mdl::CameraKey) * mdl::kTimelineKeyCapacity;     // 0xCD140
constexpr std::size_t kTrackLight = sizeof(mdl::LightKey) * mdl::kTimelineKeyCapacity;    // 0x61A80
constexpr std::size_t kTrackSel = sizeof(mdl::SelfShadowKey) * mdl::kTimelineKeyCapacity; // 0x3A980
constexpr std::size_t kTrackShadow = sizeof(mdl::GravityKey) * mdl::kTimelineKeyCapacity; // 0x57E40
constexpr std::size_t kTrackAcc = sizeof(mdl::AccessoryKey) * mdl::kTimelineKeyCapacity;  // 0x927C0

} // namespace

namespace {

// ---- SJIS box texts (byte-exact; VA recorded) ----------------------------
// kJpChainCapPhys / kJpCannotOpenModel / kJpOpenCaption are byte-identical
// to their v2 copies and live in pmm_io_common.hpp.  Wave5-C IDA verdict
// (x86 original): the four kept below are byte-identical to their v2
// copies too - single .rdata originals (0x52D770 / 0x52D788 / 0x52D848 /
// 0x52DAE0) referenced by both loader bodies (v1 sites 0x45E4FC /
// 0x45E4DA / 0x45E37A / 0x45B9B7; v2 sites 0x458B54 / 0x458B32 /
// 0x458A22 / 0x455B8C); the v2 file's former "and below" / bu / substitute / missing "retry"
// readings were transcription errors, corrected in Wave5-C.  Per-body
// duplicates stay until the loader split lands, then belong in
// pmm_io_common.hpp.
const char kJpChainCapDisp[] =
    "\x83Z\x81[\x83u\x83\x66\x81[\x83^\x82\xCC\x88\xD9\x8F\xED"; // 0x52D770
const char kJpChainFmtPhys[] = "\x22%s\x22\x83\x82\x83\x66\x83\x8B\x81\x41\x83{\x81[\x83"
                               "\x93\x22%s\x22\x82\xCC\x83t\x83\x8C\x81[\x83\x80\x83\x66"
                               "\x81[\x83^\x82\xC9\x88\xD9\x8F\xED\x82\xAA\x8C\xA9\x82\xC2"
                               "\x82\xA9\x82\xE8\x82\xDC\x82\xB5\x82\xBD\x0A\x0A\x88\xD9"
                               "\x8F\xED\x82\xC8\x83t\x83\x8C\x81[\x83\x80(\x83t\x83\x8C"
                               "\x81[\x83\x80\x94\xD4\x8D\x86%d\x88\xC8\x8D~)\x82\xF0\x8D"
                               "\xED\x8F\x9C\x82\xB5\x82\xDC\x82\xB7\x0A%d\x83t\x83\x8C\x81"
                               "[\x83\x80\x88\xC8\x8D~\x82\xCC%s\x82\xCC\x83\x82\x81[\x83"
                               "V\x83\x87\x83\x93\x82\xF0\x8D\xC4\x93x\x90\xDD\x92\xE8\x82"
                               "\xB5\x92\xBC\x82\xB5\x82\xC4\x89\xBA\x82\xB3\x82\xA2"; // 0x52D788
const char kJpChainFmtDisp[] = "\x22%s\x22\x83\x82\x83\x66\x83\x8B\x81\x41\x22\x95\x5C\x8E"
                               "\xA6\xA5IK\xA5\x8AO\x90\x65\x22\x82\xCC\x83t\x83\x8C\x81"
                               "[\x83\x80\x83\x66\x81[\x83^\x82\xC9\x88\xD9\x8F\xED\x82\xAA"
                               "\x8C\xA9\x82\xC2\x82\xA9\x82\xE8\x82\xDC\x82\xB5\x82\xBD"
                               "\x0A\x0A\x88\xD9\x8F\xED\x82\xC8\x22\x95\x5C\x8E\xA6\xA5"
                               "IK\xA5\x8AO\x90\x65\x22\x83t\x83\x8C\x81[\x83\x80\x81i\x83"
                               "t\x83\x8C\x81[\x83\x80\x94\xD4\x8D\x86%d\x88\xC8\x8D~\x81"
                               "j\x82\xF0\x8D\xED\x8F\x9C\x82\xB5\x82\xDC\x82\xB7\x0A\x90"
                               "\x5C\x82\xB5\x96\xF3\x82\xA0\x82\xE8\x82\xDC\x82\xB9\x82"
                               "\xF1\x82\xAA\x81\x41%s\x83\x82\x83\x66\x83\x8B\x82\xCC%d"
                               "\x83t\x83\x8C\x81[\x83\x80\x88\xC8\x8D~\x82\xCC\x22\x95\x5C"
                               "\x8E\xA6\xA5IK\x22\x82\xF0\x8D\xC4\x93x\x90\xDD\x92\xE8\x82"
                               "\xB5\x92\xBC\x82\xB5\x82\xC4\x89\xBA\x82\xB3\x82\xA2"; // 0x52D848
const char kJpModelFailFmt[] = "%s\x82\xCC\x83\x82\x83\x66\x83\x8B\x83t\x83@\x83\x43\x83"
                               "\x8B\x82\xAA\x8C\xA9\x82\xC2\x82\xA9\x82\xE8\x82\xDC\x82"
                               "\xB9\x82\xF1\x0A\x0A\x83\x82\x83\x66\x83\x8B\x83t\x83@\x83"
                               "\x43\x83\x8B\x82\xCC\x8F\xEA\x8F\x8A\x82\xF0\x8Ew\x92\xE8"
                               "\x82\xB5\x82\xC4\x89\xBA\x82\xB3\x82\xA2\x0A\x0A(\x83|\x83"
                               "\x8A\x83S\x83\x93\x92\xB8\x93_\x90\x94\x82\xE2\x83{\x81["
                               "\x83\x93\x8D\x5C\x91\xA2\x81\x41\x95\x5C\x8F\xEE\x82\xCC"
                               "\x8E\xED\x97\xDE\x93\x99\x91S\x82\xAD\x93\xAF\x82\xB6\x83"
                               "\x82\x83\x66\x83\x8B\x83t\x83@\x83\x43\x83\x8B\x82\xC5\x82"
                               "\xC8\x82\xA2\x82\xC6\x0A\x93\xC7\x82\xDD\x8D\x9E\x82\xDD"
                               "\x83G\x83\x89\x81[\x82\xAA\x94\xAD\x90\xB6\x82\xB5MMD\x82"
                               "\xAA\x8B\xAD\x90\xA7\x8FI\x97\xB9\x82\xB7\x82\xE9\x89\xC2"
                               "\x94\x5C\x90\xAB\x82\xAA\x82\xA0\x82\xE8\x82\xDC\x82\xB7"
                               "\x82\xCC\x82\xC5\x92\x8D\x88\xD3\x82\xB5\x82\xC4\x89\xBA"
                               "\x82\xB3\x82\xA2)"; // 0x52DE00
const char kJpCannotOpenAcc[] = "\x83\x41\x83N\x83Z\x83T\x83\x8A\x83t\x83@\x83\x43\x83\x8B"
                                "(%s)\x82\xAA\x8C\xA9\x82\xC2\x82\xA9\x82\xE8\x82\xDC\x82"
                                "\xB9\x82\xF1\x0A\x0A%s\x82\xCC\x8F\xEA\x8F\x8A\x82\xF0\x8E"
                                "w\x92\xE8\x82\xB5\x82\xC4\x89\xBA\x82\xB3\x82\xA2(\x91\xE3"
                                "\x91\xD6\x89\xC2)"; // 0x52DAE0

} // namespace

namespace {

// Per-load frame shared by the LoadSceneV1 segment functions: the
// original keeps every one of these in sub_458F80's inline v1 body
// frame; the split passes them as one context (same pattern as the v2
// loader's PmmV2LoadContext).
struct PmmV1LoadContext {
    MMDApp* s;
    HWND main;
    PathResolutionWorkspace* paths;
    D3DRenderer* wrap;
    mdl::ModelRecord** slots;
    mdl::AccessoryRecord** accs;
    mdl::AccessoryKey** accTracks;
    // shared scratch buffers (the 0x218/0x318/0x380/0xdb8 frame slots of
    // the original stack)
    char text[0x100];        // name/EM_REPLACESEL scratch
    char box[0x3E8];         // 1000-byte message-box text (0x459994)
    char mbPath[0x100];      // accessory/background path scratch
    wchar_t widePath[0x100]; // model path
    wchar_t wideTmp[0x100];  // accessory path
    wchar_t ofnFile[0x100];  // GetOpenFileName buffer
    wchar_t ofnTitle[0x100];
    wchar_t wndText[0x100]; // window-title sprintf target
    char lbText[0x100];     // CB_GETLBTEXT buffer
    // the re-allocated global tracks this body reads back (0x45AA93
    // segment; the gravity track stays local - nothing reads it)
    mdl::CameraKey* cameraKeys = nullptr;
    mdl::LightKey* lightKeys = nullptr;
    mdl::SelfShadowKey* selfShadowKeys = nullptr;

    explicit PmmV1LoadContext(MMDApp* app)
        : s(app), main(reinterpret_cast<HWND>(app->Hwnd())), paths(&app->PathWorkspace()),
          wrap(app->Renderer()), slots(app->ModelSlots()), accs(app->AccessorySlots()),
          accTracks(app->AccessoryKeyTracks()) {}
};

// common abort for a cancelled locate-file dialog (0x45A7C5):
// close + ResetAppState + HandleWindowSize + return; the split-out twin
// of the abortLoad lambda the inline body carried.
static void AbortV1Load(MMDApp* s, int fd) {
    _close(fd);          // 0x45A7CA
    ResetAppState(s);    // 0x45A7D4
    HandleWindowSize(s); // 0x45A7DB
}

// ---- 0x45916D..0x459630: dispose + scene-state reset + menu/checkbox
// reset + AVI teardown + header-field reads + UI clear run -------------
static void LoadSceneV1_DisposeAndHeader(PmmV1LoadContext& ctx, int fd) {
    auto* const s = ctx.s;
    HWND const main = ctx.main;
    char* const text = ctx.text;

    // ---- dispose models + tracks + accessories (0x45916D..0x459257) ------
    ReleaseSceneModels(*s);          // 0x45916D
    ReleaseGlobalTimelineTracks(*s); // 0x459196
    ReleaseAccessoriesAndTracks(*s); // 0x459221
    // ---- scene-state reset run (0x45922D..0x4592F5) ----------------------
    s->state.accessoryRenderSplitOrder = 0;
    s->SelectGlobalTimelineTrack(GlobalTimelineTrack::Camera);
    s->state.mainModelComboSelection = 0;
    s->state.cameraParentModel = -1;
    s->state.cameraParentBone = 0;
    s->state.cameraAttachmentBasis[14] = 0;    // 0xA0470
    s->state.cameraAttachmentBasis[13] = 0;    // 0xA046C
    s->state.cameraAttachmentBasis[12] = 0.0f; // 0xA0468
    s->state.cameraAttachmentBasis[11] = 0.0f; // 0xA0464
    s->state.cameraAttachmentBasis[9] = 0.0f;  // 0xA045C
    s->state.cameraAttachmentBasis[8] = 0.0f;  // 0xA0458
    s->state.cameraAttachmentBasis[7] = 0.0f;  // 0xA0454
    s->state.cameraAttachmentBasis[6] = 0.0f;  // 0xA0450
    s->state.cameraAttachmentBasis[4] = 0.0f;  // 0xA0448
    s->state.cameraAttachmentBasis[3] = 0.0f;  // 0xA0444
    s->state.cameraAttachmentBasis[2] = 0.0f;  // 0xA0440
    s->state.cameraAttachmentBasis[1] = 0.0f;  // 0xA043C
    s->state.cameraAttachmentBasis[15] = 1.0f; // 0xA0474
    s->state.cameraAttachmentBasis[10] = 1.0f; // 0xA0460
    s->state.cameraAttachmentBasis[5] = 1.0f;  // 0xA044C
    s->state.cameraAttachmentBasis[0] = 1.0f;  // 0xA0438
    s->state.followCameraEnabled = 0;          // 0x9ED98
    // ---- menu/checkbox reset (0x4592F6..0x45933F) ------------------------
    CheckMenuItem(GetMenu(main), MDS_CMD_TOGGLE_CAMERA_TRACKING, 0);                 // 0x459316
    SendMessageA(GetDlgItem(main, panel::kFollowCameraCheckbox), BM_SETCHECK, 0, 0); // 0x45933A

    s->state.selfShadowDistance = kSelfShadowDistanceDefault; // 0x45934D
    s->state.selfShadowMode = 0;                              // 0x45935C

    // ---- AVI teardown trio (0x459369..0x45939D) --------------------------
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
    // v1 additionally clears the AVI gate dword before clearing the path.
    s->AviBackgroundEnabled() = 0;                  // 0x4593A8
    swprintf_s(s->AviBackgroundPath(), 0x100, L""); // 0x4593C7

    if (s->AviBackgroundTexture() != nullptr) { // 0x4593DC
        s->AviBackgroundTexture()->Release();
        s->AviBackgroundTexture() = nullptr;
        s->CpuNativeBackgroundTexturePresent() = false;
    }

    // ---- header-field reads (0x4593F9..0x459501) -------------------------
    format::PmmSceneSettings settings;
    if (format::ReadPmmSceneSettings(ReadPmmFileBytes, &fd, format::PmmVersion::V1, settings)) {
        s->state.renderW = settings.renderWidth;
        s->state.renderH = settings.renderHeight;
        if (s->state.floatingWindow == 0)
            s->state.sidebarWidth = settings.editPanelWidth;
        s->state.cameraFov = settings.cameraFov;
        for (int index = 0; index < 6; ++index)
            s->state.optflag[index] = settings.options[index];
    }
    strcpy_s(text, 0x100, ""); // 0x459526

    // ---- UI clear run (0x45953F..0x459630) -------------------------------
    for (int id = panel::kAccPosXEdit; id <= panel::kAccScaleEdit; ++id) { // 0x1DE..0x1E4 edits
        HWND item = GetDlgItem(main, id);
        const int len = GetWindowTextLengthA(item);
        SendMessageA(item, EM_SETSEL, 0, len);
        SendMessageA(item, EM_REPLACESEL, 0, reinterpret_cast<LPARAM>(text));
    }
    SendMessageA(GetDlgItem(main, panel::kShadowCheckbox), BM_SETCHECK, 0, 0);   // 0x4595A8
    SendMessageA(GetDlgItem(main, panel::kAddBlendCheckbox), BM_SETCHECK, 0, 0); // 0x4595C2
    SendMessageA(GetDlgItem(main, panel::kAccessoryAddBlendCheckbox), BM_SETCHECK, 0,
                 0);                                                            // 0x4595DC
    CheckMenuItem(GetMenu(main), MDS_CMD_TOGGLE_GROUND_SHADOW_TRANSPARENCY, 0); // 0x4595F3
    EnableMenuItem(GetMenu(main), MDS_CMD_MODEL_DRAW_ORDER, 1);                 // 0x45960E
    EnableMenuItem(GetMenu(main), MDS_CMD_MODEL_CALCULATION_ORDER, 1);          // 0x459629
}

// ---- 0x45964A..0x459841: 9ED9A=0, slot byte, model count, combo resets
// + the per-model 20-byte name pre-pass; returns the model count ------
static unsigned char LoadSceneV1_ModelPrePass(PmmV1LoadContext& ctx, int fd) {
    auto* const s = ctx.s;
    HWND const main = ctx.main;
    char* const text = ctx.text;

    // ---- model count / combo resets (0x45964A..0x45979E) ----------------
    s->state.projectedShadowBlendEnabled = 0; // 0x459640
    Rd(fd, &s->SelectedModelSlot(), 1);       // 0x45964A
    unsigned char modelCount = 0;
    Rd(fd, &modelCount, 1); // 0x45965B

    SendMessageA(GetDlgItem(main, panel::kMainComboModel), CB_RESETCONTENT, 0, 0); // 0x459675
    if (s->EnglishUI() == 0)
        SendMessageW(GetDlgItem(main, panel::kMainComboModel), CB_ADDSTRING, 0,
                     reinterpret_cast<LPARAM>(kWCamLight)); // 0x4596BA
    else
        SendMessageA(GetDlgItem(main, panel::kMainComboModel), CB_ADDSTRING, 0,
                     reinterpret_cast<LPARAM>("camera/light/accessory"));
    SendMessageA(GetDlgItem(main, panel::kMainComboGround), CB_RESETCONTENT, 0, 0); // 0x4596D8
    if (s->EnglishUI() == 0)
        SendMessageW(GetDlgItem(main, panel::kMainComboGround), CB_ADDSTRING, 0,
                     reinterpret_cast<LPARAM>(kWGround)); // 0x45971D
    else
        SendMessageA(GetDlgItem(main, panel::kMainComboGround), CB_ADDSTRING, 0,
                     reinterpret_cast<LPARAM>("ground"));
    SendMessageA(GetDlgItem(main, panel::kMainComboNormal), CB_RESETCONTENT, 0, 0); // 0x45973B
    if (s->EnglishUI() == 0)
        SendMessageW(GetDlgItem(main, panel::kMainComboNormal), CB_ADDSTRING, 0,
                     reinterpret_cast<LPARAM>(kWNashi)); // 0x459780
    else
        SendMessageA(GetDlgItem(main, panel::kMainComboNormal), CB_ADDSTRING, 0,
                     reinterpret_cast<LPARAM>(kNon));
    SendMessageA(GetDlgItem(main, panel::kBoneRegisterCombo), CB_RESETCONTENT, 0, 0); // 0x45979E

    // per-model 20-byte name pre-pass (0x4597C6..0x459841)
    for (unsigned char i = 0; i < modelCount; ++i) {
        Rd(fd, text, 0x14); // 0x4597C6
        SendMessageA(GetDlgItem(main, panel::kMainComboModel), CB_ADDSTRING, 0,
                     reinterpret_cast<LPARAM>(text));
        SendMessageA(GetDlgItem(main, panel::kMainComboGround), CB_ADDSTRING, 0,
                     reinterpret_cast<LPARAM>(text));
        SendMessageA(GetDlgItem(main, panel::kMainComboNormal), CB_ADDSTRING, 0,
                     reinterpret_cast<LPARAM>(text));
    }
    return modelCount;
}

// ---- 0x459861..0x45A6B4: one per-model record - allocate/init, load
// the PMD (MessageBoxA + GetOpenFileNameW retry; cancel aborts), then
// the direct-index state load.  Returns false after AbortV1Load ran.
static bool LoadSceneV1_ModelBlock(PmmV1LoadContext& ctx, int fd) {
    auto* const s = ctx.s;
    HWND const main = ctx.main;
    PathResolutionWorkspace& paths = *ctx.paths;
    D3DRenderer* const wrap = ctx.wrap;
    mdl::ModelRecord** const slots = ctx.slots;
    char* const box = ctx.box;
    char* const mbPath = ctx.mbPath;
    wchar_t* const widePath = ctx.widePath;
    wchar_t* const ofnFile = ctx.ofnFile;
    wchar_t* const ofnTitle = ctx.ofnTitle;

    unsigned char slotByte = 0;
    Rd(fd, &slotByte, 1); // 0x459861
    mdl::ModelRecord* nm = static_cast<mdl::ModelRecord*>(operator new(mdl::kSize));
    if (nm != nullptr)
        IdentityCtor(nm);                        // 0x459888
    slots[slotByte] = nm;                        // 0x459897
    std::memset(slots[slotByte], 0, mdl::kSize); // 0x4598BC
    ModelInitDefaults(slots[slotByte]);          // 0x4598D0

    char name20[0x14];
    Rd(fd, name20, 0x14);  // 0x4598E4
    Rd(fd, mbPath, 0x100); // 0x4598FB
    ResolveAnsiUserFile(wrap, mbPath, widePath, 0x100,
                        paths); // 0x459925

    LogV1Stage("model-resolved", _tell(fd), slotByte);
    mdl::ModelRecord* model = slots[slotByte];
    bool loaded = ModelLoadPMD( // 0x459965
        model, main, widePath, wrap,
        static_cast<int>(reinterpret_cast<std::uintptr_t>(s->state.exeDir)), 0, s->EnglishUI(),
        s->Physics(), paths);
    if (!loaded) { // 0x45998B
        sprintf_s(box, 0x3E8,
                  s->EnglishUI() != 0 ? "Cannot open the model file:%s"
                                        "\n\nPlease select the same model"
                                      : kJpModelFailFmt,
                  name20);
        MessageBoxA(main, box, s->EnglishUI() != 0 ? "open file" : kJpOpenCaption,
                    0);                                                          // 0x4599ED
        SetCurrentDirectoryW(reinterpret_cast<const wchar_t*>(s->state.exeDir)); // 0x4599FA
        swprintf_s(ofnFile, 0x100, L"");                                         // 0x459A11
        OPENFILENAMEW ofn;
        std::memset(&ofn, 0, sizeof(ofn));
        ofn.lStructSize = sizeof(ofn); // 0x459A22
        ofn.hwndOwner =
            s->state.floatingWindow ? reinterpret_cast<HWND>(s->state.floatingWindow) : main;
        ofn.lpstrFilter = kWFilterModel;
        ofn.lpstrFile = ofnFile;
        ofn.nMaxFile = 0x100;
        ofn.Flags = OFN_FILEMUSTEXIST;
        ofn.lpstrInitialDir = (GetMenuState(GetMenu(main), MDS_CMD_TOGGLE_PREVIOUS_FOLDER, 0) & 8)
                                  ? reinterpret_cast<LPCWSTR>(s->state.dirModel)
                                  : kWUserModel;
        ofn.lpstrDefExt = L"pmd;pmx";
        ofn.nMaxFileTitle = 0x100;
        ofn.lpstrFileTitle = ofnTitle;
        ofn.lpstrTitle =
            s->EnglishUI() != 0 ? L"load model" : reinterpret_cast<LPCWSTR>(kWOpenFile);
        if (!GetOpenFileNameW(&ofn)) { // 0x459B09
            AbortV1Load(s, fd);        // 0x45A7C5
            return false;
        }
        if (GetMenuState(GetMenu(main), MDS_CMD_TOGGLE_PREVIOUS_FOLDER, 0) & 8) {
            ExtractDirFromPath(ofnTitle, ofnFile);                                    // 0x459B3F
            wcscpy_s(reinterpret_cast<wchar_t*>(s->state.dirModel), 0x3E8, ofnTitle); // 0x459B51
        }
        if (!ModelLoadPMD( // 0x459B94
                model, main, ofnFile, wrap,
                static_cast<int>(reinterpret_cast<std::uintptr_t>(s->state.exeDir)), 0,
                s->EnglishUI(), s->Physics(), paths)) {
            // retry failed: box + CONTINUE (no abort)   // 0x459BCC
            MessageBoxA(main,
                        s->EnglishUI() != 0 ? "Cannot open the model file" : kJpCannotOpenModel,
                        s->EnglishUI() != 0 ? "open file" : kJpOpenCaption, 0);
        }
    }

    LogV1Stage("model-loaded", _tell(fd), loaded ? 1 : 0);
    // ---- state load (direct indices, no remap records) ----------
    mdl::ModelRecord* const record = model;
    // The two PMM display-order bytes are adjacent in the original
    // x86 allocation.  Keep that file layout while naming the
    // x64 members instead of indexing the model allocation.
    format::PmmModelSelection diskSelection;
    if (!format::ReadPmmModelSelection(ReadPmmFileBytes, &fd, format::PmmVersion::V1, 0,
                                       diskSelection)) {
        AbortV1Load(s, fd);
        return false;
    }
    record->comboSelIndex = diskSelection.comboIndex;
    record->comboSelIndex2 = record->comboSelIndex;
    s->state.mainModelComboSelection = record->comboSelIndex;
    record->loadComplete = diskSelection.loadComplete;
    record->selectedBone = diskSelection.selectedBone;
    for (std::size_t index = 0; index < diskSelection.selectedMorphs.size(); ++index)
        record->selectedMorphs[index] = diskSelection.selectedMorphs[index];
    mdl::DisplayGroup* const displayGroups = (model)->displayGroups;
    const unsigned char loadedGroupCount = model->groupCount;
    for (std::size_t index = 0; index < diskSelection.groupFlags.size(); ++index)
        if (displayGroups != nullptr && index < loadedGroupCount)
            displayGroups[index].flags = diskSelection.groupFlags[index];
    record->boneListPos = diskSelection.boneListPos;
    record->maxFrame = diskSelection.maxFrame;
    LogV1Stage("misc-done", _tell(fd), static_cast<int>(model->boneCount),
               static_cast<int>(model->morphCount));

    // dense bone keys (0x26E0 array, 0x3C stride)   0x459DF4..0x459EDE
    {
        const std::int32_t boneCnt = static_cast<std::int32_t>(record->boneCount);
        mdl::BoneKey* keys = model->boneKeys;
        for (std::int32_t i = 0; i < boneCnt; ++i) {
            ReadPmmBoneKey<PmmStream::V1>(fd, keys[i]);
        }
        // Sparse records name a bone-key pool index, not a timeline
        // frame.  The first boneCount entries are the dense heads.
        // (0x459F18..0x45A0D6)
        std::int32_t cnt = 0;
        Rd(fd, &cnt, 4);
        for (std::int32_t i = 0; i < cnt; ++i) {
            std::int32_t keyIndex = 0;
            Rd(fd, &keyIndex, 4);
            ReadPmmBoneKey<PmmStream::V1>(fd, keys[keyIndex]);
        }
    }
    // dense + sparse morph keys (0x26E4 array, 0x14 stride)
    {
        const std::int32_t morphCnt = static_cast<std::int32_t>(record->morphCount);
        mdl::MorphKey* keys = model->morphKeys;
        for (std::int32_t i = 0; i < morphCnt; ++i) // 0x45A0D7
            ReadPmmMorphKey(fd, keys[i]);
        std::int32_t cnt = 0;
        Rd(fd, &cnt, 4); // 0x45A107
        for (std::int32_t i = 0; i < cnt; ++i) {
            std::int32_t keyIndex = 0;
            Rd(fd, &keyIndex, 4);
            ReadPmmMorphKey(fd, keys[keyIndex]);
        }
    }

    LogV1Stage("bonekeys-done", _tell(fd));
    // physics key records (0x26E8 array, 0x1C stride; record 0
    // keeps the IK bool-array pointer at +0x10 and the flag at +0x14)
    {
        mdl::DisplayKey* keys = (model)->displayKeys;
        const auto readDisplay = [&](mdl::DisplayKey& key) {
            format::PmmDisplayKey disk;
            if (!ReadPmmDisplayKey(fd, PmmStream::V1, record->ikChainCount, 0, disk))
                return;
            key.frame = disk.frame;
            key.previous = disk.previous;
            key.next = disk.next;
            key.visible = disk.visible;
            key.allocated = disk.allocated;
            for (std::size_t index = 0; index < disk.ikEnabled.size(); ++index)
                mdl::IkStates(key)[index] = disk.ikEnabled[index];
        };
        readDisplay(keys[0]);
        std::int32_t cnt = 0;
        Rd(fd, &cnt, 4); // 0x45A23E
        for (std::int32_t i = 0; i < cnt; ++i) {
            std::int32_t keyIndex = 0;
            Rd(fd, &keyIndex, 4);
            readDisplay(keys[keyIndex]);
        }
    }
    LogV1Stage("physkeys-done", _tell(fd));
    // Current bone pose. The first quaternion component is read
    // twice, matching the original stream consumption order.
    {
        const std::int32_t boneCnt = static_cast<std::int32_t>(record->boneCount);
        mdl::BoneRecord* const bones = model->boneTable;
        for (std::int32_t i = 0; i < boneCnt; ++i) {
            mdl::BoneRecord& bone = bones[i];
            Rd(fd, &bone.trans[0], sizeof(bone.trans[0]));
            Rd(fd, &bone.trans[1], sizeof(bone.trans[1]));
            Rd(fd, &bone.trans[2], sizeof(bone.trans[2]));
            Rd(fd, &bone.rotQuat[0], sizeof(bone.rotQuat[0]));
            Rd(fd, &bone.rotQuat[1], sizeof(bone.rotQuat[1]));
            Rd(fd, &bone.rotQuat[2], sizeof(bone.rotQuat[2]));
            Rd(fd, &bone.rotQuat[3], sizeof(bone.rotQuat[3]));
            Rd(fd, &bone.rotQuat[0], sizeof(bone.rotQuat[0]));
            unsigned char b = 0;
            Rd(fd, &b, 1); // 0x45A4E8
            record->bonePhysicsState[i] = (b == 1) ? 1 : 0;
            Rd(fd, &b, 1); // 0x45A539
            record->boneSelection[i] = (b == 1) ? 1 : 0;
        }
    }
    // Current morph values.
    mdl::MorphRecord* const morphs = model->morphs;
    for (std::int32_t i = 0; i < static_cast<std::int32_t>(record->morphCount); ++i)
        Rd(fd, &morphs[i].value, sizeof(morphs[i].value));
    // Current IK enabled flags.
    mdl::IkChain* const ikChains = model->ikChains;
    for (std::int32_t i = 0; i < static_cast<std::int32_t>(record->ikChainCount); ++i) {
        unsigned char b = 0;
        Rd(fd, &b, 1);
        ikChains[i].enabled = (b == 1) ? 1 : 0;
    }
    LogV1Stage("state-done", _tell(fd));
    EnableMenuItem(GetMenu(main), MDS_CMD_MODEL_DRAW_ORDER, 0);        // 0x45A682
    EnableMenuItem(GetMenu(main), MDS_CMD_MODEL_CALCULATION_ORDER, 0); // 0x45A6A2
    return true;
}

// ---- 0x45A6D3..0x45AA89: register-combo 0x1B1 (5 model-mode entries
// incl. reselect, or 7 camera-mode entries) ----------------------------
static void LoadSceneV1_RegisterCombo(PmmV1LoadContext& ctx) {
    auto* const s = ctx.s;
    HWND const main = ctx.main;
    mdl::ModelRecord** const slots = ctx.slots;

    // ---- register-combo 0x1B1 (0x45A6D3..0x45AA89) -----------------------
    SendMessageA(GetDlgItem(main, panel::kInterpCurveCombo), CB_RESETCONTENT, 0, 0); // 0x45A6D3
    if (s->CameraMode() == 0) {
        // model-edit mode: 5 entries + reselect current model
        if (s->EnglishUI() == 0) {
            SendMessageW(GetDlgItem(main, panel::kInterpCurveCombo), CB_ADDSTRING, 0,
                         reinterpret_cast<LPARAM>(kWXMove)); // 0x45A9A3
            SendMessageW(GetDlgItem(main, panel::kInterpCurveCombo), CB_ADDSTRING, 0,
                         reinterpret_cast<LPARAM>(kWYMove));
            SendMessageW(GetDlgItem(main, panel::kInterpCurveCombo), CB_ADDSTRING, 0,
                         reinterpret_cast<LPARAM>(kWZMove));
            SendMessageW(GetDlgItem(main, panel::kInterpCurveCombo), CB_ADDSTRING, 0,
                         reinterpret_cast<LPARAM>(kWRot));
            SendMessageW(GetDlgItem(main, panel::kInterpCurveCombo), CB_ADDSTRING, 0,
                         reinterpret_cast<LPARAM>(kWAll));
        } else {
            SendMessageA(GetDlgItem(main, panel::kInterpCurveCombo), CB_ADDSTRING, 0,
                         reinterpret_cast<LPARAM>("x axis move"));
            SendMessageA(GetDlgItem(main, panel::kInterpCurveCombo), CB_ADDSTRING, 0,
                         reinterpret_cast<LPARAM>("y axis move"));
            SendMessageA(GetDlgItem(main, panel::kInterpCurveCombo), CB_ADDSTRING, 0,
                         reinterpret_cast<LPARAM>("z axis move"));
            SendMessageA(GetDlgItem(main, panel::kInterpCurveCombo), CB_ADDSTRING, 0,
                         reinterpret_cast<LPARAM>("rotation"));
            SendMessageA(GetDlgItem(main, panel::kInterpCurveCombo), CB_ADDSTRING, 0,
                         reinterpret_cast<LPARAM>("all")); // 0x52D594
        }
        SendMessageA(GetDlgItem(main, panel::kMainComboModel), CB_SETCURSEL, // 0x45AA59
                     slots[s->SelectedModelSlot()]->comboSelIndex, 0);
        PostLoadInit(slots[s->SelectedModelSlot()]); // 0x45AA6F
    } else {
        // camera mode: 7 entries (distance + view angle included)
        if (s->EnglishUI() == 0) {
            SendMessageW(GetDlgItem(main, panel::kInterpCurveCombo), CB_ADDSTRING, 0,
                         reinterpret_cast<LPARAM>(kWXMove)); // 0x45A7FA
            SendMessageW(GetDlgItem(main, panel::kInterpCurveCombo), CB_ADDSTRING, 0,
                         reinterpret_cast<LPARAM>(kWYMove));
            SendMessageW(GetDlgItem(main, panel::kInterpCurveCombo), CB_ADDSTRING, 0,
                         reinterpret_cast<LPARAM>(kWZMove));
            SendMessageW(GetDlgItem(main, panel::kInterpCurveCombo), CB_ADDSTRING, 0,
                         reinterpret_cast<LPARAM>(kWRot));
            SendMessageW(GetDlgItem(main, panel::kInterpCurveCombo), CB_ADDSTRING, 0,
                         reinterpret_cast<LPARAM>(kWDist)); // 0x45A87E
            SendMessageW(GetDlgItem(main, panel::kInterpCurveCombo), CB_ADDSTRING, 0,
                         reinterpret_cast<LPARAM>(kWViewAng));
            SendMessageW(GetDlgItem(main, panel::kInterpCurveCombo), CB_ADDSTRING, 0,
                         reinterpret_cast<LPARAM>(kWAll));
        } else {
            SendMessageA(GetDlgItem(main, panel::kInterpCurveCombo), CB_ADDSTRING, 0,
                         reinterpret_cast<LPARAM>("x axis move"));
            SendMessageA(GetDlgItem(main, panel::kInterpCurveCombo), CB_ADDSTRING, 0,
                         reinterpret_cast<LPARAM>("y axis move"));
            SendMessageA(GetDlgItem(main, panel::kInterpCurveCombo), CB_ADDSTRING, 0,
                         reinterpret_cast<LPARAM>("z axis move"));
            SendMessageA(GetDlgItem(main, panel::kInterpCurveCombo), CB_ADDSTRING, 0,
                         reinterpret_cast<LPARAM>("rotation"));
            SendMessageA(GetDlgItem(main, panel::kInterpCurveCombo), CB_ADDSTRING, 0,
                         reinterpret_cast<LPARAM>("distance")); // 0x52D5A4
            SendMessageA(GetDlgItem(main, panel::kInterpCurveCombo), CB_ADDSTRING, 0,
                         reinterpret_cast<LPARAM>("view angle")); // 0x52D598
            SendMessageA(GetDlgItem(main, panel::kInterpCurveCombo), CB_ADDSTRING, 0,
                         reinterpret_cast<LPARAM>("all")); // 0x52D594
        }
        SendMessageA(GetDlgItem(main, panel::kMainComboModel), CB_SETCURSEL, 0, 0); // 0x45A8DE
    }
    SendMessageA(GetDlgItem(main, panel::kInterpCurveCombo), CB_SETCURSEL, 3, 0); // 0x45AA86
}

// ---- 0x45AA93..0x45ABCC: track re-allocation through the 0x4C6889
// malloc-family helper + defaults + 255 accessory tracks --------------
static void LoadSceneV1_ReallocateTracks(PmmV1LoadContext& ctx) {
    auto* const s = ctx.s;
    D3DRenderer* const wrap = ctx.wrap;
    mdl::AccessoryRecord** const accs = ctx.accs;
    mdl::AccessoryKey** const accTracks = ctx.accTracks;

    // ---- track re-allocation (0x45AA93..0x45ABCC) -------------------------
    ctx.cameraKeys = reinterpret_cast<mdl::CameraKey*>(AllocBytes(kTrackCam)); // 0x45AA93
    s->CameraKeys() = ctx.cameraKeys;
    std::memset(ctx.cameraKeys, 0, kTrackCam);                                 // 0x45AAA6
    ctx.lightKeys = reinterpret_cast<mdl::LightKey*>(AllocBytes(kTrackLight)); // 0x45AAB0
    s->LightKeys() = ctx.lightKeys;
    std::memset(ctx.lightKeys, 0, kTrackLight);
    ctx.selfShadowKeys = reinterpret_cast<mdl::SelfShadowKey*>(AllocBytes(kTrackSel)); // 0x45AACD
    s->ShadowKeys() = ctx.selfShadowKeys;
    std::memset(ctx.selfShadowKeys, 0, kTrackSel);
    auto* const gravityKeys =
        reinterpret_cast<mdl::GravityKey*>(AllocBytes(kTrackShadow)); // 0x45AAEA
    s->GravityKeys() = gravityKeys;
    std::memset(gravityKeys, 0, kTrackShadow);
    ctx.selfShadowKeys[0].mode = wrap->postProcessEnabled ? 1 : 0; // 0x45AB29
    ctx.selfShadowKeys[0].distance = 0.0112500004f;                // flt_52A1D8
    s->state.selfShadowMode = 1;                                   // 0x45AB46
    s->state.selfShadowEnabled = 0;
    gravityKeys[0].noise = 10;
    gravityKeys[0].acceleration = 9.8000002f;                   // 0x52A1DC
    gravityKeys[0].direction[1] = -1.0f;                        // 0x5295E8
    for (std::size_t i = 0; i < mdl::kTimelineKeyCapacity; ++i) // 0x45AB84
        ctx.cameraKeys[i].parentModel = -1;
    for (int i = 0; i < 255; ++i) { // 0x45ABB0
        auto* const keys = reinterpret_cast<mdl::AccessoryKey*>(AllocBytes(kTrackAcc));
        accTracks[i] = keys;
        std::memset(keys, 0, kTrackAcc); // 0x45ABC3
        keys[0].visible = 1;
        keys[0].parentModel = -1;
        keys[0].scale = 1.0f;
        keys[0].opacity = 1.0f;
        accs[i] = nullptr;
    }
}

// ---- 0x45AC10..0x45B090: camera track read (shorter v1 field set) +
// camera misc + frame UI (inverted 0x1BE sense) ------------------------
static void LoadSceneV1_CameraTrack(PmmV1LoadContext& ctx, int fd) {
    auto* const s = ctx.s;
    HWND const main = ctx.main;
    char* const text = ctx.text;
    mdl::CameraKey* const cameraKeys = ctx.cameraKeys;

    // ---- camera track read (0x45AC10..0x45ACF7) ---------------------------
    {
        // v1 camera record: 0x28 bytes, 6x4 interpolation bytes (the
        // 0x45AC96 loop bound is 6, as in v2), flag +0x40, dword +0x44,
        // flag +0x48 (no +0x4C/+0x50 dwords here - those come from the
        // config tail reregister pass).  Shape lives in
        // pmm_io_common.hpp ReadPmmCameraKey.
        ReadPmmCameraKey<PmmStream::V1>(fd, cameraKeys[0]);
        std::int32_t cnt = 0;
        Rd(fd, &cnt, 4); // 0x45AD51
        for (std::int32_t i = 0; i < cnt; ++i) {
            std::int32_t timelineFrame = 0;
            Rd(fd, &timelineFrame, 4);
            ReadPmmCameraKey<PmmStream::V1>(fd, cameraKeys[timelineFrame]);
        }
    }
    LogV1Stage("camtrack-done", _tell(fd));
    // camera misc (0x45AEE7..0x45AF93)
    Rd(fd, &s->state.cameraPosition[0], 4);
    Rd(fd, &s->state.cameraPosition[1], 4);
    Rd(fd, &s->state.cameraPosition[2], 4);
    Rd(fd, &s->state.viewOffsetX, 4);
    Rd(fd, &s->state.viewOffsetY, 4);
    Rd(fd, &s->state.cameraDistance, 4); // 0xA08DC
    Rd(fd, &s->state.cameraPitch, 4);
    Rd(fd, &s->state.cameraYaw, 4);
    Rd(fd, &s->state.cameraRoll, 4);
    {
        unsigned char b = 0;
        Rd(fd, &b, 1); // 0x45AF93
        s->state.cameraPerspective = (b == 1) ? 1 : 0;
    }
    // frame UI (0x45AFB8..0x45B07D); the original converts the fov float
    // with __ftol for both the range and the "%3d" text.
    {
        const int frame = static_cast<int>(s->state.cameraFov); // 0x9E1E8
        // 0x405 = TBM_SETPOS (WM_USER+5, trackbar): wParam 1 = redraw,
        // lParam = position (0x45AFBF..0x45AFCE).
        SendMessageA(GetDlgItem(main, panel::kFovSlider), TBM_SETPOS, 1,
                     frame); // 0x45AFCE
        HWND item = GetDlgItem(main, panel::kFovEdit);
        SendMessageA(item, EM_SETSEL, 0, GetWindowTextLengthA(item));
        sprintf_s(text, 0x100, "%3d", frame); // 0x45B022
        SendMessageA(item, EM_REPLACESEL, 0, reinterpret_cast<LPARAM>(text));
        // v1 checks the box when 0x31C == 0 (v2 uses != 0).
        SendMessageA(GetDlgItem(main, panel::kPerspectiveCheckbox), BM_SETCHECK,
                     s->state.cameraPerspective == 0 ? 1 : 0,
                     0); // 0x45B07A
    }

    LogV1Stage("cammisc-done", _tell(fd));
}

// ---- 0x45B090..0x45B761: light track read + light misc + rgb/direction
// slider UI --------------------------------------------------------------
static void LoadSceneV1_LightTrack(PmmV1LoadContext& ctx, int fd) {
    auto* const s = ctx.s;
    HWND const main = ctx.main;
    char* const text = ctx.text;
    mdl::LightKey* const lightKeys = ctx.lightKeys;

    // ---- light track read (0x45B090..0x45B2D2) ----------------------------
    {
        // Record shape (37 stream bytes) lives in pmm_io_common.hpp
        // ReadPmmLightKey - byte-identical to the v2 reader.
        ReadPmmLightKey(fd, lightKeys[0]);
        std::int32_t cnt = 0;
        Rd(fd, &cnt, 4); // 0x45B185
        for (std::int32_t i = 0; i < cnt; ++i) {
            std::int32_t timelineFrame = 0;
            Rd(fd, &timelineFrame, 4);
            ReadPmmLightKey(fd, lightKeys[timelineFrame]);
        }
    }
    // light misc (0x45B326..0x45B385)
    Rd(fd, s->LightColor() + 0, sizeof(float));
    Rd(fd, s->LightColor() + 1, sizeof(float));
    Rd(fd, s->LightColor() + 2, sizeof(float));
    Rd(fd, s->LightDirection() + 0, sizeof(float));
    Rd(fd, s->LightDirection() + 1, sizeof(float));
    Rd(fd, s->LightDirection() + 2, sizeof(float));

    LogV1Stage("light-done", _tell(fd));
    // rgb/direction slider UI (0x45B399..0x45B74E)
    {
        const int sliderIds[] = {
            panel::kLightColorSliderR, panel::kLightColorSliderG,
            panel::kLightColorSliderB, panel::kLightDirSliderX,
            panel::kLightDirSliderY,   panel::kLightDirSliderZ}; // 0x1C7..0x1CC
        const int editIds[] = {panel::kLightColorEditR, panel::kLightColorEditG,
                               panel::kLightColorEditB, panel::kLightDirEditX,
                               panel::kLightDirEditY,   panel::kLightDirEditZ}; // 0x1CD..0x1D2
        const float values[] = {s->LightColor()[0],     s->LightColor()[1],
                                s->LightColor()[2],     s->LightDirection()[0],
                                s->LightDirection()[1], s->LightDirection()[2]};
        for (int k = 0; k < 6; ++k) {
            const double scale = k < 3 ? 256.0 : 100.0;
            // 0x405 = TBM_SETPOS, same shape as the frame slider.
            SendMessageA(GetDlgItem(main, sliderIds[k]), TBM_SETPOS, 1,
                         static_cast<int>(values[k] * scale)); // 0x45B3AF..
            HWND item = GetDlgItem(main, editIds[k]);
            SendMessageA(item, EM_SETSEL, 0, GetWindowTextLengthA(item));
            if (k < 3)
                sprintf_s(text, 0x100, "%3d",
                          static_cast<int>(values[k] * 256.0)); // 0x45B4DB..
            else
                sprintf_s(text, 0x100, "%+3.1f", values[k]); // 0x45B643..
            SendMessageA(item, EM_REPLACESEL, 0, reinterpret_cast<LPARAM>(text));
        }
    }
}

// ---- 0x45B761..0x45C479: accessory block (retry dialog; cancel ->
// AbortV1Load, retry failure -> close+ResetAppState WITHOUT
// HandleWindowSize) + current accessory UI; false = aborted -------------
static bool LoadSceneV1_AccessoryBlock(PmmV1LoadContext& ctx, int fd) {
    auto* const s = ctx.s;
    HWND const main = ctx.main;
    PathResolutionWorkspace& paths = *ctx.paths;
    D3DRenderer* const wrap = ctx.wrap;
    mdl::AccessoryRecord** const accs = ctx.accs;
    mdl::AccessoryKey** const accTracks = ctx.accTracks;
    mdl::ModelRecord** const slots = ctx.slots;
    char* const text = ctx.text;
    char* const mbPath = ctx.mbPath;
    wchar_t* const wideTmp = ctx.wideTmp;
    wchar_t* const ofnFile = ctx.ofnFile;
    wchar_t* const ofnTitle = ctx.ofnTitle;

    // ---- accessory block (0x45B761..0x45C479) -----------------------------
    Rd(fd, &s->SelectedAccessorySlot(), 1);                                         // 0x45B761
    Rd(fd, &s->DisplayObjectListScrollPosition(), 4);                               // 0x45B774
    SendMessageA(GetDlgItem(main, panel::kAccessoryCombo), CB_RESETCONTENT, 0, 0);  // 0x45B78E
    SendMessageA(GetDlgItem(main, panel::kAttachBoneCombo), CB_RESETCONTENT, 0, 0); // 0x45B7A8
    unsigned char accCount = 0;
    Rd(fd, &accCount, 1);                                                             // 0x45B7BC
    EnableMenuItem(GetMenu(main), MDS_CMD_ACCESSORY_SETTINGS, accCount == 0 ? 1 : 0); // 0x45B7EA
    // shadow name list (0x45B814..0x45B837)
    for (unsigned char i = 0; i < accCount; ++i) {
        Rd(fd, text, 100);
        SendMessageA(GetDlgItem(main, panel::kAccessoryCombo), CB_ADDSTRING, 0,
                     reinterpret_cast<LPARAM>(text));
    }
    LogV1Stage("acc-head-done", _tell(fd), accCount);
    char accName[100];
    for (unsigned char i = 0; i < accCount; ++i) {
        unsigned char accSlot = 0;
        Rd(fd, &accSlot, 1); // 0x45B867
        auto* acc = static_cast<mdl::AccessoryRecord*>(operator new(
            sizeof(mdl::AccessoryRecord))); // 0x45B871
        if (acc != nullptr)
            IdentityCtor(acc); // 0x45B88E
        accs[accSlot] = acc;
        std::memset(accs[accSlot], 0, sizeof(mdl::AccessoryRecord));
        InitAccessoryRecord(accs[accSlot]); // 0x45B8D6
        Rd(fd, accName, 100);               // 0x45B8EA
        Rd(fd, mbPath, 0x100);              // 0x45B901
        ResolveAnsiUserFile(wrap, mbPath, wideTmp, 0x100,
                            paths);                            // 0x45B92B
        if (!LoadAccessoryObject(s, accs[accSlot], wideTmp)) { // 0x45B961
            sprintf_s(text, 0x100,
                      s->EnglishUI() != 0 ? "Cannot open file:%s.\n\nPlease select "
                                            "accessory of %s."
                                          : kJpCannotOpenAcc,
                      accName, accName); // 0x45B9CE
            MessageBoxA(main, text, s->EnglishUI() != 0 ? "open file" : kJpOpenCaption,
                        0);                                                          // 0x45B9ED
            SetCurrentDirectoryW(reinterpret_cast<const wchar_t*>(s->state.exeDir)); // 0x45B9FA
            swprintf_s(ofnFile, 0x100, L"");                                         // 0x45BA11
            OPENFILENAMEW ofn;
            std::memset(&ofn, 0, sizeof(ofn));
            ofn.lStructSize = sizeof(ofn); // 0x45BA22
            ofn.hwndOwner =
                s->state.floatingWindow ? reinterpret_cast<HWND>(s->state.floatingWindow) : main;
            ofn.lpstrFilter = s->EnglishUI() != 0 ? kWFilterAccEn : kWFilterAccJp;
            ofn.lpstrFile = ofnFile;
            ofn.nMaxFile = 0x100;
            ofn.Flags = OFN_FILEMUSTEXIST;
            ofn.lpstrInitialDir =
                (GetMenuState(GetMenu(main), MDS_CMD_TOGGLE_PREVIOUS_FOLDER, 0) & 8)
                    ? reinterpret_cast<LPCWSTR>(s->state.dirAccs)
                    : kWUserAcc;
            ofn.lpstrDefExt = L"x"; // 0x5297D8
            ofn.nMaxFileTitle = 0x100;
            ofn.lpstrFileTitle = ofnTitle;
            ofn.lpstrTitle = s->EnglishUI() != 0 ? L"open file" // 0x52DA00
                                                 : reinterpret_cast<LPCWSTR>(kWOpenFile);
            if (!GetOpenFileNameW(&ofn)) { // 0x45BB0D
                AbortV1Load(s, fd);        // 0x45A7C5
                return false;
            }
            if (GetMenuState(GetMenu(main), MDS_CMD_TOGGLE_PREVIOUS_FOLDER, 0) & 8) {
                ExtractDirFromPath(ofnTitle, ofnFile);                                   // 0x45BB47
                wcscpy_s(reinterpret_cast<wchar_t*>(s->state.dirAccs), 0x3E8, ofnTitle); // 0x45BB59
            }
            if (!LoadAccessoryObject(s, accs[accSlot], ofnFile)) {
                _close(fd);       // 0x45C5D5
                ResetAppState(s); // 0x45C5DF
                // NOTE: no HandleWindowSize on this path (unlike 0x45A7C5).
                return false;
            }
        }

        LogV1Stage("acc-obj-done", _tell(fd), accSlot);
        mdl::AccessoryRecord& accessory = *mdl::Accessory(accs[accSlot]);
        Rd(fd, &accessory.order, 1);                              // 0x45BBB8
        strcpy_s(accessory.name, sizeof accessory.name, accName); // 0x45BBDA
        // accessory track record 0 + sparse keys (0x45BBF3..0x45C16F);
        // record shape (55 stream bytes, transparency quirk included) lives
        // in pmm_io_common.hpp ReadPmmAccessoryKey - byte-identical to v2.
        auto* const accessoryKeys = reinterpret_cast<mdl::AccessoryKey*>(accTracks[accSlot]);
        ReadPmmAccessoryKey(fd, accessoryKeys[0]);
        {
            std::int32_t cnt = 0;
            Rd(fd, &cnt, 4); // 0x45BE2B
            for (std::int32_t k = 0; k < cnt; ++k) {
                std::int32_t timelineFrame = 0;
                Rd(fd, &timelineFrame, 4);
                ReadPmmAccessoryKey(fd, accessoryKeys[timelineFrame]);
            }
        }
        format::PmmAccessoryState diskState;
        if (!format::ReadPmmAccessoryState(ReadPmmFileBytes, &fd, format::PmmVersion::V1,
                                           diskState)) {
            AbortV1Load(s, fd);
            return false;
        }
        accessory.visible = diskState.visible;
        accessory.opacity = diskState.opacity;
        accessory.parentModel = diskState.parentModel;
        accessory.parentBone = diskState.parentBone;
        for (int index = 0; index < 3; ++index) {
            accessory.rotation[index] = diskState.rotation[index];
            accessory.position[index] = diskState.position[index];
        }
        accessory.scale = diskState.scale;
        accessory.shadowEnabled = diskState.shadowEnabled;
    }
    // current accessory selection UI (0x45C357..0x45C479)
    {
        const unsigned char cur = s->SelectedAccessorySlot();
        if (accs[cur] != nullptr) {
            mdl::AccessoryRecord& accessory = *mdl::Accessory(accs[cur]);
            SendMessageA(GetDlgItem(main, panel::kAccessoryCombo), CB_SETCURSEL, // 0x45C390
                         accessory.order, 0);
            const std::int32_t parentSlot = accessory.parentModel;
            mdl::ModelRecord* pm = slots[parentSlot];
            if (pm != nullptr && static_cast<std::int32_t>(pm->boneCount) > 0) {
                for (std::int32_t b = 0; b < static_cast<std::int32_t>(pm->boneCount); ++b) {
                    const mikudancestudio::mdl::BoneRecord& entry = pm->boneTable[b];
                    if (entry.type < mikudancestudio::mdl::BoneType::InertTip ||
                        entry.type == mikudancestudio::mdl::BoneType::FixedAxis)
                        SendMessageA(GetDlgItem(main, panel::kAttachBoneCombo), CB_ADDSTRING, 0,
                                     reinterpret_cast<LPARAM>(entry.name));
                }
            }
            SyncAccessoryEditPanel(s); // 0x45C479
        }
    }

    LogV1Stage("accessories-done", _tell(fd));
    return true;
}

// ---- 0x45C48C..0x45D278: config block head, register radios, checkbox
// bytes, wave/AVI/picture blocks, menu checks, FPS + capture-mode
// switches, physics defaults --------------------------------------------
static bool LoadSceneV1_ConfigBlock(PmmV1LoadContext& ctx, int fd) {
    auto* const s = ctx.s;
    HWND const main = ctx.main;
    PathResolutionWorkspace& paths = *ctx.paths;
    D3DRenderer* const wrap = ctx.wrap;
    char* const text = ctx.text;
    char* const mbPath = ctx.mbPath;

    // ---- config block head (0x45C48C..0x45C553) ---------------------------
    format::PmmTimelineSettings timeline;
    if (!format::ReadPmmTimelineSettings(ReadPmmFileBytes, &fd, timeline))
        return false;
    s->state.currentFrame = timeline.currentFrame;
    s->state.timelineStartFrame = timeline.startFrame;
    s->state.lastRegisteredFrame = timeline.lastRegisteredFrame;
    {
        HWND item = GetDlgItem(main, panel::kCurrentFrameEdit);
        SendMessageA(item, EM_SETSEL, 0, GetWindowTextLengthA(item));
        sprintf_s(text, 0x100, "%d",
                  s->state.currentFrame); // 0x45C501
        SendMessageA(item, EM_REPLACESEL, 0, reinterpret_cast<LPARAM>(text));
    }
    PostModelReload2(s);          // 0x45C52B
    RebuildTimelineTrackPanel(s); // 0x45C532
    HandleWindowSize(s);          // 0x45C539
    ApplyModelComboSelection(s);  // 0x45C540

    // register radios (0x45C553..0x45C766); v1 map: 0 -> 0x1EA, 1 -> 0x1EB,
    // 2 -> none of them checked, 3 -> 0x1ED, 4 -> 0x1EC.
    s->EditMode() = static_cast<ViewportEditMode>(timeline.editMode);
    {
        const ViewportEditMode mode = s->EditMode();
        const int check4 = mode == ViewportEditMode::Rotate ? 1 : 0;
        const int check2 = mode == ViewportEditMode::Move ? 1 : 0;
        SendMessageA(GetDlgItem(main, panel::kBoneSelectRadio), BM_SETCHECK,
                     mode == ViewportEditMode::Bone ? 1 : 0, 0);
        SendMessageA(GetDlgItem(main, panel::kBoxSelectRadio), BM_SETCHECK,
                     mode == ViewportEditMode::BoneBox ? 1 : 0, 0);
        SendMessageA(GetDlgItem(main, panel::kBoneMoveRadio), BM_SETCHECK, check2, 0);
        SendMessageA(GetDlgItem(main, panel::kBoneRotateRadio), BM_SETCHECK, check4, 0);
    }
    {
        ApplyPmmPlaybackOptions(s, main, timeline, text);
        ClearTimelineAndCurveDCs(s); // 0x45CA2F
        format::PmmWaveReference wave;
        if (!format::ReadPmmWaveReference(ReadPmmFileBytes, &fd, wave))
            return false;
        s->state.waveEnabled = wave.enabled;
        std::memcpy(mbPath, wave.pathBytes.data(), wave.pathBytes.size());
        ResolveAnsiUserFile(wrap, mbPath, reinterpret_cast<wchar_t*>(s->state.wavPath), 0x100,
                            paths); // 0x45CA9A
        if (s->state.waveEnabled != 0)
            LoadWaveFile(s); // 0x45CAAA
        format::PmmBackgroundReference avi;
        if (!format::ReadPmmBackgroundReference(ReadPmmFileBytes, &fd,
                                                format::PmmBackgroundKind::Avi, avi))
            return false;
        std::memcpy(mbPath, avi.pathBytes.data(), avi.pathBytes.size());
        ResolveAnsiUserFile(wrap, mbPath, s->AviBackgroundPath(), 0x100, paths); // 0x45CB23
        if (mbPath[0] != 0)
            LoadAviFile(s); // 0x45CB34

        // AVI block (0x45CB54..0x45CBFD); read-before-test on 0x91C.
        // (w1/w2/w3 -> 9E414/9E418/9E41C: the exact local-slot mapping in
        // the Ghidra output is scrambled by frame-base drift, so the v2
        // field order of the three dwords is used.)
        if (s->AviBackgroundEnabled() == 1) {
            s->AviBackgroundEnabled() = avi.enabled;
            s->AviOffsetX() = avi.offsetX;
            s->AviOffsetY() = avi.offsetY;
            s->AviScale() = avi.scale;
            if (s->AviBackgroundEnabled() == 1) {
                CheckMenuItem(GetMenu(main), MDS_CMD_TOGGLE_BACKGROUND_VIDEO, 8);
                AviBgOverlayRefresh(s); // 0x45CBA6
            } else {
                CheckMenuItem(GetMenu(main), MDS_CMD_TOGGLE_BACKGROUND_VIDEO, 0);
            }
        } else {
            s->AviBackgroundEnabled() = 0;
            CheckMenuItem(GetMenu(main), MDS_CMD_TOGGLE_BACKGROUND_VIDEO, 0);
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
        // picture block (0x45CC5F..0x45CD84); same local-slot caveat,
        // v2 field order p1/p2/p3 -> 9E434/9E438/9E43C.
        format::PmmBackgroundReference picture;
        if (!format::ReadPmmBackgroundReference(ReadPmmFileBytes, &fd,
                                                format::PmmBackgroundKind::Picture, picture))
            return false;
        std::memcpy(mbPath, picture.pathBytes.data(), picture.pathBytes.size());
        ResolveAnsiUserFile(wrap, mbPath, s->PictureBackgroundPath(), 0x100, paths); // 0x45CCC4
        s->PictureBackgroundEnabled() = 0;
        if (mbPath[0] != 0)
            LoadBackgroundPicture(s); // 0x45CCDC
        if (s->PictureBackgroundEnabled() == 0) {
            s->PictureBackgroundEnabled() = 0;
            CheckMenuItem(GetMenu(main), MDS_CMD_TOGGLE_BACKGROUND_IMAGE, 0); // 0x45CD7D
        } else {
            s->PictureOffsetX() = picture.offsetX;
            s->PictureOffsetY() = picture.offsetY;
            s->PictureScale() = picture.scale;
            s->PictureBackgroundEnabled() = picture.enabled ? 1 : 0;
            if (picture.enabled != 0) {
                CheckMenuItem(GetMenu(main), 0xE9, 8);
                PicBgOverlayRefresh(s); // 0x45CD4E
            } else {
                CheckMenuItem(GetMenu(main), MDS_CMD_TOGGLE_BACKGROUND_IMAGE, 0); // 0x45CD7D
            }
        }
    }
    format::PmmViewSettings view;
    if (!format::ReadPmmViewSettings(ReadPmmFileBytes, &fd, view))
        return false;
    // 0x31E / 0x31D / 0x918 menu checks (0x45CD95..0x45CF2C)
    {
        const HWND owner =
            s->state.floatingWindow ? reinterpret_cast<HWND>(s->state.floatingWindow) : main;
        unsigned char b = 0;
        b = view.fpsOverlay;
        if (b != 0) {
            s->state.fpsOverlayEnabled = 1;
            CheckMenuItem(GetMenu(main), MDS_CMD_TOGGLE_INFORMATION, 8);
            SendMessageA(GetDlgItem(owner, panel::kInfoCheckbox), BM_SETCHECK, 1, 0);
        } else {
            s->state.fpsOverlayEnabled = 0;
            CheckMenuItem(GetMenu(main), MDS_CMD_TOGGLE_INFORMATION, 0);
            SendMessageA(GetDlgItem(owner, panel::kInfoCheckbox), BM_SETCHECK, 0, 0);
        }
        b = view.groundGrid;
        if (b != 0) {
            s->state.groundGridEnabled = 1;
            CheckMenuItem(GetMenu(main), MDS_CMD_TOGGLE_COORDINATE_AXIS, 8);
            SendMessageA(GetDlgItem(owner, panel::kCoordAxisCheckbox), BM_SETCHECK, 1, 0);
        } else {
            s->state.groundGridEnabled = 0;
            CheckMenuItem(GetMenu(main), MDS_CMD_TOGGLE_COORDINATE_AXIS, 0);
            SendMessageA(GetDlgItem(owner, panel::kCoordAxisCheckbox), BM_SETCHECK, 0, 0);
        }
        b = view.groundShadow;
        s->state.groundShadowEnabled = b ? 1 : 0;
        CheckMenuItem(GetMenu(main), MDS_CMD_TOGGLE_GROUND_SHADOW, b ? 8 : 0); // 0x45CF2C
    }

    LogV1Stage("config-done", _tell(fd));
    // FPS menu (0x45CF3F..0x45D02A); float compares against the 1000.0/30.0
    // double constants at 0x52BA60/0x52BA68.
    s->state.fpsLimit = view.fpsLimit;
    {
        const float v = s->state.fpsLimit;
        if (v == 1000.0f) {
            CheckMenuItem(GetMenu(main), MDS_CMD_FPS_30, 0);
            CheckMenuItem(GetMenu(main), MDS_CMD_FPS_60, 0);
            CheckMenuItem(GetMenu(main), MDS_CMD_FPS_UNLIMITED, 8);
        } else if (v == 30.0f) {
            CheckMenuItem(GetMenu(main), MDS_CMD_FPS_UNLIMITED, 0);
            CheckMenuItem(GetMenu(main), MDS_CMD_FPS_60, 0);
            CheckMenuItem(GetMenu(main), MDS_CMD_FPS_30, 8);
        } else {
            CheckMenuItem(GetMenu(main), MDS_CMD_FPS_30, 0);
            CheckMenuItem(GetMenu(main), MDS_CMD_FPS_UNLIMITED, 0);
            CheckMenuItem(GetMenu(main), MDS_CMD_FPS_60, 8);
        }
    }
    // screen-capture mode (0x45D03D..0x45D1CF)
    s->state.captureMode = static_cast<std::uint32_t>(view.captureMode);
    switch (static_cast<std::int32_t>(s->state.captureMode)) {
    case 0:
        CheckMenuItem(GetMenu(main), MDS_CMD_SCREEN_CAPTURE_OFF, 8);
        CheckMenuItem(GetMenu(main), MDS_CMD_SCREEN_CAPTURE_MODE_1, 0);
        CheckMenuItem(GetMenu(main), MDS_CMD_SCREEN_CAPTURE_MODE_2, 0);
        CheckMenuItem(GetMenu(main), MDS_CMD_SCREEN_CAPTURE_MODE_3, 0);
        break;
    case 1:
        CheckMenuItem(GetMenu(main), MDS_CMD_SCREEN_CAPTURE_OFF, 0);
        CheckMenuItem(GetMenu(main), MDS_CMD_SCREEN_CAPTURE_MODE_1, 8);
        CheckMenuItem(GetMenu(main), MDS_CMD_SCREEN_CAPTURE_MODE_2, 0);
        CheckMenuItem(GetMenu(main), MDS_CMD_SCREEN_CAPTURE_MODE_3, 0);
        break;
    case 2:
        CheckMenuItem(GetMenu(main), MDS_CMD_SCREEN_CAPTURE_OFF, 0);
        CheckMenuItem(GetMenu(main), MDS_CMD_SCREEN_CAPTURE_MODE_1, 0);
        CheckMenuItem(GetMenu(main), MDS_CMD_SCREEN_CAPTURE_MODE_2, 8);
        CheckMenuItem(GetMenu(main), MDS_CMD_SCREEN_CAPTURE_MODE_3, 0);
        break;
    default:
        CheckMenuItem(GetMenu(main), MDS_CMD_SCREEN_CAPTURE_OFF, 0);
        CheckMenuItem(GetMenu(main), MDS_CMD_SCREEN_CAPTURE_MODE_1, 0);
        CheckMenuItem(GetMenu(main), MDS_CMD_SCREEN_CAPTURE_MODE_2, 0);
        CheckMenuItem(GetMenu(main), MDS_CMD_SCREEN_CAPTURE_MODE_3, 8);
        break;
    }
    // physics defaults + combos (0x45D1C8..0x45D265)
    s->state.gravityMagnitude = 9.8000002f; // 0x52A1DC
    s->state.gravityNoiseTimer = 0.0f;
    s->state.gravityX = 0.0f;
    s->state.gravityY = -1.0f; // 0x5295E8
    s->state.gravityNoiseEnabled = 0;
    s->state.gravityNoise = 10;
    s->state.gravityZ = 0.0f;
    s->state.rigidBodyDisplayEnabled = 0;
    s->state.modelOutlineColorRed = 0;
    s->state.modelOutlineColorGreen = 0;
    s->state.modelOutlineColorBlue = 0;
    SendMessageA(GetDlgItem(main, panel::kMainComboNormal), CB_SETCURSEL, 0, 0);   // 0x45D248
    SendMessageA(GetDlgItem(main, panel::kBoneRegisterCombo), CB_SETCURSEL, 0, 0); // 0x45D262

    LogV1Stage("physics-defaults-done", _tell(fd));
    return true;
}

// ---- 0x45D278..0x45E117: read-gated tail.  The original tests every
// _read result here and unwinds early at end-of-stream; the if(Rd>0)
// tower is kept VERBATIM (flattening would change control flow) - the
// [read-gate N] comments mark each level -------------------------------
static void LoadSceneV1_ReadGatedTail(PmmV1LoadContext& ctx, int fd) {
    auto* const s = ctx.s;
    HWND const main = ctx.main;
    mdl::ModelRecord** const slots = ctx.slots;
    mdl::AccessoryRecord** const accs = ctx.accs;
    char* const lbText = ctx.lbText;
    mdl::CameraKey* const cameraKeys = ctx.cameraKeys;
    mdl::SelfShadowKey* const selfShadowKeys = ctx.selfShadowKeys;

    // ---- read-gated tail (0x45D278..0x45E117) ----------------------------
    // Unlike the rest of the body, this chain tests the _read results and
    // unwinds early at end-of-stream.
    // [read-gate 1] end-of-stream unwinds here
    if (Rd(fd, &s->state.accessoryRenderSplitOrder, 4) > 0) { // 0x45D278
        Rd(fd, &s->ProjectedShadowAmbientIntensity(), 4);     // 0x45D296
        s->SetProjectedShadowAmbient(s->ProjectedShadowAmbientIntensity());
        // x64 load twin sub_7FF7CB498E30: every post-read slot walk runs to
        // 255 (0xFF counters at 0x7FF7CB49D33A..0x7FF7CB4A2A3A in the tail).
        for (int i = 0; i < kModelSlotCount; ++i) {
            if (slots[i] != nullptr) {
                unsigned char b = 0;
                Rd(fd, &b, 1); // 0x45D2EA
                slots[i]->postLoadFlag2 = (b == 1) ? 1 : 0;
            }
        }
        if (slots[s->SelectedModelSlot()] != nullptr &&
            slots[s->SelectedModelSlot()]->postLoadFlag2 != 0 && s->CameraMode() == 0)
            SendMessageA(GetDlgItem(main, panel::kAddBlendCheckbox), BM_SETCHECK, 1,
                         0); // 0x45D360
        for (int i = 0; i < 255; ++i) {
            if (accs[i] != nullptr) {
                unsigned char b = 0;
                Rd(fd, &b, 1); // 0x45D38F
                mdl::Accessory(accs[i])->additiveBlend = (b == 1) ? 1 : 0;
            }
        }
        {
            const unsigned char cur = s->SelectedAccessorySlot();
            if (accs[cur] != nullptr && mdl::Accessory(accs[cur])->additiveBlend != 0)
                SendMessageA(GetDlgItem(main, panel::kAccessoryAddBlendCheckbox), BM_SETCHECK, 1,
                             0); // 0x45D3FB
        }
        std::int32_t sharedEdgeScaleBits = 0;
        // [read-gate 2] end-of-stream unwinds here
        if (Rd(fd, &sharedEdgeScaleBits, 4) > 0) { // 0x45D40F
            float sharedEdgeScale;
            std::memcpy(&sharedEdgeScale, &sharedEdgeScaleBits, sizeof sharedEdgeScale);
            for (int i = 0; i < kModelSlotCount; ++i)
                if (slots[i] != nullptr)
                    slots[i]->edgeScale = sharedEdgeScale;
            // Keep this shared byte: a short read carries the preceding value.
            unsigned char shadowBlendOrEdgeScaleFlag = 0;
            Rd(fd, &shadowBlendOrEdgeScaleFlag, 1); // 0x45D44E
            if (shadowBlendOrEdgeScaleFlag == 1) {
                s->state.projectedShadowBlendEnabled = 1;
                CheckMenuItem(GetMenu(main), MDS_CMD_TOGGLE_GROUND_SHADOW_TRANSPARENCY,
                              8); // 0x45D47A
            } else {
                s->state.projectedShadowBlendEnabled = 0;
            }
            const int individualEdgeScaleFlagBytesRead =
                Rd(fd, &shadowBlendOrEdgeScaleFlag, 1); // 0x45D48F
            // [read-gate 3] end-of-stream unwinds here
            if (shadowBlendOrEdgeScaleFlag == 1 && individualEdgeScaleFlagBytesRead > 0) {
                // per-model 0x31C0 re-read pass (0x45D4B0..0x45D4FB):
                // ONE float dword per occupied slot, unlike the broadcast
                // sweep of the first dword above.
                for (int i = 0; i < kModelSlotCount; ++i) {
                    if (slots[i] != nullptr) {
                        std::int32_t edge = 0;
                        Rd(fd, &edge, 4); // 0x45D4D3
                        std::memcpy(&slots[i]->edgeScale, &edge, sizeof edge);
                    }
                }
                unsigned char physicsSectionPresent = 0;
                const int physicsSectionFlagBytesRead =
                    Rd(fd, &physicsSectionPresent, 1); // 0x45D509
                // [read-gate 4] end-of-stream unwinds here
                if (physicsSectionPresent == 1 && physicsSectionFlagBytesRead > 0) {
                    Rd(fd, reinterpret_cast<unsigned char*>(&s->PlaybackPhysicsMode()),
                       1); // 0x45D538
                    // v1: only 0/1/2 update the menus; the default case leaves
                    // them untouched (the v2 body re-checks 0x10E).
                    switch (s->PlaybackPhysicsMode()) {
                    case 0:
                        CheckMenuItem(GetMenu(main), MDS_CMD_PHYSICS_ON_OFF, 0);
                        CheckMenuItem(GetMenu(main), MDS_CMD_PHYSICS_ALWAYS, 0);
                        CheckMenuItem(GetMenu(main), MDS_CMD_PHYSICS_TRACE, 0);
                        CheckMenuItem(GetMenu(main), MDS_CMD_PHYSICS_DISABLED, 8);
                        break;
                    case 1:
                        CheckMenuItem(GetMenu(main), MDS_CMD_PHYSICS_ON_OFF, 0);
                        CheckMenuItem(GetMenu(main), MDS_CMD_PHYSICS_ALWAYS, 8);
                        CheckMenuItem(GetMenu(main), MDS_CMD_PHYSICS_TRACE, 0);
                        CheckMenuItem(GetMenu(main), MDS_CMD_PHYSICS_DISABLED, 0);
                        break;
                    case 2:
                        CheckMenuItem(GetMenu(main), MDS_CMD_PHYSICS_ON_OFF, 8);
                        CheckMenuItem(GetMenu(main), MDS_CMD_PHYSICS_ALWAYS, 0);
                        CheckMenuItem(GetMenu(main), MDS_CMD_PHYSICS_TRACE, 0);
                        CheckMenuItem(GetMenu(main), MDS_CMD_PHYSICS_DISABLED, 0);
                        break;
                    default:
                        break;
                    }

                    // physics reads (0x45D697..0x45D6E3)
                    Rd(fd, &s->state.gravityMagnitude, 4);
                    Rd(fd, &s->state.gravityNoise, 4);
                    Rd(fd, &s->state.gravityX, 4);
                    Rd(fd, &s->state.gravityY, 4);
                    Rd(fd, &s->state.gravityZ, 4);
                    unsigned char gravityNoiseFlag = 0;
                    Rd(fd, &gravityNoiseFlag, 1); // 0x45D6F4
                    s->state.gravityNoiseEnabled = (gravityNoiseFlag == 1) ? 1 : 0;
                    {
                        unsigned char selfShadowSectionPresent = 0;
                        const int selfShadowSectionFlagBytesRead =
                            Rd(fd, &selfShadowSectionPresent, 1); // 0x45D716
                        // [read-gate 5] end-of-stream unwinds here
                        if (selfShadowSectionPresent == 1 && selfShadowSectionFlagBytesRead > 0) {
                            unsigned char selfShadowMode = 0;
                            Rd(fd, &selfShadowMode, 1); // 0x45D743
                            s->state.selfShadowMode = selfShadowMode;
                            s->state.selfShadowEnabled = (selfShadowMode != 0) ? 1 : 0;
                            Rd(fd, &s->state.selfShadowDistance,
                               4); // 0x45D76E
                            selfShadowKeys[0].mode =
                                static_cast<unsigned char>(s->state.selfShadowMode);
                            selfShadowKeys[0].distance = s->state.selfShadowDistance;
                            for (int i = 0; i < kModelSlotCount; ++i) {
                                if (slots[i] != nullptr) {
                                    unsigned char b = 0;
                                    Rd(fd, &b, 1); // 0x45D7BB
                                    slots[i]->toonFlag = (b == 1) ? 1 : 0;
                                }
                            }
                            unsigned char selfShadowKeysPresent = 0;
                            const int selfShadowKeysFlagBytesRead =
                                Rd(fd, &selfShadowKeysPresent, 1); // 0x45D7FE
                            // [read-gate 6] end-of-stream unwinds here
                            if (selfShadowKeysPresent == 1 && selfShadowKeysFlagBytesRead > 0) {
                                // selection/self-shadow track on app+0x37C
                                // (24-byte records); shape lives in
                                // pmm_io_common.hpp ReadPmmSelfShadowKey.
                                ReadPmmSelfShadowKey(fd, selfShadowKeys[0]);
                                std::int32_t cnt = 0;
                                Rd(fd, &cnt, 4); // 0x45D8C7
                                for (std::int32_t i = 0; i < cnt; ++i) {
                                    std::int32_t timelineFrame = 0;
                                    Rd(fd, &timelineFrame, 4);
                                    ReadPmmSelfShadowKey(fd, selfShadowKeys[timelineFrame]);
                                }
                                unsigned char outlineColorSectionPresent = 0;
                                const int outlineColorFlagBytesRead =
                                    Rd(fd, &outlineColorSectionPresent, 1); // 0x45D9EC
                                // [read-gate 7] end-of-stream unwinds here
                                if (outlineColorSectionPresent == 1 &&
                                    outlineColorFlagBytesRead > 0) {
                                    // model color sweep (0x45DA1B..0x45DA91)
                                    Rd(fd, &s->state.modelOutlineColorRed, 4);
                                    Rd(fd, &s->state.modelOutlineColorGreen, 4);
                                    Rd(fd, &s->state.modelOutlineColorBlue, 4);
                                    if (s->state.modelOutlineColorRed != 0 ||
                                        s->state.modelOutlineColorGreen != 0 ||
                                        s->state.modelOutlineColorBlue != 0) {
                                        for (int i = 0; i < kModelSlotCount; ++i)
                                            if (slots[i] != nullptr)
                                                SetModelColor(slots[i],
                                                              s->state.modelOutlineColorRed,
                                                              s->state.modelOutlineColorGreen,
                                                              s->state.modelOutlineColorBlue);
                                    }

                                    unsigned char backgroundSectionPresent = 0;
                                    const int backgroundFlagBytesRead =
                                        Rd(fd, &backgroundSectionPresent, 1); // 0x45DAB4
                                    // [read-gate 8] end-of-stream unwinds here
                                    if (backgroundSectionPresent == 1 &&
                                        backgroundFlagBytesRead > 0) {
                                        unsigned char blackBackgroundFlag = 0;
                                        Rd(fd, &blackBackgroundFlag, 1); // 0x45DAE1
                                        s->state.blackBackgroundEnabled =
                                            (blackBackgroundFlag != 0) ? 1 : 0;
                                        CheckMenuItem(GetMenu(main),
                                                      MDS_CMD_TOGGLE_BLACK_BACKGROUND,
                                                      blackBackgroundFlag ? 8 : 0);
                                        unsigned char cameraParentSectionPresent = 0;
                                        const int cameraParentFlagBytesRead =
                                            Rd(fd, &cameraParentSectionPresent, 1); // 0x45DB35
                                        // [read-gate 9] end-of-stream unwinds here
                                        if (cameraParentSectionPresent == 1 &&
                                            cameraParentFlagBytesRead > 0) {
                                            // camera parent reregister
                                            // (0x45DB67..0x45DC76)
                                            Rd(fd, &cameraKeys[0].parentModel, 4);
                                            Rd(fd, &cameraKeys[0].parentBone, 4);
                                            const std::int32_t parentBoneIndex =
                                                cameraKeys[0].parentBone;
                                            const std::int32_t parentModelIndex =
                                                cameraKeys[0].parentModel;
                                            if (parentBoneIndex >= 0 && parentBoneIndex < 10000 &&
                                                parentModelIndex > -1 && parentModelIndex < 1000) {

                                                std::int32_t cnt = 0;
                                                Rd(fd, &cnt, 4); // 0x45DBD3
                                                for (std::int32_t i = 0; i < cnt; ++i) {
                                                    std::int32_t timelineFrame = 0;
                                                    Rd(fd, &timelineFrame, 4);
                                                    Rd(fd, &cameraKeys[timelineFrame].parentModel,
                                                       4); // 0x45DC14
                                                    Rd(fd, &cameraKeys[timelineFrame].parentBone,
                                                       4); // 0x45DC32
                                                }
                                                Rd(fd, &s->state.cameraParentModel, 4); // 0x45DC59
                                                Rd(fd, &s->state.cameraParentBone, 4);  // 0x45DC6C
                                                const std::int32_t selectedParentModel =
                                                    s->state.cameraParentModel;
                                                const std::int32_t selectedParentBone =
                                                    s->state.cameraParentBone;
                                                if (selectedParentModel >= 0) {
                                                    SendMessageA(
                                                        GetDlgItem(main, panel::kMainComboNormal),
                                                        CB_SETCURSEL,
                                                        slots[selectedParentModel]->comboSelIndex,
                                                        0); // 0x45DC9D
                                                }
                                                RefillBoneRegisterCombo(
                                                    s, selectedParentModel); // 0x45DCAE

                                                if (selectedParentModel >= 0) {
                                                    const LRESULT n = SendMessageA(
                                                        GetDlgItem(main, panel::kBoneRegisterCombo),
                                                        CB_GETCOUNT, 0,
                                                        0); // 0x45DCD5
                                                    for (LRESULT i = 0; i < n; ++i) {
                                                        SendMessageA(
                                                            GetDlgItem(main,
                                                                       panel::kBoneRegisterCombo),
                                                            CB_GETLBTEXT, static_cast<WPARAM>(i),
                                                            reinterpret_cast<LPARAM>(lbText));
                                                        mdl::BoneRecord* entry =
                                                            slots[selectedParentModel]->boneTable +
                                                            static_cast<std::size_t>(
                                                                selectedParentBone);
                                                        if (strcmp(lbText, entry->name) == 0)
                                                            SendMessageA(
                                                                GetDlgItem(
                                                                    main,
                                                                    panel::kBoneRegisterCombo),
                                                                CB_SETCURSEL,
                                                                static_cast<WPARAM>(i),
                                                                0); // 0x45DDD3
                                                    }
                                                }
                                            }
                                            unsigned char cameraAttachmentSectionPresent = 0;
                                            const int cameraAttachmentFlagBytesRead = Rd(
                                                fd, &cameraAttachmentSectionPresent, 1); // 0x45DDFC
                                            // [read-gate 10] end-of-stream unwinds here
                                            if (cameraAttachmentSectionPresent == 1 &&
                                                cameraAttachmentFlagBytesRead > 0) {
                                                // 16 config dwords
                                                // (0x45DE2B..0x45DF4E)
                                                Rd(fd, &s->state.cameraAttachmentBasis[0], 4);
                                                for (int i = 0; i < 15; ++i)
                                                    Rd(fd, &s->state.cameraAttachmentBasis[1 + i],
                                                       4);
                                                unsigned char followCameraFlag = 0;
                                                Rd(fd, &followCameraFlag, 1); // 0x45DF5F
                                                if (followCameraFlag == 1) {
                                                    s->state.followCameraEnabled = 1;
                                                    CheckMenuItem(GetMenu(main),
                                                                  MDS_CMD_TOGGLE_CAMERA_TRACKING,
                                                                  8);
                                                    SendMessageA(
                                                        GetDlgItem(main,
                                                                   panel::kFollowCameraCheckbox),
                                                        BM_SETCHECK, 1, 0);
                                                } else {
                                                    s->state.followCameraEnabled = 0;
                                                    CheckMenuItem(GetMenu(main),
                                                                  MDS_CMD_TOGGLE_CAMERA_TRACKING,
                                                                  0);
                                                    SendMessageA(
                                                        GetDlgItem(main,
                                                                   panel::kFollowCameraCheckbox),
                                                        BM_SETCHECK, 0, 0);
                                                }

                                                unsigned char suppressAttachmentTransformFlag = 0;
                                                Rd(fd, &suppressAttachmentTransformFlag,
                                                   1); // 0x45DFEF
                                                s->state.cameraAttachmentTransformSuppressed =
                                                    (suppressAttachmentTransformFlag == 1) ? 1 : 0;
                                                unsigned char physicsFloorSectionPresent = 0;
                                                const int physicsFloorFlagBytesRead = Rd(
                                                    fd, &physicsFloorSectionPresent, 1); // 0x45E00E
                                                // [read-gate 11] end-of-stream unwinds here
                                                if (physicsFloorSectionPresent == 1 &&
                                                    physicsFloorFlagBytesRead > 0) {
                                                    unsigned char physicsFloorFlag = 0;
                                                    Rd(fd, &physicsFloorFlag, 1); // 0x45E03B
                                                    if (physicsFloorFlag != 0) {
                                                        CheckMenuItem(GetMenu(main),
                                                                      MDS_CMD_TOGGLE_PHYSICS_FLOOR,
                                                                      8);
                                                        s->state.floorVisible = 1;
                                                        s->Physics()
                                                            ->groundBody->setDeactivationTime(1.0f);
                                                    } else {
                                                        CheckMenuItem(GetMenu(main),
                                                                      MDS_CMD_TOGGLE_PHYSICS_FLOOR,
                                                                      0);
                                                        s->state.floorVisible = 0;
                                                        s->Physics()
                                                            ->groundBody->setDeactivationTime(
                                                                -1.0f);
                                                    }
                                                    unsigned char modelOrderSectionPresent = 0;
                                                    const int modelOrderFlagBytesRead =
                                                        Rd(fd, &modelOrderSectionPresent,
                                                           1); // 0x45E0BC
                                                    // [read-gate 12] end-of-stream unwinds here
                                                    if (modelOrderSectionPresent == 1 &&
                                                        modelOrderFlagBytesRead > 0) {
                                                        for (int i = 0; i < kModelSlotCount; ++i)
                                                            if (slots[i] != nullptr)
                                                                Rd(fd, &slots[i]->comboSelIndex2,
                                                                   1); // 0x45E0FE
                                                    }
                                                }
                                            }
                                        }
                                    }
                                }
                            }
                        }
                    }
                }
            }
        }
    }
}

// ---- 0x45E149..0x45E7F7: success tail - shadow-mode gate, light
// direction to the physics scene, window title, the two key-chain
// integrity boxes, combo 0x1B2 population, repaint ----------------------
static void LoadSceneV1_SuccessTail(PmmV1LoadContext& ctx) {
    auto* const s = ctx.s;
    HWND const main = ctx.main;
    D3DRenderer* const wrap = ctx.wrap;
    mdl::ModelRecord** const slots = ctx.slots;
    char* const text = ctx.text;
    char* const lbText = ctx.lbText;
    wchar_t* const wndText = ctx.wndText;

    // ---- success tail (0x45E149..0x45E7F7) --------------------------------
    if (wrap->postProcessEnabled != 0) {
        RefreshSelfShadowPanel(s); // 0x45E149
    } else {
        s->state.selfShadowEnabled = 0;
        s->state.selfShadowMode = 0;
    }
    CheckMenuItem(GetMenu(main), MDS_CMD_TOGGLE_SELF_SHADOW,
                  s->state.selfShadowEnabled != 0 ? 8 : 0);
    if (slots[s->SelectedModelSlot()] != nullptr && slots[s->SelectedModelSlot()]->toonFlag != 0 &&
        s->CameraMode() == 0)
        SendMessageA(GetDlgItem(main, panel::kShadowCheckbox), BM_SETCHECK, 1, 0); // 0x45E1C1
    { // light direction into the physics scene (0x45E1EF..0x45E286)
        float dir[3] = {s->state.gravityX, s->state.gravityY, s->state.gravityZ};
        auto& d3dxApi = d3dx::Get();
        // Preserve the imported runtime's normalization rounding.
        d3dxApi.vec3Normalize(dir, dir);
        const float mag = s->state.gravityMagnitude;
        // The original reaches btDynamicsWorld::setGravity through a vtable
        // slot.  That slot number is an ABI implementation detail and is not
        // valid for the x64 Bullet build.  Keep the same scene state while
        // expressing the intended operation through Bullet's public API.
        PhysicsScene* const physics = s->Physics();
        if (physics != nullptr && physics->world != nullptr) {
            physics->world->setGravity(
                btVector3(dir[0] * mag * 10.0f, dir[1] * mag * 10.0f, dir[2] * mag * 10.0f));
        }
    }
    // Title brand: the port ships as MikuDanceStudio (About-box rename);
    // the same kAppTitleFormat constant serves the v2 load and pmm_save.cpp.
    swprintf_s(wndText, 0x100, kAppTitleFormat, // 0x45E2A4
               reinterpret_cast<const wchar_t*>(s->state.envFileName));
    SetWindowTextW(main, wndText);

    // Display/IK key-chain integrity (x86 model+0x26E8, 0x1C stride)
    // (0x45E2C2..0x45E3C0)
    for (int mi = 0; mi < kModelSlotCount; ++mi) {
        mdl::ModelRecord* m = slots[mi];
        if (m == nullptr)
            continue;
        mdl::DisplayKey* keys = m->displayKeys;
        keys[0].previous = 0;
        std::int32_t cur = keys[0].next;
        if (cur == 0)
            continue;
        std::int32_t prev = 0;
        for (;;) {
            if (static_cast<std::int32_t>(keys[cur].previous) != prev) {
                // The original passes four varargs to a five-placeholder
                // format; the fifth reads stack garbage.  Reproduced.
                sprintf_s(text, 0x100, kJpChainFmtDisp, m->name, keys[prev].frame, keys[prev].frame,
                          keys[prev].frame);
                MessageBoxA(main, text, kJpChainCapPhys, 0);
                keys[prev].next = 0;
                break;
            }
            prev = cur;
            cur = keys[cur].next;
            if (cur == 0)
                break;
        }
    }
    // per-bone key-chain integrity (0x26E0 array, 0x3C stride)
    // (0x45E3D9..0x45E510).  Like the v2 body, the first comparison of
    // each bone chain uses the bone index itself as the expected prev
    // value (0x45E52C reloads ECX from the loop counter before the
    // advance), so a root's first sparse key points back to b.
    for (int mi = 0; mi < kModelSlotCount; ++mi) {
        mdl::ModelRecord* m = slots[mi];
        if (m == nullptr)
            continue;
        if (static_cast<std::int32_t>(m->boneCount) <= 0)
            continue;
        mdl::BoneKey* keys = m->boneKeys;
        for (std::int32_t b = 0; b < static_cast<std::int32_t>(m->boneCount); ++b) {
            keys[b].previous = 0;
            std::int32_t cur = keys[b].next;
            if (cur == 0)
                continue;
            std::int32_t prev = b;
            for (;;) {
                if (static_cast<std::int32_t>(keys[cur].previous) != prev) {
                    const mdl::BoneRecord& frameEntry = m->boneTable[b];
                    sprintf_s(text, 0x100, kJpChainFmtPhys, m->name, frameEntry.name,
                              keys[prev].frame, keys[prev].frame);
                    MessageBoxA(main, text, kJpChainCapDisp, 0);
                    keys[prev].next = 0;
                    break;
                }
                prev = cur;
                cur = keys[cur].next;
                if (cur == 0)
                    break;
            }
        }
    }

    // combo 0x1B2 population (0x45E585..0x45E753)
    {
        const LRESULT sel =
            SendMessageA(GetDlgItem(main, panel::kMainComboModel), CB_GETCURSEL, 0, 0);
        SendMessageA(GetDlgItem(main, panel::kRegisterScopeCombo), CB_RESETCONTENT, 0, 0);
        if (sel == 0) {
            if (s->EnglishUI() == 0) {
                SendMessageW(GetDlgItem(main, panel::kRegisterScopeCombo), CB_ADDSTRING, 0,
                             reinterpret_cast<LPARAM>(kWCamera));
                SendMessageW(GetDlgItem(main, panel::kRegisterScopeCombo), CB_ADDSTRING, 0,
                             reinterpret_cast<LPARAM>(kWLight)); // 0x45E672
                SendMessageW(GetDlgItem(main, panel::kRegisterScopeCombo), CB_ADDSTRING, 0,
                             reinterpret_cast<LPARAM>(kWSelfSh));
                SendMessageW(GetDlgItem(main, panel::kRegisterScopeCombo), CB_ADDSTRING, 0,
                             reinterpret_cast<LPARAM>(kWGravity));
            } else {
                SendMessageA(GetDlgItem(main, panel::kRegisterScopeCombo), CB_ADDSTRING, 0,
                             reinterpret_cast<LPARAM>("camera")); // 0x45E5D8
                SendMessageA(GetDlgItem(main, panel::kRegisterScopeCombo), CB_ADDSTRING, 0,
                             reinterpret_cast<LPARAM>("light"));
                SendMessageA(GetDlgItem(main, panel::kRegisterScopeCombo), CB_ADDSTRING, 0,
                             reinterpret_cast<LPARAM>("s shadow"));
                SendMessageA(GetDlgItem(main, panel::kRegisterScopeCombo), CB_ADDSTRING, 0,
                             reinterpret_cast<LPARAM>("gravity"));
            }
            const LRESULT n =
                SendMessageA(GetDlgItem(main, panel::kAccessoryCombo), CB_GETCOUNT, 0, 0);
            for (LRESULT i = 0; i < n; ++i) { // 0x45E705
                SendMessageA(GetDlgItem(main, panel::kAccessoryCombo), CB_GETLBTEXT,
                             static_cast<WPARAM>(i), reinterpret_cast<LPARAM>(lbText));
                SendMessageA(GetDlgItem(main, panel::kRegisterScopeCombo), CB_ADDSTRING, 0,
                             reinterpret_cast<LPARAM>(lbText));
            }
            SendMessageA(GetDlgItem(main, panel::kRegisterScopeCombo), CB_SETCURSEL, 0, 0);
        } else {
            int found = 0; // 0x45E794
            while (found < kModelSlotCount &&
                   (slots[found] == nullptr ||
                    slots[found]->comboSelIndex != static_cast<unsigned char>(sel)))
                ++found;
            if (found < kModelSlotCount) {
                s->SetSelectedModelSlot(static_cast<unsigned char>(found));
                PostLoadInit(slots[found]);
            }
        }
    }
    // child window refresh (0x45E7AA..0x45E7C4)
    if (s->state.floatingWindow != 0) {
        RefreshSeparateWindowViewport(s); // 0x45E7AA
        InvalidateRect(reinterpret_cast<HWND>(s->state.floatingWindow), nullptr, FALSE);
    }
    if (s->CameraMode() != 0) {
        RefreshLightPanel(s); // 0x45E7C7
        PanelPaint(s);        // 0x45E7CE
    }
    InvalidateRect(main, nullptr, FALSE); // 0x45E7DB
    RelayoutSidebarControls(s);           // 0x45E7E2
    s->PhysicsResetPending() = 1;
    s->state.windowLayoutReady = 1;
    s->ApplyTimelineLightState();
    TraceSceneLightState(s, "pmm-v1-load-tail");
    PostViewRefresh(s); // 0x45E7F7
}

} // namespace

void LoadSceneV1(MMDApp* app, int fd) {
    auto* s = app;
    PmmV1LoadContext ctx(app);

    LoadSceneV1_DisposeAndHeader(ctx, fd); // 0x45916D..0x459630
    const unsigned char modelCount = LoadSceneV1_ModelPrePass(ctx, fd);
    LogV1Stage("pre-pass-done", _tell(fd), modelCount);
    if (modelCount != 0) {
        unsigned char modelIdx = 0;
        for (;;) {
            if (!LoadSceneV1_ModelBlock(ctx, fd))
                return; // AbortV1Load already ran (0x45A7C5)
            if (++modelIdx >= modelCount)
                break; // 0x45A6B4
        }
    }

    LoadSceneV1_RegisterCombo(ctx);    // 0x45A6D3..0x45AA89
    LoadSceneV1_ReallocateTracks(ctx); // 0x45AA93..0x45ABCC
    LogV1Stage("regcombo-done", _tell(fd));
    LoadSceneV1_CameraTrack(ctx, fd); // 0x45AC10..0x45B090
    LoadSceneV1_LightTrack(ctx, fd);  // 0x45B090..0x45B761
    if (!LoadSceneV1_AccessoryBlock(ctx, fd))
        return; // 0x45B761..0x45C479
    LogV1Stage("accessories-done", _tell(fd));
    if (!LoadSceneV1_ConfigBlock(ctx, fd)) {
        AbortV1Load(s, fd);
        return;
    }
    LoadSceneV1_ReadGatedTail(ctx, fd); // 0x45D278..0x45E117
    LogV1Stage("gated-tail-done", _tell(fd));
    _close(fd);                   // 0x45E11D
    LoadSceneV1_SuccessTail(ctx); // 0x45E149..0x45E7F7
    auto session = std::make_unique<format::SceneSession>();
    if (LoadPmmV1RuntimeSession(app, *session))
        app->AttachPortableSceneSession(std::move(session));
#ifdef MIKUDANCESTUDIO_DIAG
    PreparePmmWorldParityReport(app);
    WritePmmWorldParityAtLoad(app);
    WritePmmV1RuntimeParityReport(app);
#endif
}

} // namespace mikudancestudio
