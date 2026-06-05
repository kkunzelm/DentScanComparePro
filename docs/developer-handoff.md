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
│   ├── ToothSegmentation.{h,cpp}   Dijkstra-based crown segmentation from seed points
│   └── AlignmentTransformLoader.{h,cpp}  Load DentScanAlign JSON transforms
├── config/                     Configuration parsing
│   ├── ROIConfig.{h,cpp}       ROI structures + ROITemplate JSON I/O
│   ├── StudyConfig.{h,cpp}     JSON/YAML study configuration
│   └── FileDiscovery.{h,cpp}   Glob pattern file discovery
├── batch/                      Batch processing
│   ├── BatchRunner.{h,cpp}     Orchestrates all group processing
│   ├── GroupProcessor.{h,cpp}  Processes one SKD group
│   └── CSVWriter.{h,cpp}       Output CSV files
├── qc/                         Quality Control workflow
│   ├── QCExporter.{h,cpp}      Export GPA means, transforms, difference images, segmented meshes
│   ├── ErrandManager.{h,cpp}   Track accept/reject status, filter CSV output
│   ├── LandmarkRegistration.{h,cpp}  Kabsch algorithm + ICP refinement
│   ├── QCReviewWidget.{h,cpp}  Thumbnail grid for quick visual review
│   ├── QFlowLayout.{h,cpp}     Flow layout for thumbnail grid
│   ├── ErrandResolutionDialog.{h,cpp}  Interactive re-registration dialog
│   └── AlignmentQCDialog.{h,cpp}   Detailed alignment QC with reference overlay
├── gui/                        Interactive ROI template editor
│   └── MainWindow.{h,cpp}      Main window with tabs (config, ROI, batch, results, QC)
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
    --alignments alignments/ \
    --external-ref reference.stl \
    --pre-aligned \
    --verbose
```

**Options:**
- `--batch` / `-b`: Run in headless CLI mode (no GUI)
- `--study` / `-s`: Path to study configuration JSON file
- `--data-root` / `-d`: Root directory containing scanner folders
- `--output` / `-o`: Output directory for CSV files (default: ./results)
- `--roi-template` / `-r`: Optional ROI template with tooth segmentation settings
- `--alignments` / `-a`: Directory containing DentScanAlign JSON transform files
- `--external-ref` / `-e`: External reference STL (CAD or lab scanner)
- `--pre-aligned`: Skip GPA computation (scans already coarsely aligned)
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

## QC Workflow

The QC (Quality Control) workflow enables visual verification of registration results and interactive correction of failures.

### Batch QC Output

When batch processing completes, a `qc/` folder is created with:

```
results/qc/
├── gpa_means/                    # GPA reference meshes (one per SKD group)
│   ├── SKD_18_gpa_mean.stl
│   ├── SKD_20_gpa_mean.stl
│   └── ...
├── transforms/                   # Transform matrices + metrics per scan
│   ├── iTeroLumina_SKD_18_r1.json
│   ├── Primescan_SKD_20_r1.json
│   └── ...
├── segmented/                    # Tooth-only meshes (when tooth mask is used)
│   ├── iTeroLumina_SKD_18_r1.stl
│   ├── Primescan_SKD_20_r1.stl
│   └── ...
└── difference_images/            # (Currently disabled - see Known Issues)
    └── *.png
