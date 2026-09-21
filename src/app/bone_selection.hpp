#pragma once

namespace mikudancestudio {
namespace mdl { struct ModelRecord; }

// Consume a viewport click using the bone markers' projected client positions.
bool PickBoneAtPoint(mdl::ModelRecord& model, int x, int y,
                     int physicsMode, bool additive);
}
