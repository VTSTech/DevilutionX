// libslang-vk — Chain implementation (M1): pipelines, FBOs, samplers,
// descriptor sets and the per-frame execution loop.
//
// Descriptor conventions come from the translator (README "Consuming"):
//   set 1 binding 0 — vertex UBO (MVP),   set 2 — fragment combined image
//   samplers (dense), set 3 binding 0 — fragment params UBO (std140 offsets
//   already reflected in UniformBlock::members).
//
// M2 semantics (ported from the reference host, PROVENANCE.md):
//   - pass output sizes: Source/Viewport/Absolute rules, half-up rounding,
//     viewport base aspect-corrected per the F6 pin;
//   - UBO fill: sizes as vec4(w,h,1/w,1/h), FrameCount with frame_count_mod,
//     FrameDirection/FrameTime/PASSINDEX/Rotation, <Texture>Size forms
//     (alias/#pragma-name sizes resolve to the OWNING pass's output),
//     parameters (preset override -> pragma default -> zero);
//   - MVP divergence (documented): the Vulkan-native quad pairs uv (0,0)
//     with NDC (-1,-1) top-left, so every pass gets the unit-quad -> NDC
//     matrix and no per-draw mirror exists anywhere in the chain;
//   - history ring: depth+1 RGBA8 slots at game resolution, seeded on
//     first frame / resize, OriginalHistoryN binds frame T-N (F5);
//   - feedback: demand-driven double buffering for passes read through
//     PassFeedback# / <name>Feedback (SPEC 5.3, F2: feedback_pass unused);
//   - mipmap_input (F1): mip-capable copy of the chain input for pass 0,
//     mip-chained pass outputs refilled between render passes;
//   - sampler policy (SPEC 5.4): Source uses the reading pass's
//     filter/wrap/mipmap; every other binding is nearest/clamp-to-edge.
//
// M4 semantics (plan §8 output (a), the DevilutionX presentation mode):
//   - final passes that do not resolve to the output target size, feedback
//     final passes with rotation, and every swapchain target render into the
//     pass's own FBO and then go through the present draw: a fit-and-center
//     (or explicit-rect) letterbox quad with a linear sampler, black margins
//     (reference-host present math, PROVENANCE.md);
//   - rotation: the chain renders the input in source orientation and every
//     pass's Rotation uniform carries InputFrame::rotation (SPEC gap #5);
//     the present draw rotates the final output clockwise into the rect;
//   - swapchain targets: the host acquires (optionally signaling
//     acquireSemaphore, waited at COLOR_ATTACHMENT_OUTPUT) and presents
//     after the synchronous render returns; the image rests in PRESENT_SRC.
//
// Not yet: sync2/dynamic-rendering fast path (capability-gated perf path,
// plan §8), async upload timeline semaphores.

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <unordered_map>
#include <unordered_set>

#include "vk_internal.hpp"
#include "impls.hpp"

namespace slv::vk {

auto MapTargetFormat(const std::string &pragmaFormat, bool srgbFramebuffer, bool floatFramebuffer) -> VkFormat
{
        // Preset flags first (RetroArch order), then #pragma format tokens;
        // unknown tokens fall back to RGBA8 (documented divergence, SPEC).
        if (floatFramebuffer)
                return VK_FORMAT_R16G16B16A16_SFLOAT;
        if (srgbFramebuffer && pragmaFormat.empty())
                return VK_FORMAT_R8G8B8A8_SRGB;
        if (pragmaFormat.empty() || pragmaFormat == "RGBA8")
                return VK_FORMAT_R8G8B8A8_UNORM;
        if (pragmaFormat == "R8")
                return VK_FORMAT_R8_UNORM;
        if (pragmaFormat == "R16F")
                return VK_FORMAT_R16_SFLOAT;
        if (pragmaFormat == "R32F")
                return VK_FORMAT_R32_SFLOAT;
        if (pragmaFormat == "RGBA16F")
                return VK_FORMAT_R16G16B16A16_SFLOAT;
        if (pragmaFormat == "RGBA32F")
                return VK_FORMAT_R32G32B32A32_SFLOAT;
        if (pragmaFormat == "RGBA16")
                return VK_FORMAT_R16G16B16A16_UNORM;
        if (pragmaFormat == "sRGB8" || pragmaFormat == "SRGB8_ALPHA8")
                return VK_FORMAT_R8G8B8A8_SRGB;
        if (pragmaFormat == "A2B10G10R10_UNORM_PACK32")
                return VK_FORMAT_A2B10G10R10_UNORM_PACK32;
        return VK_FORMAT_R8G8B8A8_UNORM;
}

auto FormatTexelSize(VkFormat format) -> uint32_t
{
        switch (format) {
                case VK_FORMAT_R8_UNORM:
                case VK_FORMAT_R8_SRGB:
                        return 1;
                case VK_FORMAT_R16_SFLOAT:
                case VK_FORMAT_R16_UNORM:
                        return 2;
                case VK_FORMAT_R32_SFLOAT:
                case VK_FORMAT_R8G8B8A8_UNORM:
                case VK_FORMAT_R8G8B8A8_SRGB:
                case VK_FORMAT_B8G8R8A8_UNORM:
                case VK_FORMAT_B8G8R8A8_SRGB:
                case VK_FORMAT_A2B10G10R10_UNORM_PACK32:
                        return 4;
                case VK_FORMAT_R16G16B16A16_SFLOAT:
                case VK_FORMAT_R16G16B16A16_UNORM:
                        return 8;
                case VK_FORMAT_R32G32B32A32_SFLOAT:
                        return 16;
                default:
                        return 0;
        }
}

auto DeriveViewportSize(uint32_t targetWidth, uint32_t targetHeight, uint32_t gameWidth, uint32_t gameHeight)
    -> std::pair<uint32_t, uint32_t>
{
        // Aspect-corrected game rect inside the output target (F6 pin,
        // half-up rounding; pillarbox/letterbox).
        if (targetWidth == 0 || targetHeight == 0 || gameWidth == 0 || gameHeight == 0)
                return {targetWidth, targetHeight};
        const float gameAspect = static_cast<float>(gameWidth) / static_cast<float>(gameHeight);
        if (gameAspect > static_cast<float>(targetWidth) / static_cast<float>(targetHeight))
                return {targetWidth,
                    static_cast<uint32_t>(std::max(1.0F, std::floor(static_cast<float>(targetWidth) / gameAspect + 0.5F)))};
        return {static_cast<uint32_t>(std::max(1.0F, std::floor(static_cast<float>(targetHeight) * gameAspect + 0.5F))),
            targetHeight};
}

namespace {

void WriteFloat(std::vector<uint8_t> &bytes, uint32_t offset, float value)
{
        std::memcpy(bytes.data() + offset, &value, sizeof(float));
}

void WriteInt(std::vector<uint8_t> &bytes, uint32_t offset, int32_t value)
{
        std::memcpy(bytes.data() + offset, &value, sizeof(int32_t));
}

void WriteUint(std::vector<uint8_t> &bytes, uint32_t offset, uint32_t value)
{
        std::memcpy(bytes.data() + offset, &value, sizeof(uint32_t));
}

} // namespace

auto FillBlockBytes(const UniformBlock &block, const PassDef &passDef, const SlangTranslation &translation,
    const std::vector<ParamOverride> &presetOverrides, const BlockFillContext &ctx) -> std::vector<uint8_t>
{
        uint32_t total = 0;
        for (const UniformMember &member : block.members)
                total = std::max(total, member.offset + member.size);
        std::vector<uint8_t> out(std::max<uint32_t>(total, 16), 0);

        // Unit-quad -> NDC matrix, column-major (GLSL convention). Vulkan NDC
        // (-1,-1) is top-left, matching texture uv (0,0), so no flip anywhere.
        static constexpr float kQuadToNdc[16] = {2.F, 0.F, 0.F, 0.F, 0.F, 2.F, 0.F, 0.F, 0.F, 0.F, 1.F, 0.F, -1.F, -1.F, 0.F, 1.F};

        const auto writeVec4 = [&](const UniformMember &member, float x, float y, float z, float w) {
                WriteFloat(out, member.offset, x);
                WriteFloat(out, member.offset + 4, y);
                if (member.type == UniformType::Vec4 || member.type == UniformType::IVec4
                    || member.type == UniformType::UVec4) {
                        WriteFloat(out, member.offset + 8, z);
                        WriteFloat(out, member.offset + 12, w);
                }
        };

        const auto sizeVec = [](uint32_t w, uint32_t h) {
                const float fw = static_cast<float>(w);
                const float fh = static_cast<float>(h);
                return std::make_tuple(fw, fh, fw > 0.0F ? 1.0F / fw : 0.0F, fh > 0.0F ? 1.0F / fh : 0.0F);
        };

        for (const UniformMember &member : block.members) {
                const std::string &name = member.name;
                if (member.arraySize > 0)
                        continue; // Size members are never arrays; leave zeros.

                if (name == "MVP") {
                        for (int i = 0; i < 16; ++i)
                                WriteFloat(out, member.offset + 4 * i, kQuadToNdc[i]);
                        continue;
                }
                if (name == "OutputSize") {
                        const auto [x, y, z, w] = sizeVec(ctx.outputWidth, ctx.outputHeight);
                        writeVec4(member, x, y, z, w);
                        continue;
                }
                if (name == "FinalViewportSize") {
                        const auto [x, y, z, w] = sizeVec(ctx.targetWidth, ctx.targetHeight);
                        writeVec4(member, x, y, z, w);
                        continue;
                }
                if (name == "OriginalSize") {
                        const auto [x, y, z, w] = sizeVec(ctx.gameWidth, ctx.gameHeight);
                        writeVec4(member, x, y, z, w);
                        continue;
                }
                if (name == "SourceSize") {
                        const auto [x, y, z, w] = sizeVec(ctx.inputWidth, ctx.inputHeight);
                        writeVec4(member, x, y, z, w);
                        continue;
                }
                if (name == "FrameCount") {
                        uint64_t count = ctx.frameCount;
                        if (passDef.frameCountMod > 0)
                                count %= passDef.frameCountMod;
                        WriteUint(out, member.offset, static_cast<uint32_t>(count));
                        continue;
                }
                if (name == "FrameDirection") {
                        WriteInt(out, member.offset, ctx.frameDirection);
                        continue;
                }
                if (name == "FrameTime") {
                        WriteFloat(out, member.offset, ctx.frameDeltaUs);
                        continue;
                }
                if (name == "PASSINDEX") {
                        WriteInt(out, member.offset, static_cast<int32_t>(ctx.passIndex));
                        continue;
                }
                if (name == "Rotation") {
                        // SPEC gap #5 semantic: uint 0..3 = 0/90/180/270 degrees
                        // (M4: the input frame's rotation; shaders use it to
                        // adapt, the runner applies the rotation at present).
                        WriteUint(out, member.offset, ctx.rotation);
                        continue;
                }

                // <TextureName>Size members (ported resolution order).
                if (name.size() > 4 && name.compare(name.size() - 4, 4, "Size") == 0) {
                        std::string texName = name.substr(0, name.size() - 4);
                        if (texName.size() > 8 && texName.compare(texName.size() - 8, 8, "Feedback") == 0)
                                texName = texName.substr(0, texName.size() - 8); // Feedback shares pass output size.
                        uint32_t w = 0;
                        uint32_t h = 0;
                        if (texName == "Source") {
                                w = ctx.inputWidth;
                                h = ctx.inputHeight;
                        } else if (texName == "Original" || texName.rfind("OriginalHistory", 0) == 0) {
                                w = ctx.gameWidth;
                                h = ctx.gameHeight;
                        } else if (texName.rfind("PassOutput", 0) == 0 || texName.rfind("PassFeedback", 0) == 0) {
                                // Parse the trailing index after the 10-char prefix.
                                if (texName.size() > 10) {
                                        const int idx = std::atoi(texName.c_str() + 10);
                                        if (idx >= 0 && idx < static_cast<int>(ctx.passCount) && ctx.passOutputSizes != nullptr
                                            && static_cast<size_t>(idx) < ctx.passOutputSizes->size()) {
                                                w = (*ctx.passOutputSizes)[static_cast<size_t>(idx)].first;
                                                h = (*ctx.passOutputSizes)[static_cast<size_t>(idx)].second;
                                        }
                                }
                        } else if (ctx.aliasSizes != nullptr) {
                                // Alias / #pragma name sizes come from the OWNING pass's
                                // output (M2 fix: the reading pass's own size was used
                                // before, which only went unnoticed when every pass
                                // rendered at the same resolution).
                                for (const BlockFillContext::AliasSize &entry : *ctx.aliasSizes)
                                        if (CaseInsensitiveEqual(entry.name, texName)) {
                                                w = entry.width;
                                                h = entry.height;
                                                break;
                                        }
                                if (w == 0 && h == 0 && ctx.lutSizes != nullptr) {
                                        // LUT textures (M3): uploaded size; User# aliases
                                        // resolve to their positional LUT (extension over
                                        // the reference host, which left User#Size at 0).
                                        for (const BlockFillContext::LutSize &entry : *ctx.lutSizes)
                                                if (CaseInsensitiveEqual(entry.name, texName)) {
                                                        w = entry.width;
                                                        h = entry.height;
                                                        break;
                                                }
                                }
                        }
                        const auto [x, y, z, ww] = sizeVec(w, h);
                        writeVec4(member, x, y, z, ww);
                        continue;
                }

                // Shader parameters: preset override, then pragma default, then zero.
                // Name matching is case-insensitive: the parser lowercases .slangp
                // keys while shader members keep their declared case.
                float value = 0.0F;
                bool found = false;
                for (const ParamOverride &override : presetOverrides) {
                        if (CaseInsensitiveEqual(override.name, name)) {
                                value = override.value;
                                found = true;
                                break;
                        }
                }
                if (!found) {
                        for (const slv::preset::ParamInfo &param : translation.parameters) {
                                if (CaseInsensitiveEqual(param.name, name)) {
                                        value = param.defaultValue;
                                        found = true;
                                        break;
                                }
                        }
                }
                WriteFloat(out, member.offset, value);
        }
        return out;
}

} // namespace slv::vk

namespace slv {

// Core-layer vocabulary + internal Vulkan helpers, unqualified in these TUs.
using slv::preset::FilterMode;
using slv::preset::ParamInfo;
using slv::preset::ParamOverride;
using slv::preset::PassDef;
using slv::preset::PassSpirv;
using slv::preset::SlangTranslation;
using slv::preset::ScaleRule;
using slv::preset::ScaleType;
using slv::preset::TextureDef;
using slv::preset::TextureRef;
using slv::preset::UniformBlock;
using slv::preset::UniformMember;
using slv::preset::UniformType;
using slv::preset::WrapMode;
using vk::BarrierMipLevel;
using vk::MipLevelsForExtent;
using vk::RecordMipChain;
using vk::VkImageWithView;

struct Chain::Impl {
        struct Ubo {
                VkBuffer buffer = VK_NULL_HANDLE;
                VmaAllocation allocation = VK_NULL_HANDLE;
                uint8_t *mapped = nullptr;
                VkDeviceSize size = 0;
        };

        // One pass output allocation. Double-buffered passes (read through
        // PassFeedback# / <name>Feedback) own two; readers always bind the
        // buffer NOT being written this frame (SPEC 5.3).
        struct PassTarget {
                VkImageWithView img;                     // img.view samples the full mip chain.
                VkImageView renderView = VK_NULL_HANDLE; // Level-0-only view for the framebuffer (aliases img.view at 1 mip).
                VkFramebuffer framebuffer = VK_NULL_HANDLE;
        };

