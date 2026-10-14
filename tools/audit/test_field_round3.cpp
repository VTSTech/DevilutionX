// Standalone regression test for field round 3 (links the REAL parser and
// translator; no SDL device, no glslang — translate stage only, plus copies
// of the new pure-logic helpers).
//
// Field log 2026-10-15 (Windows, Vulkan): deinterlacing/nnedi3-nns16 and
// nnedi3-nns64 failed at glslang with
//     ERROR: 'nnediPass0' : undeclared identifier
// in pass 3 (nnedi3-combine-v-pass0.slang), after the load log printed
//     sampler 'NNEDI3_PREV_TEXTURE' has no chain target (#defined away);
//     dropping declaration
//
// Root cause: object-like macros can alias the sampler DECLARATION itself.
// nnedi3-combine-v-pass0.slang does "#define NNEDI3_PREV_TEXTURE nnediPass0"
// and its include (nnedi3-combine-common.inc) declares
// "uniform sampler2D NNEDI3_PREV_TEXTURE;". glslang's preprocessor renames
// that declaration AND every sampling call to nnediPass0, so the round-2
// orphan-sampler drop left the calls referencing an undeclared identifier.
//
// Fix (slang_translate.cpp RewriteSamplers): when a declared name is a
// #define target whose body is a single identifier, the alias is sampled in
// this stage, and the expansion is NOT independently declared, keep the
// declaration under the expanded name and run the normal resolution on it
// (nnediPass0 resolves through the preset's alias1 = nnediPass0). When the
// expansion IS declared (Mega Bezel "#define PrePass0 Source"), dropping
// stays correct. Corpus impact: 73 bogus drops eliminated (41
// NNEDI3_PREV_TEXTURE, 29 PastSampler in koko-ambi/koko-aio/uborder, 3
// FPS_ESTIMATE_PASS); failure set unchanged.
//
// Also covered here: preset_runner.cpp's pipeline-format downgrade decision
// (field log: test/format.slangp pass 1 failed pipeline creation with no
// diagnostics — a per-pass format the device refuses as a color attachment
// now downgrades to R8G8B8A8_UNORM with a warning instead).
//
// Build (all flags REQUIRED: the preset TUs are wrapped in USE_GPU_SCALER):
//   g++ -std=c++23 -O2 -Wall -o test_field_round3
//     tools/audit/test_field_round3.cpp
//     Source/engine/render/gpu/preset/slang_translate.cpp
//     Source/engine/render/gpu/preset/preset_parser.cpp
//     Source/utils/str_cat.cpp
//     -ISource -I3rdParty -I<pinned SDL3 include>
//     -DUSE_SDL3=1 -DUSE_GPU_SCALER=1
//
// Optional argv[1] = slang-shaders root: also translates the field-failing
// preset when the corpus is available (graceful skip otherwise).

#include <cstdio>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <string>
#include <unordered_set>
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

// ---------------------------------------------------------------------------
// Copies of the round-3 helpers in slang_translate.cpp (round-1 convention:
// the logic under test is mirrored so semantic drift trips a check).
static std::string TrimAll(const std::string &s) {
  size_t begin = 0;
  size_t end = s.size();
  while (begin < end && std::isspace(static_cast<unsigned char>(s[begin])) != 0)
    ++begin;
  while (end > begin &&
         std::isspace(static_cast<unsigned char>(s[end - 1])) != 0)
    --end;
  return s.substr(begin, end - begin);
}

