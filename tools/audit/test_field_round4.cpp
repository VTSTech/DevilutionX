// Standalone regression test for field round 4 (links the REAL parser and
// translator; no SDL device, no glslang — translate stage only).
//
// Field log 2026-10-15 (Windows, Vulkan, round-3 build): koko-aio-ng (and the
// user's Monitor-Ambilight-immersive / Base variants) failed at glslang with
//     ERROR: 0:1749: 'hsv2rgb' : no matching overloaded function found
// compiling shaders-ng/colortools_and_ntsc_pass.slang (pass 0).
//
// Root cause: the spec's #include is plain textual inclusion, and the shader
// deliberately re-includes includes/functions.include.slang under
// #pragma stage vertex (line 65) AND again under #pragma stage fragment
// (line 211). ResolveIncludes kept ONE file-lifetime visited set, so the
// second inclusion was dropped and the fragment translation unit
// (shared + fragment) never saw the functions (hsv2rgb lives at
// functions.include.slang:1330; the file has no #pragma once).
//
// Fix: include-once is scoped per stage bucket (shared/vertex/fragment),
// mirroring SplitStages' walk over the merged text. A repeat within ONE
// region would double-paste into a single translation unit and fail to
// compile everywhere, so once-per-bucket is exactly the safe set; an active
// expansion stack additionally guards cross-bucket self-recursion.
//
// Covered here (self-contained fixtures under field-presets/test/, plus the
// optional corpus check):
//   1. cross-bucket re-include survives (definition present in BOTH stages)
//   2. same-bucket repeat still dedupes (exactly one definition per stage)
//   3. same-bucket include cycle still terminates
//   4. #pragma stage lines stay in the merged text (stage-boundary markers
//      do not leak across stages)
//   5. corpus: koko-aio-ng pass 0 fragment now carries the hsv2rgb definition
//
// Build (all flags REQUIRED: the preset TUs are wrapped in USE_GPU_SCALER):
//   g++ -std=c++23 -O2 -Wall -o test_field_round4
//     tools/audit/test_field_round4.cpp
//     Source/engine/render/gpu/preset/slang_translate.cpp
//     Source/engine/render/gpu/preset/preset_parser.cpp
//     Source/utils/str_cat.cpp
//     -ISource -I3rdParty -I<pinned SDL3 include>
//     -DUSE_SDL3=1 -DUSE_GPU_SCALER=1
//
// Optional argv[1] = slang-shaders root: also translates koko-aio-ng's
// field-failing pass 0 when the corpus is available (graceful skip otherwise).
// Run from the repo root: fixtures are addressed repo-root-relative.

#include <cstdio>
#include <filesystem>
#include <fstream>
#include <string>
#include <unordered_set>

#include "engine/render/gpu/preset/preset_parser.hpp"
#include "engine/render/gpu/preset/preset_types.hpp"
#include "engine/render/gpu/preset/slang_translate.hpp"

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

static size_t CountOccurrences(const std::string &haystack,
                               const std::string &needle) {
  size_t count = 0;
  size_t pos = 0;
  while ((pos = haystack.find(needle, pos)) != std::string::npos) {
    ++count;
    pos += needle.size();
  }
  return count;
}

