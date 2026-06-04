#pragma once

#include "LandmarkRegistration.h"
#include "ErrandManager.h"
#include "../core/Mesh.h"
#include "../visualization/VTKMeshWidget.h"
#include <QDialog>
#include <QLabel>
#include <QPushButton>
#include <QListWidget>
#include <QString>
#include <memory>
#include <vector>

namespace DentScanBatch {

/**
 * Dialog for resolving errand scans via landmark-based re-registration.
 *
 * Workflow:
 * 1. User picks 3+ corresponding points on scan and GPA reference
 * 2. Compute initial alignment from landmarks (Kabsch algorithm)
 * 3. Run ICP refinement
 * 4. Show new difference map
 * 5. User decides: Accept (add to CSV) or Reject (keep excluded)
 *
 * UI Layout (3 panels):
 * ┌──────────────────────────────────────────────────────────────────────────┐
 * │  Re-Register: Scanner_SKDXX_rN                                           │
 * ├──────────────────────┬──────────────────────┬────────────────────────────┤
 * │  GPA REFERENCE       │  SCAN (aligning)     │  DIFFERENCE MAP            │
 * │  ┌────────────────┐  │  ┌────────────────┐  │  ┌────────────────┐        │
 * │  │   [3D mesh]    │  │  │   [3D mesh]    │  │  │   [color map]  │        │
 * │  │   • Pick pts   │  │  │   • Pick pts   │  │  │                │        │
 * │  └────────────────┘  │  └────────────────┘  │  └────────────────┘        │
 * ├──────────────────────┴──────────────────────┴────────────────────────────┤
 * │  [Compute Alignment]  [Run ICP]       [Accept Result]  [Reject / Skip]   │
 * └──────────────────────────────────────────────────────────────────────────┘
 */
class ErrandResolutionDialog : public QDialog {
    Q_OBJECT
public:
    /**
     * Create resolution dialog for an errand scan.
     * @param scanPath Path to original STL file
     * @param gpaMeanPath Path to saved GPA mean STL
     * @param scanId Unique scan identifier
     * @param parent Parent widget
     */
    explicit ErrandResolutionDialog(
        const QString& scanPath,
        const QString& gpaMeanPath,
        const QString& scanId,
        QWidget* parent = nullptr);

    ~ErrandResolutionDialog() override;

    /**
     * Get the result after dialog closes.
     * @return True if user accepted the corrected registration
     */
    bool wasAccepted() const { return m_accepted; }

    /**
     * Get the corrected metrics.
     */
    BatchMetricReport correctedMetrics() const { return m_correctedMetrics; }

    /**
     * Get the new RMS distance after correction.
     */
    double newRMS() const { return m_newRMS; }

private slots:
    void onScanPointPicked(double x, double y, double z);
    void onRefPointPicked(double x, double y, double z);
    void onUndoLastPair();
    void onClearAllPairs();
    void onComputeAlignment();
    void onRunICP();
    void onShowDifferenceMap();
    void onAccept();
    void onReject();

private:
    void setupUI();
    void updateLandmarkList();
    void updateButtonStates();
    void updateDifferenceView();
    void loadMeshes();

    QString m_scanPath;
    QString m_gpaMeanPath;
    QString m_scanId;

    std::shared_ptr<ScanData> m_scan;
    std::shared_ptr<ScanData> m_scanBackup;  // For reset
    std::shared_ptr<SurfaceMesh> m_gpaRef;
    std::shared_ptr<ScanData> m_refData;  // Cached ScanData for reference (avoid copies)

    std::vector<std::array<double, 3>> m_scanLandmarks;
    std::vector<std::array<double, 3>> m_refLandmarks;

    bool m_accepted = false;
    bool m_alignmentComputed = false;
    bool m_icpRefined = false;
    double m_newRMS = 0.0;
    BatchMetricReport m_correctedMetrics;

    // UI elements - three views
    VTKMeshWidget* m_refView = nullptr;   // GPA reference (left)
    VTKMeshWidget* m_scanView = nullptr;  // Scan being aligned (middle)
    VTKMeshWidget* m_diffView = nullptr;  // Difference map (right)

    QLabel* m_refPointsLabel = nullptr;
    QLabel* m_scanPointsLabel = nullptr;
    QLabel* m_diffLabel = nullptr;
    QListWidget* m_landmarkList = nullptr;
    QLabel* m_statusLabel = nullptr;

    QPushButton* m_undoBtn = nullptr;
    QPushButton* m_clearBtn = nullptr;
    QPushButton* m_alignBtn = nullptr;
    QPushButton* m_icpBtn = nullptr;
    QPushButton* m_acceptBtn = nullptr;
    QPushButton* m_rejectBtn = nullptr;
};

} // namespace DentScanBatch
