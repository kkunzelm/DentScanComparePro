// SPDX-License-Identifier: GPL-2.0-or-later
// SPDX-FileCopyrightText: 2024 Prof. Dr. Karl-Heinz Kunzelmann

#include "QCReviewWidget.h"
#include "QFlowLayout.h"
#include "QCExporter.h"

#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QDir>
#include <QFile>
#include <QJsonDocument>
#include <QJsonObject>
#include <QJsonArray>
#include <QPainter>
#include <QMouseEvent>
#include <QStyleOption>
#include <QMessageBox>
#include <algorithm>

namespace DentScanBatch {

// ============================================================================
// QCThumbnail implementation
// ============================================================================

QCThumbnail::QCThumbnail(const QString& scanId, QWidget* parent)
    : QWidget(parent)
    , m_scanId(scanId)
{
    setFixedSize(140, 160);
    setMouseTracking(true);

    auto* layout = new QVBoxLayout(this);
    layout->setContentsMargins(4, 4, 4, 4);
    layout->setSpacing(2);

    m_imageLabel = new QLabel(this);
    m_imageLabel->setFixedSize(130, 110);
    m_imageLabel->setAlignment(Qt::AlignCenter);
    m_imageLabel->setStyleSheet("background-color: #2a2a2a;");
    layout->addWidget(m_imageLabel);

    m_idLabel = new QLabel(scanId, this);
    m_idLabel->setAlignment(Qt::AlignCenter);
    m_idLabel->setStyleSheet("font-size: 9px; color: #ccc;");
    m_idLabel->setWordWrap(true);
    layout->addWidget(m_idLabel);

    auto* bottomRow = new QHBoxLayout();
    bottomRow->setContentsMargins(0, 0, 0, 0);

    m_rmsLabel = new QLabel("--", this);
    m_rmsLabel->setStyleSheet("font-size: 9px; color: #aaa;");
    bottomRow->addWidget(m_rmsLabel);

    bottomRow->addStretch();

    m_statusIcon = new QLabel(this);
    m_statusIcon->setFixedSize(16, 16);
    bottomRow->addWidget(m_statusIcon);

    layout->addLayout(bottomRow);

    updateBorderColor();
}

void QCThumbnail::setImage(const QPixmap& pixmap)
{
    if (pixmap.isNull()) {
        m_imageLabel->setText("No image");
    } else {
        m_imageLabel->setPixmap(pixmap.scaled(
            m_imageLabel->size(), Qt::KeepAspectRatio, Qt::SmoothTransformation));
    }
}

void QCThumbnail::setRMS(double rms)
{
    m_rms = rms;
    m_rmsLabel->setText(QString("%1 mm").arg(rms, 0, 'f', 3));
}

void QCThumbnail::setStatus(QCStatus status)
{
    m_status = status;

    QString icon;
    switch (status) {
        case QCStatus::Pending:
            icon = "○";
            break;
        case QCStatus::Accepted:
            icon = "✓";
            m_statusIcon->setStyleSheet("color: #4CAF50; font-size: 14px;");
            break;
        case QCStatus::Errand:
            icon = "✗";
            m_statusIcon->setStyleSheet("color: #f44336; font-size: 14px;");
            break;
    }
    m_statusIcon->setText(icon);
    updateBorderColor();
}

void QCThumbnail::setOutlier(bool isOutlier)
{
    m_outlier = isOutlier;
    updateBorderColor();
}

void QCThumbnail::setSelected(bool selected)
{
    m_selected = selected;
    updateBorderColor();
}

void QCThumbnail::paintEvent(QPaintEvent* event)
{
    Q_UNUSED(event);
    QPainter painter(this);

    QColor borderColor;
    int borderWidth = m_selected ? 3 : 2;

    if (m_selected) {
        borderColor = QColor(0x21, 0x96, 0xF3);  // Blue for selected
    } else if (m_status == QCStatus::Accepted) {
        borderColor = QColor(0x4C, 0xAF, 0x50);  // Green
    } else if (m_status == QCStatus::Errand) {
        borderColor = QColor(0xF4, 0x43, 0x36);  // Red
    } else if (m_outlier) {
        borderColor = QColor(0xFF, 0xEB, 0x3B);  // Yellow for outliers
    } else {
        borderColor = QColor(0x60, 0x60, 0x60);  // Grey
    }

    if (m_hovered) {
        borderColor = borderColor.lighter(130);
    }

    painter.setPen(QPen(borderColor, borderWidth));
    painter.setBrush(QColor(0x1e, 0x1e, 0x1e));
    painter.drawRoundedRect(rect().adjusted(1, 1, -1, -1), 4, 4);
}

void QCThumbnail::mousePressEvent(QMouseEvent* event)
{
    if (event->button() == Qt::LeftButton) {
        emit clicked(m_scanId);
    }
    QWidget::mousePressEvent(event);
}

void QCThumbnail::mouseDoubleClickEvent(QMouseEvent* event)
{
    if (event->button() == Qt::LeftButton) {
        emit doubleClicked(m_scanId);
    }
    QWidget::mouseDoubleClickEvent(event);
}

void QCThumbnail::enterEvent(QEnterEvent* event)
{
    m_hovered = true;
    update();
    QWidget::enterEvent(event);
}

void QCThumbnail::leaveEvent(QEvent* event)
{
    m_hovered = false;
    update();
    QWidget::leaveEvent(event);
}

void QCThumbnail::updateBorderColor()
{
    update();
}

// ============================================================================
// QCReviewWidget implementation
// ============================================================================

QCReviewWidget::QCReviewWidget(QWidget* parent)
    : QWidget(parent)
{
    setupUI();
}

void QCReviewWidget::setupUI()
{
    auto* mainLayout = new QVBoxLayout(this);
    mainLayout->setContentsMargins(8, 8, 8, 8);

    // Top toolbar
    auto* toolbar = new QHBoxLayout();

    toolbar->addWidget(new QLabel("Group:"));
    m_groupFilter = new QComboBox(this);
    m_groupFilter->setMinimumWidth(120);
    connect(m_groupFilter, QOverload<int>::of(&QComboBox::currentIndexChanged),
            this, &QCReviewWidget::onGroupFilterChanged);
    toolbar->addWidget(m_groupFilter);

    toolbar->addStretch();

    m_statusLabel = new QLabel(this);
    m_statusLabel->setStyleSheet("font-size: 11px;");
    toolbar->addWidget(m_statusLabel);

    toolbar->addStretch();

    m_acceptAllBtn = new QPushButton("Accept All Visible", this);
    connect(m_acceptAllBtn, &QPushButton::clicked, this, &QCReviewWidget::acceptAllVisible);
    toolbar->addWidget(m_acceptAllBtn);

    m_clearSelectionBtn = new QPushButton("Clear Selection", this);
    connect(m_clearSelectionBtn, &QPushButton::clicked, this, &QCReviewWidget::clearSelection);
    toolbar->addWidget(m_clearSelectionBtn);

    m_acceptSelectedBtn = new QPushButton("Accept Selected", this);
    connect(m_acceptSelectedBtn, &QPushButton::clicked, this, &QCReviewWidget::acceptSelected);
    toolbar->addWidget(m_acceptSelectedBtn);

    m_flagSelectedBtn = new QPushButton("Flag as Errand", this);
    m_flagSelectedBtn->setStyleSheet("background-color: #d32f2f; color: white;");
    connect(m_flagSelectedBtn, &QPushButton::clicked, this, &QCReviewWidget::flagSelectedAsErrands);
    toolbar->addWidget(m_flagSelectedBtn);

    m_reregisterSelectedBtn = new QPushButton("Re-align Selected", this);
    m_reregisterSelectedBtn->setStyleSheet("background-color: #e65100; color: white;");
    m_reregisterSelectedBtn->setToolTip("Open the re-alignment dialog for each selected scan in sequence");
    connect(m_reregisterSelectedBtn, &QPushButton::clicked,
            this, &QCReviewWidget::reregisterSelectedErrands);
    toolbar->addWidget(m_reregisterSelectedBtn);

    m_saveBtn = new QPushButton("Save QC Status", this);
    connect(m_saveBtn, &QPushButton::clicked, this, &QCReviewWidget::saveStatus);
    toolbar->addWidget(m_saveBtn);

    mainLayout->addLayout(toolbar);

    // Thumbnail grid (scrollable)
    m_scrollArea = new QScrollArea(this);
    m_scrollArea->setWidgetResizable(true);
    m_scrollArea->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);

