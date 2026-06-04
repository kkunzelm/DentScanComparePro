#include "VTKMeshWidget.h"
#include "ColorMapLUT.h"

#include <QVTKOpenGLNativeWidget.h>
#include <QVBoxLayout>
#include <QMouseEvent>

#include <vtkPoints.h>
#include <vtkCellArray.h>
#include <vtkDoubleArray.h>
#include <vtkFloatArray.h>
#include <vtkPointData.h>
#include <vtkPolyDataNormals.h>
#include <vtkInteractorStyleTrackballCamera.h>
#include <vtkRenderWindowInteractor.h>
#include <vtkProperty.h>
#include <vtkCamera.h>
#include <vtkTextProperty.h>
#include <vtkCellPicker.h>
#include <vtkSphereSource.h>
#include <vtkDiskSource.h>
#include <vtkCubeSource.h>
#include <vtkOutlineFilter.h>
#include <vtkTransform.h>
#include <vtkTransformPolyDataFilter.h>
#include <vtkMath.h>
#include <vtkUnsignedCharArray.h>

#include <vtkWindowToImageFilter.h>
#include <vtkPNGWriter.h>
#include <vtkBillboardTextActor3D.h>
#include <vtkAxesActor.h>
#include <vtkOrientationMarkerWidget.h>
#include <vtkCaptionActor2D.h>
#include <vtkTextActor.h>

#include <Eigen/Geometry>

VTKMeshWidget::VTKMeshWidget(QWidget* parent)
    : QWidget(parent)
{
    buildPipeline();
}

VTKMeshWidget::~VTKMeshWidget()
{
    // Remove event filter first to prevent any events during destruction
    if (m_vtkWidget) {
        m_vtkWidget->removeEventFilter(this);
    }

    // Clear extra actor vectors first
    m_overlayActors.clear();
    m_sphereActors.clear();
    m_textActors.clear();
    m_planeActors.clear();
    m_bboxActor = nullptr;

    // Remove all props from renderer while it's still valid
    if (m_renderer) {
        m_renderer->RemoveAllViewProps();
    }

    // Disable orientation widget before cleanup
    if (m_orientationWidget) {
        m_orientationWidget->SetEnabled(0);
        m_orientationWidget = nullptr;
    }

    // Clear main VTK pipeline objects
    m_colorBar = nullptr;
    m_actor = nullptr;
    m_mapper = nullptr;
    m_polyData = nullptr;

    // Finalize render window before clearing it (releases OpenGL resources)
    if (m_renderWindow) {
        m_renderWindow->Finalize();
    }

    // Clear renderer and window last
    m_renderer = nullptr;
    m_renderWindow = nullptr;

    // Note: Don't call m_vtkWidget->setRenderWindow(nullptr) - let Qt handle
    // the QVTKOpenGLNativeWidget cleanup naturally when this parent is destroyed.
}

