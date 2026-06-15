# DentScanComparePro – Metric Interpretation Guide

This document explains every metric that DentScanComparePro writes to `trueness_metrics.csv`,
how to read the values, and what ranges are typical for modern intraoral scanners. It also
describes how to interpret the QC difference images that the software generates.

---

## 1. CSV Column Reference

### Identification columns

**Scanner_Model**
The scanner name as defined in the `id` field of the `scanners` array in your study
configuration. Assigned by matching the full file path against the scanner's glob patterns.

**Group_ID**
The group identifier as defined in the `id` field of the `groups` array in your study
configuration. In phantom studies this typically encodes the test condition (e.g. `SKD_20`);
in patient cohort studies it encodes the patient (e.g. `002`).

**Repetition_ID**
The repetition number extracted from the filename. In the Nold study naming convention
`{Scanner}_{Patient}_{Rep}_aligned.stl`, the token `D1`, `D2`, … maps to repetitions 1, 2, …
The extraction algorithm tries multiple filename conventions (`_D1_`, `_r1`, `rep1`, `(1)`, etc.)
and falls back to sequential numbering when no pattern matches.

---

### TESSELLATION QUALITY (measured before registration, intrinsic to each scanner)

These metrics describe the geometric quality and resolution of the raw mesh as exported
by the scanner. They are computed before any registration and therefore reflect the scanner
hardware and firmware, not the alignment algorithm.

**Triangles**
Total number of triangular faces in the mesh after import. Directly reflects the spatial
resolution chosen by the scanner firmware. More triangles mean finer detail representation,
but also larger file size and longer processing time.

**Edge_mm**
Mean edge length, in millimetres. Computed as the average of all three edge lengths across
every triangle. A smaller value means the mesh is finer. Typical range for modern intraoral
scanners: 0.10–0.25 mm.

**AspRatio**
Mean triangle aspect ratio, defined as (longest edge) / (shortest edge) for each triangle,
averaged across all triangles. An equilateral triangle gives 1.0. Values above ~2.5 indicate
many elongated, needle-like triangles, which degrade curvature estimation and can produce
artefacts in distance maps near sharp features. A well-designed mesh keeps the mean below 2.0.

**MaxAspRatio**
The largest single aspect ratio across all triangles in the mesh. While `AspRatio` summarises
the average quality, `MaxAspRatio` reveals extreme outliers — single highly degenerate triangles
that can arise at scan boundaries or at topological holes. Isolated values above 10–15 are
common at scan borders and are not necessarily a concern; values above 50 in the interior of
the mesh may indicate degenerate patches that affect local distance computation near the
affected region.

**ATI** [Adaptive Tessellation Index]
Spearman rank correlation between |mean curvature| (|κ_H|) and the reciprocal of triangle
area (1/A), calculated per triangle. Range: −1 to +1.

- **ATI ≈ +1**: Ideal adaptive tessellation — small triangles where the surface curves
  strongly (tooth cusps, marginal ridges), large triangles on flat areas (palate, gingiva).
  The scanner concentrates its resolution budget exactly where clinical detail matters most.
- **ATI ≈  0**: Uniform tessellation — triangle size is independent of surface curvature.
  Common in older scan engines or when stitching multiple video frames at constant resolution.
- **ATI < 0**: Inverted — more triangles on flat areas than on curved ones.

ATI measures scanner design intelligence, not accuracy. A high-ATI scanner uses its triangle
budget where it matters clinically. A scanner can have excellent ATI and still be inaccurate
(high RMS), or vice versa.

**DensHighK** [triangles/mm²]
Triangle density in high-curvature zones, defined as regions where |κ_H| exceeds the
per-scan median. This corresponds to the cusp tips, fissures, and marginal ridges — the
anatomically critical surfaces that determine the fit of restorations and occlusal contacts.
Higher values indicate more detail exactly where the scanner needs it most.

**DensLowK** [triangles/mm²]
Triangle density in low-curvature zones (|κ_H| ≤ median), corresponding to flat areas
like the hard palate or buccal gingiva. Lower values relative to DensHighK indicate good
adaptive behaviour — the scanner does not waste its triangle budget on featureless surfaces.
A ratio DensHighK / DensLowK > 2 is generally desirable.

---

### ACCURACY METRICS (measured after GPA registration to the group reference)