```

**Transform JSON format:**
```json
{
  "transform": [[r00,r01,r02,tx], [r10,r11,r12,ty], [r20,r21,r22,tz], [0,0,0,1]],
  "metrics": {
    "rms_mm": 0.045,
    "mad_mm": 0.032,
    "hausdorff95_mm": 0.12,
    "hausdorff100_mm": 0.28,
    "signed_mean_mm": -0.002,
    "coverage_pct": 98.5,
    "vertices_included": 45230,
    "vertices_total": 45890
  },
  "scanner": "Primescan",
  "group": "SKD_20",
  "repetition": 1,
  "file_path": "/path/to/scan.stl"
}
```

### QC Review Widget

The GUI includes a QC Review tab (Tab 5) with:
- Thumbnail grid of difference images (when available)
- Group filtering dropdown
- Click to view details, double-click to toggle accept/errand status
- Color-coded borders: green=accepted, red=errand, grey=pending
- Status counts: Errands / Accepted / Pending

### Errand Resolution Dialog

For flagged scans (errands), the ErrandResolutionDialog provides a **three-panel layout**:

```
┌──────────────────────┬──────────────────────┬────────────────────────┐
│  GPA REFERENCE       │  SCAN (aligning)     │  DIFFERENCE MAP        │
│  ┌────────────────┐  │  ┌────────────────┐  │  ┌────────────────┐    │
│  │   [3D mesh]    │  │  │   [3D mesh]    │  │  │   [color map]  │    │
│  │   • Pick pts   │  │  │   • Pick pts   │  │  │                │    │
│  └────────────────┘  │  └────────────────┘  │  └────────────────┘    │
│  Points: 3 picked    │  Points: 3 picked    │  RMS: 0.045 mm         │
├──────────────────────┴──────────────────────┴────────────────────────┤
│  [Compute Alignment]  [Run ICP]        [Accept Result]  [Reject/Skip]│
└──────────────────────────────────────────────────────────────────────┘
```

**Workflow:**
1. Pick 3+ corresponding landmarks on GPA Reference and Scan views
2. Click "Compute Alignment" - Kabsch algorithm computes initial rigid transform
3. Difference map auto-updates showing color-coded distances
4. Optional: Click "Run ICP" for fine refinement
5. Accept → re-add to CSV with corrected metrics; Reject → keep excluded

**Features:**
- Landmark spheres cleared after alignment (don't float in wrong position)
- Pick mode disabled after alignment (no accidental clicks)
- Difference map auto-updates after alignment and ICP
- Settings persisted via QSettings (survives crashes)

### LandmarkRegistration Algorithm

```cpp
// 1. Compute centroids
Eigen::Vector3d srcCentroid = mean(sourcePoints);
Eigen::Vector3d tgtCentroid = mean(targetPoints);

// 2. Center the points
MatrixXd P = sourcePoints - srcCentroid;  // 3xN
MatrixXd Q = targetPoints - tgtCentroid;  // 3xN

// 3. Cross-covariance matrix
Matrix3d H = P * Q.transpose();

// 4. SVD for optimal rotation
JacobiSVD<Matrix3d> svd(H, ComputeFullU | ComputeFullV);
Matrix3d R = svd.matrixV() * svd.matrixU().transpose();

// 5. Handle reflection
if (R.determinant() < 0) {
    Matrix3d V = svd.matrixV();
    V.col(2) *= -1;
    R = V * svd.matrixU().transpose();
}

// 6. Translation
Vector3d t = tgtCentroid - R * srcCentroid;
```

### Known Issue: Difference Image Export

VTK offscreen rendering crashes in headless batch mode due to GLEW/OpenGL initialization failure. The error is:
```
vtkGenericOpenGLRenderWindow: GLEW could not be initialized: Missing GL version
```

**Current workaround:** Image export is disabled in batch mode. GPA meshes (STL) and transforms (JSON) are still exported. Difference images can be generated in GUI mode.

**Potential solutions for future:**
1. Build VTK with OSMesa support for software rendering
2. Use EGL backend for headless GPU rendering
3. Generate images in a separate GUI post-processing step

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

### VTK Widget Cleanup on Dialog Destruction

When a QDialog containing VTKMeshWidget is destroyed, VTK may crash if cleanup isn't done properly. Key learnings:

**DO NOT:**
- Call `clearMesh()` in dialog destructor - triggers Render() during destruction
- Call `setRenderWindow(nullptr)` in VTKMeshWidget destructor - ambiguous overload issues
- Immediately refresh other widgets after dialog closes - memory may still be corrupt

**DO:**
- Let Qt handle natural widget destruction order
- Clear smart pointer references in dialog destructor (just `.reset()` them)
- Defer any operations that allocate memory (like QPixmap loading) using QTimer::singleShot
- Call `QApplication::processEvents()` multiple times after dialog closes

```cpp
// In dialog destructor - minimal cleanup:
ErrandResolutionDialog::~ErrandResolutionDialog()
{
    // Just release mesh data references, don't call VTK methods
    m_scan.reset();
    m_scanBackup.reset();
    m_gpaRef.reset();
    m_refData.reset();
}

