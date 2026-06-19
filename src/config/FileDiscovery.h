// SPDX-License-Identifier: GPL-2.0-or-later
// SPDX-FileCopyrightText: 2024 Prof. Dr. Karl-Heinz Kunzelmann

#ifndef FILEDISCOVERY_H
#define FILEDISCOVERY_H

#include "StudyConfig.h"
#include <QString>
#include <QStringList>
#include <QMap>
#include <vector>

namespace DentScanBatch {

/**
 * Information about a discovered scan file.
 */
struct DiscoveredFile {
    QString path;           // Full path to STL file
    QString scannerId;      // Matched scanner ID (e.g., "Primescan")
    QString groupId;        // Group ID (user-defined label, e.g., "SKD_20" or "condition_A")
    int repetitionId = 0;   // Repetition number (1-5)
    bool valid = true;      // False if file couldn't be properly identified
};

/**
 * Summary of discovered files for a group.
 */
struct GroupDiscovery {
    QString groupId;
    int conditionValue = 0;
    QMap<QString, QStringList> filesByScanner;  // scanner ID -> list of file paths
    int totalFiles = 0;
    QStringList warnings;
};

/**
 * File discovery utility for finding STL files based on glob patterns.
 */
class FileDiscovery {
public:
    /**
     * Discover all files matching the study configuration.
     * @param config Study configuration with file patterns
     * @param dataRoot Root directory to search (overrides config if non-empty)
     * @return List of discovered files
     */
    static std::vector<DiscoveredFile> discoverFiles(
        const StudyConfig& config,
        const QString& dataRoot = QString());

    /**
     * Discover files for a specific group.
     * @param group Group configuration
     * @param scanners Scanner definitions for ID matching
     * @param dataRoot Root directory to search
     * @return Group discovery summary
     */
    static GroupDiscovery discoverGroup(
        const GroupConfig& group,
        const std::vector<ScannerDef>& scanners,
        const QString& dataRoot);

    /**
     * Get a summary of discovered files by group.
     * @param config Study configuration
     * @param dataRoot Root directory to search
     * @return Map of group ID -> GroupDiscovery
     */
    static QMap<QString, GroupDiscovery> discoverAllGroups(
        const StudyConfig& config,
        const QString& dataRoot = QString());

    /**
     * Match a file path against scanner patterns to identify the scanner.
     * @param filePath Path to check
     * @param scanners Scanner definitions
     * @return Scanner ID or empty string if no match
     */
    static QString identifyScanner(
        const QString& filePath,
        const std::vector<ScannerDef>& scanners);

    /**
     * Extract repetition number from filename.
     * Looks for patterns like "r1", "rep1", "_1", " 1" etc.
     * @param filePath Path to check
     * @return Repetition number (1-based) or 0 if not identified
     */
    static int extractRepetitionId(const QString& filePath);

    /**
     * Expand a glob pattern to matching file paths.
     * Supports ** for recursive directory matching.
     * @param pattern Glob pattern (e.g., "**.stl")
     * @param rootDir Root directory for pattern matching
     * @return List of matching file paths
     */
    static QStringList expandGlob(const QString& pattern, const QString& rootDir);

    /**
     * Check if a filename matches a glob pattern.
     * @param filename Filename to check
     * @param pattern Glob pattern (supports * and ?)
     * @return True if matches
     */
    static bool matchesPattern(const QString& filename, const QString& pattern);

private:
    static void expandGlobRecursive(
        const QString& currentDir,
        const QStringList& patternParts,
        int partIndex,
        QStringList& results);
};

} // namespace DentScanBatch

#endif // FILEDISCOVERY_H
