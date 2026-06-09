// SPDX-License-Identifier: GPL-2.0-or-later
// SPDX-FileCopyrightText: 2024 Prof. Dr. Karl-Heinz Kunzelmann

#ifndef STUDYCONFIG_H
#define STUDYCONFIG_H

#include "ROIConfig.h"
#include <QString>
#include <QStringList>
#include <vector>

namespace DentScanBatch {

/**
 * Scanner definition with ID and file matching patterns.
 */
struct ScannerDef {
    QString id;                    // e.g., "FussenS6000"
    QStringList patterns;          // Glob patterns to match scanner folders
};

/**
 * Configuration for a single SKD (inter-incisor distance) group.
 * Each group contains scans from multiple scanners at the same SKD level.
 */
struct GroupConfig {
    QString id;                    // e.g., "SKD_20"
    int skd_mm = 0;               // SKD value in mm
    QStringList filePatterns;      // Glob patterns to find STL files
    ROIConfig roi;                 // ROI configuration for this group
    QString representativeScan;    // Path to representative scan for template editor
    QString inheritROIFrom;        // ID of group to inherit ROI from (empty = own ROI)
};

/**
 * Alignment parameters for ICP registration.
 */
struct AlignmentConfig {
    int maxIcpIterations = 100;
    double convergenceThreshold = 0.01;  // mm
    bool usePcaCoarse = true;
    bool use4OrientationTest = true;
};

/**
 * Metrics to compute for each scan.
 */
struct MetricsConfig {
    bool computeRMS = true;
    bool computeMeanAbsolute = true;
    bool computeMax = true;
    bool computePercentile95 = true;
    bool computePrecision = true;  // Pairwise precision within scanner groups
};

/**
 * Output configuration.
 */
struct OutputConfig {
    QString baseDir = "./results";
    QString metricsCSV = "long_format_metrics.csv";
    QString summaryCSV = "summary_by_scanner_skd.csv";
    QString precisionCSV = "precision_matrix.csv";
};

/**
 * Complete study configuration.
 * Loaded from YAML or JSON configuration file.
 */
class StudyConfig {
public:
    // Study metadata
    QString name;
    int version = 1;
    QString referenceStrategy = "gpa_mean";  // "gpa_mean", "fixed_scanner:<name>", or "external"
    QString externalReferencePath;           // Path to external reference STL (when referenceStrategy == "external")
    bool scansPreAligned = false;            // If true, skip GPA alignment (scans already aligned by DentScanAlign)
    bool scansNormalized = true;             // If true, do NOT load/apply JSON transforms (geometry already contains the baked transform)
    QString alignmentsDirectory;             // Directory containing DentScanAlign JSON transform files

    // Scanner definitions
    std::vector<ScannerDef> scanners;

    // SKD groups
    std::vector<GroupConfig> groups;

    // Processing parameters
    AlignmentConfig alignment;
    MetricsConfig metrics;
    OutputConfig output;

    // Data root directory (can be overridden from command line)
    QString dataRootDir;

    /**
     * Load configuration from a YAML or JSON file.
     * File format is detected from extension (.yaml/.yml vs .json).
     * @param path Path to configuration file
     * @return Loaded configuration
     * @throws std::runtime_error on parse errors
     */
    static StudyConfig loadFromFile(const QString& path);

    /**
     * Save configuration to a file.
     * @param path Path to output file (.yaml/.yml or .json)
     */
    void saveToFile(const QString& path) const;

    /**
     * Create a default configuration suitable for the dental scanner study.
     */
    static StudyConfig createDefault();

    /**
     * Resolve ROI inheritance between groups.
     * Should be called after loading to propagate inherited ROI settings.
     */
    void resolveInheritance();

    /**
     * Get the effective ROI config for a group, considering inheritance.
     */
    const ROIConfig& getEffectiveROI(const QString& groupId) const;

    /**
     * Find a group by ID.
     * @return Pointer to group or nullptr if not found
     */
    const GroupConfig* findGroup(const QString& groupId) const;
    GroupConfig* findGroup(const QString& groupId);

    /**
     * Find a scanner by ID.
     * @return Pointer to scanner or nullptr if not found
     */
    const ScannerDef* findScanner(const QString& scannerId) const;

    /**
     * Validate configuration for completeness and consistency.
     * @return List of validation errors (empty if valid)
     */
    QStringList validate() const;

private:
    static StudyConfig loadFromJSON(const QString& path);
    void saveToJSON(const QString& path) const;

#ifdef HAVE_YAML_CPP
    static StudyConfig loadFromYAML(const QString& path);
    void saveToYAML(const QString& path) const;
#endif
};

} // namespace DentScanBatch

#endif // STUDYCONFIG_H
