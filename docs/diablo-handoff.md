# DevilutionX GPU Shaders — Session Handoff

**Last updated:** 2026-10-13 evening (America/Toronto).

## 1. Mission (TL;DR)

Fork feature line on **VTSTech/DevilutionX**, targeted at **diasurgical/DevilutionX**:

- **PR #8737 (open, awaiting review):** GPU scaler on the SDL3 GPU API (Vulkan/D3D12/Metal)
  with build-time-baked shaders, plus a WAV-loading fix. Both to be re-scoped before review
  (see §2 course correction).
- **Current feature line (on fork master):** runtime **libretro slang presets**
  (`.slangp`/`.slang`) loaded from `.\shaders\` beside the exe — multi-pass chains,
  in-process glslang → SPIR-V, content-addressed disk cache + sidecar `.spv` fast path.
  Baked ScaleFX/HQx filters and their Settings entries are **removed**; Shader Filter =
  Off + folder-browser presets. Flip-parity fix field-verified; cross-build PNG codec
  fixed (hq2x LUTs load); d3dcompile fault forensics shipped; D3D12 under active
  diagnosis (§7).

## 2. State snapshot

| Ref | Tip | State |
|---|---|---|
| fork `master` (what CI builds) | `2c29042` | feature line + flip fix + d3dcompile forensics (incl. mingw include-order fix) + `docs/diablo-handoff.md` |
| PR **#8737** (`gpu-shaders-combined`) | `b11054ba2` | OPEN, awaiting review; 18 commits ahead of upstream master; still contains baked filters + WAV fix |
| upstream **SDL_mixer PR #906** | `30034b15` | OPEN; fixes zero-length RIFF chunk bug (Diablo's 1996 WAVs fail to decode); tarball SHA256 `6d175afd…` (needed for pin bump) |
| fork branch `wav-mixer-fix` | `658e948` | standalone DevilutionX-side WAV fix; optional stopgap, superseded by #906 + pin bump |
| `gpu-shaders-folder` / `gpu-shader-scaler` | — | historical; the feature line lives on fork master (folder branch deleted from remote) |

PRs #8734/#8736: closed, superseded by #8737. Related upstream issue **#4453** (libretro
shader support request); demo video: https://youtu.be/uQqkW-1UWec?t=12. Verify PR state
on GitHub HTML (`curl` the PR page — the API rate-limits).

**#8737 course correction** (text ready in `download/PR8737-course-correction.md`):
1. post the comment (fills in the #906 link); 2. strip the WAV files from the branch
(`3rdParty/SDL3_mixer` patch + CMake, `Source/engine/sound.cpp`,
`Source/utils/soundsample.cpp`, GPU_SHADERS.md WAV section); 3. retitle to
`SDL3: GPU shader scaler (libretro slang presets)`. After #906 merges: 2-line pin-bump
PR (package: `download/SDL3-mixer-pin-bump.md`; recompute the tarball SHA256 if upstream
squash-merges; `DEVILUTIONX_SYSTEM_SDL_MIXER=ON` builds only benefit once an upstream
release exists).

## 3. Sandbox map

- `/home/z/my-project/devilutionx` — main repo. `origin` = VTSTech fork (push), `upstream`
  = diasurgical (fetch only). Local `master` tracks fork master (tip may lag; fetch first).
- `/home/z/my-project/worklog.md` — append-only multi-agent log; per-round records with
  exact edit-tooling gotchas.
- `/home/z/my-project/scripts/` — reusable tooling: per-round byte-exact patch scripts
  (`apply_*.py`, `fix_preset_folder_confusion.py`), CI repro, MinGW toolchain setup,
  `gen_upstream_wav_patch.py`, `apply_d3dcompile_forensics.py`.
- `/home/z/my-project/reference/` — slang preset research (see §10).
- `/home/z/my-project/download/` — user-facing deliverables: round zips
  (`devilutionx-gpu-fixes-for-master-v9.zip` latest), WAV-fix/upstream packages
  (`0001-WAV-*.patch`, `decoder_wav.c`, `SDL_mixer-upstream-PR-draft.md`,
  `PR8737-course-correction.md`, `SDL3-mixer-pin-bump.md`),
  `0011-d3dcompile-fault-forensics-*.patch`,
  `d3dcompile-forensics-changed-files-2026-10-13.zip`.

## 4. Design decisions (confirmed with VTSTech — do not re-litigate)

| Item | Decision |
|---|---|
| Format | libretro slang presets (`.slangp` + `.slang`), libretro preset spec semantics |
| Dir layout | `shaders\` NEXT TO THE EXECUTABLE (recursive scan, subfolders OK), not the in-Source tree |
| Compile | In-process glslang (FetchContent `3rdParty/glslang`) → SPIR-V; cache in `shaders/cache/`; `<name>.vert.spv`/`.frag.spv` sidecars always win |
| Multi-pass | Per-pass scale rules, filter/wrap, sRGB & float FBOs, aliases, `Original`/`OriginalHistory#`/feedback, LUT PNGs, `#pragma` params at defaults, `frame_count_mod`, minimal history depth |
| **Pass orientation** | Every pass rendering into an INTERMEDIATE target gets a Y-mirror MVP (`diag(1,-1,1,1)`); last pass keeps the legacy convention so the present blit cancels — even/odd chain flips fixed (`7ea8e6e93`, field-verified); vertex stages without an MVP uniform keep their own
  (unknown) orientation — flagged `vertexMvp=no` in the load log, none seen in the field |
