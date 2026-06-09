# DentScanComparePro User Manual

## Overview

DentScanComparePro is a tool for automated batch evaluation of dental intraoral scanner accuracy. It computes ISO 5725/12836-compliant trueness and precision metrics across multiple scanners and clinical conditions (SKD levels = inter-incisor distance).

The application supports two modes:
- **GUI Mode** (default): Interactive interface for configuration, ROI template editing, and QC review
- **CLI Mode** (`--batch`): Headless batch processing for automated pipelines

---

## Quick Start

### GUI Mode
```bash
./DentScanComparePro
```

### CLI Batch Mode
```bash
./DentScanComparePro --batch \
    --study study.json \
    --data-root /path/to/scans \
    --output /path/to/results \
    --verbose
```

---

## Complete Workflow

### Step 1: Prepare Your Data

Organize your scanner data in a directory structure like:
```
data/
├── Primescan/
│   ├── SKD_20/
│   │   ├── scan_r1.stl
│   │   ├── scan_r2.stl
│   │   └── ...
│   ├── SKD_22/
│   └── ...
├── Trios5/
│   └── ...
└── ...
```

### Step 2: Create a Study Configuration

Create a JSON file describing your study:

```json
{
  "study": {
    "name": "Scanner_Comparison_2024",
    "description": "Comparing 6 intraoral scanners across SKD levels"
  },
  "scanners": [
    {"id": "Primescan", "patterns": ["*Primescan*", "*PS*"]},
    {"id": "Trios5", "patterns": ["*Trios*", "*T5*"]},
    {"id": "iTeroLumina", "patterns": ["*iTero*", "*Lumina*"]}
  ],
  "groups": [
    {"id": "SKD_20", "skd_mm": 20, "file_patterns": ["**/SKD_20/*.stl", "**/20mm/*.stl"]},
    {"id": "SKD_22", "skd_mm": 22, "file_patterns": ["**/SKD_22/*.stl"]},
    {"id": "SKD_24", "skd_mm": 24, "file_patterns": ["**/SKD_24/*.stl"]}
  ],
  "output": {
    "metrics_csv": "trueness_metrics.csv",
    "precision_csv": "precision_metrics.csv",
    "summary_csv": "summary_stats.csv"
  }
}
```

### Step 3: Create an ROI Template (Optional but Recommended)

The ROI template defines which region of the scan to analyze. This is especially important for focusing on tooth surfaces and excluding gingiva.

#### Understanding the ROI Selection Model

The ROI (Region of Interest) is built from multiple layers that combine to determine which vertices are included in analysis:

```
┌─────────────────────────────────────────────────────────────────┐
│  GEOMETRIC CONSTRAINTS (combined with AND)                      │
│  ┌──────────────┐  ┌──────────────┐  ┌──────────────┐          │
│  │ Bounding Box │  │  Plane Slab  │  │    Base      │          │
│  │  (optional)  │──│  (optional)  │──│  Selection   │          │
│  └──────────────┘  └──────────────┘  └──────────────┘          │
└─────────────────────────────────────────────────────────────────┘
                              │
                              ▼
┌─────────────────────────────────────────────────────────────────┐
│  MANUAL OVERRIDES (applied on top)                              │
│  ┌──────────────────────────────────────────────────────────┐  │
│  │  Include zones (green) → force vertices IN               │  │
│  │  Exclude zones (red) → force vertices OUT              │  │
│  └──────────────────────────────────────────────────────────┘  │
└─────────────────────────────────────────────────────────────────┘
                              │
                              ▼
┌─────────────────────────────────────────────────────────────────┐
│  FINAL ROI → used for ICP alignment and metric computation      │
└─────────────────────────────────────────────────────────────────┘
```

**Key Concepts:**

| Term | Description | Visualization |
|------|-------------|---------------|
| **Base Selection** | Initial vertex selection from Tooth Segmentation algorithm. Computed from seed points using geodesic distance and curvature. | Ivory (selected) / Dark grey (not selected) |
| **Manual Overrides** | Brush zones that force-include or force-exclude specific areas. These are saved with the ROI template and applied after scan alignment. | Green (include) / Red (exclude) |

**Important Distinction:**

