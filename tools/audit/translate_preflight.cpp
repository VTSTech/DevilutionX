// Offline translate-stage pre-flight for slang presets.
// Runs the REAL preset parser + REAL slang translator over the presets named
// on the command line (no SDL device, no glslang - the translate stage only)
// and prints the per-pass report the field log would show: parse result
// (#reference merges included), translation ok/error/warnings, UBO members
// (Rotation visible), texture binds, semantic flags, mipmap/srgb/float/scale
// rules, #pragma name/format.
//
// Pass ABSOLUTE preset paths: the path resolver walks parent directories, and
// a relative path never reaches the shader root (game parity).
//
// Build (all flags REQUIRED: the preset TUs are wrapped in USE_GPU_SCALER):
//   g++ -std=c++23 -O2 -Wall -o translate_preflight
//     tools/audit/translate_preflight.cpp
//     Source/engine/render/gpu/preset/slang_translate.cpp
//     Source/engine/render/gpu/preset/preset_parser.cpp
//     Source/utils/str_cat.cpp
//     -ISource -I3rdParty
//     -I<pinned SDL3 include> -DUSE_SDL3=1 -DUSE_GPU_SCALER=1
#include <cctype>
#include <cstdio>
#include <regex>
#include <string>
#include <unordered_set>
#include <vector>

#include "engine/render/gpu/preset/preset_parser.hpp"
#include "engine/render/gpu/preset/preset_types.hpp"
#include "engine/render/gpu/preset/slang_translate.hpp"

using namespace devilution::gpu_scaler::preset;

// ---- link stubs (the real device-side entry points are not pulled in) ----
extern "C" const char *SDL_GetBasePath(void) { return "./"; }
void app_fatal(std::string_view) {}

static std::string Lower(const std::string &name) {
  std::string lower;
  lower.reserve(name.size());
  for (const char c : name)
    lower += static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
  return lower;
}

static const char *ScaleTypeName(ScaleType t) {
  switch (t) {
  case ScaleType::Source:
    return "source";
  case ScaleType::Viewport:
    return "viewport";
  case ScaleType::Absolute:
    return "absolute";
  }
  return "?";
}

static std::string TypeName(UniformType t, int arr) {
  const char *base = "?";
  switch (t) {
  case UniformType::Float:
    base = "float";
    break;
  case UniformType::Vec2:
    base = "vec2";
    break;
  case UniformType::Vec3:
    base = "vec3";
    break;
  case UniformType::Vec4:
    base = "vec4";
    break;
  case UniformType::Int:
    base = "int";
    break;
  case UniformType::IVec2:
    base = "ivec2";
    break;
  case UniformType::IVec3:
    base = "ivec3";
    break;
  case UniformType::IVec4:
    base = "ivec4";
    break;
  case UniformType::Uint:
    base = "uint";
    break;
  case UniformType::UVec2:
    base = "uvec2";
    break;
  case UniformType::UVec3:
    base = "uvec3";
    break;
  case UniformType::UVec4:
    base = "uvec4";
    break;
  case UniformType::Bool:
    base = "bool";
    break;
  case UniformType::Mat4:
    base = "mat4";
    break;
  }
  return arr > 0 ? std::string(base) + "[" + std::to_string(arr) + "]"
                 : std::string(base);
}

static const char *YesNo(bool v) { return v ? "yes" : "no"; }

