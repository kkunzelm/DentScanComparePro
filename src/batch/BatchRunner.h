// SPDX-License-Identifier: GPL-2.0-or-later
// SPDX-FileCopyrightText: 2024 Prof. Dr. Karl-Heinz Kunzelmann

#ifndef BATCHRUNNER_H
#define BATCHRUNNER_H

#include "GroupProcessor.h"
#include "CSVWriter.h"
#include "../config/StudyConfig.h"
#include "../config/FileDiscovery.h"
#include <QObject>
#include <QString>
#include <QStringList>
#include <QSet>
#include <vector>
#include <atomic>
#include <optional>

namespace DentScanBatch {

/**
 * Main batch processing runner.
 * Orchestrates processing of all condition groups and writes output files.
 * Supports incremental saving and resume capability.
 */
class BatchRunner : public QObject {
    Q_OBJECT

public:
    explicit BatchRunner(QObject* parent = nullptr);

    /**
     * Run batch processing on a study configuration.
     * Automatically resumes from previous progress if found.
     * @param config Study configuration
     * @param dataRoot Root directory for scan data
     * @param outputDir Output directory for results
     * @param roiTemplate Optional ROI template with tooth segmentation settings
     * @param forceFullMesh If true, bypass all ROI/masked ICP and use full mesh
     * @return True if processing completed successfully
     */
    bool run(const StudyConfig& config,
             const QString& dataRoot,
             const QString& outputDir,
             const std::optional<ROITemplate>& roiTemplate = std::nullopt,
             bool forceFullMesh = false);

    /**
     * Check if a previous run can be resumed.
     * @param outputDir Output directory to check
     * @param studyName Expected study name
     * @return Set of group IDs that were already completed
     */
    static QSet<QString> getCompletedGroups(const QString& outputDir, const QString& studyName);

    /**
     * Cancel running batch processing.
     */
    void cancel();

    /**
     * Check if processing was cancelled.
     */
    bool wasCancelled() const { return m_cancelled.load(); }

    /**
     * Set verbose mode for detailed output.
     */
    void setVerbose(bool verbose) { m_verbose = verbose; }

    /**
     * Get warnings from the last run.
     */
    QStringList warnings() const { return m_warnings; }

    /**
     * Get errors from the last run.
     */
    QStringList errors() const { return m_errors; }

    /**
     * Get all results from the last run.
     */
    const std::vector<GroupResult>& results() const { return m_results; }

signals:
    /**
     * Emitted when overall progress updates.
     * @param current Current group number (1-based)
     * @param total Total number of groups
     * @param status Human-readable status message
     */
    void progressUpdated(int current, int total, const QString& status);

    /**
     * Emitted when a group processing completes.
     * @param groupId Group ID
     * @param success True if processing succeeded
     * @param message Status message
     */
    void groupCompleted(const QString& groupId, bool success, const QString& message);

    /**
     * Emitted when batch processing completes.
     * @param success True if all groups processed successfully
     */
    void batchCompleted(bool success);

    /**
     * Emitted for log messages.
     * @param message Log message
     */
    void logMessage(const QString& message);

private:
    /**
     * Save progress to a JSON file in the output directory.
     */
    bool saveProgress(const QString& outputDir, const QString& studyName,
                      const QSet<QString>& completedGroups, int currentObsId);

    /**
     * Load progress from a JSON file in the output directory.
     * @return Current observation ID counter (0 if no progress file)
     */
    int loadProgress(const QString& outputDir, const QString& studyName,
                     QSet<QString>& completedGroups);

    /**
     * Get the progress file path.
     */
    static QString progressFilePath(const QString& outputDir);

    std::atomic<bool> m_cancelled{false};
    bool m_verbose = false;
    QStringList m_warnings;
    QStringList m_errors;
    std::vector<GroupResult> m_results;
};

} // namespace DentScanBatch

#endif // BATCHRUNNER_H
