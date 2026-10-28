#include "mikudancestudio/mmd_app.hpp"

#include <cstdio>
#include <cstdlib>
#include <memory>
#include <cstring>

using namespace mikudancestudio;
namespace {
int failures = 0;
void Check(bool condition, const char* message) {
    if (!condition) {
        std::fprintf(stderr, "%s\n", message);
        ++failures;
    }
}

template <class T> T* Allocate() {
    return static_cast<T*>(std::calloc(1, sizeof(T)));
}

void TestClipboardRelease(MMDApp& app) {
    app.ClipboardCounts() = {1, 1, 1, 1, 1, 1, 1, 1};
    app.BonePoseClipboardCount() = 1;
    app.BonePoseClipboard() = static_cast<mdl::BonePoseClipboardRecord*>(
        ::operator new(sizeof(mdl::BonePoseClipboardRecord)));
    app.BoneClipboard() = Allocate<mdl::BoneClipboardRecord>();
    app.MorphClipboard() = Allocate<mdl::MorphClipboardRecord>();
    app.DisplayClipboard() = Allocate<mdl::DisplayClipboardRecord>();
    app.DisplayClipboard()->ikStates = Allocate<mdl::IkClipboardState>();
    app.DisplayClipboard()->selectorStates = Allocate<mdl::SelectorClipboardState>();
    app.CameraClipboard() = Allocate<mdl::CameraClipboardRecord>();
    app.LightClipboard() = Allocate<mdl::LightClipboardRecord>();
    app.ShadowClipboard() = Allocate<mdl::ShadowClipboardRecord>();
    app.GravityClipboard() = Allocate<mdl::GravityClipboardRecord>();
    app.AccessoryClipboard() = Allocate<mdl::AccessoryClipboardKey>();
    app.ReleaseClipboardBuffers();
    Check(!app.BonePoseClipboard() && !app.BoneClipboard() && !app.MorphClipboard() &&
              !app.DisplayClipboard() && !app.CameraClipboard() && !app.LightClipboard() &&
              !app.ShadowClipboard() && !app.GravityClipboard() && !app.AccessoryClipboard(),
          "Clipboard cleanup clears all owned pointers");
    Check(app.BonePoseClipboardCount() == 0 && app.ClipboardCounts().bones == 0 &&
              app.ClipboardCounts().displays == 0 && app.ClipboardCounts().accessories == 0,
          "Clipboard cleanup clears counts together with buffers");
    app.ReleaseClipboardBuffers();
}

void TestSelection(MMDApp& app, MMDApp& other) {
    for (int i = 0; i < 8; ++i) {
        auto band = static_cast<TimelineSelectionBand>(i);
        app.TimelineSelectionCount(band) = i + 1;
        app.TimelineSelectionRecords(band) = static_cast<TimelineSelectionRecord*>(
            ::operator new(sizeof(TimelineSelectionRecord) * (i + 1)));
        Check(other.TimelineSelectionCount(band) == 0 && !other.TimelineSelectionRecords(band),
              "Selection records are isolated per application, including gravity");
    }
    app.ReleaseTimelineSelectionRecords();
    app.ReleaseTimelineSelectionRecords();
    for (int i = 0; i < 8; ++i) {
        auto band = static_cast<TimelineSelectionBand>(i);
        Check(!app.TimelineSelectionRecords(band) && app.TimelineSelectionCount(band) == 0,
              "Selection cleanup clears all eight native count/pointer pairs");
    }
    app.TimelineRangeFirstColumn() = 15;
    app.TimelineRangeLastRow() = 30;
    app.TimelineRangeApplyEnabled() = 1;
    std::memset(app.PanelRowFlags(), 1, 204);
    app.ClearPanelRowFlags();
    for (int i = 0; i < 204; ++i)
        Check(app.PanelRowFlags()[i] == 0, "Panel reset reaches tree and joint row flags");
    Check(app.TimelineRangeFirstColumn() == 15 && app.TimelineRangeLastRow() == 30 &&
              app.TimelineRangeApplyEnabled() == 1,
          "Panel flags no longer overlap timeline range anchors");
    app.ClearTimelineRange();
    Check(!app.TimelineRangeFirstColumn() && !app.TimelineRangeLastRow() &&
              !app.TimelineRangeApplyEnabled(),
          "Range reset clears its live storage");
    app.PanelRowFlags()[203] = 1;
    app.InitDefaults();
    Check(app.PanelRowFlags()[203] == 0, "InitDefaults resets the live panel flags");
}

void TestLightAndMatrices(MMDApp& app, MMDApp& other) {
    app.CameraFov() = 30;
    app.LightDirection()[0] = -0.5f;
    app.LightDirection()[1] = -1.0f;
    app.LightDirection()[2] = 0.5f;
    app.LightColor()[0] = 0.25f;
    app.LightColor()[1] = 0.5f;
    app.LightColor()[2] = 0.75f;
    Check(app.SceneLight().Ambient.r == 0.25f && app.SceneLight().Ambient.g == 0.5f &&
              app.SceneLight().Ambient.b == 0.75f,
          "PMM RGB and device Ambient are the same live value, as in original MMD");
    app.SceneLight().Ambient.r = 0.125f;
    Check(app.LightColor()[0] == 0.125f, "Device light color changes are visible to PMM writers");
    app.ApplyTimelineLightState();
    Check(app.SceneLight().Type == D3DLIGHT_DIRECTIONAL && app.SceneLight().Specular.r == 0.125f &&
              app.SceneLight().Specular.g == 0.5f && app.SceneLight().Direction.x == -0.5f &&
              app.SceneLight().Direction.z == 0.5f,
          "Track application updates the same directional light used by rendering");
    app.LightViewProjection()._11 = 2;
    app.WorldViewProjection()._22 = 3;
    app.ViewRotationTransform()._33 = 4;
    app.SceneLight() = {};
    Check(app.LightColor()[0] == 0 && app.CameraFov() == 30 && app.LightDirection()[0] == -0.5f &&
              app.LightViewProjection()._11 == 2 && app.WorldViewProjection()._22 == 3 &&
              app.ViewRotationTransform()._33 == 4,
          "Light initialization cannot overwrite camera, direction or matrix storage");
    Check(other.LightViewProjection()._11 == 0 && other.WorldViewProjection()._22 == 0 &&
              other.ViewRotationTransform()._33 == 0,
          "Render workspaces are initialized and isolated per application");
    std::memset(&app.SceneLight(), 0xCD, sizeof(D3DLIGHT9));
    app.ResetSceneLight();
    // Original x64 reset at RVA 0x745FB..0x7468D, all 26 dwords of D3DLIGHT9.
    constexpr std::uint32_t expected[] = {
        3,          0,          0,          0, 0, 0x3F1A1CAC, 0x3F1A1CAC, 0x3F1A1CAC, 0,
        0x3F1A1CAC, 0x3F1A1CAC, 0x3F1A1CAC, 0, 0, 0,          0,          0xBF000000, 0xBF800000,
        0x3F000000, 0,          0,          0, 0, 0,          0,          0};
    static_assert(sizeof expected == sizeof(D3DLIGHT9));
    Check(std::memcmp(&app.SceneLight(), expected, sizeof expected) == 0 &&
              app.LightDirection()[0] == -0.5f && app.LightDirection()[1] == -1.0f &&
              app.LightDirection()[2] == 0.5f && app.CameraFov() == 30 &&
              app.LightViewProjection()._11 == 2,
          "Default light matches the original complete 104-byte state and preserves neighbours");
}
} // namespace

int main() {
    auto app = std::make_unique<MMDApp>();
    auto other = std::make_unique<MMDApp>();
    app->state = {};
    other->state = {};
    app->InitDefaults();
    Check(!app->EdgeThicknessDialog() && !app->FrameCopyOrPhysicsDialog() &&
              !app->EnglishNameDialog() && !app->GroundShadowColorDialog(),
          "Dialog handles start empty on both architectures");
    app->RotationDialogTemp(0) = 1.25f;
    app->CameraFrameScratch()[171] = 42;
    app->BoneFrameScratch()[139] = 17;
    Check(other->RotationDialogTemp(0) == 0.0f && other->CameraFrameScratch()[171] == 0 &&
              other->BoneFrameScratch()[139] == 0,
          "Dialog workspaces belong to each application instance");
    TestClipboardRelease(*app);
    TestSelection(*app, *other);
    TestLightAndMatrices(*app, *other);
    if (!failures)
        std::puts("Application runtime state: initialization, isolation and cleanup pass");
    return failures ? 1 : 0;
}
