# Core computation sources (reused from DentScanCompare)
set(CORE_SOURCES
    core/STLReader.cpp
    core/CurvatureAnalysis.cpp
    core/ICPRegistration.cpp
    core/GPAReference.cpp
    core/DistanceField.cpp
    core/ToothSegmentation.cpp
    core/AlignmentTransformLoader.cpp
)

# Visualization sources (reused from DentScanCompare)
set(VIS_SOURCES
    visualization/VTKMeshWidget.cpp
    visualization/ColorMapLUT.cpp
)

# Configuration system sources
set(CONFIG_SOURCES
    config/StudyConfig.cpp
    config/FileDiscovery.cpp
    config/ROIConfig.cpp
)

# Batch processing sources
set(BATCH_SOURCES
    batch/BatchRunner.cpp
    batch/GroupProcessor.cpp
    batch/CSVWriter.cpp
)

# QC (Quality Control) sources
set(QC_SOURCES
    qc/QCExporter.cpp
    qc/ErrandManager.cpp
    qc/LandmarkRegistration.cpp
    qc/QCReviewWidget.cpp
    qc/QFlowLayout.cpp
    qc/ErrandResolutionDialog.cpp
    qc/AlignmentQCDialog.cpp
)

# GUI sources
set(GUI_SOURCES
    gui/MainWindow.cpp
)

set(SOURCES
    main.cpp
    ${CORE_SOURCES}
    ${VIS_SOURCES}
    ${CONFIG_SOURCES}
    ${BATCH_SOURCES}
    ${QC_SOURCES}
    ${GUI_SOURCES}
)

add_executable(DentScanComparePro ${SOURCES})

target_include_directories(DentScanComparePro PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}
)

target_link_libraries(DentScanComparePro PRIVATE
    Qt6::Widgets
    Qt6::Concurrent
    Qt6::PrintSupport
    Qt6::OpenGL
    Qt6::OpenGLWidgets
    ${VTK_LIBRARIES}
    CGAL::CGAL
    Eigen3::Eigen
)

# Optional yaml-cpp linking
if(yaml-cpp_FOUND)
    target_link_libraries(DentScanComparePro PRIVATE yaml-cpp::yaml-cpp)
endif()

vtk_module_autoinit(
    TARGETS DentScanComparePro
    MODULES ${VTK_LIBRARIES}
)