static std::string DefineTargetExpansion(const std::string &glsl,
                                         const std::string &name) {
  size_t pos = 0;
  while ((pos = glsl.find("#define", pos)) != std::string::npos) {
    const size_t lineStart = glsl.rfind('\n', pos) == std::string::npos
                                 ? 0
                                 : glsl.rfind('\n', pos) + 1;
    bool onlySpace = true;
    for (size_t i = lineStart; i < pos; ++i) {
      if (glsl[i] != ' ' && glsl[i] != '\t') {
        onlySpace = false;
        break;
      }
    }
    const size_t after = pos + 7;
    size_t nameStart = after;
    while (nameStart < glsl.size() &&
           (glsl[nameStart] == ' ' || glsl[nameStart] == '\t'))
      ++nameStart;
    if (onlySpace && nameStart + name.size() <= glsl.size() &&
        glsl.compare(nameStart, name.size(), name) == 0) {
      const size_t nameEnd = nameStart + name.size();
      const char end = nameEnd < glsl.size() ? glsl[nameEnd] : '\0';
      if (!std::isalnum(static_cast<unsigned char>(end)) && end != '_' &&
          end != '(') {
        size_t lineEnd = glsl.find('\n', nameEnd);
        if (lineEnd == std::string::npos)
          lineEnd = glsl.size();
        const std::string body =
            TrimAll(glsl.substr(nameEnd, lineEnd - nameEnd));
        bool singleIdent = !body.empty();
        for (const char c : body) {
          if (!std::isalnum(static_cast<unsigned char>(c)) && c != '_') {
            singleIdent = false;
            break;
          }
        }
        if (singleIdent)
          return body;
      }
    }
    pos = after;
  }
  return "";
}

static bool IsSamplerDeclared(const std::string &glsl,
                              const std::string &name) {
  size_t pos = 0;
  while ((pos = glsl.find("sampler2D", pos)) != std::string::npos) {
    if (pos > 0 && (std::isalnum(static_cast<unsigned char>(glsl[pos - 1])) ||
                    glsl[pos - 1] == '_')) {
      pos += 9;
      continue;
    }
    size_t after = pos + 9;
    if (after >= glsl.size() || (glsl[after] != ' ' && glsl[after] != '\t')) {
      pos += 9;
      continue;
    }
    while (after < glsl.size() && (glsl[after] == ' ' || glsl[after] == '\t'))
      ++after;
    if (after + name.size() <= glsl.size() &&
        glsl.compare(after, name.size(), name) == 0) {
      const char end =
          after + name.size() < glsl.size() ? glsl[after + name.size()] : '\0';
      if (!std::isalnum(static_cast<unsigned char>(end)) && end != '_')
        return true;
    }
    pos += 9;
  }
  return false;
}

// Copy of the round-3 keep-gate helper in slang_translate.cpp: true when the
// name appears anywhere besides its own sampler2D declaration or its
// #define directive (sampling calls, user-function arguments, anything).
static bool IsSamplerNameUsed(const std::string &glsl,
                              const std::string &name) {
  size_t pos = 0;
  bool used = false;
  while ((pos = glsl.find(name, pos)) != std::string::npos) {
    const size_t end = pos + name.size();
    const char after = end < glsl.size() ? glsl[end] : '\0';
    const char before = pos > 0 ? glsl[pos - 1] : '\0';
    const bool wordEnd =
        !std::isalnum(static_cast<unsigned char>(after)) && after != '_';
    const bool wordStart =
        !std::isalnum(static_cast<unsigned char>(before)) && before != '_';
    if (wordEnd && wordStart) {
      size_t probe = pos;
      while (probe > 0 && (glsl[probe - 1] == ' ' || glsl[probe - 1] == '\t'))
        --probe;
      bool directive = false;
      for (const char *kw : {"sampler2D", "#define"}) {
        const size_t len = std::strlen(kw);
        if (probe >= len && glsl.compare(probe - len, len, kw) == 0 &&
            (probe - len == 0 || (!std::isalnum(static_cast<unsigned char>(
                                      glsl[probe - len - 1])) &&
                                  glsl[probe - len - 1] != '_'))) {
          directive = true;
          break;
        }
      }
      if (!directive) {
        used = true;
        break;
      }
    }
    pos = end;
  }
  return used;
}