    m_gridContainer = new QWidget();
    m_flowLayout = new QFlowLayout(m_gridContainer, 8, 8, 8);
    m_gridContainer->setLayout(m_flowLayout);

    m_scrollArea->setWidget(m_gridContainer);
    mainLayout->addWidget(m_scrollArea, 1);
}

bool QCReviewWidget::loadFromDirectory(const QString& qcDir)
{
    m_qcDir = qcDir;

    // Load QC status (creates empty if doesn't exist)
    if (!m_errandManager.load(qcDir)) {
        return false;
    }

    m_imagePaths.clear();
    m_rmsValues.clear();
    m_allGroups.clear();
    m_filePaths.clear();

    QSet<QString> groupSet;

    // Primary source: load from transforms directory
    QDir transformDir(qcDir + "/qc/transforms");
    if (transformDir.exists()) {
        QStringList jsonFiles = transformDir.entryList(QStringList() << "*.json", QDir::Files);
        for (const QString& file : jsonFiles) {
            QString scanId = file;
            scanId.chop(5);  // Remove .json

            QString jsonPath = transformDir.absoluteFilePath(file);
            QFile jsonFile(jsonPath);
            if (jsonFile.open(QIODevice::ReadOnly)) {
                QJsonDocument doc = QJsonDocument::fromJson(jsonFile.readAll());
                if (doc.isObject()) {
                    QJsonObject obj = doc.object();
                    QJsonObject metrics = obj["metrics"].toObject();
                    double rms = metrics["rms_mm"].toDouble();
                    m_rmsValues[scanId] = rms;

                    QString groupId = obj["group"].toString();
                    if (!groupId.isEmpty()) {
                        groupSet.insert(groupId);
                    }

                    QString filePath = obj["file_path"].toString();
                    if (!filePath.isEmpty()) {
                        m_filePaths[scanId] = filePath;
                    }

                    // Initialize errand manager record if not already present
                    QCRecord existingRecord = m_errandManager.getRecord(scanId);
                    if (existingRecord.scanId.isEmpty()) {
                        // Create full record from transform JSON
                        m_errandManager.initializeRecord(
                            scanId, filePath, groupId,
                            metrics["rms_mm"].toDouble());
                    }
                }
            }
        }
    }

    // Secondary source: check for difference images (optional)
    QDir imgDir(qcDir + "/qc/difference_images");
    if (imgDir.exists()) {
        QStringList pngFiles = imgDir.entryList(QStringList() << "*.png", QDir::Files);
        for (const QString& file : pngFiles) {
            QString scanId = file;
            scanId.chop(4);  // Remove .png
            m_imagePaths[scanId] = imgDir.absoluteFilePath(file);
        }
    }

    // Check if we found any data
    if (m_rmsValues.empty()) {
        return false;  // No transforms found
    }

    m_allGroups = groupSet.values();
    m_allGroups.sort();

    // Update group filter
    m_groupFilter->clear();
    m_groupFilter->addItem("All Groups");
    for (const QString& group : m_allGroups) {
        m_groupFilter->addItem(group);
    }

    loadThumbnails();
    updateStatusCounts();

    return true;
}

