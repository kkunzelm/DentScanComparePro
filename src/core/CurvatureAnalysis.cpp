#include "CurvatureAnalysis.h"

#include <CGAL/Polygon_mesh_processing/interpolated_corrected_curvatures.h>

namespace CurvatureAnalysis {

void compute(ScanData& scan, double ballRadius)
{
    auto& mesh = scan.mesh;

    auto [meanMap,  okM] = mesh.add_property_map<VertexDesc, double>("v:mean_curv",  0.0);
    auto [gaussMap, okG] = mesh.add_property_map<VertexDesc, double>("v:gauss_curv", 0.0);
    (void)okM; (void)okG;

    CGAL::Polygon_mesh_processing::interpolated_corrected_curvatures(
        mesh,
        CGAL::parameters::vertex_mean_curvature_map(meanMap)
                         .vertex_Gaussian_curvature_map(gaussMap)
                         .ball_radius(ballRadius)
    );

    scan.curvatureComputed = true;
}

} // namespace CurvatureAnalysis
