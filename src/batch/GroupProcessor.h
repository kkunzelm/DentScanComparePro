// SPDX-License-Identifier: GPL-2.0-or-later
// SPDX-FileCopyrightText: 2024 Prof. Dr. Karl-Heinz Kunzelmann

#ifndef GROUPPROCESSOR_H
#define GROUPPROCESSOR_H

#include "../config/StudyConfig.h"
#include "../config/FileDiscovery.h"
#include "../config/ROIConfig.h"
#include "../core/Mesh.h"
#include "../core/MetricReport.h"
#include "../core/ToothSegmentation.h"
#include <QObject>
#include <QString>
#include <QStringList>
#include <Eigen/Core>
#include <memory>
#include <vector>
#include <map>
#include <optional>
#include <atomic>
#include <limits>

namespace DentScanBatch {

/**
 * Extended metric report for batch processing.
 * Includes additional identification fields.
 */
struct BatchMetricReport : public MetricReport {
    QString groupId;
    int skd_mm = 0;
    int repetitionId = 0;
    QString filePath;
    std::size_t verticesIncluded = 0;
    std::size_t verticesTotal = 0;
};

/**
 * Precision metrics for a scanner within a group.
 */
struct PrecisionReport {
    QString scannerId;
    QString groupId;
    int skd_mm = 0;
    double meanRMS = 0.0;      // Mean of pairwise RMS values
    double sdRMS = 0.0;        // SD of pairwise RMS values
    double cv = 0.0;           // Coefficient of variation (SD/mean)
    int pairwiseCount = 0;     // Number of pairs compared (typically 10 for 5 reps)
};

/**
 * Result of processing a single SKD group.
 */
struct GroupResult {
    QString groupId;
    int skd_mm = 0;
    std::vector<BatchMetricReport> truenessReports;  // One per scan
    std::vector<PrecisionReport> precisionReports;   // One per scanner
    std::shared_ptr<SurfaceMesh> gpaMean;            // GPA mean reference mesh
    QStringList warnings;
    QStringList errors;
    bool success = false;
};

/**
 * Processor for a single SKD group.
 * Handles the complete processing pipeline:
 * 1. Load STL files
 * 2. Compute curvature (for occlusal plane detection)
 * 3. Run GPA alignment
 * 4. Apply ROI mask
 * 5. Compute trueness metrics (vs GPA mean)
 * 6. Compute precision metrics (pairwise within scanner)
 */
class GroupProcessor : public QObject {
    Q_OBJECT

public:
    explicit GroupProcessor(QObject* parent = nullptr);

    /**
     * Process a single SKD group.
     * @param group Group configuration
     * @param files List of files to process (from FileDiscovery)
     * @param alignment Alignment parameters
     * @param roiTemplate Optional ROI template with tooth segmentation
     * @param outputDir Optional output directory for QC export (empty = no export)
     * @param externalRefPath Optional path to external reference STL (empty = compute GPA)
     * @param scansPreAligned If true, skip alignment (scans already aligned by DentScanAlign)
     * @param precomputedTransforms Optional map of normalized filename -> transform from DentScanAlign
     * @param forceFullMesh If true, bypass all ROI/masked ICP and use full mesh
     * @return Processing result
     */
    GroupResult process(
        const GroupConfig& group,
        const std::vector<DiscoveredFile>& files,
        const AlignmentConfig& alignment,
        const std::optional<ROITemplate>& roiTemplate = std::nullopt,
        const QString& outputDir = QString(),
        const QString& externalRefPath = QString(),
        bool scansPreAligned = false,
        const std::map<std::string, Eigen::Matrix4d>& precomputedTransforms = {},
        bool forceFullMesh = false,
        bool computePrecision = true,
        bool scansNormalized = false);

    /**
     * Set external cancellation flag to check during processing.
     * @param cancelFlag Pointer to atomic bool that can be set externally
     */
    void setCancelFlag(std::atomic<bool>* cancelFlag) { m_externalCancelFlag = cancelFlag; }

    /**
     * Cancel processing (if running in async mode).
     */
    void cancel();