void VTKMeshWidget::buildPipeline()
{
    auto* layout = new QVBoxLayout(this);
    layout->setContentsMargins(2, 2, 2, 2);
    layout->setSpacing(2);

    m_titleLabel = new QLabel(this);
    m_titleLabel->setAlignment(Qt::AlignCenter);
    m_titleLabel->setStyleSheet("font-weight: bold; font-size: 11px;");
    m_titleLabel->setSizePolicy(QSizePolicy::Preferred, QSizePolicy::Fixed);
    layout->addWidget(m_titleLabel, 0);   // no stretch — label stays at its natural height

    m_vtkWidget = new QVTKOpenGLNativeWidget(this);
    m_vtkWidget->setMinimumSize(280, 280);
    layout->addWidget(m_vtkWidget, 1);    // all extra vertical space goes to the 3-D view

    m_renderWindow = vtkSmartPointer<vtkGenericOpenGLRenderWindow>::New();
    m_vtkWidget->setRenderWindow(m_renderWindow);

    m_renderer = vtkSmartPointer<vtkRenderer>::New();
    m_renderer->SetBackground(0.15, 0.15, 0.15);
    m_renderWindow->AddRenderer(m_renderer);

    // pipeline
    m_polyData = vtkSmartPointer<vtkPolyData>::New();
    m_mapper   = vtkSmartPointer<vtkPolyDataMapper>::New();
    m_mapper->SetInputData(m_polyData);
    m_mapper->ScalarVisibilityOff();

    m_actor = vtkSmartPointer<vtkActor>::New();
    m_actor->SetMapper(m_mapper);
    m_actor->GetProperty()->SetColor(0.85, 0.82, 0.78);  // front face: light beige
    m_actor->GetProperty()->SetAmbient(0.1);
    m_actor->GetProperty()->SetDiffuse(0.7);
    m_actor->GetProperty()->SetSpecular(0.3);
    m_actor->GetProperty()->SetSpecularPower(30.0);
    // Backface coloring - darker color when viewing from "inside"
    m_actor->GetProperty()->SetBackfaceCulling(false);
    // Create and set backface property explicitly (GetBackfaceProperty returns nullptr by default)
    auto backfaceProp = vtkSmartPointer<vtkProperty>::New();
    backfaceProp->SetColor(0.4, 0.35, 0.35);  // dark brownish-grey
    backfaceProp->SetAmbient(0.3);
    backfaceProp->SetDiffuse(0.5);
    m_actor->SetBackfaceProperty(backfaceProp);
    m_renderer->AddActor(m_actor);

    // scalar bar (hidden by default)
    m_colorBar = vtkSmartPointer<vtkScalarBarActor>::New();
    m_colorBar->SetOrientationToVertical();
    m_colorBar->SetWidth(0.08);
    m_colorBar->SetHeight(0.6);
    m_colorBar->GetPositionCoordinate()->SetValue(0.88, 0.2);
    m_colorBar->GetTitleTextProperty()->SetFontSize(10);
    m_colorBar->GetLabelTextProperty()->SetFontSize(8);
    m_colorBar->VisibilityOff();
    m_renderer->AddActor2D(m_colorBar);

    // interactor style
    auto style = vtkSmartPointer<vtkInteractorStyleTrackballCamera>::New();
    m_vtkWidget->interactor()->SetInteractorStyle(style);

    // Orientation axes widget in corner (shows X/Y/Z directions)
    // Note: Setup deferred - will be enabled on first setMesh() call
    // to avoid issues with interactor not being ready
    m_orientationWidget = nullptr;

    // event filter intercepts mouse events for pick mode
    m_vtkWidget->installEventFilter(this);
}

// static
vtkSmartPointer<vtkPolyData> VTKMeshWidget::cgalToVTK(const SurfaceMesh& mesh)
{
    auto points = vtkSmartPointer<vtkPoints>::New();
    points->SetDataTypeToFloat();
    points->SetNumberOfPoints(static_cast<vtkIdType>(mesh.num_vertices()));

    for (auto v : mesh.vertices()) {
        const Point3& p = mesh.point(v);
        points->SetPoint(static_cast<vtkIdType>(v.idx()),
                         p.x(), p.y(), p.z());
    }

    auto cells = vtkSmartPointer<vtkCellArray>::New();
    for (auto f : mesh.faces()) {
        auto h = mesh.halfedge(f);
        vtkIdType ids[3];
        int i = 0;
        for (auto vd : mesh.vertices_around_face(h))
            ids[i++] = static_cast<vtkIdType>(vd.idx());
        cells->InsertNextCell(3, ids);
    }

    auto polydata = vtkSmartPointer<vtkPolyData>::New();
    polydata->SetPoints(points);
    polydata->SetPolys(cells);

    // compute smooth normals
    auto normFilter = vtkSmartPointer<vtkPolyDataNormals>::New();
    normFilter->SetInputData(polydata);
    normFilter->ComputePointNormalsOn();
    normFilter->ComputeCellNormalsOff();
    normFilter->SplittingOff();
    normFilter->Update();

    return normFilter->GetOutput();
}

