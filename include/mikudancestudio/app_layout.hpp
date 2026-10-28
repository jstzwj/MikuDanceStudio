// ===========================================================================
// MikuDanceStudio - the application state object
// ===========================================================================
// Hand-maintained.  Placeholders (v<off>/f<off>/pad*) are promoted to real
// names as field semantics are recovered; keep each member's size/arch
// split intact while doing so - the offset pins below are the safety net:
//
//   * x86: recovered members retain the offsets recorded in layout_pins.hpp.
//   * x64: anchors include lastRegisteredFrame (0x9F038), fpsLimit
//     (0xA1904) and the struct size (0xA55E0). Pins protect this port's
//     compatibility layout; they do not prove the semantics of every field.
//
// Process-local handles, callbacks, scratch, clipboard, selection and render
// workspaces live in AppRuntimeState on both architectures. Members prefixed
// reserved retain historical layout anchors only; production code must use
// MMDApp accessors. This record is not a PMM disk image. Unknown regions
// remain byte arrays rather than guessed application fields.
// ===========================================================================
#pragma once

#include <cstddef>
#include <cstdint>
#include <type_traits>

#include "mikudancestudio/clipboard_layout.hpp"
#include "mikudancestudio/bone_binding.hpp"
#include "mikudancestudio/path_workspace.hpp"
#include "mikudancestudio/raw_pad.hpp"

#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <Windows.h>

