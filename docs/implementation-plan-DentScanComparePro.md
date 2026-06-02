# Plan: DentScanBatch - Automated Multi-Factor Scanner Evaluation

## Problem Analysis

You have a multi-factor experimental design:
- **6 scanners** (Fussen S6000, iTeroLumina, Medit i700, Primescan, Trios 4, Trios 5)
- **6-7 SKD levels** (18-30 mm inter-incisor distance)
- **5 repetitions** per cell
- **~180 STL files** total

The goal: Compute ISO 5725/12836-compliant trueness and precision metrics with proper ROI clipping, then output a structured CSV for statistical analysis (Two-Way ANOVA with Scanner × SKD interaction).

### Key Challenges:
1. **Background clipping**: Scans include mounting fixtures, scanner artifacts
2. **Inconsistent file/folder naming**: Requires flexible parsing
3. **Interactive ROI definition**: Must be fast and propagate across groups
4. **Statistical grouping**: Need per-SKD reference surfaces (GPA within each SKD group)

---

## Proposed Architecture: Template-Driven Batch Processing

Instead of embedding macro logic into DentScanCompare, create a **separate new application** with dual modes (GUI + CLI):

```
┌─────────────────────────────────────────────────────────────────────┐
│                    DentScanBatch (Single Application)               │
├─────────────────────────────────────────────────────────────────────┤
│                                                                     │
│  ┌────────────────────────────────────────────────────────────────┐ │
│  │                         GUI Mode                                │ │
│  │  ┌─────────────────────┐    ┌────────────────────────────────┐ │ │
│  │  │  Template Editor    │    │     Batch Runner Tab           │ │ │
│  │  │  • Load 1 scan/group│    │     • File discovery preview   │ │ │
│  │  │  • Define ROI       │    │     • Progress monitoring      │ │ │
│  │  │  • Live preview     │    │     • Start/Cancel controls    │ │ │
│  │  └─────────────────────┘    └────────────────────────────────┘ │ │
│  └────────────────────────────────────────────────────────────────┘ │
│                              │                                      │
│                         study.yaml                                  │
│                              │                                      │
│  ┌────────────────────────────────────────────────────────────────┐ │
│  │                        CLI Mode (--batch)                       │ │
│  │  • Headless processing     • Console progress output           │ │
│  │  • Same engine as GUI      • Scriptable/automatable            │ │
│  └────────────────────────────────────────────────────────────────┘ │
│                              │                                      │
│                              ▼                                      │
│                   ┌──────────────────────────────┐                  │
│                   │   long_format_metrics.csv    │                  │
│                   │   → R / Python / SPSS        │                  │
│                   └──────────────────────────────┘                  │
└─────────────────────────────────────────────────────────────────────┘
```

---

## Key Abstraction: ROI as Transferable Geometry

The critical insight: **All scans within an SKD group share the same physical test surface geometry**. Therefore, an ROI defined on ONE representative scan can transfer to ALL scans in that group after ICP alignment.

### ROI Definition Methods (All Available):

| Method | Description | Use Case |
|--------|-------------|----------|
| **Z-Plane Slab** | Keep vertices in `[Z_max - below, Z_max + above]` | Quick setup, occlusal-focused analysis |
| **Bounding Box** | Axis-aligned box defined interactively | Rectangular ROI, exclude scan bed edges |
| **Manual Brush** | Paint inclusion/exclusion zones on mesh surface | Fine-tuning edge cases, artifact removal |
| **Statistical** | Auto-exclude vertices > Nσ from mean distance | Automatic outlier removal (applied after ROI) |

Methods can be **combined** in priority order:
1. Bounding box clips to rectangular region
2. Z-plane slab further restricts within the box
3. Manual brush adds/removes specific vertices
4. Statistical σ-clip removes remaining outliers

This layered approach allows quick rough cropping (box + Z-plane) followed by targeted refinement (brush) only where needed.

---

## Study Configuration File (`study.yaml`)

A declarative configuration drives the entire pipeline:

```yaml
study:
  name: "DefektIIa_MultiScanner_SKD_Evaluation"
  version: 1

  # How to compute reference surface within each group
  reference_strategy: "gpa_mean"  # or "fixed_scanner: Primescan"

  # Alignment parameters (reused from DentScanCompare)
  alignment:
    max_icp_iterations: 100
    convergence_threshold_mm: 0.01
    use_pca_coarse: true
    use_4orientation_test: true

# Scanner metadata for output labeling
scanners:
  - id: "FussenS6000"
    patterns: ["Fussen*S6000*", "FussenS6000*"]
  - id: "iTeroLumina"
    patterns: ["iTero*Lumina*", "iTeroLumina*"]
  - id: "Mediti700"
    patterns: ["Medit*i700*", "Mediti700*"]
  - id: "Primescan"
    patterns: ["Primescan*"]
  - id: "Trios4"
    patterns: ["Trios*4*"]
  - id: "Trios5"
    patterns: ["Trios*5*"]

# Each SKD level is a separate analysis group
groups:
  - id: "SKD_20"
    skd_mm: 20
    file_patterns:
      - "**/SKD*20*/*.stl"
      - "**/SKD 20/**/*.stl"
      - "**/20mm*/*.stl"
    roi:
      method: "z_plane_slab"
      above_occlusal_mm: 2.0
      below_occlusal_mm: 12.0
    outlier:
      method: "sigma_clip"
      threshold_sigma: 3.0

  - id: "SKD_22"
    skd_mm: 22
    file_patterns:
      - "**/SKD*22*/*.stl"
      - "**/22mm*/*.stl"
    roi:
      inherit_from: "SKD_20"  # Template inheritance
    outlier:
      inherit_from: "SKD_20"

  # ... SKD_24, SKD_26, SKD_28, SKD_30 similarly

# What metrics to compute
metrics:
  trueness:
    - name: "RMS"
      formula: "sqrt(mean(d^2))"
    - name: "MeanAbsolute"
      formula: "mean(abs(d))"
    - name: "Max"
      formula: "max(abs(d))"
    - name: "Percentile95"
      formula: "percentile(abs(d), 95)"

  precision:
    - name: "IntraScannerRMS"
      description: "Pairwise RMS within scanner's 5 repetitions"
    - name: "CoeffVariation"
      description: "SD/mean of repetition RMS values"

# Output configuration
output:
  base_dir: "./results"
  metrics_csv: "long_format_metrics.csv"
  summary_csv: "summary_by_scanner_skd.csv"
  precision_csv: "precision_matrix.csv"
```

---

## GUI Mode: Template Editor Tab

### UI Layout

```
┌─────────────────────────────────────────────────────────────────────────┐
│  DentScanBatch - Template Editor                                   [X]  │
├─────────────────────────────────────────────────────────────────────────┤
│ [Template Editor] [Batch Runner]                                        │
├───────────────────────┬─────────────────────────────────────────────────┤
│ SKD Groups            │                                                 │
│ ┌───────────────────┐ │           3D Viewport (VTK)                     │
│ │ ● SKD 18 (iTero)  │ │                                                 │
│ │ ● SKD 20  [✓]     │ │      ┌────────────────────────────┐             │
│ │ ○ SKD 22          │ │      │                            │             │
│ │ ○ SKD 24          │ │      │   Representative Scan      │             │
│ │ ○ SKD 26          │ │      │                            │             │
│ │ ○ SKD 28          │ │      │   ivory = included         │             │
│ │ ○ SKD 30          │ │      │   grey  = excluded         │             │
│ └───────────────────┘ │      │   red   = brush-excluded   │             │
│                       │      │                            │             │
│ ─── ROI Method ────── │      └────────────────────────────┘             │
│ ┌───────────────────┐ │                                                 │
│ │ [Z-Plane] [Box]   │ │  ─── Tool Mode ───────────────────────────     │
│ │ [Brush+] [Brush-] │ │  ○ Rotate   ● Include Brush   ○ Exclude Brush  │
│ └───────────────────┘ │  Brush size: [====●====] 3.0 mm                │
│                       │                                                 │
│ ─── Z-Plane ───────── │  ─── Statistics ──────────────────────────     │
│ Above: [2.0 ] mm      │  Vertices: 12,456 / 15,234 (81.8%)             │
│ Below: [12.0] mm      │  BBox:     active (45 x 32 x 18 mm)            │
│                       │  Brush:    3 include zones, 1 exclude zone     │
│ ─── Bounding Box ──── │                                                 │
│ [Define Box...]       │  ─── Z Profile ───────────────────────────     │
│ [Clear Box]           │       ▲ +2mm ════════════════════              │
│                       │       │      ░░ occlusal ░░                    │
│ ─── Outlier ───────── │       │══════════════════════════              │
│ σ threshold: [3.0]    │    Z  │    included region                     │
│                       │       │══════════════════════════              │
│ ───────────────────── │       ▼ -12mm ════════════════════             │
│ [Apply to All Groups] │                                                 │
│ [Copy from SKD 20]    │                                                 │
│ ───────────────────── │                                                 │
│ [Load study.yaml]     │                                                 │
│ [Save study.yaml]     │                                                 │
└───────────────────────┴─────────────────────────────────────────────────┘
```

