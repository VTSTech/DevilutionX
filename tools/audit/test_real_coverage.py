#!/usr/bin/env python3
"""Test coverage population with real field log data."""

import sys
import os
sys.path.insert(0, os.path.dirname(__file__))

from field_log_audit_interactive import InteractiveAuditDigest

def test_with_real_log():
    """Test with the actual field log from Diablo."""
    print("=== Testing Coverage Population with Real Field Log ===\n")
    
    # Create digest and feed real log data
    d = InteractiveAuditDigest()
    log_path = "/home/vtstech/workspace/Diablo/gpu-scaler.log"
    
    if os.path.exists(log_path):
        print(f"Processing real log: {log_path}")
        d.feed(log_path)
        
        print(f"Found {len(d.order)} presets in log:")
        for i, preset in enumerate(d.order, 1):
            rec = d.agg[preset]
            print(f"  {i}. {preset} ({rec['result']})")
        
        # Test different modes
        existing_csv = "/tmp/sample-coverage.csv"
        output_csv = "/tmp/real-test-output.csv"
        
        print(f"\n1. Testing UPDATE mode with existing coverage:")
        print("   Existing coverage:")
        with open(existing_csv, "r") as f:
            print(f.read())
        
        updated_count, added_count = d.populate_vulkan_coverage(output_csv, mode="update")
        print(f"   Result: {updated_count} updated, {added_count} added")
        
        print("\n   Updated coverage:")
        with open(output_csv, "r") as f:
            print(f.read())
        
        print("\n2. Testing APPEND mode:")
        # Copy existing to fresh output
        import shutil
        shutil.copy(existing_csv, output_csv)
        
        append_count, _ = d.populate_vulkan_coverage(output_csv, mode="append")
        print(f"   Result: {append_count} added")
        
        print("\n   Appended coverage:")
        with open(output_csv, "r") as f:
            print(f.read())
        
    else:
        print(f"Log file not found: {log_path}")

if __name__ == "__main__":
    test_with_real_log()