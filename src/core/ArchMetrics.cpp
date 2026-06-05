#include "ArchMetrics.h"

#include <cmath>
#include <numbers>
#include <unordered_set>

namespace ArchMetrics {

namespace {

Vector3K faceNormal(const SurfaceMesh& mesh, FaceDesc f)
{
    auto h = mesh.halfedge(f);
    const Point3& p0 = mesh.point(mesh.source(h));
    const Point3& p1 = mesh.point(mesh.target(h));
    const Point3& p2 = mesh.point(mesh.target(mesh.next(h)));
    return CGAL::cross_product(p1 - p0, p2 - p0);
}

} // namespace

void computeBoundaryMetrics(const ScanData& scan, MetricReport& report)
{
    const auto& mesh = scan.mesh;

    double borderLength = 0.0;
    int    holeCount    = 0;
    std::unordered_set<HalfedgeDesc::size_type> visited;

    for (auto h : mesh.halfedges()) {
        if (!mesh.is_border(h)) continue;
        if (visited.count(h.idx())) continue;

        ++holeCount;
        auto start = h;
        auto cur   = h;
        do {
            visited.insert(cur.idx());
            const Point3& p0 = mesh.point(mesh.source(cur));
            const Point3& p1 = mesh.point(mesh.target(cur));
            Vector3K e = p1 - p0;
            borderLength += std::sqrt(CGAL::to_double(e.squared_length()));
            cur = mesh.next(cur);
        } while (cur != start);
    }

    report.openBoundaryLength = borderLength;
    report.holeCount          = holeCount;
}

void computeStitchingArtifacts(const ScanData& scan, MetricReport& report)
{
    const auto& mesh = scan.mesh;
    double maxAngle = 0.0;

    for (auto e : mesh.edges()) {
        if (mesh.is_border(e)) continue;
        auto h0 = mesh.halfedge(e, 0);
        auto h1 = mesh.halfedge(e, 1);
        FaceDesc f0 = mesh.face(h0);
        FaceDesc f1 = mesh.face(h1);
        if (f0 == SurfaceMesh::null_face() || f1 == SurfaceMesh::null_face()) continue;

        Vector3K n0 = faceNormal(mesh, f0);
        Vector3K n1 = faceNormal(mesh, f1);
        double len0 = std::sqrt(CGAL::to_double(n0.squared_length()));
        double len1 = std::sqrt(CGAL::to_double(n1.squared_length()));
        if (len0 < 1e-10 || len1 < 1e-10) continue;

        double cosA = CGAL::to_double(n0 * n1) / (len0 * len1);
        cosA = std::clamp(cosA, -1.0, 1.0);
        double angle = std::acos(cosA) * 180.0 / std::numbers::pi;
        maxAngle = std::max(maxAngle, angle);
    }

    report.maxStitchingAngle = maxAngle;
}

void computeArchMetrics(const ScanData& scan, MetricReport& report)
{
    // Intermolar distance and arch-form analysis require landmark detection
    // (local curvature maxima in molar regions). These are left as NaN
    // until a landmark-picker UI is implemented.
    (void)scan;
    // report.intermolarDistance remains NaN
    // report.archFormDeviation remains NaN
}

} // namespace ArchMetrics
