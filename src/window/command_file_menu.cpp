// ===========================================================================
// CommandDispatch family: File/Play/Edit/View menu cases (0x47E8A0)
// ===========================================================================
// Cases 200..250 (0xC8..0xFA) of the 368-case switch in CommandDispatch
// (0x47E8A0): file open/save dialogs (GetOpenFileNameW/GetSaveFileNameW),
// modal editor dialogs (DialogBoxParamA), menu check toggles
// (CheckMenuItem), confirmations (MessageBox) and frame-maintenance loops.
//   PORTING NOTE: stub migrated from src/app/late_ports.cpp (phase
//   scaffolding); real body filled by parallel port.  Reference:
//   ../translated/MikuMikuDance/fcn_0047e8a0.cpp
//
// Family semantics:
//   * The dispatch is thiscall: ebp == the app object (0x47E8F4 mov ebp,ecx),
//     so every [ebp+off] below is app field `off`.
//   * The file-open cases share the CommandImpl pattern: SetCurrentDirectoryW
//     (exe dir) -> OPENFILENAMEW with original filter/title strings ->
//     loader call (stubbed, VA recorded) -> dirty flag byte 0xA0B0D.
//   * DialogBoxParamA calls keep their original template ids (EN/JP) and
//     unported dialog procs as file-local extern stubs (see "Unported
//     dependencies" below; stubs.cpp must not be touched).
//   * 0xD4/0xDB/0xDE gate on the UI option flag byte 0x2F8 (kByteOptflag0).
//
// Case map (id -> original VA, jumptable 0x48F30C / index byte 0x48F650
// verified by coordinator - do not re-verify):
//   200:0x48BCF9  201:0x48E17F  202:0x487A6A  203:0x48789C  204:0x4898F9
//   205:0x489950  206:0x4876EC  207:0x489AF7  208:0x489B12  209:0x487E44
//   210:0x487BD0  211:0x47EAC7  212:0x47E90A  213:0x487091  214:0x487FA0
//   215:0x47FBF5  216:0x4871F9  217:0x4831EA  218:0x4832C8  219:0x48817E
//   220:0x483258  221:0x47FC97  222:0x48835D  223:0x489C62  224:0x488AE8
//   225:0x488DDE  226:0x488BD4  227:0x48944B  228:0x4890A7  229:0x48B4DF
//   230:0x48DACB  231:0x489279  232:0x487266  233:0x4873DB  234:0x48801B
//   235:0x488071  236:0x4880C7  237:0x483339  238:0x48338D  239:0x4833DA
//   240:0x483427  241:0x483474  242:0x48811D  243:0x489760  244:0x4897CE
//   245:0x489835  246:0x48989C  247:0x489E10  248:0x489F5B  249:0x48A03B
//   250:0x485387
//
// Implemented elsewhere (SKIP, do not re-implement):
//   200 (File: New), 0xC9 (About), 0xCA (load VPD), 0xCB (save VPD),
//   0xCC (reset), 0xCD (open PMM), 0xCE (open WAV), 0xCF/0xD3 (play/pause),
//   0xD0 (save PMM), 0xD1 (load VMD), 0xD2 (save VMD), 0xD5 (load AVI),
//   0xD6/0xD7/0xD8 (display toggles), 0xD9/0xDA/0xDC (select-all frames) -
//   all live in command_dispatch.cpp / command_impl.cpp.
//
// Ported in this file (id -> behaviour -> VA; all verified against the
// IDA disassembly of sub_47E8A0):
//   212 (0xD4, 0x47E90A)  screen-size dialog (sub_40ECE0, tpl 0x28D/0x25F);
//                         on OK re-run model/physics/render init chain
//   219 (0xDB, 0x48817E)  model-offset dialog (sub_40EEF0, tpl 0x28B/0x258);
//                         on OK add app+0xA08E4/8/C to every root-bone frame
//                         position, then PanelPaint + undo-record + dirty
//   221 (0xDD, 0x47FC97)  model-panel display toggle: app+0x918 byte +
//                         CheckMenuItem(0xDD, MF_CHECKED/UNCHECKED)
//   222 (0xDE, 0x48835D)  delete unused frames: clear used-flags, walk the
//                         bone/morph/accessory frame chains marking
//                         duplicates (x87 "<=" idiom), DeleteMarkedKeyframes purge,
//                         "%d point was deleted." report
//   223 (0xDF, 0x489C62)  output AVI: GetSaveFileNameW ("AVI files(*.avi)",
//                         Flags 6) + AVI-out options dialog (sub_40F2F0,
//                         tpl 0x28E/0x260) + 0x464760/0x45E820
//   224 (0xE0, 0x488AE8)  load VSQ: OPENFILENAMEW ("vsq files(*.vsq)",
//                         "UserFile\Vsq", defext "vsq"), LoadVsqFile (VSQ load)
//   225 (0xE1, 0x488DDE)  frame-shift dialog (sub_40F0B0,
//                         tpl 0x28F/0x267): drop chain frames while
//                         (frame0 + app+0xA08F4) <= 0
//   226 (0xE2, 0x488BD4)  delete lip frames: confirm + PurgeMorphFrames(3)
//   227 (0xE3, 0x48944B)  randomly register blinking: find "まばたき" morph
//                         (9-byte name), blink dialog (sub_40F1E0,
//                         tpl 0x290/0x269), rand()-driven keyframes at
//                         frame/+2/+3/+6 via RegisterMorphKeyCurrent
//   228 (0xE4, 0x4890A7)  delete eyes frames: confirm + PurgeMorphFrames(2)
//   231 (0xE7, 0x489279)  delete eyebrow frames: confirm + PurgeMorphFrames(1)
//   232 (0xE8, 0x487266)  load background picture (multi-format filter,
//                         defext "bmp", DirBg/"UserFile\BackGround"),
//                         LoadBackgroundPicture + dirty
//   233 (0xE9, 0x4873DB)  accessory-bone display toggle (app+0x9E428 +
//                         0xE9 menu check, gated on app+0x9E42C)
//   234/235/236 (0xEA/0xEB/0xEC) FPS-cap radios: 1000/30/60 into
//                         app+0xA08E0 + menu checks
//   237..241 (0xED..0xF1) select-all frames of each family (bone/morph/
//                         camera/accessory/255 acc tables; "record used or
//                         first record" quirk) + PanelPaint
//   242 (0xF2, 0x48811D)  camera-frame multiply dialog (sub_44D2D0, tpl 0x291/
//                         0x26F); NOTE inverted gate (runs when 0x2F8 != 0)
//   243..246 (0xF3..0xF6) panel-mode radios: app+0x9EB84 0..3 + menu checks
//   247 (0xF7, 0x489E10)  undo on/off toggle: app+0x9ED98 + 0xF7 check +
//                         0x217 checkbox, camera floats cleared, reload
//                         chain + PostModelReload/PostViewRefresh/
//                         PostLanguageSweep2
//   248 (0xF8, 0x489F5B)  modeless ground-shadow-color dialog (sub_42DFF0, tpl 0x292/0x270)
//                         stored into app+0xA0B14, SW_SHOW
//   249 (0xF9, 0x48A03B)  modal accessory-settings dialog (sub_44CA40, tpl 0x293/0x273)
//   250 (0xFA, 0x485387)  paste to difference flame: confirm (accessary/
//                         bone by 0x2F8), selection clears, accessary
//                         record replay (app+0x370, 0x34 stride,
//                         0x414110) or bone paste (undo records at
//                         model+0x26EC..0x2700, 0x24 snapshots,
//                         ResetBoneKeyCursor, record replay app+0x354, 0x54
//                         stride, RegisterBoneKey) + refresh chain
//   229 (0xE5, 0x48B4DF)  clear all frames: used-flags cleared, every
//                         morph gets a frame-0 keyframe at the current
//                         frame, watermark + undo record + refresh
//   230 (0xE6, 0x48DACB)  reset morph weights: table weights zeroed,
//                         panel morph sliders/edits (0x1F9..0x209) reset
// TODO(port): cases 251..302 (View/option menu) live in
//              command_view_menu.cpp; 282..287 menu toggles sit between
//              0x48B624 and 0x48DACB in the original (out of scope here).
// =========================================================================//
#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <Windows.h>
#include <commctrl.h>
#include <commdlg.h>

#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <new>

#include "mikudancestudio/mmd_app.hpp"
#include "mikudancestudio/accessory_layout.hpp"
#include "mikudancestudio/ported_funcs.hpp"
#include "mikudancestudio/model.hpp"
#include "mikudancestudio/panel_controls.hpp"

#include "dialog_scaffold.hpp"

