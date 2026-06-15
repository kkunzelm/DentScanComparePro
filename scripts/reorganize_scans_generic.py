#!/usr/bin/env python3
"""
Reorganize dental scanner data into a flat directory structure.

Source structure:
  Trios 3 Daten/PMM{pat}/PMM{pat}D{rep} UpperJawScan.ply
  Medit 700 Daten/PMM{pat}/PMM{pat}D{rep}-{date}/*maxillary*.ply
  Primescan Daten/PMM{pat}D{rep}/UpperJaw/Data.obj
  Carestream 3700 Daten/PMM{pat}/{timestamp} - PMM{pat}D{rep} .../Maxillary Anatomy.ply

New filename format: {Scanner}_{PatientNum}_{RepetitionNum}.{ext}
Example: Trios3_002_D1.ply, Primescan_014_D6.obj

Special handling:
  - Carestream: patient/rep ID read from session subdir name (PMM016/PMM017 parent dirs are swapped)
  - Carestream: when multiple sessions exist for same patient+rep, the latest timestamp wins
  - Carestream PMM001: only Mandibular D2 exists (no Maxillary) -> skipped with warning

Usage:
  python reorganize_scans_generic.py             # dry run, shows planned copies
  python reorganize_scans_generic.py --execute   # actually copy files and write study.json
  python reorganize_scans_generic.py --output /path/to/flat --execute
"""

import re
import json
import shutil
import argparse
from pathlib import Path
from collections import defaultdict

BASE_DIR = Path("/path/to/scan/data")
DEFAULT_OUTPUT = BASE_DIR / "flat"

SCANNER_DIRS = {
    "Trios3":         BASE_DIR / "Trios 3 Daten",
    "Medit700":       BASE_DIR / "Medit 700 Daten",
    "Primescan":      BASE_DIR / "Primescan Daten",
    "Carestream3700": BASE_DIR / "Carestream 3700 Daten",
}


def collect_trios3(warnings):
    records = []
    scanner = "Trios3"
    for patient_dir in sorted(SCANNER_DIRS[scanner].iterdir()):
        if not patient_dir.is_dir():
            continue
        m = re.match(r'PMM(\d{3})$', patient_dir.name)
        if not m:
            continue
        patient_num = m.group(1)
        for f in sorted(patient_dir.glob("*.ply")):
            fm = re.search(r'D(\d+)', f.name)
            if not fm:
                warnings.append(f"[Trios3] Cannot extract repetition from: {f.name}")
                continue
            rep = fm.group(1)
            records.append({
                "scanner": scanner, "patient": patient_num, "repetition": f"D{rep}",
                "source": f, "target": f"{scanner}_{patient_num}_D{rep}.ply",
            })
    return records


def collect_medit700(warnings):
    records = []
    scanner = "Medit700"
    for patient_dir in sorted(SCANNER_DIRS[scanner].iterdir()):
        if not patient_dir.is_dir():
            continue
        m = re.match(r'PMM(\d{3})$', patient_dir.name)
        if not m:
            continue
        patient_num = m.group(1)
        for session_dir in sorted(patient_dir.iterdir()):
            if not session_dir.is_dir():
                continue
            sm = re.match(r'PMM\d{3}D(\d+)', session_dir.name)
            if not sm:
                warnings.append(f"[Medit700] Unrecognized session dir: {session_dir.name}")
                continue
            rep = sm.group(1)
            ply_files = sorted(session_dir.glob("*[Mm]axillary*.ply"))
            if not ply_files:
                ply_files = sorted(session_dir.glob("*.ply"))
            if not ply_files:
                warnings.append(f"[Medit700] No PLY in {session_dir}")
                continue
            if len(ply_files) > 1:
                warnings.append(
                    f"[Medit700] Multiple PLY in {session_dir.name}: "
                    f"taking {ply_files[0].name}"
                )
            records.append({
                "scanner": scanner, "patient": patient_num, "repetition": f"D{rep}",
                "source": ply_files[0], "target": f"{scanner}_{patient_num}_D{rep}.ply",
            })
    return records


def collect_primescan(warnings):
    records = []
    scanner = "Primescan"
    for session_dir in sorted(SCANNER_DIRS[scanner].iterdir()):
        if not session_dir.is_dir():
            continue
        m = re.match(r'PMM(\d{3})D(\d+)$', session_dir.name)
        if not m:
            warnings.append(f"[Primescan] Unrecognized dir: {session_dir.name}")
            continue
        patient_num, rep = m.group(1), m.group(2)

        # Prefer UpperJaw/Data.obj (newer export format); fall back to UpperJaw.stl
        obj_file = session_dir / "UpperJaw" / "Data.obj"
        stl_file = session_dir / "UpperJaw.stl"

        if obj_file.exists():
            source, ext = obj_file, "obj"
        elif stl_file.exists():
            source, ext = stl_file, "stl"
            warnings.append(
                f"[Primescan] PMM{patient_num}D{rep}: No OBJ found, using STL format"
            )
        else:
            lower = list(session_dir.rglob("*[Ll]ower*"))
            if lower:
                warnings.append(
                    f"[Primescan] PMM{patient_num}D{rep}: Only LowerJaw data — SKIPPED"
                )
            else:
                warnings.append(
                    f"[Primescan] PMM{patient_num}D{rep}: No UpperJaw file found — SKIPPED"
                )
            continue

        records.append({
            "scanner": scanner, "patient": patient_num, "repetition": f"D{rep}",
            "source": source, "target": f"{scanner}_{patient_num}_D{rep}.{ext}",
        })
    return records