void VTKMeshWidget::setMesh(const std::shared_ptr<ScanData>& scan, bool doResetCamera)
{
    if (!scan) return;

    // Lazy initialization of orientation widget (interactor must be ready)
    if (!m_orientationWidget && m_vtkWidget && m_vtkWidget->interactor()) {
        auto axesActor = vtkSmartPointer<vtkAxesActor>::New();
        axesActor->SetShaftTypeToCylinder();
        axesActor->SetTotalLength(1.5, 1.5, 1.5);
        axesActor->SetCylinderRadius(0.03);
        axesActor->SetConeRadius(0.15);

        m_orientationWidget = vtkSmartPointer<vtkOrientationMarkerWidget>::New();
        m_orientationWidget->SetOrientationMarker(axesActor);
        m_orientationWidget->SetInteractor(m_vtkWidget->interactor());
        m_orientationWidget->SetViewport(0.0, 0.0, 0.18, 0.18);  // bottom-left corner
        m_orientationWidget->SetEnabled(1);
        m_orientationWidget->InteractiveOff();
    }

    auto pd = cgalToVTK(scan->mesh);
    m_polyData->DeepCopy(pd);
    m_polyData->Modified();  // Ensure VTK knows data changed
    m_mapper->SetInputData(m_polyData);
    m_mapper->ScalarVisibilityOff();
    m_actor->GetProperty()->SetColor(0.85, 0.82, 0.78);
    m_colorBar->VisibilityOff();

    std::string info = scan->scannerName
        + "\n" + std::to_string(scan->triangleCount) + " triangles";
    if (!scan->stlHeader.empty())
        info += "\n[" + scan->stlHeader + "]";
    m_titleLabel->setText(QString::fromStdString(info));

    if (doResetCamera) {
        resetCamera();
    }
    m_renderWindow->Render();
}

void VTKMeshWidget::clearMesh()
{
    // Clear all VTK data to prepare for safe destruction
    clearPickActors();
    clearOverlayActors();

    // Reset polydata to empty
    if (m_polyData) {
        m_polyData->Initialize();
    }

    // Hide actors
    if (m_actor) {
        m_actor->VisibilityOff();
    }
    if (m_colorBar) {
        m_colorBar->VisibilityOff();
    }

    // Force a final render to flush any pending operations
    if (m_renderWindow) {
        m_renderWindow->Render();
    }

    m_titleLabel->setText("");
}

void VTKMeshWidget::showDistanceMap(const std::shared_ptr<ScanData>& scan,
                                     double rangeMin, double rangeMax)
{
    if (!scan || !scan->distanceComputed) return;

    // copy distance values into vtkDoubleArray
    auto scalars = vtkSmartPointer<vtkDoubleArray>::New();
    scalars->SetName("Distance [mm]");
    scalars->SetNumberOfValues(
        static_cast<vtkIdType>(scan->distanceToRef.size()));
    for (std::size_t i = 0; i < scan->distanceToRef.size(); ++i)
        scalars->SetValue(static_cast<vtkIdType>(i), scan->distanceToRef[i]);

    m_polyData->GetPointData()->SetScalars(scalars);

    auto lut = ColorMapLUT::divergingBWR(rangeMin, rangeMax);
    m_mapper->SetLookupTable(lut);
    m_mapper->SetScalarRange(rangeMin, rangeMax);
    m_mapper->ScalarVisibilityOn();

    m_colorBar->SetLookupTable(lut);
    m_colorBar->SetTitle("mm");
    m_colorBar->VisibilityOn();

    m_renderWindow->Render();
}

void VTKMeshWidget::showDistanceMap(const std::shared_ptr<ScanData>& scan,
                                     double rangeMin, double rangeMax,
                                     const std::vector<bool>& toothMask)
{
    if (!scan || !scan->distanceComputed) return;

    const bool useMask = !toothMask.empty() &&
                         toothMask.size() == scan->mesh.num_vertices();
    if (!useMask) {
        showDistanceMap(scan, rangeMin, rangeMax);
        return;
    }

    auto lut = ColorMapLUT::divergingBWR(rangeMin, rangeMax);

    // Per-vertex RGBA: tooth → diverging colour from LUT, gingiva → dark grey.
    auto colors = vtkSmartPointer<vtkUnsignedCharArray>::New();
    colors->SetName("MaskedDistance");
    colors->SetNumberOfComponents(4);
    colors->SetNumberOfTuples(
        static_cast<vtkIdType>(scan->distanceToRef.size()));

    for (std::size_t i = 0; i < scan->distanceToRef.size(); ++i) {
        vtkIdType idx = static_cast<vtkIdType>(i);
        if (toothMask[i]) {
            double rgb[3];
            lut->GetColor(scan->distanceToRef[i], rgb);
            colors->SetTuple4(idx,
                static_cast<unsigned char>(rgb[0] * 255.0 + 0.5),
                static_cast<unsigned char>(rgb[1] * 255.0 + 0.5),
                static_cast<unsigned char>(rgb[2] * 255.0 + 0.5),
                255);
        } else {
            colors->SetTuple4(idx, 55, 55, 55, 255);
        }
    }

    m_polyData->GetPointData()->SetScalars(colors);
    m_mapper->SetColorModeToDirectScalars();
    m_mapper->ScalarVisibilityOn();

    // Colour bar still represents the LUT range (tooth area only).
    m_colorBar->SetLookupTable(lut);
    m_colorBar->SetTitle("mm");
    m_colorBar->VisibilityOn();

    m_renderWindow->Render();
}

