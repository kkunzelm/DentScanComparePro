# DentScanComparePro User Manual

## Overview

DentScanComparePro is a tool for automated batch evaluation of dental intraoral scanner accuracy. It computes ISO 5725/12836-compliant trueness and precision metrics across multiple scanners and study groups (SKD levels in phantom studies, patient IDs in clinical cohort studies, or any other grouping label).

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

Create a JSON file describing your study. For a full field-by-field reference see
[docs/study-config-reference.md](study-config-reference.md).

```json
{
  "study": {
    "name": "Scanner_Comparison_2024",
    "version": 1,
    "reference_strategy": "gpa_mean",
    "scans_normalized": true,
    "alignment": {
      "icp_trim_fraction": 1.0
    }
  },
  "scanners": [
    {"id": "Primescan",   "patterns": ["*Primescan*"]},
    {"id": "Trios4",      "patterns": ["*Trios*4*"]},
    {"id": "iTeroLumina", "patterns": ["*iTero*", "*Lumina*"]}
  ],
  "groups": [
    {"id": "SKD_20", "condition_value": 20, "file_patterns": ["**/SKD_20/*.stl"]},
    {"id": "SKD_22", "condition_value": 22, "file_patterns": ["**/SKD_22/*.stl"]},
    {"id": "SKD_24", "condition_value": 24, "file_patterns": ["**/SKD_24/*.stl"]}
  ],
  "output": {
    "base_dir": "./results",
    "metrics_csv": "trueness_metrics.csv",
    "precision_csv": "precision_metrics.csv",
    "summary_csv": "summary_stats.csv"
  }
}
```

The `group.id` field is a free-form string label: use SKD values for phantom studies or patient IDs for clinical cohort studies. The optional `condition_value` integer stores a numeric parameter (e.g. depth in mm for phantom studies) and appears in QC JSON files; omit it or set to `0` for patient cohort studies where only the label matters. The software works with two factors as well as three — see the Study Config Reference for details.

### Step 3: Create an ROI Template (Optional but Recommended)

The ROI template defines which region of the scan to analyze. This is especially important for focusing on tooth surfaces and excluding gingiva.

#### Understanding the ROI Selection Model

The ROI (Region of Interest) is split into two parts that work differently:

**Geometric ROI (bounding box, plane slab, brush override zones) — applied to the REFERENCE:**

```
┌─────────────────────────────────────────────────────────────────┐
│  GEOMETRIC ROI COMPONENTS (combined with AND)                   │
│  ┌──────────────┐  ┌──────────────┐  ┌──────────────┐          │
│  │ Bounding Box │  │  Plane Slab  │  │ Brush Zones  │          │
│  │  (optional)  │──│  (optional)  │──│  (optional)  │          │
│  └──────────────┘  └──────────────┘  └──────────────┘          │
└─────────────────────────────────────────────────────────────────┘
                              │
                              ▼ applied ONCE to the reference mesh
┌─────────────────────────────────────────────────────────────────┐
│  ROI-MASKED REFERENCE (saved as <group>_roi.stl in qc/)         │
│  A trimmed submesh containing only faces inside the ROI region. │
│  Source scans (full mesh) align to this trimmed reference via   │
│  standard ICP — no source-side coordinate masking needed.       │
│  Source vertices >5 mm from the ROI are excluded from metrics.  │
└─────────────────────────────────────────────────────────────────┘
```

**Tooth segmentation (Base Selection) — applied per source scan:**

```
┌─────────────────────────────────────────────────────────────────┐
│  TOOTH SEGMENTATION (per-scan, requires curvature)              │
│  Geodesic expansion from seed points → tooth crown mask         │
│  Applied as an additional filter during METRIC COMPUTATION only │
│  (tooth masks cannot be transferred to the reference mesh)      │
└─────────────────────────────────────────────────────────────────┘
```

**Why the split?** The geometric ROI coordinates (bounding box corners, plane origin) are defined in the canonical reference frame. Applying them to the reference once is always correct. Applying the same absolute coordinates to each source scan used to fail whenever a scanner had a different coordinate system origin, causing the box or plane to miss most of the scan's vertices and ICP to diverge. The new approach is robust: source scans use their full geometry during alignment, and ICP naturally focuses on the reference ROI region because that is all the reference offers.

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

**Batch log — ROI template status messages:**

| Message | Meaning |
|---------|---------|
| `ROI template active: bbox, z-plane — masked ICP + restricted metrics` | One or more ROI components are active; masked ICP and restricted metrics will be used |
| `ROI template active: 14 tooth seeds — masked ICP + restricted metrics` | Tooth seeds are active; segmentation will run before alignment |
| `ROI template: no active components — full-mesh ICP and full-mesh metrics` | Template loaded but nothing is active; equivalent to no template |
| `Full-mesh mode: ENABLED (ROI template ignored, forceFullMesh=true)` | "Use ROI mask" checkbox is unchecked; template is completely ignored |

**When "Use ROI mask" is CHECKED (Masked ICP Mode):**

Masked ICP uses **all active ROI components** combined with AND logic. The same combined mask is applied to every alignment stage (GPA iterations, pre-aligned ICP, external-reference ICP) **and** to metric computation — the two are always consistent:

| Component | Active Toggle | Effect |
|-----------|--------------|--------|
| Bounding Box | "Active" checkbox | Only vertices inside box used for alignment and metrics |
| Plane Slab | "Active" checkbox | Only vertices in slab used for alignment and metrics |
| Manual Overrides | (always if present) | Include/exclude specific regions (green/red zones) |
| Base Selection (tooth seeds) | "Use Base Selection as ROI" | Only tooth crown vertices used for alignment and metrics |

You do **not** need tooth seeds to activate masked ICP. Enabling the Plane Slab or Bounding Box alone is sufficient to restrict both alignment and metric computation to that region.

