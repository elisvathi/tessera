#!/usr/bin/env python3
"""Makes the small audio files the unit tests read. Every file is synthesised here, so they are free to use (CC0).

Run from the repository root: python3 tests/audio/make_test_audio.py. Needs ffmpeg, flac and lame for the
FLAC, AIFF and MP3 copies.
"""
import math
import os
import struct
import subprocess
import tempfile
import wave

HERE = os.path.dirname(os.path.abspath(__file__))


def write_wav(path, rate, channels, frames, extra_chunks=b""):
    """Writes 16-bit PCM; `frames` is a list of per-frame tuples of floats in -1..1."""
    data = bytearray()
    for frame in frames:
        for x in frame:
            data += struct.pack("<h", max(-32767, min(32767, int(round(x * 32767)))))
    fmt = struct.pack("<HHIIHH", 1, channels, rate, rate * channels * 2, channels * 2, 16)
    body = b"WAVE" + b"fmt " + struct.pack("<I", len(fmt)) + fmt + extra_chunks
    body += b"data" + struct.pack("<I", len(data)) + bytes(data)
    with open(path, "wb") as f:
        f.write(b"RIFF" + struct.pack("<I", len(body)) + body)


def acid_chunk(tempo):
    """An 'acid' chunk, the tempo chunk loop libraries put in WAV files."""
    payload = struct.pack("<IHHfIHHf", 0x2, 60, 0, 0.0, 8, 4, 4, tempo)
    return b"acid" + struct.pack("<I", len(payload)) + payload


def click_track(rate, bpm, seconds):
    """A 3 kHz blip on every beat, 2 ms long."""
    n = int(round(seconds * rate))
    out = [0.0] * n
    period = 60.0 / bpm
    beat = 0
    while beat * period < seconds:
        start = int(round(beat * period * rate))
        for i in range(int(0.002 * rate)):
            if start + i < n:
                out[start + i] = 0.8 * math.sin(2 * math.pi * 3000 * i / rate) * math.exp(-i / (0.0005 * rate))
        beat += 1
    return [(x,) for x in out]


def to_format(src, dst, *args):
    subprocess.run(["ffmpeg", "-loglevel", "error", "-y", "-i", src, *args, dst], check=True)


def main():
    write_wav(os.path.join(HERE, "click120.wav"), 22050, 1, click_track(22050, 120, 4.0))
    write_wav(os.path.join(HERE, "drums_92bpm.wav"), 8000, 1, click_track(8000, 92, 0.6))
    write_wav(os.path.join(HERE, "acid.wav"), 8000, 1, click_track(8000, 140, 0.3), acid_chunk(140.0))

    with tempfile.TemporaryDirectory() as tmp:
        clicks = os.path.join(tmp, "clicks.wav")
        write_wav(clicks, 22050, 1, click_track(22050, 128, 10.3))
        subprocess.run(["flac", "--silent", "-f", "-8", "-o", os.path.join(HERE, "click128.flac"), clicks], check=True)

        rate = 44100
        sine = os.path.join(tmp, "sine.wav")
        write_wav(sine, rate, 1, [(0.5 * math.sin(2 * math.pi * 1000 * i / rate),) for i in range(rate // 2)])
        to_format(sine, os.path.join(HERE, "sine1k.aiff"))

        sweep = os.path.join(tmp, "sweep.wav")
        frames = []
        for i in range(rate):
            t = i / rate
            phase = 2 * math.pi * (100 * t + (2000 - 100) * t * t / 2)
            frames.append((0.5 * math.sin(phase), 0.5 * math.sin(phase)))
        write_wav(sweep, rate, 2, frames)
        subprocess.run(["lame", "--silent", "-b", "64", sweep, os.path.join(HERE, "sweep.mp3")], check=True)

        pad = os.path.join(tmp, "pad.wav")
        r = 22050
        write_wav(pad, r, 1, [(0.2 * sum(math.sin(2 * math.pi * f * i / r) for f in (220, 277.18, 329.63)),)
                              for i in range(r * 6)])
        subprocess.run(["flac", "--silent", "-f", "-8", "-o", os.path.join(HERE, "pad.flac"), pad], check=True)

    r = 22050
    kick = []
    for i in range(int(0.4 * r)):
        t = i / r
        kick.append((0.9 * math.sin(2 * math.pi * (60 * t + 200 * (1 - math.exp(-t * 30)) / 30)) * math.exp(-t * 9),))
    write_wav(os.path.join(HERE, "kick.wav"), r, 1, kick)

    write_wav(os.path.join(HERE, "six.wav"), 8000, 6, [tuple(0.1 * (c + 1) for c in range(6))] * 400)

    with open(os.path.join(HERE, "notaudio.wav"), "w") as f:
        f.write("This is a text file with an audio file's name.\n")


if __name__ == "__main__":
    main()
