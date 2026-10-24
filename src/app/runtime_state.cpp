#include "mikudancestudio/mmd_app.hpp"

#include <cstdlib>
#include <cstring>

namespace mikudancestudio {

void MMDApp::ResetSceneLight() noexcept {
    auto& render = m_runtime.render;
    render.lightDirection[0] = -0.5f;
    render.lightDirection[1] = -1.0f;
    render.lightDirection[2] = 0.5f;
    auto& light = render.sceneLight;
    light = {};
    light.Type = D3DLIGHT_DIRECTIONAL;
    light.Direction = {-0.5f, -1.0f, 0.5f};
    // Immediate 0x3F1A1CAC in the original, including PMM's live Ambient.rgb.
    light.Specular = {0.602f, 0.602f, 0.602f, 0.0f};
    light.Ambient = light.Specular;
}

void MMDApp::ReleaseClipboardBuffers() noexcept {
    auto& clipboard = m_runtime.clipboard;
    if (clipboard.displays != nullptr) {
        for (std::uint32_t i = 0; i < clipboard.counts.displays; ++i) {
            std::free(clipboard.displays[i].ikStates);
            std::free(clipboard.displays[i].selectorStates);
        }
    }
    std::free(clipboard.displays);
    std::free(clipboard.lights);
    std::free(clipboard.cameras);
    std::free(clipboard.morphs);
    std::free(clipboard.shadows);
    std::free(clipboard.gravity);
    std::free(clipboard.accessories);
    std::free(clipboard.bones);
    // Bone poses use operator new in bone_pose_clipboard.cpp, unlike frame buffers.
    ::operator delete(clipboard.bonePoses);
    clipboard = {};
}

void MMDApp::ReleaseTimelineSelectionRecords() noexcept {
    // Original x64 RVA 0x3D698..0x3D747 releases these seven bands in order.
    // Also release gravity, whose separately allocated array otherwise leaks.
    constexpr TimelineSelectionBand order[] = {
        TimelineSelectionBand::Accessory,  TimelineSelectionBand::ModelIk,
        TimelineSelectionBand::Camera,     TimelineSelectionBand::Light,
        TimelineSelectionBand::ModelBone,  TimelineSelectionBand::SelfShadow,
        TimelineSelectionBand::ModelMorph, TimelineSelectionBand::Gravity};
    for (auto band : order) {
        auto& selection = m_runtime.selection.timeline[static_cast<std::size_t>(band)];
        ::operator delete(selection.records);
        selection = {};
    }
}

void MMDApp::ClearPanelRowFlags() noexcept {
    std::memset(m_runtime.selection.panelRowFlags, 0, sizeof m_runtime.selection.panelRowFlags);
}

} // namespace mikudancestudio