**Note:** The Plane Slab is **inactive by default**. If you want to restrict analysis to the occlusal region, you must explicitly check the "Active" checkbox in the Plane Slab section.

**Output Directory Selection:**

| Masked ICP Checkbox | Masked ICP Output Dir | Result |
|---------------------|----------------------|--------|
| Checked | Specified | Results saved to Masked ICP Output |
| Checked | Empty | Results saved to Output Dir |
| Unchecked | Any | Results saved to Output Dir |

Note: When "Use ROI mask" is unchecked, all ROI settings are completely ignored for both alignment and metrics — full-mesh ICP and full-mesh metrics apply.

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

**TrICP for scans with soft-tissue coverage:**

Patient intraoral scans often cover gingiva, buccal mucosa, and soft palate in addition to the teeth. These soft-tissue surfaces deform between scan repetitions, so their ICP correspondences have large residuals that pull the rigid transform away from the correct tooth alignment. Trimmed ICP (TrICP) addresses this by discarding the worst-residual correspondences each iteration.

Set `icp_trim_fraction` in your study JSON or via `--trim-fraction` on the command line:

| Value | Effect | Recommended when |
|-------|--------|-----------------|
| `1.0` | No trimming (default) | Phantom studies with low soft-tissue coverage |
| `0.7` | Discard worst 30% | Scans with moderate gingival coverage |
| `0.5` | Discard worst 50% | Patient scans with extensive soft tissue (palate, buccal mucosa) |

In the GUI, the **"ICP trim fraction"** spinbox (0.10–1.00, step 0.05) is on the Study Configuration tab below the normalized/pre-aligned checkboxes. Its value is saved across sessions.

**ICP resolution hierarchy for large initial offsets (Xi-2025):**

When scans start with large offsets relative to each other — for example, different scanner coordinate frames, or a phantom scan with substantial tilt — standard ICP can converge to a local minimum because the basin of attraction is narrow at full resolution. The ICP resolution hierarchy (Xi-2025) solves this by running ICP on progressively finer versions of the mesh:

1. Decimate source to 5% of faces → run ICP to convergence
2. Apply that transform, decimate to 20% of faces → run ICP
3. Apply that transform, run ICP on the full-resolution mesh

The decimation uses curvature-weighted QEM (Garland-Heckbert quadric cost multiplied by 10 for concave regions — CEJ, developmental grooves, gingival crevice). This preserves tooth boundary triangles at coarse levels so the coarse alignment still locks onto clinically meaningful geometry.

Enable via study JSON, CLI flag, or GUI checkbox:

```json
"alignment": {
  "use_icp_hierarchy": true,
  "icp_hierarchy_levels": [0.05, 0.20, 1.0],
  "icp_hierarchy_neg_curv_k": 10.0
}
```

```bash
./DentScanComparePro --batch --study study.json ... --icp-hierarchy
```

GUI: check **"Use ICP resolution hierarchy (Xi-2025)"** on the Study Configuration tab.

| Scenario | Recommendation |
|----------|---------------|
| Well-pre-aligned scans (DentScanAlignPro output) | Hierarchy not needed — leave disabled |
| Large initial offsets or failed pre-alignment | Enable hierarchy |
| `icp_hierarchy_neg_curv_k` | Keep at `10.0` for dental scans; lower to `1.0` to disable curvature weighting |

Workflow B — Using raw STL files with JSON transforms:

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
| `trueness_metrics.csv` | Per-scan metrics, QC-filtered (errands excluded) |
| `trueness_metrics_all.csv` | Per-scan metrics, all scans including errands |
| `long_format_metrics.csv` | Per-scan metrics written directly by the batch runner |
| `precision_metrics.csv` | Per-scanner-per-group pairwise precision |
| `summary_stats.csv` | Aggregated RMS statistics by scanner and group |

Filenames for precision and summary are configured in the `output` section of your study JSON. The names above match the recommended convention.

See **Appendix A — Output CSV Reference** for a full description of every column in every file.

**QC Data (in `qc/` subdirectory):**

| Directory | Contents |
|-----------|----------|
| `qc/reference_meshes/<group>_reference.stl` | Full GPA mean or external reference mesh (one per group) |
| `qc/reference_meshes/<group>_roi.stl` | ROI-trimmed reference submesh used for ICP and distance computation (only when a geometric ROI is active). Load alongside aligned scans to verify the ROI lands on the intended region. |
| `qc/aligned_meshes/<scan>.stl` | Each scan's geometry after ICP, in the GPA frame. Open in any STL viewer to check that scans actually land on top of each other. |
| `qc/difference_meshes/<scan>.ply` | Same aligned geometry as binary PLY with a per-vertex `distance` float scalar (signed mm to reference). Open in MeshLab (*Render → Color → Per-Vertex Quality*) or ParaView to inspect the spatial distribution of errors. |
| `qc/transforms/<scan>.json` | Transform matrix + metrics (JSON per scan) |
| `qc/segmented/` | Tooth-only meshes (when Base Selection is used) |
| `qc/difference_images/` | Color-coded distance maps (PNG, generated in QC tab) |

### Step 6: Quality Control Review

Poor registrations corrupt your statistics silently: a scan that failed ICP alignment may appear in your CSV with an artificially high or low RMS value and skew both trueness and precision results. The QC workflow exists to detect these failures before you run your statistics, correct them using manually placed landmarks, and rebuild the final metric files from the corrected data.

Work through phases 6a through 6e in order. Phases 6d and 6e are only needed when you have flagged errands that require re-registration.

---

#### 6a — Load QC Data

1. Go to the **QC Review** tab.
2. Confirm that **Output Dir** in the Batch Configuration tab points to the same directory that was used for batch processing.
3. Click **Load QC Data from Results**.

