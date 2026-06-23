// SPDX-License-Identifier: GPL-2.0-or-later
// SPDX-FileCopyrightText: 2024 Prof. Dr. Karl-Heinz Kunzelmann

#include "DistanceField.h"

#include <CGAL/AABB_tree.h>
#include <CGAL/AABB_traits_3.h>
#include <CGAL/AABB_face_graph_triangle_primitive.h>

#include <algorithm>
#include <cmath>
#include <numeric>
#include <iostream>

namespace DistanceField {

namespace {

using Primitive  = CGAL::AABB_face_graph_triangle_primitive<SurfaceMesh>;
using AABBTraits = CGAL::AABB_traits_3<Kernel, Primitive>;
using AABBTree   = CGAL::AABB_tree<AABBTraits>;

} // namespace

// ─── ReferenceTree implementation ────────────────────────────────────────────

class ReferenceTreeImpl {
public:
    explicit ReferenceTreeImpl(const SurfaceMesh& mesh)
        : m_mesh(mesh)
        , m_tree(faces(m_mesh).first, faces(m_mesh).second, m_mesh)
        , m_hasCoverageMask(false)
    {
        std::cout << "      Building AABB tree for reference ("
                  << m_mesh.number_of_faces() << " triangles)..." << std::flush;
        m_tree.accelerate_distance_queries();
        std::cout << " done\n" << std::flush;
    }

    ReferenceTreeImpl(const SurfaceMesh& mesh, const std::vector<bool>& faceCoverageMask)
        : m_mesh(mesh)
        , m_tree(faces(m_mesh).first, faces(m_mesh).second, m_mesh)
        , m_faceCoverageMask(faceCoverageMask)
        , m_hasCoverageMask(!faceCoverageMask.empty() &&
                            faceCoverageMask.size() == mesh.number_of_faces())
    {
        std::cout << "      Building AABB tree for reference ("
                  << m_mesh.number_of_faces() << " triangles";
        if (m_hasCoverageMask) {
            std::size_t coveredFaces = std::count(m_faceCoverageMask.begin(),
                                                   m_faceCoverageMask.end(), true);
            std::cout << ", " << coveredFaces << " with coverage";
        }
        std::cout << ")..." << std::flush;
        m_tree.accelerate_distance_queries();
        std::cout << " done\n" << std::flush;
    }

    void computeDistances(ScanData& scan) const {
        std::size_t nVerts = scan.mesh.num_vertices();
        scan.distanceToRef.resize(nVerts);

        // Populate validity mask if we have a coverage mask
        if (m_hasCoverageMask) {
            scan.distanceValidMask.resize(nVerts, false);
        } else {
            scan.distanceValidMask.clear();
        }

        for (auto v : scan.mesh.vertices()) {
            const Point3& p = scan.mesh.point(v);
            auto result = m_tree.closest_point_and_primitive(p);
            const Point3& closestPt = result.first;
            FaceDesc      primId    = result.second;

            double dist = std::sqrt(CGAL::to_double(
                CGAL::squared_distance(p, closestPt)));

            // sign: dot(p - closestPt, face_normal)
            auto hh = m_mesh.halfedge(primId);
            const Point3& fp0 = m_mesh.point(m_mesh.source(hh));
            const Point3& fp1 = m_mesh.point(m_mesh.target(hh));
            const Point3& fp2 = m_mesh.point(m_mesh.target(m_mesh.next(hh)));
            Vector3K fn = CGAL::cross_product(fp1 - fp0, fp2 - fp0);

            Vector3K diff(p.x() - closestPt.x(),
                          p.y() - closestPt.y(),
                          p.z() - closestPt.z());
            double dot = CGAL::to_double(diff * fn);
            scan.distanceToRef[v.idx()] = (dot >= 0.0) ? dist : -dist;

            // Mark validity based on coverage mask
            if (m_hasCoverageMask) {
                scan.distanceValidMask[v.idx()] = m_faceCoverageMask[primId.idx()];
            }
        }

        scan.distanceComputed = true;
    }