void VTKMeshWidget::showPhongShading()
{
    m_mapper->ScalarVisibilityOff();
    m_actor->GetProperty()->SetColor(0.85, 0.82, 0.78);
    m_colorBar->VisibilityOff();
    m_renderWindow->Render();
}

void VTKMeshWidget::setColorBarVisible(bool visible)
{
    m_colorBar->SetVisibility(visible ? 1 : 0);
    m_renderWindow->Render();
}

QString VTKMeshWidget::title() const
{
    return m_titleLabel->text();
}

void VTKMeshWidget::setTitle(const QString& t)
{
    m_titleLabel->setText(t);
}

void VTKMeshWidget::resetCamera()
{
    m_renderer->ResetCamera();
    m_renderWindow->Render();
}

void VTKMeshWidget::clearOverlayActors()
{
    for (auto& a : m_overlayActors)
        m_renderer->RemoveActor(a);
    m_overlayActors.clear();
    m_actor->VisibilityOn();
}

void VTKMeshWidget::setOverlayMeshes(
    const std::vector<std::shared_ptr<ScanData>>& scans)
{
    clearOverlayActors();
    m_actor->VisibilityOff(); // hide the single-mesh actor

    auto colors = ColorMapLUT::scannerColors();
    for (std::size_t si = 0; si < scans.size(); ++si) {
        if (!scans[si]) continue;

        auto pd = cgalToVTK(scans[si]->mesh);

        auto mapper = vtkSmartPointer<vtkPolyDataMapper>::New();
        mapper->SetInputData(pd);
        mapper->ScalarVisibilityOff();

        auto actor = vtkSmartPointer<vtkActor>::New();
        actor->SetMapper(mapper);
        std::size_t ci = si % colors.size();
        actor->GetProperty()->SetColor(colors[ci][0], colors[ci][1], colors[ci][2]);
        actor->GetProperty()->SetOpacity(0.45);
        actor->GetProperty()->SetAmbient(0.3);
        actor->GetProperty()->SetDiffuse(0.7);

        m_renderer->AddActor(actor);
        m_overlayActors.push_back(actor);
    }

    m_titleLabel->setText(QString("Overlay – %1 registered scans").arg(scans.size()));
    m_renderer->ResetCamera();
    m_renderWindow->Render();
}

// ── occlusal-plane picking ────────────────────────────────────────────────────

void VTKMeshWidget::setPickMode(bool active)
{
    m_pickMode = active;
    // Change cursor so the user gets visual feedback
    if (active)
        m_vtkWidget->setCursor(Qt::CrossCursor);
    else
        m_vtkWidget->unsetCursor();
}

bool VTKMeshWidget::eventFilter(QObject* obj, QEvent* event)
{
    if (m_pickMode && obj == m_vtkWidget) {
        if (event->type() == QEvent::MouseButtonPress) {
            auto* me = static_cast<QMouseEvent*>(event);
            if (me->button() == Qt::LeftButton) {
                m_pressPos = me->pos();
                // Pass through to VTK so drag-to-rotate still works.
                return false;
            }
        }
        if (event->type() == QEvent::MouseButtonRelease) {
            auto* me = static_cast<QMouseEvent*>(event);
            if (me->button() == Qt::LeftButton) {
                // Only treat as a pick if the mouse barely moved (click, not drag).
                if ((me->pos() - m_pressPos).manhattanLength() < 6) {
                    // Convert from Qt logical pixels to VTK device pixels.
                    // On high-DPI displays, me->pos() returns logical coords but
                    // vtkCellPicker::Pick() expects device (physical) pixels.
                    const qreal dpr = m_vtkWidget->devicePixelRatioF();
                    const int x = static_cast<int>(me->pos().x() * dpr);
                    const int y = static_cast<int>((m_vtkWidget->height() - me->pos().y() - 1) * dpr);
                    vtkNew<vtkCellPicker> picker;
                    picker->SetTolerance(0.0005);
                    if (picker->Pick(x, y, 0, m_renderer)) {
                        double pos[3];
                        picker->GetPickPosition(pos);
                        emit pointPicked(pos[0], pos[1], pos[2]);
                    }
                }
                // Always pass through so VTK resets its rotation state.
                return false;
            }
        }
    }
    return QWidget::eventFilter(obj, event);
}

