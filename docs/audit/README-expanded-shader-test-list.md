# Expanded Shader Test List for DevilutionX

This document describes the expanded shader test list structure and management system for DevilutionX GPU shader auditing.

## Overview

The expanded shader test list provides comprehensive coverage of all shaders in the DevilutionX repository, organized into logical tiers based on purpose, complexity, and testing priority. The system includes management tools for tracking progress and updating shader status.

## Tier Structure

### 🎯 **Tier 1: Core Validation (Priority 1)**
**Total: 24 presets (26.1%)**

#### Tier 1a: F5 History-Ring Canaries (8 presets, 100% complete)
- **Purpose**: Core validation of fundamental features
- **Features**: MVP, original history
- **Status**: All running
- **Examples**: deinterlacing/, motionblur/, stereoscopic-3d/, test/

#### Tier 1b: Field Round 1 Fix Confirmations (8 presets, 100% complete)
- **Purpose**: Validate core fixes from initial field testing
- **Features**: MVP, viewport-scale, float-fb, mipmap, lut, orig-history
- **Status**: All running
- **Examples**: scanlines/, misc/, sharpen/, edge-smoothing/, crt/

#### Tier 1c: Field Round 2 Fix Confirmations (8 presets, 25% complete)
- **Purpose**: Advanced features and complex interactions
- **Features**: MVP, float-fb, mipmap, lut, pass-feedback, includes
- **Status**: 2 correct, 6 pending
- **Examples**: bezel/, presets/, handheld/, koko-aio-ng variants

### 📊 **Tier 2: Comprehensive Coverage (Priority 2)**
**Total: 44 presets (47.8%)**

#### Tier 2a: Medium-Risk Presets (44 presets, 100% complete)
- **Purpose**: Comprehensive coverage of common shaders
- **Features**: MVP, viewport-scale, F1, feedback, lut
- **Status**: All running
- **Examples**: anti-aliasing/, blurs/, denoisers/, reshade/, sharpen/, edge-smoothing/

### 🌪️ **Tier 3: Advanced & Edge Cases (Priority 3)**
**Total: 11 presets (12.0%)**

#### Tier 3a: High-Risk Complex (6 presets, 0% complete)
- **Purpose**: Stress testing and complex shader validation
- **Features**: All features combined (MVP, F1, feedback, lut, viewport-scale)
- **Status**: All pending
- **Examples**: bezel/Mega_Bezel/, scanlines/, crt/, presets/

#### Tier 3b: Edge Cases (5 presets, 0% complete)
- **Purpose**: Boundary condition and error handling testing
- **Features**: MVP, boundary conditions
- **Status**: All pending
- **Examples**: test/ directory with boundary cases

### 🆕 **Tier 4: New & Legacy (Priority 4)**
**Total: 7 presets (7.6%)**

#### Tier 4a: New Discovery (4 presets, 0% complete)
- **Purpose**: Integration of new shaders and technologies
- **Features**: Modern techniques, AI/ML integration
- **Status**: All pending
- **Examples**: experimental/ directory

#### Tier 4b: Legacy/Deprecated (3 presets, 0% complete)
- **Purpose**: Maintenance and compatibility tracking
- **Features**: Legacy implementations
- **Status**: All not_running
- **Examples**: legacy/ directory

### 🏗️ **Tier 5: System & Development (Priority 5)**
**Total: 6 presets (6.5%)**

#### Tier 5a: Framework/Infrastructure (3 presets, 100% complete)
- **Purpose**: System shaders and core infrastructure
- **Features**: MVP only
- **Status**: All correct
- **Examples**: gpu/ directory

#### Tier 5b: Test/Debug (3 presets, 0% complete)
- **Purpose**: Development tools and testing utilities
- **Features**: MVP, development features
- **Status**: All pending
- **Examples**: test/ directory

## Management Tools

### Tier Management Script (`manage_tiers.py`)

```bash
# Generate tier summary
python3 tools/audit/manage_tiers.py docs/audit/shader-test-list-clean.csv --summary

# Generate TODO list
python3 tools/audit/manage_tiers.py docs/audit/shader-test-list-clean.csv --todo

# Show progress by tier
python3 tools/audit/manage_tiers.py docs/audit/shader-test-list-clean.csv --progress

# Show detailed statistics
python3 tools/audit/manage_tiers.py docs/audit/shader-test-list-clean.csv --stats

# Add new preset
python3 tools/audit/manage_tiers.py docs/audit/shader-test-list-clean.csv --add-preset "path/to/preset.slangp" "Tier Name" "passes" "risk" "features" "status" "notes"

# Update preset status
python3 tools/audit/manage_tiers.py docs/audit/shader-test-list-clean.csv --update-status "path/to/preset.slangp" "new_status"

# List presets by status
python3 tools/audit/manage_tiers.py docs/audit/shader-test-list-clean.csv --list-status "parsing"
```

### Field Log Audit Tool (`field_log_audit_interactive.py`)

```bash
# Run interactive audit
python3 tools/audit/field_log_audit_interactive.py \
  --field-log /path/to/gpu-scaler.log \
  --audit-csv /path/to/shader-test-list.csv \
  --coverage-csv /path/to/vulkan-coverage.csv \
  --mode update \
  --interactive
```