    const SurfaceMesh& mesh() const { return m_mesh; }
    bool hasCoverageMask() const { return m_hasCoverageMask; }

    // Compute pairwise distances from sourceMesh to this cached reference
    std::vector<double> computePairwiseDistances(const SurfaceMesh& sourceMesh) const {
        std::size_t nVerts = sourceMesh.num_vertices();
        std::vector<double> distances(nVerts);

        for (auto v : sourceMesh.vertices()) {
            const Point3& p = sourceMesh.point(v);
            auto result = m_tree.closest_point_and_primitive(p);
            const Point3& closestPt = result.first;
            FaceDesc      primId    = result.second;

            double dist = std::sqrt(CGAL::to_double(
                CGAL::squared_distance(p, closestPt)));

            // sign: dot(p - closestPt, face_normal)
            auto hh = m_mesh.halfedge(primId);
            const Point3& fp0 = m_mesh.point(m_mesh.source(hh));
            const Point3& fp1 = m_mesh.point(m_mesh.target(hh));
            const Point3& fp2 = m_mesh.point(m_mesh.target(m_mesh.next(hh)));
            Vector3K fn = CGAL::cross_product(fp1 - fp0, fp2 - fp0);

            Vector3K diff(p.x() - closestPt.x(),
                          p.y() - closestPt.y(),
                          p.z() - closestPt.z());
            double dot = CGAL::to_double(diff * fn);
            distances[v.idx()] = (dot >= 0.0) ? dist : -dist;
        }

        return distances;
    }