        struct PassRuntime {
                PassDef def;
                SlangTranslation translation;
                VkShaderModule vertexModule = VK_NULL_HANDLE;
                VkShaderModule fragmentModule = VK_NULL_HANDLE;
                // One graphics pipeline per render-pass format the pass is used
                // with (M4: the final pass renders into its own FBO under the
                // present path but straight into the host target on the direct
                // path — the host and pass formats can differ).
                std::unordered_map<VkFormat, VkPipeline> pipelines;
                VkPipelineLayout pipelineLayout = VK_NULL_HANDLE;
                uint32_t outputWidth = 0;
                uint32_t outputHeight = 0;
                uint32_t allocatedWidth = 0;   // Actual FBO allocation (drift detection).
                uint32_t allocatedHeight = 0;
                VkFormat targetFormat = VK_FORMAT_R8G8B8A8_UNORM;
                bool feedbackRead = false;     // A pass samples this output's previous-frame buffer.
                uint32_t targetCount = 1;      // 2 when feedbackRead.
                uint32_t writeIndex = 0;       // Buffer written this frame; feedback readers use 1 - writeIndex.
                bool nextPassReadsMips = false; // The NEXT pass reads this output with mipmap_input (F1).
                uint32_t mipLevels = 1;        // Resolved at allocation time from nextPassReadsMips.
                std::array<PassTarget, 2> targets;
                Ubo vertexUbo;
                Ubo fragmentUbo;
                VkDescriptorSet set1 = VK_NULL_HANDLE;
                VkDescriptorSet set2 = VK_NULL_HANDLE;
                VkDescriptorSet set3 = VK_NULL_HANDLE;
        };

        Device *device = nullptr;
        std::vector<PassRuntime> passes;
        // Uploaded LUT textures (M3): borrowed views into Preset::Impl's images
        // (read-only, so sharing across chains of the same preset is safe) plus
        // a copy of the TextureDef for the LUT's own sampler options. User#
        // resolves positionally over this list (SPEC 4.5, declaration order).
        struct LutBinding {
                std::string name;
                uint32_t width = 0;
                uint32_t height = 0;
                VkImageView view = VK_NULL_HANDLE;
                slv::preset::TextureDef def;
        };
        std::vector<LutBinding> luts;
        std::vector<ParamOverride> presetParameterOverrides;   // From the .slangp parameters list.
        std::unordered_map<std::string, float> paramOverrides; // setParameter() runtime overrides.
        VkDescriptorPool descriptorPool = VK_NULL_HANDLE;
        VkDescriptorSet emptySet = VK_NULL_HANDLE;
        VkDescriptorSetLayout emptyLayout = VK_NULL_HANDLE;
        std::unordered_map<size_t, VkDescriptorSetLayout> set2Layouts; // Binding count -> layout
        VkDescriptorSetLayout set1Layout = VK_NULL_HANDLE;
        VkDescriptorSetLayout set3Layout = VK_NULL_HANDLE;
        VkPipelineLayout pipelineLayout = VK_NULL_HANDLE;
        std::unordered_map<uint64_t, VkSampler> samplers; // (wrap|filter|mipmap) -> sampler
        std::unordered_map<VkFormat, VkRenderPass> renderPasses;
        std::unordered_map<VkImageView, VkFramebuffer> outputFramebuffers;
        VkBuffer vertexBuffer = VK_NULL_HANDLE;
        VmaAllocation vertexAllocation = VK_NULL_HANDLE;

        // M4 presentation: letterbox/rotation draw of the final pass output
        // into the output target (Offscreen fit-and-center or SwapchainFrame
        // rect). Quad shaders compiled once at create; one pipeline per
        // target format (lazily created against its render pass), one
        // combined-image-sampler set, a host-mapped 4-vertex buffer rewritten
        // per present, and a UNDEFINED -> PRESENT_SRC render pass variant for
        // swapchain images.
        VkShaderModule presentVertexModule = VK_NULL_HANDLE;
        VkShaderModule presentFragmentModule = VK_NULL_HANDLE;
        VkPipelineLayout presentPipelineLayout = VK_NULL_HANDLE;
        std::unordered_map<VkFormat, VkPipeline> presentPipelines;
        VkDescriptorSetLayout presentSetLayout = VK_NULL_HANDLE;
        VkDescriptorSet presentSet = VK_NULL_HANDLE;
        VkSampler presentSampler = VK_NULL_HANDLE;
        VkBuffer presentVertexBuffer = VK_NULL_HANDLE;
        VmaAllocation presentVertexAllocation = VK_NULL_HANDLE;
        void *presentVertexMapped = nullptr;
        std::unordered_map<VkFormat, VkRenderPass> presentSwapchainPasses;
        std::unordered_map<VkImageView, VkFramebuffer> presentSwapchainFramebuffers;

        // History ring (SPEC 5.2): depth+1 RGBA8 slots at game resolution;
        // OriginalHistoryN binds slot [(slot - N) mod count] = frame T-N. All
        // slots are seeded with the current frame on the first render and
        // after every input-size change (RetroArch history warmup, F5).
        uint32_t historyDepth = 0; // Max OriginalHistoryN (N >= 1) referenced by any pass.
        struct HistoryRing {
                std::vector<VkImageWithView> slots;
                uint32_t slot = 0;   // Slot holding the current frame after the update.
                uint32_t width = 0;
                uint32_t height = 0;
                bool seeded = false;
        } history;

        // Pass-0 mipmap_input (F1): the chain input itself stays a 1-mip host
        // image, so pass 0 samples a mip-capable RGBA8 copy blitted and
        // refilled every frame.
        bool useSourceMipCopy = false;
        VkImageWithView sourceMipCopy{};
        uint32_t srcMipWidth = 0;
        uint32_t srcMipHeight = 0;

        std::unordered_set<std::string> warnedTextureFallbacks;

        uint32_t gameWidth = 0;
        uint32_t gameHeight = 0;
        uint32_t targetWidth = 0;
        uint32_t targetHeight = 0;
        VkFormat targetFormat = VK_FORMAT_R8G8B8A8_UNORM;

        ~Impl()
        {
                if (device == nullptr)
                        return;
                VkDevice dev = device->device();
                for (PassRuntime &pass : passes) {
                        for (auto &entry : pass.pipelines)
                                vkDestroyPipeline(dev, entry.second, nullptr);
                        if (pass.pipelineLayout != VK_NULL_HANDLE)
                                vkDestroyPipelineLayout(dev, pass.pipelineLayout, nullptr);
                        if (pass.vertexModule != VK_NULL_HANDLE)
                                vkDestroyShaderModule(dev, pass.vertexModule, nullptr);
                        if (pass.fragmentModule != VK_NULL_HANDLE)
                                vkDestroyShaderModule(dev, pass.fragmentModule, nullptr);
                        for (PassTarget &t : pass.targets)
                                DestroyPassTarget(t);
                        DestroyUbo(pass.vertexUbo);
                        DestroyUbo(pass.fragmentUbo);
                }
                DestroyHistory();
                DestroySourceMipCopy();
                for (auto &entry : outputFramebuffers)
                        vkDestroyFramebuffer(dev, entry.second, nullptr);
                for (auto &entry : renderPasses)
                        vkDestroyRenderPass(dev, entry.second, nullptr);
                for (auto &entry : samplers)
                        vkDestroySampler(dev, entry.second, nullptr);
                for (auto &entry : set2Layouts)
                        vkDestroyDescriptorSetLayout(dev, entry.second, nullptr);
                if (set1Layout != VK_NULL_HANDLE)
                        vkDestroyDescriptorSetLayout(dev, set1Layout, nullptr);
                if (set3Layout != VK_NULL_HANDLE)
                        vkDestroyDescriptorSetLayout(dev, set3Layout, nullptr);
                if (emptyLayout != VK_NULL_HANDLE)
                        vkDestroyDescriptorSetLayout(dev, emptyLayout, nullptr);
                if (descriptorPool != VK_NULL_HANDLE)
                        vkDestroyDescriptorPool(dev, descriptorPool, nullptr);
                if (pipelineLayout != VK_NULL_HANDLE)
                        vkDestroyPipelineLayout(dev, pipelineLayout, nullptr);
                if (vertexBuffer != VK_NULL_HANDLE)
                        vmaDestroyBuffer(device->impl().allocator, vertexBuffer, vertexAllocation);
                for (auto &entry : presentPipelines)
                        vkDestroyPipeline(dev, entry.second, nullptr);
                if (presentVertexModule != VK_NULL_HANDLE)
                        vkDestroyShaderModule(dev, presentVertexModule, nullptr);
                if (presentFragmentModule != VK_NULL_HANDLE)
                        vkDestroyShaderModule(dev, presentFragmentModule, nullptr);
                if (presentPipelineLayout != VK_NULL_HANDLE)
                        vkDestroyPipelineLayout(dev, presentPipelineLayout, nullptr);
                if (presentSetLayout != VK_NULL_HANDLE)
                        vkDestroyDescriptorSetLayout(dev, presentSetLayout, nullptr);
                if (presentSampler != VK_NULL_HANDLE)
                        vkDestroySampler(dev, presentSampler, nullptr);
                if (presentVertexBuffer != VK_NULL_HANDLE)
                        vmaDestroyBuffer(device->impl().allocator, presentVertexBuffer, presentVertexAllocation);
                for (auto &entry : presentSwapchainPasses)
                        vkDestroyRenderPass(dev, entry.second, nullptr);
                for (auto &entry : presentSwapchainFramebuffers)
                        vkDestroyFramebuffer(dev, entry.second, nullptr);
        }

        void DestroyPassTarget(PassTarget &t)
        {
                VkDevice dev = device->device();
                if (t.framebuffer != VK_NULL_HANDLE)
                        vkDestroyFramebuffer(dev, t.framebuffer, nullptr);
                if (t.renderView != VK_NULL_HANDLE && t.renderView != t.img.view)
                        vkDestroyImageView(dev, t.renderView, nullptr);
                if (t.img.view != VK_NULL_HANDLE)
                        vkDestroyImageView(dev, t.img.view, nullptr);
                if (t.img.image != VK_NULL_HANDLE)
                        vmaDestroyImage(device->impl().allocator, t.img.image, t.img.allocation);
                t = {};
        }

        void DestroyTargets(PassRuntime &pass)
        {
                for (PassTarget &t : pass.targets)
                        DestroyPassTarget(t);
                pass.allocatedWidth = 0;
                pass.allocatedHeight = 0;
                pass.mipLevels = 1;
        }

        void DestroyUbo(Ubo &ubo)
        {
                if (ubo.buffer != VK_NULL_HANDLE)
                        vmaDestroyBuffer(device->impl().allocator, ubo.buffer, ubo.allocation);
                ubo = {};
        }

        auto MakeUbo(VkDeviceSize size, Ubo &out) -> std::expected<void, Error>
        {
                VkBufferCreateInfo bufferInfo{};
                bufferInfo.sType = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO;
                bufferInfo.size = size;
                bufferInfo.usage = VK_BUFFER_USAGE_UNIFORM_BUFFER_BIT;
                VmaAllocationCreateInfo allocInfo{};
                allocInfo.usage = VMA_MEMORY_USAGE_AUTO;
                allocInfo.flags = VMA_ALLOCATION_CREATE_HOST_ACCESS_SEQUENTIAL_WRITE_BIT
                    | VMA_ALLOCATION_CREATE_MAPPED_BIT;
                VmaAllocationInfo mappedInfo{};
                if (vmaCreateBuffer(device->impl().allocator, &bufferInfo, &allocInfo, &out.buffer, &out.allocation,
                        &mappedInfo)
                    != VK_SUCCESS)
                        return std::unexpected(SLV_ERROR(Errc::vk_out_of_memory, "uniform buffer allocation failed"));
                out.mapped = static_cast<uint8_t *>(mappedInfo.pMappedData);
                out.size = size;
                return {};
        }

        // Allocates one target slot: image (full mip chain when the next pass
        // reads mips), sample view, level-0 render view + framebuffer, and the
        // initial UNDEFINED -> SHADER_READ_ONLY transition. Feedback buffers
        // are additionally cleared to opaque black so the first frame's
        // readers sample defined content (SPEC 5.3: "first frames after a
        // preset switch start from black").
        auto MakePassTarget(PassRuntime &pass, uint32_t slotIndex, uint32_t width, uint32_t height, bool clearToBlack)
            -> std::expected<void, Error>
        {
                PassTarget &t = pass.targets[slotIndex];
                DestroyPassTarget(t);
                const uint32_t levels = pass.nextPassReadsMips ? MipLevelsForExtent(width, height) : 1;
                pass.mipLevels = levels;

                VkImageCreateInfo imageInfo{};
                imageInfo.sType = VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO;
                imageInfo.imageType = VK_IMAGE_TYPE_2D;
                imageInfo.format = pass.targetFormat;
                imageInfo.extent = {width, height, 1};
                imageInfo.mipLevels = levels;
                imageInfo.arrayLayers = 1;
                imageInfo.samples = VK_SAMPLE_COUNT_1_BIT;
                imageInfo.tiling = VK_IMAGE_TILING_OPTIMAL;
                imageInfo.usage = VK_IMAGE_USAGE_SAMPLED_BIT | VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT
                    | VK_IMAGE_USAGE_TRANSFER_SRC_BIT | VK_IMAGE_USAGE_TRANSFER_DST_BIT;
                imageInfo.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;

                VmaAllocationCreateInfo allocInfo{};
                allocInfo.usage = VMA_MEMORY_USAGE_AUTO_PREFER_DEVICE;
                if (vmaCreateImage(device->impl().allocator, &imageInfo, &allocInfo, &t.img.image, &t.img.allocation,
                        nullptr)
                    != VK_SUCCESS)
                        return std::unexpected(SLV_ERROR(Errc::vk_out_of_memory, "pass target allocation failed"));

                VkImageViewCreateInfo viewInfo{};
                viewInfo.sType = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO;
                viewInfo.image = t.img.image;
                viewInfo.viewType = VK_IMAGE_VIEW_TYPE_2D;
                viewInfo.format = pass.targetFormat;
                viewInfo.subresourceRange = {VK_IMAGE_ASPECT_COLOR_BIT, 0, levels, 0, 1};
                SLV_VK(vkCreateImageView(device->device(), &viewInfo, nullptr, &t.img.view), Errc::vk_error);

                if (levels > 1) {
                        // The framebuffer renders into mip 0 only.
                        viewInfo.subresourceRange = {VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, 0, 1};
                        SLV_VK(vkCreateImageView(device->device(), &viewInfo, nullptr, &t.renderView), Errc::vk_error);
                } else {
                        t.renderView = t.img.view;
                }

                const VkRenderPass renderPass = GetRenderPass(pass.targetFormat);
                VkFramebufferCreateInfo fbInfo{};
                fbInfo.sType = VK_STRUCTURE_TYPE_FRAMEBUFFER_CREATE_INFO;
                fbInfo.renderPass = renderPass;
                fbInfo.attachmentCount = 1;
                fbInfo.pAttachments = &t.renderView;
                fbInfo.width = width;
                fbInfo.height = height;
                fbInfo.layers = 1;
                SLV_VK(vkCreateFramebuffer(device->device(), &fbInfo, nullptr, &t.framebuffer), Errc::vk_error);

                // New images start UNDEFINED; the renderpass expects every
                // target to rest in SHADER_READ_ONLY between uses (its
                // initialLayout), so transition once here.
                auto transitioned = device->impl().SubmitAndWait([&](VkCommandBuffer cmd) {
                        VkImageMemoryBarrier barrier{};
                        barrier.sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER;
                        barrier.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
                        barrier.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
                        barrier.oldLayout = VK_IMAGE_LAYOUT_UNDEFINED;
                        barrier.newLayout = clearToBlack ? VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL
                                                         : VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
                        barrier.image = t.img.image;
                        barrier.subresourceRange = {VK_IMAGE_ASPECT_COLOR_BIT, 0, levels, 0, 1};
                        vkCmdPipelineBarrier(cmd, VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT,
                            clearToBlack ? VK_PIPELINE_STAGE_TRANSFER_BIT : VK_PIPELINE_STAGE_ALL_COMMANDS_BIT, 0, 0,
                            nullptr, 0, nullptr, 1, &barrier);
                        if (clearToBlack) {
                                VkClearColorValue black{};
                                black.float32[0] = black.float32[1] = black.float32[2] = 0.0F;
                                black.float32[3] = 1.0F;
                                const VkImageSubresourceRange range = {VK_IMAGE_ASPECT_COLOR_BIT, 0, levels, 0, 1};
                                vkCmdClearColorImage(cmd, t.img.image, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, &black, 1,
                                    &range);
                                VkImageMemoryBarrier toShader = barrier;
                                toShader.oldLayout = VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL;
                                toShader.newLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
                                vkCmdPipelineBarrier(cmd, VK_PIPELINE_STAGE_TRANSFER_BIT,
                                    VK_PIPELINE_STAGE_ALL_COMMANDS_BIT, 0, 0, nullptr, 0, nullptr, 1, &toShader);
                        }
                });
                if (!transitioned)
                        return std::unexpected(transitioned.error());

                if (slotIndex == 0) {
                        pass.allocatedWidth = width;
                        pass.allocatedHeight = height;
                }
                return {};
        }

