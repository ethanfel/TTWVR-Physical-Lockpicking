#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-only
"""Prepare private native scene buffers/materials from locally prepared assets.
Never redistribute the generated game-derived NIFs or textures.
"""
import argparse, io, struct, json, os, hashlib
from pathlib import Path
from prepare_hands import NifFormat as N, bsa_file, Image

def dds(path, image):
    levels=[image.convert('RGBA')]
    while max(levels[-1].size)>1:
        w,h=levels[-1].size
        levels.append(levels[-1].resize((max(1,w//2),max(1,h//2)),Image.Resampling.LANCZOS))
    w,h=levels[0].size
    header=[124,0x2100f,h,w,w*4,0,len(levels)]+[0]*11
    header += [32,0x41,0,32,0xff,0xff00,0xff0000,0xff000000,0x401008,0,0,0,0]
    path.write_bytes(b'DDS '+struct.pack('<31I',*header)+b''.join(x.tobytes() for x in levels))

def texture_aliases(textures):
    """Identical atlas pixels must share an engine texture-cache key.

    Keep the prepared filenames for legacy users, but have native materials
    point to one deterministic representative of each complete DDS payload.
    """
    canonical={};aliases={}
    for path in sorted(textures.glob('*.dds')):
        digest=hashlib.sha256(path.read_bytes()).digest()
        aliases[path.stem]=canonical.setdefault(digest,path.stem)
    return aliases

def rebind_template(path, aliases):
    """Update only texture paths in an already prepared native template."""
    data=N.Data()
    with path.open('rb') as f:data.read(f)
    changed=0
    for block in data.blocks:
        if not isinstance(block,N.BSShaderTextureSet):continue
        for i,value in enumerate(block.textures):
            value=bytes(value)
            if not value:continue
            parts=[part for part in value.decode().replace(chr(92),'/').split('/') if part]
            if [part.lower() for part in parts[:-1]]!=['textures','ttwvrphysicallockpicking']:continue
            stem=parts[-1].removesuffix('.dds')
            replacement=chr(92).join(parts[:-1]+[aliases.get(stem,stem)+'.dds']).encode()
            if replacement!=value:block.textures[i]=replacement;changed+=1
    if changed:
        temporary=path.with_suffix('.nif.tmp')
        with temporary.open('wb') as f:data.write(f)
        os.replace(temporary,path)
    return changed

def layout(assets, kind):
    """One native transform for each rigid piece, never one for a whole material."""
    raw=(assets/'props.bin').read_bytes()
    assert raw[:8]==b'TTWProp5' and struct.unpack_from('<I',raw,8)[0]==7
    offset=12;props=[]
    for _ in range(7):
        n,t,material=struct.unpack_from('<3I',raw,offset);offset+=12+n*32+t*12
        assert 0<n<65536 and 0<t*3<65536
        props.append(t*3)
    assert offset==len(raw)
    raw=(assets/'hands.bin').read_bytes();assert raw[:8]==b'TTWHand4'
    offset=8;hands=[]
    for _ in range(2):
        n,t=struct.unpack_from('<2I',raw,offset);offset+=8+40+n*80+t*12
        hands.append(t*3)
    assert offset==len(raw)
    style=struct.unpack_from('<I',(assets/f'target-{kind}.bin').read_bytes(),28)[0] if kind else 0
    surfaces={2:(8 if style==1 else 9 if style==2 else 6,props[1]),
              3:(8 if style==1 else 9 if style==2 else 6,240*3),
              10:(3,props[5]),11:(4,props[6]),203:(2,props[2]),
              210:(1,hands[0]),211:(1,hands[1])}
    if style==1:surfaces.update({4:(30,6000),5:(30,6000),6:(30,6000)})
    elif style==2:surfaces[7]=(31,2880)
    else:surfaces[1]=(2,props[0])
    for first in [100+3*i for i in range(18)]+[200]:
        surfaces[first]=(5,props[3]);surfaces[first+1]=(5,props[4]);surfaces[first+2]=(5,576)
    if kind:
        raw=(assets/f'target-{kind}.bin').read_bytes()
        magic,k,parts,materials,frames,duration,style=struct.unpack_from('<8s4IfI',raw)
        assert magic==b'TTWTarg2' and k==kind and parts<128
        offset=32+24+frames*28
        for i in range(parts):
            n,t,material,moving,alpha=struct.unpack_from('<5I',raw,offset);offset+=20+n*32+t*12
            assert material<8 and alpha in (0,1)
            surfaces[1000+i]=((20 if alpha else 10)+material,t*3)
        assert offset==len(raw)
    result={}
    for id,(surface,count) in surfaces.items():
        assert 0<count<65536 and count%3==0
        if surface in (0,):name='white'
        elif surface==1:name='hands'
        elif surface in (2,6,8,9):name='props-0'
        elif surface in (3,4):name=f'props-{surface-2}'
        elif surface==5:name='pin-metal'
        elif surface in (30,31):name='target-2-0' if surface==30 else 'target-1-0'
        else:name=f'target-{kind}-{surface-(20 if surface>=20 else 10)}'
        result[id]=(surface,name,count)
    return result

def main():
    ap=argparse.ArgumentParser(description=__doc__)
    ap.add_argument('--assets',type=Path,required=True)
    ap.add_argument('--data',type=Path,required=True)
    ap.add_argument('--output',type=Path,required=True)
    ap.add_argument('--kind',type=int,action='append')
    ap.add_argument('--rebind-textures',action='store_true',help='Share identical textures in existing templates without rebuilding their geometry')
    a=ap.parse_args()
    meshes=a.output/'meshes/TTWVRPhysicalLockpicking';textures=a.output/'textures/TTWVRPhysicalLockpicking'
    meshes.mkdir(parents=True,exist_ok=True);textures.mkdir(parents=True,exist_ok=True)
    for p in a.assets.glob('*.rgba'):
        dest=textures/(p.stem+'.dds')
        if dest.exists() and dest.stat().st_mtime>=p.stat().st_mtime:continue
        raw=p.read_bytes();w,h=struct.unpack_from('<II',raw)
        assert len(raw)==8+w*h*4 and 0<w<=8192 and 0<h<=8192
        dds(textures/(p.stem+'.dds'),Image.frombytes('RGBA',(w,h),raw[8:]))
    dds(textures/'white.dds',Image.new('RGBA',(4,4),(255,255,255,255)))
    dds(textures/'flat-n.dds',Image.new('RGBA',(4,4),(128,128,255,0)))
    aliases=texture_aliases(textures)
    (a.output/'native-texture-aliases.json').write_text(json.dumps(aliases,indent=2)+'\n')
    if a.rebind_textures:
        paths=[meshes/f'workspace-{k}.nif' for k in a.kind] if a.kind else sorted(meshes.glob('workspace-*.nif'))
        for path in paths:print('shared texture paths',path.name,rebind_template(path,aliases),flush=True)
        provenance={str(p.relative_to(a.output)):hashlib.sha256(p.read_bytes()).hexdigest() for folder in (meshes,textures) for p in folder.iterdir() if p.suffix in ('.nif','.dds')}
        (a.output/'native-scene-files.json').write_text(json.dumps(provenance,indent=2)+'\n')
        return
    data=N.Data();data.read(io.BytesIO(bsa_file(a.data/'Fallout - Meshes.bsa',r'meshes\terminals\lockinterface01.nif')))
    original=next(b for b in data.blocks if getattr(b,'name',None)==b'lock:0')
    shader=next(p for p in original.properties if isinstance(p,N.BSShaderPPLightingProperty))
    material=next(p for p in original.properties if isinstance(p,N.NiMaterialProperty))
    common_shapes={}
    placeholder_blocks={}
    for kind in a.kind or [0]+sorted(int(p.stem.split('-')[1]) for p in a.assets.glob('target-*.bin')):
        print("preparing native template",kind,flush=True)
        materials=layout(a.assets,kind)
        d=N.Data(version=data.version,user_version=data.user_version,user_version_2=data.user_version_2)
        d.header.endian_type=1
        root=N.NiNode();root.name=b'TTWVRNativeWorkspace';root.flags=14;d.roots=[root]
        for id,(surface,name,count) in materials.items():
            key=(id,surface,name,count)
            if id<1000 and key in common_shapes:
                root.add_child(common_shapes[key]);continue
            g=N.NiTriShape();g.name=f'LP_{id}'.encode();g.flags=15
            # Each live shape needs a separate geometry block. Its initial arrays
            # depend only on capacity and mutability and contain no external links.
            x=N.NiTriShapeData();g.data=x
            mutable=id in (200,201,202,203,210,211) or (100<=id<154 and (id-100)%3==2)
            layout_key=(count,mutable)
            if layout_key not in placeholder_blocks:
                x.num_vertices=count;x.keep_flags=63;x.compress_flags=0;x.has_vertices=True;x.vertices.update_size()
                x.has_vertex_colors=True;x.vertex_colors.update_size()
                for color in x.vertex_colors:color.r=color.g=color.b=color.a=1
                x.has_normals=True;x.normals.update_size();x.extra_vectors_flags=16;x.tangents.update_size();x.bitangents.update_size()
                x.num_uv_sets=1;x.uv_sets.update_size()
                # The active pin starts hidden. Its shaft/handle are populated only on
                # first pickup, after the engine may have buffered the template.
                # Static buffers retain that tiny placeholder; all three pieces
                # must accept the later upload (not just the deforming bend).
                x.consistency_flags=N.ConsistencyType.CTMUTABLE if mutable else N.ConsistencyType.CTSTATIC
                for n in range(count):
                    x.vertices[n].x=(n%3==1)*.001;x.vertices[n].y=(n%3==2)*.001
                    x.normals[n].z=1;x.tangents[n].x=1;x.bitangents[n].y=1
                x.num_triangles=count//3;x.num_triangle_points=count;x.has_triangles=True;x.triangles.update_size()
                for n,t in enumerate(x.triangles):t.v_1=n*3;t.v_2=n*3+1;t.v_3=n*3+2
                x.radius=10
                buffer=io.BytesIO()
                x.write(buffer,d)
                placeholder_blocks[layout_key]=buffer.getvalue()
            payload=placeholder_blocks[layout_key]
            x.get_size=lambda data=None, raw=payload: len(raw)
            x.write=lambda stream,data,raw=payload: stream.write(raw)
            sh=N.BSShaderPPLightingProperty();mat=N.NiMaterialProperty()
            sh.shader_type=shader.shader_type;sh.shader_flags.deepcopy(shader.shader_flags);sh.shader_flags_2.deepcopy(shader.shader_flags_2)
            sh.texture_set=N.BSShaderTextureSet();sh.texture_set.num_textures=6;sh.texture_set.textures.update_size()
            mat.glossiness=material.glossiness;mat.alpha=1
            mat.ambient_color.r=mat.ambient_color.g=mat.ambient_color.b=1
            mat.diffuse_color.r=mat.diffuse_color.g=mat.diffuse_color.b=1
            sh.texture_set.textures[0]=f'textures\\TTWVRPhysicalLockpicking\\{aliases[name]}.dds'.encode()
            normal=name+'-n' if (textures/(name+'-n.dds')).exists() else 'flat-n'
            sh.texture_set.textures[1]=f'textures\\TTWVRPhysicalLockpicking\\{aliases[normal]}.dds'.encode()
            for n in range(2,sh.texture_set.num_textures):sh.texture_set.textures[n]=b''
            sh.shader_flags.sf_specular=surface not in (1,3)
            sh.shader_flags.sf_skinned=0;sh.shader_flags.sf_environment_mapping=0
            sh.shader_flags_2.sf_2_vertex_colors=1
            g.add_property(mat);g.add_property(sh)
            stencil=N.NiStencilProperty();stencil.flags=19840;g.add_property(stencil)
            if 20<=surface<28:
                alpha=N.NiAlphaProperty();alpha.flags=4845;alpha.threshold=12;g.add_property(alpha)
            root.add_child(g)
            if id<1000:common_shapes[key]=g
        path=meshes/f'workspace-{kind}.nif'
        temporary=path.with_suffix('.nif.tmp')
        with temporary.open('wb') as f:d.write(f)
        os.replace(temporary,path)
        (meshes/f'workspace-{kind}.json').write_text(json.dumps(materials,indent=2)+'\n')
        print('native template',kind,'vertices',sum(n for _,_,n in materials.values()),flush=True)
    provenance={str(p.relative_to(a.output)):hashlib.sha256(p.read_bytes()).hexdigest() for folder in (meshes,textures) for p in folder.iterdir() if p.suffix in ('.nif','.dds')}
    (a.output/'native-scene-files.json').write_text(json.dumps(provenance,indent=2)+'\n')
if __name__=='__main__':main()
