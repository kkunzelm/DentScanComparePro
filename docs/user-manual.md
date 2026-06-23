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
    "name": "Scanner_Comparison_2026",
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

**Geometric ROI (bounding box, plane slab, brush override zones) — applied to the REFERENCE and to aligned source scans:**

```
┌─────────────────────────────────────────────────────────────────┐
│  GEOMETRIC ROI COMPONENTS (combined with AND)                   │
│  ┌──────────────┐  ┌──────────────┐  ┌──────────────┐          │
│  │ Bounding Box │  │  Plane Slab  │  │ Brush Zones  │          │
│  │  (optional)  │──│  (optional)  │──│  (optional)  │          │
│  └──────────────┘  └──────────────┘  └──────────────┘          │
└─────────────────────────────────────────────────────────────────┘
                              │
                  ┌───────────┴────────────┐
                  ▼                        ▼
   Applied ONCE to the              Applied to each source
   reference mesh                   scan AFTER ICP alignment
   → ROI-masked reference           → source-side vertex mask
   (saved as <group>_roi.stl)
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

#### How distances and metrics are computed

Distances are computed for **every vertex of the full scan surface** — not only the ROI region. Each scan vertex receives its distance to the nearest point on the ROI-masked reference surface. This means a gingival vertex gets a distance too: it will be the distance from that vertex to the nearest point on the crown boundary of the masked reference.

During metric computation, three filters are applied in sequence. A vertex must pass **all three** to contribute to RMS, MAD, Hausdorff, and the other numbers in the CSV:

| Filter | What it does | Why it is needed |
|--------|-------------|-----------------|
| **1. Source-side geometric ROI** | The bounding box, plane slab, and brush zones from the ROI template are re-applied directly to the aligned scan vertex positions. Vertices outside the ROI are excluded. | Catches gingival vertices that are geometrically outside the defined region, even when their distance to the reference boundary happens to be small (1–3 mm). This is the primary filter for soft tissue exclusion. |
| **2. Distance guard (5 mm)** | Any vertex whose absolute distance to the masked reference exceeds 5 mm is excluded. | Belt-and-suspenders check for vertices that somehow passed filter 1 but are clearly far from the analysis region. |
| **3. Tooth segmentation mask** | If tooth seeds were placed and segmentation was run, only vertices identified as crown surfaces pass. | Fine-grained exclusion of inter-proximal and cervical soft tissue that the geometric ROI did not cleanly cut off. |

**What the difference PLY files show:** In the mask STL path, the PLY files written to `qc/difference_meshes/` contain **only the mask mesh geometry** (the ROI-trimmed reference surface), colored by the distance from each reference surface point to the nearest point on the scan. This is identical to the `maskDistances` values that feed the trueness metrics: positive = scan surface is proud (outside the reference), negative = scan is recessed. There is no outside-mask geometry in the file at all — no boundary band, no misleading colors. Open these files in MeshLab (*Render → Color → Per-Vertex Quality*) to inspect the spatial distribution of trueness errors within the analysis region. In the full-mesh or ROI-trimmed-reference path (no mask STL), the PLY contains the full aligned scan geometry colored per scan vertex.

**Why the split between reference-side and source-side masking?** The geometric ROI coordinates (bounding box corners, plane origin) are defined in the reference coordinate frame. Applying them to the reference once is always correct. Applying the same coordinates to a source scan that has not yet been aligned would fail if the scanner uses a different origin — the box would miss the scan entirely. After ICP alignment, the source scan is in the reference coordinate frame, so the ROI coordinates apply correctly to its vertices. The software therefore applies the ROI to the reference first (for ICP alignment focus) and re-applies it to each source scan after alignment (for metric filtering).

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

### Step 3b: Per-Patient ROI for Clinical Studies

---

#### Why every patient needs their own ROI

When you scan real patients — rather than a standardised phantom — each person's scan looks different:

- The **height of the occlusal plane** varies: one patient's teeth may sit 5 mm higher in the image than another's.
- The **jaw orientation** varies: the arch may be tilted to the left, right, forward, or backward depending on how the patient held their head during scanning.
- The **amount of soft tissue** in the scan varies: some patients show extensive gingiva, buccal mucosa, or palatal tissue; others show mainly teeth.

Because of these differences, a single ROI mask defined on one patient's reference scan will **not transfer correctly** to another patient's scan. If you try to use a shared mask, it will cut off the wrong part of the mesh — either slicing through the teeth of one patient or leaving gingiva included for another.

The solution is to define a **separate ROI mask for every patient group**. Once defined and saved, the software automatically applies the correct mask for each patient when the batch runs — you do not need to do anything extra during the batch.

---

#### What an ROI mask does (plain language)

Think of the ROI mask as a **stencil** that you place over the patient's reference mesh. Everything inside the stencil — typically the tooth surfaces — is included in the analysis. Everything outside the stencil — gingiva, scan borders, soft tissue — is ignored during alignment and metric computation.

The stencil is defined interactively on the **reference mesh** of each patient (which is the average surface computed from all scanners during the first batch run). Once you have drawn the stencil, the software saves it as a small JSON file alongside an STL file showing which triangles are included. During the actual batch run, the software loads this stencil for each patient and applies it automatically.

---

#### Two-step process overview

```
Step 0  Set ROI Masks Dir in the Study Configuration tab
        → A permanent directory for all per-patient mask files

