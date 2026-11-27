#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-only
"""Prepare the game's worn key for a 60 mm, tip-first VR grip."""
import argparse
import io
from pathlib import Path
from prepare_props import NifFormat as N, bsa_file, mesh, source_normals

def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--data", type=Path, required=True)
    parser.add_argument("--output", type=Path, required=True)
    args = parser.parse_args()
    data = N.Data()
    data.read(io.BytesIO(bsa_file(args.data / "Fallout - Meshes.bsa", r"meshes\clutter\key01dirty.nif")))
    name = "Key_Dirty:0"
    shape = next(b for b in data.blocks if getattr(b, "name", None) == name.encode())
    vertices, _, _ = mesh(data, name, True)
    normals = source_normals(data, name, True)
    vertices[:, 1] -= vertices[:, 1].min()
    vertices *= .060 / vertices[:, 1].max()
    def set_vector(v, xyz):
        v.x, v.y, v.z = map(float, xyz)
    for i, point in enumerate(vertices):
        set_vector(shape.data.vertices[i], point[[2,0,1]])
        set_vector(shape.data.normals[i], normals[i, [2,0,1]])
    shape.translation.x = shape.translation.y = shape.translation.z = 0
    shape.rotation.set_identity()
    shape.scale = 1
    shape.name = b"TTWVRKeyBlade"
    shape.data.update_center_radius()
    shape.update_tangent_space()
    root = N.NiNode()
    root.flags, root.name = 14, b"TTWVRPhysicalKey"
    root.add_child(shape)
    data.roots = [root]
    path = args.output / "meshes/TTWVRPhysicalLockpicking/physical-key.nif"
    path.parent.mkdir(parents=True, exist_ok=True)
    with path.open("wb") as stream:
        data.write(stream)

if __name__ == "__main__":
    main()
