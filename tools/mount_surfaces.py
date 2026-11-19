# SPDX-License-Identifier: GPL-3.0-only
"""Measure solid mounting surfaces in the textured, prepared target geometry.

Points use the original NIF's coordinates. A face basis is right, up, outward.
Only the visible side of an opaque triangle can support hardware. The nearest
triangle also owns the depth, so a protruding knob cannot be hidden by a wall
behind it. Alpha-tested cutouts use the prepared diffuse texture.
"""
import struct
from pathlib import Path

import numpy as np


def mount_basis(profile):
    if 'basis' in profile:
        return np.array(profile['basis'], dtype=float)
    return np.eye(3)[:, profile['axes']] * profile.get('signs', [1, 1, 1])


class MountSurface:
    def __init__(self, folder, original, basis):
        self.parts = []
        self.scale = original['scale']
        self.basis = basis
        kind = original['kind']
        blob = (folder / f'target-{kind}.bin').read_bytes()
        magic, index, parts, _, frames, _, _ = struct.unpack_from('<8s4IfI', blob)
        if magic != b'TTWTarg2' or kind != index:
            raise ValueError('Unexpected prepared target')
        offset = 56 + frames * 28
        for i in range(parts):
            nv, nt, mat, _, alpha = struct.unpack_from('<5I', blob, offset)
            offset += 20
            v = np.frombuffer(blob, '<f4', nv * 8, offset).reshape(-1, 8)
            offset += nv * 32
            faces = np.frombuffer(blob, '<u4', nt * 3, offset).reshape(-1, 3)
            offset += nt * 12
            model = ((v[:, :3] - [0, 0, -.013]) / self.scale) @ mount_basis(original).T + original['anchor']
            points = (model @ basis)[faces]
            ab = points[:, 1] - points[:, 0]
            ac = points[:, 2] - points[:, 0]
            cross = np.cross(ab, ac)
            mask = cross[:, 2] > 1e-7
            texture = None
            if alpha:
                path = folder / f'target-{kind}-{mat}.rgba'
                with path.open('rb') as stream:
                    w, h = struct.unpack('<2I', stream.read(8))
                texture = np.memmap(path, 'u1', 'r', offset=8, shape=(h,w,4))
            if mask.any():
                normal_z = cross[mask, 2] / np.linalg.norm(cross[mask], axis=1)
                self.parts.append((points[mask, 0], ab[mask], ac[mask], cross[mask, 2],
                                   normal_z, v[faces[mask], 6:8], texture))
        if offset != len(blob):
            raise ValueError('Unexpected end of prepared mesh')

    def depths(self, xy):
        """Outermost visible depth and geometric facing at each sample."""
        z = np.full(len(xy), -np.inf)
        facing = np.zeros(len(xy))
        for a, ab, ac, det, nz, uv, tex in self.parts:
            low = np.minimum(np.minimum(a[:, :2], (a+ab)[:, :2]), (a+ac)[:, :2])
            high = np.maximum(np.maximum(a[:, :2], (a+ab)[:, :2]), (a+ac)[:, :2])
            overlap = ((high >= xy.min(axis=0)) & (low <= xy.max(axis=0))).all(axis=1)
            for i in np.flatnonzero(overlap):
                hits = np.flatnonzero(((xy >= low[i]-1e-7) & (xy <= high[i]+1e-7)).all(axis=1))
                q = xy[hits]-a[i,:2]
                b = (q[:,0]*ac[i,1] - q[:,1]*ac[i,0])/det[i]
                c = (ab[i,0]*q[:,1] - ab[i,1]*q[:,0])/det[i]
                valid = (b >= -1e-6) & (c >= -1e-6) & (b+c <= 1+1e-6)
                depth = a[i,2]+b*ab[i,2]+c*ac[i,2]
                valid &= depth > z[hits]
                if tex is not None:
                    coord = (uv[i,0]+b[:,None]*(uv[i,1]-uv[i,0])+c[:,None]*(uv[i,2]-uv[i,0])) % 1
                    h,w = tex.shape[:2]
                    valid &= tex[(coord[:,1]*h).astype(int)%h, (coord[:,0]*w).astype(int)%w, 3] >= 128
                take = hits[valid]
                z[take] = depth[valid]
                facing[take] = nz[i]
        return z, facing

    def candidates(self, seed, style, search_mm=105):
        """Fit the ring or the hanging padlock staple, keeping a clear approach."""
        ring = np.arange(12)*2*np.pi/12
        # The staple attaches 74 mm above the padlock's working centre.
        if style == 1:
            origin = np.array([0., .074])
            bearing = np.array([[0,0],[-.004,0],[.004,0],[0,-.005],[0,.005]])
            clearance = np.array([[-.027,-.055],[0,-.075],[.027,-.055],[-.020,-.030],[.020,-.030]])
        else:
            origin = np.zeros(2)
            radius = .043 if style==0 else .041
            bearing = np.vstack([[0.,0.], np.column_stack((np.cos(ring), np.sin(ring)))*radius,
                                 np.column_stack((np.cos(ring), np.sin(ring)))*radius*.55])
            clearance = np.column_stack((np.cos(ring), np.sin(ring)))*radius*1.18
        # Integer millimetres deduplicate samples across candidate placements.
        steps = np.arange(-search_mm, search_mm+1, 7)
        steps = np.unique(np.append(steps, 0))
        shift = np.array([(x,y) for x in steps for y in steps if x*x+y*y <= search_mm**2], dtype=float)*.001
        seed_xy = (np.asarray(seed) @ self.basis)[:2]
        offsets = np.vstack([bearing, clearance])
        samples = np.rint((shift[:,None,:]+offsets)*1000).astype(int)
        unique, inv = np.unique(samples.reshape(-1,2), axis=0, return_inverse=True)
        depth, facing = self.depths(seed_xy + (origin+unique*.001)/self.scale)
        depth, facing = (a[inv].reshape(samples.shape[:2]) for a in (depth, facing))
        base = depth[:,0]
        valid = np.isfinite(base) & (facing[:,0] >= .70)
        # Across the footprint a step/knob/slot fails, even if its centre would pass.
        with np.errstate(invalid='ignore'):
            relative = (depth-base[:,None])*self.scale
        n = len(bearing)
        supported = (np.abs(relative[:,:n]) <= .0065) & (facing[:,:n] >= .66)
        valid &= supported.all(axis=1)
        # Permit objects behind the hanging padlock, but reject geometry in front of it.
        valid &= ~(relative[:,n:] > (.013 if style==1 else .008)).any(axis=1)
        if not valid.any():
            return {}
        score = np.linalg.norm(shift,axis=1) + .7*np.max(np.where(supported, np.abs(relative[:,:n]),0),axis=1)
        choices = {}
        for i in np.flatnonzero(valid):
            point = np.r_[seed_xy+shift[i]/self.scale, base[i]] @ self.basis.T
            key = tuple(np.rint(shift[i]*1000).astype(int))
            choices[key] = (score[i], point.round(6).tolist())
        return choices

    def fit(self, seed, style, search_mm=105):
        choices = self.candidates(seed, style, search_mm)
        return min(choices.values())[1] if choices else None


