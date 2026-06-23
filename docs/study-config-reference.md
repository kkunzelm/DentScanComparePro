# Study Configuration JSON Reference

Every batch run is driven by a single JSON configuration file. This document describes every field, its type, whether it is required or optional, its default value, and what it does.

For annotated ready-to-use examples see the end of this file.

---

## Top-level structure

```json
{
  "study":    { ... },
  "scanners": [ ... ],
  "groups":   [ ... ],
  "output":   { ... }
}
```

All four top-level keys are required.

---

## `study` — Study-level settings

```json
"study": {
  "name": "P2026-Nold",
  "version": 1,
  "reference_strategy": "gpa_mean",
  "external_reference_path": "",
  "scans_pre_aligned": false,
  "scans_normalized": true,
  "alignments_directory": "",
  "alignment": {
    "max_icp_iterations": 100,
    "max_gpa_iterations": 20,
    "convergence_threshold_mm": 0.01,
    "use_pca_coarse": true,
    "use_4orientation_test": true,
    "mean_mesh_max_distance_mm": 0.15,
    "mean_mesh_min_coverage": 0.9,
    "mean_mesh_min_normal_dot": 0.5
  }
}
```

| Field | Type | Required | Default | Description |
|-------|------|----------|---------|-------------|
| `name` | string | yes | — | Study identifier. Appears in log output. |
| `version` | integer | no | `1` | Config schema version. Currently unused. |
| `reference_strategy` | string | no | `"gpa_mean"` | How the reference mesh is obtained. `"gpa_mean"` computes a group consensus from all scans; `"external"` uses a single external STL (set `external_reference_path`). |
| `external_reference_path` | string | only if `reference_strategy: "external"` | `""` | Absolute or relative path to the external reference STL. |
| `scans_pre_aligned` | bool | no | `false` | **See explanation below.** When `true`, skips the GPA iterative alignment stage and uses the scans' existing positions as the starting point for ICP refinement. Set to `false` for the standard workflow. |
| `scans_normalized` | bool | no | `true` | **See explanation below.** When `true`, the software does not load JSON transform files from `alignments_directory`, even if that directory is set. Use `true` when scan geometry already has the alignment transform baked in (DentScanAlign normalized STL output). |
| `alignments_directory` | string | no | `""` | Path to a directory containing DentScanAlign JSON transform files. Only used when `scans_normalized: false`. |

### `study.alignment` — ICP and GPA registration parameters

| Field | Type | Default | Description |
|-------|------|---------|-------------|
| `max_icp_iterations` | integer | `100` | Maximum number of point-to-plane ICP iterations per alignment pass. |
| `max_gpa_iterations` | integer | `20` | Maximum number of GPA cycles. Each cycle aligns all scans to the current reference and updates the mean mesh. Set to 10–12 if GPA oscillates instead of converging. |
| `convergence_threshold_mm` | float | `0.01` | GPA is considered converged when the maximum reference vertex displacement between cycles falls below this value (mm). For datasets with boundary coverage variation, 0.1–0.15 may be more practical. |
| `use_pca_coarse` | bool | `true` | Run a PCA-based coarse alignment (centroid + principal axes) before ICP. Required when scans may have arbitrary orientation. |
| `use_4orientation_test` | bool | `true` | After PCA, test four 90° rotations around the Z-axis and pick the one that gives the lowest ICP residual. Catches 180° flips that PCA alone cannot resolve. |
| `mean_mesh_max_distance_mm` | float | `0.15` | Maximum distance (mm) for a closest point to be valid during mean mesh computation. Larger values allow more correspondences but risk including points from adjacent surfaces. |
| `mean_mesh_min_coverage` | float | `0.9` | Minimum fraction of scans (0.0–1.0) that must have valid correspondences for a vertex to be included. Lower values (0.5–0.7) allow more vertices but may include poorly-covered regions. |
| `mean_mesh_min_normal_dot` | float | `0.5` | Minimum normal dot product (0.0–1.0) for correspondence validity. 0.5 allows ~60° deviation; lower values are more permissive at boundaries. |

---

### Understanding `scans_pre_aligned` and `scans_normalized`

