#!/usr/bin/env python3
"""Cross-reference the v2 slang audit CSV with the code-audit findings (2026-10-14).

Code findings being cross-referenced (from static audit of preset_runner.cpp /
slang_translate.cpp / preset_parser.cpp on fork master 2a30fca65):

  F1  mipmap_input no-op   - pass output textures created with num_levels=1 and
                             GenerateMipmaps is never called for pass outputs;
                             the mipmapped SAMPLER is selected but samples clamp
                             to LOD 0. Presets with mipmap_input render, but the
                             mipmap benefit is absent (visual defect class).
  F5  history ring bug    - ring allocated with historyDepth_ slots (= max
                             OriginalHistory# index) and indexed as
                             (slot + N) % depth. Correct model: depth+1 slots,
                             (slot - N) mod (depth+1). Net effect:
                             depth=1: OriginalHistory1 == current frame (no
                             temporal data at all).
                             depth=2: OriginalHistory2 == current frame.
                             depth>=3: all OriginalHistoryN# >= 1 shifted toward
                             newer frames (OH1 actually returns T-(depth-N)); OH_depth == current
  F6  viewport scale      - uses swapchain size as "viewport"; no aspect
                             correction (matches simple implementations; watch
                             for over-wide intermediate targets).
  F8  srgb+float together - FormatForPass prefers floatFramebuffer over
                             srgbFramebuffer (spec says float wins too, OK) but
                             a preset setting BOTH gets float with no warning.
  T9  case-sensitive Size - <Name>Size uniform validation is case-sensitive in
                             slang_translate.cpp while sampler names are matched
                             case-insensitively -> 'unknown uniform block member'
                             failures for case-mismatched declarations.

Outputs: per-risk bucket counts, per-finding exposure counts, a refined
phase-1/phase-2 target list, and a CSV of presets affected by F5/F1.
"""

import csv
import sys
from collections import Counter

CSV_PATH = "/home/z/my-project/audit_v2/slang_audit.csv"
OUT_AFFECTED = "/home/z/my-project/download/vulkan-audit-codefindings-affected.csv"


def parse_bool(v: str) -> bool:
    return v.strip().lower() == "true"


