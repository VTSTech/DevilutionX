# Vulkan Field Sweep — Shader Test List (2026-10-15)

Phase-1 field sweep per `docs/diablo-handoff.md` §12. Master tip: `92831323e` + doc cleanup `22388a08d`.

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
