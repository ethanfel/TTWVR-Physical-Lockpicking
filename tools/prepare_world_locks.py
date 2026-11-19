#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-only
"""Prepare private, textured world hardware from the local hardware export."""
import argparse
import hashlib
import io
import json
import struct
from pathlib import Path

import numpy as np

from mount_surfaces import mount_basis
from prepare_hands import NifFormat as N, bsa_file
from prepare_native_scene import texture_aliases


def read_parts(path):
    raw = path.read_bytes()
    if raw[:8] != b'TTWHard1':
        raise ValueError(f'Invalid hardware export {path}')
    offset = 12
    parts = []
    for _ in range(struct.unpack_from('<I', raw, 8)[0]):
        nv, nt, material = struct.unpack_from('<3I', raw, offset)
        offset += 12
        vertices = np.frombuffer(raw, '<f4', nv*8, offset).reshape(nv, 8).copy()
        offset += nv*32
        triangles = np.frombuffer(raw, '<u4', nt*3, offset).reshape(nt, 3).copy()
        offset += nt*12
        parts.append((vertices, triangles, material))
    if offset != len(raw):
        raise ValueError('Incomplete hardware export')
    return parts


def extend_round_rim(vertices, triangles):
    """Seat the original textured sidewall in the support, including UV seam vertices."""
    positions, ids = np.unique(vertices[:, :3].round(6), axis=0, return_inverse=True)
    faces = ids[triangles]
    edges = np.sort(np.vstack((faces[:, [0, 1]], faces[:, [1, 2]], faces[:, [2, 0]])), axis=1)
    edges, counts = np.unique(edges, axis=0, return_counts=True)
    rear = edges[(counts == 1) & (positions[edges, 2] < 0).all(axis=1)]
    ring, degree = np.unique(rear, return_counts=True)
    if len(ring) < 8 or np.any(degree != 2):
        raise ValueError('Expected a closed outer rim on the round frame')
    # The rear edge only meets the cylindrical wall. Extending that wall keeps
    # its authored rim UVs and tangent direction continuous down to the door.
    fitted = vertices.copy()
    fitted[np.isin(ids, ring), 2] = -.020
    return fitted, triangles


def mounted_parts(folder, style):
    parts = read_parts(folder / f'hardware-{style}.bin')
    out = []
    for i,(v,t,material) in enumerate(parts):
        if i == 0 and style == 0:
            v,t = extend_round_rim(v,t)
        elif i == 2 and style == 1:
            # The rear arc of the fixed staple penetrates the mounting surface.
            back = v[:,2] < 0
            v[back,2] *= 2
            v[back,5] *= .5
            v[back,3:6] /= np.linalg.norm(v[back,3:6],axis=1)[:,None]
        elif i == 0 and style == 2:
            v[v[:,2] < -.0119,2] = -.020
        # The game culls back faces. Some procedural wall triangles in the
        # padlock bore are wound opposite to the authored outward normals.
        q=v[t]
        face=np.cross(q[:,1,:3]-q[:,0,:3], q[:,2,:3]-q[:,0,:3])
        flipped=(face*q[:,:,3:6].mean(axis=1)).sum(axis=1)<0
        t[flipped] = t[flipped][:,[0,2,1]]
        out.append((v,t,material))
    return out


def make_shape(v, triangles, material_id, shade, aliases, shader, material, name):
    g = N.NiTriShape()
    g.name, g.flags = name.encode(), 14
    x = N.NiTriShapeData()
    g.data = x
    x.num_vertices = len(v)
    x.has_vertices = x.has_normals = x.has_vertex_colors = True
    x.num_uv_sets = 1
    for array in (x.vertices,x.normals,x.vertex_colors,x.uv_sets):
        array.update_size()
    x.consistency_flags = N.ConsistencyType.CTSTATIC
    for i,(px,py,pz,nx,ny,nz,u,w) in enumerate(v):
        x.vertices[i].x,x.vertices[i].y,x.vertices[i].z = px,py,pz
        x.normals[i].x,x.normals[i].y,x.normals[i].z = nx,ny,nz
        x.uv_sets[0][i].u,x.uv_sets[0][i].v = u,w
        c=x.vertex_colors[i]
        c.r=c.g=c.b=float(shade[i])
        c.a=1
    x.set_triangles(triangles.tolist())
    x.update_center_radius()
    g.update_tangent_space()
    sh=N.BSShaderPPLightingProperty()
    sh.shader_type=shader.shader_type
    sh.shader_flags.deepcopy(shader.shader_flags)
    sh.shader_flags_2.deepcopy(shader.shader_flags_2)
    sh.texture_set=N.BSShaderTextureSet()
    sh.texture_set.num_textures=6
    sh.texture_set.textures.update_size()
    stem=['props-0','target-2-0','target-1-0'][material_id]
    for j in range(6):
        sh.texture_set.textures[j]=b''
    for j,key in enumerate((stem,stem+'-n')):
        sh.texture_set.textures[j]=('textures\\TTWVRPhysicalLockpicking\\'+aliases.get(key,key)+'.dds').encode()
    sh.shader_flags.sf_skinned=0
    sh.shader_flags.sf_environment_mapping=0
    sh.shader_flags_2.sf_2_vertex_colors=1
    m=N.NiMaterialProperty()
    m.glossiness=material.glossiness
    m.ambient_color.r=m.ambient_color.g=m.ambient_color.b=1
    m.diffuse_color.r=m.diffuse_color.g=m.diffuse_color.b=1
    m.alpha=1
    g.add_property(m)
    g.add_property(sh)
    return g


