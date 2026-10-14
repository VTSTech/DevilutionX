// Standalone logic test for field round 1 (no SDL needed).
// The first field log (game-side gpu-scaler log, 2026-10-14) surfaced four
// failure classes, each verified against the libretro/slang-shaders corpus:
//  N1: #pragma parameter lines with trailing comments were silently dropped
//      by the parser regex (it required end-of-line after the step value).
//      58 corpus lines carry a trailing comment; scanlines-Modern lost
//      densY/densX/colAtten this way.
//  N2: comma-separated UBO declarators WITHOUT spaces ("float a,b,c;")
//      tokenized as one giant token and the comma-erase GLUED the names into
//      a single bogus member ("sc_satsc_blacksc_..."). 34+ corpus lines.
//  N3: unknown UBO members hard-failed translation; RetroArch leaves them at
//      zero. Downgraded to warn + zero-fill (auto_res, XBR_ANTI_RINGING,
//      ia_FLIP_HORZ all hit this in the field).
//  N4: the translator's texture-name deduction looks names up lowercased but
//      the runner inserted aliases/#pragma names in original case only, so
//      any mixed-case alias (Pass0, PrePass0) failed deduction; runtime binds
//      were also case-sensitive.
// Copies of the affected logic in slang_translate.cpp / preset_runner.cpp.
// Build: g++ -O2 -Wall -Wextra -std=c++17 -o test_field_round1
// tools/audit/test_field_round1.cpp
#include <cctype>
#include <cstdio>
#include <regex>
#include <string>
#include <unordered_set>
#include <vector>

static int failures = 0;

static void Check(bool ok, const char *label) {
  if (!ok) {
    ++failures;
    std::printf("FAIL %s\n", label);
  } else {
    std::printf("ok   %s\n", label);
  }
}

