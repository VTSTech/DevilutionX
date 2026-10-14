# GPU Shaders — Vulkan Renderer & libretro Preset Support

This standalone mod of [DevilutionX](https://github.com/diasurgical/DevilutionX) layers a
**GPU shader pipeline** on top of the stock SDL render path. The game still renders
internally at its native 640x480/960x540 8-bit paletted surface; when a shader filter is
active, the frame is uploaded to a GPU texture each frame and pushed through an SDL3 GPU
(`SDL_GPU`) pipeline backed by **Vulkan** on Windows and Linux (**Metal** on macOS). The
filters themselves are **libretro slang presets** (`.slangp`/`.slang`) loaded at runtime
from a `shaders/` folder next to the executable — there are **no baked-in shaders**: the
filters you get are exactly the presets you install, drawn from the full
[RetroArch shader ecosystem](https://github.com/libretro/slang-shaders) (CRT simulation,
xBR, ScaleFX, HQx, and hundreds more).

The feature is a drop-in addition: nothing about the stock rendering path changes unless
a Shader Filter is selected, and the game falls back to the stock renderer automatically
if a compatible GPU device cannot be created (driver update, remote desktop, VM without
GPU — the filter simply reports as `(Unavailable)`).

> Related reading: [docs/SLANG_SPEC.md](SLANG_SPEC.md) is the normative reference for
> every `.slangp` key and `.slang` semantic this implementation supports (with a
> per-feature status matrix and gap list), [docs/diablo-handoff.md](diablo-handoff.md)
> holds the architecture history and engineering gotchas, and per-preset compatibility
> audits live under [docs/audit/](audit/).

## Requirements

- A GPU with **Vulkan** support (or **Metal** on macOS) and a driver new enough for that
  API. Vulkan is the only actively-supported backend on Windows and Linux.
- A build with SDL3 (`USE_SDL3=ON`). `USE_GPU_SCALER` is a dependent option that defaults
  to ON whenever `USE_SDL3` is ON and `USE_SDL1` is OFF.
- The in-game **Upscale** option enabled (it is on by default).
- [`git-lfs`](https://git-lfs.com) when cloning the shader pack — some presets (e.g. the
  HQx family) load PNG lookup textures at runtime, and without LFS those PNGs are left
  as pointer stubs.

## Setting Up Shaders

Shader presets are loaded at runtime from a `shaders/` folder **next to the executable**
(recursive scan — subfolders are fine). The easiest way to populate it is to clone
libretro's shader collection directly into your Diablo folder:

```sh
git clone https://github.com/libretro/slang-shaders shaders
cd shaders
git lfs pull
```

The `git lfs pull` step matters: presets that reference PNG lookup textures fail to load
or render incorrectly if the LFS assets were never pulled. Make sure `git-lfs` is
installed and initialized (`git lfs install`) before cloning. You can keep the whole
collection or just the handful of `.slangp` files you actually use.

## Using Shaders

1. Start the game and open **Main Menu → Options → Graphics Options**.
2. Scroll to **Shader Filter** and pick a preset from the folder browser (`Off` uses the
   stock renderer).
3. Confirm — the renderer is recreated and the filter applies immediately from the menu.
   The option is disabled during an ongoing game because it forces a renderer recreation.
4. Shaders load immediately upon selection — no restart required. If a shader fails to
   compile or load, the overlay reports it as `(Unavailable)`; you can select a new
   shader straight from that state and it will load.
5. The choice persists in `diablo.ini` under the `Shader Filter` key (plus a hidden
   companion key storing the preset's relative path, which is the durable selection) and
   is restored on the next launch.

**Live shader feedback:** the on-screen overlay always shows the active filter and
backend — e.g. `Shader Filter: ScaleFX 3x (Vulkan)` or `Shader Filter: Off`. The label is
drawn into the game frame itself, so it is upscaled by the active filter and every switch
lets you compare text sharpness between filters. Every change is also logged to
`gpu-scaler.log`.

The GPU backend can be forced explicitly with the `DEVILUTIONX_GPU_BACKEND=vulkan`
environment variable. Supported values are the SDL GPU driver names: `vulkan` (and
`metal` on macOS). Vulkan is the only actively-supported backend on Windows and Linux;
the variable is belt-and-suspenders since no other backend is selectable.

## How the shader pipeline works

**Frame flow.** The palette-blit into an RGBA texture is unchanged from the stock path.
When a Shader Filter is active, the RGBA frame is uploaded to a GPU texture each frame
and pushed through the preset's pass chain. Every intermediate pass renders to its own
offscreen texture (with per-pass scale rules, filter and wrap modes, and optional sRGB or
float framebuffer formats); the final pass letterboxes into the swapchain viewport (1:1
when it fits, linear sampling otherwise). The **Integer Scaling** option is honored by
clamping the effective scale. Passes rendering into intermediate targets get a Y-mirror
transform so even- and odd-length chains both come out upright.

**Preset parsing.** `.slangp` files are parsed in-process: shader chains, per-pass scale
rules, framebuffer format flags, texture LUTs (PNG), aliases, `#pragma` parameters (at
their declared defaults), `frame_count_mod` for frame-count feedback, and `#reference`
inheritance with rebased paths. Textures referenced by a preset load at preset-load time
(see the PNG codec note in Troubleshooting).

**Translation and compilation.** `.slang` sources are translated to Vulkan GLSL in-process
(std140 uniform blocks, dense bindings), then compiled to SPIR-V by the bundled
[glslang](https://github.com/KhronosGroup/glslang) (`3rdParty/glslang`) — no external
shader toolchain is needed at build time and nothing is pre-compiled into the binary.
Compiled SPIR-V is cached in a content-addressed `shaders/cache/` directory, and a
sidecar `<name>.vert.spv`/`.frag.spv` next to the shader always wins for near-instant
reloads. Preset passes always submit SPIR-V; on macOS the Metal backend transpiles it to
MSL at runtime (untested — see Known limitations).

**Multi-pass execution.** The runner builds one pipeline per pass, manages the frame
history ring (with the current frame plus the requested history depth), feedback
textures, aliases, and a sampler cache, and feeds each pass the standard libretro uniform
set (`MVP`, `OutputSize`, `InputSize`, `Source`/`Original`/`OriginalHistory#`,
`frame_count`, `frame_direction`, `PASSINDEX`, ...). The exact feature set — what is
supported, partial, or not yet implemented — is tabulated per key and per semantic in
[docs/SLANG_SPEC.md](SLANG_SPEC.md).

**Failure contract.** Any preset load or run failure — parse error, translation error,
compile error, pipeline creation failure, device loss — tears the scaler down and the
game **automatically reverts to the regular SDL_Renderer path for the rest of the
session**. The game never dies; the failure is recorded in `gpu-scaler.log` and the
overlay reports the filter as `(Unavailable)`.

## The built-in filters are gone

Early builds of this mod shipped a set of baked filters (`HQ2x/HQ3x/HQ4x`, `ScaleFX
2x/3x/4x`) compiled into the binary. Those were removed — the same effects (and hundreds
more) now load as libretro presets instead: the ScaleFX family lives under
`edge-smoothing/scalefx/` and the HQx family under `edge-smoothing/hqx/` in the
slang-shaders collection. Stale `diablo.ini` values pointing at the old built-in filter
slots are sanitized to `Off` on load. The only programs still shipped inside the binary
are the shared full-screen-vertex and present-blit helpers used by every render path.

## Technical notes

- The SDL_GPU device, swapchain, and pipelines are created lazily and torn down on
  shutdown and renderer reinitialization, so switching a shader filter to `Off` (or
  losing the GPU) restores the exact stock render path. On Windows, enabling a filter
  hands the game a freshly recreated window so the Vulkan device can claim it cleanly
  (see Troubleshooting).
- The shared Vertex/Blit helper programs are the only baked shader content: SPIR-V blobs
  are committed prebuilt (`Source/engine/render/gpu/gpu_shader_spv_prebuilt.hpp`), and if
  `glslangValidator` is found at configure time they are regenerated at build time
  instead (custom target `gpu_scaler_spv`). MSL copies for macOS live in
  `gpu_shaders_msl.hpp`.
- The virtual gamepad overlay is not drawn in GPU mode; touch devices continue to use
  the stock renderer path (GPU scaler is a desktop feature).
- Video playback goes through the final blit pass rather than the filter chain; it
  works, but its aspect-fit behavior differs slightly.
- Preset behavior at fractional window sizes is defined by each preset's own scale rules
  (libretro semantics), so filters differ in how gracefully they degrade off exact
  multiples — this is per-preset, not a global behavior.

## Building

Linux (native):

```sh
cmake -S . -B build -G Ninja -DCMAKE_BUILD_TYPE=Release -DUSE_SDL3=ON \
  -DDEVILUTIONX_SYSTEM_SDL3=OFF -DDEVILUTIONX_SYSTEM_LIBSODIUM=OFF \
  -DBUILD_TESTING=OFF
cmake --build build -j$(nproc)
```

Windows (MinGW-w64 cross build from Linux, fully vendored deps, static SDL3; the bundled
`CMake/platforms/mingwcc64.toolchain.cmake` understands `CROSS_PREFIX`):

```sh
cmake -S . -B build-win -G Ninja \
  -DCMAKE_TOOLCHAIN_FILE=CMake/platforms/mingwcc64.toolchain.cmake \
  -DCROSS_PREFIX=/usr -DCMAKE_BUILD_TYPE=Release -DUSE_SDL3=ON \
  -DDEVILUTIONX_SYSTEM_SDL3=OFF -DDEVILUTIONX_SYSTEM_LIBSODIUM=OFF \
  -DDEVILUTIONX_SYSTEM_ZLIB=OFF -DDEVILUTIONX_SYSTEM_BZIP2=OFF \
  -DDEVILUTIONX_SYSTEM_LIBPNG=OFF -DBUILD_TESTING=OFF
cmake --build build-win -j$(nproc)
```

On GCC 14-based toolchains, add
`-DCMAKE_C_FLAGS="-Wno-error=incompatible-pointer-types -Wno-error=implicit-function-declaration -Wno-error=int-conversion"`
to keep the vendored ZeroTier/miniupnpc C sources building (GCC 14 promotes those
warnings to errors; this is mingw-specific).

CI (`.github/workflows/gpu-shaders.yml`) builds **Windows x64 and Linux x64** on every
push, manual dispatch, and release. The Windows job reproduces the verified local
cross-build exactly: MinGW-w64 cross toolchain, all dependencies vendored, GCC14+
diagnostic relaxation for the vendored C sources, and **fully static linking**
(`-static -static-libgcc -static-libstdc++`) so the resulting `devilutionx.exe` depends
on Windows system DLLs only and launches on a clean Windows install with no extra
runtime DLLs. The repo keeps only this workflow plus repo-level checks
(`clang-format-check`, `clang-tidy-check`, `translations`, `src_dist_release`,
`cache-cleanup`) — upstream's ~31 per-platform workflows are intentionally absent.

## Troubleshooting

The GPU scaler writes its own log file, **`gpu-scaler.log`, next to
`devilutionx.exe`**. It records every initialization step (backend in use, swapchain
format, shader/pipeline creation), the first presented frame, and any failure with the
underlying SDL error text. If a filter misbehaves, include that file in your bug report
(the log is capped at 200 KB with one `.old` generation, and a `heartbeat:` line is
written every 5 seconds — if the game appears frozen, the log shows whether the render
loop was still running).

If the GPU path hits an unrecoverable error (shader compilation failure, pipeline
creation failure, device loss), the scaler tears itself down and the game **automatically
reverts to the regular SDL_Renderer renderer for the rest of the session** — the game
keeps running, it just loses the shader filter. The failure is recorded in
`gpu-scaler.log`. The log also records fatal native exceptions (`CRASH #1: access
violation ...` lines) with the faulting address, thread name, and a raw stack backtrace
as `devilutionx.exe+0x...` offsets, all of which should be attached to a bug report.

### Window claim failures ("Access is denied" / 0x80070005)

On Windows, the GPU driver can refuse to create a swapchain on a window that a previous
GPU device already claimed. Enabling a filter therefore hands the game a **freshly
recreated window** before the Vulkan device claims it (and the same is done in reverse
when switching back to the regular renderer), so the window switch is expected and costs
a brief flicker. All of these steps are recorded in `gpu-scaler.log`.

To get back to a clean state manually, set `Shader Filter=0` under the `[Graphics]`
section of `diablo.ini`.

### Sound effects fail with "Audio data is in unknown/unsupported/corrupt format" (fixed in this fork)

The vendored SDL3_mixer 3.2.0 shipped with a WAV decoder regression: it treats a
zero-length RIFF chunk inside a WAV file as end-of-file, then rejects the file for having
no data chunk. 1996-era WAVs — like every sound effect in `DIABDAT.MPQ` — can contain
empty metadata chunks, so menu sounds (`sfx\items\titlemov.wav` / `titlslct.wav`) failed
to load and the stock "fatal error" dialog locked the UI. This fork carries two fixes:

1. A patch to the vendored SDL3_mixer
   (`3rdParty/SDL3_mixer/0001-WAV-decoder-skip-zero-length-chunks.patch`) that skips
   zero-length chunks instead of aborting the chunk walk.
2. A fallback in `SoundSample::SetChunk`: if SDL3_mixer still cannot load a sound effect,
   it is decoded with SDL's own `SDL_LoadWAV_IO` (the loader SDL2_mixer used for years)
   and handed to the mixer as a canonical WAV. A
   `SoundSample::SetChunk: recovered audio via SDL_LoadWAV_IO` line in `gpu-scaler.log`
   records when this path triggers.

Additionally, a sound effect that cannot be loaded for any reason no longer raises a
fatal error dialog: the game logs the failure and continues without that sound.

### Other checks

- **Preset renders wrong or `(Unavailable)`:** check `gpu-scaler.log` for the
  `translating`/`compiling` error lines, then consult the preset's entry in
  [docs/SLANG_SPEC.md](SLANG_SPEC.md) — the status matrix indicates whether the feature
  that preset needs is implemented.
- **HQx-family presets fail or render placeholder colors:** the PNG lookup textures were
  not pulled — run `git lfs pull` inside the `shaders/` clone (see Setting Up Shaders).
- **PNG load/save errors in the cross build:** the build vendors SDL_image's own
  libpng+zlib (`SDLIMAGE_VENDORED`); a build without it silently has no PNG codec.
- As a workaround for a broken driver, the backend can be forced with an environment
  variable, e.g.:

      set DEVILUTIONX_GPU_BACKEND=vulkan   (cmd.exe)
      $env:DEVILUTIONX_GPU_BACKEND="vulkan"   (PowerShell)

## Known limitations

- **macOS/Metal is untested.** The device layer accepts Metal and the shared helper
  programs ship with MSL; preset passes submit SPIR-V, which SDL transpiles to MSL at
  runtime — but no macOS field validation has happened yet. If Metal fails at runtime,
  the game falls back to the stock renderer cleanly. Feedback from macOS users is needed.
- Preset load is not hot-reloaded from disk while playing (restart to pick up edited
  files); switching presets from the menu does rebuild the chain immediately.
- The option cannot be changed while a game session is active (by design, since it
  recreates the renderer).
- Preset coverage is broad but not total — see the status matrix and prioritized gap
  list in [docs/SLANG_SPEC.md](SLANG_SPEC.md); deep bezel/Mega Bezel-style chains
  exercise the largest unimplemented surface.

## Documentation map

| Doc | Contents |
|---|---|
| [README](../README.md) | Project scope (standalone mod), install, quick start |
| [docs/SLANG_SPEC.md](SLANG_SPEC.md) | Normative slang spec reference: every `.slangp` key + `.slang` semantic with implementation status; prioritized gap list |
| [docs/diablo-handoff.md](diablo-handoff.md) | Architecture history, design decisions, engineering gotchas, audit campaign plan |
| [docs/audit/](audit/) | Per-preset compatibility audit report + feature/risk CSVs over all 2658 stock presets |

## Credits

- The libretro slang-shaders ecosystem and its authors — presets are loaded verbatim at
  runtime; see per-shader headers for licensing (the collection is GPLv3; do not
  redistribute selected presets without checking).
- [glslang](https://github.com/KhronosGroup/glslang) by The Khronos Group for runtime
  shader translation.
- ScaleFX by Sp00kyFox (MIT) and the HQx family by Maxim Steppin, Adam Zemek and Blok
  (LGPL 2.1) — consumed as libretro presets; historical baked ports were removed.
- DevilutionX: Diasurgical and contributors.
- Mod integration and maintenance by [VTSTech](https://github.com/VTSTech)
  ([vts-tech.org](https://www.vts-tech.org)).

