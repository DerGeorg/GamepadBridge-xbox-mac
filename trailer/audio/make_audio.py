#!/usr/bin/env python3
"""
The trailer's sound: a music bed and the effects, generated from scratch.

Standard library only, so it runs on any Mac without installing anything:

    python3 audio/make_audio.py

Every time comes from src/timeline.json, the same file the video reads -
scene lengths and cues (the dongle plugging in, the controller connecting,
the rumble). Change a time there, run this again, and sound and picture
still meet. Writes public/trailer-audio.wav (44.1 kHz, 16-bit stereo).

The structure follows the story: a quiet, slightly uneasy pad while the Mac
ignores the controller, a riser into the reveal, then a groove under the
demo, and a last chord on the logo.
"""

import array
import json
import math
import random
import wave
from pathlib import Path

ROOT = Path(__file__).resolve().parent.parent
TIMELINE = json.loads((ROOT / "src" / "timeline.json").read_text())

RATE = 44100
random.seed(7)  # the same noise every run, so the file only changes with the timeline

# --- the timeline in seconds ---------------------------------------------------

starts = {}
t = 0.0
for scene in TIMELINE["scenes"]:
    starts[scene["id"]] = t
    t += scene["seconds"]
LENGTH = t
CUES = TIMELINE["cues"]


def at(scene, cue):
    """Absolute time of a cue: the scene's start plus the cue's offset."""
    return starts[scene] + CUES[scene][cue]


REVEAL = at("Reveal", "hit")
OUTRO = at("Outro", "hit")

# --- the mix ---------------------------------------------------------------------

frames = int(LENGTH * RATE) + RATE // 2
left = array.array("f", bytes(4 * frames))
right = array.array("f", bytes(4 * frames))


def place(start, samples, gain=1.0, pan=0.0):
    """Mixes a mono sound in at `start` seconds; pan -1 left .. 1 right."""
    lg = gain * math.cos((pan + 1) * math.pi / 4)
    rg = gain * math.sin((pan + 1) * math.pi / 4)
    i0 = int(start * RATE)
    for i, s in enumerate(samples):
        j = i0 + i
        if 0 <= j < frames:
            left[j] += s * lg
            right[j] += s * rg


# --- oscillators: wavetables, because sin() per sample per voice is slow ------------

TABLE = 2048


def wavetable(harmonics):
    return [
        sum(a * math.sin(2 * math.pi * n * i / TABLE) for n, a in harmonics)
        for i in range(TABLE)
    ]


SINE = wavetable([(1, 1.0)])
SOFT = wavetable([(1, 1.0), (2, 0.35), (3, 0.18), (4, 0.08)])
BRIGHT = wavetable([(n, 1.0 / n) for n in range(1, 10)])


def tone(freq, seconds, table=SINE, attack=0.005, release=0.05, decay=None):
    """One note: a wavetable at `freq`, with attack, optional exponential
    decay and a release at the end."""
    n = int(seconds * RATE)
    out = [0.0] * n
    phase = 0.0
    step = freq * TABLE / RATE
    for i in range(n):
        tt = i / RATE
        env = min(1.0, tt / attack) if attack else 1.0
        if decay:
            env *= math.exp(-tt / decay)
        if tt > seconds - release:
            env *= max(0.0, (seconds - tt) / release)
        out[i] = table[int(phase) % TABLE] * env
        phase += step
    return out


def noise(seconds, cutoff_from=8000, cutoff_to=8000, highpass=False):
    """White noise through a one-pole filter whose cutoff glides; a
    high-pass is the input minus that low-pass."""
    n = int(seconds * RATE)
    out = [0.0] * n
    low = 0.0
    for i in range(n):
        cutoff = cutoff_from + (cutoff_to - cutoff_from) * i / max(1, n - 1)
        a = 1 - math.exp(-2 * math.pi * cutoff / RATE)
        x = random.uniform(-1, 1)
        low += a * (x - low)
        out[i] = x - low if highpass else low
    return out


def shaped(samples, curve):
    n = len(samples)
    return [s * curve(i / max(1, n - 1)) for i, s in enumerate(samples)]


def hz(midi):
    return 440.0 * 2 ** ((midi - 69) / 12)


# --- music -----------------------------------------------------------------------

BEAT = 60 / 112
BAR = 4 * BEAT

