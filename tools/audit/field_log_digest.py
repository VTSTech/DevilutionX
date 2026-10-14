#!/usr/bin/env python3
"""field_log_digest.py - compact a raw gpu-scaler.log field log into a digest.

Field sweeps produce hundreds of near-identical lines (per-pass boilerplate,
cache-hit spam, pass-target dumps). This tool reduces one or more logs to:

  - a summary header (sessions, loads, uniques, fallbacks, errors, cache hits,
    reinit/heartbeat pacing)
  - one line per unique preset (loads, passes, cache hits, flags, result)
  - a "notables" line (longest chain, F1 mip users, feedback-texture users,
    re-loads)
  - an EXCEPTIONS section - errors / warnings / fallbacks / loads that never
    completed / unclean exits. Empty unless something actually needs eyes.

Workflow convention (field round 4): run this FIRST and share the digest;
attach the raw log only when the exceptions section is non-empty.

Usage:
  python field_log_digest.py gpu-scaler.log [more.log ...] [--tsv out.tsv]

Exit codes: 0 = clean (no exceptions), 2 = exceptions present, 1 = usage/IO.
Stdlib only; works on the Windows build box and in CI.
"""
import argparse
import datetime
import os
import re
import sys

try:
    from zoneinfo import ZoneInfo

    def today():
        return datetime.datetime.now(ZoneInfo("America/Toronto")).date().isoformat()
except Exception:  # pragma: no cover - pre-3.9 fallback
    def today():
        return datetime.datetime.now(datetime.timezone.utc).date().isoformat()


# ------------------------------------------------------------- line grammar --
RE_CHANGED = re.compile(r"\] preset changed: (.+) -> (.+)$")
RE_LOADED = re.compile(r"\] ok: preset '([^']+)' loaded \((\d+) passes?\)")
RE_PASS = re.compile(
    r"GPU scaler preset: pass (\d+) '([^']+)' .*? textures=\[([^\]]*)\]")
RE_CACHE = re.compile(r"GPU scaler preset: cache hit for (\S+)")
RE_MIPMAP = re.compile(
    r"mipmapped inputs \(F1\): pass outputs with mip chains \[([0-9, ]+)\]")
RE_HEART = re.compile(
    r"\] heartbeat: (\d+) frames \((\d+) skipped, (\d+) total, filter=(\d+)\)")
RE_REINIT = re.compile(r"\] Reinit: filter=(\d+)")
RE_FALLBACK = re.compile(r"custom preset failed to load; Shader Filter is off")
RE_SHUTDOWN = re.compile(r"\] Shutdown")
RE_ERRISH = re.compile(r"\b(error|failed|failure|fallback|warn(ing)?)\b", re.I)
RE_PNG = re.compile(r"\.png", re.I)


def dir_of(path):
    """Preset dir = path component after the last 'shaders' dir, minus file."""
    parts = [p for p in re.split(r"[\\/]+", path) if p]
    for i in range(len(parts) - 2, -1, -1):
        if parts[i].lower() == "shaders":
            return parts[i + 1] if i + 1 < len(parts) - 1 else "(root)"
    return parts[-2] if len(parts) >= 2 else "(root)"


def name_of(path):
    return os.path.splitext(os.path.basename(path.replace("\\", "/")))[0]


def new_window(path):
    return {
        "path": path,
        "name": name_of(path),
        "dir": dir_of(path),
        "cache_hits": 0,
        "passes": 0,
        "loaded": False,
        "fallback": False,
        "flags": set(),
    }


