# Long-Term Merge Plan (r2) — GPU Shader Preset Pipeline → diasurgical/DevilutionX

**Status: discussion document — nothing scaffolded, no branches created, no PRs opened.**
r2, 2026-10-16. Supersedes r1. The ladder itself is unchanged; r2 adds the audit-closure
gate, per-PR readiness gates, and a noise-control policy (§0).
Prepared for VTSTech (fork) and diasurgical maintainers (upstream) to react to.
All numbers measured against upstream master `b0a3b7bab` (2026-09-27, the fork's actual
merge-base; upstream is only 1 commit ahead) and fork tip `0f9e69324`.

---

## 0. What changed in r2 (VTSTech's observations, incorporated)

r1 answered AJenbo's three conditions but treated the fork's audit as background. It
isn't background: the mod carries a live audit ledger — **72 findings to date, 28 closed,
44 open (0 High / 14 Medium / 30 Low)** — and submitting today would mean (a) upstream
reviewing code we already know needs fixes, then (b) review threads accumulating fixup
churn while the audit cycle continues, and (c) fork-only documentation work constantly
threatening to leak into PR diffs. r2 adds three things and changes nothing else about
the ladder:

1. **Per-PR readiness gates (§5).** Every PR now lists the exact open audit findings
   that block it. "More ready today than others" becomes a measurable property:
   PRs 1–3 are one fix-round away, PRs 4–5 are two, PR 6 is three.
2. **Phase 0 — audit-closure gate (§3).** Three bounded fix rounds (8A/8B/8C)
   disposition the entire open set. First contact requires 8A+8B (9 of 14 Medium
   closed; the remaining five — three 8C items plus the two FEAT deferrals — live
   in runner/feature code no early PR touches).
3. **Noise-control policy (§6).** Six rules keeping the audit/docs cycle fork-side
   forever: no fixup tails on open PRs, no ledger files in branches, upstream-wide
   findings become upstream *issues* instead of riders on shader PRs.

## 1. AJenbo's three conditions, with facts (unchanged from r1; kept for standalone reading)

### 1.1 "If we can make it so I don't have to patch SDL"

Measured fact: **the GPU pipeline already runs on upstream's SDL3 unmodified.**
Both repos vendor the identical SDL3 tarball — `libsdl-org/SDL@570f7229`, same URL and
same SHA256 in `3rdParty/SDL3/CMakeLists.txt`. The fork changes **zero** lines in the
SDL3 core.

The **only** SDL patch anywhere in the fork is
`3rdParty/SDL3_mixer/0001-WAV-decoder-skip-zero-length-chunks.patch` — the Hellfire
WAV-audio fix (plus `sound.cpp`/`soundsample.cpp` WAV-fallback code). That is exactly
the part AJenbo already said upstream doesn't need from us. So:

- **The mixer patch and WAV fallback are removed from the merge series entirely.**
  They stay fork-only — or, better home: the zero-length-chunk decoder fix is submitted
  directly to `libsdl-org/SDL_mixer`, which benefits everyone and removes the fork's
  last SDL-side patch.
- One residual to resolve in the backend PR: the fork simplifies
  `3rdParty/SDL3_image/CMakeLists.txt` (drops the system-libpng probe block, ±103
  lines of glue). The PR either restores the vanilla glue or the change is justified
  and rehomed to SDL3_image. Flagged as an open item, not a blocker.

### 1.2 "That part is optional for Hellfire, we can fix it ourselves"

Agreed and dropped. WAV fallback + mixer patch = fork-only, out of every PR below.

### 1.3 "We can't merge an 18k trust-me-bro PR — break it into functional changes"

The 18k number deserves a decomposition. The full fork delta vs upstream master is
~12.2k added / 2.1k deleted lines of *files*, but most of it is fork-internal and is
never proposed for upstream:

| Area | Added | Deleted | Goes upstream? |
|---|---|---|---|
| `docs/` (audit ledger, deltas, handoff, changelogs) | 14,185 | 1,784 | **No** — internal audit trail. Only `GPU_SHADERS.md` (327) + `SLANG_SPEC.md` (352) go, with their PRs |
| `tools/audit/` (audit harnesses, sweeps) | 4,996 | 0 | **No** — fork-internal QA tooling |
| `.github/` (fork's own CI; upstream's workflow matrix untouched in PRs) | 339 | 2,092* | Optional: one CI job, offered late |
| `Source/engine/render/gpu/` (the mod itself — all new files) | 6,270 | 0 | **Yes — the core of the series** |
| `Source/` integration points (display, options, DiabloUI, dx, scrollrt…) | ~860 | ~51 | **Yes** — spread across the PRs |
| `test/` (preset failure-modes, options tests, corpus) | 832 | 0 | **Yes** — travels with the code |
| CMake + Packaging (flag, SPV generator, shader install scripts) | ~545 | ~5 | **Yes** — spread across the PRs |
| `3rdParty/` glue (glslang, SPIRV-Cross additions; SDL3_image delta) | 159 | 59 | Decision point (§9.3) |

\* the 2,092 deleted workflow lines are the fork's own pruning of upstream's CI matrix
in its private tree — **no PR touches upstream workflows**; they are extracted fresh.

**The actual upstreamable surface is ≈ 8.6k lines of code + tests + build glue**,
split into 8 reviewable PRs, largest ≈ 1.9k, median ≈ 1.2k. The first three are pure
additions with zero engine impact.

---

## 2. Ground rules for every PR in the series

1. **One concept per PR.** If a reviewer can't hold the diff in their head, it's two PRs.
2. **Every PR is green on upstream's own gates**: upstream clang-format (already proven —
   the whole `Source/` + `test/` tree passes the repo's own format gate 698/698 files),
   upstream test suite green, no warnings, CMake supports the existing platforms.
3. **Zero behavior change unless the PR is explicitly the behavior-change PR.**
   Phase A PRs add library code that nothing calls yet; Phase B lands behind a build
   flag defaulting to the stock renderer until the feature PR is accepted.
4. **Tests travel with the code.** The parser/translator failure-contract corpus
   (37 hostile-input checks + good/malformed preset corpora, currently green in CI)
   is part of the same PR as the code it locks.
5. **Stacked branches, one open PR per track.** `pr-N` branches off `pr-(N-1)`; after
   merge, rebase the stack onto new upstream master (currently trivial: 1 commit drift
   in 3 weeks) and drop the merged branch. Fork master remains
   `upstream/master + PR stack + fork-internal bits`.
6. **No trust-me-bro claims in PR descriptions.** Each PR carries: what/why, the test
   evidence, how to review it (entry-point file first), and what it does NOT touch.
7. **Exit ramp at every stage.** If the series stalls after any PR, everything already
   merged is still inert or independently useful (§4 exit ramps).
8. **A PR opens only when its surface is audit-clean (new in r2).** Every open finding
   that shares a file with the PR diff is closed or dispositioned *before* the PR
   exists (§5). Upstream reviews hardened code once — never code plus a known-fixes
   tail.

---

## 3. Phase 0 — audit-closure gate (new in r2)

