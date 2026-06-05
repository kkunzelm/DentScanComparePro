#include "TessellationMetrics.h"

#include <CGAL/Polygon_mesh_processing/measure.h>

#include <algorithm>
#include <cmath>
#include <numeric>

namespace TessellationMetrics {

namespace {

double triangleAspectRatio(const Point3& p0, const Point3& p1, const Point3& p2)
{
    auto edgeLen = [](const Point3& a, const Point3& b) {
        return std::sqrt(CGAL::to_double(CGAL::squared_distance(a, b)));
    };
    double l0 = edgeLen(p0, p1);
    double l1 = edgeLen(p1, p2);
    double l2 = edgeLen(p2, p0);
    double maxL = std::max({l0, l1, l2});
    double minL = std::min({l0, l1, l2});
    return (minL > 0.0) ? maxL / minL : std::numeric_limits<double>::infinity();
}

double triangleArea(const Point3& p0, const Point3& p1, const Point3& p2)
{
    auto v1 = p1 - p0;
    auto v2 = p2 - p0;
    auto cross = CGAL::cross_product(v1, v2);
    return 0.5 * std::sqrt(CGAL::to_double(cross.squared_length()));
}

} // namespace

void compute(ScanData& scan)
{
    const auto& mesh = scan.mesh;

    auto meanMapOpt = mesh.property_map<VertexDesc, double>("v:mean_curv");
    const bool haveCurv = meanMapOpt.has_value();
    auto meanMap = meanMapOpt.has_value()
        ? meanMapOpt.value()
        : SurfaceMesh::Property_map<VertexDesc, double>{};

    std::size_t nFaces = mesh.number_of_faces();
    scan.faceArea.resize(nFaces);
    scan.faceMeanCurv.resize(nFaces, 0.0);
    scan.faceAspectRatio.resize(nFaces);

    std::size_t fi = 0;
    for (auto f : mesh.faces()) {
        auto h = mesh.halfedge(f);
        std::array<VertexDesc, 3> verts;
        int vi = 0;
        for (auto v : mesh.vertices_around_face(h))
            verts[vi++] = v;

        const Point3& p0 = mesh.point(verts[0]);
        const Point3& p1 = mesh.point(verts[1]);
        const Point3& p2 = mesh.point(verts[2]);

        scan.faceArea[fi]        = triangleArea(p0, p1, p2);
        scan.faceAspectRatio[fi] = triangleAspectRatio(p0, p1, p2);

        if (haveCurv) {
            double kH = (std::abs(get(meanMap, verts[0])) +
                         std::abs(get(meanMap, verts[1])) +
                         std::abs(get(meanMap, verts[2]))) / 3.0;
            scan.faceMeanCurv[fi] = kH;
        }
        ++fi;
    }

    scan.tessellationMetricsComputed = true;
}

double spearmanCorrelation(const std::vector<double>& x, const std::vector<double>& y)
{
    if (x.size() != y.size() || x.empty()) return 0.0;
    std::size_t n = x.size();

    auto rank = [&](const std::vector<double>& v) {
        std::vector<std::size_t> idx(n);
        std::iota(idx.begin(), idx.end(), 0);
        std::sort(idx.begin(), idx.end(), [&](std::size_t a, std::size_t b){ return v[a] < v[b]; });
        std::vector<double> r(n);
        for (std::size_t i = 0; i < n; ++i) r[idx[i]] = static_cast<double>(i + 1);
        return r;
    };

    auto rx = rank(x);
    auto ry = rank(y);

    double meanRx = (n + 1.0) / 2.0;
    double meanRy = (n + 1.0) / 2.0;
    double num = 0.0, dx2 = 0.0, dy2 = 0.0;
    for (std::size_t i = 0; i < n; ++i) {
        double drx = rx[i] - meanRx;
        double dry = ry[i] - meanRy;
        num  += drx * dry;
        dx2  += drx * drx;
        dy2  += dry * dry;
    }
    double denom = std::sqrt(dx2 * dy2);
    return (denom > 0.0) ? num / denom : 0.0;
}

void fillReport(const ScanData& scan, MetricReport& report)
{
    if (!scan.tessellationMetricsComputed) return;

    report.scannerName  = scan.scannerName;
    report.triangleCount = scan.triangleCount;

    // mean edge length ≈ sqrt(2 * mean_area) for equilateral triangles
    double totalArea = 0.0;
    for (double a : scan.faceArea) totalArea += a;
    if (!scan.faceArea.empty())
        report.meanEdgeLength = std::sqrt(2.0 * totalArea / scan.faceArea.size());

    // aspect ratio stats
    if (!scan.faceAspectRatio.empty()) {
        report.meanAspectRatio = std::accumulate(scan.faceAspectRatio.begin(),
                                                  scan.faceAspectRatio.end(), 0.0)
                                 / scan.faceAspectRatio.size();
        report.maxAspectRatio = *std::max_element(scan.faceAspectRatio.begin(),
                                                   scan.faceAspectRatio.end());
    }

    // density in high/low curvature zones (threshold = median of |κ_H|)
    if (!scan.faceMeanCurv.empty()) {
        auto sorted = scan.faceMeanCurv;
        std::sort(sorted.begin(), sorted.end());
        double threshold = sorted[sorted.size() / 2];

        double highArea = 0.0, lowArea = 0.0;
        int highCount = 0, lowCount = 0;
        for (std::size_t i = 0; i < scan.faceArea.size(); ++i) {
            if (scan.faceMeanCurv[i] > threshold) {
                highArea += scan.faceArea[i]; ++highCount;
            } else {
                lowArea  += scan.faceArea[i]; ++lowCount;
            }
        }
        report.densityHighCurv = (highArea > 0.0) ? highCount / highArea : 0.0;
        report.densityLowCurv  = (lowArea  > 0.0) ? lowCount  / lowArea  : 0.0;

        // ATI: Spearman(|κ_H|, 1/area)  —  subsample for performance
        const std::size_t maxSamples = 50000;
        if (scan.faceMeanCurv.size() <= maxSamples) {
            std::vector<double> invArea(scan.faceArea.size());
            for (std::size_t i = 0; i < scan.faceArea.size(); ++i)
                invArea[i] = (scan.faceArea[i] > 0.0) ? 1.0 / scan.faceArea[i] : 0.0;
            report.ati = spearmanCorrelation(scan.faceMeanCurv, invArea);
        } else {
            // stratified subsample
            std::vector<std::size_t> idx(scan.faceMeanCurv.size());
            std::iota(idx.begin(), idx.end(), 0);
            std::nth_element(idx.begin(), idx.begin() + maxSamples, idx.end(),
                             [&](std::size_t a, std::size_t b){
                                 return scan.faceMeanCurv[a] < scan.faceMeanCurv[b]; });
            idx.resize(maxSamples);
            std::vector<double> xS, yS;
            xS.reserve(maxSamples); yS.reserve(maxSamples);
            for (auto i : idx) {
                xS.push_back(scan.faceMeanCurv[i]);
                yS.push_back((scan.faceArea[i] > 0.0) ? 1.0 / scan.faceArea[i] : 0.0);
            }
            report.ati = spearmanCorrelation(xS, yS);
        }
    }
}

} // namespace TessellationMetrics
