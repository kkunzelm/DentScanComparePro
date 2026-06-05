#pragma once

#include "Mesh.h"
#include "MetricReport.h"

namespace TessellationMetrics {

// Computes per-face metrics (area, |mean curvature|, aspect ratio) and
// aggregates them into the MetricReport fields that don't require registration.
// Requires scan.curvatureComputed == true.
void compute(ScanData& scan);

// Fills the tessellation section of report from a fully computed ScanData.
void fillReport(const ScanData& scan, MetricReport& report);

// Spearman rank correlation between two vectors (used for ATI).
double spearmanCorrelation(const std::vector<double>& x, const std::vector<double>& y);

} // namespace TessellationMetrics
