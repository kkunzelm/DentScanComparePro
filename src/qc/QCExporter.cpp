// SPDX-License-Identifier: GPL-2.0-or-later
// SPDX-FileCopyrightText: 2024 Prof. Dr. Karl-Heinz Kunzelmann

#include "QCExporter.h"
#include "../visualization/ColorMapLUT.h"

#include <QDir>
#include <QFile>
#include <QJsonDocument>
#include <QJsonObject>
#include <QJsonArray>

#include <vtkSmartPointer.h>
#include <vtkRenderWindow.h>
#include <vtkRenderer.h>
#include <vtkPolyData.h>
#include <vtkPolyDataMapper.h>
#include <vtkActor.h>
#include <vtkPoints.h>
#include <vtkCellArray.h>
#include <vtkDoubleArray.h>
#include <vtkPointData.h>
#include <vtkPolyDataNormals.h>
#include <vtkProperty.h>
#include <vtkCamera.h>
#include <vtkScalarBarActor.h>
#include <vtkTextProperty.h>
#include <vtkWindowToImageFilter.h>
#include <vtkPNGWriter.h>
#include <vtkUnsignedCharArray.h>

#include <fstream>
#include <cstring>

namespace DentScanBatch {

// Static member initialization
bool QCExporter::s_imageExportEnabled = true;

void QCExporter::setImageExportEnabled(bool enabled) {
    s_imageExportEnabled = enabled;
}

bool QCExporter::isImageExportEnabled() {
    return s_imageExportEnabled;
}

namespace {

// Convert CGAL Surface_mesh to VTK PolyData
vtkSmartPointer<vtkPolyData> cgalToVTK(const SurfaceMesh& mesh)
{
    auto points = vtkSmartPointer<vtkPoints>::New();
    points->SetDataTypeToFloat();
    points->SetNumberOfPoints(static_cast<vtkIdType>(mesh.num_vertices()));

    for (auto v : mesh.vertices()) {
        const Point3& p = mesh.point(v);
        points->SetPoint(static_cast<vtkIdType>(v.idx()), p.x(), p.y(), p.z());
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

    // Compute smooth normals
    auto normFilter = vtkSmartPointer<vtkPolyDataNormals>::New();
    normFilter->SetInputData(polydata);
    normFilter->ComputePointNormalsOn();
    normFilter->ComputeCellNormalsOff();
    normFilter->SplittingOff();
    normFilter->Update();

    return normFilter->GetOutput();
}

// Compute mesh bounds
void computeBounds(const SurfaceMesh& mesh, double bounds[6])
{
    bounds[0] = bounds[2] = bounds[4] = std::numeric_limits<double>::max();
    bounds[1] = bounds[3] = bounds[5] = std::numeric_limits<double>::lowest();

    for (auto v : mesh.vertices()) {
        const Point3& p = mesh.point(v);
        bounds[0] = std::min(bounds[0], p.x());
        bounds[1] = std::max(bounds[1], p.x());
        bounds[2] = std::min(bounds[2], p.y());
        bounds[3] = std::max(bounds[3], p.y());
        bounds[4] = std::min(bounds[4], p.z());
        bounds[5] = std::max(bounds[5], p.z());
    }
}

} // anonymous namespace

bool QCExporter::createQCDirectories(const QString& outputDir)
{
    QDir dir(outputDir);
    if (!dir.exists("qc")) {
        if (!dir.mkdir("qc")) return false;
    }

    QDir qcDir(outputDir + "/qc");
    if (!qcDir.exists("reference_meshes")) {
        if (!qcDir.mkdir("reference_meshes")) return false;
    }
    if (!qcDir.exists("difference_images")) {
        if (!qcDir.mkdir("difference_images")) return false;
    }
    if (!qcDir.exists("transforms")) {
        if (!qcDir.mkdir("transforms")) return false;
    }

    return true;
}

QString QCExporter::makeScanFilename(const QString& scannerName,
                                      const QString& groupId,
                                      int repetition)
{
    // Remove problematic characters from scanner name
    QString safeName = scannerName;
    safeName.replace(QLatin1Char(' '), QLatin1Char('_'));
    safeName.replace(QLatin1Char('/'), QLatin1Char('_'));
    safeName.replace(QLatin1Char('\\'), QLatin1Char('_'));
    safeName.replace(QLatin1Char(':'), QLatin1Char('_'));

    return QString("%1_%2_r%3").arg(safeName, groupId).arg(repetition);
}

bool QCExporter::writeBinarySTL(const SurfaceMesh& mesh, const QString& filePath)
{
    std::ofstream out(filePath.toStdString(), std::ios::binary);
    if (!out) return false;

    // 80-byte header
    char header[80] = {};
    std::strncpy(header, "GPA Mean Mesh - DentScanComparePro", 79);
    out.write(header, 80);

    // Number of triangles
    uint32_t numTriangles = static_cast<uint32_t>(mesh.num_faces());
    out.write(reinterpret_cast<const char*>(&numTriangles), 4);

    // Write each triangle
    for (auto f : mesh.faces()) {
        auto h = mesh.halfedge(f);
        Point3 pts[3];
        int i = 0;
        for (auto v : mesh.vertices_around_face(h))
            pts[i++] = mesh.point(v);

        // Compute face normal
        Vector3K e1(pts[0], pts[1]);
        Vector3K e2(pts[0], pts[2]);
        Vector3K n = CGAL::cross_product(e1, e2);
        double len = std::sqrt(n.squared_length());
        if (len > 1e-12) {
            n = n / len;
        }

        float data[12];
        data[0] = static_cast<float>(n.x());
        data[1] = static_cast<float>(n.y());
        data[2] = static_cast<float>(n.z());
        for (int j = 0; j < 3; ++j) {
            data[3 + j*3 + 0] = static_cast<float>(pts[j].x());
            data[3 + j*3 + 1] = static_cast<float>(pts[j].y());
            data[3 + j*3 + 2] = static_cast<float>(pts[j].z());
        }
        out.write(reinterpret_cast<const char*>(data), 48);

        uint16_t attrib = 0;
        out.write(reinterpret_cast<const char*>(&attrib), 2);
    }

    return out.good();
}

bool QCExporter::exportReferenceMesh(const std::shared_ptr<SurfaceMesh>& mesh,
                                const QString& outputDir,
                                const QString& groupId)
{
    if (!mesh || mesh->is_empty()) return false;

    createQCDirectories(outputDir);

    QString filename = QString("%1_reference.stl").arg(groupId);
    QString fullPath = outputDir + "/qc/reference_meshes/" + filename;

    return writeBinarySTL(*mesh, fullPath);
}

bool QCExporter::exportDifferenceImage(const std::shared_ptr<ScanData>& scan,
                                        const QString& outputDir,
                                        const QString& filename,
                                        double rangeMin, double rangeMax,
                                        const std::vector<bool>& toothMask)
{
    if (!scan || !scan->distanceComputed) return false;

    // Check if image export is enabled (disabled in batch mode)
    if (!s_imageExportEnabled) {
        return false;
    }

    createQCDirectories(outputDir);

    QString fullPath = outputDir + "/qc/difference_images/" + filename + ".png";

    // Create offscreen render window
    auto renderWindow = vtkSmartPointer<vtkRenderWindow>::New();
    renderWindow->SetOffScreenRendering(1);
    renderWindow->SetSize(800, 800);

    // Force initialization of the render window
    renderWindow->Start();

    auto renderer = vtkSmartPointer<vtkRenderer>::New();
    renderer->SetBackground(0.15, 0.15, 0.15);
    renderWindow->AddRenderer(renderer);

    // Convert mesh to VTK
    auto polyData = cgalToVTK(scan->mesh);

    // Setup color mapping
    auto lut = ColorMapLUT::divergingBWR(rangeMin, rangeMax);

    const bool useMask = !toothMask.empty() &&
                         toothMask.size() == scan->mesh.num_vertices();

    if (useMask) {
        // Per-vertex RGBA with mask
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
        polyData->GetPointData()->SetScalars(colors);
    } else {
        // Standard scalar mapping
        auto scalars = vtkSmartPointer<vtkDoubleArray>::New();
        scalars->SetName("Distance");
        scalars->SetNumberOfValues(
            static_cast<vtkIdType>(scan->distanceToRef.size()));
        for (std::size_t i = 0; i < scan->distanceToRef.size(); ++i)
            scalars->SetValue(static_cast<vtkIdType>(i), scan->distanceToRef[i]);
        polyData->GetPointData()->SetScalars(scalars);
    }

    // Mapper
    auto mapper = vtkSmartPointer<vtkPolyDataMapper>::New();
    mapper->SetInputData(polyData);
    if (!useMask) {
        mapper->SetLookupTable(lut);
        mapper->SetScalarRange(rangeMin, rangeMax);
    } else {
        mapper->SetColorModeToDirectScalars();
    }
    mapper->ScalarVisibilityOn();

    // Actor
    auto actor = vtkSmartPointer<vtkActor>::New();
    actor->SetMapper(mapper);
    actor->GetProperty()->SetAmbient(0.1);
    actor->GetProperty()->SetDiffuse(0.7);
    actor->GetProperty()->SetSpecular(0.3);
    actor->GetProperty()->SetSpecularPower(30.0);
    renderer->AddActor(actor);

    // Color bar
    auto colorBar = vtkSmartPointer<vtkScalarBarActor>::New();
    colorBar->SetLookupTable(lut);
    colorBar->SetTitle("mm");
    colorBar->SetOrientationToVertical();
    colorBar->SetWidth(0.08);
    colorBar->SetHeight(0.6);
    colorBar->GetPositionCoordinate()->SetValue(0.88, 0.2);
    colorBar->GetTitleTextProperty()->SetFontSize(10);
    colorBar->GetLabelTextProperty()->SetFontSize(8);
    renderer->AddActor2D(colorBar);

    // Set camera to occlusal (top-down) view
    double bounds[6];
    computeBounds(scan->mesh, bounds);
    double centerX = (bounds[0] + bounds[1]) / 2.0;
    double centerY = (bounds[2] + bounds[3]) / 2.0;
    double centerZ = (bounds[4] + bounds[5]) / 2.0;
    double sizeX = bounds[1] - bounds[0];
    double sizeY = bounds[3] - bounds[2];
    double sizeZ = bounds[5] - bounds[4];
    double maxSize = std::max({sizeX, sizeY, sizeZ});

    auto camera = renderer->GetActiveCamera();
    camera->SetPosition(centerX, centerY, centerZ + maxSize * 2);
    camera->SetFocalPoint(centerX, centerY, centerZ);
    camera->SetViewUp(0, 1, 0);
    camera->SetParallelProjection(true);
    camera->SetParallelScale(maxSize * 0.6);

    // Render
    renderWindow->Render();

    // Capture to image
    auto windowToImage = vtkSmartPointer<vtkWindowToImageFilter>::New();
    windowToImage->SetInput(renderWindow);
    windowToImage->SetScale(1);
    windowToImage->SetInputBufferTypeToRGBA();
    windowToImage->Update();

    // Write PNG
    auto writer = vtkSmartPointer<vtkPNGWriter>::New();
    writer->SetFileName(fullPath.toStdString().c_str());
    writer->SetInputConnection(windowToImage->GetOutputPort());
    writer->Write();

    return true;
}

bool QCExporter::exportTransform(const std::shared_ptr<ScanData>& scan,
                                  const BatchMetricReport& metrics,
                                  const QString& outputDir,
                                  const QString& filename)
{
    if (!scan) return false;

    createQCDirectories(outputDir);

    QString fullPath = outputDir + "/qc/transforms/" + filename + ".json";

    QJsonObject root;

    // Transform matrix as nested array
    QJsonArray transformArray;
    for (int r = 0; r < 4; ++r) {
        QJsonArray row;
        for (int c = 0; c < 4; ++c) {
            row.append(scan->transform(r, c));
        }
        transformArray.append(row);
    }
    root["transform"] = transformArray;

    // Metrics
    QJsonObject metricsObj;
    metricsObj["rms_mm"] = metrics.rmsDistance;
    metricsObj["mad_mm"] = metrics.madDistance;
    metricsObj["hausdorff95_mm"] = metrics.hausdorff95;
    metricsObj["hausdorff100_mm"] = metrics.hausdorff100;
    metricsObj["signed_mean_mm"] = metrics.signedMean;
    metricsObj["coverage_pct"] = metrics.coverageRate;
    metricsObj["vertices_included"] = static_cast<qint64>(metrics.verticesIncluded);
    metricsObj["vertices_total"] = static_cast<qint64>(metrics.verticesTotal);
    root["metrics"] = metricsObj;

    // Identification
    root["scanner"] = QString::fromStdString(metrics.scannerName);
    root["group"] = metrics.groupId;
    root["skd_mm"] = metrics.skd_mm;
    root["repetition"] = metrics.repetitionId;
    root["file_path"] = metrics.filePath;

    QFile file(fullPath);
    if (!file.open(QIODevice::WriteOnly)) return false;

    QJsonDocument doc(root);
    file.write(doc.toJson(QJsonDocument::Indented));
    return true;
}

SurfaceMesh QCExporter::extractSubmesh(
    const SurfaceMesh& mesh,
    const std::vector<bool>& vertexMask)
{
    SurfaceMesh result;

    if (vertexMask.size() != mesh.num_vertices()) {
        return result;  // Invalid mask size
    }

    // Map from old vertex index to new vertex index
    std::vector<SurfaceMesh::Vertex_index> oldToNew(mesh.num_vertices());

    // Add vertices that are in the mask
    std::size_t idx = 0;
    for (auto v : mesh.vertices()) {
        if (vertexMask[idx]) {
            oldToNew[idx] = result.add_vertex(mesh.point(v));
        } else {
            oldToNew[idx] = SurfaceMesh::null_vertex();
        }
        idx++;
    }

    // Add faces where ALL vertices are in the mask
    for (auto f : mesh.faces()) {
        auto h = mesh.halfedge(f);
        SurfaceMesh::Vertex_index newVerts[3];
        int i = 0;
        bool allInMask = true;

        for (auto v : mesh.vertices_around_face(h)) {
            if (oldToNew[v.idx()] == SurfaceMesh::null_vertex()) {
                allInMask = false;
                break;
            }
            newVerts[i++] = oldToNew[v.idx()];
        }

        if (allInMask && i == 3) {
            result.add_face(newVerts[0], newVerts[1], newVerts[2]);
        }
    }

    return result;
}

bool QCExporter::exportSegmentedMesh(
    const std::shared_ptr<ScanData>& scan,
    const std::vector<bool>& toothMask,
    const QString& outputDir,
    const QString& filename)
{
    if (!scan || toothMask.empty()) return false;

    // Create segmented directory
    QDir dir(outputDir);
    if (!dir.exists("qc/segmented")) {
        dir.mkpath("qc/segmented");
    }

    // Extract submesh
    SurfaceMesh segmentedMesh = extractSubmesh(scan->mesh, toothMask);

    if (segmentedMesh.is_empty()) {
        return false;
    }

    QString fullPath = outputDir + "/qc/segmented/" + filename + ".stl";
    return writeBinarySTL(segmentedMesh, fullPath);
}

QStringList QCExporter::exportSegmentedMeshes(
    const std::vector<std::shared_ptr<ScanData>>& scans,
    const std::vector<DiscoveredFile>& files,
    const std::vector<std::vector<bool>>& toothMasks,
    const QString& outputDir)
{
    QStringList errors;

    if (toothMasks.empty()) {
        return errors;  // No masks to apply
    }

    // Create segmented directory
    QDir dir(outputDir);
    if (!dir.exists("qc/segmented")) {
        if (!dir.mkpath("qc/segmented")) {
            errors << "Failed to create qc/segmented directory";
            return errors;
        }
    }

    int exported = 0;
    for (std::size_t i = 0; i < scans.size() && i < files.size(); ++i) {
        if (i >= toothMasks.size() || toothMasks[i].empty()) continue;

        const auto& scan = scans[i];
        const auto& file = files[i];

        // Use same naming convention as transforms
        QString baseName = makeScanFilename(
            file.scannerId,
            file.groupId,
            file.repetitionId);

        if (exportSegmentedMesh(scan, toothMasks[i], outputDir, baseName)) {
            exported++;
        } else {
            errors << QString("Failed to export segmented mesh: %1").arg(baseName);
        }
    }

    return errors;
}

QStringList QCExporter::exportGroupQC(const GroupResult& result,
                                       const std::vector<std::shared_ptr<ScanData>>& scans,
                                       const std::vector<DiscoveredFile>& files,
                                       const QString& outputDir,
                                       const std::vector<std::vector<bool>>& toothMasks,
                                       double rangeMin, double rangeMax)
{
    QStringList errors;

    if (!createQCDirectories(outputDir)) {
        errors << "Failed to create QC directories";
        return errors;
    }

    // Export GPA mean mesh
    if (result.gpaMean && !result.gpaMean->is_empty()) {
        if (!exportReferenceMesh(result.gpaMean, outputDir, result.groupId)) {
            errors << QString("Failed to export GPA mean for %1").arg(result.groupId);
        }
    }

    // Export per-scan data
    int imagesExported = 0;
    for (std::size_t i = 0; i < scans.size() && i < files.size(); ++i) {
        const auto& scan = scans[i];
        const auto& file = files[i];

        if (!scan || !scan->distanceComputed) continue;

        // Find corresponding metrics report
        const BatchMetricReport* metrics = nullptr;
        for (const auto& report : result.truenessReports) {
            if (report.filePath == file.path) {
                metrics = &report;
                break;
            }
        }
        if (!metrics) continue;

        QString baseName = makeScanFilename(
            file.scannerId,
            result.groupId,
            file.repetitionId);

        // Difference image (only if enabled - disabled in batch mode)
        if (s_imageExportEnabled) {
            std::vector<bool> mask;
            if (i < toothMasks.size()) mask = toothMasks[i];

            if (exportDifferenceImage(scan, outputDir, baseName,
                                       rangeMin, rangeMax, mask)) {
                imagesExported++;
            }
        }

        // Transform JSON (always works, no OpenGL needed)
        if (!exportTransform(scan, *metrics, outputDir, baseName)) {
            errors << QString("Failed to export transform: %1").arg(baseName);
        }
    }

    return errors;
}

} // namespace DentScanBatch
