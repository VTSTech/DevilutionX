#!/usr/bin/env python3
"""Quantify code-audit findings against the real libretro/slang-shaders tree.

Findings under test (from static audit of fork master 2a30fca65):
  P1  parser: bare `scale` AFTER `scale_type*` for the same pass index resets
      the type to Source (key order dependency; RetroArch is order-independent).
  P2  parser: undeclared implicit texture key with a trailing digit suffix
      (e.g. `mask1 = "x.png"` not in textures=) is eaten by SplitPassKey and
      silently dropped.
  T2  translate: trailing // comment on a `uniform sampler2D ...;` line breaks
      the line-anchored rewrite regex -> undeclared identifier compile error.
  T3  translate: vertex-stage sampler2D declarations -> hard load failure.
  T4  translate: sampler arrays (`sampler2D name[N];`) -> hard load failure.
  T5  translate: >1 plain UBO in one stage -> hard load failure (spec: max 1).
  F9  run: LUT/alias case mismatch between .slangp declaration and .slang
      sampler name passes translation (case-insensitive) but fails the
      case-sensitive Run() binding -> silent source-texture substitution.
  F8  preset: both srgb_framebuffer and float_framebuffer on one pass
      (runner prefers float silently; matches RetroArch, but note it).

Emit: per-finding counts + affected preset lists (CSV out).
"""

import os
import re
import csv
import sys
from collections import Counter, defaultdict

ROOT = "/home/z/my-project/reference/slang-shaders"
OUT = "/home/z/my-project/download/slang-codefindings-scan.csv"

IMG_EXTS = (".png", ".jpg", ".jpeg", ".bmp", ".tga", ".dds", ".webp")

samp_re = re.compile(r"^\s*(?:layout\s*\([^)]*\)\s*)?uniform\s+(?:lowp\s+|mediump\s+|highp\s+)?sampler2D\s+(.+?)\s*;", re.M)
stage_re = re.compile(r"^\s*#pragma\s+stage\s+(vertex|fragment)\s*$", re.M)
include_re = re.compile(r'^\s*#include\s+"([^"]+)"')
block_re = re.compile(r"(layout\s*\([^;{]*?\)\s*)?uniform\s+(\w+)\s*\{")


def strip_comment(line):
    # Approximate the runner's comment handling: '#' outside quotes starts a
    # comment; '#reference' at line start is protected.
    in_q = False
    seen_ns = False
    for i, c in enumerate(line):
        if c == '"':
            in_q = not in_q
        elif c == "#" and not in_q:
            if not seen_ns and line.startswith("#reference", i):
                return line
            return line[:i]
        elif not in_q and c not in " \t":
            seen_ns = True
    return line


def parse_preset_keys(path):
    """Return (ordered [(key, value)], reference_paths) like the runner does."""
    kvs = []
    refs = []
    try:
        with open(path, "r", encoding="utf-8", errors="replace") as fh:
            for raw in fh:
                line = strip_comment(raw.rstrip("\n"))
                m = re.match(r'^\s*#reference\s+"([^"]+)"\s*$', line)
                if m:
                    refs.append(m.group(1))
                    continue
                eq = line.find("=")
                if eq < 0:
                    continue
                key = line[:eq].strip().lower()
                val = line[eq + 1:].strip()
                if val.startswith('"'):
                    close = val.find('"', 1)
                    val = val[1:close] if close > 0 else val.strip('"')
                kvs.append((key, val))
    except OSError:
        pass
    return kvs, refs


def resolve_file(preset_dir, ref, root):
    """Approximate ResolvePresetFilePath: preset dir, then 6 ancestors, then root."""
    ref = ref.replace("\\", "/")
    if os.path.isabs(ref) or (len(ref) > 2 and ref[1] == ":"):
        return ref if os.path.exists(ref) else ref
    d = os.path.normpath(preset_dir)
    bases = [d]
    for _ in range(6):
        parent = os.path.dirname(d)
        if parent == d or not parent:
            break
        d = parent
        bases.append(d)
    bases.append(root)
    for b in bases:
        cand = os.path.normpath(os.path.join(b, ref))
        if os.path.exists(cand):
            return cand
    return os.path.normpath(os.path.join(preset_dir, ref))


