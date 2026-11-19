#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-only
"""Prepare PRIVATE hand assets from an installed TTWVR mod and game.

Requires PyFFI 2.2.3, numpy and Pillow. No game/mod meshes or textures are
redistributed with the addon. API reference: https://www.niftools.org/pyffi/
"""
import argparse
from functools import lru_cache
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


@lru_cache(maxsize=128)
def bsa_index(path):
    """Index each local BSA once during a preparation run."""
    with path.open('rb') as f:
        header = f.read(36)
        if len(header) != 36 or header[:4] != b'BSA\0':
            return 0, {}
        version, offset, flags, folders, count, _, names_size, _ = struct.unpack('<8I', header[4:])
        if version != 104 or folders > 100000 or count > 1000000:
            return 0, {}
        f.seek(offset)
        records = [struct.unpack('<QII', f.read(16)) for _ in range(folders)]
        files = []
        for _, n, _ in records:
            length = f.read(1)[0]
            folder = f.read(length).rstrip(b'\0').lower()
            for _ in range(n):
                _, size, address = struct.unpack('<QII', f.read(16))
                files.append((folder, size, address))
        names = f.read(names_size).split(b'\0')
        index = {folder + b'\\' + name.lower(): (size, address)
                 for (folder, size, address), name in zip(files, names)}
        return flags, index


def bsa_file(path, wanted):
    flags, index = bsa_index(path)
    entry = index.get(wanted.lower().encode('utf-8'))
    if entry is None:
        return None
    size, address = entry
    with path.open('rb') as f:
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


def matrix(m):
    return np.array(m.as_list(), dtype=float)


def nif_matrix(a):
    m = NifFormat.Matrix44()
    for row in range(4):
        for col in range(4):
            setattr(m, f'm_{row+1}{col+1}', float(a[row, col]))
    return m


def driver_mesh(path):
    raw=path.read_bytes()
    if raw[:8]!=b'TTWProp5':raise ValueError('Prepare current props before the hands')
    offset=12
    for part in range(struct.unpack_from('<I',raw,8)[0]):
        n,t,material=struct.unpack_from('<3I',raw,offset);offset+=12
        if n>10000 or t>30000:raise ValueError('Invalid private prop mesh')
        vertices=np.frombuffer(raw,'<f4',n*8,offset).reshape(n,8);offset+=n*32
        triangles=np.frombuffer(raw,'<u4',t*3,offset).reshape(t,3);offset+=t*12
        if part==2:return vertices[:,:3].copy(),triangles.copy()
    raise ValueError('Screwdriver mesh missing from prepared props')


def handle_surface(points,mesh):
    vertices,triangles=mesh
    radius=np.linalg.norm(points[:,:2],axis=1)
    direction=np.column_stack([points[:,:2]/np.maximum(radius[:,None],1e-9),np.zeros(len(points))])
    origin=points.copy();origin[:,:2]=0
    a=vertices[triangles[:,0]];e1=vertices[triangles[:,1]]-a;e2=vertices[triangles[:,2]]-a
    h=np.cross(direction[:,None,:],e2[None,:,:]);det=np.sum(e1[None,:,:]*h,axis=2)
    f=np.divide(1,det,out=np.zeros_like(det),where=abs(det)>1e-9)
    delta=origin[:,None,:]-a[None,:,:];u=f*np.sum(delta*h,axis=2)
    q=np.cross(delta,e1[None,:,:]);v=f*np.sum(direction[:,None,:]*q,axis=2)
    distance=f*np.sum(e2[None,:,:]*q,axis=2)
    hit=(abs(det)>1e-9)&(u>=-1e-6)&(v>=-1e-6)&(u+v<=1.000001)&(distance>0)
    return np.max(np.where(hit,distance,0),axis=1)


