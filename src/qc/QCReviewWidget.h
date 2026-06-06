// SPDX-License-Identifier: GPL-2.0-or-later
// SPDX-FileCopyrightText: 2024 Prof. Dr. Karl-Heinz Kunzelmann

#pragma once

#include "ErrandManager.h"
#include <QWidget>
#include <QScrollArea>
#include <QLabel>
#include <QPushButton>
#include <QComboBox>
#include <QString>
#include <QStringList>
#include <QPixmap>
#include <map>
#include <memory>
#include <set>

class QFlowLayout;  // Forward declaration

namespace DentScanBatch {

/**
 * Thumbnail widget for a single scan in the QC review grid.
 */
class QCThumbnail : public QWidget {
    Q_OBJECT
public:
    explicit QCThumbnail(const QString& scanId, QWidget* parent = nullptr);

    void setImage(const QPixmap& pixmap);
    void setRMS(double rms);
    void setStatus(QCStatus status);
    void setOutlier(bool isOutlier);
    void setSelected(bool selected);
    bool isSelected() const { return m_selected; }
    QString scanId() const { return m_scanId; }
    QCStatus status() const { return m_status; }

signals:
    void clicked(const QString& scanId);
    void doubleClicked(const QString& scanId);
    void statusChanged(const QString& scanId, QCStatus newStatus);

protected:
    void paintEvent(QPaintEvent* event) override;
    void mousePressEvent(QMouseEvent* event) override;
    void mouseDoubleClickEvent(QMouseEvent* event) override;
    void enterEvent(QEnterEvent* event) override;
    void leaveEvent(QEvent* event) override;

private:
    void updateBorderColor();

    QString m_scanId;
    QCStatus m_status = QCStatus::Pending;
    bool m_outlier = false;
    bool m_selected = false;
    bool m_hovered = false;
    double m_rms = 0.0;

    QLabel* m_imageLabel = nullptr;
    QLabel* m_idLabel = nullptr;
    QLabel* m_rmsLabel = nullptr;
    QLabel* m_statusIcon = nullptr;
};

/**
 * Thumbnail grid widget for QC review of batch results.
 *
 * Features:
 * - Flow layout grid of difference images
 * - Color-coded borders: green=accepted, red=errand, grey=pending
 * - Yellow highlight for statistical outliers (RMS > mean + 2σ)
 * - Click to expand to full 3D view
 * - Double-click to toggle errand status
 * - Multi-select with Ctrl/Shift
 * - Keyboard navigation (arrow keys, Enter to toggle, Escape to deselect)
 *
 * The widget loads thumbnails from qc/difference_images/ and tracks
 * status via ErrandManager.
 */
class QCReviewWidget : public QWidget {
    Q_OBJECT
public:
    explicit QCReviewWidget(QWidget* parent = nullptr);

    /**
     * Load QC data from a results directory.
     * @param qcDir Directory containing qc/ folder
     * @return True if successful
     */
    bool loadFromDirectory(const QString& qcDir);

    /**
     * Refresh thumbnails and status from disk.
     */
    void refresh();

    /**
     * Get the errand manager for direct manipulation.
     */
    ErrandManager& errandManager() { return m_errandManager; }
    const ErrandManager& errandManager() const { return m_errandManager; }

    /**
     * Get currently selected scan IDs.
     */
    QStringList selectedIds() const;

    /**
     * Clear selection.
     */
    void clearSelection();

    /**
     * Get current group filter.
     */
    QString currentGroup() const;

signals:
    /**
     * Emitted when user wants to view a scan in detail.
     * @param scanId Scan identifier
     * @param imagePath Path to difference image
     */
    void viewRequested(const QString& scanId, const QString& imagePath);

    /**
     * Emitted when user wants to re-register an errand.
     * @param scanId Scan identifier
     */
    void reregisterRequested(const QString& scanId);

    /**
     * Emitted when QC status changes.
     */
    void statusChanged();

    /**
     * Emitted when save is requested.
     */
    void saveRequested();

public slots:
    /**
     * Accept all visible (filtered) scans.
     */
    void acceptAllVisible();

    /**
     * Flag selected scans as errands.
     */
    void flagSelectedAsErrands();

    /**
     * Accept selected scans.
     */
    void acceptSelected();

    /**
     * Filter by group.
     * @param groupId Group to show ("All Groups" for no filter)
     */
    void filterByGroup(const QString& groupId);

    /**
     * Save QC status to disk.
     */
    void saveStatus();

private slots:
    void onThumbnailClicked(const QString& scanId);
    void onThumbnailDoubleClicked(const QString& scanId);
    void onGroupFilterChanged(int index);

private:
    void setupUI();
    void loadThumbnails();
    void updateStatusCounts();
    void applyFilter();
    QColor borderColorForStatus(QCStatus status, bool outlier) const;

    QString m_qcDir;
    ErrandManager m_errandManager;
    std::map<QString, QCThumbnail*> m_thumbnails;  // scanId -> widget
    std::set<QString> m_selectedIds;

    // Cached data
    QStringList m_allGroups;
    std::map<QString, QString> m_imagePaths;  // scanId -> image path
    std::map<QString, QString> m_filePaths;   // scanId -> original STL path
    std::map<QString, double> m_rmsValues;    // scanId -> RMS

    // UI elements
    QScrollArea* m_scrollArea = nullptr;
    QWidget* m_gridContainer = nullptr;
    QFlowLayout* m_flowLayout = nullptr;

    QComboBox* m_groupFilter = nullptr;
    QLabel* m_statusLabel = nullptr;
    QPushButton* m_acceptAllBtn = nullptr;
    QPushButton* m_clearSelectionBtn = nullptr;
    QPushButton* m_flagSelectedBtn = nullptr;
    QPushButton* m_acceptSelectedBtn = nullptr;
    QPushButton* m_saveBtn = nullptr;
};

} // namespace DentScanBatch
