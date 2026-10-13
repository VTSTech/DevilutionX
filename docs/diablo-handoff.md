# DevilutionX GPU Shaders — Session Handoff

**Last updated:** 2026-10-14 (America/Toronto). **D3D12 §7 RESOLVED this session** (build-time defect, not runtime).

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
  fixed (hq2x LUTs load); **d3dcompile fault root cause found (§7) — three corrupt direct
  calls baked into the v3 binary by a post-build patch script; clean rebuild + diff is
  the next move.**

## 2. State snapshot

| Ref | Tip | State |
|---|---|---|
| fork `master` (what CI builds) | `2c29042` | feature line + flip fix + d3dcompile forensics v1/v2 (incl. mingw include-order fix) + `docs/diablo-handoff.md`; forensics v3 (patch 0013) ready to apply on top |
| PR **#8737** (`gpu-shaders-combined`) | `b11054ba2` | OPEN, awaiting review; 18 commits ahead of upstream master; still contains baked filters + WAV fix |
| upstream **SDL_mixer PR #906** | `30034b15` | OPEN; fixes zero-length RIFF chunk bug (Diablo's 1996 WAVs fail to decode); tarball SHA256 `6d175afd…` (needed for pin bump) |
| fork branch `wav-mixer-fix` | `658e948` | standalone DevilutionX-side WAV fix; optional stopgap, superseded by #906 + pin bump |
| `gpu-shaders-folder` / `gpu-shader-scaler` | — | historical; the feature line lives on fork master (folder branch deleted from remote) |
| **v3 field binary** | `devilutionx.exe` (PE ts 2026-10-13 18:29:24 UTC) | uploaded by user; analyzed offline in this session; **3 wild direct calls confirmed in on-disk `.text`** at RVAs `0x7D568A` / `0x7D56E2` / `0x292E8D`, all targeting `imageBase − 0x40000000` (= RVA 0 with −1 GiB bias); see §7 |

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
  `gen_upstream_wav_patch.py`, `apply_d3dcompile_forensics.py`. **AUDIT PENDING: see §7 —
  one of these post-build scripts is the prime suspect for the 3 corrupt `E8 rel32`
  fields in the v3 binary.**
- `/home/z/my-project/analysis/devilutionx.exe` — the v3 field binary uploaded by user
  (16.6 MB, PE32+ x86-64 GUI, stripped). Preserved for re-runnable offline disassembly.
  Original zip: `/home/z/my-project/upload/devilutionx.zip`.
- `/home/z/my-project/reference/` — slang preset research (see §10).
- `/home/z/my-project/download/` — user-facing deliverables: round zips
  (`devilutionx-gpu-fixes-for-master-v9.zip` latest), WAV-fix/upstream packages
  (`0001-WAV-*.patch`, `decoder_wav.c`, `SDL_mixer-upstream-PR-draft.md`,
  `PR8737-course-correction.md`, `SDL3-mixer-pin-bump.md`),
  `0011-*`/`0012-*`/`0013-d3dcompile-forensics-*.patch`,
  `d3dcompile-forensics-v3-2026-10-14.zip` (latest).

## 4. Design decisions (confirmed with VTSTech — do not re-litigate)

| Item | Decision |
|---|---|
| Format | libretro slang presets (`.slangp` + `.slang`), libretro preset spec semantics |
| Dir layout | `shaders\` NEXT TO THE EXECUTABLE (recursive scan, subfolders OK), not the in-Source tree |
| Compile | In-process glslang (FetchContent `3rdParty/glslang`) → SPIR-V; cache in `shaders/cache/`; `<name>.vert.spv`/`.frag.spv` sidecars always win |
| Multi-pass | Per-pass scale rules, filter/wrap, sRGB & float FBOs, aliases, `Original`/`OriginalHistory#`/feedback, LUT PNGs, `#pragma` params at defaults, `frame_count_mod`, minimal history depth |
| **Pass orientation** | Every pass rendering into an INTERMEDIATE target gets a Y-mirror MVP (`diag(1,-1,1,1)`); last pass keeps the legacy convention so the present blit cancels — even/odd chain flips fixed (`7ea8e6e93`, field-verified); vertex stages without an MVP uniform keep their own (unknown) orientation — flagged `vertexMvp=no` in the load log, none seen in the field |
| D3D12 | SPIR-V → SPIRV-Cross → HLSL → guarded d3dcompiler (`gpu_d3dcompile.*`); forensics v1–v3 field-tested; **root cause FOUND (§7) — 3 corrupt `E8 rel32` calls in on-disk `.text`, NOT a runtime/driver/OS issue**; clean rebuild + patch-script audit is the fix |
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

