// Standalone regression test for field round 2 (links the REAL parser and
// translator; no SDL device, no glslang — translate stage only).
//
// Re-created 2026-10-15: the original file was cited by docs/diablo-handoff.md
// §8 and docs/SLANG_SPEC.md items 18–20 but never committed. The round-2
// fixes themselves live in slang_translate.cpp (orphan-sampler drop + comment
// stripping in ProcessStage) and preset_parser.cpp (modifiers attach only to
// textures that exist). This test recreates the promised coverage against the
// CURRENT behavior (round 3 extended the orphan rule with macro-alias keep;
// the drop cases here must still drop).
//
// Round-2 field findings covered:
//  R2.1 orphan sampler declared but never sampled -> warn + drop, preset loads
//      (uborder crt-geom's ambi_temporal_pass pattern)
//  R2.2 orphan sampler that is a #define target -> warn + drop
//      (Mega Bezel hsm-custom-fast-sharpen: "#define PrePass0 Source")
//  R2.3 commented-out UBO block must not trip the one-UBO rewrite
//      (Mega Bezel hsm-interlace-and-linearize)
//  R2.4 commented-out sampler declaration must not match the rewriters
//  R2.5 undeclared texture modifier keys ("delta_filter_linear = true" with
//      no `delta` texture and no textures list) are ignored, not parsed into
//      a pathless texture entry (crt-geom-deluxe-ntsc-adaptive)
//  R2.6 real modifiers on declared textures still apply
//
// Build (all flags REQUIRED: the preset TUs are wrapped in USE_GPU_SCALER):
//   g++ -std=c++23 -O2 -Wall -o test_field_round2
//     tools/audit/test_field_round2.cpp
//     Source/engine/render/gpu/preset/slang_translate.cpp
//     Source/engine/render/gpu/preset/preset_parser.cpp
//     Source/utils/str_cat.cpp
//     -ISource -I3rdParty -I<pinned SDL3 include>
//     -DUSE_SDL3=1 -DUSE_GPU_SCALER=1
//
// Optional argv[1] = slang-shaders root: also runs the two round-2 field
// presets when the corpus is available (graceful skip otherwise).

#include <cstdio>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <string>
#include <vector>

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

static bool Contains(const std::vector<std::string> &v, const char *needle) {
  for (const std::string &s : v)
    if (s.find(needle) != std::string::npos)
      return true;
  return false;
}

static void WriteFile(const std::filesystem::path &path,
                      const std::string &text) {
  std::filesystem::create_directories(path.parent_path());
  std::ofstream out(path, std::ios::binary);
  out << text;
}

/** @brief Translates a shader with a one-pass known-name set (plus extras). */
static bool Translate(const std::filesystem::path &slang, SlangTranslation &t,
                      std::string &error,
                      const std::vector<std::string> &extraKnown = {}) {
  (void)error; // TranslateSlang reports through t.error; kept for call-site
               // symmetry.
  std::unordered_set<std::string> known{"Source", "source", "Original",
                                        "original"};
  for (const std::string &name : extraKnown) {
    known.insert(name);
    std::string lower;
    for (const char c : name)
      lower += static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
    known.insert(lower);
  }
  return TranslateSlang(slang.string(), known, 0, 1, t, {});
}

