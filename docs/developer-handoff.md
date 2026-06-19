# DentScanComparePro – Developer Handoff

## Goal

Automated batch evaluation of dental intraoral scanner accuracy. Computes ISO 5725/12836-compliant trueness and precision metrics across multiple scanners and study designs. Groups are generic string IDs — SKD levels for phantom studies, patient IDs for clinical cohort studies, or any other label.

Based on core algorithms from DentScanCompare (`/home/kkunzelm/claude-code/DentScanCompare/`), extended with:
- JSON-driven batch configuration with generic group IDs
- Automated file discovery via glob patterns with scanner ID matching
- Per-group GPA alignment with ICP fine-registration and convergence monitoring
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
│   ├── ICPRegistration.{h,cpp}     Point-to-plane ICP (nanoflann + Eigen); hierarchy variants
│   ├── MeshDecimation.{h,cpp}      Curvature-weighted QEM decimation (Xi-2025)
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
│   ├── GroupProcessor.{h,cpp}  Processes one condition group
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

**Conditional execution**: curvature (and tessellation metrics) are skipped when `scansPreAligned = true` and no ROI tooth mask is configured (`needCurvature` flag in `GroupProcessor::process()`). When scans come from DentScanAlignPro, canonical orientation is already established by an 11-term scorer — recomputing curvature for Z-sign resolution is redundant. Tooth mask segmentation still requires curvature when active.

### ICP residual vs trueness RMS

`ICPRegistration::Result::finalRms` is the RMS of the **point-to-plane** distances `|n · (sp − qp)|` across all kept correspondences at the last ICP iteration. It is the convergence metric for the alignment solve — not a trueness metric. In the batch progress output it appears as `res=` (per scan) and `max_res=` (max across scans per GPA cycle).

`RMS_mm` in the CSV is computed by `DistanceField::fillReport()` after alignment completes: the RMS of the 3D Euclidean distances from each scan vertex to its nearest point on the GPA mean reference mesh. This is the ISO 12836 trueness metric.

They differ in both definition and magnitude. A scan with `res=0.05 mm` (tight ICP convergence) may still have `RMS_mm=0.25 mm` (genuine trueness error) because ICP minimises point-to-plane residuals, not Euclidean distances, and the correspondence set is filtered by `maxCorrespDist`.

### 3. GPAReference::compute (standard path)
Three-stage alignment:
- **Stage 1 – PCA coarse alignment**: Translate to centroid, rotate largest-variance axis → X, smallest → Z. Z-sign resolved via curvature (occlusal = high curvature → +Z). X-sign forced to align with canonical +X to avoid 180° inter-group frame inconsistency.
- **Stage 2 – 4-orientation Z-rotation test**: Tries 0°/90°/180°/270° Z rotations, picks best via ICP score.
- **Stage 3 – GPA iterations**: Coarse ICP (15 mm, 30 iter), then fine ICP (5 mm, 100 iter). Converges when max displacement < 0.01 mm. When `Params::scanMasks[si]` is non-empty for a scan, `alignMasked()` is used instead of `align()` for both passes — the solve focuses only on the masked vertices (tooth crowns, plane slab, etc.).
- **Stage 4 – Mean mesh update**: Reference vertices moved to mean of closest points on all aligned scans.

### 3b. Pre-aligned ICP path (`--pre-aligned`, no external reference)
Replaces the full GPA when `scansPreAligned = true` and no external reference is provided:
1. Pick scan with most triangles as initial reference.
2. Run one ICP pass per scan against that reference (`maxCorrespDist = 5 mm`).
3. Call `GPAReference::updateMeanMesh()` to compute true mean surface.

Pre-alignment (e.g. DentScanAlignPro) provides a coarse canonical orientation; ICP resolves the residual inter-scanner offsets before the mean mesh is computed. Skipping ICP and computing the mean directly would produce a biased, smeared reference.

### 3c. TrICP — Trimmed ICP for Soft-Tissue Rejection

Patient intraoral scans cover large non-rigid regions (gingiva, buccal mucosa, soft palate) in addition to the teeth. These regions deform between scan repetitions, so their ICP correspondences have large point-to-plane residuals that pull the rigid solve away from the correct tooth alignment.

**TrICP** adds a sort-and-trim step after the standard distance gate, inside every ICP iteration:

1. Collect all correspondences passing `maxCorrespDist`
2. Sort by point-to-plane residual `|n · (sp − qp)|` (ascending)
3. Keep only the best `trimFraction` fraction (minimum 6 correspondences)
4. Build A, b and solve on the trimmed set only

Teeth are rigid → small residuals → survive the trim.
Soft tissue deforms → large residuals → discarded.

