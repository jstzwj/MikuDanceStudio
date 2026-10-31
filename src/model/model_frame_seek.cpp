// Rebuild a model pose at an arbitrary frame, in display/morph/bone order.
// Architecture-specific expressions preserve the original rounding stages.
// Architecture and numerical compatibility rules: docs/X64_RECONSTRUCTION.md.
#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <Windows.h>

#include <cmath>
#include <cstdint>
#include <cstring>

#include "mikudancestudio/mmd_app.hpp"
#include "mikudancestudio/ported_funcs.hpp"
#include "mikudancestudio/model.hpp"

#include "native_keyframe_kernel.hpp"
#include "mikudancestudio/pmm_scene_evaluation.hpp"
#include "mikudancestudio/scene_timeline_bridge.hpp"

namespace mikudancestudio {
int SeekModelFrame(mdl::ModelRecord& model, int frameArg, int physicsMode) {
    const int result = NativeKeyframeKernel::SeekModelFrame(model, frameArg, physicsMode);
    SeekPortableSceneModel(&model, frameArg);
    return result;
}

// Seek the selected model and request reevaluation.
void SeekSelectedModelToCurrentFrame(MMDApp* app) {
    mdl::ModelRecord* model = app->SelectedModel();
    SeekModelFrame(*model, app->state.currentFrame, app->PlaybackPhysicsMode());
    app->RecordedFrameCountByte1() = 1;
}

} // namespace mikudancestudio