        void DestroyHistory()
        {
                if (device == nullptr)
                        return;
                for (VkImageWithView &s : history.slots) {
                        if (s.view != VK_NULL_HANDLE)
                                vkDestroyImageView(device->device(), s.view, nullptr);
                        if (s.image != VK_NULL_HANDLE)
                                vmaDestroyImage(device->impl().allocator, s.image, s.allocation);
                }
                history.slots.clear();
                history.width = history.height = 0;
                history.slot = 0;
                history.seeded = false;
        }

        // Ring slots are created UNDEFINED; the first frame's command buffer
        // seeds every slot from the input image.
        auto MakeHistorySlots(uint32_t width, uint32_t height) -> std::expected<void, Error>
        {
                DestroyHistory();
                const uint32_t count = historyDepth + 1;
                history.slots.resize(count);
                for (uint32_t i = 0; i < count; ++i) {
                        VkImageWithView &s = history.slots[i];
                        VkImageCreateInfo imageInfo{};
                        imageInfo.sType = VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO;
                        imageInfo.imageType = VK_IMAGE_TYPE_2D;
                        imageInfo.format = VK_FORMAT_R8G8B8A8_UNORM;
                        imageInfo.extent = {width, height, 1};
                        imageInfo.mipLevels = 1;
                        imageInfo.arrayLayers = 1;
                        imageInfo.samples = VK_SAMPLE_COUNT_1_BIT;
                        imageInfo.tiling = VK_IMAGE_TILING_OPTIMAL;
                        imageInfo.usage = VK_IMAGE_USAGE_SAMPLED_BIT | VK_IMAGE_USAGE_TRANSFER_DST_BIT;
                        imageInfo.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
                        VmaAllocationCreateInfo allocInfo{};
                        allocInfo.usage = VMA_MEMORY_USAGE_AUTO_PREFER_DEVICE;
                        if (vmaCreateImage(device->impl().allocator, &imageInfo, &allocInfo, &s.image, &s.allocation,
                                nullptr)
                            != VK_SUCCESS)
                                return std::unexpected(
                                    SLV_ERROR(Errc::vk_out_of_memory, "history ring slot allocation failed"));
                        VkImageViewCreateInfo viewInfo{};
                        viewInfo.sType = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO;
                        viewInfo.image = s.image;
                        viewInfo.viewType = VK_IMAGE_VIEW_TYPE_2D;
                        viewInfo.format = VK_FORMAT_R8G8B8A8_UNORM;
                        viewInfo.subresourceRange = {VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, 0, 1};
                        SLV_VK(vkCreateImageView(device->device(), &viewInfo, nullptr, &s.view), Errc::vk_error);
                }
                history.width = width;
                history.height = height;
                history.slot = 0;
                history.seeded = false;
                return {};
        }

        void DestroySourceMipCopy()
        {
                if (device == nullptr)
                        return;
                if (sourceMipCopy.view != VK_NULL_HANDLE)
                        vkDestroyImageView(device->device(), sourceMipCopy.view, nullptr);
                if (sourceMipCopy.image != VK_NULL_HANDLE)
                        vmaDestroyImage(device->impl().allocator, sourceMipCopy.image, sourceMipCopy.allocation);
                sourceMipCopy = {};
                srcMipWidth = 0;
                srcMipHeight = 0;
        }

        // Mip-capable RGBA8 copy of the chain input for pass-0 mipmap_input
        // (F1). Created UNDEFINED; refilled from the input every frame.
        auto MakeSourceMipCopy(uint32_t width, uint32_t height) -> std::expected<void, Error>
        {
                DestroySourceMipCopy();
                const uint32_t levels = MipLevelsForExtent(width, height);
                VkImageCreateInfo imageInfo{};
                imageInfo.sType = VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO;
                imageInfo.imageType = VK_IMAGE_TYPE_2D;
                imageInfo.format = VK_FORMAT_R8G8B8A8_UNORM;
                imageInfo.extent = {width, height, 1};
                imageInfo.mipLevels = levels;
                imageInfo.arrayLayers = 1;
                imageInfo.samples = VK_SAMPLE_COUNT_1_BIT;
                imageInfo.tiling = VK_IMAGE_TILING_OPTIMAL;
                imageInfo.usage = VK_IMAGE_USAGE_SAMPLED_BIT | VK_IMAGE_USAGE_TRANSFER_DST_BIT
                    | VK_IMAGE_USAGE_TRANSFER_SRC_BIT;
                imageInfo.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
                VmaAllocationCreateInfo allocInfo{};
                allocInfo.usage = VMA_MEMORY_USAGE_AUTO_PREFER_DEVICE;
                if (vmaCreateImage(device->impl().allocator, &imageInfo, &allocInfo, &sourceMipCopy.image,
                        &sourceMipCopy.allocation, nullptr)
                    != VK_SUCCESS)
                        return std::unexpected(SLV_ERROR(Errc::vk_out_of_memory, "source mip copy allocation failed"));
                VkImageViewCreateInfo viewInfo{};
                viewInfo.sType = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO;
                viewInfo.image = sourceMipCopy.image;
                viewInfo.viewType = VK_IMAGE_VIEW_TYPE_2D;
                viewInfo.format = VK_FORMAT_R8G8B8A8_UNORM;
                viewInfo.subresourceRange = {VK_IMAGE_ASPECT_COLOR_BIT, 0, levels, 0, 1};
                SLV_VK(vkCreateImageView(device->device(), &viewInfo, nullptr, &sourceMipCopy.view), Errc::vk_error);

                auto transitioned = device->impl().SubmitAndWait([&](VkCommandBuffer cmd) {
                        VkImageMemoryBarrier barrier{};
                        barrier.sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER;
                        barrier.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
                        barrier.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
                        barrier.oldLayout = VK_IMAGE_LAYOUT_UNDEFINED;
                        barrier.newLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
                        barrier.image = sourceMipCopy.image;
                        barrier.subresourceRange = {VK_IMAGE_ASPECT_COLOR_BIT, 0, levels, 0, 1};
                        vkCmdPipelineBarrier(cmd, VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT,
                            VK_PIPELINE_STAGE_ALL_COMMANDS_BIT, 0, 0, nullptr, 0, nullptr, 1, &barrier);
                });
                if (!transitioned)
                        return std::unexpected(transitioned.error());
                srcMipWidth = width;
                srcMipHeight = height;
                return {};
        }

        auto GetRenderPass(VkFormat format) -> VkRenderPass
        {
                const auto it = renderPasses.find(format);
                if (it != renderPasses.end())
                        return it->second;

                VkAttachmentDescription attachment{};
                attachment.format = format;
                attachment.samples = VK_SAMPLE_COUNT_1_BIT;
                attachment.loadOp = VK_ATTACHMENT_LOAD_OP_CLEAR;
                attachment.storeOp = VK_ATTACHMENT_STORE_OP_STORE;
                attachment.stencilLoadOp = VK_ATTACHMENT_LOAD_OP_DONT_CARE;
                attachment.stencilStoreOp = VK_ATTACHMENT_STORE_OP_DONT_CARE;
                // Every target rests in SHADER_READ_ONLY between uses: the next
                // pass samples it, the next frame re-renders into it, and the
                // host composites the final one.
                attachment.initialLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
                attachment.finalLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;

                VkAttachmentReference colorRef{0, VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL};
                VkSubpassDescription subpass{};
                subpass.pipelineBindPoint = VK_PIPELINE_BIND_POINT_GRAPHICS;
                subpass.colorAttachmentCount = 1;
                subpass.pColorAttachments = &colorRef;

                std::array<VkSubpassDependency, 2> deps{};
                deps[0].srcSubpass = VK_SUBPASS_EXTERNAL;
                deps[0].dstSubpass = 0;
                deps[0].srcStageMask = VK_PIPELINE_STAGE_ALL_COMMANDS_BIT;
                deps[0].srcAccessMask = VK_ACCESS_SHADER_READ_BIT | VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT;
                deps[0].dstStageMask = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT;
                deps[0].dstAccessMask = VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT;
                deps[1].srcSubpass = 0;
                deps[1].dstSubpass = VK_SUBPASS_EXTERNAL;
                deps[1].srcStageMask = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT;
                deps[1].srcAccessMask = VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT;
                deps[1].dstStageMask = VK_PIPELINE_STAGE_ALL_COMMANDS_BIT;
                deps[1].dstAccessMask = VK_ACCESS_SHADER_READ_BIT | VK_ACCESS_MEMORY_READ_BIT;

                VkRenderPassCreateInfo info{};
                info.sType = VK_STRUCTURE_TYPE_RENDER_PASS_CREATE_INFO;
                info.attachmentCount = 1;
                info.pAttachments = &attachment;
                info.subpassCount = 1;
                info.pSubpasses = &subpass;
                info.dependencyCount = static_cast<uint32_t>(deps.size());
                info.pDependencies = deps.data();

                VkRenderPass renderPass = VK_NULL_HANDLE;
                if (vkCreateRenderPass(device->device(), &info, nullptr, &renderPass) != VK_SUCCESS)
                        return VK_NULL_HANDLE;
                renderPasses.emplace(format, renderPass);
                return renderPass;
        }

        // Present render pass for swapchain images: UNDEFINED -> PRESENT_SRC
        // (acquired images arrive UNDEFINED; the host presents after render).
        auto GetPresentSwapchainPass(VkFormat format) -> VkRenderPass
        {
                const auto it = presentSwapchainPasses.find(format);
                if (it != presentSwapchainPasses.end())
                        return it->second;

                VkAttachmentDescription attachment{};
                attachment.format = format;
                attachment.samples = VK_SAMPLE_COUNT_1_BIT;
                attachment.loadOp = VK_ATTACHMENT_LOAD_OP_CLEAR;
                attachment.storeOp = VK_ATTACHMENT_STORE_OP_STORE;
                attachment.stencilLoadOp = VK_ATTACHMENT_LOAD_OP_DONT_CARE;
                attachment.stencilStoreOp = VK_ATTACHMENT_STORE_OP_DONT_CARE;
                attachment.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
                attachment.finalLayout = VK_IMAGE_LAYOUT_PRESENT_SRC_KHR;

                VkAttachmentReference colorRef{0, VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL};
                VkSubpassDescription subpass{};
                subpass.pipelineBindPoint = VK_PIPELINE_BIND_POINT_GRAPHICS;
                subpass.colorAttachmentCount = 1;
                subpass.pColorAttachments = &colorRef;

                std::array<VkSubpassDependency, 2> deps{};
                deps[0].srcSubpass = VK_SUBPASS_EXTERNAL;
                deps[0].dstSubpass = 0;
                deps[0].srcStageMask = VK_PIPELINE_STAGE_ALL_COMMANDS_BIT;
                deps[0].srcAccessMask = VK_ACCESS_SHADER_READ_BIT | VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT;
                deps[0].dstStageMask = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT;
                deps[0].dstAccessMask = VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT;
                deps[1].srcSubpass = 0;
                deps[1].dstSubpass = VK_SUBPASS_EXTERNAL;
                deps[1].srcStageMask = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT;
                deps[1].srcAccessMask = VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT;
                deps[1].dstStageMask = VK_PIPELINE_STAGE_ALL_COMMANDS_BIT;
                deps[1].dstAccessMask = VK_ACCESS_MEMORY_READ_BIT;

                VkRenderPassCreateInfo info{};
                info.sType = VK_STRUCTURE_TYPE_RENDER_PASS_CREATE_INFO;
                info.attachmentCount = 1;
                info.pAttachments = &attachment;
                info.subpassCount = 1;
                info.pSubpasses = &subpass;
                info.dependencyCount = static_cast<uint32_t>(deps.size());
                info.pDependencies = deps.data();

                VkRenderPass renderPass = VK_NULL_HANDLE;
                if (vkCreateRenderPass(device->device(), &info, nullptr, &renderPass) != VK_SUCCESS)
                        return VK_NULL_HANDLE;
                presentSwapchainPasses.emplace(format, renderPass);
                return renderPass;
        }

