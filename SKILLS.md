# SKILLS.md - Custom Skills for DentScanComparePro

## /copy-core

Copy core computation files from DentScanCompare to this project.

```bash
# Source and destination
SRC="/home/kkunzelm/claude-code/DentScanCompare/src"
DST="/home/kkunzelm/claude-code/DentScanComparePro/src"

# Create directories
mkdir -p "$DST/core" "$DST/visualization"

# Copy core computation files
cp "$SRC/core/Mesh.h" "$DST/core/"
cp "$SRC/core/STLReader.h" "$SRC/core/STLReader.cpp" "$DST/core/"
cp "$SRC/core/CurvatureAnalysis.h" "$SRC/core/CurvatureAnalysis.cpp" "$DST/core/"
cp "$SRC/core/ICPRegistration.h" "$SRC/core/ICPRegistration.cpp" "$DST/core/"
cp "$SRC/core/GPAReference.h" "$SRC/core/GPAReference.cpp" "$DST/core/"
cp "$SRC/core/DistanceField.h" "$SRC/core/DistanceField.cpp" "$DST/core/"
cp "$SRC/core/MetricReport.h" "$DST/core/"

# Copy visualization files
cp "$SRC/visualization/VTKMeshWidget.h" "$SRC/visualization/VTKMeshWidget.cpp" "$DST/visualization/"
cp "$SRC/visualization/ColorMapLUT.h" "$SRC/visualization/ColorMapLUT.cpp" "$DST/visualization/"

echo "Core files copied. Remember to update #include paths if directory structure differs."
```

## /scaffold

Create the initial project directory structure.

```bash
cd /home/kkunzelm/claude-code/DentScanComparePro

# Create source directories
mkdir -p src/{core,config,batch,gui}
mkdir -p data
mkdir -p tests

# Create placeholder files
touch src/main.cpp
touch src/config/StudyConfig.h src/config/StudyConfig.cpp
touch src/config/ROIConfig.h src/config/ROIConfig.cpp
touch src/config/FileDiscovery.h src/config/FileDiscovery.cpp
touch src/batch/BatchRunner.h src/batch/BatchRunner.cpp
touch src/batch/GroupProcessor.h src/batch/GroupProcessor.cpp
touch src/batch/MetricsComputer.h src/batch/MetricsComputer.cpp
touch src/batch/CSVWriter.h src/batch/CSVWriter.cpp
touch src/gui/MainWindow.h src/gui/MainWindow.cpp
touch src/gui/TemplateEditorTab.h src/gui/TemplateEditorTab.cpp
touch src/gui/BatchRunnerTab.h src/gui/BatchRunnerTab.cpp
touch src/gui/ROIPreviewWidget.h src/gui/ROIPreviewWidget.cpp
touch src/gui/BrushTool.h src/gui/BrushTool.cpp

echo "Project structure created."
```

## /create-cmake

Generate the CMakeLists.txt for the project.

Creates a properly configured CMakeLists.txt with Qt5, VTK 9.3, CGAL 6.0, Eigen dependencies.

## /build

Configure and build the project.

```bash
cd /home/kkunzelm/claude-code/DentScanComparePro
mkdir -p build && cd build
cmake .. -DCMAKE_BUILD_TYPE=Release
make -j$(nproc)
```

## /test-load

Test loading a single STL file to verify core library works.

```bash
cd /home/kkunzelm/claude-code/DentScanComparePro/build
./dentscan-batch --test-load "/home/kkunzelm/claude-code/match3d-plus/data/3d-data/stl/Primescan/Defekt 2a/SKD 20/DefektIIa_Primescan_20_1min27s_r1_UpperJaw.stl"
```

## /list-scanners

List all scanner data files organized by scanner and SKD level.

```bash
cd "/home/kkunzelm/claude-code/match3d-plus/data/3d-data/stl"
for scanner in */; do
    echo "=== $scanner ==="
    find "$scanner" -name "*.stl" | head -5
    echo "  ... ($(find "$scanner" -name "*.stl" | wc -l) total files)"
done
```

## /validate-yaml

Validate a study.yaml configuration file syntax and file pattern matches.

## /run-batch

Run batch processing on a study configuration.

```bash
cd /home/kkunzelm/claude-code/DentScanComparePro/build
./dentscan-batch --batch \
    --study ../data/study.yaml \
    --data-root "/home/kkunzelm/claude-code/match3d-plus/data/3d-data/stl" \
    --output ../results \
    --verbose
```

## /check-deps

Verify all dependencies are available.

```bash
echo "Checking dependencies..."
pkg-config --modversion Qt5Widgets 2>/dev/null || echo "Qt5 not found via pkg-config"
pkg-config --modversion eigen3 2>/dev/null || echo "Eigen3 not found via pkg-config"
ls /usr/include/nanoflann.hpp 2>/dev/null && echo "nanoflann found" || echo "nanoflann NOT found"
echo "VTK and CGAL checked via CMake find_package"
```
