#!/usr/bin/env python3
"""
Generate a DentScanComparePro study config JSON for a patient study.

Reads *_aligned.stl files from flat_aligned/ and produces a JSON config where
each GROUP is a patient. All scanners per patient go into one GPA computation,
matching the mixed-effects model (scanner = fixed, patient = random).

Optionally reorganizes files into a scanner-first directory tree:
  flat_aligned/{Scanner}/{Patient}/{Rep}_aligned.stl

Usage:
  python gen_study_config_generic.py                    # generate JSON only (dry run)
  python gen_study_config_generic.py --reorganize       # dry run of file moves
  python gen_study_config_generic.py --reorganize --execute  # move files + write JSON
  python gen_study_config_generic.py --input /other/dir --output /other/config.json
"""

import re
import json
import shutil
import argparse
from pathlib import Path
from collections import defaultdict

FLAT_ALIGNED_DIR = Path("/path/to/scan/data/flat_aligned")
DEFAULT_OUTPUT_JSON = Path("/path/to/scan/data/flat_aligned/study.json")

# Scanners that exist in the dataset, in order of appearance
SCANNER_ORDER = ["Carestream3700", "Medit700", "Primescan", "Trios3"]

# Filename pattern: {Scanner}_{Patient}_{Rep}_aligned.stl
FILENAME_RE = re.compile(
    r'^([A-Za-z0-9]+)_(\d{3})_(D\d+)_aligned\.stl$'
)


def scan_aligned_dir(directory: Path) -> list[dict]:
    """Return list of {scanner, patient, rep, path} for every *_aligned.stl found."""
    records = []
    for stl in sorted(directory.glob("*_aligned.stl")):
        if not stl.is_file():
            continue
        m = FILENAME_RE.match(stl.name)
        if not m:
            print(f"  ! Skipping unrecognised filename: {stl.name}")
            continue
        scanner, patient, rep = m.group(1), m.group(2), m.group(3)
        records.append({
            "scanner": scanner,
            "patient": patient,
            "rep": rep,
            "path": stl,
        })
    return records


def reorganize_files(records: list[dict], base_dir: Path, execute: bool) -> list[dict]:
    """
    Move files from flat_aligned/ into {base_dir}/{Scanner}/{Patient}/{Rep}_aligned.stl.
    Returns updated records with new paths.
    """
    updated = []
    for r in records:
        dest_dir = base_dir / r["scanner"] / r["patient"]
        dest_file = dest_dir / f"{r['rep']}_aligned.stl"

        if execute:
            dest_dir.mkdir(parents=True, exist_ok=True)
            if not dest_file.exists():
                shutil.move(str(r["path"]), str(dest_file))
                print(f"  MOVE: {r['path'].name} -> {dest_file.relative_to(base_dir)}")
            else:
                print(f"  SKIP (exists): {dest_file.relative_to(base_dir)}")
        else:
            print(f"  {r['path'].name}")
            print(f"    -> {dest_file.relative_to(base_dir)}")

        updated.append({**r, "path": dest_file})
    return updated


def build_config(records: list[dict], scan_dir: Path, use_scanner_subdirs: bool) -> dict:
    """Build a DentScanComparePro study config dict from the observation records."""

    # Collect patients and scanners present
    patients = sorted(set(r["patient"] for r in records))
    scanners_present = sorted(set(r["scanner"] for r in records),
                               key=lambda s: SCANNER_ORDER.index(s) if s in SCANNER_ORDER else 99)

    # Build scanner definitions — patterns match the filename prefix
    scanner_defs = []
    for s in scanners_present:
        scanner_defs.append({"id": s, "patterns": [f"{s}*"]})

    # Build one group per patient
    default_roi = {
        "bbox": {
            "active": False,
            "min": [0.0, 0.0, 0.0],
            "max": [0.0, 0.0, 0.0],
        },
        "z_plane": {
            "active": False,
            "above_mm": 2.0,
            "below_mm": 12.0,
        },
        "brush_zones": [],
        "outlier_sigma": 3.0,
    }

    groups = []
    for patient in patients:
        pat_records = [r for r in records if r["patient"] == patient]
        n = len(pat_records)

        if use_scanner_subdirs:
            # Files are in {Scanner}/{Patient}/ subdirs; use per-patient wildcard
            file_patterns = [f"**/{patient}/*_aligned.stl"]
        else:
            # Flat directory; match by patient number in filename
            file_patterns = [f"*_{patient}_*_aligned.stl"]

        groups.append({
            "id": patient,
            "skd_mm": 0,
            "file_patterns": file_patterns,
            "roi": default_roi,
            "_comment_n_files": n,  # informational, ignored by C++
        })

    return {
        "study": {
            "name": "StudyName",
            "version": 1,
            "reference_strategy": "gpa_mean",
            "scans_pre_aligned": False,
            "scans_normalized": True,
            "alignment": {
                "max_icp_iterations": 100,
                "convergence_threshold_mm": 0.01,
                "use_pca_coarse": True,
                "use_4orientation_test": True,
            },
        },
        "scanners": scanner_defs,
        "groups": groups,
        "output": {
            "base_dir": "./results",
            "metrics_csv": "trueness_metrics.csv",
            "summary_csv": "summary_stats.csv",
            "precision_csv": "precision_metrics.csv",
        },
    }


