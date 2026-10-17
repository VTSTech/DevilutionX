# Third-party dependencies — all permissively licensed (plan §4).
#
# M0 wires glslang only: it is the one dependency the core layer consumes.
# Vulkan-Headers and VulkanMemoryAllocator are declared here and MakeAvailable
# flips on in M1 when the Vulkan runner starts consuming them — declaring them
# now pins the provenance/licensing intent without paying the fetch cost in a
# build that cannot use them yet.

include(FetchContent)

# glslang — Khronos reference GLSL frontend; compiles translated .slang to
# SPIR-V in-process (the same frontend RetroArch uses for its slang pipeline).
# Same pinned version as the reference host for behavioral parity.
# Link targets: glslang::glslang, glslang::SPIRV,
# glslang::glslang-default-resource-limits (GetDefaultResources()).
#
# Vendored-copy adaptation (DevilutionX): the host build usually provides
# glslang itself (3rdParty/glslang, pinned to the identical 16.6.0 release
# with the same flags, or a system package). Reuse those targets instead of
# fetching a second copy — add_library() would otherwise abort configure with
# duplicate target names. The fetch below only runs for standalone consumers
# of this directory.
if(TARGET glslang::glslang AND TARGET glslang::SPIRV
        AND TARGET glslang::glslang-default-resource-limits)
        message(STATUS "libslang-vk: reusing the host build's glslang targets")
else()
set(BUILD_EXTERNAL OFF)
set(BUILD_WERROR OFF)
set(GLSLANG_TESTS OFF)
set(GLSLANG_ENABLE_INSTALL OFF)
set(ENABLE_SPIRV ON)
set(ENABLE_GLSLANG_BINARIES OFF)
set(ENABLE_GLSLANG_JS OFF)
set(ENABLE_HLSL ON)
set(ENABLE_OPT OFF) # skips the SPIRV-Tools submodule dependency
set(ENABLE_SPVREMAPPER OFF)
set(ENABLE_PCH ON)
set(BUILD_SHARED_LIBS OFF)

FetchContent_Declare(glslang
  URL https://github.com/KhronosGroup/glslang/archive/refs/tags/16.6.0.tar.gz
  URL_HASH SHA256=9c09b901149c729df745057dafa815278aaa101b84d2b6e14f16a42de52f97f2
)
FetchContent_MakeAvailable(glslang)
endif()

# Vulkan-Headers — raw Vulkan API definitions (no loader). The runner talks
# to Vulkan through volk (below), which loads the system loader at runtime;
# hosts that adopt their own VkDevice keep full control of instance creation.
FetchContent_Declare(vulkan-headers
  URL https://github.com/KhronosGroup/Vulkan-Headers/archive/refs/tags/v1.4.364.tar.gz
  URL_HASH SHA256=37e00e30611375938a9437477de793e355e55d486c25ab3dfd7a1a11ab0f8f07
)
FetchContent_MakeAvailable(vulkan-headers)
add_library(slv-headers-only INTERFACE)
target_include_directories(slv-headers-only INTERFACE "${vulkan-headers_SOURCE_DIR}/include")

# volk — meta-loader for the Vulkan API (loads vkGetInstanceProcAddr at
# runtime; no link-time dependency on libvulkan). Compiled once as a static
# object inside the library so consumers need no Vulkan SDK at link time.
FetchContent_Declare(volk
  URL https://github.com/zeux/volk/archive/refs/tags/vulkan-sdk-1.4.363.0.tar.gz
  URL_HASH SHA256=1547d8d74395d4048fb3f4a6313da56db626e89b55db238d0d7e8944c8a645f3
)
FetchContent_MakeAvailable(volk)
add_library(slv-volk STATIC "${volk_SOURCE_DIR}/volk.c")
target_include_directories(slv-volk PUBLIC "${volk_SOURCE_DIR}")
target_link_libraries(slv-volk PUBLIC slv-headers-only)
target_compile_definitions(slv-volk PUBLIC VK_NO_PROTOTYPES)

# Vulkan Memory Allocator — device memory suballocation for the runner's
# images and buffers (FBOs, staging, UBOs, readback).
FetchContent_Declare(vma
  URL https://github.com/GPUOpen-LibrariesAndSDKs/VulkanMemoryAllocator/archive/refs/tags/v3.3.0.tar.gz
  URL_HASH SHA256=c4f6bbe6b5a45c2eb610ca9d231158e313086d5b1a40c9922cb42b597419b14e
)
FetchContent_MakeAvailable(vma)
add_library(slv-vma-headers INTERFACE)
target_include_directories(slv-vma-headers INTERFACE "${vma_SOURCE_DIR}/include")
target_link_libraries(slv-vma-headers INTERFACE slv-volk)