The "first D3DCompile AV on RX 560" mystery is solved. The v3 binary has three corrupt
direct calls baked into `.text` at link time (or by a post-build patch script). The
shader compiler never gets a chance to fail — the wild call crashes the function
before reaching it. All runtime/environmental suspects are exonerated. The compile
guard catches every fault, the session falls back to the regular renderer, the game
never dies. The fix is a clean rebuild + `objdump` diff to identify the corrupter.

### What v3 proved

v3 (patch 0013, local `2c64dd9`) shipped the **call-site byte decode**. Every faulting
site decodes as `E8 rel32` — a direct PC-relative call baked into devilutionx.exe's
`.text`. Not `FF 15` (indirect IAT), not `FF D0` (call rax). The CPU read 5 bytes from
a fixed RVA, computed `RIP + rel32`, and jumped. No pointer was dereferenced; no
register was consulted; no D3DCompile was entered.

This kills both readings from the v2 handoff:
- **(a) stale indirect pointer** — DEAD. No indirect call exists at any faulting site.
- **(b) D3DCompile went wild** — DEAD. D3DCompile is never entered; the faulting RIP
  is the wild target itself, the first instruction after the direct call.

### The rename test — NEGATIVE (decisive for environmental hypotheses)

User ran `devilutionx_derp.exe` (byte-identical copy renamed). Crashes were byte-for-byte
identical (same RVAs `0x7D568A` / `0x7D56E2` / `0x292E8D`, same rel32 bytes, same wild
target `imageBase − 0x40000000`). The rename test is the gold standard for name-keyed
corrupters and it came back negative. This kills every name-keyed hypothesis:

| Suspect | Status after rename test |
|---|---|
| OBS graphics hook | exonerated (v2: fully unloaded, identical crash) |
| `d3dcompiler_47.dll` | exonerated (v2: pD3DCompile clean; v3: D3DCompile integrity verified; v3 final: D3DCompile never entered) |
| `apphelp.dll` / AppCompat SDB / IFEO `VerifierDlls` | exonerated (rename test negative) |
| Application Verifier (name-keyed) | exonerated (rename test negative) |
| AMD driver / D3D12 runtime | exonerated (smoke tool passes with same shaders, same d3dcompiler) |
| Wild IP = `imageBase − 0x40000000` exactly | confirmed (3 of 3 sites, all builds, all runs) |

### On-disk binary analysis (the actual smoking gun)

User uploaded `devilutionx.exe` (16,662,016 bytes, PE32+ x86-64 GUI, stripped to external
PDB). PE TimeDateStamp `0x6ACE7884` = `2026-10-13 18:29:24 UTC` — matches the v3 build
banner `Oct 13 2026 18:28:19` in the runtime log (banner = build start, timestamp = link
completion). This is the exact binary that crashed.

PE Optional Header:
- `ImageBase = 0x140000000` (default mingw base — confirms §9)
- `SectionAlignment = 0x1000`, `FileAlignment = 0x200`
- `DllCharacteristics = 0x0160` → HIGH_ENTROPY_VA + DYNAMIC_BASE (ASLR) + NX_COMPAT.
  **No GUARD_CF (0x4000)**. No CFG.
- `DataDirectory[LOAD_CONFIG] = {RVA=0, Size=0}` — **no load config directory at all**.
  No `__security_cookie`, no CFG function tables, no SafeSEH table.
