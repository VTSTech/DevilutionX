#!/usr/bin/env python3
"""audit_slang_presets.py — categorize libretro slang presets by feature usage.

Walks a directory of .slangp + .slang files (the libretro slang-shaders tree)
and produces a CSV report of every preset with:

  - preset path
  - pass count
  - uses #reference (preset inclusion)
  - uses aliases
  - uses OriginalHistory#
  - uses PassFeedback
  - uses PassOutput#
  - uses User# LUTs
  - uses named LUTs (sampler2D lookups)
  - uses frame_count_mod
  - uses floatFramebuffer / srgbFramebuffer / mipmapInput
  - uses absolute-scale passes (ScaleType.ABSOLUTE)
  - uses #pragma parameters
  - total #include depth (transitive shader files)
  - max history depth required
  - distinct LUT count
  - estimated Vulkan risk (heuristic: 'low' / 'medium' / 'high' — see RISK_NOTES)

The output CSV can be sorted/pivoted to plan Vulkan validation work:
which presets to test first (lowest risk, most popular), which are blocked
by missing features (high risk), which fall back gracefully.

Usage:
    python3 audit_slang_presets.py /path/to/slang-shaders > report.csv

The script is read-only and idempotent; running it twice yields identical
output. It does NOT compile anything — it does static feature extraction
only. Compile testing is a separate step (run the game's preset_smoke
harness per preset, or use the in-game preset browser).
"""
from __future__ import annotations

import csv
import os
import re
import sys
from collections import defaultdict
from dataclasses import dataclass, field
from pathlib import Path


# ---------------------------------------------------------------------------
# Patterns. libretro preset spec: https://github.com/libretro/RetroArch/blob/master/retroarch_shader_driver.c  # noqa: E501
# (.slangp keys documented in the libretro shader spec.)
# ---------------------------------------------------------------------------

# .slangp top-level keys. The ones we care about for feature extraction:
# libretro allows both `key = value` and `key = "value"` (quoted), so every
# regex below accepts an optional pair of double quotes around the value.
RE_SHADERS = re.compile(r'^\s*shaders\s*=\s*"?(\d+)"?\s*$', re.MULTILINE)
RE_FILTER  = re.compile(r'^\s*filter\d+\s*=\s*"?(\w+)"?\s*$', re.MULTILINE)
RE_WRAP    = re.compile(r'^\s*wrap\d+\s*=\s*"?(\w+)"?\s*$', re.MULTILINE)
RE_SCALE_TYPE = re.compile(r'^\s*scale_type[_xy]?\d*\s*=\s*"?(\w+)"?\s*$', re.MULTILINE)
RE_SCALE_X  = re.compile(r'^\s*scale_x\d+\s*=\s*"?([\d.]+)"?\s*$', re.MULTILINE)
RE_SCALE_Y  = re.compile(r'^\s*scale_y\d+\s*=\s*"?([\d.]+)"?\s*$', re.MULTILINE)
RE_FLOAT_FB = re.compile(r'^\s*float_framebuffer\d+\s*=\s*"?true"?\s*$', re.MULTILINE | re.IGNORECASE)
RE_SRGB_FB  = re.compile(r'^\s*srgb_framebuffer\d+\s*=\s*"?true"?\s*$', re.MULTILINE | re.IGNORECASE)
RE_MIPMAP   = re.compile(r'^\s*mipmap_input\d+\s*=\s*"?true"?\s*$', re.MULTILINE | re.IGNORECASE)
RE_ALIAS    = re.compile(r'^\s*alias\d+\s*=\s*"?(\w+)"?\s*$', re.MULTILINE)
RE_FCMOD    = re.compile(r'^\s*frame_count_mod\s*=\s*"?(\d+)"?\s*$', re.MULTILINE)
RE_FEEDBACK = re.compile(r'^\s*feedback_pass\s*=\s*"?(-?\d+)"?\s*$', re.MULTILINE)
RE_HISTORY  = re.compile(r'^\s*history_size_exp\s*=\s*"?(\d+)"?\s*$', re.MULTILINE)  # rare
RE_REFERENCE = re.compile(r'^\s*#reference\s*=\s*"([^"]+)"\s*$', re.MULTILINE)
RE_SHADER_N = re.compile(r'^\s*shader\d+\s*=\s*"?([^"\s]+)"?\s*$', re.MULTILINE)
RE_TEXTURE_LUT = re.compile(r'^\s*textures\s*=\s*"([^"]+)"\s*$', re.MULTILINE)
RE_LUT_PATH = re.compile(r'^\s*(\w+)\s*=\s*"([^"]+\.png)"\s*$', re.MULTILINE)
RE_LUT_FILTER = re.compile(r'^\s*(\w+)_filter\s*=\s*"?(\w+)"?\s*$', re.MULTILINE)
RE_LUT_WRAP   = re.compile(r'^\s*(\w+)_wrap_mode\s*=\s*"?(\w+)"?\s*$', re.MULTILINE)
RE_LUT_MIPMAP = re.compile(r'^\s*(\w+)_mipmap\s*=\s*"?true"?\s*$', re.MULTILINE | re.IGNORECASE)
RE_PARAM     = re.compile(r'^\s*(\w+)\s*=\s*"?\S+"?\s*$', re.MULTILINE)

