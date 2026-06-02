# DentScanComparePro – Implementation Prompt (Part 2)

## Context

You are continuing development of **DentScanComparePro**, a Qt/VTK/CGAL application for automated batch evaluation of dental intraoral scanner accuracy. The batch processing core is complete and tested. This prompt covers the remaining work to complete the project.

## Quick Start – Files to Read First

Read these files in order to understand the current state:

1. **`CLAUDE.md`** (project root) – Build environment, coding standards, key abstractions
2. **`docs/developer-handoff.md`** – Detailed technical documentation, pitfalls, current status
3. **`docs/IMPLEMENTATION-PROMPT.md`** – Original implementation plan with full requirements
4. **`src/main.cpp`** – Entry point, CLI argument parsing, dual-mode structure
5. **`src/batch/GroupProcessor.cpp`** – Core processing pipeline (load → GPA → metrics)
6. **`src/config/StudyConfig.h`** – Configuration data structures
7. **`data/test_single_group.json`** – Working test configuration example

## What Has Been Completed

### Phase 1: Project Setup
- CMakeLists.txt with Qt6, VTK 9.3, CGAL 6.0, Eigen3
- Core files copied from DentScanCompare (STLReader, CurvatureAnalysis, ICPRegistration, GPAReference, DistanceField)

### Phase 2: Configuration System
- `ROIConfig.h` – BoundingBox, ZPlaneSlab, BrushZone structures
- `StudyConfig.{h,cpp}` – JSON configuration parsing (YAML stubbed but not fully working)
- `FileDiscovery.{h,cpp}` – Glob pattern file discovery with QDirIterator

### Phase 3: Batch Processing
- `GroupProcessor.{h,cpp}` – Processes one SKD group (load → curvature → GPA → distances → metrics)
- `BatchRunner.{h,cpp}` – Orchestrates all groups, handles cancellation
- `CSVWriter.{h,cpp}` – Writes trueness, precision, and summary CSV files
- `DistanceField::computePairwise()` – Optimized pairwise distance computation

### Tested and Working
```bash
./src/DentScanComparePro --batch \
    --study ../data/test_single_group.json \
    --data-root /KHKsData/usr2/daten/P2026-Kessler-ScanVergleich/IOS \
    --output /tmp/test_small \
    --verbose
```
- Successfully processes 5 Primescan scans at SKD 20
- Produces trueness metrics (RMS 0.032-0.074 mm)
- Produces precision metrics (10 pairwise comparisons, Mean RMS 0.269 mm)

## What Remains To Be Done

### Priority 1: Full Multi-Scanner Batch Run

Create a complete study configuration for all 6 scanners and all SKD levels:

**Scanners:**
- Primescan
- Trios5
- Medit i700
- iTero Lumina
- Fussen S6000
- (possibly others in the data directory)

**SKD Levels:** 20mm, 30mm, 40mm, 50mm, 60mm (and possibly 18mm for iTero)

**Tasks:**
1. Explore `/KHKsData/usr2/daten/P2026-Kessler-ScanVergleich/IOS/` to understand directory structure
2. Create `data/full_study.json` with all scanners and SKD levels
3. Run full batch and verify output
4. Handle any edge cases (missing files, different naming conventions)

### Priority 2: ROI Integration

The ROIConfig structures exist but are not connected to the processing pipeline.

**Files to modify:**
- `src/batch/GroupProcessor.cpp` – Apply ROI filtering in `computeTruenessMetrics()` and `computePrecisionMetrics()`
- `src/config/ROIConfig.h` – Verify `isInROI()` implementation

**ROI Filter Priority (from DistanceField.h):**
1. toothMask (highest) – Per-vertex segmentation mask
2. plane.active – Occlusal plane slab filter
3. zWindowMm > 0 – Simple Z-max window (legacy)
4. (none) – All vertices

### Priority 3: GUI Mode – ROI Template Editor (Phase 4)