The application scans the `qc/transforms/` subdirectory of your output directory and reads every `*.json` file it finds. Each JSON file encodes the 4×4 alignment matrix and all metric values that were computed for one scan during the batch run. From these files the application builds its scan registry in memory.

In parallel, it loads `qc/qc_status.json` if that file exists. This file persists the QC decisions (Pending / Accepted / Errand) from previous review sessions. If the file does not yet exist, every scan starts with **Pending** status.

After loading, the thumbnail gallery is populated — one card per scan — showing:
- The scan identifier (format: `ScannerName_SKDXX_rN`)
- The RMS distance value in millimetres
- A status icon (see Section 6c)
- The difference image PNG as the card background, or a grey placeholder if images have not been generated yet

The status bar above the gallery shows running counts: `Pending: N  |  Accepted: N  |  Errands: N`.

---

#### 6b — Generate Difference Images

Difference images are color-coded surface deviation maps rendered from a top-down (occlusal) camera position. Each pixel encodes how far the scan surface deviates from the GPA reference mesh at that point. They are the primary visual tool for detecting registration failures: a correctly aligned scan shows a symmetric, low-amplitude color pattern; a misaligned scan shows a strong directional gradient or systematic one-sided coloring.

Difference images are **not** generated during batch processing. VTK offscreen rendering is explicitly disabled in the batch processing thread because it is not reliable under a background thread. Instead, the batch run writes two files per scan to disk — the alignment matrix JSON and the reference mesh STL — and defers all rendering to this explicit step.

**To generate images:**

1. (Recommended) Check **Apply ROI template** if you want the images to reflect the analysis region actually used for metric computation. With this option:
   - Vertices that are **inside** the ROI are colored by their signed distance value on a diverging blue–white–red scale (±0.5 mm: blue = scan recessed, red = scan proud).
   - Vertices that are **outside** the ROI are rendered in dark grey, making the analysis boundary immediately visible.
   - The ROI template path must be set in the Batch Configuration tab. If it is empty or the file does not exist, the application will show a warning and abort.
2. Click **Generate Difference Images**.
3. A progress dialog shows the per-scan progress. Scans whose images are already up-to-date are skipped automatically (see **Smart Image Caching** further above). Only missing or stale images are regenerated.

After generation completes, the thumbnail gallery refreshes automatically and shows the new images.

**Note on the "Apply ROI" limitation:** The ROI mask applied during image generation covers the geometric ROI only — bounding box, plane slab, and brush override zones. The tooth segmentation base selection (which is scan-specific and was not saved to the transform JSON) is not re-applied. If your metric ROI relied primarily on the base selection, the grey/colored boundary in the images will not perfectly match the exact vertex set that contributed to the CSV numbers. All spatial constraints (box, slab, brush zones) are, however, correctly reflected.

---

#### 6c — Understanding the Thumbnail Gallery

Each thumbnail card shows the following visual indicators:

**Border color** (outside edge of the card):

| Border | Meaning |
|--------|---------|
| Grey | Pending — not yet reviewed |
| Green | Accepted — registration confirmed good |
| Red | Errand — registration failure, needs re-alignment |
| Yellow | Statistical outlier (RMS > mean + 2σ) — still Pending, but flagged for attention |
| Blue | Currently selected (click again to deselect) |

**Status icon** (small symbol in the bottom-left corner of each card):

| Icon | Meaning |
|------|---------|
| ○ | Pending |
| ✓ (green) | Accepted |
| ✗ (red) | Errand |

**Sorting and priority:** The gallery is sorted such that statistical outliers (yellow borders) appear first in each group, making it easy to start your review with the most suspicious scans. Within each group, cards are sorted alphabetically by scan ID.

---

#### 6d — Reviewing and Classifying Scans

**Single-scan detailed review:**

Double-click any thumbnail to open the alignment inspection dialog. This shows the scan mesh overlaid on the reference mesh, colored by signed distance. You can rotate the view and inspect the alignment from any angle. Two buttons at the bottom of the dialog let you:
- **Accept** — mark this scan as good; the thumbnail immediately shows a green ✓ and green border
- **Flag as Errand** — mark this scan as a registration failure; the thumbnail immediately shows a red ✗ and red border

**Selecting multiple scans:**

Single-click any thumbnail to select it (blue border). Click again to deselect. You can build up a selection of any number of scans this way. Selections are used as input for the **Flag as Errand** and **Re-align Selected** buttons.

**Bulk acceptance:**

Click **Accept All Visible** to mark every currently visible pending scan as Accepted in one step. This is appropriate when the thumbnail review and the difference images confirm that all alignments are reasonable and you do not want to inspect each scan individually.

**Flagging failures:**

Select one or more thumbnails (single-click), then click **Flag as Errand**. The selected scans are immediately marked with ✗ and red borders. The errand count in the status bar updates accordingly.

**Saving QC status manually:**

Click **Save QC Status** at any point to persist the current status of all scans to disk. The application writes:
- `qc/qc_status.json` — complete status record for every scan (status, review time, RMS values, whether resolved)
- `qc/errands.json` — compact list of scans that are currently in Errand status (unresolved)

You do **not** need to save manually after re-registration — the application saves automatically when you accept a re-alignment result (see Section 6e). Manual saving is mainly useful when you have accepted or flagged scans interactively without going through the re-registration dialog.

---

#### 6e — Re-registering Failed Scans (Errand Resolution)

Scans flagged as errands can be re-registered interactively using manually placed landmark point pairs. This is the recommended approach whenever the automatic ICP algorithm failed because the scan was too far from the reference in its initial position (e.g., a different scanner coordinate system origin, a large jaw-opening angle difference, or a grossly misaligned starting pose).

**Selecting scans for re-registration:**

