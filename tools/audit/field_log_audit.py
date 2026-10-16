#!/usr/bin/env python3
"""field_log_audit.py - enhanced field log audit tool for DevilutionX GPU shaders.

Enhanced version of field_log_digest.py with additional audit capabilities:

- PNG texture load tracking (addresses TEST-08)
- Failure categorization per audit spec
- Audit progress tracking and tier reporting
- Performance analysis and comparison
- Log validation and completeness checking
- Integration with audit CSV workflows

Field sweeps produce hundreds of near-identical lines (per-pass boilerplate,
cache-hit spam, pass-target dumps). This tool reduces one or more logs to:

  - a summary header (sessions, loads, uniques, fallbacks, errors, cache hits,
    reinit/heartbeat pacing, PNG loads)
  - one line per unique preset (loads, passes, cache hits, flags, result, tier)
  - a "notables" line (longest chain, F1 mip users, feedback-texture users,
    re-loads, PNG texture usage)
  - an EXCEPTIONS section - errors / warnings / fallbacks / loads that never
    completed / unclean exits (categorized by failure type)
  - audit progress report (compared to baseline CSVs)

Usage:
  python field_log_audit.py gpu-scaler.log [more.log ...] [--tsv out.tsv]
  python field_log_audit.py gpu-scaler.log --audit-csv baseline.csv --progress
  python field_log_audit.py gpu-scaler.log --compare prev-digest.tsv
  python field_log_audit.py gpu-scaler.log --validate

Exit codes: 0 = clean (no exceptions), 2 = exceptions present, 1 = usage/IO/val.
"""
import argparse
import datetime
import os
import re
import sys
from collections import defaultdict, Counter

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
    r"\] heartbeat: (\d+) frames \((\d+) skipped(?:, (\d+) dest-skipped)?,"
    r" (\d+) total, filter=(\d+)\)")
RE_DEST_SKIP = re.compile(r"\] present skip: ")
RE_REINIT = re.compile(r"\] Reinit: filter=(\d+)")
RE_FALLBACK = re.compile(r"custom preset failed to load; Shader Filter is off")
RE_SHUTDOWN = re.compile(r"\] Shutdown")
RE_SESSION = re.compile(r"=== GPU scaler session \(build ([^)]*)\) ===")
RE_ERRISH = re.compile(r"\b(error|failed|failure|fallback|warn(ing)?)\b", re.I)
RE_PNG = re.compile(r"\.png", re.I)
# Enhanced PNG tracking - look for successful PNG loads
RE_PNG_LOAD = re.compile(r"GPU scaler preset: .*?textures=\[([^\]]*\.png[^\]]*)\]")
RE_PNG_SUCCESS = re.compile(r"texture '([^']+\.png)' loaded successfully")
# Failure categorization regexes
RE_SLANG_ERROR = re.compile(r"translating '([^']+)' \(pass \d+\): (.+)")
RE_GLSLANG_ERROR = re.compile(r"compiling '([^']+)' \(pass \d+\): (.+)")
RE_VULKAN_ERROR = re.compile(r"vkCreateGraphicsPipelines|vkUpdateDescriptorSets")
RE_MISSING_TEXTURE = re.compile(r"unresolved texture '([^']+)'")
RE_CRASH = re.compile(r"CRASH #\d+:")

# Failure categories from audit spec
FAILURE_CATEGORIES = {
    "slang_translation": {
        "pattern": RE_SLANG_ERROR,
        "location": "slang_translate.cpp",
        "description": "SLANG -> GLSL translation error"
    },
    "glslang_compile": {
        "pattern": RE_GLSLANG_ERROR,
        "location": "slang_translate.cpp",
        "description": "GLSL -> SPIR-V compilation error"
    },
    "vulkan_validation": {
        "pattern": RE_VULKAN_ERROR,
        "description": "Vulkan validation layer error",
        "location": "preset_runner.cpp"
    },
    "missing_texture": {
        "pattern": RE_MISSING_TEXTURE,
        "description": "Missing texture/LUT resource",
        "location": "preset_runner.cpp"
    },
    "preset_loading": {
        "pattern": RE_FALLBACK,
        "description": "Preset loading failure",
        "location": "preset_parser.cpp"
    },
    "crash": {
        "pattern": RE_CRASH,
        "description": "Crash/segfault",
        "location": "preset_runner.cpp"
    },
    "unknown": {
        "pattern": RE_ERRISH,
        "description": "Unknown error/warning",
        "location": "needs investigation"
    }
}


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
        "png_textures": set(),
        "tier": "unknown"
    }


