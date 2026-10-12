#include "engine/render/gpu/preset/preset_parser.hpp"

#ifdef USE_GPU_SCALER
#ifndef USE_SDL1

#include <algorithm>
#include <cctype>
#include <cstdint>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <map>
#include <regex>
#include <set>
#include <sstream>

#include <SDL3/SDL_filesystem.h>

#include "utils/log.hpp"
#include "utils/str_cat.hpp"

namespace devilution {
namespace gpu_scaler {
namespace preset {
namespace {

/** @brief Normalizes path separators to '/' so logs, cache keys and joins are stable. */
std::string NormalizeSeparators(std::string path)
{
	for (char &c : path) {
		if (c == '\\')
			c = '/';
	}
	return path;
}

} // namespace
namespace {

std::string Trim(const std::string &s)
{
	size_t begin = 0;
	size_t end = s.size();
	while (begin < end && std::isspace(static_cast<unsigned char>(s[begin])) != 0)
		++begin;
	while (end > begin && std::isspace(static_cast<unsigned char>(s[end - 1])) != 0)
		--end;
	return s.substr(begin, end - begin);
}

std::string ToLower(std::string s)
{
	for (char &c : s)
		c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
	return s;
}

/** @brief Strips a '#' comment that is not inside double quotes. */
std::string StripComment(const std::string &line)
{
	bool inQuotes = false;
	bool seenNonSpace = false;
	for (size_t i = 0; i < line.size(); ++i) {
		const char c = line[i];
		if (c == '"') {
			inQuotes = !inQuotes;
		} else if (c == '#' && !inQuotes) {
			// A '#' that opens the line and starts the #reference directive is
			// not a comment; everything else is.
			if (!seenNonSpace && line.compare(i, 10, "#reference") == 0)
				return line;
			return line.substr(0, i);
		} else if (!inQuotes && c != ' ' && c != '\t') {
			seenNonSpace = true;
		}
	}
	return line;
}

/** @brief True when the value looks like an image file path (RetroArch
 * presets sometimes declare textures without a `textures =` list). */
bool LooksLikeImagePath(const std::string &value)
{
	static constexpr const char *kExtensions[] = { ".png", ".jpg", ".jpeg", ".bmp", ".tga", ".dds", ".webp" };
	for (const char *ext : kExtensions) {
		const size_t len = std::strlen(ext);
		if (value.size() >= len && value.compare(value.size() - len, len, ext) == 0)
			return true;
	}
	return false;
}

/** @brief Strips one surrounding pair of double quotes, if present. */
std::string StripQuotes(const std::string &s)
{
	if (s.size() >= 2 && s.front() == '"' && s.back() == '"')
		return s.substr(1, s.size() - 2);
	return s;
}

bool ParseBool(const std::string &value, bool defaultValue)
{
	const std::string v = ToLower(Trim(value));
	if (v == "true" || v == "1" || v == "yes" || v == "on")
		return true;
	if (v == "false" || v == "0" || v == "no" || v == "off")
		return false;
	return defaultValue;
}

int ParseInt(const std::string &value, int defaultValue)
{
	try {
		return std::stoi(Trim(value));
	} catch (const std::exception &) {
		return defaultValue;
	}
}

float ParseFloat(const std::string &value, float defaultValue)
{
	try {
		return std::stof(Trim(value));
	} catch (const std::exception &) {
		return defaultValue;
	}
}

WrapMode ParseWrapMode(const std::string &value)
{
	const std::string v = ToLower(Trim(value));
	if (v == "repeat")
		return WrapMode::Repeat;
	if (v == "mirrored_repeat")
		return WrapMode::MirroredRepeat;
	return WrapMode::ClampToEdge;
}

FilterMode ParseFilterMode(const std::string &value)
{
	return ParseBool(value, false) ? FilterMode::Linear : FilterMode::Nearest;
}

ScaleType ParseScaleType(const std::string &value, ScaleType defaultValue)
{
	const std::string v = ToLower(Trim(value));
	if (v == "source")
		return ScaleType::Source;
	if (v == "viewport")
		return ScaleType::Viewport;
	if (v == "absolute")
		return ScaleType::Absolute;
	return defaultValue;
}

/** @brief Extracts a trailing pass index from a key like "shader0" / "scale_type_y3".
 * @return the pass index and the key without the index, or -1 when there is no index. */
int SplitPassKey(const std::string &key, std::string &baseKey)
{
	size_t end = key.size();
	while (end > 0 && std::isdigit(static_cast<unsigned char>(key[end - 1])) != 0)
		--end;
	if (end == key.size())
		return -1;
	baseKey = key.substr(0, end);
	return ParseInt(key.substr(end), -1);
}

void ApplyPassKey(PassDef &pass, const std::string &key, const std::string &value, const std::string &presetDir)
{
	if (key == "shader") {
		pass.shaderPath = value;
	} else if (key == "alias") {
		pass.alias = value;
	} else if (key == "filter_linear") {
		pass.filter = ParseFilterMode(value);
	} else if (key == "wrap_mode") {
		pass.wrap = ParseWrapMode(value);
	} else if (key == "srgb_framebuffer") {
		pass.srgbFramebuffer = ParseBool(value, false);
	} else if (key == "float_framebuffer") {
		pass.floatFramebuffer = ParseBool(value, false);
	} else if (key == "mipmap_input") {
		pass.mipmapInput = ParseBool(value, false);
	} else if (key == "frame_count_mod") {
		pass.frameCountMod = static_cast<uint32_t>(std::max(0, ParseInt(value, 0)));
	} else if (key == "scale") {
		pass.scaleX = { ScaleType::Source, ParseFloat(value, 1.0F) };
		pass.scaleY = pass.scaleX;
	} else if (key == "scale_x") {
		pass.scaleX.scale = ParseFloat(value, 1.0F);
	} else if (key == "scale_y") {
		pass.scaleY.scale = ParseFloat(value, 1.0F);
	} else if (key == "scale_type") {
		pass.scaleX.type = ParseScaleType(value, ScaleType::Source);
		pass.scaleY.type = pass.scaleX.type;
	} else if (key == "scale_type_x") {
		pass.scaleX.type = ParseScaleType(value, ScaleType::Source);
	} else if (key == "scale_type_y") {
		pass.scaleY.type = ParseScaleType(value, ScaleType::Source);
	} else if (key == "shaderpath") {
		// Unofficial convenience: absolute or preset-relative path override.
		if (!value.empty() && value[0] != '/') {
			pass.shaderPath = presetDir + value;
		} else {
			pass.shaderPath = value;
		}
	}
}

} // namespace

bool ParsePresetImpl(const std::string &path, Preset &out, std::string &error, int depth, bool isReferenceTarget)
{
	constexpr int kMaxReferenceDepth = 8;
	if (depth > kMaxReferenceDepth) {
		error = StrCat("preset #reference depth limit exceeded at ", path);
		return false;
	}
	std::ifstream stream(path, std::ios::binary);
	if (!stream) {
		error = StrCat("cannot open preset file: ", path);
		return false;
	}
	std::ostringstream ss;
	ss << stream.rdbuf();

	Preset preset;
	preset.filePath = path;
	std::filesystem::path fsPath(path);
	preset.dir = NormalizeSeparators(fsPath.parent_path().string());
	if (!preset.dir.empty() && preset.dir.back() != '/' && preset.dir.back() != '\\')
		preset.dir += '/';
	preset.displayName = fsPath.stem().string();

	// Pass 1: collect key/value pairs (comments stripped, quotes resolved)
	// and follow #reference chains (a referenced parent preset provides the
	// base; the referencing file's keys win).
	static const std::regex referenceRe(R"re(^\s*#reference\s+"([^"]+)"\s*$)re");
	std::vector<std::pair<std::string, std::string>> keyValuePairs;
	std::vector<std::string> referencePaths;
	{
		std::istringstream lines(ss.str());
		std::string line;
		while (std::getline(lines, line)) {
			line = StripComment(line);
			std::smatch match;
			if (std::regex_match(line, match, referenceRe)) {
				referencePaths.push_back(match[1].str());
				continue;
			}
			const size_t eq = line.find('=');
			if (eq == std::string::npos)
				continue;
			std::string key = ToLower(Trim(line.substr(0, eq)));
			std::string value = Trim(line.substr(eq + 1));
			if (!value.empty() && value.front() == '"') {
				const size_t closing = value.find('"', 1);
				value = closing != std::string::npos ? value.substr(1, closing - 1) : StripQuotes(value);
			} else {
				value = Trim(value);
			}
			keyValuePairs.emplace_back(std::move(key), std::move(value));
		}
	}
	// A preset may #reference several parents (a shader-chain preset plus
	// one or more .params overlays, as the Mega Bezel collection does).
	// Parents merge in order: the first chain providing passes wins,
	// textures and parameter overrides accumulate, and this file's own keys
	// are still applied on top afterwards.
	for (const std::string &referencePath : referencePaths) {
		std::filesystem::path parent = referencePath.front() == '/'
		    ? std::filesystem::path(referencePath)
		    : std::filesystem::path(preset.dir) / referencePath;
		Preset parentPreset;
		std::string parentError;
		if (!ParsePresetImpl(parent.string(), parentPreset, parentError, depth + 1, /*isReferenceTarget=*/true)) {
			error = StrCat("referenced preset failed: ", parentError);
			return false;
		}
		if (!parentPreset.passes.empty())
			preset.passes = std::move(parentPreset.passes);
		for (TextureDef &tex : parentPreset.textures) {
			bool exists = false;
			for (const TextureDef &existing : preset.textures)
				exists = exists || ToLower(existing.name) == ToLower(tex.name);
			if (!exists)
				preset.textures.push_back(std::move(tex));
		}
		for (ParamOverride &param : parentPreset.parameters)
			preset.parameters.push_back(std::move(param));
		if (preset.feedbackPass < 0 && parentPreset.feedbackPass >= 0)
			preset.feedbackPass = parentPreset.feedbackPass;
		if (preset.histories <= 1 && parentPreset.histories > preset.histories)
			preset.histories = parentPreset.histories;
	}

	// Pass 2: apply keys.
	int shaderCount = static_cast<int>(preset.passes.size());
	int feedbackPass = preset.feedbackPass;
	int histories = preset.histories;
	std::string texturesList;
	std::string parametersList;

	// Discover the texture name list first so `name = path` keys (which may
	// appear before or after the `textures =` key) are recognized.
	for (const auto &[key, value] : keyValuePairs) {
		if (key == "textures")
			texturesList = value;
		else if (key == "parameters")
			parametersList = value;
	}
	// Declared texture names, keyed by lowercase form. The original spelling
	// is preserved because GLSL identifiers are case-sensitive: the shader
	// sampler name must keep matching the binding name byte for byte.
	std::map<std::string, std::string> textureNames;
	{
		std::istringstream texStream(texturesList);
		std::string token;
		while (std::getline(texStream, token, ';')) {
			const std::string orig = Trim(token);
			if (!orig.empty())
				textureNames.emplace(ToLower(orig), orig);
		}
	}

	auto textureByName = [&preset](const std::string &name) -> TextureDef * {
		for (TextureDef &t : preset.textures) {
			if (t.name == name)
				return &t;
		}
		preset.textures.emplace_back();
		preset.textures.back().name = name;
		return &preset.textures.back();
	};

	for (const auto &[key, value] : keyValuePairs) {
		if (key == "shaders") {
			shaderCount = std::max(shaderCount, std::max(0, ParseInt(value, 0)));
			continue;
		}
		if (key == "feedback_pass") {
			feedbackPass = ParseInt(value, -1);
			continue;
		}
		if (key == "histories") {
			histories = std::clamp(ParseInt(value, 1), 1, 8);
			continue;
		}
		if (key == "textures" || key == "parameters")
			continue;

		// Texture definition keys.
		static constexpr const char *kTextureKeySuffixes[] = { "_linear", "_wrap_mode", "_mipmap", "_path" };
		bool handledTextureKey = false;
		for (const char *suffix : kTextureKeySuffixes) {
			const size_t suffixLen = std::strlen(suffix);
			if (key.size() > suffixLen && key.compare(key.size() - suffixLen, suffixLen, suffix) == 0) {
				const std::string baseLower = key.substr(0, key.size() - suffixLen);
				const auto found = textureNames.find(baseLower);
				const std::string texName = found != textureNames.end() ? found->second : baseLower;
				TextureDef *tex = textureByName(texName);
				if (suffix == std::string("_linear")) {
					tex->filter = ParseFilterMode(value);
				} else if (suffix == std::string("_wrap_mode")) {
					tex->wrap = ParseWrapMode(value);
				} else if (suffix == std::string("_mipmap")) {
					tex->mipmap = ParseBool(value, false);
				} else {
					tex->path = value;
				}
				handledTextureKey = true;
				break;
			}
		}
		if (handledTextureKey)
			continue;

		// "name = path" for a name declared in the textures list.
		{
			const auto found = textureNames.find(key);
			if (found != textureNames.end()) {
				textureByName(found->second)->path = value;
				continue;
			}
			// Paths may also arrive for texture entries inherited from a
			// #reference parent; match those case-insensitively.
			TextureDef *inherited = nullptr;
			for (TextureDef &tex : preset.textures) {
				if (ToLower(tex.name) == key) {
					inherited = &tex;
					break;
				}
			}
			if (inherited != nullptr && inherited->path.empty()) {
				inherited->path = value;
				continue;
			}
		}

		// Parameter override keys (validated against the parameters list).
		bool isParameter = false;
		if (!parametersList.empty()) {
			std::istringstream paramStream(parametersList);
			std::string token;
			while (std::getline(paramStream, token, ';')) {
				if (ToLower(Trim(token)) == key) {
					isParameter = true;
					break;
				}
			}
		}
		if (isParameter) {
			// Replace an inherited override if present.
			bool replaced = false;
			for (ParamOverride &param : preset.parameters) {
				if (param.name == key) {
					param.value = ParseFloat(value, 0.0F);
					replaced = true;
					break;
				}
			}
			if (!replaced)
				preset.parameters.push_back({ key, ParseFloat(value, 0.0F) });
			continue;
		}
		if (parametersList.empty()) {
			// Overlay files without a `parameters` list (e.g. Mega Bezel
			// .params) still carry numeric values; accept any numeric key as
			// an override. The runner applies only overrides whose name
			// matches a #pragma parameter, so unknown keys stay inert.
			const std::string trimmed = Trim(value);
			char *end = nullptr;
			std::strtof(trimmed.c_str(), &end);
			if (!trimmed.empty() && end != nullptr && end == trimmed.c_str() + trimmed.size()) {
				bool replaced = false;
				for (ParamOverride &param : preset.parameters) {
					if (param.name == key) {
						param.value = ParseFloat(trimmed, 0.0F);
						replaced = true;
						break;
					}
				}
				if (!replaced)
					preset.parameters.push_back({ key, ParseFloat(trimmed, 0.0F) });
				continue;
			}
		}

		std::string baseKey;
		const int passIndex = SplitPassKey(key, baseKey);
		if (passIndex >= 0) {
			if (static_cast<int>(preset.passes.size()) <= passIndex)
				preset.passes.resize(static_cast<size_t>(passIndex) + 1);
			ApplyPassKey(preset.passes[static_cast<size_t>(passIndex)], baseKey, value, preset.dir);
			continue;
		}
		// Implicit texture declaration (RetroArch-compat): a path-valued key
		// with no other meaning declares a texture usable by the shaders.
		if (passIndex < 0 && LooksLikeImagePath(value)) {
			TextureDef *tex = textureByName(key);
			if (tex->path.empty())
				tex->path = value;
			continue;
		}
		// Unknown key: ignore silently (the preset format has harmless extras).
	}

	if (static_cast<int>(preset.passes.size()) < shaderCount)
		preset.passes.resize(static_cast<size_t>(shaderCount));
	preset.passes.erase(std::remove_if(preset.passes.begin(), preset.passes.end(),
	                        [](const PassDef &p) { return p.shaderPath.empty(); }),
	    preset.passes.end());
	if (preset.passes.empty() && !isReferenceTarget) {
		// Reference targets may legitimately be parameter-only overlays
		// (e.g. Mega Bezel .params files); the top-level preset must still
		// end up with a shader chain.
		error = StrCat("preset has no shader passes: ", path);
		return false;
	}
	preset.feedbackPass = feedbackPass;
	preset.histories = histories;

	// Materialize declared texture names and validate that every texture
	// carries a path.
	if (!texturesList.empty()) {
		// Materialize with the original spelling from the list (the map keys
		// are lowercase; the values carry the declared case).
		for (const auto &[_, orig] : textureNames)
			textureByName(orig);
	}
	for (TextureDef &tex : preset.textures) {
		if (tex.name.empty() || tex.path.empty()) {
			error = StrCat("preset texture has no path: ", tex.name.empty() ? "(unnamed)" : tex.name, " (", path, ")");
			return false;
		}
	}

	out = std::move(preset);
	return true;
}

bool ParsePreset(const std::string &path, Preset &out, std::string &error)
{
	return ParsePresetImpl(path, out, error, 0, /*isReferenceTarget=*/false);
}

std::vector<PresetEntry> ScanPresets(const std::string &rootDir)
{
	std::vector<PresetEntry> entries;
	std::error_code ec;
	if (!std::filesystem::is_directory(rootDir, ec))
		return entries;

	constexpr int kMaxDepth = 3;
	std::filesystem::recursive_directory_iterator it(rootDir, std::filesystem::directory_options::skip_permission_denied, ec);
	if (ec)
		return entries;
	std::filesystem::recursive_directory_iterator end;
	for (; it != end; it.increment(ec)) {
		if (ec) {
			it.disable_recursion_pending();
			ec.clear();
			continue;
		}
		const int depth = it.depth();
		if (depth >= kMaxDepth) {
			it.disable_recursion_pending();
		}
		if (!it->is_regular_file(ec) || ec) {
			ec.clear();
			continue;
		}
		const std::filesystem::path &p = it->path();
		if (p.extension() != ".slangp" && p.extension() != ".SLANGP")
			continue;
		PresetEntry entry;
		entry.relativePath = std::filesystem::relative(p, rootDir, ec).generic_string();
		if (ec || entry.relativePath.empty()) {
			ec.clear();
			continue;
		}
		entry.displayName = p.stem().string();
		entries.push_back(std::move(entry));
	}
	std::sort(entries.begin(), entries.end(), [](const PresetEntry &a, const PresetEntry &b) {
		return a.relativePath < b.relativePath;
	});
	return entries;
}

std::string ShaderRootDir()
{
	static std::string cached;
	static bool initialized = false;
	if (!initialized) {
		initialized = true;
		const char *basePath = SDL_GetBasePath();
		if (basePath != nullptr) {
			cached = NormalizeSeparators(std::string(basePath) + "shaders");
		} else {
			cached = "shaders";
		}
	}
	return cached;
}

std::string ShaderCacheDir()
{
	return ShaderRootDir() + "/cache";
}

std::string ResolvePresetPath(const std::string &relativePath)
{
	if (relativePath.empty())
		return {};
	std::filesystem::path p(std::filesystem::path(ShaderRootDir()) / relativePath);
	std::error_code ec;
	if (!std::filesystem::exists(p, ec) || ec)
		return {};
	return NormalizeSeparators(p.string());
}

} // namespace preset
} // namespace gpu_scaler
} // namespace devilution
#endif // !USE_SDL1
#endif // USE_GPU_SCALER
