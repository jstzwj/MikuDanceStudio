#pragma once
#include "mikudancestudio/pmm_scene_document.hpp"

namespace mikudancestudio {
class D3DRenderer;
class MMDApp;
struct PathResolutionWorkspace;
namespace format {
class SceneSession;
}

struct PmmV1FileLayoutContext {
    D3DRenderer* renderer = nullptr;
    PathResolutionWorkspace* paths = nullptr;
    bool modelUnavailable = false;
};

// Supplies the counts absent from PMM v1 using the same path search as the
// Windows model loader. A missing or unreadable PMD sets modelUnavailable so
// the caller can retain the loader's locate-and-retry dialog.
bool ResolvePmmV1FileLayout(void* context, std::uint8_t slot, const std::array<char, 20>& name,
                            const std::array<char, 256>& path, format::PmmModelLayout& layout);
// Reopens the PMM after the v1 loader has completed its graphical model
// recovery. Model resources are read from the paths actually selected by the
// user, indexed by their loaded runtime slots.
bool LoadPmmV1RuntimeSession(MMDApp* app, format::SceneSession& session);
}