Step A  Run batch once WITHOUT any ROI mask
        → Software creates reference_meshes/ folder with one STL per patient

Step B  For each patient (one at a time):
        → Load that patient's reference STL in the ROI Template Editor
        → Draw the ROI mask (exclude gingiva, include teeth)
        → Save the template
          (mask STL written to {maskStlDir}/{groupId}_roi_mask.stl automatically)

Step C  Run batch again WITH "Use ROI mask" checked
        → BatchRunner auto-discovers {groupId}_roi_mask.stl for each patient
        → Each patient's own mask is applied automatically
```

You only need to do Steps 0, A, and B once. After all per-patient masks are saved to the masks directory, you can run Step C (and re-run it if needed) as many times as you like.

> **Why a separate masks directory?** Mask STL files must survive between batch runs. If they were stored inside the batch output directory, clearing that directory for a fresh run would destroy all your ROI work. The ROI Masks Directory is a permanent location independent of any batch output.

---

#### Step 0 — Set the ROI Masks Directory (do this once, before anything else)

1. Go to the **Study Configuration** tab.
2. Find the **"ROI Masks Dir:"** field (below the External Ref field).
3. Click **Browse...** and choose — or create — a permanent directory for your mask files. A good location is a folder like `roi_masks/` next to your study JSON file, **not** inside the batch output directory.
4. Click **Save Configuration** (or load a study JSON that already has `mask_stl_directory` set) — the path is stored in the study JSON so it persists across sessions.

> **The masks directory must not be the batch output directory.** Clearing the batch output for a fresh run would destroy your mask files. Keep them in a separate, permanent location.

---

#### Step A — First batch run (without ROI, to generate reference meshes)

1. Make sure the **"Use ROI mask for registration (masked ICP)"** checkbox in the **Batch Processing** tab is **unchecked** for this first run. You want the software to process all patients with the full mesh so it can create the reference surfaces.
2. Run the batch as normal (see Step 4 in this manual).
3. When the batch finishes, look inside the output directory you specified. You should find a folder called `qc/reference_meshes/`. Inside it there is one file per patient, named like `002_reference.stl`, `003_reference.stl`, and so on. These are the **GPA mean reference meshes** — the average surface computed from all scanner repetitions for each patient. This is the surface you will use as the basis for defining each patient's ROI mask.

> **If the output folder already exists from a previous run**, the reference meshes are already there and you can skip Step A entirely.

---

#### Step B — Define ROI masks, one patient at a time

You will repeat the following steps for every patient in your study. The procedure takes 2–5 minutes per patient once you are familiar with it.

##### Open the ROI Template Editor

1. Start DentScanComparePro.
2. Load your study configuration file in the **Study Configuration** tab and click **Load Configuration**.
3. Click the **ROI Template Editor** tab.

##### Load the patient's reference mesh

4. In the **ROI Template I/O** section at the top right of the tab, click the **Browse...** button next to "Template STL".
5. Navigate to the `qc/reference_meshes/` folder inside your output directory.
6. Select the file for the first patient you want to work on, for example `002_reference.stl`.
7. Click **Load**. The patient's reference mesh appears in the 3D viewport. All ROI settings reset to a blank state — brush zones, plane slab picks, bounding box, and seed points are all cleared, ready for you to define a fresh ROI for this patient.

> **Tip:** You can rotate the 3D view by left-click-dragging, zoom with the scroll wheel, and pan with middle-click-drag.

##### Define the ROI for this patient

You have four tools available. You can use any combination — for example, a Plane Slab to cut off the bottom (roots and gingiva below the crowns) combined with a Brush Exclude zone to remove a patch of buccal mucosa that the Plane Slab missed.

---

**Tool 1 — Plane Slab (most useful for patient scans)**

The Plane Slab defines a slab of space above and below a plane you pick on the tooth surface. Everything outside the slab is excluded from analysis. This is the primary tool for cutting off gingiva and roots.

How to use it:

1. Check the **"Active"** checkbox in the **Plane Slab (ROI Height)** section.
2. Click the **"Pick Plane"** button. The cursor changes to a crosshair.
3. Click on the tooth surface at the level where you want the plane to pass — typically on the buccal surfaces of the teeth at the gingival margin, or just above the cervical line. You need to place **3 points** to define the plane. After the third click, the software fits a plane through your three points and draws it as a semi-transparent grey disk over the mesh.
4. Adjust the **"Offset A (above)"** spinbox to set how many millimetres above the plane are included (this controls how much of the crown is included — typically 8–15 mm depending on crown height).
5. Adjust the **"Offset B (below)"** spinbox to set how many millimetres below the plane are included (typically 1–3 mm to include a small buffer below the CEJ).
6. The coloured ROI mask updates live in the viewport. Green areas are inside the ROI; darker areas are excluded. Rotate the mesh to check that the slab is cutting in the right place for this patient.

> **Important note for upper jaw patients:** In the coordinate system this software uses, Z increases toward the skull. For lower jaw scans, the tooth crowns are at the top (high Z) and the roots point downward (low Z) — the Plane Slab works naturally. For upper jaw scans, the geometry is inverted: crowns point downward (low Z) and the palate is at the top (high Z). The automatic plane height detection may not work correctly for upper jaw patients. In those cases, use the Plane Slab by manually picking points, and verify the result visually before saving.

---

**Tool 2 — Brush Exclude / Include**

The brush tool lets you paint areas directly onto the mesh surface to force-exclude or force-include them from the ROI. Use it to remove patches of gingiva or buccal mucosa that the Plane Slab did not cleanly cut off.

How to use it:

1. Set the **Radius** spinbox to the brush size in millimetres (e.g. 3 mm for a medium brush, 8 mm for a large sweep).
2. Click the **"Exclude"** button (turns red) to activate the exclusion brush.
3. Click on the mesh surface over the area you want to exclude. Each click paints a spherical zone around that point. The excluded area turns dark in the viewport.
4. To include an area that was incorrectly excluded, click the **"Include"** button and paint over it.
5. Click **"Exclude"** or **"Include"** again to deactivate the brush (or click a different pick tool).

> **Tip:** You can combine the Plane Slab and Brush tools freely. The Plane Slab cuts the bulk of the gingiva; the Brush cleans up individual patches that the slab missed.

> **Note:** Clicking "Include" or "Exclude" activates pick mode — the cursor changes and you can click on the mesh. Clicking the same button again deactivates it. Clicking a different tool (e.g. "Pick Plane") also deactivates the brush automatically.

---

**Tool 3 — Bounding Box**

The bounding box restricts analysis to a rectangular region in 3D space. It is less useful for patient scans (where the Plane Slab is usually better) but can be helpful for cutting off the lateral borders of the scan or the posterior end of the arch.

How to use it:

1. Check the **"Active"** checkbox in the **Bounding Box** section.
2. Adjust the Min X/Y/Z and Max X/Y/Z spinboxes to frame the region of interest. The current mesh bounds are filled in automatically when you load a file.
3. A wireframe box appears in the viewport showing the current bounding region.

---

**Tool 4 — Tooth Segmentation (most precise, but slowest)**

Tooth Segmentation uses an algorithm that grows outward from points you click on tooth cusp tips and automatically stops at the gingival margin. It produces the most precise tooth-only mask but requires placing seed points carefully and running a computation step.

How to use it:

1. Click **"Pick Seeds"**.
2. Click once on the tip of each cusp or incisal edge visible in the scan — one click per tooth. Yellow numbered spheres appear at each picked location.
3. Click **"Run Segmentation"**. After a few seconds (depending on mesh size), the tooth surfaces are highlighted in ivory and the excluded areas in dark grey.
4. If the segmentation missed a tooth or included too much gingiva, you can:
   - Add more seed points on missed areas and re-run.
   - Use the Brush Exclude tool to paint over incorrectly included gingiva.
5. Check **"Use Base Selection as ROI"** to include the segmentation result in the final ROI mask.

> **For most patient cohort studies**, the Plane Slab combined with Brush Exclude zones gives sufficient accuracy and is much faster than Tooth Segmentation. Reserve Tooth Segmentation for studies where precise crown-only boundaries are critical, or when the Plane Slab cannot cleanly separate crowns from gingiva due to unusual jaw orientation.

---

##### Save the template for this patient

8. Once you are satisfied with the ROI for this patient, click **"Save Template..."** in the **ROI Template I/O** section.
9. A file dialog opens. The suggested filename is based on the patient ID inferred from the loaded STL name and defaults to the ROI Masks Directory you set in Step 0 (e.g. `/path/to/roi_masks/002_roi_mask.stl`). Accept the suggested path — the exact filename `{groupId}_roi_mask.stl` is what the batch runner expects.
10. The software does two things automatically when you click Save:
    - It writes the **ROI template as a JSON file** alongside the STL. This JSON contains the full ROI definition (bounding box, plane definition, brush zones, tooth segmentation seeds) so you can reload and edit it later.
    - It writes the **ROI mask STL** — named `{groupId}_roi_mask.stl` (e.g. `002_roi_mask.stl`) — to the ROI Masks Directory. This STL contains only the triangles of the reference mesh that fall inside the ROI. The batch runner will load this file directly when processing patient `002`.

> **How to verify the ROI mask STL:** Open the `_roi_mask.stl` file in MeshLab. You should see only the tooth surfaces, without gingiva or scan borders. If soft tissue is still visible, go back into the ROI Template Editor and add more Brush Exclude zones, then save again (the files are overwritten).

> **Important: the filename must match the group ID exactly.** The batch runner looks for `{groupId}_roi_mask.stl` in the masks directory, where `{groupId}` is the `id` field in your study JSON (e.g. `002`, `003`, …). If you rename or move the file, update the path accordingly.

---

##### Proceed to the next patient

11. In the ROI Template Editor, click **Browse...** again and load the next patient's reference STL (e.g. `003_reference.stl`). All ROI settings reset to blank automatically when you load a new file. This ensures that the settings from patient 002 do not carry over to patient 003.
12. Repeat the definition and save steps for this patient.
13. Continue until all patients have a saved ROI mask STL. You can verify which patients are done by listing the contents of your ROI Masks Directory — you should see one `{groupId}_roi_mask.stl` file per patient group (e.g. `002_roi_mask.stl`, `003_roi_mask.stl`, …).

---

#### Step C — Run the batch with per-patient ROI masks active

1. Go to the **Batch Processing** tab.
2. Check the **"Use ROI mask for registration (masked ICP)"** checkbox. This must be checked for the per-patient ROI masks to be applied.
3. Click **Run Batch Processing**.

In the batch log, for each group you will see a line like:

```
    Mask STL (dir lookup): /path/to/roi_masks/002_roi_mask.stl
