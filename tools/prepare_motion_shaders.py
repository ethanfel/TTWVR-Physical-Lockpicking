#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-only
"""Prepare private Fallout shader overrides for TTWVR's rigid-motion analysis.

The author misses some vanilla SLS shaders that route clip positions through
reused temporary registers. Prove their final position is exactly c0..c3 dot v0,
then express those four writes directly. Lighting, fog and texture outputs stay
unchanged. Generated game shader packages must not be redistributed.
"""
import argparse
import hashlib
import json
import os
from pathlib import Path
import struct


def reg(token):
    return ((token >> 28) & 7) | ((token >> 8) & 24), token & 2047


def instructions(code):
    if len(code) % 4 or len(code) < 8:
        raise ValueError('Invalid shader size')
    words = struct.unpack('<' + 'I' * (len(code) // 4), code)
    if words[0] != 0xfffe0200:  # Only the verified vertex-shader 2.0 format.
        return None
    result = []
    pos = 1
    while pos < len(words):
        token = words[pos]
        opcode = token & 65535
        if opcode == 65535:
            if pos != len(words) - 1:
                raise ValueError('Trailing shader instructions')
            return result
        length = ((token >> 16) & 32767) if opcode == 65534 else ((token >> 24) & 15)
        if pos + length >= len(words):
            raise ValueError('Truncated shader instruction')
        result.append(tuple(words[pos:pos + length + 1]))
        pos += length + 1
    raise ValueError('Missing shader terminator')


def rewrite(code):
    ops = instructions(code)
    if ops is None:
        return code
    state = {}
    position_writes = []

    def source(token):
        if token & 0x0f002000:  # Relative addressing or a source modifier.
            return [None] * 4
        key = reg(token)
        values = state.get(key)
        if values is None:
            values = [(key[0], key[1], i) for i in range(4)] if key[0] in (1, 2) else [None] * 4
        return [values[(token >> (16 + 2 * i)) & 3] for i in range(4)]

    # All these opcodes have one ordinary destination. Unknown operations clear
    # its symbolic components; they can never prove a position expression.
    ordinary = {1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11, 12, 13, 14, 15, 16, 17,
                18, 19, 20, 21, 22, 23, 24, 32, 33, 34, 35, 36, 37, 46, 47,
                48, 51, 78, 79, 80, 81, 88, 90}
    for index, op in enumerate(ops):
        opcode = op[0] & 65535
        if opcode in (0, 31, 65534):  # nop, declaration, comment
            continue
        if opcode not in ordinary or len(op) < 2 or op[0] & 0x10000000:
            return code  # No control flow / predication assumptions.
        if opcode not in (48, 51, 81) and any(reg(t) == (4, 0) for t in op[2:]):
            return code  # Never remove an output write read by later code.
        dst = op[1]
        key = reg(dst)
        mask = (dst >> 16) & 15
        values = [None] * 4
        if opcode == 1 and len(op) == 3:
            values = source(op[2])
        elif opcode == 9 and len(op) == 4:
            a, b = source(op[2]), source(op[3])
            inp = [(1, 0, i) for i in range(4)]
            for row in range(4):
                con = [(2, row, i) for i in range(4)]
                if (a == inp and b == con) or (b == inp and a == con):
                    values = [('dp4', row)] * 4
        if dst & 0x0ff00000:  # Saturation / destination shifts are not equivalent.
            values = [None] * 4
        old = state.setdefault(key, [None] * 4)
        for i in range(4):
            if mask & (1 << i):
                old[i] = values[i]
        if key == (4, 0):
            if opcode not in (1, 9) or dst & 0x0ff00000:
                return code
            position_writes.append(index)
    if state.get((4, 0)) != [('dp4', i) for i in range(4)]:
        return code
    # Already direct: retain the original byte-for-byte.
    if len(position_writes) == 4 and all((ops[i][0] & 65535) == 9 for i in position_writes):
        return code
    direct = [(0x03000009, 0xc0000000 | (1 << (16 + i)), 0xa0e40000 + i, 0x90e40000)
              for i in range(4)]
    result = [0xfffe0200]
    for i, op in enumerate(ops):
        if i not in position_writes:
            result.extend(op)
    for op in direct:
        result.extend(op)
    result.append(65535)
    return struct.pack('<' + 'I' * len(result), *result)


def package(raw):
    if len(raw) < 12:
        raise ValueError('Truncated package')
    version, count, size = struct.unpack_from('<3I', raw)
    if version != 100 or count > 10000 or size != len(raw) - 12:
        raise ValueError('Unsupported shader package')
    pos, parts, changed = 12, [], []
    for _ in range(count):
        if pos + 260 > len(raw):
            raise ValueError('Truncated package entry')
        name = raw[pos:pos + 256]
        length = struct.unpack_from('<I', raw, pos + 256)[0]
        code = raw[pos + 260:pos + 260 + length]
        if len(code) != length:
            raise ValueError('Truncated shader')
        pos += 260 + length
        label = name.split(b'\0')[0].decode('ascii')
        output = rewrite(code) if label.startswith('SLS2') and label.endswith('.vso') else code
        if output != code:
            changed.append({'shader': label, 'before_sha256': hashlib.sha256(code).hexdigest(),
                            'after_sha256': hashlib.sha256(output).hexdigest()})
        parts.append(name + struct.pack('<I', len(output)) + output)
    if pos != len(raw):
        raise ValueError('Trailing package data')
    body = b''.join(parts)
    return struct.pack('<3I', version, count, len(body)) + body, changed


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--data', type=Path, required=True, help='Original game Data directory')
    parser.add_argument('--output', type=Path, required=True, help='Separate addon mod directory')
    args = parser.parse_args()
    source = next((p for p in args.data.iterdir() if p.name.lower() == 'shaders'), None)
    if source is None:
        raise SystemExit('No original Shaders directory found')
    dest = args.output / 'Shaders'
    if dest.resolve() == source.resolve():
        raise SystemExit('Output must be a separate mod; original packages are never overwritten')
    packages = sorted(source.glob('*.sdp'))
    if not packages:
        raise SystemExit('No shader packages found')
    prepared, report = [], []
    # Validate every input before writing any output.
    for path in packages:
        raw = path.read_bytes()
        output, changes = package(raw)
        prepared.append((path.name, output))
        report.append({'package': path.name, 'source_sha256': hashlib.sha256(raw).hexdigest(),
                       'output_sha256': hashlib.sha256(output).hexdigest(), 'changes': changes})
    dest.mkdir(parents=True, exist_ok=True)
    for name, data in prepared:
        temp = dest / (name + '.tmp')
        temp.write_bytes(data)
        os.replace(temp, dest / name)
    (args.output / 'native-motion-shaders.json').write_text(json.dumps(report, indent=2) + '\n')
    print(f'Prepared {len(prepared)} private packages; rewrote {sum(len(x["changes"]) for x in report)} position expressions')


if __name__ == '__main__':
    main()
