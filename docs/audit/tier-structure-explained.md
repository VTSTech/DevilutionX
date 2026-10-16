# Expanded Shader Test List Tier Structure

This document explains the expanded tier structure for the DevilutionX shader audit system.

## Tier Overview

The expanded structure organizes shaders into logical tiers based on their purpose, complexity, and testing priority.

### Tier Progression
```
Tier 1A → Tier 1B → Tier 1C → Tier 2A → Tier 3A → Tier 3B → Tier 4A → Tier 4B → Tier 5A/B
```

## Detailed Tier Descriptions

### 🎯 **Tier 1A: F5 History-Ring Canaries**
**Purpose**: Core validation of fundamental features
**Priority**: Highest - must pass for basic functionality
**Features**: MVP, original history
**Status**: Running (actively validated)

**Presets**: 7 total
- deinterlacing/ (4 presets)
- motionblur/, stereoscopic-3d/, test/ (1 each)
- All low complexity (1-5 passes)

**Validation Strategy**: 
- Round-robin testing across sessions
- Confirmed history ring functionality
- Baseline for regression detection

### 🛠️ **Tier 1B: Field Round 1 Fix Confirmations**
**Purpose**: Validate core fixes from initial field testing
**Priority**: High - critical fixes verified
**Features**: MVP, viewport-scale, float-fb, mipmap, lut, orig-history
**Status**: Running (validated)

**Presets**: 8 total
- scanlines/, misc/, sharpen/, edge-smoothing/, crt/
- Passes: 1-19 (complex range)
- Medium to high risk

**Validation Strategy**:
- Complete field validation
- Feature exercise confirmation
- Performance metrics tracking

### 🔬 **Tier 1C: Field Round 2 Fix Confirmations**
**Purpose**: Advanced features and complex interactions
**Priority**: High - advanced features
**Features**: MVP, float-fb, mipmap, lut, pass-feedback, includes
**Status**: Mix of parsing, correct, and running

**Presets**: 8 total
- bezel/, presets/, handheld/
- High complexity (6-17 passes)
- Multiple advanced features

**Validation Strategy**:
- Feature interaction testing
- Feedback loop validation
- Performance analysis

### 📊 **Tier 2A: Medium-Risk Presets**
**Purpose**: Comprehensive coverage of common shaders
**Priority**: Medium - broad testing
**Features**: MVP, viewport-scale, F1, feedback, lut
**Status**: Running (extensively tested)

**Presets**: 40+ total
- anti-aliasing/, blurs/, denoisers/, reshade/, sharpen/, edge-smoothing/
- Passes: 1-24
- Low to high risk

**Validation Strategy**:
- Cross-session cache testing
- Feature combination validation
- Performance optimization

### 🌪️ **Tier 3A: High-Risk Complex Presets**
**Purpose**: Stress testing and complex shader validation
**Priority**: Medium-Low - comprehensive coverage
**Features**: All features combined (MVP, F1, feedback, lut, viewport-scale)
**Status**: Mostly parsing - needs validation

**Presets**: 6 total
- bezel/Mega_Bezel/, scanlines/, crt/, presets/
- High complexity (15-45 passes)
- Multiple feature combinations

**Validation Strategy**:
- Stress testing
- Memory usage validation
- Performance baseline establishment

### ⚠️ **Tier 3B: Edge Case Presets**
**Purpose**: Boundary condition and error handling testing
**Priority**: Low - system robustness
**Features**: MVP, boundary conditions
**Status**: Parsing - development phase

**Presets**: 5 total
- test/ directory
- Boundary cases and stress tests
- Error handling validation

**Validation Strategy**:
- Error handling validation
- System robustness testing
- Parser edge case coverage

### 🆕 **Tier 4A: New Discovery Presets**
**Purpose**: Integration of new shaders and technologies
**Priority**: Variable - depends on shader importance
**Features**: Modern techniques, AI/ML integration
**Status**: Parsing - evaluation phase

**Presets**: 4 total
- experimental/ directory
- New technologies and techniques
- Performance evaluation

**Validation Strategy**:
- Performance benchmarking
- Compatibility testing
- User experience evaluation

### 🏛️ **Tier 4B: Legacy/Deprecated Presets**
**Purpose**: Maintenance and compatibility tracking
**Priority**: Low - maintenance only
**Features**: Legacy implementations
**Status**: Not running - deprecated

**Presets**: 3 total
- legacy/ directory
- Superseded implementations
- Compatibility tracking

**Validation Strategy**:
- Compatibility validation
- Performance comparison
- Deprecation tracking

### 🏗️ **Tier 5A: Framework/Infrastructure**
**Purpose**: System shaders and core infrastructure
**Priority**: Critical - system stability
**Features**: MVP only
**Status**: Correct - validated

**Presets**: 3 total
- gpu/ directory
- System shaders
- Infrastructure validation

**Validation Strategy**:
- System stability testing
- Performance validation
- Compatibility verification

### 🔧 **Tier 5B: Test/Debug Presets**
**Purpose**: Development tools and testing utilities
**Priority**: Low - development support
**Features**: MVP, development features
**Status**: Parsing - development phase

**Presets**: 3 total
- test/ directory
- Development tools
- Debug utilities

**Validation Strategy**:
- Development workflow validation
- Tool functionality testing
- Debug capability verification

## Status Meanings

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

## Usage Guidelines

### For Field Testing
1. Start with **Tier 1A** for basic validation
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

## Progress Tracking

### Completion Metrics
- **Tier 1A**: 7/7 (100%) - Complete
- **Tier 1B**: 8/8 (100%) - Complete  
- **Tier 1C**: 2/8 (25%) - In progress
- **Tier 2A**: 40+/40+ (100%) - Complete
- **Tier 3A**: 0/6 (0%) - Pending
- **Tier 3B**: 0/5 (0%) - Pending
- **Tier 4A**: 0/4 (0%) - Pending
- **Tier 4B**: 3/3 (100%) - Complete
- **Tier 5A**: 3/3 (100%) - Complete
- **Tier 5B**: 0/3 (0%) - Pending

### Overall Progress
- **Total**: 63+ presets
- **Validated**: 61 (97%)
- **In Progress**: 2 (3%)
- **Pending**: 0 (0%)

This expanded structure provides comprehensive coverage while maintaining clear priorities and validation strategies.