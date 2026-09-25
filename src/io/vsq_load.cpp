// ===========================================================================
// VA 0x00435FE0..0x00438CC9 - LoadVsqFile  (VSQ load / auto lipsync)
// ===========================================================================
// __thiscall(app, path); ret 4.  Menu "facial expression -> lip-sync with
// .VSQ file" (0xE0) and the .vsq drag-drop path both land here.
//
// Phase 1 - VSQ(MIDI SMF) -> .txt dump  (0x435FE0..0x43647B)
//   _wsopen_s probe (errno -> EN 0x52CDF0 / JP 0x52CDD4 box, caption "").
//   The path buffer is edited in place: wcsstr(path, L".vsq")+1 gets
//   L"txt" -> "<name>.txt".  _wfopen_s(txt, path, L"wt"), then a plain SMF
//   walk with ReadFixedString / ReadBeWord (was
//   0x41A1A0):
//     MThd hdr(4) hdrlen(4) format(2) ntracks(2) division(2);
//     "[number of Track:%d]" via "%s\n", "[UnitTime:%d]" via "%s\n\n".
//   per track: "[Track%d]" via "%s\n", MTrk(4) tracklen(4), event loop with
//   big-endian varlen deltas accumulated into `time`:
//     0x80-0x8F/0x90-0x9F/0xB0-0xBF -> skip 2 data bytes;
//     0xF0/0xF7 -> JP-only SysEx box (0x52D108, caption 0x52D134), close+ret;
//     other    -> EN 0x52D0E0 / JP 0x52D0C8 "vsq analysis error" box, ret;
//     0xFF meta: type 0 -> skip 1 byte; type 1 (text) -> len(1), skip 8,
//       fprintf "%s" of the remaining len-8 bytes; types 2..5 -> len(1) +
//       skip len; 0x2F -> len(1), len==0 -> "[End of Track%d]" via "%s\n\n",
//       next track; 0x51 -> len(1), tempo(3) ->
//       "[ChangeTempo time=%d,tempo=%d]" via "%s\n" (tempoCount++),
//       NOT stored in a table here (re-parsed below); 0x58 -> 4 bytes
//       nn dd cc bb, sprintf only (never written; JP fmt 0x52D178);
//       0x59 -> 2 bytes sf mi, sprintf JP 0x52D158(major)/0x52D13C(minor)
//       via "%s\n"; other meta types -> the analysis-error box.
//   fclose + _close.
//
// Phase 2 - re-read the txt  (0x43647B..0x4366C8)
//   tempoTable = malloc(ntracks*8) in the original (pairs {time,tempo});
//   tempoCount can exceed ntracks, so the port allocates tempoCount entries.
//   _wfopen_s(path, L"r"); sequential fgets scan:
//   tempoCount x "[ChangeTempo time=" lines -> atoi of the text between
//   the first '=' and ',' / the next '=' and ']'.
//   Then find "[EventList]", fgets/fgets, then count the lines that contain
//   "=ID#" (one fgets per match after the initial pair) -> `count`.
//   events = malloc(count*0x24), memset 0.
//
// Phase 3 - fill the 0x24-byte event records  (0x4366CD..0x436D5D)
//   reopen "r", find "[EventList]" again, one fgets:
//   loop A (count x): fgets; tick = atoi(text before first '=');
//     ev+0x10 = (float)tick.
//   loop B (count x): sprintf "[ID#%04d]" (i+1); fgets-scan until a line
//     contains it; scan until "Length=": ev+0x14 = (float)atoi(after '=');
//     scan until "LyricHandle=": sprintf_s(ev+0x18, 10, after '=') (the
//     source string doubles as the format, exactly like the original);
//     ev[0x1E] = 0.
//   loop C (count x): fgets-scan until a line contains the ev handle id;
//     fgets (the L0/L1 lyric line); m = strstr(line, "\",\""); *m = 0;
//     sprintf_s(ev+7, 8, first '"'+1)  -> L0 token; if m[4] == '"'
//     (second field is a single char): ev[0] = 0, ev[6] = m[3];
//     else if m[4] == '\\' (x64 0x7FF7CB48F664): if m[5] == ' ' then
//     m[5] = 0, sprintf_s(ev, 6, m+3), ev[6] = m[6]; else ev[0] = 0,
//     ev[6] = 'N';  else slot = strstr(m+1, " ") (per-iteration scratch,
//     NOT loop-carried); NULL -> ev[0]=0, ev[6]='N';  else *slot=0,
//     sprintf_s(ev, 6, m+3), ev[6] = slot[1].
//     Finally 'M' -> 'u' on ev[6].
//
// Phase 4 - tick->seconds + phoneme class  (0x436D70..0x437BBA)
//   x = (float)division, C = 1e-6 (double @0x52D0C0).  Per event:
//   tick = (int)ev.start; walk the tempo table (segment add while
//   tick > table[ci+1].time; tail/break uses table[ci].tempo);
//   ev.start = sec; tempoF = (float)table[ci].tempo;
//   ev.end = sec + C * tempoF * (len / x)  (len = old ev.end float).
//   Then the consonant chain: exact string compares of ev[0..] against
//   "k'","S","tS","J","C","m'","4'","g'","dZ","b'","p'" (vowel-checked,
//   ev[6] in a/i/u/o picks the replacement) and "4","j","p\\","dz","ts"
//   (unconditional), each hit overwriting ev[0..5] with one of the
//   single-letter mouth shapes; then 'm'/'N' -> 'n' on ev[6].
//
// Phase 5 - write the lipsync txt  (0x437BC0..0x437C41)
//   reopen "wt" (discarding the debug dump), per event one line
//   '%7.3f -%7.3f   %-4s %1s %c' (fmt 0x52D040) with (start, end,
//   ev+7 token, ev+0 consonant, ev[6] vowel) via "%s\n".  fclose.
//
// Phase 6 - model checks + morph keys  (0x437C46..0x438B07)
//   resolve the active model, then use its MorphRecord table and count
//   (empty -> 'a' box).
//   3-byte compare finds the SJIS faces "あ"/"い"/"う"/"お"; any miss ->
//   EN 0x52CFF4/52CF6C/52CEE4/52CE5C or JP 0x52CFB0/52CF28/52CEA0/52CE18
//   box (captions "lipsync" 0x52D034 / "vsq解析" 0x52D134) and return.
//   ('え' is synthesized from the あ+い faces, no search.)
//   Clear allocated marks in the bone, morph and display key arrays.
//   Per event (weight is MorphRecord::value, frames are
//   (int)(sec*30.0) with a -2-frame lead-in; -30.0 double @0x52CE10,
//   30.0 @0x52BA68):
//     'h' vowel inherits ev[0]; then
//     'a': w=0 @f1, 0.7f @f2, 0.7f @f3, 0 @f4        (0.7f @0x52CE0C)
//     'i': same shape with 0.6f @0x52CE08
//     'u': same shape with 1.0f
//     'e': a-face 0.5f @f1/f2/f3 + 0 @f4 AND i-face 0.3f @f1/f2/f3 + 0 @f4
//          (0.5f @0x52960C, 0.3f @0x52CE04)
//     'o': same shape with 1.0f
//   every step = set weight then RegisterMorphKeyCurrent(model, idx, frame).
//
// Phase 7 - epilogue  (0x438B0D..0x438CC9)
//   app+0x980 = (int)(last ev.end * 30.0); app+0x9E16C = max(it, model
//   ModelRecord::maxFrame); select-all + EM_REPLACESEL(0xC2) of "%d" into
//   control 0x1A1; SeekModelFrame(model, frame, app+0xA0CC4);
//   app+0x97C = frame > 6 ? frame-6 : 0; PanelPaint(app);
//   if app+0xA06CC: TimelineDrawTicks(0x97C, (HGDIOBJ)app+0xA06C8) +
//   InvalidateRect(hwnd, {6,0x5F,width-3,0x92}, 0);
//   if app+0x91C == 1: AviBgOverlayRefresh(app).
//
// Deviations from the binary (behaviour-preserving unless noted):
//   * the '\\' phoneme path reads an uninitialised stack slot on its
//     first iteration in the original; the port starts it at NULL.
//   * with count == 0 the original still reads 16 bytes before the
//     malloc'd event block for the final frame; the port reads it only
//     when count > 0 (avoids deliberate heap OOB).
//   * the tempo/event tables are malloc'd and never freed, as in the
//     original.
// =========================================================================//
#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <Windows.h>

