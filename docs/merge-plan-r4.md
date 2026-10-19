# merge-plan-r4.md — Individual-Change Branches & PR Sequence

**Status:** ACTIVE — supersedes merge-plan-r3.md §5 (single mega-PR / fresh-fork route) for PR submission.
**Directive (upstream maintainers):** *"make a branch for your individual changes"* — split the
curated payload into individual topical branches, one PR each, ordered so every PR is reviewable
and buildable on its own, with hard dependencies stacked.
**Prepared:** 2026-10-19 (America/Toronto)
**Inputs:**
- Payload blobs: fork `6dc2a8713` (libslang-vk tip used by r3) — except `CMakeLists.txt` = `78113c7c6` (bumped libslang-vk pin `b91b0499`, per r3 manifest provenance).
- Base: `diasurgical/master` (re-check tip at push time; rebase-free single commits).
- Attribution headers (Task 4) are stamped into the PR-5 new-file contents by the builder script.

---

## 0. Why a split is required (curation findings)

Diffing the r3 payload (35 adds + 37 mods) against `diasurgical/master` shows it is **not one
change** — it is at least six. Reviewers would have to reverse-engineer the boundaries, so we
draw them ourselves:

| # | Finding | Evidence |
|---|---------|----------|
| F1 | **Independent audio-robustness fix** hides in the payload | `sound.cpp`, `soundsample.*` (+152), `mpq_reader.hpp` (+2), SDL3_mixer patch + CMake glue, `DEVILUTIONX_WAV_FALLBACK` option. Zero coupling to the scaler. |
| F2 | **Independent crash guards** ("ROB-27") | `cursor.cpp` +8, `controls/touch/renderers.cpp` +8 — menu-time null-art guards triggered by browsing shader settings outside a game session. |
| F3 | **Independent SDL3 dependency build fixes** | `3rdParty/SDL3_image/CMakeLists.txt` (vendored png/zlib), `Dependencies.cmake` (`BUILD_SHARED_LIBS` leak pin + zlib CMP0002 provisioning), `libpng` `png_shared`, `vcpkg.json` `sdl3` feature (fills a real upstream gap — upstream's `VcPkgManifestFeatures.cmake` already appends `sdl3`), `Tests.cmake` gmock link fix. Required for the scaler's Windows/vendored SDL3 CI to configure at all. |
| F4 | **Fork branding must not ride any PR** | `diablo.cpp:1221-1224` rewrites `gszProductName` to `github.com/VTSTech/DevilutionX-libslang-vk …` (r3 payload accidentally included it; URL updated when the fork was renamed); `diablo.h` buffer bump 64→128 exists only to serve that string. Both excluded. |
| F5 | **EOL rewrites inflate 3 files ~10x** | Repo has `.gitattributes` `* -text` (EOLs stored raw). Upstream stores `Source/options.cpp`, `vcpkg.json`, `3rdParty/SDL3_image/CMakeLists.txt` as **CRLF**; the fork rewrote them LF, turning true diffs of 392 / 7 / 38 lines into 3766 / 121 / 104. r4 re-normalizes every PR file to its upstream file's stored convention (new/changed lines take the file's dominant EOL; unchanged lines keep upstream's exact bytes). |
| F6 | Shared-file hunks need surgical splitting | `CMakeLists.txt` carries both the scaler block (~52 lines) and the 1-line WAV option; `CMake/Tests.cmake` carries both the gmock fix and the options_test block. Each hunk goes to its own PR. |
| F7 | `preset_smoke` fixtures ↔ `SLANG_SPEC.md` ↔ `tools/audit_slang_presets.py` cross-reference; `options_test.cpp` stages its own fixtures at runtime | Fixtures/tool/docs travel together in the scaler PR (or its optional lean variant drops all three). |

**r4 ground rule:** every PR is (a) single-commit, (b) zero file deletions, (c) contains no
`.github/`, no branding strings, (d) diffs match the *true* (EOL-normalized) sizes below, and
(e) builds green standalone on upstream CI — upstream workflows trigger on
`pull_request: [opened, synchronize]`, so each pushed branch self-validates with zero extra setup.

---

## 1. PR series overview (submission & merge order)

| PR | Branch | Base | Topic | Files | True Δ | Depends on |
|----|--------|------|-------|-------|--------|------------|
| 1 | `pr/1-sdl3-build-fixes` | `upstream/master` | SDL3/dependency build fixes | 5 M | +93/−23 | — |
| 2 | `pr/2-wav-audio-robustness` | `upstream/master` | WAV decode robustness + no-fatal audio errors | 1 A + 8 M | +228/−10 | — |
| 3 | `pr/3-robustness-guards` | `upstream/master` | Menu-time art guards + screenshot log fix | 3 M | +17/−1 | — (PR 5 UX leans on it) |
| 4 | `pr/4-test-missing-asset-guards` | `upstream/master` | UI tests survive missing MPQ fixtures | 9 M | +49/0 | — |
| 5 | `pr/5-gpu-scaler` | **`pr/1-sdl3-build-fixes`** (stacked) | The feature: slang-shader Vulkan GPU scaler | 34 A + 12 M | +4161/−42 | **hard:** PR 1 · soft: PR 3 |
| 6 *(optional)* | `pr/6-sdl3-main-glue` | `upstream/master` | SDL3 `SDL_main.h` include (fixes WIN32 GUI link) | 1 M | +3/0 | — |

Merge mechanics: 1–4 and 6 are independent — merge in any order (the numbers are review
priority, not a chain). PR 5 is stacked on PR 1: set the PR's *base* to the fork's
`pr/1-sdl3-build-fixes` branch so its diff shows only scaler files; GitHub auto-retargets it to
`master` when PR 1 merges (any merge style — content-identical).

---

## 2. Per-PR specifications

### PR 1 — `pr/1-sdl3-build-fixes` (foundation; required by PR 5)

**Purpose:** make SDL3 builds configure and link everywhere (vcpkg dynamic, vendored fallback,
cross builds, nonstatic CI). Upstream's own SDL3 CI (Linux, system SDL3) is green today — these
fixes unblock the *other* SDL3 configurations, which the scaler PR exercises.

| File | Type | True Δ | Content |
|------|------|--------|---------|
| `3rdParty/SDL3_image/CMakeLists.txt` | M | 38 | `SDLIMAGE_VENDORED ON`, fetch git repo with png/zlib/jpeg submodules, `PNG_SHARED OFF` (avoids `LOAD_PNG_DYNAMIC` dlopen of a DLL that is never packaged); fixes "Unsupported image format" PNG failures + broken screenshots. **Re-normalized to upstream CRLF.** |
| `CMake/Dependencies.cmake` | M | 61 | `set(BUILD_SHARED_LIBS OFF)` root pin (stops SDL_image/SDL_mixer's dependent-option from flipping every later dependency to shared — broke libzt/natpmp linking); zlib provisioning moved into the `USE_SDL3` branch with reuse of SDL3_image's vendored zlib (CMP0002 duplicate-target abort). |
| `3rdParty/libpng/CMakeLists.txt` | M | 2 | dynamic target name `png` → `png_shared`. |
| `vcpkg.json` | M | +7 | declare the `sdl3` feature (`sdl3`, `sdl3-image`) that `CMake/VcPkgManifestFeatures.cmake` already references. **Re-normalized to upstream CRLF.** |
| `CMake/Tests.cmake` | M | +8 | hunk 1 only: link `GTest::gmock` for standalone tests that include gmock without it (only built where system gtest sat on the default include path). *(options_test hunk lives in PR 5.)* |

- **Commit message:** `Fix SDL3 dependency builds: vendor SDL3_image png/zlib, pin BUILD_SHARED_LIBS, declare the sdl3 vcpkg feature`
- **CHANGELOG:** none (build infrastructure).
- **Validation:** upstream MinGW/nonstatic jobs turn green; `cmake -DUSE_SDL3=ON` configure passes on a clean Windows vcpkg tree.
- **Review note:** each hunk is independent; if maintainers prefer, `vcpkg.json` can be split out — it stands alone.

### PR 2 — `pr/2-wav-audio-robustness`

**Purpose:** 1996-era WAVs (original Diablo SFX) contain zero-length RIFF chunks that
SDL3_mixer 3.2.0 rejects ("no DATA chunk"), and `sound_file_load` then `app_fatal`'d — a config
file's worth of bad audio froze the game. Three layers: patched vendored decoder, canonical-RIFF
re-encode fallback, and graceful degradation with diagnostics.

| File | Type | True Δ | Content |
|------|------|--------|---------|
| `3rdParty/SDL3_mixer/0001-WAV-decoder-skip-zero-length-chunks.patch` | A | new | skip zero-length chunks in the WAV decoder. |
| `3rdParty/SDL3_mixer/CMakeLists.txt` | M | 22 | apply the patch when `find_package(Patch)` succeeds; unchanged fallback otherwise. |
| `CMake/Definitions.cmake` | M | +1 | add `DEVILUTIONX_WAV_FALLBACK` to the option-definition list. |
| `CMakeLists.txt` | M | +1 | `option(DEVILUTIONX_WAV_FALLBACK … ON)` (inserted before `STREAM_ALL_AUDIO_MIN_FILE_SIZE`). |
| `Source/engine/sound.cpp` | M | 27 | archive-aware load-failure diagnostics; `sound_file_load` returns an empty sample instead of `app_fatal` (playback paths skip `!IsLoaded()` samples). |
| `Source/utils/soundsample.cpp` | M | 146 | `DEVILUTIONX_WAV_FALLBACK`: rebuild a canonical 44-byte RIFF/WAVE around `SDL_LoadWAV_IO` PCM and retry; `SetChunk` gains `assetPath`/`archivePath` diagnostics params. |
| `Source/utils/soundsample.h` | M | 6 | declarations. |
| `Source/mpq/mpq_reader.hpp` | M | +2 | `MpqArchive::path()` accessor (names the archive in diagnostics). |
| `docs/CHANGELOG.md` | M | +1 | under `### Bug Fixes → #### Graphics / Audio`: `- Recover 1990s-era WAV sound effects that SDL3_mixer 3.2.0 cannot parse (zero-length RIFF chunks): skip empty chunks in the vendored decoder and add an SDL_LoadWAV_IO-based fallback (DEVILUTIONX_WAV_FALLBACK, default ON); undecodable sounds now log an error instead of aborting the game` |

- **Commit message:** `Audio: recover WAVs SDL3_mixer cannot parse (zero-length RIFF chunks) and stop fatal-erroring on undecodable sounds`
- **Validation:** upstream Linux SDL2/SDL3 test jobs; manual: play Diablo SFX from spawn.mpq.

### PR 3 — `pr/3-robustness-guards`

**Purpose:** the two ROB-27 guards + a one-line log fix. The guards make menu-time gamepad art
construction safe when item-cursor art isn't loaded — the exact window opened by the scaler's
shader browser in the main menu, but a general correctness fix.

| File | Type | True Δ | Content |
|------|------|--------|---------|
| `Source/cursor.cpp` | M | +8 | `GetInvItemSize` returns `{0,0}` when `pCursCels` is empty (was a null CLX deref → CTD). |
| `Source/controls/touch/renderers.cpp` | M | +8 | `LoadPotionArt` skips the strip on zero-size cursor art instead of failing the session. |
| `Source/capture.cpp` | M | 2 | `LogError("…at {}: {}", fileName, result.error())` — format string had one `{}` for two args. |

- **Commit message:** `Guard menu-time item-cursor art access and fix the screenshot failure log format`
- **CHANGELOG:** optional (Bug Fixes → Stability / Performance / System); default off to keep the PR tiny.
- **Note:** PR 5's shader-browser flow is the reproducer; merging PR 3 before/with PR 5 is recommended, not required.

### PR 4 — `pr/4-test-missing-asset-guards`

**Purpose:** `UITest::SetUp()` `GTEST_SKIP`s when MPQ fixtures are absent, but TearDown and the
derived `SetUp()` halves still ran cleanup/init on never-initialised engine state. Adds early
returns behind `MissingAssets()`. (Upstream CI supplies spawn.mpq so it never trips there; this
hardens asset-less environments — honest expectation: low upstream priority, zero risk.)

| File | Type | True Δ |
|------|------|--------|
| `test/ui_test.hpp` | M | +9 |
| `test/char_panel_test.cpp`, `game_menu_test.cpp`, `inventory_ui_test.cpp`, `panel_state_test.cpp`, `spell_ui_test.cpp`, `stash_test.cpp`, `store_transaction_test.cpp`, `visual_store_test.cpp` | M | +5 each |

- **Commit message:** `Skip UI test setup/teardown halves when MPQ fixtures are missing`
- **CHANGELOG:** none.

### PR 5 — `pr/5-gpu-scaler` (the feature; stacked on PR 1)

**Purpose:** optional Vulkan GPU scaler rendering the game through libretro slang shader presets
via the MIT-licensed `libslang-vk` (FetchContent, pinned). One coherent feature: adapter,
options/UI, presentation integration, build wiring, docs, QA corpus.

**Base branch:** `pr/1-sdl3-build-fixes` (set as the GitHub PR base). Rationale: the scaler's
Windows/vendored-SDL3 configure path hits exactly the CMP0002/`BUILD_SHARED_LIBS` issues PR 1
fixes; without it the stacked CI cannot go green.

**Adds (34, attribution headers stamped per Task 4 rules — `//` C++/test, `#` py & sh-after-shebang, `rem` cmd):**

- `Source/engine/render/gpu/gpu_scaler.cpp`, `gpu_scaler.hpp`, `gpu_log_sanitize.hpp`
- `Packaging/linux/clear_shader_cache.sh` *(exec bit +x)*, `Packaging/linux/install_shaders.sh` *(exec bit +x)*
- `Packaging/windows/clear_gpu-scaler.log.cmd`, `clear_shader_cache.cmd`, `install_git.cmd`, `install_shaders.cmd` *(CRLF kept — cmd.exe requires it)*
- `docs/GPU_SHADERS.md`, `docs/SLANG_SPEC.md`
- `test/fixtures/preset_smoke/**` (21 files: 6 good presets, 8 good shaders, 1 LUT texture, 5 malformed presets, 1 broken shader)
- `test/options_test.cpp` (TEST-03: shader-filter options layer + ROB-09 corrupt-ini recovery; stages its own fixtures at runtime)
- `tools/audit_slang_presets.py` (preset QA tool; cross-referenced by SLANG_SPEC.md)

**Mods (12):**

| File | True Δ | Content |
|------|--------|---------|
| `CMakeLists.txt` | ~+52 | `USE_GPU_SCALER` dependent option (ON for SDL3, OFF otherwise) + `GPU_SCALER_SHARED` + libslang-vk FetchContent pin `b91b0499`. *(r4: fork blob @ `78113c7c6` minus the PR-2 WAV option line.)* |
| `Source/CMakeLists.txt` | +30 | gpu_scaler source + `slangvk`/`slv-volk` link plumbing for `libdevilutionx_options` and `libdevilutionx`. |
| `CMake/Tests.cmake` | +18 | register `options_test` + its link block (built **on top of PR 1's Tests.cmake**). |
| `Source/options.cpp` | 392 | `ShaderFilter` enum + `OptionEntryShaderFilter(Preset)` (preset browser: rescan, stale-value self-heal, ini round-trip) + ROB-09 corrupt-ini recovery (preserve-and-restart). **Re-normalized to upstream CRLF — the r3 whole-file rewrite was an EOL artifact.** |
| `Source/options.h` | 105 | option declarations; forward-declares `slv::preset::PresetEntry` to keep slangvk headers out of the game-wide include. |
| `Source/utils/display.cpp` | 218 | scaler window/swapchain lifecycle: Vulkan-capable window creation, `RecreateMainRenderWindow` (DXGI one-flip-swapchain-per-HWND), handback/ROB-14/ROB-22/ROB-26 fixes, vsync dispatch (ROB-31 callback-slot overwrite). |
| `Source/engine/dx.cpp` | 22 | `RenderPresent` scaler path + failure fallback + shutdown. |
| `Source/engine/render/scrollrt.cpp` | +5 | `DrawFilterOsd` hook. |
| `Source/interfac.cpp` | +10 | `GPU_SCALER_LOG_PHASE` load-phase diagnostics. |
| `Source/DiabloUI/diabloui.cpp` | +4 | UI-surface filter OSD. |
| `Source/DiabloUI/settingsmenu.cpp` | 16 | list-navigation entries (folder drill-down) + `RescanCustomEntries()` on menu open. |
| `docs/CHANGELOG.md` | +1 | under `### Features → #### Graphics / Audio`: `- Optional Vulkan GPU scaler: render through libretro slang shader presets via the MIT-licensed libslang-vk library (new USE_GPU_SCALER CMake option, on by default for SDL3 builds, graceful fallback to stock rendering)` |

- **Commit message:** `Optional Vulkan GPU scaler: slang shader presets via libslang-vk (USE_GPU_SCALER)`

  Body: adapter delegates preset parsing/translation/SPIR-V compilation/Vulkan pass chain to
  libslang-vk (MIT, pinned FetchContent, volk runtime loading — no Vulkan SDK at build time);
  graceful fallback to the stock renderer on any GPU-path failure; OSS strings for the filter
  OSD; Packaging scripts install/clear shader caches.
- **CHANGELOG correction vs r3:** the option defaults **ON for SDL3 builds** (`cmake_dependent_option`), not "default OFF" — bullet wording above fixes r3's inaccuracy. *Variant:* if maintainers prefer opt-in, flip the dependent-option default to OFF (1 line) and revert the bullet clause.
- **Validation:** upstream CI on the stacked branch (Linux SDL3 job compiles the scaler; tests exercise `options_test`); fork dev-branch (`libslang-vk` + `gpu-scaler-build.yml`) remains the Windows dogfood runner.
- **Lean variant (if reviewers want a smaller diff):** drop docs/GPU_SHADERS.md + SLANG_SPEC.md + 21 fixtures + tools/ (−24 files; core PR = 22 files) into a follow-up PR 7. Core feature stays reviewable.

### PR 6 *(optional)* — `pr/6-sdl3-main-glue`

**Purpose:** upstreamable form of fork commit `539f73aef`. SDL3's `SDL.h` no longer includes
`SDL_main.h`; without it, WIN32 GUI-subsystem executables (`add_executable(devilutionx WIN32)`)
fail to link (`LNK2019 unresolved external symbol WinMain`). One include + one comment line;
`SDL_main.h` is a no-op on Linux/macOS desktop.

| File | Type | True Δ |
|------|------|--------|
| `Source/main.cpp` | M | +3 (2-line comment + `#include <SDL3/SDL_main.h>`, inserted after the `SDL3/SDL.h` include; the fork's stray blank line is dropped) |

- **Commit message:** `SDL3: include SDL3/SDL_main.h in main.cpp so WIN32 GUI builds link`
- **Note:** r4 keeps the long explanatory comment out of the upstream PR (the fork branch keeps the verbose version).

---

## 3. Excluded from every PR (fork-only inventory)

| Item | Reason |
|------|--------|
| `Source/diablo.cpp`, `Source/diablo.h` | VTSTech branding (`gszProductName` rewrite + [128] bump that only serves it). |
| `Source/main.cpp` blank line | cosmetic junk (the SDL3 include is PR 6). |
| `.gitignore` `*.bak` | personal housekeeping. |
| `.github/workflows/gpu-scaler-build.yml` | fork-only CI gate (r3 §6.4 never-rides rule). |
| `docs/CHANGELOG.md` fork rewrite | replaced by per-PR one-bullet edits of the upstream file. |
| Attribution headers on fork-only files | remain on the fork branch only where the file itself is fork-only. |

## 4. Mechanics

**Builder (re-runnable):** `build_r4_branches.py` (delivered beside this plan).

```bash
cd <your DevilutionX clone>          # VTSTech fork; branch state does not matter
git remote add upstream https://github.com/diasurgical/DevilutionX.git  # if absent
git fetch upstream master
git fetch origin libslang-vk          # provides 6dc2a8713 / 78113c7c6 blobs
python3 build_r4_branches.py          # creates pr/1…pr/6 locally + verifies each
git push origin pr/1-sdl3-build-fixes pr/2-wav-audio-robustness pr/3-robustness-guards \
               pr/4-test-missing-asset-guards pr/5-gpu-scaler pr/6-sdl3-main-glue
```

Then open PRs 1–4 (+6) against `diasurgical:master`; open PR 5 against **your own**
`pr/1-sdl3-build-fixes` branch ("base:" dropdown → repositories may select a branch of the same
fork). As PR 1 merges, GitHub retargets PR 5 to `master`; rebase PR 5 only if conflicts appear
(shared files' hunks are disjoint, so conflicts are unlikely).

**EOL rules (enforced by the builder, obey in manual edits):** `.gitattributes` is `* -text` —
EOLs are content. Unchanged lines keep the upstream file's exact bytes; new/changed lines take
that file's dominant upstream EOL; `.cmd` stays CRLF. Never re-save CRLF files with an LF editor
(that is what inflated r3's `options.cpp` 392→3766).

## 5. Per-PR acceptance gates

```bash
BR=pr/5-gpu-scaler; BASE=pr/1-sdl3-build-fixes     # example; PR 1–4/6 use BASE=upstream/master
git diff --name-status $BASE..$BR | grep -c '^D'                     # must be 0
git diff --name-only  $BASE..$BR | grep -E '^\.github/'              # must be empty
git diff $BASE..$BR -- Source | grep 'github.com/VTSTech'            # must be empty (branding)
git diff --stat       $BASE..$BR                                     # matches §2 sizes
git log --oneline $BASE..$BR | wc -l                                 # must be 1
```

Plus, for every PR: upstream CI green on the PR itself (automatic via
`pull_request: [opened, synchronize]`); PR 5 additionally smoke-tested on the fork dev branch
(`gpu-scaler-build.yml`: Windows MSVC + Linux x64, `--version` + ctest).

## 6. Risk register

| Risk | Likelihood | Mitigation |
|------|------------|------------|
| Upstream balks at FetchContent from `VTSTech/libslang-vk` (third-party pin in their CI) | medium | MIT license, SHA-pinned, no SDK requirement, graceful OFF fallback; fallback plan: vendor as `3rdParty/slang-vk` submodule or offer `USE_GPU_SCALER` default-OFF variant. |
| `options.cpp` 392-line diff is the biggest review surface | medium | ROB-09 corrupt-ini recovery is cleanly separable on request; option classes are additive. |
| PR 4 seen as unnecessary upstream (their CI has MPQs) | medium | present as hardening; it is independent — dropping it costs nothing. |
| Attribution headers questioned upstream | low/medium | user's Task-4 call: keep (provenance) or strip on request; headers live only in PR 5's new files. |
| Squash-merge of PR 1 | none | GitHub content-compares; PR 5 retargets cleanly. |
| EOL churn reintroduced by local editors | medium | builder re-derives everything from blobs; run it, don't hand-edit. |

## 7. Suggested reply to maintainers

> Thanks — split into individual changes, one branch/PR each, ordered by dependency:
> 1. SDL3 dependency build fixes (needed by the scaler's CI) · 2. WAV/audio robustness ·
> 3. menu-time crash guards · 4. test hardening for asset-less environments ·
> 5. the GPU scaler itself (stacked on #1; its base branch is set so the diff shows only the
> feature) · 6. SDL3 `SDL_main.h` include fix. Each is a single commit with zero deletions;
> per-PR CHANGELOG bullets included where relevant.

---

## Appendix A — r3 → r4 deltas

1. Single 73-file PR → six individual PRs (hunk-level curation of `CMakeLists.txt` and `Tests.cmake`).
2. **Branding leakage fixed:** `diablo.cpp` / `diablo.h` / `main.cpp` blank line / `.gitignore` excluded (r3 payload carried them).
3. **EOL normalization added** (`options.cpp`, `vcpkg.json`, `3rdParty/SDL3_image/CMakeLists.txt`, 2 stray-LF lines in `Tests.cmake`) — true diff shrinks ~1780 lines.
4. CHANGELOG: one bullet at merge time → per-PR bullets inside each PR.
5. `Tests.cmake` stray EOF blank line dropped.
6. r3's cleanup/lean tier gates (§3–§4 of that plan) remain the foundation for the *dev branch*; r4 governs PR submission only. The r3 overlay zip remains the "everything" snapshot but is no longer the PR vehicle.

## Appendix B — file→PR assignment (exhaustive, for tooling)

```text
PR1  M  3rdParty/SDL3_image/CMakeLists.txt   (CRLF-normalized)
PR1  M  CMake/Dependencies.cmake
PR1  M  3rdParty/libpng/CMakeLists.txt
PR1  M  vcpkg.json                            (CRLF-normalized)
PR1  M  CMake/Tests.cmake                     (gmock hunk only)
PR2  A  3rdParty/SDL3_mixer/0001-WAV-decoder-skip-zero-length-chunks.patch
PR2  M  3rdParty/SDL3_mixer/CMakeLists.txt
PR2  M  CMake/Definitions.cmake
PR2  M  CMakeLists.txt                        (WAV option hunk only)
PR2  M  Source/engine/sound.cpp
PR2  M  Source/utils/soundsample.cpp
PR2  M  Source/utils/soundsample.h
PR2  M  Source/mpq/mpq_reader.hpp
PR2  M  docs/CHANGELOG.md                     (Bug Fixes bullet)
PR3  M  Source/cursor.cpp
PR3  M  Source/controls/touch/renderers.cpp
PR3  M  Source/capture.cpp
PR4  M  test/ui_test.hpp
PR4  M  test/{char_panel,game_menu,inventory_ui,panel_state,spell_ui,stash,store_transaction,visual_store}_test.cpp
PR5  A  Source/engine/render/gpu/{gpu_scaler.cpp,gpu_scaler.hpp,gpu_log_sanitize.hpp}   [stamped]
PR5  A  Packaging/linux/{clear_shader_cache,install_shaders}.sh                          [stamped, +x]
PR5  A  Packaging/windows/{clear_gpu-scaler.log,clear_shader_cache,install_git,install_shaders}.cmd  [stamped, CRLF]
PR5  A  docs/{GPU_SHADERS,SLANG_SPEC}.md
PR5  A  test/fixtures/preset_smoke/** (21)
PR5  A  test/options_test.cpp                 [stamped]
PR5  A  tools/audit_slang_presets.py          [stamped]
PR5  M  CMakeLists.txt                        (scaler block only, @78113c7c6 minus WAV line)
PR5  M  Source/CMakeLists.txt
PR5  M  CMake/Tests.cmake                     (options_test hunk, on top of PR1)
PR5  M  Source/options.cpp                    (CRLF-normalized)
PR5  M  Source/options.h
PR5  M  Source/utils/display.cpp
PR5  M  Source/engine/dx.cpp
PR5  M  Source/engine/render/scrollrt.cpp
PR5  M  Source/interfac.cpp
PR5  M  Source/DiabloUI/diabloui.cpp
PR5  M  Source/DiabloUI/settingsmenu.cpp
PR5  M  docs/CHANGELOG.md                     (Features bullet)
PR6  M  Source/main.cpp                       (SDL_main.h include only)
—    X  Source/diablo.cpp, Source/diablo.h, .gitignore, .github/workflows/gpu-scaler-build.yml  (never)
```
