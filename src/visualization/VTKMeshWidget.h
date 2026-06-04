#pragma once

#include "../core/Mesh.h"
#include <QWidget>
#include <QLabel>
#include <vtkSmartPointer.h>
#include <vtkRenderer.h>
#include <vtkPolyData.h>
#include <vtkPolyDataMapper.h>
#include <vtkActor.h>
#include <vtkScalarBarActor.h>
#include <vtkLookupTable.h>
#include <vtkProp3D.h>
#include <vtkOrientationMarkerWidget.h>
#include <vtkGenericOpenGLRenderWindow.h>
#include <Eigen/Core>
#include <QPoint>
#include <array>
#include <memory>
#include <vector>

class QVTKOpenGLNativeWidget;

// A Qt widget embedding a single VTK 3D viewport for one ScanData mesh.
// Supports plain Phong shading, color-mapped distance display, and
// interactive point picking for occlusal-plane fitting.
class VTKMeshWidget : public QWidget
{
    Q_OBJECT
public:
    explicit VTKMeshWidget(QWidget* parent = nullptr);
    ~VTKMeshWidget() override;

    void setMesh(const std::shared_ptr<ScanData>& scan, bool doResetCamera = true);
    void clearMesh();  // Clear all mesh data and stop rendering
    void showDistanceMap(const std::shared_ptr<ScanData>& scan,
                         double rangeMin = -1.0, double rangeMax = 1.0);

    // Same as above but greys out vertices not in toothMask so the colour
    // map focuses on the tooth crown area only.
    void showDistanceMap(const std::shared_ptr<ScanData>& scan,
                         double rangeMin, double rangeMax,
                         const std::vector<bool>& toothMask);
    void showPhongShading();
    void setColorBarVisible(bool visible);
    vtkRenderer* renderer() const { return m_renderer; }
    QString title() const;
    void    setTitle(const QString& t);
    void resetCamera();
    void setOverlayMeshes(const std::vector<std::shared_ptr<ScanData>>& scans);
    void clearOverlayActors();

    // ── occlusal-plane picking ────────────────────────────────────────────
    // Activate/deactivate point-pick mode.  While active, a short left-click
    // (< 6 px movement) picks a surface point and emits pointPicked().
    // Left-click-drag still rotates the camera normally.
    void setPickMode(bool active);
    bool pickMode() const { return m_pickMode; }

    // Render yellow spheres at the given world positions (replaces previous).
    void showPickSpheres(const std::vector<std::array<double,3>>& pts);

    // Render a semi-transparent disk representing the fitted occlusal plane,
    // plus two offset disks at ±above/below mm.
    void showOcclusalPlane(const Eigen::Vector3d& normal,
                           const Eigen::Vector3d& origin,
                           double aboveMm, double belowMm,
                           double radius = 38.0);

    // Remove all pick-related actors (spheres and plane disks).
    void clearPickActors();

    // Show or hide the three occlusal-plane disk actors without removing them.
    void setPlanesVisible(bool visible);

    // ── Offscreen rendering ───────────────────────────────────────────────────
    // Render the current scene to a PNG file (for QC export).
    // @param filePath Output PNG file path
    // @param width Image width in pixels (default 800)
    // @param height Image height in pixels (default 800)
    // @return True if successful
    bool renderToFile(const QString& filePath, int width = 800, int height = 800);

    // Set camera to occlusal (top-down) view.
    // Useful for creating consistent QC difference images.
    void setOcclusalView();

    // Colour-code the mesh by segmentation: tooth = warm ivory,
    // gingiva = dark grey.  Pass an empty mask to revert to plain shading.
    void showToothSegmentation(const std::shared_ptr<ScanData>& scan,
                               const std::vector<bool>& toothMask);

    // Show a wireframe bounding box with the given min/max coordinates.
    void showBoundingBox(const std::array<double, 3>& minPt,
                         const std::array<double, 3>& maxPt);

    // Hide the bounding box wireframe.
    void hideBoundingBox();

    // Show alignment overlay: scan with distance coloring + reference as wireframe.
    // This is useful for QC review to see how well the scan aligns with reference.
    // @param scan The aligned scan with distance values computed
    // @param reference The reference mesh to show as wireframe overlay
    // @param rangeMin Minimum distance value for color mapping (mm)
    // @param rangeMax Maximum distance value for color mapping (mm)
    void showAlignmentOverlay(
        const std::shared_ptr<ScanData>& scan,
        const std::shared_ptr<SurfaceMesh>& reference,
        double rangeMin = -0.5, double rangeMax = 0.5);

    // Hide the reference wireframe overlay (keeps the scan visible).
    void hideReferenceOverlay();

signals:
    // Emitted when the user left-clicks a surface in pick mode.
    void pointPicked(double x, double y, double z);

protected:
    bool eventFilter(QObject* obj, QEvent* event) override;

private:
    void buildPipeline();
    static vtkSmartPointer<vtkPolyData> cgalToVTK(const SurfaceMesh& mesh);

    // Creates a disk actor at origin with the given normal, radius, opacity,
    // and colour (r,g,b in 0–1).
    vtkSmartPointer<vtkActor> makeDiskActor(
        const Eigen::Vector3d& normal,
        const Eigen::Vector3d& center,
        double radius,
        double opacity,
        double r, double g, double b);

    QLabel*                  m_titleLabel  = nullptr;
    QVTKOpenGLNativeWidget*  m_vtkWidget   = nullptr;
    bool                     m_pickMode    = false;
    QPoint                   m_pressPos;              // for click-vs-drag detection

    std::vector<vtkSmartPointer<vtkActor>> m_overlayActors;
    std::vector<vtkSmartPointer<vtkActor>> m_sphereActors; // seed point spheres
    std::vector<vtkSmartPointer<vtkActor>> m_planeActors;  // three occlusal disks
    std::vector<vtkSmartPointer<vtkProp3D>> m_textActors;  // numbered labels for spheres
    vtkSmartPointer<vtkActor>              m_bboxActor;    // wireframe bounding box
    vtkSmartPointer<vtkActor>              m_referenceWireframeActor; // reference mesh wireframe

    vtkSmartPointer<vtkGenericOpenGLRenderWindow> m_renderWindow;
    vtkSmartPointer<vtkRenderer>       m_renderer;
    vtkSmartPointer<vtkPolyData>       m_polyData;
    vtkSmartPointer<vtkPolyDataMapper> m_mapper;
    vtkSmartPointer<vtkActor>          m_actor;
    vtkSmartPointer<vtkScalarBarActor> m_colorBar;
    vtkSmartPointer<vtkOrientationMarkerWidget> m_orientationWidget;
};
