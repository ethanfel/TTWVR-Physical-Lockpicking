#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-only
"""Package the player guides, installer, compiled addon and corresponding source."""
import argparse
from pathlib import Path
from zipfile import ZIP_DEFLATED, ZipFile

parser = argparse.ArgumentParser(description=__doc__)
parser.add_argument("--bin", type=Path, required=True)
parser.add_argument("--output", type=Path, required=True)
args = parser.parse_args()
release = Path(__file__).resolve().parent
root = release.parent
source_extensions = {".cpp", ".h", ".cmake", ".in", ".py", ".json", ".txt"}
with ZipFile(args.output, "x", compression=ZIP_DEFLATED, compresslevel=8) as archive:
    for name in ("README.md", "LICENSE", "docs/install-linux.md", "docs/install-windows.md",
                 "docs/help.md", "docs/advanced.md"):
        archive.write(root / name, name)
    for name in ("install.py", "prepare.py", "openxr.py", "requirements.txt"):
        archive.write(release / name, name)
    for name in ("TTWVRPhysicalLockpicking.dll", "lock_hardware_export", "lock_hardware_export.exe"):
        archive.write(args.bin / name, "bin/" + name)
    for name in ("CMakeLists.txt", "TTWVRPhysicalLockpicking.ini"):
        archive.write(root / name, "source/" + name)
    for folder in ("src", "cmake", "kit", "manifest", "tests", "tools"):
        for path in sorted((root / folder).rglob("*")):
            if path.is_file() and path.suffix in source_extensions:
                archive.write(path, "source/" + path.relative_to(root).as_posix())
