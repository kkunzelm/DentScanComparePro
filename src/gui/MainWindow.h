// SPDX-License-Identifier: GPL-2.0-or-later
// SPDX-FileCopyrightText: 2024 Prof. Dr. Karl-Heinz Kunzelmann

#pragma once

#include "../core/Mesh.h"
#include "../core/ToothSegmentation.h"
#include "../config/StudyConfig.h"
#include "../config/ROIConfig.h"
#include "../qc/QCReviewWidget.h"
#include <QMainWindow>
#include <QTabWidget>
#include <QListWidget>
#include <QProgressBar>
#include <QLabel>
#include <QLineEdit>
#include <QPushButton>
#include <QTextEdit>
#include <QFutureWatcher>
#include <memory>
#include <vector>
#include <Eigen/Core>

class QTreeWidget;
class QTreeWidgetItem;
class VTKMeshWidget;

namespace DentScanBatch {
class BatchRunner;
}

/**
 * Main application window for DentScanComparePro.
 * Provides GUI for:
 * - Loading and viewing study configurations
 * - Editing ROI templates interactively
 * - Running batch processing with progress monitoring
 * - Viewing results
 */
class MainWindow : public QMainWindow
{
    Q_OBJECT
public:
    explicit MainWindow(QWidget* parent = nullptr);
    ~MainWindow() override;

private slots:
    // Study configuration
    void browseStudyFile();
    void browseDataRoot();
    void browseOutputDir();
    void browseMaskedOutputDir();
    void browseExternalRef();
    void browseROITemplate();
    void loadStudyConfig();

    // ROI Template Editor
    void browseTemplateScan();
    void loadTemplateScan();
    void onBBoxChanged();
    void onZPlaneChanged();
    void onBrushModeToggled(bool active);
    void onPointPicked(double x, double y, double z);
    void saveROITemplate();
    void loadROITemplate();

    // Tooth Segmentation
    void onSeedPickModeToggled(bool active);
    void onSeedPicked(double x, double y, double z);
    void runSegmentation();
    void runSegmentationAuto();  // Auto-runs after each seed change
    void clearSeeds();
    void undoLastSeed();

    // Occlusal plane picking
    void onOcclusPlanePickModeToggled(bool active);
    void onOcclusPlanePicked(double x, double y, double z);
    void clearOcclusPlanePoints();

    // Brush for tooth mask editing
    void onBrushEditToothMaskToggled(bool active);

    // Batch processing
    void runBatch();
    void cancelBatch();
    void onBatchProgress(int current, int total, const QString& message);
    void onBatchFinished();
    void onBatchError(const QString& error);

    // Results
    void refreshResults();

    // QC Review
    void loadQCData();
    void generateDifferenceImages();
    void rebuildMetricsFromTransforms();
    void onQCViewRequested(const QString& scanId, const QString& imagePath);
    void onQCReregisterRequested(const QString& scanId);
    void onQCBatchReregisterRequested(const QStringList& scanIds);
    void onQCStatusChanged();
    void onQCSaveRequested();

private:
    void setupUI();
    void setupMenuBar();
    void setupConfigTab();
    void setupROITab();
    void setupBatchTab();
    void setupResultsTab();
    void setupQCReviewTab();

    void loadSettings();
    void saveSettings();

    void updateStudyOverview();
    void updateROIVisualization();
    void enableBatchControls(bool enabled);
    bool regenerateDifferenceImageForScan(const QString& scanId, const QString& outputDir);

    // === Data ===
    DentScanBatch::StudyConfig m_studyConfig;
    bool m_configLoaded = false;

    // Template scan for ROI editing
    std::shared_ptr<ScanData> m_templateScan;
    DentScanBatch::ROIConfig m_currentROI;
    std::vector<std::array<double, 3>> m_brushPoints;
    bool m_brushIncludeMode = true;

    // Tooth segmentation
    std::vector<std::array<double, 3>> m_seedPoints;
    std::vector<bool> m_toothMask;
    ToothSegmentation::Params m_segParams;
    bool m_seedPickMode = false;

    // Occlusal plane picking (3 points)
    std::vector<std::array<double, 3>> m_occlusPlanePoints;
    bool m_occlusPlanePickMode = false;
    Eigen::Vector3d m_occlusPlaneNormal{0, 0, 1};
    Eigen::Vector3d m_occlusPlaneOrigin{0, 0, 0};
    bool m_occlusPlaneValid = false;

    // Brush mode for tooth mask editing
    bool m_brushEditToothMask = false;