def print_summary(records: list[dict], patients: list[str], scanners: list[str]) -> None:
    print(f"\n=== STUDY SUMMARY ===")
    print(f"  Scanners:  {', '.join(scanners)}")
    print(f"  Patients:  {len(patients)} ({patients[0]}-{patients[-1]})")

    by_patient = defaultdict(lambda: defaultdict(list))
    for r in records:
        by_patient[r["patient"]][r["scanner"]].append(r["rep"])

    print(f"\n  Observations per patient (scanner × reps):")
    header = f"  {'Patient':>8}  " + "  ".join(f"{s:>14}" for s in scanners)
    print(header)
    for p in patients:
        row = f"  {p:>8}  "
        for s in scanners:
            reps = sorted(by_patient[p].get(s, []))
            row += f"  {','.join(reps) if reps else '—':>14}"
        print(row)

    print(f"\n  Total files: {len(records)}")
    missing = []
    for p in patients:
        for s in scanners:
            if s not in by_patient[p]:
                missing.append(f"{s}/{p}")
    if missing:
        print(f"  Missing scanner/patient combinations: {', '.join(missing)}")


def main():
    parser = argparse.ArgumentParser(
        description=__doc__,
        formatter_class=argparse.RawDescriptionHelpFormatter,
    )
    parser.add_argument(
        "--input",
        default=str(FLAT_ALIGNED_DIR),
        help=f"Directory with *_aligned.stl files (default: {FLAT_ALIGNED_DIR})",
    )
    parser.add_argument(
        "--output",
        default=str(DEFAULT_OUTPUT_JSON),
        help=f"Output JSON path (default: {DEFAULT_OUTPUT_JSON})",
    )
    parser.add_argument(
        "--reorganize",
        action="store_true",
        help="Move files into {{Scanner}}/{{Patient}}/ subdirectory tree",
    )
    parser.add_argument(
        "--execute",
        action="store_true",
        help="Actually move files and write JSON (default: dry run)",
    )
    args = parser.parse_args()

    input_dir = Path(args.input)
    output_json = Path(args.output)
    dry_run = not args.execute

    if dry_run:
        print("=== DRY RUN — no files will be moved or written ===\n")

    if not input_dir.exists():
        print(f"ERROR: Input directory does not exist: {input_dir}")
        return 1

    # Scan flat_aligned/ for *_aligned.stl files
    records = scan_aligned_dir(input_dir)
    if not records:
        print(f"ERROR: No *_aligned.stl files found in {input_dir}")
        return 1

    patients = sorted(set(r["patient"] for r in records))
    scanners = sorted(set(r["scanner"] for r in records),
                      key=lambda s: SCANNER_ORDER.index(s) if s in SCANNER_ORDER else 99)

    print_summary(records, patients, scanners)

    use_scanner_subdirs = args.reorganize
    scan_dir = input_dir

    if args.reorganize:
        print(f"\n=== {'PLANNED' if dry_run else 'EXECUTING'} FILE REORGANIZATION ===")
        print(f"Target structure: {input_dir}/{{Scanner}}/{{Patient}}/{{Rep}}_aligned.stl\n")
        records = reorganize_files(records, input_dir, execute=not dry_run)

    config = build_config(records, scan_dir, use_scanner_subdirs)

    if not dry_run:
        with open(output_json, "w", encoding="utf-8") as f:
            json.dump(config, f, indent=2, ensure_ascii=False)
        print(f"\nConfig written to: {output_json}")
    else:
        print(f"\n=== GENERATED JSON PREVIEW ===")
        preview = dict(config)
        preview["groups"] = config["groups"][:2]  # show first 2 groups only
        print(json.dumps(preview, indent=2))
        print(f"  ... ({len(config['groups'])} groups total)")
        print(f"\nRun with --execute to write {output_json}")

    return 0


if __name__ == "__main__":
    exit(main())