# .slang shader features (slang -> GLSL translation concerns)
RE_PRAGMA_NAME   = re.compile(r'^\s*#pragma\s+name\s+(\w+)\s*$', re.MULTILINE)
RE_PRAGMA_PARAM  = re.compile(r'^\s*#pragma\s+parameter\s+(\w+)\s+', re.MULTILINE)
RE_PRAGMA_FORMAT = re.compile(r'^\s*#pragma\s+format\s+(\w+)\s*$', re.MULTILINE)
RE_PRAGMA_STAGE  = re.compile(r'^\s*#pragma\s+stage\s+(\w+)\s*$', re.MULTILINE)
RE_INCLUDE       = re.compile(r'^\s*#include\s+"([^"]+)"\s*$', re.MULTILINE)
RE_UNIFORM_TEX   = re.compile(r'^\s*uniform\s+sampler2D\s+(\w+)\s*;', re.MULTILINE)
RE_UNIFORM_TEX_EXTERN = re.compile(r'^\s*layout\s*\([^)]*\)\s*uniform\s+sampler2D\s+(\w+)\s*;', re.MULTILINE)
RE_MVP_USAGE     = re.compile(r'\bMVP\b')
RE_FRAME_COUNT   = re.compile(r'\bFrameCount\b')
RE_FRAME_DIR     = re.compile(r'\bFrameDirection\b')
RE_FRAME_TIME    = re.compile(r'\bFrameTimeDelta\b')
RE_PASS_INDEX    = re.compile(r'\bPassIndex\b')
RE_PASS_OUTPUT   = re.compile(r'\bPassOutput(\d+)\b')
RE_PASS_FEEDBACK = re.compile(r'\bPassFeedback(\d+)\b')
RE_ORIG_HISTORY  = re.compile(r'\bOriginalHistory(\d+)\b')
RE_USER_LUT      = re.compile(r'\bUser(\d+)\b')


@dataclass
class PresetReport:
    path: str
    rel_path: str
    passes: int = 0
    has_reference: bool = False
    reference_target: str = ""
    aliases: int = 0
    uses_original_history: bool = False
    max_history_depth: int = 0
    uses_pass_feedback: bool = False
    max_pass_feedback: int = -1
    uses_pass_output: bool = False
    uses_user_lut: bool = False
    uses_named_lut: bool = False
    named_luts: int = 0
    uses_frame_count_mod: bool = False
    uses_float_fb: bool = False
    uses_srgb_fb: bool = False
    uses_mipmap_input: bool = False
    uses_absolute_scale: bool = False
    uses_viewport_scale: bool = False
    uses_pragma_param: bool = False
    pragma_param_count: int = 0
    uses_pragma_format: bool = False
    uses_mvp: bool = False
    uses_frame_count: bool = False
    uses_frame_direction: bool = False
    uses_frame_time: bool = False
    uses_pass_index: bool = False
    uses_includes: bool = False
    max_include_depth: int = 0
    distinct_shader_files: int = 0
    distinct_lut_files: int = 0
    feedback_pass: int = -1
    risk: str = "low"
    risk_reasons: list = field(default_factory=list)


def read_text(path: Path) -> str:
    """Read with multiple encodings; slang files are mixed."""
    for enc in ("utf-8", "latin-1"):
        try:
            return path.read_text(encoding=enc)
        except UnicodeDecodeError:
            continue
    return path.read_text(encoding="utf-8", errors="replace")


