#pragma once

// libslang-vk internal — Vulkan runner shared internals (M1).
//
// All Vulkan calls go through volk (loaded at runtime, VK_NO_PROTOTYPES).
// Every VkResult-producing call is checked; failures become slv::Error via
// SLV_VK / SLV_VK_EXPECT and carry the call site.

#include <cctype>
#include <cstdint>
#include <expected>
#include <string>
#include <vector>

#include <volk.h>
#include <vk_mem_alloc.h>

#include "slangvk/runner.hpp"
#include "slangvk/core/preset_parser.hpp"
#include "slangvk/core/slang_translate.hpp"
#include "slangvk/core/slang_compile.hpp"

namespace slv::vk {

// Core-layer vocabulary (sibling namespace slv::preset), used unqualified
// throughout the runner's translation units.
using slv::preset::FilterMode;
using slv::preset::ParamInfo;
using slv::preset::ParamOverride;
using slv::preset::PassDef;
using slv::preset::ScaleRule;
using slv::preset::ScaleType;
using slv::preset::SlangTranslation;
using slv::preset::StageTranslation;
using slv::preset::TextureDef;
using slv::preset::TextureRef;
using slv::preset::UniformBlock;
using slv::preset::UniformMember;
using slv::preset::UniformType;
using slv::preset::WrapMode;

// ---------------------------------------------------------------------------
// Error helpers

inline auto MakeError(Errc code, std::string message, const char *file, int line) -> Error
{
        return Error{code, std::move(message), file, line};
}

auto VkResultToString(VkResult result) -> const char *;

#define SLV_ERROR(code, message) ::slv::vk::MakeError(code, message, __FILE__, __LINE__)
#define SLV_VKERR(result, code, message) \
        ::slv::vk::MakeError(code, std::string(message) + ": VkResult " + ::slv::vk::VkResultToString(result), __FILE__, __LINE__)

// Statement-style check: on failure return unexpected Error from the caller.
#define SLV_VK(expr, code)                                                             \
        do {                                                                           \
                const VkResult slvVkResult_ = (expr);                                  \
                if (slvVkResult_ != VK_SUCCESS)                                        \
                        return std::unexpected(SLV_VKERR(slvVkResult_, code, #expr));  \
        } while (false)

// ---------------------------------------------------------------------------
// Non-owning Vulkan object wrappers (vkDestroy*/free via device/allocator).

struct VkImageWithView {
        VkImage image = VK_NULL_HANDLE;
        VkImageView view = VK_NULL_HANDLE;
        VmaAllocation allocation = VK_NULL_HANDLE;
};

// ---------------------------------------------------------------------------
// Format mapping: slang/preset format tokens + srgb/float flags -> VkFormat.
// Subset contract for M1; unknown tokens fall back to RGBA8 (documented).

auto MapTargetFormat(const std::string &pragmaFormat, bool srgbFramebuffer, bool floatFramebuffer) -> VkFormat;
auto FormatTexelSize(VkFormat format) -> uint32_t; // Bytes per texel; 0 = unsupported.

// ---------------------------------------------------------------------------
// Pass output size resolution — semantics pinned by tests/reference/
// test_viewport_derive.cpp (F6): ScaleType::Viewport resolves against the
// aspect-corrected game rect inside the output target, half-up rounding.

auto DeriveViewportSize(uint32_t targetWidth, uint32_t targetHeight, uint32_t gameWidth, uint32_t gameHeight)
    -> std::pair<uint32_t, uint32_t>;

// ---------------------------------------------------------------------------
// Image utilities shared by the chain (pass targets, history ring, mip
// refill) and the LUT uploader (M3): classic-barrier mip chain generation,
// per-level barriers, full-chain level counts and staging uploads.

inline uint32_t MipLevelsForExtent(uint32_t width, uint32_t height)
{
        uint32_t levels = 1;
        uint32_t size = width > height ? width : height;
        while (size > 1) {
                size >>= 1;
                ++levels;
        }
        return levels;
}

void BarrierMipLevel(VkCommandBuffer cmd, VkImage image, uint32_t level, uint32_t levelCount, VkImageLayout oldLayout,
    VkImageLayout newLayout, VkPipelineStageFlags srcStage, VkPipelineStageFlags dstStage, VkAccessFlags srcAccess,
    VkAccessFlags dstAccess);

// Refills a full mip chain from level 0 (F1 mip plan): level 0 starts in
// SHADER_READ_ONLY (render pass finalLayout) or TRANSFER_DST (freshly
// blitted/copied); every level rests in SHADER_READ_ONLY afterwards.
void RecordMipChain(VkCommandBuffer cmd, VkImage image, uint32_t levels, uint32_t width, uint32_t height,
    bool level0InTransferDst);

// Mapped host-visible staging buffer for one-shot uploads (Image::create,
// LUT upload). Destroy with vmaDestroyBuffer(allocator, buffer, allocation).
auto CreateStagingBuffer(Device &device, VkDeviceSize bytes, VkBuffer *outBuffer, VmaAllocation *outAllocation,
    void **outMapped) -> std::expected<void, Error>;

// ---------------------------------------------------------------------------
// UBO fill — semantics ported from the reference host's Runner::FillBlockBytes
// (PROVENANCE.md): SourceSize/OutputSize/OriginalSize/FinalViewportSize as
// vec4(w, h, 1/w, 1/h), FrameCount with frame_count_mod, FrameDirection,
// FrameTime (µs), PASSINDEX, Rotation (the input frame's rotation, M4),
// <Texture>Size forms, then parameters (preset override first, pragma default
// second, zero fallback).
//
// MVP divergence (documented): the Vulkan-native quad pairs uv (0,0) with
// NDC (-1,-1) top-left, so no per-draw mirror and no flip-cancel dance is
// needed; MVP members receive the unit-quad -> NDC matrix for every pass.
// Rotation (M4) is NOT folded into the pass MVPs: the chain renders the input
// in source orientation (shaders read the Rotation uniform, SPEC gap #5), and
// the presentation draw applies the rotation to the final output.

struct BlockFillContext {
        uint32_t passIndex = 0;
        uint32_t passCount = 0;
        uint32_t inputWidth = 0;    // This pass's input (chain input for pass 0).
        uint32_t inputHeight = 0;
        uint32_t outputWidth = 0;   // This pass's FBO size.
        uint32_t outputHeight = 0;
        uint32_t gameWidth = 0;     // Chain input size (OriginalSize).
        uint32_t gameHeight = 0;
        uint32_t targetWidth = 0;   // Output target size (FinalViewportSize).
        uint32_t targetHeight = 0;
        uint64_t frameCount = 0;
        float frameDeltaUs = 0.0F;
        int32_t frameDirection = 1;
        uint32_t rotation = 0; // InputFrame rotation (0..3, SPEC Rotation semantic).
        bool vertexStage = false;
        bool isLastPass = false;
        const std::vector<std::pair<uint32_t, uint32_t>> *passOutputSizes = nullptr; // PassOutput# size lookups.
        struct AliasSize {                                                           // Alias / #pragma name -> owning pass's output size.
                std::string name;
                uint32_t width = 0;
                uint32_t height = 0;
        };
        const std::vector<AliasSize> *aliasSizes = nullptr;
        struct LutSize {                                                             // LUT name / User# -> uploaded image size.
                std::string name;
                uint32_t width = 0;
                uint32_t height = 0;
        };
        const std::vector<LutSize> *lutSizes = nullptr;
};

auto FillBlockBytes(const UniformBlock &block, const PassDef &passDef, const SlangTranslation &translation,
    const std::vector<ParamOverride> &presetOverrides, const BlockFillContext &ctx) -> std::vector<uint8_t>;

// ---------------------------------------------------------------------------
// Case-insensitive name matching (the parser stores .slangp keys lowercased;
// shader members keep their declared case — RetroArch semantics).

inline auto CaseInsensitiveEqual(const std::string &a, const std::string &b) -> bool
{
        if (a.size() != b.size())
                return false;
        for (size_t i = 0; i < a.size(); ++i)
                if (std::tolower(static_cast<unsigned char>(a[i])) != std::tolower(static_cast<unsigned char>(b[i])))
                        return false;
        return true;
}

// ---------------------------------------------------------------------------
// Debug messenger: collects validation messages; ERROR severity fails tests.

struct ValidationLog {
        std::vector<std::string> errors;
        std::vector<std::string> warnings;
};

} // namespace slv::vk
