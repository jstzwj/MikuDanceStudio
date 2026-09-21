#pragma once

#include <Windows.h>
#include <cstdint>

namespace mikudancestudio {
class MMDApp;

// Focus is sampled before the panel traversal. SetFocus and synchronous
// commands may change live focus; later keys still consume this snapshot.
struct KeyboardInputContext {
    HWND focus{};
    bool focusNotInPanelEdit{};
};

void ConsumeKeyboardInput(MMDApp* app);
void ConsumeKeyboardInput(MMDApp* app, const KeyboardInputContext& input);
bool PumpPanelFocusChain(MMDApp* app, HWND focus);
void PumpInterpolationToggle(MMDApp* app, HWND focus, bool focusNotInPanelEdit);
void PumpDeleteRebuild(MMDApp* app, HWND focus, bool focusNotInPanelEdit);
void PumpTabCycle(MMDApp* app, HWND focus, bool focusNotInPanelEdit);
void PumpFullscreenKeys(MMDApp* app, HWND focus, bool focusNotInPanelEdit);
void PumpGlobalEnterRegister(MMDApp* app, HWND focus, bool focusNotInPanelEdit);
void PumpModelEnterRegister(MMDApp* app);
void PumpEscStop(MMDApp* app, bool focusNotInPanelEdit);
void ConsumeArrowKeyNavigation(MMDApp* app, const KeyboardInputContext& input);
void ConsumeNumpadViewPresets(MMDApp* app, const KeyboardInputContext& input);
void ConsumeRightButtonDrag(MMDApp* app);
void ConsumeMiddleButtonPan(MMDApp* app);

void FineShadowModeNotice(MMDApp* app);
void RefreshAfterFrameApply(MMDApp* app);
void RegisterCameraState(MMDApp* app, int frame);
void RegisterLightState(MMDApp* app, int frame);
void RegisterSelfShadowState(MMDApp* app, int frame);
void RegisterGravityKeyCurrent(MMDApp* app, std::int32_t frame);
}  // namespace mikudancestudio
