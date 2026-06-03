#include "MainWindow.h"
#include "../visualization/VTKMeshWidget.h"
#include "../batch/BatchRunner.h"
#include "../core/STLReader.h"
#include "../core/CurvatureAnalysis.h"
#include "../core/ToothSegmentation.h"
#include <Eigen/Geometry>
#include <QMenuBar>
#include <QMenu>
#include <QAction>
#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QFormLayout>
#include <QGroupBox>
#include <QSplitter>
#include <QFileDialog>
#include <QMessageBox>
#include <QTreeWidget>
#include <QTreeWidgetItem>
#include <QHeaderView>
#include <QDoubleSpinBox>
#include <QCheckBox>
#include <QtConcurrent>
#include <QApplication>
#include <QJsonDocument>
#include <QJsonObject>
#include <QJsonArray>
#include <QFile>

MainWindow::MainWindow(QWidget* parent)
    : QMainWindow(parent)
{
    setWindowTitle("DentScanComparePro - Scanner Accuracy Evaluation");
    setMinimumSize(1200, 800);

    setupUI();
    setupMenuBar();

    m_statusLabel->setText("Ready. Load a study configuration to begin.");
}

MainWindow::~MainWindow()
{
    if (m_batchRunner && m_batchRunning) {
        m_batchRunner->cancel();
    }
}

void MainWindow::setupUI()
{
    auto* centralWidget = new QWidget(this);
    setCentralWidget(centralWidget);

    auto* mainLayout = new QVBoxLayout(centralWidget);

    // Tab widget
    m_tabs = new QTabWidget(this);
    mainLayout->addWidget(m_tabs, 1);

    // Status bar
    m_statusLabel = new QLabel(this);
    m_statusLabel->setFrameStyle(QFrame::StyledPanel | QFrame::Sunken);
    mainLayout->addWidget(m_statusLabel);

    // Create tabs
    setupConfigTab();
    setupROITab();
    setupBatchTab();
    setupResultsTab();
}

void MainWindow::setupMenuBar()
{
    auto* fileMenu = menuBar()->addMenu("&File");

    auto* openStudyAction = new QAction("&Open Study Configuration...", this);
    openStudyAction->setShortcut(QKeySequence::Open);
    connect(openStudyAction, &QAction::triggered, this, &MainWindow::browseStudyFile);
    fileMenu->addAction(openStudyAction);

    fileMenu->addSeparator();

    auto* exitAction = new QAction("E&xit", this);
    exitAction->setShortcut(QKeySequence::Quit);
    connect(exitAction, &QAction::triggered, this, &QMainWindow::close);
    fileMenu->addAction(exitAction);

    auto* helpMenu = menuBar()->addMenu("&Help");

    auto* aboutAction = new QAction("&About DentScanComparePro", this);
    connect(aboutAction, &QAction::triggered, this, [this]() {
        QMessageBox::about(this, "About DentScanComparePro",
            "DentScanComparePro v1.0\n\n"
            "Automated batch evaluation of dental intraoral scanner accuracy.\n"
            "Computes ISO 5725/12836-compliant trueness and precision metrics.\n\n"
            "Prof. Dr. Karl-Heinz Kunzelmann");
    });
    helpMenu->addAction(aboutAction);
}

void MainWindow::setupConfigTab()
{
    m_configTab = new QWidget();
    m_tabs->addTab(m_configTab, "Study Configuration");

    auto* layout = new QVBoxLayout(m_configTab);

    // File paths group
    auto* pathsGroup = new QGroupBox("File Paths");
    auto* pathsLayout = new QFormLayout(pathsGroup);

    // Study file
    auto* studyRow = new QHBoxLayout();
    m_studyPathEdit = new QLineEdit();
    m_studyPathEdit->setPlaceholderText("Path to study.json configuration file");
    auto* browseStudyBtn = new QPushButton("Browse...");
    connect(browseStudyBtn, &QPushButton::clicked, this, &MainWindow::browseStudyFile);
    studyRow->addWidget(m_studyPathEdit, 1);
    studyRow->addWidget(browseStudyBtn);
    pathsLayout->addRow("Study Config:", studyRow);

    // Data root
    auto* dataRow = new QHBoxLayout();
    m_dataRootEdit = new QLineEdit();
    m_dataRootEdit->setPlaceholderText("Root directory containing scanner folders");
    auto* browseDataBtn = new QPushButton("Browse...");
    connect(browseDataBtn, &QPushButton::clicked, this, &MainWindow::browseDataRoot);
    dataRow->addWidget(m_dataRootEdit, 1);
    dataRow->addWidget(browseDataBtn);
    pathsLayout->addRow("Data Root:", dataRow);

    // Output directory
    auto* outputRow = new QHBoxLayout();
    m_outputDirEdit = new QLineEdit();
    m_outputDirEdit->setText("./results");
    auto* browseOutputBtn = new QPushButton("Browse...");
    connect(browseOutputBtn, &QPushButton::clicked, this, &MainWindow::browseOutputDir);
    outputRow->addWidget(m_outputDirEdit, 1);
    outputRow->addWidget(browseOutputBtn);
    pathsLayout->addRow("Output Dir:", outputRow);

    // Load button
    auto* loadBtn = new QPushButton("Load Configuration");
    connect(loadBtn, &QPushButton::clicked, this, &MainWindow::loadStudyConfig);
    pathsLayout->addRow("", loadBtn);

    layout->addWidget(pathsGroup);

    // Study overview group
    auto* overviewGroup = new QGroupBox("Study Overview");
    auto* overviewLayout = new QVBoxLayout(overviewGroup);

    auto* statsLayout = new QHBoxLayout();
    m_studyNameLabel = new QLabel("Study: (not loaded)");
    m_scannerCountLabel = new QLabel("Scanners: -");
    m_groupCountLabel = new QLabel("Groups: -");
    m_fileCountLabel = new QLabel("Files: -");
    statsLayout->addWidget(m_studyNameLabel);
    statsLayout->addStretch();
    statsLayout->addWidget(m_scannerCountLabel);
    statsLayout->addWidget(m_groupCountLabel);
    statsLayout->addWidget(m_fileCountLabel);
    overviewLayout->addLayout(statsLayout);

    // Tree view for scanners and groups
    m_studyTree = new QTreeWidget();
    m_studyTree->setHeaderLabels({"Item", "Details"});
    m_studyTree->header()->setStretchLastSection(true);
    m_studyTree->setAlternatingRowColors(true);
    overviewLayout->addWidget(m_studyTree, 1);

    layout->addWidget(overviewGroup, 1);
}

