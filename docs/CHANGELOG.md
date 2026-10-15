# Changelog

All notable changes to this project will be documented in this file.

The format is based on [Keep a Changelog](https://keepachangelog.com/en/1.0.0/)
and this project adheres to [Semantic Versioning](https://semver.org/spec/v2.0.0.html).

Fork releases are numbered `1.6.0-dev-<commit>`, where `<commit>` is the
short hash of the commit a build or overlay was produced from, and are
dated with an America/Toronto timestamp. Upstream (diasurgical) release
history lives in [docs/CHANGELOG_UPSTREAM.md](docs/CHANGELOG_UPSTREAM.md)
and is not modified.

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