void VTKMeshWidget::clearPickActors()
{
    for (auto& a : m_sphereActors) m_renderer->RemoveActor(a);
    m_sphereActors.clear();
    for (auto& t : m_textActors) m_renderer->RemoveActor(t);
    m_textActors.clear();
    for (auto& a : m_planeActors)  m_renderer->RemoveActor(a);
    m_planeActors.clear();
    m_renderWindow->Render();
}

void VTKMeshWidget::setPlanesVisible(bool visible)
{
    for (auto& a : m_planeActors)
        a->SetVisibility(visible ? 1 : 0);
    m_renderWindow->Render();
}

void VTKMeshWidget::showPickSpheres(const std::vector<std::array<double,3>>& pts)
{
    // Clear existing spheres and labels
    for (auto& a : m_sphereActors) m_renderer->RemoveActor(a);
    m_sphereActors.clear();
    for (auto& t : m_textActors) m_renderer->RemoveActor(t);
    m_textActors.clear();

    int index = 1;
    for (const auto& pt : pts) {
        // Create sphere
        auto sphere = vtkSmartPointer<vtkSphereSource>::New();
        sphere->SetCenter(pt[0], pt[1], pt[2]);
        sphere->SetRadius(0.6);
        sphere->SetPhiResolution(12);
        sphere->SetThetaResolution(12);

        auto mapper = vtkSmartPointer<vtkPolyDataMapper>::New();
        mapper->SetInputConnection(sphere->GetOutputPort());

        auto actor = vtkSmartPointer<vtkActor>::New();
        actor->SetMapper(mapper);
        actor->GetProperty()->SetColor(1.0, 0.85, 0.0); // yellow
        actor->GetProperty()->SetAmbient(0.4);
        actor->GetProperty()->SetDiffuse(0.6);

        m_renderer->AddActor(actor);
        m_sphereActors.push_back(actor);

        // Create numbered text label (billboard - always faces camera)
        auto textActor = vtkSmartPointer<vtkBillboardTextActor3D>::New();
        textActor->SetInput(std::to_string(index).c_str());
        // Position label slightly above and to the side of the sphere
        textActor->SetPosition(pt[0] + 0.8, pt[1] + 0.8, pt[2] + 0.8);
        textActor->GetTextProperty()->SetFontSize(24);
        textActor->GetTextProperty()->SetColor(1.0, 1.0, 1.0); // white text
        textActor->GetTextProperty()->SetBackgroundColor(0.2, 0.2, 0.8); // blue background
        textActor->GetTextProperty()->SetBackgroundOpacity(0.8);
        textActor->GetTextProperty()->SetBold(true);
        textActor->GetTextProperty()->SetJustificationToCentered();

        m_renderer->AddActor(textActor);
        m_textActors.push_back(textActor);

        ++index;
    }
    m_renderWindow->Render();
}

// Build a flat disk perpendicular to `normal` centred at `center`.
// Uses a vtkDiskSource and a vtkTransform to orient it.
vtkSmartPointer<vtkActor> VTKMeshWidget::makeDiskActor(
    const Eigen::Vector3d& normal,
    const Eigen::Vector3d& center,
    double radius,
    double opacity,
    double r, double g, double b)
{
    auto disk = vtkSmartPointer<vtkDiskSource>::New();
    disk->SetInnerRadius(0.0);
    disk->SetOuterRadius(radius);
    disk->SetCircumferentialResolution(48);
    disk->SetRadialResolution(1);
    disk->Update();

    // vtkDiskSource lies in the XY plane (normal = Z).  Rotate to `normal`.
    Eigen::Vector3d zAxis(0.0, 0.0, 1.0);
    Eigen::Vector3d axis = zAxis.cross(normal);
    double sinAngle = axis.norm();
    double cosAngle = zAxis.dot(normal);
    double angleDeg = std::atan2(sinAngle, cosAngle) * 180.0 / vtkMath::Pi();

    auto transform = vtkSmartPointer<vtkTransform>::New();
    transform->Translate(center.x(), center.y(), center.z());
    if (sinAngle > 1e-6)
        transform->RotateWXYZ(angleDeg,
                              axis.x() / sinAngle,
                              axis.y() / sinAngle,
                              axis.z() / sinAngle);

    auto filter = vtkSmartPointer<vtkTransformPolyDataFilter>::New();
    filter->SetInputConnection(disk->GetOutputPort());
    filter->SetTransform(transform);

    auto mapper = vtkSmartPointer<vtkPolyDataMapper>::New();
    mapper->SetInputConnection(filter->GetOutputPort());

    auto actor = vtkSmartPointer<vtkActor>::New();
    actor->SetMapper(mapper);
    actor->GetProperty()->SetColor(r, g, b);
    actor->GetProperty()->SetOpacity(opacity);
    actor->GetProperty()->LightingOff();
    actor->GetProperty()->SetRepresentationToSurface();
    return actor;
}

