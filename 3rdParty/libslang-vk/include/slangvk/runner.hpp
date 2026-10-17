#pragma once

// libslang-vk — public Vulkan runner API (M1, plan §6/§8/§11).
//
// Raw Vulkan handle types at the boundary (no vulkan-hpp); errors as
// std::expected<T, slv::Error>. The runner loads the Vulkan library at
// runtime through volk internally (no link-time dependency on libvulkan),
// so consumers need no Vulkan SDK at link time — only the headers that
// ship alongside this one for the handle types.
//
// M2 scope: Device adopt/create, Preset load (parse + translate + compile),
// Chain create/render for multi-pass offscreen execution with the full chain
// semantics — history rings (OriginalHistory#), feedback double-buffering
// (PassFeedback# / <name>Feedback), mipmap refill, per-pass FBO formats,
// FBO recreation on resize and live parameters — plus host image helpers
// for headless use (tests, golden images, CI).
// M3 adds LUT texture upload: image files decode to RGBA8 and upload as
// R8G8B8A8_UNORM (full mip chain when `name_mipmap` is set); bindings
// resolve by declared name (case-insensitive) and positionally via `User#`,
// each LUT sampled with its own linear/wrap/mipmap options (SPEC 3.5/4.5/5.4).
// M4 adds presentation: SwapchainFrame output (final-pass letterbox draw into
// an acquired swapchain image, host owns acquire/present), InputFrame rotation
// (Rotation uniform + rotated presentation), the fit-and-center present path
// for final passes that do not resolve to the output target size, and
// Image::update for per-frame host uploads.

#include <cstdint>
#include <expected>
#include <filesystem>
#include <memory>
#include <span>
#include <string>
#include <string_view>
#include <utility>
#include <variant>
#include <vector>

#include <vulkan/vulkan.h>

#include "slangvk/export.h"

namespace slv {

/** @brief Error categories reported through Error::code. */
enum class Errc {
        preset_parse,         ///< .slangp/.slang syntax or reference problem
        include_cycle,        ///< #include / #reference cycle detected
        shader_compile,       ///< glslang front-end rejected a stage
        unsupported_feature,  ///< valid slang/preset construct not yet implemented (see message)
        invalid_argument,     ///< caller passed mismatched sizes/formats/layouts
        vk_init_failed,       ///< loader/instance/device creation failed
        vk_device_lost,       ///< VK_ERROR_DEVICE_LOST during execution
        vk_out_of_memory,     ///< host or device allocation failed
        vk_error,             ///< any other Vulkan VkResult failure
};

/** @brief A failure with category and human-readable context. */
struct Error {
        Errc code;
        std::string message;
        std::string file; // Source location of the throw site (library-side).
        int line = -1;
};

/** @brief Rotation of the input frame relative to the output (RetroArch uint 0..3). */
enum class Rotation : uint32_t { None = 0, Cw90 = 1, Cw180 = 2, Cw270 = 3 };

struct DeviceOptions {
        std::string_view appName;
        uint32_t minApiVersion = VK_API_VERSION_1_1; ///< API version the device must support.
        bool validationLayers = false;               ///< Enable VK_LAYER_KHRONOS_validation when present.
        /**
         * @brief Extra instance extensions requested verbatim (M4: e.g.
         * VK_KHR_surface + a platform-surface extension for swapchain use).
         * A missing name fails instance creation with the driver's error.
         */
        std::vector<const char *> instanceExtensions;
        /** @brief Extra device extensions requested verbatim (e.g. VK_KHR_swapchain). */
        std::vector<const char *> deviceExtensions;
};

/**
 * @brief A Vulkan logical device context: either adopted from the host
 * (instance/device/queue stay host-owned) or created internally headless.
 *
 * Owns the VMA allocator and the command machinery the runner needs. All
 * other objects (Preset, Chain, Image) reference a Device and must be
 * destroyed before it.
 */
class Device {
public:
        struct Impl; // Internal; opaque on purpose (pimpl access below is library-internal).

