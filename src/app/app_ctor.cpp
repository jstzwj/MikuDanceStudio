// ===========================================================================
// VA 0x0042AE60 - MMDApp constructor  (original: sub_42AE60)
// ===========================================================================
// Original x86 construction only visits a no-op workspace constructor;
// WinMain then clears its 0xA4530-byte compatibility state. The port's
// AppRuntimeState initializes native pointers and workspaces separately on
// both architectures. Project defaults are still applied by InitDefaults.
// ===========================================================================
#include "mikudancestudio/mmd_app.hpp"

namespace mikudancestudio {

MMDApp::MMDApp() {
    // AppRuntimeState member initializers construct the process-local state.
}

} // namespace mikudancestudio
