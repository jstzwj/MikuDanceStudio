#pragma once
#include "mikudancestudio/vmd_format.hpp"
#include "mikudancestudio/scene_session.hpp"
#include <string>
namespace mikudancestudio::format {
// Decode the same camera curve ordering used by the Windows registrar.
bool DecodeVmdSceneKey(const VmdCameraKey&, PmmCameraKey&);
bool DecodeVmdSceneKey(const VmdLightKey&, PmmLightKey&);
bool DecodeVmdSceneKey(const VmdShadowKey&, PmmSelfShadowKey&);
// Merge by frame, last duplicate wins. This replaces runtime tracks, not the
// original PMM byte envelope; SceneSession::Save retains its stale-envelope guard.
bool ApplyVmdSceneTracks(SceneSession&, const VmdDocument&, std::string& reason);
}
