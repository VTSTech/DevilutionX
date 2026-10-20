<p align="center">
  <img width="560" height="144" alt="image" src="https://github.com/user-attachments/assets/7ac73801-ef7b-4cc1-8442-a191a2a0a1ce" />
</p>

<h1 align="center">DevilutionX — Vulkan GPU &amp; libretro Shader Mod</h1>

<p align="center">
  A standalone mod of <a href="https://github.com/diasurgical/DevilutionX">DevilutionX</a> that adds
  <b>Vulkan GPU rendering</b> and <b>libretro shader support</b> to Diablo &amp; Hellfire on modern operating systems.
</p>

---

[![Discord Channel](https://img.shields.io/discord/518540764754608128?color=%237289DA&logo=discord&logoColor=%23FFFFFF)](https://discord.gg/devilutionx-518540764754608128)
[![Downloads](https://img.shields.io/github/downloads/VTSTech/DevilutionX-libslang-vk/total.svg)](https://github.com/VTSTech/DevilutionX-libslang-vk/releases/latest)
[![GPU Shaders CI](https://github.com/VTSTech/DevilutionX-libslang-vk/actions/workflows/gpu-shaders.yml/badge.svg)](https://github.com/VTSTech/DevilutionX-libslang-vk/actions/workflows/gpu-shaders.yml)
[![GPU scaler build (libslang-vk branch)](https://github.com/VTSTech/DevilutionX-libslang-vk/actions/workflows/gpu-scaler-build.yml/badge.svg?branch=libslang-vk)](https://github.com/VTSTech/DevilutionX-libslang-vk/actions/workflows/gpu-scaler-build.yml)
[![libslang-vk CI](https://github.com/VTSTech/libslang-vk/actions/workflows/ci.yml/badge.svg)](https://github.com/VTSTech/libslang-vk/actions/workflows/ci.yml)

<p align="center">
<img width="1920" height="1080" alt="image" src="https://github.com/user-attachments/assets/6df35aea-9b46-490b-91cc-da78be272647" />
<img width="1920" height="1080" alt="image" src="https://github.com/user-attachments/assets/6c932e6a-f388-47ae-ad09-3b6fb8dc8d89" />
<img width="1920" height="1080" alt="image" src="https://github.com/user-attachments/assets/7731ede4-8869-4ab7-ab4d-4578a425a623" />
</p>

<sub>*(The health-bar and XP-bar are off by default but can be enabled in the [game settings](https://github.com/diasurgical/DevilutionX/wiki/Config-File). Widescreen can also be disabled if preferred.)*</sub>

> [!IMPORTANT]
> **This is a standalone mod, not the official DevilutionX.**
> The GPU shader work in this repository originally targeted upstream ([PR #8737](https://github.com/diasurgical/DevilutionX/pull/8737)). As of **October 11, 2026**, development continues here as an independent mod, branched from upstream **`1.6.0-dev`**.
>
> - Report bugs and request features in **this** repository — please do not send mod-specific reports to upstream.
> - The official project lives at https://github.com/diasurgical/DevilutionX.

# Project Status — October 2026

The audit phase is **complete**, and the shader pipeline has moved into its own library. Where things stand and where development happens now:

- **`master` (this branch) is the audit home.** The fork's full audit history lives here: the finding ledger and field-sweep write-ups in [docs/audit.md](docs/audit.md) and [docs/audit/](docs/audit), the delta ledger in [docs/deltas.md](docs/deltas.md), and the release-by-release record in the fork [changelog](docs/CHANGELOG.md). Every **High** and **Medium** audit finding has been **closed** — the gates that gated first upstream contact are done, with only Low-severity notes remaining.
- **Shader work beyond this fork master continued at [VTSTech/libslang-vk](https://github.com/VTSTech/libslang-vk), now at milestone [M4.05](https://github.com/VTSTech/libslang-vk/releases).** With the High/Medium bar cleared, the shader pipeline was ported out of the game tree into **libslang-vk** — an embeddable **C++23 library** (MIT, clean-room) that parses, translates and compiles libretro slang presets (`.slangp` / `.slang`) with a full Vulkan execution path. It vendors glslang, Vulkan-Headers, volk and VMA with SHA-256 pinning (no Vulkan SDK needed at link time), runs a full-corpus compile sweep against libretro's `slang-shaders` in CI, and is usable by **any SDL3/Vulkan project** — the long-term goal originally sketched in [docs/diablo-handoff.md](docs/diablo-handoff.md), §14.
- **DevilutionX work continues on the [`libslang-vk` branch](https://github.com/VTSTech/DevilutionX-libslang-vk/tree/libslang-vk) of this repository.** The game integration consumes the library via CMake `FetchContent` (pinned M4.05) and carries the current integration work: SDL3 display/DPI fixes, scaler mouse-coordinate mapping (`MapWindowToGame`), MSVC static and nonstatic (DLL) build fixes, a fork-only CI build gate, and the r4 upstream-prep tooling. See the branch's [gpu-scaler-build](https://github.com/VTSTech/DevilutionX-libslang-vk/actions/workflows/gpu-scaler-build.yml) CI for the current green state.
- **Upstream merge track, updated:** the phased plan shared with upstream maintainers on October 16, 2026 ([docs/merge-plan-draft-r2.md](docs/merge-plan-draft-r2.md)) got its reply — *individual changes*. Per [docs/merge-plan-r4.md](https://github.com/VTSTech/DevilutionX-libslang-vk/blob/libslang-vk/docs/merge-plan-r4.md) on the `libslang-vk` branch, the payload is split into one topical branch (and PR) per change — `pr/1-sdl3-build-fixes` through `pr/6-sdl3-main-glue` — each reviewable and buildable on its own, EOL-normalized against upstream and stamped with attribution. Nothing has been merged upstream yet.

| Where | What |
|---|---|
| **`master`** *(this branch)* | Audited standalone mod — release line, full audit history and ledger |
| [`libslang-vk` branch](https://github.com/VTSTech/DevilutionX-libslang-vk/tree/libslang-vk) | Active DevilutionX integration — consumes the libslang-vk library (M4.05) |
| [VTSTech/libslang-vk](https://github.com/VTSTech/libslang-vk) | The standalone, reusable C++23 shader library — shader development beyond fork master (now M4.05) |
| [`wav-mixer-fix` branch](https://github.com/VTSTech/DevilutionX-libslang-vk/tree/wav-mixer-fix) | Standalone fix for Diablo's 1996-era WAV decoding under SDL3_mixer |

# What is DevilutionX

DevilutionX is a port of Diablo and Hellfire that strives to make it simple to run the game while providing engine improvements, bug fixes, and some optional quality of life features.

Check out the [wiki](https://github.com/diasurgical/devilutionX/wiki) for available features and how to take advantage of them.

For a full list of base-game changes, see the [upstream changelog](docs/CHANGELOG_UPSTREAM.md).

# What this mod adds

This mod keeps the stock SDL render path fully intact and layers a GPU shader pipeline on top of it:

- **Vulkan GPU render path** — the game still renders internally at its native 640x480/960x540 8-bit paletted surface, but when a shader filter is active, the frame is uploaded to a GPU texture each frame and pushed through an SDL3 GPU (`SDL_GPU`) pipeline backed by **Vulkan** on Windows and Linux (**Metal** on macOS). If no compatible GPU device can be created — driver update, remote desktop, VM without GPU — the shader simply reports as `(Unavailable)` and the game keeps running on the stock render path.
- **libretro shader preset support** — load `.slangp`/`.slang` presets from a `shaders/` folder next to the executable. Multi-pass chains are translated in-process with glslang to SPIR-V at runtime, with a content-addressed disk cache and sidecar `.spv` fast path for near-instant reloads. There are **no baked-in shaders**: the filters you get are exactly the presets you install (see [Setting Up Shaders](#setting-up-shaders)).
- **The full RetroArch shader ecosystem** — CRT simulation, xBR, ScaleFX, HQx, and hundreds more, straight from libretro's `slang-shaders` collection. Unlike the stock renderer's plain stretch, these GPU filters reconstruct pixel-art detail: sharper edges, smoother diagonals, and cleaner corner transitions. Some presets (e.g. the HQx family) load PNG lookup textures at runtime.
- **Live shader feedback** — the on-screen overlay always shows the active filter and backend (e.g. `Shader: ScaleFX 3x (Vulkan)` or `Shader: Off`). If a shader fails to compile or load, the overlay reports it as `(Unavailable)` — and you can select a new shader right from that state without restarting. Every change is also logged to `gpu-scaler.log`, and the choice persists in `diablo.ini` under the `Shader Filter` key.
- **Audio fix available** — a standalone fix for decoding Diablo's 1996-era WAV files (zero-length RIFF chunks fail on current SDL3_mixer) is maintained on the [`wav-mixer-fix`](https://github.com/VTSTech/DevilutionX-libslang-vk/tree/wav-mixer-fix) branch.
- **Documented and audited** — feature docs in [docs/GPU_SHADERS.md](docs/GPU_SHADERS.md), architecture notes in [docs/diablo-handoff.md](docs/diablo-handoff.md), and the complete audit trail (per-preset compatibility audits, field-sweep test lists, finding ledger) under [docs/audit.md](docs/audit.md) and [docs/audit/](docs/audit). This audit is what gated the port into the standalone library — see [Project Status](#project-status--october-2026).

See it in action: [shader demo video](https://www.youtube.com/watch?v=PHQl0EOa7L8).

# Requirements

- A GPU with **Vulkan** support (or **Metal** on macOS) and a driver new enough for that API. Vulkan is the only actively-supported backend on Windows and Linux.
- A build with SDL3 (`USE_SDL3=ON`). `USE_GPU_SCALER` is a dependent option that defaults to ON whenever `USE_SDL3` is ON and `USE_SDL1` is OFF.
- The in-game **Upscale** option enabled (it is on by default).
- [`git-lfs`](https://git-lfs.com) installed when cloning the shader pack (some presets load PNG lookup textures at runtime — see [Setting Up Shaders](#setting-up-shaders)).

# How to Install

Note: You'll need access to the data from the original game. If you don't have an original CD, you can [buy Diablo from GoG.com](https://www.gog.com/game/diablo) or Battle.net. Alternatively, you can use `spawn.mpq` from the [shareware](https://github.com/diasurgical/devilutionx-assets/releases/latest/download/spawn.mpq) [[2]](http://ftp.blizzard.com/pub/demos/diablosw.exe) version, in place of `DIABDAT.MPQ`, to play the shareware portion of the game.

Download the latest [release](https://github.com/VTSTech/DevilutionX-libslang-vk/releases/latest) and extract the contents to a location of your choosing, or [build from source](#building-from-source). Releases are built by CI for **Windows x64** and **Linux x64**; other platforms require building from source.

- Copy `DIABDAT.MPQ` from the CD or Diablo installation (or [extract it from the GoG installer](https://github.com/diasurgical/devilutionX/wiki/Extracting-MPQs-from-the-GoG-installer)) to the DevilutionX folder.
- To run the Diablo: Hellfire expansion, you will also need to copy `hellfire.mpq`, `hfmonk.mpq`, `hfmusic.mpq`, and `hfvoice.mpq`.
- Optional: install the libretro shader pack — see [Setting Up Shaders](#setting-up-shaders) (on Windows, run the included `install_shaders.cmd`; on Linux, `install_shaders.sh`).

For more detailed instructions: [Installation Instructions](./docs/installing.md).

# Setting Up Shaders

Shader presets are loaded at runtime from a `shaders/` folder next to the executable — nothing is baked into the binary. The easiest way to populate it is to clone libretro's shader collection directly into your Diablo folder:

```sh
git clone https://github.com/libretro/slang-shaders shaders
cd shaders
git lfs pull
```

The `git lfs pull` step matters: some presets (like HQ2x) reference PNG lookup textures, and without pulling the LFS assets those PNGs are left as pointer stubs, so the presets that need them will fail to load or render incorrectly. Make sure `git-lfs` is installed and initialized (`git lfs install`) before cloning.

The release zips ship helper scripts beside the executable that automate the above:

- `install_shaders.cmd` (Windows) / `install_shaders.sh` (Linux) — wipes and re-downloads `shaders/` beside the executable (fresh clone plus `git lfs pull`; needs `git` and `git-lfs` — the Linux script prints the per-distro install command if either is missing).
- `clear_shader_cache.cmd` (Windows) / `clear_shader_cache.sh` (Linux) — deletes `shaders/cache/`, the translated-shader compile cache. It is rebuilt automatically on the next preset load; run it after a corpus update or a game update if presets misbehave.
- `install_git.cmd` (Windows) — one-time helper for machines without `git`: installs Git for Windows (bundling Git LFS) via winget so `install_shaders.cmd` can run.

Presets are selected from the in-game folder browser, so you can keep the whole collection or just the handful of `.slangp` files you actually use.

# Using Shaders

1. Install the shader pack as described in [Setting Up Shaders](#setting-up-shaders).
2. Start the game and open **Main Menu → Options → Graphics Options**.
3. Scroll to **Shader Filter** and pick a preset from the folder browser (`Off` uses the stock renderer).
4. Confirm — the renderer is recreated and the filter applies immediately from the menu. The option is disabled during an ongoing game because it forces a renderer recreation.
5. Shaders are loaded immediately upon selecting them — no restart required. If a shader fails to compile or load, the overlay reports it as `(Unavailable)`; you can select a new shader straight from that state and it will load.

The GPU backend can be forced explicitly with the `DEVILUTIONX_GPU_BACKEND=vulkan` environment variable.

# Building from Source

Want to compile the program by yourself? Great! Follow the general [build instructions](./docs/building.md), and see [docs/GPU_SHADERS.md](docs/GPU_SHADERS.md) for the full mod-specific build matrix (including the MinGW-w64 Windows cross-build). Quick start on Linux:

```sh
cmake -S . -B build -G Ninja -DCMAKE_BUILD_TYPE=Release -DUSE_SDL3=ON \
  -DDEVILUTIONX_SYSTEM_SDL3=OFF -DDEVILUTIONX_SYSTEM_LIBSODIUM=OFF \
  -DBUILD_TESTING=OFF
cmake --build build -j$(nproc)
```

Build notes:

- `USE_GPU_SCALER` defaults to ON when `USE_SDL3=ON` and `USE_SDL1=OFF`.
- On **this branch (`master`)**, shader presets are compiled at runtime by the bundled glslang (`3rdParty/glslang`) — no external shader toolchain is needed at build time.
- On the **`libslang-vk` branch**, the in-tree preset/translator stack is replaced by the standalone [libslang-vk](https://github.com/VTSTech/libslang-vk) library (C++23, fetched and pinned at M4.05 by CMake), which builds its own glslang — still no Vulkan SDK or shader toolchain required. That branch also ships `tools/build-local-win11.ps1`, a local Windows 11 build script mirroring its CI (`static`, `nonstatic` DLL, or both).
- The SDL_GPU device, swapchain, and pipelines are created lazily and torn down on shutdown and renderer reinitialization, so switching a shader filter to `Off` (or losing the GPU) restores the exact stock render path.
- CI: [gpu-shaders.yml](.github/workflows/gpu-shaders.yml) builds Windows x64 and Linux x64 on this branch; the [gpu-scaler-build.yml](https://github.com/VTSTech/DevilutionX-libslang-vk/blob/libslang-vk/.github/workflows/gpu-scaler-build.yml) gate does the same (plus nonstatic DLL variants) on the `libslang-vk` branch.

# Contributing

Development is split by branch: **this `master` branch is the audit archive and release line** — issues, regression reports and documentation fixes are all welcome here; active game-side development happens on the **`libslang-vk` branch**, and standalone shader-library development happens at [VTSTech/libslang-vk](https://github.com/VTSTech/libslang-vk). The upstream merge track follows the individual-change plan in [docs/merge-plan-r4.md](https://github.com/VTSTech/DevilutionX-libslang-vk/blob/libslang-vk/docs/merge-plan-r4.md) (one topical branch/PR per change, per the upstream maintainers' directive); until anything is merged there, this repository remains the single place where mod development happens. For engine behavior that this mod does not touch, the upstream [wiki](https://github.com/diasurgical/devilutionX/wiki) and [contributing guide](docs/CONTRIBUTING.md) remain the best references.

# Credits

- The original Devilution project: [Devilution](https://github.com/diasurgical/devilution#credits)
- [Everyone](https://github.com/diasurgical/devilutionX/graphs/contributors) who worked on Devilution/DevilutionX
- [Nikolay Popov](https://www.instagram.com/nikolaypopovz/) for UI and graphics
- [WiAParker](https://wiaparker.pl/projekty/diablo-hellfire/) for the Polish voice pack
- Shader authors whose work this mod builds on: ScaleFX by [Sp00kyFox](https://github.com/Sp00kyFox) (MIT), the HQx family by Maxim Steppin, Adam Zemek and Blok (LGPL-2.1), and the wider [libretro slang shaders](https://github.com/libretro/slang-shaders) ecosystem
- [glslang](https://github.com/KhronosGroup/glslang) by The Khronos Group for runtime shader translation (bundled on `master`, vendored by [libslang-vk](https://github.com/VTSTech/libslang-vk) on the library track)
- The [libslang-vk](https://github.com/VTSTech/libslang-vk) standalone library — the C++23 home of the shader pipeline (current milestone M4.05)
- Mod integration, audit and maintenance by [VTSTech](https://github.com/VTSTech) ([vts-tech.org](https://www.vts-tech.org))

# Legal

DevilutionX is made publicly available and released under the Sustainable Use License (see [LICENSE](LICENSE.md)). This mod is a derivative work of DevilutionX and is distributed under the same terms.

The source code in this repository is for non-commercial use only. If you use the source code, you may not charge others for access to it or any derivative work thereof.

Diablo® - Copyright © 1996 Blizzard Entertainment, Inc. All rights reserved. Diablo and Blizzard Entertainment are trademarks or registered trademarks of Blizzard Entertainment, Inc. in the U.S. and/or other countries.

DevilutionX and any of its maintainers are in no way associated with or endorsed by Blizzard Entertainment®.
