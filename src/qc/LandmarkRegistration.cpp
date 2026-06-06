// SPDX-License-Identifier: GPL-2.0-or-later
// SPDX-FileCopyrightText: 2024 Prof. Dr. Karl-Heinz Kunzelmann

#include "LandmarkRegistration.h"
#include "../core/DistanceField.h"
#include "../core/STLReader.h"

#include <Eigen/SVD>
#include <Eigen/Geometry>

#include <cmath>
#include <numeric>
#include <algorithm>

namespace DentScanBatch {

Eigen::Matrix4d LandmarkRegistration::computeKabschTransform(
    const std::vector<std::array<double, 3>>& scanPoints,
    const std::vector<std::array<double, 3>>& refPoints)
{
    // Need at least 3 points for a rigid transform
    if (scanPoints.size() < 3 || scanPoints.size() != refPoints.size()) {
        return Eigen::Matrix4d::Identity();
    }

    const int n = static_cast<int>(scanPoints.size());

    // Convert to Eigen matrices (3 x N)
    Eigen::MatrixXd P(3, n);  // scan points
    Eigen::MatrixXd Q(3, n);  // reference points

    for (int i = 0; i < n; ++i) {
        P(0, i) = scanPoints[i][0];
        P(1, i) = scanPoints[i][1];
        P(2, i) = scanPoints[i][2];
        Q(0, i) = refPoints[i][0];
        Q(1, i) = refPoints[i][1];
        Q(2, i) = refPoints[i][2];
    }

    // Compute centroids
    Eigen::Vector3d centroidP = P.rowwise().mean();
    Eigen::Vector3d centroidQ = Q.rowwise().mean();

    // Center the points
    Eigen::MatrixXd P_centered = P.colwise() - centroidP;
    Eigen::MatrixXd Q_centered = Q.colwise() - centroidQ;

    // Compute cross-covariance matrix H
    Eigen::Matrix3d H = P_centered * Q_centered.transpose();

    // SVD of H
    Eigen::JacobiSVD<Eigen::Matrix3d> svd(H, Eigen::ComputeFullU | Eigen::ComputeFullV);
    Eigen::Matrix3d U = svd.matrixU();
    Eigen::Matrix3d V = svd.matrixV();

    // Compute rotation matrix R = V * U^T
    Eigen::Matrix3d R = V * U.transpose();

    // Handle reflection case (det(R) = -1)
    if (R.determinant() < 0) {
        V.col(2) *= -1;
        R = V * U.transpose();
    }

    // Compute translation t = centroidQ - R * centroidP
    Eigen::Vector3d t = centroidQ - R * centroidP;

    // Build 4x4 transformation matrix
    Eigen::Matrix4d T = Eigen::Matrix4d::Identity();
    T.block<3, 3>(0, 0) = R;
    T.block<3, 1>(0, 3) = t;

    return T;
}

double LandmarkRegistration::computeLandmarkRMSE(
    const std::vector<std::array<double, 3>>& scanPoints,
    const std::vector<std::array<double, 3>>& refPoints,
    const Eigen::Matrix4d& transform)
{
    if (scanPoints.empty() || scanPoints.size() != refPoints.size()) {
        return 0.0;
    }

    double sumSqDist = 0.0;
    const int n = static_cast<int>(scanPoints.size());

    for (int i = 0; i < n; ++i) {
        Eigen::Vector4d p(scanPoints[i][0], scanPoints[i][1], scanPoints[i][2], 1.0);
        Eigen::Vector4d pTransformed = transform * p;

        double dx = pTransformed[0] - refPoints[i][0];
        double dy = pTransformed[1] - refPoints[i][1];
        double dz = pTransformed[2] - refPoints[i][2];

        sumSqDist += dx*dx + dy*dy + dz*dz;
    }

    return std::sqrt(sumSqDist / n);
}

void LandmarkRegistration::applyTransform(SurfaceMesh& mesh, const Eigen::Matrix4d& transform)
{
    for (auto v : mesh.vertices()) {
        Point3 p = mesh.point(v);
        Eigen::Vector4d pVec(p.x(), p.y(), p.z(), 1.0);
        Eigen::Vector4d pTransformed = transform * pVec;
        mesh.point(v) = Point3(pTransformed[0], pTransformed[1], pTransformed[2]);
    }
}

LandmarkResult LandmarkRegistration::registerWithLandmarks(
    std::shared_ptr<ScanData>& scan,
    const std::shared_ptr<SurfaceMesh>& reference,
    const std::vector<std::array<double, 3>>& scanLandmarks,
    const std::vector<std::array<double, 3>>& refLandmarks,
    const ICPRegistration::Params& icpParams)
{
    LandmarkResult result;

    if (!scan || !reference) {
        result.errorMessage = "Null scan or reference";
        return result;
    }

    if (scanLandmarks.size() < 3 || scanLandmarks.size() != refLandmarks.size()) {
        result.errorMessage = QString("Need at least 3 corresponding landmarks (got %1)")
            .arg(scanLandmarks.size());
        return result;
    }

    result.landmarkCount = static_cast<int>(scanLandmarks.size());

    // Step 1: Compute Kabsch transform from landmarks
    Eigen::Matrix4d kabschTransform = computeKabschTransform(scanLandmarks, refLandmarks);

    // Compute landmark RMSE before ICP
    result.landmarkRMSE = computeLandmarkRMSE(scanLandmarks, refLandmarks, kabschTransform);

    // Step 2: Apply initial transform to scan mesh
    applyTransform(scan->mesh, kabschTransform);

    // Update cumulative transform
    scan->transform = kabschTransform * scan->transform;

    // Step 3: Create a temporary ScanData for reference
    ScanData refData;
    refData.mesh = *reference;
    refData.registered = true;

    // Step 4: Run ICP refinement
    auto icpResult = ICPRegistration::align(*scan, refData, icpParams);

    if (icpResult.converged) {
        // Apply ICP transform
        ICPRegistration::applyTransform(*scan, icpResult.transform);
        scan->transform = icpResult.transform * scan->transform;
        result.finalRMSE = icpResult.finalRms;
    } else {
        // ICP didn't converge, but we still applied the Kabsch transform
        result.finalRMSE = result.landmarkRMSE;
    }

    scan->registered = true;
    result.transform = scan->transform;
    result.success = true;

    return result;
}

void LandmarkRegistration::recomputeDistances(
    std::shared_ptr<ScanData>& scan,
    const std::shared_ptr<SurfaceMesh>& reference)
{
    if (!scan || !reference) return;

    // Compute distances directly using the mesh
    // DistanceField::compute needs ScanData, so create a lightweight wrapper
    // that shares the mesh data (no deep copy)
    ScanData refData;
    refData.mesh = *reference;  // This is still a copy, but unavoidable with current API

    DistanceField::compute(*scan, refData);
}

void LandmarkRegistration::recomputeDistances(
    std::shared_ptr<ScanData>& scan,
    const ScanData& reference)
{
    if (!scan) return;

    // Direct call without copying
    DistanceField::compute(*scan, reference);
}

BatchMetricReport LandmarkRegistration::recomputeMetrics(
    const std::shared_ptr<ScanData>& scan,
    const std::vector<bool>& roiMask)
{
    BatchMetricReport report;

    if (!scan || !scan->distanceComputed) {
        return report;
    }

    report.scannerName = scan->scannerName;
    report.triangleCount = scan->triangleCount;
    report.verticesTotal = scan->mesh.num_vertices();

    // Filter distances based on ROI mask
    std::vector<double> filteredDistances;
    const auto& distances = scan->distanceToRef;

    if (!roiMask.empty() && roiMask.size() == distances.size()) {
        for (std::size_t i = 0; i < distances.size(); ++i) {
            if (roiMask[i]) {
                filteredDistances.push_back(distances[i]);
            }
        }
    } else {
        filteredDistances = distances;
    }

    if (filteredDistances.empty()) {
        return report;
    }

    report.verticesIncluded = filteredDistances.size();

    // Compute RMS
    double sumSq = 0.0;
    double sum = 0.0;
    for (double d : filteredDistances) {
        sumSq += d * d;
        sum += d;
    }
    report.rmsDistance = std::sqrt(sumSq / filteredDistances.size());
    report.signedMean = sum / filteredDistances.size();

    // Compute MAD (median absolute deviation)
    std::vector<double> absDistances;
    absDistances.reserve(filteredDistances.size());
    for (double d : filteredDistances) {
        absDistances.push_back(std::abs(d));
    }
    std::sort(absDistances.begin(), absDistances.end());

    std::size_t mid = absDistances.size() / 2;
    if (absDistances.size() % 2 == 0) {
        report.madDistance = (absDistances[mid - 1] + absDistances[mid]) / 2.0;
    } else {
        report.madDistance = absDistances[mid];
    }

    // Compute percentiles
    report.hausdorff100 = absDistances.back();

    std::size_t p95Index = static_cast<std::size_t>(absDistances.size() * 0.95);
    if (p95Index >= absDistances.size()) p95Index = absDistances.size() - 1;
    report.hausdorff95 = absDistances[p95Index];

    // Coverage rate (within 0.2mm)
    const double coverageThreshold = 0.2;
    int inCoverage = 0;
    for (double d : absDistances) {
        if (d <= coverageThreshold) ++inCoverage;
    }
    report.coverageRate = 100.0 * inCoverage / absDistances.size();

    return report;
}

std::shared_ptr<SurfaceMesh> LandmarkRegistration::loadGPAMean(const QString& stlPath)
{
    std::string errorMsg;
    auto scanData = STLReader::read(stlPath.toStdString(), errorMsg);

    if (!scanData || scanData->mesh.is_empty()) {
        return nullptr;
    }

    auto mesh = std::make_shared<SurfaceMesh>(std::move(scanData->mesh));
    return mesh;
}

} // namespace DentScanBatch
