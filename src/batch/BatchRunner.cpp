#include "BatchRunner.h"
#include "../qc/QCExporter.h"
#include "../core/AlignmentTransformLoader.h"
#include <QDir>
#include <QFile>
#include <QJsonDocument>
#include <QJsonObject>
#include <QJsonArray>
#include <iostream>

namespace DentScanBatch {

BatchRunner::BatchRunner(QObject* parent)
    : QObject(parent)
{
}

void BatchRunner::cancel() {
    m_cancelled.store(true);
}

QString BatchRunner::progressFilePath(const QString& outputDir) {
    return QDir(outputDir).filePath(".batch_progress.json");
}

bool BatchRunner::saveProgress(const QString& outputDir, const QString& studyName,
                               const QSet<QString>& completedGroups, int currentObsId)
{
    QJsonObject root;
    root["study_name"] = studyName;
    root["current_obs_id"] = currentObsId;

    QJsonArray groupsArray;
    for (const QString& groupId : completedGroups) {
        groupsArray.append(groupId);
    }
    root["completed_groups"] = groupsArray;

    QFile file(progressFilePath(outputDir));
    if (!file.open(QIODevice::WriteOnly)) {
        return false;
    }

    file.write(QJsonDocument(root).toJson());
    return true;
}

int BatchRunner::loadProgress(const QString& outputDir, const QString& studyName,
                              QSet<QString>& completedGroups)
{
    completedGroups.clear();

    QFile file(progressFilePath(outputDir));
    if (!file.exists() || !file.open(QIODevice::ReadOnly)) {
        return 1;  // Start from observation ID 1
    }

    QJsonParseError parseError;
    QJsonDocument doc = QJsonDocument::fromJson(file.readAll(), &parseError);
    if (parseError.error != QJsonParseError::NoError) {
        return 1;
    }

    QJsonObject root = doc.object();

    // Verify study name matches
    if (root["study_name"].toString() != studyName) {
        return 1;  // Different study, start fresh
    }

    // Load completed groups
    QJsonArray groupsArray = root["completed_groups"].toArray();
    for (const auto& val : groupsArray) {
        completedGroups.insert(val.toString());
    }

    return root["current_obs_id"].toInt(1);
}

QSet<QString> BatchRunner::getCompletedGroups(const QString& outputDir, const QString& studyName)
{
    QSet<QString> completed;

    QFile file(progressFilePath(outputDir));
    if (!file.exists() || !file.open(QIODevice::ReadOnly)) {
        return completed;
    }

    QJsonParseError parseError;
    QJsonDocument doc = QJsonDocument::fromJson(file.readAll(), &parseError);
    if (parseError.error != QJsonParseError::NoError) {
        return completed;
    }

    QJsonObject root = doc.object();

    // Verify study name matches
    if (root["study_name"].toString() != studyName) {
        return completed;
    }

    QJsonArray groupsArray = root["completed_groups"].toArray();
    for (const auto& val : groupsArray) {
        completed.insert(val.toString());
    }

    return completed;
}

bool BatchRunner::run(
    const StudyConfig& config,
    const QString& dataRoot,
    const QString& outputDir,
    const std::optional<ROITemplate>& roiTemplate,
    bool forceFullMesh)
{
    m_cancelled.store(false);
    m_warnings.clear();
    m_errors.clear();
    m_results.clear();

    // Disable image export in batch mode (VTK offscreen rendering often fails)
    // GPA meshes (STL) and transforms (JSON) will still be exported
    QCExporter::setImageExportEnabled(false);
    if (m_verbose) {
        std::cout << "Note: Difference image export disabled in batch mode.\n" << std::flush;
        if (forceFullMesh) {
            std::cout << "Full-mesh mode: ENABLED (ROI/masked ICP bypassed)\n" << std::flush;
        }
    }

    // Load precomputed transforms from DentScanAlign (if directory specified)
    std::map<std::string, Eigen::Matrix4d> precomputedTransforms;
    if (!config.alignmentsDirectory.isEmpty()) {
        if (m_verbose) {
            std::cout << "Loading precomputed transforms from: "
                      << config.alignmentsDirectory.toStdString() << "...\n" << std::flush;
        }
        precomputedTransforms = AlignmentTransformLoader::loadTransforms(config.alignmentsDirectory);
        if (m_verbose && !precomputedTransforms.empty()) {
            std::cout << "  Loaded " << precomputedTransforms.size() << " transforms\n" << std::flush;
        }
    }

    // Check for existing progress (resume support)
    QSet<QString> completedGroups;
    int currentObsId = loadProgress(outputDir, config.name, completedGroups);

    if (!completedGroups.isEmpty() && m_verbose) {
        std::cout << "Resuming from previous run. Already completed: "
                  << completedGroups.size() << " groups.\n" << std::flush;
    }

    // Discover files for all groups
    emit logMessage("Discovering files...");
    if (m_verbose) {
        std::cout << "Discovering files in: " << dataRoot.toStdString() << "..." << std::flush;
    }
    auto groupDiscoveries = FileDiscovery::discoverAllGroups(config, dataRoot);
    if (m_verbose) {
        std::cout << " done.\n" << std::flush;
    }

    if (groupDiscoveries.isEmpty()) {
        m_errors.append("No groups discovered");
        emit batchCompleted(false);
        return false;
    }

    // Process each group
    int totalGroups = config.groups.size();
    int currentGroup = 0;
    int skippedGroups = 0;

    GroupProcessor processor;
    processor.setCancelFlag(&m_cancelled);  // Share cancellation flag with processor
    connect(&processor, &GroupProcessor::progressUpdated,
            this, [this](int current, int total, const QString& status) {
        if (m_verbose) {
            emit logMessage(QString("  [%1/%2] %3").arg(current).arg(total).arg(status));
        }
    });

    for (const auto& group : config.groups) {
        if (m_cancelled.load()) {
            emit logMessage("Processing cancelled by user");
            // Save progress before exiting
            saveProgress(outputDir, config.name, completedGroups, currentObsId);
            emit batchCompleted(false);
            return false;
        }

        currentGroup++;

        // Skip already completed groups (resume support)
        if (completedGroups.contains(group.id)) {
            skippedGroups++;
            if (m_verbose) {
                std::cout << "\n[" << currentGroup << "/" << totalGroups << "] "
                          << "Skipping " << group.id.toStdString() << " (already completed)\n" << std::flush;
            }
            emit groupCompleted(group.id, true, "Already completed (resumed)");
            continue;
        }

        emit progressUpdated(currentGroup, totalGroups,
            QString("Processing %1").arg(group.id));

        if (m_verbose) {
            std::cout << "\n[" << currentGroup << "/" << totalGroups << "] "
                      << "Processing group " << group.id.toStdString() << "...\n" << std::flush;
        }

        // Get discovered files for this group
        auto it = groupDiscoveries.find(group.id);
        if (it == groupDiscoveries.end()) {
            QString msg = QString("No files discovered for group: %1").arg(group.id);
            m_warnings.append(msg);
            emit groupCompleted(group.id, false, msg);
            continue;
        }

        const GroupDiscovery& discovery = it.value();

        if (m_verbose) {
            std::cout << "  Files: " << discovery.totalFiles << std::flush;
            if (discovery.filesByScanner.size() > 0) {
                std::cout << " (";
                bool first = true;
                for (auto scanIt = discovery.filesByScanner.begin();
                     scanIt != discovery.filesByScanner.end(); ++scanIt) {
                    if (!first) std::cout << ", ";
                    std::cout << scanIt.key().toStdString() << ": " << scanIt.value().size();
                    first = false;
                }
                std::cout << ")";
            }
            std::cout << "\n" << std::flush;
        }

        if (discovery.totalFiles == 0) {
            QString msg = QString("No files found for group: %1").arg(group.id);
            m_warnings.append(msg);
            emit groupCompleted(group.id, false, msg);
            continue;
        }

        // Convert discovery to file list (only include known scanners)
        std::vector<DiscoveredFile> files;
        for (auto scanIt = discovery.filesByScanner.begin();
             scanIt != discovery.filesByScanner.end(); ++scanIt)
        {
            QString scannerId = scanIt.key();
            // Skip files that don't match any known scanner
            if (scannerId == "Unknown") {
                if (m_verbose) {
                    std::cout << "  Skipping " << scanIt.value().size()
                              << " files with unknown scanner\n" << std::flush;
                }
                continue;
            }
            for (const QString& path : scanIt.value()) {
                DiscoveredFile file;
                file.path = path;
                file.scannerId = scannerId;
                file.groupId = group.id;
                file.repetitionId = FileDiscovery::extractRepetitionId(path);
                files.push_back(file);
            }
        }

        // Process the group (with QC export to outputDir)
        GroupResult result = processor.process(
            group, files, config.alignment, roiTemplate, outputDir,
            config.externalReferencePath, config.scansPreAligned,
            precomputedTransforms, forceFullMesh);

        // Collect warnings and errors
        m_warnings.append(result.warnings);
        m_errors.append(result.errors);

        if (result.success) {
            emit groupCompleted(group.id, true,
                QString("%1 scans processed").arg(result.truenessReports.size()));

            if (m_verbose) {
                std::cout << "  Trueness reports: " << result.truenessReports.size() << "\n";
                std::cout << "  Precision reports: " << result.precisionReports.size() << "\n";
            }

            // === INCREMENTAL SAVE ===
            // Save this group's results immediately
            QStringList appendErrors = CSVWriter::appendGroupResult(
                result, outputDir,
                config.output.metricsCSV,
                config.output.precisionCSV,
                currentObsId);

            if (!appendErrors.isEmpty()) {
                m_warnings.append(appendErrors);
                if (m_verbose) {
                    std::cout << "  Warning: Error saving incremental results\n";
                }
            } else {
                // Mark group as completed and save progress
                completedGroups.insert(group.id);
                saveProgress(outputDir, config.name, completedGroups, currentObsId);

                if (m_verbose) {
                    std::cout << "  Results saved incrementally.\n" << std::flush;
                }
            }
        } else {
            emit groupCompleted(group.id, false,
                result.errors.isEmpty() ? "Processing failed" : result.errors.first());
        }

        m_results.push_back(result);
    }

    // Generate summary CSV (needs all trueness data)
    emit logMessage("Generating summary statistics...");
    if (m_verbose) {
        std::cout << "\nGenerating summary statistics...\n" << std::flush;
    }

    // Collect all trueness reports for summary
    std::vector<BatchMetricReport> allTrueness;
    for (const auto& result : m_results) {
        allTrueness.insert(allTrueness.end(),
            result.truenessReports.begin(), result.truenessReports.end());
    }

    // Also need to read previously saved trueness data if resuming
    // For now, just write summary from current session's data
    // (Full implementation would re-read the incremental CSV)

    QString summaryPath = QDir(outputDir).filePath(config.output.summaryCSV);
    if (!CSVWriter::writeSummaryCSV(allTrueness, summaryPath)) {
        m_errors.append(QString("Failed to write summary: %1").arg(summaryPath));
    }

    // Remove progress file on successful completion
    if (completedGroups.size() == static_cast<std::size_t>(totalGroups)) {
        QFile::remove(progressFilePath(outputDir));
    }

    bool success = m_errors.isEmpty();

    if (m_verbose) {
        if (success) {
            std::cout << "\nBatch processing completed successfully.\n";
            if (skippedGroups > 0) {
                std::cout << "  (" << skippedGroups << " groups resumed from previous run)\n";
            }
            std::cout << "Output files:\n";
            std::cout << "  " << QDir(outputDir).filePath(config.output.metricsCSV).toStdString() << "\n";
            std::cout << "  " << QDir(outputDir).filePath(config.output.precisionCSV).toStdString() << "\n";
            std::cout << "  " << QDir(outputDir).filePath(config.output.summaryCSV).toStdString() << "\n";
        } else {
            std::cout << "\nBatch processing completed with errors:\n";
            for (const QString& err : m_errors) {
                std::cout << "  - " << err.toStdString() << "\n";
            }
        }

        if (!m_warnings.isEmpty()) {
            std::cout << "\nWarnings:\n";
            for (const QString& warn : m_warnings) {
                std::cout << "  - " << warn.toStdString() << "\n";
            }
        }
    }

    emit batchCompleted(success);
    return success;
}

} // namespace DentScanBatch
