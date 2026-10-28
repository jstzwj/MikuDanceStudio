#include "mikudancestudio/math_runtime.hpp"

#include <cmath>

namespace mikudancestudio {

void InitializeMathRuntime() {
#if defined(_M_X64)
    // MMD v9.32 x64 uses the VC90 SSE2 CRT. Modern UCRT dispatches some
    // trigonometric functions to FMA3 by default; their different rounding
    // changes contact/constraint trajectories over time.
    // This controls CRT dispatch, independently of compiler /fp settings.
    _set_FMA3_enable(0);
#endif
}

} // namespace mikudancestudio
