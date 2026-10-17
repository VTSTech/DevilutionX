#include "engine/render/gpu/slv_runner.hpp"

#ifdef USE_GPU_SCALER
#ifndef USE_SDL1

// volk.h must come first: it pulls in <vulkan/vulkan.h> with VK_NO_PROTOTYPES
// (defined by the slv-volk target) and provides every entry point as a
// runtime-loaded function pointer, so neither the adapter nor the game needs
// a Vulkan SDK or a link-time loader dependency. libslang-vk loads the
// library during Device::create; the loads below only repopulate the same
// global table for this translation unit's WSI calls.
#include <volk.h>

#include <SDL3/SDL_vulkan.h>
#include <SDL3/SDL_video.h>

#include <algorithm>
#include <cmath>
#include <cstring>
#include <filesystem>
#include <limits>
#include <optional>
#include <utility>
#include <vector>

#include <slangvk/runner.hpp>

#include "utils/log.hpp"
#include "utils/str_cat.hpp"

namespace devilution {

namespace gpu_scaler {
namespace {

// Pixel format of the converted game frame (same mapping as the SDL_GPU
// path's GameTextureFormat()).
VkFormat GameTextureVkFormat()
{
        switch (DEVILUTIONX_DISPLAY_TEXTURE_FORMAT) {
        case SDL_PIXELFORMAT_ABGR8888:
                return VK_FORMAT_R8G8B8A8_UNORM;
        case SDL_PIXELFORMAT_ARGB8888:
        case SDL_PIXELFORMAT_XRGB8888:
        default:
                return VK_FORMAT_B8G8R8A8_UNORM;
        }
}

// Timeout for the swapchain image acquire, mirroring the SDL_GPU path's
// "non-blocking on purpose" contract: a blocking acquire can stall the main
// thread indefinitely while minimized, which looks like a frozen UI. A
// timed-out acquire just skips the frame.
constexpr uint64_t kAcquireTimeoutNs = 50'000'000; // 50 ms

} // namespace

struct SlvRunner::Impl {
        SDL_Window *window = nullptr;
        bool vsync = false;

        std::optional<slv::Device> device;

        VkSurfaceKHR surface = VK_NULL_HANDLE;
        VkSwapchainKHR swapchain = VK_NULL_HANDLE;
        std::vector<VkImage> images;
        std::vector<VkImageView> views;
        VkFormat format = VK_FORMAT_UNDEFINED;
        VkExtent2D extent { 0, 0 };
        VkSemaphore acquireSemaphore = VK_NULL_HANDLE;
        // Set when the next Present must rebuild the swapchain first (out of
        // date or suboptimal acquire/present).
        bool swapchainDirty = false;

        std::optional<slv::Preset> preset;
        std::optional<slv::Chain> chain;
        std::string loadedPresetPath;

        std::optional<slv::Image> gameImage;
        // Tightly packed copy of the SDL_Surface frame: slv::Image::update
        // requires pitch == width * 4, which the game surface does not promise.
        std::vector<uint8_t> staging;
        int gameWidth = 0;
        int gameHeight = 0;

        // Sizes the chain was last (re)built or resized for, so Present only
        // calls resize() when something actually changed.
        uint32_t chainInputWidth = 0;
        uint32_t chainInputHeight = 0;
        uint32_t chainOutputWidth = 0;
        uint32_t chainOutputHeight = 0;

        uint64_t frameCount = 0;

