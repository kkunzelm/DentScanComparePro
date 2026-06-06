// SPDX-License-Identifier: GPL-2.0-or-later
// SPDX-FileCopyrightText: 2024 Prof. Dr. Karl-Heinz Kunzelmann

#include "StudyConfig.h"
#include <QFile>
#include <QJsonDocument>
#include <QJsonObject>
#include <QJsonArray>
#include <QFileInfo>
#include <stdexcept>

#ifdef HAVE_YAML_CPP
#include <yaml-cpp/yaml.h>
#endif

namespace DentScanBatch {

// Helper to parse ROIConfig from JSON
static ROIConfig parseROIFromJSON(const QJsonObject& obj) {
    ROIConfig roi;

    if (obj.contains("bbox")) {
        auto bbox = obj["bbox"].toObject();
        roi.bbox.active = bbox["active"].toBool(false);
        if (bbox.contains("min")) {
            auto minArr = bbox["min"].toArray();
            if (minArr.size() >= 3) {
                roi.bbox.min = {minArr[0].toDouble(), minArr[1].toDouble(), minArr[2].toDouble()};
            }
        }
        if (bbox.contains("max")) {
            auto maxArr = bbox["max"].toArray();
            if (maxArr.size() >= 3) {
                roi.bbox.max = {maxArr[0].toDouble(), maxArr[1].toDouble(), maxArr[2].toDouble()};
            }
        }
    }

    if (obj.contains("z_plane")) {
        auto zp = obj["z_plane"].toObject();
        roi.zPlane.active = zp["active"].toBool(false);
        roi.zPlane.above_mm = zp["above_mm"].toDouble(2.0);
        roi.zPlane.below_mm = zp["below_mm"].toDouble(12.0);
    }

    if (obj.contains("brush_zones")) {
        auto zones = obj["brush_zones"].toArray();
        for (const auto& zoneVal : zones) {
            auto zone = zoneVal.toObject();
            BrushZone bz;
            auto center = zone["center"].toArray();
            if (center.size() >= 3) {
                bz.center = {center[0].toDouble(), center[1].toDouble(), center[2].toDouble()};
            }
            bz.radius_mm = zone["radius_mm"].toDouble(2.0);
            bz.include = zone["include"].toBool(true);
            roi.brushZones.push_back(bz);
        }
    }

    roi.outlierSigma = obj["outlier_sigma"].toDouble(3.0);

    return roi;
}

// Helper to convert ROIConfig to JSON
static QJsonObject roiToJSON(const ROIConfig& roi) {
    QJsonObject obj;

    QJsonObject bbox;
    bbox["active"] = roi.bbox.active;
    bbox["min"] = QJsonArray{roi.bbox.min[0], roi.bbox.min[1], roi.bbox.min[2]};
    bbox["max"] = QJsonArray{roi.bbox.max[0], roi.bbox.max[1], roi.bbox.max[2]};
    obj["bbox"] = bbox;

    QJsonObject zPlane;
    zPlane["active"] = roi.zPlane.active;
    zPlane["above_mm"] = roi.zPlane.above_mm;
    zPlane["below_mm"] = roi.zPlane.below_mm;
    obj["z_plane"] = zPlane;

    QJsonArray brushZones;
    for (const auto& zone : roi.brushZones) {
        QJsonObject zoneObj;
        zoneObj["center"] = QJsonArray{zone.center[0], zone.center[1], zone.center[2]};
        zoneObj["radius_mm"] = zone.radius_mm;
        zoneObj["include"] = zone.include;
        brushZones.append(zoneObj);
    }
    obj["brush_zones"] = brushZones;

    obj["outlier_sigma"] = roi.outlierSigma;

    return obj;
}

StudyConfig StudyConfig::loadFromFile(const QString& path) {
    QFileInfo fi(path);
    QString ext = fi.suffix().toLower();

#ifdef HAVE_YAML_CPP
    if (ext == "yaml" || ext == "yml") {
        return loadFromYAML(path);
    }
#endif

    return loadFromJSON(path);
}

void StudyConfig::saveToFile(const QString& path) const {
    QFileInfo fi(path);
    QString ext = fi.suffix().toLower();

#ifdef HAVE_YAML_CPP
    if (ext == "yaml" || ext == "yml") {
        saveToYAML(path);
        return;
    }
#endif

    saveToJSON(path);
}

StudyConfig StudyConfig::loadFromJSON(const QString& path) {
    QFile file(path);
    if (!file.open(QIODevice::ReadOnly)) {
        throw std::runtime_error("Cannot open config file: " + path.toStdString());
    }

    QByteArray data = file.readAll();
    QJsonParseError parseError;
    QJsonDocument doc = QJsonDocument::fromJson(data, &parseError);

    if (parseError.error != QJsonParseError::NoError) {
        throw std::runtime_error("JSON parse error: " + parseError.errorString().toStdString());
    }

    QJsonObject root = doc.object();
    StudyConfig config;

    // Study metadata
    auto study = root["study"].toObject();
    config.name = study["name"].toString();
    config.version = study["version"].toInt(1);
    config.referenceStrategy = study["reference_strategy"].toString("gpa_mean");
    config.externalReferencePath = study["external_reference_path"].toString();
    config.scansPreAligned = study["scans_pre_aligned"].toBool(false);
    config.alignmentsDirectory = study["alignments_directory"].toString();

    // Alignment config
    auto align = study["alignment"].toObject();
    config.alignment.maxIcpIterations = align["max_icp_iterations"].toInt(100);
    config.alignment.convergenceThreshold = align["convergence_threshold_mm"].toDouble(0.01);
    config.alignment.usePcaCoarse = align["use_pca_coarse"].toBool(true);
    config.alignment.use4OrientationTest = align["use_4orientation_test"].toBool(true);

    // Scanners
    auto scanners = root["scanners"].toArray();
    for (const auto& scanVal : scanners) {
        auto scan = scanVal.toObject();
        ScannerDef scanner;
        scanner.id = scan["id"].toString();
        auto patterns = scan["patterns"].toArray();
        for (const auto& pat : patterns) {
            scanner.patterns.append(pat.toString());
        }
        config.scanners.push_back(scanner);
    }

    // Groups
    auto groups = root["groups"].toArray();
    for (const auto& grpVal : groups) {
        auto grp = grpVal.toObject();
        GroupConfig group;
        group.id = grp["id"].toString();
        group.skd_mm = grp["skd_mm"].toInt();

        auto patterns = grp["file_patterns"].toArray();
        for (const auto& pat : patterns) {
            group.filePatterns.append(pat.toString());
        }

        if (grp.contains("roi")) {
            auto roiObj = grp["roi"].toObject();
            if (roiObj.contains("inherit_from")) {
                group.inheritROIFrom = roiObj["inherit_from"].toString();
            } else {
                group.roi = parseROIFromJSON(roiObj);
            }
        }

        if (grp.contains("outlier")) {
            auto outlier = grp["outlier"].toObject();
            if (!outlier.contains("inherit_from")) {
                group.roi.outlierSigma = outlier["threshold_sigma"].toDouble(3.0);
            }
        }

        group.representativeScan = grp["representative_scan"].toString();
        config.groups.push_back(group);
    }

    // Metrics config
    auto metrics = root["metrics"].toObject();
    if (metrics.contains("trueness")) {
        auto trueness = metrics["trueness"].toArray();
        config.metrics.computeRMS = false;
        config.metrics.computeMeanAbsolute = false;
        config.metrics.computeMax = false;
        config.metrics.computePercentile95 = false;
        for (const auto& m : trueness) {
            QString name = m.toObject()["name"].toString();
            if (name == "RMS") config.metrics.computeRMS = true;
            else if (name == "MeanAbsolute") config.metrics.computeMeanAbsolute = true;
            else if (name == "Max") config.metrics.computeMax = true;
            else if (name == "Percentile95") config.metrics.computePercentile95 = true;
        }
    }

    // Output config
    auto output = root["output"].toObject();
    config.output.baseDir = output["base_dir"].toString("./results");
    config.output.metricsCSV = output["metrics_csv"].toString("long_format_metrics.csv");
    config.output.summaryCSV = output["summary_csv"].toString("summary_by_scanner_skd.csv");
    config.output.precisionCSV = output["precision_csv"].toString("precision_matrix.csv");

    config.resolveInheritance();
    return config;
}

void StudyConfig::saveToJSON(const QString& path) const {
    QJsonObject root;

    // Study metadata
    QJsonObject study;
    study["name"] = name;
    study["version"] = version;
    study["reference_strategy"] = referenceStrategy;
    if (!externalReferencePath.isEmpty()) {
        study["external_reference_path"] = externalReferencePath;
    }
    if (scansPreAligned) {
        study["scans_pre_aligned"] = scansPreAligned;
    }
    if (!alignmentsDirectory.isEmpty()) {
        study["alignments_directory"] = alignmentsDirectory;
    }

    QJsonObject align;
    align["max_icp_iterations"] = alignment.maxIcpIterations;
    align["convergence_threshold_mm"] = alignment.convergenceThreshold;
    align["use_pca_coarse"] = alignment.usePcaCoarse;
    align["use_4orientation_test"] = alignment.use4OrientationTest;
    study["alignment"] = align;

    root["study"] = study;

    // Scanners
    QJsonArray scannersArr;
    for (const auto& scanner : scanners) {
        QJsonObject scanObj;
        scanObj["id"] = scanner.id;
        QJsonArray patterns;
        for (const auto& pat : scanner.patterns) {
            patterns.append(pat);
        }
        scanObj["patterns"] = patterns;
        scannersArr.append(scanObj);
    }
    root["scanners"] = scannersArr;

    // Groups
    QJsonArray groupsArr;
    for (const auto& group : groups) {
        QJsonObject grpObj;
        grpObj["id"] = group.id;
        grpObj["skd_mm"] = group.skd_mm;

        QJsonArray patterns;
        for (const auto& pat : group.filePatterns) {
            patterns.append(pat);
        }
        grpObj["file_patterns"] = patterns;

        if (!group.inheritROIFrom.isEmpty()) {
            QJsonObject roiObj;
            roiObj["inherit_from"] = group.inheritROIFrom;
            grpObj["roi"] = roiObj;
        } else {
            grpObj["roi"] = roiToJSON(group.roi);
        }

        if (!group.representativeScan.isEmpty()) {
            grpObj["representative_scan"] = group.representativeScan;
        }

        groupsArr.append(grpObj);
    }
    root["groups"] = groupsArr;

    // Output config
    QJsonObject outputObj;
    outputObj["base_dir"] = output.baseDir;
    outputObj["metrics_csv"] = output.metricsCSV;
    outputObj["summary_csv"] = output.summaryCSV;
    outputObj["precision_csv"] = output.precisionCSV;
    root["output"] = outputObj;

    QJsonDocument doc(root);
    QFile file(path);
    if (!file.open(QIODevice::WriteOnly)) {
        throw std::runtime_error("Cannot write config file: " + path.toStdString());
    }
    file.write(doc.toJson(QJsonDocument::Indented));
}

StudyConfig StudyConfig::createDefault() {
    StudyConfig config;
    config.name = "DefektIIa_MultiScanner_SKD_Evaluation";
    config.version = 1;
    config.referenceStrategy = "gpa_mean";

    // Default scanners
    config.scanners = {
        {"FussenS6000", {"Fussen*S6000*", "FussenS6000*"}},
        {"iTeroLumina", {"iTero*Lumina*", "iTeroLumina*"}},
        {"Mediti700", {"Medit*i700*", "Mediti700*"}},
        {"Primescan", {"Primescan*"}},
        {"Trios4", {"Trios*4*"}},
        {"Trios5", {"Trios*5*"}}
    };

    // Default ROI - z-plane inactive by default, user must explicitly enable
    ROIConfig defaultROI;
    defaultROI.zPlane.active = false;
    defaultROI.zPlane.above_mm = 2.0;
    defaultROI.zPlane.below_mm = 12.0;
    defaultROI.outlierSigma = 3.0;

    // SKD groups
    std::vector<std::pair<QString, int>> skdLevels = {
        {"SKD_18", 18},
        {"SKD_20", 20},
        {"SKD_22", 22},
        {"SKD_24", 24},
        {"SKD_26", 26},
        {"SKD_28", 28},
        {"SKD_30", 30}
    };

    for (const auto& [id, skd] : skdLevels) {
        GroupConfig group;
        group.id = id;
        group.skd_mm = skd;
        group.filePatterns = {
            QString("**/SKD*%1*/*.stl").arg(skd),
            QString("**/SKD %1/**/*.stl").arg(skd),
            QString("**/%1mm*/*.stl").arg(skd)
        };
        group.roi = defaultROI;
        config.groups.push_back(group);
    }

    return config;
}

void StudyConfig::resolveInheritance() {
    for (auto& group : groups) {
        if (!group.inheritROIFrom.isEmpty()) {
            const GroupConfig* parent = findGroup(group.inheritROIFrom);
            if (parent) {
                group.roi = parent->roi;
            }
        }
    }
}

const ROIConfig& StudyConfig::getEffectiveROI(const QString& groupId) const {
    const GroupConfig* group = findGroup(groupId);
    if (!group) {
        static ROIConfig defaultROI;
        return defaultROI;
    }
    return group->roi;
}

const GroupConfig* StudyConfig::findGroup(const QString& groupId) const {
    for (const auto& group : groups) {
        if (group.id == groupId) {
            return &group;
        }
    }
    return nullptr;
}

GroupConfig* StudyConfig::findGroup(const QString& groupId) {
    for (auto& group : groups) {
        if (group.id == groupId) {
            return &group;
        }
    }
    return nullptr;
}

const ScannerDef* StudyConfig::findScanner(const QString& scannerId) const {
    for (const auto& scanner : scanners) {
        if (scanner.id == scannerId) {
            return &scanner;
        }
    }
    return nullptr;
}

QStringList StudyConfig::validate() const {
    QStringList errors;

    if (name.isEmpty()) {
        errors.append("Study name is required");
    }

    if (groups.empty()) {
        errors.append("At least one group is required");
    }

    for (const auto& group : groups) {
        if (group.id.isEmpty()) {
            errors.append("Group ID is required");
        }
        if (group.filePatterns.empty()) {
            errors.append(QString("Group '%1' has no file patterns").arg(group.id));
        }
        if (!group.inheritROIFrom.isEmpty() && !findGroup(group.inheritROIFrom)) {
            errors.append(QString("Group '%1' inherits from unknown group '%2'")
                .arg(group.id, group.inheritROIFrom));
        }
    }

    return errors;
}

#ifdef HAVE_YAML_CPP
// Helper to parse ROIConfig from YAML
static ROIConfig parseROIFromYAML(const YAML::Node& node) {
    ROIConfig roi;

    if (node["bbox"]) {
        auto bbox = node["bbox"];
        roi.bbox.active = bbox["active"].as<bool>(false);
        if (bbox["min"]) {
            auto minArr = bbox["min"];
            if (minArr.size() >= 3) {
                roi.bbox.min = {minArr[0].as<double>(), minArr[1].as<double>(), minArr[2].as<double>()};
            }
        }
        if (bbox["max"]) {
            auto maxArr = bbox["max"];
            if (maxArr.size() >= 3) {
                roi.bbox.max = {maxArr[0].as<double>(), maxArr[1].as<double>(), maxArr[2].as<double>()};
            }
        }
    }

    if (node["z_plane"]) {
        auto zp = node["z_plane"];
        roi.zPlane.active = zp["active"].as<bool>(false);
        roi.zPlane.above_mm = zp["above_mm"].as<double>(2.0);
        roi.zPlane.below_mm = zp["below_mm"].as<double>(12.0);
    }

    if (node["brush_zones"]) {
        for (const auto& zoneNode : node["brush_zones"]) {
            BrushZone bz;
            if (zoneNode["center"]) {
                auto center = zoneNode["center"];
                if (center.size() >= 3) {
                    bz.center = {center[0].as<double>(), center[1].as<double>(), center[2].as<double>()};
                }
            }
            bz.radius_mm = zoneNode["radius_mm"].as<double>(2.0);
            bz.include = zoneNode["include"].as<bool>(true);
            roi.brushZones.push_back(bz);
        }
    }

    roi.outlierSigma = node["outlier_sigma"].as<double>(3.0);

    return roi;
}

StudyConfig StudyConfig::loadFromYAML(const QString& path) {
    YAML::Node root = YAML::LoadFile(path.toStdString());
    StudyConfig config;

    // Study metadata
    if (root["study"]) {
        auto study = root["study"];
        config.name = QString::fromStdString(study["name"].as<std::string>(""));
        config.version = study["version"].as<int>(1);
        config.referenceStrategy = QString::fromStdString(study["reference_strategy"].as<std::string>("gpa_mean"));

        if (study["alignment"]) {
            auto align = study["alignment"];
            config.alignment.maxIcpIterations = align["max_icp_iterations"].as<int>(100);
            config.alignment.convergenceThreshold = align["convergence_threshold_mm"].as<double>(0.01);
            config.alignment.usePcaCoarse = align["use_pca_coarse"].as<bool>(true);
            config.alignment.use4OrientationTest = align["use_4orientation_test"].as<bool>(true);
        }
    }

    // Scanners
    if (root["scanners"]) {
        for (const auto& scanNode : root["scanners"]) {
            ScannerDef scanner;
            scanner.id = QString::fromStdString(scanNode["id"].as<std::string>(""));
            if (scanNode["patterns"]) {
                for (const auto& pat : scanNode["patterns"]) {
                    scanner.patterns.append(QString::fromStdString(pat.as<std::string>()));
                }
            }
            config.scanners.push_back(scanner);
        }
    }

    // Groups
    if (root["groups"]) {
        for (const auto& grpNode : root["groups"]) {
            GroupConfig group;
            group.id = QString::fromStdString(grpNode["id"].as<std::string>(""));
            group.skd_mm = grpNode["skd_mm"].as<int>(0);

            if (grpNode["file_patterns"]) {
                for (const auto& pat : grpNode["file_patterns"]) {
                    group.filePatterns.append(QString::fromStdString(pat.as<std::string>()));
                }
            }

            if (grpNode["roi"]) {
                auto roiNode = grpNode["roi"];
                if (roiNode["inherit_from"]) {
                    group.inheritROIFrom = QString::fromStdString(roiNode["inherit_from"].as<std::string>());
                } else {
                    group.roi = parseROIFromYAML(roiNode);
                }
            }

            if (grpNode["representative_scan"]) {
                group.representativeScan = QString::fromStdString(grpNode["representative_scan"].as<std::string>());
            }

            config.groups.push_back(group);
        }
    }

    // Output config
    if (root["output"]) {
        auto output = root["output"];
        config.output.baseDir = QString::fromStdString(output["base_dir"].as<std::string>("./results"));
        config.output.metricsCSV = QString::fromStdString(output["metrics_csv"].as<std::string>("long_format_metrics.csv"));
        config.output.summaryCSV = QString::fromStdString(output["summary_csv"].as<std::string>("summary_by_scanner_skd.csv"));
        config.output.precisionCSV = QString::fromStdString(output["precision_csv"].as<std::string>("precision_matrix.csv"));
    }

    config.resolveInheritance();
    return config;
}

void StudyConfig::saveToYAML(const QString& path) const {
    YAML::Emitter out;
    out << YAML::BeginMap;

    // Study metadata
    out << YAML::Key << "study" << YAML::Value << YAML::BeginMap;
    out << YAML::Key << "name" << YAML::Value << name.toStdString();
    out << YAML::Key << "version" << YAML::Value << version;
    out << YAML::Key << "reference_strategy" << YAML::Value << referenceStrategy.toStdString();

    out << YAML::Key << "alignment" << YAML::Value << YAML::BeginMap;
    out << YAML::Key << "max_icp_iterations" << YAML::Value << alignment.maxIcpIterations;
    out << YAML::Key << "convergence_threshold_mm" << YAML::Value << alignment.convergenceThreshold;
    out << YAML::Key << "use_pca_coarse" << YAML::Value << alignment.usePcaCoarse;
    out << YAML::Key << "use_4orientation_test" << YAML::Value << alignment.use4OrientationTest;
    out << YAML::EndMap;

    out << YAML::EndMap;

    // Scanners
    out << YAML::Key << "scanners" << YAML::Value << YAML::BeginSeq;
    for (const auto& scanner : scanners) {
        out << YAML::BeginMap;
        out << YAML::Key << "id" << YAML::Value << scanner.id.toStdString();
        out << YAML::Key << "patterns" << YAML::Value << YAML::BeginSeq;
        for (const auto& pat : scanner.patterns) {
            out << pat.toStdString();
        }
        out << YAML::EndSeq;
        out << YAML::EndMap;
    }
    out << YAML::EndSeq;

    // Groups
    out << YAML::Key << "groups" << YAML::Value << YAML::BeginSeq;
    for (const auto& group : groups) {
        out << YAML::BeginMap;
        out << YAML::Key << "id" << YAML::Value << group.id.toStdString();
        out << YAML::Key << "skd_mm" << YAML::Value << group.skd_mm;

        out << YAML::Key << "file_patterns" << YAML::Value << YAML::BeginSeq;
        for (const auto& pat : group.filePatterns) {
            out << pat.toStdString();
        }
        out << YAML::EndSeq;

        if (!group.inheritROIFrom.isEmpty()) {
            out << YAML::Key << "roi" << YAML::Value << YAML::BeginMap;
            out << YAML::Key << "inherit_from" << YAML::Value << group.inheritROIFrom.toStdString();
            out << YAML::EndMap;
        }

        out << YAML::EndMap;
    }
    out << YAML::EndSeq;

    // Output config
    out << YAML::Key << "output" << YAML::Value << YAML::BeginMap;
    out << YAML::Key << "base_dir" << YAML::Value << output.baseDir.toStdString();
    out << YAML::Key << "metrics_csv" << YAML::Value << output.metricsCSV.toStdString();
    out << YAML::Key << "summary_csv" << YAML::Value << output.summaryCSV.toStdString();
    out << YAML::Key << "precision_csv" << YAML::Value << output.precisionCSV.toStdString();
    out << YAML::EndMap;

    out << YAML::EndMap;

    QFile file(path);
    if (!file.open(QIODevice::WriteOnly | QIODevice::Text)) {
        throw std::runtime_error("Cannot write config file: " + path.toStdString());
    }
    file.write(out.c_str());
}
#endif // HAVE_YAML_CPP

} // namespace DentScanBatch
