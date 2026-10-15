# Vulkan Field Sweep — Shader Test List (2026-10-15)

Phase-1 field sweep per `docs/diablo-handoff.md` §12. Master tip: `f49a29ce5` (history rebased 2026-10-15; the `92831323e`/`22388a08d` hashes below are pre-rebase).

> **Field round 3 (2026-10-15):** first live log is in — Tier 1a partial results: bob/motion-adaptive deinterlacing, mix_frames, shutter-to-anaglyph, `test/history` + `test/feedback` all working; `test/feedback-noncausal` correctly rejected (negative control); **nnedi3-nns16/nns64 failed and are fixed on `664d0f740`** (macro-aliased sampler declarations — re-test all three nnedi3 canaries on the next build); **`test/format` failed and is fixed** (per-pass format downgrade + `#pragma format` wiring — the in-repo copy lives at `tools/audit/field-presets/test/`). koko-ambi/koko-aio presets: sampler handling fixed, but koko-aio-ng has a separate repeat-include issue (handoff §8, next round).
>
> **Field round 4 (2026-10-15):** Phase-1 breadth segment 1 — **15/15 unique presets working** across `denoisers/` (1), `reshade/` (8), `sharpen/` (6); 0 fallbacks, 0 crashes, clean shutdown (v9-lineage build, 2 sessions, 16 loads). First live confirmations: **F1 mipmaps** (magicbloom, 11 mip-chained pass outputs) and **PassFeedback** (magicbloom adapt ring). CSV updated: Tier 1a backfilled from round 3 (nnedi3-nns16/nns64 = fallback→fixed, re-test pending), Tier 1b Anime4k + Tier 2a FilmGrain/bsnes-gamma-ramp = working. Full per-preset results: `vulkan-coverage-2026-10-15.csv` (round deliverables). Next: nnedi3 ×3 + koko-ambi/koko-aio re-tests, then the remaining Tier 2a/2b rows.
>
> **Field round 5 (2026-10-15):** Phase-1 breadth segment 3 — **33/33 unique presets working** across `dithering/` (16 — dir complete), `deinterlacing/` (11 — dir complete), `misc/` (6/18); 0 fallbacks/errors, clean shutdown. **Round-3 nnedi3 macro-alias fix confirmed live**: all five nnedi3 variants (nns16/32/64/128/256) load their 5-pass chains, disk-cache hits on 4/5 passes. Firsts: float-framebuffer field-confirmed (`gtu-v050-deinterlaced`, audit risk high), deep OriginalHistory5 ring (`misc/anti-flicker`). Phase-1 total: **69 unique working, 71 loads, 0 failures across 4 logs**. Next: koko ×3 re-tests, then misc 12 / handheld 17 / presets 14 / crt 9 / reshade rest 42.
>
> **Field round 6 (2026-10-15):** Phase-1 breadth segment 4 — **32/32 unique presets working**, `misc/` **complete: 38/38 across rounds 5+6**; 0 fallbacks/errors, clean shutdown. **First self-identifying log**: the digest's `builds:` line carries the exe version automatically (round-6 session header — no hand labeling needed). Firsts: F1 mip chain on `misc/geom` (pass 0 — 5th distinct F1 chain), `misc/print-resolution` TextGeneratePassFeedback, `misc/glass` OriginalHistory6 deep ring, `misc/patchy-color` 3 passes + 6 LUTs (longest misc chain). Phase-1 total: **101 unique working, 103 loads, 0 failures across 5 logs**. Next: koko ×3 re-tests, then handheld 17 / presets 14 / crt 9 / reshade rest 42 / blurs slots 110/113.
>
> **Phase-1 breadth segment 5 (2026-10-15):** `presets/` swept — **23/23 unique presets working** (24 loads; `black-and-white-tv` re-loaded 2x); 0 fallbacks/errors, clean shutdown, single session; no PNG LUT activity. Build self-identified via the round-6 session header: `Oct 15 2026 19:23:33, exe 1.6.0-dev-Release-a67a4c0f7` — **caveat: codebase-audit round-5 lineage, predates the field-round 7-9 fixes (not on origin/master); this segment has no ds-hybrid/oversize presets, so dest-skipped 0 does NOT regression-prove the round-9 fit-floor fix**. Firsts: 6 new F1 mip chains — `crt-royale-smooth` F1[17] (19 passes, longest this segment), `crt-royale-downsample` F1[12], `crt-royale-xm29plus` + `crt-royale-kurozumi` F1[11], `imgborder-royale-kurozumi` F1[10], `crt-guest-philips-27ps55-syboxez` F1[2] (11 distinct chains total); PassFeedback family grows by Trails0Feedback (`apple-monitor-II`, `black-and-white-tv`, `crt-lottes-multipass-interlaced-glow`) + AfterglowPassFeedback (`crt-guest-philips-27ps55-syboxez`). Preset paths recorded digest-style (bare names) in the coverage CSV this segment. Phase-1 total: **124 unique working, 127 loads, 0 failures across 6 logs**. Next: unchanged queue minus presets — ds-hybrid ×2 re-test (cut that build from a tree carrying the round-7-9 fixes; `a67a4c0f7` predates them), koko ×3, then handheld rest / crt 9 / reshade rest 42 / blurs slots 110/113.
>
> **Phase-1 breadth segment 6 (2026-10-15):** next-30 list partially run — **20 presets: 19 working / 1 fallback** (16 new uniques; `kawase_blur_5pass` + `gizmo-composite-blur` re-confirmed since segment 2; `ds-hybrid` ×2 status flip). Same self-identified build as segment 5 (`a67a4c0f7`, pre-round-7-9-fixes). **ds-hybrid-sabr + ds-hybrid-scalefx both load and render on the OLD build** — the round-9 protocol re-test (round-9 build + `DEVILUTIONX_GPU_DEBUG=1` + Integer Scaling ON) is NOT satisfied by this; the earlier crash/black-screen is environment-sensitive and the protocol re-test stays queued. Firsts: `koko-ambi` F1[0] (12th distinct F1 chain) + PassFeedback pair (`ambi_temporal_passFeedback`, `avglum_passFeedback`); first handheld/ dot-matrix LCD chains (3, all v2-high-risk, green); first `presets/fsr/` data (`fsr-aa-lv2-median3x3`, Tier 2a canary); `blurs/` closed 17/17 (`gaussian_blur_2_pass` + `kawase_blur_9pass` — the browser-skipped slots 110/113). **First JPEG-texture failure**: koko-aio-ng fallback — `IMG_Load` "Unsupported image format" on `bezel/koko-aio/textures/backdrop.jpg` (SDL_image lacks JPG; PNG LUTs unaffected; both attempts identical, log 141/213) — koko-aio family blocked until JPG support or PNG swap. Phase-1 total: **141 unique working (142 exercised), 147 loads, 1 fallback / 0 crashes across 7 logs**. Next: ds-hybrid protocol re-test on a round-9 build; koko-aio-ng after JPEG support or PNG swap; handheld rest 9 (`NSO-gba-color`, `nds-color`, `palm-color`, `psp-color`, `sp101-color`, `gb-palette` ×3, `gbc-dev`), presets/fsr rest 3 (`fsr-aa-lv2`, `fsr-crt-potato-bvm`, `fsr-crt-potato-bvm-vhs`), crt low 19 (opener 5 not yet run), reshade low 47 (recount vs queued 42).
>
> **Phase-1 breadth segment 7 (2026-10-15):** crt/ opener batch — **13/13 unique presets working** (14 loads; `crt-yah-single-pass` re-loaded 2x); 0 fallbacks / 0 errors / 0 warns, 2 sessions, clean shutdown; same self-identified build `a67a4c0f7` (pre-round-7-9-fixes). Opener 5 (`phosphor-persistence`, `crt-potato-BVM`, `crt-yah-single-pass`, `crt-yah`, `fake-crt-geom-potato`) + 8 early sweeps from the wider crt/ census (`crtsim`, `crtglow_lanczos`, `crtglow_gauss`, `crt-yo6` ×3, `crt-royale`, `zfast-crt-hdmask`) — the batch spans all three v2 risk tiers (5 low / 4 medium / 4 high). Firsts: **`crt-yah` 13 passes** — longest chain this segment — with `PhosphorPassFeedback` PassFeedback; `phosphor-persistence` `phosphorFeedback` PassFeedback; F1 chains #13/#14/#15 — `crtglow_lanczos` + `crtglow_gauss` F1[3] (v2 `mipmap_input` prediction confirmed) and **`crt-royale` F1[10]** with the full 12-pass chain disk-cache-hit. dest-skipped stayed 0 across the 12/13-pass chains (same old-build caveat — not round-9 fix proof). Phase-1 total: **154 unique working (155 exercised), 161 loads, 1 fallback / 0 crashes across 8 logs**. Next: crt low 14 remain of 19 (`base-lite/medium/strong`, `newpixie-mini`, `pure-` ×5, `xbr-lv2`/`lv3`/`lv3-horizontal`, `zfast-crt-geo` ×2 — v2 crt/ census: 132 presets, 19 low / 70 medium / 43 high); ds-hybrid protocol re-test (round-9 build) unchanged; koko-aio-ng JPEG gate unchanged; handheld rest 9; presets/fsr rest 3; reshade low 47 (recounted: 47 of 55 untested, all v2-low — queued-42 figure stale).

