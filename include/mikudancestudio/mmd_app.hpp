// ===========================================================================
// MikuDanceStudio - the MikuMikuDance v932 application state object
// ===========================================================================
// MMDAppState keeps the recovered binary-layout anchors (x86 0xA4530,
// x64 0xA55E0). AppRuntimeState owns the native dialog, clipboard, selection
// and rendering workspaces on both architectures. Accessors provide one
// live storage location; PMM I/O continues to serialize explicit fields.
// ===========================================================================
#pragma once

#include <cstddef>
#include <cstdint>

#include "mikudancestudio/app_layout.hpp"
#include "mikudancestudio/app_runtime_state.hpp"
#include "mikudancestudio/keyboard_state.hpp"
#include "mikudancestudio/accessory_layout.hpp"
#include "mikudancestudio/clipboard_layout.hpp"
#include "mikudancestudio/d3d_wrapper.hpp"
#include "mikudancestudio/dialog_order_arrays.hpp"
#include "mikudancestudio/dshow_recorder.hpp"
#include "mikudancestudio/global_key_layout.hpp"
#include "mikudancestudio/physics_scene.hpp"
#include "mikudancestudio/path_workspace.hpp"
#include "mikudancestudio/wave_audio_context.hpp"

namespace mikudancestudio {

enum class UiThemeColor : int {
    WindowFill = 0,
    WindowBorder = 1,
    ControlLight = 2,
    ControlDark = 3,
    TimelineBase = 22,
    TimelineRows = 23,
    TimelineGrid = 24,
    PanelHeader = 25,
    PanelBody = 26,
    HeaderBand = 27,
    RowBand = 28,
    LabelGrid = 29,
    SelectionText = 30,
    SelectionFill = 31,
    SelectionBorder = 32,
    DisabledText = 33,
    Accent = 34,
};

enum class AccessoryRenderPass : std::int32_t {
    None = 0,
    FixedFunction = 1,
    Effect = 2,
    ProjectedGroundShadow = 3,
    ModelOutline = 4,
    ModelEffect = 5,
};

enum class ViewportEditMode : std::int32_t {
    Bone = 0,
    BoneBox = 1,
    None = 2,
    Rotate = 3,
    Move = 4,
    ToolDrag = 5,
};

enum class GlobalTimelineTrack : std::uint8_t {
    Camera = 0,
    Light = 1,
    SelfShadow = 2,
    Gravity = 3,
};

// Panel expansion bytes follow the camera/model mode byte in the original
// optflag array. These values also pin the order stored in PMM files.
enum class UiPanel : std::uint8_t {
    Camera = 1,
    Light = 2,
    Accessory = 3,
    Bone = 4,
    Morph = 5,
    SelfShadow = 6,
};

enum class TimelineSelectionBand : std::uint8_t {
    Camera = 0,
    Light = 1,
    SelfShadow = 2,
    Gravity = 3,
    Accessory = 4,
    ModelIk = 5,
    ModelMorph = 6,
    ModelBone = 7,
};

enum class TimelineSelectionRow : std::uint8_t {
    None = 0,
    Bone = 1,
    Morph = 2,
};

struct TimelineSelectionRecord {
    std::int32_t words[4];
};

enum class ScreenCaptureMode : std::int32_t {
    Disabled = 0,
    FullFrame = 1,
    CropFourByThree = 2,
    BackgroundRefresh = 3,
};

using DepthTextureProvider = void(__stdcall*)(IDirect3DBaseTexture9**);

enum class ViewportDragMode : std::int32_t {
    None = 0,
    ViewAxisRotateX = 1,
    ViewAxisRotateY = 2,
    ViewAxisRotateZ = 3,
    LocalAxisTranslateX = 4,
    LocalAxisTranslateY = 5,
    LocalAxisTranslateZ = 6,
    BoneScale = 7,
    BoneMoveVertical = 8,
    BoneMoveScreenPlane = 9,
    PhysicsAxisX = 10,
    PhysicsAxisY = 11,
    PhysicsAxisZ = 12,
    CameraAdjustX = 13,
    CameraAdjustY = 14,
    CameraAdjustZ = 15,
    AngleAdjustX = 16,
    AngleAdjustY = 17,
    AngleAdjustZ = 18,
};

enum class ViewportToolAction : std::int32_t {
    None = 0,
    CameraOrbit = 1,
    CameraPan = 2,
    CameraGizmoHorizontal = 3,
    CameraGizmoVertical = 4,
    CameraGizmoRing = 5,
    LightGizmoHorizontal = 6,
    LightGizmoVertical = 7,
    LightGizmoCenter = 8,
    TransformAxisX = 9,
    TransformAxisY = 10,
    TransformAxisZ = 11,
    PhysicsAxisX = 12,
    PhysicsAxisY = 13,
    PhysicsAxisZ = 14,
    CameraAdjustX = 15,
    CameraAdjustY = 16,
    CameraAdjustZ = 17,
    AngleAdjustX = 18,
    AngleAdjustY = 19,
    AngleAdjustZ = 20,
    CycleCoordinateSystem = 21,
};

enum class CameraAttachmentReference : std::uint8_t {
    None = 0,
    ModelRoot = 1,
    SelectedBone = 2,
};

class MMDApp {
public:
    MMDApp();            // VA 0x0042AE60
    void InitDefaults(); // VA 0x0040A730
    void ReleaseClipboardBuffers() noexcept;

    // Recovered compatibility fields still used by unmigrated subsystems.
    // Process-local state is accessed through the methods below.
    MMDAppState state;

    // Polling stores key edges; menu commands also write 1 to suppress re-entry.
    std::int32_t& LetterKeyState(LetterKey key) {
        return state.letterKeyState[static_cast<int>(key)];
    }
    const std::int32_t& LetterKeyState(LetterKey key) const {
        return state.letterKeyState[static_cast<int>(key)];
    }

    HWND& EdgeThicknessDialog() { return m_runtime.dialogs.edgeThickness; }
    HWND& FrameCopyOrPhysicsDialog() { return m_runtime.dialogs.frameCopyOrPhysics; }
    void*& RigidScratchArray() { return m_runtime.dialogs.rigidScratchArray; }
    void*& JointScratchArray() { return m_runtime.dialogs.jointScratchArray; }
    mdl::BoneOrderEntry*& SelectNavRecords() { return m_runtime.dialogs.selectNavRecords; }

