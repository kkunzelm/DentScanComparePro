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
│   ├── DistanceField.{h,cpp}       CGAL AABB-tree → per-vertex signed distances
│   └── ToothSegmentation.{h,cpp}   Dijkstra-based crown segmentation from seed points
├── config/                     Configuration parsing
│   ├── ROIConfig.{h,cpp}       ROI structures + ROITemplate JSON I/O
│   ├── StudyConfig.{h,cpp}     JSON/YAML study configuration
│   └── FileDiscovery.{h,cpp}   Glob pattern file discovery
├── batch/                      Batch processing
│   ├── BatchRunner.{h,cpp}     Orchestrates all group processing
│   ├── GroupProcessor.{h,cpp}  Processes one SKD group
│   └── CSVWriter.{h,cpp}       Output CSV files
├── gui/                        Interactive ROI template editor
│   └── MainWindow.{h,cpp}      Main window with tabs for config, ROI, batch, results
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

### 5. ToothSegmentation
Dijkstra-based region growing from seed points on tooth cusps. Expands outward and stops at the gingival margin using curvature-weighted geodesic distance.

**Parameters:**
- `maxGeodesicMm` (default 12.0): Primary budget limit – stops expansion when accumulated curvature-weighted distance exceeds this value
- `maxCreaseAngleDeg` (default 50°): Secondary hard stop at CEJ kink
- `minMeanCurvature` (default -4.0): Secondary hard stop at concave gingival sulcus
- `curvatureRepulsion` (default 0.1): Edge-cost scaling for concave zones (0 = disabled)

**Edge Cost Formula:**
```
W(f,nb) = dist(centroid_f, centroid_nb) × (1 + curvatureRepulsion × max(0, -κ_min_avg))
```

Where κ_min = κ_H - √max(0, κ_H² - κ_G) is the minimum principal curvature, more sensitive at saddle-like CEJ geometry.

**Usage:**
```cpp
ToothSegmentation::Params params;
params.maxGeodesicMm = 12.0;
std::vector<bool> toothMask = ToothSegmentation::segmentFromPoints(scan, seedPoints, params);
```

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
    --roi-template roi_template.json \
    --verbose
```

**Options:**
- `--batch` / `-b`: Run in headless CLI mode (no GUI)
- `--study` / `-s`: Path to study configuration JSON file
- `--data-root` / `-d`: Root directory containing scanner folders
- `--output` / `-o`: Output directory for CSV files (default: ./results)
- `--roi-template` / `-r`: Optional ROI template with tooth segmentation settings
- `--verbose`: Print detailed progress information

### Incremental Save & Resume

The batch processor saves results incrementally after each SKD group completes:

- **Automatic resume**: If the batch is interrupted, simply re-run the same command. Already-completed groups are skipped automatically.
- **Progress tracking**: A `.batch_progress.json` file in the output directory tracks completed groups and the current observation ID.
- **Incremental CSV**: Trueness and precision CSVs are appended to after each group (not rewritten from scratch).
- **Safe interruption**: Progress is saved before exiting on cancel, so no work is lost.
- **Clean completion**: The progress file is automatically deleted when all groups complete successfully.

Example output during resume:
```
Resuming from previous run. Already completed: 3 groups.
[1/7] Skipping SKD_18 (already completed)
[2/7] Skipping SKD_20 (already completed)
[3/7] Skipping SKD_22 (already completed)
[4/7] Processing group SKD_24...
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

## Current Status (as of 2026-06-03)

### Implemented
- Full CLI batch mode with JSON configuration
- File discovery with glob patterns and scanner ID matching
- Per-group GPA alignment and distance computation
- Trueness metrics (RMS, MAD, Max, P95, coverage rate)
- Precision metrics (pairwise RMS between repetitions)
- CSV output (metrics, precision, summary)
- Optimized `computePairwise()` for efficient precision computation
- **ROI filtering fully integrated** (bbox, Z-plane, brush zones, sigma clipping)
- **GUI mode with MainWindow** - tabbed interface for:
  - Study configuration loading and overview
  - ROI Template Editor with interactive 3D visualization
  - Batch processing with progress monitoring
  - Results file browser with CSV preview
- Full multi-scanner batch configuration (`data/full_study.json`)
- **Tooth segmentation** - Dijkstra-based crown region growing:
  - Interactive seed point placement on tooth cusps
  - Curvature-weighted geodesic expansion
  - Parameters: geodesic limit, crease angle, curvature thresholds
  - Combinable with other ROI filters (bbox, Z-plane, brush, sigma)
- **ROI template batch integration** (`--roi-template` CLI option):
  - Load ROI template with tooth segmentation seeds
  - Apply tooth masks to all scans in batch mode
  - Seeds snapped to nearest mesh vertices after GPA alignment

