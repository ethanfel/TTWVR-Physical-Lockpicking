#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-only
"""Prepare PRIVATE hand assets from an installed TTWVR mod and game.

Requires PyFFI 2.2.3, numpy and Pillow. No game/mod meshes or textures are
redistributed with the addon. API reference: https://www.niftools.org/pyffi/
"""
import argparse
import io
import json
import struct
import time
import zlib
from pathlib import Path
import numpy as np
from PIL import Image
# PyFFI 2.2.3 predates Python 3.8's removal of time.clock.
if not hasattr(time, "clock"):
    time.clock = time.perf_counter
from pyffi.formats.nif import NifFormat


def bsa_file(path, wanted):
    """Read one named file from a Fallout 3/New Vegas (104) BSA."""
    with path.open('rb') as f:
        header = f.read(36)
        if len(header) != 36 or header[:4] != b'BSA\0':
            return None
        version, offset, flags, folders, count, _, names_size, _ = struct.unpack('<8I', header[4:])
        if version != 104 or folders > 100000 or count > 1000000:
            return None
        f.seek(offset)
        records = [struct.unpack('<QII', f.read(16)) for _ in range(folders)]
        files = []
        for _, n, _ in records:
            length = f.read(1)[0]
            folder = f.read(length).rstrip(b'\0').decode('utf-8').lower()
            for _ in range(n):
                _, size, address = struct.unpack('<QII', f.read(16))
                files.append((folder, size, address))
        names = f.read(names_size).split(b'\0')
        for (folder, size, address), name in zip(files, names):
            if folder + '\\' + name.decode('utf-8').lower() != wanted.lower():
                continue
            f.seek(address)
            payload = f.read(size & 0x3fffffff)
            if flags & 0x100:
                payload = payload[1 + payload[0]:]
            if bool(flags & 4) != bool(size & 0x40000000):
                expected = struct.unpack('<I', payload[:4])[0]
                payload = zlib.decompress(payload[4:])
                if len(payload) != expected:
                    raise ValueError('BSA decompressed size mismatch')
            return payload
    return None


def matrix(m):
    return np.array(m.as_list(), dtype=float)


def nif_matrix(a):
    m = NifFormat.Matrix44()
    for row in range(4):
        for col in range(4):
            setattr(m, f'm_{row+1}{col+1}', float(a[row, col]))
    return m


