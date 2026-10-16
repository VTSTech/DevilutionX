# Field Log Audit Tool Usage Guide

The enhanced `field_log_audit.py` tool provides comprehensive analysis of DevilutionX GPU shader field logs with audit integration and improved reporting.

## Features

### Core Functionality
- **PNG Texture Tracking**: Addresses TEST-08 by tracking successful PNG texture loads
- **Failure Categorization**: Maps exceptions to audit specification categories
- **Audit Progress Tracking**: Compares against baseline CSVs to show progress
- **Performance Analysis**: Detailed performance metrics beyond basic cache hits
- **Log Validation**: Checks for common issues and data quality
- **Comparison Mode**: Compare digests across runs to track changes

### Failure Categories
The tool categorizes failures according to the audit specification:

| Category | Pattern | Location | Description |
|----------|---------|----------|-------------|
| slang_translation | `translating 'shader' (pass N): error` | slang_translate.cpp | SLANG → GLSL translation error |
| glslang_compile | `compiling 'shader' (pass N): error` | slang_translate.cpp | GLSL → SPIR-V compilation error |
| vulkan_validation | `vkCreateGraphicsPipelines`/`vkUpdateDescriptorSets` | preset_runner.cpp | Vulkan validation layer error |
| missing_texture | `unresolved texture 'name'` | preset_runner.cpp | Missing texture/LUT resource |
| preset_loading | `custom preset failed to load` | preset_parser.cpp | Preset loading failure |
| crash | `CRASH #N:` | preset_runner.cpp | Crash/segfault |
| unknown | Other errors/warnings | needs investigation | Unknown error/warning |

## Usage Examples

### Basic Usage
```bash
# Basic field log analysis
python tools/audit/field_log_audit.py gpu-scaler.log

# Save TSV output
python tools/audit/field_log_audit.py gpu-scaler.log --tsv audit-digest.tsv
```

### Audit Progress Tracking
```bash
# Compare against audit CSV to show progress
python tools/audit/field_log_audit.py gpu-scaler.log --audit-csv docs/audit/shader-test-list-2026-10-15.csv

# Generate progress report and save TSV
python tools/audit/field_log_audit.py gpu-scaler.log --audit-csv docs/audit/shader-test-list-2026-10-15.csv --tsv progress-digest.tsv
```

### Comparison Mode
```bash
# Compare current digest with previous one
python tools/audit/field_log_audit.py current-log.log --compare prev-digest.tsv
```

### Validation Mode
```bash
# Perform validation checks
python tools/audit/field_log_audit.py gpu-scaler.log --validate
```

### Combined Usage (Audit Workflow)
```bash
# Complete audit workflow for a field sweep
python tools/audit/field_log_audit.py gpu-scaler.log \
  --tsv field-sweep-digest.tsv \
  --audit-csv docs/audit/shader-test-list-2026-10-15.csv \
  --validate
```

## Output Format

### Summary Header
- Session information (builds, shutdowns)
- Load statistics (loads, unique presets, fallbacks)
- Performance metrics (cache hits, reinits, heartbeats)
- PNG texture tracking (successful loads, texture names)

### Per-Preset Table
- Name, directory, tier (from audit CSV)
- Load counts, pass counts, cache hits
- Feature flags (F1 mip chains, feedback textures)
- Result status (working/fallback/incomplete)
- PNG texture count

### Notables Section
- Longest shader chain
- F1 mip chain users
- Feedback texture users
- Re-loaded presets
- PNG texture users

### Failure Summary
- Count of failures by category
- Percentage breakdown

### Exceptions Section
- Grouped by failure category
- File and line number references
- Raw error messages

### Audit Progress Report (when --audit-csv specified)
- Tier-by-tier progress comparison
- Overall success rate
- Failed presets by tier

## Integration with Audit Workflow

### Field Sweep Process
1. Collect `gpu-scaler.log` from field testing
2. Run basic analysis: `python tools/audit/field_log_audit.py gpu-scaler.log`
3. Generate detailed report: `--tsv audit-digest.tsv`
4. Check progress: `--audit-csv docs/audit/shader-test-list-2026-10-15.csv`
5. Validate data quality: `--validate`

### Issue Tracking
- Use failure categorization to identify fix locations
- PNG tracking helps identify LUT loading issues
- Validation checks catch common data quality problems

### Progress Monitoring
- Compare digests across field sweeps
- Track tier completion rates
- Monitor failure rate trends

## Exit Codes
- `0`: Clean (no exceptions, validation passed)
- `1`: Usage error, file not found, or other I/O error
- `2`: Exceptions present or validation issues found
- `3`: Comparison mode detected changes

## Troubleshooting

### Common Issues
1. **PNG textures not tracked**: Ensure logs contain successful PNG load messages
2. **Missing tier information**: Check that audit CSV paths are correct
3. **Validation failures**: Review logs for truncation or excessive fallbacks
4. **Comparison differences**: Check for preset result changes or increased load counts

### Best Practices
1. Always run `--validate` to ensure data quality
2. Use `--tsv` for machine-readable output
3. Save baseline digests for comparison
4. Monitor failure categories to identify systemic issues
5. Use audit progress tracking to measure sweep effectiveness

## Enhancements Over Original

The enhanced tool addresses several limitations of the original `field_log_digest.py`:

1. **PNG Texture Tracking**: Successfully loads are now tracked (TEST-08 fix)
2. **Failure Categorization**: Exceptions are mapped to specific fix locations
3. **Audit Integration**: Direct comparison with audit CSVs
4. **Validation**: Data quality checks and common issue detection
5. **Comparison**: Track changes across multiple field sweeps
6. **Enhanced Reporting**: More detailed performance metrics and statistics

This tool significantly improves the audit workflow by providing more actionable insights and better integration with the tiered testing system.