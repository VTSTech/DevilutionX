# DevilutionX GPU Shaders — Session Handoff

**Last updated:** 2026-10-15 (America/Toronto). **SCOPE CHANGE (read the README first):** this is now a **standalone mod** — upstream `diasurgical/DevilutionX` enforces a no-AI policy, so PR #8737 is dead and all development, bugs, and features live here (pivot dated 2026-10-11 in the README). **No more DX12** (excised last session — debug code removed from `gpu_scaler.cpp`/`preset_runner.cpp`/`main.cpp`/`gpu_scaler.hpp`/`preset_runner.hpp`/`Source/CMakeLists.txt`/`docs/GPU_SHADERS.md`; 4 files deleted: `gpu_d3dcompile.{cpp,hpp}`, `tools/d3dcompiler_smoke.c`, `gpu_shaders_hlsl.hpp` — 1905 lines total). **No more baked shaders** (built-in ScaleFX/HQx filters removed at `77c53bf35`; only shared Vertex/Blit programs remain baked). **PRIMARY GOAL (2026-10-14, user direction): audit the shader implementation and parser against `docs/SLANG_SPEC.md`** — every `.slangp` key and `.slang` semantic annotated with fork implementation status, plus a priority-ordered gap list (see §8). v2 slang audit complete (script bug fixed: 1221→701 0-pass presets; risk distribution 33%/20%/46% low/med/high — see §12). F5/P1 fixes are applied on master `dce1c4222` (§13). **2026-10-15 (user direction): D3D12 field-binary forensics and wild-call scanning are RETIRED — all v3-binary / wild-call references were removed from this file; §7 is now a stub.** **2026-10-15 (second refresh): VTSTech moved the 40-preset field-sweep list in-repo (`docs/audit/shader-test-list-2026-10-15.{md,csv}`), documented the release helper scripts in GPU_SHADERS.md, tagged `gpu-shaders-v8` (release zips now ship `install_shaders.cmd` + `clear_shader_cache.cmd` beside the exe), and the README references both scripts and the in-repo audit artifacts.** **2026-10-15 (field round 3): the first live sweep log against the 40-preset list surfaced two failures — both fixed on `664d0f740` (macro-aliased sampler declarations for the nnedi3 deinterlacing chain; per-pass format downgrade with SDL error text for `test/format.slangp`) plus two latent bugs found while verifying (`#pragma format` was never wired to the runner; numeric pass keys were swallowed by the .params overlay tolerance, silently breaking scale for 1196 corpus presets). Field round 3 details in §8.** **2026-10-15 (field round 4): fork history was REBASED — `dce1c4222`/`664d0f740` no longer resolve (content verified in-tree; new tip `f49a29ce5`, tag `gpu-shaders-v9` at `b0ba28d5f`, see §2). The koko-aio-ng repeat-include known issue is FIXED (`c1ee40544`: include-once per stage bucket — §8). First Phase-1 field segment: 15/15 unique presets working across denoisers/reshade/sharpen, 0 fallbacks, with F1 mipmaps and PassFeedback confirmed live for the first time (magicbloom) — §8 field round 4.** **Segment 2 (same day, continued log): `denoisers/` complete (7/7) and `blurs/` 15/17 slots clean — including a 24-pass chain (`dual_filter_bloom`, longest field-tested) and F1 mipmaps on three more chains. Phase-1 total now 36 unique presets working, 38 loads, 0 failures across three logs.**

## 1. Mission (TL;DR)

Standalone mod on **VTSTech/DevilutionX**, branched from upstream `1.6.0-dev` (see the README "IMPORTANT" note — no upstream PRs):

