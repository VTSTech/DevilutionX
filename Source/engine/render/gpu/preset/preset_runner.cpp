#include "engine/render/gpu/preset/preset_runner.hpp"

#ifdef USE_GPU_SCALER
#ifndef USE_SDL1

#include <SDL3/SDL_surface.h>
#include <SDL3_image/SDL_image.h>

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <memory>
#include <optional>
#include <regex>
#include <sstream>
#include <string>
#include <string_view>
#include <unordered_map>
#include <unordered_set>

#include "engine/render/gpu/gpu_scaler.hpp"
#include "engine/render/gpu/preset/preset_parser.hpp"
#include "engine/render/gpu/preset/slang_compile.hpp"
#include "engine/render/gpu/preset/slang_translate.hpp"
#include "utils/log.hpp"
#include "utils/parse_int.hpp"
#include "utils/str_cat.hpp"

namespace {
/** @brief ASCII case-insensitive string equality for preset/shader names. */
bool CaseInsensitiveEqual(const std::string &a, const std::string &b)
{
        if (a.size() != b.size())
                return false;
        for (size_t i = 0; i < a.size(); ++i) {
                if (std::tolower(static_cast<unsigned char>(a[i])) != std::tolower(static_cast<unsigned char>(b[i])))
                        return false;
        }
        return true;
}

/** @brief F6: the aspect-corrected viewport - the game image rect fitted
 *         inside the swapchain (letterbox fit, half-up rounding). This is
 *         what RetroArch resolves `viewport` scale (and FinalViewportSize)
 *         against. Degenerate sizes fall back to the raw swapchain. */
void DeriveViewportSize(Uint32 swapchainWidth, Uint32 swapchainHeight, int gameWidth, int gameHeight,
    Uint32 &outViewportWidth, Uint32 &outViewportHeight)
{
        outViewportWidth = swapchainWidth;
        outViewportHeight = swapchainHeight;
        if (swapchainWidth == 0 || swapchainHeight == 0 || gameWidth <= 0 || gameHeight <= 0)
                return;
        const float gameAspect = static_cast<float>(gameWidth) / static_cast<float>(gameHeight);
        if (gameAspect > static_cast<float>(swapchainWidth) / static_cast<float>(swapchainHeight))
                outViewportHeight = static_cast<Uint32>(std::max(1.0F,
                    std::floor(static_cast<float>(swapchainWidth) / gameAspect + 0.5F)));
        else
                outViewportWidth = static_cast<Uint32>(std::max(1.0F,
                    std::floor(static_cast<float>(swapchainHeight) * gameAspect + 0.5F)));
}

/** @brief Context for a failed IMG_Load: resolved path, file size, and
 *         an explicit hint when the file is a git-lfs pointer instead of
 *         image data (shader packs host their LUT PNGs in git-lfs, so
 *         archive downloads and non-LFS checkouts leave small text files
 *         in their place). */
std::string DescribeImageLoadFailure(const std::string &path)
{
        std::string result = path;
        std::error_code ec;
        const std::uintmax_t size = std::filesystem::file_size(path, ec);
        if (ec) {
                result += " (file missing or unreadable)";
        } else {
                result += " (";
                result += std::to_string(size);
                result += " bytes)";
                std::ifstream in(path, std::ios::binary);
                if (in) {
                        char head[32] {};
                        in.read(head, static_cast<std::streamsize>(sizeof(head)));
                        if (in.gcount() >= 23 && std::memcmp(head, "version https://git-lfs", 23) == 0) {
                                result += " [git-lfs POINTER file, not image data: run 'git lfs install && git lfs pull'";
                                result += " in the shaders repository, or re-download the pack with real binary files]";
                        }
                }
        }
        return result;
}
} // namespace