// ---------------------------------------------------------------------------
// N1: copy of the fixed paramRe in slang_translate.cpp ExtractPragmas.
static const std::regex kParamRe(
    R"re(^\s*#pragma\s+parameter\s+(\w+)\s+"([^"]*)"\s+([-+0-9.eE]+)\s+([-+0-9.eE]+)\s+([-+0-9.eE]+)(?:\s+([-+0-9.eE]+))?(?:\s*(?://|/\*).*)?\s*$)re");

struct ParsedParam {
  std::string name;
  float value = 0;
};

static bool ParseParamLine(const std::string &line, ParsedParam &out) {
  std::smatch m;
  if (!std::regex_match(line, m, kParamRe))
    return false;
  out.name = m[1].str();
  out.value = std::strtof(m[3].str().c_str(), nullptr);
  return true;
}

// ---------------------------------------------------------------------------
// N2: copy of the fixed declarator explode in ParseBlockMembers. The
// whitespace tokenizer is emulated: decl words split on spaces.
static std::vector<std::string> ExplodeNames(const std::string &declIn) {
  // The real ParseBlockMembers strips comments and cuts each declaration at
  // ';' before tokenizing - mirror that so inputs may carry the terminator.
  const size_t semi = declIn.find(';');
  const std::string decl =
      semi == std::string::npos ? declIn : declIn.substr(0, semi);
  std::vector<std::string> tokens;
  std::string cur;
  for (const char c : decl) {
    if (c == ' ' || c == '\t' || c == '\n') {
      if (!cur.empty()) {
        tokens.push_back(cur);
        cur.clear();
      }
    } else {
      cur += c;
    }
  }
  if (!cur.empty())
    tokens.push_back(cur);
  // Type is the first token naming a supported type; the rest are names.
  std::vector<std::string> nameTokens;
  size_t typeIdx = tokens.size();
  for (size_t t = 0; t + 1 < tokens.size(); ++t) {
    if (tokens[t] == "float" || tokens[t] == "vec2" || tokens[t] == "vec4" ||
        tokens[t] == "uint") {
      typeIdx = t;
      break;
    }
  }
  if (typeIdx >= tokens.size() - 1)
    return nameTokens;
  for (size_t n = typeIdx + 1; n < tokens.size(); ++n) {
    const std::string &list = tokens[n];
    size_t start = 0;
    while (start <= list.size()) {
      const size_t comma = list.find(',', start);
      const size_t end = comma == std::string::npos ? list.size() : comma;
      if (end > start)
        nameTokens.push_back(list.substr(start, end - start));
      start = end + 1;
    }
  }
  return nameTokens;
}

// ---------------------------------------------------------------------------
// N3: copy of the downgraded ValidateBlockMembers decision (whitelist +
// size forms + parameter names; unknown -> warning, not error).
static bool MemberOk(const std::string &name,
                     const std::unordered_set<std::string> &params) {
  bool ok =
      (name == "MVP" || name == "OutputSize" || name == "FinalViewportSize" ||
       name == "OriginalSize" || name == "SourceSize" || name == "FrameCount" ||
       name == "FrameDirection" || name == "FrameTime" || name == "PASSINDEX" ||
       name == "Rotation");
  if (!ok) {
    std::string base = name;
    bool sizeForm = false;
    if (base.size() > 4 && base.compare(base.size() - 4, 4, "Size") == 0) {
      base = base.substr(0, base.size() - 4);
      sizeForm = true;
    }
    if (sizeForm &&
        (base == "Original" || base == "Source" || base == "PassOutput0"))
      ok = true;
  }
  if (!ok) {
    for (const std::string &p : params) {
      if (p.size() == name.size() &&
          std::equal(p.begin(), p.end(), name.begin(), [](char a, char b) {
            return std::tolower(static_cast<unsigned char>(a)) ==
                   std::tolower(static_cast<unsigned char>(b));
          })) {
        ok = true;
        break;
      }
    }
  }
  return ok;
}

// ---------------------------------------------------------------------------
// N4: copy of insertKnownName (dual-case set) + CaseInsensitiveEqual.
static void InsertKnownName(std::unordered_set<std::string> &set,
                            const std::string &name) {
  set.insert(name);
  std::string lower;
  lower.reserve(name.size());
  for (const char c : name)
    lower += static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
  set.insert(lower);
}

static bool CaseInsensitiveEqual(const std::string &a, const std::string &b) {
  if (a.size() != b.size())
    return false;
  for (size_t i = 0; i < a.size(); ++i) {
    if (std::tolower(static_cast<unsigned char>(a[i])) !=
        std::tolower(static_cast<unsigned char>(b[i])))
      return false;
  }
  return true;
}

// Copy of the translator's deduction lookup: lowercased reference against the
// set.
static bool DeductionFinds(const std::unordered_set<std::string> &known,
                           const std::string &ref) {
  std::string baseLower;
  baseLower.reserve(ref.size());
  for (const char c : ref)
    baseLower += static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
  return known.find(baseLower) != known.end();
}

int main() {
  // --- N1: trailing comments ------------------------------------------------
  {
    ParsedParam p;
    Check(ParseParamLine("#pragma parameter densY \"Scanline Density\" 2.0 1.0 "
                         "6.0 1.0\t\t// Scanline density: 1 for 1080P",
                         p) &&
              p.name == "densY",
          "N1: pragma with trailing TAB + // comment parses");
    Check(ParseParamLine("#pragma parameter colAtten \"Attenuation\" 0.0 0.0 "
                         "2.0 0.05\t// RGB chromatic",
                         p) &&
              p.name == "colAtten",
          "N1: pragma with single-tab comment parses");
    Check(
        ParseParamLine(
            "#pragma parameter X \"T\" 0.5 0.0 1.0 0.1 /* range note */", p) &&
            p.name == "X",
        "N1: pragma with /* */ comment parses");
    Check(ParseParamLine("#pragma parameter plain \"P\" 1.0 0.0 2.0 0.01", p) &&
              p.name == "plain",
          "N1: clean pragma still parses (no regression)");
    Check(ParseParamLine("#pragma parameter nocomment \"N\" 1.0 0.0 2.0", p) &&
              p.name == "nocomment",
          "N1: 3-number form (no step) still parses");
    Check(!ParseParamLine("#pragma parameter bad \"B\" 1.0 0.0 junk 0.1", p),
          "N1: non-numeric range still rejected");
    Check(!ParseParamLine("#pragma parameter short \"S\" 1.0 0.0", p),
          "N1: too few numbers still rejected");
  }

  // --- N2: comma declarators -------------------------------------------------
  {
    // Real corpus form (simple_color_controls.slang:7): no spaces.
    const auto names = ExplodeNames(
        "float "
        "sc_sat,sc_black,sc_temp,sc_sega,sc_brightness,sc_postbr,sc_contrast,");
    Check(names.size() == 7 && names[0] == "sc_sat" &&
              names[6] == "sc_contrast",
          "N2: no-space comma list explodes to 7 members");
    // The old comma-erase produced 'sc_satsc_blacksc_...' - pin the absence.
    bool glued = false;
    for (const auto &n : names)
      glued = glued || n.find("sc_satsc") != std::string::npos;
    Check(!glued, "N2: no glued member names");
    const auto spaced = ExplodeNames("float STRENGTH_PUSH, STRENGTH_GRAD;");
    Check(spaced.size() == 2 && spaced[0] == "STRENGTH_PUSH" &&
              spaced[1] == "STRENGTH_GRAD",
          "N2: spaced comma list still works (anime4k)");
    const auto arr = ExplodeNames("float taps[2], weight;");
    Check(arr.size() == 2 && arr[0] == "taps[2]" && arr[1] == "weight",
          "N2: array declarator in comma list keeps its size");
    const auto single = ExplodeNames("float auto_res;");
    Check(single.size() == 1 && single[0] == "auto_res",
          "N2: single declarator unchanged");
  }

  // --- N3: unknown members warn instead of failing
  // ----------------------------
  {
    const std::unordered_set<std::string> scanParams = {
        "sincompx", "sincompy", "densy", "densx", "colatten"};
    Check(MemberOk("densY", scanParams),
          "N3: member matching a parameter (CI) is accepted");
    Check(MemberOk("FrameCount", scanParams), "N3: builtin accepted");
    Check(MemberOk("SourceSize", scanParams), "N3: size form accepted");
    Check(MemberOk("Rotation", scanParams), "N3: Rotation builtin accepted");
    Check(!MemberOk("totally_unknown", scanParams),
          "N3: unknown member flagged...");
    // ...but as a warning: the caller continues instead of failing the preset.
    const char *members[] = {"auto_res", "SourceSize", "XBR_ANTI_RINGING"};
    int unknown = 0;
    for (const char *m : members)
      unknown += MemberOk(m, scanParams) ? 0 : 1;
    Check(unknown == 2,
          "N3: two unknowns collected as warnings, loop continues");
  }

  // --- N4: case-insensitive deduction + binding
  // --------------------------------
  {
    std::unordered_set<std::string> known;
    // Preset: alias0 = Pass0, alias3 = PrePass0 (with trailing space, trimmed).
    InsertKnownName(known, "Pass0");
    InsertKnownName(known, "PrePass0");
    InsertKnownName(known, "hqx_refpass"); // already-lowercase precedent
    Check(DeductionFinds(known, "Pass0"),
          "N4: alias 'Pass0' found by deduction");
    Check(DeductionFinds(known, "PASS0"),
          "N4: alias found regardless of reference case");
    Check(DeductionFinds(known, "PrePass0"),
          "N4: 'PrePass0' found by deduction");
    Check(DeductionFinds(known, "hqx_refpass"),
          "N4: lowercase alias still found");
    Check(!DeductionFinds(known, "NoSuch"),
          "N4: unknown reference still rejected");

    std::unordered_set<std::string> viaPragma;
    InsertKnownName(viaPragma, "StockPass");
    Check(DeductionFinds(viaPragma, "stockpass"),
          "N4: #pragma name works case-insensitively too");

    // Runtime bind compares.
    Check(CaseInsensitiveEqual("Pass0", "pass0") &&
              CaseInsensitiveEqual("PrePass0", "prepass0"),
          "N4: runtime CaseInsensitiveEqual binds mixed-case aliases");
    Check(!CaseInsensitiveEqual("Pass0", "Pass1"),
          "N4: distinct names still distinct");
  }

  std::printf(failures == 0 ? "\nALL PASS\n" : "\n%d FAILURES\n", failures);
  return failures == 0 ? 0 : 1;
}
