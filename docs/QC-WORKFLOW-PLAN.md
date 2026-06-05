# Registration QC Workflow Plan

## Problem Statement

The batch GPA registration may produce gross registration failures (local minima). Users need:
1. Visual verification that registration worked correctly
2. Quick review of all results with ability to flag problems ("errands")
3. Flagged scans removed from final CSV and added to review list
4. Interactive re-alignment using corresponding point sets (landmarks)
5. Re-add corrected scans to final results

**Context from user:**
- Primary concern: Gross registration failures (not subtle issues)
- No misaligned scans encountered yet (safety net, not common problem)
- Results shared with other scientists (open science)
- Need certainty about what was done

## Current System Analysis

### What Exists:
- **GPA transforms preserved** in `ScanData::transform` (but unused/unserialized)
- **Per-vertex distances** stored in `ScanData::distanceToRef`
- **VTK visualization** can show color-coded distance maps (blue-white-red)
- **Batch metrics** (RMS, MAD, coverage) exported to CSV
- **Point picking** already implemented in VTKMeshWidget

### What's Missing:
- No difference image export for quick visual QC
- No "errand" flagging and CSV exclusion workflow
- No GPA mean mesh persistence for re-registration
- No landmark-based (corresponding points) registration
- No review list management

---

## Proposed QC Workflow

### Overview
```
┌─────────────────────────────────────────────────────────────────┐
│  BATCH PHASE                                                     │
│  ─────────────                                                   │
│  Run batch → For each scan:                                      │
│    • GPA alignment                                               │
│    • Compute distances                                           │
│    • Export difference image (PNG)                               │
│    • Save GPA mean mesh (once per group)                         │
│    • Save transform + metrics (JSON)                             │
│  Output: trueness_metrics.csv + qc/ folder                       │
└─────────────────────────────────────────────────────────────────┘
                              ↓
┌─────────────────────────────────────────────────────────────────┐
│  QUICK REVIEW PHASE                                              │
│  ──────────────────                                              │
│  GUI shows thumbnail grid of all difference images               │
│  User scans through quickly:                                     │
│    ✓ Good → Accept (stays in CSV)                                │
│    ✗ Bad  → Flag as "errand" (removed from CSV, added to list)  │
│  Output: Updated CSV (errands excluded) + errands.json           │
└─────────────────────────────────────────────────────────────────┘
                              ↓
┌─────────────────────────────────────────────────────────────────┐
│  ERRAND RESOLUTION PHASE (if any flagged)                        │
│  ────────────────────────                                        │
│  For each errand:                                                │
│    1. Load original STL + saved GPA mean                         │
│    2. Show side-by-side 3D view                                  │
│    3. User picks 3-N corresponding points on both meshes         │
│    4. Compute initial transform from landmarks                   │
│    5. ICP refinement                                             │
│    6. Show new difference image                                  │
│    7. Accept → Re-add to CSV with new metrics                    │
│       Reject → Keep in errands list (exclude from analysis)      │
└─────────────────────────────────────────────────────────────────┘
```

### Phase 1: Enhanced Batch Processing

**New Output Structure:**
```
results/
├── trueness_metrics.csv          # Final metrics (errands excluded after review)
├── trueness_metrics_all.csv      # All metrics (before QC, for reference)
├── precision_metrics.csv
├── summary_stats.csv
├── qc/
│   ├── gpa_means/
│   │   ├── SKD_20_gpa_mean.stl   # Saved GPA reference per group
│   │   ├── SKD_22_gpa_mean.stl
│   │   └── ...
│   ├── difference_images/
│   │   ├── Primescan_SKD20_r1.png
│   │   ├── Primescan_SKD20_r2.png
│   │   └── ...
│   ├── transforms/
│   │   ├── Primescan_SKD20_r1.json  # {transform: [...], rms: 0.045, ...}
│   │   └── ...
│   ├── qc_status.json            # Accept/reject status for all scans
│   └── errands.json              # List of flagged scans needing review
```

### Phase 2: Quick Review UI

**Thumbnail Grid View:**
```
┌────────────────────────────────────────────────────────────────┐
│  QC Review - SKD 20 (30 scans)                    [All Groups ▼]│
├────────────────────────────────────────────────────────────────┤
│  ┌──────┐ ┌──────┐ ┌──────┐ ┌──────┐ ┌──────┐ ┌──────┐        │
│  │ PS_1 │ │ PS_2 │ │ PS_3 │ │ PS_4 │ │ PS_5 │ │ T4_1 │        │
│  │ 0.04 │ │ 0.05 │ │ 0.23 │ │ 0.04 │ │ 0.05 │ │ 0.06 │        │
│  │  ✓   │ │  ✓   │ │  ⚠   │ │  ✓   │ │  ✓   │ │  ✓   │        │
│  └──────┘ └──────┘ └──────┘ └──────┘ └──────┘ └──────┘        │
│  ... (more thumbnails)                                         │
├────────────────────────────────────────────────────────────────┤
│  [Accept All Visible] [Flag Selected as Errand] [Save & Exit]  │
│  Errands: 1    Accepted: 29    Pending: 0                      │
└────────────────────────────────────────────────────────────────┘
```

