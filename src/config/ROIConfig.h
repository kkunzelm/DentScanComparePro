#ifndef ROICONFIG_H
#define ROICONFIG_H

#include <QString>
#include <array>
#include <vector>

namespace DentScanBatch {

/**
 * Axis-aligned bounding box for ROI clipping.
 * Vertices outside this box are excluded from analysis.
 */
struct BoundingBox {
    std::array<double, 3> min{0.0, 0.0, 0.0};
    std::array<double, 3> max{0.0, 0.0, 0.0};
    bool active = false;

    bool contains(double x, double y, double z) const {
        if (!active) return true;
        return x >= min[0] && x <= max[0] &&
               y >= min[1] && y <= max[1] &&
               z >= min[2] && z <= max[2];
    }

    bool contains(const std::array<double, 3>& pt) const {
        return contains(pt[0], pt[1], pt[2]);
    }
};

/**
 * Z-plane slab for restricting analysis to occlusal region.
 * Vertices are included if they fall within [Z_occlusal - below_mm, Z_occlusal + above_mm].
 * Z_occlusal is typically computed as the maximum Z value or from a fitted occlusal plane.
 */
struct ZPlaneSlab {
    double above_mm = 2.0;   // Distance above occlusal plane to include
    double below_mm = 12.0;  // Distance below occlusal plane to include
    bool active = false;  // Default to inactive - user must explicitly enable

    /**
     * Check if a Z coordinate is within the slab.
     * @param z The Z coordinate to test
     * @param z_occlusal The Z coordinate of the occlusal plane/surface
     */
    bool contains(double z, double z_occlusal) const {
        if (!active) return true;
        return z >= (z_occlusal - below_mm) && z <= (z_occlusal + above_mm);
    }
};

/**
 * Brush zone for manual inclusion/exclusion of vertices.
 * Applied after geometric ROI (bounding box + Z-plane).
 */
struct BrushZone {
    std::array<double, 3> center{0.0, 0.0, 0.0};
    double radius_mm = 2.0;
    bool include = true;  // true = force include, false = force exclude

    bool contains(double x, double y, double z) const {
        double dx = x - center[0];
        double dy = y - center[1];
        double dz = z - center[2];
        return (dx*dx + dy*dy + dz*dz) <= (radius_mm * radius_mm);
    }

    bool contains(const std::array<double, 3>& pt) const {
        return contains(pt[0], pt[1], pt[2]);
    }
};

/**
 * Complete ROI configuration combining all filtering methods.
 * Methods are applied in priority order:
 * 1. Bounding box clips to rectangular region
 * 2. Z-plane slab further restricts within the box
 * 3. Brush zones add/remove specific vertices
 * 4. Statistical σ-clip removes remaining outliers (applied during metrics computation)
 */
struct ROIConfig {
    BoundingBox bbox;
    ZPlaneSlab zPlane;
    std::vector<BrushZone> brushZones;
    double outlierSigma = 3.0;  // σ threshold for statistical outlier removal

    /**
     * Check if a vertex should be included based on geometric ROI.
     * Does NOT apply σ-clip (that requires distance values).
     * @param x, y, z Vertex coordinates
     * @param z_occlusal Z coordinate of the occlusal plane
     * @return true if vertex passes geometric ROI tests
     */
    bool isInROI(double x, double y, double z, double z_occlusal) const {
        // Start with geometric tests
        bool inROI = bbox.contains(x, y, z) && zPlane.contains(z, z_occlusal);

        // Brush zones can override geometric ROI
        for (const auto& zone : brushZones) {
            if (zone.contains(x, y, z)) {
                inROI = zone.include;
            }
        }

        return inROI;
    }

    bool isInROI(const std::array<double, 3>& pt, double z_occlusal) const {
        return isInROI(pt[0], pt[1], pt[2], z_occlusal);
    }
};

/**
 * Occlusal plane representation for Z-slab computations.
 * Fitted from picked points or detected automatically.
 */
struct OcclusalPlane {
    std::array<double, 3> point{0.0, 0.0, 0.0};   // Point on plane
    std::array<double, 3> normal{0.0, 0.0, 1.0};  // Plane normal (typically near +Z)
    bool active = false;

    /**
     * Compute signed distance from a point to the plane.
     * Positive = above plane (in direction of normal).
     */
    double signedDistance(double x, double y, double z) const {
        return normal[0] * (x - point[0]) +
               normal[1] * (y - point[1]) +
               normal[2] * (z - point[2]);
    }

    double signedDistance(const std::array<double, 3>& pt) const {
        return signedDistance(pt[0], pt[1], pt[2]);
    }
};

/**
 * Complete ROI template including tooth segmentation settings.
 * Can be saved/loaded from JSON for batch processing.
 */
struct ROITemplate {
    ROIConfig roi;

    // Tooth segmentation settings
    bool useToothMask = false;
    std::vector<std::array<double, 3>> toothSeeds;
    double segMaxGeodesicMm = 12.0;
    double segMaxCreaseAngleDeg = 50.0;
    double segMinMeanCurvature = -4.0;
    double segCurvatureRepulsion = 0.1;

    /**
     * Load ROI template from JSON file.
     * @param filePath Path to JSON file
     * @return Loaded template (throws on error)
     */
    static ROITemplate loadFromFile(const QString& filePath);

    /**
     * Save ROI template to JSON file.
     * @param filePath Path to JSON file
     * @return True if successful
     */
    bool saveToFile(const QString& filePath) const;
};

} // namespace DentScanBatch

#endif // ROICONFIG_H
