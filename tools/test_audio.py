#!/usr/bin/env python3
"""Audio regression test + demo renderer.

Simulates exactly what the firmware plays for a target approaching from 8 m
to 0.3 m over ~11.5 s: the real clip data from ../audio_clips.h, the pinger's
cadence/pitch curves and gain from pinger.cpp. Writes the result to
../docs/audio_demo.wav (listenable proof of the sound design), then measures
it and checks the numbers against the reference-scene measurements:

  far ping   ~1675 Hz, ~1250 ms apart
  close ping ~2150 Hz, ~150 ms apart

Run from tools/:  python3 test_audio.py
Exits non-zero if any check fails. Keep the constants below in sync with
pinger.cpp when tuning the sound.
"""
import re, wave, array, math, os, sys

SR = 16000

# --- pinger.cpp constants (keep in sync) ---
GAIN = 0.175
IMIN, IMAX = 150, 1250
RFAR, RCLOSE = 0.807, 1.036
DMIN, DMAX = 300.0, 8000.0

HERE = os.path.dirname(os.path.abspath(__file__))

def load_clip(name):
    src = open(os.path.join(HERE, "..", "audio_clips.h")).read()
    m = re.search(name + r"\[\d+\] = \{(.*?)\};", src, re.S)
    return [int(v) for v in re.findall(r"-?\d+", m.group(1))]

def simulate(contact, dur=12.5, approach_s=11.5):
    buf = [0.0] * int(SR * dur)
    t_ms, next_ping = 0.0, 0.0
    while t_ms < dur * 1000:
        d = max(DMIN, DMAX - (DMAX - DMIN) * (t_ms / (approach_s * 1000)))
        t = (min(max(d, DMIN), DMAX) - DMIN) / (DMAX - DMIN)
        interval = IMIN + t * (IMAX - IMIN)
        rate = RCLOSE - (t * t) * (RCLOSE - RFAR)   # front-loaded t^2 pitch
        if t_ms >= next_ping:
            s0 = int(SR * t_ms / 1000)
            pos = 0.0
            while True:
                i = int(pos)
                if i >= len(contact) - 1: break
                k = s0 + int(pos / rate)
                if k >= len(buf): break
                fr = pos - i
                buf[k] += (contact[i] * (1 - fr) + contact[i + 1] * fr) * GAIN
                pos += rate
            next_ping = t_ms + interval
        t_ms += 5
    return buf

def domfreq(mono, ms0, ms1):
    seg = mono[int(SR * ms0 / 1000):int(SR * ms1 / 1000)]
    best, bf = 0, 0
    for f in range(1200, 3200, 25):
        c = 2 * math.cos(2 * math.pi * f / SR); s1 = s2 = 0.0
        for x in seg: s0 = x + c * s1 - s2; s2 = s1; s1 = s0
        p = s1 * s1 + s2 * s2 - c * s1 * s2
        if p > best: best, bf = p, f
    return bf

def main():
    contact = load_clip("kClipContact")
    buf = simulate(contact)
    out = array.array('h', [max(-32768, min(32767, int(v))) for v in buf])
    dst = os.path.normpath(os.path.join(HERE, "..", "docs", "audio_demo.wav"))
    w = wave.open(dst, "wb"); w.setnchannels(1); w.setsampwidth(2); w.setframerate(SR)
    w.writeframes(out.tobytes()); w.close()
    print(f"wrote {dst}")

    mono = list(out)
    win = SR // 100
    env = [math.sqrt(sum(x * x for x in mono[i:i + win]) / win)
           for i in range(0, len(mono) - win, win)]
    pk = max(env); th = pk * 0.45
    on, prev = [], 0.0
    for i, e in enumerate(env):
        if e > th and prev <= th: on.append(i * 10)
        prev = e
    first_gap = on[1] - on[0]
    last_gap = on[-1] - on[-2]
    f_far = domfreq(mono, on[0], on[0] + 200)
    f_close = domfreq(mono, on[-1], on[-1] + 120)

    checks = [
        ("far ping pitch",  f_far,     1675, 40, "Hz"),
        ("close ping pitch", f_close,  2150, 40, "Hz"),
        ("far interval",    first_gap, 1250, 60, "ms"),
        ("close interval",  last_gap,  150,  20, "ms"),
    ]
    ok = True
    for name, got, want, tol, unit in checks:
        good = abs(got - want) <= tol
        ok &= good
        print(f"  {'PASS' if good else 'FAIL'}  {name}: {got} {unit} (want {want} +/- {tol})")
    print("OK" if ok else "FAILED")
    sys.exit(0 if ok else 1)

if __name__ == "__main__":
    main()