def parse_preset(preset_path: Path, root: Path) -> PresetReport:
    """Parse one .slangp file (resolving #reference chains) and report features."""
    text = read_text(preset_path)
    rep = PresetReport(
        path=str(preset_path),
        rel_path=str(preset_path.relative_to(root)).replace("\\", "/"),
    )

    # #reference: this preset inherits everything from another and may add/override
    # passes. For feature accounting, we walk the merge.
    m = RE_REFERENCE.search(text)
    if m:
        rep.has_reference = True
        rep.reference_target = m.group(1)
        # Resolve the reference relative to this preset's directory.
        ref_path = (preset_path.parent / m.group(1)).resolve()
        if ref_path.exists() and ref_path.suffix == ".slangp":
            # Recurse: merge the referenced preset's features.
            base = parse_preset(ref_path, root)
            # The base contributes its passes; this preset can override/add.
            # For feature flags we OR them; for counts we MAX.
            rep.passes = base.passes
            rep.aliases = base.aliases
            rep.uses_original_history = base.uses_original_history
            rep.max_history_depth = base.max_history_depth
            rep.uses_pass_feedback = base.uses_pass_feedback
            rep.max_pass_feedback = base.max_pass_feedback
            rep.uses_pass_output = base.uses_pass_output
            rep.uses_user_lut = base.uses_user_lut
            rep.uses_named_lut = base.uses_named_lut
            rep.named_luts = base.named_luts
            rep.uses_frame_count_mod = base.uses_frame_count_mod
            rep.uses_float_fb = base.uses_float_fb
            rep.uses_srgb_fb = base.uses_srgb_fb
            rep.uses_mipmap_input = base.uses_mipmap_input
            rep.uses_absolute_scale = base.uses_absolute_scale
            rep.uses_viewport_scale = base.uses_viewport_scale
            rep.distinct_lut_files = base.distinct_lut_files
            rep.feedback_pass = base.feedback_pass

    # Pass count
    m = RE_SHADERS.search(text)
    if m:
        rep.passes = max(rep.passes, int(m.group(1)))

    # Aliases
    rep.aliases += len(RE_ALIAS.findall(text))

    # feedback_pass
    m = RE_FEEDBACK.search(text)
    if m:
        rep.feedback_pass = int(m.group(1))

    # frame_count_mod
    if RE_FCMOD.search(text):
        rep.uses_frame_count_mod = True

    # Scale types
    for sm in RE_SCALE_TYPE.finditer(text):
        t = sm.group(1).lower()
        if t == "absolute":
            rep.uses_absolute_scale = True
        elif t == "viewport":
            rep.uses_viewport_scale = True

    # Framebuffer flags
    if RE_FLOAT_FB.search(text):
        rep.uses_float_fb = True
    if RE_SRGB_FB.search(text):
        rep.uses_srgb_fb = True
    if RE_MIPMAP.search(text):
        rep.uses_mipmap_input = True

    # LUTs
    m = RE_TEXTURE_LUT.search(text)
    if m:
        rep.uses_named_lut = True
        names = [n.strip() for n in m.group(1).split(";") if n.strip()]
        rep.named_luts = max(rep.named_luts, len(names))
        # Count distinct .png paths
        png_paths = set()
        for lut_match in RE_LUT_PATH.finditer(text):
            png_paths.add(lut_match.group(2))
        rep.distinct_lut_files = max(rep.distinct_lut_files, len(png_paths))

    # Walk every shader referenced by this preset, accumulating shader-side features.
    shader_files: set[Path] = set()
    shader_texts: list[str] = []
    for sm in RE_SHADER_N.finditer(text):
        rel = sm.group(1)
        spath = (preset_path.parent / rel).resolve()
        if not spath.exists():
            continue
        shader_files.add(spath)
        stext = read_text(spath)
        shader_texts.append(stext)
        # Recurse into #include
        visited = {spath}
        stack = [(spath, stext, 1)]
        max_depth = 1
        while stack:
            cur_path, cur_text, depth = stack.pop()
            max_depth = max(max_depth, depth)
            for inc in RE_INCLUDE.finditer(cur_text):
                inc_rel = inc.group(1)
                inc_path = (cur_path.parent / inc_rel).resolve()
                if inc_path.exists() and inc_path not in visited:
                    visited.add(inc_path)
                    shader_files.add(inc_path)
                    inc_text = read_text(inc_path)
                    shader_texts.append(inc_text)
                    stack.append((inc_path, inc_text, depth + 1))
        rep.max_include_depth = max(rep.max_include_depth, max_depth)
        rep.uses_includes = rep.uses_includes or max_depth > 1

    rep.distinct_shader_files = len(shader_files)

    # Walk shader text for slang feature usage
    all_shader_text = "\n".join(shader_texts)
    if RE_PRAGMA_PARAM.search(all_shader_text):
        rep.uses_pragma_param = True
        rep.pragma_param_count = len(RE_PRAGMA_PARAM.findall(all_shader_text))
    if RE_PRAGMA_FORMAT.search(all_shader_text):
        rep.uses_pragma_format = True
    if RE_MVP_USAGE.search(all_shader_text):
        rep.uses_mvp = True
    if RE_FRAME_COUNT.search(all_shader_text):
        rep.uses_frame_count = True
    if RE_FRAME_DIR.search(all_shader_text):
        rep.uses_frame_direction = True
    if RE_FRAME_TIME.search(all_shader_text):
        rep.uses_frame_time = True
    if RE_PASS_INDEX.search(all_shader_text):
        rep.uses_pass_index = True

    # PassOutput / PassFeedback / OriginalHistory / User# (across both .slangp and .slang)
    full_text = text + "\n" + all_shader_text
    if RE_PASS_OUTPUT.search(full_text):
        rep.uses_pass_output = True
    feedback_matches = RE_PASS_FEEDBACK.findall(full_text)
    if feedback_matches:
        rep.uses_pass_feedback = True
        rep.max_pass_feedback = max(int(n) for n in feedback_matches)
    history_matches = RE_ORIG_HISTORY.findall(full_text)
    if history_matches:
        rep.uses_original_history = True
        rep.max_history_depth = max(int(n) for n in history_matches)
    if RE_USER_LUT.search(full_text):
        rep.uses_user_lut = True

    # Risk heuristic (informs which presets to test first)
    reasons = []
    if rep.passes >= 6:
        reasons.append(f"{rep.passes} passes (long chain)")
    if rep.uses_original_history and rep.max_history_depth >= 4:
        reasons.append(f"OriginalHistory{rep.max_history_depth} (deep history)")
    if rep.uses_pass_feedback:
        reasons.append("PassFeedback (double-buffer)")
    if rep.uses_named_lut and rep.distinct_lut_files >= 4:
        reasons.append(f"{rep.distinct_lut_files} LUTs (heavy)")
    if rep.uses_absolute_scale:
        reasons.append("absolute-scale pass")
    if rep.uses_viewport_scale:
        reasons.append("viewport-scale pass")
    if rep.uses_float_fb:
        reasons.append("float framebuffer")
    if rep.uses_srgb_fb:
        reasons.append("sRGB framebuffer")
    if rep.uses_mipmap_input:
        reasons.append("mipmap_input")
    if rep.uses_includes and rep.max_include_depth >= 4:
        reasons.append(f"#include depth {rep.max_include_depth}")
    if rep.uses_pragma_param and rep.pragma_param_count >= 8:
        reasons.append(f"{rep.pragma_param_count} #pragma parameters")
    if rep.has_reference:
        reasons.append("#reference (inherits)")
    if not reasons:
        rep.risk = "low"
    elif len(reasons) <= 2:
        rep.risk = "medium"
    else:
        rep.risk = "high"
    rep.risk_reasons = reasons

    return rep