int main(int argc, char **argv) {
  const std::filesystem::path tmp =
      std::filesystem::temp_directory_path() / "devilutionx_test_field_round2";
  std::filesystem::remove_all(tmp);
  std::filesystem::create_directories(tmp);

  // ------------------------------------------------------------------
  // R2.1: orphan sampler, declared but never sampled.
  {
    WriteFile(tmp / "shaders" / "orphan.slang", R"(
#pragma name orphan_test

uniform sampler2D UnusedSampler;

#pragma stage vertex
layout(location = 0) in vec4 Position;
layout(location = 1) in vec2 TexCoord;
layout(location = 0) out vec2 vTexCoord;
void main() {
        gl_Position = Position;
        vTexCoord = TexCoord;
}

#pragma stage fragment
layout(location = 0) in vec2 vTexCoord;
layout(location = 0) out vec4 FragColor;
uniform sampler2D Source;
void main() {
        FragColor = texture(Source, vTexCoord);
}
)");
    SlangTranslation t;
    std::string error;
    const bool ok = Translate(tmp / "shaders" / "orphan.slang", t, error);
    Check(ok, "R2.1 orphan sampler translates");
    Check(
        Contains(
            t.warnings,
            "'UnusedSampler' has no chain target (declared but never sampled)"),
        "R2.1 orphan sampler warns (declared but never sampled)");
    Check(t.fragment.present && t.fragment.textures.size() == 1,
          "R2.1 orphan sampler dropped from binds");
  }

  // ------------------------------------------------------------------
  // R2.2: orphan sampler that is a #define target (Mega Bezel PrePass0).
  // Round 3 preserved this: the expansion target (Source) IS declared, so
  // the aliased declaration still drops.
  {
    WriteFile(tmp / "shaders" / "defined_away.slang", R"(
#pragma name defined_away_test

#define PrePass0 Source
uniform sampler2D PrePass0;

#pragma stage vertex
layout(location = 0) in vec4 Position;
layout(location = 1) in vec2 TexCoord;
layout(location = 0) out vec2 vTexCoord;
void main() {
        gl_Position = Position;
        vTexCoord = TexCoord;
}

#pragma stage fragment
layout(location = 0) in vec2 vTexCoord;
layout(location = 0) out vec4 FragColor;
uniform sampler2D Source;
void main() {
        FragColor = texture(Source, vTexCoord) + texture(PrePass0, vTexCoord * 0.5);
}
)");
    SlangTranslation t;
    std::string error;
    const bool ok = Translate(tmp / "shaders" / "defined_away.slang", t, error);
    Check(ok, "R2.2 #defined-away orphan translates");
    Check(
        Contains(t.warnings, "'PrePass0' has no chain target (#defined away)"),
        "R2.2 #defined-away orphan warns");
    Check(t.fragment.present && t.fragment.textures.size() == 1,
          "R2.2 only Source is bound (no duplicate)");
    Check(t.fragment.present && t.fragment.textures[0].name == "Source",
          "R2.2 bound name is Source");
  }

  // ------------------------------------------------------------------
  // R2.3: commented-out UBO block (one-UBO rewrite must not fire on it).
  {
    WriteFile(tmp / "shaders" / "commented_ubo.slang", R"(
#pragma name commented_ubo_test

/*
uniform UBO
{
        mat4 MVP;
};
*/

#pragma stage vertex
layout(location = 0) in vec4 Position;
layout(location = 1) in vec2 TexCoord;
layout(location = 0) out vec2 vTexCoord;
void main() {
        gl_Position = Position;
        vTexCoord = TexCoord;
}

#pragma stage fragment
layout(location = 0) in vec2 vTexCoord;
layout(location = 0) out vec4 FragColor;
uniform sampler2D Source;
void main() {
        FragColor = texture(Source, vTexCoord);
}
)");
    SlangTranslation t;
    std::string error;
    const bool ok =
        Translate(tmp / "shaders" / "commented_ubo.slang", t, error);
    Check(ok, "R2.3 commented-out UBO translates");
    Check(t.fragment.present && t.fragment.uniformBlocks.empty() &&
              t.vertex.uniformBlocks.empty(),
          "R2.3 no uniform blocks created from the comment");
  }

  // ------------------------------------------------------------------
  // R2.4: commented-out sampler declaration must not reach the rewriters.
  {
    WriteFile(tmp / "shaders" / "commented_sampler.slang", R"(
#pragma name commented_sampler_test

// uniform sampler2D Ghost;

#pragma stage vertex
layout(location = 0) in vec4 Position;
layout(location = 1) in vec2 TexCoord;
layout(location = 0) out vec2 vTexCoord;
void main() {
        gl_Position = Position;
        vTexCoord = TexCoord;
}

#pragma stage fragment
layout(location = 0) in vec2 vTexCoord;
layout(location = 0) out vec4 FragColor;
uniform sampler2D Source;
void main() {
        FragColor = texture(Source, vTexCoord);
}
)");
    SlangTranslation t;
    std::string error;
    const bool ok =
        Translate(tmp / "shaders" / "commented_sampler.slang", t, error);
    Check(ok, "R2.4 commented-out sampler translates");
    Check(!Contains(t.warnings, "Ghost"),
          "R2.4 no warning for the commented sampler");
  }

  // ------------------------------------------------------------------
  // R2.5: undeclared texture modifier key is ignored (no pathless entry).
  // Round-2 field log: crt-geom-deluxe-ntsc-adaptive's "delta_filter_linear"
  // created a pathless `delta_filter` texture and failed the preset.
  {
    WriteFile(tmp / "presets" / "modifier_orphan.slangp", R"(
shaders = 1
shader0 = plain.slang
delta_filter_linear = true
delta_filter_wrap_mode = "clamp_to_edge"
)");
    WriteFile(tmp / "presets" / "plain.slang", R"(
#pragma name plain_test

#pragma stage vertex
layout(location = 0) in vec4 Position;
layout(location = 1) in vec2 TexCoord;
layout(location = 0) out vec2 vTexCoord;
void main() {
        gl_Position = Position;
        vTexCoord = TexCoord;
}

#pragma stage fragment
layout(location = 0) in vec2 vTexCoord;
layout(location = 0) out vec4 FragColor;
uniform sampler2D Source;
void main() {
        FragColor = texture(Source, vTexCoord);
}
)");
    Preset preset;
    std::string error;
    const bool parsed = ParsePreset(
        (tmp / "presets" / "modifier_orphan.slangp").string(), preset, error);
    Check(parsed, "R2.5 preset with undeclared modifier parses");
    Check(parsed && preset.textures.empty(),
          "R2.5 no pathless texture entry created");
  }

  // ------------------------------------------------------------------
  // R2.6: modifiers on a REAL texture (X_path without a list) still apply.
  {
    WriteFile(tmp / "presets" / "modifier_real.slangp", R"(
shaders = 1
shader0 = plain.slang
noise_lut_path = "../luts/noise.png"
noise_lut_linear = true
noise_lut_mipmap = true
)");
    Preset preset;
    std::string error;
    const bool parsed = ParsePreset(
        (tmp / "presets" / "modifier_real.slangp").string(), preset, error);
    Check(parsed, "R2.6 preset with X_path texture parses");
    Check(parsed && preset.textures.size() == 1, "R2.6 texture entry created");
    if (parsed && preset.textures.size() == 1) {
      Check(preset.textures[0].name == "noise_lut",
            "R2.6 texture name from X_path");
      Check(preset.textures[0].filter == FilterMode::Linear,
            "R2.6 linear modifier applied");
      Check(preset.textures[0].mipmap, "R2.6 mipmap modifier applied");
    }
  }

  // ------------------------------------------------------------------
  // Negative control: PassOutput0 in pass 0 still hard-fails causality.
  {
    WriteFile(tmp / "shaders" / "noncausal.slang", R"(
#pragma name noncausal_test

#pragma stage vertex
layout(location = 0) in vec4 Position;
layout(location = 1) in vec2 TexCoord;
layout(location = 0) out vec2 vTexCoord;
void main() {
        gl_Position = Position;
        vTexCoord = TexCoord;
}

#pragma stage fragment
layout(location = 0) in vec2 vTexCoord;
layout(location = 0) out vec4 FragColor;
uniform sampler2D Source;
uniform sampler2D PassOutput0;
void main() {
        FragColor = texture(Source, vTexCoord) + texture(PassOutput0, vTexCoord);
}
)");
    SlangTranslation t;
    std::string error;
    const bool ok = Translate(tmp / "shaders" / "noncausal.slang", t, error);
    Check(!ok, "CTL PassOutput0 in pass 0 fails");
    Check(t.error.find("PassOutput0 is not causal in pass 0") !=
              std::string::npos,
          "CTL causality error text");
  }

  // ------------------------------------------------------------------
  // Optional: real corpus round-2 presets (graceful skip without corpus).
  if (argc > 1) {
    const std::string corpus = argv[1];
    const char *fieldPresets[] = {
        "bezel/uborder/base_presets/koko-ambi/koko-ambi-crt-geom.slangp",
        "presets/crt-plus-signal/crt-geom-deluxe-ntsc-adaptive.slangp",
    };
    for (const char *rel : fieldPresets) {
      const std::filesystem::path p = std::filesystem::path(corpus) / rel;
      if (!std::filesystem::exists(p)) {
        std::printf("skip %s (no corpus)\n", rel);
        continue;
      }
      Preset preset;
      std::string error;
      Check(ParsePreset(p.string(), preset, error), rel);
    }
  }

  std::printf("%d checks, %d failures\n", checks, failures);
  return failures == 0 ? 0 : 1;
}
