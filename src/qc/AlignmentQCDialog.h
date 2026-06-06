// SPDX-License-Identifier: GPL-2.0-or-later
// SPDX-FileCopyrightText: 2024 Prof. Dr. Karl-Heinz Kunzelmann

#pragma once

#include "../core/Mesh.h"
#include <QDialog>
#include <QLabel>
#include <QPushButton>
#include <QCheckBox>
#include <memory>

class VTKMeshWidget;

namespace DentScanBatch {

/**
 * Dialog for detailed QC review of a single scan alignment.
 *
 * Shows:
 * - Reference mesh as grey wireframe
 * - Scan with distance color mapping
 * - Metrics summary (RMS, Max, Coverage)
 * - Accept/Flag/Skip buttons
 *
 * This dialog is triggered by double-clicking a thumbnail in QCReviewWidget.
 */
class AlignmentQCDialog : public QDialog {
    Q_OBJECT

public:
    /**
     * Create the dialog.
     * @param scanPath Path to the aligned scan STL
     * @param referencePath Path to the reference mesh (GPA mean) STL
     * @param scanId Identifier for the scan
     * @param parent Parent widget
     */
    explicit AlignmentQCDialog(
        const QString& scanPath,
        const QString& referencePath,
        const QString& scanId,
        QWidget* parent = nullptr);

    ~AlignmentQCDialog() override;

    /**
     * Load and display the meshes with distance coloring.
     * @param transformJson Optional path to transform JSON to apply to scan
     * @return True if successful
     */
    bool loadMeshes(const QString& transformJson = QString());

    /**
     * Set the pre-computed metrics to display.
     */
    void setMetrics(double rms, double maxDist, double coverage);

    /**
     * Get the dialog result.
     * @return "accept", "flag", or "skip"
     */
    QString result() const { return m_result; }

    /**
     * Check if user accepted the alignment.
     */
    bool wasAccepted() const { return m_result == "accept"; }

    /**
     * Check if user flagged as errand.
     */
    bool wasFlagged() const { return m_result == "flag"; }

private slots:
    void onAcceptClicked();
    void onFlagClicked();
    void onSkipClicked();
    void onShowReferenceToggled(bool checked);

private:
    void setupUI();

    QString m_scanPath;
    QString m_referencePath;
    QString m_scanId;
    QString m_result = "skip";

    std::shared_ptr<ScanData> m_scanData;
    std::shared_ptr<SurfaceMesh> m_referenceMesh;

    // UI elements
    VTKMeshWidget* m_meshWidget = nullptr;
    QCheckBox* m_showReferenceChk = nullptr;
    QLabel* m_rmsLabel = nullptr;
    QLabel* m_maxLabel = nullptr;
    QLabel* m_coverageLabel = nullptr;
    QPushButton* m_acceptBtn = nullptr;
    QPushButton* m_flagBtn = nullptr;
    QPushButton* m_skipBtn = nullptr;
};

} // namespace DentScanBatch