# A minor, F, C, G - a bar each, around and around.
CHORDS = [
    [57, 60, 64],  # Am
    [53, 57, 60],  # F
    [55, 60, 64],  # C (G in the bass of the voicing)
    [55, 59, 62],  # G
]
ROOTS = [45, 41, 48, 43]


def pad(start, end, level):
    """Soft chords, two slightly detuned voices per note, left and right."""
    bar = 0
    t0 = start
    while t0 < end:
        length = min(BAR + 0.35, end - t0 + 0.35)
        for note in CHORDS[bar % 4]:
            for detune, pan in ((0.997, -0.6), (1.003, 0.6)):
                place(t0, tone(hz(note) * detune, length, SOFT, attack=0.35, release=0.35), level, pan)
        bar += 1
        t0 += BAR


def kick(start, gain=0.9):
    n = int(0.35 * RATE)
    out = []
    phase = 0.0
    for i in range(n):
        tt = i / RATE
        freq = 45 + 75 * math.exp(-tt / 0.04)
        phase += 2 * math.pi * freq / RATE
        out.append(math.sin(phase) * math.exp(-tt / 0.12))
    place(start, out, gain)


def hat(start, gain=0.12, pan=0.3):
    place(start, shaped(noise(0.06, highpass=True, cutoff_from=7000, cutoff_to=7000), lambda p: (1 - p) ** 3), gain, pan)


def clap(start, gain=0.35):
    body = shaped(noise(0.18, 2500, 1200), lambda p: math.exp(-p * 6))
    place(start, body, gain)
    place(start, tone(190, 0.12, SINE, decay=0.04), gain * 0.5)


def bass(start, midi, seconds, gain=0.32):
    place(start, tone(hz(midi - 12), seconds, SOFT, attack=0.01, release=0.04, decay=0.5), gain)


def pluck(start, midi, gain=0.14, pan=0.0):
    place(start, tone(hz(midi), 0.45, BRIGHT, attack=0.002, decay=0.12), gain, pan)


# Act one: the Mac ignores the controller. A pad, low and a little uneasy,
# with a heartbeat-like pulse.
pad(0.0, REVEAL, 0.030)
t0 = 1.0
while t0 < REVEAL - 0.4:
    kick(t0, 0.35)
    t0 += BAR / 2

# The riser into the reveal.
riser_start = REVEAL - 1.6
place(riser_start, shaped(noise(1.6, 400, 9000), lambda p: p ** 2), 0.45)

# Act two: the groove under the demo, from the reveal to the logo.
pad(REVEAL, OUTRO, 0.045)
beat = 0
t0 = REVEAL
while t0 < OUTRO - 0.05:
    bar = beat // 4
    kick(t0, 0.75 if beat % 4 in (0, 2) else 0.55)
    if t0 >= starts["Install"]:
        hat(t0 + BEAT / 2, 0.10, 0.35 if beat % 2 else -0.35)
        if beat % 4 in (1, 3):
            clap(t0, 0.28)
    root = ROOTS[bar % 4]
    bass(t0, root, BEAT * 0.9)
    bass(t0 + BEAT / 2, root + (12 if beat % 2 else 0), BEAT * 0.4, 0.2)
    beat += 1
    t0 += BEAT

# A light arpeggio while the controllers connect.
t0 = starts["Pair"]
step = BEAT / 2
i = 0
while t0 < starts["Features"]:
    chord = CHORDS[int((t0 - REVEAL) / BAR) % 4]
    pluck(t0, chord[i % 3] + 12, 0.06, -0.4 if i % 2 else 0.4)
    i += 1
    t0 += step

# Hits: the reveal and the logo, each a kick, a crash and a chord.
for hit, chord in ((REVEAL, CHORDS[0]), (OUTRO, [57, 60, 64, 71])):
    kick(hit, 1.0)
    place(hit, shaped(noise(1.6, highpass=True, cutoff_from=3000, cutoff_to=3000), lambda p: math.exp(-p * 5)), 0.22)
    for k, note in enumerate(chord):
        place(hit, tone(hz(note), 3.5 if hit == OUTRO else 1.6, SOFT, attack=0.01, release=1.2, decay=1.5), 0.10, -0.5 + k * 0.33)
        place(hit, tone(hz(note + 12), 2.0, BRIGHT, attack=0.005, decay=0.5), 0.03, 0.5 - k * 0.33)

# --- effects -----------------------------------------------------------------------

