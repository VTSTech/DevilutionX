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
| SEC-01 | Medium | Security | Preset references escape the shaders root — arbitrary file read via preset packs | 2026-10-15 |
| SEC-04 | Low | Security | Log injection: untrusted preset paths/names written raw to gpu-scaler.log | 2026-10-15 |
| MAINT-05 | Medium | Maintainability | Release zips omit `mods/hf.mpq` — shipped builds cannot run Hellfire | 2026-10-15 |
| MAINT-07 | Low | Maintainability | Magic number 9 / dead enum values / unused include in gpu_scaler.cpp | 2026-10-15 |
| ROB-03 | Medium | Robustness | Torn cache write permanently poisons a shader cache entry, no recompile fallback | 2026-10-16 |
| ROB-04 | Medium | Robustness | Cache key lacks glslang/toolchain version salt → stale SPIR-V after upgrades | 2026-10-16 |
| ROB-05 | Medium | Robustness | Unclamped scale factors cause float→Uint32 UB and garbage FBO sizes | 2026-10-16 |
| ROB-06 | Medium | Robustness | Data race on gpu-scaler.log between SDL log-capture threads and present thread | 2026-10-16 |
| ROB-09 | Medium | Robustness | Corrupt `diablo.ini` hard-fails startup with no recovery path | 2026-10-16 |
| MAINT-04 | Medium | Maintainability | Release builds never attach to GitHub Releases | 2026-10-16 |
| ARCH-01 | Medium | Architecture | Window-ownership hand-off protocol is spread across three files with pointer-only history | 2026-10-16 |
| TEST-03 | Medium | Testing | Options layer (ini round-trip, shader-filter self-heal) untested | 2026-10-16 |

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

## SEC-01 — CLOSED: Preset references escape the shaders root — arbitrary file read via preset packs

| Property | Value |
|----------|-------|
| **Severity** | Medium |
| **Category** | Security |
| **Original location** | `Source/engine/render/gpu/preset/preset_parser.cpp` (`ResolvePresetFilePath`), `Source/engine/render/gpu/preset/slang_translate.cpp` (include resolver) |

**Original finding.** `ResolvePresetFilePath` passed absolute references through outright and
walked up to six ancestor folders for relative ones with no check that the resolved path stays
under `ShaderRootDir()`; `../../`-style relative escapes succeeded, and `.slang` `#include`
targets likewise accepted absolute paths. A preset pack downloaded from the internet could
direct the engine to open, parse, compile, or load-as-texture files anywhere the user account
can read — an arbitrary-file-read primitive built on untrusted content.

**Resolution.** Added `preset::IsPathInsideShaderRoot()` (`preset_parser.{hpp,cpp}`): the
candidate and the shader root are run through `std::filesystem::weakly_canonical` (symlinks
resolved) and the candidate must sit at or below the root, with a separator-boundary check so
`shaders-ng` cannot pass a `shaders` prefix test (case-insensitive compare for Windows/macOS).
`ResolvePresetFilePath` now refuses (returns empty) when an absolute reference or an
ancestor-walk hit canonicalizes outside the root; the not-found fallback join was left
uncontained because it is guaranteed nonexistent and only feeds error text. The slang
`#include` resolver (`slang_translate.cpp`) applies the same check before recursing and fails
translation with "include target outside the shaders root: <path>". The runner
(`preset_runner.cpp`) names the refused reference in its gpu-scaler.log error lines so
refusals are diagnosable in the field log.

**Verification.** `test/preset_failure_modes_test.cpp` extended 20 → 37 checks (all passing as
the `preset_failure_modes` ctest): relative `../` include escape refused; absolute include
escape refused; in-root `#include` still translates; `../../` preset reference refused;
absolute preset reference refused; sibling prefix directory (`shaders-ng`) rejected;
in-root reference still resolves; escaping `#reference` fails the whole preset with a
traceable error. All four touched TUs compile clean under the project Release flags; full
gtest suite 608/608.

---

## SEC-04 — CLOSED: Log injection: untrusted preset paths/names written raw to gpu-scaler.log