**Parameter:** `ICPRegistration::Params::trimFraction` (default `1.0` = no trimming).
Propagated via `AlignmentConfig::icpTrimFraction` → `StudyConfig` JSON field `study.alignment.icp_trim_fraction` → CLI flag `--trim-fraction` → GUI spinbox "ICP trim fraction".

Recommended values:
- `1.0` — phantom studies, no soft-tissue problem
- `0.7` — mild soft-tissue coverage
- `0.5` — extensive soft tissue (patient scans with palate + buccal mucosa)

Applies to all three ICP call sites in `GroupProcessor`: GPA iterations, pre-aligned mean-mesh ICP, and external-reference ICP.

### 3d. ICP Resolution Hierarchy (Xi-2025)

For scans with large initial offsets (e.g. different scanner coordinate frames, or large phantom deformations) standard ICP can converge to a local minimum because the basin of attraction shrinks with mesh complexity. The hierarchy runs ICP on progressively finer decimations so coarse levels escape bad local minima before the full-resolution solve.

**Algorithm:**
1. Copy `source` into `workCopy`.
2. For each level `(fraction, maxIter)` in `[(0.05, 30), (0.20, 30), (1.0, params.maxIterations)]`:
   - If `fraction < 1.0`: decimate `workCopy` to that fraction using `MeshDecimation::decimate()`.
   - Run `ICPRegistration::align()` on the decimated copy (or full copy at the last level).
   - Apply the resulting delta transform to `workCopy` (updates vertex positions; vertex indices unchanged).
   - Accumulate the cumulative transform `cumT = deltaT × cumT`.
3. Return `cumT` as the final transform.

**Masked variant (`alignMaskedHierarchical`):** Coarse levels use the full-mesh (no mask) decimated copy because vertex masks can't transfer to decimated meshes. The final full-resolution level uses `alignMasked()` on `workCopy`, whose vertex indices are stable across all prior `applyTransform()` calls.

**GPA does not use the hierarchy** — GPA scans arrive from PCA coarse alignment and are already roughly aligned; running hierarchy ICP in every GPA iteration would add ~3× overhead for negligible gain. The hierarchy applies only to GroupProcessor's final ICP refinement passes.

**`MeshDecimation::decimate()` interface:**
```cpp
// src/core/MeshDecimation.h
namespace MeshDecimation {
std::shared_ptr<ScanData> decimate(const ScanData& source,
                                    double targetFaceFraction,
                                    double negCurvK = 10.0);
} // namespace MeshDecimation
```

**Curvature-weighted QEM (Xi-2025 Algorithm 1):**
`CurvWeightedQEMCost<TM,GT>` subclasses `CGAL::SMS::GarlandHeckbert_triangle_policies<TM,GT>` and overrides the 2-argument cost `operator()`:

```
cost = GH_quadric_cost(v0, v1) × k
  where k = 10.0  if  (κ_H(v0) + κ_H(v1))/2 < 0   (concave: CEJ, grooves, gingival crevice)
            1.0   otherwise                            (convex: cusps, ridges)
```

The 1-argument placement `operator()` delegates to the base GH optimal point unchanged. `update_after_collapse()` averages the curvature coefficients of the two collapsed endpoints for the survivor. The curvature coefficients are read from the `"v:mean_curv"` vertex property (CGAL 6.0 `std::optional` API).

Effect: concave-region edges cost 10× more to collapse, so the CEJ, developmental grooves, and gingival crevice boundaries are preserved at coarse levels — 10–15% more boundary triangles vs. standard QEM at equivalent face counts.