- **Primary goal (2026-10-14):** **audit the shader implementation and parser against `docs/SLANG_SPEC.md`** and close the gaps in its priority order. The spec doc is the static arm of the campaign (status matrix + 14-item gap list); the field sweep in §12 is the empirical arm; the audit CSVs under `docs/audit/` join the two.
- **Current feature line (on fork master):** runtime **libretro slang presets**
  (`.slangp`/`.slang`) loaded from `.\shaders\` beside the exe — multi-pass chains,
  in-process glslang → SPIR-V, content-addressed disk cache + sidecar `.spv` fast path.
  Baked ScaleFX/HQx filters and their Settings entries are **removed**; Shader Filter =
  Off + folder-browser presets. Flip-parity fix field-verified; cross-build PNG codec
  fixed (hq2x LUTs load).
- **Backend scope:** **Vulkan-only on Windows and Linux** (Metal accepted on macOS at
  the device level but untested). `DEVILUTIONX_GPU_BACKEND=vulkan` continues to work as
  an explicit override. D3D12 is excised from the shipped code.
- **Tooling on master:** `tools/audit_slang_presets.py` (preset feature
  extractor — v2 ran on the full slang-shaders tree, see §12), `tools/audit/*`
  (crossref + codefindings scanners).

## 2. State snapshot

| Ref | Tip | State |
|---|---|---|
| fork `master` (what CI builds) | `f49a29ce5` | **history REBASED 2026-10-15** — pre-rebase hashes `dce1c4222` (F5/P1) and `664d0f740` (round 3) no longer resolve; content verified in-tree (rebased line `537ad782b`). On top: **round-4 include fix** (`c1ee40544`, koko-aio-ng per-stage-bucket include-once), `install_git.cmd` (`0874f803d`), Linux `install_shaders.sh`/`clear_shader_cache.sh` + CI tweak (`e6ba06ab4`), `tools/audit/status_report.py` (`f49a29ce5`); field round 4 = Phase-1 segment 1, 15/15 working (§8) |
| release tag **`gpu-shaders-v8`** | `92831323e` | v8 release binaries; first release shipping `install_shaders.cmd` + `clear_shader_cache.cmd` beside the exe (corpus download / cache wipe) |
| release tag **`gpu-shaders-v9`** | `b0ba28d5f` | v9 release binaries (README refresh); adds Linux helper scripts + `install_git.cmd` vs v8; `status_report.py` is post-v9 on master |
| PR **#8737** (`gpu-shaders-combined`) | `b11054ba2` | **DEAD — do not pursue.** Upstream enforces a no-AI policy (README, 2026-10-11); the PR will never merge. Kept for reference only; it still contains baked filters + the WAV fix |
| upstream **SDL_mixer PR #906** | `30034b15` | OPEN; fixes zero-length RIFF chunk bug (Diablo's 1996 WAVs fail to decode). Nice-to-have, **not gating**: the mod carries its own vendored `3rdParty/SDL3_mixer` patch + `SDL_LoadWAV_IO` fallback |
| fork branch `wav-mixer-fix` | `658e948` | standalone DevilutionX-side WAV fix; the in-master vendored patch + fallback already cover this |
| `gpu-shaders-folder` / `gpu-shader-scaler` | — | historical; the feature line lives on fork master (folder branch deleted from remote) |

PRs #8734/#8736/#8737: all dead — upstream (diasurgical) enforces a no-AI policy and this
is now a standalone mod; report bugs and request features **in this repository**
(README "IMPORTANT" note). Related upstream issue **#4453** (libretro shader support
request) and the demo video (https://youtu.be/uQqkW-1UWec?t=12) remain as history.
The old #8737 course-correction plan (post comment, strip WAV files, retitle) is **moot —
never post to upstream**. The WAV fix lives in-master: vendored `3rdParty/SDL3_mixer`
patch + `SoundSample::SetChunk` `SDL_LoadWAV_IO` fallback (documented in GPU_SHADERS.md).

## 3. Sandbox map

- `/home/z/my-project/DevilutionX` — main repo (the mod's own GitHub clone: `origin` =
  VTSTech/DevilutionX, the only remote that matters now). Local `master` tracks origin
  master (tip may lag; fetch first). The mod's docs live in-repo: `README.md`,
  `docs/GPU_SHADERS.md` (feature doc), `docs/SLANG_SPEC.md` (normative slang spec +
  status matrix — the audit baseline), `docs/diablo-handoff.md` (this file),
  `docs/audit/` (audit report + CSVs).
- `/home/z/my-project/worklog.md` — append-only multi-agent log; per-round records with
  exact edit-tooling gotchas.
- `/home/z/my-project/scripts/` — reusable tooling: per-round byte-exact patch scripts
  (`apply_*.py`, `fix_preset_folder_confusion.py`), CI repro, MinGW toolchain setup,
  `gen_upstream_wav_patch.py`.
- `/home/z/my-project/reference/` — slang preset research (see §10).
- `/home/z/my-project/download/` — user-facing deliverables: round docs patches
  (`0001-*`/`0002-*`/`0003-docs-*.patch`, latest round), the sweep list
  (`shader-test-list-2026-10-15.{md,csv}` — also committed in-repo),
  `diablo-handoff.md` (this file), older round zips
  (`devilutionx-gpu-fixes-for-master-v9.zip` latest), WAV-fix/upstream packages
  (`0001-WAV-*.patch`, `decoder_wav.c`, `SDL_mixer-upstream-PR-draft.md`,
  `PR8737-course-correction.md`, `SDL3-mixer-pin-bump.md`).

## 4. Design decisions (confirmed with VTSTech — do not re-litigate)

| Item | Decision |
|---|---|
| Format | libretro slang presets (`.slangp` + `.slang`), libretro preset spec semantics |
| Dir layout | `shaders\` NEXT TO THE EXECUTABLE (recursive scan, subfolders OK), not the in-Source tree |
| Compile | In-process glslang (FetchContent `3rdParty/glslang`) → SPIR-V; cache in `shaders/cache/`; `<name>.vert.spv`/`.frag.spv` sidecars always win |
| Multi-pass | Per-pass scale rules, filter/wrap, sRGB & float FBOs, aliases, `Original`/`OriginalHistory#`/feedback, LUT PNGs, `#pragma` params at defaults, `frame_count_mod`, minimal history depth |
| **Pass orientation** | Every pass rendering into an INTERMEDIATE target gets a Y-mirror MVP (`diag(1,-1,1,1)`); last pass keeps the legacy convention so the present blit cancels — even/odd chain flips fixed (`7ea8e6e93`, field-verified); vertex stages without an MVP uniform keep their own (unknown) orientation — flagged `vertexMvp=no` in the load log, none seen in the field |
| **D3D12** | **EXCISED (2026-10-14)** — SPIR-V → SPIRV-Cross → HLSL transpile path, guarded d3dcompiler, and forensics tooling all deleted (`gpu_d3dcompile.*`, `tools/d3dcompiler_smoke.c`, `gpu_shaders_hlsl.hpp`). §7 is a stub |
| Metal | Accepted on macOS at the device level (`CreateDevice` sets both `SHADERS_SPIRV` + `SHADERS_MSL` props; shared Vertex/Blit programs have MSL in `gpu_shaders_msl.hpp`). Preset passes submit SPIR-V, which SDL's Metal backend transpiles to MSL at runtime — **untested, no field validation yet** |
| Built-ins | REMOVED (user request, `77c53bf35`): no ScaleFX/HQx pipelines/list entries; `gpu_luts.hpp` is an unreferenced orphan (safe to delete); stale ini values 1–6 sanitize to Off; only shared `Vertex`/`Blit` programs remain baked (the `gpu_scaler_spv` build target still compiles the dead hqx/scalefx sources into the generated header — harmless, trimmable) |
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
`3rdParty/glslang` is a FetchContent wrapper (SPIRV-Cross is still declared but no
longer linked into the preset path). **Normative reference for all five preset TUs:
`docs/SLANG_SPEC.md`** — per-key/per-semantic status plus the gap list to work
(§8). Full commit series: `git log b0a3b7bab..dce1c4222` on fork master (per-round detail in
worklog); docs-only commits on top through `e501dd853`.

## 6. Resolved field issues (compressed history)

1. Vulkan `vkCreateGraphicsPipelines` assert + session death on preset failure → UBO
   decoration/offset bug fixed; failure contract introduced (round 1).
2. Junk in preset list, unbounded log, `#reference` paths unresolved → list filter, log
   cap + rotation, ancestor-dir path resolution (round 2).
3. **No window/output/crash** → magic-static self-deadlock: an `Options` member ctor read
   `GetOptions()`; rescan deferred to `LoadFromIni` (round 3).
4. scalefx-aa AV → feedback alias fell through to LUT branch → null texture deref; fixed
   with plain alias binding + null guard (round 4).
5. Wrong preset loaded on selection (folder confusion) → path committed after the
   synchronous notify chain + folder-blind index fallback; fixed by committing path
   BEFORE the value change and reloading on path change (round 5; also resolved the
   "FXAA upside-down" mystery as a wrong-preset side effect, round 6).
6. hq2x LUT + screenshots "Unsupported image format" → cross build's SDL3_image had NO
   PNG codec (STB backend off, no mingw libpng); fixed by vendoring SDL_image's own
   libpng+zlib (`d389b2357`) + zlib target-collision fix (`44307699f`); hq2x confirmed
   loading in the field (rounds 7–9).
7. **Vertical flips** (stock×2, hqx chains, fsr) → even/odd FBO pass parity; hqx's
   `hqx_refpass` alias shifts it by one. Fixed by the Y-mirror MVP on intermediate passes
   (`7ea8e6e93`); 12-preset `zz-diag-*` bisect grid proved the law (round 10).
## 7. D3D12 — out of scope (retired)

D3D12 is excised and the associated field-binary forensics are retired (user direction, 2026-10-15). This section is intentionally empty; see §4 for the design decisions and §8 for the live work queue.

## 8. Remaining tasks

### Active scope — audit implementation & parser against `docs/SLANG_SPEC.md` (PRIMARY)

The campaign now has two arms that feed each other:

- **Static arm (the audit proper):** `docs/SLANG_SPEC.md` is the baseline. It documents
  every `.slangp` key and `.slang` semantic with the fork's implementation status
  (supported / partial / extension / parsed-unused / unimplemented / divergent),
  cross-checked against RetroArch's `gfx/video_shader_parse.c`, and ends in a
  priority-ordered gap list (its §7, 14 items). **Work that list top-down.** The top
  items, confirmed against master `dce1c4222`:
  1. ✅ **F6 viewport aspect** — FIXED (`f87f89d18`): the runner derives the aspect-corrected
     viewport (game image rect letterbox-fitted into the swapchain, half-up rounding) and
     resolves `ScaleType::Viewport` AND `FinalViewportSize` against it, so the 232-preset
     `uses_mvp + viewport_scale` cluster computes geometry for the area the game actually
     occupies; a viewport-scaled final pass lands exactly on the presentation blit's
     letterbox rect. Regression test: `tools/audit/test_viewport_derive.cpp` (13 checks).
  2. ✅ **`PassFeedback#Size` latent crash** — FIXED (`6f13d787f`): length-aware prefix parse in
     `FillBlockBytes` (10 for `PassOutput#`, 12 for `PassFeedback#`); previously `stoi` received
     `"ck#"` → uncaught `std::invalid_argument` → process abort. The same commit guards the
     translator's uniform `[N]` size parse (same crash class) and adds the regression test
     `tools/audit/test_uniform_names.cpp` (13 checks, g++ -O2, exit 0).
  3. ✅ **Param-case quirk** — FIXED (`6f13d787f`): the runner applies `#pragma` param overrides
     via `CaseInsensitiveEqual`, so the parser's lowercased keys land regardless of shader
     spelling (Mega Bezel mixed-case params no longer silently keep defaults).
  4. ✅ **F1 mipmaps** — FIXED (2026-10-14, F1 round): pass outputs targeted by a
     mip-reading next pass allocate full mip chains and `SDL_GenerateMipmapsForGPUTexture`
     refills them each frame between render passes; pass 0 reads the chain input through a
     mip-capable RGBA8 copy (blitted + refilled per frame; the game texture stays a
     single-level scaler surface). LUT textures now carry the usage flags SDL's
     GenerateMipmaps contract requires. Regression test: `tools/audit/test_mipmap_plan.cpp`
     (32 checks, g++ -O2, exit 0).
  5. ✅ **`Rotation` semantic** — FIXED (2026-10-14, gap-#5 round): whitelisted in the
     translator and filled with the identity value 0 (spec semantic: uint 0..3 = 0/90/180/270
     degrees; this frontend never rotates). Not 0-corpus: 16 corpus shaders declare
     `uniform uint Rotation;` (crt-effects, authentic_gbc, border fills, subframe BFI) and
     previously failed translation loudly. Regression test: `tools/audit/test_rotation_format.cpp`.
  6. ✅ **`#pragma format` token gaps** — FIXED (2026-10-14, gap-#6 round): the two-channel
     integer tokens (`R8G8`/`R16G16`/`R32G32` × `UINT`/`SINT`) and the packed 10-bit UNORM
     token now map (18 corpus shaders request `A2B10G10R10_UNORM_PACK32`); the `_UINT_PACK32`
     variant stays warn + RGBA8 (no SDL GPU UINT form of the packed 10-bit layout, 0 corpus
     hits). Regression test: `tools/audit/test_rotation_format.cpp` (34 checks).
  7. …continue per SLANG_SPEC.md §7 — every remaining item is 0-corpus or an intentional
     divergence (`clamp_to_border`, `OriginalHistory` cap, translator hard-error paths,
     `#pragma alias`/`include_optional`, `FrameDirection`, `#reference` comment,
     `feedback_pass`/`histories`).
- **Empirical arm (the field sweep):** the §12 validation methodology below. Every
  sweep failure gets categorized and mapped back onto a SLANG_SPEC status cell —
  a fallback is either a spec gap already listed (close it) or a new finding (add it
  to the spec's gap list, then fix).
- **Field round 1 (2026-10-14, first in-game log, before the gap-#5/#6 round):**
  10 preset-load failures across 13 attempts, all classified and fixed —
  trailing-comment pragma parameters dropped (scanlines-Modern; 58 corpus lines),
  no-space comma declarators glued into bogus member names (simple_color_controls,
  anime4k, ossc; 34+ corpus lines), unknown UBO members hard-failing where RetroArch
  zero-fills (`auto_res` x5, `XBR_ANTI_RINGING`, `ia_FLIP_HORZ`), and mixed-case
  preset aliases (`Pass0`, `PrePass0`) failing the lowercased deduction. All four
  closed as SLANG_SPEC §7 items 12/15/16/17 with regression test
  `tools/audit/test_field_round1.cpp` (28 checks). The same log confirms the
  fixed plumbing working in the field: F6 viewport lines (`viewport 1920x1080`),
  multi-pass chains (hq2x/hq4x/ScaleHQ/SABR, 7-pass nedi), `hqx_refpass`
  alias + LUT sampling, sRGB caches, and the failure contract (never died,
  always fell back to the regular renderer).

Reconciliation rule: a preset counts as covered only when its SLANG_SPEC feature set is
all `supported`/`extension` AND it passes the §12 field sweep acceptance criteria.

### Field validation (empirical arm) — sweep queue

- ✅ **Field round 3 (2026-10-15):** deinterlacing cluster live-tested; nnedi3
  chain fixed (macro-aliased sampler declarations) — re-test the three nnedi3
  Tier 1a canaries plus any koko-ambi/koko-aio preset on the next sweep build.
- ✅ **Field round 4 (2026-10-15, Phase-1 breadth segment 1):** two sessions on one
  v9-lineage Windows CI build — 16 preset loads / 15 unique, **0 fallbacks, 0
  crashes, clean shutdown**; F1 mipmaps + PassFeedback confirmed live for the
  first time (magicbloom); segment 2 swept
  `denoisers/` (complete) + `blurs/` (15/17) incl. a clean 24-pass chain — 36
  unique presets working total. Details in the round-4 subsection below.
- ✅ **`audit_slang_presets.py` v2 run complete** (§12) — CSV of all 2658
  `.slangp` presets with feature flags + risk heuristic. Risk distribution
  after the script bug fix: low=886 (33%), medium=539 (20%), high=1233 (46%).
  701 presets still report 0 passes (down from 1221 in v1) — these are
  `#reference` inheritance chains where the referenced path doesn't resolve
  under the audit root (mostly `bezel/Mega_Bezel` 468 + `bezel/koko-aio`
  180 + `hdr/sony_megatron_*` 34). CSV lives in-repo: `docs/audit/slang_audit_v2.csv`.
- ⬜ **Validate the low-risk presets on Vulkan.** Build the game with
  `-DUSE_SDL3=ON -DUSE_GPU_SCALER=ON` (CI does this), drop the
  slang-shaders tree into `shaders/` next to the exe (or run the shipped
  `install_shaders.cmd`), launch with
  `DEVILUTIONX_GPU_BACKEND=vulkan`, walk the preset list, screenshot each.
  Track which produce a visible image and which fall back (session fallback
  to the classic renderer — see §4 failure contract). **Start with the
  40-preset tiered list committed at
  `docs/audit/shader-test-list-2026-10-15.md`** (Tier 1 = regression
  canaries for the F5/round-1/round-2 fixes; Tier 2 = low-risk breadth +
  medium cluster reps) — fill its CSV and send `gpu-scaler.log` for every
  fallback. After that continue with the full 185 low-risk non-zero-pass
  presets (top dirs: `reshade/` 50, `misc/` 18,
  `handheld/` 17, `presets/` 14, `deinterlacing/` 10, `dithering/` 10,
  `crt/` 9).
  **Progress (field round 4, 2026-10-15, segments 1+2):** 36 Phase-1 presets
  confirmed working across `denoisers/` (7/7 — dir complete), `blurs/` (15/17),
  `reshade/` (8), `sharpen/` (6); tiered list updated (Tier 1b Anime4k + Tier 2a
  FilmGrain/bsnes-gamma-ramp/bilateral-2p/gauss_4tap `working`; Tier 1a
  backfilled from round 3). Still open: nnedi3 ×3 + koko re-tests, then the
  remaining low-risk dirs (`misc/` 18, `handheld/` 17, `presets/` 14,
  `deinterlacing/` 10, `dithering/` 10, `crt/` 9, and the rest of `reshade/`).
- ⬜ **Validate the 539 medium-risk presets** — top dirs: `presets/` 105,
  `edge-smoothing/` 87, `crt/` 70, `handheld/` 35, `border/` 34,
  `interpolation/` 32, `downsample/` 26, `pixel-art-scaling/` 20.
  Most common feature combo is `uses_mvp + viewport_scale` (232 presets) —
  a single fix in the viewport-scale path unblocks a big cluster.
- ⬜ **For each fallback, capture `gpu-scaler.log`** and categorize the
  failure: slang-translate error, glslang compile error, missing feature
  (history depth, feedback, alias, scale rule, LUT format), runtime
  validation layer error, crash. Each category maps to a fix in
  `slang_translate.cpp` / `preset_runner.cpp` / `preset_parser.cpp`.
- ⬜ **Fix the high-yield categories** (whichever buckets contain the most
  presets). Re-validate after each fix by re-running the sweep. The CSV can
  be diffed run-to-run to track progress.
### Known non-shader issue — Integer Scaling crash (documented, deliberately unfixed)

Field round 1 surfaced a crash when the game's own **Integer Scaling** option is
combined with a resolution that is not a whole-number multiple of the desktop
(2275×1280 on 1080p; 720p is fine). Ruled **out of scope by VTSTech (2026-10-14)**:
the crash is in the core window-size math (`CalculatePreferredWindowSize`,
`Source/utils/display.cpp` — integer factor truncates to 0 → divide-by-zero), plus a
latent zero-size-fit path in `PresentFrame` (`Source/engine/render/gpu/gpu_scaler.cpp`)
that honors the same option. It is user-facing documented in
[GPU_SHADERS.md](GPU_SHADERS.md) §Troubleshooting; the abandoned fix diff is snapshotted
in the mod distribution notes (`integer-scaling-known-issue/` in the mod packaging).
Do **not** re-diagnose or fix this in shader rounds; if a field crash matches this
signature (Integer Scaling on, odd resolution, no preset errors in the log), report it
as this known issue and move on.

### Field round 2 — offline preflight (2026-10-14, before the live sweep)

An offline translate-stage harness (real `ParsePreset` + `TranslateSlang`, no device)
was run over the round-2 test list from the freshly accessible corpus clone
(`presets/`, `crt/crt-effects/`, `bezel/{Mega_Bezel,uborder}` layout). Result: 117
passes across 17 of 18 presets translate clean; 3 fix-worthy failure classes were
found and fixed BEFORE the live sweep (§7 items 18–20):

- **F-A orphan samplers** — MBZ `hsm-custom-fast-sharpen` declares `PrePass0` and
  `#define`s it to `Source`; uborder single-pass `crt-geom` declares
  `ambi_temporal_pass` never sampled. Both now drop the declaration with a load
  warning; a SAMPLED unknown name still hard-fails (the reflections-active uborder
  variant samples it from `uborder_bezel_reflections_main.inc:33` and is genuinely
  broken in RetroArch too — use `base_presets/koko-ambi/koko-ambi-crt-geom.slangp`
  (6 passes, clean) as the uborder crt-geom representative instead).
- **F-B comment-unaware scanning** — a commented-out `/* uniform UBO ... */` in MBZ
  `hsm-interlace-and-linearize` tripped the one-UBO guard. Comments are stripped from
  the working stage copy before any rewrite; `#define` lines survive for glslang.
