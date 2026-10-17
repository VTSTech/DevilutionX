#pragma once

// libslang-vk internal — shared pimpl definitions.
//
// The runner's Impl structs live here (not in the .cpp that owns the public
// methods) because runner translation units cross-reference each other:
// Image/Chain need Device::Impl's allocator + submit machinery, Chain reads
// Preset::Impl's compiled artifacts. Included only by src/vk/*.cpp.

#include <functional>

#include "vk_internal.hpp"

namespace slv {

// Core-layer vocabulary + internal Vulkan helpers, unqualified in these TUs.
using slv::preset::ParamInfo;
using slv::preset::PassDef;
using slv::preset::PassSpirv;
using slv::preset::SlangTranslation;
using vk::ValidationLog;

struct Device::Impl {
        bool adopted = false;
        VkInstance instance = VK_NULL_HANDLE;
        VkPhysicalDevice physicalDevice = VK_NULL_HANDLE;
        VkDevice device = VK_NULL_HANDLE;
        VkQueue graphicsQueue = VK_NULL_HANDLE;
        uint32_t graphicsFamily = 0;
        VkPhysicalDeviceProperties properties{};
        VmaAllocator allocator = VK_NULL_HANDLE;
        VkCommandPool commandPool = VK_NULL_HANDLE;
        VkDebugUtilsMessengerEXT messenger = VK_NULL_HANDLE;
        ValidationLog validation;
        VmaVulkanFunctions vulkanFunctions{}; // volk-bound fetchers (see adopt/create).

        ~Impl()
        {
                // Destroy in reverse dependency order: pool's command buffers
                // must die before the device (VUID-vkDestroyDevice-device-05137).
                if (messenger != VK_NULL_HANDLE && vkDestroyDebugUtilsMessengerEXT != nullptr)
                        vkDestroyDebugUtilsMessengerEXT(instance, messenger, nullptr);
                if (commandPool != VK_NULL_HANDLE)
                        vkDestroyCommandPool(device, commandPool, nullptr);
                if (allocator != VK_NULL_HANDLE)
                        vmaDestroyAllocator(allocator);
                if (device != VK_NULL_HANDLE && !adopted)
                        vkDestroyDevice(device, nullptr);
                if (instance != VK_NULL_HANDLE && !adopted)
                        vkDestroyInstance(instance, nullptr);
        }

        // One-shot command recording: allocates from the pool, records, submits
        // with a fence and waits. Deliberately synchronous (plan §6). The
        // optional wait semaphore is the M4 swapchain contract: the host's
        // acquire semaphore is waited at `waitDstStageMask` before the recorded
        // work runs; completion is fence-gated, so the host may present right
        // after this returns.
        auto SubmitAndWait(const std::function<void(VkCommandBuffer)> &record,
            VkSemaphore waitSemaphore = VK_NULL_HANDLE,
            VkPipelineStageFlags waitDstStageMask = VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT)
            -> std::expected<void, Error>
        {
                VkCommandBufferAllocateInfo allocInfo{};
                allocInfo.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO;
                allocInfo.commandPool = commandPool;
                allocInfo.level = VK_COMMAND_BUFFER_LEVEL_PRIMARY;
                allocInfo.commandBufferCount = 1;
                VkCommandBuffer cmd = VK_NULL_HANDLE;
                SLV_VK(vkAllocateCommandBuffers(device, &allocInfo, &cmd), Errc::vk_error);

                VkCommandBufferBeginInfo beginInfo{};
                beginInfo.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO;
                beginInfo.flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT;
                SLV_VK(vkBeginCommandBuffer(cmd, &beginInfo), Errc::vk_error);
                record(cmd);
                SLV_VK(vkEndCommandBuffer(cmd), Errc::vk_error);

                VkFenceCreateInfo fenceInfo{};
                fenceInfo.sType = VK_STRUCTURE_TYPE_FENCE_CREATE_INFO;
                VkFence fence = VK_NULL_HANDLE;
                SLV_VK(vkCreateFence(device, &fenceInfo, nullptr, &fence), Errc::vk_error);

                VkSubmitInfo submitInfo{};
                submitInfo.sType = VK_STRUCTURE_TYPE_SUBMIT_INFO;
                submitInfo.commandBufferCount = 1;
                submitInfo.pCommandBuffers = &cmd;
                if (waitSemaphore != VK_NULL_HANDLE) {
                        submitInfo.waitSemaphoreCount = 1;
                        submitInfo.pWaitSemaphores = &waitSemaphore;
                        submitInfo.pWaitDstStageMask = &waitDstStageMask;
                }
                const VkResult submitted = vkQueueSubmit(graphicsQueue, 1, &submitInfo, fence);
                if (submitted != VK_SUCCESS) {
                        vkDestroyFence(device, fence, nullptr);
                        return std::unexpected(SLV_VKERR(submitted, Errc::vk_error, "vkQueueSubmit"));
                }
                const VkResult waited = vkWaitForFences(device, 1, &fence, VK_TRUE, UINT64_MAX);
                vkDestroyFence(device, fence, nullptr);
                SLV_VK(waited, Errc::vk_error);
                SLV_VK(vkResetCommandPool(device, commandPool, 0), Errc::vk_error);
                return {};
        }
};

struct Preset::Impl {
        // Uploaded LUT texture (M3): RGBA8, R8G8B8A8_UNORM, full mip chain
        // when the .slangp sets `name_mipmap = true`. Owned here so chains
        // from the same preset share one upload; views are borrowed by
        // Chain::Impl::luts (Preset must outlive its Chains).
        struct LutGpu {
                std::string name; // Declared spelling (case-sensitive GLSL name).
                uint32_t width = 0;
                uint32_t height = 0;
                uint32_t mipLevels = 1;
                vk::VkImageWithView img;
        };

        Device *device = nullptr; // Borrowed; the host must destroy the Preset before the Device.
        std::vector<PassDef> passDefs;
        std::vector<SlangTranslation> translations;
        std::vector<PassSpirv> spirv;
        std::vector<ParameterInfo> parameters;
        std::vector<LutGpu> luts;
        slv::preset::Preset parsed; // Core data model (textures, feedback, histories).

        ~Impl()
        {
                if (device == nullptr)
                        return;
                VkDevice dev = device->device();
                for (LutGpu &lut : luts) {
                        if (lut.img.view != VK_NULL_HANDLE)
                                vkDestroyImageView(dev, lut.img.view, nullptr);
                        if (lut.img.image != VK_NULL_HANDLE)
                                vmaDestroyImage(device->impl().allocator, lut.img.image, lut.img.allocation);
                }
        }
};

// LUT upload (M3; defined in lut.cpp). LoadLutPixels decodes an image file
// to tightly packed RGBA8 (stb_image; git-lfs pointer files and unsupported
// formats fail with a named error). UploadLutTexture uploads it as
// R8G8B8A8_UNORM with the TextureDef's own options (full mip chain when
// `name_mipmap = true`) and fills `out` (name/width/height/mipLevels/img).
namespace vk {
auto LoadLutPixels(const std::string &absolutePath, std::vector<uint8_t> &outRgba, uint32_t &outWidth,
    uint32_t &outHeight, std::string &error) -> bool;
auto UploadLutTexture(Device &device, const slv::preset::TextureDef &def, const std::string &resolvedPath,
    Preset::Impl::LutGpu &out) -> std::expected<void, Error>;
} // namespace vk

} // namespace slv