// In MainWindow after dialog closes:
QApplication::processEvents();
QTimer::singleShot(100, this, [this]() {
    m_qcReviewWidget->refresh();  // Deferred to allow VTK cleanup
});
```

**Note:** This is still an active issue. The crash may occur due to OpenGL context sharing or VTK internal state corruption.

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

## Current Status (as of 2026-06-04)

### Active Issues Under Investigation

#### 1. Coordinate System Mismatch (90-Degree Rotation)

Different scanners use different coordinate systems:
- **Some scanners**: Y-axis is "up" (occlusal direction)
- **GPA reference**: Z-axis is "up" (occlusal direction)

**Observed in landmark pairs:**
```
Scan Y values: ~5-6 (constant) → Y is vertical
Ref  Z values: ~5.6-6.2 (constant) → Z is vertical
```

The Kabsch algorithm computes a transform that includes the ~90° X-axis rotation, but visual feedback suggests alignment may be off. Debug output now prints the full 4×4 transform matrix to verify:
```
=== Kabsch Transform Matrix ===
[  R00,   R01,   R02,   tx]
[  R10,   R11,   R12,   ty]
[  R20,   R21,   R22,   tz]
[  0.0,   0.0,   0.0,  1.0]
```

**Expected rotation (Y-up → Z-up, +90° around X):**
```
R = [1   0    0 ]
    [0   0   -1 ]
    [0   1    0 ]
```

#### 2. VTK Cleanup Crash on Dialog Close

**Symptom:** Segfault or std::bad_alloc when closing ErrandResolutionDialog

**Root cause:** VTK Render() calls during widget destruction corrupt memory, affecting subsequent allocations (e.g., QPixmap in QCReviewWidget::refresh()).

**Current fix (partial):**
1. Removed `clearMesh()` calls from dialog destructor
2. Simplified VTKMeshWidget destructor cleanup order
3. Added `QTimer::singleShot(100ms)` to defer QC refresh after dialog closes
4. Multiple `QApplication::processEvents()` calls to flush pending operations

**Still under investigation** - crash may still occur.

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
  - **QC Review tab** (new - see QC Workflow section)
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
- **QC Workflow** (functional - see details below):
  - GPA mean mesh export (STL) per group
  - Transform + metrics export (JSON) per scan
  - Segmented mesh export (tooth-only STL) when tooth masks are used
  - ErrandManager for accept/reject tracking
  - QCReviewWidget thumbnail grid UI with group filtering
  - LandmarkRegistration with Kabsch algorithm
  - ErrandResolutionDialog with three-panel layout (ref/scan/diff)
  - AlignmentQCDialog for detailed overlay view (reference wireframe + colored scan)
  - Difference images generated in GUI mode
  - Settings persistence via QSettings (paths survive crashes)
- **DentScanAlign Integration**:
  - Load pre-computed transforms from DentScanAlign JSON files (`--alignments` option)
  - Apply transforms as initialization before ICP refinement
  - Masked ICP: use tooth segmentation mask to focus alignment on tooth surfaces

### Test Results (Full study: 6 scanners × 7 SKD levels)
- 185 scans total (5 repetitions per scanner per SKD)
- SKD 18: 5 iTeroLumina scans only
- SKD 20-30: 30 scans each (all 6 scanners)
- Trueness RMS: 0.032–0.074 mm (typical range)
- Precision Mean RMS: ~0.27 mm (pairwise comparisons)

### Not Yet Implemented / Known Issues
- **Difference image export**: Disabled in batch mode due to VTK headless rendering crash (GLEW/OpenGL initialization fails without display). GPA meshes and transforms still export.
- YAML configuration support (JSON only currently working)
- Statistical output enhancement (R-ready format, effect sizes)
- QC Review UI needs testing with real QC data

---

## Next Steps

1. **Fix VTK headless rendering**: Enable difference image export in batch mode
   - Option A: Rebuild VTK with OSMesa support
   - Option B: Use EGL backend for headless GPU rendering
   - Option C: Generate images in GUI post-processing step
2. **Test QC Review workflow**: Verify thumbnail loading, errand flagging, CSV filtering
3. **Test ErrandResolutionDialog**: Verify landmark picking, Kabsch alignment, ICP refinement
4. **Statistical enhancements**: Add R-ready output format, compute effect sizes
5. **Occlusal plane in batch mode**: Currently GUI-only; save/load plane definition in ROI template

---

## Proposed Architecture: Multi-Reference, Multi-Pass Evaluation

*Analysis date: 2026-06-03*

### Research Context

The project compares different intraoral scanners across clinical situations (SKD levels). Key insights:

1. **Multiple reference standards available:**
   - GPA Mean (computed consensus from all scans)
   - CAD STL (ground truth design intent)
   - Laboratory scanner (high-accuracy physical measurement)

2. **Two-stage evaluation needed:**
   - **Full mesh**: Teeth + gums (higher RMS due to gum deformation)
   - **Teeth only**: Rigid structures, expect lower RMS

3. **QC is mandatory early**: Before statistics, must confirm registration found global minimum

4. **Transform reuse**: Apply successful full-mesh registration as starting point for masked evaluation

### Abstract Workflow Model

```
┌─────────────────────────────────────────────────────────────────────┐
│  PHASE 1: REGISTRATION + QC (Mandatory Checkpoint)                  │
│  ═══════════════════════════════════════════════════                │
│                                                                     │
│  Input: Raw STL scans (variable orientation)                        │
│  Output: Validated transforms (4×4 matrices)                        │
│                                                                     │
│  ┌──────────┐    ┌──────────┐    ┌──────────┐    ┌──────────┐      │
│  │  Coarse  │───▶│   Fine   │───▶│  Visual  │───▶│ Approved │      │
│  │ Alignment│    │   ICP    │    │    QC    │    │Transforms│      │
│  └──────────┘    └──────────┘    └────┬─────┘    └──────────┘      │
│                                       │                             │
│                                       ▼ (if failed)                 │
│                                  ┌──────────┐                       │
│                                  │ Landmark │                       │
│                                  │Pre-align │───▶ Re-run ICP        │
│                                  └──────────┘                       │
│                                                                     │
│  Reference options: GPA Mean | CAD STL | Lab Scanner                │
└─────────────────────────────────────────────────────────────────────┘
                                    │
                                    ▼ (transforms only, QC approved)
