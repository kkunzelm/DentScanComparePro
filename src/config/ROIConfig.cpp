// SPDX-License-Identifier: GPL-2.0-or-later
// SPDX-FileCopyrightText: 2024 Prof. Dr. Karl-Heinz Kunzelmann

#include "ROIConfig.h"
#include <QFile>
#include <QJsonDocument>
#include <QJsonObject>
#include <QJsonArray>
#include <stdexcept>

namespace DentScanBatch {

ROITemplate ROITemplate::loadFromFile(const QString& filePath)
{
    QFile file(filePath);
    if (!file.open(QIODevice::ReadOnly)) {
        throw std::runtime_error("Cannot open ROI template file: " + filePath.toStdString());
    }

    QJsonParseError parseError;
    QJsonDocument doc = QJsonDocument::fromJson(file.readAll(), &parseError);
    if (parseError.error != QJsonParseError::NoError) {
        throw std::runtime_error("Failed to parse ROI template: " + parseError.errorString().toStdString());
    }

    QJsonObject root = doc.object();
    ROITemplate tmpl;

    // Load bounding box
    if (root.contains("bbox")) {
        QJsonObject bbox = root["bbox"].toObject();
        tmpl.roi.bbox.active = bbox["active"].toBool();
        if (bbox.contains("min")) {
            QJsonArray minArr = bbox["min"].toArray();
            for (int i = 0; i < 3 && i < minArr.size(); i++) {
                tmpl.roi.bbox.min[i] = minArr[i].toDouble();
            }
        }
        if (bbox.contains("max")) {
            QJsonArray maxArr = bbox["max"].toArray();
            for (int i = 0; i < 3 && i < maxArr.size(); i++) {
                tmpl.roi.bbox.max[i] = maxArr[i].toDouble();
            }
        }
    }

    // Load Z-plane
    if (root.contains("z_plane")) {
        QJsonObject zPlane = root["z_plane"].toObject();
        tmpl.roi.zPlane.active = zPlane["active"].toBool(false);
        tmpl.roi.zPlane.above_mm = zPlane["above_mm"].toDouble(2.0);
        tmpl.roi.zPlane.below_mm = zPlane["below_mm"].toDouble(12.0);
    }

    // Load brush zones
    if (root.contains("brush_zones")) {
        QJsonArray brushArray = root["brush_zones"].toArray();
        for (const auto& brushVal : brushArray) {
            QJsonObject brush = brushVal.toObject();
            BrushZone zone;
            if (brush.contains("center")) {
                QJsonArray center = brush["center"].toArray();
                for (int i = 0; i < 3 && i < center.size(); i++) {
                    zone.center[i] = center[i].toDouble();
                }
            }
            zone.radius_mm = brush["radius_mm"].toDouble(2.0);
            zone.include = brush["include"].toBool(true);
            tmpl.roi.brushZones.push_back(zone);
        }
    }

    // Load sigma
    tmpl.roi.outlierSigma = root["outlier_sigma"].toDouble(3.0);

    // Load tooth segmentation
    if (root.contains("tooth_segmentation")) {
        QJsonObject segObj = root["tooth_segmentation"].toObject();

        tmpl.useToothMask = segObj["use_tooth_mask"].toBool(false);
        tmpl.segMaxGeodesicMm = segObj["max_geodesic_mm"].toDouble(12.0);
        tmpl.segMaxCreaseAngleDeg = segObj["max_crease_deg"].toDouble(50.0);
        tmpl.segMinMeanCurvature = segObj["min_curvature"].toDouble(-4.0);
        tmpl.segCurvatureRepulsion = segObj["repulsion"].toDouble(0.1);

        // Load seed points
        if (segObj.contains("seeds")) {
            QJsonArray seedArray = segObj["seeds"].toArray();
            for (const auto& seedVal : seedArray) {
                QJsonArray seedPt = seedVal.toArray();
                if (seedPt.size() >= 3) {
                    tmpl.toothSeeds.push_back({
                        seedPt[0].toDouble(),
                        seedPt[1].toDouble(),
                        seedPt[2].toDouble()
                    });
                }
            }
        }
    }

    return tmpl;
}

bool ROITemplate::saveToFile(const QString& filePath) const
{
    QJsonObject root;

    // Save bounding box
    QJsonObject bbox;
    bbox["active"] = roi.bbox.active;
    QJsonArray bboxMin, bboxMax;
    for (int i = 0; i < 3; i++) {
        bboxMin.append(roi.bbox.min[i]);
        bboxMax.append(roi.bbox.max[i]);
    }
    bbox["min"] = bboxMin;
    bbox["max"] = bboxMax;
    root["bbox"] = bbox;

    // Save Z-plane
    QJsonObject zPlane;
    zPlane["active"] = roi.zPlane.active;
    zPlane["above_mm"] = roi.zPlane.above_mm;
    zPlane["below_mm"] = roi.zPlane.below_mm;
    root["z_plane"] = zPlane;

    // Save brush zones
    QJsonArray brushArray;
    for (const auto& zone : roi.brushZones) {
        QJsonObject brush;
        QJsonArray center;
        for (int i = 0; i < 3; i++) {
            center.append(zone.center[i]);
        }
        brush["center"] = center;
        brush["radius_mm"] = zone.radius_mm;
        brush["include"] = zone.include;
        brushArray.append(brush);
    }
    root["brush_zones"] = brushArray;

    // Save sigma
    root["outlier_sigma"] = roi.outlierSigma;

    // Save tooth segmentation
    QJsonObject segObj;
    segObj["use_tooth_mask"] = useToothMask;
    segObj["max_geodesic_mm"] = segMaxGeodesicMm;
    segObj["max_crease_deg"] = segMaxCreaseAngleDeg;
    segObj["min_curvature"] = segMinMeanCurvature;
    segObj["repulsion"] = segCurvatureRepulsion;

    QJsonArray seedArray;
    for (const auto& pt : toothSeeds) {
        QJsonArray seedPt;
        seedPt.append(pt[0]);
        seedPt.append(pt[1]);
        seedPt.append(pt[2]);
        seedArray.append(seedPt);
    }
    segObj["seeds"] = seedArray;

    root["tooth_segmentation"] = segObj;

    QFile file(filePath);
    if (!file.open(QIODevice::WriteOnly)) {
        return false;
    }

    file.write(QJsonDocument(root).toJson(QJsonDocument::Indented));
    return true;
}

} // namespace DentScanBatch