    // Batch runner
    std::unique_ptr<DentScanBatch::BatchRunner> m_batchRunner;
    QFutureWatcher<bool>* m_batchWatcher = nullptr;
    bool m_batchRunning = false;

    // === UI Elements ===
    QTabWidget* m_tabs = nullptr;
    QLabel* m_statusLabel = nullptr;

    // Tab 1: Configuration
    QWidget* m_configTab = nullptr;
    QLineEdit* m_studyPathEdit = nullptr;
    QLineEdit* m_dataRootEdit = nullptr;
    QLineEdit* m_outputDirEdit = nullptr;
    QLineEdit* m_maskedOutputDirEdit = nullptr;
    QLineEdit* m_externalRefEdit = nullptr;
    QLineEdit* m_roiTemplateEdit = nullptr;
    class QCheckBox* m_scansPreAlignedChk = nullptr;
    class QCheckBox* m_scansNormalizedChk = nullptr;
    QTreeWidget* m_studyTree = nullptr;
    QLabel* m_studyNameLabel = nullptr;
    QLabel* m_scannerCountLabel = nullptr;
    QLabel* m_groupCountLabel = nullptr;
    QLabel* m_fileCountLabel = nullptr;

    // Tab 2: ROI Template Editor
    QWidget* m_roiTab = nullptr;
    VTKMeshWidget* m_roiMeshWidget = nullptr;
    QLineEdit* m_templatePathEdit = nullptr;

    // Bounding box controls
    class QDoubleSpinBox* m_bboxMinX = nullptr;
    class QDoubleSpinBox* m_bboxMinY = nullptr;
    class QDoubleSpinBox* m_bboxMinZ = nullptr;
    class QDoubleSpinBox* m_bboxMaxX = nullptr;
    class QDoubleSpinBox* m_bboxMaxY = nullptr;
    class QDoubleSpinBox* m_bboxMaxZ = nullptr;
    class QCheckBox* m_bboxActiveChk = nullptr;

    // Z-plane controls
    class QDoubleSpinBox* m_zAboveSpin = nullptr;
    class QDoubleSpinBox* m_zBelowSpin = nullptr;
    class QCheckBox* m_zPlaneActiveChk = nullptr;
    QPushButton* m_pickOcclusPlaneBtn = nullptr;
    QPushButton* m_clearOcclusPlaneBtn = nullptr;
    QLabel* m_occlusPlaneStatusLabel = nullptr;

    // Brush controls
    QPushButton* m_brushIncludeBtn = nullptr;
    QPushButton* m_brushExcludeBtn = nullptr;
    class QDoubleSpinBox* m_brushRadiusSpin = nullptr;
    QPushButton* m_clearBrushBtn = nullptr;
    class QCheckBox* m_brushEditToothMaskChk = nullptr;

    // Sigma clipping
    class QDoubleSpinBox* m_sigmaSpin = nullptr;

    // Tooth segmentation controls
    QPushButton* m_seedPickBtn = nullptr;
    QPushButton* m_runSegBtn = nullptr;
    QPushButton* m_clearSeedsBtn = nullptr;
    QPushButton* m_undoSeedBtn = nullptr;
    QLabel* m_seedCountLabel = nullptr;
    class QDoubleSpinBox* m_segGeodesicSpin = nullptr;
    class QDoubleSpinBox* m_segCreaseSpin = nullptr;
    class QDoubleSpinBox* m_segCurvSpin = nullptr;
    class QDoubleSpinBox* m_segRepulsionSpin = nullptr;
    class QCheckBox* m_useToothMaskChk = nullptr;

    // Tab 3: Batch Processing
    QWidget* m_batchTab = nullptr;
    QPushButton* m_runBatchBtn = nullptr;
    QPushButton* m_cancelBatchBtn = nullptr;
    class QCheckBox* m_useMaskedICPChk = nullptr;
    QProgressBar* m_overallProgress = nullptr;
    QProgressBar* m_groupProgress = nullptr;
    QLabel* m_currentGroupLabel = nullptr;
    QLabel* m_currentStepLabel = nullptr;
    QTextEdit* m_batchLog = nullptr;

    // Tab 4: Results
    QWidget* m_resultsTab = nullptr;
    QListWidget* m_outputFilesList = nullptr;
    QTextEdit* m_filePreview = nullptr;

    // Tab 5: QC Review
    QWidget* m_qcTab = nullptr;
    DentScanBatch::QCReviewWidget* m_qcReviewWidget = nullptr;
    class QCheckBox* m_qcApplyROIChk = nullptr;
    QPushButton* m_rebuildMetricsBtn = nullptr;
};
