#pragma once

#include <string>
#include <limits>

struct MetricReport {
    std::string scannerName;

    // --- tessellation fingerprint ---
    double ati             = 0.0;   // Adaptive Tessellation Index (Spearman: |κ| vs 1/area)
    double meanEdgeLength  = 0.0;   // [mm]
    double meanAspectRatio = 0.0;
    double maxAspectRatio  = 0.0;
    double densityHighCurv = 0.0;   // triangles/mm² where |κ_H| > threshold
    double densityLowCurv  = 0.0;   // triangles/mm² where |κ_H| ≤ threshold
    std::size_t triangleCount = 0;

    // --- local accuracy (post-registration) ---
    double rmsDistance    = std::numeric_limits<double>::quiet_NaN();
    double madDistance    = std::numeric_limits<double>::quiet_NaN();  // median abs deviation
    double hausdorff95    = std::numeric_limits<double>::quiet_NaN();  // 95th percentile
    double hausdorff100   = std::numeric_limits<double>::quiet_NaN();  // max
    double signedMean     = std::numeric_limits<double>::quiet_NaN();  // bias

    // --- completeness ---
    double coverageRate         = std::numeric_limits<double>::quiet_NaN(); // % within 0.2 mm
    double openBoundaryLength   = 0.0;  // [mm]
    int    holeCount            = 0;

    // --- global deformation ---
    double intermolarDistance  = std::numeric_limits<double>::quiet_NaN();  // [mm]
    double archFormDeviation   = std::numeric_limits<double>::quiet_NaN();  // [mm]
    double maxStitchingAngle   = std::numeric_limits<double>::quiet_NaN();  // [°]
};
