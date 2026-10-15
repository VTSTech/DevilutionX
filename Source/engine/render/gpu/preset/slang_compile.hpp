#pragma once

#include <cstdint>
#include <string>
#include <vector>

#include "engine/render/gpu/preset/slang_translate.hpp"

namespace devilution {
namespace gpu_scaler {
namespace preset {

struct PassSpirv {
	std::vector<uint8_t> vertex;   // SPIR-V bytecode for the vertex stage.
	std::vector<uint8_t> fragment; // SPIR-V bytecode for the fragment stage.
};

/** @brief Where a compiled pass's SPIR-V came from. */
enum class SpirvSource {
	Compiled, // fresh glslang compile
	Cache,    // disk cache hit (content-addressed blob under ShaderCacheDir())
	Sidecar,  // precompiled <name>.vert.spv/.frag.spv next to the .slang source
};

/**
 * @brief Compiles one translated .slang pass to SPIR-V for the Vulkan flavor
 * of the SDL3 GPU conventions, using the in-process glslang frontend.
 *
 * Results are cached on disk (see CachePathFor) keyed by a hash of the
 * translated sources plus a toolchain version salt; precompiled
 * `<name>.vert.spv`/`<name>.frag.spv` files sitting next to the .slang source
 * always win over everything else.
 *
 * @param translation Translated stage sources and metadata.
 * @param slangPath Absolute path of the source .slang (for .spv fast-path lookup).
 * @param cacheDir Directory for cache blobs (may be empty to disable caching).
 * @param out Receives the SPIR-V bytecode.
 * @param error Receives a human-readable failure reason.
 * @param outSource When non-null, receives where the SPIR-V came from (the
 *                  caller uses this to self-heal poisoned cache blobs, ROB-03).
 * @param allowCache When false, the disk cache is neither read nor written
 *                  (used for the ROB-03 recompile-after-cache-rejection path).
 */
bool CompilePass(const SlangTranslation &translation, const std::string &slangPath, const std::string &cacheDir,
    PassSpirv &out, std::string &error, SpirvSource *outSource = nullptr, bool allowCache = true);

/** @brief The cache blob paths for a translated .slang pass (content-addressed, version-salted). */
void CachePathFor(const SlangTranslation &translation, const std::string &slangPath, const std::string &cacheDir,
    std::string &vertexPath, std::string &fragmentPath);

/** @brief Precompiled SPIR-V file paths next to a .slang source. */
void SidecarSpvPathsFor(const std::string &slangPath, std::string &vertexPath, std::string &fragmentPath);

/**
 * @brief Deletes the disk-cache blobs for a translated pass (best-effort).
 *
 * ROB-03 self-heal: when the GPU driver rejects a cache-hit SPIR-V module,
 * the caller removes the poisoned blobs and recompiles with allowCache=false
 * instead of failing every future session until the user wipes the cache.
 */
void DeleteCachedSpv(const SlangTranslation &translation, const std::string &slangPath, const std::string &cacheDir);

} // namespace preset
} // namespace gpu_scaler
} // namespace devilution