def resolve_shaders(preset_path):
    """Return (pass_count, [resolved shader paths], aliases, lut_names,
    per-pass scale info, reference_resolved) for one .slangp (with #reference)."""
    preset_dir = os.path.dirname(os.path.abspath(preset_path))
    kvs, refs = parse_preset_keys(preset_path)
    # Merge parents first (first chain with passes wins; textures/params accumulate)
    textures = {}   # lower name -> dict(path, declared)
    texture_order = []
    aliases = {}    # idx -> alias name
    shader_paths = {}
    scale_info = {}  # idx -> list of (line, key) for scale/scale_type keys
    for ref in refs:
        parent_path = resolve_file(preset_dir, ref, ROOT)
        if os.path.exists(parent_path):
            p_kvs, p_refs = parse_preset_keys(parent_path)
            # parent's own references (depth-2 inheritance) - flat follow
            for r2 in p_refs:
                gp = resolve_file(os.path.dirname(parent_path), r2, ROOT)
                if os.path.exists(gp):
                    g_kvs, _ = parse_preset_keys(gp)
                    _merge(g_kvs, textures, texture_order, aliases, shader_paths, scale_info, os.path.dirname(gp))
            _merge(p_kvs, textures, texture_order, aliases, shader_paths, scale_info, os.path.dirname(parent_path))
    _merge(kvs, textures, texture_order, aliases, shader_paths, scale_info, preset_dir, is_top=True)
    count = 0
    for key, _ in kvs:
        m = re.match(r'^shaders\s*=\s*"?(\d+)"?$', key) if False else None
    # shader count: max(declared count, highest shaderN)
    declared = 0
    for key, val in kvs:
        if key == "shaders":
            try:
                declared = max(declared, int(val.strip('"')))
            except ValueError:
                pass
    for idx in shader_paths:
        count = max(count, idx + 1)
    count = max(count, declared)
    resolved = []
    for i in range(count):
        p = shader_paths.get(i)
        resolved.append(resolve_file(preset_dir, p, ROOT) if p else None)
    ref_resolved = all(r is not None for r in resolved) and count > 0
    return count, resolved, aliases, textures, scale_info, ref_resolved


def _merge(kvs, textures, texture_order, aliases, shader_paths, scale_info, base_dir, is_top=False):
    tex_names = []
    for key, val in kvs:
        if key == "textures":
            tex_names = [t.strip() for t in val.split(";") if t.strip()]
    lower_map = {t.lower(): t for t in tex_names}
    for name in tex_names:
        if name.lower() not in textures:
            textures[name.lower()] = {"path": "", "declared": name}
            texture_order.append(name.lower())
    for key, val in kvs:
        m = re.match(r"^alias(\d+)$", key)
        if m:
            aliases[int(m.group(1))] = val
            continue
        m = re.match(r"^shader(\d+)$", key)
        if m:
            shader_paths[int(m.group(1))] = val
            continue
        # texture path keys (textures dict is keyed by LOWERCASE name)
        if key in lower_map:
            textures[key]["path"] = val
            continue
        matched = False
        for lname in lower_map:
            if key == lname + "_path":
                textures[lname]["path"] = val
                matched = True
        if matched:
            continue
        # implicit image key (only when it can't be a pass key)
        m = re.match(r"^([a-z0-9_]+?)(\d+)$", key)
        if val.lower().endswith(IMG_EXTS) and not m:
            if key not in textures:
                textures[key] = {"path": val, "declared": key}
                texture_order.append(key)


