#pragma once

namespace mikudancestudio {
class MMDApp;
enum class ViewportToolAction : int;
namespace mdl {
struct ModelRecord;
}

// Consume a viewport click using the bone markers' projected client positions.
bool PickBoneAtPoint(mdl::ModelRecord& model, int x, int y, int physicsMode, bool additive);
void SelectBonesInScreenRect(mdl::ModelRecord& model, int left, int top, int right, int bottom,
                             bool additive);
void FinishBoneBoxSelection(mdl::ModelRecord& model, bool additive);
// Consume the polled mouse edge before applying this frame's drag delta.
void BeginOrEndViewportToolDrag(MMDApp* app, ViewportToolAction operation);
}
