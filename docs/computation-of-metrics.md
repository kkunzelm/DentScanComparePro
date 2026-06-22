# Computation of Trueness and Precision Metrics in DentScanComparePro

## 1. Purpose and Scope

This document describes in full detail how DentScanComparePro computes its two primary accuracy statistics — **trueness** and **precision** — for intraoral scanner (IOS) evaluation studies. The explanation uses dental and metrological vocabulary throughout and does not reference internal software routines.

Both metrics are designed to be consistent with the definitions set out in **ISO 5725-1:2023** (*Accuracy (trueness and precision) of measurement methods and results — Part 1: General principles and definitions*) and with the specific test procedures for dental digitizing devices in **ISO 12836:2015** (*Dentistry — Digitizing devices for CAD/CAM systems for indirect dental restorations — Test methods for assessing accuracy*). Where the implementation goes beyond those standards — for instance by computing a full surface deviation field rather than a single scalar distance — this is noted explicitly and the relationship to the ISO definitions is explained.

---

## 2. Study Design and the Role of the Patient Group

A study consists of one or more **scanners** (e.g., Primescan, Trios 5, Medit i700) and one or more **patient groups**. In a clinical cohort study each group is one patient; in a phantom study each group is one standardised cavity depth level (SKD level). The software processes one patient group at a time. Within each group, every scanner contributes a fixed number of repeated measurements (**repetitions**), typically seven.

Each repetition is one complete intraoral scan of the same dentition. The scan is a triangulated surface mesh (STL file) covering the tooth crowns, parts of the gingival margin, and in many cases significant areas of buccal mucosa, alveolar ridge, or palatal tissue that are not relevant to the accuracy evaluation.

---

## 3. Reference Surface Construction

### 3.1 The Generalised Procrustes Analysis (GPA) Mean Mesh

Before any metric can be computed, a **reference surface** for the patient group must be established. The software constructs this reference using **Generalised Procrustes Analysis (GPA)**.

All scans from all scanners for one patient are rigidly aligned to a common coordinate frame through iterated **Iterative Closest Point (ICP)** registration. After convergence, the reference surface is updated to the **mean mesh**: for each vertex of a nominated reference scan, its position is moved to the arithmetic mean of the closest corresponding surface locations on all aligned scans. The process repeats until the mean mesh no longer moves appreciably. The final result is a triangulated surface that represents the consensus tooth geometry across all scanners and all repetitions for that patient — the best available estimate of the true dental anatomy from the measurement data themselves.

This mean-surface concept is consistent with the intent of ISO 5725-1 clause 3.6, which defines the **accepted reference value** as a value attributed by agreement and sometimes substituted for the true value. Where no external ground truth (e.g., a CAD design file or a laboratory scanner measurement) is available, the GPA mean mesh is the operationally defined reference value.

### 3.2 The ROI Mask Mesh (Per-Patient Crown Region)

Patient intraoral scans are not limited to the tooth crowns. The raw scan surface includes the gingival margin, interdental papillae, buccal mucosa, floor of the mouth, and in upper jaw scans often large areas of the palate. These anatomical structures:

- are **non-rigid** between scan repetitions (soft tissue deforms from one scanning session to the next, or even within a single session due to saliva flow, tongue position, and lip pressure);
- are **not clinically relevant** to the dimensional accuracy of crown restorations;
- introduce large surface deviations that do not reflect scanner performance on tooth surfaces.

To restrict the accuracy evaluation to the **tooth crowns** — the clinically relevant geometry — the operator defines a **ROI mask mesh** for each patient. This is a trimmed copy of the GPA mean mesh containing only the triangles that lie within the chosen region of interest. Typically, the mask covers the occlusal and approximal surfaces of all teeth visible in the scan, from the cusp tips down to approximately 1 mm below the cemento-enamel junction (CEJ).

The mask mesh is saved as an independent STL file named `{patientID}_roi_mask.stl`. It has its own vertex set, distinct from the full GPA mean mesh. Let **M** denote the number of vertices in the mask mesh for the current patient. This number is constant across all scans and all scanners within the patient group, because the mask is derived from the single shared reference surface.

---

## 4. Trueness

### 4.1 Definition

**ISO 5725-1 clause 3.7** defines trueness as *"the closeness of agreement between the arithmetic mean of a large number of test results and the true value or the accepted reference value."*

