#!/usr/bin/env python3
"""The sound CedarLogic plays the very first time it opens (about 6 s),
synthesized from scratch so it's CedarLogic's own: a warm A-major-seventh pad
swelling up under a rising whoosh, a sparkling arpeggio that quickens as the
launch screen fills, then a low bloom and a bright bell chord as the icon
lands (at 4.0 s), ringing out through a long reverb as the welcome fades in.

    python3 mac/Tools/make-launch-sound.py out.wav     (needs numpy)
    afconvert -f m4af -d aac -b 160000 out.wav mac/App/FirstLaunch.m4a
"""
import sys
import wave

import numpy as np

SR = 44100
DUR = 6.6
HIT = 4.0          # the icon lands; the launch screen's bar is full
N = int(SR * DUR)
t = np.arange(N) / SR
rng = np.random.default_rng(7)


def hz(note):
    """A4 = 440; note in semitones from A4."""
    return 440.0 * 2 ** (note / 12)


def env(a, d, s, r, length, sustain_until):
    """Attack, decay to sustain, hold until sustain_until, release."""
    e = np.zeros(N)
    i0 = 0
    for start, end, v0, v1 in [(0, a, 0, 1), (a, a + d, 1, s), (a + d, sustain_until, s, s), (sustain_until, sustain_until + r, s, 0)]:
        a0, a1 = int(start * SR), min(N, int(end * SR))
        if a1 > a0:
            e[a0:a1] = np.linspace(v0, v1, a1 - a0)
    return e


def soft_saw(f, bright, detune=0.0, phase=0.0):
    """A band-limited saw whose upper harmonics come in with `bright` (0..1 per sample)."""
    out = np.zeros(N)
    for k in range(1, 12):
        if f * k > 9000:
            break
        weight = (1.0 / k) * np.clip(bright * 12 - (k - 1), 0, 1)
        out += weight * np.sin(2 * np.pi * f * (1 + detune) * k * t + phase * k)
    return out


def one_pole(x, cutoff):
    """A one-pole low-pass; cutoff may be an array (Hz)."""
    a = np.exp(-2 * np.pi * np.broadcast_to(cutoff, x.shape) / SR)
    y = np.zeros_like(x)
    acc = 0.0
    for i in range(len(x)):
        acc = (1 - a[i]) * x[i] + a[i] * acc
        y[i] = acc
    return y


L = np.zeros(N)
R = np.zeros(N)

# --- The pad: A major seventh, voiced wide, swelling in and brightening. ---
pad_notes = [-24, -17, -12, -8, -5, -1, 4]      # A2 E3 A3 C#4 E4 G#4 C#5: A major seventh, voiced wide
bright = np.clip(t / HIT, 0, 1) ** 1.5 * 0.85 + 0.1
bright[t > HIT] = 0.95 - 0.35 * np.clip((t[t > HIT] - HIT) / 2.5, 0, 1)
pad_env = env(2.6, 0.8, 0.8, 2.4, DUR, HIT + 0.2)
for i, n in enumerate(pad_notes):
    f = hz(n)
    for det, side in [(-0.004, -1), (0.0, 0), (0.0045, 1)]:
        v = soft_saw(f, bright, det, phase=i * 0.7 + det * 300)
        pan = 0.5 + 0.35 * side * (0.6 if i % 2 else 1)
        L += v * pad_env * (1 - pan) * 0.055
        R += v * pad_env * pan * 0.055

# --- Sub swell. ---
sub = np.sin(2 * np.pi * hz(-36) * t) * env(3.0, 0.5, 0.7, 2.0, DUR, HIT + 0.3) * 0.22
L += sub
R += sub

# --- The whoosh: noise through a rising low-pass, peaking into the hit. ---
noise = rng.standard_normal(N)
cut = 250 + 4200 * np.clip(t / HIT, 0, 1) ** 3
cut[t > HIT] = 4450 * np.exp(-(t[t > HIT] - HIT) * 3)
whoosh_env = np.clip(t / HIT, 0, 1) ** 2.2 * (t < HIT) + np.exp(-(t - HIT) * 5) * (t >= HIT)
nl = one_pole(one_pole(noise, cut), cut) * whoosh_env * 0.2
nr = one_pole(one_pole(rng.standard_normal(N), cut), cut) * whoosh_env * 0.2
L += nl
R += nr


