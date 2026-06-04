# Changelog

All notable changes to DentScanComparePro are documented in this file.

## [Unreleased] - 2025-06-04

### Added
- **Brush zone visualization**: Brush tool now shows distinct colors on the mesh:
  - Include zones: bright green
  - Exclude zones: near black
  - Title bar shows vertex counts for each zone type
  - Previously only showed yellow spheres at click points
- **ROI Template field in Study Configuration**: New "ROI Template" path field allows specifying a JSON template file for batch processing. The template is used for masked ICP alignment and ROI-based metric computation.
- **Masked ICP Output directory**: New optional "Masked ICP Output" path field in Study Configuration. When masked ICP is enabled and this path is specified, results are saved to this separate directory instead of the main Output Dir.
- **"Use ROI mask for registration" checkbox**: New checkbox in Batch Processing tab under "Registration Options" controls whether masked ICP is used:
  - **Checked**: ICP alignment focuses on tooth surfaces only (requires ROI template with tooth seeds)
  - **Unchecked**: Full-mesh ICP is used, but ROI is still applied for metric computation
- **About dialog**: Help menu now includes "About DentScanComparePro" with author information (Prof. Dr. Karl-Heinz Kunzelmann) and clickable link to [www.kunzelmann.de](https://www.kunzelmann.de).
- **Editable template path**: The template scan path in ROI Template Editor is now editable. You can type/paste a path directly, press Enter or click Load, or use Browse.

### Changed
- **Masked ICP now uses combined ROI**: When "Use ROI mask for registration" is enabled, masked ICP uses all active ROI components combined with AND logic:
  - Bounding Box (if Active)
  - Plane Slab (if Active)
  - Brush zones (if any defined)
  - Tooth mask (if "Use tooth mask as ROI" checked)
  - Previously, masked ICP only worked with tooth seeds
- **ROI Template Editor layout**: Right panel now uses a scroll area for better vertical space management, with increased width (380-420px) for font readability
- **Window title**: Now includes author attribution: "DentScanComparePro - Scanner Accuracy Evaluation (Prof. K.-H. Kunzelmann)"
- **Plane Slab UI labels**:
  - Group box renamed from "Z-Plane Slab (Occlusal Region)" to "Plane Slab (ROI Height)"
  - "Above Occlusal" renamed to "Offset A"
  - "Below Occlusal" renamed to "Offset B"
- **QC output directory structure**:
  - `qc/gpa_means/` renamed to `qc/reference_meshes/`
  - `*_gpa_mean.stl` files renamed to `*_reference.stl`
  - This clarifies that the reference mesh may be either a GPA-computed mean OR an external reference

### Fixed
- **Batch cancellation**: Cancel button now properly stops batch processing mid-group. Previously, cancellation only checked between groups; now the GroupProcessor also checks the cancellation flag during loading, curvature computation, alignment, and distance computation stages.

### Technical Details

#### Cancellation Fix
- Added `setCancelFlag(std::atomic<bool>*)` to `GroupProcessor` to share the `BatchRunner`'s cancellation flag
- Changed `GroupProcessor` internal checks from `m_cancelled` to `wasCancelled()` which checks both local and external flags

#### ROI Template Integration
- `MainWindow::runBatch()` now loads the ROI template from the specified path
- Template is passed to `BatchRunner::run()` as `std::optional<ROITemplate>`
- When a template with tooth seeds is provided, masked ICP focuses alignment on tooth surfaces

#### File Naming
- `QCExporter::exportGPAMean()` renamed to `QCExporter::exportReferenceMesh()`
- All path references updated throughout `MainWindow.cpp` and `QCExporter.cpp`
