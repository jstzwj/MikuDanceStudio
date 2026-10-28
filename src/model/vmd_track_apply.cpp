#include "mikudancestudio/vmd_track_apply.hpp"

#include <cstdint>
#include <cstring>

#include "mikudancestudio/mmd_app.hpp"
#include "mikudancestudio/ported_funcs.hpp"
#include "mikudancestudio/vmd_format.hpp"

namespace mikudancestudio {

void ApplyVmdGlobalTracks(MMDApp* app, const format::VmdDocument& motion) {
    bool cameraOk = true;
    for (const format::VmdCameraKey& key : motion.cameras) {
        mdl::CameraClipboardRecord rec{};
        rec.frame = key.frame;
        rec.distance = key.distance;
        rec.parentModel = -1;
        std::memcpy(rec.eye, key.eye, sizeof rec.eye);
        std::memcpy(rec.target, key.target, sizeof rec.target);
        for (int channel = 0; channel < 6; ++channel) {
            rec.interpolation[channel] = key.interpolation[channel][0];
            rec.interpolation[12 + channel] = key.interpolation[channel][1];
            rec.interpolation[6 + channel] = key.interpolation[channel][2];
            rec.interpolation[18 + channel] = key.interpolation[channel][3];
        }
        rec.fov = static_cast<std::int32_t>(key.fov);
        rec.perspective = key.perspective != 0 ? 1 : 0;
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