Current ledger: **44 open findings — 14 Medium, 30 Low, 0 High.** *(Ledger note,
2026-10-17: rounds 8 and 9 closed five of these Medium — ROB-18/19/22 and ROB-20/21 —
and one Low was added, TEST-08, taking the live ledger to 40; the round-11 sweep then
closed MAINT-15 and the FEAT-02 disposition; round 13 closed the last two, ROB-08 and
PERF-06; the round-14 8C sweep closed ROB-07/23 and TEST-06 with the FEAT-01/FEAT-03
riders, leaving **31 open — 0 High / 0 Medium / 31 Low** per `audit.md`. The nine
Medium owned by 8A+8B below are all closed — **the first-contact bar is complete**,
and Phase 0 is done: every sweep's Medium set is closed. Field round 15 then opened and closed ROB-31 (Medium), the post-8C field CTD on the Vertical Sync toggle — the round-13 change-callback registration was silently overwritten by the stock handler (single-slot), leaving an unthrottled IMMEDIATE swapchain; fixed with an explicit scaler/stock dispatcher plus present-mode support fallback.)*
Phase 0 dispositions
all of them before/while the series opens. Nothing here is redesign work: every finding
in the ledger already carries a concrete fix sketch, so Phase 0 is execution, not
exploration. Three rounds, each scoped to unblock a specific slice of the ladder.

### 8A — library surface (unblocks PRs 1, 2, 3)

**Fixed:** ROB-20 (file-size caps + exception catch-all on the load path),
ROB-21 (sampler name-dedupe), PERF-06 (translator scans made linear),
MAINT-15 (closed r11: CI bakes nothing — the committed header is the shipped blob), SEC-02 (ini-path containment),
SEC-03 (sidecar `.spv` validation/logging), SEC-05 (byte-exact POSIX sandbox),
SEC-06 (no pre-sandbox filesystem probe), ROB-12 (honest image-extension set),
ROB-28 (locale-independent float parsing), ROB-29 (compile-path mutex/contract),
MAINT-08 (shared string utils), MAINT-09 (comment/spec agreement),
MAINT-18 (ScanPresets exclusions), MAINT-19 (bare-`shaderpath` decision),
TEST-04 (SPV drift diff in CI), PERF-05 (cache pruning + single-read).
~17 items, 13 of them Low. Est. 2–4 focused sessions.

### 8B — presentation surface (unblocks PRs 4, 5)

**Fixed:** ROB-08 (vsync change callback), ROB-18 (backend retry when SDL auto-selects
D3D12), ROB-19 (non-fatal `RecreateMainRenderWindow` on the fallback path),
ROB-22 (virtual-gamepad texture lifecycle — free/re-init pairing designed before
touching `ReinitializeRenderer`), FEAT-02 (Metal resolved r11: Vulkan-only
device, honest Metal message, Vulkan retry kept), ROB-30 (acquire-failure
classification; numbered ROB-26 when this draft was written — renumbered at the
audit's round-10 re-head, 2026-10-17), MAINT-10 (log fallback to the user dir), PERF-01 (transfer-buffer ring,
profile-gated), PERF-03 (dead work in the present path). **Dispositioned:** PERF-04
(documented ceiling — informational), ROB-13 (known-documented; rides along only if
display.cpp window math is touched anyway, as the audit pre-authorized).
Est. 2–3 sessions.

### 8C — runner surface (unblocks PR 6; runs while PRs 1–4 are under review)

**Fixed:** ROB-07 (closed r14: runtime RAII — the prerequisite the FEAT-01 reload
shipped on), ROB-23 (closed r14: FillBlockBytes semantics — parameter lookup order,
type- and size-aware writes), TEST-06 (closed r14: smoke harness honors `#pragma name`),
ROB-10 (soft-vs-hard failure contract),
ROB-11 (size_t arithmetic + LUT dimension ceiling), MAINT-17 (sidecar self-heal),
PERF-02 (precomputed binding tables, allocation-free frame loop).
Est. 2–3 sessions.

### Waivers (dispositions, not fix work)

- **Upstream issues, not PRs:** MAINT-13 (three corrupted symlink blobs break tooling —
  genuinely upstream's tree; a zero-churn good-faith report), MAINT-11 (stale packaging
  metadata — four tiny issues or one metadata PR explicitly outside this series),
  MAINT-14 (`nthread_has_500ms_passed` name — upstream-coordinated), TEST-05
  (`--fail-on-skip` — standalone upstream PR candidate, outside the series), MAINT-12
  (god-files — upstream architecture; our only duty is not to deepen it, which the
  ladder already respects).
- **Fork-only:** ARCH-02 (consolidate fork-identity/asset-gate edits under
  `Source/mods/` — done during Phase 0 because it makes the pr-branch deltas
  enumerable), FEAT-01 (post-series; needs ROB-07), FEAT-04 (post-series), FEAT-03
  (optional PR 7 rider or post-series).
- **Known-documented:** ROB-13 (explicitly ruled out of scope on 2026-10-14, failure
  signature documented for support triage; opportunistic ride-along only).

### Exit criteria

- **E1:** all 14 open Medium fixed (12) or dispositioned with written rationale
  (FEAT-01, FEAT-03).
- **E2:** all 30 Low fixed, or filed into the waiver buckets above with a one-line
  rationale each.
- **E3:** ledgers reconciled (`audit.md` ↔ `deltas.md` ↔ `CHANGELOG.md`); fork CI
  green; format gate green.
- **E4:** ARCH-02 consolidation merged fork-side; fork rebased onto upstream master
  (1 commit drift today).
- **E5** (only on agreement to act — not before): `pr/1…pr/8` branches cut from the
  post-Phase-0 snapshot.

**Bar for first contact = 8A + 8B complete** (9 of 14 Medium closed; the remaining
five — ROB-07, ROB-23, TEST-06 in 8C, plus the two FEAT deferrals — live in
runner/feature code no early PR touches). If maintainers prefer a zero-open-Medium
bar before *any* PR, run 8C first as well; cost is roughly one week of slip.

*(Arithmetic corrected 2026-10-17: this bar was originally written as "10 of 14"
with "the remaining four" — ROB-20 was counted twice, once under PR 1 and once under
PR 2's translator-half note. 8A carries four Medium — ROB-20/21, PERF-06, MAINT-15 —
and 8B five — ROB-08/18/19/22 and the FEAT-02 disposition; 4 + 5 = 9, matching E1's
"fixed (12) or dispositioned" split.)*

---

## 4. The PR ladder (unchanged from r1; each PR now carries its audit gate)

### Phase A — Library layer (pure additions, nothing calls them yet, zero engine risk)

**PR 1 — Hardened `.slangp` preset parser** (~1.0k lines)
- `preset_parser.cpp/.hpp`, `preset_types.hpp` (~955): INI-style preset reader,
  recursive pack scan, the bounded-integer limits (ROB-01/ROB-02 class), and the SEC-01
  path sandbox (`IsPathInsideShaderRoot`) that refuses shader refs escaping the shader root.
- Parser half of the failure-mode test corpus.
- **Gate: 8A** — lands with SEC-02/05/06, ROB-12/20/28, MAINT-18/19 already fixed in
  the files it introduces.
- Dependency: none. Exit ramp: a standalone hardened preset loader any future shader
  system can reuse even if the GPU track dies here.

**PR 2 — Slang→GLSL translation layer** (~1.45k)
- `slang_translate.cpp/.hpp` (~1,348): libretro-slang semantics → plain GLSL
  (parameter UBO synthesis, alias/feedback renames, known-name sets), plus the
  translator half of the failure-mode tests.
- **Gate: 8A** — ROB-21 and PERF-06 fixed before this opens; the translator arrives
  deduped and linear.
- Dependency: PR 1. Exit ramp: standalone translation library + its corpus.

**PR 3 — AOT shader compilation (build-time) + built-in pack as data** (~0.6k + blobs)
- `slang_compile.cpp/.hpp` (~338) as the compile bridge, `CMake/gen_spv_header.cmake`
  (77), `gpu_shader_spv_prebuilt.hpp` (155) — built-in shaders are compiled to SPIR-V
  at build time and shipped as a header; **no glslang in the runtime**.
- **Gate: 8A** — MAINT-15/TEST-04 mean the baked blobs are reproducible and
  drift-checked before this PR exists; SEC-03/ROB-29 hardened at introduction.
- Dependency: PR 2. Exit ramp: the built-in pack needs no runtime compiler ever.
- This PR also forces the vendoring conversation early (§9.3) with the smallest
  possible stake.

### Phase B — Engine integration (flagged; stock path remains default until the end)

**PR 4 — Display backend seam refactor** (~170 lines, inert)
- The minimal refactor of `display.cpp` (+143) / `dx.cpp` (+22) that lets a second
  presentation backend plug in. No new backend in this PR — before/after identical
  behavior. Small on purpose: it touches the hot render path, so it lands alone and
  gets scrutinized alone.
- **Gate: 8B** — ROB-19/ROB-22 fixed first, because their fix sites are the exact
  display.cpp/dx.cpp lines this refactor touches; upstream never sees us shuffle a
  region we already know is broken. ROB-13 rides along only if window math is touched.
- Dependency: none technically; sequenced here to precede the GPU PR.
- Exit ramp: a cleaner backend seam is upstream-useful on its own.

**PR 5 — SDL_GPU scaler + stock presentation path** (~1.6k, the big one)
- `gpu_scaler.cpp/.hpp` core (device init, swapchain/reinit, transfer+present of the
  un-shaded frame), `gpu_log_sanitize.hpp`, the two stock blit shaders, the
  `USE_GPU_SCALER` CMake flag (default **OFF**), hooks in `main/scrollrt/interfac/
  capture` (~22 lines), the SDL3_image glue resolution (§1.1), and a graceful-fallback
  contract: on any SDL_GPU failure the stock path takes over (ROB-18/19 hardening lands
  here before review, not after).
- **Gate: 8B** — ROB-08/18/26 + FEAT-02 disposition + PERF-01/03 closed in the files
  it introduces; the fallback story it sells is the one 8B actually proved.
- Dependency: PRs 3–4. Exit ramp: opt-in GPU present path; nobody's build changes.

**PR 6 — Preset chain runner + the smoke corpus** (~1.9k)
- `preset_runner.cpp/.hpp` (~1,635): multi-pass chains, alias/feedback, history rings,
  LUT textures, parameter plumbing — running PR 2's output through PR 3's compiled
  SPIR-V on PR 5's device.
- `preset_smoke.cpp` harness (219) + good/malformed corpus fixtures + the ctest
  registration — the full-pipeline regression lock, headless, CI-friendly.
- **Gate: 8C** — ROB-07/10/11/23, MAINT-17, PERF-02, TEST-06 closed first; the smoke
  harness predicts in-game behavior for the pragma-name idiom real packs use.
- Dependency: PRs 1–5. This is the PR where presets actually work.
- Exit ramp: even here the feature is still opt-in.

**PR 7 — Options/UI + user-facing docs + packaging** (~0.8k)
- Settings integration (`options.cpp/h` +438, DiabloUI +18): the shader/preset browser,
  per-game ini plumbing; `GPU_SHADERS.md` + `SLANG_SPEC.md`; the shader
  install/clear scripts (Packaging, 231).
- **Gate: none** — ladder-gated only; FEAT-03 (in-game error surfacing) is an optional
  rider if maintainers want it; MAINT-11 goes upstream as issues, never as riders here.
- Default flips to ON only if maintainers want it here; otherwise stays opt-in.
- Dependency: PR 6. **End state of the series.**

### Phase C — Maintainer-gated decision, not a committed PR

**PR 8 (optional) — Runtime compilation for arbitrary user packs.** In-process glslang
behind a build flag, for loading packs that ship no precompiled SPIR-V. Alternatives if
maintainers prefer no new vendored dep at runtime: (a) a small shipped CLI that
pre-compiles a pack to `.spv` sidecars, (b) sidecar-only support, (c) never. This is
§9.3's decision materialized; it can also just… not happen, and Phase B stands complete.
Gate: whatever 8A left of ROB-29 lands here if the runtime path is accepted.

---

## 5. Readiness: which PRs are more ready today (new in r2)

| PR | Hard gates (open Medium on the PR's files) | Low findings folded into the same work | Ready after |
|---|---|---|---|
| 1 parser | ROB-20 | SEC-02†, SEC-05, SEC-06, ROB-12, ROB-28, MAINT-18, MAINT-19†, MAINT-09† | **8A** |
| 2 translator | ROB-21, PERF-06 | ROB-20 (translator half, closed in 8A), MAINT-08, MAINT-09†, ROB-28† | **8A** |
| 3 AOT + baked pack | MAINT-15 | SEC-03, ROB-29, TEST-04, PERF-05 (cache half) | **8A** |
| 4 display seam | ROB-19, ROB-22 | ROB-13 (ride-along or stays documented) | **8B** |
| 5 GPU scaler | ROB-08, ROB-18, FEAT-02 (disposition) | ROB-30, MAINT-10, PERF-01, PERF-03, PERF-04 (disposition) | **8B** |
| 6 runner + smoke | ROB-07, ROB-23, TEST-06 | ROB-10, ROB-11, MAINT-17, PERF-02, SEC-03†, PERF-05† | **8C** |
| 7 options/UI/docs | — | FEAT-03 (optional rider); MAINT-11 → upstream, never here | ladder order |
| 8 runtime compile | — | ROB-29† remainder if any | maintainer decision |

† spans several PRs; closed once, fork-side, at the earliest sweep that owns the file.

Reading of the table:

- **Nothing opens today, by design** — that is VTSTech's call and r2 honors it.
- **Cheapest to ready: PR 1** — its entire gate is one focused sweep (8A), which is
  mostly Low-severity items with fix sketches already written.
- **PR 4 is the smallest diff but sits behind 8B**, because two Medium findings
  (ROB-19, ROB-22) share its exact lines. Fixing them first is also the honest order:
  we don't refactor a region we know is broken and let upstream bisect the churn.
- **PR 6's gate (8C) deliberately runs during early reviews**, so the wall-clock cost
  of the heaviest gate is ~zero if reviews take their natural time.

## 6. Noise-control policy (new in r2 — the answer to "the audit cycle would be noise on their repo")

1. **Audit work completes fork-side; PR branches are cut from the post-Phase-0
   snapshot.** Upstream never sees a fixup tail, a "harden this while we're here"
   commit, or an audit-driven rework of a merged PR.
2. **The ledger never travels.** `docs/audit*`, `deltas.md`, `handoff`, `tools/audit/`,
   fork changelogs — fork-forever, excluded from every `pr/*` branch by construction.
3. **Review findings get the same treatment.** If upstream review surfaces a defect:
   fix lands fork-side first, then a single clean rebase — one coherent diff per review
   round, at most one explicit "review fixes" commit, no drive-by churn.
4. **Upstream-wide findings become upstream issues, not riders.** MAINT-13-class defects
   in upstream's own tree get reported as issues (or nothing); they never ride a shader
   PR. One concept per PR applies to bug reports too.
5. **No fork cadence noise.** No fork changelog entries, version bumps, or audit-round
   announcements upstream; PR descriptions carry their own evidence blocks (corpora,
   harnesses, flags, before/after numbers).
6. **Fork audit rounds continue on their own schedule.** They re-baseline the fork and
   touch `pr/*` branches only via rebase, and only when a finding shares a file with a
   PR — in which case rule 3 applies.

## 7. Fork-side prerequisites (private work, not upstream PRs — updated from r1)

1. **Rebase the fork onto current upstream master now** (1 commit drift) and keep it
   weekly. The longer the series runs, the more this matters.
2. **ARCH-02 consolidation fork-side** (new in r2, part of Phase 0): gather
   fork-identity/asset-gate edits under `Source/mods/` so every `pr/*` branch delta is
   enumerable before the first branch is cut.
3. **Restructure the fork tree into the PR stack** (`pr/1-parser` … `pr/8-runtime-compile`)
   — on agreement to act, cut from the post-Phase-0 snapshot, not from squashed fork
   history.
4. ~~Fold the remaining ROB backlog into the PR that owns each area~~ — **superseded by
   Phase 0 (§3)**, which does this formally with per-PR gates.
5. Keep the audit ledger and `tools/audit/` fork-forever; they never enter a branch.

## 8. What stays fork-only, permanently

- The audit ledger + audit tooling + fork changelog entries (rule §6.2).
- WAV fallback + the SDL3_mixer patch (Hellfire; unless the SDL_mixer upstream PR lands
  and makes the fork patch unnecessary anyway).
- The fork's CI file in its current form; offered upstream as a single optional job,
  born pinned (§9.7), never as the fork's own workflow file.

## 9. Decision points for the two maintainers (r1's seven + two new)

1. **SDL version floor** — the series needs nothing beyond upstream's existing SDL3 pin
   (already identical in both repos). Confirm SDL_GPU's API surface in that pin is
   acceptable, or agree on a release floor.
2. **SDL3_image glue** — restore vanilla in PR 5, or justify/rehome the simplification?
3. **glslang / SPIRV-Cross vendoring** — Phase A needs SPIRV-Cross only for tests and
   glslang only at build time. Acceptable as build-time-only deps? Runtime compile (PR 8)
   is a separate, later yes/no.
4. **Platform stance** — SDL_GPU = Windows/Linux/macOS; stock renderer stays the only
   path on constrained platforms (Switch, Vita, 3DS, …) and the default everywhere until
   PR 7 at the earliest. Agree?
5. **Acceptance criteria** — golden images for the built-in pack? Perf gates (the fork
   has per-frame diagnostics + a log-sanitizer already)? Demo/timedemo determinism must
   remain untouched (the pipeline is presentation-only).
6. **Conventions** — target namespace/placement (`Source/engine/render/gpu/`?), error
   handling idioms, and whether the preset corpora join upstream's test suite as-is.
7. **CI shape** — new optional workflow vs a job added to the existing SDL3 test
   workflow; whichever shape, the baked-SPV path arrives pinned (MAINT-15/TEST-04
   closed in 8A).
8. **Audit transparency (new)** — how much of the hardening story should PR
   descriptions tell? Default: a short "hardening included" block per PR naming the
   defect classes fixed and the evidence (corpus/harness), with the full ledger staying
   fork-internal. Alternative: maintainers get read access to `audit.md`/`deltas.md`.
9. **Upstream-issue etiquette (new)** — file MAINT-13 (corrupted symlink blobs) as a
   diasurgical issue now — zero churn, good-faith, and it genuinely breaks tooling — or
   hold everything until the series lands? MAINT-11/TEST-05 could follow the same route
   later.

## 10. Review-burden math and cadence (updated)

- Upstreamable total: **≈ 8.6k lines in 8 PRs** — largest ≈ 1.9k, median ≈ 1.2k, three
  of them pure-addition library code with zero call sites at merge time. Phase 0 adds
  ~zero net lines upstream: hardening lands *inside* the PRs that introduce each file,
  so reviewers see hardened code once, not code plus a fixes tail.
- Phase A (PRs 1–3, ≈ 3.1k) can be reviewed and merged with **no risk to any existing
  feature** — it's a dormant library.
- Phase B (PRs 4–7, ≈ 4.5k) is where behavior lives, one opt-in slice at a time.
- At any point the series halts, everything merged so far is either inert, independently
  useful, or cleanly flag-gated off. There is no commit in the series that requires
  "trust me" — every claim has a corpus, a harness, or a flag behind it.

**Revised cadence:**

1. **Phase 0** — sweeps 8A + 8B fork-side (the maintainer-agreed bar: 9 of 14 Medium
   closed, waivers dispositioned, ledgers reconciled, ARCH-02 consolidated, rebase done).
2. **First contact** — open **PR 4** (170-line inert seam: deliberately the smallest
   possible first diff on upstream-core files — a trust signal) and **PR 1**
   (pure-add hardened parser) in parallel review lanes.
3. **Pipelined** — sweep 8C runs while PRs 1/4 are under review; PR 2 + PR 3 open as
   8A-adjacent review capacity frees; PR 5 after 3+4 merge; PR 6 after 8C + 5;
   PR 7 ends the series; Phase C is the maintainers' call.
4. If maintainers prefer zero open Medium before *any* PR, fold 8C into step 1;
   cost ≈ one week.

Rough shape: Phase 0 in the first two weeks, Phase A under review by week three,
Phase B over the following two months — driven entirely by review turnaround and the
maintainers' appetite, with an explicit pause point after every PR.
