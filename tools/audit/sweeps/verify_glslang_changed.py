#!/usr/bin/env python3
"""Dump + glslang-compile every stage of the listed presets; per-preset TSV out."""
import argparse
import concurrent.futures as cf
import csv
import shutil
import subprocess
import tempfile
from pathlib import Path

GLSLANG = "/tmp/glslang-local/usr/bin/glslangValidator"


def run_one(idx, tool, root, preset):
    work = Path(tempfile.mkdtemp(prefix=f"v{idx:04d}-"))
    try:
        r = subprocess.run([tool, "-dump", str(work), f"{root}/{preset}"],
                           capture_output=True, text=True, timeout=900)
        if r.returncode != 0:
            return preset, -1, 0, "DUMP-FAIL"
        files = sorted(work.glob("*.glsl"))
        fails = 0
        first_err = ""
        for f in files:
            stage = "frag" if f.name.endswith(".frag.glsl") else "vert"
            c = subprocess.run([GLSLANG, "--glsl-version", "450", "-V", "-S", stage,
                                str(f), "-o", "/dev/null"],
                               capture_output=True, text=True, timeout=300)
            if c.returncode != 0:
                fails += 1
                if not first_err:
                    for line in (c.stderr or "").splitlines():
                        if "ERROR" in line:
                            first_err = line.strip()
                            break
        return preset, len(files), fails, first_err
    except subprocess.TimeoutExpired:
        return preset, -1, 1, "TIMEOUT"
    finally:
        shutil.rmtree(work, ignore_errors=True)


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--tool", required=True)
    ap.add_argument("--presets", required=True)
    ap.add_argument("--out", required=True)
    ap.add_argument("--root", required=True)
    ap.add_argument("--jobs", type=int, default=8)
    args = ap.parse_args()

    presets = [l.strip() for l in open(args.presets) if l.strip()]
    print(f"verifying {len(presets)} presets with {args.tool}", flush=True)
    rows = []
    with cf.ThreadPoolExecutor(max_workers=args.jobs) as pool:
        futs = {pool.submit(run_one, i, args.tool, args.root, p): p
                for i, p in enumerate(presets)}
        done = 0
        for fut in cf.as_completed(futs):
            rows.append(fut.result())
            done += 1
            if done % 50 == 0:
                print(f"  {done}/{len(presets)}", flush=True)

    rows.sort(key=lambda r: r[0])
    with open(args.out, "w", newline="") as fh:
        w = csv.writer(fh, delimiter="\t")
        w.writerow(["preset", "stages", "compile_fails", "first_error"])
        w.writerows(rows)
    bad = [r for r in rows if r[2] != 0]
    print(f"done: {len(rows)} presets, {len(bad)} with compile failures")


if __name__ == "__main__":
    main()
