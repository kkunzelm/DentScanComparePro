#pragma once

#include "Mesh.h"
#include <Eigen/Core>
#include <functional>
#include <vector>

namespace ICPRegistration {

struct Params {
    int    maxIterations   = 100;
    double convergenceRms  = 1e-4;   // [mm] stop when ΔRMS < this
    int    sampleCount     = 20000;  // points to sample from each mesh
    double maxCorrespDist  = 5.0;    // [mm] reject correspondences further than this
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

// Computes per-vertex normals for ICP.
std::vector<Eigen::Vector3d> computeVertexNormals(const ScanData& scan);

} // namespace ICPRegistration