def hand_asset(path, side):
    data = NifFormat.Data()
    with path.open('rb') as f:
        data.read(f)
    geom = next(b for b in data.blocks if isinstance(b, NifFormat.NiTriBasedGeom))
    root = geom.skin_instance.skeleton_root
    bones = {b.name.decode(): b for b in geom.skin_instance.bones}
    prefix = 'Bip01 ' + ('L' if side == 0 else 'R') + ' '
    hand = bones[prefix + 'Hand']
    inverse_hand = np.linalg.inv(matrix(hand.get_transform(root)))
    original = {n: matrix(b.get_transform(root)) for n, b in bones.items()}
    geometry_matrix = matrix(geom.get_transform(root))
    def vertices():
        v, normals = geom.get_skin_deformation()
        points = np.array([[p.x, p.y, p.z, 1] for p in v]) @ geometry_matrix @ inverse_hand
        points = (points[:, :3] - [5.5, 1.4, 0]) * .0125
        # Grip -Z runs from little finger to thumb, +X follows OpenXR handedness.
        sign = 1 if side == 0 else -1
        positions = np.column_stack([points[:, 1] * sign, -points[:, 0], points[:, 2] * sign])
        normals = np.array([[n.x,n.y,n.z] for n in normals]) @ (geometry_matrix @ inverse_hand)[:3,:3]
        normals = np.column_stack([normals[:,1]*sign,-normals[:,0],normals[:,2]*sign])
        normals /= np.maximum(np.linalg.norm(normals,axis=1)[:,None],1e-8)
        return positions, normals
    opened, open_normals = vertices()
    def finger_pose(finger, angles, spread=0):
        parent_name = prefix + 'Hand'
        parent_new = original[parent_name]
        chain = {}
        for j, suffix in enumerate(['', '1', '2']):
            name = prefix + finger + suffix
            if name not in bones:
                continue
            local = original[name] @ np.linalg.inv(original[parent_name])
            angle = np.radians(angles[j]); c, sn = np.cos(angle), np.sin(angle)
            rotation = np.eye(4); rotation[:2,:2] = [[c,sn],[-sn,c]]
            if j == 0 and spread:
                a = np.radians(spread); c, sn = np.cos(a), np.sin(a)
                swing = np.eye(4); swing[0,0]=swing[2,2]=c
                swing[0,2]=-sn; swing[2,0]=sn
                rotation = swing @ rotation
            bent = rotation @ local @ parent_new
            chain[name] = bent
            parent_name, parent_new = name, bent
        return chain
    def apply_pose(chains):
        for name, m in original.items():
            bones[name].set_transform(nif_matrix(chains.get(name,m)))
    closed_pose = {}
    for finger in ['Finger1','Finger2','Finger3','Finger4','Thumb1']:
        closed_pose.update(finger_pose(finger,[28,38,28] if finger.startswith('Thumb') else [35,50,35]))
    apply_pose(closed_pose)
    closed, closed_normals = vertices()
    # Fit the handle axis through the centres of the curled finger arcs.
    # These are derived from this installed rig, not controller aim bindings.
    centers=[]
    for finger in ['Finger1','Finger2','Finger3','Finger4']:
        points=[]
        for suffix in ['', '1', '2']:
            points.append((closed_pose[prefix+finger+suffix] @ inverse_hand)[3,:3])
        points.append((np.array([1.1,0,0,1]) @ closed_pose[prefix+finger+'2'] @ inverse_hand)[:3])
        points=np.array(points)
        xy=points[:,:2]
        circle=np.linalg.lstsq(np.column_stack([2*xy,np.ones(len(xy))]),np.sum(xy*xy,axis=1),rcond=None)[0]
        centers.append([circle[0],circle[1],np.mean(points[:,2])])
    sign=1 if side==0 else -1
    centers=(np.array(centers)-[5.5,1.4,0])*.0125
    centers=np.column_stack([centers[:,1]*sign,-centers[:,0],centers[:,2]*sign])
    axis=centers[-1]-centers[0]
    if axis[2]<0: axis=-axis
    axis/=np.linalg.norm(axis)
    quat=np.r_[np.cross([0,0,1],axis),1+axis[2]]
    quat/=np.linalg.norm(quat)
    driver_grip=np.r_[quat,np.mean(centers,axis=0)].astype('<f4')
    assert np.isfinite(driver_grip).all() and np.linalg.norm(driver_grip[4:])<.15
    print('Driver rig',side,'grip',driver_grip.tolist(),'finger centres',centers.tolist())
    # Fit the distal index/thumb pads together in this installed hand rig.
    # Only the baked visual pose is fitted; no runtime finger tracking assumed.
    seed = np.array([20.,45.,35.,30.,35.,25.,0.])
    def pinch_pose(values):
        chains = dict(closed_pose)
        chains.update(finger_pose('Finger1',values[:3]))
        chains.update(finger_pose('Thumb1',values[3:6],values[6]))
        tips=[]
        for name, extent in [('Finger12',1.25),('Thumb12',1.1)]:
            tips.append((np.array([extent,0,0,1]) @ chains[prefix+name] @ inverse_hand)[:3])
        return chains,np.array(tips)
    def score(values):
        _, tips=pinch_pose(values)
        # A small gap leaves room for the wire; modest regularisation avoids
        # a contorted solution when several finger angles can touch.
        gap=np.linalg.norm(tips[0]-tips[1])
        return (gap-.30)**2 + .00008*np.sum((values-seed)**2)
    values=seed.copy(); bounds=[(-10,65),(0,85),(0,75),(-30,75),(-20,75),(-15,70),(-60,60)]
    for step in [12,6,3,1,.3]:
        for _ in range(40):
            changed=False
            for n,(lo,hi) in enumerate(bounds):
                best=score(values); chosen=values.copy()
                for direction in [-1,1]:
                    trial=values.copy();trial[n]=np.clip(trial[n]+direction*step,lo,hi)
                    cost=score(trial)
                    if cost<best:best=cost;chosen=trial;changed=True
                values=chosen
            if not changed:break
    pinch_pose_data,tips=pinch_pose(values);apply_pose(pinch_pose_data)
    pinched,pinch_normals=vertices()
    center=(np.mean(tips,axis=0)-[5.5,1.4,0])*.0125
    sign=1 if side==0 else -1
    pinch_point=np.array([center[1]*sign,-center[0],center[2]*sign],dtype='<f4')
    print('Pinch rig',side,'pad gap mm',round(float(np.linalg.norm(tips[0]-tips[1]))*12.5,2),
          'anchor',pinch_point.tolist(),'angles',np.round(values,1).tolist())
    uv = np.array([[v.u, v.v] for v in geom.data.uv_sets[0]])
    triangles = np.array(list(geom.data.get_triangles()), dtype='<u4')
    assert opened.shape == closed.shape and np.isfinite(closed).all()
    assert len(opened) <= 10000 and len(triangles) <= 10000
    texture = next(b for b in data.blocks if isinstance(b, NifFormat.BSShaderTextureSet)).textures[0].decode()
    return np.column_stack([opened,closed,uv,open_normals,closed_normals,pinched,pinch_normals]).astype('<f4'),triangles,texture,pinch_point,driver_grip


