#pragma once

#include <cstdint>
#include <string>
#include <vector>

#include "slangvk/export.h"
#include "slangvk/core/slang_translate.hpp"

namespace slv {
namespace preset {

struct PassSpirv {
	std::vector<uint8_t> vertex;   // SPIR-V bytecode for the vertex stage.
	std::vector<uint8_t> fragment; // SPIR-V bytecode for the fragment stage.
};

/**
 * @brief Compiles one translated .slang pass to SPIR-V (Vulkan 1.0 target,
 * GLSL 450) using the in-process glslang frontend.
 *
 * libslang-vk divergence from the reference host (see PROVENANCE.md): the
 * fork layered precompiled sidecar files and a content-addressed disk cache
 * in front of this call; v1 compiles with glslang on every Preset::load by
 * decision (plan §2, non-goal: on-disk SPIR-V cache). Nothing here touches
 * the filesystem.
 *
 * @param translation Translated stage sources and metadata.
 * @param out Receives the SPIR-V bytecode.
 * @param error Receives a human-readable failure reason.
 */
SLANGVK_API bool CompilePass(const SlangTranslation &translation, PassSpirv &out, std::string &error);

} // namespace preset
} // namespace slv