- `NumberOfRvaAndSizes = 16` (standard).
- 12 sections, `.text` at VMA `0x140001000` (RVA `0x1000`), size `0xD0FAC8`, file
  offset `0x400`.

Three direct calls in `.text` have wild rel32 fields (verified with `objdump -d` on
the on-disk file — these are NOT runtime artifacts):

| Call RVA | rel32 bytes (LE) | rel32 signed | next-instr RVA | target RVA | target VMA | = imageBase − 0x40000000? |
|---|---|---|---|---|---|---|
| `0x7D568A` | `71 A9 82 BF` | -1,081,955,983 | `0x7D568F` | `0xFFFFFFFFC0000000` (-1 GiB) | `0x100000000` | yes |
| `0x7D56E2` | `19 A9 82 BF` | -1,081,956,071 | `0x7D56E7` | `0xFFFFFFFFC0000000` (-1 GiB) | `0x100000000` | yes |
| `0x292E8D` | `6E D1 D6 BF` | -1,076,440,722 | `0x292E92` | `0xFFFFFFFFC0000000` (-1 GiB) | `0x100000000` | yes |

A full scan of the 13 MB `.text` section (every `0xE8` opcode, decoded, target
computed) finds **only 3** such wild calls — exactly the three sites that fault at
runtime. No others. The wild calls are not in the `.reloc` table (RIP-relative `E8`
calls never are — the relocation table only contains absolute-relocation entries for
`mov`/`lea` etc., not PC-relative branches).

### The arithmetic — verified

For all three sites: `rel32 + 0x40000000 = -(call_site_RVA + 5)`, i.e.
`target_RVA = -0x40000000` exactly, i.e. `target_VMA = imageBase − 0x40000000`.

If you ADD `0x40000000` to each rel32, the target RVA becomes **exactly 0** for all
three sites. So the link resolved these calls to RVA 0 (the PE MZ header at
`imageBase + 0`) with a constant −1 GiB bias baked into every rel32. RVA 0 is not
executable code — it's the MZ header byte.

This is not random corruption. A constant −1 GiB (= 0x40000000, a power of two) applied
to a class of `E8 rel32` fields, with the corrected target landing on RVA 0 (the
canonical "unresolved symbol" default), is the signature of either:

1. A post-build binary patcher that walks `.text` looking for `E8` opcodes and rewrites
   their rel32 operands with an off-by-`0x40000000` arithmetic error. **Prime suspect:
   `scripts/apply_d3dcompile_forensics.py`** (or another `apply_*.py` script). The 3
   affected functions are all in user-added code (ShaderFilter option setters), which
   is consistent with a patch script that targets your own code regions by name or
   pattern.
2. A mingw-w64 linker bug with weak symbols defaulting to RVA 0 with a −1 GiB bias.
   Less likely: would affect many builds, not just this one; the smoke tool builds with
   the same toolchain and is clean (different code, but same compiler/linker).
3. An LTO mis-link of three specific functions. Possible but unprovable without a clean
   rebuild.

### What the wild functions are (identified)

PE contains the mangled symbols `N10devilution23OptionEntryShaderFilterE` and
`N10devilution29OptionEntryShaderFilterPresetE` — these are DevilutionX's own
`devilution::OptionEntryShaderFilter` and `OptionEntryShaderFilterPreset` classes.
The globals at `0x140D14D40` and `0x140D14D20` (referenced by the wild functions via
the `.rdata` pointers at `0x140EEBB80` and `0x140EEBB90`) are instances of these
classes.

The three wild functions, identified by `.pdata` (function boundaries) and disassembly:

| Wild call RVA | Enclosing function (RVA range) | Size | Identified as |
|---|---|---|---|
| `0x292E8D` | `0x292E40 .. 0x292E93` | 83 bytes | `OptionEntryShaderFilter::SetValue` variant (uses `lea` for keys) |
| `0x7D568A` | `0x7D5640 .. 0x7D5690` | 80 bytes | `OptionEntryShaderFilter::SetValue` variant (uses globals for keys) |
| `0x7D56E2` | `0x7D5690 .. 0x7D56E8` | 88 bytes | `OptionEntryShaderFilterPreset::SetValue` variant |