- **F-C undeclared texture modifier keys** — `delta_filter_linear = true` (for a
  texture named `delta`; crt-geom-deluxe-ntsc-adaptive) derived a pathless texture and
  failed the preset. Modifiers now attach only to existing textures; undeclared keys
  are ignored like RetroArch.

Expected live-log noise for this round (all informational, RetroArch-conformant):
zero-filled members (`sborder`, `prescalex`, `mask`, `warpX/Y`, `vertmask`,
`shadowMask`, `rolling_scan`, `ring`, ... on crt-royale/guest chains),
`CurrentSubFrame`/`TotalSubFrames` zero-fill on 120hz-smart-BFI (subframe modulation
will be frozen at subframe 0 — candidate future semantic if it looks wrong), and the
new orphan-sampler drop warnings. Regression test: `tools/audit/test_field_round2.cpp`
(28 checks, links the real parser+translator; see its header for the build line).

### Field round 3 — first live sweep log (2026-10-15, v8 Windows build)

VTSTech ran the deinterlacing cluster + `test/` presets on hardware and sent the
`gpu-scaler.log`. Wins first: `bob-deinterlacing`, `motion-adaptive-deinterlacing`,
`deinterlace`, `mix_frames`, `shutter-to-anaglyph`, `test/history.slangp` (F5 canary —
gap-index history ring works in the field), `test/feedback.slangp` (F6 canary), and
`test/feedback-noncausal.slangp` was correctly REJECTED with
`slang: PassOutput0 is not causal in pass 0` (the negative control proving the failure
contract). Session fallbacks were clean (filter off, classic renderer, game alive).