void VTKMeshWidget::showOcclusalPlane(const Eigen::Vector3d& normal,
                                       const Eigen::Vector3d& origin,
                                       double aboveMm, double belowMm,
                                       double radius)
{
    // Remove and rebuild plane disk actors (spheres are in m_sphereActors, untouched).
    for (auto& a : m_planeActors) m_renderer->RemoveActor(a);
    m_planeActors.clear();

    // Central plane – grey, 30% opacity
    auto planeActor = makeDiskActor(normal, origin, radius, 0.30, 0.6, 0.6, 0.6);
    m_renderer->AddActor(planeActor);
    m_planeActors.push_back(planeActor);

    // Above-offset plane – green, 20% opacity
    auto aboveActor = makeDiskActor(normal, origin + aboveMm * normal, radius, 0.20,
                                    0.2, 0.8, 0.2);
    m_renderer->AddActor(aboveActor);
    m_planeActors.push_back(aboveActor);

    // Below-offset plane – cyan, 20% opacity
    auto belowActor = makeDiskActor(normal, origin - belowMm * normal, radius, 0.20,
                                    0.2, 0.6, 0.9);
    m_renderer->AddActor(belowActor);
    m_planeActors.push_back(belowActor);

    m_renderWindow->Render();
}

// ── Tooth-segmentation overlay ────────────────────────────────────────────────
// Colours the mesh: tooth crown = warm ivory (255,245,220),
//                   gingiva / unassigned = dark grey (80,80,80).

void VTKMeshWidget::showToothSegmentation(const std::shared_ptr<ScanData>& scan,
                                           const std::vector<bool>& toothMask)
{
    if (!scan) return;

    if (toothMask.empty()) {
        showPhongShading();
        return;
    }

    // Remove multi-scan overlay actors and make the single-mesh actor visible.
    // setOverlayMeshes() hides m_actor; without this the segmentation colours
    // are written to m_polyData but never displayed.
    clearOverlayActors();

    // Rebuild the polydata with per-vertex RGB colours
    auto pd = cgalToVTK(scan->mesh);

    auto colors = vtkSmartPointer<vtkUnsignedCharArray>::New();
    colors->SetName("SegmentationColor");
    colors->SetNumberOfComponents(3);
    colors->SetNumberOfTuples(
        static_cast<vtkIdType>(scan->mesh.num_vertices()));

    for (auto v : scan->mesh.vertices()) {
        vtkIdType idx = static_cast<vtkIdType>(v.idx());
        bool isTooth = (v.idx() < toothMask.size()) && toothMask[v.idx()];
        if (isTooth)
            colors->SetTuple3(idx, 255, 245, 220); // ivory
        else
            colors->SetTuple3(idx,  70,  70,  70); // dark grey
    }
    pd->GetPointData()->SetScalars(colors);

    m_polyData->DeepCopy(pd);
    m_mapper->SetInputData(m_polyData);
    m_mapper->SetColorModeToDirectScalars();
    m_mapper->ScalarVisibilityOn();
    m_colorBar->VisibilityOff();

    const std::size_t nTooth = std::count(toothMask.begin(), toothMask.end(), true);
    m_titleLabel->setText(
        QString::fromStdString(scan->scannerName) +
        QString(" – %1 tooth vertices").arg(nTooth));

    m_renderWindow->Render();
}

// ── Brush zone visualization ────────────────────────────────────────────────────
// Include zones = bright green, Exclude zones = near black

