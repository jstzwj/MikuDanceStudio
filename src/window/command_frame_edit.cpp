// ===========================================================================
// CommandDispatch family: control notifications 400..449 (0x47E8A0)
// ===========================================================================
// Cases 400..449 (0x190..0x1C1) of the 368-case switch in CommandDispatch
// (0x47E8A0): editor-panel control notifications - undo/redo gates (0x190/
// 0x191), view/light resets, frame-range edits (0x199/0x19A), physics-flag
// radios, light-colour copy rows (combo 0x1B1), model load/delete dialogs,
// keyframe selection marks, and the accessory/morph combo toggles.
//
// case -> original VA (jumptable 0x48F30C / index byte 0x48F650, verified):
//   400:0x482766  401:0x4828DF  402:0x47EEDF  403:0x47F163  404:0x47F211
//   405:0x47F00B  406:0x47F0B5  407:0x47F29C  408:0x487446  409:default
//   410:default   411:0x47FAE5  412:0x47F9FE  413:0x47FAFA  414:0x47FB0F
//   415:0x4829E4  416:0x48073E  417:default   418:0x480734  419:0x48072A
//   420:0x4834D2  421:0x484973  422:0x485CF2  423:0x4810F8  424:0x488300
//   425:default  426:default  427:default  428:default  429:0x4862E0
//   430:0x481109  431:0x481430  432:0x4816E6  433:def_48F263 434:def_48F27E
//   435:0x47EB7B  436:def_4828AA 437:0x47FCF5  438:0x480651  439:0x47F3AF
//   440:0x48A166  441:0x48A133  442:0x48D759  443:def_48E214 444:0x47F319
//   445:0x47F364  446:0x486392  447:default  448:default  449:def_48E37C
//   ("def_" targets live in the dispatcher's default CBN_SELCHANGE chain
//   (command_dispatch.cpp DefaultSelChangeChain); plain "default" = no-op.)
//
// Case map (id -> behaviour -> original VA):
//   id  hex  VA            behaviour
//   --- ---  ------------  ---------------------------------------------------
//   400 190  0x00482766    undo: gate 0x2F8, dirty 0xA0B0D, UndoModelEdit(model,
//                          &0x980), undo-table 0x26EC[28*cnt] gate -> disable
//                          0x190, cnt==cur gate, enable 0x191, paste-mode
//                          bytes 0x31BC/0x31BD, PanelPaint, SelectionReeval,
//                          SetFocus, undo-flag 0x9EDB5 (falls into default,
//                          no-op for this id)
//   401 191  0x004828DF    redo: mirror of 400 with RedoModelEdit, 0x191/0x190
//                          swapped, SeekModelFrame(model, frame, 0xA0CC4) when
//                          undo-table entry != 1
//   402 192  0x0047EEDF    view reset (camera cluster): 0x2F8 gate; fast path
//                          0x310/0x314/0x318 = 0 + angle -45; full path
//                          (0x308..0x318, angle, 0x30C = 0/10 by byte 0x340,
//                          light 4x4 identity 0xA0438, flag 0xA0478)
//   403 193  0x0047F163    view reset variant: rotY 0x314 = PI
//   404 194  0x0047F211    view reset variant: 0x30C = 0 (no 0x340 gate),
//                          0x310 = -PI/2
//   405 195  0x0047F00B    view reset variant: 0x314 = +PI/2
//   406 196  0x0047F0B5    view reset variant: 0x314 = -PI/2
//   407 197  0x0047F29C    view reset + model reload: fast 0x310 = PI/2; full
//                          path flag 0xA0478 = 0, 0x308/0x30C = 0,
//                          ReloadModels (0x42E640) + PostModelReload
//   408 198  0x00487446    frame-range edits 0x199/0x19A: atol -> frameA/30
//                          (0x9E654, from frame 0x980 or 0x199 by byte
//                          0x9ED99), frameB/30 (0x9E658, 0x19A or 0x9E16C),
//                          0x9E64C copy, IsWindowEnabled snapshot
//                          0x9EB77..0x9EB7D, UpdateBoneFrames, timer/subsystem
//                          calls, timeGetTime copies 0x9EDA8/0x9EDAC; the
//                          byte-0x330-set branch clears 0x330 + StopPlayback
//   411 19B  0x0047FAE5    toggle byte 0x341
//   412 19C  0x0047F9FE    checkbox 0x19C -> byte 0x340 = 1/0 (view mode),
//                          BM_SETCHECK 0x213, ApplyCameraReferenceModeChange(prev) when 0x2F8==0
//   413 19D  0x0047FAFA    toggle byte 0x342
//   414 19E  0x0047FB0F    toggle byte 0x9ED99
//   415 1A9  0x004829E4    frame-range editor: atol of edits 0x1A9/0x1AA ->
//                          from/to; combo text 0x1B2 compared byte-exact
//                          against "全ﾌﾚｰﾑ"/"All frame", "全表情ﾌﾚｰﾑ"/"All
//                          facial", "全ボーンフﾚｰﾑ"/"All bone",
//                          "選択ボーン"/"Sel Bone", "選択表情"/"Sel facial"
//                          (0x2F8==0) or CB_GETCURSEL(0x1B2) dispatch
//                          (0x2F8!=0) -> frame-range selection marks on
//                          model tables 0x26E0/0x26E4/0x26E8 or app tables
//                          0x374/0x378/0x37C/0x380 + accessory blobs (0x384)
//   416 1A0  0x0048073E    selection-mark sync: every marked entry of one
//                          frame table re-marks the equal-frame entry of
//                          the other tables' frame-sorted linked lists
//                          (0x2F8==0: 0x26E0/0x26E4/0x26E8; 0x2F8!=0:
//                          0x374/0x378/0x37C/0x380 + 0x384 blobs)
//   417 1A1  default       no-op
//   418 1A2  0x00480734    0x4312E0(app)
//   419 1A3  0x0048072A    0x430F20(app)
//   420 1A4  0x004834D2    frame copy: count marked entries (model mode
//                          0x9DA28/0x9DA2C/0x9DA30, camera mode
//                          0x9DA34..0x9DA44), track min frame; (re)alloc
//                          copy buffers 0x354/0x358/0x35C (model mode) or
//                          0x360..0x370 (camera mode), copy per-record
//                          data; gate controls 0x1A5/0x1A6 and menu 0xFA
//   421 1A5  0x00484973    frame paste: undo-table entry (0x26EC, 0x1C
//                          stride) + bone-name snapshot 0x26FC/0x2700,
//                          paste loops (model: 0x49D880/0x49F190/0x49F8C0;
//                          camera: 0x410AA0..0x414110), light-record slot
//                          fixup; tail PanelPaint/SelectionReeval/
//                          SeekModelFrame + reload chain when 0x2F8
//   422 1A6  0x00485CF2    reverse paste: same undo setup as 421 (bones only),
//                          then RegisterMirroredBoneKey mirrored insert over the 0x354
//                          records; PanelPaint/SelectionReeval/SeekModelFrame
//   423 1A7  0x004810F8    DeleteMarkedKeyframes(app) + dirty 0xA0B0D
//   424 1A8  0x00488300    frame-edit dialog: dirty 0xBC, locale-gated
//                          DialogBoxParamA (template 0x25E EN / 0x28C JP,
//                          proc sub_44C5D0)
//   425-428     default    no-op
//   429 1AD  0x004862E0    scroll gate: (sidebar-0x54)/26, 0x97C = frame -
//                          count (min 0), PanelPaint, 0x4C2A00(x, ho),
//                          InvalidateRect(6, 0x5F, sidebar-3, 0x92)
//   430 1AE  0x00481109    light colour <- selected frame: CB_GETCURSEL
//                          (0x1B1), first frame with selection mark (0x2F8
//                          gate: 0x374 table 0x54-stride / model 0x26E0
//                          0x3C-stride), copy 4 colour bytes into the 6-row
//                          0x9DA0A block, EnableWindow(0x1AF)
//   431 1AF  0x00481430    light colour -> selected frames (inverse of 430)
//   432 1B0  0x004816E6    light colour reset: rows -> 0x14/0x14/0x6B/0x6B
//   433 1B1  0x0048F263    default chain: CBN_SELCHANGE on the 0x1B1 combo
//                          -> SelectionReeval (command_dispatch.cpp)
//   434 1B2  0x0048F27E    default chain: CB_GETCURSEL(0x1B2) cursor stored
//                          into model 0x4CCF0 (frameRegistrationSelection)
//   435 1B3  0x0047EB7B    load model: OPENFILENAMEW "All Model files
//                          (*.pmd,*.pmx)", menu-0x12D gate, LoadModelFile,
//                          RefreshRequest(-1)
//   436 1B4  0x004828AA    default chain: CBN_SELCHANGE on the model combo
//                          -> ApplyModelComboSelection when the 0xA0B50 gate is clear;
//                          kept as the local case below (equivalent)
//   437 1B5  0x0047FCF5    delete model from combo 0x1B4: confirm box (EN/JP),
//                          dispose model + sub-window, slot fixup (ids
//                          0x2D7C/0x2D7D, bone tables 0x4CCE4, camera tables
//                          0x26E8), combo rebuild, menu 0x120/0x121,
//                          ApplyModelComboSelection
//   438 1B6  0x00480651    clear keyframe selection marks (0x26E0/0x26E4/
//                          0x26E8), RegisterDisplayKeyCurrent, 0x9E16C fixup, PanelPaint
//   439 1B7  0x0047F3AF    checkbox 0x1B7 -> model byte 0x2D8D
//   440 1B8  0x0048A166    toggle model byte 0x37C0
//   441 1B9  0x0048A133    toggle model byte 0x31BE
//   442 1BA  0x0048D759    accessory edit dialog: DialogBoxParamA (template
//                          0x329 EN / 0x328 JP, proc sub_47A3F0), free the
//                          0xA0B1C/0xA0B24/0xA0668 buffers, then
//                          SeekModelFrame + PostLanguageSweep + SelectionReeval
//                          when byte 0xA0664 != 0
//   443 1BB  0x0048E214    default chain: CB_GETCURSEL(0x1BB) -> IK table
//                          model+0x26C0[24*sel+0x12] flag -> CheckRadioButton
//                          0x1BC (set) / 0x1BD (clear)
//   444 1BC  0x0047F319    combo 0x1BB selection -> table model+0x26C0
//                          [24*sel+0x12] = 1
//   445 1BD  0x0047F364    combo 0x1BB selection -> table model+0x26C0
//                          [24*sel+0x12] = 0
//   446 1BE  0x00486392    perspective FOV: byte 0x31C set -> D3DX
//                          MatrixPerspectiveFovLH(fov*pi/180, aspect, 1, 1e5)
//                          via scene vtable 0xB0 + RefreshRequest(-1); clear
//                          -> byte 0x31C = 1 + RefreshRequest(-1)
//   447 1BF  default       no-op
//   448 1C0  default       no-op
//   449 1C1  0x0048E37C    default chain: camera parent model combo -
//                          model lookup by 0x2D7C, 0x1C2 bone-list rebuild,
//                          detach keeps the bone's world position (see
//                          command_dispatch.cpp DefaultSelChangeChain)
//
// Default handler def_47E903 (0x482897): when HIWORD(wParam) == 1
// (CBN_SELCHANGE) lParam is compared against GetDlgItem(hwnd, id) for each
// combo of the fixed chain 0x1B4/0x1BB/0x1D7/0x1C1/0x1C2/0x1DA/0x1DB/0x1F8/
// 0x1FD/0x202/0x207/0x1B1/0x1B2 and the matching handler runs (ported as
// DefaultSelChangeChain in command_dispatch.cpp).  Of this family's ids,
// 433/434/443/449 belong to those combos and are handled there; 436 (0x1B4)
// keeps the equivalent local case above; cases 400/401/440/441 fall into
// the chain with no control match, which is a no-op for them.
//
// Fidelity notes:
//   * Float math follows the original x87 shape (double intermediates,
//     float stores).  Constants: flt_52A1E8 = -45.0f, flt_52A1E4 = 10.0f,
//     flt_52E900 = +PI/2, flt_52E8FC = -PI/2, flt_52B738 = PI,
//     flt_52B9F0 = 4294967296.0 (fild negative fixup), dbl_52BA68 = 30.0,
//     dbl_52BB20 = PI/180, flt_52BB28 = 100000.0.
//   * App/model members are named state fields; the frame-table and
//     colour-row offsets used by this family are declared below as
//     file-local constants.
//
// Reference: ../translated/MikuMikuDance/fcn_0047e8a0.cpp
//   (the translated file covers only cases 200..0xDC; this port follows the
//   IDA decompilation of MikuMikuDance.exe 0x47E8A0 directly)
// =========================================================================//
#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <Windows.h>

#include <commdlg.h>
#include <cstdarg>
#include <cstdint>
#include <new>
#include <cstdio>
#include <cstdlib>
#include <cstring>

#include "mikudancestudio/d3dx_dyn.hpp"
#include "mikudancestudio/accessory_layout.hpp"
#include "mikudancestudio/global_key_layout.hpp"
#include "mikudancestudio/mmd_app.hpp"
#include "mikudancestudio/ported_funcs.hpp"
#include "mikudancestudio/model.hpp"
#include "mikudancestudio/panel_controls.hpp"

