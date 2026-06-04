#include "ErrandManager.h"
#include "QCExporter.h"

#include <QDir>
#include <QFile>
#include <QJsonDocument>
#include <QJsonArray>
#include <algorithm>
#include <cmath>
#include <numeric>

namespace DentScanBatch {

ErrandManager::ErrandManager() = default;

void ErrandManager::initializeFromResults(const std::vector<GroupResult>& results)
{
    m_records.clear();

    for (const auto& group : results) {
        for (const auto& report : group.truenessReports) {
            QCRecord record;
            record.scanId = QCExporter::makeScanFilename(
                QString::fromStdString(report.scannerName), report.groupId, report.repetitionId);
            record.filePath = report.filePath;
            record.groupId = report.groupId;
            record.status = QCStatus::Pending;
            record.rmsDistance = report.rmsDistance;

            m_records[record.scanId] = record;
        }
    }
}

void ErrandManager::initializeRecord(const QString& scanId, const QString& filePath,
                                      const QString& groupId, double rmsDistance)
{
    QCRecord record;
    record.scanId = scanId;
    record.filePath = filePath;
    record.groupId = groupId;
    record.status = QCStatus::Pending;
    record.rmsDistance = rmsDistance;
    m_records[scanId] = record;
}

bool ErrandManager::load(const QString& qcDir)
{
    QString path = qcDir + "/qc/qc_status.json";
    QFile file(path);

    if (!file.exists()) {
        // Not an error - file may not exist yet
        return true;
    }

    if (!file.open(QIODevice::ReadOnly)) {
        return false;
    }

    QJsonDocument doc = QJsonDocument::fromJson(file.readAll());
    if (!doc.isObject()) return false;

    QJsonObject root = doc.object();
    QJsonArray records = root["records"].toArray();

    m_records.clear();
    for (const auto& val : records) {
        QCRecord record = jsonToRecord(val.toObject());
        m_records[record.scanId] = record;
    }

    return true;
}

bool ErrandManager::save(const QString& qcDir) const
{
    QDir dir(qcDir);
    if (!dir.exists("qc")) {
        dir.mkdir("qc");
    }

    QString path = qcDir + "/qc/qc_status.json";
    QFile file(path);
    if (!file.open(QIODevice::WriteOnly)) {
        return false;
    }

    QJsonObject root;
    root["version"] = 1;
    root["generated"] = QDateTime::currentDateTime().toString(Qt::ISODate);

    QJsonArray recordsArray;
    for (const auto& [id, record] : m_records) {
        recordsArray.append(recordToJson(record));
    }
    root["records"] = recordsArray;

    // Summary counts
    QJsonObject summary;
    summary["total"] = countTotal();
    summary["pending"] = countPending();
    summary["accepted"] = countAccepted();
    summary["errands"] = countErrands();
    root["summary"] = summary;

    QJsonDocument doc(root);
    file.write(doc.toJson(QJsonDocument::Indented));
    return true;
}

bool ErrandManager::saveErrandsList(const QString& qcDir) const
{
    QDir dir(qcDir);
    if (!dir.exists("qc")) {
        dir.mkdir("qc");
    }

    QString path = qcDir + "/qc/errands.json";
    QFile file(path);
    if (!file.open(QIODevice::WriteOnly)) {
        return false;
    }

    QJsonObject root;
    root["version"] = 1;
    root["generated"] = QDateTime::currentDateTime().toString(Qt::ISODate);

    QJsonArray errandsArray;
    for (const auto& [id, record] : m_records) {
        if (record.status == QCStatus::Errand) {
            QJsonObject errObj;
            errObj["scan_id"] = record.scanId;
            errObj["file_path"] = record.filePath;
            errObj["group_id"] = record.groupId;
            errObj["rms_mm"] = record.rmsDistance;
            errObj["flagged_time"] = record.reviewTime.toString(Qt::ISODate);
            if (!record.reviewerNote.isEmpty()) {
                errObj["note"] = record.reviewerNote;
            }
            if (record.resolved) {
                errObj["resolved"] = true;
                errObj["new_rms_mm"] = record.newRmsDistance;
                errObj["correction_method"] = record.correctionMethod;
            }
            errandsArray.append(errObj);
        }
    }
    root["errands"] = errandsArray;
    root["count"] = errandsArray.size();

    QJsonDocument doc(root);
    file.write(doc.toJson(QJsonDocument::Indented));
    return true;
}

void ErrandManager::setStatus(const QString& scanId, QCStatus status,
                               const QString& note)
{
    auto it = m_records.find(scanId);
    if (it == m_records.end()) return;

    it->second.status = status;
    it->second.reviewTime = QDateTime::currentDateTime();
    if (!note.isEmpty()) {
        it->second.reviewerNote = note;
    }
}

void ErrandManager::markResolved(const QString& scanId, double newRms,
                                  const QString& method)
{
    auto it = m_records.find(scanId);
    if (it == m_records.end()) return;

    it->second.resolved = true;
    it->second.newRmsDistance = newRms;
    it->second.correctionMethod = method;
    it->second.status = QCStatus::Accepted;
    it->second.reviewTime = QDateTime::currentDateTime();
}

QCStatus ErrandManager::getStatus(const QString& scanId) const
{
    auto it = m_records.find(scanId);
    if (it == m_records.end()) return QCStatus::Pending;
    return it->second.status;
}

QCRecord ErrandManager::getRecord(const QString& scanId) const
{
    auto it = m_records.find(scanId);
    if (it == m_records.end()) return QCRecord{};
    return it->second;
}

std::vector<QCRecord> ErrandManager::getByStatus(QCStatus status) const
{
    std::vector<QCRecord> result;
    for (const auto& [id, record] : m_records) {
        if (record.status == status) {
            result.push_back(record);
        }
    }
    return result;
}

QStringList ErrandManager::getErrandIds() const
{
    QStringList ids;
    for (const auto& [id, record] : m_records) {
        if (record.status == QCStatus::Errand) {
            ids << id;
        }
    }
    return ids;
}

QStringList ErrandManager::getAcceptedIds() const
{
    QStringList ids;
    for (const auto& [id, record] : m_records) {
        if (record.status == QCStatus::Accepted) {
            ids << id;
        }
    }
    return ids;
}

std::vector<BatchMetricReport> ErrandManager::filterAccepted(
    const std::vector<BatchMetricReport>& reports) const
{
    std::vector<BatchMetricReport> filtered;
    filtered.reserve(reports.size());

    for (const auto& report : reports) {
        QString scanId = QCExporter::makeScanFilename(
            QString::fromStdString(report.scannerName), report.groupId, report.repetitionId);

        auto it = m_records.find(scanId);
        if (it == m_records.end()) {
            // Not in records, include by default
            filtered.push_back(report);
        } else if (it->second.status != QCStatus::Errand) {
            // Not an errand, include
            filtered.push_back(report);
        }
        // Errands are excluded
    }

    return filtered;
}

int ErrandManager::countPending() const
{
    return static_cast<int>(std::count_if(m_records.begin(), m_records.end(),
        [](const auto& p) { return p.second.status == QCStatus::Pending; }));
}

int ErrandManager::countAccepted() const
{
    return static_cast<int>(std::count_if(m_records.begin(), m_records.end(),
        [](const auto& p) { return p.second.status == QCStatus::Accepted; }));
}

int ErrandManager::countErrands() const
{
    return static_cast<int>(std::count_if(m_records.begin(), m_records.end(),
        [](const auto& p) { return p.second.status == QCStatus::Errand; }));
}

int ErrandManager::countTotal() const
{
    return static_cast<int>(m_records.size());
}

QStringList ErrandManager::getOutlierIds(double threshold) const
{
    if (m_records.empty()) return {};

    // Compute mean and stddev of RMS values
    std::vector<double> rmsValues;
    rmsValues.reserve(m_records.size());
    for (const auto& [id, record] : m_records) {
        rmsValues.push_back(record.rmsDistance);
    }

    double sum = std::accumulate(rmsValues.begin(), rmsValues.end(), 0.0);
    double mean = sum / rmsValues.size();

    double sqSum = 0.0;
    for (double v : rmsValues) {
        sqSum += (v - mean) * (v - mean);
    }
    double stddev = std::sqrt(sqSum / rmsValues.size());

    double cutoff = mean + threshold * stddev;

    QStringList outliers;
    for (const auto& [id, record] : m_records) {
        if (record.rmsDistance > cutoff) {
            outliers << id;
        }
    }
    return outliers;
}

void ErrandManager::acceptGroup(const QString& groupId)
{
    for (auto& [id, record] : m_records) {
        if (record.groupId == groupId && record.status == QCStatus::Pending) {
            record.status = QCStatus::Accepted;
            record.reviewTime = QDateTime::currentDateTime();
        }
    }
}

void ErrandManager::acceptAll()
{
    for (auto& [id, record] : m_records) {
        if (record.status == QCStatus::Pending) {
            record.status = QCStatus::Accepted;
            record.reviewTime = QDateTime::currentDateTime();
        }
    }
}

void ErrandManager::clear()
{
    m_records.clear();
}

QString ErrandManager::statusToString(QCStatus status)
{
    switch (status) {
        case QCStatus::Pending:  return "pending";
        case QCStatus::Accepted: return "accepted";
        case QCStatus::Errand:   return "errand";
    }
    return "pending";
}

QCStatus ErrandManager::stringToStatus(const QString& str)
{
    if (str == "accepted") return QCStatus::Accepted;
    if (str == "errand")   return QCStatus::Errand;
    return QCStatus::Pending;
}

QJsonObject ErrandManager::recordToJson(const QCRecord& record) const
{
    QJsonObject obj;
    obj["scan_id"] = record.scanId;
    obj["file_path"] = record.filePath;
    obj["group_id"] = record.groupId;
    obj["status"] = statusToString(record.status);
    obj["rms_mm"] = record.rmsDistance;

    if (record.reviewTime.isValid()) {
        obj["review_time"] = record.reviewTime.toString(Qt::ISODate);
    }
    if (!record.reviewerNote.isEmpty()) {
        obj["note"] = record.reviewerNote;
    }
    if (record.resolved) {
        obj["resolved"] = true;
        obj["new_rms_mm"] = record.newRmsDistance;
        obj["correction_method"] = record.correctionMethod;
    }

    return obj;
}

QCRecord ErrandManager::jsonToRecord(const QJsonObject& obj) const
{
    QCRecord record;
    record.scanId = obj["scan_id"].toString();
    record.filePath = obj["file_path"].toString();
    record.groupId = obj["group_id"].toString();
    record.status = stringToStatus(obj["status"].toString());
    record.rmsDistance = obj["rms_mm"].toDouble();

    if (obj.contains("review_time")) {
        record.reviewTime = QDateTime::fromString(
            obj["review_time"].toString(), Qt::ISODate);
    }
    if (obj.contains("note")) {
        record.reviewerNote = obj["note"].toString();
    }
    if (obj.contains("resolved")) {
        record.resolved = obj["resolved"].toBool();
        record.newRmsDistance = obj["new_rms_mm"].toDouble();
        record.correctionMethod = obj["correction_method"].toString();
    }

    return record;
}

} // namespace DentScanBatch
