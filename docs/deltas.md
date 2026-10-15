# Fix Deltas — Closed Findings

**DevilutionX (VTSTech GPU Shader Mod) v1.6.0-dev**

**Repository:** https://github.com/VTSTech/DevilutionX
**Baseline audit:** [audit.md](audit.md) @ commit `27548c8b2d6b1dbf7b4c348ecd79abd4598b8b99`
**Date:** 2026-10-15

This ledger tracks findings that have been **fixed and verified**. Each entry preserves the
original audit finding (title, severity, location) and records the resolution actually shipped:
what changed, in which files, and how it was verified. Once an entry appears here it is removed
from the open-findings detail in `audit.md` (the master table there keeps a CLOSED pointer for
traceability).

| ID | Severity | Category | Title | Closed |
|----|----------|----------|-------|--------|
| ROB-01 | **High** | Robustness | Uncaught `std::stoi` on texture binding names terminates the process mid-frame | 2026-10-15 |
| ROB-02 | **High** | Robustness | `shaders = 2147483647` in a preset triggers uncaught ~250 GB allocation → abort | 2026-10-15 |
| MAINT-02 | Medium | Maintainability | ~10K lines of dead built-in shader/LUT payload still compiled and embedded | 2026-10-15 |

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
