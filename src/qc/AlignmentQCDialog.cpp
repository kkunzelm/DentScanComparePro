#include "AlignmentQCDialog.h"
#include "../visualization/VTKMeshWidget.h"
#include "../core/STLReader.h"
#include "../core/DistanceField.h"
#include "../core/ICPRegistration.h"

#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QGroupBox>
#include <QFile>
#include <QJsonDocument>
#include <QJsonObject>
#include <QJsonArray>
#include <QMessageBox>

namespace DentScanBatch {

AlignmentQCDialog::AlignmentQCDialog(
    const QString& scanPath,
    const QString& referencePath,
    const QString& scanId,
    QWidget* parent)
    : QDialog(parent)
    , m_scanPath(scanPath)
    , m_referencePath(referencePath)
    , m_scanId(scanId)
{
    setWindowTitle(QString("Alignment QC - %1").arg(scanId));
    setMinimumSize(900, 700);
    setupUI();
}

AlignmentQCDialog::~AlignmentQCDialog()
{
    // Clear VTK data before destruction
    if (m_meshWidget) {
        m_meshWidget->clearMesh();
    }
    m_scanData.reset();
    m_referenceMesh.reset();
}

void AlignmentQCDialog::setupUI()
{
    auto* mainLayout = new QVBoxLayout(this);

    // Top: Controls row
    auto* controlsLayout = new QHBoxLayout();

    m_showReferenceChk = new QCheckBox("Show Reference Wireframe");
    m_showReferenceChk->setChecked(true);
    connect(m_showReferenceChk, &QCheckBox::toggled,
            this, &AlignmentQCDialog::onShowReferenceToggled);
    controlsLayout->addWidget(m_showReferenceChk);

    controlsLayout->addStretch();
    mainLayout->addLayout(controlsLayout);

    // Middle: 3D View
    m_meshWidget = new VTKMeshWidget(this);
    m_meshWidget->setMinimumSize(800, 500);
    mainLayout->addWidget(m_meshWidget, 1);

    // Metrics row
    auto* metricsGroup = new QGroupBox("Alignment Metrics");
    auto* metricsLayout = new QHBoxLayout(metricsGroup);

    m_rmsLabel = new QLabel("RMS: -- mm");
    m_rmsLabel->setStyleSheet("font-size: 14px; font-weight: bold;");
    metricsLayout->addWidget(m_rmsLabel);

    metricsLayout->addSpacing(30);

    m_maxLabel = new QLabel("Max: -- mm");
    m_maxLabel->setStyleSheet("font-size: 14px;");
    metricsLayout->addWidget(m_maxLabel);

    metricsLayout->addSpacing(30);

    m_coverageLabel = new QLabel("Coverage: --%");
    m_coverageLabel->setStyleSheet("font-size: 14px;");
    metricsLayout->addWidget(m_coverageLabel);

    metricsLayout->addStretch();
    mainLayout->addWidget(metricsGroup);

    // Bottom: Action buttons
    auto* buttonLayout = new QHBoxLayout();
    buttonLayout->addStretch();

    m_acceptBtn = new QPushButton("Accept");
    m_acceptBtn->setStyleSheet("background-color: #4CAF50; color: white; font-weight: bold; padding: 8px 24px;");
    m_acceptBtn->setToolTip("Mark this alignment as acceptable");
    connect(m_acceptBtn, &QPushButton::clicked, this, &AlignmentQCDialog::onAcceptClicked);
    buttonLayout->addWidget(m_acceptBtn);

    buttonLayout->addSpacing(20);

    m_flagBtn = new QPushButton("Flag as Errand");
    m_flagBtn->setStyleSheet("background-color: #f44336; color: white; font-weight: bold; padding: 8px 24px;");
    m_flagBtn->setToolTip("Flag this scan for re-registration");
    connect(m_flagBtn, &QPushButton::clicked, this, &AlignmentQCDialog::onFlagClicked);
    buttonLayout->addWidget(m_flagBtn);

    buttonLayout->addSpacing(20);

    m_skipBtn = new QPushButton("Skip");
    m_skipBtn->setStyleSheet("padding: 8px 24px;");
    m_skipBtn->setToolTip("Close without making a decision");
    connect(m_skipBtn, &QPushButton::clicked, this, &AlignmentQCDialog::onSkipClicked);
    buttonLayout->addWidget(m_skipBtn);

    buttonLayout->addStretch();
    mainLayout->addLayout(buttonLayout);
}

bool AlignmentQCDialog::loadMeshes(const QString& transformJson)
{
    // Load scan
    std::string errorMsg;
    m_scanData = STLReader::read(m_scanPath.toStdString(), errorMsg);
    if (!m_scanData) {
        QMessageBox::warning(this, "Load Error",
            QString("Failed to load scan: %1").arg(QString::fromStdString(errorMsg)));
        return false;
    }

    // Load and apply transform if provided
    if (!transformJson.isEmpty()) {
        QFile file(transformJson);
        if (file.open(QIODevice::ReadOnly)) {
            QJsonDocument doc = QJsonDocument::fromJson(file.readAll());
            if (doc.isObject()) {
                QJsonObject obj = doc.object();

                // Try transform_4x4 first, then transform as 4x4 array
                Eigen::Matrix4d transform = Eigen::Matrix4d::Identity();
                bool hasTransform = false;

                if (obj.contains("transform_4x4")) {
                    QJsonArray arr = obj["transform_4x4"].toArray();
                    if (arr.size() == 16) {
                        for (int i = 0; i < 16; ++i) {
                            transform(i / 4, i % 4) = arr[i].toDouble();
                        }
                        hasTransform = true;
                    }
                } else if (obj.contains("transform")) {
                    QJsonArray rows = obj["transform"].toArray();
                    if (rows.size() == 4) {
                        for (int r = 0; r < 4; ++r) {
                            QJsonArray row = rows[r].toArray();
                            for (int c = 0; c < 4 && c < row.size(); ++c) {
                                transform(r, c) = row[c].toDouble();
                            }
                        }
                        hasTransform = true;
                    }
                }

                if (hasTransform) {
                    ICPRegistration::applyTransform(*m_scanData, transform);
                }

                // Load metrics if present
                if (obj.contains("metrics")) {
                    QJsonObject metrics = obj["metrics"].toObject();
                    double rms = metrics["rms_mm"].toDouble(0);
                    double maxDist = metrics["max_mm"].toDouble(0);
                    double coverage = metrics["coverage_pct"].toDouble(0);
                    setMetrics(rms, maxDist, coverage);
                }
            }
        }
    }

    // Load reference mesh
    auto refScan = STLReader::read(m_referencePath.toStdString(), errorMsg);
    if (!refScan) {
        QMessageBox::warning(this, "Load Error",
            QString("Failed to load reference: %1").arg(QString::fromStdString(errorMsg)));
        return false;
    }
    m_referenceMesh = std::make_shared<SurfaceMesh>(refScan->mesh);

    // Compute distances
    DistanceField::compute(*m_scanData, *refScan);

    // Show overlay
    m_meshWidget->showAlignmentOverlay(m_scanData, m_referenceMesh, -0.5, 0.5);

    return true;
}

void AlignmentQCDialog::setMetrics(double rms, double maxDist, double coverage)
{
    m_rmsLabel->setText(QString("RMS: %1 mm").arg(rms, 0, 'f', 3));
    m_maxLabel->setText(QString("Max: %1 mm").arg(maxDist, 0, 'f', 3));
    m_coverageLabel->setText(QString("Coverage: %1%").arg(coverage, 0, 'f', 1));

    // Color-code RMS value
    QString rmsColor = "green";
    if (rms > 0.1) rmsColor = "orange";
    if (rms > 0.2) rmsColor = "red";
    m_rmsLabel->setStyleSheet(QString("font-size: 14px; font-weight: bold; color: %1;").arg(rmsColor));
}

void AlignmentQCDialog::onAcceptClicked()
{
    m_result = "accept";
    accept();
}

void AlignmentQCDialog::onFlagClicked()
{
    m_result = "flag";
    accept();
}

void AlignmentQCDialog::onSkipClicked()
{
    m_result = "skip";
    reject();
}

void AlignmentQCDialog::onShowReferenceToggled(bool checked)
{
    if (checked && m_referenceMesh && m_scanData) {
        m_meshWidget->showAlignmentOverlay(m_scanData, m_referenceMesh, -0.5, 0.5);
    } else {
        m_meshWidget->hideReferenceOverlay();
    }
}

} // namespace DentScanBatch
