#pragma once

#include <CGAL/Exact_predicates_inexact_constructions_kernel.h>
#include <CGAL/Surface_mesh.h>
#include <Eigen/Core>
#include <array>
#include <string>
#include <vector>

using Kernel      = CGAL::Exact_predicates_inexact_constructions_kernel;
using Point3      = Kernel::Point_3;
using Vector3K    = Kernel::Vector_3;
using SurfaceMesh = CGAL::Surface_mesh<Point3>;
using VertexDesc  = SurfaceMesh::Vertex_index;
using FaceDesc    = SurfaceMesh::Face_index;
using HalfedgeDesc = SurfaceMesh::Halfedge_index;

struct ScanData {
    // --- identity ---
    std::string filePath;
    std::string scannerName;
    std::string stlHeader;        // raw 80-byte text from binary STL

    // --- geometry ---
    SurfaceMesh mesh;

    // --- curvature (per vertex, computed by CurvatureAnalysis) ---
    // stored as named property maps on mesh: "v:mean_curv", "v:gauss_curv"
    bool curvatureComputed = false;

    // --- per-face metrics (computed by TessellationMetrics) ---
    std::vector<double> faceArea;         // [mm²]
    std::vector<double> faceMeanCurv;     // |κ_H| averaged over 3 vertices
    std::vector<double> faceAspectRatio;  // longest / shortest edge
    bool tessellationMetricsComputed = false;

    // --- registration (computed by ICP/GPA) ---
    Eigen::Matrix4d transform = Eigen::Matrix4d::Identity();
    bool registered = false;

    // --- distance to GPA reference (per vertex) ---
    std::vector<double> distanceToRef;    // signed distance [mm]
    bool distanceComputed = false;

    // --- cached stats (filled on load) ---
    std::size_t triangleCount = 0;
    std::array<double, 3> boundsMin{};
    std::array<double, 3> boundsMax{};
};