        // Present pipeline for one target format, created lazily against its
        // render pass (pipeline-renderpass compatibility) and cached.
        auto GetPresentPipeline(VkFormat format, VkRenderPass renderPass) -> std::expected<VkPipeline, Error>
        {
                const auto it = presentPipelines.find(format);
                if (it != presentPipelines.end())
                        return it->second;

                std::array<VkPipelineShaderStageCreateInfo, 2> stages{};
                stages[0].sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
                stages[0].stage = VK_SHADER_STAGE_VERTEX_BIT;
                stages[0].module = presentVertexModule;
                stages[0].pName = "main";
                stages[1].sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
                stages[1].stage = VK_SHADER_STAGE_FRAGMENT_BIT;
                stages[1].module = presentFragmentModule;
                stages[1].pName = "main";

                const VkVertexInputBindingDescription bindingDesc = {0, 16, VK_VERTEX_INPUT_RATE_VERTEX};
                std::array<VkVertexInputAttributeDescription, 2> attributes{};
                attributes[0] = {0, 0, VK_FORMAT_R32G32_SFLOAT, 0};
                attributes[1] = {1, 0, VK_FORMAT_R32G32_SFLOAT, 8};
                VkPipelineVertexInputStateCreateInfo vertexInput{};
                vertexInput.sType = VK_STRUCTURE_TYPE_PIPELINE_VERTEX_INPUT_STATE_CREATE_INFO;
                vertexInput.vertexBindingDescriptionCount = 1;
                vertexInput.pVertexBindingDescriptions = &bindingDesc;
                vertexInput.vertexAttributeDescriptionCount = static_cast<uint32_t>(attributes.size());
                vertexInput.pVertexAttributeDescriptions = attributes.data();

                VkPipelineInputAssemblyStateCreateInfo assembly{};
                assembly.sType = VK_STRUCTURE_TYPE_PIPELINE_INPUT_ASSEMBLY_STATE_CREATE_INFO;
                assembly.topology = VK_PRIMITIVE_TOPOLOGY_TRIANGLE_STRIP;

                VkPipelineViewportStateCreateInfo viewportState{};
                viewportState.sType = VK_STRUCTURE_TYPE_PIPELINE_VIEWPORT_STATE_CREATE_INFO;
                viewportState.viewportCount = 1;
                viewportState.scissorCount = 1;

                VkPipelineRasterizationStateCreateInfo raster{};
                raster.sType = VK_STRUCTURE_TYPE_PIPELINE_RASTERIZATION_STATE_CREATE_INFO;
                raster.polygonMode = VK_POLYGON_MODE_FILL;
                raster.cullMode = VK_CULL_MODE_NONE;
                raster.frontFace = VK_FRONT_FACE_COUNTER_CLOCKWISE;
                raster.lineWidth = 1.0F;

                VkPipelineMultisampleStateCreateInfo multisample{};
                multisample.sType = VK_STRUCTURE_TYPE_PIPELINE_MULTISAMPLE_STATE_CREATE_INFO;
                multisample.rasterizationSamples = VK_SAMPLE_COUNT_1_BIT;

                VkPipelineColorBlendAttachmentState blendAttachment{};
                blendAttachment.colorWriteMask = VK_COLOR_COMPONENT_R_BIT | VK_COLOR_COMPONENT_G_BIT
                    | VK_COLOR_COMPONENT_B_BIT | VK_COLOR_COMPONENT_A_BIT;
                VkPipelineColorBlendStateCreateInfo blend{};
                blend.sType = VK_STRUCTURE_TYPE_PIPELINE_COLOR_BLEND_STATE_CREATE_INFO;
                blend.attachmentCount = 1;
                blend.pAttachments = &blendAttachment;

                std::array<VkDynamicState, 2> dynamicStates = {VK_DYNAMIC_STATE_VIEWPORT, VK_DYNAMIC_STATE_SCISSOR};
                VkPipelineDynamicStateCreateInfo dynamic{};
                dynamic.sType = VK_STRUCTURE_TYPE_PIPELINE_DYNAMIC_STATE_CREATE_INFO;
                dynamic.dynamicStateCount = static_cast<uint32_t>(dynamicStates.size());
                dynamic.pDynamicStates = dynamicStates.data();

                VkGraphicsPipelineCreateInfo pipelineInfo{};
                pipelineInfo.sType = VK_STRUCTURE_TYPE_GRAPHICS_PIPELINE_CREATE_INFO;
                pipelineInfo.stageCount = 2;
                pipelineInfo.pStages = stages.data();
                pipelineInfo.pVertexInputState = &vertexInput;
                pipelineInfo.pInputAssemblyState = &assembly;
                pipelineInfo.pViewportState = &viewportState;
                pipelineInfo.pRasterizationState = &raster;
                pipelineInfo.pMultisampleState = &multisample;
                pipelineInfo.pColorBlendState = &blend;
                pipelineInfo.pDynamicState = &dynamic;
                pipelineInfo.layout = presentPipelineLayout;
                pipelineInfo.renderPass = renderPass;

                VkPipeline pipeline = VK_NULL_HANDLE;
                const VkResult created = vkCreateGraphicsPipelines(device->device(), VK_NULL_HANDLE, 1,
                    &pipelineInfo, nullptr, &pipeline);
                if (created != VK_SUCCESS)
                        return std::unexpected(SLV_VKERR(created, Errc::shader_compile, "present pipeline"));
                presentPipelines.emplace(format, pipeline);
                return pipeline;
        }

        // One pass pipeline per render-pass format (lazy, cached). The final
        // pass can be bound against the host target format (direct offscreen
        // path) or its own FBO format (present path) — these can differ.
        auto MakePassPipeline(PassRuntime &pass, VkFormat renderFormat) -> std::expected<VkPipeline, Error>
        {
                std::array<VkPipelineShaderStageCreateInfo, 2> stages{};
                stages[0].sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
                stages[0].stage = VK_SHADER_STAGE_VERTEX_BIT;
                stages[0].module = pass.vertexModule;
                stages[0].pName = "main";
                stages[1].sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
                stages[1].stage = VK_SHADER_STAGE_FRAGMENT_BIT;
                stages[1].module = pass.fragmentModule;
                stages[1].pName = "main";

                const VkVertexInputBindingDescription vertexBinding = {0, 24, VK_VERTEX_INPUT_RATE_VERTEX};
                std::array<VkVertexInputAttributeDescription, 2> attributes{};
                attributes[0] = {0, 0, VK_FORMAT_R32G32B32A32_SFLOAT, 0};
                attributes[1] = {1, 0, VK_FORMAT_R32G32_SFLOAT, 16};
                VkPipelineVertexInputStateCreateInfo vertexInput{};
                vertexInput.sType = VK_STRUCTURE_TYPE_PIPELINE_VERTEX_INPUT_STATE_CREATE_INFO;
                vertexInput.vertexBindingDescriptionCount = 1;
                vertexInput.pVertexBindingDescriptions = &vertexBinding;
                vertexInput.vertexAttributeDescriptionCount = static_cast<uint32_t>(attributes.size());
                vertexInput.pVertexAttributeDescriptions = attributes.data();

                VkPipelineInputAssemblyStateCreateInfo inputAssembly{};
                inputAssembly.sType = VK_STRUCTURE_TYPE_PIPELINE_INPUT_ASSEMBLY_STATE_CREATE_INFO;
                inputAssembly.topology = VK_PRIMITIVE_TOPOLOGY_TRIANGLE_STRIP;

                VkPipelineViewportStateCreateInfo viewportState{};
                viewportState.sType = VK_STRUCTURE_TYPE_PIPELINE_VIEWPORT_STATE_CREATE_INFO;
                viewportState.viewportCount = 1;
                viewportState.scissorCount = 1;

                VkPipelineRasterizationStateCreateInfo raster{};
                raster.sType = VK_STRUCTURE_TYPE_PIPELINE_RASTERIZATION_STATE_CREATE_INFO;
                raster.polygonMode = VK_POLYGON_MODE_FILL;
                raster.cullMode = VK_CULL_MODE_NONE;
                raster.frontFace = VK_FRONT_FACE_COUNTER_CLOCKWISE;
                raster.lineWidth = 1.0F;

                VkPipelineMultisampleStateCreateInfo multisample{};
                multisample.sType = VK_STRUCTURE_TYPE_PIPELINE_MULTISAMPLE_STATE_CREATE_INFO;
                multisample.rasterizationSamples = VK_SAMPLE_COUNT_1_BIT;

                VkPipelineColorBlendAttachmentState blendAttachment{};
                blendAttachment.colorWriteMask = VK_COLOR_COMPONENT_R_BIT | VK_COLOR_COMPONENT_G_BIT
                    | VK_COLOR_COMPONENT_B_BIT | VK_COLOR_COMPONENT_A_BIT;
                VkPipelineColorBlendStateCreateInfo blend{};
                blend.sType = VK_STRUCTURE_TYPE_PIPELINE_COLOR_BLEND_STATE_CREATE_INFO;
                blend.attachmentCount = 1;
                blend.pAttachments = &blendAttachment;

                std::array<VkDynamicState, 2> dynamicStates = {VK_DYNAMIC_STATE_VIEWPORT, VK_DYNAMIC_STATE_SCISSOR};
                VkPipelineDynamicStateCreateInfo dynamic{};
                dynamic.sType = VK_STRUCTURE_TYPE_PIPELINE_DYNAMIC_STATE_CREATE_INFO;
                dynamic.dynamicStateCount = static_cast<uint32_t>(dynamicStates.size());
                dynamic.pDynamicStates = dynamicStates.data();

                const VkRenderPass renderPass = GetRenderPass(renderFormat);
                if (renderPass == VK_NULL_HANDLE)
                        return std::unexpected(SLV_ERROR(Errc::vk_error, "render pass creation failed"));

                VkGraphicsPipelineCreateInfo pipelineInfo{};
                pipelineInfo.sType = VK_STRUCTURE_TYPE_GRAPHICS_PIPELINE_CREATE_INFO;
                pipelineInfo.stageCount = 2;
                pipelineInfo.pStages = stages.data();
                pipelineInfo.pVertexInputState = &vertexInput;
                pipelineInfo.pInputAssemblyState = &inputAssembly;
                pipelineInfo.pViewportState = &viewportState;
                pipelineInfo.pRasterizationState = &raster;
                pipelineInfo.pMultisampleState = &multisample;
                pipelineInfo.pColorBlendState = &blend;
                pipelineInfo.pDynamicState = &dynamic;
                pipelineInfo.layout = pass.pipelineLayout;
                pipelineInfo.renderPass = renderPass;

                VkPipeline pipeline = VK_NULL_HANDLE;
                const VkResult created = vkCreateGraphicsPipelines(device->device(), VK_NULL_HANDLE, 1,
                    &pipelineInfo, nullptr, &pipeline);
                if (created != VK_SUCCESS)
                        return std::unexpected(SLV_VKERR(created, Errc::shader_compile, "pass pipeline"));
                pass.pipelines.emplace(renderFormat, pipeline);
                return pipeline;
        }

        auto GetPassPipeline(PassRuntime &pass, VkFormat renderFormat) -> std::expected<VkPipeline, Error>
        {
                const auto it = pass.pipelines.find(renderFormat);
                if (it != pass.pipelines.end())
                        return it->second;
                return MakePassPipeline(pass, renderFormat);
        }

        auto GetSampler(FilterMode filter, WrapMode wrap, bool mipmap) -> VkSampler
        {
                const uint64_t key = (static_cast<uint64_t>(wrap) << 8) | (static_cast<uint64_t>(filter) << 1)
                    | static_cast<uint64_t>(mipmap);
                const auto it = samplers.find(key);
                if (it != samplers.end())
                        return it->second;

                VkSamplerCreateInfo info{};
                info.sType = VK_STRUCTURE_TYPE_SAMPLER_CREATE_INFO;
                info.magFilter = filter == FilterMode::Linear ? VK_FILTER_LINEAR : VK_FILTER_NEAREST;
                info.minFilter = info.magFilter;
                // Mip-mode policy (SPEC 5.4/F1): only mipmap_input readers get a
                // mip filter (linear, per the reference host's mip plan); every
                // other binding samples level 0. Mipmap LUTs (M3) share the
                // policy: their own `name_mipmap` flag selects the mip filter.
                info.mipmapMode = mipmap ? VK_SAMPLER_MIPMAP_MODE_LINEAR : VK_SAMPLER_MIPMAP_MODE_NEAREST;
                switch (wrap) {
                        case WrapMode::Repeat: info.addressModeU = info.addressModeV = info.addressModeW = VK_SAMPLER_ADDRESS_MODE_REPEAT; break;
                        case WrapMode::MirroredRepeat: info.addressModeU = info.addressModeV = info.addressModeW = VK_SAMPLER_ADDRESS_MODE_MIRRORED_REPEAT; break;
                        case WrapMode::ClampToEdge: break;
                }
                info.anisotropyEnable = VK_FALSE;
                info.maxLod = mipmap ? 128.0F : 0.0F;

                VkSampler sampler = VK_NULL_HANDLE;
                if (vkCreateSampler(device->device(), &info, nullptr, &sampler) != VK_SUCCESS)
                        return VK_NULL_HANDLE;
                samplers.emplace(key, sampler);
                return sampler;
        }

        auto GetSampler(const PassDef &def) -> VkSampler
        {
                return GetSampler(def.filter, def.wrap, def.mipmapInput);
        }

        auto GetSet2Layout(size_t bindingCount) -> VkDescriptorSetLayout
        {
                const auto it = set2Layouts.find(bindingCount);
                if (it != set2Layouts.end())
                        return it->second;
                std::vector<VkDescriptorSetLayoutBinding> bindings(bindingCount);
                for (size_t i = 0; i < bindingCount; ++i)
                        bindings[i] = {static_cast<uint32_t>(i), VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, 1,
                            VK_SHADER_STAGE_FRAGMENT_BIT, nullptr};
                VkDescriptorSetLayoutCreateInfo info{};
                info.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO;
                info.bindingCount = static_cast<uint32_t>(bindings.size());
                info.pBindings = bindings.empty() ? nullptr : bindings.data();
                VkDescriptorSetLayout layout = VK_NULL_HANDLE;
                if (vkCreateDescriptorSetLayout(device->device(), &info, nullptr, &layout) != VK_SUCCESS)
                        return VK_NULL_HANDLE;
                set2Layouts.emplace(bindingCount, layout);
                return layout;
        }

        auto MakeLayout(uint32_t binding, VkShaderStageFlags stage) -> VkDescriptorSetLayout
        {
                const VkDescriptorSetLayoutBinding bindingDef = {binding, VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER, 1, stage,
                    nullptr};
                VkDescriptorSetLayoutCreateInfo info{};
                info.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO;
                info.bindingCount = 1;
                info.pBindings = &bindingDef;
                VkDescriptorSetLayout layout = VK_NULL_HANDLE;
                if (vkCreateDescriptorSetLayout(device->device(), &info, nullptr, &layout) != VK_SUCCESS)
                        return VK_NULL_HANDLE;
                return layout;
        }