    // English-name editor dialog HWND (x86 blob slot 0xA0B50).
    HWND& EnglishNameDialog() { return m_runtime.dialogs.englishNames; }

    // Gravity-setting dialog (menu 266) HWND (x86 blob slot 0xA0CCC).
    // Was misnamed AccessoryFrameDialog before the .rc template audit.
    HWND& GravitySettingDialog() { return m_runtime.dialogs.gravity; }

    // Ground-shadow-color modeless dialog (menu 248; x86 blob slot
    // 0xA0B14) and the saved wndproc of its value edit (0xA0B18).
    HWND& GroundShadowColorDialog() { return m_runtime.dialogs.groundShadowColor; }
    WNDPROC& GroundShadowColorEditProc() { return m_runtime.dialogs.groundShadowColorEdit; }

    // Typed slot-index workspaces, with model and accessory ownership independent.
    DialogOrderArrays& DialogOrders() { return m_runtime.dialogs.orders; }

    // Morph-frame cleanup shift (menu 225) and blink-register range
    // (menu 227); x86 blob slots 0xA08F4..0xA08FC.
    std::int32_t& MorphFrameShift() { return m_runtime.dialogs.morphFrameShift; }
    std::int32_t& BlinkStartFrame() { return m_runtime.dialogs.blinkStartFrame; }
    std::int32_t& BlinkEndFrame() { return m_runtime.dialogs.blinkEndFrame; }

    // Saved wndprocs of subclassed dialog edits (x86 blob slots
    // 0xA0B48/0xA0B54/0xA0B78/0xA0CD0) and the English-name combo edit
    // positions (0xA0B58..0xA0B60).
    WNDPROC& EdgeThicknessEditProc() { // was ModelInfoEditProc - slot
                                       // 0xA0B48 is the edge-thickness
                                       // dialog (253) edit 646's proc
        return m_runtime.dialogs.edgeThicknessEdit;
    }
    WNDPROC& EnglishNameEditProc() { return m_runtime.dialogs.englishNameEdit; }
    std::int32_t& EnglishNameComboCursor(int combo) { // 0:669 1:673 2:677
        return m_runtime.dialogs.englishNameComboCursor[combo];
    }
    WNDPROC& FrameCopyEditProc() { return m_runtime.dialogs.frameCopyEdit; }
    WNDPROC& GravitySettingEditProc() { return m_runtime.dialogs.gravityEdit; }

    // Rotation-dialog working values (cases 300/302; x86 blob slots
    // 0xA0B28..0xA0B40).
    float& RotationDialogTemp(int index) { return m_runtime.dialogs.rotationValues[index]; }

    // Frame-copy dialog (menu 262) staging buffers (172/140 bytes).
    // Physics editor arrays have separate native owners in DialogRuntimeState.
    unsigned char* CameraFrameScratch() { return m_runtime.dialogs.cameraFrameScratch; }
    unsigned char* BoneFrameScratch() { return m_runtime.dialogs.boneFrameScratch; }

    // Ground-shadow-color RGBA (menu 248): overlays the camera staging
    // buffer at +112..+127 - the two dialogs are never open together,
    // mirroring the original blob reuse.
    float* GroundShadowColor() { return reinterpret_cast<float*>(CameraFrameScratch() + 112); }

    // Accessory-edit dialog (menu 442) scratch buffer 2 (x86 blob slot
    // 0xA0B24) and its close gate byte (0xA0664).
    void*& AccessoryEditArray() { return m_runtime.dialogs.accessoryEditArray; }
    unsigned char& AccessoryApplyGate() { return m_runtime.dialogs.accessoryApplyGate; }

    // Render/screenshot save path (x86 blob slot 0x9F134).
    wchar_t* CaptureSavePath() { return m_runtime.capture.savePath; }

    PathResolutionWorkspace& PathWorkspace() { return m_runtime.paths; }
    const PathResolutionWorkspace& PathWorkspace() const { return m_runtime.paths; }

