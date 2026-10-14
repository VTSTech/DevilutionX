#!/usr/bin/env python3
"""status_report.py - brief status report for commits / release notes.

Aggregates the artifacts the audit workflow already produces into a short
markdown report (stdout or --out):

  - a fresh translate-stage corpus sweep  (tools/audit/sweeps/corpus_translate_sweep.py
    TSVs: preset, passes, ok_passes, status, merged_sizes)
  - an optional baseline sweep of the same shape -> fixed / regressed /
    merged-size-delta counts
  - one or more gpu-scaler.log field logs -> preset loads, failures,
    Reinit / hot-switch / cache-hit events, backend, last heartbeat
  - the tiered field-test list (docs/audit/shader-test-list-*.csv) -> how
    many presets are verified / failing / pending (its status column)
  - docs/audit/slang_audit_v2.csv -> risk classification of sweep failures
  - git metadata (branch, commit, tag, dirty) from the repo this script
    lives in

Everything is optional except --sweep: missing inputs are skipped with a
one-line "(no data)" so the report stays brief and never fabricates.

Usage (see README or -h):
  status_report.py --sweep current.tsv --baseline baseline.tsv \
      --field-log gpu-scaler.log --note "known keeps: #9 #11 #14" \
      --out STATUS.md
"""
import argparse
import csv
import datetime
import os
import re
import subprocess
import sys

REPO = os.path.dirname(os.path.dirname(os.path.dirname(os.path.abspath(__file__))))


def report_date():
    """Generation date in the maintainer's timezone (fallback UTC)."""
    try:
        from zoneinfo import ZoneInfo
        return datetime.datetime.now(ZoneInfo("America/Toronto")).date().isoformat()
    except Exception:
        return datetime.datetime.now(datetime.timezone.utc).date().isoformat()


# ---------------------------------------------------------------- sweeps ----
def read_sweep(path):
    """TSV -> {preset: {passes, ok, status, sizes:[int]}}."""
    out = {}
    with open(path, newline="") as fh:
        for row in csv.DictReader(fh, delimiter="\t"):
            sizes = [int(s) for s in (row.get("merged_sizes") or "").split(",") if s]
            out[row["preset"]] = {
                "passes": int(row.get("passes") or 0),
                "ok": int(row.get("ok_passes") or 0),
                "status": (row.get("status") or "").strip(),
                "sizes": sizes,
            }
    return out


def merge_sweeps(paths):
    merged = {}
    for p in paths:
        merged.update(read_sweep(p))
    return merged


def preset_ok(rec):
    return rec["status"].startswith("OK") and rec["passes"] > 0 and rec["ok"] == rec["passes"]


def sweep_summary(sweep, risks):
    ok = {p: r for p, r in sweep.items() if preset_ok(r)}
    bad = {p: r for p, r in sweep.items() if not preset_ok(r)}
    passes = sum(r["passes"] for r in ok.values())
    ok_passes = sum(r["ok"] for r in ok.values())
    risk_mix = {}
    if risks:
        for p in bad:
            risk_mix[risks.get(p, "?")] = risk_mix.get(risks.get(p, "?"), 0) + 1
    return ok, bad, passes, ok_passes, risk_mix


def short_name(preset):
    """basename for brief listing (ambiguity is rare and counted via dir groups)."""
    return os.path.basename(preset)


def dir_groups(names, limit=3):
    """count failures by top-level corpus directory."""
    groups = {}
    for n in names:
        top = n.split("/")[0] if "/" in n else "(root)"
        groups[top] = groups.get(top, 0) + 1
    out = sorted(groups.items(), key=lambda kv: -kv[1])
    return out[:limit], len(out)


FAIL_REASON_RE = re.compile(r"TRANSLATE-FAIL: (.*)")


def probe_failures(tool, root, names, timeout=120):
    """re-run the translate tool on failures; return reason-class counts.

    Reasons are normalized (quoted identifiers -> '...', path-like tokens ->
    <path>, digits -> N) so a handful of classes cover the whole corpus.
    """
    classes = {}
    for p in names:
        try:
            proc = subprocess.run([tool, f"{root}/{p}"], capture_output=True,
                                  text=True, timeout=timeout)
        except Exception:
            continue
        m = FAIL_REASON_RE.search(proc.stdout)
        if not m:
            continue
        cls = re.sub(r"'[^']*'", "'...'", m.group(1).strip())
        cls = re.sub(r"\S*/\S*", "<path>", cls)
        cls = re.sub(r"\d+", "N", cls)
        classes[cls] = classes.get(cls, 0) + 1
    return classes


