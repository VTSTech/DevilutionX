/**
 * preset_smoke — offline validation harness for the runtime slang preset
 * loader (GPU scaler "shaders folder" feature).
 *
 * Runs the full load pipeline minus the GPU device: scans a directory for
 * .slangp presets, parses them, translates every .slang pass and compiles it
 * to SPIR-V with the in-process glslang frontend. No SDL_GPU device is
 * created, so this runs headless (CI / sandbox friendly).
 *
 * Exit code 0 when every preset compiles (or passes --allow-failures with
 * at least one success). Prints per-preset diagnostics.
 *
 * Usage: preset_smoke [--root <dir>] [--limit N] [--quiet]
 */

#include <cstdint>
#include <filesystem>
#include <cstdio>
#include <cstring>
#include <string>
#include <vector>

#include "engine/render/gpu/preset/preset_parser.hpp"

namespace preset = devilution::gpu_scaler::preset;
#include "engine/render/gpu/preset/slang_compile.hpp"
#include "engine/render/gpu/preset/slang_translate.hpp"

int main(int argc, char **argv)
{
	std::string root;
	int limit = -1;
	bool quiet = false;
	for (int i = 1; i < argc; ++i) {
		const std::string arg = argv[i];
		if (arg == "--root" && i + 1 < argc) {
			root = argv[++i];
		} else if (arg == "--limit" && i + 1 < argc) {
			limit = std::atoi(argv[++i]);
		} else if (arg == "--quiet") {
			quiet = true;
		}
	}
	if (root.empty()) {
		std::fprintf(stderr, "usage: preset_smoke --root <dir> [--limit N] [--quiet]\n");
		return 2;
	}

	const std::vector<preset::PresetEntry> entries = preset::ScanPresets(root);
	std::printf("scanned %zu presets in %s\n", entries.size(), root.c_str());
	if (entries.empty())
		return 2;

	int okCount = 0;
	int failCount = 0;
	int passTotal = 0;
	std::string firstFailure;
	std::string firstFailurePreset;

	const int runLimit = limit < 0 ? static_cast<int>(entries.size()) : std::min<int>(limit, static_cast<int>(entries.size()));
	for (int i = 0; i < runLimit; ++i) {
		const preset::PresetEntry &entry = entries[static_cast<size_t>(i)];
		std::filesystem::path presetPath = std::filesystem::path(root) / entry.relativePath;
		const std::string path = presetPath.string();
		preset::Preset preset;
		std::string error;
		if (!preset::ParsePreset(path, preset, error)) {
			++failCount;
			if (firstFailure.empty()) {
				firstFailure = "parse: " + error;
				firstFailurePreset = entry.displayName;
			}
			if (!quiet)
				std::printf("FAIL %s: %s\n", entry.displayName.c_str(), error.c_str());
			continue;
		}

		bool presetOk = true;
		std::string presetError;
		for (size_t p = 0; p < preset.passes.size(); ++p) {
			const std::string slangPath = preset.dir + preset.passes[p].shaderPath;
			preset::SlangTranslation translation;
			if (!preset::TranslateSlang(slangPath, {}, static_cast<int>(p),
			        static_cast<int>(preset.passes.size()), translation)) {
				presetOk = false;
				presetError = "translate pass " + std::to_string(p) + " (" + preset.passes[p].shaderPath
				    + "): " + translation.error;
				break;
			}
			preset::PassSpirv spirv;
			// No disk cache for the smoke run: always compile fresh.
			if (!preset::CompilePass(translation, slangPath, "", spirv, error)) {
				presetOk = false;
				presetError = "compile pass " + std::to_string(p) + " (" + preset.passes[p].shaderPath
				    + "): " + error;
				break;
			}
			if (spirv.vertex.empty() || spirv.fragment.empty()) {
				presetOk = false;
				presetError = "empty SPIR-V in pass " + std::to_string(p);
				break;
			}
			++passTotal;
		}
		if (presetOk) {
			++okCount;
			if (!quiet)
				std::printf("ok   %s (%zu passes)\n", entry.displayName.c_str(), preset.passes.size());
		} else {
			++failCount;
			if (firstFailure.empty()) {
				firstFailure = presetError;
				firstFailurePreset = entry.displayName;
			}
			if (!quiet)
				std::printf("FAIL %s: %s\n", entry.displayName.c_str(), presetError.c_str());
		}
	}

	std::printf("\nresult: %d ok, %d failed, %d passes compiled\n", okCount, failCount, passTotal);
	if (!firstFailure.empty())
		std::printf("first failure (%s): %s\n", firstFailurePreset.c_str(), firstFailure.c_str());
	return failCount == 0 ? 0 : 1;
}
