# Changelog

All notable changes to this project will be documented in this file.

The format is based on [Keep a Changelog](https://keepachangelog.com/en/1.0.0/)
and this project adheres to [Semantic Versioning](https://semver.org/spec/v2.0.0.html).

Fork releases are numbered `1.6.0-dev-<commit>`, where `<commit>` is the
short hash of the commit a build or overlay was produced from, and are
dated with an America/Toronto timestamp. Upstream (diasurgical) release
history lives in [docs/CHANGELOG_UPSTREAM.md](docs/CHANGELOG_UPSTREAM.md)
and is not modified.

## 1.6.0-dev-27548c8 (2026-10-15 03:04 EDT)

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
