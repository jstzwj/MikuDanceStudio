#ifdef _WIN32
#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <Windows.h>
#define VK_USE_PLATFORM_WIN32_KHR
#else
#include <dlfcn.h>
#endif
#define VK_NO_PROTOTYPES
#include <vulkan/vulkan.h>
#include "mikudancestudio/rhi_vulkan.hpp"
#include "mikudancestudio/rhi_render_pass.hpp"
#include "rhi_model_constants.hpp"
#include "rhi_vulkan_spirv.hpp"
#include "rhi_shader_spirv.hpp"
#include <algorithm>
#include <array>
#include <atomic>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <limits>
#include <map>
#include <sstream>
#include <vector>

namespace mikudancestudio::rhi {
namespace {
constexpr unsigned FrameCount = 3, MaxDraws = 512, MaxResources = 8192;
constexpr VkDeviceSize UniformBytes = 2 * 1024 * 1024;
// clang-format off
#define INSTANCE_FUNCTIONS(X) \
    X(DestroyInstance) \
    X(EnumeratePhysicalDevices) \
    X(GetPhysicalDeviceProperties) \
    X(GetPhysicalDeviceFeatures2) \
    X(GetPhysicalDeviceProperties2) \
    X(GetPhysicalDeviceQueueFamilyProperties) \
    X(GetPhysicalDeviceMemoryProperties) \
    X(GetPhysicalDeviceFormatProperties) \
    X(CreateDevice) \
    X(GetDeviceProcAddr)
#define DEVICE_FUNCTIONS(X) \
    X(DestroyDevice) \
    X(GetDeviceQueue) \
    X(DeviceWaitIdle) \
    X(CreateSemaphore) \
    X(DestroySemaphore) \
    X(GetSemaphoreCounterValue) \
    X(WaitSemaphores) \
    X(SignalSemaphore) \
    X(QueueSubmit2) \
    X(CreateCommandPool) \
    X(DestroyCommandPool) \
    X(ResetCommandPool) \
    X(AllocateCommandBuffers) \
    X(BeginCommandBuffer) \
    X(EndCommandBuffer) \
    X(CmdPipelineBarrier2) \
    X(CreateBuffer) \
    X(DestroyBuffer) \
    X(GetBufferMemoryRequirements) \
    X(AllocateMemory) \
    X(FreeMemory) \
    X(BindBufferMemory) \
    X(MapMemory) \
    X(UnmapMemory) \
    X(CreateImage) \
    X(DestroyImage) \
    X(GetImageMemoryRequirements) \
    X(BindImageMemory) \
    X(CreateImageView) \
    X(DestroyImageView) \
    X(CmdCopyBufferToImage) \
    X(CmdCopyImageToBuffer) \
    X(CmdCopyImage) \
    X(CmdBlitImage) \
    X(CmdBeginRendering) \
    X(CmdEndRendering) \
    X(CreateDescriptorSetLayout) \
    X(DestroyDescriptorSetLayout) \
    X(CreatePipelineLayout) \
    X(DestroyPipelineLayout) \
    X(CreateDescriptorPool) \
    X(DestroyDescriptorPool) \
    X(ResetDescriptorPool) \
    X(AllocateDescriptorSets) \
    X(UpdateDescriptorSets) \
    X(CreateShaderModule) \
    X(DestroyShaderModule) \
    X(CreateGraphicsPipelines) \
    X(DestroyPipeline) \
    X(CreateSampler) \
    X(DestroySampler) \
    X(CmdBindPipeline) \
    X(CmdSetViewport) \
    X(CmdSetScissor) \
    X(CmdSetStencilReference) \
    X(CmdBindDescriptorSets) \
    X(CmdBindVertexBuffers) \
    X(CmdBindIndexBuffer) \
    X(CmdDraw) \
    X(CmdDrawIndexed)
// clang-format on
struct Buffer {
    VkBuffer native = VK_NULL_HANDLE;
    VkDeviceMemory memory = VK_NULL_HANDLE;
    void* mapped = nullptr;
};
struct Resource {
    std::uint32_t generation = 1;
    bool alive = false, uploaded = false;
    std::uint64_t lastUse = 0;
    BufferDesc description{};
    Buffer buffer;
    SnapshotBytes indices;
    TextureDesc texture{};
    unsigned mips = 1;
    VkImage image = VK_NULL_HANDLE;
    VkDeviceMemory memory = VK_NULL_HANDLE;
    VkImageView view = VK_NULL_HANDLE;
    VkFormat format = VK_FORMAT_UNDEFINED;
    VkImageLayout layout = VK_IMAGE_LAYOUT_UNDEFINED;
    VkPipelineStageFlags2 stage = VK_PIPELINE_STAGE_2_NONE;
    VkAccessFlags2 access = VK_ACCESS_2_NONE;
};
struct Frame {
    VkSemaphore acquired = VK_NULL_HANDLE;
    VkCommandPool pool = VK_NULL_HANDLE;
    VkCommandBuffer commands = VK_NULL_HANDLE;
    VkDescriptorPool descriptors = VK_NULL_HANDLE;
    Buffer uniforms;
    VkDeviceSize offset = 0;
    std::uint64_t completion = 0;
    unsigned draws = 0;
};
VkFormat Format(TextureFormat f) {
    if (f == TextureFormat::Rgba16Float)
        return VK_FORMAT_R16G16B16A16_SFLOAT;
    return f == TextureFormat::Rgba8      ? VK_FORMAT_R8G8B8A8_UNORM
           : f == TextureFormat::R32Float ? VK_FORMAT_R32_SFLOAT
                                          : VK_FORMAT_D24_UNORM_S8_UINT;
}
VkImageAspectFlags Aspect(const Resource& r) {
    return r.format == VK_FORMAT_D24_UNORM_S8_UINT
               ? VK_IMAGE_ASPECT_DEPTH_BIT | VK_IMAGE_ASPECT_STENCIL_BIT
               : VK_IMAGE_ASPECT_COLOR_BIT;
}
VkBlendFactor Blend(BlendFactor f) {
    constexpr VkBlendFactor values[] = {VK_BLEND_FACTOR_ZERO,
                                        VK_BLEND_FACTOR_ONE,
                                        VK_BLEND_FACTOR_SRC_COLOR,
                                        VK_BLEND_FACTOR_ONE_MINUS_SRC_COLOR,
                                        VK_BLEND_FACTOR_SRC_ALPHA,
                                        VK_BLEND_FACTOR_ONE_MINUS_SRC_ALPHA,
                                        VK_BLEND_FACTOR_DST_ALPHA,
                                        VK_BLEND_FACTOR_ONE_MINUS_DST_ALPHA,
                                        VK_BLEND_FACTOR_DST_COLOR,
                                        VK_BLEND_FACTOR_ONE_MINUS_DST_COLOR,
                                        VK_BLEND_FACTOR_SRC_ALPHA_SATURATE};
    return values[unsigned(f)];
}
class VulkanDevice final : public Device {
public:
    void* library = nullptr;
    PFN_vkGetInstanceProcAddr vkGetInstanceProcAddr = nullptr;
    PFN_vkCreateInstance vkCreateInstance = nullptr;
    PFN_vkEnumerateInstanceLayerProperties vkEnumerateInstanceLayerProperties = nullptr;
    PFN_vkEnumerateInstanceExtensionProperties vkEnumerateInstanceExtensionProperties = nullptr;
#define DECLARE(name) PFN_vk##name vk##name = nullptr;
    INSTANCE_FUNCTIONS(DECLARE)
    DEVICE_FUNCTIONS(DECLARE)
#undef DECLARE
    PFN_vkCreateDebugUtilsMessengerEXT vkCreateDebugUtilsMessengerEXT = nullptr;
    PFN_vkDestroyDebugUtilsMessengerEXT vkDestroyDebugUtilsMessengerEXT = nullptr;
    VkInstance instance = VK_NULL_HANDLE;
    VkPhysicalDevice physical = VK_NULL_HANDLE;
    VkDevice native = VK_NULL_HANDLE;
    VkQueue queue = VK_NULL_HANDLE;
    VkDebugUtilsMessengerEXT messenger = VK_NULL_HANDLE;
    VkSemaphore timeline = VK_NULL_HANDLE;
    VkSemaphore testGate = VK_NULL_HANDLE;
    std::uint64_t gateValue = 0;
    bool gateBlocked = false;
    VkCommandPool transferPool = VK_NULL_HANDLE;
    VkCommandBuffer transfer = VK_NULL_HANDLE;
    VkDescriptorSetLayout descriptorLayout = VK_NULL_HANDLE;
    VkDescriptorSetLayout genericDescriptorLayout = VK_NULL_HANDLE;
    VkPipelineLayout genericPipelineLayout = VK_NULL_HANDLE;
    struct GenericProgram {
        VkShaderModule vertex = VK_NULL_HANDLE, pixel = VK_NULL_HANDLE;
    };
    std::map<std::string, GenericProgram> genericPrograms;
    ShaderPipeline genericPipeline;
    ShaderResourceBindings genericBindings;
    bool genericReady = false, genericBound = false;
    std::string genericError;
    ShaderCapabilities ShaderFeatures() const override {
        auto caps = GraphicsShaderProfile(detail::RuntimeSpirvCompilerAvailable(), true);
        caps.maxConstantBytes =
            (std::min)(caps.maxConstantBytes, properties.limits.maxUniformBufferRange);
        caps.constantAlignment = unsigned(
            (std::max)(VkDeviceSize(1), properties.limits.minUniformBufferOffsetAlignment));
        const auto aligned = [&](unsigned bytes) {
            return (bytes + caps.constantAlignment - 1) / caps.constantAlignment *
                   caps.constantAlignment;
        };
        caps.legacyDrawConstantBytes =
            aligned(sizeof(ModelVertexConstants)) + aligned(sizeof(detail::ModelPixelConstants)) +
            aligned(sizeof(BuiltinShadowConstants)) + aligned(sizeof(detail::PostProcessConstants));
        caps.maxFrameConstantBytes = UniformBytes;
        caps.maxColorAttachments = (std::min)(4u, properties.limits.maxColorAttachments);
        return caps;
    }
    TextureFormatCapabilities FormatFeatures(TextureFormat f) const override {
        if (!physical || !TexturePixelBytes(f))
            return {};
        VkFormatProperties data{};
        vkGetPhysicalDeviceFormatProperties(physical, Format(f), &data);
        const auto flags = data.optimalTilingFeatures;
        const bool depth = f == TextureFormat::Depth24Stencil8;
        return {bool(flags & (depth ? VK_FORMAT_FEATURE_DEPTH_STENCIL_ATTACHMENT_BIT
                                    : VK_FORMAT_FEATURE_COLOR_ATTACHMENT_BIT)),
                !depth && bool(flags & VK_FORMAT_FEATURE_SAMPLED_IMAGE_BIT) &&
                    bool(flags & VK_FORMAT_FEATURE_SAMPLED_IMAGE_FILTER_LINEAR_BIT),
                !depth && bool(flags & VK_FORMAT_FEATURE_COLOR_ATTACHMENT_BLEND_BIT),
                !depth && bool(flags & VK_FORMAT_FEATURE_TRANSFER_SRC_BIT)};
    }
    const char* ShaderDiagnostic() const override { return genericError.c_str(); }
    bool PrepareShaderPipeline(const ShaderPipeline&) override;
    bool SetShaderPipeline(const ShaderPipeline&) override;
    bool BindShaderResources(const ShaderResourceBindings& values) override {
        genericBound = false;
        if (!Live() || !inFrame || !modelReady || !genericReady)
            return false;
        const auto result = ValidateShaderBindings(genericPipeline, values, [&](Handle h) {
            const auto* r = Find(h);
            const auto* t = passes.Description(h);
            return ShaderTextureInfo{
                r && bool(r->image), r && r->uploaded, r && (t ? passes.CanSample(h) : r->uploaded),
                r && r->format == VK_FORMAT_R8G8B8A8_SRGB, t ? t->format : TextureFormat::Rgba8};
        });
        genericError = result.reason;
        if (!result)
            return false;
        for (const auto& s : values.samplers)
            if (s.sampler.maxAnisotropy > properties.limits.maxSamplerAnisotropy) {
                genericError = "sampler_anisotropy_not_supported";
                return false;
            }
        genericBindings = values;
        genericBound = true;
        return true;
    }
    bool BindGenericDescriptors();
    VkPipelineLayout pipelineLayout = VK_NULL_HANDLE;
    VkPhysicalDeviceProperties properties{};
    VkPhysicalDeviceMemoryProperties memoryProperties{};
    unsigned family = 0, frameIndex = FrameCount - 1, width = 0, height = 0;
    std::uint64_t identity = detail::NextDeviceIdentity(), epoch = 1, issued = 0,
                  transferCompletion = 0;
    bool lost = false, pending = false, inFrame = false, rendering = false, modelReady = false,
         legacyInitialized = false, resetPrepared = false;
    std::atomic<unsigned> validationErrors{0};
    VulkanStats stats;
    std::array<Frame, FrameCount> frames;
    std::vector<Resource> resources;
    std::vector<Resource> retiring;
    // Failed transfer waits do not make queued allocations safe to destroy.
    // Keep them until device teardown, which waits for native queue completion.
    std::vector<Buffer> failedTransfers;
    std::vector<Resource> failedUploads;
    std::vector<unsigned> freeSlots;
    Resource legacyColor, legacyDepth, white;
    RenderPassState passes;
    ModelPipeline pipeline;
    ModelViewport viewport;
    std::array<Handle, 3> textures{};
    Handle shadow;
    std::map<std::string, VkPipeline> pipelines;
    std::map<std::string, VkSampler> samplers;
    VkShaderModule modelVs = VK_NULL_HANDLE, colorVs = VK_NULL_HANDLE, modelPs = VK_NULL_HANDLE;
    VkShaderModule shadowVs = VK_NULL_HANDLE, outlineVs = VK_NULL_HANDLE, shadowPs = VK_NULL_HANDLE;
    VkShaderModule postVs = VK_NULL_HANDLE, postPs = VK_NULL_HANDLE;
    std::string reason;
#ifdef _WIN32
    PFN_vkCreateWin32SurfaceKHR vkCreateWin32SurfaceKHR = nullptr;
#endif
    PFN_vkDestroySurfaceKHR vkDestroySurfaceKHR = nullptr;
    PFN_vkGetPhysicalDeviceSurfaceSupportKHR vkGetPhysicalDeviceSurfaceSupportKHR = nullptr;
    PFN_vkGetPhysicalDeviceSurfaceCapabilitiesKHR vkGetPhysicalDeviceSurfaceCapabilitiesKHR =
        nullptr;
    PFN_vkGetPhysicalDeviceSurfaceFormatsKHR vkGetPhysicalDeviceSurfaceFormatsKHR = nullptr;
    PFN_vkGetPhysicalDeviceSurfacePresentModesKHR vkGetPhysicalDeviceSurfacePresentModesKHR =
        nullptr;
    PFN_vkCreateSwapchainKHR vkCreateSwapchainKHR = nullptr;
    PFN_vkDestroySwapchainKHR vkDestroySwapchainKHR = nullptr;
    PFN_vkGetSwapchainImagesKHR vkGetSwapchainImagesKHR = nullptr;
    PFN_vkAcquireNextImageKHR vkAcquireNextImageKHR = nullptr;
    PFN_vkQueuePresentKHR vkQueuePresentKHR = nullptr;
    VkSurfaceKHR surface = VK_NULL_HANDLE;
    VkSwapchainKHR chain = VK_NULL_HANDLE;
    SwapchainDesc presentation;
    std::vector<Resource> swapImages;
    std::vector<VkSemaphore> readyToPresent;
    unsigned activeImage = 0;
    bool presentationOutput = false, presentable = false, acquiredImage = false, retired = false;
    SurfaceHealth surfaceHealth = SurfaceHealth::Unknown;
    bool swapchainReadback = false;
    bool ValidSurface(const SwapchainDesc&) const;
    RebuildResult RebuildSurface(const SwapchainDesc&);
    void ReleaseSurface();
    SurfaceHealth Health() const;

