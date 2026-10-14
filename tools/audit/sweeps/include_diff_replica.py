#!/usr/bin/env python3
"""Replicate old (file-lifetime) vs new (per-bucket) include resolution and diff."""
import re
import sys
from pathlib import Path

inc_re = re.compile(r'^\s*#include\s+"([^"]+)"\s*$')
stage_re = re.compile(r'^\s*#pragma\s+stage\s+(vertex|fragment)\s*$')


def resolve(path, depth, new_algo, visited, active, stage, out):
    """new_algo: True = per-bucket visited + active stack; False = global visited."""
    if depth > 24:
        return None, "depth"
    key = str(Path(path).resolve())
    if new_algo and key in active:
        return None, None  # dropped: active cycle guard
    was_new = key not in visited
    if was_new:
        if new_algo:
            visited[stage[0]].add(key)
            active.append(key)
        else:
            visited.add(key)
    text = Path(path).read_text()
    lines = text.split("\n")
    if lines and lines[-1] == "":
        lines.pop()
    for line in lines:
        if line.startswith("#version"):
            continue
        m = stage_re.match(line)
        if m and new_algo:
            stage[0] = 1 if m.group(1) == "vertex" else 2
            out.append(line)
            continue
        m = inc_re.match(line)
        if m:
            inc = m.group(1)
            cand = Path(path).parent / inc
            if not cand.exists():
                cand = Path(path).parent.parent / inc  # fallback = primary dir
            ck = str(cand.resolve())
            if new_algo:
                if ck in visited[stage[0]]:
                    continue
            if new_algo:
                out.append(f"// begin include: {inc}")
                r, err = resolve(str(cand), depth + 1, new_algo, visited, active, stage, out)
                if err:
                    return None, err
                out.append(f"// end include: {inc}")
            else:
                ck = str(cand.resolve())
                if ck in visited:
                    continue
                out.append(f"// begin include: {inc}")
                r, err = resolve(str(cand), depth + 1, new_algo, visited, active, stage, out)
                if err:
                    return None, err
                out.append(f"// end include: {inc}")
        else:
            out.append(line)
    if new_algo and was_new:
        active.pop()
    return out, None


def merged(path, new_algo):
    visited = [set(), set(), set()] if new_algo else set()
    out, err = resolve(path, 0, new_algo, visited, [], [0], [])
    if err:
        return None
    return "\n".join(out) + "\n"


path = sys.argv[1]
old = merged(path, False)
new = merged(path, True)
print(f"old len={len(old)} new len={len(new)} delta={len(new)-len(old)}")
import difflib
diff = list(difflib.unified_diff(old.splitlines(), new.splitlines(), lineterm="", n=1))
print("\n".join(diff[:60]))