    mdl::AccessoryRecord*& AccessorySlot(int index) { return AccessorySlots()[index]; }
    mdl::AccessoryRecord* AccessorySlot(int index) const { return AccessorySlots()[index]; }
    mdl::AccessoryRecord** AccessorySlots() {
        return reinterpret_cast<mdl::AccessoryRecord**>(state.objectSlots);
    }
    mdl::AccessoryRecord* const* AccessorySlots() const {
        return reinterpret_cast<mdl::AccessoryRecord* const*>(state.objectSlots);
    }
    // The 255-entry display-object list shares storage with accessory
    // records, but also contains objects selected by the model timeline.
    void*& ObjectSlot(int index) { return reinterpret_cast<void*&>(AccessorySlots()[index]); }
    const void* ObjectSlot(int index) const {
        return reinterpret_cast<void* const&>(AccessorySlots()[index]);
    }
    unsigned char*& ModelSlot(int index) { return ModelSlots()[index]; }
    unsigned char* ModelSlot(int index) const { return ModelSlots()[index]; }
    unsigned char** ModelSlots() { return reinterpret_cast<unsigned char**>(state.modelSlots); }
    unsigned char* const* ModelSlots() const {
        return reinterpret_cast<unsigned char* const*>(state.modelSlots);
    }
    void ClearModelSlots() {
        for (int index = 0; index < kModelSlotCount; ++index)
            ModelSlot(index) = nullptr;
    }
    std::uint8_t& SelectedModelSlot() { return state.slotIdx; }
    std::uint8_t SelectedModelSlot() const { return state.slotIdx; }
    void SetSelectedModelSlot(std::uint8_t slot) { SelectedModelSlot() = slot; }
    unsigned char*& SelectedModel() { return ModelSlot(SelectedModelSlot()); }
    unsigned char* SelectedModel() const { return ModelSlot(SelectedModelSlot()); }
    // Process-local clipboard pointers and counts share one owner on both builds.
    mdl::BonePoseClipboardRecord*& BonePoseClipboard() { return m_runtime.clipboard.bonePoses; }
    std::int32_t& BonePoseClipboardCount() { return m_runtime.clipboard.bonePoseCount; }
    mdl::BoneClipboardRecord*& BoneClipboard() { return m_runtime.clipboard.bones; }
    mdl::MorphClipboardRecord*& MorphClipboard() { return m_runtime.clipboard.morphs; }
    mdl::DisplayClipboardRecord*& DisplayClipboard() { return m_runtime.clipboard.displays; }
    mdl::CameraClipboardRecord*& CameraClipboard() { return m_runtime.clipboard.cameras; }
    mdl::LightClipboardRecord*& LightClipboard() { return m_runtime.clipboard.lights; }
    mdl::ShadowClipboardRecord*& ShadowClipboard() { return m_runtime.clipboard.shadows; }
    mdl::GravityClipboardRecord*& GravityClipboard() { return m_runtime.clipboard.gravity; }
    mdl::AccessoryClipboardKey*& AccessoryClipboard() { return m_runtime.clipboard.accessories; }
    mdl::CameraKey*& CameraKeys() {
        return reinterpret_cast<mdl::CameraKey*&>(state.cameraKeyTrack);
    }
    mdl::LightKey*& LightKeys() { return reinterpret_cast<mdl::LightKey*&>(state.lightKeyTrack); }
    mdl::SelfShadowKey*& ShadowKeys() {
        return reinterpret_cast<mdl::SelfShadowKey*&>(state.selfShadowKeyTrack);
    }
    mdl::GravityKey*& GravityKeys() {
        return reinterpret_cast<mdl::GravityKey*&>(state.gravityKeyTrack);
    }
    mdl::AccessoryKey*& AccessoryKeys(int slot) { return AccessoryKeyTracks()[slot]; }
    mdl::AccessoryKey** AccessoryKeyTracks() {
        return reinterpret_cast<mdl::AccessoryKey**>(state.accKeyTracks);
    }
    mdl::AccessoryKey* const* AccessoryKeyTracks() const {
        return reinterpret_cast<mdl::AccessoryKey* const*>(state.accKeyTracks);
    }
    std::uint8_t& GlobalTrackSelected(GlobalTimelineTrack track) {
        return state.globalTrackSelected[static_cast<std::uint8_t>(track)];
    }
    std::uint8_t GlobalTrackSelected(GlobalTimelineTrack track) const {
        return state.globalTrackSelected[static_cast<std::uint8_t>(track)];
    }
    void SelectGlobalTimelineTrack(GlobalTimelineTrack track) {
        for (std::uint8_t index = 0; index < 4; ++index)
            state.globalTrackSelected[index] = index == static_cast<std::uint8_t>(track) ? 1 : 0;
    }
    void ClearGlobalTimelineTrackSelection() {
        for (std::uint8_t index = 0; index < 4; ++index)
            state.globalTrackSelected[index] = 0;
    }
    std::uint8_t& TimelineSelectionChanged() { return state.timelineSelectionChanged; }
    std::uint8_t TimelineSelectionChanged() const { return state.timelineSelectionChanged; }
    std::uint8_t& SceneModified() { return state.sceneModified; }
    std::uint8_t SceneModified() const { return state.sceneModified; }
    TimelineSelectionRow& PendingTimelineSelectionRow() {
        return reinterpret_cast<TimelineSelectionRow&>(state.pendingTimelineSelectionRow);
    }
    TimelineSelectionRow PendingTimelineSelectionRow() const {
        return static_cast<TimelineSelectionRow>(state.pendingTimelineSelectionRow);
    }
    std::uint8_t& SelectionBoxDragging() { return state.selectionBoxDragging; }
    std::int32_t& MouseX() { return state.mouseX; }
    std::int32_t& MouseY() { return state.mouseY; }
    std::int32_t& PreviousMouseX() { return state.previousMouseX; }
    std::int32_t& PreviousMouseY() { return state.previousMouseY; }
    std::uint8_t& ViewportInputActive() { return state.viewportInputActive; }
    std::int32_t& ShiftModifierState() { return state.shiftModifierState; }
    std::int32_t& CtrlModifierState() { return state.ctrlModifierState; }
    bool ShiftModifierActive() const { return state.shiftModifierState == 3; }
    bool CtrlModifierActive() const { return state.ctrlModifierState == 3; }
    std::uint8_t& SidebarResizeDragging() { return state.sidebarResizeDragging; }
    std::int32_t& LeftMouseButtonState() { return state.leftMouseButtonState; }
    std::int32_t& RightMouseButtonState() { return state.rightMouseButtonState; }
    std::int32_t& MiddleMouseButtonState() { return state.middleMouseButtonState; }
    bool LeftMouseButtonHeld() const { return state.leftMouseButtonState == 3; }
    bool RightMouseButtonHeld() const { return state.rightMouseButtonState == 3; }
    bool MiddleMouseButtonHeld() const { return state.middleMouseButtonState == 3; }
    std::int32_t& SelectionBoxAnchorX() { return m_runtime.selection.boxAnchorX; }
    std::int32_t& SelectionBoxAnchorY() { return m_runtime.selection.boxAnchorY; }
    std::int32_t& TimelineRangeFirstColumn() { return m_runtime.selection.rangeFirstColumn; }
    std::int32_t& TimelineRangeFirstRow() { return m_runtime.selection.rangeFirstRow; }
    std::int32_t& TimelineRangeLastColumn() { return m_runtime.selection.rangeLastColumn; }
    std::int32_t& TimelineRangeLastRow() { return m_runtime.selection.rangeLastRow; }
    std::uint8_t& TimelineRangeApplyEnabled() { return m_runtime.selection.rangeApplyEnabled; }
    // Original x64 RVA 0x3BC00..0x3BC54 stores a cell's half-open rectangle.
    void SetTimelineRangeCell(std::int32_t column, std::int32_t row) {
        TimelineRangeFirstColumn() = column;
        TimelineRangeFirstRow() = row;
        TimelineRangeLastColumn() = column + 1;
        TimelineRangeLastRow() = row + 1;
    }
    void ClearTimelineRange() {
        TimelineRangeFirstColumn() = 0;
        TimelineRangeFirstRow() = 0;
        TimelineRangeLastColumn() = 0;
        TimelineRangeLastRow() = 0;
        TimelineRangeApplyEnabled() = 0;
    }
    // Each timeline band owns a native count/pointer pair on both architectures.
    std::int32_t& TimelineSelectionCount(TimelineSelectionBand band) {
        return m_runtime.selection.timeline[static_cast<std::size_t>(band)].count;
    }
    TimelineSelectionRecord*& TimelineSelectionRecords(TimelineSelectionBand band) {
        return m_runtime.selection.timeline[static_cast<std::size_t>(band)].records;
    }
    void ReleaseTimelineSelectionRecords() noexcept;
    void ClearPanelRowFlags() noexcept;
    mdl::ClipboardSelectionCounts& ClipboardCounts() { return m_runtime.clipboard.counts; }
    const mdl::ClipboardSelectionCounts& ClipboardCounts() const {
        return m_runtime.clipboard.counts;
    }
    std::int32_t& CurrentFrame() {
        // The generated layout stores the frame counter unsigned; keep the
        // historical signed view so signed comparisons at call sites are
        // unchanged.
        return reinterpret_cast<std::int32_t&>(state.currentFrame);
    }
    HWND& MainWindow() { return state.hwnd; }
    float* LightDirection() { return m_runtime.render.lightDirection; }
    // Original x64 app+0x9F070 and x86 app+0x9E1A4 are Ambient.rgb,
    // including PMM reads/writes. Keep one live value instead of synchronizing copies.
    float* LightColor() { return &m_runtime.render.sceneLight.Ambient.r; }
    D3DLIGHT9& SceneLight() { return m_runtime.render.sceneLight; }
    const D3DLIGHT9& SceneLight() const { return m_runtime.render.sceneLight; }
    // Original x64 RVA 0x745FB..0x7468D: direction and the complete device light.
    void ResetSceneLight() noexcept;
    // The PMM light track stores only RGB and direction.  Its target is the
    // application's directional key light, whose fixed-function fields must
    // remain coherent whenever a key is applied or interpolated.
    void ApplyTimelineLightState() {
        D3DLIGHT9& light = SceneLight();
        light.Type = D3DLIGHT_DIRECTIONAL;
        light.Direction.x = LightDirection()[0];
        light.Direction.y = LightDirection()[1];
        light.Direction.z = LightDirection()[2];
        light.Specular.r = LightColor()[0];
        light.Specular.g = LightColor()[1];
        light.Specular.b = LightColor()[2];
        light.Ambient.r = LightColor()[0];
        light.Ambient.g = LightColor()[1];
        light.Ambient.b = LightColor()[2];
    }
    std::uint8_t& GroundShadowEnabled() { return state.groundShadowEnabled; }
    IDirect3DVertexBuffer9*& GroundGridVertices() {
        return reinterpret_cast<IDirect3DVertexBuffer9*&>(state.groundGridVertices);
    }
    IDirect3DIndexBuffer9*& GroundGridIndices() {
        return reinterpret_cast<IDirect3DIndexBuffer9*&>(state.groundGridIndices);
    }
    ViewportEditMode& EditMode() { return reinterpret_cast<ViewportEditMode&>(state.editMode); }
    const ViewportEditMode& EditMode() const {
        return const_cast<const ViewportEditMode&>(
            reinterpret_cast<const ViewportEditMode&>(state.editMode));
    }
    bool UsesViewportTool() const {
        return static_cast<std::int32_t>(EditMode()) >=
               static_cast<std::int32_t>(ViewportEditMode::None);
    }
    std::int32_t& ViewportToolCenterX() { return state.viewportToolCenterX; }
    std::int32_t& ViewportToolCenterY() { return state.viewportToolCenterY; }
    std::uint32_t& ViewportToolHovered() { return state.viewportToolHovered; }
    ViewportToolAction& ViewportToolOperation() {
        return reinterpret_cast<ViewportToolAction&>(state.viewportToolOperation);
    }
    ViewportToolAction& ViewToolDragOperation() {
        return reinterpret_cast<ViewportToolAction&>(state.viewToolDragOperation);
    }
    ViewportDragMode& InteractionDragMode() {
        return reinterpret_cast<ViewportDragMode&>(state.interactionDragMode);
    }
    std::int32_t& ViewportToolDragOriginX() { return state.dragOriginX; }
    std::int32_t& ViewportToolDragOriginY() { return state.dragOriginY; }
    std::int32_t& BoneBoxStartX() { return state.boneBoxStartX; }
    std::int32_t& BoneBoxStartY() { return state.boneBoxStartY; }
    std::int32_t& BoneBoxSelectionActive() { return state.boneBoxSelectionActive; }
    D3DMATRIX& LightViewProjection() { return m_runtime.render.lightViewProjection; }
    const D3DMATRIX& LightViewProjection() const { return m_runtime.render.lightViewProjection; }
    D3DMATRIX& WorldViewProjection() { return m_runtime.render.worldViewProjection; }
    const D3DMATRIX& WorldViewProjection() const { return m_runtime.render.worldViewProjection; }
    IDirect3DTexture9*& AviBackgroundTexture() {
        return reinterpret_cast<IDirect3DTexture9*&>(state.aviBackgroundTexture);
    }
    IDirect3DSurface9*& AviBackgroundSurface() {
        return reinterpret_cast<IDirect3DSurface9*&>(state.aviBackgroundSurface);
    }
    void*& AviDrawDib() { return state.drawDib; }
    void*& AviFile() { return state.aviFile; }
    void*& AviStream() { return state.aviStream; }
    void*& AviFrameReader() { return state.aviFrameReader; }
    HWND& FloatingWindow() { return state.floatingWindow; }
    HWND& RecordingWindow() { return state.recordingWindow; }
    IDirect3DTexture9*& ToonTexture(int index) {
        return reinterpret_cast<IDirect3DTexture9*&>(state.toonTextures[index]);
    }
    IDirect3DTexture9*& SceneFontTexture() {
        return reinterpret_cast<IDirect3DTexture9*&>(state.sceneFontTexture);
    }
    IDirect3DVertexBuffer9*& AviOverlayVertices() { return m_runtime.render.aviOverlayVertices; }
    IDirect3DTexture9*& PictureBackgroundTexture() {
        return reinterpret_cast<IDirect3DTexture9*&>(state.pictureBackgroundTexture);
    }
    IDirect3DVertexBuffer9*& PictureOverlayVertices() {
        return m_runtime.render.pictureOverlayVertices;
    }
    wchar_t* AviBackgroundPath() { return state.aviBackgroundPath; }
    wchar_t* PictureBackgroundPath() { return state.pictureBackgroundPath; }
    std::int32_t& AviStreamStartFrame() { return state.aviStreamStart; }
    std::int32_t& AviStreamEndFrame() { return state.aviStreamEnd; }
    std::uint8_t& AviUsesThirtyFpsTiming() { return state.aviUsesThirtyFpsTiming; }
    std::int32_t& AviOffsetX() { return state.aviOffsetX; }
    std::int32_t& AviOffsetY() { return state.aviOffsetY; }
    float& AviScale() { return state.aviScale; }
    std::int32_t& AviFrameWidth() { return state.aviFrameWidth; }
    std::int32_t& AviFrameHeight() { return state.aviFrameHeight; }
    std::uint8_t& PictureBackgroundEnabled() { return state.pictureBackgroundEnabled; }
    std::int32_t& PictureOffsetX() { return state.pictureOffsetX; }
    std::int32_t& PictureOffsetY() { return state.pictureOffsetY; }
    float& PictureScale() { return state.pictureScale; }
    std::int32_t& PictureWidth() { return state.pictureWidth; }
    std::int32_t& PictureHeight() { return state.pictureHeight; }
    std::uint32_t& AviBackgroundEnabled() { return state.aviBackgroundEnabled; }
    IDirect3DTexture9*& CaptureTexture() {
        return reinterpret_cast<IDirect3DTexture9*&>(state.captureTexture);
    }
    IDirect3DSurface9*& CaptureRenderTarget() {
        return reinterpret_cast<IDirect3DSurface9*&>(state.captureRenderTarget);
    }
    IDirect3DSurface9*& CaptureSystemSurface() {
        return reinterpret_cast<IDirect3DSurface9*&>(state.captureSystemSurface);
    }
    ScreenCaptureMode& CaptureMode() {
        return reinterpret_cast<ScreenCaptureMode&>(state.captureMode);
    }
    void*& CaptureReadbackPixels() { return state.captureReadbackPixels; }
    std::uint8_t*& RecordingCompletionFlag() {
        return reinterpret_cast<std::uint8_t*&>(state.recordingCompletionFlag);
    }
    IDirect3DTexture9*& OverlayTexture() {
        return reinterpret_cast<IDirect3DTexture9*&>(state.overlayTexture);
    }
    IDirect3DVertexBuffer9*& SpriteOverlayVertices() {
        return reinterpret_cast<IDirect3DVertexBuffer9*&>(state.spriteOverlayVertices);
    }
    std::uint32_t& SpriteOverlayPrimitiveCount() { return state.spriteOverlayPrimitiveCount; }
    IDirect3DTexture9*& ProjectedShadowRestoreTexture() {
        return reinterpret_cast<IDirect3DTexture9*&>(state.projectedShadowRestoreTexture);
    }
    std::uint8_t& ProjectedShadowBlendEnabled() { return state.projectedShadowBlendEnabled; }
    void*& ActiveRenderObject() { return state.activeRenderObject; }
    AccessoryRenderPass& ActiveRenderPass() {
        return reinterpret_cast<AccessoryRenderPass&>(state.activeRenderPass);
    }
    std::uint8_t& ModelNonDisplayMode() { return state.modelNonDisplayMode; }
    std::int32_t& ModelOutlineColorRed() { return state.modelOutlineColorRed; }
    std::int32_t& ModelOutlineColorGreen() { return state.modelOutlineColorGreen; }
    std::int32_t& ModelOutlineColorBlue() { return state.modelOutlineColorBlue; }
    std::uint8_t& WireframeRenderingEnabled() { return state.wireframeRenderingEnabled; }
    unsigned char* PanelRowFlags() {
        // 200 tree/header flags plus the joint view's four extra rows.
        return m_runtime.selection.panelRowFlags;
    }
    std::uint32_t& GravityNoiseModeWord() {
        // Dword view at +0xA0CD4: byte 0 is the promoted gravityNoiseEnabled noise-mode
        // flag; the upper three bytes land in pad368 on both architectures
        // (the original writes all four bytes from the gravity record).
        return *reinterpret_cast<std::uint32_t*>(&state.gravityNoiseEnabled);
    }
    std::uint8_t& RecordedFrameCountByte1() {
        // Byte view of recordedFrameCount+1: the original seeks poke this byte while
        // the recording catch-up reads the surrounding dword as a counter.
        return reinterpret_cast<std::uint8_t*>(&state.recordedFrameCount)[1];
    }
    std::int32_t& AccessoryRenderSplitOrder() {
        // Generated field is uint32_t; preserve the signed accessor view.
        return reinterpret_cast<std::int32_t&>(state.accessoryRenderSplitOrder);
    }
    std::uint8_t& SelectedAccessorySlot() { return state.selectedObjectSlot; }
    // The accessory and model-display lists share this historical selection
    // byte.  Use this neutral name outside accessory-specific code.
    std::uint8_t& SelectedObjectSlot() { return SelectedAccessorySlot(); }
    std::uint8_t SelectedObjectSlot() const { return state.selectedObjectSlot; }
    std::int32_t& DisplayObjectListScrollPosition() {
        return state.displayObjectListScrollPosition;
    }
    std::int32_t DisplayObjectListScrollPosition() const {
        return state.displayObjectListScrollPosition;
    }
    std::int32_t& DisplayObjectListMatchCount() { return state.displayObjectListMatchCount; }
    std::int32_t DisplayObjectListMatchCount() const { return state.displayObjectListMatchCount; }
    std::uint32_t& LastRegisteredFrame() { return state.lastRegisteredFrame; }
    IDirect3DVertexBuffer9*& OverlayVertices() {
        return reinterpret_cast<IDirect3DVertexBuffer9*&>(state.overlayVertices);
    }
    std::uint32_t& TextOverlayPrimitiveCount() { return state.textOverlayPrimitiveCount; }
    IDirect3DTexture9*& TextOverlayTexture() {
        return reinterpret_cast<IDirect3DTexture9*&>(state.sceneFontTexture);
    }
    std::uint32_t& LineOverlayPrimitiveCount() { return state.lineOverlayPrimitiveCount; }
    IDirect3DVertexBuffer9*& GroundPlaneVertices() {
        return reinterpret_cast<IDirect3DVertexBuffer9*&>(state.groundPlaneVertices);
    }
    IDirect3DVertexBuffer9*& LeftViewportVertices() {
        return reinterpret_cast<IDirect3DVertexBuffer9*&>(state.leftViewportVertices);
    }
    IDirect3DVertexBuffer9*& RightViewportVertices() {
        return reinterpret_cast<IDirect3DVertexBuffer9*&>(state.rightViewportVertices);
    }
    std::uint8_t& SelfShadowCompositionEnabled() { return state.selfShadowCompositionEnabled; }
    std::int32_t& SelfShadowMode() { return state.selfShadowMode; }
    std::uint8_t& DepthTextureCompositionEnabled() { return state.depthTextureCompositionEnabled; }
    std::uint8_t& DepthDeviceEnabled() { return state.depthDeviceEnabled; }
    DepthTextureProvider& DepthTextureCallback() {
        return reinterpret_cast<DepthTextureProvider&>(m_runtime.platform.oniExports[3]);
    }
    HDC& PanelDC() { return state.hdcMainPanel; }
    HDC& TimelineDC() { return state.hdcTimeline; }
    HDC& CurveDC() { return state.hdcInterpCurve; }
    HBITMAP& PanelBitmap() { return reinterpret_cast<HBITMAP&>(state.bmpPanel); }
    HBITMAP& PanelSpareBitmap() { return reinterpret_cast<HBITMAP&>(state.bmpPanelSpare); }
    HBITMAP& TimelineBitmap() { return reinterpret_cast<HBITMAP&>(state.bmpTimelineStrip); }
    HBITMAP& CurveBitmap() { return reinterpret_cast<HBITMAP&>(state.bmpInterpCurve); }
    HBRUSH UiBrush(int index) const {
        // brush handles are kept in 32-bit slots, as in the original state
        return reinterpret_cast<HBRUSH>(static_cast<std::uintptr_t>(state.brushes[index]));
    }
    void SetUiBrush(int index, HBRUSH brush) {
        state.brushes[index] = static_cast<std::uint32_t>(reinterpret_cast<std::uintptr_t>(brush));
    }
    COLORREF& ThemeColor(UiThemeColor color) {
        return reinterpret_cast<COLORREF&>(state.themeColors[static_cast<int>(color)]);
    }
    const COLORREF& ThemeColor(UiThemeColor color) const {
        return reinterpret_cast<const COLORREF&>(state.themeColors[static_cast<int>(color)]);
    }
    COLORREF& ThemeColorAt(int index) {
        return reinterpret_cast<COLORREF&>(state.themeColors[index]);
    }
    const COLORREF& ThemeColorAt(int index) const {
        return reinterpret_cast<const COLORREF&>(state.themeColors[index]);
    }
    std::uint8_t& UiTextRed() { return state.uiTextRed; }
    std::uint8_t& UiTextGreen() { return state.uiTextGreen; }
    std::uint8_t& UiTextBlue() { return state.uiTextBlue; }
    wchar_t* WavePath() { return reinterpret_cast<wchar_t*>(state.wavPath); }
    std::uint8_t& WaveEnabled() { return state.waveEnabled; }
    // AVI export options filled by the output dialog (0x40F2F0).
    wchar_t* AviOutputPath() { return m_runtime.capture.aviOutputPath; }
    std::int32_t& AviRecordStartFrame() { return state.aviRecordStartFrame; }
    std::int32_t& AviRecordEndFrame() { return state.aviRecordEndFrame; }
    float& AviRecordFps() { return state.aviRecordFps; }
    std::uint8_t& AviIncludeWave() { return state.aviIncludeWave; }
    std::int32_t& AviCodecSelection() { return m_runtime.capture.aviCodecSelection; }
    std::uint8_t& AviStereoOutput() { return state.aviStereoOutput; }
    std::int32_t& AviStereoWidthMultiplier() { return state.aviStereoWidthMultiplier; }
    float& ProjectedShadowDiffuseAlpha() { return state.projectedShadowDiffuseAlpha; }
    float& ProjectedShadowAmbientIntensity() { return state.projectedShadowAmbientIntensity; }
    void SetProjectedShadowAmbientRgb(float intensity) {
        ProjectedShadowAmbientIntensity() = intensity;
        state.projectedShadowAmbientG = intensity;
        state.projectedShadowAmbientB = intensity;
    }
    void SetProjectedShadowAmbient(float intensity) {
        SetProjectedShadowAmbientRgb(intensity);
        state.projectedShadowAmbientA = intensity;
    }
    float& ProjectedShadowSpecularAlpha() { return state.projectedShadowSpecularAlpha; }
    D3DMATERIAL9 ProjectedShadowMaterial() const {
        D3DMATERIAL9 material{};
        material.Diffuse.a = state.projectedShadowDiffuseAlpha;
        material.Ambient = {state.projectedShadowAmbientIntensity, state.projectedShadowAmbientG,
                            state.projectedShadowAmbientB, state.projectedShadowAmbientA};
        material.Specular.a = state.projectedShadowSpecularAlpha;
        return material;
    }
    std::uint8_t& DirectSoundAvailable() { return state.directSoundAvailable; }
    std::int32_t& TimelineStartFrame() { return state.timelineStartFrame; }
    std::uint8_t& PlaybackActive() { return state.playbackActive; }
    std::uint8_t PlaybackActive() const { return state.playbackActive; }
    std::uint8_t& PlaybackLoopEnabled() { return state.playbackLoopEnabled; }
    std::uint8_t PlaybackLoopEnabled() const { return state.playbackLoopEnabled; }
    std::uint8_t& FrameStepPlayback() { return state.frameStepPlayback; }
    std::uint8_t FrameStepPlayback() const { return state.frameStepPlayback; }
    float& PlaybackStartSeconds() { return state.playbackStartSeconds; }
    float& PlaybackCursorSeconds() { return state.playbackCursorSeconds; }
    float& PlaybackEndSeconds() { return state.playbackEndSeconds; }
    std::uint32_t& PlaybackClockAnchorLow() { return state.playbackClockAnchorLow; }
    std::uint32_t& PlaybackClockAnchorHigh() { return state.playbackClockAnchorHigh; }
    std::int32_t& SavedPlaybackPhysicsMode() { return state.savedPlaybackPhysicsMode; }
    std::uint8_t& PlaybackStartsAtCurrentFrame() { return state.playbackStartsAtCurrentFrame; }
    std::uint8_t& PlaybackFrameChanged() { return state.playbackFrameChanged; }
    std::uint8_t& PhysicsResetPending() { return state.physicsResetPending; }
    std::uint8_t PhysicsResetPending() const { return state.physicsResetPending; }
    std::int32_t& PlaybackPhysicsMode() { return state.playbackPhysicsMode; }
    // x64 app+0xA1E18 / x86 app+0xA0D6C = state.messageSeen: ONE shared
    // counter in both originals (refreshed from playing / idle-suppress-off
    // / English-name-dialog-open at the main-pump prologue, walks 1->2->3->0,
    // reset to 1 by every window message) that gates the physics section
    // (0x7FF7CB44B8E7 / x86 0x46EFBE), the pump regions and the Present.
    // Use MessageSeen() directly - see PhysicsFrame and
    // AdvanceFrameRenderGate for the full ladder.
    std::int32_t& SidebarWidth() { return state.sidebarWidth; }
    std::int32_t& RenderWidth() { return state.renderW; }
    std::int32_t& RenderHeight() { return state.renderH; }
    std::int32_t& SeparateWindowSidebarWidth() { return state.separateWindowSidebarWidth; }
    std::int32_t& SeparateWindowX() { return state.separateWindowX; }
    std::int32_t& SeparateWindowY() { return state.separateWindowY; }
    std::int32_t& SeparateWindowWidth() { return state.separateWindowWidth; }
    std::int32_t& SeparateWindowHeight() { return state.separateWindowHeight; }
    std::uint8_t& SeparateWindowMaximized() { return state.separateWindowMaximized; }
    std::uint8_t& FrameVolumeControlEnabled() { return state.frameVolumeControlEnabled; }
    float& SidebarRatio() { return state.sidebarRatio; }
    std::int32_t& FrameNormalization() { return state.frameNormalization; }
    RECT& ViewportRect() { return reinterpret_cast<RECT&>(state.hideRight); }
    const RECT& ViewportRect() const { return reinterpret_cast<const RECT&>(state.hideRight); }
    std::uint8_t& FullscreenMode() { return state.fullscreenMode; }
    std::uint32_t& MessageSeen() { return state.messageSeen; }
    std::uint8_t& WindowLayoutReady() { return state.windowLayoutReady; }
    std::uint8_t& EnhancedModelDirty() { return state.enhancedModelDirty; }
    std::uint8_t& AutoRepeatCount() { return state.autoRepeat; }
    std::uint8_t& UiOptionFlag(int index) { return state.optflag[index]; }
    std::uint8_t UiOptionFlag(int index) const { return state.optflag[index]; }
    std::uint8_t& CameraMode() { return UiOptionFlag(0); }
    std::uint8_t CameraMode() const { return UiOptionFlag(0); }
    std::uint8_t& PanelExpanded(UiPanel panel) { return UiOptionFlag(static_cast<int>(panel)); }
    std::uint8_t PanelExpanded(UiPanel panel) const {
        return UiOptionFlag(static_cast<int>(panel));
    }
    std::uint32_t& MainModelComboSelection() { return state.mainModelComboSelection; }
    std::uint8_t& GroundGridEnabled() { return state.groundGridEnabled; }
    std::uint8_t& FpsOverlayEnabled() { return state.fpsOverlayEnabled; }
    float& FpsOverlayElapsedSeconds() { return state.fpsOverlayElapsedSeconds; }
    std::int32_t& FpsOverlayFrameCount() {
        return reinterpret_cast<std::int32_t&>(state.fpsOverlayFrameCount);
    }
    std::int32_t& FramesPerSecond() {
        return reinterpret_cast<std::int32_t&>(state.framesPerSecond);
    }
    float& BoneRotationEditDegreesX() { return state.eulerX; }
    float& BoneRotationEditDegreesY() { return state.eulerY; }
    float& BoneRotationEditDegreesZ() { return state.eulerZ; }
    std::uint8_t& AudioSeekReady() { return state.a02B6; }
    std::uint8_t& AutomaticFrameAdvanceEnabled() { return state.automaticFrameAdvanceEnabled; }
    WNDPROC& OriginalEditProc() { return m_runtime.dialogs.originalEdit; }
    WNDPROC& OriginalTrackbarProc() { return m_runtime.dialogs.originalTrackbar; }
    std::uint32_t& CameraTrackCursor() { return state.cameraTrackCursor; }
    std::uint8_t& CameraTrackActive() { return state.cameraTrackActive; }
    std::uint32_t& LightTrackCursor() { return state.lightTrackCursor; }
    std::uint8_t& LightTrackActive() { return state.lightTrackActive; }
    std::uint32_t& ShadowTrackCursor() { return state.shadowTrackCursor; }
    std::uint8_t& ShadowTrackActive() { return state.shadowTrackActive; }
    std::uint32_t& GravityTrackCursor() { return state.gravityTrackCursor; }
    std::uint8_t& GravityTrackActive() { return state.gravityTrackActive; }
    std::uint32_t& AccessoryTrackCursor(int slot) { return state.accessoryTrackCursor[slot]; }
    std::uint8_t& AccessoryTrackActive(int slot) { return m_runtime.accessoryTrackActive[slot]; }
    float* CameraPosition() { return state.cameraPosition; }
    float* CameraRotation() { return &state.cameraPitch; }
    float& CameraPositionX() { return state.cameraPosition[0]; }
    float& CameraPositionY() { return state.cameraPosition[1]; }
    float& CameraPositionZ() { return state.cameraPosition[2]; }
    float& CameraPitch() { return state.cameraPitch; }
    float& CameraYaw() { return state.cameraYaw; }
    float& CameraRoll() { return state.cameraRoll; }
    float& ViewOffsetX() { return state.viewOffsetX; }
    float& ViewOffsetY() { return state.viewOffsetY; }
    float& CameraDistance() { return state.cameraDistance; }
    float& CameraFov() { return state.cameraFov; }
    std::uint8_t& CameraPerspective() { return state.cameraPerspective; }
    std::int32_t& CameraParentModel() {
        // Generated field is uint32_t; preserve the signed accessor view.
        return reinterpret_cast<std::int32_t&>(state.cameraParentModel);
    }
    std::int32_t& CameraParentBone() { return state.cameraParentBone; }
    CameraAttachmentReference& CameraReferenceMode() {
        return reinterpret_cast<CameraAttachmentReference&>(state.cameraReferenceMode);
    }
    D3DMATRIX& CameraAttachmentBasis() {
        return reinterpret_cast<D3DMATRIX&>(state.cameraAttachmentBasis);
    }
    const D3DMATRIX& CameraAttachmentBasis() const {
        return reinterpret_cast<const D3DMATRIX&>(state.cameraAttachmentBasis);
    }
    std::uint8_t& CameraAttachmentTransformSuppressed() {
        return state.cameraAttachmentTransformSuppressed;
    }
    D3DMATRIX& ViewRotationTransform() { return m_runtime.render.viewRotation; }
    const D3DMATRIX& ViewRotationTransform() const { return m_runtime.render.viewRotation; }
    std::int32_t& ShadowMode() { return state.selfShadowMode; }
    float& ShadowDistance() { return state.selfShadowDistance; }
    float* GravityDirection() { return &GravityX(); }
    std::int32_t& GravityNoise() { return reinterpret_cast<std::int32_t&>(state.gravityNoise); }
    std::uint8_t& GravityNoiseEnabled() { return state.gravityNoiseEnabled; }

