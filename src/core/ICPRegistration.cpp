#include "ICPRegistration.h"

#include <CGAL/Polygon_mesh_processing/compute_normal.h>

#include <nanoflann.hpp>
#include <Eigen/Dense>
#include <Eigen/SVD>

#include <cmath>
#include <random>

namespace ICPRegistration {

namespace {

// ---- nanoflann KD-tree adapter ----
struct PointCloud3d {
    std::vector<Eigen::Vector3d> pts;
    std::size_t kdtree_get_point_count() const { return pts.size(); }
    double kdtree_get_pt(std::size_t idx, int dim) const { return pts[idx][dim]; }
    template <class BBOX> bool kdtree_get_bbox(BBOX&) const { return false; }
};

using KDTreeL2 = nanoflann::KDTreeSingleIndexAdaptor<
    nanoflann::L2_Simple_Adaptor<double, PointCloud3d>,
    PointCloud3d, 3>;

// Build rotation matrix from small rotation vector α
Eigen::Matrix3d rotFromSmallAngle(const Eigen::Vector3d& alpha)
{
    Eigen::Matrix3d K;
    K <<       0.0, -alpha[2],  alpha[1],
          alpha[2],       0.0, -alpha[0],
         -alpha[1],  alpha[0],      0.0;
    return Eigen::Matrix3d::Identity() + K + 0.5 * K * K;
}

} // namespace

std::vector<Eigen::Vector3d> sampleMesh(const ScanData& scan, int count)
{
    const auto& mesh = scan.mesh;
    std::size_t nFaces = mesh.number_of_faces();
    if (nFaces == 0) return {};

    // compute cumulative face areas for importance sampling
    std::vector<double> cumArea;
    cumArea.reserve(nFaces);
    std::vector<std::array<VertexDesc, 3>> faceVerts;
    faceVerts.reserve(nFaces);

    double total = 0.0;
    for (auto f : mesh.faces()) {
        auto h = mesh.halfedge(f);
        std::array<VertexDesc, 3> v;
        int i = 0;
        for (auto vd : mesh.vertices_around_face(h)) v[i++] = vd;
        faceVerts.push_back(v);
        const Point3& p0 = mesh.point(v[0]);
        const Point3& p1 = mesh.point(v[1]);
        const Point3& p2 = mesh.point(v[2]);
        auto cross = CGAL::cross_product(p1 - p0, p2 - p0);
        double area = 0.5 * std::sqrt(CGAL::to_double(cross.squared_length()));
        total += area;
        cumArea.push_back(total);
    }

    std::mt19937 rng(42);
    std::uniform_real_distribution<double> uniReal(0.0, total);
    std::uniform_real_distribution<double> uni01(0.0, 1.0);

    std::vector<Eigen::Vector3d> result;
    result.reserve(count);

    for (int s = 0; s < count; ++s) {
        double r = uniReal(rng);
        auto it = std::lower_bound(cumArea.begin(), cumArea.end(), r);
        std::size_t fi = std::min<std::size_t>(
            static_cast<std::size_t>(std::distance(cumArea.begin(), it)),
            faceVerts.size() - 1);

        const auto& v = faceVerts[fi];
        const Point3& p0 = mesh.point(v[0]);
        const Point3& p1 = mesh.point(v[1]);
        const Point3& p2 = mesh.point(v[2]);

        // random barycentric coordinates
        double u = uni01(rng), w = uni01(rng);
        if (u + w > 1.0) { u = 1.0 - u; w = 1.0 - w; }
        double t = 1.0 - u - w;
        result.emplace_back(
            u * p0.x() + w * p1.x() + t * p2.x(),
            u * p0.y() + w * p1.y() + t * p2.y(),
            u * p0.z() + w * p1.z() + t * p2.z()
        );
    }
    return result;
}

std::vector<Eigen::Vector3d> computeVertexNormals(const ScanData& scan)
{
    const auto& mesh = scan.mesh;
    std::size_t nVerts = mesh.num_vertices();
    std::vector<Eigen::Vector3d> normals(nVerts, Eigen::Vector3d::Zero());

    for (auto f : mesh.faces()) {
        auto h = mesh.halfedge(f);
        std::array<VertexDesc, 3> v;
        int i = 0;
        for (auto vd : mesh.vertices_around_face(h)) v[i++] = vd;

        const Point3& p0 = mesh.point(v[0]);
        const Point3& p1 = mesh.point(v[1]);
        const Point3& p2 = mesh.point(v[2]);
        Vector3K fn = CGAL::cross_product(p1 - p0, p2 - p0);
        Eigen::Vector3d en(CGAL::to_double(fn.x()),
                           CGAL::to_double(fn.y()),
                           CGAL::to_double(fn.z()));
        normals[v[0].idx()] += en;
        normals[v[1].idx()] += en;
        normals[v[2].idx()] += en;
    }
    for (auto& n : normals) {
        double len = n.norm();
        if (len > 1e-10) n /= len; else n = Eigen::Vector3d(0, 0, 1);
    }
    return normals;
}

void applyTransform(ScanData& scan, const Eigen::Matrix4d& T)
{
    auto& mesh = scan.mesh;
    for (auto v : mesh.vertices()) {
        const Point3& p = mesh.point(v);
        Eigen::Vector4d hp(p.x(), p.y(), p.z(), 1.0);
        Eigen::Vector4d tp = T * hp;
        mesh.point(v) = Point3(tp[0], tp[1], tp[2]);
    }
    scan.transform = T * scan.transform;
}

Result align(
    const ScanData&  source,
    const ScanData&  target,
    const Params&    params,
    std::function<void(int, double)> progressCallback)
{
    Result result;
    result.transform  = Eigen::Matrix4d::Identity();
    result.converged  = false;
    result.finalRms   = 1e9;
    result.iterations = 0;

    // --- sample source and build target KD-tree ---
    auto srcPts = sampleMesh(source, params.sampleCount);
    auto tgtPts = sampleMesh(target, params.sampleCount);

    // target normals via vertex normal interpolation
    // For simplicity: compute normals on original target and query by closest vertex
    auto tgtNormals = computeVertexNormals(const_cast<ScanData&>(target));

    // build target point cloud with normals (sampled)
    // map each sampled target point to the nearest actual vertex normal
    PointCloud3d tgtCloud;
    tgtCloud.pts.resize(target.mesh.num_vertices());
    for (auto v : target.mesh.vertices()) {
        const Point3& p = target.mesh.point(v);
        tgtCloud.pts[v.idx()] = Eigen::Vector3d(p.x(), p.y(), p.z());
    }

    KDTreeL2 tgtTree(3, tgtCloud,
                     nanoflann::KDTreeSingleIndexAdaptorParams(10));
    tgtTree.buildIndex();

    Eigen::Matrix4d cumT = Eigen::Matrix4d::Identity();
    double prevRms = 1e9;

    for (int iter = 0; iter < params.maxIterations; ++iter) {
        // transform source points by cumT
        std::vector<Eigen::Vector3d> transSrc(srcPts.size());
        for (std::size_t i = 0; i < srcPts.size(); ++i) {
            Eigen::Vector4d hp(srcPts[i][0], srcPts[i][1], srcPts[i][2], 1.0);
            Eigen::Vector4d tp = cumT * hp;
            transSrc[i] = tp.head<3>();
        }

        // find correspondences
        Eigen::MatrixXd A(srcPts.size(), 6);
        Eigen::VectorXd b(srcPts.size());
        int usedCorr = 0;
        double rmsAcc = 0.0;

        for (std::size_t i = 0; i < transSrc.size(); ++i) {
            const Eigen::Vector3d& sp = transSrc[i];
            double query[3] = {sp[0], sp[1], sp[2]};
            nanoflann::KNNResultSet<double> rs(1);
            std::size_t retIdx; double outDist2;
            rs.init(&retIdx, &outDist2);
            tgtTree.findNeighbors(rs, query, nanoflann::SearchParameters());

            if (std::sqrt(outDist2) > params.maxCorrespDist) continue;

            const Eigen::Vector3d& qp = tgtCloud.pts[retIdx];
            const Eigen::Vector3d& n  = tgtNormals[retIdx];

            // point-to-plane: (α × sp + t - (qp - sp)) · n = 0
            Eigen::Vector3d cross = sp.cross(n);
            A.row(usedCorr) << cross[0], cross[1], cross[2], n[0], n[1], n[2];
            b[usedCorr] = n.dot(qp - sp);

            double d = std::abs(n.dot(sp - qp));
            rmsAcc += d * d;
            ++usedCorr;
        }

        if (usedCorr < 6) break;

        A.conservativeResize(usedCorr, 6);
        b.conservativeResize(usedCorr);

        double rms = std::sqrt(rmsAcc / usedCorr);
        result.iterations = iter + 1;
        result.finalRms   = rms;

        if (progressCallback) progressCallback(iter, rms);

        // solve 6-DOF system
        Eigen::VectorXd x = A.colPivHouseholderQr().solve(b);

        // build incremental transform
        Eigen::Vector3d alpha(x[0], x[1], x[2]);
        Eigen::Vector3d t(x[3], x[4], x[5]);
        Eigen::Matrix3d R = rotFromSmallAngle(alpha);

        Eigen::Matrix4d dT = Eigen::Matrix4d::Identity();
        dT.block<3,3>(0,0) = R;
        dT.block<3,1>(0,3) = t;
        cumT = dT * cumT;

        if (std::abs(prevRms - rms) < params.convergenceRms) {
            result.converged = true;
            break;
        }
        prevRms = rms;
    }

    result.transform = cumT;
    return result;
}

// Sample only from faces where all 3 vertices are in the mask.
// Falls back to full mesh if the masked region yields < 500 faces.
static std::vector<Eigen::Vector3d> sampleMeshMasked(
    const ScanData& scan,
    const std::vector<bool>& mask,
    int count)
{
    const auto& mesh = scan.mesh;
    const bool useMask = !mask.empty() && mask.size() == mesh.num_vertices();

    std::vector<double>                  cumArea;
    std::vector<std::array<VertexDesc,3>> faceVerts;
    double total = 0.0;

    for (auto f : mesh.faces()) {
        auto h = mesh.halfedge(f);
        std::array<VertexDesc,3> v;
        int i = 0;
        for (auto vd : mesh.vertices_around_face(h)) v[i++] = vd;

        if (useMask && (!mask[v[0].idx()] || !mask[v[1].idx()] || !mask[v[2].idx()]))
            continue;

        const Point3& p0 = mesh.point(v[0]);
        const Point3& p1 = mesh.point(v[1]);
        const Point3& p2 = mesh.point(v[2]);
        auto cross = CGAL::cross_product(p1 - p0, p2 - p0);
        double area = 0.5 * std::sqrt(CGAL::to_double(cross.squared_length()));
        total += area;
        cumArea.push_back(total);
        faceVerts.push_back(v);
    }

    // fallback if too few masked faces
    if (faceVerts.size() < 500)
        return sampleMesh(scan, count);

    std::mt19937 rng(42);
    std::uniform_real_distribution<double> uniReal(0.0, total);
    std::uniform_real_distribution<double> uni01(0.0, 1.0);

    std::vector<Eigen::Vector3d> result;
    result.reserve(count);
    for (int s = 0; s < count; ++s) {
        double r = uniReal(rng);
        auto it = std::lower_bound(cumArea.begin(), cumArea.end(), r);
        std::size_t fi = std::min<std::size_t>(
            static_cast<std::size_t>(std::distance(cumArea.begin(), it)),
            faceVerts.size() - 1);
        const auto& vv = faceVerts[fi];
        const Point3& p0 = mesh.point(vv[0]);
        const Point3& p1 = mesh.point(vv[1]);
        const Point3& p2 = mesh.point(vv[2]);
        double u = uni01(rng), w = uni01(rng);
        if (u + w > 1.0) { u = 1.0 - u; w = 1.0 - w; }
        double t = 1.0 - u - w;
        result.emplace_back(u*p0.x()+w*p1.x()+t*p2.x(),
                            u*p0.y()+w*p1.y()+t*p2.y(),
                            u*p0.z()+w*p1.z()+t*p2.z());
    }
    return result;
}

Result alignMasked(
    const ScanData& source,
    const ScanData& target,
    const std::vector<bool>& sourceMask,
    const Params&   params,
    std::function<void(int, double)> progressCallback)
{
    Result result;
    result.transform  = Eigen::Matrix4d::Identity();
    result.converged  = false;
    result.finalRms   = 1e9;
    result.iterations = 0;

    auto srcPts     = sampleMeshMasked(source, sourceMask, params.sampleCount);
    auto tgtNormals = computeVertexNormals(const_cast<ScanData&>(target));

    PointCloud3d tgtCloud;
    tgtCloud.pts.resize(target.mesh.num_vertices());
    for (auto v : target.mesh.vertices()) {
        const Point3& p = target.mesh.point(v);
        tgtCloud.pts[v.idx()] = Eigen::Vector3d(p.x(), p.y(), p.z());
    }
    KDTreeL2 tgtTree(3, tgtCloud, nanoflann::KDTreeSingleIndexAdaptorParams(10));
    tgtTree.buildIndex();

    Eigen::Matrix4d cumT = Eigen::Matrix4d::Identity();
    double prevRms = 1e9;

    for (int iter = 0; iter < params.maxIterations; ++iter) {
        std::vector<Eigen::Vector3d> transSrc(srcPts.size());
        for (std::size_t i = 0; i < srcPts.size(); ++i) {
            Eigen::Vector4d hp(srcPts[i][0], srcPts[i][1], srcPts[i][2], 1.0);
            transSrc[i] = (cumT * hp).head<3>();
        }

        Eigen::MatrixXd A(srcPts.size(), 6);
        Eigen::VectorXd b(srcPts.size());
        int usedCorr = 0;
        double rmsAcc = 0.0;

        for (std::size_t i = 0; i < transSrc.size(); ++i) {
            const Eigen::Vector3d& sp = transSrc[i];
            double query[3] = {sp[0], sp[1], sp[2]};
            nanoflann::KNNResultSet<double> rs(1);
            std::size_t retIdx; double outDist2;
            rs.init(&retIdx, &outDist2);
            tgtTree.findNeighbors(rs, query, nanoflann::SearchParameters());
            if (std::sqrt(outDist2) > params.maxCorrespDist) continue;
            const Eigen::Vector3d& qp = tgtCloud.pts[retIdx];
            const Eigen::Vector3d& n  = tgtNormals[retIdx];
            Eigen::Vector3d cross = sp.cross(n);
            A.row(usedCorr) << cross[0], cross[1], cross[2], n[0], n[1], n[2];
            b[usedCorr] = n.dot(qp - sp);
            double d = std::abs(n.dot(sp - qp));
            rmsAcc += d * d;
            ++usedCorr;
        }

        if (usedCorr < 6) break;

        A.conservativeResize(usedCorr, 6);
        b.conservativeResize(usedCorr);
        double rms = std::sqrt(rmsAcc / usedCorr);
        result.iterations = iter + 1;
        result.finalRms   = rms;
        if (progressCallback) progressCallback(iter, rms);

        Eigen::VectorXd x = A.colPivHouseholderQr().solve(b);
        Eigen::Vector3d alpha(x[0], x[1], x[2]);
        Eigen::Vector3d t(x[3], x[4], x[5]);
        Eigen::Matrix4d dT = Eigen::Matrix4d::Identity();
        dT.block<3,3>(0,0) = rotFromSmallAngle(alpha);
        dT.block<3,1>(0,3) = t;
        cumT = dT * cumT;

        if (std::abs(prevRms - rms) < params.convergenceRms) {
            result.converged = true;
            break;
        }
        prevRms = rms;
    }

    result.transform = cumT;
    return result;
}

} // namespace ICPRegistration
