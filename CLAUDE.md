# CLAUDE.md - DentScanComparePro Project Context

## Project Overview

**DentScanComparePro** (working name: DentScanBatch) is a Qt/VTK/CGAL application for automated batch evaluation of dental intraoral scanner accuracy. It computes ISO 5725/12836-compliant trueness and precision metrics across multiple scanners and clinical conditions (SKD levels).

## Key Technical Context

### Build Environment
- **Qt 6** (Qt6 compatible - custom VTK build in ~/VTK-install-linux)
- **VTK 9.3** custom build with Qt6 support (location: `~/VTK-install-linux`)
- **CGAL 6.0.1** (property_map API uses `std::optional`, not `std::pair`)
- **Eigen 3.4.0**
- **nanoflann 1.7** (header-only KD-tree)
- **Platform**: Debian 13 / Linux

**Note**: Both DentScanCompare and this project use Qt6 with custom VTK from ~/VTK-install-linux. Same setup works for Windows 11 cross-compilation.

### CMake Critical Setup
```cmake
project(DentScanComparePro VERSION 1.0 LANGUAGES CXX C)  # C required for MPI

# Point to custom VTK installation
set(VTK_DIR "$ENV{HOME}/VTK-install-linux/lib/cmake/vtk-9.3")

find_package(Qt6 REQUIRED COMPONENTS Widgets Concurrent PrintSupport OpenGL OpenGLWidgets)
find_package(MPI QUIET)  # BEFORE VTK
if(NOT TARGET MPI::MPI_C)
    add_library(MPI::MPI_C INTERFACE IMPORTED GLOBAL)
endif()
find_package(VTK 9.3 REQUIRED COMPONENTS ...)
```

### Code Reuse from DentScanCompare
The `src/core/` directory should contain copies (not symlinks) of these files from `/home/kkunzelm/claude-code/DentScanCompare/src/core/`:
- `Mesh.h` - SurfaceMesh type aliases
- `STLReader.{h,cpp}` - Binary STL parser with winding fix
- `CurvatureAnalysis.{h,cpp}` - CGAL curvature computation
- `ICPRegistration.{h,cpp}` - Point-to-plane ICP
- `GPAReference.{h,cpp}` - Generalized Procrustes Analysis
- `DistanceField.{h,cpp}` - CGAL AABB-tree distance computation
- `MetricReport.h` - Metric data structure

Also copy from `src/visualization/`:
- `VTKMeshWidget.{h,cpp}` - VTK rendering widget (will need ROI extensions)
- `ColorMapLUT.{h,cpp}` - Color lookup tables

## Architecture Summary

Single application with dual modes:
1. **GUI Mode** (default): Interactive ROI template editing + batch runner
2. **CLI Mode** (`--batch`): Headless batch processing

### Data Flow
```
study.yaml (config) + STL files → BatchRunner → CSV metrics output
                                      ↑
                     ROI templates defined in Template Editor
```

### Key Abstractions
- **StudyConfig**: YAML-driven pipeline configuration
- **GroupConfig**: Per-SKD-level settings (file patterns, ROI parameters)
- **ROIConfig**: Layered ROI (bounding box → Z-plane → brush → σ-clip)
- **GroupProcessor**: Processes one SKD group (load → GPA → metrics)

## Important Design Decisions

1. **Per-SKD GPA**: Compute separate reference mesh for each SKD level
2. **ROI Transferability**: ROI defined on one scan transfers to all scans in same SKD group after alignment
3. **Layered ROI**: Box clips first, then Z-plane, then brush overrides, then statistical outlier removal
4. **iTero 18mm**: Included as separate SKD level (unbalanced design accepted)

## File Locations

- **Implementation Plan**: `docs/implementation-plan-DentScanComparePro.md`
- **Source DentScanCompare**: `/home/kkunzelm/claude-code/DentScanCompare/`
- **Scanner Data**: `/home/kkunzelm/claude-code/match3d-plus/data/3d-data/stl/`
- **DentScanCompare Docs**: `/home/kkunzelm/claude-code/DentScanCompare/docs/developer-handoff.md`

## Common Pitfalls (from DentScanCompare)

1. **CGAL 6.0 property_map**: Use `mesh.property_map<T>("name")` returns `std::optional`, not `std::pair`
2. **VTK threading**: VTK objects are NOT thread-safe; emit signals to main thread for rendering
3. **Qt signal cascades**: Use `QSignalBlocker` when programmatically changing checkable buttons
4. **STL winding**: Primescan exports reversed normals; STLReader handles this
5. **ICP coordinate systems**: FussenS6000/iTeroLumina are 28mm offset; PCA coarse alignment required

## Coding Standards

- C++17 standard
- Use `std::shared_ptr` for mesh data
- Use Qt signals/slots for async operations
- Keep computation in `src/core/` and `src/batch/` (no Qt GUI dependencies)
- Keep GUI in `src/gui/` (Qt/VTK dependencies allowed)
