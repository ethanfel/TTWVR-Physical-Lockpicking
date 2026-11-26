#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-only
"""Build a mod-manager-ready TTWVR Physical Lockpicking 1.1.8-r2 mod from local assets."""
import argparse
import hashlib
import json
import os
from pathlib import Path
import platform
import shutil
import subprocess
import sys
import tempfile

ROOT = Path(__file__).resolve().parent
SOURCE = ROOT / "source"
TOOLS = SOURCE / "tools"
SUPPORTED = {
    "34a4e692927e331e317be993e5a73c17695634a5442e7f85bd9b3ce547c7ff0d": "1.1.8",
    "ec59aa4b8eb143dad7b0950415aa6b94020d66034749141aafc65cd56d370b39": "1.1.8",
    "ec33def19bafa1ef753a2a33c2509af9948dfa58952cd79ab81c9cb209c8a40c": "1.1.8",
    "5262d92f1e64a5374a702dffd8b779285c49e49091d9582a8fbd5cade3d99805": "1.1.8",
    "503266c4f4c2350b357c8c15021d8bde28973254745d9302b8e3cfde66489a27": "1.1.6",
    "119a085a5b0101f0618b8e076a421b4b89e30d72d9b6eae874f4e5af7bd3fb27": "1.1.7",
    "63712922499b2155a3d2c6f54abbdfc1eaf95d08712b937c6276d6a8ae0d774d": "1.1.6",
    "ae7dc0d7ba94651af0ccd729122f623d3dbdf0b85c6c787838c04bbc721135d6": "1.1.7",
}


