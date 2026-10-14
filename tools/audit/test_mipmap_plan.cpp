// Standalone logic test for the F1 mipmap fix (no SDL needed).
// F1 makes `mipmap_input` effective for pass inputs: a pass output is
// allocated with a full mip chain exactly when the NEXT pass reads it as
// Source with the flag (only `Source` binds the reading pass's mipmap
// flag), pass 0 reads the chain input through a runner-owned mip-capable
// copy, and SDL_GenerateMipmapsForGPUTexture refills the chain before the
// consuming pass every frame - never on the single-level game texture.
// Copies of the anonymous-namespace helper and of the EnsureTargets / Run
// decision logic in preset_runner.cpp.
// Build: g++ -O2 -Wall -Wextra -std=c++17 -o test_mipmap_plan
// tools/audit/test_mipmap_plan.cpp
#include <algorithm>
#include <cstdint>
#include <cstdio>
#include <string>
#include <vector>

using Uint32 = std::uint32_t;

static int failures = 0;

static void Check(bool ok, const char *label) {
  if (!ok) {
    ++failures;
    std::printf("FAIL %s\n", label);
  } else {
    std::printf("ok   %s\n", label);
  }
}

// Copy of the anonymous-namespace helper in preset_runner.cpp.
static Uint32 MipLevelsFor(Uint32 width, Uint32 height) {
  Uint32 levels = 1;
  while (width > 1 || height > 1) {
    width = std::max<Uint32>(1, width / 2);
    height = std::max<Uint32>(1, height / 2);
    ++levels;
  }
  return levels;
}

// Copy of the EnsureTargets allocation decision: pass i's output carries a
// mip chain exactly when pass i+1 exists and reads Source mipmapped.
static std::vector<bool> PlanMipChains(const std::vector<bool> &mipmapInput) {
  std::vector<bool> chains(mipmapInput.size(), false);
  for (size_t i = 0; i + 1 < mipmapInput.size(); ++i)
    chains[i] = mipmapInput[i + 1];
  return chains;
}

// Copy of the EnsureTargets chain-input-copy decision: pass 0's Source is
// the single-level game texture, so a mip-capable copy is allocated only
// when pass 0 declares mipmap_input.
static bool PlanChainInputCopy(const std::vector<bool> &mipmapInput) {
  return !mipmapInput.empty() && mipmapInput[0];
}

// Copy of the Run() per-frame decision, traced as op strings.
// Texture ids: 0 = game texture, 100 = chain-input copy, 1..N = pass
// outputs (pass i's output has id i+1). Ops: B = blit game into the copy,
// G:id = generate mips on the bound Source (id), S:id = Source rebound to
// the copy. The trace proves the two contracts: generation never targets
// the game texture, and every generation target is a texture that was
// allocated with a mip chain.
struct FrameTrace {
  std::vector<std::string> ops;
  std::vector<Uint32> generated;
};

static FrameTrace TraceFrame(const std::vector<bool> &mipmapInput) {
  const bool copy = PlanChainInputCopy(mipmapInput);
  FrameTrace trace;
  Uint32 sourceTex = 0; // game texture
  for (size_t i = 0; i < mipmapInput.size(); ++i) {
    if (mipmapInput[i]) {
      if (i == 0 && copy) {
        trace.ops.push_back("B");
        trace.ops.push_back("G:100");
        trace.generated.push_back(100);
        sourceTex = 100;
        trace.ops.push_back("S:100");
      } else if (i > 0) {
        const Uint32 id = static_cast<Uint32>(i); // pass i-1 output = id i
        trace.ops.push_back("G:" + std::to_string(id));
        trace.generated.push_back(id);
      }
    }
    // Source advances to pass i's output after its draw (id i + 1).
    if (i + 1 < mipmapInput.size())
      sourceTex = static_cast<Uint32>(i + 1);
  }
  (void)sourceTex;
  return trace;
}

