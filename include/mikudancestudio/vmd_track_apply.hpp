#pragma once

namespace mikudancestudio {
class MMDApp;
namespace format {
struct VmdDocument;
}

// Apply global VMD tracks through the existing Windows key registrars.
// UI repaint and model reload are owned by the caller.
void ApplyVmdGlobalTracks(MMDApp* app, const format::VmdDocument& motion);
}
