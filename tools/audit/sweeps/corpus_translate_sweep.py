#!/usr/bin/env python3
"""Corpus translate-stage sweep for slang presets (field round 4).

Runs translate_preflight over every preset in docs/audit/slang_audit_v2.csv
and records, per preset and per pass: translation status and mergedSize.
The mergedSize signature is what detects include-resolution changes: the
round-4 per-stage-bucket include-once fix only changes merged output where a
shader re-includes a file across stage buckets.

Usage:
  corpus_translate_sweep.py --tool <translate_preflight> --out <tsv> \
      --root <shader root> [--jobs N] [--filter SUBSTR]

TSV columns: preset, passes, ok_passes, translate_failed, merged_sizes
"""
import argparse
import concurrent.futures as cf
import csv
import re
import subprocess
import sys

PASS_RE = re.compile(r"^  PASS (\d+)/(\d+) ")
OK_RE = re.compile(r"translated=OK")
ERR_RE = re.compile(r"translated=ERROR")
SIZE_RE = re.compile(r"mergedSize=(\d+)")


def sweep_one(tool: str, root: str, preset: str):
    cmd = [tool, f"{root}/{preset}"]
    try:
        proc = subprocess.run(cmd, capture_output=True, text=True, timeout=600)
    except subprocess.TimeoutExpired:
        return preset, -1, 0, "TIMEOUT", ""
    ok = err = 0
    sizes = []
    npass = 0
    for line in proc.stdout.splitlines():
        m = PASS_RE.match(line)
        if m:
            npass = int(m.group(2))
            continue
        if OK_RE.search(line):
            ok += 1
            sm = SIZE_RE.search(line)
            if sm:
                sizes.append(int(sm.group(1)))
        elif ERR_RE.search(line):
            err += 1
            sizes.append(-1)
    status = "OK" if err == 0 and proc.returncode == 0 else f"FAIL(rc={proc.returncode})"
    return preset, npass, ok, status, ",".join(str(s) for s in sizes)


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--tool", required=True)
    ap.add_argument("--out", required=True)
    ap.add_argument("--root", required=True)
    ap.add_argument("--csv", default="docs/audit/slang_audit_v2.csv")
    ap.add_argument("--jobs", type=int, default=8)
    ap.add_argument("--filter", default="")
    ap.add_argument("--part", type=int, default=0, help="round-robin part index")
    ap.add_argument("--parts", type=int, default=1, help="round-robin part count")
    args = ap.parse_args()

    with open(args.csv, newline="") as fh:
        presets = [row["preset"] for row in csv.DictReader(fh)]
    if args.filter:
        presets = [p for p in presets if args.filter in p]
    if args.parts > 1:
        presets = presets[args.part::args.parts]
    print(f"sweeping {len(presets)} presets with {args.jobs} jobs", flush=True)

    results = []
    with cf.ThreadPoolExecutor(max_workers=args.jobs) as pool:
        futs = [pool.submit(sweep_one, args.tool, args.root, p) for p in presets]
        done = 0
        for fut in cf.as_completed(futs):
            results.append(fut.result())
            done += 1
            if done % 200 == 0:
                print(f"  {done}/{len(presets)}", flush=True)

    results.sort(key=lambda r: r[0])
    with open(args.out, "w", newline="") as fh:
        w = csv.writer(fh, delimiter="\t")
        w.writerow(["preset", "passes", "ok_passes", "status", "merged_sizes"])
        w.writerows(results)

    fails = [r for r in results if not r[3].startswith("OK")]
    print(f"done: {len(results)} presets, {len(fails)} translate-stage failures")
    for r in fails[:20]:
        print("  FAIL", r[0], r[3])


if __name__ == "__main__":
    sys.exit(main())
