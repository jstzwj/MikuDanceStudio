// Process-local application state, shared by the x86 and x64 builds.
// These objects have native C++ layouts and are never read or written as PMM records.
#pragma once

#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <Windows.h>
#include <d3d9.h>

#include <cstddef>
#include <cstdint>

#include "mikudancestudio/clipboard_layout.hpp"
#include "mikudancestudio/dialog_order_arrays.hpp"
#include "mikudancestudio/path_workspace.hpp"

namespace mikudancestudio {

struct TimelineSelectionRecord;
namespace mdl {
struct BoneOrderEntry;
}

struct DialogRuntimeState {
    HWND englishNames = nullptr;
    HWND gravity = nullptr;
    HWND groundShadowColor = nullptr;
    HWND edgeThickness = nullptr;
    // The original shares this handle between frame-copy and physics editing.
    HWND frameCopyOrPhysics = nullptr;
    WNDPROC groundShadowColorEdit = nullptr;
    WNDPROC edgeThicknessEdit = nullptr;
    WNDPROC englishNameEdit = nullptr;
    WNDPROC frameCopyEdit = nullptr;
    WNDPROC gravityEdit = nullptr;
    WNDPROC originalEdit = nullptr;
    WNDPROC originalTrackbar = nullptr;
    std::int32_t englishNameComboCursor[3]{};
    float rotationValues[7]{};
    std::int32_t morphFrameShift = 0;
    std::int32_t blinkStartFrame = 0;
    std::int32_t blinkEndFrame = 0;
    std::int32_t frameRangeStartFrame = 0;
    void* accessoryEditArray = nullptr;
    std::uint8_t accessoryApplyGate = 0;
    void* rigidScratchArray = nullptr;
    void* jointScratchArray = nullptr;
    mdl::BoneOrderEntry* selectNavRecords = nullptr;
    DialogOrderArrays orders;
    // Retain the original shared scratch: ground-shadow RGBA starts at byte 112.
    alignas(float) unsigned char cameraFrameScratch[172]{};
    alignas(void*) unsigned char boneFrameScratch[140]{};
};

// Buffers retain their existing allocators; release through the application
// cleanup paths. Keeping counts and pointers together avoids detached mirrors.
struct ClipboardRuntimeState {
    mdl::BonePoseClipboardRecord* bonePoses = nullptr;
    std::int32_t bonePoseCount = 0;
    mdl::ClipboardSelectionCounts counts{};
    mdl::BoneClipboardRecord* bones = nullptr;
    mdl::MorphClipboardRecord* morphs = nullptr;
    mdl::DisplayClipboardRecord* displays = nullptr;
    mdl::CameraClipboardRecord* cameras = nullptr;
    mdl::LightClipboardRecord* lights = nullptr;
    mdl::ShadowClipboardRecord* shadows = nullptr;
    mdl::GravityClipboardRecord* gravity = nullptr;
    mdl::AccessoryClipboardKey* accessories = nullptr;
};

struct TimelineSelectionBandState {
    std::int32_t count = 0;
    TimelineSelectionRecord* records = nullptr;
};

struct SelectionRuntimeState {
    std::int32_t boxAnchorX = 0;
    std::int32_t boxAnchorY = 0;
    std::int32_t rangeFirstColumn = 0;
    std::int32_t rangeFirstRow = 0;
    std::int32_t rangeLastColumn = 0;
    std::int32_t rangeLastRow = 0;
    std::uint8_t rangeApplyEnabled = 0;
    TimelineSelectionBandState timeline[8]{};
    // 200 tree/header flags; joint rows also use indices 200..203.
    unsigned char panelRowFlags[204]{};
};

struct RenderRuntimeState {
    float lightDirection[3]{};
    D3DLIGHT9 sceneLight{};
    D3DMATRIX lightViewProjection{};
    D3DMATRIX worldViewProjection{};
    D3DMATRIX viewRotation{};
    IDirect3DVertexBuffer9* aviOverlayVertices = nullptr;
    IDirect3DVertexBuffer9* pictureOverlayVertices = nullptr;
};

enum class OniExport : std::size_t {
    Initialize,
    Clean,
    DrawDepthMap,
    DepthTexture,
    JointPosition,
    IsTracking,
    Version,
    Count,
};

struct PlatformRuntimeState {
    void* instance = nullptr;
    void* oniExports[static_cast<std::size_t>(OniExport::Count)]{};
    WINDOWPLACEMENT savedPlacement{};
};

struct CaptureRuntimeState {
    wchar_t savePath[256]{};
    wchar_t aviOutputPath[256]{};
    std::int32_t aviCodecSelection = 0;
};

struct AppRuntimeState {
    DialogRuntimeState dialogs;
    ClipboardRuntimeState clipboard;
    SelectionRuntimeState selection;
    RenderRuntimeState render;
    PlatformRuntimeState platform;
    CaptureRuntimeState capture;
    PathResolutionWorkspace paths{};
    std::uint8_t accessoryTrackActive[55]{};
};

} // namespace mikudancestudio