def finger_solver(original,prefix):
    def finger_pose(finger, angles, spread=0, roll=0):
        parent_name = prefix + 'Hand'
        parent_new = original[parent_name]
        chain = {}
        for j, suffix in enumerate(['', '1', '2']):
            name = prefix + finger + suffix
            if name not in original:
                continue
            local = original[name] @ np.linalg.inv(original[parent_name])
            angle = np.radians(angles[j]); c, sn = np.cos(angle), np.sin(angle)
            rotation = np.eye(4); rotation[:2,:2] = [[c,sn],[-sn,c]]
            if j == 0 and spread:
                a = np.radians(spread); c, sn = np.cos(a), np.sin(a)
                swing = np.eye(4); swing[0,0]=swing[2,2]=c
                swing[0,2]=-sn; swing[2,0]=sn
                rotation = swing @ rotation
            if j == 0 and roll:
                a=np.radians(roll);c,sn=np.cos(a),np.sin(a)
                twist=np.eye(4);twist[1:3,1:3]=[[c,sn],[-sn,c]]
                rotation=twist@rotation
            bent = rotation @ local @ parent_new
            chain[name] = bent
            parent_name, parent_new = name, bent
        return chain
    return finger_pose


def fit_pinch(original,prefix):
    finger_pose=finger_solver(original,prefix)
    inverse_hand=np.linalg.inv(original[prefix + "Hand"])
    seed = np.array([20.,45.,35.,30.,35.,25.,0.])
    def pinch_pose(values):
        chains = {}
        for f in ['Finger2','Finger3','Finger4']:
            chains.update(finger_pose(f,[35,50,35]))
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
    pinch_pose_data,tips=pinch_pose(values)
    return pinch_pose_data,tips,values