void VTKMeshWidget::showBrushZones(const std::shared_ptr<ScanData>& scan,
                                    const std::vector<DentScanBatch::BrushZone>& zones,
                                    const std::vector<bool>& toothMask)
{
    if (!scan) return;

    if (zones.empty()) {
        // No brush zones - show tooth segmentation or plain shading
        if (!toothMask.empty()) {
            showToothSegmentation(scan, toothMask);
        } else {
            showPhongShading();
        }
        return;
    }

    clearOverlayActors();

    // Rebuild the polydata with per-vertex RGB colours
    auto pd = cgalToVTK(scan->mesh);

    auto colors = vtkSmartPointer<vtkUnsignedCharArray>::New();
    colors->SetName("BrushZoneColor");
    colors->SetNumberOfComponents(3);
    colors->SetNumberOfTuples(
        static_cast<vtkIdType>(scan->mesh.num_vertices()));

    // Color scheme:
    // - Base (no zone): tooth mask ivory (255,245,220) or light gray (180,180,180)
    // - Include zone: bright green (100, 220, 100)
    // - Exclude zone: near black (40, 40, 40)

    for (auto v : scan->mesh.vertices()) {
        vtkIdType idx = static_cast<vtkIdType>(v.idx());
        const Point3& p = scan->mesh.point(v);

        // Check if vertex is in any brush zone (later zones override earlier)
        bool inIncludeZone = false;
        bool inExcludeZone = false;

        for (const auto& zone : zones) {
            if (zone.contains(p.x(), p.y(), p.z())) {
                if (zone.include) {
                    inIncludeZone = true;
                    inExcludeZone = false;
                } else {
                    inExcludeZone = true;
                    inIncludeZone = false;
                }
            }
        }

        if (inIncludeZone) {
            colors->SetTuple3(idx, 100, 220, 100);  // bright green
        } else if (inExcludeZone) {
            colors->SetTuple3(idx, 40, 40, 40);     // near black
        } else {
            // Base color: use tooth mask if available
            bool isTooth = (!toothMask.empty() && v.idx() < toothMask.size() && toothMask[v.idx()]);
            if (isTooth) {
                colors->SetTuple3(idx, 255, 245, 220);  // ivory (tooth)
            } else if (!toothMask.empty()) {
                colors->SetTuple3(idx, 70, 70, 70);     // dark grey (gingiva)
            } else {
                colors->SetTuple3(idx, 180, 180, 180);  // light gray (no mask)
            }
        }
    }

    pd->GetPointData()->SetScalars(colors);

    m_polyData->DeepCopy(pd);
    m_mapper->SetInputData(m_polyData);
    m_mapper->SetColorModeToDirectScalars();
    m_mapper->ScalarVisibilityOn();
    m_colorBar->VisibilityOff();

    // Count vertices in each zone type
    std::size_t nInclude = 0, nExclude = 0;
    for (auto v : scan->mesh.vertices()) {
        const Point3& p = scan->mesh.point(v);
        for (const auto& zone : zones) {
            if (zone.contains(p.x(), p.y(), p.z())) {
                if (zone.include) nInclude++;
                else nExclude++;
                break;  // count each vertex once
            }
        }
    }

    m_titleLabel->setText(
        QString::fromStdString(scan->scannerName) +
        QString(" – Include: %1, Exclude: %2 vertices").arg(nInclude).arg(nExclude));

    m_renderWindow->Render();
}

// ── Bounding box wireframe ─────────────────────────────────────────────────────

void VTKMeshWidget::showBoundingBox(const std::array<double, 3>& minPt,
                                     const std::array<double, 3>& maxPt)
{
    // Remove existing bbox actor if any
    if (m_bboxActor) {
        m_renderer->RemoveActor(m_bboxActor);
        m_bboxActor = nullptr;
    }

    // Create a cube that spans the bounding box
    auto cube = vtkSmartPointer<vtkCubeSource>::New();
    cube->SetBounds(minPt[0], maxPt[0],
                    minPt[1], maxPt[1],
                    minPt[2], maxPt[2]);
    cube->Update();

    // Extract edges as wireframe
    auto outline = vtkSmartPointer<vtkOutlineFilter>::New();
    outline->SetInputConnection(cube->GetOutputPort());

    auto mapper = vtkSmartPointer<vtkPolyDataMapper>::New();
    mapper->SetInputConnection(outline->GetOutputPort());

    m_bboxActor = vtkSmartPointer<vtkActor>::New();
    m_bboxActor->SetMapper(mapper);
    m_bboxActor->GetProperty()->SetColor(1.0, 0.5, 0.0);  // orange
    m_bboxActor->GetProperty()->SetLineWidth(2.0);
    m_bboxActor->GetProperty()->LightingOff();

    m_renderer->AddActor(m_bboxActor);
    m_renderWindow->Render();
}

