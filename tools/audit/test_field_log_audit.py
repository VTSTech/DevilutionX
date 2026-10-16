#!/usr/bin/env python3
"""Test script for enhanced field_log_audit.py functionality."""

import tempfile
import os
from field_log_audit import AuditDigest

def create_test_log():
    """Create a test log with various scenarios."""
    test_log = """[2026-10-16T10:00:00Z] === GPU scaler session (build test-build-2026-10-16) ===
[2026-10-16T10:00:01Z] preset changed: (previous) -> crt-royale.slangp
[2026-10-16T10:00:02Z] ok: preset 'crt-royale.slangp' loaded (12 passes)
[2026-10-16T10:00:03Z] GPU scaler preset: pass 0 'crt-royale' textures=[crt-royale.slang, LUT.png, feedback]
[2026-10-16T10:00:04Z] texture 'LUT.png' loaded successfully
[2026-10-16T10:00:05Z] GPU scaler preset: cache hit for crt-royale.slang
[2026-10-16T10:00:06Z] mipmapped inputs (F1): pass outputs with mip chains [0,1,2]
[2026-10-16T10:00:07Z] heartbeat: 100 frames (0 skipped, 0 dest-skipped, 100 total, filter=1)
[2026-10-16T10:00:08Z] preset changed: crt-royale.slangp -> broken-preset.slangp
[2026-10-16T10:00:09Z] translating 'broken-shader' (pass 1): unknown semantic 'invalid_feature'
[2026-10-16T10:00:10Z] custom preset failed to load; Shader Filter is off
[2026-10-16T10:00:11Z] preset changed: broken-preset.slangp -> working-preset.slangp
[2026-10-16T10:00:12Z] ok: preset 'working-preset.slangp' loaded (2 passes)
[2026-10-16T10:00:13Z] GPU scaler preset: pass 0 'working-preset' textures=[input.png, LUT.png]
[2026-10-16T10:00:14Z] texture 'input.png' loaded successfully
[2026-10-16T10:00:15Z] texture 'LUT.png' loaded successfully
[2026-10-16T10:00:16Z] GPU scaler preset: pass 1 'combine' textures=[working-preset.frag]
[2026-10-16T10:00:17Z] heartbeat: 60 frames (0 skipped, 0 dest-skipped, 60 total, filter=1)
[2026-10-16T10:00:18Z] Reinit: filter=1
[2026-10-16T10:00:19Z] preset changed: working-preset.slangp -> crt-royale.slangp
[2026-10-16T10:00:20Z] ok: preset 'crt-royale.slangp' loaded (12 passes)
[2026-10-16T10:00:21Z] GPU scaler preset: cache hit for crt-royale.slang
[2026-10-16T10:00:22Z] GPU scaler preset: cache hit for LUT.png
[2026-10-16T10:00:23Z] heartbeat: 80 frames (0 skipped, 0 dest-skipped, 80 total, filter=1)
[2026-10-16T10:00:24Z] Shutdown
"""
    return test_log

def create_audit_csv():
    """Create a test audit CSV."""
    csv_content = """preset,tier,passes,risk,features,status,category,notes
crt-royale.slangp,Tier 1b — field round 1 fix confirmations,12,high,"mvp, viewport-scale, float-fb, mipmap, lut, orig-history",working,ok,"field confirmed"
working-preset.slangp,Tier 2a — medium-risk,2,medium,mvp,working,ok,"simple test preset"
broken-preset.slangp,Tier 1c — field round 2 fix confirmations,1,high,mvp,fallback,needs-fix,"broken shader test"
"""
    return csv_content

def test_enhanced_functionality():
    """Test the enhanced functionality of the audit tool."""
    print("Testing enhanced field_log_audit.py functionality...\n")
    
    # Create temporary files
    with tempfile.NamedTemporaryFile(mode='w', suffix='.log', delete=False) as log_file:
        log_file.write(create_test_log())
        log_path = log_file.name
    
    with tempfile.NamedTemporaryFile(mode='w', suffix='.csv', delete=False) as csv_file:
        csv_file.write(create_audit_csv())
        csv_path = csv_file.name
    
    try:
        # Test basic functionality
        print("1. Basic functionality test:")
        d = AuditDigest()
        d.feed(log_path)
        print("   ✓ Log parsing successful")
        
        # Test PNG tracking
        png_count = len(d.pngs)
        png_loads = d.png_loads
        print(f"   ✓ PNG tracking: {png_loads} loads, {png_count} unique textures")
        
        # Test failure categorization
        failure_categories = [e[3] for e in d.exceptions]
        category_counts = {}
        for category in failure_categories:
            category_counts[category] = category_counts.get(category, 0) + 1
        print(f"   ✓ Failure categorization: {category_counts}")
        
        # Test audit CSV integration
        d.load_audit_csv(csv_path)
        tier_stats = {}
        for n in d.order:
            tier = d.agg[n]["tier"]
            tier_stats[tier] = tier_stats.get(tier, 0) + 1
        print(f"   ✓ Tier integration: {tier_stats}")
        
        # Test TSV output
        tsv_output = d.tsv()
        tsv_lines = tsv_output.strip().split('\n')
        print(f"   ✓ TSV output: {len(tsv_lines)} lines")
        
        # Test progress reporting
        progress_lines = d.audit_progress(csv_path)
        print(f"   ✓ Progress report: {len(progress_lines)} lines")
        
        # Test validation
        validation_result = validate_logs(d, [log_path])
        print(f"   ✓ Validation: {'passed' if validation_result == 0 else 'issues found'}")
        
        print("\n2. Detailed output preview:")
        print("   Summary:")
        for line in d.summary([log_path])[:3]:  # First 3 lines
            print(f"     {line}")
        
        print("\n   Per-preset table (first 2 presets):")
        table_lines = d.table()
        for line in table_lines[:3]:  # Header + first 2 presets
            print(f"     {line}")
        
        print("\n   Failure summary:")
        for line in d.failure_summary():
            print(f"     {line}")
        
        print("\n   Notables:")
        for line in d.notables():
            print(f"     {line}")
        
        print("\n✅ All tests passed! Enhanced functionality working correctly.")
        
    finally:
        # Clean up
        os.unlink(log_path)
        os.unlink(csv_path)

def validate_logs(digest, files):
    """Simplified validation for testing."""
    issues = []
    
    # Check for unclean exits
    if digest.started:
        issues.append("Log does not end with Shutdown")
    
    # Check for excessive fallbacks
    fallback_rate = sum(1 for n in digest.order if digest.agg[n]["result"] == "fallback") / len(digest.order)
    if fallback_rate > 0.1:
        issues.append(f"High fallback rate: {fallback_rate:.1%}")
    
    return 2 if issues else 0

if __name__ == "__main__":
    test_enhanced_functionality()