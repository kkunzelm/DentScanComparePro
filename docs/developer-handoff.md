# DentScanComparePro – Developer Handoff

## Goal

Automated batch evaluation of dental intraoral scanner accuracy. Computes ISO 5725/12836-compliant trueness and precision metrics across multiple scanners and clinical conditions (SKD levels = inter-incisor distance).

Based on core algorithms from DentScanCompare (`/home/kkunzelm/claude-code/DentScanCompare/`), extended with:
- JSON/YAML-driven batch configuration
- Automated file discovery via glob patterns
- Per-SKD-level GPA alignment
- CSV output for statistical analysis (R, SPSS, etc.)

---

## Build Environment (Debian 13 / Linux)

| Library | Version | Notes |
|---------|---------|-------|
| Qt | 6.2+ | Custom VTK build in `~/VTK-install-linux` compiled with Qt6 |
| VTK | 9.3 | Custom build in `~/VTK-install-linux`; has MPI dependency |
| CGAL | 6.0.1 | Property-map API changed in 6.0 – see pitfall below |
| Eigen | 3.4.0 | Matrix math for ICP |
| nanoflann | 1.7 | Header-only KD-tree; lives in `/usr/include/nanoflann.hpp` |
| yaml-cpp | (optional) | For YAML config files; JSON works without it |

### Critical CMake Setup

```cmake
project(DentScanComparePro VERSION 1.0 LANGUAGES CXX C)  # C required for MPI

set(VTK_DIR "$ENV{HOME}/VTK-install-linux/lib/cmake/vtk-9.3" CACHE PATH "VTK installation")

find_package(Qt6 6.2 REQUIRED COMPONENTS Widgets Concurrent PrintSupport OpenGL OpenGLWidgets)
find_package(MPI QUIET)                                 # MUST come BEFORE VTK
if(NOT TARGET MPI::MPI_C)
    add_library(MPI::MPI_C INTERFACE IMPORTED GLOBAL)
endif()
find_package(VTK 9.3 REQUIRED COMPONENTS ...)
```

`LANGUAGES CXX C` enables MPI C-language detection. VTK's targets file unconditionally references `MPI::MPI_C`; without C language enabled, CMake fails.

### Build Commands

```bash
cd build
cmake .. -DCMAKE_BUILD_TYPE=Release
make -j4
```

---

## Source Layout

```
src/
├── main.cpp                    Dual-mode entry (GUI default, --batch for CLI)
├── core/                       Algorithms (no Qt GUI dependencies)
│   ├── Mesh.h                  SurfaceMesh type aliases + ScanData struct
│   ├── MetricReport.h          Plain metric aggregate struct
│   ├── STLReader.{h,cpp}       Binary STL → CGAL SurfaceMesh
│   ├── CurvatureAnalysis.{h,cpp}   CGAL interpolated curvatures
│   ├── ICPRegistration.{h,cpp}     Point-to-plane ICP (nanoflann + Eigen)
│   ├── GPAReference.{h,cpp}        GPA: PCA → 4-orient test → ICP → mean mesh
│   └── DistanceField.{h,cpp}       CGAL AABB-tree → per-vertex signed distances
├── config/                     Configuration parsing
│   ├── ROIConfig.h             ROI structures (bbox, Z-plane, brush zones)
│   ├── StudyConfig.{h,cpp}     JSON/YAML study configuration
│   └── FileDiscovery.{h,cpp}   Glob pattern file discovery
├── batch/                      Batch processing
│   ├── BatchRunner.{h,cpp}     Orchestrates all group processing
│   ├── GroupProcessor.{h,cpp}  Processes one SKD group
│   └── CSVWriter.{h,cpp}       Output CSV files
├── gui/                        (Future) Interactive ROI template editor
└── visualization/              VTK rendering (copied from DentScanCompare)
    ├── VTKMeshWidget.{h,cpp}
    └── ColorMapLUT.{h,cpp}
```

---

## Analysis Pipeline

### 1. STLReader
Binary STL → polygon soup → CGAL `SurfaceMesh`. Per-face winding verification against stored STL normal; fixes Primescan reversed-normal issue.

### 2. CurvatureAnalysis
`CGAL::Polygon_mesh_processing::interpolated_corrected_curvatures`. Stores mean curvature in `"v:mean_curv"`, Gaussian in `"v:gauss_curv"`.

### 3. GPAReference::compute
Three-stage alignment:
- **Stage 1 – PCA coarse alignment**: Translate to centroid, rotate largest-variance axis → X, smallest → Z.
- **Stage 2 – 4-orientation Z-rotation test**: Tries 0°/90°/180°/270° Z rotations, picks best via ICP score.
- **Stage 3 – GPA iterations**: Coarse ICP (15 mm, 30 iter), then fine ICP (5 mm, 100 iter). Converges when max displacement < 0.01 mm.
- **Stage 4 – Mean mesh update**: Reference vertices moved to mean of closest points on all aligned scans.

### 4. DistanceField
CGAL AABB tree on GPA mean. Per vertex: closest point + primitive, signed by dot(diff, face_normal).

- `compute(scan, reference)` – Stores distances in `scan.distanceToRef`
- `computePairwise(sourceMesh, targetMesh)` – Returns distance vector without modifying inputs (optimized for precision computation)
- `fillReport(scan, report, ...)` – Computes RMS, MAD, Hausdorff metrics with optional filtering

---

## Batch Processing System

### Configuration (JSON)