In the context of intraoral scanner evaluation, the "test result" for a single scan is the deviation of the scanner's reproduced tooth surface from the accepted reference surface. ISO 12836 clause 5.2 operationalises this as the mean signed distance between the digitised surface and the reference surface measured by a coordinate measuring machine (CMM). DentScanComparePro extends this scalar concept to a full per-vertex surface deviation field, which yields richer diagnostic information (spatial distribution of errors, Hausdorff distances, coverage statistics) while remaining entirely consistent with the ISO definition.

### 4.2 Step-by-Step Computation

The following procedure is executed once per scan (one scanner, one patient, one repetition).

#### Step 1 — Rigid alignment of the scan to the reference

Each scan is rigidly registered to the GPA mean reference using ICP. For patient cohort studies with extensive soft tissue, **Trimmed ICP (TrICP)** is used: at each ICP iteration, the point-to-plane residuals of all tentative correspondences are sorted in ascending order and only the best fraction (typically 50 %) are used to solve for the rigid transform. Because tooth surfaces are rigid, they contribute small residuals and are retained; because soft tissue deforms, it contributes large residuals and is discarded. The final transform positions the scan in the same coordinate frame as the GPA mean mesh and the ROI mask mesh.

#### Step 2 — Distance computation from mask surface to scan surface

For each vertex **k** of the ROI mask mesh (k = 1 … M), the software finds the **nearest point** on the registered scan surface. This is a spatial query: given the three-dimensional coordinates of mask vertex k, which triangle of the (aligned) scan mesh is closest, and where on that triangle does the minimum distance occur? The answer is a point on the scan surface.

The **signed distance** `d_k` is then:

```
d_k = +||mask_vertex_k − nearest_scan_point_k||   if the scan surface lies outside the reference
d_k = −||mask_vertex_k − nearest_scan_point_k||   if the scan surface lies inside the reference
```

The sign is determined by the dot product of the displacement vector with the outward unit normal of the mask mesh face closest to the query point. A positive distance means the scanner reproduced the tooth surface proud of the reference (the scan is thicker than the reference predicts); a negative distance means the scan is recessed (the scanner underestimated the local tooth dimension).

This produces a vector of M signed distances:

```
D = [d_1, d_2, …, d_M]   (units: millimetres)
```

All M values are used in the metric computation; no additional vertex filtering is applied in the mask path, because the mask mesh already contains only the crown region of interest.

#### Step 3 — Scalar metrics derived from the distance vector

The following metrics are computed from **D**:

**Root Mean Square distance (RMS)**

```
RMS = sqrt( (1/M) × sum_{k=1}^{M} d_k^2 )
```

RMS is the primary trueness metric. It weights large deviations heavily and is sensitive to outlier areas. ISO 12836 section 5.2 references the use of RMS deviation as the standard descriptor of surface accuracy for dental digitizing devices.

**Mean Absolute Deviation (MAD)**

```
MAD = (1/M) × sum_{k=1}^{M} |d_k|
```

MAD gives equal weight to all deviations regardless of sign. It is more robust to a small number of extreme outlier triangles at the scan boundary than RMS. Note that in the CSV output the column is labelled `MAD_mm`; the value is the **mean** absolute deviation, not the median absolute deviation.

**Bias (signed mean distance)**

```
Bias = (1/M) × sum_{k=1}^{M} d_k
```

Bias captures the **systematic dimensional error**: a positive Bias indicates the scanner consistently reproduced tooth surfaces proud of the reference (overcounting material), while a negative Bias indicates consistent undercounting. This corresponds directly to the ISO 5725-1 concept of **bias** (clause 3.8): *"the difference between the expectation of the test results and an accepted reference value."*

**Hausdorff distance at the 100th percentile (H100)**

```
H100 = max( |d_1|, |d_2|, …, |d_M| )
```

H100 is the maximum absolute deviation observed anywhere on the mask surface. It is dominated by isolated artefacts at the scan margins or stitching seams and is therefore a worst-case indicator rather than a representative accuracy measure.

**Hausdorff distance at the 95th percentile (H95)**

The absolute distances |d_k| are sorted in ascending order and the value at position floor(0.95 × M) is taken:

```
H95 = |d|_(0.95×M)   (95th order statistic of the absolute distance vector)
```