def slang_facts(shader_path, _cache={}):
    """Scan one .slang (with includes) for the translator findings."""
    if shader_path in _cache:
        return _cache[shader_path]
    facts = {
        "vertex_samplers": 0,
        "array_samplers": 0,
        "trailing_comment_samplers": 0,
        "multi_ubo": False,
        "sampler_names": set(),
        "size_members": set(),
    }
    text = ""
    try:
        with open(shader_path, "r", encoding="utf-8", errors="replace") as fh:
            text = fh.read()
    except OSError:
        _cache[shader_path] = facts
        return facts

    # Resolve includes (flat, cycle-safe) so vertex-stage samplers inside
    # includes are caught too.
    merged_lines = []
    seen = set()
    base = os.path.dirname(shader_path)

    def walk(path):
        rp = os.path.realpath(path)
        if rp in seen:
            return
        seen.add(rp)
        try:
            with open(path, "r", encoding="utf-8", errors="replace") as fh:
                lines = fh.readlines()
        except OSError:
            return
        for ln in lines:
            ln = ln.rstrip("\r\n")
            m = include_re.match(ln)
            if m:
                cand = os.path.join(base, m.group(1))
                if not os.path.exists(cand):
                    cand = os.path.join(os.path.dirname(path), m.group(1))
                walk(cand)
                continue
            if ln.startswith("#version"):
                continue
            merged_lines.append(ln)

    walk(shader_path)

    stage = "shared"
    ubo_stages = defaultdict(int)
    for ln in merged_lines:
        sm = stage_re.match(ln)
        if sm:
            stage = sm.group(1)
            continue
        if "uniform" in ln and "sampler2D" in ln and ln.strip().endswith(";"):
            body = ln.split(";")[0]
            # trailing comment check: text after the ';' that isn't whitespace
            after = ln.split(";", 1)[1]
            has_trailing_comment = "//" in after or "/*" in after
            names_part = re.sub(r"^\s*(?:layout\s*\([^)]*\)\s*)?uniform\s+(?:lowp\s+|mediump\s+|highp\s+)?sampler2D\s+", "", body)
            for nm in names_part.split(","):
                nm = nm.strip()
                if not nm:
                    continue
                if "[" in nm:
                    facts["array_samplers"] += 1
                if has_trailing_comment:
                    facts["trailing_comment_samplers"] += 1
                facts["sampler_names"].add(nm.split("[")[0].strip())
                if stage in ("vertex", "shared"):
                    # 'shared' reaches both stages; count only explicit vertex
                    if stage == "vertex":
                        facts["vertex_samplers"] += 1
            continue
        bm = block_re.search(ln)
        if bm:
            is_push = bm.group(1) and "push_constant" in bm.group(1)
            if not is_push:
                # count the stage attribution crudely: UBOs before any stage
                # pragma belong to 'shared' (both stages).
                ubo_stages[stage] += 1
        msz = re.match(r"^\s*(\w+)Size\s*(?:,|$|\[)", ln) or re.match(r"^\s*\w+\s+(\w+Size)\s*;", ln)
        if msz:
            facts["size_members"].add(msz.group(1))
    # UBO limit: more than 1 plain UBO per stage (shared counts for both)
    for stg, cnt in ubo_stages.items():
        if stg == "shared" and cnt > 1:
            facts["multi_ubo"] = True
        if stg in ("vertex", "fragment") and cnt > 1:
            facts["multi_ubo"] = True
    _cache[shader_path] = facts
    return facts