The main.cpp has placeholder for GUI mode. Implement interactive ROI template editing.

**Key features needed:**
1. Load one representative scan per SKD group
2. Interactive 3D visualization (VTKMeshWidget already available)
3. Define ROI via:
   - Bounding box (click-drag in 3D)
   - Occlusal plane fitting (pick 3+ cusp points)
   - Brush tool for fine-tuning
4. Save ROI template to JSON
5. Apply template to batch processing

**Files to create:**
- `src/gui/MainWindow.{h,cpp}` – Main application window
- `src/gui/ROITemplateEditor.{h,cpp}` – ROI editing widget
- `src/gui/BatchProgressDialog.{h,cpp}` – Progress display for batch runs

**Reference implementation:** `/home/kkunzelm/claude-code/DentScanCompare/src/MainWindow.cpp` has similar functionality for tooth segmentation and occlusal plane fitting.

### Priority 4: Tooth Segmentation Integration

Port ToothSegmentation from DentScanCompare for anatomical ROI definition.

**Files to copy:**
- `/home/kkunzelm/claude-code/DentScanCompare/src/core/ToothSegmentation.{h,cpp}`

**Integration points:**
- Use tooth mask as highest-priority ROI filter
- Allow defining tooth seeds in GUI, save to configuration
- Apply same mask to all scans in group (after GPA alignment)

### Priority 5: Statistical Output Enhancement

Current CSV output is basic. Consider adding:
- Bland-Altman analysis data
- Per-tooth metrics (if segmentation implemented)
- Confidence intervals
- Ready-to-use R/SPSS import format

## Technical Notes

### Scanner Data Location
```
/KHKsData/usr2/daten/P2026-Kessler-ScanVergleich/IOS/
├── Primescan/
│   └── Defekt 2a/
│       ├── SKD 20/
│       ├── SKD 30/
│       └── ...
├── Trios5/
├── Medit/
└── ...
```

### Known Scanner ID Patterns
From test runs, files contain scanner names in paths:
- `*Primescan*` → Primescan
- `*Trios*` → Trios5
- `*Medit*` or `*i700*` → Medit i700
- `*iTero*` or `*Lumina*` → iTero Lumina
- `*Fussen*` or `*S6000*` → Fussen S6000

### Build Commands
```bash
cd /home/kkunzelm/claude-code/DentScanComparePro/build
cmake .. -DCMAKE_BUILD_TYPE=Release
make -j4
```

### Test Command
```bash
./src/DentScanComparePro --batch \
    --study ../data/test_single_group.json \
    --data-root /KHKsData/usr2/daten/P2026-Kessler-ScanVergleich/IOS \
    --output /tmp/test_output \
    --verbose
```

## Critical Pitfalls to Remember

1. **CGAL 6.0 property_map**: Returns `std::optional`, not `std::pair`
2. **VTK threading**: All VTK calls must be on main thread
3. **STL winding**: Primescan has reversed normals – STLReader handles this
4. **ICP 28mm offset**: FussenS6000/iTeroLumina need PCA coarse alignment first
5. **Qt signal cascades**: Use `QSignalBlocker` for programmatic checkbox changes
6. **File discovery**: Use QDirIterator, not recursive functions (performance)

## Session Goals

Suggested order of work for next session:

1. **Explore scanner data** – Understand full directory structure
2. **Create full_study.json** – Configuration for all scanners/SKD levels
3. **Run full batch** – Process all data, identify any issues
4. **Begin GUI work** – Start MainWindow with basic file loading

## Output Expectations

After completing Priority 1 (full batch run), you should have:
- `trueness_metrics.csv` with ~150+ rows (6 scanners × 5 SKD × 5 repetitions)
- `precision_metrics.csv` with ~30 rows (6 scanners × 5 SKD)
- `summary_stats.csv` with aggregated statistics

These files should be directly importable into R or SPSS for statistical analysis.