    /**
     * Check if processing was cancelled.
     */
    bool wasCancelled() const { return m_cancelled || (m_externalCancelFlag && m_externalCancelFlag->load()); }

signals:
    /**
     * Emitted when progress updates.
     * @param current Current step number
     * @param total Total number of steps
     * @param status Human-readable status message
     */
    void progressUpdated(int current, int total, const QString& status);

    /**
     * Emitted when a scan is processed.
     * @param filename Name of the processed file
     * @param success True if processing succeeded
     */
    void scanProcessed(const QString& filename, bool success);

private:
    // Processing stages
    bool loadScans(const std::vector<DiscoveredFile>& files,
                   std::vector<std::shared_ptr<ScanData>>& scans,
                   GroupResult& result);

    bool computeCurvature(std::vector<std::shared_ptr<ScanData>>& scans,
                          GroupResult& result);

    bool runGPAAlignment(std::vector<std::shared_ptr<ScanData>>& scans,
                         const AlignmentConfig& alignment,
                         std::shared_ptr<SurfaceMesh>& gpaMean,
                         GroupResult& result,
                         bool scansNormalized = false,
                         const std::vector<std::vector<bool>>& icpMasks = {});

    bool computeDistances(std::vector<std::shared_ptr<ScanData>>& scans,
                          const std::shared_ptr<SurfaceMesh>& gpaMean,
                          GroupResult& result);

    // maxMetricDist: source vertices with distance > this value to the
    // (already ROI-masked) reference are excluded from metrics.
    void computeTruenessMetrics(const std::vector<std::shared_ptr<ScanData>>& scans,
                                const std::vector<DiscoveredFile>& files,
                                const ROIConfig& roi,
                                const GroupConfig& group,
                                const std::vector<std::vector<bool>>& toothMasks,
                                GroupResult& result,
                                double maxMetricDist = std::numeric_limits<double>::max());

    void computePrecisionMetrics(const std::vector<std::shared_ptr<ScanData>>& scans,
                                 const std::vector<DiscoveredFile>& files,
                                 const ROIConfig& roi,
                                 const GroupConfig& group,
                                 const std::vector<std::vector<bool>>& toothMasks,
                                 GroupResult& result);

    // Helper functions
    double computeOcclusalZ(const SurfaceMesh& mesh) const;
    std::vector<bool> computeROIMask(const ScanData& scan,
                                     const ROIConfig& roi,
                                     double z_occlusal) const;

    // Compute combined ICP mask from all active ROI components
    // Returns mask combining: bbox, plane slab, brush zones, and optionally tooth mask
    std::vector<bool> computeCombinedICPMask(
        const ScanData& scan,
        const ROIConfig& roi,
        const std::vector<bool>& toothMask) const;

    // Check if any ROI component is active (for deciding whether to use masked ICP)
    bool hasActiveROI(const ROIConfig& roi, bool hasToothMask) const;

    // Extract a submesh of refMesh containing only faces fully inside the ROI.
    // Used to create the masked reference for ICP and distance computation so
    // that the ROI only needs to match the canonical reference frame, not each
    // individual source scan.
    std::shared_ptr<ScanData> extractROIReference(
        const SurfaceMesh& refMesh,
        const ROIConfig& roi) const;

    // Compute tooth masks for all scans using template seeds
    std::vector<std::vector<bool>> computeToothMasks(
        const std::vector<std::shared_ptr<ScanData>>& scans,
        const ROITemplate& roiTemplate) const;

    // Export QC data (difference images, transforms, GPA mean)
    void exportQCData(
        const GroupResult& result,
        const std::vector<std::shared_ptr<ScanData>>& scans,
        const std::vector<DiscoveredFile>& files,
        const QString& outputDir,
        const std::vector<std::vector<bool>>& toothMasks);

    bool m_cancelled = false;
    std::atomic<bool>* m_externalCancelFlag = nullptr;
    int m_currentStep = 0;
    int m_totalSteps = 0;
};

} // namespace DentScanBatch

#endif // GROUPPROCESSOR_H
