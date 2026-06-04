#pragma once

#include "../core/Mesh.h"
#include "../core/ICPRegistration.h"
#include "../batch/GroupProcessor.h"
#include <QString>
#include <Eigen/Core>
#include <array>
#include <memory>
#include <vector>

namespace DentScanBatch {

/**
 * Result of landmark-based registration.
 */
struct LandmarkResult {
    bool success = false;
    Eigen::Matrix4d transform = Eigen::Matrix4d::Identity();
    double landmarkRMSE = 0.0;      // RMS error at landmarks before ICP
    double finalRMSE = 0.0;         // RMS error after ICP refinement
    int landmarkCount = 0;
    QString errorMessage;
};

/**
 * Landmark-based (corresponding points) registration using the Kabsch algorithm.
 *
 * Workflow:
 * 1. User picks N corresponding points on scan and reference
 * 2. Compute initial rigid transform using SVD (Kabsch algorithm)
 * 3. Apply initial transform to scan
 * 4. Run ICP refinement
 * 5. Recompute distances and metrics
 *
 * The Kabsch algorithm finds the optimal rotation matrix R and translation t
 * that minimizes the sum of squared distances between corresponding points:
 *   minimize sum_i || R * p_i + t - q_i ||^2
 * where p_i are scan points and q_i are reference points.
 */
class LandmarkRegistration {
public:
    /**
     * Compute rigid transform from corresponding point pairs.
     * Uses the Kabsch algorithm (SVD-based).
     *
     * @param scanPoints Points picked on the scan mesh
     * @param refPoints Corresponding points picked on the reference mesh
     * @return 4x4 transformation matrix (identity if fewer than 3 pairs)
     */
    static Eigen::Matrix4d computeKabschTransform(
        const std::vector<std::array<double, 3>>& scanPoints,
        const std::vector<std::array<double, 3>>& refPoints);

    /**
     * Compute RMSE between corresponding points after applying transform.
     *
     * @param scanPoints Points picked on the scan mesh
     * @param refPoints Corresponding points picked on the reference mesh
     * @param transform Transform to apply to scan points
     * @return RMSE in mm
     */
    static double computeLandmarkRMSE(
        const std::vector<std::array<double, 3>>& scanPoints,
        const std::vector<std::array<double, 3>>& refPoints,
        const Eigen::Matrix4d& transform);

    /**
     * Apply transform to a CGAL surface mesh (modifies in place).
     *
     * @param mesh The mesh to transform
     * @param transform 4x4 transformation matrix
     */
    static void applyTransform(SurfaceMesh& mesh, const Eigen::Matrix4d& transform);

    /**
     * Full landmark registration workflow:
     * 1. Compute Kabsch transform from landmarks
     * 2. Apply to scan mesh
     * 3. Run ICP refinement against reference
     * 4. Compute final RMSE
     *
     * @param scan Scan to register (mesh is modified in place)
     * @param reference Reference mesh
     * @param scanLandmarks Points picked on scan
     * @param refLandmarks Corresponding points on reference
     * @param icpParams ICP refinement parameters
     * @return Registration result
     */
    static LandmarkResult registerWithLandmarks(
        std::shared_ptr<ScanData>& scan,
        const std::shared_ptr<SurfaceMesh>& reference,
        const std::vector<std::array<double, 3>>& scanLandmarks,
        const std::vector<std::array<double, 3>>& refLandmarks,
        const ICPRegistration::Params& icpParams = {});

    /**
     * Recompute distances from scan to reference after re-registration.
     * Updates scan->distanceToRef in place.
     *
     * @param scan Scan with transformed mesh
     * @param reference Reference mesh
     */
    static void recomputeDistances(
        std::shared_ptr<ScanData>& scan,
        const std::shared_ptr<SurfaceMesh>& reference);

    /**
     * Recompute distances using a ScanData reference (avoids mesh copy).
     */
    static void recomputeDistances(
        std::shared_ptr<ScanData>& scan,
        const ScanData& reference);

    /**
     * Recompute trueness metrics after re-registration.
     *
     * @param scan Scan with recomputed distances
     * @param roiMask Per-vertex ROI mask (empty = use all vertices)
     * @return Updated metric report
     */
    static BatchMetricReport recomputeMetrics(
        const std::shared_ptr<ScanData>& scan,
        const std::vector<bool>& roiMask = {});

    /**
     * Load a previously saved GPA mean mesh from STL file.
     *
     * @param stlPath Path to GPA mean STL file
     * @return Loaded mesh (nullptr if failed)
     */
    static std::shared_ptr<SurfaceMesh> loadGPAMean(const QString& stlPath);
};

} // namespace DentScanBatch
