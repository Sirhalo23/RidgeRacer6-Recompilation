#!/usr/bin/env python3
"""Writes assets/achievement.wav: the sound the game plays when an achievement
is unlocked.

An original two-note chime made here from sine waves; it is not a recording or
a copy of any console's sound. Players who want another sound put their own
achievement.wav next to the launcher (see the README).

usage: make_chime.py <output.wav>
"""
import math
import struct
import sys
import wave

RATE = 44100
LENGTH = 1.5  # seconds


def bell(frequency, start, loudness, decay):
    """One struck note: a few partials, the higher ones dying away sooner."""
    partials = [(1.0, 1.0, 1.0), (2.0, 0.42, 1.6), (3.01, 0.20, 2.4), (4.2, 0.10, 3.5), (5.43, 0.05, 5.0)]
    def sample(t):
        t -= start
        if t < 0:
            return 0.0
        attack = min(1.0, t / 0.004)
        return loudness * attack * sum(
            level * math.exp(-t * decay * faster) * math.sin(2 * math.pi * frequency * ratio * t)
            for ratio, level, faster in partials)
    return sample


def main(path):
    notes = [bell(783.99, 0.00, 0.50, 5.0),    # G5
             bell(1174.66, 0.13, 0.55, 4.2),   # D6, a fifth above
             bell(1567.98, 0.26, 0.16, 6.0)]   # a quiet G6 on top
    count = int(RATE * LENGTH)
    dry = [sum(note(i / RATE) for note in notes) for i in range(count)]
    # A little room: two quiet echoes.
    wet = list(dry)
    for delay, level in ((0.083, 0.22), (0.171, 0.12)):
        shift = int(delay * RATE)
        for i in range(shift, count):
            wet[i] += dry[i - shift] * level
    fade = int(0.25 * RATE)
    for i in range(fade):
        wet[count - 1 - i] *= i / fade
    peak = max(abs(v) for v in wet)
    scale = 0.5 * 32767 / peak   # half of full scale: it plays over the game's own sound
    with wave.open(path, 'wb') as out:
        out.setnchannels(1)
        out.setsampwidth(2)
        out.setframerate(RATE)
        out.writeframes(b''.join(struct.pack('<h', int(v * scale)) for v in wet))


if __name__ == '__main__':
    if len(sys.argv) != 2:
        sys.exit(__doc__)
    main(sys.argv[1])
