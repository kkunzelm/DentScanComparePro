# Project Setup Checklist for Quick Restart

## Files Already Created

- [x] `CLAUDE.md` - Project context for Claude
- [x] `SKILLS.md` - Custom skills/commands
- [x] `docs/implementation-plan-DentScanComparePro.md` - Full architecture
- [x] `docs/IMPLEMENTATION-PROMPT.md` - Resume prompts

## Files Still Needed

### 1. CMakeLists.txt (Critical - Create First)

```cmake
cmake_minimum_required(VERSION 3.16)
project(DentScanComparePro VERSION 1.0 LANGUAGES CXX C)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_AUTOMOC ON)
set(CMAKE_AUTORCC ON)
set(CMAKE_AUTOUIC ON)

# Custom VTK installation (Qt6) - same for Linux and Windows cross-compile
set(VTK_DIR "$ENV{HOME}/VTK-install-linux/lib/cmake/vtk-9.3" CACHE PATH "VTK installation")

# Qt6
find_package(Qt6 REQUIRED COMPONENTS Widgets Concurrent PrintSupport OpenGL OpenGLWidgets)

# MPI stub (must come BEFORE VTK)
find_package(MPI QUIET)
if(NOT TARGET MPI::MPI_C)
    add_library(MPI::MPI_C INTERFACE IMPORTED GLOBAL)
endif()

# VTK
find_package(VTK 9.3 REQUIRED COMPONENTS
    CommonCore CommonDataModel CommonMath
    FiltersSources FiltersGeneral FiltersGeometry
    InteractionStyle InteractionWidgets
    RenderingCore RenderingOpenGL2 RenderingAnnotation
    GUISupportQt IOGeometry IOPLY
)

# CGAL
find_package(CGAL 6.0 REQUIRED)

# Eigen
find_package(Eigen3 REQUIRED)

# Sources
set(CORE_SOURCES
    src/core/STLReader.cpp
    src/core/CurvatureAnalysis.cpp
    src/core/ICPRegistration.cpp
    src/core/GPAReference.cpp
    src/core/DistanceField.cpp
)

set(CONFIG_SOURCES
    src/config/StudyConfig.cpp
    src/config/ROIConfig.cpp
    src/config/FileDiscovery.cpp
)

set(BATCH_SOURCES
    src/batch/BatchRunner.cpp
    src/batch/GroupProcessor.cpp
    src/batch/MetricsComputer.cpp
    src/batch/CSVWriter.cpp
)

set(GUI_SOURCES
    src/gui/MainWindow.cpp
    src/gui/TemplateEditorTab.cpp
    src/gui/BatchRunnerTab.cpp
    src/gui/ROIPreviewWidget.cpp
    src/gui/BrushTool.cpp
    src/visualization/VTKMeshWidget.cpp
    src/visualization/ColorMapLUT.cpp
)

add_executable(dentscan-batch
    src/main.cpp
    ${CORE_SOURCES}
    ${CONFIG_SOURCES}
    ${BATCH_SOURCES}
    ${GUI_SOURCES}
)

target_include_directories(dentscan-batch PRIVATE
    ${CMAKE_SOURCE_DIR}/src
    ${CMAKE_SOURCE_DIR}/src/core
    ${CMAKE_SOURCE_DIR}/src/config
    ${CMAKE_SOURCE_DIR}/src/batch
    ${CMAKE_SOURCE_DIR}/src/gui
    ${CMAKE_SOURCE_DIR}/src/visualization
)

target_link_libraries(dentscan-batch PRIVATE
    Qt6::Widgets
    Qt6::Concurrent
    Qt6::PrintSupport
    Qt6::OpenGL
    Qt6::OpenGLWidgets
    ${VTK_LIBRARIES}
    CGAL::CGAL
    Eigen3::Eigen
)

vtk_module_autoinit(
    TARGETS dentscan-batch
    MODULES ${VTK_LIBRARIES}
)
```

### 2. Sample study.yaml (Create in data/)

