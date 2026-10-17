#pragma once

// Optional "slangvk" presentation backend for the GPU scaler.
//
// The runtime libretro slang preset chain executes on a raw Vulkan swapchain
// through the vendored libslang-vk (3rdParty/libslang-vk, MIT) instead of the
// SDL_GPU preset runner. Opt-in via DEVILUTIONX_GPU_BACKEND=slangvk; every
// init failure falls back to the regular SDL_GPU path, which stays untouched
// and remains the reference implementation (Metal, D3D12 hardening, the
// Vulkan retry paths).

#ifdef USE_GPU_SCALER
#ifndef USE_SDL1

#include <cstdint>
#include <memory>
#include <string>

struct SDL_Surface;
struct SDL_Window;

namespace devilution {

namespace gpu_scaler {

/**
 * @brief Returns true when DEVILUTIONX_GPU_BACKEND selects the "slangvk"
 * backend. Callers must create the game window with SDL_WINDOW_VULKAN when
 * this returns true: the backend creates its own Vulkan surface on the
 * window (SDL_Vulkan_CreateSurface requires the flag).
 */
bool WantsVulkanWindow();

/**
 * @brief Owns the Vulkan context, swapchain and slang preset chain of the
 * "slangvk" backend.
 *
 * Presentation is self-contained: the game frame is uploaded every present,
 * the chain renders it, and the final pass is drawn into the acquired
 * swapchain image (fit and center, honoring Integer Scaling) before the
 * host presents it. The device itself is created by libslang-vk, so no
 * Vulkan SDK is needed at build or link time — volk loads the loader at
 * runtime, and all swapchain/WSI entry points resolve through it as well.
 *
 * Any init failure is reported through the error string and the GPU scaler
 * falls back to the SDL_GPU path. Per-frame failures skip the frame; a
 * sustained failure trips the scaler's session fallback contract
 * (LogFailHard -> HasFailed -> regular renderer).
 */
class SlvRunner {
public:
	SlvRunner();
	~SlvRunner();
	SlvRunner(const SlvRunner &) = delete;
	SlvRunner &operator=(const SlvRunner &) = delete;

	/**
	 * @brief Creates the Vulkan device (through libslang-vk), the window
	 * surface and the swapchain. The window must have been created with
	 * SDL_WINDOW_VULKAN.
	 * @return false and fills `error` on failure (no Vulkan loader or ICD,
	 * surface creation refused, present unsupported, ...).
	 */
	bool Init(SDL_Window *window, bool vsync, std::string &error);

	/** @brief Releases everything. Safe to call anytime. */
	void Shutdown();

	/** @brief True after a successful Init (device + surface + swapchain). */
	[[nodiscard]] bool IsReady() const;

	/**
	 * @brief Loads the preset at `path` through libslang-vk (parse,
	 * translate, compile, LUT upload) and builds the chain for the current
	 * swapchain. Replaces any previously loaded chain.
	 */
	bool LoadPreset(const std::string &path, std::string &error);

	/** @brief Releases the preset chain (device and swapchain stay). */
	void UnloadPreset();

	/** @brief The preset file the running chain was built from (resolved path). */
	[[nodiscard]] const std::string &LoadedPresetPath() const;

	/**
	 * @brief Uploads the frame, renders the chain and presents the result.
	 * @param frameDeltaUs Duration of the previous frame in microseconds
	 * (FrameTime uniform).
	 * @param integerScaling Honors the Integer Scaling option in the
	 * fit-and-center rect.
	 * @return false when the frame was skipped (swapchain transient or a
	 * render error; the scaler's failure counters stay in charge).
	 */
	bool Present(const SDL_Surface &frame, uint64_t frameDeltaUs, bool integerScaling);

private:
	struct Impl;
	std::unique_ptr<Impl> impl_;
};

} // namespace gpu_scaler
} // namespace devilution

#endif // !USE_SDL1
#endif // USE_GPU_SCALER
