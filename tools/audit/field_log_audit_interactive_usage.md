# Interactive Field Log Audit Tool Usage Guide

The enhanced `field_log_audit_interactive.py` tool provides comprehensive analysis of DevilutionX GPU shader field logs with interactive features for manual status management and development note collection.

## Features

### Core Functionality
- **PNG Texture Tracking**: Successfully tracks PNG texture loads
- **Failure Categorization**: Maps exceptions to specific fix locations
- **Interactive Preset Sweeping**: Manual status selection with prompts
- **Development Notes Collection**: Optional note-taking during sweeps
- **Vulkan Coverage Population**: Automatically generate coverage CSVs
- **Audit Integration**: Direct comparison with audit CSVs
- **Validation Mode**: Data quality checks

### Interactive Features
- **Status Management**: Choose between `correct`, `running`, `not_running`, or `skip`
- **Smart Defaults**: Preserves existing `not_running` status
- **Development Notes**: Optional detailed notes for each shader
- **Progress Tracking**: Shows current status before prompting
- **Batch Processing**: Efficient sweep through all presets

## Usage Examples

### 1. Populate Vulkan Coverage CSV
```bash
# Automatically generate vulkan-coverage-updated.csv from field log
python3 tools/audit/field_log_audit_interactive.py gpu-scaler.log --populate-coverage

# Specify custom output file
python3 tools/audit/field_log_audit_interactive.py gpu-scaler.log --populate-coverage my-coverage.csv
```

### 2. Interactive Preset Sweep
```bash
# Basic interactive sweep
python3 tools/audit/field_log_audit_interactive.py gpu-scaler.log --sweep

# Sweep with development notes
python3 tools/audit/field_log_audit_interactive.py gpu-scaler.log --sweep --dev-notes

# Complete workflow with audit integration
python3 tools/audit/field_log_audit_interactive.py gpu-scaler.log \
  --sweep --dev-notes \
  --audit-csv docs/audit/shader-test-list-updated.csv \
  --tsv sweep-report.tsv
```

### 3. Combined Workflow
```bash
# Full workflow: populate coverage + interactive sweep + audit report
python3 tools/audit/field_log_audit_interactive.py gpu-scaler.log \
  --populate-coverage docs/audit/vulkan-coverage-updated.csv \
  --sweep --dev-notes \
  --audit-csv docs/audit/shader-test-list-updated.csv \
  --tsv complete-report.tsv
```

## Interactive Sweep Process

### Status Options
When prompted for each preset, you can choose:
- **`c` or `correct`**: Mark as fully validated and confirmed
- **`r` or `running`**: Mark as actively being tested (default)
- **`n` or `not_running`**: Mark as having issues/failures
- **`s` or `skip`**: Skip this preset (preserves current status)

### Development Notes
When `--dev-notes` is enabled and you choose `correct` or `not_running`, you'll be prompted for:
- **Issue details**: What problems were observed
- **Performance notes**: Frame rates, cache hit efficiency
- **Visual feedback**: Image quality issues, artifacts
- **Next steps**: What fixes are needed

### Smart Status Management
- **Preserves existing `not_running`**: Won't prompt for presets already marked as failed
- **Context-aware**: Shows current status, passes, cache hits, and features
- **Efficient**: Skips presets that don't need changes

## Output Formats

### Coverage CSV Generation
When using `--populate-coverage`, creates a CSV with:
```csv
preset,status,category,notes
anti-aliasing/fxaa.slangp,running,ok,"1 pass, 1 cache hits, features: none"
reshade/magicbloom.slangp,running,f1-mipmaps,"12 passes, 0 cache hits, features: F1[0,1,2,3,4,5,6,7,8,9,10], feedback"
blurs/dual_filter_bloom.slangp,running,feedback,"24 passes, 20 cache hits, features: F1[20], feedback"
```

### TSV Output
When using `--tsv`, creates a detailed TSV with:
```csv
name    dir     status  passes  cache_hits   flags   png_textures   dev_notes
anti-aliasing/fxaa.slangp   anti-aliasing running 1       1       -       -       "First field test: clean shutdown"
```

### Progress Report
When using `--audit-csv`, shows tier-by-tier progress:
```text
--- audit progress report ---
Compared to: shader-test-list-updated.csv

Tier 1a — F5 history-ring canaries: 7 correct, 0 running, 0 not_running (7 total)
Tier 1b — field round 1 fix confirmations: 8 correct, 0 running, 0 not_running (8 total)

Overall: 15 correct, 0 running, 0 not_running (15 total)
```

## Status Categories

### Automatic Status Determination
- **`not_running`**: When preset loading fails (fallback detected)
- **`parsing`**: When preset ends without successful load
- **`running`**: Default for successfully loaded presets
- **`correct`**: When manually confirmed during sweep

### Coverage Categories
- **`ok`**: Basic working presets
- **`png-textures`**: Presets using PNG LUTs/textures
- **`f1-mipmaps`**: Presets using F1 mip chain feature
- **`feedback`**: Presets using feedback textures
- **`needs-fix`**: Presets with known issues

## Best Practices

### 1. Workflow Sequence
```bash
# Step 1: Populate coverage from fresh field log
python3 tools/audit/field_log_audit_interactive.py gpu-scaler.log --populate-coverage

# Step 2: Interactive sweep for manual verification
python3 tools/audit/field_log_audit_interactive.py gpu-scaler.log --sweep --dev-notes

# Step 3: Generate audit progress report
python3 tools/audit/field_log_audit_interactive.py gpu-scaler.log --audit-csv shader-test-list-updated.csv
```

### 2. Sweep Strategies
- **Quick sweep**: Use `--sweep` only for status updates
- **Detailed sweep**: Use `--sweep --dev-notes` for comprehensive documentation
- **Validation sweep**: Focus on `not_running` presets to identify issues

### 3. Note-Taking Tips
- **Be specific**: "Screen flickering on pass 3" vs "visual issues"
- **Include metrics**: "60 FPS stable" vs "runs well"
- **Document fixes**: "Fixed by ROB-19 sample_mask patch" vs "issue resolved"
- **Reference builds**: "Tested on build e7f213c31" vs "recent test"

## Integration with Existing Tools

### Compatibility
- Works with existing `field_log_audit.py` outputs
- Compatible with audit CSV files from `shader-test-list-updated.csv`
- Supports TSV output for downstream processing

### Data Consistency
- Preserves existing status when appropriate
- Updates both local cache and output files
- Maintains audit trail through development notes

## Troubleshooting

### Common Issues
1. **Interactive prompts not appearing**: Ensure terminal supports interactive input
2. **CSV file not found**: Check file paths and permissions
3. **Notes not saving**: Ensure proper file write permissions
4. **Status not updating**: Check for typos in status responses

### Validation
- Use `--validate` to check for log truncation issues
- Compare TSV outputs across multiple runs
- Verify CSV file integrity after population

## Exit Codes
- `0`: Success, no issues
- `1`: Usage error or file not found
- `2`: Validation issues or interactive cancelled

This interactive tool bridges the gap between automated analysis and manual verification, providing the flexibility needed for comprehensive shader audit workflows while maintaining data consistency and audit trail integrity.