```json
{
  "study": {
    "name": "Scanner_Comparison_2024",
    "description": "6 scanners × 5 SKD levels"
  },
  "scanners": [
    {"id": "Primescan", "patterns": ["*Primescan*", "*PS*"]},
    {"id": "Trios5", "patterns": ["*Trios*", "*T5*"]}
  ],
  "groups": [
    {
      "id": "SKD_20",
      "skd_mm": 20,
      "file_patterns": ["**/SKD 20/*.stl", "**/20mm/*.stl"]
    }
  ],
  "output": {
    "metrics_csv": "trueness_metrics.csv",
    "precision_csv": "precision_metrics.csv",
    "summary_csv": "summary_stats.csv"
  }
}
```

### CLI Usage

```bash
./DentScanComparePro --batch \
    --study ../data/study.json \
    --data-root /path/to/scanner/data \
    --output /path/to/results \
    --verbose
```

### Output Files

**trueness_metrics.csv** – One row per scan:
- Observation_ID, Scanner_Model, SKD_Value, Repetition_ID
- Trueness_RMS_mm, Trueness_MeanAbs_mm, Trueness_Max_mm, Trueness_P95_mm
- Signed_Mean_mm, Coverage_Rate_pct, Vertices_Included, Vertices_Total, File_Path

**precision_metrics.csv** – One row per scanner per SKD:
- Scanner_Model, SKD_Value, Precision_MeanRMS_mm, Precision_SD_mm, Coefficient_of_Variation, Pairwise_Count

**summary_stats.csv** – Aggregated trueness statistics:
- Scanner_Model, SKD_Value, N, Mean_RMS_mm, SD_RMS_mm, Min_RMS_mm, Max_RMS_mm

---

## Known Pitfalls

### CGAL 6.0 property_map API Change

```cpp
// OLD (CGAL 5.x) – returns std::pair<PropertyMap, bool>
auto [map, ok] = mesh.property_map<VertexDesc, double>("v:mean_curv");

// NEW (CGAL 6.0) – returns std::optional<PropertyMap>
auto opt = mesh.property_map<VertexDesc, double>("v:mean_curv");
if (opt.has_value()) { auto map = opt.value(); ... }
```

Note: `add_property_map()` still returns `std::pair` in CGAL 6.0.

### STL Winding and Normal Orientation

Primescan exports triangles wound opposite to other scanners. `STLReader.cpp` has per-face cross-product check against stored STL normal – do not remove this.

### ICP Alignment of FussenS6000 and iTeroLumina

These scanners use coordinate system 28 mm offset from others. PCA coarse alignment is required before fine ICP (5 mm search radius).

### VTK Threading

VTK objects are NOT thread-safe. All VTK rendering must happen on the main thread. Use Qt signals to communicate results from worker threads.

### Qt Signal Cascades

When programmatically changing checkable buttons, wrap in `QSignalBlocker` to prevent handler cascades:

```cpp
if (on && m_otherBtn->isChecked()) {
    QSignalBlocker b(m_otherBtn);
    m_otherBtn->setChecked(false);
}
```

### File Discovery Performance

Original recursive glob implementation was extremely slow. Current implementation uses `QDirIterator` with `QDir::Files` flag – much faster for large directory trees.

---

## Current Status (as of 2026-06-02)

### Implemented
- Full CLI batch mode with JSON configuration
- File discovery with glob patterns and scanner ID matching
- Per-group GPA alignment and distance computation
- Trueness metrics (RMS, MAD, Max, P95, coverage rate)
- Precision metrics (pairwise RMS between repetitions)
- CSV output (metrics, precision, summary)
- Optimized `computePairwise()` for efficient precision computation

### Test Results (5 Primescan scans, SKD 20)
- Trueness RMS: 0.032–0.074 mm
- Precision Mean RMS: 0.269 mm (10 pairwise comparisons)
- Processing time: ~30 seconds for 5 scans

### Not Yet Implemented
- GUI mode (ROI Template Editor)
- YAML configuration support (JSON only currently working)
- ROI filtering (bbox, Z-plane, brush zones, sigma clipping)
- Tooth segmentation integration
- Multi-scanner full batch run (only tested with single scanner)

---

## Next Steps

1. **Full multi-scanner test**: Create configuration for all 6 scanners and all SKD levels
2. **ROI integration**: Connect ROIConfig filtering to GroupProcessor
3. **GUI development**: Phase 4 from implementation plan – ROI Template Editor
4. **Tooth segmentation**: Port ToothSegmentation from DentScanCompare for anatomical ROI

---

## Data Locations

- **Scanner data**: `/KHKsData/usr2/daten/P2026-Kessler-ScanVergleich/IOS/`
- **Test configurations**: `data/test_single_group.json`, `data/default_study.json`
- **Source DentScanCompare**: `/home/kkunzelm/claude-code/DentScanCompare/`
- **Implementation plan**: `docs/IMPLEMENTATION-PROMPT.md`

---

## Changelog

### 2026-06-02 – Precision optimization + batch completion
- Added `DistanceField::computePairwise()` for efficient pairwise distance computation
- Re-enabled precision metrics in GroupProcessor
- Fixed CLI argument parsing (`--study` separate from `--batch`)
- Added console progress output for all processing stages
- Successful test run with 5 Primescan scans producing trueness + precision metrics

### 2026-06-01 – Initial batch system
- Created configuration system (StudyConfig, ROIConfig, FileDiscovery)
- Created batch processing (GroupProcessor, BatchRunner, CSVWriter)
- Optimized file discovery with QDirIterator
- Added scanner ID filtering (skip "Unknown" scanner files)
