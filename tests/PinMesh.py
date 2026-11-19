#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-only
"""Regression for source strip thickness leaking into the round VR wire mesh.
Run with the asset-preparation Python (NumPy required); no game data needed.
"""

from pathlib import Path
import sys
import unittest
import numpy as np

sys.path.insert(0, str(Path(__file__).resolve().parents[1] / "tools"))
from pin_mesh import wire_mesh, dimensions


class RoundWire(unittest.TestCase):
    def test_no_triangle_leaves_isolated_pin_texture(self):
        for held in [False, True]:
            p, uv, tri, n = wire_mesh(held)
            self.assertTrue((uv >= 0).all() and (uv <= 1).all())
            self.assertTrue(np.isfinite(uv).all())

    def test_reach_thickness_normals_and_winding(self):
        d = dimensions()
        for held in [False, True]:
            p, uv, tri, normals = wire_mesh(held)
            rings = 33 if held else 3
            center = np.array([0, d["pinBendRadius"], 0]) if held else np.zeros(3)
            radial = p[:10] - center
            np.testing.assert_allclose(
                np.linalg.norm(radial, axis=1), 0.0014, atol=1e-12
            )
            u = radial[0] / 0.0014
            v = np.cross([0, 1, 0] if held else [0, 0, 1], u)
            v /= np.linalg.norm(v)
            for angle in np.linspace(0, 2 * np.pi, 180):
                projected = radial @ (u * np.cos(angle) + v * np.sin(angle))
                self.assertGreaterEqual(
                    np.ptp(projected), 0.0028 * np.cos(np.pi / 10) - 1e-10
                )
            tip = p[(rings - 1) * 11 : (rings - 1) * 11 + 10].mean(axis=0)
            expected = [0, 0.085, 0] if held else [0, 0, 0.071]
            np.testing.assert_allclose(tip, expected, atol=1e-12)
            np.testing.assert_allclose(np.linalg.norm(normals, axis=1), 1, atol=1e-12)
            area = np.cross(p[tri[:, 1]] - p[tri[:, 0]], p[tri[:, 2]] - p[tri[:, 0]])
            self.assertTrue((np.linalg.norm(area, axis=1) > 1e-10).all())
            self.assertTrue(
                (np.sum(area * normals[tri].mean(axis=1), axis=1) > 0).all()
            )


if __name__ == "__main__":
    unittest.main()
