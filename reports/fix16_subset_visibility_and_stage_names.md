# Stage filename registration and material visibility

## Confirmed defects

The embedded host registered models through ExpGetPmdFilename/ExpGetAcsFilename.
Those exports use a 256-byte Shift-JIS path conversion, whose unrepresentable
character fallback uses the CRT locale and can return an empty string. The
model itself had already loaded through a wide path. An empty engine filename
cannot match Ray's *.pmx DefaultEffect rule and gives a blank mapping row.

The mapping dialog retained each material index but discarded it on checkbox
writes, calling model->setShown for Main and a whole-object hide rule for other
targets. Readback also displayed the whole-object state for every material.

## Changes

- Register embedded MME paths directly from the original wide path, converting
  to the engine's Windows code page without the SJIS/CRT fallback or fixed
  export buffer. Retain the original wide name separately and insert the object
  label with LVM_INSERTITEMW. The public MMD compatibility exports are unchanged.
- Main material rows use the existing indexed EMM visibility registry.
- Offscreen resources hold independent path and visibility overrides keyed by
  object ID and material index. Subsets inherit whole-object defaults only when
  they do not have an explicit override. Defaults are no longer rewritten.
- Render eligibility queries the active target and current material index;
  Main visibility is no longer an unconditional gate on other targets.
- Checkbox clicks affect the clicked row. Hide/Show commands still operate on
  the selected rows. Effect text and checkbox refresh read the same subset.
- Object unload removes its target overrides to avoid reuse of stale IDs.
- Bound DragQueryFileW by the actual destination array size (256 wchar_t).

## Validation and limits

The mapping regression exercises Main sky-only hiding, target and sibling
isolation, distinct object identities, subset effect preservation across
visibility changes, explicit child visibility overriding a hidden parent, and
non-SJIS filename matching/Unicode display metadata. The tests run under
Windows ACP 1252; Unicode label retention and Ray *.pmx matching are checked
even when the ANSI filename cannot represent all original characters.

This fixes the empty filename and generic Ray assignment failure. It does not
convert every legacy narrow file API to Unicode: automatically opening an FX
whose own path is outside the system code page remains a separate limitation.
The user's exact stage asset was not available for a visual scene comparison.
Offscreen override EMM persistence and complete official dialog parity are not
certified by these tests.

Final Release builds succeeded for x64/x86. CTest: 21/21 passed on both
(x64 12.77 s; x86 65.57 s). Logs: build-x64/subset-fix-build.log,
build-x64/subset-fix-ctest.log and corresponding build-x86 paths.
