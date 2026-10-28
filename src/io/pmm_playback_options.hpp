#pragma once

#include <Windows.h>

namespace mikudancestudio {
class MMDApp;
namespace format {
struct PmmTimelineSettings;
}

void ApplyPmmPlaybackOptions(MMDApp* app, HWND main, const format::PmmTimelineSettings& timeline,
                             char* text);
void RestorePmmPlaybackOptions(MMDApp* app, HWND main, int fd, char* text,
                               unsigned char& playbackByte);
} // namespace mikudancestudio