# The music so far goes to its own pair of tracks, and the effects get fresh
# ones: the music is turned down under the effects that matter, the way a
# radio host's voice ducks the song.
music_left, music_right = left, right
left = array.array("f", bytes(4 * frames))
right = array.array("f", bytes(4 * frames))
ducks = []  # (start, end, depth)


def duck(start, end, depth=0.6):
    ducks.append((start, end, depth))


def whoosh(cut):
    """Air rushing past, peaking on a scene change."""
    place(cut - 0.35, shaped(noise(0.6, 600, 5000), lambda p: math.sin(math.pi * p) ** 2), 0.28, 0.0)


def tock(time, gain=0.35, pitch=1150):
    """A soft, woody click - a short sine with a breath of low noise,
    instead of a bright tick."""
    place(time, tone(pitch, 0.06, SINE, attack=0.001, decay=0.012), gain)
    place(time, shaped(noise(0.02, 2500, 1500), lambda p: (1 - p) ** 2), gain * 0.25)


def thud(time, gain=0.6):
    """Something set down: a low tone that drops in pitch."""
    out = []
    phase = 0.0
    for i in range(int(0.25 * RATE)):
        tt = i / RATE
        phase += 2 * math.pi * (160 - 90 * min(1, tt / 0.1)) / RATE
        out.append(math.sin(phase) * math.exp(-tt / 0.06))
    place(time, out, gain)


def marimba(time, midi, gain=0.3, pan=0.0, length=1.4):
    """A struck bar: a round fundamental, plus the bright overtones of the
    strike (about 3.9 and 9.2 times the pitch) that die away within
    milliseconds. Warm, never shrill."""
    f = hz(midi)
    n = int(length * RATE)
    out = [0.0] * n
    for i in range(n):
        tt = i / RATE
        env = min(1.0, tt / 0.002)
        out[i] = env * (
            math.sin(2 * math.pi * f * tt) * math.exp(-tt / 0.45)
            + 0.22 * math.sin(2 * math.pi * f * 3.93 * tt) * math.exp(-tt / 0.045)
            + 0.07 * math.sin(2 * math.pi * f * 9.2 * tt) * math.exp(-tt / 0.012)
        )
    place(time, out, gain, pan)


def bell(time, midi, gain=0.12, pan=0.0, length=2.2):
    """A glassy bell by FM synthesis: the overtones fade faster than the
    tone, so it starts bright and settles soft."""
    f = hz(midi)
    n = int(length * RATE)
    out = [0.0] * n
    for i in range(n):
        tt = i / RATE
        index = 2.2 * math.exp(-tt / 0.25)
        mod = index * math.sin(2 * math.pi * f * 3.5 * tt)
        out[i] = math.sin(2 * math.pi * f * tt + mod) * math.exp(-tt / 0.7) * min(1.0, tt / 0.003)
    place(time, out, gain, pan)


def chime(time, notes, gain=0.3):
    """Something worked: a rising marimba arpeggio with a soft bell above."""
    for k, note in enumerate(notes):
        pan = -0.35 + 0.7 * k / max(1, len(notes) - 1)
        marimba(time + k * 0.085, note, gain, pan)
    bell(time + 0.085 * (len(notes) - 1), notes[-1] + 12, gain * 0.35, 0.2)


def reverb(left_in, right_in, wet=0.22):
    """A small room: four damped comb filters and two all-passes per
    channel (Schroeder). Softens the effects so they sit in the music
    instead of on top of it."""
    def channel(samples, offset):
        n = len(samples)
        out = [0.0] * n
        for delay_ms in (29.7, 37.1, 41.1, 43.7):
            d = int((delay_ms + offset) * RATE / 1000)
            buf = [0.0] * d
            low = 0.0
            k = 0
            for i in range(n):
                y = buf[k]
                low += 0.35 * (y - low)  # damping: the tail gets darker
                buf[k] = samples[i] + low * 0.74
                out[i] += y
                k += 1
                if k == d:
                    k = 0
        for delay_ms, g in ((5.0, 0.7), (1.7, 0.7)):
            d = int(delay_ms * RATE / 1000)
            buf = [0.0] * d
            k = 0
            for i in range(n):
                b = buf[k]
                x = out[i]
                y = -g * x + b
                buf[k] = x + g * y
                out[i] = y
                k += 1
                if k == d:
                    k = 0
        return out

    wl = channel(left_in, 0.0)
    wr = channel(right_in, 1.3)  # a slightly different room each side: width
    for i in range(len(left_in)):
        left_in[i] += wl[i] * wet * 0.25
        right_in[i] += wr[i] * wet * 0.25