namespace mikudancestudio {

namespace mdl {
struct ModelRecord;
}

constexpr std::size_t kAppObjectSize = sizeof(void*) == 8 ? 0xA55E0 : 0xA4530;

// Model-slot capacity: the x64 recompile widened the model array from the
// x86 original's 100 (app+0x780) to 255 slots (x64 app+0xBE8).  Every walk
// of the slot array must use this bound - hardcoded 100s silently drop
// models loaded past slot 99 in the x64 build, and hardcoded 255s overrun
// into neighbouring state in the x86 build.
constexpr int kModelSlotCount = sizeof(void*) == 8 ? 255 : 100;

struct MMDAppState {
#if defined(_M_X64)
    RawPad<4> pad0inst; // Historical x86 instance slot; live handle is in PlatformRuntimeState.
#else
    void* reservedHInstance;
#endif
    std::int32_t mouseX;             // +0x04
    std::int32_t mouseY;             // +0x08
    std::int32_t previousMouseX;     // +0x0C
    std::int32_t previousMouseY;     // +0x10
    std::int32_t upKeyState;         // +0x14 (VK_UP)
    std::int32_t downKeyState;       // +0x18 (VK_DOWN)
    std::int32_t leftKeyState;       // +0x1C (VK_LEFT)
    std::int32_t rightKeyState;      // +0x20 (VK_RIGHT)
    std::int32_t shiftModifierState; // +0x24 (VK_SHIFT)
    std::int32_t spaceKeyState;      // +0x28 (VK_SPACE)
    std::int32_t escKeyState;        // +0x2C (VK_ESCAPE)
    // 0x30..0x78: dialog re-entry guards - nonzero while the matching
    // dialog is up; the menu command switch reads them to reject
    // re-entry.  The same ints double as letter key-down counters for
    // PollKey ('z x c v a b g s h i k' fill 0x30..0x60 - 'g' on slot 7,
    // 's' on slot 8, per the binary poll tables - 'p u j f r l' fill
    // 0x60..0x78); modal dialog pumps never run PollKey, so the
    // two roles never collide.
    // Shared letter-key edges and menu re-entry latches; values are not booleans.
    std::int32_t letterKeyState[18];
    std::int32_t keyState221;            // +0x78 (GetKeyState(221))
    std::int32_t keyState226;            // +0x7C (GetKeyState(226))
    std::int32_t tabKeyState;            // +0x80 (VK_TAB)
    std::int32_t leftMouseButtonState;   // +0x84
    std::int32_t rightMouseButtonState;  // +0x88
    std::int32_t middleMouseButtonState; // +0x8C
    std::int32_t numpadKeyState[10];     // +0x90..0xB8 (VK_NUMPAD0..9)
    std::int32_t deleteKeyState;         // +0xB8 (VK_DELETE)
    std::int32_t enterKeyState;          // +0xBC (VK_RETURN; was bC)
    std::int32_t ctrlModifierState;      // +0xC0 (VK_CONTROL)
    std::int32_t menuKeyState;           // +0xC4 (VK_MENU)
    unsigned char sidebarResizeDragging;
#if defined(_M_X64)
    RawPad<7> pad2;
#else
    RawPad<3> pad2;
#endif
    void* audioContext; // x64 pin 208
    wchar_t wavPath[256];
#if defined(_M_X64)
    RawPad<3> pad4;
#endif
    unsigned char directSoundAvailable;
#if defined(_M_X64)
    RawPad<4> pad5;
#else
    RawPad<3> pad5;
#endif
    HDC hdcMainPanel; // x64 pin 736
    void* bmpPanel;
    void* bmpPanelSpare;
    HDC hdcTimeline;
    void* bmpTimelineStrip;
    HDC hdcInterpCurve;
    void* bmpInterpCurve;
    void* bmpRes101; // x64 pin 792
    void* bmpRes119;
    unsigned char optflag[7]; // camera/edit UI radio flags (0x2F8..0x2FE)
    RawPad<1> pad21;
    void* groundGridVertices;
    void* groundGridIndices;
    float viewOffsetX;
    float viewOffsetY;
    float cameraPitch;
    float cameraYaw;
    float cameraRoll;
    unsigned char cameraPerspective;
    unsigned char groundGridEnabled;
    unsigned char fpsOverlayEnabled;
    RawPad<1> pad31;
    float fpsOverlayElapsedSeconds;
    std::uint32_t fpsOverlayFrameCount;
    std::uint32_t framesPerSecond;
    // +0x32C: gizmo row filter (0 = both axis rows active, 1 = upper
    // only, 2 = lower only; frame_modes/sprite_overlay).  No writer
    // exists in either original - read-only zero; semantics unrecovered.
    std::uint32_t v32c;
    unsigned char playbackActive;
    RawPad<3> pad36;
    float cameraPosition[3];
    unsigned char cameraReferenceMode;
    unsigned char playbackLoopEnabled;
    unsigned char playbackReturnsToStartFrame;
    RawPad<1> pad42;
    std::uint32_t viewportToolHovered;   // +0x344
    std::uint32_t viewToolDragOperation; // +0x348
    std::uint32_t interactionDragMode;   // +0x34C
    // Retired clipboard slots. ClipboardRuntimeState owns the live counts
    // and typed pointers on both builds; these slots preserve binary offsets.
    void* reservedBoneCopyRecords; // +0x350 (was v350Clipboard)
#if defined(_M_X64)
    // Preserve the historical boundary; live pointers are in ClipboardRuntimeState.
    RawPad<24> pad46;
#else
    void* reservedBoneClipboard;  // +0x354
    void* reservedMorphClipboard; // +0x358
#endif
    void* reservedDisplayClipboard; // x64 pin 936
#if defined(_M_X64)
    RawPad<32> pad47;
#else
    void* reservedCameraClipboard;    // +0x360
    void* reservedLightClipboard;     // +0x364
    void* reservedShadowClipboard;    // +0x368
    void* reservedGravityClipboard;   // +0x36C
    void* reservedAccessoryClipboard; // +0x370
#endif
    void* cameraKeyTrack;                          // x64 pin 976
    void* lightKeyTrack;                           // x64 pin 984
    void* selfShadowKeyTrack;                      // x64 pin 992
    void* gravityKeyTrack;                         // x64 pin 1000
    void* accKeyTracks[255];                       // x64 pin 1008
    mdl::ModelRecord* modelSlots[kModelSlotCount]; // x86 pin 1920 / x64 pin 3048
    unsigned char slotIdx;                         // x64 pin 5088
    RawPad<3> pad54;
    std::int32_t editMode;
    unsigned char groundShadowEnabled;
    RawPad<3> pad56;
    std::uint32_t aviBackgroundEnabled;
    std::int32_t viewportToolCenterX; // +0x920
    std::int32_t viewportToolCenterY; // +0x924
    float selectedClipW;              // +0x928 (selected bone's clip-space
                                      //  depth; scales mouse drags)
    std::uint32_t viewportToolOperation;
    std::int32_t dragOriginX;
    std::int32_t dragOriginY;
    std::int32_t boneBoxStartX; // +0x938
    std::int32_t boneBoxStartY; // +0x93C
    std::int32_t boneBoxSelectionActive;
    std::int32_t scrollCbSize;
    std::int32_t scrollFMask;
    std::int32_t scrollNMin;
    std::int32_t scrollNMax;
    std::int32_t scrollNPage;
    std::int32_t scrollNPos;
    RawPad<20> pad67;
    std::int32_t timelineScrollNPage; // +0x970 timeline scrollbar cached nPage
                                      //  (x64 app+0x1440, nPage in PanelPaint's embedded
                                      //  SCROLLINFO; read back by HScroll cases 2/3)
    std::int32_t timelineScrollNPos;  // +0x974 timeline scrollbar cached nPos
                                      //  (x64 app+0x1444, HScroll case 5 reads back
                                      //  nPos rather than nMin)
    RawPad<4> pad67b;
    std::int32_t timelineStartFrame;
    std::int32_t currentFrame;
    // Three independent 200 x 200 hit grids. Older reconstruction split
    // their storage into 200-element heads and unrelated model-field names;
    // those names had no App consumers and were not real application fields.
    std::int32_t rowHitBone[40000];  // x64 5204
    std::int32_t rowHitMorph[40000]; // x64 165204
    std::int32_t rowHitIk[40000];    // x64 325204
    std::int32_t rowHitBand0[200];   // x64 pin 485204
    std::int32_t rowHitBand1[200];   // x64 pin 486004
    std::int32_t rowHitBand2[200];   // x64 pin 486804
    std::int32_t rowHitBand3[200];   // x64 pin 487604
    // Full 200x200 accessory hit grid (0x76904).  Promoted from [800] +
    // RawPad<156800>: byte-identical on both architectures (800*4 + 156800
    // == 40000*4), and member indexing lands elements >= 800 in the
    // reserved tail on x64 too, where the old offset path lost the xlate.
    std::int32_t rowHitAcc[40000]; // x64 pin 488404
    // 0x9DA04..0x9DA0A: interp-curve editor panel state (uniform-curve
    // found flag + the cached control-point pair, y as 127-complement)
    unsigned char interpCurveUniformFound;    // +0x9DA04
    unsigned char interpCurveControlCache[4]; // +0x9DA05..08
    unsigned char draggedInterpolationPoint;
    unsigned char lightA[6];
    unsigned char lightB[6];
    unsigned char lightC[6];
    unsigned char lightD[6];
    RawPad<2> pad100;
    // 0x9DA24: bone-copy count shared by copy/paste (496 gates and updates
    // it per emitted record; 497/498 gate on it), then the clipboard
    // selection counts overlay at +0x9DA28 (was one v9da24[9] array).
    std::int32_t reservedCopiedBoneCount;                  // +0x9DA24
    mdl::ClipboardSelectionCounts reservedClipboardCounts; // +0x9DA28
    std::int32_t displayObjectListScrollPosition;
    std::int32_t displayObjectListMatchCount;
    std::int32_t jointLineMap[200]; // x64 pin 648480
    // 255-entry display-object list (was buf9ddx): AccessoryRecord slots
    // plus model-timeline-selected objects (x64 pin 649280)
    void* objectSlots[255];
    std::uint32_t lastRegisteredFrame; // x64 pin 651320
    // shared accessory/model-display list selection byte (was
    // selLightAccSlotOrUint32)
    unsigned char selectedObjectSlot;
    RawPad<3> pad115;
    // Retired PMM light direction (+0x9E174); live values are in RenderRuntimeState.
    float reservedLightDirection[3];
    // Retired device-light region. In the original it begins after the PMM
    // direction and spans 104 bytes, with Ambient.rgb at lightColor below.
    // RenderRuntimeState now stores the complete D3DLIGHT9 on both builds.
    RawPad<36> pad118;
    // Retired Ambient.rgb / PMM RGB slot (+0x9E1A4; x64 +0x9F070).
    // LightColor() returns the live SceneLight().Ambient.rgb on both builds.
    float reservedLightColor[3];
    RawPad<28> pad121;
    std::uint32_t sceneLightRange; // D3DLIGHT9::Range overlay word (+0x9E1CC)
    RawPad<24> pad122;
    float cameraFov;                // x64 pin 651444 (0x9F0B4)
    wchar_t aviBackgroundPath[256]; // x64 pin 651448 (0x9F0B8, the AVI
                                    // open dialog writes 0x100 wchars here)
#if defined(_M_X64)
    RawPad<8> pad126; // x64 rounds the path buffer up to the pointer grid;
                      // drawDib sits at 651968 (0x9F2C0)
#endif
    void* drawDib;
    void* aviBackgroundTexture;
    void* aviBackgroundSurface;
#if !defined(_M_X64)
    std::uint32_t reservedAviOverlayVertices;
#endif
    // No AVI vertex-buffer slot in this x64 compatibility layout:
    // aviBackgroundSurface (0x9F2D0) is followed by aviFile (0x9F2D8).
    // Live overlay buffers belong to RenderRuntimeState on both builds.
    void* aviFile;
    void* aviStream; // PAVISTREAM (was v9e400OrUint32)
    void* aviFrameReader;
    std::int32_t aviStreamStart;
    std::int32_t aviStreamEnd;
    unsigned char aviUsesThirtyFpsTiming;
    RawPad<3> pad134;
    std::int32_t aviOffsetX;
    std::int32_t aviOffsetY;
    float aviScale;
    std::int32_t aviFrameWidth;
    std::int32_t aviFrameHeight;
    unsigned char pictureBackgroundEnabled;
#if defined(_M_X64)
    RawPad<7> pad140;
#else
    RawPad<3> pad140;
#endif
    void* pictureBackgroundTexture;
#if defined(_M_X64)
    // Historical vertex-buffer pointer slot (0x9F320); live storage is
    // RenderRuntimeState::pictureOverlayVertices on both builds.
    RawPad<8> pad129;
#else
    std::uint32_t reservedPictureOverlayVertices;
#endif
    std::int32_t pictureOffsetX;
    std::int32_t pictureOffsetY;
    float pictureScale;
    std::int32_t pictureWidth;
    std::int32_t pictureHeight;
    wchar_t pictureBackgroundPath[256];
    std::int32_t aviBackgroundSample;
    float playbackCursorSeconds;
    float keyRepeatTimer;
    float playbackStartSeconds;
    float playbackEndSeconds;
    // Track-key cursors (0x9E65C..): the camera/light/self-shadow/gravity
    // play-cursor dwords with their active bytes, then the 55 accessory
    // track cursors, then the 55 accessory active bytes.  In-blob on both
    // arches (x64 pins: camera cursor 0x9F550, accessory cursors 0x9F570,
    // accessory active bytes 0x9F96C).
    std::uint32_t cameraTrackCursor; // +0x9E65C / x64 pin 652624
    std::uint8_t cameraTrackActive;  // +0x9E660
    RawPad<3> pad153a;
    std::uint32_t lightTrackCursor; // +0x9E664
    std::uint8_t lightTrackActive;  // +0x9E668
    RawPad<3> pad153b;
    std::uint32_t shadowTrackCursor; // +0x9E66C
    std::uint8_t shadowTrackActive;  // +0x9E670
    RawPad<3> pad153c;
    std::uint32_t gravityTrackCursor; // +0x9E674
    std::uint8_t gravityTrackActive;  // +0x9E678
    RawPad<3> pad153d;
    std::uint32_t accessoryTrackCursor[55];        // +0x9E67C..0x9E938 / x64 652656
    RawPad<800> pad153e;                           // 0x9E938..0x9EA78 / x64 ..0x9F96C
    std::uint8_t reservedAccessoryTrackActive[55]; // +0x9EA78 / x64 pin 653676
#if defined(_M_X64)
    RawPad<199> pad153; // ..up to the 0x9FA6A snapshot
#else
    RawPad<200> pad153; // ..0x9ED97
#endif
    // IsWindowEnabled snapshots of the 7 main-window playback controls
    // (0x1F1/0x1F2/0x1AF/0x1A5/0x1A6/0x190/0x191), restored after playback
    unsigned char playbackEnabledSnapshot[7]; // x64 pin 653930 (0x9FA6A)
    unsigned char characterTransparentMode;   // x64 653937
    unsigned char blinkPhase;                 // x64 pin 653938 (0x9FA72)
#if defined(_M_X64)
    RawPad<5> pad155; // captureTexture is the next 8-aligned slot
#endif
    void* captureTexture; // x64 pin 653944 (0x9FA78)
    std::uint32_t captureMode;
#if defined(_M_X64)
    RawPad<4> pad157;
#endif
    void* captureRenderTarget;
    void* captureSystemSurface;
#if defined(_M_X64)
    // x64 blob reserves the full 512-byte path buffer (0x9FA98, the AVI
    // record dialog leas it); the port's copy lives outside the compat
    // blob (see CaptureRuntimeState::aviOutputPath)
    RawPad<512> pad159;
#else
    wchar_t reservedAviOutputPath[256];
#endif
    unsigned char frameStepPlayback; // x64 pin 654488 (0x9FC98)
    RawPad<3> pad160;
    std::int32_t recordedFrameCount;
    unsigned char followCameraEnabled;
    unsigned char playbackStartsAtCurrentFrame; // +0x9ED99
    unsigned char projectedShadowBlendEnabled;  // +0x9ED9A
    RawPad<1> pad164;
    std::int32_t coordinateSystem; // x64 pin 654500 (0x9FCA4)
    void* axisMeshObject;          // AccessoryRecord slot holding the axis gizmo
                                   // X-file mesh (was sub04b0OrUint32) - x64 pin
                                   // 654504 (0x9FCA8); no pad165 on x64, the pointer
                                   // is already 8-aligned right after the dword
#if !defined(_M_X64)
    RawPad<4> pad166;
#endif
    std::uint32_t playbackClockAnchorLow;  // +0x9EDA8
    std::uint32_t playbackClockAnchorHigh; // +0x9EDAC
    void* physicsScene;
    unsigned char physicsEditorJointPage;
    unsigned char physicsResetPending;
    unsigned char playbackFrameChanged; // +0x9EDB6
    RawPad<1> pad172;
    float gravityX;
    float gravityY;
    float gravityZ;
    float gravityMagnitude;
    std::uint32_t gravityNoise;
    float gravityNoiseTimer;
    unsigned char timelineAdvanceDue;  // +0x9EDD0
    unsigned char viewportInputActive; // +0x9EDD1
    RawPad<2> pad180;
    void* recordingCompletionFlag;
    unsigned char recordPlaybackStartPending; // +0x9EDD8
    RawPad<3> pad182;
    std::int32_t recordSavedFrame;
    void* toonTextures[11];
    void* spriteOverlayVertices;
    std::uint32_t spriteOverlayPrimitiveCount;
#if defined(_M_X64)
    RawPad<4> pad186;
#endif
    void* overlayTexture;
    void* overlayVertices;
    std::uint32_t textOverlayPrimitiveCount;
#if defined(_M_X64)
    RawPad<4> pad189;
#endif
    void* sceneFontTexture;
    unsigned char recentFile0[256];
    unsigned char recentFile1[256];
    unsigned char recentFile2[256];
    std::uint32_t lineOverlayPrimitiveCount;
#if defined(_M_X64)
    RawPad<4> pad194;
#endif
    void* groundPlaneVertices;
    unsigned char separateWindowMouseSeen;
#if defined(_M_X64)
    RawPad<7> pad196;
#else
    RawPad<3> pad196;
#endif
    void* projectedShadowRestoreTexture;
#if defined(_M_X64)
    // x64: the render-save path buffer lives outside the blob
    // (CaptureRuntimeState::savePath); the blob still reserves the full
    // 512-byte slot (0xA0098..0xA0297)
    RawPad<512> pad197;
#else
    // render/screenshot save path chosen by the render dialog (menu 276)
    // and consumed by the capture save ladder
    wchar_t reservedCaptureSavePath[256];
#endif
    void* captureReadbackPixels;
#if defined(_M_X64)
    // x64: the path-resolution workspace lives outside the blob
    // (AppRuntimeState::paths); the blob reserves the same 3536 bytes
    // as x86 (captureReadbackPixels 0xA0298 .. leftViewportVertices
    // 0xA1070).
    RawPad<3536> pad199; // was fontSubOrPtr + pad199
#else
    // +0x9F338: inline path-resolution scratch (project/exec dirs +
    // resolved path), overlaid as one blob region in the original.
    PathResolutionWorkspace reservedPathWorkspace;
#endif
    void* leftViewportVertices;
    void* rightViewportVertices;
    float toonEdgeTable[30];
    unsigned char selfShadowEnabled;
    unsigned char selectionBoxDragging;
    RawPad<2> pad204;
#if defined(_M_X64)
    // the x64 blob keeps the two selection-box anchors (0xA10FC/0xA1100);
    // the port works on AppRuntimeState on both builds and reserves the slots here
    RawPad<8> pad205;
#else
    std::int32_t reservedSelectionBoxAnchorX; // +0xA018C
    std::int32_t reservedSelectionBoxAnchorY; // +0xA0190
#endif
    unsigned char blackBackgroundEnabled; // x64 pin 659716 (0xA1104)
    unsigned char modelNonDisplayMode;
    unsigned char wavPlaysOnFrameMove; // x64 pin 659718
    unsigned char floorVisible;        // x64 pin 659719 (default 1)
    std::int32_t modelOutlineColorRed;
    std::int32_t modelOutlineColorGreen;
    std::int32_t modelOutlineColorBlue;
    // ChooseColor custom-colours table (64 bytes; the trailing
    // RawPad<16> pad209 completes the 16 COLORREFs) - was buf655780
    unsigned char customColorTable[48];
    RawPad<16> pad209; // +0xA01D4: unreferenced in both originals
    // +0xA01E4 (x64 twin +0xA1154, written/read 6+6 sites): the "wire frame"
    // menu toggle (command 287).  Both render frames gate D3DRS_FILLMODE on
    // this single byte - fixed sub_7FF7CB4BFB20 (3 reads) and effect
    // sub_7FF7CB4C1E60 (3 reads).
    unsigned char wireframeRenderingEnabled; // x64 pin 659796 (0xA1154)
#if defined(_M_X64)
    // the x64 blob carries BOTH projection matrices in full
    // (lightViewProjection 0xA1158, worldViewProjection 0xA1198); the
    // port works on AppRuntimeState on both builds and reserves the 128 bytes here
    RawPad<131> pad213;
#else
    RawPad<3> pad213a;
    // 0xA01E8/0xA0228: the fixed-function light and world view-projection
    // matrix slots (16 floats each). Live D3DMATRIX values now belong to
    // RenderRuntimeState; these float arrays retain only the old widths.
    float reservedLightViewProjectionMatrix[16]; // +0xA01E8
    float reservedWorldViewProjectionMatrix[16]; // +0xA0228
#endif
    void* activeRenderObject;      // x64 pin 659928 (0xA11D8)
    std::int32_t activeRenderPass; // x64 pin 659936
#if !defined(_M_X64)
    std::int32_t renderPassCount;
#endif
    unsigned char fullscreenMode; // +0xA0274 / x64 pin 659940 (0xA11E4) -
                                  // the x64 blob has no renderPassCount
                                  // slot (fullscreenMode moved up to the
                                  // x86 count's twin); the count parks in
                                  // the placement region below
    RawPad<3> pad217;
    HMENU savedMenu; // x64 pin 659944 (0xA11E8)
#if defined(_M_X64)
    std::int32_t renderPassCount; // Uses the compatibility placement slot
                                  // (0xA11F0). Live placement belongs to
                                  // PlatformRuntimeState::savedPlacement.
    RawPad<40> pad219;
#else
    // 0xA027C: the window placement saved across fullscreen recording
    // (GetWindowPlacement/SetWindowPlacement; .length is init 44)
    WINDOWPLACEMENT reservedSavedPlacement;
#endif
    unsigned char fullscreenFlagsSaved; // x64 pin 659996 (0xA121C)
    RawPad<3> pad220;
    std::int32_t recRTW;
    std::int32_t recRTH;
    unsigned char recordFullscreenActive;
    unsigned char stereoActivated;
    unsigned char a02B6;
    unsigned char sjisOut[256];
    unsigned char timelineAdvanceRequested; // +0xA03B7
    unsigned char depthDeviceEnabled;
    RawPad<3> pad228;
    HMODULE oniModule;
    void* reservedOniExportSlot0;
    void* reservedOniExportSlot1;
    std::uint32_t reservedOniExportSlot2;
#if defined(_M_X64)
    RawPad<4> pad232;
#endif
    void* reservedDepthTextureCallback;
    std::uint32_t reservedOniExportSlot4;
#if defined(_M_X64)
    // the blob keeps the 8-byte OpenNI tracking-callback slot (0xA1360)
    // plus its alignment fill; the port reads PlatformRuntimeState::openNi.isTracking
    RawPad<12> pad231;
#else
    void* reservedOpenniTrackingCallback; // ?OpenNIIsTracking@@YGXPA_N@Z (registered
                                          // by the OpenNI plugin; read by the frame
                                          // driver's selection-callback branch)
#endif
    void* reservedOniExportSlot6;
    unsigned char kinectMirrorEnabled;
    unsigned char kinectInitLostBone;
    unsigned char depthTextureCompositionEnabled;
    unsigned char kinectCaptureActive;
    float fpsLimitSaved; // x86 +0xA03D0 / x64 pin 660340 (0xA1374; the
                         // pump tail 0x7FF7CB44C643 restores fpsLimit from
                         // here when Kinect capture ends)
    // +0xA03E4: the four global-track (camera/light/self-shadow/gravity)
    // timeline row-selected flags (was a03E4..a03E7); the original walks
    // them as four consecutive bytes (0x441097 / 0x472C5F).
    unsigned char globalTrackSelected[4];
    // +0xA03E8 / x64 0xA137C: latched selActive from the previous pump iteration. The x64 pump tail
    // at 0x7FF7CB456F21 stores this iteration's var_1784 (selActive from the OpenNI selection callback)
    // here; app initialization at 0x7FF7CB42CA4C clears it. The physics frame at 0x46F7FF /
    // x64 0x7FF7CB44C12E requests settling only when it is zero (previously inactive; rising edge).
    unsigned char selectionActiveLatch;
    unsigned char automaticFrameAdvanceEnabled;
    unsigned char openniVersion;
    unsigned char timelineSelectionChanged;
    // Retired eight timeline count/pointer pairs: x86 stride 8 (64 bytes),
    // original x64 stride 16 (128 bytes). SelectionRuntimeState owns the
    // live native pairs; no global side table or compact-slot overlay remains.
    std::int32_t reservedTimelineSelectionSlots[16]; // x64 pin 660352 (0xA1380)
#if defined(_M_X64)
    RawPad<64> pad248; // the x64 record-pointer halves of the slots above
#endif
    std::uint32_t mainModelComboSelection;
    std::int32_t cameraParentModel;
    std::int32_t cameraParentBone;
    // 16-float basis/colour matrix (0xA0438..0xA0478); rows 0/2/4/6 of the
    // original camera-attachment basis default to identity diag 1.0
    float cameraAttachmentBasis[16];
    unsigned char cameraAttachmentTransformSuppressed; // +0xA0478 / x64
                                                       // pin 660556
    RawPad<3> pad269;
    unsigned char logFont[60];        // x64 pin 660560
    unsigned char modelReloadPending; // +0xA04B8 / x64 pin 660620
    RawPad<3> pad271;
    HFONT hFontUI; // x64 pin 660624 (0xA1490)
    float eulerX;
    float eulerY;
    float eulerZ;
    std::uint32_t brushes[11];
#if defined(_M_X64)
    // the x64 blob carries eleven 8-byte brush handles (0xA14A4..0xA14FB);
    // the port stores the low-dword handles above and reserves the rest
    RawPad<48> pad276;
#endif
    // Historical 200-byte panel flag grid (+0xA04F8; x64 +0xA1500).
    // InitDefaults/ResetAppState clear 0xC8 bytes in the original. Live flags
    // are in SelectionRuntimeState, with room for joint rows [4..203].
    // They no longer overlap the separately named timeline range anchors.
    unsigned char reservedBuf656632[200]; // x86 +0xA04F8 / x64 pin 660736 - the
                                          // panel-row flag grid, memset-0xC8 twin
                                          // at 0xA1500 in the x64 init
#if defined(_M_X64)
    RawPad<16> pad277; // timeline range lives in AppRuntimeState on both builds here
                       // (blob slots 0xA15C8..0xA15D7)
#else
    // 0xA05C0..0xA05D0: the frame-range selection anchors (menu 262's
    // first/last offset+base pairs)
    std::int32_t reservedTimelineRangeFirstOffset; // +0xA05C0
    std::int32_t reservedTimelineRangeFirstBase;   // +0xA05C4
    std::int32_t reservedTimelineRangeLastOffset;  // +0xA05C8
    std::int32_t reservedTimelineRangeLastBase;    // +0xA05CC
#endif
    unsigned char reservedTimelineRangeApplyEnabled;
    // +0xA05D1: view-dirty flag - panel repaint picks the busy palette
    // while set (original 0x42C574); armed by the camera-follow refresh.
    unsigned char viewDirty;
    // +0xA05D2: written by the morph-follow refresh path, read by the
    // pump's camera-follow gate (original 0x473D07); the port has no
    // reader ported yet - exact semantics unrecovered.
    unsigned char b6568482;
    // +0xA05D3: pointer jumped >50px (separate window / warped cursor) -
    // the pump's re-center step is skipped while set (original 0x47527C).
    unsigned char mouseJumped;
    unsigned char uiTextRed;
    unsigned char uiTextGreen;
    unsigned char uiTextBlue;
    RawPad<1> pad284;
    // 35-entry UI theme colour table (0xA0C98..0xA0CDC)
    std::uint32_t themeColors[35];
#if defined(_M_X64)
    RawPad<1> pad319; // accessory-apply gate byte lives in the MMDApp
                      // DialogRuntimeState::accessoryApplyGate (blob slot 0xA166C)
#else
    // accessory-edit dialog close gate: the post-close refresh runs
    // only while nonzero
    unsigned char reservedAccessoryApplyGate;
#endif
    unsigned char accessoryEditDialogOpen; // was a0665 (menu 442) - x64 pin
                                           // 661101 (0xA166D)
    RawPad<2> pad320;
    mdl::BoneOrderEntry* reservedSelectNavRecords; // Retired dialog 442 pointer slot.
                                                   // (was a0668OrUint32)
    unsigned char physicsBodiesMoved;
    unsigned char playbackAlwaysOnOffMode;
    RawPad<2> pad323;
    std::int32_t savedPlaybackPhysicsMode;
#if defined(_M_X64)
    // the x64 blob carries the full 64-byte view-rotation matrix
    // (0xA1680..0xA16BF); the port works on the AppRuntimeState on both builds
    RawPad<64> pad324;
#else
    // +0xA0674 view-rotation matrix (D3DMATRIX)
    float reservedViewRotationTransform[16];
#endif
    // +0xA06B4..0xA06B6: separate-window hide-margin mouse latches -
    // armed while the pointer sits outside the auto-hide margins (and by
    // viewport-tool drags), cleared once it crosses back in (mic_window /
    // ui_mousemove).  The three bytes' margin mapping differs between the
    // main and separate windows, so no per-byte rename is made.
    unsigned char a06B4;
    unsigned char a06B5;
    unsigned char a06B6;
#if defined(_M_X64)
    RawPad<5> pad327;
#else
    RawPad<1> pad327;
#endif
    HWND hwnd;
    float deltaTime;
#if defined(_M_X64)
    RawPad<4> pad329;
#endif
    void* recorder; // DShowRecorder (was sub06c)
    void* renderer; // D3DRenderer "0x1D574 object"; ConvertAnsiToWide
                    // also reads it as a locale table (was
                    // rendererOrLocaleTable)
    std::int32_t sidebarWidth;
    unsigned char waveEnabled;
    RawPad<1> pad333;
    wchar_t exeDir[256];
    RawPad<2> pad334;
    void* reservedOrigEditProc;
    std::int32_t renderW;
    std::int32_t renderH;
    float cameraDistance; // x64 pin 661760 (0xA1900, init -50.0f)
    float fpsLimit;       // x86 +0xA08E0 / x64 pin 661764 (0xA1904; init 60.0f,
                          // PMM round-trips it, the frame driver sleeps on 1/f)
    // model-center offset (menu 219, "model-offset" dialog): applied to
    // every root-bone keyframe position on OK
    float modelOffsetX;
    float modelOffsetY;
    float modelOffsetZ;
#if !defined(_M_X64)
    std::int32_t reservedFrameRangeStartFrame; // +0xA08F0 frame-range dialog start
    std::int32_t reservedMorphFrameShift;      // menu 225 morph-frame cleanup shift
    std::int32_t reservedBlinkStartFrame;      // menu 227 blink register range start
    std::int32_t reservedBlinkEndFrame;        // menu 227 blink register range end
#else
    RawPad<16> pad289; // the four frame-range/morph/blink ints live in
                       // AppRuntimeState on both builds (blob slots 0xA1914..0xA1920)
#endif
    wchar_t envFileName[256];
    std::int32_t aviRecordStartFrame;
    std::int32_t aviRecordEndFrame;
    float aviRecordFps;
    unsigned char aviIncludeWave;
    unsigned char sceneModified;
    RawPad<2> pad348;
    // +0xA0B10: sample bias added to the wall-clock AVI sample pick
    // during playback - read-only zero in both originals (no writer
    // ported); semantics unrecovered.
    std::uint32_t a0B10;
#if defined(_M_X64)
    // blob slots for the ground-shadow-color dialog (0xA1B38), its saved
    // edit proc (0xA1B40) and the reorder array (0xA1B48); the port keeps
    // dialog handles in AppRuntimeState on both builds and typed scratch ownership in DialogOrders
    RawPad<24> pad349;
#else
    HWND reservedGroundShadowColorDialog;      // menu 248, modeless
    WNDPROC reservedGroundShadowColorEditProc; // saved wndproc of its value edit
    RawPad<4> accessoryOrderArray;             // retired scratch slot; typed owners in MMDApp
#endif
    std::int32_t accessoryRenderSplitOrder; // x64 pin 662352 (0xA1B50,
                                            // init 1)
#if defined(_M_X64)
    // blob slots for the accessory-edit array (0xA1B58) and the rotation
    // dialog temps (0xA1B60..0xA1B7B); AppRuntimeState on both builds own the data
    RawPad<40> pad357;
#else
    // accessory-edit dialog scratch buffer 2 (menu 442; freed on close)
    void* reservedAccessoryEditArray;
    // rotation-dialog working values, shared by the dialog procs
    // sub_40FBC0 / sub_40F860 (cases 300/302)
    float reservedRotationDialogTemp[7];
#endif
    HWND reservedEdgeThicknessDialog; // menu 253 edge-thickness dlg, modal
                                      // (was modelInfoDialog) - x64 pin 662400
#if defined(_M_X64)
    RawPad<8> pad354; // saved edit wndproc lives in DialogRuntimeState (0xA1B88)
#else
    WNDPROC reservedEdgeThicknessEditProc; // saved wndproc of its edit 646
                                           // (was modelInfoEditProc)
#endif
    unsigned char englishUI; // x64 pin 662416 (0xA1B98; the x64E build
                             // inits it to 1)
#if defined(_M_X64)
    RawPad<7> pad353; // reservedEnglishNameDialog needs the full 8-alignment here
#else
    RawPad<3> pad353;
#endif
#if defined(_M_X64)
    // blob slots: English-name dialog HWND (0xA1BA0), English-name edit
    // proc (0xA1BA8) and the three combo cursors (0xA1BB0..0xA1BBB);
    // the port works on AppRuntimeState on both builds
    RawPad<8> pad356;
    RawPad<20> pad358;
#else
    HWND reservedEnglishNameDialog;
    WNDPROC reservedEnglishNameEditProc;            // saved wndproc of edit 667
    std::int32_t reservedEnglishNameComboCursor[3]; // combos 669/673/677 edit pos
#endif
    unsigned char enhancedModelDirty; // x64 pin 662452 (0xA1BB4)
    RawPad<3> pad355;
    std::uint32_t timeNowLow;
    std::uint32_t timeNowHigh;
    float milliToSec;
    HWND reservedFrameCopyDialog; // menu 262, modal - x64 pin 662472 (0xA1BC8)
#if defined(_M_X64)
    RawPad<8> pad361; // saved edit wndproc lives in DialogRuntimeState (0xA1BD0)
#else
    WNDPROC reservedFrameCopyEditProc; // saved wndproc of edit 705
#endif
    // physics editor (dialog 684) rigid-body scratch, RigidRecord x
    // 100000 (was cameraRecordArray); index selectedRigidIndex (was selAcc)
    void* reservedRigidScratchArray;
#if defined(_M_X64)
    // the x64 blob reserves the physics-editor staging block
    // (0xA1BE0..0xA1C9F, 192 bytes); the port's copy lives in
    // DialogRuntimeState::cameraFrameScratch
    RawPad<192> pad360;
#else
    // dialog 262 stages a 172-byte camera record here for editing; the
    // ground-shadow-color RGBA overlays bytes +112..+127 (menus 248/262
    // are never open together, mirroring the original blob reuse)
    unsigned char reservedCameraFrameScratch[172];
#endif
    std::int32_t selectedRigidIndex; // was selAcc - x64 pin 662688 (0xA1CA0)
    // physics editor joint scratch, JointRecord x 100000 (was
    // boneRecordArray); index selectedJointIndex (was sel8c)
    void* reservedJointScratchArray; // x64 pin 662696 (0xA1CA8)
#if defined(_M_X64)
    // the x64 blob reserves the bone/joint staging block
    // (0xA1CB0..0xA1D47, 152 bytes); the port's copy lives in
    // DialogRuntimeState::boneFrameScratch
    RawPad<152> pad364;
#else
    // dialog 262 stages a 140-byte bone record here for editing; the
    // accessory-frame dialog reuses byte +28 as its saved wndproc
    unsigned char reservedBoneFrameScratch[140];
#endif
    std::int32_t selectedJointIndex;       // was sel8c - x64 pin 662856 (0xA1D48)
    std::int32_t playbackPhysicsMode;      // x64 pin 662860 (0xA1D4C, init 2)
    unsigned char rigidBodyDisplayEnabled; // x64 pin 662864 (0xA1D50)
#if defined(_M_X64)
    // x64 blob slots: the alignment fill plus the gravity-setting dialog
    // (0xA1D58) and the gravity-setting edit proc (0xA1D60); the port
    // keeps both in AppRuntimeState on both builds
    RawPad<23> pad367;
#else
    RawPad<3> pad367;
    HWND reservedGravitySettingDialog;      // 0xA0CCC, gravity-setting dlg 266
                                            // (was accessoryFrameDialog)
    WNDPROC reservedGravitySettingEditProc; // saved wndproc of edit 709
#endif
    unsigned char gravityNoiseEnabled;
#if defined(_M_X64)
    // x64: the avi codec option lives outside the compat blob
    // (CaptureRuntimeState::aviCodecSelection); the blob keeps its dword slot
    // (0xA1D6C) plus the fill up to reservedOrigTrackProc (0xA1D70)
    RawPad<7> pad368;
#else
    RawPad<3> pad368;
    std::int32_t reservedAviCodecSelection;
#endif
    void* reservedOrigTrackProc;
    unsigned char aviSettings;
#if defined(_M_X64)
    RawPad<3> pad370;
#else
    RawPad<11> pad370;
#endif
    float projectedShadowDiffuseAlpha;
    float projectedShadowAmbientIntensity; // +0xA0CF0
    float projectedShadowAmbientG;
    float projectedShadowAmbientB;
    float projectedShadowAmbientA;
    RawPad<12> pad375;
    float projectedShadowSpecularAlpha;
#if defined(_M_X64)
    RawPad<30> pad376; // stretches the block up to recordingWindow
                       // (x64 pin 662976 / 0xA1DC0)
#else
    RawPad<20> pad376;
#endif
    HWND recordingWindow;                       // x64 pin 662976
    unsigned char selfShadowCompositionEnabled; // x64 pin 662984
    RawPad<3> pad378;
    float selfShadowDistance; // x64 pin 662988 (0xA1DCC, init 0.01125f)
    std::int32_t selfShadowMode;
#if defined(_M_X64)
    RawPad<12> pad380; // x64 floatingWindow lands on the next 8-grid
#else
    RawPad<4> pad380;
#endif
    HWND floatingWindow; // +0xA0D38 / x64 pin 663008 (0xA1DE0)
    std::int32_t separateWindowSidebarWidth;
    std::int32_t hideRight;
    std::int32_t hideTop;
    std::int32_t hideLeft;
    std::int32_t hideBottom;
    std::int32_t separateWindowX;
    std::int32_t separateWindowY;
    std::int32_t separateWindowWidth;
    std::int32_t separateWindowHeight;
    unsigned char separateWindowMaximized;
    unsigned char aviStereoOutput; // +0xA0D61 / x64 pin 663053 (0xA1E0D)
    RawPad<2> pad392;
    std::int32_t aviStereoWidthMultiplier; // x64 pin 663056 (0xA1E10,
                                           // init 2)
    unsigned char autoRepeat;
    RawPad<3> pad394;
    std::uint32_t messageSeen; // +0xA0D6C / x64 pin 663064 (0xA1E18,
                               // init 1)
    wchar_t dirModel[1000];    // x64 pin 663068 (0xA1E1C)
    wchar_t dirUser[1000];
    wchar_t dirAccs[1000];
    wchar_t dirMotion[1000];
    wchar_t dirPose[1000];
    wchar_t dirWave[1000];
    wchar_t dirBg[1000];                     // x64 pin 675068 - the seven 2000-byte
                                             // dir buffers end exactly here
    unsigned char frameVolumeControlEnabled; // x64 pin 677068 (0xA54CC)
    RawPad<3> pad403;
    std::int32_t frameNormalization; // x64 pin 677072 (0xA54D0, init 100)
    float sidebarRatio;
    unsigned char windowLayoutReady; // x64 pin 677080 (0xA54D8, init 1)
    // 0x100-byte load-status text buffer (sprintf_s target of the PMM
    // load-failure prompts), then the tail pad
    char statusText[256]; // x86 +0xA4535 / x64 pin 677081 (0xA54D9)
#if defined(_M_X64)
    RawPad<7> padTail;
#else
    RawPad<3> padTail;
#endif
};

static_assert(sizeof(MMDAppState) == kAppObjectSize,
              "app state size must match the original binary");

// The offset pin wall lives in layout_pins.hpp (layout regression guard,
// decoupled from the struct definition).  Still inside this namespace.
#include "mikudancestudio/layout_pins.hpp"

} // namespace mikudancestudio
