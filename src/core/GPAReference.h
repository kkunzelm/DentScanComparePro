#pragma once

#include "Mesh.h"
#include "ICPRegistration.h"
#include <functional>
#include <memory>
#include <vector>

namespace GPAReference {

struct Params {
    int    maxGPAIterations   = 20;
    double convergenceThresh  = 0.01;  // [mm] max mean displacement of reference
    ICPRegistration::Params icpParams;

    // If non-empty: use this scanner name as a fixed reference instead of
    // computing a GPA mean.  The named scanner is never ICP'd; all others
    // are aligned to it.  The mean-mesh update step is skipped.
    std::string fixedRefScannerName;
};

// Updates the GPA reference mesh vertices to the centroid of closest points
// on all aligned scans (AABB query per scan).  Call after re-registering
// to keep the mean surface consistent.
void updateMeanMesh(
    ScanData& gpaRef,
    const std::vector<std::shared_ptr<ScanData>>& scans);

// Runs Generalized Procrustes Analysis on all scans.
// - Picks the scan with the most triangles as initial reference.
// - Iteratively: register all to reference, compute new reference as mean,
//   check convergence.
// - Applies the final transforms in-place to scan->mesh.
// - Returns the GPA reference surface.
// progressCallback(gpaCycle, scanIndex, icpRms) is called each ICP step.
std::shared_ptr<ScanData> compute(
    std::vector<std::shared_ptr<ScanData>>& scans,
    const Params& params = {},
    std::function<void(int, int, double)> progressCallback = nullptr
);

} // namespace GPAReference
