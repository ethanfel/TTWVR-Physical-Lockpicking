#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-only
"""Build the private hotkey item and tool meshes using the installed hand fit."""

import argparse
import io
import json
from mount_surfaces import mount_basis, write_live_surfaces
from pathlib import Path
import shutil
import struct
import numpy as np
from prepare_hands import NifFormat as N, bsa_file, fit_driver_grip, fit_pinch, finger_solver, matrix
from prepare_world_locks import make_shape
from test_stage.build import record, group, z
from pin_mesh import dimensions

D = dimensions()
PIN_LENGTH = D["pinShaftLength"]
PIN_RADIUS = D["pinBendRadius"]
DRIVER_CENTER = (D["driverHandleStart"] + D["driverLength"]) / 2


def fit_driver(v):
    # Match fitPreparedDriverBlade and driverMetalPosition in ToolGeometry.h.
    z = v[:, 2]
    tip, neck = D["driverBladeEnd"], D["driverShaftStart"]
    mask = z < neck
    src = np.interp(
        z[mask],
        [0, tip, neck],
        [
            D["propBladeTipHalfWidth"],
            D["propBladeHalfWidth"],
            D["propBladeNeckHalfWidth"],
        ],
    )
    dst = np.interp(
        z[mask],
        [0, tip, neck],
        [
            D["driverBladeTipHalfWidth"],
            D["driverBladeHalfWidth"],
            D["propBladeNeckHalfWidth"],
        ],
    )
    scale = dst / src

    def slopes(a, b, c):
        return np.where(z[mask] > tip, (c - b) / (neck - tip), (b - a) / tip)

    slope = (
        slopes(
            D["driverBladeTipHalfWidth"],
            D["driverBladeHalfWidth"],
            D["propBladeNeckHalfWidth"],
        )
        - scale
        * slopes(
            D["propBladeTipHalfWidth"],
            D["propBladeHalfWidth"],
            D["propBladeNeckHalfWidth"],
        )
    ) / src
    v[mask, 3] /= scale
    v[mask, 5] -= v[mask, 3] * v[mask, 0] * slope
    v[mask, 0] *= scale
    span = D["driverHandleStart"] - neck
    angle = np.pi / 2 * np.clip((D["driverHandleStart"] - z) / span, 0, 1)
    for k in (0, 3):
        x, y = v[:, k].copy(), v[:, k + 1].copy()
        v[:, k] = x * np.cos(angle) - y * np.sin(angle)
        v[:, k + 1] = x * np.sin(angle) + y * np.cos(angle)
    blend = (z > neck) & (z < D["driverHandleStart"])
    v[blend, 5] += (
        np.pi / (2 * span) * (-v[blend, 1] * v[blend, 3] + v[blend, 0] * v[blend, 4])
    )
    v[:, 3:6] /= np.maximum(np.linalg.norm(v[:, 3:6], axis=1)[:, None], 1.0e-10)


def key_grip(bind, prefix, hand):
    # Curl the index and lay the thumb pad against it, with room for the
    # 5.74 mm thick head of Key01Dirty. Mirror spread and roll at the wrist.
    sign = 1 if hand else -1
    finger = finger_solver(bind, prefix)
    pose = finger("Finger1", [44.1, 55.8, 52.9], sign * 8.2, sign * -15.9)
    pose.update(finger("Thumb1", [18.5, 38.6, 7.3], sign * -31.8, sign * -65))
    for digit in ("Finger2", "Finger3", "Finger4"):
        pose.update(finger(digit, [35, 50, 35]))
    return pose


def grip_rotations(bind, prefix, pose):
    rotations = bytearray()
    for digit in ("Thumb1", "Finger1", "Finger2", "Finger3", "Finger4"):
        parent = bind[prefix + "Hand"]
        for suffix in ("", "1", "2"):
            child = pose[prefix + digit + suffix]
            local = child @ np.linalg.inv(parent)
            rotation = local[:3, :3].T
            if not np.isfinite(rotation).all() or not np.isclose(np.linalg.det(rotation), 1, atol=.001):
                raise ValueError(f"Invalid {prefix}{digit}{suffix} grip rotation")
            rotations.extend(np.asarray(rotation, dtype="<f4").tobytes())
            parent = child
    return rotations