    // -- named fields (semantic names verified so far) -------------------
    // Main loop timing cluster (WinMain 0x004C4460)
    float& FpsLimit() { return state.fpsLimit; }   // 657632
    float& DeltaTime() { return state.deltaTime; } // 657084
    std::uint32_t& TimeNowLow() { return state.timeNowLow; }
    std::uint32_t& TimeNowHigh() { return state.timeNowHigh; }
    float& MilliToSec() { return state.milliToSec; } // 0.001

    // Environment / startup scene file (wchar_t[256] @ 657664)
    wchar_t* EnvFileName() { return state.envFileName; }

    // Locale/font subsystem pointer consumed by ConvertAnsiToWide (0x00407A70)
    void*& LocaleTablePtr() { return state.renderer; } // 657092

    // Main window (0x0047A5B0)
    void*& Hwnd() { return reinterpret_cast<void*&>(state.hwnd); } // 657080
    void*& HInstance() { return m_runtime.platform.instance; }
    void*& OpenniTrackingCallback() { return m_runtime.platform.oniExports[5]; }
    // Kinect capture-stage byte (x64 0xA1E14 / x86 0xA0D68; verified to be
    // state.autoRepeat itself: menu 0x124/WM_COMMAND 292 sets it to 1; WM_TIMER 0x65
    // increments it every 1.5s until 4, then stops; pump block 0x7FF7CB44C560 tests ==4 for recording;
    // OpenNiInit/DisableKinect/12600 samples/lost tracking clear it). This alias accessor
    // gives the pump block a semantic name; every read and write uses the same byte.
    std::uint8_t& KinectCaptureStage() { return state.autoRepeat; }
    std::uint8_t KinectCaptureStage() const { return state.autoRepeat; }
    // Seven native pointer slots for DxOpenNI.dll exports on both builds.
    // Slots 3 and 5 are shared by the named depth/tracking callback accessors.
    void*& OniExportSlot(int index) { return m_runtime.platform.oniExports[index]; }
    std::int32_t& FrameRangeStartFrame() { return m_runtime.dialogs.frameRangeStartFrame; }
    WINDOWPLACEMENT& SavedPlacement() { return m_runtime.platform.savedPlacement; }
    // Render/locale subsystem ("0x1D574 object"), allocated in
    // InitMainWindowAndD3D; layout restored in d3d_wrapper.hpp.
    D3DRenderer*& Renderer() { return reinterpret_cast<D3DRenderer*&>(state.renderer); }
    WaveAudioContext*& Audio() { return reinterpret_cast<WaveAudioContext*&>(state.audioContext); }
    DShowRecorder*& Recorder() { return reinterpret_cast<DShowRecorder*&>(state.recorder); }
    // AccessoryRecord slot that carries the coordinate-axis gizmo X-file
    // mesh (was sub04b0 / 0x04B0(); 0x4B0 obj, freed via DisposeAccessory).
    void*& AxisMeshObject() { return state.axisMeshObject; }
    // Physics scene wrapper ("0x48 object"), allocated in
    // InitMainWindowAndD3D, filled by SceneConstruct; see physics_scene.hpp.
    PhysicsScene*& Physics() { return reinterpret_cast<PhysicsScene*&>(state.physicsScene); }
    wchar_t* ExeDir() { return state.exeDir; }
    unsigned char& EnglishUI() { return state.englishUI; } // 658252

