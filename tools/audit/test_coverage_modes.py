#!/usr/bin/env python3
"""Test the updated coverage population functionality."""

import sys
import os
sys.path.insert(0, os.path.dirname(__file__))

from field_log_audit_interactive import InteractiveAuditDigest

def test_coverage_modes():
    """Test different coverage population modes."""
    print("=== Testing Coverage Population Modes ===\n")
    
    # Create mock digest
    d = InteractiveAuditDigest()
    
    # Add sample data from field log
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
            "dev_notes": "First F1 mipmap field confirmation"
        }
    ]
    
    for preset in sample_presets:
        d.order.append(preset["name"])
        d.agg[preset["name"]] = preset
    
    # Test data
    existing_csv = "/tmp/sample-coverage.csv"
    output_csv = "/tmp/test-output.csv"
    
    print("1. Testing UPDATE mode (default)")
    print("   Existing data:")
    with open(existing_csv, "r") as f:
        print(f.read())
    
    updated_count, added_count = d.populate_vulkan_coverage(output_csv, mode="update")
    print(f"   Result: {updated_count} updated, {added_count} added")
    
    print("\n   Updated CSV:")
    with open(output_csv, "r") as f:
        print(f.read())
    
    print("\n2. Testing APPEND mode")
    # Reset output file
    import shutil
    shutil.copy(existing_csv, output_csv)
    
    append_count, _ = d.populate_vulkan_coverage(output_csv, mode="append")
    print(f"   Result: {append_count} added")
    
    print("\n   Appended CSV:")
    with open(output_csv, "r") as f:
        print(f.read())
    
    print("\n3. Testing OVERWRITE mode")
    # Reset output file
    shutil.copy(existing_csv, output_csv)
    
    overwrite_count, _ = d.populate_vulkan_coverage(output_csv, mode="overwrite")
    print(f"   Result: {overwrite_count} overwritten")
    
    print("\n   Overwritten CSV:")
    with open(output_csv, "r") as f:
        print(f.read())

if __name__ == "__main__":
    test_coverage_modes()