┌─────────────────────────────────────────────────────────────────────┐
│  PHASE 2: METRIC EVALUATION (Multiple Passes)                       │
│  ════════════════════════════════════════════                       │
│                                                                     │
│  Input: Approved transforms + Original STLs + Reference(s)          │
│  Output: Metrics CSVs per evaluation pass                           │
│                                                                     │
│  ┌─────────────────────────────────────────────────────────────┐    │
│  │  PASS: "full"                                               │    │
│  │  • Apply saved transform to original STL                    │    │
│  │  • Compute distances to reference (full mesh)               │    │
│  │  • Output: trueness_full.csv, precision_full.csv            │    │
│  └─────────────────────────────────────────────────────────────┘    │
│                                                                     │
│  ┌─────────────────────────────────────────────────────────────┐    │
│  │  PASS: "teeth_only"                                         │    │
│  │  • Apply saved transform (from "full" pass)                 │    │
│  │  • Apply segmentation mask (teeth template)                 │    │
│  │  • Optional: Refine registration on masked region           │    │
│  │  • Compute distances (masked vertices only)                 │    │
│  │  • Output: trueness_teeth.csv, precision_teeth.csv          │    │
│  └─────────────────────────────────────────────────────────────┘    │
│                                                                     │
│  Additional passes possible: ROI regions, individual teeth, etc.    │
└─────────────────────────────────────────────────────────────────────┘
```

### Key Abstractions

#### 1. Reference Source (Pluggable)

```
ReferenceType:
  - GPA_MEAN      → Computed from scan group (current implementation)
  - EXTERNAL_STL  → CAD file or lab scanner per group
  - MULTI_TARGET  → Compare against multiple references simultaneously
```

#### 2. Evaluation Pass (Repeatable)

```
EvaluationPass:
  - name: "full" | "teeth_only" | "roi_region" | ...
  - mask: none | template_path
  - reference: which reference mesh(es) to compare against
  - refine_registration: bool (fine-tune ICP on masked region)
  - inherit_transform_from: null | "pass_name"