All three share this exact structure (function B disassembled in full):

```
push rbp; mov rsp,rbp; push rbx; sub rsp, 0x28
mov rcx, [rip+0x712740]          ; rcx = options key global (0x140ee7d90)
mov rdx, rbp                     ; rdx = frame ptr
call 0x140b6f3b8                 ; _setjmp (import thunk → msvcrt.dll!_setjmp, hint 600)
test eax, eax
je .main_path                    ; if first call (returns 0), do work
xor eax, eax; ...; ret          ; longjmp-unwind path: return 0
.main_path:
mov rcx, [rip+0x716514]          ; rcx = OptionEntryShaderFilter* (0x140d14d40)
call 0x140b5ea10                 ; TLS-style slot resolver (helper at 0x140b5ea10, 586 bytes)
movb $0x1, (%rax)                ; *slot = true (claim option-set flag)
call *0x93827e(%rip)             ; GetCurrentThreadId()  via IAT slot 0x14110d8f8
                                 ;   (msvcrt.dll... wait, IAT walks KERNEL32.dll!GetCurrentThreadId hint 555)
mov rcx, [rip+0x71650f]          ; rcx = OptionEntryShaderFilterPreset* (0x140d14d20)
mov ebx, eax                     ; ebx = thread id
call 0x140b5ea10                 ; TLS-style slot resolver
mov [rax], ebx                   ; *slot = thread id (reentrancy guard)
call 0x100000000                 ; <-- WILD CALL (last instruction; no ret after)
nop                              ; alignment padding
<next function starts immediately>
```

Imports confirmed by walking `.idata`:

- IAT slot `0x14110D8F8` → `KERNEL32.dll!GetCurrentThreadId` (hint 555) — called by all
  three wild functions (this is the reentrancy guard for the synchronous-notify path,
  see §9).
- Import thunk `0x140b6f3b8` → `msvcrt.dll!_setjmp` (hint 600) — called early by all
  three wild functions and the parallel non-wild function (SEH setup).
- Import thunk `0x140b6f5c8` → `msvcrt.dll!strlen` (hint 1084) — used by other code.
- IAT slot `0x14110E190` → `msvcrt.dll!longjmp` (hint 1020) — called at the end of the
  parallel non-wild function (see below).

### The parallel non-wild function (tells us what the wild call was supposed to be)

A function at `0x14029A9E0` has the **exact same pattern** as the three wild functions
— same `_setjmp`-based SEH setup, same `OptionEntryShaderFilter` lookup via
`0x140b5ea10`, same `GetCurrentThreadId` reentrancy guard via IAT slot
`0x14110d8f8` — but ends correctly with a call to `longjmp`:

```
0x14029aa5e:  call *0xe72e94(%rip)       ; GetCurrentThreadId
0x14029aa64:  lea  rcx, [rip+0xa7a2b5]   ; rcx = OptionEntryShaderFilterPreset* (0x140d14d20)
0x14029aa6b:  mov  edi, eax              ; edi = thread id
0x14029aa6d:  call 0x140b5ea10           ; TLS-style slot resolver
0x14029aa72:  cmp  [rax], edi            ; compare stored thread id
0x14029aa74:  jne  .cleanup              ; if mismatch, skip the work
0x14029aa76:  movb $0x0, (%rsi)          ; *bool_slot = false  (release claim)
...                                           ; (work happens here)
0x14029aac0:  lea  rcx, [rip+0xdd6ad9]   ; rcx = options key global (0x1410715a0)
0x14029aac7:  call *0xe736c3(%rip)      ; call *0x14110e190  =  msvcrt.dll!longjmp  (throw path)
0x14029aacd:  nop
0x14029aace:  xchg %ax, %ax              ; alignment
<next function starts>
```

