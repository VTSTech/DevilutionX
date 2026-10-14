// Standalone logic test for the F6 viewport-aspect fix (no SDL needed).
// DeriveViewportSize replaces the raw-swapchain base for ScaleType::Viewport
// and FinalViewportSize with the aspect-corrected game rect (letterbox fit,
// half-up rounding) - the rect RetroArch resolves `viewport` scale against
// and the one the presentation blit letterboxes the chain output onto.
// Copy of the anonymous-namespace helper in preset_runner.cpp.
// Build: g++ -O2 -Wall -Wextra -std=c++17 -o test_viewport_derive
// tools/audit/test_viewport_derive.cpp
#include <cmath>
#include <cstdint>
#include <cstdio>

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
static void DeriveViewportSize(Uint32 swapchainWidth, Uint32 swapchainHeight,
                               int gameWidth, int gameHeight,
                               Uint32 &outViewportWidth,
                               Uint32 &outViewportHeight) {
  outViewportWidth = swapchainWidth;
  outViewportHeight = swapchainHeight;
  if (swapchainWidth == 0 || swapchainHeight == 0 || gameWidth <= 0 ||
      gameHeight <= 0)
    return;
  const float gameAspect =
      static_cast<float>(gameWidth) / static_cast<float>(gameHeight);
  if (gameAspect >
      static_cast<float>(swapchainWidth) / static_cast<float>(swapchainHeight))
    outViewportHeight = static_cast<Uint32>(std::max(
        1.0F,
        std::floor(static_cast<float>(swapchainWidth) / gameAspect + 0.5F)));
  else
    outViewportWidth = static_cast<Uint32>(std::max(
        1.0F,
        std::floor(static_cast<float>(swapchainHeight) * gameAspect + 0.5F)));
}

static void Expect(Uint32 swapW, Uint32 swapH, int gameW, int gameH,
                   Uint32 wantW, Uint32 wantH, const char *label) {
  Uint32 outW = 0;
  Uint32 outH = 0;
  DeriveViewportSize(swapW, swapH, gameW, gameH, outW, outH);
  Check(outW == wantW && outH == wantH, label);
  if (outW != wantW || outH != wantH)
    std::printf("     got %ux%u, want %ux%u\n", static_cast<unsigned>(outW),
                static_cast<unsigned>(outH), static_cast<unsigned>(wantW),
                static_cast<unsigned>(wantH));
}

int main() {
  // Classic 4:3 game frame inside a 16:9 window: pillarbox, full height.
  Expect(1280, 720, 640, 480, 960, 720, "4:3 game in 16:9 swap -> 960x720");
  Expect(1280, 720, 320, 240, 960, 720, "320x240 (4:3) in 16:9 -> 960x720");

  // Same aspect as the swapchain: viewport is the whole swapchain.
  Expect(1280, 720, 1280, 720, 1280, 720, "16:9 game in 16:9 swap -> identity");
  Expect(1280, 960, 800, 600, 1280, 960, "4:3 game in 4:3 swap -> identity");
  Expect(1024, 768, 640, 480, 1024, 768,
         "4:3 game in 4:3 swap (other size) -> identity");

  // Wider than the swapchain: letterbox top/bottom.
  Expect(1280, 720, 2560, 1080, 1280, 540,
         "21:9 game in 16:9 swap -> 1280x540");

  // Portrait game: pillarbox.
  Expect(1280, 720, 480, 640, 540, 720, "3:4 game in 16:9 swap -> 540x720");

  // Half-up rounding: 721 * 4/3 = 961.33 -> 961.
  Expect(1281, 721, 640, 480, 961, 721, "rounding half-up -> 961x721");

  // Square game in 16:9: full height, square width.
  Expect(1920, 1080, 100, 100, 1080, 1080, "square game -> 1080x1080");

  // Degenerate inputs keep the previous behavior (raw swapchain).
  Expect(1280, 720, 0, 0, 1280, 720, "zero game size -> swapchain passthrough");
  Expect(1280, 720, -1, 480, 1280, 720, "negative game width -> passthrough");
  Expect(0, 0, 640, 480, 0, 0, "zero swap -> zeros");

  // Viewport never exceeds the swapchain (blit clamp contract).
  {
    const int cases[][4] = {
        {1280, 720, 640, 480},   {1920, 1080, 320, 240},  {1366, 768, 800, 600},
        {2560, 1440, 1024, 768}, {1280, 720, 2560, 1080}, {800, 600, 640, 480},
    };
    bool within = true;
    for (const auto &c : cases) {
      Uint32 outW = 0;
      Uint32 outH = 0;
      DeriveViewportSize(static_cast<Uint32>(c[0]), static_cast<Uint32>(c[1]),
                         c[2], c[3], outW, outH);
      if (outW > static_cast<Uint32>(c[0]) || outH > static_cast<Uint32>(c[1]))
        within = false;
    }
    Check(within, "viewport always fits inside the swapchain");
  }

  if (failures == 0)
    std::printf("PASS: all checks passed\n");
  return failures == 0 ? 0 : 1;
}
