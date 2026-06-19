// SPDX-License-Identifier: GPL-2.0-or-later
// SPDX-FileCopyrightText: 2024 Prof. Dr. Karl-Heinz Kunzelmann

#pragma once

#include "../batch/GroupProcessor.h"
#include <QDateTime>
#include <QJsonObject>
#include <QString>
#include <QStringList>
#include <map>
#include <vector>

namespace DentScanBatch {

/**
 * Status of a scan in the QC review process.
 */
enum class QCStatus {
    Pending,   // Not yet reviewed
    Accepted,  // Reviewed and accepted
    Errand     // Flagged as registration failure, needs re-alignment
};

/**
 * QC record for a single scan.
 */
struct QCRecord {
    QString scanId;           // Unique identifier (Scanner_GroupID_rN)
    QString filePath;         // Original file path
    QString groupId;          // Condition group ID
    QCStatus status = QCStatus::Pending;
    QDateTime reviewTime;     // When status was last changed
    QString reviewerNote;     // Optional note from reviewer
    double rmsDistance = 0.0; // Cached RMS for quick filtering

    // After re-registration (if errand was resolved)
    bool resolved = false;
    double newRmsDistance = 0.0;
    QString correctionMethod;  // "landmark", "manual", etc.
};

/**
 * Manages QC status for all scans in a batch run.
 *
 * Persists to:
 *   qc/qc_status.json    - Full status for all scans
 *   qc/errands.json      - List of flagged scans needing review
 *
 * Provides filtering to exclude errands from final CSV output.
 */
class ErrandManager {
public:
    ErrandManager();

    /**
     * Initialize records from batch results.
     * All scans start as Pending.
     * @param results Vector of group results
     */
    void initializeFromResults(const std::vector<GroupResult>& results);

    /**
     * Load QC status from file.
     * @param qcDir Directory containing qc_status.json
     * @return True if successful (or file doesn't exist yet)
     */
    bool load(const QString& qcDir);

    /**
     * Save QC status to file.
     * @param qcDir Directory for qc_status.json
     * @return True if successful
     */
    bool save(const QString& qcDir) const;

    /**
     * Save errands list to separate file for easy review.
     * @param qcDir Directory for errands.json
     * @return True if successful
     */
    bool saveErrandsList(const QString& qcDir) const;

    /**
     * Initialize a record from transform JSON data.
     * Creates a new Pending record with full metadata.
     * @param scanId Unique scan identifier
     * @param filePath Original STL file path
     * @param groupId Group identifier (user-defined, e.g., "SKD_20" or "condition_A")
     * @param rmsDistance RMS distance metric
     */
    void initializeRecord(const QString& scanId, const QString& filePath,
                          const QString& groupId, double rmsDistance);

    /**
     * Set status for a scan.
     * @param scanId Unique scan identifier
     * @param status New status
     * @param note Optional reviewer note
     */
    void setStatus(const QString& scanId, QCStatus status,
                   const QString& note = QString());

    /**
     * Mark an errand as resolved after re-registration.
     * @param scanId Unique scan identifier
     * @param newRms New RMS distance after correction
     * @param method Correction method used
     */
    void markResolved(const QString& scanId, double newRms,
                      const QString& method = "landmark");

    /**
     * Get status for a scan.
     * @param scanId Unique scan identifier
     * @return Status (Pending if not found)
     */
    QCStatus getStatus(const QString& scanId) const;

    /**
     * Get full record for a scan.
     * @param scanId Unique scan identifier
     * @return Record (empty if not found)
     */
    QCRecord getRecord(const QString& scanId) const;

    /**
     * Get all records with a specific status.
     * @param status Filter status
     * @return Vector of matching records
     */
    std::vector<QCRecord> getByStatus(QCStatus status) const;

    /**
     * Get all errand scan IDs.
     * @return List of scan IDs flagged as errands
     */
    QStringList getErrandIds() const;

    /**
     * Get all accepted scan IDs.
     * @return List of scan IDs that passed QC
     */
    QStringList getAcceptedIds() const;

    /**
     * Filter trueness reports to exclude errands.
     * @param reports Original reports from batch processing
     * @return Filtered reports (errands excluded)
     */
    std::vector<BatchMetricReport> filterAccepted(
        const std::vector<BatchMetricReport>& reports) const;

    /**
     * Get count of scans by status.
     */
    int countPending() const;
    int countAccepted() const;
    int countErrands() const;
    int countTotal() const;

    /**
     * Get outlier scan IDs (RMS > mean + threshold*sigma).
     * Useful for auto-highlighting potential issues.
     * @param threshold Number of standard deviations (default 2.0)
     * @return List of scan IDs that are statistical outliers
     */
    QStringList getOutlierIds(double threshold = 2.0) const;

    /**
     * Accept all scans in a group.
     * @param groupId Group identifier
     */
    void acceptGroup(const QString& groupId);

    /**
     * Accept all scans.
     */
    void acceptAll();

    /**
     * Clear all records.
     */
    void clear();

private:
    std::map<QString, QCRecord> m_records;  // scanId -> record

    static QString statusToString(QCStatus status);
    static QCStatus stringToStatus(const QString& str);
    QJsonObject recordToJson(const QCRecord& record) const;
    QCRecord jsonToRecord(const QJsonObject& obj) const;
};

} // namespace DentScanBatch