## How to test

1. slang-shaders tree in `shaders\` beside the exe (recursive scan; subfolders OK). Full tree incl. `bezel/` for the Tier 1c entries.
2. Walk Settings → Graphics → Shader Filter → browse folders.
3. For each preset: check the OSD name matches what you picked (if the OSD lies, the wrong preset loaded), wait ~2 s for compile + first frame.
4. **Success** (image visible, no defect): mark `working` in the CSV — no log needed.
5. **Failure** (fallback to classic renderer / `Custom (unavailable)` / black / flipped / distorted / crash): mark status + category, and send me `gpu-scaler.log` (200 KB cap, one `.old` generation) + the preset path.
6. `status` ∈ {working, partial, fallback, crash}; `category` examples: slang-translate, glslang-compile, missing-feature, vulkan-validation, lut-load, flip, crash.
7. Known issue (do NOT report): Integer Scaling + non-multiple resolution crash (core window math, out of scope).

Start with **Tier 1** (regression canaries — proves the applied fixes in the field), then **Tier 2** (breadth). Tier 2a is the top of the 185-preset low-risk Phase-1 queue.

## Tier 1a — F5 history-ring canaries (8)

| preset | passes | risk | what it exercises |
|---|---|---|---|
| `deinterlacing/bob-deinterlacing.slangp` | 1 | low | history ring: 1-pass deinterlacer [mvp, orig-history] |
| `deinterlacing/motion-adaptive-deinterlacing.slangp` | 1 | low | history ring: motion-adaptive [mvp, orig-history] |
| `deinterlacing/nnedi3-nns16-deinterlacing.slangp` | 5 | low | history ring: 5-pass nnedi3 chain, light [mvp, orig-history, includes] |
| `deinterlacing/nnedi3-nns64-deinterlacing.slangp` | 5 | low | history ring: 5-pass nnedi3 chain, mid [mvp, orig-history, includes] |
| `deinterlacing/nnedi3-nns256-deinterlacing.slangp` | 5 | low | history ring: 5-pass nnedi3 chain, heavy [mvp, orig-history, includes] |
| `motionblur/mix_frames.slangp` | 1 | low | history ring: mix_frames tap [mvp, orig-history] |
| `stereoscopic-3d/shutter-to-anaglyph.slangp` | 1 | low | history ring: single-pass converter [mvp, orig-history] |
| `test/history.slangp` | 1 | low | history ring: max_hist=3 canary (was worst-case) [mvp, orig-history] |

## Tier 1b — field round 1 fix confirmations (8)

| preset | passes | risk | what it exercises |
|---|---|---|---|
| `scanlines/scanlines-Modern.slangp` | 1 | medium | round-1: trailing-comment pragma params (densY/densX/colAtten must apply) [mvp, viewport-scale] |
| `misc/simple_color_controls.slangp` | 1 | medium | round-1: comma declarators (sc_sat,sc_black,...) [mvp] |
| `sharpen/Anime4k.slangp` | 4 | low | round-1: comma declarators, 4-pass [mvp] |
| `scanlines/ossc.slangp` | 1 | medium | round-1: comma declarators [mvp, viewport-scale] |
| `ntsc/ntsc-adaptive.slangp` | 4 | medium | round-1: unknown UBO member auto_res zero-fill (was hard-fail) [mvp, float-fb] |
| `motion-interpolation/motion_interpolation.slangp` | 2 | medium | round-1: mixed-case alias Pass0 deduction [mvp, viewport-scale, orig-history] |
| `edge-smoothing/xbr/other presets/super-2xbr-3d-2p.slangp` | 4 | medium | round-1: XBR_ANTI_RINGING zero-fill + mvp/viewport [mvp, viewport-scale] |
| `crt/crt-guest-advanced-ntsc.slangp` | 19 | high | round-1: guest chain (alias/zero-fill family) [mvp, viewport-scale, float-fb, mipmap, lut, orig-history] |

## Tier 1c — field round 2 fix confirmations (4)

| preset | passes | risk | what it exercises |
|---|---|---|---|
| `bezel/uborder/base_presets/koko-ambi/koko-ambi-crt-geom.slangp` | 6 | high | round-2: orphan-sampler drop; 6-pass clean chain (designated uborder rep) [mvp, float-fb, mipmap] |
| `presets/crt-plus-signal/crt-geom-deluxe-ntsc-adaptive.slangp` | 7 | high | round-2: undeclared texture modifier (delta_filter_linear) ignored; 7 passes [mvp, float-fb, mipmap, lut, pass-feedback, includes] |
| `bezel/Mega_Bezel/.../hsm-custom-fast-sharpen.slangp` | — | — | OPTIONAL (locate by name in Mega_Bezel tree): orphan sampler PrePass0 #define'd to Source |
| `bezel/Mega_Bezel/.../hsm-interlace-and-linearize.slangp` | — | — | OPTIONAL (locate by name in Mega_Bezel tree): commented-out UBO must not trip one-UBO guard |

## Tier 2a — low-risk breadth (13)

| preset | passes | risk | what it exercises |
|---|---|---|---|
| `reshade/FilmGrain.slangp` | 1 | low | low-risk Phase-1 coverage (reshade/) [mvp] |
| `reshade/bsnes-gamma-ramp.slangp` | 1 | low | low-risk Phase-1 coverage (reshade/) [mvp] |
| `reshade/handheld-color-LUTs/DSLite-2020.slangp` | 1 | low | low-risk Phase-1 coverage (reshade/) [mvp] |
| `misc/accessibility_mods.slangp` | 1 | low | low-risk Phase-1 coverage (misc/) [mvp] |
| `misc/chromaticity.slangp` | 1 | low | low-risk Phase-1 coverage (misc/) [mvp] |
| `handheld/color-mod/gba-color.slangp` | 2 | low | low-risk Phase-1 coverage (handheld/) [mvp] |
| `handheld/gb-palette-dmg.slangp` | 1 | low | low-risk Phase-1 coverage (handheld/) [mvp] |
| `presets/fsr/fsr-aa-lv2-median3x3.slangp` | 5 | low | low-risk Phase-1 coverage (presets/) [mvp, includes] |
| `dithering/bayer-matrix-dithering.slangp` | 1 | low | low-risk Phase-1 coverage (dithering/) [mvp] |
| `dithering/cbod_v1.slangp` | 2 | low | low-risk Phase-1 coverage (dithering/) [mvp] |
| `crt/crt-effects/phosphor-persistence.slangp` | 3 | low | low-risk Phase-1 coverage (crt/) [mvp] |
| `blurs/gauss_4tap.slangp` | 2 | low | low-risk Phase-1 coverage (blurs/) [mvp, includes] |
| `denoisers/bilateral-2p.slangp` | 2 | low | low-risk Phase-1 coverage (denoisers/) [mvp] |

## Tier 2b — medium-risk cluster reps (7)

| preset | passes | risk | what it exercises |
|---|---|---|---|
| `edge-smoothing/xbr/other presets/2xBR-lv1-multipass.slangp` | 4 | medium | xBR multipass: mvp+viewport_scale cluster (232 presets) [mvp, viewport-scale] |
| `handheld/3ds-lcd-grid-v2.slangp` | 3 | medium | handheld LCD emulation: mvp+viewport cluster [mvp, orig-history, includes] |
| `border/effect-border-iq.slangp` | 1 | medium | border overlay: mvp+viewport cluster [mvp, viewport-scale, lut] |
| `interpolation/bicubic.slangp` | 1 | medium | interpolation: mvp+viewport cluster [mvp, viewport-scale] |
| `downsample/drez/drez_3ds_x-400_y-480.slangp` | 1 | medium | downsample: float_fb+mvp cluster [mvp] |
| `pixel-art-scaling/cleanEdge-rotate.slangp` | 1 | low | pixel-art-scaling: mvp+viewport cluster [mvp, includes] |
| `crt/crt-1tap.slangp` | 1 | medium | CRT chain: mvp+includes cluster [mvp, viewport-scale, float-fb, includes] |

**Total: 40 presets** (Tier 1: regression proof, Tier 2: breadth/cluster coverage).
