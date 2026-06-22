# DentScanComparePro

Automated batch evaluation of dental intraoral scanner accuracy. Computes ISO 5725/12836-
compliant trueness and precision metrics across multiple scanners and study designs
(phantom SKD levels, patient cohorts, or any grouping scheme).

Based on the core algorithms from [DentScanCompare](../DentScanCompare/), extended with:
- JSON-driven batch configuration with generic group IDs
- Automated file discovery via glob patterns with scanner ID matching
- Per-group GPA alignment with incremental save/resume
- **Per-patient ROI templates**: each patient group carries its own ROI mask STL. A dedicated **ROI Masks Directory** holds mask files named `{groupId}_roi_mask.stl`. The batch runner auto-discovers the correct mask per patient — no per-group entries needed in the study JSON.
- Quality Control (QC) workflow with visual verification and re-registration
- CSV output for statistical analysis (R, SPSS, etc.)

---

## Scope: DentScanComparePro vs DentScanCompare

This project has a sibling: **DentScanCompare**.  Both share the same core algorithms
(GPA registration, ICP, curvature analysis, tooth segmentation, distance fields) but
serve different use cases:

| Aspect | DentScanComparePro | DentScanCompare |
|--------|-------------------|-----------------|
| **Primary Use** | Automated batch evaluation of large studies (100+ scans) | Interactive analysis of ~5 scanner files |
| **Workflow** | JSON-driven batch configuration + CLI mode | Manual, GUI-driven |
| **Output** | ISO 5725/12836-compliant trueness & precision CSVs | Visual results + single CSV |
| **Target Users** | Production pipelines, statistical analysis (R, SPSS) | Researchers exploring data interactively |

**Choose DentScanComparePro when:**
- You have a large study (multiple scanners × multiple groups × repetitions)
- You need automated batch processing with resume capability
- You require QC workflow (visual verification, errand flagging, re-registration)
- You want to integrate with external alignment tools (DentScanAlign)
- You need pairwise precision metrics across scan repetitions

**Choose DentScanCompare when:**
- You have a small number of scans to compare (typically ≤10)
- You want rich interactive visualization (3D colour maps, scatter plots)
- You need to manually place tooth segmentation seeds and inspect results
- You're exploring data or preparing figures for publication

---

## Features

### Dual-Mode Operation
- **GUI Mode** (default): Interactive interface for configuration, ROI template editing, batch monitoring, and QC review
- **CLI Mode** (`--batch`): Headless batch processing for automated pipelines

### Batch Processing
- JSON configuration for scanners, groups (SKD levels, patients, or any label), and output paths
- Automatic file discovery via glob patterns with scanner ID matching
- Incremental save after each group (resume after interruption)
- Progress tracking via `batch_progress.json`

### Quality Control (QC) Workflow
- Export GPA mean meshes (STL), transform matrices (JSON), segmented meshes
- Thumbnail grid for quick visual review with accept/flag workflow
- Interactive re-registration dialog with landmark-based Kabsch alignment
- Detailed overlay view (reference wireframe + distance-colored scan)

### Metrics Output
- **Tessellation**: Triangle count, mean edge length, mean/max aspect ratio, ATI, curvature density (high/low)
- **Trueness**: RMS, MAD, Hausdorff (H95, H100), bias (signed mean), coverage rate, boundary length, hole count, stitching angle
- **Precision**: Pairwise RMS between scan repetitions per scanner per group
- **Summary**: Aggregated trueness statistics per scanner per group
- **Visual QC exports**: aligned scan STLs, signed-distance PLY meshes (open in MeshLab/ParaView), ROI-masked reference STL — all saved per scan in `qc/` subdirectories for independent visual verification

### Coarse-to-Fine ICP (Xi-2025)
- Optional resolution hierarchy: source mesh decimated to 5% / 20% / 100% of faces per level
- Curvature-weighted QEM decimation: edges in negative-curvature regions (CEJ, developmental grooves, gingival crevice) receive a ×10 collapse cost, preserving tooth boundary triangles at coarse levels (Xi-2025 Algorithm 1)
- Each coarse level runs to ICP convergence and seeds the next finer level — avoids local minima from large initial offsets
- Enabled via `--icp-hierarchy` CLI flag, GUI checkbox, or `use_icp_hierarchy` in study JSON