        /**
         * @brief Adopts a host-created Vulkan context (DevilutionX mode).
         *
         * The host keeps owning the instance and device; libslang-vk records
         * the graphics queue/family and creates its own allocator + command
         * machinery on top. The host must keep the instance/device alive for
         * the lifetime of every object created through this Device.
         */
        SLANGVK_API static auto adopt(VkInstance instance, VkPhysicalDevice physicalDevice, VkDevice device,
            VkQueue graphicsQueue, uint32_t graphicsFamily) -> std::expected<Device, Error>;

        /** @brief Creates instance + device internally (headless/CI mode). */
        SLANGVK_API static auto create(const DeviceOptions &options) -> std::expected<Device, Error>;

        SLANGVK_API Device(Device &&) noexcept;
        SLANGVK_API auto operator=(Device &&) noexcept -> Device &;
        SLANGVK_API ~Device();

        SLANGVK_API auto instance() const -> VkInstance;
        SLANGVK_API auto physicalDevice() const -> VkPhysicalDevice;
        SLANGVK_API auto device() const -> VkDevice;
        SLANGVK_API auto graphicsQueue() const -> VkQueue;
        SLANGVK_API auto graphicsFamily() const -> uint32_t;

        /** @brief Properties of the adopted/selected physical device. */
        SLANGVK_API auto properties() const -> const VkPhysicalDeviceProperties &;

        /** @brief Blocks until all work submitted through this Device finishes. */
        SLANGVK_API auto waitIdle() const -> std::expected<void, Error>;


        /**
         * @brief Reads an image back to host memory (row-major, tightly packed).
         *
         * Transition-safe: the image is moved to TRANSFER_SRC and restored to
         * `currentLayout` afterwards. Used by headless tests and the golden
         * image suite; hosts composite via images/views instead.
         */
        SLANGVK_API auto readPixels(VkImage image, VkFormat format, uint32_t width, uint32_t height,
            VkImageLayout currentLayout) -> std::expected<std::vector<uint8_t>, Error>;

        /** @brief Validation-layer ERROR messages collected so far (empty when validation is off). */
        SLANGVK_API auto validationErrors() const -> std::vector<std::string>;

        /// @cond internal
        /// Pimpl access for library translation units (not public API).
        auto impl() const -> Impl & { return *impl_; }
        /// @endcond

private:
        Device() = default;
        std::unique_ptr<Impl> impl_;
};

struct ImageDesc {
        uint32_t width = 0;
        uint32_t height = 0;
        VkFormat format = VK_FORMAT_R8G8B8A8_UNORM;
};

/**
 * @brief An owning 2D image + view allocated through the Device's VMA
 * allocator. Convenience for hosts/tests without their own allocator:
 * input frames, offscreen targets, LUTs and golden-image readbacks.
 */
class Image {
public:
        struct Impl; // Internal (pimpl access below is library-internal).

        struct CreateInfo {
                ImageDesc desc;
                VkImageUsageFlags usage = VK_IMAGE_USAGE_SAMPLED_BIT | VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT
                    | VK_IMAGE_USAGE_TRANSFER_SRC_BIT | VK_IMAGE_USAGE_TRANSFER_DST_BIT;
                const void *pixels = nullptr; ///< Row-major tightly packed texel data (may be null).
                size_t pixelBytes = 0;        ///< Must equal width*height*bytesPerTexel when pixels != null.
                VkImageLayout finalLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
        };

        SLANGVK_API static auto create(Device &, const CreateInfo &) -> std::expected<Image, Error>;

        /**
         * @brief Replaces the image content with tightly packed texel data
         * (M4 host-upload path: the converted game frame every present).
         *
         * Synchronous staging copy; the image must be a 1-mip Image created
         * by this class and rests in its creation layout before and after.
         * pixelBytes must equal width*height*bytesPerTexel.
         */
        SLANGVK_API auto update(const void *pixels, size_t pixelBytes) -> std::expected<void, Error>;

        SLANGVK_API Image(Image &&) noexcept;
        SLANGVK_API auto operator=(Image &&) noexcept -> Image &;
        SLANGVK_API ~Image();

        SLANGVK_API auto image() const -> VkImage;
        SLANGVK_API auto view() const -> VkImageView;
        SLANGVK_API auto desc() const -> const ImageDesc &;
        SLANGVK_API auto layout() const -> VkImageLayout;