### Key Features:

1. **Load one representative scan per SKD group** (not all 180)
2. **Live preview**: All ROI changes immediately update vertex colors
3. **Layered ROI**: Box → Z-plane → Brush → σ-clip (applied in order)
4. **Brush tools**: Click-drag on mesh to include/exclude vertices
5. **Template propagation**: "Apply to All" copies current settings to all groups
6. **Copy from**: Import settings from another SKD group as starting point
7. **Statistics feedback**: Live vertex counts and coverage percentage

### Brush Tool Implementation:

```cpp
// Brush zones stored as list of spheres
struct BrushZone {
    std::array<double, 3> center;
    double radius;
    bool include;  // true = force include, false = force exclude
};

// Applied after geometric ROI (box + Z-plane)
for (vertex v : mesh.vertices()) {
    bool inROI = bbox.contains(v) && zSlab.contains(v);

    // Brush overrides geometric ROI
    for (const auto& zone : brushZones) {
        if (distance(v, zone.center) < zone.radius) {
            inROI = zone.include;
        }
    }

    // Final σ-clip on distance values (only for included vertices)
    if (inROI && abs(distance[v]) > sigma_threshold * stddev) {
        inROI = false;
    }

    mask[v] = inROI;
}
```

---

## GUI Mode: Batch Runner Tab

```
┌─────────────────────────────────────────────────────────────────────────┐
│  DentScanBatch - Batch Runner                                      [X]  │
├─────────────────────────────────────────────────────────────────────────┤
│ [Template Editor] [Batch Runner]                                        │
├─────────────────────────────────────────────────────────────────────────┤
│                                                                         │
│  Study Configuration                                                    │
│  ┌────────────────────────────────────────────────────────────────────┐ │
│  │ Study file:  [/home/user/study.yaml                    ] [Browse] │ │
│  │ Data root:   [/home/user/scanner_data                  ] [Browse] │ │
│  │ Output dir:  [/home/user/results                       ] [Browse] │ │
│  └────────────────────────────────────────────────────────────────────┘ │
│                                                                         │
│  File Discovery Preview                                                 │
│  ┌────────────────────────────────────────────────────────────────────┐ │
│  │ SKD 18:  5 files (iTeroLumina only)                                │ │
│  │ SKD 20: 30 files (6 scanners × 5 reps)                             │ │
│  │ SKD 22: 30 files (6 scanners × 5 reps)                             │ │
│  │ SKD 24: 30 files (6 scanners × 5 reps)                             │ │
│  │ SKD 26: 30 files (6 scanners × 5 reps)                             │ │
│  │ SKD 28: 30 files (6 scanners × 5 reps)                             │ │
│  │ SKD 30: 30 files (6 scanners × 5 reps)                             │ │
│  │ ──────────────────────────────────────                             │ │
│  │ Total: 185 files                                                   │ │
│  └────────────────────────────────────────────────────────────────────┘ │
│                                                                         │
│  Processing Progress                                                    │
│  ┌────────────────────────────────────────────────────────────────────┐ │
│  │ [■■■■■■■■■■■■■■■■■■░░░░░░░░░░░░░░░░░░░░░░] 45%                     │ │
│  │                                                                    │ │
│  │ Current: SKD 22 - Running GPA alignment (scan 4/30)               │ │
│  │                                                                    │ │
│  │ Completed:                                                         │ │
│  │   ✓ SKD 18: 5/5 files (0 errors)                                  │ │
│  │   ✓ SKD 20: 30/30 files (0 errors)                                │ │
│  │   ◐ SKD 22: 4/30 files...                                         │ │
│  └────────────────────────────────────────────────────────────────────┘ │
│                                                                         │
│  [▶ Start Batch]  [⏹ Cancel]                                           │
│                                                                         │
└─────────────────────────────────────────────────────────────────────────┘
```

## CLI Mode: Headless Batch Processing

```bash
$ dentscan-batch --batch --study study.yaml --data-root /path/to/scanners --output ./results

DentScanBatch v1.0 - Headless Mode
==================================
Study: DefektIIa_MultiScanner_SKD_Evaluation
Data root: /path/to/scanners
Output: ./results

[1/7] Processing group SKD_18...
  Files: 5 (iTeroLumina only)
  Loading STL files... done
  Running GPA alignment... done (converged in 12 iterations)
  Computing distance fields... done
  Applying ROI mask... done (82.3% vertices retained)
  Computing metrics... done

[2/7] Processing group SKD_20...
  Files: 30 (6 scanners × 5 reps)
  ...

[7/7] Processing group SKD_30... done

=====================================
Batch complete.

Output files:
  ./results/long_format_metrics.csv    (185 rows)
  ./results/precision_matrix.csv       (42 rows)
  ./results/summary_statistics.csv     (42 rows)
  ./results/processing_log.txt

Elapsed time: 14m 32s
```

