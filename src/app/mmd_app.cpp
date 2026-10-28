// VA 0x0054593C - the single `Block` application pointer.
// WinMain assigns it right after operator new; ported functions reach the
// state through mikudancestudio::g_Block exactly like the original global.
#include "mikudancestudio/mmd_app.hpp"

namespace mikudancestudio {

MMDApp* g_Block = nullptr;

} // namespace mikudancestudio