void MainWindow::setupROITab()
{
    m_roiTab = new QWidget();
    m_tabs->addTab(m_roiTab, "ROI Template Editor");

    auto* layout = new QHBoxLayout(m_roiTab);

    // Left side: 3D view
    auto* leftPanel = new QWidget();
    auto* leftLayout = new QVBoxLayout(leftPanel);
    leftLayout->setContentsMargins(0, 0, 0, 0);

    // Template scan loading
    auto* loadRow = new QHBoxLayout();
    m_templatePathEdit = new QLineEdit();
    m_templatePathEdit->setReadOnly(true);
    m_templatePathEdit->setPlaceholderText("Load a template scan for ROI editing");
    auto* loadTemplateBtn = new QPushButton("Load Template Scan...");
    connect(loadTemplateBtn, &QPushButton::clicked, this, &MainWindow::loadTemplateScan);
    loadRow->addWidget(m_templatePathEdit, 1);
    loadRow->addWidget(loadTemplateBtn);
    leftLayout->addLayout(loadRow);

    // VTK mesh widget
    m_roiMeshWidget = new VTKMeshWidget(this);
    m_roiMeshWidget->setMinimumSize(600, 500);
    connect(m_roiMeshWidget, &VTKMeshWidget::pointPicked,
            this, &MainWindow::onPointPicked);
    leftLayout->addWidget(m_roiMeshWidget, 1);

    layout->addWidget(leftPanel, 1);

    // Right side: ROI controls
    auto* rightPanel = new QWidget();
    rightPanel->setMaximumWidth(350);
    auto* rightLayout = new QVBoxLayout(rightPanel);

    // Bounding Box group
    auto* bboxGroup = new QGroupBox("Bounding Box");
    auto* bboxLayout = new QFormLayout(bboxGroup);

    m_bboxActiveChk = new QCheckBox("Active");
    bboxLayout->addRow("", m_bboxActiveChk);

    m_bboxMinX = new QDoubleSpinBox();
    m_bboxMinX->setRange(-500, 500);
    m_bboxMinX->setDecimals(1);
    m_bboxMinX->setSuffix(" mm");
    bboxLayout->addRow("Min X:", m_bboxMinX);

    m_bboxMinY = new QDoubleSpinBox();
    m_bboxMinY->setRange(-500, 500);
    m_bboxMinY->setDecimals(1);
    m_bboxMinY->setSuffix(" mm");
    bboxLayout->addRow("Min Y:", m_bboxMinY);

    m_bboxMinZ = new QDoubleSpinBox();
    m_bboxMinZ->setRange(-500, 500);
    m_bboxMinZ->setDecimals(1);
    m_bboxMinZ->setSuffix(" mm");
    bboxLayout->addRow("Min Z:", m_bboxMinZ);

    m_bboxMaxX = new QDoubleSpinBox();
    m_bboxMaxX->setRange(-500, 500);
    m_bboxMaxX->setDecimals(1);
    m_bboxMaxX->setSuffix(" mm");
    bboxLayout->addRow("Max X:", m_bboxMaxX);

    m_bboxMaxY = new QDoubleSpinBox();
    m_bboxMaxY->setRange(-500, 500);
    m_bboxMaxY->setDecimals(1);
    m_bboxMaxY->setSuffix(" mm");
    bboxLayout->addRow("Max Y:", m_bboxMaxY);

    m_bboxMaxZ = new QDoubleSpinBox();
    m_bboxMaxZ->setRange(-500, 500);
    m_bboxMaxZ->setDecimals(1);
    m_bboxMaxZ->setSuffix(" mm");
    bboxLayout->addRow("Max Z:", m_bboxMaxZ);

    connect(m_bboxActiveChk, &QCheckBox::toggled, this, &MainWindow::onBBoxChanged);
    connect(m_bboxMinX, QOverload<double>::of(&QDoubleSpinBox::valueChanged), this, &MainWindow::onBBoxChanged);
    connect(m_bboxMinY, QOverload<double>::of(&QDoubleSpinBox::valueChanged), this, &MainWindow::onBBoxChanged);
    connect(m_bboxMinZ, QOverload<double>::of(&QDoubleSpinBox::valueChanged), this, &MainWindow::onBBoxChanged);
    connect(m_bboxMaxX, QOverload<double>::of(&QDoubleSpinBox::valueChanged), this, &MainWindow::onBBoxChanged);
    connect(m_bboxMaxY, QOverload<double>::of(&QDoubleSpinBox::valueChanged), this, &MainWindow::onBBoxChanged);
    connect(m_bboxMaxZ, QOverload<double>::of(&QDoubleSpinBox::valueChanged), this, &MainWindow::onBBoxChanged);

    rightLayout->addWidget(bboxGroup);

    // Z-Plane Slab group
    auto* zPlaneGroup = new QGroupBox("Z-Plane Slab (Occlusal Region)");
    auto* zPlaneLayout = new QVBoxLayout(zPlaneGroup);

    m_zPlaneActiveChk = new QCheckBox("Active");
    m_zPlaneActiveChk->setChecked(true);
    zPlaneLayout->addWidget(m_zPlaneActiveChk);

    // Occlusal plane picking
    auto* occlusRow = new QHBoxLayout();
    m_pickOcclusPlaneBtn = new QPushButton("Pick Plane (3 pts)");
    m_pickOcclusPlaneBtn->setCheckable(true);
    m_pickOcclusPlaneBtn->setToolTip("Click 3 points on occlusal surface to define the plane");
    connect(m_pickOcclusPlaneBtn, &QPushButton::toggled, this, &MainWindow::onOcclusPlanePickModeToggled);
    occlusRow->addWidget(m_pickOcclusPlaneBtn);

    m_clearOcclusPlaneBtn = new QPushButton("Clear");
    m_clearOcclusPlaneBtn->setToolTip("Clear plane points and use max-Z");
    connect(m_clearOcclusPlaneBtn, &QPushButton::clicked, this, &MainWindow::clearOcclusPlanePoints);
    occlusRow->addWidget(m_clearOcclusPlaneBtn);
    zPlaneLayout->addLayout(occlusRow);

    m_occlusPlaneStatusLabel = new QLabel("Plane: auto (max-Z)");
    m_occlusPlaneStatusLabel->setStyleSheet("font-size: 10px; color: #888;");
    zPlaneLayout->addWidget(m_occlusPlaneStatusLabel);

    auto* zPlaneParamsLayout = new QFormLayout();
    m_zAboveSpin = new QDoubleSpinBox();
    m_zAboveSpin->setRange(0, 50);
    m_zAboveSpin->setValue(2.0);
    m_zAboveSpin->setDecimals(1);
    m_zAboveSpin->setSuffix(" mm");
    zPlaneParamsLayout->addRow("Above Occlusal:", m_zAboveSpin);

    m_zBelowSpin = new QDoubleSpinBox();
    m_zBelowSpin->setRange(0, 50);
    m_zBelowSpin->setValue(12.0);
    m_zBelowSpin->setDecimals(1);
    m_zBelowSpin->setSuffix(" mm");
    zPlaneParamsLayout->addRow("Below Occlusal:", m_zBelowSpin);
    zPlaneLayout->addLayout(zPlaneParamsLayout);

    connect(m_zPlaneActiveChk, &QCheckBox::toggled, this, &MainWindow::onZPlaneChanged);
    connect(m_zAboveSpin, QOverload<double>::of(&QDoubleSpinBox::valueChanged), this, &MainWindow::onZPlaneChanged);
    connect(m_zBelowSpin, QOverload<double>::of(&QDoubleSpinBox::valueChanged), this, &MainWindow::onZPlaneChanged);

    rightLayout->addWidget(zPlaneGroup);

    // Brush Tool group
    auto* brushGroup = new QGroupBox("Brush Tool");
    auto* brushLayout = new QVBoxLayout(brushGroup);

    m_brushEditToothMaskChk = new QCheckBox("Edit tooth mask (not ROI zones)");
    m_brushEditToothMaskChk->setToolTip("When checked, brush directly adds/removes vertices from tooth mask.\nWhen unchecked, brush creates ROI zones.");
    connect(m_brushEditToothMaskChk, &QCheckBox::toggled, this, &MainWindow::onBrushEditToothMaskToggled);
    brushLayout->addWidget(m_brushEditToothMaskChk);

    auto* brushBtnRow = new QHBoxLayout();
    m_brushIncludeBtn = new QPushButton("Include");
    m_brushIncludeBtn->setCheckable(true);
    m_brushIncludeBtn->setToolTip("Click to add vertices to ROI/tooth mask");
    m_brushExcludeBtn = new QPushButton("Exclude");
    m_brushExcludeBtn->setCheckable(true);
    m_brushExcludeBtn->setToolTip("Click to remove vertices from ROI/tooth mask");
    brushBtnRow->addWidget(m_brushIncludeBtn);
    brushBtnRow->addWidget(m_brushExcludeBtn);
    brushLayout->addLayout(brushBtnRow);

    connect(m_brushIncludeBtn, &QPushButton::toggled, this, [this](bool checked) {
        if (checked) {
            m_brushExcludeBtn->setChecked(false);
            m_brushIncludeMode = true;
            onBrushModeToggled(true);
        }
    });
    connect(m_brushExcludeBtn, &QPushButton::toggled, this, [this](bool checked) {
        if (checked) {
            m_brushIncludeBtn->setChecked(false);
            m_brushIncludeMode = false;
            onBrushModeToggled(true);
        }
    });

    auto* brushRadiusLayout = new QFormLayout();
    m_brushRadiusSpin = new QDoubleSpinBox();
    m_brushRadiusSpin->setRange(0.5, 20);
    m_brushRadiusSpin->setValue(3.0);
    m_brushRadiusSpin->setDecimals(1);
    m_brushRadiusSpin->setSuffix(" mm");
    brushRadiusLayout->addRow("Radius:", m_brushRadiusSpin);
    brushLayout->addLayout(brushRadiusLayout);

    m_clearBrushBtn = new QPushButton("Clear All Brush Zones");
    connect(m_clearBrushBtn, &QPushButton::clicked, this, [this]() {
        m_brushPoints.clear();
        m_currentROI.brushZones.clear();
        updateROIVisualization();
    });
    brushLayout->addWidget(m_clearBrushBtn);

    rightLayout->addWidget(brushGroup);

    // Sigma Clipping group
    auto* sigmaGroup = new QGroupBox("Outlier Removal (σ-clipping)");
    auto* sigmaLayout = new QFormLayout(sigmaGroup);

    m_sigmaSpin = new QDoubleSpinBox();
    m_sigmaSpin->setRange(0, 10);
    m_sigmaSpin->setValue(3.0);
    m_sigmaSpin->setDecimals(1);
    m_sigmaSpin->setSpecialValueText("Disabled");
    m_sigmaSpin->setToolTip(
        "Applied during METRIC COMPUTATION (not segmentation).\n"
        "Removes vertices whose signed distance to reference is more than\n"
        "σ standard deviations from the mean. This filters measurement\n"
        "outliers caused by noise or registration errors.\n\n"
        "Set to 0 to disable outlier removal.");
    sigmaLayout->addRow("Sigma Threshold:", m_sigmaSpin);

    rightLayout->addWidget(sigmaGroup);

    // Tooth Segmentation group
    auto* segGroup = new QGroupBox("Tooth Segmentation");
    auto* segLayout = new QVBoxLayout(segGroup);

    // Seed picking controls
    auto* seedBtnRow = new QHBoxLayout();
    m_seedPickBtn = new QPushButton("Pick Seeds");
    m_seedPickBtn->setCheckable(true);
    m_seedPickBtn->setToolTip("Click on tooth cusps to place seed points");
    connect(m_seedPickBtn, &QPushButton::toggled, this, &MainWindow::onSeedPickModeToggled);
    seedBtnRow->addWidget(m_seedPickBtn);

    m_undoSeedBtn = new QPushButton("Undo");
    m_undoSeedBtn->setToolTip("Remove last seed point");
    connect(m_undoSeedBtn, &QPushButton::clicked, this, &MainWindow::undoLastSeed);
    seedBtnRow->addWidget(m_undoSeedBtn);

    m_clearSeedsBtn = new QPushButton("Clear");
    m_clearSeedsBtn->setToolTip("Remove all seed points");
    connect(m_clearSeedsBtn, &QPushButton::clicked, this, &MainWindow::clearSeeds);
    seedBtnRow->addWidget(m_clearSeedsBtn);
    segLayout->addLayout(seedBtnRow);

    m_seedCountLabel = new QLabel("Seeds: 0");
    segLayout->addWidget(m_seedCountLabel);

    // Segmentation parameters
    auto* segParamsLayout = new QFormLayout();

    m_segGeodesicSpin = new QDoubleSpinBox();
    m_segGeodesicSpin->setRange(5, 20);
    m_segGeodesicSpin->setValue(12.0);
    m_segGeodesicSpin->setDecimals(1);
    m_segGeodesicSpin->setSuffix(" mm");
    m_segGeodesicSpin->setToolTip("Maximum geodesic distance from seed (tooth size limit)");
    segParamsLayout->addRow("Max Geodesic:", m_segGeodesicSpin);

    m_segCreaseSpin = new QDoubleSpinBox();
    m_segCreaseSpin->setRange(20, 90);
    m_segCreaseSpin->setValue(50.0);
    m_segCreaseSpin->setDecimals(0);
    m_segCreaseSpin->setSuffix(" deg");
    m_segCreaseSpin->setToolTip("Maximum crease angle at CEJ boundary");
    segParamsLayout->addRow("Max Crease:", m_segCreaseSpin);

    m_segCurvSpin = new QDoubleSpinBox();
    m_segCurvSpin->setRange(-10, 0);
    m_segCurvSpin->setValue(-4.0);
    m_segCurvSpin->setDecimals(1);
    m_segCurvSpin->setSuffix(" 1/mm");
    m_segCurvSpin->setToolTip("Minimum mean curvature (gingival sulcus threshold)");
    segParamsLayout->addRow("Min Curvature:", m_segCurvSpin);

    m_segRepulsionSpin = new QDoubleSpinBox();
    m_segRepulsionSpin->setRange(0, 1);
    m_segRepulsionSpin->setValue(0.1);
    m_segRepulsionSpin->setDecimals(2);
    m_segRepulsionSpin->setToolTip("Curvature repulsion factor (0 = disabled)");
    segParamsLayout->addRow("Repulsion:", m_segRepulsionSpin);

    segLayout->addLayout(segParamsLayout);

    m_runSegBtn = new QPushButton("Run Segmentation");
    m_runSegBtn->setToolTip("Compute tooth mask from seed points");
    connect(m_runSegBtn, &QPushButton::clicked, this, &MainWindow::runSegmentation);
    segLayout->addWidget(m_runSegBtn);

    m_useToothMaskChk = new QCheckBox("Use tooth mask as ROI");
    m_useToothMaskChk->setToolTip("When checked, only tooth crown vertices are included in analysis");
    connect(m_useToothMaskChk, &QCheckBox::toggled, this, &MainWindow::updateROIVisualization);
    segLayout->addWidget(m_useToothMaskChk);

    rightLayout->addWidget(segGroup);

    // Save/Load buttons
    auto* ioGroup = new QGroupBox("ROI Template I/O");
    auto* ioLayout = new QHBoxLayout(ioGroup);

    auto* saveBtn = new QPushButton("Save Template...");
    connect(saveBtn, &QPushButton::clicked, this, &MainWindow::saveROITemplate);
    ioLayout->addWidget(saveBtn);

    auto* loadBtn = new QPushButton("Load Template...");
    connect(loadBtn, &QPushButton::clicked, this, &MainWindow::loadROITemplate);
    ioLayout->addWidget(loadBtn);

    rightLayout->addWidget(ioGroup);

    rightLayout->addStretch();
    layout->addWidget(rightPanel);
}