**Configuration:**
- `ICPRegistration::Params::useHierarchy` — enable/disable
- `ICPRegistration::Params::hierarchyLevels` — vector of face fractions (default `{0.05, 0.20, 1.0}`)
- `ICPRegistration::Params::negCurvK` — cost multiplier for concave edges (default `10.0`)
- Propagated via `AlignmentConfig::useIcpHierarchy` / `icpHierarchyLevels` / `icpHierarchyNegCurvK`
- JSON fields: `study.alignment.use_icp_hierarchy`, `icp_hierarchy_levels`, `icp_hierarchy_neg_curv_k`
- CLI flag: `--icp-hierarchy`
- GUI: **"Use ICP resolution hierarchy (Xi-2025)"** checkbox on Study Configuration tab

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
    "description": "6 scanners × 5 SKD levels",
    "alignment": {
      "icp_trim_fraction": 1.0,
      "use_icp_hierarchy": false,
      "icp_hierarchy_levels": [0.05, 0.20, 1.0],
      "icp_hierarchy_neg_curv_k": 10.0
    }
  },
  "scanners": [
    {"id": "Primescan", "patterns": ["*Primescan*", "*PS*"]},
    {"id": "Trios5", "patterns": ["*Trios*", "*T5*"]}
  ],
  "groups": [
    {
      "id": "SKD_20",
      "condition_value": 20,
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
- `--pre-aligned`: Skip GPA; run one ICP pass per scan against the scan with most triangles, then compute mean mesh. Curvature and tessellation metrics are also skipped unless a ROI tooth mask is active.
- `--normalized`: Scans are normalized (transform already baked into geometry); suppress JSON transform loading even if `alignments_directory` is set in the config
- `--trim-fraction <f>`: TrICP outlier rejection: keep only this fraction of ICP correspondences per iteration, sorted by point-to-plane residual (1.0 = no trimming; 0.5 = keep best 50%). Overrides `icp_trim_fraction` in study config.
- `--icp-hierarchy`: Enable coarse-to-fine ICP hierarchy (Xi-2025): decimates source at 5%/20%/100% of faces using curvature-weighted QEM, seeds each level with the previous transform. Overrides `use_icp_hierarchy` in study config.
- `--verbose`: Print detailed progress information

### Incremental Save & Resume

The batch processor saves results incrementally after each condition group completes:

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
(Group labels come from the user-defined `id` field in the study config.)

### Output Files

**trueness_metrics.csv** – One row per scan (23 columns):

```
Observation_ID, Scanner_Model, Group_ID, Repetition_ID,
Triangles, Edge_mm, AspRatio, MaxAspRatio, ATI, DensHighK, DensLowK,
RMS_mm, MAD_mm, H100_mm, H95_mm, Bias_mm,
Coverage_pct, Boundary_mm, Holes, Stitch_deg,
Vertices_Included, Vertices_Total, File_Path
```

Tessellation columns (Triangles–DensLowK) are always populated. `Group_ID` is the free-form string from `group.id` in the study config — it is the patient ID for cohort studies and the SKD label for phantom studies.

**precision_metrics.csv** – One row per scanner per group:
```
Scanner_Model, Group_ID, Precision_MeanRMS_mm, Precision_SD_mm,
Coefficient_of_Variation, Pairwise_Count
```

**summary_stats.csv** – Aggregated trueness statistics per scanner per group:
```
Scanner_Model, Group_ID, N, Mean_RMS_mm, SD_RMS_mm, Min_RMS_mm, Max_RMS_mm
```

---

## QC Workflow

The QC (Quality Control) workflow enables visual verification of registration results and interactive correction of failures.

### Batch QC Output

When batch processing completes, a `qc/` folder is created with:

```
results/qc/
├── reference_meshes/             # Reference meshes (one per group)
│   ├── 002_reference.stl         # full GPA mean — patient study
│   ├── SKD_20_reference.stl      # full GPA mean — phantom study
│   ├── SKD_20_roi.stl            # ROI-trimmed submesh (only when geometric ROI active)
│   └── ...
├── aligned_meshes/               # Each scan geometry after ICP, in GPA frame
│   ├── Primescan_002_r1.stl
│   └── ...
├── difference_meshes/            # Aligned geometry as PLY with per-vertex distance scalar
│   ├── Primescan_002_r1.ply      # open in MeshLab (Render→Color→Per-Vertex Quality)
│   └── ...
├── transforms/                   # Transform matrices + metrics per scan
│   ├── Primescan_002_r1.json
│   └── ...
├── segmented/                    # Tooth-only meshes (when tooth mask is used)
│   └── ...
└── difference_images/            # PNG images (generated in QC tab, not during batch)
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
  "group": "002",
  "condition_value": 0,
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

## Changelog

### 2026-06-19 — Generic condition group naming (skd_mm → conditionValue)

**Motivation:** The internal field name `skd_mm` and GUI label "Groups (SKD Levels)" were study-specific and assumed a phantom depth-of-cavity design. Patient cohort studies (`id: "002"`) had to carry `"skd_mm": 0` as dead weight.

**Changes:**

- `GroupConfig::skd_mm` → `conditionValue` (`StudyConfig.h`)
- `GroupDiscovery::skd_mm` → `conditionValue` (`FileDiscovery.h`)
- `BatchMetricReport::skd_mm` → `conditionValue` (`GroupProcessor.h`)
- `PrecisionReport::skd_mm` → `conditionValue` (`GroupProcessor.h`)
- `GroupResult::skd_mm` → `conditionValue` (`GroupProcessor.h`)
- YAML/JSON serialization key: `skd_mm` → `condition_value`. Old key accepted as fallback for backward compatibility (`StudyConfig.cpp`).
- Transform JSON: `"skd_mm"` key → `"condition_value"` (`QCExporter.cpp`).
- Default summary CSV filename: `summary_by_scanner_skd.csv` → `summary_by_scanner_group.csv` (both headers and `StudyConfig.h` default).
- GUI: "Groups (SKD Levels)" → "Groups"; numeric value displayed only when `conditionValue > 0`.
- All "SKD group" comments → "condition group" across batch, config, and QC modules.

---

### 2026-06-19 — Visual QC exports: aligned STL, difference PLY, ROI reference STL

**Motivation:** After batch processing it was impossible to inspect intermediate results visually. The only files on disk were CSVs and transform JSONs; the actual scan geometry in the aligned frame, and the per-vertex error distribution, were discarded after metric computation.

**New files written by QCExporter (always when QC output is enabled):**

- **`qc/aligned_meshes/<scan>.stl`** — each scan's geometry after ICP, in the GPA reference frame. Load in any STL viewer to verify that scans from different scanners landed on top of each other.
- **`qc/difference_meshes/<scan>.ply`** — same aligned geometry as binary PLY (`format binary_little_endian 1.0`) with a per-vertex `distance` float property (signed mm to the reference). Open in MeshLab (*Render → Color → Per-Vertex Quality*) or ParaView to inspect spatial error distribution.
- **`qc/reference_meshes/<group>_roi.stl`** — the ROI-trimmed submesh used for ICP and distance computation (only when `useROIRef` is active, i.e. a geometric ROI is configured). Written in `GroupProcessor::process()` alongside the existing `<group>_reference.stl`.

**New QCExporter methods:**
- `exportAlignedMesh(scan, outputDir, filename)` — calls `writeBinarySTL(scan->mesh, …)`.
- `exportDifferencePLY(scan, outputDir, filename)` — calls new `writeBinaryPLY(mesh, distanceToRef, "distance", …)`.
- `writeBinaryPLY(mesh, perVertexScalar, scalarName, filePath)` — writes standard binary PLY with vertices (x,y,z,scalar) and face connectivity (uchar count + int indices). No VTK dependency.
- `createQCDirectories()` updated to create `qc/aligned_meshes/` and `qc/difference_meshes/`.

**Files modified:** `src/qc/QCExporter.{h,cpp}`, `src/batch/GroupProcessor.cpp`.

---

### 2026-06-19 — Reference-side ROI masking (architectural change)

**Problem:** The previous implementation applied the geometric ROI (bounding box, z-plane slab, brush override zones) to each source scan individually using absolute world coordinates. This failed whenever a source scanner had a different coordinate system origin from the one on which the template was defined: the bounding box or z-plane would miss most of the scan's vertices, the ICP mask was nearly empty, ICP diverged, and the resulting RMS was in the mm range with near-zero coverage.

Root cause: ROI coordinates are defined in the canonical reference frame (the scan used in the ROI Template Editor). Transferring them to a source scan in a different coordinate frame requires that scan to already be closely aligned — but that is the job of ICP. The dependency was circular.

**Fix: apply the ROI to the reference once (`extractROIReference`):**

1. At the start of each group's distance-computation stage, `GroupProcessor::process()` calls `extractROIReference(referenceMesh, effectiveROI)` to build a new `ScanData` whose mesh contains only the faces whose three vertices are all inside the geometric ROI.
2. Source scans (full mesh) are aligned to this trimmed reference via standard `align()` — no `alignMasked()`, no per-scan coordinate logic. ICP focuses on the ROI region because that is all the reference offers.
3. Distances are computed from the full source to the trimmed reference. Source vertices with distance > 5 mm (well outside the ROI) are excluded from trueness metrics by `maxMetricDist = 5.0`. Tooth segmentation masks, when present, additionally filter the vertex set.
4. The ROI reference is also saved as `qc/reference_meshes/<group>_roi.stl` for visual verification.

**`useROIRef` flag:**
```cpp
const bool useROIRef = !forceFullMesh &&
    (effectiveROI.bbox.active || effectiveROI.zPlane.active ||
     !effectiveROI.brushZones.empty());
```
Tooth masks are excluded from `useROIRef` because they cannot be transferred to the reference without re-running segmentation on different vertex indices.

**`extractROIReference` implementation** (`GroupProcessor.cpp`): iterates all faces of `refMesh`; includes a face only if all three vertices pass all active geometric ROI tests. Uses `std::unordered_map<std::size_t, SurfaceMesh::Vertex_index>` for O(1) vertex index remapping.

**`computeTruenessMetrics` change:** The source-side geometric ROI mask and `computeROIMask()` call were removed. Replaced by the `maxMetricDist` threshold:
```cpp
if (std::abs(d) > maxMetricDist) continue;  // outside ROI region
if (toothMask && !(*toothMask)[i]) continue; // outside tooth segmentation
```

**Files modified:** `src/batch/GroupProcessor.{h,cpp}`.

---

### 2026-06-19 — Coarse-to-fine ICP hierarchy with curvature-weighted QEM (Xi-2025)

**Motivation:** Point-to-plane ICP is susceptible to local minima when the initial alignment offset is large (cross-scanner centroid variation, patient posture changes). A coarse-to-fine resolution hierarchy avoids this by starting with a heavily decimated version of the source mesh (few triangles → wide convergence basin) and progressively refining.

**Algorithm (Xi-2025 Algorithm 1 — Selective Downsampling):**
Standard Garland-Heckbert QEM edge-collapse cost `v̄ᵀ(Q₀+Q₁)v̄` is multiplied by a curvature coefficient:
- `coeff = 10.0` if mean curvature of the edge endpoint pair is **negative** (concave: CEJ, developmental grooves, gingival crevice)
- `coeff = 1.0` if mean curvature is **positive** (convex: tooth cusps, ridges)

Negative-curvature edges cost more to collapse → they survive longer → the decimated mesh retains more triangles at tooth boundaries. This preserves 10-15% more boundary vertices vs. standard QEM at equivalent face counts (Xi-2025, Table 1).

**Implementation (5 new/changed files):**

- **`src/core/MeshDecimation.h`** — new file. Declares `MeshDecimation::decimate(source, targetFaceFraction, negCurvK)`.

- **`src/core/MeshDecimation.cpp`** — new file. Implements the curvature-weighted QEM via CGAL SMS:
  - `CurvWeightedQEMCost<TM,GT>` — subclass of `GarlandHeckbert_triangle_policies`. Overrides the 2-arg cost `operator()` to multiply base GH cost by the curvature coefficient. The 1-arg placement `operator()` delegates to GH optimal point unchanged. `update_after_collapse()` averages curvature of the two collapsed endpoints for the survivor.
  - `buildCurvatureCoeffs()` — reads `"v:mean_curv"` property map; returns per-vertex coefficients.
  - `decimate()` — copies source, computes curvature if not present, runs `SMS::edge_collapse()` with `Face_count_ratio_stop_predicate`, calls `mesh.collect_garbage()` afterwards.

- **`src/core/ICPRegistration.h`** — Added `useHierarchy`, `hierarchyLevels`, `negCurvK` to `Params`. Added `alignHierarchical()` and `alignMaskedHierarchical()`.

- **`src/core/ICPRegistration.cpp`** — Implements `alignHierarchical()` and `alignMaskedHierarchical()`:
  - `alignHierarchical()`: Maintains a `workCopy` ScanData. For each level ≠ last, decimates `workCopy` to that fraction, runs `align()`, applies delta transform to `workCopy`, accumulates into `cumT`. Final level runs `align()` on full-res `workCopy`.
  - `alignMaskedHierarchical()`: Same pattern; coarse levels use `align()` (no mask), final level uses `alignMasked()` with the ROI mask. Vertex indices are stable through `applyTransform()` so the mask remains valid.

- **`src/config/StudyConfig.h/cpp`** — Added `useIcpHierarchy`, `icpHierarchyLevels`, `icpHierarchyNegCurvK` to `AlignmentConfig`. Serialized/parsed in both JSON and YAML paths.

- **`src/batch/GroupProcessor.cpp`** — All three ICP call sites (external-ref, pre-aligned, GPA refinement) now check `alignment.useIcpHierarchy` and dispatch to `alignHierarchical()` / `alignMaskedHierarchical()` when set.

- **`src/main.cpp`** — Added `--icp-hierarchy` CLI flag. Overrides `use_icp_hierarchy` in study JSON.

- **`src/gui/MainWindow.{h,cpp}`** — Added `m_icpHierarchyChk` checkbox. Persisted in QSettings. Wired into `m_studyConfig.alignment.useIcpHierarchy` at batch start.

**Design notes:**
- The hierarchy is NOT used inside GPA iterations (would be ~3× slower for large studies). GPA always uses flat `align()`/`alignMasked()`. The hierarchy applies only to the ICP refinement passes after GPA.
- Default levels `{0.05, 0.20, 1.0}` give 5% → 20% → full-mesh. Runtime overhead is ~30% (decimated levels are much faster than full-mesh).
- Curvature for weighting is read from `"v:mean_curv"` property map (set by `CurvatureAnalysis::compute()`). If not yet computed, `decimate()` calls `CurvatureAnalysis::compute()` automatically. Falls back to standard GH (all coeff=1) if curvature computation fails.

---

### 2026-06-19 — ROI-masked ICP for all alignment paths

**Problem:** `alignMasked()` was only wired into the external-reference ICP path. The GPA path (`GPAReference::compute()`) and the pre-aligned ICP path always used full-mesh `align()`, silently ignoring any active ROI (bbox, plane slab, brush zones, tooth seeds) during registration. Metrics were already ROI-restricted, creating a mismatch: alignment used the full mesh while evaluation used only the ROI.

**Fix (4 files):**

- **`GPAReference.h`**: Added `std::vector<std::vector<bool>> scanMasks` to `Params`. Index matches the `scans` vector passed to `compute()`. Empty = full-mesh ICP for that scan.
- **`GPAReference.cpp`**: In the GPA iteration loop, both the coarse pass (cycle 0) and fine pass now use `alignMasked()` when a non-empty mask is present for the scan index, falling back to `align()` otherwise.
- **`GroupProcessor.h`**: Added `const std::vector<std::vector<bool>>& icpMasks = {}` parameter to `runGPAAlignment()`.
- **`GroupProcessor.cpp`**:
  - `effectiveROI` (combined ROI from template or group config) computed once at the top of `process()` so all stages share the same config.
  - Combined ICP masks (bbox + z-plane + brush + tooth) built once per scan after tooth-mask computation and before any alignment. Logged with active components.
  - `icpMasks` passed to `runGPAAlignment()` → `GPAReference::Params::scanMasks`.
  - Pre-aligned ICP path also updated to use `alignMasked()` from the same `icpMasks`.
  - External-reference ICP path simplified to use pre-computed `icpMasks` (removed duplicate per-scan mask computation).
  - `metricsROI` is now a `const ROIConfig&` alias of `effectiveROI` (was a copied `ROIConfig`) — alignment and metrics always use exactly the same ROI.
- **`MainWindow.cpp`**: Log message now lists which ROI components are active instead of only checking for tooth seeds (old message incorrectly said "full-mesh ICP" when bbox/z-plane were active but no seeds were present).

**Important caveat:** Masks are computed once in the pre-GPA coordinate frame and kept fixed across all GPA cycles. This is valid for patient scans (`scans_normalized=true`) because GPA only applies sub-mm corrections. For raw unnormalized scans starting far from canonical, bbox/z-plane masks could drift slightly — tooth masks (vertex-indexed) are always valid regardless of scan movement.

---

## Current Status (as of 2026-06-15)

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
- File discovery with glob patterns and scanner ID matching; repetition extraction handles `_D1_`–`_D7_` naming (Nold study) as well as `_r1`, `rep1`, `(1)` etc.
- Per-group GPA alignment and distance computation
- Trueness metrics (RMS, MAD, H100, H95, bias, coverage, boundary length, hole count, stitching angle)
- Tessellation metrics (triangle count, mean edge, mean/max aspect ratio, ATI, density at high/low curvature)
- Precision metrics (pairwise RMS between repetitions)
- CSV output (metrics, precision, summary) with generic `Group_ID` string column — compatible with both phantom (SKD) and patient study designs
- Optimized `computePairwise()` for efficient precision computation
- **ROI filtering fully integrated** (bbox, Z-plane, brush zones, sigma clipping) — any active component restricts BOTH ICP alignment (GPA, pre-aligned, external-ref) and metric computation
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
  - `--normalized` flag (GUI default: on) suppresses JSON loading when geometry is already transformed
  - Masked ICP: use tooth segmentation mask to focus alignment on tooth surfaces
- **Trimmed ICP (TrICP)**: `icp_trim_fraction` in study JSON / `--trim-fraction` CLI / GUI spinbox. Sorts ICP correspondences by point-to-plane residual and discards the worst fraction; suppresses soft-tissue deformation from corrupting the rigid solve. Default 1.0 (no trimming). Set to 0.5 for patient scans with extensive soft tissue.
- **ROI-masked GPA ICP**: `GPAReference::Params::scanMasks` — per-scan vertex masks applied during GPA iterations (both coarse and fine pass). `GroupProcessor` computes combined masks (bbox + z-plane + brush + tooth) once before alignment and propagates them to all three ICP paths.

### Test Results

**P2026-Kessler (phantom, 6 scanners × 7 SKD levels):**
- 185 scans total (5 repetitions per scanner per SKD)
- SKD 18: 5 iTeroLumina scans only (unbalanced)
- SKD 20-30: 30 scans each (all 6 scanners)
- Trueness RMS: 0.032–0.074 mm (typical range)
- Precision Mean RMS: ~0.27 mm (pairwise comparisons)
- Results in: `/KHKsData/usr2/daten/P2026-Kessler-ScanVergleich/IOSScannerComparison/results_DentScanComparePro_all/`

**P2026-Nold (patient cohort, 4 scanners × 16 patients × 7 repetitions):**
- 432 scans total; Medit700 patient 008 entirely missing
- Groups are patient IDs (002–017); statistics use `lmer(RMS ~ Scanner + (1|Patient_ID))`
- Pre-aligned STL files in `/media/kkunzelm/T7/P2026-Nold/Patientenscans/flat_aligned/`
- Config generated by `scripts/gen_nold_study_config.py`
- Analysis script: `scripts/analyze_results_Nold.R`

### Not Yet Implemented / Known Issues
- **Difference image export**: Disabled in batch mode due to VTK headless rendering crash (GLEW/OpenGL initialization fails without display). GPA meshes and transforms still export.
- YAML configuration support (JSON only currently working)
- Statistical output enhancement (R-ready format, effect sizes)
- QC Review UI needs testing with real QC data

---

## Next Steps

1. **Run P2026-Nold batch**: `gen_nold_study_config.py --execute` → rebuild → `DentScanComparePro --batch --study … --data-root … --output … --verbose`
2. **Fix VTK headless rendering**: Enable difference image export in batch mode
   - Option A: Rebuild VTK with OSMesa support
   - Option B: Use EGL backend for headless GPU rendering
   - Option C: Generate images in GUI post-processing step
3. **Test QC Review workflow**: Verify thumbnail loading, errand flagging, CSV filtering
4. **Test ErrandResolutionDialog**: Verify landmark picking, Kabsch alignment, ICP refinement
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

| Study | Data | Config | Results |
|-------|------|--------|---------|
| P2026-Kessler (phantom) | `/KHKsData/usr2/daten/P2026-Kessler-ScanVergleich/IOS/` | `data/full_study.json` | `…/results_DentScanComparePro_all/` |
| P2026-Nold (patient) | `/media/kkunzelm/T7/P2026-Nold/Patientenscans/flat_aligned/` | generated by `scripts/gen_nold_study_config.py` | `./results_P2026_Nold/` |

- **Source DentScanCompare**: `/home/kkunzelm/claude-code/DentScanCompare/`
- **R analysis (Kessler)**: `scripts/analyze_results.R`
- **R analysis (Nold)**: `scripts/analyze_results_Nold.R`
- **Study config reference**: `docs/study-config-reference.md` — field-by-field JSON documentation, two-factor design guide

---

## Changelog

### 2026-06-19 – Trimmed ICP (TrICP) for Soft-Tissue Outlier Rejection

**Motivation:** Patient intraoral scans (P2026-Nold) produced trueness RMS values in the mm range. Root cause: ICP correspondences on deforming gingiva, buccal mucosa, and soft palate have large point-to-plane residuals that pull the rigid transform away from the correct tooth alignment. The only existing rejection mechanism was a fixed `maxCorrespDist` threshold (5 mm), which let all soft-tissue correspondences in.

**Fix: Trimmed ICP** — after the distance gate, correspondences are sorted by point-to-plane residual and the worst `(1 − trimFraction)` fraction is discarded before the least-squares solve. Teeth have small residuals and survive; deforming soft tissue has large residuals and is discarded.

**`ICPRegistration::Params::trimFraction`** (default `1.0` = backward-compatible, no trimming):
- Implemented in both `align()` and `alignMasked()` in `ICPRegistration.cpp`
- Sort is O(N log N) on the correspondence list; negligible overhead vs ICP iteration cost

**Configuration chain:**
- `AlignmentConfig::icpTrimFraction` (default `1.0`) in `StudyConfig.h`
- JSON field: `study.alignment.icp_trim_fraction` (parsed/serialized in JSON and YAML paths)
- CLI flag: `--trim-fraction <f>` in `main.cpp`
- GUI spinbox: "ICP trim fraction" (0.10–1.00, step 0.05) on Study Configuration tab, persisted via QSettings

**GroupProcessor.cpp:** all three ICP call sites (GPA path, pre-aligned mean-mesh path, external-reference path) now pass `alignment.icpTrimFraction` rather than a hardcoded value. Phantom studies use the default `1.0`; patient studies add `"icp_trim_fraction": 0.5` to their JSON config.

**Files modified:**
- `src/core/ICPRegistration.h` — `trimFraction` field in `Params`
- `src/core/ICPRegistration.cpp` — TrICP in `align()` and `alignMasked()`
- `src/config/StudyConfig.h` — `icpTrimFraction` in `AlignmentConfig`
- `src/config/StudyConfig.cpp` — JSON and YAML parse/serialize
- `src/batch/GroupProcessor.cpp` — pass `alignment.icpTrimFraction` at all ICP call sites
- `src/main.cpp` — `--trim-fraction` CLI option
- `src/gui/MainWindow.h` — `m_icpTrimFractionSpin` member
- `src/gui/MainWindow.cpp` — spinbox creation, QSettings load/save, batch config wiring

### 2026-06-15 – Generic Group IDs, MaxAspRatio in CSV, Patient Study Support

**Motivation:** Second study (P2026-Nold) uses 16 patients as groups instead of SKD levels. The CSV output previously wrote an integer `SKD_Value` column, which would always be 0 for patient groups. Tessellation metric `maxAspectRatio` was computed by `TessellationMetrics::fillReport()` but silently dropped in CSV output.

**CSVWriter.cpp — five changes:**
1. `writeTruenessHeader()`: `SKD_Value` (int) → `Group_ID` (string); `MaxAspRatio` column inserted after `AspRatio`
2. `writeTruenessCSV()` and `appendTruenessCSV()` data rows: `report.skd_mm` → `escapeCSV(report.groupId)`; `report.maxAspectRatio` added
3. Precision header: `SKD_Value` → `Group_ID`
4. Precision data rows: `report.skd_mm` → `escapeCSV(report.groupId)`
5. `writeSummaryCSV()`: `int skd` → `QString groupId` in the Summary struct; map key changed from `pair<string,int>` to `pair<string,string>`

The `int skd_mm` field still exists in `BatchMetricReport` and `PrecisionReport` structs (populated from `group.skd_mm` in GroupProcessor) but is no longer written to any CSV. Set `skd_mm: 0` in patient study configs.

**FileDiscovery.cpp — extractRepetitionId():**
Added `_D(\d+)_` as the first (highest-priority) pattern to correctly extract repetition 1–7 from filenames like `Carestream3700_002_D1_aligned.stl`. Without this, the function returned 0 and fell back to non-deterministic sequential numbering per scanner.

**New scripts:**
- `scripts/gen_nold_study_config.py` — reads `flat_aligned/*_aligned.stl`, prints observation matrix, generates DentScanComparePro JSON with one group per patient. `--reorganize --execute` moves files into `{Scanner}/{Patient}/` subdirectory tree.
- `scripts/analyze_results_Nold.R` — R analysis script for patient cohort design. Primary model: `lmer(RMS_mm ~ Scanner + (1|Patient_ID), REML=TRUE)` via lme4/lmerTest. Variance components, ICC, patient profile plots. Original `analyze_results.R` left untouched as Kessler reference.

**Files modified:**
- `src/batch/CSVWriter.cpp`
- `src/config/FileDiscovery.cpp`
- `scripts/gen_nold_study_config.py` (new)
- `scripts/analyze_results_Nold.R` (new)
- `README.md`, `docs/developer-handoff.md` (this file)

### 2026-06-09 – Normalized Scans Option (DentScanAlign Double-Transform Fix)

**Problem:** When using DentScanAlign normalized STL files together with a study config that had `alignments_directory` set, the JSON transform was applied a second time at Stage 2.5 (GroupProcessor.cpp:81), because transform loading was unconditional — guarded only by `!precomputedTransforms.empty()`, not by the `scansPreAligned` flag. This caused ICP to diverge for all scans. The only workaround was to delete the JSON files from disk.

**Root Cause — Architecture of the pre-alignment pipeline:**

Stage 2.5 in `GroupProcessor::process()` applies precomputed transforms unconditionally:
```cpp
if (!precomputedTransforms.empty()) {  // no check of scansPreAligned here
    ICPRegistration::applyTransform(*scan, it->second);
```

The `scansPreAligned` flag only affects:
1. Whether GPA is skipped (`GroupProcessor.cpp:212` — `else` branch not taken)
2. The ICP correspondence distance threshold (`icpParams.maxCorrespDist = 5.0 mm`)

It does **not** prevent JSON transforms from being loaded or applied. The two concerns are orthogonal.

**Fix: `scansNormalized` flag (default: true)**

Added `bool scansNormalized = true` to `StudyConfig`. When true, `BatchRunner` skips the `AlignmentTransformLoader::loadTransforms()` call entirely, so `precomputedTransforms` stays empty and Stage 2.5 is a no-op.

The flag defaults to `true` because the normalized-scan workflow is more common; users working with raw scans + JSON transforms must explicitly uncheck it.

**DentScanAlign workflows — summary:**

| Workflow | STL source | JSON transforms | `scansPreAligned` | `scansNormalized` |
|----------|------------|-----------------|-------------------|-------------------|
| Normalized scans | Normalized output | Ignored | optional | **true** (default) |
| Raw scans + JSON | Original scan files | Applied at Stage 2.5 | true (recommended) | false |

**Files Modified:**
- `src/config/StudyConfig.h` — Added `scansNormalized = true` field
- `src/config/StudyConfig.cpp` — Serialize/deserialize `scans_normalized` (always written)
- `src/batch/BatchRunner.cpp` — Guard transform loading with `!config.scansNormalized`
- `src/gui/MainWindow.h` — Added `m_scansNormalizedChk`
- `src/gui/MainWindow.cpp` — New checkbox (checked by default), renamed pre-aligned checkbox, updated tooltips, log messages, and config wiring
- `src/main.cpp` — Added `--normalized` CLI option
- `docs/user-manual.md` — DentScanAlign workflow section rewritten with Workflow A/B split
- `docs/developer-handoff.md` — This entry

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