class Digest:
    def __init__(self):
        self.order = []            # preset names in first-load order
        self.agg = {}              # name -> aggregated record
        self.loads = 0
        self.cache_hits = 0
        self.reinits = 0
        self.shutdowns = 0
        self.heartbeats = []
        self.exceptions = []       # (file, lineno, text)
        self.feedback = {}         # name -> texture names
        self.pngs = set()
        self.sessions = 0
        self.started = False       # saw any event after last Shutdown
        self.linenos = 0

    # ---- window bookkeeping -------------------------------------------
    def close_window(self, win, fname, lineno):
        if win is None:
            return
        rec = self.agg.setdefault(win["name"], {
            "dir": win["dir"], "loads": 0, "passes": 0, "cache_hits": 0,
            "flags": set(), "result": "incomplete", "feedback": set(),
        })
        if win["name"] not in self.order:
            self.order.append(win["name"])
        rec["loads"] += 1
        self.loads += 1
        rec["cache_hits"] += win["cache_hits"]
        rec["passes"] = max(rec["passes"], win["passes"])
        rec["flags"] |= win["flags"]
        rec["feedback"] |= win.get("feedback", set())
        if win["fallback"]:
            rec["result"] = "fallback"
        elif win["loaded"]:
            if rec["result"] not in ("fallback",):
                rec["result"] = "working"
        else:
            self.exceptions.append(
                (fname, lineno,
                 "preset '%s' window ended without a load-complete line" % win["name"]))

    # ---- feed one file -------------------------------------------------
    def feed(self, path):
        win = None
        fname = os.path.basename(path)
        with open(path, "r", encoding="utf-8", errors="replace") as fh:
            for lineno, raw in enumerate(fh, 1):
                self.linenos += 1
                line = raw.rstrip("\n")
                m = RE_CHANGED.search(line)
                if m:
                    self.close_window(win, fname, lineno)
                    win = new_window(m.group(2))
                    self.started = True
                    continue
                m = RE_LOADED.search(line)
                if m and win is not None:
                    if m.group(1) != win["name"]:
                        # ok-line name is authoritative
                        old = win["name"]
                        win["name"] = m.group(1)
                    win["loaded"] = True
                    win["passes"] = int(m.group(2))
                    continue
                m = RE_CACHE.search(line)
                if m:
                    self.cache_hits += 1
                    if win is not None:
                        win["cache_hits"] += 1
                    if RE_PNG.search(m.group(1)):
                        self.pngs.add(os.path.basename(m.group(1)))
                    continue
                m = RE_PASS.search(line)
                if m and win is not None:
                    for tex in m.group(3).split(","):
                        tex = tex.strip()
                        if tex.lower().endswith("feedback"):
                            win["flags"].add("feedback")
                            win.setdefault("feedback", set()).add(tex)
                    if RE_PNG.search(line):
                        self.pngs.add(os.path.basename(m.group(2)))
                    continue
                m = RE_MIPMAP.search(line)
                if m and win is not None:
                    win["flags"].add("F1[%s]" % m.group(1).replace(" ", ""))
                    continue
                m = RE_HEART.search(line)
                if m:
                    self.heartbeats.append((int(m.group(1)), int(m.group(2))))
                    continue
                if RE_REINIT.search(line):
                    self.reinits += 1
                    continue
                if RE_FALLBACK.search(line):
                    if win is not None:
                        win["fallback"] = True
                    self.exceptions.append((fname, lineno, line.strip()))
                    continue
                if RE_SHUTDOWN.search(line):
                    self.close_window(win, fname, lineno)
                    win = None
                    self.shutdowns += 1
                    self.started = False
                    continue
                if RE_ERRISH.search(line) and "] ok:" not in line:
                    self.exceptions.append((fname, lineno, line.strip()))
            self.close_window(win, fname, -1)
        self.sessions += self.shutdowns
        if self.started:
            # events after the last Shutdown: truncated paste or unclean exit
            self.sessions += 1
            self.exceptions.append(
                (fname, -1, "log does not end with Shutdown (unclean exit or truncated paste)"))

    # ---- output ---------------------------------------------------------
    def working(self):
        return [n for n in self.order if self.agg[n]["result"] == "working"]

    def summary(self, files):
        hb = self.heartbeats
        lo = min((h[0] for h in hb), default=0)
        hi = max((h[0] for h in hb), default=0)
        sk = max((h[1] for h in hb), default=0)
        nwork = len(self.working())
        bad = len(self.order) - nwork
        out = []
        out.append("=== field log digest | %s | files: %s ==="
                   % (today(), ", ".join(os.path.basename(f) for f in files)))
        out.append("sessions: %d (shutdowns: %d) | loads: %d | unique: %d | "
                   "fallback: %d | errors/warns: %d"
                   % (self.sessions, self.shutdowns, self.loads,
                      len(self.order), sum(1 for n in self.order
                                           if self.agg[n]["result"] == "fallback"),
                      len([e for e in self.exceptions if e[1] != -1])))
        out.append("cache hits: %d | reinits: %d | heartbeats: %d "
                   "(frames %d-%d/beat, max skipped %d) | PNG textures: %s"
                   % (self.cache_hits, self.reinits, len(hb), lo, hi, sk,
                      sorted(self.pngs) if self.pngs else "none"))
        out.append("results: working %d | not-working %d (%s)"
                   % (nwork, bad,
                      ", ".join("%s=%s" % (n, self.agg[n]["result"])
                                for n in self.order if self.agg[n]["result"] != "working")
                      or "-"))
        return out

    def table(self):
        out = ["--- per preset: name | dir | loads | passes | cache-hits | flags | result ---"]
        for i, n in enumerate(self.order, 1):
            r = self.agg[n]
            flags = ",".join(sorted(r["flags"])) or "-"
            out.append("%2d. %-34s %-12s %dx %dp %3dh %-28s %s"
                       % (i, n, r["dir"], r["loads"], r["passes"],
                          r["cache_hits"], flags, r["result"]))
        return out

    def notables(self):
        bits = []
        if self.order:
            top = max(self.order, key=lambda n: self.agg[n]["passes"])
            if self.agg[top]["passes"] > 1:
                bits.append("longest chain: %s %dp" % (top, self.agg[top]["passes"]))
        f1 = []
        for n in self.order:
            for f in sorted(self.agg[n]["flags"]):
                if f.startswith("F1"):
                    f1.append("%s %s" % (n, f))
                    break
        if f1:
            bits.append("F1 mip chains: %s" % ", ".join(f1))
        fb = ["%s(%s)" % (n, "/".join(sorted(self.agg[n]["feedback"])))
              for n in self.order if self.agg[n]["feedback"]]
        if fb:
            bits.append("feedback textures: %s" % " ".join(fb))
        rel = ["%s(%dx)" % (n, self.agg[n]["loads"]) for n in self.order
               if self.agg[n]["loads"] > 1]
        if rel:
            bits.append("re-loads: %s" % " ".join(rel))
        if self.pngs:
            bits.append("PNG LUTs loaded: %s" % ", ".join(sorted(self.pngs)))
        return ["--- notables ---", " | ".join(bits)] if bits else []

    def exceptions_block(self):
        if not self.exceptions:
            return ["--- exceptions ---", "none"]
        out = ["--- exceptions (%d) ---" % len(self.exceptions)]
        for fname, lineno, text in self.exceptions:
            out.append("%s:%s: %s" % (fname, lineno if lineno > 0 else "?", text))
        return out

    def render(self, files):
        blocks = [self.summary(files), self.table(), self.notables(),
                  self.exceptions_block()]
        return "\n".join("\n".join(b) for b in blocks if b)

    def tsv(self):
        lines = ["name\tdir\tloads\tpasses\tcache_hits\tflags\tresult"]
        for n in self.order:
            r = self.agg[n]
            lines.append("%s\t%s\t%d\t%d\t%d\t%s\t%s"
                         % (n, r["dir"], r["loads"], r["passes"],
                            r["cache_hits"], ",".join(sorted(r["flags"])) or "-",
                            r["result"]))
        return "\n".join(lines)


def main(argv=None):
    ap = argparse.ArgumentParser(description=__doc__.splitlines()[0])
    ap.add_argument("logs", nargs="+", help="gpu-scaler.log file(s)")
    ap.add_argument("--tsv", help="write per-preset TSV to this path")
    args = ap.parse_args(argv)

    d = Digest()
    for path in args.logs:
        if not os.path.isfile(path):
            print("field_log_digest: not a file: %s" % path, file=sys.stderr)
            return 1
        d.feed(path)

    text = d.render(args.logs)
    print(text)
    if args.tsv:
        with open(args.tsv, "w", encoding="utf-8", newline="\n") as fh:
            fh.write(d.tsv() + "\n")
        print("[tsv written: %s]" % args.tsv, file=sys.stderr)
    return 2 if d.exceptions else 0


if __name__ == "__main__":
    sys.exit(main())