        /// @cond internal
        auto impl() const -> Impl & { return *impl_; }
        /// @endcond

private:
        Image() = default;
        std::unique_ptr<Impl> impl_;
};

struct PresetLoadOptions {
        bool strict = false;      ///< Promote translator warnings to load errors.
        bool noLutImages = false; ///< Skip LUT texture upload (headless/compile-only sweeps).
};

/** @brief Metadata for one shader parameter (merged preset + pragma view). */
struct ParameterInfo {
        std::string name;
        std::string description;
        float defaultValue = 0.0F;
        float minimum = 0.0F;
        float maximum = 1.0F;
        float step = 0.001F;
};

/**
 * @brief A parsed + translated + SPIR-V-compiled preset. Immutable after
 * load; shareable across Chains. Compilation is synchronous here (plan §5:
 * glslang-only, no disk cache).
 */
class Preset {
public:
        struct Impl; // Internal (pimpl access below is library-internal).

        SLANGVK_API static auto load(Device &, const std::filesystem::path &slangp,
            const PresetLoadOptions &options = {}) -> std::expected<Preset, Error>;

        SLANGVK_API Preset(Preset &&) noexcept;
        SLANGVK_API auto operator=(Preset &&) noexcept -> Preset &;
        SLANGVK_API ~Preset();

        /** @brief Number of shader passes in the chain. */
        SLANGVK_API auto passCount() const -> uint32_t;

        /** @brief Parameter metadata: pragma defaults merged with preset overrides. */
        SLANGVK_API auto parameterInfo() const -> std::span<const ParameterInfo>;

        /// @cond internal
        auto impl() const -> Impl & { return *impl_; }
        /// @endcond

private:
        Preset() = default;
        std::unique_ptr<Impl> impl_;
};

struct ChainOptions {
        uint32_t outputWidth = 0;  ///< Final output target size (offscreen/swapchain rect).
        uint32_t outputHeight = 0;
        VkFormat outputFormat = VK_FORMAT_R8G8B8A8_UNORM;
};

/** @brief Per-frame inputs (values written into the params UBO). */
struct FrameInfo {
        uint64_t frameCount = 0;
        float frameDeltaUs = 0.0F; ///< Microseconds since the previous frame (FrameTime).
        int32_t frameDirection = 1;
};

/** @brief The chain's input texture (host-owned image; layout must be SHADER_READ_ONLY_OPTIMAL). */
struct InputFrame {
        VkImageView view = VK_NULL_HANDLE;
        /**
         * @brief The underlying image (required for history and mipmap-input
         * presets: the runner blits it into the history ring / mip-capable
         * copy every frame; the image content is never modified and its
         * layout returns to SHADER_READ_ONLY_OPTIMAL). May be null otherwise.
         */
        VkImage image = VK_NULL_HANDLE;
        VkFormat format = VK_FORMAT_R8G8B8A8_UNORM;
        uint32_t width = 0;
        uint32_t height = 0;
        Rotation rotation = Rotation::None;
};

/**
 * @brief Render into a host-provided image view the host composites itself
 * (headless/CI mode).
 *
 * When the final pass resolves to exactly the target size and rotation is
 * None it draws straight into this view (byte-exact golden path). Otherwise
 * the chain output is drawn centered with a fit-scale and black letterbox
 * (the swapchain presentation math, same code path).
 */
struct Offscreen {
        VkImageView view = VK_NULL_HANDLE;
        /**
         * @brief The underlying image. Required only when the final pass is
         * feedback-read (the runner blits its double-buffered output into
         * the host image; layout returns to SHADER_READ_ONLY_OPTIMAL). May
         * be null otherwise.
         */
        VkImage image = VK_NULL_HANDLE;
        /// Mismatched target params are a hard error (invalid_argument).
        VkFormat format = VK_FORMAT_R8G8B8A8_UNORM;
        uint32_t width = 0;
        uint32_t height = 0;
};

/**
 * @brief Pixel rectangle. x/y may be negative or place the rect partially
 * outside the target only through an explicit host rect (validated).
 */
struct Rect {
        int32_t x = 0;
        int32_t y = 0;
        uint32_t width = 0;
        uint32_t height = 0;
};

/**
 * @brief Render into an already-acquired swapchain image view (DevilutionX
 * presentation mode, plan §8 output (a)). M4.
 *
 * Per-frame contract: the host acquires the image (vkAcquireNextImageKHR,
 * optionally signaling `acquireSemaphore`), calls render() with the image's
 * view, then presents after render() returns. render() waits the acquire
 * semaphore at submit and leaves the image in VK_IMAGE_LAYOUT_PRESENT_SRC_KHR
 * ready for vkQueuePresentKHR; because render() is synchronous (fenced), the
 * present needs no wait semaphore. Letterbox: the final-pass output is drawn
 * into `outputRect` (all-zero rect = fit and center, reference-host math).
 * The chain's pass FBOs keep rendering at their preset-derived sizes; only
 * the presentation draw touches the swapchain image.
 */
struct SwapchainFrame {
        VkImageView swapchainImageView = VK_NULL_HANDLE;
        /// The swapchain's current format (from the surface/vkGetPhysicalDeviceSurfaceFormatsKHR).
        VkFormat format = VK_FORMAT_UNDEFINED;
        uint32_t width = 0;
        uint32_t height = 0;
        /// Content rect inside the swapchain image; zero rect = fit + center.
        Rect outputRect{};
        /// Semaphore signaled by the host's acquire (optional; waited before drawing).
        VkSemaphore acquireSemaphore = VK_NULL_HANDLE;
};

using OutputTarget = std::variant<SwapchainFrame, Offscreen>;

/**
 * @brief Per-device runtime state for one preset: pipelines, FBOs, samplers,
 * descriptor sets and the per-pass uniform buffers. Single-threaded per
 * Chain (documented API behavior, plan §5).
 *
 * The Preset must outlive every Chain created from it: LUT texture views
 * are borrowed from the preset's uploads (M3).
 */
class Chain {
public:
        struct Impl; // Internal (pimpl access below is library-internal).