```

This confirms the batch runner found and loaded the mask for that patient. If a patient's mask is missing, the batch runner falls back to full-mesh processing for that group and logs a warning — it does not fail the entire batch.

> **Note:** You do not need to select a global ROI template file anywhere. The batch runner discovers masks automatically from the ROI Masks Directory by naming convention. The global ROI Template field in the Study Configuration tab can be left empty when using per-patient masks.

---

#### Summary checklist for per-patient ROI workflow

| Step | Action | Done when… |
|------|--------|------------|
| 0 | Set ROI Masks Dir in Study Configuration tab | Path saved to study JSON as `mask_stl_directory` |
| A | Run batch without ROI | `qc/reference_meshes/` folder contains one `*_reference.stl` per patient |
| B1 | Load study config | Study Configuration tab shows all groups |
| B2 | For each patient: Browse → load `<id>_reference.stl` | 3D mesh appears in viewport, all ROI controls reset |
| B3 | Define ROI (Plane Slab + Brush Exclude recommended) | Green/dark mask in viewport looks correct |
| B4 | Save Template (accept default path in masks dir) | `{groupId}_roi_mask.stl` written to masks directory |
| B5 | Repeat B2–B4 for all patients | Masks directory contains one `{id}_roi_mask.stl` per patient |
| C | Check "Use ROI mask" checkbox and run batch | Batch log shows `Mask STL (dir lookup): …` for each patient |

---

**Note on jaw orientation:** In the coordinate system this software uses, Z increases toward the skull. For lower jaw scans, the tooth crowns are at the top (high Z) and the roots point downward (low Z) — the automatic occlusal plane detection works correctly. For upper jaw scans, the geometry is inverted: crowns point downward (low Z) and the palate is at the top (high Z). The automatic Z detection may not work correctly for upper jaw patients. Use the Plane Slab by manually picking three points on the tooth surface, and verify the result visually before saving.

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
| `qc/difference_meshes/<scan>.ply` | Binary PLY with a per-vertex `distance` float scalar (signed mm, positive = scan proud, negative = recessed). **Mask STL path**: the PLY contains the mask mesh geometry colored by `maskDistances` — only the ROI region, no boundary band. **Full-mesh / ROI path**: the PLY contains the aligned scan geometry colored per scan vertex. Open in MeshLab (*Render → Color → Per-Vertex Quality*) or ParaView. |
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

**How pairwise RMS is computed** depends on the active workflow:

- **Mask STL path** (patient studies with per-patient ROI masks): for each pair of repetitions (scan_i, scan_j) of the same scanner, the pairwise RMS is `sqrt(mean_k((d_i[k] − d_j[k])²))` where `d[k]` is the signed distance from mask vertex k to the nearest point on the scan surface — the same values used for trueness. This is consistent: both metrics operate on identical mask-vertex distances, so precision and trueness are directly comparable.

- **Full-mesh / geometric ROI path** (phantom studies): for each pair, the RMS of scan_i vertex distances to the scan_j surface (AABB tree query). The geometric ROI (bbox, plane slab, brush zones) is applied as a vertex filter.

---

## Reference Mesh Computation

This section explains when and how the reference mesh (mean mesh) is computed, and how to trigger recalculation.

### The Two Alignment Options (Mutually Exclusive)

The GUI provides two checkboxes that are **mutually exclusive** — checking one automatically unchecks the other:

| Checkbox | When to use | Effect |
|----------|-------------|--------|
| **Scans are normalized** ✓ | STL files from DentScanAlignPro with transform baked into geometry | Skip JSON transform loading. Run GPA but skip PCA coarse alignment (scans already oriented). |
| **Scans are pre-aligned** ✓ | Raw STL files + JSON transform files from DentScanAlign | Load JSON transforms, apply them, then run ICP refinement only (skip full GPA). |
| **Both unchecked** ☐ | Raw STL files with no prior alignment | Run full GPA with PCA coarse alignment. |

### Initial Reference Scan Selection

During GPA, one scan is chosen as the starting reference. This choice affects convergence speed and the quality of the initial alignment. The selection follows this priority:

1. **Manual selection** via `initial_reference_scan` in the group config (JSON study file)
2. **Automatic fallback**: scan with the **median** triangle count

**Why median instead of largest?** Previously, the software selected the scan with the most triangles. However, scans with more triangles often have more soft tissue/gingiva captured, which introduces artifacts into the reference mesh. The median is robust against such outliers.

**To manually specify the initial reference** for each group, add `initial_reference_scan` to your study JSON:

```json
"groups": [
  {
    "id": "002",
    "file_patterns": ["*_002_*_aligned.stl"],
    "initial_reference_scan": "Primescan_002_D2_aligned.stl"
  }
]
```

The console will show:
```
Initial reference: .../Primescan_002_D2_aligned.stl (selected by filename match)
```

### When Is a Reference Mesh (Mean Mesh) Computed?

The reference mesh computation depends on **two factors**:

1. **Is an external reference STL provided?** (via the "External Ref" field)
2. **Which alignment option is selected?**

**Decision table:**

| External Reference | Alignment Option | Reference Mesh Source |
|--------------------|------------------|----------------------|
| **YES** (path provided) | any | **No mean mesh computed** — external STL is used directly as reference |
| **NO** | normalized ✓ | **Full GPA** — iterative mean mesh computation (multiple cycles until convergence) |
| **NO** | pre-aligned ✓ | **Single mean mesh update** — ICP refinement against largest scan, then one mean mesh computation |
| **NO** | Both ☐ | **Full GPA with PCA** — PCA coarse alignment + iterative mean mesh computation |

**Key insight:** A new reference mesh is computed whenever:
- No external reference is provided, AND
- Any of the three alignment options is used

The difference between the options is **how** the mean mesh is computed:
- **normalized** or **both unchecked**: Full GPA iteration — align all scans, compute mean, repeat until convergence
- **pre-aligned**: Single pass — ICP refinement against the largest scan, then one mean mesh update

### How to Recalculate Reference Meshes with Robust Averaging

The software now includes **robust averaging** to prevent artifacts in the mean mesh (see docs/computation-of-metrics.md for details). To recalculate your reference meshes with these improvements:

1. **Delete the existing reference meshes** (optional but recommended):
   ```
   rm -rf output_dir/qc/reference_meshes/
   ```

2. **Run the batch again** with your normal settings. The software will:
   - Recompute the GPA alignment (or ICP refinement, depending on your settings)
   - Generate new mean meshes using robust averaging with default parameters:
     - `mean_mesh_max_distance_mm = 0.15` — reject closest points farther than this
     - `mean_mesh_min_normal_dot = 0.5` — reject if normals differ by >60°
     - `mean_mesh_min_coverage = 0.9` — require 90% of scans to have valid data

   These strict defaults implement a "boolean AND" intersection approach that automatically excludes gingiva and soft tissue regions not consistently captured across all scans.

3. **Check the console output** for statistics:
   ```
   Mean mesh statistics:
     Vertices with good coverage: 42150 / 48000 (87.8%)
     Faces with good coverage:    83200 / 96000 (86.7%)
     Distance rejections:         12450 (across all vertices)
     Normal rejections:           3200 (across all vertices)

   GPA coverage: 83200/96000 faces (86.7%) have consistent coverage across all scans
   ```

**Note:** If you are using an external reference (`External Ref` field is set), no mean mesh is computed — the external STL is used directly. The robust averaging improvements do not apply in this case.

### Tuning Coverage Filtering Parameters

If the default strict settings exclude too much tooth surface, or if you want even stricter gingiva exclusion, you can configure these parameters in your study YAML file:

```yaml
study:
  alignment:
    # Coverage filtering for gingiva exclusion
    mean_mesh_max_distance_mm: 0.15   # Default: 0.15 (strict)
    mean_mesh_min_coverage: 0.9       # Default: 0.9 (strict)
    mean_mesh_min_normal_dot: 0.5     # Default: 0.5
