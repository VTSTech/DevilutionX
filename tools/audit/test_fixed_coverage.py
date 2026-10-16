#!/usr/bin/env python3
"""Test the fixed coverage population functionality."""

import sys
import os
sys.path.insert(0, os.path.dirname(__file__))

from field_log_audit_interactive import InteractiveAuditDigest

def test_fixed_functionality():
    """Test the fixed coverage functionality."""
    print("=== Testing Fixed Coverage Population ===\n")
    
    # Create mock digest
    d = InteractiveAuditDigest()
    
    # Add sample data from field log
    sample_presets = [
        {
            "name": "fxaa",
            "dir": "anti-aliasing",
            "result": "running",
            "passes": 1,
            "cache_hits": 1,
            "flags": set(),
            "png_textures": set(),
            "dev_notes": ""
        },
        {
            "name": "fxaa+linear",
            "dir": "anti-aliasing",
            "result": "running",
            "passes": 2,
            "cache_hits": 1,
            "flags": set(),
            "png_textures": set(),
            "dev_notes": ""
        }
    ]
    
    for preset in sample_presets:
        d.order.append(preset["name"])
        d.agg[preset["name"]] = preset
    
    # Test data
    existing_csv = "/tmp/sample-coverage.csv"
    output_csv = "/tmp/fixed-test-output.csv"
    
    print("1. Testing UPDATE mode (should update existing, add new)")
    print("   Existing coverage:")
    with open(existing_csv, "r") as f:
        print(f.read())
    
    updated_count, added_count = d.populate_vulkan_coverage(output_csv, mode="update")
    print(f"   Result: {updated_count} updated, {added_count} added")
    
    print("\n   Updated coverage:")
    with open(output_csv, "r") as f:
        print(f.read())
    
    print("\n2. Testing APPEND mode (should preserve all, add only new)")
    # Copy existing to fresh output
    import shutil
    shutil.copy(existing_csv, output_csv)
    
    append_count, _ = d.populate_vulkan_coverage(output_csv, mode="append")
    print(f"   Result: {append_count} added")
    
    print("\n   Appended coverage:")
    with open(output_csv, "r") as f:
        print(f.read())
    
    print("\n3. Testing OVERWRITE mode (should replace with field log data + preserve others)")
    # Copy existing to fresh output
    shutil.copy(existing_csv, output_csv)
    
    overwrite_count, _ = d.populate_vulkan_coverage(output_csv, mode="overwrite")
    print(f"   Result: {overwrite_count} overwritten")
    
    print("\n   Overwritten coverage:")
    with open(output_csv, "r") as f:
        print(f.read())

if __name__ == "__main__":
    test_fixed_functionality()