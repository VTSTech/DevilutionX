# Fix Deltas — Closed Findings

**DevilutionX (VTSTech GPU Shader Mod) v1.6.0-dev**

**Repository:** https://github.com/VTSTech/DevilutionX
**Baseline audit:** [audit.md](audit.md) @ commit `27548c8b2d6b1dbf7b4c348ecd79abd4598b8b99`
**Date:** 2026-10-15

This ledger tracks findings that have been **fixed and verified**. Each entry preserves the
original audit finding (title, severity, location) and records the resolution actually shipped:
what changed, in which files, and how it was verified. Once an entry appears here it is removed
from `audit.md` entirely — its master table, priority matrix, and counts list only the
open findings; this ledger is the single record of closures. Since round 12 (2026-10-17) it
also carries the round-by-round fix history moved out of `audit.md`'s Executive Summary —
see [Fix History](#fix-history) below — so the audit stays a pure current-state
snapshot.

| ID | Severity | Category | Title | Closed |
|----|----------|----------|-------|--------|
| ROB-01 | **High** | Robustness | Uncaught `std::stoi` on texture binding names terminates the process mid-frame | 2026-10-15 |
| ROB-02 | **High** | Robustness | `shaders = 2147483647` in a preset triggers uncaught ~250 GB allocation → abort | 2026-10-15 |
| MAINT-02 | Medium | Maintainability | ~10K lines of dead built-in shader/LUT payload still compiled and embedded | 2026-10-15 |
| MAINT-01 | **High** | Maintainability | CI rebuilds entire vendored dep tree on every push to every branch, no caching | 2026-10-15 |
| MAINT-03 | Medium | Maintainability | Four broken upstream workflows ship active — REOPENED round 7, **re-closed round 7.5** (workflows actually deleted this time, tree-verified) | 2026-10-16 |
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
| TEST-01 | **High** | Testing | Zero test coverage for the fork's ~15K-line GPU pipeline | 2026-10-16 |
| ROB-14 | **High** | Robustness | Scaler activation destroys the SDL_Renderer with renderer-owned textures still referenced — use-after-free on filter toggle | 2026-10-16 |
| ROB-15 | **High** | Robustness | `Runner::Run` returns the post-flip double-buffer slot for a double-buffered final pass — one-frame-stale presentation | 2026-10-16 |
| ROB-16 | Medium | Robustness | Unchecked `SDL_MapGPUTransferBuffer` → `memcpy` to NULL, bypassing the failure contract | 2026-10-16 |
| ROB-17 | Medium | Robustness | `SDL_BeginGPUCopyPass`/`SDL_UploadToGPUTexture` results ignored — silent stale-frame presentation, uninstrumented | 2026-10-16 |
| ROB-24 | Low | Robustness | Vulkan-retry claim failure leaves `gWindow` set → release of an unclaimed window | 2026-10-16 |
| ROB-25 | Low | Robustness | Non-custom-filter skip is silent and uncounted | 2026-10-16 |
| MAINT-16 | Low | Maintainability | Self-heal recompile does not repopulate the cache — comment claims it does | 2026-10-16 |
| TEST-07 | Low | Testing | `wav-fallback-test.yml` paths filter omits the files round 6 actually modified | 2026-10-16 |
| ROB-18 | Medium | Robustness | Unsupported auto-selected GPU backend hard-fails the session; the Vulkan retry never applies to it | 2026-10-16 |
| ROB-19 | Medium | Robustness | `RecreateMainRenderWindow` failure is fatal on the fallback-of-the-fallback path | 2026-10-16 |
| ROB-22 | Medium | Robustness | Virtual-gamepad textures dangle across scaler activation; fallback renderer never re-initializes them | 2026-10-16 |
| ROB-20 | Medium | Robustness | Unbounded preset/shader file slurp → uncaught `bad_alloc` terminates the process (no top-level guard) | 2026-10-16 |
| ROB-21 | Medium | Robustness | Duplicate sampler declarations get duplicate dense bindings (include-once is per-bucket only) | 2026-10-16 |
| MAINT-15 | Medium | Maintainability | CI compiles baked shaders with unpinned apt `glslang-tools`; baked SPV is non-reproducible | 2026-10-17 |
| FEAT-02 | Medium | New Feature | Resolve the Metal strategy: wire the MSL path or fail fast with a clear message | 2026-10-17 |
| PERF-06 | Medium | Performance | Quadratic scans in the translator: UBO skip-loop and per-declaration full-text rescans | 2026-10-17 |
| ROB-08 | Medium | Robustness | Mid-session vsync changes never reach the GPU swapchain | 2026-10-17 |
| ROB-23 | Medium | Robustness | `FillBlockBytes` semantic bugs: `*Size` parameters swallowed, `writeVec4` over-writes, int members get float bits | 2026-10-17 |
| TEST-06 | Medium | Testing | `preset_smoke` known-texture set omits `#pragma name` identifiers — diverges from the runner | 2026-10-17 |
| ROB-07 | Medium | Robustness | `Runner::Load` mid-failure leaks GPU resources; runtimes lack RAII | 2026-10-17 |
| FEAT-01 | Medium | New Feature | Preset failure retry / hot reload of failed presets | 2026-10-17 |
| FEAT-03 | Medium | New Feature | Surface preset errors in-game instead of only in gpu-scaler.log | 2026-10-17 |

Standing rules for all future fixes: every touched C/C++ file must be clean under the project's
`Source/.clang-format` as enforced by **clang-format 18.1.8** (the exact version pinned by the
CI's `clang-format-check.yml`), and every fix round ships as a refreshed overlay zip
(`DevilutionX-<version>-overlay.zip`) in addition to the working tree.

## Fix History

Moved here from audit.md's Executive Summary in round 12 (2026-10-17) so the audit stays a
current-state snapshot and this ledger carries the full history. Rounds 1–6 enumerated
the findings; every round after that worked the backlog. This index is chronological; each
closure's full Original/Resolution/Verification record follows in ID order.

- **Rounds 1–6 (2026-10-15 → 2026-10-16, enumeration).** Examined the fork's
  entire delta and its integration points, the engine's game loop and options architecture,
  and the build/CI/packaging layer, down to individual SDL_GPU API invocations — 48
  findings, of which the first 19 closures were regression-verified before round 7 (one of
  them, MAINT-03, round 7 reopened). Per-round narrative was condensed when the Executive
  Summary was rebuilt (`d071ac558`); the records below carry each fix's story.
- **Round 7 (2026-10-16, fresh-eyes sweep).** Code unchanged since round 6 (`b60356e1a` —
  the only newer commit, `8341ef99f`, is docs-only). All 19 closures to date were re-verified
  and hold. The sweep produced **25 new findings — 2 High, 11 Medium, 12 Low** —
  and **reopened MAINT-03**: the three workflows its closure claimed to delete were never
  actually removed from any commit and still shipped with the broken `actions/checkout@v7`
  pin. The round-7 theme was that **the success paths, not the failure paths, carried the
  sharpest risk**: the two new Highs sat on ordinary, happy flows (ROB-14's use-after-free on
  filter toggle, ROB-15's one-frame-stale feedback presentation), behind them a cluster of
  **unchecked SDL_GPU return values** that bypassed the failure contract (ROB-16, ROB-17,
  ROB-18), plus **closure-integrity** process findings (MAINT-03, TEST-07) and
  **translator/resource hardening** items (ROB-20, ROB-21, ROB-23). At the close of round 7
  the open set stood at 53.
- **Round 7.5 (2026-10-16, quick-fix batch).** Closed ROB-14/15/16/17 with riders ROB-24/25,
  MAINT-16, TEST-07, and the MAINT-03 re-close — the workflows actually deleted this
  time, tree-verified. Open set 53 → 44.
- **Round 7.6.1 (2026-10-16, no finding ID).** Corrected the round-7.5 clang-format
  verification record — its own section below.
- **Round 7.6.2 (2026-10-16, no finding ID).** Fixed the `preset_smoke_corpus` ctest geometry
  (TEST-01 fixup addendum).
- **Round 8 (2026-10-16).** Closed ROB-18/19/22 — bounded fallback-path changes plus
  the gamepad free/re-init pairing.
- **Round 9 (2026-10-16).** Closed ROB-20/21 — the `gpu/preset` load path: size caps
  before every untrusted slurp, a catch-all on `ParsePreset`/`TranslateSlang`, and the
  `RewriteSamplers` name-dedupe, with regression corpus.
- **Merge-plan alignment (2026-10-16, same day — planning, no code change).** The open
  set gained its governing sequence: the upstream merge plan (r2,
  `docs/merge-plan-draft-r2.md`) gates every proposed PR on the findings that share its
  files, and orders closure into three Phase-0 sweeps — **8A** library surface
  (ROB-20/21, PERF-06, MAINT-15 plus the SEC/ROB/MAINT/TEST/PERF items on `gpu/preset/*`),
  **8B** presentation surface (ROB-08/18/19/22, FEAT-02 disposition, ROB-30 — was
  ROB-26, renumbered at the round-10 re-head — MAINT-10, PERF-01/03), **8C** runner
  surface (ROB-07/23, TEST-06, ROB-10/11, MAINT-17, PERF-02) — with first contact
  (PR 4 + PR 1) barred until 8A+8B complete. Waived out of the PR series: MAINT-11/12/13/14 +
  TEST-05 (upstream issues, not PR riders), ARCH-02 (fork-only Phase-0 hygiene), ROB-13
  (known-documented), FEAT-01/03/04 (deferred). Strategic frame recorded in handoff §14:
  whether or not any PR merges, the long-term goal is a standalone reusable shader-preset
  library — every closure in this ledger is a step toward that gate.
- **Round 10 (2026-10-17, field-regression re-head).** Re-headed the two field-regression
  closures that ended the Shaders→'Off' CTD — **ROB-26** (synchronous
  GPU-device teardown inside the switch; resolved by the deferred `HandbackWindow` teardown,
  69be0cad5) and **ROB-27** (menu-context null item-cursor CLX read in `LoadPotionArt`;
  resolved by the `GetInvItemSize`/`LoadPotionArt` guards, b0b4d5e0a) — both recorded
  below and **field-verified on build `1f7d111fd`: switching Shaders to Off with a preset
  active no longer crashes**. The field closures draw nothing from the open list (ROB-27
  never carried an open entry, and the old ROB-26 slot was a different finding); to free the
  ID the field record already shipped under, the open Low swapchain-acquire finding is
  renumbered **ROB-26 → ROB-30**. The same round re-headed the open list and
  re-indented `test/preset_unload_test.cpp` to the test clang-format profile.
- **Bar-arithmetic errata (2026-10-17, docs-only, between rounds 10 and 11 — no round
  number).** The first-contact bar corrected everywhere to **9 of the 14** Medium open when
  r2 was measured — ROB-20 had been double-counted across the PR 1/PR 2 gates; dated
  errata paragraphs live in `merge-plan-draft-r2.md` and audit.md's 8B note.
- **Round 11 (2026-10-17).** Closed **MAINT-15** (the CI jobs bake no SPIR-V anymore — a
  new `GPU_SCALER_SPV_PREBUILT` CMake option embeds the committed
  `gpu_shader_spv_prebuilt.hpp`, so shipped bytes are a function of the repo; the Linux job's
  glslang stays only as the Package step's source-health canary) and **FEAT-02** (Metal
  dispositioned: `CreateDevice` no longer accepts MSL, auto-selected Metal logs an honest
  message and takes the ROB-18 Vulkan retry — MoltenVK stays viable — and the
  handoff table now matches the code). Open set 40 → 38 (0 High / 7 Medium / 31 Low);
  first-contact bar down to ROB-08 + PERF-06.
- **Round 12 (2026-10-17, no finding ID — this restructure).** Fix history moved out of
  audit.md's Executive Summary into this section; the Executive Summary regenerated as a
  current-state snapshot; the index table above gained the two round-11 rows it was missing
  (MAINT-15, FEAT-02); audit.md's header counts corrected to 35 closed / 38 open; the
  handoff's ledger line updated to the round-11 state and its stale “10 of 14”
  bar note aligned with the 9-of-14 errata. No findings closed, no code changed; the ledger
  stands at 38 open (0 High / 7 Medium / 31 Low).
- **Round 13 (2026-10-17).** Closed the last two first-contact-bar Mediums — **PERF-06**
  (one-pass `SamplerScanIndex` in the translator replaces the five per-declaration
  full-text rescans; the record carries a measured erratum on the skip-loop half: a
  probe-counting harness showed the shipped suffix-advance already linear at 0.6×n on
  the finding's own adversarial shape, so no skip-loop change was needed) and **ROB-08**
  (a `FrameRateControl` change callback now re-applies `SDL_SetGPUSwapchainParameters`
  to the live swapchain through the new `gpu_scaler::ApplyFrameRateControl`; both
  `Reinit` claim sites share the same apply helper). **The 8A+8B first-contact bar is
  complete** — all nine Medium open at merge-plan r2 are closed; sweep 8C (ROB-07/23,
  TEST-06) remains. Between rounds 12 and 13 (un-numbered docs actions): the CI gtest
  split landed as a MAINT-15 addendum and the changelog gained a consolidated
  `1.6.0-dev-007f3956a` window entry. Ledger 38 → **36 open (0 High / 5 Medium /
  31 Low)**.
- **Round 14 (2026-10-17).** Closed the entire 8C runner sweep plus its two
  feature riders — **ROB-23** (uniform fill is type- and size-aware and the
  parameter lookup now runs before the `<TextureName>Size` branch, so
  parameter-driven packs like CRT masks get the user's values instead of
  silent zeros or float bits in int members), **TEST-06** (the smoke harness
  ports the runner's `#pragma name` pre-scan, byte-identical regex asserted,
  so cross-referencing packs predict in-game translation), **ROB-07**
  (`PassRuntime`/`LutRuntime` gained RAII destructors as the single release
  point for their GPU objects — mid-load failures clean up at scope exit,
  making the runner genuinely reusable), **FEAT-01** (re-selecting the same
  preset reloads the chain when the file's mtime/size changed on disk — pack
  iteration no longer detours through another preset) and **FEAT-03** (a
  failed preset load surfaces the first line of its error in the filter OSD,
  full text in the log). **The ledger is Medium-free** — 0 High / 0 Medium —
  and every Phase-0 sweep (8A/8B/8C) is complete. Ledger 36 → **31 open
  (0 High / 0 Medium / 31 Low)**.

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

## MAINT-03 — REOPENED (round 7, 2026-10-16): Four broken upstream workflows ship active; cleanup script never run

> **RE-CLOSED (round 7.5, 2026-10-16).** Round 7 revoked the closure below because the three
> workflow deletions it recorded never landed in any commit. The deletions have now actually
> been performed (git-tracked file removals) and tree-verified — see the addendum at the end
> of this section. The round-7 revoke text is preserved below for the record.
>
> **REOPENED (codebase-audit round 7).** The resolution below claimed `clang-tidy-check.yml`,
> `translations.yml`, and `cache-cleanup.yml` were deleted, with verification "exactly
> `clang-format-check.yml` and `gpu-shaders.yml` remain". That record does not match the
> shipped history: the three files were re-added by `8ebe60f39 combined to master`
> (2026-10-12) and never removed — they exist at every commit of the audit-fix lineage,
> still pinning `actions/checkout@v7`. The closure below is revoked; the finding is back in
> [audit.md](audit.md) as MAINT-03 (Medium). What did land and holds: the
> `clang-format-check.yml` fix, the cleanup-script removal, and dependabot curation.

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

**Re-close addendum (round 7.5, 2026-10-16).** The three deletions were performed on the
working tree as real file removals: `clang-tidy-check.yml`, `translations.yml`,
`cache-cleanup.yml` deleted. Tree verification this time is against the working tree that
ships: `ls .github/workflows` shows exactly `clang-format-check.yml`, `gpu-shaders.yml`, and
`wav-fallback-test.yml` — the third file is the WAV A/B diagnostic workflow added by
`dd3c3f14f` *after* the original round-1 closure, so its presence is by design and does not
contradict the curated set (the round-1 note's two-file list simply predates it).

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

---

## TEST-01 — CLOSED: Zero test coverage for the fork's ~15K-line GPU pipeline

| Property | Value |
|----------|-------|
| **Severity** | High |
| **Category** | Testing |
| **Original location** | `test/` (whole tree — zero references to gpu/shader), `Source/engine/render/gpu/` (all files), `Source/CMakeLists.txt` (`preset_smoke` target) |

**Original finding.** `grep -ri 'gpu\|shader' test/` returned nothing: the mod's parser,
translator, compiler bridge, and runner — code with 21 of the audit's findings against it —
had no automated tests. The codebase was otherwise test-friendly (60 gtest targets, headless
mode, golden-image text tests), and the mod even shipped a purpose-built offline harness,
`preset_smoke.cpp` (parse → translate → glslang-compile with cache disabled, exit 0 iff all
presets compile), but it was built `EXCLUDE_FROM_ALL` and never registered with ctest, so
nothing ever ran it.

**Resolution.** The audit's step (1): register the full-pipeline harness with ctest against
a committed micro-corpus.

- `preset_smoke` no longer builds `EXCLUDE_FROM_ALL` (`Source/CMakeLists.txt`): it is part
  of the normal test build and registered with ctest in `CMake/Tests.cmake`.
- Committed micro-corpus under `test/fixtures/preset_smoke/`:
  - `good/` — five presets that must parse, translate AND compile: a single pass with a
    `#pragma parameter` consumed from the fragment UBO, a 3-pass chain with alias feedback
    (`alias1 = blur` sampled from a later pass), explicit `PassOutput0` feedback,
    `OriginalHistory1/2` ring taps with `histories = 4`, and a LUT-texture preset
    (`textures = "lut"` + a committed 2x2 PNG). All shaders follow the canonical style of
    `tools/audit/field-presets/test/shaders` (explicit `layout(location = 0) out vec4
    FragColor`, parameters inside the stage UBO per SLANG_SPEC 4.6 — standalone uniforms
    are not rewritten and would fail glslang's Vulkan rules).
  - `malformed/` — five presets that must fail CLEANLY with a non-empty error string,
    never a crash: absurd declared count (`shaders = 2147483647`, ROB-02 class), absurd pass
    index (`shader2147483647 = ...`, ROB-02 class), an empty file, a missing shader file
    (fails at the translate stage), and a GLSL syntax error (fails at the glslang compile
    stage) — one failure per pipeline stage.
- The harness gained an `--expect-fail` mode: exit 0 only when every scanned preset fails
  with a non-empty error string (failure-by-crash or failure-without-explanation fails the
  run, and a clean preset under `--expect-fail` fails it too — both directions verified).
  The stale usage string (referencing a nonexistent `--allow-failures`, omitting
  `--dump`/`--disasm`) was corrected while touching the header.
- Corpus placement: `ResolvePresetFilePath` resolves references only inside the SEC-01
  shader root (`<SDL_GetBasePath()>/shaders`), so ctest copies the corpus to
  `<build>/shaders/.preset-smoke/` at build time (`preset_smoke_corpus` custom target); the
  dot prefix keeps the corpus out of the in-game preset browser (`ListPresetDir` hides
  hidden folders). No engine code was touched for this — the sandbox stays exactly as
  shipped.

**Verification.** Standalone local build of the harness + the four preset TUs + the real
glslang 16.6.0 (the exact version vendored by `3rdParty/glslang`; SDL stubbed, shader root
pointed at the corpus). Good corpus: **5/5 presets, 8/8 passes compiled, exit 0**. Malformed
corpus under `--expect-fail`: **met, 5/5 failed with named error strings** ("preset has no
shader passes", "exceeds the limit of 32", "cannot open shader file", glslang syntax error),
**zero crashes, exit 0**; the same good corpus under `--expect-fail` correctly exits 1.
`preset_smoke_corpus` and `preset_smoke_malformed` run under the fork's CI ctest invocation
whenever `USE_GPU_SCALER` and `BUILD_TESTING` are on. Broader coverage (ParsePreset /
TranslateSlang unit corpora over real packs, headless software-Vulkan runner tests) remains
backlog scope per the audit; `preset_failure_modes` (37 checks) continues to cover the
hostile-input layer.

**Fixup (r7.6.2, 2026-10-16, same day).** The ctest geometry above had a flaw the round-5
verification could not see (it ran the harness standalone with SDL stubbed and the shader
root pointed at the corpus): the `preset_smoke` executable itself landed at
`<build>/Source/preset_smoke` — `Source/CMakeLists.txt` never set a `RUNTIME_OUTPUT_DIRECTORY`,
unlike every other test executable — so on CI `SDL_GetBasePath()` (which follows the exe)
put the SEC-01 shader root at `<build>/Source/shaders`, and the corpus copied to
`<build>/shaders/.preset-smoke/` failed `IsPathInsideShaderRoot()`: all five good presets
died at translate ("cannot open shader file") and `preset_smoke_corpus` went red on its
first real CI execution, while `preset_smoke_malformed` stayed green (the `--expect-fail`
contract is "every preset fails with a non-empty error string" — still satisfied). Fix:
`preset_smoke` now gets `RUNTIME_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}` (same convention as
`preset_failure_modes_test` and the gtest targets), so the exe sits at the build root, the
shader root is `<build>/shaders`, and the corpus is inside it. Verified both sides at CI
parity: pre-fix baseline reproduced (good corpus 0 ok / 5 failed, exit 1; malformed met),
post-fix good corpus 5 ok / 8 passes / exit 0 with malformed still met, and the generated
`CTestTestfile.cmake` registers the test against `<build>/preset_smoke`. Corpus, fixtures,
harness, and the SEC-01 sandbox itself are untouched.


---

## ROB-14 — CLOSED: Scaler activation destroys the SDL_Renderer with renderer-owned textures still referenced — use-after-free on filter toggle

| Property | Value |
|----------|-------|
| **Severity** | High |
| **Category** | Robustness |
| **Original location** | `Source/utils/display.cpp` (lines 834-837, 715-718, 909), `Source/engine/dx.cpp` (lines 49, 147-154), `Source/utils/sdl_ptrs.h` (lines 57-64) |

**Original finding.** The `scalerWanted` branch in `ReinitializeRenderer` destroyed the
SDL_Renderer without resetting the global `texture` (`SDLTextureUniquePtr`, dx.cpp:49) or the
virtual-gamepad textures created from that renderer. In SDL3, `SDL_DestroyRenderer` destroys
every texture created from the renderer, so `texture` became a dangling unique_ptr; the next
`ReinitializeTexture()` (`if (texture) texture.reset()`) destroyed freed memory — reachable on
the scaler-failure fallthrough, on switching the filter Off, and on scale-quality changes. The
codebase's own `dx_cleanup` ordering (`texture = nullptr` before `SDL_DestroyRenderer`) is the
invariant that path skipped.

**Resolution.** `ReinitializeRenderer`'s `scalerWanted` branch now sets `texture = nullptr;`
immediately before `SDL_DestroyRenderer(renderer)`, mirroring `dx_cleanup`. The
virtual-gamepad-texture half is deliberately left to ROB-22: freeing them here without a
re-initialization path for the fallback renderer would trade the UAF for a visible regression
(missing gamepad UI) — that is ROB-22's fix to design, not this one-liner's.

**Verification.** Diff is 5 lines (comment + null assignment). All three reachability paths
now find `texture == nullptr` after the destroy: the `Reinit` failure fallthrough, the
filter-off path, and scale-quality changes. `clang-format 18.1.8` (the CI-pinned version)
reports the file clean.

---

## ROB-15 — CLOSED: `Runner::Run` returns the post-flip double-buffer slot for a double-buffered final pass — one-frame-stale presentation

| Property | Value |
|----------|-------|
| **Severity** | High |
| **Category** | Robustness |
| **Original location** | `Source/engine/render/gpu/preset/preset_runner.cpp` (lines 1263-1264, 1441-1450), `Source/engine/render/gpu/gpu_scaler.cpp` (lines 1097, 1125) |

**Original finding.** The per-pass loop rendered into `pass->outputTex[cur]` with `cur` read
pre-flip, but `*outTexture = last->outputTex[last->curBuffer]` executed *after* the
double-buffer flip loop — the other slot. For a feedback-final chain the returned texture held
the previous frame's render; on the first frame and after every `ReleaseTargets()` resize it
held never-rendered GPU memory. `PresentFrame` blits that texture to the swapchain in the same
submission, so presentation lagged one frame permanently plus one garbage frame per load/resize.

**Resolution.** The `*outTexture`/`*outWidth`/`*outHeight` capture (`passes_.back()`) moved to
before the flip loop — the same texture the loop just rendered into. The flip loop and the
history-slot advance are unchanged: next-frame feedback reads still see post-flip state, and
intra-frame cross-pass sampling still reads pre-flip slots.

**Verification.** Single-buffered finals behave byte-identically (the flip only touches
`doubleBuffered` passes, so pre- and post-flip `curBuffer` agree there). Feedback-final chains
now present the just-written target. `clang-format 18.1.8` clean.

---

## ROB-16 — CLOSED: Unchecked `SDL_MapGPUTransferBuffer` → `memcpy` to NULL, bypassing the failure contract

| Property | Value |
|----------|-------|
| **Severity** | Medium |
| **Category** | Robustness |
| **Original location** | `Source/engine/render/gpu/gpu_scaler.cpp` (lines 1056-1064) |

**Original finding.** `PresentFrame`'s frame upload mapped the transfer buffer with no NULL
check and `memcpy`ed into the result unconditionally; a device-loss or allocation failure
(map returns NULL) segfaulted instead of routing through the sticky-`gFailed` session fallback
that every neighboring failure uses.

**Resolution.** The map result is now NULL-checked; failure routes through
`LogFailHard("frame transfer buffer map failed")` + `SubmitSkipAfterAcquire(cmd)` + return —
the exact pattern of the adjacent transfer-buffer-creation failure. The RAII
`TransferBufferReleaser` still releases the buffer on the early return; `SDL_UnmapGPUTransferBuffer`
is only reached after a successful map, per the SDL contract.

**Verification.** Success path byte-identical; failure path now costs one skipped frame and a
descriptive log line, and the session downgrades to the stock renderer. `clang-format 18.1.8` clean.

---

## ROB-17 — CLOSED: `SDL_BeginGPUCopyPass`/`SDL_UploadToGPUTexture` results ignored — silent stale-frame presentation, uninstrumented

| Property | Value |
|----------|-------|
| **Severity** | Medium |
| **Category** | Robustness |
| **Original location** | `Source/engine/render/gpu/gpu_scaler.cpp` (lines 1066-1078) |

**Original finding.** `SDL_BeginGPUCopyPass(cmd)` was never null-checked and the bool returned
by `SDL_UploadToGPUTexture` was discarded. If either failed, the command buffer still submitted
and the blit pass sampled the *previous* contents of `gGameTex` — a frozen frame with a healthy
heartbeat, zero log lines, and no `gFailed`: precisely the silent-failure class the present-path
instrumentation exists to expose.

**Resolution.** `copyPass` is null-checked (failure: `LogFailHard("frame upload copy pass begin
failed")` + `SubmitSkipAfterAcquire`, honoring the post-acquire empty-submit contract). The
upload result is captured, `SDL_EndGPUCopyPass` runs unconditionally, and a failed upload routes
through `LogFailHard("frame texture upload failed")` + `SubmitSkipAfterAcquire`. Device loss is
the realistic cause of both, so hard-failing the session (stock-renderer fallback) matches how
the neighboring stages treat it.

**Verification.** Upload-stage failures now name themselves in gpu-scaler.log and take the
session fallback instead of presenting stale contents. `clang-format 18.1.8` clean.

**Fixup (r7.6, 2026-10-16, same day).** The upload-result half above does not compile against
the vendored SDL3 (master `570f7229`): SDL >= 3.2 changed the command-encoded copy calls to
return `void` — per-call failure reporting was removed upstream ("can't fail usefully"; SDL
logs the error and it surfaces at the checked `SDL_SubmitGPUCommandBuffer`). The
`const bool uploaded = ...` capture broke the CI build (`void value not ignored`,
gpu_scaler.cpp:1108). Reverted to a bare call (documented in-tree), which compiles against
both the old bool-returning preview signature and the current void one. The copy-pass-begin
null check is untouched — `SDL_BeginGPUCopyPass` still returns a pointer. The submit-time
check is the effective upload-failure net. Verified by compiling all four touched round-7.5
TUs against the vendored SDL3 with GCC 14.2 (CI parity).

---

## ROB-24 — CLOSED: Vulkan-retry claim failure leaves `gWindow` set → release of an unclaimed window

| Property | Value |
|----------|-------|
| **Severity** | Low |
| **Category** | Robustness |
| **Original location** | `Source/engine/render/gpu/gpu_scaler.cpp` (lines 817-818, 858-859, 871-872, 900) |

**Original finding.** The first window-claim site nulled `gWindow` on failure; the
Vulkan-retry path did not, so a failed retry claim fell through to `LogFailHard; Shutdown()`
with `gWindow` still set — and `Shutdown()` executed `SDL_ReleaseWindowFromGPUDevice` on a
window the freshly created device never claimed (unspecified SDL behavior; a debug assert
under `DEVILUTIONX_GPU_DEBUG=1`).

**Resolution.** An `else` branch on the retry claim now mirrors the first site:
`gWindow = nullptr;` before the shared `LogFailHard; Shutdown();` tail, so `Shutdown()` skips
the release.

**Verification.** Both claim sites now agree. On retry-claim failure `Shutdown()` tears down
the device without touching the window. `clang-format 18.1.8` clean.

---

## ROB-25 — CLOSED: Non-custom-filter skip is silent and uncounted

| Property | Value |
|----------|-------|
| **Severity** | Low |
| **Category** | Robustness |
| **Original location** | `Source/engine/render/gpu/gpu_scaler.cpp` (lines 1013-1024) |

**Original finding.** Every other skip class in `PresentFrame` is instrumented (swapchain skips
counted, dest-skips counted + throttled-logged) except the non-custom-filter branch, which did
`SubmitSkipAfterAcquire; return;` silently — a future filter-enum drift would present as a
permanent silent black screen with a healthy heartbeat.

**Resolution.** The branch now increments `sSkippedFrames` (feeds the existing heartbeat line)
and emits a throttled (5 s) log naming the unexpected filter value, using the same
`static last-tick` pattern as the dest-skip log.

**Verification.** Defense-in-depth instrumentation only; no behavior change on any reachable
current path (only a post-load ini edit can enter the branch). `clang-format 18.1.8` clean.

---

## MAINT-16 — CLOSED: Self-heal recompile does not repopulate the cache — comment claims it does

| Property | Value |
|----------|-------|
| **Severity** | Low |
| **Category** | Maintainability |
| **Original location** | `Source/engine/render/gpu/preset/preset_runner.cpp` (lines 408-422), `Source/engine/render/gpu/preset/slang_compile.cpp` (lines 250-254) |

**Original finding.** The ROB-03 self-heal comment claimed the retry "recompile[s] from source
once (cache is refreshed by that compile)", but the call passed `allowCache=false` and
`CompilePass` skips the cache store when `!allowCache` — so the healed session recompiled the
preset on every subsequent load until the next clean load repopulated the cache.

**Resolution.** The comment-first option from the audit's fix note: the self-heal comment now
states the actual contract — "the retry compiles fresh; the cache itself is repopulated by the
next clean load". Behavior is intentionally unchanged: keeping `allowCache=false` preserves the
heal's fresh-compile guarantee even in the corner where `DeleteCachedSpv` failed (e.g.
read-only cache dir) — flipping to `allowCache=true` there could re-hit the poisoned blob and
turn a load that succeeds today into a failure. The per-load glslang round-trip after a rare
self-heal is the cheaper risk.

**Verification.** Comment and behavior now agree; `slang_compile.cpp`'s own "skipped on the
ROB-03 no-cache retry" comment remains accurate. `clang-format 18.1.8` clean.

---

## TEST-07 — CLOSED: `wav-fallback-test.yml` paths filter omits the files round 6 actually modified

| Property | Value |
|----------|-------|
| **Severity** | Low |
| **Category** | Testing |
| **Original location** | `.github/workflows/wav-fallback-test.yml` (lines 36-44) |

**Original finding.** The WAV A/B workflow triggered on `soundsample.cpp`, `CMakeLists.txt`,
`CMake/Definitions.cmake` and itself, but round 6 (`b60356e1a`) extended the feature across
`Source/utils/soundsample.h` (the `SetChunk` signature), `Source/engine/sound.cpp` (the
`archivePath` plumbing), and `Source/mpq/mpq_reader.hpp` (the `path()` accessor) — changes to
those three files would silently skip the A/B gate.

**Resolution.** The three files were added to `push.paths`, directly below
`soundsample.cpp`.

**Verification.** YAML structure unchanged (same list, three new entries); the gate now
re-runs whenever any part of the WAV diagnostics surface changes.
---

## Round 7.6.1 — CI clang-format gate made green; round-7.5 verification record corrected (2026-10-16, no finding ID)

**Original finding.** The round-7.5 summary claimed all five touched C++ files
were clang-format-clean; that check ran with the wrong style resolution and was
wrong for `gpu_scaler.cpp`: the batch's python rewrite had left the file with a
whole-file indentation regression (LF line endings, so it slipped past the CRLF
tripwire that caught `display.cpp`/`brief.md`), shipping ~870 webkit-style
violations inside both the r7.5 and r7.6 overlays. A further 12 pre-existing
violations sat in `soundsample.cpp` (present since the wav-fallback commit).

**Resolution.** `clang-format -i` (18.1.8, `Source/.clang-format`) restored the
canonical formatting: `gpu_scaler.cpp` is byte-clean and its diff vs master
collapses from ~1,700 whitespace lines back to the surgical 38-line r7.5/6
change set, and the 12 `soundsample.cpp` violations are gone. No finding is
opened or closed by this entry — it corrects the round-7.5 verification record
and unblocks the CI clang-format gate (round-7.5's partial-fix cross-notes for
the still-open MAINT-09/MAINT-19 are unaffected).

**Verification.** Both files are token-identical to their pre-format state
(whitespace-only change, verified after stripping all whitespace), all touched
TUs recompile, and a full 698-file sweep of both CI check-paths (`Source/`,
`test/`) reports zero violations.

---

## ROB-18 — CLOSED: Unsupported auto-selected GPU backend hard-fails the session; the Vulkan retry never applies to it

| Property | Value |
|----------|-------|
| **Severity** | Medium |
| **Category** | Robustness |
| **Original location** | `Source/engine/render/gpu/gpu_scaler.cpp` (lines 760, 766-781, 848-870, 804) |

**Original finding.** Device creation requested SPIR-V and then validated the runtime
auto-selected driver against a whitelist accepting only `"vulkan"`/`"metal"`; anything else
took `LogFailHard` + `Shutdown()`, making `gFailed` sticky for the session. On Windows, SDL's
default selection order can yield `d3d12` (a SPIR-V-declaring backend per SDL), so the scaler
could never engage even when a working Vulkan driver existed — the only escape was the
`DEVILUTIONX_GPU_BACKEND=vulkan` env override. The retry machinery in `Reinit`, which
re-creates the device with an explicit backend name, was reachable only from
`EnsureFilterResources` failures, never from this driver-format rejection, and the failure was
double-logged (`LogFailHard` inside `CreateDevice` and again at the caller).

**Resolution.** `CreateDevice` now retries once with the explicit `"vulkan"` backend whenever
the whitelist rejects an *auto-selected* driver: the rejected device is destroyed, the
property set is rebuilt by a new `BuildDeviceProperties` helper (one shared implementation for
both attempts — debug mode, SPIR-V+MSL, verbose logging, forced name), and on success the
session continues on Vulkan with the SPIR-V shader format. An explicit `DEVILUTIONX_GPU_BACKEND`
value or a forced `name` is still honored as-is (no auto-retry — the override exists to
troubleshoot one specific backend, and silently retrying Vulkan would defeat it), with a log
line saying so. A belt-and-braces check destroys the device and reports failure if an
explicitly named backend ever came back as an unlisted driver. The double-log is gone:
`CreateDevice` no longer calls `LogFailHard`/`Shutdown` itself; only the caller records the
hard failure, once, with the full rejection/retry story in the diagnostic log lines above it.

**Verification.** Auto-select → `d3d12` → retry → `vulkan` now engages the scaler (the exact
Windows field scenario); auto-select → retry-fails and explicit-override paths still end in
one hard-fail record and the session fallback. Success-path behavior for
vulkan/metal-selected devices is byte-identical. `clang-format 18.1.8` clean; the TU
recompiles warning-free under the project's Release flags (GCC 14.2).

**Field verification (2026-10-16, round 12, build `c4b07281a`).** First in-the-wild exercise of the failure path this fix covers: a koko-aio-ng preset-load failure (missing `libjpeg-9.dll`, see the coverage CSV round-12 note) ran the full fallback ladder — `Shutdown` → `GPU scaler unavailable, using the regular renderer instead` → `Recreating the main window for the presentation path switch` — after which the session continued on the stock renderer with the OSD message "Shader Filter is off for this session". No fatal `ErrSdl`, no crash, window alive throughout. Fix behaves as designed.

---

## ROB-19 — CLOSED: `RecreateMainRenderWindow` failure is fatal on the fallback-of-the-fallback path

| Property | Value |
|----------|-------|
| **Severity** | Medium |
| **Category** | Robustness |
| **Original location** | `Source/utils/display.cpp` (lines 797-799, 842-843, 869-871), `Source/engine/dx.cpp` (lines 318-321) |

**Original finding.** `RecreateMainRenderWindow` destroyed the main window and then created
its replacement, ending with `if (ghMainWnd == nullptr) { ErrSdl(); return false; }` — and
`ErrSdl()` is fatal. This sat on the exact path meant to *save* a session: the GPU-failure
fallback in `RenderPresent` (`dx.cpp:318-321`) calls `ReinitializeRenderer`, whose
`!RecreateMainRenderWindow()` branches simply returned with both `ghMainWnd` and `renderer`
null — after which the next `RenderPresent` → `GetOutputSurface` →
`SDL_GetWindowSurface(nullptr)` raised a second fatal `ErrSdl`. A transient
`SDL_CreateWindow` failure during fallback therefore killed the process instead of degrading,
violating the game-never-dies contract at its deepest layer.

**Resolution.** `RecreateMainRenderWindow` now creates the replacement window *before*
destroying the old one. On create failure it logs the SDL error, clears it, keeps the old
window (and `ghMainWnd`) alive, and returns `false`; on success the old window is destroyed
and `ghMainWnd` reassigned. `ghMainWnd` can therefore never be null after the call, both
callers' `return` paths continue with a live window on the stock renderer path, and the
function no longer contains any fatal `ErrSdl` — the game only dies when it is left with no
window at all.

**Verification.** Failure path now degrades: window alive, `GetOutputSurface` serves the
window surface, `RenderPresent` continues on the non-scaler path. Success path is
order-swapped but otherwise identical (same title/size/flags restore, same position/grab/raise
behavior), and the presentation-path-switch rationale (clean HWND for the new swapchain owner)
is unchanged — the new window was never presented to. `clang-format 18.1.8` clean; TU
recompiles warning-free under the project's Release flags (GCC 14.2).

**Field regression addendum (2026-10-16, Shaders -> Off CTD).** The ROB-19 order swap
(create replacement *before* destroying the old window) was identified by field
bisection as a candidate cause of a crash-to-desktop on the Shaders -> 'Off'
presentation switch: d5ca0c0 (destroy-then-create) did not crash; b2eabc618+ (this
fix, create-then-destroy) did. Mechanism: creating the replacement *visible* while
the old window still owns the display/focus/mouse grab introduces a
two-visible-windows moment the original order never had — and on the 'Off' path this
lands microseconds after `SDL_ReleaseWindowFromGPUDevice` + `SDL_DestroyGPUDevice`,
while the driver is still tearing down the old window's swapchain (on Windows, DXGI
adapter state). The to-shader switch runs the same recreate but from a
renderer-owned window with no GPU device alive, which is consistent with it surviving
in the field. Resolution: the replacement is now created `SDL_WINDOW_HIDDEN`,
the old window is destroyed, and only then is the replacement shown/raised — one
visible transition (the battle-tested destroy-then-create semantics) while keeping
ROB-19's no-window-loss guarantee (a failed create still keeps the old window and
returns false, never fatal). Note the second in-window change on this path, ROB-22's
`InitVirtualGamepadGFX`/`InitVirtualGamepadTextures` after the fresh renderer create,
was audited line-by-line for this report (LoadArt/createTextures are re-callable and
null-safe) and stays as-is; if a field crash ever recurs on this path, revert that
pair next.

---

## ROB-22 — CLOSED: Virtual-gamepad textures dangle across scaler activation; fallback renderer never re-initializes them

| Property | Value |
|----------|-------|
| **Severity** | Medium |
| **Category** | Robustness |
| **Original location** | `Source/utils/display.cpp` (lines 834-837), `Source/engine/dx.cpp` (lines 318-321, 341), `Source/controls/touch/renderers.cpp` (`InitVirtualGamepadTextures`/`FreeVirtualGamepadTextures`), `Source/controls/touch/renderers.h` (lines 74-77) |

**Original finding.** `InitVirtualGamepadTextures(*renderer)` (gamemenu.cpp:316,
interfac.cpp:565) filled each `ButtonTexture::texture` created from the live SDL_Renderer.
When the scaler-activation path destroyed that renderer (the site the round-7.5 ROB-14 fix
touched), those handles dangled: the destroy path called neither `FreeVirtualGamepadTextures()`
nor re-created them afterwards, so (a) the next `FreeVirtualGamepadTextures()` ran
`SDL_DestroyTexture` on freed memory, and (b) after the `dx.cpp:318-321` GPU-failure fallback
created a fresh renderer, `RenderVirtualGamepad` (`dx.cpp:341`) operated on dangling non-null
pointers — a use-after-free on touch builds, with a fatal `ErrSdl` dialog as the softer
failure mode.

**Resolution.** Both halves ship together, as the ROB-14 closure anticipated. (1) The
scaler-activation branch now calls `FreeVirtualGamepadTextures()` immediately before
`SDL_DestroyRenderer(renderer)` — mirroring `dx_cleanup`'s ordering and the ROB-14 `texture`
handling — so the handles are destroyed while their renderer is still alive. (2) The
renderer-creation site in `ReinitializeRenderer` now re-runs `InitVirtualGamepadGFX()` +
`InitVirtualGamepadTextures(*renderer)` after every fresh renderer: the art reload covers the
common in-game case where `createTextures` already consumed the source surfaces (surviving
only-null textures would have traded the UAF for a permanently missing gamepad), and the
extra calls are no-ops in the steady state because `createTextures` only acts on surfaces
still held. Boot-time behavior is unchanged: with assets not yet mounted both calls fail
quietly to null and the normal `LoadAllGFX` → `ShowProgress`/`ProgressEventHandler` cycle
proceeds as before.

**Verification.** The two failure modes are closed: no `SDL_DestroyTexture` on freed memory
(textures freed pre-destroy, subsequent frees are null no-ops), and `RenderVirtualGamepad`
after any fallback renders against textures created from the *current* renderer — with the
gamepad UI restored instead of dropped. Desktop CI matrix is unaffected (the virtual gamepad
is never created there; the calls compile in for all non-SDL1 builds, matching `dx.cpp`'s
existing `FreeVirtualGamepadTextures()` usage). `clang-format 18.1.8` clean; both touched TUs
recompile warning-free under the project's Release flags (GCC 14.2).

---

## ROB-20 — CLOSED: Unbounded preset/shader file slurp → uncaught `bad_alloc` terminates the process (no top-level guard)

| Property | Value |
|----------|-------|
| **Severity** | Medium |
| **Category** | Robustness |
| **Original location** | `Source/engine/render/gpu/preset/preset_parser.cpp` (lines 304-310), `Source/engine/render/gpu/preset/slang_translate.cpp` (lines 123-125, 165-168, 1080-1091, 1170, 1173) |

**Original finding.** Both untrusted-input readers slurped entire files into memory with no size cap: the `.slangp` reader
(`std::ostringstream ss; ss << stream.rdbuf();` plus a second full copy via `ss.str()` and per-line copies), and the `.slang`
reader for the source *and every include*, with the merged text allowed to reach ~3× the include set (per-bucket
include-once). A multi-gigabyte planted file made the allocation throw `std::bad_alloc`/`std::length_error`, and no
try/catch existed anywhere upstream on the load path (verified: `main.cpp`, `scrollrt.cpp`, `gpu_scaler.cpp`,
`preset_runner.cpp`, `slang_compile.cpp`) — so the process terminated instead of returning the documented error contract.
This was the ROB-02 class (uncaught allocation death from preset content) with a different trigger: ROB-02's
`shaders = 2147483647` needed 30 bytes; this needed a large file on disk, which is why it landed at Medium. Same class:
`out.sourceSize = static_cast<int>(fileBytes.size())` overflows `int` past 2 GiB (`slang_translate.cpp:1170, :1173`).

**Resolution.** Every untrusted read is now capped *before* the slurp: `ParsePresetImpl` refuses presets above 8 MiB
("preset file too large: <path> (<N> bytes; limit is 8 MiB)"), `ResolveIncludes` refuses the top-level shader and every
include above 4 MiB ("slang: shader file too large: <path> (limit is 4 MiB)"), and the diagnostics fingerprint reader
`ReadWholeFileBytes` applies the same shader cap — oversize files simply skip the fingerprint (`sourceSize`/`sourceHash`
stay at defaults), which is all that field diagnostic ever needed. The caps sit orders of magnitude above the real corpus
(presets are key/value text; the largest known `.slang` is ~130 KiB), so no legitimate pack changes behavior. The
int-cast class is closed structurally: both diagnostics casts (`sourceSize`, `mergedSize`) now go through a clamping
`DiagSizeToInt` helper, so even a hypothetical oversized merged text cannot overflow. And the missing top-level guard now
exists: `ParsePreset` and `TranslateSlang` wrap their bodies in a catch-all (the translate body moved to an internal
`TranslateSlangImpl` inside the same TU) that converts any escaping exception — `bad_alloc` included — into the
documented failure contract: `false` plus a named error ("preset load failed for <path>: …", "slang: translation failed
for <path>: …"), with `out` left in the same state the early-failure paths leave it. Unseekable streams are refused with
their own message (both readers require regular files).

**Verification.** `preset_failure_modes` gained two checks (37 → 41): a 8 MiB+1 preset fails `ParsePreset` with "preset
file too large" and a 4 MiB+1 shader fails `TranslateSlang` with "slang: shader file too large" — both named-error
contracts assert the full message; the suite runs 41 checks / 0 failures headless (GCC 14.2). The full smoke corpus
translates and compiles clean under the caps (6 ok / 9 passes / exit 0). All touched TUs are clean under `clang-format 18.1.8`;
recompile warning-free under the project's Release flags (GCC 14.2).


---

## ROB-21 — CLOSED: Duplicate sampler declarations get duplicate dense bindings (include-once is per-bucket only)

| Property | Value |
|----------|-------|
| **Severity** | Medium |
| **Category** | Robustness |
| **Original location** | `Source/engine/render/gpu/preset/slang_translate.cpp` (lines 896-902, 949-951, 96-97, 165-168, 1212-1218), `Source/engine/render/gpu/preset/preset_runner.cpp` (line 387) |

**Original finding.** `RewriteSamplers` unconditionally emitted `layout(set = N, binding = M) uniform … sampler2D <name>;`
and appended `{name, binding}` for every declaration found; the only name-dedupe was the macro-alias branch
(`alreadyEmitted`, `:896-902`). Because include-once is tracked *per bucket* (shared/vertex/fragment), a file included
once in the shared region and again in the fragment region pasted its `#pragma stage fragment`-guarded sampler
declaration into the fragment TU twice (`split.shared` + `split.fragment`, `:1212-1218`) — valid input, two declarations
of the same sampler with bindings B and B+1. Consequences: glslang "redefinition" compile failure with misleading
diagnostics, and `stage.textures` double-counting so `num_samplers = stage.textures.size()` (`preset_runner.cpp:387`)
reserved two sampler slots for one texture. The same degradation occurred when `weakly_canonical` errored and the
visited-key fell back to the uncanonicalized string (`:164-167`).

**Resolution.** `RewriteSamplers` now name-dedupes the plain rewrite path immediately before the emit: if the resolved
name is already in `stage.textures`, the repeat is dropped (first binding wins — the earlier declaration is visible
everywhere below it in the same TU) and a warning is recorded ("sampler '<name>' is declared more than once in the stage
(a stage-guarded header was re-included); keeping the first binding"). The alias-branch dedupe (`alreadyEmitted`) is
unchanged and the two compose: a macro-alias resolution landing on an already-bound name still takes the alias branch's
path, and every other validation (unknown-texture tolerance, PassOutput/PassFeedback causality) runs as before — the
dedupe only intercepts the final emit. Exactly the per-bucket include-once contract the design intends: re-inclusion
across regions keeps the *code*, but a sampler binds once per stage.

**Verification.** New smoke-corpus entry `dupinclude.slangp` (+ `shaders/dupinclude.slang` +
`shaders/guarded_texture_header.slang`): the shader re-includes a fragment-guarded header once in the shared region and
once in the fragment region — on the old code the fragment TU carried two dense bindings for `dup` and glslang failed the
compile ("redefinition"); now the preset translates with the dedupe warning, exactly one
`layout(set = 2, binding = 0) uniform sampler2D dup;` in the fragment GLSL, `stage.textures.size() == 1`, and the full
in-process glslang compile is clean (translate-level probe asserted 1 declaration / binding 0 / textures.size 1 / warning
present; smoke run: 6 ok / 9 passes / exit 0). `clang-format 18.1.8` clean; TU recompiles warning-free under the project's Release
flags (GCC 14.2).

---

## TEST-08 — OPEN: Successful file-backed texture (LUT) loads are never logged; the field-log digest's PNG-texture counter can never fire

| Property | Value |
|----------|-------|
| **Severity** | Low |
| **Category** | Testing |
| **Original location** | `Source/engine/render/gpu/preset/preset_runner.cpp` (`Runner::LoadLut` 901-991, diagnostics loop 586-605) + `tools/audit/field_log_digest.py` |

**Original finding (round 14, 2026-10-16 — user observation on a gameboy-player
raw excerpt, confirmed by code read + upstream sources).** The pass log line's
`textures=[...]` field is the post-translation fragment-shader *sampler-name*
list (`preset_runner.cpp:586-605` — comment: "sampler names surface any
unresolved texture references"), not a record of file-backed texture loads:
`Source`/`Original` are pass-chain builtins, and a name like `BORDER` is the
preset-declared texture. Upstream
`border/gameboy-player/gameboy-player-crt-easymode.slangp` declares
`textures = "BORDER"` / `BORDER = "gameboy-player.png"` — a real PNG that
`LoadLut` resolves and loads via `IMG_Load` at preset-load time — yet nothing
in the log ever names that file.

Engine side: the only log line on the texture path is the failure string
(`IMG_Load failed: ...`, `preset_runner.cpp:911` — exactly how the round-12
`libjpeg-9.dll` gap surfaced). The success path (resolve → `IMG_Load` →
convert → create → upload, lines 909-990) returns silently. Consequences:
(a) a successfully loaded JPEG/PNG LUT leaves no positive trace, so round-13's
"all 6 bezel textures load (PNG+JPG)" was an *inference* (preset loaded ok +
zero IMG_Load errors), not an observation; (b) the audit tooling reads the
same logs — `field_log_digest.py` populates its `pngs` set only from `.png`
substrings inside cache-hit/pass lines (regexes at lines 44/62, adds at
163/173), and engine log lines never contain texture file paths (cache lines
carry shader paths, pass lines carry sampler names), so `PNG textures: none`
is a constant and the "PNG LUTs loaded:" notables branch (lines 282-283) can
never fire. Note the digest's pass-line branch would even attribute a
hypothetical `.png` match to the *shader* path basename (`m.group(2)`).

**Proposed fix (2 small changes; not applied code-side per convention).**
Engine, in `LoadLut` before `return true`:
`SDL_LogInfo(SDL_LOG_CATEGORY_APPLICATION, "GPU scaler preset: texture '%s' = %s (%ux%u)", lut.def.name.c_str(), path.c_str(), lut.width, lut.height);`
Digest: add
`RE_TEXLOAD = re.compile(r"GPU scaler preset: texture '([^']+)' = (\S+) \((\d+)x(\d+)")`,
feed the basename (extension-aware) into the texture set on match, and report
PNG/JPG separately in the summary line. Until the engine line ships, treat the
digest's `PNG textures:` field as evidence of nothing — the round-14 ledger
notes deliberately avoid citing it.

## ROB-26 — CLOSED: Synchronous GPU-device teardown inside the Shaders→Off switch — the hand-back stack never worked on Windows (field bisection false negative)

| Property | Value |
|----------|-------|
| **Severity** | High |
| **Category** | Robustness / field regression |
| **Original location** | `Source/utils/display.cpp` (`ReinitializeRenderer` hand-back branch), `Source/engine/render/gpu/gpu_scaler.cpp` (`Shutdown`), `Source/engine/render/gpu/gpu_scaler.hpp` |

**ID note (round-10 re-head, 2026-10-17).** This record keeps **ROB-26**, matching the
code comments shipped at 69be0cad5; the open Low finding that previously shared the
number in audit.md (hard swapchain-acquire failure vs a minimized-window skip) was
renumbered **ROB-30** in the same pass.

**Field evidence re-interpretation (2026-10-17, after cf4bbd519 still CTD'd).** The
user's bisection (d5ca0c0 good → b2eabc618 bad) was treated as a code regression in
the unload path, and ROB-19's hidden-create window transition was shipped as the fix.
cf4bbd519 (which contains it) still CTDs, which re-opened the window and exposed the
real mechanism: SDL3-GPU's backend priority is **D3D12 before Vulkan on Windows**
(`SDL_gpu.c` `backends[]`: METAL → D3D12 → VULKAN). At d5ca0c0 the whitelist in
`CreateDevice` accepted only `{vulkan, metal}`, so a Windows box auto-selecting d3d12
hit `LogFailHard("unsupported GPU driver: d3d12")` and **the scaler never activated at
all** — shaders could not render, and "switching Shaders to Off" exercised nothing but
the renderer-only path. d5ca0c0's "good" was a **false negative**: the hand-back stack
(synchronous `SDL_DestroyGPUDevice` → window recreation → D3D11 reclaim, all inside the
settings-switch call frame) had never once executed on that machine. ROB-18's
forced-Vulkan retry (b2eabc618) made the scaler work there for the first time, and the
'Off' switch immediately started crashing — not because the audit broke the switch,
but because the switch's device-teardown half became reachable. This is consistent
with the ROB-19 fix being correct for its own finding yet insufficient for the field
crash, and with the unload-only-when-loaded symptom.

**Mechanism.** On 'Off' with a shader active, `ReinitializeRenderer` ran
`gpu_scaler::Shutdown()` synchronously: `SDL_ReleaseWindowFromGPUDevice` +
`SDL_DestroyGPUDevice` (full Vulkan swapchain/device teardown) microseconds before
`RecreateMainRenderWindow` (window destroy/create) and `SDL_CreateRenderer` (D3D11
reclaim of a fresh HWND). Whatever the exact faulting frame inside that driver-churn
stack, it is SDL/driver-internal interplay we do not control; the contract requires
the switch to degrade gracefully, not to bet on it.

**Resolution (deferred device teardown).** New `gpu_scaler::HandbackWindow(window)`
does everything `Shutdown()` does **except** `SDL_DestroyGPUDevice`: releases frame
and filter resources, unloads the preset runner, releases the samplers, releases the
window claim explicitly **while the window is still alive** (SDL_gpu has no
window-destroy auto-release hook), nulls `gWindow`/`gFilter`, and leaves
`gLastClaimedWindowId` intact (the hand-back flow still needs
`WindowHasSwapchainHistory` to recreate the DXGI-poisoned window). `IsActive()` is
false immediately (`gWindow == nullptr && gFilter == None`), so every game path falls
back to the regular renderer at once. The parked, resource-less device is destroyed by
the next `Shutdown()` (quit / `dx_cleanup` — both call sites verified) or transparently
**reused** by the next `Reinit()` (re-selecting a shader re-claims a fresh window on
it and reloads filter resources — the exact Load→Unload→reload lifecycle
`preset_unload_test` round 2 exercises). `ReinitializeRenderer` now calls
`HandbackWindow(ghMainWnd)` in the hand-back branch instead of `Shutdown()`. The
failure-path `Shutdown()` calls inside `Reinit()` are unchanged: they only run on
filter-resource failures, not on the user-facing switch.

**Cost / residual risk.** While parked, the process holds one idle GPU device with no
swapchain, pipelines, targets, or sampler state — a few MB at most, freed at quit or
recycled on the next shader selection. If the crash turns out to live inside
`SDL_ReleaseWindowFromGPUDevice` itself (the only GPU SDL call remaining on the switch
stack), the log now brackets it exactly ("HandbackWindow: ..." line); the next
diagnostic step would be releasing the claim from a deferred point as well.

**Verification.** Static: parked state satisfies `IsActive()==false`,
`Shutdown()` handles it via its existing null-guards, `Reinit()` re-claims on the
parked device without `CreateDevice`, and the hand-back window recreation gate
(`WindowHasSwapchainHistory`) is unaffected. Field: awaiting user CI build — the
switch should now log `HandbackWindow: ...` → `Recreating the main window ...` and
continue with a working D3D11 renderer; `gpu-scaler.log` will name the failing frame
if any remains.

---

## ROB-27 - Shaders -> Off in the main menu: null item-cursor CLX read in LoadPotionArt (field CTD at 69be0cad5)

**Symptom.** Build 69be0cad5 (ROB-26 deferred teardown included): loading a preset at
startup, then switching Shaders to Off in the main menu still kills the process. The
diagnostic log ends after `Created renderer: direct3d11`; the `osd: (hidden)` line
that `ShowFilterOsd()` must emit for this switch is absent, pinning the crash to
`ReinitializeRenderer`'s regular-renderer branch between renderer creation and the
OSD update.

**Root cause.** That branch calls `InitVirtualGamepadGFX()` (ROB-22 pair) ->
`VirtualGamepadRenderer::LoadArt()` -> `LoadPotionArt()` ->
`GetInvItemSize(CURSOR_FIRSTITEM + ICURS_POTION_OF_HEALING)` -> `GetInvItemSprite()` ->
`pCursCels->numSprites()`. `pCursCels` (data\\inv\\objcurs) is loaded by `InitCursor()`
in `StartGame()` - i.e. only when a game session starts - and freed by `FreeCursor()`
in `FreeGame()`. In the main menu it is empty and the read is `LoadLE32(nullptr)`:
an instant, silent access violation (Release build, asserts compiled out). The
engine's own `InitVirtualGamepadGFX()` call sites (`LoadAllGFX`, `LoadGameLevel*`)
all run in-game, after `InitCursor()`; the hand-back branch is the only one that
runs in menu context, and the shader options are CantChangeInGame - so the menu is
the ONLY place this path executes. Sessions where the scaler never activated never
reach it (the startup renderer exists, so the branch is skipped), which is why the
field crash only appears after a scaler-active session. The to-shader direction
returns earlier and never touches this code.

**Fix.** Two guards:
- `Source/cursor.cpp`, `GetInvItemSize()`: return `{0, 0}` when `pCursCels` is not
  loaded. Zero risk to gameplay (in-game the art is always loaded); every caller
  either skips on a zero size or is in-game-only.
- `Source/controls/touch/renderers.cpp`, `LoadPotionArt()`: skip building the potion
  strip when the reported size is zero. In menus the strip is never rendered
  (`ControlMode == VirtualGamepad` is required), and the game-load path (`LoadAllGFX`)
  re-initializes the gamepad art with the item cursors available, restoring the strip
  exactly where the engine always built it.

**Verification.** `preset_unload_test` gains round 4: menu-context
`InitVirtualGamepadGFX()` / `FreeVirtualGamepadGFX()` with `pCursCels` empty - the
exact call sequence of the crashing branch. Before the fix it kills the process
(caught by the CI coverage guard); after the fix it passes asset-free (LoadPNG
misses are null-safe, the potion strip is skipped on the zero size).

---

## MAINT-15 — CLOSED: CI compiles baked shaders with unpinned apt `glslang-tools`; baked SPV is non-reproducible

| Property | Value |
|----------|-------|
| **Severity** | Medium |
| **Category** | Maintainability |
| **Original location** | `.github/workflows/gpu-shaders.yml` (lines 116, 241, 295), `CMakeLists.txt` (lines 137-151) |

**Original finding.** The Linux (gcc:14-bookworm container) and Windows (ubuntu-24.04
MinGW cross host) CI jobs installed `glslang-tools` from apt with no version pin;
`find_program(GLSLANG_VALIDATOR …)` picked it up and the `gpu_scaler_spv` target
regenerated the shipped `fullscreen.vert`/`blit.frag` blobs with whatever glslang the
runner image carried that day — Debian 12's glslang 12.x for the Linux binary,
Ubuntu 24.04's glslang 14.x for the Windows binary, so the two shipped executables
could embed different SPIR-V for the same shaders. Shipped bytes were a function of
the runner image, and an apt glslang regression could change or break the baked
shaders with nothing to bisect. Distinct from TEST-04 (drift detection): this was the
*source* of drift.

**Resolution.** New CMake option `GPU_SCALER_SPV_PREBUILT` (default OFF): when ON,
configure skips the glslangValidator probe and never creates the `gpu_scaler_spv`
target, and the scaler embeds the committed `gpu_shader_spv_prebuilt.hpp` (the same
`#else` path any glslang-less local build already took). Both CI jobs now configure
with `-DGPU_SCALER_SPV_PREBUILT=ON`; the Windows job drops its `glslang-tools` install
entirely, and the Linux job keeps the package purely as the Package step's
source-health canary and the `spirv-blobs` release artifact compiler, with the step
comments now stating that its output is informational and not what ships. Local
development is unaffected: leave the option OFF and the build-time regeneration works
as before.

**Verification.** Static: the prebuilt path is the same code path every glslang-less
build already exercised (`gpu_scaler.cpp` `#ifdef GPU_SCALER_SPV_GENERATED` /
`#else` committed header), and `Source/CMakeLists.txt` only adds the
`GPU_SCALER_SPV_GENERATED` define plus the target dependency when the variable is
truthy, so the OFF branch is the long-standing fallback. CI: the Linux job (build +
gtest suite under Xvfb/lavapipe) and the Windows job (static zero-DLL link +
portability gates) must both stay green with the option ON; shipped SPIR-V bytes
become the committed header's bytes on every platform.

**Addendum (CI job split, 2026-10-17).** The gtest suite moved out of the Linux build job
into a dedicated `linux-tests` job — the Linux job ran the full vendored-dep build and
the headless-GPU test run serially and clocked ~5x the Windows job's wall time. CI is now
three jobs: `linux` (binary, `-DBUILD_TESTING=OFF`), `linux-tests` (suite under
Xvfb/lavapipe), and `windows` — all three still configure with
`-DGPU_SCALER_SPV_PREBUILT=ON`, and only the Linux build job keeps `glslang-tools`
(Package-step canary). The CI sentence in **Verification** above describes the two-job
layout this addendum supersedes.

---

## FEAT-02 — CLOSED: Resolve the Metal strategy: wire the MSL path or fail fast with a clear message

| Property | Value |
|----------|-------|
| **Severity** | Medium |
| **Category** | New Feature |
| **Original location** | `Source/engine/render/gpu/gpu_scaler.cpp` (`BuildDeviceProperties`, `CreateDevice`, `CreateShader`), `Source/engine/render/gpu/gpu_shaders_msl.hpp`, `docs/diablo-handoff.md` §4 |

**Original finding.** The device layer accepted Metal (`CreateDevice` set both
`SHADERS_SPIRV` and `SHADERS_MSL`; an auto-selected `metal` driver selected the MSL
shader format), and the repo carried 437 lines of hand-ported MSL for the shared
Vertex/Blit programs — but `Runner::Load` hard-rejects every non-Vulkan backend
("runtime shader presets are not supported on the metal backend"). Net effect on
macOS: every shader filter choice deterministically failed into the generic "custom
preset failed to load" path, which neither explained the platform limitation nor
offered a toggle, while the handoff §4 table separately claimed SDL transpiles SPIR-V
to MSL at runtime and marked the area "untested" — the doc and the code disagreed.

**Resolution.** Option (a) of the finding's two coherent resolutions: the device
layer is Vulkan-only until a Metal path is actually validated. `BuildDeviceProperties`
no longer sets `SDL_PROP_GPU_DEVICE_CREATE_SHADERS_MSL_BOOLEAN`; the
`metal` → `SDL_GPU_SHADERFORMAT_MSL` branches in `CreateDevice`'s driver
classification and in its retry tail are gone (an explicitly forced Metal backend now
dies in the belt-and-braces branch); `CreateShader` is SPIR-V-only (entrypoint always
"main"; the MSL case and the `MslFor`/`MslEntryFor` helpers removed); the
`gpu_shaders_msl.hpp` include is dropped and the header itself carries an "unwired"
banner for a future validated path. Auto-selected Metal gets an honest, specific line
in the diagnostic log — "GPU shader mod requires Vulkan; Metal is not yet
supported" — and the ROB-18 forced-Vulkan retry still runs, so MoltenVK users keep
the scaler; without Vulkan the session reverts to the stock renderer per the standing
failure contract (never `app_fatal`). The runner-level non-Vulkan rejection remains
as belt and braces, and the handoff §4 row now states the disposition instead of the
"untested transpilation" claim.

**Verification.** Static: the only remaining `metal` references in `gpu_scaler.cpp`
are the honest-message branch and the display-name mapping; `gpu::msl::` has no
referents left outside the (now unincluded) header; `clang-format 18.1.8` (the CI
pin) passes both touched TUs with tabs and LF byte-intact. The macOS path cannot be
exercised in CI (Linux + Windows jobs only); expected field behavior is the new log
line followed by either `device created, backend=vulkan` (MoltenVK) or a clean
stock-renderer fallback — the same session-sticky `gFailed` contract every other
device-creation failure uses.

---

## PERF-06 — CLOSED: Quadratic scans in the translator: UBO skip-loop and per-declaration full-text rescans

| Property | Value |
|----------|-------|
| **Severity** | Medium |
| **Category** | Performance |
| **Original location** | `Source/engine/render/gpu/preset/slang_translate.cpp` (`FindUniformBlock`; `RewriteSamplers` + `IsDefineTarget` / `IsTextureSampled` / `IsSamplerNameUsed` / `DefineTargetExpansion` / `IsSamplerDeclared`) |

**Original finding.** Two scan patterns degraded quadratically with large or adversarial
shader text, both running on the frame path during preset load with no timeout. First,
`FindUniformBlock`'s skip of `layout(push_constant) uniform X {` hits was read as
re-probing every position to EOF per skipped block — K push-constant blocks in an n-byte
shader estimated at O(K·n) `std::regex` position probes per stage (~10¹⁰ for a 1 MB
shader padded with ~30k such blocks). Second, `RewriteSamplers` called `IsDefineTarget`
and `IsTextureSampled` — full O(n) walks of the entire stage text — once per unknown
sampler declaration, so N orphan declarations (Mega Bezel wrapper packs legitimately
carry many) cost O(N·n), with `IsSamplerNameUsed`/`DefineTargetExpansion` adding more in
the #defined-away path. Field impact: seconds-to-minutes stalls loading big wrapper packs
on cold cache. (All 8 translator regexes had been checked for catastrophic backtracking —
none; the cost was linear-scan multiplication, not blowup.)

**Resolution.** One-pass precomputation — the finding's primary prescription. A new
`SamplerScanIndex` (`BuildSamplerScanIndex`, two linear passes over the comment-stripped
stage text) is built once at the top of `RewriteSamplers` (the text is not mutated inside
the rewrite loop, so the index stays valid) and answers the five per-declaration queries
by lookup: `defineTargets`/`defineExpansion` record every `#define` directive target
(with the object-like single-identifier body) walking the keyword occurrences with the
exact branch logic of the originals; `samplerDecls` records every `sampler2D <name>`
declaration; `usedNames`/`sampledNames`/`textureSuffixNames` classify every identifier
token exactly as `IsSamplerNameUsed`/`IsTextureSampled` did (sampling-call arguments,
directive classification, and the attached-prefix mid-token branch for names that are
suffixes of texture/texelFetch-prefixed tokens). Five thin 3-arg wrappers keep the call
sites' names and dispatch on identifier shape — everything the sampler declaration
grammar yields in practice — falling back to the original full-text scans for
non-identifier names (invalid GLSL that glslang rejects later) for exact parity. The
call-site diff is five argument lists.

**Erratum (pattern 1, measured).** A probe-counting harness replicating the shipped
skip-loop put the audit's own adversarial shape — 1 MB, 30,000 push-constant blocks, one
plain UBO at the end — at **30,001 `regex_search` calls and 1,710,001 probes = 0.6×n
(66 ms)**, not the estimated ~10¹⁰: the shipped `searchFrom = match.suffix().first`
advance already bounds each rescan to the gap before the next head, so the loop was
linear and needed no change. The finding's O(K·n) arithmetic presumed each
`regex_search` re-probed from `from` to EOF; it does not. The field stalls were the
second pattern alone.

**Verification.** Parity harness over the exact shipped block (g++ -O2, clang-format
18.1.8-clean): **29,590 old-vs-new cross-checks, zero divergences**, across a
branch-torture text (mid-line `#define`, function-like and empty macros,
`sampler2DArray`/`isampler2D` skips, call/whitespace/member-access variants), the full
32-file `preset_smoke` corpus plus the vulkan `.vert`/`.frag` sources, and a 200 KB
synthetic wrapper pack — querying every identifier token plus every
texture/texelFetch-token suffix. Timing: 400 orphans on a 195 KB stage = 3.3 ms old vs
1.7 ms new (index build included); the old side scales O(N·n) while the index is O(n) —
a multi-megabyte wrapper TU (the ROB-20 caps allow 4 MiB) with a thousand orphans is
where the field's seconds-to-minutes stalls came from. CI: the corpus suite
(`preset_smoke` / `preset_smoke_corpus`) and the clang-format gate must stay green.

---

## ROB-08 — CLOSED: Mid-session vsync changes never reach the GPU swapchain

| Property | Value |
|----------|-------|
| **Severity** | Medium |
| **Category** | Robustness |
| **Original location** | `Source/engine/render/gpu/gpu_scaler.cpp` (the two `SDL_SetGPUSwapchainParameters` call sites in `Reinit`), `Source/options.cpp` (`FrameRateControl`), `Source/utils/display.cpp` (option change handlers; renderer-path vsync) |

**Original finding.** `SDL_SetGPUSwapchainParameters` — the only way to apply a new
present mode to a live swapchain — was called solely inside `Reinit`'s window-claim
branch and the Vulkan-retry path. The `FrameRateControl` option (Vertical Sync) carries
`RecreateUI` but had no change callback (callbacks were registered only for
resolution/fullscreen/shaderFilter), so toggling vsync while the scaler was active
changed nothing until restart or filter change. The SDL_Renderer path honored the change
on every `ReinitializeRenderer` (`SDL_SetRenderVSync`), so the same toggle behaved
differently per render path — a "setting did nothing" support class.

**Resolution.** The audit's prescription, mirroring the shaderFilter callback pattern.
The duplicated 4-line apply block in both `Reinit` claim sites is factored into a shared
`ApplySwapchainPresentMode()` (same SDR composition and VSYNC/IMMEDIATE parameters, same
non-fatal `LogFail` on failure — the swapchain keeps its previous present mode); a new
public `gpu_scaler::ApplyFrameRateControl()` no-ops unless `IsActive()` (the scaler must
own the present) and re-applies with a `gpu-scaler.log` line naming the new state; and
`display.cpp` registers the option's change callback beside the existing three handlers
(`OptionChangeHandlerFrameRateControl`). Success-path behavior of the two refactored
call sites is byte-identical — same parameters, same combined claim log at the first
site (via the helper's returned vsync flag), same ordering after the claim.

**Verification.** Static: both former inline blocks now route through the shared helper;
the callback fires after the option value commits, so the apply reads the new setting;
`RecreateUI` still covers the SDL_Renderer path untouched; the `IsActive()` guard makes
handback and parked-device states no-ops. Failure contract: a rejected present mode logs
and continues — never fatal. CI: the gtest suite (`linux-tests` job) and the
clang-format gate stay green.

## ROB-23 — CLOSED: `FillBlockBytes` semantic bugs: `*Size` parameters swallowed, `writeVec4` over-writes, int members get float bits

| Property | Value |
|----------|-------|
| **Severity** | Medium |
| **Category** | Robustness |
| **Original location** | `Source/engine/render/gpu/preset/preset_runner.cpp` (`FillBlockBytes` — the `<TextureName>Size` branch, the `writeVec4` helper, the generic parameter fallback) |

**Original finding.** Three defects shared one function. (a) The
`<TextureName>Size` branch was checked *before* the shader-parameter lookup,
so a UBO member named e.g. `MaskSize` backed by `#pragma parameter MaskSize`
— which the translator explicitly accepts as a parameter — never received its
value: the size resolver found no texture named `Mask`, wrote zeros, and both
the pragma default and the preset override were silently dropped. (b)
`writeVec4` wrote 8+ bytes unconditionally, so for a 4-byte `float`/`int`
member it scribbled 4 bytes into the next member's storage (transiently
clobbering an already-written neighbor). (c) The generic parameter fallback
`WriteFloat(out, member.offset, value)` ignored `member.type` entirely — an
`int`/`uint`/`bool` member matched to a parameter received IEEE-float bit
patterns (value 5 read back as ~1.09e9). The builtin semantics did this
correctly (`WriteUint` for FrameCount/Rotation, `WriteInt` for
FrameDirection), marking (c) as an oversight in the generic path.

**Resolution.** The finding's prescription, all three parts. The parameter
lookup (preset overrides, case-insensitive, then the pass's `#pragma
parameter` defaults) moved ahead of the `<TextureName>Size` branch — a found
parameter writes and continues, builtins keep priority, unresolved names fall
through to the size resolver exactly as before. Writes are now type- and
size-aware through two new helpers: `UniformComponentCount` (std140 component
count: 1 for scalars/Bool, 2/3/4 for the vector types) and `WriteMemberValue`
(one component with the member's own bit pattern — Int family → int32
truncation, Uint family → uint32, Bool → 0/1, Float family → IEEE bits).
`writeVec4` loops its components through the helpers, clamped to the member's
declared shape; the unmatched-member tail writes typed zeros. Builtin
semantics are unchanged; the MVP mat4 writer is untouched.

**Verification.** Shipped-block harness: `UniformType`/`UniformMember` and
the exact helper + `writeVec4` bodies extracted from the patched tree,
compiled standalone, 12 probes ALL OK — int/uint members get value 5 as
integer bits; Bool writes 0/1; a scalar member through `writeVec4` leaves the
offset+4 sentinel (0xAA) intact where the old code scribbled; vec2/vec3/vec4
write exactly their own widths; IVec4/IVec2 builtin-size members get int
bits; component-offset arithmetic verified at a non-zero base. Static: the
parameter block precedes the `<TextureName>Size` branch in the shipped
function. Behavior-change surface: parameter-backed `*Size` members now
receive their values (the finding's own scenario) and int-typed members get
integer bits. CI: the gtest suite and the clang-format gate stay green.

---

## TEST-06 — CLOSED: `preset_smoke` known-texture set omits `#pragma name` identifiers — diverges from the runner

| Property | Value |
|----------|-------|
| **Severity** | Medium |
| **Category** | Testing |
| **Original location** | `Source/engine/render/gpu/preset/preset_smoke.cpp` (the known-name set construction), `Source/engine/render/gpu/preset/preset_runner.cpp` (the step-1 pre-scan it diverged from) |

**Original finding.** The runner built its known-texture-name set from pass
aliases, `#pragma name` values (pre-scanned in step 1 of `Runner::Load`) and
LUT names; the smoke harness inserted only texture names and aliases. A pack
whose later pass sampled an earlier pass by its `#pragma name` — no alias, a
pattern the runner's own comments cite — translated fine in-game but failed
in smoke with "unknown texture reference". Verified against the committed
corpus: every fixture used aliases or builtins, so the pragma-name binding
path was unexercised, and expanding the corpus with real packs
(koko-aio-style cross-references) would have tripped false failures that
don't reproduce in-game — eroding trust in exactly the regression harness
TEST-01 stood up.

**Resolution.** The smoke harness mirrors the runner's step-1 pre-scan: per
preset, each pass's shader file is resolved (`ResolvePresetFilePath`) and
scanned line-by-line with the SAME regex literal the runner uses, first
`#pragma name` match per pass wins, and every found identifier joins the
known-name set in both original and lowercased form through the harness's
existing `insertBoth`. A pass file that cannot be opened is skipped in the
pre-scan (the pass loop reports the open failure with context), matching the
runner's resolve-then-fail flow. No corpus fixture changes were needed.

**Verification.** Regex parity asserted programmatically: the regex literals
extracted from both files are byte-identical. The pre-scan block is a
structural mirror of `Runner::Load`'s step 1 (same regex, same
first-match-per-pass semantics, same case-pair insertion). CI: the corpus
suite (`preset_smoke` / `preset_smoke_corpus`) and the clang-format gate stay
green — a pack using the pragma-name idiom now smokes exactly as it plays.

---

## ROB-07 — CLOSED: `Runner::Load` mid-failure leaks GPU resources; runtimes lack RAII

| Property | Value |
|----------|-------|
| **Severity** | Medium |
| **Category** | Robustness |
| **Original location** | `Source/engine/render/gpu/preset/preset_runner.cpp` (`PassRuntime`/`LutRuntime` structs, `Runner::Load` failure paths, `LoadLut`, `Runner::Unload`) |

**Original finding.** `PassRuntime` and `LutRuntime` held raw
`SDL_GPUShader*`/`SDL_GPUGraphicsPipeline*`/`SDL_GPUTexture*` members with no
destructor, relying on `Runner::Unload` seeing them via `passes_`. During
`Load`, per-pass shaders and pipelines were created before ownership transfer
into `passes_`, and `LoadLut` created the LUT texture before its transfer
buffer and command buffer succeeded — so any mid-load failure (shader N
fails, pipeline N fails, LUT transfer fails) left already-created GPU
resources unreachable when the caller's `unique_ptr` destroyed the runtimes.
The blast radius was bounded because every `Load` failure path ended in a
full `Shutdown()` + device destruction, which frees everything; but the
header's own contract ("Unload is safe to call anytime") was silently false
mid-load, and any future reuse of `Runner` — hot reload, preset retry — would
have turned this into a real leak.

**Resolution.** RAII destructors as the single release point. Both runtimes
gain a `device` member (release context, set at creation in `Load`) and
destructors that release exactly what they own — `PassRuntime`: vertex and
fragment shader, pipeline, both output textures; `LutRuntime`: the texture.
Mid-load failures now clean up automatically at scope exit through the
staging `unique_ptr`s — no failure-path routing needed. `Unload` delegates:
the live-device path deletes the runtimes and lets the destructors release
(`ReleaseTargets` already nulled the pass targets), and the post-teardown
path (`device_ == nullptr`) clears the release context first so deletion
stays pure memory cleanup. The ROB-03 self-heal's explicit release-and-retry
block is untouched (it re-creates in place). Ownership at every instant is
unambiguous — staged `unique_ptr` → `passes_`/`luts_` raw → destructor — so
no double release is possible.

**Verification.** Static call-site census on the shipped file:
`Runner::Unload` contains no shader/pipeline/texture release calls;
`SDL_ReleaseGPUShader` ×4 (destructor ×2 + ROB-03 self-heal ×2),
`SDL_ReleaseGPUGraphicsPipeline` ×1 (destructor), `SDL_ReleaseGPUTexture` ×5
(pass destructor + LUT destructor + `ReleaseTargets` ×3), release context
set at both creation sites. The FEAT-01 reload shipped in the same round is
the first consumer that reuses a `Runner` after failures — its safety rests
on this cleanup. CI: the gtest suite and the clang-format gate stay green.

---

## FEAT-01 — CLOSED: Preset failure retry / hot reload of failed presets

| Property | Value |
|----------|-------|
| **Severity** | Medium |
| **Category** | New Feature |
| **Original location** | `Source/engine/render/gpu/gpu_scaler.cpp` (`EnsurePresetRunner`), `Source/engine/render/gpu/preset/preset_runner.cpp` (`Runner::Load` failure paths — made safe by ROB-07 in the same round) |

**Original finding.** A preset that failed to load disabled the shader filter
for the entire session; the user had to reopen the folder browser and
re-select to retry. This was most painful during exactly the workflow the
mod's own docs recommend — iterating on a preset pack, where a syntax error
meant restarting the selection flow (or, for cache-related failures,
potentially restarting the game after clearing the cache). The friction was
visible in the code structure: `EnsurePresetRunner` already rebuilt "only
when resolved path changes", so a retry path existed but was gated behind
path identity — re-selecting the SAME preset with an edited slang file hit
the loaded fast-path and never re-read the file.

**Resolution.** The finding's minimal version, no new UI. The loaded-chain
identity gains a file stat snapshot — mtime and size via `SDL_GetPathInfo`,
recorded when a load succeeds; a failed stat records zeros, meaning "never
auto-reload". The same-path fast-path now re-stats: a changed mtime or size
logs "preset file changed on disk; reloading: <path>" and falls through to
the normal Unload → Parse → Load rebuild; a stat that fails (file briefly
unavailable) keeps the running chain — a transient unreadable file must not
kill a working session. Any renderer reinit with the same selection
(re-selecting the preset, changing related settings) now picks up pack edits;
re-trying a failed load on re-select already worked and is unchanged.
Chain-identity clearing is centralized in `ClearLoadedPreset()` (fast-path
reload, load failure, `Shutdown`, `HandbackWindow`).

**Verification.** Static: the snapshot is recorded on the success path; the
reload gate fires only on a successful stat whose mtime or size differs from
the recorded values, and short-circuits to keep-chain when the recorded mtime
is zero (stat unavailable at load) or the stat fails; `ClearLoadedPreset()`
census = 4 call sites. Field-verification hook: edit a loaded pack's slang
file, re-select the preset, watch for the reload log line. CI: the gtest
suite and the clang-format gate stay green.

---

## FEAT-03 — CLOSED: Surface preset errors in-game instead of only in gpu-scaler.log

| Property | Value |
|----------|-------|
| **Severity** | Medium |
| **Category** | New Feature |
| **Original location** | `Source/engine/render/gpu/gpu_scaler.cpp` (`ShowFilterOsd`/`DrawFilterOsd`, `EnsurePresetRunner` failure sites) |

**Original finding.** When a preset failed to load, the entire user-visible
symptom was that the Shader Filter silently showed `Off` (or the previous
state) — every diagnostic detail went to gpu-scaler.log next to the
executable, which most users would never find. The building blocks for
in-game feedback already existed: the OSD path (`DrawFilterOsd`/
`ShowFilterOsd`) already rendered status text into the 8-bit frame, the
failure contract already carried a descriptive `std::string` error from every
failure site, and the preset display name already flowed to the OSD. The
`(Unavailable)` convention documented in the README established the pattern;
load failures just didn't complete it.

**Resolution.** `gPresetLastError` records why the last preset load attempt
failed, set at all four `EnsurePresetRunner` failure sites (no stored path,
file not found in the shaders folder, parse failure, `Runner::Load` failure —
the log lines compose the same text as before), cleared on success, and
deliberately surviving the failure `Shutdown` so the OSD can still name the
reason. The OSD's unavailable state upgrades from a bare "(unavailable)" to
"(failed: <first line>)" for custom filters — e.g. "Shader: crt-royale
(failed: cannot open shader file: …)" — via a new `OsdErrorLine` helper that
takes the first line and caps it at 64 characters with an ellipsis so the
bottom-right label cannot walk off-screen; the full text stays in the log.
Built-in filters never show a preset error (the failure belongs to the
preset path). The full-text log line and the existing "(unavailable)"
fallback (custom filter, no recorded error) are unchanged.

**Verification.** Static: the error is recorded at all four failure sites
with log output byte-identical to the previous hardcoded strings; the OSD
branch is gated on `IsCustomFilter` and a non-empty error; the truncation
cap is enforced in `OsdErrorLine`. The OSD refresh points (every Reinit and
renderer recreation) already existed. CI: the gtest suite and the
clang-format gate stay green.

---

