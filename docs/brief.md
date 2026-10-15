# Codebase Intelligence Brief: DevilutionX (VTSTech GPU Shader Mod)

> Generated: 2026-10-16 (codebase-audit round 7) | Auditor: Super Z (GLM) | Commit: `8341ef99f9f9c3786222abb57e2eb8c16e79656a` (squashed fork history; `VERSION` = `1.6.0-dev`; code byte-identical to audit tip `b60356e1a` — the only newer commit is docs-only) — refreshed after audit rounds 1–7; 19 closures live in `docs/deltas.md` (MAINT-03 reopened round 7), open ledger (53 findings: **2 High / 17 Medium / 34 Low**) in `docs/audit.md`

---

## Project Identity

| Field | Value |
|-------|-------|
| **Purpose** | Standalone mod of DevilutionX (open-source Diablo + Hellfire engine) that adds **Vulkan GPU rendering** via SDL3-GPU and **runtime-compiled libretro shader presets** (`.slangp`/`.slang` → glslang → SPIR-V) on top of the fully intact stock SDL render path |
| **Tech Stack** | C++23 (GNU extensions), CMake ≥ 3.22 + Ninja, SDL3 GPU API (SDL2/SDL1.2 variants also build), ~29 vendored FetchContent deps (glslang 16.6.0, SPIRV-Cross, libsodium, SDL3_image, libzt, sol2/Lua…), gettext i18n, gtest + google/benchmark |
| **Entry Point** | `Source/main.cpp:36` (C `main`, platform ifdefs) → `devilution::DiabloMain()` at `Source/diablo.cpp:2758` |
| **Build/Run** | `cmake -S . -B build -G Ninja -DCMAKE_BUILD_TYPE=Release -DUSE_SDL3=ON -DDEVILUTIONX_SYSTEM_SDL3=OFF -DDEVILUTIONX_SYSTEM_LIBSODIUM=OFF -DBUILD_TESTING=OFF && cmake --build build -j$(nproc)` — GPU scaler requires `USE_SDL3=ON`; `USE_GPU_SCALER` auto-defaults ON with SDL3 |
| **Test Command** | `ctest --test-dir build` (BUILD_TESTING defaults ON; ~60 gtest targets; the headless timedemo regression needs game MPQ assets and silently skips without them) |

