# SPDX-License-Identifier: GPL-3.0-only
"""Guard against stale sessions and partially published stereo frames."""
import importlib.util
from pathlib import Path
import struct
import tempfile
import unittest

spec = importlib.util.spec_from_file_location('recorder', Path(__file__).parents[1] / 'tools/record_workspace.py')
recorder = importlib.util.module_from_spec(spec)
spec.loader.exec_module(recorder)


class StreamTests(unittest.TestCase):
    def test_session_and_atomic_stereo(self):
        with tempfile.TemporaryDirectory() as directory:
            path = Path(directory) / 'video.bin'
            token = 'a' * 32
            header = bytearray(128)
            header[:8] = b'TTWVID1\0'
            struct.pack_into('<4I', header, 8, 4, 2, 1, 30)
            struct.pack_into('<IIQQ', header, 24, 2, 1, 1000, 1)
            header[48:80] = token.encode()
            struct.pack_into('<I', header, 80, 1)
            # Each row has a red left eye and blue right eye.
            pixels = (bytes([255, 0, 0, 255]) * 2 + bytes([0, 0, 255, 255]) * 2) * 2
            path.write_bytes(header + pixels)
            with self.assertRaises(ValueError):
                recorder.Stream(path, 'b' * 32)
            stream = recorder.Stream(path, token)
            try:
                self.assertEqual(stream.frame(), (1, 1000, pixels))
                self.assertEqual(stream.last_source, 1)
                self.assertIsNone(stream.frame(previous=1))
                with path.open('r+b', buffering=0) as writer:
                    writer.seek(24); writer.write(struct.pack('<I', 3))
                    writer.seek(128); writer.write(bytes([0]) * 8)
                    writer.seek(80); writer.write(struct.pack('<I', 2))
                    self.assertIsNone(stream.frame())  # Writer has only half a new frame.
                    self.assertEqual(stream.last_source, 1)  # Partial source transition is rejected too.
                    writer.seek(128); writer.write(pixels)
                    writer.seek(32); writer.write(struct.pack('<QQ', 1033, 2))
                    writer.seek(24); writer.write(struct.pack('<I', 4))
                    self.assertEqual(stream.frame(previous=1), (2, 1033, pixels))
                    self.assertEqual(stream.last_source, 2)
                    writer.seek(24); writer.write(struct.pack('<I', 5))
                    writer.seek(80); writer.write(struct.pack('<I', 1))
                    writer.seek(32); writer.write(struct.pack('<QQ', 1066, 3))
                    writer.seek(24); writer.write(struct.pack('<I', 6))
                    self.assertEqual(stream.frame(previous=2), (3, 1066, pixels))
                    self.assertEqual(stream.last_source, 1)
                    writer.seek(28); writer.write(struct.pack('<I', 0))
                    self.assertIsNone(stream.frame())
            finally:
                stream.close()

    def test_incomplete_or_oversized_stream(self):
        with tempfile.TemporaryDirectory() as directory:
            path = Path(directory) / 'video.bin'
            path.write_bytes(b'')
            with self.assertRaises(ValueError):
                recorder.Stream(path, 'a' * 32)
            header = bytearray(128)
            header[:8] = b'TTWVID1\0'
            header[48:80] = b'a' * 32
            struct.pack_into('<4I', header, 8, 99999, 99999, 1, 30)
            path.write_bytes(header)
            with self.assertRaises(ValueError):
                recorder.Stream(path, 'a' * 32)


if __name__ == '__main__':
    unittest.main()