### Processing Pipeline per SKD Group:

```
1. File Discovery
   └─ Glob patterns from study.yaml → list of STL paths
   └─ Parse scanner name and repetition ID from path/filename

2. Load & Preprocess
   └─ STLReader::read() for each file
   └─ CurvatureAnalysis::compute() (needed for occlusal plane detection)

3. GPA Alignment (within group)
   └─ PCA coarse alignment
   └─ 4-orientation test
   └─ Iterative ICP refinement
   └─ Mean mesh computation

4. ROI Application
   └─ Fit occlusal plane to GPA mean
   └─ Apply Z-slab from template
   └─ Apply σ-clip outlier removal
   └─ Generate per-scan vertex mask

5. Trueness Metrics
   └─ For each scan: DistanceField against GPA mean
   └─ Filter by ROI mask
   └─ Compute RMS, MeanAbs, Max, P95

6. Precision Metrics
   └─ For each scanner within group:
     └─ Pairwise DistanceField among 5 repetitions
     └─ 10 combinations → mean intra-scanner RMS

7. Export
   └─ Append rows to long_format_metrics.csv
```

### Output CSV Schema:

```csv
Observation_ID,Scanner_Model,SKD_Value,Repetition_ID,Trueness_RMS_mm,Trueness_MeanAbs_mm,Trueness_Max_mm,Trueness_P95_mm,Vertices_Included,Vertices_Total,File_Path
001,FussenS6000,20,1,0.127,0.098,0.512,0.245,12456,15234,Fussen S6000/.../r1.stl
002,FussenS6000,20,2,0.131,0.101,0.498,0.251,12501,15198,Fussen S6000/.../r2.stl
...
```

Precision metrics go in a separate file (one row per scanner×SKD):

```csv
Scanner_Model,SKD_Value,Precision_MeanRMS_mm,Precision_SD_mm,Precision_CV,Pairwise_Count
FussenS6000,20,0.034,0.008,0.235,10
FussenS6000,22,0.031,0.006,0.194,10
...
```

---

## Implementation Strategy

### Project Structure

```
DentScanBatch/
├── CMakeLists.txt
├── src/
│   ├── main.cpp                    # Mode dispatch (GUI vs CLI)
│   │
│   ├── core/                       # Reused from DentScanCompare
│   │   ├── Mesh.h
│   │   ├── STLReader.{h,cpp}
│   │   ├── CurvatureAnalysis.{h,cpp}
│   │   ├── ICPRegistration.{h,cpp}
│   │   ├── GPAReference.{h,cpp}
│   │   ├── DistanceField.{h,cpp}
│   │   └── MetricReport.h
│   │
│   ├── config/                     # Study configuration
│   │   ├── StudyConfig.{h,cpp}     # YAML parser + data structures
│   │   ├── ROIConfig.{h,cpp}       # ROI parameter types
│   │   └── FileDiscovery.{h,cpp}   # Glob + regex file matching
│   │
│   ├── batch/                      # Batch processing engine
│   │   ├── BatchRunner.{h,cpp}     # Orchestrates per-group processing
│   │   ├── GroupProcessor.{h,cpp}  # Single SKD group pipeline
│   │   ├── MetricsComputer.{h,cpp} # Trueness + precision calculations
│   │   └── CSVWriter.{h,cpp}       # Output formatting
│   │
│   └── gui/                        # Qt GUI components
│       ├── MainWindow.{h,cpp}      # Tab container
│       ├── TemplateEditorTab.{h,cpp}
│       ├── BatchRunnerTab.{h,cpp}
│       ├── ROIPreviewWidget.{h,cpp}  # VTK 3D view with ROI overlay
│       └── BrushTool.{h,cpp}         # Brush inclusion/exclusion logic
│
├── data/
│   └── default_study.yaml          # Template configuration
│
└── docs/
    └── study_config_schema.md      # YAML format documentation
```

### Phase 1: Core Library Extraction

Copy computation classes from DentScanCompare (no modifications needed):
- `Mesh.h`, `STLReader`, `CurvatureAnalysis`, `ICPRegistration`
- `GPAReference`, `DistanceField`, `MetricReport`

### Phase 2: Study Configuration System