def fit_driver_grip(original, prefix, side):
    finger_pose = finger_solver(original,prefix)
    inverse_hand=np.linalg.inv(original[prefix + "Hand"])
    # Keep the approved wrist/shaft direction. Fit the fingers behind and
    # around the raised handle, leaving its upper front groove to the thumb.
    sign = 1 if side == 0 else -1
    axis = np.array([0., .95, .31225]); axis /= np.linalg.norm(axis)
    center = np.array([sign*.022, -.008, -.025])-axis*.026+np.array([-sign*.004,-axis[2]*.016,axis[1]*.016])
    def mapped(points):
        points=(np.asarray(points)-[5.5,1.4,0])*.0125
        return np.column_stack([points[:,1]*sign,-points[:,0],points[:,2]*sign])
    closed_pose={}
    fingers=['Finger1','Finger2','Finger3','Finger4']
    # Fit the whole distal segment, not the former 1.1-unit proxy (roughly
    # half the actual fingertip length in this rig).
    extents=dict(Finger1=2.30,Finger2=2.22,Finger3=2.21,Finger4=1.90)
    back_axis=np.array([0,-axis[2],axis[1]])
    def finger_sample(finger,values):
        chain=finger_pose(finger,values[:3],values[3],values[4])
        points=[(chain[prefix+finger+suffix] @ inverse_hand)[3,:3] for suffix in ['', '1','2']]
        points.append((np.array([extents[finger],0,0,1])@chain[prefix+finger+'2']@inverse_hand)[:3])
        points=mapped(points)
        fractions=np.linspace(0,1,6)
        sections=np.concatenate([points[i]+fractions[:,None]*(points[i+1]-points[i]) for i in range(3)])
        radii=np.concatenate([np.linspace(a,b,6) for a,b in [(.009,.009),(.009,.008),(.008,.003)]])
        return chain,points,sections,radii
    seeds={f:np.array([45.,60.,35.,0.,-sign*35]) for f in fingers}
    solutions={f:seeds[f].copy() for f in fingers}
    samples={f:finger_sample(f,solutions[f]) for f in fingers}
    for iteration in range(3):
      for finger in fingers:
        seed=seeds[finger]
        other=np.concatenate([samples[f][2] for f in fingers if f!=finger])
        other_radius=np.concatenate([samples[f][3] for f in fingers if f!=finger])
        def cost(values):
            chain,points,sections,radii=finger_sample(finger,values)
            delta=sections-center;along=np.clip(delta@axis,-.035,.035)
            radius=np.linalg.norm(delta-along[:,None]*axis,axis=1)
            tip=points[-1]-center;t=float(tip@axis);radial=tip-axis*np.clip(t,-.035,.035)
            penetration=np.maximum(0,.014+radii-radius)
            side_target=np.array([sign*.017,0,0])+back_axis*.005
            contact=np.sum((radial-side_target)**2)
            score=140*np.sum(penetration**2)+12*contact+.00000005*np.sum((values-seed)**2)
            station={'Finger1':-.005,'Finger2':.002,'Finger3':.018,'Finger4':.030}[finger]
            score+=30*(t-station)**2
            # Keep finger bones outside the palm and apart from other digits.
            score+=100*np.sum(np.maximum(0,.012-sign*points[1:3,0])**2)
            back=(points[1:3]-center)@back_axis
            score+=200*np.sum(np.maximum(0,.012-back)**2)
            score+=120*np.sum(np.maximum(0,-.026-delta@axis)**2)
            score+=.000001*(values[2]-.65*values[1])**2
            separation=np.linalg.norm(sections[:,None,:]-other[None,:,:],axis=2)
            overlap=np.maximum(0,radii[:,None]+other_radius[None,:]-.001-separation)
            score+=60*np.sum(overlap**2)
            return score
        values=solutions[finger].copy();limits=[(-10,95),(0,110),(0,75),(-55,55),(-110,110)]
        for step in [15,7,3,1,.3]:
          for _ in range(45):
            changed=False
            for j,(lo,hi) in enumerate(limits):
              best=cost(values);chosen=values.copy()
              for direction in [-1,1]:
                trial=values.copy();trial[j]=np.clip(trial[j]+direction*step,lo,hi);score=cost(trial)
                if score<best:best=score;chosen=trial;changed=True
              values=chosen
            if not changed:break
        solutions[finger]=values;samples[finger]=finger_sample(finger,values)
        print('Finger fit',iteration,side,finger,np.round(values,1).tolist(),flush=True)
    for f in fingers:closed_pose.update(samples[f][0])
    # Place the thumb pad over the handle's front groove, not beside it.
    # Preserve joint lengths and use a small distal bend for pad contact.
    chain=finger_pose('Thumb1',[0,0,15])
    conversion=np.array([[0,-.0125,0,0],[sign*.0125,0,0,0],[0,0,sign*.0125,0],[-1.4*.0125*sign,5.5*.0125,0,1]])
    transform=inverse_hand@conversion;unmap=np.linalg.inv(transform)
    pose={n:m@transform for n,m in chain.items()}
    n0,n1,n2=[prefix+'Thumb1'+suffix for suffix in ['', '1','2']]
    p0,p1=pose[n0][3,:3].copy(),pose[n1][3,:3].copy()
    length=np.linalg.norm(p1-p0)
    line=center+np.array([0,axis[2]*.055,-axis[1]*.055]);offset=line-p0
    projection=float(offset@axis);discriminant=projection**2-float(offset@offset)+length**2
    assert discriminant>0,'Thumb cannot reach handle without changing bone length'
    candidates=[-projection-np.sqrt(discriminant),-projection+np.sqrt(discriminant)]
    station=min(candidates,key=lambda t:abs(t-.035));destination=line+axis*station
    def rotate_chain(names,pivot,from_vector,to_vector):
        source=from_vector/np.linalg.norm(from_vector);target=to_vector/np.linalg.norm(to_vector)
        cross=np.cross(source,target);c=float(source@target)
        skew=np.array([[0,-cross[2],cross[1]],[cross[2],0,-cross[0]],[-cross[1],cross[0],0]])
        rotation=(np.eye(3)+skew+skew@skew/(1+c)).T
        matrix=np.eye(4);matrix[:3,:3]=rotation;matrix[3,:3]=pivot-pivot@rotation
        for name in names:pose[name]=pose[name]@matrix
    rotate_chain([n0,n1,n2],p0,p1-p0,destination-p0)
    p1=pose[n1][3,:3].copy();tip=(np.array([2.30,0,0,1])@pose[n2])[:3]
    thumb_pad=center-axis*.025+np.array([0,axis[2]*.004,-axis[1]*.004])
    rotate_chain([n1,n2],p1,tip-p1,thumb_pad-p1)
    for name,m in pose.items():closed_pose[name]=m@unmap
    print('Thumb support',side,'axis station',station,'joint lengths preserved')
    return closed_pose, axis, center


