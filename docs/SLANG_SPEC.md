# Slang Shader Specification Reference

**Audience:** fork maintainer (VTSTech). Working reference for extending `slang_translate.cpp` / `preset_parser.cpp` / `preset_runner.cpp` and for triaging preset failures during the Vulkan coverage campaign.
**Basis:** libretro slang shader docs (docs.libretro.com, "Slang Shader Development"), RetroArch reference parser (`gfx/video_shader_parse.c`), the slang-shaders corpus conventions, and a full source read of the five preset-layer TUs at fork master `dce1c4222` (2026-10-13, post-D3D12-excision; includes audit fixes F5 + P1).
**Companion docs:** `docs/GPU_SHADERS.md` (scaler user guide), `docs/audit/shader-support-audit-2026-10-14.md` (static audit + findings), `docs/diablo-handoff.md` (session state).

---

## 1. Scope and how to read this document

This is the format contract for the two file types the runtime shader system loads:

| File | Role |
|---|---|
| `.slangp` | Preset: an ordered chain of shader passes plus resources (LUTs, parameter values, scale rules, FBO controls) |
| `.slang` | One shader stage pair (vertex + fragment) in libretro slang — Vulkan-oriented GLSL with `#pragma` metadata and name-based texture semantics |

Every key and semantic below is annotated with the implementation status in this fork:

| Status | Meaning |
|---|---|
| Supported | Parsed, executed, and exercised in the field |
| Partial | Parsed/executed with a documented limitation (see note) |
| Extension | Not part of the libretro spec / RetroArch grammar; fork-specific |
| Parsed, unused | Accepted and stored but never consumed at runtime |
| Unimplemented | Not supported; behavior on contact is stated (hard error, silent ignore, or fallback) |
| Divergent | Implemented, but measurably different from RetroArch semantics |

Implementation ownership:

| Layer | File | Owns |
|---|---|---|
| Data model | `preset_types.hpp` | `Preset`, `PassDef`, `TextureDef`, scale/wrap/filter enums |
| Preset grammar | `preset_parser.cpp` | `.slangp` keys, `#reference` merge, path resolution, preset scan |
| Translation | `slang_translate.cpp` | `.slang` → per-stage GLSL: stage split, includes, pragmas, UBO/push re-homing, sampler rewrite, semantic validation |
| Compilation | `slang_compile.cpp` | glslang → SPIR-V, sidecar `.spv` fast path, content-addressed cache |
| Execution | `preset_runner.cpp` | Pipelines, FBOs, history ring, feedback double-buffering, LUT upload, uniform fill, texture binding |

Corpus frequencies quoted throughout come from `docs/audit/slang_audit_v2.csv` (2658 presets, full libretro/slang-shaders tree, `tools/audit_slang_presets.py` v2).

## 2. Pipeline model

A preset is a linear chain of passes. Pass 0 samples the chain input — the game frame, already RGBA8, palette resolved upstream (no palette pass needed). Pass N for N > 0 samples the output of pass N-1 (`Source`). Every pass renders a fullscreen triangle-list quad (6 vertices, `float4 Position` + `float2 TexCoord`, pitch 24 bytes) into its own color target. The final pass output is returned to the GPU scaler, which presents it through the shared blit; the scaler owns the failure contract (any load/run failure → session fallback to the classic renderer — see `docs/GPU_SHADERS.md`).

Two execution-side behaviors are part of this fork's contract and are not optional to re-derive:

- **Orientation law.** The fullscreen quad pairs UV v=0 with clip-space y=-1 while SDL3 GPU render targets map clip-space y=+1 to the top row, so every pass draw composites one vertical mirror. The present blit cancels exactly one mirror. Therefore every pass that renders into an *intermediate* target (i.e. every pass except the last) receives a Y-mirror MVP, `diag(1, -1, 1, 1)`; the last pass keeps identity so the shared present blit still cancels. Vertex stages with no `MVP` member keep their own (unknown) orientation and are flagged `vertexMvp=no` in the load log. This is field-proven (12-preset `zz-diag-*` bisect grid, handoff §6 #7) — do not re-litigate.
- **Order of a frame.** History ring blit (chain input into the current ring slot; first run and every target resize seed *all* slots) → per-pass draws in chain order (uniform push, texture bind, draw) → double-buffer flip for feedback passes → history ring slot advance → final pass output handed to the presenter.

Pass targets are re-created only when the game resolution or swapchain size changes; everything else (shaders, pipelines, LUTs, samplers) lives for the preset session. There is no hot reload; switching presets rebuilds the chain in place.

---
## 3. Preset format (`.slangp`) — key reference

### 3.1 File syntax

| Rule | Behavior in `preset_parser.cpp` |
|---|---|
| Line form | `key = value`; everything before `=` is the key, everything after the value |
| Key case | Keys are lowercased at parse time. Consequence: parameter override names become lowercase; they are matched case-insensitively at application (see 3.6) |
| Value quoting | A value starting with `"` is taken up to the next `"` (first quoted segment); otherwise trimmed as-is. Optional in all cases — the grammar accepts both `shaders = 2` and `shaders = "2"` |
| Comments | `#` starts a comment (full-line or trailing) unless it opens the line as `#reference`; a `#` inside double quotes never starts a comment. Trailing comments on a `#reference` line break the directive match (the whole line must match the directive regex) |
| Unknown keys | Silently ignored |
| Whitespace | Leading/trailing whitespace trimmed on keys, values, list tokens |
| Line endings | CRLF and LF both fine (values trimmed; includes strip CR) |

### 3.2 Global keys

| Key | Status | Semantics |
|---|---|---|
| `shaders` | Supported | Pass count. Combined with the highest indexed `shaderN` discovered (`shaders = 6` with only `shader0..2` defined still yields 3 passes after empty passes are dropped). A preset whose final chain is empty fails with "preset has no shader passes" |
| `feedback_pass` | Parsed, unused (audit F2) | RetroArch's explicit double-buffering hint. Parsed and `#reference`-merged, never read: this fork derives double-buffering from actual feedback reads (demand-driven, §5.3), which covers every observable case. Keep dead or wire up when spec-conformance testing starts |
| `histories` | Extension, Parsed, unused | Chain input history depth, clamped 1..8. Not in the RetroArch grammar (verified: no such key in `gfx/video_shader_parse.c`). The history ring depth is derived from the highest `OriginalHistory#` actually referenced by any pass, so this key is informational only today |
| `textures` | Supported | Semicolon-separated LUT texture name list, e.g. `textures = "mask;glow"`. Declared names are materialized even if no per-name key follows; every texture must end up with a path or the preset fails ("preset texture has no path") |
| `parameters` | Supported | Semicolon-separated parameter names eligible for value overrides from this file. Gates 3.6 |
| `#reference "path"` | Supported (divergence note) | Inherit from a parent preset. Multiple `#reference` lines allowed; depth limit 8. Merge rules in 3.7 |

### 3.3 Per-pass keys (`N` = pass index, 0-based)

All per-pass keys are index-suffixed (`shader0`, `wrap_mode1`, …), matching RetroArch (`gfx/video_shader_parse.c` builds every pass key as `<base><N>`). An un-indexed per-pass key is not a pass key — it falls through the texture/parameter heuristics and is then ignored.

| Key | Status | Semantics |
|---|---|---|
| `shaderN` | Supported | Path of the `.slang` file for pass N, resolved like every preset reference (3.7 path rules). RetroArch-compatible |
| `aliasN` | Supported | Names pass N's output so later passes (and pass N itself, via the `Feedback` suffix form) can sample it by name. Also enables the RetroArch "sample an earlier pass's plain alias" pattern (hqx's `hqx_refpass`). Not uniqueness-checked; first match wins at bind time |
| `filter_linearN` | Supported | `true` → linear, `false` → nearest sampling of pass N's input. Applies to `Source` sampling only (§5.4) |
| `wrap_modeN` | Supported (divergence) | `repeat` / `mirrored_repeat` / `clamp_to_edge`; anything unrecognized → `clamp_to_edge`. RetroArch also accepts `clamp_to_border`; that value silently maps to clamp-to-edge here (SDL3 GPU sampler cache builds only 3 address modes). Unrepresented in the corpus |
| `srgb_framebufferN` | Supported | Pass N's output target becomes sRGB (`R8G8B8A8_UNORM_SRGB`). Precedence over `float_framebufferN` when both are set (0 presets set both — audit F8; note the audit report's table states the opposite precedence, an erratum — the code checks sRGB first) |
| `float_framebufferN` | Supported | Pass N's output target becomes half-float (`R16G16B16A16_FLOAT`). Ignored when `srgb_framebufferN` is also set or when the pass's shader carries `#pragma format` |
| `mipmap_inputN` | Supported (F1) | Selects the mipmapped sampler for pass N's input AND makes it effective: the bound `Source` texture carries a full mip chain refilled each frame before pass N (`SDL_GenerateMipmapsForGPUTexture` between render passes). Pass N-1's output is allocated with a chain exactly when pass N reads it mipmapped; pass 0's chain input goes through a runner-owned mip-capable copy (RGBA8, same size — the game texture is a single-level scaler surface). Mip filter is linear, min/mag follow `filter_linearN` (RetroArch-style). LUT chains pre-generate at upload (§3.5). History/feedback/alias bindings keep the nearest/clamp default (§5.4) and never consume mips. 892 presets set the flag (888 Mega_Bezel-tier) |
| `frame_count_modN` | Supported | Wraps `FrameCount` to `count % mod` for pass N. Per-pass indexing confirmed against RetroArch's parser. 0 disables |
| `scaleN` | Supported (P1-fixed) | Sets both axes' scale *value* while preserving the pass's declared scale type (order-independent, RetroArch-style). The pre-fix behavior (bare `scale` reset a previously declared `scale_type` to Source) mis-scaled 897 presets — fixed by audit patch 0002 |
| `scale_xN` / `scale_yN` | Supported | Per-axis scale values (type comes from `scale_type_xN` / `scale_type_yN`, default Source) |
| `scale_typeN` | Supported | `source` / `viewport` / `absolute`; invalid → `source`. Applies the type to both axes |
| `scale_type_xN` / `scale_type_yN` | Supported | Per-axis scale type override |
| `shaderpathN` | Extension | Unofficial: absolute or preset-relative path override for the pass shader, stored as-is and resolved through the same reference path rules. Not in RetroArch |

### 3.4 Scale rule semantics