int main(int argc, char **argv) {
  static const std::regex nameRe(R"(^\s*#pragma\s+name\s+(\w+)\s*$)");
  int rc = 0;

  for (int a = 1; a < argc; ++a) {
    const std::string path = argv[a];
    std::printf("=== PRESET %s\n", path.c_str());
    Preset preset;
    std::string err;
    if (!ParsePreset(path, preset, err)) {
      std::printf("  PARSE-FAIL: %s\n", err.c_str());
      rc = 1;
      continue;
    }
    std::printf(
        "  passes=%zu feedback=%d histories=%d paramOverrides=%zu LUTs=%zu\n",
        preset.passes.size(), preset.feedbackPass, preset.histories,
        preset.parameters.size(), preset.textures.size());
    for (const TextureDef &tex : preset.textures)
      std::printf("  LUT %s <- %s (linear=%s mipmap=%s wrap=%d)\n",
                  tex.name.c_str(), tex.path.c_str(),
                  tex.filter == FilterMode::Linear ? "yes" : "no",
                  YesNo(tex.mipmap), static_cast<int>(tex.wrap));

    // Known texture names exactly like the runner: aliases, #pragma
    // names and LUT names, each in original + lowercased form.
    std::unordered_set<std::string> knownNames;
    auto insertKnownName = [&knownNames](const std::string &name) {
      knownNames.insert(name);
      knownNames.insert(Lower(name));
    };
    for (const PassDef &pass : preset.passes) {
      if (!pass.alias.empty())
        insertKnownName(pass.alias);
    }
    std::vector<std::string> pragmaNames(preset.passes.size());
    for (size_t i = 0; i < preset.passes.size(); ++i) {
      const std::string slangPath =
          ResolvePresetFilePath(preset.dir, preset.passes[i].shaderPath);
      FILE *f = std::fopen(slangPath.c_str(), "rb");
      if (f == nullptr) {
        std::printf("  PASS %zu shader UNREADABLE: %s\n", i, slangPath.c_str());
        rc = 1;
        continue;
      }
      std::string text;
      char buf[65536];
      size_t n;
      while ((n = std::fread(buf, 1, sizeof(buf), f)) > 0)
        text.append(buf, n);
      std::fclose(f);
      std::istringstream lines(text);
      std::string line;
      while (std::getline(lines, line)) {
        std::smatch match;
        if (std::regex_match(line, match, nameRe)) {
          pragmaNames[i] = match[1].str();
          break;
        }
      }
      if (!pragmaNames[i].empty())
        insertKnownName(pragmaNames[i]);
    }
    for (const TextureDef &tex : preset.textures)
      insertKnownName(tex.name);

    std::vector<ParamInfo> presetParams;
    for (const ParamOverride &override : preset.parameters) {
      ParamInfo info;
      info.name = override.name;
      info.defaultValue = override.value;
      presetParams.push_back(info);
    }

    const size_t passCount = preset.passes.size();
    for (size_t i = 0; i < passCount; ++i) {
      const PassDef &def = preset.passes[i];
      std::printf(
          "  PASS %zu/%zu %s alias=%s mipmap=%s srgb=%s float=%s fmt=%s "
          "scale=%s*%g/%s*%g pragmaName=%s\n",
          i + 1, passCount, def.shaderPath.c_str(),
          def.alias.empty() ? "-" : def.alias.c_str(), YesNo(def.mipmapInput),
          YesNo(def.srgbFramebuffer), YesNo(def.floatFramebuffer),
          def.pragmaFormat.empty() ? "-" : def.pragmaFormat.c_str(),
          ScaleTypeName(def.scaleX.type), static_cast<double>(def.scaleX.scale),
          ScaleTypeName(def.scaleY.type), static_cast<double>(def.scaleY.scale),
          pragmaNames[i].empty() ? "-" : pragmaNames[i].c_str());
      const std::string slangPath =
          ResolvePresetFilePath(preset.dir, def.shaderPath);
      SlangTranslation t;
      const bool ok =
          TranslateSlang(slangPath, knownNames, static_cast<int>(i),
                         static_cast<int>(passCount), t, presetParams);
      if (!ok) {
        std::printf("    TRANSLATE-FAIL: %s\n", t.error.c_str());
        rc = 1;
        continue;
      }
      std::printf("    translated=OK pragmaFormat=%s mergedSize=%d\n",
                  t.pragmaFormat.empty() ? "-" : t.pragmaFormat.c_str(),
                  t.mergedSize);
      for (const std::string &w : t.warnings)
        std::printf("    warning: %s\n", w.c_str());
      for (const StageTranslation *stage : {&t.vertex, &t.fragment}) {
        if (!stage->present)
          continue;
        std::printf("    %s: mvp=%s frameCount=%s frameDirection=%s "
                    "frameTime=%s passIndex=%s texelFetch=%d textureGather=%d "
                    "textures=%zu blocks=%zu params=%zu\n",
                    stage == &t.vertex ? "vertex" : "fragment",
                    YesNo(stage->usesMvp), YesNo(stage->usesFrameCount),
                    YesNo(stage->usesFrameDirection),
                    YesNo(stage->usesFrameTime), YesNo(stage->usesPassIndex),
                    stage->texelFetchCount, stage->textureGatherCount,
                    stage->textures.size(), stage->uniformBlocks.size(),
                    t.parameters.size());
        for (const TextureRef &tex : stage->textures)
          std::printf("      tex %s@%u\n", tex.name.c_str(), tex.binding);
        for (const UniformBlock &block : stage->uniformBlocks) {
          std::printf("      block '%s' binding=%u members=%zu\n",
                      block.instanceName.c_str(), block.binding,
                      block.members.size());
          for (const UniformMember &m : block.members)
            std::printf("        %s %s @%u size=%u\n", m.name.c_str(),
                        TypeName(m.type, m.arraySize).c_str(), m.offset,
                        m.size);
        }
      }
    }
  }
  return rc;
}
