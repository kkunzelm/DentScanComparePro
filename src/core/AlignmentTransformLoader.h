// SPDX-License-Identifier: GPL-2.0-or-later
// SPDX-FileCopyrightText: 2024 Prof. Dr. Karl-Heinz Kunzelmann

#pragma once

#include <Eigen/Core>
#include <QString>
#include <QDir>
#include <map>
#include <string>

namespace AlignmentTransformLoader {

/**
 * Load pre-computed alignment transforms from DentScanAlign JSON files.
 *
 * DentScanAlign outputs JSON files containing:
 * - source_file: original STL filename (may be absolute or relative)
 * - transform_4x4: 16-element row-major 4x4 transformation matrix
 *
 * @param alignmentsDir Directory containing *.json alignment files
 * @return Map from normalized source filename (lowercase, basename only) to transform
 */
std::map<std::string, Eigen::Matrix4d> loadTransforms(const QString& alignmentsDir);

/**
 * Load a single transform from a JSON file.
 *
 * @param jsonPath Path to JSON file
 * @param sourceFile Output: the source_file field from the JSON
 * @param transform Output: the 4x4 transformation matrix
 * @return True if successfully loaded
 */
bool loadSingleTransform(const QString& jsonPath,
                         QString& sourceFile,
                         Eigen::Matrix4d& transform);

/**
 * Normalize a file path for matching.
 * Extracts basename, converts to lowercase.
 *
 * @param filePath Path to normalize
 * @return Normalized path string
 */
std::string normalizeFilePath(const QString& filePath);

} // namespace AlignmentTransformLoader