Two failures, both fixed in `664d0f740`:

- **nnedi3-nns16/nns64-deinterlacing** — pass 3
  (`edge-smoothing/nnedi3/shaders/nnedi3-combine-v-pass0.slang`) failed glslang with
  `'nnediPass0' : undeclared identifier` right after the load log printed
  `sampler 'NNEDI3_PREV_TEXTURE' has no chain target (#defined away); dropping
  declaration`. Root cause: object-like macros can alias the sampler DECLARATION
  itself — the shader does `#define NNEDI3_PREV_TEXTURE nnediPass0` and its include
  (`nnedi3-combine-common.inc`) declares `uniform sampler2D NNEDI3_PREV_TEXTURE;`;
  glslang's preprocessor renames that declaration AND every `texelFetch` to
  `nnediPass0`, so the round-2 orphan drop (correct for Mega Bezel's
  `#define PrePass0 Source`, where `Source` is declared anyway) removed the only
  declaration of the alias target. `RewriteSamplers` now keeps the declaration under
  the expanded identifier when (a) the alias is referenced beyond its own declaration
  (koko-aio passes `FPS_ESTIMATE_PASS` into `get_fps(...)` — the sampling-call
  heuristic alone misses this), (b) the `#define` body is a single identifier, and
  (c) the expansion is not independently declared in the stage. The expanded name
  then resolves through the normal known-name paths (preset `aliasN` — here
  `alias1 = nnediPass0`, `#pragma name`, `<name>Feedback`, LUTs). Reproduced
  verbatim offline: dumped the pre-fix pass-3 fragment GLSL through
  `translate_preflight -dump` and got the exact field error from glslang 16.6.0;
  post-fix all 5 nnedi3 variants (25 fragment passes) compile under Vulkan 1.0.
  Corpus sweep: 73 bogus drops eliminated (41 `NNEDI3_PREV_TEXTURE`, 29 `PastSampler`
  across koko-ambi/koko-aio/uborder, 3 `FPS_ESTIMATE_PASS`), failure set unchanged —
  the koko-ambi family (incl. the Tier 1c rep `koko-ambi-crt-geom`, 6/6 passes) is
  compile-clean now too.
- **`test/format.slangp` pass 1** — `pipeline creation failed` with zero diagnostics:
  SDL builds a transient VkRenderPass with the pass's color format, and a format the
  device refuses as a color attachment (single-channel float/int targets are NOT
  mandatory attachment formats in Vulkan) fails without surfacing SDL's error text.
  Non-default formats now downgrade to `R8G8B8A8_UNORM` with a
  `color target format unavailable` warning and the target texture follows
  `pass->targetFormat`; hard failures now include `SDL_GetError()`.

Two latent bugs found while verifying (both fixed in the same commit):

- **`#pragma format` was dead code at the runner** — the token was captured into
  `SlangTranslation::pragmaFormat` and never propagated to `PassDef::pragmaFormat`
  (which `FormatForPass` reads), so the spec-4.2 override silently did nothing since
  `ae938cfb1` introduced the mapping table. Wired through; the in-repo
  `tools/audit/field-presets/test/format.slangp` ladder (default / `R16_SFLOAT` /
  `R8G8B8A8_SRGB`) exercises it.
- **Numeric pass keys swallowed by the .params overlay tolerance** — the
  "accept any numeric key as an override" fallback sat in front of the pass-key
  split in `preset_parser.cpp`, so `scale_x0 = 2.0` / `scale_y0 = ...` became bogus
  parameter overrides and the pass kept the default scale. 1196 of 3454 corpus
  presets carry `scale_x`/`scale_y` without a `parameters` list (35%!). Reordered
  after the pass-key and implicit-texture handling; verified offline
  (`scale_x0 = 2.0` now yields `scale=source*2`).

