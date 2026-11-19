# SPDX-License-Identifier: GPL-3.0-only
"""Round demo wire, unwrapped onto an isolated original pin-metal texture.

Interpolating source sections across disconnected UV islands sampled the red
screwdriver handle. A continuous verified metal strip keeps sides, caps and
mipmapped texture samples clear of unrelated atlas regions.
"""

from pathlib import Path
import re
import numpy as np


def dimensions():
    text = (Path(__file__).parent.parent / "src/ModelDimensions.h").read_text()
    return {
        name: float(value)
        for name, value in re.findall(r"constexpr float (\w+) = ([.\d]+)f;", text)
    }


def wire_mesh(held=False):
    d = dimensions()
    segments, sides = (32 if held else 2), 10
    verts, normals, coords, faces = [], [], [], []
    for ring in range(segments + 1):
        t = ring / segments
        if held:
            center = np.array(
                [
                    d["pinWaveAmplitude"] * np.sin(t * 8 * np.pi),
                    d["pinBendRadius"] + d["pinHandleLength"] * t,
                    0,
                ]
            )
            tangent = np.array(
                [
                    d["pinWaveAmplitude"] * 8 * np.pi * np.cos(t * 8 * np.pi),
                    d["pinHandleLength"],
                    0,
                ]
            )
            tangent /= np.linalg.norm(tangent)
            u, v = np.array([tangent[1], -tangent[0], 0]), np.array([0, 0, 1])
        else:
            center = np.array([0, 0, (d["pinShaftLength"] - d["pinBendRadius"]) * t])
            u, v = np.array([1, 0, 0]), np.array([0, 1, 0])
        for side in range(sides + 1):
            angle = side * 2 * np.pi / sides
            n = u * np.cos(angle) + v * np.sin(angle)
            verts.append(center + n * d["pinWireRadius"])
            normals.append(n)
            coords.append([t, side / sides])

    for ring in range(segments):
        for side in range(sides):
            a = ring * (sides + 1) + side
            b = a + sides + 1
            faces.extend([(a, b, a + 1), (a + 1, b, b + 1)])
    # Separate cap normals avoid lighting an end face like the wire's side.
    for ring in [0, segments]:
        first = ring * (sides + 1)
        points = np.array(verts[first : first + sides])
        center = points.mean(axis=0)
        tangent = np.cross(normals[first], normals[first + sides // 4])
        tangent /= np.linalg.norm(tangent)
        if held:
            tangent = -tangent
        cap_normal = tangent * (-1 if ring == 0 else 1)
        cap_first = len(verts)
        verts.extend(points)
        normals.extend([cap_normal] * sides)
        coords.extend(coords[first : first + sides])
        verts.append(center)
        normals.append(cap_normal)
        coords.append(np.mean(coords[first : first + sides], axis=0))
        for side in range(sides):
            faces.append(
                (cap_first + sides, cap_first + side, cap_first + (side + 1) % sides)
            )
    verts, normals, faces = (
        np.array(verts),
        np.array(normals),
        np.array(faces, dtype=np.uint32),
    )
    # Consistent outward winding for both longitudinal-axis conventions.
    cross = np.cross(
        verts[faces[:, 1]] - verts[faces[:, 0]], verts[faces[:, 2]] - verts[faces[:, 0]]
    )
    inward = np.sum(cross * normals[faces].mean(axis=1), axis=1) < 0
    faces[inward] = faces[inward][:, [0, 2, 1]]
    return verts, np.array(coords), faces, normals
