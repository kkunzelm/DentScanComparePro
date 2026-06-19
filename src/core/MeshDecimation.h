// SPDX-License-Identifier: GPL-2.0-or-later
// SPDX-FileCopyrightText: 2024 Prof. Dr. Karl-Heinz Kunzelmann

#pragma once

#include "Mesh.h"
#include <memory>

namespace MeshDecimation {

// Creates a decimated copy of a mesh using curvature-weighted QEM (Xi-2025 Algorithm 1).
//
// Standard Garland-Heckbert QEM cost is multiplied by a curvature coefficient:
//   coeff = negCurvK  if mean_curvature(v0, v1) < 0  (tooth boundaries, CEJ, grooves)
//   coeff = 1.0       otherwise (convex tooth cusps)
//
// This assigns higher collapse cost to boundary edges, preserving 10-15% more triangles
// at the gingival margin and developmental grooves vs. standard QEM (Xi-2025, Table 1).
//
// If curvature has not been computed (source.curvatureComputed == false), curvature is
// computed automatically on the copy. If CGAL curvature computation fails, falls back to
// standard GH (all coefficients = 1.0) with a warning.
//
// targetFaceFraction: fraction of original faces to keep (0 < f < 1).  Values >= 1 return
//                     a copy of the original without any simplification.
// negCurvK:           cost multiplier for negative-curvature edges (default 10, Xi-2025).
std::shared_ptr<ScanData> decimate(const ScanData& source,
                                    double targetFaceFraction,
                                    double negCurvK = 10.0);

} // namespace MeshDecimation