Tooling/tests this round: `tools/audit/test_field_round2.cpp` re-created (the
original was cited by the docs but never committed; 22 checks), new
`tools/audit/test_field_round3.cpp` (39 checks: helper copies + end-to-end
macro-alias keeps + the vertex-stage guard + format downgrade decision),
`translate_preflight -dump <dir>` for offline glslang validation, and the four
`test/` field presets re-created in-repo under `tools/audit/field-presets/test/`
(history / feedback / feedback-noncausal / format + README).

**RESOLVED (2026-10-15, `c1ee40544`, field round 4 code round): koko-aio-ng repeat-includes.**
`ResolveIncludes` WAS include-once GLOBALLY (a visited-set), but
`bezel/koko-aio/shaders-ng/shift_and_bleed.slang` deliberately includes
`includes/functions.include.slang` in BOTH stage sections (line 23 vertex, line 67
fragment); include-once placed the content only in the vertex bucket, so the
fragment stage misses `rgb2yiq` and glslang fails (`no matching overloaded function
found`). Confirmed identical on the pre-round-3 translator (not a round-3
regression). Fixed exactly as designed here: include-once is now scoped PER STAGE
BUCKET — the include walker uses the same `#pragma stage` matcher as the stage
splitter and keeps a visited set per bucket (shared/vertex/fragment), plus an
active-include stack guarding cross-bucket self-recursion that per-bucket sets
cannot see. Regression presets: `tools/audit/field-presets/test/r4-include.slangp`
+ `r4_*.inc`/`r4_include.slang`. Sweep tooling landed with it:
`tools/audit/sweeps/{corpus_translate_sweep,corpus_sweep_diff,include_diff_replica,verify_glslang_changed}.py`.
Field re-test of the koko-aio-ng family (3 presets) still pending on the next sweep
build; koko-ambi unaffected (verified 6/6 passes compile).

### Field round 4 — Phase-1 breadth, segment 1 (2026-10-15, v9-lineage build `Oct 14 2026 21:03:48`)

Two sessions in one `gpu-scaler.log`, Vulkan, 1920x1080 — 16 preset loads / 15
unique: **15/15 working, 0 fallbacks, 0 crashes, clean `Shutdown`**. Log-only
evidence: visual acceptance per §12 (no flip/black/corruption) is presumed but was
not explicitly reported; nothing in the log suggests a defect. Coverage CSV:
`vulkan-coverage-2026-10-15.csv` (sandbox `download/`); tiered-list CSV filled
in-repo (round-3 Tier 1a backfill + round-4 rows).

| dir | working presets (passes) |
|---|---|
| `denoisers/` | crt-fast-bilateral-super-xbr (8) — loaded in BOTH sessions: session 1 compiled all 8 fresh (~0.6 s, 20.2 s dwell), session 2 hit 8/8 disk cache (~0.35 s, 14.3 s dwell) |
| `reshade/` | magicbloom (12), bloom (9), lut (1), halftone-print (2), bsnes-gamma-ramp (1), blendoverlay (1), NormalsDisplacement (1), FilmGrain (1, 63.5 s dwell) |
| `sharpen/` | super-xbr-super-res (11), Anime4k (4), adaptive-sharpen-multipass (2), adaptive-sharpen (1), rca_sharpen (1), cheap-sharpen (1) |

First field confirmations:

- **F1 mipmaps (first live hit):** magicbloom runs 11 mip-chained pass outputs —
  `mipmapped inputs (F1): pass outputs with mip chains [0..10], chain-input copy
  off`; its blur pyramid descends 1440x810 → 76x43 with zero errors.
- **PassFeedback (first live hit post-pivot):** magicbloom pass 10 samples
  `tMagicBloom_AdaptFeedback` (auto-exposure adaptation ring) — the 23-preset
  (0.9%) pass-feedback cluster gets its first field data point (the Tier 1c
  pass-feedback canary `crt-geom-deluxe-ntsc-adaptive` is still queued).
- **Cache, both modes:** cross-preset shader hits (`LUT.slang` shared by
  lut/halftone-print/bsnes-gamma-ramp; `stock.slang` + super-xbr passes shared
  across denoisers/sharpen chains) and full-chain hits on session-2 reload (8/8).
- **LUT/texture plumbing:** SamplerLUT1/2 (hyllian multiLUT), SamplerLUT,
  `overlay` (blendoverlay), `displacementMap` (NormalsDisplacement),
  `Dirt`/`Sprite` (bloom LightingCombine), `tMagicBloom_Dirt` — every PNG loaded,
  no codec errors.
- **`Original` semantic** sampled throughout the bloom chain
  (`textures=[Source, Original]`).
- **Aliases** across chains: XbrSource, diff_refpass, REF, PreBloomPass,
  SamplerBloom1–5, LensFlare1–3, tMagicBloom_1–8/Small/Adapt.
- **Hot-switching:** 16 Reinit cycles without a single load failure or fallback;
  OSD name matched the selection on every switch.
- **Game-state transition:** dungeon `ShowProgress` under Anime4k, no reinit
  errors, then clean shutdown.

Pacing: 235–315 frames per 5 s heartbeat (~47–63 fps at 1080p).

**Segment 2 (same day, continued log — `denoisers/` + `blurs/`):** 22 loads /
21 unique (+ crt-fast-bilateral re-loaded, 8/8 cache in ~31 ms) — **all working,
0 errors/warnings/fallbacks, clean exit**. Highlights:

- **Longest field-tested chain:** `blurs/dual_filter_bloom` — 24 passes, loaded
  from cache in ~0.1 s, zero errors (siblings 18/8 passes also clean).
- **F1 mipmaps ×3 more:** mip chains on pass outputs [20] / [14] / [4] of the
  dual_filter bloom variants — F1 now field-proven on 4 distinct chains.
- **Cache reuse at scale:** 78 cache-hit lines; `dual_filter/upsample|downsample.slang`
  shared across all six dual_filter chains, `kawase/linearize.slang` across kawase.
- **Dirs:** `denoisers/` complete (7/7); `blurs/` 15/17 listing slots — slots 110
  and 113 skipped in the browser (no failures; just untested).
- **Tier 2a canaries landed:** `denoisers/bilateral-2p`, `blurs/gauss_4tap`.

Still queued from
earlier rounds: nnedi3 ×3 + koko-ambi/koko-aio re-tests (the round-3 fixes and the
round-4 include fix are both in this build lineage but were not exercised by this
log).

### D3D12 — fully excised

D3D12 (SPIR-V → SPIRV-Cross → HLSL transpile path, guarded d3dcompiler, and
debug tooling) was removed from the tree on 2026-10-14 — 4 files / 1905 lines
deleted (`gpu_d3dcompile.{cpp,hpp}`, `tools/d3dcompiler_smoke.c`,
`gpu_shaders_hlsl.hpp`) plus debug code stripped from `gpu_scaler.cpp`,
`preset_runner.*`, `main.cpp`, `Source/CMakeLists.txt`, and
`docs/GPU_SHADERS.md`. The shipped binary only ever uses Vulkan (or Metal on
macOS); with no Vulkan driver the scaler fails cleanly to the regular
renderer per the §4 failure contract. The detailed excision record lives in
the git history.

### Carried over from before the pivot

- ✅ README + docs aligned to the standalone-mod scope (2026-10-14): README rewritten
  by the user; `docs/SLANG_SPEC.md` created and committed; `docs/GPU_SHADERS.md`
  rewritten for the runtime-preset pipeline this session; this handoff reframed.
- ✅ ~~#8737 course correction (§2)~~ — DEAD, never post to upstream (no-AI policy).
- ⬜ Watch #906 (optional, not gating): the vendored SDL3_mixer patch +
  `SDL_LoadWAV_IO` fallback already fix Diablo's 1996 WAVs in-master; a merge upstream
  would only matter for a future `DEVILUTIONX_SYSTEM_SDL_MIXER=ON` release build.