def main():
    ap = argparse.ArgumentParser(description=__doc__)
    ap.add_argument('--mod', type=Path, required=True, help='Installed TTWVR mod root containing meshes/')
    ap.add_argument('--data', type=Path, required=True, help='Game Data folder containing Fallout BSAs')
    ap.add_argument('--output', type=Path, required=True, help='Private NVSE/Plugins/TTWVRPhysicalLockpicking directory')
    args = ap.parse_args()
    meshes = [hand_asset(args.mod / 'meshes/characters/_male' / (side + 'hand.nif'), n)
              for n, side in enumerate(['left', 'right'])]
    assert meshes[0][2].lower() == meshes[1][2].lower()
    texture = meshes[0][2]
    loose = args.data.joinpath(*texture.replace('\\', '/').split('/'))
    raw = loose.read_bytes() if loose.exists() else None
    if raw is None:
        for archive in sorted(args.data.glob('*.bsa')):
            raw = bsa_file(archive, texture)
            if raw is not None:
                break
    if raw is None:
        raise SystemExit('Required installed hand texture not found: ' + texture)
    im = Image.open(io.BytesIO(raw)).convert('RGBA')
    im.thumbnail((1024, 1024))
    args.output.mkdir(parents=True, exist_ok=True)
    with (args.output / 'hands.bin').open('wb') as f:
        f.write(b'TTWHand4')
        for vertices, triangles, _, pinch_point, driver_grip in meshes:
            f.write(struct.pack('<II', len(vertices), len(triangles)))
            f.write(pinch_point.tobytes())
            f.write(driver_grip.tobytes())
            f.write(vertices.tobytes())
            f.write(triangles.tobytes())
    (args.output / 'hands.rgba').write_bytes(struct.pack('<II', *im.size) + im.tobytes())
    (args.output / 'hands-source.json').write_text(json.dumps(dict(
        mod=str(args.mod), texture=texture, units_to_meters=.0125,
        mapping='OpenXR grip pose; palm centroid estimated from installed bind pose; headset alignment pending',
        animation='baked open, curled and fitted thumb/index pinch poses; tool-specific interpolation',
        pinch_anchors=[m[3].tolist() for m in meshes],
        driver_grips=[m[4].tolist() for m in meshes],
        redistribution='Local derived game assets: do not include in addon releases'), indent=2) + '\n')
    print('Prepared local hand meshes:', [(len(v), len(t)) for v, t, _, _, _ in meshes], 'texture:', im.size)


if __name__ == '__main__':
    main()
