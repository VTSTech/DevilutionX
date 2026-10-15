# Fix Deltas — Closed Findings

**DevilutionX (VTSTech GPU Shader Mod) v1.6.0-dev**

**Repository:** https://github.com/VTSTech/DevilutionX
**Baseline audit:** [audit.md](audit.md) @ commit `27548c8b2d6b1dbf7b4c348ecd79abd4598b8b99`
**Date:** 2026-10-15

This ledger tracks findings that have been **fixed and verified**. Each entry preserves the
original audit finding (title, severity, location) and records the resolution actually shipped:
what changed, in which files, and how it was verified. Once an entry appears here it is removed
from `audit.md` entirely — its master table, priority matrix, and counts list only the
open findings; this ledger is the single record of closures.

| ID | Severity | Category | Title | Closed |
|----|----------|----------|-------|--------|
| ROB-01 | **High** | Robustness | Uncaught `std::stoi` on texture binding names terminates the process mid-frame | 2026-10-15 |
| ROB-02 | **High** | Robustness | `shaders = 2147483647` in a preset triggers uncaught ~250 GB allocation → abort | 2026-10-15 |
| MAINT-02 | Medium | Maintainability | ~10K lines of dead built-in shader/LUT payload still compiled and embedded | 2026-10-15 |
| MAINT-01 | **High** | Maintainability | CI rebuilds entire vendored dep tree on every push to every branch, no caching | 2026-10-15 |
| MAINT-03 | Medium | Maintainability | Four broken upstream workflows ship active; cleanup script never run | 2026-10-15 |
| TEST-02 | **High** | Testing | No CI workflow ever runs the existing test suite | 2026-10-15 |
| MAINT-06 | Medium | Maintainability | vcpkg + `USE_SDL3` is broken: manifest requests an undefined `sdl3` feature | 2026-10-15 |

Standing rules for all future fixes: every touched C/C++ file must be clean under the project's
`Source/.clang-format` as enforced by **clang-format 18.1.8** (the exact version pinned by the
CI's `clang-format-check.yml`), and every fix round ships as a refreshed overlay zip
(`DevilutionX-<version>-overlay.zip`) in addition to the working tree.

---

## ROB-01 — CLOSED: Uncaught `std::stoi` on texture binding names terminates the process mid-frame

| Property | Value |
|----------|-------|
| **Severity** | High |
| **Category** | Robustness |
| **Original location** | `Source/engine/render/gpu/preset/preset_runner.cpp` (lines 1228, 1240, 1246, 1267, 438-439, 465), `Source/engine/render/gpu/preset/slang_translate.cpp` (lines 571, 231-235) |

**Original finding.** `Runner::Run` resolved per-pass texture bindings by name using
`std::stoi(name.substr(N))` after only a prefix check for `OriginalHistory`, `PassOutput`,
`PassFeedback`, and `User` prefixes. A preset whose alias or texture was named exactly
`"OriginalHistory"`, `"PassOutput"`, `"PassFeedback"`, or `"User"` — or whose suffix was
non-numeric (e.g. `PassOutputx`) — made `std::stoi` throw `std::invalid_argument`; a very long
numeric suffix threw `std::out_of_range`. No try/catch existed anywhere on the call path from
`PresentFrame`, so the exception propagated out of the frame loop and called `std::terminate`,
killing the game mid-frame. The same unchecked pattern existed in `Load` and in
`BuiltinTextureIndex` (`slang_translate.cpp:571`), and `std::stof` at `slang_translate.cpp:231-235`
accepted garbage like `"."` or `"-"` alone from `#pragma parameter` lines. This violated the
mod's failure contract ("any preset load/run failure → session fallback, game never dies").

**Resolution.**
- Replaced all 7 bare `std::stoi` sites in `preset_runner.cpp` with a non-throwing
  `ParseTextureIndex` helper built on the codebase's own `utils/parse_int.hpp` (returns
  `std::optional<int>`, no exceptions possible).
- Replaced the `std::stof` pragma sites in `slang_translate.cpp` with a `strtof`-based
  `ParsePragmaNumber` that reproduces `atof` semantics (malformed default/minimum parse as 0)
  without throwing.
- `BuiltinTextureIndex` now maps regex-valid but out-of-range indices (e.g. `OriginalHistory99999999999999`)
  to `-1` instead of throwing; translation then fails cleanly with a range-explaining error.