- 🔶 26-i: Linux gcc/clang build verification — clang-format 18 half DONE (2026-10-14,
  `89c7db782`): reformatted `preset_runner.cpp/.hpp`, `gpu_scaler.cpp`, `main.cpp`
  (space-indent shipped across the whole subsystem); whole-tree dry-run rescan
  (696 files) = 0 violations; the only non-whitespace delta was one line-join.
- ✅ 26-j: GPU_SHADERS.md for runtime presets — done (rewritten this session; the
  baked-filter/`gpu_shader_spv_prebuilt` description is gone; the "two dialects"
  backend text now matches the D3D12 excision)
- ⬜ Cosmetic: highlighted row can coincide with an unrelated preset while browsing a
  different folder than the active one (selection itself is correct)
- ✅ hq2x/PNG codec, screenshots codec, fsr/hqx/stock flips, deadlock, selection, crash
  containment — done (§6)

## 9. Hard-won gotchas (do NOT relearn)

- Repo identity (CHANGED 2026-10-14): this is a **standalone mod** — do NOT open PRs
  against `diasurgical/DevilutionX` (upstream enforces a no-AI policy; #8737 will never
  merge). Issues, PRs, and releases live on **VTSTech/DevilutionX**. Author identity:
  GitHub maps both `VTSTech <14359327+VTSTech@users.noreply.github.com>` and
  `Nigel Todman <veritas@vts-tech.org>`; keep one form per series.
- clang-format **18** enforced by CI over `Source`. Verify with a REAL dry-run:
  `clang-format --style=file --dry-run -Werror <files>` — claims without the run have
  been wrong before (8-space indent shipped as "clean"). Note the CI job (jidicula
  v4.18.0) scans EVERY C/C++ file under `Source` + `test`, not just the diff — one
  stale-indented file anywhere turns the push red, and docs-only pushes don't trigger
  it at all (paths-ignore), so silence is not proof. Sweep closed 2026-10-14 (`89c7db782`).
- **Include-order trap:** `<tlhelp32.h>` (and similar Windows headers) must come AFTER
  `<windows.h>` — mingw-w64 headers don't self-prime. `SortIncludes: true` re-sorts
  single blocks alphabetically, so separate order-dependent headers with a blank line +
  comment, and compile the TU with mingw g++ BEFORE pushing to CI. Local harness:
  extract `g++-mingw-w64-x86-64-win32` (brings cc1plus + target libstdc++), then
  `-std=c++23 -fsyntax-only -DUSE_SDL3 -I Source -I <pinned SDL>/include -I
  <unordered_dense>/include -I 3rdParty/tl -I <magic_enum>/include` (SDL/unordered_dense/
  magic_enum are FetchContent — grab the pinned tarballs once).
- **`git am --keep-cr` is MANDATORY** (CRLF repo); plain am corrupts diffs.
  `git format-patch` has no `--keep-cr` (am-side only).
- Helpers at the TOP of a TU (before `namespace devilution`) cannot use
  `StrCat`/`StrAppend`/`Log*` — keep them pure std.
- mingw's `IMAGE_NT_HEADERS` has NO `Sections[]` member (MSVC extension) — use
  `IMAGE_FIRST_SECTION(nt)`; compiles on MSVC, fails on mingw CI otherwise.
- Cross builds have NO system libpng: SDL_image without the STB backend and without
  vendored libpng silently has NO PNG codec (IMG_Load/IMG_SavePNG fail). Vendored
  libpng+zlib is mandatory (`SDLIMAGE_VENDORED=ON` + `GIT_SUBMODULES` — release tarballs
  lack `external/`).
- **Vendored-dep target collisions** (CMP0002): SDLIMAGE_VENDORED defines
  `zlib`/`zlibstatic`; superproject zlib is deferred for SDL3 and reuses SDL_image's
  (`44307699f`). When adding a vendored dep, grep `3rdParty/` for duplicate targets first.
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
  `Reinit` → preset reload inside `SetActiveListIndex`). Commit companion state (preset
  path!) BEFORE changing the value.
- The preset enum `100+N` is a folder-listing index and goes stale; the stored relative
  path is the only durable selection. Never re-add a value-index fallback.
- OSD preset name = loaded chain's `preset.displayName`; if the OSD lies, the wrong
  preset loaded.
- Byte-exact editing: the `Edit` tool mangles tab indentation in this repo — use the
  Python patch scripts in `scripts/` (per-file EOL: `options.cpp` CRLF+tab;
  `gpu_scaler.cpp`/`preset_parser.cpp` LF+tab), idempotent, assert-1-match. (Recent
  files like `gpu_d3dcompile.cpp` are clang-format-managed: plain edit + `clang-format
  -i` also works.)
- GUI-subsystem exe: nothing on stdout; `--verbose` only raises SDL priorities.
  Diagnostics live in `gpu-scaler.log` (200 KB cap, one `.old` generation).
- Frame handed to the scaler is already RGBA32 (palette resolved upstream) — no palette
  pass needed.
- Known upstream bug (documented in #8737, non-blocking due to fallback): Vulkan ~167 s
  idle crash on some drivers.
- tmpfiles.org: 60-minute expiry; the tokenized `/dl/...` link (from the view page HTML)
  bypasses the interstitial.
- OBS `graphics-hook64.dll` injects into ANY process that creates a D3D device — device-
  less smoke tools can't reproduce injection-dependent behavior; check the module scan
  before blaming game code.
## 10. Reference material (`/home/z/my-project/reference/`)

- Full libretro/slang-shaders tree (user-uploaded 7z, extracted):
  `reference/slang-shaders/slang-shaders-master/slang-shaders-master/` — 2658 `.slangp`
  + 1653 `.slang`; archive kept at `/home/z/my-project/upload/slang-shaders-master.7z`
  (45 MB LZMA2).
- Key locations: HQx `edge-smoothing/hqx/hq{2,3,4}x.slangp` (+ LUT PNGs in `resources/`);
  ScaleFX under `edge-smoothing/scalefx/` + `presets/scalefx-plus-smoothing/`.
- Study samples at `reference/` root: `crt-easymode.*`, `crt-royale.slangp`,
  `scalefx.*`, `sharp-bilinear-simple.slangp`.
- License: slang-shaders is GPLv3 — verify per-shader headers before bundling anything.
- `docs/SLANG_SPEC.md` — slang shader specification reference (2026-10-14): every `.slangp` key + `.slang` semantic annotated with fork implementation status (supported/partial/extension/unimplemented/divergent), grounded in a full read of the five preset TUs at master `dce1c4222` and cross-checked against RetroArch's `gfx/video_shader_parse.c`. Includes a priority-ordered gap list for the coverage campaign (F6 viewport aspect, F1 mipmaps, param-case quirk, and a NEW latent crash: `PassFeedback#Size` uniforms hit `stoi("ck#")` in `FillBlockBytes` — uncaught `std::invalid_argument`, not in the audit report's finding set).

## 11. Restore / reproduce

Everything is committed on fork master — no bundles or WIP zips needed:

```bash
git clone https://github.com/VTSTech/DevilutionX devilutionx && cd devilutionx
cmake -B build -G Ninja -DCMAKE_BUILD_TYPE=Release   # 3rdParty deps fetch at configure
# reference tree for offline preset sweeps:
7z x /home/z/my-project/upload/slang-shaders-master.7z -o/reference/
```

Established loop: fixes are packaged → user pushes to **fork master** → fork CI builds →
field test on Windows → next round branches from the result.

Nothing irreplaceable outside the repo except `reference/` (re-downloadable)
and `worklog.md`. This file + the worklog reconstruct the full state.

