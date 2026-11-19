# SPDX-License-Identifier: GPL-3.0-only
"""Visible lock hardware on known, generated demo references only.
No lock-state changes, world scanning or live-unlock implementation.
"""
import json
from pathlib import Path


def write_world_scripts(output, manifest):
    root = output / 'NVSE/user_defined_functions/TTWVRLockStage'
    root.mkdir(parents=True, exist_ok=True)
    profiles = {p['model']: p for p in json.loads(
        (Path(__file__).resolve().parents[1] / 'target_profiles.json').read_text())}

    def form(value):
        return f'GetFormFromMod "{manifest["plugin"]}" "{value[-6:]}"'

    def write(name, lines):
        (root / (name + '.txt')).write_text('\n'.join(lines) + '\n')

    def dispatch(i, page, mode, actor='player'):
        return [f' if {page} == {i}',
                f'  Call (CompileScript "TTWVRLockStage/WorldGallery{i}.txt") {mode} {actor}',
                ' endif']

    for i, gallery in enumerate(manifest['galleries']):
        lines = ['int mode', 'ref activated', 'ref target', 'begin Function {mode activated}']
        for row in gallery['rows']:
            profile = profiles.get(row['model'])
            if profile is None:
                continue
            kind = profile['kind']
            path = f'TTWVRPhysicalLockpicking\\world-lock-{kind}.nif'
            node = f'TTWVRWorldLock_{kind}'
            for station in row['stations']:
                lines += [f' let target := {form(station["reference"])}',
                          ' if target',
                          '  if mode == 0 || (mode == 2 && target == activated)',
                          f'   AttachModel target 2 "{path}"',
                          '  elseif mode == 1',
                          '   if (target.GetLocked) && (target.GetDisabled == 0)',
                          f'    if eval (ModelHasBlock target "{node}") == 0',
                          f'     AttachModel target 3 "{path}"',
                          '    endif',
                          '   else',
                          f'    AttachModel target 2 "{path}"',
                          '   endif', '  endif', ' endif']
        lines.append('end')
        write(f'WorldGallery{i}', lines)

    common = ['ref cells', 'ref currentCell', 'int page',
              f'let cells := {form(manifest["cells_list"])}',
              'let currentCell := player.GetParentCell',
              'let page := ListGetFormIndex cells currentCell']
    lines = ['begin Function {}'] + common + ['int previous', 'int enabled',
             'let previous := -1',
             'if eval ModLocalDataExists "TTWVRLockStage.WorldPage"',
             ' let previous := GetModLocalData "TTWVRLockStage.WorldPage"', 'endif',
             'let enabled := GetINIFloat "Stage:WorldLocks" "TTWVRLockStage.ini"',
             'if previous != page || enabled != 1']
    for i in range(len(manifest['galleries'])):
        lines += dispatch(i, 'previous', 0)
    lines += [' SetModLocalData "TTWVRLockStage.WorldPage" (-1)', 'endif',
              'if page < 0 || enabled != 1', ' return', 'endif',
              'SetModLocalData "TTWVRLockStage.WorldPage" page',
              # In VR a cell change can leave a non-lock menu active while
              # the room is already visible. Suppress reattachment only while
              # the native lockpicking menu owns the selected reference.
              'if MenuMode 1014', ' return', 'endif']
    for i in range(len(manifest['galleries'])):
        lines += dispatch(i, 'page', 1)
    lines.append('end')
    write('WorldTick', lines)

    lines = ['ref activated', 'ref actor', 'begin Function {activated actor}',
             'if actor != player', ' return', 'endif'] + common
    for i in range(len(manifest['galleries'])):
        lines += dispatch(i, 'page', 2, 'activated')
    lines.append('end')
    write('WorldActivate', lines)

    lines = ['begin Function {}']
    for i in range(len(manifest['galleries'])):
        lines.append(f' Call (CompileScript "TTWVRLockStage/WorldGallery{i}.txt") 0 player')
    lines += [' SetModLocalData "TTWVRLockStage.WorldPage" (-1)',
              ' SetEventHandler "OnActivate" (CompileScript "TTWVRLockStage/WorldActivate.txt")',
              ' SetGameMainLoopCallback (CompileScript "TTWVRLockStage/WorldTick.txt") 1 15 3',
              'end']
    write('WorldInstall', lines)
