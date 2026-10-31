// P0 no-window capability inventory and transfer smoke test.
// Does not certify shaders, MRT drawing, sampling, MME or presentation (P3).
#ifdef _WIN32
#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <windows.h>
#else
#include <dlfcn.h>
#endif
#define VK_NO_PROTOTYPES
#include <vulkan/vulkan.h>
#include <array>
#include <cstdint>
#include <cstring>
#include <fstream>
#include <iostream>
#include <sstream>
#include <stdexcept>
#include <string>
#include <vector>

namespace {
void Check(VkResult result, const char* operation) {
    if (result != VK_SUCCESS)
        throw std::runtime_error(std::string(operation) + ":" + std::to_string(result));
}
std::string Quote(const std::string& value) {
    std::string result = "\"";
    for (const unsigned char c : value) {
        if (c == '"' || c == '\\') { result += '\\'; result += char(c); }
        else if (c < 32) result += '?';
        else result += char(c);
    }
    return result + '"';
}
std::string Version(std::uint32_t value) {
    return std::to_string(VK_VERSION_MAJOR(value)) + "." +
           std::to_string(VK_VERSION_MINOR(value)) + "." + std::to_string(VK_VERSION_PATCH(value));
}
struct Library {
    void* handle = nullptr;
    PFN_vkGetInstanceProcAddr get = nullptr;
    std::string path;
    explicit Library(const std::string& requested) {
#ifdef _WIN32
        path = requested.empty() ? "vulkan-1.dll" : requested;
        handle = LoadLibraryA(path.c_str());
        if (handle) get = reinterpret_cast<PFN_vkGetInstanceProcAddr>(
            GetProcAddress(static_cast<HMODULE>(handle), "vkGetInstanceProcAddr"));
#elif defined(__APPLE__)
        path = requested.empty() ? "libvulkan.1.dylib" : requested;
        handle = dlopen(path.c_str(), RTLD_NOW | RTLD_LOCAL);
        if (handle) get = reinterpret_cast<PFN_vkGetInstanceProcAddr>(dlsym(handle, "vkGetInstanceProcAddr"));
#else
        path = requested.empty() ? "libvulkan.so.1" : requested;
        handle = dlopen(path.c_str(), RTLD_NOW | RTLD_LOCAL);
        if (handle) get = reinterpret_cast<PFN_vkGetInstanceProcAddr>(dlsym(handle, "vkGetInstanceProcAddr"));
#endif
    }
    ~Library() {
        if (!handle) return;
#ifdef _WIN32
        FreeLibrary(static_cast<HMODULE>(handle));
#else
        dlclose(handle);
#endif
    }
    template <class T> T Load(VkInstance instance, const char* name) {
        const auto entry = get ? get(instance, name) : nullptr;
        if (!entry) throw std::runtime_error(std::string("entrypoint_unavailable:") + name);
        return reinterpret_cast<T>(entry);
    }
};
#define INSTANCE(name) const auto name = library.Load<PFN_##name>(instance, #name)
#define DEVICE_FUNCTIONS(X) \
    X(vkDestroyDevice) X(vkDeviceWaitIdle) X(vkGetDeviceQueue) \
    X(vkCreateImage) X(vkDestroyImage) X(vkGetImageMemoryRequirements) X(vkBindImageMemory) \
    X(vkCreateBuffer) X(vkDestroyBuffer) X(vkGetBufferMemoryRequirements) X(vkBindBufferMemory) \
    X(vkAllocateMemory) X(vkFreeMemory) X(vkMapMemory) X(vkUnmapMemory) X(vkInvalidateMappedMemoryRanges) \
    X(vkCreateCommandPool) X(vkDestroyCommandPool) X(vkAllocateCommandBuffers) \
    X(vkBeginCommandBuffer) X(vkEndCommandBuffer) X(vkCmdPipelineBarrier) \
    X(vkCmdClearColorImage) X(vkCmdCopyImageToBuffer) X(vkQueueSubmit) \
    X(vkCreateFence) X(vkDestroyFence) X(vkWaitForFences)
struct Device {
    VkDevice handle = VK_NULL_HANDLE;
#define DECLARE(name) PFN_##name name = nullptr;
    DEVICE_FUNCTIONS(DECLARE)
#undef DECLARE
    ~Device() {
        if (handle && vkDestroyDevice) {
            if (vkDeviceWaitIdle) vkDeviceWaitIdle(handle);
            vkDestroyDevice(handle, nullptr);
        }
    }
};
struct Transfer {
    Device& d;
    VkImage image = VK_NULL_HANDLE;
    VkBuffer buffer = VK_NULL_HANDLE;
    VkDeviceMemory imageMemory = VK_NULL_HANDLE, bufferMemory = VK_NULL_HANDLE;
    VkCommandPool pool = VK_NULL_HANDLE;
    VkFence fence = VK_NULL_HANDLE;
    explicit Transfer(Device& device) : d(device) {}
    ~Transfer() {
        d.vkDeviceWaitIdle(d.handle);
        if (fence) d.vkDestroyFence(d.handle, fence, nullptr);
        if (pool) d.vkDestroyCommandPool(d.handle, pool, nullptr);
        if (image) d.vkDestroyImage(d.handle, image, nullptr);
        if (buffer) d.vkDestroyBuffer(d.handle, buffer, nullptr);
        if (imageMemory) d.vkFreeMemory(d.handle, imageMemory, nullptr);
        if (bufferMemory) d.vkFreeMemory(d.handle, bufferMemory, nullptr);
    }
};
unsigned MemoryType(const VkPhysicalDeviceMemoryProperties& memory, unsigned mask,
                    VkMemoryPropertyFlags required) {
    for (unsigned i = 0; i < memory.memoryTypeCount; ++i)
        if ((mask & (1u << i)) && (memory.memoryTypes[i].propertyFlags & required) == required)
            return i;
    throw std::runtime_error("memory_type_unavailable");
}
bool Readback(Device& d, VkPhysicalDeviceMemoryProperties memory, unsigned family,
              VkFormat format, const VkClearColorValue& color) {
    Transfer t(d);
    const bool half = format == VK_FORMAT_R16G16B16A16_SFLOAT;
    const unsigned byteCount = 3 * 2 * (half ? 8 : 4);
    VkImageCreateInfo image{VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO};
    image.imageType = VK_IMAGE_TYPE_2D;
    image.format = format;
    image.extent = {3, 2, 1};
    image.mipLevels = image.arrayLayers = 1;
    image.samples = VK_SAMPLE_COUNT_1_BIT;
    image.tiling = VK_IMAGE_TILING_OPTIMAL;
    image.usage = VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT | VK_IMAGE_USAGE_SAMPLED_BIT |
                  VK_IMAGE_USAGE_TRANSFER_SRC_BIT | VK_IMAGE_USAGE_TRANSFER_DST_BIT;
    Check(d.vkCreateImage(d.handle, &image, nullptr, &t.image), "create_image");
    VkMemoryRequirements requirements;
    d.vkGetImageMemoryRequirements(d.handle, t.image, &requirements);
    VkMemoryAllocateInfo allocate{VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO};
    allocate.allocationSize = requirements.size;
    allocate.memoryTypeIndex = MemoryType(memory, requirements.memoryTypeBits, 0);
    Check(d.vkAllocateMemory(d.handle, &allocate, nullptr, &t.imageMemory), "allocate_image");
    Check(d.vkBindImageMemory(d.handle, t.image, t.imageMemory, 0), "bind_image");
    VkBufferCreateInfo buffer{VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO};
    buffer.size = byteCount;
    buffer.usage = VK_BUFFER_USAGE_TRANSFER_DST_BIT;
    Check(d.vkCreateBuffer(d.handle, &buffer, nullptr, &t.buffer), "create_buffer");
    d.vkGetBufferMemoryRequirements(d.handle, t.buffer, &requirements);
    allocate.allocationSize = requirements.size;
    allocate.memoryTypeIndex = MemoryType(memory, requirements.memoryTypeBits, VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT);
    Check(d.vkAllocateMemory(d.handle, &allocate, nullptr, &t.bufferMemory), "allocate_buffer");
    Check(d.vkBindBufferMemory(d.handle, t.buffer, t.bufferMemory, 0), "bind_buffer");
    VkCommandPoolCreateInfo pool{VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO};
    pool.queueFamilyIndex = family;
    Check(d.vkCreateCommandPool(d.handle, &pool, nullptr, &t.pool), "command_pool");
    VkCommandBufferAllocateInfo commands{VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO};
    commands.commandPool = t.pool;
    commands.level = VK_COMMAND_BUFFER_LEVEL_PRIMARY;
    commands.commandBufferCount = 1;
    VkCommandBuffer command;
    Check(d.vkAllocateCommandBuffers(d.handle, &commands, &command), "command_buffer");
    VkCommandBufferBeginInfo begin{VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO};
    Check(d.vkBeginCommandBuffer(command, &begin), "begin_commands");
    VkImageMemoryBarrier barrier{VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER};
    barrier.srcQueueFamilyIndex = barrier.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
    barrier.image = t.image;
    barrier.subresourceRange = {VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, 0, 1};
    barrier.newLayout = VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL;
    barrier.dstAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT;
    d.vkCmdPipelineBarrier(command, VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT, VK_PIPELINE_STAGE_TRANSFER_BIT,
                           0, 0, nullptr, 0, nullptr, 1, &barrier);
    d.vkCmdClearColorImage(command, t.image, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, &color, 1,
                           &barrier.subresourceRange);
    barrier.oldLayout = barrier.newLayout;
    barrier.newLayout = VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL;
    barrier.srcAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT;
    barrier.dstAccessMask = VK_ACCESS_TRANSFER_READ_BIT;
    d.vkCmdPipelineBarrier(command, VK_PIPELINE_STAGE_TRANSFER_BIT, VK_PIPELINE_STAGE_TRANSFER_BIT,
                           0, 0, nullptr, 0, nullptr, 1, &barrier);
    VkBufferImageCopy copy{};
    copy.imageSubresource = {VK_IMAGE_ASPECT_COLOR_BIT, 0, 0, 1};
    copy.imageExtent = {3, 2, 1};
    d.vkCmdCopyImageToBuffer(command, t.image, VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL, t.buffer, 1, &copy);
    VkBufferMemoryBarrier host{VK_STRUCTURE_TYPE_BUFFER_MEMORY_BARRIER};
    host.srcAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT;
    host.dstAccessMask = VK_ACCESS_HOST_READ_BIT;
    host.srcQueueFamilyIndex = host.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
    host.buffer = t.buffer;
    host.size = VK_WHOLE_SIZE;
    d.vkCmdPipelineBarrier(command, VK_PIPELINE_STAGE_TRANSFER_BIT, VK_PIPELINE_STAGE_HOST_BIT,
                           0, 0, nullptr, 1, &host, 0, nullptr);
    Check(d.vkEndCommandBuffer(command), "end_commands");
    VkFenceCreateInfo fence{VK_STRUCTURE_TYPE_FENCE_CREATE_INFO};
    Check(d.vkCreateFence(d.handle, &fence, nullptr, &t.fence), "fence");
    VkQueue queue;
    d.vkGetDeviceQueue(d.handle, family, 0, &queue);
    VkSubmitInfo submit{VK_STRUCTURE_TYPE_SUBMIT_INFO};
    submit.commandBufferCount = 1;
    submit.pCommandBuffers = &command;
    Check(d.vkQueueSubmit(queue, 1, &submit, t.fence), "submit");
    Check(d.vkWaitForFences(d.handle, 1, &t.fence, VK_TRUE, 30'000'000'000ull), "wait");
    void* mapped = nullptr;
    Check(d.vkMapMemory(d.handle, t.bufferMemory, 0, VK_WHOLE_SIZE, 0, &mapped), "map");
    VkMappedMemoryRange invalidate{VK_STRUCTURE_TYPE_MAPPED_MEMORY_RANGE};
    invalidate.memory = t.bufferMemory;
    invalidate.size = VK_WHOLE_SIZE;
    const auto invalidated = d.vkInvalidateMappedMemoryRanges(d.handle, 1, &invalidate);
    bool equal = invalidated == VK_SUCCESS;
    for (unsigned pixel = 0; pixel < 6 && equal; ++pixel) {
        const std::uint16_t halfExpected[]{0xb800, 0x4000, 0x3400, 0x3c00};
        const unsigned char byteExpected[]{255, 0, 255, 0};
        equal = half ? std::memcmp(static_cast<const char*>(mapped) + pixel * 8, halfExpected, 8) == 0
                     : std::memcmp(static_cast<const char*>(mapped) + pixel * 4, byteExpected, 4) == 0;
    }
    d.vkUnmapMemory(d.handle, t.bufferMemory);
    Check(invalidated, "invalidate");
    return equal;
}

bool Probe(Library& library, std::ostream& output, bool requireHardware) {
    VkInstance instance = VK_NULL_HANDLE;
    INSTANCE(vkEnumerateInstanceExtensionProperties);
    INSTANCE(vkCreateInstance);
    std::uint32_t count = 0;
    Check(vkEnumerateInstanceExtensionProperties(nullptr, &count, nullptr), "instance_extensions");
    std::vector<VkExtensionProperties> available(count);
    Check(vkEnumerateInstanceExtensionProperties(nullptr, &count, available.data()), "instance_extensions");
    bool portability = false;
    for (const auto& extension : available)
        portability |= std::strcmp(extension.extensionName, VK_KHR_PORTABILITY_ENUMERATION_EXTENSION_NAME) == 0;
    const char* extension = VK_KHR_PORTABILITY_ENUMERATION_EXTENSION_NAME;
    auto version = VK_API_VERSION_1_0;
    if (const auto enumerate = reinterpret_cast<PFN_vkEnumerateInstanceVersion>(library.get(nullptr, "vkEnumerateInstanceVersion")))
        Check(enumerate(&version), "instance_version");
    if (version < VK_API_VERSION_1_2) throw std::runtime_error("probe_requires_vulkan_1_2");
    VkApplicationInfo app{VK_STRUCTURE_TYPE_APPLICATION_INFO};
    app.pApplicationName = "MDS P0 offscreen probe";
    app.apiVersion = version < VK_API_VERSION_1_3 ? VK_API_VERSION_1_2 : VK_API_VERSION_1_3;
    VkInstanceCreateInfo create{VK_STRUCTURE_TYPE_INSTANCE_CREATE_INFO};
    create.pApplicationInfo = &app;
    if (portability) {
        create.flags = VK_INSTANCE_CREATE_ENUMERATE_PORTABILITY_BIT_KHR;
        create.enabledExtensionCount = 1;
        create.ppEnabledExtensionNames = &extension;
    }
    Check(vkCreateInstance(&create, nullptr, &instance), "create_instance");
    INSTANCE(vkDestroyInstance);
    struct InstanceOwner { VkInstance instance; PFN_vkDestroyInstance destroy;
        ~InstanceOwner() { destroy(instance, nullptr); } } owner{instance, vkDestroyInstance};
    INSTANCE(vkEnumeratePhysicalDevices);
    INSTANCE(vkGetPhysicalDeviceProperties2);
    INSTANCE(vkGetPhysicalDeviceFeatures2);
    INSTANCE(vkGetPhysicalDeviceFormatProperties);
    INSTANCE(vkGetPhysicalDeviceMemoryProperties);
    INSTANCE(vkGetPhysicalDeviceQueueFamilyProperties);
    INSTANCE(vkEnumerateDeviceExtensionProperties);
    INSTANCE(vkCreateDevice);
    INSTANCE(vkGetDeviceProcAddr);
    Check(vkEnumeratePhysicalDevices(instance, &count, nullptr), "physical_devices");
    std::vector<VkPhysicalDevice> physicals(count);
    Check(vkEnumeratePhysicalDevices(instance, &count, physicals.data()), "physical_devices");
    output << "\"loader_version\":" << Quote(Version(version))
           << ",\"portability_enumeration\":" << portability << ",\"devices\":[";
    bool usable = false;
    for (unsigned index = 0; index < physicals.size(); ++index) {
        if (index) output << ',';
        const auto physical = physicals[index];
        VkPhysicalDeviceFloatControlsProperties floats{VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_FLOAT_CONTROLS_PROPERTIES};
        VkPhysicalDeviceDriverProperties driver{VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_DRIVER_PROPERTIES};
        driver.pNext = &floats;
        VkPhysicalDeviceProperties2 properties{VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_PROPERTIES_2};
        properties.pNext = &driver;
        vkGetPhysicalDeviceProperties2(physical, &properties);
        const auto& p = properties.properties;
        VkPhysicalDeviceVulkan13Features f13{VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_VULKAN_1_3_FEATURES};
        VkPhysicalDeviceVulkan12Features f12{VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_VULKAN_1_2_FEATURES};
        f12.pNext = p.apiVersion >= VK_API_VERSION_1_3 ? &f13 : nullptr;
        VkPhysicalDeviceFeatures2 features{VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_FEATURES_2};
        features.pNext = &f12;
        vkGetPhysicalDeviceFeatures2(physical, &features);
        const bool software = p.deviceType == VK_PHYSICAL_DEVICE_TYPE_CPU;
        const bool eligible = p.apiVersion >= VK_API_VERSION_1_3 && f13.dynamicRendering &&
            f13.synchronization2 && f13.shaderDemoteToHelperInvocation && f12.timelineSemaphore &&
            floats.shaderSignedZeroInfNanPreserveFloat32 && features.features.samplerAnisotropy;
        output << "{\"name\":" << Quote(p.deviceName) << ",\"api\":" << Quote(Version(p.apiVersion))
               << ",\"driver\":" << Quote(driver.driverName) << ",\"driver_info\":" << Quote(driver.driverInfo)
               << ",\"software\":" << software << ",\"rhi13_feature_requirements\":" << eligible
               << ",\"dynamic_rendering\":" << bool(f13.dynamicRendering)
               << ",\"synchronization2\":" << bool(f13.synchronization2)
               << ",\"shader_demote\":" << bool(f13.shaderDemoteToHelperInvocation)
               << ",\"timeline_semaphore\":" << bool(f12.timelineSemaphore)
               << ",\"signed_zero_inf_nan_float32\":" << bool(floats.shaderSignedZeroInfNanPreserveFloat32)
               << ",\"anisotropy\":" << bool(features.features.samplerAnisotropy)
               << ",\"max_color_attachments\":" << p.limits.maxColorAttachments
               << ",\"max_sampled_images\":" << p.limits.maxPerStageDescriptorSampledImages
               << ",\"max_samplers\":" << p.limits.maxPerStageDescriptorSamplers << ",\"formats\":[";
        bool formats = true;
        for (const auto format : {VK_FORMAT_R8G8B8A8_UNORM, VK_FORMAT_R16G16B16A16_SFLOAT}) {
            VkFormatProperties info;
            vkGetPhysicalDeviceFormatProperties(physical, format, &info);
            const auto flags = info.optimalTilingFeatures;
            const auto required = VK_FORMAT_FEATURE_COLOR_ATTACHMENT_BIT | VK_FORMAT_FEATURE_SAMPLED_IMAGE_BIT |
                VK_FORMAT_FEATURE_TRANSFER_SRC_BIT | VK_FORMAT_FEATURE_TRANSFER_DST_BIT;
            formats &= (flags & required) == required;
            if (format != VK_FORMAT_R8G8B8A8_UNORM) output << ',';
            output << "{\"format\":" << Quote(format == VK_FORMAT_R8G8B8A8_UNORM ? "rgba8" : "rgba16f")
                   << ",\"render\":" << bool(flags & VK_FORMAT_FEATURE_COLOR_ATTACHMENT_BIT)
                   << ",\"sample\":" << bool(flags & VK_FORMAT_FEATURE_SAMPLED_IMAGE_BIT)
                   << ",\"linear_filter\":" << bool(flags & VK_FORMAT_FEATURE_SAMPLED_IMAGE_FILTER_LINEAR_BIT)
                   << ",\"blend\":" << bool(flags & VK_FORMAT_FEATURE_COLOR_ATTACHMENT_BLEND_BIT)
                   << ",\"transfer_src\":" << bool(flags & VK_FORMAT_FEATURE_TRANSFER_SRC_BIT)
                   << ",\"transfer_dst\":" << bool(flags & VK_FORMAT_FEATURE_TRANSFER_DST_BIT) << '}';
        }
        output << ']';
        vkGetPhysicalDeviceQueueFamilyProperties(physical, &count, nullptr);
        std::vector<VkQueueFamilyProperties> queues(count);
        vkGetPhysicalDeviceQueueFamilyProperties(physical, &count, queues.data());
        unsigned family = 0;
        while (family < queues.size() && !(queues[family].queueCount && (queues[family].queueFlags & VK_QUEUE_GRAPHICS_BIT))) ++family;
        bool readback = false;
        if (formats && family < queues.size() && (!requireHardware || !software)) {
            Check(vkEnumerateDeviceExtensionProperties(physical, nullptr, &count, nullptr), "device_extensions");
            std::vector<VkExtensionProperties> extensions(count);
            Check(vkEnumerateDeviceExtensionProperties(physical, nullptr, &count, extensions.data()), "device_extensions");
            const char* subset = "VK_KHR_portability_subset";
            bool hasSubset = false;
            for (const auto& e : extensions) hasSubset |= std::strcmp(e.extensionName, subset) == 0;
            const float priority = 1;
            VkDeviceQueueCreateInfo q{VK_STRUCTURE_TYPE_DEVICE_QUEUE_CREATE_INFO};
            q.queueFamilyIndex = family;
            q.queueCount = 1;
            q.pQueuePriorities = &priority;
            VkDeviceCreateInfo device{VK_STRUCTURE_TYPE_DEVICE_CREATE_INFO};
            device.queueCreateInfoCount = 1;
            device.pQueueCreateInfos = &q;
            if (hasSubset) { device.enabledExtensionCount = 1; device.ppEnabledExtensionNames = &subset; }
            Device d;
            Check(vkCreateDevice(physical, &device, nullptr, &d.handle), "create_device");
#define LOAD(name) d.name = reinterpret_cast<PFN_##name>(vkGetDeviceProcAddr(d.handle, #name)); \
            if (!d.name) throw std::runtime_error("device_entrypoint_unavailable:" #name);
            DEVICE_FUNCTIONS(LOAD)
#undef LOAD
            VkPhysicalDeviceMemoryProperties memory;
            vkGetPhysicalDeviceMemoryProperties(physical, &memory);
            VkClearColorValue rgba8{{1, 0, 1, 0}}, rgba16{{-.5f, 2, .25f, 1}};
            readback = Readback(d, memory, family, VK_FORMAT_R8G8B8A8_UNORM, rgba8) &&
                       Readback(d, memory, family, VK_FORMAT_R16G16B16A16_SFLOAT, rgba16);
            if (!readback) throw std::runtime_error("raw_clear_readback_mismatch");
        }
        usable |= readback;
        output << ",\"raw_clear_readback\":" << readback << '}';
    }
    output << ']';
    return usable;
}
}

int main(int argc, char** argv) {
#ifdef _WIN32
    SetErrorMode(SEM_FAILCRITICALERRORS | SEM_NOGPFAULTERRORBOX | SEM_NOOPENFILEERRORBOX);
#endif
    std::string path, libraryPath;
    bool required = false, hardware = false;
    for (int n = 1; n < argc; ++n) {
        const std::string arg(argv[n]);
        if (arg == "--output" && n + 1 < argc) path = argv[++n];
        else if (arg == "--library" && n + 1 < argc) libraryPath = argv[++n];
        else if (arg == "--require-device") required = true;
        else if (arg == "--require-hardware") required = hardware = true;
        else { std::cerr << "invalid probe argument\n"; return 2; }
    }
    std::ostringstream details;
    details << std::boolalpha;
    bool passed = false;
    std::string reason;
    Library library(libraryPath);
    try {
        if (!library.get) throw std::runtime_error("vulkan_library_unavailable");
        passed = Probe(library, details, hardware);
        if (!passed) reason = "no_eligible_readback_device";
    } catch (const std::exception& error) { reason = error.what(); }
    std::ostringstream report;
    report << "{\"schema\":1,\"scope\":\"P0 inventory and transfer only; no shaders or presentation\","
           << "\"status\":" << Quote(passed ? "passed" : "unavailable_or_failed")
           << ",\"reason\":" << Quote(reason) << ",\"library\":" << Quote(library.path);
    // A partial enumeration after an exception is diagnostic text, not JSON.
    if (passed || reason == "no_eligible_readback_device") report << ',' << details.str();
    else report << ",\"partial_inventory\":" << Quote(details.str());
    report << "}\n";
    if (!path.empty()) {
        std::ofstream file(path);
        file << report.str();
        if (!file) { std::cerr << "probe report write failed\n"; return 2; }
    }
    std::cout << report.str();
    return passed || !required ? 0 : 1;
}