**Fork context (important):** branched from upstream `diasurgical/DevilutionX 1.6.0-dev`. Upstream enforces a no-AI policy, so the GPU shader work (originally upstream PR #8737) lives here as an independent mod. The fork's entire delta is concentrated in `Source/engine/render/gpu/` (~15K lines incl. generated headers) plus integration points in `dx.cpp`, `display.cpp`, `options.cpp`, `scrollrt.cpp`, and CI (`gpu-shaders.yml`). It is **not** a general fork of gameplay code — all game logic is stock DevilutionX. Maintainer docs: `docs/diablo-handoff.md` (session log), `docs/GPU_SHADERS.md` (user features), `docs/SLANG_SPEC.md` (**normative** reference for the preset pipeline, incl. a 20-item gap list).

---

## Architecture Map

```
Source/                 → Game engine. Root TUs = Diablo-era gameplay (diablo.cpp, player.cpp, monster.cpp…)
Source/engine/          → Engine primitives: palette, dx.cpp (presentation), events, demomode, MPQ, CLX assets
Source/engine/render/   → Software renderer (scrollrt.cpp frame compositor, dun_render.cpp isometric blits)
Source/engine/render/gpu/ → THE FORK'S MOD: SDL_GPU scaler + libretro preset pipeline (see Critical Files)
Source/DiabloUI/        → Out-of-game menu framework (own event loop); settingsmenu.cpp = generic settings browser
Source/controls/        → Input abstraction (keyboard/pad/touch), rebindable keymapper/padmapper
Source/dvlnet/          → Networking: loopback / TCP / ZeroTier; libsodium secretbox packet encryption (packet.h)
Source/platform/        → Per-OS shims: android, 3DS, iOS, Switch, Vita, UWP…
Source/utils/           → ~100 utility TUs: display.cpp (window/renderer lifecycle), ini, paths, audio, utf8
Source/lua/             → Sol2 Lua runtime (dev console, mod hooks); Source/mods/ = hf.mpq Hellfire mod tree
Source/qol/             → Vanilla-plus features (stash, autopickup, xpbar…)
test/                   → ~60 gtest targets + fixtures (.dun dungeons, timedemo .dmo), benchmarks
3rdParty/               → Thin CMake FetchContent wrappers ONLY (pinned tarball/SHA or GIT_TAG) — no submodules
CMake/                  → Modules: Dependencies.cmake, Tests.cmake, Platforms.cmake (~40 platform files)
Packaging/              → CPack per-OS + fork's shader helper scripts (windows/, linux/)
.github/workflows/      → gpu-shaders.yml (the fork's real CI) + clang-format-check.yml (curated set, MAINT-03 ✅)
docs/                   → handoff/GPU_SHADERS/SLANG_SPEC/audit CSVs; android-project/, uwp-project/ = stale upstream scaffolding
```

### Skip List

- `3rdParty/*/` contents — vendor code behind thin wrappers; read only the wrapper `CMakeLists.txt`
- `Translations/`, `assets/`, `mods/` asset trees; `Packaging/*` platform scripts except `windows/`, `linux/`
- `android-project/`, `uwp-project/` — cannot build the mod (no SDL3), stale upstream scaffolding
- `Source/engine/render/gpu/gpu_shader_spv_prebuilt.hpp`, `gpu_shaders_msl.hpp` — generated/committed binary blobs, trimmed (MAINT-02 ✅) to carry only the live `kVertex`/`kBlit` pair; `gpu_luts.hpp` was deleted outright (it was an unreferenced orphan)
- `docs/audit/*.csv` — per-preset field-sweep results, useful only when debugging a specific shader pack
- Root gameplay TUs you don't touch (`missiles.cpp`, `stores.cpp`, `levels/*`) — stock upstream, stable

---

## Critical Files Index

| File | Purpose | Why It Matters |
|------|---------|----------------|
| `Source/engine/render/gpu/gpu_scaler.cpp` (1197 ln) | SDL_GPU device/swapchain lifecycle, per-frame present, OSD, gpu-scaler.log | The heart of the mod. Owns `Reinit()`/`Shutdown()`/`PresentFrame()`; every failure funnel ends here via sticky `gFailed` → session fallback to SDL_Renderer |
| `Source/engine/render/gpu/preset/preset_runner.cpp` (1385 ln) | Multi-pass preset executor: pipelines, history ring, feedback double-buffering, LUTs | Where compiled presets actually run each frame; `Runner::Load()` is the preset→GPU boundary; Vulkan-only gate at line 228 |
| `Source/engine/render/gpu/preset/preset_parser.cpp` | `.slangp` parser: passes, scale rules, aliases, `#reference` merge, `ScanPresets`, path resolution | All untrusted-input entry points live here (`ResolvePresetPath`, `ResolvePresetFilePath`); references are sandboxed to the shader root via `IsPathInsideShaderRoot` (SEC-01 closed ✅) |
| `Source/engine/render/gpu/preset/slang_translate.cpp` (1207 ln) | `.slang` → GLSL translation: includes, stage split, UBO/push-block rewrites, dense bindings | The most intricate logic; cache keys hash its **output**, so changes here invalidate the entire shader cache |
| `Source/engine/render/gpu/preset/slang_compile.cpp` | glslang runtime (FetchContent singleton) → SPIR-V, sidecar `.spv` fast path, SHA-256 disk cache | Cache key = SHA-256 of translated GLSL; sidecar `.spv` always wins; the key is version-salted with the glslang/toolchain version and cache writes are atomic with self-healing recompile on GPU rejection (ROB-03/ROB-04 closed ✅) |
| `Source/engine/dx.cpp` | 3-way present: GPU scaler → SDL_Renderer → window surface; backbuffer + dirty-rect blits | `RenderPresent()` (`:294`) is where the scaler takes over (`:312-325`) and where fallback happens (`:315-321`) |
| `Source/utils/display.cpp` | Window/renderer creation & recreation, `ReinitializeRenderer()` | Mediates window ownership hand-off between SDL_Renderer and SDL_GPU (DXGI one-swapchain-per-HWND constraint). Round 7 High ROB-14 lives here: the `scalerWanted` branch destroys the SDL_Renderer without nulling renderer-owned textures — fix before touching anything nearby |
| `Source/options.cpp` | Self-registering options system; `OptionEntryShaderFilter` (`:420-626`) | Shader Filter ini persistence, folder-browser navigation via negative values, stale-value self-heal, documented magic-static deadlock trap (`:437-443`) |
| `Source/diablo.cpp` (3553 ln) | `DiabloMain` init chain, `RunGameLoop` (`:867`), `GameLogic`, fixed 50 ms ticks | Order of `DiabloMain` steps is load-bearing (options before archives, etc.); demo-mode intercepts the clock here |
| `Source/engine/render/scrollrt.cpp` (1933 ln) | `DrawAndBlit()` (`:1835`) frame compositor; draws OSD via `gpu_scaler::DrawFilterOsd` (`:1910`) | Everything renders into the 8-bit `PalSurface`; palette→RGBA conversion happens CPU-side in `dx.cpp:159-186` **before** the GPU ever sees the frame |
| `CMakeLists.txt` (root) | Options incl. `USE_GPU_SCALER` (`:133`), SPV generation w/ prebuilt fallback (`:134-170`) | The `USE_GPU_SCALER` define must be appended in BOTH glslang/no-glslang paths (`:166-170` warns) or the feature silently vanishes |
| `.github/workflows/gpu-shaders.yml` (256 ln) | The fork's only real CI: Win x64 (MinGW static) + Linux x64 (gcc:14 container) | Portability gates (GLIBC ≤ 2.36, no-DLL), version-embeds-commit check, hand-rolled release zips; no caching, no ctest |
| `docs/SLANG_SPEC.md` | Normative spec: supported slang subset + gap list | Any preset-pipeline change must be checked against §8 gaps; audit trail lives in `docs/audit/` |

**Rule of thumb**: anything under `Source/engine/render/gpu/` is fork code; everything else is upstream except the specific integration points listed above.

---

## Execution Lifecycle (per frame)

```
1.  main.cpp:36 main() → diablo.cpp:2758 DiabloMain():
    flags → LoadCoreArchives → LoadOptions → ApplicationInit (window+dx_init)
    → LuaInitialize → LoadGameArchives → DiabloInit → mainmenu_loop
2.  diablo.cpp:867 RunGameLoop(): while (gbRunGame)
    a. Event pump: FetchMessage/HandleMessage (engine/events.cpp:204)
    b. Fixed-timestep gate: nthread_has_500ms_passed() (nthread.cpp:214) — name lies, threshold = gnTickDelay (50ms)
    c. If tick due: game_loop() → GameLogic() (players→monsters→objects→missiles→items, max 3 catch-up ticks)
    d. If drawGame: RedrawViewport() + DrawAndBlit()
3.  scrollrt.cpp:1835 DrawAndBlit(): renders game+UI into 8-bit PalSurface
    → gpu_scaler::DrawFilterOsd() (scrollrt.cpp:1910) drawn INTO the 8-bit frame
4.  dx.cpp Blit(): SDL_BlitSurface PalSurface (INDEX8) → 32-bit RendererTextureSurface
    — palette expansion is CPU-side, per dirty rect; GPU never sees the palette
5.  dx.cpp:294 RenderPresent():
    • scaler active → gpu_scaler::PresentFrame() (gpu_scaler.cpp:856):
      acquire cmd buffer + non-blocking swapchain acquire → per-frame transfer buffer upload
      → Runner::Run() (preset_runner.cpp:1094): history-ring seed → per-pass render
        (std140 uniforms, name-resolved texture bindings, feedback double-buffer flip)
      → letterboxed blit into swapchain → Submit
    • scaler failed/off → SDL_Renderer path → window-surface path
6.  Failure contract: any hard GPU error → LogFailHard → gFailed sticky → dx.cpp:315
    tears down scaler, reinits SDL_Renderer, session continues (game never dies by design)
```

Preset load path (first selection or renderer recreation): `EnsurePresetRunner` (gpu_scaler.cpp:520) → `ResolvePresetPath` → `ParsePreset` (incl. `#reference` recursion ≤8) → per-pass `TranslateSlang` (include-once, stage split, UBO rewrites) → `CompilePass` (sidecar `.spv` > SHA-256 cache > glslang) → `SDL_CreateGPUShader` + pipelines + LUTs. Any failure → filter off for the session, stock path continues.

---

## Dependency Graph

```
options.cpp (Shader Filter + hidden Preset path)
   → display.cpp ResizeWindow/ReinitializeRenderer (change callback, static-init registered)
      → gpu_scaler::Reinit → CreateDevice → EnsurePresetRunner
         → preset_parser → slang_translate → slang_compile (glslang) → preset_runner
dx.cpp RenderPresent → gpu_scaler::PresentFrame → preset_runner::Run (every frame)
scrollrt.cpp → gpu_scaler::DrawFilterOsd (8-bit OSD)
interfac.cpp → gpu_scaler::LogPhase (load-phase probes)
gpu_scaler → gpu-scaler.log (SDL_GetBasePath(), rotating 200KB) + SDL log capture hook
```

- **glslang** is pulled two different ways: FetchContent `3rdParty/glslang` 16.6.0 for *runtime* preset compilation, and build-time `glslangValidator` binary for the *baked* `fullscreen.vert`/`blit.frag` header — don't confuse them.
- Coupling hotspots: `display.cpp` ↔ `gpu_scaler` (window claim/reclaim), `options.cpp` ↔ `display.cpp` (change callbacks at static-init), `dx.cpp` ↔ everything (present branch).
- `test/` links `libdevilutionx_so`; **zero test files reference gpu/shader** — the mod is untested.

---

## Patterns & Conventions

| Aspect | Pattern |
|--------|---------|
| Error handling | Fatal: `app_fatal()` (dialog + quit). Recoverable: `std::expected` (utils/status_macros.hpp). **GPU mod deviates on purpose**: log to gpu-scaler.log + session fallback, never abort |
| Exceptions | Compiled out under `DEVILUTIONX_DISABLE_EXCEPTIONS`; dvlnet uses exceptions internally; the ROB-01/ROB-02 uncaught-`stoi`/bad-allocation hazards are fixed and regression-locked (`preset_failure_modes` harness + `preset_smoke` malformed corpus ✅) |
| Options | Self-registering `OptionEntryBase` subclasses; flags `CantChangeInGame`/`RecreateUI`; change callbacks via static-initializer idiom `(GetOptions().X.SetValueChangedCallback(fn), true)` |
| Strings | `std::string_view` params, `StrCat`/`BufCopy`, fmt-style `Log("{}", …)`, gettext `N_()`/`_()`, UTF-8 everywhere |
| Naming | Namespaces `devilution`, `devilution::gpu_scaler`, `devilution::preset`; Diablo-legacy globals (`gb*`, `sg*`) coexist with snake_case; `DVL_API_FOR_TEST` marks test-exposed globals |
| Formatting | Tabs (width 4), WebKit-based `.clang-format` in Source/, clang-tidy configured |
| Ifdefs | Platform (`__SWITCH__`, `__vita__`…), renderer (`USE_SDL1`/`USE_SDL3`), feature (`USE_GPU_SCALER`, `NONET`, `PACKET_ENCRYPTION`) |
| Logging | Global: `utils/log`. Mod-local: rotating `gpu-scaler.log` next to exe with size caps + SDL log chaining |

---

## Known Landmines

- **Magic-static deadlock in options** — `OptionEntryShaderFilter` must NOT scan the shaders folder from its constructor; `GetOptions()` is a function-local static and re-entering it mid-construction deadlocks silently at startup (documented `options.cpp:437-443`). Rescans are deferred to `LoadFromIni`.
- **`USE_GPU_SCALER` define in two CMake paths** — if you add a source file or flag, it must be added in both the glslangValidator and prebuilt-header branches (`CMakeLists.txt:166-170`), or the feature silently disappears while the build stays green.
- **Demo mode replays the entire loop deterministically** — it intercepts the clock (`diablo.cpp:925`) and event pump (`demomode.cpp:725`); breaking determinism (e.g. adding wall-clock reads to the render path) breaks recorded demos and `timedemo_test`.
- **Window ownership hand-off** — ✅ FIXED (ARCH-01, `d5ca0c055`): swapchain history is explicit state keyed by SDL window ID (`gLastClaimedWindowId`, never reused by SDL) instead of a dangling-pointer compare — the ABA false-positive is gone; the consolidated protocol comment in gpu_scaler.cpp names all three participants (gpu_scaler / display.cpp / dx.cpp).
- **Orientation convention** — every intermediate pass gets a Y-mirror MVP; the last pass keeps legacy orientation so the present blit cancels the flip. Inserting/removing passes must preserve this pairing (preset_runner.cpp:924-947, well-commented).
- **Integer Scaling crash (known, deliberately unfixed)** — Integer Scaling + non-multiple resolution → divide-by-zero in `CalculatePreferredWindowSize` (display.cpp) + zero-size-fit path in `PresentFrame`. Ruled out-of-scope by VTSTech; do not "fix" casually (handoff doc §8).
- **Palette fade runs its own present loop** — `PaletteFadeIn/Out` (palette.cpp:288-319) does SDL_Delay+blit+present reentrancy, and it flows through the GPU scaler too.
- **HeadlessMode is a bare global** — every new render-path branch must check it or unit tests crash/open windows (`test/main.cpp:84` sets it globally).
- **MP asset-integrity hard abort** — release builds `app_fatal` mid-game when MP is used with modified lua/tsv/sol assets (`diablo.cpp:905-907`).
- **Cache poisoning** — ✅ FIXED (ROB-03, `d5ca0c055`): SPIR-V cache blobs are written to a process-unique temp file and atomically renamed, and a GPU-rejected cache hit self-heals by deleting the blobs and recompiling once with the cache disabled.
- **Round-7 High: renderer-destroy texture UAF (ROB-14)** — the `scalerWanted` branch in `ReinitializeRenderer` (display.cpp:836) destroys the SDL_Renderer while the global `texture` (dx.cpp:49) and virtual-gamepad textures still reference it; SDL3's `SDL_DestroyRenderer` destroys child textures, and the next `ReinitializeTexture()` destroys them again. `dx_cleanup`'s ordering (`texture = nullptr` *before* `SDL_DestroyRenderer`, dx.cpp:147-154) is the invariant to mirror. Fires on the ordinary "select a preset with upscale on" flow.
- **Round-7 High: post-flip output read (ROB-15)** — `Runner::Run` returns `last->outputTex[last->curBuffer]` *after* the double-buffer flip (preset_runner.cpp:1441-1450), so a double-buffered final pass presents the previous frame's image (undefined on first frame/after resize). Capture the pre-flip target before the flip loop.
- **Unchecked SDL_GPU transfer calls (ROB-16/17)** — `SDL_MapGPUTransferBuffer`, `SDL_BeginGPUCopyPass`, and `SDL_UploadToGPUTexture` results are unchecked in `PresentFrame` (gpu_scaler.cpp:1056-1078): map failure memcpys to NULL; ignored upload errors present a stale frame with a healthy heartbeat. Any new present-path code must route failures through `LogFailHard`/`SubmitSkipAfterAcquire` like its neighbors.
- **`FillBlockBytes` Size-branch order (ROB-23)** — the `<TextureName>Size` branch is checked before the parameter lookup and writes are type-unaware, so `*Size`-suffixed parameters get zeros and int-typed members get float bits. Reorder before adding new uniform semantics.
- **Broken symlinks in repo** — `CMake/platforms/*-pkg-config` and `Packaging/windows/mingw-prep64.sh` are dangling symlinks; they break rg/file-tooling scans (harmless to the core build).

---

## Active Decisions (do not re-litigate — confirmed in docs/diablo-handoff.md §4)

| Decision | Choice | Rationale |
|----------|--------|-----------|
| Preset format | libretro slang (`.slangp`/`.slang`) | Full RetroArch shader ecosystem; spec semantics per `docs/SLANG_SPEC.md` |
| Shader dir | `shaders/` **next to the executable**, recursive | Portable-zip deployment model; nothing baked into the binary |
| Compile | In-process glslang → SPIR-V; `shaders/cache/` content-addressed; sidecar `.spv` always wins | Near-instant reload; cache key = SHA-256 of translated GLSL |
| Built-in filters | REMOVED (user request) | Only `Off` + custom presets; dead payload deleted outright (MAINT-02 ✅); stale ini values sanitize to Off |
| D3D12 | Excised entirely (2026-10-14) | SPIR-V→HLSL transpile path deleted; Vulkan is the only Windows/Linux backend |
| Metal | Accepted at device level, but presets reject non-Vulkan | MSL scaffolding exists; preset path deterministically fails on macOS today |
| Failure contract | Any preset/GPU failure → session fallback to stock renderer, game never dies | Core product promise; scatters through gpu_scaler.cpp |
| Hot reload | None in v1; switching presets live rebuilds the chain | Restart not required for preset switch, only for pack changes |
| Pass orientation | Y-mirror MVP on intermediates, legacy flip on last pass | Fixes even/odd chain mirroring (`7ea8e6e93`, field-verified) |

---

## What's Missing / Incomplete

- ~~Zero tests for the mod~~ ✅ TEST-01/TEST-03 closed: `preset_smoke` builds with the suite and registers `preset_smoke_corpus`/`preset_smoke_malformed` ctest tests (good + malformed corpora covering every pipeline stage), plus a standalone options-layer gtest (`test/options_test.cpp`) — the mod's pipeline and options layer are regression-locked.
- ~~No CI run of the test suite~~ ✅ TEST-02 closed: the Linux CI job configures `-DBUILD_TESTING=ON` and runs ctest (608/608 at closure; three real suite fixes were needed — gmock link gap, fork link-closure gap, fixture skip-path crash). Windows stays `BUILD_TESTING=OFF` (cross-compiled binaries cannot execute on the runner). codecov remains dormant (target 0%).
- ~~No CI caching~~ ✅ MAINT-01 closed: `push.paths-ignore` for doc-only commits + ccache (`actions/cache@v4`) wired into both jobs.
- ~~No release attachment~~ ✅ MAINT-04 closed: both gpu-shaders.yml jobs attach zips to GitHub Releases on `release: published`.
- ~~4 dead upstream workflows~~ ✅ MAINT-03 closed: curated — `clang-format-check.yml` fixed (`@v4`) and kept as the format gate; `clang-tidy-check`/`translations`/`cache-cleanup` deleted; the one-shot cleanup scripts removed.
- ~~Release zips omit `mods/hf.mpq`~~ ✅ MAINT-05 closed: both Package steps copy `build/mods/hf.mpq` when present (existence-guarded) — shipped builds run Hellfire.
- ~~vcpkg + SDL3 broken~~ ✅ MAINT-06 closed: `sdl3` feature (sdl3 + sdl3-image) defined in `vcpkg.json`.
- Shader cache: pruning still absent; PNG-only LUT decode vs 6 accepted extensions in `LooksLikeImagePath` (ROB-12, open). Version salt, atomic writes, and self-heal are done (ROB-03/ROB-04 ✅).
- Per `SLANG_SPEC.md` §8: ~20 spec gaps tracked (clamp_to_border, history cap 8, `#pragma alias`, `feedback_pass` parsed-unused, …).
- **Open audit backlog (post round 7)**: **2 High** (ROB-14 renderer-destroy UAF, ROB-15 post-flip output read) **+ 17 Medium** (incl. MAINT-03 **reopened** round 7 — the three "deleted" CI workflows still ship `checkout@v7`; plus ROB-16..23, PERF-06, TEST-06, MAINT-15, ROB-07, ROB-08, FEAT-01..03) **+ 34 Low** — full ledger in `docs/audit.md`, closures in `docs/deltas.md`.

---

## Quick Start for Developer

1. **Read the Critical Files Index** — start with `gpu_scaler.cpp`, then `preset_runner.cpp`; everything else in the mod hangs off those two.
2. **Understand the two pipelines**: baked shaders (`shaders/vulkan/*.frag` → build-time SPV header, used only for `blit`) vs runtime presets (`.slangp` → parse → translate → glslang → cache → `Runner::Run`).
3. **Check Known Landmines before touching options/CMake/render present** — the deadlock, the dual-define, and the window-ownership hand-off are the three that bite.
4. **Follow the failure contract**: preset code must return descriptive error strings and degrade to the stock renderer — never `app_fatal`, never terminate. The ROB-01/ROB-02 uncaught-exception violations are fixed and regression-locked by the `preset_failure_modes` harness and the `preset_smoke` malformed corpus — keep it that way.
5. **When changing `slang_translate.cpp`**, remember it changes every cache key (users must clear `shaders/cache/`); the key is version-salted with the glslang/toolchain version (ROB-04 ✅), so toolchain bumps invalidate the cache automatically.
6. **Build once with `-DBUILD_TESTING=ON` and run `ctest`** before and after render-path changes; CI already runs ctest on Linux pushes, and the headless timedemo is the only full-state regression net.
7. Do NOT start by reading every file — use this brief as the map. `docs/diablo-handoff.md` has per-round field history if you need to know *why* something looks defensive.
