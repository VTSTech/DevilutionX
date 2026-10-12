#pragma once

#include <string>
#include <vector>

#include "engine/render/gpu/preset/preset_types.hpp"

namespace devilution {
namespace gpu_scaler {
namespace preset {

/**
 * @brief Parses a .slangp preset file (libretro slang preset format).
 *
 * Handles: shaders/shaderN chains, filter_linearN, wrap_modeN, scale rules,
 * srgb/float framebuffers, aliases, frame_count_mod, mipmap_input, lookup
 * textures (with linear/wrap/mipmap variants), parameter overrides,
 * feedback_pass and histories. Values may be quoted; '#' comments (full-line
 * and trailing) are stripped.
 *
 * @return true on success; on failure `out` is left untouched and `error`
 * contains a human-readable reason.
 */
bool ParsePreset(const std::string &path, Preset &out, std::string &error, int referenceDepth);

bool ParsePreset(const std::string &path, Preset &out, std::string &error);

/**
 * @brief Scans `rootDir` recursively (bounded depth) for .slangp presets.
 *
 * Returns entries sorted by relative path (deterministic menu ordering).
 * The scan is not cached; call sparingly (startup and when the settings
 * menu opens).
 */
std::vector<PresetEntry> ScanPresets(const std::string &rootDir);

/**
 * @brief The runtime shader folder: "shaders" next to the executable.
 * End users drop .slangp presets (and any subfolder layout) here.
 */
std::string ShaderRootDir();

/**
 * @brief Directory for compiled SPIR-V cache blobs: ShaderRootDir() + "/cache".
 */
std::string ShaderCacheDir();

/**
 * @brief Resolves a preset relative path against the shader root and returns
 * the absolute path, or an empty string when the file does not exist.
 */
std::string ResolvePresetPath(const std::string &relativePath);

} // namespace preset
} // namespace gpu_scaler
} // namespace devilution
