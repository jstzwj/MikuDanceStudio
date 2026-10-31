#pragma once
#include "mikudancestudio/vulkan_surface.hpp"
#include <memory>
#include <string>
struct NativeSurfaceHost {
    virtual ~NativeSurfaceHost() = default;
    virtual bool Resize(unsigned physicalWidth, unsigned physicalHeight) = 0;
    virtual bool Pump() = 0;
    mikudancestudio::rhi::NativeWindow window;
    std::uintptr_t display = 0;
    unsigned width = 96, height = 64;
    bool suspended = false;
};
std::shared_ptr<NativeSurfaceHost> CreateTestSurfaceHost(const std::string& system);
