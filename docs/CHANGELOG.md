# Changelog

All notable changes to this project will be documented in this file.

The format is based on [Keep a Changelog](https://keepachangelog.com/en/1.0.0/)
and this project adheres to [Semantic Versioning](https://semver.org/spec/v2.0.0.html).

Fork releases are numbered `1.6.0-dev-<commit>`, where `<commit>` is the
short hash of the commit a build or overlay was produced from, and are
dated with an America/Toronto timestamp. Upstream (diasurgical) release
history lives in [docs/CHANGELOG_UPSTREAM.md](docs/CHANGELOG_UPSTREAM.md)
and is not modified.

## 1.6.0-dev-d5ca0c0 (2026-10-15 1:46:21 PM)

Codebase audit fixes, round 4 — the entire "next sprint" batch from the audit:
the shader-cache failure contract (atomic writes + toolchain-version salt),
three more robustness closures (scale clamp, diagnostics-log race, corrupt-ini
self-heal), the window-ownership ABA fix, release-asset CI, and the first
options-layer test registration (which immediately caught a real
startup-restore bug). Field result: shader presets now load right from the
title screen instead of requiring a trip through the shader menu first. 19 of
the original 47 findings are now closed; `audit.md` carries only the open set
(28 findings: 1 High, 5 Medium, 22 Low) and `deltas.md` remains the single
record of closures.

### Fixed

#### Graphics / GPU scaler

- Shader cache blobs can no longer be poisoned by a torn write: each
  `.vert.spv`/`.frag.spv` is written to a process-unique temp sibling
  (`<path>.<pid>.tmp`) and atomically renamed into place (existing target
  removed first for Windows), so a crash or kill mid-write can no longer leave
  a truncated blob that fails every later load; `CompilePass` gained an
  out-parameter form (`SpirvSource` = Compiled/Cache/Sidecar, plus
  `allowCache`) so callers know where each pass's SPIR-V came from, and a new
  `DeleteCachedSpv` removes both blobs of a pass. A GPU-rejected cache hit now
  self-heals: `Runner::Load` releases any shader built from the poisoned blob,
  deletes the cached pair, and recompiles once with the cache disabled instead
  of failing the preset until the user manually wipes `shaders/cache/` (ROB-03)
- The disk-cache key is salted with the toolchain version: the hashed payload
  now mixes in `kSpirvCacheVersion` plus the glslang version macros
  (`GLSLANG_VERSION_MAJOR/MINOR/PATCH`, guarded for exotic builds), and cache
  file names carry a `-v<version>-` segment so a future bump can also prune by
  name; upgrading glslang no longer silently serves stale SPIR-V compiled by
  the previous toolchain (ROB-04)
- Resolved pass-target dimensions are clamped to [1, 16384] before the
  float→Uint32 cast (`ClampPassTargetDim`, NaN → 1), each clamp logged with its
  pass index, and the clamped values feed the prevW/prevH chain; a preset with
  absurd or NaN `target_size` values can no longer trigger UB in the conversion
  or request a multi-gigabyte framebuffer (ROB-05)
- Swapchain window ownership is explicit state keyed by the SDL window ID
  instead of a `SDL_Window*` compare: SDL window IDs are never reused, so the
  old `gLastClaimedWindow` dangling-pointer ABA (destroy + recreate landing on
  the same address, misread as "same window") can no longer hand the swapchain
  to the wrong window; the three-participant handoff protocol (gpu_scaler /
  display.cpp / dx.cpp) is documented in one comment (ARCH-01)

#### Configuration

- A corrupt `diablo.ini` no longer terminates the game at startup: the
  unreadable file is preserved as `diablo.ini.corrupt-<YYYYMMDD-HHMMSS>`, the
  parse failure is logged, and defaults load via the empty-buffer parse path;
  the fatal error remains only if preservation itself fails, so a bad write can
  cost the user their settings but never their config file or their session
  (ROB-09)

#### Options menu

- Fixed an undocumented startup-restore bug found by the new tests:
  `GraphicsOptions::GetEntries()` listed `shaderFilter` before
  `shaderFilterPreset`, and the preset entry re-resolves its path at Rescan
  time — which is empty at startup — so every restart sanitized a saved custom
  preset back to Off, and it only re-activated after the shader menu was
  opened; the (invisible) preset-path entry now loads first and saved custom
  presets survive restarts — shader presets load right from the title screen

#### CI

- Built binaries now land on the GitHub Release instead of only 90-day
  workflow artifacts: both `gpu-shaders.yml` jobs attach their package zips via
  `softprops/action-gh-release@v2`, conditioned on the `release` event, with
  job-level `permissions: contents: write` for the default token (MAINT-04)

### Testing

- New standalone gtest `options_test` (7 cases) registered in
  `CMake/Tests.cmake` under the `standalone_tests` pattern (links
  `libdevilutionx_options` + the preset TUs + `libdevilutionx_assets`, same as
  `dun_render_benchmark`): stale builtin-filter sanitize (1..99), custom
  shader-preset index mapping, missing-preset self-heal, ini round-trip, and
  two corrupt-ini recovery cases (TEST-03). Verification evidence for the
  round: options_test 7/7 pass headless, all 5 touched TUs compile clean with
  project flags (gnu++23, -Wall -Wextra), clang-format 18.1.8 clean

### Documentation

- Ledger: 8 closure entries added to `deltas.md` (ROB-03, ROB-04, ROB-05,
  ROB-06, ROB-09, MAINT-04, ARCH-01, TEST-03) plus the bonus fix; `audit.md`
  master table and section bodies updated, next-sprint matrix row retired —
  open set now 1 High (TEST-01), 5 Medium (ROB-07, ROB-08, FEAT-01, FEAT-02,
  FEAT-03), 22 Low

## 1.6.0-dev-c37e75a (2026-10-15 6:12:02 AM)

Codebase audit fixes, round 3 — CI repair on the `gpu-shaders.yml` pipeline plus
four more audit closures: both Security findings, Hellfire packaging, and
`gpu_scaler.cpp` hygiene. 11 of the original 47 findings are now closed;
`audit.md` carries only the open set (36 findings: 1 High, 13 Medium, 22 Low)
and `deltas.md` remains the single record of closures.

### Fixed

#### Security

- Preset references can no longer escape the shaders root (arbitrary file read
  via preset packs): new `preset::IsPathInsideShaderRoot()` runs the candidate
  and the shader root through `std::filesystem::weakly_canonical` (symlinks
  resolved) and requires the result to sit at or below the root, with a
  separator-boundary check so `shaders-ng` cannot pass a `shaders` prefix test
  (case-insensitive compare for Windows/macOS). `ResolvePresetFilePath` refuses
  absolute references and ancestor-walk hits that canonicalize outside the root,
  the slang `#include` resolver fails translation with "include target outside
  the shaders root: <path>", and the runner names the refused reference in
  gpu-scaler.log so refusals are diagnosable in the field (SEC-01)
- gpu-scaler.log is no longer an injection target: new header
  `gpu_log_sanitize.hpp` (`gpu_scaler::SanitizeLogLine()`) replaces every control
  byte (< 0x20, including `\n` `\r` `\t` ESC, and 0x7F DEL) with `?` before
  `LogFileLine` writes, so hostile preset names, `#pragma` descriptions, and
  glslang info-log text cannot forge log lines or spray terminal escapes;
  printable text and `%` signs pass through untouched (SEC-04)

#### CI

- `gpu-shaders.yml` Linux job installs dependencies again: the `apt-get install`
  continuation line ended in a doubled `\` (`ccache \\`), so the shell received a
  literal `\` as a package name and apt aborted with "E: Unable to locate
  package \" — back to a single continuation backslash
- Windows builds are static again: two `#` comment lines sat inside the folded
  `run: >` scalar of the Configure step; YAML folds comment lines into the shell
  command, where `#` starts a comment and silently truncated every argument after
  `-DCMAKE_CXX_COMPILER_LAUNCHER=ccache` — the
  `-DCMAKE_EXE_LINKER_FLAGS='-static -static-libgcc -static-libstdc++'` flags
  never reached CMake, the exe linked `libgcc_s_seh-1.dll` / `libstdc++-6.dll`,
  and the DLL-import gate failed. Comments moved above the step and a warning
  note about the folded-scalar `#` trap added

#### Packaging

- CI release zips now include Hellfire: both Package steps (Linux and Windows)
  copy `build/mods/hf.mpq` into `pkg/mods/`, existence-guarded so configurations
  that did not build the expansion MPQ still package cleanly; shipped builds are
  no longer silently Diablo-only (MAINT-05)

#### Graphics / GPU scaler

- `gFragmentShaders` is sized by a named constant (`kMaxFragmentPrograms =
  static_cast<int>(Program::Blit) + 1`) instead of the stale magic number 9 left
  over from the removed built-in filter set, so resizing the `Program` enum trips
  a compile error at the table instead of silently changing its bounds; the dead
  `case None:` switch in `FilterDisplayName` and the unused `<csetjmp>` include
  are gone (MAINT-07)

### Testing

- `preset_failure_modes` grew 20 → 37 checks: relative and absolute `#include`
  escapes refused while an in-root include still translates; `../../`, absolute,
  and sibling-prefix (`shaders-ng`) preset-reference refusals with an in-root
  reference still resolving; an escaping `#reference` failing the whole preset
  with a traceable error; and 6 log-sanitizer checks. The harness sandbox is
  re-rooted at a working-directory fake `SDL_GetBasePath` so containment staging
  works headless

## 1.6.0-dev-caee40e (2026-10-15 5:08:04 AM)

Codebase audit fixes, round 2 — CI hygiene and the test-suite revival, plus
the audit ledger split (`deltas.md` now records closed findings).

### Added

#### Testing

- New headless ctest `preset_failure_modes`: the hostile-input regression harness for the preset parser / slang translator (non-numeric and 20-digit texture indices, malformed `#pragma parameter` numbers, absurd `shaders` counts and `shaderN` indices, valid-preset regression guards). Compiles only the four parser/translator TUs — no SDL, no GPU — so it runs on any CI box (TEST-01, partial)
- The full gtest suite now runs in CI: the Linux job builds with `BUILD_TESTING=ON` (vendored googletest/benchmark) and executes `ctest --output-on-failure` (TEST-02)
- ccache is wired into both CI jobs (compiler launcher + `actions/cache` restore with per-job keys, 2 GB cap), and doc-only pushes (`**.md`, `docs/**`) no longer trigger the binary build pipeline (MAINT-01)

### Fixed

#### Build system

- `vcpkg.json` now defines the `sdl3` feature (`sdl3` + `sdl3-image`) that `VcPkgManifestFeatures.cmake` has been requesting since `USE_SDL3=ON` was introduced — vcpkg-based (MSVC) configures no longer abort during manifest install (MAINT-06)
- The test suite builds again under `BUILD_TESTING=ON`: standalone tests now link `GTest::gmock` when available (their `<gmock/gmock.h>` includes previously only resolved where system gtest headers sat on the default include path), and the four standalone test/benchmark targets that carry `libdevilutionx_options` compile the preset parser/translator TUs the fork's settings browser calls (TEST-02)
- `UITest`-based fixtures no longer segfault when MPQ fixtures are absent: `GTEST_SKIP()` in the base `SetUp()` is a plain `return`, so derived `SetUp()` overrides continued into uninitialized engine state (null `MyPlayer`, unloaded quest data) and crashed all 65 cases across `inventory_ui_test` / `panel_state_test` / `spell_ui_test`; derived setups now bail via `UITest::MissingAssets()` and `TearDown()` guards its cleanup (TEST-02). Suite status: 608/608 pass headless

#### CI

- `clang-format-check.yml` pins `actions/checkout@v4` (was the nonexistent `@v7`, which failed every run at checkout); the three other broken upstream workflows (`clang-tidy-check`, `translations`, `cache-cleanup`) and the one-shot `cleanup-fork-workflows.{sh,ps1}` scripts are removed (MAINT-03). `.github/workflows/` now contains exactly the curated pair: the format check and `gpu-shaders.yml`

### Documentation

- Audit ledger split: closed findings move from `audit.md` to `deltas.md` (ROB-01, ROB-02, MAINT-02, MAINT-01, MAINT-03, TEST-02, MAINT-06 so far); `audit.md` keeps the open set (40 findings: 1 High, 15 Medium, 24 Low) with CLOSED pointers in the master table

## 1.6.0-dev-1d9d84d (2026-10-15 3:26:39 AM)

Codebase audit fixes, round 1 — the two High robustness findings and the
dead-payload cleanup, from the audit in `audit.md`.

### Bug Fixes

#### Graphics / GPU scaler

- Malformed texture binding names no longer terminate the process mid-frame: a preset alias or sampler named `PassOutputx`, a bare `User`/`OriginalHistory`, or a 20-digit `OriginalHistory99999999999999` index used to reach `std::stoi` and abort through the uncaught exception; every builtin-name suffix is now parsed without throwing, malformed indices resolve to the closest valid binding, and the unresolved-texture guard (one-time log + current-input-frame fallback) now covers every binding branch, including out-of-range `PassOutput#`/`PassFeedback#` indices and unmatched `*Feedback` aliases that previously could bind null (ROB-01)
- Malformed `#pragma parameter` numbers (`.` or `-` alone, accepted by the pragma regex) no longer abort preset translation; values now parse with the RetroArch/atof contract — malformed tokens become 0 instead of a thrown `std::invalid_argument` (ROB-01)
- A hostile tiny preset no longer aborts the game: `shaders = 2147483647` used to resize the pass vector toward ~250 GB (`std::bad_alloc` → terminate); the declared `shaders` count no longer sizes the chain (it is built lazily from the `shaderN` keys actually present), and a `shaderN` key with an index of 32 or above fails the preset with a clear "exceeds the limit of 32" error instead of allocating (ROB-02)

### Removed

#### Graphics / GPU scaler

- Deleted ~10K lines of dead built-in ScaleFX/HQx payload left behind when the built-in filters were removed from the Shader Filter option: `gpu_luts.hpp` (3,820 lines, unreferenced) and its generator `tools/gen_luts.py`, the 7 dead `.frag` sources (`hqx_index`, `hqx_blend`, `scalefx_pass0-4`), and the dead HQx/ScaleFX arrays in the embedded SPIR-V (`gpu_shader_spv_prebuilt.hpp`, 5,595 → 155 lines) and MSL (`gpu_shaders_msl.hpp`, 438 → 53 lines) headers. Only the shared fullscreen-vertex and blit-fragment programs remain baked, and the build-time `gpu_scaler_spv` generation list now covers exactly those two (MAINT-02)