### Integration
- Load pre-computed transforms from DentScanAlign
- ROI templates: any active geometric component (bounding box, plane slab, brush zones) is applied to the **reference mesh once** to create a trimmed submesh; all source scans then align to that masked reference via standard ICP. This makes ROI-restricted alignment robust to inter-scanner offsets — source scans use their full geometry during alignment. Tooth segmentation seeds additionally filter metric computation per scan.
- **Per-patient ROI**: in clinical studies each patient needs an individual ROI mask STL. Workflow: (1) set **ROI Masks Dir** in the Study Configuration tab (a permanent directory, independent of the batch output directory); (2) run one batch without ROI to generate `qc/reference_meshes/<id>_reference.stl` per patient; (3) in the ROI Template Editor, Browse → load each patient's reference STL, draw the mask (Plane Slab + Brush Exclude recommended), click Save Template — the mask STL is written to `{maskStlDir}/{groupId}_roi_mask.stl` automatically; (4) repeat for all patients; (5) enable "Use ROI mask" and run the batch. `BatchRunner` auto-discovers each mask from the directory by naming convention before calling `GroupProcessor`.
- External reference support (CAD or lab scanner STL)

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
    --roi-template roi_template.json \
    --verbose
```

### CLI Options
| Option | Description |
|--------|-------------|
| `--batch`, `-b` | Run in headless CLI mode (no GUI) |
| `--study`, `-s` | Path to study configuration JSON file |
| `--data-root`, `-d` | Root directory containing scanner folders |
| `--output`, `-o` | Output directory for CSV files (default: ./results) |
| `--roi-template`, `-r` | Optional ROI template with tooth segmentation settings |
| `--alignments`, `-a` | Directory containing DentScanAlign JSON transform files |
| `--external-ref`, `-e` | External reference STL (CAD or lab scanner) |
| `--pre-aligned` | Skip GPA; run one ICP pass per scan then compute mean mesh. Also skips curvature/tessellation metrics unless a ROI tooth mask is active. Use with DentScanAlignPro normalized output. |
| `--normalized` | Geometry already contains the baked transform; skip JSON transform loading |
| `--trim-fraction` | TrICP outlier rejection: keep only this fraction of ICP correspondences per iteration, sorted by point-to-plane residual (1.0 = no trimming; 0.5 = keep best 50%). Overrides `icp_trim_fraction` in study config. |
| `--icp-hierarchy` | Enable coarse-to-fine ICP hierarchy (Xi-2025): decimates source at 5%/20%/100% of faces using curvature-weighted QEM, seeds each level with the previous transform. Overrides `use_icp_hierarchy` in study config. |
| `--verbose` | Print detailed progress information |

---

## Study Configuration (JSON)

The `group.id` field is a free-form string label — use SKD values for phantom studies or patient IDs for clinical studies. `Group_ID` from `id` appears in all CSVs. The optional `condition_value` integer stores a study-specific numeric parameter (e.g. depth in mm for phantom studies); omit it for patient cohort studies where the group label alone is sufficient.

**Phantom study (SKD levels):**
```json
{
  "study": {
    "name": "P2024-Kessler", "version": 1, "reference_strategy": "gpa_mean",
    "alignment": { "icp_trim_fraction": 1.0 }
  },
  "scanners": [
    {"id": "Primescan",   "patterns": ["*Primescan*"]},
    {"id": "Trios4",      "patterns": ["*Trios*4*"]},
    {"id": "iTeroLumina", "patterns": ["*iTero*"]}
  ],
  "groups": [
    {"id": "SKD_20", "condition_value": 20, "file_patterns": ["**/SKD_20/*.stl"]},
    {"id": "SKD_22", "condition_value": 22, "file_patterns": ["**/SKD_22/*.stl"]}
  ],
  "output": {
    "base_dir": "./results",
    "metrics_csv": "trueness_metrics.csv",
    "precision_csv": "precision_metrics.csv",
    "summary_csv": "summary_stats.csv"
  }
}
```

**Patient study (mixed-effects design) — note `icp_trim_fraction: 0.5` for soft-tissue rejection:**
```json
{
  "study": { "name": "P2026-Nold", "version": 1, "reference_strategy": "gpa_mean",
             "scans_normalized": true,
             "mask_stl_directory": "/path/to/roi_masks",
             "alignment": { "icp_trim_fraction": 0.5 } },
  "scanners": [
    {"id": "Carestream3700", "patterns": ["Carestream3700*"]},
    {"id": "Medit700",       "patterns": ["Medit700*"]},
    {"id": "Primescan",      "patterns": ["Primescan*"]},
    {"id": "Trios3",         "patterns": ["Trios3*"]}
  ],
  "groups": [
    {"id": "002", "file_patterns": ["*_002_*_aligned.stl"]},
    {"id": "003", "file_patterns": ["*_003_*_aligned.stl"]}
  ],
  "output": {
    "base_dir": "./results_P2026_Nold",
    "metrics_csv": "trueness_metrics.csv",
    "precision_csv": "precision_metrics.csv",
    "summary_csv": "summary_stats.csv"
  }
}
```

Use `scripts/gen_nold_study_config.py` to auto-generate the patient study config from a flat directory of `*_aligned.stl` files.

---

## Output Files

**trueness_metrics.csv** – One row per scan:

| Column | Description |
|--------|-------------|
| Observation_ID | Sequential integer |
| Scanner_Model | Scanner identifier |
| Group_ID | Group label (SKD level, patient ID, etc.) |
| Repetition_ID | Repetition number extracted from filename |
| Triangles | Face count |
| Edge_mm | Mean triangle edge length (mm) |
| AspRatio | Mean triangle aspect ratio (max/min edge) |
| MaxAspRatio | Maximum triangle aspect ratio |
| ATI | Adaptive Tessellation Index (Spearman correlation of curvature vs. triangle area) |
| DensHighK | Triangle density at high-curvature regions |
| DensLowK | Triangle density at low-curvature regions |
| RMS_mm | Root mean square distance to GPA reference (mm) |
| MAD_mm | Mean absolute deviation (mm) |
| H100_mm | Hausdorff distance 100th percentile (mm) |
| H95_mm | Hausdorff distance 95th percentile (mm) |
| Bias_mm | Signed mean distance — positive = scan outside reference (mm) |
| Coverage_pct | Percentage of reference surface covered |
| Boundary_mm | Total open boundary edge length (mm) |
| Holes | Number of topological holes |
| Stitch_deg | Maximum stitching angle at open boundaries (degrees) |
| Vertices_Included | Vertices passing ROI filter |
| Vertices_Total | Total vertices in mesh |
| File_Path | Absolute path to source STL/OBJ file |

**precision_metrics.csv** – One row per scanner per group:
- Scanner_Model, Group_ID, Precision_MeanRMS_mm, Precision_SD_mm, Coefficient_of_Variation, Pairwise_Count

**summary_stats.csv** – Aggregated trueness statistics per scanner per group:
- Scanner_Model, Group_ID, N, Mean_RMS_mm, SD_RMS_mm, Min_RMS_mm, Max_RMS_mm

---

## Dependencies

| Library | Version | Notes |
|---------|---------|-------|
| Qt | 6.2+ | Custom VTK build with Qt6 |
| VTK | 9.3 | Custom build in `~/VTK-install-linux` |
| CGAL | 6.0.1 | Computational geometry |
| Eigen | 3.4.0 | Matrix math for ICP |
| nanoflann | 1.7 | Header-only KD-tree |
| yaml-cpp | (optional) | For YAML config files |

---

## Building

```bash
mkdir build && cd build
cmake .. -DCMAKE_BUILD_TYPE=Release
make -j4
```

---

## Documentation

- [User Manual](docs/user-manual.md) – Complete workflow guide
- [Study Config Reference](docs/study-config-reference.md) – JSON configuration field reference
- [Developer Handoff](docs/developer-handoff.md) – Architecture, algorithms, changelog
- [Metric Interpretation](docs/metric-interpretation.md) – Understanding output values
- [QC Workflow](docs/QC-WORKFLOW-PLAN.md) – Quality control procedures

---

## Author

**Prof. Dr. Karl-Heinz Kunzelmann**

## License

This project is fully open-source and free to use under the **GNU General Public License v2 or later (GPL-2.0-or-later)**. You are welcome to download, modify, and self-host it at no cost. See the [LICENSE](LICENSE) file for details.


## Commercial Support, Consulting, and Training

However, if you are using this software in a professional, academic, or enterprise environment, I offer dedicated services to ensure your workflow runs smoothly and efficiently.

### What I Offer:

* **Personalized Instruction & Training:** While the core workflow is thoroughly documented, mastering the underlying concepts and navigating specific project edge cases often benefits from hands-on guidance. I offer tailored training sessions to get your team up to speed quickly.
* **Custom Development & Consulting:** Need a specific feature, third-party integration, or performance optimization? Let's discuss your requirements to tailor the software to your exact infrastructure.

### Get in Touch

If your organization requires commercial backing, custom training, or development services, please reach out:

* **Website:** [www.kunzelmann.de]
