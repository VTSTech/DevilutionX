#!/usr/bin/env python3
"""Diff two corpus translate sweeps (baseline vs fixed) and classify deltas."""
import csv
import sys

with open(sys.argv[1], newline="") as fh:
    base = {r["preset"]: r for r in csv.DictReader(fh, delimiter="\t")}
with open(sys.argv[2], newline="") as fh:
    fixed = {r["preset"]: r for r in csv.DictReader(fh, delimiter="\t")}

assert base.keys() == fixed.keys(), "preset sets differ"

status_changes = []
size_changes = []
for p in base:
    b, f = base[p], fixed[p]
    if b["status"] != f["status"]:
        status_changes.append((p, b["status"], f["status"]))
    elif b["merged_sizes"] != f["merged_sizes"]:
        size_changes.append((p, b["merged_sizes"], f["merged_sizes"]))

print(f"status changes: {len(status_changes)}")
for p, bs, fs in status_changes:
    print(f"  STATUS {p}: {bs} -> {fs}")
print(f"merged-size changes (status unchanged): {len(size_changes)}")
koko = [p for p, _, _ in size_changes if "koko" in p.lower()]
print(f"  of which koko: {len(koko)}")
for p in koko:
    print(f"    KOKO {p}")
for p, bs, fs in size_changes[:25]:
    if "koko" not in p.lower():
        print(f"    SIZE {p}")
        print(f"      before: {bs[:150]}")
        print(f"      after : {fs[:150]}")
