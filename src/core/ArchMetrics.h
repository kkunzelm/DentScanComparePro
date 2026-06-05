#pragma once

#include "Mesh.h"
#include "MetricReport.h"

namespace ArchMetrics {

// Detects boundary lengths and hole count from mesh topology.
void computeBoundaryMetrics(const ScanData& scan, MetricReport& report);

// Detects stitching artifacts as local normal discontinuities.
// Requires curvatureComputed to be true.
void computeStitchingArtifacts(const ScanData& scan, MetricReport& report);

// Placeholder for future arch-form and intermolar distance computation.
// Requires registration and curvature to be computed.
void computeArchMetrics(const ScanData& scan, MetricReport& report);

} // namespace ArchMetrics
