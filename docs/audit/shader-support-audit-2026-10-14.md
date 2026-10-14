# DevilutionX Vulkan Shader Support — Static Audit Report (2026-10-14)

**Repo:** VTSTech/DevilutionX fork master `2a30fca65` (post D3D12-excision, audit_slang_presets.py v2)
**Scope:** static audit of the runtime preset stack (`preset_parser` / `slang_translate` / `slang_compile` / `preset_runner`) cross-referenced against the v2 audit CSV (2658 presets) and a fresh clone of libretro/slang-shaders (2658 `.slangp`).
**Method:** full source read of all 5 preset-layer TUs (4,053 LOC), byte-level verification on the real corpus, standalone logic tests. No compile/GPU available in this sandbox — fixes are logic-proven and patch-packaged for the fork CI → field-test loop.

---

## 1. Executive summary

The stack is in good shape for the Phase-1 sweep: the translator's hard-failure paths (vertex-stage samplers, sampler arrays, multi-UBO, trailing-comment sampler lines) are **never exercised by the entire wild corpus** — zero hits across 2658 presets. **175 of the 185 low-risk presets are clean of all known runner gaps** and should pass validation as-is.

However, the audit found **two real bugs**, both silent (render plausibly, wrong result), both verified against the real corpus, both fixed and packaged:

| ID | Layer | Bug | Field impact | Fix |
|---|---|---|---|---|
| **F5** | `preset_runner.cpp` | History ring: wrong slot count **and** inverted index direction | **264 presets** with `OriginalHistory#` get wrong temporal data. Worst case (13 presets incl. `motion-adaptive-deinterlacing`, `nnedi3-*`, `mix_frames`): `OriginalHistory1` **returns the current frame** — motion-adaptive logic sees zero motion. With depth ≥ 3, every history tap is shifted toward newer frames and early frames read **never-written GPU memory** | **patch 0001** |
| **P1** | `preset_parser.cpp` | Bare `scale` resets a previously declared `scale_type` back to Source (line-order-dependent parse; RetroArch is order-independent) | **897 presets** mis-scale the affected pass (`aa-shader-4.0`, `smaa*`, `advanced-aa`, 639 Mega_Bezel variants, 36 edge-smoothing/xBR, 31 downsample, 28 interpolation). 0 in Phase-1; **68 in Phase-2** | **patch 0002** |

Both bugs produce "renders but wrong" results — exactly the failure class a casual field sweep records as *working*. Fixing them **before** the Phase-1 sweep makes the sweep results trustworthy.

Deliverables in this package:
- `0001-preset_runner-fix-OriginalHistory-ring-depth-and-ind.patch`
- `0002-preset_parser-bare-scale-must-not-reset-a-declared-s.patch`
- `vulkan-audit-codefindings-affected.csv` (per-preset F5/F1/F6 exposure from the v2 audit data)
- `slang-codefindings-scan.csv` (per-preset P1/F9/T2–T5/F8 scan of the fresh corpus)

Apply on fork master with (**`--keep-cr` is mandatory**, per handoff §9):
```bash
git am --keep-cr 0001-preset_runner-fix-OriginalHistory-ring-depth-and-ind.patch
git am --keep-cr 0002-preset_parser-bare-scale-must-not-reset-a-declared-s.patch
```

---

## 2. Confirmed findings (with evidence)

### F5 — History ring: wrong depth, inverted direction (HIGH, silent)

`preset_runner.cpp` (pre-fix):

```cpp
history_.assign(static_cast<size_t>(historyDepth_), nullptr);          // N slots
...
// Update the chain-input history ring: the current input overwrites the
// slot that held the oldest frame, so OriginalHistoryN resolves to
// ring[(slot + N) % depth].
...
const size_t slot = (static_cast<size_t>(historySlot_) + static_cast<size_t>(idx)) % history_.size();
```

Two independent defects:

1. **Slot count off by one.** The ring holds `depth` frames **including** the current one (the top-of-`Run()` blit writes `gameTex` into `history_[historySlot_]`). Frames available: `T .. T-(depth-1)`. `OriginalHistory_depth` (frame `T-depth`) is never in the ring.
2. **Direction inverted.** `historySlot_` advances **forward** one slot per frame (`(historySlot_ + 1) % size` at the end of `Run`), so frame `T-N` lives `N` slots **behind** the just-written slot: `(slot - N) mod size`. The shipped `(slot + N)` expression walks toward *newer* frames and wraps into the current one.

Per-configuration behavior (proven by `scripts/test_history_ring.cpp`, g++ -O2, 12-frame simulation):

| Ring config | `OriginalHistory1` resolves to | Verdict |
|---|---|---|
| depth=1 (13 wild presets) | `T-1 + 1 mod 1 = slot` → **current frame** | temporal data absent |
| depth=2 (0 wild presets — the one accidentally-correct config is never used) | `T-1` | OK by coincidence |
| depth=3 (168 presets) | `T-2` | shifted |
| depth=7 (73 presets) | `T-2` at T=2... and slots 1..6 **never written until frame 7** → undefined GPU memory sampled on early frames | shifted + garbage warmup |