        SLANGVK_API static auto create(Device &, const Preset &, const ChainOptions &)
            -> std::expected<Chain, Error>;

        SLANGVK_API Chain(Chain &&) noexcept;
        SLANGVK_API auto operator=(Chain &&) noexcept -> Chain &;
        SLANGVK_API ~Chain();

        /** @brief Recreates pass FBOs for new input/output sizes (next render picks it up). */
        SLANGVK_API auto resize(uint32_t inputWidth, uint32_t inputHeight, uint32_t outputWidth, uint32_t outputHeight)
            -> std::expected<void, Error>;

        /** @brief Overrides a parameter value (merged into the params UBO on the next render). */
        SLANGVK_API auto setParameter(std::string_view id, float value) -> std::expected<void, Error>;

        /**
         * @brief Renders one frame: uploads uniforms, binds pipelines/descriptors,
         * draws every pass, writes the final pass into the output target.
         *
         * Synchronous: records + submits one command buffer and waits.
         * The input image view must be in SHADER_READ_ONLY_OPTIMAL and stays
         * untouched; an Offscreen target image is left in
         * SHADER_READ_ONLY_OPTIMAL, a SwapchainFrame image in PRESENT_SRC_KHR.
         *
         * Rotation (M4): the chain renders the input in its source orientation
         * (shaders read the `Rotation` uniform 0..3); the presentation draw
         * rotates the final output clockwise into the output rect and sizes
         * the fit rect from the rotated aspect.
         */
        SLANGVK_API auto render(const InputFrame &, const OutputTarget &, const FrameInfo &)
            -> std::expected<void, Error>;

        /** @brief Sizes of a pass's output FBO as currently allocated. */
        SLANGVK_API auto passOutputSize(uint32_t passIndex) const
            -> std::expected<std::pair<uint32_t, uint32_t>, Error>;

        /// @cond internal
        auto impl() const -> Impl & { return *impl_; }
        /// @endcond

private:
        Chain() = default;
        std::unique_ptr<Impl> impl_;
};

} // namespace slv
