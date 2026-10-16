# Long-Term Merge Plan — GPU Shader Preset Pipeline → diasurgical/DevilutionX

**Status: discussion document — nothing scaffolded, no branches created, no PRs opened.**
Prepared for VTSTech (fork) and diasurgical maintainers (upstream) to react to.
All numbers measured against upstream master `b0a3b7bab` (2026-09-27, the fork's actual
merge-base; upstream is only 1 commit ahead of it) and fork tip `0f9e69324`.

---

## 1. Answering AJenbo's three conditions with facts

### 1.1 "If we can make it so I don't have to patch SDL"

Measured fact: **the GPU pipeline already runs on upstream's SDL3 unmodified.**
Both repos vendor the identical SDL3 tarball — `libsdl-org/SDL@570f7229`, same URL and
same SHA256 in `3rdParty/SDL3/CMakeLists.txt`. The fork changes **zero** lines in the
SDL3 core.

The **only** SDL patch anywhere in the fork is:

```
3rdParty/SDL3_mixer/0001-WAV-decoder-skip-zero-length-chunks.patch
```

— the Hellfire WAV-audio fix (plus `sound.cpp`/`soundsample.cpp` WAV-fallback code).
That is exactly the part AJenbo already said upstream doesn't need from us. So:

- **The mixer patch and WAV fallback are removed from the merge series entirely.**
  They stay fork-only (or, better home: the zero-length-chunk decoder fix is submitted
  directly to `libsdl-org/SDL_mixer`, which benefits everyone and removes the fork's
  last SDL-side patch).
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
| `3rdParty/` glue (glslang, SPIRV-Cross additions; SDL3_image delta) | 159 | 59 | Decision point (§6.3) |

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
   Phases A PRs add library code that nothing calls yet; Phase B lands behind a build
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
   merged is still inert or independently useful (§3 exit ramps).

---

## 3. The PR ladder

### Phase A — Library layer (pure additions, nothing calls them yet, zero engine risk)

**PR 1 — Hardened `.slangp` preset parser** (~1.0k lines)
- `preset_parser.cpp/.hpp`, `preset_types.hpp` (~955): INI-style preset reader,
  recursive pack scan, the bounded-integer limits (ROB-01/ROB-02 class), and the SEC-01
  path sandbox (`IsPathInsideShaderRoot`) that refuses shader refs escaping the shader root.
- Parser half of the failure-mode test corpus.
- Dependency: none. Exit ramp: a standalone hardened preset loader any future shader
  system can reuse even if the GPU track dies here.

**PR 2 — Slang→GLSL translation layer** (~1.45k)
- `slang_translate.cpp/.hpp` (~1,348): libretro-slang semantics → plain GLSL
  (parameter UBO synthesis, alias/feedback renames, known-name sets), plus the
  translator half of the failure-mode tests.
- Dependency: PR 1. Exit ramp: standalone translation library + its corpus.

**PR 3 — AOT shader compilation (build-time) + built-in pack as data** (~0.6k + blobs)
- `slang_compile.cpp/.hpp` (~338) as the compile bridge, `CMake/gen_spv_header.cmake`
  (77), `gpu_shader_spv_prebuilt.hpp` (155) — built-in shaders are compiled to SPIR-V
  at build time and shipped as a header; **no glslang in the runtime**.
- Dependency: PR 2. Exit ramp: the built-in pack needs no runtime compiler ever.
- This PR also forces the vendoring conversation early (§6.3) with the smallest
  possible stake.

### Phase B — Engine integration (flagged; stock path remains default until the end)

**PR 4 — Display backend seam refactor** (~170 lines, inert)
- The minimal refactor of `display.cpp` (+143) / `dx.cpp` (+22) that lets a second
  presentation backend plug in. No new backend in this PR — before/after identical
  behavior. Small on purpose: it touches the hot render path, so it lands alone and
  gets scrutinized alone.
- Dependency: none technically; sequenced here to precede the GPU PR.
- Exit ramp: a cleaner backend seam is upstream-useful on its own.