void QCReviewWidget::refresh()
{
    if (!m_qcDir.isEmpty()) {
        loadFromDirectory(m_qcDir);
    }
}

void QCReviewWidget::loadThumbnails()
{
    // Clear existing thumbnails
    for (auto& [id, thumb] : m_thumbnails) {
        m_flowLayout->removeWidget(thumb);
        delete thumb;
    }
    m_thumbnails.clear();
    m_selectedIds.clear();

    // Get outlier IDs for highlighting
    QStringList outlierIds = m_errandManager.getOutlierIds(2.0);
    QSet<QString> outlierSet(outlierIds.begin(), outlierIds.end());

    // Create thumbnails
    for (const auto& [scanId, imagePath] : m_imagePaths) {
        auto* thumb = new QCThumbnail(scanId, m_gridContainer);

        QPixmap pixmap(imagePath);
        thumb->setImage(pixmap);

        auto it = m_rmsValues.find(scanId);
        if (it != m_rmsValues.end()) {
            thumb->setRMS(it->second);
        }

        thumb->setStatus(m_errandManager.getStatus(scanId));
        thumb->setOutlier(outlierSet.contains(scanId));

        connect(thumb, &QCThumbnail::clicked,
                this, &QCReviewWidget::onThumbnailClicked);
        connect(thumb, &QCThumbnail::doubleClicked,
                this, &QCReviewWidget::onThumbnailDoubleClicked);

        m_flowLayout->addWidget(thumb);
        m_thumbnails[scanId] = thumb;
    }

    applyFilter();
}

void QCReviewWidget::updateStatusCounts()
{
    int pending = m_errandManager.countPending();
    int accepted = m_errandManager.countAccepted();
    int errands = m_errandManager.countErrands();

    m_statusLabel->setText(QString(
        "Pending: %1  |  Accepted: %2  |  Errands: %3")
        .arg(pending).arg(accepted).arg(errands));
}

void QCReviewWidget::applyFilter()
{
    QString currentGroup = m_groupFilter->currentText();
    bool showAll = (currentGroup == "All Groups");

    for (auto& [scanId, thumb] : m_thumbnails) {
        if (showAll) {
            thumb->setVisible(true);
        } else {
            QCRecord record = m_errandManager.getRecord(scanId);
            thumb->setVisible(record.groupId == currentGroup);
        }
    }
}

