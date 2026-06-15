// SPDX-License-Identifier: GPL-2.0-or-later
// SPDX-FileCopyrightText: 2024 Prof. Dr. Karl-Heinz Kunzelmann

#include "CSVWriter.h"
#include <QFile>
#include <QTextStream>
#include <QDir>
#include <QFileInfo>
#include <QSet>
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

void CSVWriter::writeTruenessHeader(QTextStream& out) {
    // Tessellation metrics
    out << "Observation_ID,Scanner_Model,Group_ID,Repetition_ID,"
        << "Triangles,Edge_mm,AspRatio,MaxAspRatio,ATI,DensHighK,DensLowK,"
        // Accuracy metrics
        << "RMS_mm,MAD_mm,H100_mm,H95_mm,Bias_mm,"
        // Completeness metrics
        << "Coverage_pct,Boundary_mm,Holes,Stitch_deg,"
        // Additional info
        << "Vertices_Included,Vertices_Total,File_Path\n";
}

void CSVWriter::writePrecisionHeader(QTextStream& out) {
    out << "Scanner_Model,Group_ID,Precision_MeanRMS_mm,Precision_SD_mm,"
        << "Coefficient_of_Variation,Pairwise_Count\n";
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
    writeTruenessHeader(out);

    // Data rows
    int obsId = 1;
    for (const auto& report : reports) {
        out << obsId++ << ","
            << escapeCSV(QString::fromStdString(report.scannerName)) << ","
            << escapeCSV(report.groupId) << ","
            << report.repetitionId << ","
            // Tessellation metrics
            << report.triangleCount << ","
            << QString::number(report.meanEdgeLength, 'f', 4) << ","
            << QString::number(report.meanAspectRatio, 'f', 3) << ","
            << QString::number(report.maxAspectRatio, 'f', 3) << ","
            << QString::number(report.ati, 'f', 3) << ","
            << QString::number(report.densityHighCurv, 'f', 2) << ","
            << QString::number(report.densityLowCurv, 'f', 2) << ","
            // Accuracy metrics
            << QString::number(report.rmsDistance, 'f', 4) << ","
            << QString::number(report.madDistance, 'f', 4) << ","
            << QString::number(report.hausdorff100, 'f', 4) << ","
            << QString::number(report.hausdorff95, 'f', 4) << ","
            << QString::number(report.signedMean, 'f', 4) << ","
            // Completeness metrics
            << QString::number(report.coverageRate, 'f', 2) << ","
            << QString::number(report.openBoundaryLength, 'f', 2) << ","
            << report.holeCount << ","
            << QString::number(report.maxStitchingAngle, 'f', 1) << ","
            // Additional info
            << report.verticesIncluded << ","
            << report.verticesTotal << ","
            << escapeCSV(report.filePath) << "\n";
    }

    return true;
}