def hand_asset(path, side, driver):
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
    def apply_pose(chains):
        for name, m in original.items():
            bones[name].set_transform(nif_matrix(chains.get(name,m)))
    closed_pose,axis,center=fit_driver_grip(original,prefix,side)
    apply_pose(closed_pose)
    closed,closed_normals=vertices()
    # Correct small residual skin penetrations against the actual original
    # handle surface (including its groove), not an oversized cylinder.
    basis=np.column_stack([[1.,0,0],np.cross(axis,[1.,0,0]),axis])
    local=(closed-center)@basis+np.array([0,0,.147])
    radius=np.linalg.norm(local[:,:2],axis=1);surface=handle_surface(local,driver)
    contact=(local[:,2]>.112)&(local[:,2]<.182)&(radius<surface+.0003)
    local[contact,:2]*=((surface[contact]+.0003)/np.maximum(radius[contact],1e-9))[:,None]
    closed[contact]=(local[contact]-[0,0,.147])@basis.T+center
    triangles=np.array(list(geom.data.get_triangles()),dtype='<u4')
    affected=np.unique(triangles[np.any(contact[triangles],axis=1)])
    face=np.cross(closed[triangles[:,1]]-closed[triangles[:,0]],closed[triangles[:,2]]-closed[triangles[:,0]])
    normals=np.zeros_like(closed)
    for corner in range(3):np.add.at(normals,triangles[:,corner],face)
    lengths=np.linalg.norm(normals,axis=1);normals/=np.maximum(lengths[:,None],1e-9)
    normals[np.sum(normals*closed_normals,axis=1)<0]*=-1
    valid=affected[lengths[affected]>1e-10];closed_normals[valid]=normals[valid]
    print('Handle surface corrections',side,int(contact.sum()))
    quat=np.r_[np.cross([0,0,1],axis),1+axis[2]];quat/=np.linalg.norm(quat)
    driver_grip=np.r_[quat,center].astype('<f4')
    # Fit the distal index/thumb pads together in this installed hand rig.
    # Only the baked visual pose is fitted; no runtime finger tracking assumed.
    pinch_pose_data,tips,values=fit_pinch(original,prefix)
    apply_pose(pinch_pose_data)
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
    ap.add_argument('--props', type=Path, help='Prepared props.bin; defaults to OUTPUT/props.bin')
    args = ap.parse_args()
    driver=driver_mesh(args.props or args.output/'props.bin')
    meshes = [hand_asset(args.mod / 'meshes/characters/_male' / (side + 'hand.nif'), n, driver)
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
        animation='baked open, thumb on front handle groove with rear finger wrap, preserved thumb/index pinch',
        grip_revision='thumb-contact-v35d',
        pinch_anchors=[m[3].tolist() for m in meshes],
        driver_grips=[m[4].tolist() for m in meshes],
        redistribution='Local derived game assets: do not include in addon releases'), indent=2) + '\n')
    print('Prepared local hand meshes:', [(len(v), len(t)) for v, t, _, _, _ in meshes], 'texture:', im.size)


if __name__ == '__main__':
    main()