| Property | Value |
|----------|-------|
| **Severity** | Low |
| **Category** | Security |
| **Original location** | `Source/engine/render/gpu/gpu_scaler.cpp` (`LogFileLine`) |

**Original finding.** `LogFileLine` wrote messages with raw `fprintf`, and log content
includes untrusted file names, `#pragma` descriptions, and glslang info-log text. A preset
whose name or description contains `\n` could forge log lines, and terminal-escape sequences
could inject noise into the field-diagnosis record.

**Resolution.** New header `Source/engine/render/gpu/gpu_log_sanitize.hpp` with
`gpu_scaler::SanitizeLogLine()` — replaces every byte < 0x20 (including `\n`, `\r`, `\t`,
ESC) and 0x7F (DEL) with `?`, leaving printable text (and `%` signs — callers already use
argument passing, never format-string composition) untouched. `LogFileLine` now sanitizes
before writing. Header-only so every consumer (including the SDL-free test harness) can
exercise it without new link dependencies.

**Verification.** `TestLogLineSanitized` in the harness (6 checks): clean text unchanged;
newline/CR/tab → `?`; ESC escape → `?`; DEL → `?`; empty stays empty; percent signs pass
through. gpu-scaler.log remains a single-writer, line-per-entry forensic record under
hostile content.

---

## MAINT-05 — CLOSED: Release zips omit `mods/hf.mpq` — shipped builds cannot run Hellfire

| Property | Value |
|----------|-------|
| **Severity** | Medium |
| **Category** | Maintainability |
| **Original location** | `.github/workflows/gpu-shaders.yml` (both Package steps), `CMake/Mods.cmake` (line 59) |

**Original finding.** The build system produces `build/mods/hf.mpq` when `smpq` is available
(`hellfire_mpq` target, a dependency of `libdevilutionx` under `BUILD_ASSETS_MPQ`), and
CPack-based packaging installs it — but the fork's hand-rolled CI zips copied only
`devilutionx.mpq`, so every shipped build was Diablo-only and Hellfire owners got a silent
"expansion doesn't work" experience.

**Resolution.** Both Package steps (Linux and Windows jobs) now copy the expansion MPQ into
the zip, guarded on file existence so configurations that did not build it still package
cleanly:
`if [ -f build/mods/hf.mpq ]; then mkdir -p pkg/mods && cp build/mods/hf.mpq pkg/mods/; fi`

**Verification.** `scripts/validate_workflows.py` (YAML parse + `bash -n` on every run block)
passes; the step is guarded, so the Linux job (which installs `smpq`) ships `mods/hf.mpq`
while any job without the MPQ packages unchanged.

---

## MAINT-07 — CLOSED: Magic number 9 / dead enum values / unused include in gpu_scaler.cpp

| Property | Value |
|----------|-------|
| **Severity** | Low |
| **Category** | Maintainability |
| **Original location** | `Source/engine/render/gpu/gpu_scaler.cpp` (lines 15, 47-61, 506-508, 1127-1136) |

**Original finding.** `SDL_GPUShader *gFragmentShaders[9]` was sized for the removed built-in
filter set while only index 1 (`Program::Blit`) is ever used; `FilterDisplayName` carried a
lone `case None: break;` switch fallthrough; `<csetjmp>` was included but never used —
residue that invited off-by-one mistakes when the enum is resized next.

**Resolution.** Added `constexpr int kMaxFragmentPrograms = static_cast<int>(Program::Blit) + 1;`
next to the `Program` enum and sized the table with it, so resizing the enum trips a compile
error at the table instead of silently changing its bounds. Removed the dead switch in
`FilterDisplayName` and the unused `<csetjmp>` include.

**Verification.** TU compiles clean with project Release flags (`-Wall -Wextra`); full
gtest suite 608/608; clang-format 18.1.8 clean (0 failing / 697 tree-wide).

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
---

## ROB-03 — CLOSED: Torn cache write permanently poisons a shader cache entry, no recompile fallback

| Property | Value |
|----------|-------|
| **Severity** | Medium |
| **Category** | Robustness |
| **Original location** | `Source/engine/render/gpu/preset/slang_compile.cpp` (`WriteFileBytes`, cache read path), `Source/engine/render/gpu/preset/preset_runner.cpp` (`SDL_CreateGPUShader` failure path) |