# ------------------------------------------------------------- field logs ---
FIELD_RES = {
    "loaded": re.compile(r"\] ok: preset '([^']*)' loaded \((\d+) passes\)"),
    "loadfail": re.compile(r"\] preset load failed \(([^)]*)\): (.*)"),
    "fallback": re.compile(r"\] custom preset failed to load; Shader Filter is off"),
    "osd_unavail": re.compile(r"\] osd: Shader(?: Filter)?: .* \(unavailable\)"),
    "reinit": re.compile(r"\] Reinit: filter=(\d+)"),
    "changed": re.compile(r"\] preset changed: (\S+) -> (\S+)"),
    "cachehit": re.compile(r"cache hit for (\S+)"),
    "backend": re.compile(r"\] device created, backend=(\S+)"),
    "heartbeat": re.compile(r"\] heartbeat: (\d+) frames \((\d+) skipped, (\d+) total, filter=(\d+)\)"),
    "shutdown": re.compile(r"\] Shutdown"),
    "ts": re.compile(r"^\[\s*(\d+) ms\]"),
}


def parse_field_log(path):
    st = {
        "loaded": {},          # name -> passes
        "loadfail": {},        # name -> last message
        "fallback": 0,
        "osd_unavail": 0,
        "reinit": 0,
        "changed": 0,
        "cachehit": set(),
        "backends": set(),
        "last_hb": None,
        "shutdown": False,
        "last_ts": 0,
    }
    with open(path, errors="replace") as fh:
        for line in fh:
            m = FIELD_RES["ts"].search(line)
            if m:
                st["last_ts"] = max(st["last_ts"], int(m.group(1)))
            for key, rx in FIELD_RES.items():
                m = rx.search(line)
                if not m:
                    continue
                if key == "loaded":
                    st["loaded"][m.group(1)] = int(m.group(2))
                elif key == "loadfail":
                    st["loadfail"][m.group(1)] = m.group(2).strip()
                elif key == "fallback":
                    st["fallback"] += 1
                elif key == "osd_unavail":
                    st["osd_unavail"] += 1
                elif key == "reinit":
                    st["reinit"] += 1
                elif key == "changed":
                    st["changed"] += 1
                elif key == "cachehit":
                    st["cachehit"].add(m.group(1))
                elif key == "backend":
                    st["backends"].add(m.group(1))
                elif key == "heartbeat":
                    st["last_hb"] = tuple(int(g) for g in m.groups())
                elif key == "shutdown":
                    st["shutdown"] = True
    return st


# ------------------------------------------------------------------- git ----
def git_info():
    info = {}
    try:
        def run(*args):
            return subprocess.run(["git", "-C", REPO] + list(args), capture_output=True,
                                  text=True, timeout=10).stdout.strip()
        info["branch"] = run("rev-parse", "--abbrev-ref", "HEAD")
        info["commit"] = run("rev-parse", "--short", "HEAD")
        info["dirty"] = bool(run("status", "--porcelain"))
        tag = run("describe", "--tags", "--abbrev=0")
        info["tag"] = tag if tag else None
        info["date"] = run("log", "-1", "--format=%cs")
    except Exception:
        return None
    if not info.get("commit"):
        return None
    return info