int main(int argc, char **argv) {
  const std::filesystem::path fixture =
      std::filesystem::path("tools") / "audit" / "field-presets" / "test" /
      "r4-include.slangp";

  Check(std::filesystem::exists(fixture),
        "R4 fixture present (run this test from the repo root)");
  if (!std::filesystem::exists(fixture)) {
    std::printf("%d checks, %d failures\n", checks, failures);
    return 1;
  }

  Preset preset;
  std::string error;
  const bool parsed = ParsePreset(fixture.string(), preset, error);
  Check(parsed, "R4 preset parses");
  Check(parsed && preset.passes.size() == 1, "R4 preset has one pass");

  bool translated = false;
  SlangTranslation t;
  if (parsed && preset.passes.size() == 1) {
    const std::string shader =
        (preset.dir / std::filesystem::path(preset.passes[0].shaderPath))
            .string();
    const std::unordered_set<std::string> known = {"source"};
    translated = TranslateSlang(shader, known, 0, 1, t, {});
    Check(translated, "R4 shader translates");
  }

  if (translated) {
    const std::string &vert = t.vertex.glsl;
    const std::string &frag = t.fragment.glsl;

    // 1. Cross-bucket re-include survives: the functions include is pasted
    //    once into the vertex region and once into the fragment region, so
    //    each stage's TU (shared + stage) carries its own copy.
    Check(CountOccurrences(vert, "vec4 r4Tint") == 1,
          "R4 vertex TU has exactly one r4Tint definition");
    Check(CountOccurrences(frag, "vec4 r4Tint") == 1,
          "R4 fragment TU has exactly one r4Tint definition (re-include kept)");

    // 2. Same-bucket repeat still dedupes: r4_functions.inc is included twice
    //    inside the fragment region; include-once per bucket drops the second.
    Check(CountOccurrences(frag, "vec4 r4Tint") ==
              CountOccurrences(frag, "return vec4(c.rgb * 0.5 + 0.25, c.a);"),
          "R4 fragment r4Tint body pasted once (same-bucket repeat dropped)");

    // 3. Same-bucket include cycle terminated with content kept.
    Check(CountOccurrences(frag + vert, "R4_CYCLE_A") >= 1,
          "R4 cycle member A content kept");
    Check(CountOccurrences(frag + vert, "#include") == 0,
          "R4 no raw #include lines survive into stage GLSL");

    // 4. Stage boundaries intact: markers stay in their own stage (guards the
    //    pragma-line-emission regression where everything landed in shared).
    Check(vert.find("r4VertexOnly") != std::string::npos,
          "R4 vertex marker stays in vertex TU");
    Check(vert.find("r4FragmentOnly") == std::string::npos,
          "R4 fragment marker does not leak into vertex TU");
    Check(frag.find("r4FragmentOnly") != std::string::npos,
          "R4 fragment marker stays in fragment TU");
    Check(frag.find("r4VertexOnly") == std::string::npos,
          "R4 vertex marker does not leak into fragment TU");
  }

  // ------------------------------------------------------------------
  // Optional: the field-failing koko-aio-ng pass 0 through the REAL
  // parser+translator (needs the slang-shaders corpus root as argv[1]).
  if (argc > 1) {
    const std::filesystem::path p = std::filesystem::path(argv[1]) /
                                    "bezel" / "koko-aio" / "koko-aio-ng.slangp";
    if (!std::filesystem::exists(p)) {
      std::printf("skip %s (no corpus)\n", p.string().c_str());
    } else {
      Preset koko;
      std::string kokoError;
      const bool kokoParsed = ParsePreset(p.string(), koko, kokoError);
      Check(kokoParsed, "R4 corpus koko-aio-ng parses");
      if (kokoParsed && !koko.passes.empty()) {
        const std::string shader =
            (koko.dir /
             std::filesystem::path(koko.passes[0].shaderPath))
                .string();
        std::unordered_set<std::string> known = {"source"};
        for (const PassDef &pass : koko.passes)
          if (!pass.alias.empty())
            known.insert(pass.alias);
        SlangTranslation kt;
        const bool ok = TranslateSlang(shader, known, 0,
                                       static_cast<int>(koko.passes.size()), kt,
                                       {});
        Check(ok, "R4 corpus koko-aio-ng pass 0 translates");
        Check(ok && kt.fragment.glsl.find("vec3 hsv2rgb") != std::string::npos,
              "R4 corpus koko-aio-ng fragment TU carries hsv2rgb definition");
        Check(ok && kt.vertex.glsl.find("vec3 hsv2rgb") != std::string::npos,
              "R4 corpus koko-aio-ng vertex TU carries hsv2rgb definition");
      }
    }
  }

  std::printf("%d checks, %d failures\n", checks, failures);
  return failures == 0 ? 0 : 1;
}