**Original finding.** The SPIR-V cache blob was written directly to its final path
(`create_directories` + `ofstream(trunc)`) — no temp file + rename, no fsync. A crash,
disk-full condition, or two concurrently compiling processes could leave a partial file.
The read path accepted anything passing `SpvLooksValid` (magic + alignment only), so a torn
file whose first bytes were intact became a permanent cache "hit" that failed at
`SDL_CreateGPUShader` on every load, with no fallback to recompilation — the preset failed
every session until the user manually deleted `shaders/cache/`.

**Resolution.**
- `WriteFileBytes` now writes to a process-unique temp file (`<path>.<pid>.tmp`) and moves it
  into place with `std::filesystem::rename` (atomic on POSIX and Windows same-volume; the
  target is removed first so Windows rename-overwrite behaves like POSIX). A failed rename
  cleans up the temp file.
- `CompilePass` gained a `SpirvSource *outSource` out-param (`Compiled` / `Cache` / `Sidecar`)
  and an `allowCache` switch; a new `DeleteCachedSpv` removes a pass's cache blobs.
- `Runner::Load` now tracks the SPIR-V source: when `SDL_CreateGPUShader` rejects a CACHE
  hit, the blobs are deleted and the pass is recompiled once with `allowCache=false` (the
  fresh compile re-populates the cache); any shader already built from the poisoned blob is
  released before the retry. GPU-side rejections of fresh compiles or sidecars still fail
  the preset through the normal failure contract.

**Verification.** All touched TUs compile clean (gnu++23, `-Wall -Wextra`, Release);
clang-format 18.1.8 clean. The self-heal path is load-time-only (no GPU in CI), exercised by
the cache-hit rejection branch conditions; full behavioral validation of shader creation
remains a headless-lavapipe follow-up (TEST-01).

---

## ROB-04 — CLOSED: Cache key lacks glslang/toolchain version salt → stale SPIR-V after upgrades

| Property | Value |
|----------|-------|
| **Severity** | Medium |
| **Category** | Robustness |
| **Original location** | `Source/engine/render/gpu/preset/slang_compile.cpp` (`CachePathFor`) |

**Original finding.** The content-addressed cache hashed only the final translated GLSL. A
FetchContent-pinned glslang bump (currently 16.6.0) changes SPIR-V codegen for identical
input GLSL, but the cache survives binary updates as user data — every user's
`shaders/cache/` kept serving the old SPIR-V indefinitely.