def bell(start, note, amp, pan, decay=1.6):
    f = hz(note)
    i0 = int(start * SR)
    if i0 >= N:
        return
    tt = t[i0:] - start
    tone = (np.sin(2 * np.pi * f * tt) + 0.45 * np.sin(2 * np.pi * f * 2.0 * tt) * np.exp(-tt * 4)
            + 0.25 * np.sin(2 * np.pi * f * 3.01 * tt) * np.exp(-tt * 7) + 0.12 * np.sin(2 * np.pi * f * 4.2 * tt) * np.exp(-tt * 9))
    e = np.minimum(1, tt / 0.004) * np.exp(-tt / decay)
    L[i0:] += tone * e * amp * (1 - pan)
    R[i0:] += tone * e * amp * pan


# --- The arpeggio: the same chord's tones, quickening toward the hit. ---
arp = [12, 16, 19, 23, 24, 19, 16, 23, 28, 24, 31, 28]
time_ = 1.15
k = 0
while time_ < HIT - 0.12:
    gap = 0.30 - 0.19 * ((time_ - 1.15) / (HIT - 1.15))
    level = 0.035 + 0.05 * ((time_ - 1.15) / (HIT - 1.15))
    bell(time_, arp[k % len(arp)], level, 0.25 if k % 2 else 0.75, decay=0.55)
    time_ += gap
    k += 1

# --- The hit: a low bloom and a bright bell chord. ---
i0 = int(HIT * SR)
tt = t[i0:] - HIT
boom_f = 55 * (1 + 1.2 * np.exp(-tt * 18))
boom = np.sin(2 * np.pi * np.cumsum(boom_f) / SR) * np.exp(-tt * 2.2) * np.minimum(1, tt / 0.01) * 0.5
L[i0:] += boom
R[i0:] += boom
for n, amp, pan in [(12, 0.14, 0.5), (19, 0.11, 0.3), (28, 0.09, 0.7), (35, 0.06, 0.45), (40, 0.04, 0.6)]:
    bell(HIT + 0.005, n, amp, pan, decay=1.9)
# A shimmer: the chord an octave up, arriving softly just after.
for n, amp, pan in [(24, 0.05, 0.2), (31, 0.04, 0.8), (40, 0.025, 0.5)]:
    bell(HIT + 0.09, n, amp, pan, decay=1.4)

# --- Reverb: convolve with a decaying stereo noise tail. ---
ir_len = int(SR * 2.8)
it = np.arange(ir_len) / SR
ir_env = np.exp(-it * 2.3) * np.minimum(1, it / 0.02)
ir_l = rng.standard_normal(ir_len) * ir_env
ir_r = rng.standard_normal(ir_len) * ir_env
ir_l = one_pole(ir_l, 5200)
ir_r = one_pole(ir_r, 5200)
ir_l /= np.sqrt(np.sum(ir_l ** 2))
ir_r /= np.sqrt(np.sum(ir_r ** 2))


def convolve(x, h):
    n = len(x) + len(h) - 1
    size = 1 << (n - 1).bit_length()
    return np.fft.irfft(np.fft.rfft(x, size) * np.fft.rfft(h, size), size)[:len(x)]


wet_l = convolve(L, ir_l)
wet_r = convolve(R, ir_r)
L = L * 0.78 + wet_l * 0.42
R = R * 0.78 + wet_r * 0.42

# --- Master: gentle fade in, a smooth fade out, soft limit, normalize. ---
fade = np.minimum(1, t / 0.05) * np.clip((DUR - t) / 1.4, 0, 1) ** 1.5
L *= fade
R *= fade
peak = max(np.max(np.abs(L)), np.max(np.abs(R)))
L = np.tanh(L / peak * 1.25) / np.tanh(1.25) * 0.89
R = np.tanh(R / peak * 1.25) / np.tanh(1.25) * 0.89

out = sys.argv[1] if len(sys.argv) > 1 else "launch.wav"
data = (np.stack([L, R], axis=1) * 32767).astype("<i2")
with wave.open(out, "wb") as w:
    w.setnchannels(2)
    w.setsampwidth(2)
    w.setframerate(SR)
    w.writeframes(data.tobytes())
print(out)
