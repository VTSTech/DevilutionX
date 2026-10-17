# Improvement & Enhancement Audit

**DevilutionX (VTSTech GPU Shader Mod) v1.6.0-dev**

**Repository:** https://github.com/VTSTech/DevilutionX
**Author:** VTSTech | **License:** Sustainable Use License (non-commercial, per LICENSE.md) | **Date:** 2026-10-16 (round 15)
**Audit commit:** `8341ef99f9f9c3786222abb57e2eb8c16e79656a` (code state identical to round-6 tip `b60356e1a` — the only newer commit is docs-only) | **Auditor:** Super Z (GLM) — codebase-audit skill v0.2.0
74 Findings total | 7 Categories | SEC, ROB, MAINT, PERF, FEAT, ARCH, TEST | 43 closed → [deltas.md](deltas.md) | 31 open: **0 High / 0 Medium / 31 Low**

---

## Table of Contents

- [Executive Summary](#executive-summary)
- [Findings Summary](#findings-summary)
- [Detailed Findings](#detailed-findings)
  - [Security](#security)
  - [Robustness](#robustness)
  - [Maintainability](#maintainability)
  - [Performance](#performance)
  - [New Features](#new-features)
  - [Architecture](#architecture)
  - [Testing](#testing)
- [Priority Matrix](#priority-matrix)
- [Architecture Strengths](#architecture-strengths)

---

## Executive Summary

This audit covers VTSTech's standalone mod of DevilutionX — a C++23 engine (~121,700 lines of `.cpp` across 620 source files in `Source/`, plus ~15,000 lines of fork-specific code under `Source/engine/render/gpu/`) that layers a Vulkan GPU shader pipeline over the stock SDL render path. Rounds 1–6 enumerated the finding set — the fork's entire delta and its integration points, the engine's game loop and options architecture, and the build/CI/packaging layer, down to individual SDL_GPU API invocations; round 7 re-walked the same surface with fresh eyes targeting state-machine ordering, unchecked SDL return values, resource lifetime across renderer hand-off, and translation-layer input handling. Rounds 7.5–14 then worked the backlog those sweeps produced, closing **42 of the 73 findings** — every closure regression-verified in-tree (the two field closures verified on hardware), each with its full Original/Resolution/Verification record in [deltas.md](deltas.md), the single record of closures and, since round 12, also the home of the round-by-round fix history.

The ledger stands at **31 open findings: 0 High / 0 Medium / 31 Low** — no High finding has been open since the round-7.5 quick-fix batch closed the round-7 pair, and since the round-14 8C sweep **no Medium either**: the working queue that opened the round — ROB-23 (`FillBlockBytes` uniform-fill semantics), TEST-06 (smoke pragma-names), ROB-07 (runner RAII), FEAT-01 (preset reload), FEAT-03 (in-game error surfacing) — all closed in one patch. The open set is Low-only; two partially-fixed findings stay open with cross-notes in their entries below: MAINT-09 (translator aggregated unknown-UBO-member count remains) and MAINT-19 (bare `shaderpath` behavior decision with the maintainer).

Work is sequenced by the upstream merge plan (`docs/merge-plan-draft-r2.md`), which gates every proposed PR on the findings that share its files and orders the remaining closure into three Phase-0 sweeps — **8A** library surface, **8B** presentation surface, **8C** runner surface. First contact (PR 4 + PR 1) is barred until 8A+8B complete — 9 of the 14 Medium open when r2 was measured (r2's own “10 of 14” over-counted ROB-20, which gates both PR 1 and PR 2's translator half). Rounds 8, 9, 11, and 13 have since closed all nine — ROB-18/19/22/20/21, MAINT-15, the FEAT-02 Metal disposition, and (round 13) ROB-08 and PERF-06 — so **the 8A+8B first-contact bar is complete**, and the round-14 8C sweep closed the runner surface together with the FEAT-01/FEAT-03 riders — **Phase 0 is done and the ledger is Medium-free**. Waived out of the PR series: MAINT-11/12/13/14 + TEST-05 (upstream issues, not PR riders), ARCH-02 (fork-only Phase-0 hygiene), ROB-13 (known-documented), FEAT-04 (deferred). Strategic frame recorded in handoff §14: whether or not any PR merges, the long-term goal is a standalone reusable shader-preset library — every closure in this ledger is a step toward that gate.

The mod's invariant frames every finding here: any GPU or preset failure must degrade gracefully to the stock renderer — never `app_fatal`, never a crash — and the remaining open set concentrates on exactly that contract's edges (runner failure paths, swapchain state changes, translator input handling) plus the observability that makes silent degradation visible. The Findings Summary below lists every open finding with severity and category; the Detailed Findings sections carry the full analysis per ID; the Priority Matrix orders the queue into Immediate / Short term / Backlog. For how each closure was actually made — and for the round-by-round history this summary used to carry — see [deltas.md](deltas.md).

---

## Findings Summary

| Prefix | Category |
|--------|----------|
| SEC | Security |
| ROB | Robustness |
| MAINT | Maintainability |
| PERF | Performance |
| FEAT | New Feature |
| ARCH | Architecture |
| TEST | Testing |

| ID | Severity | Category | Title |
|----|----------|----------|-------|
| SEC-02 | Low | Security | `ResolvePresetPath` joins ini-stored paths without containment |
| SEC-03 | Low | Security | Sidecar `.spv` files accepted on magic+size check alone |
| SEC-05 | Low | Security | `IsPathInsideShaderRoot` case-folds on every platform — POSIX case-sibling escape |
| SEC-06 | Low | Security | Pre-sandbox `exists()` probe on absolute/UNC `#reference` paths (Windows) |
| ROB-10 | Low | Robustness | `Runner::Run` failure-contract mismatch: header says transient, caller hard-fails |
| ROB-11 | Low | Robustness | LUT upload size computed in Uint32; no dimension cap on preset LUTs |
| ROB-12 | Low | Robustness | Preset parser accepts six image extensions the PNG-only build cannot decode |
| ROB-13 | Low | Robustness | Known Integer Scaling crash remains (deliberately unfixed, documented) |
| ROB-30 | Low | Robustness | Hard swapchain-acquire failure is indistinguishable from a minimized-window skip |
| ROB-28 | Low | Robustness | Locale-dependent float parsing in the preset pipeline (latent) |
| ROB-29 | Low | Robustness | glslang compile path has no lock (latent until async/hot-reload) |
| MAINT-08 | Low | Maintainability | Trim/lowercase/case-compare utilities triplicated across preset TUs |
| MAINT-09 | Low | Maintainability | Comment/code mismatches: `#reference` merge order and silent UBO validation — *parser comment fixed r7.5; translator aggregate-count half open* |
| MAINT-10 | Low | Maintainability | Diagnostics log written next to the executable — fails silently in protected dirs |
| MAINT-11 | Low | Maintainability | Stale packaging metadata: DEB SDL2 deps, Brewfile, vcpkg version, docs/building.md |
| MAINT-12 | Low | Maintainability | God-files at engine core: diablo.cpp 3553 lines, scrollrt.cpp 1933 lines |
| MAINT-13 | Low | Maintainability | Broken symlink blobs in repo break tooling and cross-compile helpers |
| MAINT-14 | Low | Maintainability | Misleading legacy name: `nthread_has_500ms_passed` no longer means 500 ms |
| MAINT-17 | Low | Maintainability | Driver-rejected sidecar `.spv` never self-heals (only the cache path does) |
| MAINT-18 | Low | Maintainability | `ScanPresets` lacks the hidden-folder/`cache` exclusions the browser enforces |
| MAINT-19 | Low | Maintainability | Parser nits: dead 4-arg `ParsePreset` decl, bare `shaderpath` dropped, empty-key error misleads — *2 of 3 nits fixed r7.5; bare `shaderpath` decision open* |
| PERF-01 | Low | Performance | Per-frame transfer buffer create/map/unmap round-trip |
| PERF-02 | Low | Performance | Per-pass, per-frame heap allocations and repeated string parsing in Runner::Run |
| PERF-03 | Low | Performance | Dead work in the present path: unused fitScale and unread uniform member |
| PERF-04 | Low | Performance | Whole-surface re-upload each present despite dirty-rect blits |
| PERF-05 | Low | Performance | Shader cache grows unbounded; `.slang` sources re-read 3–4× per load |
| FEAT-04 | Low | New Feature | In-game `#pragma parameter` tuning UI (params are parsed but locked at defaults) |
| ARCH-02 | Low | Architecture | Fork branding and asset-integrity hard aborts are entangled with upstream init |
| TEST-04 | Low | Testing | Baked-shader drift: regenerated SPIR-V is never diffed against the committed header |
| TEST-05 | Low | Testing | Fixture-dependent tests silently skip when MPQ assets are absent |
| TEST-08 | Low | Testing | Successful file-backed texture (LUT) loads are unlogged; the field-log digest's "PNG textures" counter can never fire (constant `none`) |

Severity levels:
- **High** — Affects correctness, security, or data integrity. Fix soon.
- Medium — Impacts maintainability, reliability, or UX. Address in planned work.
- Low — Nice-to-have improvement. Address opportunistically.

---
## Detailed Findings

### Security

#### SEC-02: `ResolvePresetPath` joins ini-stored paths without containment

| Property | Value |
|----------|-------|
| **Severity** | Low |
| **Category** | Security |
| **File(s)** | `Source/engine/render/gpu/preset/preset_parser.cpp` (lines 732-741), `Source/engine/render/gpu/gpu_scaler.cpp` (lines 528-530) |

`ResolvePresetPath` joins the `shaderFilterPreset` value stored in `diablo.ini` onto the shader root with `operator/` and only checks `exists()` — a hand-edited value like `../../tools/evil.slangp` resolves outside the root and its compiled output runs as GPU program text. Exploitation requires local write access to the ini, which already implies equivalent privilege, so this is informational rather than a true privilege boundary; it is filed separately from SEC-01 because the trust model differs (config file vs downloaded content pack). The fix is the same containment check with `lexically_normal()` plus a root-prefix assertion, applied at the ini-loading boundary where the code already sanitizes stale builtin values (`options.cpp:430-444`).

**Impact:** One shared containment helper closes both entry points and makes the sandbox auditable.

#### SEC-03: Sidecar `.spv` files accepted on magic+size check alone

| Property | Value |
|----------|-------|
| **Severity** | Low |
| **Category** | Security |
| **File(s)** | `Source/engine/render/gpu/preset/slang_compile.cpp` (lines 102-107, 147-154), `Source/engine/render/gpu/preset/preset_runner.cpp` (lines 332-352) |

The sidecar fast path — `<shader>.vert.spv`/`.frag.spv` next to a `.slang` — always wins over translation and cache, and its only validation is `SpvLooksValid`: size ≥ 20 bytes, size divisible by 4, and the SPIR-V magic `0x07230203` (`slang_compile.cpp:102-107`). A shipped or planted sidecar therefore bypasses every translation-layer structural guarantee (stage splitting, binding dedup, UBO validation) and hands arbitrary SPIR-V straight to the GPU driver. SPIR-V is designed to be validated by drivers, and modern runtimes sandbox it, but the fast path means an attacker-controlled preset pack ships pre-compiled GPU code with no reviewable source. Recommended mitigation: when a sidecar is used, log it prominently; optionally require sidecars to be opt-in via an ini key, or verify the module's entry-point/execution-model bytes match the expected stage before use.

**Impact:** Users can see when they are loading precompiled GPU code instead of translated source, and packs lose the ability to smuggle opaque binaries past inspection.

#### SEC-05: `IsPathInsideShaderRoot` case-folds on every platform — POSIX case-sibling escape

| Property | Value |
|----------|-------|
| **Severity** | Low |
| **Category** | Security |
| **File(s)** | `Source/engine/render/gpu/preset/preset_parser.cpp` (lines 752-774) |

The SEC-01 sandbox helper compares root and candidate with a per-character `tolower(a) == tolower(b)` loop unconditionally, but `std::filesystem::weakly_canonical` (used at `:755, :760` to normalize both sides) performs no case folding on POSIX. On a case-sensitive filesystem, a candidate whose path differs from the shader root only by directory case (root `/…/shaders`, candidate `/…/Shaders/x.png`) passes both the folded prefix comparison and the subsequent separator check — the containment test grants a sibling tree that is not the root. Reads outside the real root require such a case-sibling tree to actually exist on disk, and preset loading already implies local file access, so this is defense-in-depth rather than a new privilege boundary — but it silently narrows the guarantee SEC-01 was closed for on Linux. Fix: gate the case fold behind `#ifdef _WIN32` (where canonicalization folds case) and compare byte-exact elsewhere.

**Impact:** The SEC-01 sandbox holds byte-exact on POSIX instead of loosely.

#### SEC-06: Pre-sandbox `exists()` probe on absolute/UNC `#reference` paths (Windows)

| Property | Value |
|----------|-------|
| **Severity** | Low |
| **Category** | Security |
| **File(s)** | `Source/engine/render/gpu/preset/preset_parser.cpp` (lines 792-804, 40-47) |

`ResolvePresetFilePath` admits absolute paths through `IsAbsolutePath` (including `\\server\share\…` UNC forms) and calls `std::filesystem::exists(absolute, ec)` at `:796` *before* the `IsPathInsideShaderRoot` refusal at `:801`. A malicious preset line `#reference "\\evil\share\x.slangp"` therefore triggers a network probe to an attacker-chosen host — a potential multi-second stall and an NTLM/Kerberos authentication surface on Windows — even though the reference is ultimately refused and nothing is read. The defect is the probe itself, not the (blocked) read. Fix: perform the containment check first and only stat paths that pass it, so refused references never touch the filesystem.

**Impact:** Refused references cost zero I/O and no network side effects.

---

### Robustness

#### ROB-10: `Runner::Run` failure-contract mismatch: header says transient, caller hard-fails

| Property | Value |
|----------|-------|
| **Severity** | Low |
| **Category** | Robustness |
| **File(s)** | `Source/engine/render/gpu/preset/preset_runner.hpp` (lines 66-67), `Source/engine/render/gpu/gpu_scaler.cpp` (lines 1040-1049) |

The `Runner::Run` header documents a `false` return as "frame must be skipped (transient condition)", but `PresentFrame` treats *any* Run failure as a session-fatal hard error (`LogFailHard` → `gFailed` sticky). A transient failure of `SDL_BeginGPURenderPass` (`preset_runner.cpp:1191-1197`) — plausible during resize storms or driver hiccups — therefore permanently kills the GPU path for the session, when the documented intent was to skip one frame. The plumbing for the soft path already exists (`SubmitSkipAfterAcquire`, `gpu_scaler.cpp:247-251`). Fix: classify Run failures — out-of-memory/pipeline-level errors stay hard, transient render-pass/acquire errors return a distinct soft-failure signal that skips the frame and only escalates after N consecutive occurrences.

**Impact:** Transient driver hiccups cost one skipped frame instead of silently downgrading the user to the software path for the rest of the session.

#### ROB-11: LUT upload size computed in Uint32; no dimension cap on preset LUTs

| Property | Value |
|----------|-------|
| **Severity** | Low |
| **Category** | Robustness |
| **File(s)** | `Source/engine/render/gpu/preset/preset_runner.cpp` (lines 840-855, 798-883) |

`transferInfo.size = lut.width * lut.height * 4` is computed in `Uint32` arithmetic while the subsequent `memcpy` length is `size_t`; a surface whose byte size reaches 2^32 would wrap the transfer-buffer size against an un-wrapped memcpy length — a potential heap overflow. It is currently unreachable because SDL3_image caps decoded surfaces below 2 GiB, but the code relies on that external cap with no assertion of its own. (Round 7 note: the same truncation class exists for the game-frame upload at `gpu_scaler.cpp:1038` — `gameWidth * gameHeight * 4` in 32-bit arithmetic — where it is practically unreachable because `SDL_CreateSurface` for such resolutions fails fatally first; fix both sites with `size_t` arithmetic in one pass.) Separately, there is no dimension cap: a legitimate-but-absurd 23000×23000 PNG LUT allocates ~2 GiB of RAM plus ~2 GiB of VRAM from a preset, with no guardrail. Fix: compute the byte size in `size_t`, assert `size <= UINT32_MAX` before assignment, and reject LUTs larger than a documented ceiling (e.g. 4096×4096, generous for every real libretro LUT) with a parse-time warning listing the file.

**Impact:** Preset LUTs can neither trigger arithmetic wrap nor pressure memory beyond a documented, logged limit.

#### ROB-12: Preset parser accepts six image extensions the PNG-only build cannot decode

| Property | Value |
|----------|-------|
| **Severity** | Low |
| **Category** | Robustness |
| **File(s)** | `Source/engine/render/gpu/preset/preset_parser.cpp` (line 124), `3rdParty/SDL3_image/CMakeLists.txt` (lines 22-39), `docs/SLANG_SPEC.md` (§3.5) |

`LooksLikeImagePath` accepts `.png, .jpg, .jpeg, .bmp, .tga, .dds, .webp` for implicit texture references, but the vendored SDL3_image is deliberately built PNG-only (all other codecs disabled, stb backend off, to keep the binary lean and avoid `LOAD_PNG_DYNAMIC` dlopen issues). Presets referencing any of the six non-PNG extensions pass parsing and fail later at `IMG_Load` with a generic error, and SLANG_SPEC.md §3.5 ("Any SDL_image-decodable file") overstates the actual capability. Fix: restrict `LooksLikeImagePath` to `.png` (plus `.jpg` if support is ever added), emit a specific parse-time warning naming the unsupported extension, and correct the spec text.

**Impact:** Users get an actionable "this preset needs JPEG, which this build doesn't support" message instead of a cryptic load failure.

#### ROB-13: Known Integer Scaling crash remains (deliberately unfixed, documented)

| Property | Value |
|----------|-------|
| **Severity** | Low |
| **Category** | Robustness |
| **File(s)** | `Source/utils/display.cpp` (`CalculatePreferredWindowSize`), `Source/engine/render/gpu/gpu_scaler.cpp` (`PresentFrame` zero-size-fit path), `docs/diablo-handoff.md` §8 |

Combining the game's own Integer Scaling option with a desktop resolution that is not a whole-number multiple crashes: the integer factor truncates to 0 → divide-by-zero in `CalculatePreferredWindowSize`, and `PresentFrame` has a latent zero-size-fit path honoring the same option. VTSTech explicitly ruled this out of scope on 2026-10-14, and the failure signature is documented for support triage (GPU_SHADERS.md §Troubleshooting) with an abandoned fix diff snapshotted in the mod packaging notes. It is filed here solely so the audit record is complete and so the deliberate-unfixed status is visible next to the other robustness items; if anyone touches window-size math for another reason, the fix is small and should ride along.

**Impact:** Documented as known; closure is opportunistic whenever display.cpp is next modified.
#### ROB-30: Hard swapchain-acquire failure is indistinguishable from a minimized-window skip

**Renumbered (round-10 re-head, 2026-10-17):** formerly **ROB-26**; the number now belongs to the field-regression closure record in [deltas.md](deltas.md) (synchronous GPU-device teardown inside the Shaders→'Off' switch, closed at 69be0cad5).

| Property | Value |
|----------|-------|
| **Severity** | Low |
| **Category** | Robustness |
| **File(s)** | `Source/engine/render/gpu/gpu_scaler.cpp` (lines 975-981) |

`if (!SDL_AcquireGPUSwapchainTexture(…) || swapchainTexture == nullptr) { ++sSkippedFrames; SDL_CancelGPUCommandBuffer(cmd); return; }` treats a hard failure (swapchain in error state) identically to a benign "window minimized" skip: counted, cancelled, no log, `gFailed` never set. A persistent acquire error therefore presents as an ever-growing skipped-frames count over a black screen with zero error lines and no session fallback — diagnosable only by watching the heartbeat counter.

Fix: distinguish the two cases (a hard `false` return from the null-texture minimized case) and log-and-fail after N consecutive hard failures.

**Impact:** Persistent swapchain errors name themselves in the log instead of hiding behind the skip counter.

#### ROB-28: Locale-dependent float parsing in the preset pipeline (latent)

| Property | Value |
|----------|-------|
| **Severity** | Low |
| **Category** | Robustness |
| **File(s)** | `Source/engine/render/gpu/preset/preset_parser.cpp` (lines 166-173, 581-592), `Source/engine/render/gpu/preset/slang_translate.cpp` (lines 226-233) |

`ParseFloat` (`std::stof`), the numeric-override `std::strtof` calls, and `ParsePragmaNumber` all honor `LC_NUMERIC`. DevilutionX currently only queries `LC_MESSAGES`/`LC_CTYPE` (`platform/locale.cpp:190-192`) and never calls `setlocale(LC_ALL/LC_NUMERIC)`, so "1.5" parses correctly today — but any future `setlocale(LC_ALL, "")` (a plausible upstream change for i18n) silently turns `scale0 = 1.5` into `1` and every pragma default likewise, with no error anywhere. Latent by mechanism, but the failure mode is silent misrendering across every preset. (The companion `is*`/`tolower` audit is clean: every call site casts to `unsigned char`.)

Fix: parse floats with a locale-independent helper (or normalize the decimal point before `strtof`), and add a regression test that sets a comma-decimal locale.

**Impact:** Preset numeric semantics survive future locale work.

#### ROB-29: glslang compile path has no lock (latent until async/hot-reload)

| Property | Value |
|----------|-------|
| **Severity** | Low |
| **Category** | Robustness |
| **File(s)** | `Source/engine/render/gpu/preset/slang_compile.cpp` (lines 34-57) |

`GlslangRuntime::Instance()` makes `glslang::InitializeProcess` run exactly once (thread-safe static init), but `CompileStageToSpv` has no mutex, and glslang's compile entry points are not thread-safe. Today all `Runner::Load` calls are main-thread-only, so this is latent — but it becomes a real race the moment FEAT-01 adds retry/async/hot-reload, and it is exactly the kind of invariant a future refactor won't know it's breaking.

Fix: add a mutex around `CompileStageToSpv` (compile is cold-path; contention is irrelevant), or document the main-thread-only contract at the singleton.

**Impact:** The compile path is safe for the async work FEAT-01 will introduce.

---
### Maintainability
#### MAINT-08: Trim/lowercase/case-compare utilities triplicated across preset TUs

| Property | Value |
|----------|-------|
| **Severity** | Low |
| **Category** | Maintainability |
| **File(s)** | `Source/engine/render/gpu/preset/preset_parser.cpp` (lines 80-96), `Source/engine/render/gpu/preset/slang_translate.cpp` (lines 24-33), `Source/engine/render/gpu/preset/preset_runner.cpp` (lines 31-40, 267-270) |

Three near-identical `Trim`/lowercase/`CaseInsensitiveEqual` implementations live in the four preset translation units, plus lowercase-loop variants at each call site. This drift class is not hypothetical: the F9 case-asymmetry binding bug documented in the SLANG_SPEC audit came from exactly this duplication. Fix: hoist one implementation into a shared `preset_string_utils.hpp` next to `preset_types.hpp` and delete the rest.

**Impact:** One place to fix the next case-sensitivity bug instead of four.

#### MAINT-09: Comment/code mismatches: `#reference` merge order and silent UBO validation

| Property | Value |
|----------|-------|
| **Severity** | Low |
| **Category** | Maintainability |
| **File(s)** | `Source/engine/render/gpu/preset/preset_parser.cpp` (lines 344-348 vs 357-365), `Source/engine/render/gpu/preset/slang_translate.cpp` (lines 931-934, 973-981), `docs/SLANG_SPEC.md` (§4.6) |

Two documentation/code divergences will mislead the next preset-pipeline contributor. First, the `#reference` merge comment says "the first chain providing passes wins", but the code overwrites `preset.passes` per parent — the **last** chain wins (already tracked as SLANG_SPEC gap #13; the comment remains unfixed). Second, `ValidateBlockMembers`' doc says unknown UBO members are "collected as warnings", but the warning push is commented out with a maintainer note ("bloats the log very fast - VTSTech"), so unknown members now fail silently to zero — contradicting SLANG_SPEC §4.6's promise of a load-time warning. Fix: correct the comment to "last chain wins" (or implement first-wins), and replace the disabled per-item warning with a single aggregated count logged once per preset ("N unknown UBO members coerced to 0").

**Impact:** The normative spec and the code agree again, and silent-coercion behavior is at least observable.

#### MAINT-10: Diagnostics log written next to the executable — fails silently in protected dirs

| Property | Value |
|----------|-------|
| **Severity** | Low |
| **Category** | Maintainability |
| **File(s)** | `Source/engine/render/gpu/gpu_scaler.cpp` (lines 104-109, 175-194) |

`LogFilePath` is `SDL_GetBasePath() + "gpu-scaler.log"`. For the portable-zip deployments this fork targets that is exactly right, but when the executable lives in `Program Files` or another protected location, the open fails once and logging silently disables forever (the one-shot `initialized` flag in `LogFile`). Users lose the diagnostics exactly when running from an installed location. Fix: on first open failure, fall back to the platform user-config dir (the same location `diablo.ini` already uses via `GetIniPath`) and note the effective log path in the OSD or stdout once.

**Impact:** Diagnostics work everywhere, and the fallback location is discoverable when support asks for the log.

#### MAINT-11: Stale packaging metadata: DEB SDL2 deps, Brewfile, vcpkg version, docs/building.md

| Property | Value |
|----------|-------|
| **Severity** | Low |
| **Category** | Maintainability |
| **File(s)** | `CMakeLists.txt` (lines 810-814), `Brewfile` (lines 1-7), `vcpkg.json` (line 4), `docs/building.md` |

Four small staleness points cluster around "metadata predates the mod": CPack DEB `Depends` hardcodes `libsdl2-2.0-0, libsdl2-image-2.0-0` (wrong for any SDL3 package); the Brewfile lacks `sdl3` and `glslang-tools`, so macOS builders silently vendor SDL3 and use the prebuilt SPV header without knowing why; `vcpkg.json`'s `version-string` says `1.6.0` vs `VERSION` `1.6.0-dev`; and `docs/building.md` remains upstream-generic with SDL2 apt lines and no `USE_SDL3`/`USE_GPU_SCALER` section (the mod's build docs live in README + GPU_SHADERS.md instead). None blocks a current CI build; all four will confuse the next person building outside the CI matrix. Fix: 20 minutes of metadata alignment, and add a short "SDL3/GPU scaler build" section to building.md pointing at README's quick start.

**Impact:** Out-of-matrix builds (macOS, MSVC, DEB) work on the first try instead of after archaeology.

#### MAINT-12: God-files at engine core: diablo.cpp 3553 lines, scrollrt.cpp 1933 lines

| Property | Value |
|----------|-------|
| **Severity** | Low |
| **Category** | Maintainability |
| **File(s)** | `Source/diablo.cpp`, `Source/engine/render/scrollrt.cpp`, `Source/engine/dx.cpp` |

The upstream-inherited core concentrates init, loop, level-load transitions, and message handling in `diablo.cpp` (3,553 lines) and frame composition in `scrollrt.cpp` (1,933 lines), while `dx.cpp` interleaves three render-path generations (SDL1/2/3 + GPU scaler) behind ifdefs. This is upstream's architecture and the fork correctly *added* clean modules (`gpu/`, `preset/`) rather than deepening the entanglement — the present path integration is, however, a new entanglement point: the GPU branch, SDL_Renderer branch, and window-surface branch all live in `RenderPresent` with cross-file state (`ScalerWanted`, `HeadlessMode`, backbuffer state). Any split should be upstream-first; the fork-local improvement is simply to keep the scaler's present integration isolated in `dx.cpp:312-325` (it currently is — preserve that boundary).

**Impact:** The fork's integration footprint stays reviewable and upstream merges remain feasible.

#### MAINT-13: Broken symlink blobs in repo break tooling and cross-compile helpers

| Property | Value |
|----------|-------|
| **Severity** | Low |
| **Category** | Maintainability |
| **File(s)** | `CMake/platforms/aarch64-linux-gnu-pkg-config`, `CMake/platforms/i386-linux-gnu-pkg-config`, `Packaging/windows/mingw-prep64.sh` |

Three git blobs are mode 120000 (symlink) whose stored content is the *entire script text* rather than a target path — a historical corruption — so any Linux checkout materializes dangling symlinks. Consequences observed during this audit: `rg`/file-scanning tools error out traversing the tree, and the aarch64/i386 cross pkg-config wrappers and the mingw-prep helper are unusable. The core x64 build is unaffected. Fix: replace each with a real file (the content is recoverable from git object metadata or upstream history at the commit where the blob was created).

**Impact:** Repo scans stop erroring and the cross-compile helper scripts actually exist after checkout.

#### MAINT-14: Misleading legacy name: `nthread_has_500ms_passed` no longer means 500 ms

| Property | Value |
|----------|-------|
| **Severity** | Low |
| **Category** | Maintainability |
| **File(s)** | `Source/nthread.cpp` (lines 214-244) |

The fixed-timestep gate function is named `nthread_has_500ms_passed` but its threshold is `gnTickDelay` (20-50 ms, default 50), making the name a fossil of an earlier design. Anyone auditing timing behavior (e.g. checking that the GPU scaler's non-blocking acquire doesn't stall ticks) must read the body to learn what the name asserts. Fix: rename to `nthread_should_run_game_tick` (upstream-coordinated if possible), or at minimum add a one-line comment at the declaration stating the actual threshold source.

**Impact:** Timing-critical review starts from a truthful name.

#### MAINT-15: CI compiles baked shaders with unpinned apt `glslang-tools`; baked SPV is non-reproducible

| Property | Value |
|----------|-------|
| **Severity** | Medium |
| **Category** | Maintainability |
| **File(s)** | `.github/workflows/gpu-shaders.yml` (lines 110, 267), `CMakeLists.txt` (lines 137-151, 212-215) |

The Linux and Windows CI jobs install `glslang-tools` from apt with no version pin; `find_program(GLSLANG_VALIDATOR …)` then picks it up and the `gpu_scaler_spv` target regenerates the shipped `fullscreen.vert`/`blit.frag` blobs with whatever glslang Ubuntu ships that day, while the Package step's "sanity recompile" validates with the same drifting toolchain. The runtime preset path, by contrast, pins `3rdParty/glslang` 16.6.0 and salts its SPIR-V cache with the toolchain version (ROB-04) — the baked-blob path has no equivalent discipline. Consequence: shipped SPIR-V bytes are non-reproducible run-to-run, and an apt glslang regression can change or break the baked shaders with nothing to bisect. This is distinct from TEST-04 (which is about never diffing the regenerated output against the committed header) — pinning removes the *source* of drift, TEST-04 detects it.

Fix: pin the apt package version, or build/validate the baked blobs with the vendored glslang the runtime path already FetchContents.

**Impact:** Baked shader bytes become a function of the repo, not of the runner image.
#### MAINT-17: Driver-rejected sidecar `.spv` never self-heals (only the cache path does)

| Property | Value |
|----------|-------|
| **Severity** | Low |
| **Category** | Maintainability |
| **File(s)** | `Source/engine/render/gpu/preset/slang_compile.cpp` (lines 222-231), `Source/engine/render/gpu/preset/preset_runner.cpp` (lines 414-415) |

`CompilePass` loads sidecar `.spv` files before the `allowCache` gate, and the ROB-03 self-heal triggers only when `spirvSource == SpirvSource::Cache` (`:414-415`). A sidecar that passes `SpvLooksValid`'s magic+size check but is rejected by `SDL_CreateGPUShader` (wrong stage, wrong target env, truncated tail that still divides by 4) therefore fails every session forever with no delete-and-recompile path — the no-cache retry still prefers the same sidecar. The user's only remedy is hand-deleting `foo.vert.spv`/`foo.frag.spv`, and the failure message won't say so. Adjacent to SEC-03's weak content validation, but the gap here is recovery, not validation.

Fix: extend the self-heal to sidecars — on GPU rejection, delete (or rename aside) the sidecar and recompile once from source.

**Impact:** A bad shipped sidecar self-repairs instead of permanently disabling that shader for the pack's users.

#### MAINT-18: `ScanPresets` lacks the hidden-folder/`cache` exclusions the browser enforces

| Property | Value |
|----------|-------|
| **Severity** | Low |
| **Category** | Maintainability |
| **File(s)** | `Source/engine/render/gpu/preset/preset_parser.cpp` (lines 641-686, 704-711), `Source/options.cpp` (line 563), `CMake/Tests.cmake` (lines 303-307) |

`ListPresetDir` (the folder browser) skips dot-prefixed entries and the `cache` directory, but `ScanPresets` — the recursive indexer behind `RescanCustomEntries` self-heal and `ShaderFilter` option population — accepts every `.slangp` with no such filter. Consequences: the ini self-heal can resolve paths inside `.hidden`/`cache` directories that the UI can never show (index-space divergence between loader and browser), and the CI-copied `.preset-smoke` corpus (`Tests.cmake` copies it into `<build>/shaders/`) leaks into every `ScanPresets(ShaderRootDir())` result when the game runs from a build tree — `options_test.cpp:147`'s "first (only) preset in the scan" assumption currently depends on this asymmetry rather than on an explicit exclusion.

Fix: give `ScanPresets` the same dot/`cache` exclusions as `ListPresetDir` (one shared predicate), and make `options_test` assert the exclusion explicitly.

**Impact:** The browser, the ini self-heal, and the test all agree on what a preset is.

#### MAINT-19: Parser nits: dead 4-arg `ParsePreset` decl, bare `shaderpath` dropped, empty-key error misleads

| Property | Value |
|----------|-------|
| **Severity** | Low |
| **Category** | Maintainability |
| **File(s)** | `Source/engine/render/gpu/preset/preset_parser.hpp` (line 25), `Source/engine/render/gpu/preset/preset_parser.cpp` (lines 251-256, 204-213, 336-340, 626-628) |

Three small parser inconsistencies found while re-walking the key-dispatch path. First, `preset_parser.hpp:25` declares a 4-arg `ParsePreset(…, int referenceDepth)` overload that is never defined — the first external caller gets a linker error, and the signature implies a public recursion API that doesn't exist (the 3-arg overload owns the depth logic). Second, `ApplyPassKey` handles a bare `shaderpath` key (`:251-256`, documented as "absolute or preset-relative path override"), but it is only invoked from the `SplitPassKey` branch which requires trailing digits — a digit-less `shaderpath = x` line falls through to the unknown-key sink and is silently dropped; only `shaderpathN` works. Third, a line like `= x.png` yields an empty key that passes `LooksLikeImagePath` and creates an empty-named texture, then fails validation with `"preset texture has no path: (unnamed)"` — the path is set; the *name* is the problem, and the message sends field debugging the wrong way.

Fix: delete the dead overload, reject bare `shaderpath` with a named error (or implement it), and reword the empty-name validation error.

**Impact:** The parser's surface matches its spec, and error messages point at the real problem.
**Round-7.5 partial fix (2026-10-16):** the dead 4-arg `ParsePreset` overload is deleted
from the header (no definition, no callers — the first external caller would have been a
linker error), and the texture validation error now says which of name/path is actually
missing (`preset texture has an empty name (...)` vs `preset texture 'x' has no path
(...)`) instead of always blaming the path. Still open: bare `shaderpath` handling.
Rejecting it with a named error would hard-fail presets that currently load (the line is
silently dropped today), and implementing it would invent semantics RetroArch does not
define — both behavior changes, so the choice is left to the maintainer.

**Round-7.5 partial fix (2026-10-16):** the parser-side `#reference` merge comment now
states the actual semantics ("the LAST chain providing passes wins", matching the
per-parent overwrite at the merge site); the behavior question itself stays tracked as
SLANG_SPEC gap #13. Still open: the translator half — `ValidateBlockMembers`' disabled
per-item warning should become a single aggregated count logged once per preset
("N unknown UBO members coerced to 0") so silent coercion is observable.


---

### Performance

#### PERF-01: Per-frame transfer buffer create/map/unmap round-trip

| Property | Value |
|----------|-------|
| **Severity** | Low |
| **Category** | Performance |
| **File(s)** | `Source/engine/render/gpu/gpu_scaler.cpp` (lines 974-1022) |

`PresentFrame` creates a fresh `SDL_GPUTransferBuffer`, maps it, memcpys the frame, unmaps, and destroys it — every single frame. The code comments explain this is deliberate: reusing a buffer the previous frame's copy pass might still be reading would be a hazard. That reasoning is sound for a single buffer but not for a small ring: a 2-3 entry ring of long-lived transfer buffers (one per in-flight frame) removes the per-frame create/destroy round-trip while preserving the never-overwrite-an-in-flight-buffer guarantee. At 640×480×4 ≈ 1.2 MB per upload the cost is small but nonzero; it also adds allocation churn to driver telemetry. Fix when profiling shows transfer-buffer creation in the frame budget, or as a low-risk first GPU-path change for a new contributor.

**Impact:** One less driver-level allocation/free pair per rendered frame, with a bounded, testable change.

#### PERF-02: Per-pass, per-frame heap allocations and repeated string parsing in Runner::Run

| Property | Value |
|----------|-------|
| **Severity** | Low |
| **Category** | Performance |
| **File(s)** | `Source/engine/render/gpu/preset/preset_runner.cpp` (lines 1086, 1202-1207, 1217, 1228-1267) |

Every frame, for every pass, `Runner::Run` heap-allocates a `std::vector<SDL_GPUTextureSamplerBinding>` (`:1217`), a `std::vector<uint8_t>` for each pushed uniform block (`:1086`), copies `UniformBlockRuntime` values (`:1202-1207`), and re-parses texture binding names with repeated `std::stoi`/`substr` (`:1228-1267`) — work whose results are identical between frames and change only on preset load or resize. None of this currently registers against the frame budget at Diablo resolutions, but it is pure waste in the hottest loop of the mod. Fix: precompute a per-pass binding table and std140 byte blobs in `Load`/`EnsureTargets`, storing them in `PassRuntime`; `Run` then only flips double buffers and binds. This also removes the per-frame `stoi` exposure surface that ROB-01 documents.

**Impact:** The frame loop becomes allocation-free and string-free, and ROB-01's crash surface shrinks to load time only.

#### PERF-03: Dead work in the present path: unused fitScale and unread uniform member

| Property | Value |
|----------|-------|
| **Severity** | Low |
| **Category** | Performance |
| **File(s)** | `Source/engine/render/gpu/gpu_scaler.cpp` (lines 945-954, 1112), `Source/engine/render/gpu/shaders/vulkan/blit.frag` (lines 11-13) |

`fitScale` is computed every frame but consumed only by the removed non-custom branch (`:962-967` — dead since the built-in filters were deleted), and `uniforms.params[0] = 0.0F` feeds a `uParams` member that `blit.frag` declares but never reads. Trivial cost, real reader cost: both suggest live builtin-fit logic that no longer exists. Fix: delete both (and the shader member), keeping the comment at `:962-964` that documents why the branch is gone.

**Impact:** The present path contains only live work, and the blit shader's uniform layout matches what the code actually sends.

#### PERF-04: Whole-surface re-upload each present despite dirty-rect blits

| Property | Value |
|----------|-------|
| **Severity** | Low |
| **Category** | Performance |
| **File(s)** | `Source/engine/render/gpu/gpu_scaler.cpp` (lines 974-1022), `Source/engine/dx.cpp` (lines 159-210) |

The CPU-side blit into `RendererTextureSurface` is dirty-rect incremental (`dx.cpp:193-200`), but `PresentFrame` always uploads the full `gameW×gameH×4` frame to the GPU. At Diablo's internal resolutions (640×480 / 960×540) the whole upload is ~1-2 MB and cheap; this finding is informational and becomes actionable only if the scaler is ever used at high internal resolutions or on bandwidth-constrained devices (the handheld platforms the upstream tree targets). If it ever matters, the fix is a dirty-rect upload path using `SDL_QueueGPUCommandBuffer`-friendly staged copies per rect — a nontrivial change that should be profiled first.

**Impact:** Documented ceiling: the GPU path's upload cost is O(frame), not O(dirty), and that's acceptable at target resolutions.

#### PERF-05: Shader cache grows unbounded; `.slang` sources re-read 3-4× per load

| Property | Value |
|----------|-------|
| **Severity** | Low |
| **Category** | Performance |
| **File(s)** | `Source/engine/render/gpu/preset/slang_compile.cpp` (lines 171-175), `Source/engine/render/gpu/preset/preset_runner.cpp` (lines 237-255), `Source/engine/render/gpu/preset/slang_translate.cpp` (lines 114-122, 1137-1143) |

Two cold-path inefficiencies compound on preset load. First, the cache never prunes: every distinct translated source leaves a `.vert.spv`/`.frag.spv` pair in `shaders/cache/` forever, so large shader packs that get updated accumulate dead blobs indefinitely (and ROB-04's version-salt fix would accelerate this without pruning). Second, each `.slang` is read from disk 3-4 times per load (pragma pre-scan, include resolution, fingerprint re-read). For single-preset loads this is milliseconds; for 20+ pass packs on cold cache it is noticeable. Fix: prune cache entries older than N days or not matching the current version salt at startup (best-effort, behind a log line), and read each `.slang` once into a string view passed through the three stages.

**Impact:** Cache directories stay proportional to installed presets, and multi-pass pack loading is single-read.

### New Features

#### FEAT-02: Resolve the Metal strategy: wire the MSL path or fail fast with a clear message

| Property | Value |
|----------|-------|
| **Severity** | Medium |
| **Category** | New Feature |
| **File(s)** | `Source/engine/render/gpu/gpu_scaler.cpp` (lines 682-729, 719-720), `Source/engine/render/gpu/preset/preset_runner.cpp` (lines 228-231), `Source/engine/render/gpu/gpu_shaders_msl.hpp`, `docs/diablo-handoff.md` §4 |

The device layer accepts Metal on macOS (`CreateDevice` sets both SPIR-V and MSL shader format props, `:719-720`), and the repo embeds 437 lines of hand-ported MSL for the shared Vertex/Blit programs — but `Runner::Load` hard-rejects every non-Vulkan backend ("runtime shader presets are not supported on the metal backend", `:228-231`). Net effect: on macOS, every shader filter choice deterministically fails into the generic "custom preset failed to load" path (`:786-790`), which neither explains the platform limitation nor offers a toggle. The handoff doc separately claims SDL transpiles SPIR-V to MSL at runtime and marks the whole area "untested" — the doc and code disagree, and the code is authoritative. Two coherent resolutions: (a) remove Metal acceptance at `CreateDevice` until validated, surfacing "GPU shader mod requires Vulkan; Metal is not yet supported" at device-selection time, or (b) actually validate the Metal path (SDL's Metal backend does accept SPIR-V via transpilation in some configurations) and update the doc to match. Either is an afternoon; the current state is the only genuinely confusing one.

**Impact:** macOS users get an honest, specific message instead of a misleading generic preset failure.

#### FEAT-04: In-game `#pragma parameter` tuning UI (params are parsed but locked at defaults)

| Property | Value |
|----------|-------|
| **Severity** | Low |
| **Category** | New Feature |
| **File(s)** | `Source/engine/render/gpu/preset/preset_parser.cpp` (parameter parsing), `Source/engine/render/gpu/preset/preset_runner.cpp` (`FillBlockBytes` uniform fill), `docs/diablo-handoff.md` §4 |

The pipeline fully parses libretro `#pragma parameter` declarations and feeds them into the std140 uniform blocks at their default values — by explicit design decision ("`#pragma` params at defaults", handoff §4). But the parsed data model already carries names, min/max/step, and the uniform plumbing is live every frame, so an in-game tuning surface (a submenu off the shader browser listing the active preset's parameters, values persisted per-preset into `diablo.ini`) would complete the RetroArch parity story with no new GPU work. This is the single highest-leverage quality-of-life feature for shader users: libretro packs rely on parameter tweaks (e.g. CRT mask strength, curvature amount), and today the only way to change them is editing the preset file. Scope it as a follow-up to the options-browser work that already handles dynamic lists (the folder-browser navigation at `options.cpp:577-626` demonstrates the machinery).

**Impact:** Users tune shaders from the game instead of editing files, matching the RetroArch experience the mod imports.

---

### Architecture

#### ARCH-02: Fork branding and asset-integrity hard aborts are entangled with upstream init

| Property | Value |
|----------|-------|
| **Severity** | Low |
| **Category** | Architecture |
| **File(s)** | `Source/diablo.cpp` (lines 905-907, 1219-1226, 1228-1242), `Source/mods/mod_identity.{h,cpp}` |

The fork's identity lives in two places that couple it to upstream behavior: `SetApplicationVersions` hardcodes `"github.com/VTSTech/DevilutionX "` into `gszProductName` (`:1219-1226`), and the multiplayer asset-integrity gate `app_fatal`s mid-game loop when modified lua/tsv/sol assets are detected (`:905-907`), with `CheckArchivesUpToDate` similarly hard-failing on stale MPQs at startup (`:1228-1242`). These are reasonable choices individually — mod identity must be distinguishable for MP compatibility, and asset integrity protects the MP ecosystem (the `mods/mod_identity.cpp` SHA-256 manifest system is the fork-aware part) — but they are spread through upstream init code as edits rather than as a fork-identity module, which makes upstream merges harder and makes the hard-abort policy invisible to contributors reading `mods/`. Fix: gather the fork's identity/version/MP-policy code into `Source/mods/` (where `mod_identity` already lives) behind small functions the upstream files call, so the delta is enumerable.

**Impact:** Fork-vs-upstream diffs stay reviewable and the MP abort policy has a single, documented home.

---

### Testing

#### TEST-04: Baked-shader drift: regenerated SPIR-V is never diffed against the committed header

| Property | Value |
|----------|-------|
| **Severity** | Low |
| **Category** | Testing |
| **File(s)** | `.github/workflows/gpu-shaders.yml` (lines 174-179), `CMakeLists.txt` (lines 134-170), `Source/engine/render/gpu/gpu_shader_spv_prebuilt.hpp` |

The build uses `glslangValidator` to regenerate the SPV header when available and falls back to the committed `gpu_shader_spv_prebuilt.hpp` otherwise — a good resilience design — but CI only *recompiles* the GLSL corpus as a sanity check (`:174-179`) without diffing the output against the committed header. If the GLSL sources are edited and the prebuilt header is not regenerated (the exact workflow the fallback invites on machines without glslang), a stale binary ships silently and the drift is invisible until a shader behaves differently between build environments. Fix: one CI step — regenerate the header with `gen_spv_header.cmake`, `git diff --exit-code` against the committed file, and fail with "run the SPV regeneration, commit the header".

**Impact:** The two shader-provisioning paths can never disagree without CI saying exactly how to fix it.

#### TEST-05: Fixture-dependent tests silently skip when MPQ assets are absent

| Property | Value |
|----------|-------|
| **Severity** | Low |
| **Category** | Testing |
| **File(s)** | `test/main.cpp` (lines 40-84), `test/timedemo_test.cpp` (lines 50-53) |

The most valuable regression test in the repo — the headless timedemo that replays a recorded demo and hero-compares the result — plus several benchmarks `GTEST_SKIP` when the game's MPQ assets are absent. That is the right call for a fresh checkout, but it means an environment misconfiguration (or a future CI job that forgets the asset step) silently reports green with the suite hollowed out. The custom skip-summary listener (`test/main.cpp:14-77`) already prints skip counts; what's missing is enforcement. Fix: add a `--fail-on-skip` style flag (or env var) used by CI that converts skips into failures, so a CI test job that lost its fixtures fails loudly instead of passing empty.

**Impact:** CI test results are trustworthy — green means the regression net was actually cast.

#### ROB-31: Field CTD on the Vertical Sync toggle — dead callback registration left an unthrottled IMMEDIATE swapchain

**Original finding.** Enabling Vertical Sync in the settings with the GPU scaler
active crashed the game (field report against the round-14 build, 9cac3fe). The
round-13 ROB-08 fix registered `gpu_scaler::ApplyFrameRateControl` as the option's
change callback, but `OptionEntryBase::SetValueChangedCallback` stores a single
slot and display.cpp registers the stock `ReinitializeRenderer` handler for the
same option further down the same translation unit — static initialization runs
in definition order, so the stock handler always won the slot and the scaler
registration was dead on arrival. The toggle therefore never reached the
swapchain: with the scaler active, `ReinitializeRenderer` takes the same-window
fast path (the claim block is skipped when the window object is unchanged and
the SDL_Renderer is already gone, so neither the window recreation nor
`SDL_SetGPUSwapchainParameters` runs), while dx.cpp's present path stops calling
`LimitFrameRate` as soon as the option reads `VerticalSync`. Net effect: the CPU
limiter switched off but the swapchain stayed in the `IMMEDIATE` present mode it
was claimed with — an unbounded present/submit flood on a still-immediate
swapchain, which field-crashed.

**Resolution.** display.cpp now registers one dispatching handler,
`OptionFrameRateControlChanged`: while the scaler is active it forwards to
`ApplyFrameRateControl` (the live swapchain re-applies the option's present mode
in place — VSYNC throttles presentation through swapchain backpressure, matching
the disabled CPU limiter), otherwise it calls `ReinitializeRenderer`, preserving
the stock renderer's upstream recreate-and-`SDL_SetRenderVSync` behavior. The
dead round-13 registration was removed so the single-slot trap cannot recur.
`ApplySwapchainPresentMode` gained the failure-contract hardening: null
device/window guards, and an unsupported present mode falls back to the nearest
supported one (IMMEDIATE → MAILBOX → VSYNC, fallback logged) instead of failing
the parameter change and leaving the option desynced from the swapchain.

**Verification.** Byte-extracted-block harness: VSYNC request applies FIFO;
IMMEDIATE applies when supported; IMMEDIATE → MAILBOX and → VSYNC fallbacks
engage with the log line; a failed parameter change logs (`LogFail`) and returns
normally; the null-device state makes no SDL call; the dispatcher routes
scaler-active to `ApplyFrameRateControl` and stock to `ReinitializeRenderer`.
clang-format 18.1.8 clean on both files. Failure contract: a rejected present
mode costs a log line, never a crash. CI: the gtest suite and the clang-format
gate stay green.


---

## Priority Matrix

The project versions as `1.6.0-dev` without its own release cadence, so relative timelines are used.

| Timeline | Findings |
|----------|----------|
| **Immediate** (next 1–2 builds) | *(empty — the entire previous Immediate row shipped as the 2026-10-16 quick-fix batch: ROB-14, ROB-15, ROB-16, ROB-17, MAINT-03 re-close; see [deltas.md](deltas.md))* |
| **Short term** (planned work) | *(empty — the round-14 8C runner sweep closed the whole row: ROB-23, TEST-06, ROB-07 → FEAT-01, FEAT-03; see [deltas.md](deltas.md))* |
| **Backlog** (opportunistic) | SEC-02, SEC-03, SEC-05, SEC-06, ROB-10, ROB-11, ROB-12, ROB-13, ROB-28, ROB-29, ROB-30, MAINT-08 through MAINT-15, MAINT-17 through MAINT-19, PERF-01 through PERF-05, FEAT-04, ARCH-02, TEST-04, TEST-05 |

Sequencing notes: the round-7 Highs (ROB-14, ROB-15), the unchecked-SDL cluster (ROB-16/17) and the MAINT-03 re-close shipped together as one low-risk patch on 2026-10-16, with ROB-24/ROB-25/MAINT-16/TEST-07 and the partials (MAINT-09, MAINT-19) as riders. The open set now has no High findings; the round-8 patch closed the former queue openers ROB-18/ROB-19/ROB-22 (bounded fallback-path changes plus the gamepad free/re-init pairing), and the round-9 patch closed ROB-20/ROB-21 (the load-path caps + catch-all and the sampler dedupe, with regression corpus), so the short-term queue now opens at ROB-23. ROB-07 (runtime RAII) remains the prerequisite for FEAT-01 (retry/hot-reload), and ROB-29 (glslang mutex) should land before any async variant of FEAT-01. FEAT-02 (Metal) is dispositioned as of round 11 — the device layer is Vulkan-only, auto-selected Metal gets an honest message plus the ROB-18 Vulkan retry (MoltenVK stays viable) — so the backend policy is decided and the handoff table matches the code. The round-14 8C sweep closed the entire short-term row — ROB-23 (type- and size-aware uniform fill with the parameter lookup ahead of the `*Size` branch), TEST-06 (smoke ports the runner's `#pragma name` pre-scan), ROB-07 (RAII destructors as the single release point) — with FEAT-01 (reload-on-select when the file's mtime/size changed) and FEAT-03 (the OSD names the load failure) as riders; ROB-29 (glslang mutex) remains the prerequisite to honor before any async variant of the now-shipped reload. Field round 15 (2026-10-16) then opened and closed ROB-31, the post-8C field regression: the Vertical Sync toggle's change-callback registration was dead on arrival (the option's single callback slot was won by the stock handler), leaving an unthrottled IMMEDIATE swapchain behind the disabled CPU limiter — fixed with an explicit scaler/stock dispatcher plus present-mode support fallback.

**Merge-plan Phase-0 mapping (2026-10-16, `docs/merge-plan-draft-r2.md` §3/§5):** Short-term row → sweeps: ROB-18/19/22 (round 8) + FEAT-02 (round-11 disposition) + ROB-08 (round 13) + ROB-30 → **8B**; ROB-20/21 (round 9) + MAINT-15 (round 11) + PERF-06 (round 13) → **8A**; ROB-23 + ROB-07 + TEST-06 → **8C** (all three closed round 14); FEAT-03 → optional PR-7 rider (closed round 14 alongside). Backlog row → 8A absorbs SEC-02/03/05/06, ROB-12/28/29, MAINT-08/09/18/19, TEST-04, PERF-05; 8C absorbs ROB-10/11, MAINT-17, PERF-02; waived to upstream issues: MAINT-11/13/14 + TEST-05 (+ MAINT-12 no-action); fork-only: ARCH-02; known-documented: ROB-13; deferred features: FEAT-01 (needs ROB-07), FEAT-04. First-contact bar = 8A+8B (9 of the original 14 open Medium closed — r2's “10” double-counted ROB-20 across the PR 1/PR 2 gates; rounds 8+9 shipped 5 of those 9, round 11 closed MAINT-15 and the FEAT-02 disposition, and round 13 closed the last two, ROB-08 and PERF-06 — **8A+8B complete as of round 13**); the round-14 sweep closed 8C's own three Mediums with the FEAT-01/FEAT-03 riders — **Phase 0 complete, ledger Medium-free**.

---

## Architecture Strengths

These patterns are working well and should be preserved during any refactoring:

1. **A disciplined failure contract with correct SDL3-GPU state-machine handling.** The acquire/cancel/skip matrix in `PresentFrame` is exactly right — command buffers are cancelled only *before* swapchain acquire (`gpu_scaler.cpp:922`), and empty-submit-after-acquire is implemented with the API-contract rationale documented at the call site (`:239-251`). The session-sticky `gFailed` flag downgrades to the stock renderer instead of retrying a broken GPU every frame (`:78-80`, `dx.cpp:315-321`). This is the pattern the audit's ROB findings ask to extend, not replace.

2. **The stock render path is untouched, making the mod strictly additive.** All rendering still targets the 8-bit `PalSurface`; the GPU scaler receives an already-converted RGBA frame and owns only the present step (`dx.cpp:312-325`). Palette animation, fades, and every upstream rendering feature work identically with the scaler active — and any scaler failure returns the user to a renderer that never changed. This layered design is why the mod can ship at all without forking gameplay behavior.

3. **Field-hardened observability built into the render path.** The rotating, size-capped gpu-scaler.log with session headers, SDL log chaining, per-5s heartbeat counting silently-skipped frames, `DEVILUTIONX_GPU_DEBUG` validation mode, load-phase probes in `interfac.cpp`, FNV-1a content fingerprints per translated pass, and git-lfs pointer detection in LUT failures (`slang_translate.cpp:1136-1144`, `preset_runner.cpp:67-89`) — every "round N" comment in the code corresponds to a real field failure that can no longer repeat silently. Most projects add this telemetry only after their second crisis; this one bakes it in.

4. **The preset runner honors libretro semantics in depth, with per-decision commentary.** Scale-rule target sizing with aspect-corrected viewports, feedback double-buffering with correct pre-flip reads, a seeded history ring so early `OriginalHistory#` taps never read undefined GPU memory, a reasoned MVP orientation law for even/odd pass chains (`preset_runner.cpp:924-947`), and a self-critical normative spec (`SLANG_SPEC.md` §7 tracks ~20 gaps with fix commits). Two static-audit-discovered wrongness bugs (history ring, bare scale) were fixed with regression tests and documented in `docs/audit/`.

5. **Fully reproducible vendored dependencies with zero submodule risk.** `.gitmodules` is empty by design; every `3rdParty/*` entry is a thin wrapper pinning a tarball+SHA256 or exact GIT_TAG, with system-first resolution via the consistent `DEVILUTIONX_SYSTEM_*` convention. The zlib/libpng dual-provisioning between SDL_image paths is worked around with documented CMake care (`CMake/Dependencies.cmake:8-22, 170-205`). Nothing about the dependency tree is left to chance.

6. **Distribution-grade CI gates encoded as executable lessons.** gpu-shaders.yml fails the build if a Windows binary imports a runtime DLL, if a Linux binary exceeds its glibc-2.36 floor, or if `--version` doesn't embed the commit hash (`:124-148, 244-249`) — three real field failure modes converted into automated gates, alongside per-run binary size telemetry. The portability gates in particular are the kind of institutional knowledge that usually lives only in a maintainer's head.