void MainWindow::setupBatchTab()
{
    m_batchTab = new QWidget();
    m_tabs->addTab(m_batchTab, "Batch Processing");

    auto* layout = new QVBoxLayout(m_batchTab);

    // Control buttons
    auto* controlRow = new QHBoxLayout();
    m_runBatchBtn = new QPushButton("Run Batch Processing");
    m_runBatchBtn->setEnabled(false);
    connect(m_runBatchBtn, &QPushButton::clicked, this, &MainWindow::runBatch);
    controlRow->addWidget(m_runBatchBtn);

    m_cancelBatchBtn = new QPushButton("Cancel");
    m_cancelBatchBtn->setEnabled(false);
    connect(m_cancelBatchBtn, &QPushButton::clicked, this, &MainWindow::cancelBatch);
    controlRow->addWidget(m_cancelBatchBtn);

    controlRow->addStretch();
    layout->addLayout(controlRow);

    // Progress bars
    auto* progressGroup = new QGroupBox("Progress");
    auto* progressLayout = new QFormLayout(progressGroup);

    m_currentGroupLabel = new QLabel("(not running)");
    progressLayout->addRow("Current Group:", m_currentGroupLabel);

    m_groupProgress = new QProgressBar();
    m_groupProgress->setRange(0, 100);
    progressLayout->addRow("Group Progress:", m_groupProgress);

    m_overallProgress = new QProgressBar();
    m_overallProgress->setRange(0, 100);
    progressLayout->addRow("Overall Progress:", m_overallProgress);

    m_currentStepLabel = new QLabel("");
    progressLayout->addRow("Current Step:", m_currentStepLabel);

    layout->addWidget(progressGroup);

    // Log output
    auto* logGroup = new QGroupBox("Processing Log");
    auto* logLayout = new QVBoxLayout(logGroup);

    m_batchLog = new QTextEdit();
    m_batchLog->setReadOnly(true);
    m_batchLog->setFontFamily("monospace");
    logLayout->addWidget(m_batchLog);

    layout->addWidget(logGroup, 1);
}

