DentScanCompare – Metric and Visualisation Guide
================================================

=== 1. CSV COLUMN ABBREVIATIONS ===

TESSELLATION QUALITY (measured before registration, intrinsic to each scanner)

  Scanner
    Name extracted from the STL filename (second underscore-delimited token,
    e.g. "DefektIIa_Primescan_30_3min23s_r3" → "Primescan").

  Triangles
    Total number of triangular faces in the mesh after import and polygon-soup
    repair.  Directly reflects the spatial resolution chosen by the scanner
    firmware.  More triangles → finer detail representation, but also larger
    file size and longer processing time.

  Edge [mm]
    Mean edge length, in millimetres.  Computed as the average of all three
    edge lengths across every triangle.  A smaller value means the mesh is
    finer.  Typical range for modern intraoral scanners: 0.10–0.25 mm.

  AspRatio
    Mean triangle aspect ratio, defined as (longest edge) / (shortest edge)
    for each triangle, averaged across all triangles.  An equilateral triangle
    gives 1.0.  Values above ~2.5 indicate many elongated, "needle-like"
    triangles which degrade curvature estimation and FEA accuracy.

  ATI  [Adaptive Tessellation Index]
    Spearman rank correlation between |mean curvature| (|κ_H|) and the
    reciprocal of triangle area (1/A), calculated per triangle.
    Range: −1 to +1.
      ATI ≈ +1  Ideal adaptive tessellation: small triangles where the surface
                curves strongly (tooth cusps, marginal ridges), large triangles
                on flat areas (palate, gingiva).
      ATI ≈  0  Uniform tessellation: triangle size is independent of surface
                curvature.  Common in older scan engines or when stitching
                multiple video frames at constant resolution.
      ATI < 0   Inverted: more triangles on flat areas than on curved ones.
    ATI measures scanner intelligence, not accuracy.  A high ATI scanner uses
    its triangle budget where it matters clinically.

  DensHighκ [/mm²]
    Triangle density (triangles per mm²) in high-curvature zones, defined as
    vertices where |κ_H| exceeds the per-scan median.  This is the cusp/fissure
    region.  Higher values → more detail on anatomically critical surfaces.

  DensLowκ [/mm²]
    Triangle density in low-curvature zones (|κ_H| ≤ median).  These are flat
    areas like the hard palate or buccal gingiva.  Lower values relative to
    DensHighκ indicate good adaptive behaviour (the scanner "wastes" fewer
    triangles on flat surfaces).


ACCURACY METRICS (measured after GPA registration to the mean reference)

  RMS [mm]  [Root Mean Square distance]
    √(Σ dᵢ² / n) where dᵢ is the signed distance from each scan vertex to
    the nearest point on the GPA mean reference surface.
    This is the primary accuracy metric.  It is sensitive to outliers (large
    errors inflate it strongly).  Lower is better.

  MAD [mm]  [Median Absolute Deviation]
    The median of |dᵢ|.  Robust to outliers and scan-boundary artefacts.
    Reflects the "typical" deviation of the central 50% of the surface.
    Should be read alongside RMS: a large RMS with a small MAD indicates
    a few extreme outlier regions rather than a systematic global error.

  H95 [mm]  [95th-percentile Hausdorff distance]
    The 95th percentile of |dᵢ|.  Ignores the 5% worst points (boundary
    artefacts, isolated holes).  Clinically meaningful: 95% of the scan
    surface is within H95 mm of the reference.

  H100 [mm]  [Maximum Hausdorff distance]
    The single largest |dᵢ| across the entire mesh.  Dominated by scan
    boundaries, incomplete coverage, and topological holes.  Should be
    interpreted in conjunction with Coverage% and Holes.  High H100 with
    low RMS and MAD suggests a scan that is accurate in the centre but
    frays badly at its margins.

  Bias [mm]  [Signed mean distance]
    Mean of dᵢ (signed).  Positive = the scan surface lies systematically
    outside the reference (oversized / bulging outward, e.g. expansive
    distortion).  Negative = the scan is systematically undersized (inward
    collapse, e.g. gingival compression artefact).  Near zero = no
    systematic directional error.