```

#### 3. Transform as First-Class Output

```
Transform persistence:
  - Saved after registration (JSON with 4×4 matrix)
  - Can be applied to any STL (load → transform → evaluate)
  - Can be refined (masked ICP) and saved as new version
  - Enables: full → masked workflow without full re-registration
```

#### 4. QC as Mandatory Gate

```
Pipeline stages:
  1. Registration → produces transforms (NOT metrics)
  2. QC Review → approve/reject/fix each transform
  3. Metrics → only computed on QC-approved transforms
```

### Proposed Configuration Structure

```yaml
study:
  name: "Scanner Comparison Study"

references:
  gpa_mean: { type: computed }
  cad: { type: external, path_pattern: "{group}_cad.stl" }
  lab_scanner: { type: external, path_pattern: "{group}_lab.stl" }

registration:
  primary_reference: gpa_mean    # or cad, lab_scanner
  coarse_method: pca             # or landmarks
  fine_method: icp
  qc_required: true              # Mandatory checkpoint before metrics

evaluation_passes:
  - name: full
    mask: none
    reference: [gpa_mean, cad, lab_scanner]  # Compare to all three

  - name: teeth_only
    mask: teeth_template.json
    inherit_transform: full      # Reuse alignment from full pass
    refine_registration: true    # Fine-tune on teeth only
    reference: [gpa_mean, cad, lab_scanner]
