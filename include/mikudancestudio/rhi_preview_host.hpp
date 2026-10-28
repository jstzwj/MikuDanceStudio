#pragma once
namespace mikudancestudio {
// Entry before legacy editor/MME/DX9 bootstrap. Owns its CPU import/session and
// native window, and delegates all device/resource work to the shared layers.
int RunIndependentRhiPreview(int argc, wchar_t** argv);
}
