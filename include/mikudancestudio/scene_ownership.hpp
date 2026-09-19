// ===========================================================================
// MikuDanceStudio - ownership of scene-resident heap allocations
// ===========================================================================
#pragma once

namespace mikudancestudio {

class MMDApp;

// Preserve the reference executable's release order.  These are raw,
// zero-initialized blocks allocated with operator new and released with operator delete.
void ReleaseSceneModels(MMDApp& app);
void ReleaseGlobalTimelineTracks(MMDApp& app);
void ReleaseAccessoriesAndTracks(MMDApp& app);

}  // namespace mikudancestudio
