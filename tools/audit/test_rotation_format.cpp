// Standalone logic test for the SLANG_SPEC gap #5 + #6 round (no SDL needed).
// #5: the `Rotation` builtin uniform member is whitelisted by the translator's
//     ValidateBlockMembers and filled by Runner::FillBlockBytes with the
//     identity value 0 (RetroArch semantic: uint 0..3 = 0/90/180/270 degrees;
//     this frontend never rotates the view). 16 corpus shaders declare
//     `uniform uint Rotation;` and previously hard-failed at translate.
// #6: the `#pragma format` mapping in FormatForPass now covers the two-channel
//     integer tokens (R8G8/R16G16/R32G32 x UINT/SINT) and the packed 10-bit
//     UNORM token A2B10G10R10_UNORM_PACK32 (SDL names the layout
//     R10G10B10A2_UNORM; 18 corpus shaders request it). The only token still
//     unmapped is A2B10G10R10_UINT_PACK32 (no SDL GPU UINT form) -> warn +
//     RGBA8 fallback, per the established contract.
// Copies of the anonymous-namespace helpers and the validation/fill logic in
// slang_translate.cpp / preset_runner.cpp. The local Fmt enum mirrors the SDL
// GPU texture-format names used by the runner's kFormats table - keep the two
// in sync by name when the table changes.
// Build: g++ -O2 -Wall -Wextra -std=c++17 -o test_rotation_format
// tools/audit/test_rotation_format.cpp
#include <cstdint>
#include <cstdio>
#include <string>
#include <unordered_map>
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
// Copy of the builtin whitelist in slang_translate.cpp ValidateBlockMembers
// (with this round's Rotation addition).
static bool Whitelisted(const std::string &name) {
  bool ok = (name == "MVP" || name == "OutputSize" || name == "FinalViewportSize"
      || name == "OriginalSize" || name == "SourceSize" || name == "FrameCount"
      || name == "FrameDirection" || name == "FrameTime" || name == "PASSINDEX"
      || name == "Rotation");
  return ok;
}

// ---------------------------------------------------------------------------
// Mini std140 reflector for the member layouts this test pins. Scalars:
// float/int/uint/bool are 4 bytes, alignment 4 (the runner's UniformMember
// carries the same offsets for these forms).
struct Member {
  std::string type; // "float" | "uint" | "vec4" | "vec2" | "uvec4"
  std::string name;
  uint32_t offset = 0;
  uint32_t size = 0;
};

static void Reflect(std::vector<Member> &members) {
  uint32_t off = 0;
  for (Member &m : members) {
    uint32_t align = 4;
    m.size = 4;
    if (m.type == "vec2") {
      align = 8;
      m.size = 8;
    } else if (m.type == "vec4" || m.type == "uvec4") {
      align = 16;
      m.size = 16;
    }
    off = (off + align - 1) / align * align;
    m.offset = off;
    off += m.size;
  }
}

// Copy of the FillBlockBytes builtin-member fill for the Rotation member
// (after the real function's zero initialization of the block buffer).
static void FillRotation(const Member &m, std::vector<uint8_t> &out) {
  if (m.name != "Rotation")
    return;
  const uint32_t v = 0; // identity: non-rotating frontend
  for (uint32_t i = 0; i < 4 && m.offset + i < out.size(); ++i)
    out[m.offset + i] = static_cast<uint8_t>((v >> (8 * i)) & 0xFF);
}

// ---------------------------------------------------------------------------
// Copy of FormatForPass in preset_runner.cpp: fallback precedence and the
// #pragma format table (Fmt mirrors the SDL_GPU_TEXTUREFORMAT_* names).
enum Fmt {
  RGBA8_UNORM = 1,
  RGBA8_SRGB,
  R16G16B16A16_FLOAT,
  R8_UNORM,
  R8G8_UNORM,
  R8_UINT,
  R8G8_UINT,
  R8_SINT,
  R8G8_SINT,
  R16_FLOAT,
  R16G16_FLOAT,
  R16_UINT,
  R16G16_UINT,
  R16_SINT,
  R16G16_SINT,
  R32_FLOAT,
  R32G32_FLOAT,
  R32_UINT,
  R32G32_UINT,
  R32_SINT,
  R32G32_SINT,
  RGBA16_FLOAT,
  RGBA8_UINT,
  RGBA16_UINT,
  RGBA32_UINT,
  RGBA32_FLOAT,
  RGBA8_INT,
  RGBA16_INT,
  RGBA32_INT,
  R10G10B10A2_UNORM,
  INVALID_FMT = 0xFF
};

