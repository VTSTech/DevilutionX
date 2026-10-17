// libslang-vk — Preset implementation (M1): orchestrates the GPU-agnostic
// core (parse -> translate -> glslang compile) into a load-ready artifact.
//
// The shader root follows the core layer's contract (SetShaderRootDir, plan
// §5): hosts configure it once at startup. For headless convenience, when
// the root still points at the default non-existent "shaders" folder the
// preset's own directory is adopted implicitly.

#include <algorithm>
#include <cctype>
#include <filesystem>
#include <unordered_set>

#include "vk_internal.hpp"
#include "impls.hpp"

namespace slv {

namespace {

auto MergeParameters(const Preset::Impl &impl) -> std::vector<ParameterInfo>
{
        // Parameters are preset-global (RetroArch semantics): union by name in
        // first-seen order; pragma defaults, then preset override values.
        std::vector<ParameterInfo> out;
        for (const SlangTranslation &translation : impl.translations) {
                for (const slv::preset::ParamInfo &param : translation.parameters) {
                        bool found = false;
                        for (ParameterInfo &existing : out)
                                if (existing.name == param.name) {
                                        found = true;
                                        break;
                                }
                        if (!found) {
                                ParameterInfo info;
                                info.name = param.name;
                                info.description = param.description;
                                info.defaultValue = param.defaultValue;
                                info.minimum = param.minimum;
                                info.maximum = param.maximum;
                                info.step = param.step;
                                out.push_back(std::move(info));
                        }
                }
        }
        // Surface preset overrides as the "effective default" for UIs; the
        // parser lowercases .slangp keys, so match names case-insensitively.
        for (ParameterInfo &info : out)
                for (const slv::preset::ParamOverride &override : impl.parsed.parameters)
                        if (vk::CaseInsensitiveEqual(override.name, info.name))
                                info.defaultValue = override.value;
        return out;
}

} // namespace

auto Preset::load(Device &device, const std::filesystem::path &slangp, const PresetLoadOptions &options)
    -> std::expected<Preset, Error>
{
        namespace fs = std::filesystem;
        namespace core = slv::preset;

        if (slangp.empty())
                return std::unexpected(SLV_ERROR(Errc::invalid_argument, "Preset::load: empty path"));

        // Headless convenience: adopt the preset's directory as shader root when
        // the default root does not exist (hosts configure it explicitly).
        if (!fs::exists(core::ShaderRootDir())) {
                std::error_code ec;
                const fs::path parent = fs::absolute(slangp, ec).parent_path();
                if (!ec && !parent.empty())
                        core::SetShaderRootDir(parent.generic_string());
        }

        Preset out;
        out.impl_ = std::make_unique<Impl>();
        Impl &impl = *out.impl_;
        impl.device = &device;

        // 1. Parse the .slangp (order-independent, RetroArch semantics).
        std::string parseError;
        if (!core::ParsePreset(slangp.generic_string(), impl.parsed, parseError))
                return std::unexpected(SLV_ERROR(Errc::preset_parse, parseError));

        // Sampler name universe: LUT texture names + pass aliases + User#
        // positional aliases (SPEC 4.5). Both the declared spelling and the
        // lowercase form are admitted: the translator validates against
        // lowercased names (F9 case-insensitive matching), while GLSL binds
        // by the declared spelling (case-sensitive identifiers).
        const auto toLower = [](std::string s) {
                for (char &c : s)
                        c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
                return s;
        };
        std::unordered_set<std::string> knownTextureNames;
        for (const core::TextureDef &texture : impl.parsed.textures) {
                knownTextureNames.insert(texture.name);
                knownTextureNames.insert(toLower(texture.name));
        }
        for (const core::PassDef &pass : impl.parsed.passes) {
                if (pass.alias.empty())
                        continue;
                knownTextureNames.insert(pass.alias);
                knownTextureNames.insert(toLower(pass.alias));
        }
        for (size_t i = 0; i < impl.parsed.textures.size(); ++i)
                knownTextureNames.insert("User" + std::to_string(i));

        // 2. Per pass: translate to stage GLSL, compile to SPIR-V.
        const int passCount = static_cast<int>(impl.parsed.passes.size());
        impl.passDefs = impl.parsed.passes;
        impl.translations.resize(impl.passDefs.size());
        impl.spirv.resize(impl.passDefs.size());
        for (int index = 0; index < passCount; ++index) {
                const core::PassDef &passDef = impl.passDefs[static_cast<size_t>(index)];
                const std::string shaderPath = core::ResolvePresetFilePath(impl.parsed.dir, passDef.shaderPath);

                SlangTranslation &translation = impl.translations[static_cast<size_t>(index)];
                core::TranslateSlang(shaderPath, knownTextureNames, index, passCount, translation);
                if (!translation.ok())
                        return std::unexpected(SLV_ERROR(
                            translation.error.find("cycle") != std::string::npos ? Errc::include_cycle
                                                                                 : Errc::shader_compile,
                            "pass " + std::to_string(index) + " '" + passDef.shaderPath + "': " + translation.error));
                if (options.strict && !translation.warnings.empty()) {
                        std::string joined;
                        for (const std::string &warning : translation.warnings) {
                                if (!joined.empty())
                                        joined += "; ";
                                joined += warning;
                        }
                        return std::unexpected(
                            SLV_ERROR(Errc::shader_compile, "strict load, pass " + std::to_string(index) + ": " + joined));
                }

                PassSpirv &spirv = impl.spirv[static_cast<size_t>(index)];
                std::string compileError;
                if (!core::CompilePass(translation, spirv, compileError))
                        return std::unexpected(SLV_ERROR(Errc::shader_compile,
                            "pass " + std::to_string(index) + " '" + passDef.shaderPath + "': " + compileError));
        }

        impl.parameters = MergeParameters(impl);

        // 3. Upload LUT textures (M3): decode to RGBA8, upload as
        // R8G8B8A8_UNORM, mip chain at upload when `name_mipmap` is set.
        // noLutImages keeps the load compile-only (corpus sweeps, no-ICD
        // hosts); chains then fail at bind time with an explicit error.
        if (!impl.parsed.textures.empty() && !options.noLutImages) {
                for (const core::TextureDef &texture : impl.parsed.textures) {
                        const std::string resolved = core::ResolvePresetFilePath(impl.parsed.dir, texture.path);
                        Preset::Impl::LutGpu lut;
                        lut.name = texture.name;
                        auto uploaded = vk::UploadLutTexture(device, texture, resolved, lut);
                        if (!uploaded)
                                return std::unexpected(uploaded.error());
                        impl.luts.push_back(std::move(lut));
                }
        }
        return out;
}

Preset::~Preset() = default;
Preset::Preset(Preset &&) noexcept = default;
auto Preset::operator=(Preset &&) noexcept -> Preset & = default;

auto Preset::passCount() const -> uint32_t { return static_cast<uint32_t>(impl_->passDefs.size()); }
auto Preset::parameterInfo() const -> std::span<const ParameterInfo> { return impl_->parameters; }

} // namespace slv
