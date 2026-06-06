// SPDX-License-Identifier: GPL-2.0-or-later
// SPDX-FileCopyrightText: 2024 Prof. Dr. Karl-Heinz Kunzelmann

#include "ErrandResolutionDialog.h"
#include "LandmarkRegistration.h"
#include "../core/STLReader.h"
#include "../core/DistanceField.h"
#include "../core/Mesh.h"

#include <algorithm>
#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QSplitter>
#include <QGroupBox>
#include <QMessageBox>
#include <QApplication>
#include <QDebug>

namespace DentScanBatch {

ErrandResolutionDialog::ErrandResolutionDialog(
    const QString& scanPath,
    const QString& gpaMeanPath,
    const QString& scanId,
    QWidget* parent)
    : QDialog(parent)
    , m_scanPath(scanPath)
    , m_gpaMeanPath(gpaMeanPath)
    , m_scanId(scanId)
{
    setWindowTitle(QString("Re-Register: %1").arg(scanId));
    setMinimumSize(1400, 700);

    // Don't quit app when this dialog closes
    setAttribute(Qt::WA_QuitOnClose, false);

    setupUI();
    loadMeshes();
}

ErrandResolutionDialog::~ErrandResolutionDialog()
{
    // Don't call clearMesh() here - it triggers VTK Render() calls during
    // destruction which can corrupt state. Let VTKMeshWidget's destructor
    // handle its own cleanup.

    // Just release our mesh data references
    m_scan.reset();
    m_scanBackup.reset();
    m_gpaRef.reset();
    m_refData.reset();
}

void ErrandResolutionDialog::setupUI()
{
    auto* mainLayout = new QVBoxLayout(this);

    // Top: Three side-by-side 3D views
    auto* splitter = new QSplitter(Qt::Horizontal, this);

    // 1. GPA Reference view panel (LEFT)
    auto* refPanel = new QWidget();
    auto* refLayout = new QVBoxLayout(refPanel);
    refLayout->setContentsMargins(4, 4, 4, 4);

    refLayout->addWidget(new QLabel("<b>GPA REFERENCE</b>"));

    m_refView = new VTKMeshWidget(refPanel);
    m_refView->setMinimumSize(300, 350);
    refLayout->addWidget(m_refView, 1);

    m_refPointsLabel = new QLabel("Points: 0 picked");
    refLayout->addWidget(m_refPointsLabel);

    auto* refViewBtn = new QPushButton("Occlusal View");
    refViewBtn->setToolTip("Set camera to look straight down Z-axis");
    connect(refViewBtn, &QPushButton::clicked, this, [this]() {
        m_refView->setOcclusalView();
    });
    refLayout->addWidget(refViewBtn);

    splitter->addWidget(refPanel);

    // 2. Scan view panel (MIDDLE)
    auto* scanPanel = new QWidget();
    auto* scanLayout = new QVBoxLayout(scanPanel);
    scanLayout->setContentsMargins(4, 4, 4, 4);

    scanLayout->addWidget(new QLabel("<b>SCAN (aligning)</b>"));

    m_scanView = new VTKMeshWidget(scanPanel);
    m_scanView->setMinimumSize(300, 350);
    scanLayout->addWidget(m_scanView, 1);

    m_scanPointsLabel = new QLabel("Points: 0 picked");
    scanLayout->addWidget(m_scanPointsLabel);

    auto* scanViewBtn = new QPushButton("Occlusal View");
    scanViewBtn->setToolTip("Set camera to look straight down Z-axis");
    connect(scanViewBtn, &QPushButton::clicked, this, [this]() {
        m_scanView->setOcclusalView();
    });
    scanLayout->addWidget(scanViewBtn);

    splitter->addWidget(scanPanel);

    // 3. Difference map view panel (RIGHT)
    auto* diffPanel = new QWidget();
    auto* diffLayout = new QVBoxLayout(diffPanel);
    diffLayout->setContentsMargins(4, 4, 4, 4);

    diffLayout->addWidget(new QLabel("<b>DIFFERENCE MAP</b>"));

    m_diffView = new VTKMeshWidget(diffPanel);
    m_diffView->setMinimumSize(300, 350);
    diffLayout->addWidget(m_diffView, 1);

    m_diffLabel = new QLabel("Compute alignment first");
    m_diffLabel->setStyleSheet("color: #888;");
    diffLayout->addWidget(m_diffLabel);

    splitter->addWidget(diffPanel);
    splitter->setSizes({400, 400, 400});

    mainLayout->addWidget(splitter, 1);

    // Middle: Landmark list (compact)
    auto* landmarkGroup = new QGroupBox("Corresponding Landmarks");
    auto* landmarkLayout = new QHBoxLayout(landmarkGroup);

    m_landmarkList = new QListWidget();
    m_landmarkList->setMaximumHeight(80);
    landmarkLayout->addWidget(m_landmarkList, 1);

    auto* landmarkBtns = new QVBoxLayout();
    m_undoBtn = new QPushButton("Undo Last");
    connect(m_undoBtn, &QPushButton::clicked, this, &ErrandResolutionDialog::onUndoLastPair);
    landmarkBtns->addWidget(m_undoBtn);

    m_clearBtn = new QPushButton("Clear All");
    connect(m_clearBtn, &QPushButton::clicked, this, &ErrandResolutionDialog::onClearAllPairs);
    landmarkBtns->addWidget(m_clearBtn);
    landmarkBtns->addStretch();

    landmarkLayout->addLayout(landmarkBtns);

    mainLayout->addWidget(landmarkGroup);

    // Status label with guidance
    m_statusLabel = new QLabel("Pick same anatomical landmark on Ref then Scan (e.g., cusp tip 16). Numbers must match!");
    m_statusLabel->setStyleSheet("font-style: italic; color: #666; font-size: 11px;");
    mainLayout->addWidget(m_statusLabel);

    // Bottom: Action buttons
    auto* buttonLayout = new QHBoxLayout();

    m_alignBtn = new QPushButton("Compute Alignment");
    m_alignBtn->setStyleSheet("font-weight: bold;");
    connect(m_alignBtn, &QPushButton::clicked, this, &ErrandResolutionDialog::onComputeAlignment);
    buttonLayout->addWidget(m_alignBtn);

    m_icpBtn = new QPushButton("Run ICP Refinement");
    m_icpBtn->setEnabled(false);
    connect(m_icpBtn, &QPushButton::clicked, this, &ErrandResolutionDialog::onRunICP);
    buttonLayout->addWidget(m_icpBtn);

    buttonLayout->addStretch();

    m_acceptBtn = new QPushButton("Accept Result");
    m_acceptBtn->setStyleSheet("background-color: #4CAF50; color: white; font-weight: bold;");
    m_acceptBtn->setEnabled(false);
    connect(m_acceptBtn, &QPushButton::clicked, this, &ErrandResolutionDialog::onAccept);
    buttonLayout->addWidget(m_acceptBtn);

    m_rejectBtn = new QPushButton("Reject / Skip");
    m_rejectBtn->setStyleSheet("background-color: #f44336; color: white;");
    connect(m_rejectBtn, &QPushButton::clicked, this, &ErrandResolutionDialog::onReject);
    buttonLayout->addWidget(m_rejectBtn);

    mainLayout->addLayout(buttonLayout);

    // Connect point picking signals
    connect(m_scanView, &VTKMeshWidget::pointPicked,
            this, &ErrandResolutionDialog::onScanPointPicked);
    connect(m_refView, &VTKMeshWidget::pointPicked,
            this, &ErrandResolutionDialog::onRefPointPicked);
}

void ErrandResolutionDialog::loadMeshes()
{
    // Load scan mesh
    std::string errorMsg;
    m_scan = STLReader::read(m_scanPath.toStdString(), errorMsg);
    if (!m_scan) {
        QMessageBox::critical(this, "Error",
            QString("Failed to load scan: %1").arg(QString::fromStdString(errorMsg)));
        return;
    }

    // Make a backup for potential reset
    m_scanBackup = std::make_shared<ScanData>(*m_scan);

    // Load GPA mean mesh
    m_gpaRef = LandmarkRegistration::loadGPAMean(m_gpaMeanPath);
    if (!m_gpaRef) {
        QMessageBox::critical(this, "Error", "Failed to load GPA reference mesh");
        return;
    }

    // Create ScanData for reference (stored for reuse, avoids repeated copies)
    m_refData = std::make_shared<ScanData>();
    m_refData->mesh = std::move(*m_gpaRef);
    m_refData->scannerName = "GPA Reference";
    m_refData->triangleCount = m_refData->mesh.num_faces();
    m_refData->registered = true;

    m_gpaRef.reset();

    // Display meshes
    m_refView->setMesh(m_refData);
    m_scanView->setMesh(m_scan);

    // Enable pick mode on reference and scan views
    m_refView->setPickMode(true);
    m_scanView->setPickMode(true);
}

void ErrandResolutionDialog::onScanPointPicked(double x, double y, double z)
{
    m_scanLandmarks.push_back({x, y, z});
    updateLandmarkList();
    updateButtonStates();
}

void ErrandResolutionDialog::onRefPointPicked(double x, double y, double z)
{
    m_refLandmarks.push_back({x, y, z});
    updateLandmarkList();
    updateButtonStates();
}

void ErrandResolutionDialog::onUndoLastPair()
{
    if (m_scanLandmarks.size() > m_refLandmarks.size() && !m_scanLandmarks.empty()) {
        m_scanLandmarks.pop_back();
    } else if (!m_refLandmarks.empty()) {
        m_refLandmarks.pop_back();
    } else if (!m_scanLandmarks.empty()) {
        m_scanLandmarks.pop_back();
    }
    updateLandmarkList();
    updateButtonStates();
}

void ErrandResolutionDialog::onClearAllPairs()
{
    m_scanLandmarks.clear();
    m_refLandmarks.clear();
    updateLandmarkList();
    updateButtonStates();

    // Reset alignment state
    m_alignmentComputed = false;
    m_icpRefined = false;

    // Restore scan from backup
    if (m_scanBackup) {
        m_scan = std::make_shared<ScanData>(*m_scanBackup);
        m_scanView->setMesh(m_scan);
    }

    // Clear difference view
    m_diffView->clearMesh();
    m_diffLabel->setText("Compute alignment first");
}

void ErrandResolutionDialog::updateLandmarkList()
{
    m_landmarkList->clear();

    std::size_t pairs = std::min(m_scanLandmarks.size(), m_refLandmarks.size());

    for (std::size_t i = 0; i < pairs; ++i) {
        const auto& sp = m_scanLandmarks[i];
        const auto& rp = m_refLandmarks[i];

        QString item = QString("%1. Ref(%2,%3,%4) <-> Scan(%5,%6,%7)")
            .arg(i + 1)
            .arg(rp[0], 0, 'f', 1).arg(rp[1], 0, 'f', 1).arg(rp[2], 0, 'f', 1)
            .arg(sp[0], 0, 'f', 1).arg(sp[1], 0, 'f', 1).arg(sp[2], 0, 'f', 1);
        m_landmarkList->addItem(item);
    }

    // Show unpaired points
    for (std::size_t i = pairs; i < m_refLandmarks.size(); ++i) {
        const auto& rp = m_refLandmarks[i];
        m_landmarkList->addItem(QString("  Ref: (%1,%2,%3) - awaiting scan pair")
            .arg(rp[0], 0, 'f', 1).arg(rp[1], 0, 'f', 1).arg(rp[2], 0, 'f', 1));
    }
    for (std::size_t i = pairs; i < m_scanLandmarks.size(); ++i) {
        const auto& sp = m_scanLandmarks[i];
        m_landmarkList->addItem(QString("  Scan: (%1,%2,%3) - awaiting ref pair")
            .arg(sp[0], 0, 'f', 1).arg(sp[1], 0, 'f', 1).arg(sp[2], 0, 'f', 1));
    }

    // Update point count labels
    m_refPointsLabel->setText(QString("Points: %1 picked").arg(m_refLandmarks.size()));
    m_scanPointsLabel->setText(QString("Points: %1 picked").arg(m_scanLandmarks.size()));

    // Update sphere visualization
    m_refView->showPickSpheres(m_refLandmarks);
    m_scanView->showPickSpheres(m_scanLandmarks);
}

void ErrandResolutionDialog::updateButtonStates()
{
    std::size_t pairs = std::min(m_scanLandmarks.size(), m_refLandmarks.size());

    bool canAlign = pairs >= 3;
    m_alignBtn->setEnabled(canAlign && !m_alignmentComputed);
    m_undoBtn->setEnabled(!m_scanLandmarks.empty() || !m_refLandmarks.empty());
    m_clearBtn->setEnabled(!m_scanLandmarks.empty() || !m_refLandmarks.empty());

    if (pairs >= 3) {
        m_statusLabel->setText(QString("%1 pairs ready. Verify: #1 on Ref = #1 on Scan (same tooth)!").arg(pairs));
    } else {
        m_statusLabel->setText(QString("Need %1 more pairs. Pick SAME anatomical point on Ref then Scan.")
            .arg(3 - pairs));
    }

    m_icpBtn->setEnabled(m_alignmentComputed && !m_icpRefined);
    m_acceptBtn->setEnabled(m_alignmentComputed);
}

void ErrandResolutionDialog::onComputeAlignment()
{
    std::size_t pairs = std::min(m_scanLandmarks.size(), m_refLandmarks.size());
    if (pairs < 3) {
        QMessageBox::warning(this, "Error", "Need at least 3 landmark pairs");
        return;
    }

    // Truncate to paired points only
    std::vector<std::array<double, 3>> scanPts(
        m_scanLandmarks.begin(), m_scanLandmarks.begin() + pairs);
    std::vector<std::array<double, 3>> refPts(
        m_refLandmarks.begin(), m_refLandmarks.begin() + pairs);

    // Debug output
    qDebug() << "=== Landmark Registration ===";
    for (std::size_t i = 0; i < pairs; ++i) {
        qDebug() << "Pair" << i << ": Scan(" << scanPts[i][0] << "," << scanPts[i][1] << "," << scanPts[i][2]
                 << ") -> Ref(" << refPts[i][0] << "," << refPts[i][1] << "," << refPts[i][2] << ")";
    }

    // Compute Kabsch transform
    Eigen::Matrix4d transform = LandmarkRegistration::computeKabschTransform(scanPts, refPts);

    // Debug: print transform matrix
    qDebug() << "=== Kabsch Transform Matrix ===";
    qDebug() << QString("[%1, %2, %3, %4]")
        .arg(transform(0,0), 8, 'f', 4).arg(transform(0,1), 8, 'f', 4)
        .arg(transform(0,2), 8, 'f', 4).arg(transform(0,3), 8, 'f', 4);
    qDebug() << QString("[%1, %2, %3, %4]")
        .arg(transform(1,0), 8, 'f', 4).arg(transform(1,1), 8, 'f', 4)
        .arg(transform(1,2), 8, 'f', 4).arg(transform(1,3), 8, 'f', 4);
    qDebug() << QString("[%1, %2, %3, %4]")
        .arg(transform(2,0), 8, 'f', 4).arg(transform(2,1), 8, 'f', 4)
        .arg(transform(2,2), 8, 'f', 4).arg(transform(2,3), 8, 'f', 4);
    qDebug() << QString("[%1, %2, %3, %4]")
        .arg(transform(3,0), 8, 'f', 4).arg(transform(3,1), 8, 'f', 4)
        .arg(transform(3,2), 8, 'f', 4).arg(transform(3,3), 8, 'f', 4);

    // Compute landmark RMSE
    double rmse = LandmarkRegistration::computeLandmarkRMSE(scanPts, refPts, transform);
    qDebug() << "Landmark RMSE:" << rmse << "mm";

    // Apply transform to scan
    LandmarkRegistration::applyTransform(m_scan->mesh, transform);
    m_scan->transform = transform * m_scan->transform;

    // Clear landmark spheres (they don't move with the mesh)
    m_scanView->clearPickActors();
    m_refView->clearPickActors();

    // Disable pick mode after alignment
    m_scanView->setPickMode(false);
    m_refView->setPickMode(false);

    // Update scan visualization (don't reset camera so user sees the mesh move)
    m_scanView->setMesh(m_scan, false);

    m_alignmentComputed = true;

    // Auto-compute and show difference map
    updateDifferenceView();

    m_statusLabel->setText(QString("Aligned (RMSE: %1 mm). Click 'Run ICP' to refine, or 'Accept' if satisfied.")
        .arg(rmse, 0, 'f', 3));

    updateButtonStates();
}

void ErrandResolutionDialog::onRunICP()
{
    if (!m_scan || !m_refData) return;

    m_statusLabel->setText("Running ICP refinement...");
    QApplication::processEvents();

    ICPRegistration::Params params;
    params.maxIterations = 100;
    params.convergenceRms = 1e-4;

    auto result = ICPRegistration::align(*m_scan, *m_refData, params);

    if (result.converged) {
        ICPRegistration::applyTransform(*m_scan, result.transform);
        m_scan->transform = result.transform * m_scan->transform;
        m_icpRefined = true;

        // Update scan view (don't reset camera)
        m_scanView->setMesh(m_scan, false);

        // Update difference view
        updateDifferenceView();

        m_statusLabel->setText(QString("ICP converged. RMS: %1 mm, Coverage: %2%")
            .arg(m_newRMS, 0, 'f', 3)
            .arg(m_correctedMetrics.coverageRate, 0, 'f', 1));
    } else {
        m_statusLabel->setText(QString("ICP did not converge after %1 iterations (RMS: %2 mm)")
            .arg(result.iterations).arg(result.finalRms, 0, 'f', 4));
    }

    updateButtonStates();
}

void ErrandResolutionDialog::onShowDifferenceMap()
{
    updateDifferenceView();
}

void ErrandResolutionDialog::updateDifferenceView()
{
    if (!m_scan || !m_refData) return;

    // Compute distances
    LandmarkRegistration::recomputeDistances(m_scan, *m_refData);

    // Compute metrics
    m_correctedMetrics = LandmarkRegistration::recomputeMetrics(m_scan);
    m_newRMS = m_correctedMetrics.rmsDistance;

    // Show difference map in the third view
    m_diffView->setMesh(m_scan);
    m_diffView->showDistanceMap(m_scan, -0.5, 0.5);

    m_diffLabel->setText(QString("RMS: %1 mm | Coverage: %2%")
        .arg(m_newRMS, 0, 'f', 3)
        .arg(m_correctedMetrics.coverageRate, 0, 'f', 1));
}

void ErrandResolutionDialog::onAccept()
{
    qDebug() << "=== onAccept ===";

    if (!m_scan || !m_refData) {
        reject();
        return;
    }

    // Ensure distances are computed
    if (!m_scan->distanceComputed) {
        LandmarkRegistration::recomputeDistances(m_scan, *m_refData);
        m_correctedMetrics = LandmarkRegistration::recomputeMetrics(m_scan);
        m_newRMS = m_correctedMetrics.rmsDistance;
    }

    qDebug() << "Accepting with RMS:" << m_newRMS;
    m_accepted = true;
    accept();
}

void ErrandResolutionDialog::onReject()
{
    m_accepted = false;
    reject();
}

} // namespace DentScanBatch
