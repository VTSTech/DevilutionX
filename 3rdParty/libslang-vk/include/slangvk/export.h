#pragma once

// libslang-vk export/visibility macros.
//
// Static builds need no annotation; shared builds export annotated symbols.
// Note (plan §9): the public API uses C++23 vocabulary types, so ABI is
// same-toolchain only — shared libraries are distributed per toolchain, as
// CI-built artifacts, not as a stable cross-compiler ABI.

#if defined(_WIN32) || defined(__CYGWIN__)
	#if defined(SLANGVK_BUILDING_SHARED)
		#define SLANGVK_API __declspec(dllexport)
	#elif defined(SLANGVK_SHARED)
		#define SLANGVK_API __declspec(dllimport)
	#else
		#define SLANGVK_API
	#endif
#elif defined(__GNUC__) || defined(__clang__)
	#define SLANGVK_API __attribute__((visibility("default")))
#else
	#define SLANGVK_API
#endif

#define SLANGVK_VERSION_MAJOR 0
#define SLANGVK_VERSION_MINOR 1
#define SLANGVK_VERSION_PATCH 0
