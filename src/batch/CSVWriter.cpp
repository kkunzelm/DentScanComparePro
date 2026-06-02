#include "CSVWriter.h"
#include <QFile>
#include <QTextStream>
#include <QDir>
#include <QFileInfo>
#include <cmath>
#include <map>

namespace DentScanBatch {

void CSVWriter::writeBOM(QFile& file) {
    // UTF-8 BOM for Windows Excel
    file.write("\xEF\xBB\xBF");
}

QString CSVWriter::escapeCSV(const QString& str) {
    if (str.contains(',') || str.contains('"') || str.contains('\n')) {
        QString escaped = str;
        escaped.replace("\"", "\"\"");
        return "\"" + escaped + "\"";
    }
    return str;
}

bool CSVWriter::writeTruenessCSV(
    const std::vector<BatchMetricReport>& reports,
    const QString& filePath)
{
    QFile file(filePath);
    if (!file.open(QIODevice::WriteOnly | QIODevice::Text)) {
        return false;
    }

    writeBOM(file);
    QTextStream out(&file);

    // Header
    out << "Observation_ID,Scanner_Model,SKD_Value,Repetition_ID,"
        << "Trueness_RMS_mm,Trueness_MeanAbs_mm,Trueness_Max_mm,Trueness_P95_mm,"
        << "Signed_Mean_mm,Coverage_Rate_pct,Vertices_Included,Vertices_Total,File_Path\n";

    // Data rows
    int obsId = 1;
    for (const auto& report : reports) {
        out << obsId++ << ","
            << escapeCSV(QString::fromStdString(report.scannerName)) << ","
            << report.skd_mm << ","
            << report.repetitionId << ","
            << QString::number(report.rmsDistance, 'f', 4) << ","
            << QString::number(report.madDistance, 'f', 4) << ","
            << QString::number(report.hausdorff100, 'f', 4) << ","
            << QString::number(report.hausdorff95, 'f', 4) << ","
            << QString::number(report.signedMean, 'f', 4) << ","
            << QString::number(report.coverageRate, 'f', 2) << ","
            << report.verticesIncluded << ","
            << report.verticesTotal << ","
            << escapeCSV(report.filePath) << "\n";
    }

    return true;
}

bool CSVWriter::writePrecisionCSV(
    const std::vector<PrecisionReport>& reports,
    const QString& filePath)
{
    QFile file(filePath);
    if (!file.open(QIODevice::WriteOnly | QIODevice::Text)) {
        return false;
    }

    writeBOM(file);
    QTextStream out(&file);

    // Header
    out << "Scanner_Model,SKD_Value,Precision_MeanRMS_mm,Precision_SD_mm,"
        << "Coefficient_of_Variation,Pairwise_Count\n";

    // Data rows
    for (const auto& report : reports) {
        out << escapeCSV(report.scannerId) << ","
            << report.skd_mm << ","
            << QString::number(report.meanRMS, 'f', 4) << ","
            << QString::number(report.sdRMS, 'f', 4) << ","
            << QString::number(report.cv, 'f', 4) << ","
            << report.pairwiseCount << "\n";
    }

    return true;
}

bool CSVWriter::writeSummaryCSV(
    const std::vector<BatchMetricReport>& reports,
    const QString& filePath)
{
    QFile file(filePath);
    if (!file.open(QIODevice::WriteOnly | QIODevice::Text)) {
        return false;
    }

    writeBOM(file);
    QTextStream out(&file);

    // Group reports by Scanner×SKD
    struct Summary {
        QString scanner;
        int skd = 0;
        int count = 0;
        double sumRMS = 0.0;
        double sumRMS2 = 0.0;
        double minRMS = std::numeric_limits<double>::max();
        double maxRMS = std::numeric_limits<double>::lowest();
    };

    std::map<std::pair<std::string, int>, Summary> groups;

    for (const auto& report : reports) {
        auto key = std::make_pair(report.scannerName, report.skd_mm);
        auto& s = groups[key];
        s.scanner = QString::fromStdString(report.scannerName);
        s.skd = report.skd_mm;
        s.count++;
        s.sumRMS += report.rmsDistance;
        s.sumRMS2 += report.rmsDistance * report.rmsDistance;
        s.minRMS = std::min(s.minRMS, report.rmsDistance);
        s.maxRMS = std::max(s.maxRMS, report.rmsDistance);
    }

    // Header
    out << "Scanner_Model,SKD_Value,N,Mean_RMS_mm,SD_RMS_mm,Min_RMS_mm,Max_RMS_mm\n";

    // Data rows
    for (const auto& [key, s] : groups) {
        double mean = s.sumRMS / s.count;
        double sd = 0.0;
        if (s.count > 1) {
            double variance = (s.sumRMS2 - s.sumRMS * s.sumRMS / s.count) / (s.count - 1);
            sd = std::sqrt(std::max(0.0, variance));
        }

        out << escapeCSV(s.scanner) << ","
            << s.skd << ","
            << s.count << ","
            << QString::number(mean, 'f', 4) << ","
            << QString::number(sd, 'f', 4) << ","
            << QString::number(s.minRMS, 'f', 4) << ","
            << QString::number(s.maxRMS, 'f', 4) << "\n";
    }

    return true;
}

QStringList CSVWriter::writeAll(
    const std::vector<GroupResult>& results,
    const QString& outputDir,
    const QString& metricsFilename,
    const QString& precisionFilename,
    const QString& summaryFilename)
{
    QStringList errors;

    // Create output directory if needed
    QDir dir(outputDir);
    if (!dir.exists()) {
        if (!dir.mkpath(".")) {
            errors.append(QString("Cannot create output directory: %1").arg(outputDir));
            return errors;
        }
    }

    // Collect all reports
    std::vector<BatchMetricReport> allTrueness;
    std::vector<PrecisionReport> allPrecision;

    for (const auto& result : results) {
        allTrueness.insert(allTrueness.end(),
            result.truenessReports.begin(), result.truenessReports.end());
        allPrecision.insert(allPrecision.end(),
            result.precisionReports.begin(), result.precisionReports.end());
    }

    // Write metrics CSV
    QString metricsPath = dir.filePath(metricsFilename);
    if (!writeTruenessCSV(allTrueness, metricsPath)) {
        errors.append(QString("Failed to write: %1").arg(metricsPath));
    }

    // Write precision CSV
    QString precisionPath = dir.filePath(precisionFilename);
    if (!writePrecisionCSV(allPrecision, precisionPath)) {
        errors.append(QString("Failed to write: %1").arg(precisionPath));
    }

    // Write summary CSV
    QString summaryPath = dir.filePath(summaryFilename);
    if (!writeSummaryCSV(allTrueness, summaryPath)) {
        errors.append(QString("Failed to write: %1").arg(summaryPath));
    }

    return errors;
}

} // namespace DentScanBatch
