// Standalone regression test for field round 5 (links the REAL parser only;
// no SDL device, no glslang, no translator).
//
// Field log 2026-10-17 (Windows, Vulkan, round-4 build): switching presets
// crt-beam-simulator-fsr-sony-crt-megatron-hdr ->
// crt-beam-simulator-nnedi3-nns16-4x-luma-fsr-crtroyale failed at parse:
//
//     preset parse failed: preset pass index 32 exceeds the limit of 32:
//         .../crt-beam-simulator-nnedi3-nns16-4x-luma-fsr-crtroyale.slangp
//     custom preset failed to load; Shader Filter is off for this session
//
// and the engine fell back to the stock renderer (session fallback, OSD
// message, no crash - the ROB-01/ROB-02 failure contract held in the wild).
//
// Root cause: ROB-02's kMaxPresetPasses = 32 bounds the hostile in-int-range
// pass key (`shader2000000000 = x`) so the lazy `passes` resize can never
// grow into a multi-gigabyte allocation - but 32 ALSO rejects real combined
// presets. The slangp format sets no pass-count limit ("no limit on #",
// slang-shaders README), and stacked chains (NNEDI3 + FSR + CRT-Royale + HDR)
// reach 33+ passes. The cap value was never a format requirement, only a
// DoS bound, so it can move freely as long as it stays a bound.
//
// Fix: kMaxPresetPasses 32 -> 128 (preset_parser.cpp). Hostile-key semantics
// unchanged: an index at or above the cap fails the preset with a named
// error before any resize. The boundary assertions in
// test/preset_failure_modes_test.cpp (preset_failure_modes ctest) moved
// 31/32 -> 127/128 with this round.
//
// Covered here (fixtures generated at runtime under the OS temp dir):
//   1. a 33-pass preset (indices 0..32 - the exact field failure) parses
//   2. pass 32's keys (shader/alias/filter/scale) land on the right pass
//   3. a sparse chain still compacts to the keyed passes (no cap-sized pad)
//   4. the cap still rejects: shader128 fails with a named error
//   5. the ROB-02 hostile key (shader2147483647) still fails politely
//   6. the caller's Preset is untouched on failure (no partial resize)
//
// Build (all flags REQUIRED: the preset TUs are wrapped in USE_GPU_SCALER):
//   g++ -std=c++23 -O2 -Wall -o test_field_round5
//     tools/audit/test_field_round5.cpp
//     Source/engine/render/gpu/preset/preset_parser.cpp
//     Source/utils/str_cat.cpp
//     -ISource -I3rdParty -I<pinned SDL3 include>
//     -DUSE_SDL3=1 -DUSE_GPU_SCALER=1
//
// Run from anywhere: fixtures are self-contained (no corpus, no shader
// files on disk - the parser only stores paths).

#include <cstdio>
#include <filesystem>
#include <fstream>
#include <string>

#include "engine/render/gpu/preset/preset_parser.hpp"

using namespace devilution::gpu_scaler::preset;

extern "C" const char *SDL_GetBasePath(void) { return "./"; }
void app_fatal(std::string_view) {}

static int failures = 0;
static int checks = 0;

static void Check(bool ok, const char *label) {
  ++checks;
  if (!ok) {
    ++failures;
    std::printf("FAIL %s\n", label);
  } else {
    std::printf("ok   %s\n", label);
  }
}

static const std::string kWorkRoot =
    (std::filesystem::temp_directory_path() / "devilutionx_field_round5").string();

static void WriteFile(const std::string &path, const std::string &content) {
  std::filesystem::create_directories(std::filesystem::path(path).parent_path());
  std::ofstream out(path, std::ios::binary | std::ios::trunc);
  out << content;
}

// The field-failing shape: 33 passes, indices 0..32 (index 32 is what the
// old cap rejected), with per-pass keys scattered across the chain and the
// last pass carrying alias/scale keys to prove pass 32 is fully addressable.
static std::string MakeField33Preset() {
  std::string text = "shaders = 33\n";
  for (int i = 0; i < 33; ++i) {
    text += "shader" + std::to_string(i) + " = shaders/r5/pass" + std::to_string(i) + ".slang\n";
    if (i % 2 == 0)
      text += "filter_linear" + std::to_string(i) + " = true\n";
  }
  text += "alias32 = r5final\n";
  text += "scale_type_x32 = source\n";
  text += "scale_x32 = 1.0\n";
  return text;
}

int main() {
  std::filesystem::create_directories(kWorkRoot);

  // 1+2. The exact field failure: 33 passes with pass index 32 present.
  {
    const std::string path = kWorkRoot + "/field33.slangp";
    WriteFile(path, MakeField33Preset());
    Preset preset;
    std::string error;
    const bool parsed = ParsePreset(path, preset, error);
    Check(parsed, "R5 33-pass field shape parses (pass index 32 accepted)");
    Check(parsed && preset.passes.size() == 33, "R5 chain has all 33 passes");
    if (parsed && preset.passes.size() == 33) {
      Check(preset.passes[32].shaderPath == "shaders/r5/pass32.slang",
          "R5 pass 32 carries its shader key");
      Check(preset.passes[32].alias == "r5final", "R5 pass 32 alias lands");
      Check(preset.passes[32].scaleX.scale == 1.0F, "R5 pass 32 scale_x lands");
      Check(preset.passes[0].filter == FilterMode::Linear &&
              preset.passes[16].filter == FilterMode::Linear &&
              preset.passes[1].filter == FilterMode::Nearest,
          "R5 filter_linear parity across the chain");
    }
  }

  // 3. Sparse chain still compacts (cap raise does not pad the vector).
  {
    const std::string path = kWorkRoot + "/sparse.slangp";
    WriteFile(path, "shader127 = shaders/r5/pass127.slang\n");
    Preset preset;
    std::string error;
    const bool parsed = ParsePreset(path, preset, error);
    Check(parsed, "R5 shader127 (last allowed slot) parses");
    Check(parsed && preset.passes.size() == 1 &&
              preset.passes[0].shaderPath == "shaders/r5/pass127.slang",
        "R5 sparse chain collapses to the 1 keyed pass");
  }

  // 4. The cap is still a cap: first index over 128 fails with a named error.
  {
    const std::string path = kWorkRoot + "/over.slangp";
    WriteFile(path, "shader128 = shaders/r5/pass128.slang\n");
    Preset preset;
    std::string error;
    Check(!ParsePreset(path, preset, error), "R5 shader128 (first over the limit) fails");
    Check(error.find("exceeds the limit of 128") != std::string::npos,
        "R5 error names the new limit");
    Check(preset.passes.empty(), "R5 caller's Preset untouched on over-cap failure");
  }

  // 5. ROB-02 hostile key unchanged: in-int-range absurd index fails politely.
  {
    const std::string path = kWorkRoot + "/hostile.slangp";
    WriteFile(path, "shader2147483647 = shaders/r5/pass-big.slang\n");
    Preset preset;
    std::string error;
    Check(!ParsePreset(path, preset, error), "R5 hostile shader2147483647 still fails");
    Check(error.find("exceeds the limit of 128") != std::string::npos,
        "R5 hostile-key error names the bounded limit");
    Check(preset.passes.empty(), "R5 no resize happened on the caller's Preset");
  }

  std::printf("%d checks, %d failures\n", checks, failures);
  return failures == 0 ? 0 : 1;
}
