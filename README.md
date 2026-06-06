# DentScanComparePro

Automated batch evaluation of dental intraoral scanner accuracy.  Computes ISO 5725/12836-
compliant trueness and precision metrics across multiple scanners and clinical conditions
(SKD levels = inter-incisor distance).

Based on the core algorithms from [DentScanCompare](../DentScanCompare/), extended with:
- JSON/YAML-driven batch configuration
- Automated file discovery via glob patterns
- Per-SKD-level GPA alignment with incremental save/resume
- Quality Control (QC) workflow with visual verification
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
- You have a large study (multiple scanners × multiple SKD levels × repetitions)
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
- JSON configuration for scanners, groups (SKD levels), and output paths
- Automatic file discovery via glob patterns with scanner ID matching
- Incremental save after each group (resume after interruption)
- Progress tracking via `.batch_progress.json`

### Quality Control (QC) Workflow
- Export GPA mean meshes (STL), transform matrices (JSON), segmented meshes
- Thumbnail grid for quick visual review with accept/flag workflow
- Interactive re-registration dialog with landmark-based Kabsch alignment
- Detailed overlay view (reference wireframe + distance-colored scan)

### Metrics Output
- **Trueness**: RMS, MAD, Hausdorff (P95, Max), signed mean, coverage rate
- **Precision**: Pairwise RMS between scan repetitions per scanner per SKD
- **Summary**: Aggregated statistics per scanner per SKD level

### Integration
- Load pre-computed transforms from DentScanAlign
- Apply tooth segmentation masks from ROI templates
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
| `--pre-aligned` | Skip GPA computation (scans already coarsely aligned) |
| `--verbose` | Print detailed progress information |

---

## Study Configuration (JSON)

```json
{
  "study": {
    "name": "Scanner_Comparison_2024",
    "description": "6 scanners × 7 SKD levels"
  },
  "scanners": [
    {"id": "Primescan", "patterns": ["*Primescan*", "*PS*"]},
    {"id": "Trios5", "patterns": ["*Trios*", "*T5*"]},
    {"id": "iTeroLumina", "patterns": ["*iTero*", "*Lumina*"]}
  ],
  "groups": [
    {"id": "SKD_20", "skd_mm": 20, "file_patterns": ["**/SKD_20/*.stl"]},
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

---

## Output Files

**trueness_metrics.csv** – One row per scan:
- Observation_ID, Scanner_Model, SKD_Value, Repetition_ID
- Trueness_RMS_mm, Trueness_MeanAbs_mm, Trueness_Max_mm, Trueness_P95_mm
- Signed_Mean_mm, Coverage_Rate_pct, Vertices_Included, Vertices_Total, File_Path

**precision_metrics.csv** – One row per scanner per SKD:
- Scanner_Model, SKD_Value, Precision_MeanRMS_mm, Precision_SD_mm, Coefficient_of_Variation, Pairwise_Count

**summary_stats.csv** – Aggregated trueness statistics:
- Scanner_Model, SKD_Value, N, Mean_RMS_mm, SD_RMS_mm, Min_RMS_mm, Max_RMS_mm

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
- [Developer Handoff](docs/developer-handoff.md) – Architecture, algorithms, changelog
- [Metric Interpretation](docs/metric-interpretation.md) – Understanding output values
- [QC Workflow](docs/QC-WORKFLOW-PLAN.md) – Quality control procedures

---

## Author

**Prof. Dr. Karl-Heinz Kunzelmann**

## License

This project is fully open-source and free to use under the **[GNU General Public License v2 (GPL v2)]** license. You are welcome to download, modify, and self-host it at no cost.


## Commercial Support, Consulting, and Training

However, if you are using this software in a professional, academic, or enterprise environment, I offer dedicated services to ensure your workflow runs smoothly and efficiently.

### What I Offer:

* **Personalized Instruction & Training:** While the core workflow is thoroughly documented, mastering the underlying concepts and navigating specific project edge cases often benefits from hands-on guidance. I offer tailored training sessions to get your team up to speed quickly.
* **Custom Development & Consulting:** Need a specific feature, third-party integration, or performance optimization? Let's discuss your requirements to tailor the software to your exact infrastructure.

### Get in Touch

If your organization requires commercial backing, custom training, or development services, please reach out:

* **Website:** [www.kunzelmann.de]