def rumble(start, end, gain=0.4):
    """The controller's motors: a low growl that throbs."""
    n = int((end - start) * RATE)
    out = []
    p1 = p2 = 0.0
    for i in range(n):
        tt = i / RATE
        p1 += 2 * math.pi * 48 / RATE
        p2 += 2 * math.pi * 73 / RATE
        throb = 0.6 + 0.4 * math.sin(2 * math.pi * 9 * tt)
        fade = min(1.0, tt / 0.05, (n / RATE - tt) / 0.08)
        out.append((math.sin(p1) + 0.5 * math.copysign(1, math.sin(p2)) * 0.6) * throb * fade)
    place(start, out, gain)


for scene in TIMELINE["scenes"][1:]:
    whoosh(starts[scene["id"]])

thud(at("Problem", "plug"), 0.7)
tock(at("Problem", "plug"), 0.45, 900)

tock(at("Install", "grab"), 0.35)
thud(at("Install", "drop"), 0.6)
marimba(at("Install", "drop"), 57, 0.32)  # A3, landing in the folder
duck(at("Install", "drop") - 0.05, at("Install", "drop") + 0.3, 0.5)
for cue, note in (("permission1", 76), ("permission2", 81)):  # E5, then A5
    tock(at("Install", cue), 0.3)
    marimba(at("Install", cue) + 0.02, note, 0.38, 0.2)
    duck(at("Install", cue) - 0.03, at("Install", cue) + 0.4, 0.45)

tock(at("Pair", "iconIn"), 0.25)
tock(at("Pair", "menuOpen"), 0.4)
tock(at("Pair", "pairClick"), 0.4)
chime(at("Pair", "connected"), [72, 76, 79, 84], 0.42)  # C5 E5 G5 C6
duck(at("Pair", "connected") - 0.05, at("Pair", "connected") + 1.2, 0.7)

chime(at("Xbox360", "ring"), [67, 72, 76], 0.42)  # G4 C5 E5
duck(at("Xbox360", "ring") - 0.05, at("Xbox360", "ring") + 1.0, 0.7)
rumble(at("Xbox360", "rumble"), at("Xbox360", "rumbleEnd"), 0.5)
duck(at("Xbox360", "rumble"), at("Xbox360", "rumbleEnd"), 0.5)

for k, cue in enumerate(CUES["Features"]["cards"]):
    marimba(starts["Features"] + cue, [69, 72, 76, 81][k], 0.28, -0.3 + k * 0.2)

# The effects get the room; the music has its own.
reverb(left, right)

# --- master -------------------------------------------------------------------------

# How far the music is turned down at each sample: ramps of 60 ms either
# side of each duck, so it dips rather than drops.
gain = array.array("f", [1.0]) * frames
ramp = 0.06
for start, end, depth in ducks:
    for i in range(max(0, int((start - ramp) * RATE)), min(frames, int((end + ramp) * RATE))):
        tt = i / RATE
        edge = min(1.0, (tt - (start - ramp)) / ramp, ((end + ramp) - tt) / ramp)
        gain[i] = min(gain[i], 1 - depth * max(0.0, edge))

# Mix, fade the last second and a half, soften peaks, normalise to -1 dBFS.
fade_from = LENGTH - 1.5
for i in range(frames):
    left[i] += music_left[i] * gain[i]
    right[i] += music_right[i] * gain[i]

for i in range(frames):
    tt = i / RATE
    g = 1.0
    if tt > fade_from:
        g = max(0.0, (LENGTH - tt) / 1.5)
    if tt < 0.4:
        g *= tt / 0.4
    left[i] = math.tanh(left[i] * 1.6) * g
    right[i] = math.tanh(right[i] * 1.6) * g

peak = max(max(abs(x) for x in left), max(abs(x) for x in right)) or 1.0
scale = 0.89 / peak

pcm = array.array("h", bytes(4 * frames))
for i in range(frames):
    pcm[2 * i] = int(left[i] * scale * 32767)
    pcm[2 * i + 1] = int(right[i] * scale * 32767)

target = ROOT / "public" / "trailer-audio.wav"
with wave.open(str(target), "wb") as out:
    out.setnchannels(2)
    out.setsampwidth(2)
    out.setframerate(RATE)
    out.writeframes(pcm.tobytes())

print(f"{target.relative_to(ROOT)}: {LENGTH:.1f} s, peak normalised from {peak:.2f}")