struct FormatSpec {
  std::string pragmaFormat;
  bool srgbFramebuffer = false;
  bool floatFramebuffer = false;
};

static Fmt FormatForPass(const FormatSpec &pass, std::string &warning) {
  std::string formatName = pass.pragmaFormat;
  if (formatName.empty()) {
    if (pass.srgbFramebuffer)
      formatName = "R8G8B8A8_SRGB";
    else if (pass.floatFramebuffer)
      formatName = "R16G16B16A16_SFLOAT";
    else
      return RGBA8_UNORM;
  }

  static const std::unordered_map<std::string, Fmt> kFormats = {
    { "R8_UNORM", R8_UNORM },
    { "R8G8_UNORM", R8G8_UNORM },
    { "R8G8B8A8_UNORM", RGBA8_UNORM },
    { "R8G8B8A8_SRGB", RGBA8_SRGB },
    { "R16_SFLOAT", R16_FLOAT },
    { "R16G16_SFLOAT", R16G16_FLOAT },
    { "R16G16B16A16_SFLOAT", RGBA16_FLOAT },
    { "R32_SFLOAT", R32_FLOAT },
    { "R32G32_SFLOAT", R32G32_FLOAT },
    { "R32G32B32A32_SFLOAT", RGBA32_FLOAT },
    { "R8_UINT", R8_UINT },
    { "R8G8B8A8_UINT", RGBA8_UINT },
    { "R16_UINT", R16_UINT },
    { "R16G16B16A16_UINT", RGBA16_UINT },
    { "R32_UINT", R32_UINT },
    { "R32G32B32A32_UINT", RGBA32_UINT },
    { "R8_SINT", R8_SINT },
    { "R8G8B8A8_SINT", RGBA8_INT },
    { "R16_SINT", R16_SINT },
    { "R16G16B16A16_SINT", RGBA16_INT },
    { "R32_SINT", R32_SINT },
    { "R32G32B32A32_SINT", RGBA32_INT },
    // This round's additions (SLANG_SPEC gap #6).
    { "R8G8_UINT", R8G8_UINT },
    { "R8G8_SINT", R8G8_SINT },
    { "R16G16_UINT", R16G16_UINT },
    { "R16G16_SINT", R16G16_SINT },
    { "R32G32_UINT", R32G32_UINT },
    { "R32G32_SINT", R32G32_SINT },
    { "A2B10G10R10_UNORM_PACK32", R10G10B10A2_UNORM },
  };
  const auto it = kFormats.find(formatName);
  if (it == kFormats.end()) {
    warning = "unsupported #pragma format " + formatName + "; falling back to R8G8B8A8_UNORM";
    return RGBA8_UNORM;
  }
  return it->second;
}