- **Base Selection** is scan-specific: it's computed from a template scan's geometry and identifies tooth crown vertices
- **Manual Overrides** are transferable: they're defined as 3D regions and can be applied to other scans after alignment

#### Creating an ROI Template

**In GUI Mode:**

1. Go to the **ROI Template Editor** tab
2. Enter a path or click **Browse...** then **Load** to load a representative STL file
3. Configure the region of interest:
   - **Bounding Box**: Limit analysis to a rectangular region
   - **Plane Slab (ROI Height)**: Define a slab with **Offset A** and **Offset B** distances from a picked plane
   - **Tooth Segmentation**: Place seed points on tooth cusps, then run segmentation to compute the Base Selection
4. Click **Save Template...** in the ROI Template I/O section to save as JSON

**Tooth Segmentation Workflow (Base Selection):**

1. Click **Pick Seeds** button
2. Click on the cusp tips of each tooth (one click per tooth)
3. Adjust parameters if needed:
   - Max Geodesic: tooth size limit (default 12 mm)
   - Max Crease: angle at CEJ boundary (default 50°)
   - Min Curvature: gingival sulcus threshold (default -4.0)
4. Click **Run Segmentation** to compute the Base Selection
5. Check **Use Base Selection as ROI** to include it in the final ROI
6. Save the template

**Brush Tool (Manual Adjustments):**

The brush tool has two modes controlled by the **"Edit Base Selection"** checkbox:

| Checkbox State | Mode | Effect | Colors |
|----------------|------|--------|--------|
| **Checked** | Edit Base Selection | Directly modify the segmentation result. Changes are permanent to the current template. | Ivory / Dark grey |
| **Unchecked** | Create Manual Overrides | Create include/exclude zones that override other ROI layers. These zones are transferable to other scans. | Green / Red |

**To use the brush tool:**

1. Choose the mode via **"Edit Base Selection"** checkbox
2. Click **Include** or **Exclude** button to activate painting
   - This automatically disables seed picking mode if it was active
3. Set **Radius** (brush size in mm, 3D spherical radius)
4. Click on mesh to paint regions

**Note:** Pick modes are mutually exclusive. Activating the brush (Include/Exclude) will disable seed picking. Activating seed picking will disable the brush.

**Manual Overrides (when "Edit Base Selection" is unchecked):**

| Button | Effect | Visualization |
|--------|--------|---------------|
| Include | Force vertices INTO ROI regardless of other constraints | Bright green |
| Exclude | Force vertices OUT of ROI regardless of other constraints | Red |

Use Manual Overrides to:
- Include a region that's outside the bounding box or plane slab
- Exclude artifacts or unwanted areas that the segmentation included
- Make adjustments that should apply to all scans in a batch

**Editing Base Selection (when "Edit Base Selection" is checked):**

| Button | Effect |
|--------|--------|
| Include | Add vertices to the Base Selection (tooth region) |
| Exclude | Remove vertices from the Base Selection |

Use this mode to:
- Fix segmentation errors (missed tooth areas, included gingiva)
- Fine-tune the boundary between tooth and gingiva

**Clearing Adjustments:**

- **"Clear Manual Overrides"** button: Removes all green/red brush zones. Does NOT affect the Base Selection.
- To reset the Base Selection: Re-run Tooth Segmentation with the same or different seed points.

### Step 4: Run Batch Processing

**GUI Mode:**
1. Go to **Study Configuration** tab
2. Set paths:
   - **Study Config**: Path to your study.json
   - **Data Root**: Directory containing scanner folders
   - **Output Dir**: Where to save results (used for full-mesh ICP)
   - **Masked ICP Output** (optional): Separate output directory for masked ICP results
   - **External Ref** (optional): CAD or lab scanner reference STL
   - **ROI Template** (optional): JSON file from ROI Template Editor (enables masked ICP and ROI-based metrics)
3. Configure DentScanAlign options (see below) if using DentScanAlign output
4. Click **Load Configuration** to verify
5. Go to **Batch Processing** tab
6. Configure **Registration Options**:
   - **Use ROI mask for registration**: Check to use masked ICP (ROI region only), uncheck for full-mesh ICP
7. Click **Run Batch Processing**

**Registration Options Explained:**

The **"Use ROI mask for registration (masked ICP)"** checkbox controls whether ROI restrictions are applied:

| Checkbox State | Registration | Metrics |
|----------------|--------------|---------|
| **Checked** | Masked ICP (uses active ROI components) | Filtered by ROI |
| **Unchecked** | Full-mesh ICP (ignores ALL ROI settings) | Full mesh metrics |

**When "Use ROI mask" is UNCHECKED (Full-Mesh Mode):**
- All ROI settings from templates and configs are **ignored**
- ICP uses all mesh vertices for alignment
- Metrics are computed on the entire mesh
- Console shows: `Full-mesh mode: ACTIVE (ignoring ROI settings from config)`

**When "Use ROI mask" is CHECKED (Masked ICP Mode):**

Masked ICP uses **all active ROI components** combined with AND logic:

| Component | Active Toggle | Effect on Masked ICP |
|-----------|--------------|---------------------|
| Bounding Box | "Active" checkbox | Only vertices inside box used for alignment |
| Plane Slab | "Active" checkbox | Only vertices in slab used for alignment |
| Manual Overrides | (always if present) | Include/exclude specific regions (green/red zones) |
| Base Selection | "Use Base Selection as ROI" | Only tooth crown vertices used |

**Note:** The Plane Slab is now **inactive by default**. If you want to restrict analysis to the occlusal region, you must explicitly check the "Active" checkbox in the Plane Slab section.

**Output Directory Selection:**

| Masked ICP Checkbox | Masked ICP Output Dir | Result |
|---------------------|----------------------|--------|
| Checked | Specified | Results saved to Masked ICP Output |
| Checked | Empty | Results saved to Output Dir |
| Unchecked | Any | Results saved to Output Dir |

Note: Even when masked ICP is disabled, the ROI template is still used to filter which vertices are included in metric calculations (RMS, precision, etc.).

**CLI Mode:**
```bash
./DentScanComparePro --batch \
    --study study.json \
    --data-root /path/to/scans \
    --output ./results \
    --roi-template roi_template.json \
    --verbose
```

**DentScanAlign Integration — Two Workflows**

DentScanAlign produces two outputs for each scan: a **normalized STL** (mesh geometry already in the aligned coordinate frame) and a **JSON transform file** (the 4×4 matrix that was applied). These two outputs require different settings in DentScanComparePro.

**Workflow A — Using normalized STL files (transform already baked into geometry):**

This is the most common case. The STL files are already positioned correctly; the JSON files must be ignored to avoid applying the transform a second time.

```bash
./DentScanComparePro --batch \
    --study study.json \
    --data-root normalized/ \
    --external-ref reference.stl \
    --normalized \
    --verbose
```

GUI: enable **"Scans are normalized (skip JSON transforms, already applied)"** (checked by default).

**Workflow B — Using raw STL files with JSON transforms:**

The STL files are in their original scanner coordinate frame. The JSON transform files are loaded and applied before ICP refinement.

```bash
./DentScanComparePro --batch \
    --study study.json \
    --data-root raw/ \
    --alignments alignments/ \
    --external-ref reference.stl \
    --pre-aligned \
    --verbose
```

GUI: uncheck **"Scans are normalized"**, check **"Scans are pre-aligned, use JSON transforms for ICP"**, and set `alignments_directory` in your study config.

### Step 5: Review Results

**Output Files:**

| File | Description |
|------|-------------|
| `trueness_metrics.csv` | Per-scan metrics (RMS, MAD, Max, P95, coverage) |
| `precision_metrics.csv` | Per-scanner-per-SKD pairwise precision |
| `summary_stats.csv` | Aggregated statistics by scanner and SKD |

**QC Data (in `qc/` subdirectory):**

| Directory | Contents |
|-----------|----------|
| `qc/reference_meshes/` | Reference meshes (GPA mean or external reference, one STL per SKD group) |
| `qc/transforms/` | Transform matrices + metrics (JSON per scan) |
| `qc/segmented/` | Tooth-only meshes (when Base Selection is used) |
| `qc/difference_images/` | Color-coded distance maps (PNG) |

### Step 6: Quality Control Review

Poor registrations can corrupt your statistics. Use the QC workflow to verify alignments.

**In GUI Mode:**