    // User directory names (wchar_t[1000] each, 0x0047A5B0)
    wchar_t* DirModel() { return state.dirModel; }
    wchar_t* DirUser() { return state.dirUser; }
    wchar_t* DirAccs() { return state.dirAccs; }
    wchar_t* DirMotion() { return state.dirMotion; }
    wchar_t* DirPose() { return state.dirPose; }
    wchar_t* DirWave() { return state.dirWave; }
    wchar_t* DirBg() { return state.dirBg; }

    // Physics gravity (defaults 0.0 / -1.0 / 0.0, magnitude 9.8)
    float& GravityX() { return state.gravityX; }
    float& GravityY() { return state.gravityY; }
    float& GravityZ() { return state.gravityZ; }
    float& GravityMagnitude() { return state.gravityMagnitude; }

    // Recent-file ANSI buffers (char[256] each)
    char* RecentFile(int index) {
        return index == 0   ? reinterpret_cast<char*>(state.recentFile0)
               : index == 1 ? reinterpret_cast<char*>(state.recentFile1)
                            : reinterpret_cast<char*>(state.recentFile2);
    }

private:
    AppRuntimeState m_runtime;
};

static_assert(sizeof(MMDApp) >= sizeof(MMDAppState),
              "MMDApp must retain the complete compatibility state");
// The compatibility record's per-architecture size is pinned in app_layout.hpp.

// The single instance - mirrors the `Block` global at VA 0x0054593C.
extern MMDApp* g_Block;

} // namespace mikudancestudio
