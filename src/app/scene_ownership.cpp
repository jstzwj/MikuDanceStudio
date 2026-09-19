// ===========================================================================
// MikuDanceStudio - scene-resident allocation teardown
// ===========================================================================
#include <cstdio>
#include <cstdlib>
#include <new>

#include "mikudancestudio/mmd_app.hpp"
#include "mikudancestudio/ported_funcs.hpp"
#include "mikudancestudio/scene_ownership.hpp"

namespace mikudancestudio {

namespace {

// Porting-era trace under MIKUDANCESTUDIO_PMM_TRACE_DIR (CMake option
// MIKUDANCESTUDIO_DIAG, default OFF); the OFF stub keeps the call sites
// valid and inlines away to nothing.
#ifdef MIKUDANCESTUDIO_DIAG
void TraceAccessoryRelease(int slot, const char* phase, const void* track) {
    const char* directory = std::getenv("MIKUDANCESTUDIO_PMM_TRACE_DIR");
    if (directory == nullptr || directory[0] == '\0')
        return;
    char path[MAX_PATH];
    sprintf_s(path, "%s\\pmm_model_load.log", directory);
    FILE* stream = nullptr;
    if (fopen_s(&stream, path, "ab") != 0 || stream == nullptr)
        return;
    fprintf(stream, "stage=accessory-track-%s slot=%d track=%p\r\n", phase,
            slot, track);
    fclose(stream);
}
#else
inline void TraceAccessoryRelease(int, const char*, const void*) {}
#endif

}  // namespace

void ReleaseSceneModels(MMDApp& app) {
    // x64 teardown sub_7FF7CB42BD40+0x42C130: 255-count do/while over the
    // model slots (app+0xBE8), each entry through sub_7FF7CB4C8D50
    // (ModelDispose twin) then operator delete - kModelSlotCount wide.
    for (int slot = 0; slot < kModelSlotCount; ++slot) {
        unsigned char*& model = app.ModelSlot(slot);
        if (model == nullptr)
            continue;
        ModelDispose(model);
        ::operator delete(model);
        model = nullptr;
    }
}

void ReleaseGlobalTimelineTracks(MMDApp& app) {
    if (app.CameraKeys() != nullptr) {
        ::operator delete(app.CameraKeys());
        app.CameraKeys() = nullptr;
    }
    if (app.LightKeys() != nullptr) {
        ::operator delete(app.LightKeys());
        app.LightKeys() = nullptr;
    }
    if (app.ShadowKeys() != nullptr) {
        ::operator delete(app.ShadowKeys());
        app.ShadowKeys() = nullptr;
    }
    if (app.GravityKeys() != nullptr) {
        ::operator delete(app.GravityKeys());
        app.GravityKeys() = nullptr;
    }
}

void ReleaseAccessoriesAndTracks(MMDApp& app) {
    for (int slot = 0; slot < 255; ++slot) {
        mdl::AccessoryRecord*& accessory = app.AccessorySlot(slot);
        if (accessory != nullptr) {
            DisposeAccessory(accessory);
            ::operator delete(accessory);
            accessory = nullptr;
        }
        mdl::AccessoryKey*& track = app.AccessoryKeys(slot);
        if (track != nullptr) {
            TraceAccessoryRelease(slot, "before-free", track);
            ::operator delete(track);
            TraceAccessoryRelease(slot, "after-free", nullptr);
            track = nullptr;
        }
    }
}

}  // namespace mikudancestudio
