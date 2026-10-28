#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include "pmm_playback_options.hpp"

#include <cstdio>

#include "mikudancestudio/mmd_app.hpp"
#include "mikudancestudio/panel_controls.hpp"
#include "pmm_io_common.hpp"

namespace mikudancestudio {

void ApplyPmmPlaybackOptions(MMDApp* app, HWND main, const format::PmmTimelineSettings& timeline,
                             char* text) {
    // Publish the reference before notifying controls. The typed PMM adapters
    // and the legacy stream entry point must update the same runtime member.
    app->state.cameraReferenceMode = timeline.cameraReferenceMode;
    const auto reference = app->CameraReferenceMode();
    SendMessageA(GetDlgItem(main, panel::kCameraRefModelCheckbox), BM_SETCHECK,
                 reference == CameraAttachmentReference::ModelRoot, 0);
    SendMessageA(GetDlgItem(main, panel::kCameraRefBoneCheckbox), BM_SETCHECK,
                 reference == CameraAttachmentReference::SelectedBone, 0);
    app->state.playbackLoopEnabled = timeline.playbackLoop ? 1 : 0;
    app->state.playbackReturnsToStartFrame = timeline.returnToStart ? 1 : 0;
    app->state.playbackStartsAtCurrentFrame = timeline.startAtCurrent ? 1 : 0;
    SendMessageA(GetDlgItem(main, panel::kPlaybackLoopCheckbox), BM_SETCHECK,
                 app->state.playbackLoopEnabled, 0);
    SendMessageA(GetDlgItem(main, panel::kPlaybackReturnCheckbox), BM_SETCHECK,
                 app->state.playbackReturnsToStartFrame, 0);
    SendMessageA(GetDlgItem(main, panel::kPlaybackFromCurrentCheckbox), BM_SETCHECK,
                 app->state.playbackStartsAtCurrentFrame, 0);
    const auto frameEdit = [&](int id, std::int32_t frame) {
        HWND item = GetDlgItem(main, id);
        SendMessageA(item, EM_SETSEL, 0, GetWindowTextLengthA(item));
        sprintf_s(text, 0x100, "%d", frame);
        SendMessageA(item, EM_REPLACESEL, 0, reinterpret_cast<LPARAM>(text));
    };
    frameEdit(panel::kPlayStartFrameEdit, timeline.playStartFrame);
    frameEdit(panel::kPlayStopFrameEdit, timeline.playStopFrame);
}

void RestorePmmPlaybackOptions(MMDApp* app, HWND main, int fd, char* text,
                               unsigned char& playbackByte) {
    format::PmmTimelineSettings timeline;
    // Preserve the native partial-read behavior of this stream entry point.
    timeline.cameraReferenceMode = app->state.cameraReferenceMode;
    pmm_io::Rd(fd, &timeline.cameraReferenceMode, 1);
    pmm_io::Rd(fd, &playbackByte, 1);
    timeline.playbackLoop = playbackByte;
    pmm_io::Rd(fd, &playbackByte, 1);
    timeline.returnToStart = playbackByte;
    pmm_io::Rd(fd, &playbackByte, 1);
    timeline.startAtCurrent = playbackByte;
    pmm_io::Rd(fd, &timeline.playStartFrame, 4);
    pmm_io::Rd(fd, &timeline.playStopFrame, 4);
    ApplyPmmPlaybackOptions(app, main, timeline, text);
}

} // namespace mikudancestudio
