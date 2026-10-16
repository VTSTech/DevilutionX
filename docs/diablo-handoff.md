# DevilutionX GPU Shaders — Session Handoff

## 0. Resume block (bounded — update rows in place, never append)

**How to resume a session (in order):**

1. `python tools/audit/session_bootstrap.py` — one-shot snapshot: tip/branch/dirty/
   origin sync, tip-most release tag, handoff §2 drift (benign docs-only vs code),
   dead-hash scan, ledger status counts, `Field-Round:` commits, worklog tail.
2. Read this §0 and the worklog tail. Grep deeper sections as needed — do not
   re-read the whole file.
3. Field logs: run `python tools/audit/field_log_digest.py <gpu-scaler.log> --tsv
   <out>.tsv` FIRST and share the digest (≤40 lines). Attach the raw log only when
   the digest's exceptions block is non-empty.

| Item | State (update in place) |
|---|---|
| repo tip | **2026-10-16: docs-sync** — round 9 (`Fix round 9: ROB-20, ROB-21`) rebased onto the audit condensation, brief refreshed to 33 closed / 39 open, merge-plan links repaired → `d071ac558` "docs" (audit exec summary condensed to a fix-history pointer; Round 7.6.1 record added to deltas.md; koko-aio-ng BLOCKED on the SDLIMAGE_JPG gap — CMake fix `c4b07281a`) → `319da03c9` CHANGELOG (round-8 entry) → `bbb0a9349` round 8 (ROB-18/19/22) → `8690cc7bb` handoff v2 rewrite (two-track restructure: Track A slang-shaders audit + Track B upstream merge; history stripped, live state kept in the ledger CSVs / audit.md / deltas.md) → prior tip line: `0f9e69324` "Linux CI" (r7.6.2 — `preset_smoke` `RUNTIME_OUTPUT_DIRECTORY` ctest geometry fix, CI green, user-applied) → `2c9a4ddfa` committed merge-plan drafts r1+r2 → `a0cbeccb6` = release **`gpu-shaders-v14`** (preset_smoke corpus green again) → beneath that: codebase-audit rounds 1–7.5 (closure history lives in `docs/deltas.md` — do not re-derive from this file) |
| release tag | **`gpu-shaders-v14`** (`a0cbeccb6`, tip-most of 14). v8 first shipped `install_shaders.cmd` + `clear_shader_cache.cmd` beside the exe; v9 added the Linux helper scripts + `install_git.cmd` |
| Track A — slang-shaders audit | **Static arm:** SLANG_SPEC §7 gaps closed through item 13 (`ca02abf856` comment alignment; spec reconciled 2026-10-16) — remaining items 7–11 + 14 are all intentional divergences / evidence-driven: **the actionable static queue is empty**; reopen an item only when a field fallback needs it (§6). **Empirical arm:** tiered ledger `docs/audit/shader-test-list-2026-10-15.csv` = 98 rows → **80 working / 18 pending / 0 crash** (ds-hybrid ×2 closed 2026-10-16 — round-9 protocol re-test passed on build `98357ffeb`; Tier 1b fully closed 2026-10-16: scanlines-Modern + ossc on r10 build `bbb0a9349`, remaining 4 rows ntsc-adaptive / motion_interpolation / super-2xbr-3d-2p / crt-guest-advanced-ntsc confirmed field round 14 on the post-CMake-fix build `e7f213c31`); coverage CSV 231 rows → 226 working / 3 crash / 1 black-screen / 1 fallback (investigation history retained). Dirs closed: `denoisers/` 7/7, `dithering/` 16/16, `deinterlacing/` 11/11, `misc/` 38/38, `blurs/` 15/17 (slots 110/113 skipped, untested), `handheld/` ds-hybrid pair. **Next:** the 18 pending tiered rows in tier order (§6) — next up the Tier 1c canaries — koko-aio-ng family is DEFERRED (round 13: loads but renders black; parked pending RetroArch-style shader-parameter support, §6 canary 2) |
| Track B — upstream merge | **Plan-first, reply pending.** r1+r2 sent to diasurgical (AJenbo) 2026-10-16; nothing scaffolded — no `pr/*` branches, no upstream contact — until the maintainers reply AND VTSTech directs. Plans: `docs/merge-plan-draft-r1.md` / `docs/merge-plan-draft-r2.md`. Default fork-side queue meanwhile: Phase 0 audit sweeps **8A → 8B → 8C** (§7) |
| codebase-audit ledger | **73 findings / 35 closed / 38 open (0 High / 7 Medium / 31 Low)** as of fix round 11 — audit work is driven by the **codebase-audit skill**; open set `docs/audit.md`, closure records `docs/deltas.md`, repo brief `docs/brief.md`. The Phase-0 mapping (which open findings gate which merge PRs) lives in `docs/audit.md`. This handoff carries the ledger state only — never its history |

