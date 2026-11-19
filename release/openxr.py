#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-only
"""Generate scoped Windows/Proton registration and removal files for the managed game."""
import argparse
from pathlib import Path, PureWindowsPath

parser = argparse.ArgumentParser(description=__doc__)
parser.add_argument("--game", required=True, help="Absolute Windows path to the game directory as seen by FalloutNV.exe")
parser.add_argument("--output", type=Path, default=Path.cwd())
args = parser.parse_args()
game = PureWindowsPath(args.game)
if not game.is_absolute() or any(c in args.game for c in '"\n\r'):
    parser.error("Use an absolute Windows game directory")
manifest = game / "Data/NVSE/Plugins/TTWVRPhysicalLockpicking/XR_APILAYER_TTWVR_physical_lockpicking.json"
key = r"HKEY_LOCAL_MACHINE\SOFTWARE\WOW6432Node\Khronos\OpenXR\1\ApiLayers\Implicit"
value = str(manifest).replace("\\", "\\\\")
args.output.mkdir(parents=True, exist_ok=True)
for name, data in (("register", "dword:00000000"), ("unregister", "-")):
    dest = args.output / f"{name}-ttwvr-lockpicking.reg"
    dest.write_bytes(f'Windows Registry Editor Version 5.00\r\n\r\n[{key}]\r\n"{value}"={data}\r\n'.encode("utf-16"))
    print(dest)