#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <fcntl.h>
#include <io.h>
#include <share.h>
#include <sys/stat.h>

#include "mikudancestudio/model.hpp"
#include "mikudancestudio/mmd_app.hpp"
#include "mikudancestudio/ported_funcs.hpp"
#include "mikudancestudio/panel_controls.hpp"

namespace mikudancestudio {
namespace {

// ---- JP-locale texts, verbatim Shift-JIS bytes from .rdata ---------------
// 0x52CDD4 "ファイルが読み込めません:%d"
const char kJpOpenFailFmt[] =
    "\x83\x74\x83\x40\x83\x43\x83\x8b\x82\xaa\x93\xc7\x82\xdd\x8d\x9e"
    "\x82\xdf\x82\xdc\x82\xb9\x82\xf1\x3a\x25\x64";
// 0x52D108 "SysExイベントがあります。お手上げ＼(^o^)／"
const char kJpSysEx[] =
    "\x53\x79\x73\x45\x78\x83\x43\x83\x78\x83\x93\x83\x67\x82\xaa\x82"
    "\xa0\x82\xe8\x82\xdc\x82\xb7\x81\x42\x82\xa8\x8e\xe8\x8f\xe3\x82"
    "\xb0\x81\x5f\x28\x5e\x6f\x5e\x29\x81\x5f";
// 0x52D134 "vsq解析"
const char kJpVsqCaption[] = "\x76\x73\x71\x89\xf0\x90\xcd";
// 0x52D0C8 "vsq解析エラー発生！！"
const char kJpAnalysisErr[] =
    "\x76\x73\x71\x89\xf0\x90\xcd\x83\x47\x83\x89\x81\x5b\x94\xad\x90"
    "\xb6\x81\x49\x81\x49";
// 0x52CFB0 / 0x52CF28 / 0x52CEA0 / 0x52CE18
// "このモデルの表情に”あ”の形がない為\n自動リップシンクはできません" (a/i/u/o)
const char kJpNoA[] =
    "\x82\xb1\x82\xcc\x83\x82\x83\x66\x83\x8b\x82\xcc\x95\x5c\x8f\xee"
    "\x82\xc9\x81\x68\x82\xa0\x81\x68\x82\xcc\x8c\x60\x82\xaa\x82\xc8"
    "\x82\xa2\x88\xd7\x0a\x8e\xa9\x93\xae\x83\x8a\x83\x62\x83\x76\x83"
    "\x56\x83\x93\x83\x4e\x82\xcd\x82\xc5\x82\xab\x82\xdc\x82\xb9\x82"
    "\xf1";
const char kJpNoI[] =
    "\x82\xb1\x82\xcc\x83\x82\x83\x66\x83\x8b\x82\xcc\x95\x5c\x8f\xee"
    "\x82\xc9\x81\x68\x82\xa2\x81\x68\x82\xcc\x8c\x60\x82\xaa\x82\xc8"
    "\x82\xa2\x88\xd7\x0a\x8e\xa9\x93\xae\x83\x8a\x83\x62\x83\x76\x83"
    "\x56\x83\x93\x83\x4e\x82\xcd\x82\xc5\x82\xab\x82\xdc\x82\xb9\x82"
    "\xf1";
const char kJpNoU[] =
    "\x82\xb1\x82\xcc\x83\x82\x83\x66\x83\x8b\x82\xcc\x95\x5c\x8f\xee"
    "\x82\xc9\x81\x68\x82\xa4\x81\x68\x82\xcc\x8c\x60\x82\xaa\x82\xc8"
    "\x82\xa2\x88\xd7\x0a\x8e\xa9\x93\xae\x83\x8a\x83\x62\x83\x76\x83"
    "\x56\x83\x93\x83\x4e\x82\xcd\x82\xc5\x82\xab\x82\xdc\x82\xb9\x82"
    "\xf1";
const char kJpNoO[] =
    "\x82\xb1\x82\xcc\x83\x82\x83\x66\x83\x8b\x82\xcc\x95\x5c\x8f\xee"
    "\x82\xc9\x81\x68\x82\xa8\x81\x68\x82\xcc\x8c\x60\x82\xaa\x82\xc8"
    "\x82\xa2\x88\xd7\x0a\x8e\xa9\x93\xae\x83\x8a\x83\x62\x83\x76\x83"
    "\x56\x83\x93\x83\x4e\x82\xcd\x82\xc5\x82\xab\x82\xdc\x82\xb9\x82"
    "\xf1";
// 0x52D178 "拍子=%d/%d メトロノーム間隔=%d 四分音符あたりの三十二分音符の数=%d"
const char kJpTimeSig[] =
    "\x94\x8f\x8e\x71\x3d\x25\x64\x2f\x25\x64\x20\x83\x81\x83\x67\x83"
    "\x8d\x83\x6e\x81\x5b\x83\x80\x8a\xd4\x8a\x75\x3d\x25\x64\x20\x8e"
    "\x6c\x95\xaa\x89\xb9\x95\x84\x82\xa0\x82\xbd\x82\xe8\x82\xcc\x8e"
    "\x4f\x8f\x5c\x93\xf1\x95\xaa\x89\xb9\x95\x84\x82\xcc\x90\x94\x3d"
    "\x25\x64";
// 0x52D158 "キー(調) ＃(♭)の数:%d 長調" (major)
const char kJpKeyMajor[] =
    "\x83\x4c\x81\x5b\x28\x92\xb2\x29\x20\x81\x94\x28\x81\xf3\x29\x82"
    "\xcc\x90\x94\x3a\x25\x64\x20\x92\xb7\x92\xb2";
// 0x52D13C "キー(調) ＃(♭)の数:%d 短調" (minor)
const char kJpKeyMinor[] =
    "\x83\x4c\x81\x5b\x28\x92\xb2\x29\x20\x81\x94\x28\x81\xf3\x29\x82"
    "\xcc\x90\x94\x3a\x25\x64\x20\x92\x5a\x92\xb2";

// ---- vowel face needles: SJIS kana + NUL, compared over 3 bytes ----------
const char kFaceA[] = "\x82\xa0";  // あ 0x52D03C
const char kFaceI[] = "\x82\xa2";  // い 0x52CFAC
const char kFaceU[] = "\x82\xa4";  // う 0x52CF24
const char kFaceO[] = "\x82\xa8";  // お 0x52CE9C

// ---- 8-byte tempo pair / 0x24-byte event record --------------------------
struct TempoEntry {
    int time;
    int tempo;
};

struct VsqEvent {           // stride 0x24, calloc'd by the original
    char consonant[6];      // +0x00 canonical mouth shape (sprintf cap 6)
    char vowel;             // +0x06
    char token[9];          // +0x07 raw L0 token (written via a cast with
                            //        sprintf cap 8 - x64 0x7FF7CB48F3D5
                            //        "mov edx, 8": 7 chars + NUL land in
                            //        +0x07..+0x0E, +0x0F keeps its memset
                            //        zero and +0x10 (start) is never hit)
    float start;            // +0x10 tick -> start seconds
    float end;              // +0x14 length ticks -> end seconds
    char handle[10];        // +0x18 lyric-handle id (sprintf cap 10)
    char tail[2];           // +0x22
};
static_assert(sizeof(VsqEvent) == 0x24, "event record stride");

void VsqBox(MMDApp* app, const char* enText, const char* jpText,
            const char* enCaption, const char* jpCaption) {
    MessageBoxA(static_cast<HWND>(app->Hwnd()),
                app->EnglishUI() != 0 ? enText : jpText,
                app->EnglishUI() != 0 ? enCaption : jpCaption, MB_OK);
}

// 0x436C23 / 0x436C86 - "vsq analysis" failure; closes both handles.
void VsqAnalysisError(MMDApp* app, FILE* txt, int fd) {
    VsqBox(app,
           "vsq analysis error!!",                 // 0x52D0E0
           kJpAnalysisErr,                         // 0x52D0C8
           "vsq analysis",                         // 0x52D0F8
           kJpVsqCaption);                         // 0x52D134
    fclose(txt);                                   // 0x507D89
    _close(fd);                                    // 0x506B88
}

unsigned char* SlotModel(MMDApp* app) {
    return app->SelectedModel();
}

// One mouth-shape keyframe: park the weight into the face slot, then let
// RegisterMorphKeyCurrent (0x49EEE0) register it (exact original pairing).
void VsqMorphKey(unsigned char* model, mdl::MorphRecord* morphs, int idx,
                 int frame, float weight) {
    morphs[idx].value = weight;
    RegisterMorphKeyCurrent(model, idx, frame);
}

}  // namespace

// VA 0x00435FE0 - VSQ load and auto lipsync.
void LoadVsqFile(MMDApp* app, const wchar_t* path) {
    auto& s = *app;
    HWND hwnd = static_cast<HWND>(s.Hwnd());                       // 0xa06b8
    char line[0x100];        // shared fgets buffer (single buffer as binary)
    char idbuf[0x100];       // "[ID#%04d]"
    char buf[1000];          // 0x3E8 sprintf target
    char scratch[1000];      // ReadFixedString out

    // ---- 0x436000: probe the file ------------------------------------
    int fd = -1;
    const errno_t openErr =
        _wsopen_s(&fd, path, 0x8000 /*_O_BINARY*/, 0x40 /*_SH_DENYNO*/,
                  0x80 /*_S_IWRITE*/);
    if (openErr != 0) {
        if (s.EnglishUI() != 0)
            sprintf_s(buf, 1000, "Cannot open file:%d", openErr);  // 0x52CDF0
        else
            sprintf_s(buf, 1000, kJpOpenFailFmt, openErr);         // 0x52CDD4
        MessageBoxA(hwnd, buf, "", 0);                             // 0x529679
        return;
    }

    // ---- 0x436093: ".vsq" -> ".txt" in the caller's path buffer ------
    wchar_t* ext = const_cast<wchar_t*>(wcsstr(path, L".vsq")) + 1;
    ext[0] = L't';                                                 // 0x74
    ext[1] = L'x';                                                 // 0x78
    ext[2] = L't';                                                 // 0x74

    FILE* txt = nullptr;
    _wfopen_s(&txt, path, L"wt");                                  // 0x50801A

    // ---- 0x4360CB: SMF header ---------------------------------------
    int v = 0;
    int ntracks = 0;
    int division = 0;
    ReadFixedString(fd, scratch, 4);              // "MThd"
    ReadBeWord(fd, &v, 4);                   // header length
    ReadBeWord(fd, &v, 2);                   // format
    ReadBeWord(fd, &ntracks, 2);
    sprintf_s(buf, 1000, "[number of Track:%d]", ntracks);         // 0x52D278
    fprintf(txt, "%s\n", buf);                                     // 0x52D274
    ReadBeWord(fd, &division, 2);
    sprintf_s(buf, 1000, "[UnitTime:%d]", division);               // 0x52D264
    fprintf(txt, "%s\n\n", buf);                                   // 0x52D25C

    // ---- 0x4361B0: track/event dump ---------------------------------
    int tempoCount = 0;
    int trackIdx = 0;
    if (ntracks > 0) {
        do {
            sprintf_s(buf, 1000, "[Track%d]", trackIdx);           // 0x52D250
            fprintf(txt, "%s\n", buf);
            ReadFixedString(fd, scratch, 4);      // "MTrk"
            ReadBeWord(fd, &v, 4);           // track length

            int time = 0;                   // accumulated varlen delta
            for (;;) {                      // 0x436214 event loop
                ReadBeWord(fd, &v, 1);
                int delta = v;
                if (delta > 0x7f) {
                    do {
                        ReadBeWord(fd, &v, 1);
                        delta = ((delta - 0x80) << 7) + v;          // 0x436247
                    } while (v > 0x7f);
                }
                time += delta;
                int status = 0;
                ReadBeWord(fd, &status, 1);
                if (status >= 0x80 && status <= 0x8f) {
                    ReadBeWord(fd, &v, 2);
                    continue;
                }
                if (status >= 0x90 && status <= 0x9f) {
                    ReadBeWord(fd, &v, 2);
                    continue;
                }
                if (status >= 0xb0 && status <= 0xbf) {
                    ReadBeWord(fd, &v, 2);
                    continue;
                }
                if (status == 0xf0 || status == 0xf7) {             // 0x436BDC
                    MessageBoxA(hwnd, kJpSysEx, kJpVsqCaption, 0);
                    fclose(txt);
                    _close(fd);
                    return;
                }
                if (status != 0xff) {                               // 0x436C86
                    VsqAnalysisError(app, txt, fd);
                    return;
                }

                int type = 0;
                ReadBeWord(fd, &type, 1);
                if (type == 0) {
                    ReadBeWord(fd, &v, 1);
                    continue;
                }
                if (type == 1) {                                    // 0x436365
                    int len = 0;
                    ReadBeWord(fd, &len, 1);
                    ReadFixedString(fd, scratch, 8);
                    ReadFixedString(fd, scratch, len - 8);
                    fprintf(txt, "%s", scratch);                    // 0x52BD14
                    continue;
                }
                if (type >= 2 && type <= 5) {                       // 0x4363DC
                    int len = 0;
                    ReadBeWord(fd, &len, 1);
                    ReadFixedString(fd, scratch, len);
                    continue;
                }
                if (type == 0x2f) {                                 // 0x436416
                    int len = 0;
                    ReadBeWord(fd, &len, 1);
                    if (len != 0)
                        continue;
                    sprintf_s(buf, 1000, "[End of Track%d]", trackIdx);
                    fprintf(txt, "%s\n\n", buf);                    // 0x52D25C
                    ++trackIdx;
                    break;          // next track (or done)
                }
                if (type == 0x51) {                                 // 0x436A2A
                    int tempo = 0;
                    ReadBeWord(fd, &v, 1);    // length
                    ReadBeWord(fd, &tempo, 3);
                    sprintf_s(buf, 1000,
                              "[ChangeTempo time=%d,tempo=%d]",     // 0x52D1BC
                              time, tempo);
                    fprintf(txt, "%s\n", buf);
                    ++tempoCount;
                    continue;
                }
                if (type == 0x58) {                                 // 0x436A9A
                    unsigned char lenByte = 0;                      // 0x509057
                    _read(fd, &lenByte, 1);
                    int nn = 0, dd = 0, cc = 0, bb = 0;
                    ReadBeWord(fd, &nn, 1);
                    ReadBeWord(fd, &dd, 1);
                    ReadBeWord(fd, &cc, 1);
                    ReadBeWord(fd, &bb, 1);
                    int den = 1;
                    for (int t = dd; t > 0; --t) den += den;        // 0x436B03
                    sprintf_s(buf, 1000, kJpTimeSig,                // 0x52D178
                              nn, den, cc, bb);  // formatted, not written
                    continue;
                }
                if (type == 0x59) {                                 // 0x436B42
                    int sf = 0, mi = 0;
                    ReadBeWord(fd, &v, 1);    // length
                    ReadBeWord(fd, &sf, 1);
                    ReadBeWord(fd, &mi, 1);
                    sprintf_s(buf, 1000,
                              mi == 0 ? kJpKeyMajor : kJpKeyMinor,  // 0x52D158/C
                              sf);
                    fprintf(txt, "%s\n", buf);
                    continue;
                }
                VsqAnalysisError(app, txt, fd);                     // 0x436C23
                return;
            }
        } while (trackIdx < ntracks);
    }

    fclose(txt);                                                    // 0x43647F
    _close(fd);                                                     // 0x43648A

    // ---- 0x43648F: tempo table out of the txt -----------------------
    TempoEntry* tempoTable =
        static_cast<TempoEntry*>(malloc(
            static_cast<std::size_t>(tempoCount) * sizeof(TempoEntry)));
    FILE* rd = nullptr;
    _wfopen_s(&rd, path, L"r");                                     // 0x52BCB4
    for (int k = 0; k < tempoCount; ++k) {                          // 0x4364C9
        fgets(line, 0x100, rd);
        if (strstr(line, "[ChangeTempo time=") == nullptr) {        // 0x52D228
            do {
                fgets(line, 0x100, rd);
            } while (strstr(line, "[ChangeTempo time=") == nullptr);
        }
        char* p = strstr(line, "=") + 1;                            // 0x52D224
        char* q = strstr(p, ",");                                   // 0x52D220
        *q = 0;
        tempoTable[k].time = atoi(p);
        char* p2 = strstr(q + 1, "=") + 1;
        char* q2 = strstr(p2, "]");                                 // 0x52D21C
        *q2 = 0;
        tempoTable[k].tempo = atoi(p2);
    }

    // ---- 0x4365B2: locate [EventList], count the "=ID#" pairs --------
    if (strstr(line, "[EventList]") == nullptr) {                   // 0x52D210
        do {
            fgets(line, 0x100, rd);
        } while (strstr(line, "[EventList]") == nullptr);
    }
    int count = 0;
    fgets(line, 0x100, rd);
    fgets(line, 0x100, rd);
    if (strstr(line, "=ID#") != nullptr) {                          // 0x52D208
        do {
            ++count;
            fgets(line, 0x100, rd);
        } while (strstr(line, "=ID#") != nullptr);
    }
    fclose(rd);

    VsqEvent* events = static_cast<VsqEvent*>(
        malloc(static_cast<std::size_t>(count) * 0x24));
    memset(events, 0, static_cast<std::size_t>(count) * 0x24);      // 0x507530

    // ---- 0x4366CD: refill pass --------------------------------------
    FILE* rd2 = nullptr;
    _wfopen_s(&rd2, path, L"r");
    if (strstr(line, "[EventList]") == nullptr) {
        do {
            fgets(line, 0x100, rd2);
        } while (strstr(line, "[EventList]") == nullptr);
    }
    fgets(line, 0x100, rd2);                // first EventList entry

    for (int i = 0; i < count; ++i) {       // ticks 0x436760
        fgets(line, 0x100, rd2);
        char* p = strstr(line, "=");
        *p = 0;
        events[i].start = static_cast<float>(atoi(line));
    }
    for (int i = 0; i < count; ++i) {       // length + handle 0x4367C1
        sprintf_s(idbuf, 0x100, "[ID#%04d]", i + 1);                // 0x52D1FC
        if (strstr(line, idbuf) == nullptr) {
            do {
                fgets(line, 0x100, rd2);
            } while (strstr(line, idbuf) == nullptr);
        }
        if (strstr(line, "Length=") == nullptr) {                   // 0x52D1F4
            do {
                fgets(line, 0x100, rd2);
            } while (strstr(line, "Length=") == nullptr);
        }
        char* p = strstr(line, "=");
        events[i].end = static_cast<float>(atoi(p + 1));
        if (strstr(line, "LyricHandle=") == nullptr) {              // 0x52D1E4
            do {
                fgets(line, 0x100, rd2);
            } while (strstr(line, "LyricHandle=") == nullptr);
        }
        p = strstr(line, "=");
        // the handle id doubles as the format string, like the original
        sprintf_s(events[i].handle, 10, p + 1);
        events[i].handle[6] = 0;
    }

    // ---- 0x436940: phoneme extraction (still on rd2) ----------------
    // 注：x64 里空格指针是 strstr 每轮现算的临时值（rsi），没有跨迭代状态；
    // port 曾残留一个 loop-carried 的 carried 指针，'\' 分支里 ++carried
    // 先于判空自增（nullptr 时得 (char*)1，carried[1] 读地址 2 直接崩溃），
    // 且取到的是刚断言过为 ' ' 的字节，元音恒为空格。已按 x64 删除。
    for (int j = 0; j < count; ++j) {
        VsqEvent& ev = events[j];
        if (strstr(line, ev.handle) == nullptr) {
            do {
                fgets(line, 0x100, rd2);
            } while (strstr(line, ev.handle) == nullptr);
        }
        if (j == 0xae) {
            // original stores j back to itself here; no effect
        }
        fgets(line, 0x100, rd2);            // the lyric line
        char* m = strstr(line, "\",\"");                             // 0x52D1E0
        *m = 0;                    // truncate the line at the '","'
        char* firstQuote = strstr(line, "\"");                      // 0x52D1DC
        // cap 8 as in the binary (0x7FF7CB48F3D5 mov edx,8;
        // 0x7FF7CB48F3DA lea rcx,[r15+7])
        sprintf_s(reinterpret_cast<char*>(&ev) + 7, 8, firstQuote + 1);

        const char* m1 = m + 1;              // points at the ','
        if (m1[3] == '"') {                  // 0x436A16: 1-char field
            ev.consonant[0] = 0;
            ev.vowel = m[3];
        } else if (m1[3] == '\\') {          // 0x7FF7CB48F664: X-SAMPA "p\ a"
            // ふ行等带 '\' 的音素：直接检查 m[5] 是否空格（0x7FF7CB48F668），
            // 命中则先抹掉该空格（0x7FF7CB48F67D，m+3 截短后兼作格式串），
            // 元音取空格后的 m[6]（0x7FF7CB48F692）
            if (m[5] == ' ') {
                m[5] = 0;
                sprintf_s(ev.consonant, 6, m + 3);
                ev.vowel = m[6];
            } else {
                ev.consonant[0] = 0;        // 0x7FF7CB48F6B4（公共落空路径）
                ev.vowel = 'N';
            }
        } else {
            char* space = strstr(const_cast<char*>(m1), " ");        // 0x52BEB0
            if (space == nullptr) {
                ev.consonant[0] = 0;
                ev.vowel = 'N';
            } else {
                *space = 0;
                sprintf_s(ev.consonant, 6, m + 3);
                ev.vowel = space[1];
            }
        }
        if (ev.vowel == 'M')                                         // 0x4D
            ev.vowel = 'u';                                          // 0x75
    }
    fclose(rd2);                                                    // 0x436D63

    // ---- 0x436D90: seconds + consonant classification ---------------
    const float xdiv = static_cast<float>(division);                // 0x436D83
    const double kMicro = 9.999999974752427e-07;   // double @0x52D0C0
    for (int i = 0; i < count; ++i) {
        VsqEvent& ev = events[i];
        int tick = static_cast<int>(ev.start);   // _ftol trunc 0x5075B0
        float sec = 0.0f;
        int ci = 0;
        // one tempo segment: sec += ticks/x * tempo * 1e-6 (80-bit in the
        // original, float-rounded on each store)
        auto segAdd = [&](int ticks, int tempo) {
            sec = static_cast<float>(
                sec + static_cast<double>(ticks) / xdiv *
                          static_cast<double>(tempo) * kMicro);
        };
        if (tempoCount > 0) {
            for (;;) {
                if (ci + 1 == tempoCount) {                         // 0x436DFD
                    segAdd(tick, tempoTable[ci].tempo);
                    break;
                }
                if (tick <= tempoTable[ci + 1].time) {              // 0x436E11
                    segAdd(tick, tempoTable[ci].tempo);
                    break;
                }
                // 0x436DD7: the crossed segment is accumulated with the
                // CURRENT entry's tempo - piVar1[1] is read before the
                // table pointer advances to piVar16 (= &table[ci+1]).
                const int t = tempoTable[ci + 1].time;
                segAdd(t, tempoTable[ci].tempo);
                tick -= t;
                ++ci;
                if (ci >= tempoCount)
                    break;
            }
        }
        ev.start = sec;                                             // 0x436E39
        const float len = ev.end;
        ev.end = sec +
                 static_cast<float>(kMicro *
                     (static_cast<double>(tempoTable[ci].tempo) *
                      (static_cast<double>(len) / xdiv)));          // 0x436E5B

        // Consonant chain (exact compares incl. NUL; the vowel tests are
        // four independent checks in the original - an unmatched vowel
        // leaves the token untouched for that needle).
        char* token = ev.consonant;
        if (strncmp(token, "k'", 3) == 0) {                          // 0x52D0BC
            if (ev.vowel == 'a')      sprintf_s(token, 6, "K");     // 0x52D0B8
            if (ev.vowel == 'i')      sprintf_s(token, 6, "k");     // 0x52D0B4
            if (ev.vowel == 'u')      sprintf_s(token, 6, "K");
            if (ev.vowel == 'o')      sprintf_s(token, 6, "K");
        }
        if (strncmp(token, "S", 2) == 0) {                           // 0x52D0B0
            if (ev.vowel == 'a')      sprintf_s(token, 6, "S");
            if (ev.vowel == 'i')      sprintf_s(token, 6, "s");     // 0x52C76C
            if (ev.vowel == 'u')      sprintf_s(token, 6, "S");
            if (ev.vowel == 'o')      sprintf_s(token, 6, "S");
        }
        if (strncmp(token, "tS", 3) == 0) {                          // 0x52D0AC
            if (ev.vowel == 'a')      sprintf_s(token, 6, "T");     // 0x52D0A8
            if (ev.vowel == 'i')      sprintf_s(token, 6, "t");     // 0x52C770
            if (ev.vowel == 'u')      sprintf_s(token, 6, "T");
            if (ev.vowel == 'o')      sprintf_s(token, 6, "T");
        }
        if (strncmp(token, "J", 2) == 0) {                           // 0x52D0A4
            if (ev.vowel == 'a')      sprintf_s(token, 6, "N");     // 0x52D0A0
            if (ev.vowel == 'i')      sprintf_s(token, 6, "n");     // 0x52C794
            if (ev.vowel == 'u')      sprintf_s(token, 6, "N");
            if (ev.vowel == 'o')      sprintf_s(token, 6, "N");
        }
        if (strncmp(token, "C", 2) == 0) {                           // 0x52D09C
            if (ev.vowel == 'a')      sprintf_s(token, 6, "H");     // 0x52D098
            if (ev.vowel == 'i')      sprintf_s(token, 6, "h");     // 0x52C774
            if (ev.vowel == 'u')      sprintf_s(token, 6, "H");
            if (ev.vowel == 'o')      sprintf_s(token, 6, "H");
        }
        if (strncmp(token, "m'", 3) == 0) {                          // 0x52D094
            if (ev.vowel == 'a')      sprintf_s(token, 6, "M");     // 0x52C7B0
            if (ev.vowel == 'i')      sprintf_s(token, 6, "m");     // 0x52C788
            if (ev.vowel == 'u')      sprintf_s(token, 6, "M");
            if (ev.vowel == 'o')      sprintf_s(token, 6, "M");
        }
        if (strncmp(token, "4'", 3) == 0) {                          // 0x52D090
            if (ev.vowel == 'a')      sprintf_s(token, 6, "R");     // 0x52D08C
            if (ev.vowel == 'i')      sprintf_s(token, 6, "r");     // 0x52C784
            if (ev.vowel == 'u')      sprintf_s(token, 6, "R");
            if (ev.vowel == 'o')      sprintf_s(token, 6, "R");
        }
        if (strncmp(token, "g'", 3) == 0) {                          // 0x52D088
            if (ev.vowel == 'a')      sprintf_s(token, 6, "G");     // 0x52D084
            if (ev.vowel == 'i')      sprintf_s(token, 6, "g");     // 0x52C778
            if (ev.vowel == 'u')      sprintf_s(token, 6, "G");
            if (ev.vowel == 'o')      sprintf_s(token, 6, "G");
        }
        if (strncmp(token, "dZ", 3) == 0) {                          // 0x52D080
            if (ev.vowel == 'a')      sprintf_s(token, 6, "Z");     // 0x52C75C
            if (ev.vowel == 'i')      sprintf_s(token, 6, "z");     // 0x52D07C
            if (ev.vowel == 'u')      sprintf_s(token, 6, "Z");
            if (ev.vowel == 'o')      sprintf_s(token, 6, "Z");
        }
        if (strncmp(token, "b'", 3) == 0) {                          // 0x52D078
            if (ev.vowel == 'a')      sprintf_s(token, 6, "B");     // 0x52C798
            if (ev.vowel == 'i')      sprintf_s(token, 6, "b");     // 0x52D074
            if (ev.vowel == 'u')      sprintf_s(token, 6, "B");
            if (ev.vowel == 'o')      sprintf_s(token, 6, "B");
        }
        if (strncmp(token, "p'", 3) == 0) {                          // 0x52D070
            if (ev.vowel == 'a')      sprintf_s(token, 6, "P");     // 0x52C6E4
            if (ev.vowel == 'i')      sprintf_s(token, 6, "p");     // 0x52D06C
            if (ev.vowel == 'u')      sprintf_s(token, 6, "P");
            if (ev.vowel == 'o')      sprintf_s(token, 6, "P");
        }
        if (strncmp(token, "4", 2) == 0)                             // 0x52C74C
            sprintf_s(token, 6, "r");                                // 0x52C784
        if (strncmp(token, "j", 2) == 0)                             // 0x52D068
            sprintf_s(token, 6, "y");                                // 0x52C768
        if (strncmp(token, "p\\", 3) == 0)                           // 0x52D064
            sprintf_s(token, 6, "h");                                // 0x52C774
        if (strncmp(token, "dz", 3) == 0)                            // 0x52D060
            sprintf_s(token, 6, "z");                                // 0x52D07C
        if (strncmp(token, "ts", 3) == 0)                            // 0x52D05C
            sprintf_s(token, 6, "t");                                // 0x52C770
        if (ev.vowel == 'm') ev.vowel = 'n';                         // 0x437B9D
        if (ev.vowel == 'N') ev.vowel = 'n';                         // 0x437BA7
    }

    // ---- 0x437BC0: write the lipsync txt ----------------------------
    FILE* out = nullptr;
    _wfopen_s(&out, path, L"wt");                                   // 0x52BC88
    for (int i = 0; i < count; ++i) {
        VsqEvent& ev = events[i];
        char* token = reinterpret_cast<char*>(&ev) + 7;
        sprintf_s(buf, 1000,
                  "%7.3f -%7.3f   %-4s %1s %c",                     // 0x52D040
                  static_cast<double>(ev.start),
                  static_cast<double>(ev.end),
                  token, ev.consonant,
                  static_cast<char>(ev.vowel));
        fprintf(out, "%s\n", buf);                                  // 0x52D274
    }
    fclose(out);                                                    // 0x437C41

    // ---- 0x437C46: face availability --------------------------------
    unsigned char* model = SlotModel(app);
    mdl::ModelRecord& modelRecord = *mdl::Mdl(model);
    const int faceCount = modelRecord.morphCount;
    int faceA = 0, faceI = 0, faceU = 0, faceO = 0;
    if (faceCount <= 0) {
        VsqBox(app,
               "Cannot Lipsync because this model has not facial "
               "data of 'a'!!",                                    // 0x52CFF4
               kJpNoA, "lipsync", kJpVsqCaption);                  // 0x52D034
        return;
    }
    mdl::MorphRecord* const morphs = mdl::Morphs(model);
    {
        int i = 0;
        for (; i < faceCount; ++i) {
            if (memcmp(morphs[i].name, kFaceA, 3) == 0) break;
        }
        if (i >= faceCount) {
            VsqBox(app,
                   "Cannot Lipsync because this model has not facial "
                   "data of 'a'!!",
                   kJpNoA, "lipsync", kJpVsqCaption);
            return;
        }
        faceA = i;
    }
    {
        int i = 0;
        for (; i < faceCount; ++i) {
            if (memcmp(morphs[i].name, kFaceI, 3) == 0) break;
        }
        if (i >= faceCount) {
            VsqBox(app,
                   "Cannot Lipsync because this model has not facial "
                   "data of 'i'!!",                                // 0x52CF6C
                   kJpNoI, "lipsync", kJpVsqCaption);
            return;
        }
        faceI = i;
    }
    {
        int i = 0;
        for (; i < faceCount; ++i) {
            if (memcmp(morphs[i].name, kFaceU, 3) == 0) break;
        }
        if (i >= faceCount) {
            VsqBox(app,
                   "Cannot Lipsync because this model has not facial "
                   "data of 'u'!!",                                // 0x52CEE4
                   kJpNoU, "lipsync", kJpVsqCaption);
            return;
        }
        faceU = i;
    }
    {
        int i = 0;
        for (; i < faceCount; ++i) {
            if (memcmp(morphs[i].name, kFaceO, 3) == 0) break;
        }
        if (i >= faceCount) {
            VsqBox(app,
                   "Cannot Lipsync because this model has not facial "
                   "data of 'o'!!",                                // 0x52CE5C
                   kJpNoO, "lipsync", kJpVsqCaption);
            return;
        }
        faceO = i;
    }

    // ---- 0x437F50: clear the keyframe mark bytes --------------------
    mdl::BoneKey* const boneKeys = mdl::BoneKeys(model);
    for (int i = 0; i < static_cast<int>(mdl::kBoneKeyCapacity); ++i)
        boneKeys[i].allocated = 0;
    mdl::MorphKey* const morphKeys = mdl::MorphKeys(model);
    for (int i = 0; i < 20000; ++i)
        morphKeys[i].allocated = 0;
    mdl::DisplayKey* const displayKeys = mdl::DisplayKeys(model);
    for (int i = 0; i < 1000; ++i)
        displayKeys[i].allocated = 0;

    // ---- 0x43813B: per-event morph keyframes ------------------------
    for (int i = 0; i < count; ++i) {
        VsqEvent& ev = events[i];
        const double start = ev.start;
        const double end = ev.end;
        // 0x438150 (x64 0x7FF7CB4908C8): vowel=='h' 时取 ev+0x00 的首字节
        // 写回 vowel——r12=&ev.start(+0x10)，[r12-0Ah]=vowel(+0x06)，
        // [r12-10h]=ev+0x00，即 consonant[0]（分类轮重写后的口型 token）。
        if (ev.vowel == 'h')
            ev.vowel = ev.consonant[0];
        const int f1 = -2 - static_cast<int>(start * -30.0);        // 0x52CE10
        const int f2 = static_cast<int>(start * 30.0);              // 0x52BA68
        const int f3 = -2 - static_cast<int>(end * -30.0);
        const int f4 = static_cast<int>(end * 30.0);
        if (ev.vowel == 'a') {                                      // 0x43816D
            VsqMorphKey(model, morphs, faceA, f1, 0.0f);         // 0x52CE0C=.7
            VsqMorphKey(model, morphs, faceA, f2, 0.7f);
            VsqMorphKey(model, morphs, faceA, f3, 0.7f);
            VsqMorphKey(model, morphs, faceA, f4, 0.0f);
        } else if (ev.vowel == 'i') {                               // 0x438308
            VsqMorphKey(model, morphs, faceI, f1, 0.0f);         // 0x52CE08=.6
            VsqMorphKey(model, morphs, faceI, f2, 0.6f);
            VsqMorphKey(model, morphs, faceI, f3, 0.6f);
            VsqMorphKey(model, morphs, faceI, f4, 0.0f);
        } else if (ev.vowel == 'u') {                               // 0x43849B
            VsqMorphKey(model, morphs, faceU, f1, 0.0f);
            VsqMorphKey(model, morphs, faceU, f2, 1.0f);
            VsqMorphKey(model, morphs, faceU, f3, 1.0f);
            VsqMorphKey(model, morphs, faceU, f4, 0.0f);
        } else if (ev.vowel == 'e') {                               // 0x438636
            VsqMorphKey(model, morphs, faceA, f1, 0.0f);
            VsqMorphKey(model, morphs, faceI, f1, 0.0f);
            VsqMorphKey(model, morphs, faceA, f2, 0.5f);         // 0x52960C
            VsqMorphKey(model, morphs, faceI, f2, 0.3f);         // 0x52CE04
            VsqMorphKey(model, morphs, faceA, f3, 0.5f);
            VsqMorphKey(model, morphs, faceI, f3, 0.3f);
            VsqMorphKey(model, morphs, faceA, f4, 0.0f);
            VsqMorphKey(model, morphs, faceI, f4, 0.0f);
        } else if (ev.vowel == 'o') {                               // 0x438975
            VsqMorphKey(model, morphs, faceO, f1, 0.0f);
            VsqMorphKey(model, morphs, faceO, f2, 1.0f);
            VsqMorphKey(model, morphs, faceO, f3, 1.0f);
            VsqMorphKey(model, morphs, faceO, f4, 0.0f);
        }
    }

    // ---- 0x438B0D: frame + UI epilogue ------------------------------
    if (count > 0) {
        // (the original reads events+count*0x24-0x10 unconditionally)
        const float lastEnd =
            events[count - 1].end;
        s.state.currentFrame =
            static_cast<int>(lastEnd * 30.0);
    }
    const int frameNow = s.state.currentFrame;
    const std::uint32_t modelMax = modelRecord.maxFrame;
    if (s.LastRegisteredFrame() < modelMax)
        s.LastRegisteredFrame() = modelMax;

    HWND frameEdit = GetDlgItem(hwnd, panel::kCurrentFrameEdit);                       // 0x5292BC
    SendMessageA(frameEdit, EM_SETSEL, 0,
                 GetWindowTextLengthA(frameEdit));                  // 0x529260
    sprintf_s(buf, 1000, "%d", frameNow);                           // 0x52B9F4
    frameEdit = GetDlgItem(hwnd, panel::kCurrentFrameEdit);
    SendMessageA(frameEdit, EM_REPLACESEL, 0,
                 reinterpret_cast<LPARAM>(buf));                    // 0x5292B4

    SeekModelFrame(model, frameNow, s.PlaybackPhysicsMode());             // 0x4B4260

    s.state.timelineStartFrame =
        frameNow > 6 ? frameNow - 6 : 0;
    PanelPaint(app);                                                // 0x414610

    if (s.state.waveEnabled != 0) {
        TimelineDrawTicks(s.state.timelineStartFrame,
                          s.state.sidebarWidth);
        RECT rc;
        rc.left = 6;
        rc.top = 0x5F;
        rc.right = static_cast<LONG>(
            s.state.sidebarWidth) - 3;
        rc.bottom = 0x92;
        InvalidateRect(hwnd, &rc, 0);                               // 0x529280
    }
    if (s.state.aviBackgroundEnabled == 1)
        AviBgOverlayRefresh(app);                                   // 0x4168D0
}

}  // namespace mikudancestudio