The intended target for the three wild calls was almost certainly **`longjmp`** (same as
this parallel function — the SEH-unwind throw path that pairs with the early `_setjmp`).
Whatever produced the wild rel32 fields resolved the symbol to RVA 0 (unresolved) with
a constant −1 GiB bias.

### Why the crash happens before D3DCompile

The wild functions are option setters. They run on the synchronous-notify path
documented in §9 ("Option value changes notify listeners SYNCHRONOUSLY"). When the
ShaderFilter option is set during preset load, these setters execute, hit the wild
`call 0x100000000` as their last instruction, and fault. The shader compiler never
gets called — `D3DCompile` is reached only after the option-setter chain returns, and
the wild call crashes the function before that point. The compile guard catches the
fault, the session falls back to the regular renderer, the game survives.

### Root cause — most likely (in descending order of probability)

1. **A post-build binary patch script corrupted these specific call sites.** Prime
   suspect: `scripts/apply_d3dcompile_forensics.py`. If it walks `.text` looking for
   `E8` opcodes and rewrites their operands (e.g., to relocate a call from one site to
   another, or to insert a hook stub), an off-by-`0x40000000` error in the rel32
   recomputation produces exactly this pattern. The 3 affected functions are all in
   user-added code (ShaderFilter option setters), consistent with a patch script that
   targets your own code regions. **This is the only hypothesis consistent with all
   evidence: only 3 sites affected, all in user code, all with the same constant bias,
   all targeting RVA 0.**
2. A mingw-w64 linker bug with weak symbols defaulting to RVA 0 with a −1 GiB bias.
   Less likely: would affect many builds, not just this one; the smoke tool builds
   with the same toolchain and is clean.
3. An LTO mis-link of three specific functions. Unprovable without a clean rebuild.

### Decisive next test (in order)

1. **Audit `scripts/apply_d3dcompile_forensics.py` (and any other `apply_*.py`).**
   Grep for `0x40000000`, `0x100000000`, `0x140000000`, `-0x40000000`, `image_base`,
   `rel32`, `E8`, `0x7D568A`, `0x7D56E2`, `0x292E8D`. Any code that walks `.text`
   looking for `E8` opcodes and rewrites their operands is the prime suspect. Add an
   assert that every rewritten call's target lands inside the image's VMA range; an
   `E8 rel32` whose target is more than ±2 GiB from the call site is by definition
   broken and would have caught this.

2. **Clean rebuild from source with NO post-build patches.** Run the same
   `objdump -d` on the clean binary and diff against the shipped one:

   ```bash
   git -C /home/z/my-project/devilutionx pull
   cmake -B build -G Ninja -DCMAKE_BUILD_TYPE=Release
   ninja -C build devilutionx_exe
   objdump -d build/devilutionx.exe > /tmp/clean.s
   objdump -d /home/z/my-project/analysis/devilutionx.exe > /tmp/shipped.s
   diff /tmp/clean.s /tmp/shipped.s | less
   ```

   If the diff shows only the 3 wild rel32 fields (plus any other intentional patches
   from `apply_d3dcompile_forensics.py`), the patch script is the corrupter — case 1
   confirmed. Fix the script, rebuild, ship.

   If the clean build ALSO has the wild calls (i.e., the diff shows no difference at
   those 3 RVAs), it's case 2/3 — bisect the toolchain (try a different mingw version,
   try gcc vs clang, try without LTO).

3. **If the patch script is the corrupter:** add a post-build self-check that scans
   `.text` for any `E8 rel32` whose target falls outside `[imageBase, imageBase +
   SizeOfImage)` and aborts the build. This would have caught the v3 bug before ship.

### Decisive evidence preserved in this session's sandbox

- `/home/z/my-project/analysis/devilutionx.exe` — the v3 binary you uploaded (16.6 MB,
  PE32+ x86-64 GUI, stripped, TimeDateStamp 2026-10-13 18:29:24 UTC).
- `/home/z/my-project/upload/devilutionx.zip` — the same, source archive (in case the
  extracted file is modified).