You can re-register one scan at a time or process a batch of errands in sequence:
- To re-register a **single scan**: click its thumbnail once (blue border) and then click **Re-align Selected**.
- To re-register **all errands at once**: click each errand thumbnail to select them (or use any multi-select approach), then click **Re-align Selected**. The application opens the resolution dialog for each scan in sequence and waits for you to complete each one before moving to the next.

**The Errand Resolution Dialog:**

The dialog shows three panels side-by-side:

| Panel | Content | Purpose |
|-------|---------|---------|
| Left | GPA reference mesh (the group's mean surface) | The registration target — pick landmarks here |
| Middle | The failing scan in its original (un-aligned) coordinate frame | The scan to be re-aligned — pick matching landmarks here |
| Right | Color-coded distance map | Live feedback after each alignment attempt |

All three panels support full 3-D interaction: left-drag to rotate, right-drag or scroll to zoom, middle-drag to pan.

**Step-by-step re-registration:**

1. **Pick corresponding landmark pairs.** Click on a distinctive anatomical point in the **left panel** (reference mesh). A yellow sphere with a number label appears. Then click the **same anatomical location** on the **middle panel** (scan). The same number label appears, completing one pair. Repeat until you have at least **3 pairs**. Five to eight pairs spread across the arch typically give a robust result.

   Good landmark choices:
   - Cusp tips (unambiguous 3-D position)
   - Fossa centers
   - Incisal edges
   - Well-defined ridge peaks

   Avoid:
   - Gingival margins (deform between scans)
   - Flat, featureless surfaces (ambiguous in-plane position)
   - Scan borders (often incomplete or noisy)

2. Click **Compute Alignment**. The application runs the Kabsch SVD algorithm on your landmark pairs, computes the optimal rigid transform, applies it to the scan mesh, and immediately displays the resulting distance map in the right panel along with the new RMS value.

3. Inspect the right panel. A successful re-alignment shows a low-amplitude, approximately symmetric color pattern (mostly white/light blue or light red, with no strong directional gradient). If the result looks poor, click **Undo Last Pair** to remove the most recent pair, or **Clear All Pairs** to start over, then try different landmark positions.

4. (Optional but recommended) Click **Run ICP** to refine the landmark-based coarse alignment with point-to-plane ICP (100 iterations). This step is especially helpful when landmarks were placed with less precision than ideal, or when the scan has a systematic small rotational offset remaining after landmark alignment. The right panel updates automatically with the refined result.

5. If the alignment is satisfactory, click **Accept Result**.

6. If the alignment is still wrong after trying ICP refinement, click **Clear All Pairs**, choose different landmark locations (preferably more spread out across the arch), and try again from step 1.

**What happens automatically when you click Accept Result:**

The following actions happen immediately and without any further user interaction:

1. **Transform JSON overwritten.** The corrected 4×4 transform matrix and all recomputed metrics (RMS, MAD, H100, H95, Bias, Coverage) are written to `qc/transforms/<scanId>.json`, replacing the original failed registration data. This is the authoritative record for subsequent metric rebuilds.

2. **QC status updated to Accepted.** The scan's status changes from Errand to **Accepted** in the in-memory ErrandManager. The thumbnail immediately shows a green ✓ icon and a green border when the gallery refreshes.

3. **QC status files saved to disk automatically.** `qc/qc_status.json` and `qc/errands.json` are rewritten immediately. You do not need to click **Save QC Status** manually. The resolved scan is no longer listed in `errands.json`; it appears in `qc_status.json` with `"status": "accepted"`, `"resolved": true`, the new RMS value, and the correction method (`"landmark"`).

4. **Difference image regenerated.** The application immediately re-runs the full rendering pipeline for this scan — reloads the STL, applies the new transform, rebuilds the AABB reference tree, recomputes per-vertex distances, and renders a new PNG file. The old (bad) image is overwritten. If **Apply ROI template** is checked, the ROI mask is applied to the new image as well. The thumbnail gallery refreshes to show the updated image.

The status bar shows a message of the form: `Scan XYZ re-registered. New RMS: 0.042 mm. Run 'Rebuild Metrics from Transforms' to update CSVs. Difference image updated.`

**If processing a batch of errands:** After you accept (or cancel) the dialog for one scan, the dialog opens automatically for the next selected scan. The status bar tracks progress (`Re-aligning XYZ (2 of 5)...`). You can cancel the entire batch at any point by simply closing the dialog.

**Landmark tips summary:**

| Tip | Reason |
|-----|--------|
| Use cusp tips and distinct occlusal features | Unambiguous 3-D correspondence |
| Spread landmarks across the full arch | Prevents rotation drift in under-constrained areas |
| Avoid gingival margins | Soft tissue deforms between scans |
| 5–8 pairs is the practical optimum | Below 3 gives poor geometry; above 8 yields diminishing returns |
| Use ICP refinement after coarse landmark alignment | Corrects small residual misalignment from imprecise picks |

---

#### 6f — Rebuild All Metric CSVs

After resolving errands, the CSV files in your output directory still contain the original metric values from the batch run — including the bad values from failed registrations. You must explicitly trigger a rebuild to propagate the corrected numbers into the statistics files.

Click **Rebuild Metrics from Transforms**.

This button re-derives all output CSVs from scratch using only the information stored in the `qc/transforms/*.json` files, applying the current QC status (scans in Errand status are excluded from the filtered output).

**What the rebuild does, phase by phase:**

| Phase | Operation | Speed |
|-------|-----------|-------|
| Parse all JSONs | Reads every `qc/transforms/*.json` and extracts metrics + transform matrices | Near-instant |
| Write trueness CSVs | Aggregates per-scan metrics from the JSON data. No STL files are reloaded for this phase. | Fast |
| Recompute precision | Reloads original STL files, applies stored transforms, builds AABB reference trees per group, and recomputes all pairwise inter-scan RMS distances within each scanner×group cell | Slow — same computational cost as the original batch run |
| Write summary CSV | Computes scanner×group mean, SD, min, max from the trueness results | Fast |

**Files overwritten:**

| File | Contents after rebuild |
|------|----------------------|
| `long_format_metrics.csv` | All scans, all metrics (no QC filter) |
| `trueness_metrics_all.csv` | All scans including errands (pre-QC view) |
| `trueness_metrics.csv` | Only QC-accepted scans — errands excluded |
| `precision_metrics.csv` | Pairwise precision per scanner×group, errands excluded from all pair computations |
| `summary_stats.csv` | Mean/SD/Min/Max RMS per scanner×group, errands excluded |

**Important notes:**

- Scans whose QC status is **Errand** (not resolved) are excluded from `trueness_metrics.csv` and from every precision pair computation. A scan that was re-registered and accepted contributes its **corrected** metrics.
- Precision recomputation uses the geometric ROI (bounding box, plane slab, brush override zones) from the currently loaded study config. The tooth segmentation base selection is not stored in the transform JSON files and therefore cannot be re-applied during rebuild; if your ROI relies heavily on the base selection, pairwise precision values may differ very slightly from the original batch.
- You only need to run this rebuild **once** after resolving all your errands, not after every individual re-registration. It is safe to run it multiple times — the output is always fully regenerated from the JSON files.
- If your study config is not loaded in the current session, the precision rebuild uses full-mesh distances (no geometric ROI). Load the study config first for correct ROI-filtered precision values.

---

### Understanding How Difference Images Are Generated

#### The Two-Phase Pipeline

It is important to understand that difference image generation is architecturally separated from batch metric computation. These are two independent pipelines that run at different times and are connected only through files written to disk.

**Phase 1 — Batch processing (metric computation):**

During batch processing, the application computes all alignments and metrics entirely in memory. VTK rendering is explicitly disabled in this phase because offscreen VTK rendering is not reliable in a background thread. No difference images are produced during the batch run. What is written to disk is:

- The full GPA mean reference mesh per group (`qc/reference_meshes/{groupId}_reference.stl`)
- The ROI-trimmed reference submesh per group (`qc/reference_meshes/{groupId}_roi.stl`, only when a geometric ROI is active)
- Each aligned scan as STL (`qc/aligned_meshes/{scanId}.stl`)
- Each aligned scan as PLY with per-vertex signed distance scalar (`qc/difference_meshes/{scanId}.ply`)
- A JSON file per scan (`qc/transforms/{scanId}.json`) containing the 4×4 alignment matrix and the final metrics

Crucially, the per-vertex distance arrays (which would be needed to color the mesh) are **not** saved to disk. They are computed and used for statistics, then discarded.

**Phase 2 — Difference image generation (QC tab):**

When you click **Generate Difference Images** in the QC Review tab, a completely separate rendering pipeline is executed. For each scan, it:

1. Reloads the original STL file from disk
2. Reads the saved 4×4 alignment transform from the corresponding JSON file
3. Applies that transform to the mesh vertices in memory
4. Rebuilds the AABB reference tree from the saved reference mesh
5. Recomputes per-vertex signed distances from scratch
6. Renders the colored mesh to a PNG file using VTK offscreen rendering

This means the correct workflow is always: **run batch processing first, then go to the QC tab to generate difference images.** The QC tab will not work unless batch processing has previously completed and written its QC data to disk.

#### Why Difference Images Previously Showed the Full Mesh

Because Phase 2 recomputes distances independently, it had no knowledge of the ROI template that was used during batch processing. The distances were computed for every vertex of every scan, and the resulting color map covered the entire mesh surface — including gingiva, scan borders, and other regions that were explicitly excluded from the metric calculations.

This created a misleading situation: the CSV metrics correctly reflected only the tooth surfaces included in the ROI, but the difference images visually showed the full scan, making it appear as if larger or noisier regions were contributing to the numbers.

#### The "Apply ROI Template" Checkbox

To close this gap, the QC tab now includes an **"Apply ROI template"** checkbox next to the Generate button.

**When unchecked (default):** Behavior is unchanged from before. Distances are computed on the full mesh and every vertex is colored by its signed distance to the reference. Existing images are skipped (not regenerated). This mode is useful for a quick visual check of the overall scan alignment.

**When checked:** The application loads the ROI template currently set in the Batch Configuration tab and builds the same per-vertex inclusion mask that the batch processor used when computing metrics. Vertices that fall inside the ROI are colored normally by their signed distance value (blue–white–red diverging scale). Vertices that fall outside the ROI are rendered in dark grey, making the boundary of the analysis region immediately visible.

**Important:** The ROI mask applied here is the geometric part of the ROI only — bounding box, plane slab, and brush override zones. The tooth segmentation base selection (which is scan-specific and not stored in the transform JSON) is not re-applied in this phase. If your metric ROI relied primarily on the base selection, the grey-vs-colored boundary in the images will not perfectly match the metric boundary, but it will still correctly reflect all spatial constraints (box, slab, brush zones).

**Prerequisite:** The ROI template file path must be set in the **Batch Configuration** tab (the same path used during the batch run). If the field is empty or the file does not exist, the button will show a warning and abort.

#### Smart Image Caching

Generating difference images is slow because each scan requires a VTK offscreen render pass. The application avoids redundant work using a small sidecar file (`<scanId>.meta`) stored next to each PNG in `qc/difference_images/`.

| Condition | Action |
|-----------|--------|
| Image missing | Always generate |
| Image exists, "Apply ROI" unchecked | Skip (image is from a previous run without ROI) |
| Image exists, "Apply ROI" checked, same ROI template file and same file modification time | Skip (ROI already applied correctly) |
| Image exists, "Apply ROI" checked, ROI template changed | Regenerate and update the sidecar |

This means re-clicking **Generate Difference Images** after a previous successful run is essentially free — only images that are genuinely out-of-date (or missing) are regenerated. If you modify your ROI template file and re-generate, only scans whose images reflect the old template are re-rendered.

#### Summary: What the ROI Affects and Where

| Pipeline stage | ROI template applied | Notes |
|---|---|---|
| ICP alignment (batch) | Yes — when "Use ROI mask" is checked | Only ROI vertices guide the alignment |
| Distance computation (batch) | No | Distances computed for all vertices always |
| Trueness metrics (batch) | Yes | Only ROI vertices contribute to RMS, MAD, H95, etc. |
| Precision metrics (batch) | Yes | Only ROI vertices contribute to pairwise RMS |
| Distance computation (QC image generation) | No | Distances recomputed for all vertices |
| Difference image coloring (QC) | Only when "Apply ROI template" is checked | Out-of-ROI vertices shown in grey |

The key takeaway is that the CSV metrics are always computed on the ROI-filtered vertex set, regardless of how the images look. The images are a visualization aid, not the source of the numbers. Checking "Apply ROI template" makes the images consistent with the metric computation and is the recommended setting when presenting results or checking whether the ROI was applied as intended.

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
  --pre-aligned            Skip GPA; run one ICP refinement pass then compute mean mesh
  --normalized             Scans are normalized; skip JSON transform loading (default in GUI)
  --trim-fraction <f>      TrICP: keep only this fraction of correspondences per iteration
                           sorted by point-to-plane residual (1.0 = disabled; 0.5 = 50%).
                           Overrides icp_trim_fraction in study JSON.
  --icp-hierarchy          Enable coarse-to-fine ICP hierarchy (Xi-2025): decimates source
                           at 5%/20%/100% of faces using curvature-weighted QEM, seeds each
                           level with the previous transform. Overrides use_icp_hierarchy in
                           study JSON.
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
| **MaxAspRatio** | `MaxAspRatio` | Maximum aspect ratio across all triangles. Reveals extreme outlier triangles at boundaries or holes |
| **ATI** | `ATI` | Adaptive Tessellation Index. Spearman correlation between curvature and 1/area. +1 = ideal adaptive, 0 = uniform |
| **DensHighκ** | `DensHighK` | Triangle density in high-curvature zones (triangles/mm²) |
| **DensLowκ** | `DensLowK` | Triangle density in low-curvature zones (triangles/mm²) |

### Accuracy Metrics (per scan, after registration)

> **`res=` in batch output vs `RMS_mm` in CSV**
> During batch processing, each scan's progress line shows `res=X.XXXX mm`. This is the ICP alignment residual — the RMS of the point-to-plane distances `|n · (sp − qp)|` across all correspondences at the last ICP iteration. It measures how tightly the ICP converged, not scan accuracy.
> `RMS_mm` in the CSV is computed afterwards by `DistanceField`: the RMS of 3D Euclidean distances from every scan vertex to the GPA mean reference mesh. These are different quantities and will differ in magnitude.

| Metric | CSV Column | Description |
|--------|------------|-------------|
| **RMS** | `RMS_mm` | Root Mean Square 3D distance from each scan vertex to the GPA mean reference (mm). Primary trueness metric. Computed by DistanceField after alignment — distinct from the ICP residual shown in the batch log. |
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

### Precision Metrics (per scanner per group)

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
2. **Compute curvature and tessellation metrics** - CGAL interpolated curvatures (mean/Gaussian), ATI, edge length, aspect ratio, curvature densities.
   **Skipped** when `--pre-aligned` is active and no ROI tooth mask is configured. DentScanAlignPro has already resolved canonical orientation, making curvature redundant for alignment; skipping saves significant time per group.
3. **Compute Base Selection** (if ROI template provided) - Dijkstra-based segmentation from seeds (requires curvature — not skipped in this case)
4. **Apply pre-computed transforms** (if `--alignments` provided) - Load DentScanAlign results
5. **Alignment**:
   - **GPA** (default): PCA coarse → 4-orientation test → iterative ICP → mean mesh update
   - **Pre-aligned ICP** (`--pre-aligned`, no external ref): one ICP pass per scan against scan with most triangles → mean mesh update. GPA iterations skipped.
   - **ICP against external reference** (`--external-ref`): one ICP pass per scan against provided STL
   - When a geometric ROI is active: the reference is first trimmed to the ROI region (`<group>_roi.stl`); source scans (full mesh) then align to this trimmed reference via standard ICP. Tooth segmentation masks additionally filter metric computation per scan.
   - **TrICP** (`icp_trim_fraction < 1.0`): each ICP iteration discards the worst-residual fraction of correspondences before the rigid solve. Keeps stable surfaces (teeth) dominant; suppresses influence of deforming soft tissue.
   - **ICP hierarchy** (`use_icp_hierarchy`): coarse-to-fine ICP at 5%/20%/100% face decimation. Each coarse level runs to convergence and seeds the next finer level — avoids local minima from large initial offsets.
6. **Compute distances** - CGAL AABB-tree signed distances to the ROI-masked (or full) reference
7. **Compute trueness metrics** - RMS, MAD, Hausdorff, coverage, completeness. Source vertices more than 5 mm from the masked reference are excluded when a geometric ROI is active.
8. **Compute precision metrics** - Pairwise comparisons between repetitions (skipped when `compute_precision: false`)
9. **Export QC data** - GPA means, ROI reference, aligned STLs, difference PLYs, transforms, segmented meshes

---

## Tips and Best Practices

### For Best Results

1. **Use tooth segmentation** - Gingiva deforms between scans; teeth are rigid
2. **Set TrICP trim fraction for patient scans** - Set `icp_trim_fraction: 0.5` in your study JSON when scans include extensive soft tissue (palate, buccal mucosa). Leave at `1.0` for phantom studies.
3. **Enable ICP hierarchy when scans have large offsets** - If scans start far from a common frame (e.g. scanner-native coordinate systems, or large phantom tilts), enable `use_icp_hierarchy: true`. Not needed when DentScanAlignPro pre-alignment was used.
4. **Check statistical outliers** - Yellow-bordered scans may have registration failures
5. **Use external reference for trueness** - CAD or lab scanner provides ground truth
6. **Review before statistics** - QC catches registration failures before they corrupt data

### Common Issues

| Problem | Solution |
|---------|----------|
| High RMS values | Check if registration failed; use QC review |
| RMS in mm range for patient scans | Soft-tissue deformation pulling ICP — set `icp_trim_fraction: 0.5` |
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
│  ┌──────────┐    ┌──────────┐    ┌──────────┐    ┌──────────┐      │
│  │  Review  │───▶│ Re-reg.  │───▶│ Overwrite│───▶│ Rebuild  │      │
│  │Thumbnails│    │ Errands  │    │   JSON   │    │  Metrics │      │
│  └──────────┘    └──────────┘    └──────────┘    └──────────┘      │
│  (flag bad)    (landmarks+ICP)   (automatic)    (button click)     │
└─────────────────────────────────────────────────────────────────────┘
```

---

## Support

For issues and feature requests, contact:

**Prof. Dr. Karl-Heinz Kunzelmann**
Website: [www.kunzelmann.de](https://www.kunzelmann.de)

---

DentScanComparePro v1.0

---

## Appendix A — Output CSV Reference

This appendix documents every CSV file that DentScanComparePro writes to the output directory, what data each file contains, and when it is generated or overwritten.

All CSV files are written with a UTF-8 BOM so that they open correctly in Microsoft Excel on Windows without any import dialog.

---

### `trueness_metrics.csv`

**When written:** After batch processing (by the batch runner) and after every **Rebuild Metrics from Transforms** operation.

**What it contains:** One row per scan, **QC-filtered** — scans whose QC status is **Errand** (unresolved registration failure) are excluded. This is the primary input file for statistical analysis. If no QC review has been performed yet (all scans are Pending), all scans are included.

**Columns:**

| Column | Unit | Description |
|--------|------|-------------|
| `Observation_ID` | — | Sequential integer row counter, starting at 1 |
| `Scanner_Model` | — | Scanner name as defined in the study configuration |
| `Group_ID` | — | Group identifier (SKD level, patient ID, or any other label defined in the study config) |
| `Repetition_ID` | — | Repetition number within the Scanner × Group cell |
| `Triangles` | — | Number of triangular faces in the scan mesh |
| `Edge_mm` | mm | Mean edge length across all triangles (mesh resolution proxy — smaller = finer) |
| `AspRatio` | — | Mean triangle aspect ratio (longest / shortest edge). 1.0 = equilateral triangle, the ideal |
| `MaxAspRatio` | — | Maximum triangle aspect ratio across the entire mesh. Reveals extreme outlier triangles at scan boundaries or topological holes |
| `ATI` | — | Adaptive Tessellation Index. Spearman correlation between local curvature and triangle density (1/area). +1.0 = perfectly adaptive mesh; 0 = uniform tessellation regardless of curvature |
| `DensHighK` | triangles/mm² | Triangle density measured in high-curvature zones (cusp tips, ridges). Higher = finer detail where it matters |
| `DensLowK` | triangles/mm² | Triangle density measured in low-curvature zones (flat surfaces). Should be lower than DensHighK for an adaptive mesh |
| `RMS_mm` | mm | Root Mean Square distance from scan surface to GPA reference surface, computed over all vertices inside the ROI. **This is the primary trueness metric** |
| `MAD_mm` | mm | Median Absolute Deviation. Robust alternative to RMS — less sensitive to extreme outliers at scan borders |
| `H100_mm` | mm | Maximum distance (100th-percentile Hausdorff). Dominated by scan boundary artifacts; use H95 for clinical interpretation |
| `H95_mm` | mm | 95th-percentile Hausdorff distance. Clinically meaningful: 95% of the scan surface lies within this distance of the reference |
| `Bias_mm` | mm | Signed mean distance. Positive = scan surface is proud of (outside) the reference on average; negative = scan is inside the reference. Reflects systematic dimensional error |
| `Coverage_pct` | % | Percentage of reference surface vertices that have a corresponding scan vertex within 0.2 mm. Values below ~90% indicate incomplete scans |
| `Boundary_mm` | mm | Total length of open boundary edges in the mesh. High values indicate scan borders or tears |
| `Holes` | — | Number of open boundary loops (topological holes). 0 = closed mesh, >0 = incomplete regions |
| `Stitch_deg` | ° | Maximum normal discontinuity angle at mesh stitching seams. High values (>30°) indicate stitching artifacts from the scanner software |
| `Vertices_Included` | — | Number of mesh vertices that were inside the ROI and contributed to the metric computation |
| `Vertices_Total` | — | Total number of vertices in the scan mesh |
| `File_Path` | — | Absolute path to the source STL file on disk |

---

### `trueness_metrics_all.csv`

**When written:** After batch processing and after every **Rebuild Metrics from Transforms** operation.

**What it contains:** Identical structure and columns to `trueness_metrics.csv`. The only difference is that **all scans are included** — errands are not excluded. This file provides a complete pre-QC picture of all scan metrics.

Use this file to:
- Compare a scan's original (failed) metrics against its corrected metrics after re-registration
- Audit which scans were flagged as errands and why
- Check that the QC filter is behaving as expected (the rows in this file that are absent from `trueness_metrics.csv` are exactly the current errands)

---

### `long_format_metrics.csv`

**When written:** Directly by the batch runner at the end of each processing run, before any QC review. Not regenerated by **Rebuild Metrics from Transforms**.

**What it contains:** Same structure and columns as the trueness files above. This file is the raw output of the batch run, written before any QC status is applied. It contains all scans from that specific batch run, with no filtering.

In practice, once you have run QC review and rebuilding, `trueness_metrics.csv` and `trueness_metrics_all.csv` supersede this file for analysis purposes. You can think of `long_format_metrics.csv` as a batch-run receipt.

---

### `precision_metrics.csv`

(Filename is configured via `precision_csv` in the `output` section of your study JSON.)

**When written:** After batch processing (by the batch runner) and after every **Rebuild Metrics from Transforms** operation.

**What it contains:** One row per Scanner × Group cell. Each row summarises the **within-cell pairwise precision** — how consistent the same scanner is across repeated measurements under the same conditions. Precision is computed as the mean of all pairwise RMS distances between the N repetitions in the cell. For N = 5 repetitions, this involves 10 unique scan pairs.

Scans in Errand status are excluded from all pair computations. If an errand is unresolved in a cell with 5 repetitions, that cell has only 4 valid scans and therefore only 6 pairs; the Pairwise_Count column reflects this.

**Columns:**

| Column | Unit | Description |
|--------|------|-------------|
| `Scanner_Model` | — | Scanner name |
| `Group_ID` | — | Group identifier (SKD level, patient ID, or any label from the study config) |
| `Precision_MeanRMS_mm` | mm | Mean of all pairwise RMS distances within this cell. **The primary precision metric** (corresponds to ISO 5725 repeatability standard deviation when computed on repeated measurements) |
| `Precision_SD_mm` | mm | Standard deviation of the pairwise RMS values. Measures how variable the pairwise distances are — high SD suggests inconsistent scan quality |
| `Coefficient_of_Variation` | — | SD / Mean (dimensionless). Allows precision comparison across scanners and groups on a relative scale |
| `Pairwise_Count` | — | Number of scan pairs used in the computation. For N repetitions: N×(N−1)/2 pairs |

---

### `summary_stats.csv`

(Filename is configured via `summary_csv` in the `output` section of your study JSON.)

**When written:** After batch processing and after every **Rebuild Metrics from Transforms** operation.

**What it contains:** One row per Scanner × Group cell, summarising the RMS trueness distribution across all accepted repetitions in that cell. This is the highest-level aggregated view of trueness results — one number per experimental condition.

Errands are excluded (same filter as `trueness_metrics.csv`).

**Columns:**

| Column | Unit | Description |
|--------|------|-------------|
| `Scanner_Model` | — | Scanner name |
| `Group_ID` | — | Group identifier (SKD level, patient ID, or any label from the study config) |
| `N` | — | Number of accepted scans in this cell. Should equal the number of repetitions in a balanced design; fewer if errands were not resolved |
| `Mean_RMS_mm` | mm | Arithmetic mean of per-scan RMS values across repetitions |
| `SD_RMS_mm` | mm | Sample standard deviation of per-scan RMS values (denominator N−1) |
| `Min_RMS_mm` | mm | Minimum per-scan RMS in this cell |
| `Max_RMS_mm` | mm | Maximum per-scan RMS in this cell |

---

### Which file to use for what

| Analysis task | Recommended file |
|---------------|-----------------|
| Per-scan statistical model (main analysis) | `trueness_metrics.csv` |
| Checking what errands were excluded | `trueness_metrics_all.csv` |
| Precision / repeatability analysis | `precision_metrics.csv` |
| Quick descriptive table for a paper | `summary_stats.csv` |
| R analysis script (`analyze_results_Nold.R`) | reads `trueness_metrics.csv`, `precision_metrics.csv`, `summary_stats.csv` |

---

### QC sidecar files (not CSV, but referenced here for completeness)

These files are written to the `qc/` subdirectory of your output directory and drive the QC Review tab. They are not inputs to the R analysis script.

| File | Description |
|------|-------------|
| `qc/qc_status.json` | Complete QC status for every scan: status (pending/accepted/errand), review timestamp, RMS values, whether resolved, correction method. This file is updated automatically when you accept a re-registration result or save QC status manually. |
| `qc/errands.json` | Compact list of scans that are currently in Errand status. Updated automatically after each re-registration. Useful for a quick audit of which scans need attention. |
| `qc/transforms/<scanId>.json` | One file per scan. Contains the 4×4 alignment transform matrix and the full set of trueness metrics (RMS, MAD, Hausdorff, Bias, Coverage, etc.). This is the source of truth for **Rebuild Metrics from Transforms**. Overwritten when a re-registration is accepted. |
| `qc/reference_meshes/<groupId>_reference.stl` | Full GPA mean or external reference mesh for each group. Used as the registration target in the Errand Resolution Dialog. |
| `qc/reference_meshes/<groupId>_roi.stl` | ROI-trimmed submesh of the reference (only written when a geometric ROI is active). This is the actual ICP target and distance reference used during batch processing. Load it alongside `aligned_meshes/` STLs to verify that the ROI covers the intended region. |
| `qc/aligned_meshes/<scanId>.stl` | Aligned scan geometry (after ICP, in GPA frame). Open in any STL viewer to check that all scans converged to the same position. |
| `qc/difference_meshes/<scanId>.ply` | Aligned scan geometry as binary PLY with a per-vertex `distance` float property (signed mm to reference). Open in MeshLab (*Render → Color → Per-Vertex Quality*) or ParaView to inspect where the scan deviates from the reference. |
| `qc/difference_images/<scanId>.png` | Color-coded distance map (800×800 px, occlusal view). Generated by **Generate Difference Images** and automatically overwritten after each accepted re-registration. |
| `qc/difference_images/<scanId>.meta` | Small JSON sidecar storing the ROI template path and modification time at the time the corresponding PNG was generated. Used by the smart image cache to decide whether an image is still valid. |
