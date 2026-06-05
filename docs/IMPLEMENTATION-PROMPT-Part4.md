# DentScanComparePro – Implementation Prompt Part 4

## Context

This is a continuation prompt for DentScanComparePro, a Qt/VTK/CGAL application for automated batch evaluation of dental intraoral scanner accuracy.

**Previous work completed:**
- Full batch processing pipeline with GPA alignment and metrics computation
- GUI with 5 tabs: Study Config, ROI Template Editor, Batch Processing, Results, QC Review
- Tooth segmentation integration
- ROI template save/load with batch integration
- QC workflow infrastructure (partially implemented)

**Key files to read first:**
- `docs/developer-handoff.md` – Complete project documentation
- `docs/QC-WORKFLOW-PLAN.md` – Original QC workflow design
- `src/qc/` – QC module implementation

---

## Current State

### What Works
1. **Batch processing** produces:
   - `trueness_metrics.csv`, `precision_metrics.csv`, `summary_stats.csv`
   - `qc/gpa_means/*.stl` – GPA reference meshes per group
   - `qc/transforms/*.json` – Transform matrices + metrics per scan

2. **QC infrastructure** exists but needs testing:
   - `QCExporter` – Exports GPA means and transforms
   - `ErrandManager` – Tracks accept/reject status
   - `LandmarkRegistration` – Kabsch algorithm + ICP refinement
   - `QCReviewWidget` – Thumbnail grid UI
   - `ErrandResolutionDialog` – Interactive re-registration

### What Doesn't Work
1. **Difference image export** – Disabled in batch mode due to VTK GLEW crash:
   ```
   vtkGenericOpenGLRenderWindow: GLEW could not be initialized: Missing GL version
   ```
   - Occurs because VTK offscreen rendering requires OpenGL context
   - Even `xvfb-run` doesn't help (GLEW still fails)
   - Images skip silently; GPA meshes and transforms still export

2. **QC Review UI** – Untested (no difference images to display)

3. **Errand Resolution** – Untested

---

## Priority Tasks

### Option A: Fix VTK Headless Rendering (Recommended)

The VTK build (`~/VTK-install-linux`) may not have OSMesa support. Options:

1. **Check VTK build configuration:**
   ```bash
   grep -i osmesa ~/VTK-install-linux/lib/cmake/vtk-9.3/vtk-config.cmake
   ```

2. **Rebuild VTK with OSMesa:**
   ```bash
   cmake .. \
       -DVTK_USE_X=OFF \
       -DVTK_OPENGL_HAS_OSMESA=ON \
       -DOSMESA_INCLUDE_DIR=/usr/include \
       -DOSMESA_LIBRARY=/usr/lib/x86_64-linux-gnu/libOSMesa.so
   ```

3. **Alternative: Use vtkOSOpenGLRenderWindow** instead of vtkGenericOpenGLRenderWindow in `QCExporter.cpp`

### Option B: GUI-Based Image Generation

If headless rendering can't be fixed, add a GUI post-processing step:

1. After batch completes, user opens QC Review tab
2. Click "Generate Difference Images" button
3. Loop through `qc/transforms/*.json`, load scans, render images
4. Images saved to `qc/difference_images/`

### Option C: Test QC Workflow Without Images

The QC workflow can still function without difference images:

1. **ErrandManager** – Can flag scans based on metrics (RMS outliers)
2. **QCReviewWidget** – Could show text-only entries instead of thumbnails
3. **ErrandResolutionDialog** – Already loads meshes directly, doesn't need images

---

## Testing the QC Workflow

### Step 1: Generate QC Data
```bash
cd /home/kkunzelm/claude-code/DentScanComparePro/build

./DentScanComparePro --batch \
    --study ~/claude-code/DentScanComparePro/data/full_study.json \
    --data-root "/KHKsData/usr2/daten/P2026-Kessler-ScanVergleich/IOS" \
    --output "/KHKsData/usr2/daten/P2026-Kessler-ScanVergleich/IOS/results_batch" \
    --verbose
```

Expected output:
```
results_batch/
├── qc/
│   ├── gpa_means/SKD_18_gpa_mean.stl, SKD_20_gpa_mean.stl, ...
│   └── transforms/*.json
├── trueness_metrics.csv
├── precision_metrics.csv
└── summary_stats.csv
```

### Step 2: Open GUI and Load QC Data
```bash
./DentScanComparePro
```

1. Go to "Study Configuration" tab
2. Load the same study JSON
3. Set output directory to results_batch
4. Go to "QC Review" tab
5. Click "Load QC Data"

### Step 3: Test ErrandManager
- Verify scan entries appear in the list
- Flag some scans as errands
- Save and verify `qc/qc_status.json` is created

### Step 4: Test CSV Filtering
- After flagging errands, save
- Verify errands are excluded from regenerated CSV

### Step 5: Test Errand Resolution
- Select a flagged errand
- Click "Re-register"
- Pick corresponding landmarks on both meshes
- Compute alignment
- Accept or reject result

---

## Code Locations

### QC Export (batch side)
- `src/qc/QCExporter.cpp` – `exportGPAMean()`, `exportTransform()`, `exportDifferenceImage()`
- `src/batch/GroupProcessor.cpp` – Calls `QCExporter::exportGroupQC()`
- `src/batch/BatchRunner.cpp` – Disables image export: `QCExporter::setImageExportEnabled(false)`

### QC Review (GUI side)
- `src/qc/QCReviewWidget.cpp` – Thumbnail grid, status tracking
- `src/qc/ErrandManager.cpp` – Accept/reject persistence
- `src/gui/MainWindow.cpp` – `setupQCReviewTab()`, `loadQCData()`, `onQCReregisterRequested()`

### Errand Resolution
- `src/qc/ErrandResolutionDialog.cpp` – Side-by-side view, point picking
- `src/qc/LandmarkRegistration.cpp` – `computeKabschTransform()`, `registerWithLandmarks()`

---

## Build Commands

```bash
cd /home/kkunzelm/claude-code/DentScanComparePro/build
cmake .. -DCMAKE_BUILD_TYPE=Release
make -j4
```

---

## Data Locations

- **Scanner data**: `/KHKsData/usr2/daten/P2026-Kessler-ScanVergleich/IOS/`
- **Study config**: `~/claude-code/DentScanComparePro/data/full_study.json`
- **Results**: `/KHKsData/usr2/daten/P2026-Kessler-ScanVergleich/IOS/results_batch/`
- **VTK installation**: `~/VTK-install-linux/`

---

## Summary

The QC workflow infrastructure is implemented but untested. The main blocker is VTK headless rendering for difference images. Priorities:

1. **First**: Fix VTK headless rendering OR implement GUI-based image generation
2. **Then**: Test QCReviewWidget thumbnail loading
3. **Then**: Test ErrandManager save/load and CSV filtering
4. **Finally**: Test ErrandResolutionDialog landmark registration

The core algorithms (Kabsch transform, ICP refinement, distance computation) are all implemented and tested. The remaining work is UI integration and testing.