        bool DestroySwapchain();
        bool CreateSwapchain(std::string &error);
        bool EnsureSwapchainCurrent();
        bool RecreateAcquireSemaphore();
        bool EnsureGameImage(int width, int height, std::string &error);
        slv::Rect ComputePresentRect(bool integerScaling) const;
};

bool WantsVulkanWindow()
{
        const char *backend = SDL_getenv("DEVILUTIONX_GPU_BACKEND");
        return backend != nullptr && std::strcmp(backend, "slangvk") == 0;
}

bool SlvRunner::Impl::DestroySwapchain()
{
        if (device && device->device() != VK_NULL_HANDLE)
                vkDeviceWaitIdle(device->device());
        for (const VkImageView view : views) {
                if (view != VK_NULL_HANDLE)
                        vkDestroyImageView(device->device(), view, nullptr);
        }
        views.clear();
        images.clear();
        if (swapchain != VK_NULL_HANDLE) {
                vkDestroySwapchainKHR(device->device(), swapchain, nullptr);
                swapchain = VK_NULL_HANDLE;
        }
        return true;
}

bool SlvRunner::Impl::CreateSwapchain(std::string &error)
{
        const VkPhysicalDevice physicalDevice = device->physicalDevice();

        VkSurfaceCapabilitiesKHR caps {};
        vkGetPhysicalDeviceSurfaceCapabilitiesKHR(physicalDevice, surface, &caps);

        // A minimized window reports a zero extent; keep the old swapchain and
        // let Present skip frames until the window is back.
        if (caps.currentExtent.width == 0 || caps.currentExtent.height == 0) {
                error = "surface extent is zero (window minimized?)";
                return false;
        }
        VkExtent2D newExtent = caps.currentExtent;
        if (newExtent.width == std::numeric_limits<uint32_t>::max()
            || newExtent.height == std::numeric_limits<uint32_t>::max()) {
                int pixelWidth = 0;
                int pixelHeight = 0;
                SDL_GetWindowSizeInPixels(window, &pixelWidth, &pixelHeight);
                newExtent.width = static_cast<uint32_t>(std::max(pixelWidth, 1));
                newExtent.height = static_cast<uint32_t>(std::max(pixelHeight, 1));
                newExtent.width = std::clamp(newExtent.width, caps.minImageExtent.width, caps.maxImageExtent.width);
                newExtent.height = std::clamp(newExtent.height, caps.minImageExtent.height, caps.maxImageExtent.height);
        }

        // Surface format: prefer an 8-bit UNORM format with the non-linear
        // SRGB color space, matching the SDL_GPU swapchain composition (SDR).
        uint32_t formatCount = 0;
        vkGetPhysicalDeviceSurfaceFormatsKHR(physicalDevice, surface, &formatCount, nullptr);
        if (formatCount == 0) {
                error = "no supported surface formats";
                return false;
        }
        std::vector<VkSurfaceFormatKHR> formats(formatCount);
        vkGetPhysicalDeviceSurfaceFormatsKHR(physicalDevice, surface, &formatCount, formats.data());
        VkSurfaceFormatKHR chosen = formats[0];
        for (const VkSurfaceFormatKHR &candidate : formats) {
                if (candidate.colorSpace != VK_COLOR_SPACE_SRGB_NONLINEAR_KHR)
                        continue;
                if (candidate.format == VK_FORMAT_B8G8R8A8_UNORM || candidate.format == VK_FORMAT_R8G8B8A8_UNORM) {
                        chosen = candidate;
                        break;
                }
                if (chosen.format == VK_FORMAT_UNDEFINED)
                        chosen = candidate;
        }

        // Present mode: FIFO is always supported and is the vsync choice; for
        // immediate presentation prefer IMMEDIATE (tearing allowed), matching
        // SDL_GPU_PRESENTMODE_IMMEDIATE.
        uint32_t presentModeCount = 0;
        vkGetPhysicalDeviceSurfacePresentModesKHR(physicalDevice, surface, &presentModeCount, nullptr);
        std::vector<VkPresentModeKHR> presentModes(presentModeCount);
        vkGetPhysicalDeviceSurfacePresentModesKHR(physicalDevice, surface, &presentModeCount, presentModes.data());
        VkPresentModeKHR presentMode = VK_PRESENT_MODE_FIFO_KHR;
        if (!vsync) {
                for (const VkPresentModeKHR candidate : presentModes) {
                        if (candidate == VK_PRESENT_MODE_IMMEDIATE_KHR) {
                                presentMode = VK_PRESENT_MODE_IMMEDIATE_KHR;
                                break;
                        }
                }
        }

        uint32_t imageCount = caps.minImageCount + 1;
        if (imageCount < 3)
                imageCount = 3; // triple buffering when the surface allows it
        if (caps.maxImageCount != 0 && imageCount > caps.maxImageCount)
                imageCount = caps.maxImageCount;

        VkSwapchainCreateInfoKHR info {};
        info.sType = VK_STRUCTURE_TYPE_SWAPCHAIN_CREATE_INFO_KHR;
        info.surface = surface;
        info.minImageCount = imageCount;
        info.imageFormat = chosen.format;
        info.imageColorSpace = chosen.colorSpace;
        info.imageExtent = newExtent;
        info.imageArrayLayers = 1;
        info.imageUsage = VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT;
        info.imageSharingMode = VK_SHARING_MODE_EXCLUSIVE;
        info.preTransform = caps.currentTransform;
        info.compositeAlpha = VK_COMPOSITE_ALPHA_OPAQUE_BIT_KHR;
        info.presentMode = presentMode;
        info.clipped = VK_TRUE;
        info.oldSwapchain = swapchain;

        VkSwapchainKHR newSwapchain = VK_NULL_HANDLE;
        if (vkCreateSwapchainKHR(device->device(), &info, nullptr, &newSwapchain) != VK_SUCCESS) {
                error = "vkCreateSwapchainKHR failed";
                return false;
        }

        // Keep the old swapchain alive only until the new one exists.
        const VkSwapchainKHR oldSwapchain = swapchain;
        swapchain = newSwapchain;

        uint32_t actualImageCount = 0;
        vkGetSwapchainImagesKHR(device->device(), swapchain, &actualImageCount, nullptr);
        std::vector<VkImage> newImages(actualImageCount);
        vkGetSwapchainImagesKHR(device->device(), swapchain, &actualImageCount, newImages.data());
        std::vector<VkImageView> newViews(actualImageCount);
        for (uint32_t i = 0; i < actualImageCount; ++i) {
                VkImageViewCreateInfo viewInfo {};
                viewInfo.sType = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO;
                viewInfo.image = newImages[i];
                viewInfo.viewType = VK_IMAGE_VIEW_TYPE_2D;
                viewInfo.format = chosen.format;
                viewInfo.subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
                viewInfo.subresourceRange.levelCount = 1;
                viewInfo.subresourceRange.layerCount = 1;
                if (vkCreateImageView(device->device(), &viewInfo, nullptr, &newViews[i]) != VK_SUCCESS) {
                        newViews.resize(i); // only destroy the views that were created
                        for (const VkImageView view : newViews)
                                vkDestroyImageView(device->device(), view, nullptr);
                        swapchain = oldSwapchain;
                        vkDestroySwapchainKHR(device->device(), newSwapchain, nullptr);
                        error = "swapchain image view creation failed";
                        return false;
                }
        }

        if (oldSwapchain != VK_NULL_HANDLE) {
                for (const VkImageView view : views)
                        vkDestroyImageView(device->device(), view, nullptr);
                vkDestroySwapchainKHR(device->device(), oldSwapchain, nullptr);
        }
        images = std::move(newImages);
        views = std::move(newViews);
        format = chosen.format;
        extent = newExtent;
        swapchainDirty = false;
        return true;
}

bool SlvRunner::Impl::EnsureSwapchainCurrent()
{
        if (device && device->device() != VK_NULL_HANDLE) {
                VkSurfaceCapabilitiesKHR caps {};
                vkGetPhysicalDeviceSurfaceCapabilitiesKHR(device->physicalDevice(), surface, &caps);
                if (caps.currentExtent.width == 0 || caps.currentExtent.height == 0)
                        return false; // minimized: skip this frame
                if (caps.currentExtent.width != extent.width || caps.currentExtent.height != extent.height)
                        swapchainDirty = true;
        }
        if (swapchainDirty) {
                std::string error;
                if (!CreateSwapchain(error)) {
                        LogError("GPU scaler: slang-vk swapchain rebuild failed: {}", error);
                        return false;
                }
                // The window size drives both the chain output size and the
                // present rect; forcing the size check below keeps them in step.
                chainOutputWidth = 0;
                chainOutputHeight = 0;
        }
        return true;
}

bool SlvRunner::Impl::RecreateAcquireSemaphore()
{
        if (device && device->device() != VK_NULL_HANDLE) {
                if (acquireSemaphore != VK_NULL_HANDLE)
                        vkDestroySemaphore(device->device(), acquireSemaphore, nullptr);
                VkSemaphoreCreateInfo semInfo {};
                semInfo.sType = VK_STRUCTURE_TYPE_SEMAPHORE_CREATE_INFO;
                if (vkCreateSemaphore(device->device(), &semInfo, nullptr, &acquireSemaphore) != VK_SUCCESS) {
                        acquireSemaphore = VK_NULL_HANDLE;
                        return false;
                }
        }
        return true;
}

bool SlvRunner::Impl::EnsureGameImage(int width, int height, std::string &error)
{
        if (gameImage && gameWidth == width && gameHeight == height)
                return true;
        gameImage.reset();
        gameWidth = 0;
        gameHeight = 0;

        slv::Image::CreateInfo info {};
        info.desc.width = static_cast<uint32_t>(width);
        info.desc.height = static_cast<uint32_t>(height);
        info.desc.format = GameTextureVkFormat();
        // The default usage already covers what the runner needs: sampled by
        // the chain, updated per frame through the staging path, blitted into
        // history rings / mip-capable copies.
        auto image = slv::Image::create(*device, info);
        if (!image) {
                error = StrCat("input image creation failed: ", image.error().message);
                return false;
        }
        gameImage.emplace(std::move(*image));
        gameWidth = width;
        gameHeight = height;
        return true;
}

slv::Rect SlvRunner::Impl::ComputePresentRect(bool integerScaling) const
{
        // Zero rect delegates the fit-and-center math to the library.
        slv::Rect rect {};

        uint32_t outWidth = 0;
        uint32_t outHeight = 0;
        if (chain && preset && preset->passCount() > 0) {
                auto size = chain->passOutputSize(preset->passCount() - 1);
                if (size) {
                        outWidth = size->first;
                        outHeight = size->second;
                }
        }
        if (outWidth == 0 || outHeight == 0)
                return rect;
        if (extent.width == 0 || extent.height == 0)
                return rect;

        float fitScale = std::min(static_cast<float>(extent.width) / static_cast<float>(outWidth),
            static_cast<float>(extent.height) / static_cast<float>(outHeight));
        if (integerScaling)
                fitScale = std::floor(fitScale);

        int destWidth = static_cast<int>(std::lround(static_cast<float>(outWidth) * fitScale));
        int destHeight = static_cast<int>(std::lround(static_cast<float>(outHeight) * fitScale));
        // Rounding can overshoot the swapchain by a pixel; clamp so the rect
        // never exceeds the target (same guard as the SDL_GPU path).
        destWidth = std::min(destWidth, static_cast<int>(extent.width));
        destHeight = std::min(destHeight, static_cast<int>(extent.height));
        if (destWidth <= 0 || destHeight <= 0)
                return rect;

        rect.x = (static_cast<int32_t>(extent.width) - destWidth) / 2;
        rect.y = (static_cast<int32_t>(extent.height) - destHeight) / 2;
        rect.width = static_cast<uint32_t>(destWidth);
        rect.height = static_cast<uint32_t>(destHeight);
        return rect;
}

SlvRunner::SlvRunner()
    : impl_(std::make_unique<Impl>())
{
}

SlvRunner::~SlvRunner() = default;

bool SlvRunner::Init(SDL_Window *window, bool vsync, std::string &error)
{
        Shutdown();
        error.clear();

        impl_->window = window;
        impl_->vsync = vsync;

        // Instance extensions: SDL lists the platform surface extensions the
        // created window can provide (VK_KHR_surface included).
        uint32_t extensionCount = 0;
        const char *const *extensionNames = SDL_Vulkan_GetInstanceExtensions(&extensionCount);
        if (extensionNames == nullptr || extensionCount == 0) {
                error = StrCat("SDL_Vulkan_GetInstanceExtensions failed: ", SDL_GetError());
                Shutdown();
                return false;
        }

        slv::DeviceOptions options;
        options.appName = "DevilutionX";
        options.minApiVersion = VK_API_VERSION_1_1;
        options.validationLayers = SDL_getenv("DEVILUTIONX_SLV_VALIDATION") != nullptr
            && *SDL_getenv("DEVILUTIONX_SLV_VALIDATION") != '\0';
        options.instanceExtensions.assign(extensionNames, extensionNames + extensionCount);
        options.deviceExtensions = { VK_KHR_SWAPCHAIN_EXTENSION_NAME };

        auto device = slv::Device::create(options);
        if (!device) {
                error = StrCat("slv::Device::create failed: ", device.error().message);
                Shutdown();
                return false;
        }
        impl_->device.emplace(std::move(*device));

        // Repopulate volk's global table for this TU's WSI calls. Device::create
        // already loaded the loader; both calls are idempotent.
        volkLoadInstance(impl_->device->instance());
        volkLoadDevice(impl_->device->device());

        if (!SDL_Vulkan_CreateSurface(window, impl_->device->instance(), nullptr, &impl_->surface)) {
                error = StrCat("SDL_Vulkan_CreateSurface failed: ", SDL_GetError());
                Shutdown();
                return false;
        }

        // The library picks a graphics queue family on its own; presentation
        // from that family is required for this path (on desktop drivers the
        // graphics family is present-capable, but verify instead of assuming).
        VkBool32 presentSupported = VK_FALSE;
        vkGetPhysicalDeviceSurfaceSupportKHR(impl_->device->physicalDevice(), impl_->device->graphicsFamily(),
            impl_->surface, &presentSupported);
        if (!presentSupported) {
                error = "the graphics queue family does not support presenting to this surface";
                Shutdown();
                return false;
        }

        if (!impl_->CreateSwapchain(error)) {
                Shutdown();
                return false;
        }
        if (!impl_->RecreateAcquireSemaphore()) {
                error = "acquire semaphore creation failed";
                Shutdown();
                return false;
        }

        Log("GPU scaler: slang-vk backend ready ({}x{}, swapchain images: {})",
            impl_->extent.width, impl_->extent.height, impl_->images.size());
        return true;
}

void SlvRunner::Shutdown()
{
        if (impl_ == nullptr)
                return;
        impl_->chain.reset();
        impl_->preset.reset();
        impl_->gameImage.reset();
        impl_->staging.clear();
        impl_->staging.shrink_to_fit();
        impl_->gameWidth = 0;
        impl_->gameHeight = 0;
        impl_->loadedPresetPath.clear();
        impl_->chainInputWidth = 0;
        impl_->chainInputHeight = 0;
        impl_->chainOutputWidth = 0;
        impl_->chainOutputHeight = 0;
        impl_->frameCount = 0;
        impl_->swapchainDirty = false;
        if (impl_->device && impl_->device->device() != VK_NULL_HANDLE) {
                if (impl_->acquireSemaphore != VK_NULL_HANDLE)
                        vkDestroySemaphore(impl_->device->device(), impl_->acquireSemaphore, nullptr);
                impl_->acquireSemaphore = VK_NULL_HANDLE;
                impl_->DestroySwapchain();
                if (impl_->surface != VK_NULL_HANDLE) {
                        // SDL_Vulkan_DestroySurface wraps vkDestroySurfaceKHR with the
                        // same instance the surface was created on.
                        SDL_Vulkan_DestroySurface(impl_->device->instance(), impl_->surface, nullptr);
                        impl_->surface = VK_NULL_HANDLE;
                }
        }
        impl_->device.reset();
        impl_->window = nullptr;
}

bool SlvRunner::IsReady() const
{
        return impl_ != nullptr && impl_->device.has_value() && impl_->swapchain != VK_NULL_HANDLE
            && impl_->acquireSemaphore != VK_NULL_HANDLE;
}

bool SlvRunner::LoadPreset(const std::string &path, std::string &error)
{
        error.clear();
        if (!IsReady()) {
                error = "backend not initialized";
                return false;
        }
        UnloadPreset();

        auto preset = slv::Preset::load(*impl_->device, std::filesystem::path(path), {});
        if (!preset) {
                error = preset.error().message;
                return false;
        }
        impl_->preset.emplace(std::move(*preset));

        slv::ChainOptions chainOptions;
        chainOptions.outputWidth = impl_->extent.width;
        chainOptions.outputHeight = impl_->extent.height;
        chainOptions.outputFormat = impl_->format;
        auto chain = slv::Chain::create(*impl_->device, *impl_->preset, chainOptions);
        if (!chain) {
                impl_->preset.reset();
                error = chain.error().message;
                return false;
        }
        impl_->chain.emplace(std::move(*chain));
        impl_->loadedPresetPath = path;
        impl_->chainInputWidth = 0;
        impl_->chainInputHeight = 0;
        impl_->chainOutputWidth = 0;
        impl_->chainOutputHeight = 0;
        Log("GPU scaler: slang-vk chain created ({} passes)", impl_->preset->passCount());
        return true;
}

void SlvRunner::UnloadPreset()
{
        if (impl_ == nullptr)
                return;
        impl_->chain.reset();
        impl_->preset.reset();
        impl_->loadedPresetPath.clear();
        impl_->chainInputWidth = 0;
        impl_->chainInputHeight = 0;
        impl_->chainOutputWidth = 0;
        impl_->chainOutputHeight = 0;
}

const std::string &SlvRunner::LoadedPresetPath() const
{
        static const std::string kEmpty;
        return impl_ != nullptr ? impl_->loadedPresetPath : kEmpty;
}

bool SlvRunner::Present(const SDL_Surface &frame, uint64_t frameDeltaUs, bool integerScaling)
{
        if (!IsReady() || !impl_->chain || !impl_->preset)
                return false;
        if (frame.pixels == nullptr || frame.w <= 0 || frame.h <= 0)
                return false;

        if (!impl_->EnsureSwapchainCurrent())
                return false;

        // Upload path: copy the frame into a tightly packed staging buffer
        // (the surface pitch is not promised to equal width * 4), then let the
        // library stage it into the input image.
        std::string imageError;
        if (!impl_->EnsureGameImage(frame.w, frame.h, imageError)) {
                LogError("GPU scaler: slang-vk input image failed: {}", imageError);
                return false;
        }
        const size_t tightlyPacked = static_cast<size_t>(frame.w) * static_cast<size_t>(frame.h) * 4;
        impl_->staging.resize(tightlyPacked);
        {
                const size_t bytesPerRow = static_cast<size_t>(frame.w) * 4;
                auto *dst = impl_->staging.data();
                const auto *src = static_cast<const uint8_t *>(frame.pixels);
                if (static_cast<size_t>(frame.pitch) == bytesPerRow) {
                        std::memcpy(dst, src, tightlyPacked);
                } else {
                        for (int y = 0; y < frame.h; ++y)
                                std::memcpy(dst + static_cast<size_t>(y) * bytesPerRow,
                                    src + static_cast<size_t>(y) * static_cast<size_t>(frame.pitch), bytesPerRow);
                }
        }
        if (!impl_->gameImage->update(impl_->staging.data(), impl_->staging.size())) {
                LogError("GPU scaler: slang-vk frame upload failed");
                return false;
        }

        // Keep the chain's pass FBOs sized for the current input and output.
        if (impl_->chainInputWidth != static_cast<uint32_t>(frame.w)
            || impl_->chainInputHeight != static_cast<uint32_t>(frame.h)
            || impl_->chainOutputWidth != impl_->extent.width
            || impl_->chainOutputHeight != impl_->extent.height) {
                auto resized = impl_->chain->resize(static_cast<uint32_t>(frame.w), static_cast<uint32_t>(frame.h),
                    impl_->extent.width, impl_->extent.height);
                if (!resized) {
                        LogError("GPU scaler: slang-vk chain resize failed: {}", resized.error().message);
                        return false;
                }
                impl_->chainInputWidth = static_cast<uint32_t>(frame.w);
                impl_->chainInputHeight = static_cast<uint32_t>(frame.h);
                impl_->chainOutputWidth = impl_->extent.width;
                impl_->chainOutputHeight = impl_->extent.height;
        }

        // Acquire (timed out acquires skip the frame; the SDL_GPU path has the
        // same skip-frame contract for unavailable swapchain images).
        uint32_t imageIndex = 0;
        const VkResult acquired = vkAcquireNextImageKHR(impl_->device->device(), impl_->swapchain, kAcquireTimeoutNs,
            impl_->acquireSemaphore, VK_NULL_HANDLE, &imageIndex);
        if (acquired == VK_ERROR_OUT_OF_DATE_KHR) {
                impl_->swapchainDirty = true;
                return false;
        }
        if (acquired == VK_TIMEOUT || acquired == VK_NOT_READY) {
                // No image became available in time (minimized window); skip.
                return false;
        }
        if (acquired != VK_SUCCESS && acquired != VK_SUBOPTIMAL_KHR) {
                LogError("GPU scaler: slang-vk vkAcquireNextImageKHR failed ({})", static_cast<int>(acquired));
                return false;
        }
        if (acquired == VK_SUBOPTIMAL_KHR)
                impl_->swapchainDirty = true; // rebuild after this frame

        slv::SwapchainFrame target;
        target.swapchainImageView = impl_->views[imageIndex];
        target.format = impl_->format;
        target.width = impl_->extent.width;
        target.height = impl_->extent.height;
        target.outputRect = impl_->ComputePresentRect(integerScaling);
        target.acquireSemaphore = impl_->acquireSemaphore;

        slv::InputFrame input;
        input.view = impl_->gameImage->view();
        input.image = impl_->gameImage->image();
        input.format = GameTextureVkFormat();
        input.width = static_cast<uint32_t>(frame.w);
        input.height = static_cast<uint32_t>(frame.h);
        input.rotation = slv::Rotation::None; // orientation is a shader-side concern, as in the SDL_GPU path

        slv::FrameInfo frameInfo;
        frameInfo.frameCount = impl_->frameCount++;
        frameInfo.frameDeltaUs = static_cast<float>(frameDeltaUs);
        frameInfo.frameDirection = 1;

        auto rendered = impl_->chain->render(input, target, frameInfo);
        if (!rendered) {
                LogError("GPU scaler: slang-vk chain render failed: {}", rendered.error().message);
                // The acquire semaphore protocol may be out of step after a failed
                // render (signaled without a wait); rebuild it so the next acquire
                // is well-formed.
                impl_->RecreateAcquireSemaphore();
                return false;
        }

        VkPresentInfoKHR present {};
        present.sType = VK_STRUCTURE_TYPE_PRESENT_INFO_KHR;
        present.swapchainCount = 1;
        present.pSwapchains = &impl_->swapchain;
        present.pImageIndices = &imageIndex;
        // Chain::render is synchronous (fenced), so the present needs no wait
        // semaphore (the documented SwapchainFrame contract of the library).
        const VkResult presented = vkQueuePresentKHR(impl_->device->graphicsQueue(), &present);
        if (presented == VK_ERROR_OUT_OF_DATE_KHR || presented == VK_SUBOPTIMAL_KHR) {
                impl_->swapchainDirty = true;
        } else if (presented != VK_SUCCESS) {
                LogError("GPU scaler: slang-vk vkQueuePresentKHR failed ({})", static_cast<int>(presented));
                return false;
        }
        return true;
}

} // namespace gpu_scaler
} // namespace devilution

#endif // !USE_SDL1
#endif // USE_GPU_SCALER