COMPLETENESS METRICS

  Coverage%
    Percentage of GPA reference surface vertices for which the nearest
    scan vertex is within 0.2 mm.  Measures how completely the scanner
    captured the reference anatomy.  100% = perfect coverage; lower values
    indicate regions the scanner failed to image.

  Boundary [mm]
    Total length of open-boundary edges (edges belonging to only one face),
    in millimetres.  Open boundaries arise at the scan margins or around
    holes.  A higher value means the scan has more incomplete edges.
    Note: every scan of an arch has at least one boundary (the gingival
    margin) so a non-zero value is expected and normal.

  Holes
    Number of topological holes (= genus of the mesh, counted via Euler
    characteristic V − E + F = 2 − 2g).  An ideal arch scan has 1 (the
    open gingival margin counts as one boundary loop, genus 0 means
    one boundary component).  Additional holes indicate missing patches,
    where the scanner lost tracking between frames.

  Stitch [°]  [Maximum stitching-artefact angle]
    Maximum normal-discontinuity angle between adjacent faces, in degrees.
    When a scanner merges two independently captured strips, the junction
    can appear as a sharp angular kink even though the surface itself is
    smooth.  Values above 90° indicate visible stitching artefacts.
    Values near 180° typically occur at boundary edges (normal flip at
    open mesh border) and are expected.


=== 2. HOW TO INTERPRET THE TESSELLATION FINGERPRINT ===

The scatter plot shows one dot per triangle for all loaded scans.

  X-axis (horizontal, log scale): |Mean curvature| (1/mm)
    Left = flat regions (palate, buccal gingiva, flat tooth surfaces)
    Right = highly curved regions (cusp tips, fissures, margins)
    A sphere of radius r has |κ_H| = 1/r; a typical molar cusp has
    radii of ~0.3–1.0 mm, giving |κ_H| of 1–3 mm⁻¹.

  Y-axis (vertical, log scale): Triangle area (mm²)
    Top = large, coarse triangles
    Bottom = small, fine triangles

  What to look for

  a) Direction of the cloud
     An adaptive scanner concentrates fine triangles where curvature is
     high: the cloud slopes from top-left (flat, coarse) to bottom-right
     (curved, fine).  This is the expected diagonal for a well-adapted
     mesh.  ATI quantifies the strength of this slope.

  b) Width and scatter of the cloud
     A tight, narrow cloud means consistent tessellation behaviour across
     the scan.  A diffuse cloud or bimodal distribution suggests that the
     scanner uses very different strategies in different anatomical zones
     (e.g. a coarser mode for palate and a fine mode for tooth crowns).

  c) Position of the cloud relative to other scanners
     A cloud shifted toward the bottom-left has many fine triangles even
     on flat surfaces (high triangle count, possibly wasteful but safe).
     A cloud shifted toward the top-right has coarse triangles even on
     curved surfaces (low resolution where it matters most — clinically
     concerning).

  d) Comparing scanners
     Each scanner has its own colour.  Overlapping clouds show that two
     scanners produce similar tessellation strategies.  A scanner whose
     cloud extends further toward the bottom-right corner captures finer
     curvature detail.

  Typical findings in this dataset
     All five scanners show the expected negative diagonal trend, confirming
     at least some degree of adaptive tessellation.  Primescan shows the
     densest cluster at high curvature (bottom-right) consistent with its
     highest triangle count.  Trios5 has the best ATI (0.250), meaning its
     triangle-to-curvature correlation is strongest.  FussenS6000 has the
     lowest ATI (0.088), indicating the most uniform tessellation strategy.


=== 3. READING THE DISTANCE MAPS ===

Colour encoding (diverging blue–white–red):
  Red   = positive distance = scan surface lies OUTSIDE the reference (oversized)
  White = zero deviation (on the reference surface)
  Blue  = negative distance = scan surface lies INSIDE the reference (undersized)

The colour scale is set automatically to ±H95 of the worst scanner in the set
(clamped to ±2 mm).  All five maps share the same scale for direct visual
comparison.

Clinically:
  Widespread red on the buccal aspects → scanner produces an expanded arch
  Widespread blue on the occlusal surface → scanner records the teeth as
    shorter / less protruding than the reference
  Mixed red/blue with sharp boundaries → stitching artefact, local
    registration failure, or a true scanner-specific deformation pattern

The large oval grey region in the centre of most scans is the DefektIIa
standardised defect (an artificial missing-tooth area in the test model).
Colours there may be artefactual if coverage is incomplete.
