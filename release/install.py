#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-only
"""Guided preparation for a managed TTWVR Physical Lockpicking installation."""
import hashlib
import os
from pathlib import Path
import subprocess
import sys

from prepare import SUPPORTED

HERE = Path(__file__).resolve().parent
MOD = "TTWVR Physical Lockpicking - Beta 0.1"


def ask(label, default=None):
    suffix = f" [{default}]" if default is not None else ""
    value = input(f"{label}{suffix}\n> ").strip().strip('"')
    if not value and default is None:
        raise ValueError("A path is required.")
    return value or str(default)


def local(value):
    return Path(value).expanduser().resolve()


def main():
    if sys.version_info[:2] not in ((3, 11), (3, 12)):
        raise ValueError("Run the installer with Python 3.11 or 3.12.")
    if not (HERE / "bin/TTWVRPhysicalLockpicking.dll").is_file():
        raise ValueError("Extract the complete Beta 0.1 release ZIP and run install.py there.")
    print("TTWVR Physical Lockpicking · Beta 0.1\nClose Fallout and the mod manager before continuing.")
    game = local(ask("Managed game folder containing FalloutNV.exe"))
    if not (game / "FalloutNV.exe").is_file():
        raise ValueError(f"FalloutNV.exe is missing from {game}")
    data = game / "Data"
    if not (data / "Fallout - Meshes.bsa").is_file():
        data = local(ask("Original game Data folder containing Fallout - Meshes.bsa"))
    if not (data / "Fallout - Meshes.bsa").is_file():
        raise ValueError(f"Fallout - Meshes.bsa is missing from {data}")
    profile = local(ask("VR profile's modlist.txt (or its folder)"))
    if profile.is_dir():
        profile = profile / "modlist.txt"
    if not profile.is_file() or profile.name.casefold() != "modlist.txt":
        raise ValueError("Choose modlist.txt from the profile you use for VR.")
    mods = profile.parent.parent.parent / "mods"
    entries = [s[1:] for s in profile.read_text(encoding="utf-8-sig").splitlines() if s.startswith("+")]
    def enabled_host(folder):
        return next((folder / name for name in entries
                     if (folder / name / "NVSE/Plugins/TTWVR.dll").is_file()), None)
    host = enabled_host(mods)
    if host is None:
        mods = local(ask("Mod manager's mods folder", mods))
        host = enabled_host(mods)
    if host is None:
        raise ValueError("The selected profile has no enabled TTWVR.dll in that mods folder.")
    digest = hashlib.sha256((host / "NVSE/Plugins/TTWVR.dll").read_bytes()).hexdigest()
    if digest not in SUPPORTED:
        raise ValueError(f"The enabled TTWVR.dll in {host} does not match TTWVR 1.1.6 or 1.1.7.")
    output = mods / MOD
    if output.exists():
        raise ValueError(f"{output} already exists. For an update or a second profile, use the manual preparation command in the guide.")
    print(f"\nUsing TTWVR {SUPPORTED[digest]} in {host}\nPreparing {output}", flush=True)
    env = HERE / ".venv"
    python = env / ("Scripts/python.exe" if os.name == "nt" else "bin/python")
    if not python.is_file():
        subprocess.run([sys.executable, "-m", "venv", str(env)], check=True)
    subprocess.run([str(python), "-m", "pip", "install", "-r", str(HERE / "requirements.txt")], check=True)
    subprocess.run([str(python), str(HERE / "prepare.py"), "--data", str(data), "--ttwvr", str(host),
                    "--mods", str(mods), "--modlist", str(profile), "--output", str(output)], check=True)
    print("\nPrepared. Enable the generated mod and TTWVRLockKit.esp in your VR profile.")


if __name__ == "__main__":
    try:
        main()
    except (ValueError, OSError, subprocess.CalledProcessError, EOFError) as error:
        sys.exit(f"Setup stopped: {error}")
