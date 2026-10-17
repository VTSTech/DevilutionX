#include "engine/render/gpu/gpu_scaler.hpp"

#ifdef USE_GPU_SCALER
#ifndef USE_SDL1

#include <SDL3/SDL_filesystem.h>
#include <SDL3/SDL_gpu.h>
#include <SDL3/SDL_log.h>
#include <SDL3/SDL_properties.h>
#include <SDL3/SDL_timer.h>
#include <SDL3/SDL_video.h>

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <memory>
#include <mutex>
#include <string>
#include <utility>
#include <vector>

#include "config.h"
#ifdef GPU_SCALER_SPV_GENERATED
#include "engine/render/gpu/gpu_shader_spv.hpp"
#else
#include "engine/render/gpu/gpu_shader_spv_prebuilt.hpp"
#endif
#include "engine/render/gpu/gpu_log_sanitize.hpp"
#include "engine/render/gpu/preset/preset_parser.hpp"
#include "engine/render/gpu/preset/preset_runner.hpp"
#include "engine/render/text_render.hpp"
#include "engine/surface.hpp"
#include "utils/log.hpp"
#include "utils/str_cat.hpp"

namespace devilution {
namespace gpu_scaler {
namespace {

struct alignas(16) FragUniforms {
	float sourceSize[4]; // (w, h, 1/w, 1/h)
	float params[4];
};

enum class Program {
	Vertex,
	Blit,
};

/** @brief Slot count of the fragment shader table: one slot per live Program
 *  index. Sized by the live program set, not by the removed built-in filter
 *  set (which once needed 9), so resizing the enum trips a compile error
 *  here instead of silently changing the table bounds. */
constexpr int kMaxFragmentPrograms = static_cast<int>(Program::Blit) + 1;

SDL_GPUDevice *gDevice = nullptr;
SDL_Window *gWindow = nullptr;
SDL_GPUShaderFormat gShaderFormat = SDL_GPU_SHADERFORMAT_INVALID;
ShaderFilter gFilter = ShaderFilter::None;

int gGameWidth = 0;
int gGameHeight = 0;

SDL_GPUShader *gVertexShader = nullptr;
SDL_GPUShader *gFragmentShaders[kMaxFragmentPrograms] = {};

SDL_GPUGraphicsPipeline *gBlitPipeline = nullptr;

SDL_GPUTexture *gGameTex = nullptr;

// Runtime libretro slang preset chain (custom shader filters).
preset::Runner gPresetRunner;
std::string gPresetDisplayName;
// The preset file the running chain was built from (resolved path).
std::string gLoadedPresetPath;
// FEAT-01: the file stat snapshot taken when the chain was built. A non-zero
// mtime enables the changed-on-disk reload check in EnsurePresetRunner; zero
// means the file could not be statted at load time and the chain never
// auto-reloads.
Uint64 gLoadedPresetMtimeNs = 0;
Uint64 gLoadedPresetFileSize = 0;
// FEAT-03: why the last preset load attempt failed (full text; the OSD shows
// its first line). Deliberately survives the failure Shutdown so the OSD can
// name the reason; overwritten by the next attempt, cleared on success.
std::string gPresetLastError;

// FEAT-01: clears the loaded-chain identity wherever the chain goes away.
void ClearLoadedPreset()
{
	gLoadedPresetPath.clear();
	gLoadedPresetMtimeNs = 0;
	gLoadedPresetFileSize = 0;
}

SDL_GPUSampler *gNearestSampler = nullptr;
SDL_GPUSampler *gLinearSampler = nullptr;

bool gLoggedFailure = false;

// Set when the scaler hits an unrecoverable error. Session-sticky: the game
// reverts to the regular renderer instead of retrying a broken GPU path.
bool gFailed = false;
// ARCH-01: the window-presentation hand-off protocol, in one place.
//
// On Windows, DXGI permits only one flip-model swapchain per HWND, so the SDL
// window alternates ownership between SDL_Renderer (D3D11/GL) and the SDL_GPU
// device (Vulkan/D3D12). A window presented to by EITHER path keeps that
// swapchain history even after its owner releases it, and the next owner of
// the OTHER kind fails with "Access is denied" (0x80070005) until the window
// is destroyed and recreated (display.cpp RecreateMainRenderWindow).
//
// Protocol (three participants):
//   - gpu_scaler.cpp (here): records the ID of every window it successfully
//     claims (SDL_ClaimWindowForGPUDevice) and answers
//     WindowHasSwapchainHistory() for display.cpp. The ID is deliberately
//     NOT cleared on Shutdown(): the (possibly still alive) window keeps its
//     DXGI swapchain history.
//   - display.cpp: before handing the window across paths, recreates it when
//     WindowHasSwapchainHistory() is true (scaler -> renderer direction) or
//     when the renderer owned it (renderer -> scaler direction).
//   - dx.cpp: the failure funnel that routes any scaler error back to the
//     stock renderer for the session.
//
// The history is tracked as explicit state keyed by the SDL window ID, not
// by the SDL_Window pointer: SDL never reuses a window ID within a process,
// so a destroyed window's recorded ID can never match a NEW window that
// happened to be allocated at the recycled address (the ABA false-positive
// the previous pointer-only approach allowed — it forced an unnecessary
// window recreation and could not distinguish "this window had a swapchain"
// from "some window at this address did"). The recorded ID is only ever
// compared, never dereferenced (the window may already be destroyed).
Uint32 gLastClaimedWindowId = 0;
// Format the current blit pipeline was built for (swapchain format can change).
SDL_GPUTextureFormat gBlitPipelineFormat = SDL_GPU_TEXTUREFORMAT_INVALID;

// SDL_Log output is invisible for a GUI-subsystem application on Windows, so
// GPU scaler diagnostics are also appended to gpu-scaler.log next to the
// executable. The file opens lazily on first use and is size-capped: when a
// write would push it past kMaxLogBytes, the current log is rotated to
// gpu-scaler.log.old (one generation kept) and a fresh file starts.
constexpr long kMaxLogBytes = 200 * 1024;

FILE *gLogFile = nullptr;
// Bytes currently in the log file, kept in step with the writes below so
// that the cap check does not have to stat the file on every line.
long gLogBytes = 0;

// ROB-06: every mutation of gLogFile / gLogBytes (open, rotate, write) must
// hold this mutex. SDL log capture routes SDL's own messages here, and those
// can originate on SDL-internal threads (GPU, audio, validation), so the
// capture callback races the main-thread LogFileLine writes. Without the
// mutex the worst case was use-after-close of the FILE* or a double rotation
// — exactly when users need the log most (an SDL-internal error storm).
// Cheap at this call rate; the file is only touched for diagnostics.
std::mutex gLogMutex;

void InstallSdlLogCapture();

std::string LogFilePath()
{
	const char *basePath = SDL_GetBasePath();
	// SDL3 owns the returned string; it must not be freed.
	return basePath != nullptr ? std::string(basePath) + "gpu-scaler.log" : std::string("gpu-scaler.log");
}

long LogFileSize(const std::string &path)
{
	FILE *file = std::fopen(path.c_str(), "rb");
	if (file == nullptr)
		return 0;
	long size = 0;
	if (std::fseek(file, 0, SEEK_END) == 0)
		size = std::ftell(file);
	std::fclose(file);
	return size > 0 ? size : 0;
}

// Moves path aside as path.old (one generation kept). When the rename is
// refused (e.g. a viewer holding the .old file on Windows), the current
// file is removed instead so the cap holds either way.
void RotateLogFile(const std::string &path)
{
	const std::string oldPath = path + ".old";
	std::remove(oldPath.c_str());
	if (std::rename(path.c_str(), oldPath.c_str()) != 0)
		std::remove(path.c_str());
}

// --- log-file internals (LogFileClose / LogOpenAppended / RotateLog / LogFile /
// LogFileForWrite): the caller must hold gLogMutex. ---

void LogClose()
{
	if (gLogFile != nullptr) {
		std::fclose(gLogFile);
		gLogFile = nullptr;
	}
}

FILE *LogOpenAppended()
{
	const std::string path = LogFilePath();
	// A leftover log from previous sessions must not push this session's
	// file past the cap: rotate oversized leftovers once at open.
	if (LogFileSize(path) > kMaxLogBytes)
		RotateLogFile(path);
	FILE *file = std::fopen(path.c_str(), "a");
	if (file != nullptr && std::fseek(file, 0, SEEK_END) == 0) {
		const long size = std::ftell(file);
		gLogBytes = size > 0 ? size : 0;
	} else {
		gLogBytes = 0;
	}
	return file;
}

// Starts a fresh log mid-session, keeping the current one as
// gpu-scaler.log.old.
void RotateLog()
{
	LogClose();
	RotateLogFile(LogFilePath());
	gLogBytes = 0;
	gLogFile = std::fopen(LogFilePath().c_str(), "a");
	if (gLogFile != nullptr) {
		const std::string marker = StrCat("=== gpu-scaler.log rotated (", kMaxLogBytes / 1024, " KB cap) ===\n");
		std::fwrite(marker.data(), 1, marker.size(), gLogFile);
		gLogBytes += static_cast<long>(marker.size());
		std::fflush(gLogFile);
	}
}

FILE *LogFile()
{
	static bool initialized = false;
	if (!initialized) {
		initialized = true;
		gLogFile = LogOpenAppended();
		if (gLogFile != nullptr) {
			if (gLogBytes + 64 > kMaxLogBytes)
				RotateLog(); // keep even the session header within the cap
			if (gLogFile != nullptr) {
				const std::string header = StrCat("\n=== GPU scaler session (build ", __DATE__, " ", __TIME__, ", exe ", PROJECT_VERSION, ") ===\n");
				std::fwrite(header.data(), 1, header.size(), gLogFile);
				gLogBytes += static_cast<long>(header.size());
				std::fflush(gLogFile);
			}
			InstallSdlLogCapture();
		}
	}
	return gLogFile;
}

// Returns the log file for a write of `incomingBytes` more bytes, rotating
// it first when that write would push the file past the size cap. Returns
// nullptr when the log is unavailable.
FILE *LogFileForWrite(size_t incomingBytes)
{
	if (LogFile() == nullptr)
		return nullptr;
	if (gLogBytes + static_cast<long>(incomingBytes) > kMaxLogBytes)
		RotateLog();
	return gLogFile;
}

void LogFileLine(std::string_view line)
{
	// SEC-04: log text includes untrusted strings (preset paths, pragma
	// descriptions, glslang info logs). Control bytes would let a hostile
	// preset forge log lines or inject terminal escapes. Sanitized outside
	// the lock (pure string work), then written under the lock so the
	// rotation state cannot change between the cap check and the write.
	const std::string safe = SanitizeLogLine(line);
	const std::lock_guard<std::mutex> lock(gLogMutex);
	if (FILE *file = LogFileForWrite(line.size() + 16); file != nullptr) {
		std::fprintf(file, "[%10u ms] %.*s\n", static_cast<unsigned>(SDL_GetTicks()), static_cast<int>(safe.size()), safe.data());
		std::fflush(file);
	}
}

void LogOk(std::string_view what)
{
	LogFileLine(StrCat("ok: ", what));
}

void LogFail(std::string_view what)
{
	if (!gLoggedFailure) {
		gLoggedFailure = true;
		LogError("GPU scaler: {}: {} (further GPU scaler errors will be suppressed)", what, SDL_GetError());
	}
	LogFileLine(StrCat("ERROR: ", what, " | SDL_GetError: ", SDL_GetError()));
}

// A failure that cannot be recovered from within the GPU path: the scaler is
// torn down and the game falls back to the regular renderer for this session.
void LogFailHard(std::string_view what)
{
	gFailed = true;
	LogFail(what);
	LogFileLine("GPU scaler disabled for this session (hard failure)");
}

// A frame aborted after the swapchain texture was already acquired for
// this command buffer: SDL forbids cancelling it in that state (debug
// mode asserts "Cannot cancel command buffer after a swapchain texture
// has been acquired!", release leaves the acquired image unpresented
// and can wedge the swapchain). Submit the empty command buffer instead:
// the skip presents the frame's swapchain image untouched and the game
// either retries next frame or has already hard-failed into the stock
// renderer.
void SubmitSkipAfterAcquire(SDL_GPUCommandBuffer *cmd)
{
	if (!SDL_SubmitGPUCommandBuffer(cmd))
		LogFail("swapchain-skip submit failed");
}

// SDL's own log output (adapter info, validation errors, device removal,
// backend initialization) is invisible for a GUI-subsystem application on
// Windows. Chain it into the diagnostic file as well.
SDL_LogOutputFunction gPrevSdlLogFunction = nullptr;
void *gPrevSdlLogUserdata = nullptr;

void SDLCALL SdlLogCapture(void *userdata, int category, SDL_LogPriority priority, const char *message)
{
	// Chain to the previous output function BEFORE taking gLogMutex: the
	// previous handler is arbitrary user code and must never observe (or
	// recurse into) our log lock, and SDL-internal messages can originate
	// on SDL's own threads (ROB-06) — the write below is what needs the
	// mutual exclusion against the main-thread LogFileLine writers.
	if (gPrevSdlLogFunction != nullptr)
		gPrevSdlLogFunction(gPrevSdlLogUserdata, category, priority, message);
	const size_t messageLen = message != nullptr ? std::strlen(message) : 0;
	const std::lock_guard<std::mutex> lock(gLogMutex);
	if (FILE *file = LogFileForWrite(messageLen + 16); file != nullptr) {
		std::fprintf(file, "[       SDL] %s\n", message != nullptr ? message : "");
		std::fflush(file);
	}
}

void InstallSdlLogCapture()
{
	if (gPrevSdlLogFunction != nullptr)
		return;
	SDL_GetLogOutputFunction(&gPrevSdlLogFunction, &gPrevSdlLogUserdata);
	SDL_SetLogOutputFunction(SdlLogCapture, nullptr);
}

struct ShaderCode {
	const uint8_t *data = nullptr;
	size_t size = 0;
};

ShaderCode SpvFor(Program program)
{
	switch (program) {
	case Program::Vertex: return { gpu::spv::kVertex, sizeof(gpu::spv::kVertex) };
	case Program::Blit: return { gpu::spv::kBlit, sizeof(gpu::spv::kBlit) };
	}
	return { nullptr, 0 };
}

int SamplersFor(Program program)
{
	switch (program) {
	case Program::Vertex: return 0;
	case Program::Blit: return 1;
	}
	return 0;
}

const char *NameFor(Program program)
{
	switch (program) {
	case Program::Vertex: return "vertex";
	case Program::Blit: return "blit";
	}
	return "unknown";
}

bool CreateShader(Program program, SDL_GPUShaderStage stage, SDL_GPUShader **out)
{
	SDL_GPUShaderCreateInfo info {};
	info.stage = stage;
	info.entrypoint = "main";
	info.num_samplers = (stage == SDL_GPU_SHADERSTAGE_FRAGMENT) ? SamplersFor(program) : 0;
	info.num_uniform_buffers = (stage == SDL_GPU_SHADERSTAGE_FRAGMENT) ? 1 : 0;

	switch (gShaderFormat) {
	case SDL_GPU_SHADERFORMAT_SPIRV: {
		const ShaderCode code = SpvFor(program);
		info.code = code.data;
		info.code_size = code.size;
		info.format = SDL_GPU_SHADERFORMAT_SPIRV;
	} break;
	default:
		return false;
	}

	*out = SDL_CreateGPUShader(gDevice, &info);
	if (*out == nullptr) {
		LogFail(StrCat("shader creation failed ('", NameFor(program), "')"));
		return false;
	}
	LogOk(StrCat("shader '", NameFor(program), "' created"));
	return true;
}

SDL_GPUTexture *CreateTexture(SDL_GPUTextureFormat format, Uint32 width, Uint32 height, SDL_GPUTextureUsageFlags usage)
{
	SDL_GPUTextureCreateInfo info {};
	info.type = SDL_GPU_TEXTURETYPE_2D;
	info.format = format;
	info.usage = usage;
	info.width = width;
	info.height = height;
	info.layer_count_or_depth = 1;
	info.num_levels = 1;
	info.sample_count = SDL_GPU_SAMPLECOUNT_1;
	SDL_GPUTexture *texture = SDL_CreateGPUTexture(gDevice, &info);
	if (texture == nullptr)
		LogFail("texture creation failed");
	return texture;
}

SDL_GPUTextureFormat GameTextureFormat()
{
	switch (DEVILUTIONX_DISPLAY_TEXTURE_FORMAT) {
	case SDL_PIXELFORMAT_ABGR8888:
		return SDL_GPU_TEXTUREFORMAT_R8G8B8A8_UNORM;
	case SDL_PIXELFORMAT_ARGB8888:
	case SDL_PIXELFORMAT_XRGB8888:
	default:
		return SDL_GPU_TEXTUREFORMAT_B8G8R8A8_UNORM;
	}
}

SDL_GPUGraphicsPipeline *CreatePipeline(Program fragmentProgram, SDL_GPUTextureFormat targetFormat)
{
	if (gVertexShader == nullptr)
		return nullptr;
	SDL_GPUShader *fragmentShader = gFragmentShaders[static_cast<int>(fragmentProgram)];
	if (fragmentShader == nullptr)
		return nullptr;
	if (targetFormat == SDL_GPU_TEXTUREFORMAT_INVALID)
		return nullptr;

	SDL_GPUColorTargetDescription targetDesc {};
	targetDesc.format = targetFormat;

	SDL_GPUGraphicsPipelineCreateInfo info {};
	info.vertex_shader = gVertexShader;
	info.fragment_shader = fragmentShader;
	info.primitive_type = SDL_GPU_PRIMITIVETYPE_TRIANGLELIST;
	info.rasterizer_state.fill_mode = SDL_GPU_FILLMODE_FILL;
	info.rasterizer_state.cull_mode = SDL_GPU_CULLMODE_NONE;
	info.rasterizer_state.front_face = SDL_GPU_FRONTFACE_COUNTER_CLOCKWISE;
	info.multisample_state.sample_count = SDL_GPU_SAMPLECOUNT_1;
	// SDL 3.5: sample_mask is reserved and must stay 0 (SDL_gpu.c
	// release-asserts and rejects the pipeline otherwise when the device
	// was created with debug mode). With sample_count 1 the default mask
	// already covers every sample; the old 0xFFFFFFFF form is stale.
	info.depth_stencil_state.compare_op = SDL_GPU_COMPAREOP_ALWAYS;
	info.depth_stencil_state.front_stencil_state.fail_op = SDL_GPU_STENCILOP_KEEP;
	info.depth_stencil_state.front_stencil_state.pass_op = SDL_GPU_STENCILOP_KEEP;
	info.depth_stencil_state.front_stencil_state.depth_fail_op = SDL_GPU_STENCILOP_KEEP;
	info.depth_stencil_state.front_stencil_state.compare_op = SDL_GPU_COMPAREOP_ALWAYS;
	info.depth_stencil_state.back_stencil_state = info.depth_stencil_state.front_stencil_state;
	info.target_info.color_target_descriptions = &targetDesc;
	info.target_info.num_color_targets = 1;

	SDL_GPUGraphicsPipeline *pipeline = SDL_CreateGPUGraphicsPipeline(gDevice, &info);
	if (pipeline == nullptr)
		LogFail(StrCat("pipeline creation failed ('", NameFor(fragmentProgram), "')"));
	else
		LogOk(StrCat("pipeline '", NameFor(fragmentProgram), "' created"));
	return pipeline;
}

bool EnsureSamplers()
{
	if (gNearestSampler != nullptr)
		return true;

	SDL_GPUSamplerCreateInfo samplerInfo {};
	samplerInfo.min_filter = SDL_GPU_FILTER_NEAREST;
	samplerInfo.mag_filter = SDL_GPU_FILTER_NEAREST;
	samplerInfo.mipmap_mode = SDL_GPU_SAMPLERMIPMAPMODE_NEAREST;
	samplerInfo.address_mode_u = SDL_GPU_SAMPLERADDRESSMODE_CLAMP_TO_EDGE;
	samplerInfo.address_mode_v = SDL_GPU_SAMPLERADDRESSMODE_CLAMP_TO_EDGE;
	samplerInfo.address_mode_w = SDL_GPU_SAMPLERADDRESSMODE_CLAMP_TO_EDGE;
	samplerInfo.max_anisotropy = 1.0F;
	samplerInfo.min_lod = 0.0F;
	samplerInfo.max_lod = 1000.0F;
	gNearestSampler = SDL_CreateGPUSampler(gDevice, &samplerInfo);

	SDL_GPUSamplerCreateInfo linearInfo = samplerInfo;
	linearInfo.min_filter = SDL_GPU_FILTER_LINEAR;
	linearInfo.mag_filter = SDL_GPU_FILTER_LINEAR;
	gLinearSampler = SDL_CreateGPUSampler(gDevice, &linearInfo);

	if (gNearestSampler == nullptr || gLinearSampler == nullptr) {
		LogFail("sampler creation failed");
		return false;
	}
	return true;
}

bool EnsureBlitPipeline(SDL_GPUTextureFormat format)
{
	if (gBlitPipeline != nullptr && gBlitPipelineFormat == format)
		return true;
	if (gBlitPipeline != nullptr) {
		SDL_ReleaseGPUGraphicsPipeline(gDevice, gBlitPipeline);
		gBlitPipeline = nullptr;
		gBlitPipelineFormat = SDL_GPU_TEXTUREFORMAT_INVALID;
	}
	gBlitPipeline = CreatePipeline(Program::Blit, format);
	if (gBlitPipeline == nullptr) {
		LogFail("blit pipeline creation failed");
		return false;
	}
	gBlitPipelineFormat = format;
	LogFileLine(StrCat("blit pipeline ready, swapchain format=", static_cast<int>(format)));
	return true;
}

void ReleaseFilterResources()
{
	gBlitPipelineFormat = SDL_GPU_TEXTUREFORMAT_INVALID;
	if (gVertexShader != nullptr) {
		SDL_ReleaseGPUShader(gDevice, gVertexShader);
		gVertexShader = nullptr;
	}
	for (auto *&shader : gFragmentShaders) {
		if (shader != nullptr) {
			SDL_ReleaseGPUShader(gDevice, shader);
			shader = nullptr;
		}
	}
	if (gBlitPipeline != nullptr) {
		SDL_ReleaseGPUGraphicsPipeline(gDevice, gBlitPipeline);
		gBlitPipeline = nullptr;
	}
}

bool EnsureShaders()
{
	if (gVertexShader != nullptr)
		return true;
	if (!CreateShader(Program::Vertex, SDL_GPU_SHADERSTAGE_VERTEX, &gVertexShader))
		return false;
	static constexpr Program kFragmentPrograms[] = {
		Program::Blit
	};
	for (const Program program : kFragmentPrograms) {
		if (!CreateShader(program, SDL_GPU_SHADERSTAGE_FRAGMENT, &gFragmentShaders[static_cast<int>(program)]))
			return false;
	}
	return true;
}

// Parses, translates, compiles and uploads the runtime preset selected in the
// options. The preset chain is fully independent of the baked-in filter
// pipelines; a failure here is a soft failure (the session falls back per the
// regular contract).
bool EnsurePresetRunner()
{
	// The selected preset file: the path stored alongside the filter value
	// is the single source of truth. The filter value itself is only an
	// index into the browser's CURRENT folder listing, so it must never be
	// resolved against a full-tree scan -- that loaded unrelated presets
	// from other folders.
	std::string path;
	const std::string &storedPath = GetOptions().Graphics.shaderFilterPreset.value;
	if (!storedPath.empty())
		path = preset::ResolvePresetPath(storedPath);
	if (path.empty()) {
		// FEAT-03: remember why the attempt failed so the OSD can surface it.
		if (storedPath.empty())
			gPresetLastError = "no preset path is stored for the selected custom filter";
		else
			gPresetLastError = StrCat("preset file not found in the shaders folder: ", storedPath);
		LogFileLine(StrCat("preset load failed: ", gPresetLastError));
		return false;
	}
	if (gPresetRunner.IsLoaded()) {
		if (gLoadedPresetPath == path) {
			// FEAT-01: re-selecting the same preset rechecks the stat snapshot
			// taken at load time; a changed mtime or size reloads the chain, so
			// pack edits and failed-load retries land without detouring through
			// another preset. A stat that fails here (file briefly unavailable)
			// keeps the running chain.
			SDL_PathInfo info;
			if (gLoadedPresetMtimeNs == 0 || !SDL_GetPathInfo(path.c_str(), &info)
			    || (info.modify_time == static_cast<SDL_Time>(gLoadedPresetMtimeNs)
			        && info.size == gLoadedPresetFileSize))
				return true;
			LogFileLine(StrCat("preset file changed on disk; reloading: ", path));
		} else {
			// The selection moved to a different preset: rebuild the chain from
			// scratch instead of presenting the previously loaded one.
			LogFileLine(StrCat("preset changed: ", gLoadedPresetPath, " -> ", path));
		}
		gPresetRunner.Unload();
		ClearLoadedPreset();
	}

	preset::Preset preset;
	std::string error;
	if (!preset::ParsePreset(path, preset, error)) {
		gPresetLastError = error;
		LogFileLine(StrCat("preset parse failed: ", error));
		return false;
	}
	gPresetDisplayName = preset.displayName;

	const char *driver = SDL_GetGPUDeviceDriver(gDevice);
	if (!gPresetRunner.Load(gDevice, driver != nullptr ? driver : "", preset, error)) {
		gPresetLastError = error;
		LogFileLine(StrCat("preset load failed (", preset.displayName, "): ", error));
		gPresetRunner.Unload();
		ClearLoadedPreset();
		return false;
	}
	gLoadedPresetPath = path;
	// FEAT-01: snapshot the file's mtime/size at load time - the anchor the
	// changed-on-disk reload check compares against.
	SDL_PathInfo info;
	gLoadedPresetMtimeNs = 0;
	gLoadedPresetFileSize = 0;
	if (SDL_GetPathInfo(path.c_str(), &info)) {
		gLoadedPresetMtimeNs = static_cast<Uint64>(info.modify_time);
		gLoadedPresetFileSize = info.size;
	}
	gPresetLastError.clear();
	LogOk(StrCat("preset '", preset.displayName, "' loaded (", preset.passes.size(), " passes)"));
	return true;
}

bool EnsureFilterResources(ShaderFilter filter)
{
	if (!EnsureSamplers() || !EnsureShaders())
		return false;
	LogOk("samplers and shaders ready");

	if (IsCustomFilter(filter)) {
		if (!EnsurePresetRunner())
			return false;
	}

	if (gBlitPipeline == nullptr) {
		const SDL_GPUTextureFormat swapchainFormat = SDL_GetGPUSwapchainTextureFormat(gDevice, gWindow);
		if (swapchainFormat == SDL_GPU_TEXTUREFORMAT_INVALID) {
			// The swapchain is not ready yet (claimed but never
			// presented to). Build the blit pipeline lazily on the
			// first present instead of guessing the format here.
			LogFileLine("swapchain format unavailable at init; deferring blit pipeline creation");
			return true;
		}
		if (!EnsureBlitPipeline(swapchainFormat))
			return false;
	}
	return true;
}

void ReleaseFrameResources()
{
	for (SDL_GPUTexture **texture : { &gGameTex }) {
		if (*texture != nullptr) {
			SDL_ReleaseGPUTexture(gDevice, *texture);
			*texture = nullptr;
		}
	}
	gGameWidth = 0;
	gGameHeight = 0;
}

bool EnsureFrameResources(int gameWidth, int gameHeight)
{
	if (gGameTex != nullptr && gGameWidth == gameWidth && gGameHeight == gameHeight)
		return true;
	ReleaseFrameResources();
	gGameWidth = gameWidth;
	gGameHeight = gameHeight;

	const SDL_GPUTextureFormat gameFormat = GameTextureFormat();
	gGameTex = CreateTexture(gameFormat, gameWidth, gameHeight,
	    SDL_GPU_TEXTUREUSAGE_SAMPLER | SDL_GPU_TEXTUREUSAGE_COLOR_TARGET);
	if (gGameTex == nullptr) {
		LogFailHard("frame resource creation failed");
		ReleaseFrameResources();
		return false;
	}
	return true;
}

bool RenderPass(SDL_GPUCommandBuffer *cmd, SDL_GPUTexture *target, Uint32 targetWidth, Uint32 targetHeight,
    SDL_GPUGraphicsPipeline *pipeline, const FragUniforms &uniforms,
    const SDL_GPUTextureSamplerBinding *bindings, Uint32 numBindings,
    const SDL_GPUViewport *viewport)
{
	SDL_GPUColorTargetInfo colorInfo {};
	colorInfo.texture = target;
	colorInfo.clear_color = { 0.0F, 0.0F, 0.0F, 1.0F };
	colorInfo.load_op = SDL_GPU_LOADOP_CLEAR;
	colorInfo.store_op = SDL_GPU_STOREOP_STORE;
	SDL_GPURenderPass *renderPass = SDL_BeginGPURenderPass(cmd, &colorInfo, 1, nullptr);
	if (renderPass == nullptr) {
		LogFailHard("render pass creation failed");
		return false;
	}
	SDL_BindGPUGraphicsPipeline(renderPass, pipeline);
	if (numBindings > 0)
		SDL_BindGPUFragmentSamplers(renderPass, 0, bindings, numBindings);
	SDL_PushGPUFragmentUniformData(cmd, 0, &uniforms, sizeof(uniforms));
	if (viewport != nullptr)
		SDL_SetGPUViewport(renderPass, viewport);
	SDL_DrawGPUPrimitives(renderPass, 3, 1, 0, 0);
	SDL_EndGPURenderPass(renderPass);
	return true;
}

} // namespace

void LogPhase(std::string_view line)
{
	LogFileLine(line);
}

bool ScalerWanted()
{
	return *GetOptions().Graphics.shaderFilter != ShaderFilter::None;
}

bool IsActive()
{
	return gDevice != nullptr && gWindow != nullptr && gFilter != ShaderFilter::None;
}

bool HasFailed()
{
	return gFailed;
}

bool WindowHasSwapchainHistory(SDL_Window *window)
{
	return window != nullptr && gLastClaimedWindowId != 0 && SDL_GetWindowID(window) == gLastClaimedWindowId;
}

// Builds the device-creation property set shared by the first creation
// attempt and the ROB-18 Vulkan retry. `backend` forces a backend driver, or
// SDL auto-selects one when null.
static SDL_PropertiesID BuildDeviceProperties(bool debugDevice, const char *backend)
{
	SDL_PropertiesID props = SDL_CreateProperties();
	SDL_SetBooleanProperty(props, SDL_PROP_GPU_DEVICE_CREATE_DEBUGMODE_BOOLEAN, debugDevice);
	SDL_SetBooleanProperty(props, SDL_PROP_GPU_DEVICE_CREATE_SHADERS_SPIRV_BOOLEAN, true);
	// FEAT-02: MSL is deliberately not accepted. The hand-ported MSL for the
	// shared programs (gpu_shaders_msl.hpp) was never field-validated and the
	// preset runner submits SPIR-V only, so an MSL-capable device produced a
	// session that deterministically failed at first preset load behind the
	// generic error. Auto-selected Metal now gets an honest log line plus the
	// ROB-18 Vulkan retry below (MoltenVK keeps the scaler available).
	// Ask SDL to log the adapter name and driver version; the log capture
	// installed below redirects these messages into the diagnostic file.
	SDL_SetBooleanProperty(props, SDL_PROP_GPU_DEVICE_CREATE_VERBOSE_BOOLEAN, true);
	if (backend != nullptr)
		SDL_SetStringProperty(props, SDL_PROP_GPU_DEVICE_CREATE_NAME_STRING, backend);
	return props;
}

// Creates the GPU device and selects the shader format for its backend.
// `name` forces a specific backend (e.g. "vulkan") or auto-selects when null.
// The DEVILUTIONX_GPU_BACKEND environment variable overrides both, which is
// useful for troubleshooting a broken D3D12 or Vulkan driver.
//
// DEVILUTIONX_GPU_DEBUG=1 creates the device in SDL debug mode: SDL's
// own validation block activates (release asserts on pipeline/texture
// state) and the Vulkan backend requests the Khronos validation layer
// when it is installed. A state the release path would pass through
// silently (or that a driver could die on) then surfaces as a logged
// SDL error and the scaler's graceful fallback instead of a process
// abort. Field instrument for silent first-frame crashes (ds-hybrid
// round): run with DEVILUTIONX_GPU_DEBUG=1, reproduce, read the log.
static bool CreateDevice(const char *name)
{
	const char *backendOverride = SDL_getenv("DEVILUTIONX_GPU_BACKEND");
	if (backendOverride != nullptr && *backendOverride == '\0')
		backendOverride = nullptr;
	if (backendOverride != nullptr)
		LogFileLine(StrCat("backend override from DEVILUTIONX_GPU_BACKEND: ", backendOverride));
	const char *debugEnv = SDL_getenv("DEVILUTIONX_GPU_DEBUG");
	const bool debugDevice = debugEnv != nullptr && *debugEnv == '1';
	if (debugDevice)
		LogFileLine("GPU debug mode enabled via DEVILUTIONX_GPU_DEBUG=1");
	SDL_PropertiesID props = BuildDeviceProperties(debugDevice,
	    backendOverride != nullptr ? backendOverride : name);
	gDevice = SDL_CreateGPUDeviceWithProperties(props);
	SDL_DestroyProperties(props);
	if (gDevice == nullptr)
		return false;
	const char *driver = SDL_GetGPUDeviceDriver(gDevice);
	if (driver != nullptr && std::strcmp(driver, "vulkan") == 0) {
		gShaderFormat = SDL_GPU_SHADERFORMAT_SPIRV;
	} else {
		if (driver != nullptr && std::strcmp(driver, "metal") == 0) {
			// FEAT-02: say why, in the platform's own terms, instead of leaving
			// the generic unsupported-driver line (or a later generic preset
			// failure) to speak for all of macOS.
			LogFileLine("GPU shader mod requires Vulkan; Metal is not yet supported - "
			            "retrying Vulkan once (MoltenVK), then falling back to the stock renderer");
		}
		// ROB-18: the whitelist above is the set of backends the mod can drive,
		// but SDL's auto-selection can still return something else (typically
		// d3d12 on Windows, which SDL reports as a SPIR-V-capable backend). That
		// used to hard-fail the session even when a working Vulkan driver
		// existed: the retry machinery in Reinit() is only reachable from
		// filter-resource failures, never from this driver-format rejection.
		// Retry once with the Vulkan backend forced before handing the session
		// back to the stock renderer. An explicit DEVILUTIONX_GPU_BACKEND or a
		// forced name is honored as-is: the override exists to troubleshoot one
		// specific backend, and silently retrying Vulkan would defeat it.
		const bool autoSelected = backendOverride == nullptr && name == nullptr;
		SDL_DestroyGPUDevice(gDevice);
		gDevice = nullptr;
		if (autoSelected) {
			LogFileLine(StrCat("unsupported GPU driver: ", driver != nullptr ? driver : "unknown",
			    " - retrying once with the vulkan backend"));
			props = BuildDeviceProperties(debugDevice, "vulkan");
			gDevice = SDL_CreateGPUDeviceWithProperties(props);
			SDL_DestroyProperties(props);
		} else {
			LogFileLine(StrCat("unsupported GPU driver: ", driver != nullptr ? driver : "unknown",
			    " (no retry: the backend was selected explicitly)"));
		}
		if (gDevice == nullptr)
			return false;
		driver = SDL_GetGPUDeviceDriver(gDevice);
		if (driver != nullptr && std::strcmp(driver, "vulkan") == 0) {
			gShaderFormat = SDL_GPU_SHADERFORMAT_SPIRV;
		} else {
			// Belt and braces: an explicitly named backend should never come back
			// as an unlisted driver (and Metal is no longer accepted - FEAT-02).
			// Same contract as before: the caller records the hard failure and
			// the session reverts to the stock renderer.
			SDL_DestroyGPUDevice(gDevice);
			gDevice = nullptr;
			return false;
		}
	}
	Log("GPU scaler: using the {} backend", driver);
	LogFileLine(StrCat("device created, backend=", driver != nullptr ? driver : "unknown"));
	return true;
}

/** @brief Applies the current FrameRateControl setting to the live swapchain
 * (SDR composition, vsync or immediate present) and returns the requested
 * vsync flag for the caller's log line. A failed SDL_SetGPUSwapchainParameters
 * is logged and non-fatal: the swapchain keeps its previous present mode.
 * Shared by the Reinit window-claim paths and the ROB-31 option change
 * dispatcher (ApplyFrameRateControl). An unsupported present mode falls
 * back to the nearest supported one instead of failing the change, and a
 * null device/window state is a no-op (ROB-31). */
static bool ApplySwapchainPresentMode()
{
	const bool vsync = *GetOptions().Graphics.frameRateControl == FrameRateControl::VerticalSync;
	if (gDevice == nullptr || gWindow == nullptr)
		return vsync;
	SDL_GPUPresentMode mode = vsync ? SDL_GPU_PRESENTMODE_VSYNC : SDL_GPU_PRESENTMODE_IMMEDIATE;
	if (!SDL_WindowSupportsGPUPresentMode(gDevice, gWindow, mode)) {
		// ROB-31: an unsupported present mode must fall back instead of
		// failing the parameter change and leaving the option desynced from
		// the swapchain (MAILBOX is the nearest non-blocking mode; the
		// FIFO/VSYNC mode is supported on every SDL3-GPU backend).
		if (!vsync && SDL_WindowSupportsGPUPresentMode(gDevice, gWindow, SDL_GPU_PRESENTMODE_MAILBOX))
			mode = SDL_GPU_PRESENTMODE_MAILBOX;
		else
			mode = SDL_GPU_PRESENTMODE_VSYNC;
		LogFileLine(StrCat("requested present mode unsupported; using present mode ",
		    static_cast<int>(mode)));
	}
	if (!SDL_SetGPUSwapchainParameters(gDevice, gWindow,
	        SDL_GPU_SWAPCHAINCOMPOSITION_SDR, mode)) {
		LogFail("SDL_SetGPUSwapchainParameters failed");
	}
	return vsync;
}

bool Reinit(SDL_Window *window)
{
	if (gFailed) {
		LogFileLine("Reinit skipped: the scaler failed earlier this session");
		gFilter = ShaderFilter::None;
		return false;
	}
	gFilter = *GetOptions().Graphics.shaderFilter;
	if (gFilter == ShaderFilter::None) {
		Shutdown();
		return false;
	}
	LogFileLine(StrCat("Reinit: filter=", static_cast<int>(gFilter),
	    " (0=Off, >=100=custom preset)"));

	if (gDevice == nullptr) {
		if (!CreateDevice(/*name=*/nullptr)) {
			LogFailHard("SDL_CreateGPUDevice failed");
			gFilter = ShaderFilter::None;
			return false;
		}
	}

	if (gWindow != window) {
		if (gWindow != nullptr)
			SDL_ReleaseWindowFromGPUDevice(gDevice, gWindow);
		gWindow = window;
		bool claimed = SDL_ClaimWindowForGPUDevice(gDevice, gWindow);
		if (!claimed) {
			LogFailHard("SDL_ClaimWindowForGPUDevice failed");
			gWindow = nullptr;
			Shutdown();
			return false;
		}
		gLastClaimedWindowId = SDL_GetWindowID(gWindow);
		const bool vsync = ApplySwapchainPresentMode();
		LogFileLine(StrCat("window claimed, swapchain format=", static_cast<int>(SDL_GetGPUSwapchainTextureFormat(gDevice, gWindow)),
		    " vsync=", vsync ? "1" : "0"));
		// The swapchain format may have changed with the new window.
		if (gBlitPipeline != nullptr) {
			SDL_ReleaseGPUGraphicsPipeline(gDevice, gBlitPipeline);
			gBlitPipeline = nullptr;
			gBlitPipelineFormat = SDL_GPU_TEXTUREFORMAT_INVALID;
		}
	}

	if (!EnsureFilterResources(gFilter)) {
		if (IsCustomFilter(gFilter)) {
			// A runtime preset that fails on every backend must not take the
			// GPU scaler (and the game) down: turn the filter off for this
			// session instead. Picking another preset in Settings retries the
			// load from scratch.
			LogFileLine("custom preset failed to load; Shader Filter is off for this session");
			gFilter = ShaderFilter::None;
			Shutdown();
			return false;
		}
		// A shader compile or pipeline creation failure can be
		// backend-specific. Retry once on the Vulkan backend before
		// giving up on the GPU path.
		const char *driver = gDevice != nullptr ? SDL_GetGPUDeviceDriver(gDevice) : nullptr;
		if (driver != nullptr && std::strcmp(driver, "vulkan") != 0) {
			LogFileLine(StrCat("filter resource initialization failed on ", driver,
			    "; retrying once with the vulkan backend"));
			Shutdown();
			gFilter = *GetOptions().Graphics.shaderFilter;
			if (CreateDevice(/*name=*/"vulkan")) {
				gWindow = window;
				if (SDL_ClaimWindowForGPUDevice(gDevice, gWindow)) {
					gLastClaimedWindowId = SDL_GetWindowID(gWindow);
					ApplySwapchainPresentMode();
					if (EnsureFilterResources(gFilter))
						return true;
				} else {
					// ROB-24: mirror the first claim site - a failed claim must not
					// leave gWindow set, or the Shutdown() below releases a window
					// this device never claimed.
					gWindow = nullptr;
				}
			}
		}
		LogFailHard("filter resource initialization failed");
		Shutdown();
		return false;
	}
	return true;
}

void ApplyFrameRateControl()
{
	if (!IsActive())
		return;
	ApplySwapchainPresentMode();
	LogFileLine(StrCat("FrameRateControl changed: vsync=",
	    *GetOptions().Graphics.frameRateControl == FrameRateControl::VerticalSync ? "1" : "0"));
}

void Shutdown()
{
	LogFileLine("Shutdown");
	if (gDevice == nullptr) {
		gWindow = nullptr;
		gFilter = ShaderFilter::None;
		return;
	}
	ReleaseFrameResources();
	ReleaseFilterResources();
	gPresetRunner.Unload();
	ClearLoadedPreset();
	gPresetDisplayName.clear();
	if (gNearestSampler != nullptr) {
		SDL_ReleaseGPUSampler(gDevice, gNearestSampler);
		gNearestSampler = nullptr;
	}
	if (gLinearSampler != nullptr) {
		SDL_ReleaseGPUSampler(gDevice, gLinearSampler);
		gLinearSampler = nullptr;
	}
	if (gWindow != nullptr) {
		SDL_ReleaseWindowFromGPUDevice(gDevice, gWindow);
		gWindow = nullptr;
	}
	SDL_DestroyGPUDevice(gDevice);
	gDevice = nullptr;
	gShaderFormat = SDL_GPU_SHADERFORMAT_INVALID;
	gBlitPipelineFormat = SDL_GPU_TEXTUREFORMAT_INVALID;
	gFilter = ShaderFilter::None;
	// gLastClaimedWindowId is intentionally NOT cleared: the (possibly still
	// alive) window keeps its DXGI swapchain history on Windows. Window IDs
	// are never reused, so the stale ID cannot match a future window.
}

void HandbackWindow(SDL_Window *window)
{
	LogFileLine("HandbackWindow: handing the window back to the regular renderer (device teardown deferred)");
	if (gDevice == nullptr) {
		gWindow = nullptr;
		gFilter = ShaderFilter::None;
		return;
	}
	// Release exactly what Shutdown() releases, minus the device itself.
	// All of these run against a live device while the window is still
	// alive - well-defined operations, unlike SDL_DestroyGPUDevice landing
	// in the middle of the switch stack (see gpu_scaler.hpp).
	ReleaseFrameResources();
	ReleaseFilterResources();
	gPresetRunner.Unload();
	ClearLoadedPreset();
	gPresetDisplayName.clear();
	if (gNearestSampler != nullptr) {
		SDL_ReleaseGPUSampler(gDevice, gNearestSampler);
		gNearestSampler = nullptr;
	}
	if (gLinearSampler != nullptr) {
		SDL_ReleaseGPUSampler(gDevice, gLinearSampler);
		gLinearSampler = nullptr;
	}
	if (gWindow != nullptr) {
		// SDL has no window-destroy auto-release for GPU claims (no
		// WINDOWEVENT hook in SDL_gpu.c): release explicitly while the
		// window is still alive, BEFORE the caller recreates it.
		SDL_ReleaseWindowFromGPUDevice(gDevice, gWindow);
		gWindow = nullptr;
	}
	(void)window;
	gFilter = ShaderFilter::None;
	gShaderFormat = SDL_GPU_SHADERFORMAT_INVALID;
	gBlitPipelineFormat = SDL_GPU_TEXTUREFORMAT_INVALID;
	// ROB-26: gDevice intentionally left ALIVE and parked, and
	// gLastClaimedWindowId intentionally kept (the hand-back flow still
	// needs WindowHasSwapchainHistory to recreate the DXGI-poisoned
	// window). The parked device holds no filter/frame/swapchain
	// resources from here on. Destroyed by the next Shutdown() (quit /
	// dx_cleanup) or reused by the next Reinit(); IsActive() is already
	// false (gWindow == nullptr && gFilter == None), so every game path
	// falls back to the regular renderer immediately.
}

void PresentFrame(const SDL_Surface &frame)
{
	if (!IsActive())
		return;

	// Heartbeat: lets a remote log show whether the present loop is alive
	// (UI responsive) or stalled (main thread stuck elsewhere), and counts
	// frames skipped due to an unavailable swapchain image plus frames
	// skipped by the zero-destination guard (round 9: that skip class was
	// SILENT - the field log looked healthy while the screen stayed black).
	static Uint32 sLastHeartbeat = 0;
	static unsigned long sHeartbeatFrames = 0;
	static unsigned long sSkippedFrames = 0;
	static unsigned long sDestSkipFrames = 0;
	static unsigned long sTotalFrames = 0;
	++sHeartbeatFrames;
	++sTotalFrames;
	{
		const Uint32 now = SDL_GetTicks();
		if (sLastHeartbeat == 0)
			sLastHeartbeat = now;
		if (now - sLastHeartbeat >= 5000) {
			LogFileLine(StrCat("heartbeat: ", sHeartbeatFrames, " frames (", sSkippedFrames,
			    " skipped, ", sDestSkipFrames, " dest-skipped, ", sTotalFrames,
			    " total, filter=", static_cast<int>(gFilter), ")"));
			sLastHeartbeat = now;
			sHeartbeatFrames = 0;
			sSkippedFrames = 0;
			sDestSkipFrames = 0;
		}
	}

	static bool loggedFirstPresentStart = false;
	if (!loggedFirstPresentStart) {
		loggedFirstPresentStart = true;
		LogFileLine("present: first frame, acquiring command buffer and swapchain");
	}

	const ShaderFilter filter = gFilter;
	const bool isCustom = IsCustomFilter(filter);

	// Frame duration for FrameTime, measured around the whole present.
	static Uint32 sLastFrameTick = 0;
	const Uint32 nowTick = SDL_GetTicks();
	Uint64 frameDeltaUs = 0;
	if (sLastFrameTick != 0)
		frameDeltaUs = static_cast<Uint64>(nowTick - sLastFrameTick) * 1000U;
	sLastFrameTick = nowTick;

	SDL_GPUCommandBuffer *cmd = SDL_AcquireGPUCommandBuffer(gDevice);
	if (cmd == nullptr) {
		LogFailHard("SDL_AcquireGPUCommandBuffer failed");
		return;
	}

	SDL_GPUTexture *swapchainTexture = nullptr;
	Uint32 swapchainWidth = 0;
	Uint32 swapchainHeight = 0;
	// Non-blocking on purpose: the blocking acquire variant can stall the
	// main thread indefinitely (minimized window, resize, driver quirk),
	// which looks like a frozen UI while audio keeps playing. Skipping a
	// frame costs nothing; the next frame retries.
	if (!SDL_AcquireGPUSwapchainTexture(cmd, gWindow, &swapchainTexture, &swapchainWidth, &swapchainHeight)
	    || swapchainTexture == nullptr) {
		// Window minimized or no swapchain image available this frame.
		++sSkippedFrames;
		SDL_CancelGPUCommandBuffer(cmd);
		return;
	}

	const int gameWidth = frame.w;
	const int gameHeight = frame.h;

	static bool loggedFirstPresent = false;
	if (!loggedFirstPresent) {
		loggedFirstPresent = true;
		LogFileLine(StrCat("first present: game=", gameWidth, "x", gameHeight,
		    " swapchain=", swapchainWidth, "x", swapchainHeight));
	}

	// The swapchain format can change (window resize, fullscreen switch).
	// Rebuild the blit pipeline for the current format when needed.
	if (!EnsureBlitPipeline(SDL_GetGPUSwapchainTextureFormat(gDevice, gWindow))) {
		LogFailHard("blit pipeline creation failed for current swapchain format");
		SubmitSkipAfterAcquire(cmd);
		return;
	}

	// Fit the game frame into the window, honoring the Integer Scaling option.
	float fitScale = std::min(static_cast<float>(swapchainWidth) / gameWidth,
	    static_cast<float>(swapchainHeight) / gameHeight);
	if (*GetOptions().Graphics.integerScaling) {
		const float floored = std::floor(fitScale);
		// Round 9: integer scaling is an UPSCALE constraint. Flooring a
		// DOWNSCALE ratio (< 1) collapses the scale to zero and silently
		// blacks out every frame; keep the fractional fit instead.
		if (floored >= 1.0F)
			fitScale = floored;
	}

	float destScale;
	if (isCustom) {
		// Preset chains size their own outputs via the libretro scale
		// rules; the final fit is computed after the chain has run,
		// from the actual output dimensions.
		destScale = 1.0F;
	} else {
		// Builtin HQ2x/ScaleFX filters were removed; only runtime
		// presets render here. ROB-25: count and log this skip like every
		// other present-path skip - an unexpected filter value must not
		// present as a permanent silent black screen with a healthy heartbeat.
		++sSkippedFrames;
		static Uint32 sLastFilterSkipLog = 0;
		const Uint32 nowTickFilter = SDL_GetTicks();
		if (sLastFilterSkipLog == 0 || nowTickFilter - sLastFilterSkipLog >= 5000) {
			LogFileLine(StrCat("present skip: unexpected filter value ", static_cast<int>(gFilter),
			    " (not a custom preset); skipping presentation"));
			sLastFilterSkipLog = nowTickFilter;
		}
		SubmitSkipAfterAcquire(cmd);
		return;
	}

	if (!EnsureFrameResources(gameWidth, gameHeight)) {
		// EnsureFrameResources already logged + hard-failed the session.
		SubmitSkipAfterAcquire(cmd);
		return;
	}
	// Upload the frame through a fresh transfer buffer created per frame.
	// The buffer is released when this function returns (RAII below); SDL
	// defers the actual destruction until the GPU is done with it. This
	// avoids overwriting a long-lived buffer that the GPU may still be
	// reading from on the previous frame's copy pass.
	SDL_GPUTransferBufferCreateInfo transferInfo {};
	transferInfo.usage = SDL_GPU_TRANSFERBUFFERUSAGE_UPLOAD;
	transferInfo.size = static_cast<Uint32>(gameWidth) * gameHeight * 4;
	SDL_GPUTransferBuffer *transfer = SDL_CreateGPUTransferBuffer(gDevice, &transferInfo);
	if (transfer == nullptr) {
		LogFailHard("frame transfer buffer creation failed");
		SubmitSkipAfterAcquire(cmd);
		return;
	}
	struct TransferBufferReleaser {
		SDL_GPUDevice *device;
		SDL_GPUTransferBuffer *buffer;
		~TransferBufferReleaser()
		{
			SDL_ReleaseGPUTransferBuffer(device, buffer);
		}
	} transferReleaser { gDevice, transfer };

	{
		const int bytesPerRow = gameWidth * 4;
		auto *dst = static_cast<uint8_t *>(SDL_MapGPUTransferBuffer(gDevice, transfer, false));
		if (dst == nullptr) {
			// ROB-16: device loss / allocation failure must cost one skipped
			// frame like every neighboring stage, not a memcpy to NULL outside
			// the failure contract.
			LogFailHard("frame transfer buffer map failed");
			SubmitSkipAfterAcquire(cmd);
			return;
		}
		const auto *src = static_cast<const uint8_t *>(frame.pixels);
		if (frame.pitch == bytesPerRow) {
			std::memcpy(dst, src, static_cast<size_t>(bytesPerRow) * gameHeight);
		} else {
			for (int y = 0; y < gameHeight; ++y)
				std::memcpy(dst + static_cast<size_t>(y) * bytesPerRow, src + static_cast<size_t>(y) * frame.pitch, bytesPerRow);
		}
		SDL_UnmapGPUTransferBuffer(gDevice, transfer);

		SDL_GPUCopyPass *copyPass = SDL_BeginGPUCopyPass(cmd);
		if (copyPass == nullptr) {
			// ROB-17: device loss at copy-pass begin - submit the empty command
			// buffer (post-acquire contract) and fail visibly instead of blitting
			// the previous frame contents with a clean log.
			LogFailHard("frame upload copy pass begin failed");
			SubmitSkipAfterAcquire(cmd);
			return;
		}
		SDL_GPUTextureTransferInfo transferSrc {};
		transferSrc.transfer_buffer = transfer;
		transferSrc.offset = 0;
		transferSrc.pixels_per_row = gameWidth;
		transferSrc.rows_per_layer = gameHeight;
		SDL_GPUTextureRegion region {};
		region.texture = gGameTex;
		region.w = gameWidth;
		region.h = gameHeight;
		region.d = 1;
		// ROB-17 fixup (r7.6): SDL >= 3.2 made the command-encoded copy calls
		// void - per-call failure reporting was removed upstream ("can't fail
		// usefully"); SDL logs the error and it surfaces at the checked
		// SDL_SubmitGPUCommandBuffer below. Bare call on purpose: it compiles
		// against both the old bool-returning preview signature and the
		// current void one (the bool capture here broke the CI build).
		SDL_UploadToGPUTexture(copyPass, &transferSrc, &region, false);
		SDL_EndGPUCopyPass(copyPass);
	}

	FragUniforms uniforms {};
	uniforms.sourceSize[0] = static_cast<float>(gameWidth);
	uniforms.sourceSize[1] = static_cast<float>(gameHeight);
	uniforms.sourceSize[2] = 1.0F / gameWidth;
	uniforms.sourceSize[3] = 1.0F / gameHeight;

	// Texture presented by the filter chain: runtime presets produce
	// their own final texture.
	SDL_GPUTexture *presentTex = nullptr;
	Uint32 presentWidth = 0;
	Uint32 presentHeight = 0;

	if (isCustom) {
		SDL_GPUTexture *outTex = nullptr;
		Uint32 outWidth = 0;
		Uint32 outHeight = 0;
		if (!gPresetRunner.Run(cmd, gGameTex, gameWidth, gameHeight, swapchainWidth, swapchainHeight,
		        frameDeltaUs, &outTex, &outWidth, &outHeight)) {
			// Run failure = hard failure per the failure contract: the runner
			// logged the reason (target creation, render pass, samplers);
			// tear the scaler down and revert to the regular renderer for the
			// session instead of retrying every frame.
			LogFailHard("preset run failed");
			SubmitSkipAfterAcquire(cmd);
			return;
		}
		// Fit the chain output into the window, honoring Integer Scaling.
		float outFit = std::min(static_cast<float>(swapchainWidth) / static_cast<float>(outWidth),
		    static_cast<float>(swapchainHeight) / static_cast<float>(outHeight));
		if (*GetOptions().Graphics.integerScaling) {
			// Round 9 field fix (ds-hybrid-scalefx black screen): chains
			// whose FINAL pass renders LARGER than the window (viewport
			// scale 2x/3x finals: ds-hybrid-sabr 3840x2160, scalefx
			// 5760x3240 into 1920x1080) had their fit floored from 1/2
			// or 1/3 to ZERO, hitting the destWidth <= 0 skip below
			// every frame - black screen, responsive game, zero log
			// lines. The chain content itself is verified good (round-9
			// sandbox readback: avgLuma 150/255, 100% non-black on
			// lavapipe). Keep the fractional fit for downscales.
			const float floored = std::floor(outFit);
			if (floored >= 1.0F)
				outFit = floored;
		}
		destScale = outFit;
		presentTex = outTex;
		presentWidth = outWidth;
		presentHeight = outHeight;
	}

	// Blit the scaled image into the swapchain with letterboxing.
	int destWidth;
	int destHeight;
	destWidth = static_cast<int>(std::lround(static_cast<float>(presentWidth) * destScale));
	destHeight = static_cast<int>(std::lround(static_cast<float>(presentHeight) * destScale));
	// Rounding can overshoot the swapchain by a pixel; clamp so the
	// viewport never exceeds the render target.
	if (destWidth > static_cast<int>(swapchainWidth))
		destWidth = static_cast<int>(swapchainWidth);
	if (destHeight > static_cast<int>(swapchainHeight))
		destHeight = static_cast<int>(swapchainHeight);
	if (destWidth <= 0 || destHeight <= 0) {
		// Window smaller than the scaled image can occupy; skip.
		// Round 9: this skip used to be SILENT and it masked the integer
		// scaling floor-to-zero above (field: black screen while the log
		// reported zero errors). Count it for the heartbeat and log the
		// full dimensions the first time each episode occurs, so the next
		// silent-black class names itself in the field log.
		++sDestSkipFrames;
		static Uint32 sLastDestSkipLog = 0;
		const Uint32 nowTickDest = SDL_GetTicks();
		if (sLastDestSkipLog == 0 || nowTickDest - sLastDestSkipLog >= 5000) {
			LogFileLine(StrCat("present skip: destination rect ", destWidth, "x", destHeight,
			    " (present ", presentWidth, "x", presentHeight, ", swapchain ",
			    swapchainWidth, "x", swapchainHeight, ", scale ", std::to_string(destScale),
			    ", integer ", *GetOptions().Graphics.integerScaling ? "on" : "off", ")"));
			sLastDestSkipLog = nowTickDest;
		}
		SubmitSkipAfterAcquire(cmd);
		return;
	}
	SDL_GPUViewport viewport {};
	viewport.x = (swapchainWidth - destWidth) / 2.0F;
	viewport.y = (swapchainHeight - destHeight) / 2.0F;
	viewport.w = destWidth;
	viewport.h = destHeight;
	viewport.min_depth = 0.0F;
	viewport.max_depth = 1.0F;
	SDL_GPUTextureSamplerBinding blitBindings[1] = { { presentTex, gLinearSampler } };
	uniforms.params[0] = 0.0F;
	if (!RenderPass(cmd, swapchainTexture, swapchainWidth, swapchainHeight, gBlitPipeline, uniforms, blitBindings, 1, &viewport)) {
		// RenderPass already logged + hard-failed the session.
		SubmitSkipAfterAcquire(cmd);
		return;
	}

	if (!SDL_SubmitGPUCommandBuffer(cmd))
		LogFailHard("SDL_SubmitGPUCommandBuffer failed");
}

namespace {

std::string gOsdText;

// FEAT-03: the OSD carries the first line of the last preset load failure
// (the full text stays in gpu-scaler.log), capped so the bottom-right label
// cannot walk off the screen on long paths.
std::string OsdErrorLine(const std::string &error)
{
	std::string line = error.substr(0, error.find('\n'));
	constexpr size_t kMaxOsdErrorChars = 64;
	if (line.size() > kMaxOsdErrorChars) {
		line.resize(kMaxOsdErrorChars - 3);
		line += "...";
	}
	return line;
}

const char *FilterDisplayName(ShaderFilter filter)
{
	if (IsCustomFilter(filter))
		return gPresetDisplayName.empty() ? "Custom" : gPresetDisplayName.c_str();
	return "Off";
}

std::string PrettyBackendName(std::string_view driver)
{
	if (driver == "vulkan")
		return "Vulkan";
	if (driver == "direct3d12")
		return "Direct3D 12";
	if (driver == "metal")
		return "Metal";
	return std::string(driver);
}

} // namespace

void ShowFilterOsd()
{
	const int value = static_cast<int>(*GetOptions().Graphics.shaderFilter);
	std::string text;
	if (value == static_cast<int>(ShaderFilter::None)) {
		// No filter active: show no label at all.
		text.clear();
	} else if (!IsActive()) {
		// FEAT-03: name the failure instead of a bare "(unavailable)" - the
		// first line of the last preset load error; the full text stays in
		// gpu-scaler.log. Custom filters only: the error belongs to the
		// preset path, not to a built-in filter's own failure.
		if (IsCustomFilter(static_cast<ShaderFilter>(value)) && !gPresetLastError.empty())
			text = StrCat("Shader: ", FilterDisplayName(static_cast<ShaderFilter>(value)), " (failed: ",
			    OsdErrorLine(gPresetLastError), ")");
		else
			text = StrCat("Shader: ", FilterDisplayName(static_cast<ShaderFilter>(value)), " (unavailable)");
	} else {
		text = StrCat("Shader: ", FilterDisplayName(static_cast<ShaderFilter>(value)),
		    " (", PrettyBackendName(SDL_GetGPUDeviceDriver(gDevice)), ")");
	}
	if (text != gOsdText) {
		gOsdText = std::move(text);
		LogFileLine(StrCat("osd: ", gOsdText.empty() ? "(hidden)" : gOsdText.c_str()));
	}
}

std::string_view FilterOsdText()
{
	return gOsdText;
}

void DrawFilterOsd(const Surface &out)
{
	const std::string_view text = FilterOsdText();
	if (text.empty())
		return;
	// FontSizeDialog + ColorDialogWhite is the engine's standard in-game
	// dialog text combo and reads correctly under BOTH the menu palette
	// (raw glyph index 224 = white in ui_art palettes) and the game
	// palettes (gamedialogwhite.trn maps 224 -> PAL16_GRAY white ramp).
	// Plain ColorWhite maps into palette entries that are pure
	// red/green/blue/... primaries in the menu palette ("rainbow" text).
	constexpr GameFontTables OsdFontSize = FontSizeDialog;
	constexpr int Margin = 8;
	const int textWidth = GetLineWidth(text, OsdFontSize);
	const int textHeight = GetLineHeight(text, OsdFontSize);
	const Point position { out.w() - Margin - textWidth, out.h() - Margin - textHeight };
	DrawString(out, text, position, { .flags = UiFlags::ColorDialogWhite | UiFlags::FontSizeDialog });
}

} // namespace gpu_scaler
} // namespace devilution
#endif // !USE_SDL1
#endif // USE_GPU_SCALER