## Status Definitions

| Status | Description | Action Required |
|--------|-------------|----------------|
| **correct** | Fully validated and confirmed | None - maintenance mode |
| **running** | Actively being tested | Continue monitoring |
| **parsing** | In evaluation phase | Complete testing |
| **not_running** | Has issues/failures | Fix and re-test |
| **needs-investigation** | Requires analysis | Investigate and resolve |
| **deprecated** | Legacy/superseded | Track for removal |
| **boundary-test** | Edge case testing | Validate handling |
| **stress-test** | Performance testing | Establish baselines |
| **new-tech** | New technology | Evaluate integration |
| **dev-tool** | Development utility | Validate functionality |
| **infrastructure** | System component | Monitor stability |

## Risk Assessment

### Risk Levels
- **Low**: 1-5 passes, basic features
- **Medium**: 6-15 passes, moderate features  
- **High**: 16+ passes, complex features

### Feature Complexity
- **MVP**: Minimum viable product features
- **Viewport-scale**: Dynamic scaling
- **Float-fb**: Floating point framebuffers
- **Mipmap**: Texture mipmapping
- **LUT**: Look-up tables
- **Feedback**: Pass feedback loops
- **Includes**: External file inclusion
- **F1**: Mip chain feature

## Progress Tracking

### Current Status
- **Total Presets**: 92
- **Validated**: 65 (70.7%)
- **In Progress**: 2 (2.2%)
- **Pending**: 24 (26.1%)
- **Deprecated**: 3 (3.3%)

### Tier Completion
- **Tier 1a**: 8/8 (100%) ✅ Complete
- **Tier 1b**: 8/8 (100%) ✅ Complete
- **Tier 1c**: 2/8 (25%) 🔄 In Progress
- **Tier 2a**: 44/44 (100%) ✅ Complete
- **Tier 3a**: 0/6 (0%) ⏳ Pending
- **Tier 3b**: 0/5 (0%) ⏳ Pending
- **Tier 4a**: 0/4 (0%) ⏳ Pending
- **Tier 4b**: 0/3 (0%) ⏳ Pending
- **Tier 5a**: 3/3 (100%) ✅ Complete
- **Tier 5b**: 0/3 (0%) ⏳ Pending

## Usage Guidelines

### For Field Testing
1. Start with **Tier 1a** for basic validation
2. Progress through **Tier 1B** for critical fixes
3. Test **Tier 1C** for advanced features
4. Use **Tier 2A** for comprehensive coverage

### For Development
1. Use **Tier 3B** for boundary testing
2. Evaluate **Tier 4A** for new integrations
3. Monitor **Tier 5A** for system stability
4. Validate **Tier 5B** for development tools

### For Maintenance
1. Track **Tier 4B** for legacy compatibility
2. Monitor **Tier 3A** for performance regressions
3. Update **Tier 5A** for infrastructure changes
4. Validate **Tier 5B** for tool updates

## File Structure

```
docs/audit/
├── shader-test-list-clean.csv          # Main shader test list
├── shader-test-list-expanded.csv       # Original expanded version
├── vulkan-coverage-updated.csv         # Coverage tracking
├── tier-structure-explained.md        # Detailed tier documentation
└── README-expanded-shader-test-list.md # This file

tools/audit/
├── manage_tiers.py                     # Tier management tool
├── field_log_audit_interactive.py      # Interactive audit tool
├── field_log_audit_usage.md           # Usage documentation
└── field_log_audit_interactive_usage.md # Interactive usage guide
```

## Integration with CI/CD

The shader test list can be integrated into the CI/CD pipeline for automated testing:

```bash
# Run automated audit
python3 tools/audit/field_log_audit_interactive.py \
  --field-log /path/to/gpu-scaler.log \
  --audit-csv docs/audit/shader-test-list-clean.csv \
  --coverage-csv docs/audit/vulkan-coverage-updated.csv \
  --mode update \
  --non-interactive

# Check for failed presets
python3 tools/audit/manage_tiers.py docs/audit/shader-test-list-clean.csv --list-status "not_running"
```

## Contributing

### Adding New Shaders
1. Determine appropriate tier based on complexity
2. Add to CSV with proper tier classification
3. Set initial status to "parsing"
4. Run field testing to determine actual status

### Updating Existing Shaders
1. Use `manage_tiers.py` to update status
2. Include detailed notes about changes
3. Track performance metrics
4. Update risk assessment if needed

### Documentation Updates
- Keep this README updated with tier changes
- Document new shader categories
- Update progress tracking regularly
- Maintain usage guidelines

## Future Enhancements

1. **Automated Tier Assignment**: AI-based tier classification
2. **Performance Baselines**: Automated performance tracking
3. **Integration Testing**: Cross-shader compatibility testing
4. **User Feedback System**: Community-driven status updates
5. **Automated Regression Testing**: Continuous validation of existing shaders

## Support

For questions or issues:
1. Check the TODO list for pending items
2. Review the tier structure documentation
3. Use the management tools for status updates
4. Consult field testing documentation for procedures

---

*This document is part of the DevilutionX shader audit system and should be kept up to date with changes to the shader test list.*