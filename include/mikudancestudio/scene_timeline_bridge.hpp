#pragma once
namespace mikudancestudio {
class MMDApp;
namespace mdl {
struct ModelRecord;
}
// Windows-only timeline bridge for a loaded PMM v1/v2 scene.
void SeekPortableSceneModel(mdl::ModelRecord* model, int frame);
void StartPortableScenePlayback(MMDApp* app);
void AdvancePortableScenePlayback(MMDApp* app);
void SeekPortableSceneCamera(MMDApp* app, int frame);
void ApplyPortableSceneCamera(MMDApp* app);
void SeekPortableSceneAccessory(MMDApp* app, int slot, int frame);
void ApplyPortableSceneAccessories(MMDApp* app);
void SeekPortableSceneSelfShadow(MMDApp* app, int frame);
void ApplyPortableSceneSelfShadow(MMDApp* app);
void SeekPortableSceneGravity(MMDApp* app, int frame);
void ApplyPortableSceneGravity(MMDApp* app);
}
