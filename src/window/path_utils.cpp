// ===========================================================================
// VA 0x00408960 - ExtractDirFromPath  (original: sub_408960)
// ===========================================================================
// __thiscall(wchar_t* this, const wchar_t* Source):
//   wcscpy_s(this + 512, 1000, Source);  then strip the file part:
//     - empty path        -> leave as-is
//     - trailing L'\\'    -> drop the trailing backslash
//     - backslash found   -> truncate at the last backslash (keep it -> no,
//                            set *pos = 0 so dir excludes the backslash)
//     - no backslash      -> first char = 0 (empty dir)
//   Returns this + 512.
//
// Deviation note: in the original the caller (0x0047A5B0) passes a stack
// local of only 256 wchars as `this`, so the +512-wchar output lands in an
// adjacent stack local - stack-layout reuse that is UB to replicate.  The
// port exposes the same algorithm with an explicit output buffer.
// ===========================================================================
#include <cwchar>

#include "mikudancestudio/ported_funcs.hpp"

namespace mikudancestudio {

wchar_t* ExtractDirFromPath(wchar_t* destination, const wchar_t* fullPath) {
    wchar_t* out = destination; // caller owns a wchar_t[1000]-class buffer
    wcscpy_s(out, 1000, fullPath);

    if (*out == L'\0')
        return out;

    // walk to the terminator, then inspect the last character
    wchar_t* p = out;
    while (*p++)
        ;
    --p;                   // p -> terminator
    wchar_t* last = p - 1; // last real character
    if (*last == L'\\') {
        *last = L'\0'; // trailing backslash: drop it
        return out;
    }
    while (last >= out) {
        if (*last == L'\\') {
            *last = L'\0'; // truncate at final backslash
            return out;
        }
        --last;
    }
    *out = L'\0'; // no backslash at all
    return out;
}

} // namespace mikudancestudio
