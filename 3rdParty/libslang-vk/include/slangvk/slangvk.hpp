#pragma once

// libslang-vk — parse, translate and compile libretro slang shader presets,
// with a Vulkan execution path.
//
// The GPU-agnostic core layer (preset data model, .slangp parsing, .slang
// translation, glslang SPIR-V compilation) lives in namespace slv::preset.
// The Vulkan runner (namespace slv) is built by default; disable with
// SLANGVK_ENABLE_VK=OFF for a core-only build.

#include "slangvk/export.h"
#include "slangvk/core/types.hpp"
#include "slangvk/core/preset_parser.hpp"
#include "slangvk/core/slang_translate.hpp"
#include "slangvk/core/slang_compile.hpp"

#if defined(SLANGVK_ENABLE_VK) || !defined(SLANGVK_NO_DEFAULTS)
// The runner is part of every default build (SLANGVK_ENABLE_VK defaults ON;
// core-only consumers define SLANGVK_NO_DEFAULTS or turn the option off and
// include only the core headers they need).
#include "slangvk/runner.hpp"
#endif