def share_identical_images(folder, extension):
    """Generated images are immutable. Save disk space when hard links are supported."""
    seen = {}
    for path in sorted(folder.glob(extension)):
        with path.open("rb") as stream:
            digest = hashlib.file_digest(stream, "sha256").digest()
        original = seen.setdefault(digest, path)
        if original == path:
            continue
        link = path.with_suffix(".link-tmp")
        try:
            os.link(original, link)
        except OSError:
            return
        os.replace(link, path)


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--data", required=True, type=Path, help="Original New Vegas Data folder with BSAs")
    parser.add_argument("--ttwvr", required=True, type=Path, help="Installed TTWVR folder containing NVSE and meshes")
    parser.add_argument("--output", required=True, type=Path, help="New mod directory")
    parser.add_argument("--modlist", type=Path, help="Active MO2 or Fluorine profile modlist.txt")
    parser.add_argument("--mods", type=Path, help="Manager's mods directory")
    parser.add_argument("--archive", action="append", type=Path, default=[], help="Prioritize a BSA, winning first")
    parser.add_argument("--lock-texture", type=Path, help="Optional local lockinterface01 diffuse DDS")
    args = parser.parse_args()
    if bool(args.modlist) != bool(args.mods):
        parser.error("--modlist and --mods are used together")
    output = args.output.resolve()
    if output.exists() or output.is_symlink():
        parser.error("Output already exists. Choose a new mod directory.")
    if not (args.data / "Fallout - Meshes.bsa").is_file():
        parser.error("The original Fallout - Meshes.bsa is missing from --data")
    dll = args.ttwvr / "NVSE/Plugins/TTWVR.dll"
    if not dll.is_file():
        parser.error("TTWVR.dll is missing from the supplied TTWVR mod")
    version = SUPPORTED.get(hashlib.sha256(dll.read_bytes()).hexdigest())
    if not version:
        parser.error("Unsupported TTWVR.dll. 1.1.8-r2 supports TTWVR 1.1.6, 1.1.7 and 1.1.8.")
    roots = []
    if args.mods:
        roots = [args.mods / row[1:] for row in args.modlist.read_text(encoding="utf-8-sig").splitlines()
                 if row.startswith("+")]
    roots.append(args.data)
    # Files from higher-priority mod roots come first. Within a root, prefer
    # TTW's updated archives over the corresponding vanilla game archives.
    def priority(path):
        name = path.name.casefold()
        return (0 if name.startswith(("yupttw", "taleoftwowastelands", "update")) else
                2 if name.startswith(("fallout -", "fallout3 -")) else 1, name)
    archives = list(args.archive)
    for root in roots:
        if root.is_dir():
            archives.extend(p for p in sorted(root.glob("*"), key=priority)
                            if p.suffix.lower() == ".bsa" and not any(
                                x in p.stem.lower() for x in ("voices", "sounds", " - sound", "invalidation")))
    archives = list(dict.fromkeys(p.resolve() for p in archives))
    for path in [*archives, *([args.lock_texture] if args.lock_texture else [])]:
        if not path.is_file():
            parser.error(f"Input missing: {path}")
    helper = ROOT / "bin" / ("lock_hardware_export.exe" if os.name == "nt" else "lock_hardware_export")
    if os.name != "nt" and (sys.platform != "linux" or platform.machine() not in ("x86_64", "AMD64")):
        parser.error("Use Windows or x86-64 Linux for asset preparation")
    output.parent.mkdir(parents=True, exist_ok=True)
    with tempfile.TemporaryDirectory(prefix=".ttwvr-lockpick-", dir=output.parent) as tmp:
        temp = Path(tmp)
        mod = temp / "mod"
        plugins = mod / "NVSE/Plugins"
        assets = plugins / "TTWVRPhysicalLockpicking"
        assets.mkdir(parents=True)
        def run(name, *options):
            print(f"Preparing {name} for TTWVR {version}", flush=True)
            subprocess.run([sys.executable, "-B", str(TOOLS / f"prepare_{name}.py"), *map(str, options)], check=True)
        run("props", "--data", args.data, "--output", assets,
            *(["--lock-texture", args.lock_texture] if args.lock_texture else []))
        run("hands", "--mod", args.ttwvr, "--data", args.data, "--output", assets)
        options = [value for path in archives for value in ("--archive", path)]
        if args.modlist:
            options.extend(["--mods", args.mods, "--modlist", args.modlist])
        run("targets", "--data", args.data, "--output", assets, *options)
        run("native_scene", "--data", args.data, "--assets", assets, "--output", mod)
        # Patch the winning loose shader package for each name.
        shaders = temp / "shader-source/Shaders"
        shaders.mkdir(parents=True)
        names = set()
        for root in roots:
            folder = next((p for p in root.iterdir() if p.name.casefold() == "shaders" and p.is_dir()), None) if root.is_dir() else None
            if folder:
                for p in folder.iterdir():
                    if p.suffix.lower() == ".sdp" and p.name.lower() not in names:
                        names.add(p.name.lower())
                        shutil.copyfile(p, shaders / p.name.lower())
        run("motion_shaders", "--data", shaders.parent, "--output", mod)
        # Zip extractors on Linux often drop executable permissions.
        local_helper = temp / helper.name
        shutil.copy2(helper, local_helper)
        local_helper.chmod(0o755)
        subprocess.run([str(local_helper), str(assets / "props.bin"), str(temp / "hardware-")], check=True)
        run("world_locks", "--data", args.data, "--hardware", temp, "--output", mod)
        run("kit", "--data", args.data, "--assets", assets, "--hand-mod", args.ttwvr, "--output", mod)
        shutil.copyfile(ROOT / "bin/TTWVRPhysicalLockpicking.dll", plugins / "TTWVRPhysicalLockpicking.dll")
        shutil.copyfile(SOURCE / "TTWVRPhysicalLockpicking.ini", plugins / "TTWVRPhysicalLockpicking.ini")
        for manifest in (SOURCE / "manifest").glob("*.json"):
            shutil.copyfile(manifest, assets / manifest.name)
        (mod / "meta.ini").write_text("[General]\nversion=1.1.8-r2\ncomments=TTWVR Physical Lockpicking 1.1.8-r2\n", encoding="utf-8")
        profiles = json.loads((TOOLS / "target_profiles.json").read_text())
        meshes = mod / "meshes/TTWVRPhysicalLockpicking"
        required = [mod / "TTWVRLockKit.esp", assets / "kit-pinch.bin", assets / "kit-world.bin", assets / "kit-surfaces.bin",
                    meshes / "workspace-0.nif", *[meshes / f"kit-{hand}-{tool}.nif" for hand in (0, 1) for tool in (0, 1)],
                    *[meshes / f"{kind}-{row['kind']}.nif" for row in profiles for kind in ("workspace", "world-lock")]]
        if any(not p.is_file() or p.stat().st_size == 0 for p in required):
            raise RuntimeError("Generated addon is incomplete")
        if output.exists():
            raise FileExistsError(output)
        share_identical_images(assets, "*.rgba")
        share_identical_images(mod / "textures/TTWVRPhysicalLockpicking", "*.dds")
        mod.rename(output)
    print(f"1.1.8-r2 prepared in {output}. Enable this mod and TTWVRLockKit.esp and launch TTWVR as usual.")


if __name__ == "__main__":
    main()
