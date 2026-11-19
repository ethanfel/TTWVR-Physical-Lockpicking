# SPDX-License-Identifier: GPL-3.0-only
"""Guard against stale sessions and partially published stereo frames."""
import importlib.util
import json
import os
import shutil
import signal
import subprocess
import sys
import time
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


@unittest.skipUnless(os.name == 'posix' and shutil.which('ffmpeg') and shutil.which('ffprobe'),
                     'Real Ctrl+C process-group check needs POSIX, FFmpeg and ffprobe')
class RecorderTests(unittest.TestCase):
    def record(self, seconds=None):
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory)
            plugins = root / 'plugins'
            plugins.mkdir()
            (plugins / 'TTWVRPhysicalLockpicking.dll').touch()
            output = root / 'clips'
            cmd = [sys.executable, '-B', recorder.__file__, '--plugins', str(plugins),
                   '--output', str(output), '--delay', '0', '--wait', '5',
                   '--fps', '10', '--encoder', 'software']
            if seconds is not None:
                cmd += ['--seconds', str(seconds)]
            with (root / 'recorder.log').open('w+') as log:
                proc = subprocess.Popen(cmd, stdout=log, stderr=subprocess.STDOUT,
                                        start_new_session=True)
                try:
                    request = plugins / 'lockpick-video.request'
                    deadline = time.monotonic() + 8
                    while not request.exists() and proc.poll() is None and time.monotonic() < deadline:
                        time.sleep(.01)
                    self.assertTrue(request.exists(), 'Recorder did not request frames')
                    token = request.read_text().split()[0]
                    header = bytearray(128)
                    header[:8] = b'TTWVID1\0'
                    struct.pack_into('<4I', header, 8, 64, 32, 1, 10)
                    struct.pack_into('<IIQQ', header, 24, 2, 1, 1000, 1)
                    header[48:80] = token.encode()
                    struct.pack_into('<I', header, 80, 1)
                    # Publish one complete pair; repeating it needs no game/GPU.
                    (plugins / 'lockpick-video.bin').write_bytes(
                        header + bytes([80, 120, 160, 255]) * (64 * 32))
                    while time.monotonic() < deadline:
                        log.seek(0)
                        if 'No audio is recorded.' in log.read():
                            break
                        self.assertIsNone(proc.poll())
                        time.sleep(.01)
                    else:
                        self.fail('Recorder did not start encoding')
                    if seconds is None or seconds == 0 or seconds > 600:
                        time.sleep(.65)
                        # Terminal Ctrl+C reaches every process in this group.
                        os.killpg(proc.pid, signal.SIGINT)
                    proc.wait(timeout=10)
                    log.seek(0)
                    self.assertEqual(proc.returncode, 0, log.read())
                finally:
                    if proc.poll() is None:
                        os.killpg(proc.pid, signal.SIGTERM)
                        proc.wait(timeout=10)
            stats = json.loads(next(output.glob('*.json')).read_text())
            self.assertGreater(stats['frames'], 0)
            self.assertEqual(stats['interrupted'], seconds is None or seconds == 0 or seconds > 600)
            self.assertFalse(request.exists())
            self.assertFalse((plugins / 'lockpick-video.recorder-lock').exists())
            probe = json.loads(subprocess.check_output([
                'ffprobe', '-v', 'error', '-count_frames', '-select_streams', 'v:0',
                '-show_entries', 'stream=nb_read_frames', '-of', 'json', stats['file']]))
            self.assertEqual(int(probe['streams'][0]['nb_read_frames']), stats['frames'])
            return stats

    def test_unlimited_ctrl_c_saves_and_releases(self):
        self.record()
        self.record(0)

    def test_timed_recording_and_long_duration(self):
        self.assertEqual(self.record(.3)['frames'], 3)
        self.record(601)


if __name__ == '__main__':
    unittest.main()
