#!/usr/bin/env python3
"""
Plot a histogram of per-vertex distance values from a difference PLY file.
The PLY must have a per-vertex scalar property (e.g. 'distance') as written
by QCExporter::writeBinaryPLY().

Usage:
    python3 ply_distance_histogram.py <file.ply> [file2.ply ...]
    python3 ply_distance_histogram.py /path/to/qc/difference_meshes/*.ply
"""

import sys
import struct
import numpy as np
import matplotlib.pyplot as plt
from pathlib import Path


def read_ply_scalar(filepath):
    """Read per-vertex scalar from a binary PLY file. Returns (distances, scalar_name)."""
    with open(filepath, 'rb') as f:
        # Parse ASCII header
        properties = []
        n_vertices = 0
        scalar_idx = None
        in_vertex = False

        while True:
            line = f.readline().decode('ascii', errors='replace').strip()
            if line == 'end_header':
                break
            if line.startswith('element vertex'):
                n_vertices = int(line.split()[-1])
                in_vertex = True
            elif line.startswith('element'):
                in_vertex = False
            elif line.startswith('property') and in_vertex:
                parts = line.split()
                properties.append((parts[1], parts[2]))  # (type, name)

        # Determine byte layout
        np_type_map = {
            'float': np.float32, 'float32': np.float32,
            'double': np.float64, 'float64': np.float64,
            'int': np.int32,   'int32': np.int32,
            'uint': np.uint32, 'uint32': np.uint32,
            'uchar': np.uint8, 'uint8': np.uint8,
            'short': np.int16, 'ushort': np.uint16,
        }

        dt_fields = []
        for ptype, pname in properties:
            np_type = np_type_map.get(ptype, np.float32)
            dt_fields.append((pname, np_type))

        dt = np.dtype(dt_fields)
        raw = f.read(n_vertices * dt.itemsize)

    data = np.frombuffer(raw, dtype=dt)

    # Find scalar property (anything that is not x, y, z, normals, or color)
    skip = {'x', 'y', 'z', 'nx', 'ny', 'nz', 'red', 'green', 'blue', 'alpha'}
    scalar_name = next((name for name, _ in dt_fields if name not in skip), None)

    if scalar_name is None:
        raise ValueError(f"No non-spatial scalar property found in {filepath}")

    return data[scalar_name].astype(np.float64), scalar_name


def plot_histogram(filepaths, clip_mm=None, bins=200):
    fig, axes = plt.subplots(len(filepaths), 1,
                             figsize=(10, 3 * len(filepaths)),
                             squeeze=False)

    for ax, fp in zip(axes[:, 0], filepaths):
        distances, scalar_name = read_ply_scalar(fp)

        print(f"\n{Path(fp).name}")
        print(f"  Vertices  : {len(distances):,}")
        print(f"  Min       : {distances.min():.3f} mm")
        print(f"  Max       : {distances.max():.3f} mm")
        print(f"  Mean      : {distances.mean():.3f} mm")
        print(f"  Std       : {distances.std():.3f} mm")
        print(f"  |d|<0.2mm : {100*np.mean(np.abs(distances)<0.2):.1f}%")
        print(f"  |d|<0.5mm : {100*np.mean(np.abs(distances)<0.5):.1f}%")
        print(f"  |d|>1.0mm : {100*np.mean(np.abs(distances)>1.0):.1f}%")
        print(f"  |d|>2.0mm : {100*np.mean(np.abs(distances)>2.0):.1f}%")

        d = distances
        if clip_mm is not None:
            d = d[np.abs(d) <= clip_mm]
            clipped_pct = 100 * (1 - len(d) / len(distances))
            label_suffix = f"  (clipped >{clip_mm} mm: {clipped_pct:.1f}% of vertices)"
        else:
            label_suffix = ""

        ax.hist(d, bins=bins, color='steelblue', edgecolor='none', alpha=0.8)
        ax.axvline(0, color='black', linewidth=1, linestyle='--')
        ax.set_xlabel(f'{scalar_name} (mm)')
        ax.set_ylabel('Vertex count')
        ax.set_title(Path(fp).name + label_suffix)
        ax.grid(True, alpha=0.3)

    plt.tight_layout()
    out = Path(filepaths[0]).parent / 'distance_histogram.png'
    plt.savefig(out, dpi=150)
    print(f"\nSaved: {out}")
    plt.show()


if __name__ == '__main__':
    if len(sys.argv) < 2:
        print(__doc__)
        sys.exit(1)

    files = sys.argv[1:]

    # Optional: clip display range with --clip <mm>
    clip = None
    if '--clip' in files:
        idx = files.index('--clip')
        clip = float(files[idx + 1])
        files = [f for i, f in enumerate(files) if i != idx and i != idx + 1]

    plot_histogram(files, clip_mm=clip)
