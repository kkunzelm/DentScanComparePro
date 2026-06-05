Batch mode:

./DentScanComparePro --batch --study /home/kkunzelm/claude-code/DentScanComparePro/data/full_study.json --data-root /KHKsData/usr2/daten/P2026-Kessler-ScanVergleich/IOS/results_align/normalized/ --output /KHKsData/usr2/daten/P2026-Kessler-ScanVergleich/IOS/results_batch/ --external-ref "/KHKsData/usr2/daten/P2026-Kessler-ScanVergleich/IOS/results_align/normalized/references/Referenzscan mit Tischscanner Hauslabor.stl" --pre-aligned --verbose

Langfristig: Ich benötige ein Modul, das mir erlaubt GPA mit normalisierten Daten als neue Referenz zu berechnen. 


KHK: qc bringt mir nur was, wenn master und model sind nebeneinder richtig orientiert, difference image ist schwer zu interpretieren.

##############

Ready to code?

 Here is Claude's plan:
╌╌╌╌╌╌╌╌╌╌╌╌╌╌╌╌╌╌╌╌╌╌╌╌╌╌╌╌╌╌╌╌╌╌╌╌╌╌╌╌╌╌╌╌╌╌╌╌╌╌╌╌╌╌╌╌╌╌╌╌╌╌╌╌╌╌╌╌╌╌╌╌╌╌╌╌╌╌╌╌╌╌╌╌╌╌╌╌╌╌╌╌╌╌╌╌╌╌╌╌╌╌╌╌╌╌╌╌╌╌╌╌╌
 Implementation Plan: Masked ICP with DentScanAlign Integration and Enhanced QC 

 Overview

 Three enhancements to improve registration accuracy and QC workflow:
 1. Load DentScanAlign JSON transforms as ICP initialization
 2. Use tooth segmentation mask for ICP alignment (not just metrics)
 3. Enhanced QC visualization showing reference + scan overlay

 --- 
 Feature 1: Load DentScanAlign JSON Transforms 

 Goal

 Use pre-computed transforms from DentScanAlign as starting point for ICP refinement. 

 Changes

 New file: src/core/AlignmentTransformLoader.h/.cpp
 - Parse JSON files from alignments/ directory 
 - Extract transform_4x4 (16-element row-major array) → Eigen::Matrix4d
 - Return map: normalized source_file path → transform

 Modify: src/config/StudyConfig.h/.cpp
 - Add field: QString alignmentsDirectory
 - Parse from JSON config 

 Modify: src/main.cpp
 - Add CLI option: -a, --alignments <directory>

 Modify: src/batch/GroupProcessor.cpp 
 - After loading scans, apply precomputed transforms before ICP
 - Match by normalizing source_file path from JSON

 --- 
 Feature 2: Masked ICP Using Tooth Segmentation

 Goal

 Focus ICP alignment on tooth surfaces only, excluding gingiva.

 Key Change: Reorder Pipeline

 Current order:
 1. Load → 2. Curvature → 3. GPA/ICP → 4. Distances → 5. Tooth masks → 6. Metrics

 New order:
 1. Load → 2. Curvature → 3. Tooth masks → 4. Masked ICP → 5. Distances → 6. Metrics

 Changes

 Modify: src/batch/GroupProcessor.cpp 

 Move computeToothMasks() before alignment stage, then: 

 // Use existing ICPRegistration::alignMasked()
 if (!toothMasks[i].empty()) {
  auto result = ICPRegistration::alignMasked(*scan, refData, toothMasks[i], icpParams);
 } else {
  auto result = ICPRegistration::align(*scan, refData, icpParams);
 }

 The existing alignMasked() function already:
 - Filters to faces where ALL 3 vertices have mask[v] == true 
 - Falls back to full mesh if masked region < 500 faces 

 --- 
 Feature 3: Enhanced QC Visualization 

 Goal

 Show reference mesh + aligned scan + difference coloring together for QC review.

 Design Decisions

 - New dialog: Create AlignmentQCDialog (separate from ErrandResolutionDialog which handles landmark 
 re-registration)
 - Trigger: Double-click on thumbnail in QCReviewWidget 

 Changes

 New file: src/qc/AlignmentQCDialog.h/.cpp

 Dialog layout:
 ┌─────────────────────────────────────────────────┐ 
 │  [Show Reference: ☑]  [Overlay Mode: ☑]│ 
 ├─────────────────────────────────────────────────┤ 
 │ │ 
 │Reference (grey wireframe)  │ 
 │+ Scan (distance colored, solid)  │ 
 │ │ 
 ├─────────────────────────────────────────────────┤ 
 │  RMS: 0.045 mm | Max: 0.32 mm | Coverage: 98%  │
 ├─────────────────────────────────────────────────┤ 
 │[Accept]  [Flag Errand]  [Skip]│ 
 └─────────────────────────────────────────────────┘ 

 Modify: src/visualization/VTKMeshWidget.h/.cpp

 Add method:
 void showAlignmentOverlay(
  const std::shared_ptr<ScanData>& scan, 
  const std::shared_ptr<SurfaceMesh>& reference,
  double rangeMin = -0.5, double rangeMax = 0.5); 

 Implementation: 
 - Scan: distance-colored solid surface (existing showDistanceMap)
 - Reference: grey wireframe overlay (new)

 Modify: src/gui/MainWindow.cpp 
 - Connect QCReviewWidget double-click to open AlignmentQCDialog 
 - Handle accept/reject results 

 --- 
 Files to Modify 
 ┌──────────────────────────────────────────┬───────────────────────────────┐
 │ File │Change │
 ├──────────────────────────────────────────┼───────────────────────────────┤
 │ src/core/AlignmentTransformLoader.h/.cpp │ NEW - JSON parser │
 ├──────────────────────────────────────────┼───────────────────────────────┤
 │ src/config/StudyConfig.h/.cpp│ Add alignmentsDirectory field │
 ├──────────────────────────────────────────┼───────────────────────────────┤
 │ src/main.cpp  │ Add --alignments CLI option│
 ├──────────────────────────────────────────┼───────────────────────────────┤
 │ src/batch/GroupProcessor.cpp │ Reorder pipeline, masked ICP  │
 ├──────────────────────────────────────────┼───────────────────────────────┤
 │ src/visualization/VTKMeshWidget.cpp│ Add overlay rendering│
 ├──────────────────────────────────────────┼───────────────────────────────┤
 │ src/qc/AlignmentQCDialog.h/.cpp │ NEW - QC dialog│
 ├──────────────────────────────────────────┼───────────────────────────────┤
 │ src/gui/MainWindow.cpp │ Wire up QC dialog │
 ├──────────────────────────────────────────┼───────────────────────────────┤
 │ src/CMakeLists.txt  │ Add new source files │
 └──────────────────────────────────────────┴───────────────────────────────┘
 --- 
 Verification 

 1. CLI test with alignments:
 ./DentScanComparePro --batch \ 
