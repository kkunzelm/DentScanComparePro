// SPDX-License-Identifier: GPL-2.0-or-later
// SPDX-FileCopyrightText: 2024 Prof. Dr. Karl-Heinz Kunzelmann

#ifndef CSVWRITER_H
#define CSVWRITER_H

#include "GroupProcessor.h"
#include <QFile>
#include <QString>
#include <QStringList>
#include <vector>

namespace DentScanBatch {

/**
 * CSV file writer with UTF-8 BOM support for Windows compatibility.
 * Outputs metrics in long format suitable for statistical analysis.
 * Supports both full write and incremental append modes.
 */
class CSVWriter {
public:
    /**
     * Write trueness metrics to CSV in long format.
     * One row per scan.
     * @param reports Trueness reports from all groups
     * @param filePath Output file path
     * @return True if successful
     */
    static bool writeTruenessCSV(
        const std::vector<BatchMetricReport>& reports,
        const QString& filePath);

    /**
     * Append trueness metrics to an existing CSV file (or create if doesn't exist).
     * @param reports Trueness reports to append
     * @param filePath Output file path
     * @param startObsId Starting observation ID for this batch
     * @return True if successful
     */
    static bool appendTruenessCSV(
        const std::vector<BatchMetricReport>& reports,
        const QString& filePath,
        int startObsId = 1);

    /**
     * Write precision metrics to CSV.
     * One row per scanner×group combination.
     * @param reports Precision reports from all groups
     * @param filePath Output file path
     * @return True if successful
     */
    static bool writePrecisionCSV(
        const std::vector<PrecisionReport>& reports,
        const QString& filePath);

    /**
     * Append precision metrics to an existing CSV file (or create if doesn't exist).
     * @param reports Precision reports to append
     * @param filePath Output file path
     * @return True if successful
     */
    static bool appendPrecisionCSV(
        const std::vector<PrecisionReport>& reports,
        const QString& filePath);

    /**
     * Write summary statistics grouped by scanner and condition group.
     * @param reports Trueness reports from all groups
     * @param filePath Output file path
     * @return True if successful
     */
    static bool writeSummaryCSV(
        const std::vector<BatchMetricReport>& reports,
        const QString& filePath);

    /**
     * Write all output files to a directory.
     * @param results Results from all group processing
     * @param outputDir Output directory
     * @param metricsFilename Name for metrics CSV
     * @param precisionFilename Name for precision CSV
     * @param summaryFilename Name for summary CSV
     * @return List of errors (empty if successful)
     */
    static QStringList writeAll(
        const std::vector<GroupResult>& results,
        const QString& outputDir,
        const QString& metricsFilename = "long_format_metrics.csv",
        const QString& precisionFilename = "precision_matrix.csv",
        const QString& summaryFilename = "summary_by_scanner_group.csv");

    /**
     * Append a single group's results to existing CSV files.
     * Creates files with headers if they don't exist.
     * @param result Single group result to append
     * @param outputDir Output directory
     * @param metricsFilename Name for metrics CSV
     * @param precisionFilename Name for precision CSV
     * @param currentObsId Current observation ID counter (updated on return)
     * @return List of errors (empty if successful)
     */
    static QStringList appendGroupResult(
        const GroupResult& result,
        const QString& outputDir,
        const QString& metricsFilename,
        const QString& precisionFilename,
        int& currentObsId);

    /**
     * Write trueness CSVs with QC filtering.
     * Writes both trueness_metrics_all.csv (complete) and trueness_metrics.csv (errands excluded).
     * @param reports All trueness reports
     * @param outputDir Output directory
     * @param errandScanIds Set of scan IDs to exclude from filtered CSV
     * @param allFilename Name for complete CSV (default: trueness_metrics_all.csv)
     * @param filteredFilename Name for filtered CSV (default: trueness_metrics.csv)
     * @return List of errors (empty if successful)
     */
    static QStringList writeTruenessWithQCFilter(
        const std::vector<BatchMetricReport>& reports,
        const QString& outputDir,
        const QStringList& errandScanIds,
        const QString& allFilename = "trueness_metrics_all.csv",
        const QString& filteredFilename = "trueness_metrics.csv");

    /**
     * Generate scan ID from a report (for QC matching).
     * @param report Metric report
     * @return Scan ID in format "Scanner_GroupID_rN"
     */
    static QString makeScanId(const BatchMetricReport& report);

private:
    // Write UTF-8 BOM for Windows Excel compatibility
    static void writeBOM(QFile& file);

    // Escape a string for CSV (quote if contains comma, quote, or newline)
    static QString escapeCSV(const QString& str);

    // Write trueness CSV header
    static void writeTruenessHeader(QTextStream& out);

    // Write precision CSV header
    static void writePrecisionHeader(QTextStream& out);
};

} // namespace DentScanBatch

#endif // CSVWRITER_H
