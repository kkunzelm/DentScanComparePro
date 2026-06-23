// SPDX-License-Identifier: GPL-2.0-or-later
// SPDX-FileCopyrightText: 2024 Prof. Dr. Karl-Heinz Kunzelmann

#pragma once

#include "Defaults.h"
#include "Mesh.h"
#include <Eigen/Core>
#include <functional>
#include <vector>

namespace ICPRegistration {

// Default values are defined in Defaults.h for consistency across the codebase.
struct Params {
    int    maxIterations   = Defaults::kMaxIcpIterations;
    double convergenceRms  = 1e-4;   // [mm] stop when ΔRMS < this
    int    sampleCount     = 20000;  // points to sample from each mesh
    double maxCorrespDist  = 5.0;    // [mm] reject correspondences further than this
    // TrICP: after distance rejection, keep only this fraction of correspondences
    // sorted by point-to-plane residual (smallest first).  Discards the worst
    // correspondences — typically deforming soft tissue (gingiva, palate) —
    // so the rigid solve focuses on stable surfaces (teeth).
    double trimFraction    = Defaults::kIcpTrimFraction;

    // Resolution hierarchy (coarse-to-fine, Xi-2025 decimation):
    // Each entry is a face fraction (0,1] — mesh is decimated to that fraction for
    // that level.  Last entry should be 1.0 (full-resolution fine pass).
    // Hierarchy is only used by alignHierarchical() / alignMaskedHierarchical().
    bool                useHierarchy    = Defaults::kUseIcpHierarchy;
    std::vector<double> hierarchyLevels = {0.05, 0.20, 1.0};
    // Xi-2025 boundary-preservation weight: cost × negCurvK for negative-curvature edges.
    double              negCurvK        = Defaults::kIcpHierarchyNegCurvK;
};

// Result of one ICP run
struct Result {
    Eigen::Matrix4d transform;   // 4×4 rigid transform (source → target)
    double          finalRms;    // final RMS point-to-plane distance [mm]
    int             iterations;  // iterations used
    bool            converged;
};

// Registers source to target using Point-to-Plane ICP.
// Uses nanoflann KD-tree for nearest-neighbor queries.
// progressCallback(iteration, rms) is called each iteration if non-null.
Result align(
    const ScanData& source,
    const ScanData& target,
    const Params&   params = {},
    std::function<void(int, double)> progressCallback = nullptr
);

// Applies a 4×4 rigid transform to all vertices of a mesh.
void applyTransform(ScanData& scan, const Eigen::Matrix4d& T);

// Uniformly samples ~count points from a mesh (area-weighted sampling).
std::vector<Eigen::Vector3d> sampleMesh(const ScanData& scan, int count);

// Like align(), but only samples source vertices where sourceMask[v] is true.
// Use this for crown-restricted refinement after an initial full-mesh GPA.
// Falls back to full-mesh sampling if the masked region is too small.
Result alignMasked(
    const ScanData& source,
    const ScanData& target,
    const std::vector<bool>& sourceMask,
    const Params&   params = {},
    std::function<void(int, double)> progressCallback = nullptr
);

// Coarse-to-fine ICP using curvature-weighted QEM mesh decimation (Xi-2025).
// Params::hierarchyLevels defines face fractions per level (ascending, last = 1.0).
// At each level the source is decimated to that fraction and ICP runs to convergence;
// the resulting transform seeds the next (finer) level.
Result alignHierarchical(
    const ScanData& source,
    const ScanData& target,
    const Params&   params = {},
    std::function<void(int, double)> progressCallback = nullptr
);

// Like alignHierarchical() but at the fine level uses ROI masking.
// Coarse levels use full-mesh decimated ICP; fine level uses alignMasked().
Result alignMaskedHierarchical(
    const ScanData& source,
    const ScanData& target,
    const std::vector<bool>& sourceMask,
    const Params&   params = {},
    std::function<void(int, double)> progressCallback = nullptr
);

// Computes per-vertex normals for ICP.
std::vector<Eigen::Vector3d> computeVertexNormals(const ScanData& scan);

} // namespace ICPRegistration
