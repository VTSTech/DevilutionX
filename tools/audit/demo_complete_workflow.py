#!/usr/bin/env python3
"""Demonstrate complete workflow with updated coverage functionality."""

import sys
import os
sys.path.insert(0, os.path.dirname(__file__))

from field_log_audit_interactive import InteractiveAuditDigest

def demo_complete_workflow():
    """Demonstrate the complete audit workflow."""
    print("=== Complete DevilutionX Shader Audit Workflow Demo ===\n")
    
    # Step 1: Load and analyze field log
    print("Step 1: Analyzing field log data...")
    d = InteractiveAuditDigest()
    
    # Simulate field log data
    sample_presets = [
        {
            "name": "anti-aliasing/fxaa.slangp",
            "dir": "anti-aliasing",
            "result": "running",
            "passes": 1,
            "cache_hits": 1,
            "flags": set(),
            "png_textures": set(),
            "dev_notes": ""
        },
        {
            "name": "reshade/magicbloom.slangp",
            "dir": "reshade",
            "result": "running",
            "passes": 12,
            "cache_hits": 0,
            "flags": {"F1[0,1,2,3,4,5,6,7,8,9,10]", "feedback"},
            "png_textures": set(),
            "dev_notes": ""
        },
        {
            "name": "blurs/dual_filter_bloom.slangp",
            "dir": "blurs",
            "result": "not_running",
            "passes": 0,
            "cache_hits": 0,
            "flags": set(),
            "png_textures": set(),
            "dev_notes": "Failed to load - fallback triggered"
        }
    ]
    
    for preset in sample_presets:
        d.order.append(preset["name"])
        d.agg[preset["name"]] = preset
    
    print(f"✓ Loaded {len(d.order)} presets from field log")
    print(f"  - Running: {len([r for r in d.agg.values() if r['result'] == 'running'])}")
    print(f"  - Not running: {len([r for r in d.agg.values() if r['result'] == 'not_running'])}")
    
    # Step 2: Populate coverage CSV
    print("\nStep 2: Populating coverage CSV...")
    
    # Create sample existing coverage
    existing_csv = "/tmp/demo-existing-coverage.csv"
    with open(existing_csv, "w") as f:
        f.write("""preset,status,category,notes
anti-aliasing/fxaa.slangp,correct,ok,"Previously tested on build e7f213c31"
reshade/bloom.slangp,running,ok,"9 passes, Original semantic sampled"
old-preset.slangp,correct,ok,"Legacy entry"
""")
    
    print("Existing coverage:")
    with open(existing_csv, "r") as f:
        print(f.read())
    
    # Test different coverage modes
    print("\nTesting coverage modes:")
    
    # Update mode
    output_csv = "/tmp/demo-coverage-update.csv"
    updated, added = d.populate_vulkan_coverage(output_csv, mode="update")
    print(f"✓ Update mode: {updated} updated, {added} added")
    
    print("Updated coverage:")
    with open(output_csv, "r") as f:
        print(f.read())
    
    # Append mode
    output_csv = "/tmp/demo-coverage-append.csv"
    appended, _ = d.populate_vulkan_coverage(output_csv, mode="append")
    print(f"✓ Append mode: {appended} added")
    
    print("Appended coverage:")
    with open(output_csv, "r") as f:
        print(f.read())
    
    # Step 3: Interactive sweep demonstration
    print("\nStep 3: Interactive sweep demonstration...")
    print("Simulating interactive prompts for each preset:")
    
    # Simulate interactive sweep
    for i, preset_name in enumerate(d.order, 1):
        rec = d.agg[preset_name]
        
        print(f"\n[{i:2d}/{len(d.order)}] {preset_name}")
        print(f"    Directory: {rec['dir']}")
        print(f"    Passes: {rec['passes']}, Cache hits: {rec['cache_hits']}")
        print(f"    Features: {', '.join(sorted(rec['flags'])) if rec['flags'] else 'none'}")
        print(f"    Current status: {rec['result']}")
        
        if rec['result'] == 'not_running':
            print("    ⚠️  Skipping - already marked as not_running")
            continue
        
        # Simulate user input
        if "fxaa" in preset_name:
            print("Status [c]orrect, [r]unning, [n]not_running, [s]kip? c")
            print("Development notes (press Enter to skip): Clean test, no issues")
            rec['result'] = 'correct'
            rec['dev_notes'] = "Clean test, no issues observed"
        elif "magicbloom" in preset_name:
            print("Status [c]orrect, [r]unning, [n]not_running, [s]kip? n")
            print("Development notes (press Enter to skip): Screen black on pass 10")
            rec['result'] = 'not_running'
            rec['dev_notes'] = "Screen black on pass 10, feedback texture issue"
        else:
            print("Status [c]orrect, [r]unning, [n]not_running, [s]kip? s")
            print("    Skipping")
    
    print(f"\n✓ Interactive sweep complete")
    print(f"  Status changes: 2 presets updated")
    print(f"  Notes added: 2 presets")
    
    # Step 4: Generate final report
    print("\nStep 4: Generating final audit report...")
    
    # Show final status
    status_counts = {}
    for n in d.order:
        status = d.agg[n]["result"]
        status_counts[status] = status_counts.get(status, 0) + 1
    
    print("Final status summary:")
    for status, count in status_counts.items():
        print(f"  {status}: {count}")
    
    # Show TSV output
    print("\nTSV Output:")
    print(d.tsv())
    
    print("\n" + "="*60)
    print("✅ Complete workflow demonstrated successfully!")
    print("="*60)
    
    print("\nReal-world usage:")
    print("1. Run field test and collect gpu-scaler.log")
    print("2. Populate coverage: --populate-coverage --coverage-mode update")
    print("3. Interactive sweep: --sweep --dev-notes")
    print("4. Generate report: --audit-csv shader-test-list-updated.csv --tsv report.tsv")

if __name__ == "__main__":
    demo_complete_workflow()