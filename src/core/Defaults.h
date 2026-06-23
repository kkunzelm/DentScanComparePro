// SPDX-License-Identifier: GPL-2.0-or-later
// SPDX-FileCopyrightText: 2024 Prof. Dr. Karl-Heinz Kunzelmann

#pragma once

// Centralized default values for algorithm parameters.
// This ensures consistency between core algorithms (GPAReference, ICPRegistration)
// and configuration structures (StudyConfig, AlignmentConfig).
//
// When adding new parameters, define the default here and reference it from
// both the algorithm struct and the config struct.

namespace Defaults {

// --- Mean Mesh / Coverage Filtering ---
// These control the "boolean AND" intersection approach for excluding
// gingiva and soft tissue regions not consistently captured across scans.

// Maximum distance [mm] for a closest point to be valid during mean mesh
// computation. Smaller values = stricter intersection (excludes more).
//   - 0.15 mm: strict intersection for gingiva exclusion
//   - 0.3 mm: moderate (good balance)
//   - 0.5 mm: permissive, for artifact prevention only
constexpr double kMeanMeshMaxDistance = 0.15;

// Minimum fraction of scans that must have valid correspondences for a
// vertex to be included in the coverage mask. Range: 0.0 to 1.0.
//   - 0.9-1.0: strict intersection (all/most scans must agree)
//   - 0.5: permissive, allows 50% of scans to have holes
constexpr double kMeanMeshMinCoverage = 0.9;

// Minimum normal dot product for correspondence validity (0.0 to 1.0).
//   - 0.5: allow up to ~60° deviation
//   - 0.0: disable normal checking
constexpr double kMeanMeshMinNormalDot = 0.5;

// --- ICP Registration ---

// Maximum ICP iterations before giving up
constexpr int kMaxIcpIterations = 100;

// Convergence threshold [mm] for GPA reference displacement
constexpr double kConvergenceThreshold = 0.01;

// TrICP trim fraction: keep only this fraction of correspondences
// with the smallest point-to-plane residuals (1.0 = no trimming)
constexpr double kIcpTrimFraction = 1.0;

// --- ICP Hierarchy (coarse-to-fine) ---

// Whether to use resolution hierarchy by default
constexpr bool kUseIcpHierarchy = false;

// Xi-2025 boundary-preservation weight for curvature-weighted QEM
constexpr double kIcpHierarchyNegCurvK = 10.0;

// --- GPA ---

// Maximum GPA iterations
constexpr int kMaxGpaIterations = 20;

} // namespace Defaults
