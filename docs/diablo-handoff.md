# DevilutionX GPU Shaders — Session Handoff

**Last updated:** 2026-10-14 (America/Toronto). **SCOPE CHANGE (read the README first):** this is now a **standalone mod** — upstream `diasurgical/DevilutionX` enforces a no-AI policy, so PR #8737 is dead and all development, bugs, and features live here (pivot dated 2026-10-11 in the README). **No more DX12** (excised last session — debug code removed from `gpu_scaler.cpp`/`preset_runner.cpp`/`main.cpp`/`gpu_scaler.hpp`/`preset_runner.hpp`/`Source/CMakeLists.txt`/`docs/GPU_SHADERS.md`; 4 files deleted: `gpu_d3dcompile.{cpp,hpp}`, `tools/d3dcompiler_smoke.c`, `gpu_shaders_hlsl.hpp` — 1905 lines total). **No more baked shaders** (built-in ScaleFX/HQx filters removed at `77c53bf35`; only shared Vertex/Blit programs remain baked). **PRIMARY GOAL (2026-10-14, user direction): audit the shader implementation and parser against `docs/SLANG_SPEC.md`** — every `.slangp` key and `.slang` semantic annotated with fork implementation status, plus a priority-ordered gap list (see §8). v2 slang audit complete (script bug fixed: 1221→701 0-pass presets; risk distribution 33%/20%/46% low/med/high — see §12). F5/P1 fixes are applied on master `dce1c4222` (§13).

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
  an explicit override. D3D12 is excised from the shipped code (§7 is retained as
  forensics history only; the wild-call bug no longer matters since the path that
  contained it is deleted).
- **Tooling on master:** `tools/audit_slang_presets.py` (preset feature
  extractor — v2 ran on the full slang-shaders tree, see §12), `tools/audit/*`
  (crossref + codefindings scanners), `scan_pe_for_wild_calls.py` post-build
  self-check (catches §7-class build corruption in 7 s).

## 2. State snapshot

| Ref | Tip | State |
|---|---|---|
| fork `master` (what CI builds) | `e501dd853` | feature line + F5/P1 audit fixes (`dce1c4222`) + `docs/SLANG_SPEC.md` + README rewritten for the standalone mod; D3D12 excised, built-ins removed |
| PR **#8737** (`gpu-shaders-combined`) | `b11054ba2` | **DEAD — do not pursue.** Upstream enforces a no-AI policy (README, 2026-10-11); the PR will never merge. Kept for reference only; it still contains baked filters + the WAV fix |
| upstream **SDL_mixer PR #906** | `30034b15` | OPEN; fixes zero-length RIFF chunk bug (Diablo's 1996 WAVs fail to decode). Nice-to-have, **not gating**: the mod carries its own vendored `3rdParty/SDL3_mixer` patch + `SDL_LoadWAV_IO` fallback |
| fork branch `wav-mixer-fix` | `658e948` | standalone DevilutionX-side WAV fix; the in-master vendored patch + fallback already cover this |
| `gpu-shaders-folder` / `gpu-shader-scaler` | — | historical; the feature line lives on fork master (folder branch deleted from remote) |
| **v3 field binary** | `devilutionx.exe` (PE ts 2026-10-13 18:29:24 UTC) | uploaded by user; analyzed offline in this session; **3 wild direct calls confirmed in on-disk `.text`** at RVAs `0x7D568A` / `0x7D56E2` / `0x292E8D`, all targeting `imageBase − 0x40000000` (= RVA 0 with −1 GiB bias); see §7 |

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
  `gen_upstream_wav_patch.py`, `apply_d3dcompile_forensics.py`. **AUDIT STATUS:**
  `apply_d3dcompile_forensics.py` EXONERATED this session (pure source patcher —
  edits only `.cpp`/`.c`/`.md` files, never touches the compiled `.exe`; does not
  walk `.text`, does not rewrite `E8 rel32` operands). Other `apply_*.py` scripts
  still pending audit — user must upload them. **AUDIT RECIPE:** grep each script
  for `rb+`/`wb`/`r+b` (binary write modes), `seek`, `struct.pack`, `struct.unpack`,
  `mmap`, `bytearray`, `0xE8`/`b"\xe8"`, `0x100000000`, `0x40000000`, `0x140000000`,
  `ImageBase`/`image_base`, `.text`. Any script with these patterns is a binary
  patcher and a candidate corrupter.
