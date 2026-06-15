// SPDX-License-Identifier: GPL-2.0-or-later
// SPDX-FileCopyrightText: 2024 Prof. Dr. Karl-Heinz Kunzelmann

#include "FileDiscovery.h"
#include <QDir>
#include <QDirIterator>
#include <QFileInfo>
#include <QRegularExpression>

namespace DentScanBatch {

std::vector<DiscoveredFile> FileDiscovery::discoverFiles(
    const StudyConfig& config,
    const QString& dataRoot)
{
    QString root = dataRoot.isEmpty() ? config.dataRootDir : dataRoot;
    std::vector<DiscoveredFile> files;

    for (const auto& group : config.groups) {
        GroupDiscovery discovery = discoverGroup(group, config.scanners, root);

        for (auto it = discovery.filesByScanner.begin();
             it != discovery.filesByScanner.end(); ++it)
        {
            QString scannerId = it.key();
            int repId = 1;

            for (const QString& path : it.value()) {
                DiscoveredFile file;
                file.path = path;
                file.scannerId = scannerId;
                file.groupId = group.id;
                file.repetitionId = extractRepetitionId(path);
                if (file.repetitionId == 0) {
                    file.repetitionId = repId++;  // Fallback to sequential numbering
                }
                files.push_back(file);
            }
        }
    }

    return files;
}

GroupDiscovery FileDiscovery::discoverGroup(
    const GroupConfig& group,
    const std::vector<ScannerDef>& scanners,
    const QString& dataRoot)
{
    GroupDiscovery discovery;
    discovery.groupId = group.id;
    discovery.skd_mm = group.skd_mm;

    // Expand all patterns and collect matching files
    QStringList allFiles;
    for (const QString& pattern : group.filePatterns) {
        QStringList matches = expandGlob(pattern, dataRoot);
        allFiles.append(matches);
    }

    // Remove duplicates
    allFiles.removeDuplicates();

    // Categorize by scanner
    for (const QString& filePath : allFiles) {
        QString scannerId = identifyScanner(filePath, scanners);
        if (scannerId.isEmpty()) {
            discovery.warnings.append(
                QString("Could not identify scanner for: %1").arg(filePath));
            scannerId = "Unknown";
        }
        discovery.filesByScanner[scannerId].append(filePath);
        discovery.totalFiles++;
    }

    return discovery;
}

QMap<QString, GroupDiscovery> FileDiscovery::discoverAllGroups(
    const StudyConfig& config,
    const QString& dataRoot)
{
    QString root = dataRoot.isEmpty() ? config.dataRootDir : dataRoot;
    QMap<QString, GroupDiscovery> results;

    for (const auto& group : config.groups) {
        results[group.id] = discoverGroup(group, config.scanners, root);
    }

    return results;
}

QString FileDiscovery::identifyScanner(
    const QString& filePath,
    const std::vector<ScannerDef>& scanners)
{
    // Check each scanner's patterns against the file path
    for (const auto& scanner : scanners) {
        for (const QString& pattern : scanner.patterns) {
            // Convert glob pattern to regex
            QString regexPattern = QRegularExpression::escape(pattern);
            regexPattern.replace("\\*", ".*");
            regexPattern.replace("\\?", ".");

            QRegularExpression rx(regexPattern, QRegularExpression::CaseInsensitiveOption);
            if (rx.match(filePath).hasMatch()) {
                return scanner.id;
            }
        }
    }

    return QString();
}

int FileDiscovery::extractRepetitionId(const QString& filePath) {
    QFileInfo fi(filePath);
    QString name = fi.completeBaseName();

    // Try various patterns for repetition ID
    static const QVector<QRegularExpression> patterns = {
        QRegularExpression(R"(_[Dd](\d+)_)"),                                                 // _D1_ or _d1_ (Nold naming)
        QRegularExpression(R"(\br(\d+)\b)", QRegularExpression::CaseInsensitiveOption),       // r1, R1
        QRegularExpression(R"(\brep(\d+)\b)", QRegularExpression::CaseInsensitiveOption),     // rep1, Rep1
        QRegularExpression(R"(_r(\d+))", QRegularExpression::CaseInsensitiveOption),          // _r1
        QRegularExpression(R"(_(\d+)$)"),                                                      // _1 at end
        QRegularExpression(R"(\s(\d+)$)"),                                                     // space + digit at end
        QRegularExpression(R"(#(\d+))"),                                                       // #1
        QRegularExpression(R"(\((\d+)\))"),                                                    // (1)
    };

    for (const auto& rx : patterns) {
        QRegularExpressionMatch match = rx.match(name);
        if (match.hasMatch()) {
            bool ok;
            int rep = match.captured(1).toInt(&ok);
            if (ok && rep >= 1 && rep <= 20) {  // Reasonable range
                return rep;
            }
        }
    }

    return 0;
}

QStringList FileDiscovery::expandGlob(const QString& pattern, const QString& rootDir) {
    QStringList results;

    QString startDir = rootDir;
    if (startDir.isEmpty()) {
        startDir = QDir::currentPath();
    }

    // Check if pattern starts with **
    bool recursive = pattern.startsWith("**/") || pattern.startsWith("**\\");

    QString searchPattern = pattern;
    if (recursive) {
        // Remove leading **/
        searchPattern = pattern.mid(3);
    }

    // Build a filter for the final filename pattern
    QString filenamePattern;
    QString dirPattern;

    int lastSlash = searchPattern.lastIndexOf('/');
    if (lastSlash == -1) {
        lastSlash = searchPattern.lastIndexOf('\\');
    }

    if (lastSlash >= 0) {
        dirPattern = searchPattern.left(lastSlash);
        filenamePattern = searchPattern.mid(lastSlash + 1);
    } else {
        filenamePattern = searchPattern;
    }

    // Use QDirIterator for efficient recursive search
    QDirIterator::IteratorFlags flags = recursive ?
        QDirIterator::Subdirectories : QDirIterator::NoIteratorFlags;

    QDirIterator it(startDir, QStringList() << filenamePattern,
                    QDir::Files, flags);

    while (it.hasNext()) {
        QString filePath = it.next();

        // If we have a dir pattern, check if path contains it
        if (!dirPattern.isEmpty()) {
            // Convert dir pattern to regex for matching
            QString regexDir = QRegularExpression::escape(dirPattern);
            regexDir.replace("\\*\\*", ".*");
            regexDir.replace("\\*", "[^/\\\\]*");
            regexDir.replace("\\?", ".");

            QRegularExpression rx(regexDir, QRegularExpression::CaseInsensitiveOption);
            if (!rx.match(filePath).hasMatch()) {
                continue;
            }
        }

        results.append(filePath);
    }

    return results;
}

void FileDiscovery::expandGlobRecursive(
    const QString& currentDir,
    const QStringList& patternParts,
    int partIndex,
    QStringList& results)
{
    // This method is now unused - keeping for API compatibility
    Q_UNUSED(currentDir);
    Q_UNUSED(patternParts);
    Q_UNUSED(partIndex);
    Q_UNUSED(results);
}

bool FileDiscovery::matchesPattern(const QString& filename, const QString& pattern) {
    // Convert glob pattern to regex
    QString regexPattern;
    regexPattern.reserve(pattern.size() * 2);

    for (QChar c : pattern) {
        if (c == '*') {
            regexPattern += ".*";
        } else if (c == '?') {
            regexPattern += ".";
        } else if (c == '[' || c == ']' || c == '(' || c == ')' ||
                   c == '{' || c == '}' || c == '^' || c == '$' ||
                   c == '.' || c == '+' || c == '|' || c == '\\') {
            regexPattern += '\\';
            regexPattern += c;
        } else {
            regexPattern += c;
        }
    }

    regexPattern = '^' + regexPattern + '$';

    QRegularExpression rx(regexPattern, QRegularExpression::CaseInsensitiveOption);
    return rx.match(filename).hasMatch();
}

} // namespace DentScanBatch