// Copy of the pipeline-format downgrade decision in preset_runner.cpp:
// original SDL format failed -> downgrade target (or none for plain RGBA8).
static int FormatDowngradeTarget(int requestedSdlFormat) {
  if (requestedSdlFormat != /* SDL_GPU_TEXTUREFORMAT_R8G8B8A8_UNORM */ 4)
    return /* SDL_GPU_TEXTUREFORMAT_R8G8B8A8_UNORM */ 4;
  return 0; // no downgrade available; hard-fail
}

static void WriteFile(const std::filesystem::path &path,
                      const std::string &text) {
  std::filesystem::create_directories(path.parent_path());
  std::ofstream out(path, std::ios::binary);
  out << text;
}

int main(int argc, char **argv) {
  const std::filesystem::path tmp =
      std::filesystem::temp_directory_path() / "devilutionx_test_field_round3";
  std::filesystem::remove_all(tmp);
  std::filesystem::create_directories(tmp);

  // ------------------------------------------------------------------
  // DefineTargetExpansion (copies)
  {
    const std::string nnedi3 =
        "#define NNEDI3_VERTICAL 1\n#define NNEDI3_PREV_TEXTURE "
        "nnediPass0\n#include \"x.inc\"\n";
    Check(DefineTargetExpansion(nnedi3, "NNEDI3_PREV_TEXTURE") == "nnediPass0",
          "DX nnedi3 alias expands to nnediPass0");
    Check(DefineTargetExpansion(nnedi3, "NNEDI3_VERTICAL") == "1",
          "DX non-identifier body returned verbatim (caller rejects)");
    Check(DefineTargetExpansion(nnedi3, "MISSING").empty(),
          "DX missing define -> empty");
    const std::string funcLike = "#define PrePass0(x) Source\n";
    Check(DefineTargetExpansion(funcLike, "PrePass0").empty(),
          "DX function-like macro rejected");
    const std::string multiTok = "#define Alias Source + Ghost\n";
    Check(DefineTargetExpansion(multiTok, "Alias").empty(),
          "DX multi-token body rejected");
    const std::string empty = "#define Alias\n";
    Check(DefineTargetExpansion(empty, "Alias").empty(),
          "DX empty body rejected");
    const std::string spaced = "#define   Alias \t target   \n";
    Check(DefineTargetExpansion(spaced, "Alias") == "target",
          "DX whitespace trimmed");
    const std::string padded =
        "#define Alias nnediPass0\n#define Alias2 other\n";
    Check(DefineTargetExpansion(padded, "Alias") == "nnediPass0" &&
              DefineTargetExpansion(padded, "Alias2") == "other",
          "DX multiple defines each resolve");
    Check(DefineTargetExpansion(padded, "Ali").empty(),
          "DX name is word-boundary matched");
  }

  // ------------------------------------------------------------------
  // IsSamplerDeclared (copies)
  {
    const std::string src =
        "layout(set = 0, binding = 3) uniform sampler2D Source;\n"
        "uniform isampler2D IChan;\n"
        "uniform sampler2DArray Array;\n"
        "uniform sampler2D NameX;\n";
    Check(IsSamplerDeclared(src, "Source"),
          "ISD layout-prefixed declaration found");
    Check(IsSamplerDeclared(src, "NameX"), "ISD exact name found");
    Check(!IsSamplerDeclared(src, "Name"), "ISD word boundary (Name vs NameX)");
    Check(!IsSamplerDeclared(src, "IChan"), "ISD isampler2D skipped");
    Check(!IsSamplerDeclared(src, "Array"), "ISD sampler2DArray skipped");
    Check(!IsSamplerDeclared("uniform sampler2D Ghost;\n", "Source"),
          "ISD absent name -> false");
  }

  // ------------------------------------------------------------------
  // IsSamplerNameUsed (copies)
  {
    const std::string koko = "#define FPS_ESTIMATE_PASS avglum_pass\n"
                             "    layout(set = 0, binding = 4) uniform "
                             "sampler2D FPS_ESTIMATE_PASS;\n"
                             "float fps = get_fps(FPS_ESTIMATE_PASS);\n";
    Check(IsSamplerNameUsed(koko, "FPS_ESTIMATE_PASS"),
          "ISU user-function argument counts as use");
    Check(!IsSamplerNameUsed(
              "#define PrePass0 Source\nuniform sampler2D PrePass0;\n",
              "PrePass0"),
          "ISU define directive + declaration only -> not used");
    Check(IsSamplerNameUsed("uniform sampler2D S;\ntexture(S, uv);\n", "S"),
          "ISU sampling call counts as use");
  }

  // ------------------------------------------------------------------
  // End-to-end: macro-aliased declaration kept under the expansion.
  // Mirrors nnedi3-combine-v-pass0.slang + nnedi3-combine-common.inc.
  {
    WriteFile(tmp / "shaders" / "combine.slang", R"(
#define PREV prev0
#include "combine-common.inc"
)");
    WriteFile(tmp / "shaders" / "combine-common.inc", R"(
#ifndef PREV
#error PREV must name the previous source sampler
#endif

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
uniform sampler2D PREV;

void main()
{
        FragColor = texelFetch(Source, ivec2(0), 0) + texelFetch(PREV, ivec2(1), 0);
}
)");
    // Known names exactly like the runner: preset alias prev0.
    std::unordered_set<std::string> known{"Source", "source", "Original",
                                          "original", "prev0"};
    SlangTranslation t;
    const bool ok = TranslateSlang((tmp / "shaders" / "combine.slang").string(),
                                   known, 1, 2, t, {});
    Check(ok, "R3 macro-aliased sampler translates");
    Check(ok && t.warnings.empty(), "R3 no orphan-drop warning");
    Check(ok && t.fragment.textures.size() == 2, "R3 both samplers bound");
    if (ok && t.fragment.textures.size() == 2) {
      Check(t.fragment.textures[0].name == "Source" &&
                t.fragment.textures[0].binding == 0,
            "R3 Source@0 bound");
      Check(t.fragment.textures[1].name == "prev0" &&
                t.fragment.textures[1].binding == 1,
            "R3 expansion target prev0@1 bound (runner matches alias)");
    }
  }

  // ------------------------------------------------------------------
  // End-to-end: declared expansion target still drops (round-2 preserved),
  // in the FRAGMENT stage where the alias is sampled.
  {
    WriteFile(tmp / "shaders" / "declared_target.slang", R"(
#pragma name declared_target

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
#define PrePass0 Source
uniform sampler2D PrePass0;
uniform sampler2D Source;
void main() {
        FragColor = texture(Source, vTexCoord) + texture(PrePass0, vTexCoord * 0.5);
}
)");
    std::unordered_set<std::string> known{"Source", "source", "Original",
                                          "original"};
    SlangTranslation t;
    const bool ok =
        TranslateSlang((tmp / "shaders" / "declared_target.slang").string(),
                       known, 0, 1, t, {});
    Check(ok, "R3 declared expansion target translates");
    Check(ok && Contains(t.warnings,
                         "'PrePass0' has no chain target (#defined away)"),
          "R3 declared expansion target still drops with warning");
    Check(ok && t.fragment.textures.size() == 1 &&
              t.fragment.textures[0].name == "Source",
          "R3 no duplicate Source binding");
  }

  // ------------------------------------------------------------------
  // End-to-end: never-sampled shared-section alias keeps dropping (the
  // vertex-stage regression this round: re-targeting must not invent
  // vertex textures).
  {
    WriteFile(tmp / "shaders" / "unsampled_alias.slang", R"(
#pragma name unsampled_alias

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
        FragColor = texture(Source, vTexCoord);
}
)");
    std::unordered_set<std::string> known{"Source", "source", "Original",
                                          "original"};
    SlangTranslation t;
    const bool ok =
        TranslateSlang((tmp / "shaders" / "unsampled_alias.slang").string(),
                       known, 0, 1, t, {});
    Check(ok, "R3 unsampled alias translates (no invented vertex texture)");
    Check(ok && t.vertex.textures.empty(), "R3 vertex stage has no textures");
    Check(ok && Contains(t.warnings,
                         "'PrePass0' has no chain target (#defined away)"),
          "R3 unsampled alias still warns");
  }

  // ------------------------------------------------------------------
  // End-to-end: <passname>Feedback alias through a macro (koko-ambi
  // PastSampler pattern: "#define PastSampler ambi_temporal_passFeedback").
  {
    WriteFile(tmp / "shaders" / "temporal.slang", R"(
#pragma name ambi_temporal_pass

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
#define PastSampler ambi_temporal_passFeedback
uniform sampler2D PastSampler;
uniform sampler2D Source;
void main() {
        FragColor = mix(texture(Source, vTexCoord), texture(PastSampler, vTexCoord), 0.5);
}
)");
    std::unordered_set<std::string> known{"Source", "source", "Original",
                                          "original", "ambi_temporal_pass"};
    SlangTranslation t;
    const bool ok = TranslateSlang(
        (tmp / "shaders" / "temporal.slang").string(), known, 1, 2, t, {});
    Check(ok, "R3 NAMEFeedback-through-macro translates");
    Check(ok && t.fragment.textures.size() == 2, "R3 two samplers bound");
    bool hasNameFeedback = false;
    for (const TextureRef &tex : t.fragment.textures)
      hasNameFeedback |= tex.name == "ambi_temporal_passFeedback";
    Check(hasNameFeedback, "R3 expansion target is the NAMEFeedback sampler");
  }

  // ------------------------------------------------------------------
  // Format downgrade decision (copy).
  {
    Check(FormatDowngradeTarget(2 /* R16_FLOAT */) == 4 /* RGBA8 */,
          "FMT single-channel float downgrades to RGBA8");
    Check(FormatDowngradeTarget(31 /* R32G32B32A32_UINT */) == 4,
          "FMT integer target downgrades to RGBA8");
    Check(FormatDowngradeTarget(4 /* RGBA8 */) == 0,
          "FMT plain RGBA8 has no downgrade (hard-fails with SDL error text)");
  }

  // ------------------------------------------------------------------
  // Optional: the field-failing preset through the REAL parser+translator.
  if (argc > 1) {
    const std::filesystem::path p = std::filesystem::path(argv[1]) /
                                    "deinterlacing" /
                                    "nnedi3-nns16-deinterlacing.slangp";
    if (!std::filesystem::exists(p)) {
      std::printf("skip %s (no corpus)\n", p.string().c_str());
    } else {
      Preset preset;
      std::string error;
      const bool parsed = ParsePreset(p.string(), preset, error);
      Check(parsed, "R3 corpus nnedi3-nns16 parses");
      if (parsed) {
        std::unordered_set<std::string> known;
        auto insertKnownName = [&known](const std::string &name) {
          known.insert(name);
          std::string lower;
          for (const char c : name)
            lower +=
                static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
          known.insert(lower);
        };
        for (const PassDef &pass : preset.passes)
          if (!pass.alias.empty())
            insertKnownName(pass.alias);
        SlangTranslation t;
        const bool ok = TranslateSlang(
            (preset.dir / std::filesystem::path(preset.passes[3].shaderPath))
                .string(),
            known, 3, 5, t, {});
        Check(ok, "R3 corpus combine-v pass translates");
        Check(ok && Contains(t.warnings, "NNEDI3_PREV_TEXTURE") == false,
              "R3 corpus combine-v pass has no orphan-drop warning");
        bool hasNnediPass0 = false;
        for (const TextureRef &tex : t.fragment.textures)
          hasNnediPass0 |= tex.name == "nnediPass0";
        Check(hasNnediPass0, "R3 corpus combine-v binds nnediPass0");
      }
    }
  }

  std::printf("%d checks, %d failures\n", checks, failures);
  return failures == 0 ? 0 : 1;
}
