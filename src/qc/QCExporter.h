// SPDX-License-Identifier: GPL-2.0-or-later
// SPDX-FileCopyrightText: 2024 Prof. Dr. Karl-Heinz Kunzelmann

#pragma once

#include "../core/Mesh.h"
#include "../batch/GroupProcessor.h"
#include <QString>
#include <Eigen/Core>
#include <memory>
#include <vector>

namespace DentScanBatch {

/**
 * Exports QC data from batch processing for visual verification and reproducibility.
 *
 * Output structure:
 *   qc/
 *   ├── reference_meshes/    - Reference meshes (one per group)
 *   │   └── GroupID_reference.stl
 *   ├── difference_images/   - Color-coded distance maps (PNG)
 *   │   └── Scanner_GroupID_rN.png
 *   └── transforms/          - Registration transforms (JSON)
 *       └── Scanner_GroupID_rN.json
 */
class QCExporter {
public:
    /**
     * Export a reference mesh as STL file.
     * @param mesh The reference surface mesh (GPA mean or external reference)
     * @param outputDir Base output directory (qc/reference_meshes/ will be appended)
     * @param groupId Group identifier (e.g., "SKD_20", "condition_A")
     * @return True if successful
     */
    static bool exportReferenceMesh(
        const std::shared_ptr<SurfaceMesh>& mesh,
        const QString& outputDir,
        const QString& groupId);

    /**
     * Export a difference image as PNG (occlusal top-down view).
     * Uses offscreen VTK rendering with color bar.
     * @param scan Scan with computed distanceToRef
     * @param outputDir Base output directory (qc/difference_images/ will be appended)
     * @param filename Output filename (without path)
     * @param rangeMin Color map minimum (mm)
     * @param rangeMax Color map maximum (mm)
     * @param toothMask Optional mask to grey out non-tooth regions
     * @return True if successful
     */
    static bool exportDifferenceImage(
        const std::shared_ptr<ScanData>& scan,
        const QString& outputDir,
        const QString& filename,
        double rangeMin = -0.5,
        double rangeMax = 0.5,
        const std::vector<bool>& toothMask = {});

    /**
     * Export transform matrix and metrics as JSON.
     * @param scan Scan with transform and metrics
     * @param metrics Computed metrics for this scan
     * @param outputDir Base output directory (qc/transforms/ will be appended)
     * @param filename Output filename (without path)
     * @return True if successful
     */
    static bool exportTransform(
        const std::shared_ptr<ScanData>& scan,
        const BatchMetricReport& metrics,
        const QString& outputDir,
        const QString& filename);

    /**
     * Export all QC data for a group result.
     * Convenience method that calls the individual export functions.
     * @param result Group processing result
     * @param scans Vector of processed scans with distances
     * @param files Original discovered files (for naming)
     * @param outputDir Base output directory
     * @param toothMasks Per-scan tooth masks (empty vector = no masking)
     * @param rangeMin Color map minimum
     * @param rangeMax Color map maximum
     * @return List of errors (empty if successful)
     */
    static QStringList exportGroupQC(
        const GroupResult& result,
        const std::vector<std::shared_ptr<ScanData>>& scans,
        const std::vector<DiscoveredFile>& files,
        const QString& outputDir,
        const std::vector<std::vector<bool>>& toothMasks = {},
        double rangeMin = -0.5,
        double rangeMax = 0.5);

    /**
     * Create the QC directory structure.
     * @param outputDir Base output directory
     * @return True if successful
     */
    static bool createQCDirectories(const QString& outputDir);

    /**
     * Generate a safe filename from scanner name, group, and repetition.
     * @param scannerName Scanner identifier
     * @param groupId Group identifier
     * @param repetition Repetition number
     * @return Safe filename (no extension)
     */
    static QString makeScanFilename(
        const QString& scannerName,
        const QString& groupId,
        int repetition);

    /**
     * Enable or disable difference image export.
     * Useful for batch mode where VTK offscreen rendering may not work.
     * @param enabled True to enable, false to disable
     */
    static void setImageExportEnabled(bool enabled);

    /**
     * Check if image export is enabled.
     */
    static bool isImageExportEnabled();

    /**
     * Extract a submesh containing only vertices where mask is true.
     * Creates a new mesh with only the faces whose ALL vertices are in the mask.
     * @param mesh Input mesh
     * @param vertexMask Boolean mask (true = include vertex)
     * @return New mesh containing only masked region
     */
    static SurfaceMesh extractSubmesh(
        const SurfaceMesh& mesh,
        const std::vector<bool>& vertexMask);

    /**
     * Export a segmented (tooth-only) mesh as STL.
     * Uses the tooth mask to extract only tooth crown vertices/faces.
     * @param scan Scan data with mesh
     * @param toothMask Vertex mask from tooth segmentation
     * @param outputDir Base output directory (qc/segmented/ will be appended)
     * @param filename Output filename (without path)
     * @return True if successful
     */
    static bool exportSegmentedMesh(
        const std::shared_ptr<ScanData>& scan,
        const std::vector<bool>& toothMask,
        const QString& outputDir,
        const QString& filename);

    /**
     * Export all segmented meshes for a group.
     * @param scans Vector of processed scans
     * @param files Original discovered files (for naming)
     * @param toothMasks Per-scan tooth masks
     * @param outputDir Base output directory
     * @return List of errors (empty if successful)
     */
    static QStringList exportSegmentedMeshes(
        const std::vector<std::shared_ptr<ScanData>>& scans,
        const std::vector<DiscoveredFile>& files,
        const std::vector<std::vector<bool>>& toothMasks,
        const QString& outputDir);

    /**
     * Export aligned scan mesh as STL (geometry after ICP, before metric computation).
     * Saved to qc/aligned_meshes/<filename>.stl
     */
    static bool exportAlignedMesh(
        const std::shared_ptr<ScanData>& scan,
        const QString& outputDir,
        const QString& filename);

    /**
     * Export scan mesh as PLY with per-vertex RGB (blue-white-red colormap) and
     * signed distance scalar. Opens colored immediately in MeshLab.
     * Saved to qc/difference_meshes/<filename>.ply
     * @param colorRangeMax Distance range for colormap (mm); values beyond ±rangeMax clamp to blue/red.
     */
    static bool exportDifferencePLY(
        const std::shared_ptr<ScanData>& scan,
        const QString& outputDir,
        const QString& filename,
        double colorRangeMax = 0.5);

private:
    // Write CGAL Surface_mesh as binary STL
    static bool writeBinarySTL(
        const SurfaceMesh& mesh,
        const QString& filePath);

    // Write CGAL Surface_mesh as binary PLY with per-vertex float scalar and
    // pre-computed RGB (blue-white-red colormap) so MeshLab shows color on open.
    static bool writeBinaryPLY(
        const SurfaceMesh& mesh,
        const std::vector<double>& perVertexScalar,
        const QString& scalarName,
        const QString& filePath,
        double colorRangeMax = 0.5);

    static bool s_imageExportEnabled;
};

} // namespace DentScanBatch
