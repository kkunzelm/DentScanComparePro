// SPDX-License-Identifier: GPL-2.0-or-later
// SPDX-FileCopyrightText: 2024 Prof. Dr. Karl-Heinz Kunzelmann

#include "AlignmentTransformLoader.h"

#include <QFile>
#include <QFileInfo>
#include <QJsonDocument>
#include <QJsonObject>
#include <QJsonArray>
#include <iostream>

namespace AlignmentTransformLoader {

std::string normalizeFilePath(const QString& filePath)
{
    QFileInfo fi(filePath);
    return fi.fileName().toLower().toStdString();
}

bool loadSingleTransform(const QString& jsonPath,
                         QString& sourceFile,
                         Eigen::Matrix4d& transform)
{
    QFile file(jsonPath);
    if (!file.open(QIODevice::ReadOnly)) {
        return false;
    }

    QJsonParseError parseError;
    QJsonDocument doc = QJsonDocument::fromJson(file.readAll(), &parseError);

    if (parseError.error != QJsonParseError::NoError) {
        std::cerr << "JSON parse error in " << jsonPath.toStdString()
                  << ": " << parseError.errorString().toStdString() << std::endl;
        return false;
    }

    if (!doc.isObject()) {
        return false;
    }

    QJsonObject obj = doc.object();

    // Extract source_file
    if (!obj.contains("source_file")) {
        std::cerr << "Missing source_file in " << jsonPath.toStdString() << std::endl;
        return false;
    }
    sourceFile = obj["source_file"].toString();

    // Extract transform_4x4 (16-element row-major array)
    transform = Eigen::Matrix4d::Identity();

    if (obj.contains("transform_4x4")) {
        QJsonArray arr = obj["transform_4x4"].toArray();
        if (arr.size() == 16) {
            for (int i = 0; i < 16; ++i) {
                int row = i / 4;
                int col = i % 4;
                transform(row, col) = arr[i].toDouble();
            }
        } else {
            std::cerr << "transform_4x4 has " << arr.size() << " elements, expected 16"
                      << " in " << jsonPath.toStdString() << std::endl;
            return false;
        }
    } else if (obj.contains("transform")) {
        // Alternative: 4x4 nested array format
        QJsonArray rows = obj["transform"].toArray();
        if (rows.size() == 4) {
            for (int r = 0; r < 4; ++r) {
                QJsonArray row = rows[r].toArray();
                if (row.size() == 4) {
                    for (int c = 0; c < 4; ++c) {
                        transform(r, c) = row[c].toDouble();
                    }
                }
            }
        }
    } else {
        std::cerr << "No transform found in " << jsonPath.toStdString() << std::endl;
        return false;
    }

    return true;
}

std::map<std::string, Eigen::Matrix4d> loadTransforms(const QString& alignmentsDir)
{
    std::map<std::string, Eigen::Matrix4d> result;

    QDir dir(alignmentsDir);
    if (!dir.exists()) {
        std::cerr << "Alignments directory does not exist: "
                  << alignmentsDir.toStdString() << std::endl;
        return result;
    }

    QStringList jsonFiles = dir.entryList(QStringList() << "*.json", QDir::Files);

    for (const QString& filename : jsonFiles) {
        QString jsonPath = dir.absoluteFilePath(filename);

        QString sourceFile;
        Eigen::Matrix4d transform;

        if (loadSingleTransform(jsonPath, sourceFile, transform)) {
            std::string normalizedKey = normalizeFilePath(sourceFile);
            result[normalizedKey] = transform;
        }
    }

    std::cout << "Loaded " << result.size() << " pre-computed transforms from "
              << alignmentsDir.toStdString() << std::endl;

    return result;
}

} // namespace AlignmentTransformLoader