Per pass, per axis: `source` → previous output size (game frame for pass 0) × scale; `viewport` → viewport size × scale, where the viewport is the aspect-corrected game image rect fitted inside the swapchain (F6, fixed in `f87f89d18`); `absolute` → scale pixels. Axes are independent; results are rounded half-up and clamped to ≥ 1. The resolved sizes feed the FBO allocation and every size uniform (`OutputSize`, and the next pass's `SourceSize`).

Fixed (audit F6, `f87f89d18`): `viewport` now resolves against the aspect-corrected viewport — the game image rect fitted inside the swapchain with letterbox margins (half-up rounding), matching RetroArch — instead of the raw swapchain. Border/bezel presets computing aspect-dependent geometry now size their FBOs for the area the game actually occupies, and a viewport-scaled final pass lands exactly on the presentation blit's letterbox rect (the blit fits the chain output the same way). Extension note: integer scaling stays a presentation-level fit here and is not folded into the shader viewport base (RetroArch folds `video_scale_integer` in when enabled); watch field logs if integer-scaled bezels misalign.

### 3.5 Lookup textures (LUTs)

| Aspect | Status | Behavior |
|---|---|---|
| Declaration | Supported | `textures = "a;b"` plus per-name keys. Names keep their declared case (GLSL identifiers are case-sensitive) |
| Path key | Supported | `name = "path"` (any position relative to the `textures` line); also `name_path` |
| Per-texture options | Supported | `name_linear` (filter), `name_wrap_mode` (wrap), `name_mipmap` (generate mip chain + mipmapped sampler). Suffix matching is case-insensitive against the declared list |
| Implicit declaration | Supported (RetroArch-compat) | A key with no other meaning whose value ends in `.png/.jpg/.jpeg/.bmp/.tga/.dds/.webp` declares a texture by that key name |
| Formats | Partial | Any SDL_image-decodable file; converted to RGBA8 and uploaded as `R8G8B8A8_UNORM`. Float LUT textures (e.g. `.exr`, half-float PNG) are not preserved at full precision — 8-bit upload |
| Mipmaps | Supported | `SDL_GenerateMipmapsForGPUTexture` at upload when `name_mipmap = true`; mip levels = full chain. The texture now also carries COLOR_TARGET usage: SDL's GenerateMipmaps contract requires SAMPLER + COLOR_TARGET, and the Vulkan path never needed it (transfer bits are always present and the device disables SDL debug mode), so the flag gap was invisible on the shipping backend (fixed with F1) |
| `User#` sampling | Supported | Spec-sanctioned positional access to the declared textures (`User0` = first declared LUT). Direct `User#` is discouraged by the spec in favor of aliases, but supported. Note `User#Size` uniforms do NOT resolve (§4.7) |
| git-lfs pointers | Supported diagnostic | A LUT file that is a git-lfs pointer (archive download / non-LFS checkout) fails `IMG_Load` with an explicit "POINTER file, not image data" hint in the error string (`preset_runner.cpp`) |

### 3.6 Parameters

| Aspect | Status | Behavior |
|---|---|---|
| `#pragma parameter` (shader side) | Supported | `#pragma parameter ID "Description" initial min max [step]` — order verified against RetroArch. Extracted by `slang_translate.cpp`; step defaults 0.001 when absent. Trailing `//` / `/* */` comments after the numbers are accepted (RetroArch's parse is sscanf-style and ignores trailing junk; 58 corpus pragma lines carry one — field round 1: they were silently dropped before, leaving their UBO members unvalidated). These are the source of defaults |
| Preset override value | Supported (modern RetroArch semantics) | `<name> = "0.35"` — a single float that becomes the parameter's *current* value, overriding the pragma `initial`. Modern RetroArch reads exactly one float per parameter (`video_shader_load_current_parameter_values`); legacy multi-field value strings ("initial, min, max, step") are tolerated because only the leading number is parsed |
| `parameters` gating | Supported | A key only counts as a parameter override if listed in `parameters` (or via the overlay heuristic below) |
| Overlay heuristic | Extension | When a `#reference`d overlay (Mega Bezel `.params` files) declares no `parameters` list at all, any unknown key whose value parses fully as a number is accepted as an override. Unknown names stay inert at runtime (applied only if they match a `#pragma parameter`) |
| Merge | Supported | Parent overrides accumulate across `#reference` parents; the child file's own keys replace same-named inherited overrides |
| Application | Supported | `preset_runner.cpp` fills a UBO member with: preset override (case-insensitive name match, fixed in `6f13d787f` — gap #3) → `#pragma` default → 0.0. Before the fix the parser-lowercased override key only matched lowercase shader names, so mixed-case Mega Bezel parameters (`ABS_APECT_mult`-style) silently kept pragma defaults |
| Uniforms UI | Unimplemented | Parameters are fixed per session at defaults/overrides; there is no in-game slider UI. By design (§4 of the handoff) |

### 3.7 `#reference` inheritance and path resolution

| Aspect | Status | Behavior |
|---|---|---|
| Directive | Supported | `#reference "relative/or/absolute.slangp"` on its own line. Multiple parents allowed (chain preset + one or more `.params` overlays, the Mega Bezel pattern) |
| Depth | Supported | Recursion limit 8; beyond it the preset fails |
| Parent merge — passes | Supported (behavior note) | Each parent with a non-empty chain *replaces* the current chain (parent shader paths are rebased to the parent's folder first). With several chain-bearing parents the last one wins — the in-code comment says "first chain wins", which does not match the code; only one chain-bearing parent exists in practice, but fix the comment or the code before relying on the order |
| Parent merge — textures | Supported | Accumulate; case-insensitive name dedupe (first occurrence wins); paths rebased to the parent's folder |
| Parent merge — params / `feedback_pass` / `histories` | Supported | Params accumulate (child wins per name); `feedback_pass` adopted only if the child did not set one; `histories` adopted only if the child kept the default 1 |
| Child keys | Supported | Applied on top of everything inherited |
| Overlay-only parents | Supported | A `#reference` target may legitimately have zero passes (parameter-only overlay); only the top-level preset must end with a chain |
| Path resolution (all preset references: passes, LUTs, `#reference`) | Supported (extension) | `ResolvePresetFilePath`: absolute paths pass through; relative paths try the preset's own folder, then up to 6 ancestor folders, then the shader root — first existing candidate wins; on total miss the conventional preset-relative join is returned so the error message shows the expected path. This is broader than RetroArch (preset-folder-relative only) and is what makes redistributed pack layouts (`<pack>/Presets-4.1/*.slangp` referencing `<pack>/shaders-ng/*.slang`) load |
| Unresolved parent | Failure | A `#reference` target that cannot be parsed fails the whole preset (error names the parent error) |

---
## 4. Shader format (`.slang`) — semantics reference

### 4.1 File conventions and stage split

| Aspect | Status | Behavior |
|---|---|---|
| `#version` | Supported | Honored only in the top-level file; stripped from `#include`d files. Injected as `#version 450` when absent. Compiled as Vulkan 1.0 / SPIR-V 1.0 (`slang_compile.cpp`) |
| `#pragma stage vertex` / `#pragma stage fragment` | Supported | Line directives switch the capture bucket; text before the first directive is *shared* and is concatenated into both stages (this is how the corpus declares common UBOs/functions once) |
| Fragment-only files | Supported | Missing vertex stage gets the stock pass-through vertex (MVP × Position, TexCoord → vTexCoord) injected by `slang_translate.cpp` |
| Vertex-only files | Unimplemented | Fail ("shader file has no fragment stage") |
| Entry point | Supported | `main` in both stages; a fragment stage without `main` fails |
| Empty files | Unimplemented | Fail ("shader file has no code") |

### 4.2 Pragmas

| Pragma | Status | Behavior |
|---|---|---|
| `#pragma name <id>` | Supported | Display/identifier metadata; also makes the pass's output samplable by that identifier from other passes (§4.5). Pre-scanned by `preset_runner.cpp` before translation |
| `#pragma stage ...` | Supported | See 4.1 |
| `#pragma parameter ...` | Supported | See 3.6 |
| `#pragma format <token>` | Supported (subset) | Per-pass output format override; takes precedence over `srgb_framebufferN`/`float_framebufferN`. Supported token set in 4.2.1; the single unmapped token (`A2B10G10R10_UINT_PACK32`) warns and falls back to `R8G8B8A8_UNORM` instead of failing the preset |
| `#pragma alias <id>` | Unimplemented | Not extracted; the line passes through to glslang, which ignores unknown pragmas. Preset-level `aliasN` works; the in-shader declaration does nothing. Rare in the corpus |
| `#pragma include_optional "path"` | Unimplemented | Not resolved (treated as an unknown pragma); if the shader actually needs the file's symbols it then fails at compile. Not observed in the corpus |
| `#error` / `#warning` | Divergent | RetroArch treats `#error` as a "skip this shader" feature gate. Here they are left in the source and glslang fails the compile on `#error` — the preset falls back per the failure contract. Net effect for gated shaders is the same (not loaded), but the failure is a compile error, not a clean skip |

#### 4.2.1 `#pragma format` tokens

| Spec token group | Status |
|---|---|
| `R8_UNORM`, `R8G8_UNORM`, `R8G8B8A8_UNORM`, `R8G8B8A8_SRGB`, `R16_SFLOAT`, `R16G16_SFLOAT`, `R16G16B16A16_SFLOAT`, `R32_SFLOAT`, `R32G32_SFLOAT`, `R32G32B32A32_SFLOAT` | Supported |
| `*_UINT` / `*_SINT` 8/16/32-bit: `R8`, `R8G8B8A8`, `R16`, `R16G16B16A16`, `R32`, `R32G32B32A32` | Supported |
| `R8G8_UINT`, `R8G8_SINT`, `R16G16_UINT`, `R16G16_SINT`, `R32G32_UINT`, `R32G32_SINT` | Supported (2026-10-14, gap-#6 round; mapping table in `preset_runner.cpp`). 0 corpus hits — mapped for spec conformance |
| `A2B10G10R10_UNORM_PACK32` (HDR10 outputs) | Supported (2026-10-14, gap-#6 round): SDL GPU names the same layout `R10G10B10A2_UNORM` and its Vulkan backend maps it back to `VK_FORMAT_A2B10G10R10_UNORM_PACK32`. 18 corpus shaders request it (crt-effects curvature, bezel packs) |
| `A2B10G10R10_UINT_PACK32` | Unimplemented → warn + RGBA8 fallback. SDL GPU has no UINT form of the packed 10-bit layout; 0 corpus hits |

### 4.3 Includes

| Aspect | Status | Behavior |
|---|---|---|
| `#include "path"` | Supported | Resolved recursively, relative to the *including* file; falls back to the primary shader's folder for packs assuming a flat layout (spec-divergent convenience, needed in the field) |
| Include-once | Supported | Canonical-path dedupe: an already-included file is dropped, so cycles terminate silently (spec leaves cycles undefined) |
| Depth | Supported | Limit 24; beyond it the shader fails |
| `#version` in includes | Supported | Stripped; only the top-level file may carry it |

### 4.4 Vertex interface

| Aspect | Status | Behavior |
|---|---|---|
| Inputs | Supported | `layout(location = 0) in vec4 Position;` and `layout(location = 1) in vec2 TexCoord;` — injected when absent (name- and location-regex detected). This matches the spec's two mandated vertex inputs |
| `MVP` | Supported | Standard member of the vertex UBO/push block (`uniform projection { mat4 MVP; };` is the corpus convention). Filled per the orientation law (§2). Absent → stage runs un-mirrored and the load log flags `vertexMvp=no` |
| `gl_Position` | Supported | Authored by the shader as usual |
| Vertex textures | Unimplemented | Any `sampler2D` in a vertex stage is a hard error ("vertex-stage texture sampling is not supported yet"). Spec allows vertex texture fetch; zero corpus presets use it |
| Vertex `TexelFetch`/derivatives | Supported | Compiled as written; glslang enforces Vulkan rules |

### 4.5 Fragment texture semantics (name deduction)

Sampler declarations are rewritten to dense bindings (set 0 vertex / set 2 fragment). Deduction is by declared name, the spec's "deduction by name" model:

| Name | Status | Binding |
|---|---|---|
| `Source` | Supported | Previous pass output this frame (chain input for pass 0). Sampler = the reading pass's `filter_linearN` / `wrap_modeN` / `mipmap_inputN` |
| `Original` | Supported | Chain input this frame (game texture). Default nearest/clamp sampler |
| `OriginalHistory0` | Supported | Identical to `Original` (spec: alias) |
| `OriginalHistory1..8` | Supported | Chain input N frames back, from the history ring (§5.2). Spec allows unbounded N; the translator rejects N > 8 ("out of range") — corpus maximum is 7 |
| `OriginalHistory9+` | Unimplemented | Hard translate error. Raise the cap only if a preset ever needs it |
| `PassOutput#` | Supported | Pass # output this frame; causality enforced — `N >= passIndex` or `N >= passCount` is a hard error (spec-conformant) |
| `PassFeedback#` | Supported | Pass # output from the *previous* frame; requires `N < passCount`, otherwise hard error. Any pass may read any pass's feedback (always causal per spec) |
| `aliasN` names | Supported | Earlier passes' outputs by plain alias name (RetroArch's hqx pattern); only passes before the reader have output this frame, later ones resolve through the fallback path below |
| `#pragma name` identifiers | Supported | Same as alias names — outputs samplable by pass identifier |
| `<name>Feedback` | Supported | Alias/identifier + `Feedback` suffix = that pass's previous-frame output; declaring pass is auto-double-buffered (§5.3). Suffix match is case-insensitive at validation and at bind (F9 closed, field round 1) |
| LUT texture names | Supported | Declared `textures` names; case-insensitive at validation and at bind (F9 closed, field round 1) |
| `User#` | Supported | LUT by declaration order |
| Unknown names | Fallback | Warn once, bind the current source texture ("unresolved texture" — silent-wrongness guard; never a null bind, which crashed on AMD in round 4) |

### 4.6 Uniform blocks (UBO / push constants)

| Aspect | Status | Behavior |
|---|---|---|
| Plain UBO per stage | Supported (max 1) | Rewritten to `std140` + SDL3 GPU descriptor conventions: vertex UBO → set 1 binding 0; fragment UBO → set 3 binding 0. A second plain UBO per stage is a hard error ("spec allows one UBO") — never occurs in the corpus (audit T5) |
| `push_constant` block | Supported | Re-homed as a std140 UBO (binding 1 when a plain UBO exists in the same stage, else binding 0), pushed per draw. SDL3 GPU push-constant size limits do not apply |
| std140 layout | Supported | Computed by the translator: `float/vec2/3/4`, `int/ivec2-4`, `uint/uvec2-4`, `bool`, `mat4`/`mat4x4`; arrays sized 1..64 with std140 stride (rounded to a full vec4); multi-name declarations (`float A, B;` and the no-space form `float A,B,C;` — 34+ corpus lines) handled. Unsized or non-numeric sizes are a hard error (the size parse is guarded — previously a malformed `[x2]` aborted on uncaught `stoi`) |
| Standalone uniforms | Unimplemented | `uniform float x;` outside a block is not rewritten — it would compile with glslang's auto-assigned set/binding and then violate the SDL3 GPU descriptor model. Not present in the corpus (all semantic uniforms live in blocks) |
| Member validation | Supported (tolerant) | Every block member is checked against recognized semantics (4.7), `<Texture>Size`/`<Texture>FeedbackSize` forms, and `#pragma parameter` names (case-insensitive). Unrecognized members log a warning at load and read 0 — RetroArch leaves unknown UBO members at zero too (field round 1: `auto_res`, `XBR_ANTI_RINGING`, `ia_FLIP_HORZ` hard-failed translation here for shaders RetroArch loads; the loud gate cost 10+ real presets). Structural errors (unsized arrays, bad array sizes, >1 UBO) remain hard errors |

### 4.7 Builtin uniform semantics

All size semantics carry the spec's `vec4(x, y, 1/x, 1/y)` payload; a `vec2` member is accepted and receives just (x, y). The same member name may appear in both the UBO and the push block (spec-sanctioned; both get filled).

| Member | Type | Status | Value written by `preset_runner.cpp` |
|---|---|---|---|
| `MVP` | `mat4` | Supported | Orientation law: flip-Y for every intermediate pass, identity for the last pass |
| `OutputSize` | vec4 | Supported | This pass's output size |
| `FinalViewportSize` | vec4 | Supported | Aspect-corrected viewport: the game image rect fitted inside the swapchain (F6, fixed in `f87f89d18`; was the raw swapchain). RetroArch's final-viewport semantics; the presentation blit letterboxes onto the same rect |
| `OriginalSize` | vec4 | Supported | Game frame size |
| `SourceSize` | vec4 | Supported | Current pass input size |
| `FrameCount` | uint | Supported | Frames since preset load (starts at 1), pre-wrapped by the pass's `frame_count_modN` |
| `FrameDirection` | int | Supported (divergent) | Constant `+1`. Spec: `-1` while rewinding — there is no rewind state in this game |
| `FrameTime` | float | Supported | Duration of the previous frame in microseconds (RetroArch extension; 5 corpus presets) |
| `PASSINDEX` | int | Extension | Current pass index. Not in the libretro spec or RetroArch; supported because packs use it |
| `Rotation` | uint | Supported | Spec builtin (0..3 = 0/90/180/270 degrees). Added to the member whitelist (gap #5, 2026-10-14) and filled with the identity value `0` — this frontend never rotates the view. 16 corpus shaders declare `uniform uint Rotation;` (crt-effects, authentic_gbc, border fills, subframe BFI); all previously failed translation loudly |
| `<Texture>Size` | vec4 | Supported | Texture size by name: `Source` → input; `Original`/`OriginalHistory#` → game frame; `PassOutput#` → that pass's output; alias/`#pragma name`/LUT → that texture's size. It is valid per spec to declare a size without the texture itself. (The one latent bug this family had — the `PassFeedback#Size` index parse — is fixed, next row) |
| `PassFeedback#Size` | vec4 | Supported | Fixed in `6f13d787f` (gap #4): the index parse is prefix-length-aware (10 for `PassOutput#`, 12 for `PassFeedback#`); previously `stoi` got `"ck#"` and the uncaught `std::invalid_argument` aborted the process. Covers both the `<PassFeedback#>Size` and `<PassFeedback#>FeedbackSize` member forms (the Feedback-suffix strip lands on the same branch). Regression-tested in `tools/audit/test_uniform_names.cpp` |
| `<Texture>FeedbackSize` | vec4 | Supported | Same size as the texture (feedback shares the pass output's size) |
| `User#Size` | vec4 | Unimplemented | Falls into the alias/LUT lookup by literal name `User#` and resolves to (0, 0) rather than the indexed LUT's size. No corpus preset observed using it |

### 4.8 Compilation and caching

| Aspect | Behavior (`slang_compile.cpp`) |
|---|---|
| Compiler | In-process glslang (FetchContent), Vulkan 1.0 client, SPIR-V 1.0 target, `EShMsgSpvRules` + `EShMsgVulkanRules` |
| Fast path | `<name>.vert.spv` / `<name>.frag.spv` sidecars next to the `.slang` always win (magic-number + size validated) |
| Cache | Content-addressed: SHA-256 over the *translated* vertex + fragment GLSL, so any source/include/translator change invalidates; blobs in `shaders/cache/` named `slang-<stem>-<hash24>.{vert,frag}.spv` |
| Failure | Compile errors fail the preset load with the glslang info log; the session falls back per the failure contract |

---
## 5. Runtime execution contract

### 5.1 Target lifecycle and formats

Pass targets are allocated per pass at the resolved scale-rule size, with usage sampler + color target, 1 mip level, 1 sample. Format precedence: `#pragma format` (4.2.1) → `srgb_framebufferN` → `float_framebufferN` → `R8G8B8A8_UNORM`. Load/clear ops are clear(black, opaque)/store per pass. The history ring (when used) allocates `depth + 1` RGBA8 textures at game resolution and is re-created on any game/swapchain resize together with the pass targets. All double-buffered passes flip after the frame; history slot advances after the frame.

### 5.2 History (`OriginalHistory#`)

The ring holds `maxReferencedOriginalHistoryIndex + 1` slots. Each frame, the chain input is blitted into the current slot before any pass runs; `OriginalHistoryN` binds `ring[(slot - N) mod size]`, i.e. frame T-N. On the first run and after every resize all slots are seeded with the current frame so early taps read valid content (matches RetroArch's history warmup; `OriginalHistory0` binds the live game texture directly). This is audit fix F5 (patch 0001): the pre-fix ring held `depth` slots and indexed forward, so `OriginalHistory1` returned the current frame at depth 1 and depth ≥ 3 sampled never-written memory on early frames — silent wrongness across 264 corpus presets. `test/history.slangp` (max_hist 3) is the standing canary.

### 5.3 Feedback (`PassFeedback#` / `<name>Feedback`)

Double-buffering is demand-driven: a pass whose output is read through `PassFeedback#` or a `<name>Feedback` sampler gets two output textures; the reader binds the buffer not being written this frame (frame N-1's output), and buffers flip after the frame. `feedback_pass` stays parsed-but-unused (F2). The null-texture guard from round 4 stands: an unresolved feedback/alias never binds null — it warns once and binds the source texture.

### 5.4 Sampler assignment

| Bound texture | Sampler |
|---|---|
| `Source` | The reading pass's `filter_linearN` + `wrap_modeN` + `mipmap_inputN`; with the mip flag the input is mip-chain refilled per frame (F1) |
| `Original`, `OriginalHistory#`, `PassOutput#`, `PassFeedback#`, `<name>Feedback`, plain aliases | Nearest / clamp-to-edge (deliberate default; RetroArch binds pass outputs by the producing pass's filter — verify against RetroArch only if an alias-heavy Phase-2 preset misrenders) |
| LUTs (by name or `User#`) | The LUT's own `linear` / `wrap_mode` / `mipmap` options |

Sampler cache: 2 filters × 3 wraps × 2 mip modes, created lazily per device session.

### 5.5 What "working" means

Unchanged from the handoff (§12 acceptance criteria): compiles clean, first frame defect-free (no flip/black/corruption), survives 60 s of gameplay, no feedback tearing across the `frame_count_mod` boundary. Partial outputs (renders with a visible defect) are tracked separately in sweep CSVs because they tend to share root causes with full failures.

## 6. Implementation status matrix (one page)

| Feature | Status | Where | Notes / finding |
|---|---|---|---|
| Chain of N passes, per-pass shaders | Supported | parser, runner | |
| `filter_linear` / `wrap_mode` per pass and per LUT | Supported | parser, runner | `clamp_to_border` → clamp-to-edge (divergence) |
| Scale rules (source/viewport/absolute, per-axis) | Supported | parser, runner | Viewport = aspect-corrected game rect (F6 fixed, `f87f89d18`); bare-`scale` order fix shipped (P1) |
| sRGB / float FBOs | Supported | runner | sRGB precedence when both set (F8 erratum); `#pragma format` overrides |
| `#pragma format` | Supported (subset) | translate→runner | One token left unmapped (`A2B10G10R10_UINT_PACK32` → warn + RGBA8; 4.2.1) |
| Aliases + `#pragma name` identifiers + plain-alias sampling | Supported | runner | First-match on duplicate names; case-insensitive end to end (field round 1) |
| `Original` / `Source` / `OriginalHistory0..8` | Supported | runner | Ring depth+1, seeded, T-N (F5 fixed); cap 8 vs spec unbounded |
| `PassOutput#` | Supported | translate, runner | Causality enforced |
| `PassFeedback#` / `<name>Feedback` | Supported | runner | Demand-driven double buffering (F2: `feedback_pass` dead) |
| `PassFeedback#Size` uniform | Supported | runner | Length-aware index parse (gap #4 fixed, `6f13d787f`); was a `stoi("ck#")` latent crash (4.7) |
| LUTs incl. mipmap LUTs, `User#` | Supported | runner | 8-bit upload; `User#Size` unresolved |
| `mipmap_input` | Supported (F1) | runner | Mip-chained pass outputs + per-frame `SDL_GenerateMipmapsForGPUTexture`; chain-input copy for pass 0; LUT usage flags satisfy SDL's GenerateMipmaps contract |
| Parameters: `#pragma parameter` + preset overrides | Supported | translate, runner | Case-insensitive application (gap #3 fixed, `6f13d787f`); no UI (by design) |
| `#reference` inheritance + ancestor path fallback | Supported | parser | Multi-parent; last non-empty chain wins (code/comment mismatch note) |
| UBO (≤1/stage) + push-constant re-homing, std140 | Supported | translate, runner | Unsized arrays / extra UBOs / standalone uniforms unsupported |
| Semantic member validation | Supported (tolerant) | translate, runner | Whitelist + `<Tex>Size` forms + param names; unknown members warn + read 0 (RetroArch-conformant, field round 1) |
| Vertex interface (Position/TexCoord) + stock vertex injection | Supported | translate | |
| Vertex textures | Unimplemented | translate | Hard error; 0 corpus hits (T3) |
| Sampler arrays, multi-name samplers | Arrays error, multi-name OK | translate | 0 array hits in corpus (T4) |
| `frame_count_mod` per pass | Supported | parser, runner | |
| `FrameCount` / `FrameDirection` / `FrameTime` / `PASSINDEX` | Supported | runner | Direction constant +1; PASSINDEX is an extension |
| `Rotation` | Supported | translate, runner | uint semantic, filled with 0 (no rotation source; gap #5) — 16 corpus shaders declare it |
| `#include` (recursive, include-once) | Supported | translate | Depth 24; flat-layout fallback |
| `#pragma name/stage/parameter/format` | Supported | translate | |
| `#pragma alias` / `include_optional` / `#error` skip | Unimplemented | translate | Ignored / unresolved / compile-fails (4.2) |
| glslang → SPIR-V, sidecar `.spv`, SHA-256 cache | Supported | compile | Sidecars always win |
| Vulkan-only backend | Supported | runner | D3D12 excised (handoff §8); Metal not wired for presets |
| Failure contract (session fallback, never dies) | Supported | gpu_scaler | handoff §4 |

## 7. Known divergences and gaps (priority order)

Ordered by expected yield for the Vulkan coverage campaign; corpus numbers from the v2 audit CSVs (`docs/audit/`).

1. ✅ **Viewport scale = swapchain, no aspect derivation (F6)** — FIXED (`f87f89d18`): `preset_runner.cpp` derives the aspect-corrected viewport (game image rect letterbox-fitted into the swapchain, half-up rounding) and resolves `viewport` scale and `FinalViewportSize` against it; a viewport-scaled final pass lands exactly on the presentation blit's letterbox rect. Regression test: `tools/audit/test_viewport_derive.cpp` (13 checks). Integer scaling remains a presentation-level fit (extension note in §3.4).
2. ✅ **`mipmap_input` no-op for pass outputs (F1)** — FIXED (2026-10-14, F1 round): pass outputs targeted by a mip-reading next pass allocate full mip chains and `SDL_GenerateMipmapsForGPUTexture` refills them each frame between render passes; pass 0 reads the chain input through a mip-capable RGBA8 copy blitted and refilled per frame (the game texture stays a single-level scaler surface). LUT textures now carry the usage flags SDL's GenerateMipmaps contract requires (SAMPLER-only worked only because the Vulkan path needs just transfer bits and the device disables SDL debug mode). Mip filter: linear, min/mag per `filter_linearN`. Regression test: `tools/audit/test_mipmap_plan.cpp` (32 checks). Deliberate divergence: history/feedback/alias bindings keep the nearest/clamp default sampler (§5.4) and never consume mips.
3. ✅ **Parameter override case quirk** — FIXED (`6f13d787f`): `preset_runner.cpp` applies overrides via `CaseInsensitiveEqual`; the parser's lowercased keys now land regardless of the `#pragma` spelling. Was: mixed-case parameters (Mega Bezel `.params` overlays) silently kept pragma defaults.
4. ✅ **`PassFeedback#Size` latent crash** — FIXED (`6f13d787f`): length-aware index parse in `FillBlockBytes` (plus the same-class guard on the translator's uniform `[N]` size parse, which previously aborted on non-numeric sizes instead of erroring loudly). Regression test: `tools/audit/test_uniform_names.cpp`.
5. ✅ **`Rotation` semantic missing** — FIXED (2026-10-14, gap-#5 round): `slang_translate.cpp` whitelists the member and `FillBlockBytes` fills the identity value 0 (spec semantic: uint 0..3 = 0/90/180/270 degrees; this frontend has no rotation source). Not 0-corpus after all: 16 corpus shaders declare `uniform uint Rotation;` (crt-effects, authentic_gbc, border fills, subframe BFI) and every one of them previously failed translation loudly — the earlier "rare" estimate undercounted because the audit CSV never had a rotation column. Regression test: `tools/audit/test_rotation_format.cpp`.
6. ✅ **`#pragma format` token gaps** — FIXED (2026-10-14, gap-#6 round): `preset_runner.cpp` maps the two-channel integer tokens (`R8G8`/`R16G16`/`R32G32` × `UINT`/`SINT`) and `A2B10G10R10_UNORM_PACK32` (SDL GPU's `R10G10B10A2_UNORM`; 18 corpus shaders request it). The `_UINT_PACK32` variant remains deliberately unmapped — SDL GPU exposes no UINT form of the packed 10-bit layout (0 corpus hits) — and keeps the warn + RGBA8 fallback contract. Regression test: `tools/audit/test_rotation_format.cpp`.
7. **`clamp_to_border` wrap mode** — `preset_parser.cpp` maps it to clamp-to-edge; SDL3 GPU can express border clamping (`SDL_GPU_SAMPLERADDRESSMODE_CLAMP_TO_BORDER`) if a preset ever needs it. Unrepresented in the corpus.
8. **`OriginalHistory` cap 8** — `slang_translate.cpp` rejects > 8 (spec unbounded, corpus max 7). Raise only on evidence.
9. **Hard-error translator paths, zero corpus exposure (T2–T5)** — vertex-stage samplers, sampler arrays, > 1 plain UBO, unsized arrays. Leave as loud errors; they flag genuinely unsupported shaders.
10. **`#pragma alias` / `#pragma include_optional` / `#error` skip** — ignored/unresolved/compile-fail (4.2). Add only if a pack needs them.
11. **`FrameDirection` constant +1** — correct for this game (no rewind); keep.
12. ✅ **Case asymmetries (F9/T9)** — FIXED (2026-10-14, field round 1): not 0-corpus after all — preset-level `alias0 = Pass0` / `alias3 = PrePass0` (motion_interpolation, guest advanced ntsc) failed deduction because the runner inserted aliases/#pragma names in original case while the translator's deduction looks references up lowercased (`hqx_refpass` only ever worked because it is already lowercase). Names now go into the known set in both forms, and the runtime binds (alias, `#pragma name` identifier, `<name>Feedback` form, LUT) all use `CaseInsensitiveEqual`. `<Tex>Size` validation stays strict (0 hits).
13. **`#reference` multi-chain order** — code: last non-empty parent chain wins; comment says first. Fix the comment (or make it first-wins) before any preset relies on order.
14. **`feedback_pass` / `histories` parsed-unused (F2)** — intentional; re-evaluate only when spec-conformance testing starts.
15. ✅ **`#pragma parameter` lines with trailing comments silently dropped** — FIXED (2026-10-14, field round 1): the pragma regex required end-of-line after the step value, so the 58 corpus pragma lines carrying a trailing `//` (or `/* */`) comment were dropped and their UBO members left unvalidated (field: scanlines-Modern lost `densY`/`densX`/`colAtten`). The regex now accepts a trailing comment, matching RetroArch's sscanf-style parse. Regression test: `tools/audit/test_field_round1.cpp`.
16. ✅ **Comma-separated UBO declarators glue into one bogus member** — FIXED (2026-10-14, field round 1): the whitespace tokenizer handed a no-space list (`float sc_sat,sc_black,...`) over as ONE token and the old comma-erase GLUED the names (`sc_satsc_blacksc_...`), which then matched no parameter. 34+ corpus lines affected (field: simple_color_controls, anime4k, ossc). Declarators are now exploded on commas before std140 layout. Regression test: `tools/audit/test_field_round1.cpp`.
17. ✅ **Unknown UBO members hard-failed translation** — FIXED (2026-10-14, field round 1): the member-validation gate rejected any member that is not a builtin, size form or parameter — but RetroArch leaves unknown members at 0. Field log: `auto_res` (5 ntsc-adaptive presets), `XBR_ANTI_RINGING` (super-xbr), `ia_FLIP_HORZ` (glass) — 10+ real presets failed that RetroArch loads. Now: warning at load + zero-fill (4.6). Structural errors stay hard. Regression test: `tools/audit/test_field_round1.cpp`.

## 8. Cross-references

- `docs/GPU_SHADERS.md` — user-facing runtime-preset scaler guide (rewritten for the runtime scope).
- `docs/audit/shader-support-audit-2026-10-14.md` — static audit: F5/P1 evidence and patches, F1/F2/F6/F8/F9/T2–T5/T9 disposition, updated phase plan.
- `docs/audit/slang_audit_v2.csv`, `docs/audit/vulkan-audit-codefindings-affected.csv`, `docs/audit/slang-codefindings-scan.csv` — per-preset feature flags and finding exposure (2658 presets).
- `tools/audit_slang_presets.py`, `tools/audit/*.py` — audit tooling (re-run after every fix; diff CSVs run-to-run).
- `docs/diablo-handoff.md` — mission state, field history, gotchas (§9), coverage plan (§12).
- Spec basis: libretro docs "Slang Shader Development" page; RetroArch `gfx/video_shader_parse.c` (preset grammar reference implementation); libretro/slang-shaders corpus (GPLv3 — verify per-shader headers before bundling anything).