def main():
    ap=argparse.ArgumentParser(description=__doc__)
    ap.add_argument('--hardware',type=Path,required=True)
    ap.add_argument('--data',type=Path,required=True)
    ap.add_argument('--output',type=Path,required=True)
    ap.add_argument('--profiles',type=Path,default=Path(__file__).with_name('target_profiles.json'))
    ap.add_argument('--kind',type=int,action='append')
    ap.add_argument('--texture-dir',type=Path, help='Prepared native texture directory when staging meshes separately')
    a=ap.parse_args()
    textures=a.texture_dir or a.output/'textures/TTWVRPhysicalLockpicking'
    aliases=texture_aliases(textures)
    source=N.Data()
    source.read(io.BytesIO(bsa_file(a.data/'Fallout - Meshes.bsa',r'meshes\terminals\lockinterface01.nif')))
    example=next(b for b in source.blocks if getattr(b,'name',None)==b'lock:0')
    shader=next(p for p in example.properties if isinstance(p,N.BSShaderPPLightingProperty))
    material=next(p for p in example.properties if isinstance(p,N.NiMaterialProperty))
    folder=a.output/'meshes/TTWVRPhysicalLockpicking'
    folder.mkdir(parents=True,exist_ok=True)
    hardware={i:mounted_parts(a.hardware,i) for i in range(3)}
    report=[]
    for profile in json.loads(a.profiles.read_text()):
        kind,style=profile['kind'],profile.get('lock_style',0)
        if a.kind and kind not in a.kind:continue
        basis = mount_basis(profile)
        placements=[('front',profile['anchor'],basis)]
        if 'reverse_anchor' in profile:
            placements.append(('reverse',profile['reverse_anchor'],basis@np.diag([-1,1,-1])))
        d=N.Data(version=source.version,user_version=source.user_version,user_version_2=source.user_version_2)
        d.header.endian_type=1
        root=N.NiNode()
        root.name,root.flags=f'TTWVRWorldLock_{kind}'.encode(),14
        d.roots=[root]
        for label,anchor,orientation in placements:
            for i,(local,tri,mat) in enumerate(hardware[style]):
                v=local.copy()
                v[:,:3]=((local[:,:3]-[0,0,-.013])/profile['scale'])@orientation.T+anchor
                v[:,3:6]=local[:,3:6]@orientation.T
                shade=np.ones(len(v))
                if i>=len(hardware[style])-2:
                    cavity=np.clip((local[:,2]+.012)/.036,0,1)
                    shade=(.035+.965*cavity*cavity)*(.55 if style==2 else 1.)
                if mat==2:shade*=.85
                root.add_child(make_shape(v,tri,mat,shade,aliases,shader,material,
                                          f'TTWVRLockHardware_{label}_{i}'))
        path=folder/f'world-lock-{kind}.nif'
        tmp=path.with_suffix('.nif.tmp')
        with tmp.open('wb') as f:d.write(f)
        tmp.replace(path)
        report.append(dict(kind=kind,model=profile['model'],style=style,faces=[x[0] for x in placements],
                           path=str(path.relative_to(a.output)),sha256=hashlib.sha256(path.read_bytes()).hexdigest()))
    manifest=a.output/'world-lock-files.json'
    if a.kind and manifest.is_file():
        saved={row['kind']:row for row in json.loads(manifest.read_text())}
        saved.update((row['kind'],row) for row in report)
        rows=[saved[k] for k in sorted(saved)]
    else:
        rows=report
    tmp=manifest.with_suffix('.json.tmp')
    tmp.write_text(json.dumps(rows,indent=2)+'\n')
    tmp.replace(manifest)
    print('Prepared',len(report),'private world mounts')

if __name__=='__main__':main()
