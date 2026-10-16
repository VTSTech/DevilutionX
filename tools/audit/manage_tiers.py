#!/usr/bin/env python3
"""Tier management tool for shader test list."""

import argparse
import csv
import os
import sys
from collections import defaultdict, Counter
from datetime import datetime

class TierManager:
    def __init__(self, csv_path):
        self.csv_path = csv_path
        self.data = self.load_csv()
        self.tiers = self.organize_by_tier()
        
    def load_csv(self):
        """Load CSV data."""
        data = []
        if os.path.exists(self.csv_path):
            with open(self.csv_path, 'r', encoding='utf-8') as f:
                reader = csv.DictReader(f)
                for row in reader:
                    data.append(row)
        return data
    
    def organize_by_tier(self):
        """Organize data by tier."""
        tiers = defaultdict(list)
        for row in self.data:
            tier = row.get('tier', 'unknown')
            tiers[tier].append(row)
        return dict(tiers)
    
    def get_stats(self):
        """Get statistics by tier."""
        stats = {}
        for tier, presets in self.tiers.items():
            status_counts = Counter(p.get('status', 'unknown') for p in presets)
            risk_counts = Counter(p.get('risk', 'unknown') for p in presets)
            feature_counts = Counter()
            
            for preset in presets:
                features_str = preset.get('features', '')
                if features_str:
                    features = features_str.strip('"').split(', ')
                    for feature in features:
                        if feature.strip():
                            feature_counts[feature.strip()] += 1
            
            stats[tier] = {
                'total': len(presets),
                'status': dict(status_counts),
                'risk': dict(risk_counts),
                'features': dict(feature_counts),
                'presets': presets
            }
        return stats
    
    def generate_summary(self):
        """Generate tier summary."""
        stats = self.get_stats()
        lines = ["# Shader Test List Tier Summary", ""]
        
        total_presets = sum(s['total'] for s in stats.values())
        
        for tier in sorted(stats.keys() or []):
            stat = stats[tier]
            lines.append(f"## {tier}")
            lines.append(f"**Total Presets**: {stat['total']} ({stat['total']/total_presets*100:.1f}%)")
            
            if stat['status']:
                lines.append("**Status Breakdown**:")
                for status, count in sorted(stat['status'].items()):
                    lines.append(f"  - {status}: {count}")
            
            if stat['risk']:
                lines.append("**Risk Distribution**:")
                for risk, count in sorted(stat['risk'].items()):
                    lines.append(f"  - {risk}: {count}")
            
            if stat['features']:
                lines.append("**Feature Usage**:")
                for feature, count in sorted(stat['features'].items(), key=lambda x: x[1], reverse=True):
                    lines.append(f"  - {feature}: {count}")
            
            lines.append("")
        
        # Overall statistics
        lines.append("## Overall Statistics")
        lines.append(f"**Total Presets**: {total_presets}")
        lines.append(f"**Total Tiers**: {len(stats)}")
        
        # Status summary
        all_status = Counter()
        for stat in stats.values():
            all_status.update(stat['status'])
        
        lines.append("**Overall Status**:")
        for status, count in sorted(all_status.items()):
            lines.append(f"  - {status}: {count} ({count/total_presets*100:.1f}%)")
        
        return "\n".join(lines)
    
    def generate_todo_list(self):
        """Generate TODO list for incomplete presets."""
        stats = self.get_stats()
        lines = ["# Shader Audit TODO List", ""]
        
        for tier in sorted(stats.keys() or []):
            presets = stats[tier]['presets']
            incomplete = [p for p in presets if p.get('status') in ['parsing', 'needs-investigation']]
            
            if incomplete:
                lines.append(f"## {tier}")
                lines.append(f"**{len(incomplete)} presets need attention**")
                
                for preset in incomplete:
                    status = preset.get('status', 'unknown')
                    notes = preset.get('notes', '')
                    lines.append(f"- **{preset['preset']}** ({status})")
                    if notes:
                        lines.append(f"  - Notes: {notes}")
                lines.append("")
        
        return "\n".join(lines)
    
    def add_preset(self, preset_path, tier, passes, risk, features, status='parsing', category='needs-investigation', notes=''):
        """Add a new preset to the CSV."""
        new_preset = {
            'preset': preset_path,
            'tier': tier,
            'passes': passes,
            'risk': risk,
            'features': f'"{features}"',
            'status': status,
            'category': category,
            'notes': notes
        }
        
        self.data.append(new_preset)
        self.tiers = self.organize_by_tier()
        self.save_csv()
        
        print(f"✓ Added preset: {preset_path} to {tier}")
    
    def update_preset_status(self, preset_path, new_status, notes=''):
        """Update preset status."""
        for preset in self.data:
            if preset['preset'] == preset_path:
                old_status = preset.get('status', 'unknown')
                preset['status'] = new_status
                if notes:
                    preset['notes'] = notes
                
                self.save_csv()
                print(f"✓ Updated {preset_path}: {old_status} → {new_status}")
                return True
        
        print(f"✗ Preset not found: {preset_path}")
        return False
    
    def save_csv(self):
        """Save CSV data."""
        if not self.data:
            return
        
        fieldnames = ['preset', 'tier', 'passes', 'risk', 'features', 'status', 'category', 'notes']
        
        with open(self.csv_path, 'w', encoding='utf-8', newline='\n') as f:
            writer = csv.DictWriter(f, fieldnames=fieldnames)
            writer.writeheader()
            writer.writerows(self.data)
    
    def list_presets_by_status(self, status):
        """List presets by status."""
        presets = [p for p in self.data if p.get('status') == status]
        return presets
    
    def get_tier_progress(self):
        """Get progress by tier."""
        stats = self.get_stats()
        progress = {}
        
        for tier, stat in stats.items():
            total = stat['total']
            complete = sum(count for status, count in stat['status'].items() 
                         if status in ['correct', 'running'])
            pending = sum(count for status, count in stat['status'].items() 
                         if status in ['parsing', 'needs-investigation'])
            
            progress[tier] = {
                'total': total,
                'complete': complete,
                'pending': pending,
                'percentage': complete / total * 100 if total > 0 else 0
            }
        
        return progress