## 12. Vulkan libretro preset coverage plan (v2 audit complete — 2026-10-14)

Scope: maximize the count of working libretro slang presets on the Vulkan
backend. D3D12 is fully excised from the shipped code (§8). The Vulkan path
was always the field-verified one; the runtime preset executor
(`preset_runner.cpp`) now has a single Vulkan-only code path (the D3D12
SPIRV-Cross → HLSL transpile path is deleted). The work is breadth, not
depth — find which of the 2658 slang presets work today, fix the high-yield
failure categories, repeat.

**Relation to the primary audit goal (§8):** this section is the *empirical*
arm — what actually renders in the field. The *static* arm is
`docs/SLANG_SPEC.md` (per-key status + gap list). Reconcile after every
sweep: each fallback category below should either close a listed spec gap or
add a new row to it.

### Tooling shipped this session

- **`audit_slang_presets.py`** (`/home/z/my-project/download/`) — static
  feature extractor for the libretro slang-shaders tree. Walks every
  `.slangp`, resolves `#reference` inheritance, walks every `shaderN = "..."`
  entry plus transitive `#include`s, and emits a CSV with one row per
  preset. Run:

  ```bash
  python3 audit_slang_presets.py /path/to/slang-shaders > presets.csv
  ```

  v1 had a regex bug — `^\s*shaders\s*=\s*(\d+)\s*$` required no quotes
  around the number, but the libretro spec allows `shaders = "6"`. Fixed in
  v2: every value regex now accepts optional double quotes. v1 reported 1221
  presets as 0-pass; v2 reports 701 (the remainder are `#reference`
  inheritance chains where the referenced path resolves outside the audit
  root — mostly `bezel/Mega_Bezel` 468 + `bezel/koko-aio` 180 +
  `hdr/sony_megatron_*` 34).

### v2 audit results (2658 presets, full slang-shaders tree)

Risk distribution (after the script bug fix):

| Risk   | Count | %  | vs v1 |
|--------|-------|----|-------|
| low    | 886   | 33% | ↓ (was 958, 36%) |
| medium | 539   | 20% | ↓ (was 738, 28%) |
| high   | 1233  | 46% | ↑ (was 962, 36%) |

v1's "low" bucket was inflated by the 1221 0-pass presets (all classified
low because no features were detected). After the fix, those redistribute
to their real risk levels — most land in `high` (the `Mega_Bezel` /
`scanline-classic` / CRT-royale families use every feature in the spec).

**Feature frequency** (top 10, across all 2658 presets):

| Feature | Count | %  |
|---|---|---|
| `uses_mvp` | 1957 | 73.6% |
| `uses_frame_count` | 1830 | 68.8% |
| `pragma_param` | 1844 | 69.4% |
| `viewport_scale` | 1355 | 51.0% |
| `uses_includes` | 1299 | 48.9% |
| `named_lut` | 1126 | 42.4% |
| `mipmap_input` | 892 | 33.6% |
| `float_fb` | 482 | 18.1% |
| `srgb_fb` | 371 | 14.0% |
| `orig_hist` | 348 | 13.1% |

`pass_output` (1 preset), `pass_fb` (23, 0.9%), `frame_time` (5, 0.2%),
`frame_dir` (212, 8.0%) are rarer — these are the long-tail features
where the implementation may have gaps.

### Validation sweep prioritization

**Phase 1 — low-risk, non-zero-pass (185 presets):** these are the highest-
yield first batch. The Vulkan path is field-verified on similar presets;
expect most to Just Work. Top directories:

| Dir | Count |
|---|---|
| `reshade/` | 50 |
| `misc/` | 18 |
| `handheld/` | 17 |
| `presets/` | 14 |
| `deinterlacing/` | 10 |
| `dithering/` | 10 |
| `crt/` | 9 |
| `blurs/` | 6 |
| `denoisers/` | 6 |
| `sharpen/` | 5 |

**Phase 2 — medium-risk (539 presets):** the bulk of the work. Top
directories and the dominant feature combo per cluster:

| Dir | Count | Dominant combo |
|---|---|---|
| `presets/` | 105 | mixed |
| `edge-smoothing/` | 87 | MVP + viewport_scale (xBR family) |
| `crt/` | 70 | MVP + includes (CRT shader chains) |
| `handheld/` | 35 | MVP + viewport_scale (LCD emulation) |
| `border/` | 34 | MVP + viewport_scale (bezel overlays) |
| `interpolation/` | 32 | MVP + viewport_scale (motion blur) |
| `downsample/` | 26 | float_fb + MVP (bloom/blur) |
| `pixel-art-scaling/` | 20 | MVP + viewport_scale |

The single most common medium-risk combo is **`uses_mvp + viewport_scale`**
(232 presets). A single fix in the viewport-scale code path
(`preset_runner.cpp` `ScaleType::Viewport` handling) would unblock the
largest cluster.

**Phase 3 — high-risk (1233 presets):** save for last, or skip. The
dominant combo is **`mipmap_input + uses_includes + uses_mvp +
viewport_scale`** (548 presets — mostly `bezel/Mega_Bezel` CRT emulation
chains with 18-45 passes each). The "kitchen sink" combo
`abs_scale + float_fb + mipmap_input + orig_hist + srgb_fb + uses_includes
+ uses_mvp + viewport_scale` appears in 119 presets (CRT-royale-style).

**Phase 4 — the 701 unresolved 0-pass presets:** mostly `#reference`
inheritance chains where the referenced path is outside the audit root.
A separate audit script run with the reference target resolved would
classify these. Likely high-risk (Mega_Bezel inheritance chains use every
feature).

### Vulkan validation methodology (per preset)

1. Drop the slang-shaders tree into `shaders/` next to the exe (recursive
   scan, subfolders OK — §4 dir layout).
2. Launch the game with `DEVILUTIONX_GPU_BACKEND=vulkan` (forces Vulkan —
   `gpu_scaler.cpp:719`). Since D3D12 is excised, the code will only ever
   pick Vulkan (or Metal on macOS); the env var is now belt-and-suspenders.
3. Walk the preset list (Settings → Graphics → Shader Filter → browse
   folders). For each preset:
   - Note the OSD name (`Shader Filter: <displayName>`) — if it lies, the
     wrong preset loaded (§9 gotcha).
   - Wait 2 seconds for the chain to compile + the first frame to render.
   - Check the in-game image: visible / black / distorted / flipped.
   - If fallback (image gone, "Custom (unavailable)" in OSD), capture
     `gpu-scaler.log` (200 KB cap, one `.old` generation — §9) and note
     the failure category (see below).
4. Screenshot each success. For failures, save the log + the preset path.

### Failure categories (mapped to source files)

When a preset fails on Vulkan, the `gpu-scaler.log` entry usually falls
into one of these buckets. Each bucket maps to a fix location:

