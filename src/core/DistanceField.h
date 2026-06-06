// SPDX-License-Identifier: GPL-2.0-or-later
// SPDX-FileCopyrightText: 2024 Prof. Dr. Karl-Heinz Kunzelmann

#pragma once

#include "Mesh.h"
#include "MetricReport.h"
#include <Eigen/Core>
#include <memory>

namespace DistanceField {

// Forward declaration for PIMPL
class ReferenceTreeImpl;

// Defines a fitted occlusal plane with asymmetric offsets.
// After the user picks ≥3 cusp points, a least-squares plane is fitted.
// Only mesh vertices within [origin − belowMm·n, origin + aboveMm·n] are
// included in the accuracy statistics.
struct OcclusalPlane {
    Eigen::Vector3d normal = {0.0, 0.0, 1.0}; // unit normal pointing toward teeth (+Z after PCA)
    Eigen::Vector3d origin = {0.0, 0.0, 0.0}; // centroid of picked cusp points
    double aboveMm = 2.0;   // include vertices up to this far above the plane [mm]
    double belowMm = 12.0;  // include vertices up to this far below the plane [mm]
    bool   active  = false;
};

/**
 * Pre-built AABB tree for efficient distance queries against a reference mesh.
 * Use this when computing distances from multiple scans to the same reference.
 * Building the tree once and reusing it is much faster for large reference meshes.
 */
class ReferenceTree {
public:
    explicit ReferenceTree(const SurfaceMesh& referenceMesh);
    ~ReferenceTree();

    // Non-copyable but movable
    ReferenceTree(const ReferenceTree&) = delete;
    ReferenceTree& operator=(const ReferenceTree&) = delete;
    ReferenceTree(ReferenceTree&&) noexcept;
    ReferenceTree& operator=(ReferenceTree&&) noexcept;

    /**
     * Compute signed distances from scan vertices to this reference.
     * Results stored in scan->distanceToRef.
     */
    void computeDistances(ScanData& scan) const;

    /**
     * Compute signed distances from source mesh vertices to this reference.
     * Returns distances vector without modifying source.
     * Use this for batch pairwise comparisons.
     */
    std::vector<double> computePairwiseDistances(const SurfaceMesh& sourceMesh) const;

    /**
     * Get the reference mesh (for normal computation).
     */
    const SurfaceMesh& mesh() const;

private:
    std::unique_ptr<ReferenceTreeImpl> m_impl;
};

// Computes per-vertex signed distance from each scan to the GPA reference.
// Sign: positive = scan vertex is on the outer side of the reference normal.
// Stores results in scan->distanceToRef and sets scan->distanceComputed = true.
// Note: For multiple scans against the same reference, use ReferenceTree instead.
void compute(ScanData& scan, const ScanData& reference);

// Compute distances between two meshes without modifying either.
// Returns per-vertex signed distances from sourceMesh to targetMesh.
// This is more efficient for pairwise comparisons as it doesn't copy data.
// WARNING: This rebuilds the AABB tree for targetMesh each time. For batch
// pairwise comparisons, use computePairwiseWithTree instead.
std::vector<double> computePairwise(const SurfaceMesh& sourceMesh,
                                     const SurfaceMesh& targetMesh);

// Compute distances using a pre-built AABB tree.
// Use this for batch pairwise comparisons to avoid rebuilding the tree.
std::vector<double> computePairwiseWithTree(const SurfaceMesh& sourceMesh,
                                             const ReferenceTree& targetTree);

// Fills the local-accuracy section of report from scan->distanceToRef.
//
// Filter priority (highest wins):
//   toothMask non-empty → use per-vertex segmentation mask (from ToothSegmentation)
//   plane.active        → plane-based slab filter
//   zWindowMm > 0       → simple Z-max window (legacy fallback)
//   (none active)       → all vertices
//
// toothMask: per-vertex bool, size == scan.mesh.num_vertices().  Empty = unused.
void fillReport(const ScanData& scan, MetricReport& report,
                double coverageThreshold = 0.2,
                double zWindowMm = 0.0,
                const OcclusalPlane& plane = {},
                const std::vector<bool>& toothMask = {});

} // namespace DistanceField
