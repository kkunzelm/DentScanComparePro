#include "GPAReference.h"

#include <CGAL/AABB_tree.h>
#include <CGAL/AABB_traits_3.h>
#include <CGAL/AABB_face_graph_triangle_primitive.h>

#include <Eigen/Eigenvalues>

#include <algorithm>
#include <cmath>
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

// ─── True mean-mesh update ───────────────────────────────────────────────────
// After GPA convergence the reference is still one scanner's mesh (the one
// with the most triangles).  Updating each reference vertex to the centroid of
// its nearest points on ALL aligned scans produces a neutral mean surface so
// that every scanner — including the original reference — shows a non-zero
// distance to it.
void updateToMeanMesh(ScanData& gpaRef,
                      const std::vector<std::shared_ptr<ScanData>>& scans)
{
    using Primitive  = CGAL::AABB_face_graph_triangle_primitive<SurfaceMesh>;
    using AABBTraits = CGAL::AABB_traits_3<Kernel, Primitive>;
    using AABBTree   = CGAL::AABB_tree<AABBTraits>;

    std::vector<std::unique_ptr<AABBTree>> trees;
    trees.reserve(scans.size());
    for (const auto& s : scans) {
        auto t = std::make_unique<AABBTree>(
            faces(s->mesh).first, faces(s->mesh).second, s->mesh);
        t->accelerate_distance_queries();
        trees.push_back(std::move(t));
    }

    const double invN = 1.0 / static_cast<double>(trees.size());
    for (auto v : gpaRef.mesh.vertices()) {
        const Point3& p = gpaRef.mesh.point(v);
        double sx = 0.0, sy = 0.0, sz = 0.0;
        for (const auto& tree : trees) {
            Point3 cp = tree->closest_point(p);
            sx += CGAL::to_double(cp.x());
            sy += CGAL::to_double(cp.y());
            sz += CGAL::to_double(cp.z());
        }
        gpaRef.mesh.point(v) = Point3(sx * invN, sy * invN, sz * invN);
    }
}

} // namespace

// ─── Public mean-mesh update ─────────────────────────────────────────────────
void updateMeanMesh(ScanData& gpaRef,
                    const std::vector<std::shared_ptr<ScanData>>& scans)
{
    updateToMeanMesh(gpaRef, scans);
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
    for (auto& scan : scans)
        pcaCoarseAlign(*scan);

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
    for (auto& scan : scans) {
        if (scan.get() == refIt->get()) continue; // reference is already correct
        resolveZRotation(*scan, *gpaRef);
    }

    // Step 3: GPA iterations with multi-pass ICP (coarse → fine).
    ICPRegistration::Params fineP = params.icpParams;

    ICPRegistration::Params coarseP = fineP;
    coarseP.maxCorrespDist = 15.0;
    coarseP.maxIterations  = 30;
    coarseP.convergenceRms = 0.05;

    for (int cycle = 0; cycle < params.maxGPAIterations; ++cycle) {
        double maxDisp = 0.0;

        for (std::size_t si = 0; si < scans.size(); ++si) {
            auto& scan = scans[si];
            if (scan.get() == refIt->get()) continue;

            // Coarse pass in the first cycle only.
            if (cycle == 0) {
                auto r0 = ICPRegistration::align(*scan, *gpaRef, coarseP);
                ICPRegistration::applyTransform(*scan, r0.transform);
            }

            // Fine pass.
            auto r1 = ICPRegistration::align(*scan, *gpaRef, fineP,
                [&](int it, double rms){
                    if (progressCallback) progressCallback(cycle, (int)si, rms);
                });
            ICPRegistration::applyTransform(*scan, r1.transform);
            maxDisp = std::max(maxDisp, r1.finalRms);
        }

        if (maxDisp < params.convergenceThresh) break;
    }

    // Update to true mean only for GPA mode; in fixed-reference mode the
    // chosen scanner stays as the reference (distances to it are reported).
    if (params.fixedRefScannerName.empty())
        updateToMeanMesh(*gpaRef, scans);

    return gpaRef;
}

} // namespace GPAReference
