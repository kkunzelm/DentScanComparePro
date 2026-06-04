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

**In GUI Mode:**

1. Go to the **ROI Template Editor** tab
2. Enter a path or click **Browse...** then **Load** to load a representative STL file
3. Configure the region of interest:
   - **Bounding Box**: Limit analysis to a rectangular region
   - **Plane Slab (ROI Height)**: Define a slab with **Offset A** and **Offset B** distances from a picked plane
   - **Tooth Segmentation**: Place seed points on tooth cusps, then run segmentation
4. Click **Save Template...** in the ROI Template I/O section to save as JSON

**Tooth Segmentation Workflow:**

1. Click **Pick Seeds** button
2. Click on the cusp tips of each tooth (one click per tooth)
3. Adjust parameters if needed:
   - Max Geodesic: tooth size limit (default 12 mm)
   - Max Crease: angle at CEJ boundary (default 50°)
   - Min Curvature: gingival sulcus threshold (default -4.0)
4. Click **Run Segmentation**
5. Check **Use tooth mask as ROI**
6. Save the template

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
3. Check **Scans are pre-aligned** if using DentScanAlign output
4. Click **Load Configuration** to verify
5. Go to **Batch Processing** tab
6. Configure **Registration Options**:
   - **Use ROI mask for registration**: Check to use masked ICP (tooth surfaces only), uncheck for full-mesh ICP
7. Click **Run Batch Processing**

**Registration Options Explained:**

| Checkbox State | ROI Template | Alignment Method | Output Directory |
|----------------|--------------|------------------|------------------|
| Checked | With tooth seeds | Masked ICP (tooth surfaces) | Masked ICP Output (if set) or Output Dir |
| Checked | No seeds/template | Full-mesh ICP | Output Dir |
| Unchecked | Any | Full-mesh ICP | Output Dir |

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

**With Pre-aligned Scans (from DentScanAlign):**
```bash
./DentScanComparePro --batch \
    --study study.json \
    --data-root normalized/ \
    --alignments alignments/ \
    --external-ref reference.stl \
    --pre-aligned \
    --verbose
```

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
| `qc/segmented/` | Tooth-only meshes (when tooth mask used) |
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
  --pre-aligned            Skip GPA; scans already coarsely aligned
  --verbose                Print detailed progress information
  -h, --help               Show help message
```

---

## Understanding the Metrics

### Trueness Metrics (per scan)

| Metric | Description |
|--------|-------------|
| **RMS** | Root Mean Square distance to reference (mm) |
| **MAD** | Mean Absolute Distance (mm) |
| **Max** | Maximum distance (Hausdorff 100%) |
| **P95** | 95th percentile distance (Hausdorff 95%) |
| **Signed Mean** | Mean signed distance (positive = scan outside reference) |
| **Coverage** | Percentage of vertices within valid distance range |

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
3. **Compute tooth masks** (if ROI template provided) - Dijkstra-based segmentation from seeds
4. **Apply pre-computed transforms** (if `--alignments` provided) - Load DentScanAlign results
5. **Alignment** - GPA or ICP against external reference
   - Uses **masked ICP** when tooth masks available (focuses on tooth surfaces)
6. **Compute distances** - CGAL AABB-tree signed distances to reference
7. **Compute trueness metrics** - RMS, MAD, Hausdorff, coverage
8. **Compute precision metrics** - Pairwise comparisons between repetitions
9. **Export QC data** - GPA means, transforms, segmented meshes

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