int main() {
  // --- #5: whitelist -------------------------------------------------------
  Check(Whitelisted("Rotation"), "whitelist accepts Rotation");
  Check(Whitelisted("MVP") && Whitelisted("PASSINDEX") && Whitelisted("FrameTime"),
      "pre-existing builtins still accepted");
  Check(!Whitelisted("rotation"), "lowercase 'rotation' rejected (case-sensitive)");
  Check(!Whitelisted("ROTATION"), "uppercase 'ROTATION' rejected (case-sensitive)");
  Check(!Whitelisted("RotationX"), "suffix variant rejected");
  Check(!Whitelisted("Rot"), "random unknown member rejected");

  // --- #5: std140 offsets for `uint Rotation` in common placements ---------
  {
    std::vector<Member> m = { { "vec4", "SourceSize", 0, 0 }, { "uint", "Rotation", 0, 0 } };
    Reflect(m);
    Check(m[1].offset == 16 && m[1].size == 4, "uint Rotation after vec4 at offset 16");
  }
  {
    std::vector<Member> m = { { "vec2", "Offset", 0, 0 }, { "uint", "Rotation", 0, 0 } };
    Reflect(m);
    Check(m[1].offset == 8, "uint Rotation after vec2 at offset 8");
  }
  {
    std::vector<Member> m = { { "float", "Bare", 0, 0 }, { "uint", "Rotation", 0, 0 } };
    Reflect(m);
    Check(m[1].offset == 4, "uint Rotation after float at offset 4");
  }

  // --- #5: fill semantics --------------------------------------------------
  {
    std::vector<Member> m = { { "vec4", "SourceSize", 0, 0 }, { "uint", "Rotation", 0, 0 } };
    Reflect(m);
    std::vector<uint8_t> block(m.back().offset + m.back().size, 0);
    FillRotation(m[1], block);
    const bool zero = block[m[1].offset] == 0 && block[m[1].offset + 1] == 0
        && block[m[1].offset + 2] == 0 && block[m[1].offset + 3] == 0;
    Check(zero, "Rotation fills the identity value 0");
    std::vector<uint8_t> block2(m.back().offset + m.back().size, 0);
    FillRotation(m[1], block2);
    Check(block == block2, "Rotation is stable across frames (no rotation source)");
  }
  {
    // Degenerate-but-legal declaration form: all 16 bytes must read as 0.
    std::vector<Member> m = { { "uvec4", "Rotation", 0, 0 } };
    Reflect(m);
    std::vector<uint8_t> block(m[0].offset + m[0].size, 0);
    FillRotation(m[0], block);
    bool allZero = true;
    for (uint8_t b : block)
      allZero = allZero && b == 0;
    Check(allZero, "uvec4 Rotation form reads all-zero (vec4(0) identity)");
  }

  // --- #6: precedence ------------------------------------------------------
  {
    FormatSpec p;
    p.pragmaFormat = "R16_SFLOAT";
    p.srgbFramebuffer = true;
    std::string w;
    Check(FormatForPass(p, w) == R16_FLOAT && w.empty(),
        "#pragma format takes precedence over srgb_framebuffer");
  }
  {
    FormatSpec p;
    p.pragmaFormat = "R8_UNORM";
    p.floatFramebuffer = true;
    std::string w;
    Check(FormatForPass(p, w) == R8_UNORM && w.empty(),
        "#pragma format takes precedence over float_framebuffer");
  }
  {
    FormatSpec p;
    p.srgbFramebuffer = true;
    std::string w;
    Check(FormatForPass(p, w) == RGBA8_SRGB && w.empty(), "no pragma + srgb flag -> RGBA8 SRGB");
  }
  {
    FormatSpec p;
    p.floatFramebuffer = true;
    std::string w;
    Check(FormatForPass(p, w) == RGBA16_FLOAT && w.empty(), "no pragma + float flag -> RGBA16F");
  }
  {
    FormatSpec p;
    std::string w;
    Check(FormatForPass(p, w) == RGBA8_UNORM && w.empty(), "bare pass -> RGBA8 UNORM");
  }

  // --- #6: this round's token additions ------------------------------------
  {
    const std::pair<const char *, Fmt> added[] = {
      { "R8G8_UINT", R8G8_UINT },       { "R8G8_SINT", R8G8_SINT },
      { "R16G16_UINT", R16G16_UINT },   { "R16G16_SINT", R16G16_SINT },
      { "R32G32_UINT", R32G32_UINT },   { "R32G32_SINT", R32G32_SINT },
      { "A2B10G10R10_UNORM_PACK32", R10G10B10A2_UNORM },
    };
    for (const auto &e : added) {
      FormatSpec p;
      p.pragmaFormat = e.first;
      std::string w;
      Check(FormatForPass(p, w) == e.second && w.empty(), e.first);
    }
  }

  // --- #6: original table rows unchanged -----------------------------------
  {
    const std::pair<const char *, Fmt> kept[] = {
      { "R8_UNORM", R8_UNORM },
      { "R8G8B8A8_UNORM", RGBA8_UNORM },
      { "R8G8B8A8_SRGB", RGBA8_SRGB },
      { "R16G16B16A16_SFLOAT", RGBA16_FLOAT },
      { "R32G32B32A32_SFLOAT", RGBA32_FLOAT },
      { "R32_UINT", R32_UINT },
      { "R32G32B32A32_SINT", RGBA32_INT },
    };
    for (const auto &e : kept) {
      FormatSpec p;
      p.pragmaFormat = e.first;
      std::string w;
      Check(FormatForPass(p, w) == e.second && w.empty(), e.first);
    }
  }

  // --- #6: the one remaining unmapped token --------------------------------
  {
    FormatSpec p;
    p.pragmaFormat = "A2B10G10R10_UINT_PACK32";
    std::string w;
    Check(FormatForPass(p, w) == RGBA8_UNORM, "A2B10G10R10_UINT_PACK32 falls back to RGBA8");
    Check(w.find("A2B10G10R10_UINT_PACK32") != std::string::npos,
        "fallback warning names the unsupported token");
  }
  {
    FormatSpec p;
    p.pragmaFormat = "TOTALLY_MADE_UP";
    std::string w;
    Check(FormatForPass(p, w) == RGBA8_UNORM && !w.empty(), "garbage token falls back with warning");
  }

  std::printf(failures == 0 ? "\nALL PASS\n" : "\n%d FAILURES\n", failures);
  return failures == 0 ? 0 : 1;
}
