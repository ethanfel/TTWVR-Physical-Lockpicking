# SPDX-License-Identifier: GPL-3.0-only
"""Describe the motion shader edits as copies from the installed game plus new instructions."""
import hashlib
import struct
from prepare_motion_shaders import instructions, reg, rewrite


def build(original, expected):
    commands = []
    def copy(offset, size):
        if commands and commands[-1][0] >= 0 and sum(commands[-1]) == offset:
            commands[-1][1] += size
        elif size:
            commands.append([offset,size])
    def literal(value):
        commands.append([-1,value])

    copy(0,8)
    literal(expected[8:12])
    offset = 12
    count, = struct.unpack_from('<I',original,4)
    for _ in range(count):
        shader = offset+260
        size, = struct.unpack_from('<I',original,offset+256)
        code = original[shader:shader+size]
        name = original[offset:offset+256].split(b'\0',1)[0].decode()
        patched = rewrite(code) if name.startswith('SLS2') and name.endswith('.vso') else code
        if patched == code:
            copy(offset,260+size)
        else:
            copy(offset,256)
            literal(struct.pack('<I',len(patched)))
            copy(shader,4)
            pos = shader+4
            for op in instructions(code):
                size_op = len(op)*4
                if not ((op[0]&65535) in (1,9) and reg(op[1])==(4,0)):
                    copy(pos,size_op)
                pos += size_op
            # Four new, direct clip-position writes before the end token.
            literal(patched[-68:-4])
            copy(pos,4)
        offset = shader+size
    assert offset==len(original)
    output = bytearray()
    delta = bytearray(b'TTWVSD01'+hashlib.sha256(original).digest()+hashlib.sha256(expected).digest())
    delta.extend(struct.pack('<II',len(expected),len(commands)))
    for pos, value in commands:
        if pos==-1:
            delta.extend(struct.pack('<II',0xffffffff,len(value))+value)
            output.extend(value)
        else:
            delta.extend(struct.pack('<II',pos,value))
            output.extend(original[pos:pos+value])
    if output!=expected:
        raise ValueError('Shader edit does not reconstruct the prepared package')
    return delta
