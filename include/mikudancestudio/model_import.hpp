#pragma once
namespace mikudancestudio {
// Explicit import ownership. CpuOnly preserves source paths and runtime CPU
// records; it never creates or borrows a graphics device/resource. The host
// subsequently publishes geometry/textures to its chosen SceneRenderer.
enum class ModelResourceMode {
    LegacyDx9,
    CpuOnly
};
}
