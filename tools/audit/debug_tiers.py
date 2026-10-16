#!/usr/bin/env python3
"""Debug tier management."""

import csv
from collections import defaultdict

def debug_csv_loading():
    """Debug CSV loading."""
    csv_path = "/home/vtstech/workspace/DevilutionX/docs/audit/shader-test-list-expanded.csv"
    
    print(f"Loading CSV: {csv_path}")
    
    if not os.path.exists(csv_path):
        print("CSV file not found!")
        return
    
    data = []
    with open(csv_path, 'r', encoding='utf-8') as f:
        reader = csv.DictReader(f)
        for i, row in enumerate(reader):
            data.append(row)
            if i < 5:  # Show first 5 rows
                print(f"Row {i+1}: {row}")
    
    print(f"Total rows loaded: {len(data)}")
    
    # Organize by tier
    tiers = defaultdict(list)
    for row in data:
        tier = row.get('tier')
        if tier:
            tiers[tier].append(row)
    
    print(f"Total tiers: {len(tiers)}")
    for tier in sorted(tiers.keys()):
        print(f"  {tier}: {len(tiers[tier])} presets")

if __name__ == "__main__":
    import os
    debug_csv_loading()