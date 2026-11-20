#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-only
"""Generate the bounded runtime target lookup; --check verifies tracked output."""
import argparse,json,math,re
from pathlib import Path

def generate(profiles):
    seen=set()
    for kind,p in enumerate(profiles,1):
        path=p['model']
        if p['kind']!=kind or path in seen or not re.fullmatch(r'[a-z0-9_ /.-]+\.nif',path) or '..' in path or path.startswith('/'):
            raise ValueError('Invalid/duplicate target path or non-contiguous kind')
        seen.add(path)
        if sorted(p['axes'])!=[0,1,2] or p.get('lock_style',0) not in (0,1,2):raise ValueError('Invalid mount basis/style')
        if len(p['anchor'])!=3 or not all(math.isfinite(v) for v in p['anchor']) or not 0<p['scale']<=.1:raise ValueError('Invalid mount transform')
        if 'reverse_anchor' in p:
            back = p['reverse_anchor']
            if (p.get('category','DOOR') != 'DOOR' or p.get('lock_style',0) not in (0,1)
                    or len(back) != 3 or not all(math.isfinite(v) for v in back)
                    or back == p['anchor']):
                raise ValueError('Invalid reverse door mount')
    if not 1<=len(profiles)<=128:raise ValueError('Catalogue exceeds bounded capacity')
    return '// SPDX-License-Identifier: GPL-3.0-only\n// Generated from tools/target_profiles.json by tools/generate_target_profiles.py.\n#pragma once\nnamespace locks {\nstruct TargetProfile { const char *path; unsigned style; };\ninline constexpr TargetProfile targetProfiles[]{\n'+''.join('  {'+json.dumps(p['model'].replace('/','\\'))+', '+str(p.get('lock_style',0))+'},\n' for p in profiles)+'};\ninline constexpr unsigned targetKinds = sizeof(targetProfiles)/sizeof(targetProfiles[0]);\n} // namespace locks\n'

def main():
    ap=argparse.ArgumentParser(description=__doc__);ap.add_argument('--check',action='store_true');a=ap.parse_args()
    root=Path(__file__).resolve().parents[1];text=generate(json.loads((root/'tools/target_profiles.json').read_text()));path=root/'src/TargetProfiles.h'
    if a.check:
        if path.read_text()!=text:raise SystemExit('TargetProfiles.h is stale; run this generator')
        print('Target catalogue header verified')
    else:path.write_text(text)
if __name__=='__main__':main()