All accuracy metrics are computed by comparing each scan to the GPA mean mesh — the
consensus reference surface derived from all scans in the group. A lower value indicates
that the scan surface is closer to the group consensus.

**RMS_mm** [Root Mean Square distance]
√(Σ dᵢ² / n), where dᵢ is the signed distance from each scan vertex to the nearest point
on the GPA mean reference surface. This is the **primary trueness metric**. RMS is sensitive
to outliers because large errors are squared before averaging, so a few extreme values can
dominate the result. Typical values for accurate modern intraoral scanners: 0.04–0.15 mm.

**MAD_mm** [Median Absolute Deviation]
The median of |dᵢ|. Because it is based on the median rather than the mean, MAD is robust
to outliers and scan-boundary artefacts. It reflects the "typical" deviation of the central
50% of the surface. Read RMS and MAD together: if RMS is much larger than MAD, a small
number of extreme outlier regions are inflating the RMS — the typical surface is actually
closer to the reference than RMS alone suggests.

**H95_mm** [95th-percentile Hausdorff distance]
The 95th percentile of |dᵢ|, i.e. the distance below which 95% of scan vertices lie. It
ignores the worst 5% of points (scan boundary artefacts, isolated holes, topological
imperfections at the margins). Clinically meaningful interpretation: 95% of the scan surface
lies within H95 mm of the reference. Because it excludes the extreme tails, H95 is more
stable across repetitions than H100 and is the preferred Hausdorff metric for clinical
reporting.

**H100_mm** [Maximum Hausdorff distance]
The single largest |dᵢ| across the entire mesh. Dominated by scan boundaries, incomplete
coverage, and topological holes — locations where the scanner lost tracking or where the
mesh was not closed cleanly. H100 should always be interpreted together with Coverage_pct
and Holes. A high H100 with low RMS and MAD typically means an accurate scan that frays
at its margins, rather than a globally inaccurate scan.

**Bias_mm** [Signed mean distance]
The arithmetic mean of dᵢ (signed, with positive = outward from the reference surface).
Positive Bias means the scan surface lies systematically outside the reference on average
(oversized, e.g. expansive deformation of the arch). Negative Bias means the scan is
systematically undersized on average (inward collapse, e.g. gingival compression from
the scanner wand or from impression material). A near-zero Bias indicates no systematic
directional error. Bias does not capture random variation — use RMS for that.

---

### COMPLETENESS METRICS

**Coverage_pct**
Percentage of GPA reference surface vertices for which the nearest scan vertex is within
0.2 mm. Measures how completely the scanner captured the reference anatomy. 100% means
the scanner imaged every part of the reference. Values below ~90% indicate regions the
scanner failed to image, either because of limited patient cooperation, restricted scanner
range, soft-tissue obstruction, or tracking loss.

**Boundary_mm**
Total length of open-boundary edges (edges belonging to only one face), in millimetres.
Open boundaries arise at the scan margins and around holes where the mesh is incomplete.
A higher value means the scan has a larger incomplete perimeter. Note: every arch scan
has at least one boundary — the gingival margin — so a non-zero value is normal and
expected. Unusually high values (>50 mm for a single arch) may indicate significant scan
incompleteness beyond what is clinically acceptable.

**Holes**
Number of topological holes, counted as the number of distinct open boundary loops in
the mesh. An ideal, complete arch scan has 1 (the single open gingival boundary loop).
Additional holes indicate missing patches where the scanner lost tracking between video
frames. Each distinct lost-tracking region creates one additional hole in the mesh topology.

**Stitch_deg** [Maximum stitching-artefact angle]
Maximum normal-discontinuity angle between adjacent faces, in degrees. When a scanner
merges two independently captured strips, the seam can produce a sharp angular kink at
the junction even though the underlying anatomy is smooth — a stitching artefact. Values
above ~30° suggest a visible geometric step at a seam. Values above 90° indicate a
prominent stitching fault that may affect the RMS if the seam runs through the analysis
region.

Note: the angle at an open boundary edge (where there is only one adjacent face) is
mathematically reported as 180° — this is a normal artefact of open mesh borders, not
a stitching fault. The `Stitch_deg` value is therefore expected to be near 180° for any
scan with open boundaries, even when there is no stitching problem at all.