Correct model: **`depth + 1` slots, `OriginalHistoryN` → `ring[(slot + size - N) mod size]`**, plus a first-Run seed blit of `gameTex` into every slot (re-armed on target resize) so early reads see valid content — matching RetroArch's history warmup.

Corpus exposure (v2 audit CSV cross-ref, `vulkan-audit-codefindings-affected.csv`):
- 348 presets reference `OriginalHistory#`; **264 have `max_hist ≥ 1`** (84 reference only `OriginalHistory0`, which is correct).
- `max_hist` distribution: `{1: 13, 3: 168, 5: 2, 6: 8, 7: 73}` — **no preset uses depth 2**, so the accidentally-correct case never occurs in the wild.
- Risk mix of the broken set: 10 low / 19 medium / 235 high. The 10 low-risk ones are in Phase-1 directories: `deinterlacing/` (bob, motion-adaptive, 6× nnedi3), `motionblur/mix_frames`, `stereoscopic-3d/shutter-to-anaglyph`, `test/history` (max_hist=3 — a built-in canary for this exact bug).
- Note: the field-verified presets (stock×2, hqx, fsr, scalefx-aa) use no `OriginalHistory#`, which is why this never surfaced. Why it looks "fine" when broken: a deinterlacer with `OH1 == current` renders a plausible static image; a 3-tap accumulation shader with shifted taps still renders *something* smooth. Silent-wrong, not crash — the worst kind for a validation sweep.

### P1 — Bare `scale` clobbers declared `scale_type` (MEDIUM-HIGH for Phase 2, silent)

`preset_parser.cpp` `ApplyPassKey` (pre-fix):

```cpp
} else if (key == "scale") {
        pass.scaleX = { ScaleType::Source, ParseFloat(value, 1.0F) };   // type RESET
        pass.scaleY = pass.scaleX;
}
```

Keys are applied in file order, so

```
scale_type0 = viewport
scale0 = 1.0
```

ends with **type = Source** (input-sized pass) where RetroArch — which parses all keys first, order-independently — yields **viewport** (window-sized pass). Verified on the real file `anti-aliasing/aa-shader-4.0.slangp` (emulation):

```
OLD (reset to Source): pass0 type=source   scale=1.0 -> output W =  640
NEW (preserve type):   pass0 type=viewport scale=1.0 -> output W = 1920
RetroArch reference:   pass0 type=viewport scale=1.0 -> output W = 1920
```

The fix fills in the value only and keeps whatever type the pass already has (`ScaleType::Source` remains the default when nothing was declared, and `scale_type` *after* `scale` still wins — both preserve existing behavior for all other orderings). `scale_x`/`scale_y` were already value-only and are unaffected.

Corpus exposure (fresh scan, `slang-codefindings-scan.csv`): **897 presets** declare a non-source `scale_type` before a bare `scale` on the same pass — `bezel/` 639 (Mega_Bezel LCD-GRID / PASSTHROUGH families), `presets/` 93, `edge-smoothing/` 36 (xBR), `downsample/` 31, `interpolation/` 28, `crt/` 26, `handheld/` 20, `border/` 7. Risk mix: 829 high / 68 medium / **0 low**. The `anti-aliasing/` hits (`aa-shader-4.0`, `smaa*`, `advanced-aa`) are near-Phase-1 and validate the fix in one look (pass-0 target size in the load log goes 640→1920 on a 1080p window).

---

## 3. Downgraded / informational findings (no patches shipped)

| ID | Layer | Observation | Wild impact | Action |
|---|---|---|---|---|
| F9 | runner | Translate validates LUT names case-insensitively; `Run()` binds LUTs/aliases case-**sensitively** (`lut->def.name == lutName`, `other->def.alias == base`) while `FillBlockBytes` is case-insensitive — a case-mismatched declaration would translate clean and then silently substitute the source texture for the LUT | **0 presets** in the entire tree mismatch case (verified) | Optional hardening: use `CaseInsensitiveEqual` at the two `Run()` bind sites; aligns the layers. 2 lines |
| F1 | runner | `mipmap_input` selects the mipmapped sampler (correct) but pass outputs are created `num_levels = 1` and `SDL_GenerateMipmapsForGPUTexture` is only ever called for LUTs → chain-input mips clamp to LOD 0; feature is a no-op for pass outputs | 892 presets, but 888 are high-risk (826 = `bezel/` Mega_Bezel) — irrelevant until Phase 3 | Defer; implement mip-chain outputs + per-frame `GenerateMipmaps` when Mega_Bezel validation starts |
| F6 | runner | `ScaleType::Viewport` resolves against the **swapchain** size with no aspect-ratio viewport derivation | 1355 presets; 0 in Phase-1, dominant in Phase-2 | Watch in field logs; likely acceptable for a fullscreen game, revisit if Phase-2 borders/overlays letterbox wrong |
| F2 | runner | `feedback_pass` is parsed/merged/stored but never read — double-buffering is derived from actual feedback reads (demand-driven), which covers all observable cases | 0 | None (document as intentional; remove or wire when spec-conformance testing starts) |
| F8 | runner | Pass with both `srgb_framebuffer` and `float_framebuffer` → float silently wins | **0 presets** set both | None |
| T9 | translate | `<Name>Size` uniform validation is case-sensitive while sampler names resolve case-insensitively; a case-mismatched `NameSize` member fails **loudly** at translate (not silently) | 0 | Optional: lowercase `base` before the `knownTextureNames` lookup for symmetry |
| T2/T3/T4/T5 | translate | Hard-failure paths — trailing-comment sampler lines, vertex-stage samplers, sampler arrays, >1 plain UBO per stage — **never occur** in the wild corpus (0 / 2658) | 0 | None; the translator's coverage of the corpus is effectively complete for these classes |