- Hoisted the null-texture guard in `Runner::Run` so *every* binding branch — including
  out-of-range `PassOutput#`/`PassFeedback#` and unmatched `*Feedback` aliases — falls back to
  the source texture instead of binding null.

**Verification.**
- All 3 touched TUs compile clean with the project's own flags (gnu++23, `-Wall -Wextra`, Release).
- Hostile-input regression harness (`test/preset_failure_modes.cpp`): 20/20 PASS covering
  non-numeric and empty suffixes, 20-digit indices, `#pragma parameter "."`, and valid-preset
  regression guards.
- Re-verified after the clang-format re-indentation fix (same results).

---

## ROB-02 — CLOSED: `shaders = 2147483647` in a preset triggers uncaught ~250 GB allocation → abort

| Property | Value |
|----------|-------|
| **Severity** | High |
| **Category** | Robustness |
| **Original location** | `Source/engine/render/gpu/preset/preset_parser.cpp` (lines 424, 535-536, 582-583) |

**Original finding.** The parser read `shaders = N` into `shaderCount` and resized
`preset.passes` accordingly, with no upper clamp. A 30-byte preset file containing
`shaders = 2147483647` made `std::vector<PassDef>::resize(2^31-1)` attempt an allocation on the
order of 250 GB — either throwing `std::bad_alloc`/`std::length_error` uncaught (→
`std::terminate`) or thrashing the system into OOM under overcommit. Meanwhile individual
`shaderN = path` keys with huge indices were already safe (`SplitPassKey` catches `stoi`
failures), making the unclamped count path an inconsistency rather than a design choice.

**Resolution.**
- Removed the declared-count resize path entirely: `shaders =` is consumed as metadata and the
  pass vector now grows lazily, one entry per `shaderN` key actually present — matching the
  libretro semantics the runner expects (the chain is defined by its keyed passes, not by a
  declared count).
- Added `kMaxPresetPasses = 32` with a clear load error for any `shaderN` key with
  `index >= 32` (`preset pass index 2147483647 exceeds the limit of 32`), consistent with the
  codebase's existing clamp convention (`histories` clamped 1..8).
- The padded-passes path was provably dead (entries erased immediately after resize) and was
  dropped with the resize.

**Verification.**
- Regression harness cases: `shaders = 2147483647` fails to load with an error naming the real
  problem; `shader2147483647 = x` fails naming the 32-pass limit; `shader31` loads, `shader32`
  is rejected; a valid 2-pass preset still parses identically (regression guard). 20/20 PASS.
- Compile-clean on project flags as above.

---

## MAINT-02 — CLOSED: ~10K lines of dead built-in shader/LUT payload still compiled and embedded

| Property | Value |
|----------|-------|
| **Severity** | Medium |
| **Category** | Maintainability |
| **Original location** | `Source/engine/render/gpu/gpu_luts.hpp`, `gpu_shader_spv_prebuilt.hpp` (lines 101-5538), `gpu_shaders_msl.hpp` (lines 39-417), `shaders/vulkan/hqx_*.frag`, `scalefx_pass*.frag`, `CMakeLists.txt` (lines 145-153) |

**Original finding.** The built-in HQ2x/ScaleFX filters had been removed from the option
(`options.cpp:433-436`), but their payloads still shipped: `gpu_luts.hpp` (3,820 lines of HQ2x/
HQ3x/HQ4x LUT data) was referenced by nothing anywhere in the build; the prebuilt SPV header
embedded ~5.4K lines of dead `kHqxIndex`/`kHqxBlend`/`kSfxPass0-4` SPIR-V (only `kVertex` and
`kBlit` are live); the MSL header carried the same dead set; and the `gpu_scaler_spv` CMake
target still compiled the 7 dead `.frag` sources on every build with glslangValidator. The dead
weight cost compile time and binary size, and — worst — reader confusion.

**Resolution.**
- Deleted `Source/engine/render/gpu/gpu_luts.hpp` (3,820 lines) and `tools/gen_luts.py` (its only
  remaining consumer).
- Deleted the 7 dead shader sources: `shaders/vulkan/hqx_blend.frag`, `hqx_index.frag`,
  `scalefx_pass0-4.frag`.