def build(output, assets, data, hand_mod=None):
    output.mkdir(parents=True, exist_ok=True)
    # Hidden inventory token stores the vanilla hotkey for a Misc bobby pin.
    armor = record(
        b"ARMO",
        0x01000800,
        [
            (b"EDID", z("TTWVRLockpickKit")),
            (b"FULL", z("Bobby Pin")),
            (b"BMDT", struct.pack("<II", 0, 0x40)),
            (b"ICON", z(r"Interface\Icons\PipboyImages\Items\item_bobby_pin.dds")),
            (b"DATA", struct.pack("<IIf", 0, 100, 0)),
            (b"DNAM", bytes(12)),
        ],
        flags=0x400,
    )
    header = record(
        b"TES4",
        0,
        [
            (b"HEDR", struct.pack("<fII", 1.34, 2, 0x801)),
            (b"CNAM", z("TTWVR Physical Lockpicking")),
            (b"MAST", z("FalloutNV.esm")),
            (b"DATA", bytes(8)),
        ],
    )
    (output / "TTWVRLockKit.esp").write_bytes(header + group(b"ARMO", 0, armor))
    shutil.copytree(
        Path(__file__).resolve().parents[1] / "kit", output, dirs_exist_ok=True
    )
    template = N.Data()
    template.read(
        io.BytesIO(
            bsa_file(
                data / "Fallout - Meshes.bsa", r"meshes\terminals\lockinterface01.nif"
            )
        )
    )
    shape = next(b for b in template.blocks if getattr(b, "name", None) == b"lock:0")
    shader = next(
        p for p in shape.properties if isinstance(p, N.BSShaderPPLightingProperty)
    )
    mat = next(p for p in shape.properties if isinstance(p, N.NiMaterialProperty))
    props = (assets / "props.bin").read_bytes()
    hands = (assets / "hands.bin").read_bytes()
    assert props[:8] == b"TTWProp5" and hands[:8] == b"TTWHand4"
    pieces = []
    off = 12
    for _ in range(7):
        nv, nt, _ = struct.unpack_from("<3I", props, off)
        off += 12
        v = np.frombuffer(props, "<f4", nv * 8, off).reshape(-1, 8).copy()
        off += nv * 32
        t = np.frombuffer(props, "<u4", nt * 3, off).reshape(-1, 3).copy()
        off += nt * 12
        pieces.append((v, t))

    def rotate(q):
        x, y, z, w = q
        a = np.array([[0, -z, y], [z, 0, -x], [-y, x, 0]])
        return np.eye(3) + 2 * (w * a + a @ a)

    out = output / "meshes/TTWVRPhysicalLockpicking"
    out.mkdir(parents=True, exist_ok=True)
    rig = bytearray(b"TTWGrip2")
    key_rig = bytearray(b"TTWKeyG1")
    frames = bytearray(b"TTWLive1")
    off = 8
    for hand in range(2):
        nv, nt = struct.unpack_from("<2I", hands, off)
        off += 8
        pinch = np.frombuffer(hands, "<f4", 3, off)
        grip = np.frombuffer(hands, "<f4", 7, off + 12)
        off += 40 + nv * 80 + nt * 12
        hand_source = N.Data()
        name = ("left", "right")[hand] + "hand.nif"
        with ((hand_mod or data) / "meshes/characters/_male" / name).open("rb") as f:
            hand_source.read(f)
        geom = next(b for b in hand_source.blocks if isinstance(b, N.NiTriBasedGeom))
        skin = geom.skin_instance
        prefix = ("Bip01 L ", "Bip01 R ")[hand]
        bind = {b.name.decode(): matrix(b.get_transform(skin.skeleton_root)) for b in skin.bones}
        pinch_pose, _, _ = fit_pinch(bind, prefix)
        driver_pose, _, _ = fit_driver_grip(bind, prefix, hand)
        for pose in (pinch_pose, driver_pose):
            rig.extend(grip_rotations(bind, prefix, pose))
        key_rig.extend(grip_rotations(bind, prefix, key_grip(bind, prefix, hand)))
        sign = 1 if hand == 0 else -1
        for tool in range(2):
            root = N.NiNode()
            root.name = f"TTWVRKit_{hand}_{tool}".encode()
            root.flags = 14
            d = N.Data(
                version=template.version,
                user_version=template.user_version,
                user_version_2=template.user_version_2,
            )
            d.header.endian_type = 1
            d.roots = [root]
            # Invert the canonical hand mapping used by prepare_hands.py.
            joint = np.array([[0, -1, 0], [sign, 0, 0], [0, 0, sign]])
            angle = np.deg2rad(-145 if hand else 145) / 2
            R = (
                rotate(grip[:4])
                if tool
                else rotate((0, 0, np.sin(angle), np.cos(angle)))
                @ np.diag([-1, 1, -1] if hand else [1, 1, 1])
            )
            anchor = grip[4:] if tool else pinch
            center = (
                np.array([0, 0, DRIVER_CENTER])
                if tool
                else np.array([0.006, -PIN_LENGTH + 0.065, 0])
            )
            if tool:
                tip = joint @ (anchor - R @ center) / .0125 + [5.5, 1.4, 0]
                frames.extend(struct.pack("<12f", *(joint @ R).flat, *tip))
            for idx in [2] if tool else [3, 4, 5]:
                if idx == 5:
                    # Closed pin's curved loop, using its actual two endpoints.
                    v, t = loop_mesh()
                else:
                    v, t = pieces[idx]
                    v = v.copy()
                if idx == 2:
                    fit_driver(v)
                if tool:
                    v[:, :3] = (((v[:, :3] - center) @ R.T + anchor) @ joint.T) / 0.0125 + [
                        5.5, 1.4, 0,
                    ]
                    v[:, 3:6] = v[:, 3:6] @ R.T @ joint.T
                else:
                    # The live mesh has separate shaft, loop and held-leg
                    # transforms. Keep their authored axes and textures.
                    v[:, :3] *= 70.0
                stem = "props-0" if tool else "pin-metal"
                aliases = {"props-0": stem, "props-0-n": stem + "-n"}
                sh = make_shape(
                    v, t, 0, np.ones(len(v)), aliases, shader, mat, f"KitPart{idx}"
                )
                if idx == 5:
                    sh.data.consistency_flags = N.ConsistencyType.CTMUTABLE
                root.add_child(sh)
            path = out / f"kit-{hand}-{tool}.nif"
            with path.open("wb") as f:
                d.write(f)
    assert off == len(hands)
    output_rig = output / "NVSE/Plugins/TTWVRPhysicalLockpicking/kit-pinch.bin"
    output_rig.parent.mkdir(parents=True, exist_ok=True)
    output_rig.write_bytes(rig + key_rig)
    profiles = json.loads(Path(__file__).with_name("target_profiles.json").read_text())
    frames.extend(struct.pack("<I", len(profiles)))
    for kind, profile in enumerate(profiles, 1):
        assert profile["kind"] == kind
        frames.extend(struct.pack("<2I16f", profile.get("lock_style", 0),
                                  int("reverse_anchor" in profile), profile["scale"],
                                  *mount_basis(profile).flat, *profile["anchor"],
                                  *profile.get("reverse_anchor", [0, 0, 0])))
    output_rig.with_name("kit-world.bin").write_bytes(frames)
    write_live_surfaces(assets, profiles, output_rig.with_name("kit-surfaces.bin"))