```

**Parameter guide:**

| Use Case | `mean_mesh_max_distance_mm` | `mean_mesh_min_coverage` |
|----------|----------------------------|--------------------------|
| **Strict gingiva exclusion** (default) | 0.10 – 0.15 | 0.9 – 1.0 |
| **Moderate filtering** | 0.2 – 0.3 | 0.7 – 0.8 |
| **Permissive (old behavior)** | 0.5 | 0.5 |

- `mean_mesh_max_distance_mm`: Maximum distance for a scan's closest point to be valid. Smaller = stricter. If a scan has a hole at a location, its closest point will be farther away and will be rejected.
- `mean_mesh_min_coverage`: Fraction of scans that must have valid data. 1.0 = all scans must agree (true boolean AND). 0.9 = 90% of scans.
- `mean_mesh_min_normal_dot`: Normal consistency check. 0.5 = allow up to ~60° deviation. Set to 0.0 to disable.

### Typical Workflow for Recalculation

For most users with normalized scans from DentScanAlignPro:

```
Settings:
  ☑ Scans are normalized (checked)
  ☐ Scans are pre-aligned (automatically unchecked)
  External Ref: (empty)

→ Runs full GPA (skip PCA since scans are oriented)
→ Computes new mean mesh with robust averaging
→ New reference meshes saved to qc/reference_meshes/
```

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
7. **Compute trueness metrics** - RMS, MAD, Hausdorff, coverage, completeness. Three filters applied in sequence: (1) source-side geometric ROI mask re-applied to the aligned scan vertex positions — primary soft-tissue exclusion; (2) distance guard: vertices more than 5 mm from the masked reference excluded; (3) tooth segmentation mask, if active. Only vertices passing all three filters contribute to the CSV numbers.
8. **Compute precision metrics** - Pairwise comparisons between repetitions per scanner (skipped when `compute_precision: false`). In the mask STL path: reuses existing `maskDistances` arrays — `pairRMS(i,j) = sqrt(mean((d_i − d_j)²))` over mask vertices, consistent with trueness. In the fallback path: AABB tree query, scan_i vertices to scan_j surface.
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

Progress is tracked in `batch_progress.json` in the output directory.

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

All CSV files are written as plain UTF-8 without a byte-order mark (BOM) and open correctly in LibreOffice Calc on Linux and in Microsoft Excel 2016+ on Windows.

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

**What it contains:** One row per Scanner × Group cell. Each row summarises the **within-cell pairwise precision** — how consistent the same scanner is across repeated measurements under the same conditions.

For N = 7 repetitions there are N×(N−1)/2 = 21 unique pairs. Each pair contributes one pairwise RMS value. The row reports the mean, SD, and CV of those 21 values.

**How the pairwise RMS for one pair is computed:**

- **Mask STL path** (per-patient ROI masks, e.g. P2026-Nold): `pairRMS = sqrt(mean_k((d_i[k] − d_j[k])²))` where `d[k]` is the signed distance from mask vertex k to the scan surface. Both scans in the pair use the same mask mesh, so the distances are indexed at the same spatial locations. This is consistent with the trueness RMS — both metrics use identical `maskDistances` values, just combined differently.

- **Full-mesh / geometric ROI path** (phantom studies): for each scan_i vertex inside the geometric ROI, find its distance to the scan_j surface (AABB tree). `pairRMS = sqrt(mean(d²))` over those vertices.

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
