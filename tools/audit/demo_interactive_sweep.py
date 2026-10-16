#!/usr/bin/env python3
"""Demonstration of the interactive sweep functionality."""

import sys
import os
sys.path.insert(0, os.path.dirname(__file__))

from field_log_audit_interactive import InteractiveAuditDigest

def demo_sweep():
    """Demonstrate how the interactive sweep would work."""
    print("=== DEMO: Interactive Sweep Functionality ===\n")
    
    # Create a mock digest with sample data
    d = InteractiveAuditDigest()
    
    # Add some sample presets
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
            "name": "broken-preset.slangp",
            "dir": "test", 
            "result": "not_running",
            "passes": 0,
            "cache_hits": 0,
            "flags": set(),
            "png_textures": set(),
            "dev_notes": ""
        }
    ]
    
    for preset in sample_presets:
        d.order.append(preset["name"])
        d.agg[preset["name"]] = preset
    
    print("Sample presets loaded:")
    for i, name in enumerate(d.order, 1):
        rec = d.agg[name]
        print(f"  {i}. {name} ({rec['result']})")
    
    print(f"\nCurrent status summary:")
    status_counts = {}
    for n in d.order:
        status = d.agg[n]["result"]
        status_counts[status] = status_counts.get(status, 0) + 1
    
    for status, count in status_counts.items():
        print(f"  {status}: {count}")
    
    print("\n" + "="*50)
    print("INTERACTIVE SWEEP DEMO")
    print("="*50)
    
    # Simulate interactive prompts
    print("\n[ 1/3] anti-aliasing/fxaa.slangp")
    print("    Directory: anti-aliasing")
    print("    Passes: 1, Cache hits: 1")
    print("    Features: none")
    print("    PNG textures: 0")
    print("    Current status: running")
    print("Status [c]orrect, [r]unning, [n]not_running, [s]kip? c")
    print("Development notes (press Enter to skip): Clean test, no issues observed")
    print("    ✓ Updated anti-aliasing/fxaa.slangp to correct")
    
    print("\n[ 2/3] reshad/magicbloom.slangp")
    print("    Directory: reshad")
    print("    Passes: 12, Cache hits: 0")
    print("    Features: F1[0,1,2,3,4,5,6,7,8,9,10], feedback")
    print("    PNG textures: 0")
    print("    Current status: running")
    print("Status [c]orrect, [r]unning, [n]not_running, [s]kip? n")
    print("Development notes (press Enter to skip): Screen black on pass 10, feedback texture issue")
    print("    ✓ Updated reshad/magicbloom.slangp to not_running")
    
    print("\n[ 3/3] broken-preset.slangp")
    print("    Directory: test")
    print("    Passes: 0, Cache hits: 0")
    print("    Features: none")
    print("    PNG textures: 0")
    print("    Current status: not_running")
    print("    ⚠️  Skipping - already marked as not_running")
    
    print("\n Sweep complete!")
    print("  Updated 2 presets")
    print("  Added notes to 2 presets")
    
    # Show updated status
    print(f"\nUpdated status summary:")
    for status, count in status_counts.items():
        actual_count = len([r for r in d.agg.values() if r["result"] == status])
        if actual_count != count:
            print(f"  {status}: {actual_count} (was {count})")
    
    # Show TSV output
    print(f"\nTSV Output Preview:")
    print(d.tsv())
    
    return d

def demo_populate_coverage():
    """Demonstrate coverage population."""
    print("\n" + "="*50)
    print("DEMO: Vulkan Coverage Population")
    print("="*50)
    
    # Create mock digest
    d = InteractiveAuditDigest()
    
    # Add sample data
    sample_data = [
        {
            "name": "anti-aliasing/fxaa.slangp",
            "dir": "anti-aliasing",
            "result": "running",
            "passes": 1,
            "cache_hits": 1,
            "flags": set(),
            "png_textures": set()
        },
        {
            "name": "reshade/magicbloom.slangp",
            "dir": "reshade", 
            "result": "running",
            "passes": 12,
            "cache_hits": 0,
            "flags": {"F1[0,1,2,3,4,5,6,7,8,9,10]", "feedback"},
            "png_textures": set()
        },
        {
            "name": "blurs/dual_filter_bloom.slangp",
            "dir": "blurs",
            "result": "running", 
            "passes": 24,
            "cache_hits": 20,
            "flags": {"F1[20]", "feedback"},
            "png_textures": set()
        }
    ]
    
    for preset in sample_data:
        d.order.append(preset["name"])
        d.agg[preset["name"]] = preset
    
    print(f"Populating coverage CSV with {len(d.order)} presets...")
    
    # Show what the CSV would contain
    print("\nGenerated CSV content:")
    print("preset,status,category,notes")
    for preset in d.order:
        rec = d.agg[preset]
        
        # Determine status
        if rec["result"] == "not_running":
            status = "not_running"
        elif rec["result"] == "incomplete":
            status = "parsing"
        else:
            status = "running"
        
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
        
        print(f"{preset},{status},{category},\"{notes}\"")

if __name__ == "__main__":
    print("DevilutionX Field Log Audit - Interactive Tool Demo")
    print("=" * 60)
    
    demo_sweep()
    demo_populate_coverage()
    
    print("\n" + "="*60)
    print("Interactive Tool Features Demonstrated!")
    print("="*60)
    print("\nKey Features:")
    print("✓ Interactive preset sweeping with status selection")
    print("✓ Development notes collection")
    print("✓ Automatic coverage CSV population")
    print("✓ Smart status management (preserves not_running)")
    print("✓ TSV output for integration")
    print("✓ Audit CSV progress tracking")
    print("\nUsage:")
    print("  python3 field_log_audit_interactive.py gpu-scaler.log --sweep --dev-notes")
    print("  python3 field_log_audit_interactive.py gpu-scaler.log --populate-coverage coverage.csv")