H95 is clinically meaningful: it states that 95 % of the tooth crown surface is reproduced within this distance of the reference. It is more robust than H100 and is widely used in dental accuracy studies.

**Coverage rate**

```
Coverage = (100/M) × count{ k : |d_k| ≤ 0.2 mm }
```

Coverage measures the fraction of the reference crown surface that the scanner reproduced within 0.2 mm. Values below 90 % typically indicate that significant portions of the crown were not captured or are heavily distorted. The threshold of 0.2 mm has no specific ISO mandate but is widely used in the dental literature as a clinically acceptable dimensional tolerance for full-arch IOS evaluation.

### 4.3 Consistency with ISO 5725-1 and ISO 12836

ISO 5725-1 clause 3.7 defines trueness as the closeness between the **mean** of many test results and the accepted reference value. In our implementation, the "measurement result" for one scan is the full deviation field D = [d_1 … d_M]. The RMS and Bias metrics map directly to the ISO concepts:

| ISO 5725-1 concept | Implementation equivalent |
|---|---|
| Accepted reference value | GPA mean mesh trimmed to the ROI mask |
| Test result (single measurement) | Signed-distance field D at all M mask vertices |
| Bias (systematic error) | `Bias_mm` = mean(D) |
| Measure of spread around the reference | `RMS_mm` = sqrt(mean(D²)) |

ISO 12836:2015 section 5.2.3 specifies that trueness is assessed by computing the deviation between the digitised surface and a CMM-measured reference, expressed as "mean error" and "RMS error". Our RMS is precisely the "RMS error" of ISO 12836 when the accepted reference is the GPA mean. When an external reference (CAD file or laboratory scanner measurement) is supplied, the computation is identical but uses the external surface instead of the GPA mean, making the result fully equivalent to the ISO 12836 CMM-reference procedure.

---

## 5. Precision

### 5.1 Definition

**ISO 5725-1 clause 3.9** defines precision as *"the closeness of agreement between independent test results obtained under stipulated conditions."* When the stipulated conditions are those of **repeatability** (same scanner, same patient, same operator, short time interval), precision becomes **repeatability** (ISO 5725-1 clause 3.13). The standard recommends expressing precision as the standard deviation of replicate measurements.

In clinical IOS studies the measurement unit is not a scalar quantity but a three-dimensional surface; a single scan does not produce one number but a surface mesh with tens of thousands of vertices. The precision metric must therefore characterise how closely two surface meshes of the same tooth agree with each other — a surface-to-surface comparison rather than a scalar comparison.

### 5.2 Step-by-Step Computation

#### Step 1 — Reuse of the distance vectors from trueness computation

A key design principle is that precision uses exactly the same per-vertex distance values as trueness, avoiding any additional geometric computation.

After the trueness computation, every scan in the patient group has an associated vector of M signed distances: the signed distance from each of the M mask-mesh vertices to the nearest point on that scan's surface. Call these:

```
D_i = [d_{i,1}, d_{i,2}, …, d_{i,M}]   for repetition i of scanner S
D_j = [d_{j,1}, d_{j,2}, …, d_{j,M}]   for repetition j of scanner S
```

Both vectors are indexed by the **same M mask vertices** in the same order. This is guaranteed because the mask mesh does not change within a patient group: every scan, regardless of scanner brand, is registered to the same GPA mean mesh, and the mask is a fixed submesh of that reference.

#### Step 2 — Pairwise surface difference for one scan pair

The pairwise surface difference between repetitions i and j of the same scanner is:

```
ΔD_{ij} = D_i − D_j = [d_{i,1}−d_{j,1},  d_{i,2}−d_{j,2},  …,  d_{i,M}−d_{j,M}]
```

Each element `d_{i,k} − d_{j,k}` quantifies how differently the two scans reproduced the tooth surface **at the same spatial location** (mask vertex k). If scan i is 0.08 mm proud at the central fossa of tooth 36 and scan j is 0.03 mm proud at the same location, the difference at that vertex is 0.05 mm.

The **pairwise RMS** for this scan pair is:

```
pairRMS(i, j) = sqrt( (1/M) × sum_{k=1}^{M} (d_{i,k} − d_{j,k})^2 )
              = sqrt( mean( ΔD_{ij}^2 ) )
```

This is the root mean square of the pointwise differences across all M mask surface locations. It is zero when both scans reproduce the reference surface identically at every point, and grows with inter-repetition variability.

