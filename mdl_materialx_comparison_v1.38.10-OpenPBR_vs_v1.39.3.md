# Comparison Summary: MDL MaterialX Files

## Branches Compared
- **Current Branch**: v1.38.10-OpenPBR
- **Comparison Branch**: v1.39.3
- **Directory**: `source/MaterialXGenMdl/mdl/materialx`

## Overview

The current branch (v1.38.10-OpenPBR) has **significant differences** compared to v1.39.3, with **14 files changed** involving approximately **1,792 insertions and 1,776 deletions**.

## Files Added in Current Branch (Not in v1.39.3)

- **`swizzle.mdl`** - New file with ~1,007 lines
  - Contains comprehensive swizzle functions for vector type conversions
  - Supports conversions between float, float2, float3, float4, color, and color4 types
  - Provides all combinations of component access (x, y, z, w, xx, xy, xz, etc.)

## Files Deleted from v1.39.3 (Not in Current Branch)

- `pbrlib_1_9.mdl` - Removed (373 lines deleted)
- `pbrlib_1_10.mdl` - Removed (234 lines deleted)  
- `stdlib_1_9.mdl` - Removed (388 lines deleted)
- `stdlib_1_10.mdl` - Removed (18 lines deleted)

**Total**: 1,013 lines removed across 4 version-specific files

## Files Modified

### Major Changes

#### 1. `stdlib_1_6.mdl` - Heavily Refactored
- **Lines changed**: 1,214 (659 insertions, 555 deletions)
- Largest modification in the comparison
- Likely restructured to use the new `swizzle.mdl` module
- Extensive reorganization of standard library functions

#### 2. `pbrlib_1_6.mdl` - Significant Modifications
- **Lines changed**: 180
- **Added**: Import for `swizzle` module
- **Removed enum**: `mx_sheen_mode` (conty_kulla, zeltner)
- **Parameter removals**:
  - `mxp_energy_compensation` from `mx_oren_nayar_diffuse_bsdf`
  - `mxp_top_weight` from `mx_dielectric_bsdf` and `mx_generalized_schlick_bsdf`
  - `multiscatter_tint` parameters from microfacet functions
  - `mxp_color82` parameter from `mx_generalized_schlick_bsdf`
- **Simplified calculations**: Removed `* mxp_top_weight` multiplications from tint calculations
- **Simplified layering**: Removed explicit top weight handling in BSDF layering

#### 3. `core.mdl` - Minor Changes
- **Lines changed**: 13
- **Removed**: Swizzle helper methods (moved to dedicated `swizzle.mdl`)
  - `mx_swizzle_xy(float2)`
  - `mx_swizzle_xy(float3)`
  - `mx_swizzle_xy(float4)`
  - `mx_swizzle_xy(color)`
  - `mx_swizzle_xy(color4)`
- **Note**: Contains some typo reversions:
  - "respective" → "repective"
  - "multiple" → "mutliple"
  - "parameter" → "paramater"

#### 4. Other Modified Files
- **`stdlib_1_8.mdl`** - 79 lines changed
- **`noise.mdl`** - 15 lines changed
- **`sampling.mdl`** - 19 lines changed
- **`pbrlib.mdl`** - 10 lines changed
- **`pbrlib_1_7.mdl`** - 8 lines changed
- **`stdlib.mdl`** - 10 lines changed

## Key Architectural Changes

### 1. Swizzle Refactoring
The current branch extracts all swizzle/vector conversion functions into a dedicated `swizzle.mdl` module:
- Provides comprehensive support for type conversions
- Supports all swizzle combinations (single, double, triple, quad component access)
- Cleaner separation of concerns from core functionality

### 2. Version Rollback
Removal of MDL 1.9 and 1.10 specific files indicates:
- Targets earlier MDL versions (likely MDL 1.6-1.8)
- Removes features that require newer MDL capabilities
- Ensures broader compatibility with older renderers

### 3. Simplified BSDF Parameters
Several advanced parameters removed:
- **Energy compensation flags**: Removed from Oren-Nayar diffuse
- **Top weight layering**: Removed explicit layer weight control
- **Multi-scatter tint**: Simplified to single tint parameter
- **Additional color parameters**: Reduced color control points (removed color82)

This simplification suggests:
- Focus on essential physically-based rendering features
- Reduced complexity for end users
- Possible performance optimizations

### 4. API Simplification
The changes indicate a move toward:
- Simpler, more streamlined API
- Compatibility with older MDL versions
- Reduced parameter count in key functions
- More implicit behaviors vs. explicit controls

## Current Branch File List

Files present in v1.38.10-OpenPBR:
- `core.mdl`
- `hsv.mdl`
- `noise.mdl`
- `pbrlib.mdl`
- `pbrlib_1_6.mdl`
- `pbrlib_1_7.mdl`
- `pbrlib_1_8.mdl`
- `sampling.mdl`
- `stdlib.mdl`
- `stdlib_1_6.mdl`
- `stdlib_1_7.mdl`
- `stdlib_1_8.mdl`
- `swizzle.mdl` *(new)*

## v1.39.3 File List

Files present in v1.39.3 (includes all above except swizzle.mdl, plus):
- `pbrlib_1_9.mdl` *(removed in current)*
- `pbrlib_1_10.mdl` *(removed in current)*
- `stdlib_1_9.mdl` *(removed in current)*
- `stdlib_1_10.mdl` *(removed in current)*

## Analysis & Implications

### Purpose of Changes
This appears to be a deliberate backport or adaptation for OpenPBR compatibility, trading some advanced features for:
- **Broader compatibility**: Works with older MDL versions
- **Simpler implementation**: Fewer parameters and edge cases to handle
- **Cleaner architecture**: Better code organization with swizzle module
- **OpenPBR focus**: Tailored specifically for OpenPBR specification needs

### Trade-offs
**Gained:**
- Better code organization (swizzle module)
- Simpler API surface
- Broader renderer compatibility
- Focused feature set

**Lost:**
- Advanced MDL 1.9/1.10 features
- Energy compensation controls
- Fine-grained layering weight control
- Multi-scatter tint customization
- Additional color curve control points

### Recommendation
The current branch represents a **strategic simplification** appropriate for:
- OpenPBR-focused workflows
- Environments requiring MDL 1.6-1.8 compatibility
- Users preferring simpler, more predictable material behaviors
- Systems where advanced PBR controls are not critical

For projects requiring cutting-edge MDL features or maximum material control, v1.39.3 would be more appropriate.

---

*Generated: November 25, 2025*
*Analysis based on git diff between v1.38.10-OpenPBR and v1.39.3*