bool CSVWriter::appendTruenessCSV(
    const std::vector<BatchMetricReport>& reports,
    const QString& filePath,
    int startObsId)
{
    bool fileExists = QFile::exists(filePath);

    QFile file(filePath);
    QIODevice::OpenMode mode = QIODevice::WriteOnly | QIODevice::Text;
    if (fileExists) {
        mode |= QIODevice::Append;
    }

    if (!file.open(mode)) {
        return false;
    }

    QTextStream out(&file);

    // Write header only if new file
    if (!fileExists) {
        writeBOM(file);
        writeTruenessHeader(out);
    }

    // Data rows
    int obsId = startObsId;
    for (const auto& report : reports) {
        out << obsId++ << ","
            << escapeCSV(QString::fromStdString(report.scannerName)) << ","
            << escapeCSV(report.groupId) << ","
            << report.repetitionId << ","
            // Tessellation metrics
            << report.triangleCount << ","
            << QString::number(report.meanEdgeLength, 'f', 4) << ","
            << QString::number(report.meanAspectRatio, 'f', 3) << ","
            << QString::number(report.maxAspectRatio, 'f', 3) << ","
            << QString::number(report.ati, 'f', 3) << ","
            << QString::number(report.densityHighCurv, 'f', 2) << ","
            << QString::number(report.densityLowCurv, 'f', 2) << ","
            // Accuracy metrics
            << QString::number(report.rmsDistance, 'f', 4) << ","
            << QString::number(report.madDistance, 'f', 4) << ","
            << QString::number(report.hausdorff100, 'f', 4) << ","
            << QString::number(report.hausdorff95, 'f', 4) << ","
            << QString::number(report.signedMean, 'f', 4) << ","
            // Completeness metrics
            << QString::number(report.coverageRate, 'f', 2) << ","
            << QString::number(report.openBoundaryLength, 'f', 2) << ","
            << report.holeCount << ","
            << QString::number(report.maxStitchingAngle, 'f', 1) << ","
            // Additional info
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
    writePrecisionHeader(out);

    // Data rows
    for (const auto& report : reports) {
        out << escapeCSV(report.scannerId) << ","
            << escapeCSV(report.groupId) << ","
            << QString::number(report.meanRMS, 'f', 4) << ","
            << QString::number(report.sdRMS, 'f', 4) << ","
            << QString::number(report.cv, 'f', 4) << ","
            << report.pairwiseCount << "\n";
    }

    return true;
}

bool CSVWriter::appendPrecisionCSV(
    const std::vector<PrecisionReport>& reports,
    const QString& filePath)
{
    bool fileExists = QFile::exists(filePath);

    QFile file(filePath);
    QIODevice::OpenMode mode = QIODevice::WriteOnly | QIODevice::Text;
    if (fileExists) {
        mode |= QIODevice::Append;
    }

    if (!file.open(mode)) {
        return false;
    }

    QTextStream out(&file);

    // Write header only if new file
    if (!fileExists) {
        writeBOM(file);
        writePrecisionHeader(out);
    }

    // Data rows
    for (const auto& report : reports) {
        out << escapeCSV(report.scannerId) << ","
            << escapeCSV(report.groupId) << ","
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
        QString groupId;
        int count = 0;
        double sumRMS = 0.0;
        double sumRMS2 = 0.0;
        double minRMS = std::numeric_limits<double>::max();
        double maxRMS = std::numeric_limits<double>::lowest();
    };

    std::map<std::pair<std::string, std::string>, Summary> groups;

    for (const auto& report : reports) {
        auto key = std::make_pair(report.scannerName, report.groupId.toStdString());
        auto& s = groups[key];
        s.scanner = QString::fromStdString(report.scannerName);
        s.groupId = report.groupId;
        s.count++;
        s.sumRMS += report.rmsDistance;
        s.sumRMS2 += report.rmsDistance * report.rmsDistance;
        s.minRMS = std::min(s.minRMS, report.rmsDistance);
        s.maxRMS = std::max(s.maxRMS, report.rmsDistance);
    }

    // Header
    out << "Scanner_Model,Group_ID,N,Mean_RMS_mm,SD_RMS_mm,Min_RMS_mm,Max_RMS_mm\n";

    // Data rows
    for (const auto& [key, s] : groups) {
        double mean = s.sumRMS / s.count;
        double sd = 0.0;
        if (s.count > 1) {
            double variance = (s.sumRMS2 - s.sumRMS * s.sumRMS / s.count) / (s.count - 1);
            sd = std::sqrt(std::max(0.0, variance));
        }

        out << escapeCSV(s.scanner) << ","
            << escapeCSV(s.groupId) << ","
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

QStringList CSVWriter::appendGroupResult(
    const GroupResult& result,
    const QString& outputDir,
    const QString& metricsFilename,
    const QString& precisionFilename,
    int& currentObsId)
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

    // Append trueness metrics
    QString metricsPath = dir.filePath(metricsFilename);
    if (!appendTruenessCSV(result.truenessReports, metricsPath, currentObsId)) {
        errors.append(QString("Failed to append to: %1").arg(metricsPath));
    }
    currentObsId += static_cast<int>(result.truenessReports.size());

    // Append precision metrics
    QString precisionPath = dir.filePath(precisionFilename);
    if (!appendPrecisionCSV(result.precisionReports, precisionPath)) {
        errors.append(QString("Failed to append to: %1").arg(precisionPath));
    }

    return errors;
}

QString CSVWriter::makeScanId(const BatchMetricReport& report)
{
    // Generate consistent scan ID matching QCExporter::makeScanFilename
    QString safeName = QString::fromStdString(report.scannerName);
    safeName.replace(QLatin1Char(' '), QLatin1Char('_'));
    safeName.replace(QLatin1Char('/'), QLatin1Char('_'));
    safeName.replace(QLatin1Char('\\'), QLatin1Char('_'));
    safeName.replace(QLatin1Char(':'), QLatin1Char('_'));

    return QString("%1_%2_r%3").arg(safeName, report.groupId).arg(report.repetitionId);
}

QStringList CSVWriter::writeTruenessWithQCFilter(
    const std::vector<BatchMetricReport>& reports,
    const QString& outputDir,
    const QStringList& errandScanIds,
    const QString& allFilename,
    const QString& filteredFilename)
{
    QStringList errors;

    QDir dir(outputDir);
    if (!dir.exists()) {
        if (!dir.mkpath(".")) {
            errors.append(QString("Cannot create output directory: %1").arg(outputDir));
            return errors;
        }
    }

    // Convert errand list to set for O(1) lookup
    QSet<QString> errandSet(errandScanIds.begin(), errandScanIds.end());

    // Write complete CSV (all reports)
    QString allPath = dir.filePath(allFilename);
    if (!writeTruenessCSV(reports, allPath)) {
        errors.append(QString("Failed to write: %1").arg(allPath));
    }

    // Filter out errands and write filtered CSV
    std::vector<BatchMetricReport> filtered;
    filtered.reserve(reports.size());

    for (const auto& report : reports) {
        QString scanId = makeScanId(report);
        if (!errandSet.contains(scanId)) {
            filtered.push_back(report);
        }
    }

    QString filteredPath = dir.filePath(filteredFilename);
    if (!writeTruenessCSV(filtered, filteredPath)) {
        errors.append(QString("Failed to write: %1").arg(filteredPath));
    }

    return errors;
}

} // namespace DentScanBatch
