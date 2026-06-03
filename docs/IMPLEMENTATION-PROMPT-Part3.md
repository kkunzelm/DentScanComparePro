# DentScanComparePro – Implementation Prompt (Part 3)

## Context

You are continuing development of **DentScanComparePro**, a Qt/VTK/CGAL application for automated batch evaluation of dental intraoral scanner accuracy. The batch processing system is complete, including ROI template integration with tooth segmentation. This prompt covers the remaining polish and statistical enhancements.

## Quick Start – Files to Read First

Read these files in order to understand the current state:

1. **`CLAUDE.md`** (project root) – Build environment, coding standards, key abstractions
2. **`docs/developer-handoff.md`** – Detailed technical documentation, current status, changelog
3. **`src/batch/BatchRunner.cpp`** – Batch orchestration with incremental save/resume
4. **`src/batch/GroupProcessor.cpp`** – Processing pipeline with ROI template support
5. **`src/gui/MainWindow.cpp`** – GUI implementation with ROI Template Editor
6. **`src/config/ROIConfig.h`** – ROITemplate struct with tooth segmentation settings

## What Has Been Completed

### Core Batch Processing
- Full CLI batch mode with JSON configuration
- Per-group GPA alignment and distance computation
- Trueness metrics (RMS, MAD, Max, P95, coverage rate)
- Precision metrics (pairwise RMS between repetitions)
- CSV output (metrics, precision, summary)
- Incremental save with resume capability (`.batch_progress.json`)

### ROI System
- Bounding box, Z-plane slab, brush zones, sigma clipping
- **ROI Template batch integration** (`--roi-template` CLI option)
- Tooth segmentation from seed points (Dijkstra-based)
- Seeds transferred to all scans after GPA alignment

### GUI Features
- Tabbed interface: Study Config, ROI Template Editor, Batch Processing, Results
- Interactive 3D visualization with VTK
- Seed point picking for tooth segmentation
- **Occlusal plane picking** (3-point plane definition)
- **Bounding box wireframe visualization**
- **Brush tool for direct tooth mask editing**

### Tested and Working
```bash
./src/DentScanComparePro --batch \
    --study ../data/full_study.json \
    --data-root /KHKsData/usr2/daten/P2026-Kessler-ScanVergleich/IOS \
    --output ./results \
    --roi-template roi_template.json \
    --verbose
```
- Successfully processes 185 scans (6 scanners × 7 SKD levels × 5 reps)
- Produces complete trueness, precision, and summary CSV files
- Handles interrupted runs via automatic resume

---

## What Remains To Be Done

### Task 1: Statistical Enhancements

**Goal:** Add R-ready output format and compute effect sizes for scanner comparisons.

**Current CSV Output:**
- `trueness_metrics.csv` – One row per scan with RMS, MAD, Max, P95
- `precision_metrics.csv` – One row per scanner×SKD with pairwise RMS statistics
- `summary_stats.csv` – Aggregated trueness by scanner×SKD

**Enhancements Needed:**

1. **R-Ready Format Improvements**
   - Add `Scanner_Factor` column (for ANOVA: numeric encoding 1-6)
   - Add `SKD_Factor` column (ordered factor: 18, 20, 22, 24, 26, 28, 30)
   - Include column headers that are valid R variable names (no spaces, use underscores)
   - Consider adding R script snippet in output directory for quick analysis

2. **Effect Size Computation**
   - Cohen's d for pairwise scanner comparisons
   - Eta-squared (η²) for overall scanner effect
   - Partial eta-squared for SKD×Scanner interaction

3. **Additional Statistical Metrics**
   - Confidence intervals (95% CI) for mean RMS
   - Intraclass correlation coefficient (ICC) for precision assessment
   - Bland-Altman limits of agreement

4. **Per-Scanner Summary Report**
   - `scanner_summary.csv` with:
     - Mean trueness across all SKD levels
     - Mean precision across all SKD levels
     - Best/worst SKD performance
     - Overall ranking

**Files to modify:**
- `src/batch/CSVWriter.{h,cpp}` – Add new output methods
- `src/batch/BatchRunner.cpp` – Call new statistical computations
- `src/batch/GroupProcessor.h` – May need additional metrics in BatchMetricReport

**Reference formulas:**
```cpp
// Cohen's d = (mean1 - mean2) / pooled_sd
// pooled_sd = sqrt(((n1-1)*sd1² + (n2-1)*sd2²) / (n1+n2-2))

// Eta-squared = SS_between / SS_total
// SS_between = Σ n_i * (mean_i - grand_mean)²
// SS_total = Σ (x_ij - grand_mean)²
```

---

### Task 2: GUI Refinements

**Goal:** Improve BatchRunner ↔ GUI communication and error handling.

**Current Issues:**
1. BatchRunner runs in background thread but progress signals not connected to GUI
2. Errors during batch processing not displayed in GUI
3. No visual feedback during long operations (curvature computation, GPA)

**Enhancements Needed:**

