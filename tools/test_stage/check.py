#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-only
"""Validate the generated stage, including its catalogue and lock matrix."""

import json, sys
from pathlib import Path
from build import records, LEVELS, NifFormat

p = Path(sys.argv[1])
m = json.loads((p / "stage-catalog.json").read_text())
rr = list(
    records(
        p / m["plugin"],
        {
            b"TES4",
            b"GLOB",
            b"ACTI",
            b"STAT",
            b"CONT",
            b"DOOR",
            b"FLST",
            b"CELL",
            b"REFR",
        },
    )
)
forms = {}
for sig, id, flags, it in rr:
    if sig == b"TES4":
        assert [v.rstrip(b"\0") for k, v in it if k == b"MAST"] == [b"FalloutNV.esm"]
        continue
    assert id not in forms and id >> 24 == 1, (hex(id), sig)
    forms[id] = (sig, dict(it))
    if sig in (b"CONT", b"DOOR"):
        assert not {b"SCRI", b"CNTO", b"DEST"} & dict(it).keys()
    if sig == b"REFR":
        assert not {b"XTEL", b"XOWN", b"XESP"} & dict(it).keys()
        assert not flags & 0x820, (hex(id), "deleted or disabled reference")
count = 0
for page in m["galleries"]:
    assert forms[int(page["form"], 16)][0] == b"CELL"
    for row in page["rows"]:
        assert [(x["difficulty"], x["level"]) for x in row["stations"]] == LEVELS
        for station in row["stations"]:
            sig, d = forms[int(station["reference"], 16)]
            assert sig == b"REFR"
            assert int.from_bytes(d[b"NAME"], "little") == int(station["base"], 16)
            assert (
                len(d[b"XLOC"]) == 20
                and d[b"XLOC"][0] == station["level"]
                and not any(d[b"XLOC"][1:])
            )
            count += 1
assert count == m["stations"] == m["models"] * 5
for id, (sig, d) in forms.items():
    if sig == b"FLST":
        items = next(it for s, f, _, it in rr if f == id)
        assert all(
            int.from_bytes(v, "little") in forms for k, v in items if k == b"LNAM"
        )
    for k in [b"MODL"]:
        if k in d and d[k].startswith(b"TTWVRLockStage"):
            assert (
                p / "meshes" / d[k].rstrip(b"\0").decode().replace("\\", "/")
            ).is_file()
print(
    "PASS additive records, private model labels, reference links, complete difficulty matrix:",
    count,
    "locks",
)

for path in (p / "meshes/TTWVRLockStage").glob("*.nif"):
    data = NifFormat.Data()
    with path.open("rb") as f:
        data.read(f)
    assert data.header.endian_type == 1 and data.version == 0x14020007
    shapes = [b for b in data.blocks if isinstance(b, NifFormat.NiTriShapeData)]
    shaders = [
        b for b in data.blocks if isinstance(b, NifFormat.BSShaderPPLightingProperty)
    ]
    assert len(shapes) == len(shaders) == 1
    assert shapes[0].num_triangles == 2 and shapes[0].num_vertices == 4
    assert shaders[0].shader_type == 1 and shaders[0].shader_flags.sf_z_buffer_test
    for raw in shaders[0].texture_set.textures:
        if raw:
            assert (p / raw.decode().replace("\\", "/")).is_file()
for name in [
    "Menu",
    "Button",
    "BonusTick",
    "Tick",
    "Install",
    "Control",
    "Reset",
    "Enter",
    "Arrive",
    "WorldInstall",
    "WorldTick",
    "WorldActivate",
]:
    s = (p / f"NVSE/user_defined_functions/TTWVRLockStage/{name}.txt").read_text()
    assert "{{" not in s and "%n" not in s, (
        name,
        "invalid function brackets or formatting",
    )
assert (
    "MessageBoxExAlt"
    in (p / "NVSE/user_defined_functions/TTWVRLockStage/Menu.txt").read_text()
)
print("PASS native sign mesh headers/shaders/textures and generated script sanity")
