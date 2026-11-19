#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-only
"""Build a PRIVATE, additive Fallout lock-testing plugin from installed records.
Requires the prepare_hands Python environment. No source records are overridden.
Record layout: https://tes5edit.github.io/fopdoc/FalloutNV/Records/REFR.html
"""

import argparse, collections, hashlib, json, math, re, struct, sys, zlib
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parents[1]))
from prepare_hands import NifFormat, Image
from PIL import ImageDraw, ImageFont

PLUGIN = "TTWVRLockStage.esp"
SUPPORTED = {p["model"] for p in json.loads((Path(__file__).resolve().parents[1] / "target_profiles.json").read_text())}
LEVELS = [
    ("VERY EASY", 0),
    ("EASY", 25),
    ("AVERAGE", 50),
    ("HARD", 75),
    ("VERY HARD", 100),
]
CORE = [
    "clutter/security/genericsafe01.nif",
    "clutter/ammo/ammobox01.nif",
    "dungeons/office/doors/offdoorsml01.nif",
    "architecture/urban/buildingkits/schdoorsmr01.nif",
    "clutter/security/genericsafewall01.nif",
    "clutter/security/genericsafefloor01.nif",
    "clutter/footlocker/footlocker01.nif",
    "clutter/junk/toolbox01.nif",
]
CORE_NAMES = [
    "Freestanding safe",
    "Ammunition box",
    "Office door - left",
    "School door - right",
    "Wall safe",
    "Floor safe",
    "Footlocker",
    "Toolbox",
]


def subfields(data):
    p = 0
    out = []
    extended = None
    while p + 6 <= len(data):
        tag, n = struct.unpack_from("<4sH", data, p)
        p += 6
        if tag == b"XXXX":
            if n != 4:
                raise ValueError("Malformed extended subrecord")
            extended = struct.unpack_from("<I", data, p)[0]
            p += n
            continue
        if extended is not None:
            n, extended = extended, None
        if p + n > len(data):
            raise ValueError("Truncated subrecord")
        out.append((tag, data[p : p + n]))
        p += n
    if p != len(data):
        raise ValueError("Trailing subrecord bytes")
    return out


def records(path, keep):
    with path.open("rb") as f:

        def walk(end):
            while f.tell() < end:
                start = f.tell()
                header = f.read(24)
                if len(header) != 24:
                    raise ValueError("Truncated record")
                sig, n, flags, form = struct.unpack_from("<4sIII", header)
                if sig == b"GRUP":
                    if n < 24 or start + n > end:
                        raise ValueError("Invalid group")
                    yield from walk(start + n)
                else:
                    if f.tell() + n > end:
                        raise ValueError("Invalid record size")
                    if sig not in keep:
                        f.seek(n, 1)
                        continue
                    data = f.read(n)
                    if flags & 0x40000:
                        size = struct.unpack_from("<I", data)[0]
                        data = zlib.decompress(data[4:])
                        if len(data) != size:
                            raise ValueError("Invalid compressed record")
                    yield sig, form, flags, subfields(data)

        yield from walk(path.stat().st_size)


def txt(d, k=b"EDID"):
    return d.get(k, b"").rstrip(b"\0").decode("cp1252", errors="replace")


def sub(k, v):
    return k + struct.pack("<H", len(v)) + v


def z(s):
    return s.encode("cp1252", errors="replace") + b"\0"


def u(n):
    return struct.pack("<I", n)


def record(k, id, items, flags=0):
    data = b"".join(sub(k, v) for k, v in items)
    return struct.pack("<4sIIIIHH", k, len(data), flags, id, 0, 15, 0) + data


def group(label, kind, data):
    return (
        struct.pack("<4sI4sIHHI", b"GRUP", len(data) + 24, label, kind, 0, 0, 0) + data
    )