| D3D12 | SPIR-V → SPIRV-Cross → HLSL → guarded d3dcompiler (`gpu_d3dcompile.*`); forensics round 1 done, cause narrowed (§7) |
| Metal | Not done (baked MSL path still exists, unused by filters) |
| Built-ins | REMOVED (user request, `77c53bf35`): no ScaleFX/HQx pipelines/list entries/`gpu_luts.hpp`; stale ini values 1–6 sanitize to Off; only shared `Vertex`/`Blit` programs remain baked |
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
vulkan | direct3d12), `preset_smoke.cpp` (offline harness). Plus `gpu_d3dcompile.*`
(guarded d3dcompiler + crash forensics) and `tools/d3dcompiler_smoke.c` (standalone
device-less compiler test). `3rdParty/glslang|SPIRV-Cross` are FetchContent wrappers.
Full commit series: `git log b0a3b7bab..2c29042` on fork master (per-round detail in
worklog).

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

## 7. D3D12 — active diagnosis (resumed 2026-10-13, was parked)

Symptom: first `D3DCompile` faults on the RX 560 box; Vulkan unaffected. Forensics
build (patch 0011, on fork master via `cae0454` + include fix `2c29042`) + standalone
smoke tool produced the field data:

- **Machine-level causes ruled out:** the smoke tool (no game, no SDL, same system
  d3dcompiler_47.dll 6.2.26100.9549, exact game shaders) compiles all stages OK.
- **Field crash record:** `0xC0000005, execute of 0x7FF5D4750000` — faulting IP **not in
  any loaded module** (same address all 6 faults). The captured "stack:" is the HANDLER's
  stack (exe+0x28DA1A/0x28E3FB are LogCrashDetails/ScalerCrashHandler frames, then
  exception dispatch, then the wild IP as the machine frame) — the real pre-fault call
  chain is NOT captured yet.
- **Only non-system module:** `C:\ProgramData\obs-studio-hook\graphics-hook64.dll`
  (OBS capture hook; injects when a D3D device is created — the smoke tool creates none,
  which is exactly why it passes).
- **Containment works in the field:** all 6 faults contained, session fell back to the
  regular renderer, OSD showed "Custom (unavailable)". Ship-safe as-is.

Next steps: (1) 5-min experiment — fully exit OBS and rerun with
`DEVILUTIONX_GPU_BACKEND=direct3d12`; (2) forensics v2 (designed, NOT yet shipped):
`VirtualQuery` on the faulting IP (free/reserve/committed-exec? image/private?),
dump of the resolved `pD3DCompile` pointer + its module, faulting-CONTEXT stack walk via
`RtlVirtualUnwind` (yields the real call site, addr2line-able), byte dump at the target
(MZ header → HMODULE-as-pointer type confusion; E9/E8 → trampoline), known-injector
verdict line in the module scan. Failed-HLSL dumps for post-mortem land in
`shaders/cache/failed-p*.hlsl`.

## 8. Remaining tasks

- ⬜ D3D12 §7: OBS-exit experiment → forensics v2 → decode → fix or document
- ⬜ #8737 course correction (§2), then refresh the PR branch from fork master:
  `git push --force-with-lease origin origin/master:gpu-shaders-combined` (drop
  `.github/workflows/gpu-shaders.yml` + `cleanup-fork-workflows.*` noise first)
- ⬜ Watch #906 → after merge, pin-bump PR (`SDL3-mixer-pin-bump.md`)
- ⬜ 26-i: Linux gcc/clang build verification + clang-format 18 sweep over all new files
- ⬜ 26-j: GPU_SHADERS.md for runtime presets (NOTE: fork master's copy lacks the
  forensics troubleshooting section — the 3rd file of the forensics zip was never
  applied) + PR decision: update #8737 vs new branch + PR (user's call)
- ⬜ Cosmetic: highlighted row can coincide with an unrelated preset while browsing a
  different folder than the active one (selection itself is correct)
- ✅ hq2x/PNG codec, screenshots codec, fsr/hqx/stock flips, deadlock, selection, crash
  containment — done (§6)

## 9. Hard-won gotchas (do NOT relearn)

- Repo identity: PRs target **diasurgical/DevilutionX `master`** (not `devilution`, not
  `development`). Author identity: GitHub maps both `VTSTech
  <14359327+VTSTech@users.noreply.github.com>` and `Nigel Todman <veritas@vts-tech.org>`;
  keep one form per series.
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
  idle crash on some drivers; D3D12 first-present AV on some drivers.
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

Nothing irreplaceable outside the repo except `reference/` (re-downloadable) and
`worklog.md`; this file + the worklog reconstruct the full state.
