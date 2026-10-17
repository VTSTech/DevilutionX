// libslang-vk — Device implementation (M1, plan §6/§8).
//
// Two construction paths:
//  - Device::create(): headless/CI — instance + device created internally,
//    physical device picked by type score (discrete > integrated > cpu).
//  - Device::adopt(): host context (DevilutionX mode) — instance/device/queue
//    stay host-owned; the runner layers allocator + command machinery on top.
//
// volk loads the system Vulkan library at runtime; the process-global
// function pointers mean one libslang-vk Device per process for M1
// (documented limitation; per-device tables are an M2 refinement).

#include <algorithm>
#include <cstring>
#include <functional>
#include <utility>

#include "vk_internal.hpp"
#include "impls.hpp"

namespace slv::vk {

auto VkResultToString(VkResult result) -> const char *
{
        switch (result) {
                case VK_SUCCESS: return "VK_SUCCESS";
                case VK_NOT_READY: return "VK_NOT_READY";
                case VK_TIMEOUT: return "VK_TIMEOUT";
                case VK_ERROR_OUT_OF_HOST_MEMORY: return "VK_ERROR_OUT_OF_HOST_MEMORY";
                case VK_ERROR_OUT_OF_DEVICE_MEMORY: return "VK_ERROR_OUT_OF_DEVICE_MEMORY";
                case VK_ERROR_INITIALIZATION_FAILED: return "VK_ERROR_INITIALIZATION_FAILED";
                case VK_ERROR_DEVICE_LOST: return "VK_ERROR_DEVICE_LOST";
                case VK_ERROR_EXTENSION_NOT_PRESENT: return "VK_ERROR_EXTENSION_NOT_PRESENT";
                case VK_ERROR_LAYER_NOT_PRESENT: return "VK_ERROR_LAYER_NOT_PRESENT";
                case VK_ERROR_INCOMPATIBLE_DRIVER: return "VK_ERROR_INCOMPATIBLE_DRIVER";
                default: return "VK_ERROR (see vulkan_core.h)";
        }
}

VKAPI_ATTR VkBool32 VKAPI_CALL ValidationCallback(VkDebugUtilsMessageSeverityFlagBitsEXT severity,
    VkDebugUtilsMessageTypeFlagsEXT /*types*/, const VkDebugUtilsMessengerCallbackDataEXT *data, void *userData)
{
        auto *log = static_cast<ValidationLog *>(userData);
        if (data->pMessage == nullptr)
                return VK_FALSE;
        if ((severity & VK_DEBUG_UTILS_MESSAGE_SEVERITY_ERROR_BIT_EXT) != 0)
                log->errors.emplace_back(data->pMessage);
        else if ((severity & VK_DEBUG_UTILS_MESSAGE_SEVERITY_WARNING_BIT_EXT) != 0)
                log->warnings.emplace_back(data->pMessage);
        return VK_FALSE;
}

} // namespace slv::vk