```

### Implementation Comparison

| Current Implementation | Proposed Architecture |
|------------------------|----------------------|
| Registration + metrics in one batch pass | Registration separate from metrics |
| GPA mean hardcoded as reference | Pluggable reference source |
| Single evaluation (full mesh) | Multiple evaluation passes |
| QC optional/post-hoc | QC mandatory gate before metrics |
| Transforms saved but underutilized | Transforms are primary output, reusable |

### Implementation Priority

1. **Immediate**: Complete QC workflow for visual verification of registrations
2. **Next**: Add external reference support (CAD/lab scanner as alternative to GPA)
3. **Then**: Implement multi-pass evaluation (full → masked with transform reuse)
4. **Finally**: Transform refinement for masked regions

### Benefits of This Architecture

- **Flexibility**: Same scan data, multiple evaluation configurations
- **Reproducibility**: Saved transforms enable exact replication
- **Efficiency**: Transform reuse avoids redundant registration
- **Quality**: Mandatory QC catches registration failures early
- **Comparability**: Multiple references answer different research questions:
  - CAD → How accurately do scanners reproduce designed geometry?
  - Lab scanner → How do IOS compare to high-accuracy bench scanner?
  - GPA mean → Which scans deviate from consensus? (outlier detection)

---

## Data Locations

- **Scanner data**: `/KHKsData/usr2/daten/P2026-Kessler-ScanVergleich/IOS/`
- **Test configurations**: `data/test_single_group.json`, `data/default_study.json`
- **Source DentScanCompare**: `/home/kkunzelm/claude-code/DentScanCompare/`
- **Implementation plan**: `docs/IMPLEMENTATION-PROMPT.md`

---

## Changelog

### 2026-06-05 – Full-Mesh Mode Fix and Performance Optimizations

**Problem:** Registration quality degraded for ~50% of scans when using pre-aligned STL files from DentScanAlign. Root causes identified:
1. ROI/masked ICP was being triggered even when disabled in GUI (Z-plane defaulted to active)
2. Precision metrics computation hung due to O(N²) AABB tree constructions
3. Difference image generation was slow due to repeated tree building

**Fix 1: Z-Plane Default Changed to Inactive**
- `ROIConfig.h`: `ZPlaneSlab.active` default changed from `true` to `false`
- `MainWindow.cpp`: Z-plane checkbox now unchecked by default
- `ROIConfig.cpp`, `StudyConfig.cpp`: JSON/YAML loading defaults changed to `false`
- **Impact**: Users must now explicitly enable Z-plane ROI restriction

**Fix 2: Explicit `forceFullMesh` Flag**
- Added `forceFullMesh` parameter to `BatchRunner::run()` and `GroupProcessor::process()`
- When GUI checkbox "Use ROI mask for registration" is unchecked, `forceFullMesh=true`
- This bypasses ALL ROI logic regardless of config file settings:
  - Skips tooth segmentation computation
  - Uses full-mesh ICP (not masked)
  - Uses empty ROI for metrics computation
- Added debug logging showing ROI state and whether it's being ignored

**Fix 3: AABB Tree Caching for Precision Metrics**
- Added `ReferenceTree::computePairwiseDistances()` method to `DistanceField.{h,cpp}`
- `GroupProcessor::computePrecisionMetrics()` now pre-builds AABB trees for all scans in each scanner group
- Trees are reused across pairwise comparisons instead of rebuilt each time
- **Performance**: For 5 scans per scanner, reduces tree builds from 10 to 5 (O(N) instead of O(N²))

**Fix 4: AABB Tree Caching for Difference Image Generation**
- `MainWindow::generateDifferenceImages()` now caches `ReferenceTree` per group
- Previously built a new AABB tree for every scan (expensive for 180+ scans)
- Now builds one tree per reference mesh (typically 6-7 for a full study)

**Files Modified:**
- `src/config/ROIConfig.h` - ZPlaneSlab.active default = false
- `src/config/ROIConfig.cpp` - JSON loading default = false
- `src/config/StudyConfig.cpp` - JSON/YAML defaults = false, createDefault() = false
- `src/batch/BatchRunner.{h,cpp}` - Added forceFullMesh parameter
- `src/batch/GroupProcessor.{h,cpp}` - Added forceFullMesh, cached AABB trees for precision
- `src/core/DistanceField.{h,cpp}` - Added computePairwiseDistances() to ReferenceTree
- `src/gui/MainWindow.cpp` - Pass forceFullMesh, cache trees for difference images

**New Console Output:**
```
Full-mesh mode: ACTIVE (ignoring ROI settings from config)
Running FULL-MESH ICP refinement against reference...
Building AABB trees for <scanner> (N scans)... done
Computing pairwise distances...... done (M pairs)
```

**Note:** Existing study config files may still have `z_plane: { active: true }` saved. This is now ignored when `forceFullMesh=true` (checkbox unchecked). To permanently fix, regenerate the config or manually edit to set `active: false`.

### 2026-06-04 – Masked ICP, DentScanAlign Integration, and Enhanced QC

**Feature 1: Load DentScanAlign JSON Transforms**
- New `AlignmentTransformLoader.{h,cpp}` module parses JSON files from alignments/ directory
- Extracts `transform_4x4` (16-element row-major array) → Eigen::Matrix4d
- Returns map: normalized source_file path → transform
- Added `alignmentsDirectory` field to StudyConfig
- Added `-a, --alignments <directory>` CLI option

**Feature 2: Masked ICP Using Tooth Segmentation**
- Reordered GroupProcessor pipeline: tooth masks now computed BEFORE alignment
- Uses `ICPRegistration::alignMasked()` when tooth masks are available
- Focuses ICP alignment on tooth surfaces only, excluding gingiva
- Falls back to full-mesh ICP if mask is unavailable or too small

**Feature 3: Enhanced QC Visualization**
- New `AlignmentQCDialog.{h,cpp}` shows reference mesh + aligned scan overlay
- Reference displayed as grey wireframe, scan as distance-colored solid surface
- Metrics summary (RMS, Max, Coverage) with color-coding
- Accept/Flag/Skip buttons for quick QC decisions
- Double-click on QCReviewWidget thumbnail opens AlignmentQCDialog
- Added `showAlignmentOverlay()` and `hideReferenceOverlay()` to VTKMeshWidget

**Feature 4: Segmented Mesh Export**
- New `QCExporter::extractSubmesh()` creates mesh from vertex mask
- New `QCExporter::exportSegmentedMesh()` exports tooth-only STL
- Segmented meshes exported to `qc/segmented/` directory
- GroupProcessor now exports segmented meshes when tooth masks are available

**Files Added:**
- `src/core/AlignmentTransformLoader.{h,cpp}`
- `src/qc/AlignmentQCDialog.{h,cpp}`

**Files Modified:**
- `src/config/StudyConfig.{h,cpp}` - Added alignmentsDirectory field
- `src/main.cpp` - Added --alignments CLI option
- `src/batch/GroupProcessor.{h,cpp}` - Reordered pipeline, masked ICP, precomputed transforms
- `src/batch/BatchRunner.cpp` - Load and pass precomputed transforms
- `src/visualization/VTKMeshWidget.{h,cpp}` - Added alignment overlay methods
- `src/qc/QCExporter.{h,cpp}` - Added submesh extraction and segmented mesh export
- `src/qc/QCReviewWidget.cpp` - Double-click emits viewRequested signal
- `src/gui/MainWindow.cpp` - Wire up AlignmentQCDialog
- `src/CMakeLists.txt` - Added new source files

### 2026-06-03 – Landmark Registration Debugging (Part 5)

**Coordinate System Investigation:**
- Identified Y-up vs Z-up coordinate system mismatch between scanners and GPA reference
- Added transform matrix debug output to verify Kabsch rotation computation
- Prints full 4×4 matrix after each landmark alignment

**VTK Cleanup Crash Fix (Iteration 3):**
- Removed `clearMesh()` calls from ErrandResolutionDialog destructor
- Simplified VTKMeshWidget destructor (removed `setRenderWindow(nullptr)` call)
- Deferred QCReviewWidget::refresh() by 100ms using QTimer::singleShot
- Added multiple processEvents() calls after dialog closes

**Files Modified:**
- `src/qc/ErrandResolutionDialog.cpp` - Added transform matrix debug output, simplified destructor
- `src/visualization/VTKMeshWidget.cpp` - Simplified destructor cleanup order
- `src/gui/MainWindow.cpp` - Added QTimer include, deferred refresh with lambda capture

### 2026-06-03 – ErrandResolutionDialog Improvements

**Three-Panel Layout:**
- Redesigned dialog with three views: GPA Reference (left), Scan (middle), Difference Map (right)
- Wider minimum size (1400x700) to accommodate all three panels
- Difference map auto-updates after alignment and ICP refinement

**Landmark Registration UX:**
- Landmark spheres now cleared after alignment (previously floated in wrong position)
- Pick mode disabled after alignment to prevent accidental picks
- Removed "Show Difference Map" button - now automatic

**Crash Fix - VTK Widget Cleanup:**
- Added `clearMesh()` method to VTKMeshWidget for safe cleanup
- Dialog destructor now clears VTK widgets before Qt destroys them
- Prevents segmentation faults on dialog close

**Settings Persistence:**
- Path fields (study, data root, output dir, template) now save immediately on change
- Uses QSettings with QSignalBlocker to avoid save-during-load
- Paths persist even if app crashes

**Technical Details:**
- Added `m_diffView` and `m_diffLabel` members to ErrandResolutionDialog
- Added `updateDifferenceView()` private method
- VTKMeshWidget::clearMesh() clears polydata, hides actors, flushes render

### 2026-06-03 – QC Workflow Implementation

**New QC Module (`src/qc/`):**
- `QCExporter.{h,cpp}` - Export GPA mean meshes (STL), transform matrices (JSON), and difference images (PNG, currently disabled)
- `ErrandManager.{h,cpp}` - Track accept/reject status for all scans, filter CSV output
- `LandmarkRegistration.{h,cpp}` - Kabsch algorithm for corresponding point registration + ICP refinement
- `QCReviewWidget.{h,cpp}` - Thumbnail grid for quick visual review with accept/flag workflow
- `QFlowLayout.{h,cpp}` - Flow layout widget for thumbnail arrangement
- `ErrandResolutionDialog.{h,cpp}` - Interactive re-registration dialog with side-by-side mesh views

**Batch Processing Changes:**
- GroupProcessor now exports QC data (GPA mean + transforms) after processing each group
- QCExporter::setImageExportEnabled(false) called in batch mode to avoid VTK crash
- QC directory structure created: `qc/gpa_means/`, `qc/transforms/`, `qc/difference_images/`

**GUI Changes:**
- Added QC Review tab (Tab 5) to MainWindow
- QCReviewWidget integrated with thumbnail grid and status tracking
- ErrandResolutionDialog accessible for flagged scans

**Known Issue:**
- Difference image export disabled in batch mode due to VTK GLEW initialization failure in headless environment
- GPA meshes and transforms still export successfully

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
