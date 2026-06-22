// SPDX-License-Identifier: GPL-2.0-or-later
// SPDX-FileCopyrightText: 2024 Prof. Dr. Karl-Heinz Kunzelmann

#include "MainWindow.h"
#include "../visualization/VTKMeshWidget.h"
#include "../batch/BatchRunner.h"
#include "../core/STLReader.h"
#include "../core/CurvatureAnalysis.h"
#include "../core/ToothSegmentation.h"
#include "../core/DistanceField.h"
#include "../qc/QCReviewWidget.h"
#include "../qc/QCExporter.h"
#include "../qc/ErrandResolutionDialog.h"
#include "../qc/AlignmentQCDialog.h"
#include <Eigen/Geometry>
#include <QDir>
#include <QProgressDialog>
#include <QTimer>
#include <QDebug>
#include <algorithm>
#include <QMenuBar>
#include <QMenu>
#include <QAction>
#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QFormLayout>
#include <QGroupBox>
#include <QSplitter>
#include <QScrollArea>
#include <QFileDialog>
#include <QMessageBox>
#include <QTreeWidget>
#include <QTreeWidgetItem>
#include <QHeaderView>
#include <QDoubleSpinBox>
#include <QCheckBox>
#include <QComboBox>
#include <QtConcurrent>
#include <QApplication>
#include <QJsonDocument>
#include <QJsonObject>
#include <QJsonArray>
#include <QFile>
#include <QFileInfo>
#include <QSettings>
#include <unordered_map>
#include <limits>

MainWindow::MainWindow(QWidget* parent)
    : QMainWindow(parent)
{
    setWindowTitle("DentScanComparePro - Scanner Accuracy Evaluation (Prof. K.-H. Kunzelmann)");
    setMinimumSize(1200, 800);

    setupUI();
    setupMenuBar();
    loadSettings();

    // Auto-load study config if path was previously saved (so group selector is populated
    // immediately and saveROITemplate() can update the JSON without requiring a manual click)
    QTimer::singleShot(0, this, [this]() {
        QString studyPath = m_studyPathEdit->text();
        if (!studyPath.isEmpty() && QFile::exists(studyPath)) {
            try {
                m_studyConfig = DentScanBatch::StudyConfig::loadFromFile(studyPath);
                m_configLoaded = true;
                updateStudyOverview();
                m_runBatchBtn->setEnabled(true);
                m_statusLabel->setText("Configuration loaded: " + m_studyConfig.name);
            } catch (...) {
                m_statusLabel->setText("Ready. Load a study configuration to begin.");
            }
        } else {
            m_statusLabel->setText("Ready. Load a study configuration to begin.");
        }
    });
}

MainWindow::~MainWindow()
{
    saveSettings();
    if (m_batchRunner && m_batchRunning) {
        m_batchRunner->cancel();
    }
}

void MainWindow::loadSettings()
{
    QSettings settings("DentScanComparePro", "DentScanComparePro");

    // Block signals to avoid triggering saveSettings during load
    QSignalBlocker b1(m_studyPathEdit);
    QSignalBlocker b2(m_dataRootEdit);
    QSignalBlocker b3(m_outputDirEdit);
    QSignalBlocker b4(m_templatePathEdit);
    QSignalBlocker b5(m_externalRefEdit);
    QSignalBlocker b6(m_scansPreAlignedChk);
    QSignalBlocker b10(m_scansNormalizedChk);
    QSignalBlocker b11(m_icpTrimFractionSpin);
    QSignalBlocker b12(m_icpHierarchyChk);
    QSignalBlocker b7(m_roiTemplateEdit);
    QSignalBlocker b8(m_useMaskedICPChk);
    QSignalBlocker b9(m_maskedOutputDirEdit);

    m_studyPathEdit->setText(settings.value("paths/studyFile").toString());
    m_dataRootEdit->setText(settings.value("paths/dataRoot").toString());

    QString outputDir = settings.value("paths/outputDir").toString();
    if (!outputDir.isEmpty()) {
        m_outputDirEdit->setText(outputDir);
    }

    m_maskedOutputDirEdit->setText(settings.value("paths/maskedOutputDir").toString());
    m_externalRefEdit->setText(settings.value("paths/externalRef").toString());
    m_roiTemplateEdit->setText(settings.value("paths/roiTemplate").toString());
    m_scansPreAlignedChk->setChecked(settings.value("options/scansPreAligned", false).toBool());
    m_scansNormalizedChk->setChecked(settings.value("options/scansNormalized", true).toBool());
    m_icpTrimFractionSpin->setValue(settings.value("options/icpTrimFraction", 1.0).toDouble());
    m_icpHierarchyChk->setChecked(settings.value("options/icpHierarchy", false).toBool());
    m_useMaskedICPChk->setChecked(settings.value("options/useMaskedICP", true).toBool());

    m_templatePathEdit->setText(settings.value("paths/templateScan").toString());
}