def label_asset(output, index, lines, fontpath):
    image = Image.new("RGB", (1024, 256), (25, 30, 27))
    draw = ImageDraw.Draw(image)
    draw.rectangle((6, 6, 1017, 249), outline=(203, 167, 82), width=5)
    for n, text in enumerate(lines):
        size = 58 if len(text) < 28 else 42
        font = ImageFont.truetype(str(fontpath), size)
        draw.text(
            (512, 60 + n * 105), text, font=font, fill=(236, 198, 106), anchor="mm"
        )
    tex = output / f"textures/TTWVRLockStage/label-{index}.dds"
    tex.parent.mkdir(parents=True, exist_ok=True)
    image.convert("RGBA").save(tex)
    normal = output / "textures/TTWVRLockStage/normal.dds"
    Image.new("RGBA", (4, 4), (128, 128, 255, 255)).save(normal)
    data = NifFormat.Data(version=0x14020007, user_version=11, user_version_2=34)
    data.header.endian_type = 1
    root = NifFormat.NiNode()
    root.name = b"TTWVR lock testing label"
    root.flags = 14
    data.roots = [root]
    shape = NifFormat.NiTriShape()
    shape.name = b"Original label geometry"
    shape.flags = 14
    root.add_child(shape)
    geo = NifFormat.NiTriShapeData()
    shape.data = geo
    geo.num_vertices = 4
    geo.has_vertices = True
    geo.vertices.update_size()
    for v, p in zip(
        geo.vertices, [(-128, 0, 0), (128, 0, 0), (128, 0, 64), (-128, 0, 64)]
    ):
        v.x, v.y, v.z = p
    geo.has_normals = True
    geo.normals.update_size()
    for v in geo.normals:
        v.x, v.y, v.z = 0, -1, 0
    geo.num_uv_sets = 1
    geo.uv_sets.update_size()
    for v, p in zip(geo.uv_sets[0], [(0, 1), (1, 1), (1, 0), (0, 0)]):
        v.u, v.v = p
    geo.set_triangles([(0, 1, 2), (0, 2, 3)])
    geo.update_center_radius()
    mat = NifFormat.NiMaterialProperty()
    mat.diffuse_color.r = mat.diffuse_color.g = mat.diffuse_color.b = 1
    mat.emissive_color.r = mat.emissive_color.g = mat.emissive_color.b = 0.2
    mat.alpha = 1
    shape.add_property(mat)
    shader = NifFormat.BSShaderPPLightingProperty()
    shader.shader_type = 1
    shader.shader_flags.sf_specular = 0
    shader.shader_flags.sf_remappable_textures = 1
    shader.shader_flags.sf_z_buffer_test = 1
    shader.shader_flags_2.sf_2_z_buffer_write = 1
    ts = NifFormat.BSShaderTextureSet()
    ts.num_textures = 6
    ts.textures.update_size()
    ts.textures[0] = f"textures\\TTWVRLockStage\\label-{index}.dds".encode()
    ts.textures[1] = b"textures\\TTWVRLockStage\\normal.dds"
    shader.texture_set = ts
    shape.add_property(shader)
    path = output / f"meshes/TTWVRLockStage/label-{index}.nif"
    path.parent.mkdir(parents=True, exist_ok=True)
    with path.open("wb") as f:
        data.write(f)
    verify = NifFormat.Data()
    with path.open("rb") as f:
        verify.read(f)
    assert verify.version == 0x14020007 and len(verify.roots) == 1
    return f"TTWVRLockStage\\label-{index}.nif"