---

## 2. Interpreting Tessellation Quality from CSV Values

The tessellation metrics in `trueness_metrics.csv` can be used to build a "fingerprint"
for each scanner's behaviour independent of accuracy.

### The adaptive tessellation signature

A well-designed intraoral scanner concentrates its triangles on anatomically detailed
regions (cusps, fissures, marginal ridges) and uses fewer, larger triangles on flat
surfaces (palate, buccal mucosa). This adaptive behaviour is captured by three metrics
working together:

| What to check | Good sign | Possible concern |
|---------------|-----------|-----------------|
| **ATI** | ≥ 0.15 | < 0.05 (near-uniform tessellation) |
| **DensHighK / DensLowK ratio** | ≥ 2.0 | < 1.2 (sparse where it matters) |
| **Edge_mm** | 0.10–0.20 | > 0.30 (coarse mesh, detail loss) |

### Comparing scanners

When you have results from multiple scanners in the same group, compare ATI and the
density ratio across `Scanner_Model` values. A scanner with high ATI and a large
DensHighK/DensLowK ratio is investing its resolution budget where it matters clinically —
a desirable property for assessment of restoration fit.

Tessellation quality is independent of accuracy: a scanner can be highly adaptive (high ATI)
while also being inaccurate (high RMS), or very accurate with a uniform (low ATI) mesh.
The two sets of metrics answer different questions about scanner design.

### Practical threshold ranges

The ranges below are indicative and depend strongly on study design, the region of interest,
and whether the ROI is restricted to tooth surfaces only. Use them as starting points for
QC review rather than hard pass/fail thresholds.

| Metric | Typical range | Possible concern |
|--------|---------------|-----------------|
| Edge_mm | 0.10–0.25 | > 0.30 (coarse) |
| AspRatio | 1.0–2.0 | > 2.5 (many elongated triangles) |
| MaxAspRatio | 5–20 at borders | > 50 in mesh interior |
| ATI | 0.05–0.35 | < 0.05 (non-adaptive) |
| RMS_mm | 0.04–0.15 | > 0.25 (clinically significant) |
| MAD_mm | 0.03–0.12 | > 0.20 |
| H95_mm | 0.08–0.30 | > 0.50 |
| Bias_mm | −0.05 to +0.05 | \|Bias\| > 0.10 (systematic offset) |
| Coverage_pct | 90–100 | < 85 (significant incompleteness) |

---

## 3. Reading the Difference Images

Difference images (generated in the QC Review tab) are colour-coded surface deviation maps
rendered from an occlusal (top-down) camera position. The colour encoding is:

- **Red** = positive distance = scan surface lies **outside** the reference (oversized / proud)
- **White** = zero deviation (exactly on the reference surface)
- **Blue** = negative distance = scan surface lies **inside** the reference (undersized / recessed)

The colour scale is set to ±H95 of the scan being visualised, clamped to ±2 mm. When
comparing multiple scans visually, be aware that each image may use a different scale unless
you have standardised the colour range externally.

When the image was generated with **Apply ROI template** enabled, vertices outside the
analysis region are shown in dark grey. This makes the boundary of the metric computation
immediately visible and helps verify that the ROI was applied as intended.

### Clinical interpretation guide

| Pattern | Likely cause |
|---------|-------------|
| Widespread red on buccal aspects | Scanner produces an expanded arch |
| Widespread blue on occlusal surfaces | Scanner records teeth as shorter / more recessed than the reference |
| Strong asymmetry (one side red, one blue) | Lateral flex or torsion of the arch; may also indicate a GPA convergence problem in a small group |
| Mixed red/blue with a sharp straight boundary | Stitching artefact or local registration failure at a scan seam |
| Large deviations concentrated at scan margins | Normal — scan borders are always noisy; these are excluded from metric computation by the ROI filter |

### When difference images seem inconsistent with CSV metrics

The CSV metrics are computed on the ROI-filtered vertex set; the difference images show
distances across the entire visible mesh (unless **Apply ROI template** is enabled). It is
normal for the image to appear noisier than the RMS number suggests — large deviations at
scan boundaries are visible in the image but were excluded from the metric computation by
the ROI. Enabling **Apply ROI template** during image generation closes this gap and makes
the visual and numeric results directly comparable.