**PR 5 — SDL_GPU scaler + stock presentation path** (~1.6k, the big one)
- `gpu_scaler.cpp/.hpp` core (device init, swapchain/reinit, transfer+present of the
  un-shaded frame), `gpu_log_sanitize.hpp`, the two stock blit shaders, the
  `USE_GPU_SCALER` CMake flag (default **OFF**), hooks in `main/scrollrt/interfac/
  capture` (~22 lines), the SDL3_image glue resolution (§1.1), and a graceful-fallback
  contract: on any SDL_GPU failure the stock path takes over (ROB-18/19 hardening lands
  here before review, not after).
- Dependency: PRs 3–4. Exit ramp: opt-in GPU present path; nobody's build changes.

**PR 6 — Preset chain runner + the smoke corpus** (~1.9k)
- `preset_runner.cpp/.hpp` (~1,635): multi-pass chains, alias/feedback, history rings,
  LUT textures, parameter plumbing — running PR 2's output through PR 3's compiled
  SPIR-V on PR 5's device.
- `preset_smoke.cpp` harness (219) + good/malformed corpus fixtures + the ctest
  registration — the full-pipeline regression lock, headless, CI-friendly.
- Dependency: PRs 1–5. This is the PR where presets actually work.
- Exit ramp: even here the feature is still opt-in.

**PR 7 — Options/UI + user-facing docs + packaging** (~0.8k)
- Settings integration (`options.cpp/h` +438, DiabloUI +18): the shader/preset browser,
  per-game ini plumbing; `GPU_SHADERS.md` + `SLANG_SPEC.md`; the shader
  install/clear scripts (Packaging, 231).
- Default flips to ON only if maintainers want it here; otherwise stays opt-in.
- Dependency: PR 6. **End state of the series.**

### Phase C — Maintainer-gated decision, not a committed PR

**PR 8 (optional) — Runtime compilation for arbitrary user packs.** In-process glslang
behind a build flag, for loading packs that ship no precompiled SPIR-V. Alternatives if
maintainers prefer no new vendored dep at runtime: (a) a small shipped CLI that
pre-compiles a pack to `.spv` sidecars, (b) sidecar-only support, (c) never. This is
§6.3's decision materialized; it can also just… not happen, and Phase B stands complete.

---

## 4. Fork-side prerequisites (private work, not upstream PRs)

1. **Rebase the fork onto current upstream master now** (1 commit drift) and keep it
   weekly. The longer the series runs, the more this matters.
2. **Restructure the fork tree into the PR stack** (`pr/1-parser` … `pr/8-runtime-compile`)
   so each PR is a clean branch, not a slice of the squashed fork history.
3. **Fold the remaining ROB backlog into the PR that owns each area**: ROB-18/19
   (bounded fallback) → PR 5; ROB-22 (gamepad texture lifecycle) → PR 5; TEST-06/PERF-06
   → PR 6/7 evidence. Upstream sees hardened code, not a fixup tail.
4. Keep the audit ledger (`docs/audit*`, `deltas.md`, `brief.md`, `handoff`) and
   `tools/audit/` fork-forever; they never enter a branch.

## 5. What stays fork-only, permanently

- Audit ledger + audit tooling + fork changelog entries.
- WAV fallback + the SDL3_mixer patch (Hellfire; unless the SDL_mixer upstream PR lands
  and makes the fork patch unnecessary anyway).
- The fork's CI file in its current form; offered upstream as a single optional job (§6.7).

## 6. Decision points for the two maintainers

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
7. **CI shape** — new optional workflow vs a job added to the existing SDL3 test workflow.

## 7. Review-burden math (the answer to "18k trust-me-bro")

- Upstreamable total: **≈ 8.6k lines in 8 PRs** — largest ≈ 1.9k, median ≈ 1.2k, three
  of them pure-addition library code with zero call sites at merge time.
- Phase A (PRs 1–3, ≈ 3.1k) can be reviewed and merged with **no risk to any existing
  feature** — it's a dormant library.
- Phase B (PRs 4–7, ≈ 4.5k) is where behavior lives, one opt-in slice at a time.
- At any point the series halts, everything merged so far is either inert, independently
  useful, or cleanly flag-gated off. There is no commit in the series that requires
  "trust me" — every claim has a corpus, a harness, or a flag behind it.

**Suggested cadence:** open PR 1 + PR 2 together (independent review lanes), then one
PR at a time; maintainers re-scope Phase C after PR 3 merges. Rough shape: Phase A in
the first month, Phase B over the following two, driven entirely by review turnaround.
