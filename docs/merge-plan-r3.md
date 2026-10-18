# Merge Plan (r3) — Clean-Fork Submission of the GPU Shader Pipeline → diasurgical/DevilutionX

| | |
|---|---|
| Status | Draft r3 — supersedes the fork-side prerequisites in r2 (§7); ladder in r2 §4 is re-cut in §5 of this document |
| Date | 2026-10-18 (America/Toronto) |
| DvX-side owner | this session (DevilutionX fork/CI/config work) |
| Library-side owner | libslang-vk agent |
| Fork ops | VTSTech |
| Field state | build 1.6.0-dev-Release-6d1c770e4 (Oct 18 2026 01:03:27) play-tested on RX 560: 'pixel aa fast' 2-pass preset, ~22.7k frames / 0 skipped, ZeroTier public game created + normal exit |

---

## 0. What changed in r3

r1/r2 solved merging the library **into** the fork. That is done and field-proven.
r3 solves getting the work **out** — because the fork, as it stands, cannot produce a
clean upstream PR:

1. `libslang-vk` carries **194 fork-side commits**, almost all tiny web-edit commits
   ("Update CMakeLists.txt", "Update CI", …). No squash performed *inside* the fork
   changes what an upstream PR shows, because a PR's commit list is computed by
   reachability (§2), not by how the tip was produced.
