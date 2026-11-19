#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-only
"""Make a Data-root player ZIP from a prepared stock TTW installation."""
import argparse
import io
import json
from pathlib import Path
import re
import subprocess
import sys
import tempfile
from zipfile import ZIP_DEFLATED, ZipFile

ROOT = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(ROOT / 'tools'))
from prepare_native_scene import N, game_materials
from shader_delta import build as shader_delta


def patch_hardware(path, materials):
    """The kit and world lock meshes each have a handful of texture sets."""
    data = N.Data()
    with path.open('rb') as f:
        data.read(f)
    for block in data.blocks:
        if not isinstance(block, N.BSShaderTextureSet):
            continue
        for i, entry in enumerate(block.textures):
            value = bytes(entry).decode().replace(chr(92), '/')
            if value.lower().startswith('textures/ttwvrphysicallockpicking/'):
                block.textures[i] = materials[Path(value).stem].encode()
    output = io.BytesIO()
    data.write(output)
    return output.getvalue()


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--prepared', type=Path, required=True)
    parser.add_argument('--data', type=Path, required=True)
    parser.add_argument('--dll', type=Path, required=True)
    parser.add_argument('--output', type=Path, required=True)
    args = parser.parse_args()
    base = args.prepared
    assets = base/'NVSE/Plugins/TTWVRPhysicalLockpicking'
    meshes = Path('meshes/TTWVRPhysicalLockpicking')
    textures = Path('textures/TTWVRPhysicalLockpicking')
    materials = game_materials(assets)
    profiles = json.loads((ROOT/'tools/target_profiles.json').read_text())
    kinds = {row['kind'] for row in profiles}
    if args.output.exists():
        parser.error('Choose a new output ZIP')
    with tempfile.TemporaryDirectory(dir=args.output.parent) as tmp:
        tmp = Path(tmp)
        subprocess.run([sys.executable, '-B', str(ROOT/'tools/prepare_native_scene.py'),
                        '--data', str(args.data), '--assets', str(assets), '--output', str(tmp),
                        '--game-materials'], check=True)
        archive = tmp/'player.zip'
        with ZipFile(archive, 'x', ZIP_DEFLATED, compresslevel=6) as z:
            def add(path, name=None):
                if not path.is_file() or not path.stat().st_size:
                    raise ValueError(f'Missing payload: {path}')
                z.write(path, name or path.relative_to(base))
            add(args.dll, 'NVSE/Plugins/TTWVRPhysicalLockpicking.dll')
            add(ROOT/'TTWVRPhysicalLockpicking.ini','NVSE/Plugins/TTWVRPhysicalLockpicking.ini')
            # Override stale opt-in manifest entries when upgrading an old install.
            manifest = 'XR_APILAYER_TTWVR_physical_lockpicking.json'
            add(ROOT/'manifest'/manifest,(assets/manifest).relative_to(base))
            add(base/'TTWVRLockKit.esp')
            for script in sorted((ROOT/'kit').rglob('*.txt')):
                add(script,script.relative_to(ROOT/'kit'))
            packages = sorted((base/'Shaders').glob('*.sdp'))
            if not packages:
                raise ValueError('Missing stock motion shader build inputs')
            for path in packages:
                source = args.data/'Shaders'/path.name
                delta = shader_delta(source.read_bytes(),path.read_bytes())
                z.writestr((assets/'motion'/(path.name+'.delta')).relative_to(base).as_posix(),delta)
            for name in ['hands','props','kit-pinch','kit-world','kit-surfaces',*[f'target-{k}' for k in sorted(kinds)]]:
                add(assets/(name+'.bin'))
            for k in sorted({0,*kinds}):
                add(tmp/meshes/f'workspace-{k}.nif', meshes/f'workspace-{k}.nif')
            for name in [*[f'kit-{h}-{t}' for h in (0,1) for t in (0,1)],*[f'world-lock-{k}' for k in sorted(kinds)]]:
                path = meshes/(name+'.nif')
                z.writestr(path.as_posix(),patch_hardware(base/path, materials))
            for name in ('pin-metal','pin-metal-n','flat-n','white'):
                add(base/textures/(name+'.dds'))
            add(ROOT/'LICENSE', 'docs/TTWVRPhysicalLockpicking/LICENSE.txt')
        with ZipFile(archive) as z:
            files = set(z.namelist())
            # Validate every private texture reference from the final NIF bytes.
            private = re.compile(rb'textures[\\\\/]TTWVRPhysicalLockpicking[\\\\/]([\w.-]+\.dds)', re.I)
            for name in files:
                if name.endswith('.nif'):
                    for texture in private.findall(z.read(name)):
                        if (textures/texture.decode()).as_posix() not in files:
                            raise ValueError(f'{name}: missing material {texture!r}')
            if z.testzip() or len(files) != len(z.infolist()):
                raise ValueError('Invalid player ZIP')
        archive.rename(args.output)


if __name__ == '__main__':
    main()
