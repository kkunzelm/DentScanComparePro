# DentScanComparePro - Implementation Continuation Prompt (Part 5)

## Project Overview

DentScanComparePro is a Qt6/VTK/CGAL application for batch evaluation of dental intraoral scanner accuracy. It computes ISO 5725/12836-compliant trueness and precision metrics.

**Key documentation:**
- `docs/developer-handoff.md` - Full technical documentation
- `docs/QC-WORKFLOW-PLAN.md` - QC workflow design
- `CLAUDE.md` - Project context and coding standards

## Current State

### What's Working
- Full CLI batch mode with JSON configuration
- Per-group GPA alignment and distance computation
- Trueness and precision metrics with CSV output
- GUI mode with tabbed interface (config, ROI editor, batch, results, QC review)
- Tooth segmentation with interactive seed placement
- ROI template batch integration
- QC workflow: GPA mean export, transform export, thumbnail review grid

### What's Being Debugged

#### 1. Landmark Registration 90-Degree Rotation Issue

**Problem:** After computing Kabsch transform from 3+ corresponding landmarks, the aligned scan mesh appears ~90 degrees rotated from correct position. ICP refinement then improves alignment (movement is correct direction).

**Root cause hypothesis:** Different scanners use different coordinate systems:
- Some scanners: Y-axis is "up" (constant Y ~5-6 in landmark picks)
- GPA reference: Z-axis is "up" (constant Z ~5.6-6.2 in landmark picks)

**Debug output added:** Transform matrix is now printed after Kabsch computation:
```
=== Kabsch Transform Matrix ===
[  R00,   R01,   R02,   tx]
[  R10,   R11,   R12,   ty]
[  R20,   R21,   R22,   tz]
[  0.0,   0.0,   0.0,  1.0]
```

**Expected:** Upper-left 3x3 should show significant off-diagonal values for ~90° X-axis rotation:
```
R (Y-up → Z-up) = [1   0    0 ]
                  [0   0   -1 ]
                  [0   1    0 ]
```

**Files involved:**
- `src/qc/LandmarkRegistration.cpp` - Kabsch algorithm implementation
- `src/qc/ErrandResolutionDialog.cpp` - Dialog that calls the alignment

**Next steps:**
1. User tests and provides transform matrix output
2. Verify rotation is computed correctly
3. If rotation is correct, check if transform is being applied correctly to mesh
4. If rotation is wrong, debug the Kabsch algorithm or landmark pairing

#### 2. VTK Cleanup Crash on Dialog Close

**Problem:** Segfault or std::bad_alloc when closing ErrandResolutionDialog, crash occurs during QCReviewWidget::refresh() which loads QPixmap thumbnails.

**Timeline of fixes attempted:**
1. Added `clearMesh()` calls in dialog destructor → Still crashed
2. Removed clearMesh() calls, simplified VTKMeshWidget destructor → Changed to std::bad_alloc
3. Deferred refresh by 100ms using QTimer::singleShot → **Currently testing**

**Current fix (may need more work):**
```cpp
// MainWindow.cpp after dialog.exec():
for (int i = 0; i < 3; ++i) {
    QApplication::processEvents();
}
QTimer::singleShot(100, this, [this, sid, newRMS]() {
    m_qcReviewWidget->refresh();  // Deferred
});
```

**Files involved:**
- `src/visualization/VTKMeshWidget.cpp` - Destructor cleanup
- `src/qc/ErrandResolutionDialog.cpp` - Dialog destructor
- `src/gui/MainWindow.cpp` - Dialog invocation and refresh

**Potential further fixes if crash persists:**
1. Increase timer delay (100ms → 500ms)
2. Use `QDialog::finished` signal instead of checking result after exec()
3. Force Qt widget deletion with `dialog.setAttribute(Qt::WA_DeleteOnClose)`
4. Investigate VTK/OpenGL context sharing issues

## Test Command

```bash
cd build
./DentScanComparePro
```

1. In GUI, go to Batch Processing tab and run batch on a small dataset
2. Go to QC Review tab, flag a scan as errand
3. Click "Re-Register" to open ErrandResolutionDialog
4. Pick 3+ corresponding landmarks on reference and scan
5. Click "Compute Alignment" - observe transform matrix in terminal
6. Check if mesh rotation is correct visually
7. Click "Accept Result" - check if crash occurs

## Key Code Locations

| Component | File | Description |
|-----------|------|-------------|
| Kabsch algorithm | `src/qc/LandmarkRegistration.cpp:14-72` | computeKabschTransform() |
| Transform debug | `src/qc/ErrandResolutionDialog.cpp:333-355` | Matrix print after alignment |
| Dialog destructor | `src/qc/ErrandResolutionDialog.cpp:38-49` | Minimal cleanup |
| VTK destructor | `src/visualization/VTKMeshWidget.cpp` | Simplified cleanup |
| Deferred refresh | `src/gui/MainWindow.cpp:1516-1534` | QTimer::singleShot fix |

## Architecture Context

The QC workflow follows this pattern:
```
Batch Processing
    ↓
[GPA alignment per group, export GPA mean STL + transforms JSON]
    ↓
QC Review Widget (thumbnail grid)
    ↓ [flag problematic scans as errands]
ErrandResolutionDialog
    ↓ [pick 3+ landmarks on ref and scan]
LandmarkRegistration::computeKabschTransform()
    ↓ [apply transform, optional ICP refine]
Accept/Reject → Update CSV metrics
```

## Previous Work Summary

- Part 1-2: Core algorithms, batch processing, file discovery
- Part 3: GUI implementation, ROI template editor, tooth segmentation
- Part 4: QC workflow, ErrandManager, QCReviewWidget, ErrandResolutionDialog
- Part 5 (current): Debugging landmark registration rotation and VTK cleanup crash

## Notes for Next Session

1. **Check terminal output** for the transform matrix when user tests
2. The Kabsch algorithm in `LandmarkRegistration.cpp` follows standard SVD approach - likely correct
3. If transform looks correct but mesh is wrong, issue may be in `applyTransform()` or coordinate handedness
4. STL winding order is already handled in `STLReader.cpp` (per-face normal check)
5. ICP "moving in correct direction" after wrong initial alignment suggests ICP is working but starting position is off