#### Step 3 — All pairwise combinations within a scanner × patient cell

For N repetitions of the same scanner for the same patient, there are N×(N−1)/2 unique unordered pairs. For seven repetitions (as in the P2026-Nold study), this gives 7×6/2 = **21 pairs**.

Each pair contributes one `pairRMS` value. The 21 values form a distribution:

```
P = [ pairRMS(1,2),  pairRMS(1,3),  …,  pairRMS(6,7) ]
```

#### Step 4 — Summary statistics for the precision report

The following statistics are computed from **P** and written to `precision_metrics.csv`:

**Mean pairwise RMS (`Precision_MeanRMS_mm`)**

```
Precision_MeanRMS = (1/C) × sum_{(i,j)} pairRMS(i,j)
```

where C = N×(N−1)/2 is the number of pairs. This is the primary precision metric.

**Standard deviation of pairwise RMS (`Precision_SD_mm`)**

```
Precision_SD = sqrt( (1/(C−1)) × sum_{(i,j)} (pairRMS(i,j) − Precision_MeanRMS)^2 )
```

A high SD indicates inconsistent scan quality across repetitions — some pairs agree well while others diverge.

**Coefficient of Variation (`Coefficient_of_Variation`)**

```
CV = Precision_SD / Precision_MeanRMS
```

CV is dimensionless and allows comparison of precision across different scanner models or different patient groups that may have different absolute deviation scales.

**Pairwise count (`Pairwise_Count`)**

The number of pairs actually used in the computation. Equal to N×(N−1)/2 when all repetitions are present; smaller when one or more scans were flagged as errands and excluded from the analysis.

### 5.3 Consistency with ISO 5725-1 and ISO 12836

The relationship between our pairwise-RMS precision metric and the ISO 5725-1 repeatability standard deviation is as follows.

ISO 5725-1 clause 3.13 defines the **repeatability standard deviation** σ_r as the standard deviation of the distribution of individual test results obtained under repeatability conditions. If the N replicate scans of one scanner for one patient are independent and identically distributed with true deviation variance σ² (at each mask vertex), then the expected value of pairRMS(i,j) equals:

```
E[ pairRMS(i,j) ] = sqrt( Var(d_{i,k} − d_{j,k}) )
                   = sqrt( 2 × σ² )
                   = σ_r × sqrt(2)
```

(Here σ² is the vertex-level repeatability variance averaged over all M mask vertices, assuming stationarity.) Consequently:

```
σ_r = Precision_MeanRMS / sqrt(2)
```

This factor of sqrt(2) arises because pairwise differences magnify variability: if each repetition independently deviates from the reference with standard deviation σ_r, the difference between two independent repetitions has standard deviation σ_r×sqrt(2). The pairwise RMS metric is therefore a direct linear transform of the ISO 5725-1 repeatability standard deviation. Researchers wishing to report results in ISO 5725-1 notation should divide `Precision_MeanRMS_mm` by sqrt(2) ≈ 1.414.

ISO 12836:2015 section 5.3.2 states that precision for dental digitizing devices is expressed as the standard deviation of repeated digitisations. Our metric is consistent with this requirement: the `Precision_SD_mm` column records the spread of the 21 pairwise RMS values, which estimates the variability of the underlying repeat-measurement distribution.

---

## 6. Why Trueness and Precision Use the Same Distance Values

A central design decision in DentScanComparePro is that both trueness and precision are derived from the same set of per-mask-vertex signed distances D_i, D_j, etc., computed once during the batch run.

**Trueness** asks: *how far is scan i from the accepted reference surface?*

```
RMS trueness of scan i = sqrt( mean_k( d_{i,k}^2 ) )
```

**Precision** asks: *how much do scans i and j differ from each other?*

```
pairRMS(i,j) = sqrt( mean_k( (d_{i,k} − d_{j,k})^2 ) )
```

Both computations operate on the identical M-dimensional distance vectors. The mask surface serves as a common spatial registration grid: every scanner's deviation from the reference is sampled at exactly the same M anatomical locations on the tooth crown. This has three important consequences:

1. **Dimensional consistency**: trueness and precision are expressed in the same physical units and can be meaningfully compared on the same scale. A scanner with trueness RMS of 0.08 mm and precision mean RMS of 0.04 mm is accurate and reproducible; a scanner with trueness 0.08 mm and precision 0.12 mm is accurate on average but highly variable between sessions.