namespace mikudancestudio {

// ---------------------------------------------------------------------------
// No offset-style app-state accesses remain in this file.  The one
// file-local offset below is a MODEL-object field (not app state),
// reached through model pointer arithmetic.
// ---------------------------------------------------------------------------
// D3D wrapper fields (the 0x1D574 render subsystem object reached via
// app->Renderer(); layout in d3d_wrapper.hpp): stereoEnabled (+0x1D566,
// stereo-3D gate byte), maxTextureWidth/maxTextureHeight (+0x1D568/+0x1D56C,
// D3DCAPS9 caps).

// ---------------------------------------------------------------------------
// Japanese strings, byte-exact Shift-JIS as in the binary.
// ---------------------------------------------------------------------------
// 0x52FC88: "不要ポイントの削除" (delete-unused-frame caption, 0xDE)
static const char kCaptionDelUnusedJp[] =
    "\x95\x73\x97\x76"                     // 不要
    "\x83\x7C\x83\x43\x83\x93\x83\x67"     // ポイント
    "\x82\xCC\x8D\xED\x8F\x9C";            // の削除
// 0x52FB60: "リップフレーム削除" (delete-lip-frame caption, 0xE2)
static const char kCaptionDelLipJp[] =
    "\x83\x8A\x83\x62\x83\x76\x83\x74\x83\x8C\x81\x5B\x83\x80"  // リップフレーム
    "\x8D\xED\x8F\x9C";                                        // 削除
// 0x52FB08: "リップのフレームを全て削除します\n表情フレームの変更は元に戻せません\n\nよろしいですか？"
static const char kMsgDelLipJp[] =
    "\x83\x8A\x83\x62\x83\x76\x82\xCC"     // リップの
    "\x83\x74\x83\x8C\x81\x5B\x83\x80\x82\xF0"  // フレームを
    "\x91\x53\x82\xC4\x8D\xED\x8F\x9C\x82\xB5\x82\xDC\x82\xB7\n"  // 全て削除します
    "\x95\x5C\x8F\xEE\x83\x74\x83\x8C\x81\x5B\x83\x80"  // 表情フレーム
    "\x82\xCC\x95\xCF\x8D\x58\x82\xCD"    // の変更は
    "\x8C\xB3\x82\xC9\x96\xDF\x82\xB9\x82\xDC\x82\xB9\x82\xF1\n\n"  // 元に戻せません
    "\x82\xE6\x82\xEB\x82\xB5\x82\xA2\x82\xC5\x82\xB7\x82\xA9\x81\x48";  // よろしいですか？
// 0x52FA58: "目のフレームを全て削除します\n表情フレームの変更は元に戻せません\n\nよろしいですか？"
static const char kMsgDelEyesJp[] =
    "\x96\xDA\x82\xCC\x83\x74\x83\x8C\x81\x5B\x83\x80\x82\xF0"  // 目のフレームを
    "\x91\x53\x82\xC4\x8D\xED\x8F\x9C\x82\xB5\x82\xDC\x82\xB7\n"  // 全て削除します
    "\x95\x5C\x8F\xEE\x83\x74\x83\x8C\x81\x5B\x83\x80"  // 表情フレーム
    "\x82\xCC\x95\xCF\x8D\x58\x82\xCD"    // の変更は
    "\x8C\xB3\x82\xC9\x96\xDF\x82\xB9\x82\xDC\x82\xB9\x82\xF1\n\n"  // 元に戻せません
    "\x82\xE6\x82\xEB\x82\xB5\x82\xA2\x82\xC5\x82\xB7\x82\xA9\x81\x48";  // よろしいですか？
// 0x52F9A8: "まゆのフレームを全て削除します\n表情フレームの変更は元に戻せません\n\nよろしいですか？"
static const char kMsgDelEyebrowJp[] =
    "\x82\xDC\x82\xE4\x82\xCC\x83\x74\x83\x8C\x81\x5B\x83\x80\x82\xF0"  // まゆのフレームを
    "\x91\x53\x82\xC4\x8D\xED\x8F\x9C\x82\xB5\x82\xDC\x82\xB7\n"  // 全て削除します
    "\x95\x5C\x8F\xEE\x83\x74\x83\x8C\x81\x5B\x83\x80"  // 表情フレーム
    "\x82\xCC\x95\xCF\x8D\x58\x82\xCD"    // の変更は
    "\x8C\xB3\x82\xC9\x96\xDF\x82\xB9\x82\xDC\x82\xB9\x82\xF1\n\n"  // 元に戻せません
    "\x82\xE6\x82\xEB\x82\xB5\x82\xA2\x82\xC5\x82\xB7\x82\xA9\x81\x48";  // よろしいですか？
// 0x52FAAC: "新規作成" - the original reuses this string as the JP caption
// of the delete-eyes / delete-eyebrow confirmations (shared constant).
static const char kCaptionNewJp[] =
    "\x90\x56\x8B\x4B\x8D\xEC\x90\xAC";  // 新規作成
// 0x52F900: "このモデルの表情に「まばたき」がない為\n自動リップブランクはできません"
static const char kMsgNoBlinkMorphJp[] =
    "\x82\xB1\x82\xCC\x83\x82\x83\x66\x83\x8B\x82\xCC\x95\x5C\x8F\xEE\x82\xC9"
    "\x81\x68\x82\xDC\x82\xCE\x82\xBD\x82\xAB\x81\x68\x82\xAA\x82\xC8\x82\xA2"
    "\x88\xD7\n\x8E\xA9\x93\xAE\x83\x8A\x83\x62\x83\x76\x83\x56\x83\x93\x83\x4E"
    "\x82\xCD\x82\xC5\x82\xAB\x82\xDC\x82\xB9\x82\xF1";
// 0x52A570: "まばたきランダム登録"
static const char kCaptionRegBlinkJp[] =
    "\x82\xDC\x82\xCE\x82\xBD\x82\xAB\x83\x89\x83\x93\x83\x5F\x83\x80\x93\x6F\x98\x5E";
// 0x52F8A0: "まばたき追加" (blink-count report caption)
static const char kCaptionBlinkCntJp[] =
    "\x82\xDC\x82\xCE\x82\xBD\x82\xAB\x92\xC7\x89\xC1";
// x64 0x7FF7CB54EB38 (x86 0x52FC9C): "%d個のポイントを削除しました" - the
// delete-unused-frames report format (case 222, JP branch of
// "%d point was deleted.").
static const char kFmtPointsDeletedJp[] =
    "%d"
    "\x8C\xC2"                                // 個
    "\x82\xCC"                                // の
    "\x83\x7C\x83\x43\x83\x93\x83\x67"        // ポイント
    "\x82\xF0"                                // を
    "\x8D\xED\x8F\x9C"                        // 削除
    "\x82\xB5\x82\xDC\x82\xB5\x82\xBD";       // しました
// x64 0x7FF7CB54EF80 (x86 0x52F8B0): "%d回のまばたきを追加しました" - the
// random-blink-registration report format (case 227, JP branch of
// "%d blinking is registerd").
static const char kFmtBlinksAddedJp[] =
    "%d"
    "\x89\xF1"                                // 回
    "\x82\xCC"                                // の
    "\x82\xDC\x82\xCE\x82\xBD\x82\xAB"        // まばたき
    "\x82\xF0"                                // を
    "\x92\xC7\x89\xC1"                        // 追加
    "\x82\xB5\x82\xDC\x82\xB5\x82\xBD";       // しました
// 0x52F99C: "まばたき" + NULs - the blink-morph name compared by 9 bytes.
static const char kNameMabataki[] =
    "\x82\xDC\x82\xCE\x82\xBD\x82\xAB\x00\x00\x00\x00";
// 0x530A88: "別コピー先へペースト" (case 250 caption)
static const char kCaptionPasteJp[] =
    "\x95\xCA\xCC\xDA\xB0\xD1\x82\xD6\xCD\xDF\xB0\xBD\xC4";
// 0x5309A0: accessary paste confirmation (case 250)
static const char kMsgPasteAccJp[] =
    "\x8C\xBB\x8D\xDD\x83\x52\x83\x73\x81\x5B\x97\xCC\x88\xE6\x82\xC9"
    "\x8A\x69\x94\x5B\x82\xB3\x82\xEA\x82\xC4\x82\xA2\x82\xE9"
    "\x83\x41\x83\x4E\x83\x5A\x83\x54\x83\x8A\x82\xCC\x83\x74\x83\x8C"
    "\x81\x5B\x83\x80\x83\x66\x81\x5B\x83\x5E\x82\xF0\n"
    "\x83\x41\x83\x4E\x83\x5A\x83\x54\x83\x8A\x96\xBC\x82\xF0\x96\xB3"
    "\x8E\x8B\x82\xB5\x82\xC4\x91\x80\x8D\xEC\x91\xCE\x8F\xDB"
    "\x83\x41\x83\x4E\x83\x5A\x83\x54\x83\x8A\x82\xCC\x83\x74\x83\x8C"
    "\x81\x5B\x83\x80\x82\xF0\x8D\xED\x8F\x9C\x82\xB5\x82\xDC\x82\xB7\n\n"
    "\x82\xE6\x82\xEB\x82\xB5\x82\xA2\x82\xC5\x82\xB7\x82\xA9\x81\x48";
// 0x5308C8: bone paste confirmation (case 250)
static const char kMsgPasteBoneJp[] =
    "\x8C\xBB\x8D\xDD\x83\x52\x83\x73\x81\x5B\x97\xCC\x88\xE6\x82\xC9"
    "\x8A\x69\x94\x5B\x82\xB3\x82\xEA\x82\xC4\x82\xA2\x82\xE9"
    "\x83\x74\x83\x8C\x81\x5B\x83\x80\x83\x66\x81\x5B\x83\x5E\x82\xF0"
    "\x83\x7B\x81\x5B\x83\x93\x96\xBC\x82\xF0\x96\xB3\x8E\x8B\x82\xB5"
    "\x82\xC4\n\x91\x80\x8D\xEC\x91\xCE\x8F\xDB\x83\x7B\x81\x5B\x83\x93"
    "\x28\x89\xE6\x96\xCA\x8D\xB6\x8F\xE3\x82\xC9\x95\x5C\x8E\xA6\x29"
    "\x82\xF0\x91\x49\x91\xF0\x82\xB5\x82\xC4\n\x8D\xED\x8F\x9C\x82\xB5"
    "\x82\xDC\x82\xB7\n\n\x82\xE6\x82\xEB\x82\xB5\x82\xA2\x82\xC5\x82\xB7"
    "\x82\xA9\x81\x48";
// 0x530878 / 0x530834: "操作対象アクセサリ/ボーン(画面左上に表示)を選択して下さい"
static const char kMsgSelectAccJp[] =
    "\x91\x80\x8D\xEC\x91\xCE\x8F\xDB\x83\x41\x83\x4E\x83\x5A\x83\x54"
    "\x83\x8A\x28\x89\xE6\x96\xCA\x8D\xB6\x8F\xE3\x82\xC9\x95\x5C\x8E\xA6"
    "\x29\x82\xF0\x91\x49\x91\xF0\x82\xB5\x82\xC4\x89\xBA\x82\xB3\x82\xA2";
static const char kMsgSelectBoneJp[] =
    "\x91\x80\x8D\xEC\x91\xCE\x8F\xDB\x83\x7B\x81\x5B\x83\x93\x28\x89\xE6"
    "\x96\xCA\x8D\xB6\x8F\xE3\x82\xC9\x95\x5C\x8E\xA6\x29\x82\xF0\x91\x49"
    "\x91\xF0\x82\xB5\x82\xC4\x89\xBA\x82\xB3\x82\xA2";

// 0x529688: swprintf_s format L"\0\0%s%s" - leading NULs make the original
// call (which passes NO varargs) write an empty string; the %s slots are
// never consumed.  Kept byte-identical; the two dummy args below are never
// read (only silence the compiler's format-string warning).
static const wchar_t kEmptyPathFormat[] = L"\x0\x0%s%s";

// 0x52B834: "このPCで処理できる大きさを超えています" (canvas-size dialog
// "too big for your PC!" JP text, 0x40ECE0; x64 0x7FF7CB54F9E8 is
// byte-identical)
static const char kMsgCanvasTooBigJp[] =
    "\x82\xB1\x82\xCC\x50\x43\x82\xC5"   // このPCで
    "\x8F\x88\x97\x9D\x82\xC5\x82\xAB"   // 処理でき
    "\x82\xE9\x91\xE5\x82\xAB\x82\xB3"   // る大きさ
    "\x82\xF0\x92\xB4\x82\xA6\x82\xC4"   // を超えて
    "\x82\xA2\x82\xDC\x82\xB7";          // います
// 0x52B884: "終了フレームが開始フレームより小さいです" (AVI end<start JP
// text, 0x40F2F0).  The bytes mix SJIS kanji with half-width katakana
// (ﾌﾚｰﾑ = CC DA B0 D1) exactly as stored in the binary - copied verbatim.
static const char kMsgEndLessJp[] =
    "\x98\x5E\x89\xE6"        // 終了
    "\x8F\x49\x97\xB9"        // ﾌﾚｰﾑ (JIS X 0212 form of フレーム)
    "\xCC\xDA\xB0\xD1"        // ﾌﾚｰﾑ (half-width)
    "\x82\xAA"                // が
    "\x98\x5E\x89\xE6"        // 終了
    "\x8A\x4A\x8E\x6E"        // 開始
    "\xCC\xDA\xB0\xD1"        // ﾌﾚｰﾑ (half-width)
    "\x82\xE6\x82\xE8\x82\xE0"  // よりも
    "\x8F\xAC\x82\xB3\x82\xA2\x82\xC5\x82\xB7";  // 小さいです
// 0x52B784: "カメラ" / 0x52D390: "照明" / 0x52D398: "セルフ影" /
// 0x52D3A4: "重力" - accessory-type combo entries of the modal dialog
// (0x44CA40), sent as wide strings (SendMessageW).
static const wchar_t kAccTypeCameraJp[] = L"\x30AB\x30E1\x30E9";       // カメラ
static const wchar_t kAccTypeLightJp[] = L"\x7167\x660E";              // 照明
static const wchar_t kAccTypeSelfShadowJp[] = L"\x30BB\x30EB\x30D5\x5F71";  // セルフ影
static const wchar_t kAccTypeGravityJp[] = L"\x91CD\x529B";            // 重力

// 0x529679: the original's "Locale" label - a zero-initialised string blob,
// so the canvas "too big" MessageBox caption is EMPTY (the original pushes
// this address directly as lpCaption).
static const char kEmptyCaption[] = "";

// ---------------------------------------------------------------------------
// External targets ported in other translation units (declared here with
// their original VAs; not yet registered in ported_funcs.hpp).
// ---------------------------------------------------------------------------
void PanelPaint(MMDApp* app);                       // VA 0x00414610
void SelectionReeval(MMDApp* app);                  // VA 0x00430510 (stubs.cpp)

// ---------------------------------------------------------------------------
// Unported dependencies - kept as file-local external stubs with the call
// sites intact (stubs.cpp must not be touched).  TODO(port): replace with
// real bodies as the corresponding functions are ported.  The dialog procs
// are now implemented below ("Dialog proc implementations").
// ---------------------------------------------------------------------------
void RefreshMainWindowViewport(MMDApp* app);  // VA 0x0042C810 viewport refresh
void AviBgOverlayRefresh(MMDApp* app);             // VA 0x004168D0 (bg_overlay.cpp)
void PicBgOverlayRefresh(MMDApp* app);             // VA 0x00417130 (bg_overlay.cpp)
void PostDeviceReset(MMDApp* app);             // VA 0x00440DB0 (device_reset.cpp)
void LayoutViewportPanels(MMDApp* app);             // VA 0x0040CAC0
                                                   // (ui_viewport_layout.cpp)
void SnapshotSelectedKeysForUndo(unsigned char* model, int frame);  // VA 0x004A1510
int SeekModelFrame(unsigned char* model, int frame, int physicsMode);  // VA 0x004B4260 frame seek
void DeleteMarkedKeyframes(MMDApp* app);                       // VA 0x004316B0 frame
                                                   // cleanup (delete unused)
void LoadVsqFile(MMDApp* app, const wchar_t* path);  // VA 0x00435FE0 VSQ load
void CopyDirPathW(wchar_t* dest, const wchar_t* src); // VA 0x0042AE20 path copy
// 0x42AE40 path copy - real port in src/media/media_load.cpp as CopyPathW
void CopyPathW(wchar_t* dest, const wchar_t* src);
void StartAviRecordFullscreen(MMDApp* app);        // VA 0x00464760 (avi_record_start.cpp)
void StartAviRecordWindow(MMDApp* app);            // VA 0x0045E820 (avi_record_start.cpp)
void RegisterMorphKeyCurrent(unsigned char* model, int morph,
               int frame);                         // VA 0x0049EEE0 frame
                                                   // register (blink keys)
void RefreshLightPanel(MMDApp* app);                       // VA 0x00411070 undo chain
void RefreshSelfShadowPanel(MMDApp* app);                       // VA 0x00411B90 undo chain
void ApplyGravityTrack(MMDApp* app);                       // VA 0x00412330 undo chain
void ApplyAccessoryTrack(MMDApp* app, int slot);             // VA 0x00413120 accessory
                                                   // reload (undo chain)
void SyncAccessoryEditPanel(MMDApp* app);                       // VA 0x004134E0 undo chain
// 0x4337A0 picture load - real port in src/media/media_load.cpp
void LoadBackgroundPicture(MMDApp* app);
                                                   // load (0xE8)
int PasteAccessoryKeyRecord(MMDApp* app, void* rec, int useSelectedSlot);  // VA 0x00414110
                                                   // accessary paste step (returns
                                                   // int: 0 stops the replay loop)
void ResetBoneKeyCursor(unsigned char* model);              // VA 0x004A4940 frame
                                                   // paste helper (0xFA)
bool RegisterBoneKey(unsigned char* model, unsigned char* rec, int frameOffset,
                   unsigned char useSelected);  // VA 0x0049D880
                                                   // paste step (0xFA)
void IdentityCtor(void* obj);                         // VA 0x004C46F0 ctor
void* ConstructArrayElements(void* block, std::uint32_t elementSize,
                             std::uint32_t count,
                             void* ctor);          // VA 0x00401150

// 32-bit multiply with the original's overflow idiom (mul/seto/neg/or):
// returns 0xFFFFFFFF when the product overflows (case 250 allocations).
std::uint32_t MulOrMax(std::uint32_t a, std::uint32_t b) {
    const std::uint64_t r = static_cast<std::uint64_t>(a) * b;
    return r > 0xFFFFFFFFull ? 0xFFFFFFFFu : static_cast<std::uint32_t>(r);
}

// ---------------------------------------------------------------------------
// Helpers
// ---------------------------------------------------------------------------
// Active model = slot array at this+0x780 indexed by byte this+0x910
// (sub_47E8A0 pattern; the original re-derives it on every use).
static unsigned char* ActiveModel(MMDApp* app) {
    return app->SelectedModel();
}

HWND MainHwnd(MMDApp* app) {
    return static_cast<HWND>(app->Hwnd());  // app+0xA06B8
}

// Shared tail of the morph-frame purge cases 226/228/231 (and the 225
// variant): undo record, panel repaint, selection re-eval, dirty flag.
void FramePurgeTail(MMDApp* app) {
    unsigned char* model = ActiveModel(app);
    SeekModelFrame(model, app->state.currentFrame,
              app->PlaybackPhysicsMode());
    PanelPaint(app);      // 0x414610
    SelectionReeval(app); // 0x430510
    app->SceneModified() = 1;
}

// 0xE2/0xE4/0xE7 morph-frame purge (0x488C4C / 0x48911F / 0x4892F1): for
// every morph of the active model whose type byte (morph table +0x58)
// equals `type`, clear the frame record of the morph itself and of every
// chain successor that still has a successor (the chain tail survives -
// original quirk).  The frame records live at model+0x26E4 (0x14 stride);
// +8 is the chain successor index, +4/+0/+0xC/+0x10 the cleared fields.
void PurgeMorphFrames(MMDApp* app, std::uint8_t type) {
    unsigned char* model = ActiveModel(app);
    const std::int32_t morphCount =
        static_cast<std::int32_t>(mdl::Mdl(model)->morphCount);
    for (std::uint16_t m = 0; static_cast<std::int32_t>(m) < morphCount; ++m) {
        model = ActiveModel(app);
        const mdl::MorphRecord* const morphs = mdl::Morphs(model);
        if (morphs[m].type != type) {
            continue;
        }
        mdl::MorphKey* frames = mdl::MorphKeys(model);
        std::int32_t cur = m;  // chain cursor, starts at the morph itself
        if (frames[m].next != 0) {
            for (;;) {
                frames[cur].frame = 0;
                frames[cur].previous = 0;
                frames[cur].allocated = 0;
                frames[cur].value = 0.0f;
                const std::int32_t next =
                    static_cast<std::int32_t>(frames[cur].next);
                frames[m].next = 0;
                cur = next;
                model = ActiveModel(app);  // original re-derives per step
                frames = mdl::MorphKeys(model);
                if (frames[cur].next == 0) {
                    break;
                }
            }
        }
        // the morph's own record is cleared again (loc_488D12 pattern)
        frames[m].frame = 0;
        frames[m].previous = 0;
        frames[m].allocated = 0;
        frames[m].value = 0.0f;
    }
}

// ---------------------------------------------------------------------------
// Dialog proc implementations - original VAs recorded.  All procs reach the
// app state through the g_Block global (the original's `Block` at 0x54593C,
// ebp of sub_47E8A0); every message returns 0 unless noted, exactly like
// the original (no DefDlgProc anywhere).  The "owned-mode topmost" dance is
// the original's SetWindowPos(hDlg, HWND_TOPMOST, 0,0,0,0, 3) gated on the
// app+0xA0D38 owner-handle byte.
// ---------------------------------------------------------------------------

// Forward declarations of the in-file dependency stubs defined below (the
// dialog procs reference them before their definitions).
int FillCodecCombo(DShowRecorder* recorder, HWND hCombo, char english);  // VA 0x00408F20
void RebindCodecOnComboChange(DShowRecorder* recorder, int sel, HWND hButton, char english);  // VA 0x00409730
void ShowCodecConfigDialog(DShowRecorder* recorder, HWND hDlg);  // VA 0x004092A0
void ApplyCameraFrameScaleAdd(MMDApp* app, HWND hDlg);  // VA 0x0043DAD0 (misc_dialogs.cpp)
void BuildAccessoryOrderArray(MMDApp* app, int count);        // VA 0x00439C90
void ApplyAccessorySettingsDialog(MMDApp* app, int count, HWND hDlg);  // VA 0x00439D00
LRESULT __stdcall GroundShadowColorEditSubclassProc(HWND, UINT, WPARAM, LPARAM);  // VA 0x0040F730

// VA 0x0040ECE0 - screen-size dialog proc (template caption "screen size" /
// 出力画面サイズ変更; case 212, tpl 0x28D/0x25F).
// WM_INITDIALOG: topmost, edits 0x26D/0x26E pre-filled with "%3d" of
// app+0xA08D4/0xA08D8 (render w/h), focus + select-all on 0x26D.  OK: atol
// both edits; when either exceeds the D3D wrapper caps (maxTextureWidth/
// maxTextureHeight, +0x1D568/+0x1D56C) a "too big for your PC!" MessageBox (EN/JP) with an
// EMPTY caption (the original pushes the zero-initialised blob at
// 0x529679) is shown; otherwise the values land in app+0xA08D4/0xA08D8 and
// EndDialog(1).  Cancel: EndDialog(2).
INT_PTR __stdcall ScreenSizeDlgProc(HWND hDlg, UINT msg, WPARAM wParam,
                                    LPARAM lParam) {  // VA 0x0040ECE0
    MMDApp* app = g_Block;
    (void)lParam;
    char text[0x100];
    if (msg == WM_INITDIALOG) {
        MakeDialogTopmostIfRequested(app, hDlg);
        sprintf_s(text, 0x100, "%3d",
                  app->RenderWidth());
        PrefillEdit(hDlg, panel::kScreenWidthEdit, text);
        sprintf_s(text, 0x100, "%3d",
                  app->RenderHeight());
        PrefillEdit(hDlg, panel::kScreenHeightEdit, text);
        SelectAllEdit(hDlg, panel::kScreenWidthEdit);
    } else if (msg == WM_COMMAND) {
        if (LOWORD(wParam) == 1) {  // IDOK
            const int w = ReadIntFromEdit(hDlg, panel::kScreenWidthEdit);
            const int h = ReadIntFromEdit(hDlg, panel::kScreenHeightEdit);
            const D3DRenderer* sub = app->Renderer();
            if (w > sub->maxTextureWidth ||  // 0x1D568
                h > sub->maxTextureHeight) {  // 0x1D56C
                MessageBoxA(hDlg,
                            app->EnglishUI() != 0 ? "too big for your PC!"
                                                  : kMsgCanvasTooBigJp,
                            kEmptyCaption, 0);
            } else {
                app->RenderWidth() = w;
                app->RenderHeight() = h;
                EndDialog(hDlg, 1);
            }
        } else if (LOWORD(wParam) == 2) {  // IDCANCEL
            EndDialog(hDlg, 2);
        }
    }
    return 0;
}

// VA 0x0040EEF0 - model-offset dialog proc (case 219, tpl 0x28B/0x258).
// WM_INITDIALOG: topmost, edits 0x259/0x25A/0x25B pre-filled "0.0", focus +
// select-all on 0x259.  OK: atof each edit into the model-offset floats
// app+0xA08E4/0xA08E8/0xA08EC and EndDialog(1).  Cancel: EndDialog(2).
INT_PTR __stdcall ModelOffsetDlgProc(HWND hDlg, UINT msg, WPARAM wParam,
                                     LPARAM lParam) {  // VA 0x0040EEF0
    MMDApp* app = g_Block;
    (void)lParam;
    if (msg == WM_INITDIALOG) {
        MakeDialogTopmostIfRequested(app, hDlg);
        PrefillEdit(hDlg, panel::kBiasXEdit, "0.0");
        PrefillEdit(hDlg, panel::kBiasYEdit, "0.0");
        PrefillEdit(hDlg, panel::kBiasZEdit, "0.0");
        SelectAllEdit(hDlg, panel::kBiasXEdit);
    } else if (msg == WM_COMMAND) {
        if (LOWORD(wParam) == 1) {  // IDOK
            app->state.modelOffsetX = ReadFloatFromEdit(hDlg, panel::kBiasXEdit);
            app->state.modelOffsetY = ReadFloatFromEdit(hDlg, panel::kBiasYEdit);
            app->state.modelOffsetZ = ReadFloatFromEdit(hDlg, panel::kBiasZEdit);
            EndDialog(hDlg, 1);
        } else if (LOWORD(wParam) == 2) {  // IDCANCEL
            EndDialog(hDlg, 2);
        }
    }
    return 0;
}

// VA 0x0040F0B0 - frame-shift dialog proc (template caption "frame number
// to shift" / シフトするﾌﾚｰﾑ数; case 225, tpl 0x28F/
// 0x267).  WM_INITDIALOG: topmost, edit 0x268 pre-filled "0", focus +
// select-all.  OK: app+0xA08F4 = atol(edit 0x268) and EndDialog(1).
// Cancel: EndDialog(2).
INT_PTR __stdcall FrameShiftDlgProc(HWND hDlg, UINT msg, WPARAM wParam,
                                    LPARAM lParam) {  // VA 0x0040F0B0
    MMDApp* app = g_Block;
    (void)lParam;
    if (msg == WM_INITDIALOG) {
        MakeDialogTopmostIfRequested(app, hDlg);
        PrefillEdit(hDlg, panel::kShiftFramesEdit, "0");
        SelectAllEdit(hDlg, panel::kShiftFramesEdit);
    } else if (msg == WM_COMMAND) {
        if (LOWORD(wParam) == 1) {  // IDOK
            app->MorphFrameShift() = ReadIntFromEdit(hDlg, panel::kShiftFramesEdit);
            EndDialog(hDlg, 1);
        } else if (LOWORD(wParam) == 2) {  // IDCANCEL
            EndDialog(hDlg, 2);
        }
    }
    return 0;
}

// VA 0x0040F1E0 - blink-register dialog proc (case 227, tpl 0x290/0x269).
// WM_INITDIALOG: topmost only.  OK: app+0xA08F8/0xA08FC = atol(edits
// 0x26A/0x26B) and EndDialog(1).  Cancel: EndDialog(2).
INT_PTR __stdcall BlinkRegisterDlgProc(HWND hDlg, UINT msg, WPARAM wParam,
                                       LPARAM lParam) {  // VA 0x0040F1E0
    MMDApp* app = g_Block;
    (void)lParam;
    if (msg == WM_INITDIALOG) {
        MakeDialogTopmostIfRequested(app, hDlg);
    } else if (msg == WM_COMMAND) {
        if (LOWORD(wParam) == 1) {  // IDOK
            app->BlinkStartFrame() = ReadIntFromEdit(hDlg, panel::kBlinkStartEdit);
            app->BlinkEndFrame() = ReadIntFromEdit(hDlg, panel::kBlinkEndEdit);
            EndDialog(hDlg, 1);
        } else if (LOWORD(wParam) == 2) {  // IDCANCEL
            EndDialog(hDlg, 2);
        }
    }
    return 0;
}

// VA 0x0040F2F0 - AVI-output options dialog proc (case 223, tpl 0x28E/
// 0x260).  WM_INITDIALOG: topmost; edits 0x261/0x262 copy the main-window
// frame edits (0x199/0x19A), 0x263 gets "30", 0x264/0x265 show the render
// size "%5d" and are disabled; checkbox 0x266 is checked when app+0xA06CC
// (else disabled); 0x326/0x327 disabled when the D3D wrapper stereo byte
// (stereoEnabled, +0x1D566) is clear; FillCodecCombo (method of the 0xA06C0 codec obj) fills
// the codec combo 0x274 and the selection is saved to app+0xA0CD8; the
// "test" button 0x2D4 starts disabled.  OK: start/end/fps edits ->
// app+0xA0B00/0xA0B04/0xA0B08, checkboxes -> app+0xA0B0C (0x266),
// app+0xA0D61 (0x326) and app+0xA0D64 (0x327, 1..2); EndDialog(1) only
// when end >= start, else a "WARNING" MessageBox (EN/JP).  Cancel:
// EndDialog(2).  CBN_SELCHANGE on 0x274 -> RebindCodecOnComboChange (codec
// bind) + app+0xA0CD8 update; button 0x2D4 -> ShowCodecConfigDialog (the
// codec's own config dialog + state capture).
INT_PTR __stdcall AviOutDlgProc(HWND hDlg, UINT msg, WPARAM wParam,
                                LPARAM lParam) {  // VA 0x0040F2F0
    MMDApp* app = g_Block;
    char text[0x100];
    if (msg == WM_INITDIALOG) {
        MakeDialogTopmostIfRequested(app, hDlg);
        const HWND main = MainHwnd(app);
        GetWindowTextA(GetDlgItem(main, panel::kPlayStartFrameEdit), text, 8);
        PrefillEdit(hDlg, panel::kAviStartFrameEdit, text);
        GetWindowTextA(GetDlgItem(main, panel::kPlayStopFrameEdit), text, 8);
        PrefillEdit(hDlg, panel::kAviEndFrameEdit, text);
        PrefillEdit(hDlg, panel::kAviFpsEdit, "30");
        sprintf_s(text, 0x100, "%5d",
                  app->RenderWidth());
        PrefillEdit(hDlg, panel::kAviWidthEdit, text);
        sprintf_s(text, 0x100, "%5d",
                  app->RenderHeight());
        PrefillEdit(hDlg, panel::kAviHeightEdit, text);
        EnableWindow(GetDlgItem(hDlg, panel::kAviWidthEdit), FALSE);
        EnableWindow(GetDlgItem(hDlg, panel::kAviHeightEdit), FALSE);
        if (app->state.waveEnabled != 0) {
            SendMessageA(GetDlgItem(hDlg, panel::kAviWaveCheckbox), BM_SETCHECK, 1, 0);
        } else {
            EnableWindow(GetDlgItem(hDlg, panel::kAviWaveCheckbox), FALSE);
        }
        const D3DRenderer* sub = app->Renderer();
        if (sub->stereoEnabled == 0) {  // 0x1D566
            EnableWindow(GetDlgItem(hDlg, panel::kAvi3dVisionCheckbox), FALSE);
            EnableWindow(GetDlgItem(hDlg, panel::kAviLeftHalfCheckbox), FALSE);
        }
        app->AviCodecSelection() = FillCodecCombo(
            app->Recorder(), GetDlgItem(hDlg, panel::kAviCodecCombo),
            app->EnglishUI());
        EnableWindow(GetDlgItem(hDlg, panel::kAviSettingsButton), FALSE);
    } else if (msg == WM_COMMAND) {
        if (LOWORD(wParam) == 1) {  // IDOK
            app->AviRecordStartFrame() =
                ReadIntFromEdit(hDlg, panel::kAviStartFrameEdit);
            app->AviRecordEndFrame() =
                ReadIntFromEdit(hDlg, panel::kAviEndFrameEdit);
            app->AviRecordFps() =
                ReadFloatFromEdit(hDlg, panel::kAviFpsEdit);
            app->AviIncludeWave() =
                IsDlgButtonChecked(hDlg, panel::kAviWaveCheckbox) == 1 ? 1 : 0;
            app->AviStereoOutput() =
                IsDlgButtonChecked(hDlg, panel::kAvi3dVisionCheckbox) == 1 ? 1 : 0;
            app->AviStereoWidthMultiplier() =
                (IsDlgButtonChecked(hDlg, panel::kAviLeftHalfCheckbox) != 1) + 1;
            if (app->AviRecordEndFrame() >= app->AviRecordStartFrame()) {
                EndDialog(hDlg, 1);
            } else if (app->EnglishUI() != 0) {
                MessageBoxA(hDlg, "End frame is less than start frame!",
                            "WARNING", 0);
            } else {
                MessageBoxA(hDlg, kMsgEndLessJp, "WARNING", 0);
            }
        } else if (LOWORD(wParam) == 2) {  // IDCANCEL
            EndDialog(hDlg, 2);
        } else if (HIWORD(wParam) == 1 /*CBN_SELCHANGE*/) {
            if (reinterpret_cast<HWND>(lParam) == GetDlgItem(hDlg, panel::kAviCodecCombo)) {
                const LRESULT sel = SendMessageA(GetDlgItem(hDlg, panel::kAviCodecCombo),
                                                 CB_GETCURSEL, 0, 0);
                if (app->AviCodecSelection() != sel) {
                    RebindCodecOnComboChange(app->Recorder(),
                              static_cast<int>(sel), GetDlgItem(hDlg, panel::kAviSettingsButton),
                              app->EnglishUI());
                    app->AviCodecSelection() =
                        static_cast<std::int32_t>(sel);
                }
            }
        } else if (LOWORD(wParam) == 0x2D4) {
            ShowCodecConfigDialog(app->Recorder(), hDlg);
        }
    }
    return 0;
}

// VA 0x0044D2D0 - camera-frame multiply dialog proc (template caption
// "multiply of camera frame position-angle" / ｶﾒﾗﾌﾚｰﾑ位置角度補正; case 242,
// tpl 0x291/0x26F).
// WM_INITDIALOG: topmost; the 8 scale/edit pairs 0x2AE..0x2BD are
// pre-filled ("1.0" on the even ids, "0.0" on the odd ids), focus +
// select-all on 0x2AE.  OK: ApplyCameraFrameScaleAdd (0x43DAD0, in
// misc_dialogs.cpp; reads the 16 edits into the camera frame transform
// scale/offset) then EndDialog(1).
// Cancel: EndDialog(2).
INT_PTR __stdcall CameraFrameMultiplyDlgProc(HWND hDlg, UINT msg, WPARAM wParam,
                                             LPARAM lParam) {  // VA 0x0044D2D0
    MMDApp* app = g_Block;
    (void)lParam;
    if (msg == WM_INITDIALOG) {
        MakeDialogTopmostIfRequested(app, hDlg);
        for (int i = 0; i < 16; i += 2) {
            PrefillEdit(hDlg, panel::kCamMulPosXScaleEdit + i, "1.0");
        }
        for (int j = 0; j < 16; j += 2) {
            PrefillEdit(hDlg, panel::kCamMulPosXOffsetEdit + j, "0.0");
        }
        SelectAllEdit(hDlg, panel::kCamMulPosXScaleEdit);
    } else if (msg == WM_COMMAND) {
        if (LOWORD(wParam) == 1) {  // IDOK
            ApplyCameraFrameScaleAdd(app, hDlg);  // 0x43DAD0
            EndDialog(hDlg, 1);
            return 0;
        }
        if (LOWORD(wParam) == 2) {  // IDCANCEL
            EndDialog(hDlg, 2);
            return 0;
        }
    }
    return 0;
}

// VA 0x0042DFF0 - modeless ground-shadow-color dialog proc (template caption
// "ground shadow color" / 地面影色設定; case 248, tpl 0x292/
// 0x270).  WM_INITDIALOG: topmost; the value edit 0x272 is subclassed
// (old wndproc saved to app+0xA0B18, new proc sub_40F730) and filled with
// "%3.2f" of the scale float app+0xA0BF0; the trackbar 0x271 gets range
// 0..200, a tic every 100 and position (int)(value*100).  WM_COMMAND
// IDCANCEL: DestroyWindow + app+0xA0B14=0 (returns 1).  WM_HSCROLL:
// app+0xA0D6C=1, the four app+0xA0BF0..0xA0BFC floats take
// TBM_GETPOS/100.0 and the edit is refreshed (returns 1).
INT_PTR __stdcall GroundShadowColorDlgProc(HWND hDlg, UINT msg, WPARAM wParam,
                                           LPARAM lParam) {  // VA 0x0042DFF0
    MMDApp* app = g_Block;
    (void)lParam;
    char text[0x100];
    switch (msg) {
    case WM_INITDIALOG:
        MakeDialogTopmostIfRequested(app, hDlg);
        reinterpret_cast<void*&>(app->GroundShadowColorEditProc()) = reinterpret_cast<void*>(
            GetWindowLongPtrA(GetDlgItem(hDlg, panel::kGroundShadowEdit), GWLP_WNDPROC));
        SetWindowLongPtrA(GetDlgItem(hDlg, panel::kGroundShadowEdit), GWLP_WNDPROC,
                       reinterpret_cast<LONG_PTR>(GroundShadowColorEditSubclassProc));
        sprintf_s(text, 0x100, "%3.2f", app->GroundShadowColor()[0]);
        PrefillEdit(hDlg, panel::kGroundShadowEdit, text);
        SendMessageA(GetDlgItem(hDlg, panel::kGroundShadowSlider), TBM_SETRANGEMIN, 0, 0);
        SendMessageA(GetDlgItem(hDlg, panel::kGroundShadowSlider), TBM_SETRANGEMAX, 0, 200);
        SendMessageA(GetDlgItem(hDlg, panel::kGroundShadowSlider), TBM_SETTICFREQ, 0x64, 0);
        SendMessageA(GetDlgItem(hDlg, panel::kGroundShadowSlider), TBM_SETPOS, 1,
                     static_cast<LPARAM>(
                         static_cast<int>(app->GroundShadowColor()[0] * 100.0)));
        break;
    case WM_COMMAND:
        if (LOWORD(wParam) == 2) {  // IDCANCEL
            DestroyWindow(hDlg);
            app->GroundShadowColorDialog() = 0;
            return 1;
        }
        break;
    case WM_HSCROLL: {
        app->state.messageSeen = 1;  // flag set first
        const float v = static_cast<float>(
            static_cast<double>(SendMessageA(GetDlgItem(hDlg, panel::kGroundShadowSlider),
                                             TBM_GETPOS, 0, 0)) /
            100.0);
        for (int c = 0; c < 4; ++c) {
            app->GroundShadowColor()[c] = v;
        }
        SendMessageA(GetDlgItem(hDlg, panel::kGroundShadowEdit), EM_SETSEL, 0,
                     GetWindowTextLengthA(GetDlgItem(hDlg, panel::kGroundShadowEdit)));
        sprintf_s(text, 0x100, "%3.2f", v);
        SendMessageA(GetDlgItem(hDlg, panel::kGroundShadowEdit), EM_REPLACESEL, 0,
                     (LPARAM)text);
        return 1;
    }
    }
    return 0;
}

// Port of the original file-scope global at 0x545934: accessory/effect list
// count captured at WM_INITDIALOG (CB_GETCOUNT of the main combo 0x1D7).
// The original global is only ever touched by sub_44CA40, so a file-local
// static is behaviourally identical.
static int g_accOrderCount = 0;

// VA 0x0044CA40 - modal accessory-order dialog proc (case 249, tpl 0x293/
// 0x273).  WM_INITDIALOG: topmost; the main-window accessory combo 0x1D7
// is copied into the dialog list 0x274 (CB_GETCOUNT/CB_GETLBTEXT ->
// LB_ADDSTRING), BuildAccessoryOrderArray rebuilds the order array (app+0xA0B1C, new
// 4*count), the count edit 0x27B shows app+0xA0B20 ("%d") and the list
// selection is cleared (-1).
//   0x276/0x277 (up/down): the selected item is swapped with its neighbour
//      in the listbox AND the order array.
//   0x278 (OK): count edit -> app+0xA0B20, ApplyAccessorySettingsDialog commits the reorder,
//      the main combo 0x1D7 is rebuilt from the list, the type combo 0x1B2
//      gets the four fixed entries ("camera"/"light"/"s shadow"/"grav" EN,
//      wide JP equivalents) plus every item of 0x1D7, EndDialog(1) and the
//      order array is freed.  IDCANCEL(2): EndDialog(2) + free.
//   633: rename - when the focus is not on edit 0x27B, the text of edit
//      0x275 replaces the selected list item (cancelled when the selection
//      is out of range or the edit is empty).
//   CBN_SELCHANGE on 0x274: edit 0x275 shows the selected item's text.
//   EN_CHANGE on 0x27B: the count is clamped to [0..count] (text rewritten
//      only when clamped) and the list selection reflects it (-1 when out
//      of range).
INT_PTR __stdcall AccessorySettingsDlgProc(HWND hDlg, UINT msg, WPARAM wParam,
                                           LPARAM lParam) {  // VA 0x0044CA40
    MMDApp* app = g_Block;
    char text[0x100];
    if (msg == WM_INITDIALOG) {
        MakeDialogTopmostIfRequested(app, hDlg);
        const HWND main = MainHwnd(app);
        g_accOrderCount = static_cast<int>(
            SendMessageA(GetDlgItem(main, panel::kAccessoryCombo), CB_GETCOUNT, 0, 0));
        reinterpret_cast<unsigned char*&>(app->AccessoryOrderArray()) = static_cast<unsigned char*>(
            ::operator new(4u * static_cast<std::uint32_t>(g_accOrderCount)));
        for (int i = 0; i < g_accOrderCount; ++i) {
            SendMessageA(GetDlgItem(main, panel::kAccessoryCombo), CB_GETLBTEXT, i,
                         (LPARAM)text);
            SendMessageA(GetDlgItem(hDlg, panel::kOrderListBox), LB_ADDSTRING, 0,
                         (LPARAM)text);
        }
        BuildAccessoryOrderArray(app, g_accOrderCount);
        sprintf_s(text, 0x64, "%d",
                  app->state.accessoryRenderSplitOrder);
        SetWindowTextA(GetDlgItem(hDlg, panel::kAccessoryOrderEdit), text);
        SendMessageA(GetDlgItem(hDlg, panel::kOrderListBox), LB_SETCURSEL,
                     0xFFFFFFFF, 0);
        return 0;
    }
    if (msg != WM_COMMAND) {
        return 0;
    }
    if (LOWORD(wParam) != 633) {
        switch (LOWORD(wParam)) {
        case 0x276: {  // move up
            const LRESULT sel = SendMessageA(GetDlgItem(hDlg, panel::kOrderListBox),
                                             LB_GETCURSEL, 0, 0);
            if (sel >= 1) {
                SendMessageA(GetDlgItem(hDlg, panel::kOrderListBox), LB_GETTEXT, sel,
                             (LPARAM)text);
                SendMessageA(GetDlgItem(hDlg, panel::kOrderListBox), LB_DELETESTRING,
                             sel, 0);
                SendMessageA(GetDlgItem(hDlg, panel::kOrderListBox), LB_INSERTSTRING,
                             sel - 1, (LPARAM)text);
                SendMessageA(GetDlgItem(hDlg, panel::kOrderListBox), LB_SETCURSEL,
                             sel - 1, 0);
                std::int32_t* order = reinterpret_cast<std::int32_t*&>(app->AccessoryOrderArray());
                const std::int32_t tmp = order[sel - 1];
                order[sel - 1] = order[sel];
                order[sel] = tmp;
            }
            return 0;
        }
        case 0x277: {  // move down
            const LRESULT sel = SendMessageA(GetDlgItem(hDlg, panel::kOrderListBox),
                                             LB_GETCURSEL, 0, 0);
            if (sel != -1 && sel < g_accOrderCount - 1) {
                SendMessageA(GetDlgItem(hDlg, panel::kOrderListBox), LB_GETTEXT, sel,
                             (LPARAM)text);
                SendMessageA(GetDlgItem(hDlg, panel::kOrderListBox), LB_DELETESTRING,
                             sel, 0);
                SendMessageA(GetDlgItem(hDlg, panel::kOrderListBox), LB_INSERTSTRING,
                             sel + 1, (LPARAM)text);
                SendMessageA(GetDlgItem(hDlg, panel::kOrderListBox), LB_SETCURSEL,
                             sel + 1, 0);
                std::int32_t* order = reinterpret_cast<std::int32_t*&>(app->AccessoryOrderArray());
                const std::int32_t tmp = order[sel + 1];
                order[sel + 1] = order[sel];
                order[sel] = tmp;
            }
            return 0;
        }
        case 0x278: {  // OK: commit the new order
            app->state.accessoryRenderSplitOrder =
                ReadIntFromEdit(hDlg, panel::kAccessoryOrderEdit, 256);
            ApplyAccessorySettingsDialog(app, g_accOrderCount, hDlg);
            const HWND main = MainHwnd(app);
            SendMessageA(GetDlgItem(main, panel::kAccessoryCombo), CB_RESETCONTENT, 0, 0);
            for (int j = 0; j < g_accOrderCount; ++j) {
                SendMessageA(GetDlgItem(hDlg, panel::kOrderListBox), LB_GETTEXT, j,
                             (LPARAM)text);
                SendMessageA(GetDlgItem(main, panel::kAccessoryCombo), CB_ADDSTRING, 0,
                             (LPARAM)text);
            }
            SendMessageA(GetDlgItem(main, panel::kAccessoryCombo), CB_SETCURSEL, 0, 0);
            SendMessageA(GetDlgItem(main, panel::kRegisterScopeCombo), CB_RESETCONTENT, 0, 0);
            if (app->EnglishUI() != 0) {
                SendMessageA(GetDlgItem(main, panel::kRegisterScopeCombo), CB_ADDSTRING, 0,
                             (LPARAM)"camera");
                SendMessageA(GetDlgItem(main, panel::kRegisterScopeCombo), CB_ADDSTRING, 0,
                             (LPARAM)"light");
                SendMessageA(GetDlgItem(main, panel::kRegisterScopeCombo), CB_ADDSTRING, 0,
                             (LPARAM)"s shadow");
                SendMessageA(GetDlgItem(main, panel::kRegisterScopeCombo), CB_ADDSTRING, 0,
                             (LPARAM)"grav");
            } else {
                SendMessageW(GetDlgItem(main, panel::kRegisterScopeCombo), CB_ADDSTRING, 0,
                             (LPARAM)kAccTypeCameraJp);
                SendMessageW(GetDlgItem(main, panel::kRegisterScopeCombo), CB_ADDSTRING, 0,
                             (LPARAM)kAccTypeLightJp);
                SendMessageW(GetDlgItem(main, panel::kRegisterScopeCombo), CB_ADDSTRING, 0,
                             (LPARAM)kAccTypeSelfShadowJp);
                SendMessageW(GetDlgItem(main, panel::kRegisterScopeCombo), CB_ADDSTRING, 0,
                             (LPARAM)kAccTypeGravityJp);
            }
            char buf[100];
            const LRESULT n = SendMessageA(GetDlgItem(main, panel::kAccessoryCombo),
                                           CB_GETCOUNT, 0, 0);
            for (LRESULT k = 0; k < n; ++k) {
                SendMessageA(GetDlgItem(main, panel::kAccessoryCombo), CB_GETLBTEXT, k,
                             (LPARAM)buf);
                SendMessageA(GetDlgItem(main, panel::kRegisterScopeCombo), CB_ADDSTRING, 0,
                             (LPARAM)buf);
            }
            SendMessageA(GetDlgItem(main, panel::kRegisterScopeCombo), CB_SETCURSEL, 0, 0);
            EndDialog(hDlg, 1);
            if (app->AccessoryOrderArray() != nullptr) {
                free(app->AccessoryOrderArray());
                app->AccessoryOrderArray() = nullptr;
            }
            return 0;
        }
        case 2:  // IDCANCEL
            EndDialog(hDlg, 2);
            if (app->AccessoryOrderArray() != nullptr) {
                free(app->AccessoryOrderArray());
                app->AccessoryOrderArray() = nullptr;
            }
            return 0;
        }
        if (HIWORD(wParam) == 1 /*CBN_SELCHANGE*/) {
            const LRESULT sel = SendMessageA(GetDlgItem(hDlg, panel::kOrderListBox),
                                             LB_GETCURSEL, 0, 0);
            if (sel >= 0) {
                SendMessageA(GetDlgItem(hDlg, panel::kOrderListBox), LB_GETTEXT, sel,
                             (LPARAM)text);
                SendMessageA(GetDlgItem(hDlg, panel::kAccessoryNameEdit), EM_SETSEL, 0,
                             GetWindowTextLengthA(GetDlgItem(hDlg, panel::kAccessoryNameEdit)));
                SendMessageA(GetDlgItem(hDlg, panel::kAccessoryNameEdit), EM_REPLACESEL, 0,
                             (LPARAM)text);
            }
            return 0;
        }
        if (reinterpret_cast<HWND>(lParam) != GetDlgItem(hDlg, panel::kAccessoryOrderEdit) ||
            HIWORD(wParam) != 0x300 /*EN_CHANGE*/) {
            return 0;
        }
        {
            GetWindowTextA(GetDlgItem(hDlg, panel::kAccessoryOrderEdit), text, 256);
            int orderIdx = atol(text);
            if (orderIdx > 0) {
                if (orderIdx > g_accOrderCount) {
                    orderIdx = g_accOrderCount;
                    sprintf_s(text, 0x64, "%d", g_accOrderCount);
                    SetWindowTextA(GetDlgItem(hDlg, panel::kAccessoryOrderEdit), text);
                    SendMessageA(GetDlgItem(hDlg, panel::kAccessoryOrderEdit), EM_SETSEL,
                                 0, GetWindowTextLengthA(GetDlgItem(hDlg, panel::kAccessoryOrderEdit)));
                }
            } else {
                orderIdx = 0;
                sprintf_s(text, 0x64, "%d", 0);
                SetWindowTextA(GetDlgItem(hDlg, panel::kAccessoryOrderEdit), text);
                SendMessageA(GetDlgItem(hDlg, panel::kAccessoryOrderEdit), EM_SETSEL, 0,
                             GetWindowTextLengthA(GetDlgItem(hDlg, panel::kAccessoryOrderEdit)));
            }
            const bool below = orderIdx < g_accOrderCount;
            SendMessageA(GetDlgItem(hDlg, panel::kOrderListBox), LB_SETCURSEL,
                         below ? static_cast<WPARAM>(orderIdx)
                               : static_cast<WPARAM>(0xFFFFFFFF),
                         0);
        }
        return 0;
    }
    // LOWORD == 633: rename the selected list item from edit 0x275 (the
    // original's IDCANCEL id - the dialog never closes on it).
    if (GetFocus() != GetDlgItem(hDlg, panel::kAccessoryOrderEdit)) {
        const LRESULT sel = SendMessageA(GetDlgItem(hDlg, panel::kOrderListBox),
                                         LB_GETCURSEL, 0, 0);
        GetWindowTextA(GetDlgItem(hDlg, panel::kAccessoryNameEdit), text, 256);
        if (text[0] != 0 && sel < g_accOrderCount && sel >= 0) {
            SendMessageA(GetDlgItem(hDlg, panel::kOrderListBox), LB_DELETESTRING,
                         sel, 0);
            SendMessageA(GetDlgItem(hDlg, panel::kOrderListBox), LB_INSERTSTRING,
                         sel, (LPARAM)text);
            SendMessageA(GetDlgItem(hDlg, panel::kOrderListBox), LB_SETCURSEL,
                         sel, 0);
        }
    }
    return 0;
}

// ---------------------------------------------------------------------------
// New unported dependencies of the dialog procs above - kept as in-file
// external-linkage stubs with the call sites intact (stubs.cpp must not be
// touched).  TODO(port): replace with real bodies as the corresponding
// functions are ported.
// ---------------------------------------------------------------------------
// 0x408F20 / 0x409730 / 0x4092A0 (AVI codec combo fill / bind / test) now
// have real bodies in src/app/dshow_record_graph.cpp; the forward
// declarations above are their only in-file reference.
// VA 0x0043DAD0 (camera frame-scale apply) now lives in misc_dialogs.cpp as
// ApplyCameraFrameScaleAdd (x64 sub_7FF7CB4BC330 semantics: float math and
// a kTimelineKeyCapacity-wide scan); the forward declaration above is its
// only in-file reference.
// VA 0x00439C90 - thiscall app method (this = Block): rebuilds the
// accessory order index array (app+0xA0B1C) from the 255 accessory
// objects (table at app+0x9DD70, order byte at obj+1181), then
// 0x42F1E0 + 0x40D070.
void BuildAccessoryOrderArray(MMDApp* app, int count) {  // VA 0x00439C90
    void** order = static_cast<void**>(
        app->AccessoryOrderArray());   // 0xA0B1C
    for (int i = 0; i < count; ++i) {
        for (int j = 0; j < 255; ++j) {
            mdl::AccessoryRecord* acc = app->AccessorySlot(j);
            if (acc != nullptr && acc->order == i) {
                order[i] = acc;                                 // 0x439CC6
                break;
            }
        }
    }
    PostLanguageSweep(app);                                     // 0x42F1E0
    PostLanguageSweep2(app);                                    // 0x40D070
}
// VA 0x00439D00 - thiscall app method (this = Block): commits the reorder
// - order byte (obj+1181) and name (obj+568) updated from the dialog list
// 0x274 (LB_GETTEXT per row), the selected-index byte (app+0x9E170) set
// from order[0], SyncAccessoryEditPanel, per-object show-flag sweep (+1196) over the
// 255 accessory slots (51 x 5 unrolled in the original), 0x42F1E0 +
// 0x40D070.
void ApplyAccessorySettingsDialog(MMDApp* app, int count, HWND hDlg) {  // VA 0x00439D00
    auto& s = *app;
    mdl::AccessoryRecord** order = static_cast<mdl::AccessoryRecord**>(
        app->AccessoryOrderArray());   // 0xA0B1C
    char text[100];
    const HWND list = GetDlgItem(hDlg, panel::kOrderListBox);                   // 0x274
    for (int i = 0; i < count; ++i) {
        mdl::AccessoryRecord* acc = order[i];
        if (acc == nullptr)
            continue;
        acc->order = static_cast<unsigned char>(i);             // 0x439D2D
        SendMessageA(list, LB_GETTEXT, i,
                     reinterpret_cast<LPARAM>(text));
        strcpy_s(acc->name, sizeof(acc->name), text);
    }
    s.SelectedObjectSlot() =
        order[0] != nullptr
            ? reinterpret_cast<unsigned char*>(order[0])[0] : 0; // 0x439D92
    SyncAccessoryEditPanel(app);                                             // 0x4134E0
    for (int j = 0; j < 255; ++j) {
        mdl::AccessoryRecord* acc = app->AccessorySlot(j);
        if (acc != nullptr)
            acc->rowSelected = 0;                               // 0x439DC7
    }
    {
        mdl::AccessoryRecord* selected =
            app->AccessorySlot(s.SelectedObjectSlot());
        if (selected != nullptr)
            selected->rowSelected = 1;                          // 0x439E34
    }
    PostLanguageSweep(app);                                     // 0x42F1E0
    PostLanguageSweep2(app);                                    // 0x40D070
}
// VA 0x0040F730 (x64 sub_7FF7CB476D30) - value-edit (0x272) subclass wndproc
// installed by sub_42DFF0's WM_INITDIALOG (SetWindowLongPtrA GWLP_WNDPROC;
// the original proc is first saved to app+0xA0B18).  Only WM_KEYDOWN
// wParam==13 (VK_RETURN) arriving on the edit of the live dialog (guard:
// hWnd == GetDlgItem(app dialog handle, 0x272)) takes the apply path;
// everything else goes to CallWindowProcA with the saved proc.
//   Apply path (disassembly verified):
//     GetWindowTextA(hWnd, buf, 8)  // nMaxCount 8 (lea r8d,[rbx-5] with 13)
//     -> atof -> float v -> the SAME v written to all four RGBA slots
//     (x64 app+0xA1D88..0xA1D94 = the 4-float shadow colour, grayscale
//     brightness, NOT four distinct semantic slots)
//     -> SendMessageA(GetDlgItem(dialog, 0x1CA), TBM_SETPOS, 1,
//                     (int)(v*100.0f))  // constant 0x42C80000 = 100.0f
//     -> return 0 (Enter swallowed).
//   Quirk kept 1:1: the trackbar message targets control 0x1CA (458),
//   which does NOT exist in template 0x292/0x270 (controls are only two
//   statics, edit 0x272 and trackbar 0x271 - verified against the original
//   PE's DLGTEMPLATEEX resources).  GetDlgItem returns NULL and the
//   SendMessage is a no-op: in the original, pressing Enter applies the
//   four floats immediately but does NOT snap the trackbar back; the
//   port's identical templates (res/templates/dialog/dialogs.rc 624/658)
//   reproduce this behaviour exactly.
LRESULT __stdcall GroundShadowColorEditSubclassProc(HWND hWnd, UINT uMsg,
                                                    WPARAM wParam,
                                                    LPARAM lParam) {  // VA 0x0040F730
    MMDApp* app = g_Block;
    if (uMsg != WM_KEYDOWN || wParam != 13 ||  // VK_RETURN
        hWnd != GetDlgItem(app->GroundShadowColorDialog(),
                           panel::kGroundShadowEdit)) {
        return CallWindowProcA(app->GroundShadowColorEditProc(), hWnd, uMsg,
                               wParam, lParam);
    }
    char text[16];  // original stack buffer CHAR[16]
    GetWindowTextA(hWnd, text, 8);
    const float v = static_cast<float>(atof(text));
    for (int c = 0; c < 4; ++c) {  // one value into all four RGBA floats
        app->GroundShadowColor()[c] = v;
    }
    SendMessageA(GetDlgItem(app->GroundShadowColorDialog(), 0x1CA), TBM_SETPOS,
                 1, static_cast<LPARAM>(static_cast<int>(v * 100.0f)));
    return 0;
}

void CmdFileMenu(MMDApp* app, HWND hwnd, std::uint16_t id, std::uint16_t notify) {
    (void)notify;
    switch (id) {
    // ------------------------------------------------------------------
    // 212 (0x0047E90A): screen-size dialog.  DialogBoxParamA template
    // 0x28D (EN) / 0x25F (JP), proc sub_40ECE0 which stores the typed
    // width/height into app+0xA08D4/0xA08D8 and returns 1 (OK) / 2
    // (cancel).  On OK the whole model/render chain is re-initialised.
    // ------------------------------------------------------------------
    case 212: {
        app->state.enterKeyState = 1;  // 0xBC
        const std::intptr_t result = DialogBoxParamA(
            static_cast<HINSTANCE>(app->HInstance()),  // this+0 hInstance
            MAKEINTRESOURCEA(app->EnglishUI() != 0 ? 0x28D : 0x25F),
            MainHwnd(app), ScreenSizeDlgProc, 0);
        if (result == 2) {  // IDCANCEL
            break;
        }
        app->SceneModified() = 1;
        RefreshMainWindowViewport(app);                                 // model init
        if (app->state.aviBackgroundEnabled == 1) {
            AviBgOverlayRefresh(app);                     // AVI bg overlay
        }
        if (app->state.pictureBackgroundEnabled != 0) {
            PicBgOverlayRefresh(app);                     // picture bg overlay
        }
        LayoutViewportPanels(app);                                 // render ops
        InvalidateRect(MainHwnd(app), nullptr, FALSE);
        break;
    }

    // ------------------------------------------------------------------
    // 219 (0x0048817E): model-offset dialog.  DialogBoxParamA template
    // 0x28B (EN) / 0x258 (JP), proc sub_40EEF0 which stores three floats
    // into app+0xA08E4/0xA08E8/0xA08EC.  On OK every existing root-bone
    // frame of the active model gets the offset added to its stored
    // position (+0x1C/+0x20/+0x24); PanelPaint + SelectionReeval +
    // undo-record + dirty + undo-available flags follow.  The bone index
    // for a frame is the key's `previous` link field; frames whose bone is not a root
    // (parent != -1) are skipped; the parent chain is walked when the
    // frame counter exceeds the bone count.
    // ------------------------------------------------------------------
    case 219: {
        if (app->state.optflag[0] != 0) {  // 0x2F8
            break;
        }
        app->state.dialogFlags[6] = 1;
        app->state.enterKeyState = 1;
        const std::intptr_t result = DialogBoxParamA(
            static_cast<HINSTANCE>(app->HInstance()),
            MAKEINTRESOURCEA(app->EnglishUI() != 0 ? 0x28B : 0x258),
            MainHwnd(app), ModelOffsetDlgProc, 0);
        if (result == 2) {  // IDCANCEL
            break;
        }
        unsigned char* model = ActiveModel(app);
        SnapshotSelectedKeysForUndo(model, app->state.currentFrame);
        std::int32_t frameIdx = 0;  // ebx: counts frames (incl. skipped)
        for (std::size_t frameIdx = 0;
             frameIdx < mdl::kBoneKeyCapacity; ++frameIdx) {
            model = ActiveModel(app);
            mdl::BoneKey* const keys = mdl::BoneKeys(model);
            mdl::BoneKey& key = keys[frameIdx];
            if (key.allocated != 0) {
                const std::int32_t boneCount =
                    static_cast<std::int32_t>(mdl::Mdl(model)->boneCount);
                std::int32_t bone = static_cast<std::int32_t>(frameIdx);
                if (bone >= boneCount) {
                    do {
                        bone = static_cast<std::int32_t>(keys[bone].previous);
                    } while (bone >= boneCount);
                }
                const mdl::BoneRecord* const bones = mdl::Bones(model);
                // root bone only (parent == -1); 0x48824B tests the
                // RESOLVED bone (uVar22 after the previous-chain walk)
                if (bones[bone].parent == -1) {
                    key.position[0] += app->state.modelOffsetX;
                    key.position[1] += app->state.modelOffsetY;
                    key.position[2] += app->state.modelOffsetZ;
                }
            }
        }
        PanelPaint(app);      // 0x414610
        SelectionReeval(app); // 0x430510
        model = ActiveModel(app);
        SeekModelFrame(model, app->state.currentFrame,
                  app->PlaybackPhysicsMode());
        app->SceneModified() = 1;
        app->PhysicsResetPending() = 1;
        break;
    }

    // ------------------------------------------------------------------
    // 221 (0x0047FC97): model-panel display toggle.  Flips app+0x918 and
    // the 0xDD menu checkmark.
    // ------------------------------------------------------------------
    case 221: {
        app->state.dialogFlags[8] = 1;
        if (app->state.groundShadowEnabled != 0) {
            app->state.groundShadowEnabled = 0;
            CheckMenuItem(GetMenu(MainHwnd(app)), 0xDD, MF_UNCHECKED);
        } else {
            app->state.groundShadowEnabled = 1;
            CheckMenuItem(GetMenu(MainHwnd(app)), 0xDD, MF_CHECKED);
        }
        break;
    }

    // ------------------------------------------------------------------
    // 222 (0x0048835D): delete unused frames.  Clears the used flag of
    // every frame (bone +0x38 / morph +0x10 / acc +0x14), then walks the
    // per-bone/per-morph/per-accessory frame chains and marks a frame when
    // its transform (or frame number / IK-name list / camera records)
    // EQUALS both neighbours: each component test is
    //   flag=1; fucompp; test $0x44; jnp keep-1; flag=0
    // i.e. equal (C3 alone -> odd parity -> keep 1), unequal or unordered
    // (NaN -> 0); all 14 per-component flags AND to 1 only for exact
    // duplicates.  DeleteMarkedKeyframes then purges the marked frames and a MessageBox
    // reports the count.
    // ------------------------------------------------------------------
    case 222: {
        if (app->state.optflag[0] != 0) {  // 0x2F8
            break;
        }
        app->state.dialogFlags[4] = 1;
        // clear the used flags of all three frame tables
        {
            unsigned char* m = ActiveModel(app);
            mdl::BoneKey* f = mdl::BoneKeys(m);
            for (std::size_t i = 0; i < mdl::kBoneKeyCapacity; ++i)
                f[i].allocated = 0;
        }
        {
            unsigned char* m = ActiveModel(app);
            mdl::MorphKey* f = mdl::MorphKeys(m);
            for (std::size_t i = 0; i < mdl::kMorphKeyCapacity; ++i)
                f[i].allocated = 0;
        }
        {
            unsigned char* m = ActiveModel(app);
            mdl::DisplayKey* f = mdl::DisplayKeys(m);
            for (std::size_t i = 0; i < mdl::kDisplayKeyCapacity; ++i)
                f[i].allocated = 0;
        }

        std::int32_t deleted = 0;  // var_A38

        // ---- bone frames ---------------------------------------------
        unsigned char* model = ActiveModel(app);
        const std::int32_t boneCount =
            static_cast<std::int32_t>(mdl::Mdl(model)->boneCount);
        for (std::uint16_t b = 0;
             static_cast<std::int32_t>(b) < boneCount; ++b) {
            model = ActiveModel(app);
            mikudancestudio::mdl::BoneRecord* boneFrames =
                mikudancestudio::mdl::Bones(model);
            const mdl::BoneType type = boneFrames[b].type;
            if (type == mdl::BoneType::InertTip ||
                type == mdl::BoneType::Effector) {
                continue;
            }
            unsigned char* frames = reinterpret_cast<unsigned char*>(
                mikudancestudio::mdl::BoneKeys(model));
            std::int32_t cur = *reinterpret_cast<std::int32_t*>(
                frames + 0x3C * b + 8);           // chain head
            if (cur == 0) {
                continue;
            }
            std::int32_t next = *reinterpret_cast<std::int32_t*>(
                frames + 0x3C * cur + 8);
            if (next == 0) {
                continue;
            }
            for (;;) {
                const std::int32_t prev = *reinterpret_cast<std::int32_t*>(
                    frames + 0x3C * cur + 4);
                const float curX = *reinterpret_cast<float*>(frames + 0x3C * cur + 0x1C);
                const float curY = *reinterpret_cast<float*>(frames + 0x3C * cur + 0x20);
                const float curZ = *reinterpret_cast<float*>(frames + 0x3C * cur + 0x24);
                const float curQx = *reinterpret_cast<float*>(frames + 0x3C * cur + 0x28);
                const float curQy = *reinterpret_cast<float*>(frames + 0x3C * cur + 0x2C);
                const float curQz = *reinterpret_cast<float*>(frames + 0x3C * cur + 0x30);
                const float curQw = *reinterpret_cast<float*>(frames + 0x3C * cur + 0x34);
                const float pX = *reinterpret_cast<float*>(frames + 0x3C * prev + 0x1C);
                const float pY = *reinterpret_cast<float*>(frames + 0x3C * prev + 0x20);
                const float pZ = *reinterpret_cast<float*>(frames + 0x3C * prev + 0x24);
                const float pQx = *reinterpret_cast<float*>(frames + 0x3C * prev + 0x28);
                const float pQy = *reinterpret_cast<float*>(frames + 0x3C * prev + 0x2C);
                const float pQz = *reinterpret_cast<float*>(frames + 0x3C * prev + 0x30);
                const float pQw = *reinterpret_cast<float*>(frames + 0x3C * prev + 0x34);
                const float nX = *reinterpret_cast<float*>(frames + 0x3C * next + 0x1C);
                const float nY = *reinterpret_cast<float*>(frames + 0x3C * next + 0x20);
                const float nZ = *reinterpret_cast<float*>(frames + 0x3C * next + 0x24);
                const float nQx = *reinterpret_cast<float*>(frames + 0x3C * next + 0x28);
                const float nQy = *reinterpret_cast<float*>(frames + 0x3C * next + 0x2C);
                const float nQz = *reinterpret_cast<float*>(frames + 0x3C * next + 0x30);
                const float nQw = *reinterpret_cast<float*>(frames + 0x3C * next + 0x34);
                // 0x4884AB: equality per component (NaN -> false), all 14
                // flags ANDed (0x488660..0x4886a4).
                const bool all = (curX == pX) && (curY == pY) && (curZ == pZ) &&
                                 (curQx == pQx) && (curQy == pQy) &&
                                 (curQz == pQz) && (curQw == pQw) &&
                                 (curX == nX) && (curY == nY) && (curZ == nZ) &&
                                 (curQx == nQx) && (curQy == nQy) &&
                                 (curQz == nQz) && (curQw == nQw);
                if (all) {
                    ++deleted;
                    frames[0x3C * cur + 0x38] = 1;
                }
                // advance the chain
                model = ActiveModel(app);
                frames = reinterpret_cast<unsigned char*>(
                    mikudancestudio::mdl::BoneKeys(model));
                cur = *reinterpret_cast<std::int32_t*>(frames + 0x3C * cur + 8);
                next = *reinterpret_cast<std::int32_t*>(frames + 0x3C * cur + 8);
                if (next == 0) {
                    break;
                }
            }
        }

        // ---- morph frames --------------------------------------------
        model = ActiveModel(app);
        const std::int32_t morphCount =
            static_cast<std::int32_t>(mdl::Mdl(model)->morphCount);
        for (std::uint16_t m = 0;
             static_cast<std::int32_t>(m) < morphCount; ++m) {
            model = ActiveModel(app);
            unsigned char* frames = reinterpret_cast<unsigned char*>(
                mdl::MorphKeys(model));
            std::int32_t cur = *reinterpret_cast<std::int32_t*>(
                frames + 0x14 * m + 8);
            if (cur == 0) {
                continue;
            }
            std::int32_t next = *reinterpret_cast<std::int32_t*>(
                frames + 0x14 * cur + 8);
            if (next == 0) {
                continue;
            }
            for (;;) {
                const std::int32_t prev = *reinterpret_cast<std::int32_t*>(
                    frames + 0x14 * cur + 4);
                // Same equality idiom as the bone walk (0x48862F area).
                const bool hit =
                    (*reinterpret_cast<float*>(frames + 0x14 * cur + 0xC) ==
                     *reinterpret_cast<float*>(frames + 0x14 * prev + 0xC)) &&
                    (*reinterpret_cast<float*>(frames + 0x14 * cur + 0xC) ==
                     *reinterpret_cast<float*>(frames + 0x14 * next + 0xC));
                if (hit) {
                    ++deleted;
                    frames[0x14 * cur + 0x10] = 1;
                }
                model = ActiveModel(app);
                frames = reinterpret_cast<unsigned char*>(
                    mdl::MorphKeys(model));
                cur = *reinterpret_cast<std::int32_t*>(frames + 0x14 * cur + 8);
                next = *reinterpret_cast<std::int32_t*>(frames + 0x14 * cur + 8);
                if (next == 0) {
                    break;
                }
            }
        }

        // ---- accessory / camera / light frames -----------------------
        // These are the model DisplayKey records (x86 0x1C-stride):
        // next/previous chain, visible byte, ikStates name list and
        // selectorStates camera records - accessed through the typed
        // record so the x64 layout (40-byte stride) stays correct.
        model = ActiveModel(app);
        mdl::DisplayKey* frames = mdl::DisplayKeys(model);
        std::int32_t cur = static_cast<std::int32_t>(frames->next);
        if (cur != 0) {
            std::int32_t next = static_cast<std::int32_t>(frames[cur].next);
            while (next != 0) {
                const std::int32_t prev =
                    static_cast<std::int32_t>(frames[cur].previous);
                const std::uint8_t typeByte = frames[cur].visible;
                const mdl::DisplayKey& fPrev = frames[prev];
                const mdl::DisplayKey& fNext = frames[next];
                if (fPrev.visible == typeByte && fNext.visible == typeByte) {
                    // IK name lists of the neighbours must match byte-exact
                    std::uint8_t nameOk = 1;
                    const std::int32_t ikCount = static_cast<std::int32_t>(
                        mdl::Mdl(model)->ikChainCount);
                    if (ikCount > 0) {
                        unsigned char* m2 = ActiveModel(app);  // re-derived
                        mdl::DisplayKey* f2 = mdl::DisplayKeys(m2);
                        const std::int32_t p1 =
                            static_cast<std::int32_t>(f2[cur].next);
                        const char* x1 = reinterpret_cast<const char*>(
                            mdl::IkStates(f2[p1]));
                        const std::int32_t p2 =
                            static_cast<std::int32_t>(f2[cur].previous);
                        const char* x2 = reinterpret_cast<const char*>(
                            mdl::IkStates(f2[p2]));
                        const char* y = reinterpret_cast<const char*>(
                            mdl::IkStates(f2[cur]));
                        for (std::int32_t k = 0; k < ikCount; ++k) {
                            if (x2[k] != y[k] || x1[k] != y[k]) {
                                nameOk = 0;
                                break;
                            }
                        }
                    }
                    if (nameOk != 0) {
                        // 8-byte camera records must match
                        const std::int32_t camCount = static_cast<std::int32_t>(
                            mdl::Mdl(model)->boneOrderCount);
                        const char* A = reinterpret_cast<const char*>(
                            mdl::SelectorStates(frames[cur]));
                        const char* E = reinterpret_cast<const char*>(
                            mdl::SelectorStates(fPrev));
                        const char* B = reinterpret_cast<const char*>(
                            mdl::SelectorStates(fNext));
                        bool camOk = true;
                        for (std::int32_t j = 0; j < camCount; ++j) {
                            const char* pA = A + 8 * j;
                            if (*reinterpret_cast<const std::int32_t*>(B + 8 * j) !=
                                    *reinterpret_cast<const std::int32_t*>(pA) ||
                                *reinterpret_cast<const std::int32_t*>(B + 8 * j + 4) !=
                                    *reinterpret_cast<const std::int32_t*>(pA + 4) ||
                                *reinterpret_cast<const std::int32_t*>(E + 8 * j + 4) !=
                                    *reinterpret_cast<const std::int32_t*>(pA + 4) ||
                                *reinterpret_cast<const std::int32_t*>(E + 8 * j) !=
                                    *reinterpret_cast<const std::int32_t*>(pA)) {
                                camOk = false;
                                break;
                            }
                        }
                        if (camOk) {
                            ++deleted;
                            frames[cur].allocated = 1;
                        }
                    }
                }
                // advance the chain
                model = ActiveModel(app);
                frames = mdl::DisplayKeys(model);
                cur = static_cast<std::int32_t>(frames[cur].next);
                next = static_cast<std::int32_t>(frames[cur].next);
            }
        }

        // ---- purge marked frames and report --------------------------
        DeleteMarkedKeyframes(app);                              // 0x4316B0
        app->state.enterKeyState = 1;
        char text[0x100];
        if (app->EnglishUI() != 0) {
            sprintf_s(text, 0x100, "%d point was deleted.", deleted);
            MessageBoxA(MainHwnd(app), text, "delete unused frame", MB_TOPMOST);
        } else {
            sprintf_s(text, 0x100, kFmtPointsDeletedJp, deleted);
            MessageBoxA(MainHwnd(app), text, kCaptionDelUnusedJp, MB_TOPMOST);
        }
        app->SceneModified() = 1;
        break;
    }

    // ------------------------------------------------------------------
    // 224 (0x00488AE8): load VSQ.  SetCurrentDirectoryW(exe dir), empty
    // file buffer (kEmptyPathFormat), OPENFILENAMEW: filter "vsq files(*.vsq)",
    // initial dir "UserFile\Vsq", defext "vsq", title "load vsq data"
    // (EN) / JP, Flags 0x1000; on OK LoadVsqFile(app, path) (VSQ load) and
    // dirty.  Owner = app+0xA0D38 when set, else the main window.
    // ------------------------------------------------------------------
    case 224: {
        SetCurrentDirectoryW(app->ExeDir());
        app->state.enterKeyState = 1;
        wchar_t path[0x100];
        swprintf_s(path, 0x100, kEmptyPathFormat, L"", L"");
        OPENFILENAMEW ofn;
        memset(&ofn, 0, sizeof(ofn));
        ofn.lStructSize = sizeof(ofn);
        ofn.hwndOwner =
            app->state.floatingWindow != 0
                ? reinterpret_cast<HWND>(app->state.floatingWindow)
                : MainHwnd(app);
        ofn.lpstrFilter = L"vsq files(*.vsq)\0*.vsq\0";
        ofn.lpstrFile = path;
        ofn.nMaxFile = 0x100;
        ofn.Flags = OFN_FILEMUSTEXIST;
        ofn.lpstrInitialDir = L"UserFile\\Vsq";
        ofn.lpstrDefExt = L"vsq";
        wchar_t fileTitle[0x100];
        ofn.nMaxFileTitle = 0x100;
        ofn.lpstrFileTitle = fileTitle;
        ofn.lpstrTitle = app->EnglishUI() != 0
                             ? L"load vsq data"          // 0x52FBF0
                             : L"vsq\x30C7\x30FC\x30BF\x8AAD\x8FBC";  // 0x52FBDC
        if (GetOpenFileNameW(&ofn)) {
            LoadVsqFile(app, path);                     // 0x435FE0 VSQ load
            app->SceneModified() = 1;
        }
        break;
    }

    // ------------------------------------------------------------------
    // 223 (0x00489C62): output AVI.  GetSaveFileNameW ("AVI files(*.avi)",
    // defext "avi", Flags 6, initial dir "UserFile" or DirUser when the
    // 0x12D menu gate is checked); dir-copy into DirUser + path-copy into
    // app+0x9EB90, then the AVI-out options dialog (sub_40F2F0, tpl 0x28E EN /
    // 0x260 JP) and 0x464760 / 0x45E820 depending on app+0xA0D61.
    // ------------------------------------------------------------------
    case 223: {
        app->state.dialogFlags[3] = 1;
        app->state.enterKeyState = 1;
        SetCurrentDirectoryW(app->ExeDir());
        wchar_t path[0x100];
        swprintf_s(path, 0x100, kEmptyPathFormat, L"", L"");
        OPENFILENAMEW ofn;
        memset(&ofn, 0, sizeof(ofn));
        ofn.lStructSize = sizeof(ofn);
        ofn.hwndOwner = MainHwnd(app);
        ofn.lpstrFilter = L"AVI files(*.avi)\0*.avi\0";   // 0x52F720
        ofn.lpstrFile = path;
        if ((GetMenuState(GetMenu(MainHwnd(app)), 0x12D, 0) & 8) != 0) {
            ofn.lpstrInitialDir = app->DirUser();          // 0xA1540
        } else {
            ofn.lpstrInitialDir = L"UserFile";             // 0x529780
        }
        ofn.nFilterIndex = 1;
        ofn.nMaxFile = 0x100;
        ofn.Flags = (OFN_OVERWRITEPROMPT | OFN_HIDEREADONLY);
        ofn.lpstrDefExt = L"avi";
        wchar_t fileTitle[0x100];
        ofn.nMaxFileTitle = 0x100;
        ofn.lpstrFileTitle = fileTitle;
        ofn.lpstrTitle = app->EnglishUI() != 0
                             ? L"output AVI file"   // 0x52F700
                             : L"AVI\x51FA\x529B";  // 0x52F6F4 "AVI出力"
        if (!GetSaveFileNameW(&ofn)) {
            break;
        }
        if ((GetMenuState(GetMenu(MainHwnd(app)), 0x12D, 0) & 8) != 0) {
            wchar_t* dir = ExtractDirFromPath(
                app->PathWorkspace().projectDirectory, path);
            CopyDirPathW(app->DirUser(), dir);   // 0x42AE20
        }
        CopyPathW(app->AviOutputPath(), path);  // 0x42AE40
        app->state.enterKeyState = 1;
        const std::intptr_t r = DialogBoxParamA(
            static_cast<HINSTANCE>(app->HInstance()),
            MAKEINTRESOURCEA(app->EnglishUI() != 0 ? 0x28E : 0x260),
            MainHwnd(app), AviOutDlgProc, 0);
        if (r == 2) {  // IDCANCEL
            break;
        }
        if (app->AviStereoOutput() != 0) {
            StartAviRecordFullscreen(app);  // 0x464760
        } else {
            StartAviRecordWindow(app);  // 0x45E820
        }
        break;
    }

    // ------------------------------------------------------------------
    // 225 (0x00488DDE): morph-frame cleanup.  DialogBoxParamA (sub_40F0B0,
    // tpl 0x28F EN / 0x267 JP) lets the user type a frame shift
    // (app+0xA08F4).  On OK every type-3 (lip) morph's frame chain is
    // walked: the first frame of the chain is the morph's own record, then
    // each successor; a frame is dropped (relinked out of the chain and its
    // record zeroed) while (frame0 + shift) <= 0, and the walk stops once
    // the shifted value turns positive; the chain tail ends the walk.
    // ------------------------------------------------------------------
    case 225: {
        if (app->state.optflag[0] != 0) {  // 0x2F8
            break;
        }
        app->state.enterKeyState = 1;
        const std::intptr_t r = DialogBoxParamA(
            static_cast<HINSTANCE>(app->HInstance()),
            MAKEINTRESOURCEA(app->EnglishUI() != 0 ? 0x28F : 0x267),
            MainHwnd(app), FrameShiftDlgProc, 0);
        if (r == 2) {  // IDCANCEL
            break;
        }
        unsigned char* model = ActiveModel(app);
        const std::int32_t morphCount =
            static_cast<std::int32_t>(mdl::Mdl(model)->morphCount);
        for (std::uint16_t m = 0; static_cast<std::int32_t>(m) < morphCount;
             ++m) {
            model = ActiveModel(app);
            const mdl::MorphRecord* const morphs = mdl::Morphs(model);
            if (morphs[m].type != 3) {
                continue;
            }
            unsigned char* frames = reinterpret_cast<unsigned char*>(
                mdl::MorphKeys(model));
            std::int32_t cur = *reinterpret_cast<std::int32_t*>(
                frames + 0x14 * m + 8);  // chain head
            if (cur == 0) {
                continue;
            }
            for (;;) {
                model = ActiveModel(app);
                frames = reinterpret_cast<unsigned char*>(
                    mdl::MorphKeys(model));
                const std::int32_t next = *reinterpret_cast<std::int32_t*>(
                    frames + 0x14 * cur + 8);
                if (next != 0) {
                    if (cur == m) {  // self-loop edge (original 0x488F61)
                        cur = next;
                        continue;
                    }
                    std::int32_t v = *reinterpret_cast<std::int32_t*>(
                                         frames + 0x14 * cur + 0) +
                                     app->MorphFrameShift();
                    *reinterpret_cast<std::int32_t*>(frames + 0x14 * cur + 0) =
                        v;
                    if (v > 0) {
                        cur = *reinterpret_cast<std::int32_t*>(
                            frames + 0x14 * cur + 8);
                        continue;  // stop dropping, keep walking
                    }
                    // drop cur: relink m's chain past it, zero its record
                    const std::int32_t oldNext =
                        *reinterpret_cast<std::int32_t*>(frames + 0x14 * cur + 8);
                    *reinterpret_cast<std::int32_t*>(frames + 0x14 * m + 8) =
                        oldNext;
                    *reinterpret_cast<std::int32_t*>(frames + 0x14 * cur + 0) = 0;
                    *reinterpret_cast<std::int32_t*>(frames + 0x14 * cur + 8) = 0;
                    *reinterpret_cast<std::int32_t*>(frames + 0x14 * cur + 4) = 0;
                    frames[0x14 * cur + 0x10] = 0;
                    *reinterpret_cast<float*>(frames + 0x14 * cur + 0xC) = 0.0f;
                    cur = oldNext;
                    continue;
                }
                // chain tail reached
                if (cur == m) {
                    break;
                }
                std::int32_t v = *reinterpret_cast<std::int32_t*>(
                                     frames + 0x14 * cur + 0) +
                                 app->MorphFrameShift();
                *reinterpret_cast<std::int32_t*>(frames + 0x14 * cur + 0) = v;
                if (v > 0) {
                    break;
                }
                *reinterpret_cast<std::int32_t*>(frames + 0x14 * m + 8) =
                    *reinterpret_cast<std::int32_t*>(frames + 0x14 * cur + 8);
                *reinterpret_cast<std::int32_t*>(frames + 0x14 * cur + 0) = 0;
                *reinterpret_cast<std::int32_t*>(frames + 0x14 * cur + 8) = 0;
                *reinterpret_cast<std::int32_t*>(frames + 0x14 * cur + 4) = 0;
                frames[0x14 * cur + 0x10] = 0;
                *reinterpret_cast<float*>(frames + 0x14 * cur + 0xC) = 0.0f;
                break;
            }
        }
        FramePurgeTail(app);
        break;
    }

    // ------------------------------------------------------------------
    // 226 (0x00488BD4): delete all lip frames.  Confirmation MessageBox
    // (EN "Trying to delete all lip frame...", JP) with MB_OKCANCEL +
    // MB_TOPMOST, then PurgeMorphFrames(app, 3) and the shared purge tail.
    // ------------------------------------------------------------------
    case 226: {
        if (app->state.optflag[0] != 0) {  // 0x2F8
            break;
        }
        app->state.enterKeyState = 1;
        const int r = MessageBoxA(
            MainHwnd(app),
            app->EnglishUI() != 0
                ? "Trying to delete all lip frame.\n"
                  "You cannot undo this oparation.\n\nAre you OK?"
                : kMsgDelLipJp,
            app->EnglishUI() != 0 ? "delete lip frame" : kCaptionDelLipJp,
            (MB_OKCANCEL | MB_TOPMOST));
        if (r != IDOK) {
            break;
        }
        PurgeMorphFrames(app, 3);
        FramePurgeTail(app);
        break;
    }

    // ------------------------------------------------------------------
    // 227 (0x0048944B): randomly register blinking.  Searches the morph
    // table for the 9-byte "まばたき" name; without it an error MessageBox
    // is shown.  Otherwise the blink dialog (sub_40F1E0, tpl 0x290 EN /
    // 0x269 JP) supplies the frame range app+0xA08F8..0xA08FC; for each
    // random step q = rand()*230/32768 a blink sequence of 4 keyframes
    // (value 0/1/1/0 at frame, +2, +3, +6) is registered on the blink
    // morph via RegisterMorphKeyCurrent, using the app+0x9EB7F blink-phase byte.
    // ------------------------------------------------------------------
    case 227: {
        if (app->state.optflag[0] != 0) {  // 0x2F8
            break;
        }
        unsigned char* model = ActiveModel(app);
        const std::int32_t morphCount =
            static_cast<std::int32_t>(mdl::Mdl(model)->morphCount);
        std::int32_t blinkMorph = -1;  // ebx = (uint16) search index
        if (morphCount > 0) {
            const mdl::MorphRecord* const morphs = mdl::Morphs(model);
            for (std::uint16_t m = 0;
                 static_cast<std::int32_t>(m) < morphCount; ++m) {
                if (memcmp(morphs[m].name, kNameMabataki, 9) == 0) {
                    blinkMorph = m;
                    break;
                }
            }
        }
        if (blinkMorph < 0) {
            app->state.enterKeyState = 1;
            MessageBoxA(
                MainHwnd(app),
                app->EnglishUI() != 0
                    ? "You cannot register blinking because this model has "
                      "not facial motion of 'blink'."
                    : kMsgNoBlinkMorphJp,
                app->EnglishUI() != 0 ? "randomly register blinking"
                                      : kCaptionRegBlinkJp,
                MB_TOPMOST);
            break;
        }
        app->state.enterKeyState = 1;
        const std::intptr_t r = DialogBoxParamA(
            static_cast<HINSTANCE>(app->HInstance()),
            MAKEINTRESOURCEA(app->EnglishUI() != 0 ? 0x290 : 0x269),
            MainHwnd(app), BlinkRegisterDlgProc, 0);
        if (r == 2) {  // IDCANCEL
            break;
        }
        std::int32_t frame = app->BlinkStartFrame();
        std::int32_t count = 0;
        while (frame < app->BlinkEndFrame()) {
            const std::int32_t q = (rand() * 230) / 32768;  // 0xE6 / 2^15
            const std::int8_t phase = static_cast<std::int8_t>(app->state.blinkPhase);
            bool registerKeys = false;
            if (q < 90 && phase <= 0) {
                frame += 7;
                app->state.blinkPhase = 3;
                registerKeys = true;
            } else if (q > 50) {
                frame += q;
                app->state.blinkPhase =
                    static_cast<std::uint8_t>(phase - 1);
                registerKeys = true;
            }
            if (registerKeys) {
                model = ActiveModel(app);
                mdl::Morphs(model)[blinkMorph].value = 0.0f;
                RegisterMorphKeyCurrent(model, blinkMorph, frame);
                model = ActiveModel(app);
                mdl::Morphs(model)[blinkMorph].value = 1.0f;
                RegisterMorphKeyCurrent(model, blinkMorph, frame + 2);
                model = ActiveModel(app);
                mdl::Morphs(model)[blinkMorph].value = 1.0f;
                RegisterMorphKeyCurrent(model, blinkMorph, frame + 3);
                model = ActiveModel(app);
                mdl::Morphs(model)[blinkMorph].value = 0.0f;
                RegisterMorphKeyCurrent(model, blinkMorph, frame + 6);
                ++count;
            }
        }
        model = ActiveModel(app);
        const std::int32_t frameCount =
            static_cast<std::int32_t>(mdl::Mdl(model)->maxFrame);
        if (app->state.lastRegisteredFrame < frameCount) {
            app->state.lastRegisteredFrame = frameCount;
        }
        app->state.enterKeyState = 1;
        char text[0x100];
        if (app->EnglishUI() != 0) {
            sprintf_s(text, 0x100, "%d blinking is registerd", count);
            MessageBoxA(MainHwnd(app), text, "register blinking", MB_TOPMOST);
        } else {
            sprintf_s(text, 0x100, kFmtBlinksAddedJp, count);
            MessageBoxA(MainHwnd(app), text, kCaptionBlinkCntJp, MB_TOPMOST);
        }
        app->SceneModified() = 1;
        break;
    }

    // ------------------------------------------------------------------
    // 228 (0x004890A7): delete all eyes frames - PurgeMorphFrames(app, 2).
    // ------------------------------------------------------------------
    case 228: {
        if (app->state.optflag[0] != 0) {  // 0x2F8
            break;
        }
        app->state.enterKeyState = 1;
        const int r = MessageBoxA(
            MainHwnd(app),
            app->EnglishUI() != 0
                ? "Trying to delete all eyes frame.\n"
                  "You cannot undo this oparation.\n\nAre you OK?"
                : kMsgDelEyesJp,
            app->EnglishUI() != 0 ? "delete lip frame" : kCaptionNewJp,
            (MB_OKCANCEL | MB_TOPMOST));
        if (r != IDOK) {
            break;
        }
        PurgeMorphFrames(app, 2);
        FramePurgeTail(app);
        break;
    }

    // ------------------------------------------------------------------
    // 250 (0x00485387): paste to difference flame (0xFA).  Confirmation
    // MessageBox (accessary or bone variant by app+0x2F8), then either
    // the accessary paste (clear the four frame-table selections + the
    // 255 accessory tables, replay app+0x9DA44 records of app+0x370
    // (0x34 stride) through 0x414110, refresh chain) or the bone paste
    // (paste counter model+0x31B4 wrap at 0x1E, 28-byte-stride undo
    // records at model+0x26EC/0x26F8/0x26FC/0x2700, per-bone 0x24 records
    // with pos/quat/frame-flag snapshots, model+0x3904 clear, ResetBoneKeyCursor,
    // replay of app+0x9DA28 records of app+0x354 (0x54 stride) through
    // RegisterBoneKey, undo-available flag).
    // ------------------------------------------------------------------
    case 250: {
        app->state.dialogFlags[15] = 1;
        const std::uint8_t opt = app->state.optflag[0];
        const bool isAcc = opt != 0;
        const auto& clipboardCounts = app->ClipboardCounts();
        if (isAcc ? clipboardCounts.accessories == 0
                  : clipboardCounts.bones == 0) {
            break;
        }
        const char* msg = app->EnglishUI() != 0
                              ? (isAcc
                                     ? "Trying to paste accessary flame data "
                                       "to difference accessary\nwhich is "
                                       "selected for manipulate.\n\nAre you ok?"
                                     : "Trying to paste flame data to "
                                       "difference bone\nwhich is selected "
                                       "for manipulate.\n\nAre you ok?")
                              : (isAcc ? kMsgPasteAccJp : kMsgPasteBoneJp);
        const char* cap = app->EnglishUI() != 0 ? "paste to difference flame"
                                                : kCaptionPasteJp;
        const std::uint32_t flags =
            app->state.floatingWindow != 0 ? (MB_OKCANCEL | MB_TOPMOST) : MB_OKCANCEL;
        if (MessageBoxA(MainHwnd(app), msg, cap, flags) != 1) {
            break;
        }
        if (!isAcc) {
            // ---------------- bone paste (0x485677) ----------------
            unsigned char* model = ActiveModel(app);
            if (mdl::Mdl(model)->selectedBone == -1) {
                MessageBoxA(
                    MainHwnd(app),
                    app->EnglishUI() != 0 ? "Please select bone."
                                          : kMsgSelectBoneJp,
                    cap,
                    app->state.floatingWindow != 0
                        ? MB_TOPMOST
                        : MB_OK);
                break;
            }
            app->SceneModified() = 1;
            {
                model = ActiveModel(app);
                mdl::BoneKey* f = mdl::BoneKeys(model);
                for (std::size_t i = 0; i < mdl::kBoneKeyCapacity; ++i)
                    f[i].allocated = 0;
            }
            {
                model = ActiveModel(app);
                mdl::MorphKey* f = mdl::MorphKeys(model);
                for (std::size_t i = 0; i < mdl::kMorphKeyCapacity; ++i)
                    f[i].allocated = 0;
            }
            {
                model = ActiveModel(app);
                mdl::DisplayKey* f = mdl::DisplayKeys(model);
                for (std::size_t i = 0; i < mdl::kDisplayKeyCapacity; ++i)
                    f[i].allocated = 0;
            }
            EnableWindow(GetDlgItem(MainHwnd(app), panel::kUndoButton), TRUE);
            EnableWindow(GetDlgItem(MainHwnd(app), panel::kRedoButton), FALSE);
            model = ActiveModel(app);
            mdl::Mdl(model)->undoDirty = 1;
            mdl::Mdl(model)->redoDirty = 0;
            std::int32_t idx =
                static_cast<std::int32_t>(mdl::Mdl(model)->undoState[0]);
            ++idx;
            if (idx >= 0x1E) {
                idx = 0;
            }
            mdl::Mdl(model)->undoState[0] = static_cast<std::uint32_t>(idx);
            mdl::Mdl(model)->undoState[1] = static_cast<std::uint32_t>(idx);
            mdl::UndoRecord& undo =
                mdl::Mdl(model)->undoRings[0].slots[idx];
            undo.operation = 2;
            undo.frame = app->state.currentFrame;
            if (undo.bonePose != nullptr) {
                ::operator delete(undo.bonePose);
                undo.bonePose = nullptr;
            }
            const std::int32_t boneCount =
                static_cast<std::int32_t>(mdl::Mdl(model)->boneCount);
            auto* blob = static_cast<mdl::BonePoseSnapshot*>(
                ::operator new(MulOrMax(static_cast<std::uint32_t>(boneCount),
                                        sizeof(mdl::BonePoseSnapshot))));
            if (blob != nullptr) {
                ConstructArrayElements(blob, sizeof(mdl::BonePoseSnapshot),
                                       static_cast<std::uint32_t>(boneCount),
                                       &IdentityCtor);
            }
            undo.bonePose = blob;
            memset(blob, 0, static_cast<std::size_t>(boneCount) *
                                sizeof(mdl::BonePoseSnapshot));
            model = ActiveModel(app);
            const mdl::ModelRecord* const record = mdl::Mdl(model);
            const unsigned char* const physicsState = record->bonePhysicsState;
            const mdl::BoneRecord* const bones = mdl::Bones(model);
            for (std::int32_t i = 0; i < boneCount; ++i) {
                mdl::BonePoseSnapshot& rec = blob[i];
                rec.boneIndex = i;
                memcpy(rec.position, bones[i].trans, sizeof rec.position);
                memcpy(rec.rotation, bones[i].rotQuat, sizeof rec.rotation);
                rec.physicsDisabled = physicsState[i];
            }
            model = ActiveModel(app);
            undo.dirty = 0;
            if (undo.auxiliaryPose != nullptr) {
                ::operator delete(undo.auxiliaryPose);
                undo.auxiliaryPose = nullptr;
            }
            const std::int32_t recCount = static_cast<std::int32_t>(
                app->ClipboardCounts().bones);
            unsigned char* blob2 = static_cast<unsigned char*>(
                ::operator new(MulOrMax(static_cast<std::uint32_t>(3 * recCount),
                                        0x40u)));
            if (blob2 != nullptr) {
                ConstructArrayElements(blob2, 0x40,
                                       static_cast<std::uint32_t>(3 * recCount),
                                       &IdentityCtor);
            }
            model = ActiveModel(app);
            undo.auxiliaryPose = blob2;
            memset(blob2, 0, static_cast<std::size_t>(3 * recCount) * 0x40u);
            memset(mdl::Mdl(model)->keyVisitMap, 0,
                    sizeof(mdl::Mdl(model)->keyVisitMap));
            ResetBoneKeyCursor(model);
            if (recCount > 0) {
                for (std::int32_t j = 0; j < recCount; ++j) {
                    const mdl::BoneClipboardRecord* src =
                        app->BoneClipboard() + j;
                    unsigned char stackRec[0x54];
                    memcpy(stackRec, src, 0x54);
                    model = ActiveModel(app);
                    if (!RegisterBoneKey(model, stackRec,
                                   app->state.currentFrame,
                                   1)) {
                        break;
                    }
                }
            }
            PanelPaint(app);
            SelectionReeval(app);
            model = ActiveModel(app);
            SeekModelFrame(model, app->state.currentFrame,
                      app->PlaybackPhysicsMode());
            app->PhysicsResetPending() = 1;
            break;
        }
        // ---------------- accessary paste (0x4854B5) ----------------
        const std::uint8_t slotIdx = app->SelectedObjectSlot();
        if (app->AccessorySlot(slotIdx) == nullptr) {
            MessageBoxA(
                MainHwnd(app),
                app->EnglishUI() != 0 ? "Please select accessary."
                                      : kMsgSelectAccJp,
                cap,
                app->state.floatingWindow != 0 ? MB_TOPMOST
                                                                  : MB_OK);
            break;
        }
        app->SceneModified() = 1;
        // clear the four frame-table selections (all 10000 records each)
        for (std::size_t key = 0; key < 10000; ++key) {
            app->CameraKeys()[key].selected = 0;
            app->LightKeys()[key].selected = 0;
            app->ShadowKeys()[key].selected = 0;
            app->GravityKeys()[key].selected = 0;
        }
        for (int tbl = 0; tbl < 0xFF; ++tbl) {
            unsigned char* t = reinterpret_cast<unsigned char*>(
                app->AccessoryKeys(tbl));
            for (std::size_t off = 0; off < 0x927C0u; off += 0x3C) {
                t[off + 0x18] = 0;
            }
        }
        const std::int32_t accCount = static_cast<std::int32_t>(
            app->ClipboardCounts().accessories);
        for (std::int32_t i = 0; i < accCount; ++i) {
            const mdl::AccessoryClipboardKey* src =
                app->AccessoryClipboard() + i;
            unsigned char stackRec[0x34];
            memcpy(stackRec, src, 0x34);
            if (!PasteAccessoryKeyRecord(app, stackRec, 1)) {
                break;
            }
        }
        PanelPaint(app);
        SelectionReeval(app);
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
        break;
    }

    // ------------------------------------------------------------------
    // 229 (0x0048B4DF): clear all frames (0xE5).  Gate app+0x2F8; dirty;
    // the used flags of every bone/morph/accessory frame record are
    // cleared (bone +0x38 0x3C-stride over kBoneKeyCapacity x 0x3C
    // bytes, morph +0x10 0x14-stride
    // 0x61A80, acc +0x14 0x1C-stride 0x6D60); then, while the morph count
    // (model+0x2D80) is positive, each morph gets a frame-0 keyframe at
    // the current frame (RegisterMorphKeyCurrent, model re-derived per step, 16-bit
    // counter) and the app+0x9E16C frame-count watermark is bumped to
    // model+0x31B0; finally the undo record (SeekModelFrame), PanelPaint,
    // SelectionReeval and dirty again.  (0x48B5A0..0x48B61F; the range
    // beyond belongs to the out-of-scope 282..287 cases.)
    // ------------------------------------------------------------------
    case 229: {
        if (app->state.optflag[0] != 0) {  // 0x2F8
            break;
        }
        app->SceneModified() = 1;
        {
            unsigned char* m = ActiveModel(app);
            mdl::BoneKey* f = mdl::BoneKeys(m);
            for (std::size_t i = 0; i < mdl::kBoneKeyCapacity; ++i)
                f[i].allocated = 0;
        }
        {
            unsigned char* m = ActiveModel(app);
            mdl::MorphKey* f = mdl::MorphKeys(m);
            for (std::size_t i = 0; i < mdl::kMorphKeyCapacity; ++i)
                f[i].allocated = 0;
        }
        {
            unsigned char* m = ActiveModel(app);
            mdl::DisplayKey* f = mdl::DisplayKeys(m);
            for (std::size_t i = 0; i < mdl::kDisplayKeyCapacity; ++i)
                f[i].allocated = 0;
        }
        unsigned char* model = ActiveModel(app);
        const std::int32_t morphCount =
            static_cast<std::int32_t>(mdl::Mdl(model)->morphCount);
        if (morphCount > 0) {
            for (std::uint16_t m = 0; static_cast<std::int32_t>(m) < morphCount;
                 ++m) {
                model = ActiveModel(app);
                RegisterMorphKeyCurrent(model, m, app->state.currentFrame);
                model = ActiveModel(app);
                const std::int32_t frameCount =
                    static_cast<std::int32_t>(mdl::Mdl(model)->maxFrame);
                if (app->state.lastRegisteredFrame < frameCount) {
                    app->state.lastRegisteredFrame = frameCount;
                }
            }
        }
        model = ActiveModel(app);
        SeekModelFrame(model, app->state.currentFrame,
                  app->PlaybackPhysicsMode());
        PanelPaint(app);      // 0x414610
        SelectionReeval(app); // 0x430510
        app->SceneModified() = 1;
        break;
    }

    // ------------------------------------------------------------------
    // 230 (0x0048DACB): reset morph weights (0xE6).  Gate app+0x2F8; when
    // the morph table (model+0x26C4) exists and the morph count
    // (model+0x2D80) is positive, every morph's weight float (+0x30,
    // 0x88 stride) is zeroed (model re-derived per step); then the panel
    // morph controls reset: the four weight edits 0x1FA/0x1FF/0x204/0x209
    // get "0.00000" and the four sliders 0x1F9/0x1FE/0x203/0x208 get
    // position 0 (TBM_SETPOS redraw, lParam 0).  No dirty flag, no
    // repaint - the case ends here (0x48DACB..0x48DBFA).
    // ------------------------------------------------------------------
    case 230: {
        if (app->state.optflag[0] != 0) {  // 0x2F8
            break;
        }
        unsigned char* model = ActiveModel(app);
        const std::int32_t morphCount =
            static_cast<std::int32_t>(mdl::Mdl(model)->morphCount);
        if (mdl::Morphs(model) != nullptr && morphCount > 0) {
            for (std::int32_t i = 0; i < morphCount; ++i) {
                model = ActiveModel(app);
                mdl::Morphs(model)[i].value = 0.0f;
            }
        }
        const HWND main = MainHwnd(app);
        SetWindowTextA(GetDlgItem(main, panel::kMorphEdit0), "0.00000");
        SetWindowTextA(GetDlgItem(main, panel::kMorphEdit1), "0.00000");
        SetWindowTextA(GetDlgItem(main, panel::kMorphEdit2), "0.00000");
        SetWindowTextA(GetDlgItem(main, panel::kMorphEdit3), "0.00000");
        SendMessageA(GetDlgItem(main, panel::kMorphSlider0), TBM_SETPOS, 1, 0);
        SendMessageA(GetDlgItem(main, panel::kMorphSlider1), TBM_SETPOS, 1, 0);
        SendMessageA(GetDlgItem(main, panel::kMorphSlider2), TBM_SETPOS, 1, 0);
        SendMessageA(GetDlgItem(main, panel::kMorphSlider3), TBM_SETPOS, 1, 0);
        break;
    }

    // ------------------------------------------------------------------
    // 231 (0x00489279): delete all eyebrow frames -
    // PurgeMorphFrames(app, 1).
    // ------------------------------------------------------------------
    case 231: {
        if (app->state.optflag[0] != 0) {  // 0x2F8
            break;
        }
        app->state.enterKeyState = 1;
        const int r = MessageBoxA(
            MainHwnd(app),
            app->EnglishUI() != 0
                ? "Trying to delete all eyebrow frame.\n"
                  "You cannot undo this oparation.\n\nAre you OK?"
                : kMsgDelEyebrowJp,
            app->EnglishUI() != 0 ? "delete lip frame" : kCaptionNewJp,
            (MB_OKCANCEL | MB_TOPMOST));
        if (r != IDOK) {
            break;
        }
        PurgeMorphFrames(app, 1);
        FramePurgeTail(app);
        break;
    }

    // ------------------------------------------------------------------
    // 233 (0x004873DB): accessory-bone display toggle.  app+0x9E428 byte
    // + menu checkmark on 0xE9; enabling is gated on app+0x9E42C != 0.
    // ------------------------------------------------------------------
    // ------------------------------------------------------------------
    // 247 (0x00489E10): undo on/off toggle.  app+0x9ED98 byte + 0xF7 menu
    // checkmark + the 0x217 checkbox (BM_SETCHECK); on enable the camera
    // floats app+0x308/0x30C are cleared and the reload chain runs
    // (ReloadModels, RefreshLightPanel, RefreshSelfShadowPanel, ApplyGravityTrack, per-accessory
    // ApplyAccessoryTrack, SyncAccessoryEditPanel, then PostModelReload when app+0x2F8 is clear;
    // the off path additionally gates on app+0xA0430 == slot index),
    // finishing with PostViewRefresh + PostLanguageSweep2.
    // ------------------------------------------------------------------
    case 247: {
        app->state.dialogFlags[2] = 1;
        if (app->state.followCameraEnabled == 0) {
            // ---- enable undo ----
            app->state.followCameraEnabled = 1;
            app->ViewOffsetX() = 0.0f;
            app->ViewOffsetY() = 0.0f;
            CheckMenuItem(GetMenu(MainHwnd(app)), 0xF7, MF_CHECKED);
            SendMessageA(GetDlgItem(MainHwnd(app), panel::kFollowCameraCheckbox), BM_SETCHECK,
                         1, 0);
            ReloadModels(app);                             // 0x42E640
            RefreshLightPanel(app);                                // 0x411070
            RefreshSelfShadowPanel(app);                                // 0x411B90
            ApplyGravityTrack(app);                                // 0x412330
            for (std::int32_t i = 0; i < 0xFF; ++i) {
                if (app->AccessorySlot(i) != nullptr) {
                    ApplyAccessoryTrack(app, i);                     // 0x413120
                }
            }
            SyncAccessoryEditPanel(app);                                // 0x4134E0
            if (app->state.optflag[0] == 0) {  // 0x2F8
                app->CameraAttachmentTransformSuppressed() = 0;
                PostModelReload(app);                      // 0x41A650
            }
            // 0x489F4F/0x489F56: the shared tail runs for BOTH branches.
            PostViewRefresh(app);                          // 0x40D130
            PostLanguageSweep2(app);                       // 0x40D070
        } else {
            // ---- disable undo ----
            app->state.followCameraEnabled = 0;
            CheckMenuItem(GetMenu(MainHwnd(app)), 0xF7, MF_UNCHECKED);
            SendMessageA(GetDlgItem(MainHwnd(app), panel::kFollowCameraCheckbox), BM_SETCHECK,
                         0, 0);
            if (app->state.optflag[0] != 0) {  // 0x2F8
                PostViewRefresh(app);                      // 0x40D130
                PostLanguageSweep2(app);                   // 0x40D070
                break;
            }
            const std::int32_t cam = app->CameraParentModel();
            if (cam != app->SelectedModelSlot() ||
                cam < 0) {
                PostViewRefresh(app);
                PostLanguageSweep2(app);
                break;
            }
            app->CameraAttachmentTransformSuppressed() = 0;
            PostModelReload(app);                          // 0x41A650
            PostViewRefresh(app);                          // 0x40D130
            PostLanguageSweep2(app);                       // 0x40D070
        }
        break;
    }

    // ------------------------------------------------------------------
    // 232 (0x00487266): load background picture.  GetOpenFileNameW with
    // the multi-part picture filter (EN "All Picture files" / JP "全ての
    // 対応フォーマット", 8 extensions, defext "bmp"), initial dir
    // "UserFile\BackGround" or DirBg when the 0x12D menu gate is checked;
    // dir-copy into DirBg + path-copy into app+0x9E448, then
    // LoadBackgroundPicture and the dirty flag.
    // ------------------------------------------------------------------
    case 232: {
        app->state.enterKeyState = 1;
        SetCurrentDirectoryW(app->ExeDir());
        wchar_t path[0x100];
        swprintf_s(path, 0x100, kEmptyPathFormat, L"", L"");
        OPENFILENAMEW ofn;
        memset(&ofn, 0, sizeof(ofn));
        ofn.lStructSize = sizeof(ofn);
        ofn.hwndOwner =
            app->state.floatingWindow != 0
                ? reinterpret_cast<HWND>(app->state.floatingWindow)
                : MainHwnd(app);
        if (app->EnglishUI() != 0) {
            ofn.lpstrFilter =
                L"All Picture files\0*.bmp;*.jpg;*.png;*.tga;*.dds;*.dib;"
                L"*.pfm;*.hdr\0"
                L"Bmp files(*.bmp)\0*.bmp\0"
                L"Jpeg files(*.jpg)\0*.jpg\0"
                L"Png files(*.png)\0*.png\0"
                L"Tga files(*.tga)\0*.tga\0"
                L"Dds files(*.dds)\0*.dds\0"
                L"DIB files(*.dib)\0*.dib\0"
                L"pfm files(*.pfm)\0*.pfm\0"
                L"HDR files(*.hdr)\0*.hdr\0\0";
        } else {
            ofn.lpstrFilter =
                L"\x5168\x3066\x306E\x5BFE\x5FDC\x30D5\x30A9\x30FC\x30DE"
                L"\x30C3\x30C8\0*.bmp;*.jpg;*.png;*.tga;*.dds;*.dib;*.pfm;"
                L"*.hdr\0"
                L"Bmp files(*.bmp)\0*.bmp\0"
                L"Jpeg files(*.jpg)\0*.jpg\0"
                L"Png files(*.png)\0*.png\0"
                L"Tga files(*.tga)\0*.tga\0"
                L"Dds files(*.dds)\0*.dds\0"
                L"DIB files(*.dib)\0*.dib\0"
                L"pfm files(*.pfm)\0*.pfm\0"
                L"HDR files(*.hdr)\0*.hdr\0\0";
        }
        ofn.lpstrFile = path;
        ofn.nMaxFile = 0x100;
        ofn.Flags = OFN_FILEMUSTEXIST;
        if ((GetMenuState(GetMenu(MainHwnd(app)), 0x12D, 0) & 8) != 0) {
            ofn.lpstrInitialDir = app->DirBg();           // 0xA3C50
        } else {
            ofn.lpstrInitialDir = L"UserFile\\BackGround";
        }
        ofn.lpstrDefExt = L"bmp";
        wchar_t fileTitle[0x100];
        ofn.nMaxFileTitle = 0x100;
        ofn.lpstrFileTitle = fileTitle;
        ofn.lpstrTitle = app->EnglishUI() != 0
                             ? L"load picture data"       // 0x530140
                             : L"\x80CC\x666F\x753B\x50CF"  // 背景画像
                               L"\x30C7\x30FC\x30BF\x8AAD\x8FBC";  // データ読込
        if (!GetOpenFileNameW(&ofn)) {
            break;
        }
        if ((GetMenuState(GetMenu(MainHwnd(app)), 0x12D, 0) & 8) != 0) {
            wchar_t* dir = ExtractDirFromPath(
                app->PathWorkspace().projectDirectory, path);
            CopyDirPathW(app->DirBg(), dir);                 // 0x42AE20
        }
        CopyPathW(app->state.pictureBackgroundPath,
                  path);                                  // 0x42AE40
        LoadBackgroundPicture(app);                       // 0x4337A0 picture
        app->SceneModified() = 1;
        break;
    }

    case 233: {
        app->state.dialogFlags[12] = 1;
        if (app->state.pictureBackgroundEnabled != 0) {
            app->state.pictureBackgroundEnabled = 0;
            CheckMenuItem(GetMenu(MainHwnd(app)), 0xE9, MF_UNCHECKED);
            break;
        }
        if (app->PictureBackgroundTexture() == 0) {
            break;
        }
        app->state.pictureBackgroundEnabled = 1;
        CheckMenuItem(GetMenu(MainHwnd(app)), 0xE9, MF_CHECKED);
        break;
    }

    // ------------------------------------------------------------------
    // 234 / 235 / 236 (0x0048801B / 0x00488071 / 0x004880C7): FPS-cap
    // radios.  Store the cap float into app+0xA08E0 (kFloatFpslimit) and
    // flip the 0xEA / 0xEB / 0xEC menu checks (unchecked 0, checked 8).
    // ------------------------------------------------------------------
    case 234:  // 0xEA: no cap (1000.0 = flt_52FCE8)
        app->state.fpsLimit = 1000.0f;
        CheckMenuItem(GetMenu(MainHwnd(app)), 0xEB, MF_UNCHECKED);
        CheckMenuItem(GetMenu(MainHwnd(app)), 0xEC, MF_UNCHECKED);
        CheckMenuItem(GetMenu(MainHwnd(app)), 0xEA, MF_CHECKED);
        break;

    case 235:  // 0xEB: 30 fps (flt_52997C)
        app->state.fpsLimit = 30.0f;
        CheckMenuItem(GetMenu(MainHwnd(app)), 0xEA, MF_UNCHECKED);
        CheckMenuItem(GetMenu(MainHwnd(app)), 0xEC, MF_UNCHECKED);
        CheckMenuItem(GetMenu(MainHwnd(app)), 0xEB, MF_CHECKED);
        break;

    case 236:  // 0xEC: 60 fps (flt_52A1E0)
        app->state.fpsLimit = 60.0f;
        CheckMenuItem(GetMenu(MainHwnd(app)), 0xEB, MF_UNCHECKED);
        CheckMenuItem(GetMenu(MainHwnd(app)), 0xEA, MF_UNCHECKED);
        CheckMenuItem(GetMenu(MainHwnd(app)), 0xEC, MF_CHECKED);
        break;

    // ------------------------------------------------------------------
    // 242 (0x0048811D): camera-frame multiply dialog.  NOTE the gate is
    // inverted: it only opens when app+0x2F8 (kByteOptflag0) is NON-zero.
    // DialogBoxParamA with proc sub_44D2D0, template 0x291 (EN) / 0x26F
    // (JP); the result is discarded.
    // ------------------------------------------------------------------
    case 242: {
        app->state.dialogFlags[7] = 1;
        if (app->state.optflag[0] == 0) {  // 0x2F8
            break;
        }
        app->state.enterKeyState = 1;
        DialogBoxParamA(
            static_cast<HINSTANCE>(app->HInstance()),
            MAKEINTRESOURCEA(app->EnglishUI() != 0 ? 0x291 : 0x26F),
            MainHwnd(app), CameraFrameMultiplyDlgProc, 0);
        break;
    }

    // ------------------------------------------------------------------
    // 237 / 238 / 239 / 240 / 241 (0x00483339 / 0x0048338D / 0x004833DA /
    // 0x00483427 / 0x00483474): select all frames of one family.  Each
    // family table is walked with its stride and used-flag offset; a frame
    // is selected when its record is used (frame number dword != 0) or it
    // is the very first record of the walk (esi counter == 0 - the "frame
    // 0 always selected" quirk).  Table pointers: app+0x374 (bone, 0x54
    // stride, flag +0x48), app+0x378 (morph, 0x28, +0x24), app+0x37C
    // (camera/light, 0x18, +0x14), app+0x380 (accessory, 0x24, +0x21) and
    // the 255 tables at app+0x384 (0x3C, +0x18).
    // ------------------------------------------------------------------
    case 237: {  // 0xED: bone frames
        app->state.dialogFlags[2] = 1;
        std::int32_t esi = 0;
        auto* frames = app->CameraKeys();
        for (std::size_t key = 0; key < 10000; ++key) {
            frames[key].selected = 0;
            const bool used = frames[key].frame != 0;
            if (used || esi == 0) {
                frames[key].selected = 1;
            }
            ++esi;
        }
        PanelPaint(app);      // 0x414610
        SelectionReeval(app); // 0x430510
        break;
    }

    case 238: {  // 0xEE: morph frames
        app->state.dialogFlags[17] = 1;
        std::int32_t esi = 0;
        auto* frames = app->LightKeys();
        for (std::size_t key = 0; key < 10000; ++key) {
            frames[key].selected = 0;
            const bool used = frames[key].frame != 0;
            if (used || esi == 0) {
                frames[key].selected = 1;
            }
            ++esi;
        }
        PanelPaint(app);
        break;
    }

    case 239: {  // 0xEF: camera/light frames
        app->state.dialogFlags[8] = 1;
        std::int32_t esi = 0;
        auto* frames = app->ShadowKeys();
        for (std::size_t key = 0; key < 10000; ++key) {
            frames[key].selected = 0;
            const bool used = frames[key].frame != 0;
            if (used || esi == 0) {
                frames[key].selected = 1;
            }
            ++esi;
        }
        PanelPaint(app);
        break;
    }

    case 240: {  // 0xF0: accessory frames
        app->state.dialogFlags[3] = 1;
        std::int32_t esi = 0;
        auto* frames = app->GravityKeys();
        for (std::size_t key = 0; key < 10000; ++key) {
            frames[key].selected = 0;
            const bool used = frames[key].frame != 0;
            if (used || esi == 0) {
                frames[key].selected = 1;
            }
            ++esi;
        }
        PanelPaint(app);
        break;
    }

    case 241: {  // 0xF1: all 255 accessory frame tables
        app->state.dialogFlags[5] = 1;
        for (int tbl = 0; tbl < 0xFF; ++tbl) {
            unsigned char* frames = reinterpret_cast<unsigned char*>(
                app->AccessoryKeys(tbl));
            std::int32_t esi = 0;  // iVar18 resets PER TABLE (0x4834E4)
            for (std::size_t off = 0; off < 0x927C0u; off += 0x3C) {
                frames[off + 0x18] = 0;
                const bool used =
                    *reinterpret_cast<std::int32_t*>(frames + off) != 0;
                if (used || esi == 0) {
                    frames[off + 0x18] = 1;
                }
                ++esi;
            }
        }
        PanelPaint(app);
        break;
    }

    // ------------------------------------------------------------------
    // 243 / 244 / 245 / 246 (0x00489760 / 0x004897CE / 0x00489835 /
    // 0x0048989C): panel-mode radios.  app+0x9EB84 stores the mode
    // (0..3), the corresponding 0xF3..0xF6 menu item gets the checkmark
    // (8), the other three are cleared (0).
    // ------------------------------------------------------------------
    case 243: {  // 0xF3
        app->state.dialogFlags[3] = 1;
        app->CaptureMode() = ScreenCaptureMode::Disabled;
        CheckMenuItem(GetMenu(MainHwnd(app)), 0xF3, MF_CHECKED);
        CheckMenuItem(GetMenu(MainHwnd(app)), 0xF4, MF_UNCHECKED);
        CheckMenuItem(GetMenu(MainHwnd(app)), 0xF5, MF_UNCHECKED);
        CheckMenuItem(GetMenu(MainHwnd(app)), 0xF6, MF_UNCHECKED);
        break;
    }

    case 244: {  // 0xF4
        app->CaptureMode() = ScreenCaptureMode::FullFrame;
        CheckMenuItem(GetMenu(MainHwnd(app)), 0xF3, MF_UNCHECKED);
        CheckMenuItem(GetMenu(MainHwnd(app)), 0xF4, MF_CHECKED);
        CheckMenuItem(GetMenu(MainHwnd(app)), 0xF5, MF_UNCHECKED);
        CheckMenuItem(GetMenu(MainHwnd(app)), 0xF6, MF_UNCHECKED);
        break;
    }

    case 245: {  // 0xF5
        app->CaptureMode() = ScreenCaptureMode::CropFourByThree;
        CheckMenuItem(GetMenu(MainHwnd(app)), 0xF3, MF_UNCHECKED);
        CheckMenuItem(GetMenu(MainHwnd(app)), 0xF4, MF_UNCHECKED);
        CheckMenuItem(GetMenu(MainHwnd(app)), 0xF5, MF_CHECKED);
        CheckMenuItem(GetMenu(MainHwnd(app)), 0xF6, MF_UNCHECKED);
        break;
    }

    case 246: {  // 0xF6
        app->state.dialogFlags[6] = 1;
        app->CaptureMode() = ScreenCaptureMode::BackgroundRefresh;
        CheckMenuItem(GetMenu(MainHwnd(app)), 0xF3, MF_UNCHECKED);
        CheckMenuItem(GetMenu(MainHwnd(app)), 0xF4, MF_UNCHECKED);
        CheckMenuItem(GetMenu(MainHwnd(app)), 0xF5, MF_UNCHECKED);
        CheckMenuItem(GetMenu(MainHwnd(app)), 0xF6, MF_CHECKED);
        break;
    }

    // ------------------------------------------------------------------
    // 248 (0x00489F5B): open the modeless dialog (proc sub_42DFF0,
    // template 0x292 EN / 0x270 JP), stored into app+0xA0B14, shown with
    // SW_SHOW and updated.  When it is already open the case falls into
    // the (no-op) default handler.
    // ------------------------------------------------------------------
    case 248: {
        app->state.dialogFlags[0] = 1;
        if (app->GroundShadowColorDialog() != 0) {
            break;  // def_47E903 (no-op for this id)
        }
        HWND dlg = CreateDialogParamA(
            static_cast<HINSTANCE>(app->HInstance()),
            MAKEINTRESOURCEA(app->EnglishUI() != 0 ? 0x292 : 0x270),
            MainHwnd(app), GroundShadowColorDlgProc, 0);
        app->GroundShadowColorDialog() = dlg;
        ShowWindow(dlg, SW_SHOW);
        UpdateWindow(dlg);
        break;
    }

    // ------------------------------------------------------------------
    // 249 (0x0048A03B): modal dialog (proc sub_44CA40, template 0x293 EN /
    // 0x273 JP); a cancel result falls through to the no-op default.
    // ------------------------------------------------------------------
    case 249: {
        app->state.dialogFlags[8] = 1;
        app->state.enterKeyState = 1;
        const std::intptr_t r = DialogBoxParamA(
            static_cast<HINSTANCE>(app->HInstance()),
            MAKEINTRESOURCEA(app->EnglishUI() != 0 ? 0x293 : 0x273),
            MainHwnd(app), AccessorySettingsDlgProc, 0);
        if (r == 2) {  // IDCANCEL
            break;
        }
        break;  // def_47E903 (no-op for this id)
    }

    default:
        // 200..211/213..218/220 handled by command_dispatch.cpp
        break;
    }
}

}  // namespace mikudancestudio