| Failure category | Log signature | Fix location |
|---|---|---|
| slang → GLSL translation error | `translating '<shader>' (pass N): <error>` | `slang_translate.cpp` |
| glslang compile error | `compiling '<shader>' (pass N): <error>` | `slang_translate.cpp` (emit valid GLSL) |
| Missing preset feature (history, feedback, alias, scale rule) | `unresolved texture '<name>'` or silent misrender | `preset_runner.cpp` (binding logic) or `preset_parser.cpp` (spec coverage) |
| Vulkan validation layer error | `vkCreateGraphicsPipelines` / `vkUpdateDescriptorSets` validation message | `preset_runner.cpp` (pipeline/descriptor setup) |
| LUT PNG load failure | `Unsupported image format` (§6 #6) | `SDLIMAGE_VENDORED` (already fixed) |
| Vertical flip (even/odd pass parity) | visible flip, no log | Y-mirror MVP on intermediate passes (§6 #7, `7ea8e6e93`) — already fixed |
| Crash (AV / segfault) | `CRASH #N:` in log | `preset_runner.cpp` (Vulkan has no D3DCompile to blame anymore) |

The first three are expected to dominate the failure set on Vulkan —
the slang spec is large and `slang_translate.cpp` covers the common
subset. The audit CSV's `risk` column predicts which presets will exercise
the edge cases.

### Risk heuristic (in `audit_slang_presets.py`)

A preset is `low` risk if it has none of:
- ≥6 passes
- OriginalHistory# ≥ 4
- PassFeedback (double-buffer)
- ≥4 named LUTs
- absolute-scale pass
- viewport-scale pass
- float framebuffer
- sRGB framebuffer
- mipmap_input
- #include depth ≥ 4
- ≥8 #pragma parameters
- #reference inheritance

`medium` = 1–2 of those; `high` = 3+. The `risk_reasons` column lists
which triggers fired.

### Known-existing Vulkan coverage (from §6 + worklog)

- ✅ Stock ×2, hqx chains (hq2x/hq3x/hq4x), fsr — field-verified (round 10).
- ✅ scalefx-aa — fixed (round 4: null-texture guard).
- ✅ Y-mirror MVP on intermediate passes (round 10, `7ea8e6e93`).
- ✅ Failure contract: any preset load/run failure → session fallback
  (classic renderer), game never dies (round 1).
- ⚠️ Known upstream Vulkan bug: ~167 s idle crash on some drivers
  (documented in #8737, non-blocking due to fallback). Should be
  reproducible on the validation sweep — note which driver/adapter
  combinations trigger it.

### What "working" means (acceptance criteria)

A preset is **working** if, on Vulkan, with a real game frame flowing:
1. The chain compiles without `gpu-scaler.log` errors.
2. The first frame renders with no visible defect (no flip, no black,
   no corruption).
3. The output image survives for 60 seconds of gameplay without crash
   or fallback.
4. Frame count feedback (if used) doesn't tear after the `frame_count_mod`
   boundary.

A preset is **partial** if it renders but has a visible defect (slight
flip, color shift, missed pass). Track these separately — they may share
a root cause with full failures.

### Reporting

After each sweep, write a CSV with columns `preset, status, category,
notes` where `status` ∈ {working, partial, fallback, crash} and
`category` is one of the failure categories above (or `ok` for working).
This file lives in `/home/z/my-project/download/vulkan-coverage-<date>.csv`.
Diff against the previous sweep's CSV to track progress between fixes.

### Why Vulkan-only is the right call

- Vulkan was always the field-verified backend.
- The SDL3 GPU API's Vulkan backend is the most mature (it's the
  reference backend for SDL_gpu).
- Most libretro slang presets target GLSL/Vulkan first; the HLSL/D3D12
  path was the transpile target and had more edge cases — now removed.
- D3D12 was fully excised (§8) — the shipped binary only ever uses
  Vulkan (or Metal on macOS). If a user has no Vulkan driver, the
  scaler fails cleanly and falls back to the regular renderer per the
  §4 failure contract. `DEVILUTIONX_GPU_BACKEND=vulkan` continues to work
  as an explicit override (belt-and-suspenders, since D3D12 is no longer
  selectable).



## 13. Static audit fixes — history ring & bare scale (2026-10-14, applied to master)

Full report: `docs/audit/shader-support-audit-2026-10-14.md` (kept in-repo; the audit
CSVs are in-repo too — index below).

Two silent-wrongness bugs confirmed against the v2 audit CSV (2658 presets) and a fresh
libretro/slang-shaders clone, **both now applied to fork master in commit `dce1c4222`
("slang-shaders-audit") and verified in-tree** (history ring allocates `historyDepth_ + 1`
at `preset_runner.cpp:616`; "scale_type always wins over the bare-scale default" at
`preset_parser.cpp:229`):

| ID | File | Bug | Wild impact | Fix |
|---|---|---|---|---|
| F5 | `preset_runner.cpp/.hpp` | OriginalHistory ring held `depth` slots (needs `depth+1`) and indexed forward (`+N` instead of `-N`): OH1 == current frame at depth 1, shifted taps and never-written-slot sampling at depth >= 3 | 264 presets referencing `OriginalHistory#` (max_hist: 1×13, 3×168, 5×2, 6×8, 7×73) | commit `0feed0710` / patch `0001` |
| P1 | `preset_parser.cpp` | bare `scale` assignment reset a previously declared `scale_type` to Source (line-order-dependent parse; RetroArch is order-independent) | 897 presets (639 Mega_Bezel, 93 presets/, 36 edge-smoothing, 31 downsample, 28 interpolation); 0 in Phase-1, 68 in Phase-2 | commit `807c8518d` / patch `0002` |

Historical apply commands (already done on master; patches kept in `download/` for
cherry-pick reference — `--keep-cr` mandatory, §9):

```bash
git am --keep-cr 0001-preset_runner-fix-OriginalHistory-ring-depth-and-ind.patch
git am --keep-cr 0002-preset_parser-bare-scale-must-not-reset-a-declared-s.patch
```

or upload the three full files (`preset_parser.cpp`, `preset_runner.cpp`,
`preset_runner.hpp`) — they are byte-identical to the fix commits. The F5 logic is
proven by `tools/audit/test_history_ring.cpp` (pre-fix logic FAILS depth 1/3/7 exactly
as predicted; fixed logic passes the full N×T matrix, g++ -O2, exit 0).

### In-repo audit artifacts (added 2026-10-14)

- `docs/audit/shader-support-audit-2026-10-14.md` — full static audit report
- `docs/audit/slang_audit_v2.csv` — v2 preset audit, 2658 presets (previously session-only)
- `docs/audit/vulkan-audit-codefindings-affected.csv` — per-preset F5/F1/F6 exposure cross-ref
- `docs/audit/slang-codefindings-scan.csv` — per-preset P1/F9/T2–T5/F8 corpus scan
- `tools/audit_slang_presets.py` — v2 preset auditor (pre-existing; CSV to stdout)
- `tools/audit/audit_crossref.py` — joins audit CSV × code findings
- `tools/audit/scan_codefindings.py` — byte-level corpus scanner
- `tools/audit/patch_scale_order.py` — byte-exact TAB-preserving patcher (§9 gotcha workaround)
- `tools/audit/test_history_ring.cpp` — F5 ring logic proof / regression canary

### State at master `dce1c4222`

- Phase-1 readiness: 175/185 low-risk presets clean of all known gaps; the 10
  F5-affected Phase-1 presets (`deinterlacing/` bob/motion-adaptive/6×nnedi3,
  `motionblur/mix_frames`, `stereoscopic-3d/shutter-to-anaglyph`, `test/history`
  max_hist=3) were the ring-fix canaries — now fixed, confirm in the first sweep.
- Downgraded / non-issues (do not re-litigate): F9 case-mismatch (scanner
  false-positive — strip the `NAME`Feedback suffix before case compare), F1
  mipmap_input no-op (892 presets but 888 Mega_Bezel-tier — FIXED since, §8 queue
  item 4), F2
  feedback_pass dead key (benign, demand-driven feedback), T9/T2/T3/T4/T5 zero wild
  hits (translator hard-failure paths never exercised — strong coverage signal).
  F6 viewport=swapchain was logged here as an observation before it was promoted
  to the §8 queue and fixed (`f87f89d18`).
- Still pending (user side): CI build of master → Phase-1 field sweep per §12
  reporting format → fold results into the SLANG_SPEC.md gap list (§8 reconciliation
  rule).
- ✅ New finding from the spec write-up: the `PassFeedback#Size` `stoi("ck#")` crash —
  FIXED in `6f13d787f` together with the param-case fix and the translator `[N]` guard
  (§8 items 2–3).
