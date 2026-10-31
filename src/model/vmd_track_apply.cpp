#include "mikudancestudio/vmd_track_apply.hpp"

#include <cstdint>
#include <cstring>

#include "mikudancestudio/mmd_app.hpp"
#include "mikudancestudio/ported_funcs.hpp"
#include "mikudancestudio/vmd_format.hpp"
#include "mikudancestudio/vmd_scene_tracks.hpp"

namespace mikudancestudio {

void ApplyVmdGlobalTracks(MMDApp* app, const format::VmdDocument& motion) {
    bool cameraOk = true;
    for (const format::VmdCameraKey& key : motion.cameras) {
        mdl::CameraClipboardRecord rec{};
        format::PmmCameraKey decoded;
        if (!format::DecodeVmdSceneKey(key, decoded)) {
            cameraOk = false;
            continue;
        }
        rec.frame = decoded.frame;
        rec.distance = decoded.distance;
        rec.parentModel = decoded.parentModel;
        std::memcpy(rec.eye, decoded.eye.data(), sizeof rec.eye);
        std::memcpy(rec.target, decoded.target.data(), sizeof rec.target);
        std::memcpy(rec.interpolation, decoded.interpolation.data(), sizeof rec.interpolation);
        rec.fov = decoded.fov;
        rec.perspective = decoded.perspective;
        if (cameraOk && !RegisterCameraKey(app, rec))
            cameraOk = false;
    }
    bool lightOk = true;
    for (const format::VmdLightKey& key : motion.lights) {
        mdl::LightClipboardRecord rec{};
        rec.frame = key.frame;
        std::memcpy(rec.color, key.color, sizeof rec.color);
        std::memcpy(rec.direction, key.direction, sizeof rec.direction);
        if (lightOk && !RegisterLightKey(app, rec))
            lightOk = false;
    }
    bool shadowOk = true;
    for (const format::VmdShadowKey& key : motion.shadows) {
        mdl::ShadowClipboardRecord rec{};
        rec.frame = key.frame;
        rec.mode = key.mode;
        rec.distance = key.distance;
        if (shadowOk && !RegisterSelfShadowKey(app, rec))
            shadowOk = false;
    }
}

} // namespace mikudancestudio