def main() -> int:
    rows = []
    with open(CSV_PATH, newline="", encoding="utf-8") as fh:
        reader = csv.DictReader(fh)
        rows = list(reader)

    print(f"total presets: {len(rows)}")

    # --- Basic distribution (robust to commas in risk_reasons) -------------
    risk_counter = Counter(r["risk"] for r in rows)
    print(f"risk distribution: {dict(risk_counter)}")

    zero_pass = sum(1 for r in rows if int(r["passes"]) == 0)
    print(f"0-pass presets (unresolved #reference): {zero_pass}")
    nonzero = [r for r in rows if int(r["passes"]) > 0]
    print(f"non-zero-pass presets: {len(nonzero)}")

    # --- F5: history ring exposure -----------------------------------------
    # orig_hist=True means some pass references OriginalHistory# (the audit
    # script counts any index); max_hist = highest index seen.
    f5 = [r for r in rows if parse_bool(r["orig_hist"])]
    f5_by_hist = Counter(int(r["max_hist"]) for r in f5)
    print("\n[F5 history ring bug] presets referencing OriginalHistory#:",
          len(f5))
    print("  max_hist distribution:", dict(sorted(f5_by_hist.items())))
    # depth = max_hist -> what the runner does:
    #   depth==1: OH1 returns CURRENT frame (temporal shaders see no motion)
    #   depth==2: OH2 returns current frame; OH1 correct
    #   depth>=3: OH1..OH(depth-1) shifted (OH_N actually returns T-(depth-N)); OH_depth == current
    f5_broken = [r for r in f5 if int(r["max_hist"]) >= 1]
    print("  presets with WRONG temporal sampling under current runner code:",
          len(f5_broken))
    # Among these, how many are classified low/medium (i.e. will pass a casual
    # field test but render with wrong temporal data)?
    f5_risk = Counter(r["risk"] for r in f5_broken)
    print("  risk of the wrong-temporal set:", dict(f5_risk))

    # --- F1: mipmap_input exposure ------------------------------------------
    f1 = [r for r in rows if parse_bool(r["mipmap_input"])]
    print("\n[F1 mipmap_input no-op] presets:", len(f1))
    f1_risk = Counter(r["risk"] for r in f1)
    print("  risk:", dict(f1_risk))
    f1_vp = sum(1 for r in f1 if parse_bool(r["viewport_scale"]))
    print("  also viewport_scale:", f1_vp)

    # --- F6: viewport_scale exposure ----------------------------------------
    f6 = [r for r in rows if parse_bool(r["viewport_scale"])]
    print("\n[F6 viewport = swapchain size] presets:", len(f6))

    # --- combined F1+F5 (wrong-but-rendering) --------------------------------
    both = [r for r in rows if parse_bool(r["mipmap_input"]) and int(r["max_hist"]) >= 1]
    print("\n[F1+F5 combined] presets:", len(both))

    # --- refined phase lists -------------------------------------------------
    # Phase 1 candidates per handoff: low risk, non-zero passes.
    phase1 = [r for r in nonzero if r["risk"] == "low"]
    print("\n[Phase 1] low-risk non-zero-pass presets:", len(phase1))
    # Flag the ones that will NOT render correctly even if load succeeds:
    p1_f5 = [r for r in phase1 if int(r["max_hist"]) >= 1]
    p1_f1 = [r for r in phase1 if parse_bool(r["mipmap_input"])]
    p1_vp = [r for r in phase1 if parse_bool(r["viewport_scale"])]
    p1_fc = [r for r in phase1 if parse_bool(r["uses_frame_count"])]
    print("  ... using OriginalHistory>=1 (F5 wrong temporal):", len(p1_f5))
    print("  ... with mipmap_input (F1 no-op mips):", len(p1_f1))
    print("  ... with viewport_scale (F6 swapchain viewport):", len(p1_vp))
    print("  ... using FrameCount:", len(p1_fc))
    truly_clean = [r for r in phase1
                   if int(r["max_hist"]) == 0 and not parse_bool(r["mipmap_input"])]
    print("  ... CLEAN of known runner gaps (expect full pass):",
          len(truly_clean))

    # Phase 2: medium
    phase2 = [r for r in nonzero if r["risk"] == "medium"]
    print("\n[Phase 2] medium-risk non-zero-pass presets:", len(phase2))

    # --- high-yield: the biggest fix-unlocks-a-cluster analysis --------------
    def top_dirs(presets, n=8):
        dirs = Counter(r["preset"].split("/")[0] for r in presets)
        return dirs.most_common(n)

    print("\n[F5 fix impact] top dirs for max_hist>=1 presets:")
    for d, c in top_dirs(f5_broken):
        print(f"  {d}: {c}")
    print("\n[F1 fix impact] top dirs for mipmap_input presets:")
    for d, c in top_dirs(f1):
        print(f"  {d}: {c}")

    # --- write affected-set CSV ----------------------------------------------
    fieldnames = ["preset", "passes", "risk",
                  "F5_history_wrong", "max_hist",
                  "F1_mipmap_noop", "F6_viewport_swapchain",
                  "uses_frame_count", "risk_reasons"]
    with open(OUT_AFFECTED, "w", newline="", encoding="utf-8") as fh:
        w = csv.DictWriter(fh, fieldnames=fieldnames)
        w.writeheader()
        for r in rows:
            max_hist = int(r["max_hist"])
            row = {
                "preset": r["preset"],
                "passes": r["passes"],
                "risk": r["risk"],
                "F5_history_wrong": "true" if max_hist >= 1 else "false",
                "max_hist": max_hist,
                "F1_mipmap_noop": r["mipmap_input"],
                "F6_viewport_swapchain": r["viewport_scale"],
                "uses_frame_count": r["uses_frame_count"],
                "risk_reasons": r["risk_reasons"],
            }
            w.writerow(row)
    print(f"\nwrote {OUT_AFFECTED}")

    # --- sample listing: worst F5 offenders likely to be field-tested early ---
    print("\n[Phase-1 presets hit by F5] (would render, but wrong temporal data):")
    for r in p1_f5[:15]:
        print(f"  {r['preset']}  max_hist={r['max_hist']}")
    return 0


if __name__ == "__main__":
    sys.exit(main())