2. `VTSTech:master` is now burned as a PR base too: `fc974331` ("Merge
   remote-tracking branch 'upstream/master'", 2026-10-18) is **0 commits behind**
   diasurgical:master but **197 files different**. Any branch cut from it inherits a
   197-file phantom diff. This is where the "145 changed files" sighting came from
   (§2.2) — not from the payload itself.
3. The architecture moved since r2 was written: the preset parser, slang→GLSL
   translator, compiler bridge and chain runner now live in the **libslang-vk
   library** (MIT, fetched at configure time, pinned
   `b78676d303ba585712f10c3e3190eb8713435989`, comment: "M4 + cmake floor + preset
   pass > 32"). The DevilutionX-side upstreamable surface therefore **shrank** to a
   single verified payload of **73 files, +6,300/−1,857, zero deletions** vs current
   upstream master — build glue, the Vulkan scaler engine side, options/UI, tests,
   docs, packaging (§3).

**r3 in one sentence:** archive the old fork, fork fresh, apply the verified 73-file
payload on a branch cut from *current* diasurgical:master, gate it (§4.4), then open
the submission either as the r2 ladder re-cut (§5) or as the single coherent PR
(§5.4) — maintainer's choice, and we ask them first (§6).

---

## 1. Ground truth (measured 2026-10-18, facts this plan builds on)

| Ref | What it is | Key numbers |
|---|---|---|
| `diasurgical/DevilutionX:master` @ `0011dc96` | upstream tip ("CI: Fix packages", 2026-10-17) | the only valid PR base |
| `VTSTech:libslang-vk` @ `6d1c770e` | development branch, **strict superset** of upstream (0 commits behind) | 194 fork-side commits; raw tree diff 178 files, +31,095/−5,768 |
| `VTSTech:master` @ `fc974331` | fork master after merging upstream in | 197 files vs upstream, 0 commits behind — **do not branch PRs from this** |
| `gpu-scaler-upstream` @ `c514bab2` | **the payload**: single commit, parent = `0011dc96`, produced by squash-merge + curation | **73 files, +6,300/−1,857, 0 deletions**; every file byte-verified against the commit blob |
| libslang-vk pin | `b78676d303ba585712f10c3e3190eb8713435989` | MIT; fetches its own glslang at configure time; no Vulkan SDK requirement beyond headers |

Why the raw diff (178 files) and the payload (73 files) differ is the exclusion
ledger in §3.2. Nothing of the feature was dropped — what was cut is fork
housekeeping and session/audit scaffolding that r2's noise-control policy (§6)
always said must never ride upstream.

---

## 2. Why squashing inside the fork can't work — and what the 145 actually was

### 2.1 The reachability rule

When GitHub renders a PR `head → base`, the commit list is *every commit reachable
from the head branch that is not reachable from the base branch*. Consequences:

- Squash-merging `libslang-vk` into fork master creates one new commit **on top of**
  history that still contains all 194 commits — they stay reachable from the branch
  and from every other ref, so the PR list never shrinks. "History didn't go away"
  is the expected outcome, not a failed operation.
- The only way to change what a PR shows is to **re-parent**: create a commit whose
  ancestor chain starts at the base. `c514bab2` does exactly that — its parent is
  upstream's tip, so a PR from it shows **one commit**.

### 2.2 Base drift is the second failure mode

The *diff* a PR shows is head-tree vs base-tree. If the working branch is cut from
anything other than current `diasurgical:master`, every file the fork's base is
missing or stale on shows up as a phantom change (or worse, silently reverts
upstream's newer work). Concretely:

- New branch from `VTSTech:master` (`fc974331`, 197 files vs upstream) → even a
  perfect overlay lands on a base that is already 197 files "wrong"; GitHub's
  compare vs diasurgical:master shows those files as reverts/changes.
- Any reported changed-file count that is not exactly **73** means either the base
  wasn't current upstream master or the overlay applied wasn't the curated one.

**Acceptance criterion (use it as a gate, not a vibe):** after import (§4.3), the
branch's Changes list must show **exactly 73 files, 0 deletions**, and the commit's
parent must equal diasurgical:master's tip *at PR time* (§7 covers drift).

---

## 3. The payload ledger

### 3.1 What ships — 73 files, by group

| Group | Files | Content |
|---|---|---|
| Engine integration | 22 (`Source/`) | `engine/render/gpu/gpu_scaler.cpp/.hpp`, `gpu_log_sanitize.hpp` (Vulkan device/swapchain/present side), integration hooks in `scrollrt`, `display`/`dx` seam, `main`, `diablo.cpp/.h`, `interfac`, `capture`, `cursor`, options surface (`options.cpp/.h`), DiabloUI (`diabloui`, `settingsmenu`), touch renderer path, `sound`/`soundsample` (SDL3_mixer glue), `mpq_reader.hpp` |
| Build glue & deps | 9 | `CMakeLists.txt` (+53: `USE_GPU_SCALER` option + libslang-vk FetchContent), `CMake/Definitions.cmake`, `CMake/Dependencies.cmake` (incl. the root-scope `BUILD_SHARED_LIBS=OFF` pin that fixed the libzt cascade), `CMake/Tests.cmake`, `3rdParty/SDL3_image` glue, `3rdParty/SDL3_mixer` glue + vendored WAV patch, `3rdParty/libpng` glue, `vcpkg.json`, `.gitignore` |
| Test corpus | 31 (`test/`) | 22 preset-smoke fixtures (`good`/`malformed` packs incl. shaders + LUT texture) + 8 UI/options test updates + `ui_test.hpp` |
| Docs | 3 | `GPU_SHADERS.md` (user/admin doc), `SLANG_SPEC.md`, one-bullet `CHANGELOG.md` entry under Unreleased/Features |
| Packaging | 6 | shader install/clear helpers for Windows (.cmd) and Linux (.sh) |
| Tooling | 1 | `tools/audit_slang_presets.py` — preset-corpus validator |

### 3.2 What never ships — exclusion ledger (verified absent from the payload)

| Excluded | Why |
|---|---|
| 31 deleted upstream CI workflows | fork housekeeping; deleting upstream's CI matrix in a PR is dead on arrival |
| `.github/workflows/gpu-shaders.yml` | fork-specific CI (nonstatic jobs, gates, strip); CI wiring is discussed with maintainers, not smuggled in (§6) |
| `clang-format-check.yml` modification | checkout-action pin downgrade, unrelated |
| `README.md` rebrand | fork identity, not upstream content |
| `docs/CHANGELOG.md` fork rewrite + `CHANGELOG_UPSTREAM.md` (1,816-line copy) | fork release ledger; replaced by the one-bullet entry |
| `tools/audit/**` (49 files), `docs/audit*`, `brief.md`, `deltas.md`, `diablo-handoff.md`, `libslang-vk-plan.md`, `merge-plan-draft-r1/r2.md`, `slang_audit_v2.zip` | session/audit scaffolding — r2 §6 noise policy: audit artifacts stay private, they never ride PRs |
| 3 upstream-file deletions the fork carried (`CMake/platforms/*-pkg-config`, `Packaging/windows/mingw-prep64.sh`) | not part of the feature; restored to upstream state |

### 3.3 The "bloat" question, answered

The 145-file sighting was **not** this payload — it was base drift plus
non-curated content (§2.2). The curated 73 already have all audit/session bloat
removed. What remains beyond the hard core is deliberate and each piece has a
defensible upstream story:

- **Tests (31 files)** — r2 called the smoke corpus "the full-pipeline regression
  lock" and AJenbo-class review trusts tests far more than prose. Cutting them
  saves ~30 files but costs credibility. **Recommendation: keep.**
- **Docs (2 + changelog bullet)** — `GPU_SHADERS.md`/`SLANG_SPEC.md` are the
  feature's user contract. **Keep.**
- **`tools/audit_slang_presets.py` + fixtures** — corpus validator, one file.
  **Keep.**
- **Optional lean tier** (only if a maintainer complains about size): drop
  `test/`, `docs/GPU_SHADERS.md`, `docs/SLANG_SPEC.md`, `tools/` → 38 files.
  This is a fallback concession, not the opening position.

---

## 4. New-fork procedure (GUI-first)

### 4.0 Archive the old fork (do not delete it blind)

The `libslang-vk` branch is the development record — 194 commits including the
library agent's M0–M4 milestones, the audit-driven fix trail, and the CI history.
That history is worth keeping even though it can never be a PR.

1. Rename `VTSTech/DevilutionX` → `VTSTech/DevilutionX-libslang-archive`
   (Settings → General → Rename; GitHub redirects old URLs, so nothing breaks).
2. Optionally mark the archive fork private — it is a workshop, not a shop window.

### 4.1 Fresh fork + fresh clone

1. Fork `diasurgical/DevilutionX` → this becomes the new `VTSTech/DevilutionX`.
   It starts with upstream's branches and zero junk history.
2. Clone it fresh with GitHub Desktop (a full clone on your machine — no partial
   clone caveats).
3. In Desktop: Fetch → the branch list now contains upstream's branches.

### 4.2 Import the payload — three routes, pick one

| Route | Tooling | Artifact |
|---|---|---|
| Overlay zip (GUI-only, recommended) | Desktop + Explorer | `DevilutionX-1.6.0-dev-c514bab2-overlay.zip` (275 KB, 73 files, folders preserved, every entry byte-verified against the commit) |
| Bundle (1 CLI command) | `git fetch DevilutionX-gpu-scaler-upstream.bundle gpu-scaler-upstream:gpu-scaler-upstream` | `DevilutionX-gpu-scaler-upstream.bundle` (98 KB, prerequisite = upstream tip) |
| Patch | `git am 0001-Add-Vulkan-slang-shader-GPU-scaler-libslang-vk.patch` | `gpu-scaler-upstream-patch/` (386 KB, binary-safe) |

Overlay-zip flow in detail:

1. Desktop → click `upstream/master` in the branch list → check it out.
2. `Branch → New branch` → `gpu-scaler-upstream`.
3. Extract the zip over the repo root (Explorer: merge + replace).
4. The Changes tab must now show **exactly 73 files** — count it (§4.4).
5. Commit. Subject + body: paste `gpu-scaler-upstream-commit-message.txt`
   (subject in the summary box, body in the description box).
6. `Push origin`.

### 4.3 Commit identity

The prepared commit is authored `VTSTech <VTSTech@users.noreply.github.com>`.
If you prefer a different identity, amend the author before pushing — cosmetic,
but PRs render the author prominently.

### 4.4 Acceptance gates (all five must pass)

| # | Gate | How to check |
|---|---|---|
| 1 | Exactly 73 changed files, **0 deletions** | Desktop Changes tab after extract; then History → the commit's file list |
| 2 | Commit parent == current `diasurgical:master` tip | History: the commit must sit directly on the freshly fetched upstream tip (if upstream moved, see §7.1) |
| 3 | Single commit, no merge commits | History tab |
| 4 | No `.github/`, `tools/audit/`, `docs/audit*` entries anywhere in the diff | eyeball the commit's file list against §3.2 |
| 5 | PR compare shows 73 files / 1 commit | `github.com/diasurgical/DevilutionX/compare/master...VTSTech:DevilutionX:gpu-scaler-upstream` |

---

## 5. From payload to submission — two shapes, ask maintainers first

r2's ladder was written before the library absorbed the parser/translator/compiler/
runner. Re-cut against today's architecture, the DvX-side series becomes:

### 5.1 Updated ladder (r3 re-cut of r2 §4)

| PR | Content | Files (from the 73) | Risk |
|---|---|---|---|
| **A — Build/dep foundation** | libslang-vk FetchContent + `USE_GPU_SCALER` option (default **OFF**, §6.3) + dependency glue | `CMakeLists.txt`, `CMake/*` (3), `3rdParty/*` (4), `vcpkg.json`, `.gitignore` | inert: flag off = no behavior change |
| **B — Display seam** | minimal `display`/`dx` refactor so a second present backend can plug in | `Source/utils/display.cpp`, `Source/engine/dx.cpp` | small by design; lands alone, gets scrutinized alone |
| **C — Scaler core** | `gpu_scaler.cpp/.hpp`, `gpu_log_sanitize.hpp`, hooks in `scrollrt`/`main`/`diablo`/`interfac`/`capture`/`cursor`/`mpq_reader`/`sound`/`soundsample`, touch renderer | rest of `Source/` | the feature lands opt-in; stock path still default |
| **D — Corpus + tests** | preset-smoke fixtures + validator + UI test updates | `test/` (31), `tools/audit_slang_presets.py` | the regression lock |
| **E — UI/docs/packaging** | settings browser surface, `GPU_SHADERS.md`, `SLANG_SPEC.md`, changelog, install/clear scripts | `Source/DiabloUI/*`, `docs/*`, `Packaging/*` (minus what A/C already took), `CHANGELOG.md` bullet | end state; default flips to ON only here, if maintainers want it |

Note: A–E group cleanly **by file** with one exception — files like
`options.cpp/.h`, `scrollrt.cpp`, `diablo.cpp` carry both seam-era and feature-era
hunks; splitting them by hunk (not by file) is the only surgery needed, and only
if the ladder route is chosen. `gpu-shaders.yml`-style CI rides in no rung
initially (§6.4).

### 5.2 The library's own upstream story (libslang-vk agent's lane)

Upstream will not `FetchContent` a personal repo blindly. Prepared options, in
order of preference:

1. **Tag a stable release** of libslang-vk (`v1.0.0-m4` or similar) + changelog;
   the PR pins the tag, not a floating hash. *(library agent — action item)*
2. **Vendoring** if maintainers insist on in-tree code: import as
   `3rdParty/libslang-vk` subtree with LICENSE intact — a separate, mechanical PR
   with zero DvX-code changes. *(library agent prepares the import doc)*
3. **Separate upstream submission of libslang-vk itself** as a standalone project
   (its own repo/reviews) — only worth it if maintainers want the machinery
   reusable beyond DevilutionX.

License facts to state up front: libslang-vk is MIT; it fetches glslang (Apache-2.0
with LLVM exception) **at build time** — nothing is linked into the shipped binary
from glslang's repo at rest; the compiled SPIR-V is data.

### 5.3 Sequencing rule (unchanged from r2 §2)

Every PR in the series: green CI on upstream's runners, no audit riders, no CI-yml
changes, no "while I'm here" fixes outside the declared scope.

### 5.4 Single-PR fallback

If maintainers prefer reviewing one coherent, opt-in feature (it is *one* feature
with a flag), `c514bab2` **is** that PR: 73 files, +6.3k/−1.9k, zero deletions,
tests included. r2's "no 18k trust-me-bro" condition is satisfied differently: the
trust is bought with the evidence package (§6.2) and the corpus, not with commit
count. Let the maintainer pick the shape in the issue thread before pushing
anything beyond the first branch.

---

## 6. PR-readiness gates (before anything carries a PR label)

### 6.1 clang-format 18

Upstream CI enforces `clang-format-version: '18'` on every PR. Run it over all
changed `Source/` (and ideally `test/`) files **before** the first push:

```
clang-format-18 -i <changed Source/ test/ files>
```

This is the single most likely "red in 3 minutes" failure. Do it on the branch
before push; Desktop re-commits the formatting as part of the same commit via
amend, or as a tiny follow-up commit if the PR is already open.

### 6.2 Issue-first evidence package (opens the conversation, not a PR)

A render-path feature lands or dies in the issue thread. Prepare a post containing:

- what it is (one paragraph + `GPU_SHADERS.md` link),
- demo evidence: the RX 560 playtest log excerpt (preset load, ~22.7k frames /
  0 skipped heartbeats), ZeroTier multiplayer session (online → READY_IP6 →
  public game → normal exit), build id `1.6.0-dev-Release-6d1c770e4`,
- the two shapes (§5.1 ladder vs §5.4 single PR) and the question "which do you
  want to review?",
- the vendoring question (§5.2) stated plainly, with the tag ready,
- license summary (MIT lib, glslang build-time only).

### 6.3 Prepared answers to the predictable questions

| Question | Prepared answer |
|---|---|
| "Is it default-on?" | In the fork, yes (SDL3 builds). **For upstream, the payload's option default flips to OFF** for the submission (one-line change in `CMakeLists.txt`) until the ladder's end state — r2's own rule. Ship the flip with PR-A. |
| "Does it patch SDL?" | No system SDL is touched. The vendored `3rdParty/SDL3_mixer` carries one WAV-decoder hardening patch (zero-length chunk skip) — same pattern upstream already uses for vendored deps. |
| "Vulkan everywhere?" | `USE_GPU_SCALER` is OFF unless SDL3 && !SDL1; platforms without Vulkan keep stock rendering. Graceful fallback: any scaler failure hands back the stock path. |
| "What about libzt/static-link regressions?" | The `BUILD_SHARED_LIBS=OFF` root pin is included; field-proven: ZeroTier multiplayer sessions work on the nonstatic build (§0 field state). |
| "Hellfire?" | Unaffected; the scaler sits below the game-logic layer (r2 §1.2 condition holds). |

### 6.4 CI

`gpu-shaders.yml` stays fork-only. If maintainers want feature CI, offer a minimal
upstream-shaped workflow later (build + ctest with the smoke corpus) — as its own
PR, on their ask, not preemptively.

---

## 7. Contingencies

### 7.1 Upstream master moves before the PR opens

Re-run the curation on the new tip: fetch upstream, cut the branch again,
`git merge --squash <imported payload branch>`, re-apply the exclusion restores
(the procedure is fully scripted on the DvX side:
`scripts/build_overlay_zip_c514bab2.sh` documents the file set; the restore list
is §3.2). Re-verify the five gates. Never rebase a pushed PR branch by force
unless a maintainer asks.

### 7.2 Maintainers request changes to the split

§5.1's file-group map is the cutting guide. Only `options`/`scrollrt`/`diablo`
need hunk-level staging; everything else splits by file.

### 7.3 Maintainers want the library vendored

Freeze the pin (`b78676d3` → tagged release), library agent produces the
`3rdParty/libslang-vk` import, DvX PR-A switches from FetchContent to the vendored
tree. Mechanical, zero engine-code delta.

### 7.4 Review stalls / scope pushback

The exit ramps from r2 still hold: PR-B (seam) is upstream-useful even if the GPU
track dies; PR-A's dependency glue is inert; the corpus is reusable by any future
shader system. Nothing in the series is a dead end if later rungs are rejected.

---

## 8. Division of labor

| Owner | Items |
|---|---|
| **VTSTech** | fork ops (§4.0–4.1), import + push (§4.2), clang-format run (§6.1), issue post (§6.2), PR/discussion presence |
| **DvX session (this one)** | payload maintenance on upstream drift (§7.1), ladder re-cut surgery (§7.2), PR description drafts, CI answers, overlay/bundle/patch artifacts |
| **libslang-vk agent** | tag + changelog for the pinned release (§5.2.1), vendoring import doc (§5.2.2), library-surface review responses, `preset pass > 32` follow-ups |

---

## Appendix A — PR description draft (single-PR route)

> **Add Vulkan slang shader GPU scaler (libslang-vk)**
>
> This adds an opt-in presentation scaler that renders DevilutionX through
> libretro slang shader presets on Vulkan, backed by the MIT-licensed
> [libslang-vk](<repo URL>) library (fetched at configure time, pinned to a tagged
> release; it fetches its own glslang at build time — no runtime shader compiler in
> the tree).
>
> - new `USE_GPU_SCALER` CMake option (proposed **default OFF** for upstream; ON
>   for SDL3 builds in our fork where it is field-tested)
> - preset parsing, slang→GLSL translation and chain execution live in the
>   library; DevilutionX gets the device/swapchain/present side, a display seam,
>   settings-menu integration and a 22-pack preset smoke corpus under
>   `test/fixtures/preset_smoke/`
> - graceful fallback: any scaler failure returns to the stock renderer
> - docs: `docs/GPU_SHADERS.md` (users), `docs/SLANG_SPEC.md` (preset semantics)
>
> Field-tested on Windows 10 / Radeon RX 560: 2-pass 'pixel aa fast' preset,
> ~22.7k frames over 9.5 minutes with zero skipped/dest-skipped frames, and a full
> ZeroTier multiplayer session (node online → network ready → public game created
> → normal exit) on the dynamic-link build.
>
> Happy to re-cut as a series (build glue → display seam → scaler → corpus →
> UI/docs) if that reviews better — see the issue thread for the file-group map.

## Appendix B — 73-file manifest (grouped)

```
.gitignore
CMakeLists.txt
vcpkg.json
CMake/Definitions.cmake            CMake/Dependencies.cmake      CMake/Tests.cmake
3rdParty/SDL3_image/CMakeLists.txt
3rdParty/SDL3_mixer/CMakeLists.txt 3rdParty/SDL3_mixer/0001-WAV-decoder-skip-zero-length-chunks.patch
3rdParty/libpng/CMakeLists.txt
Source/CMakeLists.txt
Source/capture.cpp                 Source/cursor.cpp             Source/diablo.cpp
Source/diablo.h                    Source/interfac.cpp           Source/main.cpp
Source/engine/dx.cpp               Source/engine/sound.cpp       Source/engine/render/scrollrt.cpp
Source/engine/render/gpu/gpu_scaler.cpp   gpu_scaler.hpp   gpu_log_sanitize.hpp
Source/utils/display.cpp           Source/utils/soundsample.cpp  Source/utils/soundsample.h
Source/DiabloUI/diabloui.cpp       Source/DiabloUI/settingsmenu.cpp
Source/controls/touch/renderers.cpp  Source/mpq/mpq_reader.hpp
Source/options.cpp                 Source/options.h
docs/CHANGELOG.md                  docs/GPU_SHADERS.md           docs/SLANG_SPEC.md
Packaging/linux/clear_shader_cache.sh   Packaging/linux/install_shaders.sh
Packaging/windows/clear_gpu-scaler.log.cmd   Packaging/windows/clear_shader_cache.cmd
Packaging/windows/install_git.cmd  Packaging/windows/install_shaders.cmd
tools/audit_slang_presets.py
test/char_panel_test.cpp           test/game_menu_test.cpp       test/inventory_ui_test.cpp
test/options_test.cpp              test/panel_state_test.cpp     test/spell_ui_test.cpp
test/stash_test.cpp                test/store_transaction_test.cpp
test/visual_store_test.cpp         test/ui_test.hpp
test/fixtures/preset_smoke/good/{single,lut,multipass,history,feedback,dupinclude}.slangp
test/fixtures/preset_smoke/good/textures/lut.png
test/fixtures/preset_smoke/good/shaders/{passthrough,blend,lut_mix,history,feedback,alias_final,dupinclude,guarded_texture_header}.slang
test/fixtures/preset_smoke/malformed/{empty,bad_count,bad_index,bad_slang,missing_shader}.slangp
test/fixtures/preset_smoke/malformed/shaders/broken.slang
```
(73 files total: 4 root/glue + 3 CMake + 4 3rdParty + 22 Source + 3 docs +
6 Packaging + 1 tools + 30 test — manifest of record:
`scripts/c514bab2-filelist.txt`)

## Appendix C — artifact inventory (all in `download/`)

| Artifact | Purpose |
|---|---|
| `DevilutionX-1.6.0-dev-c514bab2-overlay.zip` | GUI import payload (73 files, verified) |
| `DevilutionX-gpu-scaler-upstream.bundle` | one-command git import |
| `gpu-scaler-upstream-patch/0001-*.patch` | `git am` import route |
| `gpu-scaler-upstream-commit-message.txt` | commit subject + body to paste |
| `merge-plan-r3.md` | this document |