```cpp
// src/config/ROIConfig.h
struct BoundingBox {
    std::array<double, 3> min;
    std::array<double, 3> max;
    bool active = false;
};

struct ZPlaneSlab {
    double above_mm = 2.0;
    double below_mm = 12.0;
    bool active = true;
};

struct BrushZone {
    std::array<double, 3> center;
    double radius_mm;
    bool include;  // true = include, false = exclude
};

struct ROIConfig {
    BoundingBox bbox;
    ZPlaneSlab zPlane;
    std::vector<BrushZone> brushZones;
    double outlierSigma = 3.0;
};

// src/config/StudyConfig.h
struct GroupConfig {
    std::string id;           // "SKD_20"
    int skd_mm;               // 20
    std::vector<std::string> filePatterns;
    ROIConfig roi;
    std::string representativeScan;  // Path for template editor preview
};

struct StudyConfig {
    std::string name;
    std::string dataRootDir;
    std::vector<GroupConfig> groups;

    // Alignment parameters
    int maxIcpIterations = 100;
    double convergenceThreshold = 0.01;

    // Output configuration
    std::string outputDir;

    static StudyConfig loadFromYAML(const QString& path);
    void saveToYAML(const QString& path) const;
};
```

### Phase 3: Batch Processing Engine

```cpp
// src/batch/GroupProcessor.h
class GroupProcessor : public QObject {
    Q_OBJECT
public:
    struct Result {
        std::vector<MetricReport> truenessReports;  // One per scan
        std::vector<PrecisionReport> precisionReports;  // One per scanner
        std::shared_ptr<SurfaceMesh> gpaMean;
    };

    Result process(const GroupConfig& group,
                   const std::vector<QString>& filePaths);

signals:
    void progressUpdated(int current, int total, const QString& status);
    void scanProcessed(const QString& filename, bool success);
};
```

### Phase 4: GUI Components

Reuse `VTKMeshWidget` from DentScanCompare with extensions:
- Add `showROIMask(const std::vector<bool>& mask)` method
- Add `setBrushMode(bool include)` for brush tool
- Add `getBrushZones()` to retrieve painted regions

---

## Verification Strategy

1. **Unit tests for core library**: Load known STL, verify vertex counts, ICP convergence
2. **Golden-file regression**: Process a small subset (2 scanners × 2 SKD × 2 reps = 8 files), compare output CSV to expected values
3. **Visual inspection**: ROI Template Editor shows preview; user confirms ROI looks correct
4. **Statistical sanity check**: Output CSV can be loaded in R/Python; verify ANOVA runs without errors

---

## Design Decisions (Confirmed)

1. **ROI strategy**: Full flexibility
   - Z-plane slab (primary, fastest)
   - Axis-aligned bounding box
   - Manual brush-based inclusion/exclusion (for edge cases)

2. **GPA reference**: Per-SKD group
   - Compute separate GPA mean for each SKD level (20, 22, 24, 26, 28, 30, and 18 for iTero)
   - Proper for factorial ANOVA design

3. **iTero 18mm**: Include as separate SKD level
   - Accept unbalanced design for that cell (only iTeroLumina has 18mm data)
   - Statistical software can handle unbalanced factorial designs

4. **Architecture**: Single application with dual modes
   - GUI mode (default): Interactive ROI template editing
   - CLI mode (`--batch`): Headless batch processing

---

## Final Architecture: Single Application Design

```
DentScanBatch
├── GUI Mode (default launch)
│   ├── Tab 1: Template Editor
│   │   ├── Load representative scan per SKD group
│   │   ├── Interactive ROI definition (Z-plane, bbox, brush)
│   │   ├── Live preview with included/excluded vertex coloring
│   │   └── Save/load study.yaml configuration
│   │
│   └── Tab 2: Batch Runner
│       ├── Select study.yaml and data root directory
│       ├── Progress display for each SKD group
│       ├── Cancel button for long-running jobs
│       └── Summary when complete
│
└── CLI Mode (--batch flag)
    └── Headless processing with console progress output
```

### Command-Line Interface

```bash
# Launch GUI (default)
$ dentscan-batch

# Run batch processing headlessly
$ dentscan-batch --batch \
    --study study.yaml \
    --data-root /path/to/scanner/folders \
    --output ./results

# Options
$ dentscan-batch --help
  --batch          Run in headless batch mode
  --study FILE     Path to study.yaml configuration
  --data-root DIR  Root directory containing scanner folders
  --output DIR     Output directory for CSV files
  --parallel N     Number of parallel workers (default: CPU cores - 1)
  --verbose        Print detailed progress
```