- Trimmed `gpu_shader_spv_prebuilt.hpp` from 5,595 to 155 lines (keeps `kVertex`/`kBlit` only)
  and `gpu_shaders_msl.hpp` from 438 to 53 lines (same pair).
- Trimmed `CMake/gen_spv_header.cmake` `SHADER_PAIRS` and the root `CMakeLists.txt` `DEPENDS`
  accordingly; fixed the stale baked-shader comments.
- All deleted content remains recoverable from git history (`27548c8`), as the audit recommended.

**Verification.**
- Full cmake configure succeeds with the vendored SDL3 stack; the `gpu_scaler_spv` generation
  path produces a header containing only the live pair.
- Whole `Source/` tree clang-format 18.1.8 scan: 0 violations / 697 files.
- Working-tree diff for the round: 135 insertions / 10,288 deletions across 17 files.

---

## MAINT-01 — CLOSED: CI rebuilds entire vendored dep tree on every push to every branch, no caching

| Property | Value |
|----------|-------|
| **Severity** | High |
| **Category** | Maintainability |
| **Original location** | `.github/workflows/gpu-shaders.yml` (trigger block, both jobs), `CMakeLists.txt` (ccache autodetect) |

**Original finding.** The fork's only real CI declared `push` with no branch or path filter —
every push, including README edits, ran both heavy jobs, and neither job configured any
`actions/cache` step or ccache despite the build system auto-detecting it. Every run
re-downloaded and recompiled the entire FetchContent tree (SDL3, SDL3_image, glslang,
SPIRV-Cross, libzt, libsodium, …) from scratch.

**Resolution.**
- Trigger filter: `push.paths-ignore: ['**.md', 'docs/**']` — doc-only commits no longer burn
  a full build.
- ccache wired into **both** jobs: `ccache` added to the apt install lists,
  `CMAKE_C_COMPILER_LAUNCHER`/`CMAKE_CXX_COMPILER_LAUNCHER=ccache` in both configure steps, and
  an `actions/cache@v4` restore step per job (`ccache-linux-x64-*` / `ccache-windows-x64-*`
  keys, SHA key + prefix restore-keys, 2 GB cap, `time_macros,pch_defines` sloppiness, cache
  dir pinned under the workspace so container jobs see it).

**Verification.** Workflow YAML parsed and step order asserted (both jobs: Checkout → Restore
ccache → Install → Configure → Build → [Test] → gates → Package → Upload). Cache effectiveness
itself is observable in the first pushed run (setup-ccache log lines + job duration).

---

## MAINT-03 — CLOSED: Four broken upstream workflows ship active; cleanup script never run

| Property | Value |
|----------|-------|
| **Severity** | Medium |
| **Category** | Maintainability |
| **Original location** | `.github/workflows/{clang-format-check,clang-tidy-check,translations,cache-cleanup}.yml`, `cleanup-fork-workflows.{sh,ps1}` |

**Original finding.** Four leftover upstream workflows pinned `actions/checkout@v7` (a version
that does not exist), so every triggered run failed instantly at checkout — red X's with no
signal. The fork author had shipped `cleanup-fork-workflows.sh`/`.ps1` to delete every workflow
except `gpu-shaders.yml` but never ran it.

**Resolution.** Curated rather than blanket-deleted, per the audit's "decide deliberately" note:
- `clang-format-check.yml` — **kept and fixed** (`actions/checkout@v7` → `@v4`; the
  `jidicula/clang-format-action@v4.18.0` pin was already valid). The format gate is actively
  used to police fork changes.