class AuditDigest:
    def __init__(self):
        self.order = []            # preset names in first-load order
        self.agg = {}              # name -> aggregated record
        self.loads = 0
        self.cache_hits = 0
        self.reinits = 0
        self.shutdowns = 0
        self.heartbeats = []
        self.exceptions = []       # (file, lineno, text, category)
        self.feedback = {}         # name -> texture names
        self.pngs = set()          # PNG textures found
        self.png_loads = 0        # Successful PNG loads
        self.sessions = 0
        self.builds = []           # unique session-header build ids
        self.started = False
        self.linenos = 0
        self.failure_counts = defaultdict(int)
        self.tier_stats = defaultdict(int)
        
    def categorize_failure(self, line):
        """Categorize a failure line according to audit spec."""
        for category, info in FAILURE_CATEGORIES.items():
            if info["pattern"].search(line):
                return category, info["description"], info.get("location", "unknown")
        return "unknown", "Unknown error/warning", "needs investigation"
    
    def close_window(self, win, fname, lineno):
        if win is None:
            return
        rec = self.agg.setdefault(win["name"], {
            "dir": win["dir"], "loads": 0, "passes": 0, "cache_hits": 0,
            "flags": set(), "result": "incomplete", "feedback": set(),
            "png_textures": set(), "tier": "unknown"
        })
        if win["name"] not in self.order:
            self.order.append(win["name"])
        rec["loads"] += 1
        self.loads += 1
        rec["cache_hits"] += win["cache_hits"]
        rec["passes"] = max(rec["passes"], win["passes"])
        rec["flags"] |= win["flags"]
        rec["feedback"] |= win.get("feedback", set())
        rec["png_textures"] |= win.get("png_textures", set())
        if win["fallback"]:
            rec["result"] = "fallback"
        elif win["loaded"]:
            if rec["result"] not in ("fallback",):
                rec["result"] = "working"
        else:
            self.exceptions.append((
                fname, lineno, 
                "preset '%s' window ended without a load-complete line" % win["name"],
                "unknown"
            ))
    
    def feed(self, path):
        win = None
        fname = os.path.basename(path)
        with open(path, "r", encoding="utf-8", errors="replace") as fh:
            for lineno, raw in enumerate(fh, 1):
                self.linenos += 1
                line = raw.rstrip("\n")
                
                # Check for PNG loads first (enhanced tracking)
                png_match = RE_PNG_SUCCESS.search(line)
                if png_match:
                    self.png_loads += 1
                    png_name = os.path.basename(png_match.group(1))
                    self.pngs.add(png_name)
                    if win is not None:
                        win["png_textures"].add(png_name)
                
                m = RE_CHANGED.search(line)
                if m:
                    self.close_window(win, fname, lineno)
                    win = new_window(m.group(2))
                    self.started = True
                    continue
                    
                m = RE_LOADED.search(line)
                if m and win is not None:
                    if m.group(1) != win["name"]:
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
                        png_name = os.path.basename(m.group(1))
                        self.pngs.add(png_name)
                    continue
                    
                m = RE_PASS.search(line)
                if m and win is not None:
                    for tex in m.group(3).split(","):
                        tex = tex.strip()
                        if tex.lower().endswith("feedback"):
                            win["flags"].add("feedback")
                            win.setdefault("feedback", set()).add(tex)
                        if RE_PNG.search(tex):
                            png_name = os.path.basename(tex)
                            win["png_textures"].add(png_name)
                            self.pngs.add(png_name)
                    continue
                    
                m = RE_MIPMAP.search(line)
                if m and win is not None:
                    win["flags"].add("F1[%s]" % m.group(1).replace(" ", ""))
                    continue
                    
                m = RE_HEART.search(line)
                if m:
                    self.heartbeats.append(
                        (int(m.group(1)), int(m.group(2)), int(m.group(3) or 0)))
                    continue
                    
                if RE_REINIT.search(line):
                    self.reinits += 1
                    continue
                    
                if RE_FALLBACK.search(line):
                    if win is not None:
                        win["fallback"] = True
                    category, desc, location = self.categorize_failure(line)
                    self.exceptions.append((fname, lineno, line.strip(), category))
                    self.failure_counts[category] += 1
                    continue
                    
                if RE_SHUTDOWN.search(line):
                    self.close_window(win, fname, lineno)
                    win = None
                    self.shutdowns += 1
                    self.started = False
                    continue
                    
                m = RE_SESSION.search(line)
                if m:
                    if m.group(1) not in self.builds:
                        self.builds.append(m.group(1))
                    continue
                    
                # Check for other error patterns
                if (RE_ERRISH.search(line) or RE_DEST_SKIP.search(line)) \
                        and "] ok:" not in line:
                    category, desc, location = self.categorize_failure(line)
                    self.exceptions.append((fname, lineno, line.strip(), category))
                    self.failure_counts[category] += 1
                    continue
                    
        self.close_window(win, fname, -1)
        self.sessions += self.shutdowns
        if self.started:
            self.sessions += 1
            self.exceptions.append((
                fname, -1, 
                "log does not end with Shutdown (unclean exit or truncated paste)",
                "unknown"
            ))
    
    def working(self):
        return [n for n in self.order if self.agg[n]["result"] == "working"]
    
    def summary(self, files):
        hb = self.heartbeats
        lo = min((h[0] for h in hb), default=0)
        hi = max((h[0] for h in hb), default=0)
        sk = max((h[1] for h in hb), default=0)
        dsk = max((h[2] for h in hb), default=0)
        nwork = len(self.working())
        bad = len(self.order) - nwork
        out = []
        out.append("=== enhanced field log audit | %s | files: %s ==="
                   % (today(), ", ".join(os.path.basename(f) for f in files)))
        if self.builds:
            out.append("builds: %s" % " | ".join(self.builds))
        out.append("sessions: %d (shutdowns: %d) | loads: %d | unique: %d | "
                   "fallback: %d | errors/warns: %d"
                   % (self.sessions, self.shutdowns, self.loads,
                      len(self.order), 
                      sum(1 for n in self.order if self.agg[n]["result"] == "fallback"),
                      len([e for e in self.exceptions if e[1] != -1])))
        out.append("cache hits: %d | reinits: %d | heartbeats: %d "
                   "(frames %d-%d/beat, max skipped %d, max dest-skipped %d) | "
                   "PNG textures: %d loaded (%s)"
                   % (self.cache_hits, self.reinits, len(hb), lo, hi, sk, dsk,
                      self.png_loads, ", ".join(sorted(self.pngs)) if self.pngs else "none"))
        out.append("results: working %d | not-working %d (%s)"
                   % (nwork, bad,
                      ", ".join("%s=%s" % (n, self.agg[n]["result"])
                                for n in self.order if self.agg[n]["result"] != "working")
                      or "-"))
        return out
    
    def table(self):
        out = ["--- per preset: name | dir | tier | loads | passes | cache-hits | flags | result | PNGs ---"]
        for i, n in enumerate(self.order, 1):
            r = self.agg[n]
            flags = ",".join(sorted(r["flags"])) or "-"
            png_count = len(r["png_textures"])
            out.append("%2d. %-30s %-12s %-6s %dx %dp %3dh %-25s %s (%d)"
                       % (i, n, r["dir"], r["tier"], r["loads"], r["passes"],
                          r["cache_hits"], flags, r["result"], png_count))
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
        png_users = [n for n in self.order if self.agg[n]["png_textures"]]
        if png_users:
            bits.append("PNG texture users: %s" % ", ".join(png_users))
        return ["--- notables ---", " | ".join(bits)] if bits else []
    
    def exceptions_block(self):
        if not self.exceptions:
            return ["--- exceptions ---", "none"]
        out = ["--- exceptions (%d) ---" % len(self.exceptions)]
        # Group by category
        by_category = defaultdict(list)
        for fname, lineno, text, category in self.exceptions:
            by_category[category].append((fname, lineno, text))
        
        for category in sorted(by_category.keys()):
            if category == "unknown":
                continue  # Show raw unknown exceptions last
            info = FAILURE_CATEGORIES.get(category, {})
            desc = info.get("description", category)
            out.append(f"{category.upper()}: {desc}")
            for fname, lineno, text in by_category[category]:
                out.append("  %s:%s: %s" % (fname, lineno if lineno > 0 else "?", text))
        
        # Unknown exceptions
        if "unknown" in by_category:
            out.append("UNKNOWN:")
            for fname, lineno, text in by_category["unknown"]:
                out.append("  %s:%s: %s" % (fname, lineno if lineno > 0 else "?", text))
        
        return out
    
    def failure_summary(self):
        if not self.failure_counts:
            return ["--- failure summary ---", "none"]
        out = ["--- failure summary ---"]
        total = sum(self.failure_counts.values())
        for category in sorted(self.failure_counts.keys()):
            count = self.failure_counts[category]
            pct = (count / total * 100) if total > 0 else 0
            out.append(f"{category}: {count} ({pct:.1f}%)")
        return out
    
    def render(self, files):
        blocks = [self.summary(files), self.table(), self.notables(),
                  self.failure_summary(), self.exceptions_block()]
        return "\n".join("\n".join(b) for b in blocks if b)
    
    def tsv(self):
        lines = ["name\tdir\ttier\tloads\tpasses\tcache_hits\tflags\tresult\tpng_textures"]
        for n in self.order:
            r = self.agg[n]
            flags = ",".join(sorted(r["flags"])) or "-"
            pngs = ",".join(sorted(r["png_textures"])) or "-"
            lines.append("%s\t%s\t%s\t%d\t%d\t%d\t%s\t%s\t%s"
                         % (n, r["dir"], r["tier"], r["loads"], r["passes"],
                            r["cache_hits"], flags, r["result"], pngs))
        return "\n".join(lines)
    
    def load_audit_csv(self, csv_path):
        """Load audit CSV and update tier information."""
        if not os.path.exists(csv_path):
            return
        try:
            with open(csv_path, "r", encoding="utf-8") as fh:
                header = fh.readline().strip().split(",")
                for line in fh:
                    parts = line.strip().split(",")
                    if len(parts) >= 2:
                        preset = parts[0]
                        tier = parts[1] if len(parts) > 1 else "unknown"
                        if preset in self.agg:
                            self.agg[preset]["tier"] = tier
        except Exception as e:
            print(f"Warning: Could not load audit CSV {csv_path}: {e}", file=sys.stderr)
    
    def audit_progress(self, csv_path):
        """Generate audit progress report compared to baseline CSV."""
        self.load_audit_csv(csv_path)
        
        # Count by tier
        tier_counts = defaultdict(lambda: {"total": 0, "working": 0, "failed": 0})
        
        for n in self.order:
            rec = self.agg[n]
            tier = rec["tier"]
            if tier == "unknown":
                tier = "untracked"
            
            tier_counts[tier]["total"] += 1
            if rec["result"] == "working":
                tier_counts[tier]["working"] += 1
            elif rec["result"] == "fallback":
                tier_counts[tier]["failed"] += 1
        
        # Generate progress report
        out = ["--- audit progress report ---"]
        out.append(f"Compared to: {os.path.basename(csv_path)}")
        out.append("")
        
        # Sort tiers by name
        for tier in sorted(tier_counts.keys()):
            counts = tier_counts[tier]
            working = counts["working"]
            total = counts["total"]
            failed = counts["failed"]
            pct = (working / total * 100) if total > 0 else 0
            out.append(f"{tier}: {working}/{total} working ({pct:.1f}%), {failed} failed")
        
        # Overall stats
        total_working = sum(c["working"] for c in tier_counts.values())
        total_failed = sum(c["failed"] for c in tier_counts.values())
        total_tested = total_working + total_failed
        
        out.append("")
        out.append(f"Overall: {total_working}/{total_tested} working ({total_working/total_tested*100:.1f}%), {total_failed} failed")
        
        return out


