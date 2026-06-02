# Implementation Prompt for DentScanComparePro

Use this prompt to resume development with full context.

---

## PROMPT

```
I am implementing DentScanComparePro, a Qt6/VTK/CGAL application for automated batch evaluation of dental intraoral scanner accuracy.

**Read these files first to understand the project:**
1. `CLAUDE.md` - Project context and technical constraints
2. `docs/implementation-plan-DentScanComparePro.md` - Full architecture and design
3. `/home/kkunzelm/claude-code/DentScanCompare/docs/developer-handoff.md` - Source project documentation

**Project Goal:**
Batch process ~185 STL files from 6 dental scanners across 7 SKD (inter-incisor distance) levels to compute ISO 5725/12836-compliant trueness and precision metrics. Output structured CSV files for statistical analysis (Two-Way ANOVA).

**Architecture:**
- Single Qt5 application with GUI mode (default) and CLI mode (--batch flag)
- GUI has two tabs: Template Editor (ROI definition) and Batch Runner (progress monitoring)
- Core computation reused from DentScanCompare (copy files, don't symlink)
- YAML-driven study configuration (`study.yaml`)

**Implementation Phases:**

**Phase 1: Project Setup**
- Create CMakeLists.txt with Qt6/VTK9.3(custom)/CGAL6.0/Eigen3 dependencies
- Copy core files from DentScanCompare/src/core/ and visualization/
- Create src/main.cpp with CLI argument parsing (QCommandLineParser)
- Verify build compiles and links

**Phase 2: Configuration System**
- Implement src/config/ROIConfig.h - BoundingBox, ZPlaneSlab, BrushZone structs
- Implement src/config/StudyConfig.{h,cpp} - YAML parsing with QJsonDocument or yaml-cpp
- Implement src/config/FileDiscovery.{h,cpp} - Glob pattern matching with QDirIterator

**Phase 3: Batch Processing Engine**
- Implement src/batch/GroupProcessor.{h,cpp} - Process one SKD group
- Implement src/batch/MetricsComputer.{h,cpp} - Trueness (vs GPA mean) and Precision (pairwise)
- Implement src/batch/CSVWriter.{h,cpp} - UTF-8 BOM, structured output
- Implement src/batch/BatchRunner.{h,cpp} - Orchestrate all groups, emit progress signals

**Phase 4: GUI - Template Editor**
- Implement src/gui/MainWindow.{h,cpp} - QTabWidget with two tabs
- Implement src/gui/TemplateEditorTab.{h,cpp} - SKD group list, ROI controls, 3D preview
- Extend VTKMeshWidget with showROIMask() method for vertex coloring
- Implement src/gui/BrushTool.{h,cpp} - Click-to-paint inclusion/exclusion zones

**Phase 5: GUI - Batch Runner**
- Implement src/gui/BatchRunnerTab.{h,cpp} - File pickers, progress bar, log display
- Connect BatchRunner signals to progress UI
- Implement cancel functionality via QFuture::cancel()

**Phase 6: CLI Mode**
- In main.cpp, detect --batch flag and run BatchRunner directly without GUI
- Print progress to stdout
- Exit with appropriate return codes

**Critical Technical Constraints:**
1. Qt6 with custom VTK build from ~/VTK-install-linux (NOT system VTK which is Qt5)
2. Set VTK_DIR to ~/VTK-install-linux/lib/cmake/vtk-9.3 in CMakeLists.txt
3. CGAL 6.0 property_map returns std::optional, not std::pair
4. VTK objects are NOT thread-safe - use signals to main thread for rendering
5. Use QSignalBlocker when programmatically toggling checkable buttons
6. STLReader already handles Primescan reversed normals
7. FussenS6000/iTeroLumina need PCA coarse alignment (28mm coordinate offset)

**Current Status:** [UPDATE THIS AS YOU PROGRESS]
- [ ] Phase 1: Project Setup
- [ ] Phase 2: Configuration System
- [ ] Phase 3: Batch Processing Engine
- [ ] Phase 4: GUI - Template Editor
- [ ] Phase 5: GUI - Batch Runner
- [ ] Phase 6: CLI Mode

**Data Location:**
- Scanner STL files: /home/kkunzelm/claude-code/match3d-plus/data/3d-data/stl/
- 6 scanners: Fussen S6000, iTeroLumina, Medit i700, Primescan, Trios 4, Trios 5
- 7 SKD levels: 18mm (iTero only), 20mm, 22mm, 24mm, 26mm, 28mm, 30mm
- 5 repetitions per scanner×SKD cell

Please start with [SPECIFY PHASE] and show me the implementation.
```

---

## Quick Resume Prompts

### Resume from scratch:
```
Read CLAUDE.md and docs/implementation-plan-DentScanComparePro.md, then start Phase 1: create CMakeLists.txt and copy core files from DentScanCompare.
```

### Resume configuration work:
```
Read CLAUDE.md. I'm working on Phase 2 (Configuration System). Implement StudyConfig.{h,cpp} with YAML parsing for the study.yaml format shown in the implementation plan.
```

### Resume batch engine:
```
Read CLAUDE.md. I'm working on Phase 3 (Batch Processing). Implement GroupProcessor that loads STL files, runs GPA alignment, applies ROI mask, and computes trueness metrics.
```

### Resume GUI work:
```
Read CLAUDE.md. I'm working on Phase 4 (GUI Template Editor). Implement TemplateEditorTab with SKD group list, ROI parameter controls, and VTK 3D preview showing included/excluded vertices.
```

### Debug build issues:
```
Read CLAUDE.md. The build is failing with [ERROR]. Check CMakeLists.txt against the critical setup requirements (Qt5, MPI before VTK, CGAL 6.0).
```

### Test with real data:
```
Read CLAUDE.md. Load one STL file from /home/kkunzelm/claude-code/match3d-plus/data/3d-data/stl/Primescan/Defekt\ 2a/SKD\ 20/ and verify STLReader and CurvatureAnalysis work correctly.
```
