// ===========================================================================
// MikuDanceStudio - shared path-resolution workspace (app+0x9F338)
// ===========================================================================
#pragma once

#include <cstddef>

namespace mikudancestudio {

// The original keeps this scratch object inline in the application state.
// Calls at 0x408870, 0x4089F0 and 0x408E70 establish the three live paths.
struct PathResolutionWorkspace {
    wchar_t projectDirectory[256];
    wchar_t executableDirectory[256];
    unsigned char scratch[2000];
    wchar_t resolvedPath[256];
};

static_assert(offsetof(PathResolutionWorkspace, executableDirectory) == 512);
static_assert(offsetof(PathResolutionWorkspace, resolvedPath) == 3024);
static_assert(sizeof(PathResolutionWorkspace) == 3536);

} // namespace mikudancestudio