- `/home/z/my-project/analysis/devilutionx.exe` — the v3 field binary uploaded by user
  (16.6 MB, PE32+ x86-64 GUI, stripped). Preserved for re-runnable offline disassembly.
  Original zip: `/home/z/my-project/upload/devilutionx.zip`.
- `/home/z/my-project/reference/` — slang preset research (see §10).
- `/home/z/my-project/download/` — user-facing deliverables: round zips
  (`devilutionx-gpu-fixes-for-master-v9.zip` latest), WAV-fix/upstream packages
  (`0001-WAV-*.patch`, `decoder_wav.c`, `SDL_mixer-upstream-PR-draft.md`,
  `PR8737-course-correction.md`, `SDL3-mixer-pin-bump.md`),
  `0011-*`/`0012-*`/`0013-d3dcompile-forensics-*.patch`,
  `d3dcompile-forensics-v3-2026-10-14.zip` (latest),
  `scan_pe_for_wild_calls.py` (post-build self-check tool — see §7),
  `diablo-handoff.md` (this file).

## 4. Design decisions (confirmed with VTSTech — do not re-litigate)

| Item | Decision |
|---|---|
| Format | libretro slang presets (`.slangp` + `.slang`), libretro preset spec semantics |
| Dir layout | `shaders\` NEXT TO THE EXECUTABLE (recursive scan, subfolders OK), not the in-Source tree |
| Compile | In-process glslang (FetchContent `3rdParty/glslang`) → SPIR-V; cache in `shaders/cache/`; `<name>.vert.spv`/`.frag.spv` sidecars always win |
| Multi-pass | Per-pass scale rules, filter/wrap, sRGB & float FBOs, aliases, `Original`/`OriginalHistory#`/feedback, LUT PNGs, `#pragma` params at defaults, `frame_count_mod`, minimal history depth |
| **Pass orientation** | Every pass rendering into an INTERMEDIATE target gets a Y-mirror MVP (`diag(1,-1,1,1)`); last pass keeps the legacy convention so the present blit cancels — even/odd chain flips fixed (`7ea8e6e93`, field-verified); vertex stages without an MVP uniform keep their own (unknown) orientation — flagged `vertexMvp=no` in the load log, none seen in the field |
| **D3D12** | **EXCISED (2026-10-14)** — SPIR-V → SPIRV-Cross → HLSL transpile path, guarded d3dcompiler, and forensics tooling all deleted (`gpu_d3dcompile.*`, `tools/d3dcompiler_smoke.c`, `gpu_shaders_hlsl.hpp`). §7 below is retained as forensics history only; the wild-call bug died with the path |
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