2. **No phantom geometry artefacts**: in the fallback path (phantom studies without a mask STL), precision is computed by nearest-surface queries from scan_i vertices to the scan_j mesh. Because different scanners produce meshes with different vertex densities and different triangle shapes, the query distances depend partly on the mesh sampling and not purely on the geometric deviation. The mask-based approach eliminates this artefact by using a fixed set of reference query points common to all scanners.

3. **Algebraic decomposition**: the mean squared trueness RMS over all repetitions of one scanner can be decomposed into a systematic (bias) component and a random (precision) component:

```
mean_i( RMS_i^2 ) ≈ Bias^2 + (Precision_MeanRMS / sqrt(2))^2
```

This follows from the standard variance decomposition `E[X²] = (E[X])² + Var(X)` applied to the per-vertex distances. The decomposition is only approximate because the pairwise RMS pools across all pair combinations rather than computing a standard variance directly, but it holds exactly in the limit of many repetitions and Gaussian deviations. This decomposition is consistent with the ISO 5725-1 accuracy model, which relates accuracy, trueness (bias), and precision (random error) through the identity: accuracy = trueness + precision.

---

## 7. Scope of the Analysis Region and Its Effect on the Metrics

The ROI mask mesh defines which part of the tooth surface is included in the analysis. All metrics — both trueness and precision — apply exclusively to the M vertices of this mask mesh. The mask deliberately **excludes**:

- **Gingival tissue** (free gingiva, interdental papillae, gingival sulcus): soft tissue deforms non-rigidly between scan repetitions. Including gingival distances in a rigidity-assuming ICP alignment corrupts the transform; including them in the metrics adds irreproducible deviations that are unrelated to scanner dimensional accuracy.
- **Buccal mucosa, floor of mouth, palate**: large non-rigid surfaces that inflate all deviation statistics and carry no information about crown surface quality.
- **Scan borders**: the perimeter of the scan typically shows boundary artefacts (stretched triangles, elevation steps). Excluding the scan border removes these artefacts from the statistics.
- **Areas not common to all scanners**: some scanners capture further into the posterior segments or onto the soft palate while others do not. Only the anatomical region consistently captured by all scanners should contribute to a fair inter-scanner comparison.

The effect of the mask on the numeric results is substantial. For the P2026-Nold patient cohort study, full-mesh analysis (no ROI mask) typically yields trueness RMS values in the 0.3–0.8 mm range, dominated by gingival deformation. Restricted to the crown surfaces (mask active), trueness RMS values fall to 0.05–0.15 mm, which reflects the actual dimensional fidelity of the scanner on rigid tooth geometry.

---

## 8. Trueness per Scan vs. Trueness per Scanner

### 8.1 The per-scan row in trueness_metrics.csv

Every scan (one scanner, one patient, one repetition) contributes one row to `trueness_metrics.csv`. The `RMS_mm` value in that row is the trueness of that specific scan against the GPA mean reference for that patient. It is not averaged over repetitions; it is not averaged over patients.

### 8.2 The per-scanner summary

`summary_stats.csv` aggregates all rows with the same `(Scanner_Model, Group_ID)` pair. For a patient cohort study, `Group_ID` is a patient ID, so each row in the summary file represents the mean of 7 repetitions of one scanner for one patient.

There is **no pre-aggregated per-scanner-across-all-patients value** in the CSV output. Obtaining a single trueness number per scanner that accounts for patient variability requires fitting a mixed-effects model:

```
RMS_mm ~ Scanner + (1 | Patient_ID)
```

in which `Scanner` is a fixed effect and `Patient_ID` is a random effect. The fixed-effect estimate for each scanner is the BLUP-adjusted mean trueness, accounting for systematic patient-level differences. This model is implemented in `scripts/analyze_results_Nold.R`.

### 8.3 Why a simple mean over patients would be misleading

If one patient has a particularly deep cavity or extensive buccal mucosa coverage, their reference-to-scan distances may be systematically larger than for other patients — not because the scanner is less accurate for that patient, but because the GPA mean is less stable when the patient's anatomy is challenging. A simple mean of per-patient RMS values would weight each patient equally regardless of how many valid repetitions they contributed and regardless of the quality of their GPA reference. The mixed-effects model accounts for these imbalances.