def main():
    if len(sys.argv) != 2:
        sys.exit("usage: audit_slang_presets.py <slang-shaders-root>")
    root = Path(sys.argv[1]).resolve()
    if not root.is_dir():
        sys.exit(f"not a directory: {root}")
    preset_paths = sorted(root.rglob("*.slangp"))
    print(f"# Found {len(preset_paths)} .slangp presets under {root}", file=sys.stderr)
    if not preset_paths:
        sys.exit("no .slangp files found")

    writer = csv.writer(sys.stdout)
    writer.writerow([
        "preset", "passes", "reference", "aliases", "orig_hist", "max_hist",
        "pass_fb", "max_pass_fb", "pass_output", "user_lut", "named_lut",
        "named_lut_count", "distinct_luts", "fcmod", "float_fb", "srgb_fb",
        "mipmap_input", "abs_scale", "viewport_scale", "pragma_param",
        "pragma_param_count", "pragma_format", "uses_mvp", "uses_frame_count",
        "uses_frame_dir", "uses_frame_time", "uses_pass_index", "uses_includes",
        "max_include_depth", "distinct_shaders", "feedback_pass", "risk",
        "risk_reasons",
    ])
    risk_counts = defaultdict(int)
    for p in preset_paths:
        try:
            rep = parse_preset(p, root)
        except Exception as e:
            print(f"WARN: parse failed for {p}: {e}", file=sys.stderr)
            continue
        writer.writerow([
            rep.rel_path, rep.passes, rep.has_reference, rep.aliases,
            rep.uses_original_history, rep.max_history_depth,
            rep.uses_pass_feedback, rep.max_pass_feedback, rep.uses_pass_output,
            rep.uses_user_lut, rep.uses_named_lut, rep.named_luts,
            rep.distinct_lut_files, rep.uses_frame_count_mod, rep.uses_float_fb,
            rep.uses_srgb_fb, rep.uses_mipmap_input, rep.uses_absolute_scale,
            rep.uses_viewport_scale, rep.uses_pragma_param, rep.pragma_param_count,
            rep.uses_pragma_format, rep.uses_mvp, rep.uses_frame_count,
            rep.uses_frame_direction, rep.uses_frame_time, rep.uses_pass_index,
            rep.uses_includes, rep.max_include_depth, rep.distinct_shader_files,
            rep.feedback_pass, rep.risk, "; ".join(rep.risk_reasons),
        ])
        risk_counts[rep.risk] += 1
    print(f"# Risk distribution: low={risk_counts['low']}, "
          f"medium={risk_counts['medium']}, high={risk_counts['high']}", file=sys.stderr)


if __name__ == "__main__":
    main()