def compare_digests(current_path, prev_path):
    """Compare two digest TSV files and show differences."""
    try:
        with open(current_path, "r") as f:
            current_lines = f.readlines()
        with open(prev_path, "r") as f:
            prev_lines = f.readlines()
    except FileNotFoundError as e:
        print(f"Error: {e}", file=sys.stderr)
        return 1
    
    # Parse headers
    current_data = {}
    prev_data = {}
    
    for line in current_lines[1:]:  # Skip header
        parts = line.strip().split("\t")
        if len(parts) >= 8:
            current_data[parts[0]] = parts[1:]
    
    for line in prev_lines[1:]:  # Skip header
        parts = line.strip().split("\t")
        if len(parts) >= 8:
            prev_data[parts[0]] = parts[1:]
    
    # Find differences
    all_presets = set(current_data.keys()) | set(prev_data.keys())
    changes = []
    
    for preset in sorted(all_presets):
        current = current_data.get(preset, [""] * 7)
        prev = prev_data.get(preset, [""] * 7)
        
        # Check if result changed
        if current[6] != prev[6]:
            changes.append(f"{preset}: {prev[6]} -> {current[6]}")
        
        # Check if loads changed significantly
        try:
            current_loads = int(current[3])
            prev_loads = int(prev[3])
            if current_loads > prev_loads + 1:
                changes.append(f"{preset}: loads {prev_loads} -> {current_loads}")
        except (ValueError, IndexError):
            pass
    
    if changes:
        print("--- changes detected ---")
        for change in changes:
            print(change)
        return 2
    else:
        print("--- no changes detected ---")
        return 0