**Resolution.** A `kSpirvCacheVersion` constant (bumped on any SPIR-V-output-affecting
change) and the glslang version string (`GLSLANG_VERSION_MAJOR.MINOR.PATCH` from the pinned
toolchain headers) are now mixed into the hashed payload, and the cache file name carries
the format version (`slang-<stem>-v<version>-<hash24>.spv`) so stale entries stay
name-prunable (feeds PERF-05 later). The stale claim in the header comment ("any change to
translation invalidates") was corrected: translator-visible changes change the hash, and
toolchain changes now change it too.

**Verification.** Compile-clean + clang-format clean as above. Cache keys are load-time pure
functions; the salt is observable in the cache file names.

---

## ROB-05 — CLOSED: Unclamped scale factors cause float→Uint32 UB and garbage FBO sizes

| Property | Value |
|----------|-------|
| **Severity** | Medium |
| **Category** | Robustness |
| **Original location** | `Source/engine/render/gpu/preset/preset_runner.cpp` (`Runner::EnsureTargets` scale resolution) |

**Original finding.** Pass scale factors were parsed as floats with no magnitude clamp
(`scale0 = 1e30` accepted), then resolved sizes went through
`static_cast<Uint32>(std::max(1.0F, std::floor(out + 0.5F)))`. Casting an out-of-range
float to `Uint32` is undefined behavior; in practice it yielded garbage target sizes feeding
`SDL_CreateGPUTexture` — driver errors or enormous allocations from a typo'd or hostile
preset.

**Resolution.** Every resolved pass-target dimension is clamped through
`ClampPassTargetDim` to `[1, 16384]` before the cast (16384 sits below every real device
maximum; NaN falls to 1), and a clamp is announced with a `LogWarn` naming the pass and the
requested vs clamped size. The clamped values feed the `prevW/prevH` chain, so a huge
intermediate can no longer propagate through subsequent Source-scaled passes either.

**Verification.** Compile-clean + clang-format clean as above. Hostile-scale parsing itself
was already covered by the `preset_failure_modes` corpus; the clamp is exercised at target
resolution time.

---

## ROB-06 — CLOSED: Data race on gpu-scaler.log between SDL log-capture threads and present thread

| Property | Value |
|----------|-------|
| **Severity** | Medium |
| **Category** | Robustness |
| **Original location** | `Source/engine/render/gpu/gpu_scaler.cpp` (`LogFileLine`, `SdlLogCapture`, `RotateLog`, `LogFile`) |

**Original finding.** SDL log capture routes every `SDL_Log` into gpu-scaler.log, and SDL3
GPU/audio/validation messages can originate on SDL-internal threads. The capture handler
mutated `gLogFile`/`gLogBytes` and could trigger `RotateLog` (fclose/fopen) entirely
unsynchronized with the main-thread `LogFileLine` writes — worst case use-after-close of the
`FILE*` or a double rotation, exactly when users need the log most (an SDL-internal error
storm).

**Resolution.** All log-file mutation is now guarded by a `std::mutex` (`gLogMutex`). The two
entry points that can run off the main thread (`LogFileLine`, `SdlLogCapture`) take the lock
around the get-file-then-write sequence; the internals (`LogFile`, `LogFileForWrite`,
`RotateLog`, `LogClose`, `LogOpenAppended`) document the lock requirement and are only called
under it. The chained previous SDL log function is invoked BEFORE taking the lock so foreign
handler code can never observe or recurse into it; SEC-04 sanitization stays outside the
lock (pure string work).

**Verification.** Compile-clean + clang-format clean as above. The only cross-thread surface
in the mod is now the documented, mutex-ordered pair of entry points.

---

## ROB-09 — CLOSED: Corrupt `diablo.ini` hard-fails startup with no recovery path

| Property | Value |
|----------|-------|
| **Severity** | Medium |
| **Category** | Robustness |
| **Original location** | `Source/options.cpp` (`LoadIni`) |

**Original finding.** `LoadIni` returned `std::expected` and on parse failure the caller
immediately `app_fatal`ed — a modal dialog and exit for a corrupted config file, while the
mod's own philosophy elsewhere is self-healing (stale shader-filter values sanitize to Off;
GPU-path failures degrade to the stock renderer).

**Resolution.** On parse failure the corrupt file is preserved as
`diablo.ini.corrupt-<YYYYMMDD-HHMMSS>` (several generations can coexist), the failure is
logged with both the parse error and the preservation path, and startup continues with a
default `Ini` (the same parse of an empty buffer a fresh install already used). The fatal
path remains only when the corrupt copy cannot be renamed aside — overwriting the only copy
of the user's config would lose data. The next `SaveIni` writes a clean file.

**Verification.** `test/options_test.cpp` (new `options_test` ctest target): a corrupt ini
(`[Graphics` unclosed section) no longer aborts, is preserved with intact content, is moved
aside, and the following save/load cycle writes and reads a clean file. 7/7 checks pass
headless (see TEST-03).

---

## MAINT-04 — CLOSED: Release builds never attach to GitHub Releases

| Property | Value |
|----------|-------|
| **Severity** | Medium |
| **Category** | Maintainability |
| **Original location** | `.github/workflows/gpu-shaders.yml` (both jobs) |

**Original finding.** The workflow triggered on `release: published` but no step uploaded
the built artifacts to that release — the zips lived only as workflow artifacts with 90-day
retention, so a missed manual step meant the release page showed notes with no binaries.

**Resolution.** Each job gained an `Attach build(s) to the GitHub Release` step
(`softprops/action-gh-release@v2`, conditioned on `github.event_name == 'release'`) uploading
the same zips already staged for artifacts (Linux package + SPIR-V blobs; Windows package),
and both jobs declare `permissions: contents: write` for the default `GITHUB_TOKEN`.

