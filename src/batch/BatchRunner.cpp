#include "BatchRunner.h"
#include <QDir>
#include <iostream>

namespace DentScanBatch {

BatchRunner::BatchRunner(QObject* parent)
    : QObject(parent)
{
}

void BatchRunner::cancel() {
    m_cancelled.store(true);
}

bool BatchRunner::run(
    const StudyConfig& config,
    const QString& dataRoot,
    const QString& outputDir)
{
    m_cancelled.store(false);
    m_warnings.clear();
    m_errors.clear();
    m_results.clear();

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

    GroupProcessor processor;
    connect(&processor, &GroupProcessor::progressUpdated,
            this, [this](int current, int total, const QString& status) {
        if (m_verbose) {
            emit logMessage(QString("  [%1/%2] %3").arg(current).arg(total).arg(status));
        }
    });

    for (const auto& group : config.groups) {
        if (m_cancelled.load()) {
            emit logMessage("Processing cancelled by user");
            emit batchCompleted(false);
            return false;
        }

        currentGroup++;
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

        // Process the group
        GroupResult result = processor.process(group, files, config.alignment);

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
        } else {
            emit groupCompleted(group.id, false,
                result.errors.isEmpty() ? "Processing failed" : result.errors.first());
        }

        m_results.push_back(result);
    }

    // Write output files
    emit logMessage("Writing output files...");
    if (m_verbose) {
        std::cout << "\nWriting output files to: " << outputDir.toStdString() << "\n";
    }

    QStringList writeErrors = CSVWriter::writeAll(
        m_results, outputDir,
        config.output.metricsCSV,
        config.output.precisionCSV,
        config.output.summaryCSV);

    m_errors.append(writeErrors);

    bool success = m_errors.isEmpty();

    if (m_verbose) {
        if (success) {
            std::cout << "\nBatch processing completed successfully.\n";
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