def main():
    presets = []
    for dirpath, _dirnames, filenames in os.walk(ROOT):
        for fn in filenames:
            if fn.lower().endswith(".slangp"):
                presets.append(os.path.join(dirpath, fn))
    print(f"scanning {len(presets)} presets under {ROOT}")

    findings = defaultdict(list)  # finding -> [preset relpaths]
    rows = []
    for pp in sorted(presets):
        rel = os.path.relpath(pp, ROOT)
        try:
            count, shaders, aliases, textures, scale_info, _rr = resolve_shaders(pp)
        except Exception as e:  # noqa: BLE001
            findings["scan_error"].append(f"{rel}: {e}")
            continue
        if count == 0 or not any(shaders):
            findings["zero_pass"].append(rel)
            continue

        # --- P1: bare `scale` AFTER a NON-SOURCE `scale_type*` (same pass
        # index, top file). Only the BARE scale key clobbers the type back to
        # Source; scale_x/scale_y touch the value only and are always safe.
        # Harmless when the declared type was "source" anyway.
        kvs, _ = parse_preset_keys(pp)
        by_idx = defaultdict(lambda: {"bare": -1, "stype": -1, "nonsource": False})
        for ln_no, (key, val) in enumerate(kvs):
            m = re.match(r"^scale(\d+)$", key)
            if m:
                by_idx[int(m.group(1))]["bare"] = ln_no
                continue
            m = re.match(r"^scale_type(?:_x|_y)?(\d+)$", key)
            if m:
                idx = int(m.group(1))
                prev = by_idx[idx]["stype"]
                by_idx[idx]["stype"] = ln_no if prev < 0 else min(prev, ln_no)
                if val.strip('"').strip().lower() in ("viewport", "absolute"):
                    by_idx[idx]["nonsource"] = True
        for idx, d in by_idx.items():
            if d["nonsource"] and d["bare"] >= 0 and d["stype"] >= 0 and d["bare"] > d["stype"]:
                findings["P1_scale_order"].append(f"{rel} (pass {idx})")

        # --- per-preset shader scans ----------------------------------------
        shader_set = [s for s in dict.fromkeys(s for s in shaders if s) ]
        lut_names = {v["declared"] for v in textures.values() if v["path"]}
        alias_names = set(aliases.values())
        known = {n.lower() for n in lut_names | alias_names}
        preset_flags = set()
        for sp in shader_set:
            facts = slang_facts(sp)
            if facts["vertex_samplers"]:
                preset_flags.add("T3_vertex_sampler")
            if facts["array_samplers"]:
                preset_flags.add("T4_sampler_array")
            if facts["trailing_comment_samplers"]:
                preset_flags.add("T2_trailing_comment")
            if facts["multi_ubo"]:
                preset_flags.add("T5_multi_ubo")
            # F9: case mismatch between shader sampler names and known names.
            # NOTE: `NAMEFeedback` is the feedback form of alias/LUT `NAME` —
            # strip the suffix before comparing case, else every feedback
            # reference false-positives.
            for nm in facts["sampler_names"]:
                base = nm[:-8] if nm.endswith("Feedback") else nm
                low = base.lower()
                if low in known:
                    for decl in lut_names | alias_names:
                        if decl.lower() == low and decl != base:
                            preset_flags.add("F9_case_mismatch")
        # --- F8: both srgb and float on any pass ----------------------------
        kvs_all, _ = parse_preset_keys(pp)
        flags_by_idx = defaultdict(set)
        for key, val in kvs_all:
            m = re.match(r"^(srgb_framebuffer|float_framebuffer)(\d+)$", key)
            if m and val.strip('"').lower() in ("true", "1", "on", "yes"):
                flags_by_idx[int(m.group(2))].add(m.group(1))
        for idx, fl in flags_by_idx.items():
            if len(fl) == 2:
                preset_flags.add("F8_srgb_and_float")
        for flag in preset_flags:
            findings[flag].append(rel)
        rows.append({"preset": rel, "flags": ";".join(sorted(preset_flags))})

    print("\n=== finding counts (presets affected) ===")
    for k in sorted(findings):
        print(f"{k}: {len(findings[k])}")
    zero = len(findings.get("zero_pass", []))
    print(f"(context) zero-pass presets skipped: {zero}")

    print("\n=== P1 affected presets (scale order clobber) ===")
    for x in findings.get("P1_scale_order", [])[:20]:
        print("  " + x)

    print("\n=== F9 affected presets (case-mismatch silent substitution) ===")
    for x in findings.get("F9_case_mismatch", [])[:20]:
        print("  " + x)

    print("\n=== T2 affected presets (trailing-comment sampler lines) ===")
    for x in findings.get("T2_trailing_comment", [])[:20]:
        print("  " + x)

    print("\n=== T3/T4/T5 (hard load failures today) ===")
    for k in ("T3_vertex_sampler", "T4_sampler_array", "T5_multi_ubo"):
        for x in findings.get(k, [])[:10]:
            print(f"  [{k}] {x}")

    with open(OUT, "w", newline="", encoding="utf-8") as fh:
        w = csv.DictWriter(fh, fieldnames=["preset", "flags"])
        w.writeheader()
        w.writerows(rows)
    print(f"\nwrote {OUT}")
    return 0


if __name__ == "__main__":
    sys.exit(main())
