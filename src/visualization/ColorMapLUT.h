#pragma once

#include <vtkSmartPointer.h>
#include <vtkLookupTable.h>
#include <array>

namespace ColorMapLUT {

// Diverging blue–white–red for signed distances
vtkSmartPointer<vtkLookupTable> divergingBWR(double minVal, double maxVal);

// Sequential blue–yellow–red (Jet-like) for absolute distances
vtkSmartPointer<vtkLookupTable> sequentialJet(double minVal, double maxVal);

// Perceptually uniform sequential (Viridis approximation) for curvature magnitude
vtkSmartPointer<vtkLookupTable> viridis(double minVal, double maxVal);

// 5 distinct colours for scanner series in scatter plot
std::array<std::array<double,3>, 5> scannerColors();

} // namespace ColorMapLUT
