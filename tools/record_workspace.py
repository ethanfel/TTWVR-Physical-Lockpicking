#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-only
"""Record gameplay and the VR lockpicking scene to one MP4 (no audio)."""
import argparse
from datetime import datetime
import json
import mmap
import os
from pathlib import Path
import secrets
import shutil
import signal
import struct
import subprocess
import sys
import threading
import time

HEADER_SIZE = 128
MAGIC = b'TTWVID1\0'


class Stream:
    def __init__(self, path, token):
        self.file = path.open('rb')
        self.mapping = None
        try:
            size = os.fstat(self.file.fileno()).st_size
            if size < HEADER_SIZE:
                raise ValueError('Waiting for producer header')
            self.mapping = mmap.mmap(self.file.fileno(), 0, access=mmap.ACCESS_READ)
            data = self.mapping[:HEADER_SIZE]
            width, height, fmt, self.fps = struct.unpack_from('<4I', data, 8)
            if data[:8] != MAGIC or data[48:80] != token.encode('ascii'):
                raise ValueError('Waiting for this recording session')
            if fmt != 1 or not (2 <= width <= 4096 and 2 <= height <= 2048):
                raise ValueError('Unsupported stream dimensions/format')
            if size != HEADER_SIZE + width * height * 4:
                raise ValueError('Incomplete stream buffer')
            self.width, self.height, self.token = width, height, token
            self.last_source = 0
        except Exception:
            self.close()
            raise

    def close(self):
        if self.mapping is not None:
            self.mapping.close()
        self.file.close()

    def frame(self, previous=None):
        # A producer publishes both eyes in one sequence-protected transaction.
        # Busy or overwritten reads reuse the previous video frame, never a
        # mixed pair. The game does not wait for this reader or the encoder.
        for _ in range(3):
            before = struct.unpack_from('<I', self.mapping, 24)[0]
            if before & 1:
                continue
            active, stamp, number = struct.unpack_from('<IQQ', self.mapping, 28)
            if not active or not number or number == previous:
                return None
            if self.mapping[48:80] != self.token.encode('ascii'):
                return None
            source = struct.unpack_from('<I', self.mapping, 80)[0]
            pixels = self.mapping[HEADER_SIZE:]
            after = struct.unpack_from('<I', self.mapping, 24)[0]
            if before == after and not (after & 1):
                self.last_source = source
                return number, stamp, pixels
        return None


def own_request(path, token):
    try:
        return path.read_text().split()[0] == token
    except (OSError, IndexError, UnicodeError):
        return False


def choose_encoder(ffmpeg, requested):
    if requested == 'software':
        return 'libx264', ['-preset', 'ultrafast', '-crf', '20', '-threads', '4']
    probe = [ffmpeg, '-hide_banner', '-loglevel', 'error', '-f', 'lavfi', '-i',
             'color=c=black:s=512x512:r=30', '-frames:v', '1', '-c:v', 'h264_nvenc',
             '-preset', 'p1', '-f', 'null', '-']
    try:
        available = subprocess.run(probe, stdout=subprocess.DEVNULL,
                                   stderr=subprocess.DEVNULL, timeout=15).returncode == 0
    except subprocess.TimeoutExpired:
        available = False
    if available:
        return 'h264_nvenc', ['-preset', 'p1', '-cq', '20', '-b:v', '0']
    if requested == 'nvenc':
        raise RuntimeError('NVENC probe failed; use --encoder software or auto.')
    return 'libx264', ['-preset', 'ultrafast', '-crf', '20', '-threads', '4']