---

## 9. Precision per Scanner × Patient Cell

### 9.1 The per-cell row in precision_metrics.csv

Every `(Scanner_Model, Group_ID)` cell contributes exactly one row to `precision_metrics.csv`. For 4 scanners and 16 patients, there are 64 rows (minus any cells where fewer than 2 repetitions were available, e.g., Medit700 for patient 008 in P2026-Nold).

### 9.2 Why there are far fewer rows than in trueness_metrics.csv

`trueness_metrics.csv` has one row per scan: 4 scanners × 16 patients × 7 repetitions = 448 rows (for a complete balanced design).

`precision_metrics.csv` has one row per scanner × patient cell: 4 × 16 = 64 rows.

The ratio is the number of repetitions per cell (7). This is by design: precision collapses the within-cell variation into a single summary statistic per cell. The underlying 21 pairwise RMS values are not individually written to the CSV.

---

## 10. Summary of Metric Formulas

All formulas below use the notation introduced in the preceding sections: M = number of mask vertices; D_i = signed-distance vector for scan i; d_{i,k} = k-th element of D_i; C = N×(N−1)/2 = number of scan pairs within the cell.

### Trueness metrics (one row per scan)

| Metric | Formula | ISO reference |
|---|---|---|
| RMS | `sqrt( mean_k(d_{i,k}²) )` | ISO 12836 §5.2.3 "RMS error" |
| MAD | `mean_k( |d_{i,k}| )` | — |
| Bias | `mean_k( d_{i,k} )` | ISO 5725-1 clause 3.8 bias |
| H100 | `max_k( |d_{i,k}| )` | — |
| H95 | 95th percentile of `|d_{i,k}|` | — |
| Coverage | `count(|d_{i,k}| ≤ 0.2 mm) / M × 100` | — |

### Precision metrics (one row per scanner × patient cell)

| Metric | Formula | ISO reference |
|---|---|---|
| pairRMS(i,j) | `sqrt( mean_k( (d_{i,k}−d_{j,k})² ) )` | ISO 5725-1 §3.13 repeatability |
| Precision_MeanRMS | `mean over all C pairs of pairRMS(i,j)` | ISO 12836 §5.3.2 |
| Precision_SD | sample standard deviation of C pairRMS values | ISO 5725-1 §3.9 precision |
| CV | `Precision_SD / Precision_MeanRMS` | — |
| σ_r (ISO notation) | `Precision_MeanRMS / sqrt(2)` | ISO 5725-1 repeatability SD |

---

## 11. Limitations

1. **GPA mean as reference**: when no external ground truth is available, the GPA mean is both the reference and a product of the data being evaluated. Systematic biases common to all scanners (e.g., all scanners consistently overcounting the buccal cusp height) will not be visible in the trueness metrics because the reference incorporates that bias. For absolute trueness against a known geometry, an external reference (CAD model or CMM measurement) must be supplied.

2. **Single-direction distance (mask to scan)**: the signed distance is computed from each mask vertex to the nearest point on the scan surface, not vice versa. This is a directed (asymmetric) distance. For a scan that completely covers the reference region, the directed distance is a good approximation of the true surface deviation. For scans with incomplete coverage (holes, missing segments), the nearest-surface query may find a point far from the intended corresponding location. The coverage rate metric partially captures this.

3. **Rigid registration assumption**: ICP and GPA assume that the tooth surfaces are rigid bodies. Slight elastic deformation of teeth under the scanner's LED pressure, thermal expansion, or vibration-induced movement violates this assumption. In practice, for intraoral scanners operating at normal clinical pressure, these effects are below 0.01 mm and are negligible relative to the measurement uncertainties of current IOS devices.

4. **Pairwise precision vs. ISO repeatability standard deviation**: as noted in Section 5.3, the pairwise RMS metric equals σ_r × sqrt(2) rather than σ_r directly. Researchers comparing results to published studies that report repeatability SD in ISO 5725-1 notation should apply the sqrt(2) correction.

5. **Patient-specific reference**: in a cohort study each patient has a different GPA mean reference, a different mask, and a different number of mask vertices M. The trueness and precision values from different patients are therefore not computed on identical spatial grids. Comparing raw RMS values across patients requires caution; the mixed-effects model accounts for this by treating patient as a random effect.
