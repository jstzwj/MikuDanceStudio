// ===========================================================================
// Standalone globals (definitions)
// ===========================================================================
#include "mikudancestudio/globals.hpp"

namespace mikudancestudio {

// VA 0x00529688 - L"%s%s"
const wchar_t g_SourceFormat[] = L"%s%s";

// VA 0x00529679 - ""
const char g_Locale[] = "";

// VA 0x0052B9F0 - 2^32
const float g_Wrap32 = 4294967296.0f;

const float g_FrameScale = 30.0f;

// var_14C8 of sub_46B090 - see globals.hpp.  Prefilled with a value that
// keeps PhysicsFrame stepping the full real dt when PlaybackCatchup has not
// run yet (matches the original's 0x46EFDE initialisation order).
float g_CatchupDtBudget = 0.0f;

}  // namespace mikudancestudio