def main():
    parser = argparse.ArgumentParser(description="Manage shader test list tiers")
    parser.add_argument("csv_path", help="Path to shader test list CSV")
    parser.add_argument("--summary", action="store_true", help="Generate tier summary")
    parser.add_argument("--todo", action="store_true", help="Generate TODO list")
    parser.add_argument("--progress", action="store_true", help="Show tier progress")
    parser.add_argument("--stats", action="store_true", help="Show detailed statistics")
    parser.add_argument("--add-preset", nargs=7, metavar=('PATH', 'TIER', 'PASSES', 'RISK', 'FEATURES', 'STATUS', 'NOTES'),
                       help="Add new preset: PATH TIER PASSES RISK FEATURES STATUS NOTES")
    parser.add_argument("--update-status", nargs=2, metavar=('PATH', 'STATUS'),
                       help="Update preset status: PATH STATUS")
    parser.add_argument("--list-status", metavar='STATUS',
                       help="List presets by status")
    
    args = parser.parse_args()
    
    if not os.path.exists(args.csv_path):
        print(f"CSV file not found: {args.csv_path}")
        return 1
    
    manager = TierManager(args.csv_path)
    
    if args.summary:
        print(manager.generate_summary())
    
    elif args.todo:
        print(manager.generate_todo_list())
    
    elif args.progress:
        progress = manager.get_tier_progress()
        print("Tier Progress:")
        for tier, data in sorted(progress.items()):
            print(f"  {tier}: {data['complete']}/{data['total']} ({data['percentage']:.1f}%)")
    
    elif args.stats:
        stats = manager.get_stats()
        print("Detailed Statistics:")
        for tier, stat in sorted(stats.items()):
            print(f"\n{tier}:")
            print(f"  Total: {stat['total']}")
            print(f"  Status: {stat['status']}")
            print(f"  Risk: {stat['risk']}")
            print(f"  Top features: {dict(list(stat['features'].items())[:3])}")
    
    elif args.add_preset:
        preset_path, tier, passes, risk, features, status, notes = args.add_preset
        manager.add_preset(preset_path, tier, passes, risk, features, status, notes)
    
    elif args.update_status:
        preset_path, new_status = args.update_status
        notes = input(f"Notes for {preset_path} (press Enter to skip): ").strip()
        manager.update_preset_status(preset_path, new_status, notes)
    
    elif args.list_status:
        presets = manager.list_presets_by_status(args.list_status)
        print(f"Presets with status '{args.list_status}':")
        for preset in presets:
            print(f"  - {preset['preset']} ({preset.get('tier', 'unknown')})")
    
    else:
        # Default: show summary
        print(manager.generate_summary())

if __name__ == "__main__":
    sys.exit(main())