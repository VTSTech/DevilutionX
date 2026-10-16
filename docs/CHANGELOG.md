# Changelog

All notable changes to this project will be documented in this file.

The format is based on [Keep a Changelog](https://keepachangelog.com/en/1.0.0/)
and this project adheres to [Semantic Versioning](https://semver.org/spec/v2.0.0.html).

Fork releases are numbered `1.6.0-dev-<commit>`, where `<commit>` is the
short hash of the commit a build or overlay was produced from, and are
dated with an America/Toronto timestamp. Upstream (diasurgical) release
history lives in [docs/CHANGELOG_UPSTREAM.md](docs/CHANGELOG_UPSTREAM.md)
and is not modified.


## 1.6.0-dev-1f7d111fd (2026-10-16 3:39:59 PM)

The window that closes the Shaders → 'Off' field CTD. Five code commits shipped
between the round-8 patch and this build — the round-9 load-path hardening
(ROB-20/21), a ROB-19 rider for the first field report, the Linux CI headless-GPU
fix, and the two fixes that actually ended the crash (ROB-26, ROB-27) — and the
field now confirms it: switching Shaders to Off with a preset active no longer
terminates the process (verified on this build, 2026-10-17). The same window gave
Linux CI its first real headless GPU test coverage, and the fork took its first
upstream sync through a true merge commit (PR #1 from diasurgical/master).

### Fixed

#### GPU scaler load path (round 9)

- **ROB-20** — every untrusted read on the preset load path is capped *before* the
  slurp: `ParsePresetImpl` refuses presets above 8 MiB, `ResolveIncludes` refuses
  the top-level shader and every include above 4 MiB, and the diagnostics
  fingerprint reader applies the same cap and simply skips the fingerprint on
  oversize input; unseekable streams are refused with their own message. Both
  readers previously slurped whole files with no guard anywhere upstream on the
  load path, so a planted multi-gigabyte file terminated the process via uncaught
  `bad_alloc` instead of returning the documented error contract. The
  int-overflow class closes structurally (both diagnostics size casts go through a
  clamping `DiagSizeToInt` helper), and the missing top-level guard now exists:
  `ParsePreset`/`TranslateSlang` wrap their bodies in a catch-all that converts
  any escaping exception — `bad_alloc` included — into the documented failure
  contract (`false` plus a named error, `out` left in the early-failure state).
  The caps sit orders of magnitude above the real corpus (the largest known
  `.slang` is ~130 KiB), so no legitimate pack changes behavior.
- **ROB-21** — `RewriteSamplers` name-dedupes the plain rewrite path immediately
  before the emit. Include-once is tracked *per bucket* (shared/vertex/fragment),
  so a file included once in the shared region and again in the fragment region
  pasted its stage-guarded sampler declaration into the fragment TU twice — valid
  input producing two dense bindings for one sampler: a glslang "redefinition"
  compile failure with misleading diagnostics, plus double-counted
  `stage.textures` reserving two sampler slots for one texture. The repeat
  declaration is now dropped (first binding wins — it is visible everywhere below
  in the same TU) with a named warning recorded; the alias-branch dedupe composes
  unchanged and every other validation runs as before. The smoke corpus gained
  `dupinclude.slangp` (+ two shaders) exercising exactly that re-inclusion, and
  `preset_failure_modes` grew from 37 to 41 checks under the new caps.

#### GPU scaler — Shaders → 'Off' CTD (field-verified closed on this build)

- **ROB-19 rider** — the first field report on the round-8 stack:
  `RecreateMainRenderWindow`'s create-before-destroy order created the replacement
  window *visible* while the old one still owned display, focus and mouse grab —
  a two-visible-windows moment the original destroy-then-create order never had.
  The replacement is now created `SDL_WINDOW_HIDDEN`, the old window destroyed,
  then shown and raised — a single visible transition, keeping ROB-19's
  no-window-loss guarantee. Correct for its own finding, though (per ROB-26's
  record below) not what the field crash ultimately turned on.
- **ROB-26** — the hand-back no longer tears the GPU device down inside the
  settings switch. The field bisection that framed the hunt ("d5ca0c0 good") was a
  false negative: SDL3-GPU's backend priority on Windows is D3D12 before Vulkan,
  and at d5ca0c0 the `CreateDevice` whitelist rejected the auto-selected d3d12, so
  the scaler never activated there and the 'Off' switch exercised nothing but the
  renderer-only path. ROB-18's forced-Vulkan retry made the scaler work on that
  machine for the first time — which made the never-once-executed hand-back stack
  reachable: synchronous `SDL_ReleaseWindowFromGPUDevice` + `SDL_DestroyGPUDevice`
  (full Vulkan teardown) microseconds before a window recreation and a D3D11
  reclaim, all inside one call frame. New `gpu_scaler::HandbackWindow(window)` does
  everything `Shutdown()` does **except** destroy the device: frame/filter
  resources, the preset runner, the samplers and the window claim are released
  explicitly while the window is still alive (SDL_gpu has no window-destroy
  auto-release hook), `IsActive()` flips false immediately so every game path
  falls back to the regular renderer at once, and the parked, resource-less device
  is destroyed by the next `Shutdown()` (quit / `dx_cleanup`) or transparently
  reused by the next `Reinit()` (re-selecting a shader re-claims a fresh window on
  it). `gpu-scaler.log` brackets the remaining GPU call (`HandbackWindow: ...`) so
  any residual failure names its frame.
- **ROB-27** — the crash ROB-26's build still hit: a null item-cursor CLX read
  when the switch runs in the main menu. The regular-renderer branch of
  `ReinitializeRenderer` re-runs `InitVirtualGamepadGFX()` (the ROB-22 pair) →
  `LoadPotionArt()` → `GetInvItemSize(CURSOR_FIRSTITEM + ICURS_POTION_OF_HEALING)`
  → `pCursCels->numSprites()`, but `pCursCels` (`data\inv\objcurs`) is loaded by
  `InitCursor()` at game-session start and freed by `FreeCursor()` — empty in the
  menu, so the read is `LoadLE32(nullptr)`: an instant, silent access violation in
  Release. Shader options are CantChangeInGame, so the menu is the only context
  this path executes in, and sessions where the scaler never activated never reach
  it (the startup renderer exists, so the branch is skipped) — which is why the
  field crash only appeared once ROB-18 made the scaler work on Windows, and why
  the diagnostic log stopped exactly after `Created renderer: direct3d11`. Two
  guards: `GetInvItemSize()` (`Source/cursor.cpp`) returns `{0, 0}` when
  `pCursCels` is not loaded, and `LoadPotionArt()`
  (`Source/controls/touch/renderers.cpp`) skips the potion strip on a zero size —
  the strip is never rendered in menus, and the game-load path (`LoadAllGFX`)
  rebuilds it exactly where the engine always did. In-game behavior is
  byte-identical.

#### Tests / CI

- `preset_unload_test` now actually runs on Linux CI instead of failing. Headless
  containers have no video device, and SDL3 (unlike SDL2) never auto-selects the
  dummy video driver — whose driver, in turn, has no Vulkan hooks at all, so
  `SDL_VIDEODRIVER=dummy` is not a viable fallback for `SDL_CreateGPUDevice`. The
  harness gained a clean skip tier for `SDL_Init(SDL_INIT_VIDEO)` failure (exit 0,
  loud SKIP line — same tier as the existing no-device/no-corpus skips), and the
  Linux job runs the suite under `xvfb-run` with `SDL_VIDEODRIVER=x11` plus
  `libvulkan1`/`mesa-vulkan-drivers` (bookworm's lavapipe ICD qualifies; SDL_gpu
  requires only Vulkan 1.0 core). A coverage guard re-runs `preset_unload`
  verbosely and fails the job unless it ends with "ALL OK", so a silently-skipping
  test can no longer leave the suite green with zero GPU lifecycle coverage. The
  harness also gained round 4: menu-context `InitVirtualGamepadGFX()` /
  `FreeVirtualGamepadGFX()` with `pCursCels` empty — the exact ROB-27 sequence,
  which kills the process pre-fix and passes asset-free post-fix.

### Added

#### Upstream sync (PR #1 — first true merge commit)

- Merged `diasurgical/master` inbound. Upstream was exactly one commit ahead of
  the 2026-10-11 sync base: 94433061f (staphen, "Free pointer returned by
  SDL_GetDisplays()"). Net effect on the fork tree is a single line —
  `SDL_free(displays)` after the desktop display mode is read in
  `CalculatePreferredWindowSize` — landing cleanly beside the fork's existing
  `ErrSdl()` handling. `Source/engine/render/gpu/` is untouched and no conflicts
  surfaced.

### Changed

#### Audit ledger / documentation

- [deltas.md](deltas.md) gains the closure records for ROB-20/21 (round 9) and
  the field regressions ROB-26/ROB-27, plus the ROB-19 field-regression addendum
  (mechanism, fix, plan-B note). The audit.md open-list re-head for the field
  closures is still pending — and note the field records' ROB-26 ID collides with
  an existing open Low finding of the same number (the swapchain-acquire vs
  minimized-window-skip finding); renumber at the next re-head. Open ledger per
  the round-9 re-head: **40 findings — 0 High / 9 Medium / 31 Low**.
- `docs/audit/vulkan-coverage-updated.csv` refreshed (+42/−15).

## 1.6.0-dev-bbb0a9349 (2026-10-16 12:20:03 AM)

Round 8 of the audit shipped the three presentation-surface Mediums that opened the
short-term queue — ROB-18, ROB-19, ROB-22 — as one bounded patch, cutting the open
ledger from 44 to 41 findings (**0 High / 11 Medium / 30 Low**). The rest of the
window is planning and documentation: the upstream merge plan went out as drafts r1
and r2 (8-PR ladder with Phase-0 audit gates, shared with the diasurgical
maintainers, reply pending), the Vulkan field-sweep coverage landed as a committed
CSV, and the maintainer handoff was consolidated. The Linux CI test-geometry fix
(r7.6.2) also landed inside this window and carries its own full entry below
(`1.6.0-dev-a0cbeccb6`).

### Fixed

#### GPU scaler

- **ROB-18** — an unsupported *auto-selected* GPU backend no longer hard-fails the
  session: `CreateDevice` now retries once with the explicit `"vulkan"` backend
  whenever its driver whitelist rejects the runtime's pick (on Windows, SDL's
  default selection order can yield `d3d12` — a SPIR-V-declaring backend per SDL —
  which previously left the scaler dead for the whole session unless
  `DEVILUTIONX_GPU_BACKEND=vulkan` was set by hand). The rejected device is
  destroyed and the property set rebuilt by a new `BuildDeviceProperties` helper
  (one shared implementation for both attempts), and the session continues on
  Vulkan with SPIR-V shaders. An explicit `DEVILUTIONX_GPU_BACKEND` value or
  forced name is still honored as-is (no auto-retry — the override exists to
  troubleshoot one specific backend), with a log line saying so, and a
  belt-and-braces check covers an explicitly named backend ever coming back
  unlisted. The old double-log is gone: `CreateDevice` no longer calls
  `LogFailHard`/`Shutdown` itself; only the caller records the hard failure, once.
  Success-path behavior for vulkan/metal-selected devices is byte-identical.

#### Window / renderer lifecycle

- **ROB-19** — `RecreateMainRenderWindow` creates the replacement window *before*
  destroying the old one, so a failed `SDL_CreateWindow` on the fallback-of-the-
  fallback path (GPU-failure fallback in `RenderPresent` → `ReinitializeRenderer`)
  keeps the existing window alive, logs the SDL error, and returns `false` instead
  of taking the game down through a fatal `ErrSdl()` — followed, previously, by a
  second one from `SDL_GetWindowSurface(nullptr)` on the next frame. `ghMainWnd`
  can therefore never be left null by the function, both callers continue with a
  live window on the stock renderer path, and the game only dies when it is left
  with no window at all. The success path is order-swapped but otherwise identical.

#### Virtual gamepad (touch)

- **ROB-22** — the second half of the ROB-14 use-after-free family: virtual-gamepad
  textures no longer dangle across scaler activation. The scaler-activation branch
  now calls `FreeVirtualGamepadTextures()` immediately before
  `SDL_DestroyRenderer(renderer)` (mirroring `dx_cleanup`'s ordering and the ROB-14
  `texture` handling), so the handles are destroyed while their renderer is still
  alive instead of the next `FreeVirtualGamepadTextures()` running
  `SDL_DestroyTexture` on freed memory. And the renderer-creation site in
  `ReinitializeRenderer` re-runs `InitVirtualGamepadGFX()` +
  `InitVirtualGamepadTextures(*renderer)` after every fresh renderer, so the
  GPU-failure fallback renders the gamepad against textures created from the
  *current* renderer — with the art reload covering the common case where
  `createTextures` already consumed the source surfaces (the extra calls are no-ops
  in the steady state). Filter toggles and fallbacks now restore the touch UI
  instead of leaving dangling handles or a permanently missing gamepad; desktop
  builds are unaffected (the virtual gamepad is never created there).

### Added

#### Upstream merge track (planning — nothing scaffolded, no PRs opened)

- `docs/merge-plan-draft-r1.md` and `docs/merge-plan-draft-r2.md` (r2 supersedes
  r1): a plan-first, phased 8-PR ladder for carrying the shader-preset pipeline
  toward `diasurgical/DevilutionX`, answering AJenbo's merge conditions with facts
  (no SDL patching — SDL3 core byte-identical; functional PRs one at a time). r2
  adds the audit-closure gate, per-PR readiness gates, and a noise-control policy:
  every proposed PR is gated on the open findings that share its files, with
  closure ordered into three Phase-0 sweeps — **8A** library surface (ROB-20/21,
  PERF-06, MAINT-15, plus the `gpu/preset/*` cluster), **8B** presentation surface
  (ROB-08, FEAT-02 disposition, ROB-26, MAINT-10, PERF-01/03 — ROB-18/19/22 closed
  this window), **8C** runner surface (ROB-07/23, TEST-06, ROB-10/11, MAINT-17,
  PERF-02) — and first contact (PR 4 + PR 1) barred until 8A+8B close 10 of the
  open Mediums. The plan was shared with the upstream maintainers on October 16,
  2026 and is awaiting their reply; whether or not any PR ever merges, the
  long-term goal is unchanged — full audit closure, then a standalone reusable
  shader-preset library (handoff §14).

#### Field-sweep coverage

- `docs/audit/vulkan-coverage-2026-10-15.csv`: per-preset Vulkan coverage from the
  field sweep, committed alongside updates to
  `docs/audit/shader-test-list-2026-10-15.md` — pass counts, load/cache timings,
  dwell times, and the first mipmap + live `PassFeedback` confirmations
  (`reshade/magicbloom.slangp`: mip chains on pass outputs 0–10, adapt-ring
  feedback, 12 passes).

### Changed

#### Audit ledger

- `audit.md` re-headed to round 8; ROB-18/19/22 move from the open Medium list to
  [deltas.md](deltas.md) with full closure records (verification: both touched TUs
  clean under `clang-format 18.1.8`, the CI-pinned version, and warning-free under
  the project's Release flags). Open ledger: **41 findings — 0 High / 11 Medium /
  30 Low**.

#### Documentation

- `docs/diablo-handoff.md` consolidated (net −728 lines), restructured around the
  merge track (§14); README and `docs/brief.md` updated for the merge-track
  addendum; `docs/SLANG_SPEC.md` gap #13 (`#reference` multi-chain order) marked
  resolved — comment-side fix (`ca02abf856`), last-wins behavior documented, no
  corpus preset relies on the order.

## 1.6.0-dev-a0cbeccb6 (2026-10-15 7:47:42 PM)

Hotfix on top of the round-7 audit patch stack: the Linux CI test job's first
real execution of the preset smoke corpus (shadowed since it landed by the
r7.5 compile break, which died before the Test step, and unblocked by the
r7.6 void-API fix) failed `preset_smoke_corpus`.

### Fixed

#### Tests

- `preset_smoke_corpus` is green again: the `preset_smoke` test binary now
  builds into the build root (`RUNTIME_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}`),
  matching the convention of every other test executable. `SDL_GetBasePath()`
  follows the binary's location, so from `build/Source/` the SEC-01 shader
  root resolved to `build/Source/shaders` and the corpus copied to
  `build/shaders/.preset-smoke/` fell outside it — every good-corpus preset
  failed at translate with "cannot open shader file" (ctest exit 8, one test
  in the FAILED list). `preset_smoke_malformed` was and stays green: its
  `--expect-fail` contract is that every preset fails cleanly with a
  non-empty error, which the sandbox breakage still satisfied. Corpus,
  fixtures, harness, and the SEC-01 sandbox are unchanged.

## 1.6.0-dev-98357ff (2026-10-15 7:01:14 PM)

Round 7 of the audit re-walked the entire mod with fresh eyes - scaler/present
lifecycle, parser/translator, runner/compiler, tests/CI - and produced 25 new
findings (2 High, 11 Medium, 12 Low) plus a reopened MAINT-03. The Priority
Matrix's entire Immediate row shipped as one low-risk patch the same day,
closing both High findings and the unchecked-SDL-return cluster behind them;
the vendored-SDL3 build break (SDL >= 3.2 made `SDL_UploadToGPUTexture` return
`void`) and the CI clang-format gate were fixed on top of it. The WAV-fallback
diagnostics learned to name the failing asset and the MPQ archive it came
from. The ledger stands at 44 open findings (0 High / 14 Medium / 30 Low);
`deltas.md` remains the single record of closures.

### Added

#### Audio

- `DEVILUTIONX_WAV_FALLBACK` CMake option: builds the `SDL_LoadWAV_IO`
  fallback path that recovers the WAV files SDL3_mixer 3.2.0's stricter chunk
  scanner rejects (zero-length or past-EOF data-chunk sizes in the tail).

#### Testing

- `preset_smoke` is registered with ctest against a committed good + malformed
  corpus (multi-pass/feedback/history/LUT presets; malformed presets must fail
  with a named error string, never a crash) - the round-5 fix that closed the
  audit's last High finding. It landed inside this release window and carries
  its own full entry above (`1.6.0-dev-a67a4c0`).

#### Packaging

- `Packaging/windows/clear_gpu-scaler.log.cmd`: one-click reset of
  `gpu-scaler.log` next to the game executable for Windows field reports.

### Changed

#### Audit ledger

- `audit.md` refreshed by the round-7 sweep (+25 findings, MAINT-03 reopened),
  then re-scoped to the open set only (44 findings: 0 High / 14 Medium /
  30 Low) after the quick-fix batch; `deltas.md` gained the closure records
  for everything shipped in this window.

### Removed

- `.github/workflows/cache-cleanup.yml`, `.github/workflows/`
  `clang-tidy-check.yml`, `.github/workflows/translations.yml` - the MAINT-03
  zombie workflows, still pinned to the nonexistent `actions/checkout@v7`,
  actually deleted from the tree this time and verified with `git ls-tree`.
- `.github/workflows/wav-fallback-test.yml` - the Windows-only A/B workflow
  built to capture the fallback mismatch, retired once the diagnostics it
  drove had landed (added and removed inside this release window).

### Fixed

#### GPU scaler (round-7 quick-fix batch)

- **ROB-14 (High)**: activating the shader filter no longer destroys the
  SDL_Renderer while the global display `texture` still references it - the
  stale handle is dropped first, mirroring `dx_cleanup`, so toggling the
  filter can no longer hand a freed texture to the next
  `ReinitializeTexture()` (the virtual-gamepad half of the finding remains
  open as ROB-22 by design).
- **ROB-15 (High)**: `Runner::Run` captures the present target BEFORE the
  double-buffer flip - feedback-driven final passes no longer present a
  permanently one-frame-stale image plus one undefined frame after every
  load/resize.
- **ROB-16**: a failed `SDL_MapGPUTransferBuffer` (device loss, allocation
  failure) now costs one skipped frame like every neighboring stage instead
  of memcpying to NULL outside the failure contract.
- **ROB-17**: `SDL_BeginGPUCopyPass` is null-checked (fail visibly + empty
  submit instead of blitting stale contents with a clean log). The
  upload-result half was reverted in an r7.6 fixup: vendored SDL3 (master,
  post-3.4.0) follows the SDL >= 3.2 API where command-encoded copy calls
  return `void` - per-call failure reporting was removed upstream, the bool
  capture broke the CI build, and the checked
  `SDL_SubmitGPUCommandBuffer` is the failure net.
- **ROB-24**: a failed Vulkan-retry window claim now nulls `gWindow`, so the
  `Shutdown()` path cannot release a window this device never claimed.
- **ROB-25**: an unexpected (non-custom) filter value is now counted and
  throttled-logged as a present-path skip instead of presenting a permanent
  silent black screen with a healthy heartbeat.

#### Audio / WAV fallback

- Load-failure diagnostics name the asset: `SoundSample::SetChunk` carries the
  asset path and - for MPQ-sourced assets - the archive path (exposed via
  `mpq_reader`'s new `path()`), so a rejection logs
  `Failed to decode audio file / <path> (<archive>) / <SDL_GetError>` instead
  of a bare SDL error, alongside the 56-byte head hex dump from the previous
  round.

#### Parser

- MAINT-09: the `#reference` merge comment now states the real semantics
  (the LAST parent chain providing passes wins; textures/parameters
  accumulate).
- MAINT-19 (partial): texture validation names the actual problem (empty-name
  vs missing-path) instead of sending field debugging the wrong way, and the
  dead 4-argument `ParsePreset` declaration is gone. The bare `shaderpath`
  behavior decision remains open with the maintainer.

#### CI / build hygiene

- The CI build break with vendored SDL3 master (`void value not ignored`,
  `SDL_UploadToGPUTexture` bool capture) is fixed - see ROB-17.
- The `clang-format check` workflow is green for the first time against the
  mod: `gpu_scaler.cpp` had shipped with a whole-file indentation regression
  from the previous batch's tooling (whitespace-only, compiled fine); the
  canonical `Source/.clang-format` formatting is restored and the file's diff
  collapses back to the surgical change set, and 12 pre-existing violations in
  `soundsample.cpp` are fixed. Both files are token-identical to their
  pre-format state (verified by whitespace-stripped byte diff); all 698
  C/C++ files under the CI's check-paths now pass.

## 1.6.0-dev-a67a4c0 (2026-10-15 3:20:26 PM)

Codebase audit fixes, round 5 — TEST-01 closes the audit's last High
finding: the fork's ~15K-line GPU pipeline now has a registered,
CI-executed, full-pipeline test. The offline `preset_smoke` harness (parse →
translate → in-process glslang compile, no GPU device) builds with the normal
test suite instead of sitting behind `EXCLUDE_FROM_ALL` where nothing ever
ran it, and executes against a committed micro-corpus covering the features
real preset packs use — multi-pass chains with alias feedback, `PassOutput`
feedback, history-ring taps, LUT textures — plus a malformed corpus that
asserts the failure contract at every pipeline stage: absurd declared
counts, absurd pass indexes, empty files, missing shader files, and GLSL
syntax errors must all fail with a named error string, never a crash (the
ROB-01/ROB-02 classes, now regression-locked mechanically). Field
diagnostics also widen the WAV load-failure hex dump from 24 to 56 bytes:
every rejection captured so far (the gendata cutscene sting, 18355 bytes)
showed a perfectly canonical 24-byte head, because the malformation
SDL3_mixer chokes on — zero-length or past-EOF data-chunk sizes — sits at
offset 36, which only a 56-byte dump reveals. 20 of the original 47 findings
are now closed; `audit.md` carries only the open set (27 findings: 0 High,
5 Medium, 22 Low) and `deltas.md` remains the single record of closures.

### Added

#### Testing

- `preset_smoke` is built with the normal test suite (`EXCLUDE_FROM_ALL`
  dropped in `Source/CMakeLists.txt`) and registered with ctest in
  `CMake/Tests.cmake` whenever `USE_GPU_SCALER` and `BUILD_TESTING` are on:
  - `preset_smoke_corpus` runs the committed good corpus
    (`test/fixtures/preset_smoke/good`) — 5 presets, 8 shader passes that
    must parse, translate, and glslang-compile: single pass with a
    `#pragma parameter` consumed from the fragment UBO, 3-pass chain with
    `alias1 = blur` sampled from a later pass, explicit `PassOutput0`
    feedback, `OriginalHistory1/2` taps with `histories = 4`, and a LUT
    preset with a committed 2x2 PNG. All shaders follow the canonical
    `tools/audit/field-presets` style (explicit `layout(location = 0) out
    vec4 FragColor`, parameters inside the stage UBO per SLANG_SPEC 4.6).
  - `preset_smoke_malformed` runs the malformed corpus with the harness's
    new `--expect-fail` mode — 5 presets, one per pipeline stage, that must
    fail cleanly with a non-empty error string: `shaders = 2147483647`
    (ROB-02 class), `shader2147483647` (ROB-02 class), an empty file, a
    missing shader file (translate stage), and a GLSL syntax error (glslang
    compile stage). Failure-by-crash or failure-without-explanation fails
    the test, and a clean corpus under `--expect-fail` fails it too.
- The corpus is copied to `<build>/shaders/.preset-smoke/` at build time
  because `ResolvePresetFilePath` resolves references only inside the SEC-01
  shader root (`<SDL_GetBasePath()>/shaders`); the dot prefix keeps the
  corpus out of the in-game preset browser. The sandbox itself is untouched.
- `preset_smoke` usage string corrected while touching the harness (it
  advertised a nonexistent `--allow-failures` and omitted `--dump`/`--disasm`).

### Fixed

#### Diagnostics / audio

- `SoundSample::SetChunk`'s load-failure hex dump widened 24 → 56 bytes
  (`DEVILUTIONX_WAV_FALLBACK`-independent, still logged by every build):
  56 bytes covers the canonical 44-byte RIFF/WAVE/fmt head plus the
  data-chunk header at offset 36, where the zero-length / past-EOF data
  sizes that SDL3_mixer 3.2.0 rejects actually live.

### Documentation

- `audit.md`: TEST-01 removed from the open set (now 27 open: 0 High,
  5 Medium, 22 Low; 20 of 47 closed); priority-matrix Immediate row emptied
  with a pointer to the closure.
- `deltas.md`: full TEST-01 closure entry (resolution, corpus layout
  rationale, verification evidence).

### Verification

- Standalone local build of the harness + the four preset TUs + the real
  glslang 16.6.0 (the exact version vendored by `3rdParty/glslang`; SDL
  stubbed, shader root pointed at the corpus): good corpus **5/5 presets,
  8/8 passes compiled, exit 0**; malformed corpus under `--expect-fail`
  **met, 5/5 failed with named error strings, zero crashes, exit 0**; the
  good corpus under `--expect-fail` correctly exits 1.
- `soundsample.cpp` compiles clean in both `DEVILUTIONX_WAV_FALLBACK=ON`
  and `=OFF` configurations (gnu++23, `-Wall -Wextra`; only the
  pre-existing `sdl_wrap.h`/`options.h` `-Wextra` noise, identical in both).
- `preset_smoke.cpp` clang-format 18.1.8 clean.


## 1.6.0-dev-d5ca0c0 (2026-10-15 1:46:21 PM)

Codebase audit fixes, round 4 — the entire "next sprint" batch from the audit:
the shader-cache failure contract (atomic writes + toolchain-version salt),
three more robustness closures (scale clamp, diagnostics-log race, corrupt-ini
self-heal), the window-ownership ABA fix, release-asset CI, and the first
options-layer test registration (which immediately caught a real
startup-restore bug). Field result: shader presets now load right from the
title screen instead of requiring a trip through the shader menu first. 19 of
the original 47 findings are now closed; `audit.md` carries only the open set
(28 findings: 1 High, 5 Medium, 22 Low) and `deltas.md` remains the single
record of closures.

### Fixed

#### Graphics / GPU scaler

- Shader cache blobs can no longer be poisoned by a torn write: each
  `.vert.spv`/`.frag.spv` is written to a process-unique temp sibling
  (`<path>.<pid>.tmp`) and atomically renamed into place (existing target
  removed first for Windows), so a crash or kill mid-write can no longer leave
  a truncated blob that fails every later load; `CompilePass` gained an
  out-parameter form (`SpirvSource` = Compiled/Cache/Sidecar, plus
  `allowCache`) so callers know where each pass's SPIR-V came from, and a new
  `DeleteCachedSpv` removes both blobs of a pass. A GPU-rejected cache hit now
  self-heals: `Runner::Load` releases any shader built from the poisoned blob,
  deletes the cached pair, and recompiles once with the cache disabled instead
  of failing the preset until the user manually wipes `shaders/cache/` (ROB-03)
- The disk-cache key is salted with the toolchain version: the hashed payload
  now mixes in `kSpirvCacheVersion` plus the glslang version macros
  (`GLSLANG_VERSION_MAJOR/MINOR/PATCH`, guarded for exotic builds), and cache
  file names carry a `-v<version>-` segment so a future bump can also prune by
  name; upgrading glslang no longer silently serves stale SPIR-V compiled by
  the previous toolchain (ROB-04)
- Resolved pass-target dimensions are clamped to [1, 16384] before the
  float→Uint32 cast (`ClampPassTargetDim`, NaN → 1), each clamp logged with its
  pass index, and the clamped values feed the prevW/prevH chain; a preset with
  absurd or NaN `target_size` values can no longer trigger UB in the conversion
  or request a multi-gigabyte framebuffer (ROB-05)
- Swapchain window ownership is explicit state keyed by the SDL window ID
  instead of a `SDL_Window*` compare: SDL window IDs are never reused, so the
  old `gLastClaimedWindow` dangling-pointer ABA (destroy + recreate landing on
  the same address, misread as "same window") can no longer hand the swapchain
  to the wrong window; the three-participant handoff protocol (gpu_scaler /
  display.cpp / dx.cpp) is documented in one comment (ARCH-01)

#### Configuration

- A corrupt `diablo.ini` no longer terminates the game at startup: the
  unreadable file is preserved as `diablo.ini.corrupt-<YYYYMMDD-HHMMSS>`, the
  parse failure is logged, and defaults load via the empty-buffer parse path;
  the fatal error remains only if preservation itself fails, so a bad write can
  cost the user their settings but never their config file or their session
  (ROB-09)

#### Options menu

- Fixed an undocumented startup-restore bug found by the new tests:
  `GraphicsOptions::GetEntries()` listed `shaderFilter` before
  `shaderFilterPreset`, and the preset entry re-resolves its path at Rescan
  time — which is empty at startup — so every restart sanitized a saved custom
  preset back to Off, and it only re-activated after the shader menu was
  opened; the (invisible) preset-path entry now loads first and saved custom
  presets survive restarts — shader presets load right from the title screen

#### CI

- Built binaries now land on the GitHub Release instead of only 90-day
  workflow artifacts: both `gpu-shaders.yml` jobs attach their package zips via
  `softprops/action-gh-release@v2`, conditioned on the `release` event, with
  job-level `permissions: contents: write` for the default token (MAINT-04)

### Testing

- New standalone gtest `options_test` (7 cases) registered in
  `CMake/Tests.cmake` under the `standalone_tests` pattern (links
  `libdevilutionx_options` + the preset TUs + `libdevilutionx_assets`, same as
  `dun_render_benchmark`): stale builtin-filter sanitize (1..99), custom
  shader-preset index mapping, missing-preset self-heal, ini round-trip, and
  two corrupt-ini recovery cases (TEST-03). Verification evidence for the
  round: options_test 7/7 pass headless, all 5 touched TUs compile clean with
  project flags (gnu++23, -Wall -Wextra), clang-format 18.1.8 clean

### Documentation

- Ledger: 8 closure entries added to `deltas.md` (ROB-03, ROB-04, ROB-05,
  ROB-06, ROB-09, MAINT-04, ARCH-01, TEST-03) plus the bonus fix; `audit.md`
  master table and section bodies updated, next-sprint matrix row retired —
  open set now 1 High (TEST-01), 5 Medium (ROB-07, ROB-08, FEAT-01, FEAT-02,
  FEAT-03), 22 Low

## 1.6.0-dev-c37e75a (2026-10-15 6:12:02 AM)

Codebase audit fixes, round 3 — CI repair on the `gpu-shaders.yml` pipeline plus
four more audit closures: both Security findings, Hellfire packaging, and
`gpu_scaler.cpp` hygiene. 11 of the original 47 findings are now closed;
`audit.md` carries only the open set (36 findings: 1 High, 13 Medium, 22 Low)
and `deltas.md` remains the single record of closures.

### Fixed

#### Security

- Preset references can no longer escape the shaders root (arbitrary file read
  via preset packs): new `preset::IsPathInsideShaderRoot()` runs the candidate
  and the shader root through `std::filesystem::weakly_canonical` (symlinks
  resolved) and requires the result to sit at or below the root, with a
  separator-boundary check so `shaders-ng` cannot pass a `shaders` prefix test
  (case-insensitive compare for Windows/macOS). `ResolvePresetFilePath` refuses
  absolute references and ancestor-walk hits that canonicalize outside the root,
  the slang `#include` resolver fails translation with "include target outside
  the shaders root: <path>", and the runner names the refused reference in
  gpu-scaler.log so refusals are diagnosable in the field (SEC-01)
- gpu-scaler.log is no longer an injection target: new header
  `gpu_log_sanitize.hpp` (`gpu_scaler::SanitizeLogLine()`) replaces every control
  byte (< 0x20, including `\n` `\r` `\t` ESC, and 0x7F DEL) with `?` before
  `LogFileLine` writes, so hostile preset names, `#pragma` descriptions, and
  glslang info-log text cannot forge log lines or spray terminal escapes;
  printable text and `%` signs pass through untouched (SEC-04)

#### CI

- `gpu-shaders.yml` Linux job installs dependencies again: the `apt-get install`
  continuation line ended in a doubled `\` (`ccache \\`), so the shell received a
  literal `\` as a package name and apt aborted with "E: Unable to locate
  package \" — back to a single continuation backslash
- Windows builds are static again: two `#` comment lines sat inside the folded
  `run: >` scalar of the Configure step; YAML folds comment lines into the shell
  command, where `#` starts a comment and silently truncated every argument after
  `-DCMAKE_CXX_COMPILER_LAUNCHER=ccache` — the
  `-DCMAKE_EXE_LINKER_FLAGS='-static -static-libgcc -static-libstdc++'` flags
  never reached CMake, the exe linked `libgcc_s_seh-1.dll` / `libstdc++-6.dll`,
  and the DLL-import gate failed. Comments moved above the step and a warning
  note about the folded-scalar `#` trap added

#### Packaging

- CI release zips now include Hellfire: both Package steps (Linux and Windows)
  copy `build/mods/hf.mpq` into `pkg/mods/`, existence-guarded so configurations
  that did not build the expansion MPQ still package cleanly; shipped builds are
  no longer silently Diablo-only (MAINT-05)

#### Graphics / GPU scaler

- `gFragmentShaders` is sized by a named constant (`kMaxFragmentPrograms =
  static_cast<int>(Program::Blit) + 1`) instead of the stale magic number 9 left
  over from the removed built-in filter set, so resizing the `Program` enum trips
  a compile error at the table instead of silently changing its bounds; the dead
  `case None:` switch in `FilterDisplayName` and the unused `<csetjmp>` include
  are gone (MAINT-07)

### Testing

- `preset_failure_modes` grew 20 → 37 checks: relative and absolute `#include`
  escapes refused while an in-root include still translates; `../../`, absolute,
  and sibling-prefix (`shaders-ng`) preset-reference refusals with an in-root
  reference still resolving; an escaping `#reference` failing the whole preset
  with a traceable error; and 6 log-sanitizer checks. The harness sandbox is
  re-rooted at a working-directory fake `SDL_GetBasePath` so containment staging
  works headless

## 1.6.0-dev-caee40e (2026-10-15 5:08:04 AM)

Codebase audit fixes, round 2 — CI hygiene and the test-suite revival, plus
the audit ledger split (`deltas.md` now records closed findings).

### Added

#### Testing

- New headless ctest `preset_failure_modes`: the hostile-input regression harness for the preset parser / slang translator (non-numeric and 20-digit texture indices, malformed `#pragma parameter` numbers, absurd `shaders` counts and `shaderN` indices, valid-preset regression guards). Compiles only the four parser/translator TUs — no SDL, no GPU — so it runs on any CI box (TEST-01, partial)
- The full gtest suite now runs in CI: the Linux job builds with `BUILD_TESTING=ON` (vendored googletest/benchmark) and executes `ctest --output-on-failure` (TEST-02)
- ccache is wired into both CI jobs (compiler launcher + `actions/cache` restore with per-job keys, 2 GB cap), and doc-only pushes (`**.md`, `docs/**`) no longer trigger the binary build pipeline (MAINT-01)

### Fixed

#### Build system

- `vcpkg.json` now defines the `sdl3` feature (`sdl3` + `sdl3-image`) that `VcPkgManifestFeatures.cmake` has been requesting since `USE_SDL3=ON` was introduced — vcpkg-based (MSVC) configures no longer abort during manifest install (MAINT-06)
- The test suite builds again under `BUILD_TESTING=ON`: standalone tests now link `GTest::gmock` when available (their `<gmock/gmock.h>` includes previously only resolved where system gtest headers sat on the default include path), and the four standalone test/benchmark targets that carry `libdevilutionx_options` compile the preset parser/translator TUs the fork's settings browser calls (TEST-02)
- `UITest`-based fixtures no longer segfault when MPQ fixtures are absent: `GTEST_SKIP()` in the base `SetUp()` is a plain `return`, so derived `SetUp()` overrides continued into uninitialized engine state (null `MyPlayer`, unloaded quest data) and crashed all 65 cases across `inventory_ui_test` / `panel_state_test` / `spell_ui_test`; derived setups now bail via `UITest::MissingAssets()` and `TearDown()` guards its cleanup (TEST-02). Suite status: 608/608 pass headless

#### CI

- `clang-format-check.yml` pins `actions/checkout@v4` (was the nonexistent `@v7`, which failed every run at checkout); the three other broken upstream workflows (`clang-tidy-check`, `translations`, `cache-cleanup`) and the one-shot `cleanup-fork-workflows.{sh,ps1}` scripts are removed (MAINT-03). `.github/workflows/` now contains exactly the curated pair: the format check and `gpu-shaders.yml`

### Documentation

- Audit ledger split: closed findings move from `audit.md` to `deltas.md` (ROB-01, ROB-02, MAINT-02, MAINT-01, MAINT-03, TEST-02, MAINT-06 so far); `audit.md` keeps the open set (40 findings: 1 High, 15 Medium, 24 Low) with CLOSED pointers in the master table

## 1.6.0-dev-1d9d84d (2026-10-15 3:26:39 AM)

Codebase audit fixes, round 1 — the two High robustness findings and the
dead-payload cleanup, from the audit in `audit.md`.

### Bug Fixes

#### Graphics / GPU scaler

- Malformed texture binding names no longer terminate the process mid-frame: a preset alias or sampler named `PassOutputx`, a bare `User`/`OriginalHistory`, or a 20-digit `OriginalHistory99999999999999` index used to reach `std::stoi` and abort through the uncaught exception; every builtin-name suffix is now parsed without throwing, malformed indices resolve to the closest valid binding, and the unresolved-texture guard (one-time log + current-input-frame fallback) now covers every binding branch, including out-of-range `PassOutput#`/`PassFeedback#` indices and unmatched `*Feedback` aliases that previously could bind null (ROB-01)
- Malformed `#pragma parameter` numbers (`.` or `-` alone, accepted by the pragma regex) no longer abort preset translation; values now parse with the RetroArch/atof contract — malformed tokens become 0 instead of a thrown `std::invalid_argument` (ROB-01)
- A hostile tiny preset no longer aborts the game: `shaders = 2147483647` used to resize the pass vector toward ~250 GB (`std::bad_alloc` → terminate); the declared `shaders` count no longer sizes the chain (it is built lazily from the `shaderN` keys actually present), and a `shaderN` key with an index of 32 or above fails the preset with a clear "exceeds the limit of 32" error instead of allocating (ROB-02)

### Removed

#### Graphics / GPU scaler

- Deleted ~10K lines of dead built-in ScaleFX/HQx payload left behind when the built-in filters were removed from the Shader Filter option: `gpu_luts.hpp` (3,820 lines, unreferenced) and its generator `tools/gen_luts.py`, the 7 dead `.frag` sources (`hqx_index`, `hqx_blend`, `scalefx_pass0-4`), and the dead HQx/ScaleFX arrays in the embedded SPIR-V (`gpu_shader_spv_prebuilt.hpp`, 5,595 → 155 lines) and MSL (`gpu_shaders_msl.hpp`, 438 → 53 lines) headers. Only the shared fullscreen-vertex and blit-fragment programs remain baked, and the build-time `gpu_scaler_spv` generation list now covers exactly those two (MAINT-02)
