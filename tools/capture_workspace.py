#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-only
"""Capture gameplay or the VR lockpicking scene as left/right and stereo PNGs."""
import argparse
from datetime import datetime
from pathlib import Path
import sys
import time
from PIL import Image


def main():
    ap = argparse.ArgumentParser(description=__doc__)
    ap.add_argument('--plugins', type=Path, required=True,
                    help='Live game Data/NVSE/Plugins folder containing the addon DLL')
    ap.add_argument('--output', type=Path, required=True)
    ap.add_argument('--delay', type=float, default=5,
                    help='Seconds before requesting capture (default: 5)')
    ap.add_argument('--timeout', type=float, default=30,
                    help='Seconds to wait for a submitted VR frame (default: 30)')
    args = ap.parse_args()
    if not 0 <= args.delay <= 300 or not 1 <= args.timeout <= 300:
        ap.error('delay must be 0–300 and timeout 1–300 seconds')
    if not (args.plugins / 'TTWVRPhysicalLockpicking.dll').is_file():
        ap.error('Addon DLL not found in the supplied plugins folder')
    paths = [args.plugins / ('lockpick-' + eye + '.ppm') for eye in ('left', 'right')]
    before = [p.stat().st_mtime_ns if p.exists() else None for p in paths]
    request = args.plugins / 'lockpick-capture.request'
    print(f'Capture in {args.delay:g}s. Return to VR; gameplay and our lockpicking scene are both supported.', flush=True)
    time.sleep(args.delay)
    if request.exists():
        raise RuntimeError('A capture request is already pending; finish that capture first.')
    request.touch(exist_ok=False)
    deadline = time.monotonic() + args.timeout
    try:
        while time.monotonic() < deadline:
            if not request.exists() and all(p.exists() and p.stat().st_mtime_ns != old
                                            for p, old in zip(paths, before)):
                break
            time.sleep(.1)
        else:
            raise RuntimeError('No fresh stereo capture. Use addon version 24 or newer, return focus to the game, and retry.')
    finally:
        request.unlink(missing_ok=True)
    eyes = []
    for p in paths:
        with Image.open(p) as im:
            eyes.append(im.convert('RGB'))
    args.output.mkdir(parents=True, exist_ok=True)
    stamp = datetime.now().strftime('%Y-%m-%d_%H-%M-%S_%f')
    for name, eye in zip(('left', 'right'), eyes):
        eye.save(args.output / f'lockpick-{stamp}-{name}.png')
    stereo = Image.new('RGB', (sum(im.width for im in eyes), max(im.height for im in eyes)))
    x = 0
    for eye in eyes:
        stereo.paste(eye, (x, 0))
        x += eye.width
    target = args.output / f'lockpick-{stamp}-stereo.png'
    stereo.save(target)
    print(f'Saved both eyes and stereo preview:\n{target}')


if __name__ == '__main__':
    try:
        main()
    except (OSError, RuntimeError) as error:
        sys.exit(str(error))
