#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-only
"""Prepare private Fallout lock/holder/tool assets; never redistribute the output.

Uses the installed Fallout BSAs through the same PyFFI/numpy/Pillow environment
as prepare_hands.py. --lock-texture optionally selects a user-owned replacement
DDS such as NV Lockpick; otherwise the installed game's texture is used.
"""

import argparse
import hashlib
import io
import json
import struct
from pathlib import Path
from prepare_hands import NifFormat, bsa_file, matrix, np, Image


def mesh(data, name, world=False):
    g = next(
        b
        for b in data.blocks
        if isinstance(b, NifFormat.NiTriBasedGeom) and b.name.decode() == name
    )
    p = np.array([v.as_list() for v in g.data.vertices], dtype=float)
    if world:
        p = (
            np.column_stack([p, np.ones(len(p))])
            @ matrix(g.get_transform(data.roots[0]))
        )[:, :3]
    uv = np.array([[v.u, v.v] for v in g.data.uv_sets[0]])
    return p, uv, np.array(list(g.data.get_triangles()), dtype=np.uint32)


def normals(p, triangles):
    n = np.zeros_like(p)
    for tri in triangles:
        face = np.cross(p[tri[1]] - p[tri[0]], p[tri[2]] - p[tri[0]])
        for i in tri:
            n[i] += face
    n /= np.maximum(np.linalg.norm(n, axis=1)[:, None], 1e-12)
    return n