void MainWindow::setupResultsTab()
{
    m_resultsTab = new QWidget();
    m_tabs->addTab(m_resultsTab, "Results");

    auto* layout = new QHBoxLayout(m_resultsTab);

    // Left: file list
    auto* leftPanel = new QWidget();
    leftPanel->setMaximumWidth(300);
    auto* leftLayout = new QVBoxLayout(leftPanel);
    leftLayout->setContentsMargins(0, 0, 0, 0);

    auto* refreshBtn = new QPushButton("Refresh File List");
    connect(refreshBtn, &QPushButton::clicked, this, &MainWindow::refreshResults);
    leftLayout->addWidget(refreshBtn);

    m_outputFilesList = new QListWidget();
    connect(m_outputFilesList, &QListWidget::currentItemChanged, this,
            [this](QListWidgetItem* current, QListWidgetItem*) {
        if (!current) {
            m_filePreview->clear();
            return;
        }
        QString filePath = current->data(Qt::UserRole).toString();
        QFile file(filePath);
        if (file.open(QIODevice::ReadOnly | QIODevice::Text)) {
            // Read first 10000 chars for preview
            m_filePreview->setPlainText(QString::fromUtf8(file.read(10000)));
        }
    });
    leftLayout->addWidget(m_outputFilesList, 1);

    layout->addWidget(leftPanel);

    // Right: file preview
    m_filePreview = new QTextEdit();
    m_filePreview->setReadOnly(true);
    m_filePreview->setFontFamily("monospace");
    layout->addWidget(m_filePreview, 1);
}

// === Slot implementations ===

void MainWindow::browseStudyFile()
{
    QString path = QFileDialog::getOpenFileName(this,
        "Select Study Configuration",
        QString(),
        "JSON Files (*.json);;YAML Files (*.yaml *.yml);;All Files (*)");

    if (!path.isEmpty()) {
        m_studyPathEdit->setText(path);
    }
}

void MainWindow::browseDataRoot()
{
    QString path = QFileDialog::getExistingDirectory(this,
        "Select Data Root Directory",
        QString(),
        QFileDialog::ShowDirsOnly);

    if (!path.isEmpty()) {
        m_dataRootEdit->setText(path);
    }
}

void MainWindow::browseOutputDir()
{
    QString path = QFileDialog::getExistingDirectory(this,
        "Select Output Directory",
        QString(),
        QFileDialog::ShowDirsOnly);

    if (!path.isEmpty()) {
        m_outputDirEdit->setText(path);
    }
}

void MainWindow::loadStudyConfig()
{
    QString studyPath = m_studyPathEdit->text();
    if (studyPath.isEmpty()) {
        QMessageBox::warning(this, "Error", "Please select a study configuration file.");
        return;
    }

    try {
        m_studyConfig = DentScanBatch::StudyConfig::loadFromFile(studyPath);
        m_configLoaded = true;
        updateStudyOverview();
        m_runBatchBtn->setEnabled(true);
        m_statusLabel->setText("Configuration loaded: " + m_studyConfig.name);
    } catch (const std::exception& e) {
        QMessageBox::critical(this, "Load Error",
            QString("Failed to load study configuration:\n%1").arg(e.what()));
        m_configLoaded = false;
        m_runBatchBtn->setEnabled(false);
    }
}