### Test Results (Full study: 6 scanners × 7 SKD levels)
- 185 scans total (5 repetitions per scanner per SKD)
- SKD 18: 5 iTeroLumina scans only
- SKD 20-30: 30 scans each (all 6 scanners)
- Trueness RMS: 0.032–0.074 mm (typical range)
- Precision Mean RMS: ~0.27 mm (pairwise comparisons)

### Not Yet Implemented
- YAML configuration support (JSON only currently working)
- Statistical output enhancement (R-ready format, effect sizes)

---

## Next Steps

1. **Statistical enhancements**: Add R-ready output format, compute effect sizes
2. **GUI refinements**: Progress signals from BatchRunner to GUI, better error handling
3. **Occlusal plane in batch mode**: Currently GUI-only; save/load plane definition in ROI template

---

## Data Locations

- **Scanner data**: `/KHKsData/usr2/daten/P2026-Kessler-ScanVergleich/IOS/`
- **Test configurations**: `data/test_single_group.json`, `data/default_study.json`
- **Source DentScanCompare**: `/home/kkunzelm/claude-code/DentScanCompare/`
- **Implementation plan**: `docs/IMPLEMENTATION-PROMPT.md`

---

## Changelog

### 2026-06-03 – ROI template batch integration + GUI improvements

**Batch Integration:**
- Added `--roi-template` / `-r` CLI option to specify ROI template JSON file
- GroupProcessor now accepts optional `ROITemplate` parameter
- Tooth segmentation seeds are snapped to nearest mesh vertices in each scan after GPA alignment
- Both trueness and precision metrics use the combined ROI + tooth mask

**GUI Improvements:**

1. **Occlusal Plane Picking (Z-Plane Slab)**
   - New "Pick Plane (3 pts)" button to define occlusal plane from 3 points
   - Cross product computes plane normal, ensured to point upward (+Z)
   - Status label shows "Plane: auto (max-Z)" or "Plane: defined at Z=X.X"
   - "Clear" button resets to automatic max-Z detection

2. **Bounding Box Visualization**
   - Orange wireframe box now displayed when "Active" checkbox is checked
   - Uses VTK `vtkOutlineFilter` for efficient edge rendering

3. **Brush Tool for Tooth Mask Editing**
   - New checkbox "Edit tooth mask (not ROI zones)"
   - When checked: brush clicks directly modify the tooth mask (add/remove vertices)
   - When unchecked: brush creates ROI zones (original behavior)
   - Status bar shows count of modified vertices

4. **Outlier Removal Clarification**
   - Updated tooltip on sigma threshold to explain:
     - Applied during METRIC COMPUTATION (not segmentation)
     - Removes vertices whose signed distance exceeds σ standard deviations
     - Set to 0 to disable

**Technical Changes:**
- VTKMeshWidget: Added `showBoundingBox()` and `hideBoundingBox()` methods
- CMakeLists.txt: Added `FiltersSources` and `FiltersModeling` VTK components
- MainWindow: New slots for occlusal plane picking and tooth mask brush editing

### 2026-06-03 – Incremental save and resume capability
- BatchRunner now saves results after each group completes (not just at the end)
- Progress tracked in `.batch_progress.json` file in output directory
- On restart, automatically resumes from where it left off
- Skips already-completed groups, continues with remaining groups
- Progress file automatically deleted on successful completion
- CSVWriter gains `appendTruenessCSV`, `appendPrecisionCSV`, `appendGroupResult` methods

### 2026-06-03 – Tooth segmentation integration
- Ported `ToothSegmentation.{h,cpp}` from DentScanCompare
- Added tooth segmentation UI to ROI Template Editor:
  - Seed point picking mode (place seeds on tooth cusps)
  - Segmentation parameters (geodesic limit, crease angle, curvature thresholds)
  - Run segmentation button computes tooth mask
  - "Use tooth mask as ROI" checkbox combines with other ROI filters
- ROI templates now save/load tooth segmentation seeds and parameters
- Visualization shows combined ROI mask (geometric + tooth segmentation)

### 2026-06-03 – GUI mode implementation
- Created `src/gui/MainWindow.{h,cpp}` with tabbed interface:
  - **Study Configuration tab**: Load JSON config, browse data root/output dirs, tree view of scanners/groups
  - **ROI Template Editor tab**: Load template scan, 3D VTK view, bounding box/Z-plane/brush controls, save/load ROI templates
  - **Batch Processing tab**: Run/cancel buttons, progress bars, processing log
  - **Results tab**: File browser, CSV preview
- Updated `main.cpp` to launch GUI mode by default, CLI mode with `--batch` flag
- ROI filtering already integrated in GroupProcessor (confirmed working)
- Created `data/full_study.json` for complete 6-scanner × 7-SKD evaluation

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