These two options are frequently confused because both relate to DentScanAlign output. They control independent parts of the pipeline.

#### `scans_normalized` — do not reload JSON transforms

DentScanAlign saves its result in two forms:
1. **Normalized STL** — the alignment transform is applied directly to the mesh vertices and saved as a new STL file. The STL file looks "already placed" when you open it.
2. **JSON transform file** — the 4×4 matrix that describes the alignment, saved alongside the original (untouched) STL.

When you use **normalized STL files**, the geometry is already where it should be. If the software were to also load and apply the JSON transform file, the scan would be shifted a second time and end up in the wrong place. Setting `scans_normalized: true` prevents the software from loading JSON transform files entirely — the JSON files on disk are ignored.

When you use **original (raw) STL files together with JSON transforms**, set `scans_normalized: false` and provide `alignments_directory`. The software loads the JSON, applies the transform as a coarse pre-alignment, and then runs ICP refinement on top.

**Default is `true`** because the normalized-STL workflow is the most common when working with DentScanAlign output.

#### `scans_pre_aligned` — skip GPA consensus computation

The standard alignment pipeline has two stages:
1. **GPA (Generalized Procrustes Analysis)** — all scans in the group are iteratively aligned against a running consensus (mean mesh). This computes the Virtual Reference Model (VRM) and places every scan relative to it. This stage is computationally expensive and is required to establish the group reference.
2. **ICP fine registration** — after GPA, each scan receives one additional fine ICP pass against the settled VRM.

Setting `scans_pre_aligned: true` skips stage 1 (GPA). The scans' current positions are treated as the starting point and only ICP fine registration runs (with a tighter 5 mm correspondence radius, because the scans are assumed to already be approximately aligned).

**Use `scans_pre_aligned: true` only when the scans are already registered to a common external reference** (for example, DentScanAlign has already aligned all scans to a fixed template and you do not want to compute a new GPA consensus from the scans themselves).

**For the standard patient-study workflow (e.g. P2026-Nold):** use `scans_normalized: true` (geometry already moved) and `scans_pre_aligned: false` (still run full GPA + ICP to establish the VRM and monitor convergence).

#### Quick reference

| Workflow | `scans_normalized` | `scans_pre_aligned` |
|----------|--------------------|---------------------|
| Standard: raw scans, no external pre-alignment | `false` | `false` |
| DentScanAlign normalized STL output, full GPA | `true` | `false` |
| DentScanAlign normalized STL, skip GPA | `true` | `true` |
| DentScanAlign raw STL + JSON transforms | `false` | `true` (recommended) |

---

## `scanners` — Scanner definitions

```json
"scanners": [
  {"id": "Primescan",      "patterns": ["Primescan*", "*PS*"]},
  {"id": "Trios3",         "patterns": ["Trios3*"]},
  {"id": "Carestream3700", "patterns": ["Carestream3700*"]}
]
```

An array of scanner definition objects. At least one entry is required.

| Field | Type | Required | Description |
|-------|------|----------|-------------|
| `id` | string | yes | Scanner name used in all CSV output. Must be unique within the file. |
| `patterns` | array of strings | yes | Glob patterns matched against the full file path. If any pattern matches, the file is assigned to this scanner. Patterns support `*` (any characters) and `?` (single character). Matching is case-insensitive. |

**Pattern matching** is applied to the complete file path (directory + filename). The pattern `Carestream3700*` matches any path that contains the string `Carestream3700`. For the Nold study naming convention `{Scanner}_{Patient}_{Rep}_aligned.stl`, the pattern `Carestream3700*` matches the filename prefix and is sufficient.

Files that do not match any scanner pattern are assigned the scanner ID `"Unknown"` with a warning in the log. They still contribute to the GPA computation but may distort group statistics.

---

## `groups` — Group definitions

```json
"groups": [
  {
    "id": "002",
    "skd_mm": 0,
    "file_patterns": ["*_002_*_aligned.stl"],
    "roi": { ... },
    "representative_scan": "/path/to/a_scan.stl"
  }
]
```