int main() {
  // MipLevelsFor: full chain length per the halving loop.
  Check(MipLevelsFor(1, 1) == 1, "1x1 -> 1 level");
  Check(MipLevelsFor(2, 2) == 2, "2x2 -> 2 levels");
  Check(MipLevelsFor(3, 3) == 2, "3x3 -> 2 levels");
  Check(MipLevelsFor(4, 4) == 3, "4x4 -> 3 levels");
  Check(MipLevelsFor(8, 8) == 4, "8x8 -> 4 levels");
  Check(MipLevelsFor(640, 480) == 10, "640x480 -> 10 levels");
  Check(MipLevelsFor(1920, 1080) == 11, "1920x1080 -> 11 levels");
  Check(MipLevelsFor(4096, 4096) == 13, "4096x4096 -> 13 levels");
  Check(MipLevelsFor(1, 1920) == 11, "1x1920 -> 11 levels");
  Check(MipLevelsFor(1920, 1) == 11, "1920x1 -> 11 levels");
  Check(MipLevelsFor(7, 3) == 3, "7x3 -> 3 levels");

  // Chain plan: only pass outputs consumed by a mip-reading NEXT pass.
  {
    const std::vector<bool> none = {false, false, false};
    Check(PlanMipChains(none) == std::vector<bool>{false, false, false},
          "no flags -> no chains");
    Check(!PlanChainInputCopy(none), "no flags -> no chain-input copy");
  }
  {
    const std::vector<bool> first = {true};
    Check(PlanMipChains(first) == std::vector<bool>{false},
          "single pass -> no chains");
    Check(PlanChainInputCopy(first),
          "single pass with flag -> chain-input copy");
  }
  {
    const std::vector<bool> mid = {false, true};
    Check(PlanMipChains(mid) == std::vector<bool>{true, false},
          "pass 1 flag -> pass 0 output chained (last output never)");
    Check(!PlanChainInputCopy(mid), "pass 1 flag only -> no chain-input copy");
  }
  {
    const std::vector<bool> last = {false, false, true};
    Check(PlanMipChains(last) == std::vector<bool>{false, true, false},
          "last-pass flag chains the previous output");
  }
  {
    const std::vector<bool> bezel = {true, false, true, false, false};
    Check(PlanMipChains(bezel) ==
              std::vector<bool>{false, true, false, false, false},
          "mixed 5-pass plan -> only pass 1 output chained");
    Check(PlanChainInputCopy(bezel), "mixed 5-pass plan -> chain-input copy");
  }

  // Frame trace: generation never touches the game texture (0) and always
  // targets a texture allocated with a mip chain.
  {
    const std::vector<bool> flags = {true, false, true};
    const std::vector<bool> chains = PlanMipChains(flags);
    FrameTrace trace = TraceFrame(flags);
    Check(trace.ops.size() == 4, "[T,F,T] -> 4 ops");
    Check(trace.ops[0] == "B" && trace.ops[1] == "G:100",
          "[T,F,T] pass 0 blits the copy then generates on it");
    Check(trace.ops[2] == "S:100", "[T,F,T] pass 0 binds the copy");
    Check(trace.ops[3] == "G:2",
          "[T,F,T] pass 2 generates on pass 1's output (id 2)");
    bool onGameTex = false;
    bool onPlainOutput = false;
    for (Uint32 id : trace.generated) {
      if (id == 0)
        onGameTex = true;
      if (id < 100 && !(id >= 1 && id - 1 < chains.size() && chains[id - 1]))
        onPlainOutput = true;
    }
    Check(!onGameTex, "generation never targets the game texture");
    Check(!onPlainOutput, "generation always targets a mip-chained output");
  }
  {
    const std::vector<bool> flags = {true, true};
    FrameTrace trace = TraceFrame(flags);
    Check(trace.ops.size() == 4, "[T,T] -> 4 ops");
    Check(trace.ops[3] == "G:1", "[T,T] pass 1 generates on pass 0's output");
    bool onGameTex = false;
    for (Uint32 id : trace.generated)
      if (id == 0)
        onGameTex = true;
    Check(!onGameTex, "[T,T] game texture never generated");
  }
  {
    const std::vector<bool> flags = {false, false};
    FrameTrace trace = TraceFrame(flags);
    Check(trace.ops.empty(), "no flags -> no per-frame mip ops");
  }
  {
    // A pass-0-only preset never generates on the raw game texture even
    // though its Source is the game texture: the copy path is mandatory.
    const std::vector<bool> flags = {true};
    FrameTrace trace = TraceFrame(flags);
    Check(trace.ops.size() == 3 && trace.ops[0] == "B",
          "pass-0-only blits into the copy before generating");
    bool onGameTex = false;
    for (Uint32 id : trace.generated)
      if (id == 0)
        onGameTex = true;
    Check(!onGameTex, "pass-0-only never generates on the game texture");
  }

  if (failures == 0)
    std::printf("PASS: all checks passed\n");
  return failures == 0 ? 0 : 1;
}
