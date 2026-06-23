// SPDX-License-Identifier: GPL-2.0-or-later
// SPDX-FileCopyrightText: 2024 Prof. Dr. Karl-Heinz Kunzelmann

#include "GPAReference.h"

#include <CGAL/AABB_tree.h>
#include <CGAL/AABB_traits_3.h>
#include <CGAL/AABB_face_graph_triangle_primitive.h>

#include <Eigen/Eigenvalues>

#include <QtConcurrent>
#include <QFuture>

#include <algorithm>
#include <atomic>
#include <cmath>
#include <iomanip>
#include <iostream>
#include <limits>
#include <memory>
#include <numbers>

namespace GPAReference {

namespace {

// ─── PCA coarse alignment ────────────────────────────────────────────────────
// Translates each scan to its centroid and rotates so that:
//   largest-variance axis  → X (left–right of arch)
//   middle-variance axis   → Y (front–back)
//   smallest-variance axis → Z (≈ occlusal normal)
// Z-sign is resolved with curvature: high-curvature side (teeth) → +Z.
Eigen::Matrix4d pcaCoarseAlign(ScanData& scan)
{
    const auto& mesh = scan.mesh;
    std::size_t n = mesh.num_vertices();
    if (n == 0) return Eigen::Matrix4d::Identity();

    Eigen::Vector3d mu = Eigen::Vector3d::Zero();
    for (auto v : mesh.vertices()) {
        const Point3& p = mesh.point(v);
        mu += Eigen::Vector3d(p.x(), p.y(), p.z());
    }
    mu /= static_cast<double>(n);

    Eigen::Matrix3d cov = Eigen::Matrix3d::Zero();
    for (auto v : mesh.vertices()) {
        const Point3& p = mesh.point(v);
        Eigen::Vector3d d(p.x()-mu[0], p.y()-mu[1], p.z()-mu[2]);
        cov += d * d.transpose();
    }
    cov /= static_cast<double>(n);

    // Eigenvalues sorted ascending; col(2) = largest variance.
    Eigen::SelfAdjointEigenSolver<Eigen::Matrix3d> eig(cov);
    Eigen::Matrix3d R;
    R.col(0) = eig.eigenvectors().col(2); // largest  → X
    R.col(1) = eig.eigenvectors().col(1); // medium   → Y
    R.col(2) = eig.eigenvectors().col(0); // smallest → Z (occlusal normal)
    if (R.determinant() < 0) R.col(0) = -R.col(0); // ensure right-handed

    // Resolve Z-sign: occlusal surface (teeth = high curvature) → +Z.
    auto meanMapOpt = mesh.property_map<VertexDesc, double>("v:mean_curv");
    if (meanMapOpt.has_value()) {
        const auto& mm = meanMapOpt.value();
        const Eigen::Vector3d& zAx = R.col(2);

        std::vector<double> kv;
        kv.reserve(n);
        for (auto v : mesh.vertices())
            kv.push_back(std::abs(get(mm, v)));
        std::nth_element(kv.begin(), kv.begin() + n/2, kv.end());
        double kMed = kv[n/2];

        double sumHighZ = 0.0, sumLowZ = 0.0;
        int    nHigh = 0,    nLow = 0;
        for (auto v : mesh.vertices()) {
            const Point3& p = mesh.point(v);
            double z = zAx.dot(Eigen::Vector3d(p.x()-mu[0], p.y()-mu[1], p.z()-mu[2]));
            if (std::abs(get(mm, v)) >= kMed) { sumHighZ += z; ++nHigh; }
            else                               { sumLowZ  += z; ++nLow;  }
        }
        if (nHigh > 0 && nLow > 0 &&
            sumHighZ / nHigh < sumLowZ / nLow) {
            // High-curvature (teeth) is at -Z → flip Z and Y to stay right-handed.
            R.col(2) = -R.col(2);
            R.col(1) = -R.col(1);
        }
    }

    // Resolve X-sign: force X-axis to align with the canonical +X direction of the
    // input data.  Without this, Eigen's eigensolver returns an arbitrary sign for
    // the largest-variance eigenvector, which makes the GPA frame 180° inconsistent
    // between groups (equivalent to a 180° rotation around Z).  Flipping both X and
    // Y preserves right-handedness while fixing the arch facing direction.
    if (R.col(0).dot(Eigen::Vector3d(1.0, 0.0, 0.0)) < 0.0) {
        R.col(0) = -R.col(0);
        R.col(1) = -R.col(1);
    }

    Eigen::Matrix4d T = Eigen::Matrix4d::Identity();
    T.block<3,3>(0,0) = R.transpose();
    T.block<3,1>(0,3) = -(R.transpose() * mu);
    ICPRegistration::applyTransform(scan, T);
    return T;
}

// ─── Z-rotation disambiguation ───────────────────────────────────────────────
// After PCA the remaining ambiguity is the sign of the X-axis, which is
// equivalent to a 0° or 180° rotation around Z.  We also test 90° and 270°
// as a safety net for unusual scanner coordinate systems.
// Each candidate orientation is evaluated with a very quick ICP run
// (few iterations, large radius).  The best wins.
void resolveZRotation(ScanData& scan, const ScanData& ref)
{
    ICPRegistration::Params evalP;
    evalP.sampleCount    = 3000;
    evalP.maxCorrespDist = 20.0;
    evalP.maxIterations  = 8;
    evalP.convergenceRms = 1.0;

    double bestRms = std::numeric_limits<double>::infinity();
    int    bestIdx = 0;

    for (int qi = 0; qi < 4; ++qi) {
        double a = qi * std::numbers::pi / 2.0;
        Eigen::Matrix4d Rz = Eigen::Matrix4d::Identity();
        Rz(0,0) =  std::cos(a); Rz(0,1) = -std::sin(a);
        Rz(1,0) =  std::sin(a); Rz(1,1) =  std::cos(a);

        // Temporary deep copy to try this orientation without mutating scan.
        ScanData trial;
        trial.mesh      = scan.mesh;
        trial.transform = scan.transform;
        ICPRegistration::applyTransform(trial, Rz);

        auto r = ICPRegistration::align(trial, ref, evalP);
        if (r.finalRms < bestRms) {
            bestRms = r.finalRms;
            bestIdx = qi;
        }
    }

    if (bestIdx != 0) {
        double a = bestIdx * std::numbers::pi / 2.0;
        Eigen::Matrix4d Rz = Eigen::Matrix4d::Identity();
        Rz(0,0) =  std::cos(a); Rz(0,1) = -std::sin(a);
        Rz(1,0) =  std::sin(a); Rz(1,1) =  std::cos(a);
        ICPRegistration::applyTransform(scan, Rz);
    }
}

// ─── Compute vertex normals ──────────────────────────────────────────────────
// Returns a vector of unit normals, one per vertex, computed as the
// area-weighted average of adjacent face normals.
std::vector<Eigen::Vector3d> computeVertexNormals(const SurfaceMesh& mesh)
{
    std::vector<Eigen::Vector3d> normals(mesh.num_vertices(), Eigen::Vector3d::Zero());

    for (auto f : mesh.faces()) {
        auto h = mesh.halfedge(f);
        auto v0 = mesh.source(h);
        auto v1 = mesh.target(h);
        auto v2 = mesh.target(mesh.next(h));

        const Point3& p0 = mesh.point(v0);
        const Point3& p1 = mesh.point(v1);
        const Point3& p2 = mesh.point(v2);

        Eigen::Vector3d e1(p1.x() - p0.x(), p1.y() - p0.y(), p1.z() - p0.z());
        Eigen::Vector3d e2(p2.x() - p0.x(), p2.y() - p0.y(), p2.z() - p0.z());
        Eigen::Vector3d faceNormal = e1.cross(e2);  // Area-weighted (not normalized)

        normals[v0.idx()] += faceNormal;
        normals[v1.idx()] += faceNormal;
        normals[v2.idx()] += faceNormal;
    }

    for (auto& n : normals) {
        double len = n.norm();
        if (len > 1e-10) n /= len;
    }

    return normals;
}

// ─── Compute face normal directly from face handle ───────────────────────────
// Given a face handle from the AABB tree primitive, compute the face normal.
// This is O(1) since we already have the face.
Eigen::Vector3d computeFaceNormal(const SurfaceMesh& mesh, SurfaceMesh::Face_index faceIdx)
{
    auto h = mesh.halfedge(faceIdx);
    const Point3& p0 = mesh.point(mesh.source(h));
    const Point3& p1 = mesh.point(mesh.target(h));
    const Point3& p2 = mesh.point(mesh.target(mesh.next(h)));

    Eigen::Vector3d e1(p1.x() - p0.x(), p1.y() - p0.y(), p1.z() - p0.z());
    Eigen::Vector3d e2(p2.x() - p0.x(), p2.y() - p0.y(), p2.z() - p0.z());
    Eigen::Vector3d normal = e1.cross(e2);
    double len = normal.norm();
    if (len > 1e-10) normal /= len;
    return normal;
}

// ─── True mean-mesh update with robust averaging ─────────────────────────────
// After GPA convergence the reference is still one scanner's mesh (the one
// with the most triangles).  Updating each reference vertex to the centroid of
// its nearest points on ALL aligned scans produces a neutral mean surface so
// that every scanner — including the original reference — shows a non-zero
// distance to it.
//
// This version includes outlier rejection to handle holes and inconsistent
// coverage:
// - Distance rejection: skip closest points farther than maxCorrespondenceDistance
// - Normal consistency: skip closest points with significantly different normals
// - Minimum coverage: keep original position if too few scans have valid data
void updateToMeanMesh(ScanData& gpaRef,
                      const std::vector<std::shared_ptr<ScanData>>& scans,
                      const MeanMeshParams& params)
{
    using Primitive  = CGAL::AABB_face_graph_triangle_primitive<SurfaceMesh>;
    using AABBTraits = CGAL::AABB_traits_3<Kernel, Primitive>;
    using AABBTree   = CGAL::AABB_tree<AABBTraits>;

    const bool useDistanceRejection = params.maxCorrespondenceDistance > 0.0;
    const bool useNormalConsistency = params.minNormalDotProduct > 0.0;
    const double maxDistSq = params.maxCorrespondenceDistance * params.maxCorrespondenceDistance;
    const std::size_t minValidScans = static_cast<std::size_t>(
        std::ceil(params.minValidScansFraction * scans.size()));

    // Build AABB trees for all scans (parallel)
    std::cout << "    Mean mesh: building " << scans.size() << " AABB trees (parallel)..." << std::flush;
    std::vector<std::unique_ptr<AABBTree>> trees(scans.size());

    QList<int> indices;
    for (int i = 0; i < static_cast<int>(scans.size()); ++i) {
        indices.append(i);
    }

    QtConcurrent::blockingMap(indices, [&](int i) {
        auto t = std::make_unique<AABBTree>(
            faces(scans[i]->mesh).first, faces(scans[i]->mesh).second, scans[i]->mesh);
        t->accelerate_distance_queries();
        trees[i] = std::move(t);
    });
    std::cout << " done\n" << std::flush;

    // Compute vertex normals for the reference mesh (needed for normal consistency check)
    std::vector<Eigen::Vector3d> refNormals;
    if (useNormalConsistency) {
        std::cout << "    Mean mesh: computing vertex normals..." << std::flush;
        refNormals = computeVertexNormals(gpaRef.mesh);
        std::cout << " done\n" << std::flush;
    }

    const std::size_t nVerts = gpaRef.mesh.num_vertices();

    // Statistics for reporting (atomic for thread safety)
    std::atomic<std::size_t> totalDistanceRejections{0};
    std::atomic<std::size_t> totalNormalRejections{0};
    std::atomic<std::size_t> totalInsufficientCoverage{0};
    std::atomic<std::size_t> totalVerticesUpdated{0};
    std::atomic<std::size_t> progressCounter{0};

    std::cout << "    Mean mesh: updating " << nVerts << " vertices (parallel)";
    if (useDistanceRejection || useNormalConsistency) {
        std::cout << " (robust mode: maxDist=" << params.maxCorrespondenceDistance
                  << "mm, minNormalDot=" << params.minNormalDotProduct
                  << ", minScans=" << minValidScans << "/" << scans.size() << ")";
    }
    std::cout << "\n" << std::flush;

    // Collect vertex descriptors into a vector for indexed access
    std::vector<VertexDesc> vertexList;
    vertexList.reserve(nVerts);
    for (auto v : gpaRef.mesh.vertices()) {
        vertexList.push_back(v);
    }

    // Pre-allocate output: new positions and validity flags
    struct VertexResult {
        Point3 newPos;
        bool valid = false;  // true if vertex should be updated
        std::size_t distReject = 0;
        std::size_t normReject = 0;
    };
    std::vector<VertexResult> results(nVerts);

    // Copy current positions for reading (mesh will be modified after parallel section)
    std::vector<Point3> currentPositions;
    currentPositions.reserve(nVerts);
    for (auto v : gpaRef.mesh.vertices()) {
        currentPositions.push_back(gpaRef.mesh.point(v));
    }

    // Raw pointers for lambda capture (trees vector)
    const auto* treesPtr = trees.data();
    const auto* scansPtr = scans.data();
    const std::size_t numTrees = trees.size();

    // Parallel vertex processing
    QtConcurrent::blockingMap(vertexList, [&](const VertexDesc& v) {
        const std::size_t idx = v.idx();
        const Point3& p = currentPositions[idx];
        const double px = CGAL::to_double(p.x());
        const double py = CGAL::to_double(p.y());
        const double pz = CGAL::to_double(p.z());

        Eigen::Vector3d refNormal;
        if (useNormalConsistency) {
            refNormal = refNormals[idx];
        }

        double sx = 0.0, sy = 0.0, sz = 0.0;
        std::size_t validCount = 0;
        std::size_t distReject = 0;
        std::size_t normReject = 0;

        for (std::size_t si = 0; si < numTrees; ++si) {
            // Use closest_point_and_primitive to get both point and face in O(log n)
            auto result = treesPtr[si]->closest_point_and_primitive(p);
            const Point3& cp = result.first;
            auto faceHandle = result.second;

            double cpx = CGAL::to_double(cp.x());
            double cpy = CGAL::to_double(cp.y());
            double cpz = CGAL::to_double(cp.z());

            // Distance rejection check
            if (useDistanceRejection) {
                double dx = cpx - px;
                double dy = cpy - py;
                double dz = cpz - pz;
                double distSq = dx*dx + dy*dy + dz*dz;
                if (distSq > maxDistSq) {
                    distReject++;
                    continue;
                }
            }

            // Normal consistency check - now O(1) using the face handle directly
            if (useNormalConsistency) {
                // faceHandle is already a SurfaceMesh::Face_index
                Eigen::Vector3d scanNormal = computeFaceNormal(scansPtr[si]->mesh, faceHandle);
                double dotProduct = refNormal.dot(scanNormal);
                if (dotProduct < params.minNormalDotProduct) {
                    normReject++;
                    continue;
                }
            }

            // Valid correspondence
            sx += cpx;
            sy += cpy;
            sz += cpz;
            validCount++;
        }

        // Store result for this vertex
        VertexResult& res = results[idx];
        res.distReject = distReject;
        res.normReject = normReject;

        if (validCount >= minValidScans) {
            double invN = 1.0 / static_cast<double>(validCount);
            res.newPos = Point3(sx * invN, sy * invN, sz * invN);
            res.valid = true;
        }

        // Progress reporting (every 10000 vertices)
        std::size_t count = ++progressCounter;
        if (count % 10000 == 0 || count == nVerts) {
            int pct = static_cast<int>(100 * count / nVerts);
            std::cout << "\r    Mean mesh: updating " << nVerts
                      << " vertices (" << pct << "%)   " << std::flush;
        }
    });

    // Apply results to mesh (single-threaded, safe)
    for (std::size_t i = 0; i < nVerts; ++i) {
        const VertexResult& res = results[i];
        totalDistanceRejections += res.distReject;
        totalNormalRejections += res.normReject;

        if (res.valid) {
            gpaRef.mesh.point(vertexList[i]) = res.newPos;
            totalVerticesUpdated++;
        } else {
            totalInsufficientCoverage++;
        }
    }

    std::cout << "\n    Mean mesh: done\n" << std::flush;

    // Report statistics
    if (params.verbose && (useDistanceRejection || useNormalConsistency)) {
        std::cout << "    Mean mesh statistics:\n"
                  << "      Vertices updated:        " << totalVerticesUpdated
                  << " / " << nVerts << "\n"
                  << "      Vertices kept original:  " << totalInsufficientCoverage
                  << " (insufficient coverage)\n"
                  << "      Distance rejections:     " << totalDistanceRejections
                  << " (across all vertices)\n"
                  << "      Normal rejections:       " << totalNormalRejections
                  << " (across all vertices)\n" << std::flush;
    }
}

} // namespace

// ─── Public mean-mesh update ─────────────────────────────────────────────────
void updateMeanMesh(ScanData& gpaRef,
                    const std::vector<std::shared_ptr<ScanData>>& scans,
                    const MeanMeshParams& params)
{
    updateToMeanMesh(gpaRef, scans, params);
}

// ─── Main GPA entry point ────────────────────────────────────────────────────
std::shared_ptr<ScanData> compute(
    std::vector<std::shared_ptr<ScanData>>& scans,
    const Params& params,
    std::function<void(int, int, double)> progressCallback)
{
    if (scans.empty()) return nullptr;

    // Step 1: PCA coarse alignment (handles large translational + moderate
    //         rotational offsets between scanner coordinate systems).
    // Skipped when scans are already in canonical orientation (skipPcaCoarseAlign=true,
    // e.g. from DentScanAlignPro): PCA on pre-oriented scans can introduce errors when
    // patient geometry produces eigenvectors at non-axis-aligned angles.
    if (!params.skipPcaCoarseAlign) {
        for (auto& scan : scans)
            pcaCoarseAlign(*scan);
    }

    // Initial reference: either the named scanner or the one with the most triangles.
    auto refIt = scans.end();
    if (!params.fixedRefScannerName.empty()) {
        refIt = std::find_if(scans.begin(), scans.end(),
            [&](const auto& s){ return s->scannerName == params.fixedRefScannerName; });
    }
    if (refIt == scans.end()) {
        refIt = std::max_element(scans.begin(), scans.end(),
            [](const auto& a, const auto& b){
                return a->triangleCount < b->triangleCount; });
    }
    auto gpaRef = std::make_shared<ScanData>();
    gpaRef->mesh          = (*refIt)->mesh;
    gpaRef->scannerName   = "GPA_Reference";
    gpaRef->triangleCount = gpaRef->mesh.number_of_faces();

    // Step 2: Resolve the 180° / 90° Z-rotation ambiguity that PCA leaves.
    //         Compares 4 orientations via a quick ICP evaluation.
    // Skipped together with PCA when scans are pre-oriented.
    if (!params.skipPcaCoarseAlign) {
        for (auto& scan : scans) {
            if (scan.get() == refIt->get()) continue; // reference is already correct
            resolveZRotation(*scan, *gpaRef);
        }
    }

    // Step 3: GPA iterations with multi-pass ICP (coarse → fine).
    ICPRegistration::Params fineP = params.icpParams;

    ICPRegistration::Params coarseP = fineP;
    coarseP.maxCorrespDist = 15.0;
    coarseP.maxIterations  = 30;
    coarseP.convergenceRms = 0.05;

    for (int cycle = 0; cycle < params.maxGPAIterations; ++cycle) {
        std::cout << "    GPA cycle " << (cycle + 1) << "/" << params.maxGPAIterations
                  << " (" << scans.size() << " scans, parallel):\n" << std::flush;

        // Collect scan indices to process (excluding reference)
        std::vector<std::size_t> scanIndices;
        for (std::size_t si = 0; si < scans.size(); ++si) {
            if (scans[si].get() != refIt->get()) {
                scanIndices.push_back(si);
            }
        }

        // Results storage for parallel ICP
        struct ICPResult {
            std::size_t scanIndex;
            std::string scannerName;
            double finalRms;
            int iterations;
            bool converged;
        };
        std::vector<ICPResult> icpResults(scanIndices.size());

        // Parallel ICP alignment
        QtConcurrent::blockingMap(scanIndices, [&](std::size_t si) {
            auto& scan = scans[si];

            const std::vector<bool>& icpMask =
                (si < params.scanMasks.size()) ? params.scanMasks[si] : std::vector<bool>();
            const bool useMask = !icpMask.empty();

            // Coarse pass in the first cycle only.
            if (cycle == 0) {
                ICPRegistration::Result r0 = useMask
                    ? ICPRegistration::alignMasked(*scan, *gpaRef, icpMask, coarseP)
                    : ICPRegistration::align(*scan, *gpaRef, coarseP);
                ICPRegistration::applyTransform(*scan, r0.transform);
            }

            // Fine pass (no progress callback in parallel mode to avoid contention).
            auto r1 = useMask
                ? ICPRegistration::alignMasked(*scan, *gpaRef, icpMask, fineP)
                : ICPRegistration::align(*scan, *gpaRef, fineP);
            ICPRegistration::applyTransform(*scan, r1.transform);

            // Find result index for this scan
            for (std::size_t ri = 0; ri < scanIndices.size(); ++ri) {
                if (scanIndices[ri] == si) {
                    icpResults[ri] = {si, scan->scannerName, r1.finalRms, r1.iterations, r1.converged};
                    break;
                }
            }
        });

        // Print results in order after parallel phase
        for (const auto& r : icpResults) {
            std::cout << "      [" << std::setw(2) << (r.scanIndex + 1) << "/" << scans.size() << "]"
                      << " " << std::left << std::setw(16) << r.scannerName << std::right
                      << "  res=" << std::fixed << std::setprecision(4) << r.finalRms << " mm"
                      << "  iter=" << std::setw(3) << r.iterations
                      << (r.converged ? "" : "  [NOT CONVERGED]")
                      << "\n" << std::flush;
        }

        // Update reference to mean mesh (GPA mode only) and measure convergence
        // as the max vertex displacement of the reference — not the ICP residual.
        double refDisp = 0.0;
        if (params.fixedRefScannerName.empty()) {
            std::vector<Point3> oldPos;
            oldPos.reserve(gpaRef->mesh.num_vertices());
            for (auto v : gpaRef->mesh.vertices())
                oldPos.push_back(gpaRef->mesh.point(v));

            updateToMeanMesh(*gpaRef, scans, params.meanMeshParams);

            std::size_t idx = 0;
            for (auto v : gpaRef->mesh.vertices()) {
                const Point3& np = gpaRef->mesh.point(v);
                double dx = CGAL::to_double(np.x()) - CGAL::to_double(oldPos[idx].x());
                double dy = CGAL::to_double(np.y()) - CGAL::to_double(oldPos[idx].y());
                double dz = CGAL::to_double(np.z()) - CGAL::to_double(oldPos[idx].z());
                refDisp = std::max(refDisp, std::sqrt(dx*dx + dy*dy + dz*dz));
                ++idx;
            }
        }

        std::cout << "    cycle " << (cycle + 1) << " done"
                  << "  max_ref_disp=" << std::fixed << std::setprecision(4) << refDisp << " mm"
                  << (refDisp < params.convergenceThresh ? "  [converged]\n" : "\n")
                  << std::flush;

        if (refDisp < params.convergenceThresh) break;
    }

    return gpaRef;
}

} // namespace GPAReference