    // Compute pairwise distances with validity output
    std::vector<double> computePairwiseDistances(const SurfaceMesh& sourceMesh,
                                                  std::vector<bool>& validityOut) const {
        std::size_t nVerts = sourceMesh.num_vertices();
        std::vector<double> distances(nVerts);

        if (m_hasCoverageMask) {
            validityOut.resize(nVerts, false);
        } else {
            validityOut.clear();
        }

        for (auto v : sourceMesh.vertices()) {
            const Point3& p = sourceMesh.point(v);
            auto result = m_tree.closest_point_and_primitive(p);
            const Point3& closestPt = result.first;
            FaceDesc      primId    = result.second;

            double dist = std::sqrt(CGAL::to_double(
                CGAL::squared_distance(p, closestPt)));

            // sign: dot(p - closestPt, face_normal)
            auto hh = m_mesh.halfedge(primId);
            const Point3& fp0 = m_mesh.point(m_mesh.source(hh));
            const Point3& fp1 = m_mesh.point(m_mesh.target(hh));
            const Point3& fp2 = m_mesh.point(m_mesh.target(m_mesh.next(hh)));
            Vector3K fn = CGAL::cross_product(fp1 - fp0, fp2 - fp0);

            Vector3K diff(p.x() - closestPt.x(),
                          p.y() - closestPt.y(),
                          p.z() - closestPt.z());
            double dot = CGAL::to_double(diff * fn);
            distances[v.idx()] = (dot >= 0.0) ? dist : -dist;

            if (m_hasCoverageMask) {
                validityOut[v.idx()] = m_faceCoverageMask[primId.idx()];
            }
        }

        return distances;
    }

private:
    const SurfaceMesh& m_mesh;
    AABBTree m_tree;
    std::vector<bool> m_faceCoverageMask;
    bool m_hasCoverageMask;
};

ReferenceTree::ReferenceTree(const SurfaceMesh& referenceMesh)
    : m_impl(std::make_unique<ReferenceTreeImpl>(referenceMesh))
{
}

ReferenceTree::ReferenceTree(const SurfaceMesh& referenceMesh,
                             const std::vector<bool>& faceCoverageMask)
    : m_impl(std::make_unique<ReferenceTreeImpl>(referenceMesh, faceCoverageMask))
{
}

ReferenceTree::~ReferenceTree() = default;

ReferenceTree::ReferenceTree(ReferenceTree&&) noexcept = default;
ReferenceTree& ReferenceTree::operator=(ReferenceTree&&) noexcept = default;

void ReferenceTree::computeDistances(ScanData& scan) const {
    m_impl->computeDistances(scan);
}

const SurfaceMesh& ReferenceTree::mesh() const {
    return m_impl->mesh();
}

bool ReferenceTree::hasCoverageMask() const {
    return m_impl->hasCoverageMask();
}

std::vector<double> ReferenceTree::computePairwiseDistances(const SurfaceMesh& sourceMesh) const {
    return m_impl->computePairwiseDistances(sourceMesh);
}

std::vector<double> ReferenceTree::computePairwiseDistances(const SurfaceMesh& sourceMesh,
                                                             std::vector<bool>& validityOut) const {
    return m_impl->computePairwiseDistances(sourceMesh, validityOut);
}

// ─── computePairwiseWithTree function ────────────────────────────────────────

std::vector<double> computePairwiseWithTree(const SurfaceMesh& sourceMesh,
                                             const ReferenceTree& targetTree) {
    return targetTree.computePairwiseDistances(sourceMesh);
}

// ─── Original compute function ───────────────────────────────────────────────

void compute(ScanData& scan, const ScanData& reference)
{
    const SurfaceMesh& refMesh = reference.mesh;

    AABBTree tree(faces(refMesh).first, faces(refMesh).second, refMesh);
    tree.accelerate_distance_queries();

    std::size_t nVerts = scan.mesh.num_vertices();
    scan.distanceToRef.resize(nVerts);

    for (auto v : scan.mesh.vertices()) {
        const Point3& p = scan.mesh.point(v);
        auto result = tree.closest_point_and_primitive(p);
        const Point3& closestPt = result.first;
        FaceDesc      primId    = result.second;

        double dist = std::sqrt(CGAL::to_double(
            CGAL::squared_distance(p, closestPt)));

        // sign: dot(p - closestPt, face_normal)
        // compute face normal on the fly to avoid non-const property map
        auto hh = refMesh.halfedge(primId);
        const Point3& fp0 = refMesh.point(refMesh.source(hh));
        const Point3& fp1 = refMesh.point(refMesh.target(hh));
        const Point3& fp2 = refMesh.point(refMesh.target(refMesh.next(hh)));
        Vector3K fn = CGAL::cross_product(fp1 - fp0, fp2 - fp0);

        Vector3K diff(p.x() - closestPt.x(),
                      p.y() - closestPt.y(),
                      p.z() - closestPt.z());
        double dot = CGAL::to_double(diff * fn);
        scan.distanceToRef[v.idx()] = (dot >= 0.0) ? dist : -dist;
    }

    scan.distanceComputed = true;
}

std::vector<double> computePairwise(const SurfaceMesh& sourceMesh,
                                     const SurfaceMesh& targetMesh)
{
    // Build AABB tree for target mesh
    AABBTree tree(faces(targetMesh).first, faces(targetMesh).second, targetMesh);
    tree.accelerate_distance_queries();

    std::size_t nVerts = sourceMesh.num_vertices();
    std::vector<double> distances(nVerts);

    for (auto v : sourceMesh.vertices()) {
        const Point3& p = sourceMesh.point(v);
        auto result = tree.closest_point_and_primitive(p);
        const Point3& closestPt = result.first;
        FaceDesc      primId    = result.second;

        double dist = std::sqrt(CGAL::to_double(
            CGAL::squared_distance(p, closestPt)));

        // sign: dot(p - closestPt, face_normal)
        auto hh = targetMesh.halfedge(primId);
        const Point3& fp0 = targetMesh.point(targetMesh.source(hh));
        const Point3& fp1 = targetMesh.point(targetMesh.target(hh));
        const Point3& fp2 = targetMesh.point(targetMesh.target(targetMesh.next(hh)));
        Vector3K fn = CGAL::cross_product(fp1 - fp0, fp2 - fp0);

        Vector3K diff(p.x() - closestPt.x(),
                      p.y() - closestPt.y(),
                      p.z() - closestPt.z());
        double dot = CGAL::to_double(diff * fn);
        distances[v.idx()] = (dot >= 0.0) ? dist : -dist;
    }

    return distances;
}

void fillReport(const ScanData& scan, MetricReport& report,
                double coverageThreshold, double zWindowMm,
                const OcclusalPlane& plane,
                const std::vector<bool>& toothMask)
{
    if (!scan.distanceComputed || scan.distanceToRef.empty()) return;

    // Check if we have a coverage validity mask from the reference
    const bool haveCoverageMask = !scan.distanceValidMask.empty() &&
                                   scan.distanceValidMask.size() == scan.mesh.num_vertices();

    // Helper lambda to check if a vertex passes the coverage filter
    auto passesRefCoverage = [&](std::size_t idx) {
        return !haveCoverageMask || scan.distanceValidMask[idx];
    };

    // ── build the distance sample set ─────────────────────────────────────
    // Priority: segmentation mask > plane slab > Z-window > all vertices.
    // All filters are combined with the reference coverage mask when available.
    std::vector<double> d;
    d.reserve(scan.distanceToRef.size());

    const bool haveMask = !toothMask.empty() &&
                          toothMask.size() == scan.mesh.num_vertices();

    if (haveMask) {
        // Tooth-segmentation mask: most anatomically accurate filter
        for (auto v : scan.mesh.vertices()) {
            if (!toothMask[v.idx()]) continue;
            if (!passesRefCoverage(v.idx())) continue;
            d.push_back(scan.distanceToRef[v.idx()]);
        }
    } else if (plane.active) {
        // Plane-based filter: keep vertices within [-belowMm, +aboveMm]
        // along the plane normal.
        for (auto v : scan.mesh.vertices()) {
            if (!passesRefCoverage(v.idx())) continue;
            const Point3& p = scan.mesh.point(v);
            Eigen::Vector3d pt(CGAL::to_double(p.x()),
                               CGAL::to_double(p.y()),
                               CGAL::to_double(p.z()));
            double dist = plane.normal.dot(pt - plane.origin);
            if (dist < -plane.belowMm || dist > plane.aboveMm) continue;
            d.push_back(scan.distanceToRef[v.idx()]);
        }
    } else if (zWindowMm > 0.0) {
        // Legacy simple Z-window: keep top zWindowMm below Z_max.
        double zMax = -std::numeric_limits<double>::infinity();
        for (auto v : scan.mesh.vertices())
            zMax = std::max(zMax, CGAL::to_double(scan.mesh.point(v).z()));
        const double zThresh = zMax - zWindowMm;
        for (auto v : scan.mesh.vertices()) {
            if (!passesRefCoverage(v.idx())) continue;
            if (CGAL::to_double(scan.mesh.point(v).z()) < zThresh) continue;
            d.push_back(scan.distanceToRef[v.idx()]);
        }
    } else {
        // All vertices (still filtered by coverage mask if available).
        for (auto v : scan.mesh.vertices()) {
            if (!passesRefCoverage(v.idx())) continue;
            d.push_back(scan.distanceToRef[v.idx()]);
        }
    }

    if (d.empty()) return;

    const std::size_t n = d.size();

    // RMS
    double rms2 = 0.0;
    for (double v : d) rms2 += v * v;
    report.rmsDistance = std::sqrt(rms2 / n);

    // signed mean
    double sum = std::accumulate(d.begin(), d.end(), 0.0);
    report.signedMean = sum / n;

    // absolute values for MAD and Hausdorff
    std::vector<double> abs_d(n);
    for (std::size_t i = 0; i < n; ++i) abs_d[i] = std::abs(d[i]);
    std::sort(abs_d.begin(), abs_d.end());

    report.madDistance  = abs_d[n / 2];
    report.hausdorff100 = abs_d.back();
    report.hausdorff95  = abs_d[static_cast<std::size_t>(0.95 * n)];

    // coverage rate
    std::size_t covered = std::count_if(abs_d.begin(), abs_d.end(),
        [&](double v){ return v <= coverageThreshold; });
    report.coverageRate = 100.0 * covered / n;
}

} // namespace DistanceField