    bool Check(VkResult result) {
        stats.lastError = result;
        if (result == VK_ERROR_DEVICE_LOST)
            lost = true;
        return result == VK_SUCCESS;
    }
    bool Live() const { return native && !lost && !pending && !retired; }
    Backend BackendType() const override { return Backend::Vulkan; }
    Capabilities Features() const override { return {true, false, true, true}; }
    DeviceStatus Status() const override {
        return {identity, epoch,
                lost     ? DeviceState::Removed
                : Live() ? DeviceState::Ready
                         : DeviceState::ResetPending};
    }
    ResourceLifecycleCapabilities ResourceLifecycleFeatures() const override {
        return {true, true, true, false, false};
    }
    ModelCapabilities ModelFeatures() const override {
        return {true, true, true, true, true, true, true, false, true, true, bool(chain), MaxDraws};
    }
    bool SetFixedPipeline(FixedPipelineState) override {
        genericReady = genericBound = false;
        modelReady = false;
        return false;
    }
    bool Present() override;
    ~VulkanDevice() override {
        if (native) {
            if (gateBlocked) {
                VkSemaphoreSignalInfo signal{VK_STRUCTURE_TYPE_SEMAPHORE_SIGNAL_INFO};
                signal.semaphore = testGate;
                signal.value = gateValue;
                vkSignalSemaphore(native, &signal);
            }
            if (vkDeviceWaitIdle)
                vkDeviceWaitIdle(native);
            ReleaseSurface();
            for (auto& r : resources)
                Release(r);
            for (auto& r : retiring)
                Release(r);
            for (auto& r : failedUploads)
                Release(r);
            for (auto& b : failedTransfers)
                Release(b);
            Release(legacyColor);
            Release(legacyDepth);
            Release(white);
            for (auto& f : frames) {
                if (f.acquired)
                    vkDestroySemaphore(native, f.acquired, nullptr);
                Release(f.uniforms);
                if (f.descriptors)
                    vkDestroyDescriptorPool(native, f.descriptors, nullptr);
                if (f.pool)
                    vkDestroyCommandPool(native, f.pool, nullptr);
            }
            for (auto p : pipelines)
                vkDestroyPipeline(native, p.second, nullptr);
            for (const auto& p : genericPrograms) {
                vkDestroyShaderModule(native, p.second.vertex, nullptr);
                vkDestroyShaderModule(native, p.second.pixel, nullptr);
            }
            for (auto s : samplers)
                vkDestroySampler(native, s.second, nullptr);
            for (auto module :
                 {modelVs, colorVs, modelPs, shadowVs, outlineVs, shadowPs, postVs, postPs})
                if (module)
                    vkDestroyShaderModule(native, module, nullptr);
            if (pipelineLayout)
                vkDestroyPipelineLayout(native, pipelineLayout, nullptr);
            if (descriptorLayout)
                vkDestroyDescriptorSetLayout(native, descriptorLayout, nullptr);
            if (genericPipelineLayout)
                vkDestroyPipelineLayout(native, genericPipelineLayout, nullptr);
            if (genericDescriptorLayout)
                vkDestroyDescriptorSetLayout(native, genericDescriptorLayout, nullptr);
            if (transferPool)
                vkDestroyCommandPool(native, transferPool, nullptr);
            if (timeline)
                vkDestroySemaphore(native, timeline, nullptr);
            if (testGate)
                vkDestroySemaphore(native, testGate, nullptr);
            vkDestroyDevice(native, nullptr);
        }
        if (surface && vkDestroySurfaceKHR)
            vkDestroySurfaceKHR(instance, surface, nullptr);
        if (messenger && vkDestroyDebugUtilsMessengerEXT)
            vkDestroyDebugUtilsMessengerEXT(instance, messenger, nullptr);
        if (instance && vkDestroyInstance)
            vkDestroyInstance(instance, nullptr);
#ifdef _WIN32
        if (library)
            FreeLibrary(static_cast<HMODULE>(library));
#else
        if (library)
            dlclose(library);
#endif
    }
    void Release(Buffer& b) {
        if (b.mapped)
            vkUnmapMemory(native, b.memory);
        if (b.native)
            vkDestroyBuffer(native, b.native, nullptr);
        if (b.memory)
            vkFreeMemory(native, b.memory, nullptr);
        b = {};
    }
    void Release(Resource& r) {
        Release(r.buffer);
        if (r.view)
            vkDestroyImageView(native, r.view, nullptr);
        if (r.image)
            vkDestroyImage(native, r.image, nullptr);
        if (r.memory)
            vkFreeMemory(native, r.memory, nullptr);
        const auto generation = r.generation;
        r = {};
        r.generation = generation;
    }
    unsigned MemoryType(unsigned bits, VkMemoryPropertyFlags flags) const {
        for (unsigned i = 0; i < memoryProperties.memoryTypeCount; ++i)
            if ((bits & (1u << i)) &&
                (memoryProperties.memoryTypes[i].propertyFlags & flags) == flags)
                return i;
        return UINT32_MAX;
    }
    bool AllocateBuffer(VkDeviceSize size, VkBufferUsageFlags usage, Buffer& out) {
        VkBufferCreateInfo b{VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO};
        b.size = size;
        b.usage = usage;
        b.sharingMode = VK_SHARING_MODE_EXCLUSIVE;
        if (!Check(vkCreateBuffer(native, &b, nullptr, &out.native)))
            return false;
        VkMemoryRequirements requirements;
        vkGetBufferMemoryRequirements(native, out.native, &requirements);
        const auto type =
            MemoryType(requirements.memoryTypeBits,
                       VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT);
        if (type == UINT32_MAX) {
            Release(out);
            return false;
        }
        VkMemoryAllocateInfo a{VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO};
        a.allocationSize = requirements.size;
        a.memoryTypeIndex = type;
        if (!Check(vkAllocateMemory(native, &a, nullptr, &out.memory)) ||
            !Check(vkBindBufferMemory(native, out.native, out.memory, 0)) ||
            !Check(vkMapMemory(native, out.memory, 0, VK_WHOLE_SIZE, 0, &out.mapped))) {
            Release(out);
            return false;
        }
        ++stats.bufferAllocations;
        return true;
    }
    bool AllocateImage(unsigned w, unsigned h, VkFormat format, unsigned mips,
                       VkImageUsageFlags usage, Resource& out) {
        VkFormatProperties support;
        vkGetPhysicalDeviceFormatProperties(physical, format, &support);
        const auto required =
            ((usage & VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT) ? VK_FORMAT_FEATURE_COLOR_ATTACHMENT_BIT
                                                           : 0) |
            ((usage & VK_IMAGE_USAGE_DEPTH_STENCIL_ATTACHMENT_BIT)
                 ? VK_FORMAT_FEATURE_DEPTH_STENCIL_ATTACHMENT_BIT
                 : 0) |
            ((usage & VK_IMAGE_USAGE_SAMPLED_BIT)
                 ? VK_FORMAT_FEATURE_SAMPLED_IMAGE_BIT |
                       VK_FORMAT_FEATURE_SAMPLED_IMAGE_FILTER_LINEAR_BIT
                 : 0) |
            ((usage & VK_IMAGE_USAGE_TRANSFER_SRC_BIT) ? VK_FORMAT_FEATURE_TRANSFER_SRC_BIT : 0) |
            ((usage & VK_IMAGE_USAGE_TRANSFER_DST_BIT) ? VK_FORMAT_FEATURE_TRANSFER_DST_BIT : 0);
        if ((support.optimalTilingFeatures & required) != required)
            return false;
        out.format = format;
        out.mips = mips;
        VkImageCreateInfo info{VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO};
        info.imageType = VK_IMAGE_TYPE_2D;
        info.format = format;
        info.extent = {w, h, 1};
        info.mipLevels = mips;
        info.arrayLayers = 1;
        info.samples = VK_SAMPLE_COUNT_1_BIT;
        info.tiling = VK_IMAGE_TILING_OPTIMAL;
        info.usage = usage;
        info.sharingMode = VK_SHARING_MODE_EXCLUSIVE;
        if (!Check(vkCreateImage(native, &info, nullptr, &out.image)))
            return false;
        VkMemoryRequirements requirements;
        vkGetImageMemoryRequirements(native, out.image, &requirements);
        const auto type =
            MemoryType(requirements.memoryTypeBits, VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT);
        if (type == UINT32_MAX) {
            Release(out);
            return false;
        }
        VkMemoryAllocateInfo allocation{VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO};
        allocation.allocationSize = requirements.size;
        allocation.memoryTypeIndex = type;
        if (!Check(vkAllocateMemory(native, &allocation, nullptr, &out.memory)) ||
            !Check(vkBindImageMemory(native, out.image, out.memory, 0))) {
            Release(out);
            return false;
        }
        VkImageViewCreateInfo view{VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO};
        view.image = out.image;
        view.viewType = VK_IMAGE_VIEW_TYPE_2D;
        view.format = format;
        view.subresourceRange = {Aspect(out), 0, mips, 0, 1};
        if (!Check(vkCreateImageView(native, &view, nullptr, &out.view))) {
            Release(out);
            return false;
        }
        return true;
    }
    void Barrier(VkCommandBuffer cmd, Resource& r, VkImageLayout layout,
                 VkPipelineStageFlags2 stage, VkAccessFlags2 access) {
        if (inFrame)
            r.lastUse = issued + 1;
        VkImageMemoryBarrier2 b{VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER_2};
        b.srcStageMask = r.stage;
        b.srcAccessMask = r.access;
        b.dstStageMask = stage;
        b.dstAccessMask = access;
        b.oldLayout = r.layout;
        b.newLayout = layout;
        b.srcQueueFamilyIndex = b.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
        b.image = r.image;
        b.subresourceRange = {Aspect(r), 0, r.mips, 0, 1};
        VkDependencyInfo dependency{VK_STRUCTURE_TYPE_DEPENDENCY_INFO};
        dependency.imageMemoryBarrierCount = 1;
        dependency.pImageMemoryBarriers = &b;
        vkCmdPipelineBarrier2(cmd, &dependency);
        r.layout = layout;
        r.stage = stage;
        r.access = access;
    }
    CompletionToken SignalCompletion() override {
        return Live() && !inFrame && issued ? CompletionToken{identity, issued} : CompletionToken{};
    }
    CompletionState PollCompletion(CompletionToken token) override {
        if (token.device != identity || !token.value || token.value > issued)
            return CompletionState::Invalid;
        if (lost)
            return CompletionState::DeviceLost;
        std::uint64_t completed = 0;
        if (!Check(vkGetSemaphoreCounterValue(native, timeline, &completed)))
            return lost ? CompletionState::DeviceLost : CompletionState::Invalid;
        stats.completed = completed;
        return completed >= token.value ? CompletionState::Complete : CompletionState::Pending;
    }
    bool WaitCompletion(CompletionToken token, std::uint32_t timeout) override {
        if (token.device != identity || !token.value || token.value > issued || lost)
            return false;
        const auto state = PollCompletion(token);
        if (state != CompletionState::Pending)
            return state == CompletionState::Complete && CollectResources();
        VkSemaphoreWaitInfo wait{VK_STRUCTURE_TYPE_SEMAPHORE_WAIT_INFO};
        wait.semaphoreCount = 1;
        wait.pSemaphores = &timeline;
        wait.pValues = &token.value;
        ++stats.timelineWaits;
        return Check(vkWaitSemaphores(native, &wait, std::uint64_t(timeout) * 1000000)) &&
               CollectResources();
    }
    bool CollectResources() {
        std::uint64_t completed = 0;
        if (!lost && !Check(vkGetSemaphoreCounterValue(native, timeline, &completed)))
            return false;
        stats.completed = completed;
        for (auto i = retiring.begin(); i != retiring.end();)
            if (lost || i->lastUse <= completed) {
                Release(*i);
                i = retiring.erase(i);
            } else
                ++i;
        stats.retiringResources = retiring.size();
        return true;
    }
    bool Idle() {
        if (!issued)
            return true;
        if (PollCompletion({identity, issued}) == CompletionState::Pending)
            ++stats.idleWaits;
        return WaitCompletion({identity, issued}, 5000);
    }
    bool Submit(VkCommandBuffer command, VkSemaphore waitBinary = VK_NULL_HANDLE,
                VkSemaphore signalBinary = VK_NULL_HANDLE) {
        if (!Check(vkEndCommandBuffer(command))) {
            pending = true;
            return false;
        }
        const auto next = issued + 1;
        VkCommandBufferSubmitInfo c{VK_STRUCTURE_TYPE_COMMAND_BUFFER_SUBMIT_INFO};
        c.commandBuffer = command;
        VkSemaphoreSubmitInfo signal{VK_STRUCTURE_TYPE_SEMAPHORE_SUBMIT_INFO};
        signal.semaphore = timeline;
        signal.value = next;
        signal.stageMask = VK_PIPELINE_STAGE_2_ALL_COMMANDS_BIT;
        VkSubmitInfo2 submit{VK_STRUCTURE_TYPE_SUBMIT_INFO_2};
        submit.commandBufferInfoCount = 1;
        submit.pCommandBufferInfos = &c;
        submit.signalSemaphoreInfoCount = 1;
        VkSemaphoreSubmitInfo signals[2] = {signal, {VK_STRUCTURE_TYPE_SEMAPHORE_SUBMIT_INFO}};
        signals[1].semaphore = signalBinary;
        signals[1].stageMask = VK_PIPELINE_STAGE_2_ALL_COMMANDS_BIT;
        submit.signalSemaphoreInfoCount = signalBinary ? 2 : 1;
        submit.pSignalSemaphoreInfos = signals;
        VkSemaphoreSubmitInfo wait{VK_STRUCTURE_TYPE_SEMAPHORE_SUBMIT_INFO};
        wait.semaphore = waitBinary;
        wait.stageMask = VK_PIPELINE_STAGE_2_ALL_COMMANDS_BIT;
        if (waitBinary) {
            submit.waitSemaphoreInfoCount = 1;
            submit.pWaitSemaphoreInfos = &wait;
        }
        if (!Check(vkQueueSubmit2(queue, 1, &submit, VK_NULL_HANDLE))) {
            pending = true;
            return false;
        }
        issued = next;
        return true;
    }
    bool BeginTransfer() {
        if (!Live() || inFrame)
            return false;
        if (transferCompletion) {
            if (PollCompletion({identity, transferCompletion}) == CompletionState::Pending)
                ++stats.transferWaits;
            if (!WaitCompletion({identity, transferCompletion}, 5000))
                return false;
        }
        if (!Check(vkResetCommandPool(native, transferPool, 0)))
            return false;
        VkCommandBufferBeginInfo begin{VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO};
        begin.flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT;
        return Check(vkBeginCommandBuffer(transfer, &begin));
    }
    bool FinishTransfer() {
        if (Submit(transfer)) {
            transferCompletion = issued;
            if (PollCompletion({identity, issued}) == CompletionState::Pending)
                ++stats.transferWaits;
            if (WaitCompletion({identity, transferCompletion}, 5000))
                return true;
        }
        pending = true;
        return false;
    }
    Handle Insert(Resource&& r) {
        unsigned slot;
        if (freeSlots.empty()) {
            if (resources.size() >= MaxResources) {
                Release(r);
                return {};
            }
            slot = unsigned(resources.size());
            resources.emplace_back();
        } else {
            slot = freeSlots.back();
            freeSlots.pop_back();
        }
        r.generation = resources[slot].generation;
        r.alive = true;
        resources[slot] = std::move(r);
        return {slot, resources[slot].generation, identity};
    }
    Resource* Find(Handle h) {
        return h.device == identity && h.index < resources.size() && resources[h.index].alive &&
                       resources[h.index].generation == h.generation
                   ? &resources[h.index]
                   : nullptr;
    }
    Handle CreateBuffer(BufferDesc d, const void* initial) override {
        if (!Live() || inFrame || !d.size || d.size > UINT32_MAX ||
            (d.usage != BufferUsage::Vertex && d.usage != BufferUsage::Index &&
             d.usage != BufferUsage::Uniform) ||
            (d.usage == BufferUsage::Index && d.indexFormat != IndexFormat::UInt16 &&
             d.indexFormat != IndexFormat::UInt32))
            return {};
        Resource r;
        r.description = d;
        const auto usage = d.usage == BufferUsage::Vertex  ? VK_BUFFER_USAGE_VERTEX_BUFFER_BIT
                           : d.usage == BufferUsage::Index ? VK_BUFFER_USAGE_INDEX_BUFFER_BIT
                                                           : VK_BUFFER_USAGE_UNIFORM_BUFFER_BIT;
        if (!AllocateBuffer(d.size, usage, r.buffer))
            return {};
        if (initial) {
            std::memcpy(r.buffer.mapped, initial, d.size);
            r.uploaded = true;
        }
        if (d.usage == BufferUsage::Index) {
            r.indices.resize(d.size);
            if (initial)
                std::memcpy(r.indices.data(), initial, d.size);
            r.indices.Seal();
        }
        if (initial) {
            ++stats.resourceUploads;
            stats.bufferUploadBytes += d.size;
        }
        return Insert(std::move(r));
    }
    bool Upload(Handle h, std::size_t offset, const void* data, std::size_t size) override {
        auto* r = Find(h);
        if (!Live() || inFrame || !r || !r->buffer.native || !data || !size ||
            offset > r->description.size || size > r->description.size - offset ||
            (!r->uploaded && (offset || size != r->description.size)))
            return false;
        // Host writes cannot race the GPU. Do not wait for unrelated latest
        // submissions: SceneRenderer supplies completed slots, direct callers
        // must retry a busy resource after its own completion boundary.
        if (r->lastUse && PollCompletion({identity, r->lastUse}) != CompletionState::Complete) {
            ++stats.uploadBusyRejects;
            return false;
        }
        std::memcpy(static_cast<char*>(r->buffer.mapped) + offset, data, size);
        if (!r->indices.empty())
            std::memcpy(r->indices.data() + offset, data, size);
        r->indices.Seal();
        r->uploaded = true;
        ++stats.resourceUploads;
        stats.bufferUploadBytes += size;
        return true;
    }
    bool UploadImage(Resource& r, const ModelTextureData& data) {
        VkDeviceSize total = 0;
        std::vector<VkBufferImageCopy> regions;
        for (unsigned level = 0; level < data.levels.size(); ++level) {
            const auto& image = data.levels[level];
            VkBufferImageCopy copy{};
            copy.bufferOffset = total;
            copy.imageSubresource = {VK_IMAGE_ASPECT_COLOR_BIT, level, 0, 1};
            copy.imageExtent = {image.width, image.height, 1};
            regions.push_back(copy);
            total += image.pixels.size();
        }
        Buffer staging;
        if (!AllocateBuffer(total, VK_BUFFER_USAGE_TRANSFER_SRC_BIT, staging))
            return false;
        for (unsigned i = 0; i < data.levels.size(); ++i)
            std::memcpy(static_cast<char*>(staging.mapped) + regions[i].bufferOffset,
                        data.levels[i].pixels.data(), data.levels[i].pixels.size());
        bool ok = BeginTransfer();
        if (ok) {
            Barrier(transfer, r, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, VK_PIPELINE_STAGE_2_COPY_BIT,
                    VK_ACCESS_2_TRANSFER_WRITE_BIT);
            vkCmdCopyBufferToImage(transfer, staging.native, r.image, r.layout,
                                   unsigned(regions.size()), regions.data());
            Barrier(transfer, r, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL,
                    VK_PIPELINE_STAGE_2_FRAGMENT_SHADER_BIT, VK_ACCESS_2_SHADER_SAMPLED_READ_BIT);
            ok = FinishTransfer();
        }
        if (!ok && pending)
            failedTransfers.push_back(staging);
        else
            Release(staging);
        if (ok) {
            r.uploaded = true;
            ++stats.resourceUploads;
        }
        return ok;
    }
    Handle CreateModelTexture(const ModelTextureData& data) override {
        if (!Live() || inFrame || data.levels.empty() || data.levels.size() > 15 ||
            data.generateMips)
            return {};
        unsigned w = data.levels[0].width, h = data.levels[0].height;
        if (!w || !h || w > 16384 || h > 16384)
            return {};
        for (unsigned i = 0; i < data.levels.size(); ++i) {
            const auto& l = data.levels[i];
            if (l.width != w || l.height != h || l.pixels.size() != std::uint64_t(w) * h * 4 ||
                (w == 1 && h == 1 && i + 1 < data.levels.size()))
                return {};
            w = std::max(1u, w / 2);
            h = std::max(1u, h / 2);
        }
        Resource r;
        r.texture = {data.levels[0].width, data.levels[0].height, TextureFormat::Rgba8};
        if (!AllocateImage(r.texture.width, r.texture.height,
                           data.srgb ? VK_FORMAT_R8G8B8A8_SRGB : VK_FORMAT_R8G8B8A8_UNORM,
                           unsigned(data.levels.size()),
                           VK_IMAGE_USAGE_SAMPLED_BIT | VK_IMAGE_USAGE_TRANSFER_DST_BIT, r))
            return {};
        if (!UploadImage(r, data)) {
            if (pending)
                failedUploads.push_back(std::move(r));
            else
                Release(r);
            return {};
        }
        return Insert(std::move(r));
    }
    Handle CreateTexture(TextureDesc d) override {
        if (!Live() || inFrame || !d.width || !d.height || d.width > 4096 || d.height > 4096 ||
            d.format != TextureFormat::Rgba8 || d.mips != TextureDesc::Mips::SingleLevel)
            return {};
        Resource r;
        r.texture = d;
        if (!AllocateImage(d.width, d.height, VK_FORMAT_R8G8B8A8_UNORM, 1,
                           VK_IMAGE_USAGE_SAMPLED_BIT | VK_IMAGE_USAGE_TRANSFER_DST_BIT, r))
            return {};
        return Insert(std::move(r));
    }
    bool UploadTexture(Handle h, TexturePixels data) override {
        auto* r = Find(h);
        if (!Live() || inFrame || !r || !r->image || passes.Description(h) || !data.data ||
            r->mips != 1 || data.rowPitch < std::size_t(r->texture.width) * 4 ||
            data.rowPitch > SIZE_MAX / r->texture.height ||
            data.bytes <
                data.rowPitch * (r->texture.height - 1) + std::size_t(r->texture.width) * 4)
            return false;
        ModelTextureData image;
        image.levels.push_back(
            {r->texture.width, r->texture.height,
             std::vector<std::uint8_t>(std::size_t(r->texture.width) * r->texture.height * 4)});
        for (unsigned y = 0; y < r->texture.height; ++y)
            std::memcpy(image.levels[0].pixels.data() + std::size_t(y) * r->texture.width * 4,
                        static_cast<const char*>(data.data) + y * data.rowPitch,
                        std::size_t(r->texture.width) * 4);
        return UploadImage(*r, image);
    }
    Handle CreateRenderTexture(const RenderTextureDesc& d) override {
        if (!Live() || inFrame || !QueryShaderTextureSupport(*this, d))
            return {};
        const bool depth = d.format == TextureFormat::Depth24Stencil8;
        VkImageUsageFlags usage = depth ? VK_IMAGE_USAGE_DEPTH_STENCIL_ATTACHMENT_BIT
                                        : VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT;
        if (d.access & unsigned(TextureAccess::Sampled))
            usage |= VK_IMAGE_USAGE_SAMPLED_BIT;
        if (d.access & unsigned(TextureAccess::CopySource))
            usage |= VK_IMAGE_USAGE_TRANSFER_SRC_BIT;
        Resource r;
        r.texture = {d.width, d.height, d.format};
        r.uploaded = true;
        if (!AllocateImage(d.width, d.height, Format(d.format), 1, usage, r))
            return {};
        const auto h = Insert(std::move(r));
        if (!h)
            return {};
        if (!passes.Register(h, d)) {
            Destroy(h);
            return {};
        }
        return h;
    }
    bool Destroy(Handle h) override {
        auto* r = Find(h);
        if (inFrame || !r || (!Live() && !lost) || !CollectResources())
            return false;
        passes.Erase(h);
        const auto generation = r->generation;
        if (!lost && r->lastUse > stats.completed) {
            retiring.push_back(std::move(*r));
            *r = {};
            r->generation = generation;
            stats.retiringResources = retiring.size();
            stats.peakRetiringResources = std::max(stats.peakRetiringResources, retiring.size());
        } else
            Release(*r);
        r->alive = false;
        if (++r->generation)
            freeSlots.push_back(h.index);
        return true;
    }
    bool EnsureModelTarget(const HistoryDesc& d) override {
        if (!Live() || inFrame || !ValidHistoryDesc(d) || d.samples != 1 || !d.color || !d.depth ||
            !d.stencil || d.width > 4096 || d.height > 4096)
            return false;
        presentationOutput = false;
        if (d.width == width && d.height == height && legacyColor.image)
            return true;
        Resource color, depth;
        color.texture = {d.width, d.height, TextureFormat::Rgba8};
        depth.texture = {d.width, d.height, TextureFormat::Depth24Stencil8};
        if (!AllocateImage(d.width, d.height, VK_FORMAT_R8G8B8A8_UNORM, 1,
                           VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT | VK_IMAGE_USAGE_TRANSFER_SRC_BIT |
                               VK_IMAGE_USAGE_TRANSFER_DST_BIT,
                           color))
            return false;
        if (!AllocateImage(d.width, d.height, VK_FORMAT_D24_UNORM_S8_UINT, 1,
                           VK_IMAGE_USAGE_DEPTH_STENCIL_ATTACHMENT_BIT, depth) ||
            !Idle()) {
            Release(color);
            Release(depth);
            return false;
        }
        Release(legacyColor);
        Release(legacyDepth);
        legacyColor = std::move(color);
        legacyDepth = std::move(depth);
        width = d.width;
        height = d.height;
        ++epoch;
        legacyInitialized = false;
        passes.InvalidateContents();
        return true;
    }
    bool EnsurePresentationTarget(const HistoryDesc& d) override {
        if (!chain || retired || d.width != presentation.width || d.height != presentation.height ||
            !EnsureModelTarget(d))
            return false;
        presentationOutput = true;
        return true;
    }
    unsigned RenderWidth() const {
        if (!passes.Active())
            return width;
        const auto& p = passes.Current();
        return passes.Description(PrimaryAttachment(p)->texture)->width;
    }
    unsigned RenderHeight() const {
        if (!passes.Active())
            return height;
        const auto& p = passes.Current();
        return passes.Description(PrimaryAttachment(p)->texture)->height;
    }
    Resource* ActiveColor() {
        return passes.Active() ? (ColorAttachments(passes.Current()).empty()
                                      ? nullptr
                                      : Find(ColorAttachments(passes.Current())[0].texture))
                               : &legacyColor;
    }
    Resource* ActiveDepth() {
        return passes.Active()
                   ? (passes.Current().depthStencil ? Find(passes.Current().depthStencil->texture)
                                                    : nullptr)
                   : &legacyDepth;
    }
    void BeginNative(Resource* color, Resource* depth, const PassAttachment* c,
                     const PassAttachment* d, unsigned w, unsigned h) {
        const auto cmd = frames[frameIndex].commands;
        VkRenderingAttachmentInfo depthInfo{VK_STRUCTURE_TYPE_RENDERING_ATTACHMENT_INFO};
        std::vector<VkRenderingAttachmentInfo> colorInfos;
        const auto load = [](AttachmentLoad l) {
            return l == AttachmentLoad::Clear  ? VK_ATTACHMENT_LOAD_OP_CLEAR
                   : l == AttachmentLoad::Load ? VK_ATTACHMENT_LOAD_OP_LOAD
                                               : VK_ATTACHMENT_LOAD_OP_DONT_CARE;
        };
        const auto store = [](AttachmentStore s) {
            return s == AttachmentStore::Store ? VK_ATTACHMENT_STORE_OP_STORE
                                               : VK_ATTACHMENT_STORE_OP_DONT_CARE;
        };
        const auto addColor = [&](Resource* resource, const PassAttachment& attachment) {
            Barrier(cmd, *resource, VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL,
                    VK_PIPELINE_STAGE_2_COLOR_ATTACHMENT_OUTPUT_BIT,
                    VK_ACCESS_2_COLOR_ATTACHMENT_READ_BIT | VK_ACCESS_2_COLOR_ATTACHMENT_WRITE_BIT);
            VkRenderingAttachmentInfo info{VK_STRUCTURE_TYPE_RENDERING_ATTACHMENT_INFO};
            info.imageView = resource->view;
            info.imageLayout = resource->layout;
            info.loadOp = load(attachment.load);
            info.storeOp = store(attachment.store);
            info.clearValue.color = {{attachment.clear.red, attachment.clear.green,
                                      attachment.clear.blue, attachment.clear.alpha}};
            colorInfos.push_back(info);
        };
        if (passes.Active()) {
            for (const auto& attachment : ColorAttachments(passes.Current()))
                addColor(Find(attachment.texture), attachment);
        } else if (color)
            addColor(color, *c);
        if (depth) {
            Barrier(cmd, *depth, VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL,
                    VK_PIPELINE_STAGE_2_EARLY_FRAGMENT_TESTS_BIT |
                        VK_PIPELINE_STAGE_2_LATE_FRAGMENT_TESTS_BIT,
                    VK_ACCESS_2_DEPTH_STENCIL_ATTACHMENT_READ_BIT |
                        VK_ACCESS_2_DEPTH_STENCIL_ATTACHMENT_WRITE_BIT);
            depthInfo.imageView = depth->view;
            depthInfo.imageLayout = depth->layout;
            depthInfo.loadOp = load(d->load);
            depthInfo.storeOp = store(d->store);
            depthInfo.clearValue.depthStencil = {d->clearDepth, d->clearStencil};
        }
        VkRenderingInfo info{VK_STRUCTURE_TYPE_RENDERING_INFO};
        info.renderArea.extent = {w, h};
        info.layerCount = 1;
        info.colorAttachmentCount = unsigned(colorInfos.size());
        info.pColorAttachments = colorInfos.data();
        info.pDepthAttachment = info.pStencilAttachment = depth ? &depthInfo : nullptr;
        vkCmdBeginRendering(cmd, &info);
        rendering = true;
    }
    bool BeginFrame(ClearColor clear) override {
        if (!Live() || inFrame || presentable || !legacyColor.image || !std::isfinite(clear.red) ||
            !std::isfinite(clear.green) || !std::isfinite(clear.blue) ||
            !std::isfinite(clear.alpha))
            return false;
        if (!CollectResources())
            return false;
        auto& f = frames[(frameIndex + 1) % FrameCount];
        if (f.completion && PollCompletion({identity, f.completion}) == CompletionState::Pending)
            ++stats.frameWaits;
        if (f.completion && !WaitCompletion({identity, f.completion}, 5000))
            return false;
        if (!Check(vkResetCommandPool(native, f.pool, 0)) ||
            !Check(vkResetDescriptorPool(native, f.descriptors, 0)))
            return false;
        VkCommandBufferBeginInfo begin{VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO};
        begin.flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT;
        if (!Check(vkBeginCommandBuffer(f.commands, &begin)))
            return false;
        if (presentationOutput) {
            if (!chain || retired || Health() == SurfaceHealth::Suspended ||
                surfaceHealth == SurfaceHealth::Resettable)
                return false;
            const auto result = vkAcquireNextImageKHR(native, chain, 5000000000ull, f.acquired,
                                                      VK_NULL_HANDLE, &activeImage);
            if (result != VK_SUCCESS && result != VK_SUBOPTIMAL_KHR) {
                Check(result);
                if (result == VK_ERROR_OUT_OF_DATE_KHR)
                    surfaceHealth = SurfaceHealth::Resettable;
                return false;
            }
            acquiredImage = true;
            surfaceHealth =
                result == VK_SUBOPTIMAL_KHR ? SurfaceHealth::Resettable : SurfaceHealth::Ready;
        }
        frameIndex = (frameIndex + 1) % FrameCount;
        f.offset = 0;
        f.draws = 0;
        inFrame = true;
        modelReady = false;
        textures = {};
        shadow = {};
        PassAttachment c, d;
        c.clear = clear;
        BeginNative(&legacyColor, &legacyDepth, &c, &d, width, height);
        vkCmdEndRendering(f.commands);
        rendering = false;
        legacyInitialized = true;
        return true;
    }
    bool BeginRenderPass(const RenderPassDesc& pass) override {
        if (!Live() || !inFrame ||
            ColorAttachments(pass).size() > ShaderFeatures().maxColorAttachments ||
            !passes.Begin(pass))
            return false;
        const auto cmd = frames[frameIndex].commands;
        if (rendering)
            vkCmdEndRendering(cmd);
        rendering = false;
        for (auto h : pass.sampled)
            Barrier(cmd, *Find(h), VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL,
                    VK_PIPELINE_STAGE_2_FRAGMENT_SHADER_BIT, VK_ACCESS_2_SHADER_SAMPLED_READ_BIT);
        BeginNative(ActiveColor(), ActiveDepth(), pass.color ? &*pass.color : nullptr,
                    pass.depthStencil ? &*pass.depthStencil : nullptr, RenderWidth(),
                    RenderHeight());
        modelReady = false;
        textures = {};
        shadow = {};
        return true;
    }
    bool EndRenderPass() override {
        if (!Live() || !inFrame || !passes.Active() || !rendering)
            return false;
        vkCmdEndRendering(frames[frameIndex].commands);
        rendering = false;
        modelReady = false;
        textures = {};
        shadow = {};
        return passes.End();
    }
    bool CopyToFrameOutput(Handle h) override {
        auto* r = Find(h);
        const auto* t = passes.Description(h);
        if (!Live() || !inFrame || rendering || !r || !t || t->format != TextureFormat::Rgba8 ||
            t->width != width || t->height != height || !passes.CanRead(h))
            return false;
        const auto cmd = frames[frameIndex].commands;
        const auto layout = r->layout;
        const auto stage = r->stage;
        const auto access = r->access;
        Barrier(cmd, *r, VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL, VK_PIPELINE_STAGE_2_COPY_BIT,
                VK_ACCESS_2_TRANSFER_READ_BIT);
        Barrier(cmd, legacyColor, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
                VK_PIPELINE_STAGE_2_COPY_BIT, VK_ACCESS_2_TRANSFER_WRITE_BIT);
        VkImageCopy region{};
        region.srcSubresource = region.dstSubresource = {VK_IMAGE_ASPECT_COLOR_BIT, 0, 0, 1};
        region.extent = {width, height, 1};
        vkCmdCopyImage(cmd, r->image, r->layout, legacyColor.image, legacyColor.layout, 1, &region);
        Barrier(cmd, *r, layout, stage, access);
        modelReady = false;
        legacyInitialized = true;
        return true;
    }
    bool EndFrame() override {
        if (!Live() || !inFrame || passes.Active())
            return false;
        if (rendering)
            vkCmdEndRendering(frames[frameIndex].commands);
        rendering = false;
        inFrame = false;
        modelReady = false;
        auto& frame = frames[frameIndex];
        if (presentationOutput) {
            if (!acquiredImage)
                return false;
            auto& target = swapImages[activeImage];
            Barrier(frame.commands, legacyColor, VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL,
                    VK_PIPELINE_STAGE_2_BLIT_BIT, VK_ACCESS_2_TRANSFER_READ_BIT);
            Barrier(frame.commands, target, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
                    VK_PIPELINE_STAGE_2_BLIT_BIT, VK_ACCESS_2_TRANSFER_WRITE_BIT);
            VkImageBlit copy{};
            copy.srcSubresource = copy.dstSubresource = {VK_IMAGE_ASPECT_COLOR_BIT, 0, 0, 1};
            copy.srcOffsets[1] = copy.dstOffsets[1] = {int(width), int(height), 1};
            vkCmdBlitImage(frame.commands, legacyColor.image, legacyColor.layout, target.image,
                           target.layout, 1, &copy, VK_FILTER_NEAREST);
            Barrier(frame.commands, target, VK_IMAGE_LAYOUT_PRESENT_SRC_KHR,
                    VK_PIPELINE_STAGE_2_NONE, VK_ACCESS_2_NONE);
        }
        if (!Submit(frame.commands, presentationOutput ? frame.acquired : VK_NULL_HANDLE,
                    presentationOutput ? readyToPresent[activeImage] : VK_NULL_HANDLE))
            return false;
        presentable = presentationOutput;
        frames[frameIndex].completion = issued;
        ++stats.submissions;
        return true;
    }
    bool PrepareForReset() override {
        if (!Live() || inFrame || !Idle())
            return false;
        pending = true;
        resetPrepared = true;
        ++epoch;
        passes.InvalidateContents();
        legacyInitialized = false;
        return true;
    }
    bool RestoreAfterReset() override {
        if (!native || lost || !pending || !resetPrepared || inFrame)
            return false;
        pending = false;
        resetPrepared = false;
        return true;
    }
    bool Read(Resource& r, std::vector<std::uint8_t>& bytes) {
        if (!BeginTransfer())
            return false;
        Buffer staging;
        if (!AllocateBuffer(VkDeviceSize(r.texture.width) * r.texture.height *
                                TexturePixelBytes(r.texture.format),
                            VK_BUFFER_USAGE_TRANSFER_DST_BIT, staging))
            return false;
        const auto layout = r.layout;
        const auto stage = r.stage;
        const auto access = r.access;
        Barrier(transfer, r, VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL, VK_PIPELINE_STAGE_2_COPY_BIT,
                VK_ACCESS_2_TRANSFER_READ_BIT);
        VkBufferImageCopy region{};
        region.imageSubresource = {VK_IMAGE_ASPECT_COLOR_BIT, 0, 0, 1};
        region.imageExtent = {r.texture.width, r.texture.height, 1};
        vkCmdCopyImageToBuffer(transfer, r.image, r.layout, staging.native, 1, &region);
        VkBufferMemoryBarrier2 b{VK_STRUCTURE_TYPE_BUFFER_MEMORY_BARRIER_2};
        b.srcStageMask = VK_PIPELINE_STAGE_2_COPY_BIT;
        b.srcAccessMask = VK_ACCESS_2_TRANSFER_WRITE_BIT;
        b.dstStageMask = VK_PIPELINE_STAGE_2_HOST_BIT;
        b.dstAccessMask = VK_ACCESS_2_HOST_READ_BIT;
        b.srcQueueFamilyIndex = b.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
        b.buffer = staging.native;
        b.size = VK_WHOLE_SIZE;
        VkDependencyInfo dep{VK_STRUCTURE_TYPE_DEPENDENCY_INFO};
        dep.bufferMemoryBarrierCount = 1;
        dep.pBufferMemoryBarriers = &b;
        vkCmdPipelineBarrier2(transfer, &dep);
        Barrier(transfer, r, layout, stage, access);
        const bool ok = FinishTransfer();
        if (ok) {
            bytes.resize(std::size_t(r.texture.width) * r.texture.height *
                         TexturePixelBytes(r.texture.format));
            std::memcpy(bytes.data(), staging.mapped, bytes.size());
        }
        if (!ok && pending)
            failedTransfers.push_back(staging);
        else
            Release(staging);
        return ok;
    }
    bool ReadRenderTexture(Handle h, AttachmentReadback& out) override {
        auto* r = Find(h);
        const auto* d = passes.Description(h);
        if (!Live() || inFrame || !r || !d || d->format == TextureFormat::Depth24Stencil8 ||
            !passes.CanRead(h))
            return false;
        AttachmentReadback next;
        next.description = *d;
        if (!Read(*r, next.bytes))
            return false;
        out = std::move(next);
        return true;
    }
    bool ReadModelTarget(RgbaImage& out) override {
        if (!Live() || inFrame || !legacyInitialized)
            return false;
        RgbaImage next;
        next.width = width;
        next.height = height;
        if (!Read(legacyColor, next.pixels))
            return false;
        out = std::move(next);
        return true;
    }
    bool BindTexture(std::uint32_t slot, Handle h) override {
        if (genericReady)
            return false;
        auto* r = Find(h);
        if (!Live() || !inFrame || !modelReady || slot >= 3 ||
            pipeline.shader.layout.stages[slot].role == MaterialTextureRole::None || !r ||
            !r->image || !r->uploaded || (passes.Description(h) && !passes.CanSample(h)))
            return false;
        textures[slot] = h;
        return true;
    }
    bool BindShadowTexture(Handle h) override {
        const auto* d = passes.Description(h);
        if (!Live() || !inFrame || !modelReady ||
            pipeline.shadow.mode != BuiltinShadowMode::Material || !Find(h) || !d ||
            d->format != TextureFormat::R32Float || !passes.CanSample(h))
            return false;
        shadow = h;
        return true;
    }
    bool SetModelPipeline(const ModelPipeline&) override;
    bool Draw(NonIndexedDraw) override;
    bool DrawIndexed(IndexedDraw) override;
    bool BindDraw();
    bool MakePipeline(const ModelPipeline&, VkPipeline&, const ShaderProgram* = nullptr,
                      const GenericProgram* = nullptr);
    VkSampler Sampler(const SamplerDesc&);
    bool Initialize(unsigned, unsigned, VulkanDriver, VulkanOptions,
                    const SwapchainDesc* = nullptr);
};

bool VulkanDevice::ValidSurface(const SwapchainDesc& d) const {
#ifdef _WIN32
    return ValidSwapchainDesc(d) && d.window.system == WindowSystem::Win32 &&
           IsWindow(reinterpret_cast<HWND>(d.window.handle)) && d.windowed && d.width <= 4096 &&
           d.height <= 4096 && d.color == SurfaceColor::Rgba8 &&
           d.depth == SurfaceDepth::Depth24Stencil8 && d.samples == 1 && !d.quality &&
           d.swap == SurfaceSwap::Discard &&
           (!presentation.window.handle || d.window.handle == presentation.window.handle);
#else
    return false;
#endif
}
SurfaceHealth VulkanDevice::Health() const {
    if (lost || retired)
        return SurfaceHealth::Removed;
#ifdef _WIN32
    if (!IsWindow(reinterpret_cast<HWND>(presentation.window.handle)))
        return SurfaceHealth::Removed;
    if (IsIconic(reinterpret_cast<HWND>(presentation.window.handle)))
        return SurfaceHealth::Suspended;
#endif
    return surfaceHealth;
}
void VulkanDevice::ReleaseSurface() {
    for (auto semaphore : readyToPresent)
        if (semaphore)
            vkDestroySemaphore(native, semaphore, nullptr);
    readyToPresent.clear();
    swapImages.clear();
    if (chain)
        vkDestroySwapchainKHR(native, chain, nullptr);
    chain = VK_NULL_HANDLE;
    presentable = acquiredImage = false;
}
RebuildResult VulkanDevice::RebuildSurface(const SwapchainDesc& d) {
    if (!ValidSurface(d))
        return RebuildResult::Unsupported;
    if (lost || retired)
        return RebuildResult::ReplacementRequired;
    if (!Live() || inFrame || presentable || acquiredImage)
        return RebuildResult::Busy;
    VkSurfaceCapabilitiesKHR caps{};
    if (!Check(vkGetPhysicalDeviceSurfaceCapabilitiesKHR(physical, surface, &caps)))
        return RebuildResult::Failed;
    if (!(caps.supportedUsageFlags & VK_IMAGE_USAGE_TRANSFER_DST_BIT))
        return RebuildResult::Unsupported;
    if (!caps.currentExtent.width || !caps.currentExtent.height)
        return RebuildResult::RetryLater;
    if (caps.currentExtent.width != UINT32_MAX &&
        (caps.currentExtent.width != d.width || caps.currentExtent.height != d.height))
        return RebuildResult::RetryLater;
    if (d.width < caps.minImageExtent.width || d.width > caps.maxImageExtent.width ||
        d.height < caps.minImageExtent.height || d.height > caps.maxImageExtent.height)
        return RebuildResult::Unsupported;
    unsigned count = 0;
    if (!Check(vkGetPhysicalDeviceSurfaceFormatsKHR(physical, surface, &count, nullptr)))
        return RebuildResult::Failed;
    std::vector<VkSurfaceFormatKHR> formats(count);
    if (!Check(vkGetPhysicalDeviceSurfaceFormatsKHR(physical, surface, &count, formats.data())))
        return RebuildResult::Failed;
    VkSurfaceFormatKHR selected{};
    selected.format = VK_FORMAT_UNDEFINED;
    for (auto desired : {VK_FORMAT_R8G8B8A8_UNORM, VK_FORMAT_B8G8R8A8_UNORM})
        for (auto f : formats)
            if (f.format == desired && f.colorSpace == VK_COLOR_SPACE_SRGB_NONLINEAR_KHR &&
                selected.format == VK_FORMAT_UNDEFINED)
                selected = f;
    if (selected.format == VK_FORMAT_UNDEFINED)
        return RebuildResult::Unsupported;
    VkFormatProperties sourceSupport{}, destSupport{};
    vkGetPhysicalDeviceFormatProperties(physical, VK_FORMAT_R8G8B8A8_UNORM, &sourceSupport);
    vkGetPhysicalDeviceFormatProperties(physical, selected.format, &destSupport);
    if (!(sourceSupport.optimalTilingFeatures & VK_FORMAT_FEATURE_BLIT_SRC_BIT) ||
        !(destSupport.optimalTilingFeatures & VK_FORMAT_FEATURE_BLIT_DST_BIT))
        return RebuildResult::Unsupported;
    count = 0;
    if (!Check(vkGetPhysicalDeviceSurfacePresentModesKHR(physical, surface, &count, nullptr)))
        return RebuildResult::Failed;
    std::vector<VkPresentModeKHR> modes(count);
    if (!Check(vkGetPhysicalDeviceSurfacePresentModesKHR(physical, surface, &count, modes.data())))
        return RebuildResult::Failed;
    const auto mode =
        d.present == PresentMode::VSync ? VK_PRESENT_MODE_FIFO_KHR : VK_PRESENT_MODE_IMMEDIATE_KHR;
    if (std::find(modes.begin(), modes.end(), mode) == modes.end())
        return RebuildResult::Unsupported;
    if (!Check(vkDeviceWaitIdle(native)))
        return lost ? RebuildResult::ReplacementRequired : RebuildResult::Failed;
    HistoryDesc output;
    output.width = d.width;
    output.height = d.height;
    if (!EnsureModelTarget(output))
        return RebuildResult::Failed;
    VkSwapchainCreateInfoKHR create{VK_STRUCTURE_TYPE_SWAPCHAIN_CREATE_INFO_KHR};
    create.surface = surface;
    create.minImageCount = caps.minImageCount + 1;
    if (caps.maxImageCount)
        create.minImageCount = std::min(create.minImageCount, caps.maxImageCount);
    create.imageFormat = selected.format;
    create.imageColorSpace = selected.colorSpace;
    create.imageExtent = {d.width, d.height};
    create.imageArrayLayers = 1;
    create.imageUsage = VK_IMAGE_USAGE_TRANSFER_DST_BIT;
    create.imageSharingMode = VK_SHARING_MODE_EXCLUSIVE;
    const bool canRead = (caps.supportedUsageFlags & VK_IMAGE_USAGE_TRANSFER_SRC_BIT) != 0;
    if (canRead)
        create.imageUsage |= VK_IMAGE_USAGE_TRANSFER_SRC_BIT;
    create.preTransform = caps.currentTransform;
    create.compositeAlpha = VK_COMPOSITE_ALPHA_OPAQUE_BIT_KHR;
    if (!(caps.supportedCompositeAlpha & create.compositeAlpha)) {
        const auto flags = unsigned(caps.supportedCompositeAlpha);
        create.compositeAlpha = static_cast<VkCompositeAlphaFlagBitsKHR>(flags & (~flags + 1));
    }
    create.presentMode = mode;
    create.clipped = VK_TRUE;
    create.oldSwapchain = chain;
    VkSwapchainKHR next = VK_NULL_HANDLE;
    if (!Check(vkCreateSwapchainKHR(native, &create, nullptr, &next)))
        return RebuildResult::Failed;
    // Once creation succeeds, oldSwapchain is retired even if later setup fails.
    ReleaseSurface();
    chain = next;
    surfaceHealth = SurfaceHealth::Resettable;
    ++epoch;
    passes.InvalidateContents();
    legacyInitialized = false;
    count = 0;
    if (!Check(vkGetSwapchainImagesKHR(native, chain, &count, nullptr)))
        return RebuildResult::Failed;
    std::vector<VkImage> images(count);
    if (!Check(vkGetSwapchainImagesKHR(native, chain, &count, images.data())))
        return RebuildResult::Failed;
    swapImages.resize(count);
    readyToPresent.resize(count, VK_NULL_HANDLE);
    VkSemaphoreCreateInfo semaphore{VK_STRUCTURE_TYPE_SEMAPHORE_CREATE_INFO};
    for (unsigned i = 0; i < count; ++i) {
        swapImages[i].image = images[i];
        swapImages[i].format = selected.format;
        swapImages[i].texture = {d.width, d.height, TextureFormat::Rgba8};
        if (!Check(vkCreateSemaphore(native, &semaphore, nullptr, &readyToPresent[i])))
            return RebuildResult::Failed;
    }
    presentation = d;
    presentationOutput = true;
    surfaceHealth = SurfaceHealth::Ready;
    swapchainReadback = canRead;
    return RebuildResult::Success;
}
bool VulkanDevice::Present() {
    if (!Live() || inFrame || !chain || !presentable || !acquiredImage ||
        Health() == SurfaceHealth::Suspended)
        return false;
    VkPresentInfoKHR info{VK_STRUCTURE_TYPE_PRESENT_INFO_KHR};
    info.swapchainCount = 1;
    info.pSwapchains = &chain;
    info.pImageIndices = &activeImage;
    info.waitSemaphoreCount = 1;
    info.pWaitSemaphores = &readyToPresent[activeImage];
    const auto result = vkQueuePresentKHR(queue, &info);
    presentable = acquiredImage = false;
    if (result == VK_ERROR_OUT_OF_DATE_KHR || result == VK_SUBOPTIMAL_KHR)
        surfaceHealth = SurfaceHealth::Resettable;
    else if (result == VK_SUCCESS) {
        if (surfaceHealth != SurfaceHealth::Resettable)
            surfaceHealth = SurfaceHealth::Ready;
    } else {
        Check(result);
        surfaceHealth = lost ? SurfaceHealth::Removed : SurfaceHealth::Unknown;
    }
    return result == VK_SUCCESS || result == VK_SUBOPTIMAL_KHR;
}
class VulkanSwapchain final : public Swapchain {
    VulkanDevice& device;

public:
    explicit VulkanSwapchain(VulkanDevice& native) : device(native) {}
    ~VulkanSwapchain() override { Retire(); }
    SwapchainDesc Description() const override { return device.presentation; }
    SwapchainSupport Query(const SwapchainDesc& d) const override {
        SwapchainSupport s;
        if (device.retired || device.lost || !device.ValidSurface(d))
            return s;
        VkSurfaceCapabilitiesKHR caps{};
        if (device.vkGetPhysicalDeviceSurfaceCapabilitiesKHR(device.physical, device.surface,
                                                             &caps) != VK_SUCCESS ||
            !(caps.supportedUsageFlags & VK_IMAGE_USAGE_TRANSFER_DST_BIT) ||
            d.width < caps.minImageExtent.width || d.width > caps.maxImageExtent.width ||
            d.height < caps.minImageExtent.height || d.height > caps.maxImageExtent.height)
            return s;
        unsigned count = 0;
        if (device.vkGetPhysicalDeviceSurfacePresentModesKHR(device.physical, device.surface,
                                                             &count, nullptr) != VK_SUCCESS)
            return s;
        std::vector<VkPresentModeKHR> modes(count);
        if (device.vkGetPhysicalDeviceSurfacePresentModesKHR(device.physical, device.surface,
                                                             &count, modes.data()) != VK_SUCCESS)
            return s;
        const auto mode = d.present == PresentMode::VSync ? VK_PRESENT_MODE_FIFO_KHR
                                                          : VK_PRESENT_MODE_IMMEDIATE_KHR;
        if (std::find(modes.begin(), modes.end(), mode) == modes.end())
            return s;
        s.supported = s.resize = s.reset = s.controllerOwnsReset = true;
        s.qualityLevels = 1;
        return s;
    }
    SurfaceHealth Health() const override { return device.Health(); }
    std::int64_t LastNativeError() const override { return device.stats.lastError; }
    RebuildResult Rebuild(const SwapchainDesc& d, RebuildReason reason) override {
        return reason == RebuildReason::DeviceReplacement ? RebuildResult::ReplacementRequired
                                                          : device.RebuildSurface(d);
    }
    bool Retire() override {
        if (device.retired)
            return true;
        if (device.inFrame)
            return false;
        if (!device.lost && !device.Check(device.vkDeviceWaitIdle(device.native)))
            return false;
        device.ReleaseSurface();
        device.retired = device.pending = true;
        ++device.epoch;
        device.passes.InvalidateContents();
        return true;
    }
};

VKAPI_ATTR VkBool32 VKAPI_CALL Validation(VkDebugUtilsMessageSeverityFlagBitsEXT severity,
                                          VkDebugUtilsMessageTypeFlagsEXT,
                                          const VkDebugUtilsMessengerCallbackDataEXT* data,
                                          void* context) {
    if (severity & VK_DEBUG_UTILS_MESSAGE_SEVERITY_ERROR_BIT_EXT) {
        ++static_cast<VulkanDevice*>(context)->validationErrors;
        std::fprintf(stderr, "Vulkan validation: %s\n", data->pMessage);
    }
    return VK_FALSE;
}
bool VulkanDevice::Initialize(unsigned w, unsigned h, VulkanDriver driver, VulkanOptions options,
                              const SwapchainDesc* window) {
    reason = "invalid_options";
    if (!w || !h || w > 4096 || h > 4096 || (window && !ValidSurface(*window)) ||
        (driver != VulkanDriver::Hardware && driver != VulkanDriver::Software))
        return false;
    if (options.model.compatibility != ModelCompatibility::Default ||
        options.model.diagnosticAlpha12 || options.model.diagnosticTextureFma ||
        options.model.diagnosticSpecularFma) {
        reason = "model_policy_not_supported";
        return false;
    }
    reason = "vulkan_loader_unavailable";
#ifdef _WIN32
    library = LoadLibraryW(L"vulkan-1.dll");
    if (library)
        vkGetInstanceProcAddr = reinterpret_cast<PFN_vkGetInstanceProcAddr>(
            GetProcAddress(static_cast<HMODULE>(library), "vkGetInstanceProcAddr"));
#else
    library = dlopen("libvulkan.so.1", RTLD_NOW | RTLD_LOCAL);
    if (library)
        vkGetInstanceProcAddr =
            reinterpret_cast<PFN_vkGetInstanceProcAddr>(dlsym(library, "vkGetInstanceProcAddr"));
#endif
    if (!vkGetInstanceProcAddr)
        return false;
    vkCreateInstance =
        reinterpret_cast<PFN_vkCreateInstance>(vkGetInstanceProcAddr(nullptr, "vkCreateInstance"));
    vkEnumerateInstanceLayerProperties = reinterpret_cast<PFN_vkEnumerateInstanceLayerProperties>(
        vkGetInstanceProcAddr(nullptr, "vkEnumerateInstanceLayerProperties"));
    vkEnumerateInstanceExtensionProperties =
        reinterpret_cast<PFN_vkEnumerateInstanceExtensionProperties>(
            vkGetInstanceProcAddr(nullptr, "vkEnumerateInstanceExtensionProperties"));
    if (!vkCreateInstance || !vkEnumerateInstanceLayerProperties ||
        !vkEnumerateInstanceExtensionProperties)
        return false;
    const char* layer = "VK_LAYER_KHRONOS_validation";
    const char* extensions[] = {VK_EXT_DEBUG_UTILS_EXTENSION_NAME,
                                VK_EXT_VALIDATION_FEATURES_EXTENSION_NAME};
    if (options.validation) {
        reason = "validation_layer_unavailable";
        unsigned count = 0;
        if (!Check(vkEnumerateInstanceLayerProperties(&count, nullptr)))
            return false;
        std::vector<VkLayerProperties> layers(count);
        if (!Check(vkEnumerateInstanceLayerProperties(&count, layers.data())) ||
            std::none_of(layers.begin(), layers.end(),
                         [&](const auto& p) { return std::strcmp(p.layerName, layer) == 0; }))
            return false;
        std::vector<VkExtensionProperties> supported;
        for (const char* provider : {static_cast<const char*>(nullptr), layer}) {
            count = 0;
            if (!Check(vkEnumerateInstanceExtensionProperties(provider, &count, nullptr)))
                return false;
            std::vector<VkExtensionProperties> part(count);
            if (!Check(vkEnumerateInstanceExtensionProperties(provider, &count, part.data())))
                return false;
            supported.insert(supported.end(), part.begin(), part.end());
        }
        reason = "validation_extensions_unavailable";
        for (const auto* name : extensions)
            if (std::none_of(supported.begin(), supported.end(), [&](const auto& p) {
                    return std::strcmp(p.extensionName, name) == 0;
                }))
                return false;
    }
    VkDebugUtilsMessengerCreateInfoEXT debug{
        VK_STRUCTURE_TYPE_DEBUG_UTILS_MESSENGER_CREATE_INFO_EXT};
    debug.messageSeverity = VK_DEBUG_UTILS_MESSAGE_SEVERITY_ERROR_BIT_EXT;
    debug.messageType = VK_DEBUG_UTILS_MESSAGE_TYPE_GENERAL_BIT_EXT |
                        VK_DEBUG_UTILS_MESSAGE_TYPE_VALIDATION_BIT_EXT |
                        VK_DEBUG_UTILS_MESSAGE_TYPE_PERFORMANCE_BIT_EXT;
    debug.pfnUserCallback = Validation;
    debug.pUserData = this;
    const VkValidationFeatureEnableEXT sync =
        VK_VALIDATION_FEATURE_ENABLE_SYNCHRONIZATION_VALIDATION_EXT;
    VkValidationFeaturesEXT validation{VK_STRUCTURE_TYPE_VALIDATION_FEATURES_EXT};
    validation.enabledValidationFeatureCount = 1;
    validation.pEnabledValidationFeatures = &sync;
    validation.pNext = &debug;
    VkApplicationInfo app{VK_STRUCTURE_TYPE_APPLICATION_INFO};
    app.pApplicationName = "MikuDanceStudio offscreen RHI";
    app.apiVersion = VK_API_VERSION_1_3;
    VkInstanceCreateInfo create{VK_STRUCTURE_TYPE_INSTANCE_CREATE_INFO};
    create.pApplicationInfo = &app;
    std::vector<const char*> enabledExtensions;
#ifdef _WIN32
    if (window) {
        enabledExtensions.push_back(VK_KHR_SURFACE_EXTENSION_NAME);
        enabledExtensions.push_back(VK_KHR_WIN32_SURFACE_EXTENSION_NAME);
    }
#else
    if (window) {
        reason = "window_system_not_supported";
        return false;
    }
#endif
    if (options.validation) {
        create.enabledLayerCount = 1;
        create.ppEnabledLayerNames = &layer;
        enabledExtensions.insert(enabledExtensions.end(), std::begin(extensions),
                                 std::end(extensions));
        create.pNext = &validation;
    }
    create.enabledExtensionCount = unsigned(enabledExtensions.size());
    create.ppEnabledExtensionNames = enabledExtensions.data();
    reason = "instance_creation_failed";
    if (!Check(vkCreateInstance(&create, nullptr, &instance)))
        return false;
#define LOAD_INSTANCE(name)                                                                        \
    vk##name = reinterpret_cast<PFN_vk##name>(vkGetInstanceProcAddr(instance, "vk" #name));        \
    if (!vk##name) {                                                                               \
        reason = "instance_entrypoint_unavailable";                                                \
        return false;                                                                              \
    }
    INSTANCE_FUNCTIONS(LOAD_INSTANCE)
    if (window) {
        LOAD_INSTANCE(DestroySurfaceKHR);
        LOAD_INSTANCE(GetPhysicalDeviceSurfaceSupportKHR);
        LOAD_INSTANCE(GetPhysicalDeviceSurfaceCapabilitiesKHR);
        LOAD_INSTANCE(GetPhysicalDeviceSurfaceFormatsKHR);
        LOAD_INSTANCE(GetPhysicalDeviceSurfacePresentModesKHR);
#ifdef _WIN32
        LOAD_INSTANCE(CreateWin32SurfaceKHR);
        VkWin32SurfaceCreateInfoKHR si{VK_STRUCTURE_TYPE_WIN32_SURFACE_CREATE_INFO_KHR};
        si.hwnd = reinterpret_cast<HWND>(window->window.handle);
        si.hinstance = reinterpret_cast<HINSTANCE>(GetWindowLongPtrW(si.hwnd, GWLP_HINSTANCE));
        if (!Check(vkCreateWin32SurfaceKHR(instance, &si, nullptr, &surface))) {
            reason = "surface_creation_failed";
            return false;
        }
#endif
    }
#undef LOAD_INSTANCE
    if (options.validation) {
        vkCreateDebugUtilsMessengerEXT = reinterpret_cast<PFN_vkCreateDebugUtilsMessengerEXT>(
            vkGetInstanceProcAddr(instance, "vkCreateDebugUtilsMessengerEXT"));
        vkDestroyDebugUtilsMessengerEXT = reinterpret_cast<PFN_vkDestroyDebugUtilsMessengerEXT>(
            vkGetInstanceProcAddr(instance, "vkDestroyDebugUtilsMessengerEXT"));
        if (!vkCreateDebugUtilsMessengerEXT || !vkDestroyDebugUtilsMessengerEXT ||
            !Check(vkCreateDebugUtilsMessengerEXT(instance, &debug, nullptr, &messenger)))
            return false;
    }
    unsigned count = 0;
    reason = "physical_device_unavailable";
    if (!Check(vkEnumeratePhysicalDevices(instance, &count, nullptr)) || !count)
        return false;
    std::vector<VkPhysicalDevice> candidates(count);
    if (!Check(vkEnumeratePhysicalDevices(instance, &count, candidates.data())))
        return false;
    unsigned best = 0;
    for (auto candidate : candidates) {
        VkPhysicalDeviceProperties p;
        vkGetPhysicalDeviceProperties(candidate, &p);
        const bool cpu = p.deviceType == VK_PHYSICAL_DEVICE_TYPE_CPU;
        if (cpu != (driver == VulkanDriver::Software) || p.apiVersion < VK_API_VERSION_1_3)
            continue;
        VkPhysicalDeviceVulkan13Features f13{VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_VULKAN_1_3_FEATURES};
        VkPhysicalDeviceVulkan12Features f12{VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_VULKAN_1_2_FEATURES};
        f12.pNext = &f13;
        VkPhysicalDeviceFeatures2 f{VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_FEATURES_2};
        f.pNext = &f12;
        vkGetPhysicalDeviceFeatures2(candidate, &f);
        VkPhysicalDeviceFloatControlsProperties floats{
            VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_FLOAT_CONTROLS_PROPERTIES};
        VkPhysicalDeviceProperties2 p2{VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_PROPERTIES_2};
        p2.pNext = &floats;
        vkGetPhysicalDeviceProperties2(candidate, &p2);
        if (!f13.dynamicRendering || !f13.synchronization2 || !f13.shaderDemoteToHelperInvocation ||
            !floats.shaderSignedZeroInfNanPreserveFloat32 || !f12.timelineSemaphore ||
            !f.features.samplerAnisotropy)
            continue;
        unsigned families = 0;
        vkGetPhysicalDeviceQueueFamilyProperties(candidate, &families, nullptr);
        std::vector<VkQueueFamilyProperties> queues(families);
        vkGetPhysicalDeviceQueueFamilyProperties(candidate, &families, queues.data());
        for (unsigned i = 0; i < families; ++i)
            if (queues[i].queueCount && (queues[i].queueFlags & VK_QUEUE_GRAPHICS_BIT)) {
                if (surface) {
                    VkBool32 supported = VK_FALSE;
                    if (!Check(vkGetPhysicalDeviceSurfaceSupportKHR(candidate, i, surface,
                                                                    &supported)) ||
                        !supported)
                        continue;
                }
                unsigned score = p.deviceType == VK_PHYSICAL_DEVICE_TYPE_DISCRETE_GPU     ? 3
                                 : p.deviceType == VK_PHYSICAL_DEVICE_TYPE_INTEGRATED_GPU ? 2
                                                                                          : 1;
                if (score > best) {
                    best = score;
                    physical = candidate;
                    properties = p;
                    family = i;
                }
                break;
            }
    }
    reason = driver == VulkanDriver::Software ? "software_device_unavailable"
                                              : "vulkan_1_3_features_unavailable";
    if (!physical)
        return false;
    vkGetPhysicalDeviceMemoryProperties(physical, &memoryProperties);
    float priority = 1;
    VkDeviceQueueCreateInfo q{VK_STRUCTURE_TYPE_DEVICE_QUEUE_CREATE_INFO};
    q.queueFamilyIndex = family;
    q.queueCount = 1;
    q.pQueuePriorities = &priority;
    VkPhysicalDeviceVulkan13Features f13{VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_VULKAN_1_3_FEATURES};
    f13.dynamicRendering = f13.synchronization2 = f13.shaderDemoteToHelperInvocation = VK_TRUE;
    VkPhysicalDeviceVulkan12Features f12{VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_VULKAN_1_2_FEATURES};
    f12.timelineSemaphore = VK_TRUE;
    f12.pNext = &f13;
    VkPhysicalDeviceFeatures features{};
    features.samplerAnisotropy = VK_TRUE;
    VkDeviceCreateInfo device{VK_STRUCTURE_TYPE_DEVICE_CREATE_INFO};
    device.pNext = &f12;
    device.queueCreateInfoCount = 1;
    device.pQueueCreateInfos = &q;
    device.pEnabledFeatures = &features;
    const char* swapExtension = VK_KHR_SWAPCHAIN_EXTENSION_NAME;
    if (window) {
        device.enabledExtensionCount = 1;
        device.ppEnabledExtensionNames = &swapExtension;
    }
    reason = "device_creation_failed";
    if (!Check(vkCreateDevice(physical, &device, nullptr, &native)))
        return false;
#define LOAD_DEVICE(name)                                                                          \
    vk##name = reinterpret_cast<PFN_vk##name>(vkGetDeviceProcAddr(native, "vk" #name));            \
    if (!vk##name) {                                                                               \
        reason = "device_entrypoint_unavailable";                                                  \
        return false;                                                                              \
    }
    DEVICE_FUNCTIONS(LOAD_DEVICE)
    if (window) {
        LOAD_DEVICE(CreateSwapchainKHR);
        LOAD_DEVICE(DestroySwapchainKHR);
        LOAD_DEVICE(GetSwapchainImagesKHR);
        LOAD_DEVICE(AcquireNextImageKHR);
        LOAD_DEVICE(QueuePresentKHR);
    }
#undef LOAD_DEVICE
    vkGetDeviceQueue(native, family, 0, &queue);
    reason = "device_resources_failed";
    VkSemaphoreTypeCreateInfo type{VK_STRUCTURE_TYPE_SEMAPHORE_TYPE_CREATE_INFO};
    type.semaphoreType = VK_SEMAPHORE_TYPE_TIMELINE;
    VkSemaphoreCreateInfo sem{VK_STRUCTURE_TYPE_SEMAPHORE_CREATE_INFO};
    sem.pNext = &type;
    if (!Check(vkCreateSemaphore(native, &sem, nullptr, &timeline)))
        return false;
    VkDescriptorSetLayoutBinding bindings[] = {
        {0, VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER, 1, VK_SHADER_STAGE_VERTEX_BIT, nullptr},
        {1, VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER, 1, VK_SHADER_STAGE_FRAGMENT_BIT, nullptr},
        {2, VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER, 1, VK_SHADER_STAGE_VERTEX_BIT, nullptr},
        {3, VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER, 1, VK_SHADER_STAGE_FRAGMENT_BIT, nullptr},
        {4, VK_DESCRIPTOR_TYPE_SAMPLED_IMAGE, 3, VK_SHADER_STAGE_FRAGMENT_BIT, nullptr},
        {7, VK_DESCRIPTOR_TYPE_SAMPLED_IMAGE, 1, VK_SHADER_STAGE_FRAGMENT_BIT, nullptr},
        {8, VK_DESCRIPTOR_TYPE_SAMPLER, 3, VK_SHADER_STAGE_FRAGMENT_BIT, nullptr},
        {11, VK_DESCRIPTOR_TYPE_SAMPLER, 1, VK_SHADER_STAGE_FRAGMENT_BIT, nullptr}};
    VkDescriptorSetLayoutCreateInfo dl{VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO};
    dl.bindingCount = unsigned(std::size(bindings));
    dl.pBindings = bindings;
    if (!Check(vkCreateDescriptorSetLayout(native, &dl, nullptr, &descriptorLayout)))
        return false;
    VkPipelineLayoutCreateInfo pl{VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO};
    pl.setLayoutCount = 1;
    pl.pSetLayouts = &descriptorLayout;
    if (!Check(vkCreatePipelineLayout(native, &pl, nullptr, &pipelineLayout)))
        return false;
    auto commands = [&](VkCommandPool& pool, VkCommandBuffer& cmd) {
        VkCommandPoolCreateInfo ci{VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO};
        ci.queueFamilyIndex = family;
        ci.flags = VK_COMMAND_POOL_CREATE_TRANSIENT_BIT;
        if (!Check(vkCreateCommandPool(native, &ci, nullptr, &pool)))
            return false;
        VkCommandBufferAllocateInfo a{VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO};
        a.commandPool = pool;
        a.level = VK_COMMAND_BUFFER_LEVEL_PRIMARY;
        a.commandBufferCount = 1;
        return Check(vkAllocateCommandBuffers(native, &a, &cmd));
    };
    if (!commands(transferPool, transfer))
        return false;
    for (auto& frame : frames) {
        if (!commands(frame.pool, frame.commands) ||
            !AllocateBuffer(UniformBytes, VK_BUFFER_USAGE_UNIFORM_BUFFER_BIT, frame.uniforms))
            return false;
        if (window) {
            VkSemaphoreCreateInfo binary{VK_STRUCTURE_TYPE_SEMAPHORE_CREATE_INFO};
            if (!Check(vkCreateSemaphore(native, &binary, nullptr, &frame.acquired)))
                return false;
        }
        VkDescriptorPoolSize sizes[] = {{VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER, 4 * MaxDraws},
                                        {VK_DESCRIPTOR_TYPE_SAMPLED_IMAGE, GraphicsTextureSlots * MaxDraws},
                                        {VK_DESCRIPTOR_TYPE_SAMPLER, GraphicsTextureSlots * MaxDraws}};
        VkDescriptorPoolCreateInfo p{VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO};
        p.maxSets = MaxDraws;
        p.poolSizeCount = 3;
        p.pPoolSizes = sizes;
        if (!Check(vkCreateDescriptorPool(native, &p, nullptr, &frame.descriptors)))
            return false;
    }
    auto module = [&](const auto& words, VkShaderModule& out) {
        VkShaderModuleCreateInfo m{VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO};
        m.codeSize = sizeof(words);
        m.pCode = words;
        return Check(vkCreateShaderModule(native, &m, nullptr, &out));
    };
    if (!module(spirv::model_vertex, modelVs) || !module(spirv::color_vertex, colorVs) ||
        !module(spirv::model_pixel, modelPs) || !module(spirv::shadow_vertex, shadowVs) ||
        !module(spirv::outline_vertex, outlineVs) || !module(spirv::shadow_pixel, shadowPs) ||
        !module(spirv::post_vertex, postVs) || !module(spirv::post_pixel, postPs))
        return false;
    white.texture = {1, 1, TextureFormat::Rgba8};
    if (!AllocateImage(1, 1, VK_FORMAT_R8G8B8A8_UNORM, 1,
                       VK_IMAGE_USAGE_SAMPLED_BIT | VK_IMAGE_USAGE_TRANSFER_DST_BIT, white) ||
        !UploadImage(white, ModelTextureData{{RgbaImage{1, 1, {255, 255, 255, 255}}}}))
        return false;
    HistoryDesc target;
    target.width = w;
    target.height = h;
    if (!EnsureModelTarget(target))
        return false;
    if (window && RebuildSurface(*window) != RebuildResult::Success) {
        reason = "swapchain_creation_failed";
        return false;
    }
    reason.clear();
    return true;
}

bool VulkanDevice::MakePipeline(const ModelPipeline& p, VkPipeline& out,
                                const ShaderProgram* generic, const GenericProgram* binary) {
    auto* color = ActiveColor();
    auto* depth = ActiveDepth();
    std::vector<VkFormat> colorFormats;
    if (passes.Active()) {
        for (auto f : passes.ColorFormats())
            colorFormats.push_back(Format(f));
    } else if (color)
        colorFormats.push_back(color->format);
    const auto cf = color ? color->format : VK_FORMAT_UNDEFINED,
               df = depth ? depth->format : VK_FORMAT_UNDEFINED;
    std::ostringstream key;
    for (auto value : {unsigned(cf),
                       unsigned(df),
                       unsigned(p.vertexLayout),
                       unsigned(p.topology),
                       unsigned(p.shadow.mode),
                       unsigned(p.postProcess.mode),
                       unsigned(p.raster.cull),
                       unsigned(p.raster.depthTest),
                       unsigned(p.raster.depthWrite),
                       unsigned(p.raster.depthCompare),
                       unsigned(p.raster.alphaBlend),
                       unsigned(p.raster.sourceBlend),
                       unsigned(p.raster.destinationBlend),
                       unsigned(p.separateAlpha),
                       unsigned(p.sourceAlpha),
                       unsigned(p.destinationAlpha),
                       unsigned(p.colorOperation),
                       unsigned(p.alphaOperation),
                       unsigned(p.stencil),
                       unsigned(p.stencilReadMask),
                       unsigned(p.stencilWriteMask),
                       unsigned(p.stencilCompare),
                       unsigned(p.stencilFail),
                       unsigned(p.stencilDepthFail),
                       unsigned(p.stencilPass),
                       unsigned(p.colorWriteMask)})
        key << value << ',';
    for (auto format : colorFormats)
        key << ":rt" << unsigned(format);
    if (generic)
        key.write(generic->key.data(), generic->key.size());
    auto found = pipelines.find(key.str());
    if (found != pipelines.end()) {
        out = found->second;
        return true;
    }
    const bool builtin = p.shadow.mode != BuiltinShadowMode::None,
               outline = p.shadow.mode == BuiltinShadowMode::Outline;
    VkPipelineShaderStageCreateInfo stages[2]{};
    stages[0].sType = stages[1].sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
    stages[0].stage = VK_SHADER_STAGE_VERTEX_BIT;
    stages[1].stage = VK_SHADER_STAGE_FRAGMENT_BIT;
    stages[0].module = builtin
                           ? (outline ? outlineVs : shadowVs)
                           : (p.vertexLayout == VertexLayout::PositionColor ? colorVs : modelVs);
    stages[0].pName = builtin ? (outline ? "shadow_outline_vs" : "shadow_vs") : "main";
    stages[1].module = builtin ? shadowPs : modelPs;
    stages[1].pName = builtin ? "shadow_ps" : "main";
    if (p.postProcess.mode != PostProcessMode::None) {
        stages[0].module = postVs;
        stages[0].pName = "post_vs";
        stages[1].module = postPs;
        stages[1].pName = "post_ps";
    }
    if (generic) {
        stages[0].module = binary->vertex;
        stages[0].pName = generic->description.vertex.entry.c_str();
        stages[1].module = binary->pixel;
        stages[1].pName = generic->description.pixel.entry.c_str();
    }
    VkVertexInputBindingDescription binding{0, ModelVertexStride(p.vertexLayout),
                                            VK_VERTEX_INPUT_RATE_VERTEX};
    VkVertexInputAttributeDescription attrs[] = {{0, 0, VK_FORMAT_R32G32B32_SFLOAT, 0},
                                                 {1, 0,
                                                  p.vertexLayout == VertexLayout::PositionColor
                                                      ? VK_FORMAT_R8G8B8A8_UNORM
                                                      : VK_FORMAT_R32G32B32_SFLOAT,
                                                  12},
                                                 {2, 0, VK_FORMAT_R32G32_SFLOAT, 24}};
    VkPipelineVertexInputStateCreateInfo vi{
        VK_STRUCTURE_TYPE_PIPELINE_VERTEX_INPUT_STATE_CREATE_INFO};
    vi.vertexBindingDescriptionCount = 1;
    vi.pVertexBindingDescriptions = &binding;
    vi.vertexAttributeDescriptionCount = p.vertexLayout == VertexLayout::PositionColor ? 2 : 3;
    vi.pVertexAttributeDescriptions = attrs;
    if (p.postProcess.mode != PostProcessMode::None) {
        attrs[1] = {1, 0, VK_FORMAT_R32G32_SFLOAT, 24};
        vi.vertexAttributeDescriptionCount = 2;
    }
    std::vector<VkVertexInputAttributeDescription> genericInputs;
    if (generic) {
        for (const auto& i : generic->description.inputs) {
            const auto format =
                i.format == ShaderVertexFormat::Float2   ? VK_FORMAT_R32G32_SFLOAT
                : i.format == ShaderVertexFormat::Float3 ? VK_FORMAT_R32G32B32_SFLOAT
                : i.format == ShaderVertexFormat::Float4 ? VK_FORMAT_R32G32B32A32_SFLOAT
                                                         : VK_FORMAT_R8G8B8A8_UNORM;
            genericInputs.push_back({i.location, 0, format, i.offset});
        }
        vi.vertexAttributeDescriptionCount = unsigned(genericInputs.size());
        vi.pVertexAttributeDescriptions = genericInputs.data();
    }
    VkPipelineInputAssemblyStateCreateInfo ia{
        VK_STRUCTURE_TYPE_PIPELINE_INPUT_ASSEMBLY_STATE_CREATE_INFO};
    ia.topology = p.topology == PrimitiveTopology::LineList ? VK_PRIMITIVE_TOPOLOGY_LINE_LIST
                                                            : VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST;
    VkPipelineViewportStateCreateInfo vp{VK_STRUCTURE_TYPE_PIPELINE_VIEWPORT_STATE_CREATE_INFO};
    vp.viewportCount = vp.scissorCount = 1;
    VkPipelineRasterizationStateCreateInfo rs{
        VK_STRUCTURE_TYPE_PIPELINE_RASTERIZATION_STATE_CREATE_INFO};
    rs.polygonMode = VK_POLYGON_MODE_FILL;
    rs.lineWidth = 1;
    rs.frontFace = VK_FRONT_FACE_CLOCKWISE;
    rs.cullMode = p.raster.cull == BatchCull::None               ? VK_CULL_MODE_NONE
                  : p.raster.cull == BatchCull::CounterClockwise ? VK_CULL_MODE_BACK_BIT
                                                                 : VK_CULL_MODE_FRONT_BIT;
    VkPipelineMultisampleStateCreateInfo ms{
        VK_STRUCTURE_TYPE_PIPELINE_MULTISAMPLE_STATE_CREATE_INFO};
    ms.rasterizationSamples = VK_SAMPLE_COUNT_1_BIT;
    VkPipelineDepthStencilStateCreateInfo ds{
        VK_STRUCTURE_TYPE_PIPELINE_DEPTH_STENCIL_STATE_CREATE_INFO};
    ds.depthTestEnable = p.raster.depthTest;
    ds.depthWriteEnable = p.raster.depthWrite;
    ds.depthCompareOp = static_cast<VkCompareOp>(p.raster.depthCompare);
    ds.stencilTestEnable = p.stencil;
    ds.front = {static_cast<VkStencilOp>(unsigned(p.stencilFail) - 1),
                static_cast<VkStencilOp>(unsigned(p.stencilPass) - 1),
                static_cast<VkStencilOp>(unsigned(p.stencilDepthFail) - 1),
                static_cast<VkCompareOp>(p.stencilCompare),
                p.stencilReadMask,
                p.stencilWriteMask,
                0};
    ds.back = ds.front;
    VkPipelineColorBlendAttachmentState ba{};
    ba.blendEnable = p.raster.alphaBlend;
    ba.srcColorBlendFactor = Blend(p.raster.sourceBlend);
    ba.dstColorBlendFactor = Blend(p.raster.destinationBlend);
    ba.srcAlphaBlendFactor = Blend(p.separateAlpha ? p.sourceAlpha : p.raster.sourceBlend);
    ba.dstAlphaBlendFactor =
        Blend(p.separateAlpha ? p.destinationAlpha : p.raster.destinationBlend);
    ba.colorBlendOp = static_cast<VkBlendOp>(unsigned(p.colorOperation) - 1);
    ba.alphaBlendOp =
        static_cast<VkBlendOp>(unsigned(p.separateAlpha ? p.alphaOperation : p.colorOperation) - 1);
    ba.colorWriteMask = p.colorWriteMask;
    VkPipelineColorBlendStateCreateInfo bs{
        VK_STRUCTURE_TYPE_PIPELINE_COLOR_BLEND_STATE_CREATE_INFO};
    std::vector<VkPipelineColorBlendAttachmentState> blendAttachments(colorFormats.size(), ba);
    bs.attachmentCount = unsigned(colorFormats.size());
    bs.pAttachments = blendAttachments.data();
    VkDynamicState dynamic[] = {VK_DYNAMIC_STATE_VIEWPORT, VK_DYNAMIC_STATE_SCISSOR,
                                VK_DYNAMIC_STATE_STENCIL_REFERENCE};
    VkPipelineDynamicStateCreateInfo dyn{VK_STRUCTURE_TYPE_PIPELINE_DYNAMIC_STATE_CREATE_INFO};
    dyn.dynamicStateCount = 3;
    dyn.pDynamicStates = dynamic;
    VkPipelineRenderingCreateInfo renderingInfo{VK_STRUCTURE_TYPE_PIPELINE_RENDERING_CREATE_INFO};
    renderingInfo.colorAttachmentCount = unsigned(colorFormats.size());
    renderingInfo.pColorAttachmentFormats = colorFormats.data();
    renderingInfo.depthAttachmentFormat = renderingInfo.stencilAttachmentFormat = df;
    VkGraphicsPipelineCreateInfo info{VK_STRUCTURE_TYPE_GRAPHICS_PIPELINE_CREATE_INFO};
    info.pNext = &renderingInfo;
    info.stageCount = 2;
    info.pStages = stages;
    info.pVertexInputState = &vi;
    info.pInputAssemblyState = &ia;
    info.pViewportState = &vp;
    info.pRasterizationState = &rs;
    info.pMultisampleState = &ms;
    info.pDepthStencilState = &ds;
    info.pColorBlendState = &bs;
    info.pDynamicState = &dyn;
    info.layout = generic ? genericPipelineLayout : pipelineLayout;
    if (!Check(vkCreateGraphicsPipelines(native, VK_NULL_HANDLE, 1, &info, nullptr, &out)))
        return false;
    pipelines.emplace(key.str(), out);
    ++stats.pipelineCreations;
    return true;
}
bool VulkanDevice::PrepareShaderPipeline(const ShaderPipeline& p) {
    const auto result = ValidateShaderTargetSupport(*this, p);
    genericError = result.reason;
    if (!result || !Live())
        return false;
    if (!genericDescriptorLayout) {
        VkDescriptorSetLayoutBinding bindings[4 + 2 * GraphicsTextureSlots]{};
        for (unsigned i = 0; i < 4 + 2 * GraphicsTextureSlots; ++i) {
            bindings[i].binding = i;
            bindings[i].descriptorCount = 1;
            bindings[i].descriptorType = i < 4   ? VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER
                                         : i < 4 + GraphicsTextureSlots ? VK_DESCRIPTOR_TYPE_SAMPLED_IMAGE
                                                 : VK_DESCRIPTOR_TYPE_SAMPLER;
            bindings[i].stageFlags = i < 4
                                         ? VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT
                                         : VK_SHADER_STAGE_FRAGMENT_BIT;
        }
        VkDescriptorSetLayoutCreateInfo info{VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO};
        info.bindingCount = 4 + 2 * GraphicsTextureSlots;
        info.pBindings = bindings;
        if (!Check(vkCreateDescriptorSetLayout(native, &info, nullptr, &genericDescriptorLayout)))
            return false;
    }
    if (!genericPipelineLayout) {
        VkPipelineLayoutCreateInfo info{VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO};
        info.setLayoutCount = 1;
        info.pSetLayouts = &genericDescriptorLayout;
        if (!Check(vkCreatePipelineLayout(native, &info, nullptr, &genericPipelineLayout)))
            return false;
    }
    auto compiled = genericPrograms.find(p.program->key);
    if (compiled == genericPrograms.end()) {
        GenericProgram program;
        SnapshotBytes vs, ps;
        const auto& desc = p.program->description;
        std::map<unsigned, std::vector<unsigned>> vertexOutputs, pixelInputs;
        if (!detail::CompileSpirv(desc.vertex, vs, genericError) ||
            !detail::CompileSpirv(desc.pixel, ps, genericError) ||
            !detail::ReflectSpirv(desc, desc.vertex, vs, genericError, &vertexOutputs) ||
            !detail::ReflectSpirv(desc, desc.pixel, ps, genericError, &pixelInputs))
            return false;
        for (const auto& input : pixelInputs) {
            const auto found = vertexOutputs.find(input.first);
            if (found == vertexOutputs.end() || found->second != input.second) {
                genericError = "shader_stage_interface_mismatch";
                return false;
            }
        }
        const auto module = [&](const SnapshotBytes& bytes, VkShaderModule& output) {
            std::vector<std::uint32_t> words(bytes.size() / 4);
            std::memcpy(words.data(), bytes.data(), bytes.size());
            VkShaderModuleCreateInfo info{VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO};
            info.codeSize = bytes.size();
            info.pCode = words.data();
            return Check(vkCreateShaderModule(native, &info, nullptr, &output));
        };
        if (!module(vs, program.vertex) || !module(ps, program.pixel)) {
            if (program.vertex)
                vkDestroyShaderModule(native, program.vertex, nullptr);
            if (program.pixel)
                vkDestroyShaderModule(native, program.pixel, nullptr);
            return false;
        }
        compiled = genericPrograms.emplace(p.program->key, program).first;
    }
    genericError.clear();
    return true;
}
bool VulkanDevice::SetShaderPipeline(const ShaderPipeline& p) {
    genericReady = genericBound = modelReady = false;
    const auto result = ValidateShaderTargetSupport(*this, p);
    genericError = result.reason;
    const auto view = p.viewport.value_or(ModelViewport{0, 0, RenderWidth(), RenderHeight(), 0, 1});
    if (!result || !Live() || !inFrame ||
        !ValidModelViewport(view, RenderWidth(), RenderHeight()) ||
        ((p.raster.depthTest || p.raster.depthWrite) && !ActiveDepth()))
        return false;
    const auto layout = ValidateShaderAttachments(
        p,
        passes.Active() ? passes.ColorFormats() : std::vector<TextureFormat>{TextureFormat::Rgba8});
    if (!layout) {
        genericError = layout.reason;
        return false;
    }
    if (!PrepareShaderPipeline(p))
        return false;
    auto compiled = genericPrograms.find(p.program->key);
    VkPipeline selected;
    const auto adapter = ShaderRasterAdapter(p);
    if (!MakePipeline(adapter, selected, p.program.get(), &compiled->second))
        return false;
    if (!rendering) {
        PassAttachment c, d;
        c.load = d.load = AttachmentLoad::Load;
        BeginNative(&legacyColor, &legacyDepth, &c, &d, width, height);
    }
    const auto cmd = frames[frameIndex].commands;
    vkCmdBindPipeline(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, selected);
    const VkViewport nativeView{float(view.x),     float(view.y + view.height),
                                float(view.width), -float(view.height),
                                view.minDepth,     view.maxDepth};
    const VkRect2D scissor{{0, 0}, {RenderWidth(), RenderHeight()}};
    vkCmdSetViewport(cmd, 0, 1, &nativeView);
    vkCmdSetScissor(cmd, 0, 1, &scissor);
    vkCmdSetStencilReference(cmd, VK_STENCIL_FACE_FRONT_AND_BACK, 0);
    pipeline = adapter;
    viewport = view;
    genericPipeline = p;
    genericReady = modelReady = true;
    genericError.clear();
    return true;
}
bool VulkanDevice::BindGenericDescriptors() {
    if (!genericBound || !BindShaderResources(genericBindings))
        return false;
    auto& f = frames[frameIndex];
    if (f.draws >= MaxDraws)
        return false;
    const auto align =
        (std::max)(VkDeviceSize(1), properties.limits.minUniformBufferOffsetAlignment);
    VkDescriptorBufferInfo buffers[4]{};
    VkDescriptorImageInfo images[GraphicsTextureSlots]{}, sampling[GraphicsTextureSlots]{};
    const auto upload = [&](VkDescriptorBufferInfo& info, const void* data, std::size_t bytes) {
        const auto offset = (f.offset + align - 1) / align * align;
        if (offset + bytes > UniformBytes)
            return false;
        std::memcpy(static_cast<char*>(f.uniforms.mapped) + offset, data, bytes);
        info = {f.uniforms.native, offset, bytes};
        f.offset = offset + bytes;
        return true;
    };
    const std::array<float, 4> zero{};
    if (!upload(buffers[0], zero.data(), sizeof(zero)))
        return false;
    buffers[1] = buffers[2] = buffers[3] = buffers[0];
    for (const auto& c : genericBindings.constants)
        if (!upload(buffers[c.slot], c.bytes.data(), c.bytes.size()))
            return false;
    for (unsigned i = 0; i < GraphicsTextureSlots; ++i) {
        Resource* r = &white;
        for (const auto& t : genericBindings.textures)
            if (t.slot == i)
                r = Find(t.texture);
        if (!r || r->layout != VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL)
            return false;
        r->lastUse = issued + 1;
        images[i].imageView = r->view;
        images[i].imageLayout = r->layout;
        SamplerDesc a;
        a.min = a.mag = a.mip = SamplerFilter::Point;
        a.u = a.v = SamplerAddress::Clamp;
        for (const auto& s : genericBindings.samplers)
            if (s.slot == i)
                a = s.sampler;
        sampling[i].sampler = Sampler(a);
        if (!sampling[i].sampler)
            return false;
    }
    VkDescriptorSetAllocateInfo alloc{VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO};
    alloc.descriptorPool = f.descriptors;
    alloc.descriptorSetCount = 1;
    alloc.pSetLayouts = &genericDescriptorLayout;
    VkDescriptorSet set;
    if (!Check(vkAllocateDescriptorSets(native, &alloc, &set)))
        return false;
    VkWriteDescriptorSet writes[4 + 2 * GraphicsTextureSlots]{};
    for (unsigned i = 0; i < 4 + 2 * GraphicsTextureSlots; ++i) {
        auto& w = writes[i];
        w.sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
        w.dstSet = set;
        w.dstBinding = i;
        w.descriptorCount = 1;
        w.descriptorType = i < 4   ? VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER
                           : i < 4 + GraphicsTextureSlots ? VK_DESCRIPTOR_TYPE_SAMPLED_IMAGE
                                   : VK_DESCRIPTOR_TYPE_SAMPLER;
        if (i < 4)
            w.pBufferInfo = &buffers[i];
        else
            w.pImageInfo = i < 4 + GraphicsTextureSlots ? &images[i - 4] : &sampling[i - 4 - GraphicsTextureSlots];
    }
    vkUpdateDescriptorSets(native, 4 + 2 * GraphicsTextureSlots, writes, 0, nullptr);
    vkCmdBindDescriptorSets(f.commands, VK_PIPELINE_BIND_POINT_GRAPHICS, genericPipelineLayout, 0,
                            1, &set, 0, nullptr);
    ++f.draws;
    return true;
}
bool VulkanDevice::SetModelPipeline(const ModelPipeline& p) {
    genericReady = genericBound = false;
    modelReady = false;
    textures = {};
    shadow = {};
    const auto view = p.viewport.value_or(ModelViewport{0, 0, RenderWidth(), RenderHeight(), 0, 1});
    const auto& s = p.shader;
    std::array<float, 4> evaluated;
    std::array<std::array<float, 4>, 3> samples{};
    if (!Live() || !inFrame || !ValidPostProcess(p) || !ValidBuiltinShadowState(p.shadow) ||
        (p.shadow.mode != BuiltinShadowMode::None &&
         (!passes.Active() || p.vertexLayout != BuiltinShadowVertexLayout(p.shadow.mode) ||
          p.topology != PrimitiveTopology::TriangleList)) ||
        !ValidModelViewport(view, RenderWidth(), RenderHeight()) ||
        !ModelVertexStride(p.vertexLayout) || !ModelPrimitiveArity(p.topology) ||
        !ValidateMaterialShaderInputs(s) ||
        !EvaluateMaterialTextureCascade(s.layout, {1, 1, 1, 1}, samples, evaluated) ||
        unsigned(p.raster.depthCompare) > 7 || unsigned(p.raster.cull) > 2 ||
        unsigned(p.raster.sourceBlend) > 10 || unsigned(p.raster.destinationBlend) > 10 ||
        unsigned(p.sourceAlpha) > 10 || unsigned(p.destinationAlpha) > 10 ||
        unsigned(p.colorOperation) < 1 || unsigned(p.colorOperation) > 5 ||
        unsigned(p.alphaOperation) < 1 || unsigned(p.alphaOperation) > 5 || p.colorWriteMask > 15 ||
        unsigned(p.stencilCompare) > 7 || unsigned(p.stencilFail) < 1 ||
        unsigned(p.stencilFail) > 8 || unsigned(p.stencilDepthFail) < 1 ||
        unsigned(p.stencilDepthFail) > 8 || unsigned(p.stencilPass) < 1 ||
        unsigned(p.stencilPass) > 8 || ((p.raster.depthTest || p.stencil) && !ActiveDepth()))
        return false;
    if (p.shadow.mode == BuiltinShadowMode::Depth &&
        (!ActiveColor() || ActiveColor()->format != VK_FORMAT_R32_SFLOAT))
        return false;
    const auto alphaFactor = [](BlendFactor b) {
        return b != BlendFactor::SourceColor && b != BlendFactor::InvSourceColor &&
               b != BlendFactor::DestinationColor && b != BlendFactor::InvDestinationColor &&
               b != BlendFactor::SourceAlphaSaturate;
    };
    if (p.raster.alphaBlend &&
        (!alphaFactor(p.separateAlpha ? p.sourceAlpha : p.raster.sourceBlend) ||
         !alphaFactor(p.separateAlpha ? p.destinationAlpha : p.raster.destinationBlend)))
        return false;
    for (unsigned i = 0; i < 3; ++i) {
        const auto& t = s.layout.stages[i];
        const auto& a = s.samplers[i];
        if (t.role != MaterialTextureRole::None &&
            (t.coordinates == MaterialCoordinates::MeshUv1 ||
             (p.vertexLayout == VertexLayout::PositionColor &&
              t.coordinates != MaterialCoordinates::MeshUv0) ||
             a.maxAnisotropy > properties.limits.maxSamplerAnisotropy ||
             (a.mip == SamplerFilter::Anisotropic && a.min != SamplerFilter::Anisotropic &&
              a.mag != SamplerFilter::Anisotropic)))
            return false;
    }
    VkPipeline selected;
    if (!MakePipeline(p, selected))
        return false;
    if (!rendering) {
        PassAttachment c, d;
        c.load = d.load = AttachmentLoad::Load;
        BeginNative(&legacyColor, &legacyDepth, &c, &d, width, height);
    }
    const auto cmd = frames[frameIndex].commands;
    vkCmdBindPipeline(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, selected);
    const float offset = p.shadow.mode == BuiltinShadowMode::None ? 0 : .5f;
    const VkViewport v{float(view.x) + offset, float(view.y + view.height) + offset,
                       float(view.width),      -float(view.height),
                       view.minDepth,          view.maxDepth};
    const VkRect2D scissor{{0, 0}, {RenderWidth(), RenderHeight()}};
    vkCmdSetViewport(cmd, 0, 1, &v);
    vkCmdSetScissor(cmd, 0, 1, &scissor);
    vkCmdSetStencilReference(cmd, VK_STENCIL_FACE_FRONT_AND_BACK, p.stencilReference);
    pipeline = p;
    viewport = view;
    modelReady = true;
    return true;
}
VkSampler VulkanDevice::Sampler(const SamplerDesc& d) {
    std::ostringstream key;
    for (auto v : {unsigned(d.min), unsigned(d.mag), unsigned(d.mip), unsigned(d.u), unsigned(d.v),
                   d.maxAnisotropy})
        key << v << ',';
    auto found = samplers.find(key.str());
    if (found != samplers.end())
        return found->second;
    VkSamplerCreateInfo s{VK_STRUCTURE_TYPE_SAMPLER_CREATE_INFO};
    s.magFilter = d.mag == SamplerFilter::Point ? VK_FILTER_NEAREST : VK_FILTER_LINEAR;
    s.minFilter = d.min == SamplerFilter::Point ? VK_FILTER_NEAREST : VK_FILTER_LINEAR;
    s.mipmapMode = d.mip == SamplerFilter::Point ? VK_SAMPLER_MIPMAP_MODE_NEAREST
                                                 : VK_SAMPLER_MIPMAP_MODE_LINEAR;
    s.addressModeU = d.u == SamplerAddress::Wrap ? VK_SAMPLER_ADDRESS_MODE_REPEAT
                                                 : VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
    s.addressModeV = d.v == SamplerAddress::Wrap ? VK_SAMPLER_ADDRESS_MODE_REPEAT
                                                 : VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
    s.addressModeW = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
    s.anisotropyEnable = d.min == SamplerFilter::Anisotropic || d.mag == SamplerFilter::Anisotropic;
    s.maxAnisotropy = float(d.maxAnisotropy);
    s.maxLod = VK_LOD_CLAMP_NONE;
    VkSampler out;
    if (!Check(vkCreateSampler(native, &s, nullptr, &out)))
        return VK_NULL_HANDLE;
    samplers.emplace(key.str(), out);
    return out;
}
bool VulkanDevice::BindDraw() {
    if (genericReady)
        return BindGenericDescriptors();
    auto& f = frames[frameIndex];
    if (f.draws >= MaxDraws)
        return false;
    VkDescriptorImageInfo images[4]{}, samplerInfo[4]{};
    for (unsigned i = 0; i < 4; ++i) {
        const bool active =
            i == 3 ? pipeline.shadow.mode == BuiltinShadowMode::Material
                   : pipeline.shader.layout.stages[i].role != MaterialTextureRole::None;
        Resource* r = &white;
        if (active) {
            const auto h = i == 3 ? shadow : textures[i];
            r = Find(h);
            if (!r || !r->image || !r->uploaded ||
                r->layout != VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL ||
                (passes.Description(h) && !passes.CanSample(h)) ||
                (i < 3 && (r->format == VK_FORMAT_R8G8B8A8_SRGB) != pipeline.shader.srgbSample[i]))
                return false;
        }
        images[i].imageView = r->view;
        r->lastUse = issued + 1;
        images[i].imageLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
        SamplerDesc a;
        a.min = a.mag = SamplerFilter::Linear;
        a.mip = SamplerFilter::Point;
        a.u = a.v = SamplerAddress::Clamp;
        if (i < 3 && active)
            a = pipeline.shader.samplers[i];
        samplerInfo[i].sampler = Sampler(a);
        if (!samplerInfo[i].sampler)
            return false;
    }
    detail::ModelConstants constants;
    if (!detail::BuildModelConstants(pipeline, viewport, false, constants))
        return false;
    const auto shadowConstants = detail::BuildBuiltinShadowConstants(pipeline, viewport);
    VkDescriptorBufferInfo buffers[4]{};
    const auto align = std::max<VkDeviceSize>(1, properties.limits.minUniformBufferOffsetAlignment);
    auto uniform = [&](unsigned slot, const auto& data) {
        const auto offset = (f.offset + align - 1) / align * align;
        if (offset + sizeof(data) > UniformBytes)
            return false;
        std::memcpy(static_cast<char*>(f.uniforms.mapped) + offset, &data, sizeof(data));
        buffers[slot] = {f.uniforms.native, offset, sizeof(data)};
        f.offset = offset + sizeof(data);
        return true;
    };
    if (!uniform(0, constants.vertex) || !uniform(1, constants.pixel) ||
        !uniform(2, shadowConstants))
        return false;
    buffers[3] = buffers[2];
    if (pipeline.postProcess.mode != PostProcessMode::None &&
        !uniform(3, detail::BuildPostProcessConstants(pipeline)))
        return false;
    VkDescriptorSetAllocateInfo alloc{VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO};
    alloc.descriptorPool = f.descriptors;
    alloc.descriptorSetCount = 1;
    alloc.pSetLayouts = &descriptorLayout;
    VkDescriptorSet set;
    if (!Check(vkAllocateDescriptorSets(native, &alloc, &set)))
        return false;
    VkWriteDescriptorSet writes[8]{};
    for (unsigned i = 0; i < 8; ++i) {
        writes[i].sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
        writes[i].dstSet = set;
        writes[i].descriptorCount = 1;
    }
    for (unsigned i = 0; i < 4; ++i) {
        writes[i].dstBinding = i;
        writes[i].descriptorType = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER;
        writes[i].pBufferInfo = &buffers[i];
    }
    writes[4].dstBinding = 4;
    writes[4].descriptorCount = 3;
    writes[4].descriptorType = VK_DESCRIPTOR_TYPE_SAMPLED_IMAGE;
    writes[4].pImageInfo = images;
    writes[5].dstBinding = 7;
    writes[5].descriptorType = VK_DESCRIPTOR_TYPE_SAMPLED_IMAGE;
    writes[5].pImageInfo = images + 3;
    writes[6].dstBinding = 8;
    writes[6].descriptorCount = 3;
    writes[6].descriptorType = VK_DESCRIPTOR_TYPE_SAMPLER;
    writes[6].pImageInfo = samplerInfo;
    writes[7].dstBinding = 11;
    writes[7].descriptorType = VK_DESCRIPTOR_TYPE_SAMPLER;
    writes[7].pImageInfo = samplerInfo + 3;
    vkUpdateDescriptorSets(native, 8, writes, 0, nullptr);
    vkCmdBindDescriptorSets(f.commands, VK_PIPELINE_BIND_POINT_GRAPHICS, pipelineLayout, 0, 1, &set,
                            0, nullptr);
    ++f.draws;
    return true;
}
bool VulkanDevice::Draw(NonIndexedDraw d) {
    auto* v = Find(d.vertices);
    const auto stride = ModelVertexStride(pipeline.vertexLayout),
               arity = ModelPrimitiveArity(pipeline.topology);
    if (!Live() || !inFrame || !rendering || !modelReady ||
        !IsCurrentSubmission(Status(), d.submission) || !v || !v->buffer.native || !v->uploaded ||
        v->description.usage != BufferUsage::Vertex ||
        (d.layout && *d.layout != pipeline.vertexLayout) || !stride || !arity || !d.vertexCount ||
        d.vertexCount % arity || d.firstVertex > v->description.size / stride ||
        d.vertexCount > v->description.size / stride - d.firstVertex)
        return false;
    if (!BindDraw())
        return false;
    v->lastUse = issued + 1;
    VkDeviceSize offset = 0;
    const auto cmd = frames[frameIndex].commands;
    vkCmdBindVertexBuffers(cmd, 0, 1, &v->buffer.native, &offset);
    vkCmdDraw(cmd, d.vertexCount, 1, d.firstVertex, 0);
    return true;
}
bool VulkanDevice::DrawIndexed(IndexedDraw d) {
    auto* v = Find(d.vertices);
    auto* i = Find(d.indices);
    const auto stride = ModelVertexStride(d.layout), arity = ModelPrimitiveArity(pipeline.topology);
    if (!Live() || !inFrame || !rendering || !modelReady ||
        !IsCurrentSubmission(Status(), d.submission) || !v || !i || !v->buffer.native ||
        !i->buffer.native || !v->uploaded || !i->uploaded ||
        v->description.usage != BufferUsage::Vertex || i->description.usage != BufferUsage::Index ||
        d.layout != pipeline.vertexLayout || !stride || !arity || !d.indexCount ||
        d.indexCount % arity || !d.vertexCount || d.vertexCount > v->description.size / stride)
        return false;
    const auto step = i->description.indexFormat == IndexFormat::UInt16 ? 2u : 4u;
    if (d.firstIndex > i->indices.size() / step ||
        d.indexCount > i->indices.size() / step - d.firstIndex)
        return false;
    if (!i->indices.IndicesFit(d.firstIndex, d.indexCount, step, d.vertexCount))
        return false;
    if (!BindDraw())
        return false;
    v->lastUse = i->lastUse = issued + 1;
    VkDeviceSize offset = 0;
    const auto cmd = frames[frameIndex].commands;
    vkCmdBindVertexBuffers(cmd, 0, 1, &v->buffer.native, &offset);
    vkCmdBindIndexBuffer(cmd, i->buffer.native, 0,
                         step == 2 ? VK_INDEX_TYPE_UINT16 : VK_INDEX_TYPE_UINT32);
    vkCmdDrawIndexed(cmd, d.indexCount, 1, d.firstIndex, 0, 0);
    return true;
}

} // namespace
VulkanBundle CreateVulkanOffscreenDevice(std::uint32_t width, std::uint32_t height,
                                         VulkanDriver driver, VulkanOptions options) {
    VulkanBundle result;
    auto device = std::make_unique<VulkanDevice>();
    if (!device->Initialize(width, height, driver, options)) {
        result.creationReason = device->reason;
        result.creationError = device->stats.lastError;
        return result;
    }
    result.adapterName = device->properties.deviceName;
    result.device = std::move(device);
    return result;
}
VulkanStats QueryVulkanStats(Device& device) {
    const auto* native = dynamic_cast<const VulkanDevice*>(&device);
    if (!native)
        return {};
    auto stats = native->stats;
    stats.validationErrors = native->validationErrors.load();
    return stats;
}
bool SetVulkanQueueBlockedForTest(Device& base, bool blocked) {
    auto* d = dynamic_cast<VulkanDevice*>(&base);
    if (!d || !d->Live() || d->chain || d->inFrame || blocked == d->gateBlocked)
        return false;
    if (!blocked) {
        VkSemaphoreSignalInfo signal{VK_STRUCTURE_TYPE_SEMAPHORE_SIGNAL_INFO};
        signal.semaphore = d->testGate;
        signal.value = d->gateValue;
        if (!d->Check(d->vkSignalSemaphore(d->native, &signal)))
            return false;
        d->gateBlocked = false;
        return true;
    }
    if (!d->testGate) {
        VkSemaphoreTypeCreateInfo type{VK_STRUCTURE_TYPE_SEMAPHORE_TYPE_CREATE_INFO};
        type.semaphoreType = VK_SEMAPHORE_TYPE_TIMELINE;
        VkSemaphoreCreateInfo info{VK_STRUCTURE_TYPE_SEMAPHORE_CREATE_INFO};
        info.pNext = &type;
        if (!d->Check(d->vkCreateSemaphore(d->native, &info, nullptr, &d->testGate)))
            return false;
    }
    VkSemaphoreSubmitInfo wait{VK_STRUCTURE_TYPE_SEMAPHORE_SUBMIT_INFO};
    wait.semaphore = d->testGate;
    wait.value = ++d->gateValue;
    wait.stageMask = VK_PIPELINE_STAGE_2_ALL_COMMANDS_BIT;
    VkSubmitInfo2 submit{VK_STRUCTURE_TYPE_SUBMIT_INFO_2};
    submit.waitSemaphoreInfoCount = 1;
    submit.pWaitSemaphoreInfos = &wait;
    if (!d->Check(d->vkQueueSubmit2(d->queue, 1, &submit, VK_NULL_HANDLE)))
        return false;
    d->gateBlocked = true;
    return true;
}
bool ReadVulkanPresentation(Device& device, RgbaImage& out) {
    auto* d = dynamic_cast<VulkanDevice*>(&device);
    if (!d || !d->Live() || d->inFrame || !d->presentable || !d->acquiredImage ||
        !d->swapchainReadback)
        return false;
    auto& r = d->swapImages[d->activeImage];
    RgbaImage next;
    next.width = d->presentation.width;
    next.height = d->presentation.height;
    if (!d->Read(r, next.pixels))
        return false;
    if (r.format == VK_FORMAT_B8G8R8A8_UNORM)
        for (std::size_t i = 0; i < next.pixels.size(); i += 4)
            std::swap(next.pixels[i], next.pixels[i + 2]);
    out = std::move(next);
    return true;
}
#ifdef _WIN32
VulkanBundle CreateVulkanDevice(const SwapchainDesc& desc, VulkanDriver driver,
                                VulkanOptions options) {
    VulkanBundle result;
    auto device = std::make_unique<VulkanDevice>();
    if (!device->Initialize(desc.width, desc.height, driver, options, &desc)) {
        result.creationReason = device->reason;
        result.creationError = device->stats.lastError;
        return result;
    }
    result.adapterName = device->properties.deviceName;
    result.swapchain = std::make_unique<VulkanSwapchain>(*device);
    result.device = std::move(device);
    return result;
}
#endif
} // namespace mikudancestudio::rhi
