cmake_minimum_required(VERSION 3.28)

project(libslang-vk
    VERSION 0.1.0
    DESCRIPTION "libretro slang shader preset parsing/compiling with a Vulkan execution path"
    LANGUAGES CXX C)

set(CMAKE_CXX_STANDARD 23)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)
# Classic translation units only — no C++ module dependency scanning (the
# ported sources are plain headers/TUs; scanning costs configure time).
set(CMAKE_CXX_SCAN_FOR_MODULES OFF)
# glslang is linked statically into both library variants; the shared variant
# needs it position-independent on Linux.
set(CMAKE_POSITION_INDEPENDENT_CODE ON)

option(SLANGVK_BUILD_TESTS "Build the unit test suite" ON)
option(SLANGVK_BUILD_EXAMPLES "Build the example programs" ON)
option(SLANGVK_BUILD_SHARED "Also build the shared library variant" ON)
option(SLANGVK_ENABLE_VK "Build the Vulkan runner (src/vk; needs Vulkan-Headers + volk + VMA, fetched automatically)" ON)
set(SLANGVK_SANITIZE "" CACHE STRING "Comma-separated sanitizers for a test build (e.g. address,undefined)")

if(SLANGVK_SANITIZE)
        string(REPLACE "," ";" SLANGVK_SANITIZE_LIST "${SLANGVK_SANITIZE}")
        add_compile_options(-fno-omit-frame-pointer)
        add_link_options(-fno-omit-frame-pointer)
        foreach(sanitizer IN LISTS SLANGVK_SANITIZE_LIST)
                add_compile_options(-fsanitize=${sanitizer})
                add_link_options(-fsanitize=${sanitizer})
        endforeach()
        string(REPLACE ";" " " SLANGVK_SANITIZE_FLAGS "${SLANGVK_SANITIZE_LIST}")
        message(STATUS "libslang-vk: sanitizers enabled (${SLANGVK_SANITIZE_FLAGS})")
endif()

list(APPEND CMAKE_MODULE_PATH "${CMAKE_CURRENT_SOURCE_DIR}/cmake")
include(deps)

set(SLANGVK_CORE_SOURCES
    src/core/preset_parser.cpp
    src/core/slang_translate.cpp
    src/core/slang_compile.cpp
)

if(SLANGVK_ENABLE_VK)
        list(APPEND SLANGVK_CORE_SOURCES
            src/vk/device.cpp
            src/vk/images.cpp
            src/vk/lut.cpp
            src/vk/preset.cpp
            src/vk/chain.cpp
            src/vk/vma_impl.cpp)
endif()

# Each variant compiles the core sources once with its own export defines:
# the shared build exports annotated symbols, the static build stays clean.
function(slangvk_add_library target shared)
        if(shared)
                add_library(${target} SHARED ${SLANGVK_CORE_SOURCES})
                target_compile_definitions(${target} PRIVATE SLANGVK_BUILDING_SHARED)
                target_compile_definitions(${target} PUBLIC SLANGVK_SHARED)
        else()
                add_library(${target} STATIC ${SLANGVK_CORE_SOURCES})
        endif()
        target_compile_features(${target} PUBLIC cxx_std_23)
        target_include_directories(${target} PUBLIC
            $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
            $<INSTALL_INTERFACE:include>)
        # Vendored single-header image decoder for LUT uploads (public domain;
        # see PROVENANCE.md). Implementation TU: src/vk/lut.cpp.
        target_include_directories(${target} PRIVATE
            ${CMAKE_CURRENT_SOURCE_DIR}/vendor/stb)
        # Raw Vulkan handle types in runner.hpp need the Khronos headers at the
        # boundary — in BOTH configurations (the public header is consumed by
        # core-only hosts too); volk + VMA stay private implementation details
        # of the ENABLE_VK build.
        target_link_libraries(${target} PUBLIC slv-headers-only)
        if(SLANGVK_ENABLE_VK)
                target_compile_definitions(${target} PUBLIC SLANGVK_ENABLE_VK=1)
                target_link_libraries(${target} PRIVATE slv-volk slv-vma-headers)
        endif()
        # glslang is an implementation detail: PRIVATE, but as a static library
        # CMake still propagates the link dependency to consumers.
        target_link_libraries(${target} PRIVATE
            glslang::glslang
            glslang::SPIRV
            glslang::glslang-default-resource-limits)
        # glslang compiles with -fno-rtti; linking Debug codegen that references
        # its typeinfo symbols fails otherwise. The core uses no RTTI itself.
        if(NOT MSVC)
                target_compile_options(${target} PRIVATE -fno-rtti)
        endif()
endfunction()

slangvk_add_library(slangvk FALSE)
add_library(slangvk::slangvk ALIAS slangvk)

if(SLANGVK_BUILD_SHARED)
        slangvk_add_library(slangvk-shared TRUE)
        set_target_properties(slangvk-shared PROPERTIES OUTPUT_NAME slangvk)
        add_library(slangvk::shared ALIAS slangvk-shared)
endif()

if(SLANGVK_BUILD_TESTS)
        enable_testing()
        add_subdirectory(tests)
endif()

if(SLANGVK_BUILD_EXAMPLES)
        add_subdirectory(examples)
endif()

install(TARGETS slangvk
    ARCHIVE DESTINATION lib
    LIBRARY DESTINATION lib
    RUNTIME DESTINATION bin)
if(SLANGVK_BUILD_SHARED)
        install(TARGETS slangvk-shared
            ARCHIVE DESTINATION lib
            LIBRARY DESTINATION lib
            RUNTIME DESTINATION bin)
endif()
install(DIRECTORY include/slangvk DESTINATION include)
# M5 (plan §11) replaces this with a full CMake package: the export set needs
# glslang targets in the same export set or find_dependency wiring.
install(CODE "message(STATUS 'libslang-vk: headers + libraries installed (package config lands in M5)')")