1. **Progress Signal Integration**
   - Connect `BatchRunner::progressUpdated()` to `MainWindow::onBatchProgress()`
   - Connect `BatchRunner::groupCompleted()` to update group list with status icons
   - Connect `BatchRunner::logMessage()` to append to batch log widget
   - Use `Qt::QueuedConnection` for cross-thread signal delivery

2. **Per-Stage Progress**
   - Currently GroupProcessor emits stage progress (load, curvature, GPA, distances, metrics)
   - Forward these signals through BatchRunner to GUI
   - Show sub-progress bar for current group

3. **Error Display**
   - Show warning/error dialogs for critical failures
   - Color-code log entries (red for errors, yellow for warnings)
   - Accumulate warnings and show summary at end

4. **Cancel Confirmation**
   - Show "Are you sure?" dialog before cancelling
   - Display "Cancelling..." status while waiting for current group to finish
   - Show "Cancelled at group X of Y" in final status

5. **Results Integration**
   - After batch completes, automatically switch to Results tab
   - Highlight newly created files
   - Add "Open in File Manager" button

**Files to modify:**
- `src/gui/MainWindow.{h,cpp}` – Signal connections, error handling
- `src/batch/BatchRunner.{h,cpp}` – Ensure signals emitted on main thread
- `src/batch/GroupProcessor.{h,cpp}` – May need additional progress signals

**Signal Connection Pattern:**
```cpp
// In MainWindow::runBatch()
connect(m_batchRunner.get(), &BatchRunner::progressUpdated,
        this, &MainWindow::onBatchProgress, Qt::QueuedConnection);
connect(m_batchRunner.get(), &BatchRunner::groupCompleted,
        this, &MainWindow::onGroupCompleted, Qt::QueuedConnection);
connect(m_batchRunner.get(), &BatchRunner::logMessage,
        this, &MainWindow::onLogMessage, Qt::QueuedConnection);
```

---

## Technical Notes

### Build Commands
```bash
cd /home/kkunzelm/claude-code/DentScanComparePro/build
cmake .. -DCMAKE_BUILD_TYPE=Release
make -j4
```

### Test GUI Mode
```bash
./src/DentScanComparePro
# Opens GUI, go to ROI Template Editor tab, load a scan, test features
```

### Test Batch Mode
```bash
./src/DentScanComparePro --batch \
    --study ../data/test_single_group.json \
    --data-root /KHKsData/usr2/daten/P2026-Kessler-ScanVergleich/IOS \
    --output /tmp/test_output \
    --verbose
```

### R Analysis Example
After generating enhanced CSV output, users should be able to run:
```r
data <- read.csv("trueness_metrics.csv")
model <- aov(Trueness_RMS_mm ~ Scanner_Factor * SKD_Factor, data=data)
summary(model)
TukeyHSD(model, "Scanner_Factor")
```

---

## Critical Pitfalls to Remember

1. **Qt threading**: Signals from BatchRunner (worker thread) must use `Qt::QueuedConnection` to reach GUI
2. **Progress updates**: Don't emit too frequently (every 100ms max) to avoid UI flooding
3. **Error accumulation**: Store warnings/errors in QStringList, display at end
4. **CSV encoding**: Use UTF-8 with BOM for Excel compatibility
5. **Effect size edge cases**: Handle n=1 groups (can't compute SD), zero variance

---

## Session Goals

Suggested order of work:

### Session A: Statistical Enhancements
1. Add effect size computation (Cohen's d, eta-squared)
2. Add confidence interval computation
3. Create R-ready output format
4. Generate scanner summary report
5. Test with full dataset

### Session B: GUI Refinements
1. Connect BatchRunner signals to GUI slots
2. Add per-stage progress display
3. Implement error/warning highlighting in log
4. Add cancel confirmation dialog
5. Auto-switch to Results tab on completion

---

## Output Expectations

After completing both tasks, you should have:

### Statistical Enhancements
- `trueness_metrics.csv` with Scanner_Factor, SKD_Factor columns
- `scanner_summary.csv` with rankings and effect sizes
- Optional: `analysis_template.R` script for quick ANOVA

### GUI Refinements
- Real-time progress updates in Batch Processing tab
- Color-coded log entries (errors red, warnings yellow)
- Cancel confirmation and graceful cancellation
- Auto-navigation to Results tab after completion

---

## Quick Resume Prompts

### Resume statistical work:
```
Read CLAUDE.md and docs/developer-handoff.md. Implement effect size computation (Cohen's d, eta-squared) in CSVWriter and add R-ready columns to CSV output.
```

### Resume GUI work:
```
Read CLAUDE.md and docs/developer-handoff.md. Connect BatchRunner progress signals to MainWindow slots using Qt::QueuedConnection. Add error highlighting to the batch log.
```

### Debug signal issues:
```
Read CLAUDE.md. The GUI progress bar isn't updating during batch runs. Check that BatchRunner signals use Qt::QueuedConnection for cross-thread delivery to MainWindow.
```

### Test statistical output:
```
Read CLAUDE.md. Run a test batch and verify the new CSV columns (Scanner_Factor, SKD_Factor, effect sizes) are correctly computed. Check edge cases with small sample sizes.
```
