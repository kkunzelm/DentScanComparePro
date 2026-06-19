// SPDX-License-Identifier: GPL-2.0-or-later
// SPDX-FileCopyrightText: 2024 Prof. Dr. Karl-Heinz Kunzelmann

#include "MeshDecimation.h"
#include "CurvatureAnalysis.h"

#include <CGAL/Surface_mesh_simplification/edge_collapse.h>
#include <CGAL/Surface_mesh_simplification/Policies/Edge_collapse/Face_count_ratio_stop_predicate.h>
#include <CGAL/Surface_mesh_simplification/Policies/Edge_collapse/GarlandHeckbert_triangle_policies.h>

#include <boost/graph/graph_traits.hpp>

#include <algorithm>
#include <cmath>
#include <iostream>

namespace SMS = CGAL::Surface_mesh_simplification;

// ---- Curvature-weighted QEM cost (Xi-2025 Algorithm 1) ----
//
// Inherits from GarlandHeckbert_triangle_policies to reuse:
//   - Q-matrix storage (dynamic vertex property on the mesh)
//   - initialize() — fills per-vertex Q matrices from incident face plane quadrics
//   - update_after_collapse() — Q(v_kept) = Q(v0) + Q(v1)
//   - placement operator() — optimal vertex placement minimising quadric error
//
// Overrides:
//   - cost operator() (2 args) to multiply by curvature coefficient
//   - update_after_collapse() additionally averages curvature of the two endpoints
//
template <typename TM, typename GT>
class CurvWeightedQEMCost
    : public SMS::GarlandHeckbert_triangle_policies<TM, GT>
{
    using Base = SMS::GarlandHeckbert_triangle_policies<TM, GT>;
    using FT   = typename GT::FT;
    using VD   = typename boost::graph_traits<TM>::vertex_descriptor;

public:
    using Update_tag    = CGAL::Tag_true;
    using Self          = CurvWeightedQEMCost;
    using Get_cost      = Self;
    using Get_placement = Self;

    CurvWeightedQEMCost(TM& mesh, std::vector<double> curvCoeff, double negK = 10.0)
        : Base(mesh), m_curvCoeff(std::move(curvCoeff)), m_negK(negK)
    {}

    const Self& get_cost()      const { return *this; }
    const Self& get_placement() const { return *this; }

    // Cost (2-arg): GH quadric cost × curvature coefficient (Xi-2025)
    template <typename Profile>
    std::optional<FT> operator()(
        const Profile&                              profile,
        const std::optional<typename Profile::Point>& placement) const
    {
        auto baseCost = Base::operator()(profile, placement);
        if (!baseCost) return {};
        double k0 = coeff(profile.v0());
        double k1 = coeff(profile.v1());
        double c  = (0.5 * (k0 + k1) < 0.0) ? m_negK : 1.0;
        return FT(*baseCost * c);
    }

    // Placement (1-arg): standard GH optimal vertex — unchanged
    template <typename Profile>
    std::optional<typename Profile::Point> operator()(const Profile& profile) const
    {
        return Base::operator()(profile);
    }

    // After collapse: update Q matrix (via Base) and average curvature for survivor
    template <typename Profile, typename VertDesc>
    void update_after_collapse(const Profile& profile, const VertDesc v_kept) const
    {
        Base::update_after_collapse(profile, v_kept);
        std::size_t ik = v_kept.idx();
        if (ik < m_curvCoeff.size())
            m_curvCoeff[ik] = 0.5 * (coeff(profile.v0()) + coeff(profile.v1()));
    }

private:
    double coeff(const VD& v) const
    {
        std::size_t idx = v.idx();
        return (idx < m_curvCoeff.size()) ? m_curvCoeff[idx] : 1.0;
    }

    mutable std::vector<double> m_curvCoeff;
    double                      m_negK;
};

// Extract per-vertex mean curvature coefficients from mesh property map.
// Returns coefficients (NOT raw curvature) matching Xi-2025:
//   negK  if mean_curv < 0  (concave: CEJ, developmental grooves)
//   1.0   if mean_curv >= 0 (convex: tooth cusps, ridges)
static std::vector<double> buildCurvatureCoeffs(
    const SurfaceMesh& mesh, double negK)
{
    std::size_t nVerts = mesh.num_vertices();
    std::vector<double> coeffs(nVerts, 1.0);  // default: neutral (standard GH)

    auto curvMapOpt = mesh.property_map<VertexDesc, double>("v:mean_curv");
    if (!curvMapOpt) return coeffs;  // curvature not available — use standard GH
    auto& curvMap = curvMapOpt.value();

    for (auto v : mesh.vertices())
        coeffs[v.idx()] = (get(curvMap, v) < 0.0) ? negK : 1.0;

    return coeffs;
}

std::shared_ptr<ScanData> MeshDecimation::decimate(
    const ScanData& source, double targetFaceFraction, double negCurvK)
{
    auto result = std::make_shared<ScanData>();
    result->mesh            = source.mesh;          // deep copy (includes property maps)
    result->curvatureComputed = source.curvatureComputed;

    if (targetFaceFraction >= 1.0 - 1e-8) {
        // Nothing to decimate — return copy as-is
        result->triangleCount = result->mesh.number_of_faces();
        return result;
    }

    // Ensure curvature is available for weighting
    if (!result->curvatureComputed) {
        std::cout << "[MeshDecimation] computing curvature for weighting..." << std::flush;
        try {
            CurvatureAnalysis::compute(*result);
            std::cout << " done\n" << std::flush;
        } catch (...) {
            std::cout << " failed (using standard GH)\n" << std::flush;
        }
    }

    // Build per-vertex curvature coefficient vector
    std::vector<double> coeffs = buildCurvatureCoeffs(result->mesh, negCurvK);

    // Sanity: fraction must be (0, 1)
    double fraction = std::clamp(targetFaceFraction, 1e-4, 1.0 - 1e-8);

    // Decimate with curvature-weighted GH cost
    CurvWeightedQEMCost<SurfaceMesh, Kernel> policy(result->mesh, std::move(coeffs), negCurvK);
    SMS::Face_count_ratio_stop_predicate<SurfaceMesh> stop(fraction, result->mesh);

    SMS::edge_collapse(result->mesh, stop,
        CGAL::parameters::get_cost(policy).get_placement(policy));

    result->mesh.collect_garbage();
    result->triangleCount = result->mesh.number_of_faces();
    return result;
}