def write_live_surfaces(folder, profiles, output):
    """Sample the actual closed model around each keyhole in its mounted frame."""
    import json
    originals = {p['model']: p for p in json.loads(
        (folder / 'targets-source.json').read_text())['models']}
    side = 81
    spacing = .004
    xy = (np.indices((side, side))[::-1].reshape(2, -1).T - side // 2) * spacing
    blob = bytearray(struct.pack('<8sIIf', b'TTWFace1', len(profiles), side, spacing))
    for profile in profiles:
        axes = mount_basis(profile)
        for reverse in range(1 + int('reverse_anchor' in profile)):
            basis = axes @ np.diag([-1, 1, -1]) if reverse else axes
            anchor = np.array(profile['reverse_anchor' if reverse else 'anchor']) @ basis
            scale = profile['scale']
            surface = MountSurface(folder, originals[profile['model']], basis)
            depth, _ = surface.depths(anchor[:2] + xy / scale)
            z = (depth - anchor[2]) * scale - .013
            # Ignore distant surfaces behind openings or on unrelated overhangs.
            valid = np.isfinite(z) & (z >= -.16) & (z <= .16)
            samples = np.full(side * side, -32768, dtype='<i2')
            samples[valid] = np.rint(z[valid] * 100000)
            blob.extend(samples.tobytes())
    tmp = output.with_suffix('.tmp')
    tmp.write_bytes(blob)
    tmp.replace(output)


def fit_through_door(front, reverse, seed, search_mm=105):
    """Find one bore axis supported on both leaves' faces, including push bars."""
    primary = front.candidates(seed, 0, search_mm)
    opposite = reverse.candidates(seed, 0, search_mm)
    pairs = []
    for (x,y),(cost,pt) in primary.items():
        back = opposite.get((-x,y))
        if back:
            thickness = np.dot(np.subtract(pt,back[1]),front.basis[:,2])*front.scale
            if .003 <= thickness <= .5:
                pairs.append((cost+back[0],pt,back[1]))
    return min(pairs)[1:] if pairs else None


def atlas_keyholes(folder, original):
    """Map both sides of the original office-door brass keyplate from its atlas."""
    blob = (folder / f"target-{original['kind']}.bin").read_bytes()
    _,_,parts,_,frames,_,_=struct.unpack_from('<8s4IfI',blob)
    offset=56+frames*28
    hits=[]
    for index in range(parts):
        nv,nt,material,_,_=struct.unpack_from('<5I',blob,offset)
        offset+=20
        v=np.frombuffer(blob,'<f4',nv*8,offset).reshape(-1,8)
        offset+=nv*32
        tri=np.frombuffer(blob,'<u4',nt*3,offset).reshape(-1,3)
        offset+=nt*12
        texture=original['materials'][material][0].replace(chr(92),'/').split('/')[-1].lower()
        if texture!='offdoorsm01.dds' or 'frame' in original['parts'][index]['mesh'].lower():
            continue
        uv=v[tri,6:8]
        a=uv[:,0];b=uv[:,1]-a;c=uv[:,2]-a
        q=np.array([917.5/1024,101.5/1024])-a
        det=b[:,0]*c[:,1]-b[:,1]*c[:,0]
        valid=np.abs(det)>1e-8
        det=np.where(valid,det,1)
        u=(q[:,0]*c[:,1]-q[:,1]*c[:,0])/det
        w=(b[:,0]*q[:,1]-b[:,1]*q[:,0])/det
        for j in np.flatnonzero(valid & (u>=0) & (w>=0) & (u+w<=1)):
            a,b,c=v[tri[j],:3]
            point=a+u[j]*(b-a)+w[j]*(c-a)
            basis=mount_basis(original)
            model=((point-[0,0,-.013])/original['scale'])@basis.T+original['anchor']
            n=np.cross(b-a,c-a)@basis.T
            hits.append((model,n/np.linalg.norm(n)))
    return hits

def check_catalogue(assets, profiles):
    import json
    report = json.loads((assets/'targets-source.json').read_text())
    models = {m['model']: m for m in report['models']}
    checks = []
    for profile in profiles:
        path = profile['model']
        original = models[path]
        basis = mount_basis(profile)
        front = np.array(profile['anchor'])
        back = profile.get('reverse_anchor')
        style = profile.get('lock_style', 0)
        scale = profile['scale']
        if profile.get('mount_basis') == 'original keyplate atlas on both faces':
            hits = atlas_keyholes(assets, original)
            for pt,side in ((front,1),(back,-1)):
                if not any(np.linalg.norm(np.subtract(pos,pt))*scale < .0002 and
                           side * np.dot(normal,basis[:,2]) > .95 for pos,normal in hits):
                    raise ValueError(f'{path}: cylinder missed the original painted keyway')
            method = 'keyplate UV'
        elif profile.get('mount_basis') == 'original shaped lock or clasp':
            digest = report['source_files']['meshes/'+path]['sha256']
            if (profile.get('source_sha256') != digest or
                not np.allclose(front, original['anchor'], atol=1e-5) or
                not np.allclose(basis, mount_basis(original), atol=1e-5)):
                raise ValueError(f'{path}: calibrated fixture needs review')
            method = 'calibrated fixture'
        else:
            placements = [(front,basis)]
            if back is not None:
                reverse = basis @ np.diag([-1,1,-1])
                delta = (np.subtract(front,back)@basis)*scale
                if np.linalg.norm(delta[:2]) > .0002 or not .003 <= delta[2] <= .5:
                    raise ValueError(f'{path}: door cylinders do not share one through-axis')
                placements.append((back,reverse))
            for anchor,orientation in placements:
                surface = MountSurface(assets,original,orientation)
                hit = surface.fit(anchor,style,search_mm=0)
                if hit is None or np.linalg.norm(np.subtract(hit,anchor))*scale > .002:
                    raise ValueError(f'{path}: missing support or blocked approach at {anchor}')
            method = 'textured footprint'
        checks.append(dict(kind=profile['kind'], model=path, method=method, faces=1+int(back is not None)))
    return checks


if __name__ == '__main__':
    import argparse
    import json
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--assets',type=Path,required=True,help='Private prepared target cache containing targets-source.json')
    parser.add_argument('--profiles',type=Path,default=Path(__file__).with_name('target_profiles.json'))
    parser.add_argument('--report',type=Path,help='Optional per-model JSON results')
    args=parser.parse_args()
    rows=check_catalogue(args.assets,json.loads(args.profiles.read_text()))
    if args.report:
        args.report.write_text(json.dumps(rows,indent=2)+'\n')
    print(f"Verified {len(rows)} target models and {sum(r['faces'] for r in rows)} mount faces.")