def loop_mesh():
    verts = []
    tris = []
    for i in range(9):
        t = i / 8
        h = np.array([0, -PIN_LENGTH, 0])
        p = (
            (1 - t) ** 2 * (h + [0, PIN_RADIUS, 0])
            + 2 * t * (1 - t) * (h + [0.003, -0.006, 0])
            + t * t * (h + [0.006, PIN_RADIUS, 0])
        )
        tangent = 2 * (1 - t) * np.array(
            [0.003, -0.006 - PIN_RADIUS, 0]
        ) + 2 * t * np.array([0.003, 0.006 + PIN_RADIUS, 0])
        normal = np.array([-tangent[1], tangent[0], 0]) / np.linalg.norm(tangent)
        for j in range(8):
            a = j * np.pi / 4
            n = normal * np.cos(a) + [0, 0, np.sin(a)]
            verts.append([*(p + D["pinWireRadius"] * n), *n, t, j / 8])
            if i:
                a = (i - 1) * 8 + j
                b = (i - 1) * 8 + (j + 1) % 8
                tris.extend(((a, b, b + 8), (a, b + 8, a + 8)))
    return np.array(verts), np.array(tris)


if __name__ == "__main__":
    p = argparse.ArgumentParser(description=__doc__)
    for name in ("output", "assets", "data"):
        p.add_argument("--" + name, required=True, type=Path)
    p.add_argument("--hand-mod", type=Path, help="TTWVR mod root when its hands are in the mod manager")
    a = p.parse_args()
    build(a.output, a.assets, a.data, a.hand_mod)
