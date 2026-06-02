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
     * Write precision metrics to CSV.
     * One row per scanner×SKD combination.
     * @param reports Precision reports from all groups
     * @param filePath Output file path
     * @return True if successful
     */
    static bool writePrecisionCSV(
        const std::vector<PrecisionReport>& reports,
        const QString& filePath);

    /**
     * Write summary statistics grouped by scanner and SKD.
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
        const QString& summaryFilename = "summary_by_scanner_skd.csv");

private:
    // Write UTF-8 BOM for Windows Excel compatibility
    static void writeBOM(QFile& file);

    // Escape a string for CSV (quote if contains comma, quote, or newline)
    static QString escapeCSV(const QString& str);
};

} // namespace DentScanBatch

#endif // CSVWRITER_H