namespace slv {

namespace {

// Physical device choice: graphics queue required; type score with a minor
// version tiebreak (discrete > integrated > virtual > cpu).
auto PickPhysicalDevice(VkInstance instance, uint32_t minApiVersion)
    -> std::expected<std::pair<VkPhysicalDevice, uint32_t>, Error>
{
        uint32_t count = 0;
        SLV_VK(vkEnumeratePhysicalDevices(instance, &count, nullptr), Errc::vk_init_failed);
        if (count == 0)
                return std::unexpected(SLV_ERROR(Errc::vk_init_failed, "no Vulkan physical devices found"));
        std::vector<VkPhysicalDevice> devices(count);
        SLV_VK(vkEnumeratePhysicalDevices(instance, &count, devices.data()), Errc::vk_init_failed);

        VkPhysicalDevice best = VK_NULL_HANDLE;
        uint32_t bestFamily = 0;
        int bestScore = -1;
        for (VkPhysicalDevice dev : devices) {
                VkPhysicalDeviceProperties props{};
                vkGetPhysicalDeviceProperties(dev, &props);
                if (props.apiVersion < minApiVersion)
                        continue;
                uint32_t families = 0;
                vkGetPhysicalDeviceQueueFamilyProperties(dev, &families, nullptr);
                std::vector<VkQueueFamilyProperties> queueProps(families);
                vkGetPhysicalDeviceQueueFamilyProperties(dev, &families, queueProps.data());
                for (uint32_t family = 0; family < families; ++family) {
                        if ((queueProps[family].queueFlags & VK_QUEUE_GRAPHICS_BIT) == 0)
                                continue;
                        int score = 0;
                        switch (props.deviceType) {
                                case VK_PHYSICAL_DEVICE_TYPE_DISCRETE_GPU: score = 40; break;
                                case VK_PHYSICAL_DEVICE_TYPE_INTEGRATED_GPU: score = 30; break;
                                case VK_PHYSICAL_DEVICE_TYPE_VIRTUAL_GPU: score = 20; break;
                                case VK_PHYSICAL_DEVICE_TYPE_CPU: score = 10; break;
                                default: score = 5; break;
                        }
                        score += static_cast<int>(props.apiVersion >> 12); // minor-version tiebreak
                        if (score > bestScore) {
                                bestScore = score;
                                best = dev;
                                bestFamily = family;
                        }
                        break; // first graphics family of this device is enough
                }
        }
        if (best == VK_NULL_HANDLE)
                return std::unexpected(
                    SLV_ERROR(Errc::vk_init_failed, "no physical device with a graphics queue and sufficient API version"));
        return std::make_pair(best, bestFamily);
}

// Debug messenger over the device's validation log (silently off when the
// debug-utils extension was not enabled at instance creation).
auto EnableValidation(VkInstance instance, ValidationLog &log) -> VkDebugUtilsMessengerEXT
{
        if (vkCreateDebugUtilsMessengerEXT == nullptr)
                return VK_NULL_HANDLE; // Extension not present; validation silently off.
        VkDebugUtilsMessengerCreateInfoEXT info{};
        info.sType = VK_STRUCTURE_TYPE_DEBUG_UTILS_MESSENGER_CREATE_INFO_EXT;
        info.messageSeverity = VK_DEBUG_UTILS_MESSAGE_SEVERITY_WARNING_BIT_EXT
            | VK_DEBUG_UTILS_MESSAGE_SEVERITY_ERROR_BIT_EXT;
        info.messageType = VK_DEBUG_UTILS_MESSAGE_TYPE_GENERAL_BIT_EXT | VK_DEBUG_UTILS_MESSAGE_TYPE_VALIDATION_BIT_EXT
            | VK_DEBUG_UTILS_MESSAGE_TYPE_PERFORMANCE_BIT_EXT;
        info.pfnUserCallback = &vk::ValidationCallback;
        info.pUserData = &log;
        VkDebugUtilsMessengerEXT messenger = VK_NULL_HANDLE;
        if (vkCreateDebugUtilsMessengerEXT(instance, &info, nullptr, &messenger) != VK_SUCCESS)
                return VK_NULL_HANDLE;
        return messenger;
}

} // namespace

Device::~Device() = default;
Device::Device(Device &&) noexcept = default;
auto Device::operator=(Device &&) noexcept -> Device & = default;

auto Device::adopt(VkInstance instance, VkPhysicalDevice physicalDevice, VkDevice device, VkQueue graphicsQueue,
    uint32_t graphicsFamily) -> std::expected<Device, Error>
{
        if (instance == VK_NULL_HANDLE || device == VK_NULL_HANDLE || graphicsQueue == VK_NULL_HANDLE)
                return std::unexpected(SLV_ERROR(Errc::invalid_argument, "Device::adopt: null Vulkan handle"));

        if (volkInitialize() != VK_SUCCESS)
                return std::unexpected(SLV_ERROR(Errc::vk_init_failed, "failed to load the Vulkan loader (libvulkan)"));

        Device out;
        out.impl_ = std::make_unique<Impl>();
        Impl &impl = *out.impl_;
        impl.adopted = true;
        impl.instance = instance;
        impl.physicalDevice = physicalDevice;
        impl.device = device;
        impl.graphicsQueue = graphicsQueue;
        impl.graphicsFamily = graphicsFamily;
        if (physicalDevice != VK_NULL_HANDLE)
                vkGetPhysicalDeviceProperties(physicalDevice, &impl.properties);

        // Bind the global volk tables to this context (one device per process
        // for M1 — documented limitation). The messenger must be created AFTER
        // this: vkCreateDebugUtilsMessengerEXT is a volk instance function and
        // is null until volkLoadInstance runs.
        volkLoadInstance(instance);
        volkLoadDevice(device);
        impl.messenger = EnableValidation(instance, impl.validation);

        VmaAllocatorCreateInfo allocInfo{};
        allocInfo.physicalDevice = physicalDevice;
        allocInfo.device = device;
        allocInfo.instance = instance;
        // VMA >= 3.3 requires the two fetchers explicitly when using dynamic
        // functions; the rest are fetched from these (volk-loaded) pointers.
        impl.vulkanFunctions.vkGetInstanceProcAddr = vkGetInstanceProcAddr;
        impl.vulkanFunctions.vkGetDeviceProcAddr = vkGetDeviceProcAddr;
        allocInfo.pVulkanFunctions = &impl.vulkanFunctions;
        if (vmaCreateAllocator(&allocInfo, &impl.allocator) != VK_SUCCESS)
                return std::unexpected(SLV_ERROR(Errc::vk_out_of_memory, "vmaCreateAllocator failed"));

        VkCommandPoolCreateInfo poolInfo{};
        poolInfo.sType = VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO;
        poolInfo.flags = VK_COMMAND_POOL_CREATE_RESET_COMMAND_BUFFER_BIT;
        poolInfo.queueFamilyIndex = graphicsFamily;
        SLV_VK(vkCreateCommandPool(device, &poolInfo, nullptr, &impl.commandPool), Errc::vk_init_failed);
        return out;
}

auto Device::create(const DeviceOptions &options) -> std::expected<Device, Error>
{
        if (volkInitialize() != VK_SUCCESS)
                return std::unexpected(SLV_ERROR(Errc::vk_init_failed, "failed to load the Vulkan loader (libvulkan)"));

        // Instance: no extensions for the M1 offscreen path; validation layer
        // optional and skipped silently when absent (headless CI images).
        VkApplicationInfo appInfo{};
        appInfo.sType = VK_STRUCTURE_TYPE_APPLICATION_INFO;
        const std::string appName(options.appName.empty() ? "libslang-vk" : options.appName);
        appInfo.pApplicationName = appName.c_str();
        appInfo.applicationVersion = VK_MAKE_VERSION(0, 1, 0);
        appInfo.pEngineName = "libslang-vk";
        appInfo.engineVersion = VK_MAKE_VERSION(0, 1, 0);
        appInfo.apiVersion = options.minApiVersion == 0 ? VK_API_VERSION_1_1 : options.minApiVersion;

        std::vector<const char *> layers;
        const char *validationLayer = "VK_LAYER_KHRONOS_validation";
        if (options.validationLayers) {
                uint32_t count = 0;
                if (vkEnumerateInstanceLayerProperties(&count, nullptr) == VK_SUCCESS && count > 0) {
                        std::vector<VkLayerProperties> available(count);
                        if (vkEnumerateInstanceLayerProperties(&count, available.data()) == VK_SUCCESS) {
                                for (const VkLayerProperties &layer : available)
                                        if (std::strcmp(layer.layerName, validationLayer) == 0) {
                                                layers.push_back(validationLayer);
                                                break;
                                        }
                        }
                }
        }

        std::vector<const char *> extensions;
        // Caller-requested extensions come first (verbatim, M4: surface
        // extensions for swapchain use); a missing name fails instance
        // creation with the driver's own error.
        for (const char *name : options.instanceExtensions)
                extensions.push_back(name);
        if (!layers.empty()) {
                // The ValidationLog collector needs the messenger, and the
                // messenger needs VK_EXT_debug_utils enabled on the instance —
                // without it vkCreateDebugUtilsMessengerEXT fails and validation
                // output would only ever reach the layer's own stderr log.
                uint32_t count = 0;
                if (vkEnumerateInstanceExtensionProperties(nullptr, &count, nullptr) == VK_SUCCESS && count > 0) {
                        std::vector<VkExtensionProperties> available(count);
                        if (vkEnumerateInstanceExtensionProperties(nullptr, &count, available.data()) == VK_SUCCESS) {
                                for (const VkExtensionProperties &ext : available)
                                        if (std::strcmp(ext.extensionName, "VK_EXT_debug_utils") == 0) {
                                                extensions.push_back("VK_EXT_debug_utils");
                                                break;
                                        }
                        }
                }
        }

        VkInstanceCreateInfo instanceInfo{};
        instanceInfo.sType = VK_STRUCTURE_TYPE_INSTANCE_CREATE_INFO;
        instanceInfo.pApplicationInfo = &appInfo;
        instanceInfo.enabledLayerCount = static_cast<uint32_t>(layers.size());
        instanceInfo.ppEnabledLayerNames = layers.empty() ? nullptr : layers.data();
        instanceInfo.enabledExtensionCount = static_cast<uint32_t>(extensions.size());
        instanceInfo.ppEnabledExtensionNames = extensions.empty() ? nullptr : extensions.data();

        VkInstance instance = VK_NULL_HANDLE;
        SLV_VK(vkCreateInstance(&instanceInfo, nullptr, &instance), Errc::vk_init_failed);
        volkLoadInstance(instance);

        auto picked = PickPhysicalDevice(instance, appInfo.apiVersion);
        if (!picked) {
                vkDestroyInstance(instance, nullptr);
                return std::unexpected(picked.error());
        }
        const auto [physicalDevice, family] = *picked;

        const float priority = 1.0F;
        VkDeviceQueueCreateInfo queueInfo{};
        queueInfo.sType = VK_STRUCTURE_TYPE_DEVICE_QUEUE_CREATE_INFO;
        queueInfo.queueFamilyIndex = family;
        queueInfo.queueCount = 1;
        queueInfo.pQueuePriorities = &priority;

        VkDeviceCreateInfo deviceInfo{};
        deviceInfo.sType = VK_STRUCTURE_TYPE_DEVICE_CREATE_INFO;
        deviceInfo.queueCreateInfoCount = 1;
        deviceInfo.pQueueCreateInfos = &queueInfo;
        // Caller-requested device extensions, verbatim (M4: VK_KHR_swapchain
        // for internally created contexts rendering into a swapchain).
        deviceInfo.enabledExtensionCount = static_cast<uint32_t>(options.deviceExtensions.size());
        deviceInfo.ppEnabledExtensionNames = options.deviceExtensions.empty() ? nullptr
                                                                              : options.deviceExtensions.data();

        VkDevice device = VK_NULL_HANDLE;
        SLV_VK(vkCreateDevice(physicalDevice, &deviceInfo, nullptr, &device), Errc::vk_init_failed);
        volkLoadDevice(device);

        VkQueue queue = VK_NULL_HANDLE;
        vkGetDeviceQueue(device, family, 0, &queue);

        Device out;
        out.impl_ = std::make_unique<Impl>();
        Impl &impl = *out.impl_;
        impl.instance = instance;
        impl.physicalDevice = physicalDevice;
        impl.device = device;
        impl.graphicsQueue = queue;
        impl.graphicsFamily = family;
        vkGetPhysicalDeviceProperties(physicalDevice, &impl.properties);
        if (!layers.empty())
                impl.messenger = EnableValidation(instance, impl.validation);

        VmaAllocatorCreateInfo allocInfo{};
        allocInfo.physicalDevice = physicalDevice;
        allocInfo.device = device;
        allocInfo.instance = instance;
        impl.vulkanFunctions.vkGetInstanceProcAddr = vkGetInstanceProcAddr;
        impl.vulkanFunctions.vkGetDeviceProcAddr = vkGetDeviceProcAddr;
        allocInfo.pVulkanFunctions = &impl.vulkanFunctions;
        if (vmaCreateAllocator(&allocInfo, &impl.allocator) != VK_SUCCESS)
                return std::unexpected(SLV_ERROR(Errc::vk_out_of_memory, "vmaCreateAllocator failed"));

        VkCommandPoolCreateInfo poolInfo{};
        poolInfo.sType = VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO;
        poolInfo.flags = VK_COMMAND_POOL_CREATE_RESET_COMMAND_BUFFER_BIT;
        poolInfo.queueFamilyIndex = family;
        SLV_VK(vkCreateCommandPool(device, &poolInfo, nullptr, &impl.commandPool), Errc::vk_init_failed);
        return out;
}

auto Device::instance() const -> VkInstance { return impl_->instance; }
auto Device::physicalDevice() const -> VkPhysicalDevice { return impl_->physicalDevice; }
auto Device::device() const -> VkDevice { return impl_->device; }
auto Device::graphicsQueue() const -> VkQueue { return impl_->graphicsQueue; }
auto Device::graphicsFamily() const -> uint32_t { return impl_->graphicsFamily; }
auto Device::properties() const -> const VkPhysicalDeviceProperties & { return impl_->properties; }

auto Device::waitIdle() const -> std::expected<void, Error>
{
        SLV_VK(vkDeviceWaitIdle(impl_->device), Errc::vk_device_lost);
        return {};
}

auto Device::validationErrors() const -> std::vector<std::string> { return impl_->validation.errors; }

} // namespace slv