def collect_carestream(warnings):
    records = []
    scanner = "Carestream3700"
    session_map = defaultdict(list)  # (patient_num, rep) -> [(timestamp_str, session_dir)]

    for parent_dir in sorted(SCANNER_DIRS[scanner].iterdir()):
        if not parent_dir.is_dir() or not re.match(r'PMM\d{3}$', parent_dir.name):
            continue
        for session_dir in sorted(parent_dir.iterdir()):
            if not session_dir.is_dir():
                continue
            # "20230724 - 133711 - PMM002D2 PMM002D2"
            m = re.match(r'(\d{8}) - (\d{6}) - PMM(\d{3})D(\d+)', session_dir.name)
            if not m:
                warnings.append(f"[Carestream] Unrecognized session: {session_dir.name}")
                continue
            timestamp = m.group(1) + m.group(2)
            patient_num, rep = m.group(3), m.group(4)
            session_map[(patient_num, rep)].append((timestamp, session_dir))

    for (patient_num, rep), sessions in sorted(session_map.items()):
        sessions_sorted = sorted(sessions, key=lambda x: x[0])
        if len(sessions) > 1:
            discarded = [s.name for _, s in sessions_sorted[:-1]]
            warnings.append(
                f"[Carestream] PMM{patient_num}D{rep}: {len(sessions)} sessions found, "
                f"discarding earlier: {discarded}"
            )
        _, best_dir = sessions_sorted[-1]

        # Prefer Maxillary/Upper; skip if only Mandibular exists
        ply_files = sorted(best_dir.glob("*[Mm]axillary*.ply"))
        if not ply_files:
            ply_files = sorted(best_dir.glob("*[Uu]pper*.ply"))
        if not ply_files:
            all_ply = sorted(best_dir.glob("*.ply"))
            found = [f.name for f in all_ply]
            warnings.append(
                f"[Carestream] PMM{patient_num}D{rep}: No Maxillary PLY "
                f"(found: {found or 'none'}) — SKIPPED"
            )
            continue

        records.append({
            "scanner": scanner, "patient": patient_num, "repetition": f"D{rep}",
            "source": ply_files[0], "target": f"{scanner}_{patient_num}_D{rep}.ply",
        })
    return records


def build_study_json(records, output_dir):
    patients = sorted(set(r["patient"] for r in records))
    scanners = sorted(set(r["scanner"] for r in records))
    repetitions = sorted(set(r["repetition"] for r in records))
    observations = [
        {
            "scanner": r["scanner"],
            "patient": r["patient"],
            "repetition": r["repetition"],
            "file": r["target"],
        }
        for r in sorted(records, key=lambda x: (x["scanner"], x["patient"], x["repetition"]))
    ]
    return {
        "study_name": "P2026-Nold",
        "description": "Dental intraoral scanner accuracy study (trueness & precision per ISO 12836)",
        "scan_directory": str(output_dir),
        "filename_pattern": "{scanner}_{patient}_{repetition}.{ext}",
        "factors": {
            "scanner": scanners,
            "patient": patients,
            "repetition": repetitions,
        },
        "file_formats": {
            "Trios3": "ply",
            "Medit700": "ply",
            "Carestream3700": "ply",
            "Primescan": "obj",
        },
        "observations": observations,
    }


def main():
    parser = argparse.ArgumentParser(description=__doc__,
                                     formatter_class=argparse.RawDescriptionHelpFormatter)
    parser.add_argument("--execute", action="store_true",
                        help="Actually copy files (default: dry run only)")
    parser.add_argument("--output", default=str(DEFAULT_OUTPUT),
                        help=f"Output directory (default: {DEFAULT_OUTPUT})")
    args = parser.parse_args()

    dry_run = not args.execute
    output_dir = Path(args.output)

    if dry_run:
        print("=== DRY RUN — no files will be modified ===\n")

    warnings = []
    records = []
    records += collect_trios3(warnings)
    records += collect_medit700(warnings)
    records += collect_primescan(warnings)
    records += collect_carestream(warnings)

    if warnings:
        print("=== WARNINGS ===")
        for w in warnings:
            print(f"  ! {w}")
        print()

    print(f"=== PLANNED COPIES ({len(records)} files) ===")
    for r in sorted(records, key=lambda x: (x["scanner"], x["patient"], x["repetition"])):
        rel = "/".join(str(r["source"]).split("/")[-3:])
        print(f"  {r['scanner']:16} P{r['patient']} {r['repetition']}  .../{rel}")
        print(f"    -> {output_dir / r['target']}")

    study = build_study_json(records, output_dir)
    print(f"\n=== STUDY SUMMARY ===")
    print(f"  Scanners:    {', '.join(study['factors']['scanner'])}")
    print(f"  Patients:    {len(study['factors']['patient'])} "
          f"({study['factors']['patient'][0]}–{study['factors']['patient'][-1]})")
    print(f"  Repetitions: {', '.join(study['factors']['repetition'])}")
    print(f"  Total files: {len(records)}")

    if not dry_run:
        output_dir.mkdir(parents=True, exist_ok=True)
        copied = 0
        skipped = 0
        for r in sorted(records, key=lambda x: (x["scanner"], x["patient"], x["repetition"])):
            dst = output_dir / r["target"]
            if dst.exists():
                print(f"  SKIP (exists): {r['target']}")
                skipped += 1
            else:
                print(f"  COPY: {r['target']}")
                shutil.copy2(r["source"], dst)
                copied += 1

        json_path = output_dir / "study.json"
        with open(json_path, "w", encoding="utf-8") as f:
            json.dump(study, f, indent=2, ensure_ascii=False)

        print(f"\nDone. Copied {copied} files, skipped {skipped} existing.")
        print(f"Study JSON: {json_path}")
    else:
        print(f"\nRun with --execute to copy {len(records)} files to:")
        print(f"  {output_dir}")
        print("A study.json will also be generated there.")


if __name__ == "__main__":
    main()