void MainWindow::saveSettings()
{
    QSettings settings("DentScanComparePro", "DentScanComparePro");
    settings.setValue("paths/studyFile", m_studyPathEdit->text());
    settings.setValue("paths/dataRoot", m_dataRootEdit->text());
    settings.setValue("paths/outputDir", m_outputDirEdit->text());
    settings.setValue("paths/maskedOutputDir", m_maskedOutputDirEdit->text());
    settings.setValue("paths/externalRef", m_externalRefEdit->text());
    settings.setValue("paths/roiTemplate", m_roiTemplateEdit->text());
    settings.setValue("options/scansPreAligned", m_scansPreAlignedChk->isChecked());
    settings.setValue("options/scansNormalized", m_scansNormalizedChk->isChecked());
    settings.setValue("options/icpTrimFraction", m_icpTrimFractionSpin->value());
    settings.setValue("options/icpHierarchy", m_icpHierarchyChk->isChecked());
    settings.setValue("options/useMaskedICP", m_useMaskedICPChk->isChecked());
    settings.setValue("paths/templateScan", m_templatePathEdit->text());
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
    setupQCReviewTab();
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
        QMessageBox aboutBox(this);
        aboutBox.setWindowTitle("About DentScanComparePro");
        aboutBox.setTextFormat(Qt::RichText);
        aboutBox.setText(
            "<h3>DentScanComparePro v1.0</h3>"
            "<p>Automated batch evaluation of dental intraoral scanner accuracy.<br>"
            "Computes ISO 5725/12836-compliant trueness and precision metrics.</p>"
            "<p><b>Prof. Dr. Karl-Heinz Kunzelmann</b><br>"
            "<a href=\"https://www.kunzelmann.de\">www.kunzelmann.de</a></p>");
        aboutBox.setTextInteractionFlags(Qt::TextBrowserInteraction);
        aboutBox.exec();
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
    connect(m_studyPathEdit, &QLineEdit::textChanged, this, &MainWindow::saveSettings);
    studyRow->addWidget(m_studyPathEdit, 1);
    studyRow->addWidget(browseStudyBtn);
    pathsLayout->addRow("Study Config:", studyRow);

    // Data root
    auto* dataRow = new QHBoxLayout();
    m_dataRootEdit = new QLineEdit();
    m_dataRootEdit->setPlaceholderText("Root directory containing scanner folders");
    auto* browseDataBtn = new QPushButton("Browse...");
    connect(browseDataBtn, &QPushButton::clicked, this, &MainWindow::browseDataRoot);
    connect(m_dataRootEdit, &QLineEdit::textChanged, this, &MainWindow::saveSettings);
    dataRow->addWidget(m_dataRootEdit, 1);
    dataRow->addWidget(browseDataBtn);
    pathsLayout->addRow("Data Root:", dataRow);

    // Output directory
    auto* outputRow = new QHBoxLayout();
    m_outputDirEdit = new QLineEdit();
    m_outputDirEdit->setText("./results");
    auto* browseOutputBtn = new QPushButton("Browse...");
    connect(browseOutputBtn, &QPushButton::clicked, this, &MainWindow::browseOutputDir);
    connect(m_outputDirEdit, &QLineEdit::textChanged, this, &MainWindow::saveSettings);
    outputRow->addWidget(m_outputDirEdit, 1);
    outputRow->addWidget(browseOutputBtn);
    pathsLayout->addRow("Output Dir:", outputRow);

    // Masked ICP output directory (optional)
    auto* maskedOutputRow = new QHBoxLayout();
    m_maskedOutputDirEdit = new QLineEdit();
    m_maskedOutputDirEdit->setPlaceholderText("Optional: separate output for masked ICP (uses Output Dir if empty)");
    auto* browseMaskedOutputBtn = new QPushButton("Browse...");
    connect(browseMaskedOutputBtn, &QPushButton::clicked, this, &MainWindow::browseMaskedOutputDir);
    connect(m_maskedOutputDirEdit, &QLineEdit::textChanged, this, &MainWindow::saveSettings);
    maskedOutputRow->addWidget(m_maskedOutputDirEdit, 1);
    maskedOutputRow->addWidget(browseMaskedOutputBtn);
    pathsLayout->addRow("Masked ICP Output:", maskedOutputRow);

    // External reference (optional)
    auto* refRow = new QHBoxLayout();
    m_externalRefEdit = new QLineEdit();
    m_externalRefEdit->setPlaceholderText("Optional: external reference STL (CAD or lab scanner)");
    auto* browseRefBtn = new QPushButton("Browse...");
    connect(browseRefBtn, &QPushButton::clicked, this, &MainWindow::browseExternalRef);
    connect(m_externalRefEdit, &QLineEdit::textChanged, this, &MainWindow::saveSettings);
    refRow->addWidget(m_externalRefEdit, 1);
    refRow->addWidget(browseRefBtn);
    pathsLayout->addRow("External Ref:", refRow);

    // ROI template (optional)
    auto* roiRow = new QHBoxLayout();
    m_roiTemplateEdit = new QLineEdit();
    m_roiTemplateEdit->setPlaceholderText("Optional: ROI template JSON (from ROI Template Editor)");
    auto* browseROIBtn = new QPushButton("Browse...");
    connect(browseROIBtn, &QPushButton::clicked, this, &MainWindow::browseROITemplate);
    connect(m_roiTemplateEdit, &QLineEdit::textChanged, this, &MainWindow::saveSettings);
    roiRow->addWidget(m_roiTemplateEdit, 1);
    roiRow->addWidget(browseROIBtn);
    pathsLayout->addRow("ROI Template:", roiRow);

    // Pre-aligned checkbox
    m_scansPreAlignedChk = new QCheckBox("Scans are pre-aligned, use JSON transforms for ICP");
    m_scansPreAlignedChk->setToolTip(
        "Check this if scans were coarsely aligned by DentScanAlign and you want to\n"
        "apply the JSON transform files as initialization before ICP refinement.\n"
        "Effect: skips GPA, loads transforms from the alignments directory, uses a\n"
        "tighter ICP correspondence distance (5 mm) since scans are already roughly aligned.\n\n"
        "Note: if your STL files are already normalized (transform baked into geometry),\n"
        "uncheck this and enable 'Scans are normalized' instead.");
    connect(m_scansPreAlignedChk, &QCheckBox::toggled, this, [this](bool checked) {
        if (checked) {
            QSignalBlocker b(m_scansNormalizedChk);
            m_scansNormalizedChk->setChecked(false);
        }
        saveSettings();
    });
    pathsLayout->addRow("", m_scansPreAlignedChk);

    // Normalized scans checkbox (default: on)
    m_scansNormalizedChk = new QCheckBox("Scans are normalized (skip JSON transforms, already applied)");
    m_scansNormalizedChk->setChecked(true);
    m_scansNormalizedChk->setToolTip(
        "Check this if your STL files are DentScanAlign normalized scans, i.e. the\n"
        "alignment transform has already been baked into the mesh geometry.\n"
        "Effect: JSON transform files in the alignments directory are NOT loaded or applied,\n"
        "preventing the transform from being applied a second time.\n\n"
        "If unchecked (and alignments directory is set), raw scans will be transformed\n"
        "using the JSON matrices before ICP refinement.");
    connect(m_scansNormalizedChk, &QCheckBox::toggled, this, [this](bool checked) {
        if (checked) {
            QSignalBlocker b(m_scansPreAlignedChk);
            m_scansPreAlignedChk->setChecked(false);
        }
        saveSettings();
    });
    pathsLayout->addRow("", m_scansNormalizedChk);

    m_icpTrimFractionSpin = new QDoubleSpinBox();
    m_icpTrimFractionSpin->setRange(0.1, 1.0);
    m_icpTrimFractionSpin->setSingleStep(0.05);
    m_icpTrimFractionSpin->setDecimals(2);
    m_icpTrimFractionSpin->setValue(1.0);
    m_icpTrimFractionSpin->setToolTip(
        "TrICP trim fraction: fraction of ICP correspondences kept per iteration.\n"
        "Correspondences are sorted by point-to-plane residual; only the best fraction\n"
        "is used for the rigid solve. Lower values reject more soft-tissue deformation.\n"
        "1.0 = no trimming (default). 0.5 recommended for scans with extensive gingiva.");
    connect(m_icpTrimFractionSpin, QOverload<double>::of(&QDoubleSpinBox::valueChanged),
            this, [this](double) { saveSettings(); });
    pathsLayout->addRow("ICP trim fraction:", m_icpTrimFractionSpin);

    m_icpHierarchyChk = new QCheckBox("ICP resolution hierarchy (coarse-to-fine)");
    m_icpHierarchyChk->setChecked(false);
    m_icpHierarchyChk->setToolTip(
        "Enable coarse-to-fine ICP hierarchy (Xi-2025).\n"
        "Decimates the source mesh to 5% / 20% / 100% of its faces using\n"
        "curvature-weighted QEM (negative-curvature edges at tooth boundaries\n"
        "cost ×10 to collapse, preserving CEJ and developmental groove detail).\n"
        "ICP runs at each level; each coarse result seeds the next finer pass.\n"
        "Useful for scans with large initial misalignment or many soft-tissue artifacts.\n"
        "Adds ~30% runtime overhead. Overrides use_icp_hierarchy in study JSON.");
    connect(m_icpHierarchyChk, &QCheckBox::toggled,
            this, [this](bool) { saveSettings(); });
    pathsLayout->addRow("", m_icpHierarchyChk);

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
    m_templatePathEdit->setPlaceholderText("Path to template STL (or use Browse)");
    auto* loadTemplateBtn = new QPushButton("Browse...");
    connect(loadTemplateBtn, &QPushButton::clicked, this, &MainWindow::browseTemplateScan);
    connect(m_templatePathEdit, &QLineEdit::returnPressed, this, &MainWindow::loadTemplateScan);
    connect(m_templatePathEdit, &QLineEdit::textChanged, this, &MainWindow::saveSettings);
    loadRow->addWidget(m_templatePathEdit, 1);
    loadRow->addWidget(loadTemplateBtn);

    auto* loadScanBtn = new QPushButton("Load");
    connect(loadScanBtn, &QPushButton::clicked, this, &MainWindow::loadTemplateScan);
    loadRow->addWidget(loadScanBtn);
    leftLayout->addLayout(loadRow);

    // VTK mesh widget
    m_roiMeshWidget = new VTKMeshWidget(this);
    m_roiMeshWidget->setMinimumSize(600, 500);
    connect(m_roiMeshWidget, &VTKMeshWidget::pointPicked,
            this, &MainWindow::onPointPicked);
    leftLayout->addWidget(m_roiMeshWidget, 1);

    layout->addWidget(leftPanel, 1);

    // Right side: ROI controls (in scroll area for vertical overflow)
    auto* scrollArea = new QScrollArea();
    scrollArea->setWidgetResizable(true);
    scrollArea->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    scrollArea->setMinimumWidth(380);
    scrollArea->setMaximumWidth(420);

    auto* rightPanel = new QWidget();
    auto* rightLayout = new QVBoxLayout(rightPanel);
    rightLayout->setSpacing(8);


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
    auto* zPlaneGroup = new QGroupBox("Plane Slab (ROI Height)");
    auto* zPlaneLayout = new QVBoxLayout(zPlaneGroup);

    m_zPlaneActiveChk = new QCheckBox("Active");
    m_zPlaneActiveChk->setChecked(false);  // Default to inactive - user must explicitly enable
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
    m_zAboveSpin->setValue(6.0);
    m_zAboveSpin->setDecimals(1);
    m_zAboveSpin->setSuffix(" mm");
    zPlaneParamsLayout->addRow("Offset A:", m_zAboveSpin);

    m_zBelowSpin = new QDoubleSpinBox();
    m_zBelowSpin->setRange(0, 50);
    m_zBelowSpin->setValue(6.0);
    m_zBelowSpin->setDecimals(1);
    m_zBelowSpin->setSuffix(" mm");
    zPlaneParamsLayout->addRow("Offset B:", m_zBelowSpin);
    zPlaneLayout->addLayout(zPlaneParamsLayout);

    connect(m_zPlaneActiveChk, &QCheckBox::toggled, this, &MainWindow::onZPlaneChanged);
    connect(m_zAboveSpin, QOverload<double>::of(&QDoubleSpinBox::valueChanged), this, &MainWindow::onZPlaneChanged);
    connect(m_zBelowSpin, QOverload<double>::of(&QDoubleSpinBox::valueChanged), this, &MainWindow::onZPlaneChanged);

    rightLayout->addWidget(zPlaneGroup);

    // Brush Tool group
    auto* brushGroup = new QGroupBox("Brush Tool");
    auto* brushLayout = new QVBoxLayout(brushGroup);

    m_brushEditToothMaskChk = new QCheckBox("Edit Base Selection");
    m_brushEditToothMaskChk->setToolTip(
        "ROI Selection has two layers:\n\n"
        "1. BASE SELECTION (from Tooth Segmentation):\n"
        "   The initial selection computed by the segmentation algorithm.\n"
        "   Changes here permanently modify the segmentation result.\n\n"
        "2. MANUAL OVERRIDES (ROI Brush Zones):\n"
        "   Temporary include/exclude zones shown in green/red.\n"
        "   These are saved with the ROI template and applied after alignment.\n\n"
        "CHECK this box to edit the Base Selection directly.\n"
        "UNCHECK to create transferable Manual Overrides.");
    connect(m_brushEditToothMaskChk, &QCheckBox::toggled, this, &MainWindow::onBrushEditToothMaskToggled);
    brushLayout->addWidget(m_brushEditToothMaskChk);

    auto* brushBtnRow = new QHBoxLayout();
    m_brushIncludeBtn = new QPushButton("Include");
    m_brushIncludeBtn->setCheckable(true);
    m_brushIncludeBtn->setToolTip(
        "Paint to INCLUDE vertices in the selection.\n"
        "- If 'Edit Base Selection' is checked: adds to Base Selection (ivory)\n"
        "- If unchecked: creates Manual Override zones (green)");
    m_brushExcludeBtn = new QPushButton("Exclude");
    m_brushExcludeBtn->setCheckable(true);
    m_brushExcludeBtn->setToolTip(
        "Paint to EXCLUDE vertices from the selection.\n"
        "- If 'Edit Base Selection' is checked: removes from Base Selection\n"
        "- If unchecked: creates Manual Override zones (red)");
    brushBtnRow->addWidget(m_brushIncludeBtn);
    brushBtnRow->addWidget(m_brushExcludeBtn);
    brushLayout->addLayout(brushBtnRow);

    connect(m_brushIncludeBtn, &QPushButton::toggled, this, [this](bool checked) {
        qDebug() << "Include button toggled:" << checked;
        if (checked) {
            // Disable other pick modes to avoid conflicts
            if (m_seedPickMode) {
                m_seedPickBtn->setChecked(false);
                m_seedPickMode = false;
            }
            if (m_occlusPlanePickMode) {
                m_pickOcclusPlaneBtn->setChecked(false);
                m_occlusPlanePickMode = false;
            }
            QSignalBlocker block(m_brushExcludeBtn);
            m_brushExcludeBtn->setChecked(false);
            m_brushIncludeMode = true;
            qDebug() << "  -> m_brushIncludeMode set to TRUE, seedPickMode disabled";
            onBrushModeToggled(true);
            if (m_brushEditToothMask) {
                m_statusLabel->setText("Brush: ADD to Base Selection (click on mesh)");
            } else {
                m_statusLabel->setText("Brush: INCLUDE zone - green (click on mesh)");
            }
        } else {
            // Button unchecked: deactivate brush cursor and pick mode
            m_roiMeshWidget->setBrushCursorEnabled(false);
            m_roiMeshWidget->setPickMode(false);
        }
    });
    connect(m_brushExcludeBtn, &QPushButton::toggled, this, [this](bool checked) {
        qDebug() << "Exclude button toggled:" << checked;
        if (checked) {
            // Disable other pick modes to avoid conflicts
            if (m_seedPickMode) {
                m_seedPickBtn->setChecked(false);
                m_seedPickMode = false;
            }
            if (m_occlusPlanePickMode) {
                m_pickOcclusPlaneBtn->setChecked(false);
                m_occlusPlanePickMode = false;
            }
            QSignalBlocker block(m_brushIncludeBtn);
            m_brushIncludeBtn->setChecked(false);
            m_brushIncludeMode = false;
            qDebug() << "  -> m_brushIncludeMode set to FALSE, seedPickMode disabled";
            onBrushModeToggled(true);
            if (m_brushEditToothMask) {
                m_statusLabel->setText("Brush: REMOVE from Base Selection (click on mesh)");
            } else {
                m_statusLabel->setText("Brush: EXCLUDE zone - red (click on mesh)");
            }
        } else {
            // Button unchecked: deactivate brush cursor and pick mode
            m_roiMeshWidget->setBrushCursorEnabled(false);
            m_roiMeshWidget->setPickMode(false);
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
    connect(m_brushRadiusSpin, QOverload<double>::of(&QDoubleSpinBox::valueChanged),
            m_roiMeshWidget, &VTKMeshWidget::setBrushCursorRadius);

    m_clearBrushBtn = new QPushButton("Clear Manual Overrides");
    m_clearBrushBtn->setToolTip(
        "Remove all Manual Override zones (green/red brush zones).\n"
        "This does NOT affect the Base Selection from segmentation.\n"
        "To reset the Base Selection, re-run Tooth Segmentation.");
    connect(m_clearBrushBtn, &QPushButton::clicked, this, [this]() {
        std::size_t clearedCount = m_currentROI.brushZones.size();
        m_brushPoints.clear();
        m_currentROI.brushZones.clear();
        // Hide brush zone pick spheres
        if (m_templateScan) {
            m_roiMeshWidget->showPickSpheres(m_seedPoints);  // Show only seed spheres
        }
        updateROIVisualization();
        if (clearedCount > 0) {
            m_statusLabel->setText(QString("Cleared %1 Manual Override zone(s)").arg(clearedCount));
        } else {
            m_statusLabel->setText("No Manual Override zones to clear");
        }
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

    m_runSegBtn = new QPushButton("Re-run Segmentation");
    m_runSegBtn->setToolTip(
        "Manually re-run segmentation with current parameters.\n"
        "Segmentation runs automatically after each seed is added/removed,\n"
        "but you can use this button to re-run after changing parameters.");
    connect(m_runSegBtn, &QPushButton::clicked, this, &MainWindow::runSegmentation);
    segLayout->addWidget(m_runSegBtn);

    m_useToothMaskChk = new QCheckBox("Use Base Selection as ROI");
    m_useToothMaskChk->setToolTip(
        "Include the Base Selection (from Tooth Segmentation) in the final ROI.\n\n"
        "When checked: Only vertices in the Base Selection are analyzed.\n"
        "The final ROI combines: BBox AND Plane Slab AND Base Selection,\n"
        "then Manual Overrides are applied on top.\n\n"
        "When unchecked: Base Selection is ignored; ROI uses only\n"
        "BBox, Plane Slab, and Manual Overrides.");
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

    // Set the scroll area content
    scrollArea->setWidget(rightPanel);
    layout->addWidget(scrollArea);
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

    // Options
    auto* optionsGroup = new QGroupBox("Registration Options");
    auto* optionsLayout = new QVBoxLayout(optionsGroup);

    m_useMaskedICPChk = new QCheckBox("Use ROI mask for registration (masked ICP)");
    m_useMaskedICPChk->setToolTip(
        "When checked, ICP alignment focuses on tooth surfaces only.\n"
        "Requires an ROI template with tooth segmentation seeds.\n"
        "When unchecked, full-mesh ICP is used.");
    m_useMaskedICPChk->setChecked(true);
    connect(m_useMaskedICPChk, &QCheckBox::toggled, this, &MainWindow::saveSettings);
    optionsLayout->addWidget(m_useMaskedICPChk);

    layout->addWidget(optionsGroup);

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

void MainWindow::browseMaskedOutputDir()
{
    QString path = QFileDialog::getExistingDirectory(this,
        "Select Masked ICP Output Directory",
        QString(),
        QFileDialog::ShowDirsOnly);

    if (!path.isEmpty()) {
        m_maskedOutputDirEdit->setText(path);
    }
}

void MainWindow::browseExternalRef()
{
    QString path = QFileDialog::getOpenFileName(this,
        "Select External Reference STL",
        QString(),
        "STL Files (*.stl *.STL);;All Files (*)");

    if (!path.isEmpty()) {
        m_externalRefEdit->setText(path);
    }
}

void MainWindow::browseROITemplate()
{
    QString path = QFileDialog::getOpenFileName(this,
        "Select ROI Template",
        QString(),
        "JSON Files (*.json);;All Files (*)");

    if (!path.isEmpty()) {
        m_roiTemplateEdit->setText(path);
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
    auto* groupsItem = new QTreeWidgetItem(m_studyTree, {"Groups", ""});
    groupsItem->setExpanded(true);
    for (const auto& group : m_studyConfig.groups) {
        QString details = group.conditionValue > 0
            ? QString("%1 mm").arg(group.conditionValue)
            : QString();
        auto* item = new QTreeWidgetItem(groupsItem, {group.id, details});
        for (const auto& pattern : group.filePatterns) {
            new QTreeWidgetItem(item, {"Pattern", pattern});
        }
    }

    m_studyTree->resizeColumnToContents(0);
}

void MainWindow::browseTemplateScan()
{
    QString path = QFileDialog::getOpenFileName(this,
        "Select Template STL File",
        m_templatePathEdit->text(),
        "STL Files (*.stl);;All Files (*)");

    if (!path.isEmpty()) {
        m_templatePathEdit->setText(path);
        loadTemplateScan();
    }
}

void MainWindow::loadTemplateScan()
{
    QString path = m_templatePathEdit->text().trimmed();
    if (path.isEmpty()) {
        QMessageBox::warning(this, "No Path", "Please enter or browse for a template STL file.");
        return;
    }

    std::string errorMsg;
    m_templateScan = STLReader::read(path.toStdString(), errorMsg);

    if (!m_templateScan) {
        QMessageBox::critical(this, "Load Error",
            QString("Failed to load STL file:\n%1").arg(QString::fromStdString(errorMsg)));
        return;
    }

    // Deactivate all pick modes before resetting state
    {
        QSignalBlocker b6(m_pickOcclusPlaneBtn), b7(m_seedPickBtn),
                       b8(m_brushIncludeBtn),    b9(m_brushExcludeBtn);
        m_pickOcclusPlaneBtn->setChecked(false);
        m_seedPickBtn->setChecked(false);
        m_brushIncludeBtn->setChecked(false);
        m_brushExcludeBtn->setChecked(false);
    }
    m_roiMeshWidget->setBrushCursorEnabled(false);
    m_roiMeshWidget->setPickMode(false);
    m_roiMeshWidget->clearPickActors();

    // Reset all ROI state to defaults when a new reference file is loaded
    {
        QSignalBlocker b1(m_bboxActiveChk), b2(m_zPlaneActiveChk),
                       b3(m_zAboveSpin),    b4(m_zBelowSpin),
                       b5(m_useToothMaskChk);
        m_currentROI = DentScanBatch::ROIConfig{};
        m_bboxActiveChk->setChecked(false);
        m_zPlaneActiveChk->setChecked(false);
        m_zAboveSpin->setValue(2.0);
        m_zBelowSpin->setValue(12.0);
        m_useToothMaskChk->setChecked(false);
    }
    m_occlusPlaneValid = false;
    m_occlusPlanePickMode = false;
    m_seedPickMode = false;
    m_occlusPlanePoints.clear();
    m_occlusPlaneNormal = Eigen::Vector3d(0, 0, 1);
    m_occlusPlaneOrigin = Eigen::Vector3d(0, 0, 0);
    m_occlusPlaneStatusLabel->setText("Plane: auto (max-Z)");
    m_roiMeshWidget->setPlanesVisible(false);
    m_roiMeshWidget->showPickSpheres({});
    m_brushPoints.clear();
    m_seedPoints.clear();
    m_toothMask.clear();

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
    m_roiMeshWidget->setBrushCursorEnabled(active);
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

        qDebug() << "Edit Base Selection mode: m_brushIncludeMode =" << m_brushIncludeMode
                 << ", includeBtn checked:" << m_brushIncludeBtn->isChecked()
                 << ", excludeBtn checked:" << m_brushExcludeBtn->isChecked()
                 << ", radius:" << radius << "mm";

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

        m_statusLabel->setText(QString("Base Selection: %1 %2 vertices (r=%3 mm)")
            .arg(m_brushIncludeMode ? "ADDED" : "REMOVED")
            .arg(modified)
            .arg(radius, 0, 'f', 1));

        updateROIVisualization();
        return;
    }

    // Add brush zone for ROI editing (Manual Override)
    DentScanBatch::BrushZone zone;
    zone.center = {x, y, z};
    zone.radius_mm = m_brushRadiusSpin->value();
    zone.include = m_brushIncludeMode;

    m_currentROI.brushZones.push_back(zone);
    m_brushPoints.push_back({x, y, z});

    qDebug() << "Added brush zone:" << (zone.include ? "INCLUDE (green)" : "EXCLUDE (red)")
             << "radius:" << zone.radius_mm << "mm at" << x << y << z;
    m_statusLabel->setText(QString("Manual Override: %1 zone added (r=%2 mm, total: %3)")
        .arg(zone.include ? "INCLUDE" : "EXCLUDE")
        .arg(zone.radius_mm, 0, 'f', 1)
        .arg(m_currentROI.brushZones.size()));

    // Show pick sphere (combine with seed points)
    std::vector<std::array<double, 3>> allPoints = m_brushPoints;
    allPoints.insert(allPoints.end(), m_seedPoints.begin(), m_seedPoints.end());
    m_roiMeshWidget->showPickSpheres(allPoints);
    updateROIVisualization();
}

void MainWindow::saveROITemplate()
{
    QSettings settings("DentScanComparePro", "DentScanComparePro");
    QString lastPath = settings.value("paths/lastROIEditorTemplate").toString();
    QString defaultPath = lastPath.isEmpty() ? "roi_template.json" : lastPath;

    QString path = QFileDialog::getSaveFileName(this,
        "Save ROI Template",
        defaultPath,
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

    // Occlusal plane (3-point pick result) — needed to restore z-slab reference on load
    if (m_occlusPlaneValid) {
        QJsonObject op;
        QJsonArray normal, origin;
        for (int i = 0; i < 3; i++) {
            normal.append(m_occlusPlaneNormal[i]);
            origin.append(m_occlusPlaneOrigin[i]);
        }
        op["normal"] = normal;
        op["origin"] = origin;
        root["occlusal_plane"] = op;
    }

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
    if (!file.open(QIODevice::WriteOnly)) {
        QMessageBox::critical(this, "Save Error", "Failed to save ROI template.");
        return;
    }
    file.write(QJsonDocument(root).toJson(QJsonDocument::Indented));
    file.close();
    settings.setValue("paths/lastROIEditorTemplate", path);

    // Export ROI mask as STL alongside the JSON template
    QString stlPath = QFileInfo(path).absolutePath() + "/" +
                      QFileInfo(path).completeBaseName() + "_roi_mask.stl";
    bool stlOk = false;
    if (m_templateScan) {
        // Build ROI submesh: keep only faces where all 3 vertices pass the current ROI filter
        const auto& refMesh = m_templateScan->mesh;

        // Compute occlusal Z (max Z of reference) — used as fallback when no plane picked
        double z_occlusal = std::numeric_limits<double>::lowest();
        for (auto v : refMesh.vertices())
            z_occlusal = std::max(z_occlusal, refMesh.point(v).z());

        // Sync picked plane into ROIConfig so the STL mask matches the visualized slab
        m_currentROI.occlusalPlane.active = m_occlusPlaneValid;
        if (m_occlusPlaneValid) {
            m_currentROI.occlusalPlane.normal = {
                m_occlusPlaneNormal[0], m_occlusPlaneNormal[1], m_occlusPlaneNormal[2]};
            m_currentROI.occlusalPlane.point = {
                m_occlusPlaneOrigin[0], m_occlusPlaneOrigin[1], m_occlusPlaneOrigin[2]};
        }

        // Mark vertices inside ROI
        std::vector<bool> inROI(refMesh.number_of_vertices(), false);
        std::size_t idx = 0;
        for (auto v : refMesh.vertices())
            inROI[idx++] = m_currentROI.isInROI(
                refMesh.point(v).x(), refMesh.point(v).y(), refMesh.point(v).z(), z_occlusal);

        // Build submesh
        SurfaceMesh roiMesh;
        std::unordered_map<std::size_t, SurfaceMesh::Vertex_index> vmap;
        for (auto f : refMesh.faces()) {
            auto hh = refMesh.halfedge(f);
            auto v0 = refMesh.source(hh);
            auto v1 = refMesh.target(hh);
            auto v2 = refMesh.target(refMesh.next(hh));
            if (!inROI[v0.idx()] || !inROI[v1.idx()] || !inROI[v2.idx()])
                continue;
            for (auto vx : {v0, v1, v2}) {
                if (!vmap.count(vx.idx()))
                    vmap[vx.idx()] = roiMesh.add_vertex(refMesh.point(vx));
            }
            roiMesh.add_face(vmap[v0.idx()], vmap[v1.idx()], vmap[v2.idx()]);
        }

        if (roiMesh.number_of_faces() > 0) {
            stlOk = DentScanBatch::QCExporter::writeBinarySTL(roiMesh, stlPath);
        } else {
            QMessageBox::warning(this, "ROI Empty",
                "The current ROI excludes all faces — ROI mask STL not written.\n"
                "Check that at least one ROI filter is active and overlaps the mesh.");
        }
    }

    // Update per-group ROI template path in the loaded study config.
    // Infer patient ID from the loaded STL filename (e.g. "002_reference.stl" → "002").
    bool studyJsonUpdated = false;
    if (m_configLoaded) {
        QString stlName = QFileInfo(m_templatePathEdit->text()).baseName(); // e.g. "002_reference"
        QString inferredId = stlName.section('_', 0, 0);                   // first token before '_'
        int groupIdx = -1;
        for (int i = 0; i < static_cast<int>(m_studyConfig.groups.size()); ++i) {
            if (m_studyConfig.groups[i].id == inferredId) { groupIdx = i; break; }
        }
        if (groupIdx >= 0) {
            m_studyConfig.groups[groupIdx].roiMaskStlPath = stlPath;
            m_studyConfig.groups[groupIdx].roi.outlierSigma = m_sigmaSpin->value();
        }

        // Write updated study config back to disk
        QString studyPath = m_studyPathEdit->text();
        if (!studyPath.isEmpty()) {
            try {
                m_studyConfig.saveToFile(studyPath);
                studyJsonUpdated = true;
            } catch (const std::exception& e) {
                QMessageBox::warning(this, "Config Save Warning",
                    QString("ROI template saved but could not update study config:\n%1").arg(e.what()));
            }
        }
    }

    QString msg = QString("ROI template saved: %1").arg(path);
    if (stlOk) msg += QString("\nROI mask STL: %1").arg(stlPath);
    if (studyJsonUpdated) {
        msg += QString("\nStudy config updated: %1").arg(m_studyPathEdit->text());
    } else {
        msg += "\nWARNING: Study config NOT updated — load the study config first.";
        QMessageBox::warning(this, "Study Config Not Updated",
            "The ROI template was saved, but the study configuration JSON was not updated.\n\n"
            "To register this template in the study:\n"
            "1. Load the study config (.json) using 'Load Config'\n"
            "2. Select the patient group in the ROI tab\n"
            "3. Save the ROI template again.");
    }
    m_statusLabel->setText(msg);
}

void MainWindow::loadROITemplate()
{
    QSettings settings("DentScanComparePro", "DentScanComparePro");
    QString lastPath = settings.value("paths/lastROIEditorTemplate").toString();

    QString path = QFileDialog::getOpenFileName(this,
        "Load ROI Template",
        lastPath,
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
    // Set spinboxes from JSON values directly — NOT via m_currentROI, because each
    // setChecked/setValue fires onBBoxChanged which reads the spinboxes and would
    // overwrite m_currentROI with stale values before all spinboxes are updated.
    if (root.contains("bbox")) {
        QJsonObject bbox = root["bbox"].toObject();
        QJsonArray bboxMin = bbox["min"].toArray();
        QJsonArray bboxMax = bbox["max"].toArray();
        m_bboxActiveChk->setChecked(bbox["active"].toBool());
        m_bboxMinX->setValue(bboxMin.size() > 0 ? bboxMin[0].toDouble() : 0.0);
        m_bboxMinY->setValue(bboxMin.size() > 1 ? bboxMin[1].toDouble() : 0.0);
        m_bboxMinZ->setValue(bboxMin.size() > 2 ? bboxMin[2].toDouble() : 0.0);
        m_bboxMaxX->setValue(bboxMax.size() > 0 ? bboxMax[0].toDouble() : 0.0);
        m_bboxMaxY->setValue(bboxMax.size() > 1 ? bboxMax[1].toDouble() : 0.0);
        m_bboxMaxZ->setValue(bboxMax.size() > 2 ? bboxMax[2].toDouble() : 0.0);
    }

    // Load Z-plane (same pattern: set UI from JSON values directly)
    if (root.contains("z_plane")) {
        QJsonObject zPlane = root["z_plane"].toObject();
        m_zPlaneActiveChk->setChecked(zPlane["active"].toBool());
        m_zAboveSpin->setValue(zPlane["above_mm"].toDouble());
        m_zBelowSpin->setValue(zPlane["below_mm"].toDouble());
    }

    // Load occlusal plane (3-point pick result)
    m_occlusPlaneValid = false;
    m_occlusPlanePoints.clear();
    if (root.contains("occlusal_plane")) {
        QJsonObject op = root["occlusal_plane"].toObject();
        QJsonArray normal = op["normal"].toArray();
        QJsonArray origin = op["origin"].toArray();
        if (normal.size() >= 3 && origin.size() >= 3) {
            m_occlusPlaneNormal = Eigen::Vector3d(
                normal[0].toDouble(), normal[1].toDouble(), normal[2].toDouble());
            m_occlusPlaneOrigin = Eigen::Vector3d(
                origin[0].toDouble(), origin[1].toDouble(), origin[2].toDouble());
            m_occlusPlaneValid = true;
            m_occlusPlaneStatusLabel->setText(
                QString("Plane: defined at Z=%1").arg(m_occlusPlaneOrigin.z(), 0, 'f', 1));
        }
    } else {
        m_occlusPlaneStatusLabel->setText("Plane: auto (max-Z)");
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

    settings.setValue("paths/lastROIEditorTemplate", path);

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

    // Sync the picked plane into ROIConfig so isInROI uses signed-distance, not raw Z
    m_currentROI.occlusalPlane.active = m_occlusPlaneValid;
    if (m_occlusPlaneValid) {
        m_currentROI.occlusalPlane.normal = {
            m_occlusPlaneNormal[0], m_occlusPlaneNormal[1], m_occlusPlaneNormal[2]};
        m_currentROI.occlusalPlane.point = {
            m_occlusPlaneOrigin[0], m_occlusPlaneOrigin[1], m_occlusPlaneOrigin[2]};
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

    // Display visualization
    if (!m_currentROI.brushZones.empty()) {
        std::vector<bool> toothMask;
        if (m_useToothMaskChk && m_useToothMaskChk->isChecked() && !m_toothMask.empty()) {
            toothMask = m_toothMask;
        }
        m_roiMeshWidget->showBrushZones(m_templateScan, m_currentROI.brushZones, toothMask);
    } else {
        m_roiMeshWidget->showToothSegmentation(m_templateScan, mask);
    }

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
    QString maskedOutputDir = m_maskedOutputDirEdit->text().trimmed();
    bool useMaskedICP = m_useMaskedICPChk->isChecked();

    if (dataRoot.isEmpty()) {
        QMessageBox::warning(this, "Error", "Please specify the data root directory.");
        return;
    }

    if (outputDir.isEmpty()) {
        outputDir = "./results";
    }

    // Use masked output directory if masked ICP is enabled and directory is specified
    if (useMaskedICP && !maskedOutputDir.isEmpty()) {
        outputDir = maskedOutputDir;
    }

    // Get external reference settings from GUI
    QString externalRef = m_externalRefEdit->text();
    QString roiTemplatePath = m_roiTemplateEdit->text();
    bool scansPreAligned = m_scansPreAlignedChk->isChecked();
    bool scansNormalized = m_scansNormalizedChk->isChecked();

    m_batchLog->clear();
    m_batchLog->append("Starting batch processing...");
    m_batchLog->append("Data root: " + dataRoot);
    m_batchLog->append("Output dir: " + outputDir);
    if (useMaskedICP && !maskedOutputDir.isEmpty()) {
        m_batchLog->append("(Using Masked ICP Output directory)");
    }
    if (!externalRef.isEmpty()) {
        m_batchLog->append("External reference: " + externalRef);
    }
    if (!roiTemplatePath.isEmpty()) {
        m_batchLog->append("ROI template: " + roiTemplatePath);
    }
    if (scansPreAligned) {
        m_batchLog->append("Scans pre-aligned: YES (skipping GPA, ICP refinement will run)");
    }
    if (scansNormalized) {
        m_batchLog->append("Scans normalized: YES (JSON transforms will NOT be loaded)");
    }
    m_batchLog->append("");

    // Update study config with GUI settings
    m_studyConfig.externalReferencePath = externalRef;
    m_studyConfig.scansPreAligned = scansPreAligned;
    m_studyConfig.scansNormalized = scansNormalized;
    m_studyConfig.alignment.icpTrimFraction  = m_icpTrimFractionSpin->value();
    m_studyConfig.alignment.useIcpHierarchy  = m_icpHierarchyChk->isChecked();
    if (!externalRef.isEmpty()) {
        m_studyConfig.referenceStrategy = "external";
    }

    enableBatchControls(false);
    m_batchRunning = true;

    // Load ROI template if specified
    std::optional<DentScanBatch::ROITemplate> roiTemplate;
    if (!roiTemplatePath.isEmpty()) {
        try {
            roiTemplate = DentScanBatch::ROITemplate::loadFromFile(roiTemplatePath);
            m_batchLog->append("ROI template loaded successfully");

            // Control masked ICP via checkbox
            if (!useMaskedICP) {
                roiTemplate.reset();
                m_batchLog->append("Full-mesh mode: ENABLED (ROI template ignored, forceFullMesh=true)");
            } else {
                if (!roiTemplate->toothSeeds.empty())
                    roiTemplate->useToothMask = true;

                // Report which ROI components are active
                QStringList active;
                if (roiTemplate->roi.bbox.active)           active << "bbox";
                if (roiTemplate->roi.zPlane.active)         active << "z-plane";
                if (!roiTemplate->roi.brushZones.empty())   active << QString("%1 brush zones").arg(roiTemplate->roi.brushZones.size());
                if (!roiTemplate->toothSeeds.empty())       active << QString("%1 tooth seeds").arg(roiTemplate->toothSeeds.size());

                if (active.isEmpty()) {
                    m_batchLog->append("ROI template: no active components — full-mesh ICP and full-mesh metrics");
                } else {
                    m_batchLog->append(QString("ROI template active: %1 — masked ICP + restricted metrics")
                        .arg(active.join(", ")));
                }
            }
        } catch (const std::exception& e) {
            QMessageBox::warning(this, "ROI Template Error",
                QString("Failed to load ROI template:\n%1\n\nContinuing without ROI template.")
                .arg(e.what()));
        }
    } else if (useMaskedICP) {
        // Check whether per-group templates are defined in the study config
        int nGroupsWithTemplate = 0;
        for (const auto& g : m_studyConfig.groups)
            if (!g.roiMaskStlPath.isEmpty()) ++nGroupsWithTemplate;
        if (nGroupsWithTemplate > 0) {
            m_batchLog->append(QString("Masked ICP: no global template — using per-group ROI templates (%1/%2 groups configured)")
                .arg(nGroupsWithTemplate).arg(m_studyConfig.groups.size()));
        } else {
            m_batchLog->append("Masked ICP: No ROI template specified, using full-mesh ICP");
        }
    }

    // Create batch runner
    m_batchRunner = std::make_unique<DentScanBatch::BatchRunner>();
    m_batchRunner->setVerbose(true);

    // Run in background thread
    m_batchWatcher = new QFutureWatcher<bool>(this);
    connect(m_batchWatcher, &QFutureWatcher<bool>::finished, this, &MainWindow::onBatchFinished);

    // forceFullMesh = true when user disabled masked ICP checkbox
    bool forceFullMesh = !useMaskedICP;
    auto future = QtConcurrent::run([this, dataRoot, outputDir, roiTemplate, forceFullMesh]() {
        return m_batchRunner->run(m_studyConfig, dataRoot, outputDir, roiTemplate, forceFullMesh);
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
        m_roiMeshWidget->setBrushCursorEnabled(false);
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

    m_statusLabel->setText(QString("Seed point added at (%1, %2, %3). Total: %4")
        .arg(x, 0, 'f', 2).arg(y, 0, 'f', 2).arg(z, 0, 'f', 2).arg(m_seedPoints.size()));

    // Auto-run segmentation after each seed is added
    runSegmentationAuto();
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

    runSegmentationAuto();
}

void MainWindow::runSegmentationAuto()
{
    // Auto-segmentation: runs without dialogs, called after each seed change
    if (!m_templateScan || m_seedPoints.empty()) {
        return;
    }

    m_statusLabel->setText("Computing segmentation...");
    QApplication::processEvents();

    // Compute curvature if not already done
    if (!m_templateScan->curvatureComputed) {
        CurvatureAnalysis::compute(*m_templateScan);
    }

    // Update segmentation parameters from UI
    m_segParams.maxGeodesicMm = m_segGeodesicSpin->value();
    m_segParams.maxCreaseAngleDeg = m_segCreaseSpin->value();
    m_segParams.minMeanCurvature = m_segCurvSpin->value();
    m_segParams.curvatureRepulsion = m_segRepulsionSpin->value();

    // Run segmentation
    m_toothMask = ToothSegmentation::segmentFromPoints(*m_templateScan, m_seedPoints, m_segParams);

    // Count segmented vertices
    std::size_t toothCount = std::count(m_toothMask.begin(), m_toothMask.end(), true);

    m_statusLabel->setText(QString("Segmentation: %1 vertices (%2 seeds)")
        .arg(toothCount).arg(m_seedPoints.size()));

    // Auto-enable "Use Base Selection as ROI" when segmentation runs
    if (!m_useToothMaskChk->isChecked()) {
        m_useToothMaskChk->setChecked(true);
    }

    // Update visualization to show tooth mask
    updateROIVisualization();
}

void MainWindow::clearSeeds()
{
    m_seedPoints.clear();
    m_toothMask.clear();
    m_seedCountLabel->setText("Seeds: 0");

    // Uncheck "Use Base Selection" since there's no selection anymore
    if (m_useToothMaskChk->isChecked()) {
        m_useToothMaskChk->setChecked(false);
    }

    // Update sphere visualization (only brush points remain)
    m_roiMeshWidget->showPickSpheres(m_brushPoints);
    updateROIVisualization();

    m_statusLabel->setText("Seed points and Base Selection cleared.");
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

    m_statusLabel->setText(QString("Seed removed. Total: %1").arg(m_seedPoints.size()));

    // Auto-run segmentation with remaining seeds (or clear mask if no seeds left)
    if (m_seedPoints.empty()) {
        m_toothMask.clear();
        // Uncheck "Use Base Selection" since there's no selection anymore
        if (m_useToothMaskChk->isChecked()) {
            m_useToothMaskChk->setChecked(false);
        }
        updateROIVisualization();
    } else {
        runSegmentationAuto();
    }
}

// === Occlusal Plane Picking ===

void MainWindow::onOcclusPlanePickModeToggled(bool active)
{
    m_occlusPlanePickMode = active;
    m_roiMeshWidget->setPickMode(active);

    // Disable other pick modes
    if (active) {
        m_roiMeshWidget->setBrushCursorEnabled(false);
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

        m_occlusPlaneStatusLabel->setText(QString("Plane: defined at Z=%1").arg(m_occlusPlaneOrigin.z(), 0, 'f', 1));

        // Auto-enable the Plane Slab Active checkbox — there is no reason to pick
        // an occlusal plane and then not use it for z-slab filtering.
        if (!m_zPlaneActiveChk->isChecked())
            m_zPlaneActiveChk->setChecked(true);  // fires onZPlaneChanged()

        // Exit pick mode
        m_pickOcclusPlaneBtn->setChecked(false);
        m_statusLabel->setText("Occlusal plane defined from 3 points. Plane Slab enabled.");

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
        // Initialize Base Selection to all false if not already computed
        m_toothMask.resize(m_templateScan->mesh.number_of_vertices(), false);
        m_statusLabel->setText("Base Selection initialized (empty). Use brush to add/remove vertices.");
    } else if (active) {
        m_statusLabel->setText("Brush now edits Base Selection directly (changes are permanent).");
    } else {
        m_statusLabel->setText("Brush now creates Manual Overrides (green/red zones, transferable).");
    }
}

// === QC Review Tab ===

void MainWindow::setupQCReviewTab()
{
    m_qcTab = new QWidget();
    m_tabs->addTab(m_qcTab, "QC Review");

    auto* layout = new QVBoxLayout(m_qcTab);

    // Load QC button row
    auto* loadRow = new QHBoxLayout();
    auto* loadQCBtn = new QPushButton("Load QC Data from Results");
    connect(loadQCBtn, &QPushButton::clicked, this, &MainWindow::loadQCData);
    loadRow->addWidget(loadQCBtn);

    auto* genImagesBtn = new QPushButton("Generate Difference Images");
    genImagesBtn->setToolTip("Render difference images for all scans (requires loaded QC data)");
    connect(genImagesBtn, &QPushButton::clicked, this, &MainWindow::generateDifferenceImages);
    loadRow->addWidget(genImagesBtn);

    m_qcApplyROIChk = new QCheckBox("Apply ROI template");
    m_qcApplyROIChk->setToolTip(
        "When checked, vertices outside the ROI template are shown in grey.\n"
        "Existing images are regenerated. Requires an ROI template to be set.");
    loadRow->addWidget(m_qcApplyROIChk);

    m_rebuildMetricsBtn = new QPushButton("Rebuild Metrics from Transforms");
    m_rebuildMetricsBtn->setToolTip(
        "Re-read all qc/transforms/*.json files and rewrite the trueness,\n"
        "precision, and summary CSVs. Run this after re-registering errands.");
    connect(m_rebuildMetricsBtn, &QPushButton::clicked,
            this, &MainWindow::rebuildMetricsFromTransforms);
    loadRow->addWidget(m_rebuildMetricsBtn);

    loadRow->addStretch();
    layout->addLayout(loadRow);

    // QC Review widget
    m_qcReviewWidget = new DentScanBatch::QCReviewWidget(this);

    // Connect signals
    connect(m_qcReviewWidget, &DentScanBatch::QCReviewWidget::viewRequested,
            this, &MainWindow::onQCViewRequested);
    connect(m_qcReviewWidget, &DentScanBatch::QCReviewWidget::reregisterRequested,
            this, &MainWindow::onQCReregisterRequested);
    connect(m_qcReviewWidget, &DentScanBatch::QCReviewWidget::batchReregisterRequested,
            this, &MainWindow::onQCBatchReregisterRequested);
    connect(m_qcReviewWidget, &DentScanBatch::QCReviewWidget::statusChanged,
            this, &MainWindow::onQCStatusChanged);
    connect(m_qcReviewWidget, &DentScanBatch::QCReviewWidget::saveRequested,
            this, &MainWindow::onQCSaveRequested);

    layout->addWidget(m_qcReviewWidget, 1);
}

void MainWindow::loadQCData()
{
    QString outputDir = m_outputDirEdit->text();
    if (outputDir.isEmpty()) {
        outputDir = "./results";
    }

    if (!m_qcReviewWidget->loadFromDirectory(outputDir)) {
        QMessageBox::warning(this, "Load Error",
            "Failed to load QC data. Make sure batch processing has completed\n"
            "and QC data was exported to the qc/ subdirectory.");
        return;
    }

    m_statusLabel->setText("QC data loaded from " + outputDir);
}

void MainWindow::onQCViewRequested(const QString& scanId, const QString& imagePath)
{
    Q_UNUSED(imagePath);

    QString outputDir = m_outputDirEdit->text();
    if (outputDir.isEmpty()) {
        outputDir = "./results";
    }

    // Get the scan record
    auto record = m_qcReviewWidget->errandManager().getRecord(scanId);
    if (record.scanId.isEmpty() || record.filePath.isEmpty()) {
        QMessageBox::warning(this, "Error", "Could not find scan record for: " + scanId);
        return;
    }

    // Find reference mesh for this group (try new naming first, then legacy)
    QString refMeshPath = outputDir + "/qc/reference_meshes/" + record.groupId + "_reference.stl";
    if (!QFile::exists(refMeshPath)) {
        // Try legacy naming (pre-136d3e1)
        refMeshPath = outputDir + "/qc/gpa_means/" + record.groupId + "_gpa_mean.stl";
    }
    if (!QFile::exists(refMeshPath)) {
        QMessageBox::warning(this, "Error",
            QString("Reference mesh not found. Looked for:\n- %1\n- %2")
                .arg(outputDir + "/qc/reference_meshes/" + record.groupId + "_reference.stl")
                .arg(outputDir + "/qc/gpa_means/" + record.groupId + "_gpa_mean.stl"));
        return;
    }

    // Find transform JSON for this scan
    QString transformPath = outputDir + "/qc/transforms/" + scanId + ".json";

    // Open AlignmentQCDialog
    DentScanBatch::AlignmentQCDialog dialog(
        record.filePath, refMeshPath, scanId, this);

    // Load meshes with transform
    if (!dialog.loadMeshes(transformPath)) {
        return;
    }

    int result = dialog.exec();

    // Process events to allow VTK cleanup
    for (int i = 0; i < 3; ++i) {
        QApplication::processEvents();
    }

    if (result == QDialog::Accepted) {
        if (dialog.wasAccepted()) {
            m_qcReviewWidget->errandManager().setStatus(scanId, DentScanBatch::QCStatus::Accepted);
            m_statusLabel->setText(QString("Scan %1 accepted").arg(scanId));
        } else if (dialog.wasFlagged()) {
            m_qcReviewWidget->errandManager().setStatus(scanId, DentScanBatch::QCStatus::Errand);
            m_statusLabel->setText(QString("Scan %1 flagged as errand").arg(scanId));
        }

        // Refresh QC review
        QTimer::singleShot(100, this, [this]() {
            m_qcReviewWidget->refresh();
        });
    }
}

void MainWindow::onQCReregisterRequested(const QString& scanId)
{
    QString outputDir = m_outputDirEdit->text();
    if (outputDir.isEmpty()) {
        outputDir = "./results";
    }

    // Get the scan record
    auto record = m_qcReviewWidget->errandManager().getRecord(scanId);
    if (record.scanId.isEmpty()) {
        QMessageBox::warning(this, "Error", "Could not find scan record for: " + scanId);
        return;
    }

    // Find reference mesh for this group (try new naming first, then legacy)
    QString refMeshPath = outputDir + "/qc/reference_meshes/" + record.groupId + "_reference.stl";
    if (!QFile::exists(refMeshPath)) {
        // Try legacy naming (pre-136d3e1)
        refMeshPath = outputDir + "/qc/gpa_means/" + record.groupId + "_gpa_mean.stl";
    }
    if (!QFile::exists(refMeshPath)) {
        QMessageBox::warning(this, "Error",
            QString("Reference mesh not found for group: %1").arg(record.groupId));
        return;
    }

    // Open errand resolution dialog
    qDebug() << "Creating ErrandResolutionDialog...";
    DentScanBatch::ErrandResolutionDialog dialog(
        record.filePath, refMeshPath, scanId, this);

    qDebug() << "Calling dialog.exec()...";
    int result = dialog.exec();
    qDebug() << "dialog.exec() returned:" << result;

    // Process events multiple times to allow VTK cleanup from dialog destruction
    for (int i = 0; i < 3; ++i) {
        QApplication::processEvents();
    }

    if (result == QDialog::Accepted && dialog.wasAccepted()) {
        qDebug() << "Dialog accepted, updating errand manager...";

        // Overwrite the transform JSON with corrected transform + metrics
        {
            auto metrics = dialog.correctedMetrics();
            metrics.filePath = record.filePath;
            metrics.groupId  = record.groupId;

            // Build a minimal ScanData carrying only the corrected transform
            auto scanProxy = std::make_shared<ScanData>();
            scanProxy->transform = dialog.correctedTransform();

            if (!DentScanBatch::QCExporter::exportTransform(
                    scanProxy, metrics, outputDir, scanId)) {
                QMessageBox::warning(this, "Warning",
                    QString("Could not overwrite transform JSON for %1.\n"
                            "Rebuild metrics manually if needed.").arg(scanId));
            }
        }

        // Update errand manager status and persist to disk
        m_qcReviewWidget->errandManager().markResolved(
            scanId, dialog.newRMS(), "landmark");
        m_qcReviewWidget->errandManager().save(outputDir);
        m_qcReviewWidget->errandManager().saveErrandsList(outputDir);

        // Regenerate difference image for this scan (overwrites old PNG)
        bool imgOk = regenerateDifferenceImageForScan(scanId, outputDir);

        double newRMS = dialog.newRMS();
        QString sid = scanId;
        bool imgGenerated = imgOk;

        qDebug() << "Deferring QC review refresh...";
        QTimer::singleShot(100, this, [this, sid, newRMS, imgGenerated]() {
            qDebug() << "Refreshing QC review (deferred)...";
            m_qcReviewWidget->refresh();
            QString msg = QString("Scan %1 re-registered. New RMS: %2 mm. "
                                  "Run 'Rebuild Metrics from Transforms' to update CSVs.")
                .arg(sid).arg(newRMS, 0, 'f', 3);
            if (imgGenerated)
                msg += " Difference image updated.";
            m_statusLabel->setText(msg);
            qDebug() << "Refresh complete";
        });
    }
    qDebug() << "onQCReregisterRequested() complete";
}

void MainWindow::onQCBatchReregisterRequested(const QStringList& scanIds)
{
    if (scanIds.isEmpty()) return;

    int completed = 0;
    for (const QString& scanId : scanIds) {
        m_statusLabel->setText(QString("Re-aligning %1 (%2 of %3)...")
            .arg(scanId).arg(completed + 1).arg(scanIds.size()));
        QApplication::processEvents();

        onQCReregisterRequested(scanId);
        ++completed;
    }

    m_statusLabel->setText(QString("Re-alignment done for %1 scan(s). "
                                   "Run 'Rebuild Metrics from Transforms' to update CSVs.")
        .arg(completed));
}

void MainWindow::onQCStatusChanged()
{
    m_statusLabel->setText("QC status changed. Remember to save.");
}

void MainWindow::onQCSaveRequested()
{
    m_statusLabel->setText("QC status saved.");
}

static bool imageMetaMatchesROI(const QString& metaPath, const QString& roiPath)
{
    QFile f(metaPath);
    if (!f.open(QIODevice::ReadOnly)) return false;
    QJsonDocument doc = QJsonDocument::fromJson(f.readAll());
    if (!doc.isObject()) return false;
    QJsonObject obj = doc.object();
    if (obj["roi_path"].toString() != roiPath) return false;
    qint64 stored  = obj["roi_mtime"].toVariant().toLongLong();
    qint64 current = QFileInfo(roiPath).lastModified().toMSecsSinceEpoch();
    return stored == current;
}

static void writeImageMeta(const QString& metaPath, const QString& roiPath)
{
    QJsonObject obj;
    obj["roi_path"] = roiPath;
    if (!roiPath.isEmpty())
        obj["roi_mtime"] = QFileInfo(roiPath).lastModified().toMSecsSinceEpoch();
    QFile f(metaPath);
    if (f.open(QIODevice::WriteOnly))
        f.write(QJsonDocument(obj).toJson(QJsonDocument::Compact));
}

bool MainWindow::regenerateDifferenceImageForScan(const QString& scanId, const QString& outputDir)
{
    // Locate reference mesh directory (new or legacy naming)
    QDir refMeshDir(outputDir + "/qc/reference_meshes");
    bool useLegacy = false;
    if (!refMeshDir.exists()) {
        QDir legacyDir(outputDir + "/qc/gpa_means");
        if (legacyDir.exists()) {
            refMeshDir = legacyDir;
            useLegacy = true;
        } else {
            return false;
        }
    }

    // Load transform JSON
    QString jsonPath = outputDir + "/qc/transforms/" + scanId + ".json";
    QFile file(jsonPath);
    if (!file.open(QIODevice::ReadOnly)) return false;

    QJsonDocument doc = QJsonDocument::fromJson(file.readAll());
    if (!doc.isObject()) return false;

    QJsonObject obj = doc.object();
    QString filePath = obj["file_path"].toString();
    QString groupId  = obj["group"].toString();
    if (filePath.isEmpty() || groupId.isEmpty()) return false;

    // Load original STL
    std::string errorMsg;
    auto scanData = STLReader::read(filePath.toStdString(), errorMsg);
    if (!scanData) return false;

    // Apply stored transform
    QJsonArray transformArray = obj["transform"].toArray();
    Eigen::Matrix4d transform = Eigen::Matrix4d::Identity();
    for (int r = 0; r < 4 && r < transformArray.size(); ++r) {
        QJsonArray row = transformArray[r].toArray();
        for (int c = 0; c < 4 && c < row.size(); ++c)
            transform(r, c) = row[c].toDouble();
    }
    for (auto v : scanData->mesh.vertices()) {
        Point3& p = scanData->mesh.point(v);
        Eigen::Vector4d pt(p.x(), p.y(), p.z(), 1.0);
        Eigen::Vector4d tp = transform * pt;
        p = Point3(tp.x(), tp.y(), tp.z());
    }

    // Load reference mesh and compute distances
    QString refFilename = useLegacy ? (groupId + "_gpa_mean.stl")
                                    : (groupId + "_reference.stl");
    auto refMesh = STLReader::read(refMeshDir.absoluteFilePath(refFilename).toStdString(), errorMsg);
    if (!refMesh) return false;

    DistanceField::ReferenceTree refTree(refMesh->mesh);
    refTree.computeDistances(*scanData);

    // Optionally build ROI mask
    const bool applyROI = m_qcApplyROIChk && m_qcApplyROIChk->isChecked();
    std::vector<bool> roiMask;
    if (applyROI) {
        QString roiPath = m_roiTemplateEdit->text().trimmed();
        if (!roiPath.isEmpty() && QFile::exists(roiPath)) {
            auto roiTemplate = DentScanBatch::ROITemplate::loadFromFile(roiPath);
            const DentScanBatch::ROIConfig& roi = roiTemplate.roi;
            double maxZ = std::numeric_limits<double>::lowest();
            for (auto v : scanData->mesh.vertices())
                maxZ = std::max(maxZ, scanData->mesh.point(v).z());
            roiMask.reserve(scanData->mesh.number_of_vertices());
            for (auto v : scanData->mesh.vertices()) {
                const Point3& p = scanData->mesh.point(v);
                roiMask.push_back(roi.isInROI(p.x(), p.y(), p.z(), maxZ));
            }
        }
    }

    DentScanBatch::QCExporter::setImageExportEnabled(true);
    bool ok = DentScanBatch::QCExporter::exportDifferenceImage(
        scanData, outputDir, scanId, -0.5, 0.5, roiMask);

    if (ok) {
        QString metaPath = outputDir + "/qc/difference_images/" + scanId + ".meta";
        writeImageMeta(metaPath, applyROI ? m_roiTemplateEdit->text().trimmed() : QString());
    }
    return ok;
}

void MainWindow::generateDifferenceImages()
{
    QString outputDir = m_outputDirEdit->text();
    if (outputDir.isEmpty()) {
        outputDir = "./results";
    }

    QDir transformDir(outputDir + "/qc/transforms");
    if (!transformDir.exists()) {
        QMessageBox::warning(this, "No QC Data",
            "No transforms found. Run batch processing first, then load QC data.");
        return;
    }

    // Check for reference meshes (new naming) or legacy gpa_means directory
    QDir refMeshDir(outputDir + "/qc/reference_meshes");
    QDir legacyDir(outputDir + "/qc/gpa_means");
    bool useLegacy = false;
    if (!refMeshDir.exists()) {
        if (legacyDir.exists()) {
            refMeshDir = legacyDir;
            useLegacy = true;
        } else {
            QMessageBox::warning(this, "No Reference Meshes",
                "No reference meshes found. Run batch processing first.");
            return;
        }
    }

    // Get list of transform files
    QStringList jsonFiles = transformDir.entryList(QStringList() << "*.json", QDir::Files);
    if (jsonFiles.isEmpty()) {
        QMessageBox::warning(this, "No Transforms", "No transform files found.");
        return;
    }

    // Enable image export (was disabled in batch mode)
    DentScanBatch::QCExporter::setImageExportEnabled(true);

    // Load ROI template if the checkbox is checked
    const bool applyROI = m_qcApplyROIChk && m_qcApplyROIChk->isChecked();
    std::optional<DentScanBatch::ROITemplate> roiTemplate;
    if (applyROI) {
        QString roiPath = m_roiTemplateEdit->text().trimmed();
        if (roiPath.isEmpty() || !QFile::exists(roiPath)) {
            QMessageBox::warning(this, "No ROI Template",
                "Apply ROI is checked but no valid ROI template file is set.\n"
                "Please set the ROI template path in the Batch Configuration tab.");
            return;
        }
        roiTemplate = DentScanBatch::ROITemplate::loadFromFile(roiPath);
    }

    // Progress dialog
    QProgressDialog progress("Generating difference images...", "Cancel", 0, jsonFiles.size(), this);
    progress.setWindowModality(Qt::WindowModal);
    progress.setMinimumDuration(0);

    int generated = 0;
    int skipped = 0;

    // Cache for loaded GPA means AND their AABB trees (avoids rebuilding tree for each scan)
    std::map<QString, std::shared_ptr<ScanData>> gpaMeanCache;
    std::map<QString, std::unique_ptr<DistanceField::ReferenceTree>> refTreeCache;

    for (int i = 0; i < jsonFiles.size(); ++i) {
        if (progress.wasCanceled()) break;

        QString jsonFile = jsonFiles[i];
        QString scanId = jsonFile;
        scanId.chop(5);  // Remove .json

        progress.setValue(i);
        progress.setLabelText(QString("Processing %1 (%2/%3)")
            .arg(scanId).arg(i + 1).arg(jsonFiles.size()));
        QApplication::processEvents();

        QString imagePath = outputDir + "/qc/difference_images/" + scanId + ".png";
        QString metaPath  = outputDir + "/qc/difference_images/" + scanId + ".meta";
        if (QFile::exists(imagePath)) {
            if (!applyROI || imageMetaMatchesROI(metaPath, m_roiTemplateEdit->text().trimmed())) {
                skipped++;
                continue;
            }
        }

        // Load transform JSON
        QString jsonPath = transformDir.absoluteFilePath(jsonFile);
        QFile file(jsonPath);
        if (!file.open(QIODevice::ReadOnly)) continue;

        QJsonDocument doc = QJsonDocument::fromJson(file.readAll());
        if (!doc.isObject()) continue;

        QJsonObject obj = doc.object();
        QString filePath = obj["file_path"].toString();
        QString groupId = obj["group"].toString();

        if (filePath.isEmpty() || groupId.isEmpty()) continue;

        // Load original STL
        std::string errorMsg;
        auto scanData = STLReader::read(filePath.toStdString(), errorMsg);
        if (!scanData) {
            continue;
        }

        // Load transform matrix
        QJsonArray transformArray = obj["transform"].toArray();
        Eigen::Matrix4d transform = Eigen::Matrix4d::Identity();
        for (int r = 0; r < 4 && r < transformArray.size(); ++r) {
            QJsonArray row = transformArray[r].toArray();
            for (int c = 0; c < 4 && c < row.size(); ++c) {
                transform(r, c) = row[c].toDouble();
            }
        }

        // Apply transform to mesh vertices
        for (auto v : scanData->mesh.vertices()) {
            Point3& p = scanData->mesh.point(v);
            Eigen::Vector4d pt(p.x(), p.y(), p.z(), 1.0);
            Eigen::Vector4d transformed = transform * pt;
            p = Point3(transformed.x(), transformed.y(), transformed.z());
        }

        // Load reference mesh and AABB tree (cached for efficiency)
        QString refMeshFilename = useLegacy
            ? (groupId + "_gpa_mean.stl")
            : (groupId + "_reference.stl");
        QString refMeshPath = refMeshDir.absoluteFilePath(refMeshFilename);

        // Get or create cached AABB tree for this group's reference
        auto treeIt = refTreeCache.find(groupId);
        if (treeIt == refTreeCache.end()) {
            // Load mesh and build tree (only once per group)
            std::shared_ptr<ScanData> refMesh = STLReader::read(refMeshPath.toStdString(), errorMsg);
            if (!refMesh) {
                continue;
            }
            gpaMeanCache[groupId] = refMesh;
            // Build and cache AABB tree - this is expensive, so do it once
            refTreeCache[groupId] = std::make_unique<DistanceField::ReferenceTree>(refMesh->mesh);
            treeIt = refTreeCache.find(groupId);
        }

        // Compute distances using cached tree (much faster than rebuilding tree each time)
        treeIt->second->computeDistances(*scanData);

        // Build ROI mask if requested (out-of-ROI vertices rendered grey)
        std::vector<bool> roiMask;
        if (applyROI && roiTemplate.has_value()) {
            const DentScanBatch::ROIConfig& roi = roiTemplate->roi;
            double maxZ = std::numeric_limits<double>::lowest();
            for (auto v : scanData->mesh.vertices())
                maxZ = std::max(maxZ, scanData->mesh.point(v).z());

            roiMask.reserve(scanData->mesh.number_of_vertices());
            for (auto v : scanData->mesh.vertices()) {
                const Point3& p = scanData->mesh.point(v);
                roiMask.push_back(roi.isInROI(p.x(), p.y(), p.z(), maxZ));
            }
        }

        // Export difference image
        if (DentScanBatch::QCExporter::exportDifferenceImage(
                scanData, outputDir, scanId, -0.5, 0.5, roiMask)) {
            generated++;
            writeImageMeta(metaPath, applyROI ? m_roiTemplateEdit->text().trimmed() : QString());
        }
    }

    progress.setValue(jsonFiles.size());

    // Refresh QC review widget
    m_qcReviewWidget->refresh();

    m_statusLabel->setText(QString("Generated %1 images, skipped %2 (already exist)")
        .arg(generated).arg(skipped));

    QMessageBox::information(this, "Complete",
        QString("Generated %1 difference images.\nSkipped %2 (already exist).")
        .arg(generated).arg(skipped));
}

// ---------------------------------------------------------------------------
// Rebuild all metric CSVs from the saved qc/transforms/*.json files.
//
// Trueness metrics: read directly from JSON (no STL reload needed).
// Precision metrics: reload original STLs, apply stored transforms, compute
//   pairwise distances within each scanner group.  No tooth masks are applied
//   (they are not stored in the JSON); geometric ROI from the currently loaded
//   study config is used when available.
// ---------------------------------------------------------------------------
void MainWindow::rebuildMetricsFromTransforms()
{
    QString outputDir = m_outputDirEdit->text();
    if (outputDir.isEmpty()) outputDir = "./results";

    QDir transformDir(outputDir + "/qc/transforms");
    if (!transformDir.exists()) {
        QMessageBox::warning(this, "No Transform Data",
            "No qc/transforms directory found. Run batch processing first.");
        return;
    }

    QStringList jsonFiles = transformDir.entryList({"*.json"}, QDir::Files);
    if (jsonFiles.isEmpty()) {
        QMessageBox::warning(this, "No Transforms", "No transform JSON files found.");
        return;
    }

    // Get QC errand status so we can build the filtered trueness CSV
    QStringList errandIds = m_qcReviewWidget->errandManager().getErrandIds();
    QSet<QString> errandSet(errandIds.begin(), errandIds.end());

    // --- Phase 1: parse all JSONs into BatchMetricReport + transform --------

    struct ScanEntry {
        DentScanBatch::BatchMetricReport metrics;
        Eigen::Matrix4d                  transform;
    };

    QProgressDialog progress("Reading transform files…", "Cancel",
                             0, jsonFiles.size(), this);
    progress.setWindowModality(Qt::WindowModal);
    progress.setMinimumDuration(0);

    std::vector<ScanEntry> entries;
    entries.reserve(static_cast<std::size_t>(jsonFiles.size()));

    for (int i = 0; i < jsonFiles.size(); ++i) {
        if (progress.wasCanceled()) return;
        progress.setValue(i);

        QFile f(transformDir.absoluteFilePath(jsonFiles[i]));
        if (!f.open(QIODevice::ReadOnly)) continue;
        QJsonDocument doc = QJsonDocument::fromJson(f.readAll());
        if (!doc.isObject()) continue;
        QJsonObject obj = doc.object();

        ScanEntry e;

        // Transform matrix
        QJsonArray ta = obj["transform"].toArray();
        e.transform = Eigen::Matrix4d::Identity();
        for (int r = 0; r < 4 && r < ta.size(); ++r) {
            QJsonArray row = ta[r].toArray();
            for (int c = 0; c < 4 && c < row.size(); ++c)
                e.transform(r, c) = row[c].toDouble();
        }

        // Identification
        e.metrics.scannerName = obj["scanner"].toString().toStdString();
        e.metrics.groupId     = obj["group"].toString();
        e.metrics.conditionValue = obj["condition_value"].toInt(obj["skd_mm"].toInt());
        e.metrics.repetitionId= obj["repetition"].toInt();
        e.metrics.filePath    = obj["file_path"].toString();

        // Per-scan trueness metrics stored directly in JSON
        QJsonObject m = obj["metrics"].toObject();
        e.metrics.rmsDistance       = m["rms_mm"].toDouble();
        e.metrics.madDistance       = m["mad_mm"].toDouble();
        e.metrics.hausdorff95       = m["hausdorff95_mm"].toDouble();
        e.metrics.hausdorff100      = m["hausdorff100_mm"].toDouble();
        e.metrics.signedMean        = m["signed_mean_mm"].toDouble();
        e.metrics.coverageRate      = m["coverage_pct"].toDouble();
        e.metrics.verticesIncluded  = static_cast<std::size_t>(
                                          m["vertices_included"].toVariant().toLongLong());
        e.metrics.verticesTotal     = static_cast<std::size_t>(
                                          m["vertices_total"].toVariant().toLongLong());

        entries.push_back(std::move(e));
    }
    progress.setValue(jsonFiles.size());

    if (entries.empty()) {
        QMessageBox::warning(this, "Parse Error", "Could not parse any transform JSON files.");
        return;
    }

    // --- Phase 2: write trueness CSVs (no mesh loading required) ------------

    std::vector<DentScanBatch::BatchMetricReport> allReports;
    allReports.reserve(entries.size());
    for (const auto& e : entries) allReports.push_back(e.metrics);

    QStringList csvErrors = DentScanBatch::CSVWriter::writeTruenessWithQCFilter(
        allReports, outputDir, errandIds);

    // Also overwrite the full long-format metrics file
    DentScanBatch::CSVWriter::writeTruenessCSV(
        allReports, outputDir + "/long_format_metrics.csv");

    // --- Phase 3: precision — reload STLs, apply transforms, pairwise RMS ---

    // Group entries by groupId
    std::map<QString, std::vector<const ScanEntry*>> byGroup;
    for (const auto& e : entries)
        byGroup[e.metrics.groupId].push_back(&e);

    // Collect ROI configs from loaded study config (keyed by group id)
    std::map<QString, DentScanBatch::ROIConfig> roiByGroup;
    if (m_configLoaded) {
        for (const auto& grp : m_studyConfig.groups)
            roiByGroup[grp.id] = grp.roi;
    }

    std::vector<DentScanBatch::PrecisionReport> allPrecision;

    QProgressDialog precProgress("Computing precision metrics…", "Cancel",
                                  0, static_cast<int>(byGroup.size()), this);
    precProgress.setWindowModality(Qt::WindowModal);
    precProgress.setMinimumDuration(0);
    int groupIdx = 0;

    for (const auto& [groupId, groupEntries] : byGroup) {
        if (precProgress.wasCanceled()) break;
        precProgress.setValue(groupIdx++);
        precProgress.setLabelText(QString("Precision: %1 (%2 scans)…")
            .arg(groupId).arg(groupEntries.size()));
        QApplication::processEvents();

        // Load and align all scans in this group
        struct AlignedScan {
            std::shared_ptr<ScanData> data;
            std::string scannerId;
        };
        std::vector<AlignedScan> alignedScans;
        alignedScans.reserve(groupEntries.size());

        std::string loadError;
        for (const auto* ep : groupEntries) {
            // Skip errands — they shouldn't affect precision
            QString scanId = DentScanBatch::QCExporter::makeScanFilename(
                QString::fromStdString(ep->metrics.scannerName),
                ep->metrics.groupId,
                ep->metrics.repetitionId);
            if (errandSet.contains(scanId)) continue;

            auto scan = STLReader::read(ep->metrics.filePath.toStdString(), loadError);
            if (!scan) continue;

            // Apply stored transform
            for (auto v : scan->mesh.vertices()) {
                Point3& p = scan->mesh.point(v);
                Eigen::Vector4d pt(p.x(), p.y(), p.z(), 1.0);
                Eigen::Vector4d tp = ep->transform * pt;
                p = Point3(tp.x(), tp.y(), tp.z());
            }
            scan->scannerName = ep->metrics.scannerName;
            AlignedScan as;
            as.data      = scan;
            as.scannerId = ep->metrics.scannerName;
            alignedScans.push_back(std::move(as));
        }

        if (alignedScans.size() < 2) continue;

        // Get ROI for this group (or empty = all vertices)
        DentScanBatch::ROIConfig roi;
        auto roiIt = roiByGroup.find(groupId);
        if (roiIt != roiByGroup.end()) roi = roiIt->second;

        // Group by scanner within this condition group
        std::map<std::string, std::vector<std::shared_ptr<ScanData>>> byScanner;
        for (const auto& as : alignedScans)
            byScanner[as.scannerId].push_back(as.data);

        int skd = groupEntries.empty() ? 0 : groupEntries[0]->metrics.conditionValue;

        for (const auto& [scannerId, scannerScans] : byScanner) {
            if (scannerScans.size() < 2) continue;

            // Build one AABB tree per scan, reuse across pairs (O(N) trees)
            std::vector<std::unique_ptr<DistanceField::ReferenceTree>> trees;
            trees.reserve(scannerScans.size());
            for (const auto& s : scannerScans)
                trees.push_back(std::make_unique<DistanceField::ReferenceTree>(s->mesh));

            std::vector<double> pairwiseRMS;

            for (std::size_t i = 0; i < scannerScans.size(); ++i) {
                for (std::size_t j = i + 1; j < scannerScans.size(); ++j) {
                    std::vector<double> dists =
                        trees[j]->computePairwiseDistances(scannerScans[i]->mesh);
                    if (dists.empty()) continue;

                    // Build ROI mask using maxZ of scan i
                    const auto& mesh = scannerScans[i]->mesh;
                    double maxZ = std::numeric_limits<double>::lowest();
                    for (auto v : mesh.vertices())
                        maxZ = std::max(maxZ, mesh.point(v).z());

                    double sq = 0.0;
                    std::size_t cnt = 0;
                    std::size_t vi = 0;
                    for (auto v : mesh.vertices()) {
                        if (vi >= dists.size()) break;
                        const Point3& p = mesh.point(v);
                        // ROIConfig::isInROI returns true for all inactive sub-filters,
                        // so no separate isActive() guard is needed.
                        bool inROI = roi.isInROI(p.x(), p.y(), p.z(), maxZ);
                        if (inROI) {
                            sq += dists[vi] * dists[vi];
                            ++cnt;
                        }
                        ++vi;
                    }
                    if (cnt > 0)
                        pairwiseRMS.push_back(std::sqrt(sq / cnt));
                }
            }

            if (pairwiseRMS.empty()) continue;

            DentScanBatch::PrecisionReport rep;
            rep.scannerId    = QString::fromStdString(scannerId);
            rep.groupId      = groupId;
            rep.conditionValue = skd;
            rep.pairwiseCount= static_cast<int>(pairwiseRMS.size());
            rep.meanRMS      = std::accumulate(pairwiseRMS.begin(),
                                               pairwiseRMS.end(), 0.0)
                               / pairwiseRMS.size();
            if (pairwiseRMS.size() > 1) {
                double sq2 = 0.0;
                for (double r : pairwiseRMS)
                    sq2 += (r - rep.meanRMS) * (r - rep.meanRMS);
                rep.sdRMS = std::sqrt(sq2 / (pairwiseRMS.size() - 1));
                rep.cv    = rep.meanRMS > 0 ? rep.sdRMS / rep.meanRMS : 0.0;
            }
            allPrecision.push_back(rep);
        }
    }
    precProgress.setValue(static_cast<int>(byGroup.size()));

    DentScanBatch::CSVWriter::writePrecisionCSV(
        allPrecision, outputDir + "/precision_matrix.csv");
    DentScanBatch::CSVWriter::writeSummaryCSV(
        allReports, outputDir + "/summary_by_scanner_group.csv");

    QString msg = QString("Rebuilt metrics from %1 scans (%2 errands excluded).\n"
                          "Precision recomputed for %3 scanner×group combinations.\n"
                          "Files updated:\n"
                          "  long_format_metrics.csv\n"
                          "  trueness_metrics_all.csv\n"
                          "  trueness_metrics.csv\n"
                          "  precision_matrix.csv\n"
                          "  summary_by_scanner_group.csv")
        .arg(entries.size())
        .arg(errandIds.size())
        .arg(allPrecision.size());

    if (!csvErrors.isEmpty())
        msg += "\n\nWarnings:\n" + csvErrors.join("\n");

    QMessageBox::information(this, "Rebuild Complete", msg);
    m_statusLabel->setText(QString("Metrics rebuilt from %1 transform files.")
        .arg(entries.size()));
}
