#include "GroupProcessor.h"
#include "../core/STLReader.h"
#include "../core/CurvatureAnalysis.h"
#include "../core/GPAReference.h"
#include "../core/DistanceField.h"
#include "../core/ToothSegmentation.h"
#include <QFileInfo>
#include <algorithm>
#include <cmath>
#include <numeric>
#include <map>
#include <iostream>

namespace DentScanBatch {

GroupProcessor::GroupProcessor(QObject* parent)
    : QObject(parent)
{
}

void GroupProcessor::cancel() {
    m_cancelled = true;
}

GroupResult GroupProcessor::process(
    const GroupConfig& group,
    const std::vector<DiscoveredFile>& files,
    const AlignmentConfig& alignment,
    const std::optional<ROITemplate>& roiTemplate)
{
    GroupResult result;
    result.groupId = group.id;
    result.skd_mm = group.skd_mm;
    m_cancelled = false;
    m_currentStep = 0;
    m_totalSteps = roiTemplate.has_value() && roiTemplate->useToothMask ? 7 : 6;

    if (files.empty()) {
        result.errors.append("No files to process");
        return result;
    }

    // Stage 1: Load STL files
    std::vector<std::shared_ptr<ScanData>> scans;
    if (!loadScans(files, scans, result)) {
        return result;
    }

    if (m_cancelled) return result;

    // Stage 2: Compute curvature (needed for occlusal plane detection and tooth segmentation)
    if (!computeCurvature(scans, result)) {
        return result;
    }

    if (m_cancelled) return result;

    // Stage 3: Run GPA alignment
    std::shared_ptr<SurfaceMesh> gpaMean;
    if (!runGPAAlignment(scans, alignment, gpaMean, result)) {
        return result;
    }
    result.gpaMean = gpaMean;

    if (m_cancelled) return result;

    // Stage 4: Compute distances to GPA mean
    if (!computeDistances(scans, gpaMean, result)) {
        return result;
    }

    if (m_cancelled) return result;

    // Stage 4.5 (optional): Compute tooth masks using template seeds
    std::vector<std::vector<bool>> toothMasks;
    if (roiTemplate.has_value() && roiTemplate->useToothMask && !roiTemplate->toothSeeds.empty()) {
        emit progressUpdated(++m_currentStep, m_totalSteps, "Computing tooth segmentation");
        std::cout << "    Computing tooth segmentation..." << std::flush;
        toothMasks = computeToothMasks(scans, *roiTemplate);
        std::cout << " done (" << toothMasks.size() << " masks)\n" << std::flush;
    }

    if (m_cancelled) return result;

    // Use ROI from template if provided, otherwise from group config
    const ROIConfig& effectiveROI = roiTemplate.has_value() ? roiTemplate->roi : group.roi;

    // Stage 5: Compute trueness metrics
    emit progressUpdated(++m_currentStep, m_totalSteps, "Computing trueness metrics");
    computeTruenessMetrics(scans, files, effectiveROI, group, toothMasks, result);

    if (m_cancelled) return result;

    // Stage 6: Compute precision metrics
    emit progressUpdated(++m_currentStep, m_totalSteps, "Computing precision metrics");
    computePrecisionMetrics(scans, files, effectiveROI, group, toothMasks, result);

    result.success = true;
    return result;
}

bool GroupProcessor::loadScans(
    const std::vector<DiscoveredFile>& files,
    std::vector<std::shared_ptr<ScanData>>& scans,
    GroupResult& result)
{
    emit progressUpdated(++m_currentStep, m_totalSteps, "Loading STL files");
    std::cout << "    Loading " << files.size() << " STL files..." << std::flush;

    scans.reserve(files.size());

    for (const auto& file : files) {
        if (m_cancelled) return false;

        std::string errorMsg;
        std::cout << "." << std::flush;
        auto scan = STLReader::read(file.path.toStdString(), errorMsg);

        if (!scan) {
            result.warnings.append(QString("Failed to load %1: %2")
                .arg(file.path, QString::fromStdString(errorMsg)));
            emit scanProcessed(file.path, false);
            continue;
        }

        scan->filePath = file.path.toStdString();
        scan->scannerName = file.scannerId.toStdString();

        // Cache basic stats
        scan->triangleCount = scan->mesh.number_of_faces();

        // Compute bounding box
        bool first = true;
        for (auto v : scan->mesh.vertices()) {
            const Point3& p = scan->mesh.point(v);
            if (first) {
                scan->boundsMin = {p.x(), p.y(), p.z()};
                scan->boundsMax = {p.x(), p.y(), p.z()};
                first = false;
            } else {
                scan->boundsMin[0] = std::min(scan->boundsMin[0], p.x());
                scan->boundsMin[1] = std::min(scan->boundsMin[1], p.y());
                scan->boundsMin[2] = std::min(scan->boundsMin[2], p.z());
                scan->boundsMax[0] = std::max(scan->boundsMax[0], p.x());
                scan->boundsMax[1] = std::max(scan->boundsMax[1], p.y());
                scan->boundsMax[2] = std::max(scan->boundsMax[2], p.z());
            }
        }

        scans.push_back(scan);
        emit scanProcessed(file.path, true);
    }

    if (scans.empty()) {
        result.errors.append("No files could be loaded");
        std::cout << " FAILED (no files loaded)\n" << std::flush;
        return false;
    }

    std::cout << " done (" << scans.size() << " loaded)\n" << std::flush;
    return true;
}

bool GroupProcessor::computeCurvature(
    std::vector<std::shared_ptr<ScanData>>& scans,
    GroupResult& result)
{
    emit progressUpdated(++m_currentStep, m_totalSteps, "Computing curvature");
    std::cout << "    Computing curvature..." << std::flush;

    for (auto& scan : scans) {
        if (m_cancelled) return false;
        std::cout << "." << std::flush;
        CurvatureAnalysis::compute(*scan);
    }

    std::cout << " done\n" << std::flush;
    return true;
}

bool GroupProcessor::runGPAAlignment(
    std::vector<std::shared_ptr<ScanData>>& scans,
    const AlignmentConfig& alignment,
    std::shared_ptr<SurfaceMesh>& gpaMean,
    GroupResult& result)
{
    emit progressUpdated(++m_currentStep, m_totalSteps, "Running GPA alignment");
    std::cout << "    Running GPA alignment..." << std::flush;

    if (scans.size() < 2) {
        result.warnings.append("Need at least 2 scans for GPA alignment");
        // Still create a reference from the single scan
        if (!scans.empty()) {
            gpaMean = std::make_shared<SurfaceMesh>(scans[0]->mesh);
            scans[0]->registered = true;
        }
        return true;
    }

    // Set up GPA parameters
    GPAReference::Params params;
    params.maxGPAIterations = 20;
    params.convergenceThresh = alignment.convergenceThreshold;
    params.icpParams.maxIterations = alignment.maxIcpIterations;

    // Run GPA
    auto gpaRef = GPAReference::compute(scans, params);

    if (!gpaRef) {
        result.errors.append("GPA alignment failed");
        return false;
    }

    // Extract the mesh from the GPA result
    gpaMean = std::make_shared<SurfaceMesh>(gpaRef->mesh);

    // Mark all scans as registered
    for (auto& scan : scans) {
        scan->registered = true;
    }

    std::cout << " done\n" << std::flush;
    return true;
}

bool GroupProcessor::computeDistances(
    std::vector<std::shared_ptr<ScanData>>& scans,
    const std::shared_ptr<SurfaceMesh>& gpaMean,
    GroupResult& result)
{
    emit progressUpdated(++m_currentStep, m_totalSteps, "Computing distance fields");
    std::cout << "    Computing distance fields..." << std::flush;

    if (!gpaMean) {
        result.errors.append("No GPA mean for distance computation");
        return false;
    }

    // Create a reference ScanData for distance computation
    ScanData refData;
    refData.mesh = *gpaMean;

    for (auto& scan : scans) {
        if (m_cancelled) return false;

        scan->distanceToRef.clear();
        DistanceField::compute(*scan, refData);

        if (scan->distanceToRef.empty()) {
            result.warnings.append(QString("Distance computation failed for: %1")
                .arg(QString::fromStdString(scan->filePath)));
            continue;
        }
        scan->distanceComputed = true;
        std::cout << "." << std::flush;
    }

    std::cout << " done\n" << std::flush;
    return true;
}

double GroupProcessor::computeOcclusalZ(const SurfaceMesh& mesh) const {
    // Simple approach: use the maximum Z value as the occlusal surface
    double maxZ = std::numeric_limits<double>::lowest();
    for (auto v : mesh.vertices()) {
        maxZ = std::max(maxZ, mesh.point(v).z());
    }
    return maxZ;
}

std::vector<bool> GroupProcessor::computeROIMask(
    const ScanData& scan,
    const ROIConfig& roi,
    double z_occlusal) const
{
    std::vector<bool> mask(scan.mesh.number_of_vertices(), false);
    std::size_t idx = 0;

    for (auto v : scan.mesh.vertices()) {
        const Point3& p = scan.mesh.point(v);
        mask[idx] = roi.isInROI(p.x(), p.y(), p.z(), z_occlusal);
        idx++;
    }

    return mask;
}

void GroupProcessor::computeTruenessMetrics(
    const std::vector<std::shared_ptr<ScanData>>& scans,
    const std::vector<DiscoveredFile>& files,
    const ROIConfig& roi,
    const GroupConfig& group,
    const std::vector<std::vector<bool>>& toothMasks,
    GroupResult& result)
{
    std::cout << "    Computing trueness metrics..." << std::flush;

    // Create a map from path to file info
    std::map<std::string, const DiscoveredFile*> fileMap;
    for (const auto& f : files) {
        fileMap[f.path.toStdString()] = &f;
    }

    for (std::size_t scanIdx = 0; scanIdx < scans.size(); scanIdx++) {
        const auto& scan = scans[scanIdx];
        if (!scan->distanceComputed || scan->distanceToRef.empty()) {
            continue;
        }

        BatchMetricReport report;
        report.scannerName = scan->scannerName;
        report.groupId = group.id;
        report.skd_mm = group.skd_mm;
        report.filePath = QString::fromStdString(scan->filePath);
        report.triangleCount = scan->triangleCount;
        report.verticesTotal = scan->mesh.number_of_vertices();

        // Get file info for repetition ID
        auto it = fileMap.find(scan->filePath);
        if (it != fileMap.end()) {
            report.repetitionId = it->second->repetitionId;
        }

        // Compute occlusal Z for this scan
        double z_occlusal = computeOcclusalZ(scan->mesh);

        // Apply ROI mask
        auto mask = computeROIMask(*scan, roi, z_occlusal);

        // Apply tooth mask if available
        if (scanIdx < toothMasks.size() && !toothMasks[scanIdx].empty()) {
            const auto& toothMask = toothMasks[scanIdx];
            for (std::size_t i = 0; i < mask.size() && i < toothMask.size(); i++) {
                mask[i] = mask[i] && toothMask[i];
            }
        }

        // Collect distances for vertices in ROI
        std::vector<double> distances;
        std::vector<double> absDistances;

        for (std::size_t i = 0; i < scan->distanceToRef.size(); i++) {
            if (mask[i]) {
                double d = scan->distanceToRef[i];
                distances.push_back(d);
                absDistances.push_back(std::abs(d));
            }
        }

        report.verticesIncluded = distances.size();

        if (distances.empty()) {
            result.warnings.append(QString("No vertices in ROI for: %1")
                .arg(QString::fromStdString(scan->filePath)));
            continue;
        }

        // Apply sigma clipping
        if (roi.outlierSigma > 0 && distances.size() > 10) {
            double mean = std::accumulate(distances.begin(), distances.end(), 0.0) / distances.size();
            double sq_sum = std::inner_product(distances.begin(), distances.end(), distances.begin(), 0.0);
            double stddev = std::sqrt(sq_sum / distances.size() - mean * mean);
            double threshold = roi.outlierSigma * stddev;

            std::vector<double> filteredDist;
            std::vector<double> filteredAbs;
            for (std::size_t i = 0; i < distances.size(); i++) {
                if (std::abs(distances[i] - mean) <= threshold) {
                    filteredDist.push_back(distances[i]);
                    filteredAbs.push_back(absDistances[i]);
                }
            }
            distances = filteredDist;
            absDistances = filteredAbs;
            report.verticesIncluded = distances.size();
        }

        // Compute metrics
        // RMS
        double sq_sum = 0.0;
        for (double d : distances) {
            sq_sum += d * d;
        }
        report.rmsDistance = std::sqrt(sq_sum / distances.size());

        // Mean absolute
        double abs_sum = std::accumulate(absDistances.begin(), absDistances.end(), 0.0);
        report.madDistance = abs_sum / absDistances.size();

        // Signed mean (bias)
        report.signedMean = std::accumulate(distances.begin(), distances.end(), 0.0) / distances.size();

        // Sort for percentiles
        std::sort(absDistances.begin(), absDistances.end());

        // Max (Hausdorff 100)
        report.hausdorff100 = absDistances.back();

        // 95th percentile
        std::size_t p95_idx = static_cast<std::size_t>(0.95 * absDistances.size());
        report.hausdorff95 = absDistances[p95_idx];

        // Coverage rate (% within 0.2 mm)
        std::size_t withinThreshold = std::count_if(absDistances.begin(), absDistances.end(),
            [](double d) { return d <= 0.2; });
        report.coverageRate = 100.0 * withinThreshold / absDistances.size();

        result.truenessReports.push_back(report);
        std::cout << "." << std::flush;
    }

    std::cout << " done (" << result.truenessReports.size() << " scans)\n" << std::flush;
}

void GroupProcessor::computePrecisionMetrics(
    const std::vector<std::shared_ptr<ScanData>>& scans,
    const std::vector<DiscoveredFile>& files,
    const ROIConfig& roi,
    const GroupConfig& group,
    const std::vector<std::vector<bool>>& toothMasks,
    GroupResult& result)
{
    std::cout << "    Computing precision metrics..." << std::flush;

    // Build index map from scan pointer to index (for tooth mask lookup)
    std::map<const ScanData*, std::size_t> scanToIndex;
    for (std::size_t i = 0; i < scans.size(); i++) {
        scanToIndex[scans[i].get()] = i;
    }

    // Group scans by scanner
    std::map<std::string, std::vector<std::shared_ptr<ScanData>>> scansByScanner;
    for (const auto& scan : scans) {
        scansByScanner[scan->scannerName].push_back(scan);
    }

    // For each scanner, compute pairwise precision
    for (const auto& [scannerId, scannerScans] : scansByScanner) {
        if (scannerScans.size() < 2) {
            continue;  // Need at least 2 scans for precision
        }

        PrecisionReport report;
        report.scannerId = QString::fromStdString(scannerId);
        report.groupId = group.id;
        report.skd_mm = group.skd_mm;

        std::vector<double> pairwiseRMS;

        // Compute pairwise RMS between all scan pairs
        // Use optimized computePairwise to avoid copying ScanData
        for (std::size_t i = 0; i < scannerScans.size(); i++) {
            for (std::size_t j = i + 1; j < scannerScans.size(); j++) {
                const auto& scan1 = scannerScans[i];
                const auto& scan2 = scannerScans[j];

                // Use optimized function that works directly on meshes
                std::vector<double> distances = DistanceField::computePairwise(
                    scan1->mesh, scan2->mesh);

                if (distances.empty()) {
                    continue;
                }

                // Get occlusal Z for ROI
                double z_occlusal = computeOcclusalZ(scan1->mesh);

                // Apply ROI mask
                auto mask = computeROIMask(*scan1, roi, z_occlusal);

                // Apply tooth mask if available (use scan1's mask for the pair)
                auto it = scanToIndex.find(scan1.get());
                if (it != scanToIndex.end()) {
                    std::size_t scanIdx = it->second;
                    if (scanIdx < toothMasks.size() && !toothMasks[scanIdx].empty()) {
                        const auto& toothMask = toothMasks[scanIdx];
                        for (std::size_t k = 0; k < mask.size() && k < toothMask.size(); k++) {
                            mask[k] = mask[k] && toothMask[k];
                        }
                    }
                }

                double sq_sum = 0.0;
                std::size_t count = 0;
                for (std::size_t k = 0; k < distances.size() && k < mask.size(); k++) {
                    if (mask[k]) {
                        double d = distances[k];
                        sq_sum += d * d;
                        count++;
                    }
                }

                if (count > 0) {
                    double rms = std::sqrt(sq_sum / count);
                    pairwiseRMS.push_back(rms);
                }
            }
        }

        if (pairwiseRMS.empty()) {
            continue;
        }

        // Compute mean and SD of pairwise RMS
        report.pairwiseCount = static_cast<int>(pairwiseRMS.size());
        report.meanRMS = std::accumulate(pairwiseRMS.begin(), pairwiseRMS.end(), 0.0) / pairwiseRMS.size();

        if (pairwiseRMS.size() > 1) {
            double sq_sum = 0.0;
            for (double r : pairwiseRMS) {
                sq_sum += (r - report.meanRMS) * (r - report.meanRMS);
            }
            report.sdRMS = std::sqrt(sq_sum / (pairwiseRMS.size() - 1));
            report.cv = (report.meanRMS > 0) ? (report.sdRMS / report.meanRMS) : 0.0;
        }

        result.precisionReports.push_back(report);
        std::cout << "." << std::flush;
    }

    std::cout << " done (" << result.precisionReports.size() << " scanner groups)\n" << std::flush;
}

std::vector<std::vector<bool>> GroupProcessor::computeToothMasks(
    const std::vector<std::shared_ptr<ScanData>>& scans,
    const ROITemplate& roiTemplate) const
{
    std::vector<std::vector<bool>> masks;
    masks.reserve(scans.size());

    // Set up segmentation parameters from template
    ToothSegmentation::Params params;
    params.maxGeodesicMm = roiTemplate.segMaxGeodesicMm;
    params.maxCreaseAngleDeg = roiTemplate.segMaxCreaseAngleDeg;
    params.minMeanCurvature = roiTemplate.segMinMeanCurvature;
    params.curvatureRepulsion = roiTemplate.segCurvatureRepulsion;

    for (const auto& scan : scans) {
        if (roiTemplate.toothSeeds.empty()) {
            // No seeds, return empty mask (all false)
            masks.push_back(std::vector<bool>(scan->mesh.number_of_vertices(), false));
            continue;
        }

        // Run tooth segmentation using the curvature-weighted Dijkstra algorithm
        // segmentFromPoints handles snapping seed points to nearest mesh vertices
        std::vector<bool> mask = ToothSegmentation::segmentFromPoints(
            *scan,
            roiTemplate.toothSeeds,
            params
        );

        masks.push_back(std::move(mask));
    }

    return masks;
}

} // namespace DentScanBatch