## 1. Mission (TL;DR)

Standalone mod on **VTSTech/DevilutionX**, branched from upstream `1.6.0-dev` (see the
README "IMPORTANT" note). Runtime **libretro slang presets** (`.slangp`/`.slang`)
loaded from `shaders\` beside the exe — multi-pass chains, in-process glslang →
SPIR-V, content-addressed disk cache + sidecar `.spv` fast path. **Vulkan-only on
Windows and Linux** (Metal accepted at the device level on macOS, untested); D3D12 is
excised; baked ScaleFX/HQx filters and their Settings entries are removed.
`DEVILUTIONX_GPU_BACKEND=vulkan` continues to work as an explicit override. Tooling
on master: `tools/audit_slang_presets.py` (preset feature extractor, v2),
`tools/audit/*` (crossref + codefindings scanners, session bootstrap, field-log
digest), `tools/audit/test_*.cpp` (regression tests).

Two parallel goals drive all work:

- **Track A (primary) — slang-shaders audit:** audit the shader implementation and
  parser against `docs/SLANG_SPEC.md` (static arm: per-key status matrix + §7 gap
  list) and validate presets on hardware (empirical arm: §6 field sweep).
  Reconciliation rule: a preset counts as covered only when its SLANG_SPEC feature
  set is all `supported`/`extension` AND it passes the §6 acceptance criteria; every
  sweep fallback either closes a listed spec gap or adds a new row to it.
- **Track B — upstream merge (conditional):** merge the upstreamable surface
  (~8.6k lines in 8 functional PRs) into diasurgical/DevilutionX per
  `docs/merge-plan-draft-r2.md` — plan agreed internally, sent to the maintainers,
  **reply pending**. Independent of the PR outcome, the long-term goal is a
  **standalone, reusable shader-preset library** (§7).

The codebase-audit campaign (rounds 1–7.5) is **handled by the codebase-audit
skill** against its ledger (`docs/audit.md` open set / `docs/deltas.md` closures /
`docs/brief.md` repo brief); this handoff carries only its live state (§0 row), not
its history.

## 2. State snapshot

| Ref | State |
|---|---|
| fork `master` (what CI builds) | `7b29aa282` (docs; prior `e7f213c31` = SDLIMAGE_JPG_SHARED OFF build fix; live tip + lineage in §0). Tracks origin/master. Field-verified on Windows (v8/v9-lineage CI builds) and Debian 12 bookworm (`gcc:14-bookworm` container, glibc 2.36 floor, GLIBCXX gate) |
| release tags | 14 total; tip-most **`gpu-shaders-v14`** (`a0cbeccb6`) |
| PRs #8734/#8736/#8737 | **DEAD — do not pursue.** 18k monoliths (baked filters + SDL patching) violate AJenbo's merge conditions (§7); superseded by the 8-PR ladder. Report bugs and request features **in this repository** (README "IMPORTANT"). Upstream issue #4453 and the demo video remain as history. Never post to upstream by any means other than VTSTech by hand (§7 standing rules) |
| upstream SDL_mixer PR #906 | OPEN; fixes the zero-length RIFF chunk bug (Diablo's 1996 WAVs fail to decode). Nice-to-have, **not gating** — the mod carries its own vendored `3rdParty/SDL3_mixer` patch + `SDL_LoadWAV_IO` fallback. Watch only |
| branch `wav-mixer-fix` | standalone DevilutionX-side WAV fix; already covered in-master (vendored patch + fallback). No action |

## 3. Sandbox map

- `/home/z/my-project/DevilutionX` — main repo (`origin` = VTSTech/DevilutionX, the
  only remote that matters). Local `master` tracks origin master (tip may lag; fetch
  first). The mod's docs live in-repo: `README.md`, `docs/GPU_SHADERS.md` (feature
  doc + troubleshooting), `docs/SLANG_SPEC.md` (normative slang spec + status
  matrix — the Track A baseline), `docs/diablo-handoff.md` (this file),
  `docs/audit/` (audit report, audit CSVs, tiered sweep list),
  `docs/audit.md` / `docs/deltas.md` / `docs/brief.md` (codebase-audit ledger),
  `docs/merge-plan-draft-r1.md` / `-r2.md` (Track B plans).
- `/home/z/my-project/worklog.md` — append-only multi-agent log; per-round records
  with exact edit-tooling gotchas. Read the tail on resume; append per round.
- `/home/z/my-project/scripts/` — reusable tooling (per-round byte-exact patch
  scripts `apply_*.py`, CI repro, MinGW toolchain setup, sweep harnesses).
- `/home/z/my-project/reference/` — full libretro/slang-shaders corpus (see §9).
- `/home/z/my-project/download/` — user-facing deliverables: round patch bundles,
  sweep/coverage CSVs, build zips.

Fresh-sandbox note: `worklog.md`, `scripts/`, `reference/` are sandbox-local and
rebuildable (§9–10); everything in-repo (`docs/audit/`, `tools/audit/`, the plans)
travels with the clone.

## 4. Design decisions (confirmed with VTSTech — do not re-litigate)

| Item | Decision |
|---|---|
| Format | libretro slang presets (`.slangp` + `.slang`), libretro preset spec semantics |
| Dir layout | `shaders\` NEXT TO THE EXECUTABLE (recursive scan, subfolders OK), not the in-Source tree |
| Compile | In-process glslang (FetchContent `3rdParty/glslang`) → SPIR-V; cache in `shaders/cache/`; `<name>.vert.spv`/`.frag.spv` sidecars always win |
| Multi-pass | Per-pass scale rules, filter/wrap, sRGB & float FBOs, aliases, `Original`/`OriginalHistory#`/feedback, LUT PNGs, `#pragma` params at defaults, `frame_count_mod`, minimal history depth |
| **Pass orientation** | Every pass rendering into an INTERMEDIATE target gets a Y-mirror MVP (`diag(1,-1,1,1)`); last pass keeps the legacy convention so the present blit cancels — even/odd chain flips fixed, field-verified; vertex stages without an MVP uniform keep their own (unknown) orientation — flagged `vertexMvp=no` in the load log, none seen in the field |
| **D3D12** | **EXCISED (2026-10-14)** — SPIR-V → SPIRV-Cross → HLSL transpile path and forensics tooling all deleted. Do not revisit; with no Vulkan driver the scaler fails cleanly to the regular renderer (failure contract) |
| Metal | **DISPOSITIONED (FEAT-02, 2026-10-17)** — the device layer no longer accepts MSL (`CreateDevice` sets `SHADERS_SPIRV` only; the earlier “SDL transpiles SPIR-V to MSL at runtime” claim was wrong and is retired with this entry). Auto-selected Metal logs “GPU shader mod requires Vulkan; Metal is not yet supported”, then takes the ROB-18 forced-Vulkan retry (MoltenVK stays viable) or falls back to the stock renderer per the failure contract. The hand-ported MSL stays in `gpu_shaders_msl.hpp`, unwired, for a future validated path |
| Built-ins | REMOVED (user request): no ScaleFX/HQx pipelines/list entries; `gpu_luts.hpp` is an unreferenced orphan (safe to delete); stale ini values 1–6 sanitize to Off; only shared `Vertex`/`Blit` programs remain baked (the `gpu_scaler_spv` build target still compiles the dead hqx/scalefx sources into the generated header — harmless, trimmable) |
| UI | Shader Filter = Off + folder-browser entries; enum `100+N` is an index into the CURRENT listing; durable selection = stored relative path (`Graphics.shaderFilterPreset.value`, hidden ini); path committed BEFORE the value change |
| Hot reload | None in v1 (restart to apply); switching presets live DOES rebuild the chain |
| Failure contract | Any preset load/run failure → session fallback (classic renderer), game never dies; custom-preset failure turns the filter off for the session |

## 5. Architecture

`Source/engine/render/gpu/preset/` — `preset_types.hpp` (data model), `preset_parser.*`
(.slangp parser: chains, scale rules, FBO flags, aliases, textures/LUTs, params,
`#reference` merge with rebased paths + ancestor fallback, `ScanPresets`,
`ResolvePresetFilePath`), `slang_translate.*` (.slang → GLSL, std140 reflection, dense
bindings), `slang_compile.*` (glslang → SPIR-V, cache, sidecar), `preset_runner.*`
(multi-pass executor: pipelines, history ring, LUTs, sampler cache, backend =
vulkan-only after the D3D12 excision), `preset_smoke.cpp` (offline harness).
`3rdParty/glslang` is a FetchContent wrapper (SPIRV-Cross is declared but no longer
linked into the preset path). **Normative reference for all five preset TUs:
`docs/SLANG_SPEC.md`** — per-key/per-semantic status plus the gap queue (§6).
Regression tests for every closed spec gap: `tools/audit/test_*.cpp`.

The present path lives in `Source/engine/render/gpu/gpu_scaler.cpp` (acquire/cancel/
skip state matrix, integer-fit logic, `present skip:` trail, `dest-skipped` heartbeat
counter); `Source/utils/display.cpp` holds the deliberately-unfixed Integer-Scaling
window math (§6). Debug mode: `DEVILUTIONX_GPU_DEBUG=1` env-gates SDL's validation
block + Khronos layer in `CreateDevice` — turns silent driver faults into logged
errors + graceful fallback.

## 6. Track A — slang-shaders audit (primary)

### Static arm — SLANG_SPEC.md §7 gap queue

`docs/SLANG_SPEC.md` is the normative baseline: every `.slangp` key and `.slang`
semantic with the fork's implementation status (supported / partial / extension /
parsed-unused / unimplemented / divergent), cross-checked against RetroArch's
`gfx/video_shader_parse.c`, ending in the priority-ordered gap list (its §7).
Items 1–6, 12, 13, 15–20 are closed (F6 viewport aspect `f87f89d18`, F1 mipmaps,
param-case quirk, `PassFeedback#Size` crash, `Rotation` semantic, `#pragma format`
tokens, case asymmetries, the four field-round-1/2 parser fixes, and item 13's
comment-side alignment `ca02abf856` — last-wins `#reference` order documented) —
each code fix carries a regression test under `tools/audit/test_*.cpp`. Do not
re-open them. **The actionable static queue is empty.** What remains is
maintenance-mode: intentional divergences, reopened only on field evidence.

| # | Item | Disposition |
|---|---|---|
| 7 | `clamp_to_border` wrap mapped to clamp-to-edge | SDL3 GPU can express border clamping; unrepresented in corpus — add on evidence |
| 8 | `OriginalHistory` cap 8 (spec unbounded, corpus max 7) | raise only on evidence |
| 9 | Hard-error translator paths, zero corpus exposure (T2–T5: vertex-stage samplers, sampler arrays, >1 plain UBO, unsized arrays) | leave as loud errors — they flag genuinely unsupported shaders |
| 10 | `#pragma alias` / `#pragma include_optional` / `#error` skip | add only if a pack needs them |
| 11 | `FrameDirection` constant +1 | correct for this game (no rewind) — keep |
| 14 | `feedback_pass` / `histories` parsed-unused (F2) | intentional (demand-driven feedback); re-evaluate only when spec-conformance testing starts |

### Empirical arm — field sweep

Ledger: `docs/audit/shader-test-list-2026-10-15.csv` (98 tiered rows: **80 working /
18 pending / 0 crash**) + `docs/audit/vulkan-coverage-2026-10-15.csv` (231 rows: 226
working / 3 crash / 1 black-screen / 1 fallback — the non-working rows are retained
investigation history). Dirs closed clean: `denoisers/` 7/7, `dithering/` 16/16,
`deinterlacing/` 11/11, `misc/` 38/38, `blurs/` 15/17 (slots 110/113 skipped in the
browser — untested, not failed); partials: `reshade/` 8, `sharpen/` 6, `handheld/`
swept rounds 7+9 (ds-hybrid pair closed by the round-9 protocol re-test). All sweeps
to date: 0 uncontained failures — the failure contract (§4) held every time.

**Immediate re-tests (canaries, in order):**

1. ✅ **`handheld/ds-hybrid-{sabr,scalefx}` — CLOSED 2026-10-16 (round-9 protocol
   re-test PASSED).** Evidence: `download/gpu-scaler-dshybrid-retest.log`, digest
   exit 0 (2 loads / 2 unique / 0 fallback / 0 errors / exceptions none). Build
   `98357ffeb` (contains the round-9 fix `0a39b81c6`), `DEVILUTIONX_GPU_DEBUG=1`
   (Khronos layer absent — SDL internal validation only), RX 560 / AMD 26.5.2: both
   chains all-cache-hit, pipelines created in debug mode (r7 `sample_mask` fix
   live), no cancel assert (r8 fix live), and the exact over-window finals
   (5760×3240 / 3840×2160) presented with **0 dest-skipped** and no `present skip:`
   lines (r9 fit guard live). Dwell 39.3 s / 2341 frames (scalefx) + 12.1 s / 657
   frames (sabr); 5–14 skipped frames per 5 s window (~4%) = the legal
   unavailable-swapchain-image class, not a contract violation; OSD correct on both
   hot-switches; clean Shutdown + benign exit-time presentation-path window
   recreate (DXGI one-flip-per-HWND workaround, fires after `Shutdown`). Ledger
   flipped to working via `scripts/apply_ds_hybrid_retest.py`. Visual defect check
   (flip/color) pending explicit user confirmation — reopen as partial if the
   image ever looked wrong.
2. **koko-aio-ng family (3 presets) — DEFERRED (2026-10-16, VTSTech call).**
   The build blockers are fixed and the preset loads end-to-end; what remains is
   engine/UX scope, not a shader bug, so the family is parked. Timeline:
   rounds 6+11 — fallback at `IMG_Load backdrop.jpg` (vendored SDL_image
   `SDLIMAGE_JPG OFF`; fixed by `c4b07281a` + `external/jpeg` submodule).
   Round 12 — backend active but `SDLIMAGE_JPG_SHARED` defaulted ON →
   `LOAD_JPG_DYNAMIC="libjpeg-9.dll"` → missing DLL; fixed by
   `set(SDLIMAGE_JPG_SHARED OFF)` (`e7f213c31`). Round 13 (build `e7f213c31`):
   family ×3 (koko-aio-ng, -DMG, Presets_Handhelds-ng/Dots-sharp_4-3) all LOAD —
   17 passes, all 6 bezel textures (PNG+JPG) clean, pass targets all correct
   (incl. absolute 96×64 / 128×128 and viewport-scaled final), mip chain on pass
   0 output, feedback aliases bound — but the screen is BLACK on all three while
   the game stays alive (HW cursor + sound; heartbeats normal, 0 dest-skipped).
   Root cause not settled; resume notes: (a) top suspect — the preset's custom
   `global` UBO (`config.inc`, ~150 members: every RA runtime shader parameter +
   per-alias sizes like `global.upscale_passSize`); if our translator leaves it
   zero-filled, `coords_QULEZ(coords, global.upscale_passSize)` divides by zero
   → NaN → black; (b) `ParseWrapMode` has no clamp_to_border case (falls back to
   clamp_to_edge) — the preset needs border wrap on backdrop/halo/avglum/ambi/
   reflection_pre; small standalone engine gap; (c) the family leans on RA's
   runtime shader-parameter UI (dozens of `#pragma parameter`s) which we do not
   have — even with (a)/(b) fixed, field tuning needs a parameter UI or defaults
   verified against the pack's stock presets. Upstream sources fetched to
   `scripts/koko-ng/` (libretro/slang-shaders `bezel/koko-aio`). Revisit only if
   parameter support gets scoped; until then the family stays out of the sweep
   queue.
3. **Then the 18 pending tiered rows** — koko-ambi-crt-geom,
   crt-geom-deluxe-ntsc-adaptive (the Tier 1c pass-feedback canary), Mega_Bezel hsm-custom-fast-sharpen + hsm-interlace-and-linearize,
   DSLite-2020, gba-color, gb-palette-dmg, fsr-aa-lv2-median3x3,
   bayer-matrix-dithering, phosphor-persistence, gauss_4tap, 2xBR-lv1-multipass,
   3ds-lcd-grid-v2, effect-border-iq, bicubic, drez_3ds, cleanEdge-rotate, crt-1tap.

Sweep-evidence rider (TEST-08, round 14): the pass line's `textures=[...]` lists
post-translation sampler names, not file loads, and `LoadLut` logs failures only —
a successful PNG/JPG LUT load leaves no positive trace, and the digest's
`PNG textures:` field is constant `none` (its counter can never fire). Treat that
field as evidence of nothing until the proposed 1-line engine log + digest regex
land (deltas.md TEST-08).

**Queue after the canaries** (risk buckets from `tools/audit_slang_presets.py` v2;
per-preset flags in `docs/audit/slang_audit_v2.csv`):

| Phase | Bucket | Count | Top dirs / dominant combo |
|---|---|---|---|
| 2 | medium-risk | 539 | `presets/` 105, `edge-smoothing/` 87, `crt/` 70, `handheld/` 35, `border/` 34, `interpolation/` 32, `downsample/` 26, `pixel-art-scaling/` 20 — dominant combo `uses_mvp + viewport_scale` (232 presets); the F6 fix targets exactly this path, so confirm with a small sample first, then walk the dirs |
| 3 | high-risk | 1233 | `bezel/Mega_Bezel` chains (18–45 passes), `scanline-classic`, CRT-royale families; "kitchen sink" combo ×119 — save for last |
| 4 | unresolved 0-pass | 701 | `#reference` inheritance chains resolving outside the audit root (mostly `bezel/Mega_Bezel` 468 + `bezel/koko-aio` 180 + `hdr/sony_megatron_*` 34) — re-run the audit with references resolved to classify |

For every fallback: capture `gpu-scaler.log`, categorize (failure table below), map
to a fix in `slang_translate.cpp` / `preset_runner.cpp` / `preset_parser.cpp`,
close-or-add the spec row, re-sweep. The CSVs can be diffed run-to-run to track
progress.

### Known non-shader issue — Integer Scaling (documented, deliberately unfixed)

The core window-size math half remains open **by VTSTech decision — do not
re-diagnose or fix in shader rounds**: `CalculatePreferredWindowSize`
(`Source/utils/display.cpp`) — the integer factor truncates to 0 → divide-by-zero
when the resolution is not a whole-number multiple of the desktop (2275×1280 on
1080p; 720p is fine). (The `PresentFrame` zero-size-fit half of this class WAS fixed
in round 9 — see the ds-hybrid re-test above.) Documented in GPU_SHADERS.md
§Troubleshooting; the abandoned fix diff is snapshotted in the mod packaging
(`integer-scaling-known-issue/`). If a field crash matches this signature (Integer
Scaling on, odd resolution, no preset errors in the log), report it as this known
issue and move on.

### Validation methodology (per preset)

1. Drop the slang-shaders tree into `shaders/` next to the exe (recursive scan,
   subfolders OK), or run `install_shaders.cmd` / `install_shaders.sh`.
2. Launch with `DEVILUTIONX_GPU_BACKEND=vulkan` (belt-and-suspenders — Vulkan is the
   only backend since the D3D12 excision).
3. Walk the preset list (Settings → Graphics → Shader Filter → browse folders). Per
   preset: the OSD name must match the selection (`Shader: <displayName>` — if it
   lies, the wrong preset loaded, §8 gotcha); wait ~2 s for compile + first frame;
   check visible / black / distorted / flipped.
4. Screenshot each success. On fallback (image gone, "Custom (unavailable)" OSD),
   capture `gpu-scaler.log` (200 KB cap, one `.old` generation) and categorize.

**Acceptance — "working"** means, on Vulkan with a real game frame flowing:
(1) the chain compiles without `gpu-scaler.log` errors; (2) the first frame renders
with no visible defect (no flip, no black, no corruption); (3) the output survives
60 s of gameplay without crash or fallback; (4) frame-count feedback doesn't tear
past the `frame_count_mod` boundary. **Partial** = renders with a visible defect —
track separately; may share a root cause with full failures.

**Reporting:** per sweep, CSV with `preset, status, category, notes` (status ∈
working / partial / fallback / crash; category from the table below or `ok`) →
`download/vulkan-coverage-<date>.csv`; update the tiered ledger CSV in-repo; send
`gpu-scaler.log` (digest FIRST — §0 step 3) for every fallback.

### Failure categories (mapped to source files)

| Failure category | Log signature | Fix location |
|---|---|---|
| slang → GLSL translation error | `translating '<shader>' (pass N): <error>` | `slang_translate.cpp` |
| glslang compile error | `compiling '<shader>' (pass N): <error>` | `slang_translate.cpp` (emit valid GLSL) |
| Missing preset feature (history, feedback, alias, scale rule) | `unresolved texture '<name>'` or silent misrender | `preset_runner.cpp` (binding logic) or `preset_parser.cpp` (spec coverage) |
| Vulkan validation layer error | `vkCreateGraphicsPipelines` / `vkUpdateDescriptorSets` validation message | `preset_runner.cpp` (pipeline/descriptor setup) |
| LUT PNG load failure | `Unsupported image format` | `SDLIMAGE_VENDORED` (already fixed) |
| Vertical flip (even/odd pass parity) | visible flip, no log | Y-mirror MVP on intermediate passes — already fixed |
| Crash (AV / segfault) | `CRASH #N:` in log, or silent death | `preset_runner.cpp`; use `DEVILUTIONX_GPU_DEBUG=1` to name it |

## 7. Track B — upstream merge track (plan-first — reply pending)

**Status:** plan r1 → r2 agreed internally (VTSTech "more or less agree" on r2) and
sent to the diasurgical maintainers together (2026-10-16); reply pending. **Nothing
scaffolded — no branches, no PRs, no upstream comments/issues — until they reply and
VTSTech says go.** Full plans: `docs/merge-plan-draft-r2.md` (r2, canonical) and
`docs/merge-plan-draft-r1.md` (r1, kept for standalone reading).

**AJenbo's merge conditions (operative constraints, measured):**

1. **No SDL patching** — measured fact: the SDL3 core is byte-identical between fork
   and upstream (same tarball `570f7229`, same SHA256); the GPU pipeline needs zero
   SDL changes. The only SDL patch anywhere in the fork is the Hellfire WAV
   `SDL3_mixer` fix — stays fork-only, or rehome to libsdl-org (upstream SDL_mixer
   PR #906 already covers the same decoder bug).
2. **The Hellfire part is optional** ("we can fix it ourselves").
3. **No 18k monolith** — break into functional changes merged one at a time.
   Upstreamable surface ≈ 8.6k lines in 8 PRs (largest ≈ 1.9k, three pure-add); the
   rest of the 18k is fork-internal ledger/tooling/docs that never travels.

**The ladder** (full detail: `docs/merge-plan-draft-r2.md` §4):

PR 1 parser (~1.0k, gate 8A) → PR 2 translator (~1.45k, 8A) → PR 3 AOT + baked pack
(~0.6k, 8A) → PR 4 display seam (~170 lines, 8B, **first contact**) → PR 5 SDL_GPU
scaler (~1.6k, 8B) → PR 6 runner + smoke corpus (~1.9k, 8C) → PR 7 options/UI/docs
(~0.8k) → PR 8 runtime compile (maintainer-gated, optional).

**r2 additions (VTSTech's observations, incorporated):**

- **Per-PR audit gates:** a PR opens only when every open finding sharing a file
  with its diff is closed or dispositioned (ground rule 8). Upstream sees hardened
  code once — never code plus a known-fixes tail.
- **Phase 0 audit-closure gate:** sweep **8A** (library surface → unblocks PRs 1–3),
  **8B** (presentation surface → PRs 4–5), **8C** (runner surface → PR 6; pipelines
  with early reviews). First-contact bar = 8A+8B (9 of the 14 Medium open when r2 was
  measured — rounds 8/9/11 closed seven of those nine, leaving ROB-08 and
  PERF-06).
  Waivers: MAINT-11/12/13/14 + TEST-05 → upstream issues; ARCH-02 → fork-only
  Phase-0 item; ROB-13 → known-documented; FEAT-01/04 → post-series; FEAT-03 →
  optional PR-7 rider.
- **Noise-control policy:** audit work completes fork-side; `pr/*` branches are cut
  from the post-Phase-0 snapshot; the ledger (`docs/audit*`, `deltas.md`,
  `brief.md`, this handoff, `tools/audit/`) never enters a branch; upstream-wide
  findings become upstream issues, never PR riders; review findings → fix fork-side
  first → single clean rebase (one coherent diff per review round).

**Long-term library goal (VTSTech — the PR outcome does not change it):** the end
state of `Source/engine/render/gpu/` is a **standalone, reusable shader-preset
library** any project could adopt, gated on: (1) **full audit closure** — zero open
findings, or every remainder dispositioned with written rationale; (2) the public
API surface hardened and documented (the parser / translator / compile / runner
boundaries are already the seams); (3) its own test corpus carried wholesale (the
failure-modes harness + smoke corpora already travel with the code); (4) docs that
stand without DevilutionX context.

**Standing rules while the track is open:**

- No `pr/*` branches; no upstream comments/PRs/issues beyond what VTSTech posts by
  hand — until the maintainers reply and VTSTech directs.
- Phase 0 sweeps 8A → 8B → 8C are the default work queue (maintainer-independent);
  Track A field-sweep items continue in parallel when field logs arrive.
- The ledger stays fork-forever; nothing in this track changes ledger ownership.

## 8. Hard-won gotchas (do NOT relearn)

- Repo identity: this is a **standalone mod** — issues, PRs, and releases live on
  **VTSTech/DevilutionX**. #8734/#8736/#8737 are dead because they are 18k monoliths
  with baked filters + SDL patching (AJenbo's conditions, §7) — note diasurgical has
  NO no-AI policy (that policy is libsdl-org's/SDL's). Author identity: GitHub maps
  both `VTSTech <14359327+VTSTech@users.noreply.github.com>` and
  `Nigel Todman <veritas@vts-tech.org>`; keep one form per series.
- clang-format **18** enforced by CI over `Source`. Verify with a REAL dry-run:
  `clang-format --style=file --dry-run -Werror <files>` — claims without the run
  have been wrong before (8-space indent shipped as "clean"). The CI job (jidicula
  v4.18.0) scans EVERY C/C++ file under `Source` + `test`, not just the diff, and
  docs-only pushes don't trigger it at all (paths-ignore) — silence is not proof.
- **Include-order trap:** `<tlhelp32.h>` (and similar Windows headers) must come
  AFTER `<windows.h>` — mingw-w64 headers don't self-prime. `SortIncludes: true`
  re-sorts single blocks alphabetically, so separate order-dependent headers with a
  blank line + comment, and compile the TU with mingw g++ BEFORE pushing to CI.
  Local harness: extract `g++-mingw-w64-x86-64-win32`, then
  `-std=c++23 -fsyntax-only -DUSE_SDL3 -I Source -I <pinned SDL>/include -I
  <unordered_dense>/include -I 3rdParty/tl -I <magic_enum>/include` (SDL/
  unordered_dense/magic_enum are FetchContent — grab the pinned tarballs once).
- **`git am --keep-cr` is MANDATORY** (CRLF repo); plain am corrupts diffs.
  `git format-patch` has no `--keep-cr` (am-side only).
- Helpers at the TOP of a TU (before `namespace devilution`) cannot use
  `StrCat`/`StrAppend`/`Log*` — keep them pure std.
- mingw's `IMAGE_NT_HEADERS` has NO `Sections[]` member (MSVC extension) — use
  `IMAGE_FIRST_SECTION(nt)`; compiles on MSVC, fails on mingw CI otherwise.
- Cross builds have NO system libpng: SDL_image without the STB backend and without
  vendored libpng silently has NO PNG codec (IMG_Load/IMG_SavePNG fail). Vendored
  libpng+zlib is mandatory (`SDLIMAGE_VENDORED=ON` + `GIT_SUBMODULES` — release
  tarballs lack `external/`).
- **Vendored-dep target collisions** (CMP0002): SDLIMAGE_VENDORED defines
  `zlib`/`zlibstatic`; superproject zlib is deferred for SDL3 and reuses SDL_image's.
  When adding a vendored dep, grep `3rdParty/` for duplicate targets first.
- Headless-configure escape hatches: `-DSDL_UNIX_CONSOLE_BUILD=ON`,
  `-DCMAKE_POLICY_VERSION_MINIMUM=3.5` (CMake 4), `-DSDL_X11=OFF -DSDL_WAYLAND=OFF …`.
- FetchContent variables are LOWERCASED (`sdl_image_SOURCE_DIR`) regardless of the
  declared case.
- Unquoted heredocs execute backticks — quote the delimiter (`<< 'EOF'`).
- When a field log contradicts the code model, ship fingerprint diagnostics (size +
  FNV-1a + API-use counts) before a behavioral "fix".
- **Never read `GetOptions()` from an `Options` member's constructor** — magic-static
  re-entry deadlocks (no window/output/crash). Defer to a `LoadFromIni` override.
- **Option value changes notify listeners SYNCHRONOUSLY** (value → `ResizeWindow` →
  `Reinit` → preset reload inside `SetActiveListIndex`). Commit companion state
  (preset path!) BEFORE changing the value.
- The preset enum `100+N` is a folder-listing index and goes stale; the stored
  relative path is the only durable selection. Never re-add a value-index fallback.
- OSD preset name = loaded chain's `preset.displayName`; if the OSD lies, the wrong
  preset loaded.
- Byte-exact editing: the `Edit` tool mangles tab indentation in this repo — use the
  Python patch scripts in `scripts/` (per-file EOL: `options.cpp` CRLF+tab;
  `gpu_scaler.cpp`/`preset_parser.cpp` LF+tab), idempotent, assert-1-match.
  (clang-format-managed files also accept plain edit + `clang-format -i`.)
- GUI-subsystem exe: nothing on stdout; `--verbose` only raises SDL priorities.
  Diagnostics live in `gpu-scaler.log` (200 KB cap, one `.old` generation).
- Frame handed to the scaler is already RGBA32 (palette resolved upstream) — no
  palette pass needed.
- Known upstream bug (documented in #8737, non-blocking due to fallback): Vulkan
  ~167 s idle crash on some drivers.
- tmpfiles.org: 60-minute expiry; the tokenized `/dl/...` link (from the view page
  HTML) bypasses the interstitial.
- OBS `graphics-hook64.dll` injects into ANY process that creates a D3D device —
  device-less smoke tools can't reproduce injection-dependent behavior; check the
  module scan before blaming game code.

## 9. Reference material (`/home/z/my-project/reference/`)

- Full libretro/slang-shaders tree: `reference/slang-shaders/slang-shaders-master/
  slang-shaders-master/` — 2658 `.slangp` + 1653 `.slang` (rebuildable: re-clone
  https://github.com/libretro/slang-shaders or re-extract the user's 7z).
- Key locations: HQx `edge-smoothing/hqx/hq{2,3,4}x.slangp` (+ LUT PNGs in
  `resources/`); ScaleFX under `edge-smoothing/scalefx/` +
  `presets/scalefx-plus-smoothing/`.
- Study samples at `reference/` root: `crt-easymode.*`, `crt-royale.slangp`,
  `scalefx.*`, `sharp-bilinear-simple.slangp`.
- License: slang-shaders is GPLv3 — verify per-shader headers before bundling
  anything.
- In-repo audit artifacts (always travel with the clone): `docs/audit/
  shader-support-audit-2026-10-14.md`, `slang_audit_v2.csv` (2658 presets),
  `vulkan-audit-codefindings-affected.csv`, `slang-codefindings-scan.csv`,
  `shader-test-list-2026-10-15.{md,csv}`, `vulkan-coverage-2026-10-15.csv`.

## 10. Restore / reproduce

Everything is committed on fork master — no bundles or WIP zips needed:

```bash
git clone https://github.com/VTSTech/DevilutionX devilutionx && cd devilutionx
cmake -B build -G Ninja -DCMAKE_BUILD_TYPE=Release   # 3rdParty deps fetch at configure
# reference corpus for offline preset sweeps (sandbox-local):
git clone https://github.com/libretro/slang-shaders reference/slang-shaders
```

Established loop: fixes are packaged → user pushes to **fork master** → fork CI
builds → field test on Windows (and Debian 12 VM) → next round branches from the
result. Nothing irreplaceable outside the repo except `reference/` (re-downloadable)
and `worklog.md`. This file + the worklog reconstruct the full state.
