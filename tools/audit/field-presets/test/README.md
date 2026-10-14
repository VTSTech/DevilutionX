# Field test presets (shaders/test/)

The four `test/*.slangp` presets referenced by `docs/audit/shader-test-list-2026-10-15.md` (Tier 1a/1c) and by earlier field rounds, re-created in-repo on 2026-10-15 so they are version-controlled and runnable through `tools/audit/translate_preflight`. VTSTech's copies live in `<game>/shaders/test/`; drop the whole `test/` folder there (preset + its `shaders/` subfolder).

| preset | passes | purpose | expected result |
|---|---|---|---|
| `history.slangp` | 1 | F5 history-ring canary: samples `Source` + `OriginalHistory1` + `OriginalHistory3` (gap index — the old worst case) | loads; textures `[Source, OriginalHistory1, OriginalHistory3]` in the load log |
| `feedback.slangp` | 1 | F6 feedback canary: mixes `Source` with `PassFeedback0` (double-buffered) | loads; persistent trail effect |
| `feedback-noncausal.slangp` | 1 | negative control: reads `PassOutput0` in pass 0 | must be REJECTED: `slang: PassOutput0 is not causal in pass 0`; filter off for the session |
| `format.slangp` | 3 | `#pragma format` ladder: pass 0 default RGBA8, pass 1 `R16_SFLOAT` (not a mandatory Vulkan color-attachment format), pass 2 `R8G8B8A8_SRGB` | loads on every device; if a format is refused, the runner downgrades to R8G8B8A8_UNORM with a `color target format unavailable` warning (field round 3) instead of failing the preset |

History: `test/history.slangp` and `test/feedback.slangp` were introduced for the F5/F6 field rounds; `test/feedback-noncausal.slangp` proves the failure contract; `test/format.slangp` was field-tested 2026-10-15 (v8 binary) and exposed the missing pipeline-format downgrade. All four re-created here from their documented behavior.