void MainWindow::updateStudyOverview()
{
    m_studyNameLabel->setText("Study: " + m_studyConfig.name);
    m_scannerCountLabel->setText(QString("Scanners: %1").arg(m_studyConfig.scanners.size()));
    m_groupCountLabel->setText(QString("Groups: %1").arg(m_studyConfig.groups.size()));

    // Populate tree
    m_studyTree->clear();

    // Scanners branch
    auto* scannersItem = new QTreeWidgetItem(m_studyTree, {"Scanners", ""});
    scannersItem->setExpanded(true);
    for (const auto& scanner : m_studyConfig.scanners) {
        auto* item = new QTreeWidgetItem(scannersItem, {scanner.id, ""});
        for (const auto& pattern : scanner.patterns) {
            new QTreeWidgetItem(item, {"Pattern", pattern});
        }
    }

    // Groups branch
    auto* groupsItem = new QTreeWidgetItem(m_studyTree, {"Groups (SKD Levels)", ""});
    groupsItem->setExpanded(true);
    for (const auto& group : m_studyConfig.groups) {
        QString details = QString("SKD %1 mm").arg(group.skd_mm);
        auto* item = new QTreeWidgetItem(groupsItem, {group.id, details});
        for (const auto& pattern : group.filePatterns) {
            new QTreeWidgetItem(item, {"Pattern", pattern});
        }
    }

    m_studyTree->resizeColumnToContents(0);
}

void MainWindow::loadTemplateScan()
{
    QString path = QFileDialog::getOpenFileName(this,
        "Select Template STL File",
        QString(),
        "STL Files (*.stl);;All Files (*)");

    if (path.isEmpty()) return;

    std::string errorMsg;
    m_templateScan = STLReader::read(path.toStdString(), errorMsg);

    if (!m_templateScan) {
        QMessageBox::critical(this, "Load Error",
            QString("Failed to load STL file:\n%1").arg(QString::fromStdString(errorMsg)));
        return;
    }

    m_templatePathEdit->setText(path);
    m_roiMeshWidget->setMesh(m_templateScan);
    m_roiMeshWidget->resetCamera();

    // Initialize bounding box from mesh bounds
    bool first = true;
    for (auto v : m_templateScan->mesh.vertices()) {
        const auto& p = m_templateScan->mesh.point(v);
        if (first) {
            m_templateScan->boundsMin = {p.x(), p.y(), p.z()};
            m_templateScan->boundsMax = {p.x(), p.y(), p.z()};
            first = false;
        } else {
            m_templateScan->boundsMin[0] = std::min(m_templateScan->boundsMin[0], p.x());
            m_templateScan->boundsMin[1] = std::min(m_templateScan->boundsMin[1], p.y());
            m_templateScan->boundsMin[2] = std::min(m_templateScan->boundsMin[2], p.z());
            m_templateScan->boundsMax[0] = std::max(m_templateScan->boundsMax[0], p.x());
            m_templateScan->boundsMax[1] = std::max(m_templateScan->boundsMax[1], p.y());
            m_templateScan->boundsMax[2] = std::max(m_templateScan->boundsMax[2], p.z());
        }
    }

    // Set spin boxes to mesh bounds
    m_bboxMinX->setValue(m_templateScan->boundsMin[0]);
    m_bboxMinY->setValue(m_templateScan->boundsMin[1]);
    m_bboxMinZ->setValue(m_templateScan->boundsMin[2]);
    m_bboxMaxX->setValue(m_templateScan->boundsMax[0]);
    m_bboxMaxY->setValue(m_templateScan->boundsMax[1]);
    m_bboxMaxZ->setValue(m_templateScan->boundsMax[2]);

    m_statusLabel->setText("Template scan loaded: " + path);
}

void MainWindow::onBBoxChanged()
{
    m_currentROI.bbox.active = m_bboxActiveChk->isChecked();
    m_currentROI.bbox.min = {m_bboxMinX->value(), m_bboxMinY->value(), m_bboxMinZ->value()};
    m_currentROI.bbox.max = {m_bboxMaxX->value(), m_bboxMaxY->value(), m_bboxMaxZ->value()};

    // Show/hide bounding box wireframe
    if (m_currentROI.bbox.active && m_templateScan) {
        m_roiMeshWidget->showBoundingBox(m_currentROI.bbox.min, m_currentROI.bbox.max);
    } else {
        m_roiMeshWidget->hideBoundingBox();
    }

    updateROIVisualization();
}

void MainWindow::onZPlaneChanged()
{
    m_currentROI.zPlane.active = m_zPlaneActiveChk->isChecked();
    m_currentROI.zPlane.above_mm = m_zAboveSpin->value();
    m_currentROI.zPlane.below_mm = m_zBelowSpin->value();
    updateROIVisualization();
}

void MainWindow::onBrushModeToggled(bool active)
{
    m_roiMeshWidget->setPickMode(active);
    if (!active) {
        m_brushIncludeBtn->setChecked(false);
        m_brushExcludeBtn->setChecked(false);
    }
}

void MainWindow::onPointPicked(double x, double y, double z)
{
    // Handle occlusal plane picking mode
    if (m_occlusPlanePickMode) {
        onOcclusPlanePicked(x, y, z);
        return;
    }

    // Handle seed picking mode for tooth segmentation
    if (m_seedPickMode) {
        onSeedPicked(x, y, z);
        return;
    }

    // Handle brush mode for ROI editing or tooth mask editing
    if (!m_brushIncludeBtn->isChecked() && !m_brushExcludeBtn->isChecked()) {
        return;
    }

    // Check if we're editing tooth mask directly
    if (m_brushEditToothMask && !m_toothMask.empty() && m_templateScan) {
        // Modify tooth mask directly
        double radius = m_brushRadiusSpin->value();
        double radius2 = radius * radius;

        std::size_t idx = 0;
        std::size_t modified = 0;
        for (auto v : m_templateScan->mesh.vertices()) {
            const auto& p = m_templateScan->mesh.point(v);
            double dx = p.x() - x;
            double dy = p.y() - y;
            double dz = p.z() - z;
            if (dx*dx + dy*dy + dz*dz <= radius2) {
                m_toothMask[idx] = m_brushIncludeMode;
                modified++;
            }
            idx++;
        }

        m_statusLabel->setText(QString("Brush: %1 %2 vertices")
            .arg(m_brushIncludeMode ? "included" : "excluded")
            .arg(modified));

        updateROIVisualization();
        return;
    }

    // Add brush zone for ROI editing
    DentScanBatch::BrushZone zone;
    zone.center = {x, y, z};
    zone.radius_mm = m_brushRadiusSpin->value();
    zone.include = m_brushIncludeMode;

    m_currentROI.brushZones.push_back(zone);
    m_brushPoints.push_back({x, y, z});

    // Show pick sphere (combine with seed points)
    std::vector<std::array<double, 3>> allPoints = m_brushPoints;
    allPoints.insert(allPoints.end(), m_seedPoints.begin(), m_seedPoints.end());
    m_roiMeshWidget->showPickSpheres(allPoints);
    updateROIVisualization();
}