- Click thumbnail → Expand to full 3D interactive view
- Double-click → Flag as errand
- Color-coded borders: Green=accepted, Red=errand, Grey=pending
- Auto-highlight outliers (RMS > mean + 2σ) with yellow border

### Phase 3: Landmark-Based Re-Registration

**Corresponding Points Workflow:**
```
┌─────────────────────────────────────────────────────────────────┐
│  Re-Register: Primescan_SKD20_r3                                │
├──────────────────────────┬──────────────────────────────────────┤
│  SCAN (original)         │  GPA REFERENCE                       │
│  ┌────────────────────┐  │  ┌────────────────────┐              │
│  │                    │  │  │                    │              │
│  │    [3D mesh]       │  │  │    [3D mesh]       │              │
│  │    • Pick points   │  │  │    • Pick points   │              │
│  │                    │  │  │                    │              │
│  └────────────────────┘  │  └────────────────────┘              │
│  Points: 3/3 picked      │  Points: 3/3 picked                  │
├──────────────────────────┴──────────────────────────────────────┤
│  Landmarks:                                                      │
│    1. Cusp tip 16 → Cusp tip 16  ✓                              │
│    2. Cusp tip 26 → Cusp tip 26  ✓                              │
│    3. Incisal 11  → Incisal 11   ✓                              │
├──────────────────────────────────────────────────────────────────┤
│  [Compute Initial Alignment]  [Run ICP Refinement]              │
│  [Show Difference Map]        [Accept Result]  [Reject/Skip]    │
└──────────────────────────────────────────────────────────────────┘
```

**Algorithm for Landmark Registration:**
1. User picks 3+ corresponding points on scan and reference
2. Compute rigid transform using SVD (Kabsch algorithm):
   - Centroid alignment
   - Optimal rotation via SVD of cross-covariance matrix
3. Apply initial transform to scan
4. Run ICP refinement (existing `ICPRegistration::align()`)
5. Recompute distances and metrics
6. User decides: Accept (add to CSV) or Reject (keep excluded)

---

## Data Persistence for Open Science

**What gets saved (shareable with colleagues):**

1. **GPA Mean Meshes** (STL format)
   - One per SKD group
   - Can be loaded in any mesh viewer
   - Reference for reproducibility

2. **Difference Images** (PNG)
   - Visual proof of registration quality
   - Color bar included
   - Filename encodes scan identity

3. **Transform Matrices** (JSON)
   - 4×4 rigid transform from original to aligned space
   - Allows reproducing alignment without re-running GPA

4. **QC Status** (JSON)
   - Accept/reject decisions
   - Timestamps
   - Reviewer notes (optional)

5. **Metrics** (CSV)
   - Final CSV excludes errands
   - `_all.csv` version includes everything for transparency

---

## Implementation Plan

### New Files:
```
src/qc/
├── QCExporter.{h,cpp}        # Export difference images, transforms
├── QCReviewWidget.{h,cpp}    # Thumbnail grid + review UI
├── LandmarkRegistration.{h,cpp}  # Corresponding points alignment
└── ErrandManager.{h,cpp}     # Track/persist errand status
```

### Modified Files:
- `src/batch/GroupProcessor.cpp` - Add QC export calls
- `src/batch/CSVWriter.cpp` - Support errand exclusion
- `src/core/GPAReference.cpp` - Return and save GPA mean mesh
- `src/gui/MainWindow.cpp` - Add QC Review tab
- `src/visualization/VTKMeshWidget.cpp` - Add offscreen PNG export

### Implementation Order:
1. **QC Export in Batch** - Save GPA means, difference images, transforms
2. **Quick Review UI** - Thumbnail grid with accept/flag workflow
3. **Errand Management** - CSV exclusion, errands.json tracking
4. **Landmark Registration** - Corresponding points + ICP refinement

---

## User Design Decisions

- **Difference images**: Occlusal view (top-down) PNG
- **GPA mean storage**: STL file (one per SKD group)
- **Landmark count**: User decides (3-10 points), click "Done" when satisfied

---

## Verification Plan

1. **Run batch** on test data → Check qc/ folder created with all outputs
2. **Open QC Review** → Verify thumbnails load, can flag errands
3. **Flag one scan** → Verify removed from trueness_metrics.csv
4. **Re-register errand** → Pick 3-10 landmarks, verify new alignment works
5. **Accept fixed scan** → Verify re-added to CSV with new metrics

---

## Summary

**Workflow:**
1. Batch runs → exports QC data (difference PNGs, GPA mean STLs, transforms)
2. Quick review → thumbnail grid, accept good scans, flag errands
3. Errands removed from final CSV, added to review list
4. Interactive re-registration → user picks 3-10 corresponding landmarks
5. Fixed scans re-added to CSV with corrected metrics

**Key outputs for open science:**
- `trueness_metrics.csv` (errands excluded)
- `trueness_metrics_all.csv` (complete, for transparency)
- `qc/gpa_means/*.stl` (reproducible references)
- `qc/difference_images/*.png` (visual proof)
- `qc/qc_status.json` (review decisions with timestamps)
