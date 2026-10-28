#pragma once

#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <Windows.h>

namespace mikudancestudio {
class MMDApp;
void CopySelectedKeyframes(MMDApp* app, HWND owner);
void PasteSelectedKeyframes(MMDApp* app, HWND owner);
void PasteMirroredBoneKeyframes(MMDApp* app, HWND owner);
} // namespace mikudancestudio
