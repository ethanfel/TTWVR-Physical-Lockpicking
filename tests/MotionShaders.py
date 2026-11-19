#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-only
import importlib.util
from pathlib import Path
import random
import struct
import sys
import unittest
sys.dont_write_bytecode = True
spec = importlib.util.spec_from_file_location('motion', Path(__file__).parents[1] / 'tools/prepare_motion_shaders.py')
motion = importlib.util.module_from_spec(spec)
spec.loader.exec_module(motion)


def encode(ops):
    words = [0xfffe0200] + [x for op in ops for x in op] + [65535]
    return struct.pack('<' + 'I' * len(words), *words)


def example():
    # Original style: position is copied out of a temporary that is also used
    # by fog, then reused for unrelated color. Removing its calculations would
    # preserve position but corrupt the other outputs.
    return encode([
        (0x03000009, 0xc0080000, 0xa0e40003, 0x90e40000),
        *[(0x03000009, 0x80000000 | (1 << (16+i)), 0xa0e40000+i, 0x90e40000) for i in range(3)],
        (0x03000008, 0x80080000, 0x80e40000, 0x80e40000),
        (0x02000001, 0xc0070000, 0x80e40000),
        (0x02000001, 0xe0080000, 0x80ff0000),
        (0x02000001, 0x80010000, 0xa0000004),
        (0x02000001, 0xd00f0000, 0x80e40000),
    ])


def evaluate(code, inputs):
    # Independent scalar evaluator for this fixture; compare ALL outputs, not
    # just the position that the rewrite is intended to repair.
    state = {k: list(v) for k,v in inputs.items()}
    def source(t):
        key = motion.reg(t)
        values = state[key]
        return [values[(t >> (16+2*i)) & 3] for i in range(4)]
    for op in motion.instructions(code):
        opcode = op[0] & 65535
        if opcode == 1:
            value = source(op[2])
        elif opcode in (8,9):
            a,b = source(op[2]),source(op[3])
            value = [sum(a[i]*b[i] for i in range(3 if opcode == 8 else 4))] * 4
        else:
            raise AssertionError(opcode)
        key = motion.reg(op[1])
        dst = state.setdefault(key,[0.0]*4)
        for i in range(4):
            if op[1] & (1 << (16+i)):
                dst[i] = value[i]
    return {k:v for k,v in state.items() if k[0] in (4,5,6)}


class MotionShaders(unittest.TestCase):
    def test_preserves_position_fog_and_color(self):
        original=example();changed=motion.rewrite(original)
        self.assertNotEqual(original,changed)
        rng=random.Random(712)
        for _ in range(500):
            inputs={(1,0):[rng.uniform(-100,100) for _ in range(4)]}
            inputs.update({(2,n):[rng.uniform(-5,5) for _ in range(4)] for n in range(5)})
            self.assertEqual(evaluate(original,inputs),evaluate(changed,inputs))

    def test_idempotent(self):
        changed=motion.rewrite(example())
        self.assertEqual(motion.rewrite(changed),changed)

    def test_nonstandard_position_is_untouched(self):
        ops=motion.instructions(example())
        ops.append((0x02000001,0xc0010000,0xa0000007))
        code=encode(ops)
        self.assertEqual(motion.rewrite(code),code)

    def test_saturated_position_is_untouched(self):
        code=encode([(0x02000001,0xc0170000,0x80e40000)])
        self.assertEqual(motion.rewrite(code),code)

    def test_flow_and_relative_addressing_are_untouched(self):
        for op in [(0x01000028,0x80000000),(0x02000001,0xc0010000,0xa0e42000)]:
            code=encode(motion.instructions(example())+[op])
            self.assertEqual(motion.rewrite(code),code)

    def test_output_read_is_untouched(self):
        code=encode(motion.instructions(example())+[(0x02000001,0x800f0001,0xc0e40000)])
        self.assertEqual(motion.rewrite(code),code)

    def test_other_shader_model_is_untouched(self):
        code=struct.pack('<I',0xfffe0300)+example()[4:]
        self.assertEqual(motion.rewrite(code),code)

    def test_package_preserves_unrelated_entries(self):
        code=example()
        entries=[]
        for name in ('SLS2012.vso','OTHER.vso','SLS2012.pso'):
            entries.append(name.encode().ljust(256,b'\0')+struct.pack('<I',len(code))+code)
        body=b''.join(entries);raw=struct.pack('<3I',100,3,len(body))+body
        output,changes=motion.package(raw)
        self.assertEqual([x['shader'] for x in changes],['SLS2012.vso'])
        self.assertTrue(output.endswith(entries[1]+entries[2]))
        self.assertEqual(motion.package(output),(output,[]))

    def test_malformed_input_rejected(self):
        for raw in (b'',struct.pack('<3I',100,1,0),struct.pack('<3I',99,0,0)):
            with self.assertRaises(ValueError):motion.package(raw)
        for code in (example()[:-4],example()[:-1],example()+b'\0'*4):
            with self.assertRaises(ValueError):motion.rewrite(code)


if __name__=='__main__':unittest.main()