def build(a):
    a.output.mkdir(parents=True, exist_ok=True)
    sources = [a.new_vegas] + ([a.fallout3] if a.fallout3 else [])
    nv = {}
    models = {}
    for path in sources:
        rr = list(records(path, {b"CONT", b"DOOR", b"REFR", b"STAT", b"ACTI", b"TERM"}))
        base = {
            id: (sig, dict(it))
            for sig, id, fl, it in rr
            if sig in (b"CONT", b"DOOR") and not fl & 0x20
        }
        if path == a.new_vegas:
            nv = {id: (sig, dict(it)) for sig, id, fl, it in rr if sig != b"REFR"}
        allbase = {
            **{id: v for id, v in nv.items() if v[0] in (b"CONT", b"DOOR")},
            **base,
        }
        for sig, id, fl, it in rr:
            d = dict(it)
            if sig != b"REFR" or fl & 0x20 or b"XLOC" not in d or d[b"XLOC"][0] > 100:
                continue
            bid = int.from_bytes(d.get(b"NAME", b""), "little")
            if bid not in allbase:
                continue
            kind, bd = allbase[bid]
            model = txt(bd, b"MODL").lower().replace("\\", "/")
            if model and not model.startswith(("effects/", "marker")):
                models.setdefault(
                    model,
                    dict(
                        kind=kind, base=bd, source=path.name, source_form=f"{bid:08X}"
                    ),
                )
    for model in CORE:
        if model not in models:
            id, (kind, d) = next(
                (id, v)
                for id, v in nv.items()
                if txt(v[1], b"MODL").lower().replace("\\", "/") == model
                and v[0] in (b"CONT", b"DOOR")
            )
            models[model] = dict(
                kind=kind, base=d, source=a.new_vegas.name, source_form=f"{id:08X}"
            )
    ordered = CORE + sorted(m for m in models if m not in CORE)
    pages = [ordered[n : n + 8] for n in range(0, len(ordered), 8)]
    nextid = 0x01000800
    groups = collections.defaultdict(list)
    count = 0
    groupcount = 0

    def alloc():
        nonlocal nextid
        result = nextid
        nextid += 1
        return result

    def add(sig, id, items, flags=0):
        nonlocal count
        count += 1
        groups[sig].append(record(sig, id, items, flags))
        return id

    def global_(ed, value=0):
        return add(
            b"GLOB",
            alloc(),
            [(b"EDID", z(ed)), (b"FNAM", b"f"), (b"FLTV", struct.pack("<f", value))],
        )

    delta = global_("TTWVRStageSkillDelta")
    menu = global_("TTWVRStageMenuActive")
    settings = dict(
        active=global_("TTWVRStageActive"),
        skill=global_("TTWVRStageSkill", 100),
        original_infiltrator=global_("TTWVRStageOriginalInfiltrator"),
        bonus=global_("TTWVRStageBonus", 2),
        original_bonus=global_("TTWVRStageOriginalBonus"),
    )
    control = add(
        b"ACTI",
        alloc(),
        [
            (b"EDID", z("TTWVRStageControl")),
            (b"OBND", nv[0x20092][1][b"OBND"]),
            (b"FULL", z("LOCK TEST TERMINAL - skill / perks / reset")),
            (b"MODL", nv[0x20092][1][b"MODL"]),
        ],
    )

    def label(index, lines):
        model = label_asset(a.output, index, lines, a.font)
        return add(
            b"STAT",
            alloc(),
            [
                (b"EDID", z("TTWVRStageLabel" + str(index))),
                (b"OBND", struct.pack("<6h", -128, 0, 0, 128, 0, 64)),
                (b"MODL", z(model)),
            ],
        )

    headings = [
        label("level" + str(n), [title, "LOCKPICK " + str(level)])
        for n, (title, level) in enumerate(LEVELS)
    ]
    cells = [alloc() for _ in pages]
    cellslist = add(
        b"FLST",
        alloc(),
        [(b"EDID", z("TTWVRStageCells"))] + [(b"LNAM", u(c)) for c in cells],
    )
    man = []
    cellgroups = []
    lists = []
    for page, paths in enumerate(pages):
        cell = cells[page]
        cellid = "TTWVRLockLab" + str(page + 1).zfill(2)
        refs = []
        locks = []
        rows = []
        controls = []

        def ref(base, x, y, zpos=0, yaw=0, level=None, scale=1, editor=None):
            nonlocal count
            id = alloc()
            it = []
            if editor:
                it.append((b"EDID", z(editor)))
            it.append((b"NAME", u(base)))
            if level is not None:
                it.append((b"XLOC", struct.pack("<B3xIB3x8x", level, 0, 0)))
            if scale != 1:
                it.append((b"XSCL", struct.pack("<f", scale)))
            it.append((b"DATA", struct.pack("<6f", x, y, zpos, 0, 0, yaw)))
            count += 1
            refs.append(record(b"REFR", id, it, 0x400 if editor else 0))
            return id

        # An enclosed office-kit room. Perimeter pieces include their own floors.
        for ix in range(12):
            for iy in range(18):
                x, y = -256 + ix * 256, -256 + iy * 256
                edges = (ix == 0, ix == 11, iy == 0, iy == 17)
                if sum(edges) == 0:
                    base = 0x159DF
                    angle = 0
                elif sum(edges) == 1:
                    base = 0x159E0
                    angle = (
                        math.pi / 2
                        if ix == 0
                        else -math.pi / 2
                        if ix == 11
                        else 0
                        if iy == 0
                        else math.pi
                    )
                else:
                    base = 0x159DD
                    angle = (
                        0
                        if ix == 0 and iy == 0
                        else math.pi / 2
                        if ix == 0
                        else -math.pi / 2
                        if iy == 0
                        else math.pi
                    )
                ref(base, x, y, 0, angle + math.pi if any(edges) else angle)
        ref(0x32, 1024, -160, 4, 0, editor=cellid + "Arrival")
        if page == 0:
            return_marker = ref(0x34, 1024, -160, 4, 0, editor="TTWVRStageReturnMarker")
        ref(0xF21B3, 1152, 0)
        panel = ref(control, 1152, 0, 55, math.pi, editor=cellid + "Control")
        controls.append(panel)
        title = label(
            "page" + str(page),
            ["LOCK TEST GALLERY", f"{page + 1:02d} / {len(pages):02d} - TRAINING"],
        )
        ref(title, 1152, 92, 194, scale=0.5)
        for col, h in enumerate(headings):
            ref(h, 128 + 512 * col, 160, 164)
        for row, path in enumerate(paths):
            source = models[path]
            kind = source["kind"]
            d = source["base"]
            bounds = struct.unpack("<6h", d.get(b"OBND", b"\0" * 12))
            height = bounds[5] - bounds[2]
            width = max(bounds[3] - bounds[0], bounds[4] - bounds[1])
            scale = min(1.0, 360 / max(width, 1), 224 / max(height, 1))
            title = (
                CORE_NAMES[CORE.index(path)]
                if path in CORE
                else re.sub(
                    r"(?<=[a-z])(?=[A-Z])",
                    " ",
                    Path(txt(d, b"MODL").replace("\\", "/")).stem,
                )
            )
            yaw = (
                math.pi / 2
                if path in (CORE[0], CORE[2], CORE[3], CORE[4])
                or (kind == b"DOOR" and bounds[3] - bounds[0] < bounds[4] - bounds[1])
                else 0
            )
            elevated = (
                kind == b"CONT" and height * scale < 70 and "safefloor" not in path
            )
            zpos = 55 - bounds[2] * scale if elevated else -bounds[2] * scale
            if "safewall" in path:
                zpos = 75 - bounds[2] * scale
                elevated = False
            rowlabel = label(
                "row" + str(len(man) * 8 + row),
                [
                    f"{page * 8 + row + 1:03d}  " + title[:34],
                    "FULL OBJECT + WORLD LOCK" if path in SUPPORTED else "STANDALONE LOCK",
                ],
            )
            y = 512 + row * 448
            ref(rowlabel, -200, y + 110, 160, -math.pi / 2)
            rowinfo = dict(
                title=title,
                model=path,
                kind=kind.decode(),
                source=source["source"],
                source_form=source["source_form"],
                scale=scale,
                stations=[],
            )
            for col, (difficulty, level) in enumerate(LEVELS):
                x = 128 + 512 * col
                bid = alloc()
                it = [
                    (b"EDID", z(f"TTWVRStageObject{page:02d}_{row}_{col}")),
                    (b"OBND", d.get(b"OBND", b"\0" * 12)),
                    (b"FULL", z(f"{page * 8 + row + 1:03d} {title} | {difficulty}")),
                    (b"MODL", d[b"MODL"]),
                ]
                # Never copy quest scripts, ownership, loot, keys, teleport links or destruction.
                if kind == b"CONT":
                    it.append((b"DATA", struct.pack("<Bf", 0, 0)))
                for tag in (
                    [b"SNAM", b"QNAM"]
                    if kind == b"CONT"
                    else [b"SNAM", b"ANAM", b"BNAM"]
                ):
                    if tag in d and int.from_bytes(d[tag], "little") >> 24 == 0:
                        it.append((tag, d[tag]))
                if kind == b"DOOR":
                    it.append((b"FNAM", bytes([d.get(b"FNAM", b"\0")[0] & 0x10])))
                add(kind, bid, it)
                cx = (bounds[0] + bounds[3]) * 0.5 * scale
                cy = (bounds[1] + bounds[4]) * 0.5 * scale
                rx = math.cos(yaw) * cx + math.sin(yaw) * cy
                ry = -math.sin(yaw) * cx + math.cos(yaw) * cy
                rid = ref(
                    bid,
                    x - rx,
                    y - ry,
                    zpos,
                    yaw,
                    level,
                    scale,
                    f"TTWVRStageLock{page:02d}_{row}_{col}",
                )
                locks.append(rid)
                rowinfo["stations"].append(
                    dict(
                        reference=f"{rid:08X}",
                        base=f"{bid:08X}",
                        difficulty=difficulty,
                        level=level,
                        position=[x - rx, y - ry, zpos],
                        yaw=yaw,
                    )
                )
                if elevated:
                    ref(0xF21B3, x, y)
                if "safewall" in path:
                    ref(0x18779, x, y + 130)
            rows.append(rowinfo)
        locklist = add(
            b"FLST",
            alloc(),
            [(b"EDID", z(cellid + "Locks"))] + [(b"LNAM", u(r)) for r in locks],
        )
        lists.append(locklist)
        lighting = bytes(
            [130, 130, 120, 0, 170, 165, 140, 0, 40, 42, 40, 0]
        ) + struct.pack("<ffii fff", 10000, 20000, 45, 45, 0.5, 0, 1)
        cellrec = record(
            b"CELL",
            cell,
            [
                (b"EDID", z(cellid)),
                (b"FULL", z(f"Lock Testing - Gallery {page + 1:02d}")),
                (b"DATA", b"\x21"),
                (b"XCLL", lighting),
                (b"LTMP", u(0)),
                (b"LNAM", u(0)),
                (b"XCLW", struct.pack("<f", -2147483648.0)),
                (b"XNAM", b"\0"),
            ],
        )
        count += 1
        # Persist only explicitly named control/lock/arrival references for safe reset lookup.
        persistent = []
        temporary = []
        for raw in refs:
            (
                persistent if int.from_bytes(raw[8:12], "little") & 0x400 else temporary
            ).append(raw)
        children = group(u(cell), 8, b"".join(persistent)) + group(
            u(cell), 9, b"".join(temporary)
        )
        groupcount += 3
        cellgroups.append((cell, cellrec + group(u(cell), 6, children)))
        man.append(
            dict(
                cell=cellid,
                form=f"{cell:08X}",
                control=f"{panel:08X}",
                lock_list=f"{locklist:08X}",
                rows=rows,
            )
        )
    # Interior grouping follows the local object ID's decimal block/sub-block.
    blocks = collections.defaultdict(lambda: collections.defaultdict(list))
    for cell, raw in cellgroups:
        blocks[(cell & 0xFFFFFF) % 10][((cell & 0xFFFFFF) // 10) % 10].append(raw)
    cellraw = b""
    for block, subs in sorted(blocks.items()):
        body = b""
        for subblock, rr in sorted(subs.items()):
            body += group(u(subblock), 3, b"".join(rr))
            groupcount += 1
        cellraw += group(u(block), 2, body)
        groupcount += 1
    groups[b"CELL"] = [cellraw]
    body = b"".join(group(k, 0, b"".join(v)) for k, v in groups.items())
    groupcount += len(groups)
    header = record(
        b"TES4",
        0,
        [
            (b"HEDR", struct.pack("<fII", 1.34, count + groupcount, nextid & 0xFFFFFF)),
            (b"CNAM", z("TTWVR Physical Lockpicking")),
            (
                b"SNAM",
                z(
                    "Isolated lock test galleries. New records only. Generated from installed models."
                ),
            ),
            (b"MAST", z("FalloutNV.esm")),
            (b"DATA", b"\0" * 8),
        ],
    )
    (a.output / PLUGIN).write_bytes(header + body)
    manifest = dict(
        plugin=PLUGIN,
        return_marker=f"{return_marker:08X}",
        source_files={
            str(p): hashlib.sha256(p.read_bytes()).hexdigest() for p in sources
        },
        models=len(ordered),
        stations=len(ordered) * 5,
        difficulties=LEVELS,
        globals=dict(
            skill_delta=f"{delta:08X}",
            menu=f"{menu:08X}",
            **{k: f"{v:08X}" for k, v in settings.items()},
        ),
        control_base=f"{control:08X}",
        cells_list=f"{cellslist:08X}",
        galleries=man,
    )
    (a.output / "stage-catalog.json").write_text(json.dumps(manifest, indent=2) + "\n")
    # TTW's bobblehead effect is verified from the installed quest function.
    ttw = a.fallout3.parent / "TaleOfTwoWastelands.esm" if a.fallout3 else None
    manifest["bobblehead_effect_verified"] = False
    if ttw and ttw.exists():
        for sig, id, flags, items in records(ttw, {b"SCPT"}):
            d = dict(items)
            if txt(d) == "TTWLockpickBobbleheadFunction":
                manifest["bobblehead_effect_verified"] = (
                    "setnumericgamesetting flockpickbonushealth 0.015"
                    in txt(d, b"SCTX").lower()
                )
    manifest["bonus_off"] = 0.01
    manifest["bonus_on"] = 0.015
    (a.output / "stage-catalog.json").write_text(json.dumps(manifest, indent=2) + "\n")
    from scripts import write_scripts

    write_scripts(a.output, manifest)
    print(
        f"Built {len(ordered)} models, {len(ordered) * 5} locks, {len(pages)} galleries: {a.output / PLUGIN}"
    )


if __name__ == "__main__":
    p = argparse.ArgumentParser(description=__doc__)
    p.add_argument("--new-vegas", type=Path, required=True)
    p.add_argument("--fallout3", type=Path)
    p.add_argument("--output", type=Path, required=True)
    p.add_argument(
        "--font", type=Path, default=Path("/usr/share/fonts/TTF/DejaVuSansMono.ttf")
    )
    build(p.parse_args())
