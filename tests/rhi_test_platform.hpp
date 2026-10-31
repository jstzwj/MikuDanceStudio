#pragma once
#include "mikudancestudio/rhi_backend.hpp"
#include <cstdlib>
// Explicit test-host configuration, never consulted by production factories.
inline void ConfigureVulkanTest(mikudancestudio::rhi::BackendOptions& options,
                                const std::string& argument) {
    if (argument.find("software") != std::string::npos)
        options.driver = mikudancestudio::rhi::BackendDriver::Software;
    if (const char* library = std::getenv("MDS_TEST_VULKAN_LIBRARY"))
        options.vulkanLibrary = library;
}