def main():
    ap = argparse.ArgumentParser(description=__doc__)
    ap.add_argument("--data", type=Path, required=True)
    ap.add_argument("--output", type=Path, required=True)
    ap.add_argument("--lock-texture", type=Path)
    a = ap.parse_args()
    provenance = {}

    def read(name):
        loose = a.data
        # Fallout paths are case-insensitive, including on a Linux host.
        for component in name.replace("\\", "/").split("/"):
            exact = loose / component
            if not exact.exists() and loose.is_dir():
                exact = next(
                    (
                        p
                        for p in sorted(loose.iterdir())
                        if p.name.casefold() == component.casefold()
                    ),
                    exact,
                )
            loose = exact
        raw = loose.read_bytes() if loose.is_file() else None
        source = str(loose)
        if raw is None:
            for bsa in sorted(
                p for p in a.data.iterdir() if p.suffix.lower() == ".bsa"
            ):
                raw = bsa_file(bsa, name)
                if raw is not None:
                    source = str(bsa) + ":" + name
                    break
        if raw is None:
            raise FileNotFoundError(name)
        provenance[name] = dict(source=source, sha256=hashlib.sha256(raw).hexdigest())
        return raw

    def nif(name):
        d = NifFormat.Data()
        d.read(io.BytesIO(read(name)))
        return d

    lock = nif(r"meshes\terminals\lockinterface01.nif")
    pin = nif(r"meshes\terminals\bobbypin01.nif")
    box = nif(r"meshes\clutter\bobbypin\bobbypinbox.nif")
    can = nif(r"meshes\clutter\junk\tincan01.nif")
    parts = []

    def add(name, p, uv, tri, material=0):
        verts = np.column_stack([p, normals(p, tri), uv]).astype("<f4")
        assert np.isfinite(verts).all() and len(verts) < 10000 and len(tri) < 10000
        assert np.max(np.abs(p)) < 1 and tri.max() < len(verts)
        parts.append((name, material, verts, tri.astype("<u4")))

    # Frame and rotating plug use the original UVs. Their dimensions fit the
    # existing housing envelope, so the interaction workspace stays unchanged.
    lp, luv, lt = mesh(lock, "lock:0")
    wp, _, _ = mesh(lock, "lock:0", True)
    centre = np.array([wp[:, 0].mean(), wp[:, 1].min(), wp[:, 2].mean()])
    # Geometric axis from the root transform, not the vertex-weighted centroid.
    g = next(
        b
        for b in lock.blocks
        if isinstance(b, NifFormat.NiTriBasedGeom) and b.name == b"lock:0"
    )
    centre[[0, 2]] = matrix(g.get_transform(lock.roots[0]))[3, [0, 2]]
    p, uv, tri = mesh(lock, "frame:0", True)
    xy = np.column_stack([p[:, 0] - centre[0], p[:, 2] - centre[2]])
    # Preserve the original frame/plug ratio instead of widening the frame
    # around a smaller plug. The front and tool contact plane stay unchanged.
    xy *= 0.075 / 7.25
    radius = np.linalg.norm(xy, axis=1)
    front = np.interp(radius, [0.05084, 0.075], [0.024, 0.014])
    add("frame", np.column_stack([xy, front - (p[:, 1] - centre[1]) * 0.010]), uv, tri)
    p = np.column_stack(
        [
            lp[:, 0] * 0.075 / 7.25,
            lp[:, 2] * 0.075 / 7.25,
            0.024 - (lp[:, 1] - lp[:, 1].min()) * 0.036 / np.ptp(lp[:, 1]),
        ]
    )
    add("cylinder", p, luv, lt)
    p, uv, tri = mesh(lock, "screwdriver:0")
    z = np.interp(
        p[:, 2],
        [7.948953, 10.590867, 13.630203, 25.500305, 49.465488],
        [0, 0.028, 0.042, 0.112, 0.182],
    )
    scale = np.where(p[:, 2] < 12, 0.0024, np.where(p[:, 2] < 25.5, 0.0098, 0.0052))
    p = np.column_stack(
        [(p[:, 0] + 17.230434) * scale, (p[:, 1] - 2.847224) * scale, z]
    )
    add("screwdriver", p, uv, tri)
    p, uv, tri = mesh(pin, "bobbypin:0")
    lo, hi = p.min(0), p.max(0)
    p = np.column_stack(
        [
            (p[:, 2] - (lo[2] + hi[2]) / 2) * 0.003,
            (p[:, 0] - (lo[0] + hi[0]) / 2) * 0.005,
            (hi[1] - p[:, 1]) * 0.075 / (hi[1] - lo[1]),
        ]
    )
    add("pin_shaft", p, uv, tri)
    p, uv, tri = mesh(pin, "Pin2:0")
    lo, hi = p.min(0), p.max(0)
    p = np.column_stack(
        [
            (p[:, 1] - (lo[1] + hi[1]) / 2) * (0.081 / (hi[0] - lo[0])),
            0.004 + (p[:, 0] - lo[0]) * 0.081 / (hi[0] - lo[0]),
            (p[:, 2] - (lo[2] + hi[2]) / 2) * 0.0048,
        ]
    )
    add("pin_handle", p, uv, tri)
    # Open the original carton lid; retain its exterior UVs and add an interior
    # matching the existing box collision bounds. The label uses the original
    # Bobby Pins artwork, with no inventory-count text.
    source, uv, tri = mesh(box, "BobbyPinBox:0")
    lo, hi = source.min(0), source.max(0)
    t = (source - lo) / (hi - lo)
    p = np.column_stack(
        [-0.031 + t[:, 1] * 0.062, -0.105 + t[:, 2] * 0.080, 0.028 - t[:, 0] * 0.118]
    )
    points = []
    tex = []
    angle = np.radians(-110)
    rotation = np.array(
        [
            [1, 0, 0],
            [0, np.cos(angle), -np.sin(angle)],
            [0, np.sin(angle), np.cos(angle)],
        ]
    )
    hinge = np.array([0, -0.025, -0.090])

    def lid(v):
        return (np.asarray(v) - hinge) @ rotation.T + hinge

    for face in tri:
        v = p[face].copy()
        u = uv[face].copy()
        if np.all(source[face, 2] > hi[2] - 0.001):
            # The raised lid's interior is red; its original label is laid out
            # at a readable aspect ratio on the inside below.
            u = np.column_stack(
                [
                    0.2 + (v[:, 0] + 0.031) / 0.062 * 0.5,
                    0.22 + (v[:, 2] + 0.09) / 0.118 * 0.35,
                ]
            )
            v = lid(v)
        points.extend(v)
        tex.extend(u)

    def quad(v, uvs):
        for indices in [(0, 1, 2), (0, 2, 3)]:
            points.extend(np.asarray(v)[list(indices)])
            tex.extend(np.asarray(uvs)[list(indices)])

    red = [(0.30, 0.25), (0.65, 0.25), (0.65, 0.55), (0.30, 0.55)]
    quad(
        [
            [-0.025, -0.098, -0.084],
            [0.025, -0.098, -0.084],
            [0.025, -0.098, 0.022],
            [-0.025, -0.098, 0.022],
        ],
        red,
    )
    for x in [-0.025, 0.025]:
        quad(
            [
                [x, -0.098, -0.084],
                [x, -0.025, -0.084],
                [x, -0.025, 0.022],
                [x, -0.098, 0.022],
            ],
            red,
        )
    for z in [-0.084, 0.022]:
        quad(
            [
                [-0.025, -0.098, z],
                [0.025, -0.098, z],
                [0.025, -0.025, z],
                [-0.025, -0.025, z],
            ],
            red,
        )
    label = lid(
        [
            [-0.026, -0.0257, -0.050],
            [0.026, -0.0257, -0.050],
            [0.026, -0.0257, -0.019],
            [-0.026, -0.0257, -0.019],
        ]
    )
    quad(label, [(0.246, 0.687), (0.749, 0.687), (0.749, 0.983), (0.246, 0.983)])
    add(
        "pin_box",
        np.array(points),
        np.array(tex),
        np.arange(len(points)).reshape(-1, 3),
        1,
    )
    p, uv, tri = mesh(can, "L_TinCan01:8")
    lo, hi = p.min(0), p.max(0)
    # Remove a possible top cap, leaving the existing rim and hollow walls.
    tri = tri[~np.all(p[tri, 2] > hi[2] - 0.05, axis=1)]
    p = np.column_stack(
        [
            (p[:, 0] - (lo[0] + hi[0]) / 2) * 0.064 / (hi[0] - lo[0]),
            -0.155 + (p[:, 2] - lo[2]) * 0.130 / (hi[2] - lo[2]),
            (p[:, 1] - (lo[1] + hi[1]) / 2) * 0.064 / (hi[1] - lo[1]),
        ]
    )
    add("driver_can", p, uv, tri, 2)
    a.output.mkdir(parents=True, exist_ok=True)
    with (a.output / "props.bin").open("wb") as f:
        f.write(b"TTWProp2")
        f.write(struct.pack("<I", len(parts)))
        for name, material, v, t in parts:
            f.write(struct.pack("<III", len(v), len(t), material))
            f.write(v.tobytes())
            f.write(t.tobytes())
    paths = [
        r"textures\terminals\lockinterface01",
        r"textures\clutter\bobbypin\bobbypinbox",
        r"textures\clutter\junk\tincan01",
    ]
    for n, path in enumerate(paths):
        for normal in [False, True]:
            raw = read(path + ("_n.dds" if normal else ".dds"))
            if n == 0 and not normal and a.lock_texture:
                raw = a.lock_texture.read_bytes()
                provenance["replacement_lock_texture"] = dict(
                    source=str(a.lock_texture), sha256=hashlib.sha256(raw).hexdigest()
                )
            image = Image.open(io.BytesIO(raw)).convert("RGBA")
            assert max(image.size) <= 4096
            (a.output / ("props-%d%s.rgba" % (n, "-n" if normal else ""))).write_bytes(
                struct.pack("<II", *image.size) + image.tobytes()
            )
    manifest = dict(
        format="TTWProp2",
        parts=[
            dict(
                name=n,
                material=m,
                vertices=len(v),
                triangles=len(t),
                bounds=[v[:, :3].min(0).tolist(), v[:, :3].max(0).tolist()],
            )
            for n, m, v, t in parts
        ],
        sources=provenance,
        redistribution="Private derived game/mod assets; excluded from addon source and release archives",
    )
    (a.output / "props-source.json").write_text(json.dumps(manifest, indent=2) + "\n")
    print("Prepared", [(n, len(t)) for n, m, v, t in parts])


if __name__ == "__main__":
    main()