- `clang-tidy-check.yml` — **deleted** (configured an SDL2 apt stack that never matched the
  mod's SDL3 configuration).
- `translations.yml`, `cache-cleanup.yml` — **deleted** (upstream tooling; PR-scoped caches that
  never existed on this fork).
- `cleanup-fork-workflows.sh` / `.ps1` — **removed**: their one-shot purpose is fulfilled, and
  re-running them now would delete the deliberately-kept format check.
- `dependabot.yml` (github-actions ecosystem, weekly) kept — it now maintains the curated set.

**Verification.** `ls .github/workflows` shows exactly `clang-format-check.yml` (YAML-valid,
`@v4`) and `gpu-shaders.yml`.

---

## TEST-02 — CLOSED: No CI workflow ever runs the existing test suite

| Property | Value |
|----------|-------|
| **Severity** | High |
| **Category** | Testing |
| **Original location** | `.github/workflows/gpu-shaders.yml` (`-DBUILD_TESTING=OFF` in both jobs), `codecov.yml` |

**Original finding.** Both CI jobs passed `-DBUILD_TESTING=OFF` explicitly; no workflow ever
executed ctest. The entire existing gtest suite — dungeon generation, savegame codec, UI state,
the deterministic timedemo regression — ran only when a contributor remembered to run it
locally. (It was not merely un-run: in this fork it did not even *build* — see below.)

**Resolution.**
- CI: the Linux job now configures `-DBUILD_TESTING=ON -DDEVILUTIONX_SYSTEM_GOOGLETEST=OFF
  -DDEVILUTIONX_SYSTEM_BENCHMARK=OFF` (vendored googletest/benchmark, consistent with the
  project's zero-submodule reproducible-deps convention) and runs
  `ctest --test-dir build --output-on-failure` after Build. Windows stays `BUILD_TESTING=OFF`
  (cross-compiled binaries cannot execute on the runner).
- The suite itself required three real fixes to build and pass headless:
  1. **gmock link gap** — the standalone tests include `<gmock/gmock.h>` but linked only
     `GTest::gtest_main`, which carries no gmock include dirs; they only ever built where system
     gtest headers sat on the default include path. `CMake/Tests.cmake` now links
     `GTest::gmock` into standalone tests whenever that target exists.
  2. **Fork link-closure gap** — the fork's settings browser in `options.cpp` (compiled
     `USE_GPU_SCALER`) calls `ScanPresets`/`ListPresetDir`/`ShaderRootDir`, which existed only
     inside the engine monolith `libdevilutionx`. Four standalone test/benchmark targets whose
     closure contains `libdevilutionx_options` failed to link; they now compile the two preset
     TUs (`preset_parser.cpp`, `slang_translate.cpp`) directly.
  3. **Fixture skip-path crash** — `UITest`-derived fixtures override `SetUp()` and call the
     base first; the base's `GTEST_SKIP()` (MPQ fixtures absent) is a plain `return`, so the
     derived setup continued into uninitialized engine state (null `MyPlayer`, unloaded quest
     data) and segfaulted all 65 cases in `inventory_ui_test`, `panel_state_test`,
     `spell_ui_test`. Derived SetUps now bail via the new `UITest::MissingAssets()`, and
     `UITest::TearDown()` guards its cleanup the same way.

**Verification.** Full ctest suite headless: **608/608 tests pass** (before the fixes: build
failure for vendored gtest, then 89% pass with 65 segfaults). `preset_failure_modes` (the
ROB-01/ROB-02 harness, TEST-01 partial progress) runs as test #608 in the same invocation.
*Residual:* `codecov.yml` coverage upload remains dormant (project target 0%, comments off) —
tracked as open scope under the coverage umbrella rather than blocking this closure.

---

## MAINT-06 — CLOSED: vcpkg + `USE_SDL3` is broken: manifest requests an undefined `sdl3` feature

| Property | Value |
|----------|-------|
| **Severity** | Medium |
| **Category** | Maintainability |
| **Original location** | `CMake/VcPkgManifestFeatures.cmake` (lines 4-5), `vcpkg.json` |

**Original finding.** `VcPkgManifestFeatures.cmake` appends feature name `"sdl3"` when
`USE_SDL3=ON`, but `vcpkg.json` defined no `sdl3` feature — any vcpkg-based configure (the
documented MSVC path) with the flagship `USE_SDL3=ON` aborted during manifest install.

**Resolution.** Added the feature to `vcpkg.json`:
`"sdl3": { "description": "Use SDL3", "dependencies": ["sdl3", "sdl3-image"] }` (mirroring the
`sdl2` feature's shape, including the image codec). The cosmetic `version-string: 1.6.0` vs
`VERSION` = `1.6.0-dev` divergence was left untouched.

**Verification.** `vcpkg.json` re-parsed as valid JSON; feature name matches the exact string
`VcPkgManifestFeatures.cmake` appends. End-to-end vcpkg configure is exercised on MSVC path
usage (not covered by this fork's CI).
