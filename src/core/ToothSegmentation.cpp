#include "ToothSegmentation.h"

#include <Eigen/Geometry>

#include <array>
#include <cmath>
#include <functional>
#include <numbers>
#include <queue>
#include <vector>

namespace ToothSegmentation {

namespace {

// Per-face precomputed geometry

struct FaceData {
    Eigen::Vector3d normal;           // outward unit normal
    Eigen::Vector3d centroid;         // face centroid in world space
    double          meanCurv;         // mean kappa_H averaged over the 3 vertices
    double          minPrincipalCurv; // kappa_min = kappa_H - sqrt(max(0, kappa_H^2 - kappa_G)); valley detector
};

std::vector<FaceData> buildFaceData(const ScanData& scan)
{
    const auto& mesh = scan.mesh;
    std::vector<FaceData> fd(mesh.number_of_faces());

    auto curvMapOpt  = mesh.property_map<VertexDesc, double>("v:mean_curv");
    auto gaussMapOpt = mesh.property_map<VertexDesc, double>("v:gauss_curv");
    const bool haveCurv  = curvMapOpt.has_value();
    const bool haveGauss = gaussMapOpt.has_value();

    for (auto f : mesh.faces()) {
        auto h = mesh.halfedge(f);
        std::array<VertexDesc, 3> vs;
        int i = 0;
        for (auto vd : mesh.vertices_around_face(h)) vs[i++] = vd;

        const Point3& p0 = mesh.point(vs[0]);
        const Point3& p1 = mesh.point(vs[1]);
        const Point3& p2 = mesh.point(vs[2]);

        auto toEigen = [](const Point3& p) {
            return Eigen::Vector3d(CGAL::to_double(p.x()),
                                   CGAL::to_double(p.y()),
                                   CGAL::to_double(p.z()));
        };
        Eigen::Vector3d e0 = toEigen(p0);
        Eigen::Vector3d e1 = toEigen(p1);
        Eigen::Vector3d e2 = toEigen(p2);

        Eigen::Vector3d fn = (e1 - e0).cross(e2 - e0);
        double len = fn.norm();
        Eigen::Vector3d n = (len > 1e-12) ? fn / len : Eigen::Vector3d(0,0,1);

        double kMean = 0.0;
        if (haveCurv) {
            const auto& cm = curvMapOpt.value();
            kMean = (get(cm, vs[0]) + get(cm, vs[1]) + get(cm, vs[2])) / 3.0;
        }

        // kappa_min = kappa_H - sqrt(max(0, kappa_H^2 - kappa_G))
        // More negative than kappa_H at saddle-like CEJ geometry (where kappa_H is small
        // but the surface curves strongly concave in one principal direction).
        // Falls back to kappa_H when Gaussian curvature is unavailable or the
        // discriminant is negative due to floating-point cancellation.
        double kMin = kMean;
        if (haveCurv && haveGauss) {
            const auto& gcm = gaussMapOpt.value();
            double kGauss = (get(gcm, vs[0]) + get(gcm, vs[1]) + get(gcm, vs[2])) / 3.0;
            double disc = kMean * kMean - kGauss;
            if (disc > 0.0)
                kMin = kMean - std::sqrt(disc);
        }

        fd[f.idx()] = { n, (e0 + e1 + e2) / 3.0, kMean, kMin };
    }
    return fd;
}

// Find mesh vertex nearest to a world-space point (O(n) - called once)

VertexDesc nearestVertex(const SurfaceMesh& mesh,
                         const std::array<double,3>& pt)
{
    VertexDesc best;
    double bestSq = std::numeric_limits<double>::infinity();
    for (auto v : mesh.vertices()) {
        const Point3& p = mesh.point(v);
        double dx = CGAL::to_double(p.x()) - pt[0];
        double dy = CGAL::to_double(p.y()) - pt[1];
        double dz = CGAL::to_double(p.z()) - pt[2];
        double sq = dx*dx + dy*dy + dz*dz;
        if (sq < bestSq) { bestSq = sq; best = v; }
    }
    return best;
}

// One incident face of vertex v

FaceDesc incidentFace(const SurfaceMesh& mesh, VertexDesc v)
{
    for (auto h : mesh.halfedges_around_target(mesh.halfedge(v)))
        if (!mesh.is_border(h))
            return mesh.face(h);
    return SurfaceMesh::null_face();
}

// Adjacent faces (sharing an edge)

std::vector<FaceDesc> adjacentFaces(const SurfaceMesh& mesh, FaceDesc f)
{
    std::vector<FaceDesc> nbrs;
    nbrs.reserve(3);
    for (auto h : mesh.halfedges_around_face(mesh.halfedge(f))) {
        auto opp = mesh.opposite(h);
        if (!mesh.is_border(opp))
            nbrs.push_back(mesh.face(opp));
    }
    return nbrs;
}

} // anonymous namespace

// Main segmentation (Dijkstra on face adjacency graph)
//
// Edge cost = centroid-to-centroid distance (geodesic approximation).
// Each face is accepted if:
//   accumulated geodesic distance < params.maxGeodesicMm       (primary)
//   crease angle to previous face  < params.maxCreaseAngleDeg  (secondary)
//   face mean curvature            > params.minMeanCurvature   (secondary)

std::vector<bool> segment(const ScanData& scan,
                          const std::vector<VertexDesc>& seedVertices,
                          const Params& params)
{
    const SurfaceMesh& mesh = scan.mesh;
    const std::size_t  nF   = mesh.number_of_faces();
    const std::size_t  nV   = mesh.num_vertices();

    if (nF == 0 || seedVertices.empty())
        return std::vector<bool>(nV, false);

    const auto fd = buildFaceData(scan);

    const double cosMaxCrease =
        std::cos(params.maxCreaseAngleDeg * std::numbers::pi / 180.0);

    // Best geodesic distance found so far for each face (-1 = not visited)
    std::vector<double> bestDist(nF, -1.0);

    // Priority queue: min-heap on accumulated distance
    struct Entry {
        double   dist;
        FaceDesc face;
        int      seed;
        bool operator>(const Entry& o) const { return dist > o.dist; }
    };
    std::priority_queue<Entry, std::vector<Entry>, std::greater<Entry>> pq;

    // Multi-source: push all seed faces at distance 0
    for (int si = 0; si < static_cast<int>(seedVertices.size()); ++si) {
        FaceDesc sf = incidentFace(mesh, seedVertices[si]);
        if (sf == SurfaceMesh::null_face()) continue;
        if (bestDist[sf.idx()] < 0.0) {
            bestDist[sf.idx()] = 0.0;
            pq.push({0.0, sf, si});
        }
    }

    // Per-seed expansion count (avoids unlimited growth on degenerate meshes)
    std::vector<int> expanded(seedVertices.size(), 0);

    while (!pq.empty()) {
        auto [d, f, seed] = pq.top();
        pq.pop();

        // Stale entry (a shorter path was already found)
        if (d > bestDist[f.idx()] + 1e-9) continue;

        if (expanded[seed] >= params.maxFacesPerSeed) continue;
        ++expanded[seed];

        const FaceData& cur = fd[f.idx()];

        for (FaceDesc nb : adjacentFaces(mesh, f)) {
            const FaceData& nfd = fd[nb.idx()];

            // Primary: curvature-weighted geodesic cost
            // Base cost is centroid-to-centroid physical distance.
            // Concave faces (negative kappa_min) incur an extra multiplicative
            // penalty so the budget is consumed faster near the CEJ and in
            // the gingival sulcus, decelerating expansion there naturally.
            //
            //   W(f,nb) = dist * (1 + curvatureRepulsion * max(0, -kappa_min_avg))
            //
            // kappa_min is more sensitive than kappa_H at saddle-like CEJ geometry and
            // is computed from both principal curvatures via kappa_H and kappa_G.
            // With curvatureRepulsion = 0 this reduces to pure physical distance.
            double physDist = (cur.centroid - nfd.centroid).norm();
            double kMinAvg  = (cur.minPrincipalCurv + nfd.minPrincipalCurv) * 0.5;
            double penalty  = std::max(0.0, -kMinAvg);
            double edgeCost = physDist * (1.0 + params.curvatureRepulsion * penalty);
            double newDist  = d + edgeCost;

            if (newDist > params.maxGeodesicMm) continue;
            if (bestDist[nb.idx()] >= 0.0 && newDist >= bestDist[nb.idx()])
                continue;

            // Secondary hard stops: CEJ kink and gingival sulcus
            // These act as safety nets for abrupt transitions that the
            // curvature-weighted cost alone might not catch quickly enough.
            if (cur.normal.dot(nfd.normal) < cosMaxCrease) continue;
            if (nfd.meanCurv < params.minMeanCurvature) continue;

            bestDist[nb.idx()] = newDist;
            pq.push({newDist, nb, seed});
        }
    }

    // Map face results to per-vertex mask.
    // A vertex is "tooth" when at least one incident face was reached.
    std::vector<bool> toothVertex(nV, false);
    for (auto v : mesh.vertices()) {
        for (auto h : mesh.halfedges_around_target(mesh.halfedge(v))) {
            if (!mesh.is_border(h) &&
                bestDist[mesh.face(h).idx()] >= 0.0) {
                toothVertex[v.idx()] = true;
                break;
            }
        }
    }

    return toothVertex;
}

std::vector<bool> segmentFromPoints(const ScanData& scan,
                                    const std::vector<std::array<double,3>>& pts,
                                    const Params& params)
{
    std::vector<VertexDesc> seeds;
    seeds.reserve(pts.size());
    for (const auto& pt : pts)
        seeds.push_back(nearestVertex(scan.mesh, pt));
    return segment(scan, seeds, params);
}

} // namespace ToothSegmentation