Also verified during the read (no action): std140 layout math (vec3/arrays/stride rules correct), push-constant → UBO re-homing, `PassOutput#` causality guard, `PassFeedback#` existence guard, `OriginalHistory` 0–8 range guard, sidecar-`.spv`-wins cache order, content-addressed SHA-256 cache keys, null-texture bind guard, sampler cache completeness, `frame_count_mod` per-pass semantics, `FrameDirection = +1` / `FrameTime = deltaUs` / `PASSINDEX` wiring, Y-mirror MVP convention with the last-pass cancel, double-buffer flip timing, feedback alias discovery (both `PassFeedback#` and `NAMEFeedback` forms), alias-sampling of earlier passes (hqx `hqx_refpass` pattern), git-lfs LUT-pointer diagnostic.

---

## 4. Updated validation plan (supersedes §12 phase lists)

1. **Apply patches 0001 + 0002 on fork master → CI build → field test** (`git am --keep-cr`, order matters for context: 0001 touches `preset_runner.*`, 0002 touches `preset_parser.cpp`).
2. **Phase 1 (185 low-risk):** expect **175/185 clean-pass**. The 10 F5-affected presets (`deinterlacing/*` ×8, `motionblur/mix_frames`, `stereoscopic-3d/shutter-to-anaglyph`) now become *meaningful* tests of the history fix — `test/history.slangp` (max_hist=3) is the direct canary: with patch 0001 its three history taps show distinct frames; without it, taps collapse toward the current frame.
3. **Quick field check for patch 0002:** load `anti-aliasing/aa-shader-4.0.slangp` on a 1080p window — the load log line `GPU scaler preset: pass targets ...` should show pass `0=1920x1080` (was `0=640x480` with source-resolved input).
4. **Phase 2 (539 medium):** re-run the audit-CSV diff after the sweep. P1's fix moves ~68 medium presets from "expected mis-scale" to "expected pass"; the remaining dominant combo is `uses_mvp + viewport_scale` (F6 observation applies).
5. **Phase 3 (Mega_Bezel tier):** before starting, implement the F1 mipmap fix (mip-chained pass outputs + `GenerateMipmaps`) and consider the F9 hardening — Mega_Bezel is where both would bite first (826 mipmap presets, dense alias/LUT usage).

**Regression risk notes for review:** patch 0001 changes ring allocation to `depth+1` textures (one extra texture per preset using history — negligible VRAM at game resolutions) and adds one seed-blit pass per preset load/resize (not per frame — the per-frame cost is unchanged: one blit + `depth+1`-slot ring). Patch 0002 changes only the bare-`scale` branch; presets relying on the old reset would have been mis-scaled already per RetroArch semantics.

---

## 5. Verification artifacts

| Artifact | Location | Result |
|---|---|---|
| History-ring logic test (old vs new, depth 1–8, full `T-N` matrix) | `scripts/test_history_ring.cpp` → g++ 14.2 -O2 -Wall -Wextra | OLD fails at depth 1/3/7 as predicted; **NEW passes all** (exit 0) |
| P1 emulation on `aa-shader-4.0.slangp` | inline (this session) | NEW matches RetroArch (1920 vs 640) |
| Corpus scans (P1/F9/T2–T5/F8, 2658 presets) | `scripts/scan_codefindings.py` → `download/slang-codefindings-scan.csv` | counts in §2/§3 |
| Audit-CSV cross-ref (F5/F1/F6 exposure, phase overlap) | `scripts/audit_crossref.py` → `download/vulkan-audit-codefindings-affected.csv` | counts in §2/§3 |
| Patches | `download/0001-*.patch`, `download/0002-*.patch` | minimal diffs: 32+6 insertions, 5+2 deletions across 3 files |

Sandbox notes: fresh session — the prior session's `scripts/`, `reference/`, `analysis/devilutionx.exe` were re-created where needed (corpus re-cloned; v3 binary **not** re-uploaded — `scan_pe_for_wild_calls.py` re-run on it remains pending and needs the binary uploaded again). The Edit-tool tab-mangling gotcha (handoff §9) re-verified the hard way on `preset_parser.cpp` — reverted and re-applied byte-exactly via `scripts/patch_scale_order.py` (LF+TAB preserved, single-match assert, idempotent).
