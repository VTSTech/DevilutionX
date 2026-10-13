# DevilutionX GPU Shaders — Session Handoff (updated)

**Last updated:** 2026-10-13 (America/Toronto) — flip fix field-verified; PR #8737 branch
refreshed by user; updated PR first-message note delivered. Round-8 field grid (12
`zz-diag-*` bisect presets) proved the flip was pass-parity: every FBO intermediate pass
composited one vertical mirror (even pass counts flipped), hqx's `hqx_refpass` alias read
shifted parity by one. Fix `7ea8e6e93` = Y-mirror MVP (`diag(1,-1,1,1)`) on every
intermediate pass, identity on the last pass so the present blit cancels; log gains
`flipY=yes/no`. User applied it to fork master (`bce6a1eac`) and confirmed all flips
fixed; then pushed the whole feature line to `gpu-shaders-combined` as two squashed
commits (`c9e6c9eaf` loader + `b11054ba2` fix series) on top of the old combined history
→ PR head `b11054ba2`, 18 commits ahead of upstream master, 57 files +18059/−106, flip
fix included, trees differ from fork master only in `.github/workflows` (PR keeps
upstream's workflows — correct). Updated PR body text:
`download/PR8737-first-message-updated.md`. Pre-review noise to drop from the PR branch:
`.github/workflows/gpu-shaders.yml`, `cleanup-fork-workflows.ps1/.sh`.
**Active work:** branch `gpu-shaders-folder` @ `44307699f`, tree clean. Chain:
`44307699f → d389b2357 → 51c2ebc20 → 3ac37adee → 8cd5292a6 → 77c53bf35 → 0ce1f67ac → 64c07a0f6 → fab294504 → 4c0609164`
(fork master tip before this series). Established workflow: fixes are packaged (git
bundle + format-patch + byte-exact overlay), user pushes to **fork master**, **fork CI
builds**, user field-tests on Windows, next round branches from the result. PR comes when
the feature is stable.
**PR status:** #8734 + #8736 CLOSED (superseded); combined PR **#8737 OPEN**, awaiting review.
**User plan:** when the feature is finished, either update the #8737 branch or open a new
branch + PR and close the old one (user's words, 2026-10-12).

---

## 1. Mission (TL;DR)

Fork feature line on **VTSTech/DevilutionX**, upstreamed to **diasurgical/DevilutionX**:

1. **DONE (PR #8737, open):** GPU scaler via SDL3 GPU API (Vulkan/D3D12/Metal) with
   build-time-baked shaders; WAV-loading fix folded in. NOTE: on `gpu-shaders-folder` the
   baked ScaleFX/HQx filters have since been **REMOVED** (see below) — the PR branch
   (`gpu-shaders-combined`) still has them. Reconcile at PR time.
2. **CURRENT (branch `gpu-shaders-folder`):** load **libretro slang presets**
   (`.slangp`/`.slang`) at RUNTIME from `.\shaders\` beside the exe — multi-pass chains,
   in-process glslang → SPIR-V, disk cache + sidecar `.spv` fast path.
   **Design change 2026-10-13 (user request):** baked-in ScaleFX/HQx shader pipelines and
   their Settings entries are GONE (`77c53bf35`). Settings → Graphics → Shader Filter now
   offers **Off + scanned custom presets** (folder browser). Only the shared `Vertex` and
   `Blit` programs remain baked. Baked MSL path still exists for Metal, unused by filters.

## 2. PR state (verify via GitHub before acting; API was rate-limited — use HTML)

- **PR #8737** `VTSTech:gpu-shaders-combined` → `diasurgical:master` — OPEN, awaiting review
  (contains the GPU scaler + WAV fix; #8734/#8736 were closed in its favor by user decision).
- Related upstream issue **#4453** (libretro shader support request) referenced in the body.
- Demo video: https://youtu.be/uQqkW-1UWec?t=12
- Saved HTML snapshots (older, 2026-10-12 ~18:30 ET): `/home/z/my-project/pr8737.html`, `pr8736.html`.
- Check fresh state: `curl -s https://api.github.com/repos/diasurgical/DevilutionX/pulls/8737`
  (if rate-limited, fetch the PR HTML page).

## 3. Sandbox map

- `/home/z/my-project/devilutionx` — main repo. Remotes: `origin` = VTSTech fork (push),
  `upstream` = diasurgical (fetch only). Branch `gpu-shaders-folder` = the feature line
  (tip `51c2ebc20`, clean tree); local `master` tracks the fork and lags behind.
  Fork master history = upstream `b0a3b7bab` + feature commits; everything the user has
  pushed so far is on fork **master** (that's what CI builds).
- `/home/z/my-project/worklog.md` — multi-agent log, append-only. Contains the full
  per-round records (packaging, deadlock, builtin removal, folder confusion) with the
  exact edit-tooling gotchas.
- `/home/z/my-project/scripts/` — reusable tooling: per-round byte-exact patch scripts
  (`apply_deadlock_fix.py`, `apply_builtin_removal.py`, `apply_runner_fixes.py`,
  `fix_preset_folder_confusion.py`), plus PR-era tooling (`ci_linux_repro.sh`,
  clang-format fixups, MinGW setup, generated-header keepers).
- `/home/z/my-project/reference/` — slang preset research material (see §6).
- `/home/z/my-project/download/` — user-facing deliverables, incl. this file and the
  round zips (`devilutionx-gpu-fixes-for-master-v6.zip` is the latest).

## 4. Design decisions (all confirmed with VTSTech — do not re-litigate)

| Item | Decision |
|---|---|
| Format | libretro slang presets (`.slangp` + `.slang`), libretro preset spec semantics |
| Dir layout | `shaders\` NEXT TO THE EXECUTABLE (recursive scan, subfolders OK). NOT the in-Source tree |
| Compile | In-process glslang (C++ API, FetchContent via `3rdParty/glslang/CMakeLists.txt`) → SPIR-V; content-addressed disk cache in `shaders/cache/`; precompiled `<name>.vert.spv`/`.frag.spv` sidecars next to the `.slang` always win |
| Multi-pass | Full chain: per-pass scale rules (source/viewport/absolute), filter/wrap, sRGB & float FBOs, aliases, `Original`/`OriginalHistory#`/feedback pass, LUT textures (PNG), `#pragma` parameters at defaults, `frame_count_mod`, `histories` (minimal history depth v1) |
| D3D12 | SPIR-V → SPIRV-Cross → HLSL → existing guarded d3dcompiler path (extracted into `gpu_d3dcompile.*`). PARKED: d3dcompiler faults on the user's box even for baked shaders; Vulkan is the working backend |
| Metal | NOT done (baked MSL path still works) |
| **Built-ins** | **REMOVED (2026-10-13, user request, `77c53bf35`):** no ScaleFX/HQx pipelines, no list entries, `gpu_luts.hpp` gone. Stale ini values 1–6 sanitize to Off. The former 26-h "ship builtins as .slangp" task is SUPERSEDED — users pick presets from `shaders\` instead |
| UI | Shader Filter list = Off + folder-browser entries (`..`, `subdir/`, presets). Enum value `100+N` is an index into the CURRENT folder listing; the durable selection is the stored relative path (`Graphics.shaderFilterPreset.value`, hidden ini option). Path is committed BEFORE the value change (see gotchas) |
| Hot reload | None in v1 (restart to apply). Switching presets live DOES rebuild the chain since `8cd5292a6` |
| Failure contract | Any preset load/run failure → existing session fallback (classic renderer), game never dies; custom-preset failure turns Shader Filter off for the session instead of killing the scaler |

## 5. Commit series on `gpu-shaders-folder` (patches 0001–0005 in the round zips)

| # | Commit | Title / content |
|---|---|---|
| — | `4c0609164` | fork master tip (feature line pushed by user; includes the round-1 CI fixes formerly known as sandbox commit `3e00ba404`) |
| 0001 | `fab294504` | Settings UI: shader preset folder browser (nav entries, persisted-path sync, in-place list rebuild) |
| 0002 | `64c07a0f6` | Preset path resolution (preset dir → ancestors ≤6 → root), slangp-only browser, folders-without-presets hidden, gpu-scaler.log 200 KB cap + `.old` rotation |
| 0003 | `0ce1f67ac` | Startup deadlock fix: `OptionEntryShaderFilter` ctor ran during `GetOptions()` magic-static init; rescan deferred to `LoadFromIni` override |
| 0004 | `77c53bf35` | Remove builtin HQ2x/ScaleFX filters + list entries; fix scalefx-aa crash (plain alias binding + null-texture guard). NOTE: the LUT diagnostics originally claimed for this commit did NOT land — they shipped in `3ac37adee` instead |
| 0005 | `8cd5292a6` | Fix wrong preset loading on selection (folder confusion): path committed BEFORE value change; full-tree-scan fallback removed; runner reloads when the selected path changes; `RescanCustomEntries` notifies only on real change |
| 0006 | `3ac37adee` | Make LUT load failures actionable (`DescribeImageLoadFailure`: path + size + git-lfs pointer hint); fix screenshot error format string dropping `result.error()`. **Broken intermediate: FAILED CI** (see 0007) |
| 0007 | `51c2ebc20` | CI fix: `DescribeImageLoadFailure` used `devilution::StrAppend` before its declaration (file-top anonymous namespace, outside `namespace devilution`, before the str_cat include) — helper rewritten with plain `std::string +=` / `std::to_string`, behavior identical |
| 0008 | `d389b2357` | Guarantee SDL3_image PNG support: vendor SDL_image's own libpng+zlib (`SDLIMAGE_VENDORED=ON`, `SDLIMAGE_PNG_SHARED=OFF`; FetchContent switched to git clone + `GIT_SUBMODULES external/libpng;external/zlib` — the release tarball lacks external/ sources); fixes LUT loading AND screenshots. Plus shader fingerprint diagnostics (per-pass src/merged size+FNV-1a hash, texelFetch/gather counts, pass output sizes) for the fsr flip investigation. **Broke the Windows configure step — see 0009** |
| 0009 | `44307699f` | CI configure fix (CMP0002): for SDL3 builds, defer devilutionx's zlib past `3rdParty/SDL3_image` and REUSE SDL_image's vendored zlib when present (ZLIB::ZLIB alias + FindZLIB variables + no-op FindZLIB stub, exported exactly like 3rdParty/zlib; mpqfs binds via its existing `NOT TARGET ZLIB::ZLIB` check); fallback fetch kept for system-SDL3_image / vendored-PNG-off. Early zlib block now `elseif(NOT USE_SDL3)` — SDL1/SDL2/dos/uwp/amiga unchanged. `DEVILUTIONX_SYSTEM_ZLIB` is inert for SDL3 builds now (workflow flag harmless, leave it) |

Architecture (unchanged since task 26, all committed):
`Source/engine/render/gpu/preset/` — `preset_types.hpp` (data model), `preset_parser.*`
(full .slangp parser: chains, scale rules, FBO flags, aliases, textures/LUTs, params,
`#reference` merge with rebased paths + ancestor dir fallback, `ScanPresets`,
`ResolvePresetFilePath`), `slang_translate.*` (.slang → SDL3-GPU-convention GLSL,
std140 reflection, dense bindings), `slang_compile.*` (glslang → SPIR-V, cache, sidecar),
`preset_runner.*` (multi-pass executor: pipelines, history ring, LUTs, sampler cache,
backend = vulkan | direct3d12), `preset_smoke.cpp` (standalone offline harness).
Plus `gpu_d3dcompile.*` (guarded d3dcompiler with crash guard + failed-HLSL dumps) and
`3rdParty/glslang|SPIRV-Cross` FetchContent wrappers.

## 5a. Field-test history (rounds 1–5)

| # | Trigger | Symptom | Root cause | Fix | Status |
|---|---|---|---|---|---|
| 1 | First CI build | Vulkan `vkCreateGraphicsPipelines Unhandled VkResult!`; session death on any preset failure; flat 2658-entry list; mixed `\`/`/` | UBO decorations kept set 0 after a push-constant block (offset math bug); `LogFailHard` on preset fail; no browser | round-1 fixes (folded into `4c0609164`) | ✅ |
| 2 | 22:14 ET build (koko-aio bezel pack) | non-slangp junk in list; unbounded log; FXAA-bloom couldn't find `shaders-ng\` files | list filter; no cap; `#reference`/shader paths resolved only against the preset's own dir | `fab294504` + `64c07a0f6` | ✅ |
| 3 | Next build | **No window, no output, no crash, nothing** | magic-static re-entry: option ctor read `GetOptions()` while `static Options` was still constructing → self-deadlock | `0ce1f67ac` | ✅ (build Oct 12 23:39:01 boots, Vulkan presets load, folder nav works) |
| 4 | Oct 12 23:39:01 build | (a) FXAA picture upside-down; (b) scalefx-aa access violation 0xC0000005; (c) hq2x `Unsupported image format` (LUT); (d) D3D12: every D3DCompile faults | (a) later traced to the wrong-preset bug (row 6); (b) `Run()` only bound `<alias>Feedback`, plain aliases fell through to the LUT branch → null texture deref; (c) LUT file content, not code (row 7); (d) PARKED (backend-level; Vulkan unaffected) | `77c53bf35` | ✅ b fixed (field-confirmed via later presets); a/c/d reclassified in rows 6–7 |
| 5 | Same build + user request | "Remove the baked-in shaders and remove them from the list"; folder confusion: selecting scanlines (far down) loads/reports anamorphic/advanced-aa (early entries) | removal = design change; confusion = path assigned AFTER the synchronous restart chain (`SetActiveListIndex` → `ResizeWindow` → `Reinit`), folder-blind full-tree fallback resolved `100+0/1` against the whole tree, and the loaded runner never swapped on subsequent selections | removal in `77c53bf35`; `8cd5292a6` | ✅ CONFIRMED FIXED (build 01:02:31: `preset changed:` lines, correct names, live switch, persistence) |
| 6 | Build 01:02:31 | FXAA + advanced-aa picture flip | Both flips were side effects of the OLD fallback loading the WRONG presets; correct paths render right-side-up | none needed (`8cd5292a6` fixed it as a side effect) | ✅ RESOLVED, open item closed |
| 7 | Build 01:02:31 | hq2x LUT `IMG_Load failed: Unsupported image format` with NO path context (the round-3 diagnostics never actually shipped — worklog r2-series overstated `77c53bf35`); screenshot failure logged an EMPTY error | Initially (WRONGLY) blamed on git-lfs pointer PNGs; `3ac37adee`: `DescribeImageLoadFailure` (path + size + LFS-pointer hint) in `Runner::LoadLut`; screenshot format string fixed | `3ac37adee` + user-side `git lfs pull` advice | ◐ superseded by row 8 — the LFS theory turned out FALSE |
| 8 | Build 01:56:52 (`51c2ebc20` deployed) | hq2x "still doesn't load" — BUT diagnostic now prints `hq2x.png (2793 bytes)`, no POINTER tag → the file is the GENUINE PNG; real root cause is upstream of it. fsr (and texelFetch-based presets generally) render vertically flipped; normalized-sampling presets (fxaa, advanced-aa) are correct | mingw cross build's SDL3_image had NO PNG decoder: devilutionx disables `SDLIMAGE_BACKEND_STB` and `find_package(PNG)` finds no mingw libpng → `LOAD_PNG` silently dropped (also explains screenshots: `IMG_SavePNG` uses the same codec). fsr flip: pipeline model contradicts field under every global (NDC/upload/blit) combination — needs ground truth | `d389b2357`: vendor SDL_image's own libpng+zlib (git clone + `GIT_SUBMODULES`, static png_static); plus fingerprint diagnostics (per-pass src/merged size+hash, texelFetch/gather counts, pass output sizes) | ◐ PNG fix awaiting CI; fsr flip awaiting the user's fingerprinted log |
| 9 | User applied 0008 → CI: Windows job fails at CONFIGURE (`add_library cannot create target "zlib"/"zlibstatic"`, CMP0002, in SDL3_image's `external/zlib`); Linux green | Windows job forces `-DDEVILUTIONX_SYSTEM_ZLIB=OFF` → devilutionx zlib FetchContent defines zlib/zlibstatic first; 0008's `SDLIMAGE_VENDORED=ON` makes SDL_image 3.2.4 vendor its OWN zlib unconditionally → duplicate targets. Linux: system zlib → devilutionx zlib skipped → no collision | `44307699f` (Dependencies.cmake): defer devilutionx zlib for SDL3, reuse SDL_image's vendored zlib (alias + vars + stub); fallback fetch otherwise; non-SDL3 unchanged. Validated by full local configure: negative control = exact abort, fixed = "Configuring done" + "enabled: png" | ✅ user git-am'd the series onto fork master (`0878c537f`, tree == `44307699f`): "compiles clean, ci passes"; hq2x LOADS (PNG codec fixed) |
| 10 | User field grid on `devilutionx-flip-bisect-pack-v1.zip` (12 `zz-diag-*` presets): A1 good, **A2 upside-down**, B1/B2 good, C1 good, **C2 upside-down**, **D1/D2 upside-down**, E1/E2 good | Flip is NOT shader content: pure stock×2 flips (A2). Law: chains of an EVEN pass count flip; odd counts correct; hqx chains (D1/D2, shipped hqx) flip at ANY depth because `hq2x.slang` reads its COLORS from the aliased `hqx_refpass` (pass-0 output) instead of the chain input — one extra FBO read = one extra mirror. All static single-convention models were disproven by A1-vs-A2 alone | `7ea8e6e93` (branch `flip-fix` on `0878c537f`): `FillBlockBytes` pushes a Y-mirror MVP (`diag(1,-1,1,1)`) to every pass that renders into an INTERMEDIATE target; LAST pass keeps the old convention so the shared present blit (fullscreen.vert, same GL-style UV anchor) still cancels it — 1-pass presets byte-identical. Pass log gains `flipY=yes/no` | ◐ awaiting user apply + CI + field round 8 |
| 11 | (pending) | (pending) | | |

## 6. Reference material (`/home/z/my-project/reference/`)

- Full libretro/slang-shaders tree (user-uploaded 7z, extracted):
  `reference/slang-shaders/slang-shaders-master/slang-shaders-master/` — **2658 `.slangp`**
  + **1653 `.slang`**. Archive kept at `/home/z/my-project/upload/slang-shaders-master.7z` (45 MB LZMA2).
- Key locations: HQx `edge-smoothing/hqx/hq{2,3,4}x.slangp` (+LUT PNGs in `resources/`);
  ScaleFX multi-pass under `edge-smoothing/scalefx/` + `presets/scalefx-plus-smoothing/`.
- Single-file study samples at `reference/` root: `crt-easymode.slangp/.slang`,
  `crt-royale.slangp`, `scalefx.slangp` + `scalefx-pass0.slang`, `sharp-bilinear-simple.slangp`.
- License note: slang-shaders is GPLv3 (verify per-shader headers before bundling anything
  into the repo; DevilutionX is sovereign/fair-use friendly).

## 7. Restore / reproduce procedure

The old 61 MB WIP zip is OBSOLETE — everything is committed. Fresh sandbox or new machine:

```bash
git clone https://github.com/VTSTech/DevilutionX devilutionx   # or existing sandbox repo
cd devilutionx && git checkout gpu-shaders-folder
# exact round-zip delivery (v4):
git fetch devilutionx-gpu-fixes.bundle gpu-shaders-folder:gpu-fixes   # -> 44307699f
# or patch route onto fork master:
git am --keep-cr patches/000*.patch        # --keep-cr MANDATORY (CRLF repo)
# reference tree (for offline preset sweeps):
7z x /home/z/my-project/upload/slang-shaders-master.7z -o/reference/
cmake -B build -G Ninja -DCMAKE_BUILD_TYPE=Release   # 3rdParty deps fetch at configure
```

Nothing irreplaceable outside the repo except `reference/` (re-downloadable) and
`worklog.md` (this file + the round zips reconstruct the state).

## 8. Remaining task list

- ✅ 26-a..26-e branch/design, recon, wrappers, loader, multi-pass Runner
- ◐ 26-f D3D12 transpile path — PARKED (d3dcompiler faults even on baked shaders on the
  user's box; failed-HLSL dumps land in `shaders/cache/failed-p*.hlsl` if revisited)
- ✅ 26-g Settings UI + ini persistence (folder browser, `8cd5292a6` selection correctness)
- ⬛ 26-h SUPERSEDED by builtin removal (`77c53bf35`)
- ⬜ 26-i Linux gcc/clang build verification + clang-format 18 over ALL new/modified files
- ⬜ 26-j docs (`GPU_SHADERS.md`: folder format, cache, sidecar spv) + PR decision:
  update #8737 branch vs new branch + PR (user's call)
- ✅ FXAA flip — RESOLVED (row 6: side effect of the wrong-preset bug; no shader bug)
- ◐ hq2x — root cause is the CROSS BUILD's missing SDL3_image PNG codec (NOT the
  user's files — their hq2x.png is the genuine 2793-byte PNG; LFS theory disproven).
  Fix shipped in `d389b2357` (vendored libpng+zlib) — but CI needs `44307699f` (0009)
  FIRST to fix the Windows configure regression, then hq2x should load
- ◐ fsr flip (texelFetch-based presets) — awaiting the user's fingerprinted log
  (per-pass `src=`/`merged=` hashes + `fragFetch=` counts + pass target sizes,
  shipped in `d389b2357`); compare hashes against upstream slang-shaders
  (fsr-pass0.slang rewritten 2026-04-12, f33c9825) before touching the pipeline
- ◐ Screenshot failure — root cause = same missing SDL3_image PNG codec as hq2x
  (`IMG_SavePNG`); fixed by `d389b2357`, verify after the 0009-fixed build
- ◐ Windows CI configure — broken by 0008 (CMP0002 zlib/zlibstatic duplicate
  targets), fixed by `44307699f` (v8 zip): apply ONLY 0009 on fork master
- ◐ Cosmetic (known, low priority): while browsing a DIFFERENT folder than the active
  preset's, the highlighted row can coincide with an unrelated preset (value is a
  folder-local index); selection itself is correct since `8cd5292a6`

## 9. Hard-won gotchas (carry-over — do NOT relearn)

- Repo identity: PRs target **diasurgical/DevilutionX `master`** (not `devilution`, not
  `development`). Upstream `development` is stale.
- clang-format **18** enforced by CI over `Source`. Verify:
  `clang-format --style=file --dry-run -Werror <files>`.
- Commit identity: `VTSTech <14359327+VTSTech@users.noreply.github.com>` is the series
  form; `8cd5292a6` accidentally used `14359327@users.noreply.github.com` (GitHub maps
  both to VTSTech — harmless, but keep the `+VTSTech` form for future commits; `3ac37adee`
  and `51c2ebc20` are back on the correct form).
- **`git am --keep-cr` is MANDATORY** on this repo (CRLF); plain am corrupts diffs.
  `git format-patch` does NOT accept `--keep-cr` (am-side flag only).
- Helpers at the TOP of a devilutionx TU (before `namespace devilution`) cannot use
  `StrCat`/`StrAppend`/`Log*` — keep them pure std (the str_cat include sits later,
  and global-scope lookup can't see `devilution::` regardless).
- devilutionx cross builds have NO system libpng: any SDL_image config relying on
  `find_package(PNG)` (with the STB backend disabled) silently produces NO PNG codec
  — IMG_Load/IMG_SavePNG then fail with "Unsupported image format"/save errors.
  Vendored libpng is mandatory for the mingw CI (see `3rdParty/SDL3_image`).
- **Vendored-dep target collisions**: enabling SDLIMAGE_VENDORED makes SDL_image
  define `zlib`/`zlibstatic` — if the superproject ALSO FetchContents zlib (forced
  by `-DDEVILUTIONX_SYSTEM_ZLIB=OFF` in the workflow's static gate), configure dies
  with CMP0002. `44307699f` resolves it by deferring devilutionx's zlib for SDL3
  builds and reusing SDL_image's. If a new vendored dep is added, grep `3rdParty/`
  for duplicate target names FIRST.
- Headless-configure escape hatch for SDL3 in the sandbox: `-DSDL_UNIX_CONSOLE_BUILD=ON`
  (SDL3 FATALs without X11/Wayland otherwise); plus `-DCMAKE_POLICY_VERSION_MINIMUM=3.5`
  under CMake 4 and `-DSDL_X11=OFF -DSDL_WAYLAND=OFF ...` to trim missing dev libs.
- FetchContent stores variables under the LOWERCASED name (`sdl_image_SOURCE_DIR`,
  confirmed by `3rdParty/SDL_image/CMakeLists.txt` usage) even when declared
  mixed-case (`SDL_image`).
- **Unquoted heredocs execute backticks**: generating READMEs via `cat << EOF`
  turned `` `zlib`/`zlibstatic` `` into command substitution ("targets /.").
  Quote the delimiter (`<< 'EOF'`) or avoid backticks in generated docs.
- SDL_image GitHub release tarballs do NOT contain the external/ submodule sources —
  vendored builds need `GIT_REPOSITORY` + `GIT_SUBMODULES` instead of the tarball.
- When a field log contradicts the code model, ship fingerprint diagnostics (file
  size + FNV-1a hash + API-usage counts) before shipping a behavioral "fix".
- **Never read `GetOptions()` from an `Options` member's constructor** — the lazy
  function-local magic static re-enters itself and deadlocks (no window/output/crash).
  Defer with a `LoadFromIni` override (runs after `Options` is fully constructed).
- **Option value changes notify listeners SYNCHRONOUSLY**: shader filter value →
  `ResizeWindow` → `ReinitializeRenderer` → `gpu_scaler::Reinit` → preset reload, all
  within `SetActiveListIndex`. Commit ALL companion state (the preset path!) BEFORE
  changing the value.
- The preset enum value `100+N` is a UI index into the CURRENT folder listing and goes
  stale across folder changes; `Graphics.shaderFilterPreset.value` (relative path) is the
  only durable selection. The scaler resolves ONLY from the path — never re-add a
  value-index fallback.
- The OSD preset name comes from `gPresetDisplayName` = the LOADED chain's
  `preset.displayName` — if the OSD lies, the wrong preset actually loaded.
- Byte-exact editing: the `Edit` tool mangles tab indentation in this repo. Use the
  Python patch scripts in `scripts/` (per-file EOL: `options.cpp` CRLF+
  tab; `gpu_scaler.cpp`/`preset_parser.cpp` LF+tab), idempotent, assert-1-match.
- GUI-subsystem exe: nothing on stdout ever; `--verbose` only raises SDL priorities.
  Diagnostics live in `gpu-scaler.log` (200 KB cap, one `.old` generation).
- The frame handed to the scaler is already RGBA32 (palette resolved upstream) → preset
  `Source` input is plain color; no palette pass needed.
- GPU scaler failure contract: sticky session flag → transparent revert to classic
  renderer; custom-preset failures turn the filter off for the session (no sticky fail).
- Known upstream bugs (documented in #8737, non-blocking due to fallback): Vulkan ~167s
  idle crash on some drivers; D3D12 first-present AV on some drivers.
- tmpfiles.org: 60-minute expiry, free tier shows an interstitial page — the `/dl/...`
  direct link (with the numeric token) bypasses it. Round zips now go through the
  chat file transfer instead.

## 10. Next steps (in order)

1. User: apply v8's single patch `0010-fix-preset-chains-rendering-vertically-flipped.patch`
   (`git am --keep-cr` on fork master `0878c537f`, or fetch the v8 bundle + FF push → `7ea8e6e93`,
   or overlay `preset_runner.cpp/.hpp`) → CI → field round 8 per the zip's README checklist:
   fsr / hqx / 2xbrz / 6xbrz / xbrz-freescale-multipass must flip to correct; halphon / fxaa /
   stock must stay correct; A2 + D2 bisect canaries should both read good. The gpu-scaler.log
   pass lines should show `flipY=yes` for every pass except the last (`flipY=no`).
2. PR #8737 (still draft): after the flip fix is on fork master, refresh the PR branch exactly
   as before: `git push --force-with-lease origin origin/master:gpu-shaders-combined`.
   Pre-ready cleanup flagged in r10: .github/workflows noise (28 files), GPU_SHADERS.md fork
   doc, optional split of the WAV/soundsample fix into its own PR.
3. D3D12: stay parked unless the user wants to chase it; Vulkan is the field backend.
   Known caveat from the flip fix: vertex stages WITHOUT an MVP uniform keep their own
   (unknown) orientation — the load log flags them `vertexMvp=no`; none seen in the field.
4. Polish: stale highlight row while browsing other folders (see §8).
5. 26-i/26-j: Linux build check, docs, then the PR decision (update #8737 vs new PR).
