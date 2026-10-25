#pragma once

namespace mikudancestudio {
class MMDApp;
namespace mdl {
struct ModelRecord;
}

// Per-model submissions. The enclosing frame pass owns model order, the active
// object/pass and shared render states; these walks publish the material cursor.
void DrawModelMaterials(MMDApp* app, mdl::ModelRecord* model, bool effectPass, bool shadowOnly);
void DrawModelEdgeGeometry(MMDApp* app, mdl::ModelRecord* model, bool projectedShadow);
} // namespace mikudancestudio
