#ifndef BATCHRUNNER_H
#define BATCHRUNNER_H

#include "GroupProcessor.h"
#include "CSVWriter.h"
#include "../config/StudyConfig.h"
#include "../config/FileDiscovery.h"
#include <QObject>
#include <QString>
#include <QStringList>
#include <vector>
#include <atomic>

namespace DentScanBatch {

/**
 * Main batch processing runner.
 * Orchestrates processing of all SKD groups and writes output files.
 */
class BatchRunner : public QObject {
    Q_OBJECT

public:
    explicit BatchRunner(QObject* parent = nullptr);

    /**
     * Run batch processing on a study configuration.
     * @param config Study configuration
     * @param dataRoot Root directory for scan data
     * @param outputDir Output directory for results
     * @return True if processing completed successfully
     */
    bool run(const StudyConfig& config,
             const QString& dataRoot,
             const QString& outputDir);

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
    std::atomic<bool> m_cancelled{false};
    bool m_verbose = false;
    QStringList m_warnings;
    QStringList m_errors;
    std::vector<GroupResult> m_results;
};

} // namespace DentScanBatch

#endif // BATCHRUNNER_H