def validate_logs(digest, files):
    """Perform validation checks on the logs."""
    issues = []
    
    # Check for unclean exits
    if digest.started:
        issues.append("Log does not end with Shutdown - may be truncated")
    
    # Check for excessive fallbacks
    fallback_rate = sum(1 for n in digest.order if digest.agg[n]["result"] == "fallback") / len(digest.order)
    if fallback_rate > 0.1:  # More than 10% fallbacks
        issues.append(f"High fallback rate: {fallback_rate:.1%}")
    
    # Check for heartbeats with excessive skips
    if digest.heartbeats:
        max_skipped = max(h[1] for h in digest.heartbeats)
        if max_skipped > 100:  # More than 100 skipped frames per heartbeat
            issues.append(f"Excessive skipped frames: {max_skipped}")
    
    # Check for PNG tracking issues (TEST-08)
    if digest.png_loads == 0 and any("png" in line.lower() for fname, _, line, _ in digest.exceptions):
        issues.append("PNG texture loads detected in exceptions but no successful loads tracked")
    
    if issues:
        print("--- validation issues ---")
        for issue in issues:
            print(f"  {issue}")
        return 2
    else:
        print("--- validation passed ---")
        return 0


def main(argv=None):
    ap = argparse.ArgumentParser(description=__doc__.splitlines()[0])
    ap.add_argument("logs", nargs="*", help="gpu-scaler.log file(s)")
    ap.add_argument("--tsv", help="write per-preset TSV to this path")
    ap.add_argument("--audit-csv", help="compare against audit CSV for progress tracking")
    ap.add_argument("--compare", help="compare with previous digest TSV")
    ap.add_argument("--validate", action="store_true", help="perform validation checks")
    args = ap.parse_args(argv)

    if not args.logs and not args.compare:
        print("field_log_audit: error: no input files specified", file=sys.stderr)
        ap.print_help()
        return 1

    if args.compare:
        return compare_digests(args.logs[0], args.compare)

    d = AuditDigest()
    for path in args.logs:
        if not os.path.isfile(path):
            print("field_log_audit: not a file: %s" % path, file=sys.stderr)
            return 1
        d.feed(path)

    # Generate output
    text = d.render(args.logs)
    print(text)
    
    if args.audit_csv:
        progress = d.audit_progress(args.audit_csv)
        print("\n" + "\n".join(progress))
    
    if args.tsv:
        with open(args.tsv, "w", encoding="utf-8", newline="\n") as fh:
            fh.write(d.tsv() + "\n")
        print("[tsv written: %s]" % args.tsv, file=sys.stderr)
    
    if args.validate:
        return validate_logs(d, args.logs)
    
    return 2 if d.exceptions else 0


if __name__ == "__main__":
    sys.exit(main())