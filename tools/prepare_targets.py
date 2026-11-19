#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-only
"""Prepare PRIVATE target models from a user's game and active mod setup.
Archives must be supplied in winning-first order. Enabled modlist entries are
also winning-first (MO2/Fluorine on-disk order); loose files precede archives.
No game data is shipped with this tool. Requires the prepare_hands dependencies.
"""
import argparse,hashlib,io,json,math,struct
from pathlib import Path
from prepare_hands import NifFormat,bsa_file,matrix,np,Image

def scalar(group,t):
    keys=list(group.keys)
    if not keys:raise ValueError('Missing rotation track')
    if t<=keys[0].time:return keys[0].value
    if t>=keys[-1].time:return keys[-1].value
    for a,b in zip(keys,keys[1:]):
        if a.time<=t<=b.time:
            u=(t-a.time)/(b.time-a.time)
            if group.interpolation==1:return a.value+(b.value-a.value)*u
            if group.interpolation==2:
                return (2*u**3-3*u*u+1)*a.value+(u**3-2*u*u+u)*a.forward+(-2*u**3+3*u*u)*b.value+(u**3-u*u)*b.backward
            raise ValueError('Unsupported animation interpolation')

def main():
    ap=argparse.ArgumentParser(description=__doc__)
    ap.add_argument('--data',type=Path,required=True)
    ap.add_argument('--modlist',type=Path);ap.add_argument('--mods',type=Path)
    ap.add_argument('--archive',type=Path,action='append',required=True)
    ap.add_argument('--output',type=Path,required=True)
    ap.add_argument('--profiles',type=Path,default=Path(__file__).with_name('target_profiles.json'))
    a=ap.parse_args();roots=[]
    if a.modlist:
        if not a.mods:ap.error('--modlist needs --mods')
        roots=[a.mods/line[1:] for line in a.modlist.read_text().splitlines() if line.startswith('+')]
    roots.append(a.data);provenance={};listing={}
    def loose(root,name):
        for part in name.replace('\\','/').split('/'):
            if part in ('','..','.'):raise ValueError('Unsafe asset path')
            if root not in listing:
                listing[root]={p.name.lower():p for p in root.iterdir()} if root.is_dir() else {}
            root=listing[root].get(part.lower(),root/part)
        return root
    def read(name):
        for root in roots:
            path=loose(root,name)
            if path.is_file():raw=path.read_bytes();source=str(path);break
        else:
            for path in a.archive:
                raw=bsa_file(path,name.replace('/','\\'))
                if raw is not None:source=str(path)+':'+name;break
            else:raise FileNotFoundError(name)
        provenance[name]=dict(source=source,sha256=hashlib.sha256(raw).hexdigest());return raw
    a.output.mkdir(parents=True,exist_ok=True)
    results=[]
    for profile in json.loads(a.profiles.read_text()):
        kind=profile['kind'];d=NifFormat.Data();d.read(io.BytesIO(read('meshes/'+profile['model'])))
        static = profile.get('static_scene', False)
        basis=np.array(profile['basis']) if 'basis' in profile else np.eye(3)[:,profile['axes']]*np.array(profile.get('signs',[1,1,1]))
        if basis.shape!=(3,3) or not np.isfinite(basis).all() or not np.allclose(basis.T@basis,np.eye(3),atol=1e-5) or np.linalg.det(basis)<.99:raise ValueError('Reflection is not a valid orientation')
        anchor=np.array(profile['anchor']);scale=profile['scale'];offset=np.array([0,0,-.013])
        def point(v):return (np.asarray(v)-anchor)@basis*scale+offset
        node=None;hinge=np.zeros(3);direction=np.array([0.,1.,0.]);duration=1.;degrees=0.
        frames=[[0,0,0,1,0,0,0]]*2
        if not static:
            seq=next(b for b in d.blocks if isinstance(b,NifFormat.NiControllerSequence) and b.name==b'Open')
            if len(seq.controlled_blocks)!=1:raise ValueError('Only single-hinge models supported')
            cb=seq.controlled_blocks[0];node=next(b for b in d.blocks if isinstance(b,NifFormat.NiNode) and b.name==cb.node_name)
            track=cb.interpolator.data
            if track.rotation_type!=4:raise ValueError('Expected XYZ hinge track')
            if track.translations.num_keys or any(abs(k.value-1)>1e-6 for k in track.scales.keys):raise ValueError('Translating/scaling tracks need a separate importer')
            moving=[i for i,g in enumerate(track.xyz_rotations) if abs(g.keys[-1].value-g.keys[0].value)>1e-5]
            if len(moving)!=1:raise ValueError('Expected one moving hinge axis')
            axis=moving[0];start=float(seq.start_time);duration=float(seq.stop_time-start)
            nw=matrix(node.get_transform(d.roots[0]));hinge=point(nw[3,:3]);direction=nw[axis,:3]@basis;direction/=np.linalg.norm(direction)
            frames=[];samples=max(2,int(math.ceil(duration*120))+1)
            for t in np.linspace(start,start+duration,samples):
                angle=scalar(track.xyz_rotations[axis],t)-scalar(track.xyz_rotations[axis],start)
                xyz=direction*math.sin(angle/2);w=math.cos(angle/2)
                rotated=hinge+2*np.cross(xyz,np.cross(xyz,hinge)+w*hinge)
                frames.append([*xyz,w,*(hinge-rotated)])
            degrees=float((scalar(track.xyz_rotations[axis],start+duration)-scalar(track.xyz_rotations[axis],start))*180/math.pi)
        parts=[];materials=[];allpoints=[];skipped=[]
        for g in d.blocks:
            if not isinstance(g,NifFormat.NiTriBasedGeom) or 'editormarker' in g.name.decode(errors='ignore').lower():continue
            prop=next((p for p in g.properties if hasattr(p,'texture_set') and p.texture_set),None)
            if prop is None:
                skipped.append(dict(mesh=g.name.decode(),reason='untextured/no-lighting decoration'))
                continue
            if g.skin_instance:raise ValueError('Skinned targets are not supported')
            data=g.data;transform=matrix(g.get_transform(d.roots[0]));verts=np.array([v.as_list() for v in data.vertices])
            pts=point((np.column_stack([verts,np.ones(len(verts))])@transform)[:,:3]);allpoints.extend(pts.tolist())
            normals=np.array([v.as_list() for v in data.normals])@np.linalg.inv(transform[:3,:3]).T@basis
            normals/=np.maximum(np.linalg.norm(normals,axis=1)[:,None],1e-12)
            uv=np.array([[v.u,v.v] for v in data.uv_sets[0]])
            textures=[p.decode().replace('\\','/') for p in list(prop.texture_set.textures)[:2]]
            if textures not in materials:materials.append(textures)
            alpha=any(isinstance(p,NifFormat.NiAlphaProperty) and p.flags&1 for p in g.properties)
            part=dict(mesh=g.name.decode(),moving=node is not None and node in d.roots[0].find_chain(g),material=materials.index(textures),alpha=alpha)
            vertices=np.column_stack([pts,normals,uv]).astype('<f4');tri=np.array(list(data.get_triangles()),dtype='<u4')
            parts.append((part,vertices,tri))
        if len(materials)>8:raise ValueError('Too many target materials')
        for m,pair in enumerate(materials):
            for channel,name in enumerate(pair):
                image=Image.open(io.BytesIO(read(name))).convert('RGBA')
                if max(image.size)>4096:image.thumbnail((4096,4096))
                suffix='-n' if channel else ''
                (a.output/f'target-{kind}-{m}{suffix}.rgba').write_bytes(struct.pack('<II',*image.size)+image.tobytes())
        pts=np.array(allpoints);low=pts.min(0);mesh_high=pts.max(0);high=mesh_high.copy()
        # A door knob may project in front of the keyhole. Keep its visible
        # geometry but use the calibrated door face for the simple body hull;
        # otherwise its global AABB would block tools in empty space.
        front=float(profile.get('collision_front',high[2]))
        if not math.isfinite(front) or front>-.005 or front>mesh_high[2]+1e-6 or front<=low[2]:
            raise ValueError(f'Invalid collision front/anchor: {front}')
        high[2]=front
        with (a.output/f'target-{kind}.bin').open('wb') as f:
            style=profile.get('lock_style',0)
            if style not in (0,1,2):raise ValueError('Unsupported lock style/model pairing')
            f.write(struct.pack('<8s4IfI',b'TTWTarg2',kind,len(parts),len(materials),len(frames),duration,style))
            f.write(np.r_[low,high].astype('<f4').tobytes());f.write(np.array(frames,dtype='<f4').tobytes())
            for p,v,t in parts:
                f.write(struct.pack('<5I',len(v),len(t),p['material'],p['moving'],p['alpha']));f.write(v.tobytes());f.write(t.tobytes())
        result=dict(**profile,triangles=sum(len(t) for _,_,t in parts),parts=[p for p,_,_ in parts],skipped=skipped,materials=materials,duration=duration,hinge=hinge.tolist(),hinge_axis=direction.tolist(),open_degrees=degrees,bounds=[low.tolist(),high.tolist()],mesh_bounds=[low.tolist(),mesh_high.tolist()])
        results.append(result);print('Prepared',profile['model'],result['triangles'],'triangles',result['open_degrees'],'degrees')
    (a.output/'targets-source.json').write_text(json.dumps(dict(models=results,source_files=provenance),indent=2)+'\n')
if __name__=='__main__':main()
