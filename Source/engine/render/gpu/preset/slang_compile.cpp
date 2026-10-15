#include "engine/render/gpu/preset/slang_compile.hpp"

#ifdef USE_GPU_SCALER
#ifndef USE_SDL1

#include <atomic>
#include <cstdint>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <mutex>

#if defined(_WIN32)
#include <process.h>
#else
#include <unistd.h>
#endif

#include <SPIRV/GlslangToSpv.h>
#include <glslang/Public/ResourceLimits.h>
#include <glslang/Public/ShaderLang.h>

#include "utils/log.hpp"
#include "utils/str_cat.hpp"

// SHA-256 for content-addressed cache keys (header-only, already vendored).
#include <picosha2.h>

namespace devilution {
namespace gpu_scaler {
namespace preset {
namespace {

/** @brief Process-wide glslang lifecycle guard. */
class GlslangRuntime {
public:
	static GlslangRuntime &Instance()
	{
		static GlslangRuntime runtime;
		return runtime;
	}

	GlslangRuntime()
	{
		glslang::InitializeProcess();
	}
	~GlslangRuntime()
	{
		glslang::FinalizeProcess();
	}
	GlslangRuntime(const GlslangRuntime &) = delete;
	GlslangRuntime &operator=(const GlslangRuntime &) = delete;
};

bool CompileStageToSpv(EShLanguage stage, const std::string &source, std::vector<uint8_t> &out, std::string &error)
{
	GlslangRuntime::Instance();

	glslang::TShader shader(stage);
	const char *sourcePtr = source.c_str();
	shader.setStrings(&sourcePtr, 1);
	shader.setEnvInput(glslang::EShSourceGlsl, stage, glslang::EShClientVulkan, 100);
	shader.setEnvClient(glslang::EShClientVulkan, glslang::EShTargetVulkan_1_0);
	shader.setEnvTarget(glslang::EShTargetSpv, glslang::EShTargetSpv_1_0);
	const EShMessages messages = static_cast<EShMessages>(EShMsgSpvRules | EShMsgVulkanRules);
	if (!shader.parse(GetDefaultResources(), 450, false, messages)) {
		error = StrCat("glslang parse failed (", stage == EShLangVertex ? "vertex" : "fragment", "): ",
		    shader.getInfoLog());
		return false;
	}
	glslang::TProgram program;
	program.addShader(&shader);
	if (!program.link(EShMsgDefault)) {
		error = StrCat("glslang link failed: ", program.getInfoLog());
		return false;
	}
	std::vector<unsigned int> spirv;
	glslang::GlslangToSpv(*program.getIntermediate(stage), spirv);
	out.resize(spirv.size() * sizeof(uint32_t));
	std::memcpy(out.data(), spirv.data(), out.size());
	return true;
}

bool ReadFileBytes(const std::string &path, std::vector<uint8_t> &out)
{
	std::ifstream stream(path, std::ios::binary);
	if (!stream)
		return false;
	std::ostringstream ss;
	ss << stream.rdbuf();
	const std::string str = ss.str();
	out.assign(str.begin(), str.end());
	return true;
}

/** @brief Per-process suffix for cache temp files, so two concurrently
 * compiling processes never share a temp blob (a shared name could let one
 * process rename away the other's half-written file, ROB-03). */
std::string ProcessIdSuffix()
{
#if defined(_WIN32)
	return StrCat(".", static_cast<long>(_getpid()), ".tmp");
#else
	return StrCat(".", static_cast<long>(getpid()), ".tmp");
#endif
}

// ROB-03: the cache blob is written to a process-unique temp file first and
// then renamed into place. std::filesystem::rename is atomic on POSIX and on
// Windows (same volume), so a crash, disk-full condition, or concurrent
// compile can no longer leave a half-written file at the final path — the
// previous write-in-place could produce a torn blob whose intact first bytes
// then passed SpvLooksValid forever (a permanent poisoned cache entry).
bool WriteFileBytes(const std::string &path, const std::vector<uint8_t> &data)
{
	std::error_code ec;
	const std::filesystem::path parent = std::filesystem::path(path).parent_path();
	std::filesystem::create_directories(parent, ec);
	const std::filesystem::path tmp = std::filesystem::path(path).string() + ProcessIdSuffix();
	{
		std::ofstream stream(tmp, std::ios::binary | std::ios::trunc);
		if (!stream)
			return false;
		stream.write(reinterpret_cast<const char *>(data.data()), static_cast<std::streamsize>(data.size()));
		stream.flush();
		if (!stream.good()) {
			std::filesystem::remove(tmp, ec);
			return false;
		}
	}
	// Windows rename refuses to overwrite an existing target; POSIX replaces
	// it. Removing first makes both behave identically (a failed remove of a
	// nonexistent target is harmless; a reader holding the old blob open is
	// unaffected — it reads the previous generation).
	std::filesystem::remove(path, ec);
	std::filesystem::rename(tmp, path, ec);
	if (ec) {
		std::error_code removeEc;
		std::filesystem::remove(tmp, removeEc);
		return false;
	}
	return true;
}

bool SpvLooksValid(const std::vector<uint8_t> &data)
{
	// SPIR-V magic number 0x07230203, little-endian.
	static constexpr uint8_t kMagic[4] = { 0x03, 0x02, 0x23, 0x07 };
	return data.size() >= 20 && data.size() % 4 == 0 && std::memcmp(data.data(), kMagic, 4) == 0;
}

} // namespace

namespace {

// ROB-04: cache format version. Bump whenever the compile pipeline can change
// the SPIR-V output for identical input GLSL: a glslang upgrade (the
// FetchContent-pinned 3rdParty/glslang tarball), a target env change in
// CompileStageToSpv, or a cache layout change. It is mixed into the hashed
// payload AND spelled out in the file name, so stale blobs are never served
// and remain name-prunable later (see PERF-05).
constexpr int kSpirvCacheVersion = 2;

/** @brief The pinned glslang version, mixed into the cache key (ROB-04). */
std::string GlslangVersionSalt()
{
#if defined(GLSLANG_VERSION_MAJOR)
	return StrCat(GLSLANG_VERSION_MAJOR, ".", GLSLANG_VERSION_MINOR, ".", GLSLANG_VERSION_PATCH);
#else
	return "unknown";
#endif
}

} // namespace

void CachePathFor(const SlangTranslation &translation, const std::string &slangPath, const std::string &cacheDir,
    std::string &vertexPath, std::string &fragmentPath)
{
	vertexPath.clear();
	fragmentPath.clear();
	if (cacheDir.empty())
		return;
	// Content-addressed key: any change to the .slang source, its includes
	// or the translator's output changes the hash, so stale SPIR-V is never
	// served from translator-visible changes. ROB-04: the toolchain is NOT
	// translator-visible, so the cache format version and the glslang
	// version are mixed into the payload too — a glslang bump recompiles
	// every pass instead of silently serving old codegen indefinitely (the
	// cache survives binary updates as user data). The version is also in
	// the file name so stale entries stay prunable by name.
	const std::string payload = StrCat("devilutionx-gpu-preset-spirv\x1fv", kSpirvCacheVersion,
	    "\x1fglslang-", GlslangVersionSalt(), "\x1f", translation.vertex.glsl, "\x1f", translation.fragment.glsl);
	std::string hash;
	picosha2::hash256_hex_string(payload, hash);
	const std::string stem = std::filesystem::path(slangPath).stem().string();
	const std::string base = StrCat(cacheDir, "/slang-", stem.empty() ? "pass" : stem, "-v", kSpirvCacheVersion, "-",
	    hash.substr(0, 24));
	vertexPath = base + ".vert.spv";
	fragmentPath = base + ".frag.spv";
}

void SidecarSpvPathsFor(const std::string &slangPath, std::string &vertexPath, std::string &fragmentPath)
{
	std::filesystem::path p(slangPath);
	vertexPath = (p.parent_path() / (p.stem().string() + ".vert.spv")).string();
	fragmentPath = (p.parent_path() / (p.stem().string() + ".frag.spv")).string();
}

bool CompilePass(const SlangTranslation &translation, const std::string &slangPath, const std::string &cacheDir,
    PassSpirv &out, std::string &error, SpirvSource *outSource, bool allowCache)
{
	out.vertex.clear();
	out.fragment.clear();
	if (outSource != nullptr)
		*outSource = SpirvSource::Compiled;

	if (!translation.vertex.present || !translation.fragment.present) {
		error = "slang: pass is missing a required stage";
		return false;
	}

	// 1. Precompiled sidecar files next to the source always win.
	std::string sidecarVert, sidecarFrag;
	SidecarSpvPathsFor(slangPath, sidecarVert, sidecarFrag);
	if (ReadFileBytes(sidecarVert, out.vertex) && SpvLooksValid(out.vertex) && ReadFileBytes(sidecarFrag, out.fragment)
	    && SpvLooksValid(out.fragment)) {
		Log("GPU scaler preset: using precompiled SPIR-V sidecars for {}", slangPath);
		if (outSource != nullptr)
			*outSource = SpirvSource::Sidecar;
		return true;
	}

	// 2. Disk cache keyed by a hash of the translated sources + toolchain salt.
	std::string cacheVert, cacheFrag;
	CachePathFor(translation, slangPath, cacheDir, cacheVert, cacheFrag);
	if (allowCache && !cacheVert.empty() && ReadFileBytes(cacheVert, out.vertex) && SpvLooksValid(out.vertex)
	    && ReadFileBytes(cacheFrag, out.fragment) && SpvLooksValid(out.fragment)) {
		Log("GPU scaler preset: cache hit for {}", slangPath);
		if (outSource != nullptr)
			*outSource = SpirvSource::Cache;
		return true;
	}

	// 3. Compile with glslang.
	if (!CompileStageToSpv(EShLangVertex, translation.vertex.glsl, out.vertex, error))
		return false;
	if (!CompileStageToSpv(EShLangFragment, translation.fragment.glsl, out.fragment, error))
		return false;

	// 4. Store in the cache (best-effort; skipped on the ROB-03 no-cache retry).
	if (allowCache && !cacheVert.empty()) {
		if (!WriteFileBytes(cacheVert, out.vertex) || !WriteFileBytes(cacheFrag, out.fragment))
			Log("GPU scaler preset: could not write SPIR-V cache files for {}", slangPath);
	}
	return true;
}

void DeleteCachedSpv(const SlangTranslation &translation, const std::string &slangPath, const std::string &cacheDir)
{
	std::string vertexPath, fragmentPath;
	CachePathFor(translation, slangPath, cacheDir, vertexPath, fragmentPath);
	std::error_code ec;
	if (!vertexPath.empty())
		std::filesystem::remove(vertexPath, ec);
	if (!fragmentPath.empty())
		std::filesystem::remove(fragmentPath, ec);
}

} // namespace preset
} // namespace gpu_scaler
} // namespace devilution
#endif // !USE_SDL1
#endif // USE_GPU_SCALER