        auto AllocateSet(VkDescriptorSetLayout layout) -> VkDescriptorSet
        {
                VkDescriptorSetAllocateInfo info{};
                info.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO;
                info.descriptorPool = descriptorPool;
                info.descriptorSetCount = 1;
                info.pSetLayouts = &layout;
                VkDescriptorSet set = VK_NULL_HANDLE;
                if (vkAllocateDescriptorSets(device->device(), &info, &set) != VK_SUCCESS)
                        return VK_NULL_HANDLE;
                return set;
        }
};

namespace {

// (image barrier + mip chain helpers moved to images.cpp — shared with the
// M3 LUT uploader; see vk::BarrierMipLevel / vk::RecordMipChain)

// Semantics ported from the reference host (F6 pin): Viewport-scale passes
// resolve against the aspect-corrected game rect inside the output target.
void ResolvePassSizes(Chain::Impl &impl, uint32_t gameW, uint32_t gameH, uint32_t targetW, uint32_t targetH)
{
        const auto [viewportW, viewportH] = vk::DeriveViewportSize(targetW, targetH, gameW, gameH);
        float prevW = static_cast<float>(gameW);
        float prevH = static_cast<float>(gameH);
        for (Chain::Impl::PassRuntime &pass : impl.passes) {
                const auto resolve = [](const ScaleRule &rule, float inputSize, float viewportSize) {
                        switch (rule.type) {
                                case ScaleType::Viewport: return viewportSize * rule.scale;
                                case ScaleType::Absolute: return rule.scale;
                                case ScaleType::Source: break;
                        }
                        return inputSize * rule.scale;
                };
                const float outW = resolve(pass.def.scaleX, prevW, static_cast<float>(viewportW));
                const float outH = resolve(pass.def.scaleY, prevH, static_cast<float>(viewportH));
                pass.outputWidth = static_cast<uint32_t>(std::max(1.0F, std::floor(outW + 0.5F)));
                pass.outputHeight = static_cast<uint32_t>(std::max(1.0F, std::floor(outH + 0.5F)));
                prevW = static_cast<float>(pass.outputWidth);
                prevH = static_cast<float>(pass.outputHeight);
        }
}

auto ResolveTexture(const std::string &name, size_t passIndex, Chain::Impl &impl, const InputFrame &input,
    VkImageView fallbackSource, VkImageView &view, VkSampler &sampler) -> std::expected<void, Error>
{
        const Chain::Impl::PassRuntime &self = impl.passes[passIndex];

        // Sampler policy (SPEC 5.4): Source uses the reading pass's
        // filter_linearN/wrap_modeN/mipmap_inputN; every other binding is
        // nearest/clamp-to-edge.
        const auto sourceSampler = [&]() -> std::expected<void, Error> {
                sampler = impl.GetSampler(self.def);
                if (sampler == VK_NULL_HANDLE)
                        return std::unexpected(SLV_ERROR(Errc::vk_error, "sampler creation failed"));
                return {};
        };
        const auto neutralSampler = [&]() -> std::expected<void, Error> {
                const PassDef neutral; // nearest / clamp-to-edge / no mips
                sampler = impl.GetSampler(neutral);
                if (sampler == VK_NULL_HANDLE)
                        return std::unexpected(SLV_ERROR(Errc::vk_error, "sampler creation failed"));
                return {};
        };
        const auto passOutputView = [&](size_t idx, bool previousFrame) -> VkImageView {
                const Chain::Impl::PassRuntime &owner = impl.passes[idx];
                const uint32_t buffer = previousFrame ? 1u - owner.writeIndex : owner.writeIndex;
                return owner.targets[buffer].img.view;
        };

        if (name == "Source") {
                auto got = sourceSampler();
                if (!got)
                        return got;
                if (passIndex == 0)
                        view = impl.useSourceMipCopy ? impl.sourceMipCopy.view : input.view;
                else
                        view = passOutputView(passIndex - 1, false);
                return {};
        }
        if (name == "Original" || name == "OriginalHistory0") {
                auto got = neutralSampler();
                if (!got)
                        return got;
                view = input.view;
                return {};
        }
        if (name.rfind("OriginalHistory", 0) == 0) {
                if (name.size() <= 15 || impl.history.slots.empty())
                        return std::unexpected(
                            SLV_ERROR(Errc::preset_parse, "OriginalHistory# reference without a ring: " + name));
                const int n = std::atoi(name.c_str() + 15);
                const size_t count = impl.history.slots.size();
                if (n < 1 || static_cast<size_t>(n) >= count)
                        return std::unexpected(SLV_ERROR(Errc::preset_parse, "OriginalHistory index out of range: " + name));
                auto got = neutralSampler();
                if (!got)
                        return got;
                view = impl.history.slots[(impl.history.slot + count - static_cast<size_t>(n)) % count].view;
                return {};
        }
        if (name.rfind("PassOutput", 0) == 0 && name.size() > 10) {
                const int idx = std::atoi(name.c_str() + 10);
                if (idx < 0 || static_cast<size_t>(idx) >= impl.passes.size())
                        return std::unexpected(SLV_ERROR(Errc::preset_parse, "PassOutput index out of range: " + name));
                if (static_cast<size_t>(idx) >= passIndex)
                        return std::unexpected(SLV_ERROR(Errc::preset_parse,
                            "PassOutput" + std::to_string(idx) + " is not causal in pass "
                                + std::to_string(passIndex)));
                auto got = neutralSampler();
                if (!got)
                        return got;
                view = passOutputView(static_cast<size_t>(idx), false);
                return {};
        }
        if (name.rfind("PassFeedback", 0) == 0 && name.size() > 12) {
                const int idx = std::atoi(name.c_str() + 12);
                if (idx < 0 || static_cast<size_t>(idx) >= impl.passes.size())
                        return std::unexpected(SLV_ERROR(Errc::preset_parse, "PassFeedback index out of range: " + name));
                auto got = neutralSampler();
                if (!got)
                        return got;
                view = passOutputView(static_cast<size_t>(idx), true);
                return {};
        }
        // <name>Feedback: alias / #pragma name + Feedback suffix = that pass's
        // previous-frame output (SPEC 5.3). Case-insensitive end to end (F9).
        if (name.size() > 8 && vk::CaseInsensitiveEqual(name.substr(name.size() - 8), "Feedback")) {
                const std::string base = name.substr(0, name.size() - 8);
                for (size_t j = 0; j < impl.passes.size(); ++j) {
                        const Chain::Impl::PassRuntime &owner = impl.passes[j];
                        const bool aliasHit = !owner.def.alias.empty() && vk::CaseInsensitiveEqual(owner.def.alias, base);
                        const bool nameHit = !owner.translation.pragmaName.empty()
                            && vk::CaseInsensitiveEqual(owner.translation.pragmaName, base);
                        if (aliasHit || nameHit) {
                                auto got = neutralSampler();
                                if (!got)
                                        return got;
                                view = passOutputView(j, true);
                                return {};
                        }
                }
        }
        // Plain alias / #pragma name: earlier passes' outputs this frame.
        for (size_t j = 0; j < impl.passes.size(); ++j) {
                const Chain::Impl::PassRuntime &owner = impl.passes[j];
                const bool aliasHit = !owner.def.alias.empty() && vk::CaseInsensitiveEqual(owner.def.alias, name);
                const bool nameHit = !owner.translation.pragmaName.empty()
                    && vk::CaseInsensitiveEqual(owner.translation.pragmaName, name);
                if (!aliasHit && !nameHit)
                        continue;
                auto got = neutralSampler();
                if (!got)
                        return got;
                if (j < passIndex) {
                        view = passOutputView(j, false);
                } else {
                        // Not causal this frame: warn once, bind the current source
                        // texture (SPEC 4.5 runtime fallback; never a null bind).
                        if (impl.warnedTextureFallbacks.insert(name).second)
                                std::fprintf(stderr, "libslang-vk: texture '%s' is not causal in pass %zu; "
                                                     "binding the current source instead\n",
                                    name.c_str(), passIndex);
                        view = fallbackSource;
                }
                return {};
        }
        const auto bindLut = [&](size_t index) -> std::expected<void, Error> {
                const Chain::Impl::LutBinding &lut = impl.luts[index];
                if (lut.view == VK_NULL_HANDLE)
                        return std::unexpected(SLV_ERROR(Errc::unsupported_feature,
                            "LUT texture '" + lut.name + "' has no uploaded image (preset was loaded with "
                                "PresetLoadOptions::noLutImages)"));
                sampler = impl.GetSampler(lut.def.filter, lut.def.wrap, lut.def.mipmap);
                if (sampler == VK_NULL_HANDLE)
                        return std::unexpected(SLV_ERROR(Errc::vk_error, "sampler creation failed"));
                view = lut.view;
                return {};
        };
        // User# (SPEC 4.5): positional access to the declared LUTs. The parser
        // materializes preset.textures in declaration order, so User0 is the
        // first declared LUT.
        if (name.size() > 4 && name.compare(0, 4, "User") == 0
            && name.find_first_not_of("0123456789", 4) == std::string::npos) {
                const size_t index = static_cast<size_t>(std::atoi(name.c_str() + 4));
                if (index >= impl.luts.size())
                        return std::unexpected(SLV_ERROR(Errc::preset_parse,
                            "User" + std::to_string(index) + " is outside the declared LUT list ("
                                + std::to_string(impl.luts.size()) + " textures)"));
                return bindLut(index);
        }
        // Declared LUT textures by name: the LUT's own linear/wrap/mipmap
        // options select the sampler (SPEC 5.4); case-insensitive at bind (F9).
        for (size_t l = 0; l < impl.luts.size(); ++l)
                if (vk::CaseInsensitiveEqual(impl.luts[l].name, name))
                        return bindLut(l);
        return std::unexpected(SLV_ERROR(Errc::preset_parse, "unresolved texture reference: " + name));
}

} // namespace

auto Chain::create(Device &device, const Preset &preset, const ChainOptions &options) -> std::expected<Chain, Error>
{
        if (options.outputWidth == 0 || options.outputHeight == 0)
                return std::unexpected(SLV_ERROR(Errc::invalid_argument, "Chain::create: zero output size"));
        if (preset.passCount() == 0)
                return std::unexpected(SLV_ERROR(Errc::invalid_argument, "Chain::create: preset has no passes"));
        // VK_FORMAT_UNDEFINED is allowed for swapchain-only chains: the present
        // draw uses the per-frame SwapchainFrame format (validated at render).
        if (options.outputFormat != VK_FORMAT_UNDEFINED && vk::FormatTexelSize(options.outputFormat) == 0)
                return std::unexpected(SLV_ERROR(Errc::unsupported_feature,
                    "Chain::create: unsupported output VkFormat"));

        Chain out;
        out.impl_ = std::make_unique<Impl>();
        Impl &impl = *out.impl_;
        impl.device = &device;
        impl.targetWidth = options.outputWidth;
        impl.targetHeight = options.outputHeight;
        impl.targetFormat = options.outputFormat;
        // LUT bindings (M3): views are borrowed from the preset's uploaded
        // images — a Preset must therefore outlive every Chain created from it
        // (runner.hpp contract). A preset loaded with noLutImages produces
        // bindings with null views; renders fail with an explicit error.
        for (const slv::preset::TextureDef &texture : preset.impl().parsed.textures) {
                Chain::Impl::LutBinding binding;
                binding.name = texture.name;
                binding.def = texture;
                for (const Preset::Impl::LutGpu &uploaded : preset.impl().luts) {
                        if (vk::CaseInsensitiveEqual(uploaded.name, texture.name)) {
                                binding.view = uploaded.img.view;
                                binding.width = uploaded.width;
                                binding.height = uploaded.height;
                                break;
                        }
                }
                impl.luts.push_back(std::move(binding));
        }
        impl.presetParameterOverrides = preset.impl().parsed.parameters;

        const auto &presetPasses = preset.impl().passDefs;
        const auto &presetTranslations = preset.impl().translations;
        const auto &presetSpirv = preset.impl().spirv;
        const size_t passCount = presetPasses.size();

        // Layouts: set0 empty placeholder, set1 vertex UBO, set2 textures, set3 params UBO.
        VkDescriptorSetLayoutCreateInfo emptyInfo{};
        emptyInfo.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO;
        SLV_VK(vkCreateDescriptorSetLayout(device.device(), &emptyInfo, nullptr, &impl.emptyLayout), Errc::vk_error);

        VkDescriptorSetLayoutBinding set1Binding = {0, VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER, 1, VK_SHADER_STAGE_VERTEX_BIT,
            nullptr};
        VkDescriptorSetLayoutCreateInfo set1Info{};
        set1Info.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO;
        set1Info.bindingCount = 1;
        set1Info.pBindings = &set1Binding;
        SLV_VK(vkCreateDescriptorSetLayout(device.device(), &set1Info, nullptr, &impl.set1Layout), Errc::vk_error);

        VkDescriptorSetLayoutBinding set3Binding = {0, VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER, 1,
            VK_SHADER_STAGE_FRAGMENT_BIT, nullptr};
        VkDescriptorSetLayoutCreateInfo set3Info{};
        set3Info.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO;
        set3Info.bindingCount = 1;
        set3Info.pBindings = &set3Binding;
        SLV_VK(vkCreateDescriptorSetLayout(device.device(), &set3Info, nullptr, &impl.set3Layout), Errc::vk_error);

        // Descriptor pool: worst case 4 sets per pass + uniform/sampler counts,
        // plus the M4 present set.
        VkDescriptorPoolSize poolSizes[2]{};
        poolSizes[0] = {VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER, static_cast<uint32_t>(2 * passCount)};
        uint32_t samplerCount = 0;
        for (const SlangTranslation &translation : presetTranslations)
                samplerCount += static_cast<uint32_t>(translation.fragment.textures.size());
        poolSizes[1] = {VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, std::max(samplerCount, 1u) + 1};
        VkDescriptorPoolCreateInfo poolInfo{};
        poolInfo.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO;
        poolInfo.flags = VK_DESCRIPTOR_POOL_CREATE_FREE_DESCRIPTOR_SET_BIT;
        poolInfo.maxSets = static_cast<uint32_t>(4 * passCount) + 1;
        poolInfo.poolSizeCount = 2;
        poolInfo.pPoolSizes = poolSizes;
        SLV_VK(vkCreateDescriptorPool(device.device(), &poolInfo, nullptr, &impl.descriptorPool), Errc::vk_error);
        impl.emptySet = impl.AllocateSet(impl.emptyLayout);
        if (impl.emptySet == VK_NULL_HANDLE)
                return std::unexpected(SLV_ERROR(Errc::vk_error, "empty descriptor set allocation failed"));

        // Per-pass resources.
        impl.passes.resize(passCount);

        // M2 chain analysis: history depth, demand-driven feedback double
        // buffering (SPEC 5.3), mip targets (F1), and the final-pass feedback
        // guard (the final pass writes the host target directly; a
        // previous-frame copy of it needs the M4 presentation path).
        for (size_t i = 0; i < passCount; ++i) {
                if (i + 1 < passCount && presetPasses[i + 1].mipmapInput)
                        impl.passes[i].nextPassReadsMips = true;
                for (const TextureRef &ref : presetTranslations[i].fragment.textures) {
                        const std::string &n = ref.name;
                        if (n.size() > 12 && n.compare(0, 12, "PassFeedback") == 0) {
                                const int idx = std::atoi(n.c_str() + 12);
                                if (idx >= 0 && static_cast<size_t>(idx) < passCount)
                                        impl.passes[static_cast<size_t>(idx)].feedbackRead = true;
                        } else if (n.size() > 8 && vk::CaseInsensitiveEqual(n.substr(n.size() - 8), "Feedback")) {
                                const std::string base = n.substr(0, n.size() - 8);
                                for (size_t j = 0; j < passCount; ++j) {
                                        const bool aliasHit = !presetPasses[j].alias.empty()
                                            && vk::CaseInsensitiveEqual(presetPasses[j].alias, base);
                                        const bool nameHit = !presetTranslations[j].pragmaName.empty()
                                            && vk::CaseInsensitiveEqual(presetTranslations[j].pragmaName, base);
                                        if (aliasHit || nameHit)
                                                impl.passes[j].feedbackRead = true;
                                }
                        }
                }
        }
        for (size_t i = 0; i < passCount; ++i)
                impl.passes[i].targetCount = impl.passes[i].feedbackRead ? 2u : 1u;
        for (const SlangTranslation &translation : presetTranslations)
                for (const TextureRef &ref : translation.fragment.textures)
                        if (ref.name.size() > 15 && ref.name.compare(0, 15, "OriginalHistory") == 0) {
                                const int n = std::atoi(ref.name.c_str() + 15);
                                if (n >= 1)
                                        impl.historyDepth = std::max(impl.historyDepth, static_cast<uint32_t>(n));
                        }
        impl.useSourceMipCopy = presetPasses[0].mipmapInput;

        for (size_t i = 0; i < passCount; ++i) {
                Chain::Impl::PassRuntime &pass = impl.passes[i];
                pass.def = presetPasses[i];
                pass.translation = presetTranslations[i];
                pass.targetFormat = vk::MapTargetFormat(
                    pass.translation.pragmaFormat, pass.def.srgbFramebuffer, pass.def.floatFramebuffer);

                VkShaderModuleCreateInfo moduleInfo{};
                moduleInfo.sType = VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO;
                moduleInfo.pCode = reinterpret_cast<const uint32_t *>(presetSpirv[i].vertex.data());
                moduleInfo.codeSize = presetSpirv[i].vertex.size();
                SLV_VK(vkCreateShaderModule(device.device(), &moduleInfo, nullptr, &pass.vertexModule), Errc::shader_compile);
                moduleInfo.pCode = reinterpret_cast<const uint32_t *>(presetSpirv[i].fragment.data());
                moduleInfo.codeSize = presetSpirv[i].fragment.size();
                SLV_VK(vkCreateShaderModule(device.device(), &moduleInfo, nullptr, &pass.fragmentModule), Errc::shader_compile);

                // Pipeline layout: [set0 empty, set1 vertex UBO, set2 textures, set3 params UBO],
                // with empty slots where the pass declares no such resource.
                const bool hasVertexUbo = !pass.translation.vertex.uniformBlocks.empty();
                const bool hasFragmentUbo = !pass.translation.fragment.uniformBlocks.empty();
                const size_t textureCount = pass.translation.fragment.textures.size();
                const VkDescriptorSetLayout set2Layout = impl.GetSet2Layout(textureCount);
                if (set2Layout == VK_NULL_HANDLE)
                        return std::unexpected(SLV_ERROR(Errc::vk_error, "set2 layout creation failed"));
                std::array<VkDescriptorSetLayout, 4> setLayouts = {impl.emptyLayout,
                    hasVertexUbo ? impl.set1Layout : impl.emptyLayout, set2Layout,
                    hasFragmentUbo ? impl.set3Layout : impl.emptyLayout};
                VkPipelineLayoutCreateInfo layoutInfo{};
                layoutInfo.sType = VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO;
                layoutInfo.setLayoutCount = static_cast<uint32_t>(setLayouts.size());
                layoutInfo.pSetLayouts = setLayouts.data();
                SLV_VK(vkCreatePipelineLayout(device.device(), &layoutInfo, nullptr, &pass.pipelineLayout), Errc::vk_error);

                // Default-format pipeline (built at load so pipeline problems
                // surface at Preset/Chain creation time). The final pass may
                // render straight into the host target (host format) or into
                // its own FBO (present path) — the render loop lazily builds
                // the variant it needs for the other format.
                const bool isLastPassAtCreate = i + 1 == passCount;
                const bool rendersToHostAtCreate = isLastPassAtCreate && !pass.feedbackRead;
                const VkFormat defaultFormat = rendersToHostAtCreate && impl.targetFormat != VK_FORMAT_UNDEFINED
                    ? impl.targetFormat
                    : pass.targetFormat;
                auto madePipeline = impl.MakePassPipeline(pass, defaultFormat);
                if (!madePipeline)
                        return std::unexpected(madePipeline.error());

                // Descriptor sets (empty slots bound as real empty sets).
                pass.set1 = impl.AllocateSet(hasVertexUbo ? impl.set1Layout : impl.emptyLayout);
                pass.set2 = impl.AllocateSet(set2Layout);
                pass.set3 = impl.AllocateSet(hasFragmentUbo ? impl.set3Layout : impl.emptyLayout);
                if (pass.set1 == VK_NULL_HANDLE || pass.set2 == VK_NULL_HANDLE || pass.set3 == VK_NULL_HANDLE)
                        return std::unexpected(SLV_ERROR(Errc::vk_error, "descriptor set allocation failed"));

                // UBOs sized by the reflected blocks; UBO descriptors written once.
                if (hasVertexUbo) {
                        const UniformBlock &block = pass.translation.vertex.uniformBlocks[0];
                        uint32_t bytes = 16;
                        for (const UniformMember &member : block.members)
                                bytes = std::max(bytes, member.offset + member.size);
                        auto made = impl.MakeUbo(bytes, pass.vertexUbo);
                        if (!made)
                                return std::unexpected(made.error());
                        VkDescriptorBufferInfo bufferInfo{pass.vertexUbo.buffer, 0, VK_WHOLE_SIZE};
                        VkWriteDescriptorSet write{};
                        write.sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
                        write.dstSet = pass.set1;
                        write.dstBinding = 0;
                        write.descriptorType = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER;
                        write.descriptorCount = 1;
                        write.pBufferInfo = &bufferInfo;
                        vkUpdateDescriptorSets(device.device(), 1, &write, 0, nullptr);
                }
                if (hasFragmentUbo) {
                        const UniformBlock &block = pass.translation.fragment.uniformBlocks[0];
                        uint32_t bytes = 16;
                        for (const UniformMember &member : block.members)
                                bytes = std::max(bytes, member.offset + member.size);
                        auto made = impl.MakeUbo(bytes, pass.fragmentUbo);
                        if (!made)
                                return std::unexpected(made.error());
                        VkDescriptorBufferInfo bufferInfo{pass.fragmentUbo.buffer, 0, VK_WHOLE_SIZE};
                        VkWriteDescriptorSet write{};
                        write.sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
                        write.dstSet = pass.set3;
                        write.dstBinding = 0;
                        write.descriptorType = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER;
                        write.descriptorCount = 1;
                        write.pBufferInfo = &bufferInfo;
                        vkUpdateDescriptorSets(device.device(), 1, &write, 0, nullptr);
                }
        }

        // Fullscreen unit quad: positions in [0,1], uv = position (Vulkan-native
        // orientation; see the MVP comment at the top of this file).
        static constexpr float kQuad[4][6] = {
            {0.F, 0.F, 0.F, 1.F, 0.F, 0.F},
            {1.F, 0.F, 0.F, 1.F, 1.F, 0.F},
            {0.F, 1.F, 0.F, 1.F, 0.F, 1.F},
            {1.F, 1.F, 0.F, 1.F, 1.F, 1.F},
        };
        VkBufferCreateInfo bufferInfo{};
        bufferInfo.sType = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO;
        bufferInfo.size = sizeof(kQuad);
        bufferInfo.usage = VK_BUFFER_USAGE_VERTEX_BUFFER_BIT;
        VmaAllocationCreateInfo vertexAllocInfo{};
        vertexAllocInfo.usage = VMA_MEMORY_USAGE_AUTO;
        vertexAllocInfo.flags = VMA_ALLOCATION_CREATE_HOST_ACCESS_SEQUENTIAL_WRITE_BIT
            | VMA_ALLOCATION_CREATE_MAPPED_BIT;
        VmaAllocationInfo vertexMapped{};
        if (vmaCreateBuffer(device.impl().allocator, &bufferInfo, &vertexAllocInfo, &impl.vertexBuffer,
                &impl.vertexAllocation, &vertexMapped)
            != VK_SUCCESS)
                return std::unexpected(SLV_ERROR(Errc::vk_out_of_memory, "vertex buffer allocation failed"));
        std::memcpy(vertexMapped.pMappedData, kQuad, sizeof(kQuad));

        // --- M4 present pipeline (letterbox/rotation draw) -------------------
        {
                // Trivial quad shaders, compiled with the same in-process
                // glslang path as preset passes (bare GLSL, Vulkan 1.0).
                preset::SlangTranslation presentTranslation;
                presentTranslation.vertex.present = true;
                presentTranslation.vertex.glsl =
                    "#version 450\n"
                    "layout(location = 0) in vec2 aPos;\n" // NDC
                    "layout(location = 1) in vec2 aUV;\n"
                    "layout(location = 0) out vec2 vUV;\n"
                    "void main() { vUV = aUV; gl_Position = vec4(aPos, 0.0, 1.0); }\n";
                presentTranslation.fragment.present = true;
                presentTranslation.fragment.glsl =
                    "#version 450\n"
                    "layout(set = 0, binding = 0) uniform sampler2D uTex;\n"
                    "layout(location = 0) in vec2 vUV;\n"
                    "layout(location = 0) out vec4 oColor;\n"
                    "void main() { oColor = texture(uTex, vUV); }\n";
                preset::PassSpirv presentSpirv;
                std::string compileError;
                if (!preset::CompilePass(presentTranslation, presentSpirv, compileError))
                        return std::unexpected(
                            SLV_ERROR(Errc::shader_compile, "present pipeline compile failed: " + compileError));

                VkShaderModuleCreateInfo moduleInfo{};
                moduleInfo.sType = VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO;
                moduleInfo.pCode = reinterpret_cast<const uint32_t *>(presentSpirv.vertex.data());
                moduleInfo.codeSize = presentSpirv.vertex.size();
                SLV_VK(vkCreateShaderModule(device.device(), &moduleInfo, nullptr, &impl.presentVertexModule),
                    Errc::shader_compile);
                moduleInfo.pCode = reinterpret_cast<const uint32_t *>(presentSpirv.fragment.data());
                moduleInfo.codeSize = presentSpirv.fragment.size();
                SLV_VK(vkCreateShaderModule(device.device(), &moduleInfo, nullptr, &impl.presentFragmentModule),
                    Errc::shader_compile);

                VkDescriptorSetLayoutBinding presentBinding = {0, VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, 1,
                    VK_SHADER_STAGE_FRAGMENT_BIT, nullptr};
                VkDescriptorSetLayoutCreateInfo presentLayoutInfo{};
                presentLayoutInfo.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO;
                presentLayoutInfo.bindingCount = 1;
                presentLayoutInfo.pBindings = &presentBinding;
                SLV_VK(vkCreateDescriptorSetLayout(device.device(), &presentLayoutInfo, nullptr,
                    &impl.presentSetLayout), Errc::vk_error);

                VkPipelineLayoutCreateInfo presentPipelineLayoutInfo{};
                presentPipelineLayoutInfo.sType = VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO;
                presentPipelineLayoutInfo.setLayoutCount = 1;
                presentPipelineLayoutInfo.pSetLayouts = &impl.presentSetLayout;
                SLV_VK(vkCreatePipelineLayout(device.device(), &presentPipelineLayoutInfo, nullptr,
                    &impl.presentPipelineLayout), Errc::vk_error);

                impl.presentSet = impl.AllocateSet(impl.presentSetLayout);
                if (impl.presentSet == VK_NULL_HANDLE)
                        return std::unexpected(SLV_ERROR(Errc::vk_error, "present descriptor set allocation failed"));

                // Linear + clamp-to-edge: the reference host's present blit
                // filtering (letterbox edges never wrap; sampling stays inside
                // the rect's uv range).
                VkSamplerCreateInfo samplerInfo{};
                samplerInfo.sType = VK_STRUCTURE_TYPE_SAMPLER_CREATE_INFO;
                samplerInfo.magFilter = VK_FILTER_LINEAR;
                samplerInfo.minFilter = VK_FILTER_LINEAR;
                samplerInfo.mipmapMode = VK_SAMPLER_MIPMAP_MODE_NEAREST;
                samplerInfo.addressModeU = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
                samplerInfo.addressModeV = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
                samplerInfo.addressModeW = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
                samplerInfo.maxLod = 0.0F;
                SLV_VK(vkCreateSampler(device.device(), &samplerInfo, nullptr, &impl.presentSampler), Errc::vk_error);

                // Host-mapped quad buffer, rewritten every present draw.
                VkBufferCreateInfo presentBufferInfo{};
                presentBufferInfo.sType = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO;
                presentBufferInfo.size = 4 * 2 * 2 * sizeof(float);
                presentBufferInfo.usage = VK_BUFFER_USAGE_VERTEX_BUFFER_BIT;
                VmaAllocationCreateInfo presentAllocInfo{};
                presentAllocInfo.usage = VMA_MEMORY_USAGE_AUTO;
                presentAllocInfo.flags = VMA_ALLOCATION_CREATE_HOST_ACCESS_SEQUENTIAL_WRITE_BIT
                    | VMA_ALLOCATION_CREATE_MAPPED_BIT;
                VmaAllocationInfo presentMapped{};
                if (vmaCreateBuffer(device.impl().allocator, &presentBufferInfo, &presentAllocInfo,
                        &impl.presentVertexBuffer, &impl.presentVertexAllocation, &presentMapped)
                    != VK_SUCCESS)
                        return std::unexpected(SLV_ERROR(Errc::vk_out_of_memory, "present vertex buffer allocation failed"));
                impl.presentVertexMapped = presentMapped.pMappedData;
        }

        return out;
}

Chain::~Chain() = default;
Chain::Chain(Chain &&) noexcept = default;
auto Chain::operator=(Chain &&) noexcept -> Chain & = default;

auto Chain::resize(uint32_t /*inputWidth*/, uint32_t /*inputHeight*/, uint32_t outputWidth, uint32_t outputHeight)
    -> std::expected<void, Error>
{
        if (outputWidth == 0 || outputHeight == 0)
                return std::unexpected(SLV_ERROR(Errc::invalid_argument, "Chain::resize: zero output size"));
        impl_->targetWidth = outputWidth;
        impl_->targetHeight = outputHeight;
        return {};
}

auto Chain::setParameter(std::string_view id, float value) -> std::expected<void, Error>
{
        if (id.empty())
                return std::unexpected(SLV_ERROR(Errc::invalid_argument, "Chain::setParameter: empty id"));
        // Case-insensitive update in place: parameter names arrive in mixed
        // case ("GAIN" vs the parser-lowercased "gain"), and case-variant
        // duplicates in the override map would merge in hash order.
        for (auto &[name, existing] : impl_->paramOverrides)
                if (vk::CaseInsensitiveEqual(name, std::string(id))) {
                        existing = value;
                        return {};
                }
        impl_->paramOverrides.insert_or_assign(std::string(id), value);
        return {};
}

auto Chain::passOutputSize(uint32_t passIndex) const -> std::expected<std::pair<uint32_t, uint32_t>, Error>
{
        if (passIndex >= impl_->passes.size())
                return std::unexpected(SLV_ERROR(Errc::invalid_argument, "passOutputSize: index out of range"));
        const Chain::Impl::PassRuntime &pass = impl_->passes[passIndex];
        return std::make_pair(pass.outputWidth, pass.outputHeight);
}

auto Chain::render(const InputFrame &input, const OutputTarget &target, const FrameInfo &frame)
    -> std::expected<void, Error>
{
        Impl &impl = *impl_;
        if (input.view == VK_NULL_HANDLE || input.width == 0 || input.height == 0)
                return std::unexpected(SLV_ERROR(Errc::invalid_argument, "render: invalid input frame"));

        // M4 output dispatch: swapchain targets always go through the present
        // draw (letterbox + rotation); offscreen targets keep the direct
        // byte-exact path only when the final pass matches the target exactly.
        const bool swapchainTarget = std::holds_alternative<SwapchainFrame>(target);
        const SwapchainFrame *swap = nullptr;
        const Offscreen *offscreen = nullptr;
        if (swapchainTarget) {
                swap = &std::get<SwapchainFrame>(target);
                if (swap->swapchainImageView == VK_NULL_HANDLE || swap->width == 0 || swap->height == 0)
                        return std::unexpected(SLV_ERROR(Errc::invalid_argument, "render: invalid swapchain target"));
                if (swap->format == VK_FORMAT_UNDEFINED || vk::FormatTexelSize(swap->format) == 0)
                        return std::unexpected(SLV_ERROR(Errc::unsupported_feature,
                            "render: unsupported swapchain VkFormat"));
                if (swap->width != impl.targetWidth || swap->height != impl.targetHeight)
                        return std::unexpected(SLV_ERROR(Errc::invalid_argument,
                            "render: swapchain size changed; call resize() with the new extent"));
        } else {
                offscreen = &std::get<Offscreen>(target);
                if (offscreen->view == VK_NULL_HANDLE || offscreen->width == 0 || offscreen->height == 0)
                        return std::unexpected(SLV_ERROR(Errc::invalid_argument, "render: invalid offscreen target"));
                if (impl.targetFormat == VK_FORMAT_UNDEFINED)
                        return std::unexpected(SLV_ERROR(Errc::invalid_argument,
                            "render: offscreen targets need a concrete ChainOptions::outputFormat"));
                if (offscreen->width != impl.targetWidth || offscreen->height != impl.targetHeight
                    || offscreen->format != impl.targetFormat)
                        return std::unexpected(SLV_ERROR(Errc::invalid_argument,
                            "render: offscreen target must match ChainOptions size/format (resize() to change)"));
        }

        impl.gameWidth = input.width;
        impl.gameHeight = input.height;

        // History ring updates and the pass-0 mip copy blit from the input
        // image itself; it is never modified and rests in SHADER_READ_ONLY
        // before and after the frame.
        const bool needsSourceImage = impl.historyDepth > 0 || impl.useSourceMipCopy;
        if (needsSourceImage && input.image == VK_NULL_HANDLE)
                return std::unexpected(SLV_ERROR(Errc::invalid_argument,
                    "render: InputFrame::image is required for history/mipmap presets"));

        // 1. Resolve pass sizes. M4: the final pass may resolve to any size —
        // it renders into its own FBO and the present draw letterboxes the
        // result into the output target (rotation-aware fit).
        ResolvePassSizes(impl, input.width, input.height, impl.targetWidth, impl.targetHeight);
        const uint32_t rotation = static_cast<uint32_t>(input.rotation);
        Chain::Impl::PassRuntime &last = impl.passes.back();
        const bool finalSizeMatches = last.outputWidth == impl.targetWidth
            && last.outputHeight == impl.targetHeight;
        const bool presentNeeded = swapchainTarget || rotation != 0 || !finalSizeMatches;

        // The 1:1 feedback resolve blit (direct offscreen path) needs the host
        // image handle; the present path only samples the pass's own target.
        const bool feedbackDirectResolve = !swapchainTarget && impl.passes.back().feedbackRead && !presentNeeded;
        if (feedbackDirectResolve && offscreen->image == VK_NULL_HANDLE)
                return std::unexpected(SLV_ERROR(Errc::invalid_argument,
                    "render: Offscreen::image is required when the final pass is feedback-read"));

        // 2. Ensure pass targets exist; recreate on size drift (M2: resize()
        // no longer requires a new Chain). Intermediate passes always render
        // into their own target; a feedback-read FINAL pass also renders into
        // its own (double-buffered) target and resolves to the host image
        // with a blit afterwards. Feedback readers get a cleared second
        // buffer so first-frame samples are defined (SPEC 5.3).
        for (size_t i = 0; i < impl.passes.size(); ++i) {
                Chain::Impl::PassRuntime &pass = impl.passes[i];
                // M4: a non-feedback final pass still needs its own target when
                // the present draw runs (size mismatch, rotation, swapchain).
                const bool isLastPass = i + 1 == impl.passes.size();
                const bool needsOwnTarget = i + 1 < impl.passes.size() || pass.feedbackRead
                    || (isLastPass && presentNeeded);
                if (!needsOwnTarget)
                        continue;
                if (pass.targets[0].img.image == VK_NULL_HANDLE || pass.outputWidth != pass.allocatedWidth
                    || pass.outputHeight != pass.allocatedHeight) {
                        auto made = impl.MakePassTarget(pass, 0, pass.outputWidth, pass.outputHeight, false);
                        if (!made)
                                return std::unexpected(made.error());
                        if (pass.targetCount == 2) {
                                made = impl.MakePassTarget(pass, 1, pass.outputWidth, pass.outputHeight, true);
                                if (!made)
                                        return std::unexpected(made.error());
                        }
                }
        }

        // 3. Output framebuffer over the host view (cached per view). Offscreen
        // targets only; the present draw reuses it, swapchain targets get their
        // framebuffer per acquired image view (step 5b).
        VkFramebuffer outputFramebuffer = VK_NULL_HANDLE;
        if (!swapchainTarget) {
                const auto it = impl.outputFramebuffers.find(offscreen->view);
                if (it != impl.outputFramebuffers.end()) {
                        outputFramebuffer = it->second;
                } else {
                        const VkRenderPass renderPass = impl.GetRenderPass(impl.targetFormat);
                        VkFramebufferCreateInfo fbInfo{};
                        fbInfo.sType = VK_STRUCTURE_TYPE_FRAMEBUFFER_CREATE_INFO;
                        fbInfo.renderPass = renderPass;
                        fbInfo.attachmentCount = 1;
                        fbInfo.pAttachments = &offscreen->view;
                        fbInfo.width = impl.targetWidth;
                        fbInfo.height = impl.targetHeight;
                        fbInfo.layers = 1;
                        SLV_VK(vkCreateFramebuffer(impl.device->device(), &fbInfo, nullptr, &outputFramebuffer),
                            Errc::vk_error);
                        impl.outputFramebuffers.emplace(offscreen->view, outputFramebuffer);
                }
        }

        // 4. Per-pass UBO fills.
        std::vector<std::pair<uint32_t, uint32_t>> passSizes;
        for (const Chain::Impl::PassRuntime &pass : impl.passes)
                passSizes.emplace_back(pass.outputWidth, pass.outputHeight);

        // Alias / #pragma name size lookups resolve to the OWNING pass's
        // output size (the fill context carries them case-insensitively). M4:
        // the final pass renders at its own FBO size — the present draw
        // letterboxes it — so <alias>Size reflects that actual output.
        std::vector<vk::BlockFillContext::AliasSize> aliasSizes;
        for (size_t j = 0; j < impl.passes.size(); ++j) {
                const Chain::Impl::PassRuntime &owner = impl.passes[j];
                if (!owner.def.alias.empty())
                        aliasSizes.push_back({owner.def.alias, owner.outputWidth, owner.outputHeight});
                if (!owner.translation.pragmaName.empty())
                        aliasSizes.push_back(
                            {owner.translation.pragmaName, owner.outputWidth, owner.outputHeight});
        }

        // LUT sizes (M3): declared names plus User# positional aliases; the
        // fill resolves <Texture>Size members case-insensitively over both.
        std::vector<vk::BlockFillContext::LutSize> lutSizes;
        for (size_t l = 0; l < impl.luts.size(); ++l) {
                if (impl.luts[l].width != 0 || impl.luts[l].height != 0)
                        lutSizes.push_back({impl.luts[l].name, impl.luts[l].width, impl.luts[l].height});
                lutSizes.push_back({"User" + std::to_string(l), impl.luts[l].width, impl.luts[l].height});
        }

        // 4b. Ring / mip-copy (re)allocation happens before recording so the
        // error paths stay outside the command lambda.
        if (impl.historyDepth > 0
            && (impl.history.width != input.width || impl.history.height != input.height)) {
                auto made = impl.MakeHistorySlots(input.width, input.height);
                if (!made)
                        return std::unexpected(made.error());
        }
        if (impl.useSourceMipCopy && (impl.srcMipWidth != input.width || impl.srcMipHeight != input.height)) {
                auto made = impl.MakeSourceMipCopy(input.width, input.height);
                if (!made)
                        return std::unexpected(made.error());
        }

        // 4c. History ring slot bookkeeping must happen BEFORE descriptor
        // writes: OriginalHistoryN binds (slot - N) as of AFTER this frame's
        // advance, and descriptors capture views up front.
        bool seedRing = false;
        if (impl.historyDepth > 0 && !impl.history.slots.empty()) {
                if (!impl.history.seeded) {
                        seedRing = true;
                        impl.history.slot = 0;
                        impl.history.seeded = true;
                } else {
                        impl.history.slot = (impl.history.slot + 1) % impl.history.slots.size();
                }
        }

        // Effective parameter values: .slangp overrides, then setParameter()
        // runtime overrides on top (matching is case-insensitive in the fill,
        // consistent with the translator's member whitelist).
        std::vector<ParamOverride> effectiveOverrides = impl.presetParameterOverrides;
        for (const auto &[name, value] : impl.paramOverrides) {
                bool found = false;
                for (ParamOverride &override : effectiveOverrides)
                        if (vk::CaseInsensitiveEqual(override.name, name)) {
                                override.value = value;
                                found = true;
                                break;
                        }
                if (!found)
                        effectiveOverrides.push_back({name, value});
        }

        uint32_t inputW = input.width;
        uint32_t inputH = input.height;
        for (size_t i = 0; i < impl.passes.size(); ++i) {
                Chain::Impl::PassRuntime &pass = impl.passes[i];

                vk::BlockFillContext ctx;
                ctx.passIndex = static_cast<uint32_t>(i);
                ctx.passCount = static_cast<uint32_t>(impl.passes.size());
                ctx.inputWidth = inputW;
                ctx.inputHeight = inputH;
                ctx.outputWidth = pass.outputWidth;
                ctx.outputHeight = pass.outputHeight;
                ctx.gameWidth = input.width;
                ctx.gameHeight = input.height;
                ctx.targetWidth = impl.targetWidth;
                ctx.targetHeight = impl.targetHeight;
                ctx.frameCount = frame.frameCount;
                ctx.frameDeltaUs = frame.frameDeltaUs;
                ctx.frameDirection = frame.frameDirection;
                ctx.rotation = rotation;
                ctx.passOutputSizes = &passSizes;
                ctx.aliasSizes = &aliasSizes;
                ctx.lutSizes = &lutSizes;

                if (!pass.translation.vertex.uniformBlocks.empty()) {
                        ctx.vertexStage = true;
                        const std::vector<uint8_t> bytes = vk::FillBlockBytes(
                            pass.translation.vertex.uniformBlocks[0], pass.def, pass.translation, effectiveOverrides, ctx);
                        if (bytes.size() > static_cast<size_t>(pass.vertexUbo.size))
                                return std::unexpected(SLV_ERROR(Errc::invalid_argument, "vertex UBO overflow"));
                        if (pass.vertexUbo.mapped != nullptr)
                                std::memcpy(pass.vertexUbo.mapped, bytes.data(), bytes.size());
                }
                if (!pass.translation.fragment.uniformBlocks.empty()) {
                        ctx.vertexStage = false;
                        const std::vector<uint8_t> bytes = vk::FillBlockBytes(
                            pass.translation.fragment.uniformBlocks[0], pass.def, pass.translation, effectiveOverrides, ctx);
                        if (bytes.size() > static_cast<size_t>(pass.fragmentUbo.size))
                                return std::unexpected(SLV_ERROR(Errc::invalid_argument, "fragment UBO overflow"));
                        if (pass.fragmentUbo.mapped != nullptr)
                                std::memcpy(pass.fragmentUbo.mapped, bytes.data(), bytes.size());
                }
                inputW = pass.outputWidth;
                inputH = pass.outputHeight;
        }

        // 5. Bind textures into set 2 for every pass (M2 resolver).
        for (size_t i = 0; i < impl.passes.size(); ++i) {
                Chain::Impl::PassRuntime &pass = impl.passes[i];
                const size_t textureCount = pass.translation.fragment.textures.size();
                if (textureCount == 0)
                        continue;
                // Fallback source for non-causal alias/pragma-name taps (SPEC 4.5):
                // the same texture Source would bind for this pass.
                VkImageView fallbackSource = VK_NULL_HANDLE;
                if (i == 0)
                        fallbackSource = impl.useSourceMipCopy ? impl.sourceMipCopy.view : input.view;
                else {
                        const Chain::Impl::PassRuntime &prev = impl.passes[i - 1];
                        fallbackSource = prev.targets[prev.writeIndex].img.view;
                }
                std::vector<VkDescriptorImageInfo> imageInfos(textureCount);
                std::vector<VkWriteDescriptorSet> writes(textureCount);
                for (size_t t = 0; t < textureCount; ++t) {
                        const TextureRef &ref = pass.translation.fragment.textures[t];
                        VkImageView view = VK_NULL_HANDLE;
                        VkSampler sampler = VK_NULL_HANDLE;
                        auto resolved = ResolveTexture(ref.name, i, impl, input, fallbackSource, view, sampler);
                        if (!resolved)
                                return std::unexpected(resolved.error());
                        imageInfos[t] = {sampler, view, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL};
                        writes[t] = {VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET, nullptr, pass.set2,
                            static_cast<uint32_t>(t), 0, 1, VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER,
                            &imageInfos[t], nullptr, nullptr};
                }
                vkUpdateDescriptorSets(impl.device->device(), static_cast<uint32_t>(writes.size()), writes.data(), 0,
                    nullptr);
        }

        // 5b. Present draw setup (M4): dest rect, quad vertices, present
        // descriptor and the target render pass/framebuffer. All host-side so
        // error paths stay outside the command lambda.
        VkRect2D presentDest{};
        VkRenderPass presentRenderPass = VK_NULL_HANDLE;
        VkFramebuffer presentFramebuffer = VK_NULL_HANDLE;
        VkPipeline presentPipeline = VK_NULL_HANDLE;
        if (presentNeeded) {
                const Chain::Impl::PassRuntime &lastPass = impl.passes.back();
                // Content dims: a quarter turn swaps the aspect for the fit.
                const bool quarterTurn = rotation == 1 || rotation == 3;
                const uint32_t contentW = quarterTurn ? lastPass.outputHeight : lastPass.outputWidth;
                const uint32_t contentH = quarterTurn ? lastPass.outputWidth : lastPass.outputHeight;
                if (swapchainTarget && swap->outputRect.width != 0 && swap->outputRect.height != 0) {
                        const Rect &r = swap->outputRect;
                        if (r.x < 0 || r.y < 0 || static_cast<uint32_t>(r.x) + r.width > impl.targetWidth
                            || static_cast<uint32_t>(r.y) + r.height > impl.targetHeight)
                                return std::unexpected(SLV_ERROR(Errc::invalid_argument,
                                    "render: outputRect lies outside the swapchain target"));
                        presentDest.offset = {r.x, r.y};
                        presentDest.extent = {r.width, r.height};
                } else {
                        // Reference-host present math: fit scale, lround, clamp
                        // overshoot, integer-center (PROVENANCE.md).
                        const float fitScale = std::min(
                            static_cast<float>(impl.targetWidth) / static_cast<float>(contentW),
                            static_cast<float>(impl.targetHeight) / static_cast<float>(contentH));
                        const int32_t dw = std::min(static_cast<int32_t>(impl.targetWidth),
                            static_cast<int32_t>(std::lround(static_cast<float>(contentW) * fitScale)));
                        const int32_t dh = std::min(static_cast<int32_t>(impl.targetHeight),
                            static_cast<int32_t>(std::lround(static_cast<float>(contentH) * fitScale)));
                        if (dw <= 0 || dh <= 0)
                                return std::unexpected(SLV_ERROR(Errc::invalid_argument,
                                    "render: output target is smaller than one pixel of chain output"));
                        presentDest.offset = {static_cast<int32_t>((impl.targetWidth - static_cast<uint32_t>(dw)) / 2),
                            static_cast<int32_t>((impl.targetHeight - static_cast<uint32_t>(dh)) / 2)};
                        presentDest.extent = {static_cast<uint32_t>(dw), static_cast<uint32_t>(dh)};
                }

                // Quad vertices: positions map the rect to NDC; uvs apply the
                // rotation. Dest normalized (nx, ny) samples the source at:
                //   0: (nx, ny)   1 Cw90: (ny, 1-nx)
                //   2 Cw180: (1-nx, 1-ny)   3 Cw270: (1-ny, nx)
                struct PresentVert {
                        float x, y, u, v;
                };
                static_assert(sizeof(PresentVert) == 4 * sizeof(float));
                const float tw = static_cast<float>(impl.targetWidth);
                const float th = static_cast<float>(impl.targetHeight);
                const float ndcX0 = 2.0F * static_cast<float>(presentDest.offset.x) / tw - 1.0F;
                const float ndcX1 = 2.0F * static_cast<float>(presentDest.offset.x
                                                      + static_cast<int32_t>(presentDest.extent.width))
                        / tw
                    - 1.0F;
                // Vulkan NDC is y-down (y=-1 is the top row), matching texture
                // v=0 — the same convention as the pass quad's MVP.
                const float ndcY0 = 2.0F * static_cast<float>(presentDest.offset.y) / th - 1.0F;
                const float ndcY1 = 2.0F * static_cast<float>(presentDest.offset.y
                                                      + static_cast<int32_t>(presentDest.extent.height))
                        / th
                    - 1.0F;
                const auto mapUv = [&](float nx, float ny) {
                        switch (rotation) {
                                case 1: return std::pair<float, float>{ny, 1.0F - nx};
                                case 2: return std::pair<float, float>{1.0F - nx, 1.0F - ny};
                                case 3: return std::pair<float, float>{1.0F - ny, nx};
                                default: return std::pair<float, float>{nx, ny};
                        }
                };
                const auto fillVert = [&](size_t i, float ndcX, float ndcY, float nx, float ny) {
                        const auto [u, v] = mapUv(nx, ny);
                        PresentVert vert{ndcX, ndcY, u, v};
                        std::memcpy(static_cast<unsigned char *>(impl.presentVertexMapped) + i * sizeof(PresentVert),
                            &vert, sizeof(PresentVert));
                };
                fillVert(0, ndcX0, ndcY0, 0.0F, 0.0F); // TL
                fillVert(1, ndcX1, ndcY0, 1.0F, 0.0F); // TR
                fillVert(2, ndcX0, ndcY1, 0.0F, 1.0F); // BL
                fillVert(3, ndcX1, ndcY1, 1.0F, 1.0F); // BR

                // Present descriptor: the final pass's freshly written buffer
                // (SHADER_READ_ONLY after its render pass).
                const VkImageView presentSrcView = lastPass.targets[lastPass.writeIndex].img.view;
                VkDescriptorImageInfo presentImage{
                    impl.presentSampler, presentSrcView, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL};
                VkWriteDescriptorSet presentWrite{VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET, nullptr, impl.presentSet,
                    0, 0, 1, VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, &presentImage, nullptr, nullptr};
                vkUpdateDescriptorSets(impl.device->device(), 1, &presentWrite, 0, nullptr);

                if (swapchainTarget) {
                        // UNDEFINED -> PRESENT_SRC variant over the acquired image.
                        presentRenderPass = impl.GetPresentSwapchainPass(swap->format);
                        if (presentRenderPass == VK_NULL_HANDLE)
                                return std::unexpected(
                                    SLV_ERROR(Errc::vk_error, "present render pass creation failed"));
                        const auto it = impl.presentSwapchainFramebuffers.find(swap->swapchainImageView);
                        if (it != impl.presentSwapchainFramebuffers.end()) {
                                presentFramebuffer = it->second;
                        } else {
                                VkFramebufferCreateInfo fbInfo{};
                                fbInfo.sType = VK_STRUCTURE_TYPE_FRAMEBUFFER_CREATE_INFO;
                                fbInfo.renderPass = presentRenderPass;
                                fbInfo.attachmentCount = 1;
                                fbInfo.pAttachments = &swap->swapchainImageView;
                                fbInfo.width = impl.targetWidth;
                                fbInfo.height = impl.targetHeight;
                                fbInfo.layers = 1;
                                SLV_VK(vkCreateFramebuffer(impl.device->device(), &fbInfo, nullptr,
                                    &presentFramebuffer), Errc::vk_error);
                                impl.presentSwapchainFramebuffers.emplace(
                                    swap->swapchainImageView, presentFramebuffer);
                        }
                } else {
                        // Shared SHADER_READ_ONLY pass over the host image view.
                        presentRenderPass = impl.GetRenderPass(impl.targetFormat);
                        presentFramebuffer = outputFramebuffer;
                }
                // One pipeline per target format, built against its render pass.
                auto madePipeline = impl.GetPresentPipeline(
                    swapchainTarget ? swap->format : impl.targetFormat, presentRenderPass);
                if (!madePipeline)
                        return std::unexpected(madePipeline.error());
                presentPipeline = *madePipeline;
        }

        // Pipelines for this frame's render modes, fetched before recording so
        // lazy per-format creation errors stay on the error path.
        std::vector<VkPipeline> framePipelines(impl.passes.size());
        for (size_t i = 0; i < impl.passes.size(); ++i) {
                Chain::Impl::PassRuntime &pass = impl.passes[i];
                const bool isLast = i + 1 == impl.passes.size();
                const bool rendersToHost = isLast && !pass.feedbackRead && !presentNeeded;
                auto pipeline = impl.GetPassPipeline(pass, rendersToHost ? impl.targetFormat : pass.targetFormat);
                if (!pipeline)
                        return std::unexpected(pipeline.error());
                framePipelines[i] = *pipeline;
        }

        // 6. Record + submit (synchronous). Swapchain frames wait the host's
        // acquire semaphore at COLOR_ATTACHMENT_OUTPUT; the submit fence makes
        // the image present-ready when render() returns.
        auto recorded = impl.device->impl().SubmitAndWait([&](VkCommandBuffer cmd) {
                // The input image is borrowed as a blit source for the history
                // ring and the pass-0 mip copy, then restored to
                // SHADER_READ_ONLY before any pass samples it.
                bool inputInTransferSrc = false;
                const auto promoteInput = [&]() {
                        if (inputInTransferSrc)
                                return;
                        BarrierMipLevel(cmd, input.image, 0, 1, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL,
                            VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL, VK_PIPELINE_STAGE_ALL_COMMANDS_BIT,
                            VK_PIPELINE_STAGE_TRANSFER_BIT, VK_ACCESS_SHADER_READ_BIT, VK_ACCESS_TRANSFER_READ_BIT);
                        inputInTransferSrc = true;
                };
                const auto restoreInput = [&]() {
                        if (!inputInTransferSrc)
                                return;
                        BarrierMipLevel(cmd, input.image, 0, 1, VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL,
                            VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL, VK_PIPELINE_STAGE_TRANSFER_BIT,
                            VK_PIPELINE_STAGE_ALL_COMMANDS_BIT, VK_ACCESS_TRANSFER_READ_BIT,
                            VK_ACCESS_SHADER_READ_BIT);
                        inputInTransferSrc = false;
                };
                const auto blitInputTo = [&](VkImage dst) {
                        VkImageBlit blit{};
                        blit.srcSubresource = {VK_IMAGE_ASPECT_COLOR_BIT, 0, 0, 1};
                        blit.srcOffsets[1] = {static_cast<int32_t>(input.width), static_cast<int32_t>(input.height), 1};
                        blit.dstSubresource = {VK_IMAGE_ASPECT_COLOR_BIT, 0, 0, 1};
                        blit.dstOffsets[1] = blit.srcOffsets[1];
                        vkCmdBlitImage(cmd, input.image, VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL, dst,
                            VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, 1, &blit, VK_FILTER_NEAREST);
                };

                // (a) History ring update (SPEC 5.2): seed every slot on the
                // first frame and after input-size changes; otherwise advance
                // one slot. The slot was already advanced before descriptor
                // writes, so OriginalHistoryN binds frame T-N.
                if (impl.historyDepth > 0 && !impl.history.slots.empty()) {
                        promoteInput();
                        const size_t count = impl.history.slots.size();
                        if (seedRing) {
                                for (size_t s = 0; s < count; ++s) {
                                        BarrierMipLevel(cmd, impl.history.slots[s].image, 0, 1,
                                            VK_IMAGE_LAYOUT_UNDEFINED, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
                                            VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT, VK_PIPELINE_STAGE_TRANSFER_BIT, 0,
                                            VK_ACCESS_TRANSFER_WRITE_BIT);
                                        blitInputTo(impl.history.slots[s].image);
                                        BarrierMipLevel(cmd, impl.history.slots[s].image, 0, 1,
                                            VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
                                            VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL, VK_PIPELINE_STAGE_TRANSFER_BIT,
                                            VK_PIPELINE_STAGE_ALL_COMMANDS_BIT, VK_ACCESS_TRANSFER_WRITE_BIT,
                                            VK_ACCESS_SHADER_READ_BIT);
                                }
                        } else {
                                const size_t current = impl.history.slot;
                                BarrierMipLevel(cmd, impl.history.slots[current].image, 0, 1,
                                    VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
                                    VK_PIPELINE_STAGE_ALL_COMMANDS_BIT, VK_PIPELINE_STAGE_TRANSFER_BIT,
                                    VK_ACCESS_SHADER_READ_BIT, VK_ACCESS_TRANSFER_WRITE_BIT);
                                blitInputTo(impl.history.slots[current].image);
                                BarrierMipLevel(cmd, impl.history.slots[current].image, 0, 1,
                                    VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL,
                                    VK_PIPELINE_STAGE_TRANSFER_BIT, VK_PIPELINE_STAGE_ALL_COMMANDS_BIT,
                                    VK_ACCESS_TRANSFER_WRITE_BIT, VK_ACCESS_SHADER_READ_BIT);
                        }
                }

                // (b) Pass-0 mipmap_input: refill the mip-capable input copy (F1).
                if (impl.useSourceMipCopy && impl.sourceMipCopy.image != VK_NULL_HANDLE) {
                        promoteInput();
                        BarrierMipLevel(cmd, impl.sourceMipCopy.image, 0, 1, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL,
                            VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, VK_PIPELINE_STAGE_ALL_COMMANDS_BIT,
                            VK_PIPELINE_STAGE_TRANSFER_BIT, VK_ACCESS_SHADER_READ_BIT, VK_ACCESS_TRANSFER_WRITE_BIT);
                        blitInputTo(impl.sourceMipCopy.image);
                        RecordMipChain(cmd, impl.sourceMipCopy.image,
                            MipLevelsForExtent(impl.srcMipWidth, impl.srcMipHeight), impl.srcMipWidth,
                            impl.srcMipHeight, true);
                }
                restoreInput();

                // (c) Passes. A mip-reading pass gets its input pass's mip
                // chain refilled right before it binds (F1); double-buffered
                // passes write targets[writeIndex] while feedback readers
                // sample targets[1 - writeIndex] (SPEC 5.3).
                for (size_t i = 0; i < impl.passes.size(); ++i) {
                        Chain::Impl::PassRuntime &pass = impl.passes[i];
                        if (i > 0 && pass.def.mipmapInput) {
                                Chain::Impl::PassRuntime &src = impl.passes[i - 1];
                                RecordMipChain(cmd, src.targets[src.writeIndex].img.image, src.mipLevels,
                                    src.allocatedWidth, src.allocatedHeight, false);
                        }
                        const bool isLast = i + 1 == impl.passes.size();
                        const bool rendersToHost = isLast && !pass.feedbackRead && !presentNeeded;
                        const uint32_t outW = rendersToHost ? impl.targetWidth : pass.outputWidth;
                        const uint32_t outH = rendersToHost ? impl.targetHeight : pass.outputHeight;
                        const VkPipeline passPipeline = framePipelines[i];

                        VkRenderPassBeginInfo beginInfo{};
                        beginInfo.sType = VK_STRUCTURE_TYPE_RENDER_PASS_BEGIN_INFO;
                        beginInfo.renderPass = impl.GetRenderPass(rendersToHost ? impl.targetFormat : pass.targetFormat);
                        beginInfo.framebuffer = rendersToHost ? outputFramebuffer
                                                              : pass.targets[pass.writeIndex].framebuffer;
                        beginInfo.renderArea = {{0, 0}, {outW, outH}};
                        std::array<float, 4> black = {0.F, 0.F, 0.F, 1.F};
                        beginInfo.clearValueCount = 1;
                        beginInfo.pClearValues = reinterpret_cast<const VkClearValue *>(&black);
                        vkCmdBeginRenderPass(cmd, &beginInfo, VK_SUBPASS_CONTENTS_INLINE);

                        VkViewport viewport{0.F, 0.F, static_cast<float>(outW), static_cast<float>(outH), 0.F, 1.F};
                        VkRect2D scissor{{0, 0}, {outW, outH}};
                        vkCmdSetViewport(cmd, 0, 1, &viewport);
                        vkCmdSetScissor(cmd, 0, 1, &scissor);

                        vkCmdBindPipeline(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, passPipeline);
                        std::array<VkDescriptorSet, 4> sets = {impl.emptySet, pass.set1, pass.set2, pass.set3};
                        vkCmdBindDescriptorSets(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, pass.pipelineLayout, 0,
                            static_cast<uint32_t>(sets.size()), sets.data(), 0, nullptr);
                        const VkDeviceSize offset = 0;
                        vkCmdBindVertexBuffers(cmd, 0, 1, &impl.vertexBuffer, &offset);
                        vkCmdDraw(cmd, 4, 1, 0, 0);
                        vkCmdEndRenderPass(cmd);
                }

                // (d) Present draw (M4): letterbox/rotation composite of the
                // final pass output into the output target; margins come from
                // the render pass clear (opaque black, like the reference
                // host's present blit).
                if (presentNeeded) {
                        const uint32_t outW = impl.targetWidth;
                        const uint32_t outH = impl.targetHeight;
                        VkRenderPassBeginInfo beginInfo{};
                        beginInfo.sType = VK_STRUCTURE_TYPE_RENDER_PASS_BEGIN_INFO;
                        beginInfo.renderPass = presentRenderPass;
                        beginInfo.framebuffer = presentFramebuffer;
                        beginInfo.renderArea = {{0, 0}, {outW, outH}};
                        std::array<float, 4> black = {0.F, 0.F, 0.F, 1.F};
                        beginInfo.clearValueCount = 1;
                        beginInfo.pClearValues = reinterpret_cast<const VkClearValue *>(&black);
                        vkCmdBeginRenderPass(cmd, &beginInfo, VK_SUBPASS_CONTENTS_INLINE);

                        VkViewport viewport{0.F, 0.F, static_cast<float>(outW), static_cast<float>(outH), 0.F, 1.F};
                        VkRect2D scissor{{0, 0}, {outW, outH}};
                        vkCmdSetViewport(cmd, 0, 1, &viewport);
                        vkCmdSetScissor(cmd, 0, 1, &scissor);

                        vkCmdBindPipeline(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, presentPipeline);
                        vkCmdBindDescriptorSets(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, impl.presentPipelineLayout, 0,
                            1, &impl.presentSet, 0, nullptr);
                        const VkDeviceSize presentOffset = 0;
                        vkCmdBindVertexBuffers(cmd, 0, 1, &impl.presentVertexBuffer, &presentOffset);
                        vkCmdDraw(cmd, 4, 1, 0, 0);
                        vkCmdEndRenderPass(cmd);
                }

                // (e) Final-pass feedback resolve: the last pass rendered into
                // its own double-buffered target and the chain output matches
                // the target 1:1 (direct offscreen path); copy it into the host
                // image (nearest — sizes are equal).
                if (feedbackDirectResolve) {
                        Chain::Impl::PassRuntime &lastPass = impl.passes.back();
                        const VkImage srcImage = lastPass.targets[lastPass.writeIndex].img.image;
                        BarrierMipLevel(cmd, srcImage, 0, 1, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL,
                            VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL, VK_PIPELINE_STAGE_ALL_COMMANDS_BIT,
                            VK_PIPELINE_STAGE_TRANSFER_BIT, VK_ACCESS_SHADER_READ_BIT, VK_ACCESS_TRANSFER_READ_BIT);
                        BarrierMipLevel(cmd, offscreen->image, 0, 1, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL,
                            VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, VK_PIPELINE_STAGE_ALL_COMMANDS_BIT,
                            VK_PIPELINE_STAGE_TRANSFER_BIT, VK_ACCESS_SHADER_READ_BIT, VK_ACCESS_TRANSFER_WRITE_BIT);
                        VkImageBlit resolve{};
                        resolve.srcSubresource = {VK_IMAGE_ASPECT_COLOR_BIT, 0, 0, 1};
                        resolve.srcOffsets[1] = {static_cast<int32_t>(impl.targetWidth),
                            static_cast<int32_t>(impl.targetHeight), 1};
                        resolve.dstSubresource = {VK_IMAGE_ASPECT_COLOR_BIT, 0, 0, 1};
                        resolve.dstOffsets[1] = resolve.srcOffsets[1];
                        vkCmdBlitImage(cmd, srcImage, VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL, offscreen->image,
                            VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, 1, &resolve, VK_FILTER_NEAREST);
                        BarrierMipLevel(cmd, srcImage, 0, 1, VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL,
                            VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL, VK_PIPELINE_STAGE_TRANSFER_BIT,
                            VK_PIPELINE_STAGE_ALL_COMMANDS_BIT, VK_ACCESS_TRANSFER_READ_BIT,
                            VK_ACCESS_SHADER_READ_BIT);
                        BarrierMipLevel(cmd, offscreen->image, 0, 1, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
                            VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL, VK_PIPELINE_STAGE_TRANSFER_BIT,
                            VK_PIPELINE_STAGE_ALL_COMMANDS_BIT, VK_ACCESS_TRANSFER_WRITE_BIT,
                            VK_ACCESS_SHADER_READ_BIT);
                }
        },
            swapchainTarget ? swap->acquireSemaphore : VK_NULL_HANDLE,
            VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT);
        if (!recorded)
                return std::unexpected(recorded.error());

        // Double-buffered passes flip AFTER the frame (SPEC 5.3): this
        // frame's write buffer becomes the next frame's feedback source.
        for (Chain::Impl::PassRuntime &pass : impl.passes)
                if (pass.targetCount == 2)
                        pass.writeIndex ^= 1u;

        return {};
}

} // namespace slv