# ------------------------------------------------------------------ main ----
def main():
    ap = argparse.ArgumentParser(description=__doc__,
                                 formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("--sweep", nargs="+", help="translate-stage sweep TSV(s); chunks are merged")
    ap.add_argument("--baseline", help="previous sweep TSV for fixed/regressed/delta counts")
    ap.add_argument("--field-log", nargs="*", help="gpu-scaler.log file(s)")
    ap.add_argument("--test-list", default="docs/audit/shader-test-list-2026-10-15.csv",
                    help="tiered field-test list CSV (status column)")
    ap.add_argument("--audit", default="docs/audit/slang_audit_v2.csv",
                    help="audit CSV for risk classification of failures")
    ap.add_argument("--title", default="GPU shader scaler status")
    ap.add_argument("--note", action="append", default=[], help="extra bullet line (repeatable)")
    ap.add_argument("--probe", nargs="*", metavar=("TOOL", "ROOT"),
                    help="translate tool + shader root: re-run failing presets "
                         "and group them by TRANSLATE-FAIL reason class")
    ap.add_argument("--fail-list", type=int, default=8, help="max failures listed by name")
    ap.add_argument("--out", help="write markdown here (default: stdout)")
    args = ap.parse_args()

    lines = []
    add = lines.append
    today = report_date()

    # -- header
    gi = git_info()
    head = f"## {args.title} - {today}"
    if gi:
        meta = f"@ {gi['commit']} ({gi['branch']}"
        if gi.get("tag"):
            meta += f", tag {gi['tag']}"
        meta += ", dirty)" if gi["dirty"] else ")"
        head += f" {meta}"
    add(head)
    add("")

    # -- risks lookup
    risks = {}
    if os.path.isfile(args.audit):
        with open(args.audit, newline="") as fh:
            for row in csv.DictReader(fh):
                risks[row["preset"]] = row.get("risk", "")

    # -- corpus sweep
    if args.sweep:
        sweep = merge_sweeps(args.sweep)
        ok, bad, passes, ok_passes, risk_mix = sweep_summary(sweep, risks)
        pct = 100.0 * len(ok) / len(sweep) if sweep else 0.0
        add(f"**Corpus translate stage (offline): {len(ok)}/{len(sweep)} presets "
            f"translate clean ({pct:.1f}%), {passes} passes covered**")
        if bad:
            names = sorted(bad)
            listed = ", ".join(f"`{short_name(n)}`" for n in names[:args.fail_list])
            more = f" (+{len(names) - args.fail_list} more)" if len(names) > args.fail_list else ""
            add(f"- translate failures ({len(names)}): {listed}{more}")
            top, distinct = dir_groups(names)
            add("- by area: " + ", ".join(f"`{k}/` {v}" for k, v in top)
                + (f" (+{distinct - len(top)} other areas)" if distinct > len(top) else ""))
            if args.probe and len(args.probe) == 2:
                classes = probe_failures(args.probe[0], args.probe[1], names)
                if classes:
                    cls_items = sorted(classes.items(), key=lambda kv: -kv[1])
                    add("- failure classes: " + "; ".join(
                        f"{c} ({v})" for c, v in cls_items[:3]))
            if risk_mix:
                mix = ", ".join(f"{k} {v}" for k, v in sorted(risk_mix.items()))
                add(f"- failure risk mix: {mix}")
        else:
            add("- translate failures: none")
        add("")
    else:
        bad, sweep = {}, None

    # -- baseline delta
    if args.sweep and args.baseline and os.path.isfile(args.baseline):
        base = read_sweep(args.baseline)
        cur = sweep
        common = set(base) & set(cur)
        fixed = [p for p in common if not preset_ok(base[p]) and preset_ok(cur[p])]
        regressed = [p for p in common if preset_ok(base[p]) and not preset_ok(cur[p])]
        sized = [p for p in common if preset_ok(base[p]) and preset_ok(cur[p])
                 and base[p]["sizes"] != cur[p]["sizes"]]
        add(f"**vs baseline sweep ({len(common)} presets in common): "
            f"{len(fixed)} fixed, {len(regressed)} regressed, {len(sized)} merged-size deltas**")
        for label, group in (("fixed", fixed), ("regressed", regressed)):
            if group:
                ex = ", ".join(f"`{short_name(n)}`" for n in sorted(group)[:args.fail_list])
                more = f" (+{len(group) - args.fail_list} more)" if len(group) > args.fail_list else ""
                add(f"- {label}: {ex}{more}")
        if sized and len(sized) <= 6:
            add("- size deltas: " + ", ".join(f"`{n}`" for n in sorted(sized)))
        elif sized:
            add("- mergedSize is the include-resolution signature; largest deltas: "
                + ", ".join(
                    f"`{os.path.basename(p)}`" for p in sorted(
                        sized, key=lambda p: -abs(sum(cur[p]["sizes"]) - sum(base[p]["sizes"])))[:3]))
        add("")

    # -- field logs
    if args.field_log:
        agg = None
        missing = []
        for path in args.field_log:
            if not os.path.isfile(path):
                missing.append(path)
                continue
            st = parse_field_log(path)
            if agg is None:
                agg = st
            else:
                agg["loaded"].update(st["loaded"])
                agg["loadfail"].update(st["loadfail"])
                agg["fallback"] += st["fallback"]
                agg["osd_unavail"] += st["osd_unavail"]
                agg["reinit"] += st["reinit"]
                agg["changed"] += st["changed"]
                agg["cachehit"] |= st["cachehit"]
                agg["backends"] |= st["backends"]
                agg["shutdown"] = agg["shutdown"] or st["shutdown"]
                agg["last_ts"] = max(agg["last_ts"], st["last_ts"])
                if st["last_hb"]:
                    agg["last_hb"] = st["last_hb"]
        if agg:
            bits = []
            if agg["backends"]:
                bits.append("backend=" + "/".join(sorted(agg["backends"])))
            if agg["last_ts"]:
                bits.append(f"{agg['last_ts'] // 1000}s logged")
            if agg["last_hb"]:
                frames, skipped, total, filt = agg["last_hb"]
                bits.append(f"last heartbeat {frames} frames, filter={filt}")
            bits.append("clean shutdown" if agg["shutdown"] else "no Shutdown line in log")
            add(f"**Field sweep ({', '.join(os.path.basename(p) for p in args.field_log)}): "
                f"{len(agg['loaded'])} distinct presets loaded, "
                f"{len(agg['loadfail'])} failed** - " + ", ".join(bits))
            if agg["loadfail"]:
                ex = ", ".join(f"`{n}`" for n in sorted(agg["loadfail"])[:args.fail_list])
                add(f"- load failures: {ex}")
            extras = []
            if agg["reinit"]:
                extras.append(f"{agg['reinit']} Reinit")
            if agg["changed"]:
                extras.append(f"{agg['changed']} hot switches")
            if agg["cachehit"]:
                extras.append(f"{len(agg['cachehit'])} distinct cache hits")
            if agg["fallback"]:
                extras.append(f"{agg['fallback']} fallback-to-stock events")
            if extras:
                add("- lifecycle: " + ", ".join(extras))
            add("")
        else:
            add(f"**Field sweep: no usable log** - (not found: {', '.join(missing)})")
            add("")

    # -- tiered field-test list
    if os.path.isfile(args.test_list):
        tiers = {}
        with open(args.test_list, newline="") as fh:
            for row in csv.DictReader(fh):
                tier = (row.get("tier") or "?").split(" -")[0].split(" —")[0].strip()
                status = (row.get("status") or "").strip().lower()
                t = tiers.setdefault(tier, {"total": 0, "pass": 0, "fail": 0})
                t["total"] += 1
                if status.startswith(("pass", "ok", "confirmed")):
                    t["pass"] += 1
                elif status:
                    t["fail"] += 1
        if len(tiers) == 1 and "?" in tiers:
            add(f"**Tiered field-test list: {tiers['?']['total']} presets, "
                f"status column empty (pending field verification)**")
        else:
            cells = []
            for t, v in sorted(tiers.items()):
                if v["pass"] == 0 and v["fail"] == 0:
                    cells.append(f"{t} {v['total']} pending")
                else:
                    cells.append(f"{t} {v['pass']}/{v['total']} pass"
                                 + (f", {v['fail']} fail" if v["fail"] else ""))
            add("**Tiered field-test list: " + " | ".join(cells) + "**")
        add("")

    # -- notes
    if args.note:
        add("**Notes:**")
        for n in args.note:
            add(f"- {n}")
        add("")

    text = "\n".join(lines).rstrip() + "\n"
    if args.out:
        with open(args.out, "w") as fh:
            fh.write(text)
        print(f"wrote {args.out} ({len(lines)} lines)", file=sys.stderr)
    else:
        sys.stdout.write(text)


if __name__ == "__main__":
    main()
