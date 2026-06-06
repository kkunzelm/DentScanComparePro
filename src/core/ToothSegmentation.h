// SPDX-License-Identifier: GPL-2.0-or-later
// SPDX-FileCopyrightText: 2024 Prof. Dr. Karl-Heinz Kunzelmann

#pragma once

#include "Mesh.h"
#include <vector>

// Tooth Crown Segmentation
//
// Implements a Dijkstra-based region growing ("snapping snake") that expands
// outward from one seed point per tooth crown and stops at the gingival margin.
//
// EDGE COST - curvature-weighted geodesic distance:
//   The cost to traverse from face f to adjacent face nb is:
//
//     W(f,nb) = dist(centroid_f, centroid_nb)
//               * (1 + curvatureRepulsion * max(0, -kappa_min_avg))
//
//   where kappa_min_avg = (kappa_min(f) + kappa_min(nb)) / 2 and kappa_min is
//   the minimum principal curvature, computed per face as kappa_H - sqrt(max(0, kappa_H^2 - kappa_G)).
//   kappa_min is more sensitive than kappa_H at saddle-like CEJ geometry (convex
//   along the arch, concave circumferentially), where kappa_H is small but
//   kappa_min is strongly negative.
//
//   Effect: convex crown surfaces (kappa_min >= 0) cost normal physical distance.
//   Concave regions (fissures, CEJ, gingival sulcus) cost proportionally more,
//   so the geodesic budget is consumed faster there - the algorithm decelerates
//   and stops at the gingival margin even before the hard stops fire.
//   With curvatureRepulsion = 0 the formula reduces to pure geodesic distance.
//
// PRIMARY stopping criterion - curvature-weighted geodesic budget:
//   The accumulated edge cost is limited to maxGeodesicMm. On predominantly
//   convex crown surfaces this equals physical mm closely; only the concave
//   border zones cost extra. Empirical tooth reach (curvatureRepulsion = 0.1):
//     Molars      8-12 mm
//     Premolars   7-10 mm
//     Canines     9-12 mm   (tall, pointed)
//     Incisors   10-13 mm   (thin blade; palatal surface included)
//
// SECONDARY stopping criteria - hard safety-net guards:
//   Adjacent-face crease angle: the cementoenamel junction (CEJ) always
//     creates a kink regardless of tooth orientation. Default 50 deg.
//   Mean curvature floor: the gingival sulcus is concave (kappa_H < 0).
//     Default -4 mm^-1. Both criteria require curvatureComputed == true.
//
// How to pick seeds:
//   Place one seed on the occlusal/incisal surface of each tooth crown.
//   A single click per tooth is sufficient; the geodesic limit handles
//   the rest. Posterior molars need a larger radius (12 mm) than
//   anterior incisors (10 mm) - a single shared value of 12 mm covers all.

namespace ToothSegmentation {

struct Params {
    double maxGeodesicMm      = 12.0;  // primary: curvature-weighted geodesic budget [mm]
    double maxCreaseAngleDeg  = 50.0;  // secondary hard stop: CEJ kink guard [deg]
    double minMeanCurvature   = -4.0;  // secondary hard stop: gingival sulcus floor [1/mm]
    double curvatureRepulsion =  0.1;  // edge-cost scaling for concave zones (0 = disabled)
    int    maxFacesPerSeed    = 40000; // hard cap on BFS nodes per seed
};

// Returns a per-vertex boolean mask. true = vertex belongs to a tooth crown.
// seedVertices: one VertexDesc per tooth (cusp-tip vertex nearest to each
//               operator-picked point).
std::vector<bool> segment(
    const ScanData& scan,
    const std::vector<VertexDesc>& seedVertices,
    const Params& params = {});

// Convenience overload: seeds given as raw 3D world positions (e.g. from
// vtkCellPicker). Each position is snapped to the nearest mesh vertex.
std::vector<bool> segmentFromPoints(
    const ScanData& scan,
    const std::vector<std::array<double,3>>& seedPoints,
    const Params& params = {});

} // namespace ToothSegmentation