QStringList QCReviewWidget::selectedIds() const
{
    QStringList ids;
    for (const QString& id : m_selectedIds) {
        ids << id;
    }
    return ids;
}

void QCReviewWidget::clearSelection()
{
    for (const QString& id : m_selectedIds) {
        auto it = m_thumbnails.find(id);
        if (it != m_thumbnails.end()) {
            it->second->setSelected(false);
        }
    }
    m_selectedIds.clear();
}

QString QCReviewWidget::currentGroup() const
{
    return m_groupFilter->currentText();
}

void QCReviewWidget::onThumbnailClicked(const QString& scanId)
{
    // Toggle selection
    auto it = m_thumbnails.find(scanId);
    if (it == m_thumbnails.end()) return;

    if (m_selectedIds.count(scanId) > 0) {
        m_selectedIds.erase(scanId);
        it->second->setSelected(false);
    } else {
        m_selectedIds.insert(scanId);
        it->second->setSelected(true);
    }
}

void QCReviewWidget::onThumbnailDoubleClicked(const QString& scanId)
{
    auto it = m_thumbnails.find(scanId);
    if (it == m_thumbnails.end()) return;

    // On double-click, open the detailed view dialog
    // (AlignmentQCDialog is connected via MainWindow)
    auto imgIt = m_imagePaths.find(scanId);
    QString imagePath = (imgIt != m_imagePaths.end()) ? imgIt->second : QString();
    emit viewRequested(scanId, imagePath);
}

void QCReviewWidget::onGroupFilterChanged(int /*index*/)
{
    applyFilter();
}

void QCReviewWidget::acceptAllVisible()
{
    for (auto& [scanId, thumb] : m_thumbnails) {
        if (thumb->isVisible() && thumb->status() == QCStatus::Pending) {
            m_errandManager.setStatus(scanId, QCStatus::Accepted);
            thumb->setStatus(QCStatus::Accepted);
        }
    }
    updateStatusCounts();
    emit statusChanged();
}

void QCReviewWidget::reregisterSelectedErrands()
{
    if (m_selectedIds.empty()) {
        QMessageBox::information(this, "Re-align", "No scans selected. Click thumbnails to select.");
        return;
    }

    QStringList ids;
    for (const QString& id : m_selectedIds) {
        ids << id;
    }
    emit batchReregisterRequested(ids);
}

void QCReviewWidget::flagSelectedAsErrands()
{
    for (const QString& scanId : m_selectedIds) {
        m_errandManager.setStatus(scanId, QCStatus::Errand);
        auto it = m_thumbnails.find(scanId);
        if (it != m_thumbnails.end()) {
            it->second->setStatus(QCStatus::Errand);
        }
    }
    clearSelection();
    updateStatusCounts();
    emit statusChanged();
}

void QCReviewWidget::acceptSelected()
{
    for (const QString& scanId : m_selectedIds) {
        m_errandManager.setStatus(scanId, QCStatus::Accepted);
        auto it = m_thumbnails.find(scanId);
        if (it != m_thumbnails.end()) {
            it->second->setStatus(QCStatus::Accepted);
        }
    }
    clearSelection();
    updateStatusCounts();
    emit statusChanged();
}

void QCReviewWidget::filterByGroup(const QString& groupId)
{
    int index = m_groupFilter->findText(groupId);
    if (index >= 0) {
        m_groupFilter->setCurrentIndex(index);
    }
}

void QCReviewWidget::saveStatus()
{
    if (m_qcDir.isEmpty()) {
        QMessageBox::warning(this, "Error", "No QC directory loaded");
        return;
    }

    bool ok = m_errandManager.save(m_qcDir);
    ok = m_errandManager.saveErrandsList(m_qcDir) && ok;

    if (ok) {
        emit saveRequested();
    } else {
        QMessageBox::warning(this, "Error", "Failed to save QC status");
    }
}

QColor QCReviewWidget::borderColorForStatus(QCStatus status, bool outlier) const
{
    if (status == QCStatus::Accepted) {
        return QColor(0x4C, 0xAF, 0x50);  // Green
    } else if (status == QCStatus::Errand) {
        return QColor(0xF4, 0x43, 0x36);  // Red
    } else if (outlier) {
        return QColor(0xFF, 0xEB, 0x3B);  // Yellow
    } else {
        return QColor(0x60, 0x60, 0x60);  // Grey
    }
}

} // namespace DentScanBatch