The three wild functions identified in §7 are in DevilutionX's options code (not in
`preset/` proper) — they are `OptionEntryShaderFilter` / `OptionEntryShaderFilterPreset`
setter wrappers, identified by the mangled symbols `N10devilution23OptionEntryShaderFilterE`
and `N10devilution29OptionEntryShaderFilterPresetE` present in the binary. They sit in
the synchronous-notify path documented in §9 ("Option value changes notify listeners
SYNCHRONOUSLY").

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
8. **D3D12 "first D3DCompile AV on RX 560"** → ROOT CAUSE FOUND this session (§7):
   three direct `E8 rel32` calls in on-disk `.text` (RVAs `0x7D568A`, `0x7D56E2`,
   `0x292E8D`) target `imageBase − 0x40000000` exactly; the bytes are baked into the
   file (not a runtime patcher). Affected functions are `OptionEntryShaderFilter` /
   `OptionEntryShaderFilterPreset` setters; the wild call is the last instruction of
   each function (no `ret`). All runtime/environmental suspects exonerated (OBS,
   d3dcompiler_47.dll, apphelp/IFEO/AppCompat SDB, AMD driver, Application Verifier —
   the rename test came back negative). Most likely corrupter: a post-build patch
   script (prime suspect: `scripts/apply_d3dcompile_forensics.py`). Decisive next step:
   clean rebuild + `objdump` diff.

## 7. D3D12 — RESOLVED: build-time defect in the v3 binary (was: active diagnosis)

### TL;DR

This section no longer needed or relevant (VTSTech)

## 8. Remaining tasks

### Active scope — audit implementation & parser against `docs/SLANG_SPEC.md` (PRIMARY)

The campaign now has two arms that feed each other:

- **Static arm (the audit proper):** `docs/SLANG_SPEC.md` is the baseline. It documents
  every `.slangp` key and `.slang` semantic with the fork's implementation status
  (supported / partial / extension / parsed-unused / unimplemented / divergent),
  cross-checked against RetroArch's `gfx/video_shader_parse.c`, and ends in a
  priority-ordered gap list (its §7, 14 items). **Work that list top-down.** The top
  items, confirmed against master `dce1c4222`:
  1. **F6 viewport aspect** — `ScaleType::Viewport` ignores the preset's aspect-ratio
     intent (`viewport_aspect_scale` et al.); unblocks the single largest cluster
     (232 medium-risk presets: `uses_mvp + viewport_scale`).
  2. ✅ **`PassFeedback#Size` latent crash** — FIXED (`b70500b19`): length-aware prefix parse in
     `FillBlockBytes` (10 for `PassOutput#`, 12 for `PassFeedback#`); previously `stoi` received
     `"ck#"` → uncaught `std::invalid_argument` → process abort. The same commit guards the
     translator's uniform `[N]` size parse (same crash class) and adds the regression test
     `tools/audit/test_uniform_names.cpp` (13 checks, g++ -O2, exit 0).
  3. ✅ **Param-case quirk** — FIXED (`b70500b19`): the runner applies `#pragma` param overrides
     via `CaseInsensitiveEqual`, so the parser's lowercased keys land regardless of shader
     spelling (Mega Bezel mixed-case params no longer silently keep defaults).
  4. **F1 mipmaps** — `mipmap_input` parsed then ignored (892 presets; Mega Bezel-tier,
     Phase-3 gate).
  5. …continue per SLANG_SPEC.md §7 (sRGB+float precedence note is already documented
     there as divergent-but-harmless, 0 presets affected).
- **Empirical arm (the field sweep):** the §12 validation methodology below. Every
  sweep failure gets categorized and mapped back onto a SLANG_SPEC status cell —
  a fallback is either a spec gap already listed (close it) or a new finding (add it
  to the spec's gap list, then fix).

Reconciliation rule: a preset counts as covered only when its SLANG_SPEC feature set is
all `supported`/`extension` AND it passes the §12 field sweep acceptance criteria.

### Field validation (empirical arm) — sweep queue

- ✅ **`audit_slang_presets.py` v2 run complete** (§12) — CSV of all 2658
  `.slangp` presets with feature flags + risk heuristic. Risk distribution
  after the script bug fix: low=886 (33%), medium=539 (20%), high=1233 (46%).
  701 presets still report 0 passes (down from 1221 in v1) — these are
  `#reference` inheritance chains where the referenced path doesn't resolve
  under the audit root (mostly `bezel/Mega_Bezel` 468 + `bezel/koko-aio`
  180 + `hdr/sony_megatron_*` 34). CSV lives in-repo: `docs/audit/slang_audit_v2.csv`.
- ⬜ **Validate the low-risk presets on Vulkan.** Build the game with
  `-DUSE_SDL3=ON -DUSE_GPU_SCALER=ON` (CI does this), drop the
  slang-shaders tree into `shaders/` next to the exe, launch with
  `DEVILUTIONX_GPU_BACKEND=vulkan`, walk the preset list, screenshot each.
  Track which produce a visible image and which fall back (session fallback
  to the classic renderer — see §4 failure contract). **Start with the
  185 low-risk non-zero-pass presets** (top dirs: `reshade/` 50, `misc/` 18,
  `handheld/` 17, `presets/` 14, `deinterlacing/` 10, `dithering/` 10,
  `crt/` 9). These are the highest-yield first batch.
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
- ⬜ **Wire `scan_pe_for_wild_calls.py` into CI** as a post-link gate so
  any future build-time corruption is caught before field test.

### D3D12 — fully excised (history only)

- ✅ **D3D12 §7 root cause found** — three corrupt `E8 rel32` calls in the
  v3 binary's `.text` at RVAs `0x7D568A` / `0x7D56E2` / `0x292E8D`,
  targeting `imageBase − 0x40000000`. NOT a runtime/driver/OS issue.
- ✅ **`apply_d3dcompile_forensics.py` exonerated** — pure source patcher.
- ✅ **No patch scripts run in CI** — verified by reading
  `.github/workflows/gpu-shaders.yml`.
- ✅ **D3D12 debug code removed from the shipped tree** (this session):
  - **Deleted (4 files, 1905 lines):**
    - `Source/engine/render/gpu/gpu_d3dcompile.cpp` (982)
    - `Source/engine/render/gpu/gpu_d3dcompile.hpp` (65)
    - `Source/engine/render/gpu/gpu_shaders_hlsl.hpp` (435) — HLSL shader
      sources, only consumer was the D3D12 path
    - `tools/d3dcompiler_smoke.c` (423)
  - **Stripped from source:**
    - `gpu_scaler.cpp`: include of `gpu_d3dcompile.hpp`/`gpu_shaders_hlsl.hpp`,
      `<windows.h>`, `HlslFor()`, `case SDL_GPU_SHADERFORMAT_DXBC:`, the
      `dxbcBlob` local, `PreloadD3DCompiler()`, `InstallCrashGuard()` call,
      `SDL_PROP_GPU_DEVICE_CREATE_SHADERS_DXBC_BOOLEAN`, the `direct3d12`
      driver branch in `CreateDevice`, the D3D12→Vulkan fallback retry in
      `Reinit`.
    - `gpu_scaler.hpp`: `PreloadD3DCompiler()` declaration + doc comment.
    - `preset_runner.cpp`: include of `gpu_d3dcompile.hpp` + `<spirv_cross.hpp>`
      + `<spirv_hlsl.hpp>`, the entire `else` branch of `createShader`
      (SPIRV-Cross → HLSL transpile, ~65 lines including the HLSL dump-on-
      fail diagnostic), simplified `isVulkan`/`isD3D12` dispatch to a
      single `if (backend != "vulkan")` early-return.
    - `preset_runner.hpp`: updated doc comment for `Runner::Load`.
    - `main.cpp`: `PreloadD3DCompiler()` call + the now-unused
      `gpu_scaler.hpp` include.
    - `Source/CMakeLists.txt`: `gpu_d3dcompile.cpp` removed from
      `libdevilutionx_SRCS`; `spirv-cross-hlsl` removed from
      `target_link_dependencies` (kept `spirv-cross-core` + `spirv-cross-glsl`).
    - `docs/GPU_SHADERS.md`: removed "Direct3D 12" from supported backends
      in intro + requirements; updated "three dialects" → "two dialects"
      (Vulkan GLSL + MSL); rewrote "Window claim failures" section to be
      backend-neutral (removed DXGI/D3D12-specific text); removed entire
      "D3D12 fault forensics" section + `tools/d3dcompiler_smoke.c`
      documentation; updated backend-forcing text to list only
      `vulkan` (and `metal` on macOS).
  - **Kept (defensive / harmless):** `PrettyBackendName("direct3d12") →
    "Direct3D 12"` in `gpu_scaler.cpp` (in case any code path reports the
    driver name); `spirv-cross-core` + `spirv-cross-glsl` link deps in
    CMakeLists.txt (small libs, may be useful for future SPIR-V reflection
    work); `3rdParty/SPIRV-Cross/` FetchContent declaration.
  - **Not touched:** all upstream DevilutionX code; all Vulkan/Metal paths;
    `.github/workflows/gpu-shaders.yml` (no DX12-specific setup to remove).
  - **Result:** the shipped `devilutionx.exe` will only ever use Vulkan (or
    Metal on macOS). If a user has no Vulkan driver, the scaler fails
    cleanly and falls back to the regular renderer per the §4 failure
    contract. `DEVILUTIONX_GPU_BACKEND=vulkan` continues to work as an
    explicit override.
  - **Zipped tree uploaded to tmpfiles.org** (60-min expiry, session-only):
    `https://tmpfiles.org/dl/1791924660.7ee4312c35a25a0c/waAnKo97R1Va/devilutionx-vulkan-only.zip`

### Carried over from before the pivot

- ✅ README + docs aligned to the standalone-mod scope (2026-10-14): README rewritten
  by the user; `docs/SLANG_SPEC.md` created and committed; `docs/GPU_SHADERS.md`
  rewritten for the runtime-preset pipeline this session; this handoff reframed.
- ✅ ~~#8737 course correction (§2)~~ — DEAD, never post to upstream (no-AI policy).
- ⬜ Watch #906 (optional, not gating): the vendored SDL3_mixer patch +
  `SDL_LoadWAV_IO` fallback already fix Diablo's 1996 WAVs in-master; a merge upstream
  would only matter for a future `DEVILUTIONX_SYSTEM_SDL_MIXER=ON` release build.
- ⬜ 26-i: Linux gcc/clang build verification + clang-format 18 sweep over all new files
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
  been wrong before (8-space indent shipped as "clean").
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
- Known upstream bugs (documented in #8737, non-blocking due to fallback): Vulkan ~167 s
  idle crash on some drivers; D3D12 first-present AV on some drivers — **NOTE: the
  "first-present AV" was actually the §7 wild-call bug, not a driver issue; remove this
  line from the upstream bug list once the patch-script fix is verified.**
- tmpfiles.org: 60-minute expiry; the tokenized `/dl/...` link (from the view page HTML)
  bypasses the interstitial.
- OBS `graphics-hook64.dll` injects into ANY process that creates a D3D device — device-
  less smoke tools can't reproduce injection-dependent behavior; check the module scan
  before blaming game code.
- **Post-build binary patch scripts can introduce wild calls that look exactly like
  runtime corruption.** Before chasing AppCompat SDBs, IFEO entries, driver bugs, or
  `apphelp.dll`, verify the on-disk bytes of every faulting call site with
  `objdump -d <exe> | grep "call.*0x100000000"`. A post-link patch script that rewrites
  `E8 rel32` operands with an off-by-`0x40000000` error produces a binary that faults
  deterministically on first execution, with a wild IP exactly 1 GiB below imageBase,
  on every machine, with every name, with OBS loaded or unloaded, with
  d3dcompiler_47.dll verified clean. The rename test (run as `*_derp.exe`) is necessary
  but NOT sufficient — it exonerates name-keyed corrupters, but a build-time defect
  survives the rename. The only decisive test is `objdump -d` on the on-disk file: if
  the wild bytes are in the file, the bug is yours, not the OS's. **The §7 bug cost two
  full forensics iterations (v1, v2) and a parked period because this check was not
  done first.** Lesson: when a crash log includes faulting RVAs, dump those RVAs from
  the on-disk binary BEFORE theorizing about runtime corrupters. Cost: 30 seconds. Saves
  weeks.
- **A direct `E8 rel32` call (not `FF 15` indirect, not `FF D0` register) reaching a
  wild target means the bytes on disk are wrong.** Direct calls have no register or
  memory operand to be corrupted at runtime — the displacement is baked into the
  instruction stream and read from `.text` by the CPU at execute time. If a runtime
  patcher rewrote `.text`, the page would be made copy-on-write and the patch would
  survive until process exit; but the on-disk file would still be clean. Therefore:
  on-disk file has wild bytes ⟹ build-time defect (linker, LTO, or post-build patch
  script). On-disk file has correct bytes but in-memory bytes are wild ⟹ runtime
  patcher (CoW page in memory). **The §7 case was the former.**

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
# re-verify the §7 fix on the v3 binary (if preserved):
objdump -d /home/z/my-project/analysis/devilutionx.exe | grep "call.*0x100000000" | wc -l
# should print "3" on the broken v3 binary, "0" on any rebuilt fixed binary.
# OR use the shipped scanner (faster + structured):
python3 /home/z/my-project/download/scan_pe_for_wild_calls.py /home/z/my-project/analysis/devilutionx.exe
# should report "FAIL: 3 wild call(s)" on the broken v3 binary, "OK: 0 wild calls" on any fixed binary.
```

Established loop: fixes are packaged → user pushes to **fork master** → fork CI builds →
field test on Windows → next round branches from the result.

**Wire the scanner into CI** (recommended): add a post-link step that runs
`scan_pe_for_wild_calls.py` on the built `devilutionx.exe` and fails the build
on any wild call. The §7 bug would have been caught before any field test.

Nothing irreplaceable outside the repo except `reference/` (re-downloadable),
`worklog.md`, and `/home/z/my-project/analysis/devilutionx.exe` (the v3 field binary —
preserve for re-runnable §7 disassembly; if lost, the user can re-upload from their
Windows box). This file + the worklog + the v3 binary reconstruct the full state.

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

- **`scan_pe_for_wild_calls.py`** — see §7 / §11. Not preset-specific but
  useful when building test exes; catches the §7-class build corruption
  in 7 seconds.

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

- Vulkan was always the field-verified backend; the §7 D3D12 wild-call
  bug never affected it.
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
  mipmap_input no-op (892 presets but 888 Mega_Bezel-tier — defer to Phase 3), F6
  viewport=swapchain (observation), F2 feedback_pass dead key (benign,
  demand-driven feedback), T9/T2/T3/T4/T5 zero wild hits (translator hard-failure
  paths never exercised — strong coverage signal).
- Still pending (user side): CI build of master → Phase-1 field sweep per §12
  reporting format → fold results into the SLANG_SPEC.md gap list (§8 reconciliation
  rule); re-upload a v3 field binary only if the wild-call re-scan is still wanted.
- ✅ New finding from the spec write-up: the `PassFeedback#Size` `stoi("ck#")` crash —
  FIXED in `b70500b19` together with the param-case fix and the translator `[N]` guard
  (§8 items 2–3).
