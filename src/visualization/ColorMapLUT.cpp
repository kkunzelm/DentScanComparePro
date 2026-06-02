#include "ColorMapLUT.h"

#include <vtkColorTransferFunction.h>

namespace ColorMapLUT {

vtkSmartPointer<vtkLookupTable> divergingBWR(double minVal, double maxVal)
{
    auto lut = vtkSmartPointer<vtkLookupTable>::New();
    lut->SetNumberOfTableValues(256);
    lut->SetRange(minVal, maxVal);
    for (int i = 0; i < 256; ++i) {
        double t = i / 255.0;  // 0→1
        double r, g, b;
        if (t < 0.5) {
            // blue to white
            double s = 2.0 * t;
            r = s; g = s; b = 1.0;
        } else {
            // white to red
            double s = 2.0 * (t - 0.5);
            r = 1.0; g = 1.0 - s; b = 1.0 - s;
        }
        lut->SetTableValue(i, r, g, b, 1.0);
    }
    lut->Build();
    return lut;
}

vtkSmartPointer<vtkLookupTable> sequentialJet(double minVal, double maxVal)
{
    auto lut = vtkSmartPointer<vtkLookupTable>::New();
    lut->SetNumberOfTableValues(256);
    lut->SetRange(minVal, maxVal);
    lut->SetHueRange(0.667, 0.0);  // blue → red
    lut->SetSaturationRange(1.0, 1.0);
    lut->SetValueRange(1.0, 1.0);
    lut->Build();
    return lut;
}

vtkSmartPointer<vtkLookupTable> viridis(double minVal, double maxVal)
{
    // 8-point Viridis approximation
    static const double viridisData[][3] = {
        {0.267, 0.005, 0.329},
        {0.283, 0.141, 0.458},
        {0.254, 0.265, 0.530},
        {0.207, 0.372, 0.553},
        {0.164, 0.471, 0.558},
        {0.128, 0.567, 0.551},
        {0.135, 0.659, 0.518},
        {0.267, 0.749, 0.441},
        {0.478, 0.821, 0.318},
        {0.741, 0.873, 0.150},
        {0.993, 0.906, 0.144}
    };
    int nPts = 11;

    auto lut = vtkSmartPointer<vtkLookupTable>::New();
    lut->SetNumberOfTableValues(256);
    lut->SetRange(minVal, maxVal);
    for (int i = 0; i < 256; ++i) {
        double t = i / 255.0 * (nPts - 1);
        int lo = static_cast<int>(t);
        int hi = std::min(lo + 1, nPts - 1);
        double f = t - lo;
        double r = viridisData[lo][0] * (1-f) + viridisData[hi][0] * f;
        double g = viridisData[lo][1] * (1-f) + viridisData[hi][1] * f;
        double b = viridisData[lo][2] * (1-f) + viridisData[hi][2] * f;
        lut->SetTableValue(i, r, g, b, 1.0);
    }
    lut->Build();
    return lut;
}

std::array<std::array<double,3>, 5> scannerColors()
{
    return {{
        {0.122, 0.471, 0.706},  // blue   – Trios5
        {1.000, 0.498, 0.055},  // orange – Primescan
        {0.173, 0.627, 0.173},  // green  – Mediti700
        {0.839, 0.153, 0.157},  // red    – iTeroLumina
        {0.580, 0.404, 0.741},  // purple – FussenS6000
    }};
}

} // namespace ColorMapLUT