--study study.json \
--data-root normalized/ \ 
--alignments alignments/ \
--external-ref reference.stl \
--pre-aligned --verbose
- Verify "loaded N transforms" message
- Verify lower RMS values with masked ICP
 2. GUI QC test: 
- Run batch processing 
- Open QC Review tab
- Double-click a scan thumbnail 
- Verify reference wireframe + colored scan overlay
- Test Accept/Reject buttons 
 3. Compare RMS values:
- Run with mask vs without mask 
- Masked ICP should produce lower RMS on tooth surfaces

























----

Build successful. Here's what I've added to help with mesh orientation:
 
  Visual aids:
 
  1. Coordinate axes indicator (bottom-left corner) - Shows X/Y/Z orientation in each view so you can see which direction is
"up"
  2. Backface coloring - The mesh now shows:
 - Light beige: Front face (occlusal surface when viewed from above)
 - Dark brownish-grey: Back face (viewing from inside/below the arch) 
  3. Numbered landmark labels - Blue numbered labels (1, 2, 3...) appear next to each picked point
  4. "Occlusal View" buttons - Under both Reference and Scan panels to set camera looking down Z-axis
 
  How to use:
  1. Click "Occlusal View" on both panels to set consistent camera angles
  2. The light-colored surface should face you (that's the occlusal/top side)
  3. The axes indicator shows you the coordinate system
  4. Pick landmarks on same anatomical features - verify numbers match
 
  The GPA reference likely has Z pointing "up" (occlusal). If your scan has Y pointing "up", it will appear rotated - that's
expected before alignment. The key is to pick same anatomical points (e.g., cusp tip of tooth 16) on both meshes
  regardless of their rotation. 





  Build successful. The orientation widget is now initialized lazily when the first mesh is loaded (avoiding the
  interactor-not-ready crash). 
 
  Changes summary: 
  1. XYZ axes indicator - Appears in bottom-left corner when mesh loads
  2. Backface coloring - Dark color when viewing mesh from inside/below
  3. "Occlusal View" buttons - Under Reference and Scan panels
  4. Numbered landmark labels - Blue numbered tags on picked points
 
  Please test again. When you load meshes in the ErrandResolutionDialog, you should see the coordinate axes appear in the
  corner of each view. 









Ich benötige User zentrierte Anleitung, zum Workflow. 

Plane und Bondingbox funktionieren einwandfrei. 
Wie funktioniert Brush Tool?

Kann ich in Study Configuration einzelne Bedingungen (Scanner, SKD) aus- und abwählen?

Workflow nochmal überprüfen:

wir haben globale Registrierung und können diese durch ein Template optimieren. Template kann Zahnreihe oder weniger sein.

Wir haben QC relativ spät im Prozess.

KHK: Reihenfolge: 

- globale Registrierung
- QC
- Auswertung 1
- lokale optimierte Registrierung via Template
- Auswertung 2

Kann man bei den im  QC akzeptierten Registrierungen die Transformation auf eine Kopie des Datensatzes anwenden. Dann wäre im nächsten Schritt kein Fehler mehr möglich, es wäre schon eine Art Grobjustierung vorhanden. Die Daten wären alle richtig orientiert. 


++++++++++++++++++++++++++++++++


Understood. Here's a quick summary of where we are:
 
  Completed: 
  - Batch processing now exports qc/gpa_means/*.stl and qc/transforms/*.json
  - Difference images disabled in batch mode (VTK headless crash)
  - Updated docs/developer-handoff.md with QC workflow documentation
  - Created docs/IMPLEMENTATION-PROMPT-Part4.md for continuation
 
  Next steps when you return:
  1. Add "Generate Difference Images" button to GUI QC Review tab
  2. This will loop through transforms, load scans, render images with VTK (works in GUI mode)
  3. Test the full QC review workflow
 
  The data is ready in:
  - /KHKsData/usr2/daten/P2026-Kessler-ScanVergleich/IOS/results_batch/qc/gpa_means/ 
  - /KHKsData/usr2/daten/P2026-Kessler-ScanVergleich/IOS/results_batch/qc/transforms/



  KHK Analyse neu:
  ================

  ### Research Context

The project compares different intraoral scanners across clinical situations (SKD levels). Key insights:

1. **Multiple reference standards available:**
- GPA Mean (computed consensus from all scans)
- CAD STL (ground truth design intent)
- Laboratory scanner (high-accuracy physical measurement)

2. **Two-stage evaluation needed:**
- **Full mesh**: Teeth + gums (higher RMS due to gum deformation)
- **Teeth only**: Rigid structures, expect lower RMS

3. **QC is mandatory early**: Before statistics, must confirm registration found global minimum

4. **Transform reuse**: Apply successful full-mesh registration as starting point for masked evaluation

### Abstract Workflow Model

```
┌─────────────────────────────────────────────────────────────────────┐
│  PHASE 1: REGISTRATION + QC (Mandatory Checkpoint)│
│  ═══════════════════════════════════════════════════ │
││
│  Input: Raw STL scans (variable orientation)│
│  Output: Validated transforms (4×4 matrices)│
││
│  ┌──────────┐ ┌──────────┐ ┌──────────┐ ┌──────────┐│
│  │  Coarse  │───▶│Fine│───▶│  Visual  │───▶│ Approved ││
│  │ Alignment│ │ICP │ │ QC │ │Transforms││
│  └──────────┘ └──────────┘ └────┬─────┘ └──────────┘│
││  │
│▼ (if failed)  │
│ ┌──────────┐  │
│ │ Landmark │  │
│ │Pre-align │───▶ Re-run ICP  │
│ └──────────┘  │
││
│  Reference options: GPA Mean | CAD STL | Lab Scanner │
└─────────────────────────────────────────────────────────────────────┘
│
▼ (transforms only, QC approved)
┌─────────────────────────────────────────────────────────────────────┐
│  PHASE 2: METRIC EVALUATION (Multiple Passes)  │
│  ════════════════════════════════════════════  │
││
│  Input: Approved transforms + Original STLs + Reference(s) │
│  Output: Metrics CSVs per evaluation pass│
││
│  ┌─────────────────────────────────────────────────────────────┐ │
│  │  PASS: "full"  │ │
│  │  • Apply saved transform to original STL  │ │
│  │  • Compute distances to reference (full mesh)│ │
│  │  • Output: trueness_full.csv, precision_full.csv│ │
│  └─────────────────────────────────────────────────────────────┘ │
││
│  ┌─────────────────────────────────────────────────────────────┐ │
│  │  PASS: "teeth_only"  │ │
│  │  • Apply saved transform (from "full" pass)  │ │
│  │  • Apply segmentation mask (teeth template)  │ │
│  │  • Optional: Refine registration on masked region  │ │
│  │  • Compute distances (masked vertices only)  │ │
│  │  • Output: trueness_teeth.csv, precision_teeth.csv │ │
│  └─────────────────────────────────────────────────────────────┘ │
││
│  Additional passes possible: ROI regions, individual teeth, etc. │
└─────────────────────────────────────────────────────────────────────┘
```

### Key Abstractions

#### 1. Reference Source (Pluggable)

```
ReferenceType:
  - GPA_MEAN→ Computed from scan group (current implementation)
  - EXTERNAL_STL  → CAD file or lab scanner per group
  - MULTI_TARGET  → Compare against multiple references simultaneously
```

#### 2. Evaluation Pass (Repeatable)

```
EvaluationPass:
  - name: "full" | "teeth_only" | "roi_region" | ...
  - mask: none | template_path
  - reference: which reference mesh(es) to compare against
  - refine_registration: bool (fine-tune ICP on masked region)
  - inherit_transform_from: null | "pass_name"
```

#### 3. Transform as First-Class Output

```
Transform persistence:
  - Saved after registration (JSON with 4×4 matrix)
  - Can be applied to any STL (load → transform → evaluate)
  - Can be refined (masked ICP) and saved as new version
  - Enables: full → masked workflow without full re-registration
```

#### 4. QC as Mandatory Gate

```
Pipeline stages:
  1. Registration → produces transforms (NOT metrics)
  2. QC Review → approve/reject/fix each transform
  3. Metrics → only computed on QC-approved transforms
```

### Proposed Configuration Structure

```yaml
study:
  name: "Scanner Comparison Study"

references:
  gpa_mean: { type: computed }
  cad: { type: external, path_pattern: "{group}_cad.stl" }
  lab_scanner: { type: external, path_pattern: "{group}_lab.stl" }

registration:
  primary_reference: gpa_mean # or cad, lab_scanner
  coarse_method: pca # or landmarks
  fine_method: icp
  qc_required: true  # Mandatory checkpoint before metrics

evaluation_passes:
  - name: full
 mask: none
 reference: [gpa_mean, cad, lab_scanner]  # Compare to all three

  - name: teeth_only
 mask: teeth_template.json
 inherit_transform: full# Reuse alignment from full pass
 refine_registration: true # Fine-tune on teeth only
 reference: [gpa_mean, cad, lab_scanner]
```


# Active Issues Under Investigation

#### 1. Coordinate System Mismatch (90-Degree Rotation)

Different scanners use different coordinate systems:
- **Some scanners**: Y-axis is "up" (occlusal direction)
- **GPA reference**: Z-axis is "up" (occlusal direction)

# User analysis using dental/anatomical nomenclature, Direktions like dorsal-ventral express a direktion. Increasing values of this axis are into the direction ventral.

3D datasets where open in Meshlab, axes where displayed
Dataset is upper jaw (teeth and gums)

"Referenzscanner Hauslabor"

z = dorsal-ventral
x = transersal
y = Zunahme in Richtung Höckerspitzen

Referenzscan "CAD"

Koordinaten nicht mit anatomischen Ebenen orientiert
y = fast dorsal ventral
x = fast transversal
z = Zunahme in Richtung Wurzelspitzen (apikal)
Rotation um z ca. 15 Grad
-----

iTeroLumina:

SKD30, SKD28
xyz intuitiv richtig, 
zunehmende Z-Werte in Richtung Höckerspitzen, 
x = transversal
y = ventral-dorsal
z = senkrecht dazu

Es gibt auch Varianten

SKD26, SKD24, SKD22, SKD20, SKD18
x = transversal
y = ventral-dorsal
z = Zunahme in Richtung Wurzelspitze


---------

Trios5:

SKD30, SKD28, SKD22, SKD20
z = dorsal-ventral 
x = transversal
y = Zunahme in Richtung Wurzelspitze

SKD26, SKD24, 
z = dorsal-ventral
x = transversal
y = Zunahme in Richtung Höckerspitzen

--------

# Details contributing to missing standarization of origin and axes asignment:

### STL Winding and Normal Orientation

Primescan exports triangles wound opposite to other scanners. `STLReader.cpp` has per-face cross-product check against stored STL normal – do not remove this.

### ICP Alignment of FussenS6000 and iTeroLumina

These scanners use coordinate system 28 mm offset from others. PCA coarse alignment is required before fine ICP (5 mm search radius).


Daraus hat Gemini diesen Prompt abgeleitet:

Target: Implement an "Anatomical Anchor" Standardization Layer to normalize mismatched scanner coordinate systems before registration (Phase 1).

### Context & Goals
We are processing intraoral STL scans from multiple vendors (iTero, Trios, Primescan, etc.) and references (CAD, Lab scanners). They suffer from permuted axes, inverted directions, and arbitrary rotational offsets (e.g., a 15-degree tilt in CAD). We need a configuration-driven pipeline layer that normalizes all incoming meshes to a strict project coordinate standard BEFORE running coarse PCA or fine ICP.

Project Coordinate Standard (Target):
- X-Axis (Transversal): Left (-) to Right (+)
- Y-Axis (Sagittal / Dorsal-Ventral): Dorsal (-) to Ventral (+)
- Z-Axis (Vertical / Apical-Occlusal): Apical (-) to Occlusal (+) [Increasing values move toward cusp tips for the upper jaw]


### Step 1: Extend Configuration Parsing
Modify our YAML configuration parser to support scanner-specific coordinate profiles and matching conditions:
1. Parse a new `scanners` map or subsection in our configuration files.
2. Support `match_conditions` (e.g., matching specific scanner names or SKD level substrings like SKD30, SKD28).
3. Extract properties per profile:
- `axis_mapping`: dictionary/map routing input axes to target axes (e.g., {x: x, y: z, z: y})
- `axis_signs`: dictionary/map for directional inversion multipliers (e.g., {x: 1, y: 1, z: -1})
- `winding_flip`: boolean flag (to be passed to our STLReader for handling opposite-wound triangles, like Primescan)
- `alignment_strategy`: enum/string ('profile', 'interactive_matrix')
- `matrix_file`: path to a stored 4x4 transformation matrix JSON file for arbitrary skews (like CAD)

### Step 2: Implement the Pre-Alignment Math Layer
Create a normalization module/class that takes a raw mesh and a matching scanner profile:
1. Construct a rigid 4x4 transformation matrix based on the profile's `axis_mapping` and `axis_signs`.
2. If `alignment_strategy` is 'interactive_matrix', load the explicit 4x4 matrix from the specified `matrix_file`.
3. If an explicit matrix or profile is missing, or if a global flag (`interactive_fallback`) is true, route the mesh to the interactive UI hook (see Step 3).
4. Apply the computed/loaded matrix to normalize the mesh coordinates and center its translation (neutralizing systematic offsets like the 28mm offset observed in FussenS6000/iTeroLumina) before passing it down the pipeline.

### Step 3: Implement the Interactive Alignment UI Hook
Integrate hooks for an Interactive Standardization Tool into Phase 1 of the pipeline:
1. Do not implement the rendering engine from scratch; leave the UI window logic open for extension/reuse of existing UI code.
2. Provide a clean interface/abstract class `IInteractiveAlignmentAssistant` with a method like `RequestStandardization(Mesh inputMesh, ScannerProfile profile)`.
3. The hook must accept:
- Orthogonal viewport orientation commands (e.g., signals/slots or callbacks for "Flip Z", "Swap Y/Z", "Rotate 90deg around X").
- A landmark selection mode that records 3 anatomical points: Midline incisal edge (11/21), Distobuccal cusp of 16, and Distobuccal cusp of 26.
4. Implement the math to compute a 4x4 matrix from these 3 landmarks that levels the occlusal plane (aligning it to the standard XY plane) and aligns the midline to the Y-axis.
5. Provide a serialization method to save this generated 4x4 transformation matrix back to the target profile's JSON/YAML file path.

### Codebase Integration
- Keep all modifications modular. Do not alter the core logic inside `STLReader.cpp` that checks face cross-products against stored normals, but ensure the `winding_flip` boolean flag is cleanly forwarded to it.
- Ensure that the final standardized transforms are saved as the initial 4x4 matrix checkpoint for Phase 1 QC approval.
