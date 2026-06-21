// SPDX-License-Identifier: GPL-2.0-or-later
// SPDX-FileCopyrightText: 2024 Prof. Dr. Karl-Heinz Kunzelmann

#include "GroupProcessor.h"
#include "../core/STLReader.h"
#include "../core/CurvatureAnalysis.h"
#include "../core/GPAReference.h"
#include "../core/DistanceField.h"
#include "../core/ToothSegmentation.h"
#include "../core/AlignmentTransformLoader.h"
#include "../core/TessellationMetrics.h"
#include "../core/ArchMetrics.h"
#include "../qc/QCExporter.h"
#include <QFileInfo>
#include <QFile>
#include <algorithm>
#include <cmath>
#include <numeric>
#include <map>
#include <unordered_map>
#include <iostream>
#include <iomanip>

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
    const std::optional<ROITemplate>& roiTemplate,
    const QString& outputDir,
    const QString& externalRefPath,
    bool scansPreAligned,
    const std::map<std::string, Eigen::Matrix4d>& precomputedTransforms,
    bool forceFullMesh,
    bool computePrecision,
    bool scansNormalized)
{
    GroupResult result;
    result.groupId = group.id;
    result.conditionValue = group.conditionValue;
    m_cancelled = false;
    m_currentStep = 0;

    // Per-group ROI template overrides the study-wide template when set
    std::optional<ROITemplate> resolvedTemplate = roiTemplate;
    if (!forceFullMesh && !group.roiTemplatePath.isEmpty()) {
        if (QFile::exists(group.roiTemplatePath)) {
            try {
                resolvedTemplate = ROITemplate::loadFromFile(group.roiTemplatePath);
                std::cout << "    Per-group ROI template: "
                          << group.roiTemplatePath.toStdString() << "\n" << std::flush;
            } catch (const std::exception& e) {
                result.warnings.append(QString("Failed to load per-group ROI template '%1': %2")
                    .arg(group.roiTemplatePath).arg(e.what()));
            }
        } else {
            result.warnings.append(QString("Per-group ROI template not found: %1")
                .arg(group.roiTemplatePath));
        }
    }

    // Calculate total steps based on options
    // Curvature is skipped when scans are pre-aligned and no ROI tooth mask is needed
    bool needCurvature = !scansPreAligned || (resolvedTemplate.has_value() && resolvedTemplate->useToothMask && !resolvedTemplate->toothSeeds.empty());

    int steps = 4; // Load, Distances, Trueness, Precision
    if (needCurvature) steps++; // Curvature + tessellation
    if (externalRefPath.isEmpty()) {
        steps++; // GPA alignment or pre-aligned ICP refinement
    }
    if (resolvedTemplate.has_value() && resolvedTemplate->useToothMask && !resolvedTemplate->toothSeeds.empty()) {
        steps++; // Tooth segmentation (now before alignment)
    }
    m_totalSteps = steps;

    if (files.empty()) {
        result.errors.append("No files to process");
        return result;
    }

    // Effective ROI for both ICP masking and metric evaluation.
    // Computed once here so all stages share the same config.
    const ROIConfig effectiveROI = (!forceFullMesh && resolvedTemplate.has_value())
                                   ? resolvedTemplate->roi : group.roi;

    // Whether to apply the geometric ROI to the REFERENCE mesh (recommended approach).
    // Masking the reference once means the absolute-coordinate ROI only needs to
    // match the canonical reference frame; individual source scans are aligned to
    // the masked reference via standard unmasked ICP regardless of their starting
    // position. Geometric-only (bbox / z-plane / brush) — tooth masks cannot be
    // transferred to the reference without re-running segmentation on it.
    const bool useROIRef = !forceFullMesh &&
        (effectiveROI.bbox.active || effectiveROI.zPlane.active ||
         !effectiveROI.brushZones.empty());

    // Stage 1: Load STL files
    std::vector<std::shared_ptr<ScanData>> scans;
    if (!loadScans(files, scans, result)) {
        return result;
    }

    if (wasCancelled()) return result;

    // Stage 2: Compute curvature (needed for GPA Z-sign resolution and tooth segmentation)
    // Skipped when scans are pre-aligned and no ROI tooth mask is active —
    // DentScanAlignPro has already resolved canonical orientation, making curvature redundant.
    if (needCurvature) {
        if (!computeCurvature(scans, result)) {
            return result;
        }
    } else {
        std::cout << "    Curvature: SKIPPED (pre-aligned, no tooth mask)\n" << std::flush;
    }

    if (wasCancelled()) return result;

    // Stage 2.5: Apply precomputed transforms from DentScanAlign (if provided)
    if (!precomputedTransforms.empty()) {
        std::cout << "    Applying precomputed transforms..." << std::flush;
        int applied = 0;
        for (auto& scan : scans) {
            std::string normalizedKey = AlignmentTransformLoader::normalizeFilePath(
                QString::fromStdString(scan->filePath));
            auto it = precomputedTransforms.find(normalizedKey);
            if (it != precomputedTransforms.end()) {
                // Apply the precomputed transform
                ICPRegistration::applyTransform(*scan, it->second);
                scan->transform = it->second;
                applied++;
                std::cout << "." << std::flush;
            }
        }
        std::cout << " done (" << applied << "/" << scans.size() << " transforms applied)\n" << std::flush;
    }

    if (wasCancelled()) return result;

    // Stage 3 (MOVED EARLIER): Compute tooth masks BEFORE alignment
    // This allows masked ICP to focus on tooth surfaces
    // Skip when forceFullMesh is set (user disabled ROI/masked ICP in GUI)
    std::vector<std::vector<bool>> toothMasks;
    if (!forceFullMesh && resolvedTemplate.has_value() && resolvedTemplate->useToothMask && !resolvedTemplate->toothSeeds.empty()) {
        emit progressUpdated(++m_currentStep, m_totalSteps, "Computing tooth segmentation");
        std::cout << "    Computing tooth segmentation..." << std::flush;
        toothMasks = computeToothMasks(scans, *resolvedTemplate);
        std::cout << " done (" << toothMasks.size() << " masks)\n" << std::flush;
    } else if (forceFullMesh) {
        std::cout << "    Tooth segmentation: SKIPPED (full-mesh mode)\n" << std::flush;
    }

    if (wasCancelled()) return result;

    // Build combined ICP masks (geometric ROI + tooth mask) per scan.
    // These are used during alignment so ICP focuses on the same region
    // that will be evaluated in the metrics.  Computed once here; kept fixed
    // across GPA cycles (valid because GPA moves scans by only small amounts).
    std::vector<std::vector<bool>> icpMasks;
    if (!forceFullMesh && hasActiveROI(effectiveROI, !toothMasks.empty())) {
        std::cout << "    Building ICP masks (";
        if (effectiveROI.bbox.active)              std::cout << "bbox ";
        if (effectiveROI.zPlane.active)            std::cout << "z-plane ";
        if (!effectiveROI.brushZones.empty())      std::cout << "brush ";
        if (!toothMasks.empty())                   std::cout << "teeth";
        std::cout << ")..." << std::flush;
        icpMasks.reserve(scans.size());
        for (std::size_t i = 0; i < scans.size(); ++i) {
            const std::vector<bool>& tm = (i < toothMasks.size()) ? toothMasks[i] : std::vector<bool>();
            icpMasks.push_back(computeCombinedICPMask(*scans[i], effectiveROI, tm));
        }
        std::cout << " done (" << icpMasks.size() << " masks)\n" << std::flush;
    }

    // Stage 4: Get reference mesh (either external or GPA-computed)
    std::shared_ptr<SurfaceMesh> referenceMesh;

    if (!externalRefPath.isEmpty()) {
        // Load external reference
        emit progressUpdated(++m_currentStep, m_totalSteps, "Loading external reference");
        std::cout << "    Loading external reference: " << externalRefPath.toStdString() << "..." << std::flush;

        std::string errorMsg;
        auto refScan = STLReader::read(externalRefPath.toStdString(), errorMsg);
        if (!refScan) {
            result.errors.append(QString("Failed to load external reference: %1").arg(QString::fromStdString(errorMsg)));
            std::cout << " FAILED\n" << std::flush;
            return result;
        }
        referenceMesh = std::make_shared<SurfaceMesh>(refScan->mesh);
        std::cout << " done (" << referenceMesh->number_of_faces() << " triangles)\n" << std::flush;

        // Pre-aligned scans skip GPA but still need ICP refinement against reference
        if (scansPreAligned) {
            std::cout << "    Scans pre-aligned (skipping GPA, running ICP refinement)\n" << std::flush;
        }

        // Run ICP alignment against external reference.
        // Apply ROI to the reference once; align each source scan (full mesh) to it.
        emit progressUpdated(++m_currentStep, m_totalSteps, "ICP refinement against reference");

        ScanData refData;
        refData.registered = true;
        if (useROIRef) {
            auto masked = extractROIReference(*referenceMesh, effectiveROI);
            if (masked->mesh.number_of_vertices() > 0) {
                refData = *masked;
                std::cout << "    ICP reference (ROI-masked): "
                          << refData.mesh.number_of_vertices() << " / "
                          << referenceMesh->number_of_vertices() << " vertices\n" << std::flush;
            } else {
                std::cout << "    WARNING: ROI reference empty — using full mesh\n" << std::flush;
                refData.mesh = *referenceMesh;
            }
        } else {
            refData.mesh = *referenceMesh;
        }

        ICPRegistration::Params icpParams;
        icpParams.maxIterations = alignment.maxIcpIterations;
        icpParams.convergenceRms = alignment.convergenceThreshold;
        if (scansPreAligned || !precomputedTransforms.empty()) {
            icpParams.maxCorrespDist = 5.0;
        }
        icpParams.trimFraction     = alignment.icpTrimFraction;
        icpParams.useHierarchy     = alignment.useIcpHierarchy;
        icpParams.hierarchyLevels  = alignment.icpHierarchyLevels;
        icpParams.negCurvK         = alignment.icpHierarchyNegCurvK;

        std::cout << "    Running ICP refinement against reference..." << std::flush;
        for (auto& scan : scans) {
            if (wasCancelled()) return result;
            std::cout << "." << std::flush;

            ICPRegistration::Result icpResult = icpParams.useHierarchy
                ? ICPRegistration::alignHierarchical(*scan, refData, icpParams)
                : ICPRegistration::align(*scan, refData, icpParams);

            if (icpResult.converged) {
                ICPRegistration::applyTransform(*scan, icpResult.transform);
                scan->registered = true;
            } else {
                result.warnings.append(QString("ICP did not converge for: %1")
                    .arg(QString::fromStdString(scan->filePath)));
            }
        }
        std::cout << " done\n" << std::flush;
    } else {
        if (scansPreAligned) {
            // Pre-aligned: ICP refinement against the scan with the most triangles,
            // then compute mean mesh. Apply ROI to the initial reference scan so ICP
            // targets the ROI region only — source scans are aligned full-mesh to it.
            emit progressUpdated(++m_currentStep, m_totalSteps, "ICP refinement (pre-aligned)");

            auto refIt = std::max_element(scans.begin(), scans.end(),
                [](const auto& a, const auto& b){ return a->triangleCount < b->triangleCount; });

            ScanData initRefData;
            initRefData.registered = true;
            if (useROIRef) {
                auto masked = extractROIReference((*refIt)->mesh, effectiveROI);
                if (masked->mesh.number_of_vertices() > 0) {
                    initRefData = *masked;
                    std::cout << "    Pre-aligned mode: ICP reference (ROI-masked): "
                              << initRefData.mesh.number_of_vertices() << " / "
                              << (*refIt)->mesh.number_of_vertices() << " vertices\n" << std::flush;
                } else {
                    std::cout << "    WARNING: ROI reference empty — using full mesh\n" << std::flush;
                    initRefData.mesh = (*refIt)->mesh;
                }
            } else {
                initRefData.mesh = (*refIt)->mesh;
                std::cout << "    Pre-aligned mode: running ICP refinement..." << std::flush;
            }

            ICPRegistration::Params icpParams;
            icpParams.maxIterations   = alignment.maxIcpIterations;
            icpParams.maxCorrespDist  = 10.0;  // larger window: cross-scanner centroid offsets can exceed 5mm
            icpParams.convergenceRms  = alignment.convergenceThreshold;
            icpParams.trimFraction    = alignment.icpTrimFraction;
            icpParams.useHierarchy    = alignment.useIcpHierarchy;
            icpParams.hierarchyLevels = alignment.icpHierarchyLevels;
            icpParams.negCurvK        = alignment.icpHierarchyNegCurvK;

            for (auto& scan : scans) {
                if (wasCancelled()) return result;

                ICPRegistration::Result r = icpParams.useHierarchy
                    ? ICPRegistration::alignHierarchical(*scan, initRefData, icpParams)
                    : ICPRegistration::align(*scan, initRefData, icpParams);

                if (r.converged)
                    ICPRegistration::applyTransform(*scan, r.transform);
                scan->registered = true;
                std::cout << "\n      " << scan->scannerName
                          << "  res=" << std::fixed << std::setprecision(4) << r.finalRms << " mm"
                          << "  iter=" << r.iterations
                          << (r.converged ? "" : "  [NOT CONVERGED]") << std::flush;
            }
            std::cout << "\n    done\n" << std::flush;

            ScanData gpaMeanData;
            gpaMeanData.mesh = (*refIt)->mesh;
            GPAReference::updateMeanMesh(gpaMeanData, scans);
            referenceMesh = std::make_shared<SurfaceMesh>(gpaMeanData.mesh);
        } else {
            // Run GPA alignment (passes icpMasks so GPA iterations also use masked ICP)
            if (!runGPAAlignment(scans, alignment, referenceMesh, result, scansNormalized, icpMasks)) {
                return result;
            }
        }
    }
    result.gpaMean = referenceMesh;

    if (wasCancelled()) return result;

    // Stage 5: Compute distances to ROI-masked reference.
    // The ROI is applied to the REFERENCE once here; every source scan (full mesh)
    // is measured against this masked reference. Source vertices far from the ROI
    // region naturally receive large distances and are excluded from metrics by
    // maxMetricDist below — no per-scan coordinate-dependent ROI masking needed.
    std::shared_ptr<SurfaceMesh> distRefMesh = referenceMesh;
    if (useROIRef) {
        auto maskedDist = extractROIReference(*referenceMesh, effectiveROI);
        if (maskedDist->mesh.number_of_vertices() > 0) {
            distRefMesh = std::make_shared<SurfaceMesh>(maskedDist->mesh);
            std::cout << "    Distance reference (ROI-masked): "
                      << maskedDist->mesh.number_of_vertices() << " / "
                      << referenceMesh->number_of_vertices() << " vertices\n" << std::flush;
            // Save ROI-masked reference so it can be visually compared to the aligned scans
            if (!outputDir.isEmpty())
                QCExporter::exportReferenceMesh(distRefMesh, outputDir,
                                                result.groupId + "_roi");
        } else {
            std::cout << "    WARNING: ROI distance reference empty — using full reference\n" << std::flush;
        }
    }

    if (!computeDistances(scans, distRefMesh, result)) {
        return result;
    }

    if (wasCancelled()) return result;

    // maxMetricDist: source vertices whose distance to the masked reference exceeds
    // this value are outside the ROI region and excluded from trueness metrics.
    // When no geometric ROI is active, all vertices are included (max double).
    const double maxMetricDist = useROIRef ? 5.0 : std::numeric_limits<double>::max();

    const ROIConfig& metricsROI = forceFullMesh ? group.roi : effectiveROI;

    // Stage 6: Compute trueness metrics
    emit progressUpdated(++m_currentStep, m_totalSteps, "Computing trueness metrics");
    computeTruenessMetrics(scans, files, metricsROI, group, toothMasks, result, maxMetricDist);

    if (wasCancelled()) return result;

    // Stage 7: Compute precision metrics (optional — skip when compute_precision=false)
    if (computePrecision) {
        emit progressUpdated(++m_currentStep, m_totalSteps, "Computing precision metrics");
        computePrecisionMetrics(scans, files, metricsROI, group, toothMasks, result);
    } else {
        std::cout << "    Precision metrics: SKIPPED (compute_precision=false)\n" << std::flush;
    }

    if (wasCancelled()) return result;

    // Stage 8 (optional): Export QC data.
    // Before writing the difference PLY files, zero out distanceToRef for vertices
    // outside the geometric ROI so they appear neutral (white) in MeshLab instead
    // of showing as coloured outliers. Metrics are already computed above so this
    // modification does not affect any numerical results.
    if (!outputDir.isEmpty()) {
        if (useROIRef) {
            for (auto& scan : scans) {
                double z_occ = computeOcclusalZ(scan->mesh);
                std::size_t idx = 0;
                for (auto v : scan->mesh.vertices()) {
                    const auto& p = scan->mesh.point(v);
                    if (!effectiveROI.isInROI(p.x(), p.y(), p.z(), z_occ)) {
                        if (idx < scan->distanceToRef.size())
                            scan->distanceToRef[idx] = 0.0;
                    }
                    ++idx;
                }
            }
        }
        exportQCData(result, scans, files, outputDir, toothMasks);
    }

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
        if (wasCancelled()) return false;

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
    emit progressUpdated(++m_currentStep, m_totalSteps, "Computing curvature and tessellation metrics");
    std::cout << "    Computing curvature..." << std::flush;

    for (auto& scan : scans) {
        if (wasCancelled()) return false;
        std::cout << "." << std::flush;
        CurvatureAnalysis::compute(*scan);
    }

    std::cout << " done\n" << std::flush;

    // Compute tessellation metrics (requires curvature to be computed first)
    std::cout << "    Computing tessellation metrics..." << std::flush;
    for (auto& scan : scans) {
        if (wasCancelled()) return false;
        std::cout << "." << std::flush;
        TessellationMetrics::compute(*scan);
    }
    std::cout << " done\n" << std::flush;

    return true;
}