void MainWindow::saveROITemplate()
{
    QString path = QFileDialog::getSaveFileName(this,
        "Save ROI Template",
        "roi_template.json",
        "JSON Files (*.json)");

    if (path.isEmpty()) return;

    QJsonObject root;

    // Bounding box
    QJsonObject bbox;
    bbox["active"] = m_currentROI.bbox.active;
    QJsonArray bboxMin, bboxMax;
    for (int i = 0; i < 3; i++) {
        bboxMin.append(m_currentROI.bbox.min[i]);
        bboxMax.append(m_currentROI.bbox.max[i]);
    }
    bbox["min"] = bboxMin;
    bbox["max"] = bboxMax;
    root["bbox"] = bbox;

    // Z-plane
    QJsonObject zPlane;
    zPlane["active"] = m_currentROI.zPlane.active;
    zPlane["above_mm"] = m_currentROI.zPlane.above_mm;
    zPlane["below_mm"] = m_currentROI.zPlane.below_mm;
    root["z_plane"] = zPlane;

    // Brush zones
    QJsonArray brushArray;
    for (const auto& zone : m_currentROI.brushZones) {
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

    // Sigma
    root["outlier_sigma"] = m_sigmaSpin->value();

    // Tooth segmentation
    QJsonObject segObj;
    segObj["max_geodesic_mm"] = m_segGeodesicSpin->value();
    segObj["max_crease_deg"] = m_segCreaseSpin->value();
    segObj["min_curvature"] = m_segCurvSpin->value();
    segObj["repulsion"] = m_segRepulsionSpin->value();
    segObj["use_tooth_mask"] = m_useToothMaskChk->isChecked();

    // Seed points
    QJsonArray seedArray;
    for (const auto& pt : m_seedPoints) {
        QJsonArray seedPt;
        seedPt.append(pt[0]);
        seedPt.append(pt[1]);
        seedPt.append(pt[2]);
        seedArray.append(seedPt);
    }
    segObj["seeds"] = seedArray;

    root["tooth_segmentation"] = segObj;

    QFile file(path);
    if (file.open(QIODevice::WriteOnly)) {
        file.write(QJsonDocument(root).toJson(QJsonDocument::Indented));
        m_statusLabel->setText("ROI template saved: " + path);
    } else {
        QMessageBox::critical(this, "Save Error", "Failed to save ROI template.");
    }
}

void MainWindow::loadROITemplate()
{
    QString path = QFileDialog::getOpenFileName(this,
        "Load ROI Template",
        QString(),
        "JSON Files (*.json)");

    if (path.isEmpty()) return;

    QFile file(path);
    if (!file.open(QIODevice::ReadOnly)) {
        QMessageBox::critical(this, "Load Error", "Failed to open ROI template file.");
        return;
    }

    QJsonParseError parseError;
    QJsonDocument doc = QJsonDocument::fromJson(file.readAll(), &parseError);
    if (parseError.error != QJsonParseError::NoError) {
        QMessageBox::critical(this, "Parse Error",
            QString("Failed to parse ROI template:\n%1").arg(parseError.errorString()));
        return;
    }

    QJsonObject root = doc.object();

    // Load bounding box
    if (root.contains("bbox")) {
        QJsonObject bbox = root["bbox"].toObject();
        m_currentROI.bbox.active = bbox["active"].toBool();
        QJsonArray bboxMin = bbox["min"].toArray();
        QJsonArray bboxMax = bbox["max"].toArray();
        for (int i = 0; i < 3 && i < bboxMin.size(); i++) {
            m_currentROI.bbox.min[i] = bboxMin[i].toDouble();
            m_currentROI.bbox.max[i] = bboxMax[i].toDouble();
        }

        m_bboxActiveChk->setChecked(m_currentROI.bbox.active);
        m_bboxMinX->setValue(m_currentROI.bbox.min[0]);
        m_bboxMinY->setValue(m_currentROI.bbox.min[1]);
        m_bboxMinZ->setValue(m_currentROI.bbox.min[2]);
        m_bboxMaxX->setValue(m_currentROI.bbox.max[0]);
        m_bboxMaxY->setValue(m_currentROI.bbox.max[1]);
        m_bboxMaxZ->setValue(m_currentROI.bbox.max[2]);
    }

    // Load Z-plane
    if (root.contains("z_plane")) {
        QJsonObject zPlane = root["z_plane"].toObject();
        m_currentROI.zPlane.active = zPlane["active"].toBool();
        m_currentROI.zPlane.above_mm = zPlane["above_mm"].toDouble();
        m_currentROI.zPlane.below_mm = zPlane["below_mm"].toDouble();

        m_zPlaneActiveChk->setChecked(m_currentROI.zPlane.active);
        m_zAboveSpin->setValue(m_currentROI.zPlane.above_mm);
        m_zBelowSpin->setValue(m_currentROI.zPlane.below_mm);
    }

    // Load brush zones
    m_currentROI.brushZones.clear();
    m_brushPoints.clear();
    if (root.contains("brush_zones")) {
        QJsonArray brushArray = root["brush_zones"].toArray();
        for (const auto& brushVal : brushArray) {
            QJsonObject brush = brushVal.toObject();
            DentScanBatch::BrushZone zone;
            QJsonArray center = brush["center"].toArray();
            for (int i = 0; i < 3 && i < center.size(); i++) {
                zone.center[i] = center[i].toDouble();
            }
            zone.radius_mm = brush["radius_mm"].toDouble();
            zone.include = brush["include"].toBool();
            m_currentROI.brushZones.push_back(zone);
            m_brushPoints.push_back(zone.center);
        }
    }

    // Load sigma
    if (root.contains("outlier_sigma")) {
        m_currentROI.outlierSigma = root["outlier_sigma"].toDouble();
        m_sigmaSpin->setValue(m_currentROI.outlierSigma);
    }

    // Load tooth segmentation
    m_seedPoints.clear();
    m_toothMask.clear();
    if (root.contains("tooth_segmentation")) {
        QJsonObject segObj = root["tooth_segmentation"].toObject();

        if (segObj.contains("max_geodesic_mm"))
            m_segGeodesicSpin->setValue(segObj["max_geodesic_mm"].toDouble());
        if (segObj.contains("max_crease_deg"))
            m_segCreaseSpin->setValue(segObj["max_crease_deg"].toDouble());
        if (segObj.contains("min_curvature"))
            m_segCurvSpin->setValue(segObj["min_curvature"].toDouble());
        if (segObj.contains("repulsion"))
            m_segRepulsionSpin->setValue(segObj["repulsion"].toDouble());
        if (segObj.contains("use_tooth_mask"))
            m_useToothMaskChk->setChecked(segObj["use_tooth_mask"].toBool());

        // Load seed points
        if (segObj.contains("seeds")) {
            QJsonArray seedArray = segObj["seeds"].toArray();
            for (const auto& seedVal : seedArray) {
                QJsonArray seedPt = seedVal.toArray();
                if (seedPt.size() >= 3) {
                    m_seedPoints.push_back({
                        seedPt[0].toDouble(),
                        seedPt[1].toDouble(),
                        seedPt[2].toDouble()
                    });
                }
            }
        }

        m_seedCountLabel->setText(QString("Seeds: %1").arg(m_seedPoints.size()));
    }

    // Show all pick spheres (brush + seed points)
    std::vector<std::array<double, 3>> allPoints = m_brushPoints;
    allPoints.insert(allPoints.end(), m_seedPoints.begin(), m_seedPoints.end());
    m_roiMeshWidget->showPickSpheres(allPoints);
    updateROIVisualization();
    m_statusLabel->setText("ROI template loaded: " + path);
}

void MainWindow::updateROIVisualization()
{
    if (!m_templateScan) return;

    // Determine occlusal plane origin and normal
    double z_occlusal;
    Eigen::Vector3d planeNormal;
    Eigen::Vector3d planeOrigin;

    if (m_occlusPlaneValid) {
        // Use user-picked plane
        planeNormal = m_occlusPlaneNormal;
        planeOrigin = m_occlusPlaneOrigin;
        z_occlusal = m_occlusPlaneOrigin.z();  // Use plane origin Z for Z-slab
    } else {
        // Find occlusal Z (max Z)
        z_occlusal = std::numeric_limits<double>::lowest();
        for (auto v : m_templateScan->mesh.vertices()) {
            z_occlusal = std::max(z_occlusal, m_templateScan->mesh.point(v).z());
        }
        planeNormal = Eigen::Vector3d(0, 0, 1);
        planeOrigin = Eigen::Vector3d(0, 0, z_occlusal);
    }

    // Create mask based on current ROI
    std::vector<bool> mask(m_templateScan->mesh.number_of_vertices(), true);
    std::size_t idx = 0;
    for (auto v : m_templateScan->mesh.vertices()) {
        const auto& p = m_templateScan->mesh.point(v);
        mask[idx] = m_currentROI.isInROI(p.x(), p.y(), p.z(), z_occlusal);
        idx++;
    }

    // Apply tooth mask if available and enabled
    if (m_useToothMaskChk && m_useToothMaskChk->isChecked() &&
        !m_toothMask.empty() && m_toothMask.size() == mask.size()) {
        for (std::size_t i = 0; i < mask.size(); i++) {
            mask[i] = mask[i] && m_toothMask[i];
        }
    }

    // Display as segmentation (in ROI vs out of ROI)
    m_roiMeshWidget->showToothSegmentation(m_templateScan, mask);

    // Show Z-plane visualization if active
    if (m_currentROI.zPlane.active) {
        m_roiMeshWidget->showOcclusalPlane(planeNormal, planeOrigin,
            m_currentROI.zPlane.above_mm, m_currentROI.zPlane.below_mm);
        m_roiMeshWidget->setPlanesVisible(true);
    } else {
        m_roiMeshWidget->setPlanesVisible(false);
    }
}

void MainWindow::runBatch()
{
    if (!m_configLoaded) {
        QMessageBox::warning(this, "Error", "Please load a study configuration first.");
        return;
    }

    QString dataRoot = m_dataRootEdit->text();
    QString outputDir = m_outputDirEdit->text();

    if (dataRoot.isEmpty()) {
        QMessageBox::warning(this, "Error", "Please specify the data root directory.");
        return;
    }

    if (outputDir.isEmpty()) {
        outputDir = "./results";
    }

    m_batchLog->clear();
    m_batchLog->append("Starting batch processing...");
    m_batchLog->append("Data root: " + dataRoot);
    m_batchLog->append("Output dir: " + outputDir);
    m_batchLog->append("");

    enableBatchControls(false);
    m_batchRunning = true;

    // Create batch runner
    m_batchRunner = std::make_unique<DentScanBatch::BatchRunner>();
    m_batchRunner->setVerbose(true);

    // Run in background thread
    m_batchWatcher = new QFutureWatcher<bool>(this);
    connect(m_batchWatcher, &QFutureWatcher<bool>::finished, this, &MainWindow::onBatchFinished);

    auto future = QtConcurrent::run([this, dataRoot, outputDir]() {
        return m_batchRunner->run(m_studyConfig, dataRoot, outputDir);
    });

    m_batchWatcher->setFuture(future);
}

void MainWindow::cancelBatch()
{
    if (m_batchRunner) {
        m_batchLog->append("\n*** Cancelling batch processing... ***");
        m_batchRunner->cancel();
    }
}

void MainWindow::onBatchProgress(int current, int total, const QString& message)
{
    m_overallProgress->setMaximum(total);
    m_overallProgress->setValue(current);
    m_currentStepLabel->setText(message);
    m_batchLog->append(message);
}

void MainWindow::onBatchFinished()
{
    bool success = m_batchWatcher->result();
    m_batchRunning = false;
    enableBatchControls(true);

    if (success) {
        m_batchLog->append("\n=== Batch processing completed successfully ===");
        m_statusLabel->setText("Batch processing completed successfully.");
        QMessageBox::information(this, "Success", "Batch processing completed successfully.");
    } else {
        m_batchLog->append("\n=== Batch processing failed or was cancelled ===");
        m_statusLabel->setText("Batch processing failed.");
    }

    // Refresh results tab
    refreshResults();

    delete m_batchWatcher;
    m_batchWatcher = nullptr;
}

void MainWindow::onBatchError(const QString& error)
{
    m_batchLog->append("ERROR: " + error);
}

void MainWindow::enableBatchControls(bool enabled)
{
    m_runBatchBtn->setEnabled(enabled && m_configLoaded);
    m_cancelBatchBtn->setEnabled(!enabled);
    m_studyPathEdit->setEnabled(enabled);
    m_dataRootEdit->setEnabled(enabled);
    m_outputDirEdit->setEnabled(enabled);
}

void MainWindow::refreshResults()
{
    m_outputFilesList->clear();
    m_filePreview->clear();

    QString outputDir = m_outputDirEdit->text();
    if (outputDir.isEmpty()) {
        outputDir = "./results";
    }

    QDir dir(outputDir);
    if (!dir.exists()) {
        return;
    }

    QStringList filters;
    filters << "*.csv" << "*.json" << "*.txt";
    QFileInfoList files = dir.entryInfoList(filters, QDir::Files, QDir::Name);

    for (const auto& fileInfo : files) {
        auto* item = new QListWidgetItem(fileInfo.fileName());
        item->setData(Qt::UserRole, fileInfo.absoluteFilePath());
        m_outputFilesList->addItem(item);
    }
}

// === Tooth Segmentation Slots ===

void MainWindow::onSeedPickModeToggled(bool active)
{
    m_seedPickMode = active;
    m_roiMeshWidget->setPickMode(active);

    // Disable brush mode when seed pick mode is active
    if (active) {
        m_brushIncludeBtn->setChecked(false);
        m_brushExcludeBtn->setChecked(false);
    }
}

void MainWindow::onSeedPicked(double x, double y, double z)
{
    m_seedPoints.push_back({x, y, z});
    m_seedCountLabel->setText(QString("Seeds: %1").arg(m_seedPoints.size()));

    // Show seed spheres (combine with brush points for visualization)
    std::vector<std::array<double, 3>> allPoints = m_brushPoints;
    allPoints.insert(allPoints.end(), m_seedPoints.begin(), m_seedPoints.end());
    m_roiMeshWidget->showPickSpheres(allPoints);

    m_statusLabel->setText(QString("Seed point added at (%.2f, %.2f, %.2f). Total: %1")
        .arg(x).arg(y).arg(z).arg(m_seedPoints.size()));
}

void MainWindow::runSegmentation()
{
    if (!m_templateScan) {
        QMessageBox::warning(this, "Error", "Please load a template scan first.");
        return;
    }

    if (m_seedPoints.empty()) {
        QMessageBox::warning(this, "Error", "Please place at least one seed point on a tooth.");
        return;
    }

    m_statusLabel->setText("Computing curvature...");
    QApplication::processEvents();

    // Compute curvature if not already done
    if (!m_templateScan->curvatureComputed) {
        CurvatureAnalysis::compute(*m_templateScan);
    }

    m_statusLabel->setText("Running tooth segmentation...");
    QApplication::processEvents();

    // Update segmentation parameters from UI
    m_segParams.maxGeodesicMm = m_segGeodesicSpin->value();
    m_segParams.maxCreaseAngleDeg = m_segCreaseSpin->value();
    m_segParams.minMeanCurvature = m_segCurvSpin->value();
    m_segParams.curvatureRepulsion = m_segRepulsionSpin->value();

    // Run segmentation
    m_toothMask = ToothSegmentation::segmentFromPoints(*m_templateScan, m_seedPoints, m_segParams);

    // Count segmented vertices
    std::size_t toothCount = std::count(m_toothMask.begin(), m_toothMask.end(), true);

    m_statusLabel->setText(QString("Segmentation complete: %1 / %2 vertices in tooth crown")
        .arg(toothCount).arg(m_toothMask.size()));

    // Update visualization to show tooth mask
    updateROIVisualization();
}

void MainWindow::clearSeeds()
{
    m_seedPoints.clear();
    m_toothMask.clear();
    m_seedCountLabel->setText("Seeds: 0");

    // Update sphere visualization (only brush points remain)
    m_roiMeshWidget->showPickSpheres(m_brushPoints);
    updateROIVisualization();

    m_statusLabel->setText("Seed points cleared.");
}

void MainWindow::undoLastSeed()
{
    if (m_seedPoints.empty()) {
        return;
    }

    m_seedPoints.pop_back();
    m_seedCountLabel->setText(QString("Seeds: %1").arg(m_seedPoints.size()));

    // Update sphere visualization
    std::vector<std::array<double, 3>> allPoints = m_brushPoints;
    allPoints.insert(allPoints.end(), m_seedPoints.begin(), m_seedPoints.end());
    m_roiMeshWidget->showPickSpheres(allPoints);

    // Clear tooth mask since seeds changed
    m_toothMask.clear();

    m_statusLabel->setText(QString("Seed removed. Total: %1").arg(m_seedPoints.size()));
}

// === Occlusal Plane Picking ===

void MainWindow::onOcclusPlanePickModeToggled(bool active)
{
    m_occlusPlanePickMode = active;
    m_roiMeshWidget->setPickMode(active);

    // Disable other pick modes
    if (active) {
        m_seedPickBtn->setChecked(false);
        m_brushIncludeBtn->setChecked(false);
        m_brushExcludeBtn->setChecked(false);
        m_statusLabel->setText("Click 3 points on the occlusal surface to define the plane.");
    }
}

void MainWindow::onOcclusPlanePicked(double x, double y, double z)
{
    m_occlusPlanePoints.push_back({x, y, z});

    // Show spheres for plane points
    m_roiMeshWidget->showPickSpheres(m_occlusPlanePoints);

    m_occlusPlaneStatusLabel->setText(QString("Plane: %1/3 points").arg(m_occlusPlanePoints.size()));

    if (m_occlusPlanePoints.size() >= 3) {
        // Fit plane to 3 points
        Eigen::Vector3d p0(m_occlusPlanePoints[0][0], m_occlusPlanePoints[0][1], m_occlusPlanePoints[0][2]);
        Eigen::Vector3d p1(m_occlusPlanePoints[1][0], m_occlusPlanePoints[1][1], m_occlusPlanePoints[1][2]);
        Eigen::Vector3d p2(m_occlusPlanePoints[2][0], m_occlusPlanePoints[2][1], m_occlusPlanePoints[2][2]);

        // Compute normal as cross product
        Eigen::Vector3d v1 = p1 - p0;
        Eigen::Vector3d v2 = p2 - p0;
        m_occlusPlaneNormal = v1.cross(v2).normalized();

        // Ensure normal points upward (positive Z component)
        if (m_occlusPlaneNormal.z() < 0) {
            m_occlusPlaneNormal = -m_occlusPlaneNormal;
        }

        // Origin is centroid of the 3 points
        m_occlusPlaneOrigin = (p0 + p1 + p2) / 3.0;

        m_occlusPlaneValid = true;

        m_occlusPlaneStatusLabel->setText(QString("Plane: defined at Z=%.1f").arg(m_occlusPlaneOrigin.z()));

        // Exit pick mode
        m_pickOcclusPlaneBtn->setChecked(false);
        m_statusLabel->setText("Occlusal plane defined from 3 points.");

        updateROIVisualization();
    }
}

void MainWindow::clearOcclusPlanePoints()
{
    m_occlusPlanePoints.clear();
    m_occlusPlaneValid = false;
    m_occlusPlaneStatusLabel->setText("Plane: auto (max-Z)");

    // Clear spheres (but keep brush/seed points)
    std::vector<std::array<double, 3>> allPoints = m_brushPoints;
    allPoints.insert(allPoints.end(), m_seedPoints.begin(), m_seedPoints.end());
    m_roiMeshWidget->showPickSpheres(allPoints);

    updateROIVisualization();
    m_statusLabel->setText("Occlusal plane cleared. Using max-Z.");
}

void MainWindow::onBrushEditToothMaskToggled(bool active)
{
    m_brushEditToothMask = active;

    if (active && m_toothMask.empty() && m_templateScan) {
        // Initialize tooth mask to all false if not already computed
        m_toothMask.resize(m_templateScan->mesh.number_of_vertices(), false);
        m_statusLabel->setText("Tooth mask initialized. Use brush to add/remove vertices.");
    } else if (active) {
        m_statusLabel->setText("Brush now edits tooth mask directly.");
    } else {
        m_statusLabel->setText("Brush now creates ROI zones.");
    }
}
