#!/usr/bin/env python3
"""field_log_audit_interactive.py - Interactive field log audit tool for DevilutionX GPU shaders.

Enhanced version with interactive features:

- Populate vulkan coverage CSV from field logs
- Interactive preset sweeping with status selection
- Development notes collection
- Status management (correct/running unless detected as not running)
- Integration with existing audit CSV workflows

Usage:
  python3 field_log_audit_interactive.py gpu-scaler.log [options]
  
Options:
  --populate-coverage    Populate vulkan-coverage-updated.csv from field log
  --sweep               Interactive sweep through presets with status prompts
  --dev-notes          Collect development notes during sweep
  --audit-csv CSV_PATH  Compare against audit CSV for progress tracking
  --tsv TSV_PATH       Save TSV output to file
  --validate           Perform validation checks

Examples:
  # Populate coverage CSV
  python3 field_log_audit_interactive.py gpu-scaler.log --populate-coverage
  
  # Interactive sweep with dev notes
  python3 field_log_audit_interactive.py gpu-scaler.log --sweep --dev-notes
  
  # Full workflow
  python3 field_log_audit_interactive.py gpu-scaler.log --sweep --dev-notes --audit-csv shader-test-list-updated.csv --tsv sweep-report.tsv
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
RE_PNG_SUCCESS = re.compile(r"texture '([^']+\.png)' loaded successfully")
RE_CRASH = re.compile(r"CRASH #\d+:")

# Failure categories
FAILURE_CATEGORIES = {
    "slang_translation": {
        "pattern": re.compile(r"translating '([^']+)' \(pass \d+\): (.+)"),
        "location": "slang_translate.cpp",
        "description": "SLANG → GLSL translation error"
    },
    "glslang_compile": {
        "pattern": re.compile(r"compiling '([^']+)' \(pass \d+\): (.+)"),
        "location": "slang_translate.cpp",
        "description": "GLSL → SPIR-V compilation error"
    },
    "vulkan_validation": {
        "pattern": re.compile(r"vkCreateGraphicsPipelines|vkUpdateDescriptorSets"),
        "description": "Vulkan validation layer error",
        "location": "preset_runner.cpp"
    },
    "missing_texture": {
        "pattern": re.compile(r"unresolved texture '([^']+)'"),
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
        "tier": "unknown",
        "dev_notes": ""
    }


class InteractiveAuditDigest:
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
            "png_textures": set(), "tier": "unknown", "dev_notes": ""
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
            rec["result"] = "not_running"
        elif win["loaded"]:
            if rec["result"] not in ("not_running",):
                rec["result"] = "running"
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
                
                # Check for PNG loads first
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
    
    def load_existing_coverage(self, csv_path):
        """Load existing coverage data from CSV file."""
        existing_data = {}
        if os.path.exists(csv_path):
            try:
                with open(csv_path, "r", encoding="utf-8") as fh:
                    header = fh.readline().strip()
                    for line in fh:
                        if line.strip():
                            parts = line.strip().split(",", 3)
                            if len(parts) >= 4:
                                preset = parts[0]
                                status = parts[1]
                                category = parts[2]
                                notes = parts[3].strip('"')
                                existing_data[preset] = {
                                    "status": status,
                                    "category": category,
                                    "notes": notes
                                }
            except Exception as e:
                print(f"Warning: Could not load existing coverage {csv_path}: {e}", file=sys.stderr)
        return existing_data
    
    def find_matching_preset(self, preset_name, existing_data):
        """Find matching preset in existing data with flexible matching."""
        # Direct match
        if preset_name in existing_data:
            return preset_name
        
        # Try partial matches (filename without extension)
        base_name = os.path.splitext(preset_name)[0]
        for existing_preset in existing_data:
            if base_name in existing_preset or existing_preset in base_name:
                return existing_preset
        
        # Try directory-based matching
        preset_dir = preset_name.split("/")[0] if "/" in preset_name else ""
        for existing_preset in existing_data:
            existing_dir = existing_preset.split("/")[0] if "/" in existing_preset else ""
            if preset_dir and preset_dir == existing_dir:
                return existing_preset
        
        return None
    
    def populate_vulkan_coverage(self, output_csv, mode="update"):
        """Populate vulkan coverage CSV from field log data.
        
        Args:
            output_csv: Path to output CSV file
            mode: "update" (update existing, append new), "append" (only add new), "overwrite" (replace all)
        """
        print(f"Populating {output_csv} with field log data (mode: {mode})...")
        
        # Load existing data
        existing_data = self.load_existing_coverage(output_csv)
        
        # Create header
        header = "preset,status,category,notes"
        lines = [header]
        
        # Track what was updated/added
        updated_count = 0
        added_count = 0
        
        # Process all presets from field log
        for preset in self.order:
            rec = self.agg[preset]
            
            # Determine status
            if rec["result"] == "not_running":
                status = "not_running"
            elif rec["result"] == "incomplete":
                status = "parsing"
            else:
                status = "running"  # Default for loaded presets
            
            # Determine category
            if rec["result"] == "not_running":
                category = "needs-fix"
            elif rec["png_textures"]:
                category = "png-textures"
            elif "F1" in rec["flags"]:
                category = "f1-mipmaps"
            elif "feedback" in rec["flags"]:
                category = "feedback"
            else:
                category = "ok"
            
            # Create notes
            notes = f"{rec['passes']} passes"
            if rec["cache_hits"] > 0:
                notes += f", {rec['cache_hits']} cache hits"
            if rec["png_textures"]:
                notes += f", PNG textures: {len(rec['png_textures'])}"
            if rec["flags"]:
                notes += f", features: {', '.join(sorted(rec['flags']))}"
            if rec["dev_notes"]:
                notes += f"; dev notes: {rec['dev_notes']}"
            
            # Check if preset already exists (with flexible matching)
            matching_preset = self.find_matching_preset(preset, existing_data)
            
            if matching_preset:
                if mode == "update":
                    # Update existing entry with new data, but keep original name
                    existing_entry = existing_data[matching_preset]
                    lines.append(f"{matching_preset},{status},{category},\"{notes}\"")
                    updated_count += 1
                elif mode == "append":
                    # Keep existing, don't update
                    existing_entry = existing_data[matching_preset]
                    lines.append(f"{matching_preset},{existing_entry['status']},{existing_entry['category']},\"{existing_entry['notes']}\"")
                else:  # overwrite
                    lines.append(f"{matching_preset},{status},{category},\"{notes}\"")
                    updated_count += 1
            else:
                # Add new entry
                lines.append(f"{preset},{status},{category},\"{notes}\"")
                added_count += 1
        
        # Add existing entries that weren't in the field log (for append/overwrite modes)
        if mode in ["append", "overwrite"]:
            for preset, data in existing_data.items():
                if preset not in self.order:
                    lines.append(f"{preset},{data['status']},{data['category']},\"{data['notes']}\"")
        
        # Write to file
        with open(output_csv, "w", encoding="utf-8", newline="\n") as fh:
            fh.write("\n".join(lines) + "\n")
        
        if mode == "update":
            total_entries = len([line for line in lines[1:] if line.strip()])
            print(f"✓ Vulkan coverage CSV updated: {updated_count} updated, {added_count} added, {total_entries} total")
        elif mode == "append":
            total_entries = len([line for line in lines[1:] if line.strip()])
            print(f"✓ Vulkan coverage CSV appended: {added_count} added, {total_entries} total")
        else:
            total_entries = len([line for line in lines[1:] if line.strip()])
            print(f"✓ Vulkan coverage CSV overwritten: {total_entries} total")
        
        return updated_count, added_count
    
    def interactive_sweep(self, collect_notes=False):
        """Interactive sweep through presets with status selection."""
        print("\n" + "="*60)
        print("INTERACTIVE PRESET SWEEP")
        print("="*60)
        print(f"Total presets to review: {len(self.order)}")
        print(f"Currently 'running': {len([r for r in self.agg.values() if r['result'] == 'running'])}")
        print(f"Currently 'not_running': {len([r for r in self.agg.values() if r['result'] == 'not_running'])}")
        print(f"Currently 'incomplete': {len([r for r in self.agg.values() if r['result'] == 'incomplete'])}")
        print("\nFor each preset, you'll be prompted for:")
        print("- Status: [c]orrect, [r]unning, [n]ot_running, [s]kip")
        print("- Optional: Development notes (if --dev-notes enabled)")
        print()
        
        updated_count = 0
        notes_count = 0
        
        for i, preset_name in enumerate(self.order, 1):
            rec = self.agg[preset_name]
            
            print(f"\n[{i:2d}/{len(self.order)}] {preset_name}")
            print(f"    Directory: {rec['dir']}")
            print(f"    Passes: {rec['passes']}, Cache hits: {rec['cache_hits']}")
            print(f"    Features: {', '.join(sorted(rec['flags'])) if rec['flags'] else 'none'}")
            print(f"    PNG textures: {len(rec['png_textures'])}")
            print(f"    Current status: {rec['result']}")
            
            # Skip if already not_running
            if rec['result'] == 'not_running':
                print("    ⚠️  Skipping - already marked as not_running")
                continue
            
            # Prompt for status
            while True:
                status_prompt = "Status [c]orrect, [r]unning, [n]ot_running, [s]kip? "
                response = input(status_prompt).strip().lower()
                
                if response in ['c', 'correct']:
                    new_status = 'correct'
                    break
                elif response in ['r', 'running']:
                    new_status = 'running'
                    break
                elif response in ['n', 'not_running']:
                    new_status = 'not_running'
                    break
                elif response in ['s', 'skip']:
                    print(f"    Skipping {preset_name}")
                    break
                else:
                    print("    Invalid response. Please enter c, r, n, or s")
            
            if response == 's':
                continue
            
            # Update status
            rec['result'] = new_status
            updated_count += 1
            
            # Collect development notes if requested
            dev_notes = ""
            if collect_notes and response in ['c', 'n']:
                notes_prompt = "Development notes (press Enter to skip): "
                dev_notes = input(notes_prompt).strip()
                if dev_notes:
                    rec['dev_notes'] = dev_notes
                    notes_count += 1
                    print(f"    Notes: {dev_notes}")
            
            # Show confirmation
            print(f"    ✓ Updated {preset_name} to {new_status}")
        
        print(f"\n Sweep complete!")
        print(f"  Updated {updated_count} presets")
        if collect_notes:
            print(f"  Added notes to {notes_count} presets")
        
        return updated_count, notes_count
    
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
                      sum(1 for n in self.order if self.agg[n]["result"] == "not_running"),
                      len([e for e in self.exceptions if e[1] != -1])))
        out.append("cache hits: %d | reinits: %d | heartbeats: %d "
                   "(frames %d-%d/beat, max skipped %d, max dest-skipped %d) | "
                   "PNG textures: %d loaded (%s)"
                   % (self.cache_hits, self.reinits, len(hb), lo, hi, sk, dsk,
                      self.png_loads, ", ".join(sorted(self.pngs)) if self.pngs else "none"))
        
        # Status breakdown
        status_counts = defaultdict(int)
        for n in self.order:
            status_counts[self.agg[n]["result"]] += 1
        
        status_str = " | ".join(f"{status}={count}" for status, count in sorted(status_counts.items()))
        out.append("status: %s" % status_str)
        
        return out
    
    def table(self):
        out = ["--- per preset: name | dir | status | passes | cache-hits | flags | PNGs ---"]
        for i, n in enumerate(self.order, 1):
            r = self.agg[n]
            flags = ",".join(sorted(r["flags"])) or "-"
            png_count = len(r["png_textures"])
            out.append("%2d. %-40s %-12s %-8s %dp %3dh %-25s %d"
                       % (i, n, r["dir"], r["result"], r["passes"],
                          r["cache_hits"], flags, png_count))
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
        png_users = [n for n in self.order if self.agg[n]["png_textures"]]
        if png_users:
            bits.append("PNG texture users: %s" % ", ".join(png_users))
        return ["--- notables ---", " | ".join(bits)] if bits else []
    
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
    
    def audit_progress(self, csv_path):
        """Generate audit progress report compared to baseline CSV."""
        self.load_audit_csv(csv_path)
        
        # Count by tier
        tier_counts = defaultdict(lambda: {"total": 0, "running": 0, "correct": 0, "not_running": 0})
        
        for n in self.order:
            rec = self.agg[n]
            tier = rec["tier"]
            if tier == "unknown":
                tier = "untracked"
            
            tier_counts[tier]["total"] += 1
            tier_counts[tier][rec["result"]] += 1
        
        # Generate progress report
        out = ["--- audit progress report ---"]
        out.append(f"Compared to: {os.path.basename(csv_path)}")
        out.append("")
        
        # Sort tiers by name
        for tier in sorted(tier_counts.keys()):
            counts = tier_counts[tier]
            total = counts["total"]
            running = counts["running"]
            correct = counts["correct"]
            not_running = counts["not_running"]
            
            out.append(f"{tier}: {correct} correct, {running} running, {not_running} not_running ({total} total)")
        
        # Overall stats
        total_correct = sum(c["correct"] for c in tier_counts.values())
        total_running = sum(c["running"] for c in tier_counts.values())
        total_not_running = sum(c["not_running"] for c in tier_counts.values())
        total_tested = total_correct + total_running + total_not_running
        
        out.append("")
        out.append(f"Overall: {total_correct} correct, {total_running} running, {total_not_running} not_running ({total_tested} total)")
        
        return out
    
    def render(self, files):
        blocks = [self.summary(files), self.table(), self.notables(),
                  self.failure_summary()]
        return "\n".join("\n".join(b) for b in blocks if b)
    
    def tsv(self):
        lines = ["name\tdir\tstatus\tpasses\tcache_hits\tflags\tpng_textures\tdev_notes"]
        for n in self.order:
            r = self.agg[n]
            flags = ",".join(sorted(r["flags"])) or "-"
            pngs = ",".join(sorted(r["png_textures"])) or "-"
            notes = r["dev_notes"].replace("\t", " ")  # Replace tabs in notes
            lines.append("%s\t%s\t%s\t%d\t%d\t%s\t%s\t%s"
                         % (n, r["dir"], r["result"], r["passes"],
                            r["cache_hits"], flags, pngs, notes))
        return "\n".join(lines)


def main(argv=None):
    ap = argparse.ArgumentParser(description=__doc__.splitlines()[0])
    ap.add_argument("logs", nargs="*", help="gpu-scaler.log file(s)")
    ap.add_argument("--populate-coverage", help="Populate vulkan-coverage-updated.csv from field log")
    ap.add_argument("--coverage-mode", choices=["update", "append", "overwrite"], default="update",
                   help="Coverage CSV mode: update (default), append, or overwrite")
    ap.add_argument("--sweep", action="store_true", help="Interactive sweep through presets with status prompts")
    ap.add_argument("--dev-notes", action="store_true", help="Collect development notes during sweep")
    ap.add_argument("--audit-csv", help="Compare against audit CSV for progress tracking")
    ap.add_argument("--tsv", help="Save TSV output to file")
    ap.add_argument("--validate", action="store_true", help="Perform validation checks")
    
    args = ap.parse_args(argv)

    if not args.logs:
        print("field_log_audit_interactive: error: no input files specified", file=sys.stderr)
        ap.print_help()
        return 1

    d = InteractiveAuditDigest()
    for path in args.logs:
        if not os.path.isfile(path):
            print("field_log_audit_interactive: not a file: %s" % path, file=sys.stderr)
            return 1
        d.feed(path)

    # Handle populate coverage option
    if args.populate_coverage:
        updated_count, added_count = d.populate_vulkan_coverage(
            args.populate_coverage, 
            mode=args.coverage_mode
        )
        if not args.sweep and not args.audit_csv:
            return 0

    # Handle interactive sweep
    if args.sweep:
        updated_count, notes_count = d.interactive_sweep(collect_notes=args.dev_notes)
        print(f"\nInteractive sweep completed: {updated_count} presets updated")

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
        # Simple validation
        issues = []
        if d.started:
            issues.append("Log does not end with Shutdown - may be truncated")
        
        if issues:
            print("--- validation issues ---")
            for issue in issues:
                print(f"  {issue}")
            return 2
    
    return 0


if __name__ == "__main__":
    sys.exit(main())