def main():
    ap = argparse.ArgumentParser(description=__doc__)
    ap.add_argument('--plugins', type=Path, required=True)
    ap.add_argument('--output', type=Path, required=True)
    ap.add_argument('--seconds', type=float, default=60, help='Clip length, up to 600 seconds (default: 60)')
    ap.add_argument('--delay', type=float, default=10, help='Time to return to VR (default: 10 seconds)')
    ap.add_argument('--wait', type=float, default=45, help='Wait for the first visible VR frame')
    ap.add_argument('--fps', type=int, default=30, choices=range(10, 31))
    ap.add_argument('--eye', choices=['stereo', 'left', 'right'], default='stereo')
    ap.add_argument('--encoder', choices=['auto', 'nvenc', 'software'], default='auto')
    args = ap.parse_args()
    if not 0 < args.seconds <= 600 or not 0 <= args.delay <= 300 or not 1 <= args.wait <= 300:
        ap.error('Invalid duration, delay or first-frame wait')
    if not (args.plugins / 'TTWVRPhysicalLockpicking.dll').is_file():
        ap.error('Addon DLL not found in the live plugins folder')
    ffmpeg = shutil.which('ffmpeg')
    if not ffmpeg:
        ap.error('Install FFmpeg with libx264 or NVIDIA NVENC support first')
    request = args.plugins / 'lockpick-video.request'
    # A local exclusive lock also prevents racing two script invocations.
    lock = args.plugins / 'lockpick-video.recorder-lock'
    try:
        owner = lock.open('x')
    except FileExistsError:
        raise RuntimeError(f'Recorder lock already exists: {lock}. If the earlier recorder has exited, remove this stale lock and retry.')
    token = secrets.token_hex(16)
    owner.write(token); owner.close()
    stop = threading.Event()
    heartbeat = None
    stream = None
    proc = None
    encoder_log = None
    written = unique = repeated = 0
    source_stamps = []
    source_counts = {}
    source_transitions = []
    interrupted = False
    target = None
    def heartbeat_loop():
        while not stop.wait(1):
            if not own_request(request, token):
                return
            try:
                os.utime(request, None)
            except OSError:
                continue  # Retry a transient sharing error before the lease expires.
    try:
        if request.exists():
            raise RuntimeError(f'A recording request already exists: {request}. Stop the earlier recorder first.')
        codec, settings = choose_encoder(ffmpeg, args.encoder)
        print(f'Recording starts in {args.delay:g}s. Return to VR; gameplay and lockpicking are captured automatically. Encoder: {codec}.', flush=True)
        time.sleep(args.delay)
        with request.open('x') as f:
            f.write(f'{token} {args.fps}\n')
        heartbeat = threading.Thread(target=heartbeat_loop, daemon=True)
        heartbeat.start()
        deadline = time.monotonic() + args.wait
        frame = None
        while time.monotonic() < deadline:
            if stream is None:
                try:
                    stream = Stream(args.plugins / 'lockpick-video.bin', token)
                except (OSError, ValueError):
                    time.sleep(.1)
                    continue
            frame = stream.frame()
            if frame:
                break
            time.sleep(.02)
        if frame is None:
            raise RuntimeError('No live video frames. Use addon version 24 or newer and return focus to the game, then retry.')
        args.output.mkdir(parents=True, exist_ok=True)
        name = 'lockpick-' + datetime.now().strftime('%Y-%m-%d_%H-%M-%S_%f')
        target = args.output / (name + '-' + args.eye + '.mp4')
        encoder_log = (args.output / (name + '-encoder.log')).open('wb')
        cmd = [ffmpeg, '-hide_banner', '-loglevel', 'warning', '-filter_threads', '2',
               '-f', 'rawvideo', '-pixel_format', 'rgba', '-video_size', f'{stream.width}x{stream.height}',
               '-framerate', str(args.fps), '-i', 'pipe:0', '-an']
        if args.eye != 'stereo':
            cmd += ['-vf', 'crop=iw/2:ih:' + ('0' if args.eye == 'left' else 'iw/2') + ':0']
        cmd += ['-c:v', codec, *settings, '-pix_fmt', 'yuv420p', '-movflags', '+faststart', '-n', str(target)]
        proc = subprocess.Popen(cmd, stdin=subprocess.PIPE, stdout=subprocess.DEVNULL, stderr=encoder_log)
        print(f'Recording {args.seconds:g}s to {target}\nCtrl+C finishes the clip early. No audio is recorded.', flush=True)
        start = time.monotonic()
        previous = None
        last = frame
        total = max(1, round(args.seconds * args.fps))
        try:
            for n in range(total):
                delay = start + n / args.fps - time.monotonic()
                if delay > 0:
                    time.sleep(delay)
                fresh = stream.frame(previous)
                # Allow a just-late producer to publish before duplicating.
                # This wait affects only the recorder, never the VR process.
                source_deadline = start + (n + .5) / args.fps
                while fresh is None and time.monotonic() < source_deadline:
                    time.sleep(.001)
                    fresh = stream.frame(previous)
                if fresh:
                    last = fresh
                number, stamp, pixels = last
                if number != previous:
                    unique += 1
                    source_stamps.append(stamp)
                    source = {1: 'game', 2: 'lockpicking'}.get(stream.last_source, 'unknown')
                    source_counts[source] = source_counts.get(source, 0) + 1
                    if not source_transitions or source_transitions[-1]['source'] != source:
                        source_transitions.append(dict(source=source, frame=n, time_seconds=n/args.fps))
                else:
                    repeated += 1
                previous = number
                proc.stdin.write(pixels)
                written += 1
        except KeyboardInterrupt:
            interrupted = True
        proc.stdin.close()
        if proc.wait(timeout=30) != 0:
            raise RuntimeError('FFmpeg failed; see the saved encoder log.')
        elapsed = time.monotonic() - start
        stats = dict(file=str(target), fps=args.fps, frames=written, unique_frames=unique,
                     repeated_frames=repeated, duration_seconds=written/args.fps,
                     recording_and_encode_seconds=elapsed, encoder=codec, eye=args.eye,
                     audio=False, interrupted=interrupted, source_timestamps_ms=source_stamps,
                     source_counts=source_counts, source_transitions=source_transitions)
        target.with_suffix('.json').write_text(json.dumps(stats, indent=2) + '\n')
        print(f'Saved {target}\n{unique} fresh frames, {repeated} repeated frames; {written/args.fps:.1f}s.', flush=True)
    finally:
        stop.set()
        if heartbeat:
            heartbeat.join(timeout=2)
        if own_request(request, token):
            request.unlink(missing_ok=True)
        if stream:
            stream.close()
        if proc and proc.poll() is None:
            try:
                proc.stdin.close()
                proc.wait(timeout=15)
            except (OSError, subprocess.TimeoutExpired):
                proc.terminate()
                try:
                    proc.wait(timeout=5)
                except subprocess.TimeoutExpired:
                    proc.kill(); proc.wait()
        if encoder_log:
            encoder_log.close()
        if own_request(lock, token):
            lock.unlink(missing_ok=True)


if __name__ == '__main__':
    def interrupted(signum, frame):
        raise KeyboardInterrupt
    signal.signal(signal.SIGTERM, interrupted)
    try:
        main()
    except KeyboardInterrupt:
        sys.exit('Recording cancelled before a clip started.')
    except (OSError, RuntimeError, subprocess.TimeoutExpired) as error:
        sys.exit(str(error))