void VTKMeshWidget::hideBoundingBox()
{
    if (m_bboxActor) {
        m_renderer->RemoveActor(m_bboxActor);
        m_bboxActor = nullptr;
        m_renderWindow->Render();
    }
}

// ── Offscreen rendering ─────────────────────────────────────────────────────────

bool VTKMeshWidget::renderToFile(const QString& filePath, int width, int height)
{
    // Store current size
    int* oldSize = m_renderWindow->GetSize();
    int oldWidth = oldSize[0];
    int oldHeight = oldSize[1];

    // Resize for rendering
    m_renderWindow->SetSize(width, height);
    m_renderWindow->Render();

    // Capture to image
    auto windowToImage = vtkSmartPointer<vtkWindowToImageFilter>::New();
    windowToImage->SetInput(m_renderWindow);
    windowToImage->SetScale(1);
    windowToImage->SetInputBufferTypeToRGBA();
    windowToImage->Update();

    // Write PNG
    auto writer = vtkSmartPointer<vtkPNGWriter>::New();
    writer->SetFileName(filePath.toStdString().c_str());
    writer->SetInputConnection(windowToImage->GetOutputPort());
    writer->Write();

    // Restore original size
    m_renderWindow->SetSize(oldWidth, oldHeight);
    m_renderWindow->Render();

    return true;
}

void VTKMeshWidget::setOcclusalView()
{
    if (!m_polyData || m_polyData->GetNumberOfPoints() == 0) return;

    double bounds[6];
    m_polyData->GetBounds(bounds);

    double centerX = (bounds[0] + bounds[1]) / 2.0;
    double centerY = (bounds[2] + bounds[3]) / 2.0;
    double centerZ = (bounds[4] + bounds[5]) / 2.0;
    double sizeX = bounds[1] - bounds[0];
    double sizeY = bounds[3] - bounds[2];
    double sizeZ = bounds[5] - bounds[4];
    double maxSize = std::max({sizeX, sizeY, sizeZ});

    auto camera = m_renderer->GetActiveCamera();
    camera->SetPosition(centerX, centerY, centerZ + maxSize * 2);
    camera->SetFocalPoint(centerX, centerY, centerZ);
    camera->SetViewUp(0, 1, 0);
    camera->SetParallelProjection(true);
    camera->SetParallelScale(maxSize * 0.6);

    m_renderWindow->Render();
}

// ── Alignment QC Overlay ───────────────────────────────────────────────────────

void VTKMeshWidget::showAlignmentOverlay(
    const std::shared_ptr<ScanData>& scan,
    const std::shared_ptr<SurfaceMesh>& reference,
    double rangeMin, double rangeMax)
{
    if (!scan || !reference) return;

    // First, show the scan with distance coloring
    setMesh(scan, false);  // Don't reset camera yet
    showDistanceMap(scan, rangeMin, rangeMax);

    // Remove any existing reference overlay
    hideReferenceOverlay();

    // Create wireframe representation of the reference mesh
    auto refPolyData = cgalToVTK(*reference);

    auto refMapper = vtkSmartPointer<vtkPolyDataMapper>::New();
    refMapper->SetInputData(refPolyData);
    refMapper->ScalarVisibilityOff();

    m_referenceWireframeActor = vtkSmartPointer<vtkActor>::New();
    m_referenceWireframeActor->SetMapper(refMapper);
    m_referenceWireframeActor->GetProperty()->SetRepresentationToWireframe();
    m_referenceWireframeActor->GetProperty()->SetColor(0.5, 0.5, 0.5);  // grey wireframe
    m_referenceWireframeActor->GetProperty()->SetLineWidth(1.0);
    m_referenceWireframeActor->GetProperty()->SetOpacity(0.5);
    m_referenceWireframeActor->GetProperty()->LightingOff();

    m_renderer->AddActor(m_referenceWireframeActor);
    m_renderer->ResetCamera();
    m_renderWindow->Render();
}

void VTKMeshWidget::hideReferenceOverlay()
{
    if (m_referenceWireframeActor) {
        m_renderer->RemoveActor(m_referenceWireframeActor);
        m_referenceWireframeActor = nullptr;
        m_renderWindow->Render();
    }
}
