// ===========================================================================
// MikuDanceStudio - standalone .data/.rdata globals recovered from the original
// ===========================================================================
#pragma once

namespace mikudancestudio {

// VA 0x00529688 - L"%s%s"; used by WinMain's no-command-line branch as the
// swprintf_s format for the env file name buffer.  The original calls it
// with zero varargs (verified in disassembly at 0x004C44CA..0x004C44DA) -
// behaviour intentionally preserved.
extern const wchar_t g_SourceFormat[];

// VA 0x00529679 - "" (empty ANSI string); format source for the three
// recent-file buffers in InitDefaults (sprintf_s(..., Locale)).
extern const char g_Locale[];

// VA 0x0052B9F0 - 4294967296.0f (2^32): negative-frame wrap fix in the
// animation-frame section of the frame driver (0x0046B090).
extern const float g_Wrap32;

// Timeline frames per second. Original x64 .rdata RVA 0x132A64
// contains 30.0f; frame-driver divisions at RVA 0x3717B / 0x3719A.
extern const float g_FrameScale;

// The FrameDriver's var_14C8 dt budget (0x46EFDE/0x46F00D): initialised to
// the real elapsed time (or 1/fps in frame-step recording mode), decremented
// by 1/60 per catch-up substep (0x46F226 in BLOCK 1 / the BLOCK 2 twin) and
// consumed by the settle section's main step (0x46FDBB) as its timeStep -
// i.e. the settle steps only the REMAINDER of the frame's dt, not the full
// dt again.  Shared between PlaybackCatchup and PhysicsFrame (the original
// keeps it in one stack slot of sub_46B090).
extern float g_CatchupDtBudget;

// Angle-unit conversion pair used by the frame-driver edit boxes:
// an entered value is converted with degrees = value / PI * 180
// (the pi factor is the original's truncated double, not true pi).
extern double g_AngleDegreesScale;
extern double g_AnglePiTruncated;

// VA 0x0052B8F0 - mode-3 view-plane rotation scale (atan2 result multiplier).
// Current definition in frame_modes.cpp is 0.5; selected by the SelA3 drag mode.
extern double g_MouseScaleA;

}  // namespace mikudancestudio