namespace devilution {
namespace gpu_scaler {
namespace preset {
namespace {

SDL_GPUTextureFormat FormatForPass(const PassDef &pass, std::string &warning)
{
        std::string formatName = pass.pragmaFormat;
        if (formatName.empty()) {
                if (pass.srgbFramebuffer)
                        formatName = "R8G8B8A8_SRGB";
                else if (pass.floatFramebuffer)
                        formatName = "R16G16B16A16_SFLOAT";
                else
                        return SDL_GPU_TEXTUREFORMAT_R8G8B8A8_UNORM;
        }

        // #pragma format mapping. Formats the SDL GPU backend guarantees for
        // color targets map directly; the only token left unmapped is
        // A2B10G10R10_UINT_PACK32 (no SDL GPU UINT form of the packed 10-bit
        // layout), which falls back to RGBA8 with a warning instead of
        // failing the whole preset.
        static const std::unordered_map<std::string, SDL_GPUTextureFormat> kFormats = {
                { "R8_UNORM", SDL_GPU_TEXTUREFORMAT_R8_UNORM },
                { "R8G8_UNORM", SDL_GPU_TEXTUREFORMAT_R8G8_UNORM },
                { "R8G8B8A8_UNORM", SDL_GPU_TEXTUREFORMAT_R8G8B8A8_UNORM },
                { "R8G8B8A8_SRGB", SDL_GPU_TEXTUREFORMAT_R8G8B8A8_UNORM_SRGB },
                { "R16_SFLOAT", SDL_GPU_TEXTUREFORMAT_R16_FLOAT },
                { "R16G16_SFLOAT", SDL_GPU_TEXTUREFORMAT_R16G16_FLOAT },
                { "R16G16B16A16_SFLOAT", SDL_GPU_TEXTUREFORMAT_R16G16B16A16_FLOAT },
                { "R32_SFLOAT", SDL_GPU_TEXTUREFORMAT_R32_FLOAT },
                { "R32G32_SFLOAT", SDL_GPU_TEXTUREFORMAT_R32G32_FLOAT },
                { "R32G32B32A32_SFLOAT", SDL_GPU_TEXTUREFORMAT_R32G32B32A32_FLOAT },
                { "R8_UINT", SDL_GPU_TEXTUREFORMAT_R8_UINT },
                { "R8G8B8A8_UINT", SDL_GPU_TEXTUREFORMAT_R8G8B8A8_UINT },
                { "R16_UINT", SDL_GPU_TEXTUREFORMAT_R16_UINT },
                { "R16G16B16A16_UINT", SDL_GPU_TEXTUREFORMAT_R16G16B16A16_UINT },
                { "R32_UINT", SDL_GPU_TEXTUREFORMAT_R32_UINT },
                { "R32G32B32A32_UINT", SDL_GPU_TEXTUREFORMAT_R32G32B32A32_UINT },
                { "R8_SINT", SDL_GPU_TEXTUREFORMAT_R8_INT },
                { "R8G8B8A8_SINT", SDL_GPU_TEXTUREFORMAT_R8G8B8A8_INT },
                { "R16_SINT", SDL_GPU_TEXTUREFORMAT_R16_INT },
                { "R16G16B16A16_SINT", SDL_GPU_TEXTUREFORMAT_R16G16B16A16_INT },
                { "R32_SINT", SDL_GPU_TEXTUREFORMAT_R32_INT },
                { "R32G32B32A32_SINT", SDL_GPU_TEXTUREFORMAT_R32G32B32A32_INT },
                { "R8G8_UINT", SDL_GPU_TEXTUREFORMAT_R8G8_UINT },
                { "R8G8_SINT", SDL_GPU_TEXTUREFORMAT_R8G8_INT },
                { "R16G16_UINT", SDL_GPU_TEXTUREFORMAT_R16G16_UINT },
                { "R16G16_SINT", SDL_GPU_TEXTUREFORMAT_R16G16_INT },
                { "R32G32_UINT", SDL_GPU_TEXTUREFORMAT_R32G32_UINT },
                { "R32G32_SINT", SDL_GPU_TEXTUREFORMAT_R32G32_INT },
                // Vulkan's packed 10-bit UNORM; SDL names the same layout
                // R10G10B10A2_UNORM and its Vulkan backend maps it back to
                // VK_FORMAT_A2B10G10R10_UNORM_PACK32 (18 corpus shaders request
                // it, e.g. crt-effects curvature). The _UINT_PACK32 variant stays
                // unsupported: SDL GPU has no UINT form of the packed 10-bit layout.
                { "A2B10G10R10_UNORM_PACK32", SDL_GPU_TEXTUREFORMAT_R10G10B10A2_UNORM },
        };
        const auto it = kFormats.find(formatName);
        if (it == kFormats.end()) {
                warning = StrCat("unsupported #pragma format ", formatName, "; falling back to R8G8B8A8_UNORM");
                return SDL_GPU_TEXTUREFORMAT_R8G8B8A8_UNORM;
        }
        return it->second;
}

Uint32 MipLevelsFor(Uint32 width, Uint32 height)
{
        Uint32 levels = 1;
        while (width > 1 || height > 1) {
                width = std::max<Uint32>(1, width / 2);
                height = std::max<Uint32>(1, height / 2);
                ++levels;
        }
        return levels;
}

/** @brief Parses the numeric suffix of a builtin texture binding name
 *         ("OriginalHistory#", "PassOutput#", "PassFeedback#", "User#")
 *         without throwing (ROB-01).
 *
 *         Binding names come straight from preset files: a bare prefix
 *         ("User"), a non-numeric suffix ("PassOutputx" via an alias) or a
 *         20-digit index all previously reached std::stoi and terminated the
 *         process through the uncaught std::invalid_argument/
 *         std::out_of_range. Returns nullopt for missing, malformed or
 *         out-of-range suffixes; every call site falls back to a valid
 *         binding instead of throwing. */
std::optional<int> ParseTextureIndex(const std::string &name, size_t prefixLen)
{
        if (name.size() <= prefixLen)
                return std::nullopt;
        const ParseIntResult<int> parsed = ParseInt<int>(std::string_view { name.data() + prefixLen, name.size() - prefixLen });
        if (!parsed.has_value())
                return std::nullopt;
        return *parsed;
}

} // namespace

struct UniformBlockRuntime {
        UniformBlock block;
};

struct PassRuntime {
        PassDef def;
        SlangTranslation translation;
        SDL_GPUShader *vertexShader = nullptr;
        SDL_GPUShader *fragmentShader = nullptr;
        SDL_GPUGraphicsPipeline *pipeline = nullptr;
        SDL_GPUTextureFormat targetFormat = SDL_GPU_TEXTUREFORMAT_INVALID;
        SDL_GPUTexture *outputTex[2] = {};
        Uint32 outputWidth = 0;
        Uint32 outputHeight = 0;
        bool doubleBuffered = false;
        bool needsFeedback = false; // Some pass reads this pass's previous output.
        int curBuffer = 0;
        bool usesMvp = false;
        bool usesFrameCount = false;
        bool usesFrameDirection = false;
        bool usesFrameTime = false;
        bool usesPassIndex = false;
};

struct LutRuntime {
        TextureDef def;
        SDL_GPUTexture *texture = nullptr;
        Uint32 width = 0;
        Uint32 height = 0;
};

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

Runner::~Runner()
{
        Unload();
}

bool Runner::Load(SDL_GPUDevice *device, std::string_view backend, const Preset &preset, std::string &error)
{
        Unload();
        device_ = device;
        preset_ = preset;

        if (backend != "vulkan") {
                error = StrCat("runtime shader presets are not supported on the ", backend, " backend");
                return false;
        }

        // 1. Pre-scan the pass identifiers (#pragma name) so shaders can
        //    reference other passes' outputs by name during translation.
        std::unordered_map<int, std::string> pragmaNames;
        for (size_t i = 0; i < preset_.passes.size(); ++i) {
                const std::string path = ResolvePresetFilePath(preset_.dir, preset_.passes[i].shaderPath);
                std::ifstream stream(path, std::ios::binary);
                if (!stream) {
                        error = StrCat("cannot open shader file: ", path);
                        return false;
                }
                std::ostringstream ss;
                ss << stream.rdbuf();
                static const std::regex nameRe(R"(^\s*#pragma\s+name\s+(\w+)\s*$)");
                std::istringstream lines(ss.str());
                std::string line;
                while (std::getline(lines, line)) {
                        std::smatch match;
                        if (std::regex_match(line, match, nameRe)) {
                                pragmaNames[static_cast<int>(i)] = match[1].str();
                                break;
                        }
                }
        }

        // 2. Known texture names: pass aliases, pass identifiers and LUT names.
        //    The translator's deduction looks names up lowercased, so every name
        //    goes in both original and lowercased form. Aliases and #pragma
        //    names used to be inserted in original case only, so any mixed-case
        //    reference failed deduction ("unknown texture reference 'Pass0'" -
        //    field log: motion_interpolation, guest advanced ntsc PrePass0).
        std::unordered_set<std::string> knownNames;
        auto insertKnownName = [&knownNames](const std::string &name) {
                knownNames.insert(name);
                std::string lower;
                lower.reserve(name.size());
                for (const char c : name)
                        lower += static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
                knownNames.insert(lower);
        };
        for (const PassDef &pass : preset_.passes) {
                if (!pass.alias.empty())
                        insertKnownName(pass.alias);
        }
        for (const auto &[_, name] : pragmaNames)
                insertKnownName(name);
        for (const TextureDef &tex : preset_.textures)
                insertKnownName(tex.name);

        // 3. Translate + compile every pass.
        std::vector<std::unique_ptr<PassRuntime>> passStorage;
        passStorage.reserve(preset_.passes.size());
        for (size_t i = 0; i < preset_.passes.size(); ++i) {
                PassDef &def = preset_.passes[i];
                auto pass = std::make_unique<PassRuntime>();
                pass->def = def;

                const std::string slangPath = ResolvePresetFilePath(preset_.dir, def.shaderPath);
                std::vector<ParamInfo> presetParams;
                for (const ParamOverride &override : preset_.parameters) {
                        ParamInfo info;
                        info.name = override.name;
                        info.defaultValue = override.value;
                        presetParams.push_back(info);
                }
                if (!TranslateSlang(slangPath, knownNames, static_cast<int>(i), static_cast<int>(preset_.passes.size()),
                        pass->translation, presetParams)) {
                        error = StrCat("translating '", def.shaderPath, "' (pass ", i, "): ", pass->translation.error);
                        return false;
                }
                for (const std::string &w : pass->translation.warnings)
                        LogWarn("GPU scaler preset (pass {}): {}", i, w);
                // Field round 3: propagate the shader's "#pragma format" token into the
                // pass definition. It was only ever captured into the translation
                // result and never read back — FormatForPass checks
                // PassDef::pragmaFormat, which stayed empty since the mapping table
                // landed (ae938cfb1), so the spec-4.2 override silently did nothing.
                def.pragmaFormat = pass->translation.pragmaFormat;
                PassSpirv spirv;
                if (!CompilePass(pass->translation, slangPath, ShaderCacheDir(), spirv, error)) {
                        error = StrCat("compiling '", def.shaderPath, "' (pass ", i, "): ", error);
                        return false;
                }

                // Semantics used by either stage (blocks are reflected per stage).
                for (const StageTranslation *stage : { &pass->translation.vertex, &pass->translation.fragment }) {
                        pass->usesMvp |= stage->usesMvp;
                        pass->usesFrameCount |= stage->usesFrameCount;
                        pass->usesFrameDirection |= stage->usesFrameDirection;
                        pass->usesFrameTime |= stage->usesFrameTime;
                        pass->usesPassIndex |= stage->usesPassIndex;
                }

                // Target format.
                std::string formatWarning;
                pass->targetFormat = FormatForPass(def, formatWarning);
                if (!formatWarning.empty())
                        LogWarn("GPU scaler preset (pass {}): {}", i, formatWarning);

                // Create the GPU shaders for the selected backend.
                auto createShader = [&](bool vertex) -> SDL_GPUShader * {
                        const StageTranslation &stage = vertex ? pass->translation.vertex : pass->translation.fragment;
                        SDL_GPUShaderCreateInfo info {};
                        info.stage = vertex ? SDL_GPU_SHADERSTAGE_VERTEX : SDL_GPU_SHADERSTAGE_FRAGMENT;
                        info.entrypoint = "main";
                        info.num_samplers = static_cast<Uint32>(stage.textures.size());
                        info.num_uniform_buffers = static_cast<Uint32>(stage.uniformBlocks.size());
                        info.format = SDL_GPU_SHADERFORMAT_SPIRV;
                        info.code = spirv.vertex.data();
                        info.code_size = spirv.vertex.size();
                        if (!vertex) {
                                info.code = spirv.fragment.data();
                                info.code_size = spirv.fragment.size();
                        }
                        SDL_GPUShader *shader = SDL_CreateGPUShader(device_, &info);
                        if (shader == nullptr) {
                                error = StrCat("SDL_CreateGPUShader failed for '", def.shaderPath, "' (",
                                    vertex ? "vertex" : "fragment", ")");
                                return nullptr;
                        }
                        return shader;
                };
                pass->vertexShader = createShader(/*vertex=*/true);
                if (pass->vertexShader == nullptr)
                        return false;
                pass->fragmentShader = createShader(/*vertex=*/false);
                if (pass->fragmentShader == nullptr)
                        return false;

                passStorage.push_back(std::move(pass));
        }

        // 4. Pipelines (one per pass; the target format is fixed per pass).
        for (auto &pass : passStorage) {
                SDL_GPUColorTargetDescription targetDesc {};
                targetDesc.format = pass->targetFormat;
                SDL_GPUVertexBufferDescription bufferDesc {};
                bufferDesc.slot = 0;
                bufferDesc.pitch = 24; // float4 position + float2 uv
                bufferDesc.input_rate = SDL_GPU_VERTEXINPUTRATE_VERTEX;
                SDL_GPUVertexAttribute attributes[2] = {};
                attributes[0].location = 0;
                attributes[0].buffer_slot = 0;
                attributes[0].format = SDL_GPU_VERTEXELEMENTFORMAT_FLOAT4;
                attributes[0].offset = 0;
                attributes[1].location = 1;
                attributes[1].buffer_slot = 0;
                attributes[1].format = SDL_GPU_VERTEXELEMENTFORMAT_FLOAT2;
                attributes[1].offset = 16;
                SDL_GPUVertexInputState vertexInput {};
                vertexInput.num_vertex_buffers = 1;
                vertexInput.vertex_buffer_descriptions = &bufferDesc;
                vertexInput.num_vertex_attributes = 2;
                vertexInput.vertex_attributes = attributes;

                SDL_GPUGraphicsPipelineCreateInfo info {};
                info.vertex_shader = pass->vertexShader;
                info.fragment_shader = pass->fragmentShader;
                info.primitive_type = SDL_GPU_PRIMITIVETYPE_TRIANGLELIST;
                info.rasterizer_state.fill_mode = SDL_GPU_FILLMODE_FILL;
                info.rasterizer_state.cull_mode = SDL_GPU_CULLMODE_NONE;
                info.rasterizer_state.front_face = SDL_GPU_FRONTFACE_COUNTER_CLOCKWISE;
                info.multisample_state.sample_count = SDL_GPU_SAMPLECOUNT_1;
                // SDL 3.5: multisample_state.sample_mask is "reserved, must be 0"
                // (SDL_gpu.c release-asserts and rejects the pipeline otherwise on
                // debug-mode devices; llvmpipe rejects it outright). With sample_count
                // 1 the default mask already covers every sample; the old 0xFFFFFFFF
                // form predates the contract change.
                info.depth_stencil_state.compare_op = SDL_GPU_COMPAREOP_ALWAYS;
                info.depth_stencil_state.front_stencil_state.fail_op = SDL_GPU_STENCILOP_KEEP;
                info.depth_stencil_state.front_stencil_state.pass_op = SDL_GPU_STENCILOP_KEEP;
                info.depth_stencil_state.front_stencil_state.depth_fail_op = SDL_GPU_STENCILOP_KEEP;
                info.depth_stencil_state.front_stencil_state.compare_op = SDL_GPU_COMPAREOP_ALWAYS;
                info.depth_stencil_state.back_stencil_state = info.depth_stencil_state.front_stencil_state;
                info.target_info.color_target_descriptions = &targetDesc;
                info.target_info.num_color_targets = 1;
                info.vertex_input_state = vertexInput;
                pass->pipeline = SDL_CreateGPUGraphicsPipeline(device_, &info);
                if (pass->pipeline == nullptr && pass->targetFormat != SDL_GPU_TEXTUREFORMAT_R8G8B8A8_UNORM) {
                        // Field round 3: a per-pass #pragma format can name a format the device
                        // refuses as a color attachment (SDL builds a transient VkRenderPass
                        // with it; single-channel float/int targets are not mandatory
                        // attachment formats in Vulkan — field log: test/format.slangp pass 1
                        // failed with no diagnostics at all). Downgrade to RGBA8 and retry
                        // instead of failing the preset; the target texture follows
                        // pass->targetFormat, so pipeline and target stay consistent.
                        LogWarn("GPU scaler preset (pass {}): color target format unavailable on this device (SDL error: {}); retrying with R8G8B8A8_UNORM",
                            static_cast<int>(&pass - passStorage.data()), SDL_GetError());
                        SDL_ClearError();
                        targetDesc.format = SDL_GPU_TEXTUREFORMAT_R8G8B8A8_UNORM;
                        pass->targetFormat = SDL_GPU_TEXTUREFORMAT_R8G8B8A8_UNORM;
                        pass->pipeline = SDL_CreateGPUGraphicsPipeline(device_, &info);
                }
                if (pass->pipeline == nullptr) {
                        error = StrCat("pipeline creation failed for preset pass ", static_cast<int>(&pass - passStorage.data()),
                            ": ", SDL_GetError());
                        return false;
                }
        }

        // 5. Which passes need double buffering (their previous-frame output is
        //    read through PassFeedback#/NAMEFeedback)?
        for (auto &pass : passStorage) {
                for (const StageTranslation *stage : { &pass->translation.vertex, &pass->translation.fragment }) {
                        for (const TextureRef &ref : stage->textures) {
                                const std::string &name = ref.name;
                                if (name.rfind("PassFeedback", 0) == 0) {
                                        // ROB-01: a malformed suffix ("PassFeedbackx") or a bare
                                        // prefix previously threw out of Load and killed the game.
                                        const std::optional<int> idx = ParseTextureIndex(name, 12);
                                        if (idx && *idx >= 0 && *idx < static_cast<int>(passStorage.size()))
                                                passStorage[static_cast<size_t>(*idx)]->needsFeedback = true;
                                } else if (name.size() > 8 && name.compare(name.size() - 8, 8, "Feedback") == 0) {
                                        const std::string base = name.substr(0, name.size() - 8);
                                        for (size_t j = 0; j < passStorage.size(); ++j) {
                                                const std::string alias = passStorage[j]->def.alias;
                                                const auto it = pragmaNames.find(static_cast<int>(j));
                                                if ((!alias.empty() && CaseInsensitiveEqual(alias, base))
                                                    || (it != pragmaNames.end() && CaseInsensitiveEqual(it->second, base))) {
                                                        passStorage[j]->needsFeedback = true;
                                                }
                                        }
                                }
                        }
                }
        }
        for (auto &pass : passStorage)
                pass->doubleBuffered = pass->needsFeedback;

        // 6. History depth: highest referenced OriginalHistory# index.
        historyDepth_ = 0;
        for (auto &pass : passStorage) {
                for (const StageTranslation *stage : { &pass->translation.vertex, &pass->translation.fragment }) {
                        for (const TextureRef &ref : stage->textures) {
                                if (ref.name.rfind("OriginalHistory", 0) == 0) {
                                        // ROB-01: same non-throwing suffix parse as the run path.
                                        const std::optional<int> idx = ParseTextureIndex(ref.name, 15);
                                        if (idx)
                                                historyDepth_ = std::max(historyDepth_, *idx);
                                }
                        }
                }
        }

        // 7. Lookup textures.
        luts_.reserve(preset_.textures.size());
        for (const TextureDef &texDef : preset_.textures) {
                auto lut = std::make_unique<LutRuntime>();
                lut->def = texDef;
                if (!LoadLut(*lut, error)) {
                        error = StrCat("loading lookup texture '", texDef.name, "': ", error);
                        return false;
                }
                luts_.push_back(lut.release());
        }

        // 8. Vertex buffer.
        if (!EnsureVertexBuffer()) {
                error = "vertex buffer creation failed";
                return false;
        }

        // Transfer ownership.
        for (auto &pass : passStorage)
                passes_.push_back(pass.release());

        // Orientation/binding diagnostics for remote field testing: presets
        // whose vertex stage ignores MVP compute positions in an unknown
        // convention and may render rotated; sampler names surface any
        // unresolved texture references.
        for (size_t pi = 0; pi < passes_.size(); ++pi) {
                const PassRuntime &p = *passes_[pi];
                std::string textureNames;
                for (const auto &tex : p.translation.fragment.textures) {
                        if (!textureNames.empty())
                                textureNames += ", ";
                        textureNames += tex.name;
                }
                SDL_LogInfo(SDL_LOG_CATEGORY_APPLICATION,
                    "GPU scaler preset: pass %u '%s' alias='%s' vertexMvp=%s flipY=%s mipIn=%s"
                    " src=%u:%08x merged=%u:%08x fragFetch=%d/%d textures=[%s]",
                    static_cast<unsigned>(pi), p.def.shaderPath.c_str(), p.def.alias.c_str(),
                    p.translation.vertex.usesMvp ? "yes" : "no",
                    (p.translation.vertex.usesMvp && pi + 1 < passes_.size()) ? "yes" : "no",
                    p.def.mipmapInput ? "yes" : "no",
                    static_cast<unsigned>(p.translation.sourceSize), p.translation.sourceHash,
                    static_cast<unsigned>(p.translation.mergedSize), p.translation.mergedHash,
                    p.translation.fragment.texelFetchCount, p.translation.fragment.textureGatherCount,
                    textureNames.c_str());
        }
        return true;
}

bool Runner::EnsureSamplers()
{
        SDL_GPUSamplerCreateInfo base {};
        base.min_lod = 0.0F;
        base.max_lod = 1000.0F;
        base.max_anisotropy = 1.0F;
        base.address_mode_u = SDL_GPU_SAMPLERADDRESSMODE_CLAMP_TO_EDGE;
        base.address_mode_v = SDL_GPU_SAMPLERADDRESSMODE_CLAMP_TO_EDGE;
        base.address_mode_w = SDL_GPU_SAMPLERADDRESSMODE_CLAMP_TO_EDGE;
        base.mipmap_mode = SDL_GPU_SAMPLERMIPMAPMODE_NEAREST;
        const SDL_GPUFilter kFilters[2] = { SDL_GPU_FILTER_NEAREST, SDL_GPU_FILTER_LINEAR };
        for (int filter = 0; filter < 2; ++filter) {
                for (int wrap = 0; wrap < 3; ++wrap) {
                        for (int mip = 0; mip < 2; ++mip) {
                                if (samplers_[filter][wrap][mip] != nullptr)
                                        continue;
                                SDL_GPUSamplerCreateInfo info = base;
                                info.min_filter = kFilters[filter];
                                info.mag_filter = kFilters[filter];
                                switch (wrap) {
                                case 0: info.address_mode_u = info.address_mode_v = info.address_mode_w = SDL_GPU_SAMPLERADDRESSMODE_CLAMP_TO_EDGE; break;
                                case 1: info.address_mode_u = info.address_mode_v = info.address_mode_w = SDL_GPU_SAMPLERADDRESSMODE_REPEAT; break;
                                default: info.address_mode_u = info.address_mode_v = info.address_mode_w = SDL_GPU_SAMPLERADDRESSMODE_MIRRORED_REPEAT; break;
                                }
                                info.mipmap_mode = mip == 1 ? SDL_GPU_SAMPLERMIPMAPMODE_LINEAR : SDL_GPU_SAMPLERMIPMAPMODE_NEAREST;
                                if (mip == 1) {
                                        info.min_lod = 0.0F;
                                        info.max_lod = 1000.0F;
                                }
                                samplers_[filter][wrap][mip] = SDL_CreateGPUSampler(device_, &info);
                                if (samplers_[filter][wrap][mip] == nullptr)
                                        return false;
                        }
                }
        }
        return true;
}

SDL_GPUSampler *Runner::SamplerFor(FilterMode filter, WrapMode wrap, bool mipmapped)
{
        const int filterIdx = filter == FilterMode::Linear ? 1 : 0;
        int wrapIdx = 0;
        switch (wrap) {
        case WrapMode::Repeat: wrapIdx = 1; break;
        case WrapMode::MirroredRepeat: wrapIdx = 2; break;
        case WrapMode::ClampToEdge: wrapIdx = 0; break;
        }
        return samplers_[filterIdx][wrapIdx][mipmapped ? 1 : 0];
}

bool Runner::EnsureVertexBuffer()
{
        if (vertexBuffer_ != nullptr)
                return true;
        // Full-screen quad, triangle strip: bl, br, tl, tr.
        static constexpr float kQuad[6][4 + 2] = {
                { -1.0F, -1.0F, 0.0F, 1.0F, 0.0F, 0.0F },
                { 1.0F, -1.0F, 0.0F, 1.0F, 1.0F, 0.0F },
                { -1.0F, 1.0F, 0.0F, 1.0F, 0.0F, 1.0F },
                { 1.0F, -1.0F, 0.0F, 1.0F, 1.0F, 0.0F },
                { 1.0F, 1.0F, 0.0F, 1.0F, 1.0F, 1.0F },
                { -1.0F, 1.0F, 0.0F, 1.0F, 0.0F, 1.0F },
        };
        SDL_GPUBufferCreateInfo info {};
        info.usage = SDL_GPU_BUFFERUSAGE_VERTEX;
        info.size = sizeof(kQuad);
        vertexBuffer_ = SDL_CreateGPUBuffer(device_, &info);
        if (vertexBuffer_ == nullptr)
                return false;
        SDL_GPUTransferBufferCreateInfo transferInfo {};
        transferInfo.usage = SDL_GPU_TRANSFERBUFFERUSAGE_UPLOAD;
        transferInfo.size = sizeof(kQuad);
        SDL_GPUTransferBuffer *transfer = SDL_CreateGPUTransferBuffer(device_, &transferInfo);
        if (transfer == nullptr)
                return false;
        auto *data = static_cast<uint8_t *>(SDL_MapGPUTransferBuffer(device_, transfer, false));
        std::memcpy(data, kQuad, sizeof(kQuad));
        SDL_UnmapGPUTransferBuffer(device_, transfer);
        SDL_GPUCommandBuffer *cmd = SDL_AcquireGPUCommandBuffer(device_);
        if (cmd == nullptr) {
                SDL_ReleaseGPUTransferBuffer(device_, transfer);
                return false;
        }
        SDL_GPUCopyPass *copyPass = SDL_BeginGPUCopyPass(cmd);
        SDL_GPUTransferBufferLocation src {};
        src.transfer_buffer = transfer;
        src.offset = 0;
        SDL_GPUBufferRegion dst {};
        dst.buffer = vertexBuffer_;
        dst.offset = 0;
        dst.size = sizeof(kQuad);
        SDL_UploadToGPUBuffer(copyPass, &src, &dst, false);
        SDL_EndGPUCopyPass(copyPass);
        SDL_SubmitGPUCommandBuffer(cmd);
        SDL_ReleaseGPUTransferBuffer(device_, transfer);
        return true;
}

bool Runner::EnsureTargets(int gameWidth, int gameHeight, Uint32 swapchainWidth, Uint32 swapchainHeight)
{
        if (targetGameWidth_ == gameWidth && targetGameHeight_ == gameHeight
            && targetSwapchainWidth_ == swapchainWidth && targetSwapchainHeight_ == swapchainHeight
            && !passes_.empty() && passes_[0]->outputTex[0] != nullptr)
                return true;
        ReleaseTargets();
        targetGameWidth_ = gameWidth;
        targetGameHeight_ = gameHeight;
        targetSwapchainWidth_ = swapchainWidth;
        targetSwapchainHeight_ = swapchainHeight;

        // Resolve pass target sizes following the libretro scale rules.
        float prevW = static_cast<float>(gameWidth);
        float prevH = static_cast<float>(gameHeight);
        // F6: derive the aspect-corrected viewport once per target resize - the
        // game image rect fitted inside the swapchain (letterbox fit, half-up
        // rounding). RetroArch resolves `viewport` scale against this rect, and
        // the presentation blit letterboxes the chain output onto it, so
        // viewport-scaled final passes land exactly on the presented game area.
        DeriveViewportSize(swapchainWidth, swapchainHeight, gameWidth, gameHeight,
            targetViewportWidth_, targetViewportHeight_);
        for (PassRuntime *pass : passes_) {
                auto resolve = [](const ScaleRule &rule, float inputSize, float viewportSize) -> float {
                        switch (rule.type) {
                        case ScaleType::Source: return inputSize * rule.scale;
                        case ScaleType::Viewport: return viewportSize * rule.scale;
                        case ScaleType::Absolute: return rule.scale;
                        }
                        return inputSize;
                };
                const float viewportW = static_cast<float>(targetViewportWidth_);
                const float viewportH = static_cast<float>(targetViewportHeight_);
                const float outW = resolve(pass->def.scaleX, prevW, viewportW);
                const float outH = resolve(pass->def.scaleY, prevH, viewportH);
                pass->outputWidth = static_cast<Uint32>(std::max(1.0F, std::floor(outW + 0.5F)));
                pass->outputHeight = static_cast<Uint32>(std::max(1.0F, std::floor(outH + 0.5F)));
                prevW = static_cast<float>(pass->outputWidth);
                prevH = static_cast<float>(pass->outputHeight);
        }
        // Field diagnostics: resolved pass output sizes (logs once per size change).
        std::string targets;
        for (size_t ti = 0; ti < passes_.size(); ++ti) {
                const PassRuntime &pass = *passes_[ti];
                targets += StrCat(ti == 0 ? "" : " ", ti, "=", pass.outputWidth, "x", pass.outputHeight);
        }
        SDL_LogInfo(SDL_LOG_CATEGORY_APPLICATION,
            "GPU scaler preset: pass targets (game %ux%u, swapchain %ux%u, viewport %ux%u): %s",
            static_cast<unsigned>(gameWidth), static_cast<unsigned>(gameHeight), swapchainWidth, swapchainHeight,
            targetViewportWidth_, targetViewportHeight_, targets.c_str());

        // Pass output textures (double-buffered when a pass reads its own
        // previous-frame output as feedback). F1: an output carries a full mip
        // chain only when the NEXT pass reads it as Source with mipmap_input -
        // only `Source` binds the reading pass's mipmap flag (SLANG_SPEC.md 5.4).
        // Run() refills the chain before that pass every frame.
        std::string mipChainPasses;
        for (size_t pi = 0; pi < passes_.size(); ++pi) {
                PassRuntime *pass = passes_[pi];
                const bool nextPassWantsMips = pi + 1 < passes_.size() && passes_[pi + 1]->def.mipmapInput;
                SDL_GPUTextureCreateInfo info {};
                info.type = SDL_GPU_TEXTURETYPE_2D;
                info.format = pass->targetFormat;
                info.usage = SDL_GPU_TEXTUREUSAGE_SAMPLER | SDL_GPU_TEXTUREUSAGE_COLOR_TARGET;
                info.width = pass->outputWidth;
                info.height = pass->outputHeight;
                info.layer_count_or_depth = 1;
                info.num_levels = nextPassWantsMips ? MipLevelsFor(pass->outputWidth, pass->outputHeight) : 1;
                info.sample_count = SDL_GPU_SAMPLECOUNT_1;
                pass->outputTex[0] = SDL_CreateGPUTexture(device_, &info);
                if (pass->doubleBuffered)
                        pass->outputTex[1] = SDL_CreateGPUTexture(device_, &info);
                if (pass->outputTex[0] == nullptr || (pass->doubleBuffered && pass->outputTex[1] == nullptr)) {
                        // Failure trail: the silent first-frame abort class - without
                        // this line the caller's hard failure carries no reason
                        // (round-8 field evidence).
                        SDL_LogInfo(SDL_LOG_CATEGORY_APPLICATION,
                            "GPU scaler preset: pass %u target texture creation failed (%ux%u, format %u, double-buffered %s): %s",
                            static_cast<unsigned>(pi), pass->outputWidth, pass->outputHeight,
                            static_cast<unsigned>(pass->targetFormat),
                            pass->doubleBuffered ? "yes" : "no", SDL_GetError());
                        return false;
                }
                if (nextPassWantsMips) {
                        if (!mipChainPasses.empty())
                                mipChainPasses += ',';
                        mipChainPasses += std::to_string(pi);
                }
        }

        // F1: pass 0 reads the chain input through its mipmapped sampler when it
        // declares mipmap_input, but the game texture is a single-level
        // scaler-owned surface. Blit it into a runner-owned mip-capable copy
        // (RGBA8, same size - the history ring convention) and refill the chain
        // before pass 0 every frame (Run).
        if (!passes_.empty() && passes_[0]->def.mipmapInput) {
                SDL_GPUTextureCreateInfo info {};
                info.type = SDL_GPU_TEXTURETYPE_2D;
                info.format = SDL_GPU_TEXTUREFORMAT_R8G8B8A8_UNORM;
                info.usage = SDL_GPU_TEXTUREUSAGE_SAMPLER | SDL_GPU_TEXTUREUSAGE_COLOR_TARGET;
                info.width = static_cast<Uint32>(gameWidth);
                info.height = static_cast<Uint32>(gameHeight);
                info.layer_count_or_depth = 1;
                info.num_levels = MipLevelsFor(info.width, info.height);
                info.sample_count = SDL_GPU_SAMPLECOUNT_1;
                mipSourceTex_ = SDL_CreateGPUTexture(device_, &info);
                if (mipSourceTex_ == nullptr) {
                        SDL_LogInfo(SDL_LOG_CATEGORY_APPLICATION,
                            "GPU scaler preset: chain-input mip copy creation failed (%ux%u): %s",
                            info.width, info.height, SDL_GetError());
                        return false;
                }
        }

        // Field diagnostics: which inputs actually carry mip chains (logs once
        // per target resize).
        if (!mipChainPasses.empty() || mipSourceTex_ != nullptr)
                SDL_LogInfo(SDL_LOG_CATEGORY_APPLICATION,
                    "GPU scaler preset: mipmapped inputs (F1): pass outputs with mip chains [%s], chain-input copy %s",
                    mipChainPasses.c_str(), mipSourceTex_ != nullptr ? "on" : "off");

        // Chain-input history ring.
        if (historyDepth_ > 0) {
                // depth+1 slots: the slot at historySlot_ holds the CURRENT
                // frame (blitted at the top of Run) and OriginalHistory1..N
                // need N further slots behind it (OriginalHistoryN = T-N).
                history_.assign(static_cast<size_t>(historyDepth_) + 1, nullptr);
                for (SDL_GPUTexture *&tex : history_) {
                        SDL_GPUTextureCreateInfo info {};
                        info.type = SDL_GPU_TEXTURETYPE_2D;
                        info.format = SDL_GPU_TEXTUREFORMAT_R8G8B8A8_UNORM;
                        info.usage = SDL_GPU_TEXTUREUSAGE_SAMPLER | SDL_GPU_TEXTUREUSAGE_COLOR_TARGET;
                        info.width = static_cast<Uint32>(gameWidth);
                        info.height = static_cast<Uint32>(gameHeight);
                        info.layer_count_or_depth = 1;
                        info.num_levels = 1;
                        info.sample_count = SDL_GPU_SAMPLECOUNT_1;
                        tex = SDL_CreateGPUTexture(device_, &info);
                        if (tex == nullptr) {
                                SDL_LogInfo(SDL_LOG_CATEGORY_APPLICATION,
                                    "GPU scaler preset: history ring texture creation failed (%ux%u): %s",
                                    info.width, info.height, SDL_GetError());
                                return false;
                        }
                }
                historySlot_ = 0;
        }
        return true;
}

void Runner::ReleaseTargets()
{
        for (PassRuntime *pass : passes_) {
                for (SDL_GPUTexture *&tex : pass->outputTex) {
                        if (tex != nullptr) {
                                SDL_ReleaseGPUTexture(device_, tex);
                                tex = nullptr;
                        }
                }
                pass->curBuffer = 0;
        }
        for (SDL_GPUTexture *&tex : history_) {
                if (tex != nullptr) {
                        SDL_ReleaseGPUTexture(device_, tex);
                        tex = nullptr;
                }
        }
        history_.clear();
        historySeeded_ = false;
        if (mipSourceTex_ != nullptr) {
                SDL_ReleaseGPUTexture(device_, mipSourceTex_);
                mipSourceTex_ = nullptr;
        }
        targetGameWidth_ = 0;
        targetGameHeight_ = 0;
        targetSwapchainWidth_ = 0;
        targetSwapchainHeight_ = 0;
}

bool Runner::LoadLut(LutRuntime &lut, std::string &error)
{
        const std::string path = ResolvePresetFilePath(preset_.dir, lut.def.path);
        SDL_Surface *loaded = IMG_Load(path.c_str());
        if (loaded == nullptr) {
                error = StrCat("IMG_Load failed: ", DescribeImageLoadFailure(path), "; SDL: ", SDL_GetError());
                return false;
        }
        SDL_Surface *rgba = SDL_ConvertSurface(loaded, SDL_PIXELFORMAT_ABGR8888);
        SDL_DestroySurface(loaded);
        if (rgba == nullptr) {
                error = StrCat("surface conversion failed: ", SDL_GetError());
                return false;
        }
        lut.width = static_cast<Uint32>(rgba->w);
        lut.height = static_cast<Uint32>(rgba->h);

        SDL_GPUTextureCreateInfo info {};
        info.type = SDL_GPU_TEXTURETYPE_2D;
        info.format = SDL_GPU_TEXTUREFORMAT_R8G8B8A8_UNORM;
        // SDL's SDL_GenerateMipmapsForGPUTexture contract requires SAMPLER +
        // COLOR_TARGET usage on mipmapped textures. The Vulkan backend never
        // needed COLOR_TARGET (transfer bits are always present and this
        // device disables SDL debug mode), but keep the flags contract-correct
        // so mip generation also survives debug mode and future backends.
        info.usage = SDL_GPU_TEXTUREUSAGE_SAMPLER;
        if (lut.def.mipmap)
                info.usage |= SDL_GPU_TEXTUREUSAGE_COLOR_TARGET;
        info.width = lut.width;
        info.height = lut.height;
        info.layer_count_or_depth = 1;
        info.num_levels = lut.def.mipmap ? MipLevelsFor(lut.width, lut.height) : 1;
        info.sample_count = SDL_GPU_SAMPLECOUNT_1;
        lut.texture = SDL_CreateGPUTexture(device_, &info);
        if (lut.texture == nullptr) {
                error = StrCat("LUT texture creation failed: ", SDL_GetError());
                SDL_DestroySurface(rgba);
                return false;
        }

        SDL_GPUTransferBufferCreateInfo transferInfo {};
        transferInfo.usage = SDL_GPU_TRANSFERBUFFERUSAGE_UPLOAD;
        transferInfo.size = lut.width * lut.height * 4;
        SDL_GPUTransferBuffer *transfer = SDL_CreateGPUTransferBuffer(device_, &transferInfo);
        if (transfer == nullptr) {
                error = StrCat("LUT transfer buffer failed: ", SDL_GetError());
                SDL_DestroySurface(rgba);
                return false;
        }
        auto *dst = static_cast<uint8_t *>(SDL_MapGPUTransferBuffer(device_, transfer, false));
        const int bytesPerRow = static_cast<int>(lut.width) * 4;
        if (rgba->pitch == bytesPerRow) {
                std::memcpy(dst, rgba->pixels, static_cast<size_t>(bytesPerRow) * lut.height);
        } else {
                for (Uint32 y = 0; y < lut.height; ++y)
                        std::memcpy(dst + static_cast<size_t>(y) * bytesPerRow,
                            static_cast<const uint8_t *>(rgba->pixels) + static_cast<size_t>(y) * rgba->pitch, bytesPerRow);
        }
        SDL_UnmapGPUTransferBuffer(device_, transfer);
        SDL_DestroySurface(rgba);

        SDL_GPUCommandBuffer *cmd = SDL_AcquireGPUCommandBuffer(device_);
        if (cmd == nullptr) {
                error = "command buffer acquisition failed";
                SDL_ReleaseGPUTransferBuffer(device_, transfer);
                return false;
        }
        SDL_GPUCopyPass *copyPass = SDL_BeginGPUCopyPass(cmd);
        SDL_GPUTextureTransferInfo src {};
        src.transfer_buffer = transfer;
        src.offset = 0;
        src.pixels_per_row = lut.width;
        src.rows_per_layer = lut.height;
        SDL_GPUTextureRegion region {};
        region.texture = lut.texture;
        region.w = lut.width;
        region.h = lut.height;
        region.d = 1;
        SDL_UploadToGPUTexture(copyPass, &src, &region, false);
        SDL_EndGPUCopyPass(copyPass);
        if (lut.def.mipmap)
                SDL_GenerateMipmapsForGPUTexture(cmd, lut.texture);
        SDL_SubmitGPUCommandBuffer(cmd);
        SDL_ReleaseGPUTransferBuffer(device_, transfer);
        return true;
}

void Runner::FillBlockBytes(const UniformBlockRuntime &block, int passIndex, float inputWidth, float inputHeight,
    bool fragmentStage, std::vector<uint8_t> &out) const
{
        const UniformBlock &def = block.block;
        uint32_t total = 0;
        for (const UniformMember &member : def.members)
                total = std::max(total, member.offset + member.size);
        out.assign(std::max<uint32_t>(total, 16), 0);

        const PassRuntime *pass = passes_[static_cast<size_t>(passIndex)];
        const float passOutW = static_cast<float>(pass->outputWidth);
        const float passOutH = static_cast<float>(pass->outputHeight);
        const float gameW = static_cast<float>(targetGameWidth_);
        const float gameH = static_cast<float>(targetGameHeight_);
        const float viewportW = static_cast<float>(targetViewportWidth_);
        const float viewportH = static_cast<float>(targetViewportHeight_);
        const float inputW = inputWidth;
        const float inputH = inputHeight;

        // MVP matrices (column-major, matching GLSL convention). Passes that
        // render into an intermediate target get kFlipY - see the MVP member
        // write below for the orientation rationale.
        static constexpr float kIdentity[16] = { 1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1 };
        static constexpr float kFlipY[16] = { 1, 0, 0, 0, 0, -1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1 };

        auto writeVec4 = [&](const UniformMember &member, float x, float y, float z, float w) {
                WriteFloat(out, member.offset, x);
                WriteFloat(out, member.offset + 4, y);
                if (member.type == UniformType::Vec4 || member.type == UniformType::IVec4 || member.type == UniformType::UVec4) {
                        WriteFloat(out, member.offset + 8, z);
                        WriteFloat(out, member.offset + 12, w);
                }
        };

        for (const UniformMember &member : def.members) {
                const std::string &name = member.name;
                if (member.arraySize > 0)
                        continue; // Size members are never arrays; leave zeros.

                if (name == "MVP") {
                        // Orientation calibration. The fullscreen quad pairs UV v=0 with
                        // clip-space y=-1, while SDL_GPU render targets map clip-space y=+1
                        // to the top row, so every preset pass draw composites one vertical
                        // mirror: it samples its input one way and stores the result the
                        // other way. A single pass never shows it because the shared present
                        // blit (fullscreen.vert anchors vUV the same GL-style way) mirrors
                        // once more on the way to the screen. Chains break that
                        // cancellation: N passes land mirrored iff N is even (field bisect
                        // A1/A2/C2/E1), and hqx-style presets add a mirror at any depth
                        // because their upscale pass reads colors from an aliased earlier
                        // pass (hqx_refpass) instead of the chain input (field bisect
                        // D1/D2). Cancel the per-draw mirror for every pass that feeds
                        // another pass; the LAST pass keeps the unmodified convention so
                        // the shared present blit still cancels it exactly as it always has
                        // (1-pass presets render identically to before this fix). Vertex
                        // stages without an MVP member skip this branch entirely and keep
                        // their own convention (the load log flags them as vertexMvp=no).
                        const float *mvp = kIdentity;
                        if (!fragmentStage && passIndex + 1 < static_cast<int>(passes_.size()))
                                mvp = kFlipY;
                        for (int i = 0; i < 16; ++i)
                                WriteFloat(out, member.offset + 4 * i, mvp[i]);
                        continue;
                }
                if (name == "OutputSize") {
                        writeVec4(member, passOutW, passOutH, passOutW > 0 ? 1.0F / passOutW : 0.0F, passOutH > 0 ? 1.0F / passOutH : 0.0F);
                        continue;
                }
                if (name == "FinalViewportSize") {
                        // F6: RetroArch's FinalViewportSize is the aspect-corrected
                        // viewport (the game rect inside the swapchain), not the raw window.
                        writeVec4(member, viewportW, viewportH, viewportW > 0 ? 1.0F / viewportW : 0.0F, viewportH > 0 ? 1.0F / viewportH : 0.0F);
                        continue;
                }
                if (name == "OriginalSize") {
                        writeVec4(member, gameW, gameH, gameW > 0 ? 1.0F / gameW : 0.0F, gameH > 0 ? 1.0F / gameH : 0.0F);
                        continue;
                }
                if (name == "SourceSize") {
                        writeVec4(member, inputW, inputH, inputW > 0 ? 1.0F / inputW : 0.0F, inputH > 0 ? 1.0F / inputH : 0.0F);
                        continue;
                }
                if (name == "FrameCount") {
                        Uint64 count = frameCounter_;
                        if (pass->def.frameCountMod > 0)
                                count %= pass->def.frameCountMod;
                        WriteUint(out, member.offset, static_cast<uint32_t>(count));
                        continue;
                }
                if (name == "FrameDirection") {
                        WriteInt(out, member.offset, 1);
                        continue;
                }
                if (name == "FrameTime") {
                        WriteFloat(out, member.offset, static_cast<float>(frameDeltaUs_));
                        continue;
                }
                if (name == "PASSINDEX") {
                        WriteInt(out, member.offset, passIndex);
                        continue;
                }
                if (name == "Rotation") {
                        // RetroArch fills the Rotation semantic with the frontend's
                        // content rotation (0/1/2/3 = 0/90/180/270 degrees). This game
                        // never rotates the view, so presets declaring it (16 corpus
                        // shaders: crt-effects, authentic_gbc, border fills, subframe
                        // BFI) read the identity value 0 (SLANG_SPEC.md gap #5).
                        WriteUint(out, member.offset, 0);
                        continue;
                }

                // <TextureName>Size members.
                if (name.size() > 4 && name.compare(name.size() - 4, 4, "Size") == 0) {
                        std::string texName = name.substr(0, name.size() - 4);
                        bool feedbackForm = false;
                        if (texName.size() > 8 && texName.compare(texName.size() - 8, 8, "Feedback") == 0) {
                                texName = texName.substr(0, texName.size() - 8);
                                feedbackForm = true;
                        }
                        (void)feedbackForm; // Feedback textures share the size of their pass output.
                        float w = 0;
                        float h = 0;
                        if (texName == "Source") {
                                w = inputW;
                                h = inputH;
                        } else if (texName == "Original" || texName.rfind("OriginalHistory", 0) == 0) {
                                w = gameW;
                                h = gameH;
                        } else if (texName.rfind("PassOutput", 0) == 0 || texName.rfind("PassFeedback", 0) == 0) {
                                // "PassOutput" is a 10-char prefix but "PassFeedback"
                                // is 12: slicing both at 10 handed stoi("ck#") for the
                                // feedback form and aborted on an uncaught
                                // std::invalid_argument (SLANG_SPEC.md gap #4). Covers
                                // both the <PassFeedback#>Size and the
                                // <PassFeedback#>FeedbackSize member forms.
                                const size_t prefixLen = texName.rfind("PassFeedback", 0) == 0 ? 12 : 10;
                                // ROB-01: a bare prefix ("PassOutputSize" -> suffix "") or a
                                // non-numeric suffix previously threw out of PushBlockUniforms.
                                const std::optional<int> idx = ParseTextureIndex(texName, prefixLen);
                                if (idx && *idx >= 0 && *idx < static_cast<int>(passes_.size())) {
                                        w = static_cast<float>(passes_[static_cast<size_t>(*idx)]->outputWidth);
                                        h = static_cast<float>(passes_[static_cast<size_t>(*idx)]->outputHeight);
                                }
                        } else {
                                // Alias or lookup texture.
                                bool resolved = false;
                                for (const PassRuntime *other : passes_) {
                                        const bool nameMatch = (!other->def.alias.empty() && CaseInsensitiveEqual(other->def.alias, texName))
                                            || (!other->translation.pragmaName.empty() && CaseInsensitiveEqual(other->translation.pragmaName, texName));
                                        if (nameMatch) {
                                                w = static_cast<float>(other->outputWidth);
                                                h = static_cast<float>(other->outputHeight);
                                                resolved = true;
                                                break;
                                        }
                                }
                                if (!resolved) {
                                        for (const LutRuntime *lut : luts_) {
                                                if (CaseInsensitiveEqual(lut->def.name, texName)) {
                                                        w = static_cast<float>(lut->width);
                                                        h = static_cast<float>(lut->height);
                                                        resolved = true;
                                                        break;
                                                }
                                        }
                                }
                        }
                        writeVec4(member, w, h, w > 0 ? 1.0F / w : 0.0F, h > 0 ? 1.0F / h : 0.0F);
                        continue;
                }

                // Shader parameters (defaults from #pragma parameter, overridden by
                // the preset file's `parameters` list).
                float value = 0.0F;
                bool found = false;
                for (const ParamOverride &override : preset_.parameters) {
                        // Preset keys are lowercased at parse time while #pragma
                        // parameter identifiers keep their original case; an
                        // exact-case compare made every mixed-case override (Mega
                        // Bezel overlays) silently keep defaults (SLANG_SPEC.md
                        // gap #3).
                        if (CaseInsensitiveEqual(override.name, name)) {
                                value = override.value;
                                found = true;
                                break;
                        }
                }
                if (!found) {
                        for (const ParamInfo &param : pass->translation.parameters) {
                                if (param.name == name) {
                                        value = param.defaultValue;
                                        found = true;
                                        break;
                                }
                        }
                }
                WriteFloat(out, member.offset, value);
        }
}

void Runner::PushBlockUniforms(SDL_GPUCommandBuffer *cmd, bool fragmentStage, const UniformBlockRuntime &block,
    int passIndex, float inputWidth, float inputHeight)
{
        std::vector<uint8_t> bytes;
        FillBlockBytes(block, passIndex, inputWidth, inputHeight, fragmentStage, bytes);
        if (fragmentStage)
                SDL_PushGPUFragmentUniformData(cmd, block.block.binding, bytes.data(), static_cast<Uint32>(bytes.size()));
        else
                SDL_PushGPUVertexUniformData(cmd, block.block.binding, bytes.data(), static_cast<Uint32>(bytes.size()));
}

bool Runner::Run(SDL_GPUCommandBuffer *cmd, SDL_GPUTexture *gameTex, int gameWidth, int gameHeight,
    Uint32 swapchainWidth, Uint32 swapchainHeight, Uint64 frameDeltaUs, SDL_GPUTexture **outTexture,
    Uint32 *outWidth, Uint32 *outHeight)
{
        if (!IsLoaded() || passes_.empty())
                return false;
        if (!EnsureSamplers()) {
                SDL_LogInfo(SDL_LOG_CATEGORY_APPLICATION,
                    "GPU scaler preset: sampler creation failed: %s", SDL_GetError());
                return false;
        }
        if (!EnsureTargets(gameWidth, gameHeight, swapchainWidth, swapchainHeight))
                return false;

        frameCounter_ += 1;
        frameDeltaUs_ = frameDeltaUs;
        swapchainWidth_ = swapchainWidth;
        swapchainHeight_ = swapchainHeight;

        // Update the chain-input history ring: the current input overwrites
        // the slot at historySlot_; older frames live at decreasing slot
        // indices (historySlot_ advances forward by one every frame), so
        // OriginalHistoryN resolves to ring[(historySlot_ - N) mod size].
        if (!history_.empty()) {
                // Seed every slot with the current frame on the first run (and
                // after every target resize) so early OriginalHistory# reads
                // see valid content instead of undefined texture memory.
                if (!historySeeded_) {
                        for (SDL_GPUTexture *slotTex : history_) {
                                SDL_GPUBlitInfo seed {};
                                seed.source.texture = gameTex;
                                seed.source.w = static_cast<Uint32>(gameWidth);
                                seed.source.h = static_cast<Uint32>(gameHeight);
                                seed.destination.texture = slotTex;
                                seed.destination.w = static_cast<Uint32>(gameWidth);
                                seed.destination.h = static_cast<Uint32>(gameHeight);
                                seed.load_op = SDL_GPU_LOADOP_DONT_CARE;
                                seed.filter = SDL_GPU_FILTER_NEAREST;
                                SDL_BlitGPUTexture(cmd, &seed);
                        }
                        historySeeded_ = true;
                }
                SDL_GPUBlitInfo blit {};
                blit.source.texture = gameTex;
                blit.source.w = static_cast<Uint32>(gameWidth);
                blit.source.h = static_cast<Uint32>(gameHeight);
                blit.destination.texture = history_[static_cast<size_t>(historySlot_)];
                blit.destination.w = static_cast<Uint32>(gameWidth);
                blit.destination.h = static_cast<Uint32>(gameHeight);
                blit.load_op = SDL_GPU_LOADOP_DONT_CARE;
                blit.filter = SDL_GPU_FILTER_NEAREST;
                SDL_BlitGPUTexture(cmd, &blit);
        }

        float inputW = static_cast<float>(gameWidth);
        float inputH = static_cast<float>(gameHeight);
        SDL_GPUTexture *sourceTex = gameTex;

        for (size_t i = 0; i < passes_.size(); ++i) {
                PassRuntime *pass = passes_[i];
                const int cur = pass->curBuffer;
                SDL_GPUTexture *target = pass->outputTex[cur];

                // F1: refill the mip chain of this pass's Source before the draw -
                // SDL_GenerateMipmapsForGPUTexture must run outside any render pass,
                // so it sits between the per-pass render passes here. Pass 0's Source
                // is the chain input, a single-level scaler-owned texture: blit it
                // into the mip-capable copy first and bind the copy. Later passes
                // generate directly on the producing pass's output (allocated with a
                // full chain in EnsureTargets exactly when this flag is set).
                if (pass->def.mipmapInput) {
                        if (i == 0 && mipSourceTex_ != nullptr) {
                                SDL_GPUBlitInfo copy {};
                                copy.source.texture = gameTex;
                                copy.source.w = static_cast<Uint32>(gameWidth);
                                copy.source.h = static_cast<Uint32>(gameHeight);
                                copy.destination.texture = mipSourceTex_;
                                copy.destination.w = static_cast<Uint32>(gameWidth);
                                copy.destination.h = static_cast<Uint32>(gameHeight);
                                copy.load_op = SDL_GPU_LOADOP_DONT_CARE;
                                copy.filter = SDL_GPU_FILTER_NEAREST;
                                SDL_BlitGPUTexture(cmd, &copy);
                                SDL_GenerateMipmapsForGPUTexture(cmd, mipSourceTex_);
                                sourceTex = mipSourceTex_;
                        } else if (i > 0) {
                                SDL_GenerateMipmapsForGPUTexture(cmd, sourceTex);
                        }
                }

                const StageTranslation &frag = pass->translation.fragment;
                const StageTranslation &vert = pass->translation.vertex;

                SDL_GPUColorTargetInfo colorInfo {};
                colorInfo.texture = target;
                colorInfo.clear_color = { 0.0F, 0.0F, 0.0F, 1.0F };
                colorInfo.load_op = SDL_GPU_LOADOP_CLEAR;
                colorInfo.store_op = SDL_GPU_STOREOP_STORE;
                SDL_GPURenderPass *renderPass = SDL_BeginGPURenderPass(cmd, &colorInfo, 1, nullptr);
                if (renderPass == nullptr) {
                        SDL_LogInfo(SDL_LOG_CATEGORY_APPLICATION,
                            "GPU scaler preset: render pass begin failed in pass %u (%ux%u): %s",
                            static_cast<unsigned>(i), pass->outputWidth, pass->outputHeight, SDL_GetError());
                        return false;
                }
                SDL_BindGPUGraphicsPipeline(renderPass, pass->pipeline);

                // Push uniform blocks (std140 bytes computed from the reflected layout).
                for (const UniformBlock &block : vert.uniformBlocks) {
                        UniformBlockRuntime runtime { block };
                        PushBlockUniforms(cmd, /*fragmentStage=*/false, runtime, static_cast<int>(i), inputW, inputH);
                }
                for (const UniformBlock &block : frag.uniformBlocks) {
                        UniformBlockRuntime runtime { block };
                        PushBlockUniforms(cmd, /*fragmentStage=*/true, runtime, static_cast<int>(i), inputW, inputH);
                }

                SDL_GPUBufferBinding vertexBinding {};
                vertexBinding.buffer = vertexBuffer_;
                vertexBinding.offset = 0;
                SDL_BindGPUVertexBuffers(renderPass, 0, &vertexBinding, 1);

                // Fragment texture bindings, dense by assigned slot.
                if (!frag.textures.empty()) {
                        std::vector<SDL_GPUTextureSamplerBinding> bindings(frag.textures.size());
                        for (size_t b = 0; b < frag.textures.size(); ++b) {
                                const std::string &name = frag.textures[b].name;
                                SDL_GPUTexture *tex = nullptr;
                                SDL_GPUSampler *sampler = SamplerFor(FilterMode::Nearest, WrapMode::ClampToEdge, false);
                                if (name == "Source") {
                                        tex = sourceTex;
                                        sampler = SamplerFor(pass->def.filter, pass->def.wrap, pass->def.mipmapInput);
                                } else if (name == "Original") {
                                        tex = gameTex;
                                } else if (name.rfind("OriginalHistory", 0) == 0) {
                                        // ROB-01: "OriginalHistory" with a missing, non-numeric
                                        // ("OriginalHistoryx") or 20-digit suffix used to throw out
                                        // of the frame loop and terminate the process.
                                        const std::optional<int> idx = ParseTextureIndex(name, 15);
                                        if (!idx || *idx == 0 || history_.empty()) {
                                                tex = gameTex;
                                        } else {
                                                // Frame T-N sits N slots behind the just-written
                                                // current-frame slot (slots advance forward per frame).
                                                const size_t slot = (static_cast<size_t>(historySlot_)
                                                                        + history_.size() - static_cast<size_t>(*idx))
                                                    % history_.size();
                                                tex = history_[slot];
                                        }
                                } else if (name.rfind("PassOutput", 0) == 0) {
                                        // ROB-01: non-throwing suffix parse; an unparsable or
                                        // out-of-range index leaves tex null and the null-texture
                                        // guard below binds the current input frame instead.
                                        const std::optional<int> idx = ParseTextureIndex(name, 10);
                                        if (idx && *idx >= 0 && *idx < static_cast<int>(passes_.size())) {
                                                PassRuntime *other = passes_[static_cast<size_t>(*idx)];
                                                tex = other->outputTex[other->curBuffer];
                                        }
                                } else if (name.rfind("PassFeedback", 0) == 0) {
                                        const std::optional<int> idx = ParseTextureIndex(name, 12);
                                        if (idx && *idx >= 0 && *idx < static_cast<int>(passes_.size())) {
                                                PassRuntime *other = passes_[static_cast<size_t>(*idx)];
                                                tex = other->doubleBuffered ? other->outputTex[1 - other->curBuffer]
                                                                            : other->outputTex[other->curBuffer];
                                        }
                                } else if (name.size() > 8 && name.compare(name.size() - 8, 8, "Feedback") == 0) {
                                        const std::string base = name.substr(0, name.size() - 8);
                                        for (PassRuntime *other : passes_) {
                                                const bool nameMatch = (!other->def.alias.empty() && CaseInsensitiveEqual(other->def.alias, base))
                                                    || (!other->translation.pragmaName.empty() && CaseInsensitiveEqual(other->translation.pragmaName, base));
                                                if (nameMatch) {
                                                        tex = other->doubleBuffered ? other->outputTex[1 - other->curBuffer]
                                                                                    : other->outputTex[other->curBuffer];
                                                        break;
                                                }
                                        }
                                } else {
                                        // Lookup texture (by preset name or User#).
                                        std::string lutName = name;
                                        if (lutName.rfind("User", 0) == 0) {
                                                // ROB-01: non-throwing suffix parse ("User" alone or
                                                // "UserFoo" previously threw out of the frame loop).
                                                const std::optional<int> idx = ParseTextureIndex(lutName, 4);
                                                if (idx && *idx >= 0 && *idx < static_cast<int>(luts_.size()))
                                                        lutName = luts_[static_cast<size_t>(*idx)]->def.name;
                                        }
                                        for (const LutRuntime *lut : luts_) {
                                                if (CaseInsensitiveEqual(lut->def.name, lutName)) {
                                                        tex = lut->texture;
                                                        sampler = SamplerFor(lut->def.filter, lut->def.wrap, lut->def.mipmap);
                                                        break;
                                                }
                                        }
                                        if (tex == nullptr) {
                                                // RetroArch lets a pass sample an EARLIER pass's output
                                                // by its plain alias name (hqx.slangp: alias0 =
                                                // hqx_refpass, used as texture = hqx_refpass). Only
                                                // earlier passes have an output this frame.
                                                for (size_t oi = 0; oi < i; ++oi) {
                                                        PassRuntime *other = passes_[oi];
                                                        const bool nameMatch = (!other->def.alias.empty() && CaseInsensitiveEqual(other->def.alias, lutName))
                                                            || (!other->translation.pragmaName.empty() && CaseInsensitiveEqual(other->translation.pragmaName, lutName));
                                                        if (nameMatch) {
                                                                tex = other->outputTex[other->curBuffer];
                                                                break;
                                                        }
                                                }
                                        }
                                }
                                if (tex == nullptr) {
                                        // Never bind a null texture: the backend dereferences
                                        // it and crashes (access violation mid-frame). Reached
                                        // from every branch above: unresolved lookup names, an
                                        // unparsable or out-of-range builtin index (ROB-01)
                                        // and unmatched Feedback aliases all fall back to a
                                        // valid binding instead of crashing.
                                        static std::unordered_set<std::string> warnedNames;
                                        if (warnedNames.insert(name).second)
                                                SDL_LogInfo(SDL_LOG_CATEGORY_APPLICATION,
                                                    "GPU scaler preset: unresolved texture '%s' in pass %u; using the current input frame",
                                                    name.c_str(), static_cast<unsigned>(i));
                                        tex = sourceTex;
                                        sampler = SamplerFor(FilterMode::Nearest, WrapMode::ClampToEdge, false);
                                }
                                bindings[b].texture = tex;
                                bindings[b].sampler = sampler;
                        }
                        SDL_BindGPUFragmentSamplers(renderPass, 0, bindings.data(), static_cast<Uint32>(bindings.size()));
                }

                SDL_DrawGPUPrimitives(renderPass, 6, 1, 0, 0);
                SDL_EndGPURenderPass(renderPass);

                // The next pass samples this pass's output.
                inputW = static_cast<float>(pass->outputWidth);
                inputH = static_cast<float>(pass->outputHeight);
                sourceTex = target;
        }

        // Flip the double-buffered outputs for the next frame's feedback reads.
        for (PassRuntime *pass : passes_) {
                if (pass->doubleBuffered)
                        pass->curBuffer = 1 - pass->curBuffer;
        }
        if (!history_.empty())
                historySlot_ = (historySlot_ + 1) % static_cast<int>(history_.size());

        PassRuntime *last = passes_.back();
        *outTexture = last->outputTex[last->curBuffer];
        *outWidth = last->outputWidth;
        *outHeight = last->outputHeight;
        return true;
}

void Runner::Unload()
{
        if (device_ == nullptr) {
                for (PassRuntime *pass : passes_)
                        delete pass;
                passes_.clear();
                for (LutRuntime *lut : luts_)
                        delete lut;
                luts_.clear();
                return;
        }
        ReleaseTargets();
        for (PassRuntime *pass : passes_) {
                if (pass->vertexShader != nullptr)
                        SDL_ReleaseGPUShader(device_, pass->vertexShader);
                if (pass->fragmentShader != nullptr)
                        SDL_ReleaseGPUShader(device_, pass->fragmentShader);
                if (pass->pipeline != nullptr)
                        SDL_ReleaseGPUGraphicsPipeline(device_, pass->pipeline);
                delete pass;
        }
        passes_.clear();
        for (LutRuntime *lut : luts_) {
                if (lut->texture != nullptr)
                        SDL_ReleaseGPUTexture(device_, lut->texture);
                delete lut;
        }
        luts_.clear();
        if (vertexBuffer_ != nullptr) {
                SDL_ReleaseGPUBuffer(device_, vertexBuffer_);
                vertexBuffer_ = nullptr;
        }
        for (int f = 0; f < 2; ++f) {
                for (int w = 0; w < 3; ++w) {
                        for (int m = 0; m < 2; ++m) {
                                if (samplers_[f][w][m] != nullptr) {
                                        SDL_ReleaseGPUSampler(device_, samplers_[f][w][m]);
                                        samplers_[f][w][m] = nullptr;
                                }
                        }
                }
        }
        device_ = nullptr;
        preset_ = Preset {};
}

} // namespace preset
} // namespace gpu_scaler
} // namespace devilution
#endif // !USE_SDL1
#endif // USE_GPU_SCALER
