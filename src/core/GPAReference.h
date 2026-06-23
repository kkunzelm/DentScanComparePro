// SPDX-License-Identifier: GPL-2.0-or-later
// SPDX-FileCopyrightText: 2024 Prof. Dr. Karl-Heinz Kunzelmann

#pragma once

#include "Mesh.h"
#include "ICPRegistration.h"
#include <functional>
#include <memory>
#include <vector>

namespace GPAReference {

// Parameters for robust mean mesh computation.
// These control outlier rejection to prevent artifacts from holes and
// inconsistent scan coverage.
//
// The defaults are tuned for a "boolean AND" intersection approach that
// excludes gingiva and soft tissue regions not consistently captured
// across all scans.
struct MeanMeshParams {
    // Maximum distance [mm] for a closest point to be considered valid.
    // If a scan's closest point is farther than this, the scan likely has a
    // hole at this location and is excluded from the average for this vertex.
    // Set to 0 to disable distance rejection.
    //
    // For strict gingiva exclusion (intersection mode): use 0.1-0.15 mm
    // For artifact prevention only: use 0.5 mm
    double maxCorrespondenceDistance = 0.15;  // Strict default for intersection

    // Minimum dot product between reference vertex normal and scan surface
    // normal at the closest point. Values near 1.0 require nearly parallel
    // normals; 0.5 allows up to ~60° deviation; 0.0 disables normal checking.
    // When a scan has a hole, the closest point may be on a surface facing
    // a completely different direction — this check catches such cases.
    double minNormalDotProduct = 0.5;

    // Minimum fraction of scans that must have valid correspondences for a
    // vertex to be updated. If fewer scans pass the distance and normal
    // checks, the vertex position is kept unchanged (original template
    // position). Range: 0.0 to 1.0.
    //
    // For strict gingiva exclusion (intersection mode): use 0.9-1.0
    //   - 1.0 = ALL scans must have data (true boolean AND)
    //   - 0.9 = 90% of scans must have data (allows one outlier scan)
    // For artifact prevention only: use 0.5
    double minValidScansFraction = 0.9;  // Strict default for intersection

    // Enable verbose logging of rejection statistics.
    bool verbose = true;
};

// Result of mean mesh computation including coverage information.
// The coverage mask indicates which reference vertices have good coverage
// from all/most scans - only these should be used for metric computation.
struct MeanMeshResult {
    // Per-vertex coverage mask: true if the vertex has sufficient coverage
    // (enough scans contributed valid correspondences during mean mesh computation).
    // Size equals reference mesh vertex count.
    std::vector<bool> vertexCoverageMask;

    // Per-face coverage mask derived from vertex mask: true if all 3 vertices
    // of the face have good coverage. Use this when querying closest points
    // to filter out faces in poorly-covered regions (gingiva, scan boundaries).
    std::vector<bool> faceCoverageMask;

    // Statistics
    std::size_t validVertexCount = 0;
    std::size_t totalVertexCount = 0;
    std::size_t validFaceCount = 0;
    std::size_t totalFaceCount = 0;
};

struct Params {
    int    maxGPAIterations   = 20;
    double convergenceThresh  = 0.01;  // [mm] max mean displacement of reference
    ICPRegistration::Params icpParams;

    // If non-empty: use this scanner name as a fixed reference instead of
    // computing a GPA mean.  The named scanner is never ICP'd; all others
    // are aligned to it.  The mean-mesh update step is skipped.
    std::string fixedRefScannerName;

    // Skip pcaCoarseAlign() and resolveZRotation() when scans are already in
    // canonical orientation (e.g. from DentScanAlignPro).  Running PCA on
    // pre-oriented scans can introduce errors when patient geometry produces
    // eigenvectors at non-axis-aligned angles (e.g. 45° around Z).
    bool skipPcaCoarseAlign = false;

    // Per-scan vertex masks for masked ICP during GPA iterations.
    // Index must match the scans vector passed to compute().
    // If a mask is empty or the index is out of range, full-mesh ICP is used
    // for that scan.  Masks are kept fixed across GPA cycles (valid for
    // nearly-aligned scans; tooth masks are vertex-indexed and always valid).
    std::vector<std::vector<bool>> scanMasks;

    // Parameters for robust mean mesh computation (outlier rejection).
    MeanMeshParams meanMeshParams;
};

// Updates the GPA reference mesh vertices to the centroid of closest points
// on all aligned scans (AABB query per scan).  Call after re-registering
// to keep the mean surface consistent.
//
// The MeanMeshParams control outlier rejection:
// - Distance rejection: excludes closest points farther than maxCorrespondenceDistance
// - Normal consistency: excludes closest points with inconsistent surface normals
// - Minimum coverage: keeps original vertex position if too few scans have valid data
//
// These checks prevent artifacts (stretched triangles, self-intersections) in
// regions where scans have holes or inconsistent coverage.
//
// Returns MeanMeshResult containing coverage masks that indicate which vertices/faces
// have good coverage from all scans. Use these masks to filter metric computation
// and exclude gingiva/boundary regions that aren't consistently captured.
MeanMeshResult updateMeanMesh(
    ScanData& gpaRef,
    const std::vector<std::shared_ptr<ScanData>>& scans,
    const MeanMeshParams& params = {});

// Extract only faces with good coverage into a clean mesh.
// This creates a new mesh containing only faces where all 3 vertices passed
// the coverage test during mean mesh computation. Eliminates gingiva, soft
// tissue, and boundary artifacts from the reference mesh itself.
SurfaceMesh extractCoveredFaces(
    const SurfaceMesh& mesh,
    const std::vector<bool>& faceCoverageMask);

// Result of GPA computation including reference mesh and coverage information.
struct GPAResult {
    std::shared_ptr<ScanData> reference;  // The GPA reference surface
    MeanMeshResult coverage;              // Coverage masks for filtering metrics
};

// Runs Generalized Procrustes Analysis on all scans.
// - Picks the scan with the most triangles as initial reference.
// - Iteratively: register all to reference, compute new reference as mean,
//   check convergence.
// - Applies the final transforms in-place to scan->mesh.
// - Returns GPAResult containing the reference surface and coverage masks.
//   The coverage masks indicate which reference vertices/faces are consistently
//   captured by all scans - use these to filter out gingiva and boundary regions.
// progressCallback(gpaCycle, scanIndex, icpRms) is called each ICP step.
GPAResult compute(
    std::vector<std::shared_ptr<ScanData>>& scans,
    const Params& params = {},
    std::function<void(int, int, double)> progressCallback = nullptr
);

} // namespace GPAReference