namespace mikudancestudio {

// ---------------------------------------------------------------------------
// App-state offsets used by this family, kept file-local (never
// promoted to the shared layout headers).
// ---------------------------------------------------------------------------

// Model-field offsets (model = slot array app+0x780 [byte app+0x910]).
constexpr std::size_t kModelBoneFlag2D8D = 0x2D8D;  // checkbox 0x1B7 gate
constexpr std::size_t kModelSelId2D7C = 0x2D7C;     // combo order byte
constexpr std::size_t kModelSelId2D7D = 0x2D7D;     // combo order byte 2
// Frame-copy/paste buffers (case 420 allocates, 421/422 consume).
// Model mode: 0x354 = bone records (0x54 stride), 0x358 = morph records
// (0x28), 0x35C = camera records (0x18; sub-pointers at +0x0C/+0x14).
// Camera mode: 0x360 (0x48 stride, 0x374 copy), 0x364 (0x1C, 0x378 copy),
// 0x368 (0x0C, 0x37C copy), 0x36C (0x1C, 0x380 copy), 0x370 (0x34,
// accessory copy).

// Selected-frame counts for the copy/paste buffers.
// Model mode (0x2F8 == 0): 0x9DA28 bone / 0x9DA2C morph / 0x9DA30 camera.
// Camera mode (0x2F8 != 0): 0x9DA34 (0x374) / 0x9DA38 (0x378) / 0x9DA3C
// (0x37C) / 0x9DA40 (0x380) / 0x9DA44 (accessories).

using mdl::BoneClipboardRecord;
using mdl::CameraClipboardRecord;
using mdl::DisplayClipboardRecord;
using mdl::GravityClipboardRecord;
using mdl::IkClipboardState;
using mdl::LightClipboardRecord;
using mdl::MorphClipboardRecord;
using mdl::SelectorClipboardState;
using mdl::ShadowClipboardRecord;

template <typename Record>
static void ReplaceClipboardBuffer(Record*& buffer, std::uint32_t count) {
    if (buffer != nullptr) {
        free(buffer);
        buffer = nullptr;
    }
    if (count == 0) {
        return;
    }
    buffer = static_cast<Record*>(malloc(count * sizeof(Record)));
    memset(buffer, 0, count * sizeof(Record));
}

// Porting-era trace under MIKUDANCESTUDIO_STATE_DUMP_DIR (CMake option
// MIKUDANCESTUDIO_DIAG, default OFF); the OFF stub keeps the call sites
// valid and inlines away to nothing.
#ifdef MIKUDANCESTUDIO_DIAG
static void TraceModelPaste(const char* format, ...) {
    char directory[MAX_PATH]{};
    const DWORD length = GetEnvironmentVariableA(
        "MIKUDANCESTUDIO_STATE_DUMP_DIR", directory, MAX_PATH);
    if (length == 0 || length >= MAX_PATH)
        return;
    char path[MAX_PATH]{};
    std::snprintf(path, sizeof(path), "%s\\model_paste_trace.log", directory);
    std::FILE* stream = nullptr;
    if (fopen_s(&stream, path, "ab") != 0 || stream == nullptr)
        return;
    va_list args;
    va_start(args, format);
    std::vfprintf(stream, format, args);
    va_end(args);
    std::fputc('\n', stream);
    std::fclose(stream);
}
#else
static inline void TraceModelPaste(const char*, ...) {}
#endif

// Additional model fields (model = slot array app+0x780 [byte app+0x910]).
// Bone frame record sub-fields used by the copy code (0x3C-stride 0x26E0):
//   +0x1C..+0x24 translation, +0x28..+0x34 scale/colour, +0x39/0x0C..0x18
//   per-channel bytes (see case 420).

// ---------------------------------------------------------------------------
// External targets ported in other translation units (declared here with
// their original VAs; not yet registered in ported_funcs.hpp).
// ---------------------------------------------------------------------------
void RefreshRequest(int area);                     // VA 0x00440AC0 (ui_refresh)
void PanelPaint(MMDApp* app);                      // VA 0x00414610 (ui_panel)
void SelectionReeval(MMDApp* app);                 // VA 0x00430510 (stubs.cpp)
void CopyDirPathW(wchar_t* dest, const wchar_t* src); // VA 0x0042AE20 path copy
void ReloadModels(MMDApp* app);                    // VA 0x0042E640 (timeline_advance.cpp)
int SeekModelFrame(unsigned char* model, int frame, int physicsMode);  // VA 0x004B4260
void SnapshotSelectedKeysForUndo(unsigned char* model, int frame);  // VA 0x004A1510

// ---------------------------------------------------------------------------
// Cross-translation-unit dependencies.
// ---------------------------------------------------------------------------
void UndoModelEdit(unsigned char* model, std::int32_t& frame);  // VA 0x004A1870
                                                            // model undo step
void RedoModelEdit(unsigned char* model, std::int32_t& frame);  // VA 0x004A2490
                                                            // model redo step
void ApplyCameraReferenceModeChange(MMDApp* app, int oldMode);  // VA 0x0041ACD0
void StepFrame(MMDApp* app, bool forward);      // VA 0x00430F20 (fwd) / 0x004312E0
                                                // (back)/0x4312E0
                                                // (ui_frame_step.cpp)
void DeleteMarkedKeyframes(MMDApp* app);                    // VA 0x004316B0
void StopPlayback(MMDApp* app);             // VA 0x004341E0 stop playback
void RegisterDisplayKeyCurrent(unsigned char* model, int frame);// VA 0x0049F480
void DeleteModel(unsigned char* model, int flag);  // VA 0x0040A710 model dispose
void SeekSelectedModelToCurrentFrame(MMDApp* app);                    // VA 0x004220C0
void ApplyModelComboSelection(MMDApp* app);                    // VA 0x0044D940
INT_PTR CALLBACK SelectNavDlgProc(HWND, UINT, WPARAM, LPARAM);  // VA 0x0047A3F0
                                                        // accessory dialog
// Real bodies: ui_refresh.cpp (0x4C2680) and ui_timeline_gfx.cpp
// (0x4C2A00/0x4C2B80); declared centrally in ported_funcs.hpp.
bool WaveStartPlayback(void* obj);               // VA 0x004C2760 (media/wave_audio.cpp)
void WaveSeekAndFeed(void* obj, double seconds);  // VA 0x004C34A0

// Additional stubs defined in src/app/late_ports.cpp (signatures fixed
// there; stubs.cpp must not be touched).
void RefreshLightPanel(MMDApp* app);                    // VA 0x00411070
void RefreshSelfShadowPanel(MMDApp* app);                    // VA 0x00411B90
void ApplyGravityTrack(MMDApp* app);                    // VA 0x00412330
void ApplyAccessoryTrack(MMDApp* app, int idx);           // VA 0x00413120
void SyncAccessoryEditPanel(MMDApp* app);                    // VA 0x004134E0
bool RegisterBoneKey(unsigned char* model, unsigned char* rec, int frameOffset, unsigned char useSelected);  // VA 0x0049D880
void ResetBoneKeyCursor(unsigned char* model);           // VA 0x004A4940
int  PasteAccessoryKeyRecord(MMDApp* app, void* rec, int useSelectedSlot);  // VA 0x00414110
void IdentityCtor(void* obj);                      // VA 0x004C46F0
void* ConstructArrayElements(void* block, std::uint32_t elementSize,
                             std::uint32_t count,
                             void* ctor);       // VA 0x00401150

// Newly-required unported dependencies - file-local stub bodies so the
// call sites below link (stubs.cpp must not be modified).  TODO(port):
// move each body to src/app/late_ports.cpp when the function is ported.
// Real body: src/model/key_registrars.cpp (name-based frame mark).
int MarkKeyTrackRangeByName(unsigned char* model, std::uint32_t from,
                            std::uint32_t to,
                            const char* name);   // VA 0x004A27F0
int RegisterCameraKey(MMDApp* app, const void* rec,
                      int overflowAdvertised);             // VA 0x00410AA0 0x374 paste
int RegisterLightKey(MMDApp* app, const void* rec);      // VA 0x00411900 0x378 paste
int RegisterSelfShadowKey(MMDApp* app, const void* rec);      // VA 0x004120B0 0x37C paste
int RegisterGravityKey(MMDApp* app, const void* rec);      // VA 0x00412DF0 0x380 paste
// FrameRangeDlgProc real body: dialog_procs.cpp (0x0044C5D0).

// ---------------------------------------------------------------------------
// Helpers shared by the family.
// ---------------------------------------------------------------------------

// Active model = slot array at this+0x780 indexed by byte this+0x910
// (sub_47E8A0 pattern; the original re-derives it on every use).
static unsigned char* ActiveModel(MMDApp* app) {
    return app->SelectedModel();
}

// 0x410AA0/0x411900/0x4120B0/0x412DF0 all insert into a 10000-record,
// frame-sorted list.  Record 0 is the list head; +4/+8 are prev/next indices,
// while an unused non-head slot is identified by frame==0.  The original
// functions duplicate this allocator/splice sequence byte for byte for each
// payload shape.
template <typename Key, typename Source, typename Fill>
static int InsertGlobalFrame(MMDApp* app, Key* table,
                             std::uint32_t relativeFrame,
                             const Source& source, Fill fill,
                             int advertisedCapacity = 10000) {
    if (table == nullptr) {
        return 0;
    }

    const std::uint32_t frame =
        relativeFrame + static_cast<std::uint32_t>(app->CurrentFrame());
    std::uint32_t index = 0;
    std::uint32_t next = table[0].next;
    while (next != 0) {
        index = next;
        const Key& candidate = table[index];
        if (candidate.frame >= frame) {
            break;
        }
        next = candidate.next;
    }

    Key& current = table[index];
    if (current.frame == frame) {
        fill(current, source);
        return 1;
    }

    std::uint32_t freeIndex = 1;
    while (freeIndex < 10000 && table[freeIndex].frame != 0) {
        ++freeIndex;
    }
    if (freeIndex >= 10000) {
        char message[0x100];
        if (app->state.englishUI != 0) {
            sprintf_s(message, sizeof(message),
                      "You cannot regist over %dpoint.\n"
                      "Please execute 'delete unused frame'",
                      advertisedCapacity);
            MessageBoxA(app->MainWindow(), message,
                        "register frame", 0);
        } else {
            // 0x52B918 / 0x52B908, original Shift-JIS resources.
            static const char kJpOverflow[] =
                "\x93\x6f\x98\x5e\x83\x7c\x83\x43\x83\x93\x83\x67"
                "\x90\x94\x82\xaa%d\x8c\xc2\x82\xf0\x89\x7a\x82\xa6"
                "\x82\xdc\x82\xb5\x82\xbd\n\x82\xb1\x82\xea\x88\xc8"
                "\x8f\xe3\x82\xcc\x93\x6f\x98\x5e\x82\xcd\x8d\x73"
                "\x82\xa6\x82\xdc\x82\xb9\x82\xf1\n\x81\x75\xcc\xda"
                "\xb0\xd1\x95\xd2\x8f\x57\x81\x76\x82\xcc\x81\x75"
                "\x95\x73\x97\x70\xcc\xda\xb0\xd1\x8d\xed\x8f\x9c"
                "\x81\x76\x82\xf0\x8e\xc0\x8d\x73\x82\xb5\x82\xc4"
                "\x89\xba\x82\xb3\x82\xa2";
            static const char kJpTitle[] =
                "\xcc\xda\xb0\xd1\x93\x6f\x98\x5e";
            sprintf_s(message, sizeof(message), kJpOverflow,
                      advertisedCapacity);
            MessageBoxA(app->MainWindow(), message,
                        kJpTitle, 0);
        }
        return 0;
    }

    Key& added = table[freeIndex];
    if (current.frame < frame) {
        // Append after the last record reached by the walk.
        current.next = freeIndex;
        added.previous = index;
        added.next = 0;
    } else {
        // Insert immediately before current and preserve both neighbours.
        const std::uint32_t previous = current.previous;
        table[previous].next = freeIndex;
        added.previous = previous;
        current.previous = freeIndex;
        added.next = index;
    }
    added.frame = frame;
    fill(added, source);
    if (frame > app->state.lastRegisteredFrame) {
        app->state.lastRegisteredFrame = frame;
    }
    return 1;
}

static void FillCameraFrame(mdl::CameraKey& key,
                            const CameraClipboardRecord& source) {
    key.distance = source.distance;
    std::memcpy(key.eye, source.eye, sizeof(key.eye));
    std::memcpy(key.target, source.target, sizeof(key.target));
    std::memcpy(key.interpolation, source.interpolation,
                sizeof(key.interpolation));
    key.perspective = source.perspective;
    key.fov = source.fov;
    key.selected = 1;
    key.parentModel = source.parentModel;
    key.parentBone = source.parentBone;
}

static void FillLightFrame(mdl::LightKey& key,
                           const LightClipboardRecord& source) {
    std::memcpy(key.direction, source.direction, sizeof(key.direction));
    std::memcpy(key.color, source.color, sizeof(key.color));
    key.selected = 1;
}

static void FillShadowFrame(mdl::SelfShadowKey& key,
                            const ShadowClipboardRecord& source) {
    key.mode = source.mode;
    key.distance = source.distance;
    key.selected = 1;
}

static void FillGravityFrame(mdl::GravityKey& key,
                             const GravityClipboardRecord& source) {
    key.acceleration = source.acceleration;
    std::memcpy(key.direction, source.direction, sizeof(key.direction));
    key.noise = source.noise;
    key.noiseEnabled = source.noiseEnabled;
    key.selected = 1;
}

int RegisterCameraKey(MMDApp* app, const void* rec,
                      int overflowAdvertised) {  // VA 0x00410AA0
    const auto& source = *static_cast<const CameraClipboardRecord*>(rec);
    return InsertGlobalFrame(app, app->CameraKeys(), source.frame, source,
                             FillCameraFrame, overflowAdvertised);
}

int RegisterLightKey(MMDApp* app, const void* rec) {  // VA 0x00411900
    const auto& source = *static_cast<const LightClipboardRecord*>(rec);
    return InsertGlobalFrame(app, app->LightKeys(), source.frame, source,
                             FillLightFrame);
}

int RegisterSelfShadowKey(MMDApp* app, const void* rec) {  // VA 0x004120B0
    const auto& source = *static_cast<const ShadowClipboardRecord*>(rec);
    return InsertGlobalFrame(app, app->ShadowKeys(), source.frame, source,
                             FillShadowFrame);
}

int RegisterGravityKey(MMDApp* app, const void* rec) {  // VA 0x00412DF0
    const auto& source = *static_cast<const GravityClipboardRecord*>(rec);
    return InsertGlobalFrame(app, app->GravityKeys(), source.frame, source,
                             FillGravityFrame);
}

template <typename Key>
static Key* FindGlobalFrame(Key* table, std::uint32_t frame) {
    std::uint32_t index = 0;
    for (;;) {
        Key* rec = &table[index];
        const std::uint32_t value = rec->frame;
        if (value >= frame) {
            return value == frame ? rec : nullptr;
        }
        index = rec->next;
        if (index == 0) {
            return nullptr;
        }
    }
}

// VA 0x00410560: Camera manipulation "register" button backend
//.
void RegisterCameraState(MMDApp* app, int frame) {
    CameraClipboardRecord source{};
    const std::uint32_t absolute = static_cast<std::uint32_t>(frame);
    source.frame =
        absolute - static_cast<std::uint32_t>(app->CurrentFrame());
    std::memcpy(source.eye, app->CameraPosition(), sizeof(source.eye));
    std::memcpy(source.target, app->CameraRotation(), sizeof(source.target));
    source.fov = static_cast<std::int32_t>(
        app->state.cameraFov);
    source.perspective = app->CameraPerspective();
    source.distance = app->CameraDistance();
    source.parentModel = app->CameraParentModel();
    source.parentBone = app->CameraParentBone();

    std::uint8_t defaultInterpolation[4][6]{};
    std::memset(defaultInterpolation[0], 20, 12);
    std::memset(defaultInterpolation[2], 107, 12);
    const bool resetInterpolation =
        SendMessageA(GetDlgItem(app->MainWindow(), panel::kPhysicsFrameCheckbox),
                     BM_GETCHECK, 0, 0) == BST_CHECKED;
    const std::uint8_t (*interpolation)[6] = defaultInterpolation;
    mdl::CameraKey* existing = FindGlobalFrame(app->CameraKeys(), absolute);
    if (existing != nullptr && !resetInterpolation) {
        interpolation = existing->interpolation;
    }
    std::memcpy(source.interpolation, interpolation,
                sizeof(source.interpolation));
    // 满表文案 x64 一比一照抄 600000（0x7FF7CB47B18D sprintf_s 的实参；
    // 表容实为 10000 条，孪生粘贴注册器 0x410AA0/x64 0x7FF7CB47B769 才打
    // 10000 —— 原版自身不一致，按行为基准保留）
    RegisterCameraKey(app, &source, 600000);
}

// VA 0x00411630: Light manipulation "register" button backend
//.
void RegisterLightState(MMDApp* app, int frame) {
    LightClipboardRecord source{};
    source.frame =
        static_cast<std::uint32_t>(frame) -
        static_cast<std::uint32_t>(app->CurrentFrame());
    std::memcpy(source.direction, app->LightDirection(),
                sizeof(source.direction));
    std::memcpy(source.color, app->LightColor(), sizeof(source.color));
    RegisterLightKey(app, &source);
}

static void UnlinkAccessoryKey(mdl::AccessoryKey* keys,
                               std::uint32_t index) {
    mdl::AccessoryKey& key = keys[index];
    keys[key.previous].next = key.next;
    keys[key.next].previous = key.previous;
    key.frame = 0;
    key.previous = 0;
    key.next = 0;
}

template <typename Key>
static void UnlinkGlobalKey(Key* keys, std::uint32_t index) {
    Key& key = keys[index];
    keys[key.previous].next = key.next;
    keys[key.next].previous = key.previous;
    key.frame = 0;
    key.previous = 0;
    key.next = 0;
}

static void ResetCameraRecord(mdl::CameraKey& key, bool head) {
    key.selected = 0;
    key.distance = -45.0f;
    key.eye[0] = 0.0f;
    key.eye[1] = head ? 10.0f : 0.0f;
    key.eye[2] = 0.0f;
    std::memset(key.target, 0, sizeof(key.target));
    key.parentModel = -1;
    key.parentBone = 0;
    std::memset(key.interpolation, 20, 12);
    std::memset(reinterpret_cast<unsigned char*>(key.interpolation) + 12,
                107, 12);
}

static void ResetLightRecord(mdl::LightKey& key, bool head) {
    key.selected = 0;
    if (head) {
        key.direction[0] = -0.5f;
        key.direction[1] = -1.0f;
        key.direction[2] = 0.5f;
        key.color[0] = 0.602f;  // bit-exact: 0x3F1A1CAC, see case 467
        key.color[1] = 0.602f;
        key.color[2] = 0.602f;
    } else {
        std::memset(key.direction, 0,
                    sizeof(key.direction) + sizeof(key.color));
    }
}

static unsigned char DefaultShadowMode(MMDApp* app) {
    D3DRenderer* locale = app->Renderer();
    if (locale != nullptr && locale->postProcessEnabled != 0) {  // 0x1D544
        return 1;
    }
    // Fixed-function fallback: keep the PMM self-shadow mode when the
    // optional post-effect object was unavailable on this device.
    return app->state.selfShadowMode != 0 ? 1 : 0;
}

static void ResetShadowRecord(MMDApp* app, mdl::SelfShadowKey& key) {
    key.selected = 0;
    key.mode = DefaultShadowMode(app) != 0 ? 1 : 0;
    key.distance = 0.01125f;
}

static void ResetGravityRecord(mdl::GravityKey& key) {
    key.selected = 0;
    key.noiseEnabled = 0;
    key.noise = 10;
    key.acceleration = 9.8000002f;
    key.direction[0] = 0.0f;
    key.direction[1] = -1.0f;
    key.direction[2] = 0.0f;
}

static void ResetAccessoryRecord(mdl::AccessoryKey& key) {
    key.selected = 0;
    key.parentBone = 0;
    key.visible = 1;
    key.shadowEnabled = 0;
    key.parentModel = -1;
    std::memset(key.position, 0, sizeof(key.position));
    std::memset(key.rotation, 0, sizeof(key.rotation));
    key.opacity = 1.0f;
    key.scale = 1.0f;
}

// VA 0x004316B0. Camera mode purges the five global track families; model
// mode delegates to the exact 0x4A09E0 model-key deletion backend.
void DeleteMarkedKeyframes(MMDApp* app) {
    if (app == nullptr) {
        return;
    }
    if (app->state.optflag[0] != 0) {
        auto* camera = app->CameraKeys();
        if (camera[0].selected != 0) {
            ResetCameraRecord(camera[0], true);
        }
        for (std::uint32_t i = 1; i < 10000; ++i) {
            if (camera[i].selected != 0) {
                UnlinkGlobalKey(camera, i);
                ResetCameraRecord(camera[i], false);
            }
        }

        auto* light = app->LightKeys();
        if (light[0].selected != 0) {
            ResetLightRecord(light[0], true);
            light[0].next = 0;
        }
        for (std::uint32_t i = 1; i < 10000; ++i) {
            if (light[i].selected != 0) {
                UnlinkGlobalKey(light, i);
                ResetLightRecord(light[i], false);
            }
        }

        auto* shadow = app->ShadowKeys();
        if (shadow[0].selected != 0) {
            ResetShadowRecord(app, shadow[0]);
            shadow[0].next = 0;
        }
        for (std::uint32_t i = 1; i < 10000; ++i) {
            if (shadow[i].selected != 0) {
                UnlinkGlobalKey(shadow, i);
                ResetShadowRecord(app, shadow[i]);
            }
        }

        auto* gravity = app->GravityKeys();
        if (gravity[0].selected != 0) {
            ResetGravityRecord(gravity[0]);
            gravity[0].next = 0;
        }
        for (std::uint32_t i = 1; i < 10000; ++i) {
            if (gravity[i].selected != 0) {
                UnlinkGlobalKey(gravity, i);
                ResetGravityRecord(gravity[i]);
            }
        }

        for (std::uint32_t slot = 0; slot < 255; ++slot) {
            mdl::AccessoryKey* accessory = app->AccessoryKeys(slot);
            if (accessory == nullptr) {
                continue;
            }
            if (accessory[0].selected != 0) {
                ResetAccessoryRecord(accessory[0]);
            }
            for (std::uint32_t i = 1; i < 10000; ++i) {
                if (accessory[i].selected != 0) {
                    UnlinkAccessoryKey(accessory, i);
                    ResetAccessoryRecord(accessory[i]);
                }
            }
        }
    } else {
        unsigned char* model = ActiveModel(app);
        if (model != nullptr)
            DeleteMarkedModelKeys(model,
                      app->state.currentFrame);
    }

    PanelPaint(app);
    SelectionReeval(app);
    if (app->state.optflag[0] != 0) {
        ReloadModels(app);
        RefreshLightPanel(app);
        RefreshSelfShadowPanel(app);
        ApplyGravityTrack(app);
        for (int slot = 0; slot < 255; ++slot) {
            if (app->AccessoryKeys(slot) != nullptr) {
                ApplyAccessoryTrack(app, slot);
            }
        }
        SyncAccessoryEditPanel(app);
    } else {
        unsigned char* model = ActiveModel(app);
        if (model != nullptr) {
            SeekModelFrame(model,
                      app->state.currentFrame,
                      app->PlaybackPhysicsMode());
        }
    }
}

// The view-reset shared tail loc_47EFF6: PostViewRefresh + RefreshRequest(-1)
// (the 0x2F8!=0 fast paths and the gate-skip path all land here).
void ViewResetRefresh(MMDApp* app) {
    PostViewRefresh(app);
    RefreshRequest(-1);
}

void ResetCameraAttachmentBasis(MMDApp* app) {
    D3DMATRIX& basis = app->CameraAttachmentBasis();
    basis = {};
    basis.m[0][0] = 1.0f;
    basis.m[1][1] = 1.0f;
    basis.m[2][2] = 1.0f;
    basis.m[3][3] = 1.0f;
}

// Shared body of cases 402..406 (0x47EEDF / 0x47F163 / 0x47F211 / 0x47F00B /
// 0x47F0B5): `rot` is the per-case rotation constant (flt_52A1E8-adjacent
// table: 0 / PI / -PI/2 / +PI/2 / -PI/2) placed in 0x314 except for case 404
// which places it in 0x310; `gate30C` is false only for 404, whose 0x30C
// store skips the byte-0x340 gate (always 0).  The 0x2F8!=0 fast path stores
// 0x310/0x314/0x318 + angle -45 and jumps straight to loc_47EFF6; the full
// path applies the 0xA0430/0x910/0x330/0x9ED98 gate and resets the light
// matrix (0xA0438) to identity.
void ResetViewVariant(MMDApp* app, float rot, bool gate30C, bool rotIn310) {
    if (app->state.optflag[0] != 0) {
        app->CameraRotation()[0] = rotIn310 ? rot : 0.0f;
        app->CameraRotation()[1] = rotIn310 ? 0.0f : rot;
        app->CameraRotation()[2] = 0.0f;
        app->CameraDistance() = -45.0f;
        ViewResetRefresh(app);
        return;
    }
    const std::int32_t a0430 = app->CameraParentModel();
    const std::uint8_t slot = app->SelectedModelSlot();
    const std::uint8_t esi = app->state.followCameraEnabled;
    const bool c = a0430 >= 0;  // setnl cl
    const bool b = (slot == a0430) &&
                   app->PlaybackActive() == 0 &&
                   esi != 0;  // setz bl / setz al / and / and
    if (c && b) {
        ViewResetRefresh(app);  // test ecx, ebx; jnz loc_47EFF6
        return;
    }
    if (c && esi != 0) {
        app->CameraAttachmentTransformSuppressed() = 1;
    }
    app->ViewOffsetX() = 0.0f;
    app->CameraDistance() = -45.0f;
    if (gate30C) {
        if (app->CameraReferenceMode() ==
            CameraAttachmentReference::SelectedBone) {
            app->ViewOffsetY() = 0.0f;
        } else {
            app->ViewOffsetY() = 10.0f;
        }
    } else {
        app->ViewOffsetY() = 0.0f;
    }
    app->CameraRotation()[0] = rotIn310 ? rot : 0.0f;
    app->CameraRotation()[1] = rotIn310 ? 0.0f : rot;
    app->CameraRotation()[2] = 0.0f;
    ResetCameraAttachmentBasis(app);
    ViewResetRefresh(app);
}

// The frame/30 conversion of case 408 (0x487446): fild (int -> double),
// fadd 4294967296.0 when negative (i.e. unsigned re-interpretation), then
// fdiv by 30.0 - stored as float.
float FrameToSeconds(std::int32_t v) {
    const double d = static_cast<double>(static_cast<std::uint32_t>(v));
    return static_cast<float>(d / 30.0);  // dbl_52BA68
}

// First selected frame in the app+0x374 table (0x54 stride, selection byte
// +0x48); returns -1 when none found within the 0x2710-entry scan (cases
// 430/431/432, 0x2F8 != 0 branch).
std::int32_t FirstSelectedFrame374(MMDApp* app) {
    auto* keys = app->CameraKeys();
    for (std::int32_t i = 0; i < 0x2710; ++i) {
        if (keys[i].selected != 0) {
            return i;
        }
    }
    return -1;
}

template <typename Key>
void SelectGlobalFrameRange(Key* keys, std::uint32_t from,
                            std::uint32_t to) {
    for (std::uint32_t i = 0; i < 10000; ++i) {
        Key& key = keys[i];
        key.selected = 0;
        if (from <= key.frame && key.frame <= to &&
            (i == 0 || key.frame != 0)) {
            key.selected = 1;
        }
    }
}

// First marked bone key; returns -1 when none is present.
std::int32_t FirstSelectedBoneKey(unsigned char* model) {
    mdl::BoneKey* keys = mdl::BoneKeys(model);
    for (std::int32_t i = 0;
         i < static_cast<std::int32_t>(mdl::kBoneKeyCapacity); ++i) {
        if (keys[i].allocated != 0) {
            return i;
        }
    }
    return -1;
}

static void ReadBoneCurveChannel(MMDApp* app, const mdl::BoneKey& key,
                                 std::size_t channel, std::size_t row) {
    app->state.lightA[row] = key.interpolation[channel];
    app->state.lightB[row] = key.interpolation[4 + channel];
    app->state.lightC[row] = key.interpolation[8 + channel];
    app->state.lightD[row] = key.interpolation[12 + channel];
}

static void WriteBoneCurveChannel(MMDApp* app, mdl::BoneKey& key,
                                  std::size_t channel, std::size_t row) {
    key.interpolation[channel] = app->state.lightA[row];
    key.interpolation[4 + channel] = app->state.lightB[row];
    key.interpolation[8 + channel] = app->state.lightC[row];
    key.interpolation[12 + channel] = app->state.lightD[row];
}

static void ResetBoneCurveChannel(mdl::BoneKey& key, std::size_t channel) {
    key.interpolation[channel] = 0x14;
    key.interpolation[4 + channel] = 0x14;
    key.interpolation[8 + channel] = 0x6B;
    key.interpolation[12 + channel] = 0x6B;
}

static void ReadCameraCurveChannel(MMDApp* app, const mdl::CameraKey& key,
                                   std::size_t channel, std::size_t row) {
    app->state.lightA[row] = key.interpolation[0][channel];
    app->state.lightB[row] = key.interpolation[1][channel];
    app->state.lightC[row] = key.interpolation[2][channel];
    app->state.lightD[row] = key.interpolation[3][channel];
}

static void WriteCameraCurveChannel(MMDApp* app, mdl::CameraKey& key,
                                    std::size_t channel, std::size_t row) {
    key.interpolation[0][channel] = app->state.lightA[row];
    key.interpolation[1][channel] = app->state.lightB[row];
    key.interpolation[2][channel] = app->state.lightC[row];
    key.interpolation[3][channel] = app->state.lightD[row];
}

static void ResetCameraCurveChannel(mdl::CameraKey& key,
                                    std::size_t channel) {
    key.interpolation[0][channel] = 0x14;
    key.interpolation[1][channel] = 0x14;
    key.interpolation[2][channel] = 0x6B;
    key.interpolation[3][channel] = 0x6B;
}

// Case-415 combo text compare (the original `repe cmpsb` of exactly `n`
// bytes against the literal, NUL position included - GetWindowTextA only
// zero-terminates, so a shorter edit text fails on the NUL byte).
static bool TextEqN(const char* buf, const char* lit, std::size_t n) {
    return memcmp(buf, lit, n) == 0;
}

// Case-415 frame-range editor combo literals.  The JP strings mix SJIS
// kanji with half-width katakana in the original (0x530BF0 "全ﾌﾚｰﾑ",
// 0x530BD8 "全表情ﾌﾚｰﾑ", 0x530BBC "全ボーンフﾚｰﾑ", 0x530BA4 "選択ボーン",
// 0x530B8C "選択表情").
static const char kJpAllFrame[] = "\x91\x53\xCC\xDA\xB0\xD1";          // 全ﾌﾚｰﾑ
static const char kJpAllFacial[] =
    "\x91\x53\x95\x5C\x8F\xEE\xCC\xDA\xB0\xD1";                       // 全表情ﾌﾚｰﾑ
static const char kJpAllBone[] =
    "\x91\x53\x83\x7B\x81\x5B\x83\x93\xCC\xDA\xB0\xD1";               // 全ボーンフﾚｰﾑ
static const char kJpSelBone[] = "\x91\x49\x91\xF0\xCE\xDE\xB0\xDD";  // 選択ボーン
static const char kJpSelFacial[] = "\x91\x49\x91\xF0\x95\x5C\x8F\xEE";  // 選択表情
// 0x52D354: light-record name "ルート" (root) used by case 420.
static const char kLightNameRoot[] = "\x83\x8B\x81\x5B\x83\x67";

template <typename Key>
static void MarkFrameSelected(Key* keys, std::uint32_t start,
                              std::uint32_t frame) {
    std::uint32_t index = start;
    while (keys[index].frame < frame) {
        const std::uint32_t next = keys[index].next;
        if (next == 0) {
            break;
        }
        index = next;
    }
    if (keys[index].frame == frame) {
        keys[index].selected = 1;
    }
}

template <typename Key>
static void MarkModelFrameAllocated(Key* keys, std::uint32_t start,
                                    std::uint32_t frame) {
    std::uint32_t index = start;
    while (keys[index].frame < frame) {
        const std::uint32_t next = keys[index].next;
        if (next == 0) {
            break;
        }
        index = next;
    }
    if (keys[index].frame == frame) {
        keys[index].allocated = 1;
    }
}

template <typename Key>
static void ClearModelKeyMarks(Key* keys, std::size_t capacity) {
    for (std::size_t i = 0; i < capacity; ++i) {
        keys[i].allocated = 0;
    }
}

template <typename Key>
static void SelectModelFrameRange(Key* keys, std::size_t capacity,
                                  std::uint32_t activeHeadCount,
                                  std::uint32_t from, std::uint32_t to) {
    for (std::size_t i = 0; i < capacity; ++i) {
        Key& key = keys[i];
        key.allocated = 0;
        if (from <= key.frame && key.frame <= to &&
            (i < activeHeadCount || key.frame != 0)) {
            key.allocated = 1;
        }
    }
}

template <typename Key>
static std::uint32_t CountMarkedModelKeys(const Key* keys,
                                          std::size_t capacity,
                                          std::uint32_t& minFrame) {
    std::uint32_t count = 0;
    for (std::size_t i = 0; i < capacity; ++i) {
        const Key& key = keys[i];
        if (key.allocated == 0) {
            continue;
        }
        ++count;
        if (key.frame < minFrame) {
            minFrame = key.frame;
        }
    }
    return count;
}

template <typename Key>
static std::uint32_t CountSelectedKeys(const Key* keys, std::size_t capacity,
                                       std::uint32_t& minFrame) {
    std::uint32_t count = 0;
    for (std::size_t i = 0; i < capacity; ++i) {
        const Key& key = keys[i];
        if (key.selected == 0) {
            continue;
        }
        ++count;
        if (key.frame < minFrame) {
            minFrame = key.frame;
        }
    }
    return count;
}

template <typename Key>
static void ClearSelectedKeys(Key* keys, std::size_t capacity) {
    for (std::size_t i = 0; i < capacity; ++i) {
        keys[i].selected = 0;
    }
}

// ---------------------------------------------------------------------------
// Case-437 strings (Shift-JIS byte-exact).
// 0x530C10: "モデル：%sを削除します\nモデルのフレームデータも全て削除されます\n
//            またはアクセサリでこのモデルに追加するものは全て変更されます\n
//            (この操作は元に戻す事はできません)\n\n削除してもよろしいですか？"
static const char kMsgDelModelJp[] =
    "\x83\x82\x83\x66\x83\x8b\x81\x46"
    "%s"
    "\x82\xF0\x8D\xED\x8F\x9C\x82\xB5\x82\xDC\x82\xB7\n"
    "\x83\x82\x83\x66\x83\x8b\x82\xCC\x83\x74\x83\x8C\x81\x5B\x83\x80"
    "\x83\x66\x81\x5B\x83\x5E\x82\xE0\x91\x53\x82\xC4\x8D\xED\x8F\x9C"
    "\x82\xB3\x82\xEA\x82\xDC\x82\xB7\n"
    "\x82\xDC\x82\xBD\x83\x41\x83\x4E\x83\x5A\x83\x54\x83\x8A\x82\xC5"
    "\x82\xB1\x82\xCC\x83\x82\x83\x66\x83\x8B\x82\xC9\x92\xC7\x8F\x5D"
    "\x82\xB5\x82\xE9\x82\xE0\x82\xCC\x82\xCD\x92\x6E\x96\xCA\x82\xC9"
    "\x92\xC7\x8F\x5D\x82\xB5\x82\xE9\x82\xE6\x82\xA4\x82\xC9\x95\xCF"
    "\x8D\x58\x82\xB3\x82\xEA\x82\xDC\x82\xB7\n"
    "\x28\x82\xB1\x82\xCC\x91\x80\x8D\xEC\x82\xCD\x8C\xB3\x82\xC9"
    "\x96\xDF\x82\xB7\x8E\x96\x82\xCD\x82\xC5\x82\xAB\x82\xDC\x82\xB9"
    "\x82\xF1\x29\n\n"
    "\x8D\xED\x8F\x9C\x82\xB5\x82\xC4\x82\xE0\x82\xE6\x82\xEB\x82\xB5"
    "\x82\xA2\x82\xC5\x82\xB7\x82\xA9\x81\x48";
// 0x530C00: "モデル削除"
static const char kCaptionDelModelJp[] =
    "\x83\x82\x83\x66\x83\x8b\x8d\xed\x8f\x9c";
// 0x530CF0: EN delete-model message.
static const char kMsgDelModelEn[] =
    "Trying to delete Model(%s). \n"
    "All flame data about this model will be deleted too.\n"
    "(This operation cannot undo!!)\n\nAre you OK?";

// Case-435 open-file strings (wide).
// 0x52DCE0: "All Model files(*.pmd,*.pmx)" (used for both locales)
static const wchar_t kFilterModel[] =
    L"All Model files(*.pmd,*.pmx)\x00*.pmd;*.pmx\x00";
// 0x52DCBC: "UserFile\\Model"
static const wchar_t kInitDirModel[] = L"UserFile\\Model";
// 0x52DCAC: "pmd;pmx" def-ext
static const wchar_t kDefExtPmd[] = L"pmd;pmx";
// 0x52DC94: "load model"
static const wchar_t kTitleLoadModel[] = L"load model";
// 0x52DC84: JP title "ファイルを開く"
static const wchar_t kTitleOpenFileJp[] =
    L"\x30D5\x30A1\x30A4\x30EB\x3092\x958B\x304F";

// ---------------------------------------------------------------------------
// Case handlers (extraction only, no rewrite).  Each handler body is the
// verbatim statement sequence of its original case; the single control-flow
// edit is that a case-level `break` (an exit of the dispatch switch below)
// is expressed as `return` here - the call site's `break` restores the same
// switch-exit effect, so the control-flow graph is unchanged.  `break`s that
// belong to loops inside a case body are untouched.  The per-case provenance
// notes (original VAs) moved here together with the code.
// ---------------------------------------------------------------------------

// ------------------------------------------------------------------
// 400 (0x00482766): undo button 0x190.  Gated on byte 0x2F8; the
// paste-undo table model+0x26EC[28*cnt] and the cnt==cur equality both
// disable 0x190 / clear paste-mode byte 0x31BC; 0x191 enabled and mode
// byte 0x31BD = 1, then PanelPaint + SelectionReeval + SetFocus and
// undo-available flag 0x9EDB5.  The original falls through into
// def_47E903, a no-op for this id (control 0x190 != 0x1B4).
// ------------------------------------------------------------------
static void Cmd400_Undo(MMDApp* app, HWND hwnd) {
    if (app->state.optflag[0] != 0) {
        return;  // was: break (switch exit)
    }
    app->SceneModified() = 1;
    unsigned char* model = ActiveModel(app);
    mdl::ModelRecord& record = *mdl::Mdl(model);
    UndoModelEdit(model, app->state.currentFrame);
    const std::int32_t cnt = record.undoState[0];
    if (record.undoRings[0].slots[cnt].operation == 0) {
        EnableWindow(GetDlgItem(hwnd, panel::kUndoButton), FALSE);
        record.undoDirty = 0;
        SetFocus(hwnd);
    }
    if (record.undoState[0] == record.undoState[1]) {
        EnableWindow(GetDlgItem(hwnd, panel::kUndoButton), FALSE);
        record.undoDirty = 0;
        SetFocus(hwnd);
    }
    EnableWindow(GetDlgItem(hwnd, panel::kRedoButton), TRUE);
    record.redoDirty = 1;
    PanelPaint(app);
    SelectionReeval(app);
    SetFocus(hwnd);
    app->PhysicsResetPending() = 1;
}
// ------------------------------------------------------------------
// 401 (0x004828DF): redo button 0x191 - mirror of 400 with
// RedoModelEdit, 0x191/0x190 swapped and mode bytes inverted; when the
// undo-table entry model+0x26EC[28*cnt] != 1, SeekModelFrame is invoked
// (original: thiscall(ecx = model, frame = dword app+0x980,
// app+0xA0CC4) - the stub only takes this + pos).
// ------------------------------------------------------------------
static void Cmd400_Redo(MMDApp* app, HWND hwnd) {
    if (app->state.optflag[0] != 0) {
        return;  // was: break (switch exit)
    }
    app->SceneModified() = 1;
    unsigned char* model = ActiveModel(app);
    mdl::ModelRecord& record = *mdl::Mdl(model);
    RedoModelEdit(model, app->state.currentFrame);
    if (record.undoState[0] == record.undoState[1]) {
        EnableWindow(GetDlgItem(hwnd, panel::kRedoButton), FALSE);
        record.redoDirty = 0;
        SetFocus(hwnd);
    }
    EnableWindow(GetDlgItem(hwnd, panel::kUndoButton), TRUE);
    record.undoDirty = 1;
    PanelPaint(app);
    SelectionReeval(app);
    const std::int32_t cnt = record.undoState[0];
    if (record.undoRings[0].slots[cnt].operation != 1) {
        // original: sub_4B4260(ecx = model, frame = app+0x980,
        // app+0xA0CC4)
        SeekModelFrame(model,
                  app->state.currentFrame,
                  app->PlaybackPhysicsMode());
    }
    SetFocus(hwnd);
    app->PhysicsResetPending() = 1;
}
// ------------------------------------------------------------------
// 407 (0x0047F29C): view reset + model reload.  Fast path (0x2F8!=0):
// 0x310 = +PI/2, 0x314/0x318 = 0, angle -45, refresh.  Full path:
// gate (slot==0xA0430 && 0x330==0 && 0x9ED98!=0 && 0xA0430>=0 -> skip
// the reset, only refresh), else flag 0xA0478 = 0, 0x308/0x30C = 0,
// ReloadModels (0x42E640) + PostModelReload, refresh.
// ------------------------------------------------------------------
static void Cmd400_ViewResetReload(MMDApp* app) {
    if (app->state.optflag[0] != 0) {
        app->CameraRotation()[0] = 1.5707964f;
        app->CameraRotation()[1] = 0.0f;
        app->CameraRotation()[2] = 0.0f;
        app->CameraDistance() = -45.0f;
        ViewResetRefresh(app);
        return;  // was: break (switch exit)
    }
    const std::int32_t a0430 = app->CameraParentModel();
    const std::uint8_t slot = app->SelectedModelSlot();
    const bool hit = (slot == a0430) &&
                     app->PlaybackActive() == 0 &&
                     app->state.followCameraEnabled != 0 &&
                     a0430 >= 0;
    if (!hit) {
        app->CameraAttachmentTransformSuppressed() = 0;
        app->ViewOffsetX() = 0.0f;
        app->ViewOffsetY() = 0.0f;
        ReloadModels(app);  // 0x42E640
        PostModelReload(app);
    }
    ViewResetRefresh(app);
}
// ------------------------------------------------------------------
// 408 (0x00487446): play-range edits.  When byte 0x330 != 0: clear it,
// StopPlayback, and (byte 0xA06CC != 0) 0x4C2680/0x4C2760 on the 0xCC
// subsystem.  Otherwise read the 0x199 (start) / 0x19A (end) edits via
// atol; frameA/30 (0x9E654) comes from dword 0x980 when byte 0x9ED99
// != 0 (0x9EDB6 = 1) or from the 0x199 edit (0x9EDB6 = 1 only when the
// edit EQUALS 0x980 - x64 0x7FF7CB46B6DA cmp/jnz; the x86 jnz/jz was
// transcribed inverted here before); frameB/30 (0x9E658) from the 0x19A edit
// (fallback dword 0x9E16C when 0); 0x9E64C = frameA/30; snapshot
// IsWindowEnabled of 0x1F1/0x1F2/0x1AF/0x1A5/0x1A6/0x190/0x191 into
// 0x9EB77..0x9EB7D; byte 0x330 = 1; UpdateBoneFrames; then
// KillTimer(0x64) + 0x4C2680/0x4C2760 when 0xA02B6, and 0x4C2B80 +
// WaveSeekAndFeed((double)0x9E654) when 0xA03E9 == 0 (both gated on 0xA06CC);
// finally timeGetTime copies 0x9EDA8/0x9EDAC.
// ------------------------------------------------------------------
static void Cmd400_PlayRangeEdits(MMDApp* app, HWND hwnd) {
    if (app->PlaybackActive() != 0) {
        app->PlaybackActive() = 0;
        StopPlayback(app);
        if (app->WaveEnabled() != 0) {
            CloseDataFile(app->Audio());
            WaveStartPlayback(app->Audio());
        }
        return;  // was: break (switch exit)
    }
    char text[0x100];
    GetWindowTextA(GetDlgItem(hwnd, panel::kPlayStartFrameEdit), text, 8);
    const std::int32_t start = atol(text);  // ebx
    GetWindowTextA(GetDlgItem(hwnd, panel::kPlayStopFrameEdit), text, 8);
    std::int32_t end = atol(text);  // eax
    if (app->PlaybackStartsAtCurrentFrame() != 0) {
        app->PlaybackStartSeconds() = FrameToSeconds(
            app->state.currentFrame);
        app->PlaybackFrameChanged() = 1;
    } else {
        app->PlaybackStartSeconds() = FrameToSeconds(start);
        // x64 0x7FF7CB46B6DA: cmp start, currentFrame; jnz skip ->
        // the changed-flag is set only when the typed start frame
        // EQUALS the current frame (not "differs").
        if (start == app->state.currentFrame) {
            app->PlaybackFrameChanged() = 1;
        }
    }
    if (end == 0) {
        end = app->state.lastRegisteredFrame;
    }
    app->PlaybackEndSeconds() = FrameToSeconds(end);
    app->PlaybackCursorSeconds() = app->PlaybackStartSeconds();
    app->state.playbackEnabledSnapshot[0] =
        IsWindowEnabled(GetDlgItem(hwnd, panel::kBonePasteButton)) ? 1 : 0;
    app->state.playbackEnabledSnapshot[1] =
        IsWindowEnabled(GetDlgItem(hwnd, panel::kBoneReversePasteButton)) ? 1 : 0;
    app->state.playbackEnabledSnapshot[4] =
        IsWindowEnabled(GetDlgItem(hwnd, panel::kCurvePasteButton)) ? 1 : 0;
    app->state.playbackEnabledSnapshot[2] =
        IsWindowEnabled(GetDlgItem(hwnd, panel::kPasteButton)) ? 1 : 0;
    app->state.playbackEnabledSnapshot[3] =
        IsWindowEnabled(GetDlgItem(hwnd, panel::kReversePasteButton)) ? 1 : 0;
    app->state.playbackEnabledSnapshot[5] =
        IsWindowEnabled(GetDlgItem(hwnd, panel::kUndoButton)) ? 1 : 0;
    app->state.playbackEnabledSnapshot[6] =
        IsWindowEnabled(GetDlgItem(hwnd, panel::kRedoButton)) ? 1 : 0;
    app->PlaybackActive() = 1;
    UpdateBoneFrames(app);  // 0x433A40
    if (app->WaveEnabled() != 0) {
        if (app->AudioSeekReady() != 0) {
            app->AudioSeekReady() = 0;
            KillTimer(hwnd, 0x64);
            CloseDataFile(app->Audio());
            WaveStartPlayback(app->Audio());
        }
        if (app->AutomaticFrameAdvanceEnabled() == 0) {
            SetFrameNormalized(
                app->FrameNormalization());
            WaveSeekAndFeed(app->Audio(),
                      static_cast<double>(app->PlaybackStartSeconds()));
        }
    }
    app->PlaybackClockAnchorLow() = app->TimeNowLow();
    app->PlaybackClockAnchorHigh() = app->TimeNowHigh();
}
// ------------------------------------------------------------------
// 415 (0x004829E4): frame-range editor.  Edits 0x1A9/0x1AA hold the
// from/to frames (atol); 0x1B2 holds the target text/combo.  With
// byte 0x2F8 == 0 the 0x1B2 text is compared byte-exact (repe cmpsb
// over N bytes incl. the NUL) against "全ﾌﾚｰﾑ"/"All frame" (bone+morph+
// camera sweep), "全表情ﾌﾚｰﾑ"/"All facial" (morph), "全ボーンフﾚｰﾑ"/
// "All bone" (bone), "選択ボーン"/"Sel Bone" and "選択表情"/"Sel facial"
// (0x4A27F0 per selected item), else 0x4A27F0 with the raw text.
// With 0x2F8 != 0 the 0x1B2 combo cursor selects the app-level table
// (0x374/0x378/0x37C/0x380 or the accessory slot whose byte +0x49D
// equals cursor-4, scanning its 0x384 blob).  Every sweep clears the
// mark byte first, then sets it when from <= frame <= to (unsigned)
// and (first entry or frame != 0); the model-mode bone/morph sweeps
// additionally require (counter < count || frame != 0).  Tail:
// PanelPaint + SelectionReeval + SetFocus.
// ------------------------------------------------------------------
static void Cmd400_FrameRangeSelect(MMDApp* app, HWND hwnd) {
    char text[0x100];
    GetWindowTextA(GetDlgItem(hwnd, panel::kRangeStartEdit), text, 8);
    const std::int32_t from = atol(text);   // ebx
    GetWindowTextA(GetDlgItem(hwnd, panel::kRangeStopEdit), text, 8);
    const std::int32_t to = atol(text);     // var_A40
    GetWindowTextA(GetDlgItem(hwnd, panel::kRegisterScopeCombo), text, 20);
    if (app->state.optflag[0] == 0) {
        // ---- name-compare chain (loc_482CF2..loc_4831CB) ----
        if (TextEqN(text, kJpAllFrame, 7) || TextEqN(text, "All frame", 10)) {
            // bone sweep (loc_482D40..482DB2)
            {
                unsigned char* model = ActiveModel(app);
                SelectModelFrameRange(
                    mdl::BoneKeys(model), mdl::kBoneKeyCapacity,
                    mdl::Mdl(model)->boneCount,
                    static_cast<std::uint32_t>(from),
                    static_cast<std::uint32_t>(to));
            }
            // morph sweep (loc_482DE0..482E52, gated on 0x2D80 > 0)
            {
                unsigned char* model = ActiveModel(app);
                const std::uint32_t morphCount =
                    mdl::Mdl(model)->morphCount;
                if (morphCount > 0) {
                    SelectModelFrameRange(
                        mdl::MorphKeys(model), 20000, morphCount,
                        static_cast<std::uint32_t>(from),
                        static_cast<std::uint32_t>(to));
                }
            }
            // camera sweep (loc_482E60..482ED8)
            {
                unsigned char* model = ActiveModel(app);
                SelectModelFrameRange(
                    mdl::DisplayKeys(model), 1000, 1,
                    static_cast<std::uint32_t>(from),
                    static_cast<std::uint32_t>(to));
            }
        } else if (TextEqN(text, kJpAllFacial, 11) ||
                   TextEqN(text, "All facial", 11)) {
            // morph sweep only (loc_482F30..482FA2)
            unsigned char* model = ActiveModel(app);
            const std::uint32_t morphCount = mdl::Mdl(model)->morphCount;
            if (morphCount > 0) {
                SelectModelFrameRange(
                    mdl::MorphKeys(model), 20000, morphCount,
                    static_cast<std::uint32_t>(from),
                    static_cast<std::uint32_t>(to));
            }
        } else if (TextEqN(text, kJpAllBone, 13) ||
                   TextEqN(text, "All bone", 9)) {
            // bone sweep only (loc_482FF2..483064)
            unsigned char* model = ActiveModel(app);
            SelectModelFrameRange(
                mdl::BoneKeys(model), mdl::kBoneKeyCapacity,
                mdl::Mdl(model)->boneCount,
                static_cast<std::uint32_t>(from),
                static_cast<std::uint32_t>(to));
        } else if (TextEqN(text, kJpSelBone, 9) ||
                   TextEqN(text, "Sel Bone", 9)) {
            // selected-bone sweep (loc_4830D0..48310F): 0x4A27F0 per
            // selected bone (0x2D94 byte array), name at 0x26BC +
            // i*0x25C
            unsigned char* model = ActiveModel(app);
            const std::int32_t boneCount = mdl::Mdl(model)->boneCount;
            if (boneCount > 0) {
                const mdl::ModelRecord& record = *mdl::Mdl(model);
                unsigned char* sel = record.boneSelection;
                const mdl::BoneRecord* bones = record.boneTable;
                for (std::int32_t i = 0; i < boneCount; ++i) {
                    if (sel[i] != 0) {
                        MarkKeyTrackRangeByName(
                            model, static_cast<std::uint32_t>(from),
                            static_cast<std::uint32_t>(to),
                            bones[i].name);
                    }
                }
            }
        } else if (TextEqN(text, kJpSelFacial, 9) ||
                   TextEqN(text, "Sel facial", 11)) {
            // selected-morph sweep (loc_483173..4831B2): morph table
            // 0x26DC, 0x2E stride, selected byte +0x2C, count byte
            // 0x2DAC
            unsigned char* model = ActiveModel(app);
            const mdl::ModelRecord& record = *mdl::Mdl(model);
            const std::uint8_t morphCount = record.facialFrameCount;
            if (morphCount != 0) {
                mdl::FrameGroup* groups = record.displayFrames;
                for (std::uint8_t i = 0; i < morphCount; ++i) {
                    mdl::FrameGroup& group = groups[i];
                    if (group.selected != 0) {
                        MarkKeyTrackRangeByName(
                            model, static_cast<std::uint32_t>(from),
                            static_cast<std::uint32_t>(to), group.name);
                    }
                }
            }
        } else {
            // fallback (loc_4831B6..4831CB): treat the text as a name
            unsigned char* model = ActiveModel(app);
            MarkKeyTrackRangeByName(model, static_cast<std::uint32_t>(from),
                                    static_cast<std::uint32_t>(to), text);
        }
        PanelPaint(app);
        SelectionReeval(app);
        SetFocus(hwnd);
    } else {
        // ---- 0x2F8 != 0: CB_GETCURSEL(0x1B2) dispatch ----
        // (loc_482A92..loc_482CED)
        const LRESULT sel =
            SendMessageA(GetDlgItem(hwnd, panel::kRegisterScopeCombo), CB_GETCURSEL,
                         0, 0);
        if (sel == 0) {
            SelectGlobalFrameRange(
                app->CameraKeys(),
                static_cast<std::uint32_t>(from),
                static_cast<std::uint32_t>(to));
        } else if (sel == 1) {
            SelectGlobalFrameRange(
                app->LightKeys(),
                static_cast<std::uint32_t>(from),
                static_cast<std::uint32_t>(to));
        } else if (sel == 2) {
            SelectGlobalFrameRange(
                app->ShadowKeys(),
                static_cast<std::uint32_t>(from),
                static_cast<std::uint32_t>(to));
        } else if (sel == 3) {
            SelectGlobalFrameRange(
                app->GravityKeys(),
                static_cast<std::uint32_t>(from),
                static_cast<std::uint32_t>(to));
        } else {
            // accessory slot scan (loc_482C54..loc_482CED): the slot
            // whose byte +0x49D equals cursor-4; its AccessoryKey
            // timeline is swept.
            for (std::int32_t slot = 0; slot < 0xFF; ++slot) {
                mdl::AccessoryRecord* acc = app->AccessorySlot(slot);
                if (acc != nullptr &&
                    static_cast<unsigned int>(acc->order) ==
                        static_cast<unsigned int>(sel - 4)) {
                    auto* keys = app->AccessoryKeys(slot);
                    SelectGlobalFrameRange(
                        keys, static_cast<std::uint32_t>(from),
                        static_cast<std::uint32_t>(to));
                    break;
                }
            }
        }
        PanelPaint(app);
        SelectionReeval(app);
        SetFocus(hwnd);
    }
}
// ------------------------------------------------------------------
// 416 (0x0048073E): selection-mark synchronisation.  For every entry
// of each frame table whose selection mark is set, the equal-frame
// entry of the *other* tables is marked via their frame-sorted linked
// lists (frame dword +0, next index +8; walks start at the current
// index for the model-mode bone/morph searches and at the head
// otherwise).  0x2F8 == 0: tables 0x26E0 (+0x38) / 0x26E4 (+0x10) /
// 0x26E8 (+0x14), camera entries only propagate into bone+morph.
// 0x2F8 != 0: app tables 0x374 (+0x48) / 0x378 (+0x24) / 0x37C
// (+0x14) / 0x380 (+0x21) plus the 0x384 accessory blobs (+0x18),
// then every marked blob entry re-marks the four app tables and the
// other blobs.  Tail: PanelPaint + SelectionReeval (no SetFocus).
// ------------------------------------------------------------------
static void Cmd400_SelectionMarkSync(MMDApp* app) {
    if (app->state.optflag[0] == 0) {
        // ---- model mode (loc_480D1D..loc_4810F3) ----
        unsigned char* model = ActiveModel(app);
        mdl::ModelRecord* modelRecord = mdl::Mdl(model);
        mdl::BoneKey* boneKeys = mdl::BoneKeys(model);
        mdl::MorphKey* morphKeys = mdl::MorphKeys(model);
        mdl::DisplayKey* displayKeys = mdl::DisplayKeys(model);

        // loop A: bone marks -> bone/morph/camera
        for (std::uint32_t keyIndex = 0; keyIndex < static_cast<std::uint32_t>(mdl::kBoneKeyCapacity); ++keyIndex) {
            if (boneKeys[keyIndex].allocated != 0) {
                const std::uint32_t frame = boneKeys[keyIndex].frame;
                if (modelRecord->boneCount > 0) {
                    for (std::uint32_t i = 0; i < modelRecord->boneCount;
                         ++i) {
                        MarkModelFrameAllocated(boneKeys, i, frame);
                    }
                }
                if (modelRecord->morphCount > 0) {
                    for (std::uint32_t i = 0; i < modelRecord->morphCount;
                         ++i) {
                        MarkModelFrameAllocated(morphKeys, i, frame);
                    }
                }
                MarkModelFrameAllocated(displayKeys, 0, frame);
            }
        }
        // loop B: morph marks -> bone/morph/camera
        for (std::uint32_t keyIndex = 0; keyIndex < 20000; ++keyIndex) {
            if (morphKeys[keyIndex].allocated != 0) {
                const std::uint32_t frame = morphKeys[keyIndex].frame;
                if (modelRecord->boneCount > 0) {
                    for (std::uint32_t i = 0; i < modelRecord->boneCount;
                         ++i) {
                        MarkModelFrameAllocated(boneKeys, i, frame);
                    }
                }
                if (modelRecord->morphCount > 0) {
                    for (std::uint32_t i = 0; i < modelRecord->morphCount;
                         ++i) {
                        MarkModelFrameAllocated(morphKeys, i, frame);
                    }
                }
                MarkModelFrameAllocated(displayKeys, 0, frame);
            }
        }
        // loop C: camera marks -> bone/morph only
        for (std::uint32_t keyIndex = 0; keyIndex < 1000; ++keyIndex) {
            if (displayKeys[keyIndex].allocated != 0) {
                const std::uint32_t frame = displayKeys[keyIndex].frame;
                if (modelRecord->boneCount > 0) {
                    for (std::uint32_t i = 0; i < modelRecord->boneCount;
                         ++i) {
                        MarkModelFrameAllocated(boneKeys, i, frame);
                    }
                }
                if (modelRecord->morphCount > 0) {
                    for (std::uint32_t i = 0; i < modelRecord->morphCount;
                         ++i) {
                        MarkModelFrameAllocated(morphKeys, i, frame);
                    }
                }
            }
        }
        PanelPaint(app);
        SelectionReeval(app);
    } else {
        // ---- camera mode (loc_480750..loc_480D18) ----
        auto* cameraKeys = app->CameraKeys();
        auto* lightKeys = app->LightKeys();
        auto* shadowKeys = app->ShadowKeys();
        auto* gravityKeys = app->GravityKeys();

        const auto markAccessories =
            [app](std::uint32_t frame, std::int32_t excludedSlot) {
                for (std::int32_t slot = 0; slot < 0xFF; ++slot) {
                    if (slot == excludedSlot ||
                        app->AccessorySlot(slot) == nullptr) {
                        continue;
                    }
                    auto* keys = app->AccessoryKeys(slot);
                    MarkFrameSelected(keys, 0, frame);
                }
            };

        for (std::int32_t i = 0; i < 0x2710; ++i) {
            if (cameraKeys[i].selected != 0) {
                const std::uint32_t frame = cameraKeys[i].frame;
                MarkFrameSelected(lightKeys, 0, frame);
                MarkFrameSelected(shadowKeys, 0, frame);
                MarkFrameSelected(gravityKeys, 0, frame);
                markAccessories(frame, -1);
            }
            if (lightKeys[i].selected != 0) {
                const std::uint32_t frame = lightKeys[i].frame;
                MarkFrameSelected(cameraKeys, 0, frame);
                MarkFrameSelected(shadowKeys, 0, frame);
                MarkFrameSelected(gravityKeys, 0, frame);
                markAccessories(frame, -1);
            }
            if (shadowKeys[i].selected != 0) {
                const std::uint32_t frame = shadowKeys[i].frame;
                MarkFrameSelected(cameraKeys, 0, frame);
                MarkFrameSelected(lightKeys, 0, frame);
                MarkFrameSelected(gravityKeys, 0, frame);
                markAccessories(frame, -1);
            }
            if (gravityKeys[i].selected != 0) {
                const std::uint32_t frame = gravityKeys[i].frame;
                MarkFrameSelected(cameraKeys, 0, frame);
                MarkFrameSelected(lightKeys, 0, frame);
                MarkFrameSelected(shadowKeys, 0, frame);
                markAccessories(frame, -1);
            }
        }
        // accessory-blob marks (loc_480B7C..480D04): each marked blob
        // entry re-marks the four app tables and every *other* blob
        for (std::int32_t slot = 0; slot < 0xFF; ++slot) {
            if (app->AccessorySlot(slot) == nullptr) {
                continue;
            }
            auto* keys = app->AccessoryKeys(slot);
            for (std::int32_t i = 0; i < 0x2710; ++i) {
                if (keys[i].selected == 0) {
                    continue;
                }
                const std::uint32_t frame = keys[i].frame;
                MarkFrameSelected(cameraKeys, 0, frame);
                MarkFrameSelected(lightKeys, 0, frame);
                MarkFrameSelected(shadowKeys, 0, frame);
                MarkFrameSelected(gravityKeys, 0, frame);
                markAccessories(frame, slot);
            }
        }
        PanelPaint(app);
        SelectionReeval(app);
    }
}
// ------------------------------------------------------------------
// 420 (0x004834D2): frame copy.  Counts the marked entries and the
// minimum frame, then (re)allocates the copy buffers and copies every
// marked record.  0x2F8 == 0: counts 0x9DA28/0x9DA2C/0x9DA30, buffers
// 0x354 (bone, 0x54-stride) / 0x358 (morph, 0x28) / 0x35C (camera,
// 0x18; per-record bone-name/light arrays at +0x0C/+0x14).  0x2F8 !=
// 0: counts 0x9DA34..0x9DA44, buffers 0x360..0x370.  Controls 0x1A5
// (paste), 0x1A6 (delete) and menu 0xFA are gated on the totals; the
// record index for the name lookups walks the +4 rank pointers while
// idx >= count.  Tail: nothing (jmp loc_48F2E0).
// ------------------------------------------------------------------
static void Cmd400_FrameCopy(MMDApp* app, HWND hwnd) {
    std::uint32_t minFrame = 0xFFFFFFFFu;  // ebx
    if (app->state.optflag[0] == 0) {
        // ---- model mode (loc_483E7C..) ----
        // free the previous camera records' sub-buffers (loc_483E7C)
        if (app->DisplayClipboard() != nullptr &&
            app->ClipboardCounts().displays != 0) {
            auto* records = app->DisplayClipboard();
            const std::uint32_t camCount =
                app->ClipboardCounts().displays;
            for (std::uint32_t i = 0; i < camCount; ++i) {
                if (records[i].ikStates != nullptr) {
                    free(records[i].ikStates);
                    records[i].ikStates = nullptr;
                }
                if (records[i].selectorStates != nullptr) {
                    free(records[i].selectorStates);
                    records[i].selectorStates = nullptr;
                }
            }
        }
        auto& clipboardCounts = app->ClipboardCounts();
        clipboardCounts.bones = 0;
        clipboardCounts.morphs = 0;
        clipboardCounts.displays = 0;
        // selected-entry counts + min frame
        {
            unsigned char* model = ActiveModel(app);
            clipboardCounts.bones = CountMarkedModelKeys(
                mdl::BoneKeys(model), mdl::kBoneKeyCapacity, minFrame);
            clipboardCounts.morphs = CountMarkedModelKeys(
                mdl::MorphKeys(model), 20000, minFrame);
            clipboardCounts.displays = CountMarkedModelKeys(
                mdl::DisplayKeys(model), 1000, minFrame);
        }
        const std::uint32_t boneSel = clipboardCounts.bones;
        const std::uint32_t morphSel = clipboardCounts.morphs;
        const std::uint32_t camSel = clipboardCounts.displays;
        if (boneSel == 0 && morphSel == 0 && camSel == 0) {
            // loc_48402C..48406D: nothing selected
            EnableWindow(GetDlgItem(hwnd, panel::kPasteButton), FALSE);
            EnableWindow(GetDlgItem(hwnd, panel::kReversePasteButton), FALSE);
            EnableMenuItem(GetMenu(hwnd), 0xFA, TRUE);
            return;  // was: break (switch exit)
        }
        EnableWindow(GetDlgItem(hwnd, panel::kPasteButton), TRUE);
        EnableMenuItem(GetMenu(hwnd), 0xFA, FALSE);
        EnableWindow(GetDlgItem(hwnd, panel::kReversePasteButton),
                     boneSel != 0 ? TRUE : FALSE);
        // bone buffer 0x354 (0x54-stride records)
        if (app->BoneClipboard() != nullptr) {
            free(app->BoneClipboard());
            app->BoneClipboard() = nullptr;
        }
        if (boneSel != 0) {
            void* p = malloc(boneSel * sizeof(BoneClipboardRecord));
            if (p != nullptr) {
                ConstructArrayElements(p, sizeof(BoneClipboardRecord), boneSel,
                                       reinterpret_cast<void*>(&IdentityCtor));
            }
            app->BoneClipboard() = static_cast<BoneClipboardRecord*>(p);
            memset(p, 0, boneSel * sizeof(BoneClipboardRecord));
        }
        // morph buffer 0x358 (0x28-stride records)
        if (app->MorphClipboard() != nullptr) {
            free(app->MorphClipboard());
            app->MorphClipboard() = nullptr;
        }
        if (morphSel != 0) {
            void* p = malloc(morphSel * sizeof(MorphClipboardRecord));
            app->MorphClipboard() = static_cast<MorphClipboardRecord*>(p);
            memset(p, 0, morphSel * sizeof(MorphClipboardRecord));
        }
        // camera buffer 0x35C (0x18-stride records) + per-record
        // bone-name (0x2D88 * 0x15) and light (0x4CCE8 * 0x1C) arrays
        if (app->DisplayClipboard() != nullptr) {
            free(app->DisplayClipboard());
            app->DisplayClipboard() = nullptr;
        }
        if (camSel != 0) {
            unsigned char* model = ActiveModel(app);
            void* p = malloc(camSel * sizeof(DisplayClipboardRecord));
            app->DisplayClipboard() =
                static_cast<DisplayClipboardRecord*>(p);
            memset(p, 0, camSel * sizeof(DisplayClipboardRecord));
            if (mdl::Mdl(model)->ikChainCount > 0 &&
                camSel != 0) {
                auto* records = app->DisplayClipboard();
                const std::uint32_t cnt = mdl::Mdl(model)->ikChainCount;
                for (std::uint32_t i = 0; i < camSel; ++i) {
                    records[i].ikStates = static_cast<IkClipboardState*>(
                        malloc(cnt * sizeof(IkClipboardState)));
                }
            }
            if (mdl::Mdl(model)->boneOrderCount > 0 &&
                camSel != 0) {
                auto* records = app->DisplayClipboard();
                const std::uint32_t cnt =
                    mdl::Mdl(model)->boneOrderCount;
                for (std::uint32_t i = 0; i < camSel; ++i) {
                    records[i].selectorStates =
                        static_cast<SelectorClipboardState*>(
                            malloc(cnt * sizeof(SelectorClipboardState)));
                }
            }
        }
        // bone record copy (loc_484321..484551)
        {
            unsigned char* model = ActiveModel(app);
            mdl::BoneKey* boneKeys = mdl::BoneKeys(model);
            const mdl::BoneRecord* bones = mdl::Mdl(model)->boneTable;
            const std::uint32_t boneCount = mdl::Mdl(model)->boneCount;
            auto* dst = app->BoneClipboard();
            std::uint32_t cnt = 0;
            for (std::uint32_t recordIndex = 0; recordIndex < static_cast<std::uint32_t>(mdl::kBoneKeyCapacity);
                 ++recordIndex) {
                const mdl::BoneKey& key = boneKeys[recordIndex];
                if (key.allocated == 0) {
                    continue;
                }
                // rank walk: while idx >= boneCount follow the +4
                // pointer (loc_484350)
                std::uint32_t idx = recordIndex;
                while (idx >= boneCount) {
                    idx = boneKeys[idx].previous;
                }
                BoneClipboardRecord& rec = dst[cnt];
                strcpy_s(rec.name, sizeof(rec.name), bones[idx].name);
                rec.frame = key.frame - minFrame;
                std::memcpy(rec.rotation, key.rotation,
                            sizeof(rec.rotation));
                std::memcpy(rec.position, key.position,
                            sizeof(rec.position));
                rec.physicsDisabled = key.physicsDisabled;
                for (std::int32_t i = 0; i < 4; ++i) {
                    rec.interpolation[i] = key.interpolation[i];
                    rec.interpolation[4 + i] = key.interpolation[4 + i];
                    rec.interpolation[8 + i] = key.interpolation[8 + i];
                    rec.interpolation[12 + i] = key.interpolation[12 + i];
                }
                ++cnt;
            }
        }
        // morph record copy (loc_484590..484667, gated 0x2D80 > 0)
        if (mdl::Mdl(ActiveModel(app))->morphCount > 0) {
            unsigned char* model = ActiveModel(app);
            mdl::MorphKey* morphKeys = mdl::MorphKeys(model);
            const mdl::MorphRecord* morphs = mdl::Mdl(model)->morphs;
            const std::uint32_t morphCount = mdl::Mdl(model)->morphCount;
            auto* dst = app->MorphClipboard();
            std::uint32_t cnt = 0;
            for (std::uint32_t recordIndex = 0; recordIndex < 20000;
                 ++recordIndex) {
                const mdl::MorphKey& key = morphKeys[recordIndex];
                if (key.allocated == 0) {
                    continue;
                }
                std::uint32_t idx = recordIndex;
                while (idx >= morphCount) {
                    idx = morphKeys[idx].previous;
                }
                MorphClipboardRecord& rec = dst[cnt];
                strcpy_s(rec.name, sizeof(rec.name), morphs[idx].name);
                rec.frame = key.frame - minFrame;
                rec.value = key.value;
                ++cnt;
            }
        }
        // display record copy (loc_484675..)
        {
            unsigned char* model = ActiveModel(app);
            mdl::DisplayKey* displayKeys = mdl::DisplayKeys(model);
            const mdl::BoneRecord* bones = mdl::Mdl(model)->boneTable;
            auto* records = app->DisplayClipboard();
            const std::int32_t boneIdxCount =
                mdl::Mdl(model)->ikChainCount;
            const std::int32_t lightCount =
                mdl::Mdl(model)->boneOrderCount;
            std::uint32_t cnt = 0;
            for (std::uint32_t keyIndex = 0; keyIndex < 1000;
                 ++keyIndex) {
                const mdl::DisplayKey& key = displayKeys[keyIndex];
                if (key.allocated == 0) {
                    continue;
                }
                DisplayClipboardRecord& rec = records[cnt];
                rec.frame = key.frame - minFrame;
                rec.visible = key.visible;
                rec.ikCount = boneIdxCount;
                // per-bone records at +0x0C (0x15 stride: 0x14-byte
                // name + value byte)
                if (boneIdxCount > 0) {
                    const mdl::IkChain* chains =
                        mdl::Mdl(model)->ikChains;
                    const auto* pose = mdl::IkStates(key);
                    for (std::int32_t i = 0; i < boneIdxCount; ++i) {
                        const std::uint32_t boneIdx =
                            chains[i].boneIndex;
                        strcpy_s(rec.ikStates[i].boneName,
                                 sizeof(rec.ikStates[i].boneName),
                                 bones[boneIdx].name);
                        rec.ikStates[i].enabled = pose[i];
                    }
                }
                // light records at +0x14 (0x1C stride)
                rec.selectorCount = lightCount;
                const auto* lightData = mdl::SelectorStates(key);
                strcpy_s(rec.selectorStates[0].boneName,
                         sizeof(rec.selectorStates[0].boneName),
                         kLightNameRoot);
                rec.selectorStates[0].modelIndex = lightData[0].modelIndex;
                rec.selectorStates[0].boneIndex = lightData[0].boneIndex;
                if (lightCount > 1) {
                    const auto* order = mdl::BoneOrder(model);
                    for (std::int32_t i = 1; i < lightCount; ++i) {
                        const std::uint32_t boneIdx = order[i].boneIndex;
                        strcpy_s(rec.selectorStates[i].boneName,
                                 sizeof(rec.selectorStates[i].boneName),
                                 bones[boneIdx].name);
                        rec.selectorStates[i].modelIndex =
                            lightData[i].modelIndex;
                        rec.selectorStates[i].boneIndex =
                            lightData[i].boneIndex;
                    }
                }
                ++cnt;
            }
        }
    } else {
        // ---- camera mode (loc_4834E2..) ----
        auto& clipboardCounts = app->ClipboardCounts();
        clipboardCounts.cameras = 0;
        clipboardCounts.lights = 0;
        clipboardCounts.shadows = 0;
        clipboardCounts.gravity = 0;
        clipboardCounts.accessories = 0;
        clipboardCounts.cameras =
            CountSelectedKeys(app->CameraKeys(), 10000, minFrame);
        clipboardCounts.lights =
            CountSelectedKeys(app->LightKeys(), 10000, minFrame);
        clipboardCounts.shadows =
            CountSelectedKeys(app->ShadowKeys(), 10000, minFrame);
        clipboardCounts.gravity =
            CountSelectedKeys(app->GravityKeys(), 10000, minFrame);
        {
            std::uint32_t accessoryCount = 0;
            for (std::size_t keyIndex = 0; keyIndex < 10000;
                 ++keyIndex) {
                for (std::int32_t slot = 0; slot < 0xFF; ++slot) {
                    auto* keys = app->AccessoryKeys(slot);
                    const mdl::AccessoryKey& key = keys[keyIndex];
                    if (key.selected != 0) {
                        ++accessoryCount;
                        if (key.frame < minFrame) {
                            minFrame = key.frame;
                        }
                    }
                }
            }
            clipboardCounts.accessories = accessoryCount;
        }
        const std::uint32_t cameraCount = clipboardCounts.cameras;
        const std::uint32_t lightCount = clipboardCounts.lights;
        const std::uint32_t shadowCount = clipboardCounts.shadows;
        const std::uint32_t gravityCount = clipboardCounts.gravity;
        const std::uint32_t accessoryCount = clipboardCounts.accessories;
        if (cameraCount == 0 && lightCount == 0 && shadowCount == 0 &&
            gravityCount == 0 && accessoryCount == 0) {
            // loc_483657..48369C
            EnableWindow(GetDlgItem(hwnd, panel::kPasteButton), FALSE);
            EnableWindow(GetDlgItem(hwnd, panel::kReversePasteButton), FALSE);
            EnableMenuItem(GetMenu(hwnd), 0xFA, TRUE);
            return;  // was: break (switch exit)
        }
        EnableWindow(GetDlgItem(hwnd, panel::kPasteButton), TRUE);
        EnableWindow(GetDlgItem(hwnd, panel::kReversePasteButton), FALSE);
        EnableMenuItem(GetMenu(hwnd), 0xFA, FALSE);
        // buffers 0x360..0x370 (0x48/0x1C/0x0C/0x1C/0x34 strides)
        ReplaceClipboardBuffer(app->CameraClipboard(), cameraCount);
        ReplaceClipboardBuffer(app->LightClipboard(), lightCount);
        ReplaceClipboardBuffer(app->ShadowClipboard(), shadowCount);
        ReplaceClipboardBuffer(app->GravityClipboard(), gravityCount);
        ReplaceClipboardBuffer(app->AccessoryClipboard(), accessoryCount);
        // copy loop 1: 0x374 marked -> 0x360 records (0x48 stride)
        {
            auto* keys = app->CameraKeys();
            auto* dst = app->CameraClipboard();
            std::uint32_t cnt = 0;
            for (std::size_t i = 0; i < 10000; ++i) {
                const mdl::CameraKey& key = keys[i];
                if (key.selected == 0) {
                    continue;
                }
                CameraClipboardRecord& rec = dst[cnt];
                rec.frame = key.frame - minFrame;
                std::memcpy(rec.eye, key.eye, sizeof(rec.eye));
                std::memcpy(rec.target, key.target, sizeof(rec.target));
                rec.fov = key.fov;
                rec.perspective = key.perspective;
                std::memcpy(rec.interpolation, key.interpolation,
                            sizeof(rec.interpolation));
                rec.distance = key.distance;
                rec.parentModel = key.parentModel;
                rec.parentBone = key.parentBone;
                ++cnt;
            }
        }
        // copy loop 2: 0x378 marked -> 0x364 records (0x1C stride)
        {
            auto* keys = app->LightKeys();
            auto* dst = app->LightClipboard();
            std::uint32_t cnt = 0;
            for (std::size_t i = 0; i < 10000; ++i) {
                const mdl::LightKey& key = keys[i];
                if (key.selected == 0) {
                    continue;
                }
                LightClipboardRecord& rec = dst[cnt];
                rec.frame = key.frame - minFrame;
                std::memcpy(rec.direction, key.direction,
                            sizeof(rec.direction));
                std::memcpy(rec.color, key.color, sizeof(rec.color));
                ++cnt;
            }
        }
        // copy loop 3: 0x37C marked -> 0x368 records (0x0C stride)
        {
            auto* keys = app->ShadowKeys();
            auto* dst = app->ShadowClipboard();
            std::uint32_t cnt = 0;
            for (std::size_t i = 0; i < 10000; ++i) {
                const mdl::SelfShadowKey& key = keys[i];
                if (key.selected == 0) {
                    continue;
                }
                ShadowClipboardRecord& rec = dst[cnt];
                rec.frame = key.frame - minFrame;
                rec.mode = key.mode;
                rec.distance = key.distance;
                ++cnt;
            }
        }
        // copy loop 4: 0x380 marked -> 0x36C records (0x1C stride)
        {
            auto* keys = app->GravityKeys();
            auto* dst = app->GravityClipboard();
            std::uint32_t cnt = 0;
            for (std::size_t i = 0; i < 10000; ++i) {
                const mdl::GravityKey& key = keys[i];
                if (key.selected == 0) {
                    continue;
                }
                GravityClipboardRecord& rec = dst[cnt];
                rec.frame = key.frame - minFrame;
                rec.acceleration = key.acceleration;
                std::memcpy(rec.direction, key.direction,
                            sizeof(rec.direction));
                rec.noise = key.noise;
                rec.noiseEnabled = key.noiseEnabled;
                ++cnt;
            }
        }
        // copy loop 5: accessory blobs -> 0x370 records (0x34 stride,
        // slot index at +4)
        {
            auto* dst = app->AccessoryClipboard();
            std::uint32_t cnt = 0;
            for (std::size_t keyIndex = 0; keyIndex < 10000;
                 ++keyIndex) {
                for (std::int32_t slot = 0; slot < 0xFF; ++slot) {
                    auto* keys = app->AccessoryKeys(slot);
                    const mdl::AccessoryKey& key = keys[keyIndex];
                    if (key.selected == 0) {
                        continue;
                    }
                    mdl::AccessoryClipboardKey& rec = dst[cnt];
                    rec.frameOffset = key.frame - minFrame;
                    rec.slot = static_cast<std::uint8_t>(slot);
                    rec.visible = key.visible;
                    rec.shadowEnabled = key.shadowEnabled;
                    rec.parentModel = key.parentModel;
                    rec.parentBone = key.parentBone;
                    std::memcpy(rec.position, key.position,
                                sizeof(rec.position));
                    std::memcpy(rec.rotation, key.rotation,
                                sizeof(rec.rotation));
                    rec.scale = key.scale;
                    rec.opacity = key.opacity;
                    ++cnt;
                }
            }
        }
    }
}
// ------------------------------------------------------------------
// 421 (0x00484973): frame paste.  0x2F8 == 0: when any model-mode
// count is non-zero, clear all marks, open an undo-table entry
// (0x26EC, 0x1C stride: type 2 at +0, frame at +0x0C, bone-name
// snapshot at +0x10, light snapshot at +0x14; second 0x1C-stride
// history at 0x23F0 zeroed), snapshot the bone records, then paste
// the copy buffers: RegisterBoneKey per 0x354 bone record, RegisterMorphKeyFromRecord per
// 0x358 morph record (0x28), RegisterDisplayKeyFromRecord per 0x35C camera record (0x18)
// with the light-record slot fixup (stale model slots severed).
// 0x2F8 != 0: same over the 0x360..0x370 camera-mode buffers with
// RegisterCameraKey/RegisterLightKey/RegisterSelfShadowKey/RegisterGravityKey/0x414110.  Common tail:
// byte 0x9EDB5 = 1, PanelPaint, SelectionReeval, SeekModelFrame(model,
// frame, 0xA0CC4), and for camera mode the reload chain (ReloadModels,
// RefreshLightPanel, RefreshSelfShadowPanel, ApplyGravityTrack, ApplyAccessoryTrack per slot, SyncAccessoryEditPanel).
// ------------------------------------------------------------------
static void Cmd400_FramePaste(MMDApp* app, HWND hwnd) {
    if (app->state.optflag[0] == 0) {
        const auto& clipboardCounts = app->ClipboardCounts();
        const std::uint32_t boneSel = clipboardCounts.bones;
        const std::uint32_t morphSel = clipboardCounts.morphs;
        const std::uint32_t camSel = clipboardCounts.displays;
        TraceModelPaste("enter bone=%u morph=%u display=%u", boneSel,
                        morphSel, camSel);
        if (boneSel == 0 && morphSel == 0 && camSel == 0) {
            return;  // was: break (switch exit)
        }
        app->SceneModified() = 1;
        const std::int32_t frame =
            app->state.currentFrame;
        unsigned char* model = ActiveModel(app);
        TraceModelPaste("active model=%p frame=%d", model, frame);
        // clear all model-mode marks (loc_4849C0..)
        ClearModelKeyMarks(mdl::BoneKeys(model), mdl::kBoneKeyCapacity);
        ClearModelKeyMarks(mdl::MorphKeys(model), 20000);
        ClearModelKeyMarks(mdl::DisplayKeys(model), 1000);
        if (boneSel != 0) {
            // undo/redo buttons + undo-table entry (loc_484C84..)
            EnableWindow(GetDlgItem(hwnd, panel::kUndoButton), TRUE);
            EnableWindow(GetDlgItem(hwnd, panel::kRedoButton), FALSE);
            mdl::Mdl(model)->undoDirty = 1;
            mdl::Mdl(model)->redoDirty = 0;
            mdl::ModelRecord& record = *mdl::Mdl(model);
            const std::int32_t next = record.undoState[0] + 1;
            record.undoState[0] = next >= 0x1E ? 0 : next;
            const std::int32_t cur = record.undoState[0];
            record.undoState[1] = cur;
            auto& undo = mikudancestudio::mdl::Mdl(model)->undoRings[0].slots[cur];
            undo.operation = 2;
            undo.frame = frame;
            // bone-name snapshot buffer (+0x10 of the entry, 0x24-
            // stride records)
            void* p = undo.bonePose;
            if (p != nullptr) {
                ::operator delete(p);
                undo.bonePose = nullptr;
            }
            const std::int32_t boneCount = mdl::Mdl(model)->boneCount;
            auto* undoBone = static_cast<mikudancestudio::mdl::BonePoseSnapshot*>(
                ::operator new(static_cast<std::uint32_t>(boneCount) *
                       sizeof(mikudancestudio::mdl::BonePoseSnapshot)));
            if (undoBone != nullptr) {
                ConstructArrayElements(undoBone,
                                       sizeof(mikudancestudio::mdl::BonePoseSnapshot),
                                       static_cast<std::uint32_t>(boneCount),
                                       reinterpret_cast<void*>(&IdentityCtor));
            }
            undo.bonePose = undoBone;
            memset(undoBone, 0,
                   static_cast<std::uint32_t>(boneCount) *
                       sizeof(mikudancestudio::mdl::BonePoseSnapshot));
            if (boneCount > 0) {
                const mdl::ModelRecord& record = *mdl::Mdl(model);
                const mdl::BoneRecord* bones = record.boneTable;
                unsigned char* boneState = record.bonePhysicsState;
                for (std::int32_t i = 0; i < boneCount; ++i) {
                    auto& rec = undoBone[i];
                    const auto& src = bones[i];
                    rec.boneIndex = i;
                    memcpy(rec.position, src.trans, sizeof rec.position);
                    memcpy(rec.rotation, src.rotQuat, sizeof rec.rotation);
                    rec.physicsDisabled = boneState[i];
                }
            }
            // second undo-history table (0x23F0 base, 0x1C stride):
            // entry cnt+0x164 cleared
            undo.dirty = 0;
            // light snapshot buffer (+0x14 of the entry, 3*0x40 *
            // boneSel bytes)
            p = undo.auxiliaryPose;
            if (p != nullptr) {
                ::operator delete(p);
                undo.auxiliaryPose = nullptr;
            }
            unsigned char* undoLight = static_cast<unsigned char*>(
                ::operator new(boneSel * 3 * 0x40));
            if (undoLight != nullptr) {
                ConstructArrayElements(undoLight, 0x40, boneSel * 3,
                                       reinterpret_cast<void*>(&IdentityCtor));
            }
            undo.auxiliaryPose = undoLight;
            memset(undoLight, 0, boneSel * 3 * 0x40);
        }
        std::memset(mdl::Mdl(model)->keyVisitMap, 0,
                    sizeof(mdl::Mdl(model)->keyVisitMap));
        ResetBoneKeyCursor(model);
        TraceModelPaste("undo-ready free=%d",
                        mdl::Mdl(model)->searchCursor);
        // bone paste loop (loc_485130..485174): 0x54-byte record +
        // frame + 0 flag; original is __userpurge(ecx = model,
        // ebx = i) - the stub drops the index
        if (boneSel != 0) {
            const auto* src = app->BoneClipboard();
            std::uint32_t i = 0;
            do {
                BoneClipboardRecord rec = src[i];
                if (RegisterBoneKey(model,
                              reinterpret_cast<unsigned char*>(&rec),
                              frame, 0) == 0) {
                    break;
                }
                ++i;
            } while (i < boneSel);
        }
        ResetMorphKeyCursor(model);
        TraceModelPaste("bone-done free=%d",
                        mdl::Mdl(model)->searchCursor);
        // morph paste loop (loc_48519B..4851DD)
        if (morphSel != 0) {
            const auto* src = app->MorphClipboard();
            std::uint32_t i = 0;
            do {
                const MorphClipboardRecord rec = src[i];
                if (RegisterMorphKeyFromRecord(model,
                              reinterpret_cast<const unsigned char*>(&rec),
                              frame) == 0) {
                    break;
                }
                ++i;
            } while (i < morphSel);
        }
        ResetDisplayKeyCursor(model);
        TraceModelPaste("morph-done display-free=%d",
                        mdl::Mdl(model)->searchCursor);
        // camera paste loop (loc_485210..4852E4): light-record slot
        // fixup + RegisterDisplayKeyFromRecord with the 6 record dwords + frame
        if (camSel != 0) {
            auto* records = app->DisplayClipboard();
            std::uint32_t i = 0;
            do {
                DisplayClipboardRecord& rec = records[i];
                const std::int32_t lightCount = rec.selectorCount;
                TraceModelPaste(
                    "display[%u] rec=%p rel=%d relptr=%p ik=%d ikptr=%p",
                    i, &rec, lightCount, rec.selectorStates,
                    rec.ikCount, rec.ikStates);
                if (lightCount > 0) {
                    for (std::int32_t j = 0; j < lightCount; ++j) {
                        SelectorClipboardState& state =
                            rec.selectorStates[j];
                        const std::int32_t slotIdx = state.modelIndex;
                        TraceModelPaste(
                            "display[%u] relation[%d] slot=%d bone=%d",
                            i, j, slotIdx, state.boneIndex);
                        if (slotIdx < 0) {
                            continue;
                        }
                        unsigned char* slotModel =
                            app->ModelSlot(slotIdx);
                        if (slotModel == nullptr) {
                            state.modelIndex = -1;
                            state.boneIndex = 0;
                        } else if (static_cast<std::int32_t>(
                                       mdl::Mdl(slotModel)->boneCount) <
                                   state.boneIndex) {
                            state.boneIndex = 0;
                        }
                    }
                }
                TraceModelPaste("display[%u] fixup-done", i);
                if (RegisterDisplayKeyFromRecord(model,
                              rec.frame, rec.visible, rec.ikCount,
                              reinterpret_cast<unsigned char*>(rec.ikStates),
                              rec.selectorCount,
                              reinterpret_cast<unsigned char*>(
                                  rec.selectorStates),
                              frame) == 0) {
                    break;
                }
                TraceModelPaste("display[%u] registrar-done", i);
                ++i;
            } while (i < camSel);
        }
        app->PhysicsResetPending() = 1;
        TraceModelPaste("model-paste loops done");
    } else {
        // ---- camera mode (loc_4849A0..) ----
        const auto& clipboardCounts = app->ClipboardCounts();
        const std::uint32_t cameraCount = clipboardCounts.cameras;
        const std::uint32_t lightCount = clipboardCounts.lights;
        const std::uint32_t shadowCount = clipboardCounts.shadows;
        const std::uint32_t gravityCount = clipboardCounts.gravity;
        const std::uint32_t accessoryCount = clipboardCounts.accessories;
        if (cameraCount == 0 && lightCount == 0 && shadowCount == 0 &&
            gravityCount == 0 && accessoryCount == 0) {
            return;  // was: break (switch exit)
        }
        app->SceneModified() = 1;
        // clear all camera-mode marks (single sweep over the four
        // tables + the blobs)
        ClearSelectedKeys(app->CameraKeys(), 10000);
        ClearSelectedKeys(app->LightKeys(), 10000);
        ClearSelectedKeys(app->ShadowKeys(), 10000);
        ClearSelectedKeys(app->GravityKeys(), 10000);
        // (the original dereferences every blob pointer here - the
        // 0x384 table is expected to be fully populated)
        for (std::int32_t slot = 0; slot < 0xFF; ++slot) {
            auto* keys = app->AccessoryKeys(slot);
            ClearSelectedKeys(keys, 10000);
        }
        const std::int32_t frame =
            app->state.currentFrame;
        // paste loops (loc_484A54..484Bxx): record-by-value + bool
        if (cameraCount != 0) {
            const auto* src = app->CameraClipboard();
            std::uint32_t i = 0;
            do {
                const CameraClipboardRecord rec = src[i];
                if (RegisterCameraKey(app, &rec) == 0) {
                    break;
                }
                ++i;
            } while (i < cameraCount);
        }
        if (lightCount != 0) {
            const auto* src = app->LightClipboard();
            std::uint32_t i = 0;
            do {
                const LightClipboardRecord rec = src[i];
                if (RegisterLightKey(app, &rec) == 0) {
                    break;
                }
                ++i;
            } while (i < lightCount);
        }
        if (shadowCount != 0) {
            const auto* src = app->ShadowClipboard();
            std::uint32_t i = 0;
            do {
                const ShadowClipboardRecord rec = src[i];
                if (RegisterSelfShadowKey(app, &rec) == 0) {
                    break;
                }
                ++i;
            } while (i < shadowCount);
        }
        if (gravityCount != 0) {
            const auto* src = app->GravityClipboard();
            std::uint32_t i = 0;
            do {
                const GravityClipboardRecord rec = src[i];
                if (RegisterGravityKey(app, &rec) == 0) {
                    break;
                }
                ++i;
            } while (i < gravityCount);
        }
        if (accessoryCount != 0) {
            auto* src = app->AccessoryClipboard();
            std::uint32_t i = 0;
            do {
                mdl::AccessoryClipboardKey rec = src[i];
                if (PasteAccessoryKeyRecord(app, &rec, 0) == 0) {
                    break;
                }
                ++i;
            } while (i < accessoryCount);
        }
        app->PhysicsResetPending() = 1;
    }
    // common tail (loc_4852F1..485382)
    PanelPaint(app);
    SelectionReeval(app);
    unsigned char* model = ActiveModel(app);
    if (model != nullptr) {
        SeekModelFrame(model, app->state.currentFrame,
                  app->PlaybackPhysicsMode());
    }
    if (app->state.optflag[0] != 0) {
        ReloadModels(app);
        RefreshLightPanel(app);
        RefreshSelfShadowPanel(app);
        ApplyGravityTrack(app);
        for (std::int32_t i = 0; i < 0xFF; ++i) {
            if (app->AccessorySlot(i) != nullptr) {
                ApplyAccessoryTrack(app, i);
            }
        }
        SyncAccessoryEditPanel(app);
    }
}
// ------------------------------------------------------------------
// 422 (0x00485CF2): reverse paste.  0x2F8 == 0 with a non-zero bone
// count: identical undo-table setup as 421 (bone snapshot), then the
// RegisterMirroredBoneKey mirrored insert loop over 0x354 bone records; morph/camera
// buffers are left untouched.  Tail: PanelPaint, SelectionReeval,
// SeekModelFrame(model, frame, 0xA0CC4) and byte 0x9EDB5 = 1 (no camera-
// mode branch at all).
// ------------------------------------------------------------------
static void Cmd400_FrameReversePaste(MMDApp* app, HWND hwnd) {
    if (app->state.optflag[0] != 0) {
        return;  // was: break (switch exit)
    }
    const std::uint32_t boneSel = app->ClipboardCounts().bones;
    if (boneSel == 0) {
        return;  // was: break (switch exit)
    }
    app->SceneModified() = 1;
    const std::int32_t frame =
        app->state.currentFrame;
    unsigned char* model = ActiveModel(app);
    // clear all model-mode marks (loc_485D60..)
    ClearModelKeyMarks(mdl::BoneKeys(model), mdl::kBoneKeyCapacity);
    ClearModelKeyMarks(mdl::MorphKeys(model), 20000);
    ClearModelKeyMarks(mdl::DisplayKeys(model), 1000);
    // undo-table entry (loc_485D88..) - identical shape to 421
    EnableWindow(GetDlgItem(hwnd, panel::kUndoButton), TRUE);
    EnableWindow(GetDlgItem(hwnd, panel::kRedoButton), FALSE);
    mdl::Mdl(model)->undoDirty = 1;
    mdl::Mdl(model)->redoDirty = 0;
    mdl::ModelRecord& record = *mdl::Mdl(model);
    const std::int32_t next = record.undoState[0] + 1;
    record.undoState[0] = next >= 0x1E ? 0 : next;
    const std::int32_t cur = record.undoState[0];
    record.undoState[1] = cur;
    auto& undo = mikudancestudio::mdl::Mdl(model)->undoRings[0].slots[cur];
    undo.operation = 2;
    undo.frame = frame;
    void* p = undo.bonePose;
    if (p != nullptr) {
        free(p);
        undo.bonePose = nullptr;
    }
    const std::int32_t boneCount = mdl::Mdl(model)->boneCount;
    auto* undoBone = static_cast<mikudancestudio::mdl::BonePoseSnapshot*>(
        malloc(static_cast<std::uint32_t>(boneCount) *
               sizeof(mikudancestudio::mdl::BonePoseSnapshot)));
    if (undoBone != nullptr) {
        ConstructArrayElements(undoBone,
                               sizeof(mikudancestudio::mdl::BonePoseSnapshot),
                               static_cast<std::uint32_t>(boneCount),
                               reinterpret_cast<void*>(&IdentityCtor));
    }
    undo.bonePose = undoBone;
    memset(undoBone, 0, static_cast<std::uint32_t>(boneCount) *
                            sizeof(mikudancestudio::mdl::BonePoseSnapshot));
    if (boneCount > 0) {
        const mdl::BoneRecord* bones = record.boneTable;
        unsigned char* boneState = record.bonePhysicsState;
        for (std::int32_t i = 0; i < boneCount; ++i) {
            auto& rec = undoBone[i];
            const auto& src = bones[i];
            rec.boneIndex = i;
            memcpy(rec.position, src.trans, sizeof rec.position);
            memcpy(rec.rotation, src.rotQuat, sizeof rec.rotation);
            rec.physicsDisabled = boneState[i];
        }
    }
    undo.dirty = 0;
    p = undo.auxiliaryPose;
    if (p != nullptr) {
        free(p);
        undo.auxiliaryPose = nullptr;
    }
    unsigned char* undoLight =
        static_cast<unsigned char*>(malloc(boneSel * 3 * 0x40));
    if (undoLight != nullptr) {
        ConstructArrayElements(undoLight, 0x40, boneSel * 3,
                               reinterpret_cast<void*>(&IdentityCtor));
    }
    undo.auxiliaryPose = undoLight;
    memset(undoLight, 0, boneSel * 3 * 0x40);
    std::memset(record.keyVisitMap, 0, sizeof(record.keyVisitMap));
    ResetBoneKeyCursor(model);
    // mirrored bone paste loop (loc_486255..486291)
    if (boneSel != 0) {
        const auto* src = app->BoneClipboard();
        std::uint32_t i = 0;
        do {
            BoneClipboardRecord rec = src[i];
            if (RegisterMirroredBoneKey(model,
                          reinterpret_cast<unsigned char*>(&rec),
                          frame) == 0) {
                break;
            }
            ++i;
        } while (i < boneSel);
    }
    // tail (loc_4862A5..4862DB)
    PanelPaint(app);
    SelectionReeval(app);
    SeekModelFrame(model, frame,
              app->PlaybackPhysicsMode());
    app->PhysicsResetPending() = 1;
}
// ------------------------------------------------------------------
// 430 (0x00481109): light colour <- selected frame.  Combo 0x1B1
// cursor selects the source row; the source frame is the first frame
// whose selection mark is set (0x2F8 != 0: app+0x374 table, 0x54
// stride, mark +0x48, 0x2710 limit; 0x2F8 == 0: model+0x26E0, 0x3C
// stride, mark +0x38, kBoneKeyCapacity limit).  With cursor < 6 (or < 4) a
// single frame is copied into all rows, otherwise the first 6 (4)
// frames are copied row by row.  Colour bytes: R/G/B/aux at +0x28/
// +0x2E/+0x34/+0x3A (0x54-stride) or +0x0C/+0x10/+0x14/+0x18
// (0x3C-stride) -> 0x9DA0A/0x9DA10/0x9DA16/0x9DA1C rows.  Ends with
// EnableWindow(0x1AF) + SetFocus.
// ------------------------------------------------------------------
static void Cmd400_CurveCopyFromFrame(MMDApp* app, HWND hwnd) {
    const LRESULT sel =
        SendMessageA(GetDlgItem(hwnd, panel::kInterpCurveCombo), CB_GETCURSEL, 0, 0);
    if (app->state.optflag[0] != 0) {
        const std::int32_t idx = FirstSelectedFrame374(app);
        if (idx < 0) {
            SetFocus(hwnd);
            return;  // was: break (switch exit)
        }
        EnableWindow(GetDlgItem(hwnd, panel::kCurvePasteButton), TRUE);
        auto* keys = app->CameraKeys();
        for (std::int32_t i = 0; i < 6; ++i) {
            const std::int32_t row = (sel < 6) ? sel : i;
            ReadCameraCurveChannel(app, keys[idx], row, i);
        }
        SetFocus(hwnd);
    } else {
        unsigned char* model = ActiveModel(app);
        const std::int32_t idx = FirstSelectedBoneKey(model);
        if (idx < 0) {
            SetFocus(hwnd);
            return;  // was: break (switch exit)
        }
        EnableWindow(GetDlgItem(hwnd, panel::kCurvePasteButton), TRUE);
        mdl::BoneKey* keys = mdl::BoneKeys(model);
        if (sel < 4) {
            // 6 rows duplicated from the single frame at idx+sel
            for (std::int32_t i = 0; i < 6; ++i) {
                ReadBoneCurveChannel(app, keys[idx],
                                     static_cast<std::size_t>(sel), i);
            }
        } else {
            // All four interpolation channels into the first four rows.
            for (std::int32_t i = 0; i < 4; ++i) {
                ReadBoneCurveChannel(app, keys[idx], i, i);
            }
        }
        SetFocus(hwnd);
    }
}
// ------------------------------------------------------------------
// 431 (0x00481430): light colour -> selected frames (inverse of 430).
// Every frame with its selection mark set receives the colour of the
// combo-0x1B1 row (single frame at cursor offset) or all 6/4 rows;
// byte 0xA0B0D dirty; 0x2F8==0 branch runs SnapshotSelectedKeysForUndo first and uses
// the model+0x26E0 table; ends SelectionReeval + SetFocus.
// ------------------------------------------------------------------
static void Cmd400_CurvePasteToFrames(MMDApp* app, HWND hwnd) {
    app->SceneModified() = 1;
    const LRESULT sel =
        SendMessageA(GetDlgItem(hwnd, panel::kInterpCurveCombo), CB_GETCURSEL, 0, 0);
    if (app->state.optflag[0] != 0) {
        auto* keys = app->CameraKeys();
        for (std::int32_t i = 0; i < 10000; ++i) {
            mdl::CameraKey& key = keys[i];
            if (key.selected == 0) {
                continue;
            }
            if (sel < 6) {
                WriteCameraCurveChannel(
                    app, key, static_cast<std::size_t>(sel), sel);
            } else {
                for (std::int32_t channel = 0; channel < 6; ++channel) {
                    WriteCameraCurveChannel(app, key, channel, channel);
                }
            }
        }
        SelectionReeval(app);
        SetFocus(hwnd);
    } else {
        unsigned char* model = ActiveModel(app);
        // original: 0x4A1510(ecx = model, frame = dword app+0x980)
        SnapshotSelectedKeysForUndo(model, app->state.currentFrame);
        mdl::BoneKey* keys = mdl::BoneKeys(model);
        for (std::int32_t i = 0; i < static_cast<std::int32_t>(mdl::kBoneKeyCapacity); ++i) {
            mdl::BoneKey& key = keys[i];
            if (key.allocated == 0) {
                continue;
            }
            if (sel < 4) {
                WriteBoneCurveChannel(app, key,
                                      static_cast<std::size_t>(sel), sel);
            } else {
                for (std::int32_t channel = 0; channel < 4; ++channel) {
                    WriteBoneCurveChannel(app, key, channel, channel);
                }
            }
        }
        SelectionReeval(app);
        SetFocus(hwnd);
    }
}
// ------------------------------------------------------------------
// 432 (0x004816E6): light colour reset - same sweep as 431 but the
// colour bytes are fixed to 0x14/0x14/0x6B/0x6B instead of the row
// values (0x2F8==0 branch uses model+0x26E0 with +0x0C/+0x10/+0x14/
// +0x18; 0x2F8!=0 uses app+0x374 with +0x28/+0x2E/+0x34/+0x3A).
// ------------------------------------------------------------------
static void Cmd400_CurveResetRows(MMDApp* app, HWND hwnd) {
    app->SceneModified() = 1;
    const LRESULT sel =
        SendMessageA(GetDlgItem(hwnd, panel::kInterpCurveCombo), CB_GETCURSEL, 0, 0);
    if (app->state.optflag[0] != 0) {
        auto* keys = app->CameraKeys();
        for (std::int32_t i = 0; i < 10000; ++i) {
            mdl::CameraKey& key = keys[i];
            if (key.selected == 0) {
                continue;
            }
            if (sel < 6) {
                ResetCameraCurveChannel(key,
                                        static_cast<std::size_t>(sel));
            } else {
                for (std::int32_t channel = 0; channel < 6; ++channel) {
                    ResetCameraCurveChannel(key, channel);
                }
            }
        }
        SelectionReeval(app);
        SetFocus(hwnd);
    } else {
        unsigned char* model = ActiveModel(app);
        // original: 0x4A1510(ecx = model, frame = dword app+0x980)
        SnapshotSelectedKeysForUndo(model, app->state.currentFrame);
        mdl::BoneKey* keys = mdl::BoneKeys(model);
        for (std::int32_t i = 0; i < static_cast<std::int32_t>(mdl::kBoneKeyCapacity); ++i) {
            mdl::BoneKey& key = keys[i];
            if (key.allocated == 0) {
                continue;
            }
            if (sel < 4) {
                ResetBoneCurveChannel(key, static_cast<std::size_t>(sel));
            } else {
                for (std::int32_t channel = 0; channel < 4; ++channel) {
                    ResetBoneCurveChannel(key, channel);
                }
            }
        }
        SelectionReeval(app);
        SetFocus(hwnd);
    }
}
// ------------------------------------------------------------------
// 435 (0x0047EB7B): load model.  SetCurrentDirectoryW(ExeDir), dirty
// 0xBC, zeroed 0x200-wide file buffer + OPENFILENAMEW (0x4C),
// owner = dword 0xA0D38 ?: hwnd, filter "All Model files(*.pmd,*.pmx)"
// (both locales), initial dir = DirModel() when menu 0x12D checked
// else "UserFile\\Model", def-ext "pmd;pmx", title "load model" (JP:
// "ファイルを開く"); on OK and menu gate: ExtractDirFromPath(app+0x9F338,
// path) -> CopyDirPathW(DirModel(), dir); LoadModelFile(app, path);
// always RefreshRequest(-1) (also when cancelled).
// ------------------------------------------------------------------
static void Cmd400_LoadModel(MMDApp* app, HWND hwnd) {
    SetCurrentDirectoryW(app->ExeDir());
    app->state.enterKeyState = 1;
    wchar_t fileBuf[0x100];
    memset(fileBuf, 0, sizeof(fileBuf));
    OPENFILENAMEW ofn;
    memset(&ofn, 0, sizeof(ofn));
    ofn.lStructSize = sizeof(ofn);
    ofn.hwndOwner = app->state.floatingWindow != 0
                        ? reinterpret_cast<HWND>(
                              app->state.floatingWindow)
                        : hwnd;
    ofn.lpstrFilter = kFilterModel;
    ofn.lpstrFile = fileBuf;
    ofn.nMaxFile = 0x100;
    ofn.Flags = OFN_FILEMUSTEXIST;
    ofn.lpstrInitialDir =
        (GetMenuState(GetMenu(hwnd), 0x12D, 0) & 8) != 0
            ? app->DirModel()
            : kInitDirModel;
    ofn.lpstrDefExt = kDefExtPmd;
    ofn.nMaxFileTitle = 0x100;
    ofn.lpstrFileTitle = nullptr;
    ofn.lpstrTitle = app->state.englishUI != 0
                         ? kTitleLoadModel
                         : kTitleOpenFileJp;
    if (GetOpenFileNameW(&ofn) != 0) {
        if ((GetMenuState(GetMenu(hwnd), 0x12D, 0) & 8) != 0) {
            wchar_t* dir = ExtractDirFromPath(
                app->PathWorkspace().projectDirectory,
                fileBuf);
            CopyDirPathW(app->DirModel(), dir);
        }
        LoadModelFile(app, fileBuf);
    }
    RefreshRequest(-1);
}
// ------------------------------------------------------------------
// 437 (0x0047FCF5): delete model selected in combo 0x1B4.  Gate
// dword[0xA0B50]; CB_GETCURSEL(0x1B4) -> slot lookup by model byte
// +0x2D7C (x64 为 255 槽：查找环 0x7FF7CB460A73 mov r13d,0FFh、
// any-model 环 0x7FF7CB460C2C、逐槽修复环 0x7FF7CB460DB5 均以
// r13=0xFF 为界); confirm MessageBox (JP caption "モデル削除"
// unconditionally; EN text "Trying to delete Model(%s)...", JP
// "モデル：%sを削除します..."; name at model+0x227A EN / +0x2248 JP;
// flags 0x40001 when 0xA0D38 else 1); teardown of the model-edit
// sub-window (SeekSelectedModelToCurrentFrame, free 0xA0B7C/0xA0C30, DestroyWindow 0xA0B74,
// enable 0x1B4/0x198); dispose model (0x40A710(model, 1)), clear the
// slot, 0xA042C = any-model flag; combo rebuild (CB_RESETCONTENT
// 0x1B4/0x1DA/0x1C1 with old id, CB_SETCURSEL 0x1B4, CB_GETLBTEXT +
// CB_ADDSTRING 0x1D7 -> 0x1B2); menu 0x120/0x121 enable, then per
// remaining model: menu disable + id fixups (2D7C/2D7D decrement, bone
// table 0x4CCE4, camera frame pointers model+0x26E8); when the deleted
// slot was the edited one (0xA0430) reset it and re-sync combos 0x1C1/
// 0x1C2; clear bone-frame references (0x374 +0x4C/+0x50) and
// accessory-frame references (0x9DD70 +0x230/+0x234 and the 0x384
// blob +0x10/+0x0C); ApplyModelComboSelection.
// ------------------------------------------------------------------
static void Cmd400_DeleteModel(MMDApp* app, HWND hwnd) {
    if (app->FrameRangeDialog() != nullptr) {
        return;  // was: break (switch exit)
    }
    const LRESULT sel =
        SendMessageA(GetDlgItem(hwnd, panel::kMainComboModel), CB_GETCURSEL, 0, 0);
    app->state.enterKeyState = 1;
    std::int32_t found = -1;
    // 槽查找环：x64 0x7FF7CB460A73 mov r13d,0FFh（界 255）
    for (std::int32_t i = 0; i < kModelSlotCount; ++i) {
        unsigned char* m = app->ModelSlot(i);
        if (m != nullptr && mdl::Mdl(m)->comboSelIndex == sel) {
            found = i;
            break;
        }
    }
    if (found < 0) {
        return;  // was: break (switch exit)
    }
    app->state.enterKeyState = 1;
    unsigned char* model = app->ModelSlot(found);
    const mdl::ModelRecord& record = *mdl::Mdl(model);
    char text[0x100];
    if (app->state.englishUI != 0) {
        sprintf_s(text, 0x100, kMsgDelModelEn, record.nameEn);
    } else {
        sprintf_s(text, 0x100, kMsgDelModelJp, record.name);
    }
    const std::uint32_t flags =
        app->state.floatingWindow != 0 ? (MB_OKCANCEL | MB_TOPMOST) : MB_OKCANCEL;
    if (MessageBoxA(hwnd, text, kCaptionDelModelJp, flags) != 1) {
        return;  // was: break (switch exit)
    }
    if (app->state.frameCopyDialog != 0) {
        SeekSelectedModelToCurrentFrame(app);
        if (app->state.rigidScratchArray != nullptr) {
            free(app->state.rigidScratchArray);
            app->state.rigidScratchArray = nullptr;
        }
        if (app->state.jointScratchArray != nullptr) {
            free(app->state.jointScratchArray);
            app->state.jointScratchArray = nullptr;
        }
        DestroyWindow(reinterpret_cast<HWND>(
            app->state.frameCopyDialog));
        app->state.frameCopyDialog = nullptr;
        EnableWindow(GetDlgItem(hwnd, panel::kMainComboModel), TRUE);
        EnableWindow(GetDlgItem(hwnd, panel::kPlayButton), TRUE);
    }
    const std::int32_t oldSel = record.comboSelIndex;
    const std::int32_t oldIdx = record.comboSelIndex2;
    if (model != nullptr) {
        DeleteModel(model, 1);  // model dispose (thiscall)
    }
    app->ModelSlot(found) = nullptr;
    app->SceneModified() = 1;
    app->state.mainModelComboSelection = 0;
    // any-model 扫描环：x64 0x7FF7CB460C2C mov rcx,r13（=0xFF，同界）
    for (std::int32_t i = 0; i < kModelSlotCount; ++i) {
        if (app->ModelSlot(i) != nullptr) {
            app->state.mainModelComboSelection = 1;
        }
    }
    // combo rebuild: 0x1B4 / 0x1DA / 0x1C1 reset with the old id as
    // wParam, 0x1B4 cursor 0, accessory names 0x1D7 -> 0x1B2
    SendMessageA(GetDlgItem(hwnd, panel::kMainComboModel), CB_DELETESTRING,
                 static_cast<WPARAM>(sel), 0);
    SendMessageA(GetDlgItem(hwnd, panel::kMainComboGround), CB_DELETESTRING,
                 static_cast<WPARAM>(sel), 0);
    SendMessageA(GetDlgItem(hwnd, panel::kMainComboNormal), CB_DELETESTRING,
                 static_cast<WPARAM>(sel), 0);
    SendMessageA(GetDlgItem(hwnd, panel::kMainComboModel), CB_SETCURSEL, 0, 0);
    const LRESULT accCount =
        SendMessageA(GetDlgItem(hwnd, panel::kAccessoryCombo), CB_GETCOUNT, 0, 0);
    for (std::int32_t i = 0; i < accCount; ++i) {
        char buf[0x100];
        SendMessageA(GetDlgItem(hwnd, panel::kAccessoryCombo), CB_GETLBTEXT,
                     static_cast<WPARAM>(i),
                     reinterpret_cast<LPARAM>(buf));
        SendMessageA(GetDlgItem(hwnd, panel::kRegisterScopeCombo), CB_ADDSTRING, 0,
                     reinterpret_cast<LPARAM>(buf));
    }
    EnableMenuItem(GetMenu(hwnd), 0x120, 1);
    EnableMenuItem(GetMenu(hwnd), 0x121, 1);
    // per-slot fixup：x64 0x7FF7CB460DB5 mov rax,r13（=0xFF）+ 尾部 dec rax/jne
    for (std::int32_t j = 0; j < kModelSlotCount; ++j) {
        unsigned char* m = app->ModelSlot(j);
        if (m == nullptr) {
            continue;
        }
        EnableMenuItem(GetMenu(hwnd), 0x120, 0);
        EnableMenuItem(GetMenu(hwnd), 0x121, 0);
        if (mdl::Mdl(m)->comboSelIndex > oldSel) {
            --mdl::Mdl(m)->comboSelIndex;
        }
        if (mdl::Mdl(m)->comboSelIndex2 > oldIdx) {
            --mdl::Mdl(m)->comboSelIndex2;
        }
        // bone parent table 0x4CCE4 (0x14 stride) - entries pointing at
        // the deleted slot index are severed
        mdl::ModelRecord& record = *mdl::Mdl(m);
        mdl::DetachBoneBindings(record.boneOrderTable, record.boneOrderCount,
                                found);
        mdl::DisplayKey* displayKeys = mdl::DisplayKeys(m);
        for (std::int32_t keyIndex = 0; keyIndex < 1000; ++keyIndex) {
            auto* states = mdl::SelectorStates(displayKeys[keyIndex]);
            const std::int32_t cnt = record.boneOrderCount;
            for (std::int32_t k = 0; k < cnt; ++k) {
                if (states[k].modelIndex == found) {
                    states[k].modelIndex = -1;
                    states[k].boneIndex = 0;
                }
            }
        }
    }
    if (app->CameraParentModel() == found) {
        app->CameraParentModel() = -1;
        app->CameraParentBone() = 0;
        SendMessageA(GetDlgItem(hwnd, panel::kMainComboNormal), CB_SETCURSEL, 0, 0);
        SendMessageA(GetDlgItem(hwnd, panel::kBoneRegisterCombo), CB_RESETCONTENT, 0,
                     0);
    }
    // Clear camera key attachment references to the deleted model.
    {
        auto* keys = app->CameraKeys();
        for (std::int32_t i = 0; i < 10000; ++i) {
            if (keys[i].parentModel == found) {
                keys[i].parentModel = -1;
                keys[i].parentBone = 0;
            }
        }
    }
    // clear accessory references (0x9DD70 slots + 0x384 frame blobs)
    for (std::int32_t i = 0; i < 0xFF; ++i) {
        mdl::AccessoryRecord* acc = app->AccessorySlot(i);
        if (acc == nullptr) {
            continue;
        }
        if (acc->parentModel == found) {
            acc->parentModel = -1;
            acc->parentBone = 0;
        }
        auto* keys = app->AccessoryKeys(i);
        if (keys != nullptr) {
            for (std::int32_t keyIndex = 0; keyIndex < 10000;
                 ++keyIndex) {
                mdl::AccessoryKey& key = keys[keyIndex];
                if (key.parentModel == found) {
                    key.parentModel = -1;
                    key.visible = 0;
                }
            }
        }
    }
    ApplyModelComboSelection(app);
}
// ------------------------------------------------------------------
// 442 (0x0048D759): accessory edit dialog.  Dirty 0xBC, dialog flag
// byte 0xA0665 = 1, DialogBoxParamA(instance, template 0x329 EN /
// 0x328 JP, hwnd, sub_47A3F0, 0); on result != 2 (cancelled): dirty
// 0xA0B0D, flag 0xA0665 = 0, free the dialog buffers 0xA0B1C /
// 0xA0B24 / 0xA0668; when byte 0xA0664 != 0:
// SeekModelFrame(model, frame 0x980, 0xA0CC4) + PostLanguageSweep +
// SelectionReeval.
// ------------------------------------------------------------------
static void Cmd400_AccessoryEditDialog(MMDApp* app, HWND hwnd) {
    app->state.enterKeyState = 1;
    app->state.accessoryEditDialogOpen = 1;
    const std::intptr_t result =
        DialogBoxParamA(static_cast<HINSTANCE>(app->HInstance()),
                        app->state.englishUI != 0
                            ? reinterpret_cast<LPCSTR>(0x329)
                            : reinterpret_cast<LPCSTR>(0x328),
                        hwnd, &SelectNavDlgProc, 0);
    if (result == 2) {
        return;  // was: break (switch exit)
    }
    app->SceneModified() = 1;
    app->state.accessoryEditDialogOpen = 0;
    if (app->DialogOrders().modelIndices != nullptr) {
        app->DialogOrders().modelIndices.reset();
    }
    if (app->AccessoryEditArray() != nullptr) {
        free(app->AccessoryEditArray());
        app->AccessoryEditArray() = nullptr;
    }
    if (app->state.selectNavRecords != nullptr) {
        delete[] app->state.selectNavRecords;
        app->state.selectNavRecords = nullptr;
    }
    if (app->AccessoryApplyGate() == 0) {
        return;  // was: break (switch exit)
    }
    unsigned char* model = ActiveModel(app);
    // original: sub_4B4260(ecx = model, frame = app+0x980,
    // app+0xA0CC4)
    SeekModelFrame(model,
              app->state.currentFrame,
              app->PlaybackPhysicsMode());
    PostLanguageSweep(app);
    SelectionReeval(app);
}

void CmdControl400(MMDApp* app, HWND hwnd, std::uint16_t id,
                   std::uint16_t notify) {
    switch (id) {
    case 400:  // undo (0x00482766) -> Cmd400_Undo
        Cmd400_Undo(app, hwnd);
        break;

    case 401:  // redo (0x004828DF) -> Cmd400_Redo
        Cmd400_Redo(app, hwnd);
        break;

    // ------------------------------------------------------------------
    // 402 (0x0047EEDF): camera/view reset - 0x308..0x318 cleared, angle
    // -45, 0x30C = 0 (byte 0x340 == 2) or 10, light matrix identity; the
    // 0x2F8==0 path is gated: skipped when dword[0xA0430] >= 0 AND slot
    // == dword[0xA0430] AND byte 0x330 == 0 AND byte 0x9ED98 != 0; flag
    // 0xA0478 set when (0xA0430 >= 0 && 0x9ED98 != 0).  All paths end at
    // loc_47EFF6 (PostViewRefresh + RefreshRequest(-1)).
    // 403 (0x0047F163): same with 0x314 = PI (flt_52B738).
    // 404 (0x0047F211): 0x310 = -PI/2 (flt_52E8FC), 0x30C = 0 without the
    // byte-0x340 gate.
    // 405 (0x0047F00B): 0x314 = +PI/2 (flt_52E900).
    // 406 (0x0047F0B5): 0x314 = -PI/2 (flt_52E8FC).
    // ------------------------------------------------------------------
    case 402:
        ResetViewVariant(app, 0.0f, true, false);
        break;
    case 403:
        ResetViewVariant(app, 3.1415927f, true, false);  // flt_52B738 = PI
        break;
    case 404:
        ResetViewVariant(app, -1.5707964f, false, true);  // flt_52E8FC
        break;
    case 405:
        ResetViewVariant(app, 1.5707964f, true, false);  // flt_52E900
        break;
    case 406:
        ResetViewVariant(app, -1.5707964f, true, false);  // flt_52E8FC
        break;

    case 407:  // view reset + reload (0x0047F29C) -> Cmd400_ViewResetReload
        Cmd400_ViewResetReload(app);
        break;

    case 408:  // play-range edits (0x00487446) -> Cmd400_PlayRangeEdits
        Cmd400_PlayRangeEdits(app, hwnd);
        break;

    // ------------------------------------------------------------------
    // 411/413/414 (0x0047FAE5 / 0x0047FAFA / 0x0047FB0F): toggle bytes
    // 0x341 / 0x342 / 0x9ED99 (setz cl / al / cl; mov [..], cl).
    // ------------------------------------------------------------------
    case 411:
        app->PlaybackLoopEnabled() =
            app->PlaybackLoopEnabled() == 0 ? 1 : 0;
        break;
    case 413:
        app->state.playbackReturnsToStartFrame =
            app->state.playbackReturnsToStartFrame == 0 ? 1 : 0;
        break;
    case 414:
        app->PlaybackStartsAtCurrentFrame() =
            app->PlaybackStartsAtCurrentFrame() == 0 ? 1 : 0;
        break;

    // ------------------------------------------------------------------
    // 412 (0x0047F9FE): checkbox 0x19C - view-mode byte 0x340 := 1 when
    // checked (BM_GETCHECK) / 0 when not; 0x213 is unchecked (BM_SETCHECK
    // 0) in the checked branch; ApplyCameraReferenceModeChange(app, old value) runs only when
    // byte 0x2F8 == 0.
    // ------------------------------------------------------------------
    case 412: {
        const LRESULT checked =
            SendMessageA(GetDlgItem(hwnd, panel::kCameraRefModelCheckbox), BM_GETCHECK, 0, 0);
        const int prev = static_cast<int>(app->CameraReferenceMode());
        if (checked == 1) {
            app->CameraReferenceMode() =
                CameraAttachmentReference::ModelRoot;
            SendMessageA(GetDlgItem(hwnd, panel::kCameraRefBoneCheckbox), BM_SETCHECK, 0, 0);
            if (app->state.optflag[0] != 0) {
                break;
            }
            ApplyCameraReferenceModeChange(app, prev);
        } else {
            app->CameraReferenceMode() = CameraAttachmentReference::None;
            if (app->state.optflag[0] != 0) {
                break;
            }
            ApplyCameraReferenceModeChange(app, prev);
        }
        break;
    }

    // ------------------------------------------------------------------
    // 418/419 (0x00480734 / 0x0048072A): single refresh helpers.
    // ------------------------------------------------------------------
    case 418:
        StepFrame(app, false);
        break;
    case 419:
        StepFrame(app, true);
        break;

    // ------------------------------------------------------------------
    // 429 (0x004862E0): frame-scroll gate.  count = (sidebar - 0x54) / 26
    // (magic 0x4EC4EC4F, sar 3); dword 0x97C = frame(0x980) - count when
    // positive else 0; PanelPaint; when byte 0xA06CC != 0:
    // 0x4C2A00(0xCC sub, x = 0x97C, ho = sidebar) + InvalidateRect of
    // {6, 0x5F, sidebar-3, 0x92}.
    // ------------------------------------------------------------------
    case 429: {
        const std::int32_t sidebar =
            app->SidebarWidth();
        const std::int32_t count = (sidebar - 0x54) / 26;
        const std::int32_t frame =
            app->state.currentFrame;
        if (frame > count) {
            app->state.timelineStartFrame = frame - count;
        } else {
            app->state.timelineStartFrame = 0;
        }
        PanelPaint(app);
        if (app->state.waveEnabled != 0) {
            TimelineDrawTicks(app->state.timelineStartFrame,
                              sidebar);
            RECT rc;
            rc.left = 6;
            rc.top = 0x5F;
            rc.right = sidebar - 3;
            rc.bottom = 0x92;
            InvalidateRect(hwnd, &rc, FALSE);
        }
        break;
    }

    case 430:  // curve <- frame (0x00481109) -> Cmd400_CurveCopyFromFrame
        Cmd400_CurveCopyFromFrame(app, hwnd);
        break;

    case 431:  // curve -> frames (0x00481430) -> Cmd400_CurvePasteToFrames
        Cmd400_CurvePasteToFrames(app, hwnd);
        break;

    case 432:  // curve reset (0x004816E6) -> Cmd400_CurveResetRows
        Cmd400_CurveResetRows(app, hwnd);
        break;

    case 435:  // load model (0x0047EB7B) -> Cmd400_LoadModel
        Cmd400_LoadModel(app, hwnd);
        break;

    // ------------------------------------------------------------------
    // 436 is handled by the original dispatcher's default chain rather
    // than its command-ID jump table: CBN_SELCHANGE from the model combo
    // enters ApplyModelComboSelection when the modal/model-load gate at 0xA0B50 is clear.
    // ------------------------------------------------------------------
    case 436:
        if (notify == CBN_SELCHANGE &&
            app->FrameRangeDialog() == nullptr) {
            ApplyModelComboSelection(app);
        }
        break;

    case 437:  // delete model (0x0047FCF5) -> Cmd400_DeleteModel
        Cmd400_DeleteModel(app, hwnd);
        break;

    // ------------------------------------------------------------------
    // 438 (0x00480651): clear keyframe selection marks: 0x26E0 (0x3C
    // stride, +0x38), 0x26E4 (0x14 stride, +0x10), 0x26E8 (0x1C stride,
    // +0x14) of the active model; RegisterDisplayKeyCurrent(model, dword 0x980);
    // 0x9E16C = max(0x9E16C, model+0x31B0); PanelPaint.  Dirty 0xA0B0D.
    // ------------------------------------------------------------------
    case 438: {
        app->SceneModified() = 1;
        unsigned char* model = ActiveModel(app);
        ClearModelKeyMarks(mdl::BoneKeys(model), mdl::kBoneKeyCapacity);
        ClearModelKeyMarks(mdl::MorphKeys(model), 20000);
        ClearModelKeyMarks(mdl::DisplayKeys(model), 1000);
        RegisterDisplayKeyCurrent(model, app->state.currentFrame);
        const std::int32_t v =
            static_cast<std::int32_t>(mdl::Mdl(model)->maxFrame);
        if (app->state.lastRegisteredFrame < v) {
            app->state.lastRegisteredFrame = v;
        }
        PanelPaint(app);
        break;
    }

    // ------------------------------------------------------------------
    // 439 (0x0047F3AF): checkbox 0x1B7 -> model byte 0x2D8D (1/0).
    // ------------------------------------------------------------------
    case 439: {
        mdl::Mdl(ActiveModel(app))->loadComplete =
            IsDlgButtonChecked(hwnd, panel::kModelVisibleCheckbox) == 1 ? 1
                                                                        : 0;
        break;
    }

    // ------------------------------------------------------------------
    // 440/441 (0x0048A166 / 0x0048A133): model-flag toggles 0x37C0 /
    // 0x31BE when the active model exists; the original then jumps into
    // def_47E903, a no-op for these ids.
    // ------------------------------------------------------------------
    case 440: {
        unsigned char* model = ActiveModel(app);
        if (model != nullptr) {
            mdl::ModelRecord& record = *mdl::Mdl(model);
            record.toonFlag = record.toonFlag == 0 ? 1 : 0;
        }
        break;
    }
    case 441: {
        unsigned char* model = ActiveModel(app);
        if (model != nullptr) {
            mdl::ModelRecord& record = *mdl::Mdl(model);
            record.postLoadFlag2 = record.postLoadFlag2 == 0 ? 1 : 0;
        }
        break;
    }

    // ------------------------------------------------------------------
    // 444/445 (0x0047F319 / 0x0047F364): combo 0x1BB selection writes the
    // flag byte +0x12 of the 24-byte-stride table model+0x26C0 (1 / 0).
    // ------------------------------------------------------------------
    case 444: {
        const LRESULT sel =
            SendMessageA(GetDlgItem(hwnd, panel::kIkChainCombo), CB_GETCURSEL, 0, 0);
        unsigned char* model = ActiveModel(app);
        mdl::IkChain* chains = mdl::Mdl(model)->ikChains;
        if (chains != nullptr) {
            chains[sel].enabled = 1;
        }
        break;
    }
    case 445: {
        const LRESULT sel =
            SendMessageA(GetDlgItem(hwnd, panel::kIkChainCombo), CB_GETCURSEL, 0, 0);
        unsigned char* model = ActiveModel(app);
        mdl::IkChain* chains = mdl::Mdl(model)->ikChains;
        if (chains != nullptr) {
            chains[sel].enabled = 0;
        }
        break;
    }

    // ------------------------------------------------------------------
    // 446 (0x00486392): perspective FOV toggle.  When byte 0x31C != 0:
    // clear it and rebuild the projection - D3DXMatrixPerspectiveFovLH(
    // &m, fov = 0x9E1E8 * pi/180, aspect = locale->aspectRatio (0x1D4EC),
    // zn = 1, zf = 100000.0), scene (locale->device, 0x1D4E0) vtable slot 0xB0
    // (obj, 3, &m), RefreshRequest(-1).  Else: byte 0x31C = 1 +
    // RefreshRequest(-1).
    // ------------------------------------------------------------------
    case 446: {
        if (app->CameraPerspective() != 0) {
            app->CameraPerspective() = 0;
            d3dx::D3DXMATRIXF mat{};
            auto* d3dx = &d3dx::Get();
            D3DRenderer* locale = app->Renderer();
            const float fov = static_cast<float>(
                static_cast<double>(app->CameraFov()) *
                0.0174532925199433);  // dbl_52BB20 (pi/180)
            const float aspect = locale->aspectRatio;  // 0x1D4EC
            {
                d3dx->perspectiveFovLH(&mat, fov, aspect, 1.0f, 100000.0f);
            }
            IDirect3DDevice9* device = locale->device;
            device->SetTransform(
                D3DTS_PROJECTION,
                reinterpret_cast<const D3DMATRIX*>(&mat));
            RefreshRequest(-1);
        } else {
            app->CameraPerspective() = 1;
            RefreshRequest(-1);
        }
        break;
    }

    // ------------------------------------------------------------------
    // 423 (0x004810F8): DeleteMarkedKeyframes(app) then dirty 0xA0B0D.
    // ------------------------------------------------------------------
    case 423:
        DeleteMarkedKeyframes(app);
        app->SceneModified() = 1;
        break;

    case 442:  // accessory edit dialog (0x0048D759) -> Cmd400_AccessoryEditDialog
        Cmd400_AccessoryEditDialog(app, hwnd);
        break;

    case 415:  // frame-range selection (0x004829E4) -> Cmd400_FrameRangeSelect
        Cmd400_FrameRangeSelect(app, hwnd);
        break;

    case 416:  // selection-mark sync (0x0048073E) -> Cmd400_SelectionMarkSync
        Cmd400_SelectionMarkSync(app);
        break;

    case 420:  // frame copy (0x004834D2) -> Cmd400_FrameCopy
        Cmd400_FrameCopy(app, hwnd);
        break;

    case 421:  // frame paste (0x00484973) -> Cmd400_FramePaste
        Cmd400_FramePaste(app, hwnd);
        break;

    case 422:  // reverse paste (0x00485CF2) -> Cmd400_FrameReversePaste
        Cmd400_FrameReversePaste(app, hwnd);
        break;

    // ------------------------------------------------------------------
    // 424 (0x00488300): frame-edit dialog.  0x2F8 == 0 only; dirty
    // 0xBC, then DialogBoxParamA with the locale-gated template (0x25E
    // when byte 0xA0B4C == 0 i.e. JP UI, 0x28C otherwise) and the
    // sub_44C5D0 dialog proc.
    // ------------------------------------------------------------------
    case 424: {
        if (app->state.optflag[0] != 0) {
            break;
        }
        app->state.enterKeyState = 1;
        if (app->state.englishUI != 0) {
            DialogBoxParamA(static_cast<HINSTANCE>(app->HInstance()),
                            reinterpret_cast<LPCSTR>(0x28C), hwnd,
                            &FrameRangeDlgProc, 0);
        } else {
            DialogBoxParamA(static_cast<HINSTANCE>(app->HInstance()),
                            reinterpret_cast<LPCSTR>(0x25E), hwnd,
                            &FrameRangeDlgProc, 0);
        }
        break;
    }

    default:
        // Remaining controls in this family have no action in the original
        // default chain.
        break;
    }
}

}  // namespace mikudancestudio