```yaml
study:
  name: "DefektIIa_MultiScanner_SKD_Evaluation"
  version: 1
  reference_strategy: "gpa_mean"

  alignment:
    max_icp_iterations: 100
    convergence_threshold_mm: 0.01
    use_pca_coarse: true
    use_4orientation_test: true

scanners:
  - id: "FussenS6000"
    patterns: ["*Fussen*S6000*", "*FussenS6000*"]
  - id: "iTeroLumina"
    patterns: ["*iTero*Lumina*", "*iTeroLumina*"]
  - id: "Mediti700"
    patterns: ["*Medit*i700*", "*Mediti700*"]
  - id: "Primescan"
    patterns: ["*Primescan*"]
  - id: "Trios4"
    patterns: ["*Trios*4*", "*Trios4*"]
  - id: "Trios5"
    patterns: ["*Trios*5*", "*Trios5*"]

groups:
  - id: "SKD_18"
    skd_mm: 18
    file_patterns:
      - "**/18mm*/*.stl"
    roi:
      z_plane:
        above_mm: 2.0
        below_mm: 12.0
      outlier_sigma: 3.0

  - id: "SKD_20"
    skd_mm: 20
    file_patterns:
      - "**/SKD*20*/*.stl"
      - "**/SKD 20/**/*.stl"
      - "**/20mm*/*.stl"
    roi:
      z_plane:
        above_mm: 2.0
        below_mm: 12.0
      outlier_sigma: 3.0

  - id: "SKD_22"
    skd_mm: 22
    file_patterns:
      - "**/SKD*22*/*.stl"
      - "**/22mm*/*.stl"
    roi:
      inherit_from: "SKD_20"

  - id: "SKD_24"
    skd_mm: 24
    file_patterns:
      - "**/SKD*24*/*.stl"
      - "**/24mm*/*.stl"
    roi:
      inherit_from: "SKD_20"

  - id: "SKD_26"
    skd_mm: 26
    file_patterns:
      - "**/SKD*26*/*.stl"
      - "**/26mm*/*.stl"
    roi:
      inherit_from: "SKD_20"

  - id: "SKD_28"
    skd_mm: 28
    file_patterns:
      - "**/SKD*28*/*.stl"
      - "**/28mm*/*.stl"
    roi:
      inherit_from: "SKD_20"

  - id: "SKD_30"
    skd_mm: 30
    file_patterns:
      - "**/SKD*30*/*.stl"
      - "**/30mm*/*.stl"
    roi:
      inherit_from: "SKD_20"

output:
  metrics_csv: "long_format_metrics.csv"
  precision_csv: "precision_matrix.csv"
  summary_csv: "summary_statistics.csv"
```

### 3. .gitignore

```
build/
*.o
*.a
*.so
CMakeFiles/
CMakeCache.txt
cmake_install.cmake
Makefile
compile_commands.json
.cache/
*.user
results/
```

## Recommended Additional Setup

### 1. Create a test subset of data

Copy 2 scanners × 2 SKD × 2 reps = 8 files to a `test_data/` directory for quick iteration:

```bash
mkdir -p /home/kkunzelm/claude-code/DentScanComparePro/test_data
# Copy a small subset for testing
```

### 2. Create expected output for regression testing

After first successful run, save the CSV output as "golden files" to verify future changes don't break metrics.

### 3. Install yaml-cpp (optional, for cleaner YAML parsing)

```bash
sudo apt install libyaml-cpp-dev
```

Then add to CMakeLists.txt:
```cmake
find_package(yaml-cpp REQUIRED)
target_link_libraries(dentscan-batch PRIVATE yaml-cpp)
```

Alternative: Use Qt's QJsonDocument with JSON config instead of YAML.

### 4. Create compile_commands.json for IDE support

Add to CMakeLists.txt:
```cmake
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)
```

Then symlink for clangd:
```bash
ln -s build/compile_commands.json .
```

## Quick Start Commands

```bash
# 1. Create project structure
cd /home/kkunzelm/claude-code/DentScanComparePro
mkdir -p src/{core,config,batch,gui,visualization} data build

# 2. Copy core files from DentScanCompare
cp /home/kkunzelm/claude-code/DentScanCompare/src/core/*.{h,cpp} src/core/
cp /home/kkunzelm/claude-code/DentScanCompare/src/visualization/VTKMeshWidget.{h,cpp} src/visualization/
cp /home/kkunzelm/claude-code/DentScanCompare/src/visualization/ColorMapLUT.{h,cpp} src/visualization/

# 3. Build
cd build
cmake ..
make -j$(nproc)

# 4. Test
./dentscan-batch --help
```

## Session Memory Anchors

When restarting, tell Claude:
1. Project location: `/home/kkunzelm/claude-code/DentScanComparePro`
2. Source project: `/home/kkunzelm/claude-code/DentScanCompare`
3. Data location: `/home/kkunzelm/claude-code/match3d-plus/data/3d-data/stl/`
4. Read `CLAUDE.md` first for technical constraints
5. Current phase: [UPDATE THIS]