1. Go to **QC Review** tab
2. Click **Load QC Data from Results**
3. Review thumbnails:
   - **Green border** = Accepted
   - **Red border** = Flagged as errand
   - **Yellow border** = Statistical outlier (RMS > mean + 2σ)
   - **Grey border** = Pending review
4. **Double-click** a thumbnail to open detailed view:
   - See reference mesh (grey wireframe) + scan (distance colored)
   - View metrics (RMS, Max, Coverage)
   - Click **Accept**, **Flag as Errand**, or **Skip**
5. For flagged scans, use **ErrandResolutionDialog** to manually re-register:
   - Pick 3+ corresponding landmarks on reference and scan
   - Click **Compute Alignment** (Kabsch algorithm)
   - Click **Run ICP** for refinement
   - Accept or reject the corrected registration
6. Click **Save QC Status** when done

---

## CLI Reference

```
Usage: DentScanComparePro [options]

Options:
  -b, --batch              Run in headless CLI mode (no GUI)
  -s, --study <file>       Path to study configuration JSON
  -d, --data-root <dir>    Root directory containing scanner folders
  -o, --output <dir>       Output directory (default: ./results)
  -r, --roi-template <file> ROI template with tooth segmentation settings
  -a, --alignments <dir>   Directory with DentScanAlign JSON transforms
  -e, --external-ref <file> External reference STL (CAD or lab scanner)
  --pre-aligned            Skip GPA; apply JSON transforms before ICP refinement
  --normalized             Scans are normalized; skip JSON transform loading (default in GUI)
  --verbose                Print detailed progress information
  -h, --help               Show help message
```

---

## Understanding the Metrics

For detailed metric interpretation, see **docs/metric-interpretation.md**.

### Tessellation Quality Metrics (per scan, measured before registration)

| Metric | CSV Column | Description |
|--------|------------|-------------|
| **Triangles** | `Triangles` | Total triangular faces in the mesh |
| **Edge** | `Edge_mm` | Mean edge length (mm). Smaller = finer mesh |
| **AspRatio** | `AspRatio` | Mean aspect ratio (longest/shortest edge). 1.0 = equilateral |
| **ATI** | `ATI` | Adaptive Tessellation Index. Spearman correlation between curvature and 1/area. +1 = ideal adaptive, 0 = uniform |
| **DensHighκ** | `DensHighK` | Triangle density in high-curvature zones (triangles/mm²) |
| **DensLowκ** | `DensLowK` | Triangle density in low-curvature zones (triangles/mm²) |

### Accuracy Metrics (per scan, after registration)

| Metric | CSV Column | Description |
|--------|------------|-------------|
| **RMS** | `RMS_mm` | Root Mean Square distance to reference (mm). Primary accuracy metric |
| **MAD** | `MAD_mm` | Median Absolute Deviation (mm). Robust to outliers |
| **H100** | `H100_mm` | Maximum distance (Hausdorff 100%). Dominated by boundary artifacts |
| **H95** | `H95_mm` | 95th percentile distance. Clinically meaningful: 95% of surface within this |
| **Bias** | `Bias_mm` | Signed mean distance. Positive = scan outside reference (oversized) |

### Completeness Metrics (per scan)

| Metric | CSV Column | Description |
|--------|------------|-------------|
| **Coverage** | `Coverage_pct` | Percentage of vertices within 0.2 mm of reference |
| **Boundary** | `Boundary_mm` | Total length of open boundary edges (mm) |
| **Holes** | `Holes` | Number of topological holes (open boundary loops) |
| **Stitch** | `Stitch_deg` | Maximum normal discontinuity angle (°). High values indicate stitching artifacts |

### Precision Metrics (per scanner per SKD)

| Metric | Description |
|--------|-------------|
| **Mean RMS** | Average pairwise RMS between repetitions |
| **SD** | Standard deviation of pairwise RMS |
| **CV** | Coefficient of Variation (SD/Mean) |
| **Pairwise Count** | Number of scan pairs compared |

---

## Pipeline Stages

The batch processor executes these stages for each SKD group:

1. **Load STL files** - Parse binary STL into CGAL meshes
2. **Compute curvature** - CGAL interpolated curvatures for segmentation
3. **Compute tessellation metrics** - ATI, edge length, aspect ratio, curvature densities
4. **Compute Base Selection** (if ROI template provided) - Dijkstra-based segmentation from seeds
5. **Apply pre-computed transforms** (if `--alignments` provided) - Load DentScanAlign results
6. **Alignment** - GPA or ICP against external reference
   - Uses **masked ICP** when ROI is defined (focuses on tooth surfaces)
7. **Compute distances** - CGAL AABB-tree signed distances to reference
8. **Compute trueness metrics** - RMS, MAD, Hausdorff, coverage, completeness
9. **Compute precision metrics** - Pairwise comparisons between repetitions
10. **Export QC data** - GPA means, transforms, segmented meshes

---

## Tips and Best Practices

### For Best Results

1. **Use tooth segmentation** - Gingiva deforms between scans; teeth are rigid
2. **Check statistical outliers** - Yellow-bordered scans may have registration failures
3. **Use external reference for trueness** - CAD or lab scanner provides ground truth
4. **Review before statistics** - QC catches registration failures before they corrupt data

### Common Issues

| Problem | Solution |
|---------|----------|
| High RMS values | Check if registration failed; use QC review |
| Missing files | Verify glob patterns in study.json |
| Scans rotated 90° | Different scanners use different coordinate systems; ICP should handle this |
| Memory issues | Process fewer groups at a time |
| ROI still active when disabled | See "Full-Mesh Mode" below |
| Precision metrics hang | Fixed in v1.0.1 - AABB trees are now cached |

### Full-Mesh Mode (Important for Normalized/Pre-Aligned Scans)

If you're using normalized STL files from DentScanAlign, you typically want **full-mesh ICP** without any ROI restrictions.

**To ensure full-mesh mode:**
1. **Uncheck** "Use ROI mask for registration (masked ICP)" in Batch Processing options
2. The console should show: `Full-mesh mode: ACTIVE (ignoring ROI settings from config)`

**If you see `zPlane=ON` in the log but want full-mesh:**
This is normal - the log shows your config file's ROI settings, but they are **ignored** when the checkbox is unchecked. Look for the confirmation message `Full-mesh mode: ACTIVE`.

**To permanently disable Z-plane in your config:**
Edit your study config JSON/YAML and set:
```json
"z_plane": {
  "active": false
}
```
Or regenerate the config file using the new defaults.

### Resume After Interruption

If batch processing is interrupted:
```bash
# Simply re-run the same command - completed groups are skipped
./DentScanComparePro --batch --study study.json --data-root data/ --output results/
```

Progress is tracked in `.batch_progress.json` in the output directory.

---

## Workflow Diagram

```
┌─────────────────────────────────────────────────────────────────────┐
│  PREPARATION                                                        │
│  ┌──────────┐    ┌──────────┐    ┌──────────┐                       │
│  │ Organize │───▶│  Create  │───▶│  Create  │                       │
│  │   Data   │    │study.json│    │ROI Template│                     │
│  └──────────┘    └──────────┘    └──────────┘                       │
└─────────────────────────────────────────────────────────────────────┘
                              │
                              ▼
┌─────────────────────────────────────────────────────────────────────┐
│  BATCH PROCESSING                                                   │
│  ┌──────────┐    ┌──────────┐    ┌──────────┐    ┌──────────┐      │
│  │   Load   │───▶│  Align   │───▶│ Compute  │───▶│  Export  │      │
│  │   STLs   │    │  (ICP)   │    │ Metrics  │    │   CSV    │      │
│  └──────────┘    └──────────┘    └──────────┘    └──────────┘      │
└─────────────────────────────────────────────────────────────────────┘
                              │
                              ▼
┌─────────────────────────────────────────────────────────────────────┐
│  QUALITY CONTROL                                                    │
│  ┌──────────┐    ┌──────────┐    ┌──────────┐                       │
│  │  Review  │───▶│   Fix    │───▶│  Final   │                       │
│  │Thumbnails│    │ Errands  │    │ Statistics│                      │
│  └──────────┘    └──────────┘    └──────────┘                       │
└─────────────────────────────────────────────────────────────────────┘
```

---

## Support

For issues and feature requests, contact:

**Prof. Dr. Karl-Heinz Kunzelmann**
Website: [www.kunzelmann.de](https://www.kunzelmann.de)

---

DentScanComparePro v1.0