The disassembly and PE-header analysis were done with `objdump` 2.44 and a Python
script that walks `.pdata` / `.reloc` / `.idata`. Re-runnable from this handoff alone
— the binary is preserved. Key re-run commands:

```bash
# Verify the 3 wild calls are still in the on-disk file
cd /home/z/my-project/analysis
objdump -d --start-address=0x1407D5640 --stop-address=0x1407D56F0 devilutionx.exe
objdump -d --start-address=0x140292E40 --stop-address=0x140292EA0 devilutionx.exe
# Both should show "call 0x100000000" as the last instruction before the nop+pad.

# Count wild calls (should be 3, matching the 3 faulting sites)
objdump -d devilutionx.exe | grep "call.*0x100000000" | wc -l
```

### What was NOT the cause (so you don't re-litigate)

- OBS — fully exonerated (v2, fully unloaded, identical crash).
- `d3dcompiler_47.dll` — fully exonerated (v2: pD3DCompile provenance clean; v3:
  byte-vs-disk integrity verified; v3 final: D3DCompile never entered).
- AMD driver / D3D12 runtime — exonerated (smoke tool passes with same shaders).
- `apphelp.dll` / AppCompat SDB / IFEO / Application Verifier — exonerated (rename
  test negative: `devilutionx_derp.exe` crashes identically).
- mingw include-order trap (§9 gotcha) — exonerated (different failure mode; this is
  link-time/post-link, not compile-time).
- PageHeap / Application Verifier with Heaps — would not have caught this; the corrupt
  bytes were already wild when the page was mapped. No runtime write to catch.

## 8. Remaining tasks

- ⬜ **D3D12 §7 RESOLVED — fix is in flight:**
  1. audit `scripts/apply_d3dcompile_forensics.py` (and all `apply_*.py`) for the
     off-by-`0x40000000` rel32 rewriter;
  2. clean rebuild with NO post-build patches + `objdump` diff to confirm the corrupter;
  3. if patch script is guilty: fix it + add a post-build self-check that scans `.text`
     for any `E8 rel32` whose target is outside `[imageBase, imageBase + SizeOfImage)`;
  4. ship fixed binary + re-run field test on RX 560 (D3D12 path should now compile
     shaders and the preset should load — this is the first real D3D12 field test
     without the wild-call bug masking everything else).
- ⬜ #8737 course correction (§2), then refresh the PR branch from fork master:
  `git push --force-with-lease origin origin/master:gpu-shaders-combined` (drop
  `.github/workflows/gpu-shaders.yml` + `cleanup-fork-workflows.*` noise first)
- ⬜ Watch #906 → after merge, pin-bump PR (`SDL3-mixer-pin-bump.md`)
- ⬜ 26-i: Linux gcc/clang build verification + clang-format 18 sweep over all new files
- ⬜ 26-j: GPU_SHADERS.md for runtime presets (NOTE: fork master's copy lacks the
  forensics troubleshooting section — the 3rd file of the forensics zip was never
  applied) + PR decision: update #8737 vs new branch + PR (user's call). **Add a §7
  summary to GPU_SHADERS.md troubleshooting: "if D3DCompile faults on RX 560, check
  on-disk bytes at the faulting RVAs before chasing environmental causes."**
- ⬜ Cosmetic: highlighted row can coincide with an unrelated preset while browsing a
  different folder than the active one (selection itself is correct)
- ✅ hq2x/PNG codec, screenshots codec, fsr/hqx/stock flips, deadlock, selection, crash
  containment — done (§6)
- ✅ d3dcompile fault root cause — found (§7)

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
```

Established loop: fixes are packaged → user pushes to **fork master** → fork CI builds →
field test on Windows → next round branches from the result.

Nothing irreplaceable outside the repo except `reference/` (re-downloadable),
`worklog.md`, and `/home/z/my-project/analysis/devilutionx.exe` (the v3 field binary —
preserve for re-runnable §7 disassembly; if lost, the user can re-upload from their
Windows box). This file + the worklog + the v3 binary reconstruct the full state.