An array of group definition objects. At least one group is required. Each group becomes one GPA computation — all files matching the group's `file_patterns` are loaded together, aligned by GPA, and measured against the resulting VRM.

**What is a group?** A group is whatever unit of comparison makes sense for your study design. In a phantom study it is an SKD level (one defect width); in a patient cohort study it is one patient (one person's dentition). All scans in a group must be anatomically comparable — GPA only makes sense when all scans are of the same object.

| Field | Type | Required | Default | Description |
|-------|------|----------|---------|-------------|
| `id` | string | yes | — | Group label. Appears as `Group_ID` in all CSV output. Use any string: `"SKD_20"`, `"002"`, `"Patient_A"`. |
| `skd_mm` | integer | no | `0` | Legacy field from phantom studies. Ignored in CSV output — use `id` for the label. Set to `0` for non-phantom studies. |
| `file_patterns` | array of strings | yes | — | Glob patterns for file discovery. Patterns support `*`, `**` (recursive), and `?`. Relative to the `--data-root` directory. |
| `roi` | object | no | All ROI inactive | Region of Interest configuration for this group. See `roi` section below. |
| `representative_scan` | string | no | `""` | Path to a scan used in the GUI ROI Template Editor for this group. Not used during batch processing. |
| `initial_reference_scan` | string | no | `""` | Filename or path of the scan to use as the initial reference for GPA computation. Matches against the filename or full path. If empty or no match found, falls back to selecting the scan with the **median** triangle count (robust against outliers with excessive soft tissue). See **Initial Reference Selection** below. |

### `groups[].roi` — Region of Interest

The ROI defines which part of each scan surface contributes to metric computation. All ROI components are disabled by default; without an active ROI, the full mesh is used.

```json
"roi": {
  "bbox": {
    "active": false,
    "min": [-20.0, -30.0, 0.0],
    "max": [ 20.0,  30.0, 15.0]
  },
  "z_plane": {
    "active": false,
    "above_mm": 2.0,
    "below_mm": 12.0
  },
  "brush_zones": [
    {
      "center": [5.0, -2.0, 8.0],
      "radius_mm": 3.0,
      "include": true
    }
  ],
  "outlier_sigma": 3.0
}
```

| Field | Type | Default | Description |
|-------|------|---------|-------------|
| `bbox.active` | bool | `false` | When `true`, only vertices inside the bounding box are included. |
| `bbox.min` | [x, y, z] | `[0,0,0]` | Minimum corner of the bounding box in mm. |
| `bbox.max` | [x, y, z] | `[0,0,0]` | Maximum corner of the bounding box in mm. |
| `z_plane.active` | bool | `false` | When `true`, only vertices between `above_mm` and `below_mm` from the detected occlusal plane are included. **Default is `false`** — must be explicitly activated. |
| `z_plane.above_mm` | float | `2.0` | Include vertices up to this distance above the occlusal plane (mm). |
| `z_plane.below_mm` | float | `12.0` | Include vertices up to this distance below the occlusal plane (mm). |
| `brush_zones` | array | `[]` | Spherical override regions. Each entry forces vertices inside its radius to be included or excluded regardless of other ROI components. |
| `brush_zones[].center` | [x, y, z] | — | Center of the sphere (mm, in GPA reference coordinate frame). |
| `brush_zones[].radius_mm` | float | — | Radius of the sphere (mm). |
| `brush_zones[].include` | bool | — | `true` = force vertices inside this sphere into the ROI; `false` = force vertices out. |
| `outlier_sigma` | float | `3.0` | After distance computation, vertices whose signed distance deviates more than this many standard deviations from the group mean are excluded from metric aggregation. Set to `0` to disable outlier removal. |

**Inheritance:** A group's ROI can be copied from another group using:
```json
"roi": { "inherit_from": "SKD_20" }
```
This is convenient when multiple groups share the same ROI definition. The group named in `inherit_from` must appear earlier in the `groups` array.

---

### Initial Reference Selection

During GPA computation, one scan must be chosen as the initial reference mesh. The other scans are then iteratively aligned to this reference, and the reference is updated to the mean of all aligned scans until convergence.

**Selection priority:**

1. **`initial_reference_scan`** (if specified): The scan whose filename or path contains this string is used. This gives you full manual control over which scan starts the GPA computation.

2. **Median triangle count** (automatic fallback): If `initial_reference_scan` is empty or no matching scan is found, the software selects the scan with the **median** triangle count across all scans in the group.

**Why median instead of largest?** Previously, the software selected the scan with the largest number of triangles. However, more triangles often correlates with more soft tissue/gingiva being scanned, which introduces artifacts into the reference mesh. The median is more robust against such outliers.

**Example usage:**

```json
"groups": [
  {
    "id": "002",
    "file_patterns": ["*_002_*_aligned.stl"],
    "initial_reference_scan": "Primescan_002_D2_aligned.stl"
  }
]
```

The console output shows which scan was selected and why:
```
Initial reference: .../Primescan_002_D2_aligned.stl (selected by filename match)
```
or:
```
Initial reference: .../SomeScanner_002_r3.stl (median triangle count: 125000)
```

---

## Study designs with fewer than three factors

DentScanComparePro recognises three independent factors in every study: **Group**, **Scanner**, and **Repetition**. In many study designs all three vary simultaneously (for example: 16 patients × 4 scanners × 7 repetitions). However, it is valid and supported to design a study with only two varying factors by making the third factor trivially constant.

### Group × Scanner (no repetitions)

The most common two-factor design: one scan per scanner per group, no repeated measurements.

- Trueness metrics are computed normally — one row per scan in `trueness_metrics.csv`.
- Precision metrics require at least two scans in the same Scanner × Group cell. With exactly one scan per cell, `precision_metrics.csv` will contain rows with `Pairwise_Count = 0` and empty precision values.

This design answers: *"How accurate is each scanner relative to the GPA reference?"*

```json
"groups": [
  {"id": "Patient_A", "file_patterns": ["*_A_*.stl"]},
  {"id": "Patient_B", "file_patterns": ["*_B_*.stl"]}
],
"scanners": [
  {"id": "Primescan", "patterns": ["*Primescan*"]},
  {"id": "Trios3",    "patterns": ["*Trios3*"]}
]
```

### Group × Repetition (single scanner)

If you have only one scanner model (or want to treat all scanners as identical), use a single catch-all scanner entry. Trueness and precision both work normally.

```json
"scanners": [
  {"id": "AllScanners", "patterns": ["*.stl"]}
]
```

This design answers: *"How precise is the scanning procedure across repeated measurements, regardless of which scanner was used?"*

### Scanner × Repetition (single group)

If all scans belong to one anatomical unit (e.g. a single patient scanned many times), use a single group:

```json
"groups": [
  {"id": "Patient_01", "file_patterns": ["*.stl"]}
]
```

This design is useful for a single-subject reproducibility check. Trueness is computed relative to the GPA mean of that one group.

### Minimum viable study

The absolute minimum is **one group with one scanner and two scans**. Trueness computes one row per scan; precision computes one pair. In practice:

- At least **3 repetitions per cell** for precision to be meaningful.
- At least **3 groups** for the GPA consensus to be stable — GPA converges better with more samples and more anatomical variation.

---

## `output` — Output file settings

```json
"output": {
  "base_dir": "./results_P2026_Nold",
  "metrics_csv": "trueness_metrics.csv",
  "precision_csv": "precision_metrics.csv",
  "summary_csv": "summary_stats.csv"
}
```

| Field | Type | Default | Description |
|-------|------|---------|-------------|
| `base_dir` | string | `"./results"` | Output directory for all CSV files. Created if it does not exist. Relative paths are resolved from the working directory where the application is started. |
| `metrics_csv` | string | `"long_format_metrics.csv"` | Filename for the per-scan trueness metrics (one row per scan). |
| `precision_csv` | string | `"precision_matrix.csv"` | Filename for the per-scanner-per-group precision summary. |
| `summary_csv` | string | `"summary_by_scanner_skd.csv"` | Filename for the per-scanner-per-group trueness aggregation. |

The `--output` command-line option overrides `base_dir`. Filenames within the directory are always taken from the JSON.

**Recommended naming:** The default filenames were chosen for the first (Kessler) study. For clarity, newer studies use descriptive names:
- `"metrics_csv": "trueness_metrics.csv"`
- `"precision_csv": "precision_metrics.csv"`
- `"summary_csv": "summary_stats.csv"`

---

## Complete examples

### Phantom study (scanner × SKD level design)

```json
{
  "study": {
    "name": "P2024-Kessler",
    "version": 1,
    "reference_strategy": "gpa_mean",
    "scans_pre_aligned": false,
    "scans_normalized": false,
    "alignment": {
      "max_icp_iterations": 100,
      "max_gpa_iterations": 20,
      "convergence_threshold_mm": 0.01,
      "use_pca_coarse": true,
      "use_4orientation_test": true
    }
  },
  "scanners": [
    {"id": "Primescan",   "patterns": ["*Primescan*"]},
    {"id": "Trios4",      "patterns": ["*Trios*4*"]},
    {"id": "iTeroLumina", "patterns": ["*iTero*", "*Lumina*"]},
    {"id": "FussenS6000", "patterns": ["*Fussen*"]},
    {"id": "Mediti700",   "patterns": ["*Medit*i700*"]},
    {"id": "Trios5",      "patterns": ["*Trios*5*"]}
  ],
  "groups": [
    {"id": "SKD_18", "skd_mm": 18, "file_patterns": ["**/SKD*18*/*.stl"]},
    {"id": "SKD_20", "skd_mm": 20, "file_patterns": ["**/SKD*20*/*.stl"]},
    {"id": "SKD_22", "skd_mm": 22, "file_patterns": ["**/SKD*22*/*.stl"]},
    {"id": "SKD_24", "skd_mm": 24, "file_patterns": ["**/SKD*24*/*.stl"]},
    {"id": "SKD_26", "skd_mm": 26, "file_patterns": ["**/SKD*26*/*.stl"]},
    {"id": "SKD_28", "skd_mm": 28, "file_patterns": ["**/SKD*28*/*.stl"]},
    {"id": "SKD_30", "skd_mm": 30, "file_patterns": ["**/SKD*30*/*.stl"]}
  ],
  "output": {
    "base_dir": "./results_Kessler",
    "metrics_csv": "trueness_metrics.csv",
    "precision_csv": "precision_metrics.csv",
    "summary_csv": "summary_stats.csv"
  }
}
```

### Patient cohort study (scanner × patient design, pre-aligned STLs)

```json
{
  "study": {
    "name": "P2026-Nold",
    "version": 1,
    "reference_strategy": "gpa_mean",
    "scans_pre_aligned": false,
    "scans_normalized": true,
    "alignment": {
      "max_icp_iterations": 100,
      "max_gpa_iterations": 12,
      "convergence_threshold_mm": 0.15,
      "use_pca_coarse": true,
      "use_4orientation_test": true,
      "mean_mesh_max_distance_mm": 0.5,
      "mean_mesh_min_coverage": 0.5,
      "mean_mesh_min_normal_dot": 0.3
    }
  },
  "scanners": [
    {"id": "Carestream3700", "patterns": ["Carestream3700*"]},
    {"id": "Medit700",       "patterns": ["Medit700*"]},
    {"id": "Primescan",      "patterns": ["Primescan*"]},
    {"id": "Trios3",         "patterns": ["Trios3*"]}
  ],
  "groups": [
    {"id": "002", "skd_mm": 0, "file_patterns": ["*_002_*_aligned.stl"]},
    {"id": "003", "skd_mm": 0, "file_patterns": ["*_003_*_aligned.stl"]},
    {"id": "004", "skd_mm": 0, "file_patterns": ["*_004_*_aligned.stl"]}
  ],
  "output": {
    "base_dir": "./results_P2026_Nold",
    "metrics_csv": "trueness_metrics.csv",
    "precision_csv": "precision_metrics.csv",
    "summary_csv": "summary_stats.csv"
  }
}
```

The patient study config is generated automatically by `scripts/gen_nold_study_config.py` — you do not need to write it by hand.
