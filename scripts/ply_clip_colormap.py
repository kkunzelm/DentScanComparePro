#!/usr/bin/env python3
"""
Re-colour a difference PLY so that vertices outside ±N×SD get sentinel colours:
    distance < -N*SD  →  black        (0,   0,   0)
    distance > +N*SD  →  bright green (0, 255,   0)
    |distance| <= N*SD →  original colour kept

Writes 4 files to /tmp, one per SD multiplier (1×, 2×, 3×, 4×):
    /tmp/<stem>_clip1sd.<ext>
    /tmp/<stem>_clip2sd.<ext>
    /tmp/<stem>_clip3sd.<ext>
    /tmp/<stem>_clip4sd.<ext>

The input file is NOT modified.

Usage:
    python3 ply_clip_colormap.py <difference.ply>
"""

import sys
import numpy as np
from pathlib import Path


# ── PLY header parser ──────────────────────────────────────────────────────────

def parse_header(f):
    header_lines = []
    while True:
        line = f.readline()
        header_lines.append(line)
        if line.strip() == b'end_header':
            break

    header_bytes = b''.join(header_lines)
    elements = []
    current = None
    for line in header_lines:
        s = line.decode('ascii', errors='replace').strip()
        if s.startswith('element'):
            parts = s.split()
            current = {'name': parts[1], 'count': int(parts[2]), 'properties': []}
            elements.append(current)
        elif s.startswith('property') and current is not None:
            parts = s.split()
            current['properties'].append((parts[1], parts[2]))

    return header_bytes, elements


NP_MAP = {
    'float':   np.float32, 'float32': np.float32,
    'double':  np.float64, 'float64': np.float64,
    'int':     np.int32,   'int32':   np.int32,
    'uint':    np.uint32,  'uint32':  np.uint32,
    'uchar':   np.uint8,   'uint8':   np.uint8,
    'short':   np.int16,   'ushort':  np.uint16,
}


def element_dtype(props):
    return np.dtype([(name, NP_MAP.get(ptype, np.float32)) for ptype, name in props])


# ── colour application ─────────────────────────────────────────────────────────

def apply_clip(verts, dist, threshold):
    v = verts.copy()
    mask_neg = dist < -threshold
    mask_pos = dist >  threshold

    v['red'][mask_neg]   = 0;   v['green'][mask_neg] = 0;   v['blue'][mask_neg] = 0
    v['red'][mask_pos]   = 0;   v['green'][mask_pos] = 255; v['blue'][mask_pos] = 0

    return v, mask_neg, mask_pos


# ── main ───────────────────────────────────────────────────────────────────────

def run(src_path: Path):
    with open(src_path, 'rb') as f:
        header_bytes, elements = parse_header(f)

        vert_el = next(e for e in elements if e['name'] == 'vertex')
        n_verts = vert_el['count']
        props   = vert_el['properties']
        dt      = element_dtype(props)
        verts   = np.frombuffer(f.read(n_verts * dt.itemsize), dtype=dt).copy()
        rest_bytes = f.read()

    names = [n for _, n in props]

    skip = {'x', 'y', 'z', 'nx', 'ny', 'nz', 'red', 'green', 'blue', 'alpha'}
    scalar_name = next((n for n in names if n not in skip), None)
    if scalar_name is None:
        sys.exit("ERROR: no distance scalar found in vertex properties")

    for col in ('red', 'green', 'blue'):
        if col not in names:
            sys.exit(f"ERROR: property '{col}' not found – PLY has no RGB data")

    dist = verts[scalar_name].astype(np.float64)
    sd   = dist.std()
    mean = dist.mean()

    print(f"Input  : {src_path}")
    print(f"Scalar : '{scalar_name}'  n={len(dist):,}")
    print(f"Mean   : {mean:+.4f} mm")
    print(f"SD     : {sd:.4f} mm\n")

    stem = src_path.stem
    ext  = src_path.suffix

    for n in (1, 2, 3, 4):
        threshold = n * sd
        v, mask_neg, mask_pos = apply_clip(verts, dist, threshold)

        dst = Path('/tmp') / f"{stem}_clip{n}sd{ext}"
        with open(dst, 'wb') as f:
            f.write(header_bytes)
            f.write(v.tobytes())
            f.write(rest_bytes)

        pct_neg  = 100 * mask_neg.mean()
        pct_pos  = 100 * mask_pos.mean()
        pct_keep = 100 * (~mask_neg & ~mask_pos).mean()
        print(f"  {n}×SD  threshold = ±{threshold:.4f} mm")
        print(f"    black (< -{threshold:.4f})  : {mask_neg.sum():>7,}  ({pct_neg:.1f}%)")
        print(f"    green (> +{threshold:.4f})  : {mask_pos.sum():>7,}  ({pct_pos:.1f}%)")
        print(f"    kept                    : {(~mask_neg & ~mask_pos).sum():>7,}  ({pct_keep:.1f}%)")
        print(f"    → {dst}")
        print()


if __name__ == '__main__':
    if len(sys.argv) != 2:
        print(__doc__)
        sys.exit(1)

    run(Path(sys.argv[1]))