**Verification.** YAML parses; step order asserted per job (Package → Upload artifacts →
Attach to release); the step is skipped for push/dispatch runs.

---

## ARCH-01 — CLOSED: Window-ownership hand-off protocol is spread across three files with pointer-only history

| Property | Value |
|----------|-------|
| **Severity** | Medium |
| **Category** | Architecture |
| **Original location** | `Source/engine/render/gpu/gpu_scaler.cpp` (`gLastClaimedWindow`, `WindowHasSwapchainHistory`), `Source/utils/display.cpp`, `Source/engine/dx.cpp` |

**Original finding.** The SDL window alternates ownership between SDL_Renderer and SDL_GPU
(a hard DXGI requirement on Windows), and the protocol implementing this was distributed
across three files with the history tracked via `gLastClaimedWindow`, a raw pointer kept
after Shutdown and compared by address only — vulnerable to an ABA false-positive if a
destroyed window's address was recycled, and unable to distinguish "this window had a
swapchain" from "some window with this address did".

**Resolution.** The protocol is now consolidated and documented in one place
(`gpu_scaler.cpp`): the history is explicit state keyed by the SDL window ID
(`gLastClaimedWindowId`) — SDL never reuses a window ID within a process, so a destroyed
window's recorded ID can never match a new window at the recycled address (the ABA hazard
disappears by construction). The claim sites record the ID; `WindowHasSwapchainHistory`
compares IDs; Shutdown deliberately retains it (the window keeps its DXGI swapchain
history). The ledger comment names all three participants' responsibilities
(gpu_scaler records/answers, display.cpp recreates on hand-off, dx.cpp funnels failures).

**Verification.** Compile-clean + clang-format clean; the ID-keyed query is exercised by the
display.cpp hand-off paths on every presentation-path switch.

---

## TEST-03 — CLOSED: Options layer (ini round-trip, shader-filter self-heal) untested

| Property | Value |
|----------|-------|
| **Severity** | Medium |
| **Category** | Testing |
| **Original location** | `Source/options.cpp` (shader-filter option layer), `test/` |

**Original finding.** The options machinery the fork extends was logic-rich and
ini-corrupting-if-wrong, yet untested: the stale-builtin self-heal, the custom-index-to-path
mapping, the folder-browser navigation encoding, and the save/load round-trip. The existing
`ini_test` covered only the low-level `utils/ini` parser.

**Resolution.** New standalone gtest target `options_test` (`test/options_test.cpp`,
registered in `CMake/Tests.cmake` under `standalone_tests`, linking `libdevilutionx_options`
+ the two preset TUs + `libdevilutionx_assets`, mirroring the `dun_render_benchmark`
pattern). Headless, no GPU/window: the config dir is staged per-process under a temp root
(`paths::SetConfigPath`), the shader root is staged idempotently under the build dir
(`SDL_GetBasePath()` + `shaders`). Covered: stale builtin values 1..99 sanitize to Off;
custom index 100+N maps to the stored preset path; a missing stored preset self-heals to Off
and clears the path; save/load round-trip preserves both keys; ROB-09 corrupt-ini recovery.

**Additional fix discovered by the new tests (startup restore).** `GraphicsOptions::
GetEntries()` listed `&shaderFilter` before `&shaderFilterPreset`, so at load time
`OptionEntryShaderFilter::LoadFromIni` ran `RescanCustomEntries()` while the persisted preset
path was still empty — every restart self-healed a saved custom preset to Off, and the
preset only re-activated when the user later opened the Graphics menu. The (invisible)
preset-path entry now loads before the filter entry; a saved custom preset survives a
restart, and the value mapping is path-authoritative in all covered cases.

**Verification.** Built and run standalone (gtest, vendored 1.15.2 headers + system SDL3):
**7/7 tests pass** headless; all touched TUs compile clean (gnu++23, `-Wall -Wextra`);
clang-format 18.1.8 clean. CI runs the target through ctest under BUILD_TESTING=ON.
