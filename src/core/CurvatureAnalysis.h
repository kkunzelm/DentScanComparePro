#pragma once

#include "Mesh.h"

namespace CurvatureAnalysis {

// Computes per-vertex mean curvature (κ_H) and Gaussian curvature (κ_G)
// using CGAL's interpolated corrected curvatures method.
// Results are stored as named property maps on scan->mesh:
//   "v:mean_curv"  (double, signed mean curvature)
//   "v:gauss_curv" (double, Gaussian curvature)
// Sets scan->curvatureComputed = true on success.
// ball_radius < 0: use 1-ring only (fast); > 0: geodesic ball in mm
void compute(ScanData& scan, double ballRadius = -1.0);

} // namespace CurvatureAnalysis