bool GroupProcessor::runGPAAlignment(
    std::vector<std::shared_ptr<ScanData>>& scans,
    const AlignmentConfig& alignment,
    std::shared_ptr<SurfaceMesh>& gpaMean,
    GroupResult& result,
    bool scansNormalized,
    const std::vector<std::vector<bool>>& icpMasks)
{
    emit progressUpdated(++m_currentStep, m_totalSteps, "Running GPA alignment");
    if (scansNormalized)
        std::cout << "    Running GPA alignment (PCA skipped — scans normalized):\n" << std::flush;
    else
        std::cout << "    Running GPA alignment:\n" << std::flush;

    if (!icpMasks.empty())
        std::cout << "    GPA will use masked ICP (" << icpMasks.size() << " masks)\n" << std::flush;

    if (scans.size() < 2) {
        result.warnings.append("Need at least 2 scans for GPA alignment");
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
    params.icpParams.maxIterations  = alignment.maxIcpIterations;
    params.skipPcaCoarseAlign       = scansNormalized;
    params.icpParams.trimFraction   = alignment.icpTrimFraction;
    params.icpParams.useHierarchy   = alignment.useIcpHierarchy;
    params.icpParams.hierarchyLevels = alignment.icpHierarchyLevels;
    params.icpParams.negCurvK       = alignment.icpHierarchyNegCurvK;
    params.scanMasks = icpMasks;

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

    // Build AABB tree once for the reference mesh (important for large references!)
    std::cout << "\n";
    DistanceField::ReferenceTree refTree(*gpaMean);
    std::cout << "    Computing distances";

    for (auto& scan : scans) {
        if (wasCancelled()) return false;

        scan->distanceToRef.clear();
        refTree.computeDistances(*scan);

        if (scan->distanceToRef.empty()) {
            result.warnings.append(QString("Distance computation failed for: %1")
                .arg(QString::fromStdString(scan->filePath)));
            continue;
        }
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

bool GroupProcessor::hasActiveROI(const ROIConfig& roi, bool hasToothMask) const
{
    // Check if any ROI component is active
    return roi.bbox.active || roi.zPlane.active || !roi.brushZones.empty() || hasToothMask;
}

std::vector<bool> GroupProcessor::computeCombinedICPMask(
    const ScanData& scan,
    const ROIConfig& roi,
    const std::vector<bool>& toothMask) const
{
    std::size_t numVertices = scan.mesh.number_of_vertices();
    std::vector<bool> mask(numVertices, true);  // Start with all included

    // Compute occlusal Z for plane slab
    double z_occlusal = computeOcclusalZ(scan.mesh);

    std::size_t idx = 0;
    for (auto v : scan.mesh.vertices()) {
        const Point3& p = scan.mesh.point(v);

        // Apply bounding box (if active)
        if (roi.bbox.active) {
            if (!roi.bbox.contains(p.x(), p.y(), p.z())) {
                mask[idx] = false;
                idx++;
                continue;
            }
        }

        // Apply plane slab (if active)
        if (roi.zPlane.active) {
            if (!roi.zPlane.contains(p.z(), z_occlusal)) {
                mask[idx] = false;
                idx++;
                continue;
            }
        }

        // Apply brush zones (override previous decisions)
        for (const auto& zone : roi.brushZones) {
            if (zone.contains(p.x(), p.y(), p.z())) {
                mask[idx] = zone.include;
            }
        }

        idx++;
    }

    // Apply tooth mask (AND with existing mask)
    if (!toothMask.empty() && toothMask.size() == numVertices) {
        for (std::size_t i = 0; i < numVertices; i++) {
            mask[i] = mask[i] && toothMask[i];
        }
    }

    return mask;
}

void GroupProcessor::computeTruenessMetrics(
    const std::vector<std::shared_ptr<ScanData>>& scans,
    const std::vector<DiscoveredFile>& files,
    const ROIConfig& roi,
    const GroupConfig& group,
    const std::vector<std::vector<bool>>& toothMasks,
    GroupResult& result,
    double maxMetricDist)
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
        report.conditionValue = group.conditionValue;
        report.filePath = QString::fromStdString(scan->filePath);
        report.triangleCount = scan->triangleCount;
        report.verticesTotal = scan->mesh.number_of_vertices();

        // Get file info for repetition ID
        auto it = fileMap.find(scan->filePath);
        if (it != fileMap.end()) {
            report.repetitionId = it->second->repetitionId;
        }

        // Fill tessellation metrics (ATI, edge length, aspect ratio, densities)
        TessellationMetrics::fillReport(*scan, report);

        // Fill arch/completeness metrics (boundary length, holes, stitching angle)
        ArchMetrics::computeBoundaryMetrics(*scan, report);
        ArchMetrics::computeStitchingArtifacts(*scan, report);

        // Collect distances.
        // Three-layer filter (all must pass):
        // 1. |d| <= maxMetricDist  — coarse guard: excludes vertices >5 mm from the
        //    masked reference; catches obvious soft-tissue regions far from the crown.
        // 2. Source-side geometric ROI mask (bbox / z-plane / brush zones) computed
        //    from the ALIGNED scan vertex positions. After ICP, the scan is in the
        //    reference coordinate frame, so the ROI coordinates apply correctly.
        //    This catches gingival vertices that are close (<5 mm) to the crown
        //    boundary and would otherwise slip through filter 1.
        // 3. Tooth segmentation mask (if active).
        const std::vector<bool>* toothMask =
            (scanIdx < toothMasks.size() && !toothMasks[scanIdx].empty())
            ? &toothMasks[scanIdx] : nullptr;

        // Build source-side geometric ROI mask (layer 2) when any component is active
        bool hasGeomROI = roi.bbox.active || roi.zPlane.active || !roi.brushZones.empty();
        std::vector<bool> geoMask;
        if (hasGeomROI) {
            double z_occ = computeOcclusalZ(scan->mesh);
            geoMask = computeROIMask(*scan, roi, z_occ);
        }

        std::vector<double> distances;
        std::vector<double> absDistances;

        for (std::size_t i = 0; i < scan->distanceToRef.size(); i++) {
            double d    = scan->distanceToRef[i];
            double absD = std::abs(d);
            if (absD > maxMetricDist) continue;
            if (!geoMask.empty() && i < geoMask.size() && !geoMask[i]) continue;
            if (toothMask && i < toothMask->size() && !(*toothMask)[i]) continue;
            distances.push_back(d);
            absDistances.push_back(absD);
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

        // Pre-build AABB trees for all scans in this scanner group
        // This avoids O(N^2) tree construction - build N trees once, reuse
        std::cout << "\n      Building AABB trees for " << scannerId << " ("
                  << scannerScans.size() << " scans)..." << std::flush;
        std::vector<std::unique_ptr<DistanceField::ReferenceTree>> trees;
        trees.reserve(scannerScans.size());
        for (const auto& scan : scannerScans) {
            trees.push_back(std::make_unique<DistanceField::ReferenceTree>(scan->mesh));
        }
        std::cout << " done\n      Computing pairwise distances..." << std::flush;

        PrecisionReport report;
        report.scannerId = QString::fromStdString(scannerId);
        report.groupId = group.id;
        report.conditionValue = group.conditionValue;

        std::vector<double> pairwiseRMS;
        int totalPairs = static_cast<int>(scannerScans.size() * (scannerScans.size() - 1) / 2);
        int pairsDone = 0;

        // Compute pairwise RMS between all scan pairs using pre-built trees
        for (std::size_t i = 0; i < scannerScans.size(); i++) {
            for (std::size_t j = i + 1; j < scannerScans.size(); j++) {
                const auto& scan1 = scannerScans[i];

                // Use pre-built tree for scan j
                std::vector<double> distances = trees[j]->computePairwiseDistances(scan1->mesh);

                pairsDone++;
                if (pairsDone % 5 == 0 || pairsDone == totalPairs) {
                    std::cout << "." << std::flush;
                }

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
        std::cout << " done (" << pairsDone << " pairs)\n" << std::flush;
    }

    std::cout << "    Precision metrics complete: " << result.precisionReports.size() << " scanner groups\n" << std::flush;
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

std::shared_ptr<ScanData> GroupProcessor::extractROIReference(
    const SurfaceMesh& refMesh,
    const ROIConfig& roi) const
{
    double z_occlusal = computeOcclusalZ(refMesh);

    // Mark which vertices are inside the ROI
    std::vector<bool> inROI(refMesh.number_of_vertices(), false);
    std::size_t idx = 0;
    for (auto v : refMesh.vertices()) {
        const auto& p = refMesh.point(v);
        inROI[idx++] = roi.isInROI(p.x(), p.y(), p.z(), z_occlusal);
    }

    // Build submesh from faces where ALL 3 vertices are inside the ROI.
    // Using all-3 keeps the submesh manifold at boundary edges.
    auto result = std::make_shared<ScanData>();
    std::unordered_map<std::size_t, SurfaceMesh::Vertex_index> vmap;
    vmap.reserve(refMesh.number_of_vertices() / 4);

    for (auto f : refMesh.faces()) {
        auto hh = refMesh.halfedge(f);
        auto v0 = refMesh.source(hh);
        auto v1 = refMesh.target(hh);
        auto v2 = refMesh.target(refMesh.next(hh));

        if (!inROI[v0.idx()] || !inROI[v1.idx()] || !inROI[v2.idx()])
            continue;

        for (auto vx : {v0, v1, v2}) {
            if (!vmap.count(vx.idx()))
                vmap[vx.idx()] = result->mesh.add_vertex(refMesh.point(vx));
        }
        result->mesh.add_face(vmap[v0.idx()], vmap[v1.idx()], vmap[v2.idx()]);
    }

    result->registered = true;
    return result;
}

void GroupProcessor::exportQCData(
    const GroupResult& result,
    const std::vector<std::shared_ptr<ScanData>>& scans,
    const std::vector<DiscoveredFile>& files,
    const QString& outputDir,
    const std::vector<std::vector<bool>>& toothMasks)
{
    std::cout << "    Exporting QC data..." << std::flush;

    QStringList errors = QCExporter::exportGroupQC(
        result, scans, files, outputDir, toothMasks);

    // Export segmented meshes (tooth-only surfaces)
    if (!toothMasks.empty()) {
        std::cout << "\n    Exporting segmented meshes..." << std::flush;
        QStringList segErrors = QCExporter::exportSegmentedMeshes(
            scans, files, toothMasks, outputDir);
        errors.append(segErrors);
        std::cout << " done (" << toothMasks.size() << " meshes)" << std::flush;
    }

    for (const QString& err : errors) {
        std::cout << "\n      Warning: " << err.toStdString() << std::flush;
    }

    std::cout << " done\n" << std::flush;
}

} // namespace DentScanBatch
